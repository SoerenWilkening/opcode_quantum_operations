/* src/kernels/qstore.h — M30, Step 27 (v1.2, PRD §15 D24, plan §0.6). K14: the
 * shadow store at a quantum index, over M29's tree.
 *
 * NOT A RULE 7 KERNEL, AND SAID SO HERE AS K08.md AND D17 SAY IT FOR `addc`.
 * It is `cell[idx] := val` with the displaced value moved onto ONE W-bit tape
 * slot: in place, destructive on the cells and on the slot, and its inverse is
 * the REVERSE CIRCUIT — Bennett's three sweeps in the opposite order — so a
 * second call with the same operands does not cancel (it would move `val` onto
 * the slot and zero the cell). It may never stand in for K13's shape: the
 * forward value would be right and only the undo wrong, which is a silent
 * miscompile and not a test failure. Two named entry points, one body, one
 * index map between them. There is NO L5: a store into an all-classical array
 * with a classical index is still a store, priced lane by lane by the fold
 * table rather than short-circuited (K14.md, header note).
 *
 * The construction is `emit_shadow_store!` (shadow_memory.jl:38-55 — three
 * CNOT sweeps: cell → T, T → cell, val → cell) with every sweep gate guarded by
 * the leaf flag, which is `emit_shadow_store_guarded!` (:98-121) with
 * `pred_wire = leaf_j`, under the SAME tree K13 exports. Exactly one leaf is
 * set, so sweep 0 over every cell lands exactly cell[idx] in the one slot —
 * K14.md §5 delta 1: W tape qubits per store, not count·W.
 *
 * The tree is the compute half of cq_sandwich and its reversal is structural;
 * the sweeps are the copyout, run ONCE. The controlled store is M06's (Rule 9)
 * and this file does not know the axis exists.
 */
#ifndef CQOPS_KERNELS_QSTORE_H
#define CQOPS_KERNELS_QSTORE_H

#include "bit.h"
#include "ctx.h"

/* K14, PUSH: cell[idx'] := val, tape := old cell[idx'], every other cell and
 * every source unchanged; at a padded index nothing moves. `tape` must be all
 * CQ_BIT_ZERO on entry (a rail born |0>) — asserted.
 *
 * K14, POP: the exact reverse with the SAME operands — every cell restored and
 * `tape` back to |0>. Whether the operands ARE the same is the caller's
 * premise and D15's certificate's evidence (PRD §15 D24 (c)); this kernel
 * asserts nothing about it and stamps no `proven_zero`.
 *
 * Cost at all-quantum operands, count >= 2 (K14.md §3): the tree's
 * (2, 4(Lp−1), 2(Lp−1)) plus 3·count·W CCX, push and pop alike; count == 1 is
 * 3W CX and no scratch. */
void cq_kernel_qstore_push(cq_ctx *ctx, cq_bit *const *cells, int count,
                           const cq_bit *idx, int n_idx,
                           const cq_bit *val, cq_bit *tape, int W);
void cq_kernel_qstore_pop (cq_ctx *ctx, cq_bit *const *cells, int count,
                           const cq_bit *idx, int n_idx,
                           const cq_bit *val, cq_bit *tape, int W);

/* The copyout's length, for the suite's palindrome and composition checks. */
int cq_qstore_sweep_steps(int count, int W);

#endif /* CQOPS_KERNELS_QSTORE_H */
