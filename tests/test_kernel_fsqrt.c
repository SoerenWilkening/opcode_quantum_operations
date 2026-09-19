/* tests/test_kernel_fsqrt.c — M40, K21. `soft_fsqrt` at f64, the first UNARY
 * floating-point kernel of libcqops and the first with a LOOP in it.
 * PRD-v2 §5, §7; docs/constructions/K21.md.
 *
 * THE SUBJECT OF THIS FILE IS THE SLOT ARITHMETIC ACROSS SIXTY-FOUR IDENTICAL
 * ITERATIONS. Whether `lower_eq!`, `lower_ult!`, `lower_add!`, `lower_sub!`
 * and `lower_mux!` compute the right bits is M14's, M16's and M17's suites
 * next door, whether the two shared softfloat helpers do is M32's, and whether
 * `(ea == 0x7FF) & (fa != 0)` is the right NaN test is M31's. What is new here
 * is (i) which SLOT each of 1,070 rows occupies, (ii) which BITS each of them
 * owns — 64 comparators, 65 subtractors and 136 muxes at as many offsets in
 * one region — and (iii) whether the digit recurrence's four CARRIED values
 * chain from the right iteration. Every one of the three is invisible to a
 * gate count, and K21.md's risk 1 is that an off-by-one inside one iteration
 * is ABSORBED BY THE NEXT and keeps emitting plausible gates for the remaining
 * sixty-three.
 *
 * SO L1'S ORACLE IS THE HOST `sqrt()` AND SHARES NOTHING WITH THE PORT —
 * EXCEPT ON THE CELLS IEEE LEAVES UNSPECIFIED, WHICH ARE PINNED BY TABLE. That
 * is PRD-v2 §7.4's instruction, and `sqrt` is the one fp operator where the
 * host is a GOOD oracle on the specified cells: IEEE 754 §5.4.1 requires it to
 * be correctly rounded, and x86's `sqrtsd` is. It is also the one operator
 * where `bd host-fp-operator-commutes-so-the-nan-payload-is-not-an-oracle`
 * does NOT reach, because there is no second operand for a compiler to
 * exchange. What the host does NOT specify is `sqrt` of a negative — every
 * such input is upstream's `INDEF` (fsqrt.jl:115) and the host's NaN payload
 * there is its own business — and the NaN passthrough, where upstream
 * preserves the sign and the payload and force-quietens an sNaN. `ref_fsqrt`
 * below answers those two rows from a TABLE and calls the host only where IEEE
 * specifies the result.
 *
 * SLOTS AND GATES COME APART, AS THEY DO IN M31, M32, M33, M34 AND M36
 * (D-K18-6), AND HERE THE FOLD PATTERN ALSO MOVES WITH THE ITERATION INDEX:
 * `a_lo` is a chain of `<< 2` views and is entirely constant zero from t = 32,
 * because the radicand is being consumed. Every composition identity below is
 * over SLOTS, and the slot scan's prediction is four-valued — NONE / X / CX /
 * CCX — walked with a separate stream cursor.
 *
 * WIDTH 64 AND NOTHING ELSE, DELIBERATELY, AND IT IS A REFUSAL RATHER THAN A
 * GAP. PRD-v2 §1 scopes v2 to `f64` and `soft_fsqrt` is `(UInt64)`, so a
 * second width would be a fiction; the kernel hard-errors on one in both
 * configurations and tests/test_kernel_fsqrt_death.c drives that.
 *
 * THIS SUITE IS SLOW AND THAT IS NOT A REGRESSION (PRD-v2 §7.13). 157,108
 * compute-half slots at ~229k gates per kernel call, times a forced anchor
 * block of 33 singles at three mask rows.
 */

#include "kernels/fsqrt.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/fpfield.h"
#include "kernels/fpround.h"
#include "kernels/mux.h"
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

enum { W64 = CQ_FP64_W };

/* ---- L1's oracle: the host `sqrt`, with §7.4's cells pinned by table. ---- */

static int fs_is_nan(uint64_t u)
{
    return ((u >> 52) & CQ_FP64_EXP_ALL) == CQ_FP64_EXP_ALL
        && (u & CQ_F64_MANT_MASK) != 0u;
}

