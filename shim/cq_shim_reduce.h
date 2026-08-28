/* shim/cq_shim_reduce.h — the PORTED reduction engine. PRD §15 D15 §2.
 *
 * "PORT THE REDUCTION, DO NOT RE-DERIVE THE PARITY — Rule 1's posture applied
 * to an ANALYSIS rather than to a circuit." The source is
 * `third_party/cq_free_pairing/free_pairing_check.py`, vendored at a pinned
 * revision on 2026-08-27 with its own COMMIT and a sha256 the CONFIGURE checks
 * (`cmake/CqopsFreePairingPin.cmake`). It was not on disk before that:
 * `reduces_to_identity`, `unchanged_over`, `pair_operands_unchanged` and
 * `co_written_stable` appeared NOWHERE under `third_party/`, so Rule 1's "it
 * must be on disk at a pinned commit before any kernel is written" was false
 * for the very thing `bd 06t` instructed an implementer to port. The pin's
 * COMMIT carries that reasoning; this header carries the port.
 *
 * ONE ENGINE, NOT THREE RULES, AND THAT CHANGES THE SHAPE RATHER THAN THE
 * WORDING. D15 §2 records that U1 was first written as "neither R nor any
 * source is written between", and that measured against the corpus this is far
 * too strict — hundreds of shipped frees have an intervening write to R. The
 * repair is that U1's EMPTINESS condition IS the REDUCTION condition, so U1,
 * U2 and U3 are three ENTRY CONDITIONS into one engine. An implementation that
 * builds three separate checkers gets U1 wrong in exactly that way and
 * UNDER-discharges — "which presents as a qubit leak rather than as a
 * miscompile, so no test will shout."
 *
 * FIVE THINGS THE PORT KEEPS EXACTLY, each because dropping it is a real bug:
 *
 *  1. STRICT `lo < p < hi`. This one character is the termination proof.
 *     `unchanged_over` reduces exactly the writes strictly inside its window
 *     and never widens it, so every recursive query narrows. Relaxing either
 *     end to `<=` sends every `cqrt_cswap`-bearing history into unbounded
 *     recursion. It is enforced in `cq_rec_writes_in`, once.
 *  2. THE RE-ENTRY TRIPWIRE, ON BOTH GUARDS. Re-entering the same
 *     `(name, lo, hi)` query is an ASSERTION on the termination argument, not a
 *     policy: upstream raises rather than answering "false", because a silent
 *     false is an over-decline whose value depends on the order queries happen
 *     to run in. Upstream also records that a tripwire on only ONE of the two
 *     guards is not a tripwire, so both ship.
 *  3. TOP-DOWN SCAN AND GREEDY COMMIT. The stack of unpaired writes is scanned
 *     nearest-adjoint-first and an accepted pair is erased with no backtracking.
 *     Each cancellation is individually a sound operator rewrite, so greed can
 *     only cause FALSE DECLINES, never false accepts — that is the whole
 *     defence of having no search, and it is why the engine is INCOMPLETE by
 *     construction and must not be "improved" into one without an argument.
 *  4. A REJECTED PAIR DOES NOT STOP THE SCAN. `continue`, never `break`: an
 *     earlier adjoint deeper in the stack may still pair.
 *  5. UNPAIRED WRITES ARE PUSHED, NEVER DROPPED. A non-empty stack at the end
 *     is a decline. A deleted uncompute fails exactly here.
 *
 * COMMUTATION IS LIMITED TO COMPLEMENTARY FLAG BRANCHES AND NOTHING ELSE. Not
 * diagonal-with-anything, not disjoint support, not same-symbol. Two writes
 * commute iff they share a control HANDLE whose `cqrt_x` parity differs between
 * them, that handle is one this stream minted, and nothing but `cqrt_x` touched
 * it strictly in between. It is an ACCEPT arm: dropping it does not admit a
 * wrong build, it REJECTS a correct one.
 *
 * AND THE TWO `commutes` CALL SITES USE DIFFERENT INTERVALS, DELIBERATELY. The
 * reducer compares an intervening write against the LATER pair member; the
 * co-written check compares it against the EARLIER one. Upstream records that
 * an earlier draft called the second redundant and was refuted with a runnable
 * input: one non-`cqrt_x` write on the flag placed on a single side makes the
 * two answers differ. Do not unify them.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * WHERE THE PORT DELIBERATELY DIVERGES, AND WHY EACH DIVERGENCE IS SAFE.
 * Upstream runs over CQ_lang's LOWERED LLVM IR, where a rail is an SSA name and
 * the loop algebra is still present; we see a CALL STREAM of opaque handles.
 * Every divergence below is toward UNPROVEN, never toward proven-clean, because
 * an over-discharge here recycles a non-|0> index — the one unforgivable bug.
 *
 * (a) THE OBLIGATION IS DIFFERENT, AND THIS IS THE ONE THAT WOULD HAVE BITTEN.
 *     Upstream's rules prove "a KNOWN CLASSICAL BASIS STATE, unentangled" —
 *     because ITS free is a hardware reset that collapses nothing on a basis
 *     state. Its A-rules explicitly accept a rail returning to its ALLOC-TIME
 *     value `v`, not to zero. Rule 6 needs |0>. A faithful transcription would
 *     therefore hand |v> indices to the free list. The port carries the BIRTH
 *     VALUE alongside the reduction and requires it to be zero — which is not a
 *     patch but the mechanism that makes `bd ckd.18` PROVABLE rather than
 *     merely unprovable: `alloc_i32(5); ry(θ); ry(−θ); free` reduces to the
 *     identity and lands on |5>, so the same engine that CLEARS a template rail
 *     CONVICTS that one. D15 §4's carve-out and D15 §3's residue split are the
 *     same fact read twice, and this is where both come from.
 *
 * (b) NO LOOK-AHEAD, SO UPSTREAM'S R1 IS REPLACED RATHER THAN DROPPED.
 *     Upstream's R1 refuses a rail that carries a non-diagonal rotation AND is
 *     transitively correlated with a "kept carrier", computed by scanning the
 *     WHOLE function — information that does not exist when `cqrt_free` arrives.
 *     Dropping it would WIDEN us. The port refuses instead on a PAST-ONLY
 *     witness: a rail carrying a non-diagonal rotation that was READ by any
 *     call after that rotation is UNPROVEN. That is strictly stronger — a rail
 *     reaches upstream's `recorded` set only by being copied or taped, which is
 *     a read, and the ancestor closure only adds rails that were read as
 *     sources — so nothing upstream refuses is discharged here.
 *
 *     IT ALSO CLOSES A HOLE UPSTREAM LEAVES OPEN, and that is worth stating
 *     because it is the only place the port is SOUNDER than its source. D15 §5
 *     demonstrates `ry(R,t) … <a CNOT-class READ of R> … ry(R,−t)` cancelling
 *     upstream at exit 0 while R is left entangled with the reader, "because
 *     the reduction reasons over WRITES to R and never asks what read it".
 *     Here that shape is a READ after a rotation, so it is UNPROVEN. What is
 *     still NOT covered is the same shape with no rotation anywhere, which is
 *     the general entanglement class Rule 13 forecloses instrumenting; D15 §5
 *     says so and this port does not claim otherwise.
 *
 * (c) THE CLASSICAL-IMMEDIATE SLOTS NEED NO EVIDENCE HERE. Upstream cannot tell
 *     an `xorc` immediate from a handle in the IR at i32 — both are `int32_t` —
 *     and had to add `provably_not_a_rail` after a live hole (its own `az1h`).
 *     Our effect table declares the slot from the frozen ABI's DECLARATION, and
 *     the shim's own entry point consumes it arithmetically. There is no
 *     ambiguity to resolve, so the guard is absent rather than weakened.
 *
 * (d) HANDLE IDENTITY IS STRONGER THAN SSA IDENTITY, so three of upstream's
 *     documented aliasing holes cannot arise: a `select`/`phi` laundering a
 *     rail into a fresh name, a callee receiving one handle twice, and a
 *     cross-function escape. A flattened runtime stream is simply more calls.
 *
 * (e) THE MEMOS ARE NOT PORTED AND THE BOUND IS EXPLICIT INSTEAD. Upstream
 *     measured `co_written_stable` as EXPONENTIAL without its cache — 44.6 s on
 *     a 63-call module — and that cache is load-bearing for termination in
 *     practice rather than a speed-up. The port bounds the recursion DEPTH
 *     explicitly and answers UNPROVEN when the bound is reached, which is the
 *     safe direction and is a decline the caller can see. That is a real
 *     divergence: upstream refuses to over-decline by budget on principle. It
 *     is taken because only `cqrt_cswap` reaches the co-written path at all
 *     (every other opcode's single write slot IS the target), the v1 surface
 *     has one such opcode, and a build gate a caller can hang is worse here
 *     than a strand.
*
 * (f) UPSTREAM'S "NO REVERSAL" ESCALATION IS NOT INHERITED, AND THE REASON IS
 *     THE RESIDUE SPLIT RATHER THAN SOUNDNESS. Upstream turns a minted rail
 *     with no reversal from UNPROVEN into a VIOLATION when the rail's own
 *     writes net to identity, on the ground that it "DEMONSTRABLY still holds
 *     f(args) at the free". That is a correct statement about ITS obligation —
 *     a known classical basis state — because `f` of unknown quantum arguments
 *     is not one. It is NOT a statement that the rail is non-zero, and OUR
 *     conviction row means exactly that: Rule 6's real content is that the
 *     library can SEE the rail is not |0>. `and(a,b)` with `a = b = 0` is |0>.
 *
 *     So a template forward with no `_unc` answers UNPROVEN here, not DIRTY.
 *     THE ACT IS THE SAME under D15 §4 — both strand — so nothing is released
 *     that should not be; what would be damaged is the REPORT, which is the one
 *     thing `bd 06t` exists to produce. A verdict that convicts on "I cannot
 *     see a reversal" makes the proven-dirty row mean "unproven, loudly", and a
 *     maintainer reading the split would be told the library had seen something
 *     it had not. The shadow still convicts this shape whenever it CAN see the
 *     value, which is the division of labour cq_shim_free_proof exists for.
 * ───────────────────────────────────────────────────────────────────────── */
#ifndef CQ_SHIM_REDUCE_H
#define CQ_SHIM_REDUCE_H

#include <stdint.h>

#include "cq_shim_record.h"

/* Does `h`'s recorded write history strictly inside (lo, hi) reduce to the
 * IDENTITY? Returns 1 for yes, 0 for "not provably" — never a third value: the
 * engine is SUFFICIENT and never necessary (a Bennett-style uncompute that
 * zeroes by RECOMPUTING its predicate is genuinely correct and genuinely
 * undecidable here), so a 0 means ignorance and the CALLER decides what that
 * costs. Mapping 0 onto a conviction would be exactly the over-claim upstream
 * warns about. */
int cq_reduce_to_identity(int32_t h, uint32_t lo, uint32_t hi);

/* Test seam: how many times the depth bound was reached since the last reset.
 * Exposed so the bound is a TESTED decline rather than an invisible one — a
 * decline nobody can see is indistinguishable from a rule that never fired. */
uint32_t cq_reduce_depth_declines(void);
void     cq_reduce_reset_stats(void);

#endif /* CQ_SHIM_REDUCE_H */
