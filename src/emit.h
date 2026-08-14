/* src/emit.h — M05: the gate emitter. PRD §3.
 *
 * Where classical/quantum is decided, and the only place. Everything above
 * this layer is Bennett.jl transcribed against these three functions, so a bug
 * here is a bug in all twelve kernels simultaneously — and it will present as
 * a kernel bug (Rule 11, plan §5).
 *
 * CONTROLS ARE `const`; TARGETS ARE NOT. This is not stylistic. It is one of
 * the two mechanisms enforcing I6 (plan §0.2): materialisation mutates a bit,
 * so a `const` control CANNOT BE MATERIALISED BY CONSTRUCTION, and a sandwich
 * compute half can therefore never turn a source into a qubit behind the
 * driver's back. The fold table never materialises a control anyway —
 * constants in control position are folded away — so the qualifier costs
 * nothing and removes a whole class of R1 miscompile. An earlier PRD draft
 * declared all three operands non-const, which would have silently disarmed
 * it. The other mechanism is the Debug scratch-extent check in ctx.h.
 *
 * KIND, NEVER SHADOW. Every row dispatches on the bit's kind (ZERO, ONE, Q)
 * and never on a qubit's shadow value. A Q control whose shadow is known-0 is
 * NOT folded away — that would be shadow-driven demotion, which D6 excludes
 * from v1 because it makes the qubit count depend on shadow precision and
 * every L4 golden fragile.
 */
#ifndef CQOPS_EMIT_H
#define CQOPS_EMIT_H

#include "bit.h"
#include "ctx.h"

void cq_emit_x  (cq_ctx *ctx,                         cq_bit *t);
void cq_emit_cx (cq_ctx *ctx, const cq_bit *c,        cq_bit *t);
void cq_emit_ccx(cq_ctx *ctx, const cq_bit *c1,
                              const cq_bit *c2,       cq_bit *t);

/* Takes a qubit from the pool (guaranteed |0> by I3), emits X if the bit's
 * constant was 1, sets kind = CQ_BIT_Q.
 *
 * THE ONLY PLACE A QUBIT IS EVER ALLOCATED FOR DATA (Rule 5), and exactly the
 * rule "a CX from a tainted bit into an untainted bit allocates a qubit".
 * Allocation is lazy and per bit — never at declaration, never per register —
 * which is what makes I4 (an all-constant register owns zero qubits) hold
 * without anyone maintaining it.
 *
 * Exposed rather than static because M07 needs it for cqrt_copy and M22 for
 * rotations; it aborts if the bit is already a qubit, since a second
 * materialisation would leak the first. */
void cq_materialise(cq_ctx *ctx, cq_bit *b);

#endif /* CQOPS_EMIT_H */
