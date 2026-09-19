/* tests/test_kernel_fma_anchors.inc — PRD-v2 §7.12's anchor list for `fma`,
 * refined against `soft_fma`'s own named predicates, as a provider COMPOSING
 * tests/support/fpanchors.c's generic binary one. Included by
 * test_kernel_fma.c BEFORE the shape function that installs it.
 *
 * THIS PROVIDER WRITES `v[2]` ON EVERY ROW, AND THAT IS THE WHOLE REASON IT
 * EXISTS RATHER THAN A BARE `cq_fp_anchors_binary` (bd memory
 * anchors-live-on-the-shape-and-a-provider-overwrites). The sampler OVERWRITES
 * the operands a row NAMES over a tuple it has ALREADY DRAWN, so an unnamed
 * third operand stays RANDOM — which is correct behaviour and is exactly what
 * a three-source fp kernel must not rely on: every one of `soft_fma`'s
 * interesting branches is about the RELATION between `a*b` and `c`
 * (`same_sign`, `expDiff_neg`, `use_short_shift`, `underflow`,
 * `complete_cancel`, `hi_zero`, `inf_clash`), and a random `c` reaches none of
 * them. `M39's fma is the first three-source fp kernel and would hit it on day
 * one` is that memory's own sentence.
 *
 * SO THE GENERIC ROWS GET A ROTATING `c` rather than a constant one. A fixed
 * `c = +0` would turn all twenty-five of them into `fmul` rows and leave
 * `c_nan`, `c_inf` and `c_zero` unreached by the generic block entirely; the
 * rotation walks ±0, ±Inf, a payload NaN and two ordinary normals, so each of
 * the three class predicates on `c` is true on some generic row whatever the
 * generic table holds.
 *
 * NO ARITHMETIC, ONLY LITERALS, for fpanchors.c's reason: a constant computed
 * as `1.0 / 0.0` would be a host probe rather than a table, and this file must
 * read the same on every host. Every constructed row below is stated as the
 * PREDICATE it satisfies and is verified BY EXECUTION in
 * `the_classical_row_agrees_with_the_oracle_on_every_anchor`, against the host
 * `fma()` — which IEEE fully specifies wherever these rows are not NaN rows.
 * K20.md §1.1a states its anchor 26's expected `0x3970000000000000` as a
 * HAND-DERIVED figure; this file does not repeat it, and
 * `the_single_rounding_path_is_not_a_multiply_then_an_add` discharges it by
 * running the reference instead.
 */

/* Values this file needs that fpanchors.h does not name. Everything else comes
 * from there, so a reader checks a row against that file rather than against a
 * second copy of the NaNs and the zeroes. */
#define FA_TWO        0x4000000000000000ull   /* 2.0                        */
#define FA_THREE      0x4008000000000000ull   /* 3.0                        */
#define FA_FOUR       0x4010000000000000ull   /* 4.0                        */
#define FA_SIX        0x4018000000000000ull   /* 6.0                        */
#define FA_PI         0x400921fb54442d18ull   /* an arbitrary odd normal    */
#define FA_1E300      0x7e37e43c8800759cull   /* 1e300                      */
#define FA_1EM300     0x01a56e1fc2f8f359ull   /* 1e-300                     */
#define FA_1EM30      0x3a0c6f7a0b5ed8d3ull   /* 1e-30                      */

/* THE KAHAN SINGLE-ROUNDING WITNESS, and it is the one row that separates an
 * `fma` from a `fmul` followed by an `fadd`. `x = 1 + 2^-52`, so the EXACT
 * product `x*x = 1 + 2^-51 + 2^-104`; the ROUNDED product is `1 + 2^-51`
 * (`FA_X2_RND`), because `2^-104` is far below that value's ulp. A single
 * rounding therefore returns the residue `2^-104`; a double rounding returns
 * `+0`. `fma.jl:19-21` names this case by name.  */
#define FA_X          0x3ff0000000000001ull   /* 1 + 2^-52                  */
#define FA_X2_RND     0x3ff0000000000002ull   /* fl(x*x) = 1 + 2^-51        */
#define FA_NEG_X2_RND 0xbff0000000000002ull   /* -fl(x*x)                   */

