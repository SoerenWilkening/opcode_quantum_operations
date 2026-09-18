/* src/kernels/fpround_eval.c — M32, K23, the CLASSICAL half. The third seam:
 * the `_eval` bodies <-> the circuit (K23 §5.8 / D-K23-10). Not one line here
 * emits a gate, names a scratch span or knows what a block costs.
 *
 * WHAT THESE ARE FOR, AND WHAT THEY ARE NOT. PRD-v2 §7.4 decides that risk
 * R9's classical short-circuit is "a C transcription of the Julia body over
 * `uint64_t`" — the same source evaluated on constants — and NEVER the host
 * `double` operator, which disagrees with upstream on five IEEE-unspecified
 * cells on this box and differs again on arm64. M32's five consumers each have
 * a classical row and each of those rows must evaluate these same four
 * helpers, so exporting them once is K15 §5.4's argument applied to the C side,
 * where it is EASIER to get subtly wrong because nothing structural checks it
 * (D-K23-9). M31's `cq_fp_class_eval` set the precedent.
 *
 * THEY ARE NOT L1's ORACLE FOR THIS MODULE AND MUST NEVER BE USED AS ONE. An
 * oracle sharing shape with the implementation is blind to exactly what that
 * implementation gets wrong — the Step 18 finding — and PRD-v2 §7.16 decides
 * L1 here is a hand-written IEEE-754 reference model instead
 * (tests/support/fpref.h), written from the standard's rules and never from
 * the Julia. These bodies are checked AGAINST that model, not with it.
 *
 * THE EXPONENTS ARE HELD AS `uint64_t` BIT PATTERNS INSIDE, AND THAT IS ABOUT
 * UB RATHER THAN TASTE. `result_exp + 1` at INT64_MAX and `Int64(1) -
 * result_exp` at INT64_MIN are undefined in C, while the circuit's `add` and
 * `sub` blocks wrap mod 2^64 without comment — so a faithful transcription has
 * to wrap too. Unsigned arithmetic wraps by definition; the two boundary
 * conversions go through `memcpy`, which is also what keeps -Wconversion
 * quiet without an implementation-defined cast.
 *
 * THE SIGNED COMPARES BRANCH ON THE SIGN BIT AND THEN COMPARE MAGNITUDES,
 * which is deliberately NOT `lower_slt!`'s bias flip (arith.jl:465-472). The
 * two are independent spellings of the same order, so a slip in one does not
 * reproduce in the other — the same reason `cq_ref_icmp`'s SGE row exists.
 *
 * JULIA'S SHIFT BY 64 IS ZERO, NOT THE IDENTITY, and C's is undefined. The two
 * helpers below give Julia's answer. At the one site that matters
 * (softfloat_common.jl:180, :182) the amount is bounded into [0, 63] by the
 * clamp at :178, so they never fire — and they are written the Julia way
 * rather than masked mod 64 on purpose: masking would make this body agree
 * with a MIS-PORTED clamp instead of disagreeing with it.
 */

#include "kernels/fpround.h"

#include <string.h>

static int64_t  as_i64(uint64_t v) { int64_t  r; memcpy(&r, &v, sizeof r); return r; }
static uint64_t as_u64(int64_t  v) { uint64_t r; memcpy(&r, &v, sizeof r); return r; }

/* `a <s b` over two's-complement bit patterns: a negative operand orders below
 * a non-negative one, and same-sign operands order as unsigned. */
static int i64_lt(uint64_t a, uint64_t b)
{
    int sa = (int)(a >> 63), sb = (int)(b >> 63);

    if (sa != sb) return sa;
    return a < b;
}

static int i64_le(uint64_t a, uint64_t b) { return !i64_lt(b, a); }
static int i64_ge(uint64_t a, uint64_t b) { return !i64_lt(a, b); }

static uint64_t shl_jl(uint64_t v, uint64_t k) { return (k >= 64u) ? 0u : v << k; }
static uint64_t shr_jl(uint64_t v, uint64_t k) { return (k >= 64u) ? 0u : v >> k; }

/* `clamp(x, lo, hi)` read as `ifelse(x < lo, lo, ifelse(x > hi, hi, x))` —
 * D-K23-5, the same four rows in the same order the circuit transcribes, and
 * the one construction in M32 that Rule 1 does not cover because `Base.clamp`'s
 * body is not in the pinned snapshot. Every reading agrees in VALUE wherever
 * `lo <= hi`, which both M32 sites satisfy by construction. */
static uint64_t clamp_i64(uint64_t x, uint64_t lo, uint64_t hi)
{
    uint64_t v = i64_lt(hi, x) ? hi : x;

    return i64_lt(x, lo) ? lo : v;
}

/* --- softfloat_common.jl:113-139 and :68-105, the two CLZ ladders. -------- */

/* One stage: the mask test, the shift and the decrement, as :114-116 writes
 * them. `k` is the shift and the decrement both, which is upstream's shape. */
#define CLZ_STAGE_EVAL(v, ev, mk, k)                                           \
    do {                                                                       \
        int need = (((v) & (mk)) == UINT64_C(0));                              \
        (v)  = need ? ((v) << (k)) : (v);                                      \
        (ev) = need ? ((ev) - (uint64_t)(k)) : (ev);                           \
    } while (0)

