/* src/kernels/fcmp_step.c — M36, K18, the STEP MACHINE half of the ORDERED
 * CORE. A SECOND SPLIT, on the seam ROW TABLES <-> STEP MACHINE, taken at
 * implementation because fcmp.c measured 298 of Rule 12's 300 non-blank
 * non-comment lines with both halves in one file — a scheduled split in the
 * sense that it was hit rather than surprised by, but one PRD-v2 §5 did NOT
 * record in advance, so it is named in K18.md §2.0 as D-K18-7.
 *
 * fcmp.c holds the three row tables of fcmp.jl's ordered cores and nothing
 * else; this file turns a row into spans and gates and knows no Julia. The
 * seam is real rather than arithmetic: every cost here is ASKED of another
 * module (cq_fp_class_steps, cq_eq_steps, cq_ult_steps, cq_sub_steps,
 * CQ_MUX_STEPS_PER_BIT) or is upstream's own three-gate bitwise vocabulary,
 * and not one line of it would move if a body table changed.
 *
 * ONE GATE PER SLOT, AND IT IS FORCED (bd ckd.14a, sandwich.h). cq_sandwich
 * runs the reverse pass by re-calling compute(env, s) with the SAME index, so
 * a step undoes itself only if it is an involution. Every branch of emit_row
 * emits exactly one X, CX or CCX, or delegates to a step block that does the
 * same — cq_fp_class_step, cq_eq_step, cq_ult_step, cq_sub_step, cq_mux_step.
 *
 * COMPOSITE KERNELS CALL THE STEP FUNCTION, NEVER THE KERNEL. `cq_kernel_eq`,
 * `cq_kernel_ult` and `cq_kernel_fp_is_nan` are each a whole sandwich, and
 * cq_sandwich refuses nesting in BOTH configurations, so reaching for one from
 * inside this compute half aborts before allocating anything. M12 over M17 is
 * the standing witness; K18 is the fifth consumer of the same rule.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. Every target below is a bit
 * of the caller's region — an inner block's own internals, a `lower_not1!`
 * wire, an `and`/`or` output. The rails `a` and `b`, M31's views over them and
 * the constant spans reach the emitter only through cq_emit_*'s
 * `const cq_bit *` parameters, so a source cannot be materialised here by
 * construction. `dst` is touched exclusively in fcmp_pred.c's copy-out, which
 * the driver runs with the extent deliberately disarmed.
 *
 * THE VIEWS AND CONSTANTS ARE REBUILT PER STEP, ON PURPOSE — fpclass.c's
 * reason, verbatim: a block carrying cached operands would carry state whose
 * initialisation a consumer can forget, and a forgotten bind is a silent wrong
 * circuit rather than a failure.
 *
 * THE PREFIX-OFFSET WALK IS DONE ONCE PER STEP AND NOT ONCE PER LOOKUP. A row
 * reference resolves to a span, and a span needs the sum of every earlier
 * row's region; doing that per operand would make the step O(n^2) in a program
 * of up to 42 rows driven up to 7,253 times per call.
 */

#include "kernels/fcmp.h"

#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"

enum { W64 = CQ_FP64_W };

/* --- Slot and region arithmetic, ASKED of the owning modules. ------------- */

/* `lower_not1!` is CNOT(w, r) then NOT(r) into a FRESH wire — two slots, one
 * bit (arith.jl:474-478). `lower_and!` is one Toffoli per lane, so one slot
 * and no bit of its own at one lane (:268-272). `lower_or!` is CNOT, CNOT,
 * Toffoli per lane, so three slots and one bit (:274-282). Transcribed from
 * the pinned source because they are emitted directly here and no module
 * publishes a cost for them (PRD-v2 §7.10). */
enum { FC_NOT1_STEPS = 2, FC_AND1_STEPS = 1, FC_OR1_STEPS = 3 };

