/* tests/test_kernel_shift.c — M11, Step 11. K4 constant shl / lshr / ashr.
 *
 * THE SUBJECT OF THIS FILE IS D8, not the shuffle. The three loops are four
 * lines each and Bennett's; what needed deciding — and what a future reader
 * will be tempted to "fix" — is what happens when the shift amount is out of
 * range. PRD §15 D8: MASK, THEN SATURATE, one formula for both the constant
 * path here and the variable barrel shifter at Step 14:
 *
 *     dst ^= sat_shift(a, k mod 2^ceil(log2 W))
 *
 * The cases below pin every corner of that, because the corners are where the
 * two paths would drift apart and the drift would be invisible: a kernel that
 * saturated instead of masking still passes L1 against a reference that made
 * the same mistake, which is why the reference here calls the LIBRARY's
 * cq_shift_stages rather than recomputing ceil(log2 W) on its own.
 */

#include "kernels/shift_const.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "reg.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/goldens.h"
#include "support/harness.h"
#include "support/kerneldrv.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <stdio.h>

/* The amount operand is CONSTRAINED TO BE CLASSICAL. A quantum amount is not a
 * harder case for M11, it is a different module: M12's barrel shifter. Handing
 * one here is a hard error, so the driver must never generate it — which is
 * exactly what `classical[1] = ~0` tells cq_bk_fixed_pairs. */
static void shift_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->classical[1] = ~(uint64_t)0;
}

/* The reference's reduction goes through the LIBRARY's cq_shift_stages, not
 * through a second ceil(log2 W). D8's whole content is that one reduction is
 * shared by three implementations; a reference that computed its own would
 * turn the one thing under test into the one thing assumed. */
static int eff_k(const cq_ref_w *src, int W)
{
    int s = cq_shift_stages(W);
    return (int)(src[1].lo & ((s >= 64) ? ~(uint64_t)0 : (((uint64_t)1 << s) - 1u)));
}

/* The two-word reference, because i80 and i128 are shipped shift widths and a
 * one-word model aborts above 64. The one-word cq_ref_shl/lshr/ashr are kept
 * as an INDEPENDENT cross-check below 64 — different arithmetic (word shifts
 * and an explicit fill) against these bit loops. */
static cq_ref_w ref_shl(const cq_ref_w *src, const cq_kd_shape *sh)
{ return cq_ref_w_shl(src[0], eff_k(src, sh->w_dst), sh->w_dst); }
static cq_ref_w ref_lshr(const cq_ref_w *src, const cq_kd_shape *sh)
{ return cq_ref_w_lshr(src[0], eff_k(src, sh->w_dst), sh->w_dst); }
static cq_ref_w ref_ashr(const cq_ref_w *src, const cq_kd_shape *sh)
{ return cq_ref_w_ashr(src[0], eff_k(src, sh->w_dst), sh->w_dst); }

static const cq_kd_spec SHL  = { "shl",  cq_kernel_shl,  NULL,
                                 shift_shape, NULL, ref_shl  };
static const cq_kd_spec LSHR = { "lshr", cq_kernel_lshr, NULL,
                                 shift_shape, NULL, ref_lshr };
static const cq_kd_spec ASHR = { "ashr", cq_kernel_ashr, NULL,
                                 shift_shape, NULL, ref_ashr };

/* ---- L1 + L2 + L3 + L5, the whole sweep. -------------------------------- */

/* The standard ladder, then the two shipped widths above it. i80 carries 47%
 * of the corpus's shift calls and is where D8's saturating branch lives; i128
 * is reachable through casts. Neither is in cq_kd_sweep's default ladder,
 * which stops at 64 because that is where a one-word value model stopped. */
static void sweep_shift(const cq_kd_spec *k)
{
    cq_kd_sweep(k);
    cq_kd_sweep_at(k, 80, 0);
    cq_kd_sweep_at(k, 128, 0);
}

CQ_TEST(k4_shl_sweep)  { sweep_shift(&SHL);  }
CQ_TEST(k4_lshr_sweep) { sweep_shift(&LSHR); }
CQ_TEST(k4_ashr_sweep) { sweep_shift(&ASHR); }

