/* Step 23, landing 1, step 6: the OPCODE surface's hard errors.
 *
 * A SEPARATE BINARY RATHER THAN A SEAM. `CQ_DEATH_MAIN` needs its own
 * translation unit, so this is not a Rule 12 split of tests/test_template.c and
 * IMPLEMENTATION_PLAN §3 records it as its own artefact with its own reserve
 * seam (`the OPCODE SURFACE <-> the HANDLE BOUNDARY`, to
 * tests/test_template_death_d7.inc).
 *
 * THREE GROUPS, AND THE SPLIT IS WHICH LAYER MUST SPEAK.
 *
 *   D7a — `out` among the sources — IS M07's, reached through the shim, and it
 *   is checked on the SIX `_unc` doors ONLY, because they are the only ones
 *   that NAME an `out`: a forward symbol mints its own and cannot alias it.
 *   Measured at Step 7: D7a is 0 of 25,147 `_unc` calls across all 239 goldens,
 *   so this is a guard against something the corpus never does — which is
 *   exactly why it must be a hard error in BOTH configurations rather than a
 *   Debug assert. It breaks Rule 7's `dst ^= f(a,b)` outright, and R2's whole
 *   value is firing during the L6 fixture run at Step 24, where Rule 17 pins L6
 *   under Release.
 *
 *   THE WIDTH TOKEN is the shim's own claim about a rail, and these cases pin
 *   the RESOLVE-BEFORE-COMPARE order that `bd pnu` found unpinned on four
 *   sibling helpers: `cq_reg_width` goes through `cq_reg_slot`, which admits a
 *   TOMBSTONE on purpose so a use-after-free can name its width, so hoisting
 *   the comparison above the resolve turns M07's use-after-free message into
 *   ours. The fixture needs BOTH faults at once — a rail that is NOT LIVE and
 *   of the WRONG width — and a MEASURED rail cannot convict the READ door,
 *   because `cq_reg_cbits` admits one and both orders agree.
 *
 *   THE THREE ENUMS are transcription of a frozen ABI, and a value outside one
 *   indexes past a table into whatever follows it in `.rodata` and CALLS it.
 *   C does not require an enum object to hold one of its enumerators, so this
 *   is reachable rather than theoretical.
 *
 * `UndefinedBehaviorSanitizer` is in every negative list (bd u76): CQ_EXPECT_
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

/* A DISCARDING sink, installed before the first cq_shim_ctx(): these cases
 * build real rails on the way to the assertion and a death binary's stdout is
 * not a trace. tests/test_runtime_rail_death.c's, verbatim. */
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

/* --- D7a, on the six `_unc` doors ----------------------------------------- */

/* THE `qq` DOOR IS THE ONE THE CORPUS COULD REACH, and `out` can coincide with
 * EITHER source, so both lanes get a case: `cq_reg_check_operands` loops, and a
 * guard written for `srcs[0]` only would pass the second. */
static void d7a_out_aliases_the_first_source_of_a_qq_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i8(0x0F);
    const int32_t b = cqrt_alloc_i8(0x33);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, a));
    (void)b;
    CQ_EXPECT_ABORT(cq_shim_bin_qq_unc(CQ_SHIM_OP_ADD, 8, a, a, b));
}

static void d7a_out_aliases_the_second_source_of_a_qq_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i8(0x0F);
    const int32_t b = cqrt_alloc_i8(0x33);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, b));
    CQ_EXPECT_ABORT(cq_shim_bin_qq_unc(CQ_SHIM_OP_ADD, 8, b, a, b));
}

/* THE LITERAL DOORS ARE WHERE CHECKLIST ITEM 6 BITES: `_hl` and `_lh` have ONE
 * handle among two operands, so a shape passing `n_operands` instead of
 * `n_handles` hands `cq_reg_check_operands` an uninitialised slot to test for
 * liveness and for D7a — undefined behaviour that reads as a pass whenever the
 * garbage happens not to equal `out`. */
