/* tests/test_template_fma_rec.inc — D7b's RECORD at arity 3, walked. Part of
 * tests/test_template_fma.c (bd 9ve.37).
 *
 * `cq_tpl_ternary` records each defensive copy and its un-copy through
 * `cq_tpl_rec_copy`, exactly as `cq_tpl_binary` does, and the hole is the same
 * one (tests/test_template_d7_rec.inc's header): drop BOTH records and a
 * temporary's history is its MINT alone, which reduces VACUOUSLY and RELEASES —
 * the release `d7b_on_every_aliasing_shape_gives_the_unaliased_answer` asserts.
 * MEASURED 2026-09-24 (bd 9ve.37): with both calls deleted from
 * shim/cq_template_ternary.c, every `template|shim|runtime` test stayed green
 * in Release and Debug — AND SO DID EACH HALF DELETED ALONE, unlike arity 2.
 * A lone copy leaves the certificate short of a proof, but this family has no
 * rotation-poisoned D7b case, so the exact shadow answers CLEAN first and the
 * temporary releases anyway (bd memory
 * the-shadow-masks-every-certificate-defect-off-the-rotation-taint). So the
 * instrument is the stream.
 *
 * THE CLAIM, per aliasing shape and per door, over the call's own slice: one
 * temporary is minted per lane that repeats an EARLIER lane (two for
 * a == b == c); each temporary receives EXACTLY two uncontrolled COPY records
 * from the aliased source, one before the template record and one after it,
 * and is named by nothing else; the template record names the temporaries in
 * the redirected lanes (the post-D7b handles the kernel read); forward and
 * `_unc` share one tag. All-constant rails, so no gate is emitted. */

#define FR_MAXT 2

static uint32_t fr_walk(uint32_t base, uint16_t op, int32_t out,
                        const int32_t use[3])
{
    int32_t  t[FR_MAXT] = { CQ_REG_NONE, CQ_REG_NONE }, want[3];
    int32_t  t_src[FR_MAXT] = { CQ_REG_NONE, CQ_REG_NONE };
    uint32_t n_t = 0u, n_tpl = 0u, n_copy = 0u, p_tpl = 0u, tag = 0u;
    uint32_t pre[FR_MAXT] = { 0u, 0u }, post[FR_MAXT] = { 0u, 0u };
    uint32_t named[FR_MAXT] = { 0u, 0u };

    /* THE EXPECTED LANES, from the shape: a lane equal to an earlier lane is
     * redirected to the next temporary minted, in lane order. The temporaries'
     * handles are learned from the MINT records, in stream order. */
    for (uint32_t p = base; p < cq_rec_len(); p++) {
        const cq_call_rec *c = cq_rec_at(p);

        if (c->op == (uint16_t)CQ_ROP_MINT && c->h[0] != out && n_t < FR_MAXT)
            t[n_t++] = c->h[0];
    }
    {
        uint32_t k = 0u;

        for (int i = 0; i < 3; i++) {
            int dup = 0;

            for (int j = 0; j < i; j++) if (use[i] == use[j]) dup = 1;
            want[i] = use[i];
            if (dup && k < n_t) { t_src[k] = use[i]; want[i] = t[k++]; }
        }
        CHECK_EQ(k, n_t);
    }

    for (uint32_t p = base; p < cq_rec_len(); p++) {
        const cq_call_rec *c = cq_rec_at(p);

        for (uint32_t k = 0; k < n_t; k++)
            for (int s = 0; s < CQ_REC_SLOTS; s++)
                if (c->h[s] == t[k]) { named[k]++; break; }
        if (c->op == (uint16_t)CQ_ROP_TPL_FWD || c->op == (uint16_t)CQ_ROP_TPL_UNC) {
            CHECK_EQ((int)c->op, (int)op);
            CHECK_EQ((long long)c->h[0], (long long)out);
            for (int i = 0; i < 3; i++)
                CHECK_EQ((long long)c->h[1 + i], (long long)want[i]);
            p_tpl = p; tag = c->tag; n_tpl++;
        }
    }
    CHECK_EQ(n_tpl, 1u);

    for (uint32_t p = base; p < cq_rec_len(); p++) {
        const cq_call_rec *c = cq_rec_at(p);

        if (c->op != (uint16_t)CQ_ROP_COPY) continue;
        n_copy++;
        CHECK_EQ((long long)c->h[2], (long long)CQ_REG_NONE);
        CHECK_EQ((long long)c->h[3], (long long)CQ_REG_NONE);
        CHECK_EQ((long long)c->ctrl, (long long)CQ_REG_NONE);
        for (uint32_t k = 0; k < n_t; k++) {
            if (c->h[1] != t[k]) continue;
            CHECK_EQ((long long)c->h[0], (long long)t_src[k]);
            if (p < p_tpl) pre[k]++; else post[k]++;
        }
    }
    /* THE DETECTOR: every temporary has its copy AND its un-copy on record. */
    CHECK_EQ(n_copy, 2u * n_t);
    for (uint32_t k = 0; k < n_t; k++) {
        CHECK_EQ(pre[k], 1u);
        CHECK_EQ(post[k], 1u);
        CHECK_EQ(named[k], 4u);      /* mint, copy, template, un-copy */
    }
    return tag;
}

CQ_TEST(an_aliased_fma_call_records_every_d7b_copy_and_its_uncopy)
{
    static const int SHAPES[][4] = {   /* lane -> source index, n_tmp */
        { 0, 0, 2, 1 },   /* a == b            */
        { 0, 1, 0, 1 },   /* a == c            */
        { 0, 1, 1, 1 },   /* b == c            */
        { 0, 0, 0, 2 },   /* a == b == c       */
        { 0, 1, 2, 0 }    /* NEGATIVE CONTROL: no alias, no temporary */
    };

    for (size_t k = 0; k < sizeof SHAPES / sizeof SHAPES[0]; k++) {
        cq_mock m; cq_sink s;
        cq_ctx *ctx = open_with(&m, &s);
        const int32_t h[3] = { cqrt_alloc_f64(f64_of(FT_X)),
                               cqrt_alloc_f64(f64_of(0x4000000000000000ull)),
                               cqrt_alloc_f64(f64_of(FT_NEG_X2_RND)) };
        int32_t use[3], out;
        uint32_t base, n_mint = 0u, t_fwd, t_unc;

        for (int i = 0; i < 3; i++) use[i] = h[SHAPES[k][i]];

        base  = cq_rec_len();
        out   = cq_template_fma_f64(use[0], use[1], use[2]);
        t_fwd = fr_walk(base, (uint16_t)CQ_ROP_TPL_FWD, out, use);
        /* The walker learns the temporaries from the mints, so the COUNT is
         * pinned here, against the shape, rather than by the walker. */
        for (uint32_t p = base; p < cq_rec_len(); p++)
            if (cq_rec_at(p)->op == (uint16_t)CQ_ROP_MINT && cq_rec_at(p)->h[0] != out)
                n_mint++;
        CHECK_EQ(n_mint, (uint32_t)SHAPES[k][3]);

        base  = cq_rec_len();
        cq_template_fma_f64_unc(out, use[0], use[1], use[2]);
        t_unc = fr_walk(base, (uint16_t)CQ_ROP_TPL_UNC, out, use);
        CHECK_EQ(t_fwd, t_unc);

        CHECK_EQ(cq_mock_count(&m), 0);
        cqrt_free(out);
        for (int i = 0; i < 3; i++) cqrt_free(h[i]);
        cq_reg_audit(ctx);
        close_with(&m);
    }
}
