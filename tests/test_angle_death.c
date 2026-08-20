/* Step 18's other half: M21's two hard errors are real, and they are separate.
 *
 * WHAT THESE GUARDS ARE FOR, and the two halves are not the same. A tolerance
 * BELOW the range — negative, NaN — is only a loudness matter: it would degrade
 * cq_angle_lattice to CQ_ANGLE_GENERAL, which is the sound answer (angle.h), so
 * the circuit stays right and what a caller loses in silence is every fold in
 * PRD §7's table. A tolerance ABOVE CQ_ANGLE_TOLERANCE_MAX is a CORRECTNESS
 * matter: the cap is what bounds |k| for the `(long long)` cast in
 * cq_angle_lattice, and what keeps the window far under the π/2 at which two
 * lattice points could match at once. Both are refused for cq_sink_active's
 * reason — a value that resolves to nothing is a hard error, never a quiet
 * substitution — but only one of them is defensive.
 *
 * BOTH CONFIGURATIONS. CQOPS_DEBUG_INVARIANTS gates checking machinery, never
 * behaviour, and nothing here is an invariant check.
 *
 * NO FAIL_REGULAR_EXPRESSION IS OWED, and that was checked rather than assumed.
 * The two guards are on disjoint call paths — cq_angle_set_tolerance never
 * enters the lattice, and a direct cq_angle_lattice call never enters the
 * setter — so neither can mask the other, and deleting either makes exactly its
 * own four cases report "survived". They share one predicate,
 * tolerance_is_usable, whose two clauses have disjoint witnesses: `>= 0.0` is
 * witnessed only by the negative cases and `<= MAX` only by the too-loose ones.
 * Mutating the predicate to always-true makes all eight go red.
 */

#include "angle.h"
#include "support/death.h"

#include <math.h>
#include <stdio.h>

/* In-range first, outside the armed window, so a guard that fired on a
 * perfectly good tolerance cannot pass for the death under test. */
static void preflight(void)
{
    if (cq_angle_lattice(0.0, 0.0) != CQ_ANGLE_IDENTITY ||
        cq_angle_lattice(1.0, CQ_ANGLE_TOLERANCE_DEFAULT) != CQ_ANGLE_GENERAL)
        fprintf(stderr, "unreachable: the lattice refused a legal tolerance\n");
}

static void a_negative_tolerance(void)
{
    preflight();
    CQ_EXPECT_ABORT(cq_angle_set_tolerance(-1e-12));
}

static void a_nan_tolerance(void)
{
    preflight();
    CQ_EXPECT_ABORT(cq_angle_set_tolerance(NAN));
}

static void an_infinite_tolerance(void)
{
    preflight();
    CQ_EXPECT_ABORT(cq_angle_set_tolerance(INFINITY));
}

static void the_lattice_called_directly_with_a_negative_tolerance(void)
{
    preflight();
    CQ_EXPECT_ABORT(cq_angle_lattice(1.0, -1.0));
}

static void the_lattice_called_directly_with_a_nan_tolerance(void)
{
    preflight();
    CQ_EXPECT_ABORT(cq_angle_lattice(1.0, NAN));
}

static void the_lattice_called_directly_with_an_infinite_tolerance(void)
{
    preflight();
    CQ_EXPECT_ABORT(cq_angle_lattice(1.0, INFINITY));
}

/* The two that are about CORRECTNESS rather than loudness, and the only
 * witnesses for tolerance_is_usable's `<= CQ_ANGLE_TOLERANCE_MAX` clause: the
 * cap is what bounds |k| for cq_angle_lattice's cast to long long. The value is
 * one decade past the cap, so it also fails if the cap is merely loosened. */
static void a_tolerance_above_the_cap(void)
{
    preflight();
    CQ_EXPECT_ABORT(cq_angle_set_tolerance(CQ_ANGLE_TOLERANCE_MAX * 10.0));
}

static void the_lattice_called_directly_with_a_tolerance_above_the_cap(void)
{
    preflight();
    CQ_EXPECT_ABORT(cq_angle_lattice(1.0, CQ_ANGLE_TOLERANCE_MAX * 10.0));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(a_negative_tolerance),
    CQ_DEATH_CASE(a_nan_tolerance),
    CQ_DEATH_CASE(an_infinite_tolerance),
    CQ_DEATH_CASE(a_tolerance_above_the_cap),
    CQ_DEATH_CASE(the_lattice_called_directly_with_a_negative_tolerance),
    CQ_DEATH_CASE(the_lattice_called_directly_with_a_nan_tolerance),
    CQ_DEATH_CASE(the_lattice_called_directly_with_an_infinite_tolerance),
    CQ_DEATH_CASE(the_lattice_called_directly_with_a_tolerance_above_the_cap)
)
