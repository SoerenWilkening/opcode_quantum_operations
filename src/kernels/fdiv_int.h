/* src/kernels/fdiv_int.h — M35's INTERNAL seams, M33's ladder one module over.
 * NOT a public header: nothing outside `src/kernels/fdiv*.c` includes it, and
 * every name in it is `cq_fd_`-prefixed to say so.
 *
 * TWO SEAMS LIVE HERE, BOTH RECORDED IN ADVANCE (bd a-row-table-fp-kernel-
 * needs-three-seams-not-two: "record FOUR seams in advance for M34/M35/M39/M40,
 * not two"). M36 took its second cut at 298/300 and M32 took its at 418/300 and
 * M33 its fourth at 326/300; M35 is bigger than all three on its op vocabulary,
 * so both cuts are made up front.
 *
 *   fdiv_step.c      what a row COSTS (asked of M14/M16/M17/M31/M32) and where
 *                    its spans LIE inside the caller's region — the segmented
 *                    prefix map below.
 *   fdiv_operand.c   what each operand code RESOLVES to: the collapsing view
 *                    chain, the constant vocabulary, the block outputs, the
 *                    hand-off projections and the one-bit flags.
 *   fdiv_emit.c      which GATE a slot emits, the public block's entry points
 *                    and accessors, and the Rule 7 kernel.
 *
 * THE MAP IS A PREFIX SUM, A MODULUS AND A PREFIX SUM (K17.md §2.7, K21's
 * shape rather than K12's or K15's). K15 is a pure prefix sum because it is
 * straight-line and K12 is a pure modulus because it is one repeated
 * iteration; K17 is both, and the two ends are NOT the loop's shape.
 *
 * AND THAT IS WHY THE MAP EXISTS AT ALL RATHER THAN A 478-ROW FLAT `off[]`.
 * The segmented map prepares 36 + 7 + 50 row costs instead of 478 and answers
 * `cq_fd_off` in constant time, because the loop's offsets are `loop_off + t *
 * iter_bits + iter[j]` — an ARITHMETIC consequence of every iteration being
 * the same seven rows. Its base-zero form is immutable and cached once; an
 * invocation copies and rebases it without storing operands or circuit state.
 *
 * `loop_off`, `iter_bits`, `loop_slot` and `iter_steps` ARE PURE FUNCTIONS OF
 * THE BLOCK COSTS AND OF NOTHING ELSE (plan §0.1, ckd.14(a)). The base-zero
 * map is therefore built once; a block invocation copies and rebases that
 * immutable metadata, never operands or circuit state.
 */
#ifndef CQOPS_KERNELS_FDIV_INT_H
#define CQOPS_KERNELS_FDIV_INT_H

#include "kernels/fdiv.h"

enum { CQ_FD_W = CQ_FP64_W, CQ_FD_MAXSEG = CQ_FDIV_MAX_SEG };

/* What a row costs. Every term is ASKED of its owning module at width 64 or is
 * upstream's own four-gate bitwise vocabulary; not one line of either function
 * would move if the row table changed. */
int      cq_fd_row_steps (const cq_fdiv_row *r);
uint32_t cq_fd_row_region(const cq_fdiv_row *r);

/* The segmented prefix map. `pre` and `post` are ABSOLUTE offsets inside the
 * region (the base has already been added); `iter` is RELATIVE to the start of
 * an iteration, which is what lets one array serve all CQ_FDIV_N_ITERS of
 * them. The `*_s` arrays are the matching SLOT prefixes. Each has one extra
 * entry holding the segment total. */
typedef struct {
    uint32_t pre [CQ_FDIV_MAX_SEG + 1];
    uint32_t iter[CQ_FDIV_ITER_ROWS + 1];
    uint32_t post[CQ_FDIV_MAX_SEG + 1];
    int      pre_s [CQ_FDIV_MAX_SEG + 1];
    int      iter_s[CQ_FDIV_ITER_ROWS + 1];
    int      post_s[CQ_FDIV_MAX_SEG + 1];
    uint32_t loop_off;     /* absolute offset of iteration 0's first row    */
    uint32_t post_off;     /* absolute offset of the POST segment           */
    uint32_t iter_bits;    /* bits ONE iteration owns                       */
    int      loop_slot;    /* slot index of iteration 0's first slot        */
    int      post_slot;    /* slot index of the POST segment                */
    int      iter_steps;   /* K17.md §2.7's `loop_K`                        */
    int      n_pre, n_post;
} cq_fd_map;

/* The layout, as a pure function of `base`. No block, so `cq_fdiv_steps` and
 * `cq_fdiv_region` reach it too. */
void cq_fd_map_build(cq_fd_map *m, uint32_t base);
const cq_fd_map *cq_fd_map_get(void);

/* The same plus the fit check, which is the BLOCK's and has to be: without it
 * the first out-of-region span aborts in M08 naming the REGION rather than the
 * consumer that mis-sized its offset, and the two-programs-in-one-region shape
 * then has no diagnostic of its own. Hard error in both configurations. */
void cq_fd_arm(const cq_fdiv_block *k, cq_fd_map *m);

/* Row `i`'s absolute offset, in constant time. */
uint32_t cq_fd_off(const cq_fd_map *m, int i);

/* The row `u` falls in, and `*local` receives `u` rebased into that row's own
 * `[0, cq_fd_row_steps(row))`. A hard error on an out-of-range `u`. */
int cq_fd_row_of_slot(const cq_fd_map *m, int u, int *local);

cq_bit *cq_fd_sp(const cq_fdiv_block *k, uint32_t at, uint32_t len);

/* Row `i`'s value. `cq_fd_val64` needs a 64-entry `buf` for a projection that
 * is an assembled VIEW (`flushed_result`); `cq_fd_flag_of` is pure addressing.
 * Each refuses a row of the other width, and the 1-bit one is the refusal that
 * gets forgotten (bd a-table-driven-kernel-needs-both-operand-refusals). */
const cq_bit *cq_fd_val64  (const cq_fdiv_block *k, const cq_fd_map *m, int i,
                            cq_bit *buf);
const cq_bit *cq_fd_flag_of(const cq_fdiv_block *k, const cq_fd_map *m, int i);

/* A 64-lane OPERAND. `buf` receives a view, a constant or an assembled
 * projection; `tmp` is the second buffer a view OVER one of those needs, and
 * the two must be distinct arrays. */
const cq_bit *cq_fd_op64(const cq_fdiv_block *k, const cq_fd_map *m, int s,
                         cq_bit *buf, cq_bit *tmp);

/* The view chain: returns the first non-view operand and yields the collapsed
 * `(shift, mask)`. Exposed because `cq_fd_val64` and `cq_fd_op64` are two
 * entry points into one walk. */
int  cq_fd_view_collapse(int s, int *shift, uint64_t *mask);
void cq_fd_view_fill(const cq_bit *base, int shift, uint64_t mask,
                     cq_bit *out);

#endif /* CQOPS_KERNELS_FDIV_INT_H */
