/* tests/test_template_fmisc_value.inc — VALUE, L5, L2 and the pool for M38 at
 * the ABI edge. Part of tests/test_template_fmisc.c. */

/* THE HOST IS AN ORACLE ONLY AFTER THIS. `rint`/`nearbyint` read the CURRENT
 * rounding mode, and FTZ/DAZ would corrupt every subnormal row; the 9ve.4
 * probe is asserted in-process before any row trusts the host. */
CQ_TEST(the_host_rounds_to_nearest_before_it_is_trusted_as_an_oracle)
{
    char why[CQ_FPHOST_WHY_MAX];

    if (!cq_fphost_check(why, sizeof why))
        cq_h_fail(__FILE__, __LINE__, "host fp probe: %s", why);
    CHECK_EQ(cq_fphost_arms_run(), CQ_FPHOST_ARMS);
}

static void fu_check(cq_ctx *ctx, const fu_row *row, uint64_t v)
{
    const int32_t a = cqrt_alloc_f64(f64_of(v));
    const int32_t out = row->fwd(a);
    const uint64_t want = row->ref(v);

    CHECK_EQ(cq_reg_width(&ctx->regs, out), 64u);
    if (cq_pc_value(ctx, out) != want)
        cq_h_fail(__FILE__, __LINE__, "%s(%016llx) = %016llx, the independent "
                  "reference says %016llx", row->name, (unsigned long long)v,
                  (unsigned long long)cq_pc_value(ctx, out),
                  (unsigned long long)want);
    CHECK_EQ(cq_pc_value(ctx, a), v);               /* the source is unchanged */
    row->unc(out, a);
    if (cq_pc_value(ctx, out) != 0u)
        cq_h_fail(__FILE__, __LINE__, "%s_unc(%016llx) left %016llx on the rail",
                  row->name, (unsigned long long)v,
                  (unsigned long long)cq_pc_value(ctx, out));
}

/* L1 AT THE EDGE, AND L5: eight generated symbols, twenty operands each (the
 * sign ops three NaNs more), on CLASSICAL rails — zero gates, zero qubits — and
 * every `_unc` zeroing its own forward. THIS IS THE SOLE DETECTOR FOR A WRONG
 * ROW IN `cq_tpl_fun_kernel`: `rint` on `soft_round_away` is right on every
 * operand here but the six ties, `floor` on `ceil` on every integer, and the
 * gate tuple, the palindrome and the pool see none of it. */
CQ_TEST(every_fp_unary_wrapper_agrees_with_an_independent_reference)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);

    CHECK_EQ((int)FU_N, 8);
    for (size_t r = 0; r < FU_N; r++) {
        for (size_t i = 0; i < FU_NV; i++) fu_check(ctx, &FU_ROWS[r], FU_VALS[i]);
        if (FU_ROWS[r].sign_op)
            for (size_t i = 0; i < FU_NNAN; i++)
                fu_check(ctx, &FU_ROWS[r], FU_NANS[i]);
    }
    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    cq_reg_audit(ctx);
    close_with(&m);
}

/* THE MAPPING ITSELF, stated as values rather than inferred from a sweep:
 * `round(2.5) = 3`, `rint(2.5) = nearbyint(2.5) = 2`, and the same at -2.5 and
 * ±0.5 (where C's round gives ±1 and ties-to-even gives a SIGNED zero). The
 * expected patterns are written out here, not asked of the host, so this case
 * also pins the oracle the sweep above trusts. */
CQ_TEST(the_tie_rows_separate_c_round_from_rint_and_nearbyint)
{
    static const struct { uint64_t a, away, even; } T[] = {
        { 0x4004000000000000ull, 0x4008000000000000ull, 0x4000000000000000ull },
        { 0xc004000000000000ull, 0xc008000000000000ull, 0xc000000000000000ull },
        { 0x3fe0000000000000ull, CQ_F64_ONE,            CQ_F64_POS_ZERO       },
        { 0xbfe0000000000000ull, CQ_F64_NEG(CQ_F64_ONE), CQ_F64_NEG_ZERO      }
    };
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        const int32_t a = cqrt_alloc_f64(f64_of(T[i].a));

        CHECK_EQ(cq_pc_value(ctx, cq_template_round_f64(a)), T[i].away);
        CHECK_EQ(cq_pc_value(ctx, cq_template_rint_f64(a)), T[i].even);
        CHECK_EQ(cq_pc_value(ctx, cq_template_nearbyint_f64(a)), T[i].even);
        CHECK_EQ(h_round(T[i].a), T[i].away);
        CHECK_EQ(h_rint(T[i].a), T[i].even);
    }
    CHECK_EQ(cq_mock_count(&m), 0);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* L1 AT THE EDGE for the binary three, on every door each opcode has, and L5.
 * `fmin` on `fmax` is one 17-row program under a swapped mux pair, identical
 * counts; `copysign` with its lanes swapped is value-wrong on every pair whose
 * signs differ. Each result is judged against the rules-written reference and
 * each `_unc` must zero its own forward. */
