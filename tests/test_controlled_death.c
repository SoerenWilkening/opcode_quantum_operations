/* Step 20 — M06's hard errors. PRD §9 and PRD §15 D11.
 *
 * EVERY CASE HERE ABORTS IN BOTH CONFIGURATIONS, and none of them carries
 * CQ_DEATH_SKIP_WITHOUT_INVARIANTS. That is a claim, not a coincidence: M06's
 * guards are about what gets EMITTED, and a promotion that emits a malformed
 * Toffoli in Release while aborting in Debug would be the worst of both. The
 * only Debug-gated checks anywhere near this module are M05's §3 distinctness
 * asserts and the I6 extent, and neither can see a pushed control — src/emit.c's
 * check_distinct compares the operands of the gate as the KERNEL wrote them and
 * has never heard of a control wire.
 *
 * THAT ALSO MEANS THE DISCRIMINATOR MATTERS. Several of these cases could in
 * principle abort one layer down instead — the coincidence cases would reach
 * cq_shadow_ccx's own Debug distinctness assert if M06's guard were deleted, and
 * only in Debug — so tests/CMakeLists.txt pins the message for each, which is
 * the Step 6/7/8 "WHICH LAYER ABORTED" lesson applied to a new module.
 */

#include "angle.h"
#include "controlled.h"
#include "ctx.h"
#include "emit.h"
#include "reg.h"
#include "rotate.h"
#include "sandwich.h"
#include "scratch.h"
#include "support/death.h"

#include <stdint.h>

/* A DISCARDING sink, the twelfth copy of this stub set (bd cue). Most cases
 * emit real gates while building the wires they then misuse, so a sink that
 * treated emission as a failure would fail the setup rather than the assertion;
 * and passing NULL to cq_ctx_init resolves CQOPS_SINK, whose "no default sink
 * registered" is itself a hard error outside the armed window. */
static void nx(void *u, uint32_t q) { (void)u; (void)q; }
static void ncx(void *u, uint32_t c, uint32_t t) { (void)u; (void)c; (void)t; }
static void nccx(void *u, uint32_t a, uint32_t b, uint32_t t)
{ (void)u; (void)a; (void)b; (void)t; }
static void nry(void *u, uint32_t q, double th) { (void)u; (void)q; (void)th; }
static void nrz(void *u, uint32_t q, double ph) { (void)u; (void)q; (void)ph; }
static void nmz(void *u, uint32_t q) { (void)u; (void)q; }

static cq_sink g_sink;

static void open_ctx(cq_ctx *ctx)
{
    g_sink.x  = nx;  g_sink.cx = ncx; g_sink.ccx = nccx;
    g_sink.ry = nry; g_sink.rz = nrz; g_sink.mz  = nmz;
    g_sink.user = NULL;
    cq_ctx_init(ctx, &g_sink);
}

static cq_bit q_of(cq_ctx *ctx) { return cq_bit_qubit(cq_ctx_fresh_qubit(ctx)); }

/* Runs OUTSIDE every armed window, so an abort from setup cannot masquerade as
 * the death under test. It exercises the shapes that must NOT abort — a
 * balanced region, a promoted gate of each arity, and D11's refusals under a
 * CLASSICAL control, where §9 row 0 makes them inert — so a guard that fires on
 * good input is caught here rather than passing as a death somewhere else. */
static void preflight(cq_ctx *ctx)
{
    cq_bit t = q_of(ctx), c1 = q_of(ctx), c2 = q_of(ctx), w = q_of(ctx);

    cq_ctrl_push(ctx, &w);
    cq_emit_x(ctx, &t);
    cq_emit_cx(ctx, &c1, &t);
    cq_emit_ccx(ctx, &c1, &c2, &t);
    cq_ctrl_pop(ctx);

    /* D11 is a QUANTUM-control refusal and must be silent under row 0's two
     * classical rows — otherwise it would break Rule 15's zero-cost claim and
     * §11's L5 shapes, which §9 row 0 exists to preserve. */
    cq_bit one = cq_bit_one(), zero = cq_bit_zero();
    int32_t h = cq_reg_alloc_zero(&ctx->regs, 2u);

    cq_ctrl_push(ctx, &one);
    cq_rotate_ry(ctx, h, 2.0 * CQ_ANGLE_PI);
    cq_rotate_rz(ctx, h, 0.4);
    cq_ctrl_pop(ctx);

    cq_ctrl_push(ctx, &zero);
    cq_rotate_ry(ctx, h, CQ_ANGLE_PI);
    cq_ctrl_pop(ctx);
}

