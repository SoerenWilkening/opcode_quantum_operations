/* src/kernels/fsqrt_eval.c — M40, K21. RISK R9's CLASSICAL ROW: `soft_fsqrt`'s
 * body transcribed into C over `uint64_t`, composing M31's class eval and
 * M32's two `*_eval` helpers. It emits nothing, allocates nothing and reads no
 * `cq_bit`.
 *
 * M32's `_eval` <-> THE CIRCUIT seam (K23 D-K23-9), taken here for the same
 * reason: the Julia is transcribed twice, once into a row table and once into
 * C, and putting the two in one file invites the second to be written from the
 * first.
 *
 * THE HOST `sqrt()` IS REFUSED, ON A MEASUREMENT (PRD-v2 §7.4). THREE of its
 * cells are reachable from `fsqrt` and one of them is in §7.4's own table BY
 * NAME: `sqrt(-1)` is `fff8000000000000` on this box and Bennett's `INDEF`,
 * and they agree HERE and would not on arm64, whose default NaN is positive.
 * The other two are the NaN passthrough — a payload preserved and an sNaN
 * force-quietened (`fsqrt.jl:116`, Bennett-r84x / U08) — where arm64
 * canonicalises instead. A host-operator short-circuit would therefore give a
 * program whose classical mode and quantum mode return different bits on the
 * same input, on some hosts only. This way they agree bit-for-bit everywhere.
 * The library's only contact with a C `double` on the fp surface stays the
 * `memcpy` at `cqrt_alloc_f64` / `cqrt_measure_f64` (PRD-v2 §3.1).
 *
 * AND NOT EVEN "JUST FOR THE NORMAL PATH", which is the tempting exception
 * here in a way it is not for `fadd`: IEEE 754 requires `sqrt` to be correctly
 * rounded, so a host `sqrt()` would agree with the circuit on every operand
 * IEEE specifies. It is still refused, because the cells it would NOT agree on
 * are exactly the ones a reader would stop checking.
 *
 * THIS IS NOT L1's ORACLE, AND USING IT AS ONE IS THE STEP 18 TRAP. An oracle
 * that shares code with the implementation is blind to exactly what that code
 * gets wrong. L1's reference is the HOST `sqrt()` with §7.4's cells pinned by
 * table (tests/support/fphost.h), which is sound for `sqrt` in a way it is not
 * for a COMMUTATIVE operator: there is no second operand for a compiler to
 * exchange, so `bd host-fp-operator-commutes-so-the-nan-payload-is-not-an-
 * oracle` does not reach this kernel.
 */

#include "kernels/fsqrt.h"

#include "kernels/fpclass.h"
#include "kernels/fpround.h"

#include <string.h>

static int64_t  as_i64(uint64_t v) { int64_t  r; memcpy(&r, &v, sizeof r); return r; }
static uint64_t as_u64(int64_t  v) { uint64_t r; memcpy(&r, &v, sizeof r); return r; }

