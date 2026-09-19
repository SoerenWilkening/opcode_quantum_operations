/* tests/test_kernel_fsqrt_anchors.inc — PRD-v2 §7.12's anchor list for
 * `fsqrt`, refined against `soft_fsqrt`'s own named predicates (K21.md §5.7),
 * as a provider COMPOSING tests/support/fpanchors.c's generic UNARY one.
 * Included by test_kernel_fsqrt.c BEFORE the shape function that installs it.
 *
 * WRITTEN BY SUBTRACTION FROM THE GENERIC FIFTEEN, which is what
 * `bd fp-anchors-the-generic-25-already-carry-most-of-them` says to do. The
 * singles in fpanchors.c already carry EIGHT of the rows K21.md §5.7 names:
 * +0 and -0 (its rows 1 and 2 — `sqrt(-0) = -0`, NOT INDEF), +Inf (row 3),
 * -Inf (row 4, the first INDEF), a negative finite (row 5, `-1.0`), the
 * default NaN — which is x86's INDEF and is therefore NEGATIVE, so it is
 * ALREADY the `:107-110` witness — two payload qNaNs and an sNaN (rows 6-8),
 * a NEGATIVE subnormal (the second INDEF row), the largest and smallest
 * subnormal (17, 18), the smallest normal (19), the largest finite (20) and
 * 1.0 (13). So what is left below is the source's own arithmetic branches.
 *
 * NO ARITHMETIC, ONLY LITERALS, for fpanchors.c's reason: a constant computed
 * as `1.0 / 0.0` would be a host probe rather than a table, and this file must
 * read the same on every host. Every EXPECTED value comes from the host
 * `sqrt()` with §7.4's cells pinned by table (test_kernel_fsqrt.c's
 * `ref_fsqrt`); no hand-computed result is ever executed as one.
 *
 * `grs` IS NAMED PER ROW AND IT WAS MEASURED INDEPENDENTLY. The three
 * discarded bits of the working value decide the rounding, and each row's
 * residue below was read off a throwaway probe that computes the exact
 * integer square root of the 112-bit radicand by BISECTION over `__int128` —
 * no digit recurrence anywhere, so it is an independent derivation and not a
 * reading of src/kernels/fsqrt*.c. That probe also measured, over 3,000,000
 * random normals, that `grs` takes only the values {1, 3, 5, 7} and that every
 * EVEN residue including 4 is unreachable — which is Kahan's no-midpoint
 * theorem (fsqrt.jl:11-15) discharged by execution rather than carried over as
 * an assumption. `the_working_value_handed_to_m32_never_ties` is the shipped
 * form of that claim.
 *
 * SO THERE IS NO TIE ANCHOR HERE AND THAT IS THE POINT, not an omission.
 * PRD-v2 §7.12 asks for "a tie-to-even case in each direction"; `soft_fsqrt`
 * has no input that produces one, in either direction, and neither does it
 * have an overflow or an underflow (fsqrt.jl:27-29). Rows 1-13 below are the
 * nearest reachable neighbours: both rounding directions, all four odd `grs`
 * residues, and the exact roots that make `grs` zero.
 */

/* --- The exponent-parity path (:57-59), which is K21's alone. ------------- */

/* `e_is_odd` TRUE and FALSE, above and below 1.0, with an EXACT root and an
 * inexact one on each side. `e_unb` is NEGATIVE for every value under 1.0 and
 * the shift at :60 is ARITHMETIC, so the four rows at 0.25, 0.5, 2^-100 and
 * 1 - 2^-53 are the only instrument for a logical shift substituted there:
 * they cost the same gates, keep the palindrome and leave scratch clean. */
#define FSQ_TWO       0x4000000000000000ull  /* 2.0     odd  e, grs 5 UP   */
#define FSQ_THREE     0x4008000000000000ull  /* 3.0     odd  e, grs 3 DOWN */
#define FSQ_FOUR      0x4010000000000000ull  /* 4.0     even e, EXACT      */
#define FSQ_NINE      0x4022000000000000ull  /* 9.0     ODD  e AND EXACT   */
#define FSQ_QUARTER   0x3fd0000000000000ull  /* 0.25   -even e, EXACT      */
#define FSQ_HALF      0x3fe0000000000000ull  /* 0.5    -odd  e, grs 5      */
#define FSQ_2P100     0x4630000000000000ull  /* 2^100   even e, EXACT      */
#define FSQ_2P101     0x4640000000000000ull  /* 2^101   odd  e, grs 5      */
#define FSQ_2PM100    0x39b0000000000000ull  /* 2^-100 -even e, EXACT      */

