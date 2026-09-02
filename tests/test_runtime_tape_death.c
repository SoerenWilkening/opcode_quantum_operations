/* Step 28 (v1.1, PRD §15 D23): the tape surface's hard errors — token / rail
 * confusion in BOTH directions, and the write's width token.
 *
 * TWO OWNERS, AND THE SPLIT IS WHICH LAYER MUST SPEAK, exactly as in
 * tests/test_runtime_rail_death.c. A TOKEN handed to any rail entry point is
 * M07's refusal (plan §0.5: `cq_reg_readable` and `cq_reg_slot_mut` are the two
 * funnels every read, write, free and measure passes through, and both refuse
 * the state) — so those cases assert `FATAL: shim:` is ABSENT, which is what
 * makes them a claim about who owns the refusal and pins that the shim did not
 * grow a masking copy. A RAIL handed as the tape is the SHIM's own — M07 has no
 * opinion about which operand slot a rail was passed in — and those cases
 * assert `FATAL: reg:` is absent.
 *
 * `UndefinedBehaviorSanitizer` is in every negative list (bd u76): a sanitizer's
 * abort() satisfies CQ_EXPECT_ABORT, so in Debug a case whose setup acquired UB
 * would pass having verified nothing.
 */

#include "cq_runtime_abi.h"
#include "cq_shim.h"
#include "cq_shim_ctx.h"

#include "bit.h"
#include "ctx.h"
#include "reg.h"

#include "cqops/cqops.h"

#include "support/bitkinds.h"
#include "support/death.h"

#include <stdint.h>

/* A DISCARDING sink (tests/test_runtime_rail_death.c's, verbatim): these cases
 * build real rails on the way to the assertion and a death binary's stdout is
 * not a trace. */
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

/* --- a RAIL in the tape slot: the shim's own ------------------------------ */

static void a_rail_handed_as_the_tape(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t not_a_tape = cqrt_alloc_i32(1);
    const int32_t src = cqrt_alloc_i32(2);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, not_a_tape));
    CQ_EXPECT_ABORT((void)cqrt_tape_write_i32(not_a_tape, src));
}

/* A TOMBSTONE IS NOT A TOKEN EITHER: the tape slot is checked by STATE, and a
 * freed rail's state is DEAD. Refused by the shim by name, not by M07 as a
 * use-after-free, because the slot itself was wrong before the handle was. */
static void a_freed_rail_handed_as_the_tape(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t dead = cqrt_alloc_i32(0);
    const int32_t src = cqrt_alloc_i32(2);

    cqrt_free(dead);
    CQ_DEATH_REQUIRE(!cq_reg_is_live(&ctx->regs, dead));
    CQ_EXPECT_ABORT((void)cqrt_tape_write_i32_controlled(src, dead, src));
}

/* THE WIDTH TOKEN IS A CLAIM ABOUT THE SOURCE, as it is on the rail surface:
 * `cqrt_tape_write_i8` on a 32-bit rail would keep the low eight bits and call
 * it the value. The message is the tape file's OWN and is worded disjointly
 * from cq_runtime_rail.c's, so the two cannot mask each other. */
static void a_tape_write_of_a_rail_of_the_wrong_width(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t t = cqrt_tape_alloc();
    const int32_t src = cqrt_alloc_i32(5);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, src) == 32u);
    CQ_EXPECT_ABORT((void)cqrt_tape_write_i8(t, src));
}

/* --- a TOKEN handed to a rail entry point: M07's, reached through the shim -- */

static void a_token_handed_to_cqrt_x(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t t = cqrt_tape_alloc();

    CQ_DEATH_REQUIRE(!cq_reg_is_live(&ctx->regs, t));
    CQ_EXPECT_ABORT(cqrt_x(t));
}

static void a_token_handed_to_cqrt_free(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t t = cqrt_tape_alloc();

    CQ_DEATH_REQUIRE(!cq_reg_is_live(&ctx->regs, t));
    CQ_EXPECT_ABORT(cqrt_free(t));
}

static void a_token_handed_to_cqrt_measure(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t t = cqrt_tape_alloc();

    CQ_DEATH_REQUIRE(!cq_reg_is_live(&ctx->regs, t));
    CQ_EXPECT_ABORT((void)cqrt_measure_i32(t));
}

static void a_token_as_the_source_of_a_tape_write(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t t = cqrt_tape_alloc();
    const int32_t t2 = cqrt_tape_alloc();

    CQ_DEATH_REQUIRE(!cq_reg_is_live(&ctx->regs, t2));
    CQ_EXPECT_ABORT((void)cqrt_tape_write_i32(t, t2));
}

/* The §9 flag resolves through cq_reg_cbits in cq_shim_region — M07's read
 * funnel — so a token there is refused by M07 before the region opens. */
static void a_token_as_the_control_flag_of_a_controlled_write(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t t = cqrt_tape_alloc();
    const int32_t src = cqrt_alloc_i32(3);

    CQ_DEATH_REQUIRE(!cq_reg_is_live(&ctx->regs, t));
    CQ_EXPECT_ABORT((void)cqrt_tape_write_i32_controlled(t, t, src));
}

/* A token in a TEMPLATE's operand slot: cq_reg_check_operands refuses it BY
 * NAME (plan §0.5) rather than as "not a live rail". */
static void a_token_as_a_template_operand(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t t = cqrt_tape_alloc();
    const int32_t a = cqrt_alloc_i32(3);

    CQ_DEATH_REQUIRE(!cq_reg_is_live(&ctx->regs, t));
    CQ_EXPECT_ABORT((void)cq_shim_bin_qq(CQ_SHIM_OP_ADD, 32, a, t));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(a_rail_handed_as_the_tape),
    CQ_DEATH_CASE(a_freed_rail_handed_as_the_tape),
    CQ_DEATH_CASE(a_tape_write_of_a_rail_of_the_wrong_width),
    CQ_DEATH_CASE(a_token_handed_to_cqrt_x),
    CQ_DEATH_CASE(a_token_handed_to_cqrt_free),
    CQ_DEATH_CASE(a_token_handed_to_cqrt_measure),
    CQ_DEATH_CASE(a_token_as_the_source_of_a_tape_write),
    CQ_DEATH_CASE(a_token_as_the_control_flag_of_a_controlled_write),
    CQ_DEATH_CASE(a_token_as_a_template_operand)
)
