/* tests/test_kernel_fma_death.c — M39's fail-loud paths. K20.
 *
 * EACH GUARD IS HERE BECAUSE SOMETHING ELSE WOULD OTHERWISE ANSWER FOR IT —
 * the question to ask before adding any guard (CLAUDE.md: "which single case
 * goes red if this exact line is deleted, and would a later copy of the same
 * guard catch it?"). Which layer WOULD answer is what each group's
 * FAIL_REGULAR_EXPRESSION in tests/CMakeLists.txt pins as forbidden, because
 * a death case's only native claim is "it aborted".
 *
 *   W != 64             PRD-v2 §1 scopes v2 to f64 and `soft_fma` is
 *                       (UInt64, UInt64, UInt64), so another width is a
 *                       FICTION and not an unimplemented case. Masked from
 *                       below by M31's `cq_fp_pack` and by `cq_fp_view`'s
 *                       span check, both pinned absent.
 *   cq_fma_step range   `u` is in [0, cq_fma_steps()). Masked on the HIGH side
 *                       by M39's own fall-off `cq_kernel_die`, a different
 *                       message from the same module, and pinned absent.
 *   cq_fu_arm (region)  the program fits at `off`. Masked from below by M08's
 *                       cq_scratch_span, whose message names the REGION and
 *                       not the consumer that mis-sized the offset.
 *   cq_fu_arm (NULL)    a block with no `scr` at all. Without it the first
 *                       cq_scratch_span dereferences NULL — a SIGSEGV in
 *                       Release, which is not a SIGABRT and so is not even a
 *                       death the window can see.
 *   base64's NULL rail  a block with no `c`. THE THIRD RAIL IS THE ONE THAT
 *                       MATTERS HERE and it is why this case names `c` rather
 *                       than `a`: a guard written for the arity-2 shape and
 *                       copied would check `a` and `b` and let a NULL `c`
 *                       through, which is the exact shape of this kernel's
 *                       one structural novelty.
 *   cq_fma_arity        the op is one of the twenty-one.
 *   cq_fma_row_width    an index outside the program, and a projection of a
 *                       row that returns no tuple.
 *   the operand codes    a ROW INDEX and an OPERAND CODE are M39's OWN
 *                       vocabulary, so nothing below can answer for a wild
 *                       one: it indexes the row table out of bounds, or hands
 *                       the emitter a wire that is not a flag, and in Release
 *                       the read succeeds and only the VALUE is wrong. Nine
 *                       such refusals are driven here; the other four read
 *                       `cq_fma_rows()` themselves and are covered by
 *                       POSITIVE reachability cases in
 *                       tests/test_kernel_fma_map.inc, which says why.
 *   D7a x3, D7b x3      `cq_kernel_check_n(dst, W, src, w, 3)` sizes every
 *                       overlap range PER OPERAND. THREE SOURCES MAKE SIX
 *                       PAIRS WHERE TWO MAKE ONE, and a guard that stopped at
 *                       the first two would pass five of the six. Each is
 *                       driven separately for that reason — the arity-2
 *                       wrapper `cq_kernel_check_dst` cannot express any of
 *                       the `c` rows at all.
 *
 * THE NEXT SEAM, RECORDED BECAUSE THIS FILE IS AT 279 OF RULE 12's 300 after
 * the nine operand-resolution cases joined it: `the KERNEL SURFACE <-> the
 * ROW-TABLE VOCABULARY`. The width, the step-index range, the block's region
 * and the six D7a/D7b pairs are all about a CALLER of `cq_kernel_fma`; the
 * table accessors, the operand codes and the three row-op switches are about
 * the PROGRAM and never run a gate — six of those nine need no `setup()` and
 * no scratch at all. The second group goes to
 * `test_kernel_fma_death_rows.inc`.
 */

#include "kernels/fma.h"
#include "kernels/fma_int.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/fpclass.h"
#include "scratch.h"
#include "support/death.h"

#include <string.h>

static cq_sink    g_sink;
static cq_ctx     g_ctx;
static cq_scratch g_scr;
static cq_bit     g_a[CQ_FP64_W], g_b[CQ_FP64_W], g_c[CQ_FP64_W],
                  g_d[CQ_FP64_W];

static void setup(void)
{
    g_sink = cq_death_null_sink();
    cq_ctx_init(&g_ctx, &g_sink);
    for (int i = 0; i < CQ_FP64_W; i++) {
        g_a[i] = cq_bit_zero(); g_b[i] = cq_bit_zero();
        g_c[i] = cq_bit_zero(); g_d[i] = cq_bit_zero();
    }
    /* A QUANTUM operand, so the kernel's R9 short-circuit does not fold the
     * whole call away before it reaches the guard under test. */
    for (int i = 0; i < CQ_FP64_W; i++) cq_materialise(&g_ctx, &g_a[i]);
}

