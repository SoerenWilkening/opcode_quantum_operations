/* Step 26's numeric half: M25b, the double → (p, q_denom) conversion.
 *
 * PRD §15 D19. `qec_rz(ctx, q, long p, long q_denom, int precision)` means
 * θ = π·p/q_denom EXACTLY — the pair reaches the gridsynth driver as integers
 * at 4·precision + 96 bits and is never divided — so what this module owes is a
 * best-rational approximation of θ/π under a denominator cap.
 *
 * THIS SUITE RUNS ON EVERY BOX, WITH OR WITHOUT THE QEC LIBRARY, which is half
 * the reason plan §3 recorded the seam here. The module depends on <math.h>
 * and nothing else.
 *
 * THE ORACLE IS AN EXACT RESIDUAL, NOT A SECOND CONVERSION, and that is the
 * Step 18 lesson applied: an oracle that shares a constant with the code is
 * blind to what that constant gets wrong. What is under test here is how θ/π is
 * FORMED — the mutant is "divide by the rounded π and stop" — so an oracle that
 * reconstructs π·p/q from the same PI_HI/PI_LO pair is independent of the thing
 * being checked, while being far more accurate than a double can be. It uses
 * `fma` to recover each product's rounding, so it is exact to ~1e-33 relative
 * and needs no `long double` (which is 64-bit on some Apple targets and would
 * make the whole instrument silently vacuous there).
 *
 * MEASURED 2026-08-28, and it is what makes the sub-half-ulp claim below a
 * DISCRIMINATOR rather than a description: across 1e-6 … 1e15 the shipped
 * conversion's worst residual is 0.443 ulp and is 0.000 over most of the range,
 * while the same continued fraction fed `theta / PI_HI` sits at ~1 ulp in EVERY
 * decade (worst 1.437, best 0.685). Dropping the PI_LO term therefore fails
 * this case at essentially every magnitude, not at one contrived one.
 */

#include "sink_qec_angle.h"
#include "support/harness.h"

#include <float.h>
#include <math.h>

/* The same double-double π the module uses. See the header note on why sharing
 * it does not disarm this oracle. */
#define PI_HI 3.14159265358979323846264338327950288
#define PI_LO 1.2246467991473531772e-16

/* |π·p/q − θ|, to ~1e-33 relative — VALID ONLY WHERE p AND q ARE EXACTLY
 * REPRESENTABLE AS DOUBLES, which is what oracle_is_exact below enforces.
 *
 * π·p/q − θ = (π·p − θ·q)/q. Both products are recovered EXACTLY as a
 * (high, low) pair by fma — for x·y the low part is fma(x, y, -high) — and the
 * two highs are then subtracted by a Knuth two-sum, which is exact for any
 * magnitudes rather than only for the near-equal case. The whole point is the
 * cancellation: π·p and θ·q agree to ~16 digits and the answer lives in what is
 * left, so an ordinary `a - b` would return zero and the case would pass
 * against anything. */
static double residual(double theta, long p, long q)
{
    double dp = (double)p, dq = (double)q;

    double a_hi = PI_HI * dp, a_lo = fma(PI_HI, dp, -a_hi) + PI_LO * dp;
    double b_hi = theta * dq, b_lo = fma(theta, dq, -b_hi);

    double y  = -b_hi;
    double s  = a_hi + y;
    double bv = s - a_hi;
    double e  = (a_hi - (s - bv)) + (y - bv);

    return fabs((s + (e + (a_lo - b_lo))) / dq);
}

/* THE ORACLE'S LIMIT, NOT THE MODULE'S, and it has to be stated rather than
 * hidden: `(double)p` ROUNDS once |p| passes 2^53, and p legitimately reaches
 * ~9e18 (the `long` bound is what stops the expansion at large |θ|). Feeding a
 * rounded p into the residual above adds ~2 ulp of pure oracle noise, which is
 * larger than the thing being measured — measured, it turned a genuine 0.000
 * into 0.772 at θ = 490000. So those points are EXCLUDED and counted, never
 * silently tolerated by a looser bound. */
#define CQ_EXACT_INT 9007199254740992.0   /* 2^53 */

static int oracle_is_exact(long p, long q)
{
    return fabs((double)p) <= CQ_EXACT_INT && (double)q <= CQ_EXACT_INT;
}

