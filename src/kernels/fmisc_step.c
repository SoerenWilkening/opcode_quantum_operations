/* src/kernels/fmisc_step.c — M38, K24, the STEP MACHINE of `fmin` / `fmax`:
 * slot and region arithmetic, operand resolution, the one-gate dispatch and
 * the block's entry points. The row tables are fmisc_rows.c and the two Rule
 * 7 kernels fmisc_minmax.c, on the seams fmisc_int.h records.
 *
 * EVERY COST IS ASKED OF ITS MODULE (cq_fp_class_steps, cq_eq_steps,
 * cq_fcmp_steps, cq_mux_steps and their `_region`s) or is upstream's own
 * one-lane vocabulary — `lower_not1!` two slots and one bit
 * (arith.jl:474-478), `lower_and!` one slot and one bit (:268-272) — which no
 * module publishes because it is emitted inline (PRD-v2 §7.10).
 *
 * ONE GATE PER SLOT, AND IT IS FORCED (bd ckd.14a, sandwich.h). Every branch
 * of emit_row emits one X, CX or CCX or delegates to a step block that does:
 * cq_fp_class_step, cq_eq_step, cq_fcmp_step, cq_mux_step. COMPOSITE KERNELS
 * CALL THE STEP FUNCTION, NEVER THE KERNEL — cq_sandwich refuses nesting in
 * both configurations, so `cq_kernel_fcmp_olt` here would abort.
 *
 * I6(a) HOLDS BY CONSTRUCTION. Every target is a bit of the caller's region;
 * the rails, the views and the constant spans reach the emitter only through
 * cq_emit_*'s `const cq_bit *` controls. `dst` is touched only by
 * fmisc_minmax.c's copy-out, which the driver runs with the extent disarmed.
 *
 * VIEWS AND CONSTANTS ARE REBUILT PER STEP, fpclass.c's reason verbatim: a
 * block carrying cached operands carries state whose initialisation a
 * consumer can forget.
 */

#include "kernels/fmisc_int.h"

#include "emit.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"

enum { W64 = CQ_FP64_W };

#define FX_ABS_MASK   UINT64_C(0x7FFFFFFFFFFFFFFF)   /* fmin.jl:87-88 */
#define FX_SIGN_MASK  UINT64_C(0x8000000000000000)   /* fmin.jl:98    */
#define FX_CANON_NAN  UINT64_C(0x7FF8000000000000)   /* fmin.jl:105   */

int cq_fx_olt(cq_fcmp_row *rows)
{
    return cq_fcmp_program(CQ_FCMP_OLT, rows);
}

int cq_fx_row_steps(const cq_fx_row *r)
{
    cq_fcmp_row olt[CQ_FCMP_MAX_ROWS];

    switch (r->op) {
    case CQ_FXOP_CLASS_NAN: return cq_fp_class_steps(CQ_FP_IS_NAN);
    case CQ_FXOP_EQ:        return cq_eq_steps(W64);
    case CQ_FXOP_NOT1:      return 2;
    case CQ_FXOP_AND1:      return 1;
    case CQ_FXOP_FCMP_OLT:  return cq_fcmp_steps(olt, cq_fx_olt(olt));
    case CQ_FXOP_MUX:       return cq_mux_steps(W64);
    default: break;
    }
    cq_kernel_die("fmisc: row steps of an unknown op");
    return 0;
}

uint32_t cq_fx_row_region(const cq_fx_row *r)
{
    cq_fcmp_row olt[CQ_FCMP_MAX_ROWS];

    switch (r->op) {
    case CQ_FXOP_CLASS_NAN: return cq_fp_class_region(CQ_FP_IS_NAN);
    case CQ_FXOP_EQ:        return (uint32_t)cq_eq_region(W64);
    case CQ_FXOP_NOT1:      return 1u;
    case CQ_FXOP_AND1:      return 1u;
    case CQ_FXOP_FCMP_OLT:  return cq_fcmp_region(olt, cq_fx_olt(olt));
    case CQ_FXOP_MUX:       return (uint32_t)cq_mux_region(W64);
    default: break;
    }
    cq_kernel_die("fmisc: row region of an unknown op");
    return 0u;
}

uint32_t cq_fmisc_region(cq_fmisc_prog p)
{
    int n = 0;
    const cq_fx_row *rows = cq_fmisc_rows(p, &n);
    uint32_t bits = 0u;

    for (int i = 0; i < n; i++) bits += cq_fx_row_region(&rows[i]);
    return bits;
}

int cq_fmisc_steps(cq_fmisc_prog p)
{
    int n = 0, slots = 0;
    const cq_fx_row *rows = cq_fmisc_rows(p, &n);

    for (int i = 0; i < n; i++) slots += cq_fx_row_steps(&rows[i]);
    return slots;
}

