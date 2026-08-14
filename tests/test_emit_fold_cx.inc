/* test_emit_fold_cx.inc — the 25 CX rows, hand-transcribed from PRD §3.
 *
 * Included by test_emit_fold.c. Kept in an .inc because plan §2.3 says a large
 * static test table belongs in one rather than inflating a test module — and
 * because the 300-line guard counts .c/.h/.py only, which is what makes that
 * a real escape hatch rather than a rename that dodges the rule.
 *
 * This table exists to be INDEPENDENT of the oracle in test_emit_fold.c. The
 * oracle is a second statement of §3, so a misreading of the PRD would appear
 * in both it and the emitter and cancel out. These rows were read off the four
 * CX rows of the §3 table directly:
 *
 *   c = ZERO             nothing                                     0 gates
 *   c = ONE              emit_x(t)                                   0 or 1
 *   c = Q, t constant    materialise t, then sink.cx                 1 or 2
 *   c = Q, t = Q         sink.cx                                     1
 *
 * Note rows 11-25: Q0, Q1 and QU controls all behave IDENTICALLY, because the
 * table branches on kind and never on shadow (D6, no demotion). A row where
 * they differed would be shadow-driven demotion having crept in.
 */

typedef struct {
    okind    c, t;
    size_t   n_gates;
    cq_op    last_op;   /* only read when n_gates > 0 */
    uint32_t n_alloc;
    int      target_is_q;
} cx_row;

static const cx_row cx_literal[] = {
    /* c = ZERO — nothing happens, whatever the target is. */
    { K_ZERO, K_ZERO, 0, CQ_OP_X,  0, 0 },
    { K_ZERO, K_ONE,  0, CQ_OP_X,  0, 0 },
    { K_ZERO, K_Q0,   0, CQ_OP_X,  0, 1 },
    { K_ZERO, K_Q1,   0, CQ_OP_X,  0, 1 },
    { K_ZERO, K_QU,   0, CQ_OP_X,  0, 1 },

    /* c = ONE — degenerates to X(t): free on a constant, one gate on a qubit. */
    { K_ONE,  K_ZERO, 0, CQ_OP_X,  0, 0 },
    { K_ONE,  K_ONE,  0, CQ_OP_X,  0, 0 },
    { K_ONE,  K_Q0,   1, CQ_OP_X,  0, 1 },
    { K_ONE,  K_Q1,   1, CQ_OP_X,  0, 1 },
    { K_ONE,  K_QU,   1, CQ_OP_X,  0, 1 },

    /* c = Q known-0 — a Q control is NOT folded away on a known-0 shadow. */
    { K_Q0,   K_ZERO, 1, CQ_OP_CX, 1, 1 },   /* materialise 0: no X needed   */
    { K_Q0,   K_ONE,  2, CQ_OP_CX, 1, 1 },   /* materialise 1: X, then CX    */
    { K_Q0,   K_Q0,   1, CQ_OP_CX, 0, 1 },
    { K_Q0,   K_Q1,   1, CQ_OP_CX, 0, 1 },
    { K_Q0,   K_QU,   1, CQ_OP_CX, 0, 1 },

    /* c = Q known-1 — identical to Q known-0. */
    { K_Q1,   K_ZERO, 1, CQ_OP_CX, 1, 1 },
    { K_Q1,   K_ONE,  2, CQ_OP_CX, 1, 1 },
    { K_Q1,   K_Q0,   1, CQ_OP_CX, 0, 1 },
    { K_Q1,   K_Q1,   1, CQ_OP_CX, 0, 1 },
    { K_Q1,   K_QU,   1, CQ_OP_CX, 0, 1 },

    /* c = Q unknown — identical again. */
    { K_QU,   K_ZERO, 1, CQ_OP_CX, 1, 1 },
    { K_QU,   K_ONE,  2, CQ_OP_CX, 1, 1 },
    { K_QU,   K_Q0,   1, CQ_OP_CX, 0, 1 },
    { K_QU,   K_Q1,   1, CQ_OP_CX, 0, 1 },
    { K_QU,   K_QU,   1, CQ_OP_CX, 0, 1 },
};
