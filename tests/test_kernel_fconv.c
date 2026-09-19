/* tests/test_kernel_fconv.c — M37, K19. `fptosi` / `fptoui` / `sitofp` /
 * `uitofp` at f64: the fp <-> int boundary, and the first fp module whose two
 * halves share a machine and nothing else. PRD-v2 §5, §7; K19.md.
 *
 * THE SUBJECT OF THIS FILE IS THE SLOT ARITHMETIC, THE SATURATION TABLE AND
 * THE ROUTING. Whether `lower_eq!`, `lower_ult!`, `lower_sub!`, `lower_add!`,
 * `lower_mux!` and the barrel compute the right bits is M14's, M16's, M17's
 * and M12's suites next door; whether `soft_fsub` is right is M33's. What is
 * new here is (i) which of the three sources' operator occurrences each row
 * transcribes and in what order, (ii) which SLOT each of 31 / 74 / 75 rows
 * occupies, (iii) that every NaN, every infinity and every out-of-range
 * operand comes back as x86's `0x8000000000000000` rather than whatever a C
 * cast would have folded to, and (iv) that `uitofp` is `zext` then the SAME
 * sitofp program and that its i64 row is a REFUSAL. Every one of the four is
 * invisible to a gate count.
 *
 * SO L1'S ORACLE IS THE HOST CAST WHERE C DEFINES IT AND PRD-v2 §7.5's TABLE
 * AS LITERALS WHERE C DOES NOT, and it shares nothing with the port.
 * fconv_eval.c evaluates its own transcription of the three Julia bodies over
 * `uint64_t` for the classical short-circuit (§7.4); if this reference did the
 * same it would agree with a wrong row rather than catch it (the Step 18
 * finding). The saturating rows are expressed as VALUE facts — `isnan(d)`,
 * `fabs(d) >= 2^63`, `d >= 2^64` — and never as `exp >= 1086`, which is the
 * port's own predicate and the constant an oracle must not share.
 *
 * THE `uitofp` ORACLE IS THE *CORRECT* UNSIGNED CONVERSION AND NOT UPSTREAM'S
 * ROUTING, WHICH IS ONLY SOUND BECAUSE i64 IS REFUSED. `(double)(uint64_t)x`
 * is what the opcode means; upstream's `zext`-then-`soft_sitofp` agrees with
 * it for every source of 32 bits or fewer and disagrees with it on half the
 * i64 space, which is bead 9ve.34. Pointing this oracle at the host is
 * therefore a check on the port AND, at the one width we do not ship, the
 * witness for the refusal — `the_i64_row_upstream_would_have_emitted_is_wrong`
 * asserts the divergence directly rather than letting a red anchor tempt
 * someone into "fixing" the port (K19.md §6.5 risk 1).
 *
 * SLOTS AND GATES COME APART, AS THEY DO IN M31, M32, M33, M34 AND M36
 * (D-K18-6). A block operand is a view over a rail with a constant fill, or a
 * constant span, so the §3 fold table elides gates at EVERY operand mask
 * including the all-quantum one L4 pins. Every composition identity below is
 * over SLOTS, and the slot scan's prediction is four-valued — NONE / X / CX /
 * CCX — walked with a separate stream cursor.
 *
 * WIDTHS ARE ENUMERATED, NEVER SAMPLED, AND THE FIVE PAIRS ARE THE WHOLE
 * LADDER: f64->i64 three times over and i{1,8,16,32}->f64. PRD-v2 §1 scopes v2
 * to `f64` and all three Julia bodies are `(UInt64)`, so every other pair is a
 * REFUSAL rather than a gap and tests/test_kernel_fconv_death.c drives it in
 * both configurations.
 */

#include "kernels/fconv.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fadd.h"
#include "kernels/fpfield.h"
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

#define FV_HIBIT  UINT64_C(0x8000000000000000)

/* 2^63 and 2^64 as exact doubles, written as the decimal the C standard's own
 * limits use. Both are exactly representable, which is the point: they are
 * boundaries, not approximations. */
#define FV_TWO63  9223372036854775808.0
#define FV_TWO64  18446744073709551616.0

/* ---- L1's oracle: the host cast where C defines it, §7.5's value where not. */

