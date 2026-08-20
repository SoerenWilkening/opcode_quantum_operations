/* src/shadow.h — M02: the per-qubit classical shadow. PRD §2.2 and §3.
 *
 * Two bits per qubit — {value, unknown} — and that is the whole of our
 * "simulation" (Rule 13). No amplitudes, no correlations, no statevector.
 * The shadow exists so the emitter can fold classical operands away and so
 * the pool can check I3; it is emphatically not a simulator, and it can be
 * exactly right while the circuit is wrong (the Prime Directive).
 *
 * Discipline, one-way: the shadow MAY report unknown where the truth is
 * determinate — it forgets correlations, and a CCX with a known-0 control is
 * the standing example — but it may NEVER report determinate where the truth
 * is unknown. Every rule below is written to fail in that safe direction.
 *
 * Layer 0, no internal dependencies (plan §3): the rules take raw qubit
 * indices, not cq_bits. By the time a §3 fold row reaches the shadow, every
 * surviving operand is CQ_BIT_Q, so M01 never enters.
 *
 * THERE IS DELIBERATELY NO UN-POISON OF A LIVE QUBIT, and there never will be.
 * Nothing here can return a live entry to determinate once it is unknown. The
 * reason is structural, not stylistic: a certified-but-live qubit read as a
 * CONTROL hits `t.unknown |= c.unknown`, so with c.unknown freshly zeroed the
 * poison STOPS PROPAGATING and the shadow starts claiming determinate
 * downstream of a genuine superposition — the one direction the discipline
 * above forbids.
 *
 * cq_shadow_retire — designed at bd ckd.17 (settled 2026-08-15), landed with
 * M09 at Step 8 — is NOT an exception to that, and this note is here so that
 * whoever maintains it does not turn it into something else. Its precondition
 * is that cq_qubits_release has ALREADY RETURNED for the index, so it never
 * runs on a live qubit — and by I3 an index on the free list IS |0⟩, which
 * makes {value 0, unknown 0} the CORRECT entry for it, bit-for-bit what
 * cq_shadow_ensure writes for a freshly minted one. Birth and retirement are
 * one rule; only the fact that `minted` never decreases had hidden that. The
 * single caller is cq_ctx_release_qubit, where the ORDER is the enforcement.
 * See PRD §10 for the certificate and IMPLEMENTATION_PLAN §0.1 for the
 * sandwich half; bd ckd.17b (the CQ_lang rail at cqrt_free) is still OPEN and
 * is not answered by this write.
 */
#ifndef CQOPS_SHADOW_H
#define CQOPS_SHADOW_H

#include <stdint.h>

/* PRD §2.2. `value` is meaningful iff `unknown` is clear — read `unknown`
 * first, always; cq_shadow_known_zero exists so the common check cannot get
 * that order wrong. */
typedef struct {
    uint8_t value;
    uint8_t unknown;
} cq_shadow;

/* Growable, indexed by qubit index. The pool hands out indices monotonically
 * (D5), so this only ever grows at the tail. */
typedef struct {
    cq_shadow *e;
    uint32_t   n;     /* entries in use; every index < n is addressable */
    uint32_t   cap;
} cq_shadow_table;

void cq_shadow_init(cq_shadow_table *sh);
void cq_shadow_dispose(cq_shadow_table *sh);

/* Grow to at least `n` qubits, each new entry born known-0 (I3: a qubit off
 * the free list is |0⟩). Monotone — it never shrinks, and it never disturbs
 * an entry that already exists. */
void cq_shadow_ensure(cq_shadow_table *sh, uint32_t n);

uint32_t cq_shadow_count(const cq_shadow_table *sh);

/* The raw pair. No precondition: reading it is how a caller decides whether
 * `value` means anything. An out-of-range index is a hard error. */
cq_shadow cq_shadow_get(const cq_shadow_table *sh, uint32_t q);

/* "Provably |0⟩" — the I3 and Rule 6 predicate. Tests `unknown` BEFORE
 * `value`: an entry poisoned while it happened to hold 0 still carries a zero
 * value byte, and calling that clean is exactly the laundering ckd.17 warns
 * about, arrived at by accident. */
int cq_shadow_known_zero(const cq_shadow_table *sh, uint32_t q);

/* The four PRD §3 update rules. Operands are qubit indices, and the two
 * multi-operand rules require distinct ones — §3's distinctness constraint,
 * whose real owner is M05, re-checked here in Debug because a coincident
 * operand would silently corrupt the shadow's own arithmetic. */
void cq_shadow_x     (cq_shadow_table *sh, uint32_t t);
void cq_shadow_cx    (cq_shadow_table *sh, uint32_t c, uint32_t t);
void cq_shadow_ccx   (cq_shadow_table *sh, uint32_t a, uint32_t b, uint32_t t);

/* The rotation row. Poisons, full stop — and WHICH §7 rows call it is the
 * caller's decision, not this function's. Classifying θ is M21's job and acting
 * on the class is M22's; PRD §15 D12 settles the acting part: ONLY `Ry` at an
 * angle off the π-lattice poisons, because every `Rz` and the `Z` of the
 * half-turn row are DIAGONAL and a diagonal gate cannot move a
 * computational-basis value. So a rotation that is classical never reaches here,
 * and neither does one that is merely phase-only. One-way, like every other
 * rule: nothing un-poisons a live qubit. */
void cq_shadow_rotate(cq_shadow_table *sh, uint32_t q);

/* RETIREMENT — the ckd.17a certificate, which is an ACT and not a thing.
 * Writes {value 0, unknown 0} for an index that has ALREADY gone back to the
 * pool. See the header comment above for why that is not an un-poisoning, and
 * PRD §10 for the governing rule: a certificate may only be written on a qubit
 * that has already left data use. Call it only through cq_ctx_release_qubit.
 *
 * BOTH BYTES ARE WRITTEN, ALWAYS. A bit poisoned while it held 1 has a FROZEN
 * value byte — cq_shadow_x is a no-op under poison, and cx/ccx update `value`
 * only when !unknown — so clearing `unknown` alone would publish a stale byte
 * as determinate.
 *
 * VERIFIED, NEVER RECOMPUTED. Recomputing the expected value would be a
 * simulator, which Rule 13 forbids outright. Instead this hard-errors in BOTH
 * configurations when the entry is determinate and NON-ZERO — the qubit is
 * demonstrably not |0⟩ and something upstream just certified it anyway. PRD
 * §10 records the exact reach: a complete detector of a non-cancelling
 * sandwich across the whole rotation-free kernel surface (Steps 10-17),
 * because cq_shadow_rotate is the only producer of `unknown` and, since Step 19,
 * its only caller is M22's general-Ry row (D12 — an Rz never reaches here) — and
 * INERT on the L6
 * corpus, where nearly every rail is rotation-tainted. Never report an L6 run
 * as evidence that the certificate held. */
void cq_shadow_retire(cq_shadow_table *sh, uint32_t q);

#endif /* CQOPS_SHADOW_H */
