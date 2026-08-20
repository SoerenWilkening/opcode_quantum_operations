/* tests/test_rotate_cells.inc — §7's table swept cell by cell, and the per-bit
 * dispatch that decides which column each bit takes.
 *
 * SPLIT FROM tests/test_rotate.c AT STEP 19, on the second of the two seams
 * that file records. The first (`--- measurement ---`) went to
 * test_rotate_measure.inc; this one is `the table itself ↔ the claims ABOUT the
 * table`. What lives here sweeps every row against every column and asserts
 * counts, kinds and the pool; what stays behind asserts the things a sweep
 * cannot see — the ORDER of the half-turn pair (bd lk0), which rows poison
 * (PRD §15 D12), and that the module consults its own tolerance.
 *
 * The two files share test_rotate_table.inc, which is the specification both
 * are written against.
 */

/* --- §7's table, both columns -------------------------------------------- */

/* The CONSTANT column of every row, at three widths and two values.
 *
 * The assertion that matters most here is not the value — it is
 * `cq_reg_owned_qubits == 0` on every row but general Ry. That is M22's L5, it
 * is invariant I4 for the rail, and it is what a `cq_materialise` hoisted above
 * the row dispatch would break while leaving every value and every gate count
 * of the surviving rows correct. */
CQ_TEST(the_constant_column_of_every_prd7_row)
{
    static const uint32_t WS[] = { 1, 5, 8 };

    for (int c = 0; c < RT_N_CELLS; c++) {
        const rt_cell *cell = &RT_CELLS[c];

        for (int wi = 0; wi < 3; wi++) {
            const uint32_t W = WS[wi];
            const uint64_t all = (W == 64u) ? ~0ull : (((uint64_t)1 << W) - 1u);

            for (int vi = 0; vi < 2; vi++) {
                const uint64_t v = vi ? all : 0u;
                fixture f; fx_open(&f);

                int32_t h = cq_bk_reg(&f.ctx, W, v, 0u);   /* mask 0: all classical */
                CHECK_EQ(rt_quantum_bits(&f.ctx, h), 0u);
                cq_mock_reset(&f.mock);
                const uint32_t live_before = cq_pc_take(&f.ctx).live;

                rt_apply_reg(&f.ctx, cell, h);

                if (cell->c_materialise) {
                    /* Ry general is the ONE row that leaves the constant column:
                     * W fresh qubits, one `ry` each, and an `X` only for the
                     * bits whose constant was 1. */
                    CHECK_EQ(rt_quantum_bits(&f.ctx, h), W);
                    CHECK_EQ(cq_pc_take(&f.ctx).live, live_before + W);
                    CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RY), W);
                    CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RZ), 0u);
                    CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_X), vi ? W : 0u);
                    CHECK_EQ(rt_poisoned_bits(&f.ctx, h), W);
                } else {
                    /* Every other row: zero gates, zero qubits, I4 intact. */
                    CHECK_EQ(rt_quantum_bits(&f.ctx, h), 0u);
                    CHECK_EQ(cq_pc_take(&f.ctx).live, live_before);
                    CHECK_EQ(cq_mock_count(&f.mock), 0u);
                    CHECK_EQ(rt_value(&f.ctx, h), cell->c_flip ? (v ^ all) : v);
                }

                fx_close(&f);
            }
        }
    }
}

/* The QUBIT column of every row: the per-bit gate tuple, and the shadow.
 *
 * `cq_bk_reg` materialises through the sanctioned route, so every bit arrives
 * CQ_BIT_Q with a determinate shadow — which is exactly the state D12's claim
 * is about, and the only state in which "the shadow is still determinate
 * afterwards" says anything. */
