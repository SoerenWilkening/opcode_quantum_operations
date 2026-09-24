/* tests/test_template_intr_death.c — the INTEGER UNARY door's hard errors
 * (bead 9ve.44): `cq_shim_iun` / `cq_shim_iun_unc`, M41's five and M42's abs.
 *
 * A SIXTH DEATH BINARY, separate from test_template_death and
 * test_template_fp_death for the reason those two are separate from each
 * other: its pins are written against the messages these cases must NOT
 * produce, and every novelty here is the new door's — a NINTH tag space's
 * enum, two tables consulted by one request builder, and the one width rule
 * an integer unary opcode has (`bswap`'s multiple of 16), hoisted to the entry
 * point so that it fires BEFORE a rail is minted and a D21 bracket opened.
 *
 * The widened `cq_shim_op` / `cq_shim_fop` / `cq_shim_fun_op` have no case
 * here: their past-the-end guards are the existing cases in
 * tests/test_template_death.c and tests/test_template_fp_death.c, re-aimed at
 * the new LAST enumerator (the old LAST + 1 is now a LEGAL opcode and those
 * cases would have stopped aborting).
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

/* A DISCARDING sink installed before the first cq_shim_ctx(): these cases
 * abort, and a printf sink would interleave gate lines with the message the
 * regex reads. */
static cq_sink g_sink;

static cq_ctx *open_shim(void)
{
    g_sink = cq_death_null_sink();
    cqops_set_sink(&g_sink);
    cq_shim_ctx_reset();
    return cq_shim_ctx();
}

/* --- bswap's width rule, AT THE ENTRY POINT ------------------------------- */

/* `cq_kernel_bswap` refuses W % 16 != 0 in BOTH configurations — one layer
 * down, after `cq_tpl_unary` has minted a rail and opened a D21 bracket it
 * will never close. `kernel:` banned is the whole claim: delete the shim's
 * check and the kernel still aborts, with the banned prefix. */
static void a_bswap_width_that_is_not_a_multiple_of_16_is_refused(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i8(0x12);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, a) == 8u);
    CQ_EXPECT_ABORT((void)cq_shim_iun(CQ_SHIM_IUN_BSWAP, 8, a));
}

/* The `_unc` door runs the same request builder; W = 24 is a multiple of 8
 * and not of 16, the width a byte-count reading would admit. */
static void a_bswap_unc_width_that_is_not_a_multiple_of_16_is_refused(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i32(0);
    const int32_t out = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, a) == 32u);
    CQ_EXPECT_ABORT(cq_shim_iun_unc(CQ_SHIM_IUN_BSWAP, 24, out, a));
}

/* --- the range, BEFORE the enum and the bswap rule ------------------------ */

/* `129` is outside the ABI's width range AND not a multiple of 16, so if the
 * bswap rule ran first it would answer and the range door would never be
 * reachable for this opcode. The ORDER claim, as fcast_req's case makes it. */
static void an_iun_width_outside_the_abis_range_is_refused(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_shim_iun(CQ_SHIM_IUN_BSWAP, 129, cqrt_alloc_i8(0)));
}

/* --- the enum: the entry point, and each table on its own ----------------- */

static void an_iun_op_outside_the_abis_enum_is_refused(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_shim_iun((cq_shim_iun_op)(CQ_SHIM_IUN_ABS + 1), 8,
                                      cqrt_alloc_i8(0)));
}

/* TWO GUARDS, ONE MESSAGE: `iun_req` consults the KERNEL table and then the
 * NAME table and both refuse the same value with the same sentence, so the
 * entry-point case above is satisfied by whichever is still present and
 * identifies neither (bd memory two-guards-one-message-the-entry-point-
 * identifies-neither). These two drive each table directly. */
static void the_iun_kernel_table_refuses_an_index_past_its_end(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_tpl_iun_kernel((cq_shim_iun_op)(CQ_SHIM_IUN_ABS + 1)));
}

static void the_iun_name_table_refuses_an_index_past_its_end(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_tpl_iun_name((cq_shim_iun_op)(CQ_SHIM_IUN_ABS + 1)));
}

/* --- D7a and the handle doors, which are NOT this door's ------------------ */

/* D7a is M07's refusal and must stay M07's: the door hands its handles to
 * `cq_reg_check_operands` and must not grow a copy of the check. */
static void d7a_out_aliases_the_source_of_an_iun_unc(void)
{
    (void)open_shim();
    {
        const int32_t a = cqrt_alloc_i16(7);
        CQ_EXPECT_ABORT(cq_shim_iun_unc(CQ_SHIM_IUN_CTPOP, 16, a, a));
    }
}

/* The source's width is the READ door's claim and the destination's the WRITE
 * door's; both rails are live, so M07 has nothing to say, and the enum and
 * bswap sentences are banned because they have already passed. */
static void an_iun_source_rail_is_not_the_width_its_symbol_names(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_shim_iun(CQ_SHIM_IUN_CTLZ, 32, cqrt_alloc_i16(1)));
}

static void an_iun_unc_destination_is_not_the_width_its_symbol_names(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i32(1);
    const int32_t out = cqrt_alloc_i8(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, out) == 8u);
    CQ_EXPECT_ABORT(cq_shim_iun_unc(CQ_SHIM_IUN_ABS, 32, out, a));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(a_bswap_width_that_is_not_a_multiple_of_16_is_refused),
    CQ_DEATH_CASE(a_bswap_unc_width_that_is_not_a_multiple_of_16_is_refused),
    CQ_DEATH_CASE(an_iun_width_outside_the_abis_range_is_refused),
    CQ_DEATH_CASE(an_iun_op_outside_the_abis_enum_is_refused),
    CQ_DEATH_CASE(the_iun_kernel_table_refuses_an_index_past_its_end),
    CQ_DEATH_CASE(the_iun_name_table_refuses_an_index_past_its_end),
    CQ_DEATH_CASE(d7a_out_aliases_the_source_of_an_iun_unc),
    CQ_DEATH_CASE(an_iun_source_rail_is_not_the_width_its_symbol_names),
    CQ_DEATH_CASE(an_iun_unc_destination_is_not_the_width_its_symbol_names)
)
