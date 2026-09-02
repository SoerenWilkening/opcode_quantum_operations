/* tests/test_kernel_qstore.c — M30, Step 27 (v1.2, PRD §15 D24). K14, the
 * shadow store at a quantum index.
 *
 * HAND-ROLLED, FOR K8's REASON. The shared driver's four levels are stated
 * for Rule 7 — `dst ^= f`, sources unchanged, uncompute is a SECOND CALL —
 * and K14 satisfies none of them: it writes the cells and the tape slot IN
 * PLACE, and its inverse is the REVERSE CIRCUIT. So every level is restated:
 *
 *   L1  after PUSH: cell[j'] == val, tape == the old cell[j'], every OTHER
 *       cell unchanged, idx and val unchanged; at a padded index NOTHING
 *       moves and the slot stays 0.
 *   L2  the live set is exactly cells ∪ idx ∪ val ∪ tape, after the push and
 *       after the pop — a SET; the slot's qubits stay held after the pop
 *       (releasing them is the shim's, through D15's certificate).
 *   L3  is POP, not a second push: every cell restored and the slot back to
 *       0 on VALUES, at every mask (Rule 14: never on kinds).
 *   L4  qstore.counts, push and pop pinned separately.
 *   L5  DOES NOT APPLY (K14.md header note) — a store is a store; what is
 *       asserted instead is the known-index path: Bennett's unguarded
 *       `emit_shadow_store!`, 3W CX at most, no tree, no scratch.
 *   §9  the promotion identity through cq_ctrl_push, plus rows ZERO and ONE.
 *
 * THE ONE-SLOT DELTA (K14.md §5 delta 1) IS WHAT L1 AND L3 TOGETHER PIN:
 * with `count` cells and ONE W-bit slot, sweep 0 must land exactly cell[j']
 * in the slot and sweep 1 must clear exactly that cell. A tree with two leaves
 * set — the K13 wiring fault — lands the XOR of two cells in the slot and
 * corrupts a second cell, and only the per-cell VALUE checks below see it.
 */

#include "kernels/qstore.h"

#include "bit.h"
#include "controlled.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/qrom.h"
#include "qubits.h"
#include "reg.h"
#include "sandwich.h"
#include "scratch.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/goldens.h"
#include "support/harness.h"
#include "support/kerneldrv.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include <stdio.h>

/* cq_bk_reg refuses W > 64 (it is the one-word form); the cells reach i80. */
static int32_t bk(cq_ctx *ctx, uint32_t W, uint64_t v, uint64_t q)
{
    return cq_bk_reg_w(ctx, W, cq_ref_w_make(v, 0u, (int)W),
                       cq_ref_w_make(q, 0u, (int)W));
}

/* ALL-QUANTUM at any width — the L4 mask. `wmask(W)` is a 64-bit VALUE mask
 * and would leave lanes 64..79 of an i80 cell classical, folding a fifth of
 * the sweep away and pinning a golden at the wrong mask. */
static int32_t bk_all(cq_ctx *ctx, uint32_t W, uint64_t v)
{
    return cq_bk_reg_w(ctx, W, cq_ref_w_make(v, 0u, (int)W), cq_ref_w_ones((int)W));
}

typedef struct {
    cq_ctx     ctx;
    cq_counter cnt;
    cq_sink    sink;
    int        count, W;
    int32_t    h_cells[64], h_idx, h_val, h_tape;
    cq_bit    *cells[64];
} fix;

static uint64_t wmask(int W) { return W >= 64 ? ~(uint64_t)0 : ((uint64_t)1 << W) - 1u; }
static int lp_of(int count) { return 1 << cq_qtree_depth(count); }

