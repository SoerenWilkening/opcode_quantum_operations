/* tests/test_kernel_ibits.c — M41, K26. ctpop / ctlz / cttz / bitreverse /
 * bswap, the integer intrinsics Bennett expands in its extractor
 * (instructions.jl:4826-4946). docs/constructions/K26.md is the spec.
 *
 * THREE FILES, ON THE SEAMS K26.md §7 RECORDED IN ADVANCE: this one holds the
 * shared driver's levels (L1 + L2 + L3 + L5 through cq_kd_sample_at, L4's
 * goldens, the scratch claim, the controlled axis); test_kernel_ibits_slots.inc
 * holds the COMPOSITION identity and the four-valued SLOT SCAN; and
 * test_kernel_ibits_spans.inc holds the LAYOUT — span disjointness, the
 * palindrome at a mixed mask and two programs in one region. The two .inc
 * files read NO golden, which is what makes a blessed golden answerable.
 *
 * THE ORACLE IS INDEPENDENT OF THE KERNEL (Rule 10, the Step 18 trap). It is
 * the compiler's own popcount / clz / ctz with LLVM's zero rule spelled out,
 * and plain loops for the two reversals — nothing it computes goes through
 * ibits.c's rows, the expansion's `select` cascade, or the library's R9
 * short-circuit. Packed uint64_t is allowed HERE (Rule 3's reference
 * exception), which caps every value-checking width at 64.
 */

#include "kernels/ibits.h"

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

/* ---- The oracle. -------------------------------------------------------- */

/* LLVM LangRef: ctlz / cttz of 0 is the bit width when is_zero_poison is
 * false, and the expansion returns W whatever the flag says. */
static uint64_t or_ctpop(uint64_t v, int W)
{
    return (uint64_t)__builtin_popcountll(v & cq_ref_mask(W));
}

static uint64_t or_ctlz(uint64_t v, int W)
{
    v &= cq_ref_mask(W);
    return v == 0u ? (uint64_t)W : (uint64_t)(__builtin_clzll(v) - (64 - W));
}

static uint64_t or_cttz(uint64_t v, int W)
{
    v &= cq_ref_mask(W);
    return v == 0u ? (uint64_t)W : (uint64_t)__builtin_ctzll(v);
}

static uint64_t or_bitreverse(uint64_t v, int W)
{
    uint64_t r = 0u;

    for (int i = 0; i < W; i++)
        if ((v >> i) & 1u) r |= UINT64_C(1) << (W - 1 - i);
    return r;
}

static uint64_t or_bswap(uint64_t v, int W)
{
    uint64_t r = 0u;
    int nb = W / 8;

    for (int b = 0; b < nb; b++)
        r |= ((v >> (8 * b)) & 0xFFu) << (8 * (nb - 1 - b));
    return r;
}

typedef uint64_t (*ib_oracle)(uint64_t, int);
typedef void (*ib_kernel)(cq_ctx *, cq_bit *, const cq_bit *, int);

static const ib_oracle ORACLE[CQ_IBF_N] = {
    or_ctpop, or_ctlz, or_cttz, or_bitreverse, or_bswap
};
static const ib_kernel KERNEL[CQ_IBF_N] = {
    cq_kernel_ctpop, cq_kernel_ctlz, cq_kernel_cttz, cq_kernel_bitreverse,
    cq_kernel_bswap
};

/* ---- The specs: unary, one width, value corners as ANCHORS. ------------- */

/* 0, all-ones, the single top bit and the single low bit — FORCED into every
 * width's draw at three mask rows (kerneldrv.h). ctlz and cttz are the pair
 * the corners separate: 0 hits the `W` seed, the top bit is ctlz's 0 and
 * cttz's W-1, the low bit the mirror. */
static int ib_anchors(int W, int i, cq_ref_w *v)
{
    if (i < 0) return 4;
    switch (i) {
    case 0:  v[0] = cq_ref_w_zero();          return 1;
    case 1:  v[0] = cq_ref_w_ones(W);         return 1;
    case 2:  v[0] = cq_ref_w_setbit(W - 1);   return 1;
    case 3:  v[0] = cq_ref_w_setbit(0);       return 1;
    default: return 0;
    }
}

static void ib_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->n_src = 1;
    out->w[0] = W;
    out->w_dst = W;
    out->anchors = ib_anchors;
}

