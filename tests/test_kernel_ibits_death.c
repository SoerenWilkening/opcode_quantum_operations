/* tests/test_kernel_ibits_death.c — M41's fail-loud paths. Every refusal in
 * src/kernels/ibits*.c that a caller can reach is driven DIRECTLY here, one
 * case each; which lower layer could otherwise answer for it, and why the
 * regex names that layer, is in tests/CMakeLists.txt beside the registration.
 *
 * FOUR REFUSALS HAVE NO CASE, and each has an executed reachability claim
 * instead (bd memories a-guard-that-reads-its-own-table-has-no-death-case):
 *   - "a program outgrew CQ_IBITS_MAX_ROWS": the builders' largest program is
 *     ctlz/cttz at W = 128, which the_program_tables_are_well_formed_at_every_
 *     width pins at EXACTLY the bound;
 *   - "the step dispatch fell off the end": masked by the step-index guard
 *     above it by construction — named ABSENT in the step-index regex so the
 *     guard's case is a claim about the guard;
 *   - "emit of an unknown op": the dispatch's op_of refuses first;
 *   - "fold of an unknown intrinsic": the kernel's program builder refuses the
 *     same selector before the fold is reached.
 */

#include "kernels/ibits.h"

#include "bit.h"
#include "ctx.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "reg.h"
#include "scratch.h"
#include "support/bitkinds.h"
#include "support/death.h"

static cq_sink        g_sink;
static cq_ctx         g_ctx;
static cq_scratch     g_scr;
static cq_ibits_row   g_rows[CQ_IBITS_MAX_ROWS];
static cq_bit         g_a[CQ_REG_WIDTH_MAX];

static void setup(void)
{
    g_sink = cq_death_null_sink();
    cq_ctx_init(&g_ctx, &g_sink);
    for (int i = 0; i < (int)CQ_REG_WIDTH_MAX; i++) g_a[i] = cq_bit_zero();
}

/* A block over a program of `f` at W, in a region of exactly its size plus
 * `slack` bits, at offset `off`. Nothing is materialised: a death case needs
 * only to reach the refusal. */
static cq_ibits_block bind(cq_ibits_fn f, int W, int slack, uint32_t off)
{
    cq_ibits_block k;
    int n = cq_ibits_program(f, W, g_rows);
    int64_t size = (int64_t)cq_ibits_region(g_rows, n, W) + slack;

    cq_scratch_alloc(&g_scr, (uint32_t)size);
    k.a = g_a; k.scr = &g_scr; k.off = off;
    k.rows = g_rows; k.n_rows = n; k.W = W;
    return k;
}

/* ---- The width door. ---------------------------------------------------- */

static void bswap_kernel_at_a_width_that_is_not_a_multiple_of_16(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0x5Au, 0xFFu);
    int32_t hd = cq_bk_reg(&g_ctx, 8u, 0x00u, 0x00u);

    CQ_EXPECT_ABORT(cq_kernel_bswap(&g_ctx, cq_reg_bits(&g_ctx.regs, hd),
                                    cq_reg_cbits(&g_ctx.regs, ha), 8));
}

static void bswap_program_at_w24(void)
{
    CQ_EXPECT_ABORT((void)cq_ibits_program(CQ_IBF_BSWAP, 24, g_rows));
}

static void ctpop_kernel_at_width_zero(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_ctpop(&g_ctx, g_a + 1, g_a, 0));
}

static void ctlz_program_above_the_register_maximum(void)
{
    CQ_EXPECT_ABORT((void)cq_ibits_program(CQ_IBF_CTLZ,
                                           (int)CQ_REG_WIDTH_MAX + 1, g_rows));
}

static void program_of_an_unknown_intrinsic(void)
{
    CQ_EXPECT_ABORT((void)cq_ibits_program(CQ_IBF_N, 8, g_rows));
}

static void program_into_a_null_buffer(void)
{
    CQ_EXPECT_ABORT((void)cq_ibits_program(CQ_IBF_CTPOP, 8, NULL));
}

static void name_of_an_unknown_intrinsic(void)
{
    CQ_EXPECT_ABORT((void)cq_ibits_name(CQ_IBF_N));
}

