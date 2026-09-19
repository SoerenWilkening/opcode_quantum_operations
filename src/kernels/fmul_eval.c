/* src/kernels/fmul_eval.c — M34, K16. RISK R9's CLASSICAL ROW: `soft_fmul`'s
 * body transcribed into C over `uint64_t`, composing M32's four `*_eval`
 * helpers. It emits nothing, allocates nothing and reads no `cq_bit`.
 *
 * M32's `_eval` <-> THE CIRCUIT seam (K23 D-K23-9), taken here for the same
 * reason: the Julia is transcribed twice, once into a row table and once into
 * C, and putting the two in one file invites the second to be written from the
 * first.
 *
 * THE HOST `double` OPERATOR IS REFUSED, ON A MEASUREMENT (PRD-v2 §7.4). FOUR
 * of its five IEEE-unspecified cells are reachable from `fmul` — one more than
 * from `fadd`: `0 · Inf` through :210's INDEF arm, and the three NaN-payload
 * cells through :212's `_sf_propagate_nan2`. Every one is x86's choice and
 * arm64 differs on all of them, so a host-operator short-circuit would give a
 * program whose classical mode and quantum mode return different bits on the
 * same input, on some hosts only. This way they agree bit-for-bit everywhere.
 * The library's only contact with a C `double` on the fp surface stays the
 * `memcpy` at `cqrt_alloc_f64` / `cqrt_measure_f64` (PRD-v2 §3.1).
 *
 * THIS IS NOT L1's ORACLE, AND USING IT AS ONE IS THE STEP 18 TRAP. An oracle
 * that shares code with the implementation is blind to exactly what that code
 * gets wrong. L1's reference is the HOST operator with §7.4's cells pinned by
 * table (tests/support/fphost.h).
 *
 * WHY THE DEAD BINDINGS ARE HERE TOO. `fmul.jl:94-102` is abandoned — see
 * fmul.c's header — and the circuit transcribes it under PRD-v2 §7.6's
 * discarded-arm rule. This body transcribes it as well, so that the C and the
 * table are line-for-line the same reading of the same source and a reader
 * checking one against the other does not find a gap he has to explain. The
 * `(void)` casts are what stop -Werror from deleting the evidence.
 */

#include "kernels/fmul.h"

#include "kernels/fpclass.h"
#include "kernels/fpround.h"

/* `soft_fmul` — third_party/bennett/src/softfloat/fmul.jl:14-215, plus
 * `_sf_propagate_nan2` (softfloat_common.jl:23-24) inlined at :212. */
