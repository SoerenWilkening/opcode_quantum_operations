/* tests/test_kernel_fadd.c — M33, K15. `fadd` and `fsub` at f64: the second
 * floating-point kernel of libcqops and the first fp ARITHMETIC one. PRD-v2
 * §5, §7; docs/constructions/K15.md.
 *
 * THE SUBJECT OF THIS FILE IS THE SLOT ARITHMETIC AND THE PORT'S OWN ROWS.
 * Whether `lower_eq!`, `lower_ult!`, `lower_sub!`, `lower_add!`, `lower_mux!`
 * and the barrel compute the right bits is M14's, M16's, M17's and M12's
 * suites next door; whether `(ea == 0x7FF) & (fa != 0)` is the right NaN test
 * is M31's; whether `_sf_normalize_clz`, `_sf_handle_subnormal` and
 * `_sf_round_and_pack` are right is M32's. What is new here is (i) which of
 * fadd.jl's 124 operator occurrences each row transcribes and in what order,
 * (ii) which SLOT each of 133 rows occupies, (iii) that `fsub` is fsub.jl's
 * own body and not `fadd(a, fneg(b))`, and (iv) that the G/R/S word handed to
 * M32 means what fadd.jl:13-14 says it means. Every one of the four is
 * invisible to a gate count.
 *
 * SO L1'S ORACLE IS THE HOST `+` / `-`, WITH §7.4's THREE REACHABLE CELLS
 * PINNED BY TABLE — and it shares nothing with the port. fadd_eval.c evaluates
 * its own transcription of the Julia body over `uint64_t` for the classical
 * short-circuit (PRD-v2 §7.4); if this reference did the same it would agree
 * with a wrong row rather than catch it (the Step 18 finding). The bits go
 * into a `double` through fphost's memcpy and the verdict comes from C's `+`
 * and `-`.
 *
 * AND UNLIKE `fcmp`, ASKING THE HOST IS NOT SAFE ON ITS OWN. M36's own note
 * says why: IEEE specifies the result of every COMPARISON on every input, so
 * that oracle reads the same on x86 and arm64. An ARITHMETIC result does not —
 * `Inf - Inf` and the two NaN-payload orders are x86's choices — so the three
 * cells `fadd` reaches are asserted as LITERALS from tests/support/fphost.h
 * before the host is consulted, exactly as PRD-v2 §7.4 requires
 * ("the cells above are asserted as literals so an arm64 box reaches the same
 * verdict"). The other two of §7.4's five (`0 * Inf`, `sqrt(-1)`) are
 * unreachable from this kernel.
 *
 * SLOTS AND GATES COME APART, AS THEY DO IN M31, M32 AND M36 (D-K18-6). A
 * block operand is a view over a rail with a constant fill, or a constant
 * span, so the §3 fold table elides gates at EVERY operand mask including the
 * all-quantum one L4 pins. Every composition identity below is over SLOTS, and
 * the slot scan's prediction is four-valued — NONE / X / CX / CCX — walked
 * with a separate stream cursor.
 *
 * WIDTH 64 AND NOTHING ELSE, DELIBERATELY, AND IT IS A REFUSAL RATHER THAN A
 * GAP. PRD-v2 §1 scopes v2 to `f64` and `soft_fadd` is `(UInt64, UInt64)`, so
 * a second width would be a fiction; the kernels hard-error on one in both
 * configurations and tests/test_kernel_fadd_death.c drives that for both.
 *
 * ONE CIRCUIT CASE IS STILL LARGE: a sandwiched call is ~47,800 compute slots
 * twice over. The L1 sweep therefore keeps the shared 32-case budget and uses
 * eight representative anchors; the complete table stays in the cheap
 * classical oracle case.
 */

#include "kernels/fadd.h"
#include "kernels/fadd_int.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/fpfield.h"
#include "kernels/fpround.h"
#include "kernels/mux.h"
#include "kernels/shift_var.h"
#include "reg.h"
#include "scratch.h"
#include "shadow.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/fpanchors.h"
#include "support/fphost.h"
#include "support/goldens.h"
#include "support/harness.h"
#include "support/kerneldrv.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void check_cached_prefix_map(cq_fadd_prog p)
{
    const cq_fa_map *m = cq_fa_map_get(p);
    int row = 0, start = 0;

    for (int u = 0; u < m->steps; u++) {
        int got, local, n;

        for (;;) {
            n = cq_fa_row_steps(&m->rows[row]);
            if (n > 0 && u < start + n) break;
            start += n;
            row++;
        }
        got = cq_fa_row_at(m, u, &local);
        CHECK_EQ(got, row);
        CHECK_EQ(local, u - start);
    }
}

