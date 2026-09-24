/* tests/support/fmiscref.c — see fmiscref.h. Nothing here reads src/. */

#include "support/fmiscref.h"

#include "support/fphost.h"

#define REF_SIGN  UINT64_C(0x8000000000000000)

/* NaN by the standard's definition: every exponent bit set and a non-zero
 * significand field. Spelled with the two masks rather than a shift so the
 * test side shares no expression with M31's `(a >> 52) & 0x7FF` view. */
static int ref_is_nan(uint64_t u)
{
    return (u & UINT64_C(0x7FF0000000000000)) == UINT64_C(0x7FF0000000000000)
        && (u & UINT64_C(0x000FFFFFFFFFFFFF)) != 0u;
}

static int ref_is_zero(uint64_t u) { return (u & ~REF_SIGN) == 0u; }

/* `want_min` selects min; the two differ only in the ±0 row and in which way
 * the host comparison points. */
static uint64_t ref_minmax(uint64_t a, uint64_t b, int want_min)
{
    int an = ref_is_nan(a), bn = ref_is_nan(b);

    if (an && bn) return CQ_REF_FMINMAX_CANON_NAN;
    if (an)       return b;
    if (bn)       return a;

    if (ref_is_zero(a) && ref_is_zero(b)) {
        int any_neg = ((a | b) & REF_SIGN) != 0u;
        int all_neg = ((a & b) & REF_SIGN) != 0u;

        if (want_min) return any_neg ? REF_SIGN : UINT64_C(0);
        return all_neg ? REF_SIGN : UINT64_C(0);
    }

    {
        double x = cq_fphost_from_bits(a), y = cq_fphost_from_bits(b);

        /* Neither is a NaN and they are not both zero, so `x == y` means the
         * SAME bit pattern and either operand is the answer. */
        if (want_min) return (y < x) ? b : a;
        return (y > x) ? b : a;
    }
}

uint64_t cq_ref_fmin(uint64_t a, uint64_t b) { return ref_minmax(a, b, 1); }
uint64_t cq_ref_fmax(uint64_t a, uint64_t b) { return ref_minmax(a, b, 0); }

uint64_t cq_ref_fneg(uint64_t a) { return a ^ REF_SIGN; }
uint64_t cq_ref_fabs(uint64_t a) { return a & ~REF_SIGN; }

uint64_t cq_ref_copysign(uint64_t x, uint64_t y)
{
    return (x & ~REF_SIGN) | (y & REF_SIGN);
}
