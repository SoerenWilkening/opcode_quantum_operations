/* Trace gate backend: no state, each operation prints one record of the qec-trace-viewer input
 * contract (qec-trace-viewer/docs/TRACE_CONTRACT.md). Written at exit to the file named by the
 * environment variable CQ_TRACE, else stdout.
 *
 *   x, cnot, ccx   "# STAGE: logical X|CX|CCX begin (...)" / "... end"   (contract 4.1, 4.3)
 *   ry, rz, m      gate lines "RY q", "RZ q", "MZ q"                     (contract 3.2)
 *   ry__ctl        "CRY c t"
 *
 * With libcq_backend.a's operation notes (cq_gates.h) the gates of one ABI call are one
 * "# STAGE: op begin (name=add, in=h1|h2, out=h3)" box and every rail that held a qubit is a
 * "#REGISTER name=h<handle> type=i<width>" (contract 3.9). Gates outside any noted operation are
 * an op named "other". The header needs every register before the first op, so the trace is
 * held in a temporary file until exit.
 *
 * A wire of the trace is not a qubit index: the pool hands an index out again, and the contract
 * allows a wire one register. A qubit a rail gave up gets a new wire at its next gate.
 *
 * shortcut: the contract has no two-operand gate but CX and no angle field, so the viewer drops
 * "CRY c t" and no angle is written; upgrade when the contract gains a logical rotation label.
 * shortcut: m returns 0 (there is no state), so a program that branches on a measurement traces
 * the 0 branch only.
 * shortcut: the trace is written by atexit, so a program that aborts leaves none; write it from
 * a SIGABRT handler if a trace of a failing run is ever needed. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cq_gates.h"

typedef struct { uint32_t h, bit, width; } wire;   /* its rail (0: none, an ancilla) and bit there */
typedef struct { uint32_t wire1, mark; } qubit;    /* wire + 1 (0: none yet), scratch of cq_gates_rail */

static FILE    *body;                /* every finished op */
static wire    *wires;
static uint32_t nwires, capwires, stamp;
static qubit   *qubits;
static size_t   capq;
static char    *op;                  /* the open op's lines */
static size_t   noplen, capop;
static char     opname[64];
static uint32_t in[6], out[6];
static int      nin, nout, open;

static void die(const char *msg) {
    fprintf(stderr, "cq_trace: %s\n", msg);
    abort();
}

static int by_rail(const void *a, const void *b) {
    const wire *x = &wires[*(const uint32_t *)a], *y = &wires[*(const uint32_t *)b];
    if (!x->h != !y->h) return !x->h - !y->h;                  /* rails first, ancillas after */
    if (x->h != y->h) return x->h < y->h ? -1 : 1;
    if (x->bit != y->bit) return x->bit < y->bit ? -1 : 1;
    return (x > y) - (x < y);
}

static void flush_op(void) {
    if (noplen) {
        fprintf(body, "# STAGE: op begin (name=%s", opname);
        for (int i = 0; i < nin; i++) fprintf(body, "%sh%u", i ? "|" : ", in=", in[i]);
        for (int i = 0; i < nout; i++) fprintf(body, "%sh%u", i ? "|" : ", out=", out[i]);
        fputs(")\n", body);
        fwrite(op, 1, noplen, body);
        fputs("# STAGE: op end\n", body);
    }
    noplen = 0; nin = nout = open = 0;
}

static void finish(void) {
    flush_op();
    const char *path = getenv("CQ_TRACE");
    FILE *f = path ? fopen(path, "w") : stdout;
    uint32_t *ord = malloc((nwires + 1) * sizeof *ord), nreg = 0;
    if (!f || !ord) { fprintf(stderr, "cq_trace: cannot write the trace\n"); return; }
    for (uint32_t i = 0; i < nwires; i++) ord[i] = i;
    qsort(ord, nwires, sizeof *ord, by_rail);
    fputs("#TRACE v1\n", f);
    for (uint32_t i = 0; i < nwires; i++)   /* lanes are drawn in this order */
        fprintf(f, "#PATCH logical=%u d=0 data=%u-%u ancilla=%u-%u\n", ord[i], ord[i], ord[i], ord[i] + 1, ord[i]);
    for (uint32_t i = 0; i < nwires && wires[ord[i]].h; i++) {
        const wire *w = &wires[ord[i]];
        if (!i || wires[ord[i - 1]].h != w->h) { fprintf(f, "%s#REGISTER name=h%u type=i%u qubits=", i ? "\n" : "", w->h, w->width); nreg++; }
        else fputc(',', f);
        fprintf(f, "%u", ord[i]);
    }
    if (nreg) fputc('\n', f);
    rewind(body);
    for (char line[256]; fgets(line, sizeof line, body); )
        if (nreg || strncmp(line, "# STAGE: op ", 12)) fputs(line, f);   /* the viewer refuses an op without a register */
    if (f != stdout) fclose(f); else fflush(f);
}

