/* tests/support/fpref.c — the IEEE 754 reference model M32's L1 compares
 * against (PRD-v2 §7.16). Read fpref.h first: the load-bearing fact about this
 * file is HOW it was written, not what it computes.
 *
 * Every body below is an ordinary branchy C reading of the standard. Nothing
 * here is shared with `src/kernels/fpround_eval.c`, which is the Julia
 * transcription the library's own R9 rows call, and nothing here is derived
 * from the six-stage ladders, the `grs` packing or the clamp the circuit uses.
 *
 * THE EXPONENTS ARE `int64_t` AND STAY `int64_t`, WHICH IS THE OTHER HALF OF
 * THE INDEPENDENCE. The port holds them as 64 `cq_bit`s and wraps mod 2^64;
 * this model does signed arithmetic in the range the standard's exponents
 * actually occupy and REFUSES anything that would overflow, so a test drawing
 * an absurd exponent gets a refusal here rather than a silent agreement. The
 * caller keeps its draws inside `FPREF_E_LIMIT`.
 */

#include "support/fpref.h"

#include <stdlib.h>

/* Well inside int64_t and far outside every exponent the four helpers meet:
 * `result_exp` lives in about [-1100, 2100] across all five callers, and the
 * anchors reach a few thousand either way. A draw beyond this is out of the
 * model's domain and aborts rather than wrapping quietly. */
#define FPREF_E_LIMIT  ((int64_t)1 << 40)

static void want(int cond)
{
    if (!cond) abort();
}

static void want_e(int64_t e)
{
    want(e > -FPREF_E_LIMIT && e < FPREF_E_LIMIT);
}

/* --- Normalisation: count the places, do not search for them. ------------- */

/* The ladders the port uses move at most 32+16+8+4+2+1 = 63 places in total,
 * so a zero input comes out having "moved" the full 63 and stayed zero. That
 * bound is the loop's, and it is the only thing this model borrows from the
 * port's SHAPE — it is a property of any six-stage binary search and the one
 * place a counting loop would otherwise run forever. */
enum { FPREF_MAX_SHIFT = 63 };

static void normalise_to(uint64_t *v, int64_t *e, int target)
{
    uint64_t hi = UINT64_C(1) << (unsigned)target;
    int moved = 0;

    want_e(*e);
    while (moved < FPREF_MAX_SHIFT && (*v & hi) == UINT64_C(0)) {
        *v <<= 1;
        moved++;
    }
    *e -= moved;
}

void fpref_clz(uint64_t *wr, int64_t *rexp)
{
    /* Domain: the leading 1 is at or below bit 55, or the value is zero. A
     * bit above 55 makes a counting loop and a binary search over masks that
     * stop at 55 genuinely different functions, and neither is the answer. */
    want((*wr >> 56) == UINT64_C(0));
    normalise_to(wr, rexp, 55);
}

void fpref_norm52(uint64_t *m, int64_t *e)
{
    /* softfloat_common.jl:46-48's precondition, restated as this model's
     * domain: no bit above 52. Every caller guarantees it. */
    want((*m >> 53) == UINT64_C(0));
    want_e(*e);

    /* Zero is not normalisable and the exponent is meaningless for it, so the
     * result is the zero and the exponent the caller came in with. */
    if (*m == UINT64_C(0)) return;
    normalise_to(m, e, 52);
}

/* --- Underflow to a subnormal (§7.5). ------------------------------------ */

/* The working value is 56 bits wide — bit 55 down to bit 0 — so a shift of 56
 * or more moves every bit of it out. See fpref.h on why the boundary is drawn
 * at exactly 56 rather than at 57. */
enum { FPREF_WORK_BITS = 56 };

void fpref_subnormal(uint64_t *wr, int64_t *rexp, uint64_t rsign,
                     uint64_t *flushed, int *subnormal, int *ftz)
{
    int64_t places;

    want_e(*rexp);

    /* IEEE's smallest normal has a stored exponent of 1, so a computed
     * exponent at or below 0 is where the result stops being normal. */
    *subnormal = (*rexp <= 0);
    places     = 1 - *rexp;
    *ftz       = (*subnormal && places >= (int64_t)FPREF_WORK_BITS);
    *flushed   = (rsign & UINT64_C(1)) << 63;

    if (!*subnormal) return;

    if (!*ftz) {
        uint64_t keep = *wr, lost = UINT64_C(0);
        int64_t i;

        /* One place at a time, OR-reducing what falls off — the standard's
         * "the discarded bits contribute to the sticky" spelled literally,
         * rather than as a mask, a clamp and a barrel shift. */
        for (i = 0; i < places; i++) {
            lost |= keep & UINT64_C(1);
            keep >>= 1;
        }
        *wr = keep | lost;
    }
    *rexp = 0;
}

/* --- Round to nearest, ties to even (§4.3.1), then pack (§3.4). ----------- */

void fpref_round_and_pack(uint64_t wr, int64_t rexp, uint64_t rsign,
                          uint64_t *normal, uint64_t *overflow,
                          int *exp_ovf, int *exp_ovf_after)
{
    /* The three bits below the retained fraction. `g` is the place worth half
     * an ulp; `r` and `s` together say whether anything remains beyond it. */
    int g = (int)((wr >> 2) & UINT64_C(1));
    int r = (int)((wr >> 1) & UINT64_C(1));
    int s = (int)(wr & UINT64_C(1));
    uint64_t frac = (wr >> 3) & UINT64_C(0x000FFFFFFFFFFFFF);
    uint64_t sign = (rsign & UINT64_C(1)) << 63;
    int64_t  e    = rexp;
    int odd, up;

    want_e(rexp);

    /* §4.3.1: deliver the nearer of the two representable neighbours; if they
     * are equally near, the one whose least significant digit is even. The
     * discarded tail exceeds half an ulp exactly when `g` is set and something
     * is left below it; it is exactly half when `g` is set and nothing is. */
    odd = (int)(frac & UINT64_C(1));
    up  = g && (r || s || odd);

    /* An exponent at or above the all-ones encoding has no normal
     * representation; the caller's select chain replaces the result with the
     * infinity below. Tested both before the rounding step and after it,
     * because the step can carry out of the fraction field. */
    *exp_ovf  = (rexp >= 0x7FF);
    *overflow = sign | UINT64_C(0x7FF0000000000000);

    if (up) {
        frac += 1;
        if (frac == UINT64_C(0x0010000000000000)) {   /* carried out of 52 bits */
            frac = 0;
            e += 1;
        }
    }
    *exp_ovf_after = (e >= 0x7FF);

    /* §3.4's encoding: sign, then the 11-bit biased exponent, then the 52
     * stored fraction bits. The exponent field saturates rather than wraps,
     * which the flags above make unobservable in both directions. */
    if (e < 0)     e = 0;
    if (e > 0x7FE) e = 0x7FE;
    *normal = sign | ((uint64_t)e << 52) | frac;
}
