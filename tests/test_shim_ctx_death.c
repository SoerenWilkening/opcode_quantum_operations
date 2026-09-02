/* Step 23, landing 1, step 3: the fail-loud paths of shim/cq_shim_ctx.c.
 *
 * Two subjects. (i) PRD §1's v1 boundary really terminates — the message itself
 * is pinned byte for byte in tests/test_shim_ctx.c, which forks to read it;
 * what is asserted here is that the process does not come back. (ii) The one §9
 * region bracket refuses the three things it can be handed that are not a
 * control flag: a rail that is not one bit, a body that is not there, and a body
 * that leaves the control stack unbalanced.
 *
 * EVERY CASE'S FAIL_REGULAR_EXPRESSION NAMES UndefinedBehaviorSanitizer (bd u76).
 * CQ_EXPECT_ABORT arms a SIGABRT window and cannot tell whose abort it caught;
 * UBSan runs -fno-sanitize-recover=all and calls abort() itself, so in Debug a
 * case whose setup acquires undefined behaviour passes having verified nothing.
 * The tripwire is the only thing separating "our die() ran" from "a sanitizer
 * aborted", and it composes with the exit-code check where a
 * PASS_REGULAR_EXPRESSION would displace it.
 */

#include "cq_shim.h"
#include "cq_shim_ctx.h"

#include "bit.h"
#include "controlled.h"
#include "ctx.h"
#include "reg.h"

#include "cqops/cqops.h"

#include "support/death.h"

#include <stdint.h>

/* A DISCARDING sink, the twelfth copy of the same stub set (bd cue tracks
 * consolidating them). It is installed BEFORE the first cq_shim_ctx() so the
 * shim's built-in printf sink does not win: these cases build real rails on
 * their way to the assertion, and a death binary's stdout is not a trace. */
static void nx(void *u, uint32_t q) { (void)u; (void)q; }
static void ncx(void *u, uint32_t c, uint32_t t) { (void)u; (void)c; (void)t; }
static void nccx(void *u, uint32_t a, uint32_t b, uint32_t t)
{ (void)u; (void)a; (void)b; (void)t; }
static void nry(void *u, uint32_t q, double th) { (void)u; (void)q; (void)th; }
static void nrz(void *u, uint32_t q, double ph) { (void)u; (void)q; (void)ph; }
static void nmz(void *u, uint32_t q) { (void)u; (void)q; }

static cq_sink g_sink;

static cq_ctx *open_shim(void)
{
    g_sink.x  = nx;  g_sink.cx = ncx; g_sink.ccx = nccx;
    g_sink.ry = nry; g_sink.rz = nrz; g_sink.mz  = nmz;
    g_sink.user = NULL;
    cqops_set_sink(&g_sink);
    cq_shim_ctx_reset();
    return cq_shim_ctx();
}

/* -------------------------------------------------------------------------
 * PRD §1's boundary.
 * ------------------------------------------------------------------------- */

static void the_v1_boundary_terminates(void)
{
    CQ_EXPECT_ABORT(cq_shim_unsupported("cq_template_sitofp_i32_to_f64",
                                        "fp is v2"));
}

/* -------------------------------------------------------------------------
 * The §9 region bracket's refusals.
 * ------------------------------------------------------------------------- */

static void ran(void *p) { *(int *)p = 1; }

