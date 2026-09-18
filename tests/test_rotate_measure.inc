/* tests/test_rotate_measure.inc — PRD §7's Measurement subsection.
 *
 * SPLIT FROM tests/test_rotate.c AT STEP 19, on the seam that file recorded
 * before it was needed (Rule 12). Measurement is the only part of M22 that
 * touches cq_reg_mark_measured, the only part with a return value, the only
 * part that is terminal and the only part that never calls M21 — and §7 gives
 * it its own subsection. It is a different subject from the rotation table.
 */

/* --- measurement --------------------------------------------------------- */

/* PRD §7's measurement subsection, all four clauses at once: the shadow value
 * for every bit whose shadow is known, ZERO for every bit whose shadow is not,
 * one `sink.mz` per QUBIT (never per bit — a constant owns no wire to measure),
 * and terminal. */
CQ_TEST(measurement_returns_the_shadow_and_zero_for_unknown)
{
    fixture f; fx_open(&f);

    const uint32_t W = 8;
    /* value 0xB5 = 1011_0101; bits 4..7 quantum, bits 0..3 classical. */
    int32_t h = cq_bk_reg(&f.ctx, W, 0xB5u, 0xF0u);
    cq_mock_reset(&f.mock);

    uint64_t lo = 0xDEADu, hi = 0xBEEFu;
    cq_measure(&f.ctx, h, &lo, &hi);

    CHECK_EQ(lo, 0xB5u);
    CHECK_EQ(hi, 0u);
    CHECK_EQ(cq_reg_state(&f.ctx.regs, h), CQ_SLOT_MEASURED);

    /* THE OPERANDS, ORDERED — not just the count. A count cannot see the most
     * natural transcription slip in the loop, `cq_sink_mz(sink, i)` for
     * `cq_sink_mz(sink, q)`: measuring the bit POSITION instead of the qubit
     * INDEX emits exactly four `mz` and returns exactly the right value,
     * because the value is read from the shadow on OUR side of the vtable and
     * never from the sink. What reaches the QEC driver is four wires the rail
     * does not own, while the four it does own stay live and unmeasured — a
     * silent miscompile of the Prime Directive's own class. Measured: that
     * mutant survived the whole suite before this assertion existed. */
    const cq_bit *b = cq_reg_cbits(&f.ctx.regs, h);
    const cq_rec want[] = {
        CQ_REC_MZ(cq_bit_qindex(b[4])), CQ_REC_MZ(cq_bit_qindex(b[5])),
        CQ_REC_MZ(cq_bit_qindex(b[6])), CQ_REC_MZ(cq_bit_qindex(b[7])),
    };
    if (!cq_mock_matches(&f.mock, want, 4)) {
        cq_h_fail(__FILE__, __LINE__, "measure: wrong mz stream");
        cq_mock_dump(&f.mock, "measure");
    }

    fx_close(&f);
}

/* A POISONED QUBIT IS STILL MEASURED, AND ITS VALUE IS 0 RATHER THAN THE STALE
 * BYTE. Two clauses, and the first one had no assertion at all until a mutation
 * battery asked: `if (!s.unknown) cq_sink_mz(...)` survived the entire suite,
 * because every mz-count assertion ran on a rail whose shadow was determinate.
 * That mutant skips the `mz` on precisely the qubit where the measurement is
 * physically real — the superposed one — so the caller believes it measured
 * while the qubit stays entangled and, under the terminal rule, live forever.
 *
 * THE RAIL IS MIXED ON PURPOSE. Poisoning every bit tests the two branches one
 * at a time; a rail with two poisoned and two determinate qubits requires the
 * emission to be unconditional and the VALUE to be conditional in the same
 * call, which is what §7 actually says. */