#define IB_ADAPT(fn, F)                                                        \
    static void call_##fn(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,  \
                          const cq_kd_shape *sh)                               \
    { cq_kernel_##fn(ctx, dst, src[0], sh->w[0]); }                            \
    static cq_ref_w ref_##fn(const cq_ref_w *s, const cq_kd_shape *sh)         \
    { return cq_ref_w_make(ORACLE[F](s[0].lo, sh->w[0]), 0u, sh->w[0]); }

IB_ADAPT(ctpop, CQ_IBF_CTPOP)
IB_ADAPT(ctlz, CQ_IBF_CTLZ)
IB_ADAPT(cttz, CQ_IBF_CTTZ)
IB_ADAPT(bitreverse, CQ_IBF_BITREVERSE)
IB_ADAPT(bswap, CQ_IBF_BSWAP)

static const cq_kd_spec SPEC[CQ_IBF_N] = {
    { "ctpop",      NULL, NULL, ib_shape, call_ctpop,      ref_ctpop      },
    { "ctlz",       NULL, NULL, ib_shape, call_ctlz,       ref_ctlz       },
    { "cttz",       NULL, NULL, ib_shape, call_cttz,       ref_cttz       },
    { "bitreverse", NULL, NULL, ib_shape, call_bitreverse, ref_bitreverse },
    { "bswap",      NULL, NULL, ib_shape, call_bswap,      ref_bswap      },
};

/* The widths each kernel is swept and pinned at. The ABI's are i8..i64
 * (intrinsic_table.yaml:102-137), bswap i16..i64; the narrow odd widths are
 * where the expansion is still defined and a loop bound or an MSB edge moves.
 * bswap's only defined widths are multiples of 16, so its ladder adds 48. */
static const int WIDTHS[]       = { 1, 2, 3, 4, 5, 8, 16, 32, 64 };
static const int BSWAP_WIDTHS[] = { 16, 32, 48, 64 };
enum { N_WIDTHS = 9, N_BSWAP_WIDTHS = 4 };

static int ladder(cq_ibits_fn f, const int **ws)
{
    if (f == CQ_IBF_BSWAP) { *ws = BSWAP_WIDTHS; return N_BSWAP_WIDTHS; }
    *ws = WIDTHS;
    return N_WIDTHS;
}

/* One kernel call on an explicit (value, mask) into a mock; returns dst. */
static cq_ref_w ib_run(cq_ibits_fn f, int W, cq_ref_w v, cq_ref_w q,
                       cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_ref_w got;
    int32_t ha, hd;

    cq_ctx_init(&ctx, &s);
    ha = cq_bk_reg_w(&ctx, (uint32_t)W, v, q);
    hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);
    cq_mock_reset(m);                   /* drop the rail's materialising Xs */
    KERNEL[f](&ctx, cq_reg_bits(&ctx.regs, hd), cq_reg_cbits(&ctx.regs, ha), W);
    got = cq_pc_value_w(&ctx, hd);
    cq_ctx_dispose(&ctx);
    return got;
}

/* ---- L1 + L2 + L3 + L5: the shared constant sample budget. -------------- */

CQ_TEST(ibits_sweep_narrow_widths)
{
    for (int f = 0; f < CQ_IBF_BSWAP; f++)
        for (int W = 1; W <= 5; W++) cq_kd_sample_at(&SPEC[f], W);
}

CQ_TEST(ibits_sweep_abi_widths)
{
    for (int f = 0; f < CQ_IBF_N; f++) {
        const int *ws;
        int n = ladder((cq_ibits_fn)f, &ws);

        for (int i = 0; i < n; i++)
            if (ws[i] >= 8) cq_kd_sample_at(&SPEC[f], ws[i]);
    }
}

/* Step 20's gate: the same levels under §9's four regions at the cheap
 * widths, and the promotion identity at every pinned width. */
static void ib_narrow(void)
{
    for (int f = 0; f < CQ_IBF_BSWAP; f++)
        for (int W = 1; W <= 4; W++) cq_kd_sample_at(&SPEC[f], W);
    cq_kd_sample_at(&SPEC[CQ_IBF_BSWAP], 16);
}

CQ_TEST(controlled)
{
    uint64_t reached = 0u;

    cq_kd_for_each_region("ibits", ib_narrow);
    for (int f = 0; f < CQ_IBF_N; f++) {
        const int *ws;
        int n = ladder((cq_ibits_fn)f, &ws);

        for (int i = 0; i < n; i++)
            reached += cq_kd_check_promotion(&SPEC[f], ws[i]);
    }
    CHECK(reached > 0u);
}

/* ---- L4: the goldens, at the all-quantum mask, in Release. -------------- */

CQ_TEST(l4_goldens)
{
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/ibits.counts",
                      "M41 kernels/ibits*.c — K26 integer intrinsics "
                      "(sandwiched extractor expansions)",
                      "ALL-QUANTUM on the one source, values all-ones, at "
                      "ctrl_depth 0",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (int f = 0; f < CQ_IBF_N; f++) {
        const int *ws;
        int n = ladder((cq_ibits_fn)f, &ws);

        for (int i = 0; i < n; i++) {
            cq_counter fwd, unc;

            cq_kd_measure(&SPEC[f], ws[i], &fwd, &unc);
            CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
            CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);
            cq_gold_check(&g, SPEC[f].name, "forward", ws[i],
                          fwd.x, fwd.cx, fwd.ccx);
            cq_gold_check(&g, SPEC[f].name, "unc", ws[i],
                          unc.x, unc.cx, unc.ccx);
        }
    }
    CHECK(cq_gold_close(&g));
}

