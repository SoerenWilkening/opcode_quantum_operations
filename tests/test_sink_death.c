/* Step 5's other half: M04's fail-loud paths.
 *
 * The theme is that a gate must never be silently discarded. Emission is the
 * library's only output; a NULL vtable entry or an unresolvable sink name
 * would drop gates on the floor and leave every downstream suite green while
 * verifying nothing at all.
 */

#include "sink.h"
#include "support/death.h"
#include "support/mock_sink.h"

#include <stdlib.h>

/* A vtable with a hole in it. The qec sink "stubs" Ry/Rz (PRD §8), and a stub
 * is a no-op function — not a NULL pointer. Calling through the hole would be
 * undefined behaviour, so it is caught and named instead. */
static void missing_entry_is_not_called(void)
{
    cq_mock m;
    cq_mock_init(&m);
    cq_sink s = cq_mock_sink(&m);
    s.ccx = NULL;

    cq_sink_x(&s, 0u);          /* the intact entries still work */
    CQ_EXPECT_ABORT(cq_sink_ccx(&s, 1u, 2u, 3u));
}

/* CQOPS_SINK naming something that was never registered. Silently falling
 * back would hand the user a different sink than the one they asked for. */
static void env_names_an_unknown_sink(void)
{
    cq_mock m;
    cq_mock_init(&m);
    cq_sink_reset();

    cq_sink s = cq_mock_sink(&m);
    cq_sink_register("printf", &s);
    setenv("CQOPS_SINK", "telepathy", 1);

    CQ_EXPECT_ABORT(cq_sink_active());
}

/* No env var and no "printf" registered: there is no default to fall back on,
 * and returning NULL would only move the crash somewhere less informative. */
static void no_sink_registered_at_all(void)
{
    cq_sink_reset();
    unsetenv("CQOPS_SINK");

    CQ_EXPECT_ABORT(cq_sink_active());
}

/* The registry is a small fixed table; overflowing it is a programming error
 * in the caller, not a condition to grow silently past. */
static void registry_overflow(void)
{
    static const char *names[] = { "s0", "s1", "s2", "s3", "s4", "s5",
                                   "s6", "s7", "s8", "s9", "s10", "s11" };
    cq_mock m;
    cq_mock_init(&m);
    cq_sink_reset();
    cq_sink s = cq_mock_sink(&m);

    size_t i = 0;
    for (; i < CQ_SINK_MAX; i++) cq_sink_register(names[i], &s);

    CQ_EXPECT_ABORT(cq_sink_register(names[i], &s));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(missing_entry_is_not_called),
    CQ_DEATH_CASE(env_names_an_unknown_sink),
    CQ_DEATH_CASE(no_sink_registered_at_all),
    CQ_DEATH_CASE(registry_overflow)
)