static void region(uint32_t bits)
{
    cq_scratch_alloc(&g_scr, bits);
    for (uint32_t i = 0; i < g_scr.n; i++) cq_materialise(&g_ctx, &g_scr.bits[i]);
}

static cq_fma_block block(uint32_t off)
{
    cq_fma_block k;

    k.a = g_a; k.b = g_b; k.c = g_c; k.scr = &g_scr; k.off = off;
    return k;
}

/* ---- The width. --------------------------------------------------------- */

static void width_is_not_64(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fma(&g_ctx, g_d, g_a, g_b, g_c, 32));
}

static void width_is_128(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fma(&g_ctx, g_d, g_a, g_b, g_c, 128));
}

/* ---- The step-index range. ---------------------------------------------- */

static void step_index_past_the_end(void)
{
    cq_fma_block k;

    setup();
    region(cq_fma_region());
    k = block(0u);
    CQ_EXPECT_ABORT(cq_fma_step(&g_ctx, &k, cq_fma_steps()));
}

static void step_index_is_negative(void)
{
    cq_fma_block k;

    setup();
    region(cq_fma_region());
    k = block(0u);
    CQ_EXPECT_ABORT(cq_fma_step(&g_ctx, &k, -1));
}

/* ---- The block's own region. -------------------------------------------- */

static void block_region_is_one_bit_short(void)
{
    cq_fma_block k;

    setup();
    region(cq_fma_region() - 1u);
    k = block(0u);
    CQ_EXPECT_ABORT(cq_fma_step(&g_ctx, &k, 0));
}

static void block_offset_runs_off_the_region(void)
{
    cq_fma_block k;

    setup();
    region(cq_fma_region());
    k = block(1u);
    CQ_EXPECT_ABORT(cq_fma_step(&g_ctx, &k, 0));
}

static void a_block_with_no_region(void)
{
    cq_fma_block k;

    setup();
    k.a = g_a; k.b = g_b; k.c = g_c; k.scr = NULL; k.off = 0u;
    CQ_EXPECT_ABORT(cq_fma_step(&g_ctx, &k, 0));
}

/* THE THIRD RAIL, and it is the one guard this kernel owes that no arity-2
 * kernel does.
 *
 * STEP 0 DOES NOT REACH IT, AND THAT IS WHY THIS CASE NAMES A STEP INDEX.
 * Measured: the first draft drove step 0 and reported NO abort, because
 * `soft_fma`'s first TWELVE rows are views — which emit nothing and are only
 * resolved when an operand chain reaches them — and the first EMITTING rows
 * are the class predicates at `:43` and `:44`, both over `a` and `b`. The
 * first row that resolves `c` at all is `c_nan` at `:45`, the THIRD class
 * block.
 *
 * THE INDEX IS ASKED OF M31 RATHER THAN WRITTEN DOWN. Rows 12 and 13 are
 * `is_nan` over `a` and `b`, so `c_nan`'s first slot is exactly two of those
 * blocks in — and if M31's cost ever moves, this case follows it instead of
 * silently landing in the middle of someone else's block and aborting for the
 * wrong reason. */
static void a_block_with_no_operand_c(void)
{
    cq_fma_block k;

    setup();
    region(cq_fma_region());
    k = block(0u);
    k.c = NULL;
    CQ_EXPECT_ABORT(cq_fma_step(&g_ctx, &k,
                                2 * cq_fp_class_steps(CQ_FP_IS_NAN)));
}

/* ---- The table's own accessors. ----------------------------------------- */

static void arity_of_an_unknown_op(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fma_arity(CQ_FUOP_N_OP));
}

static void row_width_outside_the_program(void)
{
    const cq_fma_row *rows;
    int n;

    setup();
    rows = cq_fma_rows(&n);
    CQ_EXPECT_ABORT((void)cq_fma_row_width(rows, n, n));
}

static void a_projection_of_a_row_that_returns_no_tuple(void)
{
    cq_fma_row bad[2];

    setup();
    memset(bad, 0, sizeof bad);
    bad[0].op = (short)CQ_FUOP_ADD;
    bad[1].op = (short)CQ_FUOP_OUT;
    bad[1].s0 = 0; bad[1].s1 = 0;
    CQ_EXPECT_ABORT((void)cq_fma_row_width(bad, 2, 1));
}

static void a_projection_of_a_row_that_is_not_earlier(void)
{
    cq_fma_row bad[2];

    setup();
    memset(bad, 0, sizeof bad);
    bad[0].op = (short)CQ_FUOP_OUT;
    bad[0].s0 = 1; bad[0].s1 = 0;
    bad[1].op = (short)CQ_FUOP_NORM52;
    CQ_EXPECT_ABORT((void)cq_fma_row_width(bad, 2, 0));
}

