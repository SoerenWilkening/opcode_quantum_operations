/* tests/support/refmodel.c — plan §2.2. The plain-C side of L1: the ONE-WORD
 * half. The two-word half (cq_ref_w — everything an i80 or i128 kernel needs)
 * is tests/support/refmodel_w.c, split off 2026-09-02 (`bd zmo`: this file had
 * reached 284 of Rule 12's 300 with no seam recorded, one line at a time). The
 * only thing the halves share is the refusal below, which is why it is the one
 * non-static helper in either file. */

#include "refmodel.h"

#include <stdio.h>
#include <stdlib.h>

void cq_ref_die(const char *what, int W)
{
    fprintf(stderr, "libcqops: FATAL: refmodel: %s (W = %d)\n", what, W);
    abort();
}

uint64_t cq_ref_mask(int W)
{
    /* W == 64 is the case that makes the obvious one-liner undefined:
     * 1ULL << 64 is UB, and on x86 the shift count wraps to 0 so the
     * expression quietly yields 0 rather than all-ones — a reference model
     * that says every 64-bit result is zero and agrees with nothing. */
    if (W <= 0 || W > 64) cq_ref_die("width out of range for a 64-bit reference", W);
    return W == 64 ? ~(uint64_t)0 : ((uint64_t)1 << W) - 1u;
}

uint64_t cq_ref_trunc(uint64_t v, int W)
{
    return v & cq_ref_mask(W);
}

int64_t cq_ref_sext(uint64_t v, int W)
{
    uint64_t m = cq_ref_mask(W);
    uint64_t t = v & m;
    uint64_t sign = (uint64_t)1 << (W - 1);

    /* Sign extension by the shift-free identity (t ^ sign) - sign, so there is
     * no implementation-defined right shift of a negative and no branch on the
     * sign bit. At W == 64, sign is the top bit and the arithmetic is exact. */
    return (int64_t)((t ^ sign) - sign);
}

uint64_t cq_ref_xor(uint64_t a, uint64_t b, int W)
{
    return (a ^ b) & cq_ref_mask(W);
}

uint64_t cq_ref_and(uint64_t a, uint64_t b, int W)
{
    return (a & b) & cq_ref_mask(W);
}

uint64_t cq_ref_or(uint64_t a, uint64_t b, int W)
{
    return (a | b) & cq_ref_mask(W);
}

/* K6 and K7 HAVE NO ONE-WORD REFERENCE, deliberately — see cq_ref_w_add /
 * cq_ref_w_sub at the foot of this file, and refmodel.h on why.
 *
 * A `(a + b) & cq_ref_mask(W)` pair was written here at Step 12 and REMOVED
 * the same day, because the cross-check it existed for could not fail. Below
 * 64 `lo_mask(W)` and `cq_ref_mask(W)` are provably the same function and
 * `hi_mask(W)` is 0, so `cq_ref_w_add(A,B,W).lo == cq_ref_add(a,b,W)` reduces
 * to `(a+b) & m == (a+b) & m` and `.hi == 0` holds whatever the carry does.
 * Two models sharing an idiom cross-check nothing; the wide pair is now
 * crossed against a BIT-SERIAL ripple at widths that actually span the 64-bit
 * seam (tests/test_kernel_add.c). Do not re-add these. */

/* --- K4. `k` arrives ALREADY REDUCED by D8; these are only sat_shift. ----- */

uint64_t cq_ref_shl(uint64_t a, int k, int W)
{
    if (k < 0) cq_ref_die("negative shift amount", W);
    if (k >= W) return 0u;                       /* saturate */
    return (a << k) & cq_ref_mask(W);
}

uint64_t cq_ref_lshr(uint64_t a, int k, int W)
{
    if (k < 0) cq_ref_die("negative shift amount", W);
    if (k >= W) return 0u;                       /* saturate */
    return (a & cq_ref_mask(W)) >> k;
}

uint64_t cq_ref_ashr(uint64_t a, int k, int W)
{
    uint64_t m = cq_ref_mask(W);
    uint64_t sign = (a & m) >> (W - 1);
    uint64_t fill = sign ? m : 0u;               /* all-sign, masked to W */

    if (k < 0) cq_ref_die("negative shift amount", W);
    if (k >= W) return fill;                     /* saturate to all-sign */

    /* Built from a logical shift plus an explicit fill rather than from C's
     * >> on a signed value, whose behaviour for negatives is
     * implementation-defined. A reference model may not rest on that. */
    return (((a & m) >> k) | (k ? (fill << (W - k)) : 0u)) & m;
}
