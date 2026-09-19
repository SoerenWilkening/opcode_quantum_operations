/* tests/test_template_fma_death.c — the fp TERNARY surface's hard errors
 * (bead 9ve.24, review round 1).
 *
 * A FIFTH DEATH BINARY RATHER THAN A SEAM, on tests/test_template_fp_death.c's
 * own ground twice over: `CQ_DEATH_MAIN` needs its own translation unit, and
 * the pins in the arity-2 file are written against messages these cases must
 * NOT produce. `shim/cq_template_ternary.c`'s whole reason for existing is that
 * arity 3 is not arity 2 with a lane added — two literal lanes, six aliasing
 * pairs, a fourth slot in D15's record and an eighth tag space — and each of
 * the groups below is one of those novelties failing loud.
 *
 * FOUR GROUPS, AND THE SPLIT IS WHICH LAYER MUST SPEAK.
 *
 *   THE WIDTH DOOR is the shim's own claim and is HOISTED to the entry point on
 *   `shim/cq_template_fparith.c`'s precedent. `cq_kernel_fma` DOES hard-error on
 *   `W != 64` in both configurations — so deleting `fma_width` leaves the
 *   program aborting anyway, one layer down, AFTER the call has minted a rail
 *   and opened a D21 bracket that is then never closed, which is a FATAL parse
 *   error for the viewer rather than a cosmetic gap. That is the whole content
 *   of these two cases, and it is why `kernel:` is BANNED in their pin: without
 *   that ban the mutant that deletes the hoist passes.
 *
 *   THE ENUM TABLES ARE TWO GUARDS AND ONE MESSAGE, and the entry point
 *   identifies NEITHER. `fma_req` consults `cq_tpl_fma_name` and then
 *   `cq_tpl_fma_kernel`, both of which refuse the same value with the same
 *   sentence — so an out-of-range opcode at the door is caught by whichever is
 *   still present. MEASURED, both directions, by widening one table's threshold
 *   at a time and rebuilding: with the KERNEL guard disabled the entry-point
 *   case stays green (the name table answers); with the NAME guard disabled it
 *   stays green too (the kernel table answers). Each mutant reddened exactly
 *   the one direct case below and nothing else. That is the masking trap this
 *   project has recorded five times, in its symmetric form, and the remedy is
 *   M15's for `cq_addacc_check`: DRIVE EACH GUARD DIRECTLY. So there are three
 *   cases — the entry point, which proves the door is guarded at all and says
 *   nothing about which table did it, and one per table.
 *
 *   D7a — `out` among the sources — IS M07's, reached through the shim, and
 *   three sources make it three separate lanes rather than two. It is driven on
 *   ALL FOUR `_unc` shapes because the literal lanes are what make the lane
 *   index and the `srcs` index DISAGREE: `qlq` packs `{a, c}`, so a guard that
 *   reads `srcs[1]` believing it to be `b` compares the wrong rail, and `qll`
 *   packs one slot, so a shape passing 3 hands `cq_reg_check_operands` two
 *   uninitialised ints to test for liveness and for D7a — undefined behaviour
 *   that reads as a pass whenever the garbage happens not to equal `out`.
 *
 *   THE `_unc` DESTINATION'S WIDTH is a separate guard from the sources', and
 *   at arity 3 the sources are checked in a LOOP while `out` is resolved once,
 *   so a draft that folded the destination into the loop would check it against
 *   whichever lane ran last.
 *
 * `UndefinedBehaviorSanitizer` IS IN EVERY NEGATIVE LIST (bd u76): CQ_EXPECT_
 * ABORT arms a SIGABRT window and cannot tell whose abort it caught, and UBSan
 * runs -fno-sanitize-recover=all and calls abort() itself.
 */

#include "cq_runtime_abi.h"
#include "cq_shim.h"
#include "cq_shim_ctx.h"
#include "cq_template_dispatch.h"

#include "bit.h"
#include "ctx.h"
#include "reg.h"

#include "cqops/cqops.h"

#include "support/death.h"

#include <stdint.h>