CQ_TEST(the_qubit_column_of_every_prd7_row)
{
    static const uint32_t WS[] = { 1, 3, 8 };

    for (int c = 0; c < RT_N_CELLS; c++) {
        const rt_cell *cell = &RT_CELLS[c];

        for (int wi = 0; wi < 3; wi++) {
            const uint32_t W = WS[wi];
            const uint64_t all = (((uint64_t)1 << W) - 1u);

            for (int vi = 0; vi < 2; vi++) {
                const uint64_t v = vi ? all : 0u;
                fixture f; fx_open(&f);

                int32_t h = cq_bk_reg(&f.ctx, W, v, all);  /* every bit quantum */
                CHECK_EQ(rt_quantum_bits(&f.ctx, h), W);
                cq_mock_reset(&f.mock);
                const uint32_t live_before = cq_pc_take(&f.ctx).live;

                rt_apply_reg(&f.ctx, cell, h);

                /* A qubit column never allocates: the bit is already a qubit. */
                CHECK_EQ(rt_quantum_bits(&f.ctx, h), W);
                CHECK_EQ(cq_pc_take(&f.ctx).live, live_before);

                CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_X),  cell->q_x  * W);
                CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RY), cell->q_ry * W);
                CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RZ), cell->q_rz * W);
                CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_MZ), 0u);
                CHECK_EQ(cq_mock_count(&f.mock),
                         (size_t)(cell->q_x + cell->q_ry + cell->q_rz) * W);

                /* PRD §15 D12, stated as an equality rather than as a bound so
                 * that poisoning too MUCH is caught as well as too little. */
                CHECK_EQ(rt_poisoned_bits(&f.ctx, h), cell->poisons ? W : 0u);

                if (!cell->poisons)
                    CHECK_EQ(rt_value(&f.ctx, h), cell->c_flip ? (v ^ all) : v);

                fx_close(&f);
            }
        }
    }
}

/* --- The per-bit dispatch ------------------------------------------------ */

/* THE COLUMN IS CHOSEN PER BIT, NOT ONCE PER REGISTER, and only a MIXED mask
 * can see it. Deciding from bit 0 and applying that answer to the whole rail is
 * invisible on an all-constant rail and invisible on an all-quantum rail — the
 * two shapes every other case here uses — and it is the K9/K10 "invisible to
 * everything but the mask sweep" failure in its M22 form.
 *
 * The rail is half quantum, half constant, and each half must take its own
 * column: the quantum bits get `X; rz(π)` and the constant bits flip for
 * nothing. */
CQ_TEST(the_column_is_chosen_per_bit_not_per_register)
{
    static const uint32_t WS[] = { 2, 3, 4, 8 };

    for (int wi = 0; wi < 4; wi++) {
        const uint32_t W = WS[wi];
        const uint64_t all = (((uint64_t)1 << W) - 1u);

        for (uint64_t mask = 0; mask <= all; mask++) {
            const uint32_t nq = rt_popcount(mask);
            fixture f; fx_open(&f);

            int32_t h = cq_bk_reg(&f.ctx, W, 0u, mask);
            CHECK_EQ(rt_quantum_bits(&f.ctx, h), nq);
            cq_mock_reset(&f.mock);
            const uint32_t live_before = cq_pc_take(&f.ctx).live;

            cq_rotate_ry(&f.ctx, h, RT_PI);

            /* One X and one rz per QUANTUM bit; the classical bits cost
             * nothing and stay classical. */
            CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_X),  nq);
            CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RZ), nq);
            CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RY), 0u);
            CHECK_EQ(rt_quantum_bits(&f.ctx, h), nq);
            CHECK_EQ(cq_pc_take(&f.ctx).live, live_before);

            /* Every bit flipped, whichever column it took. */
            CHECK_EQ(rt_value(&f.ctx, h), all);
            CHECK_EQ(rt_poisoned_bits(&f.ctx, h), 0u);

            fx_close(&f);
        }
    }
}

/* A general Ry materialises each constant bit EXACTLY ONCE, and the `X` for a
 * constant ONE lands BEFORE that bit's `ry`. Materialising twice would leak the
 * first qubit; materialising after the rotation would rotate the wrong wire. */
