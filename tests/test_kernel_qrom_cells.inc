/* tests/test_kernel_qrom_cells.inc — the REAL-CELL half of tests/test_kernel_qrom.c,
 * split on the seam recorded for it and taken on the first measurement (the .c
 * landed at 361 against the 300 wall): `the DRIVER ↔ the REAL CELLS`. This half
 * owns the per-cell fixture the shim's shape needs, the all-quantum L4
 * measurement, the tree-alone instrument and the composition check, and the two
 * stream cases; the .c keeps the driver spec and sweep, the corpus-shape L1, the
 * known-index path, the scratch claim and the §9 regions. Included by the .c and
 * inheriting its includes; out of check_loc.sh's reach by the EXTENSION rule.
 */
/* ---- Real cells: the shim's shape, beyond the driver's reach. ----------- */

typedef struct {
    cq_ctx     ctx;
    cq_counter cnt;
    cq_sink    sink;
    int        count, W;
    int32_t    h_cells[64], h_idx, h_dst;
    cq_bit    *cells[64];
} fix;

/* `contents[j]` and `idxv` are values; `qcells`/`qidx` say which lanes are
 * quantum. The counter is reset AFTER the operands are built, so what is
 * counted is the KERNEL. */
static void fx_open(fix *f, int count, int W, const uint64_t *contents,
                    uint64_t qcells, uint64_t idxv, uint64_t qidx)
{
    cq_count_reset(&f->cnt);
    f->sink = cq_sink_counter(&f->cnt);
    cq_ctx_init(&f->ctx, &f->sink);
    f->count = count; f->W = W;
    for (int j = 0; j < count; j++) {
        f->h_cells[j] = bk(&f->ctx, (uint32_t)W, contents[j], qcells);
        f->cells[j]   = cq_reg_bits(&f->ctx.regs, f->h_cells[j]);
    }
    f->h_idx = cq_bk_reg(&f->ctx, 32u, idxv, qidx);
    f->h_dst = cq_reg_alloc_zero(&f->ctx.regs, (uint32_t)W);
    cq_count_reset(&f->cnt);
}

/* The L4 fixture: every cell lane and every index lane quantum. */
static void fx_open_allq(fix *f, int count, int W, const uint64_t *contents, uint64_t idxv)
{
    cq_count_reset(&f->cnt);
    f->sink = cq_sink_counter(&f->cnt);
    cq_ctx_init(&f->ctx, &f->sink);
    f->count = count; f->W = W;
    for (int j = 0; j < count; j++) {
        f->h_cells[j] = bk_all(&f->ctx, (uint32_t)W, contents[j]);
        f->cells[j]   = cq_reg_bits(&f->ctx.regs, f->h_cells[j]);
    }
    f->h_idx = cq_bk_reg(&f->ctx, 32u, idxv, 0xFFFFFFFFu);
    f->h_dst = cq_reg_alloc_zero(&f->ctx.regs, (uint32_t)W);
    cq_count_reset(&f->cnt);
}

static void fx_load(fix *f)
{
    cq_kernel_qload(&f->ctx, cq_reg_bits(&f->ctx.regs, f->h_dst),
                    cq_reg_cbits(&f->ctx.regs, f->h_idx), 32,
                    (const cq_bit *const *)f->cells, f->count, f->W);
}

static void fx_close(fix *f) { cq_ctx_dispose(&f->ctx); }

static uint64_t wmask(int W) { return W >= 64 ? ~(uint64_t)0 : ((uint64_t)1 << W) - 1u; }

/* Both passes at the all-quantum mask, every counter field. */
static void measure_load(int count, int W, cq_counter *fwd, cq_counter *unc)
{
    uint64_t contents[64];
    fix f;

    for (int j = 0; j < count; j++) contents[j] = (uint64_t)(j * 37 + 11) & wmask(W);
    fx_open_allq(&f, count, W, contents, 5u);
    fx_load(&f); *fwd = f.cnt;
    cq_count_reset(&f.cnt);
    fx_load(&f); *unc = f.cnt;
    fx_close(&f);
}

/* THE TREE ALONE, asked rather than written down: the exported block under
 * cq_sandwich with no copyout, at an all-quantum index. Both halves. */
