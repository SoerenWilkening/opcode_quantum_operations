/* tests/test_kernel_fconv_anchors.inc — PRD-v2 §7.12's anchors for M37,
 * composed onto the generic providers. Included by test_kernel_fconv.c.
 *
 * WRITTEN BY SUBTRACTION FROM tests/support/fpanchors.c's GENERIC LIST, NOT
 * FROM THE K-DOC's (bd fp-anchors-the-generic-25-already-carry-most-of-them).
 * `cq_fp_anchors_unary` already carries +-0, +-Inf, the default NaN, two
 * payload NaNs, an sNaN, the largest and smallest subnormal, the smallest
 * normal, the largest finite, 1.0 and two negative rows; K19.md §5.4's table
 * and §5.9's list add what is left, and what is left is entirely the
 * SATURATION BOUNDARIES and the source's own branch predicates.
 *
 * THE TWO HALVES OF THE MODULE NEED DIFFERENT PROVIDERS AND THAT IS PRD-v2
 * §5's seam showing through. `fptosi` and `fptoui` take an IEEE bit pattern,
 * so they compose the fp singles; `sitofp` and `uitofp` take an INTEGER, where
 * a NaN payload means nothing and what matters is the CLZ rung, the rounding
 * tie and the sign. K19.md §5.4's own table splits at exactly the same line —
 * its rows 1-28 are fp operands and 29-36 are integer ones.
 *
 * NO `double` ARITHMETIC ANYWHERE BELOW. Every constant is a LITERAL bit
 * pattern, so the table reads the same on every host (fpanchors.h's rule).
 */

/* --- The fp-source extras (fptosi, fptoui). ------------------------------ */

/* Each row names a SATURATION BOUNDARY or one of the two sources' own
 * predicates. The cluster around 2^63 is the point: `fptosi` saturates at
 * `exp >= 1086` and `fptoui`'s [2^63, 2^64) half is `exp == 1086` exactly, so
 * the three doubles either side of 2^63 and the three either side of 2^64 are
 * where a one-off in either predicate becomes visible and nowhere else. */
static const uint64_t FV_FP_EXTRA[] = {
    UINT64_C(0x3FE0000000000000),  /* +0.5   — K19 §5.4 row 12; magnitude 0  */
    UINT64_C(0xBFE0000000000000),  /* -0.5   — row 13 AND row 26, the one
                                    *          input where fptoui's negative
                                    *          reinterpretation gives 0       */
    UINT64_C(0x4000000000000000),  /* +2.0   — a plain in-range integer       */
    UINT64_C(0x4004000000000000),  /* +2.5   — truncation TOWARD ZERO, + side */
    UINT64_C(0xC004000000000000),  /* -2.5   — row 17, the - side             */
    UINT64_C(0x43E0000000000000),  /* +2^63  — rows 6 and 22, the boundary    */
    UINT64_C(0x43DFFFFFFFFFFFFF),  /* prevfloat(2^63) = 2^63-1024, row 8: the
                                    * largest double fptosi does NOT saturate */
    UINT64_C(0xC3E0000000000000),  /* -2^63 exactly — row 7 (idempotent) and
                                    * row 24 (fptoui's invalid)               */
    UINT64_C(0xC3E0000000000001),  /* -(2^63 + 2048), the double BELOW -2^63  */
    UINT64_C(0xC3DFFFFFFFFFFFFF),  /* -(2^63 - 1024), the largest-magnitude
                                    * negative fptosi does NOT saturate       */
    UINT64_C(0x43F0000000000000),  /* +2^64  — row 20, exp = 1087             */
    UINT64_C(0x43EFFFFFFFFFFFFF),  /* prevfloat(2^64) = 2^64-2048, row 23     */
    UINT64_C(0x43E158E460913D00),  /* 1e19   — row 21, the `U31` witness and
                                    * the ONLY anchor that sees fptoui routed
                                    * through fptosi                          */
    UINT64_C(0x4330000000000000),  /* 2^52   — exp == 1075 exactly, where
                                    * BOTH shift arms are the identity        */
    UINT64_C(0x432FFFFFFFFFFFFF),  /* the largest operand taking the RIGHT
                                    * arm, with right_shift == 1              */
    UINT64_C(0x3F30000000000000),  /* 2^-12  — K19 §5.5's D8 band: the only
                                    * input at which a DROPPED :45 clamp
                                    * returns full_mant instead of 0          */
    UINT64_C(0x3F20000000000000),  /* 2^-13  — one decade past it, where the
                                    * clamped amount is 65 rather than 64     */
    UINT64_C(0x8010000000000000)   /* -min normal — the negative side of the
                                    * subnormal boundary                      */
};

