/* src/kernels/mul.h — M18, Step 16. K11 `mul`, W-bit truncated.
 *
 * Read docs/constructions/K11.md before changing anything here. The skeleton is
 * Bennett's `lower_mul!` = `lower_mul_wide!(…, W, W)`
 * (third_party/bennett/src/multiplier.jl:1-33) ported literally; the inner
 * adder slot is the ONE place libcqops departs from upstream.
 *
 * THE HEADLINE DELTA: multiplier.jl:29 calls the OUT-OF-PLACE ripple
 * `lower_add!`, and we substitute the IN-PLACE Cuccaro accumulator K8 (M15,
 * src/kernels/addacc.c) into that slot. Decided 2026-08-14, recorded in
 * PRD-v1.md:550-560 and K11.md §2 / §5 delta 4. It is a derived composition,
 * not a port: the string `cuccaro` does not occur in multiplier.jl, no upstream
 * dispatch path composes shift-add with Cuccaro, and no Bennett test exercises
 * it. What that buys is the scratch region — `W² + 2W` against ripple's
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
 * EXPORTED FOR THE SUITE, NOT FOR A CALLER. `cq_mock_is_palindrome(m, n_head,
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

#endif /* CQOPS_KERNELS_MUL_H */
