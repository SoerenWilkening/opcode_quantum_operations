/* Step 23, landing 1, step 4: M26's gate surface — shim/cq_runtime_gate.c, the
 * 30 `cqrt_*` symbols that emit a gate onto rails that already exist.
 *
 * TWO SUBJECTS, AND THE SPLIT BETWEEN THEM IS THIS FILE'S RECORDED SEAM
 * (IMPLEMENTATION_PLAN §3): `the DISCRETE gate primitives <-> the ROTATION
 * families`. Five discrete symbols — x / cnot / toffoli / x_controlled /
 * cnot_controlled — carry NO width token, act on one-bit flag rails, and reach
 * M05's X/CX/CCX surface where the two-bit shadow is EXACT. Twenty-five
 * rotation symbols are `_i{1,8,16,32,64}`, carry a `double angle`, walk every
 * bit of the rail, and reach M22 — PRD §7's twelve cells, D11's refusal, D12's
 * poison. The rotation half is the only part of the whole shim that can make a
 * rail unfreeable, and the only part that can hard-error for a reason the
 * discrete half has no vocabulary for.
 *
 * THE ONE-BIT GUARD IS THIS FILE'S OWN AND IS NOT IN ANY DOCUMENT. The ABI
 * declares `void cqrt_x(int32_t q)` with no width token and no width parameter,
 * and CQ_lang's own header calls the operand handle-typed — so `cqrt_x` on a
 * 32-bit rail is UNSPECIFIED, not merely unusual. Measured over the 247 goldens
 * on 2026-08-23: 4,922 `cqrt_x`, 29,260 `cqrt_cnot` and 53,332 `cqrt_toffoli`
 * calls, and every one of the 220,760-odd handle operand slots resolves to a
 * WIDTH-1 rail — 100%, zero exceptions. Refusing is therefore free today and
 * the alternative is silently picking bit 0, which is a miscompile on a wider
 * rail. Read the corpus figures as a ratio and a date: nothing in this
 * repository pins that corpus, and it moved twice on 2026-08-23 alone.
 *
 * THE PARTIAL-EMISSION CHARACTERISATION IS THE SHARPEST CASE HERE and it needed
 * a fork. `cq_rotate_rz` walks lanes in order and D11's refusal fires on the
 * CONSTANT column only, so on a mixed rail under a quantum control real gates
 * reach the sink BEFORE the abort. Nothing in the tree could see that: all five
 * existing D11 rotation death cases use uniform rails, where the refusal lands
 * at lane 0 having emitted nothing. Reading a dying process's trace takes
 * another process.
 */

#include "cq_runtime_abi.h"
#include "cq_shim_ctx.h"

#include "angle.h"
#include "bit.h"
#include "controlled.h"
#include "ctx.h"
#include "reg.h"
#include "rotate.h"
#include "sink.h"
#include "sink_printf.h"

#include "cqops/cqops.h"

#include "support/bitkinds.h"
#include "support/harness.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void fresh(void)
{
    cq_sink_reset();
    unsetenv("CQOPS_SINK");
    cq_shim_ctx_reset();
}

static cq_ctx *open_with(cq_mock *m, cq_sink *s)
{
    cq_mock_init(m);
    *s = cq_mock_sink(m);
    fresh();
    cqops_set_sink(s);
    return cq_shim_ctx();
}

static void close_with(cq_mock *m)
{
    cq_shim_ctx_reset();
    cqops_set_sink(NULL);
    cq_mock_dispose(m);
}

/* A one-bit rail of the given kind, which is the only shape these five symbols
 * accept: (0,0) ZERO, (1,0) ONE, (0,1) Q holding 0, (1,1) Q holding 1. */
static int32_t wire(cq_ctx *ctx, uint64_t value, uint64_t q)
{
    return cq_bk_reg(ctx, 1u, value, q);
}

/* -------------------------------------------------------------------------
 * 1. The five discrete primitives.
 * ------------------------------------------------------------------------- */

/* THE OPERAND IS A HANDLE, NOT A WIRE, and that is what makes this file
 * possible at all: if `cqrt_x(int32_t q)` named a raw qubit index libcqops
 * could not serve it, because a bare index has no bit KIND and the §3 fold
 * table dispatches on nothing else. CQ_lang's header settles it (the operand is
 * handle-typed and its trace prints h<N>), so each of the three is exactly the
 * fold table's own row on the rail's single bit — including the row that costs
 * nothing, which is the half a quantum-only fixture would miss. */
