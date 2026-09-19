/* src/kernels/fdiv_eval.c — M35, K17. RISK R9's CLASSICAL ROW: `soft_fdiv`'s
 * body transcribed into C over `uint64_t`, composing M31's `cq_fp_class_eval`
 * and M32's four `*_eval` helpers. It emits nothing, allocates nothing and
 * reads no `cq_bit`.
 *
 * M32's `_eval` <-> THE CIRCUIT seam (K23 D-K23-9), taken here for the same
 * reason: the Julia is transcribed twice, once into a row table and once into
 * C, and putting the two in one file invites the second to be written from the
 * first.
 *
 * THE HOST `double` OPERATOR IS REFUSED, ON A MEASUREMENT (PRD-v2 §7.4). FOUR
 * of its five IEEE-unspecified cells are reachable from `fdiv`: `0/0` and
 * `Inf/Inf` both take :130 and :133's INDEF arm, and the three NaN-payload
 * cells go through `_sf_propagate_nan2` at :136. Every one is x86's choice and
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
 * THE EXPONENT ARITHMETIC GOES THROUGH `uint64_t`, which is K23's note one
 * module over: `result_exp - 1` at INT64_MIN is UB in C and the circuit wraps
 * mod 2^64 without comment. Every step that can wrap is spelled unsigned and
 * cast back.
 */

#include "kernels/fdiv.h"

#include "kernels/fpclass.h"
#include "kernels/fpround.h"

/* `soft_fdiv` — third_party/bennett/src/softfloat/fdiv.jl:44-139, plus
 * `_sf_propagate_nan2` (softfloat_common.jl:23-24) inlined at :136. */
