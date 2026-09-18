/* src/kernels/mul.h — M18, Step 16. K11 `mul`, W-bit truncated.
 *
 * Read docs/constructions/K11.md before changing anything here. The skeleton is
 * Bennett's `lower_mul!` = `lower_mul_wide!(…, W, W)`
 * (third_party/bennett/src/multiplier.jl:1-33) ported literally; the inner
 * adder slot is the ONE place libcqops departs from upstream.
 *
 * THE HEADLINE DELTA: multiplier.jl:29 calls the OUT-OF-PLACE ripple
 * `lower_add!`, and we substitute the IN-PLACE Cuccaro accumulator K8 (M15,
 * src/kernels/addacc.c) into that slot. Decided 2026-08-14, recorded in PRD §6,
 * "K11 uses Cuccaro, and this is a deliberate delta from upstream"
 * (PRD-v1.md:645 @ 961905f), and in K11.md §2 / §5 delta 4. It is a derived
 * composition, not a port: the string `cuccaro` does not occur in multiplier.jl,
 * no upstream dispatch path composes shift-add with Cuccaro, and no Bennett test
 * exercises it. What that buys is the scratch region — `W² + 2W` against ripple's
 * `3W² + W`, i.e. 1088 qubits at i32 rather than 3104 — plus `2W` fewer
 * Toffolis. What it costs is the upstream cross-check: K11's L4 golden is
 * SELF-PINNED and there is no published figure to reconcile it against.
 * K11.md Appendix A survives solely because the ripple variant does reconcile,
 * at residual zero, and that check covers the SKELETON only.
 *
 * THE RULE 7 OBJECTION TO CUCCARO DOES NOT REACH HERE. K8 is barred from K6/K7
 * because its uncompute is the reverse circuit rather than a re-run, which does
 * not match CQ_lang's `_unc` contract. Inside K11 the accumulator is internal:
 * created, used and destroyed between one `cq_kernel_mul` entry and its return,
 * never exposed to `_unc`. CQ_lang's uncompute re-enters this function from the
 * top, re-runs the whole sandwich, and gets `f ⊕ f = 0`.
 *
 * THE ACCUMULATE IS 6W-5 STEPS, NOT ONE, AND TRANSCRIBING IT AS ONE IS A SILENT
 * MISCOMPILE (bd rhp). K11.md §2b writes the accumulate on a single line for
 * readability while budgeting `6W-5` step slots for it twenty lines earlier.
 * `cq_sandwich` reverses STEP order and re-calls `compute(env, s)` with the SAME
 * index, so a step must be an involution; a whole Cuccaro accumulate is not, and
 * re-running it gives `accum + 2·pp[j]` (K08.md §5 point 2). The reverse half
 * would RE-ACCUMULATE rather than undo, leaving `accum` and every `pp[j]` dirty
 * AFTER `dst` was already copied out — so L1 stays green and only L2/L3 and the
 * palindrome can see it. mul.c therefore calls `cq_addacc_step`, never
 * `cq_kernel_addacc`, exactly as M12's barrel consumes `cq_mux_step`.
 *
 * THERE IS NO HIGH HALF TO LEAK, and that is structural rather than a slice.
 * CQ_lang's `mul` is same-width (opcode_table.yaml:186, no `mul_wide`, no
 * `umulh`), so we instantiate `result_width = W`, where multiplier.jl:26's
 * `dest > result_width && break` drops every partial-product Toffoli of weight
 * >= W before it is emitted and the accumulator emits no carry-out
 * (adder.jl:33-34). This is the decisive contrast with Bennett's `:qcla_tree`
 * strategy, whose truncation IS a slice and whose discarded half IS a dirty
 * ancilla (arith.jl:222-224) — a Rule 6 hard error here, which is why v1 ports
 * the `:shift_add` skeleton and not that one.
 */
#ifndef CQOPS_KERNELS_MUL_H
#define CQOPS_KERNELS_MUL_H

#include "bit.h"
#include "ctx.h"
#include "scratch.h"

