/* tests/test_kernel_fma.c — M39, K20. `soft_fma` at f64: the largest single
 * transcription in the catalogue and the FIRST THREE-SOURCE fp kernel.
 * PRD-v2 §5, §6.1, §7; docs/constructions/K20.md.
 *
 * THE SUBJECT OF THIS FILE IS THE SLOT ARITHMETIC AND THE SINGLE-ROUNDING
 * PATH. Whether `lower_eq!`, `lower_ult!`, `lower_slt!`, `lower_add!`,
 * `lower_sub!`, `lower_mux!`, `lower_mul!` and the two barrels compute the
 * right bits is M12's, M14's, M16's, M17's and M18's suites next door, and
 * whether the three shared softfloat helpers do is M32's. What is new here is
 * (i) which SLOT each of 395 rows occupies, (ii) which BITS each of them owns
 * — four `cq_mul_block`s and TWELVE `cq_barrel_block`s at sixteen offsets in
 * one region — and (iii) that `round(a*b + c)` is computed with ONE rounding.
 * Every one of the three is invisible to a gate count.
 *
 * SO L1'S ORACLE IS THE HOST `fma()` AND SHARES NOTHING WITH THE PORT —
 * EXCEPT ON THE CELLS IEEE LEAVES UNSPECIFIED, WHICH ARE PINNED BY TABLE.
 * That is PRD-v2 §7.4's instruction. The host `fma()` is a legitimate oracle
 * wherever IEEE specifies the result, and it specifies rather a lot: `fma` is
 * one of the operations IEEE-754 defines EXACTLY, as the correctly-rounded
 * value of the infinitely-precise `a*b + c`. What it does NOT specify is the
 * NaN payload that survives, `Inf x 0`, and `Inf + (-Inf)` — and on those this
 * file answers from a table built out of upstream's own three-operand
 * precedence rule (`_sf_propagate_nan3`, softfloat_common.jl:32-35) and
 * fphost.h's measured INDEF, which are two independent statements of the same
 * numbers and not a reading of src/kernels/fma_eval.c.
 *
 * THE HOST ORACLE IS ALSO NOT COMMUTATIVITY-EXPOSED HERE, UNLIKE M33's AND
 * M34's. `a*b` is commutative and clang measurably exchanges the operands of
 * a `*` per call site (bd memory
 * host-fp-operator-commutes-so-the-nan-payload-is-not-an-oracle), so K15 and
 * K16 had to pin the two-NaN rows for that reason as well. `fma(a, b, c)` is
 * a three-argument CALL and its arguments cannot be permuted by the compiler
 * — but the surviving payload is still unspecified, so the rows are pinned
 * anyway and the reason is the arm64 one rather than the clang one.
 *
 * SLOTS AND GATES COME APART, AS THEY DO IN EVERY fp KERNEL (D-K18-6). 73 of
 * the 395 rows are VIEWS, 15 are projections and eighteen constant spans sit
 * inside block operands, so the §3 fold table elides gates at EVERY operand
 * mask including the all-quantum one L4 pins. Every composition identity here
 * is over SLOTS.
 *
 * WIDTH 64 AND NOTHING ELSE, DELIBERATELY, AND IT IS A REFUSAL RATHER THAN A
 * GAP. PRD-v2 §1 scopes v2 to `f64` and `soft_fma` is
 * `(UInt64, UInt64, UInt64)`, so a second width would be a fiction; the kernel
 * hard-errors on one in both configurations and tests/test_kernel_fma_death.c
 * drives that.
 *
 * THIS SUITE IS THE SLOWEST IN THE PROJECT AND THAT IS NOT A REGRESSION
 * (PRD-v2 §7.13). 241,083 compute-half slots, twice per call under Rule 2,
 * times a forced anchor block of 53 rows over three mask rows. §7.13 has
 * already said the wall clock is not a reason to cut the budget.
 */

#include "kernels/fma.h"
#include "kernels/fma_int.h"

#include "kernels/fadd.h"
#include "kernels/fmul.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/fpfield.h"
#include "kernels/fpround.h"
#include "kernels/mul.h"
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

enum { W64 = CQ_FP64_W };

/* ---- L1's oracle: the host `fma()`, with §7.4's cells pinned by table. -- */

static int fa_is_nan(uint64_t u)
{
    return ((u >> 52) & CQ_FP64_EXP_ALL) == CQ_FP64_EXP_ALL
        && (u & CQ_F64_MANT_MASK) != 0u;
}