/* TIE-TO-EVEN IN BOTH DIRECTIONS, and the construction is fmul's rather than a
 * search (K16's anchors): both operands are odd integers below 2^27, so the
 * exact product is an odd integer of 54 bits — the retained significand is its
 * top 53 and the guard bit is its low one, with NOTHING below, i.e. `grs == 4`
 * by construction. With `c = +0` the `fma` frame preserves that exactly, so
 * which way it rounds is bit 1 of the product, i.e. the parity of the retained
 * significand.
 *   67108867 x 134217723 = 9007199321849841, bit 1 clear -> rounds DOWN
 *   67108865 x 134217727 = 9007199321849855, bit 1 set   -> rounds UP       */
#define FA_TIEDN_A    0x419000000c000000ull   /* 67108867.0                 */
#define FA_TIEDN_B    0x419fffffec000000ull   /* 134217723.0                */
#define FA_TIEUP_A    0x4190000004000000ull   /* 67108865.0                 */
#define FA_TIEUP_B    0x419ffffffc000000ull   /* 134217727.0                */

/* A THIRD DISTINGUISHABLE qNaN, so the three-operand a > b > c precedence of
 * `_sf_propagate_nan3` (softfloat_common.jl:32-35) is observable rather than a
 * coincidence. Two NaNs cannot tell a two-operand rule from a three-operand
 * one; three can. */
#define FA_QNAN_C     0x7ff8000000000003ull

/* The rotating `c` for the twenty-five generic binary rows. Six entries, so
 * the cycle is coprime with nothing in particular and every one of `c_zero`,
 * `c_inf` and `c_nan` lands on several rows. */
static const uint64_t FA_GENERIC_C[] = {
    CQ_F64_POS_ZERO, CQ_F64_NEG_ZERO, CQ_F64_POS_INF,
    CQ_F64_QNAN_B,   FA_PI,           CQ_F64_NEG_INF
};
enum { N_FA_GENERIC_C = (int)(sizeof FA_GENERIC_C / sizeof FA_GENERIC_C[0]) };

/* `fma`'s OWN rows: one per named predicate of `soft_fma` that the generic
 * table and its rotating `c` cannot reach. Each carries the predicate it makes
 * true, because that is what a reader checks. */
