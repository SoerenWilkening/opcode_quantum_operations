/* tests/test_kernel_fdiv.c — M35, K17. `soft_fdiv` at f64, the third
 * floating-point arithmetic KERNEL of libcqops and the first with a LOOP in
 * it. PRD-v2 §5, §7, §7.16; docs/constructions/K17.md.
 *
 * THE SUBJECT OF THIS FILE IS THE SLOT ARITHMETIC OF A 56-ITERATION LOOP.
 * Whether `lower_eq!`, `lower_ult!`, `lower_add!`, `lower_sub!` and
 * `lower_mux!` compute the right bits is M14's, M16's and M17's suites next
 * door, whether the four shared softfloat helpers do is M32's, and whether the
 * class predicates do is M31's. What is new here is (i) which SLOT each of 478
 * rows occupies, (ii) which BITS each of them owns — and 280 of the emitting
 * rows are 56 instances of ONE four-block group, so an off-by-one in the
 * per-iteration offset lands inside a DIFFERENT ITERATION of the SAME block —
 * and (iii) whether the five hand-offs cross M32's two encodings the right way
 * round. Every one of the three is invisible to a gate count.
 *
 * THE LOOP IS TRANSCRIBED PER OPERATOR AND THAT IS PRD-v2 §7.16's DECISION.
 * §5's M35 row used to say "compose M19's exported step block"; K17.md §5.1
 * measured that that and §7.2's literal grain select different constructions,
 * and the maintainer settled it at the Wave 4 boundary. So there is no
 * `cq_divrem_block` anywhere in this suite, and the composition identity below
 * spells the loop as `56 x (C_ult + C_sub + 2*C_mux + C_or)` with 56 read off
 * `for i in 0:55` rather than as a closed form in W.
 *
 * SO L1'S ORACLE IS THE HOST DOUBLE DIVIDE AND SHARES NOTHING WITH THE PORT
 * — EXCEPT ON THE CELLS IEEE LEAVES UNSPECIFIED, WHICH ARE PINNED BY TABLE.
 * MEASURED on this box while writing this file, Apple clang 17,
 * `-std=c11 -ffp-contract=off`: with the operands `volatile` — i.e. actually
 * divided at run time — `0.0/0.0` and `Inf/Inf` both give `fff8000000000000`,
 * x86's INDEF and Bennett's, at `-O0` and at `-O2` alike. With the operands
 * visible to the optimiser as constants, clang FOLDS the division and returns
 * `7ff8000000000000` — LLVM's positive default NaN, the SIGN BIT the other way
 * up. That is a new dress for M34's finding that the host `*` returns a
 * different NaN payload at `-O0` and `-O2`: the host operator is not a
 * well-defined oracle on the IEEE-unspecified cells, and here the difference
 * is not the payload but whether the expression was executed at all.
 * `ref_fdiv` below therefore answers the NaN rows, `0/0` and `Inf/Inf` from a
 * TABLE, exactly as §7.4 says, and calls the host only where IEEE specifies
 * the result.
 *
 * SLOTS AND GATES COME APART, AS THEY DO IN M31, M32, M33, M34 AND M36
 * (D-K18-6). 125 of the 478 rows are VIEWS and seven constant spans sit inside
 * block operands, so the §3 fold table elides gates at EVERY operand mask
 * including the all-quantum one L4 pins. Every composition identity below is
 * over SLOTS, and the slot scan's prediction is four-valued — NONE / X / CX /
 * CCX — walked with a separate stream cursor. Deleting the constant rows from
 * the prediction turns the instrument off rather than simplifying it.
 *
 * WIDTH 64 AND NOTHING ELSE, DELIBERATELY, AND IT IS A REFUSAL RATHER THAN A
 * GAP. PRD-v2 §1 scopes v2 to `f64` and `soft_fdiv` is `(UInt64, UInt64)`, so
 * a second width would be a fiction; the kernel hard-errors on one in both
 * configurations and tests/test_kernel_fdiv_death.c drives that.
 *
 * THIS SUITE IS SLOW AND THAT IS NOT A REGRESSION (PRD-v2 §7.13). 139,245
 * compute-half slots per call, times a forced anchor block of 54 ordered pairs
 * at three mask rows. §7.13 has already said the wall clock is not a reason to
 * cut the budget.
 */

#include "kernels/fdiv.h"

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

#include <stdio.h>
#include <string.h>

enum { W64 = CQ_FP64_W };

/* ---- L1's oracle: the host `/`, with §7.4's cells pinned by table. ------ */

static int fd_is_nan(uint64_t u)
{
    return ((u >> 52) & CQ_FP64_EXP_ALL) == CQ_FP64_EXP_ALL
        && (u & CQ_F64_MANT_MASK) != 0u;
}

