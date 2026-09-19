/* tests/test_kernel_fconv_death.c — M37's fail-loud paths. K19.
 *
 * EACH GUARD IS HERE BECAUSE SOMETHING ELSE WOULD OTHERWISE ANSWER FOR IT —
 * the question to ask before adding any guard (CLAUDE.md: "which single case
 * goes red if this exact line is deleted, and would a later copy of the same
 * guard catch it?"). Which layer WOULD answer is what each group's
 * FAIL_REGULAR_EXPRESSION in tests/CMakeLists.txt pins as forbidden, because a
 * death case's only native claim is "it aborted".
 *
 *   uitofp at i64       BEAD 9ve.34, AND IT IS THE ONE CASE IN THIS FILE THAT
 *                       IS NOT ABOUT A MALFORMED CALL. Upstream routes UIToFP
 *                       to `soft_sitofp` and, at a 64-bit source only, emits
 *                       no widening cast at all (instructions.jl:7672-7673),
 *                       so sitofp.jl:20 reads bit 63 as a SIGN and every
 *                       u >= 2^63 converts negative. PRD-v2 §7.9 keeps that
 *                       one width a loud abort; the hazard is that it falls
 *                       through to `sitofp` and returns a plausible negative
 *                       double, which no count, palindrome or pool check can
 *                       see. The guard is BEFORE the shipped-width table
 *                       precisely so adding 64 to that table cannot enable it,
 *                       and the classical row refuses it too — a caller must
 *                       not reach through `cq_uitofp_eval` what the circuit
 *                       refuses.
 *   the width pairs     PRD-v2 §1 scopes v2 to f64 and all three Julia bodies
 *                       are (UInt64), so every pair but the five shipped ones
 *                       is a FICTION and not an unimplemented case. Masked
 *                       from below by M31's `cq_fp_pack` (which aborts on a
 *                       quantum lane) and by `cq_fp_view`'s span check, both
 *                       pinned absent.
 *   cq_fptosi_step      `u` is in [0, cq_fptosi_steps()). Masked from BELOW on
 *                       the LOW side by every inner block — a negative index
 *                       would reach the first emitting row, an `eq` — and on
 *                       the HIGH side by M37's own fall-off `cq_kernel_die`, a
 *                       different message from the same module. Both pinned
 *                       absent.
 *   cq_fv_arm (region)  the program fits at `off`. Masked from below by M08's
 *                       cq_scratch_span, whose message names the REGION and
 *                       not the consumer that mis-sized the offset. This is
 *                       the guard the two-blocks-in-one-region case does NOT
 *                       substitute for and vice versa: that case drives the
 *                       offset being APPLIED, this one drives it being
 *                       CHECKED.
 *   cq_fv_arm (NULL)    a block with no `scr` at all. Without it the first
 *                       cq_scratch_span dereferences NULL — a SIGSEGV in
 *                       Release, which is not a SIGABRT and so is not even a
 *                       death the window can see.
 *   base64's NULL rail  a block with no `a`. `soft_fptosi`'s first EMITTING
 *                       row is the `eq` of :28, whose first operand is a view
 *                       over the rail, so step 0 reaches it immediately.
 *   base64's width      a `cq_fv_ctx` whose `a_w` is outside (0, 64]. Reached
 *                       only through the internal header, which is why it has
 *                       a case: the public surface fixes `a_w` at 64 or at one
 *                       of the four shipped widths.
 *   cq_fconv_arity      the op is one of the fourteen. Reached by the row
 *                       table's own well-formedness case and by any future
 *                       consumer that walks the program.
 *   the operand refusals  x3, through the internal header: a 64-lane operand
 *                       naming a one-bit row, a one-bit operand naming a
 *                       64-lane row, and an unknown negative code. Without
 *                       them a malformed table hands the emitter whatever it
 *                       read — a plausible gate on a wrong wire, and in
 *                       Release nothing at all notices
 *                       (bd a-table-driven-kernel-needs-both-operand-refusals).
 *   cq_kernel_check_n   D7a at the KERNEL boundary: `dst` overlapping the
 *                       source. M07 compares HANDLES and cannot run here;
 *                       with the guard deleted and a CLASSICAL source the
 *                       fold table folds happily and returns a wrong answer in
 *                       silence, in both configurations.
 *
 * BOTH CONFIGURATIONS: no case carries CQ_DEATH_SKIP_WITHOUT_INVARIANTS. Every
 * guard above is a plain `if` in library code, not a Debug-gated assert, for
 * kernel.h's reason — risk R2's value is firing during a Release run.
 */