static void cttz_dst_aliases_its_source(void)
{
    setup();
    int32_t ha = cq_bk_reg(&g_ctx, 8u, 0x10u, 0x00u);   /* all-classical */
    cq_bit *a = cq_reg_bits(&g_ctx.regs, ha);

    CQ_EXPECT_ABORT(cq_kernel_cttz(&g_ctx, a, a, 8));
}

/* ---- The block and the step machine. ------------------------------------ */

static void step_index_past_the_end(void)
{
    setup();
    cq_ibits_block k = bind(CQ_IBF_CTLZ, 4, 0, 0u);

    CQ_EXPECT_ABORT(cq_ibits_step(&g_ctx, &k,
                                  cq_ibits_steps(k.rows, k.n_rows, 4)));
}

static void step_index_is_negative(void)
{
    setup();
    cq_ibits_block k = bind(CQ_IBF_CTLZ, 4, 0, 0u);

    CQ_EXPECT_ABORT(cq_ibits_step(&g_ctx, &k, -1));
}

static void block_region_is_one_bit_short(void)
{
    setup();
    cq_ibits_block k = bind(CQ_IBF_CTPOP, 4, -1, 0u);

    CQ_EXPECT_ABORT(cq_ibits_step(&g_ctx, &k, 0));
}

static void block_offset_runs_off_the_region(void)
{
    setup();
    cq_ibits_block k = bind(CQ_IBF_BITREVERSE, 4, 0, 1u);

    CQ_EXPECT_ABORT(cq_ibits_step(&g_ctx, &k, 0));
}

static void a_block_with_no_region(void)
{
    setup();
    cq_ibits_block k = bind(CQ_IBF_CTPOP, 4, 0, 0u);

    k.scr = NULL;
    CQ_EXPECT_ABORT(cq_ibits_step(&g_ctx, &k, 0));
}

static void result_of_a_program_that_does_not_fit(void)
{
    setup();
    cq_ibits_block k = bind(CQ_IBF_CTPOP, 4, -1, 0u);

    CQ_EXPECT_ABORT((void)cq_ibits_result(&k));
}

static void result_of_a_block_with_no_region(void)
{
    setup();
    cq_ibits_block k = bind(CQ_IBF_CTPOP, 4, 0, 0u);

    k.scr = NULL;
    CQ_EXPECT_ABORT((void)cq_ibits_result(&k));
}

static void result_when_the_last_row_is_not_the_closing_add(void)
{
    setup();
    cq_ibits_block k = bind(CQ_IBF_BITREVERSE, 4, 0, 0u);

    g_rows[k.n_rows - 1].op = (short)CQ_IBOP_OR;
    CQ_EXPECT_ABORT((void)cq_ibits_result(&k));
}

static void a_block_whose_width_is_zero(void)
{
    setup();
    cq_ibits_block k = bind(CQ_IBF_CTPOP, 4, 0, 0u);

    k.W = 0;
    CQ_EXPECT_ABORT(cq_ibits_step(&g_ctx, &k, 0));
}

static void steps_of_a_program_of_no_rows(void)
{
    CQ_EXPECT_ABORT((void)cq_ibits_steps(g_rows, 0, 4));
}

static void row_steps_of_an_unknown_op(void)
{
    cq_ibits_row r;

    r.op = (short)CQ_IBOP_N;
    CQ_EXPECT_ABORT((void)cq_ibits_row_steps(&r, 4));
}

static void row_region_of_an_unknown_op(void)
{
    cq_ibits_row r;

    r.op = (short)CQ_IBOP_N;
    CQ_EXPECT_ABORT((void)cq_ibits_row_region(&r, 4));
}

/* The mutated row's OWN slot is driven (bd memories a-death-case-must-drive-
 * the-mutated-rows-own-slot): ctpop at W = 4 is three `add` rows and the
 * closing one, so row 1 begins at slot cq_add_steps(4). */
static void a_row_with_an_unknown_op(void)
{
    setup();
    cq_ibits_block k = bind(CQ_IBF_CTPOP, 4, 0, 0u);

    g_rows[1].op = (short)CQ_IBOP_N;
    CQ_EXPECT_ABORT(cq_ibits_step(&g_ctx, &k, cq_add_steps(4)));
}