static int fa_is_inf(uint64_t u)
{
    return ((u >> 52) & CQ_FP64_EXP_ALL) == CQ_FP64_EXP_ALL
        && (u & CQ_F64_MANT_MASK) == 0u;
}

static int fa_is_zero(uint64_t u) { return (u & ~CQ_F64_SIGN_MASK) == 0u; }

/* NOTHING HERE IS DERIVED FROM src/kernels/fma_eval.c. The pinned rows are
 * literals — upstream's `_sf_propagate_nan3` a > b > c precedence
 * (softfloat_common.jl:32-35, "matching Intel VFMADD*'s NaN-handling order")
 * and fphost.h's measured INDEF — and everything else is the host `fma()`,
 * which IEEE specifies exactly. An oracle that re-transcribed the Julia would
 * agree with a wrong port rather than catch it (the Step 18 trap). */
static uint64_t ref_fma(uint64_t a, uint64_t b, uint64_t c)
{
    const int prod_inf = (fa_is_inf(a) && !fa_is_zero(b))
                      || (fa_is_inf(b) && !fa_is_zero(a));

    /* a > b > c, and an sNaN is quietened rather than given priority. */
    if (fa_is_nan(a)) return a | CQ_F64_QUIET_BIT;
    if (fa_is_nan(b)) return b | CQ_F64_QUIET_BIT;
    if (fa_is_nan(c)) return c | CQ_F64_QUIET_BIT;
    /* Inf x 0, either way round. */
    if ((fa_is_inf(a) && fa_is_zero(b)) || (fa_is_zero(a) && fa_is_inf(b)))
        return CQ_FPHOST_INDEF;
    /* An infinite product against an infinity of the other sign. */
    if (prod_inf && fa_is_inf(c)
        && ((((a ^ b) >> 63) & 1u) != ((c >> 63) & 1u)))
        return CQ_FPHOST_INDEF;
    return cq_fphost_bits(fma(cq_fphost_from_bits(a), cq_fphost_from_bits(b),
                              cq_fphost_from_bits(c)));
}

/* ---- §7.12's anchors, composed. ----------------------------------------- */

#include "test_kernel_fma_anchors.inc"

/* ---- The spec: three sources, so it needs an adapter. ------------------- */

/* PRD-v2 §7.11: "`fma` declares three sources exactly as the mux does,
 * reached through `cq_kd_spec`'s `call` adapter with `.kernel` NULL".
 * `cq_kernel_fn` is arity 2 and must not be widened (Rule 7), so the spec
 * leaves `.kernel` NULL and supplies both adapters. */
static void call_fma(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                     const cq_kd_shape *sh)
{
    cq_kernel_fma(ctx, dst, src[0], src[1], src[2], sh->w[0]);
}

static cq_ref_w refn_fma(const cq_ref_w *src, const cq_kd_shape *sh)
{
    (void)sh;
    return cq_ref_w_make(ref_fma(src[0].lo, src[1].lo, src[2].lo), 0u, W64);
}

static void fma_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->n_src   = 3;
    out->w[0]    = W64;
    out->w[1]    = W64;
    out->w[2]    = W64;
    out->w_dst   = W64;
    out->anchors = fma_anchors;

    /* THE FLOOR IS READ OFF THE PROVIDER JUST INSTALLED, NOT WRITTEN DOWN
     * (bd 9ve.32). The sampler forces anchors row-major over three mask rows
     * after reserving slots 0 and 1, so `3 x anchors + 8` is the smallest
     * budget at which NO row is dropped, and it tracks the table in the .inc
     * with no edit here. */
    out->min_samples = 3 * out->anchors(W, -1, NULL) + 8;
}

static const cq_kd_spec SPEC = { "fma", NULL, NULL,
                                 fma_shape, call_fma, refn_fma };

/* One run of the kernel into a mock, at a chosen value triple and mask
 * triple. Returns `dst`'s value. Shared by the palindrome case. */
