/* tests/test_kernel_mul_sweep.inc — included by test_kernel_mul.c.
 *
 * WHICH WIDTHS, WHICH VALUES, WHICH MASKS — and nothing about what is asserted.
 * Split from the .c on the seam plan §2.2 recorded for the shared driver, the
 * same cut kerneldrv.c/kernelsweep.c took at Step 11 and the barrel repeated at
 * Step 14. The LEVELS live next door; this file decides only what is fed to
 * them.
 *
 * K11 IS NOW THE MOST EXPENSIVE THING THE SUITE RUNS, BY A LARGE MARGIN, and
 * bd mmv predicted it before this file existed. One sandwiched mul case is
 * 13W² − 8W gates over W² + 2W scratch qubits, run TWICE per driver case
 * (forward and uncompute) — so a single case at W = 64 emits 105,472 gates and
 * cycles 8,448 qubits through the pool, against ~1,400 gates and 256 qubits for
 * an add case at the same width. That is roughly SEVENTY-FIVE TIMES a barrel
 * case, which was itself ten times a compare case. The fixed mask set meanwhile
 * grows LINEARLY in W (12 named pairs plus a one-bit-quantum sweep over all W
 * positions), so the uncapped product is quadratic in W on top of a quadratic
 * per-case cost.
 *
 * WHAT IS CAPPED, ALL OF IT PRINTED, BECAUSE A CAP NOBODY CAN SEE READS AS
 * COVERAGE. Above W = 8:
 *
 *   - the one-bit-quantum mask rows are strided (mul_mask_keep). NO NAMED PAIR
 *     IS EVER DROPPED: all-classical (which IS L5), all-quantum (which is what
 *     L4 pins), alternating, lsb-only, msb-only and risk R8's six asymmetric
 *     rows run at every width, and the top lane is always kept whatever the
 *     stride landed on. An off-by-one at the MSB and one at the LSB are the two
 *     faults that sweep exists to catch and both survive striding.
 *   - the random tail is mul_samples(W) ≈ 4096/W², clamped — the same "measure
 *     the depth in WORK, not in cases" argument kernelsweep.c makes with 1024/W
 *     and the barrel makes with 512/W, applied to a kernel whose per-case work
 *     is O(W²) rather than O(W) or O(WL).
 *
 * WHAT IS NOT CAPPED: everything at W ≤ 8. The full value×mask cross product
 * runs at W ∈ {1,2,3,4,5} and the structured set runs at W = 8, both through
 * the shared driver with no local policy at all — which is where L1 actually
 * catches things, and where risk R8's named witnesses live (W = 3 and W = 5).
 * The L4 goldens are single measurements rather than products and run at every
 * shipped width including i128, uncapped.
 *
 * WHAT CARRIES i128 INSTEAD OF A BIG SWEEP. The circuit is width-generic with
 * no width switch (I5, Rule 3), so W = 128 is structurally the same circuit as
 * W = 64 with different loop bounds — pinned by the L4 golden. What genuinely
 * differs at 128 is the REFERENCE's 64-bit word seam, and that is checked
 * exhaustively and for free by l1s_oracle_agrees_with_an_independent_shift_add
 * in the parent file, which runs no circuit at all.
 */

enum { MUL_MAX_PAIRS = 128 + 12, MUL_NAMED_PAIRS = 12 };

/* ONE KNOB, IN GATE EMISSIONS PER WIDTH BLOCK, AND EVERYTHING ELSE IS DERIVED
 * FROM IT. The barrel's caps at Step 14 were three independent hand-set
 * schedules (a mask stride, a sample count, an amount stride), each with its own
 * justification and its own way of drifting; here the per-case cost is known
 * exactly — 2(13W² − 8W) gates, since I6(b) makes it a function of W alone on
 * the scratch side — so a budget divided by it IS the case count. Raising or
 * lowering coverage is one number, and the run prints what that number bought at
 * every width.
 *
 * WHY 6 MILLION. Measured 2026-08-16 on this box, Debug, isolated. The uncapped
 * first draft (all mask pairs x 4 corners + 4096/W² samples) ran 73.4 s, which
 * would have made K11 the longest pole in the project by 2x over the barrel's
 * 33.5 s and undone most of bd 97s's two commits. 6M puts each of the four wide
 * blocks at a comparable cost and the whole suite at %s — see the measured table
 * in the parent file's header. */
enum { MUL_GATE_BUDGET = 6000000 };

/* NEVER BELOW THE 12 NAMED PAIRS. cq_bk_fixed_pairs writes all-classical (which
 * IS L5), all-quantum (which is what L4 pins), alternating, lsb-only, msb-only
 * and risk R8's six asymmetric rows first, then a one-bit-quantum sweep across
 * all W positions. The floor guarantees every named row runs at every width and
 * only the sweep rows are ever strided. */
static int mul_cases(int W)
{
    uint64_t w = (uint64_t)W;
    uint64_t per = 2u * (13u * w * w - 8u * w);
    uint64_t n = (uint64_t)MUL_GATE_BUDGET / (per ? per : 1u);

    return n < MUL_NAMED_PAIRS ? MUL_NAMED_PAIRS : (int)n;
}

