/* tests/test_template_fp_death.c — the fp ARITHMETIC surface's hard errors
 * (bead 9ve.36).
 *
 * A SEPARATE BINARY RATHER THAN A SEAM, on tests/test_template_death.c's own
 * ground: `CQ_DEATH_MAIN` needs its own translation unit. It is a separate
 * binary from THAT one for the subject reason the whole bead turns on — the fp
 * arithmetic families are where the width doors, the enum guards and the
 * conversion fence are new, and the integer file's pins are written against
 * messages these cases must NOT produce.
 *
 * FOUR GROUPS, AND THE SPLIT IS WHICH LAYER MUST SPEAK.
 *
 *   THE WIDTH DOORS are the shim's own claim and are HOISTED to the entry
 *   point on `shim/cq_template_fp.c`'s precedent: `cq_kernel_fadd` hard-errors
 *   on `W != 64` in both configurations, but it would do so AFTER the call has
 *   minted a rail and opened a D21 bracket, leaving a viewer an operation it
 *   never sees closed. So `kernel:` is pinned ABSENT in every one of these.
 *
 *   THE CONVERSION FENCE is two different refusals that must not be confused.
 *   A width pair that is not one of the seventeen shipped ones is "not
 *   shipped"; `uitofp` from i64 is bead 9ve.34's REFUSAL of a known-wrong
 *   upstream routing, and it must never quietly become `sitofp` — which is why
 *   its case pins the bead's own string and bans the generic one.
 *
 *   THE ENUMS are transcription of a frozen ABI, and a value outside one
 *   indexes past a table into whatever follows it in `.rodata` and CALLS it. C
 *   does not require an enum object to hold one of its enumerators.
 *
 *   D7a — `out` among the sources — IS M07's, reached through the shim, and is
 *   checked on every `_unc` door the new families have, because those are the
 *   only ones that NAME an `out`. The shim must not grow its own copy of the
 *   check, so every string the SHIM could have printed is pinned absent.
 *
 * `UndefinedBehaviorSanitizer` IS IN EVERY NEGATIVE LIST (bd u76): CQ_EXPECT_
 * ABORT arms a SIGABRT window and cannot tell whose abort it caught, and UBSan
 * runs -fno-sanitize-recover=all and calls abort() itself.
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

/* A DISCARDING sink installed before the first cq_shim_ctx(): these cases
 * build real rails on the way to the assertion and a death binary's stdout is
 * not a trace. tests/test_template_death.c's, verbatim. */
static cq_sink g_sink;

static cq_ctx *open_shim(void)
{
    g_sink = cq_death_null_sink();
    cqops_set_sink(&g_sink);
    cq_shim_ctx_reset();
    return cq_shim_ctx();
}

/* --- the width doors ------------------------------------------------------ */

/* PRD-v2 §1 IS f64-ONLY AND THE REFUSAL SAYS SO AT THE CALL. Both cases go
 * through the one request builder, so a second copy of the check could not be
 * what catches either; what the `lh` case adds is that the shape the ABI
 * spells only for `fsub` and `fdiv` reaches that builder at all. */
static void an_fbin_width_that_is_not_64_is_refused(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i32(0);
    const int32_t b = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, a) == 32u);
    CQ_EXPECT_ABORT((void)cq_shim_fbin_qq(CQ_SHIM_FOP_FADD, 32, a, b));
}

static void an_fbin_lh_width_that_is_not_64_is_refused(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t b = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, b));
    CQ_EXPECT_ABORT((void)cq_shim_fbin_lh(CQ_SHIM_FOP_FSUB, 32, 0u, 0u, b));
}

static void a_fun_width_that_is_not_64_is_refused(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, a) == 32u);
    CQ_EXPECT_ABORT((void)cq_shim_fun(CQ_SHIM_FUN_FSQRT, 32, a));
}

/* --- the conversion fence ------------------------------------------------- */

/* SEVENTEEN PAIRS SHIP AND EVERY OTHER ONE IS A FICTION. `fptosi f64 -> i80`
 * is the sharpest shape: both widths are real ABI widths, the direction is
 * right, and only the pair is wrong. */