/* MEASURED, NOT ASSUMED: across all 243 of CQ_lang's e2e goldens (CQ_lang at
 * 02afdfe) every one of the 4,944 `cqrt_*_controlled` calls — ALL symbols, not
 * src/controlled.h's copy-only 4,918 at the 239-golden corpus — carries a flag
 * handle minted by cqrt_alloc_i1 (4,096) or returned by an icmp (532) or fcmp
 * (316) template, and `opcode_table.yaml:222-223` gives both compare families
 * `result_type: flag_handle`. So a rail of any other width is a caller bug, and
 * without this guard the bracket would silently READ THE LSB of a wider rail —
 * not "promote against" it, which is the imprecision this comment carried at
 * first: a CLASSICAL LSB makes it row 0's SKIP and emits nothing at all.
 *
 * THE FIXTURE IS DELIBERATELY ALL-CLASSICAL. A wide rail whose bit 0 is a
 * QUBIT looks like the sharper fixture and is measurably the weaker one: under
 * the relaxation this case exists to catch — "only refuse a wide rail whose
 * LSB is quantum" — the quantum fixture still aborts and the case stays GREEN,
 * while this one returns normally and CQ_EXPECT_ABORT reports survived. The
 * guard reads cq_reg_width and never a bit, so no width mutant is
 * kind-sensitive at all. */
static void a_control_flag_wider_than_one_bit(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cq_reg_alloc_zero(&ctx->regs, 8u);
    int fired = 0;

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 8u);
    CQ_EXPECT_ABORT(cq_shim_region(h, ran, &fired));
}

/* "No region at all" is the ABSENCE of the _controlled symbol, not a value of
 * ctrl_flag — so CQ_REG_NONE must not be read as a fifth row 0. It reaches
 * M07's handle check, which is the right layer to refuse it: the shim has no
 * business inventing a sentinel the ABI does not have. */
static void the_no_region_sentinel_is_not_a_control_flag(void)
{
    cq_ctx *ctx = open_shim();
    int fired = 0;

    CQ_DEATH_REQUIRE(ctx != NULL);
    CQ_EXPECT_ABORT(cq_shim_region(CQ_REG_NONE, ran, &fired));
}

static void a_region_with_no_body(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cq_reg_alloc_zero(&ctx->regs, 1u);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 1u);
    CQ_EXPECT_ABORT(cq_shim_region(h, NULL, NULL));
}

/* The bracket pops the frame IT pushed. A body that pushed one of its own and
 * did not pop it would otherwise have its frame popped here and leave the
 * caller's region open — silently, one layer away from the cause, until
 * cq_ctx_dispose finally names it. */
static void a_body_that_leaves_the_control_stack_unbalanced(void *p)
{
    cq_ctx *ctx = (cq_ctx *)p;
    cq_ctrl_push(ctx, &cq_reg_cbits(&ctx->regs, 0)[0]);
}

static void a_region_body_that_does_not_balance_its_own_pushes(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cq_reg_alloc_zero(&ctx->regs, 1u);

    CQ_DEATH_REQUIRE(h == 0);          /* the body reaches it by handle 0 */
    CQ_DEATH_REQUIRE(cq_ctrl_depth(ctx) == 0);
    CQ_EXPECT_ABORT(cq_shim_region(h, a_body_that_leaves_the_control_stack_unbalanced,
                                   ctx));
}

/* The shim never nests, and cq_shim_region checks that premise rather than
 * assuming it — cq_sandwich's precedent, which refuses a nested sandwich in
 * both configurations for exactly the same reason. Reached here by opening a
 * region directly, which is the only way to reach it at all. */
static void a_nested_region(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cq_reg_alloc_zero(&ctx->regs, 1u);
    int fired = 0;

    CQ_DEATH_REQUIRE(cq_ctrl_depth(ctx) == 0);
    cq_ctrl_push(ctx, &cq_reg_cbits(&ctx->regs, h)[0]);
    CQ_DEATH_REQUIRE(cq_ctrl_depth(ctx) == 1);

    CQ_EXPECT_ABORT(cq_shim_region(h, ran, &fired));
}

/* The balance guard is `!= 1`, not `> 1`, and this is the half that says so:
 * without a case for a body that pops MORE than it pushed, nothing
 * distinguishes the two. The refusal must be the SHIM's — M06's own
 * empty-stack abort is one layer down and would mean the shim never looked. */
static void a_body_that_pops_the_frame_it_was_given(void *p)
{
    cq_ctrl_pop((cq_ctx *)p);
}