/* The wire qubit q is on now; a new one if it has none. */
static uint32_t wire_of(uint32_t q) {
    if (!body) {
        if (!(body = tmpfile())) die("cannot create a temporary file");
        atexit(finish);
    }
    if ((size_t)q >= capq) {
        size_t n = capq ? capq : 64;
        while (n <= q) n *= 2;
        if (!(qubits = realloc(qubits, n * sizeof *qubits))) die("out of memory");
        memset(qubits + capq, 0, (n - capq) * sizeof *qubits);
        capq = n;
    }
    if (!qubits[q].wire1) {
        if (nwires == capwires) {
            capwires = capwires ? 2 * capwires : 64;
            if (!(wires = realloc(wires, capwires * sizeof *wires))) die("out of memory");
        }
        wires[nwires] = (wire){ 0, 0, 0 };
        qubits[q].wire1 = ++nwires;
    }
    return qubits[q].wire1 - 1;
}

/* One record of the open op; a gate outside any noted operation opens "other". */
static void put(const char *fmt, ...) {
    if (!open) { strcpy(opname, "other"); open = 1; }
    for (;;) {
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(op + noplen, capop - noplen, fmt, ap);
        va_end(ap);
        if (n < 0) die("cannot format a record");
        if ((size_t)n < capop - noplen) { noplen += (size_t)n; return; }
        capop = 2 * capop + (size_t)n + 1;
        if (!(op = realloc(op, capop))) die("out of memory");
    }
}

int cq_gates_op_begin(const char *name) {
    flush_op();
    snprintf(opname, sizeof opname, "%s", name);
    open = 1;
    return 1;
}

void cq_gates_op_end(void) { flush_op(); }

void cq_gates_rail(uint32_t h, uint32_t width, const uint32_t *q) {
    int has = 0;
    stamp++;
    for (uint32_t i = 0; i < width; i++) {
        if (q[i] == CQ_GATES_NO_QUBIT) continue;
        uint32_t w = wire_of(q[i]);
        if (wires[w].h && wires[w].h != h) { qubits[q[i]].wire1 = 0; w = wire_of(q[i]); }   /* a release went unnoted */
        wires[w] = (wire){ h, i, width };
        qubits[q[i]].mark = stamp;
        has = 1;
    }
    for (size_t p = 0; p < capq; p++)   /* the qubits h gave up: a new wire at their next gate */
        if (qubits[p].wire1 && wires[qubits[p].wire1 - 1].h == h && qubits[p].mark != stamp) qubits[p].wire1 = 0;
    if (!has) return;
    for (int i = 0; i < nin; i++) if (in[i] == h) return;
    for (int i = 0; i < nout; i++) if (out[i] == h) return;
    if (!noplen) in[nin++] = h; else out[nout++] = h;   /* held qubits before the op's first gate: an input */
}

void __quantum__qis__x__body(uint32_t q) {
    put("# STAGE: logical X begin (q=%u)\n# STAGE: logical X end\n", wire_of(q));
}
void __quantum__qis__cnot__body(uint32_t c, uint32_t t) {
    put("# STAGE: logical CX begin (control=%u, target=%u)\n# STAGE: logical CX end\n", wire_of(c), wire_of(t));
}
void __quantum__qis__ccx__body(uint32_t a, uint32_t b, uint32_t t) {
    put("# STAGE: logical CCX begin (control=%u, control2=%u, target=%u)\n# STAGE: logical CCX end\n",
        wire_of(a), wire_of(b), wire_of(t));
}
void __quantum__qis__ry__body(double th, uint32_t q) { (void)th; put("RY %u\n", wire_of(q)); }
void __quantum__qis__ry__ctl(double th, uint32_t c, uint32_t t) { (void)th; put("CRY %u %u\n", wire_of(c), wire_of(t)); }
void __quantum__qis__rz__body(double th, uint32_t q) { (void)th; put("RZ %u\n", wire_of(q)); }
int __quantum__qis__m__body(uint32_t q) { put("MZ %u\n", wire_of(q)); return 0; }
