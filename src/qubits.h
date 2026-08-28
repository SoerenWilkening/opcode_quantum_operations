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
 *
 * BOTH SURVIVE STRANDING (PRD §15 D15 §3, Step 23), AND THAT IS WHY STRANDING
 * IS NOT A THIRD BUCKET. A stranded index is never released, so it stays in
 * `live` — it genuinely is live, nobody got it back — and neither `n_free` nor
 * `minted` moves. The arithmetic is therefore untouched, which is exactly D15
 * §3's "simply not calling cq_ctx_release_qubit keeps both identities".
 * A `retired` bucket would have broken both and would have made a retired
 * index read as a leaked ancilla to cq_pc_live_is_exactly, which runs on every
 * L1 case of every kernel. See cq_qubits_strand.
 *
 * STEP 26'S RETIREMENT (PRD §15 D21 (b)) IS THE SAME MECHANISM UNDER A SECOND
 * NAME, AND IT IS STILL NOT A BUCKET. With recycling off a released index is
 * marked and dropped rather than pushed, so `live` does not fall and `n_free`
 * does not rise: both identities hold with no arithmetic, exactly as for a
 * strand. What differs is only the FACT recorded — a retired qubit was proven
 * |0⟩ and a stranded one was not — which is why the counters are separate.
 */
#ifndef CQOPS_QUBITS_H
#define CQOPS_QUBITS_H

#include <stdint.h>

