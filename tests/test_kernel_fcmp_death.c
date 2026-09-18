/* tests/test_kernel_fcmp_death.c — M36's fail-loud paths. K18.
 *
 * EVERY GUARD HERE IS ONE SOMETHING ELSE WOULD OTHERWISE ANSWER FOR, and the
 * question CLAUDE.md asks before adding any guard — "which single case goes
 * red if this exact line is deleted, and would a later copy of the same guard
 * catch it?" — is answered per group in tests/CMakeLists.txt, where the
 * FAIL_REGULAR_EXPRESSION names the layers that must NOT have spoken.
 *
 *   W != 64              PRD-v2 §1 scopes v2 to f64 and every soft_fcmp_* is
 *                        (UInt64, UInt64). Masked from below by NOTHING on the
 *                        all-classical path: cq_kernel_check_n accepts any
 *                        positive width, cq_fp_pack reads a 64-lane rail
 *                        happily, and the kernel would return a plausible
 *                        answer computed from the wrong number of lanes. Two
 *                        predicates, because the guard is in ONE shared body
 *                        and a reader should not have to know that.
 *   step index           Masked from BELOW by M31 on the low side — a negative
 *                        `u` reaches cq_fp_class_step and aborts with
 *                        "fpclass: step index outside" — and from ITSELF on
 *                        the high side, where the dispatch falls off the end
 *                        of the program and dies with a DIFFERENT fcmp
 *                        message. Both are pinned absent, which is the only
 *                        way the high case is a claim about the range check
 *                        rather than about the fall-off.
 *   check_region         Masked from below by M08's cq_scratch_span, whose
 *                        message names the REGION rather than the consumer
 *                        that mis-sized its offset, and by M31's own region
 *                        check one layer in. Both pinned absent.
 *   check_program        A program of no rows, or one longer than the buffer.
 *                        Nothing below M36 knows what a program is.
 *   the vocabulary       An unknown op and an unknown operand code. Release
 *                        would otherwise read past a table or hand a wild
 *                        pointer to the emitter.
 *   the predicate enum   Nothing below M36 knows the fourteen rows exist.
 *   D7a / D7b            kernel.h's guard, reached at the kernel boundary.
 *
 * BOTH CONFIGURATIONS: no case carries CQ_DEATH_SKIP_WITHOUT_INVARIANTS. Every
 * guard above is a plain `if` in library code, not a Debug-gated assert, for
 * kernel.h's reason — risk R2's value is firing during a Release run.
 */

#include "kernels/fcmp.h"

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
#define FC_ONE_BITS  0x3ff0000000000000ull

static cq_sink    g_sink;
static cq_ctx     g_ctx;
static cq_scratch g_scr;

static void setup(void)
{
    g_sink = cq_death_null_sink();
    cq_ctx_init(&g_ctx, &g_sink);
}

/* A 64-lane all-classical rail. Classical on purpose: the width cases must not
 * be answered by a materialisation somewhere below. */
static const cq_bit *rail(uint64_t v)
{
    int32_t h = cq_bk_reg(&g_ctx, (uint32_t)CQ_FP64_W, v, 0x0ull);

    return cq_reg_cbits(&g_ctx.regs, h);
}

static cq_bit *one_bit_dst(void)
{
    return cq_reg_bits(&g_ctx.regs, cq_reg_alloc_zero(&g_ctx.regs, 1u));
}

/* ---- W must be 64. ------------------------------------------------------ */

static void oeq_width_is_not_64(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fcmp_oeq(&g_ctx, one_bit_dst(),
                                       rail(FC_ONE_BITS),
                                       rail(FC_ONE_BITS), 32));
}

static void ule_width_is_not_64(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fcmp_ule(&g_ctx, one_bit_dst(),
                                       rail(FC_ONE_BITS),
                                       rail(FC_ONE_BITS), 1));
}

/* ---- The step index. ---------------------------------------------------- */

/* `uno` is the smallest program — three rows, 513 bits — so these cases build
 * the least scratch that still exercises a real dispatch. */
static int block(cq_fcmp_block *k, cq_fcmp_row *rows, uint32_t off,
                 int region_delta)
{
    int n = cq_fcmp_program(CQ_FCMP_UNO, rows);

    setup();
    cq_scratch_alloc(&g_scr, (uint32_t)((int)cq_fcmp_region(rows, n)
                                        + region_delta));
    k->a = rail(FC_ONE_BITS);
    k->b = rail(FC_ONE_BITS);
    k->scr = &g_scr;
    k->off = off;
    k->rows = rows;
    k->n_rows = n;
    return n;
}

