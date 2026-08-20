/* src/angle.c — M21. PRD §7's row selection, and nothing else.
 *
 * THE PREDICATE, stated once so the tests can be written against it rather than
 * against this code:
 *
 *     θ sits on the π-lattice at index k, to tolerance `tol`, iff
 *         k = round(θ / π)  and  |θ − k·π| ≤ tol·π
 *
 * and the §7 row is then read off `k mod 4`: 0 → I, 2 → −I, odd → the half
 * turn. One congruence test serves all three rows, because they ARE one lattice
 * seen at three granularities — θ ≡ 0 (mod 4π) is k ≡ 0 (mod 4), θ ≡ 2π
 * (mod 4π) is k ≡ 2 (mod 4), and θ ≡ π (mod 2π) is k odd. Testing the three
 * separately would leave the reader to prove they cannot both fire.
 *
 * THE WINDOW IS ABSOLUTE — `tol·π`, not `tol·|θ|` — AND THAT WAS MEASURED, NOT
 * PREFERRED (PRD §15 D10). §7 says "1e-12 relative" and never says relative to
 * what. Read as relative to θ, the window grows without bound while the lattice
 * spacing stays π, so above |θ| ≈ 1e11 it starts swallowing whole lattice
 * cells: at θ = 1e12 the window is a full radian, and `Ry(1e12)` — 0.657625
 * radians from the nearest multiple of π, checked against a 60-digit π — folds
 * to CQ_ANGLE_IDENTITY. M22 would then emit nothing at all, which is the
 * forbidden direction (angle.h). An absolute window cannot do that: what a fold
 * discards is bounded by the window itself, at every magnitude.
 *
 * WHAT THE RELATIVE READING WAS FOR, AND WHY IT IS NOT NEEDED. The apparent
 * argument for it — that a caller spelling `k*M_PI` accumulates representation
 * error proportional to k, so the window must grow to keep recognising large
 * multiples — is FALSE, and measured false. The residual below is
 * |θ − fl(k·π_double)|, and for θ spelled `k*M_PI` that is EXACTLY ZERO at
 * every k, because both sides are the same rounded product. Large multiples are
 * recognised at any window down to and including nothing.
 *
 * What genuinely does scale with |θ| is the ERROR IN THE ANSWER, and that is
 * what the one refusal below bounds.
 */

#include "angle.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* CQ_ANGLE_PI moved to angle.h at Step 19 — M22 needs the same double for §7's
 * `Z`, which is `sink.rz(q, π)` (bd lk0), and a second spelling of it would be
 * a second home for one constant. */

/* HOW FAR `k·CQ_ANGLE_PI` CAN BE FROM `k·π`, RELATIVE TO |θ|. Two terms, both
 * proportional to the magnitude:
 *
 *   1.110223e-16  one half-ulp, from rounding the product k·CQ_ANGLE_PI (2^-53)
 *   0.389817e-16  CQ_ANGLE_PI is not π. (π − CQ_ANGLE_PI)/π, and π is irrational
 *                 so no non-zero double is ever exactly a multiple of it
 *   ------------
 *   1.500040e-16  rounded up to 1.6e-16
 *
 * This is the term §7's wording hides, and it is why a zero residual is not the
 * same as a correct answer. Measured against a 60-digit π: θ = 2^52·CQ_ANGLE_PI
 * has residual exactly 0 and is 0.551532 radians from any true multiple of 4π. */
#define CQ_ANGLE_PI_ERROR 1.6e-16

static double angle_tol = CQ_ANGLE_TOLERANCE_DEFAULT;

/* A hard error in BOTH configurations. A tolerance is configuration, and a
 * quietly-substituted default would hand the caller a circuit they did not ask
 * for — cq_sink_active's posture, applied to §7. */
static void cq_angle_die(const char *what, double v)
{
    fprintf(stderr, "libcqops: FATAL: angle: %s (%.17g)\n", what, v);
    abort();
}