static void fx_open(fix *f, int count, int W, const uint64_t *contents,
                    uint64_t qcells, uint64_t idxv, uint64_t qidx,
                    uint64_t valv, uint64_t qval)
{
    cq_count_reset(&f->cnt);
    f->sink = cq_sink_counter(&f->cnt);
    cq_ctx_init(&f->ctx, &f->sink);
    f->count = count; f->W = W;
    for (int j = 0; j < count; j++) {
        f->h_cells[j] = bk(&f->ctx, (uint32_t)W, contents[j], qcells);
        f->cells[j]   = cq_reg_bits(&f->ctx.regs, f->h_cells[j]);
    }
    f->h_idx  = cq_bk_reg(&f->ctx, 32u, idxv, qidx);
    f->h_val  = bk(&f->ctx, (uint32_t)W, valv, qval);
    f->h_tape = cq_reg_alloc_zero(&f->ctx.regs, (uint32_t)W);
    cq_count_reset(&f->cnt);
}

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
    f->h_idx  = cq_bk_reg(&f->ctx, 32u, idxv, 0xFFFFFFFFu);
    f->h_val  = bk_all(&f->ctx, (uint32_t)W, 0xA5u);
    f->h_tape = cq_reg_alloc_zero(&f->ctx.regs, (uint32_t)W);
    cq_count_reset(&f->cnt);
}

static void fx_push(fix *f)
{
    cq_kernel_qstore_push(&f->ctx, f->cells, f->count,
                          cq_reg_cbits(&f->ctx.regs, f->h_idx), 32,
                          cq_reg_cbits(&f->ctx.regs, f->h_val),
                          cq_reg_bits(&f->ctx.regs, f->h_tape), f->W);
}

static void fx_pop(fix *f)
{
    cq_kernel_qstore_pop(&f->ctx, f->cells, f->count,
                         cq_reg_cbits(&f->ctx.regs, f->h_idx), 32,
                         cq_reg_cbits(&f->ctx.regs, f->h_val),
                         cq_reg_bits(&f->ctx.regs, f->h_tape), f->W);
}

static void fx_close(fix *f) { cq_ctx_dispose(&f->ctx); }

static int fx_live_is_exactly(fix *f)
{
    int32_t hs[68];
    for (int j = 0; j < f->count; j++) hs[j] = f->h_cells[j];
    hs[f->count] = f->h_idx; hs[f->count + 1] = f->h_val; hs[f->count + 2] = f->h_tape;
    return cq_pc_live_is_exactly(&f->ctx, hs, (uint32_t)f->count + 3u);
}

/* ---- L1 + L2 + L3, over random (idx, contents, val, masks). ------------- */

static const struct { int count, W; } PAIRS[] = {
    { 1, 1 }, { 1, 8 }, { 2, 1 }, { 2, 16 }, { 3, 8 }, { 4, 4 }, { 4, 32 },
    { 5, 8 }, { 8, 8 }, { 8, 32 }, { 16, 8 }, { 3, 64 },
};
#define N_PAIRS (sizeof PAIRS / sizeof PAIRS[0])

static void one_draw(int count, int W, uint64_t idxv, uint64_t qidx,
                     const uint64_t *contents, uint64_t qcells,
                     uint64_t valv, uint64_t qval)
{
    const int j = (int)(idxv & (uint64_t)(lp_of(count) - 1));
    const int hit = j < count;
    fix f;

    fx_open(&f, count, W, contents, qcells, idxv, qidx, valv, qval);
    fx_push(&f);

    /* L1 after the push, cell by cell. */
    for (int k = 0; k < count; k++)
        CHECK_EQ(cq_pc_value(&f.ctx, f.h_cells[k]),
                 (hit && k == j) ? valv : contents[k]);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_tape), hit ? contents[j] : 0u);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_idx), idxv);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_val), valv);
    CHECK(fx_live_is_exactly(&f));
    cq_reg_audit(&f.ctx);

    /* L3: the pop, on VALUES. */
    fx_pop(&f);
    for (int k = 0; k < count; k++)
        CHECK_EQ(cq_pc_value(&f.ctx, f.h_cells[k]), contents[k]);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_tape), 0u);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_idx), idxv);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_val), valv);
    CHECK(fx_live_is_exactly(&f));
    cq_reg_audit(&f.ctx);
    fx_close(&f);
}