static int fd_is_inf(uint64_t u)
{
    return ((u >> 52) & CQ_FP64_EXP_ALL) == CQ_FP64_EXP_ALL
        && (u & CQ_F64_MANT_MASK) == 0u;
}

static int fd_is_zero(uint64_t u) { return (u & ~CQ_F64_SIGN_MASK) == 0u; }

/* NOTHING HERE IS DERIVED FROM src/kernels/fdiv_eval.c. The pinned rows are
 * literals — upstream's `_sf_propagate_nan2` rule (softfloat_common.jl:23-24,
 * "first-operand NaN, x86 SSE") and fphost.h's measured INDEF — and everything
 * else is the host operator, which IEEE fully specifies. An oracle that
 * re-transcribed the Julia would agree with a wrong port rather than catch it
 * (the Step 18 trap). */
static uint64_t ref_fdiv(uint64_t a, uint64_t b, int W)
{
    (void)W;
    if (fd_is_nan(a)) return a | CQ_F64_QUIET_BIT;
    if (fd_is_nan(b)) return b | CQ_F64_QUIET_BIT;
    if ((fd_is_zero(a) && fd_is_zero(b)) || (fd_is_inf(a) && fd_is_inf(b)))
        return CQ_FPHOST_INDEF;
    return cq_fphost_bits(cq_fphost_from_bits(a) / cq_fphost_from_bits(b));
}

/* ---- §7.12's anchors, composed. ----------------------------------------- */

#include "test_kernel_fdiv_anchors.inc"

/* ---- The spec. ---------------------------------------------------------- */

/* Rule 7's canonical shape unchanged — arity 2, `w_dst == w[0]` — so the
 * driver's DEFAULT call and reference paths serve it with no adapter, which
 * `cq_kd_shape_of` checks rather than assumes. */
static void fdiv_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->n_src   = 2;
    out->w[0]    = W64;
    out->w[1]    = W64;
    out->w_dst   = W64;
    out->anchors = fdiv_anchors;

    /* THE FLOOR IS READ OFF THE PROVIDER JUST INSTALLED, NOT WRITTEN DOWN
     * (bd 9ve.32). The sampler forces anchors row-major over three mask rows
     * after reserving slots 0 and 1, so `3 x anchors + 8` is the smallest
     * budget at which NO row is dropped, and it tracks the table in the .inc
     * with no edit here. */
    out->min_samples = 3 * out->anchors(W, -1, NULL) + 8;
}

static const cq_kd_spec SPEC = { "fdiv", cq_kernel_fdiv, ref_fdiv,
                                 fdiv_shape, NULL, NULL };

/* One run of the kernel into a mock, at a chosen value pair and mask pair.
 * Returns `dst`'s value. Shared by the slot scan, the span scans and the
 * palindrome. */
static uint64_t fdiv_run(uint64_t va, uint64_t vb, cq_ref_w qa, cq_ref_w qb,
                         cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    int32_t ha, hb, hd;
    uint64_t got;

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);
    ha = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(va, 0u, W64), qa);
    hb = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(vb, 0u, W64), qb);
    hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W64);

    cq_mock_reset(m);                   /* drop the rails' materialising Xs */
    cq_kernel_fdiv(&ctx, cq_reg_bits(&ctx.regs, hd),
                   cq_reg_cbits(&ctx.regs, ha), cq_reg_cbits(&ctx.regs, hb),
                   W64);
    got = cq_pc_value(&ctx, hd);
    cq_ctx_dispose(&ctx);
    return got;
}

/* ---- The classical row against the oracle, on every anchor. ------------- */

/* THE ONLY CASE THAT RUNS NO CIRCUIT AND STILL CATCHES A PORT ERROR. L1 drives
 * the all-classical mask row for every anchor too, but it does so through
 * `cq_kd_case`, and this one says what went wrong in bits rather than through
 * the driver's framing. It is also the only place the pinned cells and the
 * transcription meet without the circuit in between. */
