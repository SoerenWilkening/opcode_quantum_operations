/* tests/support/refmodel.c — plan §2.2. The plain-C side of L1. */

#include "refmodel.h"

#include <stdio.h>
#include <stdlib.h>

static void cq_ref_die(const char *what, int W)
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

/* --- The two-word half. ------------------------------------------------- */

static void cq_ref_w_bounds(int W)
{
    if (W <= 0 || W > 128) cq_ref_die("width out of range for a 128-bit reference", W);
}

/* The mask for the HIGH word: all ones for W == 128, empty for W <= 64. */
static uint64_t hi_mask(int W)
{
    if (W <= 64) return 0u;
    return W == 128 ? ~(uint64_t)0 : ((uint64_t)1 << (W - 64)) - 1u;
}

static uint64_t lo_mask(int W)
{
    return W >= 64 ? ~(uint64_t)0 : ((uint64_t)1 << W) - 1u;
}

cq_ref_w cq_ref_w_make(uint64_t lo, uint64_t hi, int W)
{
    cq_ref_w v;
    cq_ref_w_bounds(W);
    v.lo = lo & lo_mask(W);
    v.hi = hi & hi_mask(W);
    return v;
}

cq_ref_w cq_ref_w_zero(void)          { cq_ref_w v = { 0u, 0u }; return v; }
int cq_ref_w_eq(cq_ref_w a, cq_ref_w b) { return a.lo == b.lo && a.hi == b.hi; }

int cq_ref_w_bit(cq_ref_w v, int i)
{
    if (i < 0 || i >= 128) cq_ref_die("bit index out of range", i);
    return i < 64 ? (int)((v.lo >> i) & 1u) : (int)((v.hi >> (i - 64)) & 1u);
}

static void set_bit(cq_ref_w *v, int i, int one)
{
    if (!one) return;
    if (i < 64) v->lo |= (uint64_t)1 << i;
    else        v->hi |= (uint64_t)1 << (i - 64);
}

/* K4 at any width. EXPLICIT BIT LOOPS, for the reason the casts below give:
 * a shifted-word reference needs its own 128-bit shift, which is the thing
 * most likely to be wrong in the same direction as the kernel under test.
 * Saturation falls out — for k >= W every source index is out of range, so
 * shl/lshr write nothing and ashr writes the sign everywhere. */
cq_ref_w cq_ref_w_shl(cq_ref_w a, int k, int W)
{
    cq_ref_w r = cq_ref_w_zero();
    cq_ref_w_bounds(W);
    if (k < 0) cq_ref_die("negative shift amount", W);

    for (int i = k; i < W; i++) set_bit(&r, i, cq_ref_w_bit(a, i - k));
    return r;
}

cq_ref_w cq_ref_w_lshr(cq_ref_w a, int k, int W)
{
    cq_ref_w r = cq_ref_w_zero();
    cq_ref_w_bounds(W);
    if (k < 0) cq_ref_die("negative shift amount", W);

    for (int i = 0; i + k < W; i++) set_bit(&r, i, cq_ref_w_bit(a, i + k));
    return r;
}

cq_ref_w cq_ref_w_ashr(cq_ref_w a, int k, int W)
{
    cq_ref_w r = cq_ref_w_zero();
    int sign;
    cq_ref_w_bounds(W);
    if (k < 0) cq_ref_die("negative shift amount", W);

    sign = cq_ref_w_bit(a, W - 1);
    for (int i = 0; i < W; i++)
        set_bit(&r, i, (i + k < W) ? cq_ref_w_bit(a, i + k) : sign);
    return r;
}

/* All three casts are written as EXPLICIT BIT LOOPS rather than as shifts of a
 * 128-bit value. It is slower and it is the point: a shifted-word reference
 * would need its own 128-bit shift, which is the thing most likely to be wrong
 * in the same direction as the kernel under test. A per-bit copy shares no
 * arithmetic with the circuit at all. */
cq_ref_w cq_ref_w_zext(cq_ref_w a, int F, int T)
{
    cq_ref_w r = cq_ref_w_zero();
    cq_ref_w_bounds(F); cq_ref_w_bounds(T);
    if (T < F) cq_ref_die("zext narrows", T);

    for (int i = 0; i < F; i++) set_bit(&r, i, cq_ref_w_bit(a, i));
    return r;                       /* bits F..T-1 stay 0 */
}

cq_ref_w cq_ref_w_sext(cq_ref_w a, int F, int T)
{
    cq_ref_w r = cq_ref_w_zero();
    int sign;
    cq_ref_w_bounds(F); cq_ref_w_bounds(T);
    if (T < F) cq_ref_die("sext narrows", T);

    sign = cq_ref_w_bit(a, F - 1);
    for (int i = 0; i < F; i++) set_bit(&r, i, cq_ref_w_bit(a, i));
    for (int i = F; i < T; i++) set_bit(&r, i, sign);
    return r;
}

cq_ref_w cq_ref_w_trunc(cq_ref_w a, int F, int T)
{
    cq_ref_w r = cq_ref_w_zero();
    cq_ref_w_bounds(F); cq_ref_w_bounds(T);
    if (T > F) cq_ref_die("trunc widens", T);

    for (int i = 0; i < T; i++) set_bit(&r, i, cq_ref_w_bit(a, i));
    return r;
}

/* --- Mask algebra. See refmodel.h on why masks are two-word at every W. --- */

cq_ref_w cq_ref_w_ones(int W)
{
    cq_ref_w v;
    cq_ref_w_bounds(W);
    v.lo = lo_mask(W);
    v.hi = hi_mask(W);
    return v;
}

cq_ref_w cq_ref_w_setbit(int i)
{
    cq_ref_w v = cq_ref_w_zero();
    if (i < 0 || i >= 128) cq_ref_die("bit index out of range", i);
    set_bit(&v, i, 1);
    return v;
}

cq_ref_w cq_ref_w_and(cq_ref_w a, cq_ref_w b)
{
    cq_ref_w v; v.lo = a.lo & b.lo; v.hi = a.hi & b.hi; return v;
}

cq_ref_w cq_ref_w_or(cq_ref_w a, cq_ref_w b)
{
    cq_ref_w v; v.lo = a.lo | b.lo; v.hi = a.hi | b.hi; return v;
}

cq_ref_w cq_ref_w_andnot(cq_ref_w a, cq_ref_w b)
{
    cq_ref_w v; v.lo = a.lo & ~b.lo; v.hi = a.hi & ~b.hi; return v;
}

int cq_ref_w_is_zero(cq_ref_w a) { return a.lo == 0u && a.hi == 0u; }

uint32_t cq_ref_w_popcount(cq_ref_w a)
{
    uint32_t n = 0;
    for (int i = 0; i < 128; i++) if (cq_ref_w_bit(a, i)) n++;
    return n;
}
