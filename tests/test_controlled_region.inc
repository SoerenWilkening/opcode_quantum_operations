/* test_controlled_region.inc — the REGION half of M06's suite.
 *
 * Included by test_controlled.c, on the seam that file's header records: the
 * three exhaustive sweeps and their oracle live there, and everything about a
 * region's LIFECYCLE — row 0 composing down the stack, the shared ancilla, the
 * nested AND, §7's two rotation rows, and what a whole sandwich kernel does
 * under a control — lives here.
 */

/* --- D6 one axis over: kind, never shadow. -------------------------------- */

CQ_TEST(a_quantum_control_promotes_whatever_its_shadow_says)
{
    /* A Q control whose shadow reads a determinate 0 is STILL a promotion, and
     * that is the same rule the §3 fold table already follows for its own
     * operands (D6, no demotion). Folding it away would be shadow-driven
     * demotion at the region level: the emitted gate count would depend on
     * shadow precision, and every L4 golden with it.
     *
     * The three Q kinds must therefore all produce the IDENTICAL stream, which
     * is what this asserts — the sweeps above check each against the oracle,
     * and this checks them against each other, which no oracle can. */
    cq_rec seen[3][1];

    for (int k = 0; k < 3; k++) {
        fixture f;
        fx_init(&f);
        cq_bit t = fresh_q(&f);                        /* q0 */
        cq_bit w = fx_operand(&f, (okind)(K_Q0 + k));  /* q1 */
        cq_mock_reset(&f.mock);

        cq_ctrl_push(&f.ctx, &w);
        cq_emit_x(&f.ctx, &t);
        cq_ctrl_pop(&f.ctx);

        CHECK_EQ((int)cq_mock_count(&f.mock), 1);
        if (cq_mock_count(&f.mock) == 1u) seen[k][0] = *cq_mock_at(&f.mock, 0);
        fx_dispose(&f);
    }

    CHECK(memcmp(&seen[0][0], &seen[1][0], sizeof seen[0][0]) == 0);
    CHECK(memcmp(&seen[0][0], &seen[2][0], sizeof seen[0][0]) == 0);
}

/* --- bd skh, and the one fixture that can falsify it. --------------------- */

CQ_TEST(materialisation_is_not_promoted)
{
    /* THE DISCRIMINATOR IS A CONSTANT-**ONE** TARGET UNDER A SHADOW-**ZERO**
     * CONTROL, and neither half is negotiable. At shadow 1 the promoted and
     * unpromoted choices agree — the disagreement is the single cell
     * `b = 1, k = 0` — and at a ZERO target there is no X to promote at all.
     *
     * CQ_lang ships this shape: slice_control_cond_onward_phase copies a rail
     * born 10 into one born 3 under a quantum control, and `10 & 3` has bit 1
     * set in both.
     *
     * A GATE COUNT CANNOT SEE IT. Both choices emit exactly two gates; they
     * differ in the KIND of the first (X versus CX) and, decisively, in the
     * value the rail ends up holding. */
    for (int ctrl_value = 0; ctrl_value <= 1; ctrl_value++) {
        fixture f;
        fx_init(&f);

        cq_bit t = cq_bit_one();                                   /* constant 1 */
        cq_bit c = fx_operand(&f, K_Q1);                           /* q0, value 1 */
        cq_bit w = fx_operand(&f, ctrl_value ? K_Q1 : K_Q0);       /* q1          */
        cq_mock_reset(&f.mock);

        cq_ctrl_push(&f.ctx, &w);
        cq_emit_cx(&f.ctx, &c, &t);          /* t ^= c, under control w */
        cq_ctrl_pop(&f.ctx);

        /* The materialising X is UNPROMOTED: `X(q2)`, not `CX(q1, q2)`. */
        const cq_rec want[] = { CQ_REC_X(2u), CQ_REC_CCX(1u, 0u, 2u) };
        pin_stream(&f, want, 2u, ctrl_value ? "skh ctrl=1" : "skh ctrl=0");

        /* And the value is the whole point: `b ⊕ (k ∧ c)` with b = 1, c = 1. */
        CHECK_EQ(bit_value(&f, t), ctrl_value ? 0 : 1);
        fx_dispose(&f);
    }
}

