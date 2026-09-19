/* tests/test_template_fma_cert.inc — the four cases whose subject is D15's
 * CERTIFICATE rather than the value: the slot-3 refusal, its positive twin,
 * the eighth tag space, and the record itself. Included by
 * test_template_fma.c.
 *
 * THE SEAM IS `THE VALUE SURFACE <-> THE CERTIFICATE SURFACE`, and it is a
 * subject cut rather than a line count: every case here runs on POISONED
 * rails, because on the rotation-free surface `cq_shim_free_proof`'s shadow
 * row is exact and answers before the certificate is consulted at all —
 * measured one family over, where dropping `CQ_ROP_TPL_FWD`'s write row left
 * the unpoisoned case green. Nothing in the parent file poisons anything, and
 * nothing here asserts a gate count.
 */

/* ---- (4) The poisoned certificate case — the R(3) detector. ------------- */

/* THE ONLY THING IN THE PROJECT THAT CAN SEE SLOT 3. Every other case here
 * runs on the rotation-free surface, where `cq_shim_free_proof`'s SHADOW row
 * is exact and answers before the certificate is consulted at all. A general
 * `Ry` off §7's lattice is the one row that poisons (PRD §15 D12), and after
 * it the free is the certificate's alone.
 *
 * THE SHAPE IS EXACT AND EVERY PART OF IT IS LOAD-BEARING. Forward, then MOVE
 * `c` with an `X`, then `_unc`, then free. The two halves no longer cancel, so
 * `out` is dirty — and the ONLY thing standing between that and a release is
 * `CQ_ROP_TPL_FWD` READING slot 3: without it `c`'s `last_read` never moves,
 * the engine sees an unchanged operand, the pair reduces to identity and the
 * rail is released. With it the pair is refused and the free STRANDS, loudly.
 *
 * THE NEGATIVE CONTROL IS WHAT MAKES THE REST A CLAIM: without the
 * `CQ_PROOF_UNPROVEN` assertion a change that stopped poisoning would leave
 * every line below green for the wrong reason. */
CQ_TEST(an_fma_unc_whose_third_source_moved_is_refused_by_the_certificate)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    (void)ctx;
    int32_t ha = wire(FT_X);
    int32_t hb = wire(FT_X);
    int32_t hc = wire(FT_NEG_X2_RND);
    uint32_t strand0 = cq_qubits_stranded(&ctx->pool);
    int32_t out;

    out = cq_template_fma_f64(ha, hb, hc);
    CHECK_EQ(cq_reg_disposition(ctx, out, cq_pc_zero_proof_rotation_free),
             CQ_PROOF_UNPROVEN);

    /* THE THIRD SOURCE MOVES BETWEEN THE TWO HALVES, THROUGH THE ABI. `cqrt_x`
     * takes the HANDLE of a ONE-BIT rail and not a qubit index — measured:
     * handing it a lane index aborts in M07 with "handle out of range" — so
     * the move is `cqrt_xorc_i64`, which flips `c`'s bit 0 on a 64-wide rail
     * and is RECORDED as a `CQ_ROP_XORC` write. A recorded write is what the
     * reduction has to see; a poke at the register table would not be one. */
    cqrt_xorc_i64(hc, 1);

    cq_template_fma_f64_unc(out, ha, hb, hc);
    cqrt_free(out);

    /* STRANDED, LOUDLY. That is D15 §3's disposition working rather than
     * failing: releasing here would be the silent state collapse Rule 6
     * exists to prevent. */
    CHECK(cq_qubits_stranded(&ctx->pool) > strand0);
    CHECK_EQ(cq_reg_strand_reports(ctx), 1u);

    cq_reg_audit(ctx);
    close_with(&m);
}

/* AND THE POSITIVE TWIN, so the case above is not passing because the
 * certificate discharges nothing at all. Same poison, nothing moved, and the
 * free must be CLEAN. */