static int row_steps(const cq_fcmp_row *r)
{
    switch (r->op) {
    case CQ_FCOP_CLASS_NAN: return cq_fp_class_steps(CQ_FP_IS_NAN);
    case CQ_FCOP_EQ:        return cq_eq_steps(W64);
    case CQ_FCOP_ULT:       return cq_ult_steps(W64);
    case CQ_FCOP_SUB:       return cq_sub_steps(W64);
    case CQ_FCOP_MUX:       return CQ_MUX_STEPS_PER_BIT;      /* one bit */
    case CQ_FCOP_NOT1:      return FC_NOT1_STEPS;
    case CQ_FCOP_AND1:      return FC_AND1_STEPS;
    case CQ_FCOP_OR1:       return FC_OR1_STEPS;
    default: break;
    }
    cq_kernel_die("fcmp: unknown op in the program");
    return 0;
}

/* The bits each row owns. `cq_eq_block` budgets `2W - 1` (diff ++ orr) and
 * `cq_ult_block` `3W + 1` (nb ++ carry ++ axnb, carry being W+1 and not W) —
 * both stated as contracts in cmp.h; `cq_sub_block` is `3W` (nb ++ d ++ c,
 * add.h) and `cq_mux_block` is `2` at one bit (r ++ d, mux.h). M31 publishes
 * its own as a function. */
static uint32_t row_region(const cq_fcmp_row *r)
{
    switch (r->op) {
    case CQ_FCOP_CLASS_NAN: return cq_fp_class_region(CQ_FP_IS_NAN);
    case CQ_FCOP_EQ:        return (uint32_t)(2 * W64 - 1);
    case CQ_FCOP_ULT:       return (uint32_t)(3 * W64 + 1);
    case CQ_FCOP_SUB:       return (uint32_t)(3 * W64);
    case CQ_FCOP_MUX:       return 2u;
    default:                return 1u;    /* not1, and, or: one output bit */
    }
}

static void check_program(const cq_fcmp_row *rows, int n)
{
    if (rows == NULL || n <= 0 || n > CQ_FCMP_MAX_ROWS)
        cq_kernel_die("fcmp: the program is empty or longer than "
                      "CQ_FCMP_MAX_ROWS");
}

uint32_t cq_fcmp_region(const cq_fcmp_row *rows, int n)
{
    uint32_t bits = 0u;

    check_program(rows, n);
    for (int i = 0; i < n; i++) bits += row_region(&rows[i]);
    return bits;
}

int cq_fcmp_steps(const cq_fcmp_row *rows, int n)
{
    int slots = 0;

    check_program(rows, n);
    for (int i = 0; i < n; i++) slots += row_steps(&rows[i]);
    return slots;
}

/* --- The step machine. --------------------------------------------------- */

/* `at` is ABSOLUTE inside the region: the prefix walk in `offsets` has already
 * added `k->off`, which is the one place this block's base is applied. */
static cq_bit *span(const cq_fcmp_block *k, uint32_t at, uint32_t len)
{
    return cq_scratch_span(k->scr, at, len);
}

/* The region check is the BLOCK's, and it has to be: without it the first
 * out-of-region span aborts in M08 naming the REGION rather than the consumer
 * that mis-sized its offset, and the two-programs-in-one-region shape then has
 * no diagnostic of its own. Hard error in both configurations. */
static void check_region(const cq_fcmp_block *k)
{
    if (k->scr == NULL) cq_kernel_die("fcmp: the block has no region");
    if ((uint64_t)k->off + cq_fcmp_region(k->rows, k->n_rows)
        > (uint64_t)cq_scratch_size(k->scr))
        cq_kernel_die("fcmp: the block's region does not fit at its offset");
}

static void offsets(const cq_fcmp_block *k, uint32_t *off)
{
    uint32_t o = k->off;

    for (int i = 0; i < k->n_rows; i++) {
        off[i] = o;
        o += row_region(&k->rows[i]);
    }
}

/* Row `i`'s answer, as a one-bit span inside the caller's region. Read through
 * the owning module's accessor wherever there is one: `cq_eq_flag` is
 * `orr[W-2]` in general and `diff[0]` at W == 1, and a consumer spelling that
 * rule inline reads `orr[-1]` at the bottom of the ladder (cmp.h). */
