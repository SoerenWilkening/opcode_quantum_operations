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

#include <string.h>

/* cq_fp_view_fill starts from an all-zero array, which is CQ_BIT_ZERO with
 * q == 0 lane for lane — what cq_bit_zero() returns — only because the kind
 * numbering says so. */
_Static_assert(CQ_BIT_ZERO == 0,
               "an all-zero cq_bit must be CQ_BIT_ZERO with q == 0");

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

/* Lanes i with 0 <= i + shift < 64, as a lane-selection word. */
static uint64_t in_range(int shift)
{
    if (shift >= CQ_FP64_W || shift <= -CQ_FP64_W) return UINT64_C(0);
    if (shift >= 0) return ~UINT64_C(0) >> (unsigned)shift;
    return ~UINT64_C(0) << (unsigned)(-shift);
}

/* The index of the lowest set bit of a NON-ZERO word, in six halvings rather
 * than one test per lane (no compiler builtin: libc and C11 only). */
static int lowest_set(uint64_t v)
{
    int n = 0;

    if ((v & UINT64_C(0xFFFFFFFF)) == 0u) { n += 32; v >>= 32; }
    if ((v & UINT64_C(0xFFFF)) == 0u)     { n += 16; v >>= 16; }
    if ((v & UINT64_C(0xFF)) == 0u)       { n += 8;  v >>= 8;  }
    if ((v & UINT64_C(0xF)) == 0u)        { n += 4;  v >>= 4;  }
    if ((v & UINT64_C(0x3)) == 0u)        { n += 2;  v >>= 2;  }
    if ((v & UINT64_C(0x1)) == 0u)        { n += 1;            }
    return n;
}

/* One run of kept lanes per pass: [lo, lo + len) is the lowest run, `len`
 * being the first CLEAR bit of `keep >> lo` (64 - lo when the run reaches lane
 * 63, every bit of `keep >> lo` above it being a shifted-in zero). */
void cq_fp_view_fill(const cq_bit *base, int shift, uint64_t mask, cq_bit *out)
{
    uint64_t keep = mask & in_range(shift);

    memset(out, 0, sizeof *out * (size_t)CQ_FP64_W);
    while (keep != 0u) {
        int lo = lowest_set(keep);
        uint64_t gap = ~(keep >> (unsigned)lo);
        int len = (gap == 0u) ? CQ_FP64_W - lo : lowest_set(gap);

        memcpy(&out[lo], &base[lo + shift], sizeof *out * (size_t)len);
        keep = (lo + len >= CQ_FP64_W) ? UINT64_C(0)
             : keep & (~UINT64_C(0) << (unsigned)(lo + len));
    }
}

/* 64 CQ_BIT_ONE lanes, immutable: a constant span is a VIEW over this masked
 * by its own bit pattern, so cq_fp_const is cq_fp_view_fill — the same 64
 * bits the per-lane cq_bit_const form wrote, without 64 struct assignments
 * (under Debug's ASan each one an intercepted memcpy; a step machine fills a
 * constant span per operand per GATE). Measured 2026-10-05 in a scratch
 * micro-benchmark: Release on par with the per-lane form, Debug ~10x faster. */
#define FP_ONE  { CQ_BIT_ONE, 0u }
#define FP_ONE8 FP_ONE, FP_ONE, FP_ONE, FP_ONE, FP_ONE, FP_ONE, FP_ONE, FP_ONE
static const cq_bit FP_ONES[] = {
    FP_ONE8, FP_ONE8, FP_ONE8, FP_ONE8, FP_ONE8, FP_ONE8, FP_ONE8, FP_ONE8
};
_Static_assert(sizeof FP_ONES / sizeof FP_ONES[0] == CQ_FP64_W,
               "the all-ONE span is one lane per binary64 bit");

void cq_fp_const(cq_bit *out, uint64_t pattern)
{
    cq_fp_view_fill(FP_ONES, 0, pattern, out);
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
