/* src/kernels/fround_emit.c — M38b, K25. THE DISPATCH AND THE SURFACE: which
 * gate a slot emits, the public block's entry points, and the five Rule 7
 * kernels. Costs and layout are fround_step.c, operands fround_operand.c, the
 * M33 hand-off fround_fadd.c; the tables are fround_rows.inc.
 *
 * ONE GATE PER SLOT, AND IT IS FORCED (bd ckd.14a): cq_sandwich runs the
 * reverse pass by re-calling compute(env, s) with the SAME index, so a step
 * undoes itself only if it is an involution. Every branch of emit_row emits
 * exactly one X, CX or CCX or delegates to a step block that does.
 *
 * COMPOSITE KERNELS CALL THE STEP FUNCTION, NEVER THE KERNEL: `cq_kernel_eq`,
 * `cq_kernel_slt`, `cq_kernel_sub`, `cq_kernel_mux`, `cq_kernel_shl_var` and
 * `cq_kernel_fadd` are each a whole sandwich, and cq_sandwich refuses nesting
 * in BOTH configurations.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK: every target below is a bit
 * of the caller's region; `dst` is touched only in the copy-out, which the
 * driver runs with the extent deliberately disarmed.
 */

#include "kernels/fround_int.h"

#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"
#include "kernels/shift_var.h"
#include "sandwich.h"

enum { W64 = CQ_FI_W };

typedef struct { cq_bit v[3][CQ_FI_W], t[3][CQ_FI_W]; } fi_bufs;

#define OP(j, s) cq_fi_op64(k, (s), f.v[j], f.t[j])

