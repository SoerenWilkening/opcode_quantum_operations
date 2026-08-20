/* Step 18 — M21. PRD §7's table, row by row, plus the two boundaries bd dl7
 * names explicitly: mod-4π against mod-2π, and the tolerance edges.
 *
 * TWO ORACLES, AND NEITHER IS THE MODULE — `ref_row` for the parity and
 * `distance_to_true_multiple_of_pi` for the error bound. Both live in
 * test_angle_oracles.inc, which carries the argument for why the second one is
 * not redundant: the first draft of this module used a window proportional to
 * |θ|, every case in an earlier draft of this file was green, and `Ry(1e12)`
 * folded to the identity 0.657625 radians away from any multiple of π.
 *
 * THE ASYMMETRY IN WHAT IS ASSERTED IS DELIBERATE. CQ_ANGLE_GENERAL is sound at
 * every θ — it emits the rotation. The other three DELETE or REPLACE it. So
 * `no_fold_ever_discards_more_than_the_window_says` bounds the error of every
 * fold and would pass vacuously against a module that never folded; the cases
 * that close that gap are the ones REQUIRING a fold —
 * `the_four_ry_rows_of_prd_7s_table`,
 * `the_two_boundaries_alternate_across_the_whole_lattice` and
 * `angles_spelled_the_ordinary_ways_land_on_the_lattice`.
 *
 * SPLIT SEAM, RECORDED BEFORE IT IS NEEDED (Rule 12). This file is at 294 of
 * 300 and the next case breaks the guard. The seam is the `--- D10's error
 * bound ---` divider: everything from `no_fold_ever_discards_more_than_the_
 * window_says` through `a_zero_tolerance_admits_only_zero` is about the
 * TOLERANCE and moves to `test_angle_bounds.inc`; everything above it is about
 * the TABLE and stays. That leaves ~150 lines either side. `bd zmo` exists
 * because tests/support/refmodel.c reached 284 with no seam recorded; this is
 * the same situation caught one step earlier.
 */

#include "angle.h"
#include "support/harness.h"

#include <float.h>
#include <math.h>
#include <string.h>

#include "test_angle_oracles.inc"

/* --- The table itself ----------------------------------------------------- */

CQ_TEST(the_four_ry_rows_of_prd_7s_table)
{
    CHECK_RY(0.0,             CQ_ANGLE_IDENTITY);       /* θ ≡ 0  (mod 4π) */
    CHECK_RY(4.0 * REF_PI,    CQ_ANGLE_IDENTITY);
    CHECK_RY(-4.0 * REF_PI,   CQ_ANGLE_IDENTITY);
    CHECK_RY(8.0 * REF_PI,    CQ_ANGLE_IDENTITY);

    CHECK_RY(2.0 * REF_PI,    CQ_ANGLE_NEG_IDENTITY);   /* θ ≡ 2π (mod 4π) */
    CHECK_RY(-2.0 * REF_PI,   CQ_ANGLE_NEG_IDENTITY);
    CHECK_RY(6.0 * REF_PI,    CQ_ANGLE_NEG_IDENTITY);

    CHECK_RY(REF_PI,          CQ_ANGLE_HALF_TURN);      /* θ ≡ π  (mod 2π) */
    CHECK_RY(-REF_PI,         CQ_ANGLE_HALF_TURN);
    CHECK_RY(3.0 * REF_PI,    CQ_ANGLE_HALF_TURN);
    CHECK_RY(-3.0 * REF_PI,   CQ_ANGLE_HALF_TURN);
    CHECK_RY(5.0 * REF_PI,    CQ_ANGLE_HALF_TURN);

    CHECK_RY(REF_PI / 2.0,    CQ_ANGLE_GENERAL);        /* otherwise       */
    CHECK_RY(-REF_PI / 2.0,   CQ_ANGLE_GENERAL);
    CHECK_RY(REF_PI / 3.0,    CQ_ANGLE_GENERAL);
    CHECK_RY(1.0,             CQ_ANGLE_GENERAL);
    CHECK_RY(-0.7,            CQ_ANGLE_GENERAL);
    CHECK_RY(2.5 * REF_PI,    CQ_ANGLE_GENERAL);
}

