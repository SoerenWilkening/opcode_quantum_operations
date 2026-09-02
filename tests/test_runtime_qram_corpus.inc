/* tests/test_runtime_qram_corpus.inc — the CORPUS half of tests/test_runtime_qram.c,
 * split on a second seam taken on the first measurement (the .c landed at 314
 * against the 300 wall after the sweep had already moved out): `the ROWS ↔ the
 * CORPUS`. The six integer qram fixtures' call sequences, replayed verbatim from
 * their goldens with real `cqrt_ry` indices — CQ_lang `893b769`, dirty tree —
 * asserting completion and a zero residue. Included by the .c and inheriting its
 * includes and open_with/close_with.
 */
/* -------------------------------------------------------------------------
 * 5. The six corpus shapes, end to end, with real rotations.
 * ------------------------------------------------------------------------- */

static int32_t theta_i32(int v) { int32_t h = cqrt_alloc_i32(v); cqrt_ry_i32(h, 0.5); return h; }

/* slice_qram_store and slice_qram_read: no free at all, so no residue can
 * exist; what is asserted is completion and the pool's accounting. */
CQ_TEST(corpus_qram_store_and_qram_read_run_to_completion)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    int32_t a = cqrt_qram_alloc_i32(8), h1 = theta_i32(0), h2 = theta_i32(1);
    int32_t h3 = cqrt_alloc_i32(7), h4;

    cqrt_qram_store_i32(a, h1, h3);
    h4 = cqrt_qram_load_i32(a, h2);
    (void)cqrt_measure_i32(h4);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    cq_reg_audit(ctx);
    close_with(&m);

    ctx = open_with(&m, &s);
    a = cqrt_qram_alloc_i32(8); h1 = theta_i32(0);
    h2 = cqrt_qram_load_i32(a, h1);
    (void)cqrt_measure_i32(h2);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* slice_qram_local_uncompute, verbatim from the golden. Two frees: `out`
 * after its `_unc`, and the slot inside `store_unc`. Both CLEAN. */
CQ_TEST(corpus_qram_local_uncompute_frees_clean)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    int32_t a = cqrt_qram_alloc_i32(8), h1 = theta_i32(0), h2 = theta_i32(1);
    int32_t h3 = cqrt_alloc_i32(7), h4, h5;

    cqrt_qram_store_i32(a, h1, h3);
    h4 = cqrt_qram_load_i32(a, h2);
    h5 = cqrt_alloc_i32(0);
    cqrt_copy_i32(h4, h5);
    cqrt_qram_load_i32_unc(h4, a, h2);
    cqrt_free(h4);
    cqrt_qram_store_i32_unc(a, h1, h3);
    cqrt_ry_i32(h2, -0.5);
    cqrt_ry_i32(h1, -0.5);
    (void)cqrt_measure_i32(h5);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    CHECK_EQ(cq_reg_stranded_unproven(ctx) + cq_reg_stranded_dirty(ctx), 0);
    CHECK_EQ(cq_qram_find(a)->n_tape, 0u);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* slice_qram_guarded_store, _multi and _swapped: the guarded store with the
 * flag STILL LIVE through the `_unc`, then stripped and freed. Three frees each
 * in the single-store shapes, five in _multi. All CLEAN. */
static void guarded(int multi, cq_shim_pred pred_kind)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    int32_t a = cqrt_qram_alloc_i32(8), h1 = theta_i32(0), h2 = theta_i32(1);
    int32_t h3 = multi ? theta_i32(2) : CQ_REG_NONE;
    int32_t f1, f2 = CQ_REG_NONE, v1, v2 = CQ_REG_NONE, o, c;

    f1 = cq_shim_icmp_hl(pred_kind, 32, h1, 3u, 0u);
    v1 = cqrt_alloc_i32(7);
    cqrt_qram_store_i32_controlled(f1, a, h2, v1);
    if (multi) {
        f2 = cq_shim_icmp_hl(pred_kind, 32, h1, 5u, 0u);
        v2 = cqrt_alloc_i32(8);
        cqrt_qram_store_i32_controlled(f2, a, h3, v2);
    }
    o = cqrt_qram_load_i32(a, h1);
    c = cqrt_alloc_i32(0);
    cqrt_copy_i32(o, c);
    cqrt_qram_load_i32_unc(o, a, h1);
    cqrt_free(o);
    if (multi) {
        cqrt_qram_store_i32_controlled_unc(f2, a, h3, v2);
        cq_shim_icmp_hl_unc(pred_kind, 32, f2, h1, 5u, 0u);
        cqrt_free(f2);
    }
    cqrt_qram_store_i32_controlled_unc(f1, a, h2, v1);
    cq_shim_icmp_hl_unc(pred_kind, 32, f1, h1, 3u, 0u);
    cqrt_free(f1);
    if (multi) cqrt_ry_i32(h3, -0.5);
    cqrt_ry_i32(h2, -0.5);
    cqrt_ry_i32(h1, -0.5);
    (void)cqrt_measure_i32(c);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    CHECK_EQ(cq_qram_find(a)->n_tape, 0u);
    cq_reg_audit(ctx);
    close_with(&m);
}

CQ_TEST(corpus_guarded_store_multi_and_swapped_free_clean)
{
    guarded(0, CQ_SHIM_PRED_SLT);
    guarded(1, CQ_SHIM_PRED_SLT);
    guarded(0, CQ_SHIM_PRED_NE);
}

