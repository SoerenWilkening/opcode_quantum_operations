/* src/kernels/fround_eval.c — M38b, K25. RISK R9's CLASSICAL ROWS: the five
 * fround.jl bodies transcribed into C over `uint64_t`, composing M33's
 * `cq_fadd_eval` for floor/ceil. Emits nothing, allocates nothing, reads no
 * `cq_bit`. The `_eval` <-> THE CIRCUIT seam (K23 D-K23-9): the Julia is
 * transcribed twice, once into rows and once into C, and one file would invite
 * the second to be written from the first.
 *
 * THE HOST libm IS REFUSED (PRD-v2 §7.4). trunc/floor/ceil/rint/round are
 * exact, so the host would agree on every non-NaN input — and it would still
 * disagree on the NaN cells: upstream force-quietens an sNaN and keeps the
 * sign and payload (Bennett-r84x / U08), where a host may canonicalise. The
 * cells it would NOT agree on are exactly the ones a reader stops checking.
 *
 * NOT L1's ORACLE (the Step 18 trap): that is the host libm with the NaN rows
 * pinned by table (tests/test_kernel_fround_spec.inc).
 *
 * EVERY ARITHMETIC STEP GOES THROUGH `uint64_t` (fpround_eval.c's reason: the
 * circuit wraps mod 2^64 and signed overflow is UB in C); the `Int64` compares
 * compare the reinterpreted value, which is what `slt` computes.
 */

#include "kernels/fround.h"

#include "kernels/fadd.h"
#include "kernels/kernel.h"

#include <string.h>

#define SIGN_BIT UINT64_C(0x8000000000000000)
#define ONE_F    UINT64_C(0x3FF0000000000000)   /* reinterpret(UInt64, 1.0) */

static int64_t as_i64(uint64_t v) { int64_t r; memcpy(&r, &v, sizeof r); return r; }

/* `Base.clamp` is not in the snapshot; D-K23-5's reading, the one M32 ships:
 * `ifelse(x < lo, lo, ifelse(x > hi, hi, x))`. */
static uint64_t clamp_i64(uint64_t x, int64_t lo, int64_t hi)
{
    uint64_t inner = (as_i64(x) > hi) ? (uint64_t)hi : x;

    return (as_i64(x) < lo) ? (uint64_t)lo : inner;
}

static uint64_t shl(uint64_t v, uint64_t s) { return s < 64u ? v << s : 0u; }
static uint64_t shr(uint64_t v, uint64_t s) { return s < 64u ? v >> s : 0u; }

/* `soft_trunc` — fround.jl:16-50. */
static uint64_t trunc_eval(uint64_t a)
{
    uint64_t sign = a & SIGN_BIT;                                     /* :17 */
    uint64_t exp = (a >> 52) & CQ_FP64_EXP_ALL;                       /* :18 */
    int is_special = exp == UINT64_C(0x7ff);                          /* :26 */
    int is_nan_input = is_special && (a & CQ_FP64_FRAC_MASK) != 0u;   /* :27 */
    uint64_t special_result = is_nan_input ? (a | CQ_FP64_QUIET_BIT) : a;
    int is_small = as_i64(exp) < 1023;                                /* :30 */
    int is_integer = as_i64(exp) >= 1075;                             /* :32 */
    uint64_t frac_bits = UINT64_C(1075) - exp;                        /* :35 */
    uint64_t fbc = clamp_i64(frac_bits, 0, 52);                       /* :37 */
    uint64_t mask = ~(shl(UINT64_C(1), fbc) - UINT64_C(1));           /* :39 */
    uint64_t normal_result = a & mask;                                /* :42 */

    return is_special ? special_result                                /* :45 */
         : is_small ? sign
         : is_integer ? a
         : normal_result;
}

/* `soft_floor` — fround.jl:58-75 — and `soft_ceil` — :83-95. `-1.0` is
 * :69-70's `one_bits | 0x8000000000000000`. */
static uint64_t floor_ceil_eval(uint64_t a, int ceil)
{
    uint64_t sign = a & SIGN_BIT;                                  /* :59 :84 */
    int is_neg = sign != 0u;                                              /* :60 */
    int is_pos = sign == 0u;                                              /* :85 */
    uint64_t truncated = trunc_eval(a);                            /* :62 :87 */
    int has_frac = truncated != a;                                 /* :65 :89 */
    uint64_t step;

    if (!ceil) {
        step = cq_fadd_eval(truncated, ONE_F | SIGN_BIT);                 /* :73 */
        return (is_neg && has_frac) ? step : truncated;                   /* :74 */
    }
    step = cq_fadd_eval(truncated, ONE_F);                                /* :93 */
    return (is_pos && has_frac) ? step : truncated;                       /* :94 */
}