CQ_TEST(the_cached_prefix_maps_match_a_linear_dispatch_at_every_slot)
{
    check_cached_prefix_map(CQ_FADD_PROG_ADD);
    check_cached_prefix_map(CQ_FADD_PROG_SUB);
}

/* ---- L1's oracle: the host operator, three cells pinned by table. -------- */

/* NOTHING HERE IS DERIVED FROM THE PORT. The NaN and Inf rows are LITERALS
 * from tests/support/fphost.h — §7.4's own table — and the predicates that
 * select them are the HOST's `isnan`/`isinf`/`signbit` rather than a second
 * copy of `(ea == 0x7FF) & (fa != 0)`. Everything else is C's `+` and `-`.
 *
 * THE THREE CELLS, AND WHY EACH ONE IS A LITERAL RATHER THAN A HOST CALL.
 *   - a NaN operand: `_sf_propagate_nan2` (softfloat_common.jl:23-24) returns
 *     the FIRST NaN operand with QUIET_BIT forced, preserving sign and
 *     payload. x86 agrees; arm64's default NaN is positive and its priority
 *     differs, so the row is pinned.
 *   - `Inf - Inf` in either spelling: `INDEF = 0xFFF8000000000000`
 *     (softfloat_common.jl:14, Intel SDM Vol 1 §4.8.3.7). NEGATIVE, which is
 *     the single cheapest way for this oracle to notice it is on an arm64 box.
 *   - `fsub`'s NaN-`b`: the sign is NOT flipped (fsub.jl:8-12). Expressed here
 *     by testing `b` BEFORE any negation, which is the oracle's independent
 *     statement of the same requirement. */
static int fa_is_nan(uint64_t u) { return isnan(cq_fphost_from_bits(u)) != 0; }
static int fa_is_inf(uint64_t u) { return isinf(cq_fphost_from_bits(u)) != 0; }
static int fa_sign  (uint64_t u) { return signbit(cq_fphost_from_bits(u)) != 0; }

static uint64_t host_fadd(uint64_t a, uint64_t b)
{
    if (fa_is_nan(a)) return a | CQ_FPHOST_QUIET_BIT;
    if (fa_is_nan(b)) return b | CQ_FPHOST_QUIET_BIT;
    if (fa_is_inf(a) && fa_is_inf(b) && fa_sign(a) != fa_sign(b))
        return CQ_FPHOST_INDEF;
    return cq_fphost_bits(cq_fphost_from_bits(a) + cq_fphost_from_bits(b));
}

static uint64_t host_fsub(uint64_t a, uint64_t b)
{
    if (fa_is_nan(a)) return a | CQ_FPHOST_QUIET_BIT;
    if (fa_is_nan(b)) return b | CQ_FPHOST_QUIET_BIT;   /* sign NOT flipped */
    if (fa_is_inf(a) && fa_is_inf(b) && fa_sign(a) == fa_sign(b))
        return CQ_FPHOST_INDEF;
    return cq_fphost_bits(cq_fphost_from_bits(a) - cq_fphost_from_bits(b));
}

/* ---- §7.12's anchors, composed. ----------------------------------------- */

#include "test_kernel_fadd_anchors.inc"

/* ---- The two kernels, as one table. ------------------------------------- */

static int fadd_circuit_anchors(int W, int i, cq_ref_w *v)
{
    const int available = fadd_anchors(W, -1, NULL);
    const int picked = cq_fp_representative_binary_index(available, i);

    if (i < 0) return cq_fp_representative_count(available);
    return picked >= 0 ? fadd_anchors(W, picked, v) : 0;
}

static void fadd_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->n_src   = 2;
    out->w[0]    = CQ_FP64_W;
    out->w[1]    = CQ_FP64_W;
    out->w_dst   = CQ_FP64_W;
    out->anchors = fadd_circuit_anchors;
}

static void call_fadd(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                      const cq_kd_shape *sh)
{ cq_kernel_fadd(ctx, dst, src[0], src[1], sh->w[0]); }

