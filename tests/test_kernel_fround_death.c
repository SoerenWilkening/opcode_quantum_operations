/* tests/test_kernel_fround_death.c — M38b's fail-loud paths. K25.
 *
 * Every hard error M38b adds, driven DIRECTLY. The internal refusals are
 * reached through src/kernels/fround_int.h because the public block carries
 * no row pointer — a consumer that had to fill one could fill it wrong — so a
 * malformed row is unreachable through the public surface and would otherwise
 * be an assertion nobody has seen fail. Which layer would answer for each
 * guard if it were deleted, and why the regex names it, is recorded beside
 * the case list in tests/CMakeLists.txt.
 *
 * THREE GUARDS READ THEIR OWN MODULE-GLOBAL TABLE and cannot have a death case
 * (bd a-guard-that-reads-its-own-table-has-no-death-case): a view chain that
 * does not terminate, a dispatch landing on a zero-slot row, and `result`
 * whose last row is not a mux. Their reachability claims are executed in
 * test_kernel_fround.c's well-formedness case instead.
 */

#include "kernels/fround.h"
#include "kernels/fround_int.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "scratch.h"
#include "support/death.h"

static cq_sink    g_sink;
static cq_ctx     g_ctx;
static cq_scratch g_scr;
static cq_bit     g_a[CQ_FP64_W], g_d[CQ_FP64_W];

static void setup(void)
{
    g_sink = cq_death_null_sink();
    cq_ctx_init(&g_ctx, &g_sink);
    for (int i = 0; i < CQ_FP64_W; i++) {
        g_a[i] = cq_bit_zero(); g_d[i] = cq_bit_zero();
    }
    /* A QUANTUM operand, so the R9 short-circuit does not fold the call away
     * before it reaches the guard under test. */
    for (int i = 0; i < CQ_FP64_W; i++) cq_materialise(&g_ctx, &g_a[i]);
}

static void region(uint32_t bits)
{
    cq_scratch_alloc(&g_scr, bits);
    for (uint32_t i = 0; i < g_scr.n; i++) cq_materialise(&g_ctx, &g_scr.bits[i]);
}

static cq_fround_block block(cq_fround_prog p, uint32_t off)
{
    cq_fround_block k;

    k.a = g_a; k.scr = &g_scr; k.off = off; k.prog = p;
    return k;
}

/* The first row of program `p` with op `op`, found by OP rather than by a bare
 * index (bd a-death-case-must-drive-the-mutated-rows-own-slot). */
static int find_row(cq_fround_prog p, int op)
{
    const cq_fround_row *rows;
    int n;

    rows = cq_fround_rows(p, &n);
    for (int i = 0; i < n; i++) if (rows[i].op == op) return i;
    return -1;
}

/* ---- The width and the selector. ---------------------------------------- */

static void width_is_not_64(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_ftrunc(&g_ctx, g_d, g_a, 32));
}

static void width_is_128(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fround_away(&g_ctx, g_d, g_a, 128));
}

static void kernel_names_a_program_outside_the_five(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fround(&g_ctx, CQ_FROUND_N_PROG, g_d, g_a,
                                     CQ_FP64_W));
}

static void rows_of_a_program_outside_the_five(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fround_rows(CQ_FROUND_N_PROG, NULL));
}

static void trunc_base_of_a_program_outside_the_five(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fround_trunc_base(CQ_FROUND_N_PROG));
}

static void eval_of_a_program_outside_the_five(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fround_eval(CQ_FROUND_N_PROG, 0u));
}

static void block_names_a_program_outside_the_five(void)
{
    cq_fround_block k;

    setup();
    region(cq_fround_region(CQ_FROUND_EVEN));
    k = block(CQ_FROUND_N_PROG, 0u);
    CQ_EXPECT_ABORT(cq_fround_step(&g_ctx, &k, 0));
}

/* ---- The step-index range. ---------------------------------------------- */

static void step_index_past_the_end(void)
{
    cq_fround_block k;

    setup();
    region(cq_fround_region(CQ_FROUND_AWAY));
    k = block(CQ_FROUND_AWAY, 0u);
    CQ_EXPECT_ABORT(cq_fround_step(&g_ctx, &k, cq_fround_steps(CQ_FROUND_AWAY)));
}