CQ_TEST(the_rz_column_recognises_only_the_mod_4pi_identity)
{
    CHECK_RZ(0.0,           CQ_ANGLE_IDENTITY);     /* φ ≡ 0 (mod 4π)  */
    CHECK_RZ(-0.0,          CQ_ANGLE_IDENTITY);
    CHECK_RZ(4.0 * REF_PI,  CQ_ANGLE_IDENTITY);
    CHECK_RZ(-4.0 * REF_PI, CQ_ANGLE_IDENTITY);
    CHECK_RZ(8.0 * REF_PI,  CQ_ANGLE_IDENTITY);

    /* And everything else is "otherwise" — INCLUDING the two rows the Ry column
     * does recognise. That is PRD §7 as written, not an oversight: §7's Rz
     * constant column is "nothing" at every φ, so nothing is lost on a constant
     * bit, and on a QUBIT emitting the rotation is the sound direction —
     * folding a global −1 away is wrong under Rule 9's controlled axis, where
     * it becomes a relative phase. The paired ry_row assertion is what makes
     * the collapse visible rather than accidental. */
    CHECK_RY(2.0 * REF_PI,  CQ_ANGLE_NEG_IDENTITY);
    CHECK_RZ(2.0 * REF_PI,  CQ_ANGLE_GENERAL);
    CHECK_RY(REF_PI,        CQ_ANGLE_HALF_TURN);
    CHECK_RZ(REF_PI,        CQ_ANGLE_GENERAL);
    CHECK_RY(6.0 * REF_PI,  CQ_ANGLE_NEG_IDENTITY);
    CHECK_RZ(6.0 * REF_PI,  CQ_ANGLE_GENERAL);
    CHECK_RY(3.0 * REF_PI,  CQ_ANGLE_HALF_TURN);
    CHECK_RZ(3.0 * REF_PI,  CQ_ANGLE_GENERAL);

    CHECK_RZ(REF_PI / 2.0,  CQ_ANGLE_GENERAL);
    CHECK_RZ(0.25,          CQ_ANGLE_GENERAL);
}

/* --- The mod-4π against mod-2π boundary, across the whole lattice ---------- */

CQ_TEST(the_two_boundaries_alternate_across_the_whole_lattice)
{
    /* Every row of §7's Ry column at 2049 consecutive lattice points, and this
     * is the ONLY guard anywhere in the project on the IDENTITY-versus-
     * NEG_IDENTITY distinction: §7 gives rows 1 and 2 the identical action in
     * BOTH columns, so no M22 test, gate count or trace can tell them apart
     * until Rule 9's controlled axis exists. Never weaken it to a mod-2 check.
     *
     * The negative half is load-bearing on its own. C's `%` is accidentally
     * right for k ≡ 0 (mod 4) and for negative odd k, so the `+4`
     * normalisation in angle.c is witnessed ONLY by negative k ≡ 2 (mod 4) —
     * k = −2, −6, −10 … Do not "simplify" this sweep to non-negative k. */
    for (long k = -1024; k <= 1024; k++)
        CHECK_RY((double)k * REF_PI, row_of_index(k));

    /* And the Rz collapse over the same lattice: only k ≡ 0 (mod 4) survives. */
    for (long k = -64; k <= 64; k++)
        CHECK_RZ((double)k * REF_PI,
                 row_of_index(k) == CQ_ANGLE_IDENTITY ? CQ_ANGLE_IDENTITY
                                                      : CQ_ANGLE_GENERAL);
}

CQ_TEST(the_midpoints_between_lattice_points_are_never_special)
{
    /* θ = (k + ½)π is the furthest an angle can be from the lattice, and it is
     * also where `round` breaks its tie. Whichever way it breaks, the residual
     * is π/2 — far outside any admissible window — so the answer is GENERAL. */
    for (long k = -256; k <= 256; k++) {
        CHECK_RY(((double)k + 0.5) * REF_PI,  CQ_ANGLE_GENERAL);
        CHECK_RY(((double)k + 0.25) * REF_PI, CQ_ANGLE_GENERAL);
        CHECK_RY(((double)k + 0.75) * REF_PI, CQ_ANGLE_GENERAL);
    }
}

