/* src/kernels/addacc.h — M15, Step 15. K8, the Cuccaro in-place accumulator.
 *
 * Read docs/constructions/K08.md before changing anything here. Every gate is
 * `lower_add_cuccaro!` (third_party/bennett/src/adder.jl:64-146) transcribed in
 * order; the W=1 path is the one thing that is NOT a port (§5 D1 — upstream
 * falls back to the OUT-OF-PLACE ripple there, which is not an accumulator).
 *
 * THIS IS NOT A RULE 7 KERNEL AND IT MUST NEVER BE USED AS ONE. Rule 7 fixes
 * the SEMANTICS — `dst ^= f(sources)`, sources unchanged, ancilla clean — and
 * K8 satisfies none of the first two: it is `acc += b`, in place and
 * DESTRUCTIVE, and it transiently writes the addend as well. Four independent
 * reasons it may never be substituted for K6/K7 (K08.md §5, in decreasing
 * severity):
 *
 *   1. The shape is wrong. `acc += b` is not `dst ^= f(a,b)`.
 *   2. Its uncompute is the REVERSE CIRCUIT, not a re-run. Running K8 twice
 *      gives `acc + 2b`, not `acc`. CQ_lang's `_unc` contract is "recompute
 *      from the still-live sources and XOR", which is `f ^ f = 0` for K6 and
 *      is simply wrong here. Substituting K8 for K6 is a SILENT MISCOMPILE:
 *      the forward value is right and only the `_unc` is wrong.
 *   3. It destroys a live SSA operand, and upstream has no guard against that
 *      any more — `op2_dead` is computed at arith.jl:204-205 and never read by
 *      `_pick_add_strategy` (arith.jl:20-26). We run no liveness analysis
 *      either, so there is no deadness proof to inherit.
 *   4. It is more expensive both ways at every width the ABI uses: sandwiched
 *      `13W-10` against ripple's `11W-4` for W >= 4, and ~2x the Toffoli depth.
 *
 * K8's one advantage is the ANCILLA COUNT — `1`, against `W` for ripple — which
 * is worth having exactly where the accumulator is a long-lived scratch
 * register inside a bigger sandwich. That is K11 (M18, Step 16).
 *
 * K8 HAS A SECOND CALLER SINCE 2026-08-23, AND IT IS NOTHING LIKE K11 (PRD §15
 * D17, `bd dzj`). `shim/cq_runtime_rail.c`'s `rail_addc` — the body of
 * `cqrt_addc_i<W>`, reached straight from CQ_lang's frozen ABI — runs this
 * accumulator IN PLACE and NOT inside a sandwich, because `cqrt_addc` is
 * `h := (h + imm) mod 2^W` and libcqops has no other in-place adder. Four
 * statements in this header were written when K11 was the only caller and are
 * corrected below where they occur. The one to carry: with no sandwich there is
 * no I6(b), so that caller materialises every operand and the ancilla itself,
 * and disposes them itself. PRD §6, "K11 uses Cuccaro, and this is a deliberate
 * delta from upstream" (PRD-v1.md:645 @ 961905f), records the decision to
 * substitute Cuccaro into the multiplier, since `multiplier.jl:29`
 * calls the ripple `lower_add!` and no upstream construction composes shift-add
 * with Cuccaro. K08.md §5 D2 still presents that as an OPEN fork; it is not,
 * and the PRD is the authority.
 *
 * i6_ok = false, AND THAT IS THE WHOLE REASON THIS HEADER IS LONG. I6 says
 * every gate target inside a compute half is a bit of the scratch region;
 * Bennett stores the carry chain in the ADDEND's wires (adder.jl:100, "compute
 * carries into a[] wires") and restores them in phase 3, so at every W >= 3
 * eight gate families target `b`. The end state of `b` is unchanged — upstream
 * pins it at test_gboa_dirty_bit_hygiene.jl:74 — but I6 is a PER-GATE
 * predicate, not an end-state one. Three consequences, all enforced below:
 *
 *   (a) `b` IS NON-CONST (K08.md D4). This is the only operand in the whole
 *       kernel catalogue that cannot be `const cq_bit *`, and it deletes the
 *       by-construction half of the I6 defence for this one module — cq_emit_*
 *       taking controls as `const cq_bit *` is what normally makes a source
 *       unmaterialisable. What is left is the Debug scratch-extent assertion in
 *       src/emit.c, which is therefore LOAD-BEARING here in a way it is not
 *       anywhere else.
 *   (b) EVERY BIT OF acc, b AND x MUST ALREADY BE ON A QUBIT — hard error in
 *       both configurations, `cq_addacc_check` below. See the note on it.
 *   (c) A caller inside a sandwich must have acc, b and x all inside its own
 *       scratch extent. K11 satisfies this: the accumulator, every partial
 *       product and every Cuccaro ancilla are bits of one pre-materialised
 *       region (K11.md §2b, §4).
 *
 *       "ANYWHERE ELSE THE DEBUG EXTENT CHECK FIRES, AND IT IS RIGHT TO" — this
 *       clause said that until 2026-08-23 and it is FALSE, which matters
 *       because it named the backstop a second caller would rely on.
 *       `check_target` (src/emit.c) is inside `#if CQOPS_DEBUG_INVARIANTS` AND
 *       is conditioned on `ctx->scratch_lo` being non-NULL, which only
 *       `cq_sandwich` sets. Outside a sandwich it is inert in BOTH
 *       configurations, so for M26's `rail_addc` the ONLY guard is
 *       `cq_addacc_check` below.
 *
 * THE ANCILLA IS SUPPLIED BY THE CALLER, NOT ALLOCATED HERE, and that is
 * forced rather than tidy (K11.md §2b's interface note, K08.md §4's 2026-08-15
 * settlement). Bennett allocates it inside the function (`X = allocate!(wa, 1)`,
 * adder.jl:69). We cannot: an allocation in the middle of a compute half is a
 * scratch bit that step 0 did not pre-materialise, which breaks I6(b) for the
 * very wire K11.md's R8 trace indicts — `x` is read as a control at adder.jl:104
 * while still BIT_ZERO and materialised as a target at adder.jl:142, so the
 * reverse pass would emit gates the forward never did while L1 stayed green.
 * It also settles who frees it: nobody here — but WHO the caller is decides how.
 * For K11 `x` is a bit of the caller's scratch region and goes back to the pool
 * through cq_sandwich's epilogue on the CQ_ZERO_BY_PALINDROME premise. For
 * M26's `rail_addc` (PRD §15 D17) there is no sandwich and no region: the
 * ancilla is a one-bit REGISTER the wrapper mints, materialises and frees
 * itself, on its own `CQ_ZERO_BY_CUCCARO_RESTORE` premise — which rests on the
 * same upstream hygiene contract 3 this file already cites.
 */