/* A NEGATIVE value that is not `-0`: `fsqrt.jl:111`'s `a_neg & !a_zero`. */
static int fs_is_neg_nonzero(uint64_t u)
{
    return (u >> 63) != 0u && (u & ~CQ_F64_SIGN_MASK) != 0u;
}

/* NOTHING HERE IS DERIVED FROM src/kernels/fsqrt_eval.c. The two pinned rows
 * are literals — upstream's NaN passthrough (fsqrt.jl:116, sign and payload
 * preserved, sNaN force-quietened per Bennett-r84x / U08) and fphost.h's
 * measured INDEF — and everything else is the host operator, which IEEE fully
 * specifies for `sqrt`. An oracle that re-transcribed the Julia would agree
 * with a wrong port rather than catch it (the Step 18 trap).
 *
 * THE ORDER MATTERS AND IT IS THE SAME ORDER THE SOURCE'S SELECT CHAIN USES:
 * `a_nan` is tested FIRST here because it fires LAST there, and a NEGATIVE
 * NaN satisfies both rows. Swap them and every negative NaN becomes INDEF —
 * which is precisely the port defect anchor `FSQ_NEG_QNAN` exists to catch,
 * so an oracle with the same defect would agree with it. */
static uint64_t ref_fsqrt(uint64_t a, uint64_t b, int W)
{
    (void)b; (void)W;
    if (fs_is_nan(a))          return a | CQ_F64_QUIET_BIT;
    if (fs_is_neg_nonzero(a))  return CQ_FPHOST_INDEF;
    return cq_fphost_bits(sqrt(cq_fphost_from_bits(a)));
}

/* ---- §7.12's anchors, composed. ----------------------------------------- */

#include "test_kernel_fsqrt_anchors.inc"

/* ---- The spec. ---------------------------------------------------------- */

/* UNARY, ONE WIDTH (PRD-v2 §7.11), so `n_src` is 1 and the spec supplies a
 * `call` adapter — `cq_kd_shape_of` REFUSES a shape the default path cannot
 * serve, because that path passes `w_dst` as the kernel's `W` and reads
 * `src[1]` (bd zwh). `w_dst` is 64 here and not 1, which is where K21 differs
 * from M31's one-bit predicates. */
static void fsqrt_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->n_src   = 1;
    out->w[0]    = W64;
    out->w_dst   = W64;
    out->anchors = fsqrt_anchors;

    /* THE FLOOR IS READ OFF THE PROVIDER JUST INSTALLED, NOT WRITTEN DOWN
     * (bd 9ve.32). The sampler forces anchors row-major over three mask rows
     * after reserving slots 0 and 1, so `3 x anchors + 8` is the smallest
     * budget at which NO row is dropped, and it tracks the table in the .inc
     * with no edit here. */
    out->min_samples = 3 * out->anchors(W, -1, NULL) + 8;
}

static void call_fsqrt(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                       const cq_kd_shape *sh)
{
    (void)sh;
    cq_kernel_fsqrt(ctx, dst, src[0], W64);
}

static cq_ref_w refn_fsqrt(const cq_ref_w *s, const cq_kd_shape *sh)
{
    (void)sh;
    return cq_ref_w_make(ref_fsqrt(s[0].lo, 0u, W64), 0u, W64);
}

static const cq_kd_spec SPEC = { "fsqrt", NULL, NULL,
                                 fsqrt_shape, call_fsqrt, refn_fsqrt };

/* ---- Two helpers the .inc files share. ---------------------------------- */

/* A 64-lane span's VALUE, through the shadow for a qubit lane and through the
 * kind for a constant one. `unknown` is a failure and not a fallback: on the
 * rotation-free surface the shadow is EXACT, so an unknown entry means a gate
 * this kernel must not contain. */
static uint64_t fq_read(cq_ctx *ctx, const cq_bit *v)
{
    uint64_t got = 0u;

    for (int i = 0; i < W64; i++) {
        cq_shadow sv;

        if (cq_bit_is_const(v[i])) {
            if (cq_bit_value(v[i])) got |= UINT64_C(1) << (unsigned)i;
            continue;
        }
        sv = cq_shadow_get(&ctx->shadow, cq_bit_qindex(v[i]));
        if (sv.unknown)
            cq_h_fail(__FILE__, __LINE__,
                      "lane %d's shadow is UNKNOWN — only a rotation poisons "
                      "the shadow and this kernel emits none", i);
        if (sv.value) got |= UINT64_C(1) << (unsigned)i;
    }
    return got;
}