static double ulp_of(double x)
{
    double u = nextafter(fabs(x), INFINITY) - fabs(x);
    return u > 0.0 ? u : 0x1p-1074;
}

static void expect_ratio(double theta, long cap, long want_p, long want_q)
{
    long p, q;
    cq_qec_ratio(theta, cap, &p, &q);
    if (p != want_p || q != want_q)
        cq_h_fail(__FILE__, __LINE__,
                  "theta=%a cap=%ld: got %ld/%ld, want %ld/%ld",
                  theta, cap, p, q, want_p, want_q);
}

/* -------------------------------------------------------------------------
 * §7's folding rows, which are the ones that must cost nothing.
 * ------------------------------------------------------------------------- */

/* Every row §7 folds is an exact multiple of π, so it must land on q = 1 and
 * nothing else: those are the angles the QEC library's own degenerate branch
 * recognises — MEASURED, π·1/1 and π·1/2 emit ZERO T gates at every precision —
 * and a denominator of anything but 1 would turn a free Clifford into a
 * synthesised rotation costing ~3·log₂(1/ε) T.
 *
 * THE HARD PART IS THE ONE THAT LOOKS TRIVIAL. θ = k·π is spelled as the double
 * k·PI_HI, whose exact quotient by TRUE π is k − k·3.9e-17, i.e. JUST BELOW the
 * integer — so a floor taken on the leading double alone returns k and a floor
 * taken on the pair returns k − 1. The module's dd_floor is what settles it,
 * and both answers reach q = 1 by different routes: k directly, or [k−1; 1, …]
 * whose second convergent is k/1 and whose third has a denominator of ~1e16,
 * far over the cap. */
CQ_TEST(zero_and_the_folding_rows_land_on_denominator_one)
{
    expect_ratio(0.0, CQ_QEC_DENOM_CAP, 0, 1);

    for (long k = 1; k <= 8; k++) {
        expect_ratio((double)k * PI_HI, CQ_QEC_DENOM_CAP,  k, 1);
        expect_ratio(-(double)k * PI_HI, CQ_QEC_DENOM_CAP, -k, 1);
    }
    expect_ratio(100.0 * PI_HI, CQ_QEC_DENOM_CAP, 100, 1);
}

/* D19's band is [2^32, 2^48] and it was established by bisection on exactly
 * this: a nice angle must keep snapping to its nice rational across the whole
 * of it. The snapping is a MECHANISM rather than luck — a double near a simple
 * rational has an enormous partial quotient, so the convergent survives an
 * enormous range of caps. Checking the ends and the shipped middle is what
 * turns "we picked 2^40" into "2^40 is not near an edge". */
CQ_TEST(the_clifford_angles_snap_across_d19s_whole_measured_band)
{
    static const long caps[] = { 1L << 32, CQ_QEC_DENOM_CAP, 1L << 48 };

    for (size_t i = 0; i < sizeof caps / sizeof caps[0]; i++) {
        expect_ratio(PI_HI / 2.0,  caps[i],  1, 2);
        expect_ratio(PI_HI / 4.0,  caps[i],  1, 4);
        expect_ratio(PI_HI / 8.0,  caps[i],  1, 8);
        expect_ratio(3.0 * PI_HI / 4.0, caps[i], 3, 4);
        expect_ratio(-PI_HI / 4.0, caps[i], -1, 4);
    }
}

/* THE CAP FAILS AT BOTH ENDS AND THIS IS THE END THAT MISCOMPILES. Too small,
 * and the corpus's own `3.14` — which is a REAL rotation, not π — becomes 1/1: a
 * silent 1.593e-03 rad rewrite to π, which is D10's named miscompile arriving
 * through a second door. The second half of the case is the negative control
 * that matters: at the shipped cap the same angle is faithful.
 *
 * (The other end is not a value error at all. A cap far too large stops a nice
 * angle snapping — fl(π/4) becomes 365555973729107575/1462223894916430357, the
 * same double and the same round-trip — so no value check can see it and only
 * the emitted T-count can. That is why the case above sweeps the band instead
 * of asserting an error here.) */