uint64_t cq_fdiv_eval(uint64_t a, uint64_t b)
{
    uint64_t sa = a >> 63;                                         /* :47  */
    uint64_t ea = (a >> 52) & CQ_FP64_EXP_ALL;                     /* :48  */
    uint64_t fa = a & CQ_FP64_FRAC_MASK;                           /* :49  */
    uint64_t sb = b >> 63;                                         /* :50  */
    uint64_t eb = (b >> 52) & CQ_FP64_EXP_ALL;                     /* :51  */
    uint64_t fb = b & CQ_FP64_FRAC_MASK;                           /* :52  */
    uint64_t result_sign = sa ^ sb;                                /* :54  */

    /* THE SIX PREDICATES ARE M31's, HERE TOO — `cq_fp_class_eval` is the same
     * four rows the circuit runs (D-K23-9's shape), so the classical row and
     * the CLASS block cannot drift apart. */
    int a_nan  = cq_fp_class_eval(a, CQ_FP_IS_NAN);                /* :56  */
    int b_nan  = cq_fp_class_eval(b, CQ_FP_IS_NAN);                /* :57  */
    int a_inf  = cq_fp_class_eval(a, CQ_FP_IS_INF);                /* :58  */
    int b_inf  = cq_fp_class_eval(b, CQ_FP_IS_INF);                /* :59  */
    int a_zero = cq_fp_class_eval(a, CQ_FP_IS_ZERO);               /* :60  */
    int b_zero = cq_fp_class_eval(b, CQ_FP_IS_ZERO);               /* :61  */

    uint64_t inf_result  = (result_sign << 63) | CQ_FP64_INF_BITS; /* :63  */
    uint64_t zero_result = result_sign << 63;                      /* :64  */

    uint64_t ma = (ea != UINT64_C(0)) ? (fa | CQ_FP64_IMPLICIT) : fa;  /* :66 */
    uint64_t mb = (eb != UINT64_C(0)) ? (fb | CQ_FP64_IMPLICIT) : fb;  /* :67 */
    int64_t ea_eff = (ea != UINT64_C(0)) ? (int64_t)ea : INT64_C(1);   /* :68 */
    int64_t eb_eff = (eb != UINT64_C(0)) ? (int64_t)eb : INT64_C(1);   /* :69 */

    int64_t result_exp;
    uint64_t q, r, sticky, wr;
    uint64_t flushed_result, normal_result, overflow_result, result;
    int need_shift, subnormal, flush_to_zero;
    int exp_overflow, exp_overflow_after_round;

    /* Bennett-r6e3: BOTH operands, unconditionally. `m == 0` returns `(0, e)`
     * unchanged (post-Bennett-tpg0), which is why a zero operand reaching the
     * ladder is harmless and is caught by the select chain regardless. */
    cq_normalize_to_bit52_eval(&ma, &ea_eff);                      /* :78  */
    cq_normalize_to_bit52_eval(&mb, &eb_eff);                      /* :79  */

    result_exp = (int64_t)((uint64_t)ea_eff - (uint64_t)eb_eff
                           + UINT64_C(1023));                      /* :81  */

    /* :89-98 — the 56-iteration restoring-division loop, VERBATIM. 56 and not
     * 64: the count is decoupled from the span width and is what :13-14's
     * invariant needs. The loop shifts r AFTER extracting the quotient bit, so
     * the sticky test at :101 reads the SHIFTED remainder. */
    q = UINT64_C(0);                                               /* :89  */
    r = ma;                                                        /* :90  */
    for (int i = 0; i <= 55; i++) {                                /* :91  */
        int fits = (r >= mb);                                      /* :93  */

        r = fits ? (r - mb) : r;                                   /* :94  */
        q = (q << 1) | (fits ? UINT64_C(1) : UINT64_C(0));         /* :95  */
        r = r << 1;                                                /* :97  */
    }

    sticky = (r != UINT64_C(0)) ? UINT64_C(1) : UINT64_C(0);       /* :101 */
    wr = q | sticky;                                               /* :102 */

    need_shift = ((wr >> 55) == UINT64_C(0));                      /* :106 */
    wr = need_shift ? (wr << 1) : wr;                              /* :107 */
    result_exp = need_shift ? (int64_t)((uint64_t)result_exp - UINT64_C(1))
                            : result_exp;                          /* :108 */

    cq_normalize_clz_eval(&wr, &result_exp);                       /* :111 */
    cq_handle_subnormal_eval(&wr, &result_exp, result_sign,
                             &flushed_result, &subnormal,
                             &flush_to_zero);                      /* :114 */
    cq_round_and_pack_eval(wr, result_exp, result_sign,
                           &normal_result, &overflow_result,
                           &exp_overflow, &exp_overflow_after_round); /* :122 */

    /* `overflow_result` is destructured as `_` at :122 (Bennett-ardf / U138) —
     * the overflow rows fire `inf_result` directly at :127. The binding is
     * kept so the C and the row table are line-for-line the same reading of
     * the same source; `(void)` is what stops -Werror deleting the evidence. */
    (void)overflow_result;

    /* :126-136 — the select chain, bottom-up: the last `ifelse` written wins,
     * so NaN is outermost. `INDEF` is x86's and is NEGATIVE (Bennett-r84x); a
     * port emitting `QNAN` would be right on the exponent and wrong on the
     * sign bit. */
    result = normal_result;                                        /* :126 */
    if (exp_overflow || exp_overflow_after_round)
        result = inf_result;                                       /* :127 */
    if (subnormal && flush_to_zero) result = flushed_result;       /* :128 */
    if (a_zero && b_zero)           result = CQ_FP64_INDEF;        /* :130 */
    if (a_zero && !b_zero)          result = zero_result;          /* :131 */
    if (b_zero && !a_zero)          result = inf_result;           /* :132 */
    if (a_inf && b_inf)             result = CQ_FP64_INDEF;        /* :133 */
    if (a_inf && !b_inf)            result = inf_result;           /* :134 */
    if (b_inf && !a_inf)            result = zero_result;          /* :135 */
    if (a_nan || b_nan)                                            /* :136 */
        result = a_nan ? (a | CQ_FP64_QUIET_BIT)                   /* common */
                       : (b | CQ_FP64_QUIET_BIT);                  /* :23-24 */
    return result;                                                 /* :138 */
}
