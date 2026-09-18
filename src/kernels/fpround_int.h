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

/* One helper, resolved: the three inputs by position, the caller's region, and
 * the row table. Built per entry point; nothing is cached across calls, for
 * fpclass.c's reason — a block carrying cached operands would carry state
 * whose initialisation a consumer can forget, and a forgotten bind is a silent
 * wrong circuit rather than a failure. */
typedef struct {
    const cq_bit         *in[3];
    cq_scratch           *scr;
    uint32_t              off;
    cq_fpround_id         id;
    const cq_fpround_row *rows;
    int                   n;
} cq_fpr_ctx;

cq_fpr_ctx cq_fpr_ctx_of(cq_fpround_id id, const cq_bit *a, const cq_bit *b,
                         const cq_bit *c, cq_scratch *scr, uint32_t off);

/* 1 for a row whose output is a Bool, CQ_FPR_W for the rest. A VIEW is 64. */
int      cq_fpr_op_width (int op);
int      cq_fpr_row_steps(const cq_fpround_row *r);
uint32_t cq_fpr_region_of(const cq_fpr_ctx *x);
int      cq_fpr_steps_of (const cq_fpr_ctx *x);

/* The prefix-offset walk plus the fit check, in ONE pass. `off` is
 * CQ_FPR_MAXR entries and every one is ABSOLUTE inside the region — this is
 * the one place `x->off` is applied, which is why a block cannot see its own
 * offset and why a two-blocks-in-one-region case is the only detector for
 * dropping it (bd a-block-that-ignores-its-own-off-needs-a-two-block-case). */
void     cq_fpr_arm(const cq_fpr_ctx *x, uint32_t *off);
cq_bit  *cq_fpr_sp (const cq_fpr_ctx *x, uint32_t at, uint32_t len);

/* Row `i`'s 64-lane output span, and row `i`'s one-bit flag. Each refuses a
 * row of the other width: the 64-lane resolver refusing an unknown code is the
 * obvious guard and the 1-bit one is the one that gets forgotten (bd
 * a-table-driven-kernel-needs-both-operand-refusals, measured on M36). */
cq_bit       *cq_fpr_row_out(const cq_fpr_ctx *x, const uint32_t *off, int i);
const cq_bit *cq_fpr_flag_of(const cq_fpr_ctx *x, const uint32_t *off, int i);

/* A 64-lane operand. `buf` receives a view or a constant; `tmp` is the second
 * buffer a view OVER a constant needs, and the two must be distinct arrays. */
const cq_bit *cq_fpr_op64(const cq_fpr_ctx *x, const uint32_t *off, int s,
                          cq_bit *buf, cq_bit *tmp);

/* The view chain, for `cq_subnorm_flushed` — the one OUTPUT that is a view and
 * so has no home in the caller's region (D-K23-8). Returns the first non-view
 * operand and yields the collapsed (shift, mask). */
int  cq_fpr_view_collapse(const cq_fpr_ctx *x, int s, int *shift,
                          uint64_t *mask);
void cq_fpr_view_fill(const cq_bit *base, int shift, uint64_t mask,
                      cq_bit *out);
const cq_bit *cq_fpr_base64(const cq_fpr_ctx *x, const uint32_t *off, int s,
                            cq_bit *buf);

#endif /* CQOPS_KERNELS_FPROUND_INT_H */
