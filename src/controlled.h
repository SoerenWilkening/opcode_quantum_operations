/* src/controlled.h — M06, Step 20. PRD §9: the controlled axis.
 *
 * THE AXIS IS AN EMITTER MODE, NOT A KERNEL REWRITE (Rule 9, plan §0.3). PRD
 * §9's promotion — `NOT→CNOT`, `CNOT→Toffoli`, `Toffoli→` three Toffolis and one
 * reusable ancilla — is a GATE-LEVEL transform, so it lives here as a stack on
 * the context and `cq_emit_x/cx/ccx` consult it. Every kernel becomes controlled
 * for free and no kernel is aware the axis exists: there is no `_controlled`
 * variant of anything in `src/kernels/`, and there must never be one.
 *
 * ROW 0 IS THE FIRST THING THIS MODULE DOES, AND IT IS WHAT KEEPS EVERY
 * ZERO-COST CLAIM IN THE PRD TRUE. A `CQ_BIT_ZERO` control skips the region
 * entirely — 0 gates, 0 qubits; a `CQ_BIT_ONE` control emits it UNCONTROLLED,
 * verbatim; only a `CQ_BIT_Q` control promotes. A classical control is a
 * DECISION, not a circuit — the §3 fold table's own posture one level up. So
 * §11's L5 shapes, §7's "flip the constant, 0 gates, 0 qubits" and §12's
 * classical mode all survive the existence of this axis untouched.
 *
 * FOLD ON CONTROLS FIRST; PROMOTE BEFORE FOLDING ON THE TARGET. This ordering is
 * the whole correctness of the module and it is not symmetric, so it is stated
 * rather than left to be read out of emit.c:
 *
 *   - A fold that reads a gate's CONTROL is a SEMANTIC simplification and stays
 *     valid under any control: `CX(ZERO, t)` is the identity, and controlled-
 *     identity is the identity, so folding first and never promoting is right.
 *     Promoting first would emit `CCX(w, ZERO, t)` for a gate that is not there.
 *   - A fold that reads a gate's TARGET is a REPRESENTATION choice and is
 *     invalid under a quantum control. `cq_emit_x` on a constant target flips
 *     the constant in place for zero gates — UNCONDITIONALLY. Inside a promoted
 *     region that runs whether or not the control is set, which is a controlled
 *     region silently made unconditional: the single most dangerous defect this
 *     step can introduce, and one that is invisible at the all-quantum operand
 *     mask. So the promotion is taken BEFORE that row, and the target is
 *     materialised and driven by a real `CX(w, t)` instead.
 *
 * `cq_kernel_xor` at an ordinary mixed mask is the live witness that the second
 * case is reachable: a classical `CQ_BIT_ONE` source bit in control position
 * folds `dst` from ZERO to ONE for zero gates (emit.c's ONE row into its
 * constant row), so a `CQ_BIT_ONE` target really does arise mid-kernel.
 *
 * `bd skh` IS RESOLVED HERE AS **UNPROMOTED**, AND IT IS FORCED. `cq_materialise`
 * emits its `X` straight to the sink and does NOT route through `cq_emit_x`, so
 * it is untouched by this module — which is the correct answer, not an
 * accident of layering. With `b` the rail's classical value before the region,
 * `c` an inner control and `k` the control branch, the required semantics is
 * `b ⊕ (k ∧ c)`. Unpromoted, the fresh qubit is set to `b` unconditionally and
 * the promoted `CX→CCX` gives exactly that. Promoted, the fresh qubit is `k ∧ b`
 * and the result is `k ∧ (b ⊕ c)` — which disagrees in exactly one cell,
 * `b = 1, k = 0`: the branch on which the rail must still read its old value.
 * Materialisation changes a bit's ENCODING (constant → qubit, I4), never its
 * VALUE, and an encoding is not conditional on anything. PRD §15 D11's
 * constant-column chain is stated to be contingent on this answer.
 *
 * NESTING ANDs INTO ONE WIRE, so the promotion never sees more than one control
 * (PRD §9). The second push takes a fresh flag qubit, computes `w' = w ∧ ctrl`
 * with one UNPROMOTED Toffoli, promotes against `w'`, and uncomputes the AND
 * with the identical Toffoli at its pop. Pushing a control that IS the current
 * wire is idempotent (`q ∧ q = q`) and takes no ancilla — `if (q) { if (q) … }`
 * is an ordinary program and must not emit a Toffoli with two coincident
 * controls. CQ_lang cannot express nesting at all (its ABI prepends one i32
 * flag per template call and ANDs multi-condition control itself), so depth
 * seen from the shim is only ever 0 or 1; nesting exists because PRD §9 asks
 * for it, and its only tests are libcqops-internal.
 *
 * A CONTROL THAT COINCIDES WITH AN OPERAND OF THE GATE IT PROMOTES IS A HARD
 * ERROR IN BOTH CONFIGURATIONS, and the two halves of that are not the same
 * fact. Coincidence with the gate's TARGET requests a non-injective map — `if
 * (q) q ^= 1` sends both |0⟩ and |1⟩ to |0⟩ — and in the Toffoli case it also
 * LEAKS THE SHARED ANCILLA, because gate 3 reads a control gate 2 has already
 * flipped. There is no correct spelling and it must abort. Coincidence with an
 * inner CONTROL is by contrast perfectly well defined — `q ∧ q = q`, so the
 * right answer is to drop the duplicate and emit the gate one promotion level
 * down — and v1 REFUSES IT ANYWAY. Measured over all 239 CQ_lang goldens:
 * in every one of the 4,918 integer-and-fp `cqrt_*_controlled` calls the control
 * handle is distinct from every other operand, so the collapse would be
 * untested behaviour sitting in the tree. The arithmetic is recorded in PRD §9
 * so that enabling it later is an implementation rather than a re-derivation —
 * the same posture D11 takes for the rotation phases. Bennett cannot settle it:
 * `controlled()` allocates `ctrl_wire = n_wires + 1` and ASSERTS every inner
 * gate stays within `1:n_wires`, so upstream's promotion is defined only for a
 * control disjoint from every inner wire (Rule 1 has nothing to port).
 *
 * WHAT THIS MODULE DOES NOT DO. It takes no position on §7's rotations beyond
 * the two rows PRD §9 gives exactly (`R(θ/2); CX; R(−θ/2); CX`) and the refusal
 * PRD §15 D11 mandates for every row that FOLDS — v1 will not emit five
 * hand-derived control-side phases into a project with no instrument that can
 * see a wrong one. It also does not implement §9's v2 optimisation (only the
 * copy-out CNOTs need controlling, because a sandwich's compute and reverse
 * halves cancel at ctrl = 0): v1 promotes every gate, which is CORRECT and
 * costs ≤ 3×. The stronger reason promoting everything is sound, and the one
 * worth keeping: at ctrl = 0 nothing ever writes the scratch region and at
 * ctrl = 1 the reverse half empties it, so scratch is in the SAME state on both
 * branches — unentangled with the control, which is what makes `cq_sandwich`'s
 * release of it a release rather than a state collapse.
 */
