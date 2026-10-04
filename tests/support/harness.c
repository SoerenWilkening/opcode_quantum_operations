#include "support/harness.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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

/* --- Sample sharding (bd 5mqp). See harness.h. ------------------------- */

static int cq_h_shard_i = 0;
static int cq_h_shard_k = 1;

int cq_h_shard(int *index)
{
    if (index != NULL) *index = cq_h_shard_i;
    return cq_h_shard_k;
}

int cq_h_shard_owns(long s)
{
    return s >= 0 && s % cq_h_shard_k == cq_h_shard_i;
}

/* `run` NULL means every case, which is cq_h_run's meaning; otherwise only
 * the marked ones, numbered 1..m in the TAP stream so the plan stays honest. */
static int run_cases(const cq_test_case *cases, size_t n,
                     const unsigned char *run)
{
    size_t failed = 0, m = 0, t = 0;
    size_t i;

    for (i = 0; i < n; i++) m += (run == NULL || run[i]) ? 1u : 0u;

    printf("TAP version 13\n");
    printf("1..%zu\n", m);
    if (m != n || cq_h_shard_k != 1)
        printf("# harness: argv selected %zu of %zu case(s), sample shard "
               "%d/%d (bd 5mqp)\n", m, n, cq_h_shard_i, cq_h_shard_k);

    for (i = 0; i < n; i++) {
        if (run != NULL && !run[i]) continue;
        t++;
        cq_h_case = cases[i].name;

        /* All three, so a window a previous case left half-open cannot make
         * the next case's take subtract failures this one never provoked. */
        cq_h_case_failures = 0;
        cq_h_since_take    = 0;
        cq_h_provoked      = 0;

        cases[i].fn();

        if (cq_h_case_failures == 0) {
            printf("ok %zu - %s\n", t, cases[i].name);
        } else {
            printf("not ok %zu - %s (%d failed check%s)\n",
                   t, cases[i].name, cq_h_case_failures,
                   cq_h_case_failures == 1 ? "" : "s");
            failed++;
        }

        fflush(stdout);
    }

    cq_h_case = NULL;

    if (failed != 0) printf("# %zu of %zu case(s) FAILED\n", failed, m);

    return failed == 0 ? 0 : 1;
}

int cq_h_run(const cq_test_case *cases, size_t n)
{
    return run_cases(cases, n, NULL);
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

/* --- Case selection (bd 5mqp). See harness.h for the grammar. ------------ */

static int refuse(const char *why, const char *what)
{
    if (!cq_h_muted)
        printf("# harness: refused: %s%s%s%s — no case ran (bd 5mqp)\n", why,
               what ? " '" : "", what ? what : "", what ? "'" : "");
    return 2;
}

/* "I/K" in full, 0 <= I < K <= 1000; anything else (a sign, a space, a
 * trailing character, an empty side) is malformed. K >= 1 needs no test of
 * its own: I is a digit string, so I >= 0, and I < K is required. */
static int parse_shard(const char *t, int *i, int *k)
{
    char *end;
    long a, b;

    if (*t < '0' || *t > '9') return 0;
    a = strtol(t, &end, 10);
    if (*end != '/' || end[1] < '0' || end[1] > '9') return 0;
    b = strtol(end + 1, &end, 10);
    if (*end != '\0' || b > 1000 || a >= b) return 0;
    *i = (int)a;
    *k = (int)b;
    return 1;
}

int cq_h_select(const cq_test_case *cases, size_t n, int argc, char **argv,
                unsigned char *run)
{
    int mode = 0;                        /* 0 every case, 1 --case, -1 --skip */
    int si = 0, sk = 1, sharded = 0;
    size_t c, m = 0;

    for (c = 0; c < n; c++) run[c] = 1;

    for (int a = 1; a < argc; a++) {
        const char *flag = argv[a], *op;
        int want;
        long hit = -1;

        if (cq_h_streq(flag, "--update-goldens")) continue;
        if (!cq_h_streq(flag, "--case") && !cq_h_streq(flag, "--skip")
            && !cq_h_streq(flag, "--shard"))
            return refuse("unrecognised argument", flag);
        if (a + 1 >= argc) return refuse("no operand after", flag);
        op = argv[++a];

        if (cq_h_streq(flag, "--shard")) {
            if (sharded) return refuse("--shard given twice, second", op);
            if (!parse_shard(op, &si, &sk))
                return refuse("--shard wants I/K, 0 <= I < K <= 1000, got", op);
            sharded = 1;
            continue;
        }

        want = cq_h_streq(flag, "--case") ? 1 : -1;
        if (mode == -want) return refuse("--case and --skip together", NULL);
        for (c = 0; c < n; c++)
            if (cq_h_streq(cases[c].name, op)) hit = (long)c;
        if (hit < 0) return refuse("no case named", op);

        /* The first --case clears the table; --skip starts from all of it. */
        if (mode == 0)
            for (c = 0; c < n; c++) run[c] = (unsigned char)(want < 0);
        mode = want;
        run[hit] = (unsigned char)(want > 0);
    }

    for (c = 0; c < n; c++) m += run[c];
    if (m == 0) return refuse("the selection leaves no case to run", NULL);

    cq_h_shard_i = si;
    cq_h_shard_k = sk;
    return 0;
}

int cq_h_run_argv(const cq_test_case *cases, size_t n, int argc, char **argv)
{
    unsigned char *run = malloc(n != 0 ? n : 1u);
    int rc;

    if (run == NULL) return refuse("out of memory", NULL);
    cq_h_args(argc, argv);
    rc = cq_h_select(cases, n, argc, argv, run);
    if (rc == 0) rc = run_cases(cases, n, run);
    free(run);
    return rc;
}
