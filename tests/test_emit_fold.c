/* Step 6's gate: the PRD §3 fold table, exhaustively. L0.
 *
 * THE CRITICAL PATH (plan §5, Rule 11). The fold table is the only place
 * classical/quantum is decided; a bug here is a bug in all twelve kernels at
 * once, and it will present as a kernel bug. Everything above this layer is
 * Bennett transcribed against three functions, so these three functions get
 * over-invested in.
 *
 * 155 exhaustive cases — 5 X + 25 CX + 125 CCX — over the five operand kinds
 * {const-0, const-1, Q known-0, Q known-1, Q unknown}. Each pins FOUR things:
 * the exact gate sequence, the qubits allocated, the resulting bit-kind, and
 * the resulting shadow. The 4 distinctness death-tests live in
 * test_emit_death.c, since a process that aborts cannot also run 155 cases.
 *
 * WHY FIVE KINDS WHEN THE TABLE BRANCHES ON THREE. The table dispatches on
 * kind and never on shadow — a Q control whose shadow is known-0 is NOT
 * folded away, because that would be shadow-driven demotion and D6 excludes
 * it from v1. So there are only 3 + 9 + 27 = 39 distinct gate-emission
 * behaviours here. The five-way split exists to pin the resulting SHADOW,
 * which the three-way split cannot see.
 *
 * THE ORACLE IS A SECOND STATEMENT OF PRD §3, not a call into the emitter.
 * It follows the PRD's own rows, and for CCX it uses the PRD's own reductions
 * ("either control ZERO -> nothing", "c1 = ONE -> emit_cx(c2,t)") rather than
 * re-deriving anything. Where it could still drift, the 25 CX rows are ALSO
 * pinned as a literal hand-written table that never goes through the oracle.
 *
 * On top of that, every one of the 155 cases is checked against five
 * invariants that do not depend on the fold table at all — see check_universal.
 */

#include "ctx.h"
#include "emit.h"
#include "support/harness.h"
#include "support/mock_sink.h"

#include <stdio.h>
#include <string.h>

typedef enum { K_ZERO = 0, K_ONE, K_Q0, K_Q1, K_QU, N_KINDS } okind;

static const char *kind_name(okind k)
{
    static const char *n[N_KINDS] = { "ZERO", "ONE", "Q0", "Q1", "QU" };
    return n[k];
}

static int kind_is_q(okind k)   { return k >= K_Q0; }
static int kind_unknown(okind k){ return k == K_QU; }
/* Shadow value of a kind, meaningful only when !kind_unknown. */
static int kind_value(okind k)  { return k == K_ONE || k == K_Q1; }

/* ------------------------------------------------------------------------
 * Fixture.
 * ------------------------------------------------------------------------ */

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

/* Builds an operand of the requested kind. Q operands take a real qubit from
 * the pool so the indices in the recorded stream are the ones a real program
 * would see. The mock is reset by the caller after ALL operands are built, so
 * setup gates never reach the assertions. */
static cq_bit fx_operand(fixture *f, okind k)
{
    if (k == K_ZERO) return cq_bit_zero();
    if (k == K_ONE)  return cq_bit_one();

    uint32_t q = cq_ctx_fresh_qubit(&f->ctx);
    cq_bit   b = cq_bit_qubit(q);

    if (k == K_Q1) cq_emit_x(&f->ctx, &b);              /* shadow -> known 1 */
    if (k == K_QU) cq_shadow_rotate(&f->ctx.shadow, q); /* shadow -> unknown */
    return b;
}

#include "test_emit_fold_oracle.inc"

/* ------------------------------------------------------------------------
 * Comparison, and the fold-table-independent invariants.
 * ------------------------------------------------------------------------ */