enum { N_FV_FP_EXTRA = (int)(sizeof FV_FP_EXTRA / sizeof FV_FP_EXTRA[0]) };

_Static_assert(N_FV_FP_EXTRA == 18,
               "K19.md §5.4's rows and §5.9's 37-40 minus what "
               "cq_fp_anchors_unary already carries; adding or removing one is "
               "a deliberate act");

static int fconv_fp_anchors(int W, int i, cq_ref_w *v)
{
    int base = cq_fp_anchors_unary_count();

    /* f64 only, which is what lets one provider ride a width ladder. */
    if (W != CQ_FP64_W) return 0;
    if (i < 0) return base + N_FV_FP_EXTRA;
    if (i < base) return cq_fp_anchors_unary(W, i, v);
    if (i - base >= N_FV_FP_EXTRA || v == NULL) return 0;

    v[0] = cq_ref_w_make(FV_FP_EXTRA[i - base], 0u, CQ_FP64_W);
    return 1;
}

/* --- The int-source anchors (sitofp, uitofp). ---------------------------- */

/* EVERY POWER OF TWO AT THE SOURCE WIDTH, AND THAT IS THE CLZ LADDER'S OWN
 * COVERAGE RATHER THAN A ROUND NUMBER. K19.md §5.9 anchor 42 asks for six
 * rungs — `clz` in {32,16,8,4,2,1} — because a ladder stage transcribed with
 * the wrong increment `k` or the wrong probe `p` is caught ONLY by its own
 * rung; sweeping every power of two subsumes that and also pins the stage-6
 * asymmetry (K19.md §2.4), since `clz == 63` at `magnitude == 1` is the only
 * value that drives all six stages.
 *
 * AND IT SHRINKS WITH THE SOURCE WIDTH, WHICH IS WHY IT IS COMPUTED RATHER
 * THAN TABULATED: at F = 8 there are eight powers of two and the named rows
 * that do not fit are DROPPED rather than masked. Masking would silently turn
 * `INT64_MAX` into `0xFF` and run the same anchor twice under two names. */
static const struct { uint64_t v; int min_w; } FV_INT_NAMED[] = {
    { UINT64_C(0),                  1  },  /* sitofp.jl:83's only override    */
    { UINT64_C(3),                  2  },  /* an odd magnitude, clz = 62      */
    { UINT64_C(0x0020000000000001), 54 },  /* 2^53+1 — the first inexact
                                            * integer, a tie that rounds DOWN
                                            * because the significand is even */
    { UINT64_C(0x0020000000000003), 54 },  /* 2^53+3 — the same tie the other
                                            * parity, rounds UP               */
    { UINT64_C(0x7FFFFFFFFFFFFFFF), 64 },  /* INT64_MAX — K19 §5.4 row 31,
                                            * where mant_overflow fires and
                                            * the answer rounds UP to 2^63    */
    { UINT64_C(0x8000000000000001), 64 },  /* INT64_MIN + 1                   */
    { UINT64_C(0xFFFFFFFFFFFFFFFE), 64 }   /* -2                              */
};

enum { N_FV_INT_NAMED = (int)(sizeof FV_INT_NAMED / sizeof FV_INT_NAMED[0]) };

static uint64_t fv_ones(int W)
{
    return (W >= 64) ? ~UINT64_C(0)
                     : ((UINT64_C(1) << (unsigned)W) - UINT64_C(1));
}

static int fv_int_named_fit(int W)
{
    int n = 0;

    for (int j = 0; j < N_FV_INT_NAMED; j++)
        if (FV_INT_NAMED[j].min_w <= W) n++;
    return n;
}

/* Layout: `W` powers of two, then the all-ones row (which at F = 64 IS -1 and
 * at every narrower F is the largest unsigned value), then the named rows that
 * fit. */
static int fconv_int_anchors(int W, int i, cq_ref_w *v)
{
    int n_named = fv_int_named_fit(W);

    if (W <= 0 || W > 64) return 0;
    if (i < 0) return W + 1 + n_named;
    if (i >= W + 1 + n_named || v == NULL) return 0;

    if (i < W) {
        v[0] = cq_ref_w_make(UINT64_C(1) << (unsigned)i, 0u, W);
        return 1;
    }
    if (i == W) {
        v[0] = cq_ref_w_make(fv_ones(W), 0u, W);
        return 1;
    }
    for (int j = 0, k = 0; j < N_FV_INT_NAMED; j++) {
        if (FV_INT_NAMED[j].min_w > W) continue;
        if (k++ == i - W - 1) {
            v[0] = cq_ref_w_make(FV_INT_NAMED[j].v, 0u, W);
            return 1;
        }
    }
    return 0;
}