/* K11 — `dst ^= (a · b) mod 2^W`. Rule 7's canonical shape, unchanged: two
 * sources of width W, a destination of width W, sources unchanged, every
 * internal ancilla back at |0>. Unlike K5, K9 and K10 this kernel needed no
 * departure from the parameter list at all, so it is storable in a
 * `cq_kernel_fn` and the shared Phase-B driver drives it with no adapter.
 *
 * Sandwiched: `(X, CX, CCX) = (0, 8W² − 3W, 5W² − 5W)`, total `13W² − 8W`, over
 * `W² + 2W` scratch qubits — at the ALL-QUANTUM operand mask, which is what the
 * L4 golden pins (K11.md §3). W = 1 is NOT that formula: see below.
 *
 * W = 1 IS A DELEGATION TO K2, AND THE CLOSED FORM IS WRONG THERE IN A WAY THAT
 * LOOKS RIGHT. `lower_add_cuccaro!` falls back to the out-of-place `lower_add!`
 * at W <= 1 (adder.jl:66) and stops being an accumulator at all, so the uniform
 * path is out of domain. Evaluating the sandwiched closed form at W = 1 gives
 * total `13 − 8 = 5`, and the uniform path really would emit five gates — but
 * the SPLIT is wrong both ways: the formula says `(0, 5, 0)` and the uniform
 * path emits `(0, 3, 2)`. Neither is the answer. `a·b mod 2` is `a ∧ b`, so
 * K11 at i1 is K2 with one Toffoli: `(0, 0, 1)`, no scratch, no sandwich
 * (K11.md §3, §5 note 9). That is a delegation to an existing catalogue entry,
 * not a new construction, so Rule 1 holds. This is the same shape as K08.md
 * D1's W = 1 trap one level up — there the total was accidentally right and the
 * components were not; here even the total belongs to a circuit we do not emit.
 *
 * THE ALL-CLASSICAL SHORT-CIRCUIT IS MANDATORY, NOT AN OPTIMISATION (risk R9,
 * plan §0.2 consequence 2, K11.md §4 point 3). I6(b) pre-materialisation is
 * unconditional once `cq_sandwich` is entered, so without the fold at the top
 * of the entry point a fully classical multiply would draw `W² + 2W` qubits —
 * 4224 at i64 — from the pool for an operation with no quantum input at all,
 * and L5's "zero gates and zero qubits fully-classical" would be false. This
 * kernel has by far the most to lose from a missing R9 check in the catalogue.
 *
 * D7b — `mul(dst, a, a)`, i.e. `x*x` — is REFUSED here, by
 * `cq_kernel_check_dst`. It is legal at the handle boundary and CQ_lang ships
 * it (`cq_template_mul_i32(h10, h10)`,
 * tests/e2e/slice_select_rail_alias_cond.expected.log:31 in the CQ_lang tree);
 * its remedy is M26's defensive `cqrt_copy` at that boundary (bd -493), in one
 * place. A kernel that sees the alias is looking at a missing copy — and here
 * it would emit phase-P Toffolis whose two controls are one physical qubit. */
void cq_kernel_mul(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);

/* HOW MANY COMPUTE-HALF STEPS AT THIS WIDTH — `(13W² − 9W)/2`, one gate slot
 * per step, and 0 at W = 1 where there is no sandwich at all.
 *
 * IT WAS EXPORTED FOR THE SUITE AND IT NOW HAS A CALLER TOO — that clause read
 * "NOT FOR A CALLER" until `cq_mul_step` landed beside it (PRD-v2 §7.10), and a
 * consumer needs exactly this number to size the run of its own step indices it
 * maps onto this block. The suite's use is unchanged and is still what pins the
 * closed form: `cq_mock_is_palindrome(m, n_head,
 * n_mid)` needs the compute half's exact length to check that the stream
 * mirrors around the copy-out, and that ordered check is the only instrument
 * with teeth against a non-cancelling reverse pass — a gate COUNT cannot see an
 * R8 divergence, because a reversed forward list can have a different multiset
 * with an identical total (Rule 10 L4 note 2). Deriving the length inside the
 * test from the same expression the kernel uses would make the check circular,
 * so the test pins the literal closed form AND calls this; the two agreeing is
 * itself an assertion. Do NOT size a step loop from Bennett's `sizehint!` at
 * multiplier.jl:20 (`W(W + 5·result_width)`, 384 at W = 8 against the real 380)
 * — it is an upper bound for a different adder. */