/* Step 20 — the same four levels under PRD §9's four regions, plus §9's
 * gate-tuple transform at every shipped width. The sweep body is this suite's
 * OWN, at its cheap widths only: the promotion is per gate and width-
 * independent, so what the axis adds is its interaction with the §3 fold table
 * — a property of the bit-KIND space (D6: the table reads kind, never value),
 * densest at narrow widths, where cq_kd_samples() draws cover most of the
 * mask-pair pool. Nothing has been exhaustive since 2026-08-21 (bd p3z). Every
 * shipped width is still covered by cq_kd_check_promotion, at two kernel calls
 * apiece. */
static const cq_kd_spec *const SHIFTS[] = { &SHL, &LSHR, &ASHR };

static void shift_narrow(void)
{
    for (size_t i = 0; i < sizeof SHIFTS / sizeof SHIFTS[0]; i++)
        for (int W = 1; W <= 5; W++) cq_kd_sweep_at(SHIFTS[i], W, 1);
}

CQ_TEST(controlled)
{
    uint64_t reached = 0u;

    /* i80 and i128 are in the promotion ladder for the same reason they are in
     * the sweep: i80 carries 47% of the corpus's shift calls and is where D8's
     * saturating branch lives. */
    static const int widths[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 80, 128 };

    cq_kd_for_each_region("shift_const", shift_narrow);

    for (size_t i = 0; i < sizeof SHIFTS / sizeof SHIFTS[0]; i++)
        for (size_t w = 0; w < sizeof widths / sizeof widths[0]; w++)
            reached += cq_kd_check_promotion(SHIFTS[i], widths[w]);
    /* NOT VACUOUS: the identity above is an equality between two measurements,
     * and it holds trivially where the uncontrolled tuple is empty. K4 makes
     * that a real case rather than a hypothetical — its all-ones L4 fixture
     * saturates under D8 at every non-power-of-two width — so the ladder has to
     * say it reached something. */
    CHECK(reached > 0u);
}

/* ---- D8, corner by corner, against a hand-written expectation. ---------- */

/* Runs one (op, W, k) and returns dst's value. Deliberately does NOT go
 * through the reference model: these cases state the answer in the test, so a
 * reference that drifted would be caught rather than agreed with. */
static cq_ref_w run_shift_wide(const cq_kd_spec *k_spec, int W, uint64_t a,
                               uint64_t k, cq_counter *fwd, cq_counter *unc)
{
    cq_ctx ctx; cq_counter cnt; cq_sink sink;
    cq_count_reset(&cnt);
    sink = cq_sink_counter(&cnt);
    cq_ctx_init(&ctx, &sink);

    /* TWO-WORD, because i80 is one of the cases below and cq_ref_mask stops at
     * 64 by design. The values these cases use all fit the low word; the high
     * word is asserted zero on the way out rather than dropped. */
    int32_t ha = cq_bk_reg_w(&ctx, (uint32_t)W, cq_ref_w_make(a, 0u, W),
                             cq_ref_w_ones(W));                  /* quantum  */
    int32_t hb = cq_bk_reg_w(&ctx, (uint32_t)W, cq_ref_w_make(k, 0u, W),
                             cq_ref_w_zero());                   /* classical */
    int32_t hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);

    cq_count_reset(&cnt);
    k_spec->kernel(&ctx, cq_reg_bits(&ctx.regs, hd),
                   cq_reg_cbits(&ctx.regs, ha), cq_reg_cbits(&ctx.regs, hb), W);
    if (fwd) *fwd = cnt;

    cq_ref_w v = cq_pc_value_w(&ctx, hd);

    /* The UNCOMPUTE pass, measured SEPARATELY (Rule 14: their equality is not
     * an invariant and must never be asserted). Run after the value is read,
     * so the forward result is what the caller sees. */
    if (unc) {
        cq_count_reset(&cnt);
        k_spec->kernel(&ctx, cq_reg_bits(&ctx.regs, hd),
                       cq_reg_cbits(&ctx.regs, ha), cq_reg_cbits(&ctx.regs, hb), W);
        *unc = cnt;
    }

    cq_ctx_dispose(&ctx);
    return v;
}

/* The value-only form the D8 corner cases use. Every one of them is stated at
 * a width and value whose RESULT fits the low word, so a high word here means
 * the case has drifted from what it claims to check rather than that the
 * kernel is wrong — which is why the guard lives on this wrapper and not on
 * run_shift_wide, whose callers legitimately reach i80 and i128. */
