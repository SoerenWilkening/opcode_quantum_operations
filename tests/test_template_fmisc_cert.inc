/* tests/test_template_fmisc_cert.inc — D15's certificate and D14's `_inv`
 * for M38 at the ABI edge. Part of tests/test_template_fmisc.c.
 *
 * EVERY FREE-TIME CASE POISONS ITS SOURCE FIRST, through the ABI
 * (`cqrt_alloc_f64` for the record history, a general `cqrt_ry_f64` for the
 * poison), and carries the negative control that the shadow really has lost
 * the result — on the rotation-free surface the shadow answers first and the
 * certificate is never asked (bd memories
 * the-shadow-masks-every-certificate-defect-off-the-rotation-taint,
 * a-test-minted-rail-has-no-certificate-history). */

typedef enum { M38_UN, M38_HL, M38_LH } m38_shape;

/* One poisoned forward -> `_unc` -> free. The kernel runs on WIRES here (the
 * poison makes the operand quantum), which is also where the rounding and
 * min/max circuits are exercised on the wire path at all. */
static void m38_poisoned(const char *what, m38_shape sh, uint64_t lit,
                         int32_t (*un)(int32_t), void (*un_u)(int32_t, int32_t),
                         const fb_row *bin)
{
    cq_ctx *ctx = cq_shim_ctx();
    const int32_t a = cqrt_alloc_f64(f64_of(0x4004000000000000ull));  /* 2.5 */
    uint32_t strand_before;
    int32_t out;

    cqrt_ry_f64(a, 0.5);
    strand_before = cq_qubits_stranded(&ctx->pool);
    out = sh == M38_UN ? un(a)
        : sh == M38_HL ? bin->fwd_hl(a, f64_of(lit))
        :                bin->fwd_lh(f64_of(lit), a);
    CHECK_EQ(cq_reg_disposition(ctx, out, cq_pc_zero_proof_rotation_free),
             CQ_PROOF_UNPROVEN);
    if (sh == M38_UN)      un_u(out, a);
    else if (sh == M38_HL) bin->unc_hl(out, a, f64_of(lit));
    else                   bin->unc_lh(out, f64_of(lit), a);
    cqrt_free(out);
    if (cq_qubits_stranded(&ctx->pool) != strand_before)
        cq_h_fail(__FILE__, __LINE__, "the certificate did not discharge a "
                  "poisoned %s round trip — %u qubit(s) stranded", what,
                  cq_qubits_stranded(&ctx->pool) - strand_before);
}

/* ONE PER KERNEL FAMILY M38 BROUGHT: a sign op through the unary door, a
 * rounding opcode through it, a min/max through the binary `_hl` door (whose
 * literal rides the record's `imm`), and `copysign` through `_lh`, the one
 * door only a NON-commutative opcode has. */
CQ_TEST(an_m38_unc_on_a_poisoned_source_is_discharged_by_the_certificate)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    m38_poisoned("fneg", M38_UN, 0u, cq_template_fneg_f64,
                 cq_template_fneg_f64_unc, NULL);
    m38_poisoned("rint", M38_UN, 0u, cq_template_rint_f64,
                 cq_template_rint_f64_unc, NULL);
    m38_poisoned("fmin_hl", M38_HL, CQ_F64_ONE, NULL, NULL, &FB_ROWS[0]);
    m38_poisoned("copysign_lh", M38_LH, 0x4008000000000000ull, NULL, NULL,
                 &FB_ROWS[2]);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0u);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* `ROUND` AND `RINT` ARE TWO TWIN IDENTITIES, NOT ONE. They are two ABI rows
 * and two kernels (PRD-v2 7.7), and on a tie they disagree — so `round`'s
 * forward then `rint`'s `_unc` leaves `round(a) ^ rint(a)` on the rail, and a
 * certificate that paired them would release it. On a poisoned source the
 * free must STRAND and report once (D15 §3: the program continues). */
CQ_TEST(a_rint_unc_does_not_pair_with_a_round_forward_on_the_same_handles)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t a = cqrt_alloc_f64(f64_of(0x4004000000000000ull));  /* 2.5 */
    uint32_t strand_before;
    int32_t out;

    CHECK(h_round(0x4004000000000000ull) != h_rint(0x4004000000000000ull));
    cqrt_ry_f64(a, 0.5);
    strand_before = cq_qubits_stranded(&ctx->pool);
    out = cq_template_round_f64(a);
    cq_template_rint_f64_unc(out, a);
    cqrt_free(out);
    CHECK(cq_qubits_stranded(&ctx->pool) > strand_before);
    CHECK_EQ(cq_reg_strand_reports(ctx), 1u);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* `fneg`'s `_inv` IS D14's ABORT, AND THE SENTENCE IS THE BUCKET'S. `fneg` is
 * an involution, so `_inv` would be numerically the forward — and D14 refuses
 * defining `_inv` as the forward even where that is exact (tier 3), because
 * the ABI gives the fresh rail it would mint no `_inv_unc` twin to undo it.
 * Read out of a forked child: a death case can say only "it aborted". */
int32_t cq_template_fneg_f64_inv(int32_t);

static void call_fneg_inv(void *p)
{
    (void)p;
    (void)cq_template_fneg_f64_inv(0);
}

CQ_TEST(fneg_f64_inv_aborts_naming_itself_and_d14s_reason)
{
    char msg[512];
    const int aborted = cq_child_capture(CQ_CHILD_STDERR, call_fneg_inv, NULL,
                                         NULL, 0u, msg, sizeof msg);

    CHECK(aborted);
    CHECK_STR_EQ(msg, "cqops: cq_template_fneg_f64_inv not implemented "
                      "(_inv is f-inverse, not f; the data family has no "
                      "uniform definition (PRD 15 D14))\n");
}
