/* tests/test_kernel_iminmax.c — M42, K27. `abs`, `smin`, `smax`, `umin`,
 * `umax` — the integer intrinsics Bennett's extractor expands into
 * icmp + select (and a `sub` for abs), instructions.jl:4782-4825.
 *
 * WRITTEN BEFORE src/kernels/iminmax*.c EXISTED (Rule 10): the first build of
 * this binary failed to link on every cq_kernel_* / cq_imm_* symbol.
 *
 * THE SUBJECT OF THIS SUITE IS THE PREDICATE TABLE, BECAUSE NOTHING ELSE CAN
 * SEE IT. `umax` and `umin` differ only by an operand swap in the compare, and
 * `smax`/`umax` only by `slt` vs `ult` — the same gate SHAPE at every width.
 * Swap one and the count, the palindrome, the pool and every golden stay green
 * (bd `only-l1-catches-a-wrong-icmp-predicate-row`). So L1 is written against a
 * reference that shares NOTHING with the circuit: packed uint64_t, explicit
 * sign extension, the C comparison operators, and the anchors that separate
 * each op from its neighbour (`-1` vs `0` is where smin and umin disagree).
 */

#include "kernels/iminmax.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/mux.h"
#include "reg.h"
#include "scratch.h"
#include "shadow.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/goldens.h"
#include "support/harness.h"
#include "support/kerneldrv.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <stdio.h>

/* ---- L1's oracle. Plain C, W <= 64, NOTHING derived from the circuit. ---- */

static uint64_t im_mask(int W) { return W >= 64 ? ~0ull : ((1ull << W) - 1u); }

/* Explicit sign extension: a W-bit pattern whose bit W-1 is set is negative,
 * so the bits above W are filled with ones before the int64_t comparison. */
static int64_t im_sext(uint64_t v, int W)
{
    v &= im_mask(W);
    if (W < 64 && ((v >> (W - 1)) & 1u)) v |= ~im_mask(W);
    return (int64_t)v;
}

static uint64_t ref_umax(uint64_t a, uint64_t b, int W)
{ a &= im_mask(W); b &= im_mask(W); return a > b ? a : b; }
static uint64_t ref_umin(uint64_t a, uint64_t b, int W)
{ a &= im_mask(W); b &= im_mask(W); return a < b ? a : b; }
static uint64_t ref_smax(uint64_t a, uint64_t b, int W)
{ return (im_sext(a, W) > im_sext(b, W) ? a : b) & im_mask(W); }
static uint64_t ref_smin(uint64_t a, uint64_t b, int W)
{ return (im_sext(a, W) < im_sext(b, W) ? a : b) & im_mask(W); }

/* |a| mod 2^W: negate in unsigned arithmetic (defined wrap), so INT_MIN maps to
 * itself — LLVM's `llvm.abs(x, false)`, the total function the ABI lowers. */
static uint64_t ref_abs(uint64_t a, int W)
{
    return (im_sext(a, W) < 0 ? (0u - a) : a) & im_mask(W);
}

/* ---- The anchors (PRD-v2 §7.12's provider contract). -------------------- */

static uint64_t im_int_min(int W) { return 1ull << (W - 1); }
static uint64_t im_int_max(int W) { return im_int_min(W) - 1u; }

/* Six pairs, each chosen to separate two neighbours:
 *   (5,5)            equal operands — the tie; either arm is the value
 *   (INT_MIN,INT_MAX) and its mirror — min vs max, and signed vs unsigned
 *   (-1, 0) and (0, -1) — THE smin/umin separator: -1 <s 0 but -1 >u 0
 *   (INT_MIN, 0)     a single-bit difference in the sign lane */
enum { IM_N_BIN_ANCHORS = 6, IM_N_ABS_ANCHORS = 6 };

static void im_bin_anchor(int W, int i, uint64_t *a, uint64_t *b)
{
    const uint64_t m = im_mask(W);

    switch (i) {
    case 0:  *a = 5u & m;         *b = 5u & m;         break;
    case 1:  *a = im_int_min(W);  *b = im_int_max(W);  break;
    case 2:  *a = im_int_max(W);  *b = im_int_min(W);  break;
    case 3:  *a = m;              *b = 0u;             break;
    case 4:  *a = 0u;             *b = m;              break;
    default: *a = im_int_min(W);  *b = 0u;             break;
    }
}

/* abs(INT_MIN) wraps; abs(0); abs(-1); abs(1); abs(INT_MAX); abs(INT_MIN+1). */
static uint64_t im_abs_anchor(int W, int i)
{
    const uint64_t m = im_mask(W);

    switch (i) {
    case 0:  return im_int_min(W);
    case 1:  return 0u;
    case 2:  return m;
    case 3:  return 1u & m;
    case 4:  return im_int_max(W);
    default: return (im_int_min(W) + 1u) & m;
    }
}