/* `soft_fsqrt` — third_party/bennett/src/softfloat/fsqrt.jl:31-118. */
uint64_t cq_fsqrt_eval(uint64_t a)
{
    uint64_t sa = a >> 63;                                         /* :34  */
    uint64_t ea = (a >> 52) & CQ_FP64_EXP_ALL;                     /* :35  */
    uint64_t fa = a & CQ_FP64_FRAC_MASK;                           /* :36  */

    /* THE THREE PREDICATES ARE M31's, HERE TOO — `cq_fp_class_eval` is the
     * same rows the CLASS block runs, so the classical row and the circuit
     * cannot drift apart. `a_neg` is not one of M31's four. */
    int a_nan  = cq_fp_class_eval(a, CQ_FP_IS_NAN);                /* :39  */
    int a_inf  = cq_fp_class_eval(a, CQ_FP_IS_INF);                /* :40  */
    int a_zero = cq_fp_class_eval(a, CQ_FP_IS_ZERO);               /* :41  */
    int a_neg  = sa != UINT64_C(0);                                /* :42  */

    uint64_t ma = (ea != UINT64_C(0)) ? (fa | CQ_FP64_IMPLICIT) : fa;   /* :45 */
    int64_t ea_eff = (ea != UINT64_C(0)) ? as_i64(ea) : INT64_C(1);     /* :46 */

    uint64_t e_unb, ma_adj, a_hi, a_lo, q, r, wr, normal, overflow, result;
    int64_t result_exp;
    int e_is_odd, exp_overflow, exp_overflow_after_round;

    cq_normalize_to_bit52_eval(&ma, &ea_eff);                      /* :49  */

    /* EVERY ARITHMETIC STEP GOES THROUGH `uint64_t`, for fpround_eval.c's
     * reason: the circuit wraps mod 2^64 without comment and signed overflow
     * is UB in C. */
    e_unb = as_u64(ea_eff) - UINT64_C(1023);                       /* :57  */
    e_is_odd = (e_unb & UINT64_C(1)) != UINT64_C(0);               /* :58  */
    ma_adj = e_is_odd ? (ma << 1) : ma;                            /* :59  */

    /* :60's `>>` IS ARITHMETIC — `e_unb` is an `Int64` and :53-56 says why it
     * has to be — and C's `>>` on a negative signed value is
     * implementation-defined, so it is spelled bitwise: the circuit's
     * operation rather than the compiler's. `e_unb` is negative for every
     * operand below 1.0 and for every subnormal. */
    result_exp = as_i64(((e_unb >> 1) | (e_unb & (UINT64_C(1) << 63)))
                        + UINT64_C(1023));                         /* :60  */

    a_hi = ma_adj >> 6;                                            /* :68  */
    a_lo = (ma_adj & UINT64_C(0x3F)) << 58;                        /* :69  */

    q = UINT64_C(0);                                               /* :78  */
    r = UINT64_C(0);                                               /* :79  */
    for (int i = 0; i < CQ_FSQRT_ITERS; i++) {                     /* :80  */
        uint64_t top2 = (a_hi >> 62) & UINT64_C(3);                /* :81  */
        uint64_t t;
        int fits;

        a_hi = (a_hi << 2) | (a_lo >> 62);                         /* :82  */
        a_lo = a_lo << 2;                                          /* :83  */

        r = (r << 2) | top2;                                       /* :85  */
        t = (q << 2) | UINT64_C(1);                                /* :86  */
        fits = r >= t;                                             /* :87  */
        r = fits ? r - t : r;                                      /* :88  */
        q = (q << 1) | (fits ? UINT64_C(1) : UINT64_C(0));         /* :89  */
    }

    /* Kahan's theorem (:11-15, :93-95): one OR-ed sticky bit is enough and no
     * Markstein or Tuckerman correction follows. */
    wr = q | ((r != UINT64_C(0)) ? UINT64_C(1) : UINT64_C(0));     /* :96  */

    /* THREE OF THE FOUR RETURNED VALUES ARE DISCARDED AND ARE STILL COMPUTED,
     * which is PRD-v2 §7.6's discarded-arm rule and upstream's own statement
     * at :99-102. The `(void)` casts are what stop -Werror from deleting the
     * evidence, exactly as fmul_eval.c does for `fmul.jl:94-102`. */
    cq_round_and_pack_eval(wr, result_exp, UINT64_C(0), &normal, &overflow,
                           &exp_overflow, &exp_overflow_after_round); /* :103 */
    (void)overflow; (void)exp_overflow; (void)exp_overflow_after_round;

    /* :111-116 — the select chain, bottom-up: the last `ifelse` written wins.
     * `a_nan` fires STRICTLY LAST so a NEGATIVE NaN keeps its payload instead
     * of becoming INDEF (:107-110), and `sqrt(-0) = -0` by both the `a_zero`
     * row and `a_neg_nonzero`'s exclusion of it. */
    result = normal;                                               /* :112 */
    if (a_inf && !a_neg)              result = CQ_FP64_INF_BITS;   /* :113 */
    if (a_zero)                       result = a;                  /* :114 */
    if ((a_neg && !a_zero) && !a_nan) result = CQ_FP64_INDEF;      /* :115 */
    if (a_nan)                        result = a | CQ_FP64_QUIET_BIT; /* :116 */
    return result;                                                 /* :117 */
}
