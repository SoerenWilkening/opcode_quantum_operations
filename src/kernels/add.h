/* src/kernels/add.h — M14, Step 12. K6 add, K7 sub. THE FIRST SANDWICH USERS.
 *
 * Read docs/constructions/K06.md and K07.md before changing anything here.
 * Both are ports of `third_party/bennett/src/adder.jl` — `lower_add!` at :1-18
 * and `lower_sub!` at :148-172 — and `lower_sub!`'s body at :162-170 is
 * `lower_add!`'s carry chain verbatim with `not_b` substituted for `b`, which
 * is why one module carries both and why the two exported step functions share
 * one `ripple` body rather than transcribing the recurrence twice.
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
 * not call this KERNEL at all: it costs itself `C_sub(W) = 7W − 1` (K12.md §3.0
 * and §3.1), which is K7's COMPUTE HALF and not the sandwiched `15W − 2`,
 * because K12 is ONE sandwich over its own scratch region and runs the
 * recurrence as its own steps. So nothing nests.
 *
 * BUT SOMETHING IS NOW REUSED, AND M14 IS THE EXPORTER — bd 4tt RESOLVED
 * 2026-08-16 as plan §0.4 / PRD §15 D9(e), and SHIPPED at Step 17. The question
 * this comment used to leave open ("whether M19/M20 re-transcribe it or M14
 * exports its step function") is decided: **M14 exports.** `k7_compute` and the
 * sub half of `adder_env` are now `cq_sub_step` over a `cq_sub_block`, on the
 * shape M15's `cq_addacc_step` and M17's `cq_mux_step` already ship. M19 maps a
 * contiguous run of its own step indices onto `[0, cq_sub_steps(W))`, `W` times,
 * and transcribes no gate list of its own.
 * Rule 1: every re-typing of `lower_sub!` is a fresh chance to put the majority
 * Toffoli before the two CNOTs that build its control, which is the ckd.14(a)
 * ordering hazard and is invisible to L1.
 *
 * The export is ADDITIVE: `cq_kernel_add` and `cq_kernel_sub` keep their
 * signatures, and `cq_kernel_fn` is not widened (Rule 7). Four obligations come
 * with it, all in plan §0.4 — one gate per step; the block allocates nothing
 * (I6(b) forbids a mid-compute-half allocation, so the caller owns every bit);
 * sub-array operands are the sanctioned calling shape, and K12 hands `a` a view
 * that ALIASES a register an earlier step wrote (K12.md §2.1a), so guards
 * compare RANGES not base pointers; and the operand bindings are the consumer's
 * business.
 *
 * `ckd.13` is closed too, and item (b) of bd 4tt is answered NO: none of the
 * three K12 scratch schemes that were built and measured needs a depth-aware
 * `cq_sandwich`, because per-iteration uncompute is scheduled as extra step
 * indices inside one flat step space rather than as an inner sandwich.
 *
 * AND M14 EXPORTS TWICE NOW — `cq_add_block` landed 2026-09-18 for the fp port
 * (bd 9ve.30, PRD-v2 §7.10, K06.md §7), on exactly the shape below and under
 * the same four obligations. Both kernels DISPATCH through their block, so the
 * recurrence has one reader and one writer; the goldens did not move on either
 * landing, which is what says an export is additive.
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

/* --- K6's compute half, exported for the fp port (PRD-v2 §7.10, bd 9ve.30). -
 *
 * WHO ASKED. K15.md's block table found it missing on 2026-09-18: `soft_fadd`
 * has two `+` occurrences and `soft_fma` twelve, and PRD-v2 §5's M32 / M33 /
 * M39 rows compose this block at W = 64. A consumer cannot call
 * `cq_kernel_add` instead — it is a whole sandwich and `cq_sandwich` refuses
 * nesting in both configurations, which is the composite-kernels-call-the-
 * step-function rule M12 is the witness for. `cq_kernel_add` DISPATCHES
 * through the block, so there is ONE body and never a second transcription of
 * `lower_add!`, and tests/goldens/add.counts did not move.
 *
 * THE SILENTLY-WRONG SUBSTITUTE IS K8, AND IT IS THE REASON THIS EXISTS AT
 * ALL. `cq_addacc_step` is `acc += b` — in place, destructive in `acc` and
 * transiently destructive in `b`, and its inverse is the REVERSE CIRCUIT
 * rather than a re-run (CLAUDE.md Rule 7's K8 paragraph, K08.md §5 D1).
 * Substituted here it gives the right forward VALUE and a wrong `_unc`, which
 * is a silent miscompile rather than a test failure — and an fp sandwich's
 * replay-in-reverse is exactly what breaks. The out-of-place ripple is what
 * makes the replay cancel.
 *
 * NO CARRY-IN AND NO CARRY-OUT, AND THAT IS A PORT DECISION RATHER THAN AN
 * OMISSION. `t` is the sum mod 2^W; `c[W-1]` is the last INTERMEDIATE carry
 * and no carry-out is produced, because `lower_add!`'s own `if i < W` guard
 * (adder.jl:11) drops the top stage's two Toffolis. `c[0]` is never a target
 * and is read at i = 0 as the carry-in, so binding a value to it is possible
 * in principle — but it is NOT what upstream does: `_add128`
 * (softfloat_common.jl:299-304) materialises the carry as a VALUE,
 * `carry = ifelse(lo < a_lo, 1, 0)` — an unsigned compare plus a mux — and
 * then adds it as an ordinary 64-bit operand. A carry-chained variant would
 * be a re-derivation of `_add128` (Rule 1); it is owed by nothing and is on no
 * bead. K20.md's Phase I ("UPSTREAM'S 128-BIT ADD NEEDS NO CARRY-IN") states it
 * in full; K06.md §7.2 is this module's copy.
 *
 * THE BLOCK ALLOCATES NOTHING, and `a` / `b` MAY BE SCRATCH SUB-ARRAYS that
 * overlap a region an earlier step wrote — plan §0.4 obligations 2, 3 and 4,
 * exactly as for `cq_sub_block` below and sound for its reason: both reach the
 * emitter ONLY through `cq_emit_*`'s `const cq_bit *` control parameters, so
 * neither can ever be a target, and guards compare RANGES not base pointers.
 *
 * A consumer's layout budgets `2W` bits for `t` and `c`, in EITHER order — the
 * block imposes none, which is why tests/test_kernel_add_block.inc lays them
 * out `c ++ t`, the opposite of add.c's own, on the eq block's precedent. */
