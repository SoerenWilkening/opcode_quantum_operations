/* tests/test_kernel_fmul_death.c — M34's fail-loud paths. K16.
 *
 * NINE GUARDS, AND EACH ONE IS HERE BECAUSE SOMETHING ELSE WOULD OTHERWISE
 * ANSWER FOR IT — the question to ask before adding any guard (CLAUDE.md:
 * "which single case goes red if this exact line is deleted, and would a later
 * copy of the same guard catch it?"). Which layer WOULD answer is what each
 * group's FAIL_REGULAR_EXPRESSION in tests/CMakeLists.txt pins as forbidden,
 * because a death case's only native claim is "it aborted".
 *
 *   W != 64             PRD-v2 §1 scopes v2 to f64 and `soft_fmul` is
 *                       (UInt64, UInt64), so another width is a FICTION and
 *                       not an unimplemented case. Masked from below by
 *                       M31's `cq_fp_pack` (which aborts on a quantum lane)
 *                       and by `cq_fp_view`'s span check, both pinned absent.
 *   cq_fmul_step range  `u` is in [0, cq_fmul_steps()). Masked from BELOW on
 *                       the LOW side by every inner block: a negative index
 *                       reaches the first emitting row — an `xor`, emitted
 *                       directly — and then M02's shadow or ASan speaks.
 *                       Masked on the HIGH side by M34's own fall-off
 *                       `cq_kernel_die`, a different message from the same
 *                       module. Both are pinned absent.
 *   cq_fm_arm (region)  the program fits at `off`. Masked from below by M08's
 *                       cq_scratch_span, whose message names the REGION and
 *                       not the consumer that mis-sized the offset.
 *   cq_fm_arm (NULL)    a block with no `scr` at all. Without it the first
 *                       cq_scratch_span dereferences NULL — a SIGSEGV in
 *                       Release, which is not a SIGABRT and so is not even a
 *                       death the window can see.
 *   base64's NULL rail  a block with no `a`. `soft_fmul`'s first EMITTING row
 *                       is `sa ^ sb` at :28, whose two operands are views over
 *                       the two rails, so step 0 reaches it immediately.
 *   cq_fmul_arity       the op is one of the seventeen. Reached by the row
 *                       table's own well-formedness case and by any future
 *                       consumer that walks the program.
 *   cq_fmul_row_width   x3. An index outside the program; a projection of a
 *                       row that is not STRICTLY earlier; and a projection of
 *                       a row that returns no tuple. Without them a malformed
 *                       table indexes out of bounds and hands the emitter
 *                       whatever it read — a plausible gate on a wrong wire,
 *                       and in Release nothing at all notices.
 *   cq_kernel_check_dst D7a and D7b at the KERNEL boundary. D7b is LEGAL at
 *                       the handle boundary and its remedy is M26's defensive
 *                       cqrt_copy; a kernel that sees the alias is looking at
 *                       a missing copy, and here it would emit phase-P
 *                       Toffolis whose two controls are one physical qubit.
 *
 * THE INNER OPERAND REFUSALS HAVE NO CASE OF THEIR OWN, AND THAT IS M32's
 * PRECEDENT RATHER THAN AN OMISSION. `cq_fm_val64`'s "a 64-lane operand names
 * a row of another width", `cq_fm_flag_of`'s mirror and "an operand names a
 * row outside the program" are reachable only from a MALFORMED row table, and
 * `cq_fmul_block` deliberately carries no `rows` pointer — there is one
 * program and a consumer cannot substitute another. They are pinned as
 * FORBIDDEN messages in the step-index group instead, exactly as M32 pins its
 * three (bd a-table-driven-kernel-needs-both-operand-refusals).
 *
 * BOTH CONFIGURATIONS: no case carries CQ_DEATH_SKIP_WITHOUT_INVARIANTS. Every
 * guard above is a plain `if` in library code, not a Debug-gated assert, for
 * kernel.h's reason — risk R2's value is firing during a Release run.
 */

#include "kernels/fmul.h"

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

static cq_fmul_block block(uint32_t off)
{
    cq_fmul_block k;

    k.a = g_a; k.b = g_b; k.scr = &g_scr; k.off = off;
    return k;
}

/* ---- The width. --------------------------------------------------------- */

static void width_is_not_64(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fmul(&g_ctx, g_d, g_a, g_b, 32));
}

static void width_is_128(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fmul(&g_ctx, g_d, g_a, g_b, 128));
}

/* ---- The step-index range. ---------------------------------------------- */

