/* tests/test_template_fun.inc — the UNARY M26 door, `cq_shim_fun` /
 * `cq_shim_fun_unc`, over `cq_shim_fun_op { CQ_SHIM_FUN_FSQRT }`.
 *
 * NO `cq_template_*` SYMBOL REACHES IT YET, AND THAT IS RECORDED RATHER THAN
 * WORKED AROUND. `opcode_table.yaml`'s only unary opcode is `fneg`
 * (`:209-210`), whose kernel is bead 9ve.27's and does not exist, so `fneg`
 * keeps its `"fp is v2"` abort and `gen_bodies.ENTRY` has no `("funary", ...)`
 * row — its KeyError is the documented failure for a premature landing.
 * `fsqrt` is `llvm.sqrt` in CQ_lang's intrinsic_table.yaml, which bead 9ve.24
 * vendors; M27's scope is `opcode_table.yaml` ONLY (PRD §1).
 *
 * SO THE DOOR IS BUILT, TESTED BY NAME, AND UNREACHED. That is the same
 * posture `cq_shim_fbin_lh` has for `fadd` — a shape the ABI does not spell —
 * except here it is the whole family, and the reason to build it now is that
 * M40's kernel exists and the arity-1 ordered call sequence is being written
 * anyway: a second consumer is what says the sequence is not secretly a cast.
 *
 * `cq_kernel_fsqrt` IS NOT A `cq_kernel_fn`. One source, one width, dst 64
 * lanes — Rule 7 fixes the SEMANTICS and not the parameter list (`ckd.15`), so
 * it is reached through the dispatch table's `(F, T)` adapter, exactly as M13's
 * casts are, with `F == T == 64` guaranteed by the entry point's own refusal.
 */

/* §7.12 cells a square root distinguishes: a perfect square, a non-square, a
 * NEGATIVE (whose result is the default NaN and is x86's choice, pinned by
 * table in M40's own suite), both zeros, an infinity and a NaN. */
static const uint64_t FSQRT_SRC[] = {
    0x4010000000000000ull,          /* 4.0   -> 2.0                        */
    0x4000000000000000ull,          /* 2.0   -> an irrational, rounded     */
    0xBFF0000000000000ull,          /* -1.0  -> the default NaN            */
    CQ_F64_POS_ZERO, CQ_F64_NEG_ZERO,
    CQ_F64_POS_INF,  CQ_F64_QNAN_A,
    0x0000000000000001ull           /* the smallest subnormal              */
};

#define FSQRT_N (sizeof FSQRT_SRC / sizeof FSQRT_SRC[0])

/* THE DISPATCH ORACLE AND L5. Eight calls, zero gates and zero qubits for an
 * operation whose circuit is 229,302 gates over 67,405 scratch qubits — and
 * every result against the kernel's own `_eval`, which is `soft_fsqrt`
 * transcribed over `uint64_t` and is NEVER the host `sqrt()`: IEEE specifies
 * the square root exactly, but the NEGATIVE row and the NaN passthrough are
 * x86's choices and arm64 differs on both (PRD-v2 §7.4). */
CQ_TEST(the_unary_door_dispatches_fsqrt_on_a_constant_rail)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);

    for (size_t i = 0; i < FSQRT_N; i++) {
        const int32_t a = cqrt_alloc_f64(f64_of(FSQRT_SRC[i]));
        const int32_t out = cq_shim_fun(CQ_SHIM_FUN_FSQRT, 64, a);

        CHECK_EQ(cq_reg_width(&ctx->regs, out), 64);
        if (cq_pc_value(ctx, out) != cq_fsqrt_eval(FSQRT_SRC[i]))
            cq_h_fail(__FILE__, __LINE__,
                      "fsqrt(%016llx) = %016llx, the kernel's own eval says "
                      "%016llx", (unsigned long long)FSQRT_SRC[i],
                      (unsigned long long)cq_pc_value(ctx, out),
                      (unsigned long long)cq_fsqrt_eval(FSQRT_SRC[i]));
    }
    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    cq_reg_audit(ctx);
    close_with(&m);
}

/* ONE ALL-QUANTUM ROUND TRIP. It is the only case in this file that runs the
 * 229,302-gate circuit twice, and what it adds over the constant sweep is
 * everything the constant sweep structurally cannot say: that the arity-1
 * sequence resolves ONE source and mints a 64-lane result on the wire path,
 * that `_unc` is the SAME call and zeroes the rail, and that the free reaches
 * the pool per index. */
CQ_TEST(a_fsqrt_round_trip_on_wires_zeroes_its_out_rail_and_frees_clean)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    static const uint64_t FOUR = 0x4010000000000000ull;
    const int32_t a = qreg(ctx, 64u, FOUR);
    const cq_pc_snap before = cq_pc_take(ctx);
    const int32_t out = cq_shim_fun(CQ_SHIM_FUN_FSQRT, 64, a);
    const int32_t live[2] = { a, out };
    uint32_t idx[64];
    uint32_t n;

    CHECK_EQ(cq_reg_width(&ctx->regs, out), 64);
    CHECK_EQ(cq_pc_value(ctx, out), cq_fsqrt_eval(FOUR));
    CHECK_EQ(cq_pc_value(ctx, a), FOUR);        /* the source is unchanged */
    CHECK(cq_pc_live_is_exactly(ctx, live, 2u));

    n = cq_pc_indices(ctx, out, idx, 64u);
    cq_shim_fun_unc(CQ_SHIM_FUN_FSQRT, 64, out, a);
    CHECK_EQ(cq_pc_value(ctx, out), 0);
    CHECK(cq_pc_live_is_exactly(ctx, live, 2u));

    cqrt_free(out);
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0u);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    CHECK(cq_pc_indices_are_free(ctx, idx, n));
    cq_reg_audit(ctx);
    close_with(&m);
}

