/* src/kernels/iminmax_step.c — M42, K27. The BLOCK: layout, slot count and
 * the one-gate step dispatch over M14/M16/M17's exported step functions.
 *
 * SEAM (K27.md §7, recorded before the split): this file is the compute half
 * and nothing else — it allocates nothing, folds nothing and never enters
 * cq_sandwich. The kernel entry points, the R9 classical short-circuit and the
 * sandwich live in iminmax.c.
 *
 * NO GATE IS EMITTED HERE DIRECTLY. Every gate is emitted by a sibling
 * module's step function, which is itself the port of a named upstream
 * function; this file only decides WHICH block runs a given slot and wires its
 * operands, one block per IR instruction of
 * third_party/bennett/src/extract/instructions.jl:4782-4825, in IR order.
 */

#include "kernels/iminmax.h"

#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"
#include "reg.h"

/* `iconst(0)` of instructions.jl:4821-4822, as a SOURCE (PRD-v2 §7.3): a
 * const array of CQ_BIT_ZERO. It is only ever a CONTROL, so every gate it
 * reaches folds away at every operand mask; it is never scratch and never
 * materialised. Sized by the register width cap, which is why abs refuses
 * anything wider. */
_Static_assert(CQ_BIT_ZERO == 0, "IMM_ZERO is zero-initialised");
static const cq_bit IMM_ZERO[CQ_REG_WIDTH_MAX];

/* The one guard every entry shares. A single function rather than a copy per
 * entry, so there is exactly one line whose deletion each death case names. */
static void imm_check(int op, int W)
{
    if (op < 0 || op >= CQ_IMM_N_OP)
        cq_kernel_die("iminmax: unknown op (outside cq_imm_op)");
    if (W <= 0)
        cq_kernel_die("iminmax: width is not positive");
    if (op == CQ_IMM_ABS && W > (int)CQ_REG_WIDTH_MAX)
        cq_kernel_die("iminmax: abs width above CQ_REG_WIDTH_MAX (its "
                      "constant-zero source is that long)");
}

static int imm_signed (int op) { return op == CQ_IMM_SMAX || op == CQ_IMM_SMIN; }
static int imm_swapped(int op) { return op == CQ_IMM_UMIN || op == CQ_IMM_SMIN; }

/* The compare row's cost, ASKED of M16. `ule`/`sle` are the same block with
 * the operands exchanged, so the swap never changes a count. */
static int cmp_steps (int op, int W)
{ return imm_signed(op) ? cq_slt_steps(W)  : cq_ult_steps(W); }
static int cmp_region(int op, int W)
{ return imm_signed(op) ? cq_slt_region(W) : cq_ult_region(W); }

int cq_imm_steps(int op, int W)
{
    imm_check(op, W);
    if (op == CQ_IMM_ABS)
        return cq_sub_steps(W) + cq_slt_steps(W) + cq_mux_steps(W);
    return cmp_steps(op, W) + cq_mux_steps(W);
}

uint32_t cq_imm_region(int op, int W)
{
    imm_check(op, W);
    if (op == CQ_IMM_ABS)
        return (uint32_t)(cq_sub_region(W) + cq_slt_region(W) + cq_mux_region(W));
    return (uint32_t)(cmp_region(op, W) + cq_mux_region(W));
}

/* THE ONE PLACE `off` IS ADDED. Every span below is carved from this base. */
static cq_bit *imm_base(const cq_imm_block *k)
{
    return k->scr->bits + k->off;
}

/* `lower_slt!`'s five spans, in cq_slt_block's declaration order. */
static cq_slt_block slt_at(const cq_bit *x, const cq_bit *y, cq_bit *g, int W)
{
    cq_slt_block s = { x, y, g, g + W, g + 2 * W, g + 3 * W, g + 4 * W + 1, W };
    return s;
}

/* umax/umin/smax/smin: [compare][mux]. The compare is `lower_icmp!`'s row for
 * the predicate (arith.jl:409-418) exactly as cmp.c lowers it: uge/sge over
 * (a, b), ule/sle over (b, a), the raw flag read unchanged (all four are in
 * the KEEP set). The select is `lower_mux!(cond, a, b)` — ops[1] on true
 * (instructions.jl:4787 etc.), so the arms are NEVER swapped with the compare. */
static void minmax_step(cq_ctx *ctx, const cq_imm_block *k, cq_bit *g, int u)
{
    const int W = k->W, nc = cmp_steps(k->op, W);
    const cq_bit *x = imm_swapped(k->op) ? k->b : k->a;
    const cq_bit *y = imm_swapped(k->op) ? k->a : k->b;
    const cq_bit *flag;
    cq_bit *m = g + cmp_region(k->op, W);

    if (imm_signed(k->op)) {
        cq_slt_block s = slt_at(x, y, g, W);

        if (u < nc) { cq_slt_step(ctx, &s, u); return; }
        flag = cq_slt_flag(&s);                      /* x >=s y */
    } else {
        cq_ult_block c = { x, y, g, g + W, g + 2 * W + 1, W };

        if (u < nc) { cq_ult_step(ctx, &c, u); return; }
        flag = &c.carry[W];                          /* x >=u y (cmp.h) */
    }
    {
        cq_mux_block mb = { flag, k->a, k->b, m, m + W };

        cq_mux_step(ctx, &mb, u - nc);
    }
}

/* abs: [sub][slt][mux], instructions.jl:4821-4823 in order:
 *   neg = sub(0, x)             lower_sub!  (adder.jl:149-172) via cq_sub_step
 *   cmp = icmp sge(x, 0)        lower_slt!(x, 0), raw flag unchanged
 *   dst = select(cmp, x, neg)   lower_mux!  (arith.jl:522-532) */
static void abs_step(cq_ctx *ctx, const cq_imm_block *k, cq_bit *g, int u)
{
    const int W = k->W;
    const int ns = cq_sub_steps(W), nt = cq_slt_steps(W);
    cq_bit *t0 = g + cq_sub_region(W);
    cq_bit *m  = t0 + cq_slt_region(W);
    cq_sub_block s = { IMM_ZERO, k->a, g, g + W, g + 2 * W, W };  /* nb, d, c */
    cq_slt_block t = slt_at(k->a, IMM_ZERO, t0, W);

    if (u < ns)      { cq_sub_step(ctx, &s, u);      return; }
    if (u < ns + nt) { cq_slt_step(ctx, &t, u - ns); return; }
    {
        cq_mux_block mb = { cq_slt_flag(&t), k->a, s.d, m, m + W };

        cq_mux_step(ctx, &mb, u - ns - nt);
    }
}

void cq_imm_step(cq_ctx *ctx, const cq_imm_block *k, int u)
{
    const int n = cq_imm_steps(k->op, k->W);       /* runs imm_check first */
    const uint32_t need = cq_imm_region(k->op, k->W);

    /* cq_mux_step has NO range guard of its own, so this one is the only
     * thing between an off-by-one and a wild lane in the select row. */
    if (u < 0 || u >= n)
        cq_kernel_die("iminmax: step index outside [0, cq_imm_steps(op, W))");
    if (!k->scr || k->off > k->scr->n || k->scr->n - k->off < need)
        cq_kernel_die("iminmax: the block's region does not fit its scratch "
                      "at this offset");

    if (k->op == CQ_IMM_ABS) abs_step(ctx, k, imm_base(k), u);
    else                     minmax_step(ctx, k, imm_base(k), u);
}

const cq_bit *cq_imm_out(const cq_imm_block *k)
{
    return imm_base(k) + (cq_imm_region(k->op, k->W)
                          - (uint32_t)cq_mux_region(k->W));
}
