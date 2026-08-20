/* src/kernels/cmp.h — M16, Step 13. K9: `icmp`, all ten LLVM predicates.
 *
 * Read docs/constructions/K09.md before changing anything here. All three
 * primitives are ports of `third_party/bennett/src/lowering/arith.jl` —
 * `lower_eq!` at :424-447, `lower_ult!` at :449-463, `lower_slt!` at :465-472 —
 * and the seven derived predicates are `lower_icmp!`'s own dispatch (:409-418),
 * not a derivation of ours: `ne = ¬eq`, `ugt = ult(b,a)`, `ule = ¬ult(b,a)`,
 * `uge = ¬ult(a,b)`, and the signed trio the same way over `slt`.
 *
 * `dst` IS EXACTLY ONE cq_bit AND `W` IS THE OPERAND WIDTH. K9 is the only
 * kernel that keeps Rule 7's single-`W` signature while producing a result of a
 * DIFFERENT width — `icmp` is `i1` (ir_types.jl:79; K09.md §5 delta 10). K5's
 * casts also have two widths, but they name both explicitly
 * (`cq_kernel_zext(ctx, dst, a, F, T)`) and so have no single `W` to disagree
 * with; here `W` means the operands' and `dst` is one bit regardless. So the
 * copy-out is 1 CX (+ at most 1 X)
 * and NOT the "W CNOTs" PRD §5's generic sandwich sketch writes. A harness that
 * iterates `dst[0..W)` is reading off the end of a one-bit register; the shared
 * driver is told through cq_kd_shape's `w_dst`, and the kernel itself asserts
 * the one-bit extent through cq_kernel_check_n(dst, 1, ...).
 *
 * THERE IS NO PREDICATE PARAMETER, and that is Rule 7 rather than taste. The
 * kernel contract is one shape — `void k(cq_ctx*, cq_bit *dst, const cq_bit *a,
 * const cq_bit *b, int W)` — and it is what makes forward, uncompute and (at
 * Step 20) controlled the same function. A tenth argument would fork every one
 * of those axes. Ten entry points over one shared body is the cost of that, and
 * it is paid once here.
 *
 * K9 DOES NOT REUSE K7's CARRY CHAIN, and the question was a real one (bd 4tt):
 * at Step 13 M14's kernels were whole sandwiches with a file-static step
 * function, and cq_sandwich refuses to nest, so nothing of M14 was callable
 * from here. It does not need to be. `lower_ult!` is its OWN upstream function
 * — `lower_add!`'s recurrence (adder.jl:8-16) minus the trailing
 * `CNOT(carry[i], result[i])` that produces the sum bit, plus its own `axnb`
 * array — so porting it is Rule 1 applied literally, not a second
 * transcription of K7.
 *
 * M16 IS ITSELF AN EXPORTER AS OF STEP 17 — bd 4tt RESOLVED 2026-08-16 as plan
 * §0.4 / PRD §15 D9(e), and shipped. K12's per-iteration comparator IS this
 * module's `ult` compute half, so `ult_compute` and the `ult` half of `cmp_env`
 * are now `cq_ult_step` over a `cq_ult_block`, on the shape M15's
 * `cq_addacc_step` and M17's `cq_mux_step` already had. M19 maps a contiguous
 * run of its own step indices onto `[0, cq_ult_steps(W))`, `W` times. K9 emits
 * the same gates in the same order and its goldens did not move. Two things
 * about that export:
 *
 *   - **What K12 wants is the RAW CARRY-OUT, and that is what this module
 *     already produces.** `layout`'s PRIM_ULT sets `raw = &carry[W]`, which is
 *     `a >=u b` verbatim, and `cq_kernel_uge` copies it out unchanged —
 *     Bennett's `lower_not1!(lower_ult!(…))` double negation folded into the
 *     copy-out (K09.md §5 delta 2). PRD §15 D9(b) makes K12 read the same wire
 *     rather than re-materialise `ult` and `fits`: keeping the Bennett-IR shape
 *     would have put TWO different realisations of `uge` in one library, with
 *     the more expensive one in the only kernel that invokes it `W` times per
 *     call. Worth `4W` gates and `2W` qubits to K12.
 *   - **The block's operands may be scratch sub-arrays, including a view that
 *     ALIASES a register an earlier step wrote.** K12 binds `ua` to its shifted
 *     remainder, which overlaps the previous iteration's mux output (K12.md
 *     §2.1a). That is legal because `ua` is only ever a control; guards compare
 *     RANGES, never base pointers.
 *
 * RULE 12: `cmp.c` IS NOW AT EXACTLY 200/200. The export cost ten lines, not
 * thirty, because `cq_ult_step` REPLACED `ult_compute` rather than being added
 * beside it — so plan §3's recorded seam (`primitives ↔ predicate derivation`,
 * moving to `src/kernels/cmp_prim.c`) was not taken and has zero headroom left.
 * The next line added to this module takes it. The hard limit is 300, so this
 * is a scheduled split rather than a wall, but do not add "just one more" here
 * without moving the three primitives out first.
 *
 * ALL THREE PRIMITIVES ARE DIRTY BY DESIGN and therefore sandwiched.
 * `lower_eq!` leaves `diff` and the OR-prefix behind, `lower_ult!` leaves `nb`,
 * the whole carry chain and `axnb`, `lower_slt!` adds the two sign-flipped
 * copies. Bennett tolerates that because its single global forward-copy-reverse
 * wrap cleans up at the top level; we have no global wrap, so PRD §5's
 * Bennett-in-the-small applies and cq_sandwich runs the compute half twice.
 */