/* NOTHING HERE IS DERIVED FROM THE PORT. The saturating cells are the ONE
 * literal PRD-v2 §7.5 pins, and the predicates that select them are HOST
 * comparisons on the `double` — never a second copy of `exp >= 1086`, which is
 * `fptosi.jl:66`'s own expression and exactly the constant an oracle must not
 * reuse (the Step 18 trap).
 *
 * WHY EACH BRANCH IS WHERE IT IS.
 *   - `isnan` / `|d| >= 2^63`: C11 §6.3.1.4 leaves `(int64_t)d` UNDEFINED for
 *     a NaN, an infinity or a value outside the target's range, so the cast
 *     cannot be consulted there at all. `d == -2^63` exactly IS in range, and
 *     saturating it is harmless because the saturation value and the true
 *     value are the same bits — which is what fptosi.jl:16-20 means by
 *     "idempotent on that value".
 *   - `fptoui`'s three bands are C's three DEFINED regions: `(uint64_t)d` for
 *     d in [0, 2^64), and `(uint64_t)(int64_t)d` for d in (-2^63, 2^63),
 *     whose two's-complement reinterpretation is exactly the "honesty over a
 *     stricter LLVM-spec saturation" fptoui.jl:20-22 chose. */
static uint64_t host_fptosi(uint64_t u, int F)
{
    double d = cq_fphost_from_bits(u);

    (void)F;
    if (isnan(d) || fabs(d) >= FV_TWO63) return FV_HIBIT;
    return (uint64_t)(int64_t)d;
}

static uint64_t host_fptoui(uint64_t u, int F)
{
    double d = cq_fphost_from_bits(u);

    (void)F;
    if (isnan(d))          return FV_HIBIT;
    if (d >= FV_TWO64)     return FV_HIBIT;   /* +Inf and 2^64 and above   */
    if (d <= -FV_TWO63)    return FV_HIBIT;   /* -Inf and -2^63 and below  */
    if (d >= FV_TWO63)     return (uint64_t)d;             /* the biased half */
    return (uint64_t)(int64_t)d;                           /* incl. negatives */
}

/* `(double)(int64_t)x` is fully specified for every `int64_t` — that is the
 * real asymmetry between K19's two halves and it follows PRD-v2 §5's seam
 * exactly (K19.md §5.4's rider). The host probe has already pinned
 * round-to-nearest-even and FTZ/DAZ off, so the two tie anchors judge the port
 * rather than the box. */
static uint64_t host_sitofp(uint64_t u, int F)
{
    (void)F;
    return cq_fphost_bits((double)(int64_t)u);
}

static uint64_t host_uitofp(uint64_t u, int F)
{
    uint64_t m = (F >= 64) ? ~UINT64_C(0)
                           : ((UINT64_C(1) << (unsigned)F) - UINT64_C(1));

    return cq_fphost_bits((double)(u & m));
}

/* ---- §7.12's anchors, composed. ----------------------------------------- */

#include "test_kernel_fconv_anchors.inc"

/* ---- The four kernels, as one table. ------------------------------------ */

/* The width PAIR under test. A conversion's shape depends on (F, T), which the
 * driver hands in as a single W — so the pair is carried here and the shape
 * function reads it, exactly as M13's suite does. Set before each sweep;
 * single-threaded by construction, like the harness's own current-case
 * pointer. */
static int g_F = CQ_FP64_W, g_T = CQ_FP64_W;
static cq_kd_anchor_fn g_anchors;

static void fconv_shape(int W, cq_kd_shape *out)
{
    (void)W;
    cq_kd_default_shape(g_F, out);
    out->n_src   = 1;
    out->w[0]    = g_F;
    out->w_dst   = g_T;
    out->anchors = g_anchors;

    /* THE FLOOR IS READ OFF THE PROVIDER JUST INSTALLED, NOT WRITTEN DOWN
     * (bd 9ve.32). The sampler forces anchors row-major over three mask rows
     * after reserving slots 0 and 1, so `3 x anchors + 8` is the smallest
     * budget at which NO row is dropped, and it tracks the tables in the .inc
     * with no edit here. It also SHRINKS with the source width for the
     * int-source provider, which is the whole reason the count is asked for
     * rather than written. */
    out->min_samples = 3 * out->anchors(g_F, -1, NULL) + 8;
}