static uint64_t run_shift(const cq_kd_spec *k_spec, int W, uint64_t a, uint64_t k,
                          uint32_t *cx_out)
{
    cq_counter fwd;
    cq_ref_w v = run_shift_wide(k_spec, W, a, k, &fwd, NULL);

    if (v.hi != 0u)
        cq_h_fail(__FILE__, __LINE__,
                  "%s W=%d k=%llu: result has bits above 64 (0x%llx), which "
                  "this case did not expect", k_spec->name, W,
                  (unsigned long long)k, (unsigned long long)v.hi);

    if (cx_out) *cx_out = (uint32_t)fwd.cx;
    return v.lo;
}

/* THE ckd.16 CASE, and the sharpest one there is. At W=32 the constant path
 * would annihilate the value if it saturated at k >= W; D8 masks first, 32 mod
 * 32 is 0, and the shift is the identity. This is the knowing delta from
 * Bennett's constant path (PRD §15) — do not "fix" it to 0. */
CQ_TEST(d8_a_shift_by_exactly_the_width_is_the_identity)
{
    CHECK_EQ(run_shift(&SHL,  32, 0x800000FFull, 32, NULL), 0x800000FFull);
    CHECK_EQ(run_shift(&LSHR, 32, 0x800000FFull, 32, NULL), 0x800000FFull);
    CHECK_EQ(run_shift(&ASHR, 32, 0x800000FFull, 32, NULL), 0x800000FFull);

    /* And the bead's other example: k=40 at W=32 is a shift by 8. */
    CHECK_EQ(run_shift(&SHL,  32, 0xFFull, 40, NULL), 0xFF00ull);
    CHECK_EQ(run_shift(&LSHR, 32, 0xFF00ull, 40, NULL), 0xFFull);
}

/* THE OTHER HALF OF D8: masking is not enough on its own. At W=5, S=3, so the
 * amount reduces mod 8 and values in [5,8) survive the mask and must then
 * SATURATE. A kernel that only masked would emit a shift by 6 on a 5-bit
 * register; one that only saturated would give 0 for k=8 instead of identity.
 * Only mask-then-saturate satisfies both rows. */
CQ_TEST(d8_masking_alone_is_not_enough_at_a_non_power_of_two_width)
{
    CHECK_EQ(cq_shift_stages(5), 3);

    CHECK_EQ(run_shift(&SHL,  5, 0x1Full, 6, NULL), 0u);   /* 6 < 8: saturates */
    CHECK_EQ(run_shift(&LSHR, 5, 0x1Full, 7, NULL), 0u);
    CHECK_EQ(run_shift(&ASHR, 5, 0x1Full, 6, NULL), 0x1Full);  /* all-sign */
    CHECK_EQ(run_shift(&ASHR, 5, 0x0Full, 6, NULL), 0u);       /* sign 0 */

    CHECK_EQ(run_shift(&SHL,  5, 0x1Full, 8, NULL), 0x1Full);  /* 8 mod 8 = 0 */
    CHECK_EQ(run_shift(&SHL,  5, 0x01ull, 9, NULL), 0x02ull);  /* 9 mod 8 = 1 */
}

/* W=1 has ZERO stages (Bennett's `W <= 1 ? 0` guard), so no bit of the amount
 * is read and every shift is the identity. Pinned because it is the one width
 * where "mask to S bits" degenerates completely, and because a plausible
 * ceil(log2 1) = 0 vs 1 slip would show up here and nowhere else. */
CQ_TEST(d8_a_width_of_one_ignores_the_amount_entirely)
{
    CHECK_EQ(cq_shift_stages(1), 0);

    for (uint64_t k = 0; k < 4; k++) {
        CHECK_EQ(run_shift(&SHL,  1, 1u, k, NULL), 1u);
        CHECK_EQ(run_shift(&LSHR, 1, 1u, k, NULL), 1u);
        CHECK_EQ(run_shift(&ASHR, 1, 1u, k, NULL), 1u);
    }
}

/* The stage count itself, since three implementations share it. */
CQ_TEST(d8_the_stage_count_matches_ceil_log2)
{
    const int w[]  = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 80, 128 };
    const int s[]  = { 0, 1, 2, 2, 3, 3,  4,  5,  6,  7,   7 };

    for (size_t i = 0; i < sizeof w / sizeof w[0]; i++)
        CHECK_EQ(cq_shift_stages(w[i]), s[i]);
}