/* --- The block's premises and its layout. -------------------------------- */

void cq_fx_arm(const cq_fmisc_block *k)
{
    (void)cq_fmisc_rows(k->prog, NULL);        /* refuses an unknown program */
    if (k->scr == NULL) cq_kernel_die("fmisc: the block has no region");
    if (k->a == NULL || k->b == NULL)
        cq_kernel_die("fmisc: the block has no operand");
    if ((uint64_t)k->off + cq_fmisc_region(k->prog)
        > (uint64_t)cq_scratch_size(k->scr))
        cq_kernel_die("fmisc: the block's region does not fit at its offset");
}

void cq_fx_offsets(const cq_fmisc_block *k, uint32_t *off)
{
    int n = 0;
    const cq_fx_row *rows = cq_fmisc_rows(k->prog, &n);
    uint32_t o = k->off;

    for (int i = 0; i < n; i++) { off[i] = o; o += cq_fx_row_region(&rows[i]); }
}

static cq_bit *span(const cq_fmisc_block *k, uint32_t at, uint32_t len)
{
    return cq_scratch_span(k->scr, at, len);
}

/* --- Operand resolution. ------------------------------------------------- */

const cq_bit *cq_fx_rail(const cq_fmisc_block *k, int s)
{
    if (s == CQ_FX_A) return k->a;
    if (s == CQ_FX_B) return k->b;
    cq_kernel_die("fmisc: a rail operand that is not a rail");
    return NULL;
}

/* THE MIRROR OF op64's REFUSAL, AND THE ONE THAT GETS FORGOTTEN (bd a-table-
 * driven-kernel-needs-both-operand-refusals): a 1-bit operand handed a
 * negative code would index the row table out of bounds and hand the emitter
 * whatever it read; a MUX row handed as a condition would read lane 0 of a
 * 64-lane value as a flag. Hard errors in both configurations. */
const cq_bit *cq_fx_flag_of(const cq_fmisc_block *k, const uint32_t *off, int i)
{
    int n = 0;
    const cq_fx_row *rows = cq_fmisc_rows(k->prog, &n);
    const cq_fx_row *r;

    if (i < 0 || i >= n)
        cq_kernel_die("fmisc: a one-bit operand is not a row of this program");
    r = &rows[i];
    switch (r->op) {
    case CQ_FXOP_CLASS_NAN: {
        cq_fp_class_block c;

        c.a = cq_fx_rail(k, r->s0); c.scr = k->scr; c.off = off[i];
        c.cls = CQ_FP_IS_NAN;
        return cq_fp_class_flag(&c); }
    case CQ_FXOP_EQ: {
        cq_eq_block e;

        e.a = NULL; e.b = NULL; e.W = W64;
        e.diff = span(k, off[i], (uint32_t)W64);
        e.orr  = span(k, off[i] + (uint32_t)W64, (uint32_t)W64 - 1u);
        return cq_eq_flag(&e); }                   /* the raw `a != b` wire */
    case CQ_FXOP_FCMP_OLT: {
        cq_fcmp_row olt[CQ_FCMP_MAX_ROWS];
        cq_fcmp_block f;

        f.a = cq_fx_rail(k, r->s0); f.b = cq_fx_rail(k, r->s1);
        f.scr = k->scr; f.off = off[i]; f.rows = olt; f.n_rows = cq_fx_olt(olt);
        return cq_fcmp_flag(&f); }
    case CQ_FXOP_NOT1: case CQ_FXOP_AND1:
        return span(k, off[i], 1u);
    default: break;
    }
    cq_kernel_die("fmisc: a one-bit operand names a 64-lane row");
    return NULL;
}

/* A 64-lane operand. Every arm but the rails and a MUX row costs ZERO gates
 * and ZERO qubits: an `&` by a compile-time constant is a VIEW keeping the
 * lanes the mask leaves at ONE and CQ_BIT_ZERO elsewhere, IN PLACE (§7.3),
 * and the constants are `const cq_bit` spans. */
static void view_mask(const cq_bit *a, uint64_t mask, cq_bit *out)
{
    for (int i = 0; i < W64; i++)
        out[i] = ((mask >> (unsigned)i) & 1u) ? a[i] : cq_bit_zero();
}