static void step_index_is_negative(void)
{
    cq_fround_block k;

    setup();
    region(cq_fround_region(CQ_FROUND_AWAY));
    k = block(CQ_FROUND_AWAY, 0u);
    CQ_EXPECT_ABORT(cq_fround_step(&g_ctx, &k, -1));
}

static void row_at_outside_the_program(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fi_row_at(CQ_FROUND_TRUNC,
                                       cq_fround_steps(CQ_FROUND_TRUNC)));
}

/* ---- The region. -------------------------------------------------------- */

static void block_region_is_one_bit_short(void)
{
    cq_fround_block k;

    setup();
    region(cq_fround_region(CQ_FROUND_TRUNC) - 1u);
    k = block(CQ_FROUND_TRUNC, 0u);
    CQ_EXPECT_ABORT(cq_fround_step(&g_ctx, &k, 0));
}

static void block_offset_runs_off_the_region(void)
{
    cq_fround_block k;

    setup();
    region(cq_fround_region(CQ_FROUND_TRUNC));
    k = block(CQ_FROUND_TRUNC, 1u);
    CQ_EXPECT_ABORT(cq_fround_step(&g_ctx, &k, 0));
}

static void result_of_a_program_that_does_not_fit(void)
{
    cq_fround_block k;

    setup();
    region(cq_fround_region(CQ_FROUND_EVEN) - 1u);
    k = block(CQ_FROUND_EVEN, 0u);
    CQ_EXPECT_ABORT((void)cq_fround_result(&k));
}

static void a_block_with_no_region(void)
{
    cq_fround_block k;

    setup();
    k.a = g_a; k.scr = NULL; k.off = 0u; k.prog = CQ_FROUND_TRUNC;
    CQ_EXPECT_ABORT(cq_fround_step(&g_ctx, &k, 0));
}

/* ---- A missing rail: slot 0 of trunc is the first `eq`, whose operand is a
 * view over `a`. ---------------------------------------------------------- */

static void a_block_with_no_operand_a(void)
{
    cq_fround_block k;

    setup();
    region(cq_fround_region(CQ_FROUND_TRUNC));
    k = block(CQ_FROUND_TRUNC, 0u);
    k.a = NULL;
    CQ_EXPECT_ABORT(cq_fround_step(&g_ctx, &k, 0));
}

/* ---- The vocabulary. ---------------------------------------------------- */

static void arity_of_an_unknown_op(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fround_arity(CQ_FIOP_N_OP));
}

static void row_width_outside_the_program(void)
{
    const cq_fround_row *rows;
    int n;

    setup();
    rows = cq_fround_rows(CQ_FROUND_TRUNC, &n);
    CQ_EXPECT_ABORT((void)cq_fround_row_width(rows, n, n));
}

static void row_width_of_an_unknown_op(void)
{
    cq_fround_row bad[1];

    setup();
    bad[0].op = (short)CQ_FIOP_N_OP; bad[0].s0 = 0; bad[0].s1 = 0;
    bad[0].s2 = 0; bad[0].shift = 0; bad[0].mask = 0u;
    CQ_EXPECT_ABORT((void)cq_fround_row_width(bad, 1, 0));
}

static void row_steps_of_an_unknown_op(void)
{
    cq_fround_row bad;

    setup();
    bad.op = (short)CQ_FIOP_N_OP; bad.s0 = bad.s1 = bad.s2 = 0;
    bad.shift = 0; bad.mask = 0u;
    CQ_EXPECT_ABORT((void)cq_fi_row_steps(&bad));
}

static void row_region_of_an_unknown_op(void)
{
    cq_fround_row bad;

    setup();
    bad.op = (short)CQ_FIOP_N_OP; bad.s0 = bad.s1 = bad.s2 = 0;
    bad.shift = 0; bad.mask = 0u;
    CQ_EXPECT_ABORT((void)cq_fi_row_region(&bad));
}

/* ---- The operand refusals, one per direction. -------------------------- */

static void a_64_lane_operand_names_a_one_bit_row(void)
{
    cq_fround_block k;

    setup();
    region(cq_fround_region(CQ_FROUND_TRUNC));
    k = block(CQ_FROUND_TRUNC, 0u);
    CQ_EXPECT_ABORT((void)cq_fi_val64(&k, find_row(CQ_FROUND_TRUNC,
                                                   CQ_FIOP_EQ)));
}