/* One run of the kernel into a mock, at a chosen value and quantum mask.
 * Returns `dst`'s value. Shared by the slot scan, the span scan and the
 * palindrome. */
static uint64_t fsqrt_run(uint64_t va, cq_ref_w qa, cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    int32_t ha, hd;
    uint64_t got;

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);
    ha = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(va, 0u, W64), qa);
    hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W64);

    cq_mock_reset(m);                    /* drop the rail's materialising Xs */
    cq_kernel_fsqrt(&ctx, cq_reg_bits(&ctx.regs, hd),
                    cq_reg_cbits(&ctx.regs, ha), W64);
    got = cq_pc_value(&ctx, hd);
    cq_ctx_dispose(&ctx);
    return got;
}

/* The value the structural scans run at: an ODD exponent, an inexact root
 * (`grs` 3, so it rounds DOWN), and a mantissa whose LOW SIX BITS are 0x18 —
 * which is what makes `a_lo` non-zero and so makes the `(a_hi << 2) |
 * (a_lo >> 62)` carry-in of :82 observable at all. Every power of two, and
 * therefore every "obvious" test value, has an all-zero `a_lo`. */
#define FQ_SCAN_A  FSQ_PI

/* ---- The classical row against the oracle, on every anchor. ------------- */

/* THE ONLY CASE THAT RUNS NO CIRCUIT AND STILL CATCHES A PORT ERROR. L1 drives
 * the all-classical mask row for every anchor too, but it does so through
 * `cq_kd_case`, and this one says what went wrong in bits rather than through
 * the driver's framing. It is also the only place the pinned cells and the
 * transcription meet without the circuit in between. */
CQ_TEST(the_classical_row_agrees_with_the_oracle_on_every_anchor)
{
    int n = fsqrt_anchors(W64, -1, NULL);

    CHECK(n > 0);
    for (int i = 0; i < n; i++) {
        cq_ref_w v[CQ_KD_MAX_SRC];
        uint64_t got, want;

        memset(v, 0, sizeof v);
        CHECK_EQ(fsqrt_anchors(W64, i, v), 1);
        got  = cq_fsqrt_eval(v[0].lo);
        want = ref_fsqrt(v[0].lo, 0u, W64);
        if (got != want)
            cq_h_fail(__FILE__, __LINE__,
                      "anchor %d: soft_fsqrt(%016llx) = %016llx, the oracle "
                      "says %016llx", i, (unsigned long long)v[0].lo,
                      (unsigned long long)got, (unsigned long long)want);
    }

    /* AND THE ORACLE IS NOT THE TRANSCRIPTION, ASSERTED RATHER THAN NARRATED.
     * These are the rows the host does not specify, so the oracle must be
     * answering them from its table. */
    CHECK_EQ(ref_fsqrt(CQ_F64_NEG(CQ_F64_ONE), 0u, W64), CQ_FPHOST_INDEF);
    CHECK_EQ(ref_fsqrt(CQ_F64_NEG_INF, 0u, W64), CQ_FPHOST_INDEF);
    CHECK_EQ(ref_fsqrt(FSQ_NEG_MAX_SUB, 0u, W64), CQ_FPHOST_INDEF);
    /* A NEGATIVE NaN keeps its sign AND its payload — `a_nan` fires strictly
     * last (fsqrt.jl:107-110) — where the INDEF row above would swallow it. */
    CHECK_EQ(ref_fsqrt(FSQ_NEG_QNAN, 0u, W64), FSQ_NEG_QNAN);
    CHECK_EQ(cq_fsqrt_eval(FSQ_NEG_QNAN), FSQ_NEG_QNAN);
    /* An sNaN is force-quietened and is otherwise preserved, sign included. */
    CHECK_EQ(cq_fsqrt_eval(FSQ_NEG_SNAN), FSQ_NEG_QNAN);
    CHECK_EQ(cq_fsqrt_eval(CQ_F64_SNAN), CQ_F64_SNAN_QUIET);
    /* `sqrt(-0) = -0`, NOT INDEF, by BOTH of the source's two mechanisms. */
    CHECK_EQ(cq_fsqrt_eval(CQ_F64_NEG_ZERO), CQ_F64_NEG_ZERO);
    CHECK_EQ(cq_fsqrt_eval(CQ_F64_POS_ZERO), CQ_F64_POS_ZERO);
    /* And an infinity is not a NaN: `ea == 0x7FF` holds and `fa != 0` does
     * not, so deleting the fraction conjunct turns +Inf into a NaN. */
    CHECK_EQ(cq_fsqrt_eval(CQ_F64_POS_INF), CQ_F64_POS_INF);
    /* The exact roots, spelled out so a reader checks arithmetic and not a
     * bit pattern: sqrt(4) = 2, sqrt(9) = 3, sqrt(0.25) = 0.5. */
    CHECK_EQ(cq_fsqrt_eval(FSQ_FOUR), FSQ_TWO);
    CHECK_EQ(cq_fsqrt_eval(FSQ_NINE), 0x4008000000000000ull);
    CHECK_EQ(cq_fsqrt_eval(FSQ_QUARTER), FSQ_HALF);
}

