/* tests/support/death.h — asserting that a fail-loud path really fires.
 *
 * Every hard error in libcqops is an abort() (Rule 6: releasing an index not
 * proven |0> is a hard error, not a warning). Asserting one is awkward for two
 * reasons, and this header exists to make both go away.
 *
 * That parenthesis read "a free of a dirty rail is a hard error" until PRD §15
 * D15 §4's last clause was confirmed 2026-08-22 and shipped at Step 23: a free
 * that cannot prove its rail clean now STRANDS by default and aborts only under
 * CQOPS_FREE_ABORT, which the death cases that assert a refusal set per case.
 *
 * WHY NOT WILL_FAIL. CTest's WILL_FAIL inverts a non-zero EXIT CODE and does
 * NOT invert a crash: abort() raises SIGABRT, CTest reports "Subprocess
 * aborted", and the test stays failed however WILL_FAIL is set. (Measured at
 * Step 3; it is what the CMake docs mean by "does not work for the crash
 * cases".) tests/test_harness_negative.c is not a counter-example — it works
 * because it exits non-zero *normally*. So a death test catches SIGABRT
 * itself and exits 0 only when the abort arrived where it was expected, which
 * makes it an ORDINARY add_cqops_death_test rather than a WILL_FAIL one, and
 * makes "nothing aborted" a failure instead of a silent pass.
 *
 * WHY A SELECTOR. The process is gone after one abort, so it is one death per
 * RUN — but not one death per FILE: argv[1] names the case, and
 * add_cqops_death_test registers one CTest test per case against the same
 * binary. Step 4 needs five deaths and Step 6 needs four; without this they
 * would be nine near-identical files.
 *
 * Keep the arming window tight. CQ_EXPECT_ABORT arms immediately before the
 * statement and disarms after, so an abort from anywhere else — an ASan
 * report under ASAN_OPTIONS=abort_on_error=1, a failure in the case's own
 * setup — exits 4 instead of masquerading as the death under test.
 */
#ifndef CQOPS_TEST_DEATH_H
#define CQOPS_TEST_DEATH_H

#include "sink.h"      /* cq_death_null_sink returns one by value */

#include <stddef.h>

typedef struct {
    const char *name;
    void      (*fn)(void);
} cq_death_case;

/* Arms/disarms the SIGABRT window. Use CQ_EXPECT_ABORT rather than these. */
void cq_death_arm(void);
void cq_death_disarm(void);

/* Reports that the statement returned instead of aborting, and exits 1. */
void cq_death_survived(const char *file, int line, const char *stmt);

/* AN ASSERTION THAT ACTUALLY FAILS, INSIDE A DEATH CASE. Added at Step 23,
 * because a death binary had no such thing and the obvious substitute is a
 * SILENT NO-OP: harness.h's CHECK increments a counter that only CQ_TEST_MAIN
 * reads, and CQ_DEATH_MAIN never looks at it — so a CHECK in a death case
 * prints a TAP diagnostic line, changes no exit code, and the case passes.
 *
 * IT EXISTS BECAUSE A DEATH TEST'S ONLY NATIVE CLAIM IS "IT ABORTED", AND THAT
 * IS SOMETIMES TOO WEAK. PRD §15 D15 §3 splits a free's refusal into a PROVEN
 * DIRTY row and an UNPROVEN one; both take the SAME ACT, and under
 * CQOPS_FREE_ABORT both abort with the same exit code, so a pair of cases
 * asserting only the abort cannot tell a refusal from ignorance — and
 * IMPLEMENTATION_PLAN.md's Step 23 row names one of them by path as the
 * certificate's mandatory negative control, which is a claim about the VERDICT
 * and not about the act.
 *
 * Use it for a case's PRECONDITIONS, before CQ_EXPECT_ABORT arms the window; a
 * failure here is a broken fixture rather than a library defect, which is why
 * the exit status is distinct from both of the other two. */
void cq_death_require(const char *file, int line, const char *expr, int cond);

#define CQ_DEATH_REQUIRE(cond)                                                \
    cq_death_require(__FILE__, __LINE__, #cond, (cond) != 0)

/* Reports that the case cannot run in this configuration, and exits 0. */
void cq_death_skip(const char *why);

/* Dispatches argv[1] to a case; returns a process exit status. */
int cq_death_main(int argc, char **argv, const cq_death_case *cases, size_t n);

#define CQ_EXPECT_ABORT(stmt)                                                 \
    do {                                                                      \
        cq_death_arm();                                                       \
        (stmt);                                                               \
        cq_death_disarm();                                                    \
        cq_death_survived(__FILE__, __LINE__, #stmt);                         \
    } while (0)

/* Some hard errors are Debug-gated by design — plan §2.1 assigns the I2 owner
 * map, the I6 scratch-extent check and the §3 operand-distinctness asserts to
 * CQOPS_DEBUG_INVARIANTS. Their death cases cannot fire in Release, so they
 * SKIP there rather than fail, and say so loudly: a Release run must never
 * report having verified something the configuration compiled out (Rule 17).
 * Place this after the case's setup and before its CQ_EXPECT_ABORT. */
#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
#  define CQ_DEATH_SKIP_WITHOUT_INVARIANTS(why) ((void)0)
#else
#  define CQ_DEATH_SKIP_WITHOUT_INVARIANTS(why) cq_death_skip(why)
#endif

/* THE DISCARDING SINK, in one place (bd cue). Seventeen death suites carried a
 * byte-identical copy of the same six no-op vtable entries before 2026-09-17.
 *
 * WHAT IT IS FOR. Most death cases reach their abort through ordinary
 * bookkeeping and emit real gates while building the rail they then misuse, so
 * a sink that treated emission as a failure would fail the SETUP rather than
 * the assertion. Passing NULL to cq_ctx_init is not an option either: it
 * resolves CQOPS_SINK, and "no default sink registered" is itself a hard error,
 * raised outside the armed window. And a death binary's stdout is not a trace,
 * so the shim suites install this before their first cq_shim_ctx() to stop the
 * built-in printf sink winning.
 *
 * IT IS OPT-IN AND MUST STAY THAT WAY. A death file whose sink MEASURES
 * something is not a candidate: tests/test_emit_death.c's entries DISARM the
 * window and report a leaked gate, tests/test_rotate_death.c's `mz` is a
 * tripwire for an emission past the abort point, and four suites install a
 * COUNTING sink because a case reads the count. Converting any of those is a
 * silent loss of exactly what that file exists to see — the failure direction
 * is a green run, not a red one.
 *
 * BY VALUE, on cq_sink_counter's and cq_mock_sink's precedent, so the caller
 * keeps owning the storage: `cqops_set_sink` borrows the pointer it is given
 * and does not copy, so the object assigned into must outlive its use — every
 * caller here keeps a file-static for that reason. */
cq_sink cq_death_null_sink(void);

#define CQ_DEATH_CASE(fn) { #fn, fn }

#define CQ_DEATH_MAIN(...)                                                    \
    int main(int argc, char **argv)                                           \
    {                                                                         \
        static const cq_death_case cq_d_[] = { __VA_ARGS__ };                 \
        return cq_death_main(argc, argv, cq_d_,                               \
                             sizeof cq_d_ / sizeof cq_d_[0]);                 \
    }

#endif /* CQOPS_TEST_DEATH_H */
