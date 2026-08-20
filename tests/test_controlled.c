/* Step 20 — M06. PRD §9's controlled axis: row 0, the promotion, the shared
 * ancilla, the nested AND, and PRD §15 D11's refusals.
 *
 * THIS IS L0 FOR THE CONTROLLED AXIS, and it is built on test_emit_fold.c's
 * shape for the reason that suite states: the fold table is the only place
 * classical/quantum is decided, so it is swept exhaustively rather than
 * sampled. §9 adds one dimension to that table — the control's KIND — and this
 * file sweeps the product: 5 control kinds × (5 + 25 + 125) operand
 * combinations = 775 cases, each pinning the ordered gate stream, the qubits
 * allocated, the resulting bit kind AND the resulting shadow.
 *
 * THE THREE ROWS ARE ASSERTED THREE DIFFERENT WAYS, on purpose.
 *
 *   ZERO  differentially and absolutely: zero gates, zero qubits, and the
 *         target byte-identical to what it was. No oracle can get that wrong.
 *   ONE   differentially against THE SAME GATE RUN WITH NO REGION — which is
 *         what "emitted uncontrolled, verbatim" means, and is a stronger
 *         statement than any oracle, since the uncontrolled path is already
 *         pinned exhaustively by test_emit_fold.c's 155 cases.
 *   Q     against a second statement of §9 in test_controlled_oracle.inc, AND
 *         differentially on the VALUE: at shadow 1 the target must end where the
 *         uncontrolled gate would have left it, at shadow 0 where it started.
 *         That second half is Bennett's own contract for `controlled()` —
 *         `(ctrl, x, 0) → (ctrl, x, ctrl ? f(x) : 0)` — and it holds whatever
 *         shape the promotion takes.
 *
 * WHY Q0 AND Q1 AND QU ARE ALL SWEPT rather than one standing for all three:
 * §9 row 0 dispatches on the control's KIND and must never read its shadow (D6),
 * so a Q control whose shadow says 0 must still PROMOTE — folding it away would
 * make the emitted gate count depend on shadow precision and every L4 golden
 * fragile. Q0 is also the only fixture that can falsify `bd skh`: at ctrl
 * shadow 1 a promoted and an unpromoted materialisation AGREE, and the
 * disagreement is the single cell `b = 1, k = 0`.
 *
 * SPLIT SEAM, RECORDED BEFORE IT IS NEEDED (Rule 12). The seam is the
 * `--- The region ---` divider: the three exhaustive sweeps and their helpers
 * stay, and everything from `a_zero_control_costs_nothing_at_all` downward — the
 * region lifecycle, nesting, the rotations and the sandwich composition — moves
 * to test_controlled_region.inc. The oracle and the literal table are already
 * .inc files, which is why they are not the seam.
 */

#include "angle.h"
#include "controlled.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/add.h"
#include "reg.h"
#include "rotate.h"
#include "support/bitkinds.h"
#include "support/harness.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef enum { K_ZERO = 0, K_ONE, K_Q0, K_Q1, K_QU, N_KINDS } okind;

static const char *kind_name(okind k)
{
    static const char *n[N_KINDS] = { "ZERO", "ONE", "Q0", "Q1", "QU" };
    return n[k];
}

static int kind_is_q(okind k)    { return k >= K_Q0; }
static int kind_unknown(okind k) { return k == K_QU; }
static int kind_value(okind k)   { return k == K_ONE || k == K_Q1; }

typedef struct {
    cq_ctx  ctx;
    cq_mock mock;
    cq_sink sink;
} fixture;

static void fx_init(fixture *f)
{
    cq_mock_init(&f->mock);
    f->sink = cq_mock_sink(&f->mock);
    cq_ctx_init(&f->ctx, &f->sink);
}

static void fx_dispose(fixture *f)
{
    cq_ctx_dispose(&f->ctx);
    cq_mock_dispose(&f->mock);
}

/* Identical to test_emit_fold.c's, deliberately: a controlled case and its
 * uncontrolled reference must build their operands the same way, or the qubit
 * indices in the two streams are not comparable. */