CQ_TEST(a_cap_too_small_is_d10s_own_miscompile_and_the_shipped_cap_is_not)
{
    expect_ratio(3.14, 1L << 4, 1, 1);

    long p, q;
    cq_qec_ratio(3.14, 1L << 4, &p, &q);
    double bad = residual(3.14, p, q);
    CHECK(bad > 1.5e-3 && bad < 1.7e-3);

    cq_qec_ratio(3.14, CQ_QEC_DENOM_CAP, &p, &q);
    double good = residual(3.14, p, q);
    if (!(good < 1e-15))
        cq_h_fail(__FILE__, __LINE__,
                  "3.14 at the shipped cap: residual %a is not below 1e-15", good);
}

/* THE CONTRACT, AND THE CASE THAT PINS THE DOUBLE-DOUBLE QUOTIENT. THREE BANDS,
 * because one bound over one range CANNOT do the job — measured, not assumed.
 *
 * THIS CASE WAS WEAKER THAN ITS OWN COMMENT UNTIL A MUTATION BATTERY SAID SO.
 * Its first form asserted `worst < 0.5 ulp` over 1e-6 … 1e15 and claimed to pin
 * D19's "form θ/π against MORE than double-precision pi" rider. It killed a
 * fully naive `theta / PI_HI` quotient (~1 ulp) and it did NOT kill the mutant
 * that actually matters: dropping ONLY the `- q0 * PI_LO` term while keeping
 * the fma-recovered residual. That variant is accurate to 0.35 ulp — it fixes
 * the division's rounding and leaves the π DRIFT, which is |θ|·3.9e-17 — so it
 * sat comfortably under the bound. Two other things the battery exposed about
 * the sweep, both of which the comment got wrong: the exclusion below removes
 * EVERY point above ~1e5 (p passes 2^53 there), so "twenty-two decades" was
 * describing a range the case never reached; and at the small end the bound was
 * being set by the CAP rather than by the arithmetic, which is why the two
 * variants agree exactly there.
 *
 * THE BANDS, and what each is for. All figures MEASURED 2026-08-28.
 *
 *   A. shipped cap, 1e-4 … 1e5.  The arithmetic band: the cap does not bind and
 *      the oracle is exact. dd = 0.00243, no-PI_LO = 0.35055 over 845 points.
 *      Bound 0.05 — a 20x margin below the mutant and a 20x margin above the
 *      truth. THIS IS THE BAND THAT PINS PI_LO.
 *   B. cap 2^30, 1e0 … 1e12.  THE CAP IS A PARAMETER, so a smaller one keeps p
 *      under 2^53 and lets the identical arithmetic be checked SEVEN DECADES
 *      HIGHER — which is where the π drift is largest in absolute terms and
 *      where band A cannot see at all. dd = 0.06337, no-PI_LO = 0.37224 over
 *      735 points. Bound 0.15.
 *   C. shipped cap, 1e-6 … 1e-5.  The small end, where the CAP binds: both
 *      variants give exactly 0.44325, so nothing about the quotient is being
 *      tested and the only honest bound is the loose one. Kept because it is
 *      the regime the fold-to-identity case sits next to.
 */

/* Worst residual in ulps over a decade band, skipping the points the oracle
 * cannot certify and reporting how many it kept. */
static double worst_ulp(long cap, int elo, int ehi, long *checked)
{
    double worst = 0.0;
    *checked = 0;

    for (int e = elo; e <= ehi; e++) {
        for (int m = 1; m <= 99; m++) {
            double theta = (double)m / 10.0 * pow(10.0, (double)e);
            long p, q;
            cq_qec_ratio(theta, cap, &p, &q);
            if (!oracle_is_exact(p, q)) continue;
            (*checked)++;

            double u = residual(theta, p, q) / ulp_of(theta);
            if (u > worst) worst = u;
        }
    }
    return worst;
}

static void band(const char *name, long cap, int elo, int ehi,
                 double bound, long floor_n)
{
    long   checked = 0;
    double worst = worst_ulp(cap, elo, ehi, &checked);

    /* A FLOOR ON THE SAMPLE, because the exclusion is the one way a band could
     * go vacuous: a mutant that made every p huge would skip everything and
     * pass with worst == 0. */
    if (checked < floor_n)
        cq_h_fail(__FILE__, __LINE__,
                  "band %s: only %ld points in the oracle's exact range (want >= %ld)",
                  name, checked, floor_n);

    if (!(worst < bound))
        cq_h_fail(__FILE__, __LINE__,
                  "band %s: worst residual %.5f ulp over %ld points (want < %.2f)",
                  name, worst, checked, bound);
}

