/* src/kernels/fsqrt_emit.c — M40, K21. THE DISPATCH AND THE SURFACE: which
 * gate a slot emits, the public block's entry points, and the Rule 7 kernel.
 * The costs and the layout are in fsqrt_step.c and the operand resolution in
 * fsqrt_operand.c, on the seams fsqrt_int.h records; the row table is in
 * fsqrt.c.
 *
 * ONE GATE PER SLOT, AND IT IS FORCED (bd ckd.14a, sandwich.h). cq_sandwich
 * runs the reverse pass by re-calling compute(env, s) with the SAME index, so
 * a step undoes itself only if it is an involution. Every branch of emit_row
 * emits exactly one X, CX or CCX, or delegates to a step block that does the
 * same — cq_fp_class_step, cq_eq_step, cq_ult_step, cq_add_step, cq_sub_step,
 * cq_mux_step, cq_norm52_step, cq_round_step.
 *
 * COMPOSITE KERNELS CALL THE STEP FUNCTION, NEVER THE KERNEL. `cq_kernel_eq`,
 * `cq_kernel_ult`, `cq_kernel_add`, `cq_kernel_sub`, `cq_kernel_mux` and
 * `cq_kernel_fp_is_nan` are each a whole sandwich, and cq_sandwich refuses
 * nesting in BOTH configurations, so reaching for one from inside this compute
 * half aborts before allocating anything. M12 over M17 is the standing witness
 * and K21 is the eighth consumer of the same rule.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. Every target below is a bit
 * of the caller's region — an inner block's own internals, a `lower_not1!`
 * wire, an `or` output. The rail `a`, the views over it and the constant spans
 * reach the emitter only through cq_emit_*'s `const cq_bit *` parameters, so a
 * source cannot be materialised here by construction. `dst` is touched
 * exclusively in the copy-out, which the driver runs with the extent
 * deliberately disarmed.
 *
 * SIXTY-FOUR `cq_ult_block`s, SIXTY-FIVE `cq_sub_block`s AND ONE HUNDRED AND
 * THIRTY-SIX `cq_mux_block`s IN ONE REGION AT AS MANY OFFSETS, which is the
 * shape K11.md §7.5 says is the only thing that detects a dropped `off`: a
 * lone block whose base is lost slides wholesale, computes the right value,
 * mirrors perfectly and returns every qubit. K21 has 546, so they collide —
 * and the case that sees it is a SECOND program in one region, not this file.
 */

#include "kernels/fsqrt_int.h"

#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/fpround.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"
#include "sandwich.h"

enum { W64 = CQ_FS_W };

