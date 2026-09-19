/* src/kernels/fconv_eval.c — M37, K19. THE CLASSICAL ROWS: the same four
 * bodies evaluated in C over `uint64_t`, which emit nothing. The `_eval` <->
 * CIRCUIT seam M32 and M33 both take, taken again here.
 *
 * PRD-v2 §7.4 DECIDES THE SHAPE AND FOR K19 THE GROUND IS STRONGER THAN IT IS
 * ANYWHERE ELSE IN THE PORT. §7.4 refuses the host operator because its
 * answers on the IEEE-unspecified cells are x86's choices and ARM's differ, so
 * a host-operator short-circuit would give a program whose classical mode and
 * quantum mode return different bits on the same input, on some hosts only.
 * Here the host operator is not merely a different CHOICE: `(int64_t)d` for a
 * NaN, an infinity or an out-of-range `d`, and `(uint64_t)d` for a negative
 * one, are UNDEFINED BEHAVIOUR in C (C11 §6.3.1.4), and a compiler is entitled
 * to fold a compile-time one to anything at all. PRD-v2 §7.5's decision is to
 * pin upstream's x86 saturation, which is D3's posture applied a third time:
 * deterministic, documented, never traps.
 *
 * SO NOT ONE LINE BELOW TOUCHES A `double`. The library's only contact with
 * one on the fp surface is the memcpy at cqrt_alloc_f64 / cqrt_measure_f64
 * (PRD-v2 §3.1), and `-ffp-contract=off`'s reason does not reach here because
 * the transcription is integer-only.
 *
 * THE ARITHMETIC IS UNSIGNED THROUGHOUT, INCLUDING THE WRAPAROUND AT
 * fptosi.jl:41-42, AND THAT IS LOAD-BEARING (K19.md §5.6). Writing
 * `right_shift` as an `int64_t` reproduces §5.3's signed-comparator mutant in
 * C, where it is equally unobservable on today's inputs and equally wrong.
 *
 * NOT AN L1 ORACLE. An oracle sharing shape with the implementation is blind
 * to exactly what that implementation gets wrong (the Step 18 trap); L1's
 * reference is the HOST cast where C defines it and PRD-v2 §7.5 / K19.md §5.4
 * as LITERALS where C does not.
 */

#include "kernels/fconv.h"

#include "kernels/fadd.h"
#include "kernels/kernel.h"

#define FV_FRAC   UINT64_C(0x000FFFFFFFFFFFFF)
#define FV_HIBIT  UINT64_C(0x8000000000000000)
#define FV_KBIAS  UINT64_C(0x43E0000000000000)

/* --- `soft_fptosi` — fptosi.jl:22-69, line for line. --------------------- */

uint64_t cq_fptosi_eval(uint64_t a)
{
    uint64_t sign = (a >> 63) & UINT64_C(1);              /* :23 */
    uint64_t exp  = (a >> 52) & UINT64_C(0x7ff);          /* :24 */
    uint64_t mant = a & FV_FRAC;                          /* :25 */
    uint64_t is_normal = (exp != UINT64_C(0)) ? UINT64_C(1) : UINT64_C(0);
    uint64_t full_mant = mant | (is_normal << 52);        /* :29 */
    uint64_t right_shift = UINT64_C(1075) - exp;          /* :41 */
    uint64_t left_shift  = exp - UINT64_C(1075);          /* :42 */
    /* THE CLAMPS ARE THE D8 ROWS AND THEY LOOK LIKE COMMENTS. Drop either and
     * the circuit's barrel, which reads only the low 6 lanes at W = 64,
     * diverges from Julia's `x >> 64 == 0` on every operand below 2^-12
     * (K19.md §5.5). The comparison is UNSIGNED because the wraparound above
     * IS the out-of-range detector (PRD-v2 §7.6). */
    uint64_t rsc = (right_shift > UINT64_C(63)) ? UINT64_C(63) : right_shift;
    uint64_t lsc = (left_shift  > UINT64_C(63)) ? UINT64_C(63) : left_shift;
    uint64_t result_right = full_mant >> rsc;             /* :49 */
    uint64_t result_left  = full_mant << lsc;             /* :50 */
    uint64_t go_left = (exp >= UINT64_C(1075)) ? UINT64_C(1) : UINT64_C(0);
    uint64_t magnitude = (go_left == UINT64_C(1)) ? result_left : result_right;
    uint64_t negated = (~magnitude) + UINT64_C(1);        /* :60 */
    uint64_t signed_result = (sign == UINT64_C(1)) ? negated : magnitude;

    /* :66-67 — x86 `cvttsd2si`'s indefinite value for any NaN, either
     * infinity and every |x| >= 2^63. The single in-range value with biased
     * exp == 1086 is x == -2^63 exactly, whose own `(~mag)+1` computation
     * yields the same bits, so the unconditional saturation is idempotent on
     * it (fptosi.jl:16-20). */
    return (exp >= UINT64_C(1086)) ? FV_HIBIT : signed_result;
}

/* --- `soft_fptoui` — fptoui.jl:28-48. ------------------------------------ */

/* IT COMPOSES M33's `cq_fsub_eval` AT :45, WHICH IS THE CLASSICAL HALF OF THE
 * SAME HAND-OFF THE CIRCUIT MAKES THROUGH `cq_fsub_block`. `b` is the constant
 * KBIAS, so this is the one site where `fsub` and `fadd(a, fneg(b))` coincide
 * and PRD-v2 §5's prohibition is invisible — which is exactly why the real
 * `fsub` is called here too. */
