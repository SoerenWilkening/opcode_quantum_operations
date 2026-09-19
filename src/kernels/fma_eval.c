/* src/kernels/fma_eval.c — M39, K20. RISK R9's CLASSICAL ROW: `soft_fma`'s
 * body transcribed into C over `uint64_t`, composing M31's class predicate and
 * M32's three `*_eval` helpers. It emits nothing, allocates nothing and reads
 * no `cq_bit`.
 *
 * M32's `_eval` <-> THE CIRCUIT seam (K23 D-K23-9), taken here for the same
 * reason: the Julia is transcribed twice, once into a row table and once into
 * C, and putting the two in one file invites the second to be written from the
 * first.
 *
 * THE 128-BIT INTERMEDIATE IS A `(hi, lo)` PAIR OF `uint64_t`, NEVER
 * `unsigned __int128`. Two reasons and both are binding. `__int128` is not
 * C11, and PRD §14's "nothing beyond libc" has no room for a compiler
 * extension in a kernel's own body; and upstream declined native `UInt128` for
 * exactly this routine, on the record — softfloat_common.jl:235-252 says the
 * hand-rolled hi/lo pair is "the direct ancestor of the gate sequence soft_fma
 * emits". A `__int128` transcription would be a DIFFERENT program from the one
 * the row table runs, which is the one thing this file may not be.
 *
 * THE HOST `fma()` IS REFUSED, AND SO IS EVERY HOST `double` OPERATOR
 * (PRD-v2 §7.4). Four of its five IEEE-unspecified cells are reachable from
 * `fma`: `Inf - Inf` through the `inf_clash` arm's INDEF, `0 * Inf` through
 * `inf_times_zero`, and the NaN-payload cells through `_sf_propagate_nan3`'s
 * three-operand a > b > c precedence. Every one is x86's choice and arm64
 * differs on all of them, so a host short-circuit would give a program whose
 * classical mode and quantum mode return different bits on the same input, on
 * some hosts only. This way they agree bit-for-bit everywhere. `fma()` is
 * additionally refused because it is the ONE host operator that may be
 * contracted, fused or emulated differently per target (`-ffp-contract` moves
 * it), which is the same host-dependence in a third dress.
 *
 * THIS IS NOT L1's ORACLE, AND USING IT AS ONE IS THE STEP 18 TRAP. An oracle
 * that shares code with the implementation is blind to exactly what that code
 * gets wrong. L1's reference is the host operator with §7.4's cells pinned by
 * table (tests/support/fphost.h) — and for a three-operand rounding it is the
 * host `fma()` only where IEEE specifies the result.
 */

#include "kernels/fma.h"

#include "kernels/fpclass.h"
#include "kernels/fpround.h"

/* --- The eight inlined 128-bit helpers, softfloat_common.jl. ------------- */

typedef struct { uint64_t hi, lo; } fa128;

/* `clamp(x, lo, hi)` — the one construction Rule 1 does not cover, since
 * `Base.clamp`'s body is not in the snapshot (K23 §5.5). M32's reading,
 * followed rather than re-derived: `ifelse(x < lo, lo, ifelse(x > hi, hi, x))`.
 * SIGNED, because every caller here clamps an `Int64` that is genuinely
 * negative on the unselected branches (PRD-v2 §7.6). */
static int64_t fa_clamp(int64_t x, int64_t lo, int64_t hi)
{
    return (x < lo) ? lo : ((x > hi) ? hi : x);
}