static const struct { uint64_t a, b, c; const char *why; } FA_TRIPLES[] = {
    /* :19-21 — the defining property. A `fmul` then an `fadd` returns +0. */
    { FA_X, FA_X, FA_NEG_X2_RND,      "single rounding (Kahan residue)" },
    /* :137 — opposite signs, result exactly zero.                        */
    { FA_TWO, FA_THREE, 0xc018000000000000ull, "complete_cancel" },
    /* :125 — opposite-sign subtraction of the larger from the smaller.   */
    { CQ_F64_ONE, CQ_F64_ONE, 0xc000000000000000ull, "underflow" },
    /* :101 — expDiff == -1 AND opposite signs: Berkeley's one-bit
     * precision-preserving special, the arm that is DISCARDED on every
     * other input.                                                        */
    { CQ_F64_ONE, CQ_F64_ONE, 0xc010000000000000ull, "use_short_shift" },
    /* :92 — expDiff_neg, c dominating the frame by a wide margin.        */
    { CQ_F64_ONE, CQ_F64_ONE, FA_1E300,  "expDiff_neg (c dominates)" },
    /* :92 — expDiff strongly positive, the product dominating.           */
    { FA_1E300, CQ_F64_ONE, CQ_F64_ONE,  "expDiff > 0 (prod dominates)" },
    /* :57 — prod_is_inf & c_inf & sign_prod != sc.                       */
    { CQ_F64_POS_INF, CQ_F64_ONE, CQ_F64_NEG_INF, "inf_clash -> INDEF" },
    /* :56 — Inf x 0, whichever way round.                                */
    { CQ_F64_POS_INF, CQ_F64_POS_ZERO, CQ_F64_ONE, "inf_times_zero" },
    { CQ_F64_NEG_ZERO, CQ_F64_NEG_INF, CQ_F64_ONE, "inf_times_zero (swapped)" },
    /* :204 — c_inf & !prod_is_inf: c passes straight through.            */
    { CQ_F64_ONE, CQ_F64_ONE, CQ_F64_NEG_INF, "c_inf, finite product" },
    /* :205 — prod_is_inf and c finite.                                   */
    { CQ_F64_NEG_INF, FA_TWO, CQ_F64_ONE, "prod_is_inf, finite c" },
    /* :195-196 — the signed-zero combine, both rows, which differ.       */
    { CQ_F64_NEG_ZERO, CQ_F64_POS_ZERO, CQ_F64_POS_ZERO, "-0 * +0 + +0 = +0" },
    { CQ_F64_NEG_ZERO, CQ_F64_POS_ZERO, CQ_F64_NEG_ZERO, "-0 * +0 + -0 = -0" },
    { CQ_F64_POS_ZERO, CQ_F64_POS_ZERO, FA_PI, "prod_is_zero, c non-zero" },
    /* :208 — the three-operand a > b > c precedence, one row each.       */
    { CQ_F64_QNAN_A, CQ_F64_QNAN_B, FA_QNAN_C, "nan precedence: a wins" },
    { CQ_F64_ONE, CQ_F64_QNAN_B, FA_QNAN_C, "nan precedence: b wins" },
    { CQ_F64_ONE, FA_TWO, FA_QNAN_C,        "nan precedence: c wins" },
    { CQ_F64_SNAN, CQ_F64_QNAN_B, FA_QNAN_C,    "sNaN is quietened, a wins" },
    /* M32's round-and-pack: overflow to Inf, and the subnormal band.     */
    { FA_1E300, FA_1E300, CQ_F64_POS_ZERO,      "exp_overflow -> Inf" },
    { FA_1EM300, FA_1EM30, CQ_F64_POS_ZERO,     "subnormal result" },
    { CQ_F64_MIN_SUBNORMAL, CQ_F64_ONE, CQ_F64_MIN_SUBNORMAL,
                                                "subnormal + subnormal" },
    { FA_1EM300, FA_1EM300, CQ_F64_POS_ZERO,    "flush to zero" },
    /* The two ties, with `c = +0` so the frame is the product's exactly. */
    { FA_TIEDN_A, FA_TIEDN_B, CQ_F64_POS_ZERO,  "tie to even, rounds DOWN" },
    { FA_TIEUP_A, FA_TIEUP_B, CQ_F64_POS_ZERO,  "tie to even, rounds UP" },
    /* :146-149 — stage A: wr_hi == 0 after a near-total cancellation, so
     * the low limb is folded up and expR drops by 64.                     */
    { FA_X, FA_X, 0xbff0000000000001ull,        "hi_zero (stage A)" },
    /* :161-165 — stage C: a same-sign carry out of bit 61.               */
    { CQ_F64_MAX, CQ_F64_ONE, CQ_F64_MAX, "stage C carry" },
    /* The subnormal operands the three `_sf_normalize_to_bit52` calls
     * exist for: each is pre-normalised before the product.               */
    { CQ_F64_MIN_SUBNORMAL, CQ_F64_MAX_SUBNORMAL, CQ_F64_MIN_NORMAL,
                                                "three subnormal operands" },
    /* An ordinary odd normal triple, so the whole path runs with nothing
     * special true anywhere — the row every branch predicate is FALSE on. */
    { FA_PI, FA_SIX, FA_FOUR,                   "no predicate true" }
};
enum { N_FA_TRIPLES = (int)(sizeof FA_TRIPLES / sizeof FA_TRIPLES[0]) };

/* The composed provider. `cq_fp_anchors_binary` fills `v[0..1]` and this adds
 * `v[2]`; K20's own rows fill all three. Slot 2 is written on EVERY row, which
 * is this file's contract. */
static int fma_anchors(int W, int i, cq_ref_w *v)
{
    const int g = cq_fp_anchors_binary_count();

    if (W != 64) return 0;
    if (i < 0) return g + N_FA_TRIPLES;
    if (i >= g + N_FA_TRIPLES || v == NULL) return 0;

    if (i < g) {
        if (!cq_fp_anchors_binary(W, i, v)) return 0;
        v[2] = cq_ref_w_make(FA_GENERIC_C[i % N_FA_GENERIC_C], 0u, 64);
        return 1;
    }
    v[0] = cq_ref_w_make(FA_TRIPLES[i - g].a, 0u, 64);
    v[1] = cq_ref_w_make(FA_TRIPLES[i - g].b, 0u, 64);
    v[2] = cq_ref_w_make(FA_TRIPLES[i - g].c, 0u, 64);
    return 1;
}
