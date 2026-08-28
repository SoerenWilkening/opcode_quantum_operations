/* tests/test_runtime_gate_death_rotate.inc — the ROTATION half of the gate
 * surface's death suite, split on the seam shim/cq_runtime_gate.c records for
 * itself and IMPLEMENTATION_PLAN §3 now records for this file:
 * `the DISCRETE gate primitives <-> the ROTATION families`.
 *
 * TAKEN AT THE HOUSE 240 TRIGGER RATHER THAN AT THE 300 WALL, and recorded in
 * the plan BEFORE the five `bd pnu` cases were written — the `.c` stood at 233
 * with no seam recorded anywhere, which is a split waiting to be improvised
 * (Rule 12). The ordinary suite took the same cut at the wall
 * (`tests/test_runtime_gate_rotate.inc`, 306 of 300); the source module took it
 * as a reserve. Three files wanting one cut is the argument for the cut.
 *
 * ON THIS SIDE THE DISCRIMINATOR IS SHARPER THAN ON THE SOURCE SIDE, because a
 * death case's subject IS which layer refuses. Everything in here refuses
 * through `gate_rot_width` or through M22 — D11's two refusals, and the
 * tombstone/measured ORDER pair that is the only thing `gate_rot_width`'s
 * `(void)cq_reg_bits` buys. Everything left behind refuses through
 * `gate_target` / `gate_ctrl` / `gate_distinct`: M05's and M06's surface, where
 * the two-bit shadow is exact and where PRD §9 row 0 is the row on which the
 * shim is the SOLE detector.
 *
 * Included by tests/test_runtime_gate_death.c and inheriting its includes, and
 * ABOVE its CQ_DEATH_MAIN — the registry stays in the `.c`, so the include order
 * is load-bearing. It is out of check_loc.sh's reach because that script's
 * `find` matches `*.c`, `*.h` and `*.py` only — an EXTENSION rule, not a
 * translation-unit rule, which is what makes moving a subject into an .inc a
 * real escape hatch rather than a rename that dodges the guard.
 */

/* --- The width token is a claim about the rail ---------------------------- */

static void rz_of_a_rail_wider_than_its_symbol(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 32u);
    CQ_EXPECT_ABORT(cqrt_rz_i8(h, 0.4));
}

/* ONE WIDTH CASE PER ROTATION FAMILY. `gate_rot_width` has five call sites and
 * only the `cqrt_rz_i<W>` one had a case; a battery measured the other four as
 * deletable with nothing going red. They are separate macro expansions, so a
 * dropped call in any one of them is its own defect. */
static void ry_of_a_rail_wider_than_its_symbol(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 32u);
    CQ_EXPECT_ABORT(cqrt_ry_i8(h, 0.4));
}

static void rz_controlled_of_a_rail_wider_than_its_symbol(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = cqrt_alloc_i1(false);
    const int32_t h = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 32u);
    CQ_EXPECT_ABORT(cqrt_rz_i8_controlled(f, h, 0.4));
}

static void rz_controlled_inv_of_a_rail_wider_than_its_symbol(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = cqrt_alloc_i1(false);
    const int32_t h = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 32u);
    CQ_EXPECT_ABORT(cqrt_rz_i8_controlled_inv(f, h, 0.4));
}

static void ry_controlled_inv_of_a_rail_wider_than_its_symbol(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = cqrt_alloc_i1(false);
    const int32_t h = cqrt_alloc_i32(0);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 32u);
    CQ_EXPECT_ABORT(cqrt_ry_i8_controlled_inv(f, h, 0.4));
}

/* --- gate_rot_width's ORDER, which is the only thing its cq_reg_bits buys -- */

/* M22 MAKES THE SAME CALL ONE LAYER DOWN FOR THE SAME REASON (src/rotate.c), so
 * the REFUSAL is not this line's — a duplicate would be the masking-layer trap
 * this project has recorded four times. What IS this line's is that the refusal
 * arrives BEFORE `cq_reg_width` is asked, and `cq_reg_width` goes through
 * `cq_reg_slot`, which admits a tombstone ON PURPOSE (src/reg.c says why: the
 * width survives the tombstone so a use-after-free diagnostic can name it).
 *
 * THE FIXTURE HAS TO BE A TOMBSTONE OF THE WRONG WIDTH, and the analogue is
 * already recorded for `cq_shim_region`, whose width guard needs a WIDTH-8
 * tombstone because a width-1 one passes either order. Measured the same way
 * here: a freed i8 rail handed to `cqrt_ry_i8` prints the identical message
 * with the line and without it, because the width comparison passes and M22's
 * own resolve catches it. With the WIDTH wrong the two orders disagree —
 * present gives M07's `write access to a rail that is not live`, deleted gives
 * the SHIM's width message for a rail that was freed. So the discriminator is
 * NEGATIVE and lives in tests/CMakeLists.txt: both orders still abort, so the
 * exit code alone cannot see it. Measured: deleting the line leaves all 256
 * tests green in both configurations without these two cases. */
