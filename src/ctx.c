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
