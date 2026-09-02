/* tests/test_kernel_qrom.c — M29, Step 27 (v1.2, PRD §15 D24). K13, the QROM
 * read at a quantum index, and the unary-iteration tree it exports.
 *
 * TWO SHAPES OF CELL, AND THE DRIVER SERVES ONE OF THEM. Through the shared
 * driver the cells are ONE `count·W`-bit source that the `call` adapter carves
 * into `count` pointers — so that path is bounded by the 128-bit reference and
 * covers `count·W <= 128`, which is where the L1 sample budget, L2's set, L3
 * and the §9 regions run. The CORPUS shape is (8, 32) = 256 bits and every
 * fp-width shape is wider, so those are measured OUTSIDE the driver on real
 * per-cell registers: the L4 goldens, the composition check, the palindrome
 * and one L1 case at the corpus shape all build cells the way the shim does.
 *
 * WHAT NO COUNT CAN SEE HERE is the INDEX WIRING: a tree that reads lane
 * n−1−d at depth d (Bennett's `bit_level`) and one that reads lane d emit the
 * identical tuple, keep the palindrome and leave scratch clean — they differ
 * only in which cell a given index selects, which is L1 against a plain-C
 * reference and nothing else. That is K9's `uge`-meaning-`ule` in K13's form.
 *
 * THE COMPOSITION CHECK READS NO GOLDEN (the K11 lesson): it asks the tree
 * block what it costs under a counting sink and pins the load against THAT
 * plus `count·W` Toffolis, so the K11-shaped "optimisation" — pruning the
 * padded subtrees, or the leaves of a subtree no cell lives under — goes red
 * here whatever CQOPS_UPDATE_GOLDENS=1 blesses.
 */

#include "kernels/qrom.h"

#include "bit.h"
#include "controlled.h"
#include "ctx.h"
#include "emit.h"
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

/* --- the spec, per count (M13's file-static precedent for a second shape
 * parameter the driver cannot carry) --------------------------------------- */

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

static int g_count = 4;

static int lp_of(int count) { return 1 << cq_qtree_depth(count); }

static void qload_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->n_src = 2;
    out->w[0]  = 32;              /* the index: the ABI's i32                 */
    out->w[1]  = g_count * W;     /* the cells, contiguous, cell j at j·W     */
    out->w_dst = W;
}

static void call_qload(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                       const cq_kd_shape *sh)
{
    const cq_bit *cells[128];
    const int W = sh->w_dst;

    for (int j = 0; j < g_count; j++) cells[j] = src[1] + j * W;
    cq_kernel_qload(ctx, dst, src[0], sh->w[0], cells, g_count, W);
}

/* Plain C indexing at the TRUNCATED index, zero past `count` — and not the
 * kernel's own leaf-flag formulation, so a wrong tree can disagree with it. */
static cq_ref_w ref_qload(const cq_ref_w *s, const cq_kd_shape *sh)
{
    const int W = sh->w_dst;
    const int j = (int)(s[0].lo & (uint64_t)(lp_of(g_count) - 1));
    cq_ref_w cell;

    if (j >= g_count) return cq_ref_w_zero();
    cell = cq_ref_w_lshr(s[1], j * W, sh->w[1]);
    return cq_ref_w_make(cell.lo, cell.hi, W);
}

static const cq_kd_spec QLOAD = { "qload", NULL, NULL, qload_shape, call_qload,
                                  ref_qload };

/* (count, W) pairs with count·W <= 128 — the driver's reach. Counts include a
 * non-power-of-two (3) so the padded leaf and the truncated index are drawn. */
static const struct { int count, W; } DRIVER_PAIRS[] = {
    { 1, 1 }, { 1, 8 }, { 1, 32 }, { 1, 64 },
    { 2, 1 }, { 2, 8 }, { 2, 32 }, { 2, 64 },
    { 3, 1 }, { 3, 4 }, { 3, 8 },  { 3, 32 },
    { 4, 1 }, { 4, 8 }, { 4, 16 }, { 4, 32 },
    { 8, 1 }, { 8, 4 }, { 8, 8 },  { 8, 16 },
    { 16, 1 }, { 16, 4 }, { 16, 8 },
};
#define N_DRIVER_PAIRS (sizeof DRIVER_PAIRS / sizeof DRIVER_PAIRS[0])

/* ---- L1 + L2 + L3 + L5, the shared constant sample budget. -------------- */

CQ_TEST(k13_sweep_through_the_driver)
{
    for (size_t i = 0; i < N_DRIVER_PAIRS; i++) {
        g_count = DRIVER_PAIRS[i].count;
        printf("# qload count=%d\n", g_count);
        cq_kd_sample_at(&QLOAD, DRIVER_PAIRS[i].W);
    }
}

#include "test_kernel_qrom_cells.inc"

/* ---- The corpus shape, on real cells: L1, L2, L3 by hand. --------------- */