static void an_fcast_width_pair_that_is_not_shipped_is_refused(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_f64(1.0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, a) == 64u);
    CQ_EXPECT_ABORT((void)cq_shim_fcast(CQ_SHIM_FCAST_FPTOSI, 64, 80, a));
}

/* THE WIDTH IS VALIDATED AS A RANGE BEFORE THE PAIR IS LOOKED UP, so that a
 * value outside the ABI's range never reaches the `(uint32_t)` conversion and
 * never becomes a plausible-looking table index. */
static void an_fcast_to_a_width_above_the_abis_range_is_refused(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_shim_fcast(CQ_SHIM_FCAST_FPTOSI, 64, 129,
                                        cqrt_alloc_f64(1.0)));
}

/* BEAD 9ve.34 AT THE ENTRY POINT, AND THE REFUSAL HAS TO BE HERE RATHER THAN
 * IN THE KERNEL. `cq_kernel_uitofp` does die on `F == 64` — but one layer
 * down, after this call has minted a 64-bit result rail and opened a D21
 * bracket it will never close. The generated `cq_template_uitofp_i64_to_f64`
 * is an abort body and can never reach this door, so this case is the only
 * thing standing between a direct caller and a routing the circuit refuses.
 *
 * AND IT MUST NEVER QUIETLY BECOME `sitofp`: that is the defect — upstream
 * routes UIToFP to soft_sitofp and sitofp.jl:20 reads bit 63 as a sign, so
 * every u >= 2^63 converts negative. A fall-through would return a handle. */
static void uitofp_from_i64_is_refused_at_the_entry_point(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i64(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, a) == 64u);
    CQ_EXPECT_ABORT((void)cq_shim_fcast(CQ_SHIM_FCAST_UITOFP, 64, 64, a));
}

/* --- the three enums ------------------------------------------------------ */

static void an_fop_outside_the_abis_enum_is_refused(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_shim_fbin_qq((cq_shim_fop)(CQ_SHIM_FOP_FDIV + 1),
                                          64, cqrt_alloc_f64(1.0),
                                          cqrt_alloc_f64(1.0)));
}

static void an_fcast_kind_outside_the_abis_enum_is_refused(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_shim_fcast(
        (cq_shim_fcast_kind)(CQ_SHIM_FCAST_UITOFP + 1), 64, 64,
        cqrt_alloc_f64(1.0)));
}

static void a_fun_op_outside_the_abis_enum_is_refused(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_shim_fun((cq_shim_fun_op)(CQ_SHIM_FUN_FSQRT + 1),
                                      64, cqrt_alloc_f64(1.0)));
}

/* --- D7a, on every `_unc` door the new families have ---------------------- */

/* `out` CAN COINCIDE WITH EITHER SOURCE, so both lanes get a case:
 * `cq_reg_check_operands` loops, and a guard written for `srcs[0]` alone would
 * pass the second. */
static void d7a_out_aliases_the_first_source_of_an_fbin_qq_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_f64(1.0);
    const int32_t b = cqrt_alloc_f64(2.0);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, a));
    (void)b;
    CQ_EXPECT_ABORT(cq_shim_fbin_qq_unc(CQ_SHIM_FOP_FADD, 64, a, a, b));
}

static void d7a_out_aliases_the_second_source_of_an_fbin_qq_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_f64(1.0);
    const int32_t b = cqrt_alloc_f64(2.0);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, b));
    CQ_EXPECT_ABORT(cq_shim_fbin_qq_unc(CQ_SHIM_FOP_FADD, 64, b, a, b));
}

/* THE LITERAL DOORS ARE WHERE `n_handles` VERSUS `n_operands` BITES: `_hl` and
 * `_lh` have ONE handle among two operands, so a shape passing 2 hands
 * `cq_reg_check_operands` an uninitialised slot to test for liveness and for
 * D7a — undefined behaviour that reads as a pass whenever the garbage happens
 * not to equal `out`. */
static void d7a_out_aliases_the_only_handle_of_an_fbin_hl_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_f64(1.0);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, a));
    CQ_EXPECT_ABORT(cq_shim_fbin_hl_unc(CQ_SHIM_FOP_FADD, 64, a, a, 0u, 0u));
}