CQ_TEST(a_general_ry_on_a_constant_materialises_once_then_rotates)
{
    fixture f; fx_open(&f);

    const uint32_t W = 3;
    int32_t h = cq_bk_reg(&f.ctx, W, 0x5u, 0u);       /* bits 0 and 2 are ONE */
    cq_mock_reset(&f.mock);
    const cq_pc_snap before = cq_pc_take(&f.ctx);

    cq_rotate_ry(&f.ctx, h, 0.5);

    const cq_bit *b = cq_reg_cbits(&f.ctx.regs, h);
    const cq_rec want[] = {
        CQ_REC_X(cq_bit_qindex(b[0])), CQ_REC_RY(cq_bit_qindex(b[0]), 0.5),
                                       CQ_REC_RY(cq_bit_qindex(b[1]), 0.5),
        CQ_REC_X(cq_bit_qindex(b[2])), CQ_REC_RY(cq_bit_qindex(b[2]), 0.5),
    };
    if (!cq_mock_matches(&f.mock, want, 5)) {
        cq_h_fail(__FILE__, __LINE__, "general Ry on constants: wrong stream");
        cq_mock_dump(&f.mock, "general ry");
    }
    CHECK_EQ(cq_pc_take(&f.ctx).live, before.live + W);
    CHECK_EQ(rt_quantum_bits(&f.ctx, h), W);

    /* The three indices are distinct — one materialisation each, not one qubit
     * written three times, which no gate count could tell apart. */
    CHECK(cq_bit_qindex(b[0]) != cq_bit_qindex(b[1]));
    CHECK(cq_bit_qindex(b[1]) != cq_bit_qindex(b[2]));
    CHECK(cq_bit_qindex(b[0]) != cq_bit_qindex(b[2]));

    fx_close(&f);
}

/* --- Width generality ---------------------------------------------------- */

/* Width-generic over reg->width with no width switch (Rule 3), up to the
 * widest register the ABI can name. i80 and i128 rails cannot reach a rotation
 * through `cqrt_ry_i<W>` — the census stops that family at i64 — but M22 is
 * handed a `cq_bit *` and a width, and a module with a hidden 64-bit ceiling
 * would be an I5 violation waiting for a caller. */
CQ_TEST(the_rows_are_width_generic_to_the_widest_register)
{
    static const uint32_t WS[] = { 1, 2, 63, 64, 65, 80, 128 };

    for (int wi = 0; wi < 7; wi++) {
        const uint32_t W = WS[wi];
        fixture f; fx_open(&f);

        int32_t h = cq_reg_alloc_zero(&f.ctx.regs, W);
        cq_bit *bits = cq_reg_bits(&f.ctx.regs, h);
        for (uint32_t i = 0; i < W; i++) cq_materialise(&f.ctx, &bits[i]);
        cq_mock_reset(&f.mock);

        cq_rotate_ry(&f.ctx, h, RT_PI);
        CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_X),  W);
        CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RZ), W);

        cq_mock_reset(&f.mock);
        cq_rotate_rz(&f.ctx, h, 0.5);
        CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RZ), W);

        CHECK_EQ(rt_poisoned_bits(&f.ctx, h), 0u);
        fx_close(&f);
    }
}

/* The register entry point IS the per-bit one in a loop, gate for gate and on
 * the same qubits — asserted structurally rather than assumed.
 *
 * Both contexts are built identically and the qubit indices therefore match,
 * which is what lets the two streams be compared as sequences rather than as
 * counts. The mutation this catches is a register form that dispatches once and
 * then does something subtly different per bit — the same shape as M16's
 * `each_derived_predicate_is_its_primitives_stream`, which is the only detector
 * for a whole class of silent divergence. */
CQ_TEST(the_register_form_is_the_per_bit_form_in_a_loop)
{
    for (int c = 0; c < RT_N_CELLS; c++) {
        const rt_cell *cell = &RT_CELLS[c];
        const uint32_t W = 3;

        fixture a; fx_open(&a);
        int32_t ha = cq_bk_reg(&a.ctx, W, 0x5u, 0x3u);   /* mixed on purpose */
        cq_mock_reset(&a.mock);
        rt_apply_reg(&a.ctx, cell, ha);

        fixture b; fx_open(&b);
        int32_t hb = cq_bk_reg(&b.ctx, W, 0x5u, 0x3u);
        cq_mock_reset(&b.mock);
        for (uint32_t i = 0; i < W; i++)
            rt_apply_bit(&b.ctx, cell, &cq_reg_bits(&b.ctx.regs, hb)[i]);

        if (!cq_mock_matches(&a.mock, b.mock.v, b.mock.n)) {
            cq_h_fail(__FILE__, __LINE__,
                      "%s: the register form and the per-bit loop diverge",
                      cell->name);
            cq_mock_dump(&a.mock, "register form");
            cq_mock_dump(&b.mock, "per-bit loop");
        }
        CHECK_EQ(rt_value(&a.ctx, ha), rt_value(&b.ctx, hb));
        CHECK_EQ(rt_poisoned_bits(&a.ctx, ha), rt_poisoned_bits(&b.ctx, hb));

        fx_close(&b);
        fx_close(&a);
    }
}