typedef struct {
    const cq_bit *a, *b;   /* controls only; may be scratch views, may overlap */
    cq_bit       *t;       /* the sum a + b mod 2^W, W bits — the OUTPUT       */
    cq_bit       *c;       /* the carry chain, W bits; c[0] is the carry-in    */
    int           W;
} cq_add_block;

/* `5W - 2` at every W >= 1 — one gate per step, so this is also the gate count
 * at the all-quantum mask: `(0, 3W, 2W-2)`. Unlike K8's `6W - 5` the per-type
 * split is EXACT at W = 1 too: the loop is empty there and the three-CNOT tail
 * is the whole construction, `(0, 3, 0)` = 3 (K06.md §3.3). K8's closed form
 * gives a NEGATIVE Toffoli count at that width. */
int cq_add_steps(int W);

/* `2W` — the bits a consumer's layout must budget for `t ++ c`, in EITHER
 * order. ADDITIVE, 2026-09-18 (bd 9ve.19): the number was published only in
 * the prose above, so M32's flat scratch would have written `2W` down at every
 * one of its two `add` rows — the thing the composition-check discipline
 * exists to remove for the STEP counts and removes here for the same reason.
 * A sibling's layout moving must redden a case that NAMES the block, not
 * silently shift a consumer's offsets. No behaviour changed and no golden
 * moved; the same `W <= 0` refusal as cq_add_steps, with a DISJOINT message so
 * a death case can say which of the two spoke (M15's cq_addacc_check
 * precedent). */
int cq_add_region(int W);

/* One gate of the block, `u` in [0, cq_add_steps(W)). Out of range is a hard
 * error in BOTH configurations, for cq_sub_step's reason: a consumer maps a
 * contiguous run of its own step indices onto this one, and an off-by-one
 * there lands in `ripple`'s final `default:` and emits a real, plausible gate
 * from the wrong stage rather than failing. */
void cq_add_step(cq_ctx *ctx, const cq_add_block *k, int u);

/* --- K7's compute half, exported for M19's divider (plan §0.4, D9(e)). ----
 *
 * THE BLOCK ALLOCATES NOTHING — obligation 2. Every pointer below is a bit the
 * CALLER owns, inside the caller's ONE pre-materialised region, because an
 * allocation in the middle of a compute half is a bit step 0 did not
 * pre-materialise and I6(b) would be false for it.
 *
 * `a` AND `b` MAY BE SCRATCH SUB-ARRAYS, AND ONE OF THEM MAY OVERLAP A REGISTER
 * AN EARLIER STEP WROTE — obligations 3 and 4. K12 binds `a` to its shifted
 * remainder `r_in[t]`, a view over `z[t] ++ rnext[t-1]` (K12.md §2.1a). That is
 * sound here for one reason and it is worth naming: `a` and `b` reach the
 * emitter ONLY through `cq_emit_*`'s `const cq_bit *` control parameters, so
 * neither can be a target and neither can be materialised. Guards therefore
 * compare RANGES, never base pointers (kernels/kernel.h).
 *
 * `nb`, `d` and `c` are the three scratch vectors — Bennett's `not_b`, `result`
 * and `carry`. `d` is the block's OUTPUT (`a - b`); `c[W-1]` is the last carry
 * and the block emits no carry-OUT, because `lower_sub!`'s `i < W` guard drops
 * the top stage's two Toffolis (adder.jl:150-153). A consumer that wants a
 * borrow flag wants M16's `cq_ult_block` instead, whose chain is `W+1` long.
 *
 * ONE GATE PER STEP — obligation 1, and `ckd.14(a)` is not optional: the driver
 * replays indices, so a step must be an involution. See add.c on why the
 * five-gate ripple body is NOT one. */
typedef struct {
    const cq_bit *a, *b;   /* controls only; may be scratch views, may overlap */
    cq_bit       *nb;      /* ~b, W bits                                       */
    cq_bit       *d;       /* the difference a - b, W bits — the output        */
    cq_bit       *c;       /* the carry chain, W bits                          */
    int           W;
} cq_sub_block;

/* `7W - 1` at every W >= 1 — one gate per step, so this is also the gate count
 * at the all-quantum mask. Unlike K8's `6W - 5` the per-type split is exact at
 * W = 1 too: `(W+1, 4W, 2W-2)` is `(2, 4, 0)`, and hand-counting `lower_sub!`
 * at W = 1 gives the same six gates (K12.md §3.1). */
int cq_sub_steps(int W);

/* `3W` — `nb ++ d ++ c`. Additive, and for cq_add_region's reason. */
int cq_sub_region(int W);

/* One gate of the block, `u` in [0, cq_sub_steps(W)). Out of range is a hard
 * error in BOTH configurations: a consumer maps a contiguous run of its own
 * step indices onto this one, and an off-by-one there would emit a plausible
 * gate from the wrong stage rather than fail. */
void cq_sub_step(cq_ctx *ctx, const cq_sub_block *k, int u);

#endif /* CQOPS_KERNELS_ADD_H */
