/* tests/support/refmodel_w.c — plan §2.2. The plain-C side of L1: the TWO-WORD
 * half, for the widths a single uint64_t cannot reach (i80, i128). Split from
 * refmodel.c on 2026-09-02 (`bd zmo`) on the seam refmodel.h had documented
 * since the two-word reference landed; every declaration stayed where it was,
 * since the header already described the halves separately. This half calls
 * nothing in the one-word half but cq_ref_die, and the one-word half calls
 * nothing here — that is the whole of why this is a seam and not a size cut. */

#include "refmodel.h"

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

/* --- K9's oracle. See refmodel.h on why it is not the kernel's bias flip. -- */

/* Unsigned, two-word. Both operands arrive already masked to W, so the high
 * word above W is zero in both and the comparison is exact at every width. */
static int w_ult(cq_ref_w a, cq_ref_w b)
{
    return (a.hi != b.hi) ? (a.hi < b.hi) : (a.lo < b.lo);
}

/* Signed, by the SIGN BITS — not by biasing both operands with 2^(W-1) and
 * calling w_ult, which is exactly what arith.jl:465-472 does and therefore
 * exactly what this model must not do. Differing signs decide it outright;
 * equal signs make the unsigned order the signed order. */
static int w_slt(cq_ref_w a, cq_ref_w b, int W)
{
    int sa = cq_ref_w_bit(a, W - 1), sb = cq_ref_w_bit(b, W - 1);

    if (sa != sb) return sa;
    return w_ult(a, b);
}

/* The ten rows of lower_icmp! (arith.jl:409-418), transcribed once. The suite
 * asserts the KERNEL against this; the derivation itself — that `ule` is
 * `¬ult(b,a)` and not `¬ult(a,b)` — is the single most likely thing to be
 * mis-transcribed in K9, which is why the two tables are written independently
 * and crossed rather than shared. */