static void call_fptosi(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                        const cq_kd_shape *sh)
{ cq_kernel_fptosi(ctx, dst, src[0], sh->w[0], sh->w_dst); }

static void call_fptoui(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                        const cq_kd_shape *sh)
{ cq_kernel_fptoui(ctx, dst, src[0], sh->w[0], sh->w_dst); }

static void call_sitofp(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                        const cq_kd_shape *sh)
{ cq_kernel_sitofp(ctx, dst, src[0], sh->w[0], sh->w_dst); }

static void call_uitofp(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                        const cq_kd_shape *sh)
{ cq_kernel_uitofp(ctx, dst, src[0], sh->w[0], sh->w_dst); }

typedef struct {
    cq_kd_spec      spec;
    cq_fconv_prog   prog;
    uint64_t      (*host)(uint64_t, int);
    uint64_t      (*eval)(uint64_t, int);
    cq_kd_anchor_fn anchors;
    int             fp_source;   /* 1: the operand is an IEEE bit pattern */
} fconv_krow;

static uint64_t ev_fptosi(uint64_t a, int F) { (void)F; return cq_fptosi_eval(a); }
static uint64_t ev_fptoui(uint64_t a, int F) { (void)F; return cq_fptoui_eval(a); }
static uint64_t ev_sitofp(uint64_t a, int F) { (void)F; return cq_sitofp_eval(a); }
static uint64_t ev_uitofp(uint64_t a, int F) { return cq_uitofp_eval(a, F); }

static cq_ref_w ref_fptosi(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_make(host_fptosi(s[0].lo, sh->w[0]), 0u, sh->w_dst); }
static cq_ref_w ref_fptoui(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_make(host_fptoui(s[0].lo, sh->w[0]), 0u, sh->w_dst); }
static cq_ref_w ref_sitofp(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_make(host_sitofp(s[0].lo, sh->w[0]), 0u, sh->w_dst); }
static cq_ref_w ref_uitofp(const cq_ref_w *s, const cq_kd_shape *sh)
{ return cq_ref_w_make(host_uitofp(s[0].lo, sh->w[0]), 0u, sh->w_dst); }

static const fconv_krow ROWS[] = {
    { { "fptosi", NULL, NULL, fconv_shape, call_fptosi, ref_fptosi },
      CQ_FCONV_PROG_FPTOSI, host_fptosi, ev_fptosi, fconv_fp_anchors,  1 },
    { { "fptoui", NULL, NULL, fconv_shape, call_fptoui, ref_fptoui },
      CQ_FCONV_PROG_FPTOUI, host_fptoui, ev_fptoui, fconv_fp_anchors,  1 },
    { { "sitofp", NULL, NULL, fconv_shape, call_sitofp, ref_sitofp },
      CQ_FCONV_PROG_SITOFP, host_sitofp, ev_sitofp, fconv_int_anchors, 0 },
    { { "uitofp", NULL, NULL, fconv_shape, call_uitofp, ref_uitofp },
      CQ_FCONV_PROG_SITOFP, host_uitofp, ev_uitofp, fconv_int_anchors, 0 }
};

enum { N_FCONV_ROWS = 4, FV_UITOFP = 3 };

_Static_assert(sizeof ROWS / sizeof ROWS[0] == (size_t)N_FCONV_ROWS,
               "M37 ships exactly four Rule 7 kernels; fpext and fptrunc stay "
               "aborts on PRD-v2 §7.9");

/* Arms `g_F`/`g_T`/`g_anchors` for row `r` at source width `F`. Every entry
 * point below goes through it so no case can drive a kernel with another's
 * anchor provider — which would decline at the wrong width and silently run
 * eight random draws instead of the anchor block. */
static void fconv_arm(const fconv_krow *r, int F)
{
    g_F = F;
    g_T = CQ_FP64_W;
    g_anchors = r->anchors;
}

/* ---- One forward call, at an explicit value and mask. ------------------- */