#include "kernels/fconv.h"
#include "kernels/fconv_int.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "scratch.h"
#include "support/death.h"

static cq_sink     g_sink;
static cq_ctx      g_ctx;
static cq_scratch  g_scr;
static cq_bit      g_a[CQ_FP64_W], g_d[CQ_FP64_W];
static cq_fconv_row g_rows[CQ_FCONV_MAX_ROWS];

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

static cq_fptosi_block block(uint32_t off)
{
    cq_fptosi_block k;

    k.a = g_a; k.scr = &g_scr; k.off = off;
    return k;
}

/* A hand-built internal context, which is how the vocabulary refusals are
 * reached at all: the public `cq_fptosi_block` carries no row pointer, so a
 * consumer cannot substitute a malformed program (bd
 * a-step-block-wires-its-own-inner-block-never-the-consumer). */
static cq_fv_ctx ictx(cq_fconv_prog p, int a_w)
{
    cq_fv_ctx x;

    x.a = g_a; x.a_w = a_w; x.scr = &g_scr; x.off = 0u;
    x.n = cq_fconv_program(p, g_rows);
    x.rows = g_rows;
    return x;
}

/* THE FIRST SLOT OF ROW `i`, AND EVERY OPERAND CASE BELOW NEEDS IT. Slot 0 of
 * `soft_fptosi` belongs to row 5 — the `eq` of :28 — because rows 0-4 are
 * VIEWS and cost nothing, so a case that mutates row 8 and then drives slot 0
 * reaches a row it did not touch and SURVIVES. Measured: three of these cases
 * were written that way and passed their own binary while verifying nothing.
 * The prefix sum is the same one `cq_fv_arm` walks, asked of the module
 * (`cq_fv_row_steps`) rather than written down. */
static int first_slot(const cq_fv_ctx *x, int row)
{
    int u = 0;

    for (int i = 0; i < row; i++) u += cq_fv_row_steps(&x->rows[i]);
    return u;
}

/* The index of the first row of `kind`, so no case carries a bare row number —
 * the number a boundary error moves. */
static int find_row(const cq_fv_ctx *x, int op)
{
    for (int i = 0; i < x->n; i++) if (x->rows[i].op == op) return i;
    return -1;
}

/* ---- Bead 9ve.34: the i64 `uitofp` refusal. ---------------------------- */

static void uitofp_from_i64_is_refused(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_uitofp(&g_ctx, g_d, g_a, 64, CQ_FP64_W));
}

static void uitofp_from_i64_is_refused_classically_too(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_uitofp_eval(UINT64_C(0x8000000000000000), 64));
}

/* ---- The width pairs. --------------------------------------------------- */

static void fptosi_target_is_not_64(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fptosi(&g_ctx, g_d, g_a, CQ_FP64_W, 32));
}

static void fptoui_source_is_not_64(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fptoui(&g_ctx, g_d, g_a, 32, CQ_FP64_W));
}

static void sitofp_source_is_not_64(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_sitofp(&g_ctx, g_d, g_a, 16, CQ_FP64_W));
}

static void uitofp_source_is_not_shipped(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_uitofp(&g_ctx, g_d, g_a, 24, CQ_FP64_W));
}

static void uitofp_target_is_not_64(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_uitofp(&g_ctx, g_d, g_a, 8, 32));
}

/* ---- The step-index range. ---------------------------------------------- */

