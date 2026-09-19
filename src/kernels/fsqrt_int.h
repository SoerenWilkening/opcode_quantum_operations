/* src/kernels/fsqrt_int.h — M40's INTERNAL seam. NOT a public header: nothing
 * outside `src/kernels/fsqrt*.c` includes it, and every name in it is
 * `cq_fs_`-prefixed to say so.
 *
 * IT CARRIES TWO SEAMS, BOTH RECORDED IN ADVANCE (bd 9ve.26, on M33's
 * three-seams-not-two finding and M32's D-K23-10):
 *
 *   fsqrt_step.c     COSTS AND LAYOUT — what a row costs, asked of
 *                    M14/M16/M17/M31/M32, and the memoised prefix map.
 *   fsqrt_operand.c  OPERAND RESOLUTION — the collapsing view chain, the
 *                    arithmetic-shift view, the constant vocabulary, the block
 *                    outputs, the one-bit flags and the two projections.
 *   fsqrt_emit.c     DISPATCH AND SURFACE — which gate a slot emits, the
 *                    public block's entry points and accessors, and the Rule 7
 *                    kernel.
 *
 * THE PREFIX MAP IS MEMOISED AND THAT IS FORCED BY 1,070 ROWS, NOT CHOSEN.
 * M34's step walks its 128-row table twice per step — once to accumulate the
 * offsets and once to find the row — which is O(n) per step and fine at
 * 163,300 steps over 128 rows. The same shape here is 1,070 x 157,108 x 2 row
 * visits per compute half, about eight times M34's, and L1 drives four compute
 * halves per case. So the map is built ONCE and the row lookup is a binary
 * search over it.
 *
 * IT IS NOT MUTABLE STATE ACROSS THE REVERSAL, WHICH IS THE THING K21.md §2.6
 * POINT 4 FORBIDS. What that forbids is a CURSOR — a cached "which row am I
 * in" that the driver's index reversal would desynchronise. The map is a pure
 * function of the row table (a compile-time constant) and of the sibling
 * modules' published costs at W = 64 (themselves pure), so recomputing it at
 * any moment gives the same answer and a desynchronisation is unrepresentable.
 * It depends on neither the step index nor the block nor `k->off`: the block's
 * base is applied in `cq_fs_sp` and nowhere else, which is what keeps a
 * dropped `off` detectable by a second program in one region and by nothing
 * else.
 */
#ifndef CQOPS_KERNELS_FSQRT_INT_H
#define CQOPS_KERNELS_FSQRT_INT_H

#include "kernels/fsqrt.h"

enum { CQ_FS_W = CQ_FP64_W, CQ_FS_MAXR = CQ_FSQRT_MAX_ROWS };

/* What a row costs. Every term is ASKED of its owning module at width 64 or is
 * upstream's own bitwise vocabulary; not one line of either function would
 * move if the row table changed. */
int      cq_fs_row_steps (const cq_fsqrt_row *r);
uint32_t cq_fs_row_region(const cq_fsqrt_row *r);

/* The memoised prefix map: `slot[i]` is the first slot of row `i` and `bit[i]`
 * its first region bit RELATIVE to the block's base. Both have `n + 1` entries
 * and the last is the total. */
typedef struct {
    int      nslot;
    uint32_t nbit;
    int      slot[CQ_FS_MAXR + 1];
    uint32_t bit [CQ_FS_MAXR + 1];
} cq_fs_map;

const cq_fs_map *cq_fs_map_get(void);

/* The fit check, which is the BLOCK's and has to be: without it the first
 * out-of-region span aborts in M08 naming the REGION rather than the consumer
 * that mis-sized its offset. Hard error in both configurations. */
void cq_fs_arm(const cq_fsqrt_block *k);

/* The row whose slot range contains `u`, by binary search over the map. A row
 * with zero slots is never returned. */
int cq_fs_row_at(int u);

/* `at` is RELATIVE to the block's base; this is the ONE place `k->off` is
 * applied, which is why a block cannot see its own offset. */
cq_bit *cq_fs_sp(const cq_fsqrt_block *k, uint32_t at, uint32_t len);

/* Row `i`'s value. `cq_fs_val64` needs a 64-entry `buf` only for a projection
 * that is not a span (none today); `cq_fs_flag_of` is pure addressing. Each
 * refuses a row of the other width, and the 1-bit one is the refusal that gets
 * forgotten (bd a-table-driven-kernel-needs-both-operand-refusals). */
const cq_bit *cq_fs_val64  (const cq_fsqrt_block *k, int i, cq_bit *buf);
const cq_bit *cq_fs_flag_of(const cq_fsqrt_block *k, int i);

/* A 64-lane OPERAND. `buf` receives a view, an arithmetic view, a constant or
 * an assembled projection; `tmp` is the second buffer a view OVER one of those
 * needs, and the two must be distinct arrays. */
const cq_bit *cq_fs_op64(const cq_fsqrt_block *k, int s, cq_bit *buf,
                         cq_bit *tmp);

/* The view chain, exposed so a future caller outside the resolver can reuse
 * one walk. Returns the first non-VIEW operand and yields the collapsed
 * `(shift, mask)`; an SVIEW terminates the chain rather than joining it. */
int  cq_fs_view_collapse(int s, int *shift, uint64_t *mask);
void cq_fs_view_fill(const cq_bit *base, int shift, uint64_t mask, cq_bit *out);

#endif /* CQOPS_KERNELS_FSQRT_INT_H */
