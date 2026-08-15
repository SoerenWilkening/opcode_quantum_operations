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

static void sweep_values(const cq_kd_spec *k, int W)
{
    cq_bk_pair pairs[MAX_PAIRS];
    uint32_t np = pairs_for(k, W, pairs, MAX_PAIRS);
    uint64_t mask = cq_ref_mask(W);
    uint64_t span = mask + 1u;
    uint64_t cases = 0;
    cq_bk_rng rng;

    cq_bk_rng_init(&rng, 0x5EEDC0DEull ^ (uint64_t)W);

    /* The all-quantum row is index 1 by construction. Asserted rather than
     * assumed: reordering cq_bk_fixed_pairs would otherwise spend the full
     * cross product on some other mask while still running 65,536 cases and
     * still looking like complete coverage.
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
                  "cq_kd_sweep: fixed pair 1 is [%s], not all-quantum — the "
                  "full cross product would be spent on the wrong mask",
                  pairs[1].name);

    for (uint64_t va = 0; va < span; va++)
        for (uint64_t vb = 0; vb < span; vb++) {
            cq_kd_case2(k, W, va, vb, &pairs[1]);
            cases++;
        }

    /* THE REST GET A RANDOMLY ASSIGNED MASK PER VALUE PAIR, and the assignment
     * is random rather than a rotation for a measured reason. `p = (p+1) % np`
     * looks uniform and is not: with np=20 and span=256, gcd(20,256) = 4, so
     * mask index p only ever meets value pairs with `vb ≡ p (mod 4)` — each
     * mask sees 64 of the 256 `vb` values, all in ONE residue class. A
     * lane-specific fold bug needing a particular low bit of `b` is
     * unreachable that way. A seeded xorshift has no such structure, and it is
     * still deterministic, so a red run reproduces. */
    for (uint64_t va = 0; va < span; va++)
        for (uint64_t vb = 0; vb < span; vb++) {
            cq_kd_case2(k, W, va, vb, &pairs[cq_bk_rng_below(&rng, np)]);
            cases++;
        }

    for (uint32_t q = 0; q < np; q++)
        for (int c = 0; c < 4; c++) {
            cq_kd_case2(k, W, (c & 1) ? mask : 0u, (c & 2) ? mask : 0u,
                        &pairs[q]);
            cases++;
        }

    printf("# %s W=%2d VALUE-EXHAUSTIVE (see kernelsweep.c on why this width "
           "is not the full cross product): %llu value pairs all-quantum + "
           "%llu at a seeded-random mask + %u masks x 4 corners = %llu cases\n",
           k->name, W, (unsigned long long)(span * span),
           (unsigned long long)(span * span), np, (unsigned long long)cases);
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