CQ_TEST(a_poisoned_control_wire_does_not_block_the_ancilla_release)
{
    /* THE TRAP THIS CASE EXISTS FOR. M06's `proven_zero` evidence must be the
     * STRUCTURAL constant, never cq_shadow_known_zero — and the wrong choice
     * PASSES every other test in this project.
     *
     * The shadow's CCX rule is `t.unknown |= a.unknown | b.unknown` with no
     * clearing path, so a poisoned control wire leaves the promoted block's
     * ancilla at {value 0, unknown 1}: a shadow check would refuse to release a
     * qubit that is provably |0> by construction, and would refuse it in BOTH
     * configurations. Nothing else here can see that, because a wire only
     * becomes poisoned through M22's general-Ry row and no kernel suite
     * rotates — so on the whole surface Step 20's own gate exercises, the
     * unsound evidence agrees with the sound one and the abort waits for the
     * corpus.
     *
     * The gates are identical to the determinate case; only the release is at
     * stake, so a gate count cannot see this either. */
    fixture f;
    fx_init(&f);

    cq_bit t  = fresh_q(&f);              /* q0 */
    cq_bit c1 = fresh_q(&f);              /* q1 */
    cq_bit c2 = fresh_q(&f);              /* q2 */
    cq_bit w  = fx_operand(&f, K_QU);     /* q3, poisoned by cq_shadow_rotate */
    cq_mock_reset(&f.mock);

    const uint32_t live = cq_qubits_live(&f.ctx.pool);

    CHECK(cq_shadow_get(&f.ctx.shadow, cq_bit_qindex(w)).unknown == 1u);

    cq_ctrl_push(&f.ctx, &w);
    cq_emit_ccx(&f.ctx, &c1, &c2, &t);    /* takes the shared ancilla */
    cq_ctrl_pop(&f.ctx);                  /* ... and must give it back */

    CHECK_EQ((int)cq_mock_count(&f.mock), 3);
    CHECK_EQ((int)cq_qubits_live(&f.ctx.pool), (int)live);
    CHECK(cq_qubits_is_free(&f.ctx.pool, 4u));

    /* The nested AND flag is the same argument with the same answer, and it is
     * a separate wire with a separate release, so it gets its own probe. */
    cq_bit w2 = fx_operand(&f, K_QU);
    cq_ctrl_push(&f.ctx, &w);
    cq_ctrl_push(&f.ctx, &w2);
    cq_emit_x(&f.ctx, &t);
    cq_ctrl_pop(&f.ctx);
    cq_ctrl_pop(&f.ctx);

    CHECK_EQ((int)cq_qubits_live(&f.ctx.pool), (int)live + 1);  /* w2 stays */
    fx_dispose(&f);
}

CQ_TEST(d11_refuses_only_under_a_quantum_control)
{
    /* THE OTHER HALF OF D11, and it is the half that keeps the PRD's zero-cost
     * claims true. Every §7 fold row must still fold — for zero gates and zero
     * qubits where §7 says so — with NO region open, under a CQ_BIT_ONE control
     * (row 0: the region is emitted uncontrolled, verbatim) and under a
     * CQ_BIT_ZERO one (row 0: the region is skipped). A refusal that fired on
     * any of those would break Rule 15's constant flip and §11's L5 shapes,
     * which are exactly what row 0 exists to preserve.
     *
     * An implication-shaped guard passes vacuously against a module that never
     * refuses, so this is the positive control for the five death cases in
     * tests/test_controlled_death.c and neither half is optional. */
    /* acos(-1.0), not CQ_ANGLE_PI: an oracle that shares a constant with the
     * code is blind to exactly what that constant gets wrong (Step 18's
     * lesson). Here the angle is the INPUT, and the claim is about the rows a
     * caller's ordinary spelling of π lands on. */
    const double pi = acos(-1.0);
    const double folds[] = { 2.0 * pi, pi, 3.0 * pi };

    for (int mode = 0; mode < 3; mode++) {
        for (size_t i = 0; i < sizeof folds / sizeof folds[0]; i++) {
            fixture f;
            fx_init(&f);
            int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 4u);   /* all constant */
            cq_bit  one = cq_bit_one(), zero = cq_bit_zero();
            cq_mock_reset(&f.mock);
            const uint32_t minted = cq_qubits_minted(&f.ctx.pool);

            if (mode == 1) cq_ctrl_push(&f.ctx, &one);
            if (mode == 2) cq_ctrl_push(&f.ctx, &zero);
            cq_rotate_ry(&f.ctx, h, folds[i]);
            cq_rotate_rz(&f.ctx, h, 0.4);
            if (mode != 0) cq_ctrl_pop(&f.ctx);

            /* §7's own claim, unchanged by the axis existing: a fold on a
             * CONSTANT rail costs zero gates and zero qubits. */
            if (cq_mock_count(&f.mock) != 0u)
                cq_h_fail(__FILE__, __LINE__,
                          "mode %d, theta %g: %zu gates, §7 says 0",
                          mode, folds[i], cq_mock_count(&f.mock));
            CHECK_EQ((int)(cq_qubits_minted(&f.ctx.pool) - minted), 0);
            fx_dispose(&f);
        }
    }
}