uint64_t cq_fptoui_eval(uint64_t a)
{
    uint64_t sign = (a >> 63) & UINT64_C(1);              /* :29 */
    uint64_t exp  = (a >> 52) & UINT64_C(0x7ff);          /* :30 */
    int in_high_range = (sign == UINT64_C(0)) && (exp == UINT64_C(1086));
    uint64_t path_a = cq_fptosi_eval(a);                  /* :40 */
    uint64_t adjusted = cq_fsub_eval(a, FV_KBIAS);        /* :45 */
    uint64_t path_b = FV_HIBIT | cq_fptosi_eval(adjusted);/* :46 */

    return in_high_range ? path_b : path_a;               /* :48 */
}

/* --- `soft_sitofp` — sitofp.jl:15-85. ------------------------------------ */

/* FIVE UNIFORM STAGES AND A SHORT SIXTH, written out rather than looped, for
 * the same reason the row table is: sitofp.jl:49-50 does not update `tmp`, so
 * a loop body would have to carry the asymmetry as a condition and a reader
 * could not check it line against line. */
uint64_t cq_sitofp_eval(uint64_t a)
{
    int is_zero = (a == UINT64_C(0));                     /* :17 */
    uint64_t sign = (a >> 63) & UINT64_C(1);              /* :20 */
    uint64_t neg = (~a) + UINT64_C(1);                    /* :22 */
    uint64_t magnitude = (sign == UINT64_C(1)) ? neg : a; /* :23 */
    uint64_t clz = UINT64_C(0);                           /* :26 */
    uint64_t tmp = magnitude;                             /* :27 */
    uint64_t exponent, shift_clamped, shifted, mantissa, round_bit, sticky;
    uint64_t round_up, mant_overflow, result;

    if ((tmp >> 32) == UINT64_C(0)) { clz += 32; tmp <<= 32; }   /* :29-31 */
    if ((tmp >> 48) == UINT64_C(0)) { clz += 16; tmp <<= 16; }   /* :33-35 */
    if ((tmp >> 56) == UINT64_C(0)) { clz +=  8; tmp <<=  8; }   /* :37-39 */
    if ((tmp >> 60) == UINT64_C(0)) { clz +=  4; tmp <<=  4; }   /* :41-43 */
    if ((tmp >> 62) == UINT64_C(0)) { clz +=  2; tmp <<=  2; }   /* :45-47 */
    if ((tmp >> 63) == UINT64_C(0)) { clz +=  1; }               /* :49-50 */

    exponent = UINT64_C(1086) - clz;                      /* :54 */
    /* :59 — INERT by the ladder's own structure (32+16+8+4+2+1 = 63), and
     * transcribed anyway: K04 §7.5's box says a port that drops a clamp
     * because "the shift is in range anyway" has removed the thing that makes
     * it so. */
    shift_clamped = (clz > UINT64_C(63)) ? UINT64_C(63) : clz;
    shifted = magnitude << shift_clamped;                 /* :60 */
    mantissa  = (shifted >> 11) & FV_FRAC;                /* :63 */
    round_bit = (shifted >> 10) & UINT64_C(1);            /* :68 */
    sticky    = shifted & UINT64_C(0x3FF);                /* :69 */

    /* :71 — sitofp.jl's OWN round-to-nearest-even, NOT M32's
     * `_sf_round_and_pack`: a single round bit at 10 and a 10-bit sticky,
     * against M32's guard/round/sticky at bits 2/1/0 of a `wr << 3` working
     * value (K19.md §1.6). Substituting M32's block would be right on every
     * input that does not round and wrong on the ties. */
    round_up = round_bit & (((sticky != UINT64_C(0)) ? UINT64_C(1)
                                                     : UINT64_C(0))
                            | (mantissa & UINT64_C(1)));
    mantissa = mantissa + round_up;                       /* :72 */
    mant_overflow = (mantissa >> 52) & UINT64_C(1);       /* :75 */
    exponent = exponent + mant_overflow;                  /* :76 */
    mantissa = mantissa & FV_FRAC;                        /* :77 */
    result = (sign << 63) | (exponent << 52) | mantissa;  /* :80 */

    return is_zero ? UINT64_C(0) : result;                /* :83 */
}

/* --- `uitofp` — instructions.jl:7666-7682's routing, not a Julia body. --- */

/* There is NO `soft_uitofp` in the snapshot (grepped; `softfloat.jl:55-70`'s
 * export list and `callees.jl:63-66`'s five-entry `_CALLEES_FP_CONV` agree),
 * so this is the EXTRACTOR's dispatch transcribed: widen with `:zext` at
 * :7677-7679 and call `soft_sitofp`. The 64-bit source, which :7672-7673
 * passes through with no widening at all, is bead 9ve.34's abort and is
 * refused here as well as at the kernel — a classical caller must not be able
 * to reach a routing the circuit refuses. */
uint64_t cq_uitofp_eval(uint64_t a, int F)
{
    if (F == 64)
        cq_kernel_die("uitofp: i64 -> f64 is a loud abort, bead 9ve.34 / "
                      "PRD-v2 §7.9 — upstream routes UIToFP to soft_sitofp "
                      "with NO bias correction at this one width");
    if (F <= 0 || F > 64)
        cq_kernel_die("uitofp: the source width is outside (0, 64]");

    return cq_sitofp_eval(a & ((UINT64_C(1) << (unsigned)F) - UINT64_C(1)));
}
