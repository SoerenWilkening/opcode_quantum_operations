/* shim/cq_shim_trace.c — see cq_shim_trace.h for the division of labour, for
 * the three D21 collisions this file implements the resolutions of, and for why
 * the annotation is not a sink. */

#include "cq_shim_trace.h"

#include "cq_shim_ctx.h"

#include "bit.h"
#include "reg.h"
#include "sink_qec.h"

#include <stdio.h>
#include <stdlib.h>

/* The house shape, one layer per prefix. The string says `trace:` and not
 * `shim:` deliberately: the `FAIL_REGULAR_EXPRESSION` pins in
 * tests/CMakeLists.txt discriminate on the MESSAGE, and every other file under
 * shim/ says `shim:`. */
static void cq_trace_die(const char *what, int32_t h)
{
    fprintf(stderr, "libcqops: FATAL: trace: %s (h%d)\n", what, h);
    abort();
}

/* One row per handle. `q` is allocated at the rail's width on first use and
 * `n` is how much of it is live, so a partially-materialised rail costs one
 * allocation and no bookkeeping. */
typedef struct { uint32_t w, n; uint32_t *q; } cq_trace_reg;

static cq_trace_reg *g_reg;
static int32_t       g_cap;
static int           g_depth;
static int32_t       g_frame[5];
static uint32_t      g_ops;

int      cq_trace_open(void) { return g_depth; }
uint32_t cq_trace_ops (void) { return g_ops;   }

static FILE *trace_stream(void) { return cq_sink_qec_trace(); }

/* --- the register map ----------------------------------------------------- */

static void trace_grow(int32_t h)
{
    int32_t want = h + 1, i;
    cq_trace_reg *p;

    if (want <= g_cap) return;
    p = (cq_trace_reg *)realloc(g_reg, (size_t)want * sizeof *p);
    if (!p) cq_trace_die("out of memory growing the register map", h);
    for (i = g_cap; i < want; i++) { p[i].w = 0u; p[i].n = 0u; p[i].q = NULL; }
    g_reg = p;
    g_cap = want;
}

/* THE SNAPSHOT REPLACES, IT DOES NOT UNION, AND THAT IS WHAT MAKES `cqrt_cswap`
 * CORRECT RATHER THAN FATAL. A union would be the obvious reading of D21 (a)'s
 * "the set only grows" — and it is right for every row but one. The classical
 * ONE row of `cqrt_cswap` exchanges two rails' BIT ARRAYS for zero gates
 * (PRD §2.1), so rail `a`'s indices become rail `b`'s; a union would then have
 * both handles claiming both sets, which is an OVERLAPPING `#REGISTER` and a
 * fatal parse error rather than a merely wrong picture. Replacing at every
 * touch makes the recorded set follow the bits, exactly as `cq_rec_swap` makes
 * the birth value follow them, with no swap case to remember.
 *
 * IT IS STILL MONOTONE EVERYWHERE ELSE, which is what the header needs: D6
 * never demotes, so a rail's index set only ever grows, and it grows only
 * inside an operation that NAMES the rail — a kernel's scratch is M08's, not a
 * rail's, and M06's shared ancilla belongs to no register at all. So snapshotting
 * the named handles is complete without sweeping the table.
 *
 * A DEAD HANDLE IS SKIPPED, NOT REFUSED: `cqrt_free` tombstones its operand
 * inside its own bracket, and the set taken at the OPEN is the final one. */
static void trace_snapshot(int32_t h)
{
    cq_ctx *ctx;
    const cq_bit *b;
    cq_trace_reg *e;
    uint32_t i, w;

    if (h == CQ_REG_NONE) return;
    ctx = cq_shim_ctx();
    if (!cq_reg_is_live(&ctx->regs, h)) return;

    w = cq_reg_width(&ctx->regs, h);
    b = cq_reg_cbits(&ctx->regs, h);

    trace_grow(h);
    e = &g_reg[h];
    if (!e->q) {
        e->q = (uint32_t *)malloc((size_t)w * sizeof *e->q);
        if (!e->q) cq_trace_die("out of memory recording a rail's lanes", h);
    }
    e->w = w;
    e->n = 0u;
    for (i = 0u; i < w; i++)
        if (cq_bit_is_qubit(b[i])) e->q[e->n++] = cq_bit_qindex(b[i]);
}

static void trace_snapshot_frame(void)
{
    int i;
    for (i = 0; i < 5; i++) trace_snapshot(g_frame[i]);
}

/* --- the header (D21 (a), assembled at end of program) -------------------- */

/* `name=h<N>` IS CQ_lang'S OWN HANDLE SPELLING and costs nothing to make unique:
 * handles are monotonic and never reused (D5), which is exactly the static
 * identity handoff §3 presupposes. `type=i<W>` carries the ABI width, and that
 * is the honest token rather than a bare `int`: the viewer synthesizes the
 * display width from the MEMBER COUNT, so a 32-bit rail with three materialised
 * lanes renders `h5 : i32⟨3⟩` — the declared width and what lazy allocation
 * actually took, both visible, instead of a confident `int⟨3⟩`.
 *
 * A RAIL WITH NO QUBITS GETS NO LINE. By I4 an all-constant rail owns zero
 * qubits, `qubits=` may not be empty (§3), and it is not a quantum register in
 * the first place — so the viewer showing no rail for it IS L5 made visible.
 *
 * THE ORDER OF THE LINES IS RAIL ORDER IN THE VIEWER and the order WITHIN a
 * list is lane order (`a[0]` = first listed), so both are taken from the
 * program's own numbering: handles ascending, and bits LSB-first inside each. */
