/* tests/support/kernelsweep.c — which widths, which masks, which values.
 *
 * SPLIT FROM kerneldrv.c AT STEP 11 on plan §2.2's recorded seam. The four
 * LEVELS live next door; this file decides only what gets fed to them.
 *
 * ================================================================
 * ONE BUDGET, AND IT IS A SMALL CONSTANT (2026-08-20).
 * ================================================================
 *
 * Every L1 case runs a real circuit and reads dst's value back through the
 * shadow, so the sweep's case count IS the suite's wall clock. It used to be a
 * PRODUCT — (mask pairs) x (value pairs) — and both factors grew:
 *
 *   - the value factor was the full cross product at W <= 5 (span^2, so 1,024
 *     pairs at W = 5), a structured set of ~5W+14 at W = 8, and a 1024/W
 *     random tail above that;
 *   - the mask factor is cq_bk_fixed_pairs, which is 12 named rows PLUS a
 *     one-bit-quantum sweep across all W positions, so it is O(W).
 *
 * Measured 2026-08-20 before this rewrite: ~2.1 MILLION L1 cases across the
 * suite — 522k in the compare suite alone, 373k in shift, 363k in bitwise —
 * for a Debug run of 63.8 s against a Release run of 4.1 s.
 *
 * THE BUDGET IS NOW A SMALL CONSTANT NUMBER OF RANDOM SAMPLES PER (KERNEL,
 * WIDTH), and nothing about it scales with W. cq_kd_samples() is that constant.
 * Each case draws a mask pair AND a value pair from one seeded RNG, so the
 * (mask, value) space is sampled jointly rather than enumerated on either axis.
 *
 * WHAT IS STILL FORCED, INSIDE THE BUDGET RATHER THAN ON TOP OF IT — these
 * cost nothing extra, because they are the first few of the N cases, not
 * additions to N:
 *
 *   case 0      the ALL-CLASSICAL mask pair. That row IS L5: cq_kd_case
 *               asserts zero gates and zero qubits on it, so leaving it to a
 *               1-in-(W+12) draw would make L5 run only sometimes.
 *   case 1      the ALL-QUANTUM mask pair. It is the mask every L4 golden is
 *               pinned at and the fixed point of the no-demotion rule (D6).
 *   cases 2-5   the four value corners (0, all-ones on each of two operands)
 *               at the all-quantum mask. A masking bug lives at the corners
 *               and a uniform draw reaches 0 or all-ones with probability ~0
 *               at W = 64, so these four are the one place the value axis is
 *               not left to chance.
 *
 * WHAT WAS GIVEN UP, STATED PLAINLY BECAUSE A CAP NOBODY CAN SEE READS AS
 * COVERAGE. The 12 named mask rows other than all-classical and all-quantum —
 * alternating, lsb-only, msb-only and risk R8's six asymmetric pairs — are no
 * longer enumerated at every width; they are now rows in the pool the random
 * draw samples from. Likewise the one-bit-quantum sweep. Across the whole
 * ladder and the four control regions a given named row is still drawn many
 * times, but no single run guarantees it. That is the trade this budget is,
 * and it was made deliberately.
 *
 * REPRODUCIBILITY IS NOT GIVEN UP. The seed is a pure function of the kernel's
 * name and the width, so a red case is reproducible by re-running the binary,
 * and two kernels never draw the same sequence. Every run prints its seed.
 *
 * TO GET THE OLD DEPTH BACK for a mutation battery or a bisect, set
 * CQOPS_L1_SAMPLES in the ENVIRONMENT — never in a test's CMake ENVIRONMENT
 * property, which would win over the shell (the same trap CQOPS_UPDATE_GOLDENS
 * has, recorded in CLAUDE.md's Build & Test section).
 */

#include "support/kerneldrv.h"

#include "support/harness.h"
#include "support/refmodel.h"

#include <stdio.h>
#include <stdlib.h>

/* The standard ladder. Widths are NOT sampled — they are the axis that
 * actually catches faults here, because every kernel is width-generic over
 * reg->width with no width switch (I5, Rule 3), so what a wide width exercises
 * that a narrow one does not is a loop bound, an MSB boundary or a carry that
 * only exists above some length. A suite that sampled widths would leave those
 * to the draw. Suites whose ladder is not this one (casts, i80, i128) drive
 * cq_kd_sweep_at per width. */
static const int LADDER_W[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64 };

enum {
    CQ_KD_SAMPLES_DEFAULT = 32,
    MAX_PAIRS             = 128 + 12,
    N_ANCHORS             = 6      /* 2 mask anchors + 4 value corners */
};

int cq_kd_samples(void)
{
    static int n = -1;

    if (n < 0) {
        const char *e = getenv("CQOPS_L1_SAMPLES");
        long v = (e && *e) ? strtol(e, NULL, 10) : 0;

        n = (v > 0 && v <= 1000000) ? (int)v : CQ_KD_SAMPLES_DEFAULT;
    }
    return n;
}