CQ_TEST(d11_exempts_the_identity_rows_under_a_quantum_control)
{
    /* THE OTHER SIDE OF D11, AND THE SIDE NO ASSERTION IN THE PROJECT REACHED.
     * `d11_refuses_only_under_a_quantum_control` above sweeps §7's FOLD rows
     * with no region, under a ONE control and under a ZERO one; the five death
     * cases in tests/test_controlled_death.c sweep those same rows under a
     * QUANTUM control and require the abort. Between them, nothing said what
     * happens to §7's IDENTITY rows under a quantum control — and controlled-I
     * is I, so they are exempt: they must emit nothing AND refuse nothing.
     *
     * MEASURED, and this case exists because of it: making CQ_ANGLE_IDENTITY
     * call cq_ctrl_refuse_fold_row survives the whole 195-test suite in both
     * configurations. That mutant hard-errors on a legal controlled `Ry(0)`,
     * `Ry(4pi)` or `Rz(0)` — a refusal in the forbidden direction, exactly as
     * deleting a rotation is the forbidden direction for a fold (D10).
     *
     * THE STREAM IS PINNED RATHER THAN COUNTED, AND THE ONE GATE IN IT IS THE
     * POINT. A case that asserted only "zero gates" would pass just as happily
     * against a fixture whose control had accidentally become CLASSICAL — that
     * is §9 row 0's SKIP, where nothing emits for reasons that have nothing to
     * do with D11, and it is the same vacuity that let the shared driver's
     * CQ_KD_CTRL_Q0 rail be degraded to a constant with every assertion still
     * green (tests/support/kernelctrl.c). The trailing X is the tripwire: it
     * proves the region really is promoting, inside the very assertion that
     * proves the two rotations added nothing to the stream.
     *
     * All three Q kinds, because row 0 dispatches on KIND and never on the
     * shadow (D6). And -4pi as well as +4pi, because bd fna's split mirrors the
     * row under negation — k = 0 (mod 4) is one of the two residues that must
     * be its own mirror, and the identity row is the one where getting that
     * wrong deletes an operator instead of naming the wrong parity. */
    const double pi = acos(-1.0);
    const double ident[] = { 0.0, 4.0 * pi, -4.0 * pi };

    for (int k = 0; k < 3; k++) {
        for (size_t i = 0; i < sizeof ident / sizeof ident[0]; i++) {
            fixture f;
            fx_init(&f);

            cq_bit  w = fx_operand(&f, (okind)(K_Q0 + k));   /* q0 */
            int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 2u);
            cq_bit *b = cq_reg_bits(&f.ctx.regs, h);

            /* BOTH OF §7's COLUMNS IN ONE RAIL: b[0] stays a constant, b[1] is
             * a wire. The identity rows return before reading the bit at all,
             * so a mutant that let one column through would be caught here
             * whichever column it was. */
            b[1] = fresh_q(&f);                              /* q1 */
            cq_bit t = fresh_q(&f);                          /* q2 */
            cq_mock_reset(&f.mock);
            const uint32_t minted = cq_qubits_minted(&f.ctx.pool);

            cq_ctrl_push(&f.ctx, &w);
            cq_rotate_ry(&f.ctx, h, ident[i]);      /* theta = 0 (mod 4pi) */
            cq_rotate_rz(&f.ctx, h, ident[i]);      /* phi   = 0 (mod 4pi) */
            cq_emit_x(&f.ctx, &t);                  /* ... and the tripwire */
            cq_ctrl_pop(&f.ctx);

            const cq_rec want[] = { CQ_REC_CX(0u, 2u) };
            pin_stream(&f, want, 1u, kind_name((okind)(K_Q0 + k)));

            /* Zero qubits, which the GENERAL row would not have managed: it
             * materialises a constant bit before rotating, so an identity row
             * that fell through to it costs W wires and no gate count sees the
             * difference until the rotation itself is emitted. */
            CHECK_EQ((int)(cq_qubits_minted(&f.ctx.pool) - minted), 0);
            CHECK_EQ((int)cq_pc_value(&f.ctx, h), 0);

            /* AND D12's PAYOFF SURVIVES THE AXIS: an identity row is not a
             * rotation, so it must not poison, and the rail stays freeable. */
            CHECK(cq_shadow_get(&f.ctx.shadow,
                                cq_bit_qindex(b[1])).unknown == 0u);
            fx_dispose(&f);
        }
    }
}

CQ_TEST(a_skipped_region_costs_a_general_rotation_nothing)
{
    /* THE ONLY CASE THAT CAN FALSIFY M22's ROW-0 SKIP, and it needs a GENERAL
     * angle rather than one of §7's fold rows. A fold row emits nothing and
     * mints nothing whether or not the skip is there, so `d11_refuses_only_
     * under_a_quantum_control` — which sweeps 2pi, pi and 3pi — is green
     * against an M22 with no skip at all.
     *
     * The general row is the one cell of §7 that ALLOCATES: it materialises a
     * constant bit before rotating (Rule 5's single allocator). So a skipped
     * region containing a general Ry on a classical rail costs W qubits unless
     * M22 returns first — which is why the skip has to be in M22 and cannot be
     * left to cq_emit_*, and why row 0's claim is "0 gates, 0 QUBITS". */
    const double general = 0.7;                 /* not on the pi lattice */

    {   /* Ry on a CONSTANT rail: the allocating cell. */
        fixture f;
        fx_init(&f);
        int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 4u);
        cq_bit  z = cq_bit_zero();
        cq_mock_reset(&f.mock);
        const uint32_t minted = cq_qubits_minted(&f.ctx.pool);

        cq_ctrl_push(&f.ctx, &z);
        cq_rotate_ry(&f.ctx, h, general);
        cq_ctrl_pop(&f.ctx);

        CHECK_EQ((int)cq_mock_count(&f.mock), 0);
        CHECK_EQ((int)(cq_qubits_minted(&f.ctx.pool) - minted), 0);
        CHECK_EQ((int)cq_pc_value(&f.ctx, h), 0);
        fx_dispose(&f);
    }

    {   /* Ry and Rz on a QUBIT rail: the cells that emit. */
        fixture f;
        fx_init(&f);
        int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 1u);
        cq_bit *b = cq_reg_bits(&f.ctx.regs, h);
        *b = fresh_q(&f);
        cq_bit z = cq_bit_zero();
        cq_mock_reset(&f.mock);

        cq_ctrl_push(&f.ctx, &z);
        cq_rotate_ry(&f.ctx, h, general);
        cq_rotate_rz(&f.ctx, h, general);
        cq_ctrl_pop(&f.ctx);

        CHECK_EQ((int)cq_mock_count(&f.mock), 0);

        /* AND THE SHADOW IS UNTOUCHED. A skipped general Ry must not poison:
         * the rotation did not happen, so D12's payoff — the rail stays
         * freeable — must survive being inside a region that did not run. */
        CHECK(cq_shadow_get(&f.ctx.shadow, cq_bit_qindex(*b)).unknown == 0u);
        fx_dispose(&f);
    }
}