void cq_normalize_clz_eval(uint64_t *wr, int64_t *rexp)
{
    uint64_t v = *wr, e = as_u64(*rexp);

    CLZ_STAGE_EVAL(v, e, (UINT64_C(0xFFFFFFFF) << 24), 32);   /* :114-116 */
    CLZ_STAGE_EVAL(v, e, (UINT64_C(0xFFFF)     << 40), 16);   /* :118-120 */
    CLZ_STAGE_EVAL(v, e, (UINT64_C(0xFF)       << 48),  8);   /* :122-124 */
    CLZ_STAGE_EVAL(v, e, (UINT64_C(0xF)        << 52),  4);   /* :126-128 */
    CLZ_STAGE_EVAL(v, e, (UINT64_C(0x3)        << 54),  2);   /* :130-132 */
    CLZ_STAGE_EVAL(v, e, (UINT64_C(1)          << 55),  1);   /* :134-136 */

    *wr = v;
    *rexp = as_i64(e);
}

void cq_normalize_to_bit52_eval(uint64_t *m, int64_t *e)
{
    uint64_t v = *m, ev, e_orig = as_u64(*e);
    int m_zero = (v == UINT64_C(0));                          /* :72 */

    ev = e_orig;                                              /* :73 */
    v  = m_zero ? CQ_FP64_IMPLICIT : v;                       /* :74 */

    CLZ_STAGE_EVAL(v, ev, (UINT64_C(0xFFFFFFFF) << 21), 32);  /* :76-78  */
    CLZ_STAGE_EVAL(v, ev, (UINT64_C(0xFFFF)     << 37), 16);  /* :80-82  */
    CLZ_STAGE_EVAL(v, ev, (UINT64_C(0xFF)       << 45),  8);  /* :84-86  */
    CLZ_STAGE_EVAL(v, ev, (UINT64_C(0xF)        << 49),  4);  /* :88-90  */
    CLZ_STAGE_EVAL(v, ev, (UINT64_C(0x3)        << 51),  2);  /* :92-94  */
    CLZ_STAGE_EVAL(v, ev, (UINT64_C(1)          << 52),  1);  /* :96-98  */

    *m = m_zero ? UINT64_C(0) : v;                            /* :102 */
    *e = as_i64(m_zero ? e_orig : ev);                        /* :103 */
}

/* --- softfloat_common.jl:174-191. ---------------------------------------- */

void cq_handle_subnormal_eval(uint64_t *wr, int64_t *rexp, uint64_t rsign,
                              uint64_t *flushed, int *subnormal, int *ftz)
{
    uint64_t w = *wr, e = as_u64(*rexp);
    uint64_t shift_sub, shift_clamped, shift_u, lost_mask, lost, wr_sub, inner;
    int sn, fz;

    sn        = i64_le(e, UINT64_C(0));                       /* :175 */
    shift_sub = UINT64_C(1) - e;                              /* :176 */
    fz        = i64_ge(shift_sub, UINT64_C(56));              /* :177 */
    shift_clamped = clamp_i64(shift_sub, UINT64_C(0), UINT64_C(63));  /* :178 */
    shift_u   = fz ? UINT64_C(0) : shift_clamped;             /* :179 */
    lost_mask = shl_jl(UINT64_C(1), shift_u) - UINT64_C(1);   /* :180 */
    lost      = ((w & lost_mask) != UINT64_C(0)) ? UINT64_C(1) : UINT64_C(0);
    wr_sub    = shr_jl(w, shift_u) | lost;                    /* :182 */

    *flushed   = shl_jl(rsign, UINT64_C(63));                 /* :183 */
    inner      = fz ? w : wr_sub;                             /* :186 */
    *wr        = sn ? inner : w;                              /* :185 */
    *rexp      = as_i64(sn ? UINT64_C(0) : e);                /* :188 */
    *subnormal = sn;
    *ftz       = fz;
}

/* --- softfloat_common.jl:198-227. ---------------------------------------- */

void cq_round_and_pack_eval(uint64_t wr, int64_t rexp, uint64_t rsign,
                            uint64_t *normal, uint64_t *overflow,
                            int *exp_ovf, int *exp_ovf_after)
{
    uint64_t e = as_u64(rexp);
    uint64_t guard, round_bit, sticky, frac, grs, frac_rounded, frac_final;
    uint64_t exp_after, exp_pack;
    int round_up, mant_overflow;

    *exp_ovf  = i64_ge(e, CQ_FP64_EXP_ALL);                   /* :200 */
    *overflow = shl_jl(rsign, UINT64_C(63)) | CQ_FP64_INF_BITS;  /* :201 */

    guard     = (wr >> 2) & UINT64_C(1);                      /* :204 */
    round_bit = (wr >> 1) & UINT64_C(1);                      /* :205 */
    sticky    = wr & UINT64_C(1);                             /* :206 */
    frac      = (wr >> 3) & CQ_FP64_FRAC_MASK;                /* :207 */

    grs       = (guard << 2) | (round_bit << 1) | sticky;     /* :209 */
    round_up  = (grs > UINT64_C(4))                           /* :210 */
                || (grs == UINT64_C(4) && (frac & UINT64_C(1)) != UINT64_C(0));

    frac_rounded  = frac + UINT64_C(1);                       /* :212 */
    mant_overflow = (frac_rounded == CQ_FP64_IMPLICIT);       /* :213 */
    frac_final    = round_up ? (mant_overflow ? UINT64_C(0) : frac_rounded)
                             : frac;                          /* :214-216 */
    exp_after     = (round_up && mant_overflow) ? (e + UINT64_C(1)) : e;
    *exp_ovf_after = i64_ge(exp_after, CQ_FP64_EXP_ALL);      /* :220 */

    exp_pack = clamp_i64(exp_after, UINT64_C(0), UINT64_C(0x7FE));   /* :223 */
    *normal  = shl_jl(rsign, UINT64_C(63))
             | (exp_pack << 52) | frac_final;                 /* :224 */
}
