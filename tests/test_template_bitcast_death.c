/* tests/test_template_bitcast_death.c — the one hard error `bitcast` adds to
 * the integer cast door (bead s8g): a bitcast whose two widths differ.
 *
 * A bitcast is a SAME-WIDTH reinterpretation, and its kernel is M13's `zext`
 * at `F == T` — so a direct caller handing the door `(32, 64)` would get a
 * silent zero-extension and `(64, 32)` would reach `zext`'s own inverted-pair
 * refusal one layer down, AFTER a rail has been minted and a D21 bracket
 * opened. The refusal is hoisted to the request builder, where the claim is
 * about the CALL (the bswap rule's precedent in cq_template_unary.c). No
 * generated body can reach it: the yaml's bitcast pairs are all same-width.
 *
 * The pins (tests/CMakeLists.txt, `## s8g`) ban every sentence that is NOT
 * this door's: `kernel:` (so the narrowing case cannot be satisfied by
 * `zext`'s refusal), the width range, the enum and the operand doors.
 */

#include "cq_runtime_abi.h"
#include "cq_shim.h"
#include "cq_shim_ctx.h"

#include "bit.h"
#include "ctx.h"
#include "reg.h"

#include "cqops/cqops.h"

#include "support/death.h"

#include <stdint.h>

static cq_sink g_sink;

static cq_ctx *open_shim(void)
{
    g_sink = cq_death_null_sink();
    cqops_set_sink(&g_sink);
    cq_shim_ctx_reset();
    return cq_shim_ctx();
}

/* THE ONLY GUARD THAT SEES IT: `zext` at (32, 64) is a legal widening. */
static void a_bitcast_that_widens_is_refused_at_the_door(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i32(7);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, a) == 32u);
    CQ_EXPECT_ABORT((void)cq_shim_cast(CQ_SHIM_CAST_BITCAST, 32, 64, a));
}

/* MASKED BY `zext`'s OWN REFUSAL if the door's is deleted — which is why
 * `kernel:` is banned for this case. */
static void a_bitcast_that_narrows_is_refused_at_the_door(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i64(7);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, a) == 64u);
    CQ_EXPECT_ABORT((void)cq_shim_cast(CQ_SHIM_CAST_BITCAST, 64, 32, a));
}

/* The `_unc` door runs the same request builder. Every rail is the width the
 * call names, so neither operand door can answer first. */
static void a_bitcast_unc_whose_widths_differ_is_refused_at_the_door(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i32(7);
    const int32_t out = cqrt_alloc_i64(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, out) == 64u);
    CQ_EXPECT_ABORT(cq_shim_cast_unc(CQ_SHIM_CAST_BITCAST, 32, 64, out, a));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(a_bitcast_that_widens_is_refused_at_the_door),
    CQ_DEATH_CASE(a_bitcast_that_narrows_is_refused_at_the_door),
    CQ_DEATH_CASE(a_bitcast_unc_whose_widths_differ_is_refused_at_the_door)
)
