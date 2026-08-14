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
 * THERE IS DELIBERATELY NO UN-POISON. Nothing here can return an entry to
 * determinate once it is unknown; a fresh entry is born known-0 and that is
 * the only way an entry is ever clean. This is not an oversight — bd ckd.17
 * (P0, OPEN) owns the question of where the structural zero certificate lives
 * and who may stamp it, and shipping a general setter here would settle that
 * blocker by accident, in the one direction that can launder a dirty rail
 * into a provably-clean one. Whoever resolves ckd.17 adds the write here,
 * named so a grep finds every caller.
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

/* The Ry/Rz row. Poisons, full stop: deciding whether θ is in the classical
 * set (§7) is M21's job, and a rotation that IS classical never reaches
 * here. One-way, like every other rule. */
void cq_shadow_rotate(cq_shadow_table *sh, uint32_t q);

#endif /* CQOPS_SHADOW_H */
