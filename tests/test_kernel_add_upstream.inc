/* tests/test_kernel_add_upstream.inc — Step 12's extra gate.
 *
 * SPLIT OUT OF tests/test_kernel_add.c on the seam K06.md §3.8 draws itself:
 * validation against UPSTREAM, as against validation against ourselves. Every
 * other case in the suite compares the kernel to a reference model, a closed
 * form or its own reverse half; these two are the only ones in the whole
 * project that compare it to a number Bennett.jl published. Plan §4 calls it
 * out as Step 12's extra gate for that reason: K1-K5, K9, K10 and K12 have no
 * upstream counts at all, so `x + 1` is the single point of external contact.
 *
 * Included by test_kernel_add.c after `run_masked` and `gates_of`, which it
 * uses. An .inc rather than a second binary because these are cases of the
 * same suite (Rule 12's sanctioned escape hatch: the guard counts .c/.h/.py,
 * and the split is on a subject boundary rather than at an arbitrary line).
 */

/* ---- Step 12's extra gate: x + 1 against Bennett's published baseline. --- */

/* READ THE TRAP BEFORE EDITING (K06.md §3.8). The totals agree at all four
 * widths and the TUPLES DO NOT — ours is (2, 5W+2, 2W-2), Bennett's at i8 is
 * (6, 40, 12). The agreement is two unrelated W-independent deltas cancelling:
 *
 *     -4 X   Bennett's entry-block path-predicate NOT and its one-NOT-per-set-
 *            bit constant materialisation, both doubled by its global wrap.
 *            libcqops has no CFG and never puts a constant on a wire (I4).
 *     +2 CX  ) the two c[0]-controlled slots at i=0, doubled. Bennett's fold is
 *     +2 CCX ) value-driven and drops them; ours is kind-driven and under I6(b)
 *              c[0] is a pre-materialised qubit, so it cannot.
 *
 * So a total-only golden would pass while the circuit is wrong, which is the
 * exact L4 failure mode Rule 10 exists to prevent. The identity that actually
 * carries content is
 *
 *     Bennett_total(x+K, W) = libcqops_total(x+K, W) + 2*(1 + popcount(K)) - 4
 *
 * which evaluates to a ZERO delta at K = 1 and a +2 delta at K = 3 — which is
 * why K = 3 is here too, as the witness that the accounting is not just being
 * fitted to one point. Upstream figures verified by reading the pinned
 * snapshot, not the doc: third_party/bennett/test/test_gate_count_regression.jl
 * lines 40-48 (x+1) and 86 (x+3), and README.md:114 for the i8 tuple. */
CQ_TEST(l4_x_plus_one_against_bennetts_published_baseline)
{
    static const int WS[]  = { 8, 16, 32, 64 };
    static const int TOT[] = { 58, 114, 226, 450 };   /* :40-43 */
    static const int TOF[] = { 12, 28, 60, 124 };     /* :45-48 */
    cq_mock fwd, unc;
    uint64_t x, cx, ccx;

    cq_mock_init(&fwd);
    cq_mock_init(&unc);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];

        run_masked(&ADD, cq_ref_w_add, W,
                   cq_ref_w_ones(W), cq_ref_w_ones(W),          /* a: quantum */
                   cq_ref_w_make(1u, 0u, W), cq_ref_w_zero(),   /* b: the 1   */
                   &fwd, &unc);

        gates_of(&fwd, &x, &cx, &ccx);
        CHECK_GATES(x, cx, ccx, 2, 5 * W + 2, 2 * W - 2);

        /* The compute half, pinned STRUCTURALLY rather than by dividing the
         * total: the head is 3W+1 gates, the middle is copyout's W CX, and the
         * tail is the head reversed. At W=8 that is K06.md §3.8's (1, 17, 7). */
        CHECK(cq_mock_is_palindrome(&fwd, (size_t)(3 * W + 1), (size_t)W));

        CHECK_EQ(TOT[i], (int)(x + cx + ccx) + 2 * (1 + 1) - 4);
        CHECK_EQ(ccx, (uint64_t)TOF[i] + 2u);

        /* The uncompute is pinned separately and is NOT compared to the
         * forward (Rule 14). At the constant-`b` mask it happens to match,
         * because the only bit that could drift towards Q is `dst`, which is
         * outside the reversed region. */
        gates_of(&unc, &x, &cx, &ccx);
        CHECK_GATES(x, cx, ccx, 2, 5 * W + 2, 2 * W - 2);
    }

    /* x + 3 at i8 — the second constant, where the identity's delta is +2
     * rather than 0. K06.md §3.8: sandwiched (4, 44, 14) = 62; Bennett 64. */
    run_masked(&ADD, cq_ref_w_add, 8, cq_ref_w_ones(8), cq_ref_w_ones(8),
               cq_ref_w_make(3u, 0u, 8), cq_ref_w_zero(), &fwd, &unc);
    gates_of(&fwd, &x, &cx, &ccx);
    CHECK_GATES(x, cx, ccx, 4, 44, 14);
    CHECK_EQ(64, (int)(x + cx + ccx) + 2 * (1 + 2) - 4);
    CHECK(cq_mock_is_palindrome(&fwd, 27, 8));

    cq_mock_dispose(&fwd);
    cq_mock_dispose(&unc);
}

/* K07.md §5 D9 — sub has NO upstream baseline anywhere in the snapshot
 * (`grep -c sub` over benchmark/regression_baselines.jsonl is 0), so its
 * constant-operand case is validated against the fold table instead. `x - 1`
 * keeps the phase-1 `X(nb[i])` at every lane — the target is a pre-materialised
 * qubit, so it never folds — while `b[0] = ONE` folds phase 1's CX into an X
 * and `b[i>0] = ZERO` folds it away entirely. That is the whole saving, and
 * it saves no qubits at all. */
CQ_TEST(l4_x_minus_one_costs_thirteen_w)
{
    cq_mock fwd, unc;
    uint64_t x, cx, ccx;

    cq_mock_init(&fwd);
    cq_mock_init(&unc);

    run_masked(&SUB, cq_ref_w_sub, 8, cq_ref_w_ones(8), cq_ref_w_ones(8),
               cq_ref_w_make(1u, 0u, 8), cq_ref_w_zero(), &fwd, &unc);

    gates_of(&fwd, &x, &cx, &ccx);
    CHECK_GATES(x, cx, ccx, 20, 56, 28);            /* 2W+4 / 7W / 4W-4 = 13W */
    CHECK(cq_mock_is_palindrome(&fwd, 48, 8));      /* compute half 6W */

    cq_mock_dispose(&fwd);
    cq_mock_dispose(&unc);
}