CQ_TEST(a_measurement_is_verbatim_under_a_one_control)
{
    /* THE POSITIVE CONTROL FOR cq_ctrl_refuse_measurement's THIRD LEG. Its
     * contract is three-way — refused under PROMOTE, refused under SKIP, LEGAL
     * under OFF — and OFF is reachable only through row 0's CQ_BIT_ONE branch.
     * The two death cases pin the refusals; without this, narrowing the guard
     * to `if (!t) return;` would abort on a legal program (a measurement inside
     * a constant-true branch) and every test would stay green.
     *
     * It pins the STREAM, not just the absence of an abort: "an assertion that
     * counts is not an assertion that identifies" (Step 19). */
    fixture f;
    fx_init(&f);

    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 2u);
    cq_bit *b = cq_reg_bits(&f.ctx.regs, h);
    b[0] = fresh_q(&f);                    /* q0, |0> */
    b[1] = fresh_q(&f);                    /* q1      */
    cq_emit_x(&f.ctx, &b[1]);              /* ... |1> */
    cq_bit one = cq_bit_one();
    cq_mock_reset(&f.mock);

    uint64_t lo = 0u, hi = 0u;
    cq_ctrl_push(&f.ctx, &one);
    cq_measure(&f.ctx, h, &lo, &hi);
    cq_ctrl_pop(&f.ctx);

    const cq_rec want[] = { CQ_REC_MZ(0u), CQ_REC_MZ(1u) };
    pin_stream(&f, want, 2u, "measure under a ONE control");
    CHECK_EQ((int)lo, 2);
    CHECK_EQ((int)hi, 0);
    fx_dispose(&f);
}

/* --- The shared ancilla, and the nested AND. ------------------------------ */

CQ_TEST(the_shared_ancilla_is_one_wire_across_the_whole_region)
{
    fixture f;
    fx_init(&f);

    cq_bit t  = fresh_q(&f);      /* q0 */
    cq_bit c1 = fresh_q(&f);      /* q1 */
    cq_bit c2 = fresh_q(&f);      /* q2 */
    cq_bit u  = fresh_q(&f);      /* q3 */
    cq_bit w  = fresh_q(&f);      /* q4 */
    cq_mock_reset(&f.mock);

    const uint32_t live = cq_qubits_live(&f.ctx.pool);

    cq_ctrl_push(&f.ctx, &w);
    cq_emit_ccx(&f.ctx, &c1, &c2, &t);
    cq_emit_ccx(&f.ctx, &c2, &u,  &t);
    cq_emit_ccx(&f.ctx, &c1, &u,  &t);
    cq_ctrl_pop(&f.ctx);

    /* THREE promoted Toffolis, NINE gates, and exactly ONE new qubit — §9's
     * "one reusable ancilla shared across the whole promoted region". A
     * per-gate ancilla would mint three, and the LIFO free list would hand back
     * the same index each time, so a MINTED count is the only figure that can
     * tell the two apart. */
    CHECK_EQ((int)cq_mock_count(&f.mock), 9);
    CHECK_EQ((int)cq_qubits_minted(&f.ctx.pool), 6);
    CHECK_EQ((int)cq_qubits_live(&f.ctx.pool), (int)live);
    CHECK(cq_qubits_is_free(&f.ctx.pool, 5u));

    /* Every block used index 5, and each is its own palindrome. */
    for (size_t i = 0; i < 9u; i += 3u) {
        const cq_rec *g = cq_mock_at(&f.mock, i);
        CHECK_EQ((int)g->t, 5);
        CHECK(memcmp(g, cq_mock_at(&f.mock, i + 2u), sizeof *g) == 0);
    }
    fx_dispose(&f);
}

