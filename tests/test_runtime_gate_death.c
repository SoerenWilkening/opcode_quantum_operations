/* Step 23, landing 1, step 4: the gate surface's hard errors.
 *
 * THREE GROUPS, AND EACH ONE IS A DIFFERENT LAYER'S REFUSAL. The
 * FAIL_REGULAR_EXPRESSION lists in tests/CMakeLists.txt say which, by naming
 * the strings the OTHER refusals would have printed — the shim's five messages
 * share one prefix, so an exit code cannot tell them apart.
 *
 *   THE ONE-BIT GUARD is this file's own and has no document behind it: the ABI
 *   declares `void cqrt_x(int32_t q)` with no width token, so its meaning on a
 *   wider rail is UNSPECIFIED. Refusing is free — every operand of every
 *   `cqrt_x` / `cqrt_cnot` / `cqrt_toffoli` call in the corpus is a width-1
 *   rail — and the alternative is silently taking bit 0.
 *
 *   THE COINCIDENCE GUARD is refused in BOTH configurations because M05's §3
 *   distinctness check is Debug-gated: measured, in Release a coincident
 *   operand reaches the sink as `cx q0 q0`, a malformed gate with the right
 *   value and no diagnostic.
 *
 *   D11's REFUSAL is M06's, not ours, and these cases assert `FATAL: shim:` is
 *   ABSENT. That is the whole claim: the shim must not re-implement or
 *   pre-empt a refusal PRD §15 D11 places in the rotation module — a duplicate
 *   one layer up is the masking-layer trap this project has recorded four
 *   times, and it would make M22's own death cases untestable through here.
 *
 * `UndefinedBehaviorSanitizer` is in every negative list (bd u76): a sanitizer's
 * abort() satisfies CQ_EXPECT_ABORT, so without it a Debug case whose setup
 * acquired undefined behaviour would pass having verified nothing.
 */

#include "cq_runtime_abi.h"
#include "cq_shim_ctx.h"

#include "bit.h"
#include "ctx.h"
#include "reg.h"

#include "cqops/cqops.h"

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

/* A one-bit rail carrying a real WIRE, and the wire is the point.
 * `cqrt_alloc_i1(false)` gives an all-CONSTANT rail owning zero qubits (I4), and
 * `cq_bit_coincident` fires only on two CQ_BIT_Q bits — so a constant fixture
 * makes M05's Debug-gated distinctness assert UNREACHABLE, which means the
 * `distinctness` entry in these cases' FAIL_REGULAR_EXPRESSION would be dead
 * text discriminating against nothing, and the Release-only hazard the guards
 * exist for (`cx q0 q0` reaching the sink) would never be exercised. The ABI has
 * no way to spell a wire, so the test builds one the way the D11 cases below do. */
static int32_t one_bit(cq_ctx *ctx)
{
    const int32_t h = cqrt_alloc_i1(false);

    cq_reg_bits(&ctx->regs, h)[0] = cq_bit_qubit(cq_ctx_fresh_qubit(ctx));
    return h;
}

/* --- The one-bit guard ---------------------------------------------------- */

static void x_on_a_rail_wider_than_one_bit(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 32u);
    CQ_EXPECT_ABORT(cqrt_x(h));
}

static void cnot_on_a_rail_wider_than_one_bit(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t c = one_bit(ctx);
    const int32_t t = cqrt_alloc_i8(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, t) == 8u);
    CQ_EXPECT_ABORT(cqrt_cnot(c, t));
}

static void toffoli_on_a_rail_wider_than_one_bit(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = one_bit(ctx);
    const int32_t b = cqrt_alloc_i16(0);
    const int32_t t = one_bit(ctx);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, b) == 16u);
    CQ_EXPECT_ABORT(cqrt_toffoli(a, b, t));
}

/* THE CONTROLLED PAIR MUST REFUSE ON ROW 0 TOO, which is why the flag here is a
 * CQ_BIT_ZERO — and the reason is NOT the one a first draft of this comment
 * gave. `cq_shim_region` calls the body unconditionally; row 0 skips EMISSION,
 * one gate at a time, inside `cq_emit_*`. So the body's own resolve is what
 * refuses here, on all three rows, and that is exactly why the guard needed no
 * hoisting out of it. */
static void x_controlled_on_a_rail_wider_than_one_bit(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = cqrt_alloc_i1(false);          /* row 0's SKIP */
    const int32_t h = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_bit_is_zero(cq_reg_cbits(&ctx->regs, f)[0]));
    CQ_EXPECT_ABORT(cqrt_x_controlled(f, h));
}

