/* Step 1's gate: the build, the harness and the link all work, under both
 * configurations. It asserts nothing about the backend — no backend exists
 * yet. M01's tri-valued bit arrives at Step 2. */

#include "cqops/cqops.h"
#include "support/fphost.h"
#include "support/harness.h"

#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The negative control below changes the rounding mode at run time, which is
 * exactly what this pragma licenses: without it a compiler may assume the
 * default mode and hoist floating-point work across fesetround. Guarded on
 * __clang__ because gcc answers the pragma with "not supported, ignoring" and
 * -Werror turns that into a build failure; this tree is clang-only
 * (.github/workflows/ci.yml), and the guard keeps a gcc box building rather
 * than silently miscompiling. Measured under -Wall -Wextra -Werror
 * -Wconversion on Apple clang 17 and Homebrew clang 22: accepted, silently. */
#if defined(__clang__)
#  pragma STDC FENV_ACCESS ON
#endif

CQ_TEST(library_links_and_reports_its_version)
{
    /* A real symbol out of libcqops.a, so the include path, the archive and
     * the link line are all proved rather than assumed. */
    const char *v = cqops_version_string();

    CHECK(v != NULL);
    CHECK_STR_EQ(v, CQOPS_VERSION_STRING);
    CHECK_EQ(CQOPS_VERSION_MAJOR, 0);
    CHECK_EQ(CQOPS_VERSION_MINOR, 1);
    CHECK_EQ(CQOPS_VERSION_PATCH, 0);
}

CQ_TEST(check_macros_pass_on_truth)
{
    /* Arbitrary numbers: this exercises the macros, it pins no circuit. The
     * first real L4 golden arrives with K6 at Step 12. */
    CHECK(1 == 1);
    CHECK_EQ(2 + 2, 4);
    CHECK_STR_EQ("cqops", "cqops");
    CHECK_GATES(6, 40, 12,   6, 40, 12);
}

/* A PROVOCATION WINDOW'S ARITHMETIC, FROM THE GREEN SIDE (`bd 9ve.33`). The
 * mute/take pair under harness.h's FALSIFIABILITY heading is what every
 * CQ_EXPECT_CAUGHT / CQ_EXPECT_CLEAN in the tree is built from, and until
 * 2026-09-18 a take zeroed the case's whole tally rather than only what the
 * window provoked. This case lives here, in the suite that asserts the harness
 * itself, rather than in any of the suites that merely use it.
 *
 * WHAT IT CAN AND CANNOT SEE, STATED RATHER THAN IMPLIED. It kills a take that
 * forgot to forgive (after_fail would exceed base), one that counted a
 * provoked failure twice or not at all (caught != 1), and one that charged the
 * case for a PASS inside a window. It CANNOT see a take that zeroes the tally
 * outright — that is only visible when the tally was non-zero on the way in,
 * and a case cannot carry a surviving failure and still be green. The process
 * that can is tests/test_harness_negative_window.c, registered WILL_FAIL, and
 * the two together are the whole claim. */
CQ_TEST(a_provocation_window_counts_only_what_happens_inside_it)
{
    int base, caught, after_fail, clean, after_pass;

    base = cq_h_failures_now();

    /* A failure INSIDE the window: counted once, by the window, and taken back
     * out of the tally the case will be judged on. */
    cq_h_mute(1);
    (void)cq_h_take_failures();
    CHECK_EQ(1, 2);
    caught = cq_h_take_failures();
    cq_h_mute(0);
    after_fail = cq_h_failures_now();

    /* And a PASS inside one is counted by nobody — the negative control for
     * the leg above, without which "forgive the window" and "forgive
     * everything" read the same. */
    cq_h_mute(1);
    (void)cq_h_take_failures();
    CHECK_EQ(1, 1);
    clean = cq_h_take_failures();
    cq_h_mute(0);
    after_pass = cq_h_failures_now();

    /* Asserted after both windows are shut, so a CHECK here can never be the
     * thing a window is measuring. */
    CHECK_EQ(caught, 1);
    CHECK_EQ(after_fail, base);
    CHECK_EQ(clean, 0);
    CHECK_EQ(after_pass, base);
}

