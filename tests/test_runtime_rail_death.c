/* Step 23, landing 1, step 4: the rail surface's hard errors.
 *
 * TWO GROUPS, AND THE SPLIT IS WHICH LAYER MUST SPEAK — which is why every
 * case's FAIL_REGULAR_EXPRESSION in tests/CMakeLists.txt names the strings the
 * OTHER refusals would have printed. All five shim-owned refusals share the
 * `libcqops: FATAL: shim:` prefix, so an exit-code assertion cannot tell them
 * apart, and swapping two of their strings would survive every case here —
 * CLAUDE.md's recorded Step 20 survivor (ii), "NO TEST READ THE REFUSAL
 * MESSAGE".
 *
 *   THE SHIM'S OWN: the width token is a claim about the rail, and `cqrt_cswap`
 *   refuses three coincidences that M05 catches in DEBUG ONLY — measured, in
 *   Release all three return 0 having emitted `cx q0 q0` or a Toffoli whose two
 *   controls are one physical qubit, which is a malformed circuit with the
 *   right value and no diagnostic.
 *
 *   M07's, REACHED THROUGH THE SHIM: double free, free of a measured rail, use
 *   of a tombstone. Those cases assert `FATAL: shim:` is ABSENT, which is what
 *   makes them a claim about who owns the refusal rather than merely that one
 *   happened. They also pin that the shim does not re-implement a guard M07
 *   already has — a duplicated one would be the masking-layer trap this project
 *   has recorded four times.
 *
 * `UndefinedBehaviorSanitizer` is in every negative list (bd u76): CQ_EXPECT_
 * ABORT arms a SIGABRT window and cannot tell whose abort it caught, and UBSan
 * runs -fno-sanitize-recover=all and calls abort() itself — so in Debug a case
 * whose setup acquires undefined behaviour would pass having verified nothing.
 */

#include "cq_runtime_abi.h"
#include "cq_shim_ctx.h"

#include "bit.h"
#include "ctx.h"
#include "reg.h"

#include "cqops/cqops.h"

#include "support/bitkinds.h"
#include "support/death.h"

#include <stdint.h>

/* A DISCARDING sink, installed before the first cq_shim_ctx() so the built-in
 * printf sink does not win: these cases build real rails on the way to the
 * assertion and a death binary's stdout is not a trace. */
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

/* A one-bit rail carrying a real WIRE. The ABI has no way to spell one — every
 * `cqrt_alloc_i*` gives an all-CONSTANT rail owning zero qubits (I4) — and it
 * matters for the coincidence cases: `cq_bit_coincident` fires only on two
 * CQ_BIT_Q bits, so with constants M05's Debug-gated distinctness assert cannot
 * fire at all, the `distinctness` entry in the negative list discriminates
 * against nothing, and the Release hazard the guard exists for is never
 * reached. */
static int32_t wire(cq_ctx *ctx)
{
    const int32_t h = cqrt_alloc_i1(false);

    cq_reg_bits(&ctx->regs, h)[0] = cq_bit_qubit(cq_ctx_fresh_qubit(ctx));
    return h;
}

/* --- The width token is a claim about the rail ---------------------------- */

static void measure_of_a_rail_wider_than_its_symbol(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 32u);
    CQ_EXPECT_ABORT((void)cqrt_measure_i8(h));
}

static void copy_between_rails_of_different_widths(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t src = cqrt_alloc_i8(1);
    const int32_t dst = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, src) == 8u);
    CQ_EXPECT_ABORT(cqrt_copy_i8(src, dst));
}

/* THE ONE COPY SHAPE ONLY THE SHIM CAN REFUSE, and the case above cannot see.
 * With rails of DIFFERENT widths `cq_reg_xor_into` refuses too, so deleting the
 * shim's width check leaves that case green on M07's message — measured. Two
 * rails of the SAME wrong width is the shape where M07 finds nothing to
 * complain about and only the ABI's width TOKEN is violated: without the guard
 * the library silently copies 32 bits for a symbol that says i8. */
static void copy_of_two_rails_of_the_same_wrong_width(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t src = cqrt_alloc_i32(1);
    const int32_t dst = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, src) == cq_reg_width(&ctx->regs, dst));
    CQ_EXPECT_ABORT(cqrt_copy_i8(src, dst));
}

