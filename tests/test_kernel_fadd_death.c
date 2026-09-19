/* tests/test_kernel_fadd_death.c — M33's fail-loud paths. K15.
 *
 * EVERY GUARD HERE IS ONE SOMETHING ELSE WOULD OTHERWISE ANSWER FOR, and the
 * question CLAUDE.md asks before adding any guard — "which single case goes
 * red if this exact line is deleted, and would a later copy of the same guard
 * catch it?" — is answered per group in tests/CMakeLists.txt, where the
 * FAIL_REGULAR_EXPRESSION names the layers that must NOT have spoken.
 *
 *   W != 64              PRD-v2 §1 scopes v2 to f64 and `soft_fadd` is
 *                        (UInt64, UInt64). Masked from below by NOTHING on the
 *                        all-classical path: cq_kernel_check_dst accepts any
 *                        positive width, cq_fp_pack reads a 64-lane rail
 *                        happily, and the kernel would return a plausible
 *                        answer computed from the wrong number of lanes. Both
 *                        kernels, because the guard is in ONE shared body and
 *                        a reader should not have to know that.
 *   step index           Masked from BELOW on the low side — a negative `u`
 *                        reaches the first row's `eq` block and aborts with
 *                        "eq: step index outside" — and from ITSELF on the high
 *                        side, where the dispatch falls off the end with a
 *                        DIFFERENT fadd message. Both pinned absent, which is
 *                        the only way the high case is a claim about the range
 *                        check rather than about the fall-off.
 *   the region           Masked from below by M08's cq_scratch_span, whose
 *                        message names the REGION rather than the consumer
 *                        that mis-sized its offset, and by M31's and M32's own
 *                        region checks one layer in. All pinned absent.
 *   the program          A program of no rows, or one longer than the buffer.
 *   the vocabulary       An unknown op, an unknown operand code, a 1-bit
 *                        operand that is not a row, a 64-lane operand that
 *                        names a flag, a pick over a row with no tuple, a view
 *                        chain that does not terminate, a class row naming no
 *                        class, and a NULL input. Release would otherwise read
 *                        past a table or hand a wild pointer to the emitter.
 *   the program enum     Nothing below M33 knows ADD and SUB exist.
 *   D7a / D7b            kernel.h's guard, reached at the kernel boundary.
 *
 * THE VOCABULARY CASES GO THROUGH src/kernels/fadd_int.h, AND THAT IS WHY IT
 * DECLARES `cq_fa_step`. The public `cq_fsub_block` carries no row pointer —
 * a consumer that had to fill one could fill it wrong, which is the hazard bd
 * a-step-block-wires-its-own-inner-block-never-the-consumer records — so every
 * refusal in the step machine is UNREACHABLE through the public surface and
 * would otherwise be an assertion nobody has seen fail. Tests reach internal
 * headers directly (tests/CMakeLists.txt puts src/ on the support target's
 * PUBLIC include path), so the malformed programs below drive them.
 *
 * BOTH CONFIGURATIONS: no case carries CQ_DEATH_SKIP_WITHOUT_INVARIANTS. Every
 * guard above is a plain `if` in library code, not a Debug-gated assert, for
 * kernel.h's reason — risk R2's value is firing during a Release run.
 */

#include "kernels/fadd.h"
#include "kernels/fadd_int.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/cmp.h"
#include "kernels/fpfield.h"
#include "reg.h"
#include "scratch.h"
#include "support/bitkinds.h"
#include "support/death.h"

/* 1.0 as an IEEE binary64 bit pattern. A literal rather than an include of
 * tests/support/fpanchors.h: this file needs no anchor table and the value is
 * checked by inspection. */
#define FA_ONE_BITS  0x3ff0000000000000ull

static cq_sink    g_sink;
static cq_ctx     g_ctx;
static cq_scratch g_scr;

static void setup(void)
{
    g_sink = cq_death_null_sink();
    cq_ctx_init(&g_ctx, &g_sink);
}

/* A 64-lane rail. QUANTUM on purpose wherever the case must reach the circuit:
 * an all-classical pair takes the R9 short-circuit and never builds a region. */
static const cq_bit *rail(uint64_t v, int quantum)
{
    int32_t h = cq_bk_reg(&g_ctx, (uint32_t)CQ_FP64_W, v,
                          quantum ? ~0ull : 0ull);

    return cq_reg_cbits(&g_ctx.regs, h);
}

static cq_bit *dst64(void)
{
    return cq_reg_bits(&g_ctx.regs,
                       cq_reg_alloc_zero(&g_ctx.regs, (uint32_t)CQ_FP64_W));
}

/* ---- W must be 64. ------------------------------------------------------ */