CQ_TEST(the_classification_agrees_with_an_independent_reduction)
{
    for (long k = -128; k <= 128; k++) {
        double base = (double)k * REF_PI;
        check_against_reference(base, "lattice point");
        check_against_reference(base + REF_PI / 16.0, "lattice + π/16");
        check_against_reference(base - REF_PI / 16.0, "lattice − π/16");
        check_against_reference(base + REF_PI / 2.0,  "midpoint");
    }

    /* 200k points across [−64π, 64π] at a spacing of ~0.002, eleven orders of
     * magnitude coarser than the window, so every one must be GENERAL — and the
     * reference has to say so too. This is what a far-too-wide window fails. */
    const double lo = -64.0 * REF_PI, hi = 64.0 * REF_PI;
    for (long i = 0; i <= 200000; i++)
        check_against_reference(lo + (hi - lo) * ((double)i / 200000.0),
                                "dense scan");

    /* Pseudo-random across five magnitudes, so the agreement is not an artefact
     * of an evenly-spaced grid. A fixed LCG: the suite must be reproducible. */
    unsigned long long s = 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 100000; i++) {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        double u = (double)(s >> 11) / 9007199254740992.0;   /* [0,1) */
        double scale = pow(10.0, (double)(i % 5) - 2.0);     /* 1e-2 … 1e2 */
        check_against_reference((u - 0.5) * 2.0 * scale * REF_PI, "random");
    }
}

/* --- D10's error bound, which is the contract ----------------------------- */

CQ_TEST(no_fold_ever_discards_more_than_the_window_says)
{
    /* TWENTY-TWO DECADES OF MAGNITUDE, because the defect this case exists to
     * catch lived in a band the rest of the suite did not reach. An earlier
     * draft tested |θ| up to ~2.5e4 and again from 6.3e13 up, and the window
     * was unsound throughout 1e11 … 1.6e12 — entirely inside the gap. */
    for (int e = -6; e <= 15; e++) {
        for (int m = 1; m <= 10; m++) {
            double mag = (double)m * pow(10.0, (double)e);
            check_sound(mag);
            check_sound(-mag);
            check_sound(round(mag / REF_PI) * REF_PI);   /* snapped to lattice */
        }
    }

    /* The lattice itself, where folds actually happen, so the case is not
     * bounding the error of an empty set. */
    for (long k = -2048; k <= 2048; k++) {
        check_sound((double)k * REF_PI);
        check_sound((double)k * REF_PI * (1.0 + 5e-13));
    }

    unsigned long long s = 0xD1B54A32D192ED03ull;
    for (int i = 0; i < 100000; i++) {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        double u = (double)(s >> 11) / 9007199254740992.0;
        check_sound((u - 0.5) * 2.0 * pow(10.0, (double)(i % 12) - 3.0));
    }

    /* THE DRIFT HALF OF THE BOUND, WHICH EVERYTHING ABOVE IS BLIND TO. Every
     * call so far went through cq_angle_ry_row, i.e. at the module tolerance —
     * and there the refusal caps |θ| at 1.96e4, where the drift term is a
     * thousandth of the bound and only the residual term can be falsified. The
     * drift dominates near the refusal boundary itself, |θ| ≈ tol·π/1.6e-16,
     * which is only large enough to matter at a loose tolerance. So sweep the
     * tolerance to the cap and walk the reach at each one.
     *
     * MEASURED: `CQ_ANGLE_PI_ERROR = 5e-17` really does break D10 — by 1.22× at
     * `lattice(0x1.550f7dca5209cp+39, 0x1.8723a1d588a37p-17)`, and 1.3% of all
     * folds violate it — and before this loop existed the entire suite stayed
     * green except one magnitude pin, which reads identically for a SAFE
     * tightening of the constant and for that unsafe loosening.
     *
     * THE LADDER MUST NOT BE DERIVED FROM THE MODULE'S OWN CONSTANT, and the
     * first version of this loop was — it walked up to `tol·π/1.6e-16` and so
     * STILL missed the mutant it was written for, because a loosened constant
     * makes the module accept a LARGER |θ| than the test ever offered it. That
     * is this file's own headline lesson (an oracle that shares a constant is
     * blind to it) arriving one level down, in the probe RANGE rather than in
     * the comparison. So the ladder starts far past any reach the module could
     * plausibly have and descends 60 octaves. Over-probing is free: anything
     * refused comes back GENERAL and check_sound_at skips it.
     *
     * THE DIVISION OF LABOUR WITH `the_reach_of_the_default_tolerance_is_pinned`
     * IS DELIBERATE, AND SO IS SAYING WHERE THIS CASE STOPS. Measured, one
     * mutant at a time:
     *
     *   PI_ERROR       sound?   caught by
     *   5e-17   (3.2x loose)  no    THIS case AND the reach pin
     *   1e-16   (1.6x loose)  no    the reach pin only
     *   1.45e-16 (1.1x loose) no    the reach pin only
     *   1.51e-16 (tighter)    yes   the reach pin only
     *
     * So the REACH PIN is the guard with complete coverage of the constant —
     * any change to it moves the reach and that case goes red. What this case
     * adds is a failure that names the CONTRACT rather than a magnitude, and it
     * has that only for a gross loosening: a marginal one violates D10 too
     * rarely for a bounded sweep to land on it. Neither case subsumes the
     * other, and it is worth knowing that the cheap one is the complete one. */
    const double tols[] = { 1e-12, 1e-10, 1e-8, 1e-6, 1e-4,
                            CQ_ANGLE_TOLERANCE_MAX };
    for (size_t i = 0; i < sizeof tols / sizeof tols[0]; i++) {
        double far = tols[i] * REF_PI / 1e-18;   /* 160x the shipped reach */
        double w   = tols[i] * REF_PI;
        for (int j = 0; j <= 400; j++) {
            double mag = far * pow(2.0, -(double)j / 8.0);
            double k0  = round(mag / REF_PI);
            /* SEVERAL NEIGHBOURING INDICES PER RUNG, not one. At a lattice
             * point the whole error is the drift plus however the product
             * `k·π_double` happened to round, and that rounding varies
             * pseudo-randomly with k — so one probe per magnitude samples the
             * error distribution once and mostly misses its tail. Measured
             * against the 5e-17 mutant: one probe per rung caught nothing, 24
             * caught it. */
            for (int m = 0; m < 24; m++) {
                double base = (k0 + (double)m) * REF_PI;
                check_sound_at(base,           tols[i]);
                check_sound_at(-base,          tols[i]);
                check_sound_at(base + 0.5 * w, tols[i]);
            }
        }
    }
}

