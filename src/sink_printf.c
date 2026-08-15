/* src/sink_printf.c — M23. One line per gate. See sink_printf.h for why the
 * format is what it is; this file is only the six format strings. */

#include "sink_printf.h"

#include "sink.h"

/* NULL means stdout, and it is resolved HERE rather than in cq_sink_printf so
 * a caller that reopens stdout between construction and emission is traced to
 * the new one — which is what a CQ_lang fixture redirecting a run does. */
static FILE *out(void *u)
{
    return u ? (FILE *)u : stdout;
}

/* THE FLUSH IS PART OF THE CONTRACT, not a performance oversight. Every hard
 * error in this library is an abort() (Rule 6), and the gates emitted just
 * before it are the diagnosis; a line still sitting in a stdio buffer when the
 * process dies is a gate that never reached its sink. It also keeps our stream
 * ordered against CQ_lang's own trace, which flushes per line
 * (runtime/cq_runtime.c:450) and will be sharing stdout with us at L6. */
static void line(FILE *f)
{
    fputc('\n', f);
    fflush(f);
}

static void pf_x(void *u, uint32_t q)
{
    FILE *f = out(u);
    fprintf(f, "x(q%u)", q);
    line(f);
}

static void pf_cx(void *u, uint32_t c, uint32_t t)
{
    FILE *f = out(u);
    fprintf(f, "cx(q%u, q%u)", c, t);
    line(f);
}

static void pf_ccx(void *u, uint32_t a, uint32_t b, uint32_t t)
{
    FILE *f = out(u);
    fprintf(f, "ccx(q%u, q%u, q%u)", a, b, t);
    line(f);
}

/* %a, not %f or %g: exact and round-trippable for every double, subnormals
 * included, and the same specifier CQ_lang uses for its own angles. Angles are
 * compared bitwise throughout this project — 0.0 and -0.0 are equal in C and
 * are different gates to emit. */
static void pf_ry(void *u, uint32_t q, double theta)
{
    FILE *f = out(u);
    fprintf(f, "ry(q%u, %a)", q, theta);
    line(f);
}

static void pf_rz(void *u, uint32_t q, double phi)
{
    FILE *f = out(u);
    fprintf(f, "rz(q%u, %a)", q, phi);
    line(f);
}

static void pf_mz(void *u, uint32_t q)
{
    FILE *f = out(u);
    fprintf(f, "mz(q%u)", q);
    line(f);
}

cq_sink cq_sink_printf(FILE *f)
{
    cq_sink s;
    s.x  = pf_x;  s.cx = pf_cx; s.ccx = pf_ccx;
    s.ry = pf_ry; s.rz = pf_rz; s.mz  = pf_mz;
    s.user = f;
    return s;
}

void cq_sink_printf_register(void)
{
    /* Static, because the registry BORROWS a sink and never copies it — it
     * must outlive the registration, and a process-wide default outlives
     * everything. `user` stays NULL so stdout is taken at emit time. */
    static cq_sink s;
    s = cq_sink_printf(NULL);
    cq_sink_register("printf", &s);
}
