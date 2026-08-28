/* src/sink_qec.c — M25. See sink_qec.h for D19/D20/D21 and the two pool modes. */

#include "sink_qec.h"

#include "sink.h"
#include "sink_qec_angle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(CQOPS_HAVE_QEC) && CQOPS_HAVE_QEC

#include <qec/qec.h>

/* The house shape, one layer per prefix (compare src/sink.c's "sink:"). */
static void cq_qs_die(const char *what, const char *detail)
{
    fprintf(stderr, "libcqops: FATAL: qec sink: %s (%s)\n", what,
            detail ? detail : "-");
    abort();
}

static qec_ctx *g_qec;          /* NULL until bind; the sink's `user` */
static FILE    *g_trace;
static char    *g_trace_final;  /* the name the .partial is renamed to */
static int      g_precision = 20;
static int      g_atexit_armed;
static int    (*g_header)(FILE *);   /* M26's annotation header (§15 D21 (a)) */

/* ε = 2^−precision is gridsynth's target, and the CONVERSION has a floor of its
 * own: with a denominator capped at CQ_QEC_DENOM_CAP the rational can miss θ by
 * about π/cap ≈ 2.9e-12 rad. Above precision 30 (ε ≈ 9.3e-10, still 320× that
 * floor) the accuracy would be set by our conversion rather than by the
 * synthesis, silently — so it is refused rather than delivered. */
#define CQ_QS_PRECISION_MAX 30

static qec_ctx *qs_ctx(void *u)
{
    if (!u) cq_qs_die("a gate reached the qec sink before it was bound",
                      "call cq_sink_qec_bind after cq_ctx_init");
    return (qec_ctx *)u;
}

/* Every qec_* returns 0 or −1 and our vtable returns void, so this is where the
 * two conventions meet. −1 is the library refusing — a coincident qec_ccx, a
 * bad argument, an internal gadget failure — and it must not be dropped. */
static void qs_ck(int rc, const char *op)
{
    if (rc != 0) cq_qs_die("the QEC library refused a gate", op);
}

/* --- The four 1:1 entries ------------------------------------------------- */

static void qs_x(void *u, uint32_t q)   { qs_ck(qec_x(qs_ctx(u), q), "x"); }
static void qs_mz(void *u, uint32_t q)  { qs_ck(qec_mz(qs_ctx(u), q), "mz"); }

static void qs_cx(void *u, uint32_t c, uint32_t t)
{
    qs_ck(qec_cx(qs_ctx(u), c, t), "cx");
}

static void qs_ccx(void *u, uint32_t a, uint32_t b, uint32_t t)
{
    qs_ck(qec_ccx(qs_ctx(u), a, b, t), "ccx");
}

/* --- The two built ones (PRD §15 D19) ------------------------------------- */

static void qs_rz(void *u, uint32_t q, double phi)
{
    long p, den;
    cq_qec_ratio(phi, CQ_QEC_DENOM_CAP, &p, &den);
    qs_ck(qec_rz(qs_ctx(u), q, p, den, g_precision), "rz");
}

/* Ry(θ) = S·H·Rz(θ)·H·S†, exact to 2.220e-16 and NOT merely up to a global
 * phase — unlike §7's half-turn row. There is no qec_ry; qec_s, qec_h and
 * qec_sdg all exist, and the conjugation is T-FREE (QEC_GATE_T comes only from
 * qec_t, reached only from the gridsynth walk), so an Ry costs exactly its Rz's
 * T-count plus four Clifford gadgets. This is what unblocks §12's Grover, which
 * has no other route: cqrt_h is an over-declaration, so Grover-from-rotations
 * is forced, and a stubbed ry would have made this sink structurally unable to
 * run v1's acceptance gate.
 *
 * THE EMISSION ORDER IS THE REVERSE OF THE MATRIX PRODUCT — a product applies
 * its RIGHTMOST factor first — so the circuit is `sdg; h; rz; h; s`. Emitting
 * `s` first is wrong by 6.858e-01 at θ = π/4, and by a full 2.0 at θ = π. This
 * is §7's own "a matrix product and a circuit read in opposite orders" trap, in
 * a module that has no instrument for a wrong phase, which is why the order is
 * pinned by an ORDERED check on the library's own trace rather than by the
 * identity as written above.
 *
 * A single-qubit Clifford conjugation in a SINK is not a reversible
 * construction for the integer opcode surface, so Rule 1 does not govern it. */
static void qs_ry(void *u, uint32_t q, double theta)
{
    qec_ctx *c = qs_ctx(u);
    qs_ck(qec_sdg(c, q), "ry:sdg");
    qs_ck(qec_h(c, q),   "ry:h");
    qs_rz(u, q, theta);
    qs_ck(qec_h(c, q),   "ry:h");
    qs_ck(qec_s(c, q),   "ry:s");
}

static cq_sink g_sink = { qs_x, qs_cx, qs_ccx, qs_ry, qs_rz, qs_mz, NULL };

/* --- Install and teardown ------------------------------------------------- */

int cq_sink_qec_available(void) { return 1; }

void cq_sink_qec_register(void) { cq_sink_register("qec", &g_sink); }

void *cq_sink_qec_handle(void) { return g_qec; }

static const char *env_nonempty(const char *name)
{
    const char *v = getenv(name);
    return (v && v[0]) ? v : NULL;   /* empty means absent, as CQOPS_SINK does */
}

static void qs_read_precision(void)
{
    const char *v = env_nonempty("CQOPS_QEC_PRECISION");
    if (!v) return;

    char *end;
    long n = strtol(v, &end, 10);
    if (*end != '\0' || n < 0 || n > CQ_QS_PRECISION_MAX)
        cq_qs_die("CQOPS_QEC_PRECISION outside [0, 30]", v);
    g_precision = (int)n;
}

