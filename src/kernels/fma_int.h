/* src/kernels/fma_int.h — M39's INTERNAL seams, THREE of them across four
 * machine files. NOT a public header: nothing outside `src/kernels/fma*.c`
 * includes it, and every name in it is `cq_fu_`-prefixed to say so.
 *
 * M33's landing measured that a row-table fp kernel needs FOUR machine seams
 * rather than two, and that what drives it is the OP VOCABULARY rather than
 * the row count (bd memory a-row-table-fp-kernel-needs-three-seams-not-two).
 * K20 has 21 ops and 395 rows — more of both than M33, M34 or M35 — so all
 * four were recorded in fma.h before a line was written, plus a fifth for the
 * program text itself:
 *
 *   fma_rows.inc    the ROW TABLE and its enumerators.
 *   fma.c           the table's accessors, the arity, the row widths.
 *   fma_step.c      what a row COSTS (asked of M12/M14/M16/M17/M18/M31/M32)
 *                   and where its spans LIE inside the caller's region.
 *   fma_operand.c   what each operand code RESOLVES to — the collapsing view
 *                   chain, the constant vocabulary, the block outputs, the
 *                   hand-off projections and the flags.
 *   fma_emit.c      which GATE a slot emits, the public block's entry points
 *                   and accessors, and the three-source kernel.
 */
#ifndef CQOPS_KERNELS_FMA_INT_H
#define CQOPS_KERNELS_FMA_INT_H

#include "kernels/fma.h"

enum { CQ_FU_W = CQ_FP64_W, CQ_FU_MAXR = CQ_FMA_MAX_ROWS };

/* What a row costs. Every term is ASKED of its owning module at width 64 or is
 * upstream's own four-gate bitwise vocabulary; not one line of either function
 * would move if the row table changed. */
int      cq_fu_row_steps (const cq_fma_row *r);
uint32_t cq_fu_row_region(const cq_fma_row *r);

/* --- THE PREFIX MAP, MEMOISED — M40's shape, and for M40's reason ----------
 *
 * A 395-ROW TABLE WALKED TWICE PER STEP IS 790 MODULE CALLS PER GATE. M34's
 * step machine rebuilds its prefix array and then walks the table again to
 * find the row, which is fine at 128 rows; here it is `395 x 241,083 x 2` row
 * visits per compute half, and L1 drives four compute halves per case at a
 * forced floor of 167 cases. Measured: the suite did not finish.
 *
 * IT IS NOT A CURSOR AND IT IS NOT THE CACHED TABLE Rule 13 FORBIDS. The map
 * is a pure function of the ROW TABLE (a compile-time constant) and of the
 * siblings' PUBLISHED COSTS at width 64 — `cq_eq_steps(64)` and the rest,
 * which are themselves constants. It depends on neither the step index nor
 * `k->off`, and recomputing it at any moment gives the same answer, so a
 * desynchronisation across `cq_sandwich`'s index reversal is unrepresentable.
 * That is exactly the argument K21.md makes for `cq_fs_map_get`.
 *
 * `k->off` IS STILL APPLIED IN EXACTLY ONE PLACE. The map holds offsets
 * RELATIVE to the block's base; `cq_fu_arm` adds `k->off` to each, and nothing
 * else does. That is what keeps a dropped `off` detectable by
 * two-programs-in-one-region and by nothing else. */
typedef struct {
    uint32_t rel [CQ_FMA_MAX_ROWS];  /* row i's bits, relative to the base */
    int      step0[CQ_FMA_MAX_ROWS]; /* row i's FIRST slot index           */
    uint32_t region;                 /* the whole program's bits           */
    int      steps;                  /* the whole program's slots          */
    int      n;
} cq_fu_map;

const cq_fu_map *cq_fu_map_get(void);

/* The row owning slot `u`, by binary search over `step0`. `within` receives
 * the index INSIDE that row. Out of range is the caller's to refuse. */
int cq_fu_row_at(const cq_fu_map *m, int u, int *within);

/* The prefix-offset walk plus the fit check, in ONE pass. `off` is CQ_FU_MAXR
 * entries and every one is ABSOLUTE inside the region — this is the one place
 * `k->off` is applied, which is why a block cannot see its own offset and why
 * a two-programs-in-one-region case is the only detector for dropping it (bd
 * a-step-block-cannot-see-its-own-offset-and-inherits-the-kernels-w1-hole). */
void    cq_fu_arm(const cq_fma_block *k, uint32_t *off);
cq_bit *cq_fu_sp (const cq_fma_block *k, uint32_t at, uint32_t len);

/* Row `i`'s value. `cq_fu_val64` needs a 64-entry `buf` for a view, a constant
 * or `flushed_result`; `cq_fu_flag_of` is pure addressing. Each refuses a row
 * of the other width, and the 1-bit one is the refusal that gets forgotten
 * (bd a-table-driven-kernel-needs-both-operand-refusals, measured on M36). */
const cq_bit *cq_fu_val64  (const cq_fma_block *k, const uint32_t *off, int i,
                            cq_bit *buf);
const cq_bit *cq_fu_flag_of(const cq_fma_block *k, const uint32_t *off, int i);

/* A 64-lane OPERAND. `buf` receives a view, a constant or an assembled
 * projection; `tmp` is the second buffer a view OVER one of those needs, and
 * the two must be distinct arrays. */
const cq_bit *cq_fu_op64(const cq_fma_block *k, const uint32_t *off, int s,
                         cq_bit *buf, cq_bit *tmp);

/* The view chain. Returns the first non-view operand and yields the collapsed
 * `(shift, mask)`; exposed because `cq_fu_val64` and `cq_fu_op64` are two
 * entry points into one walk. */
int  cq_fu_view_collapse(const cq_fma_block *k, int s, int *shift,
                         uint64_t *mask);
void cq_fu_view_fill(const cq_bit *base, int shift, uint64_t mask,
                     cq_bit *out);

/* The barrel's direction for a row — M12's step count takes it, because
 * `shl`/`lshr` emit `W - 2^k` shuffle CNOTs per stage where `ashr` emits `W`
 * (shift_var.h). Shared by the cost half and the dispatch half, which is
 * exactly the kind of fact a second copy gets wrong. */
int cq_fu_barrel_dir_of(int op);

#endif /* CQOPS_KERNELS_FMA_INT_H */