static void tree_step(cq_ctx *ctx, void *env, int s)
{
    cq_qtree_step(ctx, (const cq_qtree_block *)env, s);
}

static void tree_only(int count, cq_counter *out)
{
    cq_ctx ctx; cq_counter cnt; cq_sink sink;
    cq_scratch scr; cq_qtree_block b;
    const int n = cq_qtree_depth(count);
    int32_t h_idx;

    cq_count_reset(&cnt);
    sink = cq_sink_counter(&cnt);
    cq_ctx_init(&ctx, &sink);
    h_idx = cq_bk_reg(&ctx, 32u, 5u, 0xFFFFFFFFu);
    cq_count_reset(&cnt);
    if (n == 0) { *out = cnt; cq_ctx_dispose(&ctx); return; }

    cq_scratch_alloc(&scr, (uint32_t)cq_qtree_nodes(n));
    b.flags = cq_scratch_span(&scr, 0u, (uint32_t)cq_qtree_nodes(n));
    b.idx   = cq_reg_cbits(&ctx.regs, h_idx);
    b.n     = n;
    cq_sandwich(&ctx, &scr, tree_step, cq_qtree_steps(n), NULL, 0, &b);
    cq_scratch_dispose(&scr);
    *out = cnt;
    cq_ctx_dispose(&ctx);
}

/* K13.md §3: (2, 4(Lp−1), 2(Lp−1) + count·W), or (0, W, 0) at count == 1. */
static void closed_form(int count, int W, uint64_t *x, uint64_t *cx, uint64_t *ccx)
{
    const uint64_t lp = (uint64_t)lp_of(count);
    if (count == 1) { *x = 0u; *cx = (uint64_t)W; *ccx = 0u; return; }
    *x   = 2u;
    *cx  = 4u * (lp - 1u);
    *ccx = 2u * (lp - 1u) + (uint64_t)count * (uint64_t)W;
}

static const int GOLD_COUNTS[] = { 1, 2, 3, 4, 8, 16 };
static const int GOLD_WS[]     = { 1, 8, 16, 32, 64, 80 };

CQ_TEST(l4_goldens)
{
    cq_gold g;
    char key[32];

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/qrom.counts",
                      "M29 kernels/qrom.c — K13 QROM read at a quantum index "
                      "(sandwiched; key qload_L<count>)",
                      "ALL-QUANTUM on every cell lane and every index lane; the "
                      "index is the ABI's i32; at ctrl_depth 0",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (size_t c = 0; c < sizeof GOLD_COUNTS / sizeof GOLD_COUNTS[0]; c++)
        for (size_t w = 0; w < sizeof GOLD_WS / sizeof GOLD_WS[0]; w++) {
            const int count = GOLD_COUNTS[c], W = GOLD_WS[w];
            cq_counter fwd, unc;
            uint64_t x, cx, ccx;

            closed_form(count, W, &x, &cx, &ccx);
            measure_load(count, W, &fwd, &unc);
            CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, x, cx, ccx);
            CHECK_GATES(unc.x, unc.cx, unc.ccx, x, cx, ccx);
            CHECK_EQ(fwd.ry + fwd.rz + fwd.mz + unc.ry + unc.rz + unc.mz, 0);

            (void)snprintf(key, sizeof key, "qload_L%d", count);
            cq_gold_check(&g, key, "forward", W, fwd.x, fwd.cx, fwd.ccx);
            cq_gold_check(&g, key, "unc",     W, unc.x, unc.cx, unc.ccx);
        }
    CHECK(cq_gold_close(&g));
}

/* THE COMPOSITION, reading no golden: load = tree (both halves, MEASURED) +
 * count·W Toffolis. A pruned tree moves the first term; a fan-out that skips a
 * lane moves the second. */
