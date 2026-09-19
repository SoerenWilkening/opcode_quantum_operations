/* tests/test_kernel_fdiv_anchors.inc — PRD-v2 §7.12's anchor list for `fdiv`,
 * refined against `soft_fdiv`'s own named predicates (K17.md §5.10), as a
 * provider COMPOSING tests/support/fpanchors.c's generic binary one. Included
 * by test_kernel_fdiv.c BEFORE the shape function that installs it.
 *
 * WRITTEN BY SUBTRACTION FROM THE GENERIC 25, NOT FROM THE K-DOC's LIST
 * (bd fp-anchors-the-generic-25-already-carry-most-of-them). Of K17.md §5.10's
 * 26 rows, ELEVEN are already carried by fpanchors.c — the four ±0 pairs are
 * `0/0`, the four ±Inf pairs are `Inf/Inf`, and the NaN block is rows 8-11
 * entire (default NaN both ways, two payload qNaNs both ways, an sNaN against
 * QNAN_**B** both ways with distinguishable payloads). What is left is the
 * source's own branch predicates, which is exactly what §7.12 asks for.
 *
 * NO ARITHMETIC, ONLY LITERALS, for fpanchors.c's reason: a constant computed
 * as `1.0 / 0.0` would be a host probe rather than a table, and this file must
 * read the same on every host. Each constructed row's derivation is stated so
 * a reader can check the pattern against the arithmetic rather than trust it.
 *
 * TWO ROWS OF K17.md §5.10 ARE NOT HERE AND THE REASONS ARE DIFFERENT.
 *
 * (a) §5.10 ROWS 19-20 ASK FOR A TIE "WHOSE EXACT QUOTIENT HAS EXACTLY 54
 *     SIGNIFICANT BITS", AND FOR A **NORMAL** RESULT THAT IS UNREACHABLE.
 *     After `_sf_normalize_to_bit52` both significands are integers in
 *     [2^52, 2^53), so a tie needs `ma * 2^53 = N * mb` with `N` odd, whence
 *     `v2(mb) = v2(ma) + 53 >= 53` — impossible for `mb < 2^53`. (The same
 *     count rules out a normal-range `mant_overflow`: `ma <= 2*mb - 1` gives
 *     `ma/mb <= 2 - 1/mb < 2 - 2^-53`, strictly below the midpoint, so an
 *     all-ones significand always rounds DOWN.) Division ties exist only where
 *     the precision is REDUCED, i.e. in the subnormal range, so the two tie
 *     rows below are subnormal quotients and are none the weaker for it —
 *     `_sf_round_and_pack` reads the same G/R/S either way.
 *
 * (b) §5.10 ROW 25, `exp_overflow_after_round` WITHOUT `exp_overflow`, IS
 *     UNREACHABLE FOR DIVISION AND HAS NO ROW. It needs `round_up &
 *     mant_overflow` at `result_exp == 0x7FE` (softfloat_common.jl:215-221),
 *     and the count in (a) says `mant_overflow` cannot fire in the normal
 *     range at all. It is NOT omitted by accident and it is NOT a hole in the
 *     table: it is a dead cell of M32's helper as reached FROM `soft_fdiv`,
 *     and `fmul`'s FM_OVFAFT row is what covers it in the catalogue. Row 21
 *     below fires `mant_overflow` on the path where division CAN reach it.
 */

/* Values this file needs that fpanchors.h does not name. Everything else comes
 * from there, so a reader checks a row against that file rather than against a
 * second copy of the NaNs and the zeroes. */
#define FD_TWO        0x4000000000000000ull   /* 2.0                        */
#define FD_THREE      0x4008000000000000ull   /* 3.0                        */
#define FD_FOUR       0x4010000000000000ull   /* 4.0                        */
#define FD_ONE_HALF   0x3ff8000000000000ull   /* 1.5                        */
#define FD_PI         0x400921fb54442d18ull   /* an arbitrary odd normal    */

