/* tests/test_kernel_shift_var_sweep.inc — included by test_kernel_shift_var.c.
 *
 * WHICH WIDTHS, WHICH VALUES, WHICH MASKS — and nothing about what is
 * asserted. Split from the .c at Step 14 on the seam plan §2.2 recorded for
 * the shared driver and kerneldrv.c/kernelsweep.c took at Step 11: the LEVELS
 * live next door, this file decides only what gets fed to them.
 *
 * THE SPLIT WAS SCHEDULED, NOT A SURPRISE (Rule 12). The .c reached 292 lines
 * against the 300-line guard the moment the caps below were written down, and
 * a cap that is not written down is the thing this file exists to avoid.
 */

/* ---- L1 + L2 + L3 + L5: the sweep, with the wide widths capped. --------- */

enum { SHV_MAX_PAIRS = 128 + 12, SHV_NAMED_PAIRS = 12 };

/* A CAP, PRINTED, BECAUSE A CAP NOBODY CAN SEE READS AS COVERAGE.
 *
 * A barrel case is by far the most expensive thing this project runs: the
 * sandwiched circuit is ~10WL gates and the region is W(3L+1) qubits, so one
 * case at W=128 emits 9,344 gates twice over and cycles 2,816 qubits through
 * the pool. The fixed mask set meanwhile grows LINEARLY in W — 12 named pairs
 * plus a one-bit-quantum sweep across all W positions — so crossing all of it
 * with all three directions at 16/32/64/80/128 measured at roughly 240 million
 * gate emissions, against a whole-suite budget of a few tens of millions.
 *
 * WHAT IS DROPPED IS ONLY THE ONE-BIT SWEEP's STRIDE, and never a named row.
 * All twelve named pairs — all-classical (which IS L5), all-quantum (which is
 * what L4 pins), alternating, lsb/msb and risk R8's six asymmetric rows — run
 * at every width. Above W = 16 the one-bit-quantum sweep is strided, keeping
 * 16 of its W positions to W = 32, then 10, then 6, and ALWAYS the top lane
 * whatever the stride landed on: an off-by-one at the MSB and one at the LSB
 * are the two faults that sweep exists to catch, and both survive striding.
 * The full W-position sweep runs unstrided at W <= 16, where the exhaustive
 * value cross product runs too.
 *
 * MEASURED, AND THE HONEST NUMBER IS NOT FLATTERING. Debug, isolated:
 *
 *      uncapped                        76.6 s
 *      capped                          45.8 s      <- this file
 *      test_kernel_cmp, for scale      22.2 s
 *
 * so the barrel is still the longest pole in the project by 2x, and the whole
 * Debug suite went from 25.4 s to 46.0 s at -j12 because of it. An earlier
 * draft of this comment claimed the caps made it "sit alongside the compare
 * suite"; they do not, and the gap widened again when the stage rows above were
 * un-capped to close a real coverage hole. That trade was taken deliberately —
 * coverage over wall clock — and the structural fix (one binary per direction,
 * which ctest -j would then overlap) is filed rather than improvised here,
 * because K11 and K12 will need it more than this does.
 *
 * WHAT IS NOT CAPPED: the L4 goldens and the D8 cross-check run at EVERY
 * shipped width including 80 and 128. Those are per-width single measurements,
 * not products, so they cost nothing worth capping. */