CQ_TEST(nested_control_ands_into_one_wire_and_uncomputes_it)
{
    {
        fixture f;
        fx_init(&f);
        cq_bit t  = fresh_q(&f);     /* q0 */
        cq_bit w1 = fresh_q(&f);     /* q1 */
        cq_bit w2 = fresh_q(&f);     /* q2 */
        cq_mock_reset(&f.mock);
        const uint32_t live = cq_qubits_live(&f.ctx.pool);

        cq_ctrl_push(&f.ctx, &w1);
        cq_ctrl_push(&f.ctx, &w2);
        CHECK_EQ(cq_ctrl_depth(&f.ctx), 2);
        cq_emit_x(&f.ctx, &t);
        cq_ctrl_pop(&f.ctx);
        cq_ctrl_pop(&f.ctx);

        /* ONE control wire reaches the promotion, never two — the AND is
         * computed into q3 with one Toffoli, the gate promotes against q3, and
         * the identical Toffoli takes q3 back to |0⟩. */
        const cq_rec want[] = {
            CQ_REC_CCX(1u, 2u, 3u),
            CQ_REC_CX(3u, 0u),
            CQ_REC_CCX(1u, 2u, 3u)
        };
        pin_stream(&f, want, 3u, "nested AND");
        CHECK_EQ((int)cq_qubits_live(&f.ctx.pool), (int)live);
        CHECK(cq_qubits_is_free(&f.ctx.pool, 3u));
        CHECK_EQ(cq_ctrl_depth(&f.ctx), 0);
        fx_dispose(&f);
    }

    {
        /* PUSHING THE SAME WIRE TWICE IS IDEMPOTENT, and it has to be: `q & q`
         * is `q`, and `if (q) { if (q) … }` is an ordinary program. Emitting
         * CCX(q, q, anc) instead would be a Toffoli with two coincident
         * controls — a malformed gate that Release would push at the sink. */
        fixture f;
        fx_init(&f);
        cq_bit t = fresh_q(&f);      /* q0 */
        cq_bit w = fresh_q(&f);      /* q1 */
        cq_mock_reset(&f.mock);

        cq_ctrl_push(&f.ctx, &w);
        cq_ctrl_push(&f.ctx, &w);
        cq_emit_x(&f.ctx, &t);
        cq_ctrl_pop(&f.ctx);
        cq_ctrl_pop(&f.ctx);

        const cq_rec want[] = { CQ_REC_CX(1u, 0u) };
        pin_stream(&f, want, 1u, "idempotent push");
        CHECK_EQ((int)cq_qubits_minted(&f.ctx.pool), 2);   /* no AND flag */
        fx_dispose(&f);
    }
}

CQ_TEST(the_frame_stack_survives_its_own_growth)
{
    /* SIX LEVELS, BECAUSE THE STACK GROWS AT FOUR. `push_slot` starts the frame
     * array at capacity 4 and doubles, so the FIFTH simultaneous push is the
     * first that reallocs — and until this case existed, nothing in the tree
     * reached depth 3, so the doubling arm was never executed at all. A mutant
     * of a line that never runs cannot be killed, which is exactly how the
     * use-after-free this case now guards against survived a 63-mutant battery:
     * `cq_ctrl_push` read the enclosing frame through a pointer taken BEFORE
     * push_slot, and from the fifth push on that pointer was into freed memory.
     *
     * Six rather than five so the case keeps testing the growth path if that
     * initial 4 ever changes. It is worth running under the full-sanitizer
     * configuration CLAUDE.md's Build & Test section names — the default Debug
     * build here is UBSan-only (bd 6wg) and a use-after-free is invisible to it.
     *
     * CQ_lang cannot express nesting at all (one i32 flag per template call, no
     * bracket), so this depth is reachable only from inside the library. PRD §9
     * asks for nesting anyway, and code that exists is code that gets tested. */
    enum { DEPTH = 6 };

    fixture f;
    fx_init(&f);

    cq_bit t = fresh_q(&f);                       /* q0 */
    cq_bit w[DEPTH];
    for (int i = 0; i < DEPTH; i++) w[i] = fresh_q(&f);   /* q1 .. q6 */
    cq_mock_reset(&f.mock);

    const uint32_t live = cq_qubits_live(&f.ctx.pool);

    for (int i = 0; i < DEPTH; i++) {
        cq_ctrl_push(&f.ctx, &w[i]);
        CHECK_EQ(cq_ctrl_depth(&f.ctx), i + 1);
    }

    /* Each nesting level past the first ANDs into a fresh flag with one
     * Toffoli, so the region has emitted DEPTH-1 of them before the gate. */
    CHECK_EQ((int)cq_mock_count(&f.mock), DEPTH - 1);

    cq_emit_x(&f.ctx, &t);

    /* And the gate promoted against the LAST flag, not against any earlier
     * wire — one CX from the innermost AND. */
    {
        const cq_rec *g = cq_mock_at(&f.mock, (size_t)(DEPTH - 1));
        CHECK_EQ((int)g->op, (int)CQ_OP_CX);
        CHECK_EQ((int)g->t, 0);
        CHECK_EQ((int)g->a, (int)cq_qubits_minted(&f.ctx.pool) - 1);
    }

    for (int i = 0; i < DEPTH; i++) cq_ctrl_pop(&f.ctx);

    CHECK_EQ(cq_ctrl_depth(&f.ctx), 0);
    CHECK_EQ((int)cq_qubits_live(&f.ctx.pool), (int)live);

    /* The whole region is a palindrome about its single promoted gate: the
     * DEPTH-1 AND Toffolis on the way in, and the same DEPTH-1 on the way out.
     * That is the same structural claim cq_sandwich rests on, one layer up. */
    CHECK(cq_mock_is_palindrome(&f.mock, (size_t)(DEPTH - 1), 1u));
    fx_dispose(&f);
}