static fa128 fa_widemul(uint64_t a, uint64_t b)          /* c:265-292 */
{
    const uint64_t a_lo = a & UINT64_C(0xFFFFFFFF), a_hi = a >> 32;
    const uint64_t b_lo = b & UINT64_C(0xFFFFFFFF), b_hi = b >> 32;
    const uint64_t pp_ll = a_lo * b_lo, pp_lh = a_lo * b_hi;
    const uint64_t pp_hl = a_hi * b_lo, pp_hh = a_hi * b_hi;
    const uint64_t mid = pp_lh + pp_hl;
    const uint64_t mid_carry = (mid < pp_lh) ? UINT64_C(1) : UINT64_C(0);
    const uint64_t mid_lo = mid << 32;
    const uint64_t mid_hi = (mid >> 32) | (mid_carry << 32);
    const uint64_t lo = pp_ll + mid_lo;
    const uint64_t lo_carry = (lo < pp_ll) ? UINT64_C(1) : UINT64_C(0);
    fa128 r;

    r.hi = pp_hh + mid_hi + lo_carry;
    r.lo = lo;
    return r;
}

static fa128 fa_add128(fa128 a, fa128 b)                 /* c:299-304 */
{
    fa128 r;

    r.lo = a.lo + b.lo;
    r.hi = a.hi + b.hi + ((r.lo < a.lo) ? UINT64_C(1) : UINT64_C(0));
    return r;
}

static fa128 fa_sub128(fa128 a, fa128 b)                 /* c:311-316 */
{
    fa128 r;

    r.lo = a.lo - b.lo;
    r.hi = a.hi - b.hi - ((a.lo < b.lo) ? UINT64_C(1) : UINT64_C(0));
    return r;
}

static fa128 fa_neg128(fa128 a)                          /* c:324-329 */
{
    fa128 r;

    r.lo = (~a.lo) + UINT64_C(1);
    r.hi = (~a.hi) + ((r.lo == UINT64_C(0)) ? UINT64_C(1) : UINT64_C(0));
    return r;
}

static fa128 fa_shl128_by1(fa128 a)                      /* c:336-340 */
{
    fa128 r;

    r.hi = (a.hi << 1) | (a.lo >> 63);
    r.lo = a.lo << 1;
    return r;
}

static fa128 fa_shr128jam_by1(fa128 a)                   /* c:348-353 */
{
    const uint64_t sticky = a.lo & UINT64_C(1);
    fa128 r;

    r.hi = a.hi >> 1;
    r.lo = (a.lo >> 1) | (a.hi << 63) | sticky;
    return r;
}

/* c:373-412. Every amount is CLAMPED before it is used, so no shift here is
 * ever by 64 or more and C's undefined behaviour is unreachable — which is
 * also why PRD-v2 §7.6 audits all six of this body's variable shifts as
 * CLAMPED and none as DISCARDED. */
static fa128 fa_srj128(fa128 a, int64_t dist)
{
    const int nonpos = dist <= INT64_C(0);
    const uint64_t dA = (uint64_t)fa_clamp(dist, INT64_C(1), INT64_C(63));
    const uint64_t lostA = (UINT64_C(1) << dA) - UINT64_C(1);
    const uint64_t stickyA = ((a.lo & lostA) != UINT64_C(0))
                           ? UINT64_C(1) : UINT64_C(0);
    const uint64_t hiA = a.hi >> dA;
    const uint64_t loA = (a.hi << (UINT64_C(64) - dA)) | (a.lo >> dA) | stickyA;
    const uint64_t dB = (uint64_t)(fa_clamp(dist, INT64_C(64), INT64_C(127))
                                   - INT64_C(64));
    const uint64_t lostB = (UINT64_C(1) << dB) - UINT64_C(1);
    const uint64_t stickyB = ((a.lo != UINT64_C(0))
                              || ((a.hi & lostB) != UINT64_C(0)))
                           ? UINT64_C(1) : UINT64_C(0);
    const uint64_t hiB = UINT64_C(0);
    const uint64_t loB = (a.hi >> dB) | stickyB;
    const uint64_t hiC = UINT64_C(0);
    const uint64_t loC = ((a.hi | a.lo) != UINT64_C(0))
                       ? UINT64_C(1) : UINT64_C(0);
    const int big = dist >= INT64_C(128), mid = dist >= INT64_C(64);
    fa128 r;

    r.hi = big ? hiC : (mid ? hiB : hiA);
    r.lo = big ? loC : (mid ? loB : loA);
    if (nonpos) { r.hi = a.hi; r.lo = a.lo; }
    return r;
}

