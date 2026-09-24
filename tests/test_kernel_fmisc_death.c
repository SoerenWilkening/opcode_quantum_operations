/* tests/test_kernel_fmisc_death.c — M38 K24's fail-loud paths.
 *
 * EACH GUARD IS HERE BECAUSE SOMETHING ELSE WOULD OTHERWISE ANSWER FOR IT OR
 * NOTHING WOULD. Which lower layer would answer is what each group's
 * FAIL_REGULAR_EXPRESSION in tests/CMakeLists.txt pins as forbidden, because a
 * death case's only native claim is "it aborted".
 *
 *   W != 64 (x5)          PRD-v2 §1: f64 only. The sign ops loop over 64
 *                         lanes whatever `W` says, so without the guard a
 *                         wrong `W` RETURNS — carried by the exit code.
 *   D7a (x5) / D7b (x2)   cq_kernel_check_n / _dst at the kernel boundary. A
 *                         sign op with dst == a otherwise reaches the emitter
 *                         with a control coinciding with its target, whose
 *                         `distinctness` abort is pinned absent.
 *   step index (x2)       [0, cq_fmisc_steps). The low side is masked from
 *                         below by M31's class-block range check (row 0 is a
 *                         class block) and the high side by this module's own
 *                         fall-off message — both pinned absent.
 *   region (x4)           the program fits at `off`; masked by M08's
 *                         cq_scratch_span, pinned absent.
 *   no operand / program  a NULL rail and a program outside {MIN, MAX}.
 *   accessors and operand resolution (x8), each driven DIRECTLY through
 *                         fmisc.h / fmisc_int.h with a wild op, index or code
 *                         — the refusals a malformed table would reach, which
 *                         the shipped tables provably cannot (the reachability
 *                         proof is `the_programs_are_well_formed`).
 *
 * BOTH CONFIGURATIONS: every guard is a plain `if` in library code.
 */

#include "kernels/fmisc_int.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "scratch.h"
#include "support/death.h"

static cq_sink    g_sink;
static cq_ctx     g_ctx;
static cq_scratch g_scr;
static cq_bit     g_a[CQ_FP64_W], g_b[CQ_FP64_W], g_d[CQ_FP64_W];
static uint32_t   g_off[CQ_FX_MAX_ROWS];

static void setup(void)
{
    g_sink = cq_death_null_sink();
    cq_ctx_init(&g_ctx, &g_sink);
    for (int i = 0; i < CQ_FP64_W; i++) {
        g_a[i] = cq_bit_zero(); g_b[i] = cq_bit_zero(); g_d[i] = cq_bit_zero();
    }
    /* A quantum lane in each rail, so the fmin/fmax cases reach the sandwich
     * rather than the all-classical row. */
    cq_materialise(&g_ctx, &g_a[0]);
    cq_materialise(&g_ctx, &g_b[0]);
}

static cq_fmisc_block block(uint32_t bits, uint32_t off)
{
    cq_fmisc_block k;

    cq_scratch_alloc(&g_scr, bits);
    k.a = g_a; k.b = g_b; k.scr = &g_scr; k.off = off; k.prog = CQ_FMISC_MIN;
    return k;
}

static cq_fmisc_block full(void)
{
    cq_fmisc_block k = block(cq_fmisc_region(CQ_FMISC_MIN), 0u);

    cq_fx_offsets(&k, g_off);
    return k;
}

/* ---- W != 64. ---------------------------------------------------------- */

static void fneg_width_is_not_64(void)
{ setup(); CQ_EXPECT_ABORT(cq_kernel_fneg(&g_ctx, g_d, g_a, 32)); }
static void fabs_width_is_not_64(void)
{ setup(); CQ_EXPECT_ABORT(cq_kernel_fabs(&g_ctx, g_d, g_a, 16)); }
static void copysign_width_is_not_64(void)
{ setup(); CQ_EXPECT_ABORT(cq_kernel_copysign(&g_ctx, g_d, g_a, g_b, 32)); }
static void fmin_width_is_not_64(void)
{ setup(); CQ_EXPECT_ABORT(cq_kernel_fmin(&g_ctx, g_d, g_a, g_b, 32)); }
static void fmax_width_is_not_64(void)
{ setup(); CQ_EXPECT_ABORT(cq_kernel_fmax(&g_ctx, g_d, g_a, g_b, 63)); }

