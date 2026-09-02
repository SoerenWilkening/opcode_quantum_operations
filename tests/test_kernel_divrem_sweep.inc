/* tests/test_kernel_divrem_sweep.inc — included by test_kernel_divrem.c.
 *
 * WHICH WIDTHS, WHICH VALUES, WHICH MASKS — and nothing about what is asserted.
 * Split from the .c on the seam plan §2.2 recorded for the shared driver, the
 * same cut kerneldrv.c/kernelsweep.c took at Step 11 and every expensive kernel
 * has repeated since.
 *
 * K12 IS THE MOST EXPENSIVE KERNEL IN THE CATALOGUE AND bd mmv SAID SO BEFORE
 * THIS FILE EXISTED. One `udiv` case runs the kernel TWICE (forward and
 * uncompute) at `34W²+5W` gates each, over a region of `8W²+4W-1` qubits that
 * is pre-materialised and released on every call — so a case at W = 64 is
 * 279,168 gate emissions and 66,046 qubit acquisitions, against a `mul` case's
 * 105,472 and 8,448 at the same width, and a compare case's ~1,500 and ~200.
 * The fixed mask set meanwhile grows LINEARLY in W. The uncapped product is
 * quadratic in W on top of a quadratic per-case cost, twice over.
 *
 * THE BUDGET IS THE SHARED CONSTANT AT EVERY WIDTH (2026-08-20), AND THIS
 * HEADER SAID OTHERWISE UNTIL bd mmv CLOSED ON 2026-09-02. Every width — the
 * W <= 4 block through dr_sweep_small as much as the wide one through dr_sweep
 * — draws cq_kd_samples() cases, jointly over the mask pool and the value
 * space, with the all-classical pair (which IS L5), the all-quantum pair (which
 * is what L4 pins) and the four value corners taken first, inside the budget;
 * every width prints its count, its pool and its seed. There is no gate-budget
 * cap above W = 4 and no exhaustive cross product below it: cq_kd_sweep_at's
 * `exhaustive` flag is kept in the signature and ignored (kernelsweep.c), and
 * so are dr_sweep's `per_case`, `region` and `budget` (divrem_common.inc says
 * why the parameters stayed). W = 3 is K12.md §2.4b's named R8 witness width
 * (W in {1,2} do NOT witness it) and is on the ladder for that reason; what
 * turned §2.0's "a W-bit remainder never overflows" into a measurement was
 * K12.md §6.0a's prototype pass, not this file.
 *
 * WHAT CARRIES i128. The circuit is width-generic with no width switch (I5,
 * Rule 3), so W = 128 is structurally W = 64 with different loop bounds —
 * pinned by the L4 golden and by the peak-qubit case. What genuinely differs at
 * 128 is the REFERENCE's 64-bit word seam, and that is checked without running
 * any circuit at all by l1s_oracle_agrees_with_the_hardware_divider, which
 * carries i128 on the division identity `a == q*b + r`. One live circuit case
 * per opcode runs here anyway, at the all-quantum mask, because "the kernel
 * produces the right value at the top shipped width" is a claim worth having
 * measured rather than extrapolated — and one case is 1,115,392 gate emissions
 * over 263,166 qubit acquisitions, which is why it is one and not twelve.
 */

/* THE FORMER KNOB. Until 2026-08-20 this was a budget in gate emissions per
 * (opcode, width) block from which the case count was derived; dr_sweep now
 * ignores it, and udiv_per_case survives because the i128 case prints it. The
 * knob that sets coverage is cq_kd_samples() / CQOPS_L1_SAMPLES. */
enum { DR_GATE_BUDGET = 600000 };

static uint64_t udiv_per_case(int W, int want_q)
{
    uint64_t w = (uint64_t)W;

    /* 2 x sandwiched, because cq_kd_case runs the forward AND the uncompute. */
    return 2u * (34u * w * w + (want_q ? 5u : 3u) * w);
}

/* i128 in the L1 sweep is ONE case per opcode, and the reason it is not zero
 * and not twelve is stated rather than defaulted — see the header. The mask is
 * pair 1, which cq_bk_fixed_pairs writes as all-quantum by construction and
 * which kernelsweep.c asserts; it is asserted again here rather than assumed,
 * because it is the mask L4 pins and a reordering must not move it silently. */