/* c:433-465, the six-stage 128-bit CLZ. The probe masks are spelled as the
 * source spells them so a reader checks a mask against the line rather than
 * against arithmetic. */
static fa128 fa_clz128(fa128 v, int64_t *e)
{
#define FA_CLZ_STAGE(MK, N, D)                                                 \
    do {                                                                       \
        const int need = (v.hi & (MK)) == UINT64_C(0);                         \
        const uint64_t nh = (v.hi << (N)) | (v.lo >> (64 - (N)));              \
        const uint64_t nl = v.lo << (N);                                       \
        if (need) { v.hi = nh; v.lo = nl; *e -= (D); }                         \
    } while (0)

    FA_CLZ_STAGE(UINT64_C(0xFFFFFFFF) << 30, 32, INT64_C(32));
    FA_CLZ_STAGE(UINT64_C(0xFFFF) << 46,     16, INT64_C(16));
    FA_CLZ_STAGE(UINT64_C(0xFF) << 54,        8, INT64_C(8));
    FA_CLZ_STAGE(UINT64_C(0xF) << 58,         4, INT64_C(4));
    FA_CLZ_STAGE(UINT64_C(0x3) << 60,         2, INT64_C(2));
    FA_CLZ_STAGE(UINT64_C(1) << 61,           1, INT64_C(1));
#undef FA_CLZ_STAGE
    return v;
}

/* --- `soft_fma` — fma.jl:28-211. ----------------------------------------- */

