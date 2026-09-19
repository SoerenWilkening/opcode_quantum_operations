/* tests/test_kernel_fsqrt_death.c — M40's fail-loud paths. K21.
 *
 * NINE GUARDS, AND EACH ONE IS HERE BECAUSE SOMETHING ELSE WOULD OTHERWISE
 * ANSWER FOR IT — the question to ask before adding any guard (CLAUDE.md:
 * "which single case goes red if this exact line is deleted, and would a later
 * copy of the same guard catch it?"). Which layer WOULD answer is what each
 * group's FAIL_REGULAR_EXPRESSION in tests/CMakeLists.txt pins as forbidden,
 * because a death case's only native claim is "it aborted".
 *
 *   W != 64              PRD-v2 §1 scopes v2 to f64 and `soft_fsqrt` is
 *                        (UInt64), so another width is a FICTION and not an
 *                        unimplemented case. Masked from below by M31's
 *                        `cq_fp_pack` (which aborts on a quantum lane) and by
 *                        `cq_fp_view`'s span check, both pinned absent.
 *   cq_fsqrt_step range  `u` is in [0, cq_fsqrt_steps()). Masked from BELOW on
 *                        the LOW side by `cq_fs_row_at`'s own refusal and by
 *                        every inner block; on the HIGH side by the same. Both
 *                        are pinned absent.
 *   cq_fs_arm (region)   the program fits at `off`. Masked from below by M08's
 *                        cq_scratch_span, whose message names the REGION and
 *                        not the consumer that mis-sized the offset.
 *   cq_fs_arm (NULL)     a block with no `scr` at all. Without it the first
 *                        cq_scratch_span dereferences NULL — a SIGSEGV in
 *                        Release, which is not a SIGABRT and so is not even a
 *                        death the window can see.
 *   base64's NULL rail   a block with no `a`. `soft_fsqrt`'s first EMITTING
 *                        row is the `is_nan` CLASS block at :39, whose one
 *                        operand is the rail, so step 0 reaches it at once.
 *   cq_fsqrt_arity       the op is one of the fourteen.
 *   cq_fsqrt_row_width   x3. An index outside the program; a projection of a
 *                        row that is not STRICTLY earlier; and a projection of
 *                        a row that returns no tuple.
 *   cq_fsqrt_loop_row    an iteration or a template index outside the 64 x 16
 *                        digit recurrence. It is the ONE accessor a test uses
 *                        to name a loop row instead of doing seam arithmetic,
 *                        so a silent out-of-range answer would hand every one
 *                        of those cases a plausible wrong row.
 *   cq_kernel_check_n    D7a at the KERNEL boundary. THE D7b LEG IS VACUOUS AT
 *                        ARITY 1 and there is no `sources_alias_each_other`
 *                        case here: `soft_fsqrt` has ONE source, so two
 *                        sources cannot alias. That is why `cq_kernel_fsqrt`
 *                        calls `cq_kernel_check_n(dst, W, src, w, 1)` and not
 *                        the arity-2 wrapper, which would compare `a` with
 *                        itself.
 *
 * THE INNER OPERAND REFUSALS HAVE NO CASE OF THEIR OWN, AND THAT IS M32's AND
 * M34's PRECEDENT RATHER THAN AN OMISSION. `cq_fs_val64`'s "a 64-lane operand
 * names a row of another width", `cq_fs_flag_of`'s mirror, "an operand names a
 * row outside the program", the view chain's termination guard and the
 * arithmetic-shift view's two refusals are reachable only from a MALFORMED row
 * table, and `cq_fsqrt_block` deliberately carries no `rows` pointer — there is
 * one program and a consumer cannot substitute another. They are pinned as
 * FORBIDDEN messages in the step-index group instead (bd
 * a-table-driven-kernel-needs-both-operand-refusals).
 *
 * BOTH CONFIGURATIONS: no case carries CQ_DEATH_SKIP_WITHOUT_INVARIANTS. Every
 * guard above is a plain `if` in library code, not a Debug-gated assert, for
 * kernel.h's reason — risk R2's value is firing during a Release run.
 */

#include "kernels/fsqrt.h"

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

static cq_fsqrt_block block(uint32_t off)
{
    cq_fsqrt_block k;

    k.a = g_a; k.scr = &g_scr; k.off = off;
    return k;
}

/* ---- The width. --------------------------------------------------------- */

static void width_is_not_64(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fsqrt(&g_ctx, g_d, g_a, 32));
}

static void width_is_128(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fsqrt(&g_ctx, g_d, g_a, 128));
}

/* ---- The step-index range. ---------------------------------------------- */

