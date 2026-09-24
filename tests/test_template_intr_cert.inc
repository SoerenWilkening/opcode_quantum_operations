/* tests/test_template_intr_cert.inc — D15's certificate, the NINTH tag space
 * and the rows that still abort, for the integer intrinsics at the ABI edge.
 * Part of tests/test_template_intr.c.
 *
 * EVERY FREE-TIME CASE HERE POISONS ITS SOURCE FIRST. On the rotation-free
 * surface the shadow is exact and answers before the certificate is asked, so
 * a round trip on ordinary operands frees CLEAN whatever the record says (bd
 * memory the-shadow-masks-every-certificate-defect-off-the-rotation-taint). A
 * general `cqrt_ry` is the one row that poisons (D12); each case carries a
 * NEGATIVE CONTROL that the result really is UNPROVEN to the shadow, so a
 * change that stopped poisoning cannot make the rest of it vacuous. */

int32_t cq_template_add_i8_hl(int32_t, int8_t);

/* THE RECORD ITSELF, because a strand count cannot see a MISSING record: an
 * unrecorded write gives the certificate an empty history, which reduces
 * vacuously and RELEASES (bd memory an-unrecorded-workspace-write-releases-
 * vacuously) — so a poisoned round trip that asserts a release is satisfied by
 * a dropped record too. This case asserts the forward writes `out` EXACTLY
 * ONCE naming `a` and nothing else, that the `_unc` does the same under the
 * SAME tag, and that the tag is in the ninth space and differs between two
 * opcodes and between two widths. */
static uint32_t iu_one_write(uint32_t base, uint16_t op, int32_t out, int32_t a)
{
    uint32_t n = 0u, tag = 0u;

    for (uint32_t p = base; p < cq_rec_len(); p++) {
        const cq_call_rec *c = cq_rec_at(p);

        if (c->op != (uint16_t)CQ_ROP_TPL_FWD && c->op != (uint16_t)CQ_ROP_TPL_UNC)
            continue;
        CHECK_EQ((int)c->op, (int)op);
        CHECK_EQ((long long)c->h[0], (long long)out);
        CHECK_EQ((long long)c->h[1], (long long)a);
        CHECK_EQ((long long)c->h[2], (long long)CQ_REG_NONE);
        CHECK_EQ((long long)c->h[3], (long long)CQ_REG_NONE);
        CHECK_EQ((long long)c->ctrl, (long long)CQ_REG_NONE);
        tag = c->tag;
        n++;
    }
    CHECK_EQ(n, 1u);
    return tag;
}

CQ_TEST(an_integer_unary_call_records_one_write_naming_a_under_its_tag)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t a = cqrt_alloc_i16(0x0100);
    const int32_t a8 = cqrt_alloc_i8(0x10);
    uint32_t base, t_fwd, t_unc, t_cttz, t_ctlz8;
    int32_t out;

    base  = cq_rec_len();
    out   = cq_template_ctlz_i16(a);
    t_fwd = iu_one_write(base, (uint16_t)CQ_ROP_TPL_FWD, out, a);
    base  = cq_rec_len();
    cq_template_ctlz_i16_unc(out, a);
    t_unc = iu_one_write(base, (uint16_t)CQ_ROP_TPL_UNC, out, a);

    base   = cq_rec_len();
    out    = cq_template_cttz_i16(a);
    t_cttz = iu_one_write(base, (uint16_t)CQ_ROP_TPL_FWD, out, a);
    base    = cq_rec_len();
    out     = cq_template_ctlz_i8(a8);
    t_ctlz8 = iu_one_write(base, (uint16_t)CQ_ROP_TPL_FWD, out, a8);

    CHECK_EQ(t_fwd, t_unc);                         /* the twins share one  */
    CHECK_EQ(t_fwd & 0xF0000000u, 0x60000000u);     /* the ninth space      */
    CHECK(t_fwd != t_cttz);                         /* ...per opcode        */
    CHECK(t_fwd != t_ctlz8);                        /* ...and per width     */
    CHECK_EQ(cq_mock_count(&m), 0);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* THE CERTIFICATE DISCHARGES A POISONED ROUND TRIP, once per row shape: a
 * class-2 M41 row, the class-1 `bswap` (whose fwd/unc are an additive producer
 * exactly like the class-2 rows — only its `_inv` differs, and that is a D14
 * abort), and M42's `abs`. */
static void iu_poisoned(const char *name, uint64_t v)
{
    cq_ctx *ctx = cq_shim_ctx();
    const iu_row *row = iu_find(name);
    const int32_t a = alloc_iw(row->W, v);
    uint32_t strand_before;
    int32_t out;

    if (row->W == 8) cqrt_ry_i8(a, 0.5); else cqrt_ry_i16(a, 0.5);
    strand_before = cq_qubits_stranded(&ctx->pool);
    out = row->fwd(a);
    CHECK_EQ(cq_reg_disposition(ctx, out, cq_pc_zero_proof_rotation_free),
             CQ_PROOF_UNPROVEN);
    row->unc(out, a);
    cqrt_free(out);
    if (cq_qubits_stranded(&ctx->pool) != strand_before)
        cq_h_fail(__FILE__, __LINE__, "the certificate did not discharge a "
                  "poisoned %s round trip — %u qubit(s) stranded", name,
                  cq_qubits_stranded(&ctx->pool) - strand_before);
}

