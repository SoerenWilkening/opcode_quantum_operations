/* Deliberately leaks. Registered WILL_FAIL in tests/CMakeLists.txt, and ONLY
 * when the build has measured that leak detection works here — so CTest passes
 * it exactly when a leak in an ordinary test binary makes that binary exit
 * non-zero (`bd kfi`).
 *
 * WHY IT EXISTS. Leak detection is the one sanitizer in this project that is
 * NOT a compile flag: LeakSanitizer ships inside the ASan runtime, is off by
 * default on Darwin, and is turned on through an ASAN_OPTIONS setting that
 * cmake/CqopsTest.cmake writes into every test's ENVIRONMENT property. Nothing
 * about a green run would change if that string lost its detect_leaks=1 — every
 * suite would keep passing while checking one thing less. This binary is the
 * only thing in the tree that goes red in that case, which is
 * tests/test_harness_negative.c's argument applied to a sanitizer instead of to
 * a CHECK. test_skeleton's cross-check is the cheap half; this is the
 * end-to-end one, and it reads the SAME environment every other suite gets
 * rather than one written for it.
 *
 * WHY WILL_FAIL IS LEGAL HERE AND NOT FOR A DEATH TEST. tests/support/death.h
 * records that WILL_FAIL inverts a non-zero EXIT CODE and does not invert a
 * crash. Measured 2026-08-28 on Homebrew clang 22.1.5 / Darwin 25: a leak report
 * exits 1 NORMALLY, and does so even under ASAN_OPTIONS=abort_on_error=1 —
 * abort_on_error governs hard errors, not the end-of-process leak check. So this
 * is the same shape as test_harness_negative, which "works because it exits
 * non-zero normally".
 *
 * If this test ever starts FAILING, leak detection is off, not fixed.
 *
 * NOTE the leak is made through a noinline function and a volatile static that
 * is then cleared. LSan scans globals and the stack conservatively as roots, so
 * a block anything still points at is "still reachable" — which it does not
 * report by default. cmake/CqopsSanitizers.cmake's probe leaks identically, and
 * the two must stay in step: if the technique stops producing a report, the
 * probe turns detect_leaks off and this binary is not registered at all. */

#include "support/harness.h"

#include <stdlib.h>

static void *volatile cq_hold;

/* noinline so the allocation cannot be sunk into main's frame, where a stale
 * copy of the pointer is likelier to survive as a conservative root. */
__attribute__((noinline)) static void cq_leak_one(void)
{
    cq_hold = malloc(4096);
    cq_hold = 0;
}

CQ_TEST(a_leaked_allocation_makes_the_process_exit_nonzero)
{
    /* Every assertion here PASSES: the harness returns 0 and the non-zero exit
     * comes from LeakSanitizer's atexit check alone. That is the point — if the
     * harness failed too, a broken CHECK would keep this binary red and the
     * leak claim would be vacuous. */
    cq_leak_one();
    CHECK(1 == 1);
}

CQ_TEST_MAIN(
    CQ_CASE(a_leaked_allocation_makes_the_process_exit_nonzero)
)