static void cnot_controlled_on_a_rail_wider_than_one_bit(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = cqrt_alloc_i1(false);
    const int32_t c = one_bit(ctx);
    const int32_t t = cqrt_alloc_i64(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, t) == 64u);
    CQ_EXPECT_ABORT(cqrt_cnot_controlled(f, c, t));
}

/* --- Coincident operands -------------------------------------------------- */

static void cnot_whose_control_is_its_target(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = one_bit(ctx);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, h));
    CQ_EXPECT_ABORT(cqrt_cnot(h, h));
}

static void toffoli_with_two_equal_controls(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = one_bit(ctx);
    const int32_t t = one_bit(ctx);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, a) && a != t);
    CQ_EXPECT_ABORT(cqrt_toffoli(a, a, t));
}

/* THE OTHER TWO TOFFOLI PAIRS. `gate_distinct(c1, c2)` had a case; `(c1, tgt)`
 * and `(c2, tgt)` did not, and a battery measured both deletions as SURVIVING
 * the whole suite. A control that is also the target is not merely malformed —
 * it is the non-injective row PRD §9 row B names. */
static void toffoli_whose_first_control_is_its_target(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = one_bit(ctx);
    const int32_t b = one_bit(ctx);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, a) && a != b);
    CQ_EXPECT_ABORT(cqrt_toffoli(a, b, a));
}

static void toffoli_whose_second_control_is_its_target(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t a = one_bit(ctx);
    const int32_t b = one_bit(ctx);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, b) && a != b);
    CQ_EXPECT_ABORT(cqrt_toffoli(a, b, b));
}

/* ALL THREE OF cqrt_cnot_controlled's PAIRS, each measured as an untested
 * deletion. The middle one is PRD §9 row B's other half — a control coinciding
 * with an INNER control, which is `q & q = q` and well defined, and which v1
 * refuses anyway (§9 row B). */
static void cnot_controlled_whose_flag_is_its_inner_control(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = one_bit(ctx);
    const int32_t t = one_bit(ctx);

    CQ_DEATH_REQUIRE(f != t);
    CQ_EXPECT_ABORT(cqrt_cnot_controlled(f, f, t));
}

static void cnot_controlled_whose_flag_is_its_target(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = one_bit(ctx);
    const int32_t c = one_bit(ctx);

    CQ_DEATH_REQUIRE(f != c);
    CQ_EXPECT_ABORT(cqrt_cnot_controlled(f, c, f));
}

static void cnot_controlled_whose_inner_control_is_its_target(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = one_bit(ctx);
    const int32_t c = one_bit(ctx);

    CQ_DEATH_REQUIRE(f != c);
    CQ_EXPECT_ABORT(cqrt_cnot_controlled(f, c, c));
}

/* PRD §9 row B, one layer up: a control coinciding with an operand of the gate
 * it promotes. M06 refuses it too, but only once a gate is actually emitted —
 * and row 0's ZERO branch never emits one, so the shim's own refusal is what
 * makes this uniform across all three rows. */
static void x_controlled_whose_flag_is_its_target(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = one_bit(ctx);

    CQ_DEATH_REQUIRE(cq_reg_is_live(&ctx->regs, h));
    CQ_EXPECT_ABORT(cqrt_x_controlled(h, h));
}

/* ROW 0, WHERE THE SHIM'S COINCIDENCE GUARD IS THE ONLY DETECTOR THERE IS — and
 * these three exist because the Q-flag versions above STOPPED being detectors
 * without anything going red. Measured 2026-08-27 (`bd pnu`, mutants gate-G13,
 * gate-G15, gate-G16b): deleting any of the three `gate_distinct` calls that
 * name the FLAG survives the whole 258-test suite in both configurations.
 *
 * THE CAUSE IS A FIXTURE CHANGE, NOT A CODE CHANGE, which is this project's
 * masking-layer finding in a new spelling. `one_bit()` now mints a real
 * CQ_BIT_Q wire, so with the guard deleted the flag PROMOTES and M06's
 * check_one catches the coincidence one layer down, aborting with
 * `FATAL: controlled:` — a string none of the four discriminators in those
 * cases' FAIL_REGULAR_EXPRESSION named, so they kept exiting 0. An adversarial
 * review had measured gate-G13 as KILLED with no line of shim/ or src/ changed
 * since: a battery result is evidence about a TREE STATE, not about a line.
 *
 * TWO ARMS, AND THIS IS THE ONE WITH TEETH. tests/CMakeLists.txt now bans
 * `FATAL: controlled:` in the Q-flag cases' list, which makes M06 speaking a
 * failure there. These three take a CQ_BIT_ZERO flag, where M06 CANNOT fire at
 * all — `cq_shim_region` calls the body unconditionally, PRD §9 row 0 skips
 * EMISSION inside `cq_emit_*`, and a promotion that never happens has no wire
 * to compare — and M05 cannot either, because `cq_bit_coincident` fires only on
 * two CQ_BIT_Q bits and this fixture has none. So on row 0 the shim's guard is
 * not merely FIRST, it is ALONE: with it deleted nothing aborts at all. That is
 * the row the Q-flag cases' own comments claim to test and no longer do. */