static cq_bit fx_operand(fixture *f, okind k)
{
    if (k == K_ZERO) return cq_bit_zero();
    if (k == K_ONE)  return cq_bit_one();

    uint32_t q = cq_ctx_fresh_qubit(&f->ctx);
    cq_bit   b = cq_bit_qubit(q);

    if (k == K_Q1) cq_emit_x(&f->ctx, &b);
    if (k == K_QU) cq_shadow_rotate(&f->ctx.shadow, q);
    return b;
}

/* A bit's VALUE, whatever its kind: -1 when it is on a poisoned wire and there
 * is no answer. This is what the differential rows compare, NOT the kind —
 * under a quantum control a constant target legitimately becomes a wire holding
 * the same value, and Rule 14's ban on asserting kinds across an axis is the
 * same lesson one axis over. */
static int bit_value(const fixture *f, cq_bit b)
{
    if (!cq_bit_is_qubit(b)) return cq_bit_value(b);
    cq_shadow s = cq_shadow_get(&f->ctx.shadow, cq_bit_qindex(b));
    return s.unknown ? -1 : (int)s.value;
}

#include "test_controlled_oracle.inc"

/* ------------------------------------------------------------------------
 * Comparison, and the invariants that hold whatever §9 says.
 * ------------------------------------------------------------------------ */

static void compare(fixture *f, const pexpect *e, const cq_bit *t,
                    uint32_t minted_before, const char *what)
{
    /* HOW MANY QUBITS, EXACTLY — not "at most two". This is the per-case form
     * of §9's "one reusable ancilla": a promotion that took a second one, or
     * one that took none where the target had to be materialised, moves this
     * and moves nothing else. */
    uint32_t alloc = cq_qubits_minted(&f->ctx.pool) - minted_before;
    if (alloc != e->n_alloc)
        cq_h_fail(__FILE__, __LINE__, "%s: allocated %u qubits, want %u",
                  what, alloc, e->n_alloc);

    if (!cq_mock_matches(&f->mock, e->gate, e->n_gates)) {
        cq_h_fail(__FILE__, __LINE__, "%s: gate stream differs", what);
        cq_mock_dump(&f->mock, what);
        return;
    }

    if (cq_bit_is_qubit(*t) != e->is_q)
        cq_h_fail(__FILE__, __LINE__, "%s: target kind is %s, want %s", what,
                  cq_bit_is_qubit(*t) ? "Q" : "const", e->is_q ? "Q" : "const");

    if (e->is_q && cq_bit_is_qubit(*t)) {
        cq_shadow s = cq_shadow_get(&f->ctx.shadow, cq_bit_qindex(*t));
        if (s.unknown != (uint8_t)e->unknown)
            cq_h_fail(__FILE__, __LINE__, "%s: shadow unknown=%u, want %d",
                      what, (unsigned)s.unknown, e->unknown);
        else if (!e->unknown && s.value != (uint8_t)e->value)
            cq_h_fail(__FILE__, __LINE__, "%s: shadow value=%u, want %d",
                      what, (unsigned)s.value, e->value);
    } else if (!e->is_q && cq_bit_is_const(*t)) {
        if (cq_bit_value(*t) != e->value)
            cq_h_fail(__FILE__, __LINE__, "%s: constant is %d, want %d",
                      what, cq_bit_value(*t), e->value);
    }
}

/* Six things that must hold for EVERY case regardless of what §9's table says,
 * so a wrong oracle cannot make a wrong promotion look right. */