static void addc_of_a_rail_wider_than_its_symbol(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i32(1);

    CQ_DEATH_REQUIRE(cq_reg_owned_qubits(&ctx->regs, h) == 0u);
    CQ_EXPECT_ABORT(cqrt_addc_i8(h, 3));
}

static void xorc_of_a_rail_wider_than_its_symbol(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i16(1);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 16u);
    CQ_EXPECT_ABORT(cqrt_xorc_i8(h, 3));
}

/* --- cqrt_cswap's three coincidence refusals ------------------------------ */

/* CQ_lang's own checker calls this one DEGENERATE rather than non-unitary —
 * `cswap(f, x, x)` is the identity, so it is refused because no router can have
 * meant it. Leaning on `cq_reg_swap_bits`'s own `a == b` refusal would cover
 * the CQ_BIT_ONE row alone and leave the ZERO row a silent no-op. */
static void cswap_of_a_rail_with_itself(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = wire(ctx);
    const int32_t a = cq_bk_reg(ctx, 32u, 3u, 0xFFFFFFFFu);

    CQ_DEATH_REQUIRE(cq_reg_owned_qubits(&ctx->regs, a) == 32u);
    CQ_EXPECT_ABORT(cqrt_cswap(f, a, a));
}

/* CQ_lang's `B1-input-aliases-written-rail`: `cswap(c, c, b)` sends both |1,0>
 * and |0,1> to |0,1>, so no unitary implements it. THE RAILS CARRY WIRES, and a
 * first draft of this comment claimed that while the fixture used
 * `cqrt_alloc_i1(false)` — an all-CONSTANT rail. It matters: with constants the
 * shim's refusal is the only thing that could ever fire, so the negative list's
 * `distinctness` entry had nothing to discriminate against and the case could
 * not have distinguished the shim's guard from M05's Debug-gated one. */
static void cswap_whose_control_aliases_a_rail_it_writes(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = wire(ctx);
    const int32_t b = wire(ctx);

    CQ_DEATH_REQUIRE(cq_reg_owned_qubits(&ctx->regs, a) == 1u);
    CQ_EXPECT_ABORT(cqrt_cswap(a, a, b));
}

/* THE OTHER HALF OF THE SAME REFUSAL. `ctrl == a` had a case and `ctrl == b` did
 * not, and a battery measured `ctrl == a || ctrl == b` narrowed to `ctrl == a`
 * as surviving the whole suite. The two are the same non-injectivity — cswap
 * writes BOTH data rails — so both halves need a case or half the guard is
 * decoration. */
static void cswap_whose_control_aliases_its_second_rail(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = wire(ctx);
    const int32_t b = wire(ctx);

    CQ_DEATH_REQUIRE(a != b);
    CQ_EXPECT_ABORT(cqrt_cswap(b, a, b));
}

/* WIDTH 8 AND NOT WIDTH 2, deliberately: the guard must be reached through
 * `cq_reg_cbits` FIRST so a freed flag is M07's use-after-free rather than our
 * width bug, and a narrow fixture cannot tell the two orders apart. */
static void cswap_with_a_control_flag_wider_than_one_bit(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = cqrt_alloc_i8(1);
    const int32_t a = cqrt_alloc_i32(3);
    const int32_t b = cqrt_alloc_i32(5);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, f) == 8u);
    CQ_EXPECT_ABORT(cqrt_cswap(f, a, b));
}

static void cswap_between_rails_of_different_widths(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = cqrt_alloc_i1(true);
    const int32_t a = cqrt_alloc_i32(3);
    const int32_t b = cqrt_alloc_i16(5);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, a) == 32u);
    CQ_EXPECT_ABORT(cqrt_cswap(f, a, b));
}

/* THE ZERO ROW MUST REFUSE TOO, and this is the case that says so. A guard
 * placed after the branch on the flag's kind would let a width-mismatched
 * `cqrt_cswap` under a CQ_BIT_ZERO flag return quietly, which is a guard that
 * is not there for a third of its inputs. */
static void cswap_validates_its_operands_even_on_the_zero_row(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = cqrt_alloc_i1(false);          /* row 0's SKIP */
    const int32_t a = cqrt_alloc_i32(3);
    const int32_t b = cqrt_alloc_i16(5);

    CQ_DEATH_REQUIRE(cq_bit_is_zero(cq_reg_cbits(&ctx->regs, f)[0]));
    CQ_EXPECT_ABORT(cqrt_cswap(f, a, b));
}

