/* tests/test_kernel_fdiv_death.c — M35's fail-loud paths. K17.
 *
 * TEN GUARDS, AND EACH ONE IS HERE BECAUSE SOMETHING ELSE WOULD OTHERWISE
 * ANSWER FOR IT — the question to ask before adding any guard (CLAUDE.md:
 * "which single case goes red if this exact line is deleted, and would a later
 * copy of the same guard catch it?"). Which layer WOULD answer is what each
 * group's FAIL_REGULAR_EXPRESSION in tests/CMakeLists.txt pins as forbidden,
 * because a death case's only native claim is "it aborted".
 *
 *   W != 64             PRD-v2 §1 scopes v2 to f64 and `soft_fdiv` is
 *                       (UInt64, UInt64), so another width is a FICTION and
 *                       not an unimplemented case. Masked from below by
 *                       M31's `cq_fp_pack` (which aborts on a quantum lane)
 *                       and by `cq_fp_view`'s span check, both pinned absent.
 *   cq_fdiv_step range  `u` is in [0, cq_fdiv_steps()). MASKED BY A LATER COPY
 *                       OF ITSELF: `cq_fd_row_of_slot` checks the same range
 *                       one layer down, so its message was made DISJOINT ("a
 *                       slot index outside the segmented map") and is pinned
 *                       absent here. That is the recorded M15 shape, and it is
 *                       the reason this pair of cases is a claim about the
 *                       PUBLIC entry point rather than about the map.
 *   cq_fd_arm (region)  the program fits at `off`. Masked from below by M08's
 *                       cq_scratch_span, whose message names the REGION and
 *                       not the consumer that mis-sized the offset.
 *   cq_fd_arm (NULL)    a block with no `scr` at all. Without it the first
 *                       cq_scratch_span dereferences NULL — a SIGSEGV in
 *                       Release, which is not a SIGABRT and so is not even a
 *                       death the window can see.
 *   base64's NULL rail  a block with no `a`. `soft_fdiv`'s first EMITTING row
 *                       is `sa ⊻ sb` at :54, whose two operands are views over
 *                       the two rails, so step 0 reaches it immediately.
 *   cq_fdiv_arity       the op is one of the seventeen. Reached by the row
 *                       table's own well-formedness case and by any future
 *                       consumer that walks the program.
 *   cq_fdiv_row_at      an index outside the program. THIS ONE IS NEW TO M35
 *                       AND IS LOAD-BEARING IN A WAY M34's IS NOT: there is no
 *                       `cq_fdiv_row *` array, so every read of the table goes
 *                       through this function, and 392 of the 478 rows are
 *                       MATERIALISED from a template — a rebase that left a
 *                       relative code behind would index straight past the end
 *                       and hand the emitter whatever it read.
 *   cq_fdiv_row_width   x2. A projection of a row that is not STRICTLY
 *                       earlier, and a projection of a row that returns no
 *                       tuple. Driven through the ARRAY form, because the
 *                       shipped program is well formed by construction.
 *   cq_kernel_check_dst D7a and D7b at the KERNEL boundary. D7b is LEGAL at
 *                       the handle boundary and its remedy is M26's defensive
 *                       cqrt_copy; a kernel that sees the alias is looking at
 *                       a missing copy.
 *
 * THE INNER OPERAND REFUSALS HAVE NO CASE OF THEIR OWN, AND THAT IS M32's AND
 * M34's PRECEDENT RATHER THAN AN OMISSION. `cq_fd_val64`'s "a 64-lane operand
 * names a row of another width", `cq_fd_flag_of`'s mirror and "an operand
 * names a row outside the program" are reachable only from a MALFORMED row
 * table, and `cq_fdiv_block` deliberately carries no `rows` pointer — there is
 * one program and a consumer cannot substitute another. They are pinned as
 * FORBIDDEN messages in the step-index group instead (bd
 * a-table-driven-kernel-needs-both-operand-refusals).
 *
 * BOTH CONFIGURATIONS: no case carries CQ_DEATH_SKIP_WITHOUT_INVARIANTS. Every
 * guard above is a plain `if` in library code, not a Debug-gated assert, for
 * kernel.h's reason — risk R2's value is firing during a Release run.
 */

#include "kernels/fdiv.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "scratch.h"
#include "support/death.h"

static cq_sink    g_sink;
static cq_ctx     g_ctx;
static cq_scratch g_scr;
static cq_bit     g_a[CQ_FP64_W], g_b[CQ_FP64_W], g_d[CQ_FP64_W];

static void setup(void)
{
    g_sink = cq_death_null_sink();
    cq_ctx_init(&g_ctx, &g_sink);
    for (int i = 0; i < CQ_FP64_W; i++) {
        g_a[i] = cq_bit_zero(); g_b[i] = cq_bit_zero(); g_d[i] = cq_bit_zero();
    }
    /* A QUANTUM operand, so the kernel's R9 short-circuit does not fold the
     * whole call away before it reaches the guard under test. */
    for (int i = 0; i < CQ_FP64_W; i++) cq_materialise(&g_ctx, &g_a[i]);
}

/* A region of exactly the right size, materialised the way I6(b) has it — so a
 * case that reaches a gate reaches a REAL one and the abort under test is the
 * only thing that can speak. */
static void region(uint32_t bits)
{
    cq_scratch_alloc(&g_scr, bits);
    for (uint32_t i = 0; i < g_scr.n; i++) cq_materialise(&g_ctx, &g_scr.bits[i]);
}

static cq_fdiv_block block(uint32_t off)
{
    cq_fdiv_block k;

    k.a = g_a; k.b = g_b; k.scr = &g_scr; k.off = off;
    return k;
}

/* ---- The width. --------------------------------------------------------- */

