/* tests/support/kernelsweep.c — which widths, which values, which masks.
 *
 * SPLIT FROM kerneldrv.c AT STEP 11 on plan §2.2's recorded seam. The four
 * LEVELS live next door; this file decides only what gets fed to them. The cut
 * pays off immediately: a cast's widths are a PAIR, so M13's suite drives
 * cq_kd_sweep_at directly rather than the standard ladder, and it needed no
 * change to a single assertion to do it.
 */

#include "support/kerneldrv.h"

#include "support/harness.h"
#include "support/refmodel.h"

#include <stdio.h>

/* FULL CROSS PRODUCT — every value pair against every mask pair. Plan §4 asks
 * for {1,2,4,8}; 3 and 5 are added because risk R8's measured witnesses are at
 * W=3 and W=5 ("{ZERO,Q}-only masks first fail at W=5"), and the plan's own R8
 * row says to put the witnesses in the fixed set rather than let a regression
 * depend on a random mask to be caught. */
static const int FULL_CROSS_W[] = { 1, 2, 3, 4, 5 };

/* W=8 IS EXHAUSTIVE OVER VALUES BUT NOT OVER THE PRODUCT, and this is the one
 * place the sweep is capped — stated here, and printed by every run, because a
 * cap nobody can see reads as coverage.
 *
 * The full product is 20 mask pairs x 65,536 value pairs x 3 kernels = 3.9M
 * cases, which measured 43 s in Debug against a 0.8 s suite. So at W=8: the
 * ALL-QUANTUM mask gets the full cross product on its own (it is the mask L4
 * pins and the one where no operand fold can hide a lane); every value pair
 * runs a SECOND time at a seeded-random mask, so plan §4's "all (a,b)" is met
 * literally, twice; and the four corners are crossed with every mask. Full
 * mask x value coverage is not lost, only moved: it is complete at W <= 5. */
static const int VALUE_EXHAUSTIVE_W[] = { 8 };

/* Sampled, because 2^32 value pairs at W=16 is already out of reach. */
static const int SAMPLED_W[] = { 16, 32, 64 };

enum { SAMPLES_PER_MASK = 64, MAX_PAIRS = 128 + 12 };

static uint32_t pairs_for(const cq_kd_spec *k, int W, cq_bk_pair *out,
                          uint32_t cap)
{
    cq_kd_shape sh;
    uint32_t np = cq_bk_fixed_pairs((uint32_t)W, out, cap);

    /* A shape may forbid quantum bits on an operand (K4's shift amount). Doing
     * it here rather than per case means the mask NAMES stay honest and the
     * all-classical row is still recognisably the all-classical row. */
    cq_kd_default_shape(W, &sh);
    if (k->shape) {
        cq_ref_w cls[CQ_KD_MAX_SRC];
        k->shape(W, &sh);
        for (int i = 0; i < CQ_KD_MAX_SRC; i++)
            cls[i] = cq_ref_w_make(sh.classical[i],
                                   sh.classical[i] ? ~0ull : 0ull, 128);
        for (uint32_t p = 0; p < np; p++) cq_bk_constrain(&out[p], cls, sh.n_src);
    }
    return np;
}

static void sweep_full_cross(const cq_kd_spec *k, int W)
{
    cq_bk_pair pairs[MAX_PAIRS];
    uint32_t np = pairs_for(k, W, pairs, MAX_PAIRS);
    uint64_t span = cq_ref_mask(W) + 1u;   /* W <= 5 here, so this cannot wrap */
    uint64_t cases = 0;

    for (uint32_t p = 0; p < np; p++)
        for (uint64_t va = 0; va < span; va++)
            for (uint64_t vb = 0; vb < span; vb++) {
                cq_kd_case2(k, W, va, vb, &pairs[p]);
                cases++;
            }

    printf("# %s W=%2d FULL CROSS: %u mask pairs x %llu value pairs = %llu "
           "cases\n", k->name, W, np, (unsigned long long)(span * span),
           (unsigned long long)cases);
    fflush(stdout);
}