#ifndef CQOPS_KERNELS_ADDACC_H
#define CQOPS_KERNELS_ADDACC_H

#include "bit.h"
#include "ctx.h"

/* The operands, gathered so the step function can be indexed by one integer.
 *
 * NAMING IS INVERTED FROM BENNETT AND THE INVERSION IS THE FIRST THING TO GET
 * WRONG. libcqops `acc` is Bennett's `b` (the accumulator, overwritten with the
 * sum, adder.jl:62 "b overwritten with a+b mod 2^W"); libcqops `b` is Bennett's
 * `a` (the addend, restored); libcqops `x` is Bennett's `X[1]`. Reading a
 * transcription with the letters swapped back gives a circuit that computes the
 * sum into the wrong register and still looks like the source.
 *
 * `b` is NON-CONST — see (a) in the file header. `acc` and `b` are W bits each;
 * `x` is ONE bit and only x[0] is ever touched. */
typedef struct {
    cq_bit *acc;   /* Bennett's b: acc += b lands here          */
    cq_bit *b;     /* Bennett's a: the addend, restored on exit */
    cq_bit *x;     /* Bennett's X[1]: one caller-owned ancilla  */
    int     W;
} cq_addacc_block;

/* How many steps the construction has at this width — ONE GATE PER STEP, so
 * this is also the gate count. `6W - 5` for every W >= 1.
 *
 * THE CLOSED FORM IS RIGHT AT W=1 BY COINCIDENCE AND THE COMPONENTS ARE NOT.
 * At W=1 this returns 1, which is the correct step count, but the per-type
 * split is (0 X, 1 CX, 0 CCX) and not `4W-2 = 2` CX with `2W-3 = -1` CCX. K8's
 * W=1 path is a re-derivation, not a port: `acc += b mod 2` is `acc[0] ^= b[0]`,
 * one CX and no ancilla, where Bennett short-circuits at adder.jl:66 to the
 * out-of-place `lower_add!` and returns a fresh register (K08.md §5 D1). Pin
 * W=1 as (0, 1, 0) explicitly; never evaluate 4W-2 / 2W-3 there. */
int cq_addacc_steps(int W);