CQ_TEST(the_corpus_shape_reads_the_right_cell_and_comes_back_clean)
{
    static const struct { int count, W; } SHAPES[] = { { 8, 32 }, { 3, 64 }, { 16, 64 } };
    cq_bk_rng rng;

    cq_bk_rng_init(&rng, UINT64_C(0x9c0d27) ^ 0u);
    for (size_t s = 0; s < sizeof SHAPES / sizeof SHAPES[0]; s++) {
        const int count = SHAPES[s].count, W = SHAPES[s].W;
        for (int draw = 0; draw < 6; draw++) {
            uint64_t contents[64], idxv = cq_bk_rng_next(&rng) & 0xFFFFFFFFu;
            const uint64_t qidx = draw == 0 ? 0u : 0xFFFFFFFFu;
            const int j = (int)(idxv & (uint64_t)(lp_of(count) - 1));
            int32_t hs[68]; uint32_t dst_idx[128], n_dst;
            fix f;

            for (int k = 0; k < count; k++) contents[k] = cq_bk_rng_next(&rng) & wmask(W);
            fx_open(&f, count, W, contents, wmask(W), idxv, qidx);
            fx_load(&f);
            CHECK_EQ(cq_pc_value(&f.ctx, f.h_dst), j < count ? contents[j] : 0u);
            for (int k = 0; k < count; k++) {
                CHECK_EQ(cq_pc_value(&f.ctx, f.h_cells[k]), contents[k]);
                hs[k] = f.h_cells[k];
            }
            hs[count] = f.h_idx; hs[count + 1] = f.h_dst;
            CHECK(cq_pc_live_is_exactly(&f.ctx, hs, (uint32_t)count + 2u));
            cq_reg_audit(&f.ctx);

            fx_load(&f);                                          /* _unc */
            CHECK_EQ(cq_pc_value(&f.ctx, f.h_dst), 0u);
            CHECK(cq_pc_live_is_exactly(&f.ctx, hs, (uint32_t)count + 2u));
            n_dst = cq_pc_indices(&f.ctx, f.h_dst, dst_idx, 128u);
            cq_reg_free(&f.ctx, f.h_dst, cq_pc_zero_proof_rotation_free);
            CHECK(cq_pc_indices_are_free(&f.ctx, dst_idx, n_dst));
            CHECK(cq_pc_live_is_exactly(&f.ctx, hs, (uint32_t)count + 1u));
            fx_close(&f);
        }
    }
}

/* A KNOWN INDEX TAKES BENNETT'S CASE 1: a plain copy, no tree, no scratch —
 * and at an all-classical mask that is zero gates and zero qubits (L5, R9).
 * A padded known index copies nothing. */
CQ_TEST(a_classical_index_is_a_copy_and_a_padded_one_is_nothing)
{
    uint64_t contents[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    fix f;

    fx_open(&f, 8, 8, contents, 0xFFu, 5u, 0u);           /* quantum cells */
    fx_load(&f);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_dst), 6u);
    CHECK_GATES(f.cnt.x, f.cnt.cx, f.cnt.ccx, 0u, 8u, 0u);
    fx_close(&f);

    fx_open(&f, 8, 8, contents, 0u, 5u, 0u);              /* all classical */
    fx_load(&f);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_dst), 6u);
    CHECK_EQ(cq_count_total(&f.cnt), 0u);
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), 0u);
    fx_close(&f);

    fx_open(&f, 3, 8, contents, 0xFFu, 3u, 0u);           /* padded: 3 >= 3 */
    fx_load(&f);
    CHECK_EQ(cq_pc_value(&f.ctx, f.h_dst), 0u);
    CHECK_EQ(cq_count_total(&f.cnt), 0u);
    fx_close(&f);
}

/* K13.md §4: 2·Lp − 1 scratch, all of it returned; owned is W. */
CQ_TEST(the_scratch_is_the_tree_and_it_all_comes_back)
{
    for (size_t i = 0; i < N_DRIVER_PAIRS; i++) {
        const int count = DRIVER_PAIRS[i].count, W = DRIVER_PAIRS[i].W;
        uint32_t peak = 0, owned;

        g_count = count;
        owned = cq_kd_peak(&QLOAD, W, &peak);
        CHECK_EQ(owned, (uint32_t)W);
        CHECK_EQ(peak, (uint32_t)W + (count == 1 ? 0u : (uint32_t)cq_qtree_nodes(cq_qtree_depth(count))));
    }
}

/* ---- Step 20: §9's four regions, and the promotion identity. ------------ */

static void qload_narrow(void)
{
    static const int idx[] = { 0, 4, 8, 12, 16 };   /* into DRIVER_PAIRS */
    for (size_t i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        g_count = DRIVER_PAIRS[idx[i]].count;
        cq_kd_sample_at(&QLOAD, DRIVER_PAIRS[idx[i]].W);
    }
}

CQ_TEST(controlled)
{
    uint64_t reached = 0u;

    cq_kd_for_each_region("qload", qload_narrow);
    for (size_t i = 0; i < N_DRIVER_PAIRS; i++) {
        g_count = DRIVER_PAIRS[i].count;
        reached += cq_kd_check_promotion(&QLOAD, DRIVER_PAIRS[i].W);
    }
    CHECK(reached > 0u);
}

CQ_TEST_MAIN_ARGV(
    CQ_CASE(k13_sweep_through_the_driver),
    CQ_CASE(l4_goldens),
    CQ_CASE(the_load_is_the_measured_tree_plus_one_toffoli_per_cell_lane),
    CQ_CASE(the_stream_is_a_palindrome_around_the_fanout),
    CQ_CASE(the_compute_steps_match_an_independent_derivation),
    CQ_CASE(the_corpus_shape_reads_the_right_cell_and_comes_back_clean),
    CQ_CASE(a_classical_index_is_a_copy_and_a_padded_one_is_nothing),
    CQ_CASE(the_scratch_is_the_tree_and_it_all_comes_back),
    CQ_CASE(controlled)
)
