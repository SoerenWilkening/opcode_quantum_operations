/* tests/test_kernel_fpfield_death.c — M31's fail-loud paths. K22.
 *
 * FIVE GUARDS, AND EACH ONE IS HERE BECAUSE SOMETHING ELSE WOULD OTHERWISE
 * ANSWER FOR IT — the question to ask before adding any guard (CLAUDE.md:
 * "which single case goes red if this exact line is deleted, and would a later
 * copy of the same guard catch it?").
 *
 *   cq_fp_view          the span is inside [0, 64). Masked from BELOW by
 *                       nothing at all: a view emits no gate and allocates no
 *                       qubit, so an out-of-range `lo` reads a neighbouring
 *                       object as a CONTROL and computes a plausible answer.
 *                       In Debug ASan sees the read; in RELEASE nothing does.
 *   cq_fp_pack          every lane is classical. Masked from below by nothing:
 *                       cq_bit_value's own precondition is a CQ_BIT_ASSERT and
 *                       is compiled out of Release entirely.
 *   cq_fp_class_step    `u` is in range. Masked from BELOW by M16 on the low
 *                       side — a negative index reaches cq_eq_step and aborts
 *                       with "eq: step index outside" — which is why
 *                       tests/CMakeLists.txt pins that message as one this
 *                       case must NOT pass on.
 *   check_region        the block fits at its offset. Masked from below by
 *                       M08's cq_scratch_span, whose message names the REGION
 *                       rather than the consumer that mis-sized its offset —
 *                       pinned the same way.
 *   row_of              the class is one of the four. Nothing below M31 knows
 *                       the enum exists; without it the table is indexed out
 *                       of bounds and Release reads whatever follows it.
 *
 * BOTH CONFIGURATIONS: no case carries CQ_DEATH_SKIP_WITHOUT_INVARIANTS.
 * Every guard above is a plain `if` in library code, not a Debug-gated assert,
 * for kernel.h's reason — risk R2's value is firing during a Release run.
 */

#include "kernels/fpclass.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/fpfield.h"
#include "reg.h"
#include "scratch.h"
#include "support/bitkinds.h"
#include "support/death.h"

static cq_sink     g_sink;
static cq_ctx      g_ctx;
static cq_scratch  g_scr;
static cq_bit      g_out[CQ_FP64_W];

static void setup(void)
{
    g_sink = cq_death_null_sink();
    cq_ctx_init(&g_ctx, &g_sink);
}

/* A 64-lane all-classical rail. Classical on purpose: the views and the pack
 * never emit, so nothing beneath M31 can speak for these cases. */
static const cq_bit *rail(void)
{
    int32_t h = cq_bk_reg(&g_ctx, (uint32_t)CQ_FP64_W, 0x7ff8000000000002ull,
                          0x0ull);
    return cq_reg_cbits(&g_ctx.regs, h);
}

/* ---- cq_fp_view: both ends of the span. --------------------------------- */

static void view_span_runs_off_the_top(void)
{
    setup();
    /* lanes 60..67 — the exponent's own width started one lane too high is the
     * realistic shape, and this is that mistake made loudly. */
    CQ_EXPECT_ABORT(cq_fp_view(rail(), 60, 8, g_out));
}

static void view_span_starts_below_zero(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_fp_view(rail(), -1, 4, g_out));
}

static void view_span_is_empty(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_fp_view(rail(), 0, 0, g_out));
}

/* ---- cq_fp_pack: the R9 row runs only on an all-classical rail. --------- */

static void pack_of_a_quantum_lane(void)
{
    int32_t h;

    setup();
    h = cq_bk_reg(&g_ctx, (uint32_t)CQ_FP64_W, 1ull, 1ull);   /* lane 0 is a qubit */
    CQ_EXPECT_ABORT((void)cq_fp_pack(cq_reg_cbits(&g_ctx.regs, h)));
}

/* ---- The block. --------------------------------------------------------- */

/* Nothing is materialised: every guard below fires before a gate is emitted,
 * so the region is there only to give the block well-formed spans. */
static void block(cq_fp_class_block *k, cq_fp_class cls, uint32_t off,
                  uint32_t region)
{
    setup();
    cq_scratch_alloc(&g_scr, region);
    k->a   = rail();
    k->scr = &g_scr;
    k->off = off;
    k->cls = cls;
}

