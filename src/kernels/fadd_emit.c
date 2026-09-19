/* src/kernels/fadd_emit.c — M33, K15. THE DISPATCH AND THE SURFACE: which gate
 * a slot emits, the exported `cq_fsub_block`, and the two Rule 7 kernels. The
 * layout and the operand resolution are next door in fadd_step.c, on the seam
 * fadd_int.h records.
 *
 * ONE GATE PER SLOT, AND IT IS FORCED (bd ckd.14a, sandwich.h). cq_sandwich
 * runs the reverse pass by re-calling compute(env, s) with the SAME index, so
 * a step undoes itself only if it is an involution. Every branch of `emit_row`
 * emits exactly one X, CX or CCX, or delegates to a step block that does.
 *
 * COMPOSITE KERNELS CALL THE STEP FUNCTION, NEVER THE KERNEL. cq_kernel_eq,
 * cq_kernel_ult, cq_kernel_add, cq_kernel_shl_var and cq_kernel_fp_is_nan are
 * each a whole sandwich, and cq_sandwich refuses nesting in BOTH
 * configurations, so reaching for one here would abort before allocating
 * anything. M12 over M17 is the standing witness; M33 is the seventh consumer
 * of the same rule — and it is also why `cq_kernel_fsub` cannot call
 * `cq_kernel_fadd`: fsub is a longer PROGRAM, not a nested kernel.
 *
 * EVERY TARGET BELOW IS A BIT OF THE CALLER'S REGION — an inner block's own
 * internals, a `lower_not1!` wire, an `and`/`or`/`xor` output — which is I6(a)
 * by construction. `dst` is touched exclusively in the copy-out, which the
 * driver runs with the I6 extent deliberately DISARMED (sandwich.h).
 *
 * THE COPY-OUT IS 64 CX AND NOTHING ELSE. That is Rule 7's `^=`, and it is the
 * whole reason the compute half may leave ~24k scratch bits dirty for the
 * reverse to clean: `dst` never enters the sandwich extent, and the reverse
 * half is the only thing that returns K15's scratch to |0>. Do not `cqrt_free`
 * anything inside the compute half — the high bits of `fa`, the whole `wr_add`
 * span on the opposite-sign path and the discarded `wb >> d` are all provably
 * dead MATHEMATICALLY and none of them is provably |0> to the two-bit shadow.
 */

#include "kernels/fadd_int.h"

#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fpclass.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"
#include "kernels/shift_var.h"
#include "sandwich.h"

enum { W64 = CQ_FA_W };

/* --- The dispatch. -------------------------------------------------------- */

