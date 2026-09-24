/* tests/test_kernel_iminmax_death.c — M42's fail-loud paths (K27).
 *
 * TWO LAYERS, AND EACH CASE CALLS THE LAYER IT IS NAMED FOR DIRECTLY.
 *
 *   The KERNEL layer: `cq_kernel_check_n` (kernel.h — D7a, D7b), called by all
 *   five entry points with their own arity, and M42's width cap on `abs`. Every
 *   operand here is CLASSICAL, so a deleted guard falls into the R9 fold, emits
 *   `cq_emit_x` on constants and returns — in silence, which is what the
 *   harness convicts as SURVIVED. `smin(a, a)` even returns the right answer.
 *
 *   The BLOCK layer: `imm_check` (unknown op, W <= 0, abs above the cap), the
 *   step-index range check and the region-fit check in cq_imm_step. These are
 *   masked from ABOVE by the kernel (cq_kernel_check_n refuses W <= 0 first)
 *   and from BELOW by the sibling blocks' own guards (`ult: width is not
 *   positive`, `ult: step index outside ...`), so the cases call the exported
 *   block entries directly and tests/CMakeLists.txt forbids every lower
 *   message (bd `a-composed-step-count-masks-its-own-width-guard-from-below`).
 *
 * Nothing is materialised in the block cases: every bit is CQ_BIT_ZERO, so a
 * wild gate folds away before it touches a target and a deleted guard shows
 * up as the harness's own "expected an abort" rather than as a sanitizer hit.
 * BOTH CONFIGURATIONS — no case is Debug-gated.
 */

#include "kernels/iminmax.h"

#include "bit.h"
#include "ctx.h"
#include "reg.h"
#include "scratch.h"
#include "support/bitkinds.h"
#include "support/death.h"

static cq_sink    g_sink;
static cq_ctx     g_ctx;
static cq_scratch g_scr;

static void setup(void)
{
    g_sink = cq_death_null_sink();
    cq_ctx_init(&g_ctx, &g_sink);
}

/* ---- The kernel layer. -------------------------------------------------- */

static void umax_dst_aliases_a_classical_source(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);
    int32_t hb = cq_bk_reg(&g_ctx, 8u, 0x37u, 0x00u);

    CQ_EXPECT_ABORT(cq_kernel_umax(&g_ctx, cq_reg_bits(&g_ctx.regs, ha),
                                   cq_reg_cbits(&g_ctx.regs, ha),
                                   cq_reg_cbits(&g_ctx.regs, hb), 8));
}

static void abs_dst_aliases_its_source(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0x80u, 0x00u);

    CQ_EXPECT_ABORT(cq_kernel_abs(&g_ctx, cq_reg_bits(&g_ctx.regs, ha),
                                  cq_reg_cbits(&g_ctx.regs, ha), 8));
}

/* D7b. `smin(a, a) == a`, so with the guard gone this returns the RIGHT answer
 * in silence — which is exactly why it is registered. */
static void smin_the_two_sources_are_the_same_register(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0xC8u, 0x00u);
    int32_t hd = cq_reg_alloc_zero(&g_ctx.regs, 8u);
    const cq_bit *a = cq_reg_cbits(&g_ctx.regs, ha);

    CQ_EXPECT_ABORT(cq_kernel_smin(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                   a, a, 8));
}

/* Registers cap at CQ_REG_WIDTH_MAX, so the over-wide operand is a raw
 * array — all CQ_BIT_ZERO, i.e. classical: a deleted cap falls into the R9
 * fold and returns. */
enum { IM_WIDE = 129 };
static cq_bit g_wide_a[IM_WIDE], g_wide_d[IM_WIDE];

static void abs_wider_than_the_register_cap(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_abs(&g_ctx, g_wide_d, g_wide_a, IM_WIDE));
}

/* ---- The block layer, called directly. ---------------------------------- */

static void imm_steps_zero_width(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_imm_steps(CQ_IMM_UMAX, 0));
}

static void imm_region_zero_width(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_imm_region(CQ_IMM_SMIN, 0));
}

static void imm_steps_unknown_op_past_the_end(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_imm_steps(CQ_IMM_N_OP, 4));
}

static void imm_steps_unknown_op_negative(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_imm_steps(-1, 4));
}

static void imm_steps_abs_above_the_cap(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_imm_steps(CQ_IMM_ABS, IM_WIDE));
}

/* Operands AND region inside one allocation, so even a wild lane read stays
 * in bounds and the only witness to a deleted guard is the exit code. */
static void imm_block(cq_imm_block *k, int op, int W, uint32_t slack)
{
    setup();
    cq_scratch_alloc(&g_scr, 2u * (uint32_t)W + cq_imm_region(op, W) + slack);
    k->a = g_scr.bits;
    k->b = g_scr.bits + W;
    k->scr = &g_scr;
    k->off = 2u * (uint32_t)W;
    k->op = op;
    k->W = W;
}

static void imm_step_index_past_the_end(void)
{
    cq_imm_block k;

    imm_block(&k, CQ_IMM_UMAX, 4, 64u);
    CQ_EXPECT_ABORT(cq_imm_step(&g_ctx, &k, cq_imm_steps(CQ_IMM_UMAX, 4)));
}

static void imm_step_index_negative(void)
{
    cq_imm_block k;

    imm_block(&k, CQ_IMM_UMAX, 4, 64u);
    CQ_EXPECT_ABORT(cq_imm_step(&g_ctx, &k, -1));
}

/* One bit short at the top: `off + region == n + 1`. */
static void imm_step_region_does_not_fit(void)
{
    cq_imm_block k;

    imm_block(&k, CQ_IMM_ABS, 4, 0u);
    k.off += 1u;
    CQ_EXPECT_ABORT(cq_imm_step(&g_ctx, &k, 0));
}

static void imm_step_block_has_no_scratch(void)
{
    cq_imm_block k;

    imm_block(&k, CQ_IMM_SMAX, 4, 0u);
    k.scr = NULL;
    CQ_EXPECT_ABORT(cq_imm_step(&g_ctx, &k, 0));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(umax_dst_aliases_a_classical_source),
    CQ_DEATH_CASE(abs_dst_aliases_its_source),
    CQ_DEATH_CASE(smin_the_two_sources_are_the_same_register),
    CQ_DEATH_CASE(abs_wider_than_the_register_cap),
    CQ_DEATH_CASE(imm_steps_zero_width),
    CQ_DEATH_CASE(imm_region_zero_width),
    CQ_DEATH_CASE(imm_steps_unknown_op_past_the_end),
    CQ_DEATH_CASE(imm_steps_unknown_op_negative),
    CQ_DEATH_CASE(imm_steps_abs_above_the_cap),
    CQ_DEATH_CASE(imm_step_index_past_the_end),
    CQ_DEATH_CASE(imm_step_index_negative),
    CQ_DEATH_CASE(imm_step_region_does_not_fit),
    CQ_DEATH_CASE(imm_step_block_has_no_scratch)
)