uint64_t cq_fmul_eval(uint64_t a, uint64_t b)
{
    uint64_t sa = a >> 63;                                         /* :19  */
    uint64_t ea = (a >> 52) & CQ_FP64_EXP_ALL;                     /* :20  */
    uint64_t fa = a & CQ_FP64_FRAC_MASK;                           /* :21  */
    uint64_t sb = b >> 63;                                         /* :23  */
    uint64_t eb = (b >> 52) & CQ_FP64_EXP_ALL;                     /* :24  */
    uint64_t fb = b & CQ_FP64_FRAC_MASK;                           /* :25  */
    uint64_t result_sign = sa ^ sb;                                /* :28  */

    /* THE SIX PREDICATES ARE M31's, HERE TOO — `cq_fp_class_eval` is the same
     * four rows the circuit runs (D-K23-9's shape), so the classical row and
     * the CLASS block cannot drift apart. */
    int a_nan  = cq_fp_class_eval(a, CQ_FP_IS_NAN);                /* :31  */
    int b_nan  = cq_fp_class_eval(b, CQ_FP_IS_NAN);                /* :32  */
    int a_inf  = cq_fp_class_eval(a, CQ_FP_IS_INF);                /* :33  */
    int b_inf  = cq_fp_class_eval(b, CQ_FP_IS_INF);                /* :34  */
    int a_zero = cq_fp_class_eval(a, CQ_FP_IS_ZERO);               /* :35  */
    int b_zero = cq_fp_class_eval(b, CQ_FP_IS_ZERO);               /* :36  */

    uint64_t inf_result  = (result_sign << 63) | CQ_FP64_INF_BITS; /* :40  */
    uint64_t zero_result = result_sign << 63;                      /* :41  */

    uint64_t ma = (ea != UINT64_C(0)) ? (fa | CQ_FP64_IMPLICIT) : fa;  /* :44 */
    uint64_t mb = (eb != UINT64_C(0)) ? (fb | CQ_FP64_IMPLICIT) : fb;  /* :45 */
    int64_t ea_eff = (ea != UINT64_C(0)) ? (int64_t)ea : INT64_C(1);   /* :48 */
    int64_t eb_eff = (eb != UINT64_C(0)) ? (int64_t)eb : INT64_C(1);   /* :49 */

    int64_t result_exp;
    uint64_t a_lo, a_hi, b_lo, b_hi, pp_ll, pp_lh, pp_hl, pp_hh, cross;
    uint64_t cross_lo, prod_lo, carry_lo, cross_hi, pp_hh_shifted;
    uint64_t acc_lo, term2, sum1, c1, term3, sum2, c2;
    uint64_t prod_lo_final, prod_hi_final, msb_at_105;
    uint64_t wr_105, sticky_105, wr_104, sticky_104, wr;
    uint64_t flushed_result, normal_result, overflow_result, result;
    int subnormal, flush_to_zero, exp_overflow, exp_overflow_after_round;

    cq_normalize_to_bit52_eval(&ma, &ea_eff);                      /* :57  */
    cq_normalize_to_bit52_eval(&mb, &eb_eff);                      /* :58  */

    result_exp = ea_eff + eb_eff - INT64_C(1023);                  /* :63  */

    a_lo = ma & UINT64_C(0x03FFFFFF);                              /* :73  */
    a_hi = ma >> 26;                                               /* :74  */
    b_lo = mb & UINT64_C(0x03FFFFFF);                              /* :75  */
    b_hi = mb >> 26;                                               /* :76  */

    pp_ll = a_lo * b_lo;                                           /* :78  */
    pp_lh = a_lo * b_hi;                                           /* :79  */
    pp_hl = a_hi * b_lo;                                           /* :80  */
    pp_hh = a_hi * b_hi;                                           /* :81  */

    cross = pp_lh + pp_hl;                                         /* :91  */

    /* :94-102 — THE ABANDONED ASSEMBLY. Nothing reads carry_lo, cross_hi or
     * pp_hh_shifted; the source restarts at :107. Transcribed under PRD-v2
     * §7.6's discarded-arm rule, exactly as the row table transcribes it. */
    cross_lo = cross << 26;                                        /* :94  */
    prod_lo  = pp_ll + cross_lo;                                   /* :95  */
    carry_lo = (prod_lo < pp_ll) ? UINT64_C(1) : UINT64_C(0);      /* :97  */
    cross_hi = cross >> 38;                                        /* :101 */
    pp_hh_shifted = pp_hh << 52;                                   /* :102 */
    (void)carry_lo; (void)cross_hi; (void)pp_hh_shifted;

    acc_lo = pp_ll;                                                /* :126 */
    term2  = cross << 26;                                          /* :127 */
    sum1   = acc_lo + term2;                                       /* :128 */
    c1     = (sum1 < acc_lo) ? UINT64_C(1) : UINT64_C(0);          /* :129 */
    term3  = pp_hh << 52;                                          /* :131 */
    sum2   = sum1 + term3;                                         /* :132 */
    c2     = (sum2 < sum1) ? UINT64_C(1) : UINT64_C(0);            /* :133 */

    prod_lo_final = sum2;                                          /* :135 */
    prod_hi_final = (cross >> 38) + (pp_hh >> 12) + c1 + c2;       /* :136 */

    msb_at_105 = (prod_hi_final >> 41) & UINT64_C(1);              /* :156 */

    wr_105 = ((prod_hi_final & ((UINT64_C(1) << 42) - UINT64_C(1))) << 14)
           | (prod_lo_final >> 50);                                /* :174 */
    sticky_105 = ((prod_lo_final & ((UINT64_C(1) << 50) - UINT64_C(1)))
                  != UINT64_C(0)) ? UINT64_C(1) : UINT64_C(0);     /* :175 */
    wr_105 = wr_105 | sticky_105;                                  /* :177 */

    wr_104 = ((prod_hi_final & ((UINT64_C(1) << 42) - UINT64_C(1))) << 15)
           | (prod_lo_final >> 49);                                /* :180 */
    sticky_104 = ((prod_lo_final & ((UINT64_C(1) << 49) - UINT64_C(1)))
                  != UINT64_C(0)) ? UINT64_C(1) : UINT64_C(0);     /* :181 */
    wr_104 = wr_104 | sticky_104;                                  /* :183 */

    wr = (msb_at_105 != UINT64_C(0)) ? wr_105 : wr_104;            /* :186 */
    result_exp = (msb_at_105 != UINT64_C(0)) ? result_exp + INT64_C(1)
                                             : result_exp;         /* :188 */
    wr = wr & ((UINT64_C(1) << 56) - UINT64_C(1));                 /* :191 */

    cq_normalize_clz_eval(&wr, &result_exp);                       /* :195 */
    cq_handle_subnormal_eval(&wr, &result_exp, result_sign,
                             &flushed_result, &subnormal,
                             &flush_to_zero);                      /* :198 */
    cq_round_and_pack_eval(wr, result_exp, result_sign,
                           &normal_result, &overflow_result,
                           &exp_overflow, &exp_overflow_after_round); /* :202 */

    /* :206-212 — the select chain, bottom-up: the last `ifelse` written wins.
     * INDEF is NOT result_sign-signed (Bennett-r84x). */
    result = normal_result;                                        /* :206 */
    if (exp_overflow || exp_overflow_after_round)
        result = overflow_result;                                  /* :207 */
    if (subnormal && flush_to_zero) result = flushed_result;       /* :208 */
    if (a_zero || b_zero)           result = zero_result;          /* :209 */
    if ((a_inf || b_inf) && (a_zero || b_zero))
        result = CQ_FP64_INDEF;                                    /* :210 */
    if ((a_inf || b_inf) && !(a_zero || b_zero))
        result = inf_result;                                       /* :211 */
    if (a_nan || b_nan)                                            /* :212 */
        result = a_nan ? (a | CQ_FP64_QUIET_BIT)                   /* common */
                       : (b | CQ_FP64_QUIET_BIT);                  /* :23-24 */
    return result;                                                 /* :214 */
}