static void step_index_past_the_end(void)
{
    cq_fcmp_block k;
    cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];
    int n = block(&k, rows, 0u, 0);

    CQ_EXPECT_ABORT(cq_fcmp_step(&g_ctx, &k, cq_fcmp_steps(rows, n)));
}

static void step_index_is_negative(void)
{
    cq_fcmp_block k;
    cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];

    (void)block(&k, rows, 0u, 0);
    CQ_EXPECT_ABORT(cq_fcmp_step(&g_ctx, &k, -1));
}

/* ---- The region. -------------------------------------------------------- */

static void block_region_is_one_bit_short(void)
{
    cq_fcmp_block k;
    cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];

    (void)block(&k, rows, 0u, -1);
    CQ_EXPECT_ABORT(cq_fcmp_step(&g_ctx, &k, 0));
}

/* The region is big enough for ONE program and the offset puts this one past
 * the end — the two-programs-in-one-region shape with the region sized for
 * one. */
static void block_offset_runs_off_the_region(void)
{
    cq_fcmp_block k;
    cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];

    (void)block(&k, rows, 1u, 0);
    CQ_EXPECT_ABORT(cq_fcmp_step(&g_ctx, &k, 0));
}

static void flag_of_a_program_that_does_not_fit(void)
{
    cq_fcmp_block k;
    cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];

    (void)block(&k, rows, 4u, 0);
    CQ_EXPECT_ABORT((void)cq_fcmp_flag(&k));
}

static void a_block_with_no_region(void)
{
    cq_fcmp_block k;
    cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];

    (void)block(&k, rows, 0u, 0);
    k.scr = NULL;
    CQ_EXPECT_ABORT(cq_fcmp_step(&g_ctx, &k, 0));
}

/* ---- The program itself. ------------------------------------------------ */

static void a_program_of_no_rows(void)
{
    cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];

    setup();
    (void)cq_fcmp_program(CQ_FCMP_UNO, rows);
    CQ_EXPECT_ABORT((void)cq_fcmp_steps(rows, 0));
}

static void a_body_appended_past_the_buffer(void)
{
    cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];

    setup();
    CQ_EXPECT_ABORT((void)cq_fcmp_body(CQ_FCMP_B_OLT,
                                       CQ_FCMP_MAX_ROWS - 1, rows));
}

static void a_body_id_outside_the_three(void)
{
    cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];

    setup();
    CQ_EXPECT_ABORT((void)cq_fcmp_body((cq_fcmp_body_id)7, 0, rows));
}

/* ---- The row vocabulary. ------------------------------------------------ */

static void a_row_with_an_unknown_op(void)
{
    cq_fcmp_row rows[1];

    setup();
    rows[0].op = 99;
    rows[0].s0 = CQ_FC_A; rows[0].s1 = CQ_FC_B; rows[0].s2 = 0;
    CQ_EXPECT_ABORT((void)cq_fcmp_steps(rows, 1));
}

/* An `eq` block whose first operand is neither a row nor one of the eight
 * named codes. Without the refusal `op64` falls through and the emitter is
 * handed whatever the switch left in `buf`. */
static void a_row_with_an_unknown_operand_code(void)
{
    cq_fcmp_block k;
    cq_fcmp_row rows[2];

    setup();
    rows[0].op = CQ_FCOP_EQ;
    rows[0].s0 = -20; rows[0].s1 = CQ_FC_K_ZERO; rows[0].s2 = 0;
    rows[1].op = CQ_FCOP_NOT1;
    rows[1].s0 = 0;   rows[1].s1 = 0; rows[1].s2 = 0;

    cq_scratch_alloc(&g_scr, cq_fcmp_region(rows, 2));
    k.a = rail(FC_ONE_BITS);
    k.b = rail(FC_ONE_BITS);
    k.scr = &g_scr;
    k.off = 0u;
    k.rows = rows;
    k.n_rows = 2;

    CQ_EXPECT_ABORT(cq_fcmp_step(&g_ctx, &k, 0));
}

/* A `not1` handed one of the eight NEGATIVE operand codes. Without the refusal
 * `flag_of` indexes the row table out of bounds and the emitter is handed
 * whatever it read — a plausible gate on a wrong wire, with no diagnostic in
 * Release at all. It is the mirror of the case above: `op64` refuses a code it
 * does not know, this refuses a ROW REFERENCE that is not one. */
