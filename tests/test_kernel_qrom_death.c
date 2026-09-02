/* tests/test_kernel_qrom_death.c — M29, Step 27. K13's and the tree's
 * fail-loud paths: the operand check `cq_qram_check` that K13 and K14 share.
 *
 * NO FAIL_REGULAR_EXPRESSION IS OWED HERE beyond the sanitizer tripwire: for
 * every case nothing beneath M29 speaks. M05's distinctness assert compares
 * qubit INDICES and is silent on a classical operand and on two disjoint
 * registers; M09 is not in the picture until the sandwich opens, and every
 * refusal below fires before it does; M07 never sees a cq_bit array. Delete
 * `cq_qram_check` and the index-too-narrow case reads past the index rail,
 * the aliased cases emit a malformed circuit, and `count <= 0` allocates a
 * region of zero bits — all with a right-looking value in Release.
 */

#include "kernels/qrom.h"

#include "bit.h"
#include "ctx.h"
#include "reg.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/death.h"

typedef struct {
    cq_ctx     ctx;
    cq_counter cnt;
    cq_sink    sink;
    int32_t    h_cells[8], h_idx, h_dst;
    cq_bit    *cells[8];
} d_fix;

static void d_open(d_fix *f, int count, int W, uint32_t idx_w)
{
    cq_count_reset(&f->cnt);
    f->sink = cq_sink_counter(&f->cnt);
    cq_ctx_init(&f->ctx, &f->sink);
    for (int j = 0; j < count; j++) {
        f->h_cells[j] = cq_bk_reg(&f->ctx, (uint32_t)W, (uint64_t)j, 0xFFu);
        f->cells[j]   = cq_reg_bits(&f->ctx.regs, f->h_cells[j]);
    }
    f->h_idx = cq_bk_reg(&f->ctx, idx_w, 1u, 0xFFFFFFFFu);
    f->h_dst = cq_reg_alloc_zero(&f->ctx.regs, (uint32_t)W);
}

static void an_index_narrower_than_the_tree(void)
{
    d_fix f;
    d_open(&f, 8, 4, 2u);                        /* 8 cells need 3 lanes */
    CQ_DEATH_REQUIRE(cq_reg_width(&f.ctx.regs, f.h_idx) == 2u);
    CQ_EXPECT_ABORT(cq_kernel_qload(&f.ctx, cq_reg_bits(&f.ctx.regs, f.h_dst),
                                    cq_reg_cbits(&f.ctx.regs, f.h_idx), 2,
                                    (const cq_bit *const *)f.cells, 8, 4));
}

static void a_count_of_zero(void)
{
    d_fix f;
    d_open(&f, 1, 4, 32u);
    CQ_EXPECT_ABORT(cq_kernel_qload(&f.ctx, cq_reg_bits(&f.ctx.regs, f.h_dst),
                                    cq_reg_cbits(&f.ctx.regs, f.h_idx), 32,
                                    (const cq_bit *const *)f.cells, 0, 4));
}

/* The count bound fires BEFORE the cells are looked at, so no array of that
 * size has to exist for the refusal to be reachable. */
static void a_count_beyond_the_bound(void)
{
    d_fix f;
    d_open(&f, 1, 4, 32u);
    CQ_EXPECT_ABORT((void)cq_qtree_depth(CQ_QRAM_COUNT_MAX + 1));
}

/* D7b among the cells: two cells that are one register. Both are CONTROLS in
 * K13, so nothing physical is malformed — which is exactly why only the
 * range check can see it. */
static void two_cells_that_are_one_register(void)
{
    d_fix f;
    d_open(&f, 4, 4, 32u);
    f.cells[2] = f.cells[0];
    CQ_EXPECT_ABORT(cq_kernel_qload(&f.ctx, cq_reg_bits(&f.ctx.regs, f.h_dst),
                                    cq_reg_cbits(&f.ctx.regs, f.h_idx), 32,
                                    (const cq_bit *const *)f.cells, 4, 4));
}

/* D7a: dst is a cell — `dst ^= mem[idx]` with dst inside mem. */
static void dst_is_a_cell(void)
{
    d_fix f;
    d_open(&f, 4, 4, 32u);
    CQ_EXPECT_ABORT(cq_kernel_qload(&f.ctx, f.cells[1],
                                    cq_reg_cbits(&f.ctx.regs, f.h_idx), 32,
                                    (const cq_bit *const *)f.cells, 4, 4));
}

/* D7a on the index, and PARTIAL: dst's lanes 0-3 are the index rail's lanes
 * 4-7, which a base-pointer comparison would pass. */
static void dst_overlaps_the_index(void)
{
    d_fix f;
    d_open(&f, 4, 4, 32u);
    CQ_EXPECT_ABORT(cq_kernel_qload(&f.ctx, cq_reg_bits(&f.ctx.regs, f.h_idx) + 4,
                                    cq_reg_cbits(&f.ctx.regs, f.h_idx), 32,
                                    (const cq_bit *const *)f.cells, 4, 4));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(an_index_narrower_than_the_tree),
    CQ_DEATH_CASE(a_count_of_zero),
    CQ_DEATH_CASE(a_count_beyond_the_bound),
    CQ_DEATH_CASE(two_cells_that_are_one_register),
    CQ_DEATH_CASE(dst_is_a_cell),
    CQ_DEATH_CASE(dst_overlaps_the_index)
)
