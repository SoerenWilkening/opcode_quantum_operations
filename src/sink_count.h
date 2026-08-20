/* src/sink_count.h — M24: the counting sink. PRD §8.
 *
 * Per-kind totals, and nothing else. This is the mechanism behind every L4
 * gate-count golden in Phase B (bd 6n0), so what it does NOT compute is as
 * load-bearing as what it does.
 *
 * WHY THIS IS NOT tests/support/mock_sink WITH A DIFFERENT NAME. The mock also
 * counts per kind (its `by_op`), and for a test that is the better tool — it
 * records the ORDERED stream, which is what the sandwich palindrome check
 * needs and what a bare count can never see. Two things separate them, and
 * both are the reason PRD §8 ships a counter at all. It lives in `src/`, so
 * `CQOPS_SINK=counter` can select it on a real CQ_lang fixture with no test
 * harness present; and it is O(1) in memory, where the mock allocates one
 * `cq_rec` per gate — K12 at W=64 is a circuit you do not want to hold in RAM
 * just to learn how big it is.
 *
 * IT DOES NOT COMPUTE PEAK QUBITS, AND THAT IS THE CORRECTED ROW, NOT AN
 * OMISSION. IMPLEMENTATION_PLAN §3 lists M24 as "per-kind totals, peak live
 * qubits, T-count", and PRD §8 says the counter sink matches Bennett's
 * `gate_count` / `ancilla_count`. Three facts, established at Step 9, say the
 * qubit half of that row cannot and need not live here:
 *
 *   1. Bennett's `peak_live_wires` (third_party/bennett/src/diagnostics.jl:206)
 *      SIMULATES the circuit — it walks a `bits` vector, applies every gate,
 *      and counts simultaneously non-zero wires. Rule 13 forbids a simulator
 *      "anywhere — not now, not as a test convenience". So that number is not
 *      ours to reproduce, here or in tests/. It would also be MEANINGLESS if
 *      it were legal: `bits = zeros(Bool, c.n_wires)` (diagnostics.jl:209) is
 *      the all-zero-input run, while our operands are routinely CQ_BIT_ONE —
 *      and a qubit in superposition has no Bool value to be non-zero at all.
 *   2. Bennett's `ancilla_count` is `length(c.ancilla_wires)`
 *      (diagnostics.jl:36) — a property of a circuit OBJECT. Rule 13 says the
 *      library holds no circuit object, and a streaming sink sees a gate and
 *      forgets it. There is nothing to take the length of.
 *   3. The pool already has the number, exactly. src/qubits.h:12-20 records the
 *      identity `peak == minted`: a fresh index is minted only when the free
 *      list is empty, i.e. only when `live` has already reached `minted`, so
 *      the monotonic counter IS the high-water mark. `cq_qubits_peak()` has
 *      returned it since Step 4, and qubits.h says so in as many words —
 *      "PRD §8's 'peak qubits' needs no second mechanism to compute". (The
 *      value is maintained in both configurations; only the ASSERT that the
 *      identity holds is Debug-gated, so under Rule 17 do not report the
 *      identity as verified from a Release run.)
 *
 * AND NOTE WHAT `cq_qubits_peak()` IS NOT: it is peak ALLOCATED, where
 * Bennett's is peak NON-ZERO — a function of the wire VALUES, so a wire sitting
 * at 0 does not count towards it. On our circuits the gap is not a rounding
 * error: `cq_sandwich` pre-materialises the whole scratch region at zero gates
 * and every one of those qubits is born |0⟩. The two numbers are therefore
 * different quantities on the same circuit, and PRD §8's "matching
 * `gate_count` / `ancilla_count` in Bennett.jl" was a category error for this
 * figure however it is computed. The corrected PRD drops the Bennett
 * comparison for the qubit count rather than re-pointing it; do not restore it.
 *
 * A SINK COULD ONLY EVER GUESS AT IT, AND THE GUESS WOULD BE LOW. The one
 * quantity derivable from a gate stream is `max operand index + 1`, and that
 * is a LOWER BOUND, not the peak: cq_materialise takes a qubit from the pool
 * and emits NO gate when the constant was 0 (Rule 5), so a qubit that is
 * allocated, pre-materialised as scratch (I6(b)) and never touched by a gate
 * is invisible here. Do not add such a field. A number that is silently
 * smaller than the truth is worse than no number, because the D2 pool ceiling
 * is what stands between us and over-committing a QEC device.
 *
 * WHAT `total` MEANS. Bennett's `gate_count` returns `(total, NOT, CNOT,
 * Toffoli)` where `total` is the redundant sum of the other three
 * (diagnostics.jl:21-26); CLAUDE.md's resolved note pins the three-tuple and
 * carries `total` as a checksum. Our vtable has six entries, three of which
 * (`ry`, `rz`, `mz`) are not Bennett gates at all — so cq_count_total sums
 * ONLY x + cx + ccx. Including rotations would make the number stop matching
 * Bennett's baselines the moment §7 fires, which is precisely the comparison
 * PRD §8 asks the counter sink to make possible. The rotation and measurement
 * counts are kept, individually, and stay out of the sum.
 */