static void a_one_bit_operand_that_is_not_a_row(void)
{
    cq_fcmp_block k;
    cq_fcmp_row rows[2];

    setup();
    rows[0].op = CQ_FCOP_EQ;
    rows[0].s0 = CQ_FC_A; rows[0].s1 = CQ_FC_K_ZERO; rows[0].s2 = 0;
    rows[1].op = CQ_FCOP_NOT1;
    rows[1].s0 = CQ_FC_K_ZERO; rows[1].s1 = 0; rows[1].s2 = 0;

    cq_scratch_alloc(&g_scr, cq_fcmp_region(rows, 2));
    k.a = rail(FC_ONE_BITS);
    k.b = rail(FC_ONE_BITS);
    k.scr = &g_scr;
    k.off = 0u;
    k.rows = rows;
    k.n_rows = 2;

    /* Slot `cq_eq_steps(64)` is the first slot of the `not1`. */
    CQ_EXPECT_ABORT(cq_fcmp_step(&g_ctx, &k, cq_eq_steps(CQ_FP64_W)));
}

/* ---- The predicate enum. ------------------------------------------------ */

static void callee_of_an_unknown_predicate(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fcmp_callee((cq_fcmp_pred)CQ_FCMP_N_PRED));
}

static void program_of_an_unknown_predicate(void)
{
    cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];

    setup();
    CQ_EXPECT_ABORT((void)cq_fcmp_program((cq_fcmp_pred)-1, rows));
}

static void eval_of_an_unknown_predicate(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fcmp_eval(0ull, 0ull, (cq_fcmp_pred)99));
}

/* ---- D7 at the kernel boundary. ----------------------------------------- */

/* `dst` is ONE cq_bit overlapping `a[0]`. All-classical, so the kernel would
 * take the R9 short-circuit and its only emission is cq_emit_x on a constant,
 * which carries no distinctness check — nothing beneath M36 speaks in either
 * configuration. */
static void oeq_dst_aliases_its_first_source(void)
{
    int32_t h;
    cq_bit *a;

    setup();
    h = cq_bk_reg(&g_ctx, (uint32_t)CQ_FP64_W, FC_ONE_BITS, 0x0ull);
    a = cq_reg_bits(&g_ctx.regs, h);

    CQ_EXPECT_ABORT(cq_kernel_fcmp_oeq(&g_ctx, a, a,
                                       cq_reg_cbits(&g_ctx.regs, h),
                                       CQ_FP64_W));
}

/* D7b — two sources aliasing each other. LEGAL at the handle boundary, where
 * M26's defensive cqrt_copy is the remedy, and a hard error HERE: reaching a
 * kernel means that copy is missing (bd 493, kernel.h). */
static void olt_sources_alias_each_other(void)
{
    const cq_bit *a;

    setup();
    a = rail(FC_ONE_BITS);
    CQ_EXPECT_ABORT(cq_kernel_fcmp_olt(&g_ctx, one_bit_dst(), a, a,
                                       CQ_FP64_W));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(oeq_width_is_not_64),
    CQ_DEATH_CASE(ule_width_is_not_64),
    CQ_DEATH_CASE(step_index_past_the_end),
    CQ_DEATH_CASE(step_index_is_negative),
    CQ_DEATH_CASE(block_region_is_one_bit_short),
    CQ_DEATH_CASE(block_offset_runs_off_the_region),
    CQ_DEATH_CASE(flag_of_a_program_that_does_not_fit),
    CQ_DEATH_CASE(a_block_with_no_region),
    CQ_DEATH_CASE(a_program_of_no_rows),
    CQ_DEATH_CASE(a_body_appended_past_the_buffer),
    CQ_DEATH_CASE(a_body_id_outside_the_three),
    CQ_DEATH_CASE(a_row_with_an_unknown_op),
    CQ_DEATH_CASE(a_row_with_an_unknown_operand_code),
    CQ_DEATH_CASE(a_one_bit_operand_that_is_not_a_row),
    CQ_DEATH_CASE(callee_of_an_unknown_predicate),
    CQ_DEATH_CASE(program_of_an_unknown_predicate),
    CQ_DEATH_CASE(eval_of_an_unknown_predicate),
    CQ_DEATH_CASE(oeq_dst_aliases_its_first_source),
    CQ_DEATH_CASE(olt_sources_alias_each_other)
)
