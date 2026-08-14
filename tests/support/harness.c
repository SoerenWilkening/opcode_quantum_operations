#include "support/harness.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The case currently running, so a failure can name it without every CHECK
 * having to carry it. Single-threaded by construction: the harness runs cases
 * one at a time, and libcqops has no threading. */
static const char *cq_h_case = NULL;
static int cq_h_case_failures = 0;

int cq_h_streq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return a == b;
    return strcmp(a, b) == 0;
}

void cq_h_fail(const char *file, int line, const char *fmt, ...)
{
    va_list ap;

    cq_h_case_failures++;

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
        cq_h_case_failures = 0;

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
