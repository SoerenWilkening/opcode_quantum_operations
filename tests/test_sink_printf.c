/* Step 9's first half: M23 prints one line per gate, in the documented format.
 *
 * The format is a CONVENTION borrowed from CQ_lang (lowercase op, parenthesised
 * comma-space operands, `%a` angles, newline, flush) carrying CONTENT that is
 * ours: gate names spelled like the vtable entries, operands `q<N>` because a
 * sink sees qubit indices and never handles. src/sink_printf.h has the full
 * argument; what is pinned here is that the bytes come out that way.
 *
 * Two things are asserted that a "does it print something" test would miss,
 * and each is here because exactly one deletion in sink_printf.c makes it go
 * red and nothing else catches it:
 *
 *   - ANGLES ARE EXACT. `%a` round-trips every double through strtod; `%f`
 *     and `%g` do not. Angles are compared bitwise everywhere else in this
 *     project (mock_sink.h), and a trace that cannot tell 1e-300 from 0 would
 *     be no trace.
 *   - THE FLUSH IS REAL. A second FILE* opened on the same file sees the line
 *     only if it was flushed. Buffering is not a performance detail here: the
 *     library abort()s on every hard error (Rule 6), and the gates emitted
 *     just before the abort are the diagnosis. A buffer discarded on abort() is
 *     the "gate never reached its sink" failure with extra steps.
 *
 * POSIX (mkstemp, fdopen, dup, dup2, fileno, unlink) is used deliberately, on
 * the precedent test_sink.c set for setenv: PRD §14's "no dependencies beyond
 * libc" bars LINKING a third-party library, which is why the harness is
 * hand-rolled — and these are libc functions, not a dependency. The library
 * itself stays inside C11: it calls getenv, fprintf and fflush and nothing
 * else. The precedent is a real one but it is not free, so the two cases that
 * take it say at their own site what they are buying with it; the stdout swap
 * in particular is fenced, because the harness writes TAP to the stream it
 * borrows.
 */

#include "sink.h"            /* the six dispatch helpers, and the registry */
#include "sink_printf.h"
#include "support/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CAP 4096
static char text[CAP];

/* Runs `emit` against a printf sink pointed at a fresh tmpfile, and leaves the
 * bytes it wrote in `text` as a NUL-terminated string. */
static const char *capture(void (*emit)(const cq_sink *))
{
    FILE *f = tmpfile();
    if (!f) {
        cq_h_fail(__FILE__, __LINE__, "tmpfile() failed");
        text[0] = '\0';
        return text;
    }

    cq_sink s = cq_sink_printf(f);
    emit(&s);

    rewind(f);
    size_t n = fread(text, 1, CAP - 1, f);
    text[n] = '\0';
    fclose(f);
    return text;
}

static void all_six(const cq_sink *s)
{
    /* Every operand distinct, so a swapped pair cannot survive the comparison. */
    cq_sink_x  (s, 3u);
    cq_sink_cx (s, 1u, 2u);
    cq_sink_ccx(s, 4u, 5u, 6u);
    cq_sink_ry (s, 7u, 0.5);
    cq_sink_rz (s, 8u, -0.25);
    cq_sink_mz (s, 9u);
}

CQ_TEST(every_entry_prints_its_own_line_in_the_documented_format)
{
    CHECK_STR_EQ(capture(all_six),
                 "x(q3)\n"
                 "cx(q1, q2)\n"
                 "ccx(q4, q5, q6)\n"
                 "ry(q7, 0x1p-1)\n"
                 "rz(q8, -0x1p-2)\n"
                 "mz(q9)\n");
}

/* -------------------------------------------------------------------------
 * Angles.
 * ------------------------------------------------------------------------- */

static const double g_angles[] = {
    0.0, -0.0, 0.5, 3.141592653589793, 1e-300, -1e300,
    1.7976931348623157e308,            /* DBL_MAX          */
    5e-324,                            /* min subnormal    */
};
#define N_ANGLES (sizeof g_angles / sizeof g_angles[0])

static void emit_angles(const cq_sink *s)
{
    for (size_t i = 0; i < N_ANGLES; i++)
        cq_sink_ry(s, (uint32_t)i, g_angles[i]);
}

CQ_TEST(angles_round_trip_bit_exactly_through_the_trace)
{
    const char *p = capture(emit_angles);

    for (size_t i = 0; i < N_ANGLES; i++) {
        const char *comma = strstr(p, ", ");
        if (!comma) {
            cq_h_fail(__FILE__, __LINE__, "line %zu has no angle field", i);
            return;
        }

        char  *end = NULL;
        double got = strtod(comma + 2, &end);

        /* memcmp, not ==: 0.0 and -0.0 compare equal in C and are different
         * gates to emit. This is the assertion %f and %g fail. */
        if (memcmp(&got, &g_angles[i], sizeof got) != 0)
            cq_h_fail(__FILE__, __LINE__,
                      "angle %zu came back %.17g, emitted %.17g",
                      i, got, g_angles[i]);

        p = strchr(comma, '\n');
        if (!p) {
            cq_h_fail(__FILE__, __LINE__, "line %zu is not newline-terminated", i);
            return;
        }
        p++;
    }
}