static void step_index_past_the_end(void)
{
    cq_fsqrt_block k;

    setup();
    region(cq_fsqrt_region());
    k = block(0u);
    CQ_EXPECT_ABORT(cq_fsqrt_step(&g_ctx, &k, cq_fsqrt_steps()));
}

static void step_index_is_negative(void)
{
    cq_fsqrt_block k;

    setup();
    region(cq_fsqrt_region());
    k = block(0u);
    CQ_EXPECT_ABORT(cq_fsqrt_step(&g_ctx, &k, -1));
}

/* ---- The region. -------------------------------------------------------- */

static void block_region_is_one_bit_short(void)
{
    cq_fsqrt_block k;

    setup();
    region(cq_fsqrt_region() - 1u);
    k = block(0u);
    CQ_EXPECT_ABORT(cq_fsqrt_step(&g_ctx, &k, 0));
}

static void block_offset_runs_off_the_region(void)
{
    cq_fsqrt_block k;

    setup();
    region(cq_fsqrt_region());
    k = block(1u);
    CQ_EXPECT_ABORT(cq_fsqrt_step(&g_ctx, &k, 0));
}

static void result_of_a_program_that_does_not_fit(void)
{
    cq_fsqrt_block k;

    setup();
    region(cq_fsqrt_region() - 1u);
    k = block(0u);
    CQ_EXPECT_ABORT((void)cq_fsqrt_result(&k));
}

static void a_block_with_no_region(void)
{
    cq_fsqrt_block k;

    setup();
    k.a = g_a; k.scr = NULL; k.off = 0u;
    CQ_EXPECT_ABORT(cq_fsqrt_step(&g_ctx, &k, 0));
}

/* ---- A missing rail. ---------------------------------------------------- */

static void a_block_with_no_operand_a(void)
{
    cq_fsqrt_block k;

    setup();
    region(cq_fsqrt_region());
    k = block(0u);
    k.a = NULL;
    CQ_EXPECT_ABORT(cq_fsqrt_step(&g_ctx, &k, 0));
}

/* ---- The vocabulary. ---------------------------------------------------- */

static void arity_of_an_unknown_op(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fsqrt_arity(CQ_FSOP_N_OP));
}

static void row_width_outside_the_program(void)
{
    const cq_fsqrt_row *rows;
    int n;

    setup();
    rows = cq_fsqrt_rows(&n);
    CQ_EXPECT_ABORT((void)cq_fsqrt_row_width(rows, n, n));
}

/* A malformed table, built here because `cq_fsqrt_block` carries no `rows`
 * pointer and the shipped program is well formed by construction. */
static void a_projection_of_a_row_that_is_not_earlier(void)
{
    cq_fsqrt_row bad[2];

    setup();
    bad[0].op = (short)CQ_FSOP_OUT; bad[0].s0 = 0; bad[0].s1 = 0;
    bad[0].s2 = 0; bad[0].shift = 0; bad[0].mask = 0u;
    bad[1] = bad[0];
    CQ_EXPECT_ABORT((void)cq_fsqrt_row_width(bad, 2, 0));
}

static void a_projection_of_a_row_that_returns_no_tuple(void)
{
    cq_fsqrt_row bad[2];

    setup();
    bad[0].op = (short)CQ_FSOP_ADD; bad[0].s0 = -1; bad[0].s1 = -1;
    bad[0].s2 = 0; bad[0].shift = 0; bad[0].mask = 0u;
    bad[1].op = (short)CQ_FSOP_OUT; bad[1].s0 = 0; bad[1].s1 = 0;
    bad[1].s2 = 0; bad[1].shift = 0; bad[1].mask = 0u;
    CQ_EXPECT_ABORT((void)cq_fsqrt_row_width(bad, 2, 1));
}

/* ---- Naming a loop row. ------------------------------------------------- */

static void loop_row_past_the_last_iteration(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fsqrt_loop_row(CQ_FSQRT_ITERS, 0));
}

static void loop_row_past_the_template(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fsqrt_loop_row(0, CQ_FSQRT_ITER_ROWS));
}

/* ---- D7a at the kernel boundary. --------------------------------------- */

static void dst_aliases_its_source(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fsqrt(&g_ctx, g_a, g_a, CQ_FP64_W));
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
    CQ_DEATH_CASE(row_width_outside_the_program),
    CQ_DEATH_CASE(a_projection_of_a_row_that_is_not_earlier),
    CQ_DEATH_CASE(a_projection_of_a_row_that_returns_no_tuple),
    CQ_DEATH_CASE(loop_row_past_the_last_iteration),
    CQ_DEATH_CASE(loop_row_past_the_template),
    CQ_DEATH_CASE(dst_aliases_its_source)
)
