/* src/kernels/fma_emit.c — M39, K20. THE DISPATCH AND THE SURFACE: which gate
 * a slot emits, the public block's entry points, and the three-source kernel.
 * The costs and the layout are in fma_step.c and the operand resolution in
 * fma_operand.c, on the seams fma_int.h records; the row table is in
 * fma_rows.inc.
 *
 * ONE GATE PER SLOT, AND IT IS FORCED (bd ckd.14a, sandwich.h). cq_sandwich
 * runs the reverse pass by re-calling compute(env, s) with the SAME index, so
 * a step undoes itself only if it is an involution. Every branch of emit_row
 * emits exactly one X, CX or CCX, or delegates to a step block that does the
 * same — cq_eq_step, cq_ult_step, cq_slt_step, cq_add_step, cq_sub_step,
 * cq_mux_step, cq_mul_step, cq_barrel_step, cq_fp_class_step, cq_norm52_step,
 * cq_subnorm_step, cq_round_step.
 *
 * COMPOSITE KERNELS CALL THE STEP FUNCTION, NEVER THE KERNEL. `cq_kernel_eq`,
 * `cq_kernel_ult`, `cq_kernel_slt`, `cq_kernel_add`, `cq_kernel_sub`,
 * `cq_kernel_mux`, `cq_kernel_mul` and `cq_kernel_shl_var` are each a whole
 * sandwich, and cq_sandwich refuses nesting in BOTH configurations, so
 * reaching for one from inside this compute half aborts before allocating
 * anything. M12 over M17 is the standing witness and K20 is the eighth
 * consumer of the same rule.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. Every target below is a bit
 * of the caller's region — an inner block's own internals, a `lower_not1!`
 * wire, an `and`/`or`/`xor` output. The rails `a`, `b` and `c`, the views over
 * them and the constant spans reach the emitter only through cq_emit_*'s
 * `const cq_bit *` control parameters, so a source cannot be materialised here
 * by construction. `dst` is touched exclusively in the copy-out, which the
 * driver runs with the extent deliberately disarmed.
 *
 * FOUR `cq_mul_block`s AND TWELVE `cq_barrel_block`s IN ONE REGION AT SIXTEEN
 * OFFSETS, which is the shape K11.md §7.5 says is the ONLY thing that detects
 * a dropped `off`: a single block whose base is lost slides wholesale inside
 * the caller's region, computes the right value, mirrors perfectly and returns
 * every qubit. K20 has sixteen, so they collide — and the case that sees it is
 * a SECOND program in one region, not this file.
 */

#include "kernels/fma_int.h"

#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/fpround.h"
#include "kernels/kernel.h"
#include "kernels/mul.h"
#include "kernels/mux.h"
#include "kernels/shift_var.h"
#include "sandwich.h"

enum { W64 = CQ_FU_W };