/* ---- The scratch: the whole program region, and it all comes back. ------ */

/* Under I6(b) every bit of the region becomes a qubit whatever the mask, so
 * the peak is dst's W plus EXACTLY the region the program asks for — asked of
 * the step machine, which asks M14/M16/M17. `owned` is W: L2 after the call
 * cannot see scratch that was taken and tidily released; this can. */
CQ_TEST(the_scratch_is_the_programs_region_and_it_all_comes_back)
{
    for (int f = 0; f < CQ_IBF_N; f++) {
        const int *ws;
        int n = ladder((cq_ibits_fn)f, &ws);

        for (int i = 0; i < n; i++) {
            cq_ibits_row rows[CQ_IBITS_MAX_ROWS];
            int W = ws[i], nr = cq_ibits_program((cq_ibits_fn)f, W, rows);
            uint32_t peak = 0u, owned = cq_kd_peak(&SPEC[f], W, &peak);

            CHECK_EQ(owned, (uint32_t)W);
            CHECK_EQ(peak, (uint32_t)W + cq_ibits_region(rows, nr, W));
            printf("# %-10s W=%2d rows %3d slots %6d region %6u qubits\n",
                   SPEC[f].name, W, nr, cq_ibits_steps(rows, nr, W),
                   cq_ibits_region(rows, nr, W));
        }
    }
    fflush(stdout);
}

/* ---- R9 into a quantum dst: real Xs, and still no scratch. -------------- */

/* L5 in the sweep sees a CLASSICAL dst, where the fold rewrites constants for
 * zero gates. A dst already on qubits must get a real X per set result lane —
 * and still no sandwich, so the stream holds X and nothing else. */
CQ_TEST(a_classical_rail_into_a_quantum_dst_is_xs_and_no_scratch)
{
    cq_mock m;

    cq_mock_init(&m);
    for (int f = 0; f < CQ_IBF_N; f++) {
        int W = (f == CQ_IBF_BSWAP) ? 16 : 8;
        uint64_t va = 0x1234u & cq_ref_mask(W), want = ORACLE[f](va, W);
        cq_ctx ctx;
        cq_sink s = cq_mock_sink(&m);
        int32_t ha, hd;
        cq_pc_snap before, after;

        cq_ctx_init(&ctx, &s);
        ha = cq_bk_reg_w(&ctx, (uint32_t)W, cq_ref_w_make(va, 0u, W),
                         cq_ref_w_zero());
        hd = cq_bk_reg_w(&ctx, (uint32_t)W, cq_ref_w_zero(), cq_ref_w_ones(W));
        before = cq_pc_take(&ctx);
        cq_mock_reset(&m);
        KERNEL[f](&ctx, cq_reg_bits(&ctx.regs, hd), cq_reg_cbits(&ctx.regs, ha),
                  W);
        after = cq_pc_take(&ctx);

        CHECK_EQ(cq_pc_value_w(&ctx, hd).lo, want);
        CHECK_EQ((uint64_t)cq_mock_count(&m), (uint64_t)__builtin_popcountll(want));
        CHECK_EQ((uint64_t)cq_mock_count_op(&m, CQ_OP_X), (uint64_t)cq_mock_count(&m));
        CHECK_EQ(after.peak, before.peak);
        cq_ctx_dispose(&ctx);
    }
    cq_mock_dispose(&m);
}

#include "test_kernel_ibits_slots.inc"
#include "test_kernel_ibits_spans.inc"

CQ_TEST_MAIN_ARGV(
    CQ_CASE(the_blocks_cost_what_their_modules_say_they_cost),
    CQ_CASE(each_program_is_the_sum_of_its_blocks),
    CQ_CASE(the_program_tables_are_well_formed_at_every_width),
    CQ_CASE(the_slot_boundaries_match_an_independent_four_valued_scan),
    CQ_CASE(the_programs_spans_are_pairwise_disjoint),
    CQ_CASE(the_compute_half_is_a_palindrome_at_a_mixed_mask),
    CQ_CASE(two_programs_in_one_region_do_not_collide),
    CQ_CASE(ibits_sweep_narrow_widths),
    CQ_CASE(ibits_sweep_abi_widths),
    CQ_CASE(controlled),
    CQ_CASE(l4_goldens),
    CQ_CASE(the_scratch_is_the_programs_region_and_it_all_comes_back),
    CQ_CASE(a_classical_rail_into_a_quantum_dst_is_xs_and_no_scratch)
)
