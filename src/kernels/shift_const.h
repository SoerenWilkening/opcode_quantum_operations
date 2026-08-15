/* src/kernels/shift_const.h — M11, Step 11. K4 constant shl / lshr / ashr.
 *
 * FITS THE RULE 7 SHAPE LITERALLY, and that is not a coincidence: the shift
 * amount `k` arrives as the W classical bits of the `b` operand register,
 * which is exactly the register CQ_lang passes in the `_hl` shape. M11 is
 * selected over M12 precisely because those bits are classical.
 *
 * NATURALLY CLEAN — no cq_sandwich, no scratch, no ancilla, at any W and any k
 * (PRD §5; K04.md §4). Every gate targets a bit of `dst`; `a` appears only as
 * a control and is never written.
 *
 * D8 — MASK, THEN SATURATE (PRD §15, bd ckd.16, resolved 2026-08-15):
 *
 *     dst ^= sat_shift(a, k mod 2^ceil(log2 W))
 *
 * and the mask half is not arithmetic here, it is STRUCTURAL: these kernels
 * read only the low `S = ceil(log2 W)` bits of `b`, exactly as Bennett's
 * barrel shifter reads only `b[0..S-1]` as MUX controls. Bits at or above S
 * are not masked away, they are never looked at — which is what makes M11 and
 * M12 agree by construction rather than by discipline. Then, if the effective
 * amount still reaches W (possible only when W is not a power of two, e.g.
 * W=80 leaves amounts in [80,128)), the shift saturates: all-zero for
 * shl/lshr, all-sign for ashr.
 *
 * TWO CONSEQUENCES WORTH KNOWING BEFORE YOU ARE SURPRISED BY THEM. At W=32,
 * `x << 32` returns `x`, not 0 — 32 mod 32 is 0, and that is the knowing delta
 * from Bennett's constant path recorded in PRD §15. And at W=1, S is 0, no bit
 * of `b` is read at all, and every shift is the identity for every amount.
 *
 * `k` HAS NO SIGN HERE, and must not acquire one. Bennett's `k` is an Int
 * built through LLVMConstIntGetSExtValue, so upstream an i32 amount of
 * 0x80000000 arrives as a negative number; ours is decoded from `cq_bit`s,
 * where the notion does not exist. Do not port the sign test that goes with it.
 */
#ifndef CQOPS_KERNELS_SHIFT_CONST_H
#define CQOPS_KERNELS_SHIFT_CONST_H

#include "bit.h"
#include "ctx.h"

/* ceil(log2 W) for W >= 2, and 0 for W <= 1 — Bennett's `_shift_stages` guard
 * (arith.jl:348), which is what makes a width-1 shift the identity. Exposed
 * because the L1 reference model must apply the identical reduction, and a
 * second implementation of it is a second chance to get it wrong. */
int cq_shift_stages(int W);

/* The effective amount: the low `cq_shift_stages(W)` bits of `b`, as a plain
 * integer. Aborts if any bit it READS is not classical — that operand belongs
 * to M12. Bits at or above S may be anything, including qubits, because the
 * construction never reads them. */
int cq_shift_amount(const cq_bit *b, int W);

/* K4 — dst ^= a << k.   Bennett lower_shl!,  arith.jl:305-315.  W-k CX. */
void cq_kernel_shl (cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W);

/* K4 — dst ^= a >> k, zero-fill.  Bennett lower_lshr!, arith.jl:317-323. W-k CX. */
void cq_kernel_lshr(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W);

/* K4 — dst ^= a >> k, sign-fill.  Bennett lower_ashr!, arith.jl:325-331.
 * W CX, FLAT IN k: the sign bit is a control k+1 times, so this is a fan-out
 * and not the "pure index shuffle" PRD §6 calls K4 (K04.md §5(d)). */
void cq_kernel_ashr(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W);

#endif /* CQOPS_KERNELS_SHIFT_CONST_H */