/* --- The region's own lifecycle. ------------------------------------------ */

static void pop_with_no_region_open(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    CQ_EXPECT_ABORT(cq_ctrl_pop(&ctx));
}

static void an_unbalanced_push_is_caught_at_dispose(void)
{
    /* The flag qubits of an unclosed region are still live, so without this the
     * defect surfaces as a leaked index in somebody else's L2 — one layer away
     * from its cause, and only if that somebody checks the pool as a SET. */
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit w = q_of(&ctx);
    cq_ctrl_push(&ctx, &w);

    CQ_EXPECT_ABORT(cq_ctx_dispose(&ctx));
}

/* One gate per step, and a step is replayed at the same index on the reverse
 * pass — so a push inside a compute half would emit the nested AND's Toffoli at
 * a point the replay never revisits. The stream would still be a palindrome. */
static void pushing_step(cq_ctx *ctx, void *env, int step)
{
    cq_bit *w = (cq_bit *)env;
    (void)step;
    cq_ctrl_push(ctx, w);
}

static void push_inside_a_sandwich_compute_half(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit w = q_of(&ctx);
    cq_scratch scr;
    cq_scratch_alloc(&scr, 2u);

    CQ_EXPECT_ABORT(cq_sandwich(&ctx, &scr, pushing_step, 1, NULL, 0, &w));
}

/* --- The coincidence refusal (PRD §9, the libcqops-side row). -------------- */

static void the_control_wire_is_the_target_of_an_x(void)
{
    /* `if (q) q ^= 1` sends |0> and |1> both to |0>. There is no correct
     * spelling, so this aborts rather than collapsing. */
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit w = q_of(&ctx);
    cq_bit t = w;                       /* the same index, a second cq_bit */
    cq_ctrl_push(&ctx, &w);

    CQ_EXPECT_ABORT(cq_emit_x(&ctx, &t));
}

static void the_control_wire_is_the_target_of_a_cx(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit c = q_of(&ctx);
    cq_bit w = q_of(&ctx);
    cq_bit t = w;
    cq_ctrl_push(&ctx, &w);

    CQ_EXPECT_ABORT(cq_emit_cx(&ctx, &c, &t));
}

static void the_control_wire_is_the_target_of_a_ccx(void)
{
    /* The worst of the three: gate 3 of the promoted block reads a control that
     * gate 2 has already flipped, so the SHARED ANCILLA is left dirty and
     * handed to cq_ctrl_pop, whose contract is that it is |0>. */
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit c1 = q_of(&ctx), c2 = q_of(&ctx);
    cq_bit w = q_of(&ctx);
    cq_bit t = w;
    cq_ctrl_push(&ctx, &w);

    CQ_EXPECT_ABORT(cq_emit_ccx(&ctx, &c1, &c2, &t));
}

static void the_control_wire_is_a_control_of_a_cx(void)
{
    /* WELL DEFINED AND REFUSED ANYWAY: `q & q == q`, so the right answer is the
     * gate one promotion level down. v1 declines because CQ_lang's 239 goldens
     * contain ZERO controlled calls whose control aliases another operand, so
     * the collapse would be untested behaviour sitting in the tree. */
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit t = q_of(&ctx);
    cq_bit w = q_of(&ctx);
    cq_bit c = w;
    cq_ctrl_push(&ctx, &w);

    CQ_EXPECT_ABORT(cq_emit_cx(&ctx, &c, &t));
}

static void the_control_wire_is_a_control_of_a_ccx(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit t = q_of(&ctx), c2 = q_of(&ctx);
    cq_bit w = q_of(&ctx);
    cq_bit c1 = w;
    cq_ctrl_push(&ctx, &w);

    CQ_EXPECT_ABORT(cq_emit_ccx(&ctx, &c1, &c2, &t));
}

