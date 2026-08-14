/* src/ctx.c — the aggregate. See ctx.h. */

#include "ctx.h"

#include <stddef.h>

void cq_ctx_init(cq_ctx *ctx, const cq_sink *sink)
{
    cq_qubits_init(&ctx->pool);
    cq_shadow_init(&ctx->shadow);
    ctx->sink = sink ? sink : cq_sink_active();

#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
    ctx->scratch_lo = NULL;
    ctx->scratch_hi = NULL;
#endif
}

void cq_ctx_dispose(cq_ctx *ctx)
{
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