/* ---- The OPERAND-RESOLUTION refusals (fma_operand.c, fma_step.c). --------
 *
 * THESE ARE THE ONES NOTHING BELOW M39 CAN MAKE. A row index and an operand
 * code are M39's own vocabulary: no sibling module has ever heard of either,
 * so a wild one does not reach a guard one layer down — it indexes the row
 * table out of bounds, or hands the emitter a wire that is not a flag, and in
 * Release the read succeeds, the gate is plausible and only the VALUE is
 * wrong. `kernel:` is what all of them print, which is why each group's
 * FAIL_REGULAR_EXPRESSION bans every OTHER `kernel:` sentence this module and
 * its siblings own rather than banning the prefix.
 *
 * THE TWO WIDTH ROWS ARE ASKED OF THE TABLE, NEVER WRITTEN DOWN: a hard-coded
 * "row 12 is a Bool" would go stale the first time a row moved, and would then
 * pass for the wrong reason. */
static int a_row_of_width(int want)
{
    const cq_fma_row *rows;
    int n;

    rows = cq_fma_rows(&n);
    for (int i = 0; i < n; i++)
        if (cq_fma_row_width(rows, n, i) == want) return i;
    return -1;
}

static void armed(cq_fma_block *k, uint32_t *off)
{
    setup();
    region(cq_fma_region());
    *k = block(0u);
    cq_fu_arm(k, off);
}

static void an_operand_names_a_row_outside_the_program(void)
{
    cq_fma_block k;
    uint32_t off[CQ_FMA_MAX_ROWS];
    cq_bit buf[CQ_FP64_W];
    int n;

    armed(&k, off);
    (void)cq_fma_rows(&n);
    CQ_EXPECT_ABORT((void)cq_fu_val64(&k, off, n, buf));
}

static void a_64_lane_operand_names_a_one_bit_row(void)
{
    cq_fma_block k;
    uint32_t off[CQ_FMA_MAX_ROWS];
    cq_bit buf[CQ_FP64_W];
    int i;

    armed(&k, off);
    i = a_row_of_width(1);
    CQ_DEATH_REQUIRE(i >= 0);
    CQ_EXPECT_ABORT((void)cq_fu_val64(&k, off, i, buf));
}

/* THE 1-BIT SIDE IS "THE REFUSAL THAT GETS FORGOTTEN" — fma_int.h says so in
 * those words, on M36's measurement, so it gets both rows of its own. */
static void a_one_bit_operand_is_not_a_row_of_this_program(void)
{
    cq_fma_block k;
    uint32_t off[CQ_FMA_MAX_ROWS];
    int n;

    armed(&k, off);
    (void)cq_fma_rows(&n);
    CQ_EXPECT_ABORT((void)cq_fu_flag_of(&k, off, n));
}

static void a_one_bit_operand_names_a_64_lane_row(void)
{
    cq_fma_block k;
    uint32_t off[CQ_FMA_MAX_ROWS];
    int i;

    armed(&k, off);
    i = a_row_of_width(CQ_FP64_W);
    CQ_DEATH_REQUIRE(i >= 0);
    CQ_EXPECT_ABORT((void)cq_fu_flag_of(&k, off, i));
}

/* THE CONSTANT VOCABULARY IS CLOSED AND THE SLOT DRIVEN IS `-(N_CODE + 1)`,
 * NOT `K_INDEF - 1`, WHICH IS THE WHOLE OF K4's OTHER HALF. `fma.h`'s
 * `_Static_assert` pins the count against the last NAMED code and cannot see a
 * code added at -22 while the count stays 21 — but this case can, because that
 * code would occupy `-(CQ_FU_N_CODE + 1)` and stop it aborting. Paired with
 * `every_declared_operand_code_resolves`, which walks the other direction. */
static void an_unknown_64_lane_operand_code(void)
{
    cq_fma_block k;
    uint32_t off[CQ_FMA_MAX_ROWS];
    cq_bit buf[CQ_FP64_W], tmp[CQ_FP64_W];

    armed(&k, off);
    CQ_EXPECT_ABORT((void)cq_fu_op64(&k, off, -(CQ_FU_N_CODE + 1), buf, tmp));
}

/* ---- The row-op switches: cost, region, width. --------------------------
 *
 * ALL THREE TAKE A ROW AND SO ALL THREE ARE DRIVABLE, which the table-reading
 * accessors are not. `cq_fma_arity`'s twin already ships; these three are the
 * rest of M36's finding 4 — a `default:` that RETURNS makes a twenty-second op
 * silently take some other op's arm, and `cq_fma_row_width`'s did until this
 * review. Each message is disjoint from the other two, so a case cannot be
 * satisfied by the wrong switch. */
