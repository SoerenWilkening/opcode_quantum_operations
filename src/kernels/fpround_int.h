/* src/kernels/fpround_int.h — M32's INTERNAL seam, K23 §5.8 / D-K23-10. NOT a
 * public header: nothing outside `src/kernels/fpround*.c` includes it, and
 * every name in it is `cq_fpr_`-prefixed to say so.
 *
 * THE SEAM IT CARRIES IS LAYOUT-AND-OPERANDS <-> DISPATCH-AND-SURFACE, and it
 * was taken at implementation because the step machine MEASURED 418 non-blank
 * non-comment lines against Rule 12's 300 — a scheduled split in the sense
 * that it was hit rather than surprised by, exactly as M36's own second cut
 * was (K18.md D-K18-7), and for the same reason one size up: K18's machine
 * drives 42 rows over eight ops and M32's drives 154 over fourteen.
 *
 *   fpround_step.c   what a row COSTS (asked of M12/M14/M16/M17), where its
 *                    spans LIE inside the caller's region, and what each
 *                    operand code RESOLVES to — the collapsing view chain,
 *                    the constant vocabulary, the block outputs and the flags.
 *   fpround_emit.c   which GATE a slot emits, and the four public blocks'
 *                    entry points and accessors.
 *
 * The measured split is 229 and 204 lines, so neither half is within fifty of
 * the limit — which is the point of taking a seam rather than shaving one.
 */
#ifndef CQOPS_KERNELS_FPROUND_INT_H
#define CQOPS_KERNELS_FPROUND_INT_H

#include "kernels/fpround.h"

enum { CQ_FPR_W = CQ_FP64_W, CQ_FPR_MAXR = CQ_FPROUND_MAX_ROWS };

/* THE PREFIX MAP, ONE PER HELPER, BUILT ONCE — M34's cq_fm_map_get and M39's
 * cq_fu_map_get, one module down (bd a-395-row-flat-table-needs-the-memoised-
 * prefix-map). Until 2026-10-05 every STEP re-asked M12/M14/M16/M17 for every
 * row's cost three times (the bound check, the prefix walk, the linear
 * dispatch) and every ACCESSOR once more; M34, M39 and M33 call those
 * accessors per gate of their own, and macOS `sample` put the walk at the top
 * of test_kernel_fmul's self time. It holds TABLE LAYOUT ONLY — a pure
 * function of a compile-time row table and of the siblings' published costs at
 * W=64 — never an operand, a span or circuit state, and depends on neither the
 * step index nor `x->off`; recomputing it at any moment gives the same answer,
 * so the sandwich's index reversal cannot desynchronise it. `step0` is
 * NON-DECREASING (a view owns no slot), so the dispatch searches for the LAST
 * row whose `step0 <= u`. */
typedef struct {
    const cq_fpround_row *rows;
    int                   n;
    uint32_t              rel[CQ_FPR_MAXR];       /* RELATIVE to the base  */
    int                   step0[CQ_FPR_MAXR + 1]; /* [n] is the slot total */
    uint32_t              region;
    int                   steps;
} cq_fpr_map;

const cq_fpr_map *cq_fpr_map_get(cq_fpround_id id);

/* One helper, resolved: the three inputs by position, the caller's region, and
 * the helper's map. Built per entry point; no OPERAND is cached across calls,
 * for fpclass.c's reason — a block carrying cached operands would carry state
 * whose initialisation a consumer can forget, and a forgotten bind is a silent
 * wrong circuit rather than a failure. */
typedef struct {
    const cq_bit         *in[3];
    cq_scratch           *scr;
    uint32_t              off;
    cq_fpround_id         id;
    const cq_fpr_map     *map;
    const cq_fpround_row *rows;
    int                   n;
} cq_fpr_ctx;

cq_fpr_ctx cq_fpr_ctx_of(cq_fpround_id id, const cq_bit *a, const cq_bit *b,
                         const cq_bit *c, cq_scratch *scr, uint32_t off);

/* 1 for a row whose output is a Bool, CQ_FPR_W for the rest. A VIEW is 64. */
int      cq_fpr_op_width (int op);
int      cq_fpr_row_steps(const cq_fpround_row *r);

/* The fit check, against the map's region total. */
void     cq_fpr_arm(const cq_fpr_ctx *x);

/* Row `i`'s span, ABSOLUTE inside the region: the map's relative offset plus
 * `x->off`. This is the one place `x->off` is applied, which is why a block
 * cannot see its own offset and why a two-blocks-in-one-region case is the
 * only detector for dropping it (bd
 * a-block-that-ignores-its-own-off-needs-a-two-block-case). */
uint32_t cq_fpr_at (const cq_fpr_ctx *x, int i);
cq_bit  *cq_fpr_sp (const cq_fpr_ctx *x, uint32_t at, uint32_t len);

/* Row `i`'s 64-lane output span, and row `i`'s one-bit flag. Each refuses a
 * row of the other width: the 64-lane resolver refusing an unknown code is the
 * obvious guard and the 1-bit one is the one that gets forgotten (bd
 * a-table-driven-kernel-needs-both-operand-refusals, measured on M36). */
cq_bit       *cq_fpr_row_out(const cq_fpr_ctx *x, int i);
const cq_bit *cq_fpr_flag_of(const cq_fpr_ctx *x, int i);

/* A 64-lane operand. `buf` receives a view or a constant; `tmp` is the second
 * buffer a view OVER a constant needs, and the two must be distinct arrays. */
const cq_bit *cq_fpr_op64(const cq_fpr_ctx *x, int s, cq_bit *buf,
                          cq_bit *tmp);

/* The view chain, for `cq_subnorm_flushed` — the one OUTPUT that is a view and
 * so has no home in the caller's region (D-K23-8). Returns the first non-view
 * operand and yields the collapsed (shift, mask). */
int  cq_fpr_view_collapse(const cq_fpr_ctx *x, int s, int *shift,
                          uint64_t *mask);
void cq_fpr_view_fill(const cq_bit *base, int shift, uint64_t mask,
                      cq_bit *out);
const cq_bit *cq_fpr_base64(const cq_fpr_ctx *x, int s, cq_bit *buf);

#endif /* CQOPS_KERNELS_FPROUND_INT_H */
