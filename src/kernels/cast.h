/* src/kernels/cast.h — M13, Step 11. K5 sext / zext / trunc.
 *
 * THE ONE PLACE THE RULE 7 SHAPE DOES NOT FIT, and it is a width problem, not
 * an arity problem. A cast is unary but carries TWO widths, F in and T out, so
 * there is no single `W` and no `b`. K05.md §2 prefaces its prototypes with
 * "K5 is unary, so the `b` operand is unused" and then prints a signature with
 * no `b` and two ints — the sentence and the prototype describe different
 * shapes, and the printed one is the right one. These are not assignable to
 * `cq_kernel_fn`; the shared test driver reaches them through a one-line call
 * adapter (`cq_kd_spec.call`), which is also how K10's three operands will be
 * reached at Step 14.
 *
 * CASTS ARE THE ONLY WAY AN i128 REGISTER EXISTS AT ALL — there is no
 * cqrt_alloc_i128, no cqrt_measure_i128 and no icmp at i128, because the C ABI
 * shreds __int128 into {i64,i64} at a function boundary. An i128 register is
 * born from a zext/sext here and dies at a trunc here. That is why M13's suite
 * carries a two-word reference model rather than stopping at W <= 64.
 *
 * NATURALLY CLEAN — three CX loops, zero X, zero CCX, zero ancillae, at every
 * (F,T) and every bit-kind mask, forward and uncompute (K05.md §4).
 *
 * DO NOT "OPTIMISE" trunc INTO A SLICE. Reparenting `a`'s low T qubits into
 * `dst` is the same I2 violation K04.md warns about for shifts, and it is more
 * tempting here because a trunc genuinely LOOKS like a pure slice. `a` stays
 * live, so the copy must be physical (Rule 5), and the discarded high bits are
 * bits of the caller's register: K5 neither reads, writes, nor frees them —
 * they are not ancillae and they are not provably |0>, so freeing them would be
 * the silent state collapse Rule 6 exists to catch.
 */
#ifndef CQOPS_KERNELS_CAST_H
#define CQOPS_KERNELS_CAST_H

#include "bit.h"
#include "ctx.h"

/* dst ^= zext(a).  F CX: the T-F high bits take no gate and no qubit — they are
 * born CQ_BIT_ZERO and stay so, permanently (I4). Requires T >= F. */
void cq_kernel_zext (cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T);

/* dst ^= sext(a).  T CX: F value copies plus T-F broadcasts of the sign bit
 * a[F-1], which is therefore a control T-F+1 times. Requires T >= F. */
void cq_kernel_sext (cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T);

/* dst ^= trunc(a). T CX. The high bits a[T..F-1] are never read and never
 * written. Requires T <= F. */
void cq_kernel_trunc(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T);

#endif /* CQOPS_KERNELS_CAST_H */
