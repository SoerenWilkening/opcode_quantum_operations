/* tests/test_kernel_fpround_death.c — M32's fail-loud paths. K23.
 *
 * SEVEN GUARDS, AND EACH ONE IS HERE BECAUSE SOMETHING ELSE WOULD OTHERWISE
 * ANSWER FOR IT — the question to ask before adding any guard (CLAUDE.md:
 * "which single case goes red if this exact line is deleted, and would a later
 * copy of the same guard catch it?"). Which layer WOULD answer is what each
 * group's FAIL_REGULAR_EXPRESSION in tests/CMakeLists.txt pins as forbidden,
 * because a death case's only native claim is "it aborted".
 *
 *   fpr_step's range     `u` is in [0, steps). Masked from BELOW on the LOW
 *                        side by every inner block: a negative index reaches
 *                        the first row of each program and aborts there with
 *                        "eq:"/"slt:"/"barrel: step index outside". Masked on
 *                        the HIGH side by M32's own fall-off `cq_kernel_die`,
 *                        which is a different message from the same module.
 *                        Both are pinned absent.
 *   arm (region)         the block fits at `off`. Masked from below by M08's
 *                        cq_scratch_span, whose message names the REGION and
 *                        not the consumer that mis-sized the offset.
 *   arm (NULL region)    a block with no `scr` at all. Without it the first
 *                        cq_scratch_span dereferences NULL — a SIGSEGV in
 *                        Release, which is not a SIGABRT and so is not even a
 *                        death the window can see.
 *   cq_fpround_rows      the id is one of the four. Nothing below M32 knows
 *                        the enum exists; without it the dispatch returns NULL
 *                        and Release walks it.
 *   cq_fpround_out_row   the output is one this block declares. Without it the
 *                        5-wide table is indexed out of bounds and hands back
 *                        whatever follows it — a real span, a plausible gate.
 *   cq_fpround_arity     the op is one of the fourteen. Reached by the row
 *                        tables' own well-formedness case and by any future
 *                        consumer that walks a program.
 *   base64's NULL input  a block that does not HAVE a third input. `norm52`
 *                        and `clz` pass NULL for `result_sign`; a row table
 *                        edit naming CQ_FR_IN2 there would dereference it.
 *                        PROVOKED THROUGH `_sf_round_and_pack` AND NOT THROUGH
 *                        `_sf_handle_subnormal`, and that is measured rather
 *                        than chosen: subnormal's only use of `result_sign` is
 *                        the VIEW that IS `flushed_result`, an operand of
 *                        nothing, so driving its whole program with a NULL
 *                        there completes NORMALLY. The first draft of that
 *                        case did exactly that and reported "expected an
 *                        abort". See `round_has_no_result_sign`.
 *
 * BOTH CONFIGURATIONS: no case carries CQ_DEATH_SKIP_WITHOUT_INVARIANTS. Every
 * guard above is a plain `if` in library code, not a Debug-gated assert, for
 * kernel.h's reason — risk R2's value is firing during a Release run.
 */

#include "kernels/fpround.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "scratch.h"
#include "support/death.h"

static cq_sink    g_sink;
static cq_ctx     g_ctx;
static cq_scratch g_scr;
static cq_bit     g_in[3][CQ_FP64_W];
static cq_bit     g_out[CQ_FP64_W];

static void setup(void)
{
    g_sink = cq_death_null_sink();
    cq_ctx_init(&g_ctx, &g_sink);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < CQ_FP64_W; j++) g_in[i][j] = cq_bit_zero();
}

/* A region of exactly the right size, materialised the way I6(b) has it — so
 * a case that reaches a gate reaches a REAL one and the abort under test is
 * the only thing that can speak. */
static void region(uint32_t bits)
{
    cq_scratch_alloc(&g_scr, bits);
    for (uint32_t i = 0; i < g_scr.n; i++) cq_materialise(&g_ctx, &g_scr.bits[i]);
}

static cq_round_block round_block(uint32_t off)
{
    cq_round_block k;

    k.wr = g_in[0]; k.result_exp = g_in[1]; k.result_sign = g_in[2];
    k.scr = &g_scr; k.off = off;
    return k;
}

/* ---- The step-index range. ---------------------------------------------- */

static void step_index_past_the_end(void)
{
    cq_round_block k;

    setup();
    region(cq_round_region());
    k = round_block(0u);
    CQ_EXPECT_ABORT(cq_round_step(&g_ctx, &k, cq_round_steps()));
}

/* THE LOW SIDE IS THE ONE THE INNER BLOCKS ANSWER FOR. `_sf_round_and_pack`'s
 * row 0 is an `slt`, so a negative `u` with M32's guard deleted lands in
 * cq_slt_step's own range check and aborts with "slt: step index outside" —
 * inside the window, in both configurations. That message is pinned as
 * forbidden, which is the whole of this case's coverage. */
static void step_index_is_negative(void)
{
    cq_round_block k;

    setup();
    region(cq_round_region());
    k = round_block(0u);
    CQ_EXPECT_ABORT(cq_round_step(&g_ctx, &k, -1));
}

