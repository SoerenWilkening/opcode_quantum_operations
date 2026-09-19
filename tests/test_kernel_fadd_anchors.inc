/* tests/test_kernel_fadd_anchors.inc — §7.12's anchor list for `fadd`/`fsub`,
 * as a provider COMPOSING tests/support/fpanchors.c's generic binary one.
 * Included by test_kernel_fadd.c BEFORE the shape function that installs it.
 *
 * WHY A PER-KERNEL PROVIDER AT ALL. fpanchors.h says it outright: the generic
 * table "names no per-kernel special case — the `a_nan` / `a_inf` / `a_zero` /
 * `swap` / `d >= 56` predicates of a particular Julia source are that kernel's
 * own provider's job, composing this one". §7.12 asks for one anchor per named
 * predicate, "because each is a branch of the `ifelse` tree that a random draw
 * of 32 reaches with no guarantee", and `soft_fadd` names sixteen.
 *
 * WHAT THE GENERIC TWENTY-FIVE ALREADY CARRY, so nothing below repeats them:
 * ±0 in all four combinations (K15.md anchors 9-10), ±Inf in all four
 * (anchors 5-6, which is `Inf - Inf` -> INDEF), Inf against a finite both ways
 * (7-8), the default NaN both ways (1-2), two payload NaNs both ways (3), an
 * sNaN against a qNaN with DISTINGUISHABLE payloads both ways (4), the largest
 * and smallest subnormal (15), the smallest normal against each (the
 * subnormal->normal carry-out, 29), the two tie-to-even witnesses
 * `(1, half-ulp)` and `(1+ulp, half-ulp)` (24-25), overflow to Inf via
 * `(MAX, MAX)` (27) and two exact cancellations (20).
 *
 * IT OVERWRITES, IT DOES NOT FILL (kerneldrv.h). The sampler draws a value per
 * operand first and this provider replaces v[0] and v[1]; a third operand, if
 * a future kernel composed this provider, would stay drawn rather than pinned
 * at zero — the recorded cq_kd_case2 trap.
 *
 * NO ARITHMETIC, ONLY LITERALS, for fpanchors.c's reason: a constant computed
 * as `1.0 / 0.0` would be a host probe rather than a table, and this file must
 * read the same on every host.
 */

/* The three exponent fields the `d` rows below are built from. `1.0` has a
 * biased exponent of 0x3FF, so a second operand at 0x3FF - k gives `d == k`
 * exactly — which is how the DISCARDED-arm boundary (:81's `d >= 56`) and the
 * D8 boundary (:76's `d >= 64`, where our barrel would shift by `d mod 64`)
 * are reached as NAMED values rather than hoped for. */
#define FA_TWO        0x4000000000000000ull   /* 2.0,  e = 0x400            */
#define FA_HALF       0x3fe0000000000000ull   /* 0.5,  e = 0x3FE -> d == 1  */
#define FA_D56        0x3c70000000000000ull   /* e = 0x3FF - 56             */
#define FA_D64        0x3bf0000000000000ull   /* e = 0x3FF - 64             */
#define FA_STICKY     0x3ca0000000000001ull   /* half-ulp of 1.0, LSB set   */
#define FA_NEAR_TWO   0x3fffffffffffffffull   /* 2 - 1ulp, odd significand  */
#define FA_JUST_BELOW 0xbfefffffffffffffull   /* -(1 - half an ulp)         */
#define FA_TWO_PLUS   0x4000000000000001ull   /* 2 + 1ulp, e == 0x400       */

/* THE ROWS, EACH NAMING THE fadd.jl PREDICATE IT DISCRIMINATES. An anchor list
 * whose rows are not traceable to the source is one nobody can audit for an
 * omission, and an omitted anchor fails by not existing. */