static void step_index_past_the_end(void)
{
    cq_fptosi_block k;

    setup();
    region(cq_fptosi_region());
    k = block(0u);
    CQ_EXPECT_ABORT(cq_fptosi_step(&g_ctx, &k, cq_fptosi_steps()));
}

/* THE LOW SIDE IS THE ONE THE LOWER LAYERS ANSWER FOR. `soft_fptosi`'s first
 * emitting row is the `eq` of :28, delegated to M16 — so a negative index that
 * got past this guard would be handed to `cq_eq_step`, whose own range check
 * speaks with a DIFFERENT message. Both are pinned absent in CMake so this
 * case is a claim about M37's refusal rather than about M16's. */
static void step_index_is_negative(void)
{
    cq_fptosi_block k;

    setup();
    region(cq_fptosi_region());
    k = block(0u);
    CQ_EXPECT_ABORT(cq_fptosi_step(&g_ctx, &k, -1));
}

/* ---- The region fit. ---------------------------------------------------- */

static void block_region_is_one_bit_short(void)
{
    cq_fptosi_block k;

    setup();
    region(cq_fptosi_region() - 1u);
    k = block(0u);
    CQ_EXPECT_ABORT(cq_fptosi_step(&g_ctx, &k, 0));
}

static void block_offset_runs_off_the_region(void)
{
    cq_fptosi_block k;

    setup();
    region(cq_fptosi_region());
    k = block(1u);
    CQ_EXPECT_ABORT(cq_fptosi_step(&g_ctx, &k, 0));
}

static void result_of_a_program_that_does_not_fit(void)
{
    cq_fptosi_block k;

    setup();
    region(cq_fptosi_region() - 1u);
    k = block(0u);
    CQ_EXPECT_ABORT((void)cq_fptosi_result(&k));
}

static void a_block_with_no_region(void)
{
    cq_fptosi_block k;

    setup();
    k.a = g_a; k.scr = NULL; k.off = 0u;
    CQ_EXPECT_ABORT(cq_fptosi_step(&g_ctx, &k, 0));
}

/* ---- The operand vocabulary. ------------------------------------------- */

static void a_block_with_no_operand_a(void)
{
    cq_fptosi_block k;

    setup();
    region(cq_fptosi_region());
    k = block(0u);
    k.a = NULL;
    CQ_EXPECT_ABORT(cq_fptosi_step(&g_ctx, &k, 0));
}

static void a_source_width_outside_the_range(void)
{
    cq_fv_ctx x;

    setup();
    region(cq_fptosi_region());
    x = ictx(CQ_FCONV_PROG_FPTOSI, 0);
    CQ_EXPECT_ABORT(cq_fv_step(&g_ctx, &x, 0));
}

static void arity_of_an_unknown_op(void)
{
    setup();
    CQ_EXPECT_ABORT((void)cq_fconv_arity(CQ_FVOP_N_OP));
}

/* A 64-LANE OPERAND NAMING A ONE-BIT ROW. `soft_fptosi` row 8 is the `or` of
 * :29, whose second operand is a view over the `is_normal` mux; pointing it at
 * row 5 (the `eq`, a Bool) is the malformed table the resolver must refuse.
 * Without the refusal it reads 64 lanes starting at a one-bit span — a
 * plausible gate on wires belonging to the next block. */
static void a_64_lane_operand_names_a_one_bit_row(void)
{
    cq_fv_ctx x;
    int row, flag;

    setup();
    region(cq_fptosi_region());
    x = ictx(CQ_FCONV_PROG_FPTOSI, CQ_FP64_W);
    row  = find_row(&x, CQ_FVOP_OR);          /* :29's `mant | ...`      */
    flag = find_row(&x, CQ_FVOP_EQ);          /* :28's `exp != 0`, Bool  */
    CQ_DEATH_REQUIRE(row >= 0 && flag >= 0);
    g_rows[row].s0 = (short)flag;
    CQ_EXPECT_ABORT(cq_fv_step(&g_ctx, &x, first_slot(&x, row)));
}