/* ---- The region. -------------------------------------------------------- */

static void block_region_is_one_bit_short(void)
{
    cq_round_block k;

    setup();
    region(cq_round_region() - 1u);
    k = round_block(0u);
    CQ_EXPECT_ABORT(cq_round_step(&g_ctx, &k, 0));
}

static void block_offset_runs_off_the_region(void)
{
    cq_subnorm_block k;

    setup();
    region(cq_subnorm_region());
    k.wr = g_in[0]; k.result_exp = g_in[1]; k.result_sign = g_in[2];
    k.scr = &g_scr; k.off = 1u;               /* one bit past the top */
    CQ_EXPECT_ABORT(cq_subnorm_step(&g_ctx, &k, 0));
}

/* The accessors carry the same check, and they have to: a consumer that reads
 * an output before driving a step would otherwise get a span outside the
 * region with no diagnostic of its own. */
static void output_of_a_block_that_does_not_fit(void)
{
    cq_norm52_block k;

    setup();
    region(cq_norm52_region() - 1u);
    k.m = g_in[0]; k.e = g_in[1]; k.scr = &g_scr; k.off = 0u;
    CQ_EXPECT_ABORT((void)cq_norm52_m(&k));
}

static void a_block_with_no_region(void)
{
    cq_clz_block k;

    setup();
    k.wr = g_in[0]; k.result_exp = g_in[1]; k.scr = NULL; k.off = 0u;
    CQ_EXPECT_ABORT(cq_clz_step(&g_ctx, &k, 0));
}

/* ---- The vocabulary. ---------------------------------------------------- */

static void a_block_id_outside_the_four(void)
{
    int n = 0;

    setup();
    CQ_EXPECT_ABORT((void)cq_fpround_rows((cq_fpround_id)CQ_FPR_N_BLOCK, &n));
}

/* `_sf_normalize_to_bit52` returns two values, so output 2 is not one it
 * declares — and the table row for it is -1 rather than absent, which is what
 * makes this a refusal and not an out-of-bounds read. */
static void an_output_that_is_not_declared(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fpround_out_row(CQ_FPR_NORM52, 2));
}

static void arity_of_an_unknown_op(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fpround_arity(CQ_FROP_N_OP));
}

static void flushed_with_no_output_array(void)
{
    cq_subnorm_block k;

    setup();
    region(cq_subnorm_region());
    k.wr = g_in[0]; k.result_exp = g_in[1]; k.result_sign = g_in[2];
    k.scr = &g_scr; k.off = 0u;
    CQ_EXPECT_ABORT(cq_subnorm_flushed(&k, NULL));
    (void)g_out;
}

/* The whole program, so the case reaches the first row that resolves the third
 * input. Written as a function because CQ_EXPECT_ABORT takes an EXPRESSION and
 * a braced block there is a GNU statement expression, which -std=c11 refuses. */
static cq_round_block g_rnd;

static void drive_round(void)
{
    for (int u = 0; u < cq_round_steps(); u++) cq_round_step(&g_ctx, &g_rnd, u);
}

/* `_sf_normalize_to_bit52` and `_sf_normalize_clz` have TWO inputs, so
 * `fpr_ctx.in[2]` is NULL for them and a row-table edit naming CQ_FR_IN2 in
 * either would dereference it. Provoked through `_sf_round_and_pack`, whose
 * row 1 is `result_sign << 63` (:201) and whose row 2 ORs it with INF_BITS —
 * an EMITTING row, so the operand is resolved during stepping.
 *
 * AND THE SITE IT IS **NOT** REACHABLE THROUGH IS THE INSTRUCTIVE HALF.
 * `_sf_handle_subnormal`'s only use of `result_sign` is row 18, the VIEW that
 * IS `flushed_result` (:183) — an OUTPUT of the block and an operand of
 * nothing. So driving that whole program with `result_sign = NULL` completes
 * NORMALLY: measured, the first draft of this case did exactly that and
 * reported `expected an abort`. `cq_subnorm_flushed` is the only door to it
 * there, and `flushed_with_no_output_array` above is the case for that door. */
static void round_has_no_result_sign(void)
{
    setup();
    region(cq_round_region());
    g_rnd.wr = g_in[0]; g_rnd.result_exp = g_in[1]; g_rnd.result_sign = NULL;
    g_rnd.scr = &g_scr; g_rnd.off = 0u;
    CQ_EXPECT_ABORT(drive_round());
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(step_index_past_the_end),
    CQ_DEATH_CASE(step_index_is_negative),
    CQ_DEATH_CASE(block_region_is_one_bit_short),
    CQ_DEATH_CASE(block_offset_runs_off_the_region),
    CQ_DEATH_CASE(output_of_a_block_that_does_not_fit),
    CQ_DEATH_CASE(a_block_with_no_region),
    CQ_DEATH_CASE(a_block_id_outside_the_four),
    CQ_DEATH_CASE(an_output_that_is_not_declared),
    CQ_DEATH_CASE(arity_of_an_unknown_op),
    CQ_DEATH_CASE(flushed_with_no_output_array),
    CQ_DEATH_CASE(round_has_no_result_sign)
)