CQ_TEST(row_0_composes_down_the_stack)
{
    /* Every pair of nested controls, as a table of the gate count the inner
     * region emits for a single `cq_emit_x` on a qubit target.
     *
     *   0  the region is skipped — a ZERO anywhere in the stack wins
     *   1  either uncontrolled (ONE over ONE) or promoted against ONE wire,
     *      which is what a ONE control inheriting an outer wire means
     *   3  the only row that needs an AND: two DISTINCT quantum wires, so the
     *      Toffoli that conjoins them and the twin that uncomputes it bracket
     *      the promoted gate */
    /* THE STREAM, NOT THE COUNT — and the difference is a whole row. A ONE
     * control pushed inside a quantum region must INHERIT it, and dropping that
     * inheritance emits a bare `X(q0)` where the answer is `CX(q1, q0)`: one
     * gate either way, no qubit minted either way, so a cardinality check is
     * blind to a region that has quietly stopped controlling anything. `t` is
     * q0 in every row and the single quantum wire is q1, because the classical
     * kinds mint nothing. */
    static const struct {
        okind outer, inner; int gates; cq_op op; uint32_t a;
    } rows[] = {
        { K_ZERO, K_ZERO, 0, CQ_OP_X,  0u }, { K_ZERO, K_ONE, 0, CQ_OP_X,  0u },
        { K_ZERO, K_Q1,   0, CQ_OP_X,  0u }, { K_ONE,  K_ZERO, 0, CQ_OP_X, 0u },
        { K_ONE,  K_ONE,  1, CQ_OP_X,  0u },   /* genuinely uncontrolled  */
        { K_ONE,  K_Q1,   1, CQ_OP_CX, 1u },   /* promoted on the inner   */
        { K_Q1,   K_ZERO, 0, CQ_OP_X,  0u },
        { K_Q1,   K_ONE,  1, CQ_OP_CX, 1u },   /* THE DISCRIMINATING ROW  */
        { K_Q1,   K_Q1,   3, CQ_OP_CCX, 1u }   /* two wires -> the AND    */
    };

    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        fixture f;
        fx_init(&f);
        cq_bit t  = fresh_q(&f);
        cq_bit wo = fx_operand(&f, rows[i].outer);
        cq_bit wi = fx_operand(&f, rows[i].inner);
        cq_mock_reset(&f.mock);
        const uint32_t live = cq_qubits_live(&f.ctx.pool);

        cq_ctrl_push(&f.ctx, &wo);
        cq_ctrl_push(&f.ctx, &wi);
        cq_emit_x(&f.ctx, &t);
        cq_ctrl_pop(&f.ctx);
        cq_ctrl_pop(&f.ctx);

        if ((int)cq_mock_count(&f.mock) != rows[i].gates) {
            cq_h_fail(__FILE__, __LINE__,
                      "row 0: outer %s / inner %s emitted %zu gates, want %d",
                      kind_name(rows[i].outer), kind_name(rows[i].inner),
                      cq_mock_count(&f.mock), rows[i].gates);
        } else if (rows[i].gates > 0) {
            /* The FIRST gate identifies the row: X means nothing is
             * controlling, CX means one wire is, CCX means the AND fired. */
            const cq_rec *g = cq_mock_at(&f.mock, 0);

            if (g->op != rows[i].op || g->a != rows[i].a)
                cq_h_fail(__FILE__, __LINE__,
                          "row 0: outer %s / inner %s emitted op %d from q%u, "
                          "want op %d from q%u — a ONE control INHERITS an "
                          "enclosing quantum region and must not reset it",
                          kind_name(rows[i].outer), kind_name(rows[i].inner),
                          (int)g->op, g->a, (int)rows[i].op, rows[i].a);
        }

        /* AN OUTER SKIP SWALLOWS AN INNER QUANTUM CONTROL, including its
         * ancilla: a region that does not run cannot contain one that does, so
         * the ZERO rows must not mint an AND flag either. */
        CHECK_EQ((int)cq_qubits_live(&f.ctx.pool), (int)live);
        fx_dispose(&f);
    }
}

/* --- §7's two general rows under §9. -------------------------------------- */