static void emit_row(cq_ctx *ctx, const cq_fa_ctx *x, const uint32_t *off,
                     int i, int u)
{
    const cq_fadd_row *r = &x->rows[i];
    cq_bit v0[CQ_FP64_W], v1[CQ_FP64_W], t0[CQ_FP64_W], t1[CQ_FP64_W];
    uint32_t o = off[i];
    int lane;

    switch (r->op) {
    case CQ_FAOP_CLASS: {
        cq_fp_class_block c;

        c.a = cq_fa_op64(x, off, r->s0, v0, t0);
        c.scr = x->scr; c.off = o; c.cls = (cq_fp_class)r->shift;
        cq_fp_class_step(ctx, &c, u);
        return; }
    case CQ_FAOP_EQ: {
        cq_eq_block e;

        e.a = cq_fa_op64(x, off, r->s0, v0, t0);
        e.b = cq_fa_op64(x, off, r->s1, v1, t1);
        e.diff = cq_fa_sp(x, o, (uint32_t)W64);
        e.orr  = cq_fa_sp(x, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        e.W = W64;
        cq_eq_step(ctx, &e, u);
        return; }
    case CQ_FAOP_ULT: {
        cq_ult_block c;

        c.a = cq_fa_op64(x, off, r->s0, v0, t0);
        c.b = cq_fa_op64(x, off, r->s1, v1, t1);
        c.nb    = cq_fa_sp(x, o,                            (uint32_t)W64);
        c.carry = cq_fa_sp(x, o + (uint32_t)W64,            (uint32_t)W64 + 1u);
        c.axnb  = cq_fa_sp(x, o + (uint32_t)(2 * W64) + 1u, (uint32_t)W64);
        c.W = W64;
        cq_ult_step(ctx, &c, u);
        return; }
    case CQ_FAOP_SUB: {
        cq_sub_block s;

        s.a = cq_fa_op64(x, off, r->s0, v0, t0);
        s.b = cq_fa_op64(x, off, r->s1, v1, t1);
        s.nb = cq_fa_sp(x, o,                       (uint32_t)W64);
        s.d  = cq_fa_sp(x, o + (uint32_t)W64,       (uint32_t)W64);
        s.c  = cq_fa_sp(x, o + (uint32_t)(2 * W64), (uint32_t)W64);
        s.W = W64;
        cq_sub_step(ctx, &s, u);
        return; }
    case CQ_FAOP_ADD: {
        cq_add_block a;

        a.a = cq_fa_op64(x, off, r->s0, v0, t0);
        a.b = cq_fa_op64(x, off, r->s1, v1, t1);
        a.t = cq_fa_sp(x, o,                 (uint32_t)W64);
        a.c = cq_fa_sp(x, o + (uint32_t)W64, (uint32_t)W64);
        a.W = W64;
        cq_add_step(ctx, &a, u);
        return; }
    case CQ_FAOP_MUX: {
        cq_mux_block m;

        m.cond = cq_fa_flag_of(x, off, r->s0);
        m.t    = cq_fa_op64(x, off, r->s1, v0, t0);
        m.f    = cq_fa_op64(x, off, r->s2, v1, t1);
        m.r    = cq_fa_sp(x, o,                 (uint32_t)W64);
        m.d    = cq_fa_sp(x, o + (uint32_t)W64, (uint32_t)W64);
        cq_mux_step(ctx, &m, u);
        return; }
    case CQ_FAOP_BSHL: case CQ_FAOP_BLSHR: {
        cq_barrel_block b;

        b.a = cq_fa_op64(x, off, r->s0, v0, t0);
        b.b = cq_fa_op64(x, off, r->s1, v1, t1);
        b.scr = x->scr; b.off = o; b.W = W64;
        b.dir = (r->op == CQ_FAOP_BSHL) ? CQ_BARREL_SHL : CQ_BARREL_LSHR;
        cq_barrel_step(ctx, &b, u);
        return; }
    case CQ_FAOP_CLZ: {
        cq_fa_bufs bf;
        cq_clz_block k = cq_fa_clz_of(x, off, i, &bf);

        cq_clz_step(ctx, &k, u);
        return; }
    case CQ_FAOP_SUBNORM: {
        cq_fa_bufs bf;
        cq_subnorm_block k = cq_fa_subnorm_of(x, off, i, &bf);

        cq_subnorm_step(ctx, &k, u);
        return; }
    case CQ_FAOP_ROUND: {
        cq_fa_bufs bf;
        cq_round_block k = cq_fa_round_of(x, off, i, &bf);

        cq_round_step(ctx, &k, u);
        return; }
    /* THE THREE COMMUTATIVE ROWS CARRY M32's RECORDED EQUIVALENT-MUTANT CLASS
     * (bd a-slot-scan-cannot-see-which-operand-a-slot-reads): exchanging the
     * two operands of `and`, `or` or `xor` leaves the value, the target set,
     * the gate tuple, the palindrome and the scratch all correct, because both
     * operators are commutative and no instrument in this repo checks a gate's
     * CONTROL index. DO NOT "SIMPLIFY" EITHER LINE TO MATCH THE MUTANT:
     * `lower_or!` emits CNOT(a), CNOT(b), Toffoli(a, b) in that order
     * (arith.jl:276-278) and Rule 1 is about provenance, not only behaviour. */
    case CQ_FAOP_AND:
        /* arith.jl:270's one Toffoli per lane, at 64 lanes. */
        cq_emit_ccx(ctx, &cq_fa_op64(x, off, r->s0, v0, t0)[u],
                    &cq_fa_op64(x, off, r->s1, v1, t1)[u],
                    &cq_fa_sp(x, o, (uint32_t)W64)[u]);
        return;
    case CQ_FAOP_OR:
        /* arith.jl:276-278, in order: CNOT(a), CNOT(b), Toffoli(a, b). */
        lane = u / 3;
        if (u % 3 == 0)
            cq_emit_cx(ctx, &cq_fa_op64(x, off, r->s0, v0, t0)[lane],
                       &cq_fa_sp(x, o, (uint32_t)W64)[lane]);
        else if (u % 3 == 1)
            cq_emit_cx(ctx, &cq_fa_op64(x, off, r->s1, v1, t1)[lane],
                       &cq_fa_sp(x, o, (uint32_t)W64)[lane]);
        else
            cq_emit_ccx(ctx, &cq_fa_op64(x, off, r->s0, v0, t0)[lane],
                        &cq_fa_op64(x, off, r->s1, v1, t1)[lane],
                        &cq_fa_sp(x, o, (uint32_t)W64)[lane]);
        return;
    case CQ_FAOP_XOR:
        /* arith.jl:287-288, in order: CNOT(a), CNOT(b), per lane. This is
         * `soft_fneg` (fneg.jl:6) and nothing else in K15 reaches it; with
         * `K_SIGN` as the second operand the fold table turns 63 of those 64
         * CNOTs into nothing and lane 63 into one X. */
        lane = u / 2;
        if (u % 2 == 0)
            cq_emit_cx(ctx, &cq_fa_op64(x, off, r->s0, v0, t0)[lane],
                       &cq_fa_sp(x, o, (uint32_t)W64)[lane]);
        else
            cq_emit_cx(ctx, &cq_fa_op64(x, off, r->s1, v1, t1)[lane],
                       &cq_fa_sp(x, o, (uint32_t)W64)[lane]);
        return;
    case CQ_FAOP_NOT1:
        /* arith.jl:476, in order: CNOT(w[1], r[1]) then NOT(r[1]). */
        if (u == 0) cq_emit_cx(ctx, cq_fa_flag_of(x, off, r->s0),
                               cq_fa_sp(x, o, 1u));
        else        cq_emit_x (ctx, cq_fa_sp(x, o, 1u));
        return;
    case CQ_FAOP_AND1:
        cq_emit_ccx(ctx, cq_fa_flag_of(x, off, r->s0),
                    cq_fa_flag_of(x, off, r->s1), cq_fa_sp(x, o, 1u));
        return;
    case CQ_FAOP_OR1:
        if (u == 0)      cq_emit_cx(ctx, cq_fa_flag_of(x, off, r->s0),
                                    cq_fa_sp(x, o, 1u));
        else if (u == 1) cq_emit_cx(ctx, cq_fa_flag_of(x, off, r->s1),
                                    cq_fa_sp(x, o, 1u));
        else             cq_emit_ccx(ctx, cq_fa_flag_of(x, off, r->s0),
                                     cq_fa_flag_of(x, off, r->s1),
                                     cq_fa_sp(x, o, 1u));
        return;
    default:
        break;
    }
    cq_kernel_die("fadd: unknown op in the program");
}

void cq_fa_step(cq_ctx *ctx, const cq_fa_ctx *x, int u)
{
    uint32_t off[CQ_FA_MAXR];

    if (u < 0 || u >= cq_fa_steps_of(x))
        cq_kernel_die("fadd: step index outside [0, cq_fsub_steps())");
    cq_fa_arm(x, off);

    for (int i = 0; i < x->n; i++) {
        int n = cq_fa_row_steps(&x->rows[i]);

        if (u < n) { emit_row(ctx, x, off, i, u); return; }
        u -= n;
    }
    cq_kernel_die("fadd: the step dispatch fell off the end of the program");
}

const cq_bit *cq_fa_result(const cq_fa_ctx *x, cq_fadd_prog p)
{
    uint32_t off[CQ_FA_MAXR];

    cq_fa_arm(x, off);
    return cq_fa_row_out(x, off, cq_fadd_result_row(p));
}

/* --- The exported `fsub` compute half (bd 9ve.23 / K19). ----------------- */

/* The rows are rebuilt per call rather than carried on the block, for
 * fpclass.c's reason: a block holding a row pointer would hold state whose
 * initialisation a consumer can forget, and a forgotten bind is a silent wrong
 * circuit rather than a failure. `cq_fadd_program` is a pure function of its
 * argument, so the reverse pass rebuilds the identical table. */
static cq_fa_ctx fsub_ctx(const cq_fsub_block *k, cq_fadd_row *rows)
{
    cq_fa_ctx x;

    x.a = k->a; x.b = k->b; x.scr = k->scr; x.off = k->off;
    x.n = cq_fadd_program(CQ_FADD_PROG_SUB, rows);
    x.rows = rows;
    return x;
}

uint32_t cq_fsub_region(void)
{
    cq_fadd_row rows[CQ_FADD_MAX_ROWS];
    cq_fsub_block k = { NULL, NULL, NULL, 0u };
    cq_fa_ctx x = fsub_ctx(&k, rows);

    return cq_fa_region_of(&x);
}

int cq_fsub_steps(void)
{
    cq_fadd_row rows[CQ_FADD_MAX_ROWS];
    cq_fsub_block k = { NULL, NULL, NULL, 0u };
    cq_fa_ctx x = fsub_ctx(&k, rows);

    return cq_fa_steps_of(&x);
}

void cq_fsub_step(cq_ctx *ctx, const cq_fsub_block *k, int u)
{
    cq_fadd_row rows[CQ_FADD_MAX_ROWS];
    cq_fa_ctx x = fsub_ctx(k, rows);

    cq_fa_step(ctx, &x, u);
}

const cq_bit *cq_fsub_result(const cq_fsub_block *k)
{
    cq_fadd_row rows[CQ_FADD_MAX_ROWS];
    cq_fa_ctx x = fsub_ctx(k, rows);

    return cq_fa_result(&x, CQ_FADD_PROG_SUB);
}

/* --- The two Rule 7 kernels: Bennett-in-the-small over one flat region. --- */

typedef struct {
    cq_fa_ctx    x;
    cq_fadd_prog p;
    cq_bit      *dst;
} fadd_env;

static void fadd_compute(cq_ctx *ctx, void *env, int s)
{
    cq_fa_step(ctx, &((const fadd_env *)env)->x, s);
}

static void fadd_copyout(cq_ctx *ctx, void *env, int s)
{
    const fadd_env *e = (const fadd_env *)env;

    cq_emit_cx(ctx, &cq_fa_result(&e->x, e->p)[s], &e->dst[s]);
}

static void fadd(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, const cq_bit *b,
                 int W, cq_fadd_prog p)
{
    cq_fadd_row rows[CQ_FADD_MAX_ROWS];
    cq_scratch scr;
    fadd_env e;

    /* PRD-v2 §1 scopes v2 to f64 and `soft_fadd` is (UInt64, UInt64), so
     * another width is a fiction rather than an unimplemented case. */
    if (W != CQ_FP64_W)
        cq_kernel_die(p == CQ_FADD_PROG_ADD
                      ? "fadd: W must be 64 — v2 is f64 only and soft_fadd "
                        "takes two UInt64"
                      : "fsub: W must be 64 — v2 is f64 only and soft_fsub "
                        "takes two UInt64");

    cq_kernel_check_dst(dst, a, b, W);

    /* RISK R9, AND IT IS NOT AN OPTIMISATION (plan §0.2 consequence 2).
     * Pre-materialisation is unconditional, so without this a fully classical
     * `fadd` would take ~24,000 qubits for an operation with no quantum input
     * at all. The value comes from the SAME Julia body the circuit runs
     * (PRD-v2 §7.4), never from the host `double` operator. */
    if (cq_bits_all_const(a, W) && cq_bits_all_const(b, W)) {
        uint64_t v = (p == CQ_FADD_PROG_ADD)
                   ? cq_fadd_eval(cq_fp_pack(a), cq_fp_pack(b))
                   : cq_fsub_eval(cq_fp_pack(a), cq_fp_pack(b));

        for (int i = 0; i < W; i++)
            if (((v >> (unsigned)i) & UINT64_C(1)) != 0u) cq_emit_x(ctx, &dst[i]);
        return;
    }

    e.x.a = a; e.x.b = b; e.x.off = 0u; e.x.scr = NULL;
    e.x.n = cq_fadd_program(p, rows);
    e.x.rows = rows;
    e.p = p;
    e.dst = dst;

    /* ONE CONTIGUOUS REGION, carved into named sub-arrays, because emit.c's
     * I6(a) check is a pointer RANGE test over cq_bit addresses — a kernel
     * that allocated two regions would put half its targets outside it. */
    cq_scratch_alloc(&scr, cq_fa_region_of(&e.x));
    e.x.scr = &scr;

    cq_sandwich(ctx, &scr, fadd_compute, cq_fa_steps_of(&e.x),
                fadd_copyout, W, &e);
    cq_scratch_dispose(&scr);
}

void cq_kernel_fadd(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                    const cq_bit *b, int W)
{ fadd(ctx, dst, a, b, W, CQ_FADD_PROG_ADD); }

void cq_kernel_fsub(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                    const cq_bit *b, int W)
{ fadd(ctx, dst, a, b, W, CQ_FADD_PROG_SUB); }