static uint64_t fconv_run(const fconv_krow *r, int F, uint64_t va, cq_ref_w qa,
                          cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_kd_shape sh;
    int32_t ha, hd;
    const cq_bit *src[1];
    uint64_t got;

    fconv_arm(r, F);
    fconv_shape(F, &sh);
    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);

    ha = cq_bk_reg_w(&ctx, (uint32_t)F, cq_ref_w_make(va, 0u, F), qa);
    hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)CQ_FP64_W);
    src[0] = cq_reg_cbits(&ctx.regs, ha);

    cq_mock_reset(m);                     /* drop the rail's materialising Xs */
    r->spec.call(&ctx, cq_reg_bits(&ctx.regs, hd), src, &sh);
    got = cq_pc_value(&ctx, hd);
    cq_ctx_dispose(&ctx);
    return got;
}

/* ---- L1 + L2 + L3 + L5. ------------------------------------------------- */

CQ_TEST(l1_sweep)
{
    const int *ws;
    int nw;

    for (int i = 0; i < N_FCONV_ROWS; i++) {
        if (i == FV_UITOFP) continue;
        fconv_arm(&ROWS[i], CQ_FP64_W);
        cq_kd_sweep_at(&ROWS[i].spec, CQ_FP64_W);
    }

    /* THE FOUR uitofp PAIRS, ENUMERATED. The row program is sitofp's at every
     * one of them, so what a narrow width exercises that 64 does not is the
     * ZEXT VIEW at the operand — the lanes the fold table elides — which is
     * exactly what a width sweep is for (CLAUDE.md: widths are enumerated,
     * never sampled). */
    ws = cq_fconv_uitofp_widths(&nw);
    for (int w = 0; w < nw; w++) {
        fconv_arm(&ROWS[FV_UITOFP], ws[w]);
        cq_kd_sweep_at(&ROWS[FV_UITOFP].spec, ws[w]);
    }
}

/* ---- Step 20's four regions, and §9's gate-tuple transform. -------------- */

/* THE REGION BODY IS A HANDFUL OF EXPLICIT CASES AND NOT THE SWEEP, because
 * kerneldrv.h's instruction — "`body` should be the suite's sweep at its CHEAP
 * widths only" — has no cheap width to offer for the three f64->f64 kernels.
 * What the region sweep adds over `cq_kd_check_promotion` is the axis's
 * interaction with the §3 fold table across a real L1 case, including the
 * ALL-CLASSICAL row at CQ_KD_CTRL_Q0 that no other fixture in the project
 * reaches. `uitofp` at i8 is the cheap width and carries the narrow-view row.
 */
static void fconv_narrow(void)
{
    static const uint64_t FPV[] = {
        CQ_F64_ONE,                      /* an ordinary in-range integer  */
        UINT64_C(0x43E158E460913D00),    /* 1e19 — fptoui's biased half   */
        CQ_F64_QNAN_A,                   /* the saturating branch         */
        UINT64_C(0xBFE0000000000000)     /* -0.5, the negative-zero arm   */
    };
    static const uint64_t IV[] = { 1u, 3u, 0x80u, 0xFFu };
    cq_bk_pair pairs[CQ_FP64_W + 12];
    uint32_t np = cq_bk_fixed_pairs((uint32_t)CQ_FP64_W, pairs,
                                    (uint32_t)(CQ_FP64_W + 12));

    for (int i = 0; i < N_FCONV_ROWS; i++)
        for (int v = 0; v < 4; v++) {
            cq_ref_w vv[CQ_KD_MAX_SRC];
            int F = (i == FV_UITOFP) ? 8 : CQ_FP64_W;

            fconv_arm(&ROWS[i], F);
            vv[0] = cq_ref_w_make(ROWS[i].fp_source ? FPV[v] : IV[v], 0u, F);
            vv[1] = cq_ref_w_zero();
            vv[2] = cq_ref_w_zero();
            cq_kd_case(&ROWS[i].spec, F, vv, &pairs[0]);   /* all-classical */
            cq_kd_case(&ROWS[i].spec, F, vv, &pairs[1]);   /* all-quantum   */
        }
    CHECK(np >= 2u);
}