static const cq_bit *flag_of(const cq_fcmp_block *k, const uint32_t *off, int i)
{
    const cq_fcmp_row *r;

    /* A 1-BIT OPERAND MUST BE A ROW OF THIS PROGRAM, AND NOTHING BELOW M36
     * KNOWS THAT. `op64` refuses an operand code it does not recognise; this
     * is the mirror refusal for the other direction — a `not1`, `and`, `or` or
     * `mux` handed one of the eight NEGATIVE codes, which would index the row
     * table out of bounds and hand the emitter whatever it read. Release has
     * no other detector at all: the read succeeds, the gate is plausible and
     * the value is wrong. Hard error in both configurations. */
    if (i < 0 || i >= k->n_rows)
        cq_kernel_die("fcmp: a 1-bit operand is not a row of this program");

    r = &k->rows[i];

    if (r->op == CQ_FCOP_CLASS_NAN) {
        cq_fp_class_block c;

        c.a = k->a; c.scr = k->scr; c.off = off[i]; c.cls = CQ_FP_IS_NAN;
        return cq_fp_class_flag(&c);
    }
    if (r->op == CQ_FCOP_EQ) {
        cq_eq_block e;

        e.a = NULL; e.b = NULL; e.W = W64;
        e.diff = span(k, off[i], (uint32_t)W64);
        e.orr  = span(k, off[i] + (uint32_t)W64, (uint32_t)W64 - 1u);
        return cq_eq_flag(&e);
    }
    /* `carry[W]` IS `a >=u b` (cmp.h), and carry starts one vector in. */
    if (r->op == CQ_FCOP_ULT) return span(k, off[i] + (uint32_t)(2 * W64), 1u);
    /* `d` is the difference and lies between `nb` and `c` (add.h). */
    if (r->op == CQ_FCOP_SUB)  return span(k, off[i] + (uint32_t)W64, 1u);
    /* mux `r`, and every bitwise row's single output bit, sit at rel 0. */
    return span(k, off[i], 1u);
}

/* A 64-lane block operand. Every arm but the first two costs ZERO gates and
 * ZERO qubits: PRD-v2 §7.3 as amended makes an AND or a shift by a
 * compile-time constant WIRING, so `a & ABS_MASK` and `a >> 63` are views and
 * the constants are `const cq_bit` spans. A row reference arrives as the
 * Julia type's width — one wire zero-extended to 64 — which is what `UInt64(1)
 * - soft_fcmp_oeq(a, b)` (fcmp.jl:102) subtracts from. */
static const cq_bit *op64(const cq_fcmp_block *k, const uint32_t *off, int s,
                          cq_bit *buf)
{
    switch (s) {
    case CQ_FC_A:      return k->a;
    case CQ_FC_B:      return k->b;
    case CQ_FC_ABS_A:  cq_fp_view(k->a, 0, W64 - 1, buf);  return buf;
    case CQ_FC_ABS_B:  cq_fp_view(k->b, 0, W64 - 1, buf);  return buf;
    case CQ_FC_SIGN_A: cq_fp_view_sign(k->a, buf);         return buf;
    case CQ_FC_SIGN_B: cq_fp_view_sign(k->b, buf);         return buf;
    case CQ_FC_K_ZERO: cq_fp_const_zero(buf);              return buf;
    case CQ_FC_K_ONE:  cq_fp_const(buf, UINT64_C(1));      return buf;
    default: break;
    }
    if (s < 0) cq_kernel_die("fcmp: unknown 64-lane operand code");
    cq_fp_view(flag_of(k, off, s), 0, 1, buf);
    return buf;
}