CQ_TEST(a_large_theta_is_refused_rather_than_folded)
{
    /* THE MEASURED MISCOMPILE, kept as a regression case with its numbers.
     * Every θ here was classified IDENTITY or NEG_IDENTITY by the θ-relative
     * window, and the figure after each is the TRUE distance from θ to the
     * multiple of π that row named — computed against a 60-digit π, not in
     * doubles. It is not a cliff: the error grows linearly with |θ| from the
     * first magnitude at which the window can reach a lattice point at all,
     * which is why the series below spans six decades rather than naming one
     * bad angle. Each of the first three is a real multiple plus 0.4 of the old
     * window, i.e. an angle that construction folded away. */
    CHECK_RY(4999995.504637527,  CQ_ANGLE_GENERAL);  /* was IDENTITY,  2.0e-6 rad */
    CHECK_RY(999999993.1398191,  CQ_ANGLE_GENERAL);  /* was IDENTITY,  4.0e-4     */
    CHECK_RY(99999999992.56593,  CQ_ANGLE_GENERAL);  /* was IDENTITY,  4.0e-2     */
    CHECK_RY(1e12,               CQ_ANGLE_GENERAL);  /* was IDENTITY,  0.657625   */
    CHECK_RY(-1e12,              CQ_ANGLE_GENERAL);  /* was IDENTITY,  0.657625   */
    CHECK_RY(1.5e12,             CQ_ANGLE_GENERAL);  /* was IDENTITY,  0.986437   */
    CHECK_RY(1.570673279e12,     CQ_ANGLE_GENERAL);  /* was NEG_IDENT, 1.292108   */

    /* And the sibling defect, on the other side of the same term: an EXACT
     * multiple of the module's own π is still refused once the drift between
     * that double and true π exceeds the window. 2^52·π_double has a residual
     * of exactly zero and is 0.551532 rad from any true multiple of 4π. */
    CHECK_RY(4503599627370496.0 * REF_PI, CQ_ANGLE_GENERAL);
    CHECK_RY(1e12 * REF_PI,               CQ_ANGLE_GENERAL);
    CHECK_RY(DBL_MAX,                     CQ_ANGLE_GENERAL);
    CHECK_RY(-DBL_MAX,                    CQ_ANGLE_GENERAL);
}

