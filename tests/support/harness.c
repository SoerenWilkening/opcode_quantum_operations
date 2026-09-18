#include "support/harness.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The case currently running, so a failure can name it without every CHECK
 * having to carry it. Single-threaded by construction: the harness runs cases
 * one at a time, and libcqops has no threading. */
static const char *cq_h_case = NULL;

/* THREE COUNTERS, AND THE SPLIT IS THE WHOLE OF `bd 9ve.33`. Until 2026-09-18
 * there was one, and cq_h_take_failures zeroed it — so a provocation window
 * forgave not only what it provoked but everything the case had recorded
 * before it opened. A CHECK that failed ahead of a CQ_EXPECT_CLEAN printed its
 * `# FAIL` line and the case still reported ok; runs emitting seven `# FAIL`
 * lines tallied "(2 failed checks)".
 *
 *   cq_h_case_failures  the case's VERDICT — what cq_h_run reads. Every
 *                       failure lands here, muted or not.
 *   cq_h_since_take     what a take RETURNS: failures since the last take.
 *   cq_h_provoked       ... of which were recorded while MUTED, and so are
 *                       the only ones a take may subtract from the verdict.
 *
 * A failure recorded while NOT muted is therefore never forgiven by anything,
 * which is what makes the counter survive a window opened after it. A muted
 * one still ACCRUES against the verdict — harness.h's documented behaviour,
 * unchanged — and is removed only when a take actually collects it, so a bare
 * cq_h_mute with no take is still loud. */
static int cq_h_case_failures = 0;
static int cq_h_since_take    = 0;
static int cq_h_provoked      = 0;

int cq_h_streq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return a == b;
    return strcmp(a, b) == 0;
}

/* See cq_h_mute in harness.h: set while a suite is deliberately provoking a
 * failure in order to assert that the failure happens. */
static int cq_h_muted = 0;

void cq_h_mute(int on)
{
    cq_h_muted = on;
}

int cq_h_take_failures(void)
{
    const int n = cq_h_since_take;

    /* Forgive the PROVOKED ones only. cq_h_provoked <= cq_h_case_failures
     * always: every provoked failure incremented the verdict too, and the two
     * are cleared together here and at the head of every case. */
    cq_h_case_failures -= cq_h_provoked;
    cq_h_since_take = 0;
    cq_h_provoked   = 0;
    return n;
}

int cq_h_failures_now(void)
{
    return cq_h_case_failures;
}

void cq_h_fail(const char *file, int line, const char *fmt, ...)
{
    va_list ap;

    cq_h_case_failures++;
    cq_h_since_take++;

    if (cq_h_muted) { cq_h_provoked++; return; }

    /* TAP diagnostics: '#'-prefixed lines are legal anywhere in the stream,
     * so a failure can be reported the moment it happens rather than being
     * buffered until the case ends. */
    printf("# FAIL %s:%d in %s: ", file, line,
           cq_h_case != NULL ? cq_h_case : "(no case)");

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    printf("\n");
}

int cq_h_run(const cq_test_case *cases, size_t n)
{
    size_t failed = 0;
    size_t i;

    printf("TAP version 13\n");
    printf("1..%zu\n", n);

    for (i = 0; i < n; i++) {
        cq_h_case = cases[i].name;

        /* All three, so a window a previous case left half-open cannot make
         * the next case's take subtract failures this one never provoked. */
        cq_h_case_failures = 0;
        cq_h_since_take    = 0;
        cq_h_provoked      = 0;

        cases[i].fn();

        if (cq_h_case_failures == 0) {
            printf("ok %zu - %s\n", i + 1, cases[i].name);
        } else {
            printf("not ok %zu - %s (%d failed check%s)\n",
                   i + 1, cases[i].name, cq_h_case_failures,
                   cq_h_case_failures == 1 ? "" : "s");
            failed++;
        }

        fflush(stdout);
    }

    cq_h_case = NULL;

    if (failed != 0) printf("# %zu of %zu case(s) FAILED\n", failed, n);

    return failed == 0 ? 0 : 1;
}

/* --- Command-line flags. See harness.h on what they cannot reach. -------- */

static int    cq_h_argc;
static char **cq_h_argv;

void cq_h_args(int argc, char **argv)
{
    cq_h_argc = argc;
    cq_h_argv = argv;
}

int cq_h_flag(const char *name)
{
    int i;
    for (i = 1; i < cq_h_argc; i++)
        if (cq_h_streq(cq_h_argv[i], name)) return 1;
    return 0;
}
