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
 * AND AGAIN FOR `eq` AT 2026-09-18, THIS TIME FOR THE fp PORT rather than for
 * K12. PRD-v2 §7.10 measured what v1 owes the port before its first fp kernel
 * and `eq`/`ne` was one of three missing step blocks; §5's M31 and M36 rows
 * compose it at W = 64. `cq_kernel_eq` and `cq_kernel_ne` now DISPATCH through
 * `cq_eq_step` exactly as the ten predicates dispatch through `cmp()` — one
 * body, never a second transcription of `lower_eq!` — and the ten L4 goldens
 * did not move.
 *
 * AND A THIRD TIME FOR `slt` AT 2026-09-18, in the same wave and for the same
 * consumer. K15.md's block table counts FOUR SIGNED compares on `fadd`'s
 * `Int64 result_exp`, and PRD-v2 §5's M32 (`fpround`) owns all four; the
 * silently-wrong substitute is `cq_ult_block`, which has the same gate shape
 * and is wrong only when `result_exp` is negative. `cq_kernel_slt`, `_sgt`,
 * `_sle` and `_sge` now DISPATCH through `cq_slt_step` exactly as `ult` and
 * `eq` dispatch through theirs, and the ten L4 goldens did not move.
 *
 * RULE 12: DO NOT READ A LINE COUNT OUT OF THIS PARAGRAPH — RE-MEASURE IT.
 * This sentence read "cmp.c IS NOW AT EXACTLY 200/200" and was stale in both
 * directions before the eq export landed: `tools/check_loc.sh`'s own reckoning
 * (non-blank, non-comment, and the only one that counts) put the file at 194,
 * and the eq export took it to 213 against plan §3's 200-line budget and the
 * 300-line hard limit. The ult export cost ten lines because `cq_ult_step`
 * REPLACED `ult_compute`; the eq one cost nineteen for the same reason plus
 * three new entry points — the width guard, the range guard and `cq_eq_flag`.
 * The slt one cost nineteen again, on the same arithmetic, re-measured at 232.
 *
 * PLAN §3's RECORDED SEAM (`primitives ↔ predicate derivation`, moving to
 * `src/kernels/cmp_prim.c`) IS STILL NOT TAKEN, and this paragraph used to say
 * that "a third export in this module is what takes it". Measured, it was not:
 * the third export replaced a private compute half with a public one, so it
 * paid only for the two guards and the accessor. What decides the seam is the
 * measurement and never the count of exports — the threshold is the 300-line
 * hard limit, and at 232 the split would be a churn that moves every citation
 * into this file for no guard. Do not add "just one more" here without
 * measuring first; a FOURTH export, or any body that is not a replacement,
 * is what should take it.
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