/* THE MIRROR, AND IT IS THE ONE THAT GETS FORGOTTEN. A mux `cond` pointed at a
 * 64-lane row reads a wire that is not a flag: the read succeeds, the gate is
 * plausible and the value is wrong, and Release has no other detector. */
static void a_one_bit_operand_names_a_64_lane_row(void)
{
    cq_fv_ctx x;
    int row, wide;

    setup();
    region(cq_fptosi_region());
    x = ictx(CQ_FCONV_PROG_FPTOSI, CQ_FP64_W);
    row  = find_row(&x, CQ_FVOP_MUX);         /* :28's `ifelse`, cond    */
    wide = find_row(&x, CQ_FVOP_OR);          /* :29's `full_mant`, 64   */
    CQ_DEATH_REQUIRE(row >= 0 && wide >= 0);
    g_rows[row].s0 = (short)wide;
    CQ_EXPECT_ABORT(cq_fv_step(&g_ctx, &x, first_slot(&x, row)));
}

static void an_operand_names_no_constant(void)
{
    cq_fv_ctx x;
    int row;

    setup();
    region(cq_fptosi_region());
    x = ictx(CQ_FCONV_PROG_FPTOSI, CQ_FP64_W);
    row = find_row(&x, CQ_FVOP_EQ);
    CQ_DEATH_REQUIRE(row >= 0);
    g_rows[row].s1 = (short)(CQ_FV_K_BIAS - 1);
    CQ_EXPECT_ABORT(cq_fv_step(&g_ctx, &x, first_slot(&x, row)));
}

static void an_operand_names_a_row_outside_the_program(void)
{
    cq_fv_ctx x;
    int row;

    setup();
    region(cq_fptosi_region());
    x = ictx(CQ_FCONV_PROG_FPTOSI, CQ_FP64_W);
    row = find_row(&x, CQ_FVOP_OR);
    CQ_DEATH_REQUIRE(row >= 0);
    g_rows[row].s0 = (short)x.n;
    CQ_EXPECT_ABORT(cq_fv_step(&g_ctx, &x, first_slot(&x, row)));
}

/* ---- D7a at the kernel boundary. --------------------------------------- */

static void dst_aliases_its_source(void)
{
    setup();
    CQ_EXPECT_ABORT(cq_kernel_fptosi(&g_ctx, g_a, g_a, CQ_FP64_W, CQ_FP64_W));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(uitofp_from_i64_is_refused),
    CQ_DEATH_CASE(uitofp_from_i64_is_refused_classically_too),
    CQ_DEATH_CASE(fptosi_target_is_not_64),
    CQ_DEATH_CASE(fptoui_source_is_not_64),
    CQ_DEATH_CASE(sitofp_source_is_not_64),
    CQ_DEATH_CASE(uitofp_source_is_not_shipped),
    CQ_DEATH_CASE(uitofp_target_is_not_64),
    CQ_DEATH_CASE(step_index_past_the_end),
    CQ_DEATH_CASE(step_index_is_negative),
    CQ_DEATH_CASE(block_region_is_one_bit_short),
    CQ_DEATH_CASE(block_offset_runs_off_the_region),
    CQ_DEATH_CASE(result_of_a_program_that_does_not_fit),
    CQ_DEATH_CASE(a_block_with_no_region),
    CQ_DEATH_CASE(a_block_with_no_operand_a),
    CQ_DEATH_CASE(a_source_width_outside_the_range),
    CQ_DEATH_CASE(arity_of_an_unknown_op),
    CQ_DEATH_CASE(a_64_lane_operand_names_a_one_bit_row),
    CQ_DEATH_CASE(a_one_bit_operand_names_a_64_lane_row),
    CQ_DEATH_CASE(an_operand_names_no_constant),
    CQ_DEATH_CASE(an_operand_names_a_row_outside_the_program),
    CQ_DEATH_CASE(dst_aliases_its_source)
)
