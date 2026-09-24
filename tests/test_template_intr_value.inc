/* tests/test_template_intr_value.inc — VALUE, L5, L2 and the pool, for the
 * integer intrinsics at the ABI edge. Part of tests/test_template_intr.c. */

/* L1 AT THE EDGE, AND L5. Every generated unary symbol at every width it
 * ships, six operands each, on CLASSICAL rails — zero gates and zero qubits
 * for the lot — against the langref reference. The `_unc` of each is run on
 * the result and must leave it ZERO: that is the second half of the dispatch
 * claim, since the `_unc` wrapper is a separate symbol with its own selector,
 * and a `_unc` routed to a different opcode leaves `f(a) ^ g(a)` behind.
 *
 * THIS IS THE SOLE DETECTOR FOR A TRANSPOSED `cq_tpl_iun_kernel` ROW. `ctlz`
 * and `cttz` are one row machine mirrored and `bitreverse`/`bswap` agree on
 * every palindromic byte, so the gate tuple, the palindrome and the pool all
 * stay green under a swap; the operand set above is chosen so every pair of
 * the six disagrees somewhere. */
CQ_TEST(every_integer_unary_wrapper_agrees_with_an_independent_reference)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);

    CHECK_EQ((int)IU_N, 23);
    for (size_t r = 0; r < IU_N; r++) {
        const iu_row *row = &IU_ROWS[r];

        for (size_t i = 0; i < IU_NV; i++) {
            const uint64_t v = val_at(i, row->W);
            const int32_t a = alloc_iw(row->W, v);
            const int32_t out = row->fwd(a);
            const uint64_t want = ref_un(row->op, v, row->W);

            CHECK_EQ(cq_reg_width(&ctx->regs, out), (uint32_t)row->W);
            if (cq_pc_value(ctx, out) != want)
                cq_h_fail(__FILE__, __LINE__, "%s(%llx) = %llx, the langref "
                          "reference says %llx", row->name,
                          (unsigned long long)v,
                          (unsigned long long)cq_pc_value(ctx, out),
                          (unsigned long long)want);
            CHECK_EQ(cq_pc_value(ctx, a), v);       /* the source is unchanged */
            row->unc(out, a);
            if (cq_pc_value(ctx, out) != 0u)
                cq_h_fail(__FILE__, __LINE__, "%s_unc left %llx on the rail",
                          row->name, (unsigned long long)cq_pc_value(ctx, out));
        }
    }
    CHECK_EQ(cq_mock_count(&m), 0);                  /* L5: no gate at all  */
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));      /* ...and no qubit     */
    cq_reg_audit(ctx);
    close_with(&m);
}

/* The PAIRS a signed and an unsigned comparison disagree about — `-1` against
 * `1`, INT_MIN against `1` — plus a tie and two ordinary ones. */
static const size_t MM_PAIRS[][2] = { {3, 2}, {1, 2}, {4, 5}, {2, 2}, {5, 1} };

#define MM_NP (sizeof MM_PAIRS / sizeof MM_PAIRS[0])

/* L1 AT THE EDGE for the four min/max, through BOTH shapes the yaml gives them
 * (`qq` and `hl`, each with its `_unc`), and L5. `smin` dispatching `umin` is
 * right wherever the operands share a sign bit — K9's `uge`-meaning-`ule`
 * trap once more — so the first two pairs are the ones that see it. The `_hl`
 * literal travels through CQ_SHIM_LO/HI as a SIGN-EXTENDED `intN_t`, which is
 * where a literal masked to the wrong width would show. */
CQ_TEST(every_min_max_wrapper_agrees_with_an_independent_reference)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);

    CHECK_EQ((int)MM_N, 16);
    for (size_t r = 0; r < MM_N; r++) {
        const mm_row *row = &MM_ROWS[r];

        for (size_t p = 0; p < MM_NP; p++) {
            const uint64_t va = val_at(MM_PAIRS[p][0], row->W);
            const uint64_t vb = val_at(MM_PAIRS[p][1], row->W);
            const uint64_t want = ref_mm(row->op, va, vb, row->W);
            const int32_t a = alloc_iw(row->W, va), b = alloc_iw(row->W, vb);
            const int32_t o1 = row->fwd(a, b), o2 = row->fwd_hl(a, vb);

            if (cq_pc_value(ctx, o1) != want || cq_pc_value(ctx, o2) != want)
                cq_h_fail(__FILE__, __LINE__, "%s(%llx, %llx): qq %llx, hl %llx, "
                          "the reference says %llx", row->name,
                          (unsigned long long)va, (unsigned long long)vb,
                          (unsigned long long)cq_pc_value(ctx, o1),
                          (unsigned long long)cq_pc_value(ctx, o2),
                          (unsigned long long)want);
            row->unc(o1, a, b);
            row->unc_hl(o2, a, vb);
            CHECK_EQ(cq_pc_value(ctx, o1), 0u);
            CHECK_EQ(cq_pc_value(ctx, o2), 0u);
        }
    }
    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    cq_reg_audit(ctx);
    close_with(&m);
}