int cq_mul_steps(int W);

/* --- K11's compute half as a step block, exported for the v2 fp port. -----
 *
 * PRD-v2 §5's M34 row is "the 53x53 product is M18's `mul` over a span", and
 * §7.10 measures that the span was the only part that existed: M18 exported
 * the COUNT and not the step, so `soft_fmul`'s significand product had nothing
 * to compose. This is that export, and it is ADDITIVE — `cq_kernel_mul` now
 * dispatches through `cq_mul_step` and emits the same gates in the same order,
 * so tests/goldens/mul.counts did not move.
 *
 * A CONSUMER CALLS THE STEP FUNCTION, NEVER THE KERNEL. `cq_kernel_mul` is
 * itself a whole `cq_sandwich` and the driver REFUSES NESTING in both
 * configurations, so calling it from inside another compute half aborts before
 * allocating anything. That is the same fact M12 records for `cq_mux_step` and
 * M19 for `cq_sub_step`/`cq_ult_step`, and PRD-v2 §7.1 restates it for every
 * fp kernel: a `soft_*` routine is one sandwich whose compute half dispatches a
 * global step index into the ported blocks.
 *
 * THE BLOCK ALLOCATES NOTHING. `scr` is the CALLER's one contiguous region and
 * `off` is where this block's `cq_mul_region(W)` bits start inside it — M19's
 * shape exactly (divrem_u.h), and for M08's reason: emit.c's I6(a) check is a
 * pointer RANGE test over one extent, so a consumer allocates ONE region and
 * hands out offsets rather than allocating a second.
 *
 * THE LAYOUT IS THE ONE `mul.c` HAS ALWAYS HAD, relative to `off`:
 *
 *     [0, W)                      accum      -- ONE register, updated in place
 *     [W(1+j), W(2+j))            pp[j]      -- j in [0, W), never recycled
 *     [W(1+W) + j, W(1+W)+j+1)    x[j]       -- K8's ancilla, one per accumulate
 *
 * `pp[j]` cannot be recycled for `j+1`: Cuccaro RESTORES its addend
 * (adder.jl:50-51), so `pp[j]` still holds the partial product after the
 * accumulate. `x[j]` is PASSED IN rather than allocated inside K8 as Bennett
 * does at adder.jl:69 — an allocation in the middle of a compute half is a
 * scratch bit the driver did not pre-materialise, which is the wire K11.md
 * §2b's R8 trace indicts.
 *
 * `a` AND `b` ARE CONTROLS ONLY, so they may be the consumer's own operands,
 * or scratch spans an earlier block wrote, or a view that overlaps one — plan
 * §0.4 obligations 3 and 4, and what M34 needs, since `soft_fmul`'s `ma`/`mb`
 * are intermediates of the normalise step (fmul.jl:57-58) rather than rails.
 * They reach the emitter only through `cq_emit_ccx`'s `const cq_bit *` control
 * parameters, so neither can be materialised BY CONSTRUCTION.
 *
 * THE OPERANDS MUST BE DISJOINT FROM EACH OTHER AND FROM THE REGION, and this
 * block does NOT check it — `cq_kernel_mul`'s `cq_kernel_check_dst` runs at the
 * kernel boundary and a block has no `dst` to check. A consumer binding `a` and
 * `b` to one span emits phase-P Toffolis whose two controls are one physical
 * qubit: right value, malformed circuit, and in Debug M05's distinctness assert
 * is what speaks. That is D7b at the kernel boundary (CLAUDE.md), and a
 * consumer that sees the alias is looking at a missing copy.
 *
 * THERE IS NO R9 SHORT-CIRCUIT IN THE BLOCK, AND THAT IS NOT AN OVERSIGHT.
 * `cq_kernel_mul` folds an all-classical operand pair to zero gates and zero
 * qubits before it ever calls `cq_scratch_alloc`; a BLOCK is the compute half
 * and its region is already allocated and pre-materialised by the time step 0
 * runs, so there is nothing left to fold. A consumer handing this block two
 * all-classical spans still pays `W² + 2W` qubits and `W · cq_addacc_steps(W)`
 * gates for a product it could have computed in C. That is the same split K8
 * has one layer down — "K8's L5 lives in M26's wrapper" (addacc.h) — and it is
 * where M34's own fold belongs: at `soft_fmul`'s entry, over the whole routine,
 * not per block. */
