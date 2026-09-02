/* tests/test_kernel_qstore_death.c — M30, Step 27. K14's own fail-loud paths,
 * beyond the operand check it shares with K13 (test_kernel_qrom_death.c).
 *
 * THE SLOT-NOT-ZERO CASE IS THE ONE THAT MATTERS: a push onto a slot some
 * earlier store never popped XORs the old cell into whatever the slot holds,
 * and the pop then restores the wrong value — right-looking, silent, and
 * exactly the shape D15's certificate would later strand rather than see.
 * Nothing beneath M30 speaks on any of these.
 */

#include "kernels/qstore.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "reg.h"
#include "sink_count.h"

#include "support/bitkinds.h"
#include "support/death.h"

typedef struct {
    cq_ctx     ctx;
    cq_counter cnt;
    cq_sink    sink;
    int32_t    h_cells[8], h_idx, h_val, h_tape;
    cq_bit    *cells[8];
} d_fix;

static void d_open(d_fix *f, int count, int W)
{
    cq_count_reset(&f->cnt);
    f->sink = cq_sink_counter(&f->cnt);
    cq_ctx_init(&f->ctx, &f->sink);
    for (int j = 0; j < count; j++) {
        f->h_cells[j] = cq_bk_reg(&f->ctx, (uint32_t)W, (uint64_t)j + 1u, 0xFFu);
        f->cells[j]   = cq_reg_bits(&f->ctx.regs, f->h_cells[j]);
    }
    f->h_idx  = cq_bk_reg(&f->ctx, 32u, 1u, 0xFFFFFFFFu);
    f->h_val  = cq_bk_reg(&f->ctx, (uint32_t)W, 0x5u, 0xFFu);
    f->h_tape = cq_reg_alloc_zero(&f->ctx.regs, (uint32_t)W);
}

#define PUSH(f, count, W, tape)                                               \
    cq_kernel_qstore_push(&(f).ctx, (f).cells, (count),                       \
                          cq_reg_cbits(&(f).ctx.regs, (f).h_idx), 32,         \
                          cq_reg_cbits(&(f).ctx.regs, (f).h_val), (tape), (W))

static void a_push_onto_a_slot_that_is_not_zero(void)
{
    d_fix f;
    d_open(&f, 4, 4);
    /* One lane of the slot materialised — the kind check, never the shadow. */
    cq_materialise(&f.ctx, &cq_reg_bits(&f.ctx.regs, f.h_tape)[2]);
    CQ_DEATH_REQUIRE(cq_reg_bits(&f.ctx.regs, f.h_tape)[2].kind == CQ_BIT_Q);
    CQ_EXPECT_ABORT(PUSH(f, 4, 4, cq_reg_bits(&f.ctx.regs, f.h_tape)));
}

/* A padded known index still refuses a dirty slot: the check is on entry. */
static void a_push_at_a_padded_index_onto_a_dirty_slot(void)
{
    d_fix f;
    d_open(&f, 3, 4);
    cq_reg_bits(&f.ctx.regs, f.h_idx)[0].kind = CQ_BIT_ONE;   /* idx = 3 */
    cq_reg_bits(&f.ctx.regs, f.h_idx)[1].kind = CQ_BIT_ONE;
    for (int i = 2; i < 32; i++) cq_reg_bits(&f.ctx.regs, f.h_idx)[i].kind = CQ_BIT_ZERO;
    cq_materialise(&f.ctx, &cq_reg_bits(&f.ctx.regs, f.h_tape)[0]);
    CQ_EXPECT_ABORT(PUSH(f, 3, 4, cq_reg_bits(&f.ctx.regs, f.h_tape)));
}

static void the_value_rail_is_the_slot(void)
{
    d_fix f;
    d_open(&f, 4, 4);
    CQ_EXPECT_ABORT(PUSH(f, 4, 4, cq_reg_bits(&f.ctx.regs, f.h_val)));
}

static void the_slot_is_a_cell(void)
{
    d_fix f;
    d_open(&f, 4, 4);
    CQ_EXPECT_ABORT(PUSH(f, 4, 4, f.cells[2]));
}

static void the_index_overlaps_the_value(void)
{
    d_fix f;
    d_open(&f, 4, 4);
    CQ_EXPECT_ABORT(cq_kernel_qstore_push(&f.ctx, f.cells, 4,
                                          cq_reg_cbits(&f.ctx.regs, f.h_idx), 32,
                                          cq_reg_cbits(&f.ctx.regs, f.h_idx) + 8,
                                          cq_reg_bits(&f.ctx.regs, f.h_tape), 4));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(a_push_onto_a_slot_that_is_not_zero),
    CQ_DEATH_CASE(a_push_at_a_padded_index_onto_a_dirty_slot),
    CQ_DEATH_CASE(the_value_rail_is_the_slot),
    CQ_DEATH_CASE(the_slot_is_a_cell),
    CQ_DEATH_CASE(the_index_overlaps_the_value)
)