static void fadd_width_is_not_64(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fadd(&g_ctx, dst64(), rail(FA_ONE_BITS, 0),
                                   rail(FA_ONE_BITS, 0), 32));
}

static void fsub_width_is_not_64(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fsub(&g_ctx, dst64(), rail(FA_ONE_BITS, 0),
                                   rail(FA_ONE_BITS, 0), 1));
}

/* ---- The step index and the region, through the EXPORTED block. --------- */

static void fsub_block(cq_fsub_block *k, uint32_t off, int region_delta)
{
    setup();
    cq_scratch_alloc(&g_scr, (uint32_t)((int)cq_fsub_region() + region_delta));
    k->a = rail(FA_ONE_BITS, 1);
    k->b = rail(FA_ONE_BITS, 1);
    k->scr = &g_scr;
    k->off = off;
}

static void step_index_past_the_end(void)
{
    cq_fsub_block k;

    fsub_block(&k, 0u, 0);
    CQ_EXPECT_ABORT(cq_fsub_step(&g_ctx, &k, cq_fsub_steps()));
}

static void step_index_is_negative(void)
{
    cq_fsub_block k;

    fsub_block(&k, 0u, 0);
    CQ_EXPECT_ABORT(cq_fsub_step(&g_ctx, &k, -1));
}

static void block_region_is_one_bit_short(void)
{
    cq_fsub_block k;

    fsub_block(&k, 0u, -1);
    CQ_EXPECT_ABORT(cq_fsub_step(&g_ctx, &k, 0));
}

/* The region is big enough for ONE program and the offset puts this one past
 * the end — the two-programs-in-one-region shape with the region sized for
 * one. */
static void block_offset_runs_off_the_region(void)
{
    cq_fsub_block k;

    fsub_block(&k, 1u, 0);
    CQ_EXPECT_ABORT(cq_fsub_step(&g_ctx, &k, 0));
}

static void result_of_a_block_that_does_not_fit(void)
{
    cq_fsub_block k;

    fsub_block(&k, 4u, 0);
    CQ_EXPECT_ABORT((void)cq_fsub_result(&k));
}

static void a_block_with_no_region(void)
{
    cq_fsub_block k;

    fsub_block(&k, 0u, 0);
    k.scr = NULL;
    CQ_EXPECT_ABORT(cq_fsub_step(&g_ctx, &k, 0));
}

/* ---- The program enum and the table surface. ---------------------------- */

static void program_id_is_neither_add_nor_sub(void)
{
    cq_fadd_row rows[CQ_FADD_MAX_ROWS];

    setup();
    CQ_EXPECT_ABORT((void)cq_fadd_program((cq_fadd_prog)7, rows));
}

static void result_row_of_an_unknown_program(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fadd_result_row((cq_fadd_prog)-1));
}

static void a_program_with_no_output_buffer(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fadd_program(CQ_FADD_PROG_ADD, NULL));
}

static void arity_of_an_unknown_op(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fadd_arity(99));
}

static void body_rows_with_no_count_output(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fadd_body_rows(NULL));
}

/* ---- The vocabulary, through fadd_int.h's own entry point. -------------- */

/* A hand-built program of `n` rows over a region big enough for all of them.
 * `rows` is the caller's, so each case below spells exactly the malformation
 * it is about and nothing else. */
static cq_fa_ctx bad_ctx(cq_fadd_row *rows, int n)
{
    cq_fa_ctx x;

    setup();
    x.a = rail(FA_ONE_BITS, 1);
    x.b = rail(FA_ONE_BITS, 1);
    x.rows = rows;
    x.n = n;
    x.off = 0u;
    cq_scratch_alloc(&g_scr, 8u * (uint32_t)CQ_FP64_W);
    x.scr = &g_scr;
    return x;
}

static void a_row_with_an_unknown_op(void)
{
    cq_fadd_row rows[1];
    cq_fa_ctx x;

    rows[0].op = 99;
    rows[0].s0 = CQ_FA_A; rows[0].s1 = CQ_FA_B; rows[0].s2 = 0;
    rows[0].shift = 0; rows[0].mask = ~UINT64_C(0);
    x = bad_ctx(rows, 1);
    CQ_EXPECT_ABORT(cq_fa_step(&g_ctx, &x, 0));
}

/* An `eq` block whose first operand is neither a row nor one of the twelve
 * named codes. Without the refusal `base64` falls through and the emitter is
 * handed whatever the switch left in `buf`. */
static void a_row_with_an_unknown_operand_code(void)
{
    cq_fadd_row rows[1];
    cq_fa_ctx x;

    rows[0].op = CQ_FAOP_EQ;
    rows[0].s0 = -40; rows[0].s1 = CQ_FA_K_ZERO; rows[0].s2 = 0;
    rows[0].shift = 0; rows[0].mask = 0;
    x = bad_ctx(rows, 1);
    CQ_EXPECT_ABORT(cq_fa_step(&g_ctx, &x, 0));
}

