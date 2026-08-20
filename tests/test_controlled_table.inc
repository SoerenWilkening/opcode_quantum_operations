/* test_controlled_table.inc — §9's promotion, transcribed by hand.
 *
 * Included by test_controlled.c. These rows NEVER go through
 * test_controlled_oracle.inc: they are the literal gate lists, with literal
 * qubit indices, read straight off PRD §9's block and
 * third_party/bennett/src/controlled.jl's `promote_gate!`. The oracle is a
 * second reading of the same prose and could in principle misread it the same
 * way src/controlled.c did; a transcription cannot.
 *
 * test_emit_fold_cx.inc is the precedent and the reason: 25 rows of §3 pinned
 * literally beside an oracle that also computes them.
 */

/* A fresh |0⟩ wire, taken through the same door a real program uses. */
static cq_bit fresh_q(fixture *f)
{
    return cq_bit_qubit(cq_ctx_fresh_qubit(&f->ctx));
}

static void pin_stream(fixture *f, const cq_rec *want, size_t n,
                       const char *what)
{
    if (!cq_mock_matches(&f->mock, want, n)) {
        cq_h_fail(__FILE__, __LINE__, "%s: stream differs", what);
        cq_mock_dump(&f->mock, what);
    }
}

CQ_TEST(the_promotion_table_transcribed_from_controlled_jl)
{
    /* --- NOT → CNOT(ctrl, t) --------------------------------------------- */
    {
        fixture f;
        fx_init(&f);
        cq_bit t = fresh_q(&f);              /* q0 */
        cq_bit w = fresh_q(&f);              /* q1 */
        cq_mock_reset(&f.mock);

        cq_ctrl_push(&f.ctx, &w);
        cq_emit_x(&f.ctx, &t);
        cq_ctrl_pop(&f.ctx);

        const cq_rec want[] = { CQ_REC_CX(1u, 0u) };
        pin_stream(&f, want, 1u, "NOT -> CNOT");
        CHECK_EQ((int)cq_qubits_minted(&f.ctx.pool), 2);  /* no ancilla */
        fx_dispose(&f);
    }

    /* --- CNOT → Toffoli(ctrl, c, t) -------------------------------------- */
    {
        fixture f;
        fx_init(&f);
        cq_bit t = fresh_q(&f);              /* q0 */
        cq_bit c = fresh_q(&f);              /* q1 */
        cq_bit w = fresh_q(&f);              /* q2 */
        cq_mock_reset(&f.mock);

        cq_ctrl_push(&f.ctx, &w);
        cq_emit_cx(&f.ctx, &c, &t);
        cq_ctrl_pop(&f.ctx);

        const cq_rec want[] = { CQ_REC_CCX(2u, 1u, 0u) };
        pin_stream(&f, want, 1u, "CNOT -> Toffoli");
        CHECK_EQ((int)cq_qubits_minted(&f.ctx.pool), 3);  /* still no ancilla */
        fx_dispose(&f);
    }

    /* --- Toffoli → three Toffolis and one reusable ancilla ---------------
     *
     *   push!(out, ToffoliGate(ctrl, g.control1, anc))     # anc = ctrl ∧ c1
     *   push!(out, ToffoliGate(anc, g.control2, g.target)) # target ⊻= anc ∧ c2
     *   push!(out, ToffoliGate(ctrl, g.control1, anc))     # uncompute anc
     *
     * Gates 1 and 3 are IDENTICAL, which is what makes the block self-inverse
     * and therefore a legal cq_sandwich step — and what makes the recorded
     * stream stay a palindrome through the promotion. */
    {
        fixture f;
        fx_init(&f);
        cq_bit t  = fresh_q(&f);             /* q0 */
        cq_bit c1 = fresh_q(&f);             /* q1 */
        cq_bit c2 = fresh_q(&f);             /* q2 */
        cq_bit w  = fresh_q(&f);             /* q3 */
        cq_mock_reset(&f.mock);

        cq_ctrl_push(&f.ctx, &w);
        cq_emit_ccx(&f.ctx, &c1, &c2, &t);
        cq_ctrl_pop(&f.ctx);

        const cq_rec want[] = {
            CQ_REC_CCX(3u, 1u, 4u),          /* anc = ctrl ∧ c1        */
            CQ_REC_CCX(4u, 2u, 0u),          /* target ⊻= anc ∧ c2     */
            CQ_REC_CCX(3u, 1u, 4u)           /* uncompute anc          */
        };
        pin_stream(&f, want, 3u, "Toffoli -> 3 Toffolis");
        CHECK_EQ((int)cq_qubits_minted(&f.ctx.pool), 5);  /* q4 is the ancilla */

        /* And it went back: `live` is the only figure that can (src/qubits.h). */
        CHECK_EQ((int)cq_qubits_live(&f.ctx.pool), 4);
        CHECK(cq_qubits_is_free(&f.ctx.pool, 4u));

        /* The block IS a palindrome, which is the property cq_sandwich's
         * replay-in-reverse rests on. Asserted here, on the block alone, so
         * that a sandwich case failing later has somewhere to point. */
        CHECK(cq_mock_is_palindrome(&f.mock, 1u, 1u));
        fx_dispose(&f);
    }
}
