/* src/kernels/kernel.h — the Rule 7 kernel contract, in one place.
 *
 * Header-only and all static inline, like bit.h: no translation unit, so it is
 * not a module and carries no LOC budget of its own. It exists because the
 * contract PRD §4 states in prose has to be a type somewhere before eleven
 * kernel modules and one test driver can share it.
 *
 *     void kernel(cq_ctx*, cq_bit *dst, const cq_bit *a, const cq_bit *b, int W)
 *     semantics: dst ^= f(a, b)
 *     leaving a and b unchanged and every internal ancilla at |0>.
 *
 * ONE SHAPE, THREE AXES. Forward allocates a fresh all-BIT_ZERO dst
 * (0 ^ f = f); uncompute calls the SAME kernel with dst = out (f ^ f = 0);
 * controlled promotes the gates at the emitter (§9, Rule 9 — no kernel is
 * aware that axis exists). That is why there is no _unc entry point and no
 * _controlled variant anywhere below this header.
 *
 * `W` IS AN int, NOT A uint32_t, and it matches PRD §4 verbatim rather than
 * matching cq_reg_width's return type. Widening it would be a silent ABI edit
 * to the one signature eleven modules and CQ_lang's shim all agree on.
 */
#ifndef CQOPS_KERNELS_KERNEL_H
#define CQOPS_KERNELS_KERNEL_H

#include "bit.h"
#include "ctx.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef void (*cq_kernel_fn)(cq_ctx *ctx, cq_bit *dst,
                             const cq_bit *a, const cq_bit *b, int W);

/* D7a AT THE KERNEL BOUNDARY: `out` may not be one of the sources.
 *
 * HARD ERROR IN BOTH CONFIGURATIONS, matching M07's cq_reg_check_operands one
 * level up, and for M07's reason — risk R2's entire value is firing during the
 * Step 24 fixture run, which Rule 17 pins under Release, where a Debug-gated
 * assert is simply absent. Measured over all 239 CQ_lang goldens, D7a occurs
 * 0 times in 25,147 _unc calls, so nothing legitimate is being refused.
 *
 * IT IS NOT A DUPLICATE OF M07's CHECK, which is the question to ask before
 * adding any guard here. M07 compares HANDLES and can only run where handles
 * exist; a kernel is handed three cq_bit arrays and is entered directly by the
 * test driver, and will be entered by M26 after handles have been resolved
 * away. Delete this line and the case that goes red is a direct kernel call
 * with dst == a — which no test above M07 can otherwise reach. With dst == a
 * and a CLASSICAL lane the fold table folds happily and returns a wrong answer
 * in silence; only with a Q lane would M05's distinctness assert fire, and
 * only in Debug.
 *
 * RANGES, NOT BASE POINTERS, and an earlier draft of this file got that wrong
 * on the strength of a premise this very module contradicts. It argued that
 * "a kernel is always handed whole-register base pointers, never sub-arrays,
 * so partial overlap is unrepresentable". It is not: `cq_scratch_span`
 * (src/scratch.h) exists precisely so a kernel can carve one region into named
 * sub-arrays, and bitwise.h says K9 and K12 will call these kernels that way.
 * MEASURED in both configurations before the fix: one 8-bit all-classical
 * register `r`, then `cq_kernel_xor(ctx, &r[0], &r[2], b, 4)` — `dst != a` as
 * pointers, so the guard passed, and lanes 2-3 of `dst` were lanes 0-1 of `a`.
 * Release returned a wrong answer with no diagnostic; so did Debug, because
 * M05's distinctness check compares qubit INDICES and every bit was constant.
 *
 * The comparison goes through `uintptr_t` because relational comparison of
 * pointers into different objects is undefined in C, while converting to an
 * integer and comparing is merely implementation-defined — the standard
 * portable idiom for an overlap test.
 *
 * D7b — the two SOURCES aliasing each other — IS checked, and the reason is
 * narrower than "it is illegal". At the HANDLE boundary D7b is legal and must
 * not abort: 599 occurrences in the corpus, 10 on v1's integer surface, and a
 * blanket abort there would fail fixtures CQ_lang ships as correct. Its remedy
 * is a defensive `cqrt_copy` at the M26 handle boundary (bd -493), in one
 * place. But that remedy is exactly what guarantees a KERNEL never sees the
 * alias — so a kernel that does see it is looking at a missing copy, and the
 * two things it would otherwise do are both worse. MEASURED: `and(dst,a,a)`
 * with `a` quantum aborts in Debug from M05 with a message naming the fold
 * table rather than the alias; in Release `or(dst,a,a)` returns normally and
 * emits `ccx q0 q0 q2` — a Toffoli whose two controls are one physical qubit —
 * straight to the sink. Right value, malformed circuit, no diagnostic. This
 * turns both into one loud error that names the actual cause. */
static inline int cq_kernel_overlap2(const cq_bit *x, int nx,
                                     const cq_bit *y, int ny)
{
    uintptr_t px = (uintptr_t)x, py = (uintptr_t)y;
    uintptr_t bx = (uintptr_t)((size_t)nx * sizeof(cq_bit));
    uintptr_t by = (uintptr_t)((size_t)ny * sizeof(cq_bit));

    return px < py + by && py < px + bx;
}

static inline void cq_kernel_die(const char *what)
{
    fprintf(stderr, "libcqops: FATAL: kernel: %s\n", what);
    abort();
}

/* THE N-ARY FORM, AND IT EXISTS BECAUSE WIDTHS DIFFER PER OPERAND. A cast is
 * F bits in and T bits out; K10's mux takes a 1-bit condition and two W-bit
 * arms. Sizing every range with one `W` computes the wrong byte extents the
 * moment F != T — it would check `dst` against `a` using dst's length for
 * both — and that matters more than a tidiness argument, because this guard is
 * the structural defence against exactly the aliasing temptation K05.md names:
 * "trunc LOOKS like a pure slice", and reparenting a's low T qubits into dst
 * would break I2 and make the next cqrt_free a double free. A mis-sized guard
 * is the I2 defence with the wrong bounds.
 *
 * The D7b leg is pairwise over the sources and so is VACUOUS FOR A UNARY
 * KERNEL — with one source there is no pair, and it must not fire. */
static inline void cq_kernel_check_n(const cq_bit *dst, int w_dst,
                                     const cq_bit *const *src, const int *w,
                                     int n)
{
    if (w_dst <= 0) cq_kernel_die("destination width is not positive");
    if (n <= 0)     cq_kernel_die("a kernel with no source operand");

    for (int i = 0; i < n; i++) {
        if (w[i] <= 0) cq_kernel_die("source width is not positive");

        if (cq_kernel_overlap2(dst, w_dst, src[i], w[i]))
            cq_kernel_die("D7a — dst overlaps a source; dst ^= f(...) is not "
                          "defined when they share storage");

        for (int j = i + 1; j < n; j++)
            if (cq_kernel_overlap2(src[i], w[i], src[j], w[j]))
                cq_kernel_die("D7b — two sources overlap. This is LEGAL at the "
                              "handle boundary and must not abort there; the "
                              "remedy is M26's defensive cqrt_copy (bd -493). "
                              "Reaching a kernel means that copy is missing");
    }
}

/* The arity-2, one-width case, which is most of the catalogue. */
static inline void cq_kernel_check_dst(const cq_bit *dst, const cq_bit *a,
                                       const cq_bit *b, int W)
{
    const cq_bit *src[2] = { a, b };
    int w[2] = { W, W };

    cq_kernel_check_n(dst, W, src, w, 2);
}

#endif /* CQOPS_KERNELS_KERNEL_H */
