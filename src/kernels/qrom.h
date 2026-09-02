/* src/kernels/qrom.h — M29, Step 27 (v1.2, PRD §15 D24, plan §0.6). K13: the
 * QROM read at a quantum index, and the unary-iteration TREE it is built on,
 * exported as a step block because K14 (qstore.c) is the same tree with a
 * different copyout.
 *
 * Read docs/constructions/K13.md before changing anything here. The tree's three
 * gates per node and their order are Bennett's `_qrom_tree!` (qrom.jl:110-118)
 * verbatim; what is ours is the SCHEDULE — flat, breadth-first, every flag live
 * across the copyout — because Rule 8's driver is compute → copyout → reverse
 * and cannot interleave a node's compute, its fan-out and its uncompute the
 * way Bennett's depth-first walk does. Gate count identical, qubit peak not
 * (2·Lp − 1 here against Bennett's 2n + 1); K13.md §5 delta 1, D9's precedent.
 *
 * THE TREE BLOCK, in one contiguous scratch region of `cq_qtree_nodes(n)` bits
 * in HEAP ORDER: node k has children 2k+1 (left, index lane clear) and 2k+2
 * (right, index lane set), the root is node 0, leaf j is node 2^n − 1 + j. A
 * node at depth d splits on index lane n − 1 − d, which is Bennett's
 * `bit_level` counting down from n. The block reads lanes 0 … n−1 of `idx` as
 * CONTROLS and nothing else — lanes above n are ignored (qrom.jl:171,
 * `idx_full[1:n]`), and a `count` that is not a power of two is padded to
 * Lp = 2^n with leaves that own no cell (qrom.jl:165-168).
 *
 * K13 IS A RULE 7 KERNEL — `dst ^= mem[idx]`, sources unchanged, scratch
 * clean — and departs from the parameter list only, as M17's mux does: the
 * cells are `count` pointers to W-bit arrays (M07 registers at the shim, one
 * carved `count·W`-bit source in the test driver), and `idx` carries its own
 * width. `_unc` is the same kernel with `dst = out`; the controlled axis is
 * M06's and this file does not know it exists (Rule 9). No `cq_kernel_fn`
 * entry: like the mux it is reached through the driver's `call` adapter and
 * by name from the shim.
 */
#ifndef CQOPS_KERNELS_QROM_H
#define CQOPS_KERNELS_QROM_H

#include "bit.h"
#include "ctx.h"

typedef struct {
    cq_bit       *flags;   /* cq_qtree_nodes(n) scratch bits, heap order   */
    const cq_bit *idx;     /* lanes 0 … n−1 are read; the rest are ignored */
    int           n;       /* depth: ⌈log₂ count⌉, and at least 1 here     */
} cq_qtree_block;

/* ⌈log₂ count⌉, 0 for count == 1. `count <= 0` and `count > CQ_QRAM_COUNT_MAX`
 * are hard errors in both configurations: the driver's step index is an int
 * (plan §0.1) and the tree has 3·(2^n − 1) + 1 compute steps. */
#define CQ_QRAM_COUNT_MAX (1 << 28)
int cq_qtree_depth(int count);
int cq_qtree_nodes(int n);   /* 2^(n+1) − 1 flags                            */
int cq_qtree_steps(int n);   /* 1 + 3·(2^n − 1) compute steps, ONE gate each */

/* ONE GATE PER STEP (bd ckd.14a). Step 0 is the root's X; step 1 + 3k + phase
 * is node k's Toffoli (phase 0), then the two CNOTs that make its left child
 * `parent ∧ ¬idx_lane` (phases 1 and 2), in Bennett's order. Every target is a
 * flag — I6(a) with nothing left to check — and `idx` reaches the emitter only
 * as a `const cq_bit *` control. */
void cq_qtree_step(cq_ctx *ctx, const cq_qtree_block *b, int u);

/* Leaf j's flag: set iff the truncated index equals j. */
const cq_bit *cq_qtree_leaf(const cq_qtree_block *b, int j);

/* THE SHARED OPERAND CHECK for K13 and K14: widths positive, `n_idx >= n`,
 * and every named operand pairwise range-disjoint — `dst`, `idx`, `val` and
 * `tape` may be NULL where a kernel has no such operand (their widths are then
 * ignored), the cells never. The cells are checked against each other by
 * sorting their base addresses, so an aliased pair among a large `count` is
 * still found in O(count log count). Hard errors in both configurations, on
 * kernel.h's D7 grounds: an alias reaching a kernel is a missing copy. */
void cq_qram_check(const cq_bit *dst, int w_dst,
                   const cq_bit *idx, int n_idx,
                   const cq_bit *val, const cq_bit *tape,
                   const cq_bit *const *cells, int count, int W);

/* Is every lane the tree would read a constant? Then the index is KNOWN and
 * the lookup takes Bennett's Case 1 (qrom.jl:151-160, "compile-time-constant
 * index: materialize data[idx] directly. Zero gates for the lookup itself") —
 * the entry dispatch both kernels share, and the R9 short-circuit L5 needs:
 * the driver pre-materialises scratch unconditionally, so without it a fully
 * classical lookup would take 2·Lp − 1 qubits for an operation with no quantum
 * input (M17's `cond` argument, K10.md §5 delta 4). Returns the truncated
 * index in `*j`, which may be a padded index (>= count). */
int cq_qtree_index_is_const(const cq_bit *idx, int n, int *j);

/* K13 — dst ^= mem[idx & (2^n − 1)], reading 0 at a padded index.
 *
 * Cost at all-quantum operands, count >= 2 (K13.md §3): 2 X, 4(Lp−1) CX,
 * 2(Lp−1) + count·W CCX, over 2·Lp − 1 scratch qubits taken and returned;
 * count == 1 is W CX and no scratch (Bennett's L == 1 branch). A constant cell
 * lane folds exactly as Bennett's constant data does. */
void cq_kernel_qload(cq_ctx *ctx, cq_bit *dst,
                     const cq_bit *idx, int n_idx,
                     const cq_bit *const *cells, int count, int W);

#endif /* CQOPS_KERNELS_QROM_H */