/* A DISCARDING sink installed before the first cq_shim_ctx(): these cases build
 * real rails on the way to the assertion and a death binary's stdout is not a
 * trace. tests/test_template_fp_death.c's, verbatim. */
static cq_sink g_sink;

static cq_ctx *open_shim(void)
{
    g_sink = cq_death_null_sink();
    cqops_set_sink(&g_sink);
    cq_shim_ctx_reset();
    return cq_shim_ctx();
}

/* --- the width door ------------------------------------------------------- */

/* PRD-v2 §1 IS f64-ONLY AND `soft_fma` TAKES THREE UInt64. Both cases go
 * through the one request builder, so a second copy of the check could not be
 * what catches either; what the `_unc` case adds is that the shape the ABI
 * spells with two literal lanes reaches that builder at all — `fma_width` runs
 * inside `fma_req`, BEFORE `r.out` is assigned, so an `_unc` door that built
 * its request any other way would skip it. */
static void a_ternary_width_that_is_not_64_is_refused(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i32(0);
    const int32_t b = cqrt_alloc_i32(0);
    const int32_t c = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, a) == 32u);
    CQ_EXPECT_ABORT((void)cq_shim_fma_qqq(CQ_SHIM_FMA_FMA, 32, a, b, c));
}

static void a_ternary_unc_width_that_is_not_64_is_refused(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a   = cqrt_alloc_i32(0);
    const int32_t out = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, out) == 32u);
    CQ_EXPECT_ABORT(cq_shim_fma_qll_unc(CQ_SHIM_FMA_FMA, 32, out, a,
                                        0u, 0u, 0u, 0u));
}

/* --- the two enum tables -------------------------------------------------- */

/* C DOES NOT REQUIRE AN ENUM OBJECT TO HOLD ONE OF ITS ENUMERATORS, and
 * `cq_shim_fma_op` has exactly one — so an out-of-range value indexes past a
 * one-element table into whatever follows it in `.rodata` and CALLS it. */
static void a_ternary_opcode_outside_the_abis_enum_is_refused(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_shim_fma_qqq(
        (cq_shim_fma_op)(CQ_SHIM_FMA_FMA + 1), 64, cqrt_alloc_f64(1.0),
        cqrt_alloc_f64(2.0), cqrt_alloc_f64(3.0)));
}

/* AND THE TWO TABLES ARE DRIVEN DIRECTLY, because at the entry point either
 * one satisfies the case and neither is identified (see the header). These two
 * cases are the only thing that distinguishes "both guards are present" from
 * "one guard is present and the other is a comment". */
static void the_ternary_name_table_refuses_an_index_past_its_end(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_tpl_fma_name(
        (cq_shim_fma_op)(CQ_SHIM_FMA_FMA + 1)));
}

static void the_ternary_kernel_table_refuses_an_index_past_its_end(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_tpl_fma_kernel(
        (cq_shim_fma_op)(CQ_SHIM_FMA_FMA + 1)));
}

/* --- D7a, on all four `_unc` shapes and all three lanes ------------------- */

/* THE THREE LANES OF THE ALL-HANDLE SHAPE. `cq_reg_check_operands` loops over
 * `srcs`, so a guard written for `srcs[0]` alone passes the second and the
 * third — and a guard extended from the arity-2 shape by copy-and-paste passes
 * the third. */
static void d7a_out_aliases_a_of_a_ternary_qqq_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_f64(1.0);
    const int32_t b = cqrt_alloc_f64(2.0);
    const int32_t c = cqrt_alloc_f64(3.0);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, a));
    CQ_EXPECT_ABORT(cq_shim_fma_qqq_unc(CQ_SHIM_FMA_FMA, 64, a, a, b, c));
}

static void d7a_out_aliases_b_of_a_ternary_qqq_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_f64(1.0);
    const int32_t b = cqrt_alloc_f64(2.0);
    const int32_t c = cqrt_alloc_f64(3.0);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, b));
    CQ_EXPECT_ABORT(cq_shim_fma_qqq_unc(CQ_SHIM_FMA_FMA, 64, b, a, b, c));
}