CQ_TEST(the_reach_of_the_default_tolerance_is_pinned)
{
    /* The refusal is |θ|·1.6e-16 ≤ tol·π, so at the default it admits
     * |k| ≤ 6250 and refuses above. Pinning both sides is what makes a change
     * to CQ_ANGLE_PI_ERROR or to the default a change that has to be argued. */
    CHECK_RY(6000.0 * REF_PI, CQ_ANGLE_IDENTITY);   /* 6000 ≡ 0 (mod 4)  */
    CHECK_RY(6001.0 * REF_PI, CQ_ANGLE_HALF_TURN);
    CHECK_RY(6500.0 * REF_PI, CQ_ANGLE_GENERAL);    /* past the reach    */
    CHECK_RY(-6000.0 * REF_PI, CQ_ANGLE_IDENTITY);
    CHECK_RY(-6500.0 * REF_PI, CQ_ANGLE_GENERAL);

    /* A looser tolerance buys proportionally more reach, which is the point of
     * tying the refusal to the window rather than to a fixed magnitude. */
    CHECK_LATTICE(6500.0 * REF_PI, 1e-11, CQ_ANGLE_IDENTITY);
    CHECK_LATTICE(1e6   * REF_PI,  1e-11, CQ_ANGLE_GENERAL);
    CHECK_LATTICE(1e6   * REF_PI,  1e-9,  CQ_ANGLE_IDENTITY);
}

CQ_TEST(the_largest_rotation_a_fold_discards_is_the_window)
{
    /* The window is ABSOLUTE — tol·π ≈ 3.1416e-12 rad at the default — so the
     * threshold sits at the same place whatever |θ| is. THAT is what a relative
     * window fails: at k = 6001 it would be 6001× wider. */
    const double w = cq_angle_tolerance() * REF_PI;

    CHECK_RY(w * 0.9,  CQ_ANGLE_IDENTITY);     /* inside: folded away  */
    CHECK_RY(-w * 0.9, CQ_ANGLE_IDENTITY);
    CHECK_RY(w * 1.5,  CQ_ANGLE_GENERAL);      /* outside: emitted     */
    CHECK_RY(-w * 1.5, CQ_ANGLE_GENERAL);
    CHECK_RY(1e-13,    CQ_ANGLE_IDENTITY);
    CHECK_RY(1e-300,   CQ_ANGLE_IDENTITY);
    CHECK_RY(1e-11,    CQ_ANGLE_GENERAL);

    /* The same absolute width, at four magnitudes. A window scaled by |θ| makes
     * every "outside" row below pass as HALF_TURN.
     *
     * THE LIST STOPS AT k = 1001 FOR A REAL REASON, found by this case going
     * red at k = 6001. An absolute window eventually becomes finer than the
     * double grid: ulp(θ) reaches tol·π at |θ| = tol·π·2^52 ≈ 1.4e4, and above
     * that the only representable angle inside the window is the lattice point
     * itself, so `base ± 0.9·w` rounds straight back out of it. Probing at
     * k = 6001 was therefore asserting something about the grid, not about the
     * module. The band from there to the refusal at |k| = 6250 is exact-match
     * territory, and `the_reach_of_the_default_tolerance_is_pinned` covers it. */
    const long odd[] = { 1, 3, 101, 1001 };
    for (size_t i = 0; i < sizeof odd / sizeof odd[0]; i++) {
        double base = (double)odd[i] * REF_PI;
        CHECK_RY(base,           CQ_ANGLE_HALF_TURN);
        CHECK_RY(base + w * 0.9, CQ_ANGLE_HALF_TURN);
        CHECK_RY(base - w * 0.9, CQ_ANGLE_HALF_TURN);
        CHECK_RY(base + w * 1.5, CQ_ANGLE_GENERAL);
        CHECK_RY(base - w * 1.5, CQ_ANGLE_GENERAL);
    }
}

CQ_TEST(the_corpus_angles_are_all_general_and_3_14_is_the_close_one)
{
    /* CQ_lang's 239 goldens carry 410 rotation calls and 26 distinct angles,
     * and NOT ONE lands on a special row (measured 2026-08-17; PRD §15 D10).
     * They are all small decimals, so what is worth pinning is not the list but
     * the closest approach.
     *
     * `3.14` appears in two fixtures and is 1.5927e-3 rad short of π — 5.1e8
     * times the default window, which is the headroom this classification runs
     * with in practice. It is also INSIDE the window at the loosest legal
     * tolerance, where it becomes an X. That is the cap doing its job at the
     * order where "tolerance" stops meaning "the same angle", and it is why
     * CQ_ANGLE_TOLERANCE_MAX must not be raised. */
    CHECK_RY(3.14,  CQ_ANGLE_GENERAL);
    CHECK_RY(0.5,   CQ_ANGLE_GENERAL);     /* 256 of the 410 calls */
    CHECK_RY(0.25,  CQ_ANGLE_GENERAL);
    CHECK_RY(-0.5,  CQ_ANGLE_GENERAL);
    CHECK_RZ(0.75,  CQ_ANGLE_GENERAL);

    CHECK_LATTICE(3.14, CQ_ANGLE_TOLERANCE_MAX,       CQ_ANGLE_HALF_TURN);
    CHECK_LATTICE(3.14, CQ_ANGLE_TOLERANCE_MAX / 10.0, CQ_ANGLE_GENERAL);
}