static void step_index_past_the_end(void)
{
    cq_fmul_block k;

    setup();
    region(cq_fmul_region());
    k = block(0u);
    CQ_EXPECT_ABORT(cq_fmul_step(&g_ctx, &k, cq_fmul_steps()));
}

/* THE LOW SIDE IS THE ONE THE LOWER LAYERS ANSWER FOR. `soft_fmul`'s first
 * emitting row is the `xor` of :28, which this module emits DIRECTLY — so a
 * negative `u` with M34's guard deleted runs off into `cq_fm_sp`'s span check
 * or into ASan, both inside the window and both pinned as forbidden. */
static void step_index_is_negative(void)
{
    cq_fmul_block k;

    setup();
    region(cq_fmul_region());
    k = block(0u);
    CQ_EXPECT_ABORT(cq_fmul_step(&g_ctx, &k, -1));
}

/* ---- The region. -------------------------------------------------------- */

static void block_region_is_one_bit_short(void)
{
    cq_fmul_block k;

    setup();
    region(cq_fmul_region() - 1u);
    k = block(0u);
    CQ_EXPECT_ABORT(cq_fmul_step(&g_ctx, &k, 0));
}

static void block_offset_runs_off_the_region(void)
{
    cq_fmul_block k;

    setup();
    region(cq_fmul_region());
    k = block(1u);
    CQ_EXPECT_ABORT(cq_fmul_step(&g_ctx, &k, 0));
}

static void result_of_a_program_that_does_not_fit(void)
{
    cq_fmul_block k;

    setup();
    region(cq_fmul_region() - 1u);
    k = block(0u);
    CQ_EXPECT_ABORT((void)cq_fmul_result(&k));
}

static void a_block_with_no_region(void)
{
    cq_fmul_block k;

    setup();
    k.a = g_a; k.b = g_b; k.scr = NULL; k.off = 0u;
    CQ_EXPECT_ABORT(cq_fmul_step(&g_ctx, &k, 0));
}

/* ---- A missing rail. ---------------------------------------------------- */

static void a_block_with_no_operand_a(void)
{
    cq_fmul_block k;

    setup();
    region(cq_fmul_region());
    k = block(0u);
    k.a = NULL;
    CQ_EXPECT_ABORT(cq_fmul_step(&g_ctx, &k, 0));
}

/* ---- The vocabulary. ---------------------------------------------------- */

static void arity_of_an_unknown_op(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fmul_arity(CQ_FMOP_N_OP));
}

static void row_width_outside_the_program(void)
{
    const cq_fmul_row *rows;
    int n;

    setup();
    rows = cq_fmul_rows(&n);
    CQ_EXPECT_ABORT((void)cq_fmul_row_width(rows, n, n));
}

/* A malformed table, built here because `cq_fmul_block` carries no `rows`
 * pointer and the shipped program is well formed by construction. */
static void a_projection_of_a_row_that_is_not_earlier(void)
{
    cq_fmul_row bad[2];

    setup();
    bad[0].op = (short)CQ_FMOP_OUT; bad[0].s0 = 0; bad[0].s1 = 0;
    bad[0].s2 = 0; bad[0].shift = 0; bad[0].mask = 0u;
    bad[1] = bad[0];
    CQ_EXPECT_ABORT((void)cq_fmul_row_width(bad, 2, 0));
}

static void a_projection_of_a_row_that_returns_no_tuple(void)
{
    cq_fmul_row bad[2];

    setup();
    bad[0].op = (short)CQ_FMOP_ADD; bad[0].s0 = -1; bad[0].s1 = -1;
    bad[0].s2 = 0; bad[0].shift = 0; bad[0].mask = 0u;
    bad[1].op = (short)CQ_FMOP_OUT; bad[1].s0 = 0; bad[1].s1 = 0;
    bad[1].s2 = 0; bad[1].shift = 0; bad[1].mask = 0u;
    CQ_EXPECT_ABORT((void)cq_fmul_row_width(bad, 2, 1));
}

/* ---- D7 at the kernel boundary. ---------------------------------------- */

static void dst_aliases_its_first_source(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fmul(&g_ctx, g_a, g_a, g_b, CQ_FP64_W));
}

static void sources_alias_each_other(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fmul(&g_ctx, g_d, g_a, g_a, CQ_FP64_W));
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
    CQ_DEATH_CASE(dst_aliases_its_first_source),
    CQ_DEATH_CASE(sources_alias_each_other)
)