static void d7a_out_aliases_the_only_handle_of_an_hl_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i8(0x0F);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, a));
    CQ_EXPECT_ABORT(cq_shim_bin_hl_unc(CQ_SHIM_OP_ADD, 8, a, a, 3u, 0u));
}

static void d7a_out_aliases_the_only_handle_of_an_lh_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t b = cqrt_alloc_i8(0x0F);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, b));
    CQ_EXPECT_ABORT(cq_shim_bin_lh_unc(CQ_SHIM_OP_ADD, 8, b, 3u, 0u, b));
}

/* THE COMPARE DOORS ARE THE ONES THE CORPUS ACTUALLY REACHES WITH AN ALIAS —
 * `cq_template_icmp_slt_i32_unc(h1, h0, h0)` at
 * slice_select_rail_alias_cond:12 — but that is D7b, two SOURCES aliasing, and
 * is legal. This is the D7a shape on the same door, where `out` is one bit and
 * the sources are eight, so a check that compared WIDTHS rather than HANDLES
 * would let it through. */
static void d7a_out_aliases_a_source_of_an_icmp_qq_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = cqrt_alloc_i1(false);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, f) == 1u);
    CQ_EXPECT_ABORT(cq_shim_icmp_qq_unc(CQ_SHIM_PRED_EQ, 1, f, f, f));
}

static void d7a_out_aliases_the_source_of_an_icmp_hl_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = cqrt_alloc_i1(false);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, f));
    CQ_EXPECT_ABORT(cq_shim_icmp_hl_unc(CQ_SHIM_PRED_EQ, 1, f, f, 1u, 0u));
}

/* A CAST IS ARITY-1 AND STILL NAMES AN `out`, so D7a is reachable there and is
 * the one shape where `out` and the source have DIFFERENT widths by
 * construction — which is why a guard resting on a width comparison would be
 * green here and only here. */
static void d7a_out_aliases_the_source_of_a_cast_unc(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i8(0x0F);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, a));
    CQ_EXPECT_ABORT(cq_shim_cast_unc(CQ_SHIM_CAST_ZEXT, 8, 32, a, a));
}

/* --- the width token ------------------------------------------------------ */

/* THE SHIM'S OWN REFUSAL: the rail is live and readable, and the symbol's width
 * token is simply not its width. Nothing in M07 can see this — a handle carries
 * no type — so if the shim does not check it, an `i8` opcode runs a `W = 8`
 * kernel over a 32-bit rail's first eight lanes and the top 24 are silently
 * dropped. */
static void a_source_rail_is_not_the_width_its_symbol_names(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i32(0);
    const int32_t b = cqrt_alloc_i8(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, a) == 32u);
    (void)b;
    CQ_EXPECT_ABORT((void)cq_shim_bin_qq(CQ_SHIM_OP_ADD, 8, a, b));
}

/* THE `_unc` DESTINATION GOES THROUGH THE WRITE DOOR and a MEASURED rail is
 * refused there — the asymmetry is M07's (`cq_reg_bits` vs `cq_reg_cbits`) and
 * this file only chooses which door each operand takes. A destination resolved
 * through the READ door would accept a measured rail and write to a rail whose
 * measurement has already been reported. */
static void an_unc_destination_that_has_been_measured_is_a_write_refusal(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a   = cqrt_alloc_i8(0x0F);
    const int32_t out = cqrt_alloc_i8(0);

    (void)cqrt_measure_i8(out);
    CQ_DEATH_REQUIRE(cq_reg_state(&ctx->regs, out) == CQ_SLOT_MEASURED);
    CQ_EXPECT_ABORT(cq_shim_bin_hl_unc(CQ_SHIM_OP_ADD, 8, out, a, 1u, 0u));
}