CQ_TEST(both_boundaries_are_inclusive_at_a_non_zero_threshold)
{
    /* BOTH `<=` IN cq_angle_lattice ARE INCLUSIVE, and until this case existed
     * the only thing that said so was θ = 0 at tol = 0 — where residual and
     * window are both ZERO, so `<` and `<=` differ only in a degenerate tie and
     * the suite proved nothing about either operator at a real threshold.
     *
     * These two pairs are constructed so the comparison is an EXACT tie with
     * both sides non-zero, which is why the tolerances are spelled in hex and
     * must not be "tidied" into decimals — a decimal that merely rounds near
     * them turns the tie into an ordinary inequality and the case goes quiet.
     *
     *   residual tie: window == residual == 0x1p-50, θ one ulp off π
     *   refusal  tie: window == |θ|·1.6e-16 == 0x1.21c2c25e4127dp-51, residual 0
     *
     * Verified by execution: with `<=` both are HALF_TURN; change EITHER to `<`
     * and exactly its own line becomes GENERAL. */
    CHECK_LATTICE(0x1.921fb54442d1ap+1, 0x1.45f306dc9c883p-52, CQ_ANGLE_HALF_TURN);
    CHECK_LATTICE(0x1.921fb54442d18p+1, 0x1.70ef54646d497p-53, CQ_ANGLE_HALF_TURN);
}

CQ_TEST(a_zero_tolerance_admits_only_zero)
{
    /* Not "only the exact double lattice" — only ZERO. π is irrational, so no
     * non-zero double is exactly a multiple of it, and with no window at all
     * the refusal admits |θ| ≤ 0. That is the mathematically exact answer and
     * it is worth having as a legal setting for exactly that reason. */
    CHECK_LATTICE(0.0,  0.0, CQ_ANGLE_IDENTITY);
    CHECK_LATTICE(-0.0, 0.0, CQ_ANGLE_IDENTITY);
    CHECK_LATTICE(REF_PI,       0.0, CQ_ANGLE_GENERAL);
    CHECK_LATTICE(2.0 * REF_PI, 0.0, CQ_ANGLE_GENERAL);
    CHECK_LATTICE(4.0 * REF_PI, 0.0, CQ_ANGLE_GENERAL);
    CHECK_LATTICE(DBL_TRUE_MIN, 0.0, CQ_ANGLE_GENERAL);

    /* Index 0 admits exactly one angle at every tolerance, which is the other
     * half of "the window is absolute": it does not shrink to nothing near
     * zero, and it does not open up either. */
    const double tols[] = { 0.0, 1e-15, CQ_ANGLE_TOLERANCE_DEFAULT, 1e-6 };
    for (size_t i = 0; i < sizeof tols / sizeof tols[0]; i++) {
        CHECK_LATTICE(0.0,  tols[i], CQ_ANGLE_IDENTITY);
        CHECK_LATTICE(-0.0, tols[i], CQ_ANGLE_IDENTITY);
    }
}

/* --- Non-finite input, the sign symmetry, and the cast -------------------- */

CQ_TEST(nan_and_infinity_classify_as_general)
{
    /* Not a hard error: an angle is data, and the sound answer for one we
     * cannot classify is to emit the rotation.
     *
     * WHICH LINE CATCHES NaN WAS MEASURED, NOT ARGUED, and the first version of
     * this comment was wrong about it. angle.c writes both of its tests as
     * `!(a <= b)`, and only the SECOND one is doing the work here: mutating the
     * magnitude refusal alone to `a > b` leaves every case green, because a NaN
     * θ then falls one line down to `!(fabs(NaN − k·π) <= window)`, which is
     * true, and returns GENERAL anyway. Mutating BOTH to `>` kills the suite —
     * that paired mutation is what proves the single one is equivalent rather
     * than untested, and it is why neither may be "tidied" into `>`. Nothing
     * ever reaches `(long long)round(NaN/π)`, which would be undefined. */
    CHECK_RY(NAN,       CQ_ANGLE_GENERAL);
    CHECK_RY(INFINITY,  CQ_ANGLE_GENERAL);
    CHECK_RY(-INFINITY, CQ_ANGLE_GENERAL);
    CHECK_RZ(NAN,       CQ_ANGLE_GENERAL);
    CHECK_RZ(INFINITY,  CQ_ANGLE_GENERAL);
    CHECK_RZ(-INFINITY, CQ_ANGLE_GENERAL);

    CHECK_LATTICE(NAN,       0.0,  CQ_ANGLE_GENERAL);
    CHECK_LATTICE(INFINITY,  0.0,  CQ_ANGLE_GENERAL);
    CHECK_LATTICE(NAN,       1e-3, CQ_ANGLE_GENERAL);
    CHECK_LATTICE(-INFINITY, 1e-3, CQ_ANGLE_GENERAL);
}

