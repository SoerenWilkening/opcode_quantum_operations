/* tests/test_runtime_qram_sweep.inc — the SWEEP half of tests/test_runtime_qram.c,
 * split on the seam recorded for it: `the SWEEP ↔ the ROWS`. The
 * token-dispatched ABI table, the seeded draw and the two sweep cases. Included
 * by the .c and inheriting its includes and open_with/close_with.
 */

/* --- the ABI, dispatched by TOKEN: nine rows, an fp width a bit pattern --- */

typedef struct {
    const char *tok;
    uint32_t    w;
    int32_t (*alloc)(int32_t);
    int32_t (*load)(int32_t, int32_t);
    void    (*load_unc)(int32_t, int32_t, int32_t);
    void    (*store)(int32_t, int32_t, int32_t);
    void    (*store_unc)(int32_t, int32_t, int32_t);
    void    (*store_c)(int32_t, int32_t, int32_t, int32_t);
    void    (*store_cu)(int32_t, int32_t, int32_t, int32_t);
} abi_row;

#define ROW(T, W) { #T, W##u, cqrt_qram_alloc_##T, cqrt_qram_load_##T,        \
                    cqrt_qram_load_##T##_unc, cqrt_qram_store_##T,            \
                    cqrt_qram_store_##T##_unc, cqrt_qram_store_##T##_controlled,\
                    cqrt_qram_store_##T##_controlled_unc }
static const abi_row ROWS[9] = {
    ROW(i1, 1), ROW(i8, 8), ROW(i16, 16), ROW(i32, 32), ROW(i64, 64),
    ROW(f16, 16), ROW(f32, 32), ROW(f64, 64), ROW(f80, 80),
};
#undef ROW

/* cq_bk_reg refuses W > 64 (it is the one-word form); the cells reach i80. */
static int32_t bk(cq_ctx *ctx, uint32_t W, uint64_t v, uint64_t q)
{
    return cq_bk_reg_w(ctx, W, cq_ref_w_make(v, 0u, (int)W),
                       cq_ref_w_make(q, 0u, (int)W));
}

static const int COUNTS[3] = { 1, 3, 8 };

static uint64_t wmask64(uint32_t w) { return w >= 64u ? ~UINT64_C(0) : ((UINT64_C(1) << w) - 1u); }
static int lp_of(int count) { int n = 0; while ((1 << n) < count) n++; return 1 << n; }

static uint64_t seed_for(const char *name, uint32_t w, int count)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (; *name; name++) { h ^= (uint8_t)*name; h *= UINT64_C(0x100000001b3); }
    return h ^ (uint64_t)w ^ ((uint64_t)count << 16);
}

/* A cell's contents, set by a PHYSICAL copy from a test-built register: the
 * source stays live and is part of L2's set. Values are 64-bit, so at W = 80
 * the top sixteen lanes are 0 — enough for what a store or load can move. */
static int32_t fill_cell(cq_ctx *ctx, int32_t cell, uint32_t w, uint64_t v, uint64_t q)
{
    int32_t src = bk(ctx, w, v, q);
    cq_reg_xor_into(ctx, cell, src);
    return src;
}

static int value_is(cq_ctx *ctx, int32_t h, uint64_t v, uint32_t w)
{
    return cq_ref_w_eq(cq_pc_value_w(ctx, h), cq_ref_w_make(v, 0u, (int)w));
}

/* -------------------------------------------------------------------------
 * One draw of the LOAD: load, check, load_unc, free clean; L2 throughout.
 * ------------------------------------------------------------------------- */

static void load_draw(const abi_row *r, int count, uint64_t idxv, uint64_t qidx,
                      const uint64_t *contents, const uint64_t *qcells)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int j = (int)(idxv & (uint64_t)(lp_of(count) - 1));
    int32_t hs[24], out;
    uint32_t out_idx[128], n_out, n = 0;

    const int32_t a   = r->alloc(count);
    const cq_qram_array *arr = cq_qram_find(a);
    for (int k = 0; k < count; k++) {
        hs[n++] = cq_qram_cell(arr, k);
        hs[n++] = fill_cell(ctx, cq_qram_cell(arr, k), r->w, contents[k], qcells[k]);
    }
    const int32_t idx = cq_bk_reg(ctx, 32u, idxv, qidx);
    hs[n++] = idx;

    out = r->load(a, idx);
    CHECK(value_is(ctx, out, j < count ? contents[j] : 0u, r->w));
    CHECK_EQ(cq_reg_width(&ctx->regs, out), r->w);
    for (int k = 0; k < count; k++)
        CHECK(value_is(ctx, cq_qram_cell(arr, k), contents[k], r->w));
    hs[n] = out;
    CHECK(cq_pc_live_is_exactly(ctx, hs, n + 1u));
    cq_reg_audit(ctx);

    r->load_unc(out, a, idx);
    CHECK(value_is(ctx, out, 0u, r->w));
    n_out = cq_pc_indices(ctx, out, out_idx, 128u);
    cqrt_free(out);
    CHECK(cq_pc_indices_are_free(ctx, out_idx, n_out));
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    CHECK(cq_pc_live_is_exactly(ctx, hs, n));
    cq_reg_audit(ctx);
    close_with(&m);
}