CQ_TEST(controlled)
{
    uint64_t reached = 0u;
    const int *ws;
    int nw;

    cq_kd_for_each_region("fconv anchors", fconv_narrow);

    for (int i = 0; i < N_FCONV_ROWS; i++) {
        if (i == FV_UITOFP) continue;
        fconv_arm(&ROWS[i], CQ_FP64_W);
        reached += cq_kd_check_promotion(&ROWS[i].spec, CQ_FP64_W);
    }
    ws = cq_fconv_uitofp_widths(&nw);
    for (int w = 0; w < nw; w++) {
        fconv_arm(&ROWS[FV_UITOFP], ws[w]);
        reached += cq_kd_check_promotion(&ROWS[FV_UITOFP].spec, ws[w]);
    }

    /* NOT VACUOUS: the promotion identity holds trivially where the
     * uncontrolled tuple is empty, so the caller owns the non-vacuity claim
     * (cq_kd_check_promotion returns the uncontrolled total for this reason). */
    CHECK(reached > 0u);
}

/* ---- L4: the goldens, at the all-quantum mask. -------------------------- */

static void check_counts(cq_gold *g, const fconv_krow *r, int F)
{
    cq_counter fwd, unc;

    fconv_arm(r, F);
    cq_kd_measure(&r->spec, F, &fwd, &unc);

    CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
    CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);

    /* THE W COLUMN IS THE SOURCE WIDTH, which is what distinguishes `uitofp`'s
     * four rows from each other; the target is 64 for all five pairs. */
    cq_gold_check(g, r->spec.name, "forward", F, fwd.x, fwd.cx, fwd.ccx);
    cq_gold_check(g, r->spec.name, "unc",     F, unc.x, unc.cx, unc.ccx);
}

CQ_TEST(l4_goldens)
{
    cq_gold g;
    const int *ws;
    int nw;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/fconv.counts",
                      "M37 kernels/fconv.c — K19 the f64 <-> integer "
                      "conversions (sandwiched)",
                      "ALL-QUANTUM on `a`, whose width is the W column; the "
                      "target is 64 for every row. Views and constant spans "
                      "fold at every mask, so these sit below twice the slot "
                      "count; uitofp's four rows are ONE row program under "
                      "four zext views",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (int i = 0; i < N_FCONV_ROWS; i++)
        if (i != FV_UITOFP) check_counts(&g, &ROWS[i], CQ_FP64_W);

    ws = cq_fconv_uitofp_widths(&nw);
    for (int w = 0; w < nw; w++) check_counts(&g, &ROWS[FV_UITOFP], ws[w]);

    CHECK(cq_gold_close(&g));
}

#include "test_kernel_fconv_value.inc"
#include "test_kernel_fconv_slots.inc"
#include "test_kernel_fconv_scan.inc"
#include "test_kernel_fconv_spans.inc"

CQ_TEST_MAIN_ARGV(
    CQ_CASE(the_three_programs_are_well_formed_and_reference_only_backwards),
    CQ_CASE(the_clz_ladder_is_five_uniform_stages_and_a_short_sixth),
    CQ_CASE(fptoui_inlines_fptosi_twice_at_two_disjoint_offsets),
    CQ_CASE(uitofps_row_program_is_sitofps_byte_for_byte_at_every_width),
    CQ_CASE(no_named_span_crosses_the_int_to_fp_seam),
    CQ_CASE(the_classical_row_agrees_with_the_oracle_on_every_anchor),
    CQ_CASE(every_undefined_cell_saturates_to_x86s_indefinite_value),
    CQ_CASE(the_i64_row_upstream_would_have_emitted_is_wrong_and_the_others_are_not),
    CQ_CASE(the_blocks_cost_what_their_modules_say_they_cost),
    CQ_CASE(each_program_is_the_sum_of_its_rows),
    CQ_CASE(the_slot_boundaries_match_an_independent_four_valued_scan),
    CQ_CASE(the_programs_spans_are_pairwise_disjoint),
    CQ_CASE(the_compute_half_is_a_palindrome_at_an_asymmetric_mask),
    CQ_CASE(two_fptosi_blocks_in_one_region_do_not_collide),
    CQ_CASE(dst_owns_its_lanes_and_the_scratch_comes_back),
    CQ_CASE(l1_sweep),
    CQ_CASE(controlled),
    CQ_CASE(l4_goldens)
)
