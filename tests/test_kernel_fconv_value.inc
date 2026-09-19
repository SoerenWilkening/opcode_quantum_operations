/* tests/test_kernel_fconv_value.inc — THE VALUE CLAIMS: the classical row
 * against the oracle on every anchor, PRD-v2 §7.5's saturation table as
 * LITERALS, and bead 9ve.34's refusal as a measured divergence. Included by
 * test_kernel_fconv.c AFTER the spec table, whose `ROWS`, `host_*` and
 * `fv_ones` it reuses. NOTHING HERE READS A GOLDEN AND NOTHING HERE RUNS A
 * CIRCUIT.
 *
 * SPLIT OFF THE DRIVER ON A REAL SEAM RATHER THAN ON A LINE COUNT — though
 * Rule 12 is what forced the split to happen now (356 of 300). Everything in
 * the driver is about the CIRCUIT: which kernel, at which width pair, swept
 * against which reference. Everything here is about the VALUE and is
 * evaluated in C with no `cq_ctx` in sight: `cq_*_eval` is PRD-v2 §7.4's
 * classical short-circuit, a transcription of the same Julia the circuit
 * runs, and these cases are what say it agrees with an oracle that shares
 * nothing with it.
 *
 * THAT MATTERS MORE HERE THAN IN ANY OTHER fp MODULE, because for every NaN,
 * every infinity and every out-of-range operand the obvious oracle — a C cast
 * — is UNDEFINED BEHAVIOUR (C11 §6.3.1.4) and a compiler may fold a
 * compile-time one to anything at all. PRD-v2 §7.5's decision is to pin
 * upstream's x86 saturation, which is D3's posture applied a third time:
 * deterministic, documented, never traps. The pin table is K19.md §5.4's, and
 * §6.3 listed every RESULT column in it as UNVERIFIED-BY-EXECUTION.
 */

/* ---- The classical row against the oracle, on every anchor. ------------- */

/* PRD-v2 §7.4's short-circuit and §7.5's pin table meet here. The `_eval`
 * bodies are a C transcription of the Julia; this oracle is the host cast with
 * the UB cells pinned; they must agree on every anchor, and on the saturating
 * rows the agreement is the whole of D3's posture — deterministic,
 * documented, never traps. */
CQ_TEST(the_classical_row_agrees_with_the_oracle_on_every_anchor)
{
    char why[CQ_FPHOST_WHY_MAX];

    /* THE HOST PROBE FIRST, or a box with FTZ/DAZ set fails the subnormal and
     * tie anchors in the ORACLE rather than in the port (PRD-v2 §7.4). */
    CHECK(cq_fphost_check(why, sizeof why));
    CHECK(cq_fphost_arms_run() == CQ_FPHOST_ARMS);

    for (int i = 0; i < N_FCONV_ROWS; i++) {
        const int *ws;
        int nw = 1, one = CQ_FP64_W, checked = 0;

        if (i == FV_UITOFP) ws = cq_fconv_uitofp_widths(&nw);
        else                ws = &one;

        for (int w = 0; w < nw; w++) {
            int n = ROWS[i].anchors(ws[w], -1, NULL);

            for (int a = 0; a < n; a++) {
                cq_ref_w v[CQ_KD_MAX_SRC];
                uint64_t got, want;

                v[0] = cq_ref_w_zero();
                CHECK(ROWS[i].anchors(ws[w], a, v) == 1);
                got  = ROWS[i].eval(v[0].lo, ws[w]);
                want = ROWS[i].host(v[0].lo, ws[w]);
                if (got != want)
                    cq_h_fail(__FILE__, __LINE__,
                              "%s(0x%016llx) at F = %d: the classical row says "
                              "0x%016llx, the oracle says 0x%016llx",
                              ROWS[i].spec.name,
                              (unsigned long long)v[0].lo, ws[w],
                              (unsigned long long)got,
                              (unsigned long long)want);
                checked++;
            }
        }
        CHECK(checked > 0);
    }
}

/* ---- PRD-v2 §7.5's saturation, as LITERALS. ---------------------------- */

/* THE ONE THING IN THIS FILE THAT NO HOST MAY BE ASKED. C leaves `(int64_t)d`
 * and `(uint64_t)d` UNDEFINED for every row below, so the oracle above pins
 * them by table and this case pins the table itself — against the LIBRARY, so
 * a port that quietly started returning 0 (a plausible saturation, and the one
 * LLVM's `poison` would license) is red here rather than nowhere. Both halves
 * of D3's posture are asserted: the value, and that reaching it does not
 * trap. */
