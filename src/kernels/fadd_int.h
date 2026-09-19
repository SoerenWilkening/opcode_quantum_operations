/* src/kernels/fadd_int.h — M33's INTERNAL seam, K15. NOT a public header:
 * nothing outside `src/kernels/fadd*.c` includes it, and every name in it is
 * `cq_fa_`-prefixed to say so.
 *
 * IT CARRIES TWO SEAMS, AND THE SECOND WAS FORCED BY MEASUREMENT. The first is
 * M32's LAYOUT-AND-OPERANDS ↔ DISPATCH-AND-SURFACE (D-K23-10), taken here in
 * advance. The second, COSTS-AND-LAYOUT ↔ OPERAND RESOLUTION, was taken at
 * implementation because the two halves together measured 326 of Rule 12's 300
 * lines — one cut more than M32 needed, and the reason is the OP VOCABULARY
 * rather than the row count: M32's machine drives 154 rows over fourteen ops
 * and M33's drives 133 over NINETEEN, three of which resolve a DESTRUCTURED
 * TUPLE rather than a span.
 *
 *   fadd_step.c     what a row COSTS (asked of M12, M14, M16, M17, M31 and
 *                   M32), how many bits it owns, and the prefix-offset walk.
 *   fadd_operand.c  what each operand code RESOLVES to — the collapsing view
 *                   chain, the constant vocabulary, the block outputs, the
 *                   flags and M32's three hand-offs.
 *   fadd_emit.c     which GATE a slot emits, the exported `cq_fsub_block`, and
 *                   the two Rule 7 kernels.
 */
#ifndef CQOPS_KERNELS_FADD_INT_H
#define CQOPS_KERNELS_FADD_INT_H

#include "kernels/fadd.h"
#include "kernels/fpclass.h"
#include "kernels/fpround.h"

enum { CQ_FA_W = CQ_FP64_W, CQ_FA_MAXR = CQ_FADD_MAX_ROWS };

/* One program, resolved: the two rails, the caller's region, and the rows.
 * Built per entry point; nothing is cached across calls, for fpclass.c's
 * reason — a block carrying cached operands would carry state whose
 * initialisation a consumer can forget, and a forgotten bind is a silent wrong
 * circuit rather than a failure. */
typedef struct {
    const cq_bit      *a, *b;
    cq_scratch        *scr;
    uint32_t           off;
    const cq_fadd_row *rows;
    int                n;
} cq_fa_ctx;

/* 1 for a row whose output is a Bool, CQ_FA_W for the rest. A VIEW is 64; a
 * PICK is whichever width the output it names has. */
int      cq_fa_op_width (const cq_fa_ctx *x, int i);

/* The two facts BOTH halves of the split machine need: a class row's
 * `cq_fp_class` (the cost side to price the row, the operand side to build the
 * block) and whether a picked output is one of M32's four flags — the second
 * being what makes a PICK one bit wide or sixty-four. */
cq_fp_class cq_fa_class_of    (const cq_fadd_row *r);
int         cq_fa_pick_is_flag(int block_op, int which);

int      cq_fa_row_steps(const cq_fadd_row *r);
uint32_t cq_fa_region_of(const cq_fa_ctx *x);
int      cq_fa_steps_of (const cq_fa_ctx *x);
void     cq_fa_check_program(const cq_fadd_row *rows, int n);

/* The prefix-offset walk plus the fit check, in ONE pass. `off` is CQ_FA_MAXR
 * entries and every one is ABSOLUTE inside the region — this is the one place
 * `x->off` is applied, which is why a program cannot see its own offset and
 * why a two-programs-in-one-region case is the only detector for dropping it
 * (bd a-block-that-ignores-its-own-off-needs-a-two-block-case). */
void     cq_fa_arm(const cq_fa_ctx *x, uint32_t *off);
cq_bit  *cq_fa_sp (const cq_fa_ctx *x, uint32_t at, uint32_t len);

/* Row `i`'s 64-lane output span, and row `i`'s one-bit flag. Each refuses a
 * row of the other width: the 64-lane resolver refusing an unknown code is the
 * obvious guard and the 1-bit one is the one that gets forgotten (bd
 * a-table-driven-kernel-needs-both-operand-refusals, measured on M36). */
cq_bit       *cq_fa_row_out(const cq_fa_ctx *x, const uint32_t *off, int i);
const cq_bit *cq_fa_flag_of(const cq_fa_ctx *x, const uint32_t *off, int i);

/* Four 64-lane scratch arrays, one frame of operand resolution. A block that
 * embedded its inner operands as FIELDS would hand the consumer fields whose
 * only correct value is the ones computed here (bd
 * a-step-block-wires-its-own-inner-block-never-the-consumer), so the three
 * hand-off assemblers below take a frame and fill it. */
typedef struct { cq_bit v[4][CQ_FP64_W]; } cq_fa_bufs;

/* A 64-lane operand. `buf` receives a view, a constant or an assembled PICK;
 * `tmp` is the second buffer a view OVER a constant needs, and the two must be
 * distinct arrays. */
const cq_bit *cq_fa_op64(const cq_fa_ctx *x, const uint32_t *off, int s,
                         cq_bit *buf, cq_bit *tmp);

/* ONE GATE OF A PROGRAM, and the SURFACE THE DEATH TESTS DRIVE. The public
 * `cq_fsub_block` carries no row pointer on purpose — a consumer that had to
 * fill one could fill it wrong, which is the hazard bd
 * a-step-block-wires-its-own-inner-block-never-the-consumer records — so every
 * vocabulary refusal in fadd_step.c is UNREACHABLE through the public surface
 * and would be an assertion nobody has seen fail. This entry point is how they
 * are reached: tests include this internal header (src/ is on the test
 * support's PUBLIC include path) and drive a hand-built `cq_fa_ctx`. Nothing
 * in src/ or shim/ outside `fadd*.c` includes it. */
void          cq_fa_step  (cq_ctx *ctx, const cq_fa_ctx *x, int u);
const cq_bit *cq_fa_result(const cq_fa_ctx *x, cq_fadd_prog p);

/* The three M32 hand-offs, assembled from row `i`'s own operands. */
cq_clz_block     cq_fa_clz_of    (const cq_fa_ctx *x, const uint32_t *off,
                                  int i, cq_fa_bufs *bf);
cq_subnorm_block cq_fa_subnorm_of(const cq_fa_ctx *x, const uint32_t *off,
                                  int i, cq_fa_bufs *bf);
cq_round_block   cq_fa_round_of  (const cq_fa_ctx *x, const uint32_t *off,
                                  int i, cq_fa_bufs *bf);

#endif /* CQOPS_KERNELS_FADD_INT_H */