static void qs_open_trace(const char *path)
{
    size_t n = strlen(path);
    char  *partial = malloc(n + 9);
    g_trace_final  = malloc(n + 1);
    if (!partial || !g_trace_final) cq_qs_die("out of memory opening the trace", path);
    memcpy(g_trace_final, path, n + 1);
    memcpy(partial, path, n);
    memcpy(partial + n, ".partial", 9);

    g_trace = fopen(partial, "w");
    if (!g_trace) cq_qs_die("cannot open the trace file", partial);
    free(partial);
    qec_set_trace(g_qec, g_trace);
}

FILE *cq_sink_qec_trace(void) { return g_trace; }

void cq_sink_qec_set_header(int (*fn)(FILE *)) { g_header = fn; }

/* THE HEADER GOES IN FRONT OF THE BODY, WHICH IS A COPY AND NOT A RENAME, and
 * §15 D21 (a) is where the necessity is argued: handoff §3 wants EVERY
 * `#REGISTER` before the FIRST `op begin`, and no runtime that allocates
 * mid-program can satisfy that in one pass. So the body is buffered under the
 * `.partial` name while it is written and the header is prepended once the
 * program has ended.
 *
 * A REFUSAL LEAVES THE `.partial` AND CREATES NOTHING. That is handoff §1's own
 * posture — a trace known to be non-conformant must not be shipped — and the
 * one row that reaches it is "brackets were printed and no register exists",
 * which the viewer would meet as a fatal parse error instead. */
static void qs_compose(const char *partial)
{
    FILE *dst = fopen(g_trace_final, "w");
    FILE *src;
    char  buf[4096];
    size_t n;

    if (!dst) return;                       /* nothing shipped, .partial kept */
    if (!g_header(dst)) { fclose(dst); remove(g_trace_final); return; }

    src = fopen(partial, "r");
    if (src) {
        while ((n = fread(buf, 1, sizeof buf, src)) > 0u) fwrite(buf, 1, n, dst);
        fclose(src);
    }
    fclose(dst);
    remove(partial);
}

void cq_sink_qec_teardown(void)
{
    if (g_qec) { qec_destroy(g_qec); g_qec = NULL; }
    g_sink.user = NULL;

    /* AFTER qec_destroy, which is the header's own instruction: "the caller
     * owns the FILE* and must close it after calling qec_destroy()". */
    if (g_trace) { fclose(g_trace); g_trace = NULL; }
    if (g_trace_final) {
        size_t n = strlen(g_trace_final);
        char  *partial = malloc(n + 9);
        if (partial) {
            memcpy(partial, g_trace_final, n);
            memcpy(partial + n, ".partial", 9);
            /* only a CLEAN exit ships; with no annotations there is nothing to
             * prepend and the original rename is what happens. */
            if (g_header) qs_compose(partial);
            else          rename(partial, g_trace_final);
            free(partial);
        }
        free(g_trace_final);
        g_trace_final = NULL;
    }
    g_header = NULL;
}

void cq_sink_qec_bind(cq_qubit_pool *pool)
{
    if (cq_sink_active() != &g_sink) return;   /* the one flag, and the whole of it */
    if (g_qec) return;                          /* idempotent */

    const char *cfg = env_nonempty("CQOPS_QEC_CONFIG");
    if (!cfg) cq_qs_die("the qec sink needs CQOPS_QEC_CONFIG", NULL);

    qs_read_precision();

    g_qec = qec_create(cfg);
    if (!g_qec) cq_qs_die("qec_create refused the config", cfg);
    g_sink.user = g_qec;

    const char *trace = env_nonempty("CQOPS_QEC_TRACE");
    if (trace) qs_open_trace(trace);

    /* D20 / D21 (b). Both modes, together, and before a single qubit is taken.
     * The ceiling stops us over-committing a fabric that may not exist at a
     * larger n_logical — and it is load-bearing rather than advisory, because
     * MEASURED 2026-08-28 qec_x(ctx, 99) on an n_logical = 3 context returns 0:
     * the library does not range-check a logical index anywhere. What keeps us
     * inside the range is this line plus M03's own `peak == minted`, in BOTH
     * recycling modes. No-recycle is a DISPLAY-model constraint and takes no
     * credit for it; see qubits.h for the tempting second motive that was
     * checked and is false. */
    cq_qubits_set_ceiling(pool, qec_n_logical(g_qec));
    cq_qubits_set_recycle(pool, 0);

    if (!g_atexit_armed) { atexit(cq_sink_qec_teardown); g_atexit_armed = 1; }
}

#else  /* built without the QEC library */

int  cq_sink_qec_available(void) { return 0; }
void cq_sink_qec_register(void)  { }
void cq_sink_qec_teardown(void)  { }
void *cq_sink_qec_handle(void)   { return NULL; }

/* NULL is the annotation layer's ONE activation test (see the header), so this
 * arm makes every `cq_trace_*` call in M26 inert without a build-configuration
 * branch anywhere in the shim — the same posture the register arm above takes. */
FILE *cq_sink_qec_trace(void) { return NULL; }
void cq_sink_qec_set_header(int (*fn)(FILE *)) { (void)fn; }

/* Registering nothing is what makes CQOPS_SINK=qec take M04's existing hard
 * error instead of a silent fallback, so this bind has nothing left to do. */
void cq_sink_qec_bind(cq_qubit_pool *pool) { (void)pool; }

#endif