/* A `not1` handed one of the twelve NEGATIVE codes. Without the refusal
 * `cq_fa_flag_of` indexes the row table out of bounds and the emitter is
 * handed whatever it read — a plausible gate on a wrong wire, with no
 * diagnostic in Release at all. It is the mirror of the case above: `base64`
 * refuses a code it does not know, this refuses a ROW REFERENCE that is not
 * one (bd a-table-driven-kernel-needs-both-operand-refusals). */
static void a_one_bit_operand_that_is_not_a_row(void)
{
    cq_fadd_row rows[1];
    cq_fa_ctx x;

    rows[0].op = CQ_FAOP_NOT1;
    rows[0].s0 = CQ_FA_K_ZERO; rows[0].s1 = 0; rows[0].s2 = 0;
    rows[0].shift = 0; rows[0].mask = 0;
    x = bad_ctx(rows, 1);
    CQ_EXPECT_ABORT(cq_fa_step(&g_ctx, &x, 0));
}

/* A `not1` handed a 64-LANE row — the other half of the same refusal. */
static void a_one_bit_operand_that_names_a_64_lane_row(void)
{
    cq_fadd_row rows[2];
    cq_fa_ctx x;

    rows[0].op = CQ_FAOP_OR;
    rows[0].s0 = CQ_FA_A; rows[0].s1 = CQ_FA_K_QUIET; rows[0].s2 = 0;
    rows[0].shift = 0; rows[0].mask = 0;
    rows[1].op = CQ_FAOP_NOT1;
    rows[1].s0 = 0; rows[1].s1 = 0; rows[1].s2 = 0;
    rows[1].shift = 0; rows[1].mask = 0;
    x = bad_ctx(rows, 2);
    CQ_EXPECT_ABORT(cq_fa_step(&g_ctx, &x, 3 * CQ_FP64_W));
}

/* A 64-lane operand that names a FLAG row. */
static void a_64_lane_operand_that_names_a_flag(void)
{
    cq_fadd_row rows[2];
    cq_fa_ctx x;

    rows[0].op = CQ_FAOP_EQ;
    rows[0].s0 = CQ_FA_A; rows[0].s1 = CQ_FA_B; rows[0].s2 = 0;
    rows[0].shift = 0; rows[0].mask = 0;
    rows[1].op = CQ_FAOP_OR;
    rows[1].s0 = 0; rows[1].s1 = CQ_FA_K_QUIET; rows[1].s2 = 0;
    rows[1].shift = 0; rows[1].mask = 0;
    x = bad_ctx(rows, 2);
    CQ_EXPECT_ABORT(cq_fa_step(&g_ctx, &x, cq_eq_steps(CQ_FP64_W)));
}

/* A PICK over a row that returns no tuple. */
static void a_pick_over_a_row_with_no_tuple(void)
{
    cq_fadd_row rows[3];
    cq_fa_ctx x;

    rows[0].op = CQ_FAOP_OR;
    rows[0].s0 = CQ_FA_A; rows[0].s1 = CQ_FA_K_QUIET; rows[0].s2 = 0;
    rows[0].shift = 0; rows[0].mask = 0;
    rows[1].op = CQ_FAOP_PICK;
    rows[1].s0 = 0; rows[1].s1 = 0; rows[1].s2 = 0;
    rows[1].shift = CQ_FA_PICK_WR; rows[1].mask = 0;
    rows[2].op = CQ_FAOP_OR;
    rows[2].s0 = 1; rows[2].s1 = CQ_FA_K_QUIET; rows[2].s2 = 0;
    rows[2].shift = 0; rows[2].mask = 0;
    x = bad_ctx(rows, 3);
    CQ_EXPECT_ABORT(cq_fa_step(&g_ctx, &x, 3 * CQ_FP64_W));
}

/* A view chain that does not terminate: a view over itself. */
static void a_view_chain_that_does_not_terminate(void)
{
    cq_fadd_row rows[2];
    cq_fa_ctx x;

    rows[0].op = CQ_FAOP_VIEW;
    rows[0].s0 = 0; rows[0].s1 = 0; rows[0].s2 = 0;
    rows[0].shift = 0; rows[0].mask = ~UINT64_C(0);
    rows[1].op = CQ_FAOP_OR;
    rows[1].s0 = 0; rows[1].s1 = CQ_FA_K_QUIET; rows[1].s2 = 0;
    rows[1].shift = 0; rows[1].mask = 0;
    x = bad_ctx(rows, 2);
    CQ_EXPECT_ABORT(cq_fa_step(&g_ctx, &x, 0));
}