static void a_nested_regions_and_input_is_the_target_of_a_gate(void)
{
    /* THE DEPTH-2 FORM OF THE TARGET-COINCIDENCE REFUSAL, and it is a different
     * qubit from the depth-1 form. At depth >= 2 the top frame's wire is a
     * freshly minted AND FLAG; the bits the region depends on are the two the
     * flag was computed FROM. A gate targeting one of those changes a control
     * of the push Toffoli, so the twin at pop no longer uncomputes the flag and
     * cq_ctrl_pop releases a qubit that is demonstrably not |0>.
     *
     * NOTHING ELSE COULD SEE IT. The flag belongs to no register, so no L2 set
     * check names it; its shadow is poisoned exactly when the control wire is,
     * so cq_shadow_retire is inert; and CQ_ZERO_BY_CTRL_UNCOMPUTE is a literal
     * 1, so the pool's own guard cannot fire. This is the case that makes the
     * refusal stack-wide rather than top-wire-wide. */
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit w1 = q_of(&ctx);
    cq_bit w2 = q_of(&ctx);
    cq_bit t  = w1;                     /* the OUTER control, by index */

    cq_ctrl_push(&ctx, &w1);
    cq_ctrl_push(&ctx, &w2);            /* mints the AND flag */

    CQ_EXPECT_ABORT(cq_emit_x(&ctx, &t));
}

static void a_nested_regions_inner_control_is_the_target_of_a_gate(void)
{
    /* The other input of the same AND, and it needs its own case: `and_a` is
     * the enclosing frame's wire and so is covered twice, while `and_b` — the
     * INNER pushed control — lives in no frame's `wire` at all. A guard that
     * walked each frame's wire and stopped there would miss exactly this. */
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit w1 = q_of(&ctx);
    cq_bit w2 = q_of(&ctx);
    cq_bit t  = w2;                     /* the INNER control, by index */

    cq_ctrl_push(&ctx, &w1);
    cq_ctrl_push(&ctx, &w2);

    CQ_EXPECT_ABORT(cq_emit_x(&ctx, &t));
}

static void an_enclosing_regions_wire_is_the_target_of_a_gate(void)
{
    /* And the refusal is STACK-WIDE rather than two-frames-wide: at depth 3 the
     * outermost wire is neither the top frame's wire nor either input of the
     * top frame's AND, and writing it still breaks the middle frame's
     * uncompute. */
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit w1 = q_of(&ctx);
    cq_bit w2 = q_of(&ctx);
    cq_bit w3 = q_of(&ctx);
    cq_bit t  = w1;

    cq_ctrl_push(&ctx, &w1);
    cq_ctrl_push(&ctx, &w2);
    cq_ctrl_push(&ctx, &w3);

    CQ_EXPECT_ABORT(cq_emit_x(&ctx, &t));
}

static void the_control_wire_is_the_target_of_a_rotation(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit w = q_of(&ctx);
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 1u);
    cq_bit *b = cq_reg_bits(&ctx.regs, h);
    *b = w;                             /* the rail IS the control wire */
    cq_ctrl_push(&ctx, &w);

    CQ_EXPECT_ABORT(cq_rotate_ry(&ctx, h, 0.4));
}

/* --- PRD §15 D11: v1 refuses every §7 FOLD row under a quantum control. ---
 *
 * Five cells fold, and all five are here. The four zero-gate ones are wrong by
 * exactly Rz(alpha) on the control wire; the fifth — the half turn's qubit
 * column — does emit, and realises the row only up to the +i of Rz(pi).X = Y.
 * §9's wording ("a §7 fold row") is broader than "the zero-gate rows" for
 * exactly that cell, and an M06 built against the narrower phrasing would let
 * through the one row that carried a sign error in D11's own first draft. */

static void ry_2pi_under_a_quantum_control(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit w = q_of(&ctx);
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 2u);
    cq_ctrl_push(&ctx, &w);

    CQ_EXPECT_ABORT(cq_rotate_ry(&ctx, h, 2.0 * CQ_ANGLE_PI));
}

static void ry_half_turn_on_a_constant_under_a_quantum_control(void)
{
    /* The row whose whole point is "flip the constant, 0 gates, 0 qubits". */
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit w = q_of(&ctx);
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 2u);
    cq_ctrl_push(&ctx, &w);

    CQ_EXPECT_ABORT(cq_rotate_ry(&ctx, h, CQ_ANGLE_PI));
}