CQ_TEST(the_classical_row_agrees_with_the_oracle_on_every_anchor)
{
    int n = fdiv_anchors(W64, -1, NULL);

    CHECK(n > 0);
    for (int i = 0; i < n; i++) {
        cq_ref_w v[CQ_KD_MAX_SRC];
        uint64_t got, want;

        memset(v, 0, sizeof v);
        CHECK_EQ(fdiv_anchors(W64, i, v), 1);
        got  = cq_fdiv_eval(v[0].lo, v[1].lo);
        want = ref_fdiv(v[0].lo, v[1].lo, W64);
        if (got != want)
            cq_h_fail(__FILE__, __LINE__,
                      "anchor %d: soft_fdiv(%016llx, %016llx) = %016llx, the "
                      "oracle says %016llx", i,
                      (unsigned long long)v[0].lo, (unsigned long long)v[1].lo,
                      (unsigned long long)got, (unsigned long long)want);
    }

    /* AND THE ORACLE IS NOT THE HOST ON THESE ROWS, ASSERTED RATHER THAN
     * NARRATED. `0/0` and `Inf/Inf` are the two cells clang CONSTANT-FOLDS to
     * a POSITIVE default NaN while the hardware produces x86's NEGATIVE INDEF
     * (see this file's header), so if the oracle were calling the host with
     * literals these two lines would come back `7ff8…` in a Release build. */
    CHECK_EQ(ref_fdiv(CQ_F64_POS_ZERO, CQ_F64_POS_ZERO, W64), CQ_FPHOST_INDEF);
    CHECK_EQ(ref_fdiv(CQ_F64_POS_INF, CQ_F64_NEG_INF, W64), CQ_FPHOST_INDEF);
    CHECK_EQ(ref_fdiv(CQ_F64_QNAN_A, CQ_F64_QNAN_B, W64), CQ_F64_QNAN_A);
    CHECK_EQ(ref_fdiv(CQ_F64_QNAN_B, CQ_F64_QNAN_A, W64), CQ_F64_QNAN_B);
    CHECK_EQ(ref_fdiv(CQ_F64_SNAN, CQ_F64_QNAN_B, W64), CQ_F64_SNAN_QUIET);
    CHECK_EQ(ref_fdiv(CQ_F64_QNAN_B, CQ_F64_SNAN, W64), CQ_F64_QNAN_B);

    /* An sNaN IS a NaN to `soft_fdiv`, because fdiv.jl never consults
     * QUIET_BIT: the exponent is extracted by shift-and-mask and the quiet bit
     * is not read anywhere in the file. A port that "helpfully" tested it
     * would send a signalling NaN down the arithmetic path. */
    CHECK_EQ(cq_fdiv_eval(CQ_F64_SNAN, CQ_F64_ONE), CQ_F64_SNAN_QUIET);

    /* AND AN INFINITY IS NOT: `ea == 0x7FF` holds and `fa != 0` does not, so
     * deleting the fraction conjunct turns every Inf into a NaN. */
    CHECK_EQ(cq_fdiv_eval(CQ_F64_POS_INF, FD_TWO), CQ_F64_POS_INF);

    /* THE TWO ROWS THAT DETECT A DROPPED `_sf_normalize_to_bit52`, in the
     * CLASSICAL body as well as in the circuit: without the Bennett-r6e3
     * pre-normalisation "the loop overflows and the result's low 52 bits zero
     * out" (fdiv.jl:71-77), and one of these two operands is subnormal in each
     * position. */
    CHECK_EQ(cq_fdiv_eval(CQ_F64_MIN_SUBNORMAL, CQ_F64_ONE),
             CQ_F64_MIN_SUBNORMAL);
    CHECK_EQ(cq_fdiv_eval(CQ_F64_ONE, CQ_F64_MAX_SUBNORMAL),
             ref_fdiv(CQ_F64_ONE, CQ_F64_MAX_SUBNORMAL, W64));
}

/* ---- The anchors reach the kernel, and any drop is printed. ------------- */

CQ_TEST(every_anchor_reaches_the_kernel_and_the_budget_covers_the_block)
{
    cq_kd_shape sh;
    const char *src = NULL;
    int n, budget;

    fdiv_shape(W64, &sh);
    n = sh.anchors(W64, -1, NULL);

    CHECK_EQ(n, cq_fp_anchors_binary_count() + N_FDIV_PAIRS);
    CHECK_EQ(sh.min_samples, 3 * n + 8);

    budget = cq_kd_budget(sh.min_samples, &src);
    CHECK(src != NULL);
    /* THE ENVIRONMENT WINS IN BOTH DIRECTIONS AND THAT IS DELIBERATE
     * (kerneldrv.h): a maintainer bisecting with CQOPS_L1_SAMPLES=8 gets 8,
     * floor or no floor. So the claim is about the FLOOR, and it is made only
     * on the row where the floor is what decided. */
    if (src != NULL && strcmp(src, "env") != 0) CHECK(budget >= 3 * n + 2);
    printf("# fdiv anchors %d (generic %d + K17 %d), budget %d from \"%s\"\n",
           n, cq_fp_anchors_binary_count(), N_FDIV_PAIRS, budget, src);

    /* Every row fills BOTH operands and nothing else — the cq_kd_case2 trap
     * re-armed for fp is a provider that zero-fills what it does not name. */
    for (int i = 0; i < n; i++) {
        cq_ref_w v[CQ_KD_MAX_SRC];

        v[0] = cq_ref_w_make(0xDEADBEEFull, 0u, W64);
        v[1] = cq_ref_w_make(0xDEADBEEFull, 0u, W64);
        v[2] = cq_ref_w_make(0xC0FFEEull, 0u, W64);
        CHECK_EQ(sh.anchors(W64, i, v), 1);
        CHECK_EQ(v[2].lo, 0xC0FFEEull);       /* untouched, not zero-filled */
    }
    fflush(stdout);
}