#ifndef CQOPS_CONTROLLED_H
#define CQOPS_CONTROLLED_H

#include <stdint.h>

#include "bit.h"

/* reg.h's trick, and for reg.h's reason: ctx.h includes THIS header so that the
 * stack can be a member of cq_ctx, so this header cannot include ctx.h back. */
typedef struct cq_ctx cq_ctx;

/* PRD §9 row 0, as the only three states a region can be in. OFF covers both
 * "no region at all" and "a region whose control was CQ_BIT_ONE", because those
 * are the same thing to an emitter — row 0 says a ONE control emits the region
 * uncontrolled, verbatim. Zero is OFF so a zero-initialised stack is inert. */
enum {
    CQ_CTRL_OFF = 0,
    CQ_CTRL_SKIP,
    CQ_CTRL_PROMOTE
};

typedef struct {
    int    mode;
    cq_bit wire;        /* the ONE control wire; valid iff mode == PROMOTE   */

    /* Set when `wire` is an AND flag this frame minted and must uncompute. */
    int    owns_wire;
    cq_bit and_a, and_b;

    /* PRD §9's "one reusable ancilla shared across the whole promoted region",
     * acquired lazily at the first promoted Toffoli — upstream gates it on
     * `has_toff` for the same reason — and released at this frame's pop. */
    int    has_anc;
    cq_bit anc;
} cq_ctrl_frame;

typedef struct {
    cq_ctrl_frame *f;
    int            n, cap;
} cq_ctrl_stack;

void cq_ctrl_stack_init(cq_ctrl_stack *s);

/* Aborts if the stack is not empty: an unbalanced push is a caller bug that
 * would otherwise present as a leaked flag qubit one layer away from its cause. */
void cq_ctrl_stack_dispose(cq_ctrl_stack *s);

/* --- The hot path, read by emit.c on every gate. -------------------------- */

/* These take the STACK rather than the context precisely so they can be inline
 * here without this header having to see cq_ctx. emit.c passes &ctx->ctrl. */

static inline const cq_ctrl_frame *cq_ctrl_top(const cq_ctrl_stack *s)
{
    return s->n > 0 ? &s->f[s->n - 1] : (const cq_ctrl_frame *)0;
}

static inline int cq_ctrl_skipping(const cq_ctrl_stack *s)
{
    const cq_ctrl_frame *t = cq_ctrl_top(s);
    return t != (const cq_ctrl_frame *)0 && t->mode == CQ_CTRL_SKIP;
}