static void ry_half_turn_on_a_qubit_under_a_quantum_control(void)
{
    /* THE CELL THAT IS NOT A ZERO-GATE ROW. It emits `x` then `rz(pi)` and is
     * still a fold: the pair is Y = +i.Ry(pi), so promoting it gate by gate
     * overshoots by +i and the control owes -pi/2 at k = 1, +pi/2 at k = 3. */
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit w = q_of(&ctx);
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 1u);
    cq_bit *b = cq_reg_bits(&ctx.regs, h);
    *b = q_of(&ctx);                    /* a QUBIT rail, not a constant one */
    cq_ctrl_push(&ctx, &w);

    CQ_EXPECT_ABORT(cq_rotate_ry(&ctx, h, CQ_ANGLE_PI));
}

static void ry_neg_half_turn_under_a_quantum_control(void)
{
    /* k = 3 rather than k = 1. Uncontrolled the two are one cell and M22 emits
     * the identical pair; D11 gives them DIFFERENT control-side phases, which
     * is why bd fna split the class before this step could name the row. */
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit w = q_of(&ctx);
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 2u);
    cq_ctrl_push(&ctx, &w);

    CQ_EXPECT_ABORT(cq_rotate_ry(&ctx, h, 3.0 * CQ_ANGLE_PI));
}

static void rz_on_a_constant_under_a_quantum_control(void)
{
    /* The cell that keeps a classical rail classical through any number of Rz
     * calls — and owes (2b-1).phi/2 on the control wire, the ONE row in D11's
     * table that is not sign-degenerate and therefore the only one that fixes
     * the convention for all five. */
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit w = q_of(&ctx);
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 2u);
    cq_ctrl_push(&ctx, &w);

    CQ_EXPECT_ABORT(cq_rotate_rz(&ctx, h, 0.4));
}

/* --- Measurement has no controlled form. ---------------------------------- */

static void a_measurement_inside_a_quantum_controlled_region(void)
{
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit w = q_of(&ctx);
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 2u);
    uint64_t lo, hi;
    cq_ctrl_push(&ctx, &w);

    CQ_EXPECT_ABORT(cq_measure(&ctx, h, &lo, &hi));
}

static void a_measurement_inside_a_skipped_region(void)
{
    /* Refused for a DIFFERENT reason from the case above, and it is worth
     * having both: "the measurement did not happen" is not something a function
     * that returns a value can express, so a skipped region cannot silently
     * hand back zeros as though it had measured. A ONE control is legal — row 0
     * makes that region the uncontrolled one verbatim — and preflight relies on
     * exactly that distinction not being collapsed. */
    cq_ctx ctx;
    open_ctx(&ctx);
    preflight(&ctx);

    cq_bit z = cq_bit_zero();
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 2u);
    uint64_t lo, hi;
    cq_ctrl_push(&ctx, &z);

    CQ_EXPECT_ABORT(cq_measure(&ctx, h, &lo, &hi));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(pop_with_no_region_open),
    CQ_DEATH_CASE(an_unbalanced_push_is_caught_at_dispose),
    CQ_DEATH_CASE(push_inside_a_sandwich_compute_half),
    CQ_DEATH_CASE(the_control_wire_is_the_target_of_an_x),
    CQ_DEATH_CASE(the_control_wire_is_the_target_of_a_cx),
    CQ_DEATH_CASE(the_control_wire_is_the_target_of_a_ccx),
    CQ_DEATH_CASE(the_control_wire_is_a_control_of_a_cx),
    CQ_DEATH_CASE(the_control_wire_is_a_control_of_a_ccx),
    CQ_DEATH_CASE(a_nested_regions_and_input_is_the_target_of_a_gate),
    CQ_DEATH_CASE(a_nested_regions_inner_control_is_the_target_of_a_gate),
    CQ_DEATH_CASE(an_enclosing_regions_wire_is_the_target_of_a_gate),
    CQ_DEATH_CASE(the_control_wire_is_the_target_of_a_rotation),
    CQ_DEATH_CASE(ry_2pi_under_a_quantum_control),
    CQ_DEATH_CASE(ry_half_turn_on_a_constant_under_a_quantum_control),
    CQ_DEATH_CASE(ry_half_turn_on_a_qubit_under_a_quantum_control),
    CQ_DEATH_CASE(ry_neg_half_turn_under_a_quantum_control),
    CQ_DEATH_CASE(rz_on_a_constant_under_a_quantum_control),
    CQ_DEATH_CASE(a_measurement_inside_a_quantum_controlled_region),
    CQ_DEATH_CASE(a_measurement_inside_a_skipped_region)
)