static uint64_t seed_for(const char *name, int count, int W)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (; *name; name++) { h ^= (uint8_t)*name; h *= UINT64_C(0x100000001b3); }
    return h ^ ((uint64_t)count << 8) ^ (uint64_t)W;
}

CQ_TEST(k14_push_then_pop_over_random_draws)
{
    const int n = cq_kd_samples();

    for (size_t p = 0; p < N_PAIRS; p++) {
        const int count = PAIRS[p].count, W = PAIRS[p].W;
        const uint64_t seed = seed_for("qstore", count, W);
        cq_bk_rng rng;

        cq_bk_rng_init(&rng, seed);
        printf("# qstore count=%d W=%d seed=0x%016llx samples=%d\n", count, W,
               (unsigned long long)seed, n);
        for (int i = 0; i < n; i++) {
            uint64_t contents[64], mask = wmask(W);
            uint64_t idxv = cq_bk_rng_next(&rng) & 0xFFFFFFFFu;
            uint64_t qidx = cq_bk_rng_next(&rng) & 0xFFFFFFFFu;
            uint64_t qcells = cq_bk_rng_next(&rng) & mask;
            uint64_t valv = cq_bk_rng_next(&rng) & mask;
            uint64_t qval = cq_bk_rng_next(&rng) & mask;

            for (int k = 0; k < count; k++) contents[k] = cq_bk_rng_next(&rng) & mask;
            /* The anchors, inside the budget: all-classical, all-quantum, a
             * padded index (where one exists) at all-quantum, an in-range one. */
            if (i == 0) { qidx = 0u; qcells = 0u; qval = 0u; }
            if (i == 1) { qidx = 0xFFFFFFFFu; qcells = mask; qval = mask; }
            if (i == 2) { qidx = 0xFFFFFFFFu; qcells = mask; qval = mask;
                          idxv = (uint64_t)(lp_of(count) - 1); }
            if (i == 3) { qidx = 0xFFFFFFFFu; qcells = mask; qval = mask;
                          idxv = (uint64_t)(count - 1); }
            one_draw(count, W, idxv, qidx, contents, qcells, valv, qval);
        }
    }
}

#include "test_kernel_qstore_measure.inc"

/* A KNOWN INDEX IS BENNETT'S UNGUARDED STORE ON ONE CELL: 3W CX at all-quantum
 * cells, no Toffoli, no tree, no scratch. All-classical everything folds most
 * of that away — but a store is a store, and the cell that changes VALUE
 * changes it. A padded known index moves nothing. */
CQ_TEST(a_classical_index_is_the_unguarded_shadow_store_on_one_cell)
{
    uint64_t contents[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    fix f;

    fx_open(&f, 8, 8, contents, 0xFFu, 5u, 0u, 0x5Cu, 0xFFu);
    fx_push(&f);
    CHECK_GATES(f.cnt.x, f.cnt.cx, f.cnt.ccx, 0u, 24u, 0u);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_cells[5]), 0x5Cu);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_tape), 6u);
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), 8u * 8u + 8u + 8u);   /* no scratch */
    fx_pop(&f);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_cells[5]), 6u);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_tape), 0u);
    fx_close(&f);

    fx_open(&f, 8, 8, contents, 0u, 5u, 0u, 0x5Cu, 0u);
    fx_push(&f);
    CHECK_EQ(cq_count_total(&f.cnt), 0u);
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), 0u);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_cells[5]), 0x5Cu);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_tape), 6u);
    fx_pop(&f);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_cells[5]), 6u);
    fx_close(&f);

    fx_open(&f, 3, 8, contents, 0xFFu, 3u, 0u, 0x5Cu, 0xFFu);   /* padded */
    fx_push(&f);
    CHECK_EQ(cq_count_total(&f.cnt), 0u);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_tape), 0u);
    fx_close(&f);
}

/* ---- §9: the promotion identity through cq_ctrl_push, and rows ZERO/ONE. - */

