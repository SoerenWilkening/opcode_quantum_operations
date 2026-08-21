/* tests/test_kernel_shift_var_sweep.inc — included by test_kernel_shift_var.c.
 *
 * WHICH WIDTHS — and nothing about what is asserted. Split from the .c at
 * Step 14 on the seam plan §2.2 recorded for the shared driver and
 * kerneldrv.c/kernelsweep.c took at Step 11: the LEVELS live next door, this
 * file decides only what gets fed to them.
 *
 * THE BUDGET IS NOW THE SHARED CONSTANT (2026-08-20). Every L1 case here runs
 * a real barrel circuit — ~10WL gates over a W(3L+1)-qubit region, by far the
 * most expensive case in the project — and this file used to cross that with a
 * mask pool that grows linearly in W, a full (value, amount) square below W=6
 * and an amount enumeration above it. Four local knobs existed only to cap that
 * product (a mask stride with a keep-the-first-L clause, a 512/W sample count,
 * an amount stride, and the named-pair count they were expressed in); all four
 * are gone. cq_kd_sample_at draws cq_kd_samples() cases per width, jointly over
 * masks and values, and prints the count, the pool and the seed. See
 * tests/support/kernelsweep.c for what the constant replaced and gave up.
 *
 * WHAT THAT GAVE UP HERE SPECIFICALLY, because a cap nobody can see reads as
 * coverage: the wide sweep used to enumerate the whole reduced amount range
 * [0, 2^S) — the low S bits of the amount are exactly the mux stage controls,
 * so that range IS the set of distinct circuits — and it is now sampled with
 * the rest. The per-stage mask rows (bit i quantum on both operands, i < L) are
 * likewise pool rows rather than guaranteed. What still enumerates amounts at
 * EVERY shipped width, brackets included, is the D8 cross-check in
 * test_kernel_shift_var_d8.inc, which is not an L1 sweep and was not touched.
 *
 * WIDTHS ARE ENUMERATED, NEVER SAMPLED — that is the axis that catches faults
 * in a width-generic kernel, and this suite's ladder is not the standard one.
 */

/* THE SHIPPED LADDER. shl and lshr carry i80 (opcode_table.yaml:195-196) and
 * i128; ashr carries i128 but NOT i80 (:197) — the i80 binary allow-set is
 * exactly and/lshr/or/shl (:88), so sweeping ashr there would be testing a
 * symbol CQ_lang never emits. */
static void sweep_dir(int dir)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 80, 128 };

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        if (dir == DIR_ASHR && WS[j] == 80) continue;
        cq_kd_sample_at(&SPEC[dir], WS[j]);
    }
}

CQ_TEST(k10_barrel_shl_sweep)  { sweep_dir(DIR_SHL);  }
CQ_TEST(k10_barrel_lshr_sweep) { sweep_dir(DIR_LSHR); }
CQ_TEST(k10_barrel_ashr_sweep) { sweep_dir(DIR_ASHR); }

/* Step 20 — the same four levels under PRD §9's four regions, plus §9's
 * gate-tuple transform at every shipped width. The sweep body is this suite's
 * OWN, at its cheap widths only: the promotion is per gate and width-
 * independent, so what the axis adds is its interaction with the §3 fold table.
 * Every shipped width is still covered by cq_kd_check_promotion, at two kernel
 * calls apiece. */
static void barrel_narrow(void)
{
    /* THE BARREL IS THE LONGEST POLE IN THE SUITE UNCONTROLLED (bd 97s), so the
     * controlled body stays the narrowest in the project: W <= 4, all three
     * directions. A barrel case is ~10WL gates over a W(3L+1)-qubit region and
     * the promotion multiplies that by up to three, so this body runs four
     * times over. The four regions are the reason it is capped by WIDTH and not
     * by case count — the case count is already the shared constant. */
    for (int dir = 0; dir < N_DIRS; dir++)
        for (int W = 1; W <= 4; W++) cq_kd_sample_at(&SPEC[dir], W);
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
