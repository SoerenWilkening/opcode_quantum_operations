/* src/kernels/ibits_step.c — M41, K26, the STEP MACHINE. Turns a row of
 * ibits.c's programs into spans and one gate; knows no Julia.
 *
 * EVERY COST IS ASKED OF ITS MODULE (cq_add_steps/region, cq_eq_steps/region,
 * cq_mux_steps/region) except `lower_or!`'s, which no module publishes because
 * it is emitted here: CNOT(a), CNOT(b), Toffoli(a, b) per lane, into one
 * fresh W-lane result (arith.jl:274-282) — three slots per lane, W bits.
 *
 * ONE GATE PER SLOT (bd ckd.14a). Every branch of emit_row emits one X, CX
 * or CCX itself or delegates to a step block that does — cq_add_step,
 * cq_eq_step, cq_mux_step — so the driver's index reversal IS the gate
 * reversal.
 *
 * COMPOSITE KERNELS CALL THE STEP FUNCTION, NEVER THE KERNEL. cq_kernel_add /
 * cq_kernel_ne / cq_kernel_mux are whole sandwiches and cq_sandwich refuses
 * nesting in both configurations.
 *
 * I6(a) HOLDS BY CONSTRUCTION. Every target below is a bit of a row's own
 * span inside the caller's region. The rail `a`, its views and the constant
 * spans reach the emitter only through cq_emit_*'s `const cq_bit *` controls.
 *
 * THE VIEWS ARE REBUILT PER STEP, ON PURPOSE (fpclass.c's reason): a block
 * carrying cached operands carries state whose initialisation a consumer can
 * forget. The prefix walk is ONE pass per step that both lays out the offsets
 * of every earlier row and finds the row owning the slot; row costs are asked
 * once per step per op, not once per row (the M39 finding: a flat table whose
 * walk re-asks every sibling per row does not finish).
 */

#include "kernels/ibits.h"

#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"

/* lower_or!: three gates per lane (arith.jl:276-278). */
enum { IB_OR_STEPS_PER_LANE = 3 };

int cq_ibits_row_steps(const cq_ibits_row *r, int W)
{
    switch (r->op) {
    case CQ_IBOP_ADD: return cq_add_steps(W);
    case CQ_IBOP_NE:  return cq_eq_steps(W);
    case CQ_IBOP_MUX: return cq_mux_steps(W);
    case CQ_IBOP_OR:  return IB_OR_STEPS_PER_LANE * W;
    default: break;
    }
    cq_kernel_die("ibits: row_steps of an unknown op");
    return 0;
}

uint32_t cq_ibits_row_region(const cq_ibits_row *r, int W)
{
    switch (r->op) {
    case CQ_IBOP_ADD: return (uint32_t)cq_add_region(W);   /* t ++ c      */
    case CQ_IBOP_NE:  return (uint32_t)cq_eq_region(W);    /* diff ++ orr */
    case CQ_IBOP_MUX: return (uint32_t)cq_mux_region(W);   /* r ++ d      */
    case CQ_IBOP_OR:  return (uint32_t)W;                  /* lower_or! r */
    default: break;
    }
    cq_kernel_die("ibits: row_region of an unknown op");
    return 0u;
}

static void check_program(const cq_ibits_row *rows, int n, int W)
{
    if (rows == NULL || n <= 0 || n > CQ_IBITS_MAX_ROWS)
        cq_kernel_die("ibits: the program is empty or longer than "
                      "CQ_IBITS_MAX_ROWS");
    if (W <= 0 || W > (int)CQ_REG_WIDTH_MAX)
        cq_kernel_die("ibits: block width outside [1, CQ_REG_WIDTH_MAX]");
}

int cq_ibits_steps(const cq_ibits_row *rows, int n, int W)
{
    int slots = 0;

    check_program(rows, n, W);
    for (int i = 0; i < n; i++) slots += cq_ibits_row_steps(&rows[i], W);
    return slots;
}

uint32_t cq_ibits_region(const cq_ibits_row *rows, int n, int W)
{
    uint32_t bits = 0u;

    check_program(rows, n, W);
    for (int i = 0; i < n; i++) bits += cq_ibits_row_region(&rows[i], W);
    return bits;
}