static void call_fsub(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                      const cq_kd_shape *sh)
{ cq_kernel_fsub(ctx, dst, src[0], src[1], sh->w[0]); }

static cq_ref_w ref_fadd(const cq_ref_w *s, const cq_kd_shape *sh)
{ (void)sh; return cq_ref_w_make(host_fadd(s[0].lo, s[1].lo), 0u, CQ_FP64_W); }

static cq_ref_w ref_fsub(const cq_ref_w *s, const cq_kd_shape *sh)
{ (void)sh; return cq_ref_w_make(host_fsub(s[0].lo, s[1].lo), 0u, CQ_FP64_W); }

typedef struct {
    cq_kd_spec   spec;
    cq_fadd_prog prog;
    uint64_t   (*host)(uint64_t, uint64_t);
    uint64_t   (*eval)(uint64_t, uint64_t);
} fadd_krow;

static const fadd_krow ROWS[] = {
    { { "fadd", NULL, NULL, fadd_shape, call_fadd, ref_fadd },
      CQ_FADD_PROG_ADD, host_fadd, cq_fadd_eval },
    { { "fsub", NULL, NULL, fadd_shape, call_fsub, ref_fsub },
      CQ_FADD_PROG_SUB, host_fsub, cq_fsub_eval }
};

enum { N_FADD_ROWS = 2 };

_Static_assert(sizeof ROWS / sizeof ROWS[0] == (size_t)N_FADD_ROWS,
               "M33 ships exactly two Rule 7 kernels");

/* ---- One forward call, at an explicit value and mask pair. -------------- */

static uint64_t fadd_run(const fadd_krow *r, uint64_t va, uint64_t vb,
                         cq_ref_w qa, cq_ref_w qb, cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_kd_shape sh;
    int32_t ha, hb, hd;
    const cq_bit *src[2];
    uint64_t got;

    fadd_shape(CQ_FP64_W, &sh);
    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);

    ha = cq_bk_reg_w(&ctx, (uint32_t)CQ_FP64_W,
                     cq_ref_w_make(va, 0u, CQ_FP64_W), qa);
    hb = cq_bk_reg_w(&ctx, (uint32_t)CQ_FP64_W,
                     cq_ref_w_make(vb, 0u, CQ_FP64_W), qb);
    hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)CQ_FP64_W);
    src[0] = cq_reg_cbits(&ctx.regs, ha);
    src[1] = cq_reg_cbits(&ctx.regs, hb);

    cq_mock_reset(m);                   /* drop the rails' materialising Xs */
    r->spec.call(&ctx, cq_reg_bits(&ctx.regs, hd), src, &sh);
    got = cq_pc_value(&ctx, hd);
    cq_ctx_dispose(&ctx);
    return got;
}

/* ---- L1 + L2 + L3 + L5. ------------------------------------------------- */

CQ_TEST(l1_sweep)
{
    for (int i = 0; i < N_FADD_ROWS; i++)
        cq_kd_sweep_at(&ROWS[i].spec, CQ_FP64_W);
}

/* ---- Step 20's four regions, and §9's gate-tuple transform. -------------- */

/* THE REGION BODY IS A HANDFUL OF EXPLICIT CASES AND NOT THE SWEEP, because
 * kerneldrv.h's instruction — "`body` should be the suite's sweep at its CHEAP
 * widths only" — has no cheap width to offer here: M33 has exactly one, and
 * one sandwiched call is ~95,600 slots. What the region sweep adds over
 * `cq_kd_check_promotion` is the axis's interaction with the §3 fold table
 * across a real L1 case, including the ALL-CLASSICAL row at CQ_KD_CTRL_Q0 that
 * no other fixture in the project reaches; four value tuples at three masks
 * carry that, and the promotion itself is per gate and width-independent. */