/* 2^1000 — the divisor that drives a normal dividend into the subnormal
 * result range without either operand being a special. Biased exponent
 * 1000 + 1023 = 2023 = 0x7E7. */
#define FD_POW2_1000  0x7e70000000000000ull

/* THE TWO TIE ROWS ARE A CONSTRUCTION, NOT A SEARCH, and both quotients are
 * SUBNORMAL — see this file's note (a). Divided by 2^1000 each numerator
 * becomes an odd multiple of 2^-1075, i.e. exactly half way between two
 * neighbouring subnormals, so `grs == 4` by construction and the direction is
 * the parity of the retained significand.
 *   5 * 2^-75 / 2^1000 = 5 * 2^-1075 = 2.5 * 2^-1074 -> ties DOWN to 2
 *   7 * 2^-75 / 2^1000 = 7 * 2^-1075 = 3.5 * 2^-1074 -> ties UP   to 4
 * 5*2^-75 is 1.25 * 2^-73 (biased 950 = 0x3B6, fraction 0x4000000000000) and
 * 7*2^-75 is 1.75 * 2^-73 (fraction 0xC000000000000). */
#define FD_TIEDN_A    0x3b64000000000000ull
#define FD_TIEUP_A    0x3b6c000000000000ull

/* `mant_overflow` ON THE ONE PATH DIVISION CAN REACH IT. (2^53 - 1) * 2^-75
 * over 2^1000 is (2^53 - 1) * 2^-1075 = (2^52 - 0.5) * 2^-1074 — exactly half
 * way between the LARGEST SUBNORMAL and the SMALLEST NORMAL. Ties-to-even
 * takes the even neighbour, so the 52-bit fraction is all ones, rounds up,
 * `frac_rounded == IMPLICIT` fires and the exponent is bumped from 0 to 1:
 * the result is MIN_NORMAL. (2^53-1)*2^-75 is 1.111...1 * 2^-23, biased
 * 1000 = 0x3E8 with the fraction all ones. */
#define FD_MANTOVF_A  0x3e8fffffffffffffull

/* `flush_to_zero`: `shift_sub >= 56` (softfloat_common.jl:177). 2^-1000 over
 * 2^100 is 2^-1100, far below half the smallest subnormal. Biased exponents
 * -1000 + 1023 = 23 = 0x017 and 100 + 1023 = 1123 = 0x463. */
#define FD_POW2_M1000 0x0170000000000000ull
#define FD_POW2_100   0x4630000000000000ull

/* A STICKY BIT THAT IS SET AND **LOAD-BEARING**, WHICH "AN INEXACT QUOTIENT"
 * IS NOT (bd m33-fadd-fsub-what-a-consumer-must-know already says the same
 * thing for K15, in exactly those words). ADDED 2026-09-19 AFTER A MUTANT
 * SURVIVED L1: pinning `sticky` to 0 left every one of the 167 L1 cases GREEN
 * and was seen only by `l4_goldens` — which is the detector K11's finding says
 * is not durable, since `CQOPS_UPDATE_GOLDENS=1` blesses it.
 *
 * `_sf_round_and_pack` reads `grs = g<<2 | r<<1 | s` and rounds up on
 * `grs > 4` or on `grs == 4 & frac odd` (softfloat_common.jl:209-210), so with
 * `q`'s bit 0 clear the two readings differ EXACTLY when `g == 1`, `r == 0`
 * and `frac` is EVEN: 5 rounds up, 4 is a tie that an even `frac` sends down.
 * 1.0/3.0 is inexact and does NOT discriminate — its retained LSB is odd, so
 * both readings round the same way.
 *
 * The pair below was found by an INDEPENDENT integer model of fdiv.jl:91-98 —
 * the 56-iteration loop over Python ints, checking `q`'s low four bits and
 * `r != 0` — and then written down as the patterns it printed, not computed
 * here. `ma >= mb` so `need_shift` is false and the alignment reaching
 * round-and-pack is `q` itself. Host: 0x3ff5511b4d5b3c17, one ulp above what
 * the sticky-dropped reading gives. */