/* The block's own region check, so a mis-sized consumer aborts naming the
 * PROGRAM rather than M08's region. Both configurations. */
static void check_block(const cq_ibits_block *k)
{
    check_program(k->rows, k->n_rows, k->W);
    if (k->scr == NULL) cq_kernel_die("ibits: the block has no region");
    if ((uint64_t)k->off + cq_ibits_region(k->rows, k->n_rows, k->W)
        > (uint64_t)cq_scratch_size(k->scr))
        cq_kernel_die("ibits: the block's region does not fit at its offset");
}

static cq_bit *span(const cq_ibits_block *k, uint32_t at, uint32_t len)
{
    return cq_scratch_span(k->scr, at, len);
}

/* A row's W-lane output. `ne` has none — its answer is one flag. */
static const cq_bit *out_of(const cq_ibits_block *k, const uint32_t *off, int r)
{
    if (k->rows[r].op == CQ_IBOP_NE)
        cq_kernel_die("ibits: a one-bit ne row read as a W-lane operand");
    /* add's `t`, mux's `r` and or's result all sit at rel 0 (add.h, mux.h). */
    return span(k, off[r], (uint32_t)k->W);
}

/* Operand `o` of row `i`, as W lanes. A row reference must name a STRICTLY
 * EARLIER row: anything else reads a span nobody has written yet, or the row's
 * own output as its input. */
static const cq_bit *view(const cq_ibits_block *k, const uint32_t *off, int i,
                          const cq_ibits_opnd *o, cq_bit *buf)
{
    const int W = k->W;
    const cq_bit *base = NULL;

    if (o->shr < 0 || o->shr > W || o->shl < 0 || o->shl > W
        || o->keep < 0 || o->keep > W)
        cq_kernel_die("ibits: a view field is outside [0, W]");

    if (o->src >= 0) {
        if (o->src >= i) cq_kernel_die("ibits: an operand is not an earlier row");
        base = out_of(k, off, o->src);
    } else if (o->src == CQ_IBS_A) {
        base = k->a;
    } else if (o->src != CQ_IBS_K) {
        cq_kernel_die("ibits: unknown operand code");
    }

    if (base != NULL && o->shr == 0 && o->shl == 0 && o->keep == W)
        return base;                        /* the identity view: no copy */

    for (int j = 0; j < W; j++) {
        int m = j - o->shl, s = m + o->shr;

        if (m < 0 || m >= o->keep || s >= W) buf[j] = cq_bit_zero();
        else if (base != NULL)               buf[j] = base[s];
        else buf[j] = cq_bit_const(s < 15 ? (o->k >> s) & 1 : 0);
    }
    return buf;
}

/* A MUX condition: the raw `a != b` flag of an EARLIER `ne` row, read through
 * M16's accessor (orr[W-2], or diff[0] at W == 1). */
static const cq_bit *flag_of(const cq_ibits_block *k, const uint32_t *off,
                             int i, const cq_ibits_opnd *o)
{
    cq_eq_block e;

    if (o->src < 0 || o->src >= i || k->rows[o->src].op != CQ_IBOP_NE)
        cq_kernel_die("ibits: a select condition is not an earlier ne row");
    e.a = NULL; e.b = NULL; e.W = k->W;
    e.diff = span(k, off[o->src], (uint32_t)k->W);
    e.orr  = span(k, off[o->src] + (uint32_t)k->W, (uint32_t)k->W - 1u);
    return cq_eq_flag(&e);
}

