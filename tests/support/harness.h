/* tests/support/harness.h — the test harness (IMPLEMENTATION_PLAN §2.2).
 *
 * Hand-rolled, because the project takes no dependency beyond libc (PRD §14).
 * One binary per module: a file defines its cases with CQ_TEST() and lists
 * them in CQ_TEST_MAIN(). Output is TAP 13; the process exits non-zero iff
 * some case failed.
 *
 * A failing CHECK records the failure and lets the case run on, so one run
 * reports every broken assertion in a suite rather than only the first. This
 * matters most for the exhaustive tables (L0's 159 cases, L1's masks), where
 * the shape of the failures is the diagnosis.
 *
 * The other four §2.2 support files land with the modules they need:
 * mock_sink at Step 5 (it needs M04's vtable), and refmodel / bitkinds /
 * poolcheck across Phase B.
 */
#ifndef CQOPS_TEST_HARNESS_H
#define CQOPS_TEST_HARNESS_H

#include <stddef.h>

typedef void (*cq_test_fn)(void);

typedef struct {
    const char *name;
    cq_test_fn  fn;
} cq_test_case;

/* Records one failure against the case currently running, with context. */
void cq_h_fail(const char *file, int line, const char *fmt, ...);

/* Runs every case, emits TAP 13, returns 0 iff all of them passed. */
int cq_h_run(const cq_test_case *cases, size_t n);

/* Comparison helper for CHECK_STR_EQ; NULL-safe on both sides. */
int cq_h_streq(const char *a, const char *b);

/* Command-line flags, for the suites that have a mode as well as a verdict —
 * at present only L4's golden regeneration. Recorded by CQ_TEST_MAIN_ARGV and
 * absent otherwise, so a suite built on plain CQ_TEST_MAIN sees no flags.
 *
 * NOTE WHAT THIS CANNOT REACH. CTest does not forward trailing arguments to a
 * test executable, so `ctest -- --update-goldens` (as plan §4 prints it) never
 * arrives here. The flag works when the binary is run directly; through ctest
 * the mechanism is an environment variable, which is why goldens.c consults
 * both. */
void cq_h_args(int argc, char **argv);
int  cq_h_flag(const char *name);

/* FALSIFIABILITY. An assertion nobody has ever seen fail is an assertion
 * nobody has tested, and this project has now measured that twice: a CHECK
 * that could not fail would make every suite vacuously green (which is why
 * tests/test_harness_negative.c exists), and a mutation battery over the Phase
 * B driver found five of its pool assertions surviving mutation to
 * always-true — correct, load-bearing, and never once observed to fire.
 *
 * These two let a suite provoke a failure and then assert that it happened.
 * `cq_h_mute` suppresses the printed FAIL lines so a green run is not full of
 * alarming diagnostics; the count still accrues. `cq_h_take_failures` returns
 * the count recorded since the last take and resets it to zero, so the
 * provoked failure does not fail the case that provoked it.
 *
 * USE THEM ONLY IN PAIRS AND ONLY AROUND THE PROVOCATION. A mute left on
 * silences every real failure after it, which would be the vacuous-green
 * failure mode arriving by the back door — see CQ_EXPECT_CAUGHT in
 * tests/test_kerneldrv.c for the shape that cannot leave it on. */
void cq_h_mute(int on);
int  cq_h_take_failures(void);

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) cq_h_fail(__FILE__, __LINE__, "CHECK(%s)", #cond);       \
    } while (0)

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        long long cq_a_ = (long long)(actual);                                \
        long long cq_e_ = (long long)(expected);                              \
        if (cq_a_ != cq_e_)                                                   \
            cq_h_fail(__FILE__, __LINE__, "CHECK_EQ(%s, %s): %lld != %lld",   \
                      #actual, #expected, cq_a_, cq_e_);                      \
    } while (0)

#define CHECK_STR_EQ(actual, expected)                                        \
    do {                                                                      \
        const char *cq_sa_ = (actual);                                        \
        const char *cq_se_ = (expected);                                      \
        if (!cq_h_streq(cq_sa_, cq_se_))                                      \
            cq_h_fail(__FILE__, __LINE__,                                     \
                      "CHECK_STR_EQ(%s, %s): \"%s\" != \"%s\"",               \
                      #actual, #expected,                                     \
                      cq_sa_ ? cq_sa_ : "(null)",                             \
                      cq_se_ ? cq_se_ : "(null)");                            \
    } while (0)

/* L4's gate-count assertion. The full (NOT, CNOT, Toffoli) tuple is always
 * matched — two unrelated upstream circuits can share a total, so a total
 * alone is not an identification (CLAUDE.md, Rule 10). The total is reported
 * as a checksum, never as the assertion. */
#define CHECK_GATES(a_not, a_cnot, a_tof, e_not, e_cnot, e_tof)               \
    do {                                                                      \
        long long cq_an_ = (long long)(a_not);                                \
        long long cq_ac_ = (long long)(a_cnot);                               \
        long long cq_at_ = (long long)(a_tof);                                \
        long long cq_en_ = (long long)(e_not);                                \
        long long cq_ec_ = (long long)(e_cnot);                               \
        long long cq_et_ = (long long)(e_tof);                                \
        if (cq_an_ != cq_en_ || cq_ac_ != cq_ec_ || cq_at_ != cq_et_)         \
            cq_h_fail(__FILE__, __LINE__,                                     \
                      "CHECK_GATES: got (NOT %lld, CNOT %lld, Toffoli %lld; " \
                      "total %lld), want (%lld, %lld, %lld; total %lld)",     \
                      cq_an_, cq_ac_, cq_at_, cq_an_ + cq_ac_ + cq_at_,       \
                      cq_en_, cq_ec_, cq_et_, cq_en_ + cq_ec_ + cq_et_);      \
    } while (0)

#define CQ_TEST(name) static void name(void)
#define CQ_CASE(fn)   { #fn, fn }

#define CQ_TEST_MAIN(...)                                                     \
    int main(void)                                                            \
    {                                                                         \
        static const cq_test_case cq_cases_[] = { __VA_ARGS__ };              \
        return cq_h_run(cq_cases_, sizeof cq_cases_ / sizeof cq_cases_[0]);   \
    }

/* The same, for a suite that reads a flag. Separate rather than universal so
 * that `main(void)` stays the default shape and no suite acquires an argv it
 * does not use. */
#define CQ_TEST_MAIN_ARGV(...)                                                \
    int main(int argc, char **argv)                                           \
    {                                                                         \
        static const cq_test_case cq_cases_[] = { __VA_ARGS__ };              \
        cq_h_args(argc, argv);                                                \
        return cq_h_run(cq_cases_, sizeof cq_cases_ / sizeof cq_cases_[0]);   \
    }

#endif /* CQOPS_TEST_HARNESS_H */