/* THE STRUCTURED VALUE SET, and it REPLACED VALUE EXHAUSTION AT W=8 on
 * 2026-08-16. Read this before restoring the old loop.
 *
 * WHAT WAS THERE: every one of the 2^(2W) value pairs at the all-quantum mask,
 * then every one of them AGAIN at a seeded-random mask, then the four corners
 * at each mask — 131,152 cases per kernel at W=8, which was 63% of the entire
 * compare suite and about half of the add suite.
 *
 * WHY IT BOUGHT ALMOST NOTHING HERE, and the argument is specific to this
 * codebase rather than general test-design taste. The §3 fold table dispatches
 * on a bit's KIND and never on a qubit's value (D6, no demotion), and every
 * kernel is width-generic over `reg->width` with no width switch (I5, Rule 3).
 * So **under the all-quantum mask the emitted circuit is identical for every
 * one of those 65,536 pairs** — the first loop ran one fixed gate sequence
 * 65,536 times through the classical shadow. A fault that survives W<=5, where
 * the FULL cross product (every value pair x every mask pair) still runs, has
 * to be width-dependent: a loop bound, an MSB boundary, a carry that only
 * exists above some length. Those are found by covering WIDTHS and by the
 * structural checks — the closed-form gate counts, the palindrome, the peak —
 * not by more values at one width.
 *
 * WHERE VALUES DO REACH THE CIRCUIT: through classical lanes only, and only as
 * one bit of information per lane. A classical ZERO control folds its gate
 * away; a classical ONE rewrites it (CX->X, CCX->CX) and removes none. That is
 * the K09.md §3.3.1 rule, and it is exercised by all-zeros, all-ones, single
 * bits and alternating patterns — which are exactly what this set contains —
 * far more directly than by enumeration.
 *
 * SO THIS IS NOT ONLY CHEAPER, IT IS BROADER. Exhaustion spent its budget on
 * TWO masks (all-quantum, plus one random draw per pair). The structured set
 * is crossed with EVERY fixed mask pair, so the arithmetic corners now meet the
 * asymmetric masks risk R8 names, which no value pair ever did before.
 *
 * The classes, and why each is here:
 *   0 / max / msb            the masking and sign boundaries
 *   v, v         (equal)     the comparator's tie, the adder's doubling
 *   v, v+1 / v+1, v          the top-differing-bit boundary, both orders
 *   2^i and 2^i - 1          one lane hot; and a carry/borrow that propagates
 *                            exactly i positions, for every i
 *   0x55.. / 0xAA..          alternating, the mask-vs-value interaction
 * plus SAMPLES_PER_MASK seeded-random pairs, which is what covers the
 * combinations nobody thought to name. */
static uint32_t structured_pairs(int W, uint64_t *va, uint64_t *vb, uint32_t cap)
{
    uint64_t m = cq_ref_mask(W);
    uint64_t msb = (uint64_t)1 << (W - 1);
    uint32_t n = 0;

#define PUSH(x, y) do { if (n < cap) { va[n] = (x) & m; vb[n] = (y) & m; n++; } } while (0)
    PUSH(0u, 0u);       PUSH(0u, m);        PUSH(m, 0u);        PUSH(m, m);
    PUSH(msb, msb);     PUSH(msb, msb - 1); PUSH(msb - 1, msb); PUSH(msb, 0u);
    PUSH(m, 1u);        PUSH(1u, m);        PUSH(1u, 1u);       PUSH(0u, 1u);
    PUSH(0x5555555555555555ull, 0xAAAAAAAAAAAAAAAAull);
    PUSH(0xAAAAAAAAAAAAAAAAull, 0x5555555555555555ull);

    for (int i = 0; i < W; i++) {
        uint64_t bit = (uint64_t)1 << i;

        PUSH(bit, 0u);              /* one lane hot on a only            */
        PUSH(0u, bit);              /* and on b only                     */
        PUSH(bit, bit);             /* the same lane on both             */
        PUSH(bit - 1u, 1u);         /* carry/borrow propagating i places  */
        PUSH(bit, bit - 1u);        /* the ordering boundary at lane i    */
    }
#undef PUSH
    return n;
}

/* Structured + sampled, crossed with EVERY mask pair. Replaces the value
 * exhaustion described above; see structured_pairs for the argument, and
 * IMPLEMENTATION_PLAN §4 / PRD §11, which were corrected to match. */