static void emit_row(cq_ctx *ctx, const cq_ibits_block *k, const uint32_t *off,
                     int i, int v)
{
    const cq_ibits_row *r = &k->rows[i];
    const uint32_t W = (uint32_t)k->W, o = off[i];
    cq_bit v0[CQ_REG_WIDTH_MAX], v1[CQ_REG_WIDTH_MAX];

    switch (r->op) {
    case CQ_IBOP_ADD: {
        cq_add_block b;

        b.a = view(k, off, i, &r->s[0], v0);
        b.b = view(k, off, i, &r->s[1], v1);
        b.t = span(k, o, W); b.c = span(k, o + W, W); b.W = k->W;
        cq_add_step(ctx, &b, v);
        return; }
    case CQ_IBOP_NE: {
        cq_eq_block e;

        e.a = view(k, off, i, &r->s[0], v0);
        e.b = view(k, off, i, &r->s[1], v1);
        e.diff = span(k, o, W); e.orr = span(k, o + W, W - 1u); e.W = k->W;
        cq_eq_step(ctx, &e, v);
        return; }
    case CQ_IBOP_MUX: {
        cq_mux_block m;

        m.cond = flag_of(k, off, i, &r->s[0]);
        m.t = view(k, off, i, &r->s[1], v0);
        m.f = view(k, off, i, &r->s[2], v1);
        m.r = span(k, o, W); m.d = span(k, o + W, W);
        cq_mux_step(ctx, &m, v);
        return; }
    case CQ_IBOP_OR: {
        const cq_bit *a = view(k, off, i, &r->s[0], v0);
        const cq_bit *b = view(k, off, i, &r->s[1], v1);
        int lane = v / IB_OR_STEPS_PER_LANE;
        cq_bit *t = span(k, o + (uint32_t)lane, 1u);

        /* arith.jl:276-278, in order: CNOT(a), CNOT(b), Toffoli(a, b). */
        switch (v % IB_OR_STEPS_PER_LANE) {
        case 0:  cq_emit_cx (ctx, &a[lane],           t); break;
        case 1:  cq_emit_cx (ctx, &b[lane],           t); break;
        default: cq_emit_ccx(ctx, &a[lane], &b[lane], t); break;
        }
        return; }
    default:
        break;
    }
    cq_kernel_die("ibits: emit of an unknown op");
}

/* Each op's cost at this W, asked ONCE per step rather than once per row. */
static void costs(int W, int *sl, uint32_t *rg)
{
    for (int p = 0; p < CQ_IBOP_N; p++) {
        cq_ibits_row probe;

        probe.op = (short)p;
        sl[p] = cq_ibits_row_steps(&probe, W);
        rg[p] = cq_ibits_row_region(&probe, W);
    }
}

static int op_of(const cq_ibits_row *r)
{
    if (r->op < 0 || r->op >= CQ_IBOP_N)
        cq_kernel_die("ibits: a row with an unknown op");
    return r->op;
}

void cq_ibits_step(cq_ctx *ctx, const cq_ibits_block *k, int u)
{
    uint32_t off[CQ_IBITS_MAX_ROWS], rg[CQ_IBOP_N], bits = 0u, o;
    int sl[CQ_IBOP_N], steps = 0;

    check_program(k->rows, k->n_rows, k->W);
    costs(k->W, sl, rg);
    for (int i = 0; i < k->n_rows; i++) {
        steps += sl[op_of(&k->rows[i])];
        bits  += rg[op_of(&k->rows[i])];
    }
    /* check_block's two refusals, over the tallies just taken, so a step
     * walks the table with lookups only. */
    if (k->scr == NULL) cq_kernel_die("ibits: the block has no region");
    if ((uint64_t)k->off + bits > (uint64_t)cq_scratch_size(k->scr))
        cq_kernel_die("ibits: the block's region does not fit at its offset");
    if (u < 0 || u >= steps)
        cq_kernel_die("ibits: step index outside [0, cq_ibits_steps(...))");

    o = k->off;                             /* the ONE place `off` is added */
    for (int i = 0; i < k->n_rows; i++) {
        int op = op_of(&k->rows[i]);

        off[i] = o;
        if (u < sl[op]) { emit_row(ctx, k, off, i, u); return; }
        u -= sl[op];
        o += rg[op];
    }
    cq_kernel_die("ibits: the step dispatch fell off the end of the program");
}

const cq_bit *cq_ibits_result(const cq_ibits_block *k)
{
    uint32_t o;
    int last = k->n_rows - 1;

    check_block(k);
    if (k->rows[last].op != CQ_IBOP_ADD)
        cq_kernel_die("ibits: the program's last row is not its closing add");
    o = k->off;
    for (int i = 0; i < last; i++) o += cq_ibits_row_region(&k->rows[i], k->W);
    return span(k, o, (uint32_t)k->W);
}