typedef struct {
    const cq_bit *a, *b;   /* W bits each; CONTROLS ONLY, never targets       */
    cq_scratch   *scr;     /* the caller's region — the block allocates none  */
    uint32_t      off;     /* where this block's cq_mul_region(W) bits start  */
    int           W;
} cq_mul_block;

/* `W² + 2W` — accum W, pp W², x W (K11.md §4). ZERO at W = 1, matching
 * `cq_mul_steps`, and the pair is a trap worth reading before composing.
 *
 * THE BLOCK IS THE SANDWICHED CONSTRUCTION AND THERE IS NONE AT W = 1.
 * `lower_add_cuccaro!` falls back to the out-of-place `lower_add!` at W <= 1
 * (adder.jl:66) and stops being an accumulator, so `cq_kernel_mul` delegates
 * i1 to K2 — one Toffoli, no scratch, no sandwich (K11.md §3, §5 note 9). A
 * consumer that loops `off += cq_mul_region(W); n += cq_mul_steps(W)` therefore
 * gets a consistent 0/0 at W = 1 and NO PRODUCT: at that width it must emit
 * `CCX(a[0], b[0] -> its own product bit)` itself. `a·b mod 2` is `a ∧ b`, so
 * that is a delegation to K2 and not a construction of the consumer's (Rule 1).
 * Returning `W² + 2W = 3` here instead would hand a caller three qubits the
 * construction never touches; aborting would break a width-generic loop at the
 * one width the ABI does ship (opcode_table.yaml:186). */
int cq_mul_region(int W);

/* ONE GATE OF THE COMPUTE HALF, `u` in [0, cq_mul_steps(W)).
 *
 * ONE INVOLUTION PER STEP, and for K11 that is one GATE per step — the driver
 * reverses STEP order and re-calls the step with the SAME index, so a whole
 * Cuccaro accumulate made into one step would RE-ACCUMULATE on the way back and
 * leave `accum` and every `pp[j]` dirty AFTER `dst` was copied out, with L1
 * green (bd rhp, K11.md §2b). This function dispatches into `cq_addacc_step`
 * for exactly that reason.
 *
 * THE INDEX SPACE IS FLAT AND TWO-LEVEL: block `j` is `W − j` phase-P Toffolis
 * (multiplier.jl:26's `dest > result_width && break` is what makes it shrink,
 * and the Toffoli itself is :27) followed by
 * `cq_addacc_steps(W)` accumulate steps, so `start(j) = j(7W−5) − j(j−1)/2` and
 * the whole half is `(13W² − 9W)/2`. A consumer maps a contiguous run of its own
 * indices onto `[0, cq_mul_steps(W))`, as M19 does with `cq_ult_step`.
 *
 * Out of range is a hard error in BOTH configurations, for `cq_sub_step`'s and
 * `cq_ult_step`'s reason: a consumer's off-by-one lands inside the schedule and
 * emits a plausible WRONG gate rather than failing. Its message is disjoint
 * from `cq_addacc_step`'s so a death case can say which layer spoke. */
void cq_mul_step(cq_ctx *ctx, const cq_mul_block *k, int u);

/* WHERE THE PRODUCT IS AT THE END OF THE COMPUTE HALF: `accum`, the first W
 * bits of the block's region, holding `(a · b) mod 2^W`. It is `cq_kernel_mul`'s
 * own copy-out source, so a consumer reads the same wire the kernel does.
 *
 * `const`, AND THAT IS THE ENFORCEMENT RATHER THAN THE CONVENTION. Inside one
 * compute half the product is a CONTROL for whatever comes next; a consumer
 * that WROTE into it would make this block's reverse half non-cancelling —
 * scratch that never returns to |0>, which is R1 with the detector removed. The
 * type is what makes that unrepresentable, the same move by which emit.h's
 * `const cq_bit *` controls make I6(a) a fact of the type system. */
const cq_bit *cq_mul_product(const cq_mul_block *k);

#endif /* CQOPS_KERNELS_MUL_H */