static void emit_row(cq_ctx *ctx, const cq_fma_block *k, const uint32_t *off,
                     const cq_fma_row *rows, int i, int u)
{
    const cq_fma_row *r = &rows[i];
    cq_bit v0[CQ_FU_W], v1[CQ_FU_W], v2[CQ_FU_W];
    cq_bit t0[CQ_FU_W], t1[CQ_FU_W], t2[CQ_FU_W];
    uint32_t o = off[i];
    int lane;

    switch (r->op) {
    case CQ_FUOP_CLASS: {
        cq_fp_class_block c;

        c.a = cq_fu_op64(k, off, r->s0, v0, t0);
        c.scr = k->scr; c.off = o; c.cls = (cq_fp_class)r->s1;
        cq_fp_class_step(ctx, &c, u);
        return; }
    case CQ_FUOP_EQ: {
        cq_eq_block e;

        e.a = cq_fu_op64(k, off, r->s0, v0, t0);
        e.b = cq_fu_op64(k, off, r->s1, v1, t1);
        e.diff = cq_fu_sp(k, o, (uint32_t)W64);
        e.orr  = cq_fu_sp(k, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        e.W = W64;
        cq_eq_step(ctx, &e, u);
        return; }
    case CQ_FUOP_ULT: {
        cq_ult_block c;

        c.a = cq_fu_op64(k, off, r->s0, v0, t0);
        c.b = cq_fu_op64(k, off, r->s1, v1, t1);
        c.nb    = cq_fu_sp(k, o,                            (uint32_t)W64);
        c.carry = cq_fu_sp(k, o + (uint32_t)W64,            (uint32_t)W64 + 1u);
        c.axnb  = cq_fu_sp(k, o + (uint32_t)(2 * W64) + 1u, (uint32_t)W64);
        c.W = W64;
        cq_ult_step(ctx, &c, u);
        return; }
    case CQ_FUOP_SLT: {
        cq_slt_block c;

        c.a = cq_fu_op64(k, off, r->s0, v0, t0);
        c.b = cq_fu_op64(k, off, r->s1, v1, t1);
        c.af    = cq_fu_sp(k, o,                            (uint32_t)W64);
        c.bf    = cq_fu_sp(k, o + (uint32_t)W64,            (uint32_t)W64);
        c.nb    = cq_fu_sp(k, o + (uint32_t)(2 * W64),      (uint32_t)W64);
        c.carry = cq_fu_sp(k, o + (uint32_t)(3 * W64),      (uint32_t)W64 + 1u);
        c.axnb  = cq_fu_sp(k, o + (uint32_t)(4 * W64) + 1u, (uint32_t)W64);
        c.W = W64;
        cq_slt_step(ctx, &c, u);
        return; }
    case CQ_FUOP_ADD: {
        cq_add_block a;

        a.a = cq_fu_op64(k, off, r->s0, v0, t0);
        a.b = cq_fu_op64(k, off, r->s1, v1, t1);
        a.t = cq_fu_sp(k, o,                 (uint32_t)W64);
        a.c = cq_fu_sp(k, o + (uint32_t)W64, (uint32_t)W64);
        a.W = W64;
        cq_add_step(ctx, &a, u);
        return; }
    case CQ_FUOP_SUB: {
        cq_sub_block s;

        s.a = cq_fu_op64(k, off, r->s0, v0, t0);
        s.b = cq_fu_op64(k, off, r->s1, v1, t1);
        s.nb = cq_fu_sp(k, o,                       (uint32_t)W64);
        s.d  = cq_fu_sp(k, o + (uint32_t)W64,       (uint32_t)W64);
        s.c  = cq_fu_sp(k, o + (uint32_t)(2 * W64), (uint32_t)W64);
        s.W = W64;
        cq_sub_step(ctx, &s, u);
        return; }
    case CQ_FUOP_MUX: {
        cq_mux_block m;

        m.cond = cq_fu_flag_of(k, off, r->s0);
        m.t    = cq_fu_op64(k, off, r->s1, v0, t0);
        m.f    = cq_fu_op64(k, off, r->s2, v1, t1);
        m.r    = cq_fu_sp(k, o,                 (uint32_t)W64);
        m.d    = cq_fu_sp(k, o + (uint32_t)W64, (uint32_t)W64);
        cq_mux_step(ctx, &m, u);
        return; }
    case CQ_FUOP_MUL: {
        cq_mul_block b;

        b.a = cq_fu_op64(k, off, r->s0, v0, t0);
        b.b = cq_fu_op64(k, off, r->s1, v1, t1);
        b.scr = k->scr; b.off = o; b.W = W64;
        cq_mul_step(ctx, &b, u);
        return; }
    case CQ_FUOP_BSHL: case CQ_FUOP_BLSHR: {
        cq_barrel_block b;

        b.a = cq_fu_op64(k, off, r->s0, v0, t0);   /* the value  */
        b.b = cq_fu_op64(k, off, r->s1, v1, t1);   /* the amount */
        b.scr = k->scr; b.off = o; b.W = W64;
        b.dir = (cq_barrel_dir)cq_fu_barrel_dir_of(r->op);
        cq_barrel_step(ctx, &b, u);
        return; }
    case CQ_FUOP_NORM52: {
        cq_norm52_block b;

        b.m = cq_fu_op64(k, off, r->s0, v0, t0);
        b.e = cq_fu_op64(k, off, r->s1, v1, t1);
        b.scr = k->scr; b.off = o;
        cq_norm52_step(ctx, &b, u);
        return; }
    case CQ_FUOP_SUBNORM: {
        cq_subnorm_block b;

        b.wr          = cq_fu_op64(k, off, r->s0, v0, t0);
        b.result_exp  = cq_fu_op64(k, off, r->s1, v1, t1);
        b.result_sign = cq_fu_op64(k, off, r->s2, v2, t2);
        b.scr = k->scr; b.off = o;
        cq_subnorm_step(ctx, &b, u);
        return; }
    case CQ_FUOP_ROUND: {
        cq_round_block b;

        b.wr          = cq_fu_op64(k, off, r->s0, v0, t0);
        b.result_exp  = cq_fu_op64(k, off, r->s1, v1, t1);
        b.result_sign = cq_fu_op64(k, off, r->s2, v2, t2);
        b.scr = k->scr; b.off = o;
        cq_round_step(ctx, &b, u);
        return; }
    /* THE COMMUTATIVE ROWS CARRY A RECORDED EQUIVALENT-MUTANT CLASS (bd
     * a-slot-scan-cannot-see-which-operand-a-slot-reads, measured on M32).
     * Exchanging `r->s0` and `r->s1` on `and`, `or` or `xor` survives every
     * case — same value, same targets, same tuple, same palindrome — because
     * the operator is commutative and the scan predicts a KIND per slot while
     * advancing its cursor only on a non-NONE, so the fold moves from one slot
     * of a group to another and the cursor sees the same gate either way. The
     * SAME exchange on a NON-commutative row (`sub` at fma.jl:91) is killed by
     * L1. DO NOT "SIMPLIFY" ANY OF THESE LINES TO MATCH THE MUTANT:
     * `lower_or!` emits CNOT(a), CNOT(b), Toffoli(a, b) in that order
     * (arith.jl:276-278) and Rule 1 is about provenance, not only about
     * behaviour. */
    case CQ_FUOP_AND:
        /* arith.jl:270's one Toffoli per lane. */
        lane = u;
        cq_emit_ccx(ctx, &cq_fu_op64(k, off, r->s0, v0, t0)[lane],
                    &cq_fu_op64(k, off, r->s1, v1, t1)[lane],
                    &cq_fu_sp(k, o, (uint32_t)W64)[lane]);
        return;
    case CQ_FUOP_OR:
        lane = u / 3;
        if (u % 3 == 0)
            cq_emit_cx(ctx, &cq_fu_op64(k, off, r->s0, v0, t0)[lane],
                       &cq_fu_sp(k, o, (uint32_t)W64)[lane]);
        else if (u % 3 == 1)
            cq_emit_cx(ctx, &cq_fu_op64(k, off, r->s1, v1, t1)[lane],
                       &cq_fu_sp(k, o, (uint32_t)W64)[lane]);
        else
            cq_emit_ccx(ctx, &cq_fu_op64(k, off, r->s0, v0, t0)[lane],
                        &cq_fu_op64(k, off, r->s1, v1, t1)[lane],
                        &cq_fu_sp(k, o, (uint32_t)W64)[lane]);
        return;
    case CQ_FUOP_XOR:
        /* arith.jl:286-288, in order: CNOT(a[i], r[i]) then CNOT(b[i], r[i]).
         * Both `~x` occurrences reach here with `b` = K_ONES, which the fold
         * table turns into 64 Xs and none into nothing (M37's reading). */
        lane = u / 2;
        cq_emit_cx(ctx,
                   (u % 2 == 0) ? &cq_fu_op64(k, off, r->s0, v0, t0)[lane]
                                : &cq_fu_op64(k, off, r->s1, v1, t1)[lane],
                   &cq_fu_sp(k, o, (uint32_t)W64)[lane]);
        return;
    case CQ_FUOP_NOT1:
        /* arith.jl:476, in order: CNOT(w[1], r[1]) then NOT(r[1]). */
        if (u == 0) cq_emit_cx(ctx, cq_fu_flag_of(k, off, r->s0),
                               cq_fu_sp(k, o, 1u));
        else        cq_emit_x (ctx, cq_fu_sp(k, o, 1u));
        return;
    case CQ_FUOP_AND1:
        cq_emit_ccx(ctx, cq_fu_flag_of(k, off, r->s0),
                    cq_fu_flag_of(k, off, r->s1), cq_fu_sp(k, o, 1u));
        return;
    case CQ_FUOP_OR1:
        if (u == 0)      cq_emit_cx(ctx, cq_fu_flag_of(k, off, r->s0),
                                    cq_fu_sp(k, o, 1u));
        else if (u == 1) cq_emit_cx(ctx, cq_fu_flag_of(k, off, r->s1),
                                    cq_fu_sp(k, o, 1u));
        else             cq_emit_ccx(ctx, cq_fu_flag_of(k, off, r->s0),
                                     cq_fu_flag_of(k, off, r->s1),
                                     cq_fu_sp(k, o, 1u));
        return;
    case CQ_FUOP_VIEW: case CQ_FUOP_OUT: case CQ_FUOP_N_OP:
    default:
        break;
    }
    cq_kernel_die("fma: unknown op in the program");
}

void cq_fma_step(cq_ctx *ctx, const cq_fma_block *k, int u)
{
    uint32_t off[CQ_FU_MAXR];
    const cq_fma_row *rows;
    int i, within, n;

    if (u < 0 || u >= cq_fma_steps())
        cq_kernel_die("fma: step index outside [0, cq_fma_steps())");
    cq_fu_arm(k, off);
    rows = cq_fma_rows(&n);
    /* A BINARY SEARCH OVER THE MEMOISED MAP rather than a walk that re-asks
     * every sibling module what every row costs — 790 module calls per gate at
     * 395 rows, which is what made this suite not finish (fma_int.h). */
    i = cq_fu_row_at(cq_fu_map_get(), u, &within);
    if (cq_fu_row_steps(&rows[i]) <= 0)
        cq_kernel_die("fma: the step dispatch landed on a zero-slot row");
    emit_row(ctx, k, off, rows, i, within);
}

/* The last row — fma.jl:210's `return result`, the outermost `ifelse` of the
 * select chain, whose output is a `mux`'s own span. The guard is what keeps
 * this from handing back a pointer into a dead stack frame if the table's last
 * row ever became a view or a projection: NOTHING in the library holds a
 * circuit or a buffer (Rule 13), so an assembled operand lives only as long as
 * the call that built it. */
const cq_bit *cq_fma_result(const cq_fma_block *k)
{
    uint32_t off[CQ_FU_MAXR];
    cq_bit buf[CQ_FU_W];
    const cq_fma_row *rows;
    const cq_bit *p;
    int n;

    rows = cq_fma_rows(&n);
    cq_fu_arm(k, off);
    if (rows[n - 1].op == CQ_FUOP_VIEW || rows[n - 1].op == CQ_FUOP_OUT)
        cq_kernel_die("fma: `result` is a view or a projection, not a span");
    p = cq_fu_val64(k, off, n - 1, buf);
    if (p == buf) cq_kernel_die("fma: `result` did not resolve to a span");
    return p;
}

/* --- The three-source kernel: Bennett-in-the-small over one flat region. -- */

typedef struct {
    cq_fma_block k;
    cq_bit      *dst;
} fma_env;

static void fma_compute(cq_ctx *ctx, void *env, int s)
{
    cq_fma_step(ctx, &((const fma_env *)env)->k, s);
}

/* The "^=" of Rule 7's contract, and the only place `dst` is written on the
 * sandwich path — which is why the driver runs it with the I6 extent
 * DISARMED. One CX per lane and no X: `result` already holds the value. */
static void fma_copyout(cq_ctx *ctx, void *env, int s)
{
    const fma_env *e = (const fma_env *)env;

    cq_emit_cx(ctx, &cq_fma_result(&e->k)[s], &e->dst[s]);
}

void cq_kernel_fma(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, const cq_bit *b,
                   const cq_bit *c, int W)
{
    cq_scratch scr;
    fma_env e;
    const cq_bit *src[3];
    int w[3];

    /* PRD-v2 §1 scopes v2 to f64 and `soft_fma` is (UInt64, UInt64, UInt64),
     * so another width is a fiction rather than an unimplemented case. */
    if (W != CQ_FP64_W)
        cq_kernel_die("fma: W must be 64 — v2 is f64 only and soft_fma "
                      "takes three UInt64");

    /* THE N-ARY GUARD, NOT THE ARITY-2 WRAPPER. `cq_kernel_check_dst` sizes
     * two overlap ranges and cannot express a third source at all; the N-ary
     * form sizes every range PER OPERAND, which is what Rule 7's note about
     * the mux says in so many words. D7a is a hard error in both
     * configurations and D7b is too, at all three pairs — the defensive copy
     * is the shim's, at the M26 handle boundary, and a kernel that sees an
     * alias is looking at a missing copy. */
    src[0] = a; src[1] = b; src[2] = c;
    w[0] = w[1] = w[2] = W;
    cq_kernel_check_n(dst, W, src, w, 3);

    /* RISK R9, AND IT IS NOT AN OPTIMISATION (plan §0.2 consequence 2, K11.md
     * §7.4). Pre-materialisation is unconditional and a BLOCK has no fold of
     * its own, so without this an all-classical `fma` would draw the whole
     * region from the pool for an operation with no quantum input at all —
     * the largest L5 violation available anywhere in the catalogue. The value
     * comes from the SAME Julia body the circuit runs (PRD-v2 §7.4), never
     * from the host `fma()`. */
    if (cq_bits_all_const(a, W) && cq_bits_all_const(b, W)
        && cq_bits_all_const(c, W)) {
        uint64_t v = cq_fma_eval(cq_fp_pack(a), cq_fp_pack(b), cq_fp_pack(c));

        for (int i = 0; i < W; i++)
            if (((v >> (unsigned)i) & UINT64_C(1)) != 0u) cq_emit_x(ctx, &dst[i]);
        return;
    }

    /* ONE CONTIGUOUS REGION, carved into named sub-arrays, because emit.c's
     * I6(a) check is a pointer RANGE test over cq_bit addresses — a kernel
     * that allocated two regions would put half its targets outside it. */
    cq_scratch_alloc(&scr, cq_fma_region());
    e.k.a = a; e.k.b = b; e.k.c = c; e.k.scr = &scr; e.k.off = 0u;
    e.dst = dst;

    cq_sandwich(ctx, &scr, fma_compute, cq_fma_steps(),
                fma_copyout, W, &e);
    cq_scratch_dispose(&scr);
}
