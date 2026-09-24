/* tests/test_template_bitcast_wires.inc — L1 on wires, L4 and L2/L3 for
 * `bitcast f64 <-> i64` at the ABI edge. Part of tests/test_template_bitcast.c.
 *
 * Operands here are built with `cq_bk_reg` (bit-exact value, chosen mask). That
 * is interchangeable with the ABI's allocator for value, pool, L2 and gate
 * cases and NOT for a certificate case (bd memory
 * a-test-minted-rail-has-no-certificate-history), which is why the
 * certificate lives in the other .inc and mints through `cqrt_alloc_*`. */

/* The L1 budget's seed, the house rule: FNV-1a over the name, XOR the width. */
static uint64_t bc_seed(const char *name)
{
    uint64_t h = 0xcbf29ce484222325ull;

    for (; *name; name++) { h ^= (unsigned char)*name; h *= 0x100000001b3ull; }
    return h ^ 64u;
}

static uint64_t bc_next(uint64_t *s)       /* splitmix64 */
{
    uint64_t z = (*s += 0x9e3779b97f4a7c15ull);

    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static unsigned bc_popcount(uint64_t x)
{
    unsigned n = 0u;

    for (; x; x &= x - 1u) n++;
    return n;
}

/* ONE forward and its `_unc` at one (value, mask), judged four ways: the value
 * against memcpy, the tuple against the closed form `(0, popcount(mask), 0)`,
 * the stream lane by lane, and `live` growing by exactly the lanes that became
 * wires. */
static void bc_wire_case(cq_ctx *ctx, cq_mock *m, const bc_row *row,
                         uint64_t v, uint64_t mask)
{
    const int32_t a = cq_bk_reg(ctx, 64u, v, mask);
    const uint32_t live0 = cq_qubits_live(&ctx->pool);
    const unsigned q = bc_popcount(mask);
    int32_t out;

    cq_mock_reset(m);
    out = row->fwd(a);
    if (cq_pc_value(ctx, out) != row->ref(v))
        cq_h_fail(__FILE__, __LINE__, "%s(%016llx) at mask %016llx = %016llx, "
                  "memcpy says %016llx", row->name, (unsigned long long)v,
                  (unsigned long long)mask,
                  (unsigned long long)cq_pc_value(ctx, out),
                  (unsigned long long)row->ref(v));
    CHECK_EQ(cq_pc_value(ctx, a), v);
    CHECK_GATES(cq_mock_count_op(m, CQ_OP_X), cq_mock_count_op(m, CQ_OP_CX),
                cq_mock_count_op(m, CQ_OP_CCX), 0, q, 0);
    CHECK_EQ(cq_qubits_live(&ctx->pool), live0 + q);
    bc_stream_is_the_lane_copy(ctx, m, 0u, a, out);

    cq_mock_reset(m);
    row->unc(out, a);
    CHECK_EQ(cq_pc_value(ctx, out), 0u);
    CHECK_GATES(cq_mock_count_op(m, CQ_OP_X), cq_mock_count_op(m, CQ_OP_CX),
                cq_mock_count_op(m, CQ_OP_CCX), 0, q, 0);
    cqrt_free(out);     /* `a` stays live: it is a non-zero rail */
}

/* L1 ON WIRES, a constant sample budget per direction (`cq_kd_samples()`),
 * mask and value drawn jointly from one seeded stream, with the anchors inside
 * it: all-classical, all-quantum, alternating, LSB-only, MSB-only. Every NaN
 * pattern above is also run at the all-quantum mask, which is where a kernel
 * that tested the classical exponent and flipped the quiet bit would hide. */
CQ_TEST(every_bitcast_wrapper_is_a_lane_copy_at_every_mask_drawn)
{
    static const uint64_t ANCHOR[] = { 0u, ~0ull, 0x5555555555555555ull,
                                       1ull, 1ull << 63 };
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    for (size_t r = 0; r < 2; r++) {
        uint64_t st = bc_seed(BC[r].name);
        const int n = cq_kd_samples();

        for (int k = 0; k < n; k++) {
            const uint64_t v = bc_next(&st);
            const uint64_t mask = k < 5 ? ANCHOR[k] : bc_next(&st);

            bc_wire_case(ctx, &m, &BC[r], v, mask);
        }
        for (size_t i = 0; i < BC_NV; i++)
            bc_wire_case(ctx, &m, &BC[r], BC_VALS[i], ~0ull);
    }
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0u);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* L4, stated once as the tuple: at the ALL-QUANTUM mask (the D6 fixed point)
 * the forward and the `_unc` are each `(NOT, CNOT, Toffoli) = (0, 64, 0)` in
 * BOTH directions. No golden file: the figure is M13's closed form for `zext`
 * at `F == T` — F CX and nothing else (K05.md §4) — and a pinned file would
 * be a second, blessable copy of one line of arithmetic. */
CQ_TEST(the_all_quantum_tuple_is_0_64_0_forward_and_unc_in_both_directions)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    for (size_t r = 0; r < 2; r++) {
        const int32_t a = cq_bk_reg(ctx, 64u, 0x3ff8000000000001ull, ~0ull);
        int32_t out;

        cq_mock_reset(&m);
        out = BC[r].fwd(a);
        CHECK_GATES(cq_mock_count_op(&m, CQ_OP_X), cq_mock_count_op(&m, CQ_OP_CX),
                    cq_mock_count_op(&m, CQ_OP_CCX), 0, 64, 0);
        cq_mock_reset(&m);
        BC[r].unc(out, a);
        CHECK_GATES(cq_mock_count_op(&m, CQ_OP_X), cq_mock_count_op(&m, CQ_OP_CX),
                    cq_mock_count_op(&m, CQ_OP_CCX), 0, 64, 0);
        CHECK_EQ(cq_mock_count(&m), 64);
    }
    cq_reg_audit(ctx);
    close_with(&m);
}

/* L2 AS A SET after the forward, after the `_unc` and after the free; L3 as
 * `out`'s own indices back on the free list and nothing stranded. */
CQ_TEST(a_bitcast_round_trip_on_wires_is_the_value_l2_and_the_pool)
{
    for (size_t r = 0; r < 2; r++) {
        cq_mock m; cq_sink s;
        cq_ctx *ctx = open_with(&m, &s);
        const int32_t a = cq_bk_reg(ctx, 64u, CQ_F64_SNAN, ~0ull);
        const cq_pc_snap before = cq_pc_take(ctx);
        const int32_t out = BC[r].fwd(a);
        const int32_t live[2] = { a, out };
        uint32_t idx[64], n;

        CHECK_EQ(cq_pc_value(ctx, out), CQ_F64_SNAN);
        CHECK(cq_pc_live_is_exactly(ctx, live, 2u));
        n = cq_pc_indices(ctx, out, idx, 64u);
        CHECK_EQ(n, 64u);
        BC[r].unc(out, a);
        CHECK_EQ(cq_pc_value(ctx, out), 0u);
        CHECK(cq_pc_live_is_exactly(ctx, live, 2u));
        cqrt_free(out);
        CHECK(cq_pc_live_is_exactly(ctx, live, 1u));
        CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0u);
        CHECK_EQ(cq_reg_strand_reports(ctx), 0u);
        CHECK(cq_pc_same(before, cq_pc_take(ctx)));
        CHECK(cq_pc_indices_are_free(ctx, idx, n));
        cq_reg_audit(ctx);
        close_with(&m);
    }
}