/* i80 IS WHERE THE TWO PATHS AGREE, which is the opposite of what ckd.16
 * expected and is why the bead's i80 example does not reproduce. S=7, so the
 * amount reduces mod 128, and every value in [80,128) still saturates — the
 * barrel does the same thing structurally. Pinned so that a later "simplify
 * the reduction to mod W" would go red here. */
CQ_TEST(d8_i80_saturates_across_the_whole_masked_interval)
{
    for (uint64_t k = 80; k < 128; k += 7)
        CHECK_EQ(run_shift(&SHL, 80, ~0ull, k, NULL), 0u);

    CHECK_EQ(run_shift(&SHL, 80, 1u, 128, NULL), 1u);   /* 128 mod 128 = 0 */
    CHECK_EQ(run_shift(&SHL, 80, 1u, 152, NULL), 1u << 24); /* 152 mod 128 = 24 */
}

/* THE TWO SHIFT MODELS, CROSS-CHECKED. cq_ref_w_* are bit loops; the one-word
 * cq_ref_* are word shifts with an explicit sign fill — genuinely different
 * arithmetic, so an error in one is unlikely to be repeated in the other. The
 * bit loops are the oracle above 64, where nothing else can check them, so
 * their agreement below 64 is what makes the wide half trustworthy. */
CQ_TEST(the_two_shift_models_agree_below_64)
{
    const int WS[] = { 1, 2, 5, 8, 32, 64 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        for (int k = 0; k <= W + 2; k++) {
            uint64_t vals[4] = { 0u, 1u, cq_ref_mask(W),
                                 cq_ref_mask(W) ^ (cq_ref_mask(W) >> 1) };

            for (int j = 0; j < 4; j++) {
                cq_ref_w a = cq_ref_w_make(vals[j], 0u, W);

                CHECK_EQ(cq_ref_w_shl (a, k, W).lo, cq_ref_shl (vals[j], k, W));
                CHECK_EQ(cq_ref_w_lshr(a, k, W).lo, cq_ref_lshr(vals[j], k, W));
                CHECK_EQ(cq_ref_w_ashr(a, k, W).lo, cq_ref_ashr(vals[j], k, W));
            }
        }
    }
}

/* ---- L5: the classical fold, per lane. ---------------------------------- */

/* A shift of a fully-classical register is a pure index shuffle on constants:
 * zero gates and zero qubits, at every k. The sweep's all-classical row
 * already asserts this; this states the shift-specific half — that the SHIFTED-IN
 * positions cost nothing even when `a` is quantum, which is K04.md §5(a)'s
 * headline saving over Bennett (which must allocate W wires regardless). */
CQ_TEST(l5_the_shifted_in_zeroes_are_free)
{
    uint32_t cx = 0;

    /* W=8, k=3, `a` all quantum: 5 CX and 5 qubits, not 8. */
    CHECK_EQ(run_shift(&SHL, 8, 0xFFull, 3, &cx), 0xF8ull);
    CHECK_EQ(cx, 5);

    CHECK_EQ(run_shift(&LSHR, 8, 0xFFull, 3, &cx), 0x1Full);
    CHECK_EQ(cx, 5);

    /* ashr is a FAN-OUT, not a shuffle: W CX flat in k, because the sign bit
     * is a control k+1 times (K04.md §5(d)). */
    CHECK_EQ(run_shift(&ASHR, 8, 0xFFull, 3, &cx), 0xFFull);
    CHECK_EQ(cx, 8);
}

/* ---- L4: the goldens. --------------------------------------------------- */

/* A shift's count is a function of (W, k), not of W alone, so the golden's key
 * carries k — the same trick the cast suite uses for its width pair. Pinned at
 * all-quantum `a` AND a classical amount, which is this file's whole departure
 * from the usual L4 mask and is why the golden's `measured-at` line says so in
 * its own words rather than inheriting a boilerplate claim (bd 2r5). */