/* A class row whose `shift` names no cq_fp_class. */
static void a_class_row_that_names_no_class(void)
{
    cq_fadd_row rows[1];
    cq_fa_ctx x;

    rows[0].op = CQ_FAOP_CLASS;
    rows[0].s0 = CQ_FA_A; rows[0].s1 = 0; rows[0].s2 = 0;
    rows[0].shift = 9; rows[0].mask = 0;
    x = bad_ctx(rows, 1);
    CQ_EXPECT_ABORT(cq_fa_step(&g_ctx, &x, 0));
}

/* A block with no `b`, reached through a row that reads it. */
static void a_block_with_no_such_input(void)
{
    cq_fadd_row rows[1];
    cq_fa_ctx x;

    rows[0].op = CQ_FAOP_OR;
    rows[0].s0 = CQ_FA_B; rows[0].s1 = CQ_FA_K_QUIET; rows[0].s2 = 0;
    rows[0].shift = 0; rows[0].mask = 0;
    x = bad_ctx(rows, 1);
    x.b = NULL;
    CQ_EXPECT_ABORT(cq_fa_step(&g_ctx, &x, 0));
}

static void a_program_of_no_rows(void)
{
    cq_fadd_row rows[1];
    cq_fa_ctx x;

    rows[0].op = CQ_FAOP_AND1;
    rows[0].s0 = 0; rows[0].s1 = 0; rows[0].s2 = 0;
    rows[0].shift = 0; rows[0].mask = 0;
    x = bad_ctx(rows, 1);
    x.n = 0;
    CQ_EXPECT_ABORT((void)cq_fa_steps_of(&x));
}

/* ---- D7 at the kernel boundary. ----------------------------------------- */

/* `dst` OVERLAPS `a`. All-classical, so the kernel would take the R9
 * short-circuit and its only emission is cq_emit_x on a constant, which
 * carries no distinctness check — nothing beneath M33 speaks in either
 * configuration. */
static void fadd_dst_aliases_its_first_source(void)
{
    int32_t h;
    cq_bit *a;

    setup();
    h = cq_bk_reg(&g_ctx, (uint32_t)CQ_FP64_W, FA_ONE_BITS, 0x0ull);
    a = cq_reg_bits(&g_ctx.regs, h);

    CQ_EXPECT_ABORT(cq_kernel_fadd(&g_ctx, a, a,
                                   cq_reg_cbits(&g_ctx.regs, h), CQ_FP64_W));
}

/* D7b — two sources aliasing each other. LEGAL at the handle boundary, where
 * M26's defensive cqrt_copy is the remedy, and a hard error HERE: reaching a
 * kernel means that copy is missing (bd 493, kernel.h). */
static void fsub_sources_alias_each_other(void)
{
    const cq_bit *a;

    setup();
    a = rail(FA_ONE_BITS, 0);
    CQ_EXPECT_ABORT(cq_kernel_fsub(&g_ctx, dst64(), a, a, CQ_FP64_W));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(fadd_width_is_not_64),
    CQ_DEATH_CASE(fsub_width_is_not_64),
    CQ_DEATH_CASE(step_index_past_the_end),
    CQ_DEATH_CASE(step_index_is_negative),
    CQ_DEATH_CASE(block_region_is_one_bit_short),
    CQ_DEATH_CASE(block_offset_runs_off_the_region),
    CQ_DEATH_CASE(result_of_a_block_that_does_not_fit),
    CQ_DEATH_CASE(a_block_with_no_region),
    CQ_DEATH_CASE(program_id_is_neither_add_nor_sub),
    CQ_DEATH_CASE(result_row_of_an_unknown_program),
    CQ_DEATH_CASE(a_program_with_no_output_buffer),
    CQ_DEATH_CASE(arity_of_an_unknown_op),
    CQ_DEATH_CASE(body_rows_with_no_count_output),
    CQ_DEATH_CASE(a_row_with_an_unknown_op),
    CQ_DEATH_CASE(a_row_with_an_unknown_operand_code),
    CQ_DEATH_CASE(a_one_bit_operand_that_is_not_a_row),
    CQ_DEATH_CASE(a_one_bit_operand_that_names_a_64_lane_row),
    CQ_DEATH_CASE(a_64_lane_operand_that_names_a_flag),
    CQ_DEATH_CASE(a_pick_over_a_row_with_no_tuple),
    CQ_DEATH_CASE(a_view_chain_that_does_not_terminate),
    CQ_DEATH_CASE(a_class_row_that_names_no_class),
    CQ_DEATH_CASE(a_block_with_no_such_input),
    CQ_DEATH_CASE(a_program_of_no_rows),
    CQ_DEATH_CASE(fadd_dst_aliases_its_first_source),
    CQ_DEATH_CASE(fsub_sources_alias_each_other)
)