static void x_controlled_whose_zero_flag_is_its_target(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i1(false);          /* row 0's SKIP */

    CQ_DEATH_REQUIRE(cq_bit_is_zero(cq_reg_cbits(&ctx->regs, h)[0]));
    CQ_EXPECT_ABORT(cqrt_x_controlled(h, h));
}

static void cnot_controlled_whose_zero_flag_is_its_inner_control(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = cqrt_alloc_i1(false);
    const int32_t t = cqrt_alloc_i1(false);

    CQ_DEATH_REQUIRE(f != t);
    CQ_DEATH_REQUIRE(cq_bit_is_zero(cq_reg_cbits(&ctx->regs, f)[0]));
    CQ_EXPECT_ABORT(cqrt_cnot_controlled(f, f, t));
}

static void cnot_controlled_whose_zero_flag_is_its_target(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = cqrt_alloc_i1(false);
    const int32_t c = cqrt_alloc_i1(false);

    CQ_DEATH_REQUIRE(f != c);
    CQ_DEATH_REQUIRE(cq_bit_is_zero(cq_reg_cbits(&ctx->regs, f)[0]));
    CQ_EXPECT_ABORT(cqrt_cnot_controlled(f, c, f));
}

/* --- Measurement is terminal for a WRITE and not for a READ --------------- */

/* THE WRITE HALF OF THE ACCESSOR ASYMMETRY, ON THE DISCRETE SURFACE.
 * `gate_target` resolves through `cq_reg_bits`, which refuses a MEASURED rail;
 * `gate_ctrl` resolves through `cq_reg_cbits`, which admits one because A
 * CONTROL IS A READ — and refusing a measured control would break a shape
 * CQ_lang legitimately produces, since measurement is terminal (Rule 6) but a
 * measured wire is still a wire. The READ half is a POSITIVE case and lives in
 * tests/test_runtime_gate.c; a death suite cannot assert that something works.
 *
 * Measured 2026-08-27 (`bd pnu`, mutant gate-G19): softening `gate_target` to
 * `cq_reg_cbits` survived the whole suite in both configurations, because
 * nothing anywhere handed a discrete gate primitive a measured rail at all.
 *
 * THE RAIL IS ONE BIT WIDE ON PURPOSE. A wider one aborts on the one-bit guard
 * first, and the case would pass having tested that instead — which is why the
 * negative list still bans `wider than one bit`. */
static void x_of_a_measured_rail_is_a_write_refusal(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = one_bit(ctx);

    (void)cqrt_measure_i1(h);
    CQ_DEATH_REQUIRE(cq_reg_state(&ctx->regs, h) == CQ_SLOT_MEASURED);
    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 1u);
    CQ_EXPECT_ABORT(cqrt_x(h));
}


/* --- gate_ctrl's and gate_target's RESOLVE-BEFORE-COMPARE ORDER ----------- */

/* THE SAME CLAIM AS `gate_rot_width`'s PAIR BELOW, ON THE OTHER TWO HELPERS,
 * AND IT WAS UNPINNED ON BOTH. This file's header says the two resolves "run
 * BEFORE the width comparison so a use-after-free is M07's diagnostic and not a
 * width bug", because `cq_reg_width` goes through `cq_reg_slot`, which admits a
 * tombstone ON PURPOSE. Measured 2026-08-27 (`bd pnu`, mutants gate-G20 and
 * gate-G21 — the comparison hoisted above the resolve in `gate_ctrl` and in
 * `gate_target`): both survived the whole 258-test suite in both
 * configurations. Every one-bit-guard case above uses a LIVE rail, where the
 * two orders agree, and every M07-lifetime case in the tree uses a rail whose
 * width is RIGHT, where they agree too. The defect needs BOTH at once.
 *
 * WHY `gate_rot_width` WAS THE ONE THAT ESCAPED THIS: its order is bought by a
 * discardable `(void)cq_reg_bits` line, so the mutant that names it is a
 * DELETION and its two cases have existed since the file landed. Here the
 * resolve's result is USED, so the only way to break the order is to move the
 * comparison — a different mutant shape for the same property, which is why
 * having the rotation pair was not evidence about these two. */
