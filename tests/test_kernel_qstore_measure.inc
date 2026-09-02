/* tests/test_kernel_qstore_measure.inc — the INSTRUMENT half of
 * tests/test_kernel_qstore.c, split on the seam recorded for it and taken on the
 * first measurement (the .c landed at 358 against the 300 wall): `the LEVELS ↔
 * the INSTRUMENTS`. This half MEASURES — the all-quantum L4 goldens, the
 * tree-alone composition check and the two palindromes; the .c ASSERTS the
 * restated levels L1/L2/L3 over random draws and the known-index path. The §9
 * promotion identity stays in the .c because it reads the answer as well as the
 * count. Included by the .c and inheriting its includes and fixture; out of
 * check_loc.sh's reach by the EXTENSION rule.
 */
/* ---- L4, and the composition. ------------------------------------------- */

static void measure(int count, int W, cq_counter *push, cq_counter *pop)
{
    uint64_t contents[64];
    fix f;

    for (int j = 0; j < count; j++) contents[j] = (uint64_t)(j * 37 + 11) & wmask(W);
    fx_open_allq(&f, count, W, contents, 5u);
    fx_push(&f); *push = f.cnt;
    cq_count_reset(&f.cnt);
    fx_pop(&f);  *pop = f.cnt;
    fx_close(&f);
}

static void tree_step(cq_ctx *ctx, void *env, int s)
{
    cq_qtree_step(ctx, (const cq_qtree_block *)env, s);
}

/* The tree ALONE, measured (the same instrument test_kernel_qrom.c carries). */
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

/* K14.md §3: (2, 4(Lp−1), 2(Lp−1) + 3·count·W), or (0, 3W, 0) at count == 1. */
static void closed_form(int count, int W, uint64_t *x, uint64_t *cx, uint64_t *ccx)
{
    const uint64_t lp = (uint64_t)lp_of(count);
    if (count == 1) { *x = 0u; *cx = 3u * (uint64_t)W; *ccx = 0u; return; }
    *x   = 2u;
    *cx  = 4u * (lp - 1u);
    *ccx = 2u * (lp - 1u) + 3u * (uint64_t)count * (uint64_t)W;
}

static const int GOLD_COUNTS[] = { 1, 2, 3, 4, 8, 16 };
static const int GOLD_WS[]     = { 1, 8, 16, 32, 64, 80 };

CQ_TEST(l4_goldens)
{
    cq_gold g;
    char key[32];

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/qstore.counts",
                      "M30 kernels/qstore.c — K14 shadow store at a quantum "
                      "index (tree sandwiched, sweeps in place; key qstore_L<count>)",
                      "ALL-QUANTUM on every cell lane, every index lane and every "
                      "val lane; the index is the ABI's i32; at ctrl_depth 0",
                      CQOPS_BENNETT_COMMIT))
        return;

    for (size_t c = 0; c < sizeof GOLD_COUNTS / sizeof GOLD_COUNTS[0]; c++)
        for (size_t w = 0; w < sizeof GOLD_WS / sizeof GOLD_WS[0]; w++) {
            const int count = GOLD_COUNTS[c], W = GOLD_WS[w];
            cq_counter push, pop;
            uint64_t x, cx, ccx;

            closed_form(count, W, &x, &cx, &ccx);
            measure(count, W, &push, &pop);
            CHECK_GATES(push.x, push.cx, push.ccx, x, cx, ccx);
            CHECK_GATES(pop.x,  pop.cx,  pop.ccx,  x, cx, ccx);
            CHECK_EQ(push.ry + push.rz + push.mz + pop.ry + pop.rz + pop.mz, 0);

            (void)snprintf(key, sizeof key, "qstore_L%d", count);
            cq_gold_check(&g, key, "push", W, push.x, push.cx, push.ccx);
            cq_gold_check(&g, key, "pop",  W, pop.x,  pop.cx,  pop.ccx);
        }
    CHECK(cq_gold_close(&g));
}

CQ_TEST(the_store_is_the_measured_tree_plus_three_toffolis_per_cell_lane)
{
    for (size_t c = 0; c < sizeof GOLD_COUNTS / sizeof GOLD_COUNTS[0]; c++)
        for (size_t w = 0; w < sizeof GOLD_WS / sizeof GOLD_WS[0]; w++) {
            const int count = GOLD_COUNTS[c], W = GOLD_WS[w];
            cq_counter tree, push, pop;

            tree_only(count, &tree);
            measure(count, W, &push, &pop);
            CHECK_EQ(push.x,   tree.x);
            CHECK_EQ(push.cx,  tree.cx + (count == 1 ? 3u * (uint64_t)W : 0u));
            CHECK_EQ(push.ccx, tree.ccx + (count == 1 ? 0u : 3u * (uint64_t)count * (uint64_t)W));
            CHECK_EQ(pop.x, push.x); CHECK_EQ(pop.cx, push.cx); CHECK_EQ(pop.ccx, push.ccx);
        }
}

/* ---- The stream. --------------------------------------------------------- */

CQ_TEST(both_streams_are_palindromes_around_the_sweeps)
{
    static const int CS[] = { 2, 3, 4, 8 };
    cq_mock m; cq_sink s;

    cq_mock_init(&m);
    s = cq_mock_sink(&m);
    for (size_t i = 0; i < sizeof CS / sizeof CS[0]; i++) {
        const int count = CS[i], W = 4, n = cq_qtree_depth(count);
        uint64_t contents[64];
        fix f;

        for (int j = 0; j < count; j++) contents[j] = (uint64_t)(j * 3 + 1) & 0xFu;
        fx_open(&f, count, W, contents, 0xFu, 2u, 0xFFFFFFFFu, 0x9u, 0xFu);
        f.ctx.sink = &s;
        cq_mock_reset(&m);
        fx_push(&f);
        CHECK(cq_mock_is_palindrome(&m, (size_t)cq_qtree_steps(n),
                                    (size_t)cq_qstore_sweep_steps(count, W)));
        cq_mock_reset(&m);
        fx_pop(&f);
        CHECK(cq_mock_is_palindrome(&m, (size_t)cq_qtree_steps(n),
                                    (size_t)cq_qstore_sweep_steps(count, W)));
        /* The pop is the push's copyout REVERSED: the first sweep gate the pop
         * emits is the last one the push emitted. Read off the middle. */
        fx_close(&f);
    }
    cq_mock_dispose(&m);
}

