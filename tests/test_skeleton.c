/* Step 1's gate: the build, the harness and the link all work, under both
 * configurations. It asserts nothing about the backend — no backend exists
 * yet. M01's tri-valued bit arrives at Step 2. */

#include "cqops/cqops.h"
#include "support/harness.h"

#include <stdio.h>

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

CQ_TEST_MAIN(
    CQ_CASE(library_links_and_reports_its_version),
    CQ_CASE(check_macros_pass_on_truth),
    CQ_CASE(debug_invariants_track_the_configuration),
    CQ_CASE(sanitizer_coverage_is_what_the_build_claims)
)