static const struct { uint64_t a, b; } FADD_PAIRS[] = {
    /* `swap = a_mag < b_mag` (:49) TRUE, with the two operands' SIGNS
     * DIFFERENT — the only instrument for rows 26-31's arm order, which emits
     * an identical tuple either way, keeps the palindrome and leaves scratch
     * clean. With `sa_ord` taken from the wrong operand `1.0 - 2.0` comes back
     * as `+1.0`. Both directions, because one alone passes under a transposed
     * pair the other way. */
    { CQ_F64_ONE,      CQ_F64_NEG(FA_TWO)  },
    { CQ_F64_NEG(FA_TWO), CQ_F64_ONE       },

    /* `d == 0` (:82's false arm) AND `overflow` (:103, the carry into bit 56). */
    { CQ_F64_ONE,      CQ_F64_ONE          },

    /* `d == 1` — the smallest shifting alignment, and `0 < d < 56` (:82). */
    { CQ_F64_ONE,      FA_HALF             },

    /* THE STICKY BIT (:78), SET AND LOAD-BEARING. `wb`'s low bits survive the
     * `d = 53` shift only through `sticky`, and they move `grs` from 100 (an
     * even tie, rounds DOWN) to 101 (rounds UP): with the `| sticky` of :79
     * dropped, `1.0 + FA_STICKY` returns 1.0 instead of its neighbour. Both
     * significand parities, because the tie-break reads `frac & 1`. */
    { CQ_F64_ONE,          FA_STICKY       },
    { CQ_F64_ONE_PLUS_ULP, FA_STICKY       },

    /* `mant_overflow` (softfloat_common.jl:213) — the round-up carries out of
     * the significand and bumps the exponent, `2 - 1ulp` rounding to `2.0`. */
    { FA_NEAR_TWO,     CQ_F64_HALF_ULP_OF_1 },

    /* `d >= 56` (:81), the DISCARDED arm's boundary: EXACTLY 56, where
     * `wb_large` takes over from `wb_mid`. */
    { CQ_F64_ONE,      FA_D56              },

    /* `d == 64` EXACTLY — the D8 boundary. Our barrel masks the amount to six
     * lanes, so it shifts by `64 mod 64 == 0` where Julia gives 0; the answer
     * agrees only because :81 selects `wb_large` here. If that select is
     * mis-wired the divergence goes live, and this row plus the next are its
     * only detectors (K15.md §6.5 risk 2). */
    { CQ_F64_ONE,      FA_D64              },

    /* `d` well past 64 (the smallest subnormal against 1.0), same reason. */
    { CQ_F64_ONE,      CQ_F64_MIN_SUBNORMAL },

    /* One-bit cancellation: `!same_sign` with a single `need1` CLZ stage. */
    { CQ_F64_ONE,      CQ_F64_NEG(FA_HALF) },

    /* DEEP CLZ — `1.0 - (1 - half an ulp)` cancels 52 bits, so several of
     * `_sf_normalize_clz`'s six stages fire and `result_exp` goes far
     * negative, which is also the only route to M32's SIGNED compares being
     * exercised with a negative operand from this kernel. */
    { CQ_F64_ONE,      FA_JUST_BELOW       },

    /* `d == 0` with OPPOSITE signs and unequal magnitudes: no exact cancel, a
     * deep normalisation, and `swap` FALSE. */
    { FA_TWO_PLUS,     CQ_F64_NEG(FA_TWO)  },

    /* `ea_ord == 0` on BOTH operands (:59-64): two subnormals, no implicit
     * bit, effective exponent substituted to 1. */
    { CQ_F64_MIN_SUBNORMAL, CQ_F64_MIN_SUBNORMAL },

    /* UNDERFLOW TO A SUBNORMAL: the smallest normal minus the smallest
     * subnormal is the largest subnormal, so `subnormal` (:175) is true and
     * `flush_to_zero` is not. */
    { CQ_F64_MIN_NORMAL, CQ_F64_NEG(CQ_F64_MIN_SUBNORMAL) },

    /* `Bennett-m63k`, POSITIVE payload NaN as `b`. For `fsub` the answer is
     * that NaN quietened WITH ITS OWN SIGN; `fadd(a, fneg(b))` would return
     * it with the sign flipped. */
    { CQ_F64_ONE,      CQ_F64_QNAN_A       },

    /* `Bennett-m63k`, NEGATIVE payload NaN as `b` — THE discriminating row.
     * The two spellings of `fsub` differ in exactly this cell, and the
     * generic table's NaN rows cannot see it: `CQ_F64_DEFAULT_NAN` is already
     * negative in the `a` position and the payload pair is symmetric. */
    { CQ_F64_ONE,      CQ_F64_NEG(CQ_F64_QNAN_A) },

    /* `fsub`'s ordinary sign flip — `!b_is_nan`, the arm that DOES call
     * `soft_fneg`, with an answer no special case can produce. */
    { FA_TWO,          CQ_F64_ONE          }
};

enum { N_FADD_PAIRS = (int)(sizeof FADD_PAIRS / sizeof FADD_PAIRS[0]) };

/* Pinned so a row lost to an editing accident breaks the BUILD rather than
 * quietly shrinking the forced set. */
_Static_assert(N_FADD_PAIRS == 18,
               "K15.md §5.x's fadd-specific anchor list, less the rows the "
               "generic twenty-five already carry, is 18 ordered pairs; adding "
               "or removing one is a deliberate act");

/* The two-call contract of kerneldrv.h, with the generic rows first so their
 * indices are stable and a composing provider could offset past both. */
static int fadd_anchors(int W, int i, cq_ref_w *v)
{
    int base = cq_fp_anchors_binary_count();

    if (W != CQ_FP64_W) return 0;              /* f64 only, as the generic is */
    if (i < 0)          return base + N_FADD_PAIRS;
    if (i < base)       return cq_fp_anchors_binary(W, i, v);
    if (i >= base + N_FADD_PAIRS || v == NULL) return 0;

    v[0] = cq_ref_w_make(FADD_PAIRS[i - base].a, 0u, CQ_FP64_W);
    v[1] = cq_ref_w_make(FADD_PAIRS[i - base].b, 0u, CQ_FP64_W);
    return 1;
}