/* --- `lower_eq!`'s compute half, exported for the fp port (PRD-v2 §7.10). --
 *
 * WHO ASKED, AND WHY IT IS A BLOCK RATHER THAN A CALL. PRD-v2 §7.10 measured
 * the step blocks v1 owes the port before its first fp kernel and `eq`/`ne` is
 * one of the three; §5's M31 (the IEEE class predicates) and M36 (`fcmp`) rows
 * compose it at W = 64, and `soft_fadd`'s `ea == 0x7FF` shapes are literally
 * this block over 64-bit spans (§7.2's literal grain, §7.3's constants-as-
 * sources). A consumer cannot reach `cq_kernel_eq` instead: it is a whole
 * sandwich and `cq_sandwich` refuses nesting in both configurations, which is
 * the composite-kernels-call-the-step-function rule M12 is already the witness
 * for. Nothing about K9 changed with the export — the same gates in the same
 * order, and its goldens did not move.
 *
 * THE RAW FLAG IS `a != b`, NOT `a == b`, AND THAT IS UPSTREAM'S SHAPE RATHER
 * THAN OURS. `lower_eq!` ends `CNOT(or[W-1], r); NOT(r)` (arith.jl:445) and
 * that trailing NOT is the COPY-OUT's, folded there by K09.md §5 delta 2 — so
 * `cq_kernel_ne` copies this wire out unchanged and `cq_kernel_eq` appends the
 * X. A consumer wanting `eq` owes itself that one gate. Nothing structural can
 * see the mistake: reading the flag as "equal" gives the same gates, the same
 * count, the same palindrome and clean scratch, which is K9's own
 * uge-meaning-ule one layer down.
 *
 * READ THE FLAG THROUGH cq_eq_flag AND NOT BY THE RULE. It is `orr[W-2]` in
 * general and `diff[0]` at W == 1, where `orr` is EMPTY — so a consumer that
 * spells the rule inline reads `orr[-1]` at the bottom of the ladder. The
 * accessor is pure addressing and allocates nothing.
 *
 * THE BLOCK ALLOCATES NOTHING and `a`/`b` MAY BE SCRATCH SUB-ARRAYS, including
 * a view that overlaps a region an earlier step wrote — plan §0.4 obligations
 * 2, 3 and 4, exactly as for cq_ult_block above and sound for its reason: both
 * reach the emitter only through `cq_emit_*`'s `const cq_bit *` controls, so
 * neither can ever be a target, and guards compare RANGES rather than base
 * pointers.
 *
 * `orr` IS W-1 BITS, NOT W. A consumer's layout budgets `2W - 1`, and at
 * W == 1 the one-past-the-end pointer cq_scratch_span returns for a zero-length
 * span is the correct thing to store. */
typedef struct {
    const cq_bit *a, *b;   /* controls only; may be scratch views, may overlap */
    cq_bit       *diff;    /* a ^ b, W bits                                    */
    cq_bit       *orr;     /* the OR-prefix, W-1 bits; orr[W-2] IS `a != b`    */
    int           W;
} cq_eq_block;

/* `5W - 3` at every W >= 1 — one gate per step, so this is also the gate count
 * at the all-quantum mask: `(0, 4W-2, W-1)`. At W == 1 Phase B is empty and the
 * closed form still gives the right 2, so the W == 1 branch is in the INDEXING
 * and not in the count (K09.md §2.1). */
int cq_eq_steps(int W);

/* One gate of the block, `u` in [0, cq_eq_steps(W)). Out of range is a hard
 * error in BOTH configurations, for cq_ult_step's reason. */
void cq_eq_step(cq_ctx *ctx, const cq_eq_block *k, int u);

/* The raw flag `a != b`, inside the caller's own region. */
const cq_bit *cq_eq_flag(const cq_eq_block *k);