static void emit_row(cq_ctx *ctx, const cq_fcmp_block *k, const uint32_t *off,
                     int i, int u)
{
    const cq_fcmp_row *r = &k->rows[i];
    cq_bit v0[CQ_FP64_W], v1[CQ_FP64_W];
    uint32_t o = off[i];

    switch (r->op) {
    case CQ_FCOP_CLASS_NAN: {
        cq_fp_class_block c;

        c.a = op64(k, off, r->s0, v0);
        c.scr = k->scr; c.off = o; c.cls = CQ_FP_IS_NAN;
        cq_fp_class_step(ctx, &c, u);
        return; }
    case CQ_FCOP_EQ: {
        cq_eq_block e;

        e.a = op64(k, off, r->s0, v0); e.b = op64(k, off, r->s1, v1);
        e.diff = span(k, o, (uint32_t)W64);
        e.orr  = span(k, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        e.W = W64;
        cq_eq_step(ctx, &e, u);
        return; }
    case CQ_FCOP_ULT: {
        cq_ult_block c;

        c.a = op64(k, off, r->s0, v0); c.b = op64(k, off, r->s1, v1);
        c.nb    = span(k, o,                            (uint32_t)W64);
        c.carry = span(k, o + (uint32_t)W64,            (uint32_t)W64 + 1u);
        c.axnb  = span(k, o + (uint32_t)(2 * W64 + 1),  (uint32_t)W64);
        c.W = W64;
        cq_ult_step(ctx, &c, u);
        return; }
    case CQ_FCOP_SUB: {
        cq_sub_block s;

        s.a = op64(k, off, r->s0, v0); s.b = op64(k, off, r->s1, v1);
        s.nb = span(k, o,                          (uint32_t)W64);
        s.d  = span(k, o + (uint32_t)W64,          (uint32_t)W64);
        s.c  = span(k, o + (uint32_t)(2 * W64),    (uint32_t)W64);
        s.W = W64;
        cq_sub_step(ctx, &s, u);
        return; }
    case CQ_FCOP_MUX: {
        cq_mux_block m;

        m.cond = flag_of(k, off, r->s0);
        m.t    = flag_of(k, off, r->s1);
        m.f    = flag_of(k, off, r->s2);
        m.r    = span(k, o,      1u);
        m.d    = span(k, o + 1u, 1u);
        cq_mux_step(ctx, &m, u);
        return; }
    case CQ_FCOP_NOT1:
        /* arith.jl:476, in order: CNOT(w[1], r[1]) then NOT(r[1]). */
        if (u == 0) cq_emit_cx(ctx, flag_of(k, off, r->s0), span(k, o, 1u));
        else        cq_emit_x (ctx,                         span(k, o, 1u));
        return;
    case CQ_FCOP_AND1:
        /* arith.jl:270's one Toffoli per lane, at one lane. */
        cq_emit_ccx(ctx, flag_of(k, off, r->s0), flag_of(k, off, r->s1),
                    span(k, o, 1u));
        return;
    default:
        /* arith.jl:276-278, in order: CNOT(a), CNOT(b), Toffoli(a, b). */
        if (u == 0)      cq_emit_cx(ctx, flag_of(k, off, r->s0), span(k, o, 1u));
        else if (u == 1) cq_emit_cx(ctx, flag_of(k, off, r->s1), span(k, o, 1u));
        else             cq_emit_ccx(ctx, flag_of(k, off, r->s0),
                                     flag_of(k, off, r->s1), span(k, o, 1u));
        return;
    }
}

void cq_fcmp_step(cq_ctx *ctx, const cq_fcmp_block *k, int u)
{
    uint32_t off[CQ_FCMP_MAX_ROWS];

    check_program(k->rows, k->n_rows);
    if (u < 0 || u >= cq_fcmp_steps(k->rows, k->n_rows))
        cq_kernel_die("fcmp: step index outside [0, cq_fcmp_steps(rows, n))");
    check_region(k);
    offsets(k, off);

    for (int i = 0; i < k->n_rows; i++) {
        int n = row_steps(&k->rows[i]);

        if (u < n) { emit_row(ctx, k, off, i, u); return; }
        u -= n;
    }
    cq_kernel_die("fcmp: the step dispatch fell off the end of the program");
}

const cq_bit *cq_fcmp_flag(const cq_fcmp_block *k)
{
    uint32_t off[CQ_FCMP_MAX_ROWS];

    check_program(k->rows, k->n_rows);
    check_region(k);
    offsets(k, off);
    return flag_of(k, off, k->n_rows - 1);
}
