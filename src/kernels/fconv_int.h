/* src/kernels/fconv_int.h — M37's INTERNAL seam, K19. NOT a public header:
 * nothing outside `src/kernels/fconv*.c` includes it, and every name in it is
 * `cq_fv_`-prefixed to say so.
 *
 * IT CARRIES ONE MACHINE SEAM AND RECORDS A SECOND AS AVAILABLE. The one taken
 * is M32's LAYOUT-AND-OPERANDS <-> DISPATCH-AND-SURFACE (D-K23-10), which M33
 * took too. The one recorded and NOT taken is M33's fourth,
 * COSTS-AND-LAYOUT <-> OPERAND RESOLUTION: bd
 * a-row-table-fp-kernel-needs-three-seams-not-two measured that the third and
 * fourth rungs are driven by the OP VOCABULARY rather than by the row count,
 * and M37's vocabulary is FOURTEEN ops with no destructured tuple, no picked
 * output and no M32 hand-off, against M33's nineteen with three tuples. If
 * fconv_step.c ever crosses Rule 12's 300 lines, that is the cut to take and
 * `cq_fv_op64` / `cq_fv_flag_of` / `cq_fv_row_out` are the half that moves.
 *
 *   fconv_step.c    what a row COSTS (asked of M12, M14, M16, M17 and M33),
 *                   how many bits it owns, the prefix-offset walk, and what
 *                   each operand code RESOLVES to — including the ZEXT VIEW
 *                   that is the whole of `uitofp`.
 *   fconv_emit.c    which GATE a slot emits, the exported `cq_fptosi_block`,
 *                   and the four Rule 7 kernels.
 */
#ifndef CQOPS_KERNELS_FCONV_INT_H
#define CQOPS_KERNELS_FCONV_INT_H

#include "kernels/fconv.h"

enum { CQ_FV_W = CQ_FP64_W, CQ_FV_MAXR = CQ_FCONV_MAX_ROWS };

/* One program, resolved: the rail and ITS WIDTH, the caller's region, and the
 * rows. Built per entry point; nothing is cached across calls, for fpclass.c's
 * reason — a block carrying cached operands would carry state whose
 * initialisation a consumer can forget, and a forgotten bind is a silent wrong
 * circuit rather than a failure.
 *
 * `a_w` IS THE WHOLE OF `uitofp` AND IT IS NOT A KERNEL WIDTH. PRD-v2 §1
 * scopes v2 to f64 so every row runs at 64; `a_w < 64` makes `CQ_FV_A` resolve
 * to a 64-lane ZEXT VIEW — lanes [a_w, 64) `CQ_BIT_ZERO`, no copy, no scratch,
 * no gate — which is instructions.jl:7677-7679's widening cast under §7.3's
 * amendment that a compile-time-constant widening is WIRING. The row program
 * is byte-for-byte `sitofp`'s at every source width, which is asserted rather
 * than asserted-by-comment in tests/test_kernel_fconv_slots.inc. */
typedef struct {
    const cq_bit       *a;
    int                 a_w;
    cq_scratch         *scr;
    uint32_t            off;
    const cq_fconv_row *rows;
    int                 n;
} cq_fv_ctx;

/* 1 for a row whose output is a Bool, CQ_FV_W for the rest. A VIEW is 64. */
int      cq_fv_op_width(const cq_fv_ctx *x, int i);

int      cq_fv_row_steps(const cq_fconv_row *r);
uint32_t cq_fv_region_of(const cq_fv_ctx *x);
int      cq_fv_steps_of (const cq_fv_ctx *x);
void     cq_fv_check_program(const cq_fconv_row *rows, int n);

/* THE PREFIX-OFFSET WALK, THE FIT CHECK, THE REGION TOTAL AND THE SLOT TOTAL
 * IN ONE PASS — M36's shape, and here it is a measured cost rather than
 * tidiness. `cq_fv_step` runs once per slot and `fptoui` has ~69,000 of them
 * over 74 rows; M33's step does three O(n) walks (the range check, the arm,
 * the dispatch), each calling every row's owning module for its cost, so the
 * same shape here would be three times this. `off` is CQ_FV_MAXR entries and
 * every one is ABSOLUTE inside the region — this is the ONE place `x->off` is
 * applied, which is why a program cannot see its own offset and why a
 * two-blocks-in-one-region case is the only detector for dropping it
 * (bd a-block-that-ignores-its-own-off-needs-a-two-block-case). */
void     cq_fv_arm(const cq_fv_ctx *x, uint32_t *off, int *slots);
cq_bit  *cq_fv_sp (const cq_fv_ctx *x, uint32_t at, uint32_t len);

/* Row `i`'s 64-lane output span, and row `i`'s one-bit flag. Each refuses a
 * row of the other width: the 64-lane resolver refusing an unknown code is the
 * obvious guard and the 1-bit one is the one that gets forgotten (bd
 * a-table-driven-kernel-needs-both-operand-refusals, measured on M36). */
const cq_bit *cq_fv_row_out(const cq_fv_ctx *x, const uint32_t *off, int i);
const cq_bit *cq_fv_flag_of(const cq_fv_ctx *x, const uint32_t *off, int i);

/* A 64-lane operand. `buf` receives a view, a constant or the zext of a narrow
 * rail; `tmp` is the second buffer a view OVER a constant or over the zext
 * needs, and the two must be distinct arrays. */
const cq_bit *cq_fv_op64(const cq_fv_ctx *x, const uint32_t *off, int s,
                         cq_bit *buf, cq_bit *tmp);

/* ONE GATE OF A PROGRAM, and the SURFACE THE DEATH TESTS DRIVE. The public
 * `cq_fptosi_block` carries no row pointer on purpose — a consumer that had to
 * fill one could fill it wrong (bd
 * a-step-block-wires-its-own-inner-block-never-the-consumer) — so every
 * vocabulary refusal in fconv_step.c is UNREACHABLE through the public surface
 * and would be an assertion nobody has seen fail. This entry point is how they
 * are reached: tests include this internal header (src/ is on the test
 * support's PUBLIC include path) and drive a hand-built `cq_fv_ctx`. */
void          cq_fv_step  (cq_ctx *ctx, const cq_fv_ctx *x, int u);
const cq_bit *cq_fv_result(const cq_fv_ctx *x, cq_fconv_prog p);

#endif /* CQOPS_KERNELS_FCONV_INT_H */
