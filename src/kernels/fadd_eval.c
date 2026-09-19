/* src/kernels/fadd_eval.c — M33, K15, the CLASSICAL half. The fourth seam: the
 * `_eval` bodies <-> the circuit (M32's D-K23-10, taken again here). Not one
 * line of this file emits a gate, names a scratch span or knows what a block
 * costs.
 *
 * WHAT THESE ARE FOR, AND WHAT THEY ARE NOT. PRD-v2 §7.4 decides that risk
 * R9's classical short-circuit is "a C transcription of the Julia body over
 * `uint64_t`" — the same source evaluated on constants — and NEVER the host
 * `double` operator, which disagrees with upstream on five IEEE-unspecified
 * cells on this box and differs again on arm64. `fadd` reaches three of those
 * five: `Inf - Inf` (fadd.jl:39's INDEF arm), the two-NaN payload order and
 * sNaN quietening (:133 through `_sf_propagate_nan2`). They agree on THIS box
 * and would not on an arm64 one, so a host-operator short-circuit would give a
 * program whose classical mode and quantum mode return different bits on the
 * same input, on some hosts only.
 *
 * THEY ARE NOT L1's ORACLE AND MUST NEVER BE USED AS ONE. An oracle sharing
 * shape with the implementation is blind to exactly what that implementation
 * gets wrong — the Step 18 finding. L1's reference here is the HOST `+` / `-`
 * with the three cells above pinned by TABLE from tests/support/fphost.h, so
 * the reference shares no code with this file.
 *
 * THE FOUR SHARED HELPERS ARE M32's, ON BOTH SIDES OF THE MODE SPLIT. The
 * circuit hands `_sf_normalize_clz`, `_sf_handle_subnormal` and
 * `_sf_round_and_pack` to M32's step blocks and this body hands them to M32's
 * `*_eval` — the same partition in both modes, which is the whole of D-K23-9's
 * argument ("five modules would otherwise transcribe the same thirty lines,
 * which is K15 §5.4's argument applied to the C side, where it is EASIER to
 * get subtly wrong because nothing structural checks it"). The six class
 * predicates go through M31's `cq_fp_class_eval` for the same reason.
 *
 * THE EXPONENT IS HELD AS A `uint64_t` BIT PATTERN WHEREVER IT IS INCREMENTED,
 * AND THAT IS ABOUT UB RATHER THAN TASTE — fpround_eval.c's rule, applied to
 * fadd.jl:106's `result_exp + Int64(1)`. The circuit's `add` block wraps mod
 * 2^64 without comment; signed overflow in C is undefined; unsigned arithmetic
 * wraps by definition, and the two boundary conversions go through `memcpy`,
 * which is also what keeps -Wconversion quiet without an
 * implementation-defined cast.
 *
 * JULIA'S SHIFT BY 64 IS ZERO, NOT THE IDENTITY, AND C's IS UNDEFINED.
 * fadd.jl:79's `wb >> d` takes an UNCLAMPED `d` that reaches 2046, and
 * PRD-v2 §7.6 audits it as DISCARDED: `wb_mid` is selected only for
 * `0 < d < 56`. This body gives JULIA's answer at every `d` rather than the
 * barrel's mod-64 one, for fpround_eval.c's reason — masking would make this
 * body agree with a MIS-PORTED select at :81 instead of disagreeing with it.
 */

#include "kernels/fadd.h"

#include "kernels/fpclass.h"
#include "kernels/fpround.h"

#include <string.h>

/* fadd.jl:17. The one IEEE constant softfloat_common.jl does not export. */
#define FA_SIGN_MASK  UINT64_C(0x8000000000000000)

static int64_t  as_i64(uint64_t v) { int64_t  r; memcpy(&r, &v, sizeof r); return r; }
static uint64_t as_u64(int64_t  v) { uint64_t r; memcpy(&r, &v, sizeof r); return r; }