typedef struct {
    uint32_t *stack;       /* LIFO free list (D4), top at [n_free - 1] */
    uint8_t  *is_free;     /* per index, so double-release is O(1) to catch */
    /* A SEPARATE ARRAY, NOT A THIRD VALUE IN is_free, and the reason is a
     * configuration split rather than tidiness. cq_pool_free_flag asserts that
     * byte is 0 or 1 only under CQOPS_DEBUG_INVARIANTS and otherwise returns
     * it, so a `2` would abort every strand in Debug and read as TRUE — "this
     * index is on the free list" — in Release. That is behaviour differing by
     * configuration on the one mechanism whose whole job is I3, which ctx.h
     * forbids by name. The two states are disjoint, never ordered. */
    uint8_t  *is_stranded; /* PRD §15 D15 §3: never released, never on the list */
    /* PRD §15 D21 (b), Step 26. A FOURTH ARRAY RATHER THAN A THIRD VALUE IN
     * is_stranded, for the reason above it and one more: the D15 residue report
     * must never confuse "retired for the qec trace contract" with "could not
     * prove |0⟩". They are the same ACT on the pool and different FACTS about
     * the qubit, and a shared byte would make the two indistinguishable to
     * cq_qubits_is_stranded, which poolcheck runs on every L1 case. */
    uint8_t  *is_retired;
    uint32_t  cap;         /* allocated length of all four arrays */
    uint32_t  n_free;
    uint32_t  minted;      /* monotonic: the next fresh index */
    uint32_t  live;        /* INCLUDES stranded and retired — see the header */
    uint32_t  peak;
    uint32_t  n_stranded;  /* observability; not a pool bucket */
    uint32_t  n_retired;   /* observability; not a pool bucket. D21 (b) */
    uint32_t  ceiling;     /* D2: 0 means unbounded */
    uint8_t   recycle;     /* D4 by default; 0 under the qec sink (D21 (b)) */
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

/* PRD §15 D21 (b), Step 26 — INDEX RECYCLING AS A MODE, and a DISPLAY-MODEL
 * constraint rather than a physical one. 1 is D4's LIFO reuse and is the
 * default; 0 makes a released index RETIRE instead of returning to the free
 * list, so an index belongs to at most one register for the life of the
 * process.
 *
 * WHY IT EXISTS. qec/docs/HOST_LANGUAGE_HANDOFF.md §3 — the normative contract
 * the trace annotations answer to — says "every index belongs to at most one
 * register", and declaring one index under two `#REGISTER` lines is an
 * overlapping register, which the viewer's parser rejects as a FATAL error. D4
 * hands a freed rail's index to an unrelated rail, so under that contract the
 * two cannot both hold. The qec patch is genuinely back at |0⟩ after our free
 * and would happily be reused; nothing physical is being avoided here.
 *
 * WHAT IT DOES NOT BUY, recorded because the plausible second motive is FALSE
 * and was written down before it was checked. MEASURED 2026-08-28: qec_x(ctx,
 * 99) on an n_logical = 3 context returns 0, so the QEC library does NOT
 * range-check a logical index and the D2 ceiling is the only thing standing
 * between us and an out-of-range `q`. It is tempting to conclude that the
 * ceiling must therefore be paired with this mode to bound the INDEX rather
 * than the live count — and it must not: `peak == minted` (see the header note
 * above) and `live <= ceiling` is enforced at every acquire, so `minted <=
 * ceiling` holds WITH recycling on. Every index is already below the ceiling.
 * D2 bounds the index on its own; this mode is a display constraint and nothing
 * more. The two are set together because the sink needs both, not because
 * either completes the other.
 *
 * REFUSES, in both configurations, to turn recycling OFF while the free list is
 * non-empty: those indices are already destined for reuse, so the promise could
 * not be kept retroactively. Same posture as a ceiling below the live count. */
void cq_qubits_set_recycle(cq_qubit_pool *p, int on);
int  cq_qubits_recycles   (const cq_qubit_pool *p);

/* Returns a qubit to the free list. `proven_zero` is the CALLER's evidence
 * that the qubit really is |0⟩; anything but a POSITIVE value is a hard error
 * (I3, Rule 6).
 *
 * POSITIVE, not merely non-zero, since Step 23. src/reg.h's cq_zero_proof is
 * three-valued by SIGN and a negative answer is a CONVICTION — the library can
 * see the qubit is not |0⟩ — so `!proven_zero` would have read the strongest
 * possible refusal as proof and put a dirty index straight onto the free list.
 *
 * WHY THE EVIDENCE IS A PARAMETER rather than a shadow lookup. Two reasons,
 * and they point the same way. Plan §3 puts M03 in Layer 0 with no internal
 * dependencies, so it cannot include shadow.h. More importantly, bd ckd.17
 * establishes that the two-bit shadow cannot be the free-time oracle for a
 * ROTATION-TAINTED rail — poison is sticky, so after any sandwich kernel on a
 * tainted operand every bit of the rail reads unknown and a literal shadow
 * check would hard-error on a rail that is provably back to |0⟩. That scope
 * matters: on the rotation-free surface the shadow is EXACT, and
 * tests/support/poolcheck.c is a literal shadow check that frees on every
 * kernel L3 case. The proof has to be structural. Hard-wiring a shadow read
 * here would bake in precisely the thing ckd.17 says does not work; taking the
 * fact as an argument lets the certificate change without touching this
 * module, and forces every caller to name its evidence at the call site where
 * a grep can find it. That substitution is now scheduled rather than
 * hypothetical: PRD §15 D15 makes the evidence an observed undo certificate
 * over the CALL STREAM, supplied by M26 at Step 23 (bd 06t), and this
 * signature does not change.
 *
 * Passing 1 for something you have not proven is the one unforgivable bug in
 * this project (NORTH_STAR §3): it launders a dirty ancilla onto the free
 * list, and the next cq_materialise hands it to unrelated data. D15 §3 keeps
 * that absolutely. Where a caller cannot prove |0⟩ the sanctioned move is to
 * STRAND the index — never released, never on the free list, counted — not to
 * reach this function with a 1. Stranding is the alternative to passing 1,
 * never an exception to it. */
void cq_qubits_release(cq_qubit_pool *p, uint32_t q, int proven_zero);

/* PRD §15 D15 §3 — THE UNPROVEN AND CONVICTED DISPOSITION. "The qubit is never
 * released, never reaches the free list, and is counted. The program
 * continues."
 *
 * THIS IS THE ALTERNATIVE TO PASSING 1 TO cq_qubits_release, NEVER AN
 * EXCEPTION TO IT. The layering that D15 §1 establishes — the |0⟩ proof is
 * CQ_lang's and cannot be performed at our layer — licenses NOT ABORTING; it
 * does not license RECYCLING, and the two fail differently. Not-aborting costs
 * a leaked qubit if the caller was wrong; recycling hands a non-|0⟩ index to
 * the next cq_materialise and corrupts an unrelated rail. D15's own first
 * draft released this row and an adversarial review refuted it the same day.
 *
 * WHAT IT DOES: pushes nothing, marks the index, increments a counter. It
 * touches neither `live` nor `n_free` nor `minted`, so both structural
 * identities hold with no arithmetic — a stranded index is still LIVE, because
 * nobody got it back. The mark is what makes a double strand and a later
 * release catchable, and it is the difference between a mechanism and an early
 * return: without it, stranding is unobservable and both of those are silent.
 *
 * HARD ERRORS, BOTH CONFIGURATIONS: an index that was never minted, one
 * already stranded, one sitting on the free list.
 *
 * IT DOES NOT REPORT. D15 §3 asks that the first occurrence name the HANDLE on
 * stderr, and this module is Layer 0 and has never heard of a handle. That
 * report belongs to the free path, where `h` is in scope. */
void cq_qubits_strand(cq_qubit_pool *p, uint32_t q);

/* WHICH index leaked, not just how many. A count is not an identification —
 * strand the wrong index and every total still agrees — and after cq_reg_free
 * tombstones the rail the index is unrecoverable from the register side, so
 * this predicate is the only thing that can name it. */
int      cq_qubits_is_stranded(const cq_qubit_pool *p, uint32_t q);
uint32_t cq_qubits_stranded   (const cq_qubit_pool *p);

/* The retirement counterparts. A retired index is LIVE and is neither free nor
 * stranded; the three states are disjoint. Kept apart from the strand counters
 * because `bd 06t`'s residue report reads those, and a qec run would otherwise
 * report every ordinary clean free as unproven residue. */
int      cq_qubits_is_retired(const cq_qubit_pool *p, uint32_t q);
uint32_t cq_qubits_retired   (const cq_qubit_pool *p);

uint32_t cq_qubits_live(const cq_qubit_pool *p);
uint32_t cq_qubits_peak(const cq_qubit_pool *p);
uint32_t cq_qubits_minted(const cq_qubit_pool *p);
uint32_t cq_qubits_free(const cq_qubit_pool *p);

/* Whether an index is currently on the free list. An index that was never
 * minted is not free — it does not exist. */
int cq_qubits_is_free(const cq_qubit_pool *p, uint32_t q);

#endif /* CQOPS_QUBITS_H */