/* ONE ROUND TRIP ON WIRES PER DOOR, and what it adds over the classical sweep
 * is everything that sweep structurally cannot say: that the arity-1 sequence
 * mints a result of the OPERAND's width on the wire path, that `_unc` is the
 * same call and zeroes it, that no scratch is left live (L2, as a SET), and
 * that the free returns each index to the pool. Built with `cq_bk_reg`, which
 * is right for value, L2 and pool claims and WRONG for a certificate claim
 * (the cases in test_template_intr_cert.inc mint through the ABI instead). */
/* ONE CONTEXT PER ROW: the source rail holds a non-zero value on real wires
 * and must NOT be freed (its free is D15's proven-dirty row and strands by
 * design), so it stays live — and L2's SET check on the next row would then
 * name it. A fresh context is the honest way to scope "live". */
static void iu_on_wires(const iu_row *row, uint64_t v)
{
    cq_mock mk; cq_sink sk;
    cq_mock *m = &mk;
    cq_ctx *ctx = open_with(m, &sk);
    const int32_t a = cq_bk_reg(ctx, (uint32_t)row->W, v, cq_ref_mask(row->W));
    const cq_pc_snap before = cq_pc_take(ctx);
    const int32_t out = row->fwd(a);
    const int32_t live[2] = { a, out };
    uint32_t idx[64], n;

    CHECK(cq_mock_count(m) > 0u);                   /* it really ran on wires */
    CHECK_EQ(cq_pc_value(ctx, out), ref_un(row->op, v, row->W));
    CHECK_EQ(cq_pc_value(ctx, a), v);
    CHECK(cq_pc_live_is_exactly(ctx, live, 2u));
    n = cq_pc_indices(ctx, out, idx, 64u);
    row->unc(out, a);
    CHECK_EQ(cq_pc_value(ctx, out), 0u);
    CHECK(cq_pc_live_is_exactly(ctx, live, 2u));
    cqrt_free(out);
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0u);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0u);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    CHECK(cq_pc_indices_are_free(ctx, idx, n));
    cq_reg_audit(ctx);
    close_with(m);
}

static const iu_row *iu_find(const char *name)
{
    for (size_t r = 0; r < IU_N; r++)
        if (strcmp(IU_ROWS[r].name, name) == 0) return &IU_ROWS[r];
    cq_h_fail(__FILE__, __LINE__, "no row %s", name);
    return &IU_ROWS[0];
}

/* `bswap i16` is the class-1 row (fwd and unc are an ADDITIVE PRODUCER, not an
 * in-place involution — intrinsic_table.yaml's own header), `ctlz i8` a
 * class-2 M41 row and `abs i8` M42's, at the operand that exercises INT_MIN. */
CQ_TEST(an_integer_unary_round_trip_on_wires_is_the_value_l2_and_the_pool)
{
    iu_on_wires(iu_find("bswap_i16"), 0x12F0u);
    iu_on_wires(iu_find("ctlz_i8"), 0x09u);
    iu_on_wires(iu_find("abs_i8"), 0x80u);
}

/* The binary door on wires, both shapes, at the operand pair a signed and an
 * unsigned minimum disagree about: `smin(-1, 1) = -1`, `umin` would say 1. */
CQ_TEST(a_min_max_round_trip_on_wires_is_the_value_l2_and_the_pool)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t a = cq_bk_reg(ctx, 8u, 0xFFu, cq_ref_mask(8));
    const int32_t b = cq_bk_reg(ctx, 8u, 0x01u, cq_ref_mask(8));
    const cq_pc_snap before = cq_pc_take(ctx);
    const int32_t o1 = cq_template_smin_i8(a, b);
    const int32_t o2 = cq_template_smax_i8_hl(a, (int8_t)1);
    const int32_t live[4] = { a, b, o1, o2 };
    uint32_t i1[8], i2[8], n1, n2;

    CHECK_EQ(cq_pc_value(ctx, o1), 0xFFu);
    CHECK_EQ(cq_pc_value(ctx, o2), 0x01u);
    CHECK(cq_pc_live_is_exactly(ctx, live, 4u));
    n1 = cq_pc_indices(ctx, o1, i1, 8u);
    n2 = cq_pc_indices(ctx, o2, i2, 8u);
    cq_template_smin_i8_unc(o1, a, b);
    cq_template_smax_i8_hl_unc(o2, a, (int8_t)1);
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