/* THE `_unc` DESTINATION'S WIDTH IS A SEPARATE GUARD FROM THE SOURCES', and it
 * was missing for one battery round: a mutant deleting `tpl_out`'s comparison
 * SURVIVED the whole suite, because every case drove a destination whose width
 * was already right. `out` is where the result width lives, and it is the one
 * the arity-2 shape gets wrong most easily — `icmp` mints one bit and a cast
 * mints `to_bits`, so "the destination is W wide" is false for two of the three
 * shapes and a caller that hands an `_unc` the wrong rail is not a rare slip. */
static void an_unc_destination_is_not_the_width_its_symbol_names(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a   = cqrt_alloc_i8(0x0F);
    const int32_t out = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, out));
    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, out) == 32u);
    CQ_EXPECT_ABORT(cq_shim_bin_hl_unc(CQ_SHIM_OP_ADD, 8, out, a, 1u, 0u));
}

/* AND THE COMPARE DOOR IS WHERE THE WIDTHS DIFFER BY DESIGN: an `icmp`'s `out`
 * is ONE BIT while its sources are eight, so a guard that compared `out`
 * against the OPERAND width would refuse every legal compare, and one that
 * skipped `out` entirely would accept an eight-bit destination here. */
static void an_icmp_unc_destination_wider_than_one_bit_is_refused(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a   = cqrt_alloc_i8(0x0F);
    const int32_t out = cqrt_alloc_i8(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, out) == 8u);
    CQ_EXPECT_ABORT(cq_shim_icmp_hl_unc(CQ_SHIM_PRED_EQ, 8, out, a, 1u, 0u));
}

/* RESOLVE FIRST, COMPARE SECOND — the order claim, and the fixture needs BOTH
 * faults at once. A freed 32-bit rail handed to an `i8` symbol is a
 * use-after-free AND a width mismatch; the correct order reports M07's, and
 * hoisting the comparison reports ours. The negative list is what makes this a
 * claim about WHICH message rather than merely that one appeared. */
static void a_freed_source_of_the_wrong_width_is_a_use_after_free(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cqrt_alloc_i32(0);
    const int32_t b = cqrt_alloc_i8(0);

    cqrt_free(a);
    CQ_DEATH_REQUIRE(cq_reg_state(&ctx->regs, a) == CQ_SLOT_DEAD);
    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, a) == 32u);
    (void)b;
    CQ_EXPECT_ABORT((void)cq_shim_bin_qq(CQ_SHIM_OP_ADD, 8, a, b));
}

/* --- the three enums and the width range ---------------------------------- */

static void an_opcode_outside_the_abis_enum_is_refused(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_shim_bin_qq((cq_shim_op)(CQ_SHIM_OP_ASHR + 1), 8,
                                         cqrt_alloc_i8(0), cqrt_alloc_i8(0)));
}

static void a_predicate_outside_the_abis_enum_is_refused(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_shim_icmp_qq((cq_shim_pred)(CQ_SHIM_PRED_UGE + 1), 8,
                                          cqrt_alloc_i8(0), cqrt_alloc_i8(0)));
}

static void a_cast_kind_outside_the_abis_enum_is_refused(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_shim_cast((cq_shim_cast_kind)(CQ_SHIM_CAST_TRUNC + 1),
                                       8, 32, cqrt_alloc_i8(0)));
}

/* THE WIDTH IS VALIDATED AS A RANGE AND NEVER AS A WHITELIST (src/reg.h: PRD
 * §2.2's old "1, 8, 16, 32, 64 or 128" is stale against i80). Checked HERE
 * rather than left to `cq_reg_alloc_zero`, so that a NEGATIVE width never
 * reaches the `(uint32_t)` conversion. */
static void a_width_outside_the_abis_range_is_refused(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_shim_bin_qq(CQ_SHIM_OP_ADD, 0,
                                         cqrt_alloc_i8(0), cqrt_alloc_i8(0)));
}

static void a_cast_to_a_width_above_the_abis_range_is_refused(void)
{
    (void)open_shim();
    CQ_EXPECT_ABORT((void)cq_shim_cast(CQ_SHIM_CAST_ZEXT, 8, 129,
                                       cqrt_alloc_i8(0)));
}

