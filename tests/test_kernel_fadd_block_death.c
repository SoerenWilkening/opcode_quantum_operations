/* tests/test_kernel_fadd_block_death.c — the fail-loud paths of M33's
 * exported `cq_fadd_block` (bd 9ve.27.2), the ADD twin of `cq_fsub_block`.
 *
 * THE TWIN ADDS NO NEW MESSAGE — it runs through the same mapped helpers as
 * fsub's block (fadd_emit.c's `blk_step`/`blk_result`) — but it is a new
 * ENTRY POINT, and a guard is only tested through the door a consumer uses.
 * So each of the six block-surface refusals tests/test_kernel_fadd_death.c
 * drives through `cq_fsub_*` is driven here through `cq_fadd_*`, with the
 * same FAIL_REGULAR_EXPRESSION groups (tests/CMakeLists.txt). A separate
 * binary only because the fsub death file sits at Rule 12's limit.
 */

#include "kernels/fadd.h"

#include "bit.h"
#include "ctx.h"
#include "kernels/fpfield.h"
#include "reg.h"
#include "scratch.h"
#include "support/bitkinds.h"
#include "support/death.h"

#define FA_ONE_BITS  0x3ff0000000000000ull     /* 1.0 */

static cq_sink    g_sink;
static cq_ctx     g_ctx;
static cq_scratch g_scr;

/* QUANTUM rails and a region of the given size — the exact fixture of the
 * fsub death file's `fsub_block`. */
static void fadd_block(cq_fadd_block *k, uint32_t off, int region_delta)
{
    int32_t ha, hb;

    g_sink = cq_death_null_sink();
    cq_ctx_init(&g_ctx, &g_sink);
    cq_scratch_alloc(&g_scr, (uint32_t)((int)cq_fadd_region() + region_delta));
    ha = cq_bk_reg(&g_ctx, (uint32_t)CQ_FP64_W, FA_ONE_BITS, ~0ull);
    hb = cq_bk_reg(&g_ctx, (uint32_t)CQ_FP64_W, FA_ONE_BITS, ~0ull);
    k->a = cq_reg_cbits(&g_ctx.regs, ha);
    k->b = cq_reg_cbits(&g_ctx.regs, hb);
    k->scr = &g_scr;
    k->off = off;
}

static void fadd_block_step_index_past_the_end(void)
{
    cq_fadd_block k;

    fadd_block(&k, 0u, 0);
    CQ_EXPECT_ABORT(cq_fadd_step(&g_ctx, &k, cq_fadd_steps()));
}

static void fadd_block_step_index_is_negative(void)
{
    cq_fadd_block k;

    fadd_block(&k, 0u, 0);
    CQ_EXPECT_ABORT(cq_fadd_step(&g_ctx, &k, -1));
}

static void fadd_block_region_is_one_bit_short(void)
{
    cq_fadd_block k;

    fadd_block(&k, 0u, -1);
    CQ_EXPECT_ABORT(cq_fadd_step(&g_ctx, &k, 0));
}

static void fadd_block_offset_runs_off_the_region(void)
{
    cq_fadd_block k;

    fadd_block(&k, 1u, 0);
    CQ_EXPECT_ABORT(cq_fadd_step(&g_ctx, &k, 0));
}

static void result_of_an_fadd_block_that_does_not_fit(void)
{
    cq_fadd_block k;

    fadd_block(&k, 4u, 0);
    CQ_EXPECT_ABORT((void)cq_fadd_result(&k));
}

static void an_fadd_block_with_no_region(void)
{
    cq_fadd_block k;

    fadd_block(&k, 0u, 0);
    k.scr = NULL;
    CQ_EXPECT_ABORT(cq_fadd_step(&g_ctx, &k, 0));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(fadd_block_step_index_past_the_end),
    CQ_DEATH_CASE(fadd_block_step_index_is_negative),
    CQ_DEATH_CASE(fadd_block_region_is_one_bit_short),
    CQ_DEATH_CASE(fadd_block_offset_runs_off_the_region),
    CQ_DEATH_CASE(result_of_an_fadd_block_that_does_not_fit),
    CQ_DEATH_CASE(an_fadd_block_with_no_region)
)