/* --- `lower_slt!`'s compute half, exported for the fp port (PRD-v2 §7.10). -
 *
 * WHO ASKED, AND WHY THE SUBSTITUTE IS SILENT. K15.md's block table counts FOUR
 * SIGNED compares on a `result_exp` that `fadd.jl` declares `Int64` — the
 * `result_exp <= 0` of `_sf_handle_subnormal` and the three clamp rows beside
 * it — and PRD-v2 §5's M32 (`fpround`) owns all four. `Int64` and `UInt64` are
 * THE SAME 64 `cq_bit`s under §7.2's literal grain, so the only thing that
 * distinguishes the two families is which comparator the row reaches for. Pick
 * `cq_ult_block` for a signed row and the circuit is well formed, the gate
 * tuple is the SAME SHAPE, the palindrome holds, the scratch comes back — and
 * the answer is wrong exactly when `result_exp` is negative, which is the
 * subnormal path. That is K9's own `uge`-meaning-`ule` in a new costume and
 * ONLY L1 sees it. A consumer cannot call `cq_kernel_slt` instead: it is a
 * whole sandwich and `cq_sandwich` refuses nesting in both configurations.
 *
 * THE FIVE SPANS ARE FLAT AND THE INNER `cq_ult_block` IS NOT IN THE STRUCT.
 * That is the one design choice here and it is made against the obvious
 * alternative — embedding a `cq_ult_block` the caller fills — for a reason the
 * paragraph above already gives. `lower_slt!` ends `lower_ult!(g, wa, af, bf,
 * W)` (arith.jl:471): the inner comparator runs over the BIASED COPIES, never
 * over `a`/`b`, and that wiring is part of the CONSTRUCTION rather than part of
 * the caller's layout. An embedded block hands the consumer two extra fields
 * whose only correct value is `af` and `bf`, and filling them with `a` and `b`
 * — the natural slip, since they are the operands — silently rebuilds `ult`.
 * `cq_slt_step` therefore assembles the inner block itself, from spans the
 * caller genuinely owns, and the mis-wire is unrepresentable. It also keeps
 * `k->nb` reading the way `cq_ult_block`'s and `cq_eq_block`'s spans read,
 * rather than `k->u.nb`.
 *
 * THE RAW FLAG IS `a >=s b`, THE NEGATION OF `slt`, for `cq_ult_block`'s exact
 * reason: the inner carry-out is `af >=u bf`, and the bias makes that `a >=s b`
 * verbatim. Bennett's trailing `CNOT(carry[W+1], r); NOT(r)` (arith.jl:461) is
 * the COPY-OUT's, folded there by K09.md §5 delta 2 — so `cq_kernel_sge` copies
 * this wire out unchanged and `cq_kernel_slt` appends one X. A consumer wanting
 * `slt` owes itself that one `lower_not1!`; K15.md's own table carries it as
 * the `+1` on every `<` and `>` row. Read it through `cq_slt_flag`.
 *
 * THE BLOCK ALLOCATES NOTHING and `a`/`b` MAY BE SCRATCH SUB-ARRAYS, including
 * a view that overlaps a region an earlier step wrote — plan §0.4 obligations
 * 2, 3 and 4, exactly as for the two blocks above and sound for their reason:
 * all three reach the emitter only through `cq_emit_*`'s `const cq_bit *`
 * control parameters, so none can ever be a target, and guards compare RANGES
 * rather than base pointers.
 *
 * A CONSUMER'S LAYOUT BUDGETS `5W + 1` for `af ++ bf ++ nb ++ carry ++ axnb`,
 * and `carry` IS W+1 BITS, NOT W — off-by-one there writes past the caller's
 * sub-array, which `cq_scratch_span` cannot catch since it hands out a bare
 * pointer. The five spans need not be contiguous and need not be in this
 * order; the block imposes none. */
typedef struct {
    const cq_bit *a, *b;   /* controls only; may be scratch views, may overlap */
    cq_bit       *af;      /* a with its sign bit flipped, W bits              */
    cq_bit       *bf;      /* b with its sign bit flipped, W bits              */
    cq_bit       *nb;      /* ~bf, W bits                                      */
    cq_bit       *carry;   /* W+1 bits; carry[W] IS `a >=s b`                  */
    cq_bit       *axnb;    /* af ^ ~bf, W bits                                 */
    int           W;
} cq_slt_block;

/* `2W + 2 + cq_ult_steps(W)` — the two biased copies, the two MSB flips, and
 * the inner comparator, which is `8W + 3` at every W >= 1 and is what `cmp.c`
 * hands `cq_sandwich`. One gate per step, so this is also the gate count at
 * the all-quantum mask: `(W+3, 5W, 2W)`. The `cq_ult_steps` term is CALLED,
 * never written out, so a consumer composing the two cannot disagree with M16
 * about what the inner block costs. */
int cq_slt_steps(int W);

/* One gate of the block, `u` in [0, cq_slt_steps(W)). Out of range is a hard
 * error in BOTH configurations, for cq_ult_step's reason. */
void cq_slt_step(cq_ctx *ctx, const cq_slt_block *k, int u);

/* The raw flag `a >=s b`, inside the caller's own region. */
const cq_bit *cq_slt_flag(const cq_slt_block *k);

#endif /* CQOPS_KERNELS_CMP_H */