static void x_of_a_freed_rail_wider_than_one_bit_is_a_use_after_free(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i32(0);

    cqrt_free(h);                   /* I4: an all-constant rail owns no qubit */
    CQ_DEATH_REQUIRE(cq_reg_state(&ctx->regs, h) == CQ_SLOT_DEAD);
    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 32u);
    CQ_EXPECT_ABORT(cqrt_x(h));
}

/* THE CONTROL SIDE, AND THE TARGET IS RESOLVED FIRST AND MUST BE CLEAN — a
 * width-1 LIVE rail — or the case would abort in `gate_target` and say nothing
 * about `gate_ctrl`. */
static void cnot_whose_control_is_a_freed_wide_rail_is_a_use_after_free(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t c = cqrt_alloc_i8(0);
    const int32_t t = one_bit(ctx);

    cqrt_free(c);
    CQ_DEATH_REQUIRE(cq_reg_state(&ctx->regs, c) == CQ_SLOT_DEAD);
    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, c) == 8u);
    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, t) == 1u);
    CQ_EXPECT_ABORT(cqrt_cnot(c, t));
}

#include "test_runtime_gate_death_rotate.inc"

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(x_on_a_rail_wider_than_one_bit),
    CQ_DEATH_CASE(cnot_on_a_rail_wider_than_one_bit),
    CQ_DEATH_CASE(toffoli_on_a_rail_wider_than_one_bit),
    CQ_DEATH_CASE(x_controlled_on_a_rail_wider_than_one_bit),
    CQ_DEATH_CASE(cnot_controlled_on_a_rail_wider_than_one_bit),
    CQ_DEATH_CASE(rz_of_a_rail_wider_than_its_symbol),
    CQ_DEATH_CASE(cnot_whose_control_is_its_target),
    CQ_DEATH_CASE(toffoli_with_two_equal_controls),
    CQ_DEATH_CASE(x_controlled_whose_flag_is_its_target),
    CQ_DEATH_CASE(x_controlled_whose_zero_flag_is_its_target),
    CQ_DEATH_CASE(toffoli_whose_first_control_is_its_target),
    CQ_DEATH_CASE(toffoli_whose_second_control_is_its_target),
    CQ_DEATH_CASE(cnot_controlled_whose_flag_is_its_inner_control),
    CQ_DEATH_CASE(cnot_controlled_whose_flag_is_its_target),
    CQ_DEATH_CASE(cnot_controlled_whose_inner_control_is_its_target),
    CQ_DEATH_CASE(cnot_controlled_whose_zero_flag_is_its_inner_control),
    CQ_DEATH_CASE(cnot_controlled_whose_zero_flag_is_its_target),
    CQ_DEATH_CASE(x_of_a_measured_rail_is_a_write_refusal),
    CQ_DEATH_CASE(x_of_a_freed_rail_wider_than_one_bit_is_a_use_after_free),
    CQ_DEATH_CASE(cnot_whose_control_is_a_freed_wide_rail_is_a_use_after_free),
    CQ_DEATH_CASE(ry_of_a_rail_wider_than_its_symbol),
    CQ_DEATH_CASE(rz_of_a_rail_narrower_than_its_symbol),
    CQ_DEATH_CASE(rz_controlled_of_a_rail_wider_than_its_symbol),
    CQ_DEATH_CASE(rz_controlled_inv_of_a_rail_wider_than_its_symbol),
    CQ_DEATH_CASE(ry_controlled_inv_of_a_rail_wider_than_its_symbol),
    CQ_DEATH_CASE(ry_of_a_freed_rail_of_the_wrong_width_is_a_use_after_free),
    CQ_DEATH_CASE(ry_of_a_measured_rail_of_the_wrong_width_is_a_write_refusal),
    CQ_DEATH_CASE(rz_controlled_on_a_constant_lane_is_m06s_refusal),
    CQ_DEATH_CASE(ry_controlled_inv_at_a_half_turn_is_m06s_refusal)
)