static void sweep_values(const cq_kd_spec *k, int W)
{
    cq_bk_pair pairs[MAX_PAIRS];
    uint32_t np = pairs_for(k, W, pairs, MAX_PAIRS);
    uint64_t sa[8 * 64 + 32], sb[8 * 64 + 32];
    uint32_t ns = structured_pairs(W, sa, sb, (uint32_t)(sizeof sa / sizeof sa[0]));
    uint64_t mask = cq_ref_mask(W);
    uint64_t cases = 0;
    cq_bk_rng rng;

    cq_bk_rng_init(&rng, 0x5EEDC0DEull ^ (uint64_t)W);

    /* The all-quantum row is index 1 by construction. Asserted rather than
     * assumed: it is the mask L4 pins and the one where no operand fold can
     * hide a lane, so a reordering of cq_bk_fixed_pairs must not move it
     * silently.
     *
     * CHECKED ON OPERAND 0 ONLY, AND NOT SKIPPED FOR SHAPED KERNELS. An
     * earlier draft skipped the whole check whenever a spec set a shape, on
     * the premise that "for those kernels no mask is all-quantum" — which is
     * false for every Step-11 spec: a cast constrains nothing at all, and a
     * shift constrains only operand 1, so q[0] is untouched in both cases and
     * the check would have passed verbatim. The skip disabled the guard for
     * six specs and protected none of them. */
    if (!cq_ref_w_eq(pairs[1].q[0], cq_ref_w_ones(W)))
        cq_h_fail(__FILE__, __LINE__,
                  "cq_kd_sweep: fixed pair 1 is [%s], not all-quantum",
                  pairs[1].name);

    for (uint32_t p = 0; p < np; p++) {
        for (uint32_t s = 0; s < ns; s++) {
            cq_kd_case2(k, W, sa[s], sb[s], &pairs[p]);
            cases++;
        }

        /* Seeded, so a red run reproduces from the printed seed. Per mask
         * rather than per value pair: the old code drew a mask at random FOR
         * each pair, which left every mask meeting an unpredictable slice of
         * the value space. Every mask now meets the same named corners plus
         * its own random draw. */
        for (int s = 0; s < SAMPLES_PER_MASK; s++) {
            cq_kd_case2(k, W, cq_bk_rng_next(&rng) & mask,
                        cq_bk_rng_next(&rng) & mask, &pairs[p]);
            cases++;
        }
    }

    printf("# %s W=%2d STRUCTURED (%u named pairs + %d sampled) x %u mask "
           "pairs = %llu cases — NOT value-exhaustive; see kernelsweep.c on "
           "why exhaustion at this width bought nothing the W<=5 full cross "
           "does not already have\n",
           k->name, W, ns, SAMPLES_PER_MASK, np, (unsigned long long)cases);
    fflush(stdout);
}

/* Two random words masked to W. Above 64 bits this is the only sweep there is
 * — a cast from i128 has 2^128 values and the ladder's wide pairs are exactly
 * where the 64-bit seam falls inside a register. */
static cq_ref_w rnd_w(cq_bk_rng *r, int W)
{
    return cq_ref_w_make(cq_bk_rng_next(r), cq_bk_rng_next(r), W);
}

static void sweep_sampled(const cq_kd_spec *k, int W)
{
    cq_bk_pair pairs[MAX_PAIRS];
    uint32_t np = pairs_for(k, W, pairs, MAX_PAIRS);
    cq_bk_rng rng;
    cq_ref_w ones = cq_ref_w_ones(W);
    cq_ref_w zero = cq_ref_w_zero();
    uint64_t cases = 0;

    /* Seeded from the width so a failure at W=32 is reproducible and does not
     * depend on what ran before it. */
    cq_bk_rng_init(&rng, 0xC0FFEEull ^ (uint64_t)W);

    for (uint32_t p = 0; p < np; p++) {
        cq_ref_w v[CQ_KD_MAX_SRC];

        /* The corners first: they are where a masking bug lives, and a uniform
         * sample reaches 0 and all-ones with probability ~0 at W=64. */
        for (int c = 0; c < 4; c++) {
            v[0] = (c & 1) ? ones : zero;
            v[1] = (c & 2) ? ones : zero;
            v[2] = zero;
            cq_kd_case(k, W, v, &pairs[p]);
            cases++;
        }

        for (int s = 0; s < SAMPLES_PER_MASK; s++) {
            v[0] = rnd_w(&rng, W);
            v[1] = rnd_w(&rng, W);
            v[2] = rnd_w(&rng, W);
            cq_kd_case(k, W, v, &pairs[p]);
            cases++;
        }
    }

    printf("# %s W=%2d sampled: %u mask pairs x %d cases = %llu cases "
           "(seed 0xC0FFEE^W)\n", k->name, W, np, SAMPLES_PER_MASK + 4,
           (unsigned long long)cases);
    fflush(stdout);
}

void cq_kd_sweep_at(const cq_kd_spec *k, int W, int exhaustive)
{
    if (exhaustive && W <= 5)      sweep_full_cross(k, W);
    else if (exhaustive && W <= 8) sweep_values(k, W);
    else                           sweep_sampled(k, W);
}

void cq_kd_sweep(const cq_kd_spec *k)
{
    for (size_t i = 0; i < sizeof FULL_CROSS_W / sizeof FULL_CROSS_W[0]; i++)
        sweep_full_cross(k, FULL_CROSS_W[i]);

    for (size_t i = 0; i < sizeof VALUE_EXHAUSTIVE_W / sizeof VALUE_EXHAUSTIVE_W[0]; i++)
        sweep_values(k, VALUE_EXHAUSTIVE_W[i]);

    for (size_t i = 0; i < sizeof SAMPLED_W / sizeof SAMPLED_W[0]; i++)
        sweep_sampled(k, SAMPLED_W[i]);
}