CQ_TEST(positive_and_negative_zero_are_different_lines)
{
    /* The half of the above that survives even if strtod is generous. */
    const char *p = capture(emit_angles);
    const char *nl = strchr(p, '\n');
    CHECK(nl != NULL);
    if (!nl) return;

    char first[64];
    size_t n = (size_t)(nl - p);
    CHECK(n < sizeof first);
    if (n >= sizeof first) return;
    memcpy(first, p, n);
    first[n] = '\0';

    const char *second = nl + 1;
    CHECK(strncmp(first, second, n) != 0);   /* ry(q0, +0) vs ry(q1, -0) */
}

/* -------------------------------------------------------------------------
 * The stream: the flush, and the stdout default.
 * ------------------------------------------------------------------------- */

CQ_TEST(each_line_is_flushed_as_it_is_emitted)
{
    char path[] = "/tmp/cqops_printf_XXXXXX";
    int  fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0) return;

    FILE *w = fdopen(fd, "w");
    FILE *r = fopen(path, "r");
    CHECK(w != NULL && r != NULL);
    if (!w || !r) { if (w) fclose(w); if (r) fclose(r); unlink(path); return; }

    cq_sink s = cq_sink_printf(w);
    cq_sink_x(&s, 12u);

    /* A SEPARATE FILE* on the same file. Without the flush inside the sink,
     * the bytes are still sitting in `w`'s buffer and this reads nothing. */
    char got[64] = { 0 };
    CHECK(fgets(got, (int)sizeof got, r) != NULL);
    CHECK_STR_EQ(got, "x(q12)\n");

    fclose(w);
    fclose(r);
    unlink(path);
}

CQ_TEST(a_null_stream_means_stdout_resolved_at_emit_time)
{
    /* PRD §8's default sink writes where CQ_lang's fixtures expect it. Taking
     * stdout at EMIT time rather than at construction is what lets a fixture
     * redirect the run and still be traced — which is exactly what this dup2
     * does.
     *
     * NOTHING THAT CAN WRITE TO STDOUT MAY RUN INSIDE THE SWAP. The harness
     * emits TAP on stdout and a failing CHECK calls cq_h_fail, so a CHECK
     * between the two dup2 calls would land its diagnostic in the capture file
     * and corrupt the very bytes under test. The window below therefore holds
     * exactly three statements and no assertion; every CHECK is deferred until
     * stdout is back. */
    fflush(stdout);

    int saved = dup(fileno(stdout));
    FILE *tmp = tmpfile();
    if (saved < 0 || !tmp) {
        cq_h_fail(__FILE__, __LINE__, "could not set up the stdout capture");
        if (saved >= 0) close(saved);
        if (tmp) fclose(tmp);
        return;
    }

    cq_sink s = cq_sink_printf(NULL);        /* constructed BEFORE the swap */

    /* --- window: no CHECK, no harness output --- */
    int redirected = dup2(fileno(tmp), fileno(stdout));
    if (redirected >= 0) {
        cq_sink_cx(&s, 1u, 4u);
        fflush(stdout);
    }
    int restored = dup2(saved, fileno(stdout));
    /* --- window closed --- */

    close(saved);
    CHECK(redirected >= 0);
    CHECK(restored >= 0);

    if (redirected >= 0 && restored >= 0) {
        rewind(tmp);
        char got[64] = { 0 };
        CHECK(fgets(got, (int)sizeof got, tmp) != NULL);
        CHECK_STR_EQ(got, "cx(q1, q4)\n");
    }
    fclose(tmp);
}

/* -------------------------------------------------------------------------
 * Registration.
 * ------------------------------------------------------------------------- */

CQ_TEST(registering_makes_printf_the_resolvable_default)
{
    /* PRD §8: the default is chosen by environment variable so CQ_lang's
     * fixtures need no changes. Installation stays an explicit act — see
     * sink_printf.h, and test_sink_death.c's no_sink_registered_at_all, which
     * pins that an unresolved name is a hard error rather than a fallback. */
    cq_sink_reset();
    unsetenv("CQOPS_SINK");
    CHECK(cq_sink_by_name("printf") == NULL);

    cq_sink_printf_register();
    CHECK(cq_sink_by_name("printf") != NULL);
    CHECK(cq_sink_active() == cq_sink_by_name("printf"));

    cq_sink_printf_register();               /* idempotent */
    CHECK(cq_sink_by_name("printf") != NULL);

    cq_sink_reset();
}

CQ_TEST_MAIN(
    CQ_CASE(every_entry_prints_its_own_line_in_the_documented_format),
    CQ_CASE(angles_round_trip_bit_exactly_through_the_trace),
    CQ_CASE(positive_and_negative_zero_are_different_lines),
    CQ_CASE(each_line_is_flushed_as_it_is_emitted),
    CQ_CASE(a_null_stream_means_stdout_resolved_at_emit_time),
    CQ_CASE(registering_makes_printf_the_resolvable_default)
)
