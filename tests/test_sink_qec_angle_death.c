/* M25b's fail-loud paths, Step 26.
 *
 * ALL FOUR ABORT IN BOTH CONFIGURATIONS. None is Debug-gated: a rotation the
 * conversion cannot express is a gate that would otherwise reach the QEC
 * library wrong, and PRD §15 D19's whole point is that no VALUE check
 * downstream could see it.
 *
 * THE LAST ONE IS THE INTERESTING ONE. `qec_rz` takes a `long p`, so an angle
 * whose multiple of π does not fit in a long cannot be handed over at all. The
 * tempting alternative — reduce θ mod 4π first — is not available here: at
 * |θ| = 1e300 the input double's own spacing is ~1e284 rad, so there is no
 * fractional part left to reduce and any answer would be an invention. Refusing
 * is the honest row, and it is the same posture src/angle.c takes when |θ|
 * passes its own magnitude bound (D10).
 */

#include "sink_qec_angle.h"

#include "support/death.h"

#include <math.h>

static long g_p, g_q;

static void a_nan_angle(void)
{
    CQ_EXPECT_ABORT(cq_qec_ratio(nan(""), CQ_QEC_DENOM_CAP, &g_p, &g_q));
}

static void an_infinite_angle(void)
{
    CQ_EXPECT_ABORT(cq_qec_ratio(INFINITY, CQ_QEC_DENOM_CAP, &g_p, &g_q));
}

/* A cap of zero would make every denominator illegal and the loop would return
 * the bare integer part with no refinement — a plausible-looking answer to a
 * meaningless question. */
static void a_cap_below_one(void)
{
    CQ_EXPECT_ABORT(cq_qec_ratio(3.14, 0, &g_p, &g_q));
}

static void an_angle_too_large_for_a_long(void)
{
    CQ_EXPECT_ABORT(cq_qec_ratio(1e300, CQ_QEC_DENOM_CAP, &g_p, &g_q));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(a_nan_angle),
    CQ_DEATH_CASE(an_infinite_angle),
    CQ_DEATH_CASE(a_cap_below_one),
    CQ_DEATH_CASE(an_angle_too_large_for_a_long)
)