static void check_universal(fixture *f, const cq_bit *t,
                            const cq_bit *ctrls[], size_t n_ctrl,
                            const cq_bit before[], const cq_bit *wire,
                            int wire_value_before,
                            uint32_t minted_before, const char *what)
{
    if (!cq_bit_valid(*t))
        cq_h_fail(__FILE__, __LINE__, "%s: target is not a valid bit", what);

    for (size_t i = 0; i < n_ctrl; i++)
        if (ctrls[i]->kind != before[i].kind || ctrls[i]->q != before[i].q)
            cq_h_fail(__FILE__, __LINE__, "%s: control %zu was modified", what, i);

    /* Bennett's invariant 3 for a ControlledCircuit: "cc.ctrl_wire holds its
     * initial value — control must pass through unchanged"
     * (third_party/bennett/src/controlled.jl, verify_reversibility). The wire is
     * a control of every promoted gate and the target of none.
     *
     * ASSERTED ON THE VALUE, NOT THE KIND, because the kind version CANNOT
     * FAIL: `cb` is a local this case passes to exactly one function, through a
     * `const cq_bit *`, so nothing could make it stop being a qubit. The shadow
     * is what carries the invariant — a promotion that targeted the wire moves
     * it — and on a determinate wire that is an exact statement. */
    if (wire && wire_value_before >= 0) {
        cq_shadow s = cq_shadow_get(&f->ctx.shadow, cq_bit_qindex(*wire));
        int now = s.unknown ? -1 : (int)s.value;

        if (now != wire_value_before)
            cq_h_fail(__FILE__, __LINE__,
                      "%s: the control wire came out at %d, went in at %d — it "
                      "must pass through unchanged", what, now,
                      wire_value_before);
    }

    /* §9's rightmost column: a promoted Toffoli is three gates, plus at most
     * one materialising X on the target. */
    if (cq_mock_count(&f->mock) > 4u)
        cq_h_fail(__FILE__, __LINE__, "%s: emitted %zu gates, max is 4",
                  what, cq_mock_count(&f->mock));

    /* Two allocations are possible and no more: the shared ancilla, and the
     * one qubit materialising a constant target (Rule 5). */
    uint32_t alloc = cq_qubits_minted(&f->ctx.pool) - minted_before;
    if (alloc > 2u)
        cq_h_fail(__FILE__, __LINE__, "%s: allocated %u, at most 2 is possible",
                  what, alloc);

    /* I4 one axis over: a cell that emitted nothing must not have taken a
     * qubit. This is what row 0's "0 gates, 0 qubits" reduces to per gate. */
    if (cq_mock_count(&f->mock) == 0u && alloc != 0u)
        cq_h_fail(__FILE__, __LINE__, "%s: 0 gates but %u qubits", what, alloc);
}

/* ------------------------------------------------------------------------
 * One case, driven three ways. `arity` selects the gate; unused operand kinds
 * are ignored.
 * ------------------------------------------------------------------------ */

typedef struct {
    size_t n_gates;      /* the uncontrolled reference's stream length */
    int    value;        /* the target's value after the uncontrolled gate */
    int    value_before; /* ... and before it */
} reference;

static void run_gate(fixture *f, int arity, cq_bit *c1, cq_bit *c2, cq_bit *t)
{
    if (arity == 1) cq_emit_x(&f->ctx, t);
    else if (arity == 2) cq_emit_cx(&f->ctx, c1, t);
    else cq_emit_ccx(&f->ctx, c1, c2, t);
}

/* The same gate with no region open. Its stream is the thing row 0's ONE case
 * must reproduce byte for byte, and its resulting value is what a shadow-1
 * quantum control must reproduce. */
static reference run_reference(int arity, okind k1, okind k2, okind kt,
                               cq_rec *out, size_t cap)
{
    fixture f;
    reference r;

    fx_init(&f);
    cq_bit b1 = fx_operand(&f, k1);
    cq_bit b2 = fx_operand(&f, k2);
    cq_bit tb = fx_operand(&f, kt);
    cq_mock_reset(&f.mock);

    r.value_before = bit_value(&f, tb);
    run_gate(&f, arity, &b1, &b2, &tb);
    r.value = bit_value(&f, tb);
    r.n_gates = cq_mock_count(&f.mock);

    if (r.n_gates > cap)
        cq_h_fail(__FILE__, __LINE__, "reference stream longer than %zu", cap);
    else
        for (size_t i = 0; i < r.n_gates; i++) out[i] = *cq_mock_at(&f.mock, i);

    fx_dispose(&f);
    return r;
}