CQ_TEST(the_conversion_is_sub_ulp_and_the_double_double_quotient_is_why)
{
    band("A arithmetic",  CQ_QEC_DENOM_CAP, -4,  5, 0.05, 800);
    band("B high angles", 1L << 30,          0, 12, 0.15, 700);
    band("C cap-bound",   CQ_QEC_DENOM_CAP, -6, -5, 0.50, 190);
}

/* The sign rides on p and never on the denominator, because `qec_rz`'s q_denom
 * is the thing it validates as nonzero and its own guard is a magnitude test. A
 * negative denominator would be accepted and would flip the rotation. */
CQ_TEST(a_negative_angle_keeps_a_positive_denominator)
{
    static const double thetas[] = { -0.1, -2.5, -3.14, -1e7, -PI_HI / 8.0 };

    for (size_t i = 0; i < sizeof thetas / sizeof thetas[0]; i++) {
        long p, q;
        cq_qec_ratio(thetas[i], CQ_QEC_DENOM_CAP, &p, &q);
        CHECK(q > 0);
        CHECK(p < 0);
        if (oracle_is_exact(p, q))
            CHECK(residual(thetas[i], p, q) < 1e-15 * fabs(thetas[i]) + 1e-300);
    }
}

/* THE CAP'S LOWER END IS AN IDENTITY FOLD, AND IT IS CORRECT RATHER THAN A
 * SILENT DELETION — but only because of a margin that has to be stated. p = 0
 * requires θ/π < 1/cap, i.e. |θ| < π/2^40 ≈ 2.9e-12 rad, and Rz of that differs
 * from the identity by ~1.4e-12 in operator norm. The synthesis tolerance is
 * ε = 2^−precision, and src/sink_qec.c refuses a precision above 30 (ε ≈
 * 9.3e-10) for exactly this reason: 320× the fold's floor. Raise the precision
 * cap without raising the denominator cap and this fold stops being sound. */
CQ_TEST(an_angle_below_pi_over_the_cap_folds_to_the_identity)
{
    expect_ratio(1e-300, CQ_QEC_DENOM_CAP, 0, 1);
    expect_ratio(0x1p-1074, CQ_QEC_DENOM_CAP, 0, 1);

    /* And the first angle that does NOT fold is where the arithmetic says it
     * should be: just above π/cap, not at some accident of the loop. */
    long p, q;
    cq_qec_ratio(1e-11, CQ_QEC_DENOM_CAP, &p, &q);
    CHECK(p != 0);
    CHECK(residual(1e-11, p, q) < 1e-11);
}

/* Past ~1e15 the `long p` guard, not the cap, is what stops the expansion — so
 * widening the cap buys nothing there. Pinned because it is the reading that
 * makes the previous case's upper bound honest: if these three ever disagreed,
 * the cap would be the binding constraint and the 1e15 fence would be wrong. */
CQ_TEST(the_long_bound_and_not_the_cap_binds_at_a_huge_angle)
{
    long p32, q32, p40, q40, p48, q48;
    cq_qec_ratio(1e12, 1L << 32, &p32, &q32);
    cq_qec_ratio(1e12, CQ_QEC_DENOM_CAP, &p40, &q40);
    cq_qec_ratio(1e12, 1L << 48, &p48, &q48);

    CHECK(p32 == p40 && q32 == q40);
    CHECK(p48 == p40 && q48 == q40);
    CHECK(q40 > 1);
}

CQ_TEST_MAIN(
    CQ_CASE(zero_and_the_folding_rows_land_on_denominator_one),
    CQ_CASE(the_clifford_angles_snap_across_d19s_whole_measured_band),
    CQ_CASE(a_cap_too_small_is_d10s_own_miscompile_and_the_shipped_cap_is_not),
    CQ_CASE(the_conversion_is_sub_ulp_and_the_double_double_quotient_is_why),
    CQ_CASE(a_negative_angle_keeps_a_positive_denominator),
    CQ_CASE(an_angle_below_pi_over_the_cap_folds_to_the_identity),
    CQ_CASE(the_long_bound_and_not_the_cap_binds_at_a_huge_angle)
)
