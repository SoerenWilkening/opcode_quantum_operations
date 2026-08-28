/* tests/test_template_width.inc — the RESULT-WIDTH half of
 * tests/test_template.c, on the second seam IMPLEMENTATION_PLAN §3 records for
 * that file: `the OPERAND-WIDTH shapes <-> the RESULT-WIDTH shapes`.
 *
 * WHAT MAKES THIS A SUBJECT AND NOT A SIZE CUT. The nine binary entry points
 * mint `dst` at the operand width and everything that can go wrong with them is
 * about which LANE carries what. These two families are the ones where the
 * result width is NOT the operand width — `icmp` is ONE BIT at any operand
 * width (`ir_types.jl:79`; `opcode_table.yaml:222-223` gives it
 * `result_type: flag_handle`) and a cast is `to_bits` — which is exactly what
 * the arity-2 signature suggests otherwise, and it is the trap `bd 216`'s
 * checklist item 7 exists for. A `dst` minted at the operand width still
 * compares equal on lane 0, so the VALUE oracle passes and `W-1` qubits leak
 * per compare across a corpus that emits 21,323 of them.
 *
 * `cast` is here because it is the SECOND INSTANCE of that property, not
 * because it is small: it is also the one shape whose kernel takes `(F, T)`
 * rather than a single `W`, which is why it carries its own function-pointer
 * type in shim/cq_template_dispatch.h and why widening `cq_kernel_fn` to reach
 * it is what Rule 7 forbids.
 */

/* -------------------------------------------------------------------------
 * 4. `icmp` — the result width the arity-2 shape does not suggest.
 * ------------------------------------------------------------------------- */

/* ALL TEN PREDICATES AGAINST `cq_ref_icmp`, at operand pairs that separate the
 * signed rows from the unsigned ones. 0xB5 is negative as i8 and larger than
 * 0x03 as u8, so `slt` and `ult` DISAGREE on this pair — which is what makes a
 * transposed row in either enum visible. */
CQ_TEST(every_predicate_dispatches_to_its_own_kernel_and_mints_one_bit)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    /* THREE PAIRS, AND THE EQUAL ONE IS NOT OPTIONAL. Measured: with only
     * (0xB5, 0x03) a mutant putting `cq_kernel_ule` in the `ult` row SURVIVED
     * the whole suite in both configurations — `ult(181,3)` and `ule(181,3)`
     * are both false, and the same holds for every other strict/non-strict
     * pair whenever the operands differ. The row that separates them is a == b,
     * where `lt`/`gt` are 0 and `le`/`ge` are 1. The two ORDERS are what
     * separate `lt` from `gt`, and 0xB5 is negative as i8 while being the
     * LARGER of the two as u8, which is what separates signed from unsigned. */
    static const uint64_t PAIRS[3][2] = {
        { TPL_A, TPL_B }, { TPL_B, TPL_A }, { TPL_A, TPL_A }
    };

    for (size_t p = 0; p < 3u; p++) {
        const cq_ref_w ra = cq_ref_w_make(PAIRS[p][0], 0u, (int)TPL_W);
        const cq_ref_w rb = cq_ref_w_make(PAIRS[p][1], 0u, (int)TPL_W);

        for (size_t i = 0; i < sizeof PRED_MAP / sizeof *PRED_MAP; i++) {
            const int32_t a = qreg(ctx, TPL_W, PAIRS[p][0]);
            const int32_t b = qreg(ctx, TPL_W, PAIRS[p][1]);
            const int32_t f = cq_shim_icmp_qq(PRED_MAP[i].shim, (int)TPL_W, a, b);
            const int32_t g = cq_shim_icmp_hl(PRED_MAP[i].shim, (int)TPL_W,
                                              a, PAIRS[p][1], 0u);

            /* ONE BIT AT AN EIGHT-BIT OPERAND WIDTH. A `dst` minted at the
             * operand width compares equal on lane 0 and leaks W-1 qubits per
             * compare, across a corpus that emits 21,323 of them. */
            CHECK_EQ(cq_reg_width(&ctx->regs, f), 1u);
            CHECK_EQ(cq_reg_width(&ctx->regs, g), 1u);
            CHECK_EQ((int)cq_pc_value(ctx, f),
                     cq_ref_icmp(PRED_MAP[i].ref, ra, rb, (int)TPL_W));
            CHECK_EQ((int)cq_pc_value(ctx, g),
                     cq_ref_icmp(PRED_MAP[i].ref, ra, rb, (int)TPL_W));

            cqrt_free(a); cqrt_free(b); cqrt_free(f); cqrt_free(g);
        }
    }
    close_with(&m);
}

/* -------------------------------------------------------------------------
 * 5. `cast` — arity-1, two widths, and the other result-width trap.
 * ------------------------------------------------------------------------- */

/* A CAST MINTS `to_bits`, NOT `from_bits`, and `sext` is what separates the
 * three kinds: at a source whose top bit is set, `zext` and `sext` differ in
 * every broadcast lane and `trunc` narrows. A dispatch table that named `zext`
 * for `sext` would be right for exactly the non-negative half of every input. */
CQ_TEST(a_cast_mints_the_to_width_and_each_kind_reaches_its_own_kernel)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t a = qreg(ctx, TPL_W, 0xB5u);          /* negative as i8 */
    const cq_ref_w ra = cq_ref_w_make(0xB5u, 0u, (int)TPL_W);

    const int32_t z = cq_shim_cast(CQ_SHIM_CAST_ZEXT, (int)TPL_W, 32, a);
    const int32_t x = cq_shim_cast(CQ_SHIM_CAST_SEXT, (int)TPL_W, 32, a);
    const int32_t t = cq_shim_cast(CQ_SHIM_CAST_TRUNC, (int)TPL_W, 4, a);

    CHECK_EQ(cq_reg_width(&ctx->regs, z), 32u);
    CHECK_EQ(cq_reg_width(&ctx->regs, x), 32u);
    CHECK_EQ(cq_reg_width(&ctx->regs, t), 4u);
    CHECK(cq_ref_w_eq(cq_pc_value_w(ctx, z), cq_ref_w_zext (ra, (int)TPL_W, 32)));
    CHECK(cq_ref_w_eq(cq_pc_value_w(ctx, x), cq_ref_w_sext (ra, (int)TPL_W, 32)));
    CHECK(cq_ref_w_eq(cq_pc_value_w(ctx, t), cq_ref_w_trunc(ra, (int)TPL_W, 4)));
    CHECK(cq_pc_value(ctx, z) != cq_pc_value(ctx, x));

    cq_shim_cast_unc(CQ_SHIM_CAST_SEXT, (int)TPL_W, 32, x, a);
    CHECK_EQ(cq_pc_value(ctx, x), 0u);

    cqrt_free(a); cqrt_free(z); cqrt_free(x); cqrt_free(t);
    close_with(&m);
}