CQ_TEST(an_fma_unc_on_a_poisoned_source_is_discharged_by_the_certificate)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    (void)ctx;
    int32_t ha = wire(FT_X);
    int32_t hb = wire(FT_X);
    int32_t hc = wire(FT_NEG_X2_RND);
    uint32_t strand0 = cq_qubits_stranded(&ctx->pool);
    int32_t out;

    out = cq_template_fma_f64(ha, hb, hc);
    CHECK_EQ(cq_reg_disposition(ctx, out, cq_pc_zero_proof_rotation_free),
             CQ_PROOF_UNPROVEN);
    cq_template_fma_f64_unc(out, ha, hb, hc);
    cqrt_free(out);

    if (cq_qubits_stranded(&ctx->pool) - strand0 != 0u)
        cq_h_fail(__FILE__, __LINE__,
                  "the certificate did not discharge a poisoned fma round "
                  "trip — %u qubit(s) stranded",
                  cq_qubits_stranded(&ctx->pool) - strand0);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0u);

    cq_reg_audit(ctx);
    close_with(&m);
}

/* ---- (5) The eighth tag space. ------------------------------------------ */

/* AN `fma` `_unc` MUST NOT PAIR WITH AN `fmul` FORWARD ON THE SAME HANDLES,
 * and this is what makes the ternary family's TAG SPACE a tested fact rather
 * than a comment. `adjoint_matches` pairs on the twin opcode, the tag, all
 * four handle slots, `ctrl` and `xpar` — and an `fmul` forward writing `out`
 * from `a` and `b` agrees with an `fma` `_unc` on the first three slots, on
 * ctrl and on the opcode pair. Only the `0x10000000` versus `0x50000000`
 * prefix keeps them apart, and only because the slot-3 handles differ.
 *
 * THE SOURCES MUST BE POISONED OR THE SHADOW ANSWERS FIRST, and the operands
 * must be ones the two operations DISAGREE about, or the rail ends at zero and
 * the case passes under the mutant for the wrong reason. */
CQ_TEST(an_fma_unc_does_not_pair_with_an_fmul_forward_on_the_same_handles)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    int32_t ha, hb, hc;
    uint32_t strand0;
    int32_t out;

    /* The two operations really do disagree here — asserted rather than
     * asserted-by-eye. */
    CHECK(cq_fmul_eval(FT_X, FT_X) != cq_fma_eval(FT_X, FT_X, FT_NEG_X2_RND));

    ha = wire(FT_X);
    hb = wire(FT_X);
    hc = wire(FT_NEG_X2_RND);
    strand0 = cq_qubits_stranded(&ctx->pool);

    out = cq_template_fmul_f64(ha, hb);
    /* The `_unc` of a DIFFERENT operation. A legal call; the certificate's job
     * is to decline to discharge it. */
    cq_template_fma_f64_unc(out, ha, hb, hc);
    cqrt_free(out);

    CHECK(cq_qubits_stranded(&ctx->pool) > strand0);
    CHECK_EQ(cq_reg_strand_reports(ctx), 1u);

    cq_reg_audit(ctx);
    close_with(&m);
}

/* ---- (6) D7b, at all three pairs. --------------------------------------- */

/* THREE SOURCES MAKE THREE ALIASING PAIRS WHERE TWO MAKE ONE, AND
 * `cq_reg_sources_alias` CANNOT TELL THEM APART — it answers a bare 0/1 over
 * all pairs. `a == b` with a distinct `c` needs ONE temporary; `a == b == c`
 * needs TWO. Each shape is driven separately, and the claim is the VALUE plus
 * the pool: the defensive copies are minted, written, un-copied and freed, so
 * the pool comes back and the answer is the unaliased one. */
