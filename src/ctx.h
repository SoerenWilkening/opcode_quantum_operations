/* src/ctx.h — the backend's entire mutable state.
 *
 * NOT IN PLAN §3's MODULE MAP. The map jumps from Layer 0 primitives to M05
 * `emit`, but PRD §3's emitter signature takes a `cq_ctx *` and M06–M09 all
 * need the same aggregate, so it has to exist somewhere by Step 6. It is kept
 * deliberately tiny — a struct and two functions, no policy — so that it stays
 * an aggregate rather than becoming a module with opinions of its own.
 *
 * Rule 13: this is ALL of it. A handle table (M07), a qubit pool with its free
 * list, one classical shadow bit-pair per qubit, and a borrowed sink. No
 * circuit object, no gate list, no statevector, and no simulator — a gate goes
 * out through the sink and is gone from our side.
 *
 * NOT YET PUBLIC. PRD §14 says <cqops/cqops.h> carries "context, sink,
 * config", but the only consumer of a public context is the shim at M26
 * (Step 23), and freezing an ABI four steps before its first caller would be
 * guessing. The sink half of §14 IS public already, because CQ_lang's fixtures
 * select one by environment variable.
 */
#ifndef CQOPS_CTX_H
#define CQOPS_CTX_H

#include "bit.h"
#include "qubits.h"
#include "shadow.h"
#include "sink.h"

typedef struct {
    cq_qubit_pool   pool;
    cq_shadow_table shadow;
    const cq_sink  *sink;     /* borrowed; see below on why it is resolved once */

#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
    /* I6 (plan §0.2): inside a cq_sandwich compute half, every gate TARGET is
     * a bit of the scratch region. This is the half of the enforcement that
     * cannot be done by the type system — the other half is that cq_emit_*
     * takes controls as `const cq_bit *`, so a source cannot be materialised
     * by construction. NULL means no sandwich is active and the check is off;
     * M09 sets the extent at Step 8. The range is over cq_bit ADDRESSES, not
     * qubit indices, because I6 is a statement about which bits may be
     * written, and scratch is a contiguous cq_bit array. */
    const cq_bit *scratch_lo, *scratch_hi;
#endif
} cq_ctx;

/* Borrows `sink` — it must outlive the context. Resolved ONCE here rather
 * than per gate: cq_sink_active() re-reads the environment on every call,
 * which is the right thing for a process-wide default and the wrong thing to
 * do a million times inside a kernel. Passing NULL takes cq_sink_active(). */
void cq_ctx_init(cq_ctx *ctx, const cq_sink *sink);
void cq_ctx_dispose(cq_ctx *ctx);

/* Takes a qubit from the pool and grows the shadow to cover it. The pool
 * guarantees |0> (I3) and a fresh shadow entry is born known-0, so the two
 * agree by construction. This is the only place those two facts are joined,
 * and cq_materialise is its only caller inside the library — tests use it to
 * build operands that look like a real program's. */
uint32_t cq_ctx_fresh_qubit(cq_ctx *ctx);

#endif /* CQOPS_CTX_H */
