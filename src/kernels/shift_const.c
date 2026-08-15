/* src/kernels/shift_const.c — M11, Step 11. K4 constant shl / lshr / ashr.
 *
 * Read docs/constructions/K04.md and PRD §15 D8 before changing anything here.
 * The three loops are Bennett's (arith.jl:305-331); the amount reduction is
 * D8 and is ours, forced by the fact that Bennett's constant path throws where
 * we must not.
 *
 * IT IS STILL NOT A FREE RELABELLING. The tempting move — `dst[i] = a[i-k]`,
 * copy the cq_bit struct, zero gates even for quantum bits — is forbidden by
 * I2: no qubit index may appear in two live registers, and that is exactly
 * what makes cqrt_free sound. `a` is still live after the shift (Rule 7 says
 * so), so the copy must be PHYSICAL: materialise + CX, per Rule 5. A genuine
 * zero-gate relabelling would be sound only if `a` were dead at the shift, and
 * libcqops has no liveness information — CQ_lang owns liveness and tells us
 * only via cqrt_free (K04.md §5(c)).
 */

#include "kernels/shift_const.h"

#include "emit.h"
#include "kernels/kernel.h"

int cq_shift_stages(int W)
{
    int s = 0;

    if (W <= 1) return 0;          /* Bennett's `W <= 1 ? 0` guard, verbatim */
    while ((1 << s) < W) s++;      /* ceil(log2 W) */
    return s;
}

int cq_shift_amount(const cq_bit *b, int W)
{
    int s = cq_shift_stages(W);
    int k = 0;

    for (int i = 0; i < s; i++) {
        if (!cq_bit_is_const(b[i]))
            cq_kernel_die("constant shift: the amount is not classical in the "
                          "bits the construction reads — that operand is M12's "
                          "(the variable barrel shifter), not M11's");

        if (cq_bit_value(b[i])) k |= 1 << i;
    }

    /* Bits at or above S are deliberately not examined. They are structurally
     * invisible to the barrel too — never MUX controls — so they cannot affect
     * the result and their bit-kind is not M11's business. That is the whole
     * mechanism by which D8's "mask" half costs nothing. */
    return k;
}

/* dst ^= a << k.  Positions below k take no gate at all: they are born
 * CQ_BIT_ZERO and stay so, which is 0 qubits as well as 0 gates (I4). That is
 * K04.md §5(a)'s headline saving over Bennett, which must allocate W wires
 * regardless because a wire is the only representation it has. */
void cq_kernel_shl(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W)
{
    cq_kernel_check_dst(dst, a, b, W);

    int k = cq_shift_amount(b, W);

    for (int i = k; i < W; i++)
        cq_emit_cx(ctx, &a[i - k], &dst[i]);
}

/* dst ^= a >> k, zero-fill. Saturates for k >= W by the same mechanism as shl:
 * the loop is simply empty, and the untouched dst bits are constants. */
void cq_kernel_lshr(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W)
{
    cq_kernel_check_dst(dst, a, b, W);

    int k = cq_shift_amount(b, W);

    for (int i = 0; i + k < W; i++)
        cq_emit_cx(ctx, &a[i + k], &dst[i]);
}

/* dst ^= a >> k, sign-fill. W CX for every k, including k >= W where the first
 * loop is empty and the second fills all W bits from the sign — Bennett's
 * accepted k == W row extended, and the one place D8's saturation costs gates
 * rather than saving them. */
void cq_kernel_ashr(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W)
{
    cq_kernel_check_dst(dst, a, b, W);

    int k = cq_shift_amount(b, W);
    int n = k < W ? W - k : 0;          /* body bits; 0 once saturated */

    for (int i = 0; i < n; i++)
        cq_emit_cx(ctx, &a[i + k], &dst[i]);

    for (int i = n; i < W; i++)
        cq_emit_cx(ctx, &a[W - 1], &dst[i]);
}
