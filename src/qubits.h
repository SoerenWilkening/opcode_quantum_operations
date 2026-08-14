/* src/qubits.h — M03: the qubit pool. PRD §2.2; D2, D4, I3.
 *
 * A monotonic counter plus a LIFO free list. Qubit *indices* are reused —
 * unlike handles, which are never reused (D5) — and the pool's entire job is
 * to guarantee that an index it hands out is |0⟩ (I3), so cq_materialise can
 * emit an X for a constant 1 and nothing at all for a constant 0.
 *
 * Layer 0, no internal dependencies (plan §3): this module does not include
 * shadow.h and never reads a shadow. See cq_qubits_release for why that is a
 * deliberate constraint rather than an omission.
 *
 * TWO STRUCTURAL IDENTITIES hold at every instant, and downstream code relies
 * on both:
 *
 *   minted == live + free   every index ever minted is in exactly one state
 *   peak   == minted        a fresh index is minted ONLY when the free list
 *                           is empty, which is exactly when live has already
 *                           reached minted. The monotonic counter therefore
 *                           IS the high-water mark, and PRD §8's "peak
 *                           qubits" needs no second mechanism to compute.
 */
#ifndef CQOPS_QUBITS_H
#define CQOPS_QUBITS_H

#include <stdint.h>

typedef struct {
    uint32_t *stack;     /* LIFO free list (D4), top at [n_free - 1] */
    uint8_t  *is_free;   /* per index, so double-release is O(1) to catch */
    uint32_t  cap;       /* allocated length of both arrays */
    uint32_t  n_free;
    uint32_t  minted;    /* monotonic: the next fresh index */
    uint32_t  live;
    uint32_t  peak;
    uint32_t  ceiling;   /* D2: 0 means unbounded */
} cq_qubit_pool;

void cq_qubits_init(cq_qubit_pool *p);
void cq_qubits_dispose(cq_qubit_pool *p);   /* leaves it usable, as if init'd */

/* D2. 0 lifts the ceiling; anything else bounds the number of qubits LIVE AT
 * ONCE, not the number of acquires — the ceiling models qec_n_logical, and a
 * bound on total acquires would fail long programs a device could run fine.
 * Setting one below the current live count is a hard error: it is a promise
 * that cannot be kept, and refusing it at the point it is made beats failing
 * at some unrelated acquire later. */
void     cq_qubits_set_ceiling(cq_qubit_pool *p, uint32_t ceiling);
uint32_t cq_qubits_ceiling(const cq_qubit_pool *p);

/* Takes the last-released index if there is one, else mints a fresh one.
 * Exceeding the ceiling is a hard error (D2, "fails loud"). */
uint32_t cq_qubits_acquire(cq_qubit_pool *p);

/* Returns a qubit to the free list. `proven_zero` is the CALLER's evidence
 * that the qubit really is |0⟩; a zero here is a hard error (I3, Rule 6).
 *
 * WHY THE EVIDENCE IS A PARAMETER rather than a shadow lookup. Two reasons,
 * and they point the same way. Plan §3 puts M03 in Layer 0 with no internal
 * dependencies, so it cannot include shadow.h. More importantly, bd ckd.17
 * (P0, OPEN) establishes that the two-bit shadow CANNOT be the free-time
 * oracle — poison is sticky, so after any sandwich kernel on a tainted
 * operand every bit of the rail reads unknown and a literal shadow check
 * would hard-error on every legitimate program. The proof has to be
 * structural. Hard-wiring a shadow read here would bake in precisely the
 * thing ckd.17 says does not work; taking the fact as an argument lets the
 * certificate change without touching this module, and forces every caller to
 * name its evidence at the call site where a grep can find it.
 *
 * Passing 1 for something you have not proven is the one unforgivable bug in
 * this project (NORTH_STAR §3): it launders a dirty ancilla onto the free
 * list, and the next cq_materialise hands it to unrelated data. */
void cq_qubits_release(cq_qubit_pool *p, uint32_t q, int proven_zero);

uint32_t cq_qubits_live(const cq_qubit_pool *p);
uint32_t cq_qubits_peak(const cq_qubit_pool *p);
uint32_t cq_qubits_minted(const cq_qubit_pool *p);
uint32_t cq_qubits_free(const cq_qubit_pool *p);

/* Whether an index is currently on the free list. An index that was never
 * minted is not free — it does not exist. */
int cq_qubits_is_free(const cq_qubit_pool *p, uint32_t q);

#endif /* CQOPS_QUBITS_H */