/* `v >> s` with Julia's out-of-range answer: zero, not C's undefined. */
static uint64_t shr_julia(uint64_t v, uint64_t s)
{
    return (s >= 64u) ? UINT64_C(0) : (v >> (unsigned)s);
}

uint64_t cq_fadd_eval(uint64_t a, uint64_t b)
{
    uint64_t sa = a >> 63;                                        /* :20  */
    uint64_t ea = (a >> 52) & CQ_FP64_EXP_ALL;                    /* :21  */
    uint64_t fa = a & CQ_FP64_FRAC_MASK;                          /* :22  */
    uint64_t sb = b >> 63;                                        /* :24  */
    uint64_t eb = (b >> 52) & CQ_FP64_EXP_ALL;                    /* :25  */
    uint64_t fb = b & CQ_FP64_FRAC_MASK;                          /* :26  */
    int a_nan  = cq_fp_class_eval(a, CQ_FP_IS_NAN);               /* :29  */
    int b_nan  = cq_fp_class_eval(b, CQ_FP_IS_NAN);               /* :30  */
    int a_inf  = cq_fp_class_eval(a, CQ_FP_IS_INF);               /* :31  */
    int b_inf  = cq_fp_class_eval(b, CQ_FP_IS_INF);               /* :32  */
    int a_zero = cq_fp_class_eval(a, CQ_FP_IS_ZERO);              /* :33  */
    int b_zero = cq_fp_class_eval(b, CQ_FP_IS_ZERO);              /* :34  */

    uint64_t inf_inf_result   = (sa == sb) ? a : CQ_FP64_INDEF;   /* :39  */
    uint64_t zero_zero_result = (sa == sb) ? a : UINT64_C(0);     /* :44  */

    uint64_t a_mag = a & ~FA_SIGN_MASK;                           /* :47  */
    uint64_t b_mag = b & ~FA_SIGN_MASK;                           /* :48  */
    int swap = a_mag < b_mag;                                     /* :49  */

    uint64_t sa_ord = swap ? sb : sa;                             /* :51  */
    uint64_t sb_ord = swap ? sa : sb;                             /* :52  */
    uint64_t ea_ord = swap ? eb : ea;                             /* :53  */
    uint64_t eb_ord = swap ? ea : eb;                             /* :54  */
    uint64_t fa_ord = swap ? fb : fa;                             /* :55  */
    uint64_t fb_ord = swap ? fa : fb;                             /* :56  */

    uint64_t ma = (ea_ord != 0u) ? (fa_ord | CQ_FP64_IMPLICIT) : fa_ord;
    uint64_t mb = (eb_ord != 0u) ? (fb_ord | CQ_FP64_IMPLICIT) : fb_ord;
    uint64_t ea_eff = (ea_ord != 0u) ? ea_ord : UINT64_C(1);      /* :63  */
    uint64_t eb_eff = (eb_ord != 0u) ? eb_ord : UINT64_C(1);      /* :64  */
    uint64_t d  = ea_eff - eb_eff;                                /* :65  */
    uint64_t wa = ma << 3;                                        /* :68  */
    uint64_t wb = mb << 3;                                        /* :69  */

    uint64_t wb_large  = (wb != 0u) ? UINT64_C(1) : UINT64_C(0);  /* :73  */
    uint64_t d_clamped = (d == 0u) ? UINT64_C(1)
                                   : ((d >= 64u) ? UINT64_C(63) : d);
    uint64_t lost_mask = (UINT64_C(1) << d_clamped) - UINT64_C(1);/* :77  */
    uint64_t sticky = ((wb & lost_mask) != 0u) ? UINT64_C(1) : UINT64_C(0);
    uint64_t wb_mid = shr_julia(wb, d) | sticky;                  /* :79  */
    uint64_t wb_aligned = (d >= 56u) ? wb_large
                        : ((d > 0u) ? wb_mid : wb);               /* :81-83 */

    uint64_t wr_add = wa + wb_aligned;                            /* :86  */
    uint64_t wr_sub = wa - wb_aligned;                            /* :87  */
    int same_sign = (sa_ord == sb_ord);                           /* :89  */
    uint64_t wr_raw = same_sign ? wr_add : wr_sub;                /* :90  */
    int exact_cancel = (!same_sign) && (wr_sub == 0u);            /* :93  */
    uint64_t result_sign = sa_ord;                                /* :95  */
    uint64_t wr = exact_cancel ? UINT64_C(1) : wr_raw;            /* :99  */
    int64_t  result_exp = as_i64(ea_eff);                         /* :100 */

    int overflow      = ((wr >> 56) != 0u);                       /* :103 */
    uint64_t lost_ov  = wr & UINT64_C(1);                         /* :104 */
    uint64_t wr_ov    = (wr >> 1) | lost_ov;                      /* :105 */
    int64_t  exp_ov   = as_i64(as_u64(result_exp) + UINT64_C(1)); /* :106 */

    uint64_t flushed, normal, ovf_result, result;
    int subnormal, ftz, exp_ovf, exp_ovf_after;

    wr         = overflow ? wr_ov  : wr;                          /* :107 */
    result_exp = overflow ? exp_ov : result_exp;                  /* :108 */

    cq_normalize_clz_eval(&wr, &result_exp);                      /* :111 */
    cq_handle_subnormal_eval(&wr, &result_exp, result_sign,
                             &flushed, &subnormal, &ftz);         /* :114 */
    cq_round_and_pack_eval(wr, result_exp, result_sign, &normal,
                           &ovf_result, &exp_ovf, &exp_ovf_after);/* :118 */

    /* :123-133. Priority runs BOTTOM-UP: the last `ifelse` written wins, so
     * this chain is the source's order and each line overwrites the last. */
    result = normal;
    if (exp_ovf || exp_ovf_after)  result = ovf_result;           /* :124 */
    if (subnormal && ftz)          result = flushed;              /* :125 */
    if (exact_cancel)              result = UINT64_C(0);          /* :126 */
    if (a_zero && b_zero)          result = zero_zero_result;     /* :127 */
    if (b_zero && !a_zero)         result = a;                    /* :128 */
    if (a_zero && !b_zero)         result = b;                    /* :129 */
    if (a_inf && b_inf)            result = inf_inf_result;       /* :130 */
    if (b_inf && !a_inf)           result = b;                    /* :131 */
    if (a_inf && !b_inf)           result = a;                    /* :132 */
    if (a_nan || b_nan)                                           /* :133 */
        result = a_nan ? (a | CQ_FP64_QUIET_BIT)
                       : (b | CQ_FP64_QUIET_BIT);   /* common.jl:23-24 */
    return result;
}

/* `soft_fsub` — fsub.jl:19-25. THE GUARD IS `Bennett-m63k` AND ITS NaN TEST IS
 * SPELLED DIFFERENTLY FROM `soft_fadd`'s ON PURPOSE (:21 masks then shifts,
 * fadd.jl:25 shifts then masks), so it does NOT go through M31's
 * `cq_fp_class_eval` the way `cq_fadd_eval`'s six predicates do — under
 * §7.2's literal grain those are two different operator sequences and sharing
 * them is CSE. */
uint64_t cq_fsub_eval(uint64_t a, uint64_t b)
{
    uint64_t ea_b = (b & CQ_FP64_EXP_MASK) >> 52;                 /* :21  */
    uint64_t fa_b = b & CQ_FP64_FRAC_MASK;                        /* :22  */
    int b_is_nan  = (ea_b == CQ_FP64_EXP_ALL) && (fa_b != 0u);    /* :23  */
    uint64_t b_eff = b_is_nan ? b : (b ^ FA_SIGN_MASK);           /* :24  */

    return cq_fadd_eval(a, b_eff);                                /* :25  */
}