static uint64_t fma_run(uint64_t va, uint64_t vb, uint64_t vc,
                        const cq_ref_w *q, cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    int32_t h[3], hd;
    uint64_t v[3], got;

    v[0] = va; v[1] = vb; v[2] = vc;
    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);
    for (int i = 0; i < 3; i++)
        h[i] = cq_bk_reg_w(&ctx, (uint32_t)W64,
                           cq_ref_w_make(v[i], 0u, W64), q[i]);
    hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W64);

    cq_mock_reset(m);                   /* drop the rails' materialising Xs */
    cq_kernel_fma(&ctx, cq_reg_bits(&ctx.regs, hd),
                  cq_reg_cbits(&ctx.regs, h[0]), cq_reg_cbits(&ctx.regs, h[1]),
                  cq_reg_cbits(&ctx.regs, h[2]), W64);
    got = cq_pc_value(&ctx, hd);
    cq_ctx_dispose(&ctx);
    return got;
}

#include "test_kernel_fma_slots.inc"
#include "test_kernel_fma_spans.inc"

/* THE PREFIX MAP AND THE FOUR UNREACHABLE GUARDS (review round 1). See the
 * `.inc`'s own header for why three of them are positive cases. */
#include "test_kernel_fma_map.inc"

/* ---- The classical row against the oracle, on every anchor. ------------- */

CQ_TEST(the_classical_row_agrees_with_the_oracle_on_every_anchor)
{
    int n = fma_anchors(W64, -1, NULL);

    CHECK(n > 0);
    for (int i = 0; i < n; i++) {
        cq_ref_w v[CQ_KD_MAX_SRC];
        uint64_t got, want;

        memset(v, 0, sizeof v);
        CHECK_EQ(fma_anchors(W64, i, v), 1);
        got  = cq_fma_eval(v[0].lo, v[1].lo, v[2].lo);
        want = ref_fma(v[0].lo, v[1].lo, v[2].lo);
        if (got != want)
            cq_h_fail(__FILE__, __LINE__,
                      "anchor %d: soft_fma(%016llx, %016llx, %016llx) = "
                      "%016llx, the oracle says %016llx", i,
                      (unsigned long long)v[0].lo, (unsigned long long)v[1].lo,
                      (unsigned long long)v[2].lo, (unsigned long long)got,
                      (unsigned long long)want);
    }

    /* AND THE ORACLE IS NOT THE TRANSCRIPTION, ASSERTED RATHER THAN NARRATED.
     * A three-operand precedence is what two NaNs cannot distinguish from a
     * two-operand one, so all three rows are pinned. */
    CHECK_EQ(ref_fma(CQ_F64_QNAN_A, CQ_F64_QNAN_B, FA_QNAN_C), CQ_F64_QNAN_A);
    CHECK_EQ(ref_fma(CQ_F64_ONE, CQ_F64_QNAN_B, FA_QNAN_C), CQ_F64_QNAN_B);
    CHECK_EQ(ref_fma(CQ_F64_ONE, FA_TWO, FA_QNAN_C), FA_QNAN_C);
    CHECK_EQ(ref_fma(CQ_F64_SNAN, CQ_F64_ONE, CQ_F64_POS_ZERO),
             CQ_F64_SNAN_QUIET);
    CHECK_EQ(ref_fma(CQ_F64_POS_INF, CQ_F64_POS_ZERO, CQ_F64_ONE),
             CQ_FPHOST_INDEF);
    CHECK_EQ(ref_fma(CQ_F64_POS_INF, CQ_F64_ONE, CQ_F64_NEG_INF),
             CQ_FPHOST_INDEF);

    /* An sNaN IS a NaN to `soft_fma`, because fma.jl never consults
     * QUIET_BIT. An infinity is NOT: `ea == 0x7FF` holds and `fa != 0` does
     * not, so deleting the fraction conjunct turns every Inf into a NaN. */
    CHECK_EQ(cq_fma_eval(CQ_F64_SNAN, CQ_F64_ONE, CQ_F64_POS_ZERO),
             CQ_F64_SNAN_QUIET);
    CHECK_EQ(cq_fma_eval(CQ_F64_POS_INF, CQ_F64_ONE, CQ_F64_ONE),
             CQ_F64_POS_INF);
}

/* ---- The defining property, discharged by execution. ------------------- */

