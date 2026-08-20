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
 * WHAT IS CAPPED, ALL OF IT PRINTED, BECAUSE A CAP NOBODY CAN SEE READS AS
 * COVERAGE. Above W = 4: the number of distinct mask pairs and the number of
 * cases per pair are both derived from ONE budget in gate emissions, exactly as
 * K11's are. NO NAMED PAIR IS EVER DROPPED — all-classical (which IS L5),
 * all-quantum (which is what L4 pins), alternating, lsb-only, msb-only and risk
 * R8's six asymmetric rows run at every width, and the top lane is always kept.
 *
 * WHAT IS NOT CAPPED: everything at W <= 4. The FULL value cross product runs
 * against every mask pair at W in {1,2,3,4}, `b = 0` included — which is
 * exactly what K12.md §6.0a's prototype measurement did, and what turns §2.0's
 * "a W-bit remainder never overflows" from a derivation into a measurement.
 * W = 3 is K12.md §2.4b's named R8 witness width (W in {1,2} do NOT witness
 * it), so it is inside the uncapped block deliberately.
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

/* ONE KNOB, IN GATE EMISSIONS PER (OPCODE, WIDTH) BLOCK, AND EVERYTHING ELSE IS
 * DERIVED FROM IT — K11's shape, for K11's reason: the per-case cost is known
 * exactly, since I6(b) makes it a function of W alone on the scratch side, so a
 * budget divided by it IS the case count. Raising or lowering coverage is one
 * number, and the run prints what that number bought at every width. */
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