/* ---- D7a / D7b at the kernel boundary. ---------------------------------- */

static void fneg_dst_aliases_its_source(void)
{ setup(); CQ_EXPECT_ABORT(cq_kernel_fneg(&g_ctx, g_a, g_a, CQ_FP64_W)); }
static void fabs_dst_aliases_its_source(void)
{ setup(); CQ_EXPECT_ABORT(cq_kernel_fabs(&g_ctx, g_a, g_a, CQ_FP64_W)); }
static void copysign_dst_aliases_its_second_source(void)
{ setup(); CQ_EXPECT_ABORT(cq_kernel_copysign(&g_ctx, g_b, g_a, g_b, CQ_FP64_W)); }
static void fmin_dst_aliases_its_first_source(void)
{ setup(); CQ_EXPECT_ABORT(cq_kernel_fmin(&g_ctx, g_a, g_a, g_b, CQ_FP64_W)); }
static void fmax_dst_aliases_its_second_source(void)
{ setup(); CQ_EXPECT_ABORT(cq_kernel_fmax(&g_ctx, g_b, g_a, g_b, CQ_FP64_W)); }
static void copysign_sources_alias(void)
{ setup(); CQ_EXPECT_ABORT(cq_kernel_copysign(&g_ctx, g_d, g_a, g_a, CQ_FP64_W)); }
static void fmin_sources_alias(void)
{ setup(); CQ_EXPECT_ABORT(cq_kernel_fmin(&g_ctx, g_d, g_a, g_a, CQ_FP64_W)); }

/* ---- The step machine. -------------------------------------------------- */

static void step_index_past_the_end(void)
{
    cq_fmisc_block k;

    setup(); k = full();
    CQ_EXPECT_ABORT(cq_fmisc_step(&g_ctx, &k, cq_fmisc_steps(CQ_FMISC_MIN)));
}

static void step_index_is_negative(void)
{
    cq_fmisc_block k;

    setup(); k = full();
    CQ_EXPECT_ABORT(cq_fmisc_step(&g_ctx, &k, -1));
}

static void block_region_is_one_bit_short(void)
{
    cq_fmisc_block k;

    setup(); k = block(cq_fmisc_region(CQ_FMISC_MIN) - 1u, 0u);
    CQ_EXPECT_ABORT(cq_fmisc_step(&g_ctx, &k, 0));
}

static void block_offset_runs_off_the_region(void)
{
    cq_fmisc_block k;

    setup(); k = block(cq_fmisc_region(CQ_FMISC_MIN), 1u);
    CQ_EXPECT_ABORT(cq_fmisc_step(&g_ctx, &k, 0));
}

static void result_of_a_program_that_does_not_fit(void)
{
    cq_fmisc_block k;

    setup(); k = block(cq_fmisc_region(CQ_FMISC_MIN), 64u);
    CQ_EXPECT_ABORT((void)cq_fmisc_result(&k));
}

static void a_block_with_no_region(void)
{
    cq_fmisc_block k;

    setup(); k = full(); k.scr = NULL;
    CQ_EXPECT_ABORT(cq_fmisc_step(&g_ctx, &k, 0));
}

static void a_block_with_no_operand_b(void)
{
    cq_fmisc_block k;

    setup(); k = full(); k.b = NULL;
    CQ_EXPECT_ABORT(cq_fmisc_step(&g_ctx, &k, cq_fmisc_steps(CQ_FMISC_MIN) - 1));
}

static void a_block_naming_no_program(void)
{
    cq_fmisc_block k;

    setup(); k = full(); k.prog = CQ_FMISC_N_PROG;
    CQ_EXPECT_ABORT(cq_fmisc_step(&g_ctx, &k, 0));
}

/* ---- The accessors and operand resolution, driven directly. ------------ */

static void rows_of_an_unknown_program(void)
{ setup(); CQ_EXPECT_ABORT((void)cq_fmisc_rows(CQ_FMISC_N_PROG, NULL)); }
static void arity_of_an_unknown_op(void)
{ setup(); CQ_EXPECT_ABORT((void)cq_fmisc_arity(CQ_FXOP_N_OP)); }
static void row_width_outside_the_program(void)
{ setup(); CQ_EXPECT_ABORT((void)cq_fmisc_row_width(CQ_FMISC_MAX, 17)); }