/* K20.md §1.1a's anchor 26 carries a HAND-DERIVED expected value. This case
 * does not repeat it: it derives the discriminator from the two spellings and
 * asserts that they DISAGREE and that the port takes the single-rounding one.
 * `x = 1 + 2^-52`, so `x*x` is exactly `1 + 2^-51 + 2^-104` and `fl(x*x)` is
 * `1 + 2^-51`; a single rounding of `x*x - fl(x*x)` returns the residue
 * `2^-104` and a double rounding returns `+0`.
 *
 * THIS IS THE ONE CASE THAT WOULD GO RED IF `cq_kernel_fma` WERE SPELLED
 * `fmul` THEN `fadd`, which is what PRD-v2 §5's recorded seam forbids. The
 * value, the pool, the palindrome and the golden would all be green. */
CQ_TEST(the_single_rounding_path_is_not_a_multiply_then_an_add)
{
    const uint64_t rounded_then_added =
        cq_fadd_eval(cq_fmul_eval(FA_X, FA_X), FA_NEG_X2_RND);
    const uint64_t single = cq_fma_eval(FA_X, FA_X, FA_NEG_X2_RND);

    /* The two spellings really do disagree here — asserted rather than
     * asserted-by-eye, because if they agreed the case would pass under the
     * composition for the wrong reason. */
    CHECK(rounded_then_added != single);
    CHECK_EQ(rounded_then_added, CQ_F64_POS_ZERO);
    CHECK_EQ(single, ref_fma(FA_X, FA_X, FA_NEG_X2_RND));

    /* And it is the residue rather than any other non-zero: 2^-104 is an
     * exponent of 1023 - 104 = 919 = 0x397 with a zero fraction. */
    CHECK_EQ(single, UINT64_C(0x3970000000000000));

    /* THE CIRCUIT AGREES WITH THE CLASSICAL ROW ON IT, at the all-quantum
     * mask — which is what makes this a claim about the kernel rather than
     * about fma_eval.c. */
    {
        cq_mock m;
        cq_ref_w q[3];

        cq_mock_init(&m);
        for (int i = 0; i < 3; i++) q[i] = cq_ref_w_ones(W64);
        CHECK_EQ(fma_run(FA_X, FA_X, FA_NEG_X2_RND, q, &m), single);
        cq_mock_dispose(&m);
    }
}

/* ---- The anchors reach the kernel, and any drop is printed. ------------- */

CQ_TEST(every_anchor_reaches_the_kernel_and_the_budget_covers_the_block)
{
    cq_kd_shape sh;
    const char *src = NULL;
    int n, budget;

    fma_shape(W64, &sh);
    n = sh.anchors(W64, -1, NULL);

    CHECK_EQ(n, cq_fp_anchors_binary_count() + N_FA_TRIPLES);
    CHECK_EQ(sh.min_samples, 3 * n + 8);

    budget = cq_kd_budget(sh.min_samples, &src);
    CHECK(src != NULL);
    /* THE ENVIRONMENT WINS IN BOTH DIRECTIONS AND THAT IS DELIBERATE
     * (kerneldrv.h), so the claim is about the FLOOR and is made only on the
     * row where the floor is what decided. */
    if (src != NULL && strcmp(src, "env") != 0) CHECK(budget >= 3 * n + 2);
    printf("# fma anchors %d (generic %d + K20 %d), budget %d from \"%s\"\n",
           n, cq_fp_anchors_binary_count(), N_FA_TRIPLES, budget, src);

    /* EVERY ROW FILLS ALL THREE OPERANDS, which is this provider's contract
     * and the cq_kd_case2 trap re-armed for fp: the sampler overwrites only
     * what a row NAMES, so an unnamed third operand stays random and every
     * relation between `a*b` and `c` goes untested. A fourth slot must stay
     * untouched, because zero-filling is the other half of the same trap. */
    for (int i = 0; i < n; i++) {
        cq_ref_w v[CQ_KD_MAX_SRC];

        for (int j = 0; j < CQ_KD_MAX_SRC; j++)
            v[j] = cq_ref_w_make(0xDEADBEEFull, 0u, W64);
        CHECK_EQ(sh.anchors(W64, i, v), 1);
        for (int j = 0; j < 3; j++)
            if (v[j].lo == 0xDEADBEEFull)
                cq_h_fail(__FILE__, __LINE__,
                          "anchor %d leaves operand %d unwritten", i, j);
    }
    fflush(stdout);
}

/* ---- L1 + L2 + L3 + L5. ------------------------------------------------- */

CQ_TEST(l1_sweep)
{
    cq_kd_sweep_at(&SPEC, W64);
}

/* ---- Step 20's four regions, and §9's gate-tuple transform. -------------- */

