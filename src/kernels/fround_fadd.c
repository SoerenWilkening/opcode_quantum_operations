/* src/kernels/fround_fadd.c — M38b, K25. THE M33 HAND-OFF: `soft_fadd` as one
 * row of soft_floor's and soft_ceil's programs (fround.jl:73, :93).
 *
 * WHAT IS COMPOSED IS M33's EXPORTED STEP BLOCK, `cq_fadd_block` — the twin of
 * `cq_fsub_block`, added to M33 for exactly this caller (bd 9ve.27.2) — over
 * M33's own 124-row ADD program. Transcribing `soft_fadd` again here would be
 * a second copy of M33 (Rule 1 applied to the call graph, M40's argument for
 * composing M31's class block), and a nested KERNEL is refused by cq_sandwich
 * in both configurations (bd composite-kernels-call-the-step-function-not-
 * the-kernel). `b` is a CONSTANT span here (`-1.0` or `+1.0`).
 *
 * The seam is kept as its own file although each body is one call: it is the
 * one place fround names M33, so a change to M33's block contract lands here
 * and nowhere else.
 */

#include "kernels/fround_int.h"

#include "kernels/fadd.h"

static cq_fadd_block fi_fadd(const cq_bit *a, const cq_bit *b,
                             cq_scratch *scr, uint32_t off)
{
    cq_fadd_block k;

    k.a = a; k.b = b; k.scr = scr; k.off = off;
    return k;
}

int      cq_fi_fadd_steps (void) { return cq_fadd_steps(); }
uint32_t cq_fi_fadd_region(void) { return cq_fadd_region(); }

void cq_fi_fadd_step(cq_ctx *ctx, const cq_bit *a, const cq_bit *b,
                     cq_scratch *scr, uint32_t off, int u)
{
    cq_fadd_block k = fi_fadd(a, b, scr, off);

    cq_fadd_step(ctx, &k, u);
}

const cq_bit *cq_fi_fadd_result(const cq_bit *a, const cq_bit *b,
                                cq_scratch *scr, uint32_t off)
{
    cq_fadd_block k = fi_fadd(a, b, scr, off);

    return cq_fadd_result(&k);
}
