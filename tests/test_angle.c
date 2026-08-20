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
 * THE SPLIT SEAM WAS TAKEN AT STEP 20 (Rule 12, `bd w8j`). This file reached
 * 294 of 300 at Step 18 and recorded the seam then: the `--- D10's error bound
 * ---` divider, with everything from `no_fold_ever_discards_more_than_the_
 * window_says` through `a_zero_tolerance_admits_only_zero` — the TOLERANCE half
 * — moving out. It is now `test_angle_bounds.inc`, included below at the same
 * point in the file, with the CQ_CASE list in CQ_TEST_MAIN unchanged. What
 * stays here is the TABLE half: which of §7's rows an angle sits on.
 *
 * IT WAS TAKEN AS ITS OWN STEP, ahead of `bd fna`'s enum change, precisely so
 * that the enum change did not land under a red `make lint` and get improvised.
 * `bd zmo` exists because tests/support/refmodel.c reached 284 with no seam
 * recorded; this is the same situation caught one step earlier and then acted
 * on before it bit.
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

    /* θ ≡ π (mod 2π) is ONE row of §7's table and TWO classes since bd fna:
     * k ≡ 1 is HALF_TURN, k ≡ 3 is NEG_HALF_TURN, and they differ by the global
     * −1 that D11 makes observable under a control. M22 still emits the
     * identical pair for both — tests/test_rotate_table.inc is where that is
     * pinned, and it is deliberately NOT restated here. */
    CHECK_RY(REF_PI,          CQ_ANGLE_HALF_TURN);      /* k ≡ 1 (mod 4)   */
    CHECK_RY(-3.0 * REF_PI,   CQ_ANGLE_HALF_TURN);
    CHECK_RY(5.0 * REF_PI,    CQ_ANGLE_HALF_TURN);

    CHECK_RY(-REF_PI,         CQ_ANGLE_NEG_HALF_TURN);  /* k ≡ 3 (mod 4)   */
    CHECK_RY(3.0 * REF_PI,    CQ_ANGLE_NEG_HALF_TURN);
    CHECK_RY(-5.0 * REF_PI,   CQ_ANGLE_NEG_HALF_TURN);

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
    CHECK_RY(3.0 * REF_PI,  CQ_ANGLE_NEG_HALF_TURN);
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

#include "test_angle_bounds.inc"

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
    CHECK_EQ((int)CQ_ANGLE_NEG_HALF_TURN, 4);   /* bd fna: M06 reads it too */
}

CQ_TEST(negating_theta_mirrors_the_row_and_swaps_the_half_turn_parity)
{
    /* THIS CASE USED TO ASSERT PLAIN EQUALITY AND IS NOW A SWAP, because that
     * is exactly what bd fna's split introduced and exactly what the `_inv`
     * axis needs (PRD §15 D11, obligation (iii)): k ↦ −k fixes k ≡ 0 and k ≡ 2
     * and exchanges k ≡ 1 ↔ 3. Weakening it to "equal, or both are half turns"
     * would tolerate the distinction as a don't-care — and
     * `cqrt_ry_<W>_controlled_inv` is in the frozen ABI at 7 widths with no
     * plain forward twin, so a merged row would make an `_inv` disagree with
     * the forward it inverts. `mirror_row` is the oracle, in the .inc. */
    for (long k = -512; k <= 512; k++) {
        double t = (double)k * REF_PI / 8.0;
        cq_angle_class a = cq_angle_ry_row(t);
        cq_angle_class b = cq_angle_ry_row(-t);
        if (b != mirror_row(a))
            cq_h_fail(__FILE__, __LINE__,
                      "ry_row(%.17g) = %s so ry_row(%.17g) should be %s, got %s",
                      t, cname(a), -t, cname(mirror_row(a)), cname(b));
    }

    /* The mirror is only interesting if the sweep above REACHES both parities;
     * an all-IDENTITY sweep would satisfy it vacuously. */
    CHECK(cq_angle_ry_row(REF_PI)  == CQ_ANGLE_HALF_TURN);
    CHECK(cq_angle_ry_row(-REF_PI) == CQ_ANGLE_NEG_HALF_TURN);
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
    CQ_CASE(negating_theta_mirrors_the_row_and_swaps_the_half_turn_parity),
    CQ_CASE(the_step_index_stays_inside_the_exact_integer_range),
    CQ_CASE(angles_spelled_the_ordinary_ways_land_on_the_lattice),
    CQ_CASE(the_module_tolerance_defaults_to_1e_12_and_round_trips)
)