/* THE REGION BODY IS TWO HAND-DRIVEN CASES AND NOT THE SWEEP, which is
 * kerneldrv.h's own instruction applied to a kernel with ONE width and ONE
 * entry point. K20 has no cheap width — every call is a quarter of a million
 * slots — so what this suite economises on is the CASE COUNT, and the two
 * rows kept are the two that matter: the ALL-CLASSICAL one, which at
 * CQ_KD_CTRL_Q0 is the only fixture in the project that can see a controlled
 * region silently made unconditional, and the ALL-QUANTUM one, which is the
 * mask L4 pins. */
static void fma_two_rows(void)
{
    cq_bk_pair m;
    cq_ref_w v[CQ_KD_MAX_SRC];

    v[0] = cq_ref_w_make(FA_X, 0u, W64);
    v[1] = cq_ref_w_make(FA_X, 0u, W64);
    v[2] = cq_ref_w_make(FA_NEG_X2_RND, 0u, W64);

    /* `cq_bk_pair` carries TWO masks and `cq_kd_case` gives a third source
     * `q[0]` (kerneldrv.c). Both rows here are uniform, so the third operand
     * gets exactly the mask the other two do — which is the claim, and is why
     * a THIRD mask member is not owed. */
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

    cq_kd_for_each_region("fma all-classical + all-quantum", fma_two_rows);

    reached = cq_kd_check_promotion(&SPEC, W64);

    /* NOT VACUOUS: the promotion identity holds trivially where the
     * uncontrolled tuple is empty, so the caller owns the non-vacuity claim
     * (cq_kd_check_promotion returns the uncontrolled total for this
     * reason). */
    CHECK(reached > 0u);
}

/* ---- L4: the goldens, at the all-quantum mask. -------------------------- */

CQ_TEST(l4_goldens)
{
    cq_counter fwd, unc;
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/fma.counts",
                      "M39 kernels/fma.c — K20 IEEE binary64 fused "
                      "multiply-add (sandwiched), the whole of soft_fma",
                      "ALL-QUANTUM on `a`, `b` and `c` at W = 64 (f64 only), "
                      "at ctrl_depth 0. 73 of the 395 rows are VIEWS, 15 are "
                      "projections and eighteen constant spans sit inside "
                      "block operands, so these are well below twice the "
                      "241,083-slot compute half",
                      CQOPS_BENNETT_COMMIT))
        return;

    cq_kd_measure(&SPEC, W64, &fwd, &unc);

    CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
    CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);

    cq_gold_check(&g, "fma", "forward", W64, fwd.x, fwd.cx, fwd.ccx);
    cq_gold_check(&g, "fma", "unc",     W64, unc.x, unc.cx, unc.ccx);

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
     * high-water mark is the only instrument for it. R11 is live at ~85k
     * qubits per call and D25 stands: no "that will not fit" check here. */
    CHECK_EQ(peak, cq_fma_region() + (uint32_t)W64);
}

CQ_TEST_MAIN_ARGV(
    CQ_CASE(the_program_is_the_source_line_for_line),
    CQ_CASE(the_blocks_cost_what_their_modules_say_they_cost),
    CQ_CASE(the_program_is_the_sum_of_its_blocks),
    CQ_CASE(the_rows_spans_are_pairwise_disjoint),
    CQ_CASE(the_prefix_map_names_the_last_row_at_a_zero_slot_boundary),
    CQ_CASE(every_declared_operand_code_resolves),
    CQ_CASE(every_view_chain_terminates_well_inside_the_guard),
    CQ_CASE(a_row_emits_exactly_when_its_op_has_a_dispatch_arm),
    CQ_CASE(the_result_row_is_a_span_inside_the_caller_s_region),
    CQ_CASE(the_classical_row_agrees_with_the_oracle_on_every_anchor),
    CQ_CASE(the_single_rounding_path_is_not_a_multiply_then_an_add),
    CQ_CASE(every_anchor_reaches_the_kernel_and_the_budget_covers_the_block),
    CQ_CASE(the_compute_half_is_a_palindrome_at_an_asymmetric_mask),
    CQ_CASE(two_programs_in_one_region_do_not_collide),
    CQ_CASE(l1_sweep),
    CQ_CASE(controlled),
    CQ_CASE(l4_goldens),
    CQ_CASE(dst_owns_64_qubits_and_the_scratch_comes_back)
)
