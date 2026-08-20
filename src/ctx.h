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
#include "controlled.h"
#include "qubits.h"
#include "reg.h"
#include "shadow.h"
#include "sink.h"

/* The TAG is required and the typedef is NOT repeated here: reg.h declares
 * `typedef struct cq_ctx cq_ctx;` so that its own signatures can name a
 * context it must not include. This struct was anonymous until Step 7. */
struct cq_ctx {
    cq_qubit_pool   pool;
    cq_shadow_table shadow;
    cq_reg_table    regs;     /* M07, Step 7 — handles, tombstones, I2, I4 */
    const cq_sink  *sink;     /* borrowed; see below on why it is resolved once */

    /* M09, Step 8. NOT Debug-gated, unlike the extent below, and the asymmetry
     * is deliberate. "No nested sandwich" is one of the driver's own premises
     * (plan §0.1) and it is O(1) to check, so it lives in both configurations
     * — where the I6 sweeps do not. It also covers a case the extent could
     * not: a sandwich attempted from a COPYOUT step, where the extent is
     * deliberately disarmed and so cannot answer. */
    int             sandwich_depth;

    /* M06, Step 20 — PRD §9's control stack. NOT Debug-gated, and for a
     * stronger reason than sandwich_depth's: this one CHANGES BEHAVIOUR, and
     * the house rule is that CQOPS_DEBUG_INVARIANTS gates checking and never
     * behaviour, so that both configurations emit the identical gate stream
     * (include/cqops/cqops.h). Empty means no region is open and every
     * cq_emit_* takes the path it took before Step 20 existed. */
    cq_ctrl_stack   ctrl;

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
};

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

/* THE ONE JOINT WHERE A QUBIT LEAVES DATA USE, and the ORDER IS THE
 * ENFORCEMENT (PRD §10, bd ckd.17a). Verbatim from the PRD:
 *
 *     cq_qubits_release(&ctx->pool, q, proven_zero);   // aborts unless proven
 *     cq_shadow_retire(&ctx->shadow, q);               // reached only if it did
 *
 * The governing rule it exists to make unbreakable: a certificate may only be
 * written on a qubit that has ALREADY left data use. Retirement therefore
 * cannot run on a live qubit, which is what keeps poison sticky everywhere it
 * matters — a certified-but-live qubit read as a control hits
 * `t.unknown |= c.unknown`, so with `c.unknown` freshly zeroed the poison
 * stops propagating and the shadow starts claiming determinate downstream of a
 * genuine superposition. That is the one direction M02's discipline forbids,
 * and it is why the two calls may never be reordered or separated.
 *
 * `proven_zero` is the caller's evidence and stays the caller's problem — this
 * function adds none. Its THREE callers are cq_reg_free, which forwards a
 * per-qubit proof (M07); cq_sandwich's epilogue, which passes
 * CQ_ZERO_BY_PALINDROME (M09); and cq_ctrl_pop, which passes
 * CQ_ZERO_BY_CTRL_UNCOMPUTE for the shared Toffoli ancilla and the nested AND
 * flag (M06, Step 20). Those two literals are the only ones in src/ and there
 * is no third: PRD §10's rule is that a constant may exist only where the code
 * that RUNS a construction can assert that construction's premises. Retirement here also closes the Step 7
 * hazard appended to ckd.17: nothing used to un-poison a released index, so a
 * REUSED index kept its stale entry — cq_ctx_fresh_qubit only ensures up to
 * `minted`, and cq_shadow_ensure returns early. */
void cq_ctx_release_qubit(cq_ctx *ctx, uint32_t q, int proven_zero);

#endif /* CQOPS_CTX_H */