uint64_t cq_fma_eval(uint64_t a, uint64_t b, uint64_t c)
{
    const uint64_t sa = a >> 63, ea = (a >> 52) & CQ_FP64_EXP_ALL;   /* :32-33 */
    const uint64_t fa = a & CQ_FP64_FRAC_MASK;                       /* :34  */
    const uint64_t sb = b >> 63, eb = (b >> 52) & CQ_FP64_EXP_ALL;   /* :35-36 */
    const uint64_t fb = b & CQ_FP64_FRAC_MASK;                       /* :37  */
    const uint64_t sc = c >> 63, ec = (c >> 52) & CQ_FP64_EXP_ALL;   /* :38-39 */
    const uint64_t fc = c & CQ_FP64_FRAC_MASK;                       /* :40  */

    /* THE NINE PREDICATES ARE M31's, HERE TOO — `cq_fp_class_eval` is the same
     * four rows the circuit runs (D-K23-9's shape), so the classical row and
     * the CLASS block cannot drift apart. */
    const int a_nan = cq_fp_class_eval(a, CQ_FP_IS_NAN);             /* :43  */
    const int b_nan = cq_fp_class_eval(b, CQ_FP_IS_NAN);             /* :44  */
    const int c_nan = cq_fp_class_eval(c, CQ_FP_IS_NAN);             /* :45  */
    const int a_inf = cq_fp_class_eval(a, CQ_FP_IS_INF);             /* :46  */
    const int b_inf = cq_fp_class_eval(b, CQ_FP_IS_INF);             /* :47  */
    const int c_inf = cq_fp_class_eval(c, CQ_FP_IS_INF);             /* :48  */
    const int a_zero = cq_fp_class_eval(a, CQ_FP_IS_ZERO);           /* :49  */
    const int b_zero = cq_fp_class_eval(b, CQ_FP_IS_ZERO);           /* :50  */
    const int c_zero = cq_fp_class_eval(c, CQ_FP_IS_ZERO);           /* :51  */

    const uint64_t sign_prod = sa ^ sb;                              /* :53  */
    const int any_nan = a_nan || b_nan || c_nan;                     /* :54  */
    const int prod_is_inf = (a_inf || b_inf) && !(a_zero || b_zero); /* :55  */
    const int inf_times_zero = (a_inf && b_zero) || (b_inf && a_zero); /* :56 */
    const int inf_clash = prod_is_inf && c_inf && (sign_prod != sc); /* :57  */
    const int prod_is_zero = a_zero || b_zero;                       /* :58  */

    uint64_t ma = (ea != UINT64_C(0)) ? (fa | CQ_FP64_IMPLICIT) : fa;   /* :61 */
    uint64_t mb = (eb != UINT64_C(0)) ? (fb | CQ_FP64_IMPLICIT) : fb;   /* :62 */
    uint64_t mc = (ec != UINT64_C(0)) ? (fc | CQ_FP64_IMPLICIT) : fc;   /* :63 */
    int64_t ea_eff = (ea != UINT64_C(0)) ? (int64_t)ea : INT64_C(1);    /* :64 */
    int64_t eb_eff = (eb != UINT64_C(0)) ? (int64_t)eb : INT64_C(1);    /* :65 */
    int64_t ec_eff = (ec != UINT64_C(0)) ? (int64_t)ec : INT64_C(1);    /* :66 */

    fa128 p, p_side, c_side, wr, wrf;
    uint64_t ma_s, mb_s, mc_s, wr_56, sticky;
    uint64_t result_sign, flushed_result, normal_result, overflow_result;
    uint64_t prod_zero_result, result;
    int64_t expZ, expDiff, expR;
    int same_sign, expDiff_neg, use_short_shift, underflow, complete_cancel;
    int subnormal, flush_to_zero, exp_overflow, exp_overflow_after_round;

    cq_normalize_to_bit52_eval(&ma, &ea_eff);                        /* :67  */
    cq_normalize_to_bit52_eval(&mb, &eb_eff);                        /* :68  */
    cq_normalize_to_bit52_eval(&mc, &ec_eff);                        /* :69  */

    ma_s = ma << 10; mb_s = mb << 10; mc_s = mc << 9;                /* :72-74 */

    p    = fa_widemul(ma_s, mb_s);                                   /* :77  */
    expZ = ea_eff + eb_eff - INT64_C(0x3FE);                         /* :79  */

    if (p.hi < UINT64_C(0x2000000000000000)) {                       /* :83-87 */
        p = fa_shl128_by1(p);
        expZ -= INT64_C(1);
    }

    same_sign   = (sign_prod == sc);                                 /* :90  */
    expDiff     = expZ - ec_eff;                                     /* :91  */
    expDiff_neg = expDiff < INT64_C(0);                              /* :92  */

    {
        const fa128 p_rj   = fa_srj128(p, -expDiff);                 /* :99  */
        const fa128 p_shr1 = fa_shr128jam_by1(p);                    /* :100 */
        fa128 p_right;
        fa128 mc_pair;

        use_short_shift = (expDiff == INT64_C(-1)) && !same_sign;    /* :101 */
        p_right = use_short_shift ? p_shr1 : p_rj;                   /* :102-103 */
        p_side  = expDiff_neg ? p_right : p;                         /* :105-106 */

        mc_pair.hi = mc_s; mc_pair.lo = UINT64_C(0);
        {
            const fa128 c_right = fa_srj128(mc_pair, expDiff);       /* :110 */

            c_side.hi = expDiff_neg ? mc_s : c_right.hi;             /* :111 */
            c_side.lo = expDiff_neg ? UINT64_C(0) : c_right.lo;      /* :112 */
        }
    }
    expR = expDiff_neg ? ec_eff : expZ;                              /* :115 */

    {
        const fa128 sum  = fa_add128(p_side, c_side);                /* :118 */
        const fa128 diff = fa_sub128(p_side, c_side);                /* :119 */

        wr = same_sign ? sum : diff;                                 /* :120-121 */
    }

    underflow = !same_sign && ((wr.hi >> 63) != UINT64_C(0));        /* :125 */
    {
        const fa128 neg = fa_neg128(wr);                             /* :126 */

        if (underflow) wr = neg;                                     /* :127-128 */
    }
    result_sign = underflow ? sc : sign_prod;                        /* :134 */
    complete_cancel = !same_sign && wr.hi == UINT64_C(0)
                                 && wr.lo == UINT64_C(0);            /* :137 */

    wrf.hi = (wr.hi == UINT64_C(0)) ? wr.lo : wr.hi;                 /* :147 */
    wrf.lo = (wr.hi == UINT64_C(0)) ? UINT64_C(0) : wr.lo;           /* :148 */
    if (wr.hi == UINT64_C(0)) expR -= INT64_C(64);                   /* :149 */

    if ((wrf.hi >> 63) != UINT64_C(0)) {                             /* :153-157 */
        wrf = fa_shr128jam_by1(wrf);
        expR += INT64_C(1);
    }
    if ((wrf.hi >> 62) != UINT64_C(0)) {                             /* :161-165 */
        wrf = fa_shr128jam_by1(wrf);
        expR += INT64_C(1);
    }

    if (wrf.hi == UINT64_C(0) && wrf.lo == UINT64_C(0)) {            /* :172-174 */
        wrf.hi = UINT64_C(1); wrf.lo = UINT64_C(0);
    }
    wrf = fa_clz128(wrf, &expR);                                     /* :175 */

    sticky = (((wrf.hi & UINT64_C(0x3F)) != UINT64_C(0))             /* :181-183 */
              || (wrf.lo != UINT64_C(0))) ? UINT64_C(1) : UINT64_C(0);
    wr_56 = ((wrf.hi >> 6) & ((UINT64_C(1) << 56) - UINT64_C(1)))    /* :184 */
          | sticky;

    cq_handle_subnormal_eval(&wr_56, &expR, result_sign,
                             &flushed_result, &subnormal,
                             &flush_to_zero);                        /* :187 */
    cq_round_and_pack_eval(wr_56, expR, result_sign,
                           &normal_result, &overflow_result,
                           &exp_overflow, &exp_overflow_after_round); /* :189 */

    /* :195-196. Under round-to-nearest-even `fma(-0, +0, +0)` is `+0` and
     * `fma(-0, +0, -0)` is `-0`; an opposite-sign zero combine is `+0`. */
    prod_zero_result = c_zero ? ((sign_prod == sc) ? c : UINT64_C(0)) : c;

    /* :199-208 — the select chain, bottom-up: the last `ifelse` written wins,
     * so the NaN row is outermost. INDEF is NOT result_sign-signed
     * (Bennett-r84x), and `_sf_propagate_nan3` is a > b > c precedence
     * matching Intel VFMADD*'s order (softfloat_common.jl:32-35). */
    result = normal_result;                                          /* :199 */
    if (exp_overflow || exp_overflow_after_round)
        result = overflow_result;                                    /* :200 */
    if (subnormal && flush_to_zero) result = flushed_result;         /* :201 */
    if (complete_cancel)            result = UINT64_C(0);            /* :202 */
    if (prod_is_zero)               result = prod_zero_result;       /* :203 */
    if (c_inf && !prod_is_inf)      result = c;                      /* :204 */
    if (prod_is_inf) result = (sign_prod << 63) | CQ_FP64_INF_BITS;  /* :205 */
    if (inf_clash)      result = CQ_FP64_INDEF;                      /* :206 */
    if (inf_times_zero) result = CQ_FP64_INDEF;                      /* :207 */
    if (any_nan)                                                     /* :208 */
        result = a_nan ? (a | CQ_FP64_QUIET_BIT)
               : (b_nan ? (b | CQ_FP64_QUIET_BIT)
                        : (c | CQ_FP64_QUIET_BIT));
    return result;                                                   /* :210 */
}