static int im_bin_anchors(int W, int i, cq_ref_w *v)
{
    uint64_t a, b;

    if (i < 0) return IM_N_BIN_ANCHORS;
    if (i >= IM_N_BIN_ANCHORS) return 0;
    im_bin_anchor(W, i, &a, &b);
    v[0] = cq_ref_w_make(a, 0u, W);
    v[1] = cq_ref_w_make(b, 0u, W);
    return 1;
}

static int im_abs_anchors(int W, int i, cq_ref_w *v)
{
    if (i < 0) return IM_N_ABS_ANCHORS;
    if (i >= IM_N_ABS_ANCHORS) return 0;
    v[0] = cq_ref_w_make(im_abs_anchor(W, i), 0u, W);
    return 1;
}

/* ---- The five specs. ---------------------------------------------------- */

static void bin_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->anchors = im_bin_anchors;
}

static void abs_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->n_src   = 1;
    out->w[0]    = W;
    out->w[1]    = 0;
    out->anchors = im_abs_anchors;
}

static void call_abs(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                     const cq_kd_shape *sh)
{
    cq_kernel_abs(ctx, dst, src[0], sh->w[0]);
}

static cq_ref_w refn_abs(const cq_ref_w *s, const cq_kd_shape *sh)
{
    return cq_ref_w_make(ref_abs(s[0].lo, sh->w[0]), 0u, sh->w[0]);
}

static const cq_kd_spec IM_UMAX = { "umax", cq_kernel_umax, ref_umax, bin_shape, NULL, NULL };
static const cq_kd_spec IM_UMIN = { "umin", cq_kernel_umin, ref_umin, bin_shape, NULL, NULL };
static const cq_kd_spec IM_SMAX = { "smax", cq_kernel_smax, ref_smax, bin_shape, NULL, NULL };
static const cq_kd_spec IM_SMIN = { "smin", cq_kernel_smin, ref_smin, bin_shape, NULL, NULL };
static const cq_kd_spec IM_ABS  = { "abs",  NULL, NULL, abs_shape, call_abs, refn_abs };

/* Indexed by cq_imm_op, so the slot fixtures can name a program and its spec
 * by one number. */
static const cq_kd_spec *const IM_SPECS[CQ_IMM_N_OP] = {
    &IM_UMAX, &IM_UMIN, &IM_SMAX, &IM_SMIN, &IM_ABS
};

_Static_assert(CQ_IMM_UMAX == 0 && CQ_IMM_ABS == 4 && CQ_IMM_N_OP == 5,
               "IM_SPECS is indexed by cq_imm_op");

/* ---- The oracle separates each op from its neighbour. Reads no kernel. -- */

/* AN ANCHOR THAT NO NEIGHBOUR DISAGREES ON IS NOT AN ANCHOR. At every width,
 * each named pair of ops must differ on at least one anchor — otherwise a
 * kernel wired as its neighbour would pass every forced case. */
CQ_TEST(the_anchors_separate_each_op_from_its_neighbour)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64 };

    for (size_t w = 0; w < sizeof WS / sizeof WS[0]; w++) {
        int W = WS[w];
        int s_u_min = 0, s_u_max = 0, mn_mx_u = 0, mn_mx_s = 0, abs_id = 0;

        for (int i = 0; i < IM_N_BIN_ANCHORS; i++) {
            uint64_t a, b;

            im_bin_anchor(W, i, &a, &b);
            s_u_min |= ref_smin(a, b, W) != ref_umin(a, b, W);
            s_u_max |= ref_smax(a, b, W) != ref_umax(a, b, W);
            mn_mx_u |= ref_umin(a, b, W) != ref_umax(a, b, W);
            mn_mx_s |= ref_smin(a, b, W) != ref_smax(a, b, W);
        }
        for (int i = 0; i < IM_N_ABS_ANCHORS; i++) {
            uint64_t a = im_abs_anchor(W, i);

            abs_id |= ref_abs(a, W) != a;
        }
        CHECK(s_u_min); CHECK(s_u_max); CHECK(mn_mx_u); CHECK(mn_mx_s);
        /* At W == 1 every value is its own absolute value (0 and -1 == 1). */
        CHECK(W == 1 ? !abs_id : abs_id);

        /* The pins themselves: abs wraps at INT_MIN, -1 vs 0 splits the
         * families, and the tie returns the shared value. */
        CHECK_EQ(ref_abs(im_int_min(W), W), im_int_min(W));
        CHECK_EQ(ref_abs(im_mask(W), W), 1u & im_mask(W));
        CHECK_EQ(ref_smin(im_mask(W), 0u, W), im_mask(W));
        CHECK_EQ(ref_umin(im_mask(W), 0u, W), 0u);
    }
}