/* Total without an isfinite() clause, and deliberately so: NaN fails both
 * comparisons, +inf fails the upper one and −inf the lower. */
static int tolerance_is_usable(double tol)
{
    return tol >= 0.0 && tol <= CQ_ANGLE_TOLERANCE_MAX;
}

cq_angle_class cq_angle_lattice(double theta, double tol)
{
    if (!tolerance_is_usable(tol))
        cq_angle_die("lattice: tolerance is outside [0, CQ_ANGLE_TOLERANCE_MAX]",
                     tol);

    const double window = tol * CQ_ANGLE_PI;

    /* THE REFUSAL, and the whole of D10's error bound. Everything the module
     * cannot see is proportional to |θ|, so refusing above the magnitude where
     * that unseeable part exceeds the window is what makes the answer mean
     * something. It gives the contract in angle.h: the total distance from θ to
     * the true multiple its row names is (residual ≤ window) + (drift ≤ window),
     * hence at most 2·tol·π.
     *
     * At the default tolerance this admits |θ| ≤ 19635 rad — 3125 full turns,
     * far past anything a caller writes — and refuses everything above. At
     * tol == 0 it admits |θ| ≤ 0, which is the mathematically exact answer: π is
     * irrational, so zero is the only double that is exactly a multiple of it.
     *
     * IT IS ALSO WHAT MAKES THE REST TOTAL, and that is load-bearing rather
     * than incidental. Written as `!(… <= …)`, a non-finite θ falls through it
     * to CQ_ANGLE_GENERAL — so there is no separate isfinite() clause to go
     * stale, and no path on which `round(NaN/π)` reaches the cast below. */
    if (!(fabs(theta) * CQ_ANGLE_PI_ERROR <= window)) return CQ_ANGLE_GENERAL;

    const double k = round(theta / CQ_ANGLE_PI);
    if (!(fabs(theta - k * CQ_ANGLE_PI) <= window)) return CQ_ANGLE_GENERAL;

    /* The cast is defined because the refusal above bounds |θ|, hence |k|, by
     * CQ_ANGLE_TOLERANCE_MAX / CQ_ANGLE_PI_ERROR = 6.25e12 — inside the range
     * where a double is an exact integer, never mind the range of long long.
     * tests/test_angle.c pins that arithmetic rather than trusting this note. */
    const long long ki = (long long)k;
    const long long m  = ((ki % 4) + 4) % 4;

    if (m == 0) return CQ_ANGLE_IDENTITY;       /* θ ≡ 0  (mod 4π) */
    if (m == 2) return CQ_ANGLE_NEG_IDENTITY;   /* θ ≡ 2π (mod 4π) */
    return CQ_ANGLE_HALF_TURN;                  /* θ ≡ π  (mod 2π) */
}

cq_angle_class cq_angle_ry_row(double theta)
{
    return cq_angle_lattice(theta, angle_tol);
}

cq_angle_class cq_angle_rz_row(double phi)
{
    /* §7's Rz column has ONE special row. See angle.h for why the other two
     * are not folded here, and why emitting them is the sound direction. */
    return cq_angle_lattice(phi, angle_tol) == CQ_ANGLE_IDENTITY
         ? CQ_ANGLE_IDENTITY : CQ_ANGLE_GENERAL;
}

double cq_angle_tolerance(void)
{
    return angle_tol;
}

void cq_angle_set_tolerance(double rel)
{
    /* A message disjoint from cq_angle_lattice's, so a death test can pin which
     * of the two guards spoke — the Step 15 lesson, where an identical message
     * one layer up let a mutated guard survive as always-true. */
    if (!tolerance_is_usable(rel))
        cq_angle_die("cq_angle_set_tolerance was given a tolerance outside "
                     "[0, CQ_ANGLE_TOLERANCE_MAX]", rel);
    angle_tol = rel;
}