int cq_ref_icmp(cq_icmp_pred p, cq_ref_w a, cq_ref_w b, int W)
{
    cq_ref_w_bounds(W);

    switch (p) {
    case CQ_ICMP_EQ:  return  cq_ref_w_eq(a, b);
    case CQ_ICMP_NE:  return !cq_ref_w_eq(a, b);
    case CQ_ICMP_ULT: return  w_ult(a, b);
    case CQ_ICMP_UGT: return  w_ult(b, a);
    case CQ_ICMP_ULE: return !w_ult(b, a);
    case CQ_ICMP_UGE: return !w_ult(a, b);
    case CQ_ICMP_SLT: return  w_slt(a, b, W);
    case CQ_ICMP_SGT: return  w_slt(b, a, W);
    case CQ_ICMP_SLE: return !w_slt(b, a, W);
    case CQ_ICMP_SGE: return !w_slt(a, b, W);
    }
    cq_ref_die("unknown icmp predicate", (int)p);
    return 0;
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

/* --- K6, K7 at any width up to 128. --------------------------------------
 *
 * WORD ARITHMETIC, NOT A RIPPLE, AND THAT IS THE POINT. The kernel under test
 * IS a ripple-carry circuit (Bennett `lower_add!`), so a reference written as
 * a bit-serial carry recurrence would share the kernel's own algorithm — and
 * a reference derived from the implementation proves nothing (refmodel.h). One
 * machine add with a carry-out test is a different derivation of the same
 * function, which is what a differential test needs.
 *
 * Two's complement needs no special case: unsigned wraparound is defined in C,
 * so `a - b` is already `a + ~b + 1` mod 2^64 and the borrow is the same
 * comparison the carry is.
 *
 * cq_ref_w_make masks to W afterwards, which is what makes each of these mod
 * 2^W rather than mod 2^128: below 64 the operands cannot overflow the low
 * word at all, at exactly 64 the wrap IS the reduction, and above 64 the carry
 * genuinely crosses the seam. */
cq_ref_w cq_ref_w_add(cq_ref_w a, cq_ref_w b, int W)
{
    uint64_t lo = a.lo + b.lo;
    return cq_ref_w_make(lo, a.hi + b.hi + (uint64_t)(lo < a.lo), W);
}

cq_ref_w cq_ref_w_sub(cq_ref_w a, cq_ref_w b, int W)
{
    uint64_t lo = a.lo - b.lo;
    return cq_ref_w_make(lo, a.hi - b.hi - (uint64_t)(a.lo < b.lo), W);
}

/* --- K11 at any width up to 128. ------------------------------------------
 *
 * SCHOOLBOOK ON 32-BIT LIMBS, WHICH IS A THIRD ALGORITHM AND THAT IS THE POINT.
 * The kernel is bit-serial shift-add with a Cuccaro carry chain; mul.c's own
 * classical fold is column accumulation; this is limb multiplication with an
 * explicit high word. No two of the three share a recurrence, so none of them
 * can share the others' mistakes. tests/test_kernel_mul.c crosses this against
 * a fourth — a bit-serial shift-add over cq_ref_w_add — at widths spanning the
 * 64-bit seam.
 *
 * NO __int128. It is a compiler extension, this project is C11 with no
 * dependencies beyond libc, and the 64x64 -> 128 split below is the standard
 * portable form. `mid` cannot overflow: p00>>32, p01&M and p10&M are each below
 * 2^32, so their sum is below 3·2^32 < 2^64.
 *
 * Only the LOW 128 bits are formed, and cq_ref_w_make then reduces to W — which
 * is right for every width because `mul` is same-width (opcode_table.yaml:186,
 * no widening opcode anywhere in the table) and the high half is never
 * computed by the kernel either. */
static void mul64(uint64_t x, uint64_t y, uint64_t *lo, uint64_t *hi)
{
    const uint64_t M = 0xffffffffu;
    uint64_t x0 = x & M, x1 = x >> 32, y0 = y & M, y1 = y >> 32;
    uint64_t p00 = x0 * y0, p01 = x0 * y1, p10 = x1 * y0, p11 = x1 * y1;
    uint64_t mid = (p00 >> 32) + (p01 & M) + (p10 & M);

    *lo = (mid << 32) | (p00 & M);
    *hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

cq_ref_w cq_ref_w_mul(cq_ref_w a, cq_ref_w b, int W)
{
    uint64_t lo, hi;

    mul64(a.lo, b.lo, &lo, &hi);
    return cq_ref_w_make(lo, hi + a.lo * b.hi + a.hi * b.lo, W);
}

/* --- K12 at any width up to 128. See refmodel.h on the shared recurrence. -- */

/* Restoring division, W iterations at width W. §2.0's derivation is what makes
 * that legal: after k iterations the remainder is below 2^k, so its top bit is
 * always 0 when the shift discards it and the shifted value always fits. That
 * derivation is no longer only a derivation — an exhaustive L1 sweep over every
 * (a,b) including b = 0 reproduces plain C at W <= 5 for both opcodes.
 *
 * `b == 0` needs no branch: `r >= 0` is always true, so every quotient bit is
 * set and every trial subtract is a no-op, which is D3's `2^W - 1` and `a`
 * exactly (divider.jl:15-18, :44-46). */
static void w_divrem(cq_ref_w a, cq_ref_w b, int W, cq_ref_w *q, cq_ref_w *r)
{
    cq_ref_w qq = cq_ref_w_zero(), rr = cq_ref_w_zero();

    cq_ref_w_bounds(W);

    for (int t = 0; t < W; t++) {
        int i = W - 1 - t;

        rr = cq_ref_w_shl(rr, 1, W);
        if (cq_ref_w_bit(a, i)) rr = cq_ref_w_or(rr, cq_ref_w_setbit(0));

        if (!w_ult(rr, b)) {                        /* rr >=u b, i.e. `fits` */
            rr = cq_ref_w_sub(rr, b, W);
            qq = cq_ref_w_or(qq, cq_ref_w_setbit(i));
        }
    }

    *q = qq;
    *r = rr;
}

/* Two's complement negate at width W. `0 - v` rather than `~v + 1` so the
 * masking is cq_ref_w_sub's and lives in one place; the two agree, including at
 * v == typemin, where both give typemin back. */
static cq_ref_w w_neg(cq_ref_w v, int W)
{
    return cq_ref_w_sub(cq_ref_w_zero(), v, W);
}

/* Sign-magnitude, which is aggregate.jl:69-117's shape AND C's truncating
 * division: quotient sign is the XOR of the operand signs, remainder sign
 * follows the dividend. */
static void w_sdivrem(cq_ref_w a, cq_ref_w b, int W, cq_ref_w *q, cq_ref_w *r)
{
    int sa = cq_ref_w_bit(a, W - 1), sb = cq_ref_w_bit(b, W - 1);
    cq_ref_w qq, rr;

    w_divrem(sa ? w_neg(a, W) : a, sb ? w_neg(b, W) : b, W, &qq, &rr);

    *q = (sa ^ sb) ? w_neg(qq, W) : qq;
    *r = sa        ? w_neg(rr, W) : rr;
}

cq_ref_w cq_ref_w_udiv(cq_ref_w a, cq_ref_w b, int W)
{
    cq_ref_w q, r;
    w_divrem(a, b, W, &q, &r);
    return q;
}

cq_ref_w cq_ref_w_urem(cq_ref_w a, cq_ref_w b, int W)
{
    cq_ref_w q, r;
    w_divrem(a, b, W, &q, &r);
    return r;
}

cq_ref_w cq_ref_w_sdiv(cq_ref_w a, cq_ref_w b, int W)
{
    cq_ref_w q, r;
    w_sdivrem(a, b, W, &q, &r);
    return q;
}

cq_ref_w cq_ref_w_srem(cq_ref_w a, cq_ref_w b, int W)
{
    cq_ref_w q, r;
    w_sdivrem(a, b, W, &q, &r);
    return r;
}