/* THE UNARY FAMILY'S OWN TAG SPACE, and it is the LAST of the three bead
 * 9ve.36 claims to have a detector. The fp binary space has one
 * (`an_fadd_unc_does_not_pair_with_an_add_i64_forward_on_the_same_handles`)
 * and the conversion space has one
 * (`an_fconv_unc_does_not_pair_with_an_integer_cast_on_the_same_handles`);
 * without this case, collapsing `0x20000000` onto `0x10000000` left the whole
 * selection GREEN, because nothing in the tree produced the shape that
 * separates them.
 *
 * THE SHAPE IS EXACT AND IT TAKES THE `_hl` DOOR TO REACH. `adjoint_matches`
 * compares the twin opcodes, the TAG, all four handle slots, the §9 context,
 * the immediate and the `cqrt_x` parities. A `qq` `fbin` forward records
 * `{out, a, b}` and a unary `_unc` records `{out, a, NONE}`, so slot 2 keeps
 * them apart whatever the tag says. The `_hl` door is the one that closes
 * that gap: it records `{out, a, NONE}` too, and with a literal of `+0.0` the
 * `imm` — `r.lo ^ r.hi` — is 0, which is what a `memset` record carries. Every
 * field then agrees and ONLY the prefix is left.
 *
 * `fadd(x, +0.0)` IS THE IDENTITY ON A NORMAL, so `out` ends holding
 * `a ^ fsqrt(a)`, which is far from zero — asserted rather than assumed,
 * because an operand the two agreed about would leave the rail at zero and the
 * case would pass under the mutant for the wrong reason.
 *
 * AND THE SOURCE IS POISONED OR THE SHADOW ANSWERS FIRST: on the rotation-free
 * surface the shadow sees the rail is non-zero and strands whatever the
 * certificate says, so the mutant survives. */
CQ_TEST(a_fsqrt_unc_does_not_pair_with_an_fadd_forward_on_the_same_handles)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    static const uint64_t FOUR = 0x4010000000000000ull;
    const int32_t a = qreg(ctx, 64u, FOUR);
    uint32_t strand_before;
    int32_t out;

    CHECK(cq_fadd_eval(FOUR, CQ_F64_POS_ZERO) != cq_fsqrt_eval(FOUR));

    cqrt_ry_f64(a, 0.5);
    strand_before = cq_qubits_stranded(&ctx->pool);

    /* The literal goes through the SAME macro pair a generated `_hl` body
     * uses, which is also what makes `imm` — `r.lo ^ r.hi` — exactly 0. */
    out = cq_shim_fbin_hl(CQ_SHIM_FOP_FADD, 64, a,
                          CQ_SHIM_F64_LO(0.0), CQ_SHIM_F64_HI(0.0));
    /* The `_unc` of a DIFFERENT operation on the same handles — a legal call
     * that the certificate must decline to discharge. */
    cq_shim_fun_unc(CQ_SHIM_FUN_FSQRT, 64, out, a);
    cqrt_free(out);

    CHECK(cq_qubits_stranded(&ctx->pool) > strand_before);
    CHECK_EQ(cq_reg_strand_reports(ctx), 1u);

    cq_reg_audit(ctx);
    close_with(&m);
}

/* AND THE SAME ROUND TRIP POISONED, which is the only version the certificate
 * answers: `fsqrt` emits no rotation, so on an ordinary operand the shadow
 * proves `out` clean by itself and the unary family's TAG — its D15 twin
 * identity — is never consulted. The negative control is what stops the rest
 * of the case being vacuous if a change stopped poisoning. */
CQ_TEST(a_fsqrt_unc_on_a_poisoned_source_is_discharged_by_the_certificate)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t a = qreg(ctx, 64u, 0x4010000000000000ull);
    uint32_t strand_before;
    int32_t out;

    cqrt_ry_f64(a, 0.5);
    strand_before = cq_qubits_stranded(&ctx->pool);

    out = cq_shim_fun(CQ_SHIM_FUN_FSQRT, 64, a);
    CHECK_EQ(cq_reg_disposition(ctx, out, cq_pc_zero_proof_rotation_free),
             CQ_PROOF_UNPROVEN);
    cq_shim_fun_unc(CQ_SHIM_FUN_FSQRT, 64, out, a);
    cqrt_free(out);

    if (cq_qubits_stranded(&ctx->pool) - strand_before != 0u)
        cq_h_fail(__FILE__, __LINE__,
                  "the certificate did not discharge a poisoned fsqrt round "
                  "trip — %u qubit(s) stranded",
                  cq_qubits_stranded(&ctx->pool) - strand_before);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0u);

    cq_reg_audit(ctx);
    close_with(&m);
}