CQ_TEST(measurement_of_a_poisoned_bit_is_zero_not_the_stale_value)
{
    fixture f; fx_open(&f);

    const uint32_t W = 4;
    int32_t h = cq_bk_reg(&f.ctx, W, 0xFu, 0xFu);   /* all ones, all quantum */
    cq_bit *bits = cq_reg_bits(&f.ctx.regs, h);

    /* Bits 0 and 2 poisoned, bits 1 and 3 left determinate at 1. */
    cq_rotate_ry_bit(&f.ctx, &bits[0], 0.5);
    cq_rotate_ry_bit(&f.ctx, &bits[2], 0.5);
    CHECK_EQ(rt_poisoned_bits(&f.ctx, h), 2u);
    cq_mock_reset(&f.mock);

    uint64_t lo = 1, hi = 1;
    cq_measure(&f.ctx, h, &lo, &hi);

    /* The poisoned bits' value bytes still HOLD 1 — cq_shadow_rotate sets only
     * `unknown` and freezes the byte — so returning them would be publishing a
     * stale byte as a measurement. PRD §7 says 0 for those, and the real shadow
     * value for the rest: 1010b. */
    CHECK_EQ(lo, 0xAu);
    CHECK_EQ(hi, 0u);

    /* And every qubit is measured, poisoned or not, in bit order. */
    const cq_bit *b = cq_reg_cbits(&f.ctx.regs, h);
    const cq_rec want[] = {
        CQ_REC_MZ(cq_bit_qindex(b[0])), CQ_REC_MZ(cq_bit_qindex(b[1])),
        CQ_REC_MZ(cq_bit_qindex(b[2])), CQ_REC_MZ(cq_bit_qindex(b[3])),
    };
    if (!cq_mock_matches(&f.mock, want, 4)) {
        cq_h_fail(__FILE__, __LINE__, "measure: a poisoned qubit was not measured");
        cq_mock_dump(&f.mock, "measure mixed");
    }
    fx_close(&f);
}

/* Two words, because a register reaches 128 bits (CQ_REG_WIDTH_MAX) and a
 * one-word return would silently truncate at the 64-bit seam — the exact trap
 * I5 exists to prevent, and the reason cq_reg_alloc_const takes two words in.
 * No `cqrt_measure_*` above i64 exists in the frozen ABI, so nothing reaches
 * this through the shim; a library entry point with a hidden ceiling is still
 * a defect. */
CQ_TEST(measurement_spans_both_words_at_the_widest_register)
{
    fixture f; fx_open(&f);

    const uint32_t W = 128;
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, W);
    cq_bit *bits = cq_reg_bits(&f.ctx.regs, h);
    cq_bit_flip_const(&bits[0]);      /* lo bit 0   */
    cq_bit_flip_const(&bits[63]);     /* lo bit 63  */
    cq_bit_flip_const(&bits[64]);     /* hi bit 0   */
    cq_bit_flip_const(&bits[127]);    /* hi bit 63  */
    cq_materialise(&f.ctx, &bits[64]);          /* on a qubit, still known 1 */
    cq_mock_reset(&f.mock);

    uint64_t lo = 0, hi = 0;
    cq_measure(&f.ctx, h, &lo, &hi);

    CHECK_EQ(lo, (uint64_t)1 | ((uint64_t)1 << 63));
    CHECK_EQ(hi, (uint64_t)1 | ((uint64_t)1 << 63));
    CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_MZ), 1u);   /* one qubit only */
    fx_close(&f);
}

/* Measurement is TERMINAL: the qubits are deliberately never reclaimed, because
 * CQ_lang emits no adjoint and no `cqrt_free` for a measured handle. So `live`
 * does NOT return, and that is the intended safe leak rather than a bug. */
CQ_TEST(a_measured_rail_keeps_its_qubits_forever)
{
    fixture f; fx_open(&f);

    int32_t h = cq_bk_reg(&f.ctx, 4, 0xFu, 0xFu);
    const uint32_t live = cq_pc_take(&f.ctx).live;

    uint64_t lo, hi;
    cq_measure(&f.ctx, h, &lo, &hi);

    CHECK_EQ(cq_pc_take(&f.ctx).live, live);
    CHECK_EQ(cq_pc_take(&f.ctx).n_free, 0u);
    CHECK_EQ(rt_quantum_bits(&f.ctx, h), 4u);
    fx_close(&f);
}