static void ry_of_a_freed_rail_of_the_wrong_width_is_a_use_after_free(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i32(0);

    cqrt_free(h);                   /* I4: an all-constant rail owns no qubit */
    CQ_DEATH_REQUIRE(cq_reg_state(&ctx->regs, h) == CQ_SLOT_DEAD);
    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 32u);
    CQ_EXPECT_ABORT(cqrt_ry_i8(h, 0.4));
}

/* MEASUREMENT IS TERMINAL (PRD §7) AND OUTRANKS THE WIDTH TOKEN TOO, on the
 * same order and by the same line — and this is NOT a second copy of the case
 * above. `cq_reg_bits` refuses a MEASURED rail as well as a tombstone, which is
 * where terminality is enforced for a WRITE, and a rotation is a write;
 * `cq_reg_cbits` admits one, because a read is not. Measured: softening the
 * line to `cq_reg_cbits` leaves the tombstone case GREEN — a tombstone dies
 * either way and only the message changes — and turns THIS one red, so each of
 * the two is the sole detector for one of the two properties. */
static void ry_of_a_measured_rail_of_the_wrong_width_is_a_write_refusal(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i32(0);

    (void)cqrt_measure_i32(h);
    CQ_DEATH_REQUIRE(cq_reg_state(&ctx->regs, h) == CQ_SLOT_MEASURED);
    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 32u);
    CQ_EXPECT_ABORT(cqrt_ry_i8(h, 0.4));
}

/* --- D11, which is M06's refusal and must stay M06's ---------------------- */

/* A CONSTANT LANE UNDER A QUANTUM CONTROL. By I4 a freshly allocated rail owns
 * zero qubits, so this is the ordinary case rather than a corner: every
 * `cqrt_rz_i8_controlled` on a rail CQ_lang just allocated would land here. */
static void rz_controlled_on_a_constant_lane_is_m06s_refusal(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = cqrt_alloc_i1(false);
    const int32_t h = cqrt_alloc_i8(0);

    /* Make the flag a WIRE: only CQ_BIT_Q promotes, and D11 is about promotion.
     * cq_reg_bits is the mutable accessor, which is what a test may use to
     * build a fixture the ABI has no way to spell. */
    cq_reg_bits(&ctx->regs, f)[0] = cq_bit_qubit(cq_ctx_fresh_qubit(ctx));

    CQ_DEATH_REQUIRE(cq_reg_owned_qubits(&ctx->regs, h) == 0u);
    CQ_EXPECT_ABORT(cqrt_rz_i8_controlled(f, h, 0.4));
}

/* THE Ry SIDE REFUSES ON BOTH COLUMNS, so it aborts having emitted nothing at
 * all — the difference from the Rz row above, and the reason the ordinary
 * suite's characterisation is about Rz. `pi` is CQ_ANGLE_HALF_TURN, one of the
 * three Ry rows D11 refuses. */
static void ry_controlled_inv_at_a_half_turn_is_m06s_refusal(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t f = cqrt_alloc_i1(false);
    const int32_t h = cqrt_alloc_i8(0);

    cq_reg_bits(&ctx->regs, f)[0] = cq_bit_qubit(cq_ctx_fresh_qubit(ctx));
    cq_reg_bits(&ctx->regs, h)[0] = cq_bit_qubit(cq_ctx_fresh_qubit(ctx));

    CQ_DEATH_REQUIRE(cq_reg_owned_qubits(&ctx->regs, h) == 1u);
    CQ_EXPECT_ABORT(cqrt_ry_i8_controlled_inv(f, h, 3.14159265358979323846));
}

/* THE OTHER DIRECTION OF THE SAME COMPARISON, AND IT WAS THE HOLE `gate-G12`
 * FOUND. Every one of the five cases above hands a rotation a rail WIDER than
 * its token (w = 32, want = 8), so `if (w != want)` narrowed to `if (w > want)`
 * survived the whole 258-test suite in both configurations — measured
 * 2026-08-27. It is a real defect and not a tidy: `cqrt_rz_i8` on an i1 rail
 * rotates one lane and silently drops seven, which is the same class the
 * one-bit guard's own header paragraph refuses for `cqrt_x`.
 *
 * ONE CASE COVERS ALL FIVE FAMILIES HERE, unlike the width cases above, and the
 * asymmetry is not an oversight: those five pin five distinct CALL SITES (five
 * macro expansions, so a dropped call in any one is its own defect), while this
 * pins the DIRECTION of the single comparison inside `gate_rot_width`, of which
 * there is exactly one. */
static void rz_of_a_rail_narrower_than_its_symbol(void)
{
    cq_ctx *ctx = open_shim();
    const int32_t h = cqrt_alloc_i1(false);

    CQ_DEATH_REQUIRE(cq_reg_width(&ctx->regs, h) == 1u);
    CQ_EXPECT_ABORT(cqrt_rz_i8(h, 0.4));
}