/* --- `bd fxz` (b) / PRD §15 D25: where a D2 refusal surfaces to CQ_lang. ---- */

/* THE QUESTION `bd fxz` LEFT OPEN WAS NOT "does the pool fail loud" — M03 has
 * done that since Step 4 — BUT WHERE THAT FAILURE REACHES THE CALLER, and Steps
 * 23–26 are what make it answerable by measurement instead of by deferral. The
 * answer is: at the opcode surface, unswallowed, with the abort coming from the
 * POOL. M26 catches nothing, installs no handler and has no error return to
 * fall back on — every `cq_template_*` either returns a handle or does not
 * return — so a device too small for a kernel's scratch region stops the
 * program at the operation that asked for it, naming the pool, rather than
 * emitting a circuit nobody can run.
 *
 * THE OPERANDS ARE BUILT WITH `cq_bk_reg` AND NOT `cqrt_alloc_i8`, and that is
 * the difference between testing this and testing nothing: an ABI-allocated
 * rail is all-constant, I4 gives it zero qubits, and `mul` would take R9's
 * short-circuit and never ask the pool for anything at all. Every case above
 * uses the ABI allocator because none of them needs a qubit; this one does.
 *
 * i8, NOT i128. The arithmetic is identical and the fixture is 80 qubits rather
 * than 16,640; the width the bead is about is a statement about `qec_n_logical`
 * (§15 D20), not about which width can be made to trip the bound. */
static void a_pool_ceiling_refusal_is_not_swallowed_at_the_opcode_surface(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = cq_bk_reg(ctx, 8u, 0u, 0xFFu);
    const int32_t b = cq_bk_reg(ctx, 8u, 0u, 0xFFu);

    CQ_DEATH_REQUIRE(cq_qubits_live(&ctx->pool) == 16u);
    CQ_DEATH_REQUIRE(a != b);
    /* One short of K11's W² + 2W at W = 8, on top of the operands. */
    cq_qubits_set_ceiling(&ctx->pool, 16u + 80u - 1u);

    CQ_EXPECT_ABORT((void)cq_shim_bin_qq(CQ_SHIM_OP_MUL, 8, a, b));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(d7a_out_aliases_the_first_source_of_a_qq_unc),
    CQ_DEATH_CASE(d7a_out_aliases_the_second_source_of_a_qq_unc),
    CQ_DEATH_CASE(d7a_out_aliases_the_only_handle_of_an_hl_unc),
    CQ_DEATH_CASE(d7a_out_aliases_the_only_handle_of_an_lh_unc),
    CQ_DEATH_CASE(d7a_out_aliases_a_source_of_an_icmp_qq_unc),
    CQ_DEATH_CASE(d7a_out_aliases_the_source_of_an_icmp_hl_unc),
    CQ_DEATH_CASE(d7a_out_aliases_the_source_of_a_cast_unc),
    CQ_DEATH_CASE(a_source_rail_is_not_the_width_its_symbol_names),
    CQ_DEATH_CASE(an_unc_destination_that_has_been_measured_is_a_write_refusal),
    CQ_DEATH_CASE(an_unc_destination_is_not_the_width_its_symbol_names),
    CQ_DEATH_CASE(an_icmp_unc_destination_wider_than_one_bit_is_refused),
    CQ_DEATH_CASE(a_freed_source_of_the_wrong_width_is_a_use_after_free),
    CQ_DEATH_CASE(an_opcode_outside_the_abis_enum_is_refused),
    CQ_DEATH_CASE(a_predicate_outside_the_abis_enum_is_refused),
    CQ_DEATH_CASE(a_cast_kind_outside_the_abis_enum_is_refused),
    CQ_DEATH_CASE(a_width_outside_the_abis_range_is_refused),
    CQ_DEATH_CASE(a_cast_to_a_width_above_the_abis_range_is_refused),
    CQ_DEATH_CASE(a_pool_ceiling_refusal_is_not_swallowed_at_the_opcode_surface)
)