static void a_64_lane_operand_names_a_view_row(void)
{
    cq_fround_block k;

    setup();
    region(cq_fround_region(CQ_FROUND_TRUNC));
    k = block(CQ_FROUND_TRUNC, 0u);
    CQ_EXPECT_ABORT((void)cq_fi_val64(&k, find_row(CQ_FROUND_TRUNC,
                                                   CQ_FIOP_VIEW)));
}

static void a_64_lane_operand_names_a_row_outside_the_program(void)
{
    cq_fround_block k;

    setup();
    region(cq_fround_region(CQ_FROUND_TRUNC));
    k = block(CQ_FROUND_TRUNC, 0u);
    CQ_EXPECT_ABORT((void)cq_fi_val64(&k, CQ_FROUND_TRUNC_ROWS));
}

static void a_one_bit_operand_names_a_64_lane_row(void)
{
    cq_fround_block k;

    setup();
    region(cq_fround_region(CQ_FROUND_TRUNC));
    k = block(CQ_FROUND_TRUNC, 0u);
    CQ_EXPECT_ABORT((void)cq_fi_flag_of(&k, find_row(CQ_FROUND_TRUNC,
                                                     CQ_FIOP_MUX)));
}

static void a_one_bit_operand_names_a_row_outside_the_program(void)
{
    cq_fround_block k;

    setup();
    region(cq_fround_region(CQ_FROUND_TRUNC));
    k = block(CQ_FROUND_TRUNC, 0u);
    CQ_EXPECT_ABORT((void)cq_fi_flag_of(&k, -1));
}

static void an_operand_code_outside_the_vocabulary(void)
{
    cq_fround_block k;
    cq_bit buf[CQ_FP64_W], tmp[CQ_FP64_W];

    setup();
    region(cq_fround_region(CQ_FROUND_TRUNC));
    k = block(CQ_FROUND_TRUNC, 0u);
    CQ_EXPECT_ABORT((void)cq_fi_op64(&k, -(CQ_FI_N_CODE + 1), buf, tmp));
}

/* ---- D7a at the kernel boundary. --------------------------------------- */

static void dst_aliases_its_source(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_ffloor(&g_ctx, g_a, g_a, CQ_FP64_W));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(width_is_not_64),
    CQ_DEATH_CASE(width_is_128),
    CQ_DEATH_CASE(kernel_names_a_program_outside_the_five),
    CQ_DEATH_CASE(rows_of_a_program_outside_the_five),
    CQ_DEATH_CASE(trunc_base_of_a_program_outside_the_five),
    CQ_DEATH_CASE(eval_of_a_program_outside_the_five),
    CQ_DEATH_CASE(block_names_a_program_outside_the_five),
    CQ_DEATH_CASE(step_index_past_the_end),
    CQ_DEATH_CASE(step_index_is_negative),
    CQ_DEATH_CASE(row_at_outside_the_program),
    CQ_DEATH_CASE(block_region_is_one_bit_short),
    CQ_DEATH_CASE(block_offset_runs_off_the_region),
    CQ_DEATH_CASE(result_of_a_program_that_does_not_fit),
    CQ_DEATH_CASE(a_block_with_no_region),
    CQ_DEATH_CASE(a_block_with_no_operand_a),
    CQ_DEATH_CASE(arity_of_an_unknown_op),
    CQ_DEATH_CASE(row_width_outside_the_program),
    CQ_DEATH_CASE(row_width_of_an_unknown_op),
    CQ_DEATH_CASE(row_steps_of_an_unknown_op),
    CQ_DEATH_CASE(row_region_of_an_unknown_op),
    CQ_DEATH_CASE(a_64_lane_operand_names_a_one_bit_row),
    CQ_DEATH_CASE(a_64_lane_operand_names_a_view_row),
    CQ_DEATH_CASE(a_64_lane_operand_names_a_row_outside_the_program),
    CQ_DEATH_CASE(a_one_bit_operand_names_a_64_lane_row),
    CQ_DEATH_CASE(a_one_bit_operand_names_a_row_outside_the_program),
    CQ_DEATH_CASE(an_operand_code_outside_the_vocabulary),
    CQ_DEATH_CASE(dst_aliases_its_source)
)