static void dr_i128_case(const cq_kd_spec *k, int want_q)
{
    cq_bk_pair pairs[DR_MAX_PAIRS];
    cq_ref_w v[CQ_KD_MAX_SRC];
    clock_t t0 = clock();

    cq_bk_fixed_pairs(128u, pairs, DR_MAX_PAIRS);
    if (!cq_ref_w_eq(pairs[1].q[0], cq_ref_w_ones(128))) {
        cq_h_fail(__FILE__, __LINE__, "fixed pair 1 is [%s], not all-quantum",
                  pairs[1].name);
        return;
    }

    /* A dividend with both words populated and a divisor whose value crosses
     * the seam, so the quotient is neither 0 nor 1 and every iteration does
     * real work. */
    v[0] = cq_ref_w_make(0x0123456789ABCDEFull, 0xFEDCBA9876543210ull, 128);
    v[1] = cq_ref_w_make(0x00000000DEADBEEFull, 0x0000000000000007ull, 128);
    v[2] = cq_ref_w_zero();
    cq_kd_case(k, 128, v, &pairs[1]);

    printf("# %s W=128: 1 case at the all-quantum mask = %llu gate emissions "
           "over %d scratch qubits — %.1f s. i128 IS a shipped divrem width "
           "(opcode_table.yaml:187-190); the reference's word seam is carried "
           "without a circuit by l1s_oracle_agrees_with_the_hardware_divider\n",
           k->name, (unsigned long long)udiv_per_case(128, want_q),
           cq_divrem_region(128, want_q),
           (double)(clock() - t0) / CLOCKS_PER_SEC);
    fflush(stdout);
}

/* THE SHIPPED LADDER PLUS THE EXHAUSTIVE ONE. opcode_table.yaml:187-190
 * declares all four opcodes at i1, i8, i16, i32, i64 and i128 — and NOT at i80
 * (:85, :133-135), the mirror image of `icmp`'s fence. Rule 10's exhaustive L1
 * widths {1,2,4} are extended with 3, K12.md §2.4b's named R8 witness. */
CQ_TEST(k12_unsigned_sweep)
{
    for (int W = 1; W <= 4; W++) {
        dr_sweep_small(&UDIV, W);
        dr_sweep_small(&UREM, W);
    }

    for (int wi = 0; wi < 5; wi++) {
        static const int WS[] = { 5, 8, 16, 32, 64 };
        int W = WS[wi];

        dr_sweep(&UDIV, W, udiv_per_case(W, 1), cq_divrem_region(W, 1),
                 DR_GATE_BUDGET);
        dr_sweep(&UREM, W, udiv_per_case(W, 0), cq_divrem_region(W, 0),
                 DR_GATE_BUDGET);
    }

    dr_i128_case(&UDIV, 1);
    dr_i128_case(&UREM, 0);
}

/* Step 20 — the same four levels under PRD §9's four regions, plus §9's
 * gate-tuple transform at every shipped width. The sweep body is this suite's
 * OWN, at its cheap widths only: the promotion is per gate and width-
 * independent, so what the axis adds is its interaction with the §3 fold table
 * — a property of the bit-KIND space (D6: the table reads kind, never value),
 * densest at narrow widths, where cq_kd_samples() draws cover most of the
 * mask-pair pool. Nothing has been exhaustive since 2026-08-21 (bd p3z). Every
 * shipped width is still covered by cq_kd_check_promotion, at two kernel calls
 * apiece. */
static void udivrem_narrow(void)
{
    /* W <= 3, not 5. K12 IS THE MOST EXPENSIVE THING THE SUITE RUNS — one udiv
     * case is 2(34W^2+5W) gate emissions over 2(8W^2+4W-1) qubit acquisitions —
     * and the promotion multiplies that by up to three. bd mmv's discipline is
     * to budget it up front rather than after measuring, which is what this is.
     * L1 is cq_kd_samples() cases per width here as everywhere (2026-08-20),
     * with `b = 0` reached through the all-zero value corner. */
    for (int W = 1; W <= 3; W++) {
        dr_sweep_small(&UDIV, W);
        dr_sweep_small(&UREM, W);
    }
}

CQ_TEST(controlled)
{
    uint64_t reached = 0u;

    static const int widths[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 128 };
    static const cq_kd_spec *const KS[] = { &UDIV, &UREM };

    cq_kd_for_each_region("udiv/urem", udivrem_narrow);

    for (size_t i = 0; i < sizeof KS / sizeof KS[0]; i++)
        for (size_t w = 0; w < sizeof widths / sizeof widths[0]; w++)
            reached += cq_kd_check_promotion(KS[i], widths[w]);
    /* NOT VACUOUS: the identity above is an equality between two measurements,
     * and it holds trivially where the uncontrolled tuple is empty. K4 makes
     * that a real case rather than a hypothetical — its all-ones L4 fixture
     * saturates under D8 at every non-power-of-two width — so the ladder has to
     * say it reached something. */
    CHECK(reached > 0u);
}
