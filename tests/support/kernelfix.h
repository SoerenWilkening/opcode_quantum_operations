/* tests/support/kernelfix.h — the shared driver's FIXTURE: the internal joint
 * between kerneldrv.c (the four levels) and kernelmeasure.c (L4's instrument).
 *
 * Split 2026-09-02 (bd f8c) on the third seam plan §2.2 records for the
 * driver, `the GATE` against `the INSTRUMENT`: what ASSERTS stays in
 * kerneldrv.c, and what only MEASURES — cq_kd_measure and cq_kd_peak, whose
 * readings the L4 goldens, each suite's zero-ancilla claim and kernelctrl.c's
 * promotion identity pin elsewhere — lives next door. Both halves open the
 * same context over a counting sink, resolve the same shape and reach the
 * kernel through the same one push site, so those five are declared here and
 * defined ONCE, in the gate half.
 *
 * Nothing outside tests/support/ includes this header. The public surface
 * stays in kerneldrv.h, on kernelctrl.h's precedent: a suite should see one
 * driver, not three.
 */
#ifndef CQOPS_TEST_KERNELFIX_H
#define CQOPS_TEST_KERNELFIX_H

#include "ctx.h"
#include "sink_count.h"
#include "support/kerneldrv.h"

typedef struct {
    cq_ctx     ctx;
    cq_counter cnt;
    cq_sink    sink;
} cq_kd_fixture;

/* Opens a context over a counting sink. The close disposes it WITHOUT freeing
 * the operand rails — Rule 6's intended safe leak, see the definition — and
 * forgets the control rail, so a refused shape cannot push a stale handle into
 * the next case's table. */
void cq_kd_fx_open(cq_kd_fixture *f);
void cq_kd_fx_close(cq_kd_fixture *f);

/* Resolves the spec's shape at W. Returns 0 — having already reported through
 * the harness — for a shape the default call path cannot serve (bd zwh); the
 * caller must then allocate nothing. */
int cq_kd_shape_of(const cq_kd_spec *k, int W, cq_kd_shape *sh);

/* THE ONE PLACE THE AXIS ENTERS: push the active control rail, call the kernel
 * through the spec's adapter or the default path, pop. Every kernel invocation
 * in the driver — two in cq_kd_case, three in kernelmeasure.c — goes through
 * it, which is what makes Step 20 "one parameter, not twelve new suites". */
void cq_kd_call_kernel(const cq_kd_spec *k, cq_ctx *ctx, cq_bit *dst,
                       const cq_bit *const *src, const cq_kd_shape *sh);

#endif /* CQOPS_TEST_KERNELFIX_H */