/* ---- L1 + L2 + L3 + L5: the shared constant sample budget. -------------- */

/* The standard ladder {1,2,3,4,5,8,16,32,64} — the ABI's i8/i16/i32/i64 plus
 * the narrow widths where a sign lane is also the first lane. Enumerated. */
CQ_TEST(sweep_umax) { cq_kd_sweep(&IM_UMAX); }
CQ_TEST(sweep_umin) { cq_kd_sweep(&IM_UMIN); }
CQ_TEST(sweep_smax) { cq_kd_sweep(&IM_SMAX); }
CQ_TEST(sweep_smin) { cq_kd_sweep(&IM_SMIN); }
CQ_TEST(sweep_abs)  { cq_kd_sweep(&IM_ABS);  }

/* THE ANCHORS AT THE ALL-QUANTUM MASK TOO. The sampler replays anchors at the
 * all-classical row (the R9 fold, not the circuit) and the two mixed rows; the
 * all-quantum row is the one L4 pins and the one where the -1-vs-0 anchor
 * reaches the compare block with no operand folded. Forced here explicitly. */
CQ_TEST(the_anchors_at_the_all_quantum_mask)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64 };

    for (size_t w = 0; w < sizeof WS / sizeof WS[0]; w++) {
        cq_bk_pair pairs[160];
        int W = WS[w];
        uint32_t np = cq_bk_fixed_pairs((uint32_t)W, pairs, 160u);

        CHECK(np > 1u);
        CHECK(cq_ref_w_eq(pairs[1].q[0], cq_ref_w_ones(W)));
        CHECK(cq_ref_w_eq(pairs[1].q[1], cq_ref_w_ones(W)));

        for (int op = 0; op < CQ_IMM_N_OP; op++) {
            cq_kd_shape sh;
            int n;

            IM_SPECS[op]->shape(W, &sh);
            n = sh.anchors(W, -1, NULL);
            for (int i = 0; i < n; i++) {
                cq_ref_w v[CQ_KD_MAX_SRC] = { cq_ref_w_zero(), cq_ref_w_zero(),
                                              cq_ref_w_zero() };

                CHECK(sh.anchors(W, i, v));
                cq_kd_case(IM_SPECS[op], W, v, &pairs[1]);
            }
        }
    }
}

/* ---- Step 20: the four §9 regions, and the promotion identity. ---------- */

static void im_narrow(void)
{
    for (int W = 1; W <= 4; W++)
        for (int op = 0; op < CQ_IMM_N_OP; op++)
            cq_kd_sample_at(IM_SPECS[op], W);
}

CQ_TEST(controlled)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64 };
    uint64_t reached = 0u;

    cq_kd_for_each_region("iminmax", im_narrow);
    for (int op = 0; op < CQ_IMM_N_OP; op++)
        for (size_t w = 0; w < sizeof WS / sizeof WS[0]; w++)
            reached += cq_kd_check_promotion(IM_SPECS[op], WS[w]);
    CHECK(reached > 0u);
}

#include "test_kernel_iminmax_slots.inc"
#include "test_kernel_iminmax_spans.inc"
#include "test_kernel_iminmax_l4.inc"

CQ_TEST_MAIN_ARGV(
    CQ_CASE(the_anchors_separate_each_op_from_its_neighbour),
    CQ_CASE(the_blocks_cost_what_their_modules_say_they_cost),
    CQ_CASE(each_kernel_is_twice_the_sum_of_its_blocks_plus_the_copyout),
    CQ_CASE(the_slot_boundaries_match_an_independent_four_valued_scan),
    CQ_CASE(the_rows_spans_are_pairwise_disjoint),
    CQ_CASE(the_compute_half_is_a_palindrome_at_an_asymmetric_mask),
    CQ_CASE(two_blocks_in_one_region_do_not_collide),
    CQ_CASE(sweep_umax),
    CQ_CASE(sweep_umin),
    CQ_CASE(sweep_smax),
    CQ_CASE(sweep_smin),
    CQ_CASE(sweep_abs),
    CQ_CASE(the_anchors_at_the_all_quantum_mask),
    CQ_CASE(controlled),
    CQ_CASE(l4_goldens),
    CQ_CASE(the_closed_forms_in_k27_match_what_is_emitted),
    CQ_CASE(dst_owns_w_qubits_and_the_scratch_comes_back)
)