static void emit_row(cq_ctx *ctx, const cq_fround_block *k,
                     const cq_fround_row *r, uint32_t o, int u)
{
    fi_bufs f;
    int lane;

    switch (r->op) {
    case CQ_FIOP_EQ: {
        cq_eq_block e;

        e.a = OP(0, r->s0); e.b = OP(1, r->s1); e.W = W64;
        e.diff = cq_fi_sp(k, o, (uint32_t)W64);
        e.orr  = cq_fi_sp(k, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        cq_eq_step(ctx, &e, u);
        return; }
    case CQ_FIOP_SLT: {
        cq_slt_block c;

        c.a = OP(0, r->s0); c.b = OP(1, r->s1); c.W = W64;
        c.af    = cq_fi_sp(k, o,                            (uint32_t)W64);
        c.bf    = cq_fi_sp(k, o + (uint32_t)W64,            (uint32_t)W64);
        c.nb    = cq_fi_sp(k, o + (uint32_t)(2 * W64),      (uint32_t)W64);
        c.carry = cq_fi_sp(k, o + (uint32_t)(3 * W64),      (uint32_t)W64 + 1u);
        c.axnb  = cq_fi_sp(k, o + (uint32_t)(4 * W64) + 1u, (uint32_t)W64);
        cq_slt_step(ctx, &c, u);
        return; }
    case CQ_FIOP_SUB: {
        cq_sub_block s;

        s.a = OP(0, r->s0); s.b = OP(1, r->s1); s.W = W64;
        s.nb = cq_fi_sp(k, o,                       (uint32_t)W64);
        s.d  = cq_fi_sp(k, o + (uint32_t)W64,       (uint32_t)W64);
        s.c  = cq_fi_sp(k, o + (uint32_t)(2 * W64), (uint32_t)W64);
        cq_sub_step(ctx, &s, u);
        return; }
    case CQ_FIOP_ADD: {
        cq_add_block a;

        a.a = OP(0, r->s0); a.b = OP(1, r->s1); a.W = W64;
        a.t = cq_fi_sp(k, o,                 (uint32_t)W64);
        a.c = cq_fi_sp(k, o + (uint32_t)W64, (uint32_t)W64);
        cq_add_step(ctx, &a, u);
        return; }
    case CQ_FIOP_MUX: {
        cq_mux_block m;

        m.cond = cq_fi_flag_of(k, r->s0);
        m.t = OP(1, r->s1); m.f = OP(2, r->s2);
        m.r = cq_fi_sp(k, o,                 (uint32_t)W64);
        m.d = cq_fi_sp(k, o + (uint32_t)W64, (uint32_t)W64);
        cq_mux_step(ctx, &m, u);
        return; }
    case CQ_FIOP_BSHL: case CQ_FIOP_BLSHR: {
        cq_barrel_block b;

        b.a = OP(0, r->s0); b.b = OP(1, r->s1);
        b.scr = k->scr; b.off = cq_fi_base(k, o); b.W = W64;
        b.dir = (r->op == CQ_FIOP_BSHL) ? CQ_BARREL_SHL : CQ_BARREL_LSHR;
        cq_barrel_step(ctx, &b, u);
        return; }
    case CQ_FIOP_FADD:
        cq_fi_fadd_step(ctx, OP(0, r->s0), OP(1, r->s1), k->scr, cq_fi_base(k, o), u);
        return;
    case CQ_FIOP_AND:
        /* arith.jl:270's one Toffoli per lane. */
        cq_emit_ccx(ctx, &OP(0, r->s0)[u], &OP(1, r->s1)[u],
                    &cq_fi_sp(k, o, (uint32_t)W64)[u]);
        return;
    /* `lower_or!` emits CNOT(a), CNOT(b), Toffoli(a, b) IN THAT ORDER
     * (arith.jl:276-278). Exchanging s0 and s1 is a recorded equivalent-
     * mutant class (bd a-slot-scan-cannot-see-which-operand-a-slot-reads) —
     * DO NOT "simplify" the line; Rule 1 is about provenance. */
    case CQ_FIOP_OR:
        lane = u / 3;
        if (u % 3 == 0)
            cq_emit_cx(ctx, &OP(0, r->s0)[lane], &cq_fi_sp(k, o, (uint32_t)W64)[lane]);
        else if (u % 3 == 1)
            cq_emit_cx(ctx, &OP(1, r->s1)[lane], &cq_fi_sp(k, o, (uint32_t)W64)[lane]);
        else
            cq_emit_ccx(ctx, &OP(0, r->s0)[lane], &OP(1, r->s1)[lane],
                        &cq_fi_sp(k, o, (uint32_t)W64)[lane]);
        return;
    case CQ_FIOP_XOR:
        /* arith.jl:287-288, in order: CNOT(a), CNOT(b), per lane. `~x` is
         * this against the all-ones constant, so the second CNOT of every
         * lane demotes to an X. */
        lane = u / 2;
        cq_emit_cx(ctx, &OP(u % 2, (u % 2 == 0) ? r->s0 : r->s1)[lane],
                   &cq_fi_sp(k, o, (uint32_t)W64)[lane]);
        return;
    case CQ_FIOP_NOT1:
        /* arith.jl:476, in order: CNOT(w[1], r[1]) then NOT(r[1]). */
        if (u == 0) cq_emit_cx(ctx, cq_fi_flag_of(k, r->s0), cq_fi_sp(k, o, 1u));
        else        cq_emit_x (ctx, cq_fi_sp(k, o, 1u));
        return;
    case CQ_FIOP_AND1:
        cq_emit_ccx(ctx, cq_fi_flag_of(k, r->s0), cq_fi_flag_of(k, r->s1),
                    cq_fi_sp(k, o, 1u));
        return;
    case CQ_FIOP_OR1:
        /* lower_or! at one lane: CNOT(a), CNOT(b), Toffoli(a, b). */
        if (u == 0)      cq_emit_cx(ctx, cq_fi_flag_of(k, r->s0), cq_fi_sp(k, o, 1u));
        else if (u == 1) cq_emit_cx(ctx, cq_fi_flag_of(k, r->s1), cq_fi_sp(k, o, 1u));
        else             cq_emit_ccx(ctx, cq_fi_flag_of(k, r->s0),
                                     cq_fi_flag_of(k, r->s1), cq_fi_sp(k, o, 1u));
        return;
    case CQ_FIOP_VIEW: case CQ_FIOP_N_OP:
    default:
        break;
    }
    cq_kernel_die("fround: the dispatch reached a row with no gate");
}

void cq_fround_step(cq_ctx *ctx, const cq_fround_block *k, int u)
{
    const cq_fround_row *rows;
    const cq_fi_map *m;
    int n, i;

    cq_fi_arm(k);
    if (u < 0 || u >= cq_fround_steps(k->prog))
        cq_kernel_die("fround: step index outside [0, cq_fround_steps())");
    rows = cq_fround_rows(k->prog, &n);
    m = cq_fi_map_get(k->prog);
    i = cq_fi_row_at(k->prog, u);
    if (m->slot[i + 1] == m->slot[i])
        cq_kernel_die("fround: the step dispatch landed on a zero-slot row");
    emit_row(ctx, k, &rows[i], m->bit[i], u - m->slot[i]);
}

/* The last row's span. The guard keeps this from handing back a pointer into a
 * dead stack frame if the table's last row ever became a view: nothing in the
 * library holds a buffer (Rule 13). */
const cq_bit *cq_fround_result(const cq_fround_block *k)
{
    const cq_fround_row *rows;
    int n;

    cq_fi_arm(k);
    rows = cq_fround_rows(k->prog, &n);
    if (rows[n - 1].op != CQ_FIOP_MUX)
        cq_kernel_die("fround: `result` is not a mux's own span");
    return cq_fi_val64(k, n - 1);
}

/* --- The Rule 7 kernels: Bennett-in-the-small over one flat region. ------ */

typedef struct {
    cq_fround_block k;
    cq_bit         *dst;
} fround_env;

static void fround_compute(cq_ctx *ctx, void *env, int s)
{
    cq_fround_step(ctx, &((const fround_env *)env)->k, s);
}

/* The "^=" of Rule 7's contract, and the only place `dst` is written. */
static void fround_copyout(cq_ctx *ctx, void *env, int s)
{
    const fround_env *e = (const fround_env *)env;

    cq_emit_cx(ctx, &cq_fround_result(&e->k)[s], &e->dst[s]);
}

void cq_kernel_fround(cq_ctx *ctx, cq_fround_prog p, cq_bit *dst,
                      const cq_bit *a, int W)
{
    const cq_bit *src[1];
    cq_scratch scr;
    fround_env e;
    int w[1];

    if ((unsigned)p >= (unsigned)CQ_FROUND_N_PROG)
        cq_kernel_die("fround: the kernel names a program outside the five");
    /* PRD-v2 §1 scopes v2 to f64 and every soft_* here is (UInt64), so
     * another width is a fiction rather than an unimplemented case. */
    if (W != CQ_FP64_W)
        cq_kernel_die("fround: W must be 64 — v2 is f64 only and every "
                      "soft_* in fround.jl takes one UInt64");

    /* D7a at the kernel boundary; the D7b leg is vacuous at arity 1, which is
     * why this is `cq_kernel_check_n` and not `cq_kernel_check_dst`. */
    src[0] = a;
    w[0] = W;
    cq_kernel_check_n(dst, W, src, w, 1);

    /* RISK R9, NOT AN OPTIMISATION: pre-materialisation is unconditional and
     * a block has no fold of its own, so without this an all-classical call
     * would draw the whole region from the pool. The value comes from the
     * SAME Julia body the circuit runs (PRD-v2 §7.4), never the host libm. */
    if (cq_bits_all_const(a, W)) {
        uint64_t v = cq_fround_eval(p, cq_fp_pack(a));

        for (int i = 0; i < W; i++)
            if (((v >> (unsigned)i) & UINT64_C(1)) != 0u) cq_emit_x(ctx, &dst[i]);
        return;
    }

    /* ONE CONTIGUOUS REGION, because emit.c's I6(a) check is a pointer RANGE
     * test over cq_bit addresses. */
    cq_scratch_alloc(&scr, cq_fround_region(p));
    e.k.a = a; e.k.scr = &scr; e.k.off = 0u; e.k.prog = p;
    e.dst = dst;
    cq_sandwich(ctx, &scr, fround_compute, cq_fround_steps(p),
                fround_copyout, W, &e);
    cq_scratch_dispose(&scr);
}

void cq_kernel_ftrunc(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W)
{ cq_kernel_fround(ctx, CQ_FROUND_TRUNC, dst, a, W); }

void cq_kernel_ffloor(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W)
{ cq_kernel_fround(ctx, CQ_FROUND_FLOOR, dst, a, W); }

void cq_kernel_fceil(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W)
{ cq_kernel_fround(ctx, CQ_FROUND_CEIL, dst, a, W); }

void cq_kernel_fround_even(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W)
{ cq_kernel_fround(ctx, CQ_FROUND_EVEN, dst, a, W); }

void cq_kernel_fround_away(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W)
{ cq_kernel_fround(ctx, CQ_FROUND_AWAY, dst, a, W); }