CQ_TEST(an_integer_unary_unc_on_a_poisoned_source_is_discharged_by_the_certificate)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    iu_poisoned("ctpop_i8", 0x05u);
    iu_poisoned("bswap_i16", 0x12F0u);
    iu_poisoned("abs_i8", 0x80u);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0u);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* The binary door's `_hl` shape, whose record carries the LITERAL in `imm`:
 * the forward and its `_unc` must agree on it or they are not a pair. */
CQ_TEST(a_min_max_hl_unc_on_a_poisoned_source_is_discharged_by_the_certificate)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t a = cqrt_alloc_i8(5);
    uint32_t strand_before;
    int32_t out;

    cqrt_ry_i8(a, 0.5);
    strand_before = cq_qubits_stranded(&ctx->pool);
    out = cq_template_umax_i8_hl(a, (int8_t)3);
    CHECK_EQ(cq_reg_disposition(ctx, out, cq_pc_zero_proof_rotation_free),
             CQ_PROOF_UNPROVEN);
    cq_template_umax_i8_hl_unc(out, a, (int8_t)3);
    cqrt_free(out);
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), strand_before);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0u);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* THE NINTH TAG SPACE'S DETECTOR, and the shape is EXACT for it. A binary
 * `_hl` forward records `{out, a, NONE, NONE}` with `imm = lo ^ hi`, and with a
 * literal of 0 that is 0 — which is what an integer unary `_unc` records, a
 * `memset` record. The twin opcodes agree (TPL_FWD / TPL_UNC), the §9 context
 * agrees, the parities agree, and `add` and `ctpop` are both index 0 of their
 * enums at width 8 — so ONLY the `0x60000000` prefix keeps `add i8, 0` from
 * pairing with `ctpop i8`'s `_unc`. `out` then holds `a ^ ctpop(a)`, which is
 * not zero; a certificate that paired them would RELEASE it.
 *
 * The free must STRAND, and D15 §3 says a strand is the program continuing —
 * one report, the rail's qubits counted. */
CQ_TEST(a_ctpop_unc_does_not_pair_with_an_add_hl_forward_on_the_same_handles)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t a = cqrt_alloc_i8(5);
    uint32_t strand_before;
    int32_t out;

    CHECK(ref_un(IU_CTPOP, 5u, 8) != 5u);            /* they DISAGREE on 5   */
    cqrt_ry_i8(a, 0.5);
    strand_before = cq_qubits_stranded(&ctx->pool);
    out = cq_template_add_i8_hl(a, (int8_t)0);
    cq_template_ctpop_i8_unc(out, a);
    cqrt_free(out);
    CHECK(cq_qubits_stranded(&ctx->pool) > strand_before);
    CHECK_EQ(cq_reg_strand_reports(ctx), 1u);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* THE WIDENED `cq_shim_op` KEEPS ONE TWIN IDENTITY PER OPCODE. `smin` and
 * `umin` are one program over `slt` vs `ult`, and on `(-1, 1)` they disagree,
 * so the rail after `umin` then `smin_unc` holds `1 ^ -1` and must strand. */
CQ_TEST(an_smin_unc_does_not_pair_with_a_umin_forward_on_the_same_handles)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t a = cqrt_alloc_i8(-1);
    const int32_t b = cqrt_alloc_i8(1);
    uint32_t strand_before;
    int32_t out;

    CHECK(ref_mm(MM_SMIN, 0xFFu, 1u, 8) != ref_mm(MM_UMIN, 0xFFu, 1u, 8));
    cqrt_ry_i8(a, 0.5);
    strand_before = cq_qubits_stranded(&ctx->pool);
    out = cq_template_umin_i8(a, b);
    cq_template_smin_i8_unc(out, a, b);
    cqrt_free(out);
    CHECK(cq_qubits_stranded(&ctx->pool) > strand_before);
    CHECK_EQ(cq_reg_strand_reports(ctx), 1u);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* EVERY ROW THAT STILL ABORTS SAYS SO WITH A SENTENCE TRUE OF IT, read out of
 * a forked child (a death case can say only "it aborted"; the interesting
 * failure is an abort naming the wrong SYMBOL or carrying the wrong REASON —
 * bead 9ve.29's old "not run yet" on a row that is refused, say). */
CQ_TEST(every_still_aborting_intrinsic_row_names_itself_and_its_own_reason)
{
    char msg[1024];

    CHECK_EQ((int)IN_N_ABORTS, 13);
    for (size_t i = 0; i < IN_N_ABORTS; i++) {
        char want[768];
        const int aborted = cq_child_capture(CQ_CHILD_STDERR, IN_ABORT_ROWS[i].fn,
                                             NULL, NULL, 0u, msg, sizeof msg);
        CHECK(aborted);
        (void)snprintf(want, sizeof want,
                       "cqops: cq_template_%s not implemented (%s)\n",
                       IN_ABORT_ROWS[i].sym, IN_ABORT_ROWS[i].why);
        if (strcmp(msg, want) != 0)
            cq_h_fail(__FILE__, __LINE__, "%s said `%s`, wanted `%s`",
                      IN_ABORT_ROWS[i].sym, msg, want);
    }
}
