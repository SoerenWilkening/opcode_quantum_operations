/* tests/test_template_d7_rec.inc — D7b's RECORD, walked. Part of
 * tests/test_template.c, after tests/test_template_d7.inc (bd 9ve.37).
 *
 * WHY THIS IS NOT ONE MORE POOL ASSERTION IN THE D7b FILE. `cq_tpl_binary`
 * records the defensive copy and its un-copy as two `CQ_ROP_COPY`s so the
 * certificate has a cancelling pair for the temporary. DROP BOTH and the
 * temporary's history is its MINT alone: it reduces VACUOUSLY, reads a zero
 * birth value and RELEASES — which is exactly what every case in
 * test_template_d7.inc asserts (a strand delta of zero, `live` restored), so
 * all of them stay green. MEASURED 2026-09-24 (bd 9ve.37): with both
 * `cq_tpl_rec_copy` calls deleted, all 154 `template|shim|runtime` tests
 * passed in Release AND Debug. Dropping ONE of the two IS seen (the lone copy
 * cannot pair, the temporary strands), by test_template and test_shim_cert.
 * A THIRD SHAPE of the same hole: `cq_tpl_rec_copy` with its two slots
 * SWAPPED records the copy as a write to `a` that READS the temporary, so the
 * temporary again has no write on record and releases vacuously — only this
 * case and its arity-3 sibling saw it (test_shim_cert stayed green, both
 * configurations).
 * The release here happens to be right because the gates do cancel; composed
 * with any defect that stops them cancelling, it is the unforgivable bug.
 * A strand counter cannot see an absent record in either direction (bd memory
 * a-dropped-template-record-is-seen-only-by-a-record-walk), so the instrument
 * is the stream itself.
 *
 * THE CLAIM, per aliased call, over the call's own slice of the stream: the
 * FIRST mint names a temporary `t` that is neither `a` nor `out`; EXACTLY two
 * records copy `a` into `t` (slots {a, t, NONE, NONE}, uncontrolled — the copy
 * brackets the §9 region from OUTSIDE); EXACTLY one template record names
 * {out, a, t, NONE} under the call's own control; the order is mint < copy <
 * template < un-copy; and NOTHING ELSE in the slice names `t`. */

static uint32_t d7r_walk(uint32_t base, uint16_t op, int32_t out, int32_t a,
                         int32_t ctrl)
{
    int32_t  t = CQ_REG_NONE;
    uint32_t p_mint = 0u, p_tpl = 0u, p_copy[2] = { 0u, 0u };
    uint32_t n_copy = 0u, n_tpl = 0u, n_t = 0u, tag = 0u;

    for (uint32_t p = base; p < cq_rec_len(); p++) {
        const cq_call_rec *c = cq_rec_at(p);

        if (c->op == (uint16_t)CQ_ROP_MINT && t == CQ_REG_NONE) {
            t = c->h[0];
            p_mint = p;
        }
        if (t != CQ_REG_NONE)
            for (int s = 0; s < CQ_REC_SLOTS; s++)
                if (c->h[s] == t) { n_t++; break; }
        if (c->op == (uint16_t)CQ_ROP_COPY) {
            CHECK_EQ((long long)c->h[0], (long long)a);
            CHECK_EQ((long long)c->h[1], (long long)t);
            CHECK_EQ((long long)c->h[2], (long long)CQ_REG_NONE);
            CHECK_EQ((long long)c->h[3], (long long)CQ_REG_NONE);
            CHECK_EQ((long long)c->ctrl, (long long)CQ_REG_NONE);
            if (n_copy < 2u) p_copy[n_copy] = p;
            n_copy++;
        }
        if (c->op == (uint16_t)CQ_ROP_TPL_FWD || c->op == (uint16_t)CQ_ROP_TPL_UNC) {
            CHECK_EQ((int)c->op, (int)op);
            CHECK_EQ((long long)c->h[0], (long long)out);
            CHECK_EQ((long long)c->h[1], (long long)a);
            CHECK_EQ((long long)c->h[2], (long long)t);   /* the POST-D7b lane */
            CHECK_EQ((long long)c->h[3], (long long)CQ_REG_NONE);
            CHECK_EQ((long long)c->ctrl, (long long)ctrl);
            p_tpl = p;
            tag   = c->tag;
            n_tpl++;
        }
    }
    CHECK(t != CQ_REG_NONE);
    CHECK(t != a);
    CHECK(t != out);
    CHECK_EQ(n_copy, 2u);                 /* THE DETECTOR: a dropped record */
    CHECK_EQ(n_tpl, 1u);
    CHECK_EQ(n_t, 4u);                    /* mint, copy, template, un-copy  */
    CHECK(p_mint < p_copy[0]);
    CHECK(p_copy[0] < p_tpl);
    CHECK(p_tpl < p_copy[1]);
    return tag;
}

CQ_TEST(an_aliased_binary_call_records_the_d7b_copy_and_its_uncopy)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t a    = qreg(ctx, TPL_W, TPL_A);
    const int32_t a2   = qreg(ctx, TPL_W, TPL_A);
    const int32_t flag = qreg(ctx, 1u, 1u);
    uint32_t base, t_fwd, t_unc;
    int32_t  r, rc, plain;

    /* THE FORWARD AND THE `_unc` DOORS — the twins must share one tag. */
    base  = cq_rec_len();
    r     = cq_shim_bin_qq(CQ_SHIM_OP_MUL, (int)TPL_W, a, a);
    t_fwd = d7r_walk(base, (uint16_t)CQ_ROP_TPL_FWD, r, a, CQ_REG_NONE);
    base  = cq_rec_len();
    cq_shim_bin_qq_unc(CQ_SHIM_OP_MUL, (int)TPL_W, r, a, a);
    t_unc = d7r_walk(base, (uint16_t)CQ_ROP_TPL_UNC, r, a, CQ_REG_NONE);
    CHECK_EQ(t_fwd, t_unc);

    /* THE CONTROLLED DOOR: the template record carries the flag, the two
     * copies do not — they are outside the region (bd memory
     * d7b-copy-brackets-the-region-from-outside). */
    base = cq_rec_len();
    rc   = cq_shim_bin_qq_ctrl(CQ_SHIM_OP_MUL, (int)TPL_W, flag, a, a);
    (void)d7r_walk(base, (uint16_t)CQ_ROP_TPL_FWD, rc, a, flag);

    /* NEGATIVE CONTROL: without an alias there is no temporary and no COPY,
     * so the walk above is counting D7b's records and nothing ambient. */
    base  = cq_rec_len();
    plain = cq_shim_bin_qq(CQ_SHIM_OP_MUL, (int)TPL_W, a, a2);
    for (uint32_t p = base; p < cq_rec_len(); p++)
        CHECK(cq_rec_at(p)->op != (uint16_t)CQ_ROP_COPY);

    cqrt_free(r); cqrt_free(rc); cqrt_free(plain);
    cqrt_free(a); cqrt_free(a2); cqrt_free(flag);
    close_with(&m);
}