/* --- The (a_hi, a_lo) STREAM (:68-69, :82), and these are the rows that see
 * a dropped carry-in. `a_lo` holds bits 0..5 of `ma_adj` in lanes 58..63, so
 * an operand whose low SIX mantissa bits are zero has an all-zero `a_lo` and
 * `(a_hi << 2) | (a_lo >> 62)` is right even with the second disjunct
 * deleted. 2.0, 4.0, 9.0 and every power of two below are exactly that shape.
 * These two are not: 0.1's low six bits are 0x1A and pi's are 0x18. --------- */
#define FSQ_TENTH     0x3fb999999999999aull  /* 0.1    -even e, grs 1      */
#define FSQ_PI        0x400921fb54442d18ull  /* pi      odd  e, grs 3      */

/* --- The two neighbours of 1.0, which is the one input whose root's leading
 * 1 lands exactly at bit 55 with nothing below it. `1 - 2^-53` also has a FULL
 * mantissa on the odd-exponent path. ------------------------------------- */
#define FSQ_ONE_UP    0x3ff0000000000001ull  /* 1 + 2^-52,      grs 3      */
#define FSQ_ONE_DOWN  0x3fefffffffffffffull  /* 1 - 2^-53, odd, grs 3      */

/* --- `_sf_normalize_to_bit52`'s ladder (:49). The generic singles drive its
 * two ENDS — the largest subnormal shifts once and the smallest shifts
 * fifty-two — and this drives the MIDDLE, where four of the six binary-search
 * stages fire. A suite that tests only the ends has not tested the middle. -- */
#define FSQ_SUBNORM_MID 0x0000000100000000ull /* bit 32: a 20-place shift   */

/* --- The select chain's ORDER (:111-116). The generic default NaN is x86's
 * INDEF and so is already negative, quiet and payload-free; these two add a
 * negative NaN WITH a payload and a negative SIGNALLING NaN, where "a_nan
 * fires strictly last" and "the payload survives" are separable claims. A port
 * that ran the NaN row before the INDEF row returns 0xfff8000000000000 for
 * both and passes on the default-NaN row alone. --------------------------- */
#define FSQ_NEG_QNAN  0xfff8000000000001ull  /* -qNaN, payload 1           */
#define FSQ_NEG_SNAN  0xfff0000000000001ull  /* -sNaN -> -qNaN, payload 1  */

/* --- Two more INDEF rows, at the two ends of the negative finite range. --- */
#define FSQ_NEG_MAX     0xffefffffffffffffull /* the largest negative      */
#define FSQ_NEG_MAX_SUB 0x800fffffffffffffull /* the largest negative sub  */

static const uint64_t FSQRT_SINGLES[] = {
    FSQ_TWO, FSQ_THREE, FSQ_FOUR, FSQ_NINE,
    FSQ_QUARTER, FSQ_HALF, FSQ_2P100, FSQ_2P101, FSQ_2PM100,
    FSQ_TENTH, FSQ_PI,
    FSQ_ONE_UP, FSQ_ONE_DOWN,
    FSQ_SUBNORM_MID,
    FSQ_NEG_QNAN, FSQ_NEG_SNAN, FSQ_NEG_MAX, FSQ_NEG_MAX_SUB
};

enum { N_FSQRT_SINGLES = (int)(sizeof FSQRT_SINGLES / sizeof FSQRT_SINGLES[0]) };

/* Pinned so a row lost to an editing accident breaks the BUILD rather than
 * quietly shrinking the forced set. */
_Static_assert(N_FSQRT_SINGLES == 18,
               "K21.md §5.7's fsqrt-specific anchor list is 18 singles; "
               "adding or removing one is a deliberate act");

/* The two-call contract of kerneldrv.h, with the generic rows first so their
 * indices are stable and a composing provider could offset past both. IT
 * OVERWRITES, IT DOES NOT FILL: the sampler draws a value per operand first
 * and this provider replaces v[0] only, so a composing kernel's other operands
 * stay sampled rather than pinned at zero (the cq_kd_case2 trap). */
static int fsqrt_anchors(int W, int i, cq_ref_w *v)
{
    int base = cq_fp_anchors_unary_count();

    if (W != CQ_FP64_W) return 0;              /* f64 only, as the generic is */
    if (i < 0)          return base + N_FSQRT_SINGLES;
    if (i < base)       return cq_fp_anchors_unary(W, i, v);
    if (i >= base + N_FSQRT_SINGLES || v == NULL) return 0;

    v[0] = cq_ref_w_make(FSQRT_SINGLES[i - base], 0u, CQ_FP64_W);
    return 1;
}
