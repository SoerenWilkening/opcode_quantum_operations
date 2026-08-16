/* src/kernels/add.h — M14, Step 12. K6 add, K7 sub. THE FIRST SANDWICH USERS.
 *
 * Read docs/constructions/K06.md and K07.md before changing anything here.
 * Both are ports of `third_party/bennett/src/adder.jl` — `lower_add!` at :1-18
 * and `lower_sub!` at :148-172 — and `lower_sub!`'s body at :162-170 is
 * `lower_add!`'s carry chain verbatim with `not_b` substituted for `b`, which
 * is why one module carries both and why they share one step function.
 *
 * RIPPLE-CARRY, NOT CUCCARO, AND THAT IS RULE 7 RATHER THAN A PREFERENCE.
 * `_pick_add_strategy` (arith.jl:20-26) returns `:ripple` for `:auto`, and v1
 * ports that path. Cuccaro is IN-PLACE — `(a,b) -> (a, a+b)` — so its
 * uncompute is the REVERSE CIRCUIT rather than a re-run, which does not match
 * CQ_lang's "recompute from the still-live sources" `_unc` contract. Cuccaro
 * is still needed and lands separately as K8, the in-place accumulator inside
 * the multiplier (M15, Step 15). Do not "simplify" these into it.
 *
 * THE SUM REGION IS SCRATCH, NOT `dst`, and this is the delta from upstream
 * that a reader is most likely to try to remove (K06.md §5 D6). Bennett
 * returns `result` as the SSA value and its caller owns it; our contract is
 * `dst ^= f(a,b)`, so on the `_unc` path `dst` arrives holding `f(a,b)` rather
 * than zero — and step j=3 reads the sum bit as a CONTROL. Running the carry
 * recurrence through `dst` would make the emitted circuit depend on `dst`'s
 * incoming value, which breaks the one-kernel-serves-both-passes contract.
 *
 * NEITHER MAY BE CALLED FROM INSIDE ANOTHER KERNEL'S SANDWICH. `cq_sandwich`
 * refuses to nest (src/sandwich.c:133-135, both configurations: the inner
 * region's release would break the outer palindrome), so these two are not
 * usable the way K1-K3 are — kernels/bitwise.h says explicitly that xor/and/or
 * ARE safe there, because every gate targets `dst` and they take no scratch.
 *
 * THAT IS NOT A CONFLICT WITH K12, and it is worth saying which way round it
 * goes, because the obvious reading is the wrong one. K12's flat design does
 * not call this kernel at all: it costs itself `C_sub(W) = 7W − 1`
 * (K12.md:555, :571, :714), which is K7's COMPUTE HALF and not the sandwiched
 * `15W − 2`, because K12 is ONE sandwich over its own scratch region and
 * inlines the recurrence as its own steps. So nothing nests — but nothing is
 * reused either, and `ripple` below is static. Whether M16/M19/M20
 * re-transcribe it or M14 exports its step function is a plan decision, filed
 * as **bd -4tt**, which also records that the *nested* per-iteration ancilla
 * scheme (`bd ckd.13`) would turn the no-nesting refusal into a hard blocker.
 */
#ifndef CQOPS_KERNELS_ADD_H
#define CQOPS_KERNELS_ADD_H

#include "bit.h"
#include "ctx.h"

/* K6 — dst ^= (a + b) mod 2^W.  Bennett lower_add!, adder.jl:1-18.
 * Sandwiched: 0 X, 7W CX, 4W-4 CCX = 11W-4 at the all-quantum mask.
 * 2W scratch qubits, all taken by the driver at step 0 (I6(b)). */
void cq_kernel_add(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);

/* K7 — dst ^= (a - b) mod 2^W, as a + ~b + 1.  Bennett lower_sub!,
 * adder.jl:148-172.  Sandwiched: 2W+2 X, 9W CX, 4W-4 CCX = 15W-2.
 * 3W scratch qubits. There is no strategy dispatch for sub upstream
 * (arith.jl:215 calls lower_sub! unconditionally) and no published gate-count
 * baseline for it anywhere in the snapshot — K07 is validated against the
 * K06 identity `K7 = K6 + 2W + 1` per compute half instead (K07.md §3.6). */
void cq_kernel_sub(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);

#endif /* CQOPS_KERNELS_ADD_H */