/* As many distinct mask pairs as the budget has cases for, named rows first.
 *
 * NO "FIRST L POSITIONS UNCONDITIONALLY" CLAUSE, unlike M12's capped_pairs, and
 * the difference is structural rather than an oversight. The barrel READS bits
 * 0..L−1 of its second operand as per-stage mux controls, so those rows are the
 * finest-grained per-stage test its mask set contains and a stride would drop
 * exactly the interesting half. K11 reads every bit of `b` the same way — bit j
 * is the control of the j-th partial product, and every j is one block of the
 * schedule — so no lane is privileged and a plain stride loses nothing a
 * different stride would keep. The top lane is kept regardless, because an
 * off-by-one at the MSB is one of the two faults this sweep exists to catch. */
static uint32_t mul_pairs(int W, cq_bk_pair *out)
{
    uint32_t np = cq_bk_fixed_pairs((uint32_t)W, out, MUL_MAX_PAIRS);
    uint32_t keep = (uint32_t)mul_cases(W);
    uint32_t swept, stride, n;

    if (np <= keep) return np;

    swept = keep - MUL_NAMED_PAIRS;              /* >= 0 by mul_cases' floor */
    stride = (np - MUL_NAMED_PAIRS + swept) / (swept ? swept : 1u);
    n = MUL_NAMED_PAIRS;
    for (uint32_t i = 0; i + 1u < np - MUL_NAMED_PAIRS && n + 1u < keep; i += stride)
        out[n++] = out[MUL_NAMED_PAIRS + i];

    out[n++] = out[np - 1u];                     /* the top lane, always */
    return n;
}

/* Above the widths the shared driver can afford. The four value corners come
 * first — a masking bug lives there, and uniform sampling reaches 0 and all-ones
 * with probability ~0 at W = 64 — ROTATED BY PAIR INDEX, so that when the budget
 * allows fewer than four cases per mask the corners are still spread across the
 * mask set instead of every pair seeing the same one. */
static void sweep_wide(const cq_kd_spec *k, int W)
{
    cq_bk_pair pairs[MUL_MAX_PAIRS];
    uint32_t np = mul_pairs(W, pairs);
    int cpp = mul_cases(W) / (int)np;
    cq_ref_w ones = cq_ref_w_ones(W);
    cq_ref_w zero = cq_ref_w_zero();
    cq_bk_rng rng;
    uint64_t cases = 0;
    clock_t t0 = clock();

    if (cpp < 1) cpp = 1;
    cq_bk_rng_init(&rng, 0x3010D5ull ^ (uint64_t)W);

    for (uint32_t p = 0; p < np; p++)
        for (int i = 0; i < cpp; i++) {
            cq_ref_w v[CQ_KD_MAX_SRC];

            if (i < 4) {
                int c = (int)((p + (uint32_t)i) & 3u);

                v[0] = (c & 1) ? ones : zero;
                v[1] = (c & 2) ? ones : zero;
            } else {
                v[0] = cq_ref_w_make(cq_bk_rng_next(&rng), cq_bk_rng_next(&rng), W);
                v[1] = cq_ref_w_make(cq_bk_rng_next(&rng), cq_bk_rng_next(&rng), W);
            }
            v[2] = cq_ref_w_zero();
            cq_kd_case(k, W, v, &pairs[p]);
            cases++;
        }

    printf("# mul W=%3d sampled: %u of %u mask pairs (12 named ALWAYS + a "
           "strided one-bit sweep) x %d cases each = %llu cases, %llu gate "
           "emissions against a %d budget (seed 0x3010D5^W) — %.1f s. One case "
           "is 2(13W^2-8W) = %llu gates over W^2+2W = %d scratch qubits\n",
           W, np, (uint32_t)W + 12u, cpp, (unsigned long long)cases,
           (unsigned long long)(cases * 2u * (13u * (uint64_t)W * (uint64_t)W
                                              - 8u * (uint64_t)W)),
           MUL_GATE_BUDGET, (double)(clock() - t0) / CLOCKS_PER_SEC,
           (unsigned long long)(2 * (13 * (uint64_t)W * (uint64_t)W - 8u * (uint64_t)W)),
           W * W + 2 * W);
    fflush(stdout);
}

/* The shared driver, timed the same way, so the cheap widths are on the record
 * next to the capped ones rather than only the capped ones being visible. */
static void sweep_small(int W)
{
    clock_t t0 = clock();

    cq_kd_sweep_at(&MUL, W, 1);
    printf("#   ^ mul W=%d took %.1f s\n", W,
           (double)(clock() - t0) / CLOCKS_PER_SEC);
    fflush(stdout);
}

/* THE SHIPPED LADDER PLUS THE EXHAUSTIVE ONE. opcode_table.yaml:186 declares
 * `mul` at i1, i8, i16, i32, i64 and i128 — no i80, and no widening variant
 * anywhere in the table. Rule 10's exhaustive L1 widths {1,2,4,8} are extended
 * with 3 and 5 for risk R8's named witnesses, exactly as kernelsweep.c does. */
CQ_TEST(k11_mul_sweep)
{
    for (int W = 1; W <= 5; W++) sweep_small(W);
    sweep_small(8);

    sweep_wide(&MUL, 16);
    sweep_wide(&MUL, 32);
    sweep_wide(&MUL, 64);
    sweep_wide(&MUL, 128);
}