#include "test_kernel_fdiv_slots.inc"
#include "test_kernel_fdiv_scan.inc"
#include "test_kernel_fdiv_spans.inc"

/* ---- L1 + L2 + L3 + L5. ------------------------------------------------- */

CQ_TEST(l1_sweep)
{
    cq_kd_sweep_at(&SPEC, W64);
}

/* ---- Step 20's four regions, and §9's gate-tuple transform. -------------- */

/* THE REGION BODY IS TWO HAND-DRIVEN CASES AND NOT THE SWEEP, and that is
 * kerneldrv.h's own instruction applied to a kernel with ONE width and ONE
 * entry point: "`body` should be the suite's sweep at its CHEAP widths only".
 * K17 has no cheap width, so what this suite can economise on is the CASE
 * COUNT. The two rows kept are the two that matter: the ALL-CLASSICAL one,
 * which at CQ_KD_CTRL_Q0 is the only fixture in the project that can see a
 * controlled region silently made unconditional (the short-circuit writes
 * `dst` through cq_emit_x, whose constant row cannot fire inside a promoted
 * region, so each set lane must become a wire driven by a real CX); and the
 * ALL-QUANTUM one, which is the mask L4 pins. */
static void fdiv_two_rows(void)
{
    cq_bk_pair m;
    cq_ref_w v[CQ_KD_MAX_SRC];

    v[0] = cq_ref_w_make(FD_SCAN_A, 0u, W64);
    v[1] = cq_ref_w_make(FD_SCAN_B, 0u, W64);

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

    cq_kd_for_each_region("fdiv all-classical + all-quantum", fdiv_two_rows);

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

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/fdiv.counts",
                      "M35 kernels/fdiv.c — K17 IEEE binary64 divide "
                      "(sandwiched), the whole of soft_fdiv including its "
                      "56-iteration restoring-division loop",
                      "ALL-QUANTUM on `a` and `b` at W = 64 (f64 only), at "
                      "ctrl_depth 0. 125 of the 478 rows are VIEWS and seven "
                      "constant spans sit inside block operands, so these are "
                      "well below twice the 139,245-slot compute half",
                      CQOPS_BENNETT_COMMIT))
        return;

    cq_kd_measure(&SPEC, W64, &fwd, &unc);

    CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
    CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);

    cq_gold_check(&g, "fdiv", "forward", W64, fwd.x, fwd.cx, fwd.ccx);
    cq_gold_check(&g, "fdiv", "unc",     W64, unc.x, unc.cx, unc.ccx);

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
     * high-water mark is the only instrument for it. R11's pool ceiling is
     * live at this figure and PRD §15 D25 stands: no "that will not fit" check
     * may grow in M35 or in the shim. */
    CHECK_EQ(peak, cq_fdiv_region() + (uint32_t)W64);
}

CQ_TEST_MAIN_ARGV(
    CQ_CASE(the_program_is_the_source_line_for_line),
    CQ_CASE(every_iteration_is_the_template_at_a_distinct_offset),
    CQ_CASE(the_blocks_cost_what_their_modules_say_they_cost),
    CQ_CASE(the_program_is_the_sum_of_its_blocks),
    CQ_CASE(the_classical_row_agrees_with_the_oracle_on_every_anchor),
    CQ_CASE(every_anchor_reaches_the_kernel_and_the_budget_covers_the_block),
    CQ_CASE(the_slot_boundaries_match_an_independent_four_valued_scan),
    CQ_CASE(iteration_zero_folds_differently_from_every_other),
    CQ_CASE(the_rows_spans_are_pairwise_disjoint),
    CQ_CASE(the_loop_is_fifty_six_bodies_at_a_constant_stride),
    CQ_CASE(the_compute_half_is_a_palindrome_at_an_asymmetric_mask),
    CQ_CASE(two_programs_in_one_region_do_not_collide),
    CQ_CASE(l1_sweep),
    CQ_CASE(controlled),
    CQ_CASE(l4_goldens),
    CQ_CASE(dst_owns_64_qubits_and_the_scratch_comes_back)
)