static void an_operand_that_is_not_an_earlier_row(void)
{
    setup();
    cq_ibits_block k = bind(CQ_IBF_CTPOP, 4, 0, 0u);

    g_rows[1].s[0].src = 1;                    /* the row reads itself */
    CQ_EXPECT_ABORT(cq_ibits_step(&g_ctx, &k, cq_add_steps(4)));
}

static void an_unknown_operand_code(void)
{
    setup();
    cq_ibits_block k = bind(CQ_IBF_CTPOP, 4, 0, 0u);

    g_rows[0].s[1].src = -5;
    CQ_EXPECT_ABORT(cq_ibits_step(&g_ctx, &k, 0));
}

static void a_view_field_outside_the_width(void)
{
    setup();
    cq_ibits_block k = bind(CQ_IBF_CTPOP, 4, 0, 0u);

    g_rows[0].s[1].shr = 5;
    CQ_EXPECT_ABORT(cq_ibits_step(&g_ctx, &k, 0));
}

/* ctlz at W = 2: row 0 the seed add, row 1 the first `ne`, row 2 its select. */
static int ctlz2_mux_slot(void)
{
    return cq_add_steps(2) + cq_eq_steps(2);
}

static void a_select_whose_condition_is_not_an_ne_row(void)
{
    setup();
    cq_ibits_block k = bind(CQ_IBF_CTLZ, 2, 0, 0u);

    g_rows[2].s[0].src = 0;                    /* the seed add, not the ne */
    CQ_EXPECT_ABORT(cq_ibits_step(&g_ctx, &k, ctlz2_mux_slot()));
}

static void an_ne_row_read_as_a_w_lane_operand(void)
{
    setup();
    cq_ibits_block k = bind(CQ_IBF_CTLZ, 2, 0, 0u);

    g_rows[2].s[2].src = 1;                    /* the ne row as the f arm */
    CQ_EXPECT_ABORT(cq_ibits_step(&g_ctx, &k, ctlz2_mux_slot()));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(bswap_kernel_at_a_width_that_is_not_a_multiple_of_16),
    CQ_DEATH_CASE(bswap_program_at_w24),
    CQ_DEATH_CASE(ctpop_kernel_at_width_zero),
    CQ_DEATH_CASE(ctlz_program_above_the_register_maximum),
    CQ_DEATH_CASE(program_of_an_unknown_intrinsic),
    CQ_DEATH_CASE(program_into_a_null_buffer),
    CQ_DEATH_CASE(name_of_an_unknown_intrinsic),
    CQ_DEATH_CASE(cttz_dst_aliases_its_source),
    CQ_DEATH_CASE(step_index_past_the_end),
    CQ_DEATH_CASE(step_index_is_negative),
    CQ_DEATH_CASE(block_region_is_one_bit_short),
    CQ_DEATH_CASE(block_offset_runs_off_the_region),
    CQ_DEATH_CASE(a_block_with_no_region),
    CQ_DEATH_CASE(result_of_a_program_that_does_not_fit),
    CQ_DEATH_CASE(result_of_a_block_with_no_region),
    CQ_DEATH_CASE(result_when_the_last_row_is_not_the_closing_add),
    CQ_DEATH_CASE(a_block_whose_width_is_zero),
    CQ_DEATH_CASE(steps_of_a_program_of_no_rows),
    CQ_DEATH_CASE(row_steps_of_an_unknown_op),
    CQ_DEATH_CASE(row_region_of_an_unknown_op),
    CQ_DEATH_CASE(a_row_with_an_unknown_op),
    CQ_DEATH_CASE(an_operand_that_is_not_an_earlier_row),
    CQ_DEATH_CASE(an_unknown_operand_code),
    CQ_DEATH_CASE(a_view_field_outside_the_width),
    CQ_DEATH_CASE(a_select_whose_condition_is_not_an_ne_row),
    CQ_DEATH_CASE(an_ne_row_read_as_a_w_lane_operand)
)