CQ_TEST(negative_zero_is_the_identity)
{
    /* −0.0 == 0.0 in C but is a different double, and %a shows it. */
    CHECK_RY(-0.0, CQ_ANGLE_IDENTITY);
    CHECK_RZ(-0.0, CQ_ANGLE_IDENTITY);
}

CQ_TEST(a_zero_initialised_class_is_the_safe_row)
{
    /* angle.h's numbering is load-bearing: a caller that declares a
     * cq_angle_class and forgets to assign it must emit the rotation, not
     * delete it. The _Static_assert there makes a renumbering break the build;
     * this makes it break a test as well, which is what test_bit.c does for
     * CQ_BIT_ZERO == 0 and for the same reason. Filed by a mutation battery:
     * renumbering GENERAL to 7 was the one mutant of thirty-eight that nothing
     * could see. */
    cq_angle_class zeroed;
    memset(&zeroed, 0, sizeof zeroed);
    CHECK(zeroed == CQ_ANGLE_GENERAL);
    CHECK_EQ((int)CQ_ANGLE_GENERAL, 0);
}

CQ_TEST(the_classification_is_symmetric_in_the_sign_of_theta)
{
    for (long k = -512; k <= 512; k++) {
        double t = (double)k * REF_PI / 8.0;
        cq_angle_class a = cq_angle_ry_row(t);
        cq_angle_class b = cq_angle_ry_row(-t);
        if (a != b)
            cq_h_fail(__FILE__, __LINE__,
                      "ry_row(%.17g) = %s but ry_row(%.17g) = %s",
                      t, cname(a), -t, cname(b));
    }
}

CQ_TEST(the_step_index_stays_inside_the_exact_integer_range)
{
    /* angle.c casts round(θ/π) to long long, and what makes that defined is the
     * magnitude refusal, not a bounds check. The largest |k| that can reach the
     * cast is CQ_ANGLE_TOLERANCE_MAX / CQ_ANGLE_PI_ERROR = 6.25e12, which is
     * inside 2^53 — so k is an exact integer as well as in range. Pinned here
     * rather than left as a comment, because a change to either constant would
     * otherwise reach undefined behaviour silently in Release. */
    const double k_max = CQ_ANGLE_TOLERANCE_MAX / 1.6e-16;
    CHECK(k_max < 9007199254740992.0);          /* 2^53 */

    /* At the loosest legal tolerance, the largest classifiable angle, and one
     * decade past it. Neither may trap, and the second must be refused. */
    CHECK_LATTICE(6.0e12 * REF_PI, CQ_ANGLE_TOLERANCE_MAX, CQ_ANGLE_IDENTITY);
    CHECK_LATTICE(6.0e13 * REF_PI, CQ_ANGLE_TOLERANCE_MAX, CQ_ANGLE_GENERAL);
    CHECK_LATTICE(1e300,           CQ_ANGLE_TOLERANCE_MAX, CQ_ANGLE_GENERAL);
    CHECK_LATTICE(-1e300,          CQ_ANGLE_TOLERANCE_MAX, CQ_ANGLE_GENERAL);
    CHECK_LATTICE(DBL_MAX,         CQ_ANGLE_TOLERANCE_MAX, CQ_ANGLE_GENERAL);
}

/* --- The spellings a caller actually uses --------------------------------- */

CQ_TEST(angles_spelled_the_ordinary_ways_land_on_the_lattice)
{
    const double pi = acos(-1.0);

    CHECK_RY(pi,              CQ_ANGLE_HALF_TURN);
    CHECK_RY(2.0 * pi,        CQ_ANGLE_NEG_IDENTITY);
    CHECK_RY(4.0 * pi,        CQ_ANGLE_IDENTITY);
    CHECK_RY(2.0 * asin(1.0), CQ_ANGLE_HALF_TURN);

    /* Accumulated multiples. The residual for `k*π` is EXACTLY ZERO at every k
     * — both sides are the same rounded product — which is why an absolute
     * window recognises these just as well as a relative one did, and why the
     * relative reading of §7 bought nothing for the cost it carried. */
    for (long k = 1; k <= 3000; k++) {
        CHECK_RY((double)k * (2.0 * pi),
                 (k % 2 == 0) ? CQ_ANGLE_IDENTITY : CQ_ANGLE_NEG_IDENTITY);
        CHECK_RY((double)k * pi, row_of_index(k));
    }
}