static void compare(fixture *f, const expect *e, const cq_bit *t,
                    const char *what)
{
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

/* Five things that must hold for EVERY case regardless of what the fold table
 * says, so a wrong oracle cannot make a wrong emitter look right. */
static void check_universal(fixture *f, const expect *e, const cq_bit *t,
                            const cq_bit *ctrls[], size_t n_ctrl,
                            const cq_bit before[], uint32_t minted_before,
                            const char *what)
{
    /* 1. I1 — whatever came out is still exactly one of the three kinds. */
    if (!cq_bit_valid(*t))
        cq_h_fail(__FILE__, __LINE__, "%s: target is not a valid bit", what);

    /* 2. Controls are never modified. They are `const cq_bit *` by signature
     *    (one of the two mechanisms enforcing I6), so this catches a fold that
     *    cast the qualifier away. */
    for (size_t i = 0; i < n_ctrl; i++) {
        if (ctrls[i]->kind != before[i].kind || ctrls[i]->q != before[i].q)
            cq_h_fail(__FILE__, __LINE__, "%s: control %zu was modified", what, i);
    }

    /* 3. PRD §3's rightmost column never exceeds 2 gates. */
    if (cq_mock_count(&f->mock) > 2u)
        cq_h_fail(__FILE__, __LINE__, "%s: emitted %zu gates, max is 2",
                  what, cq_mock_count(&f->mock));

    /* 4. Allocation happens in exactly one place (Rule 5), so the only qubit
     *    a fold may take is the one materialising a constant target. */
    uint32_t alloc = cq_qubits_minted(&f->ctx.pool) - minted_before;
    if (alloc != e->n_alloc)
        cq_h_fail(__FILE__, __LINE__, "%s: allocated %u qubits, want %u",
                  what, alloc, e->n_alloc);
    if (alloc > 1u)
        cq_h_fail(__FILE__, __LINE__, "%s: allocated %u, at most 1 is possible",
                  what, alloc);

    /* 5. I4 — a fold that emitted nothing must not have taken a qubit. */
    if (cq_mock_count(&f->mock) == 0u && alloc != 0u)
        cq_h_fail(__FILE__, __LINE__, "%s: 0 gates but %u qubits", what, alloc);
}

/* ------------------------------------------------------------------------
 * The three exhaustive sweeps.
 * ------------------------------------------------------------------------ */

CQ_TEST(x_over_all_5_operand_kinds)
{
    for (okind t = K_ZERO; t < N_KINDS; t++) {
        fixture f;
        fx_init(&f);
        cq_bit tb = fx_operand(&f, t);
        cq_mock_reset(&f.mock);
        uint32_t minted = cq_qubits_minted(&f.ctx.pool);

        expect e;
        memset(&e, 0, sizeof e);
        x_oracle(&e, t, cq_bit_is_qubit(tb) ? cq_bit_qindex(tb) : 0u);

        cq_emit_x(&f.ctx, &tb);

        char what[64];
        snprintf(what, sizeof what, "X(%s)", kind_name(t));
        compare(&f, &e, &tb, what);
        check_universal(&f, &e, &tb, NULL, 0, NULL, minted, what);
        fx_dispose(&f);
    }
}

CQ_TEST(cx_over_all_25_operand_pairs)
{
    for (okind c = K_ZERO; c < N_KINDS; c++)
    for (okind t = K_ZERO; t < N_KINDS; t++) {
        fixture f;
        fx_init(&f);
        cq_bit cb = fx_operand(&f, c);
        cq_bit tb = fx_operand(&f, t);
        cq_mock_reset(&f.mock);

        uint32_t minted = cq_qubits_minted(&f.ctx.pool);
        cq_bit   before = cb;
        const cq_bit *ctrls[1] = { &cb };

        expect e;
        memset(&e, 0, sizeof e);
        cx_oracle(&e, c, t,
                  cq_bit_is_qubit(cb) ? cq_bit_qindex(cb) : 0u,
                  cq_bit_is_qubit(tb) ? cq_bit_qindex(tb) : 0u,
                  minted);

        cq_emit_cx(&f.ctx, &cb, &tb);

        char what[64];
        snprintf(what, sizeof what, "CX(%s,%s)", kind_name(c), kind_name(t));
        compare(&f, &e, &tb, what);
        check_universal(&f, &e, &tb, ctrls, 1, &before, minted, what);
        fx_dispose(&f);
    }
}

CQ_TEST(ccx_over_all_125_operand_triples)
{
    for (okind c1 = K_ZERO; c1 < N_KINDS; c1++)
    for (okind c2 = K_ZERO; c2 < N_KINDS; c2++)
    for (okind t  = K_ZERO; t  < N_KINDS; t++) {
        fixture f;
        fx_init(&f);
        cq_bit b1 = fx_operand(&f, c1);
        cq_bit b2 = fx_operand(&f, c2);
        cq_bit tb = fx_operand(&f, t);
        cq_mock_reset(&f.mock);

        uint32_t minted = cq_qubits_minted(&f.ctx.pool);
        cq_bit   before[2] = { b1, b2 };
        const cq_bit *ctrls[2] = { &b1, &b2 };

        expect e;
        memset(&e, 0, sizeof e);
        ccx_oracle(&e, c1, c2, t,
                   cq_bit_is_qubit(b1) ? cq_bit_qindex(b1) : 0u,
                   cq_bit_is_qubit(b2) ? cq_bit_qindex(b2) : 0u,
                   cq_bit_is_qubit(tb) ? cq_bit_qindex(tb) : 0u,
                   minted);

        cq_emit_ccx(&f.ctx, &b1, &b2, &tb);

        char what[80];
        snprintf(what, sizeof what, "CCX(%s,%s,%s)",
                 kind_name(c1), kind_name(c2), kind_name(t));
        compare(&f, &e, &tb, what);
        check_universal(&f, &e, &tb, ctrls, 2, before, minted, what);
        fx_dispose(&f);
    }
}

/* ------------------------------------------------------------------------
 * Independent pins that never touch the oracle.
 * ------------------------------------------------------------------------ */

#include "test_emit_fold_cx.inc"

CQ_TEST(cx_matches_a_literal_hand_written_table)
{
    /* The oracle above is a second statement of PRD §3, which means an error
     * in READING the PRD would appear in both it and the emitter. These 25
     * rows are transcribed by hand from the §3 table's four CX rows and are
     * compared without the oracle ever running. */
    for (size_t i = 0; i < sizeof cx_literal / sizeof cx_literal[0]; i++) {
        const cx_row *r = &cx_literal[i];
        fixture f;
        fx_init(&f);
        cq_bit cb = fx_operand(&f, r->c);
        cq_bit tb = fx_operand(&f, r->t);
        cq_mock_reset(&f.mock);
        uint32_t minted = cq_qubits_minted(&f.ctx.pool);

        cq_emit_cx(&f.ctx, &cb, &tb);

        char what[64];
        snprintf(what, sizeof what, "CX(%s,%s) literal",
                 kind_name(r->c), kind_name(r->t));

        if (cq_mock_count(&f.mock) != r->n_gates)
            cq_h_fail(__FILE__, __LINE__, "%s: %zu gates, want %zu", what,
                      cq_mock_count(&f.mock), r->n_gates);
        else if (r->n_gates > 0 &&
                 cq_mock_at(&f.mock, r->n_gates - 1)->op != r->last_op)
            cq_h_fail(__FILE__, __LINE__, "%s: last gate is op %d, want %d",
                      what, (int)cq_mock_at(&f.mock, r->n_gates - 1)->op,
                      (int)r->last_op);

        if (cq_qubits_minted(&f.ctx.pool) - minted != r->n_alloc)
            cq_h_fail(__FILE__, __LINE__, "%s: allocated %u, want %u", what,
                      cq_qubits_minted(&f.ctx.pool) - minted, r->n_alloc);
        if (cq_bit_is_qubit(tb) != r->target_is_q)
            cq_h_fail(__FILE__, __LINE__, "%s: target kind wrong", what);

        fx_dispose(&f);
    }
}

CQ_TEST(the_fully_classical_path_costs_nothing)
{
    /* L5 in miniature, and the reason the whole tri-valued representation
     * exists: constants must cost zero qubits and zero gates. */
    fixture f;
    fx_init(&f);

    cq_bit z = cq_bit_zero(), o = cq_bit_one(), t = cq_bit_zero();
    cq_emit_ccx(&f.ctx, &z, &o, &t);
    cq_emit_cx (&f.ctx, &z, &t);
    cq_emit_ccx(&f.ctx, &o, &o, &t);      /* both ONE: flips the constant */
    cq_emit_x  (&f.ctx, &t);

    CHECK_EQ(cq_mock_count(&f.mock), 0u);
    CHECK_EQ(cq_qubits_minted(&f.ctx.pool), 0u);
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), 0u);
    CHECK(cq_bit_is_const(t));
    CHECK_EQ(cq_bit_value(t), 0);         /* flipped twice */

    fx_dispose(&f);
}

CQ_TEST(a_q_control_with_a_known_zero_shadow_is_not_folded_away)
{
    /* D6 — no shadow-driven demotion in v1. A Q control whose shadow says 0
     * still emits, because folding it would make the qubit count depend on
     * shadow precision and every L4 golden fragile. */
    fixture f;
    fx_init(&f);
    cq_bit c = fx_operand(&f, K_Q0);
    cq_bit t = fx_operand(&f, K_Q0);
    cq_mock_reset(&f.mock);

    cq_emit_cx(&f.ctx, &c, &t);

    CHECK_EQ(cq_mock_count(&f.mock), 1u);
    CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_CX), 1u);
    fx_dispose(&f);
}

CQ_TEST_MAIN(
    CQ_CASE(x_over_all_5_operand_kinds),
    CQ_CASE(cx_over_all_25_operand_pairs),
    CQ_CASE(ccx_over_all_125_operand_triples),
    CQ_CASE(cx_matches_a_literal_hand_written_table),
    CQ_CASE(the_fully_classical_path_costs_nothing),
    CQ_CASE(a_q_control_with_a_known_zero_shadow_is_not_folded_away)
)