/* --- M07's refusals, reached through the shim ----------------------------- */

static void double_free_is_m07s_refusal(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i32(0);

    cqrt_free(h);
    CQ_DEATH_REQUIRE(!cq_reg_is_live(&ctx->regs, h));
    CQ_EXPECT_ABORT(cqrt_free(h));
}

static void free_of_a_measured_rail_is_m07s_refusal(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i32(0);

    (void)cqrt_measure_i32(h);
    CQ_DEATH_REQUIRE(cq_reg_state(&ctx->regs, h) == CQ_SLOT_MEASURED);
    CQ_EXPECT_ABORT(cqrt_free(h));
}

static void a_second_measure_is_m07s_refusal(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i32(0);

    (void)cqrt_measure_i32(h);
    CQ_DEATH_REQUIRE(cq_reg_state(&ctx->regs, h) == CQ_SLOT_MEASURED);
    CQ_EXPECT_ABORT((void)cqrt_measure_i32(h));
}

/* A TOMBSTONE MUST BE M07's DIAGNOSTIC AND NOT A WIDTH BUG, which is why every
 * resolve in the shim goes through `cq_reg_bits` / `cq_reg_cbits` BEFORE
 * comparing the width: `cq_reg_width` goes through `cq_reg_slot`, which admits
 * a tombstone on purpose so a use-after-free message can name its width. The
 * rail is 32 bits and the symbol is i32, so a shim that compared first would
 * find nothing wrong and write to freed memory. */
static void use_of_a_freed_rail_is_m07s_refusal(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i32(0);

    cqrt_free(h);
    CQ_DEATH_REQUIRE(cq_reg_state(&ctx->regs, h) == CQ_SLOT_DEAD);
    CQ_EXPECT_ABORT(cqrt_xorc_i32(h, 1));
}


/* --- rail_w's RESOLVE-BEFORE-COMPARE ORDER, and its ACCESSOR -------------- */

/* TWO PROPERTIES OF ONE THREE-LINE FUNCTION, AND EACH CASE IS THE SOLE DETECTOR
 * FOR ONE OF THEM. Both aborts below are M07's, so the discriminator is
 * NEGATIVE — `FATAL: shim:` must be ABSENT — which is the same shape as the
 * four M07 cases above and for a different reason: there the claim is who OWNS
 * the refusal, here it is that `rail_w` reached M07 at all.
 *
 * THE ORDER, AND THE MUTANT THAT NAMES IT IS NOT THE OBVIOUS ONE.
 * `cq_reg_width` goes through `cq_reg_slot`, which admits a tombstone ON
 * PURPOSE (src/reg.c: the width survives the tombstone so a use-after-free
 * diagnostic can name it), so comparing the width BEFORE resolving turns a
 * use-after-free into a width bug — which is what `rail_w`'s own comment
 * forbids.
 *
 * `bd pnu` proposed the defect as mutant rail-R03, SWAPPING the two
 * declarations, and measured it surviving the whole suite. It survives because
 * IT IS EQUIVALENT: the swap moves the width READ earlier and leaves the
 * COMPARISON where it was, so `cq_reg_bits` still runs before `cq_rail_die`
 * and the abort is M07's either way. Measured by hand 2026-08-27 — the mutated
 * build prints `FATAL: reg: write access to a rail that is not live` on the
 * fixture below, exactly as the clean one does. The observable defect is
 * rail-R03b, hoisting the COMPARISON above the resolve, and that is what this
 * case kills (measured, both configurations). Two things follow and both are
 * this project's recorded shapes: a claim about ORDER is a claim about which
 * statement crosses the ABORT, not about which line is written first; and a
 * mutant is a hypothesis about a defect, so a surviving one may be reporting
 * that the hypothesis was mis-stated rather than that the suite is blind.
 *
 * The file's only non-LIVE fixture before this one —
 * `use_of_a_freed_rail_is_m07s_refusal` — hands a freed i32 to
 * `cqrt_xorc_i32`, where the width MATCHES and every order falls through to the
 * same message, so it could not have seen rail-R03b either. The asymmetry was
 * the finding: shim/cq_runtime_gate.c makes the identical claim for
 * `gate_rot_width` and has had two cases for it since it landed.
 *
 * THE ACCESSOR. `cq_reg_bits` refuses a MEASURED rail and `cq_reg_cbits` admits
 * one, which is where measurement's terminality is enforced for a WRITE
 * (Rule 6) — and the READ half of that asymmetry is legal and is pinned as a
 * POSITIVE case in tests/test_runtime_rail_write.inc, because a death suite
 * cannot assert that something works. Measured the same day (mutants rail-R04
 * and rail-R05): BOTH directions of the swap survived everything.
 *
 * THE WIDTHS DIFFER BETWEEN THE TWO CASES AND THAT IS THE POINT, not a
 * copy-paste slip. The order case needs a WRONG width, because that is the only
 * shape where the two orders disagree. The accessor case needs a MATCHING one:
 * with the width wrong it would abort under the mutant too — on the shim's
 * width message — and would then be a second, weaker copy of the order case
 * instead of the sole detector for terminality. With the width right, the
 * mutant does not abort at all and the exit code alone convicts it. */
