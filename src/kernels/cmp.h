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
 * K9 DOES NOT REUSE K7's CARRY CHAIN, and the question is a real one (bd -4tt):
 * M14's kernels are whole sandwiches with a static step function, and
 * cq_sandwich refuses to nest, so nothing of M14 is callable from here. It does
 * not need to be. `lower_ult!` is its OWN upstream function — `lower_add!`'s
 * recurrence (adder.jl:8-16) minus the trailing `CNOT(carry[i], result[i])`
 * that produces the sum bit, plus its own `axnb` array — so porting it is
 * Rule 1 applied literally, not a second transcription of K7. What bd -4tt
 * leaves open is the M19/M20 half, at Step 17.
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

#endif /* CQOPS_KERNELS_CMP_H */