static void row_steps_of_an_unknown_op(void)
{
    cq_fx_row r = { CQ_FXOP_N_OP, 0, 0, 0 };

    setup();
    CQ_EXPECT_ABORT((void)cq_fx_row_steps(&r));
}

static void row_region_of_an_unknown_op(void)
{
    cq_fx_row r = { -1, 0, 0, 0 };

    setup();
    CQ_EXPECT_ABORT((void)cq_fx_row_region(&r));
}

static void a_64_lane_operand_with_an_unknown_code(void)
{
    cq_bit buf[CQ_FP64_W];
    cq_fmisc_block k;

    setup(); k = full();
    CQ_EXPECT_ABORT((void)cq_fx_op64(&k, g_off, CQ_FX_K_QNAN - 1, buf));
}

static void a_64_lane_operand_names_a_row_outside_the_program(void)
{
    cq_bit buf[CQ_FP64_W];
    cq_fmisc_block k;

    setup(); k = full();
    CQ_EXPECT_ABORT((void)cq_fx_op64(&k, g_off, 17, buf));
}

static void a_one_bit_operand_that_is_not_a_row(void)
{
    cq_fmisc_block k;

    setup(); k = full();
    CQ_EXPECT_ABORT((void)cq_fx_flag_of(&k, g_off, CQ_FX_A));
}

static void a_one_bit_operand_names_a_64_lane_row(void)
{
    cq_fmisc_block k;

    setup(); k = full();
    CQ_EXPECT_ABORT((void)cq_fx_flag_of(&k, g_off, 11));    /* a MUX */
}

static void a_rail_operand_that_is_not_a_rail(void)
{
    cq_fmisc_block k;

    setup(); k = full();
    CQ_EXPECT_ABORT((void)cq_fx_rail(&k, CQ_FX_ABS_A));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(fneg_width_is_not_64),
    CQ_DEATH_CASE(fabs_width_is_not_64),
    CQ_DEATH_CASE(copysign_width_is_not_64),
    CQ_DEATH_CASE(fmin_width_is_not_64),
    CQ_DEATH_CASE(fmax_width_is_not_64),
    CQ_DEATH_CASE(fneg_dst_aliases_its_source),
    CQ_DEATH_CASE(fabs_dst_aliases_its_source),
    CQ_DEATH_CASE(copysign_dst_aliases_its_second_source),
    CQ_DEATH_CASE(fmin_dst_aliases_its_first_source),
    CQ_DEATH_CASE(fmax_dst_aliases_its_second_source),
    CQ_DEATH_CASE(copysign_sources_alias),
    CQ_DEATH_CASE(fmin_sources_alias),
    CQ_DEATH_CASE(step_index_past_the_end),
    CQ_DEATH_CASE(step_index_is_negative),
    CQ_DEATH_CASE(block_region_is_one_bit_short),
    CQ_DEATH_CASE(block_offset_runs_off_the_region),
    CQ_DEATH_CASE(result_of_a_program_that_does_not_fit),
    CQ_DEATH_CASE(a_block_with_no_region),
    CQ_DEATH_CASE(a_block_with_no_operand_b),
    CQ_DEATH_CASE(a_block_naming_no_program),
    CQ_DEATH_CASE(rows_of_an_unknown_program),
    CQ_DEATH_CASE(arity_of_an_unknown_op),
    CQ_DEATH_CASE(row_width_outside_the_program),
    CQ_DEATH_CASE(row_steps_of_an_unknown_op),
    CQ_DEATH_CASE(row_region_of_an_unknown_op),
    CQ_DEATH_CASE(a_64_lane_operand_with_an_unknown_code),
    CQ_DEATH_CASE(a_64_lane_operand_names_a_row_outside_the_program),
    CQ_DEATH_CASE(a_one_bit_operand_that_is_not_a_row),
    CQ_DEATH_CASE(a_one_bit_operand_names_a_64_lane_row),
    CQ_DEATH_CASE(a_rail_operand_that_is_not_a_rail)
)