static void xorc_of_a_freed_rail_of_the_wrong_width_is_a_use_after_free(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i32(0);

    cqrt_free(h);                   /* I4: an all-constant rail owns no qubit */
    CQ_DEATH_REQUIRE(cq_reg_state(&ctx->regs, h) == CQ_SLOT_DEAD);
    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 32u);
    CQ_EXPECT_ABORT(cqrt_xorc_i8(h, 3));
}

static void xorc_of_a_measured_rail_is_a_write_refusal(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i32(0);

    (void)cqrt_measure_i32(h);
    CQ_DEATH_REQUIRE(cq_reg_state(&ctx->regs, h) == CQ_SLOT_MEASURED);
    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 32u);
    CQ_EXPECT_ABORT(cqrt_xorc_i32(h, 3));
}


/* AND `rail_r` HAS THE SAME ORDER CLAIM WITH ONLY ONE FIXTURE THAT CAN SEE IT.
 * Measured 2026-08-27 (mutant rail-R06, the R03b hoist applied to the READ
 * helper): it survived the whole suite. A MEASURED rail cannot convict it —
 * `cq_reg_cbits` ADMITS one, so both orders fall through to the shim's width
 * message and agree — which leaves the TOMBSTONE as the only shape where the
 * two orders disagree. That is the mirror image of `rail_w` above, which the
 * measured row can also see, and it is why this helper gets one case and that
 * one gets two. */
static void measure_of_a_freed_rail_of_the_wrong_width_is_a_use_after_free(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i32(0);

    cqrt_free(h);
    CQ_DEATH_REQUIRE(cq_reg_state(&ctx->regs, h) == CQ_SLOT_DEAD);
    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 32u);
    CQ_EXPECT_ABORT((void)cqrt_measure_i8(h));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(measure_of_a_rail_wider_than_its_symbol),
    CQ_DEATH_CASE(copy_between_rails_of_different_widths),
    CQ_DEATH_CASE(copy_of_two_rails_of_the_same_wrong_width),
    CQ_DEATH_CASE(addc_of_a_rail_wider_than_its_symbol),
    CQ_DEATH_CASE(xorc_of_a_rail_wider_than_its_symbol),
    CQ_DEATH_CASE(cswap_of_a_rail_with_itself),
    CQ_DEATH_CASE(cswap_whose_control_aliases_a_rail_it_writes),
    CQ_DEATH_CASE(cswap_whose_control_aliases_its_second_rail),
    CQ_DEATH_CASE(cswap_with_a_control_flag_wider_than_one_bit),
    CQ_DEATH_CASE(cswap_between_rails_of_different_widths),
    CQ_DEATH_CASE(cswap_validates_its_operands_even_on_the_zero_row),
    CQ_DEATH_CASE(double_free_is_m07s_refusal),
    CQ_DEATH_CASE(free_of_a_measured_rail_is_m07s_refusal),
    CQ_DEATH_CASE(a_second_measure_is_m07s_refusal),
    CQ_DEATH_CASE(use_of_a_freed_rail_is_m07s_refusal),
    CQ_DEATH_CASE(xorc_of_a_freed_rail_of_the_wrong_width_is_a_use_after_free),
    CQ_DEATH_CASE(xorc_of_a_measured_rail_is_a_write_refusal),
    CQ_DEATH_CASE(measure_of_a_freed_rail_of_the_wrong_width_is_a_use_after_free)
)