static void class_step_index_past_the_end(void)
{
    cq_fp_class_block k;

    block(&k, CQ_FP_IS_NAN, 0u, cq_fp_class_region(CQ_FP_IS_NAN));
    CQ_EXPECT_ABORT(cq_fp_class_step(&g_ctx, &k,
                                     cq_fp_class_steps(CQ_FP_IS_NAN)));
}

static void class_step_index_is_negative(void)
{
    cq_fp_class_block k;

    block(&k, CQ_FP_IS_INF, 0u, cq_fp_class_region(CQ_FP_IS_INF));
    CQ_EXPECT_ABORT(cq_fp_class_step(&g_ctx, &k, -1));
}

/* The region is one bit short of what the block needs at this offset — the
 * off-by-one a consumer makes when it budgets `cq_fp_class_region` for the
 * wrong class. */
static void class_block_region_is_one_bit_short(void)
{
    cq_fp_class_block k;

    block(&k, CQ_FP_IS_ZERO, 0u, cq_fp_class_region(CQ_FP_IS_ZERO) - 1u);
    CQ_EXPECT_ABORT(cq_fp_class_step(&g_ctx, &k, 0));
}

/* The region is big enough for ONE block and the offset puts this one past the
 * end — the two-blocks-in-one-region shape with the region sized for one. */
static void class_block_offset_runs_off_the_region(void)
{
    cq_fp_class_block k;

    block(&k, CQ_FP_IS_NAN, 1u, cq_fp_class_region(CQ_FP_IS_NAN));
    CQ_EXPECT_ABORT(cq_fp_class_step(&g_ctx, &k, 0));
}

static void class_flag_of_a_block_that_does_not_fit(void)
{
    cq_fp_class_block k;

    block(&k, CQ_FP_IS_SUBNORMAL, 4u,
          cq_fp_class_region(CQ_FP_IS_SUBNORMAL));
    CQ_EXPECT_ABORT((void)cq_fp_class_flag(&k));
}

/* ---- The class enum. ---------------------------------------------------- */

static void steps_of_an_unknown_class(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fp_class_steps((cq_fp_class)CQ_FP_N_CLASS));
}

static void region_of_an_unknown_class(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fp_class_region((cq_fp_class)-1));
}

static void eval_of_an_unknown_class(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fp_class_eval(0ull, (cq_fp_class)7));
}

/* ---- D7a at the kernel boundary. ---------------------------------------- */

/* `dst` is ONE cq_bit overlapping `a[0]`. All-classical, so the kernel takes
 * the R9 short-circuit and its only emission is cq_emit_x on a constant, which
 * carries no distinctness check — nothing beneath M31 speaks in either
 * configuration. The D7b leg is vacuous at arity 1 and must not fire. */
static void is_nan_dst_aliases_its_source(void)
{
    int32_t h;
    cq_bit *a;

    setup();
    h = cq_bk_reg(&g_ctx, (uint32_t)CQ_FP64_W, 0x7ff8000000000002ull, 0x0ull);
    a = cq_reg_bits(&g_ctx.regs, h);

    CQ_EXPECT_ABORT(cq_kernel_fp_is_nan(&g_ctx, a, a));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(view_span_runs_off_the_top),
    CQ_DEATH_CASE(view_span_starts_below_zero),
    CQ_DEATH_CASE(view_span_is_empty),
    CQ_DEATH_CASE(pack_of_a_quantum_lane),
    CQ_DEATH_CASE(class_step_index_past_the_end),
    CQ_DEATH_CASE(class_step_index_is_negative),
    CQ_DEATH_CASE(class_block_region_is_one_bit_short),
    CQ_DEATH_CASE(class_block_offset_runs_off_the_region),
    CQ_DEATH_CASE(class_flag_of_a_block_that_does_not_fit),
    CQ_DEATH_CASE(steps_of_an_unknown_class),
    CQ_DEATH_CASE(region_of_an_unknown_class),
    CQ_DEATH_CASE(eval_of_an_unknown_class),
    CQ_DEATH_CASE(is_nan_dst_aliases_its_source)
)