#define FD_STICKY_A   0x3ffd333d0241c32bull
#define FD_STICKY_B   0x3ff5eac5652ef033ull

/* THE ROWS, EACH NAMING THE fdiv.jl PREDICATE IT DISCRIMINATES. An anchor list
 * whose rows are not traceable to the source is one nobody can audit for an
 * omission, and an omitted anchor fails by not existing. */
static const struct { uint64_t a, b; } FDIV_PAIRS[] = {
    /* `b_zero & !a_zero` at :132 — x/0 is inf_result, which carries
     * `result_sign`, so BOTH operand signs matter and all three rows are
     * needed. The generic table has ±0 against ±0 and nothing against a
     * finite. */
    { CQ_F64_ONE,            CQ_F64_POS_ZERO       },   /* +Inf            */
    { CQ_F64_NEG(CQ_F64_ONE), CQ_F64_POS_ZERO      },   /* -Inf            */
    { CQ_F64_ONE,            CQ_F64_NEG_ZERO       },   /* -Inf            */

    /* `a_zero & !b_zero` at :131 — 0/x is zero_result, also signed. */
    { CQ_F64_NEG_ZERO,       FD_TWO                },   /* -0              */

    /* `_sf_normalize_to_bit52`'s `m_zero` guard (softfloat_common.jl:69-74),
     * post-Bennett-tpg0: a zero operand reaches the ladder and its result is
     * then discarded by the select chain — PRD-v2 §7.6's discarded arm, one
     * helper down. */
    { CQ_F64_POS_ZERO,       CQ_F64_ONE            },   /* +0              */

    /* `a_inf & !b_inf` at :134 and `b_inf & !a_inf` at :135, each with a SIGN
     * FLIP so the row also pins `result_sign = sa ^ sb` on the Inf paths. The
     * generic table's two Inf-against-finite rows are both all-positive. */
    { CQ_F64_POS_INF,        CQ_F64_NEG(FD_TWO)    },   /* -Inf            */
    { CQ_F64_NEG(FD_THREE),  CQ_F64_POS_INF        },   /* -0              */

    /* THE TWO ROWS THAT DETECT A DROPPED `_sf_normalize_to_bit52` (K17.md
     * §5.5, Bennett-r6e3). Without the pre-normalisation the 56-iteration loop
     * overflows and "the result's low 52 bits zero out" (fdiv.jl:71-77), which
     * looks like dead code on every NORMAL operand — that is, on every operand
     * a random draw produces. One row per operand position, because the fix
     * prepends the helper to BOTH `ma` and `mb`. */
    { CQ_F64_MIN_SUBNORMAL,  CQ_F64_ONE            },   /* = the operand   */
    { CQ_F64_ONE,            CQ_F64_MAX_SUBNORMAL  },   /* a large finite  */

    /* `need_shift = (wr >> 55) == 0` at :106, which is `ma < mb` on the
     * SIGNIFICANDS and not on the values — iteration 0's `fits` IS bit 55 of
     * `q`. A MATCHED PAIR, and the only instrument for getting the polarity
     * backwards, which is right on half the inputs. K17.md §5.10's rows 15-16
     * name (1.0, 2.0) and (4.0, 2.0); both have ma == mb and are therefore
     * BOTH the need_shift-FALSE side. */
    { CQ_F64_ONE,            FD_ONE_HALF           },   /* ma <  mb: TRUE  */
    { FD_ONE_HALF,           CQ_F64_ONE            },   /* ma >= mb: FALSE */

    /* The sticky bit at :101, in each direction: an exact quotient leaves
     * `r == 0` after 56 iterations and an inexact one does not. Dropping the
     * sticky is invisible on every exact row. */
    { CQ_F64_ONE,            FD_TWO                },   /* exact: 0.5      */
    { CQ_F64_ONE,            FD_THREE              },   /* inexact         */
    { FD_STICKY_A,           FD_STICKY_B           },   /* inexact AND the
                                                         * sticky DECIDES   */

    /* Rounding: the two tie directions and the carry out of the significand.
     * See this file's note (a) on why all three are subnormal quotients. */
    { FD_TIEDN_A,            FD_POW2_1000          },   /* ties DOWN to 2  */
    { FD_TIEUP_A,            FD_POW2_1000          },   /* ties UP   to 4  */
    { FD_MANTOVF_A,          FD_POW2_1000          },   /* -> MIN_NORMAL   */

    /* `subnormal` at :114 without `flush_to_zero`, and the neighbour that is
     * exactly half the smallest subnormal and so ties to even — i.e. to +0. */
    { CQ_F64_MAX_SUBNORMAL,  FD_TWO                },   /* a subnormal     */
    { CQ_F64_MIN_SUBNORMAL,  FD_TWO                },   /* +0 by RTNE      */

    /* `flush_to_zero` at :114 — `shift_sub >= 56` — at both signs, because
     * `flushed_result` is `result_sign << 63` and a port that dropped the sign
     * is right on half its inputs. */
    { FD_POW2_M1000,         FD_POW2_100           },   /* +0              */
    { CQ_F64_NEG(FD_POW2_M1000), FD_POW2_100       },   /* -0              */

    /* `exp_overflow` at :122 — and §7.12's "largest / smallest", which for a
     * division is the same row. */
    { CQ_F64_MAX,            CQ_F64_MIN_SUBNORMAL  },   /* +Inf            */

    /* The three identities a reader checks first. If these fail nothing
     * further down the list means anything. */
    { CQ_F64_ONE,            CQ_F64_ONE            },   /* 1/1 = 1         */
    { FD_PI,                 CQ_F64_ONE            },   /* x/1 = x         */
    { FD_PI,                 FD_PI                 },   /* x/x = 1         */

    /* ALL FOUR SIGN COMBINATIONS ON ONE MAGNITUDE PAIR, so the only thing
     * varying is `result_sign = sa ^ sb` (:54). A port that read `sa & sb` or
     * `sa | sb` agrees on two of the four. */
    { FD_TWO,                FD_FOUR               },
    { CQ_F64_NEG(FD_TWO),    FD_FOUR               },
    { FD_TWO,                CQ_F64_NEG(FD_FOUR)   },
    { CQ_F64_NEG(FD_TWO),    CQ_F64_NEG(FD_FOUR)   }
};