#ifndef CQOPS_SINK_COUNT_H
#define CQOPS_SINK_COUNT_H

#include "cqops/cqops.h"

#include <stdint.h>

/* One counter per vtable entry, named for it. Six fields rather than an
 * indexed array so that a mis-wired entry is a compile-time-visible typo
 * rather than an off-by-one in a subscript. `uint64_t` because K12 at W=64 is
 * a large circuit and a 32-bit count is an avoidable cliff. */
typedef struct {
    uint64_t x, cx, ccx, ry, rz, mz;
} cq_counter;

/* Zeroes every field. Doubles as the reset between two measurements — there is
 * no other state, which is the point of keeping this module a plain struct. */
void cq_count_reset(cq_counter *c);

/* A vtable bound to `c`. Returned by value, like cq_mock_sink: the caller owns
 * it and may hold several at once, which is what proves nothing here is
 * global. Aborts if `c` is NULL — a counter sink with nowhere to count is a
 * gate dropped on the floor. */
cq_sink cq_sink_counter(cq_counter *c);

/* Registers a process-global counter under the name "counter", so CQOPS_SINK
 * can select it with no call into the library, and returns that instance so a
 * caller can read the totals back. See cq_sink_printf_register on why
 * installation is explicit rather than lazy. */
cq_counter *cq_sink_counter_register(void);

/* The Bennett-comparable checksum: x + cx + ccx, and deliberately NOT ry, rz
 * or mz. Read the header note before "fixing" this to sum all six. */
uint64_t cq_count_total(const cq_counter *c);

/* T-count for fault-tolerant synthesis: 7 per Toffoli, NOT and CNOT being
 * Clifford. Verbatim from Bennett's `t_count` (diagnostics.jl:122).
 *
 * A LOWER BOUND SINCE STEP 19, and unlike cq_count_total's exclusion of ry/rz/mz
 * this one is NOT a deliberate Bennett-comparability choice — it is the honest
 * limit of a formula ported from a gate set that has no rotations. `Ry(θ)` at a
 * general θ is not Clifford and costs O(log 1/ε) T gates under synthesis, and
 * this returns 7·Toffoli regardless. It stays exact for everything Bennett can
 * express, which is every kernel; it under-reports the moment §7's general rows
 * fire. Making it exact needs a synthesis model and a target ε, which is the QEC
 * sink's business (§7: the `Ry` entry stays `double` all the way down), not
 * this counter's. Do not "fix" it by folding ry/rz into the Toffoli term. */
uint64_t cq_count_t(const cq_counter *c);

#endif /* CQOPS_SINK_COUNT_H */