/* --- the repeat measure (bd 30k, 2026-09-17) ----------------------------- */

/* THE POSITIVE EXEMPTION CASE for cq_reg_mark_measured's idempotence, and it is
 * a POSITIVE one deliberately: this claim used to be a death case
 * (test_rotate_death.measure_of_a_measured_rail) and a death case that merely
 * STOPPED FIRING would prove nothing at all — it would be satisfied by a
 * cq_measure that returned garbage, emitted the wrong wires, or quietly released
 * the rail's qubits. So the assertion is what the second call DOES.
 *
 * WHY IT IS LEGAL IS THE ABI'S ANSWER AND NOT OURS: CQ_lang's own committed
 * golden tests/e2e/slice_loop_pairing_measure_owned.expected.log emits
 * `cqrt_measure_i32(h1)` twice (lines 11 and 19). src/reg.c carries the full
 * chain, including why it is deliberate upstream rather than tolerated.
 *
 * FOUR CLAUSES, AND THE ORDERED STREAM IS THE ONE THAT CARRIES WEIGHT. A COUNT
 * of `mz` would be satisfied by a second call measuring the wrong wires — the
 * exact mutant the first case in this file was written against — so the second
 * measure's stream is pinned against the SAME qubit indices, in order. The value
 * must also be bit-identical: a repeat projective measurement in the same basis
 * returns the first outcome, and our shadow is not mutated by a measure, so
 * anything else here means someone taught cq_measure to write. */
CQ_TEST(a_second_measure_of_the_same_rail_is_idempotent)
{
    fixture f; fx_open(&f);

    const uint32_t W = 8;
    int32_t h = cq_bk_reg(&f.ctx, W, 0xB5u, 0xF0u);   /* bits 4..7 quantum */
    const cq_bit *b = cq_reg_cbits(&f.ctx.regs, h);
    const cq_rec want[] = {
        CQ_REC_MZ(cq_bit_qindex(b[4])), CQ_REC_MZ(cq_bit_qindex(b[5])),
        CQ_REC_MZ(cq_bit_qindex(b[6])), CQ_REC_MZ(cq_bit_qindex(b[7])),
    };

    cq_mock_reset(&f.mock);
    uint64_t lo1 = 0, hi1 = 0;
    cq_measure(&f.ctx, h, &lo1, &hi1);
    const cq_pc_snap after_first = cq_pc_take(&f.ctx);

    /* (1) It does not abort, and the rail is still MEASURED rather than moved to
     *     some fifth state. */
    cq_mock_reset(&f.mock);
    uint64_t lo2 = 0xDEADu, hi2 = 0xBEEFu;
    cq_measure(&f.ctx, h, &lo2, &hi2);
    CHECK_EQ(cq_reg_state(&f.ctx.regs, h), CQ_SLOT_MEASURED);

    /* (2) The value is the first one, bit for bit. */
    CHECK_EQ(lo2, lo1);
    CHECK_EQ(hi2, hi1);
    CHECK_EQ(lo2, 0xB5u);

    /* (3) The `mz` stream is the SAME WIRES IN THE SAME ORDER — not merely four
     *     of them. Emitting again is the correct answer rather than a missed
     *     peephole: suppressing it would make the gate stream depend on call
     *     history, which is an optimisation v1 does not do (Rule 13). */
    if (!cq_mock_matches(&f.mock, want, 4)) {
        cq_h_fail(__FILE__, __LINE__, "second measure: wrong mz stream");
        cq_mock_dump(&f.mock, "second measure");
    }

    /* (4) THE POOL DID NOT MOVE. A measured rail's qubits are deliberately never
     *     reclaimed, so a second measure must not release, strand or re-mint one
     *     — this is the clause that would catch an idempotence implemented by
     *     re-running the whole lifetime transition. */
    const cq_pc_snap after_second = cq_pc_take(&f.ctx);
    CHECK_EQ(after_second.live,   after_first.live);
    CHECK_EQ(after_second.n_free, after_first.n_free);
    CHECK_EQ(rt_quantum_bits(&f.ctx, h), 4u);

    fx_close(&f);
}