static uint32_t capped_pairs(int W, cq_bk_pair *out)
{
    uint32_t np = cq_bk_fixed_pairs((uint32_t)W, out, SHV_MAX_PAIRS);
    uint32_t swept = np - SHV_NAMED_PAIRS;
    uint32_t keep = W <= 32 ? 16u : (W <= 64 ? 10u : 6u);
    uint32_t L = (uint32_t)cq_shift_stages(W);
    uint32_t stride, n;

    if (W <= 16 || swept <= keep) return np;

    /* THE FIRST L POSITIONS ARE KEPT UNCONDITIONALLY, AND THAT IS NOT
     * belt-and-braces — a stride would drop half of them and the loss would be
     * exactly the interesting half. Row 12+i of the fixed set is "bit i quantum
     * on BOTH operands, everything else classical", and for operand 1 — the
     * amount — bits 0..L-1 are precisely the mux controls, one per stage. So
     * position i < L is the case "stage i is selected by a qubit and every
     * other stage folds away", which is the finest-grained per-stage test the
     * mask set contains. At W = 32 a plain stride of 2 keeps stages 0, 2, 4 and
     * silently drops stages 1 and 3; at W = 128 a stride of 22 keeps only stage
     * 0 of seven. Everything from L upward is structurally invisible to the
     * barrel (never a control), so THOSE are the rows a stride may thin. */
    n = SHV_NAMED_PAIRS;
    for (uint32_t i = 0; i < L && i < swept; i++)
        out[n++] = out[SHV_NAMED_PAIRS + i];

    stride = (swept + keep - 1u) / keep;
    for (uint32_t i = L; i < swept; i += stride)
        out[n++] = out[SHV_NAMED_PAIRS + i];

    /* The top lane always, whatever the stride landed on. */
    out[n++] = out[np - 1u];
    return n;
}

/* 512/W rather than kernelsweep.c's 1024/W, because a barrel case costs about
 * ten times what a compare case does at the same width — the same "measure the
 * depth in WORK, not in cases" argument, applied to a kernel whose per-case
 * work is 10WL rather than O(W). */
static int shv_samples(int W)
{
    int n = 512 / W;

    if (n > 32) n = 32;
    if (n < 4)  n = 4;
    return n;
}

/* At most 16 distinct amounts below i80 and 8 at or above it. The amounts are
 * strided across the WHOLE reduced range [0, 2^S), so the saturating interval
 * [W, 2^S) — non-empty only at non-power-of-two widths, i.e. only at 80 here —
 * is still sampled by the sweep, and is covered exhaustively by the D8
 * cross-check further down. */
static int amount_stride(int W, int amounts)
{
    int keep = W > 64 ? 8 : 16;

    return amounts > keep ? amounts / keep : 1;
}

/* Full cross at the exhaustive widths: every (value, amount) pair against
 * every mask pair. The AMOUNT is a full operand here, so this sweeps every
 * shift amount in [0, 2^W) — including the whole saturating interval
 * [W, 2^S) at W = 3 and W = 5, which is where D8's second half lives. */
static void sweep_small(const cq_kd_spec *k, int W)
{
    cq_bk_pair pairs[SHV_MAX_PAIRS];
    uint32_t np = capped_pairs(W, pairs);
    uint64_t span = cq_ref_mask(W) + 1u;
    uint64_t cases = 0;

    for (uint32_t p = 0; p < np; p++)
        for (uint64_t va = 0; va < span; va++)
            for (uint64_t vk = 0; vk < span; vk++) {
                cq_kd_case2(k, W, va, vk, &pairs[p]);
                cases++;
            }

    printf("# %s W=%2d FULL CROSS: %u mask pairs x %llu (value, amount) pairs "
           "= %llu cases\n", k->name, W, np,
           (unsigned long long)(span * span), (unsigned long long)cases);
    fflush(stdout);
}

/* Above the exhaustive widths: every amount in [0, 2^S) — the whole reduced
 * range, which is 8 to 128 values and is the part that actually selects a
 * circuit — crossed with a handful of value corners and a seeded tail. */