CQ_TEST(the_discrete_primitives_are_the_fold_table_on_a_one_bit_rail)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    /* X on a CONSTANT rail: zero gates, zero qubits, the constant flips. */
    const int32_t c = wire(ctx, 0u, 0u);
    const cq_pc_snap before = cq_pc_take(ctx);
    cqrt_x(c);
    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    CHECK_EQ(cq_pc_value(ctx, c), 1);

    /* X on a wire: one gate. */
    const int32_t q = wire(ctx, 0u, 1u);
    cq_mock_reset(&m);
    cqrt_x(q);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_X), 1);
    CHECK_EQ(cq_pc_value(ctx, q), 1);

    /* CNOT with a ZERO control is nothing; with a quantum control it is a CX. */
    const int32_t t  = wire(ctx, 0u, 1u);
    const int32_t z  = wire(ctx, 0u, 0u);
    cq_mock_reset(&m);
    cqrt_cnot(z, t);
    CHECK_EQ(cq_mock_count(&m), 0);
    cqrt_cnot(q, t);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX), 1);
    CHECK_EQ(cq_pc_value(ctx, t), 1);

    /* TOFFOLI, PINNED BY ITS OPERANDS AND NOT BY ITS KIND — because the natural
     * fixture is the one value that hides a dropped control. With both controls
     * quantum and both holding 1, `1 AND 1` equals `1 AND 1`, so a Toffoli that
     * silently used `c2` twice keeps the count at one CCX and the value at 1,
     * and M05's distinctness assert that would catch it is DEBUG-GATED. Measured:
     * a `cqrt_toffoli` that drops `c1` from the emitted gate passes the entire
     * Release suite. The ordered stream is what sees it. */
    const int32_t a  = wire(ctx, 1u, 1u);
    const int32_t b  = wire(ctx, 1u, 1u);
    const int32_t tt = wire(ctx, 0u, 1u);
    const uint32_t qa = cq_bit_qindex(cq_reg_cbits(&ctx->regs, a)[0]);
    const uint32_t qb = cq_bit_qindex(cq_reg_cbits(&ctx->regs, b)[0]);
    const uint32_t qt = cq_bit_qindex(cq_reg_cbits(&ctx->regs, tt)[0]);
    cq_mock_reset(&m);
    cqrt_toffoli(a, b, tt);
    {
        const cq_rec want[] = { CQ_REC_CCX(qa, qb, qt) };
        CHECK(cq_mock_matches(&m, want, 1u));
    }
    CHECK_EQ(cq_pc_value(ctx, tt), 1);

    /* L2 AS A SET AND THE I2 SWEEP, which this whole FILE lacked. Neither is
     * implied by the counts above: a primitive that acquired a wire and handed
     * back one of its operands' indices moves no count, and `cq_reg_audit` is
     * the only thing in the tree that sees one index owned twice within a
     * register. The audit is Debug-only, so the set check is the Release half.
     * Every rail this case made is still live — nothing here frees. */
    CHECK(cq_pc_live_is_exactly(ctx,
              (const int32_t[]){ c, q, t, z, a, b, tt }, 7u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* `cqrt_x_controlled` IS A CX AND `cqrt_cnot_controlled` IS A CCX, and that is
 * the whole of why PRD §15 D16's capability rule implements them while leaving
 * `cqrt_h` undefined: no ir-pass site can construct any of the three names, but
 * libcqops CAN serve two of them correctly and cannot serve the third at any
 * point in v1, because §8's vtable is frozen at six entries with no `h`.
 *
 * They go through the ONE region bracket rather than emitting a CX directly,
 * which is Rule 9: the controlled axis is an emitter MODE. The assertion is on
 * the emitted KIND, because that is what a hand-rolled `cq_emit_cx` here would
 * get right by accident and a promoted Toffoli would not. */
CQ_TEST(x_controlled_is_a_cx_and_cnot_controlled_is_a_ccx)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t flag = wire(ctx, 1u, 1u);
    const int32_t q    = wire(ctx, 0u, 1u);
    cq_mock_reset(&m);

    cqrt_x_controlled(flag, q);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX), 1);
    CHECK_EQ(cq_mock_count(&m), 1);
    CHECK_EQ(cq_pc_value(ctx, q), 1);
    CHECK_EQ(cq_ctrl_depth(ctx), 0);

    const int32_t inner = wire(ctx, 1u, 1u);
    const int32_t t     = wire(ctx, 0u, 1u);
    cq_mock_reset(&m);

    cqrt_cnot_controlled(flag, inner, t);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CCX), 1);
    CHECK_EQ(cq_mock_count(&m), 1);
    CHECK_EQ(cq_pc_value(ctx, t), 1);
    CHECK_EQ(cq_ctrl_depth(ctx), 0);

    /* The region opened and closed, so M06's shared ancilla must be back: a
     * bracket that kept one would leave a live index no register owns, which is
     * the set check's own sentence and is invisible to every count above. */
    CHECK(cq_pc_live_is_exactly(ctx,
              (const int32_t[]){ flag, q, inner, t }, 4u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* PRD §9 ROW 0 OVER EVERY CONTROLLED ENTRY POINT THIS FILE OWNS. A classical
 * control is a DECISION, not a circuit: ZERO skips the region — 0 gates AND
 * 0 qubits — and ONE emits it verbatim. The `ONE` half is asserted against the
 * UNCONTROLLED symbol rather than against a count, because "verbatim" is a
 * claim about a stream; the `ZERO` half asserts the pool as well, because a
 * bracket that leaked one qubit per call would otherwise survive every case. */
CQ_TEST(row_0_holds_for_every_controlled_entry_point_in_this_file)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t off = wire(ctx, 0u, 0u);
    const int32_t on  = wire(ctx, 1u, 0u);
    const int32_t q   = wire(ctx, 0u, 1u);
    const int32_t r   = wire(ctx, 1u, 1u);
    const int32_t h   = cq_bk_reg(ctx, 8u, 0x5u, 0xFu);

    cq_mock_reset(&m);
    const cq_pc_snap before = cq_pc_take(ctx);

    cqrt_x_controlled(off, q);
    cqrt_cnot_controlled(off, r, q);
    cqrt_rz_i8_controlled(off, h, 0.4);
    cqrt_rz_i8_controlled_inv(off, h, 0.4);
    cqrt_ry_i8_controlled_inv(off, h, 0.4);

    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    CHECK_EQ(cq_ctrl_depth(ctx), 0);

    /* ONE: VERBATIM, AND THAT IS A CLAIM ABOUT A STREAM. A first draft asserted
     * counts here and said in its own comment that it was doing something
     * else — an `rz` ONE-row emitting the wrong angle on the right lanes would
     * have passed. Each controlled call is now captured and compared, gate for
     * gate and angle for angle, against the UNCONTROLLED symbol run on the same
     * operands; angles compare bitwise, which is what makes "verbatim" testable
     * at all. */
    cq_rec want[16];
    size_t n;

    cq_mock_reset(&m);
    cqrt_x(q);
    n = cq_mock_count(&m);
    CHECK_EQ(n, 1);
    for (size_t i = 0; i < n && i < 16u; i++) want[i] = *cq_mock_at(&m, i);
    cq_mock_reset(&m);
    cqrt_x_controlled(on, q);
    CHECK(cq_mock_matches(&m, want, n));

    cq_mock_reset(&m);
    cqrt_cnot(r, q);
    n = cq_mock_count(&m);
    CHECK_EQ(n, 1);
    for (size_t i = 0; i < n && i < 16u; i++) want[i] = *cq_mock_at(&m, i);
    cq_mock_reset(&m);
    cqrt_cnot_controlled(on, r, q);
    CHECK(cq_mock_matches(&m, want, n));

    cq_mock_reset(&m);
    cqrt_rz_i8(h, 0.4);
    n = cq_mock_count(&m);
    CHECK_EQ(n, 4);                                   /* one per WIRE */
    for (size_t i = 0; i < n && i < 16u; i++) want[i] = *cq_mock_at(&m, i);
    cq_mock_reset(&m);
    cqrt_rz_i8_controlled(on, h, 0.4);
    CHECK(cq_mock_matches(&m, want, n));
    CHECK_EQ(cq_ctrl_depth(ctx), 0);

    /* AND THE ZERO ROW'S "0 QUBITS" HALF AS A SET RATHER THAN AS A COUNT. Five
     * regions were opened and closed above; a bracket that leaked M06's shared
     * ancilla once per call leaves five live indices no register owns, and
     * `cq_pc_same` would still be green if it had also released five of theirs. */
    CHECK(cq_pc_live_is_exactly(ctx,
              (const int32_t[]){ off, on, q, r, h }, 5u));
    cq_reg_audit(ctx);

    close_with(&m);
}


/* A MEASURED RAIL IS A LEGAL GATE CONTROL, AND THIS IS THE ONLY THING IN THE
 * TREE THAT SAYS SO. `gate_ctrl` resolves through `cq_reg_cbits` and
 * `gate_target` through `cq_reg_bits`, and the asymmetry is the file's own
 * stated design point: A CONTROL IS A READ and a TARGET IS A WRITE, so
 * measurement's terminality (Rule 6) forecloses the second and not the first.
 * Refusing a measured control would break a shape CQ_lang legitimately
 * produces — measurement ends a rail's LIFETIME as a target, and CQ_lang emits
 * no `cqrt_free` for it, but the wires are still wires and still carry the
 * value the `mz` reported.
 *
 * IT HAD TO BE A POSITIVE CASE. Measured 2026-08-27 (`bd pnu`, mutant
 * gate-G14): hardening `gate_ctrl` to `cq_reg_bits` survived the whole 258-test
 * suite in both configurations, because no case anywhere used a measured rail
 * as a gate operand at all — and no DEATH case can convict that mutant, since
 * what it breaks is something working. The WRITE half is the death case
 * `test_runtime_gate_death.x_of_a_measured_rail_is_a_write_refusal`; neither
 * direction is pinned without the other.
 *
 * THE OPERANDS ARE PINNED AND NOT ONLY THE COUNT. A gate emitted off the wrong
 * index is a count of one exactly as the right one is (CLAUDE.md's recorded
 * Step 19 survivor: an assertion that counts is not an assertion that
 * identifies). */
CQ_TEST(a_measured_rail_is_a_legal_control_and_an_ordinary_one_as_a_target)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t c = wire(ctx, 1u, 1u);          /* a wire holding 1 */
    const int32_t t = wire(ctx, 0u, 1u);
    uint32_t ci = 0u, ti = 0u;

    CHECK_EQ(cq_pc_indices(ctx, c, &ci, 1u), 1);
    CHECK_EQ(cq_pc_indices(ctx, t, &ti, 1u), 1);
    CHECK(ci != ti);

    CHECK_EQ((int)cqrt_measure_i1(c), 1);
    CHECK_EQ(cq_reg_state(&ctx->regs, c), CQ_SLOT_MEASURED);

    /* The gate still reaches the sink, off the measured rail's own wire. */
    cq_mock_reset(&m);
    cqrt_cnot(c, t);

    const cq_rec want[] = { CQ_REC_CX(ci, ti) };
    CHECK(cq_mock_matches(&m, want, 1u));
    CHECK_EQ(cq_pc_value(ctx, t), 1);

    /* And the same rail as a TARGET is refused — asserted here as the SHAPE the
     * death case drives, so the two halves are legible in one place. */
    CHECK_EQ(cq_reg_state(&ctx->regs, t), CQ_SLOT_LIVE);

    close_with(&m);
}

