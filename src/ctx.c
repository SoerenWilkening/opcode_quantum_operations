/* src/ctx.c — the aggregate. See ctx.h. */

#include "ctx.h"

#include <stddef.h>

void cq_ctx_init(cq_ctx *ctx, const cq_sink *sink)
{
    cq_qubits_init(&ctx->pool);
    cq_shadow_init(&ctx->shadow);
    cq_reg_table_init(&ctx->regs);
    ctx->sink = sink ? sink : cq_sink_active();
    ctx->sandwich_depth = 0;
    cq_ctrl_stack_init(&ctx->ctrl);
    ctx->strand_reports = 0;
    /* D15 §3's residue split (Step 23 landing 2). Zeroed here rather than
     * left to a memset that does not exist: cq_ctx is an object declared by
     * value and every field in it is initialised by name. */
    ctx->stranded_dirty    = 0;
    ctx->stranded_unproven = 0;
    ctx->frees_dirty       = 0;
    ctx->frees_unproven    = 0;

#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
    ctx->scratch_lo = NULL;
    ctx->scratch_hi = NULL;
#endif
}

/* Order is deliberate and it is the reverse of init. The register table goes
 * first because it is the only member holding per-handle allocations, and
 * DISPOSING IS NOT FREEING: it returns nothing to the pool, so a rail that was
 * never `cqrt_free`d stays counted as live right up to the last statement here
 * — the intended Rule-6 safe leak (PRD §10), not a tidy-up we may do quietly.
 * Anything that swept the table releasing rails would be releasing them
 * without evidence, which is the one unforgivable bug. */
void cq_ctx_dispose(cq_ctx *ctx)
{
    /* FIRST, and it is the only member whose dispose can fail: an unbalanced
     * cq_ctrl_push left flag qubits live, and saying so here names the cause
     * rather than letting it surface as a leaked index in someone's L2. */
    cq_ctrl_stack_dispose(&ctx->ctrl);
    cq_reg_table_dispose(&ctx->regs);
    cq_shadow_dispose(&ctx->shadow);
    cq_qubits_dispose(&ctx->pool);
    ctx->sink = NULL;
}

uint32_t cq_ctx_fresh_qubit(cq_ctx *ctx)
{
    uint32_t q = cq_qubits_acquire(&ctx->pool);
    cq_shadow_ensure(&ctx->shadow, cq_qubits_minted(&ctx->pool));
    return q;
}

/* Two statements, and the ORDER between them is the whole content of the
 * ckd.17a certificate — see ctx.h. Do not reorder, do not separate, and do not
 * grow a third statement between them. */
void cq_ctx_release_qubit(cq_ctx *ctx, uint32_t q, int proven_zero)
{
    cq_qubits_release(&ctx->pool, q, proven_zero);
    cq_shadow_retire(&ctx->shadow, q);
}