#ifndef CQOPS_KERNELS_CMP_H
#define CQOPS_KERNELS_CMP_H

#include "bit.h"
#include "ctx.h"

/* Every one of the ten: `dst[0] ^= (a <predicate> b)`, with `a` and `b` `W`
 * bits wide and unchanged, and every scratch qubit back at |0>.
 *
 * Sandwiched cost at the all-quantum operand mask (K09.md §3.2), which is what
 * tests/goldens/cmp.counts pins:
 *
 *     eq          1 X       8W-3 CX    2W-2 CCX   = 10W-4     2W-1 qubits
 *     ne          0 X       8W-3 CX    2W-2 CCX   = 10W-5     2W-1
 *     ult ugt     2W+3 X    6W+1 CX    4W   CCX   = 12W+4     3W+1
 *     ule uge     2W+2 X    6W+1 CX    4W   CCX   = 12W+3     3W+1
 *     slt sgt     2W+7 X   10W+1 CX    4W   CCX   = 16W+8     5W+1
 *     sle sge     2W+6 X   10W+1 CX    4W   CCX   = 16W+7     5W+1
 *
 * The four negated siblings cost exactly one X LESS than the predicate they
 * negate, because the trailing NOT cancels against the raw flag's own
 * inversion (K09.md §5 delta 2) — `uge`'s raw carry-out already IS the answer.
 * The operand swap in `ugt`/`ule`/`sgt`/`sle` is an argument-order change and
 * costs nothing at all. */
void cq_kernel_eq (cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_ne (cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);

void cq_kernel_ult(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_ugt(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_ule(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_uge(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);

void cq_kernel_slt(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_sgt(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_sle(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_sge(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W);

/* --- `lower_ult!`'s compute half, exported for M19 (plan §0.4, D9(e)). ----
 *
 * WHAT K12 WANTS IS THE RAW CARRY-OUT, AND `carry[W]` IS IT. It is the carry
 * out of `a + ~b + 1`, so it is `1` exactly when `a >=u b` — the `fits` of a
 * restoring division step, with no `ult` wire, no `not1` and no negation at
 * all. That is not a K12 shortcut: `cq_kernel_uge` already copies this same bit
 * out unchanged (cmp.c's `layout`, `raw = &carry[W]`), which is Bennett's
 * double negation folded into the copy-out, K09.md §5 delta 2. PRD §15 D9(b)
 * makes K12 read the same wire rather than re-materialise `not1(ult(...))`;
 * doing otherwise would have put TWO realisations of `uge` in one library, with
 * the more expensive one in the only kernel that invokes it W times per call.
 *
 * THE BLOCK ALLOCATES NOTHING and `a`/`b` MAY BE SCRATCH SUB-ARRAYS, including
 * a view that overlaps a register an earlier step wrote — plan §0.4 obligations
 * 2, 3 and 4. K12 binds `a` to its shifted remainder `r_in[t]`, which aliases
 * the previous iteration's mux output (K12.md §2.1a). Sound because `a` and `b`
 * reach the emitter only through `cq_emit_*`'s `const cq_bit *` controls, so
 * neither is ever a target; guards compare RANGES, never base pointers.
 *
 * `carry` IS W+1 BITS, NOT W. Off-by-one here writes past the caller's
 * sub-array — which cq_scratch_span cannot catch, since it hands out a bare
 * pointer — so a consumer's layout must budget `3W + 1` for the three vectors. */
typedef struct {
    const cq_bit *a, *b;   /* controls only; may be scratch views, may overlap */
    cq_bit       *nb;      /* ~b, W bits                                       */
    cq_bit       *carry;   /* W+1 bits; carry[W] IS `a >=u b`                  */
    cq_bit       *axnb;    /* a ^ ~b, W bits                                   */
    int           W;
} cq_ult_block;

/* `6W + 1` at every W >= 1 — one gate per step, so this is also the gate count
 * at the all-quantum mask: `(W+1, 3W, 2W)`. It is `lower_ult!`'s `6W + 3` minus
 * the two-gate result-wire tail (arith.jl:461-462) that only the generic
 * `lower_icmp!` dispatcher needs. */
int cq_ult_steps(int W);

/* One gate of the block, `u` in [0, cq_ult_steps(W)). Out of range is a hard
 * error in BOTH configurations, for cq_sub_step's reason. */
void cq_ult_step(cq_ctx *ctx, const cq_ult_block *k, int u);

#endif /* CQOPS_KERNELS_CMP_H */