const cq_bit *cq_fx_op64(const cq_fmisc_block *k, const uint32_t *off, int s,
                         cq_bit *buf)
{
    int n = 0;
    const cq_fx_row *rows = cq_fmisc_rows(k->prog, &n);

    switch (s) {
    case CQ_FX_A:      return k->a;
    case CQ_FX_B:      return k->b;
    case CQ_FX_ABS_A:  view_mask(k->a, FX_ABS_MASK, buf);  return buf;
    case CQ_FX_ABS_B:  view_mask(k->b, FX_ABS_MASK, buf);  return buf;
    case CQ_FX_SGN_A:  view_mask(k->a, FX_SIGN_MASK, buf); return buf;
    case CQ_FX_K_ZERO: cq_fp_const(buf, UINT64_C(0));      return buf;
    case CQ_FX_K_QNAN: cq_fp_const(buf, FX_CANON_NAN);     return buf;
    default: break;
    }
    if (s < 0) cq_kernel_die("fmisc: unknown 64-lane operand code");
    if (s >= n) cq_kernel_die("fmisc: an operand names a row outside the program");
    if (rows[s].op == CQ_FXOP_MUX) return span(k, off[s], (uint32_t)W64);
    cq_fp_view(cq_fx_flag_of(k, off, s), 0, 1, buf);      /* zext to UInt64 */
    return buf;
}

/* --- The dispatch. -------------------------------------------------------- */

static void emit_row(cq_ctx *ctx, const cq_fmisc_block *k, const uint32_t *off,
                     const cq_fx_row *r, int i, int u)
{
    cq_bit v0[CQ_FP64_W], v1[CQ_FP64_W];
    uint32_t o = off[i];

    switch (r->op) {
    case CQ_FXOP_CLASS_NAN: {
        cq_fp_class_block c;

        c.a = cq_fx_rail(k, r->s0); c.scr = k->scr; c.off = o;
        c.cls = CQ_FP_IS_NAN;
        cq_fp_class_step(ctx, &c, u);
        return; }
    case CQ_FXOP_EQ: {
        cq_eq_block e;

        e.a = cq_fx_op64(k, off, r->s0, v0); e.b = cq_fx_op64(k, off, r->s1, v1);
        e.diff = span(k, o, (uint32_t)W64);
        e.orr  = span(k, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        e.W = W64;
        cq_eq_step(ctx, &e, u);
        return; }
    case CQ_FXOP_FCMP_OLT: {
        cq_fcmp_row olt[CQ_FCMP_MAX_ROWS];
        cq_fcmp_block f;

        f.a = cq_fx_rail(k, r->s0); f.b = cq_fx_rail(k, r->s1);
        f.scr = k->scr; f.off = o; f.rows = olt; f.n_rows = cq_fx_olt(olt);
        cq_fcmp_step(ctx, &f, u);
        return; }
    case CQ_FXOP_MUX: {
        cq_mux_block m;

        m.cond = cq_fx_flag_of(k, off, r->s0);
        m.t = cq_fx_op64(k, off, r->s1, v0);
        m.f = cq_fx_op64(k, off, r->s2, v1);
        m.r = span(k, o, (uint32_t)W64);
        m.d = span(k, o + (uint32_t)W64, (uint32_t)W64);
        cq_mux_step(ctx, &m, u);
        return; }
    case CQ_FXOP_NOT1:
        /* arith.jl:476, in order: CNOT(w[1], r[1]) then NOT(r[1]). */
        if (u == 0) cq_emit_cx(ctx, cq_fx_flag_of(k, off, r->s0), span(k, o, 1u));
        else        cq_emit_x (ctx, span(k, o, 1u));
        return;
    case CQ_FXOP_AND1:
        /* arith.jl:270's one Toffoli per lane, at one lane. */
        cq_emit_ccx(ctx, cq_fx_flag_of(k, off, r->s0),
                    cq_fx_flag_of(k, off, r->s1), span(k, o, 1u));
        return;
    default: break;
    }
    cq_kernel_die("fmisc: row steps of an unknown op");
}

void cq_fmisc_step(cq_ctx *ctx, const cq_fmisc_block *k, int u)
{
    uint32_t off[CQ_FX_MAX_ROWS];
    int n = 0;
    const cq_fx_row *rows;

    cq_fx_arm(k);
    if (u < 0 || u >= cq_fmisc_steps(k->prog))
        cq_kernel_die("fmisc: step index outside [0, cq_fmisc_steps(prog))");
    rows = cq_fmisc_rows(k->prog, &n);
    cq_fx_offsets(k, off);

    for (int i = 0; i < n; i++) {
        int s = cq_fx_row_steps(&rows[i]);

        if (u < s) { emit_row(ctx, k, off, &rows[i], i, u); return; }
        u -= s;
    }
    cq_kernel_die("fmisc: the step dispatch fell off the end of the program");
}

const cq_bit *cq_fmisc_result(const cq_fmisc_block *k)
{
    uint32_t off[CQ_FX_MAX_ROWS];
    int n = 0;

    cq_fx_arm(k);
    (void)cq_fmisc_rows(k->prog, &n);
    cq_fx_offsets(k, off);
    return span(k, off[n - 1], (uint32_t)W64);     /* row 16's `r` */
}
