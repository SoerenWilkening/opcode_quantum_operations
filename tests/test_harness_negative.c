/* Deliberately fails. Registered WILL_FAIL in tests/CMakeLists.txt, so CTest
 * passes it only when this binary exits NON-zero.
 *
 * This is the harness asserting its own failure path. A CHECK that could not
 * fail — a typo'd macro, a swallowed exit code, a `main` that returns 0
 * regardless — would make every suite in the project vacuously green while
 * verifying nothing. That is precisely the failure mode this codebase cannot
 * afford (CLAUDE.md, Rule 17: report only what you ran), so the failure path
 * is tested rather than assumed.
 *
 * If this test ever starts PASSING, the harness is broken, not fixed. */

#include "support/harness.h"

CQ_TEST(a_failing_check_is_reported_and_exits_nonzero)
{
    CHECK_EQ(1, 2);
}

CQ_TEST_MAIN(
    CQ_CASE(a_failing_check_is_reported_and_exits_nonzero)
)
