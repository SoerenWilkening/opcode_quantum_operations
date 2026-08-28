/* Step 1's gate: the build, the harness and the link all work, under both
 * configurations. It asserts nothing about the backend — no backend exists
 * yet. M01's tri-valued bit arrives at Step 2. */

#include "cqops/cqops.h"
#include "support/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

CQ_TEST_MAIN(
    CQ_CASE(library_links_and_reports_its_version),
    CQ_CASE(check_macros_pass_on_truth),
    CQ_CASE(debug_invariants_track_the_configuration),
    CQ_CASE(sanitizer_coverage_is_what_the_build_claims),
    CQ_CASE(leak_detection_is_what_the_build_claims)
)