static void a_region_body_that_pops_more_than_it_pushed(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cq_reg_alloc_zero(&ctx->regs, 1u);

    CQ_DEATH_REQUIRE(cq_ctrl_depth(ctx) == 0);
    CQ_EXPECT_ABORT(cq_shim_region(h, a_body_that_pops_the_frame_it_was_given, ctx));
}

/* A tombstone is M07's refusal and not a width complaint, which is true only
 * because the bracket resolves through cq_reg_cbits BEFORE it compares the
 * width: cq_reg_width goes through cq_reg_slot, which admits CQ_SLOT_DEAD on
 * purpose ("width survives the tombstone, so a use-after-free diagnostic can
 * name it"). With the two in the other order a freed 8-bit flag would be
 * reported as a width bug. */
static void a_tombstoned_control_flag(void)
{
    cq_ctx *ctx = open_shim();

    /* EIGHT BITS, NOT ONE, AND THAT IS THE WHOLE FIXTURE. A width-1 tombstone
     * passes the width comparison whichever order the two guards are in, so it
     * cannot tell them apart. A width-8 one is refused by M07 when cbits comes
     * first and by the SHIM when the width does — and this case's
     * FAIL_REGULAR_EXPRESSION forbids the shim's prefix. */
    const int32_t h = cq_reg_alloc_zero(&ctx->regs, 8u);
    int fired = 0;

    cq_reg_free(ctx, h, NULL);             /* all-constant rail: I4, no proof */
    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, h) == 0);

    CQ_EXPECT_ABORT(cq_shim_region(h, ran, &fired));
}

/* cq_shim_ctx_reset really disposes, and cq_ctx_dispose is what refuses a
 * context with a region still open. Deleting the dispose call leaves every
 * ordinary case green (the next cq_shim_ctx re-inits over the leak); this is
 * the other half of its detector, and the abort must come from the `controlled`
 * layer rather than from the shim's own die. */
static void a_reset_with_a_region_still_open(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cq_reg_alloc_zero(&ctx->regs, 1u);

    cq_ctrl_push(ctx, &cq_reg_cbits(&ctx->regs, h)[0]);
    CQ_DEATH_REQUIRE(cq_ctrl_depth(ctx) == 1);

    CQ_EXPECT_ABORT(cq_shim_ctx_reset());
}

/* --- `bd c55`: the residue read's one refusal. --------------------------- */

/* A NULL DESTINATION IS A HARD ERROR, NEVER A SILENT NO-OP, and the failure
 * direction is why. cqops_read_residue is a DIAGNOSTIC: a caller that asked how
 * much leaked and was quietly handed nothing back would report "no residue" for
 * a program that stranded its whole pool, and would do so with every other
 * check green. Loud here costs the caller a fixed null check they should have
 * anyway; silent costs the one sentence D15 §3 exists to make.
 *
 * IT NEEDS NO CONTEXT AND DELIBERATELY DOES NOT OPEN ONE. The refusal is about
 * the argument, not about the state, so this case reaches it in the window
 * where no context exists — which also asserts the check runs BEFORE the
 * g_live early return rather than after it. */
static void a_residue_read_with_nowhere_to_put_it(void)
{
    cq_shim_ctx_reset();
    CQ_EXPECT_ABORT(cqops_read_residue(NULL));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(the_v1_boundary_terminates),
    CQ_DEATH_CASE(a_control_flag_wider_than_one_bit),
    CQ_DEATH_CASE(the_no_region_sentinel_is_not_a_control_flag),
    CQ_DEATH_CASE(a_region_with_no_body),
    CQ_DEATH_CASE(a_region_body_that_does_not_balance_its_own_pushes),
    CQ_DEATH_CASE(a_region_body_that_pops_more_than_it_pushed),
    CQ_DEATH_CASE(a_nested_region),
    CQ_DEATH_CASE(a_tombstoned_control_flag),
    CQ_DEATH_CASE(a_reset_with_a_region_still_open),
    CQ_DEATH_CASE(a_residue_read_with_nowhere_to_put_it)
)