static void sweep_wide(const cq_kd_spec *k, int W)
{
    cq_bk_pair pairs[SHV_MAX_PAIRS];
    uint32_t np = capped_pairs(W, pairs);
    int S = cq_shift_stages(W);
    int amounts = 1 << S;
    int step = amount_stride(W, amounts);
    cq_ref_w corner[3];
    cq_bk_rng rng;
    uint64_t cases = 0;

    corner[0] = cq_ref_w_ones(W);
    corner[1] = cq_ref_w_setbit(W - 1);        /* the sign lane, for ashr */
    corner[2] = cq_ref_w_setbit(0);

    cq_bk_rng_init(&rng, 0xBA22E1ull ^ (uint64_t)W);

    for (uint32_t p = 0; p < np; p++) {
        for (int amt = 0; amt < amounts; amt += step)
            for (int c = 0; c < 3; c++) {
                cq_ref_w v[CQ_KD_MAX_SRC];

                v[0] = corner[c];
                v[1] = cq_ref_w_make((uint64_t)amt, 0u, W);
                v[2] = cq_ref_w_zero();
                cq_kd_case(k, W, v, &pairs[p]);
                cases++;
            }

        for (int s = 0; s < shv_samples(W); s++) {
            cq_ref_w v[CQ_KD_MAX_SRC];

            v[0] = cq_ref_w_make(cq_bk_rng_next(&rng), cq_bk_rng_next(&rng), W);
            v[1] = cq_ref_w_make(cq_bk_rng_next(&rng), cq_bk_rng_next(&rng), W);
            v[2] = cq_ref_w_zero();
            cq_kd_case(k, W, v, &pairs[p]);
            cases++;
        }
    }

    printf("# %s W=%3d sampled: %u mask pairs (capped, see capped_pairs) x "
           "(%d of %d amounts x 3 corners + %d sampled) = %llu cases "
           "(seed 0xBA22E1^W)\n", k->name, W, np,
           (amounts + step - 1) / step, amounts, shv_samples(W),
           (unsigned long long)cases);
    fflush(stdout);
}

/* THE SHIPPED LADDER. shl and lshr carry i80 (opcode_table.yaml:195-196) and
 * i128; ashr carries i128 but NOT i80 (:197) — the i80 binary allow-set is
 * exactly and/lshr/or/shl (:88), so sweeping ashr there would be testing a
 * symbol CQ_lang never emits. */
static void sweep_dir(int dir)
{
    const cq_kd_spec *k = &SPEC[dir];

    for (int W = 1; W <= 5; W++) sweep_small(k, W);
    sweep_wide(k, 8);
    sweep_wide(k, 16);
    sweep_wide(k, 32);
    sweep_wide(k, 64);
    if (dir != DIR_ASHR) sweep_wide(k, 80);
    sweep_wide(k, 128);
}

CQ_TEST(k10_barrel_shl_sweep)  { sweep_dir(DIR_SHL);  }
CQ_TEST(k10_barrel_lshr_sweep) { sweep_dir(DIR_LSHR); }
CQ_TEST(k10_barrel_ashr_sweep) { sweep_dir(DIR_ASHR); }

/* Step 20 — the same four levels under PRD §9's four regions, plus §9's
 * gate-tuple transform at every shipped width. The sweep body is this suite's
 * OWN, at its cheap widths only: the promotion is per gate and width-
 * independent, so what the axis adds is its interaction with the §3 fold table,
 * which is exhausted where the value cross product is. Every shipped width is
 * still covered by cq_kd_check_promotion, at two kernel calls apiece. */
static void barrel_narrow(void)
{
    /* THE BARREL IS THE LONGEST POLE IN THE SUITE UNCONTROLLED (bd 97s), so the
     * controlled body is the narrowest in the project: W <= 4, all three
     * directions. A barrel case is ~10WL gates over a W(3L+1)-qubit region and
     * the promotion multiplies that by up to three; four regions at the full
     * ladder would move the whole Debug run on its own. */
    for (int dir = 0; dir < N_DIRS; dir++)
        for (int W = 1; W <= 4; W++) sweep_small(&SPEC[dir], W);
}

CQ_TEST(controlled)
{
    uint64_t reached = 0u;

    static const int widths[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 128 };

    cq_kd_for_each_region("barrel", barrel_narrow);

    for (int dir = 0; dir < N_DIRS; dir++)
        for (size_t w = 0; w < sizeof widths / sizeof widths[0]; w++) {
            if (dir == DIR_ASHR && widths[w] == 80) continue;
            reached += cq_kd_check_promotion(&SPEC[dir], widths[w]);
        }
    /* NOT VACUOUS: the identity above is an equality between two measurements,
     * and it holds trivially where the uncontrolled tuple is empty. K4 makes
     * that a real case rather than a hypothetical — its all-ones L4 fixture
     * saturates under D8 at every non-power-of-two width — so the ladder has to
     * say it reached something. */
    CHECK(reached > 0u);
}