/* ---- The anchors reach the kernel, and any drop is printed. ------------- */

CQ_TEST(every_anchor_reaches_the_kernel_and_the_budget_covers_the_block)
{
    cq_kd_shape sh;
    const char *src = NULL;
    int n, budget;

    fsqrt_shape(W64, &sh);
    n = sh.anchors(W64, -1, NULL);

    CHECK_EQ(n, cq_fp_anchors_unary_count() + N_FSQRT_SINGLES);
    CHECK_EQ(sh.min_samples, 3 * n + 8);

    budget = cq_kd_budget(sh.min_samples, &src);
    CHECK(src != NULL);
    /* THE ENVIRONMENT WINS IN BOTH DIRECTIONS AND THAT IS DELIBERATE
     * (kerneldrv.h), so the claim is about the FLOOR and is made only on the
     * row where the floor is what decided. */
    if (src != NULL && strcmp(src, "env") != 0) CHECK(budget >= 3 * n + 2);
    printf("# fsqrt anchors %d (generic %d + K21 %d), budget %d from \"%s\"\n",
           n, cq_fp_anchors_unary_count(), N_FSQRT_SINGLES, budget, src);

    /* Every row fills v[0] and NOTHING ELSE — the cq_kd_case2 trap re-armed
     * for fp is a provider that zero-fills what it does not name. */
    for (int i = 0; i < n; i++) {
        cq_ref_w v[CQ_KD_MAX_SRC];

        v[0] = cq_ref_w_make(0xDEADBEEFull, 0u, W64);
        v[1] = cq_ref_w_make(0xC0FFEEull, 0u, W64);
        v[2] = cq_ref_w_make(0xC0FFEEull, 0u, W64);
        CHECK_EQ(sh.anchors(W64, i, v), 1);
        CHECK_EQ(v[1].lo, 0xC0FFEEull);       /* untouched, not zero-filled */
        CHECK_EQ(v[2].lo, 0xC0FFEEull);
    }
    fflush(stdout);
}

#include "test_kernel_fsqrt_slots.inc"
#include "test_kernel_fsqrt_loop.inc"
#include "test_kernel_fsqrt_handoff.inc"
#include "test_kernel_fsqrt_scan.inc"
#include "test_kernel_fsqrt_spans.inc"

/* ---- L1 + L2 + L3 + L5. ------------------------------------------------- */

CQ_TEST(l1_sweep)
{
    cq_kd_sweep_at(&SPEC, W64);
}

/* ---- Step 20's four regions, and §9's gate-tuple transform. -------------- */

/* THE REGION BODY IS TWO HAND-DRIVEN CASES AND NOT THE SWEEP, and that is
 * kerneldrv.h's own instruction applied to a kernel with ONE width and ONE
 * entry point: "`body` should be the suite's sweep at its CHEAP widths only".
 * K21 has no cheap width — every call is ~229k gates — so what this suite can
 * economise on is the CASE COUNT. The two rows kept are the two that matter:
 * the ALL-CLASSICAL one, which at CQ_KD_CTRL_Q0 is the only fixture in the
 * project that can see a controlled region silently made unconditional (the
 * short-circuit writes `dst` through cq_emit_x, whose constant row cannot fire
 * inside a promoted region, so each set lane must become a wire driven by a
 * real CX); and the ALL-QUANTUM one, which is the mask L4 pins. */