/* `soft_round` — fround.jl:141-202 (away = 0) — and `soft_round_away` —
 * :238-295 (away = 1). They differ in exactly two places, marked. */
static uint64_t round_eval(uint64_t a, int away)
{
    uint64_t sign = a & SIGN_BIT;                                        /* :142 */
    uint64_t abs_a = a & ~SIGN_BIT;                                      /* :143 */
    uint64_t exp = (abs_a >> 52) & CQ_FP64_EXP_ALL;                      /* :144 */
    uint64_t frac = abs_a & CQ_FP64_FRAC_MASK;
    int is_special = exp == UINT64_C(0x7ff);                             /* :147 */
    int is_nan_input = is_special && frac != 0u;                         /* :148 */
    uint64_t special_result = is_nan_input ? (a | CQ_FP64_QUIET_BIT) : a;
    int is_below_half = as_i64(exp) < 1022;                              /* :152 */
    int is_exactly_half = exp == UINT64_C(1022) && frac == 0u;           /* :155 */
    int is_in_half_to_one = exp == UINT64_C(1022) && frac != 0u;         /* :158 */
    uint64_t one_with_sign = sign | ONE_F;                               /* :159 */
    int is_integer = as_i64(exp) >= 1075;                                /* :162 */
    uint64_t fb = clamp_i64(UINT64_C(1075) - exp, 1, 52);         /* :167-168 */
    uint64_t rbp = fb - UINT64_C(1);                                     /* :169 */
    uint64_t m = frac | CQ_FP64_IMPLICIT;                                /* :171 */
    uint64_t tm = m & ~(shl(UINT64_C(1), fb) - UINT64_C(1));      /* :172-173 */
    uint64_t round_bit = shr(m, rbp) & UINT64_C(1);                      /* :175 */
    uint64_t sticky_mask = shl(UINT64_C(1), rbp) - UINT64_C(1);          /* :176 */
    int sticky = (m & sticky_mask) != 0u;                                /* :177 */
    uint64_t lsb = shr(tm, fb) & UINT64_C(1);                            /* :178 */
    int round_up;
    uint64_t rounded_m, final_m, final_exp, normal_result;
    int has_carry;

    /* DIFFERENCE 1 (:274): ties-away rounds up on the round bit alone. */
    round_up = away ? (round_bit == 1u)
                    : ((round_bit == 1u) && (sticky || lsb == 1u));      /* :180 */
    rounded_m = tm + (round_up ? shl(UINT64_C(1), fb) : UINT64_C(0)); /* :183-184 */
    has_carry = (rounded_m >> 53) != 0u;                                 /* :187 */
    final_m = has_carry ? (rounded_m >> 1) : rounded_m;                  /* :188 */
    final_exp = has_carry ? exp + UINT64_C(1) : exp;                     /* :189 */
    normal_result = sign | (final_exp << 52)
                  | (final_m & CQ_FP64_FRAC_MASK);                  /* :191-193 */

    if (is_special) return special_result;                               /* :196 */
    /* DIFFERENCE 2 (:254, :290-291): |x| == 0.5 goes to ±1.0, not ±0. */
    if (away) {
        if (is_below_half) return sign;
        if (exp == UINT64_C(1022)) return one_with_sign;
    } else {
        if (is_below_half || is_exactly_half) return sign;               /* :197 */
        if (is_in_half_to_one) return one_with_sign;                     /* :198 */
    }
    return is_integer ? a : normal_result;                               /* :199 */
}

uint64_t cq_fround_eval(cq_fround_prog p, uint64_t a)
{
    switch (p) {
    case CQ_FROUND_TRUNC: return trunc_eval(a);
    case CQ_FROUND_FLOOR: return floor_ceil_eval(a, 0);
    case CQ_FROUND_CEIL:  return floor_ceil_eval(a, 1);
    case CQ_FROUND_EVEN:  return round_eval(a, 0);
    case CQ_FROUND_AWAY:  return round_eval(a, 1);
    case CQ_FROUND_N_PROG:
    default: break;
    }
    cq_kernel_die("fround: a program id outside the five");
    return 0;
}