/* ONE GATE OF THE CONSTRUCTION, `u` in [0, cq_addacc_steps(W)).
 *
 * THIS IS THE ENTRY POINT A SANDWICHED CALLER USES, AND THE ONLY ONE. Rule 8's
 * driver reverses STEP order and re-calls compute(env, s) with the same index,
 * so a step must be an involution — a single X/CX/CCX always is, and a
 * multi-gate block generally is not. A caller that made one whole
 * `cq_kernel_addacc` into one step would have the reverse pass RE-RUN the
 * accumulate forwards, giving `acc + 2b` while every gate count stayed
 * plausible. Same shape as bd ckd.14a's two worked witnesses (K06.md:566-586,
 * K10.md:153-171), one level up.
 *
 * K11 offsets into this: its own step index maps a contiguous run of `6W-5`
 * indices onto `u`, exactly as M12's barrel does with cq_mux_step.
 *
 * CHECKS THE PRECONDITIONS AT `u == 0`, which is where they can be checked at
 * all: a sandwiched caller never reaches cq_kernel_addacc, and before the
 * driver's step 1 the operands are still CQ_BIT_ZERO, so an entry-time check
 * outside the compute half would fire on every legitimate use. */
void cq_addacc_step(cq_ctx *ctx, const cq_addacc_block *k, int u);

/* Every step, in order — `acc += b`, `b` restored, `x` back to |0>.
 *
 * FOR A CALLER THAT IS NOT INSIDE A SANDWICH. Inside one, use cq_addacc_step
 * (see above); this function emits a whole accumulate and a driver replaying it
 * as a single step would not undo it. It exists because K8 is CLEAN — no
 * copy-out, no reverse half, nothing to wrap — so "run the construction" is a
 * meaningful operation in its own right, and because the suite needs to drive
 * the value semantics without a sandwich in the way.
 *
 * Checks its preconditions first, both configurations. */
void cq_kernel_addacc(cq_ctx *ctx, const cq_addacc_block *k);

/* The preconditions, hard errors in BOTH configurations. Called by both entry
 * points above; exposed so a caller can assert them once per call site rather
 * than per step.
 *
 * NO PRODUCTION CALLER TODAY, AND THAT IS A CORRECTION (Step 16). This used to
 * say "exposed so K11 can assert them". M18 shipped and does not: it drives
 * `cq_addacc_step`, whose `u == 0` check already runs exactly once per
 * accumulate, and a second call from mul.c would be a guard whose deletion turns
 * no test red. The export stays because it is the shape K08.md §4 specifies and
 * because a future non-stepping caller needs it; do not "clean it up", and do
 * not add a redundant call to make the comment true.
 *
 * WHY "EVERY BIT IS ALREADY A QUBIT" IS THE CHECK, and why it is not the L5
 * classical short-circuit every other kernel has. K8 STILL HAS NO L5, and the
 * reason changed on 2026-08-23 rather than going away. It used to be that K8
 * "has no cqrt_* opcode, is never entered from CQ_lang, and its only v1 caller
 * hands it pre-materialised scratch"; the first two clauses became false when
 * PRD §15 D17 made `cqrt_addc_i<W>` a caller. What is true now is that K8's L5
 * LIVES IN M26's WRAPPER, which is where the ABI boundary is: `rail_addc` folds
 * an all-classical rail at zero gates and zero qubits and only then reaches
 * this kernel, materialising every operand on the way. So a classical bit
 * arriving HERE is still a caller bug rather than a cheap case. It is
 * also an ACTIVE HAZARD, in the two ways K08.md §2 consequence 3 and K11.md's
 * R8 trace name:
 *
 *   - a classical `b[i]` is a gate TARGET here, so the fold table materialises
 *     it mid-sequence. If the constant was 1 that emits an X the reverse pass
 *     will not emit, because kinds are monotone (D6, no demotion) and the bit
 *     is already Q on the way back. The sandwich stops cancelling while L1
 *     stays green — risk R1 exactly.
 *   - a classical ZERO `x` is read as a CONTROL at step 2 and folds to nothing,
 *     then is materialised as a TARGET near the end. The reverse pass emits
 *     what the forward folded away. That is R8's antecedent, and it is the
 *     witness K11.md §2b traces gate by gate.
 *
 * Both are silent. Refusing the input is the only honest option, and it is what
 * makes the L4 golden a function of W alone.
 *
 * It also checks pairwise disjointness of acc, b and x, by RANGE rather than by
 * base pointer, for kernel.h's reason: cq_scratch_span hands out sub-arrays of
 * one region, so partial overlap is representable and is the shape a K11 layout
 * bug would take. acc and b are BOTH targets here, so an overlap between them
 * is not merely undefined — it is two carry chains sharing wires. */
void cq_addacc_check(const cq_addacc_block *k);

#endif /* CQOPS_KERNELS_ADDACC_H */