static void one_case(int arity, okind kc, okind k1, okind k2, okind kt)
{
    cq_rec    ref_stream[4];
    reference ref = run_reference(arity, k1, k2, kt, ref_stream, 4u);

    fixture f;
    fx_init(&f);

    /* OPERANDS FIRST, THEN THE CONTROL, so that every operand index matches the
     * reference run's exactly and the two streams are comparable. The control
     * wire and the shared ancilla take the indices after them. */
    cq_bit b1 = fx_operand(&f, k1);
    cq_bit b2 = fx_operand(&f, k2);
    cq_bit tb = fx_operand(&f, kt);
    cq_bit cb = fx_operand(&f, kc);

    cq_mock_reset(&f.mock);
    const uint32_t minted = cq_qubits_minted(&f.ctx.pool);
    const uint32_t live   = cq_qubits_live(&f.ctx.pool);
    const cq_bit   before[2] = { b1, b2 };
    const cq_bit  *ctrls[2]  = { &b1, &b2 };
    const cq_bit   tb0 = tb;
    const int      wire_value_before = kind_is_q(kc) ? bit_value(&f, cb) : -1;

    char what[96];
    snprintf(what, sizeof what, "ctrl=%s %s(%s,%s,%s)", kind_name(kc),
             arity == 1 ? "X" : arity == 2 ? "CX" : "CCX",
             kind_name(k1), kind_name(k2), kind_name(kt));

    cq_ctrl_push(&f.ctx, &cb);
    run_gate(&f, arity, &b1, &b2, &tb);
    cq_ctrl_pop(&f.ctx);

    if (kc == K_ZERO) {
        /* Row 0, first line. Absolute, not differential — nothing happened. */
        if (cq_mock_count(&f.mock) != 0u) {
            cq_h_fail(__FILE__, __LINE__, "%s: %zu gates, row 0 says 0",
                      what, cq_mock_count(&f.mock));
            cq_mock_dump(&f.mock, what);
        }
        if (cq_qubits_minted(&f.ctx.pool) != minted)
            cq_h_fail(__FILE__, __LINE__, "%s: row 0 says 0 qubits, minted %u",
                      what, cq_qubits_minted(&f.ctx.pool) - minted);
        if (tb.kind != tb0.kind || tb.q != tb0.q)
            cq_h_fail(__FILE__, __LINE__, "%s: the target moved", what);
    } else if (kc == K_ONE) {
        /* Row 0, second line: "emitted UNCONTROLLED, verbatim". */
        if (!cq_mock_matches(&f.mock, ref_stream, ref.n_gates)) {
            cq_h_fail(__FILE__, __LINE__,
                      "%s: row 0 says verbatim, but the stream differs from the "
                      "same gate with no region", what);
            cq_mock_dump(&f.mock, what);
        }
        if (bit_value(&f, tb) != ref.value)
            cq_h_fail(__FILE__, __LINE__, "%s: value %d, uncontrolled gives %d",
                      what, bit_value(&f, tb), ref.value);
    } else {
        pexpect e;
        memset(&e, 0, sizeof e);
        promote_oracle(&e, arity, k1, k2, kt,
                       cq_bit_qindex(cb), kind_value(kc), kind_unknown(kc),
                       kind_is_q(k1) ? cq_bit_qindex(b1) : 0u,
                       kind_is_q(k2) ? cq_bit_qindex(b2) : 0u,
                       kind_is_q(kt) ? cq_bit_qindex(tb0) : 0u,
                       minted);
        compare(&f, &e, &tb, minted, what);

        /* BENNETT'S CONTRACT — `(ctrl, x, 0) → (ctrl, x, ctrl ? f(x) : 0)` —
         * independent of the promotion's shape, and the only assertion here
         * that would survive a completely different §9.
         *
         * AN UNKNOWN ANSWER IS ALWAYS ALLOWED AND A DETERMINATE ONE MUST BE
         * RIGHT. That asymmetry is the shadow's own discipline (src/shadow.h):
         * it may report unknown where the truth is determinate — a promoted
         * CCX ORs the control wire's poison into the target even at wire value
         * 0, where the gate provably does nothing — and may never report
         * determinate where the truth is not. Writing it as a plain equality
         * asserts the wrong direction and fails on every poisoned operand.
         *
         * The second clause is what stops the first going vacuous: with no
         * poisoned operand anywhere, the shadow is EXACT and owes an answer. */
        int got  = bit_value(&f, tb);
        int want = kind_value(kc) ? ref.value : ref.value_before;
        int poisoned = kind_unknown(kc) || kind_unknown(kt)
                     || (arity >= 2 && kind_unknown(k1))
                     || (arity == 3 && kind_unknown(k2));

        if (kc != K_QU && want != -1 && got != -1 && got != want)
            cq_h_fail(__FILE__, __LINE__,
                      "%s: value %d, but (ctrl ? f(x) : x) is %d",
                      what, got, want);

        if (!poisoned && got == -1)
            cq_h_fail(__FILE__, __LINE__,
                      "%s: the shadow went unknown with no poisoned operand",
                      what);
    }

    check_universal(&f, &tb, ctrls, arity == 1 ? 0u : (size_t)(arity - 1),
                    before, kind_is_q(kc) ? &cb : NULL, wire_value_before,
                    minted, what);

    /* THE SHARED ANCILLA IS GONE. `live` is the only pool figure that can come
     * back (minted and the free-list length are monotone), and the operands plus
     * the target account for every index that legitimately stayed. */
    uint32_t grew = cq_qubits_live(&f.ctx.pool) - live;
    uint32_t want_grew = (!kind_is_q(kt) && cq_bit_is_qubit(tb)) ? 1u : 0u;
    if (grew != want_grew)
        cq_h_fail(__FILE__, __LINE__,
                  "%s: live grew by %u, want %u — the promotion's ancilla must "
                  "be back on the free list at pop", what, grew, want_grew);

    fx_dispose(&f);
}