enum { N_FDIV_PAIRS = (int)(sizeof FDIV_PAIRS / sizeof FDIV_PAIRS[0]) };

/* Pinned so a row lost to an editing accident breaks the BUILD rather than
 * quietly shrinking the forced set. */
_Static_assert(N_FDIV_PAIRS == 29,
               "K17.md §5.10's fdiv-specific anchor list, after subtracting "
               "the eleven rows fpanchors.c already carries, dropping the one "
               "cell division cannot reach and ADDING the load-bearing-sticky "
               "row a surviving mutant asked for, is 29 ordered pairs; adding "
               "or removing one is a deliberate act");

/* The two-call contract of kerneldrv.h, with the generic rows first so their
 * indices are stable and a composing provider could offset past both. IT
 * OVERWRITES, IT DOES NOT FILL: the sampler draws a value per operand first
 * and this provider replaces v[0] and v[1] only. */
static int fdiv_anchors(int W, int i, cq_ref_w *v)
{
    int base = cq_fp_anchors_binary_count();

    if (W != CQ_FP64_W) return 0;              /* f64 only, as the generic is */
    if (i < 0)          return base + N_FDIV_PAIRS;
    if (i < base)       return cq_fp_anchors_binary(W, i, v);
    if (i >= base + N_FDIV_PAIRS || v == NULL) return 0;

    v[0] = cq_ref_w_make(FDIV_PAIRS[i - base].a, 0u, CQ_FP64_W);
    v[1] = cq_ref_w_make(FDIV_PAIRS[i - base].b, 0u, CQ_FP64_W);
    return 1;
}