/* One draw of the STORE: store, check every cell and the slot, store_unc,
 * every cell restored and the slot released CLEAN; under a set quantum flag
 * when `ctrl`. */
static void store_draw(const abi_row *r, int count, uint64_t idxv, uint64_t qidx,
                       const uint64_t *contents, const uint64_t *qcells,
                       uint64_t valv, uint64_t qval, int ctrl)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int j = (int)(idxv & (uint64_t)(lp_of(count) - 1));
    const int hit = j < count;
    int32_t hs[26], slot, flag = CQ_REG_NONE;
    uint32_t sl_idx[128], n_sl, n = 0;

    const int32_t a   = r->alloc(count);
    const cq_qram_array *arr = cq_qram_find(a);
    for (int k = 0; k < count; k++) {
        hs[n++] = cq_qram_cell(arr, k);
        hs[n++] = fill_cell(ctx, cq_qram_cell(arr, k), r->w, contents[k], qcells[k]);
    }
    const int32_t idx = cq_bk_reg(ctx, 32u, idxv, qidx);
    const int32_t val = bk(ctx, r->w, valv, qval);
    hs[n++] = idx; hs[n++] = val;
    if (ctrl) { flag = cq_bk_reg(ctx, 1u, 1u, 1u); hs[n++] = flag; }

    if (ctrl) r->store_c(flag, a, idx, val); else r->store(a, idx, val);
    slot = cq_qram_top(arr)->slot;
    for (int k = 0; k < count; k++)
        CHECK(value_is(ctx, cq_qram_cell(arr, k),
                       (hit && k == j) ? valv : contents[k], r->w));
    CHECK(value_is(ctx, slot, hit ? contents[j] : 0u, r->w));
    CHECK(value_is(ctx, idx, idxv, 32u));
    CHECK(value_is(ctx, val, valv, r->w));
    hs[n] = slot;
    CHECK(cq_pc_live_is_exactly(ctx, hs, n + 1u));
    CHECK_EQ(cq_ctrl_depth(ctx), 0);
    cq_reg_audit(ctx);
    n_sl = cq_pc_indices(ctx, slot, sl_idx, 128u);

    if (ctrl) r->store_cu(flag, a, idx, val); else r->store_unc(a, idx, val);
    CHECK(cq_qram_top(arr) == NULL);
    for (int k = 0; k < count; k++)
        CHECK(value_is(ctx, cq_qram_cell(arr, k), contents[k], r->w));
    CHECK(cq_pc_indices_are_free(ctx, sl_idx, n_sl));
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    CHECK(cq_pc_live_is_exactly(ctx, hs, n));
    cq_reg_audit(ctx);
    close_with(&m);
}

/* THE SWEEP: cq_kd_samples() draws per (token, count), each an (idx, contents,
 * val, masks) tuple from one seeded RNG, with the anchors INSIDE the budget:
 * all-classical, all-quantum, the padded index where one exists, the top
 * in-range index. Tokens and counts are enumerated, never sampled. */
static void sweep(const char *name, int what)
{
    const int n = cq_kd_samples();

    for (size_t ri = 0; ri < 9u; ri++)
        for (size_t ci = 0; ci < 3u; ci++) {
            const abi_row *r = &ROWS[ri];
            const int count = COUNTS[ci];
            const uint64_t mask = wmask64(r->w);
            const uint64_t seed = seed_for(name, r->w, count) ^ (uint64_t)ri;
            cq_bk_rng rng;

            cq_bk_rng_init(&rng, seed);
            printf("# %s %s count=%d seed=0x%016llx samples=%d\n", name, r->tok,
                   count, (unsigned long long)seed, n);
            for (int i = 0; i < n; i++) {
                uint64_t contents[8], qcells[8];
                uint64_t idxv = cq_bk_rng_next(&rng) & 0xFFFFFFFFu;
                uint64_t qidx = cq_bk_rng_next(&rng) & 0xFFFFFFFFu;
                uint64_t valv = cq_bk_rng_next(&rng) & mask;
                uint64_t qval = cq_bk_rng_next(&rng) & mask;

                for (int k = 0; k < count; k++) {
                    contents[k] = cq_bk_rng_next(&rng) & mask;
                    qcells[k]   = cq_bk_rng_next(&rng) & mask;
                }
                if (i == 0) { qidx = 0u; qval = 0u; for (int k = 0; k < count; k++) qcells[k] = 0u; }
                if (i == 1) { qidx = 0xFFFFFFFFu; qval = mask; for (int k = 0; k < count; k++) qcells[k] = mask; }
                if (i == 2) { qidx = 0xFFFFFFFFu; idxv = (uint64_t)(lp_of(count) - 1); }
                if (i == 3) { qidx = 0xFFFFFFFFu; idxv = (uint64_t)(count - 1); }

                if (what == 0)      load_draw(r, count, idxv, qidx, contents, qcells);
                else                store_draw(r, count, idxv, qidx, contents, qcells,
                                               valv, qval, what == 2);
            }
        }
}

CQ_TEST(the_load_reads_the_cell_at_the_truncated_index_over_random_draws)
{
    sweep("qram_load", 0);
}

CQ_TEST(the_store_moves_the_old_cell_onto_the_slot_and_the_pop_restores_it)
{
    sweep("qram_store", 1);
    sweep("qram_store_controlled", 2);
}