static void fsqrt_two_rows(void)
{
    cq_bk_pair m;
    cq_ref_w v[CQ_KD_MAX_SRC];

    memset(v, 0, sizeof v);
    v[0] = cq_ref_w_make(FQ_SCAN_A, 0u, W64);

    m.q[0] = cq_ref_w_zero();  m.q[1] = cq_ref_w_zero();
    m.name = "all-classical";
    cq_kd_case(&SPEC, W64, v, &m);

    m.q[0] = cq_ref_w_ones(W64); m.q[1] = cq_ref_w_ones(W64);
    m.name = "all-quantum";
    cq_kd_case(&SPEC, W64, v, &m);
}

CQ_TEST(controlled)
{
    uint64_t reached;

    cq_kd_for_each_region("fsqrt all-classical + all-quantum", fsqrt_two_rows);

    reached = cq_kd_check_promotion(&SPEC, W64);

    /* NOT VACUOUS: the promotion identity holds trivially where the
     * uncontrolled tuple is empty, so the caller owns the non-vacuity claim
     * (cq_kd_check_promotion returns the uncontrolled total for this reason). */
    CHECK(reached > 0u);
}

/* ---- L4: the goldens, at the all-quantum mask. -------------------------- */

CQ_TEST(l4_goldens)
{
    cq_counter fwd, unc;
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/fsqrt.counts",
                      "M40 kernels/fsqrt.c — K21 IEEE binary64 square root "
                      "(sandwiched), the whole of soft_fsqrt",
                      "ALL-QUANTUM on `a` at W = 64 (f64 only), ctrl_depth 0. "
                      "523 of the 1,070 rows are VIEWS and the constant spans "
                      "sit inside block operands, so these sit well below "
                      "twice the 157,108-slot compute half; `a_lo` is "
                      "all-constant from iteration 32",
                      CQOPS_BENNETT_COMMIT))
        return;

    cq_kd_measure(&SPEC, W64, &fwd, &unc);

    CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
    CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);

    cq_gold_check(&g, "fsqrt", "forward", W64, fwd.x, fwd.cx, fwd.ccx);
    cq_gold_check(&g, "fsqrt", "unc",     W64, unc.x, unc.cx, unc.ccx);

    CHECK(cq_gold_close(&g));
}

/* ---- dst owns 64 qubits and the scratch comes back. --------------------- */

CQ_TEST(dst_owns_64_qubits_and_the_scratch_comes_back)
{
    uint32_t peak = 0u;
    uint32_t owned = cq_kd_peak(&SPEC, W64, &peak);

    CHECK_EQ(owned, (uint32_t)W64);
    /* The whole program's region plus dst's 64 bits. L2 looks AFTER the call
     * and cannot see scratch that was taken and tidily released, so the
     * high-water mark is the only instrument for it. FLAT means peak = total
     * (PRD §15 D9), and the digit loop is 85% of it. */
    CHECK_EQ(peak, cq_fsqrt_region() + (uint32_t)W64);
}

CQ_TEST_MAIN_ARGV(
    CQ_CASE(the_program_is_the_source_line_for_line),
    CQ_CASE(the_blocks_cost_what_their_modules_say_they_cost),
    CQ_CASE(the_program_is_the_sum_of_its_blocks),
    CQ_CASE(the_digit_loop_is_sixty_four_iterations_of_one_template),
    CQ_CASE(the_classical_row_agrees_with_the_oracle_on_every_anchor),
    CQ_CASE(every_anchor_reaches_the_kernel_and_the_budget_covers_the_block),
    CQ_CASE(the_working_value_handed_to_m32_is_grs_at_bit_55_and_never_ties),
    CQ_CASE(the_slot_boundaries_match_an_independent_four_valued_scan),
    CQ_CASE(the_rows_spans_are_pairwise_disjoint),
    CQ_CASE(the_compute_half_is_a_palindrome_at_an_asymmetric_mask),
    CQ_CASE(two_programs_in_one_region_do_not_collide),
    CQ_CASE(l1_sweep),
    CQ_CASE(controlled),
    CQ_CASE(l4_goldens),
    CQ_CASE(dst_owns_64_qubits_and_the_scratch_comes_back)
)