static void check_shift_counts(cq_gold *g, const cq_kd_spec *spec, int W, int k)
{
    char key[24];
    cq_counter fwd, unc;
    uint64_t want_cx;

    (void)run_shift_wide(spec, W, cq_ref_w_ones(W).lo, (uint64_t)k, &fwd, &unc);

    /* The closed forms, read off the Julia bodies (K04.md §3) and not off our
     * emitter: shl and lshr copy W-k bits; ashr is W flat, because the tail is
     * filled from the sign rather than left alone.
     *
     * AT THE EFFECTIVE k, NOT THE ASKED-FOR ONE. A count is a function of what
     * D8 reduces the amount to, and the first draft of this line used the raw
     * k and went red on every saturating row — which is the formula catching
     * the formula, exactly what having it next to the golden is for. */
    int s  = cq_shift_stages(W);
    int ke = k & ((1 << s) - 1);
    if (ke >= W) ke = W;                      /* then saturate */

    if (cq_h_streq(spec->name, "ashr")) want_cx = (uint64_t)W;
    else                                want_cx = (uint64_t)(W - ke);

    /* THE FULL TUPLE, MEASURED. An earlier draft passed literal 0 for the
     * measured NOT and Toffoli columns and compared them against literal 0 —
     * an assertion that cannot fail, and one that let a stray X or CCX through
     * while the golden recorded zeros it had never observed. CLAUDE.md's own
     * rule is that the full tuple is always matched. */
    CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, 0, want_cx, 0);
    CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
    CHECK_EQ(unc.ry + unc.rz + unc.mz, 0);

    snprintf(key, sizeof key, "%s_k%d", spec->name, k);
    cq_gold_check(g, key, "forward", W, fwd.x, fwd.cx, fwd.ccx);
    /* Pinned SEPARATELY, never compared to the forward (Rule 14 / risk R6). */
    cq_gold_check(g, key, "unc", W, unc.x, unc.cx, unc.ccx);
}

CQ_TEST(l4_goldens)
{
    /* i80 AND i128 ARE IN THE LADDER, and i80 is not decoration: it is the
     * ONLY shipped width where D8's saturate half can fire at all (at every
     * power-of-two W, 2^S == W and the interval [W, 2^S) is empty), and 47% of
     * the corpus's 909 shift calls are at i80. A ladder that stopped at 64
     * would pin the half of D8 that cannot happen and skip the half that does. */
    static const int WS[] = { 1, 2, 4, 8, 16, 32, 64, 80, 128 };
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/shift_const.counts",
                      "M11 kernels/shift_const.c — K4 constant shl/lshr/ashr (D8)",
                      "ALL-QUANTUM value; the amount is a CLASSICAL immediate "
                      "(shape classical[1] = ~0), the only kind M11 accepts — "
                      "a quantum amount is M12; at ctrl_depth 0",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        /* k = 0, one, half and the saturating end — the four shapes the loop
         * bounds can take. */
        int ks[4] = { 0, 1, W / 2, W };

        for (int j = 0; j < 4; j++) {
            if (j && ks[j] == ks[j - 1]) continue;    /* W=1: 0,1,0,1 */
            check_shift_counts(&g, &SHL,  W, ks[j]);
            check_shift_counts(&g, &LSHR, W, ks[j]);
            check_shift_counts(&g, &ASHR, W, ks[j]);
        }
    }

    CHECK(cq_gold_close(&g));
}

/* ---- Zero ancillae, by peak. -------------------------------------------- */

CQ_TEST(the_shifts_allocate_only_dst)
{
    const cq_kd_spec *ks[3] = { &SHL, &LSHR, &ASHR };

    for (int i = 0; i < 3; i++) {
        uint32_t peak = 0;
        uint32_t owned = cq_kd_peak(ks[i], 8, &peak);

        /* The PEAK is the instrument: L2 looks after the call, so a kernel
         * that took scratch and tidily released it would pass L2 and fail
         * here. dst owns exactly what it was given gates for. */
        CHECK_EQ(peak, owned);
    }
}

CQ_TEST_MAIN_ARGV(
    CQ_CASE(k4_shl_sweep),
    CQ_CASE(k4_lshr_sweep),
    CQ_CASE(k4_ashr_sweep),
    CQ_CASE(controlled),
    CQ_CASE(d8_a_shift_by_exactly_the_width_is_the_identity),
    CQ_CASE(d8_masking_alone_is_not_enough_at_a_non_power_of_two_width),
    CQ_CASE(d8_a_width_of_one_ignores_the_amount_entirely),
    CQ_CASE(d8_the_stage_count_matches_ceil_log2),
    CQ_CASE(d8_i80_saturates_across_the_whole_masked_interval),
    CQ_CASE(the_two_shift_models_agree_below_64),
    CQ_CASE(l5_the_shifted_in_zeroes_are_free),
    CQ_CASE(l4_goldens),
    CQ_CASE(the_shifts_allocate_only_dst)
)