CQ_TEST(the_load_is_the_measured_tree_plus_one_toffoli_per_cell_lane)
{
    for (size_t c = 0; c < sizeof GOLD_COUNTS / sizeof GOLD_COUNTS[0]; c++)
        for (size_t w = 0; w < sizeof GOLD_WS / sizeof GOLD_WS[0]; w++) {
            const int count = GOLD_COUNTS[c], W = GOLD_WS[w];
            cq_counter tree, fwd, unc;

            tree_only(count, &tree);
            measure_load(count, W, &fwd, &unc);
            CHECK_EQ(fwd.x,   tree.x);
            CHECK_EQ(fwd.cx,  tree.cx + (count == 1 ? (uint64_t)W : 0u));
            CHECK_EQ(fwd.ccx, tree.ccx + (count == 1 ? 0u : (uint64_t)count * (uint64_t)W));
            /* And the tree really is the bead's 2(L−1) Toffoli with L = Lp. */
            if (count > 1) {
                CHECK_EQ(tree.ccx, 2u * ((uint64_t)lp_of(count) - 1u));
                CHECK_EQ(tree.cx,  4u * ((uint64_t)lp_of(count) - 1u));
                CHECK_EQ(tree.x,   2u);
            }
        }
}

/* ---- The stream: palindrome, and the step order re-derived. ------------- */

static void run_into_mock(int count, int W, cq_mock *m)
{
    cq_ctx ctx; cq_sink s = cq_mock_sink(m);
    uint64_t contents[64];
    int32_t h_cells[64], h_idx, h_dst;
    cq_bit *cells[64];

    cq_ctx_init(&ctx, &s);
    for (int j = 0; j < count; j++) {
        contents[j] = (uint64_t)(j * 41 + 3) & wmask(W);
        h_cells[j]  = bk_all(&ctx, (uint32_t)W, contents[j]);
        cells[j]    = cq_reg_bits(&ctx.regs, h_cells[j]);
    }
    h_idx = cq_bk_reg(&ctx, 32u, 6u, 0xFFFFFFFFu);
    h_dst = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);
    cq_mock_reset(m);
    cq_kernel_qload(&ctx, cq_reg_bits(&ctx.regs, h_dst),
                    cq_reg_cbits(&ctx.regs, h_idx), 32,
                    (const cq_bit *const *)cells, count, W);
    if (cq_pc_value(&ctx, h_dst) != contents[6 & (lp_of(count) - 1)] &&
        (6 & (lp_of(count) - 1)) < count)
        cq_h_fail(__FILE__, __LINE__, "qload count=%d W=%d: wrong cell", count, W);
    cq_ctx_dispose(&ctx);
}

CQ_TEST(the_stream_is_a_palindrome_around_the_fanout)
{
    static const int CS[] = { 2, 3, 4, 8, 16 };
    cq_mock m;

    cq_mock_init(&m);
    for (size_t i = 0; i < sizeof CS / sizeof CS[0]; i++) {
        const int count = CS[i], n = cq_qtree_depth(count);
        run_into_mock(count, 8, &m);
        CHECK(cq_mock_is_palindrome(&m, (size_t)cq_qtree_steps(n),
                                    (size_t)(count * 8)));
    }
    cq_mock_dispose(&m);
}

/* THE SLOT ARITHMETIC IS WHAT A TREE CAN GET WRONG, NOT THE GATES (the K12
 * lesson): re-derive the op KIND of every compute step from the heap structure
 * — step 0 is the root's X, then per node a Toffoli and two CNOTs — and compare
 * against the recorded stream, where at the all-quantum mask one step is one
 * gate. It records only the kind, which is what a boundary error moves. */
CQ_TEST(the_compute_steps_match_an_independent_derivation)
{
    static const int CS[] = { 2, 3, 4, 8, 16 };
    cq_mock m;

    cq_mock_init(&m);
    for (size_t i = 0; i < sizeof CS / sizeof CS[0]; i++) {
        const int count = CS[i], n = cq_qtree_depth(count);
        const int steps = cq_qtree_steps(n);
        int bad = 0;

        run_into_mock(count, 4, &m);
        for (int u = 0; u < steps; u++) {
            const cq_op want = u == 0 ? CQ_OP_X
                             : ((u - 1) % 3 == 0 ? CQ_OP_CCX : CQ_OP_CX);
            if (cq_mock_at(&m, (size_t)u)->op != want) bad++;
        }
        CHECK_EQ(bad, 0);
        CHECK_EQ(steps, 1 + 3 * (lp_of(count) - 1));
    }
    cq_mock_dispose(&m);
}