static void d7a_out_aliases_c_of_a_ternary_qqq_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_f64(1.0);
    const int32_t b = cqrt_alloc_f64(2.0);
    const int32_t c = cqrt_alloc_f64(3.0);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, c));
    CQ_EXPECT_ABORT(cq_shim_fma_qqq_unc(CQ_SHIM_FMA_FMA, 64, c, a, b, c));
}

/* `qql` DROPS THE LAST LANE, so `n` is 2 and the two surviving handles are the
 * two the ABI spells first. */
static void d7a_out_aliases_b_of_a_ternary_qql_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_f64(1.0);
    const int32_t b = cqrt_alloc_f64(2.0);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, b));
    CQ_EXPECT_ABORT(cq_shim_fma_qql_unc(CQ_SHIM_FMA_FMA, 64, b, a, b,
                                        0u, 0u));
}

/* `qlq` IS THE SHARP ONE: the literal is in the MIDDLE, so `c` is packed into
 * `srcs[1]` and the lane index and the source index disagree. A guard that
 * believed `srcs[1]` was always `b` would compare a rail that is not in this
 * call at all. */
static void d7a_out_aliases_c_of_a_ternary_qlq_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_f64(1.0);
    const int32_t c = cqrt_alloc_f64(3.0);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, c));
    CQ_EXPECT_ABORT(cq_shim_fma_qlq_unc(CQ_SHIM_FMA_FMA, 64, c, a,
                                        0u, 0u, c));
}

/* AND `qll` HAS ONE HANDLE AMONG THREE OPERANDS — `n_handles` versus
 * `n_operands`, the distinction tests/test_template_fp_death.c records for the
 * arity-2 literal doors, here with TWO uninitialised slots rather than one. */
static void d7a_out_aliases_the_only_handle_of_a_ternary_qll_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_f64(1.0);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, a));
    CQ_EXPECT_ABORT(cq_shim_fma_qll_unc(CQ_SHIM_FMA_FMA, 64, a, a,
                                        0u, 0u, 0u, 0u));
}

/* --- the `_unc` destination's width --------------------------------------- */

/* THE SOURCES ARE CHECKED IN A LOOP AND `out` IS RESOLVED ONCE, so this is not
 * the loop's guard seen again: all three sources here are the width the symbol
 * names and only the destination is not. It must be the shim's own width token
 * and not M07's use-after-free message — all four rails are live — and not the
 * ternary width door's, which `bits == 64` has already passed. */
static void a_ternary_unc_destination_is_not_the_width_its_symbol_names(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a   = cqrt_alloc_f64(1.0);
    const int32_t b   = cqrt_alloc_f64(2.0);
    const int32_t c   = cqrt_alloc_f64(3.0);
    const int32_t out = cqrt_alloc_i8(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, out) == 8u);
    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, a) == 64u);
    CQ_EXPECT_ABORT(cq_shim_fma_qqq_unc(CQ_SHIM_FMA_FMA, 64, out, a, b, c));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(a_ternary_width_that_is_not_64_is_refused),
    CQ_DEATH_CASE(a_ternary_unc_width_that_is_not_64_is_refused),
    CQ_DEATH_CASE(a_ternary_opcode_outside_the_abis_enum_is_refused),
    CQ_DEATH_CASE(the_ternary_name_table_refuses_an_index_past_its_end),
    CQ_DEATH_CASE(the_ternary_kernel_table_refuses_an_index_past_its_end),
    CQ_DEATH_CASE(d7a_out_aliases_a_of_a_ternary_qqq_unc),
    CQ_DEATH_CASE(d7a_out_aliases_b_of_a_ternary_qqq_unc),
    CQ_DEATH_CASE(d7a_out_aliases_c_of_a_ternary_qqq_unc),
    CQ_DEATH_CASE(d7a_out_aliases_b_of_a_ternary_qql_unc),
    CQ_DEATH_CASE(d7a_out_aliases_c_of_a_ternary_qlq_unc),
    CQ_DEATH_CASE(d7a_out_aliases_the_only_handle_of_a_ternary_qll_unc),
    CQ_DEATH_CASE(a_ternary_unc_destination_is_not_the_width_its_symbol_names)
)