static void emit_row(cq_ctx *ctx, const cq_fsqrt_block *k,
                     const cq_fsqrt_row *rows, int i, int u)
{
    const cq_fsqrt_row *r = &rows[i];
    cq_bit v0[CQ_FS_W], v1[CQ_FS_W], v2[CQ_FS_W];
    cq_bit t0[CQ_FS_W], t1[CQ_FS_W], t2[CQ_FS_W];
    uint32_t o = cq_fs_map_get()->bit[i];
    int lane;

    switch (r->op) {
    case CQ_FSOP_CLASS: {
        cq_fp_class_block c;

        c.a = cq_fs_op64(k, r->s0, v0, t0);
        c.scr = k->scr; c.off = o + k->off; c.cls = (cq_fp_class)r->s1;
        cq_fp_class_step(ctx, &c, u);
        return; }
    case CQ_FSOP_EQ: {
        cq_eq_block e;

        e.a = cq_fs_op64(k, r->s0, v0, t0);
        e.b = cq_fs_op64(k, r->s1, v1, t1);
        e.diff = cq_fs_sp(k, o, (uint32_t)W64);
        e.orr  = cq_fs_sp(k, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        e.W = W64;
        cq_eq_step(ctx, &e, u);
        return; }
    case CQ_FSOP_ULT: {
        cq_ult_block c;

        c.a = cq_fs_op64(k, r->s0, v0, t0);
        c.b = cq_fs_op64(k, r->s1, v1, t1);
        c.nb    = cq_fs_sp(k, o,                            (uint32_t)W64);
        c.carry = cq_fs_sp(k, o + (uint32_t)W64,            (uint32_t)W64 + 1u);
        c.axnb  = cq_fs_sp(k, o + (uint32_t)(2 * W64) + 1u, (uint32_t)W64);
        c.W = W64;
        cq_ult_step(ctx, &c, u);
        return; }
    case CQ_FSOP_ADD: {
        cq_add_block a;

        a.a = cq_fs_op64(k, r->s0, v0, t0);
        a.b = cq_fs_op64(k, r->s1, v1, t1);
        a.t = cq_fs_sp(k, o,                 (uint32_t)W64);
        a.c = cq_fs_sp(k, o + (uint32_t)W64, (uint32_t)W64);
        a.W = W64;
        cq_add_step(ctx, &a, u);
        return; }
    case CQ_FSOP_SUB: {
        cq_sub_block s;

        s.a = cq_fs_op64(k, r->s0, v0, t0);
        s.b = cq_fs_op64(k, r->s1, v1, t1);
        s.nb = cq_fs_sp(k, o,                       (uint32_t)W64);
        s.d  = cq_fs_sp(k, o + (uint32_t)W64,       (uint32_t)W64);
        s.c  = cq_fs_sp(k, o + (uint32_t)(2 * W64), (uint32_t)W64);
        s.W = W64;
        cq_sub_step(ctx, &s, u);
        return; }
    case CQ_FSOP_MUX: {
        cq_mux_block m;

        m.cond = cq_fs_flag_of(k, r->s0);
        m.t    = cq_fs_op64(k, r->s1, v0, t0);
        m.f    = cq_fs_op64(k, r->s2, v1, t1);
        m.r    = cq_fs_sp(k, o,                 (uint32_t)W64);
        m.d    = cq_fs_sp(k, o + (uint32_t)W64, (uint32_t)W64);
        cq_mux_step(ctx, &m, u);
        return; }
    case CQ_FSOP_NORM52: {
        cq_norm52_block b;

        b.m = cq_fs_op64(k, r->s0, v0, t0);
        b.e = cq_fs_op64(k, r->s1, v1, t1);
        b.scr = k->scr; b.off = o + k->off;
        cq_norm52_step(ctx, &b, u);
        return; }
    case CQ_FSOP_ROUND: {
        cq_round_block b;

        b.wr          = cq_fs_op64(k, r->s0, v0, t0);
        b.result_exp  = cq_fs_op64(k, r->s1, v1, t1);
        b.result_sign = cq_fs_op64(k, r->s2, v2, t2);
        b.scr = k->scr; b.off = o + k->off;
        cq_round_step(ctx, &b, u);
        return; }
    /* THE `or` ROWS CARRY A RECORDED EQUIVALENT-MUTANT CLASS (bd
     * a-slot-scan-cannot-see-which-operand-a-slot-reads, measured on M32).
     * Exchanging `r->s0` and `r->s1` survives every structural case — same
     * value, same targets, same tuple, same palindrome — because `|` is
     * commutative and the scan predicts a KIND per slot while advancing its
     * cursor only on a non-NONE. DO NOT "SIMPLIFY" THE LINE TO MATCH THE
     * MUTANT: `lower_or!` emits CNOT(a), CNOT(b), Toffoli(a, b) in that order
     * (arith.jl:276-278) and Rule 1 is about provenance, not only behaviour. */
    case CQ_FSOP_OR:
        lane = u / 3;
        if (u % 3 == 0)
            cq_emit_cx(ctx, &cq_fs_op64(k, r->s0, v0, t0)[lane],
                       &cq_fs_sp(k, o, (uint32_t)W64)[lane]);
        else if (u % 3 == 1)
            cq_emit_cx(ctx, &cq_fs_op64(k, r->s1, v1, t1)[lane],
                       &cq_fs_sp(k, o, (uint32_t)W64)[lane]);
        else
            cq_emit_ccx(ctx, &cq_fs_op64(k, r->s0, v0, t0)[lane],
                        &cq_fs_op64(k, r->s1, v1, t1)[lane],
                        &cq_fs_sp(k, o, (uint32_t)W64)[lane]);
        return;
    case CQ_FSOP_NOT1:
        /* arith.jl:476, in order: CNOT(w[1], r[1]) then NOT(r[1]). */
        if (u == 0) cq_emit_cx(ctx, cq_fs_flag_of(k, r->s0),
                               cq_fs_sp(k, o, 1u));
        else        cq_emit_x (ctx, cq_fs_sp(k, o, 1u));
        return;
    case CQ_FSOP_AND1:
        /* arith.jl:270's one Toffoli per lane, at one lane. */
        cq_emit_ccx(ctx, cq_fs_flag_of(k, r->s0), cq_fs_flag_of(k, r->s1),
                    cq_fs_sp(k, o, 1u));
        return;
    case CQ_FSOP_VIEW: case CQ_FSOP_SVIEW: case CQ_FSOP_OUT: case CQ_FSOP_N_OP:
    default:
        break;
    }
    cq_kernel_die("fsqrt: unknown op in the program");
}

void cq_fsqrt_step(cq_ctx *ctx, const cq_fsqrt_block *k, int u)
{
    const cq_fsqrt_row *rows;
    const cq_fs_map *m;
    int n, i;

    if (u < 0 || u >= cq_fsqrt_steps())
        cq_kernel_die("fsqrt: step index outside [0, cq_fsqrt_steps())");
    cq_fs_arm(k);
    rows = cq_fsqrt_rows(&n);
    m = cq_fs_map_get();
    i = cq_fs_row_at(u);
    if (i < 0 || i >= n)
        cq_kernel_die("fsqrt: the step dispatch fell off the end of the "
                      "program");
    emit_row(ctx, k, rows, i, u - m->slot[i]);
}

/* The last row — fsqrt.jl:117's `return result`, the outermost `ifelse` of the
 * select chain, whose output is a `mux`'s own span. The guard is what keeps
 * this from handing back a pointer into a dead stack frame if the table's last
 * row ever became a view or a projection: NOTHING in the library holds a
 * circuit or a buffer (Rule 13), so an assembled operand lives only as long as
 * the call that built it. */
const cq_bit *cq_fsqrt_result(const cq_fsqrt_block *k)
{
    const cq_fsqrt_row *rows;
    cq_bit buf[CQ_FS_W];
    const cq_bit *p;
    int n;

    rows = cq_fsqrt_rows(&n);
    cq_fs_arm(k);
    if (rows[n - 1].op == CQ_FSOP_VIEW || rows[n - 1].op == CQ_FSOP_SVIEW
        || rows[n - 1].op == CQ_FSOP_OUT)
        cq_kernel_die("fsqrt: `result` is a view or a projection, not a span");
    p = cq_fs_val64(k, n - 1, buf);
    if (p == buf) cq_kernel_die("fsqrt: `result` did not resolve to a span");
    return p;
}

/* --- The Rule 7 kernel: Bennett-in-the-small over one flat region. -------- */

typedef struct {
    cq_fsqrt_block k;
    cq_bit        *dst;
} fsqrt_env;

static void fsqrt_compute(cq_ctx *ctx, void *env, int s)
{
    cq_fsqrt_step(ctx, &((const fsqrt_env *)env)->k, s);
}

/* The "^=" of Rule 7's contract, and the only place `dst` is written on the
 * sandwich path — which is why the driver runs it with the I6 extent
 * DISARMED. One CX per lane and no X: `result` already holds the value. */
static void fsqrt_copyout(cq_ctx *ctx, void *env, int s)
{
    const fsqrt_env *e = (const fsqrt_env *)env;

    cq_emit_cx(ctx, &cq_fsqrt_result(&e->k)[s], &e->dst[s]);
}

void cq_kernel_fsqrt(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W)
{
    const cq_bit *src[1];
    cq_scratch scr;
    fsqrt_env e;
    int w[1];

    /* PRD-v2 §1 scopes v2 to f64 and `soft_fsqrt` is (UInt64), so another
     * width is a fiction rather than an unimplemented case. */
    if (W != CQ_FP64_W)
        cq_kernel_die("fsqrt: W must be 64 — v2 is f64 only and soft_fsqrt "
                      "takes one UInt64");

    /* D7a at the kernel boundary. The D7b leg is vacuous at arity 1, which is
     * why this is `cq_kernel_check_n` and not `cq_kernel_check_dst` — the
     * arity-2 wrapper would compare `a` against itself. */
    src[0] = a;
    w[0] = W;
    cq_kernel_check_n(dst, W, src, w, 1);

    /* RISK R9, AND IT IS NOT AN OPTIMISATION (plan §0.2 consequence 2).
     * Pre-materialisation is unconditional and a BLOCK has no fold of its own,
     * so without this an all-classical `fsqrt` would draw 67,405 qubits from
     * the pool for an operation with no quantum input at all — the largest L5
     * violation available anywhere in the catalogue. The value comes from the
     * SAME Julia body the circuit runs (PRD-v2 §7.4), never from the host
     * `sqrt()`. */
    if (cq_bits_all_const(a, W)) {
        uint64_t v = cq_fsqrt_eval(cq_fp_pack(a));

        for (int i = 0; i < W; i++)
            if (((v >> (unsigned)i) & UINT64_C(1)) != 0u) cq_emit_x(ctx, &dst[i]);
        return;
    }

    /* ONE CONTIGUOUS REGION, carved into named sub-arrays, because emit.c's
     * I6(a) check is a pointer RANGE test over cq_bit addresses — a kernel
     * that allocated two regions would put half its targets outside it. */
    cq_scratch_alloc(&scr, cq_fsqrt_region());
    e.k.a = a; e.k.scr = &scr; e.k.off = 0u;
    e.dst = dst;

    cq_sandwich(ctx, &scr, fsqrt_compute, cq_fsqrt_steps(),
                fsqrt_copyout, W, &e);
    cq_scratch_dispose(&scr);
}