static void fadd_narrow(void)
{
    static const uint64_t V[][2] = {
        { CQ_F64_ONE,      CQ_F64_ONE            },   /* d == 0, overflow  */
        { CQ_F64_ONE,      CQ_F64_NEG(FA_TWO)    },   /* swap, mixed signs */
        { CQ_F64_QNAN_A,   CQ_F64_ONE            },   /* the NaN chain     */
        { CQ_F64_ONE,      CQ_F64_HALF_ULP_OF_1  }    /* the tie           */
    };
    cq_bk_pair pairs[CQ_FP64_W + 12];
    uint32_t np = cq_bk_fixed_pairs((uint32_t)CQ_FP64_W, pairs,
                                    (uint32_t)(CQ_FP64_W + 12));

    for (int i = 0; i < N_FADD_ROWS; i++)
        for (int v = 0; v < 4; v++) {
            cq_ref_w vv[CQ_KD_MAX_SRC];

            vv[0] = cq_ref_w_make(V[v][0], 0u, CQ_FP64_W);
            vv[1] = cq_ref_w_make(V[v][1], 0u, CQ_FP64_W);
            vv[2] = cq_ref_w_zero();
            cq_kd_case(&ROWS[i].spec, CQ_FP64_W, vv, &pairs[0]);  /* classical */
            cq_kd_case(&ROWS[i].spec, CQ_FP64_W, vv, &pairs[1]);  /* quantum   */
        }
    CHECK(np >= 2u);
}

CQ_TEST(controlled)
{
    uint64_t reached = 0u;

    cq_kd_for_each_region("fadd/fsub anchors", fadd_narrow);

    for (int i = 0; i < N_FADD_ROWS; i++)
        reached += cq_kd_check_promotion(&ROWS[i].spec, CQ_FP64_W);

    /* NOT VACUOUS: the promotion identity holds trivially where the
     * uncontrolled tuple is empty, so the caller owns the non-vacuity claim
     * (cq_kd_check_promotion returns the uncontrolled total for this reason). */
    CHECK(reached > 0u);
}

/* ---- L4: the goldens, at the all-quantum mask. -------------------------- */

static void check_counts(cq_gold *g, const fadd_krow *r)
{
    cq_counter fwd, unc;

    cq_kd_measure(&r->spec, CQ_FP64_W, &fwd, &unc);

    CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
    CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);

    cq_gold_check(g, r->spec.name, "forward", CQ_FP64_W, fwd.x, fwd.cx, fwd.ccx);
    cq_gold_check(g, r->spec.name, "unc",     CQ_FP64_W, unc.x, unc.cx, unc.ccx);
}

CQ_TEST(l4_goldens)
{
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/fadd.counts",
                      "M33 kernels/fadd.c — K15 IEEE binary64 add and "
                      "subtract (sandwiched)",
                      "ALL-QUANTUM on `a` and `b`, whose width is the W column "
                      "(f64 only). The views' constant lanes, the constant "
                      "spans and M31's and M32's own views fold at EVERY mask, "
                      "so these are well below twice the slot count",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (int i = 0; i < N_FADD_ROWS; i++) check_counts(&g, &ROWS[i]);

    CHECK(cq_gold_close(&g));
}

#include "test_kernel_fadd_prog.inc"
#include "test_kernel_fadd_slots.inc"
#include "test_kernel_fadd_scan.inc"
#include "test_kernel_fadd_spans.inc"

CQ_TEST_MAIN_ARGV(
    CQ_CASE(the_two_programs_are_well_formed_and_reference_only_backwards),
    CQ_CASE(fsub_is_fadds_body_under_a_prologue_and_b_is_rebased),
    CQ_CASE(fsub_differs_from_fadd_of_fneg_on_exactly_the_nan_rhs_sign),
    CQ_CASE(the_classical_row_agrees_with_the_host_on_every_anchor),
    CQ_CASE(the_three_unspecified_cells_are_the_pinned_literals),
    CQ_CASE(the_working_format_handed_to_m32_is_grs_in_bits_2_1_0),
    CQ_CASE(the_d8_band_is_discarded_and_the_select_that_discards_it_is_live),
    CQ_CASE(the_blocks_cost_what_their_modules_say_they_cost),
    CQ_CASE(each_program_is_the_sum_of_its_rows),
    CQ_CASE(the_cached_prefix_maps_match_a_linear_dispatch_at_every_slot),
    CQ_CASE(the_slot_boundaries_match_an_independent_four_valued_scan),
    CQ_CASE(the_programs_spans_are_pairwise_disjoint),
    CQ_CASE(the_compute_half_is_a_palindrome_at_an_asymmetric_mask),
    CQ_CASE(two_fsub_blocks_in_one_region_do_not_collide),
    CQ_CASE(dst_owns_its_lanes_and_the_scratch_comes_back),
    CQ_CASE(l1_sweep),
    CQ_CASE(controlled),
    CQ_CASE(l4_goldens)
)