CQ_TEST(every_undefined_cell_saturates_to_x86s_indefinite_value)
{
    static const uint64_t UB[] = {
        CQ_F64_QNAN_A, CQ_F64_QNAN_B, CQ_F64_SNAN, CQ_F64_DEFAULT_NAN,
        CQ_F64_POS_INF, CQ_F64_NEG_INF,
        UINT64_C(0x43E0000000000000),   /* +2^63                  */
        UINT64_C(0xC3E0000000000001),   /* below -2^63            */
        UINT64_C(0x43E158E460913D00),   /* 1e19                   */
        UINT64_C(0x43F0000000000000)    /* +2^64                  */
    };
    enum { N_UB = (int)(sizeof UB / sizeof UB[0]) };

    for (int i = 0; i < N_UB; i++)
        if (cq_fptosi_eval(UB[i]) != FV_HIBIT)
            cq_h_fail(__FILE__, __LINE__,
                      "fptosi(0x%016llx) = 0x%016llx, not x86's indefinite "
                      "0x8000000000000000 (fptosi.jl:66-67, PRD-v2 §7.5)",
                      (unsigned long long)UB[i],
                      (unsigned long long)cq_fptosi_eval(UB[i]));

    /* `fptoui` saturates on the SAME rows except the two that land in its
     * [2^63, 2^64) half — which is the whole of the `U31` fix and the one
     * place the two functions' tables differ. */
    for (int i = 0; i < N_UB; i++) {
        uint64_t got = cq_fptoui_eval(UB[i]);
        int biased = (UB[i] == UINT64_C(0x43E0000000000000))
                  || (UB[i] == UINT64_C(0x43E158E460913D00));

        if (!biased && got != FV_HIBIT)
            cq_h_fail(__FILE__, __LINE__,
                      "fptoui(0x%016llx) = 0x%016llx, not the indefinite value",
                      (unsigned long long)UB[i], (unsigned long long)got);
    }
    CHECK_EQ(cq_fptoui_eval(UINT64_C(0x43E158E460913D00)),
             UINT64_C(0x8AC7230489E80000));    /* 1e19 — K19.md §5.4 row 21 */
    CHECK_EQ(cq_fptoui_eval(UINT64_C(0x43EFFFFFFFFFFFFF)),
             UINT64_C(0xFFFFFFFFFFFFF800));    /* prevfloat(2^64), row 23   */

    /* `-1.0` and `-0.5` are fptoui's two's-complement reinterpretation, whose
     * boundary is EXACTLY -1.0 (fptoui.jl:20-22, K19.md rows 25 and 26). */
    CHECK_EQ(cq_fptoui_eval(UINT64_C(0xBFF0000000000000)), ~UINT64_C(0));
    CHECK_EQ(cq_fptoui_eval(UINT64_C(0xBFE0000000000000)), UINT64_C(0));

    /* THE ROUND TRIP K19.md §5.4 WARNS AN IMPLEMENTER WILL READ AS A BUG:
     * INT64_MIN survives, INT64_MAX does not — because sitofp rounds 2^63-1 UP
     * to 2^63, which fptosi is then right to saturate. Both are correct. */
    CHECK_EQ(cq_fptosi_eval(cq_sitofp_eval(FV_HIBIT)), FV_HIBIT);
    CHECK_EQ(cq_sitofp_eval(UINT64_C(0x7FFFFFFFFFFFFFFF)),
             UINT64_C(0x43E0000000000000));
    CHECK_EQ(cq_fptosi_eval(cq_sitofp_eval(UINT64_C(0x7FFFFFFFFFFFFFFF))),
             FV_HIBIT);
}

/* ---- Bead 9ve.34: the i64 `uitofp` refusal is load-bearing. ------------ */

/* K19.md §6.5 RISK 1, AS A WITNESS RATHER THAN AS PROSE. Upstream routes
 * UIToFP to `soft_sitofp` and passes a 64-bit source through with no widening
 * cast (instructions.jl:7672-7673), so sitofp.jl:20 reads bit 63 as a sign.
 * This case measures the divergence — the two answers differ by 2^64 on every
 * operand with the high bit set — which is why PRD-v2 §7.9 keeps that one
 * width a loud abort, and why an anchor pointed at the host would have gone
 * red for a reason that is UPSTREAM'S and tempted someone to "fix" the port.
 *
 * AND IT ASSERTS THE OTHER HALF TOO: at every width we DO ship, the routing
 * and the correct unsigned conversion agree on every value, because a zext of
 * 32 bits or fewer can never set bit 63. That is the argument the four shipped
 * widths rest on, executed rather than stated. */
CQ_TEST(the_i64_row_upstream_would_have_emitted_is_wrong_and_the_others_are_not)
{
    static const uint64_t HIGH[] = {
        FV_HIBIT, UINT64_C(0x8000000000000001), ~UINT64_C(0),
        UINT64_C(0xC000000000000000), UINT64_C(0x8AC7230489E80000)
    };
    const int *ws;
    int nw, diverged = 0;

    /* What upstream's routing WOULD return at i64, against what the opcode
     * means. `cq_sitofp_eval` is the routing; `host_uitofp` at F = 64 is the
     * correct conversion. */
    for (int i = 0; i < (int)(sizeof HIGH / sizeof HIGH[0]); i++) {
        uint64_t routed = cq_sitofp_eval(HIGH[i]);
        uint64_t truth  = host_uitofp(HIGH[i], 64);

        if (routed != truth) diverged++;
        /* The routed answer is NEGATIVE — bit 63 of an IEEE double is its
         * sign — which is the shape of the defect and not merely "different". */
        CHECK((routed >> 63) == UINT64_C(1));
        CHECK((truth  >> 63) == UINT64_C(0));
    }
    CHECK_EQ(diverged, (int)(sizeof HIGH / sizeof HIGH[0]));

    /* Every shipped width agrees, over the whole anchor list AND every power
     * of two in range. A zext of at most 32 bits cannot set bit 63. */
    ws = cq_fconv_uitofp_widths(&nw);
    CHECK_EQ(nw, CQ_FCONV_N_UITOFP_W);
    for (int w = 0; w < nw; w++) {
        CHECK(ws[w] < 64);
        for (int k = 0; k < ws[w]; k++) {
            uint64_t v = UINT64_C(1) << (unsigned)k;

            CHECK_EQ(cq_uitofp_eval(v, ws[w]), host_uitofp(v, ws[w]));
        }
        CHECK_EQ(cq_uitofp_eval(fv_ones(ws[w]), ws[w]),
                 host_uitofp(fv_ones(ws[w]), ws[w]));
    }
}
