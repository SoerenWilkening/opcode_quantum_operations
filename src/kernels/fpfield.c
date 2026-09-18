/* src/kernels/fpfield.c — M31, K22, the VIEWS half. PRD-v2 §3.1, §5, §7.
 *
 * Read docs/constructions/K22.md and the header before changing anything here.
 * There is no construction in this file: every function is addressing, and the
 * only thing it can get wrong is a lane index. That is also the only thing it
 * CAN get wrong invisibly — a view off by one reads a neighbouring field's bit
 * as a control and computes a plausible answer — so the guards below are hard
 * errors in BOTH configurations rather than Debug asserts.
 */

#include "kernels/fpfield.h"

#include "kernels/kernel.h"

void cq_fp_view(const cq_bit *a, int lo, int n, cq_bit *out)
{
    if (lo < 0 || n <= 0 || lo > CQ_FP64_W - n)
        cq_kernel_die("fpfield: view span is not inside [0, 64)");

    for (int i = 0; i < n; i++) out[i] = a[lo + i];
    for (int i = n; i < CQ_FP64_W; i++) out[i] = cq_bit_zero();
}

/* fadd.jl:21 `ea = (a >> 52) & UInt64(0x7FF)`. The shift takes lanes 52..63
 * and the mask drops lane 63 (the sign), so the surviving field is lanes
 * 52..62 at positions 0..10 — which is exactly this call. Composing the two
 * upstream operators into one addressing step is K22.md §5's recorded delta. */
void cq_fp_view_exp(const cq_bit *a, cq_bit *out)
{
    cq_fp_view(a, CQ_FP64_EXP_LO, CQ_FP64_EXP_W, out);
}

/* fadd.jl:22 `fa = a & FRAC_MASK`. */
void cq_fp_view_frac(const cq_bit *a, cq_bit *out)
{
    cq_fp_view(a, CQ_FP64_FRAC_LO, CQ_FP64_FRAC_W, out);
}

/* fadd.jl:20 `sa = a >> 63`. */
void cq_fp_view_sign(const cq_bit *a, cq_bit *out)
{
    cq_fp_view(a, CQ_FP64_SIGN_LO, 1, out);
}

const cq_bit *cq_fp_span_exp (const cq_bit *a) { return &a[CQ_FP64_EXP_LO];  }
const cq_bit *cq_fp_span_frac(const cq_bit *a) { return &a[CQ_FP64_FRAC_LO]; }
const cq_bit *cq_fp_span_sign(const cq_bit *a) { return &a[CQ_FP64_SIGN_LO]; }

void cq_fp_const(cq_bit *out, uint64_t pattern)
{
    for (int i = 0; i < CQ_FP64_W; i++)
        out[i] = cq_bit_const((int)((pattern >> i) & UINT64_C(1)));
}

uint64_t cq_fp_pack(const cq_bit *a)
{
    uint64_t v = UINT64_C(0);

    for (int i = 0; i < CQ_FP64_W; i++) {
        if (!cq_bit_is_const(a[i]))
            cq_kernel_die("fpfield: pack of a lane that is a qubit — the R9 "
                          "short-circuit runs only on an all-classical rail");
        v |= (uint64_t)cq_bit_value(a[i]) << i;
    }
    return v;
}
