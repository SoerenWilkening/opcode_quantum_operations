/* tests/test_kernel_divrem_schedule.inc — included by test_kernel_divrem.c.
 *
 * THE PHASE BOUNDARIES, RE-DERIVED INDEPENDENTLY AND COMPARED SLOT BY SLOT.
 * K12 is the first kernel whose ENTIRE loop body belongs to other modules, so
 * what M19 can get wrong is not a gate — it is the SLOT ARITHMETIC. K12.md
 * §2.2's decode has five phases at four closed-form boundaries,
 *
 *      j == 0            P0  shift-in                        1 slot
 *      j in [1, 6W+1]    P1  M16's ult compute half          6W+1
 *      j in [6W+2, 13W]  P2  M14's sub compute half          7W-1
 *      j in [13W+1, 17W] P3  M17's mux                       4W
 *      j == 17W+1        P4  quotient bit (udiv/sdiv only)   1
 *
 * and an off-by-one in any of them puts a real, plausible gate in the wrong
 * place. L1 would very likely catch it, but "very likely" is not the standard
 * here, and there is a whole class it would NOT catch: a boundary that is wrong
 * by an amount which happens to keep the value right (the last of P2's three
 * trailing CNOTs and the first of P3's, say, both target scratch nothing else
 * reads yet).
 *
 * WHAT IS COMPARED. Each phase has a known op-KIND pattern that follows from
 * its own construction — `lower_ult!` is 2W alternating CX/X, then one X, then
 * a 4-cycle CX,CX,CCX,CCX; `lower_sub!` is 2W alternating, one X, then a
 * 5-cycle CX,CX,CCX,CCX,CX with a three-CNOT final stage; `lower_mux!` is a
 * 4-cycle CX,CX,CX,CCX — so the whole compute half's op-kind sequence is
 * predictable from first principles, and it is predicted here without calling
 * any of the three step functions. At the all-quantum mask one slot emits
 * exactly one gate, so slot index IS stream index and the comparison is exact.
 *
 * THIS IS NOT A RESTATEMENT OF THE GATE LISTS. It records only which of X / CX
 * / CCX each slot emits — three values — which is what a boundary error moves
 * and what a gate-list transcription error does not. The gate lists themselves
 * are M14's, M16's and M17's, and are tested in their own suites.
 */

static cq_op sched_ult(int W, int v)
{
    if (v <  2 * W) return (v % 2 == 0) ? CQ_OP_CX : CQ_OP_X;
    if (v == 2 * W) return CQ_OP_X;                     /* the carry seed */
    return ((v - (2 * W + 1)) % 4 < 2) ? CQ_OP_CX : CQ_OP_CCX;
}

static cq_op sched_sub(int W, int v)
{
    int u;

    if (v <  2 * W) return (v % 2 == 0) ? CQ_OP_CX : CQ_OP_X;
    if (v == 2 * W) return CQ_OP_X;                     /* the carry seed */

    u = v - (2 * W + 1);
    /* Bennett's `if i < W` guard: the top stage produces no carry-out, so it is
     * three CNOTs and no Toffoli (adder.jl:150-153). At W = 1 the 5-cycle is
     * empty and the final stage is the whole construction. */
    if (u >= 5 * (W - 1)) return CQ_OP_CX;
    return (u % 5 == 2 || u % 5 == 3) ? CQ_OP_CCX : CQ_OP_CX;
}

static cq_op sched_mux(int v)
{
    return (v % 4 == 3) ? CQ_OP_CCX : CQ_OP_CX;
}

/* The op-kind of compute-half slot `s`, from the boundaries alone. */
static cq_op sched_slot(int W, int want_q, int s)
{
    int K = 17 * W + (want_q ? 2 : 1);
    int j = s % K;

    if (j == 0)            return CQ_OP_CX;             /* P0 */
    if (j <= 6 * W + 1)    return sched_ult(W, j - 1);   /* P1 */
    if (j <= 13 * W)       return sched_sub(W, j - (6 * W + 2));
    if (j <= 17 * W)       return sched_mux(j - (13 * W + 1));
    return CQ_OP_CX;                                     /* P4 */
}

CQ_TEST(the_phase_boundaries_match_an_independent_slot_scan)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8, 16 };
    const cq_kd_spec *KS[2] = { &UDIV, &UREM };
    cq_mock m;

    cq_mock_init(&m);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++)
        for (int q = 0; q < 2; q++) {
            int W = WS[i], want_q = 1 - q;
            int n = cq_divrem_steps(W, want_q);
            cq_ref_w all = cq_ref_w_ones(W);
            cq_ref_w five = cq_ref_w_make(5u, 0u, W);

            /* All-quantum on BOTH operands: no fold can fire, so the forward
             * half's stream is the slot sequence one-for-one. */
            dr_run_masked(KS[q], W, all, all, five, all, &m);

            if (cq_mock_count(&m) != 2u * (size_t)n + (size_t)W) {
                cq_h_fail(__FILE__, __LINE__,
                          "%s W=%d: stream is %zu gates, want 2*%d + %d",
                          KS[q]->name, W, cq_mock_count(&m), n, W);
                continue;
            }

            for (int s = 0; s < n; s++) {
                cq_op want = sched_slot(W, want_q, s);
                cq_op got  = cq_mock_at(&m, (size_t)s)->op;

                if (want != got) {
                    cq_h_fail(__FILE__, __LINE__,
                              "%s W=%d slot %d (t=%d, j=%d): emitted op %d, the "
                              "phase decode says %d", KS[q]->name, W, s,
                              s / (17 * W + (want_q ? 2 : 1)),
                              s % (17 * W + (want_q ? 2 : 1)), (int)got,
                              (int)want);
                    break;                     /* one report per (W, opcode) */
                }
            }

            /* The copy-out is W CNOTs and nothing else — the "^=" of Rule 7's
             * contract, and the only place `dst` is written. */
            for (int c = 0; c < W; c++)
                CHECK_EQ((int)cq_mock_at(&m, (size_t)(n + c))->op, (int)CQ_OP_CX);
        }

    cq_mock_dispose(&m);
}