CQ_TEST(every_fp_minmax_and_copysign_wrapper_agrees_with_an_independent_reference)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);

    for (size_t r = 0; r < FB_N; r++) {
        const fb_row *row = &FB_ROWS[r];

        for (size_t p = 0; p < FB_NP; p++) {
            const uint64_t va = FB_PAIRS[p][0], vb = FB_PAIRS[p][1];
            const uint64_t want = row->ref(va, vb);
            const int32_t a = cqrt_alloc_f64(f64_of(va));
            const int32_t b = cqrt_alloc_f64(f64_of(vb));
            const int32_t o1 = row->fwd(a, b);
            const int32_t o2 = row->fwd_hl(a, f64_of(vb));
            const int32_t o3 = row->fwd_lh ? row->fwd_lh(f64_of(va), b) : -1;

            if (cq_pc_value(ctx, o1) != want || cq_pc_value(ctx, o2) != want
                || (o3 >= 0 && cq_pc_value(ctx, o3) != want))
                cq_h_fail(__FILE__, __LINE__, "%s(%016llx, %016llx) = qq %016llx "
                          "hl %016llx lh %016llx, the reference says %016llx",
                          row->name, (unsigned long long)va,
                          (unsigned long long)vb,
                          (unsigned long long)cq_pc_value(ctx, o1),
                          (unsigned long long)cq_pc_value(ctx, o2),
                          (unsigned long long)(o3 >= 0 ? cq_pc_value(ctx, o3) : 0u),
                          (unsigned long long)want);
            row->unc(o1, a, b);
            row->unc_hl(o2, a, f64_of(vb));
            if (o3 >= 0) row->unc_lh(o3, f64_of(va), b);
            CHECK_EQ(cq_pc_value(ctx, o1), 0u);
            CHECK_EQ(cq_pc_value(ctx, o2), 0u);
            if (o3 >= 0) CHECK_EQ(cq_pc_value(ctx, o3), 0u);
        }
    }
    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    cq_reg_audit(ctx);
    close_with(&m);
}

/* `copysign(x, y)` IS `|x|` WITH `y`'s SIGN, AND THE `_lh` DOOR IS WHERE A
 * TRANSPOSITION HIDES: its literal is the MAGNITUDE and its handle the SIGN.
 * Stated on one pair with the answer written out (magnitude 3.0, sign from
 * -0.0), so a reference that agreed with a swapped kernel cannot hide it. */
CQ_TEST(copysign_takes_its_magnitude_from_the_first_lane_on_every_door)
{
    static const uint64_t THREE = 0x4008000000000000ull, NEG3 = 0xc008000000000000ull;
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t x = cqrt_alloc_f64(f64_of(THREE));
    const int32_t y = cqrt_alloc_f64(f64_of(CQ_F64_NEG_ZERO));

    CHECK_EQ(cq_pc_value(ctx, cq_template_copysign_f64(x, y)), NEG3);
    CHECK_EQ(cq_pc_value(ctx, cq_template_copysign_f64_hl(x, f64_of(CQ_F64_NEG_ZERO))),
             NEG3);
    CHECK_EQ(cq_pc_value(ctx, cq_template_copysign_f64_lh(f64_of(THREE), y)), NEG3);
    /* ...and the swapped reading, for the record: sign from 3.0, magnitude 0. */
    CHECK_EQ(cq_pc_value(ctx, cq_template_copysign_f64(y, x)), CQ_F64_POS_ZERO);
    CHECK_EQ(cq_mock_count(&m), 0);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* ONE ROUND TRIP ON WIRES PER DOOR SHAPE, the cheap way: `fneg` (the unary
 * door) and `copysign` (the binary one) are zero-Toffoli, so what this buys —
 * a 64-lane result minted on the wire path, `_unc` zeroing it, L2 as a SET,
 * each index back on the free list — costs a few hundred gates. The rounding
 * and min/max kernels run on wires in test_template_fmisc_cert.inc. */
CQ_TEST(an_fneg_and_a_copysign_round_trip_on_wires_is_the_value_l2_and_the_pool)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t a = cq_bk_reg(ctx, 64u, 0x4008000000000000ull, cq_ref_mask(64));
    const int32_t b = cq_bk_reg(ctx, 64u, CQ_F64_NEG_ZERO, cq_ref_mask(64));
    const cq_pc_snap before = cq_pc_take(ctx);
    const int32_t o1 = cq_template_fneg_f64(a);
    const int32_t o2 = cq_template_copysign_f64(a, b);
    const int32_t live[4] = { a, b, o1, o2 };
    uint32_t i1[64], i2[64], n1, n2;

    CHECK(cq_mock_count(&m) > 0);
    CHECK_EQ(cq_pc_value(ctx, o1), cq_ref_fneg(0x4008000000000000ull));
    CHECK_EQ(cq_pc_value(ctx, o2), cq_ref_copysign(0x4008000000000000ull,
                                                   CQ_F64_NEG_ZERO));
    CHECK(cq_pc_live_is_exactly(ctx, live, 4u));
    n1 = cq_pc_indices(ctx, o1, i1, 64u);
    n2 = cq_pc_indices(ctx, o2, i2, 64u);
    cq_template_fneg_f64_unc(o1, a);
    cq_template_copysign_f64_unc(o2, a, b);
    CHECK_EQ(cq_pc_value(ctx, o1), 0u);
    CHECK_EQ(cq_pc_value(ctx, o2), 0u);
    CHECK(cq_pc_live_is_exactly(ctx, live, 4u));
    cqrt_free(o1);
    cqrt_free(o2);
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0u);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0u);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    CHECK(cq_pc_indices_are_free(ctx, i1, n1));
    CHECK(cq_pc_indices_are_free(ctx, i2, n2));
    cq_reg_audit(ctx);
    close_with(&m);
}
