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

/* THE PHYSICAL TAIL, FOR M06 ONLY (Step 20). The three above are the full
 * emitter: §9 row 0, then the §3 fold on the CONTROLS, then either §9's
 * promotion or the gate itself. These two are that last clause on its own —
 * materialise a constant target, push the gate, update the shadow — with NO
 * control consultation of any kind.
 *
 * They exist because §9's promotion is expressed in gates, and a promotion that
 * called cq_emit_* back would promote its own promotion. Nothing outside
 * src/controlled.c may call them: a kernel that reaches for one is a kernel
 * opting out of the controlled axis, which Rule 9 exists to make impossible.
 *
 * They do NOT re-run check_target. That is deliberate and it is the whole of
 * I6's amendment for this step: I6 constrains the targets a KERNEL names, which
 * the public entry points above have already checked. The promotion's extra
 * target — M06's shared ancilla — is outside every scratch region by
 * construction, and is sound there because the pair of Toffolis that touch it
 * is self-inverse within one step. Widening the extent to cover it instead
 * would silently disarm I6(a) for the whole compute half, which is the same
 * wrong fix sandwich.h already records for the copyout. */
void cq_emit_cx_phys (cq_ctx *ctx, const cq_bit *c,  cq_bit *t);
void cq_emit_ccx_phys(cq_ctx *ctx, const cq_bit *c1,
                                   const cq_bit *c2, cq_bit *t);

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