static void d7a_out_aliases_the_only_handle_of_an_fbin_lh_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t b = cqrt_alloc_f64(1.0);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, b));
    CQ_EXPECT_ABORT(cq_shim_fbin_lh_unc(CQ_SHIM_FOP_FSUB, 64, b, 0u, 0u, b));
}

/* ARITY 1 STILL NAMES AN `out`, so D7a is reachable on both arity-1 doors —
 * and the conversion is the one shape where `out` and the source have
 * DIFFERENT widths by construction, which is why a guard resting on a width
 * comparison would be green here and only here. */
static void d7a_out_aliases_the_source_of_an_fcast_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_f64(1.0);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, a));
    CQ_EXPECT_ABORT(cq_shim_fcast_unc(CQ_SHIM_FCAST_FPTOSI, 64, 8, a, a));
}

static void d7a_out_aliases_the_source_of_a_fun_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_f64(1.0);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, a));
    CQ_EXPECT_ABORT(cq_shim_fun_unc(CQ_SHIM_FUN_FSQRT, 64, a, a));
}

/* --- the `_unc` destination's width --------------------------------------- */

/* THE DESTINATION'S WIDTH IS A SEPARATE GUARD FROM THE SOURCES'. On the
 * conversion door it is the one that carries the whole of the composition's
 * claim: `fptosi f64 -> i8` mints EIGHT bits, and an `_unc` handed the 64-bit
 * rail the kernel writes internally would be writing the workspace's width
 * into the result's. */
static void an_fbin_unc_destination_is_not_the_width_its_symbol_names(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a   = cqrt_alloc_f64(1.0);
    const int32_t b   = cqrt_alloc_f64(2.0);
    const int32_t out = cqrt_alloc_i8(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, out) == 8u);
    (void)b;
    CQ_EXPECT_ABORT(cq_shim_fbin_hl_unc(CQ_SHIM_FOP_FADD, 64, out, a, 0u, 0u));
}

static void an_fcast_unc_destination_is_not_the_to_width(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a   = cqrt_alloc_f64(1.0);
    const int32_t out = cqrt_alloc_f64(0.0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, out) == 64u);
    CQ_EXPECT_ABORT(cq_shim_fcast_unc(CQ_SHIM_FCAST_FPTOSI, 64, 8, out, a));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(an_fbin_width_that_is_not_64_is_refused),
    CQ_DEATH_CASE(an_fbin_lh_width_that_is_not_64_is_refused),
    CQ_DEATH_CASE(a_fun_width_that_is_not_64_is_refused),
    CQ_DEATH_CASE(an_fcast_width_pair_that_is_not_shipped_is_refused),
    CQ_DEATH_CASE(an_fcast_to_a_width_above_the_abis_range_is_refused),
    CQ_DEATH_CASE(uitofp_from_i64_is_refused_at_the_entry_point),
    CQ_DEATH_CASE(an_fop_outside_the_abis_enum_is_refused),
    CQ_DEATH_CASE(an_fcast_kind_outside_the_abis_enum_is_refused),
    CQ_DEATH_CASE(a_fun_op_outside_the_abis_enum_is_refused),
    CQ_DEATH_CASE(d7a_out_aliases_the_first_source_of_an_fbin_qq_unc),
    CQ_DEATH_CASE(d7a_out_aliases_the_second_source_of_an_fbin_qq_unc),
    CQ_DEATH_CASE(d7a_out_aliases_the_only_handle_of_an_fbin_hl_unc),
    CQ_DEATH_CASE(d7a_out_aliases_the_only_handle_of_an_fbin_lh_unc),
    CQ_DEATH_CASE(d7a_out_aliases_the_source_of_an_fcast_unc),
    CQ_DEATH_CASE(d7a_out_aliases_the_source_of_a_fun_unc),
    CQ_DEATH_CASE(an_fbin_unc_destination_is_not_the_width_its_symbol_names),
    CQ_DEATH_CASE(an_fcast_unc_destination_is_not_the_to_width)
)