#include "test_runtime_gate_rotate.inc"

CQ_TEST_MAIN(
    CQ_CASE(the_discrete_primitives_are_the_fold_table_on_a_one_bit_rail),
    CQ_CASE(x_controlled_is_a_cx_and_cnot_controlled_is_a_ccx),
    CQ_CASE(row_0_holds_for_every_controlled_entry_point_in_this_file),
    CQ_CASE(a_measured_rail_is_a_legal_control_and_an_ordinary_one_as_a_target),
    CQ_CASE(ry_and_rz_reach_every_bit_of_the_rail_and_choose_the_column_per_bit),
    CQ_CASE(rz_controlled_promotes_exactly_and_touches_no_shadow),
    CQ_CASE(a_controlled_rz_leaves_a_target_freeable_under_a_POISONED_flag),
    CQ_CASE(rz_controlled_inv_is_the_forward_call_at_a_negated_angle),
    CQ_CASE(ry_controlled_inv_is_the_rotation_at_a_negated_angle),
    CQ_CASE(ry_controlled_inv_is_ry_controlled_at_a_negated_angle),
    CQ_CASE(a_controlled_ry_taints_its_target_and_a_controlled_rz_does_not),
    CQ_CASE(the_d11_rz_refusal_emits_four_gates_per_wire_below_the_first_constant)
)
