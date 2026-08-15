/* src/kernels/cast.c — M13, Step 11. K5 sext / zext / trunc. See cast.h. */

#include "kernels/cast.h"

#include "emit.h"
#include "kernels/kernel.h"

/* The width guard is a HARD ERROR IN BOTH CONFIGURATIONS, following
 * kernel.h's D7a/D7b precedent and for its reason: Step 24 runs under Release,
 * where a Debug-gated assert is simply absent. What it prevents is not a
 * wrong answer but a memory error — a zext with T < F would write dst[i] for
 * i up to F into a T-slot array, and a trunc with T > F would read a[i] up to
 * T from an F-slot array. Julia raises BoundsError there; C does not.
 *
 * T == F is ACCEPTED as an identity copy rather than refused. The construction
 * handles it with no special case (sext's broadcast loop is zero-trip, zext
 * and trunc degenerate to a plain F-CX copy), and both readings are defensible
 * — so this line is the decision, recorded because K05.md picked it in passing
 * without flagging it as one. It is unreachable from CQ_lang's shipped grid:
 * sext and zext are strictly widening there and trunc strictly narrowing. */
static void check_widths(const cq_bit *dst, const cq_bit *a, int F, int T,
                         int widening)
{
    const cq_bit *src[1] = { a };
    int w[1] = { F };

    if (widening ? (T < F) : (T > F))
        cq_kernel_die("cast: width pair is inverted — a widening cast needs "
                      "T >= F and a narrowing cast needs T <= F; the other way "
                      "round runs off the end of a bits array");

    cq_kernel_check_n(dst, T, src, w, 1);
}

void cq_kernel_zext(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T)
{
    check_widths(dst, a, F, T, 1);

    for (int i = 0; i < F; i++)
        cq_emit_cx(ctx, &a[i], &dst[i]);

    /* dst[F..T-1] deliberately get NO STEP. They are constants and stay
     * constants: zero gates and zero qubits, permanently. Bennett must
     * allocate all T wires and pin the high ones at |0>. */
}

void cq_kernel_sext(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T)
{
    check_widths(dst, a, F, T, 1);

    for (int i = 0; i < F; i++)
        cq_emit_cx(ctx, &a[i], &dst[i]);

    for (int i = F; i < T; i++)
        cq_emit_cx(ctx, &a[F - 1], &dst[i]);
}

void cq_kernel_trunc(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T)
{
    check_widths(dst, a, F, T, 0);

    for (int i = 0; i < T; i++)
        cq_emit_cx(ctx, &a[i], &dst[i]);

    /* a[T..F-1] are never read and never written. They belong to the caller's
     * still-live register — see cast.h on why K5 must not free them. */
}