CQ_TEST(debug_invariants_track_the_configuration)
{
    /* Both configurations are legal; what is asserted is that the flag says
     * which one we are in, so a later suite can tell whether the invariant
     * machinery it depends on is actually compiled in. */
#ifdef CQOPS_DEBUG_INVARIANTS
    CHECK_EQ(CQOPS_DEBUG_INVARIANTS, 1);
    printf("# built with CQOPS_DEBUG_INVARIANTS: invariant checks are live\n");
#else
    printf("# built without CQOPS_DEBUG_INVARIANTS: Release, counts pinnable\n");
#endif
}

/* What the compiler actually did, independent of what CMake believes. */
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define CQ_ASAN_LIVE 1
#  endif
#  if __has_feature(undefined_behavior_sanitizer)
#    define CQ_UBSAN_LIVE 1
#  endif
#endif
#ifndef CQ_ASAN_LIVE
#  define CQ_ASAN_LIVE 0
#endif
#ifndef CQ_UBSAN_LIVE
#  define CQ_UBSAN_LIVE 0
#endif

CQ_TEST(sanitizer_coverage_is_what_the_build_claims)
{
    /* The build probes each sanitizer and enables only what runs on this host
     * (cmake/CqopsSanitizers.cmake). This case is the cross-check: a
     * sanitizer CMake selected but whose flag never reached the compile line
     * would otherwise leave Debug quietly uninstrumented while still calling
     * itself Debug. Rule 17 — a coverage claim has to be literal. */
    CHECK_EQ(CQ_ASAN_LIVE, CQOPS_BUILD_ASAN);
    CHECK_EQ(CQ_UBSAN_LIVE, CQOPS_BUILD_UBSAN);

    printf("# sanitizers live in this binary: ASan=%d UBSan=%d\n",
           CQ_ASAN_LIVE, CQ_UBSAN_LIVE);
}

/* LEAK DETECTION IS THE ONE THAT __has_feature CANNOT ANSWER (`bd kfi`).
 * LeakSanitizer is not a -fsanitize= flag here: it lives inside the ASan runtime
 * and is switched on by ASAN_OPTIONS=detect_leaks=1, which cmake/CqopsTest.cmake
 * writes into every test's CTest ENVIRONMENT property. So the thing that can
 * silently go missing is not a compile line but an environment string — and the
 * cross-check has to read the environment, which is what this does.
 *
 * IT IS THE CHEAP HALF OF A PAIR. tests/test_lsan_negative.c is the expensive
 * half: it leaks on purpose and is registered WILL_FAIL, so it goes red if the
 * option stops working. But it is registered ONLY when the build claims leak
 * detection, so on its own it cannot notice the build having quietly stopped
 * claiming it. This case is what makes that direction loud, in every
 * configuration, including the ones where the answer is a legitimate zero. */
static int cq_env_has_detect_leaks(void)
{
    const char *opts = getenv("ASAN_OPTIONS");
    return opts != NULL && strstr(opts, "detect_leaks=1") != NULL;
}

CQ_TEST(leak_detection_is_what_the_build_claims)
{
    CHECK_EQ(cq_env_has_detect_leaks(), CQOPS_BUILD_LSAN);

    /* LSan ships inside the ASan runtime, so claiming it without ASan is a
     * configuration that cannot exist. cmake/CqopsSanitizers.cmake refuses to
     * produce one; this is the assertion that says so from the binary's side. */
    if (CQOPS_BUILD_LSAN) CHECK_EQ(CQ_ASAN_LIVE, 1);

    printf("# leak detection live in this run: %d (ASAN_OPTIONS=%s)\n",
           cq_env_has_detect_leaks(),
           getenv("ASAN_OPTIONS") ? getenv("ASAN_OPTIONS") : "(unset)");
}

/* THE FP HOST IS A THIRD THING THE BUILD BELIEVES AND THE BINARY MUST CONFIRM
 * (PRD-v2 §7.4). L1's fp oracle is the HOST operator with the IEEE-unspecified
 * cells pinned by table, so a host that rounds somewhere other than to-nearest,
 * or that flushes subnormals, or whose NaN cells are not x86's, would fail every
 * fp anchor in the ORACLE rather than in the port — and the failure would read
 * as our bug. cmake/CqopsFpHost.cmake measures it at configure time and compiles
 * the verdict in; this case is the cross-check, and it is a SECOND MEASUREMENT
 * rather than a restatement: the probe builds tests/support/fphost.c with no -O
 * flag, while this binary carries the configuration's own optimisation level and
 * sanitizers. The two agreeing is the claim.
 *
 * CQOPS_BUILD_FPHOST IS THREE-VALUED, and that is not decoration: 1 the probe
 * ran and passed, 0 it ran and failed, -1 it never ran (CQOPS_FPHOST=OFF). A
 * two-valued spelling would make "not measured" indistinguishable from
 * "measured and false", so a bare `!CQOPS_BUILD_FPHOST` downstream of this is
 * wrong for the same reason `!proven_zero` was (CLAUDE.md, three-valued proof).
 * Compare with >= 0, never with a negation. */