/* NULL unless a quantum control is active — so `if (cq_ctrl_wire(s))` is the
 * one test a gate needs, and the uncontrolled path costs one load. */
static inline const cq_bit *cq_ctrl_wire(const cq_ctrl_stack *s)
{
    const cq_ctrl_frame *t = cq_ctrl_top(s);
    return (t != (const cq_ctrl_frame *)0 && t->mode == CQ_CTRL_PROMOTE)
         ? &t->wire : (const cq_bit *)0;
}

/* --- The region API (plan §0.3). ------------------------------------------ */

/* Borrows nothing: the control bit is COPIED into the frame, so the region is
 * controlled by the value that bit had at entry. That is the right semantics
 * and it is also the safe one — a pointer into a register's bits array would
 * outlive nothing today but would silently follow a later materialisation.
 *
 * Refuses inside a `cq_sandwich` compute half, in both configurations. A push
 * there would emit the nested AND's Toffoli at a point the reverse replay does
 * not reach, and a kernel has no business knowing the axis exists (Rule 9). */
void cq_ctrl_push(cq_ctx *ctx, const cq_bit *ctrl);

/* Uncomputes the AND flag if this frame minted one, releases both flag and
 * shared ancilla, and pops. Aborts on an empty stack. */
void cq_ctrl_pop(cq_ctx *ctx);

int cq_ctrl_depth(const cq_ctx *ctx);

/* --- What emit.c calls once it has folded and decided to promote. --------- */

/* Each takes the gate AS THE CALLER WROTE IT, with every surviving control
 * already known to be CQ_BIT_Q, and emits §9's promotion of it. The target may
 * still be a constant; these materialise it, which is where D11's constant
 * column gets its correctness for free. */
void cq_ctrl_promote_x  (cq_ctx *ctx,                          cq_bit *t);
void cq_ctrl_promote_cx (cq_ctx *ctx, const cq_bit *c,         cq_bit *t);
void cq_ctrl_promote_ccx(cq_ctx *ctx, const cq_bit *c1,
                                      const cq_bit *c2,        cq_bit *t);

/* --- §7's rotations under §9 (M22 is the only caller). -------------------- */

/* Uncontrolled these are one `sink.ry` / `sink.rz`. Under a quantum control they
 * are PRD §9's exact promotion, `R(θ/2); CX; R(−θ/2); CX` — exact rather than
 * up-to-phase because at ctrl = 0 the half rotations cancel and at ctrl = 1
 * `X·R(α)·X = R(−α)` makes them add — and it stays inside §8's frozen six
 * entries, so the controlled axis needs no seventh either.
 *
 * NEITHER TOUCHES THE SHADOW, INCLUDING FOR THE TWO CXs, AND THAT IS EXACT
 * RATHER THAN CONSERVATIVE. The composite's two CXs cancel, so the net
 * basis-state permutation of the whole four-gate block is the identity; letting
 * the shadow see the CXs individually would only propagate the control wire's
 * poison into a target whose basis value provably did not move. What the
 * composite does to the shadow is the CALLER's, under PRD §15 D12: M22 poisons
 * for a general `Ry` and does not for an `Rz`, and controlled-Rz is diagonal —
 * so is its promotion — so D12's measured payoff survives the axis. */
void cq_ctrl_ry(cq_ctx *ctx, uint32_t q, double theta);
void cq_ctrl_rz(cq_ctx *ctx, uint32_t q, double phi);

/* --- PRD §15 D11's refusals. ---------------------------------------------- */

/* Inert unless a QUANTUM control is active; a hard error naming `row` when one
 * is. THE ABORT IS ONE SITE and it lives here rather than in M22 so that the
 * decision, the message and the bead reference have one home. `row` is the §7
 * cell being refused, e.g. "Ry, theta = 2pi (mod 4pi)".
 *
 * §9's promotion table states the operative scope in one line — a §7 FOLD row is
 * a hard error in v1 — and that is deliberately broader than "the zero-gate
 * rows". Five cells fold: the four that act by emitting nothing, and the half
 * turn's QUBIT cell, which does emit but realises the row only up to the `+i` of
 * `Rz(π)·X = Y`. An M06 built against the narrower phrasing would let exactly
 * the cell through that carried D11's `∓π/2` correction. */
void cq_ctrl_refuse_fold_row(const cq_ctx *ctx, const char *row);

/* A measurement has no controlled form: §8's vtable has six entries and none of
 * them is conditional, CQ_lang declares no `cqrt_measure_*_controlled` at any
 * width, and `mz` is not a unitary the §9 promotion could act on. Legal under a
 * CQ_BIT_ONE control, since row 0 makes that region verbatim; refused under a
 * quantum control AND under a skipped one, because "the measurement did not
 * happen" is not something a function returning a value can express. */
void cq_ctrl_refuse_measurement(const cq_ctx *ctx);

#endif /* CQOPS_CONTROLLED_H */
