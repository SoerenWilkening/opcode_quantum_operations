/* src/kernels/fround_int.h — M38b's INTERNAL seam, K25. NOT a public header:
 * nothing outside `src/kernels/fround*.c` includes it (tests do, to reach the
 * vocabulary refusals the public block cannot), and every name in it is
 * `cq_fi_`-prefixed to say so.
 *
 * IT CARRIES THE THREE MACHINE SEAMS RECORDED IN ADVANCE (bd 9ve.27.2, M33's
 * three-seams-not-two finding):
 *
 *   fround_step.c     COSTS AND LAYOUT — what a row costs, asked of M12, M14,
 *                     M16, M17 and M33, and the memoised per-program prefix
 *                     map.
 *   fround_operand.c  OPERAND RESOLUTION — the collapsing view chain, the
 *                     constant vocabulary, the block outputs and the flags.
 *   fround_fadd.c     THE M33 HAND-OFF — `soft_fadd` as a step block.
 *   fround_emit.c     DISPATCH AND SURFACE.
 *
 * THE PREFIX MAP IS MEMOISED, AND THAT IS NOT THE CURSOR K21.md §2.6 FORBIDS.
 * It is a pure function of the five compile-time tables and the siblings'
 * published costs at W = 64; it depends on neither the step index nor the
 * block nor `k->off`, so recomputing it at any moment gives the same answer and
 * a desynchronisation across cq_sandwich's index reversal is unrepresentable.
 * `k->off` is applied in `cq_fi_sp` and NOWHERE ELSE.
 */
#ifndef CQOPS_KERNELS_FROUND_INT_H
#define CQOPS_KERNELS_FROUND_INT_H

#include "kernels/fround.h"

enum { CQ_FI_W = CQ_FP64_W, CQ_FI_MAXR = CQ_FROUND_MAX_ROWS };

/* What a row costs, ASKED of the owning module at width 64. */
int      cq_fi_row_steps (const cq_fround_row *r);
uint32_t cq_fi_row_region(const cq_fround_row *r);

/* One program's memoised prefix map: `slot[i]` is the first slot of row `i`
 * and `bit[i]` its first region bit RELATIVE to the block's base; both have
 * `n + 1` entries and the last is the total. */
typedef struct {
    int      n;
    int      nslot;
    uint32_t nbit;
    int      slot[CQ_FI_MAXR + 1];
    uint32_t bit [CQ_FI_MAXR + 1];
} cq_fi_map;

const cq_fi_map *cq_fi_map_get(cq_fround_prog p);

/* The fit check and the program-id check, which are the BLOCK's: without them
 * the first out-of-region span aborts in M08 naming the REGION rather than
 * the consumer that mis-sized its offset. Hard errors in both configurations. */
void cq_fi_arm(const cq_fround_block *k);

/* The LAST row whose first slot is `<= u` — which is the emitting row of a run
 * of zero-slot views, because `slot[i] < slot[i+1]` holds exactly there. */
int cq_fi_row_at(cq_fround_prog p, int u);

/* `at` is RELATIVE to the block's base; `cq_fi_base` is the ONE place `k->off`
 * is applied, for spans (`cq_fi_sp`) and for the inner blocks that own a
 * region and take an `off` of their own (the barrel, M33's fadd). */
uint32_t cq_fi_base(const cq_fround_block *k, uint32_t at);
cq_bit  *cq_fi_sp  (const cq_fround_block *k, uint32_t at, uint32_t len);

/* Row `i`'s value. Each refuses a row of the other width, and the 1-bit one is
 * the refusal that gets forgotten (bd a-table-driven-kernel-needs-both-
 * operand-refusals). */
const cq_bit *cq_fi_val64  (const cq_fround_block *k, int i);
const cq_bit *cq_fi_flag_of(const cq_fround_block *k, int i);

/* A 64-lane OPERAND. `buf` receives a view or a constant; `tmp` is the second
 * buffer a view OVER a constant needs, and the two must be distinct arrays. */
const cq_bit *cq_fi_op64(const cq_fround_block *k, int s, cq_bit *buf,
                         cq_bit *tmp);

/* The view chain: returns the first non-VIEW operand and yields the collapsed
 * `(shift, mask)`, walked outermost-in. */
int cq_fi_view_collapse(cq_fround_prog p, int s, int *shift, uint64_t *mask);

/* --- The M33 hand-off (fround_fadd.c). ----------------------------------- */

/* `soft_fadd(a, b)` as ONE row: its SLOT count, its REGION, one of its gates,
 * and its 64-lane result span, all ASKED of M33. `a` and `b` are controls. */
int           cq_fi_fadd_steps (void);
uint32_t      cq_fi_fadd_region(void);
void          cq_fi_fadd_step  (cq_ctx *ctx, const cq_bit *a, const cq_bit *b,
                                cq_scratch *scr, uint32_t off, int u);
const cq_bit *cq_fi_fadd_result(const cq_bit *a, const cq_bit *b,
                                cq_scratch *scr, uint32_t off);

#endif /* CQOPS_KERNELS_FROUND_INT_H */