static int trace_header(FILE *dst)
{
    int32_t  h;
    uint32_t i, printed = 0u;

    for (h = 0; h < g_cap; h++) {
        const cq_trace_reg *e = &g_reg[h];
        if (e->n == 0u) continue;
        fprintf(dst, "#REGISTER name=h%d type=i%u qubits=", (int)h, e->w);
        for (i = 0u; i < e->n; i++)
            fprintf(dst, "%s%u", i ? "," : "", e->q[i]);
        fputc('\n', dst);
        printed++;
    }

    /* THE ONE REFUSAL, AND IT IS THE PACKAGE RULE RATHER THAN A COUNT. Handoff
     * §6: the two annotation kinds are a package, and an op bracket with zero
     * `#REGISTER` lines is a FATAL parse error. That row is reachable — a
     * program whose every rail stays classical emits brackets and no register —
     * and §1's posture is that a trace known to be non-conformant must not be
     * shipped. Such a trace is also empty of gates, because a gate needs a
     * materialised bit and a materialised bit is a register, so nothing of value
     * is withheld. */
    if (g_ops > 0u && printed == 0u) {
        fprintf(stderr,
                "libcqops: qec trace: %u operation bracket(s) and no #REGISTER "
                "line — every rail stayed classical, so the annotated trace "
                "would be a fatal parse error for the viewer and is NOT "
                "shipped; the body is left in the .partial file\n", g_ops);
        return 0;
    }
    return 1;
}

void cq_trace_bind(void)
{
    if (trace_stream()) cq_sink_qec_set_header(trace_header);
}

void cq_trace_reset(void)
{
    int32_t h;
    for (h = 0; h < g_cap; h++) free(g_reg[h].q);
    free(g_reg);
    g_reg   = NULL;
    g_cap   = 0;
    g_depth = 0;
    g_ops   = 0u;
}

/* --- the brackets --------------------------------------------------------- */

/* Handoff §4's payload form is exactly `(k=v, k=v)`: no space after `(` or
 * before `)`, exactly one comma and one space between pairs, and no other
 * interior whitespace. The leading `, ` therefore belongs to the KEY and is
 * written only when the key survives — which is also how the key is omitted
 * entirely, since a present-but-empty `in=` is a fatal parse error. */
static void trace_list(FILE *f, const char *key, const int32_t *h, int n)
{
    int i, first = 1;

    for (i = 0; i < n; i++) {
        if (h[i] == CQ_REG_NONE) continue;
        if (first) { fprintf(f, ", %s=", key); first = 0; }
        else       fputc('|', f);
        fprintf(f, "h%d", (int)h[i]);
    }
}

void cq_trace_op(const char *name, int32_t i0, int32_t i1, int32_t i2,
                 int32_t o0, int32_t o1)
{
    FILE *f = trace_stream();
    if (!f) return;

    /* (c). M06 supports nesting and is tested for it; the CONTRACT does not,
     * and the viewer's parser fails loudly on a nested `op begin`. Refusing
     * here names the entry point that opened the second one while the stack is
     * still standing, which the parser cannot do. */
    if (g_depth) cq_trace_die("a second operation bracket opened inside one "
                              "still open: handoff §4 forbids nesting and the "
                              "viewer fails loudly on it", i0);
    g_depth = 1;
    g_ops++;

    g_frame[0] = i0; g_frame[1] = i1; g_frame[2] = i2;
    g_frame[3] = o0; g_frame[4] = o1;
    /* THE OPEN-SIDE SNAPSHOT IS A MEASURED EQUIVALENT MUTANT TODAY, and it is
     * recorded rather than deleted. Removing it leaves the whole suite green in
     * both configurations, because no entry point in the shim both MATERIALISES
     * a rail and destroys it inside one bracket: `cqrt_free`'s operand keeps
     * whatever lanes the last op that named it recorded at its CLOSE, and no
     * other symbol tombstones a handle CQ_lang can see. The PAIRED mutation is
     * what establishes that this is an equivalence and not an untested line —
     * removing the CLOSE-side call instead is killed by every case, which
     * locates the work in the close.
     *
     * IT STAYS BECAUSE OF WHAT IT MAKES UNNECESSARY TO KNOW. With both ends,
     * "the recorded set is the rail's final set" is true of the BRACKET; with
     * the close alone it is true only given an unstated invariant over which
     * entry points free, and the first symbol that breaks it produces a
     * confident wrong picture — a rail rendered with fewer lanes than it had —
     * which is precisely the failure mode D21 (b) rejected recycling to avoid.
     * Do not "simplify" it away to match the survivor. */
    trace_snapshot_frame();

    fprintf(f, "# STAGE: op begin (name=%s", name);
    trace_list(f, "in",  &g_frame[0], 3);
    trace_list(f, "out", &g_frame[3], 2);
    fputs(")\n", f);
}

void cq_trace_op_tpl(const char *base, int is_unc, int32_t ctrl,
                     int32_t a, int32_t b, int32_t out)
{
    char nm[48];

    if (!trace_stream()) return;
    snprintf(nm, sizeof nm, "%s%s%s", base, is_unc ? "_unc" : "",
             ctrl != CQ_REG_NONE ? "_ctrl" : "");
    cq_trace_op(nm, a, b, ctrl, out, CQ_REG_NONE);
}

void cq_trace_end(void)
{
    FILE *f = trace_stream();
    if (!f) return;

    if (!g_depth) cq_trace_die("an operation bracket was closed that was never "
                               "opened", CQ_REG_NONE);
    trace_snapshot_frame();
    g_depth = 0;
    fputs("# STAGE: op end\n", f);
}
