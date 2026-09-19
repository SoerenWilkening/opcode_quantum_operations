/* src/kernels/fdiv_emit.c — M35, K17. THE DISPATCH AND THE SURFACE: which gate
 * a slot emits, the public block's entry points, and the Rule 7 kernel. The
 * costs and the layout are in fdiv_step.c and the operand resolution in
 * fdiv_operand.c, on the seams fdiv_int.h records; the row tables are in
 * fdiv.c.
 *
 * ONE GATE PER SLOT, AND IT IS FORCED (bd ckd.14a, sandwich.h). cq_sandwich
 * runs the reverse pass by re-calling compute(env, s) with the SAME index, so
 * a step undoes itself only if it is an involution. Every branch of emit_row
 * emits exactly one X, CX or CCX, or delegates to a step block that does the
 * same — cq_fp_class_step, cq_eq_step, cq_ult_step, cq_add_step, cq_sub_step,
 * cq_mux_step, cq_norm52_step, cq_clz_step, cq_subnorm_step, cq_round_step.
 *
 * COMPOSITE KERNELS CALL THE STEP FUNCTION, NEVER THE KERNEL. `cq_kernel_eq`,
 * `cq_kernel_ult`, `cq_kernel_add`, `cq_kernel_sub` and `cq_kernel_mux` are
 * each a whole sandwich, and cq_sandwich refuses nesting in BOTH
 * configurations, so reaching for one from inside this compute half aborts
 * before allocating anything. M12 over M17 is the standing witness.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. Every target below is a bit
 * of the caller's region — an inner block's own internals, a `lower_not1!`
 * wire, an `or`/`xor` output. The rails `a` and `b`, the views over them and
 * the constant spans reach the emitter only through cq_emit_*'s
 * `const cq_bit *` parameters, so a source cannot be materialised here by
 * construction. `dst` is touched exclusively in the copy-out, which the driver
 * runs with the extent deliberately disarmed.
 *
 * 56 INSTANCES OF ONE FOUR-BLOCK GROUP IN ONE REGION, which is K11.md §7.5's
 * shape at a scale no other kernel reaches: a single block whose base is lost
 * slides wholesale inside the caller's region, computes the right value,
 * mirrors perfectly and returns every qubit. Here the 56 iterations would
 * collide with each other, and the case that sees it is the span-disjointness
 * scan over EVERY iteration's spans — not this file.
 */

#include "kernels/fdiv_int.h"

#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/fpround.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"
#include "sandwich.h"

enum { W64 = CQ_FD_W };

