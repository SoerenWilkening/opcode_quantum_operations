/* Deliberately fails. Registered WILL_FAIL in tests/CMakeLists.txt, so CTest
 * passes it only when this binary exits NON-zero.
 *
 * THE SIBLING OF tests/test_harness_negative.c, AND A DIFFERENT CLAIM. That
 * file asserts a failing CHECK reaches the exit code at all. This one asserts
 * that a failing CHECK SURVIVES A PROVOCATION WINDOW opened after it — the
 * whole content of `bd 9ve.33`.
 *
 * THE BUG IT PINS, MEASURED 2026-09-18. CQ_EXPECT_CLEAN / CQ_EXPECT_CAUGHT
 * (tests/test_kerneldrv.c) open with cq_h_take_failures(), which zeroed the
 * per-case tally cq_h_run reads. So a CHECK that failed BEFORE one of those
 * macros printed its `# FAIL` line, was forgiven, and the case reported GREEN:
 * tallies of "(2 failed checks)" were seen in runs that emitted seven `# FAIL`
 * lines. Measured on this binary before the fix, it exited 0 and CTest turned
 * this entry RED; after it, it exits 1 and the entry is green.
 *
 * WHY IT HAS TO BE ITS OWN BINARY. A case added to test_harness_negative.c
 * could not discriminate: that binary already exits non-zero from its other
 * case, so reverting the fix would leave it exiting non-zero all the same. The
 * discriminating claim needs a process whose ONLY failure is the one before the
 * window.
 *
 * AND WHY main() IS HAND-WRITTEN. A WILL_FAIL binary's only native claim is
 * "it exited non-zero", which cannot say WHY — the same hole
 * CQ_DEATH_REQUIRE exists to close for a death case, where "it aborted" cannot
 * tell a refusal from a segfault. Here a CQ_EXPECT_CLEAN that wrongly reported
 * the empty window as dirty would ALSO exit non-zero and this binary would pass
 * having verified the opposite of its subject. So it exits non-zero only when
 * the tally is EXACTLY the one earlier failure, and exits 0 — red, under
 * WILL_FAIL — in every other case, saying which on the way out.
 *
 * If this test ever starts passing for a reason other than the one printed
 * below, the harness is broken, not fixed. */

#include "support/harness.h"

#include <stdio.h>

/* CQ_EXPECT_CLEAN's shape, verbatim in the property that matters: mute, take,
 * run, take, unmute, and a verdict on the second take. Copied rather than
 * shared because tests/test_kerneldrv.c owns its pair privately and this binary
 * links none of that suite — the same reason tests/test_childcap_controls.inc
 * carries its own copy. What is under test is the HARNESS contract those bodies
 * rest on, so the copy is the fixture, not a shortcut. */
#define CQ_WND_EXPECT_CLEAN(what, stmt)                                       \
    do {                                                                      \
        cq_h_mute(1);                                                         \
        (void)cq_h_take_failures();                                           \
        stmt;                                                                 \
        int cq_n_ = cq_h_take_failures();                                     \
        cq_h_mute(0);                                                         \
        if (cq_n_ != 0)                                                       \
            cq_h_fail(__FILE__, __LINE__,                                     \
                      "the window reported a CLEAN statement dirty (%s): "    \
                      "%d failure(s)", what, cq_n_);                          \
    } while (0)

/* Read at the end of the case, because cq_h_run resets the tally between
 * cases and reads it the moment the case returns. */
static int wnd_tally = -1;

CQ_TEST(a_failing_check_survives_a_provocation_window_opened_after_it)
{
    /* The one failure this process is about. Unmuted, so it prints and it is
     * the case's own — not something a window provoked. */
    CHECK_EQ(1, 2);

    /* An EMPTY window: it provokes nothing, so its own verdict is "clean" and
     * it must leave the tally exactly as it found it. An empty statement is
     * deliberate — anything the window could legitimately count would make the
     * expected tally a sum rather than the single failure above. */
    CQ_WND_EXPECT_CLEAN("an empty window", (void)0);

    wnd_tally = cq_h_failures_now();
}

int main(void)
{
    static const cq_test_case cq_cases_[] = {
        CQ_CASE(a_failing_check_survives_a_provocation_window_opened_after_it)
    };
    const int rc = cq_h_run(cq_cases_, sizeof cq_cases_ / sizeof cq_cases_[0]);

    printf("# tally the case carried to cq_h_run: %d (want exactly 1)\n",
           wnd_tally);

    if (wnd_tally != 1) {
        printf("# the failure before the window did not survive it (or the "
               "window counted something of its own), so this binary verified "
               "NOTHING — exiting 0 so WILL_FAIL reports it\n");
        return 0;
    }
    if (rc == 0) {
        printf("# the tally was 1 and cq_h_run still called the case ok — the "
               "verdict does not read the tally — exiting 0\n");
        return 0;
    }

    printf("# a CHECK that failed before a provocation window reached the "
           "exit code (bd 9ve.33)\n");
    return 1;
}