/* FNV-1a over the kernel's name, mixed with the width. A pure function of
 * both, so a failure reproduces from a bare re-run and no two kernels or
 * widths share a sequence — a time-seeded PRNG would make a red run
 * unrepeatable, which is the one thing a random test must never be. */
static uint64_t seed_for(const char *name, int W)
{
    uint64_t h = 1469598103934665603ull;

    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        h ^= (uint64_t)*p;
        h *= 1099511628211ull;
    }
    return h ^ (0x5EEDC0DEull * (uint64_t)(W + 1));
}

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

/* One kernel at one width: cq_kd_samples() cases, drawn jointly over the mask
 * pairs and the value space, with the six anchors above taken first.
 *
 * THE VALUES ARE GENERATED PER OPERAND AT ITS OWN WIDTH, which is what makes
 * this one function serve the whole catalogue with no per-kernel policy: K10's
 * mux gets a 1-bit `cond` and two W-bit arms because its shape says so, K4's
 * amount gets a full random operand whose MASK pairs_for has already forced
 * classical, and K5's casts get their width pair. The old sweep had to route
 * three-source kernels around cq_kd_case2, which fills values[2] with ZERO and
 * so ran every exhaustive mux case with one arm pinned at 0. */
void cq_kd_sample_at(const cq_kd_spec *k, int W)
{
    cq_bk_pair pairs[MAX_PAIRS];
    cq_kd_shape sh;
    uint32_t np = pairs_for(k, W, pairs, MAX_PAIRS);
    int n = cq_kd_samples();
    uint64_t seed = seed_for(k->name, W);
    cq_bk_rng rng;

    cq_kd_default_shape(W, &sh);
    if (k->shape) k->shape(W, &sh);

    /* The all-quantum row is index 1 by construction. Asserted rather than
     * assumed, and BEFORE it is used: it is the mask L4 pins and the one where
     * no operand fold can hide a lane, so a reordering of cq_bk_fixed_pairs
     * must not move it silently.
     *
     * CHECKED ON OPERAND 0 ONLY, AND NOT SKIPPED FOR SHAPED KERNELS. An earlier
     * draft skipped the whole check whenever a spec set a shape, on the premise
     * that "for those kernels no mask is all-quantum" — which is false for every
     * Step-11 spec: a cast constrains nothing at all, and a shift constrains
     * only operand 1, so q[0] is untouched in both cases. The skip disabled the
     * guard for six specs and protected none of them. */
    if (np < 2u || !cq_ref_w_eq(pairs[1].q[0], cq_ref_w_ones(W)))
        cq_h_fail(__FILE__, __LINE__,
                  "cq_kd_sample_at: fixed pair 1 is [%s], not all-quantum",
                  np < 2u ? "<none>" : pairs[1].name);

    cq_bk_rng_init(&rng, seed);

    for (int s = 0; s < n; s++) {
        cq_ref_w v[CQ_KD_MAX_SRC];
        uint32_t p;

        /* Anchors first, then a joint draw. See the header for why each anchor
         * is inside the budget rather than added to it. */
        if (s == 0)                 p = 0u;   /* all-classical — this IS L5 */
        else if (s < N_ANCHORS)     p = 1u;   /* all-quantum  — what L4 pins */
        else                        p = (uint32_t)cq_bk_rng_below(&rng, np);

        for (int i = 0; i < sh.n_src; i++) {
            if (s >= 2 && s < N_ANCHORS && i < 2) {
                /* The four corners: (0,0), (ones,0), (0,ones), (ones,ones). */
                int hot = ((s - 2) >> i) & 1;

                v[i] = hot ? cq_ref_w_ones(sh.w[i]) : cq_ref_w_zero();
            } else {
                v[i] = cq_ref_w_make(cq_bk_rng_next(&rng), cq_bk_rng_next(&rng),
                                     sh.w[i]);
            }
        }
        cq_kd_case(k, W, v, &pairs[p]);
    }

    printf("# %s W=%3d SAMPLED: %d cases (all-classical + all-quantum + 4 value "
           "corners forced, %d drawn jointly) from %u mask pairs, seed 0x%llx — "
           "a CONSTANT budget, not a product; see kernelsweep.c for what the "
           "draw replaced\n",
           k->name, W, n, n > N_ANCHORS ? n - N_ANCHORS : 0, np,
           (unsigned long long)seed);
    fflush(stdout);
}

void cq_kd_sweep_at(const cq_kd_spec *k, int W, int exhaustive)
{
    /* KEPT IN THE SIGNATURE, DELIBERATELY IGNORED. There is no longer an
     * exhaustive mode to select — the budget is the same constant at every
     * width — and 71 call sites across twelve .c files and eight .inc files
     * pass it. Removing the parameter would touch every one of them for no
     * behavioural gain, and a caller that still asks for exhaustion is asking
     * for something this file deliberately no longer offers. */
    (void)exhaustive;
    cq_kd_sample_at(k, W);
}

void cq_kd_sweep(const cq_kd_spec *k)
{
    for (size_t i = 0; i < sizeof LADDER_W / sizeof LADDER_W[0]; i++)
        cq_kd_sample_at(k, LADDER_W[i]);
}
