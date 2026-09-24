/* src/kernels/iminmax.c — M42, K27. The five Rule 7 entry points: the operand
 * guard, risk R9's classical short-circuit, and ONE cq_sandwich over the block
 * in iminmax_step.c. See iminmax.h and docs/constructions/K27.md.
 *
 * SANDWICH, because every block this kernel composes is dirty by design:
 * `lower_ult!`/`lower_slt!` leave their carry chains, `lower_sub!` its `~b`
 * and carries, `lower_mux!` its `diff` (K09/K07/K10). Bennett's one global
 * forward-copy-reverse cleans them; we have none, so Bennett-in-the-small
 * (Rule 2, PRD §5): compute, W copy-out CNOTs, the compute half reversed.
 */

#include "kernels/iminmax.h"

#include "emit.h"
#include "kernels/kernel.h"
#include "sandwich.h"
#include "scratch.h"

typedef struct {
    cq_bit       *dst;
    cq_imm_block  k;
    const cq_bit *out;
} imm_env;

static void compute(cq_ctx *ctx, void *env, int s)
{
    cq_imm_step(ctx, &((const imm_env *)env)->k, s);
}

/* The "^=" of Rule 7's contract, and the only place `dst` is written on the
 * sandwich path: the mux row's `r` into dst, one CNOT per lane. */
static void copyout(cq_ctx *ctx, void *env, int i)
{
    const imm_env *e = (const imm_env *)env;

    cq_emit_cx(ctx, &e->out[i], &e->dst[i]);
}

/* --- Risk R9: every operand a constant, so the answer is known in C. ------
 *
 * The same function the circuit computes, evaluated per cq_bit over the
 * constants' VALUES (Rule 3: no packed scalar — a bit at a time, width-generic)
 * and XORed into dst through cq_emit_x, which is what makes it correct under a
 * quantum control and into a quantum dst on the uncompute. 0 gates and 0
 * qubits when dst is constant too (L5). */

/* x >= y over W constant lanes, from the top: the first differing lane
 * decides; `sgn` flips the sign lane's meaning (two's complement order). */
static int ge_const(const cq_bit *x, const cq_bit *y, int W, int sgn)
{
    for (int i = W - 1; i >= 0; i--) {
        int xi = cq_bit_value(x[i]), yi = cq_bit_value(y[i]);

        if (xi != yi) return (sgn && i == W - 1) ? yi : xi;
    }
    return 1;                                  /* equal: `>=` holds */
}

static void fold_minmax(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                        const cq_bit *b, int W, int op)
{
    int sgn  = (op == CQ_IMM_SMAX || op == CQ_IMM_SMIN);
    int swap = (op == CQ_IMM_UMIN || op == CQ_IMM_SMIN);
    int take_a = swap ? ge_const(b, a, W, sgn) : ge_const(a, b, W, sgn);
    const cq_bit *arm = take_a ? a : b;

    for (int i = 0; i < W; i++)
        if (cq_bit_value(arm[i])) cq_emit_x(ctx, &dst[i]);
}

/* |a| mod 2^W: `a` if its sign lane is 0, else ~a + 1 lane by lane with a
 * ripple carry. abs(INT_MIN) comes out INT_MIN, as the circuit's `sub` wraps. */
static void fold_abs(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W)
{
    int neg = cq_bit_value(a[W - 1]), carry = 1;

    for (int i = 0; i < W; i++) {
        int v = cq_bit_value(a[i]);

        if (neg) {
            int nv = !v;

            v = nv ^ carry;
            carry = nv & carry;
        }
        if (v) cq_emit_x(ctx, &dst[i]);
    }
}

static void run(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, const cq_bit *b,
                int W, int op)
{
    cq_scratch scr;
    imm_env e;

    cq_scratch_alloc(&scr, cq_imm_region(op, W));
    e.dst   = dst;
    e.k.a   = a;
    e.k.b   = b;
    e.k.scr = &scr;
    e.k.off = 0u;
    e.k.op  = op;
    e.k.W   = W;
    e.out   = cq_imm_out(&e.k);

    cq_sandwich(ctx, &scr, compute, cq_imm_steps(op, W), copyout, W, &e);
    cq_scratch_dispose(&scr);
}

static void minmax(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, const cq_bit *b,
                   int W, int op)
{
    cq_kernel_check_dst(dst, a, b, W);
    (void)cq_imm_steps(op, W);                 /* the module's own guard */

    if (cq_bits_all_const(a, W) && cq_bits_all_const(b, W)) {
        fold_minmax(ctx, dst, a, b, W, op);
        return;
    }
    run(ctx, dst, a, b, W, op);
}

void cq_kernel_umax(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W)
{ minmax(ctx, dst, a, b, W, CQ_IMM_UMAX); }

void cq_kernel_umin(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W)
{ minmax(ctx, dst, a, b, W, CQ_IMM_UMIN); }

void cq_kernel_smax(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W)
{ minmax(ctx, dst, a, b, W, CQ_IMM_SMAX); }

void cq_kernel_smin(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W)
{ minmax(ctx, dst, a, b, W, CQ_IMM_SMIN); }

void cq_kernel_abs(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W)
{
    const cq_bit *src[1] = { a };
    int w[1] = { W };

    cq_kernel_check_n(dst, W, src, w, 1);
    (void)cq_imm_steps(CQ_IMM_ABS, W);        /* the width cap, before R9 */

    if (cq_bits_all_const(a, W)) {
        fold_abs(ctx, dst, a, W);
        return;
    }
    run(ctx, dst, a, NULL, W, CQ_IMM_ABS);
}