static void emit_row(cq_ctx *ctx, const cq_fdiv_block *k, const cq_fd_map *m,
                     const cq_fdiv_row *r, uint32_t o, int u)
{
    cq_bit v0[CQ_FD_W], v1[CQ_FD_W], v2[CQ_FD_W];
    cq_bit t0[CQ_FD_W], t1[CQ_FD_W], t2[CQ_FD_W];
    int lane;

    switch (r->op) {
    case CQ_FDOP_CLASS: {
        cq_fp_class_block c;

        c.a = cq_fd_op64(k, m, r->s0, v0, t0);
        c.scr = k->scr; c.off = o; c.cls = (cq_fp_class)r->s1;
        cq_fp_class_step(ctx, &c, u);
        return; }
    case CQ_FDOP_EQ: {
        cq_eq_block e;

        e.a = cq_fd_op64(k, m, r->s0, v0, t0);
        e.b = cq_fd_op64(k, m, r->s1, v1, t1);
        e.diff = cq_fd_sp(k, o, (uint32_t)W64);
        e.orr  = cq_fd_sp(k, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        e.W = W64;
        cq_eq_step(ctx, &e, u);
        return; }
    case CQ_FDOP_ULT: {
        cq_ult_block c;

        c.a = cq_fd_op64(k, m, r->s0, v0, t0);
        c.b = cq_fd_op64(k, m, r->s1, v1, t1);
        c.nb    = cq_fd_sp(k, o,                            (uint32_t)W64);
        c.carry = cq_fd_sp(k, o + (uint32_t)W64,            (uint32_t)W64 + 1u);
        c.axnb  = cq_fd_sp(k, o + (uint32_t)(2 * W64) + 1u, (uint32_t)W64);
        c.W = W64;
        cq_ult_step(ctx, &c, u);
        return; }
    case CQ_FDOP_ADD: {
        cq_add_block a;

        a.a = cq_fd_op64(k, m, r->s0, v0, t0);
        a.b = cq_fd_op64(k, m, r->s1, v1, t1);
        a.t = cq_fd_sp(k, o,                 (uint32_t)W64);
        a.c = cq_fd_sp(k, o + (uint32_t)W64, (uint32_t)W64);
        a.W = W64;
        cq_add_step(ctx, &a, u);
        return; }
    case CQ_FDOP_SUB: {
        cq_sub_block s;

        s.a = cq_fd_op64(k, m, r->s0, v0, t0);
        s.b = cq_fd_op64(k, m, r->s1, v1, t1);
        s.nb = cq_fd_sp(k, o,                       (uint32_t)W64);
        s.d  = cq_fd_sp(k, o + (uint32_t)W64,       (uint32_t)W64);
        s.c  = cq_fd_sp(k, o + (uint32_t)(2 * W64), (uint32_t)W64);
        s.W = W64;
        cq_sub_step(ctx, &s, u);
        return; }
    case CQ_FDOP_MUX: {
        cq_mux_block x;

        x.cond = cq_fd_flag_of(k, m, r->s0);
        x.t    = cq_fd_op64(k, m, r->s1, v0, t0);
        x.f    = cq_fd_op64(k, m, r->s2, v1, t1);
        x.r    = cq_fd_sp(k, o,                 (uint32_t)W64);
        x.d    = cq_fd_sp(k, o + (uint32_t)W64, (uint32_t)W64);
        cq_mux_step(ctx, &x, u);
        return; }
    case CQ_FDOP_NORM52: {
        cq_norm52_block b;

        b.m = cq_fd_op64(k, m, r->s0, v0, t0);
        b.e = cq_fd_op64(k, m, r->s1, v1, t1);
        b.scr = k->scr; b.off = o;
        cq_norm52_step(ctx, &b, u);
        return; }
    case CQ_FDOP_CLZ: {
        cq_clz_block b;

        b.wr         = cq_fd_op64(k, m, r->s0, v0, t0);
        b.result_exp = cq_fd_op64(k, m, r->s1, v1, t1);
        b.scr = k->scr; b.off = o;
        cq_clz_step(ctx, &b, u);
        return; }
    case CQ_FDOP_SUBNORM: {
        cq_subnorm_block b;

        b.wr          = cq_fd_op64(k, m, r->s0, v0, t0);
        b.result_exp  = cq_fd_op64(k, m, r->s1, v1, t1);
        b.result_sign = cq_fd_op64(k, m, r->s2, v2, t2);
        b.scr = k->scr; b.off = o;
        cq_subnorm_step(ctx, &b, u);
        return; }
    case CQ_FDOP_ROUND: {
        cq_round_block b;

        b.wr          = cq_fd_op64(k, m, r->s0, v0, t0);
        b.result_exp  = cq_fd_op64(k, m, r->s1, v1, t1);
        b.result_sign = cq_fd_op64(k, m, r->s2, v2, t2);
        b.scr = k->scr; b.off = o;
        cq_round_step(ctx, &b, u);
        return; }
    /* THE TWO COMMUTATIVE ROW KINDS CARRY A RECORDED EQUIVALENT-MUTANT CLASS
     * (bd a-slot-scan-cannot-see-which-operand-a-slot-reads, measured on M32).
     * Exchanging `r->s0` and `r->s1` on `or` or `xor` survives every case —
     * same value, same targets, same tuple, same palindrome. DO NOT "SIMPLIFY"
     * EITHER LINE TO MATCH THE MUTANT: `lower_or!` emits CNOT(a), CNOT(b),
     * Toffoli(a, b) in that order (arith.jl:276-278) and Rule 1 is about
     * provenance, not only about behaviour. */
    case CQ_FDOP_OR:
        lane = u / 3;
        if (u % 3 == 0)
            cq_emit_cx(ctx, &cq_fd_op64(k, m, r->s0, v0, t0)[lane],
                       &cq_fd_sp(k, o, (uint32_t)W64)[lane]);
        else if (u % 3 == 1)
            cq_emit_cx(ctx, &cq_fd_op64(k, m, r->s1, v1, t1)[lane],
                       &cq_fd_sp(k, o, (uint32_t)W64)[lane]);
        else
            cq_emit_ccx(ctx, &cq_fd_op64(k, m, r->s0, v0, t0)[lane],
                        &cq_fd_op64(k, m, r->s1, v1, t1)[lane],
                        &cq_fd_sp(k, o, (uint32_t)W64)[lane]);
        return;
    case CQ_FDOP_XOR:
        /* arith.jl:286-288, in order: CNOT(a[i], r[i]) then CNOT(b[i], r[i]). */
        lane = u / 2;
        cq_emit_cx(ctx,
                   (u % 2 == 0) ? &cq_fd_op64(k, m, r->s0, v0, t0)[lane]
                                : &cq_fd_op64(k, m, r->s1, v1, t1)[lane],
                   &cq_fd_sp(k, o, (uint32_t)W64)[lane]);
        return;
    case CQ_FDOP_NOT1:
        /* arith.jl:476, in order: CNOT(w[1], r[1]) then NOT(r[1]). */
        if (u == 0) cq_emit_cx(ctx, cq_fd_flag_of(k, m, r->s0),
                               cq_fd_sp(k, o, 1u));
        else        cq_emit_x (ctx, cq_fd_sp(k, o, 1u));
        return;
    case CQ_FDOP_AND1:
        /* arith.jl:270's one Toffoli per lane, at one lane. */
        cq_emit_ccx(ctx, cq_fd_flag_of(k, m, r->s0),
                    cq_fd_flag_of(k, m, r->s1), cq_fd_sp(k, o, 1u));
        return;
    case CQ_FDOP_OR1:
        if (u == 0)      cq_emit_cx(ctx, cq_fd_flag_of(k, m, r->s0),
                                    cq_fd_sp(k, o, 1u));
        else if (u == 1) cq_emit_cx(ctx, cq_fd_flag_of(k, m, r->s1),
                                    cq_fd_sp(k, o, 1u));
        else             cq_emit_ccx(ctx, cq_fd_flag_of(k, m, r->s0),
                                     cq_fd_flag_of(k, m, r->s1),
                                     cq_fd_sp(k, o, 1u));
        return;
    case CQ_FDOP_VIEW: case CQ_FDOP_OUT: case CQ_FDOP_N_OP:
    default:
        break;
    }
    cq_kernel_die("fdiv: unknown op in the program");
}

void cq_fdiv_step(cq_ctx *ctx, const cq_fdiv_block *k, int u)
{
    cq_fd_map m;
    cq_fdiv_row r;
    int local = 0, i;

    cq_fd_arm(k, &m);
    if (u < 0 || u >= m.post_s[m.n_post])
        cq_kernel_die("fdiv: step index outside [0, cq_fdiv_steps())");
    i = cq_fd_row_of_slot(&m, u, &local);
    cq_fdiv_row_at(i, &r);
    emit_row(ctx, k, &m, &r, cq_fd_off(&m, i), local);
}

/* The last row — fdiv.jl:138's `return result`, the outermost `ifelse` of the
 * select chain, whose output is a `mux`'s own span. The guard is what keeps
 * this from handing back a pointer into a dead stack frame if the table's last
 * row ever became a view or a projection: NOTHING in the library holds a
 * circuit or a buffer (Rule 13), so an assembled operand lives only as long as
 * the call that built it. */
const cq_bit *cq_fdiv_result(const cq_fdiv_block *k)
{
    cq_fd_map m;
    cq_fdiv_row r;
    cq_bit buf[CQ_FD_W];
    const cq_bit *p;
    int n = cq_fdiv_n_rows();

    cq_fd_arm(k, &m);
    cq_fdiv_row_at(n - 1, &r);
    if (r.op == CQ_FDOP_VIEW || r.op == CQ_FDOP_OUT)
        cq_kernel_die("fdiv: `result` is a view or a projection, not a span");
    p = cq_fd_val64(k, &m, n - 1, buf);
    if (p == buf) cq_kernel_die("fdiv: `result` did not resolve to a span");
    return p;
}

/* --- The Rule 7 kernel: Bennett-in-the-small over one flat region. -------- */

typedef struct {
    cq_fdiv_block k;
    cq_bit       *dst;
} fdiv_env;

static void fdiv_compute(cq_ctx *ctx, void *env, int s)
{
    cq_fdiv_step(ctx, &((const fdiv_env *)env)->k, s);
}

/* The "^=" of Rule 7's contract, and the only place `dst` is written on the
 * sandwich path — which is why the driver runs it with the I6 extent
 * DISARMED. One CX per lane and no X: `result` already holds the value. */
static void fdiv_copyout(cq_ctx *ctx, void *env, int s)
{
    const fdiv_env *e = (const fdiv_env *)env;

    cq_emit_cx(ctx, &cq_fdiv_result(&e->k)[s], &e->dst[s]);
}

void cq_kernel_fdiv(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, const cq_bit *b,
                    int W)
{
    cq_scratch scr;
    fdiv_env e;

    /* PRD-v2 §1 scopes v2 to f64 and `soft_fdiv` is (UInt64, UInt64), so
     * another width is a fiction rather than an unimplemented case. */
    if (W != CQ_FP64_W)
        cq_kernel_die("fdiv: W must be 64 — v2 is f64 only and soft_fdiv "
                      "takes two UInt64");

    cq_kernel_check_dst(dst, a, b, W);

    /* RISK R9, AND IT IS NOT AN OPTIMISATION (plan §0.2 consequence 2, K11.md
     * §7.4). Pre-materialisation is unconditional and a BLOCK has no fold of
     * its own, so without this an all-classical `fdiv` would draw the whole
     * ~65k-bit region from the pool for an operation with no quantum input at
     * all — the largest L5 violation available anywhere in the catalogue. The
     * value comes from the SAME Julia body the circuit runs (PRD-v2 §7.4),
     * never from the host `double` operator. */
    if (cq_bits_all_const(a, W) && cq_bits_all_const(b, W)) {
        uint64_t v = cq_fdiv_eval(cq_fp_pack(a), cq_fp_pack(b));

        for (int i = 0; i < W; i++)
            if (((v >> (unsigned)i) & UINT64_C(1)) != 0u) cq_emit_x(ctx, &dst[i]);
        return;
    }

    /* ONE CONTIGUOUS REGION, carved into named sub-arrays, because emit.c's
     * I6(a) check is a pointer RANGE test over cq_bit addresses — a kernel
     * that allocated two regions would put half its targets outside it. */
    cq_scratch_alloc(&scr, cq_fdiv_region());
    e.k.a = a; e.k.b = b; e.k.scr = &scr; e.k.off = 0u;
    e.dst = dst;

    cq_sandwich(ctx, &scr, fdiv_compute, cq_fdiv_steps(),
                fdiv_copyout, W, &e);
    cq_scratch_dispose(&scr);
}
