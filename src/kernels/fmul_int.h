/* src/kernels/fmul_int.h — M34's INTERNAL seam, M32's D-K23-10 one module
 * over. NOT a public header: nothing outside `src/kernels/fmul*.c` includes
 * it, and every name in it is `cq_fm_`-prefixed to say so.
 *
 * THE SEAM IT CARRIES IS LAYOUT-AND-OPERANDS <-> DISPATCH-AND-SURFACE, and it
 * was recorded in advance rather than hit at 300 lines — M36 took its second
 * cut at 298/300 and M32 took this one at 418/300, and PRD-v2 §5's landed M36
 * row says in so many words that "every M33+ kernel is bigger than `fcmp` and
 * should record the same two seams IN ADVANCE".
 *
 *   fmul_step.c   what a row COSTS (asked of M14/M16/M17/M18/M32), where its
 *                 spans LIE inside the caller's region, and what each operand
 *                 code RESOLVES to — the collapsing view chain, the constant
 *                 vocabulary, the block outputs, the hand-off projections and
 *                 the flags.
 *   fmul_emit.c   which GATE a slot emits, the public block's entry points and
 *                 accessors, and the Rule 7 kernel.
 */
#ifndef CQOPS_KERNELS_FMUL_INT_H
#define CQOPS_KERNELS_FMUL_INT_H

#include "kernels/fmul.h"

enum { CQ_FM_W = CQ_FP64_W, CQ_FM_MAXR = CQ_FMUL_MAX_ROWS };

/* What a row costs. Every term is ASKED of its owning module at width 64 or is
 * upstream's own four-gate bitwise vocabulary; not one line of either function
 * would move if the row table changed. */
int      cq_fm_row_steps (const cq_fmul_row *r);
uint32_t cq_fm_row_region(const cq_fmul_row *r);

/* Immutable metadata derived from the fixed row table and published block
 * costs. It contains no operands or circuit state and is prepared once.
 * `slot` has one terminal entry. */
typedef struct {
    uint32_t rel[CQ_FM_MAXR];
    int      slot[CQ_FM_MAXR + 1];
    uint32_t region;
    int      steps, n;
} cq_fm_map;

const cq_fm_map *cq_fm_map_get(void);
int              cq_fm_row_at(const cq_fm_map *m, int u, int *within);

/* Rebase the immutable relative map plus the block fit check. `off` is
 * CQ_FM_MAXR entries and every populated entry is ABSOLUTE inside the region —
 * this is the one place `k->off` is applied. */
void    cq_fm_arm(const cq_fmul_block *k, uint32_t *off);
cq_bit *cq_fm_sp (const cq_fmul_block *k, uint32_t at, uint32_t len);

/* Row `i`'s value. `cq_fm_val64` needs a 64-entry `buf` for a view, a constant
 * or `flushed_result`; `cq_fm_flag_of` is pure addressing. Each refuses a row
 * of the other width, and the 1-bit one is the refusal that gets forgotten
 * (bd a-table-driven-kernel-needs-both-operand-refusals, measured on M36). */
const cq_bit *cq_fm_val64  (const cq_fmul_block *k, const uint32_t *off, int i,
                            cq_bit *buf);
const cq_bit *cq_fm_flag_of(const cq_fmul_block *k, const uint32_t *off, int i);

/* A 64-lane OPERAND. `buf` receives a view, a constant or an assembled
 * projection; `tmp` is the second buffer a view OVER one of those needs, and
 * the two must be distinct arrays. */
const cq_bit *cq_fm_op64(const cq_fmul_block *k, const uint32_t *off, int s,
                         cq_bit *buf, cq_bit *tmp);

/* The view chain, exposed for the one caller outside the resolver: nothing
 * yet, and it is here because `cq_fm_val64` and `cq_fm_op64` are two entry
 * points into one walk. Returns the first non-view operand and yields the
 * collapsed `(shift, mask)`. */
int  cq_fm_view_collapse(const cq_fmul_block *k, int s, int *shift,
                         uint64_t *mask);
void cq_fm_view_fill(const cq_bit *base, int shift, uint64_t mask,
                     cq_bit *out);

#endif /* CQOPS_KERNELS_FMUL_INT_H */
