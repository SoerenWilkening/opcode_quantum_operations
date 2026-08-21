/* tests/test_kernel_mul_sweep.inc — included by test_kernel_mul.c.
 *
 * WHICH WIDTHS — and nothing about what is asserted. Split from the .c on the
 * seam plan §2.2 recorded for the shared driver, the same cut
 * kerneldrv.c/kernelsweep.c took at Step 11 and the barrel repeated at Step 14.
 * The LEVELS live next door; this file decides only what is fed to them.
 *
 * THE L1 BUDGET IS THE SHARED CONSTANT, AND THIS FILE NO LONGER HAS ONE OF ITS
 * OWN (2026-08-20). Every width on the ladder runs cq_kd_samples() cases — a
 * small constant, default 32, independent of W and of the kernel — each drawing
 * a mask pair AND a value pair from one seeded RNG, with the all-classical pair
 * (which IS L5), the all-quantum pair (which is what L4 pins) and the four value
 * corners taken first, inside the budget rather than on top of it.
 * cq_kd_sample_at prints the case count, the mask-pair pool and the seed at
 * every width, so nothing here is capped invisibly. See
 * tests/support/kernelsweep.c for what the constant replaced and what it gave
 * up; the local gate budget, the derived case count and the strided mask set
 * that used to live here are gone with it.
 *
 * K11 IS STILL THE MOST EXPENSIVE THING THE SUITE RUNS PER CASE, which is why
 * the constant matters more here than anywhere else. One sandwiched mul case is
 * 13W² − 8W gates over W² + 2W scratch qubits, run TWICE per driver case
 * (forward and uncompute) — so a single case at W = 64 emits 105,472 gates and
 * cycles 8,448 qubits through the pool, against ~1,400 gates and 256 qubits for
 * an add case at the same width. That per-case cost is unchanged; what changed
 * is that it now multiplies a constant instead of a product that was quadratic
 * in W on top of it.
 *
 * WIDTHS ARE ENUMERATED, NEVER SAMPLED — that is the axis that actually catches
 * faults here, because the kernel is width-generic over reg->width with no width
 * switch (I5, Rule 3). The ladder is the shipped one plus the narrow widths:
 * opcode_table.yaml:186 declares `mul` at i1, i8, i16, i32, i64 and i128 — no
 * i80, and no widening variant anywhere in the table — extended with 2, 3, 4
 * and 5 for Rule 10's narrow L1 widths and risk R8's named witnesses (W = 3 and
 * W = 5), exactly as kernelsweep.c does. It is NOT cq_kd_sweep's standard
 * ladder, which stops at 64.
 *
 * WHAT CARRIES i128's VALUE COVERAGE, AND IT IS NOT A BIG SWEEP. W = 128 is
 * structurally the same circuit as W = 64 with different loop bounds, pinned by
 * the L4 golden. What genuinely differs at 128 is the REFERENCE's 64-bit word
 * seam, and that is checked exhaustively and for free by
 * l1s_oracle_agrees_with_an_independent_shift_add in the parent file, which runs
 * no circuit at all.
 */

/* The shared driver at one width, timed, so the ladder's cost is on the record
 * next to the case count and seed cq_kd_sample_at prints for itself. */
static void mul_sweep_at(int W)
{
    clock_t t0 = clock();

    cq_kd_sweep_at(&MUL, W, 1);
    printf("#   ^ mul W=%d took %.1f s\n", W,
           (double)(clock() - t0) / CLOCKS_PER_SEC);
    fflush(stdout);
}

CQ_TEST(k11_mul_sweep)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 128 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) mul_sweep_at(WS[i]);
}

/* Step 20 — the same four levels under PRD §9's four regions, plus §9's
 * gate-tuple transform at every shipped width. The sweep body is this suite's
 * OWN, at its cheap widths only: the promotion is per gate and width-
 * independent, so what the axis adds is its interaction with the §3 fold table,
 * which is exercised at every width the ladder runs. Every shipped width is
 * still covered by cq_kd_check_promotion, at two kernel calls apiece. */
static void mul_narrow(void)
{
    for (int W = 1; W <= 5; W++) mul_sweep_at(W);
}

CQ_TEST(controlled)
{
    uint64_t reached = 0u;

    /* K11 IS THE FIRST COMPOSITE UNDER THE AXIS: its inner construction is
     * M15's whole accumulator, and K8 is not a Rule 7 kernel and has no
     * controlled path of its own. It does not need one — the axis is an emitter
     * mode, so every gate cq_addacc_step emits is promoted exactly as K11's own
     * are, and this case is where that composition is actually exercised. */
    static const int widths[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 128 };

    cq_kd_for_each_region("mul", mul_narrow);

    for (size_t w = 0; w < sizeof widths / sizeof widths[0]; w++)
        reached += cq_kd_check_promotion(&MUL, widths[w]);
    /* NOT VACUOUS: the identity above is an equality between two measurements,
     * and it holds trivially where the uncontrolled tuple is empty. K4 makes
     * that a real case rather than a hypothetical — its all-ones L4 fixture
     * saturates under D8 at every non-power-of-two width — so the ladder has to
     * say it reached something. */
    CHECK(reached > 0u);
}