/* ------------------------------------------------------------------------
 * The three exhaustive sweeps: 5 control kinds × §3's own 5 / 25 / 125.
 * ------------------------------------------------------------------------ */

CQ_TEST(x_under_all_5_control_kinds)
{
    for (okind kc = K_ZERO; kc < N_KINDS; kc++)
    for (okind kt = K_ZERO; kt < N_KINDS; kt++)
        one_case(1, kc, K_ZERO, K_ZERO, kt);
}

CQ_TEST(cx_under_all_5_control_kinds)
{
    for (okind kc = K_ZERO; kc < N_KINDS; kc++)
    for (okind k1 = K_ZERO; k1 < N_KINDS; k1++)
    for (okind kt = K_ZERO; kt < N_KINDS; kt++)
        one_case(2, kc, k1, K_ZERO, kt);
}

CQ_TEST(ccx_under_all_5_control_kinds)
{
    for (okind kc = K_ZERO; kc < N_KINDS; kc++)
    for (okind k1 = K_ZERO; k1 < N_KINDS; k1++)
    for (okind k2 = K_ZERO; k2 < N_KINDS; k2++)
    for (okind kt = K_ZERO; kt < N_KINDS; kt++)
        one_case(3, kc, k1, k2, kt);
}

#include "test_controlled_table.inc"

/* ------------------------------------------------------------------------
 * The region: lifecycle, nesting, the rotations, the sandwich.
 * ------------------------------------------------------------------------ */

#include "test_controlled_region.inc"

CQ_TEST_MAIN(
    CQ_CASE(x_under_all_5_control_kinds),
    CQ_CASE(cx_under_all_5_control_kinds),
    CQ_CASE(ccx_under_all_5_control_kinds),
    CQ_CASE(the_promotion_table_transcribed_from_controlled_jl),
    CQ_CASE(a_quantum_control_promotes_whatever_its_shadow_says),
    CQ_CASE(materialisation_is_not_promoted),
    CQ_CASE(a_poisoned_control_wire_does_not_block_the_ancilla_release),
    CQ_CASE(d11_refuses_only_under_a_quantum_control),
    CQ_CASE(d11_exempts_the_identity_rows_under_a_quantum_control),
    CQ_CASE(a_skipped_region_costs_a_general_rotation_nothing),
    CQ_CASE(a_measurement_is_verbatim_under_a_one_control),
    CQ_CASE(the_shared_ancilla_is_one_wire_across_the_whole_region),
    CQ_CASE(nested_control_ands_into_one_wire_and_uncomputes_it),
    CQ_CASE(the_frame_stack_survives_its_own_growth),
    CQ_CASE(row_0_composes_down_the_stack),
    CQ_CASE(the_two_rotation_rows_promote_exactly),
    CQ_CASE(an_uncontrolled_rotation_emits_its_angle_bitwise),
    CQ_CASE(a_skipped_region_costs_a_sandwich_kernel_nothing),
    CQ_CASE(a_controlled_sandwich_still_cancels_and_stays_a_palindrome),
    CQ_CASE(the_promoted_tuple_is_the_uncontrolled_one_transformed)
)