/* --- The module tolerance ------------------------------------------------- */

CQ_TEST(the_module_tolerance_defaults_to_1e_12_and_round_trips)
{
    double before = cq_angle_tolerance();
    CHECK(same_double(before, CQ_ANGLE_TOLERANCE_DEFAULT));

    /* BOTH COLUMNS MUST BE CHECKED HERE, and the Rz half is not garnish.
     * cq_angle_rz_row reads the module tolerance through its own call, and
     * every other CHECK_RZ in this suite runs at the default — so with only the
     * CHECK_RY lines below, hard-coding CQ_ANGLE_TOLERANCE_DEFAULT inside
     * cq_angle_rz_row left all 18 cases green in both configurations. That is
     * not a missed optimisation: at tol = 0 the mutant answers IDENTITY where
     * the truth is GENERAL, which is the direction angle.h forbids — M22 would
     * delete the rotation. The Rz column collapses HALF_TURN into GENERAL, so
     * the θ = π discriminator that kills the same mutation in ry_row is blind
     * here; it takes an angle on the IDENTITY row. */
    cq_angle_set_tolerance(0.0);
    CHECK(same_double(cq_angle_tolerance(), 0.0));
    CHECK_RY(REF_PI, CQ_ANGLE_GENERAL);                   /* it took effect */
    CHECK_RZ(4.0 * REF_PI, CQ_ANGLE_GENERAL);             /* on both columns */

    cq_angle_set_tolerance(1e-6);
    CHECK(same_double(cq_angle_tolerance(), 1e-6));
    CHECK_RY(REF_PI * (1.0 + 1e-9), CQ_ANGLE_HALF_TURN);  /* and again */
    CHECK_RZ(4.0 * REF_PI + 1e-9, CQ_ANGLE_IDENTITY);
    CHECK_RY(4.0 * REF_PI + 1e-9, CQ_ANGLE_IDENTITY);

    cq_angle_set_tolerance(CQ_ANGLE_TOLERANCE_MAX);       /* the cap is legal */
    CHECK(same_double(cq_angle_tolerance(), CQ_ANGLE_TOLERANCE_MAX));

    cq_angle_set_tolerance(before);
    CHECK(same_double(cq_angle_tolerance(), CQ_ANGLE_TOLERANCE_DEFAULT));
    CHECK_RY(REF_PI * (1.0 + 1e-9), CQ_ANGLE_GENERAL);
}

CQ_TEST_MAIN(
    CQ_CASE(the_four_ry_rows_of_prd_7s_table),
    CQ_CASE(the_rz_column_recognises_only_the_mod_4pi_identity),
    CQ_CASE(the_two_boundaries_alternate_across_the_whole_lattice),
    CQ_CASE(the_midpoints_between_lattice_points_are_never_special),
    CQ_CASE(the_classification_agrees_with_an_independent_reduction),
    CQ_CASE(no_fold_ever_discards_more_than_the_window_says),
    CQ_CASE(a_large_theta_is_refused_rather_than_folded),
    CQ_CASE(the_reach_of_the_default_tolerance_is_pinned),
    CQ_CASE(the_largest_rotation_a_fold_discards_is_the_window),
    CQ_CASE(the_corpus_angles_are_all_general_and_3_14_is_the_close_one),
    CQ_CASE(both_boundaries_are_inclusive_at_a_non_zero_threshold),
    CQ_CASE(a_zero_tolerance_admits_only_zero),
    CQ_CASE(nan_and_infinity_classify_as_general),
    CQ_CASE(negative_zero_is_the_identity),
    CQ_CASE(a_zero_initialised_class_is_the_safe_row),
    CQ_CASE(the_classification_is_symmetric_in_the_sign_of_theta),
    CQ_CASE(the_step_index_stays_inside_the_exact_integer_range),
    CQ_CASE(angles_spelled_the_ordinary_ways_land_on_the_lattice),
    CQ_CASE(the_module_tolerance_defaults_to_1e_12_and_round_trips)
)
