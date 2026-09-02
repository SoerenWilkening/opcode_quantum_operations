/* tests/test_runtime_qram_death.c — Step 27 (v1.2, PRD §15 D24): the QRAM
 * surface's hard errors — token / rail confusion in BOTH directions and across
 * the two token OWNERS, the width tokens, and the tape's LIFO contract.
 *
 * TWO OWNERS, AND THE SPLIT IS WHICH LAYER MUST SPEAK, as in the tape suite. A
 * TOKEN handed to a rail entry point is M07's (`FATAL: reg:`, through its two
 * funnels), and those cases assert `FATAL: shim:` is ABSENT. A RAIL or a TAPE
 * token as the array, an array token as a tape, a wrong width or a bad pop is
 * the SHIM's own, and those assert `FATAL: reg:` is absent. The messages of
 * cq_runtime_qram.c, cq_runtime_tape.c and cq_runtime_rail.c are worded
 * disjointly so the pins can tell them apart.
 */

#include "cq_runtime_abi.h"
#include "cq_shim.h"
#include "cq_shim_ctx.h"
#include "cq_shim_qram.h"

#include "bit.h"
#include "ctx.h"
#include "reg.h"

#include "cqops/cqops.h"

#include "support/bitkinds.h"
#include "support/death.h"

#include <stdint.h>

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

/* --- the shim's own ------------------------------------------------------- */

static void a_rail_handed_as_the_array(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t not_an_array = cqrt_alloc_i32(1);
    const int32_t idx = cqrt_alloc_i32(0);
    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, not_an_array));
    CQ_EXPECT_ABORT((void)cqrt_qram_load_i32(not_an_array, idx));
}

static void a_tape_token_handed_as_the_array(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t t = cqrt_tape_alloc();
    const int32_t idx = cqrt_alloc_i32(0);
    const int32_t val = cqrt_alloc_i32(3);
    CQ_DEATH_REQUIRE(cq_reg_state(&ctx->regs, t) == CQ_SLOT_TOKEN);
    CQ_DEATH_REQUIRE(cq_qram_find(t) == NULL);
    CQ_EXPECT_ABORT(cqrt_qram_store_i32(t, idx, val));
}

static void an_array_token_handed_as_a_tape(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_qram_alloc_i32(4);
    const int32_t src = cqrt_alloc_i32(3);
    CQ_DEATH_REQUIRE(cq_reg_state(&ctx->regs, a) == CQ_SLOT_TOKEN);
    CQ_EXPECT_ABORT((void)cqrt_tape_write_i32(a, src));
}

static void an_array_of_another_element_width(void)
{
    (void)open_shim();
    const int32_t a = cqrt_qram_alloc_i32(4);
    const int32_t idx = cqrt_alloc_i32(0);
    CQ_EXPECT_ABORT((void)cqrt_qram_load_i8(a, idx));
}

static void a_count_of_zero(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cqrt_qram_alloc_i32(0));
}

/* The bound fires BEFORE a single cell is minted: without it an alloc at
 * 2^28 + 1 would try to mint that many registers. */
static void a_count_beyond_the_bound(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cqrt_qram_alloc_i32((1 << 28) + 1));
}

static void an_out_of_the_wrong_width(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_qram_alloc_i32(4);
    const int32_t idx = cqrt_alloc_i32(0);
    const int32_t out = cqrt_alloc_i8(0);
    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, out) == 8u);
    CQ_EXPECT_ABORT(cqrt_qram_load_i32_unc(out, a, idx));
}

static void a_value_of_the_wrong_width(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_qram_alloc_i32(4);
    const int32_t idx = cqrt_alloc_i32(0);
    const int32_t val = cqrt_alloc_i8(3);
    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, val) == 8u);
    CQ_EXPECT_ABORT(cqrt_qram_store_i32(a, idx, val));
}

static void an_index_that_is_not_i32(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_qram_alloc_i32(4);
    const int32_t idx = cqrt_alloc_i8(0);
    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, idx) == 8u);
    CQ_EXPECT_ABORT((void)cqrt_qram_load_i32(a, idx));
}

static void a_pop_on_an_empty_tape(void)
{
    (void)open_shim();
    const int32_t a = cqrt_qram_alloc_i32(4);
    const int32_t idx = cqrt_alloc_i32(0);
    const int32_t val = cqrt_alloc_i32(3);
    CQ_DEATH_REQUIRE(cq_qram_top(cq_qram_find(a)) == NULL);
    CQ_EXPECT_ABORT(cqrt_qram_store_i32_unc(a, idx, val));
}

static void a_pop_that_is_not_the_last_push(void)
{
    (void)open_shim();
    const int32_t a = cqrt_qram_alloc_i32(4);
    const int32_t i1 = cqrt_alloc_i32(0), i2 = cqrt_alloc_i32(1);
    const int32_t val = cqrt_alloc_i32(3);
    cqrt_qram_store_i32(a, i1, val);
    cqrt_qram_store_i32(a, i2, val);
    CQ_DEATH_REQUIRE(cq_qram_find(a)->n_tape == 2u);
    CQ_EXPECT_ABORT(cqrt_qram_store_i32_unc(a, i1, val));   /* i2 is the top */
}

static void a_controlled_pop_of_an_uncontrolled_push(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_qram_alloc_i32(4);
    const int32_t idx = cqrt_alloc_i32(0);
    const int32_t val = cqrt_alloc_i32(3);
    const int32_t pred = cq_bk_reg(ctx, 1u, 1u, 0u);
    cqrt_qram_store_i32(a, idx, val);
    CQ_EXPECT_ABORT(cqrt_qram_store_i32_controlled_unc(pred, a, idx, val));
}

/* --- M07's, reached through the shim --------------------------------------- */

static void an_array_token_handed_to_cqrt_x(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_qram_alloc_i1(2);
    CQ_DEATH_REQUIRE(!cq_reg_is_live(&ctx->regs, a));
    CQ_EXPECT_ABORT(cqrt_x(a));
}

static void an_array_token_handed_to_cqrt_free(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_qram_alloc_i32(2);
    CQ_DEATH_REQUIRE(!cq_reg_is_live(&ctx->regs, a));
    CQ_EXPECT_ABORT(cqrt_free(a));
}

static void an_array_token_as_the_index(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_qram_alloc_i32(2);
    CQ_DEATH_REQUIRE(!cq_reg_is_live(&ctx->regs, a));
    CQ_EXPECT_ABORT((void)cqrt_qram_load_i32(a, a));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(a_rail_handed_as_the_array),
    CQ_DEATH_CASE(a_tape_token_handed_as_the_array),
    CQ_DEATH_CASE(an_array_token_handed_as_a_tape),
    CQ_DEATH_CASE(an_array_of_another_element_width),
    CQ_DEATH_CASE(a_count_of_zero),
    CQ_DEATH_CASE(a_count_beyond_the_bound),
    CQ_DEATH_CASE(an_out_of_the_wrong_width),
    CQ_DEATH_CASE(a_value_of_the_wrong_width),
    CQ_DEATH_CASE(an_index_that_is_not_i32),
    CQ_DEATH_CASE(a_pop_on_an_empty_tape),
    CQ_DEATH_CASE(a_pop_that_is_not_the_last_push),
    CQ_DEATH_CASE(a_controlled_pop_of_an_uncontrolled_push),
    CQ_DEATH_CASE(an_array_token_handed_to_cqrt_x),
    CQ_DEATH_CASE(an_array_token_handed_to_cqrt_free),
    CQ_DEATH_CASE(an_array_token_as_the_index)
)