CQ_TEST(the_two_rotation_rows_promote_exactly)
{
    const double th = 0.7;

    /* Uncontrolled: one gate each, which is what M22 emitted before Step 20 —
     * pinned here so that the promotion cannot quietly become unconditional. */
    {
        fixture f;
        fx_init(&f);
        int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 1u);
        cq_bit *b = cq_reg_bits(&f.ctx.regs, h);
        *b = fresh_q(&f);                               /* q0 */
        cq_mock_reset(&f.mock);

        cq_rotate_ry(&f.ctx, h, th);
        cq_rotate_rz(&f.ctx, h, th);

        const cq_rec want[] = { CQ_REC_RY(0u, 0.7), CQ_REC_RZ(0u, 0.7) };
        pin_stream(&f, want, 2u, "uncontrolled rotations");
        fx_dispose(&f);
    }

    /* Controlled: PRD §9's exact identity, `R(θ/2); CX; R(−θ/2); CX`. The
     * angles are compared BITWISE by mock_sink, so a sign slip or a `θ/2`
     * spelled `θ * 0.5` differently from the module is a failure, not a match. */
    {
        fixture f;
        fx_init(&f);
        int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 1u);
        cq_bit *b = cq_reg_bits(&f.ctx.regs, h);
        *b = fresh_q(&f);                               /* q0 */
        cq_bit w = fresh_q(&f);                         /* q1 */
        cq_mock_reset(&f.mock);

        cq_ctrl_push(&f.ctx, &w);
        cq_rotate_ry(&f.ctx, h, th);
        cq_ctrl_pop(&f.ctx);

        const cq_rec want[] = {
            CQ_REC_RY(0u,  0.7 * 0.5), CQ_REC_CX(1u, 0u),
            CQ_REC_RY(0u, -(0.7 * 0.5)), CQ_REC_CX(1u, 0u)
        };
        pin_stream(&f, want, 4u, "controlled ry");

        /* D12 still holds and still costs: a general Ry poisons its target. */
        CHECK(cq_shadow_get(&f.ctx.shadow, 0u).unknown == 1u);
        fx_dispose(&f);
    }

    {
        fixture f;
        fx_init(&f);
        int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 1u);
        cq_bit *b = cq_reg_bits(&f.ctx.regs, h);
        *b = fresh_q(&f);                               /* q0 */
        cq_bit w = fresh_q(&f);                         /* q1 */
        cq_mock_reset(&f.mock);

        cq_ctrl_push(&f.ctx, &w);
        cq_rotate_rz(&f.ctx, h, th);
        cq_ctrl_pop(&f.ctx);

        const cq_rec want[] = {
            CQ_REC_RZ(0u,  0.7 * 0.5), CQ_REC_CX(1u, 0u),
            CQ_REC_RZ(0u, -(0.7 * 0.5)), CQ_REC_CX(1u, 0u)
        };
        pin_stream(&f, want, 4u, "controlled rz");

        /* D12'S MEASURED PAYOFF SURVIVES THE AXIS, and this is the assertion
         * that says so: controlled-Rz is diagonal exactly as Rz is, so the
         * four-gate block moves no computational-basis value and the target's
         * shadow must stay DETERMINATE. Letting the two CXs update the shadow
         * individually would poison it from the control wire and take the
         * corpus's twelve rz-rooted rails out of Rule 6's reach. */
        CHECK(cq_shadow_get(&f.ctx.shadow, 0u).unknown == 0u);
        CHECK(cq_shadow_get(&f.ctx.shadow, 0u).value == 0u);
        fx_dispose(&f);
    }
}

CQ_TEST(an_uncontrolled_rotation_emits_its_angle_bitwise)
{
    /* §9's promotion needs θ/2, and the tempting shape is to hand the helper
     * `half` and let the no-region branch reconstruct `half + half`. That is
     * exact for every NORMAL double — halving is an exponent decrement, `y + y`
     * is exact — and WRONG in the subnormal range, where the halving rounds:
     * 3·DBL_TRUE_MIN round-trips to 4·DBL_TRUE_MIN. Angles are compared BITWISE
     * throughout this project (mock_sink.h), so that is an `ry` the caller did
     * not ask for.
     *
     * IT IS REACHABLE. A subnormal θ folds to CQ_ANGLE_IDENTITY at any ordinary
     * tolerance, but `tol = 0` is a legal setting that angle.h documents, and
     * there the residual test `|θ − 0| <= 0` fails and θ takes §7's GENERAL row.
     *
     * The suite's other rotation cases all use ordinary angles, so this is the
     * only case that can see it. */
    const double saved = cq_angle_tolerance();
    const double sub   = 0x1.8p-1073;        /* 3 * DBL_TRUE_MIN, odd mantissa */

    fixture f;
    fx_init(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 1u);
    cq_bit *b = cq_reg_bits(&f.ctx.regs, h);
    *b = fresh_q(&f);                        /* q0 */
    cq_mock_reset(&f.mock);

    cq_angle_set_tolerance(0.0);
    CHECK_EQ((int)cq_angle_ry_row(sub), (int)CQ_ANGLE_GENERAL);
    cq_rotate_ry(&f.ctx, h, sub);
    cq_angle_set_tolerance(saved);

    /* CQ_REC_RY compares the angle with memcmp, so `half + half` is a failure
     * rather than a match. */
    const cq_rec want[] = { CQ_REC_RY(0u, 0x1.8p-1073) };
    pin_stream(&f, want, 1u, "subnormal ry, uncontrolled");
    fx_dispose(&f);
}

/* --- A whole sandwich kernel under the axis. ------------------------------ */

/* K6 is the smallest kernel with a real compute half, and at W = 4 the three
 * cases below can pin its structure by hand. Its `dst` is minted fresh and
 * all-ZERO exactly as the shared driver does it. */

CQ_TEST(a_skipped_region_costs_a_sandwich_kernel_nothing)
{
    /* PRD §9 row 0 says "0 gates, 0 QUBITS", and the second half is the one
     * that needs cq_sandwich's own short-circuit: the driver pre-materialises
     * its whole scratch region (I6(b)) before any gate is emitted, so an M06
     * that only silenced cq_emit_* would still take W(3W) qubits for a region
     * that does not run. */
    const int W = 4;
    fixture f;
    fx_init(&f);

    int32_t ha = cq_bk_reg(&f.ctx, (uint32_t)W, 5u, ~0ull);
    int32_t hb = cq_bk_reg(&f.ctx, (uint32_t)W, 3u, ~0ull);
    int32_t hd = cq_reg_alloc_zero(&f.ctx.regs, (uint32_t)W);
    cq_bit  z  = cq_bit_zero();
    cq_mock_reset(&f.mock);

    const uint32_t minted = cq_qubits_minted(&f.ctx.pool);

    cq_ctrl_push(&f.ctx, &z);
    cq_kernel_add(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
                  cq_reg_cbits(&f.ctx.regs, ha),
                  cq_reg_cbits(&f.ctx.regs, hb), W);
    cq_ctrl_pop(&f.ctx);

    CHECK_EQ((int)cq_mock_count(&f.mock), 0);
    CHECK_EQ((int)(cq_qubits_minted(&f.ctx.pool) - minted), 0);
    CHECK_EQ((int)cq_pc_value(&f.ctx, hd), 0);
    fx_dispose(&f);
}