CQ_TEST(fp_host_is_what_the_build_claims)
{
    char why[CQ_FPHOST_WHY_MAX];
    const int live = cq_fphost_check(why, sizeof why);

    if (CQOPS_BUILD_FPHOST >= 0) {
        CHECK_EQ(live, CQOPS_BUILD_FPHOST);
    } else {
        printf("# fp host NOT probed at configure time (CQOPS_FPHOST=OFF)\n");
    }

    /* Every arm has to have RUN. A deleted arm still returns 1 on a conforming
     * host and is invisible to the verdict alone — the same hole the childcap
     * EINTR site count exists to close. */
    if (live) CHECK_EQ(cq_fphost_arms_run(), CQ_FPHOST_ARMS);

    printf("# fp host live in this binary: %d (build claims %d), %d arm(s) run\n",
           live, CQOPS_BUILD_FPHOST, cq_fphost_arms_run());
    printf("# fp host mode: fegetround()=%d FE_TONEAREST=%d; %s\n",
           fegetround(), FE_TONEAREST, live ? "all arms hold" : why);
}

/* AN ASSERTION NOBODY HAS SEEN FAIL IS AN ASSERTION NOBODY HAS TESTED. The
 * whole check is a tripwire for a host this project has never run on, so on
 * this one it can only ever return 1 — and a cq_fphost_check mutated to
 * `return 1;` would pass the case above in every configuration.
 *
 * fesetround is the one arm that can be provoked portably at run time, and it
 * is provoked here. The other nine are not reachable without a host-specific
 * control register (FTZ and DAZ live in MXCSR on x86) or a different machine;
 * that gap is stated rather than implied. */
CQ_TEST(the_fp_host_check_can_fail_and_names_the_rounding_arm)
{
    static const char sentinel[] = "sentinel-not-overwritten";
    char why[CQ_FPHOST_WHY_MAX];
    const int saved = fegetround();

    /* Baseline first, so a red below is the provocation and not the box. */
    memcpy(why, sentinel, sizeof sentinel);
    CHECK_EQ(cq_fphost_check(why, sizeof why), 1);

    CHECK_EQ(fesetround(FE_UPWARD), 0);
    if (fegetround() == FE_UPWARD) {
        memcpy(why, sentinel, sizeof sentinel);
        CHECK_EQ(cq_fphost_check(why, sizeof why), 0);
        /* WHICH arm, not merely that one fired: the check returns 0 for ten
         * different reasons and a bare 0 cannot tell them apart. */
        CHECK(strstr(why, "rounding") != NULL);
        CHECK_EQ(cq_fphost_arms_run(), 0);
        printf("# fp host negative control: %s\n", why);
    } else {
        cq_h_fail(__FILE__, __LINE__,
                  "fesetround(FE_UPWARD) reported success and did not take "
                  "effect (fegetround() = %d) — the negative control verified "
                  "nothing on this host", fegetround());
    }

    CHECK_EQ(fesetround(saved), 0);
    CHECK_EQ(cq_fphost_check(why, sizeof why), 1);
}

CQ_TEST_MAIN(
    CQ_CASE(library_links_and_reports_its_version),
    CQ_CASE(check_macros_pass_on_truth),
    CQ_CASE(a_provocation_window_counts_only_what_happens_inside_it),
    CQ_CASE(debug_invariants_track_the_configuration),
    CQ_CASE(sanitizer_coverage_is_what_the_build_claims),
    CQ_CASE(leak_detection_is_what_the_build_claims),
    CQ_CASE(fp_host_is_what_the_build_claims),
    CQ_CASE(the_fp_host_check_can_fail_and_names_the_rounding_arm)
)