static void width_is_not_64(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fdiv(&g_ctx, g_d, g_a, g_b, 32));
}

static void width_is_128(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fdiv(&g_ctx, g_d, g_a, g_b, 128));
}

/* ---- The step-index range. ---------------------------------------------- */

static void step_index_past_the_end(void)
{
    cq_fdiv_block k;

    setup();
    region(cq_fdiv_region());
    k = block(0u);
    CQ_EXPECT_ABORT(cq_fdiv_step(&g_ctx, &k, cq_fdiv_steps()));
}

static void step_index_is_negative(void)
{
    cq_fdiv_block k;

    setup();
    region(cq_fdiv_region());
    k = block(0u);
    CQ_EXPECT_ABORT(cq_fdiv_step(&g_ctx, &k, -1));
}

/* ---- The region. -------------------------------------------------------- */

static void block_region_is_one_bit_short(void)
{
    cq_fdiv_block k;

    setup();
    region(cq_fdiv_region() - 1u);
    k = block(0u);
    CQ_EXPECT_ABORT(cq_fdiv_step(&g_ctx, &k, 0));
}

static void block_offset_runs_off_the_region(void)
{
    cq_fdiv_block k;

    setup();
    region(cq_fdiv_region());
    k = block(1u);
    CQ_EXPECT_ABORT(cq_fdiv_step(&g_ctx, &k, 0));
}

static void result_of_a_program_that_does_not_fit(void)
{
    cq_fdiv_block k;

    setup();
    region(cq_fdiv_region() - 1u);
    k = block(0u);
    CQ_EXPECT_ABORT((void)cq_fdiv_result(&k));
}

static void a_block_with_no_region(void)
{
    cq_fdiv_block k;

    setup();
    k.a = g_a; k.b = g_b; k.scr = NULL; k.off = 0u;
    CQ_EXPECT_ABORT(cq_fdiv_step(&g_ctx, &k, 0));
}

/* ---- A missing rail. ---------------------------------------------------- */

static void a_block_with_no_operand_a(void)
{
    cq_fdiv_block k;

    setup();
    region(cq_fdiv_region());
    k = block(0u);
    k.a = NULL;
    CQ_EXPECT_ABORT(cq_fdiv_step(&g_ctx, &k, 0));
}

/* ---- The vocabulary. ---------------------------------------------------- */

static void arity_of_an_unknown_op(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fdiv_arity(CQ_FDOP_N_OP));
}

/* THE GUARD THAT CATCHES AN UNREBASED TEMPLATE CODE. Every read of the table
 * goes through `cq_fdiv_row_at`, and the loop's operand references are
 * MATERIALISED from a seven-row template — a rebase that left `TPL_THIS + j`
 * or `TPL_R_IN` behind would arrive here as an index of 4096 or 8192. */
static void row_at_outside_the_program(void)
{
    cq_fdiv_row r;

    setup();
    CQ_EXPECT_ABORT(cq_fdiv_row_at(cq_fdiv_n_rows(), &r));
}

static void width_at_outside_the_program(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fdiv_width_at(-1));
}

/* A malformed table, built here because `cq_fdiv_block` carries no `rows`
 * pointer and the shipped program is well formed by construction. */
static void a_projection_of_a_row_that_is_not_earlier(void)
{
    cq_fdiv_row bad[2];

    setup();
    bad[0].op = (short)CQ_FDOP_OUT; bad[0].s0 = 0; bad[0].s1 = 0;
    bad[0].s2 = 0; bad[0].shift = 0; bad[0].mask = 0u;
    bad[1] = bad[0];
    CQ_EXPECT_ABORT((void)cq_fdiv_row_width(bad, 2, 0));
}

static void a_projection_of_a_row_that_returns_no_tuple(void)
{
    cq_fdiv_row bad[2];

    setup();
    bad[0].op = (short)CQ_FDOP_ADD; bad[0].s0 = -1; bad[0].s1 = -1;
    bad[0].s2 = 0; bad[0].shift = 0; bad[0].mask = 0u;
    bad[1].op = (short)CQ_FDOP_OUT; bad[1].s0 = 0; bad[1].s1 = 0;
    bad[1].s2 = 0; bad[1].shift = 0; bad[1].mask = 0u;
    CQ_EXPECT_ABORT((void)cq_fdiv_row_width(bad, 2, 1));
}

/* ---- D7 at the kernel boundary. ---------------------------------------- */

static void dst_aliases_its_first_source(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fdiv(&g_ctx, g_a, g_a, g_b, CQ_FP64_W));
}

static void sources_alias_each_other(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fdiv(&g_ctx, g_d, g_a, g_a, CQ_FP64_W));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(width_is_not_64),
    CQ_DEATH_CASE(width_is_128),
    CQ_DEATH_CASE(step_index_past_the_end),
    CQ_DEATH_CASE(step_index_is_negative),
    CQ_DEATH_CASE(block_region_is_one_bit_short),
    CQ_DEATH_CASE(block_offset_runs_off_the_region),
    CQ_DEATH_CASE(result_of_a_program_that_does_not_fit),
    CQ_DEATH_CASE(a_block_with_no_region),
    CQ_DEATH_CASE(a_block_with_no_operand_a),
    CQ_DEATH_CASE(arity_of_an_unknown_op),
    CQ_DEATH_CASE(row_at_outside_the_program),
    CQ_DEATH_CASE(width_at_outside_the_program),
    CQ_DEATH_CASE(a_projection_of_a_row_that_is_not_earlier),
    CQ_DEATH_CASE(a_projection_of_a_row_that_returns_no_tuple),
    CQ_DEATH_CASE(dst_aliases_its_first_source),
    CQ_DEATH_CASE(sources_alias_each_other)
)