CQ_TEST(a_controlled_sandwich_still_cancels_and_stays_a_palindrome)
{
    /* The composition claim, and the only one that reaches every mechanism at
     * once: I6's extent, the region fingerprint, CQ_ZERO_BY_PALINDROME, and
     * §9's promotion in the same call.
     *
     * The head length is DERIVED from the measured stream rather than written
     * down, because under promotion a step is one gate or three and there is no
     * closed form. What is known independently is the middle: copyout is one
     * `cq_emit_cx(scratch, dst)` per bit, and each promotes to exactly one
     * Toffoli — scratch is a wire, so no fold applies, and `dst` is born ZERO,
     * so materialising it emits nothing. */
    const int W = 4;
    fixture f;
    fx_init(&f);

    int32_t ha = cq_bk_reg(&f.ctx, (uint32_t)W, 5u, ~0ull);
    int32_t hb = cq_bk_reg(&f.ctx, (uint32_t)W, 3u, ~0ull);
    int32_t hd = cq_reg_alloc_zero(&f.ctx.regs, (uint32_t)W);
    cq_bit  w  = fresh_q(&f);
    cq_mock_reset(&f.mock);

    const uint32_t live = cq_qubits_live(&f.ctx.pool);

    cq_ctrl_push(&f.ctx, &w);
    cq_kernel_add(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
                  cq_reg_cbits(&f.ctx.regs, ha),
                  cq_reg_cbits(&f.ctx.regs, hb), W);
    cq_ctrl_pop(&f.ctx);

    const size_t total = cq_mock_count(&f.mock);
    const size_t n_mid = (size_t)W;

    CHECK(total > n_mid);
    CHECK((total - n_mid) % 2u == 0u);

    const size_t n_head = (total - n_mid) / 2u;
    CHECK(n_head > 0u);
    CHECK(cq_mock_is_palindrome(&f.mock, n_head, n_mid));

    /* The scratch region went back, and so did the promotion's ancilla — the
     * only indices still live are the two operands' and dst's W. */
    CHECK_EQ((int)(cq_qubits_live(&f.ctx.pool) - live), W);

    /* And the value is right at ctrl = 0, which is the branch that can see a
     * region that ran when it should not have. */
    CHECK_EQ((int)cq_pc_value(&f.ctx, hd), 0);
    fx_dispose(&f);
}

CQ_TEST(the_promoted_tuple_is_the_uncontrolled_one_transformed)
{
    /* §9's promotion maps the by-kind tuple exactly: X→CX, CX→CCX, CCX→3 CCX,
     * so `(x, cx, ccx)` becomes `(0, x, cx + 3·ccx)` — at the ALL-QUANTUM
     * operand mask, and there only. Away from it a gate that folded to nothing
     * uncontrolled can emit under promotion (a constant target is the case), so
     * the identity is scoped to the mask every L4 golden is pinned at.
     *
     * IT READS NO GOLDEN, and that is the point: it compares two measurements
     * taken in the same run, so `CQOPS_UPDATE_GOLDENS=1` cannot bless a
     * promotion that quietly stopped promoting. */
    const int W = 4;
    uint64_t u_x = 0, u_cx = 0, u_ccx = 0;

    for (int controlled = 0; controlled <= 1; controlled++) {
        fixture f;
        fx_init(&f);
        int32_t ha = cq_bk_reg(&f.ctx, (uint32_t)W, 5u, ~0ull);
        int32_t hb = cq_bk_reg(&f.ctx, (uint32_t)W, 3u, ~0ull);
        int32_t hd = cq_reg_alloc_zero(&f.ctx.regs, (uint32_t)W);
        cq_bit  w  = fresh_q(&f);
        cq_mock_reset(&f.mock);

        if (controlled) cq_ctrl_push(&f.ctx, &w);
        cq_kernel_add(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
                      cq_reg_cbits(&f.ctx.regs, ha),
                      cq_reg_cbits(&f.ctx.regs, hb), W);
        if (controlled) cq_ctrl_pop(&f.ctx);

        const uint64_t x   = cq_mock_count_op(&f.mock, CQ_OP_X);
        const uint64_t cx  = cq_mock_count_op(&f.mock, CQ_OP_CX);
        const uint64_t ccx = cq_mock_count_op(&f.mock, CQ_OP_CCX);

        if (!controlled) { u_x = x; u_cx = cx; u_ccx = ccx; }
        else {
            CHECK(u_x + u_cx + u_ccx > 0u);      /* not vacuous */
            CHECK_EQ((int)x,   0);
            CHECK_EQ((int)cx,  (int)u_x);
            CHECK_EQ((int)ccx, (int)(u_cx + 3u * u_ccx));
        }
        fx_dispose(&f);
    }
}
