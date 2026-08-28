/* tests/support/death.c — see death.h for why this is not WILL_FAIL. */

#include "support/death.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t armed = 0;

/* Only _Exit is async-signal-safe here, and it is all we need: exit status 0
 * means "the abort landed inside an armed window", which is the whole claim. */
static void on_abort(int sig)
{
    (void)sig;
    _Exit(armed ? 0 : 4);
}

void cq_death_arm(void)
{
    fflush(NULL);       /* the handler will not run atexit flushes */
    armed = 1;
}

void cq_death_disarm(void)
{
    armed = 0;
}

void cq_death_skip(const char *why)
{
    fprintf(stderr, "# SKIP (configuration cannot reach this abort): %s\n",
            why ? why : "-");
    fflush(NULL);
    _Exit(0);
}

void cq_death_require(const char *file, int line, const char *expr, int cond)
{
    if (cond) return;
    fprintf(stderr, "%s:%d: precondition failed in a death case: %s\n",
            file, line, expr);
    fflush(NULL);
    _Exit(3);      /* distinct from 1 (survived) and 4 (aborted while disarmed) */
}

void cq_death_survived(const char *file, int line, const char *stmt)
{
    fprintf(stderr, "%s:%d: expected an abort from: %s\n", file, line, stmt);
    fflush(NULL);
    _Exit(1);
}

int cq_death_main(int argc, char **argv, const cq_death_case *cases, size_t n)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\ncases:\n",
                argc > 0 ? argv[0] : "death-test");
        for (size_t i = 0; i < n; i++) fprintf(stderr, "  %s\n", cases[i].name);
        return 2;
    }

    if (signal(SIGABRT, on_abort) == SIG_ERR) {
        fprintf(stderr, "could not install SIGABRT handler\n");
        return 5;
    }

    for (size_t i = 0; i < n; i++) {
        if (strcmp(argv[1], cases[i].name) != 0) continue;

        cases[i].fn();

        /* A case body must end in CQ_EXPECT_ABORT, which never returns.
         * Falling through here means the case forgot to assert anything. */
        fprintf(stderr, "death case '%s' ran no CQ_EXPECT_ABORT\n", argv[1]);
        return 1;
    }

    fprintf(stderr, "unknown death case '%s'\n", argv[1]);
    return 3;
}