CQ_TEST(the_promotion_identity_holds_and_rows_zero_and_one_do_what_they_say)
{
    static const int CS[] = { 1, 2, 3, 8 };

    for (size_t i = 0; i < sizeof CS / sizeof CS[0]; i++) {
        const int count = CS[i], W = 8;
        uint64_t contents[64];
        cq_counter plain;
        fix f;
        int32_t h_flag;

        for (int j = 0; j < count; j++) contents[j] = (uint64_t)(j * 5 + 2) & 0xFFu;
        {
            cq_counter push, pop;
            measure(count, W, &push, &pop);   /* the push, uncontrolled */
            plain = push;
        }

        /* Q: (x, cx, ccx) -> (0, x, cx + 3·ccx), and the answer follows the flag. */
        fx_open(&f, count, W, contents, 0xFFu, 1u, 0xFFFFFFFFu, 0x77u, 0xFFu);
        h_flag = cq_bk_reg(&f.ctx, 1u, 1u, 1u);
        cq_count_reset(&f.cnt);
        cq_ctrl_push(&f.ctx, cq_reg_cbits(&f.ctx.regs, h_flag));
        fx_push(&f);
        cq_ctrl_pop(&f.ctx);
        CHECK_GATES(f.cnt.x, f.cnt.cx, f.cnt.ccx, 0u, plain.x, plain.cx + 3u * plain.ccx);
        {
            const int j = 1 & (lp_of(count) - 1);
            CHECK_EQ(cq_pc_value(&f.ctx, f.h_cells[j]), j < count ? 0x77u : contents[j]);
        }
        cq_ctrl_push(&f.ctx, cq_reg_cbits(&f.ctx.regs, h_flag));
        fx_pop(&f);
        cq_ctrl_pop(&f.ctx);
        for (int k = 0; k < count; k++) CHECK_EQ(cq_pc_value(&f.ctx, f.h_cells[k]), contents[k]);
        CHECK_EQ(cq_pc_value(&f.ctx, f.h_tape), 0u);
        fx_close(&f);

        /* ZERO: nothing, and nothing moves. */
        fx_open(&f, count, W, contents, 0xFFu, 1u, 0xFFFFFFFFu, 0x77u, 0xFFu);
        h_flag = cq_bk_reg(&f.ctx, 1u, 0u, 0u);
        cq_count_reset(&f.cnt);
        cq_ctrl_push(&f.ctx, cq_reg_cbits(&f.ctx.regs, h_flag));
        fx_push(&f);
        cq_ctrl_pop(&f.ctx);
        CHECK_EQ(cq_count_total(&f.cnt), 0u);
        for (int k = 0; k < count; k++) CHECK_EQ(cq_pc_value(&f.ctx, f.h_cells[k]), contents[k]);
        fx_close(&f);

        /* ONE: verbatim. */
        fx_open(&f, count, W, contents, 0xFFu, 1u, 0xFFFFFFFFu, 0x77u, 0xFFu);
        h_flag = cq_bk_reg(&f.ctx, 1u, 1u, 0u);
        cq_count_reset(&f.cnt);
        cq_ctrl_push(&f.ctx, cq_reg_cbits(&f.ctx.regs, h_flag));
        fx_push(&f);
        cq_ctrl_pop(&f.ctx);
        CHECK_GATES(f.cnt.x, f.cnt.cx, f.cnt.ccx, plain.x, plain.cx, plain.ccx);
        fx_close(&f);
    }
}

CQ_TEST_MAIN_ARGV(
    CQ_CASE(k14_push_then_pop_over_random_draws),
    CQ_CASE(l4_goldens),
    CQ_CASE(the_store_is_the_measured_tree_plus_three_toffolis_per_cell_lane),
    CQ_CASE(both_streams_are_palindromes_around_the_sweeps),
    CQ_CASE(a_classical_index_is_the_unguarded_shadow_store_on_one_cell),
    CQ_CASE(the_promotion_identity_holds_and_rows_zero_and_one_do_what_they_say)
)