CQ_TEST(d7b_on_every_aliasing_shape_gives_the_unaliased_answer)
{
    static const int SHAPES[][3] = {
        { 0, 0, 2 },   /* a == b            */
        { 0, 1, 0 },   /* a == c            */
        { 0, 1, 1 },   /* b == c            */
        { 0, 0, 0 }    /* a == b == c, TWO temporaries */
    };
    static const uint64_t V[3] = { FT_X, 0x4000000000000000ull,
                                   FT_NEG_X2_RND };

    for (size_t k = 0; k < sizeof SHAPES / sizeof SHAPES[0]; k++) {
        cq_mock m; cq_sink s;
        cq_ctx *ctx = open_with(&m, &s);
        int32_t h[3];
        cq_pc_snap before;
        int32_t use[3], out;
        uint64_t want;

        for (int i = 0; i < 3; i++) h[i] = qreg(ctx, (uint32_t)W64, V[i]);
        for (int i = 0; i < 3; i++) use[i] = h[SHAPES[k][i]];
        want = cq_fma_eval(V[SHAPES[k][0]], V[SHAPES[k][1]], V[SHAPES[k][2]]);

        before = cq_pc_take(ctx);
        out = cq_template_fma_f64(use[0], use[1], use[2]);
        if (cq_pc_value(ctx, out) != want)
            cq_h_fail(__FILE__, __LINE__,
                      "aliasing shape %zu: got %016llx, want %016llx", k,
                      (unsigned long long)cq_pc_value(ctx, out),
                      (unsigned long long)want);
        /* THE TEMPORARIES ARE GONE. Whatever D7b minted is un-copied and
         * freed before the call returns, so the only live rails are the three
         * sources and the result — which a count could not distinguish from a
         * temporary that leaked and a source that was released. */
        {
            const int32_t named[] = { h[0], h[1], h[2], out };

            CHECK(cq_pc_live_is_exactly(ctx, named, 4));
        }
        CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0u);

        cq_template_fma_f64_unc(out, use[0], use[1], use[2]);
        CHECK_EQ(cq_pc_value(ctx, out), 0u);
        cqrt_free(out);
        CHECK(cq_pc_same(before, cq_pc_take(ctx)));

        cq_reg_audit(ctx);
        close_with(&m);
    }
}

/* ---- (7) The record really names all four slots. ------------------------ */

/* AN UNRECORDED WRITE RELEASES VACUOUSLY (bd memory
 * an-unrecorded-workspace-write-releases-vacuously), and a record that named
 * three slots instead of four would make every `fma` pair on `{out, a, b}`
 * alone — indistinguishable from an `fmul` and blind to `c` entirely. The
 * strand counter cannot see either in the direction that matters, so the
 * instrument has to be the RECORD. All-constant, so it costs no gates. */
CQ_TEST(the_fma_record_names_all_four_slots_and_pairs_on_them)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    int32_t ha = cqrt_alloc_f64(f64_of(FT_X));
    int32_t hb = cqrt_alloc_f64(f64_of(FT_X));
    int32_t hc = cqrt_alloc_f64(f64_of(FT_NEG_X2_RND));
    uint32_t base = cq_rec_len(), p, n_fwd = 0u, n_unc = 0u;
    uint32_t tag_fwd = 0u, tag_unc = 1u;
    int32_t out;

    out = cq_template_fma_f64(ha, hb, hc);
    cq_template_fma_f64_unc(out, ha, hb, hc);

    for (p = base; p < cq_rec_len(); p++) {
        const cq_call_rec *c = cq_rec_at(p);

        if (c->op != (uint16_t)CQ_ROP_TPL_FWD && c->op != (uint16_t)CQ_ROP_TPL_UNC)
            continue;
        CHECK_EQ((long long)c->h[0], (long long)out);
        CHECK_EQ((long long)c->h[1], (long long)ha);
        CHECK_EQ((long long)c->h[2], (long long)hb);
        /* SLOT 3, WHICH NO ARITY-2 FAMILY FILLS. */
        CHECK_EQ((long long)c->h[3], (long long)hc);
        CHECK_EQ((long long)c->ctrl, (long long)CQ_REG_NONE);
        if (c->op == (uint16_t)CQ_ROP_TPL_FWD) { tag_fwd = c->tag; n_fwd++; }
        else                                   { tag_unc = c->tag; n_unc++; }
    }
    CHECK_EQ(n_fwd, 1u);
    CHECK_EQ(n_unc, 1u);
    /* THE TWIN IDENTITY: one tag, and it is the EIGHTH space's. */
    CHECK_EQ(tag_fwd, tag_unc);
    CHECK_EQ(tag_fwd & 0xF0000000u, 0x50000000u);
    /* And it is not any other family's prefix — which is the whole claim a
     * shared tag space would break. */
    CHECK(tag_fwd != 0u);

    /* THE EFFECT TABLE REALLY DOES READ SLOT 3, asked of the table rather
     * than inferred from behaviour. */
    CHECK(cq_rec_effect(CQ_ROP_TPL_FWD)->reads & (uint8_t)(1u << 3));
    CHECK(cq_rec_effect(CQ_ROP_TPL_UNC)->reads & (uint8_t)(1u << 3));

    CHECK_EQ(cq_mock_count(&m), 0);
    cq_reg_audit(ctx);
    close_with(&m);
}