static cq_fma_row an_unknown_row(void)
{
    cq_fma_row r;

    memset(&r, 0, sizeof r);
    r.op = (short)CQ_FUOP_N_OP;
    return r;
}

static void the_cost_of_an_unknown_row_op(void)
{
    cq_fma_row r = an_unknown_row();

    setup();
    CQ_EXPECT_ABORT((void)cq_fu_row_steps(&r));
}

static void the_region_of_an_unknown_row_op(void)
{
    cq_fma_row r = an_unknown_row();

    setup();
    CQ_EXPECT_ABORT((void)cq_fu_row_region(&r));
}

static void the_width_of_an_unknown_row_op(void)
{
    cq_fma_row r = an_unknown_row();

    setup();
    CQ_EXPECT_ABORT((void)cq_fma_row_width(&r, 1, 0));
}

/* THE BARREL DIRECTION IS SHARED BY THE COST HALF AND THE DISPATCH HALF, which
 * fma_int.h names as exactly the kind of fact a second copy gets wrong — so a
 * row that is no barrel reaching it is a dispatch error and must be loud
 * rather than silently `CQ_BARREL_SHL`. */
static void a_barrel_direction_asked_of_a_row_that_is_no_barrel(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fu_barrel_dir_of(CQ_FUOP_ADD));
}

/* ---- D7a: `dst` among the three sources. -------------------------------- */

static void dst_aliases_its_first_source(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fma(&g_ctx, g_a, g_a, g_b, g_c, CQ_FP64_W));
}

static void dst_aliases_its_second_source(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fma(&g_ctx, g_b, g_a, g_b, g_c, CQ_FP64_W));
}

/* THE THIRD SLOT, which the arity-2 wrapper cannot express and which a guard
 * written for two sources and extended by copy-and-paste is the likeliest to
 * miss. */
static void dst_aliases_its_third_source(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fma(&g_ctx, g_c, g_a, g_b, g_c, CQ_FP64_W));
}

/* ---- D7b: two sources aliasing each other, all three pairs. ------------- */

static void sources_a_and_b_alias(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fma(&g_ctx, g_d, g_a, g_a, g_c, CQ_FP64_W));
}

static void sources_a_and_c_alias(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fma(&g_ctx, g_d, g_a, g_b, g_a, CQ_FP64_W));
}

static void sources_b_and_c_alias(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fma(&g_ctx, g_d, g_a, g_b, g_b, CQ_FP64_W));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(width_is_not_64),
    CQ_DEATH_CASE(width_is_128),
    CQ_DEATH_CASE(step_index_past_the_end),
    CQ_DEATH_CASE(step_index_is_negative),
    CQ_DEATH_CASE(block_region_is_one_bit_short),
    CQ_DEATH_CASE(block_offset_runs_off_the_region),
    CQ_DEATH_CASE(a_block_with_no_region),
    CQ_DEATH_CASE(a_block_with_no_operand_c),
    CQ_DEATH_CASE(arity_of_an_unknown_op),
    CQ_DEATH_CASE(row_width_outside_the_program),
    CQ_DEATH_CASE(a_projection_of_a_row_that_returns_no_tuple),
    CQ_DEATH_CASE(a_projection_of_a_row_that_is_not_earlier),
    CQ_DEATH_CASE(an_operand_names_a_row_outside_the_program),
    CQ_DEATH_CASE(a_64_lane_operand_names_a_one_bit_row),
    CQ_DEATH_CASE(a_one_bit_operand_is_not_a_row_of_this_program),
    CQ_DEATH_CASE(a_one_bit_operand_names_a_64_lane_row),
    CQ_DEATH_CASE(an_unknown_64_lane_operand_code),
    CQ_DEATH_CASE(the_cost_of_an_unknown_row_op),
    CQ_DEATH_CASE(the_region_of_an_unknown_row_op),
    CQ_DEATH_CASE(the_width_of_an_unknown_row_op),
    CQ_DEATH_CASE(a_barrel_direction_asked_of_a_row_that_is_no_barrel),
    CQ_DEATH_CASE(dst_aliases_its_first_source),
    CQ_DEATH_CASE(dst_aliases_its_second_source),
    CQ_DEATH_CASE(dst_aliases_its_third_source),
    CQ_DEATH_CASE(sources_a_and_b_alias),
    CQ_DEATH_CASE(sources_a_and_c_alias),
    CQ_DEATH_CASE(sources_b_and_c_alias)
)
