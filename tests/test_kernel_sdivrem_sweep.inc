/* tests/test_kernel_sdivrem_sweep.inc — included by test_kernel_sdivrem.c.
 *
 * The M19 sweep's shape and budget, applied to the two signed opcodes. See
 * tests/test_kernel_divrem_sweep.inc for the argument; what differs here is
 * only the per-case cost, which carries the wrapper's `27W+10` / `25W+6`, and
 * the value corners, which are chosen for SIGN rather than for magnitude.
 *
 * THE CORNERS MATTER MORE ON THIS SIDE. A signed divider has four sign
 * quadrants and two of them are only reachable through the conditional negate,
 * so all-ones (which is -1 at every width) and the sign bit alone (which is
 * typemin) are not merely "extreme values" — they are the inputs that make the
 * prefix and the suffix do anything at all. dr_sweep's four corners are
 * {0, all-ones} x {0, all-ones}, i.e. {0, -1} x {0, -1}, which covers `b = 0`
 * (D3), `typemin/-1` is pinned separately, and the random tail supplies the
 * rest of the quadrants at every mask.
 */

enum { DS_GATE_BUDGET = 600000 };

static uint64_t sdiv_per_case(int W, int want_q)
{
    uint64_t w = (uint64_t)W;

    /* 2 x sandwiched, because cq_kd_case runs the forward AND the uncompute. */
    return 2u * (34u * w * w + (want_q ? 27u * w + 10u : 25u * w + 6u));
}

/* One case per opcode at the top shipped width, at the all-quantum mask. The
 * signed path had never been executed at ANY width before Step 17, so an i128
 * case is not a formality here the way it is for a kernel whose core was
 * already measured — but one case is 1,120,564 gate emissions over 132,691
 * qubit acquisitions, which is why it is one and not twelve. */
static void ds_i128_case(const cq_kd_spec *k, int want_q)
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

    /* A NEGATIVE dividend and a positive divisor, so both conditional negates
     * and the sign fix all do work — the quadrant an all-positive sample would
     * never reach. */
    v[0] = cq_ref_w_make(0x0123456789ABCDEFull, 0xFEDCBA9876543210ull, 128);
    v[1] = cq_ref_w_make(0x00000000DEADBEEFull, 0x0000000000000007ull, 128);
    v[2] = cq_ref_w_zero();
    cq_kd_case(k, 128, v, &pairs[1]);

    printf("# %s W=128: 1 case at the all-quantum mask = %llu gate emissions "
           "over %d scratch qubits — %.1f s. i128 IS a shipped divrem width "
           "(opcode_table.yaml:187-190)\n",
           k->name, (unsigned long long)sdiv_per_case(128, want_q),
           cq_sdivrem_region(128, want_q),
           (double)(clock() - t0) / CLOCKS_PER_SEC);
    fflush(stdout);
}

CQ_TEST(k12_signed_sweep)
{
    for (int W = 1; W <= 4; W++) {
        dr_sweep_small(&SDIV, W);
        dr_sweep_small(&SREM, W);
    }

    for (int wi = 0; wi < 5; wi++) {
        static const int WS[] = { 5, 8, 16, 32, 64 };
        int W = WS[wi];

        dr_sweep(&SDIV, W, sdiv_per_case(W, 1), cq_sdivrem_region(W, 1),
                 DS_GATE_BUDGET);
        dr_sweep(&SREM, W, sdiv_per_case(W, 0), cq_sdivrem_region(W, 0),
                 DS_GATE_BUDGET);
    }

    ds_i128_case(&SDIV, 1);
    ds_i128_case(&SREM, 0);
}
