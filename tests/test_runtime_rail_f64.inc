/* tests/test_runtime_rail_f64.inc — the f64 CORE surface (PRD-v2 §1 and §3.1,
 * bead 9ve.28), on the seam tests/test_runtime_rail.c records for itself: an
 * `.inc` rather than 90 more lines in a file already at 245 of Rule 12's 300.
 *
 * WHAT THERE IS TO GET WRONG HERE IS EXACTLY ONE THING, AND IT IS NOT THE
 * CIRCUIT. PRD-v2 §3.1 is explicit that an f64 rail is v1's register at an fp
 * width and that NOTHING about the representation changes — so copy, free, the
 * pool, I2, I4 and the §9 axis are the integer file's subjects and are already
 * tested there at i64, over the same 64 `cq_bit` lanes, by the same code. What
 * is new is the REINTERPRETATION at the two ABI doors: `cqrt_alloc_f64` must
 * memcpy a `double` into 64 constant bits and `cqrt_measure_f64` must memcpy
 * back, and the wrong spelling — a C cast — compiles clean and is silent under
 * -Wconversion because the conversion is explicit.
 *
 * SO THE ORACLE IS A BIT PATTERN AND NEVER A `double` COMPARISON, and that is
 * load-bearing twice over rather than fastidious:
 *
 *   - `NaN != NaN`, so a `==` oracle REPORTS A FAILURE on a correct round trip
 *     of every NaN row;
 *   - `-0.0 == 0.0`, so a `==` oracle PASSES on a rail whose sign bit was
 *     dropped — the silent direction.
 *
 * Every comparison below therefore goes through `f64_bits`, and the anchors are
 * the LITERAL patterns of tests/support/fpanchors.h (PRD-v2 §7.4 pins those
 * cells by table precisely so they read the same on every host). Nothing in this
 * file does `double` arithmetic; `f64_bits` and `f64_of` are memcpys.
 */

/* The two reinterpretations, spelled here rather than borrowed from the shim:
 * an oracle that shares a function with the code is blind to what that function
 * gets wrong. These are three lines of memcpy and the point is that they are
 * INDEPENDENT of `cq_shim_f64_bits`, not that they are different. */
static uint64_t f64_bits(double d)
{
    uint64_t u;
    memcpy(&u, &d, sizeof u);
    return u;
}

static double f64_of(uint64_t u)
{
    double d;
    memcpy(&d, &u, sizeof d);
    return d;
}

/* §7.12's cells, as PATTERNS. The ±0 pair, the ±Inf pair, the default NaN, two
 * payload NaNs, a signalling NaN, both ends of the subnormal range, the
 * smallest normal, the largest finite, 1.0 and its neighbour. Every one is a
 * row where a numeric cast and a reinterpretation disagree, and the four
 * NEGATIVE rows are the ones a sign-dropping bug survives. */
static const uint64_t F64_CELLS[] = {
    CQ_F64_POS_ZERO,      CQ_F64_NEG_ZERO,
    CQ_F64_POS_INF,       CQ_F64_NEG_INF,
    CQ_F64_DEFAULT_NAN,   CQ_F64_QNAN_A,      CQ_F64_QNAN_B,
    CQ_F64_SNAN,          CQ_F64_SNAN_QUIET,
    CQ_F64_MIN_SUBNORMAL, CQ_F64_MAX_SUBNORMAL,
    CQ_F64_MIN_NORMAL,    CQ_F64_MAX,
    CQ_F64_ONE,           CQ_F64_ONE_PLUS_ULP, CQ_F64_HALF_ULP_OF_1,
    CQ_F64_NEG(CQ_F64_ONE), CQ_F64_NEG(CQ_F64_MIN_SUBNORMAL)
};

#define F64_N_CELLS (sizeof F64_CELLS / sizeof F64_CELLS[0])

/* AN f64 RAIL IS SIXTY-FOUR CONSTANT BITS AND OWNS NO QUBIT — I4 at an fp
 * width, which is PRD-v2 §3.1 item 3's whole claim and the reason a fully
 * classical fp program can cost nothing. Zero gates at every cell including the
 * NaNs, and the rail is 64 bits wide rather than whatever `sizeof(double)`
 * might have suggested.
 *
 * THE MUTANT THIS IS THE SOLE DETECTOR FOR is an `alloc` that materialises —
 * `cq_reg_alloc_zero` plus a write, or a stray `cq_materialise` loop. Every
 * value assertion in this file stays green under it, because a materialised
 * rail still HOLDS the pattern; only the pool and the gate count move. */
CQ_TEST(alloc_f64_mints_sixty_four_constant_bits_at_zero_cost)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const cq_pc_snap before = cq_pc_take(ctx);

    for (size_t i = 0; i < F64_N_CELLS; i++) {
        const int32_t h = cqrt_alloc_f64(f64_of(F64_CELLS[i]));

        CHECK_EQ(cq_reg_width(&ctx->regs, h), 64);
        CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, h), 0);
    }

    CHECK_EQ(cq_mock_count(&m), 0);              /* zero gates            */
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));  /* zero qubits (I4)      */
    cq_reg_audit(ctx);

    close_with(&m);
}

/* THE RAIL HOLDS THE IEEE PATTERN, NOT THE NUMBER — and this is the case that
 * separates `memcpy` from `(uint64_t)value`. At 3.5 the two answers are
 * 0x400C000000000000 and 3; at every NaN and Inf the cast is UNDEFINED
 * BEHAVIOUR, which is worse than wrong. It reads the register directly rather
 * than through `cqrt_measure_f64`, so a matched pair of wrong conversions —
 * cast in, cast out — cannot cancel into a green round trip. */
CQ_TEST(an_f64_rails_bits_are_the_ieee_pattern_and_not_a_converted_value)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    for (size_t i = 0; i < F64_N_CELLS; i++) {
        const int32_t h = cqrt_alloc_f64(f64_of(F64_CELLS[i]));

        CHECK(cq_pc_value(ctx, h) == F64_CELLS[i]);
    }

    /* The two rows a reader can check by hand, spelled out so the table above
     * is not the only statement of what a pattern is. */
    CHECK(cq_pc_value(ctx, cqrt_alloc_f64(3.5)) == UINT64_C(0x400C000000000000));
    CHECK(cq_pc_value(ctx, cqrt_alloc_f64(1.0)) == CQ_F64_ONE);

    close_with(&m);
}

/* alloc -> measure IS BIT-EXACT AT EVERY PINNED CELL, which is the second door
 * and the one a cast-in/cast-out pair would hide from the case above only if
 * the two casts were inverse — they are not, but the round trip is the claim a
 * caller actually makes, so it is asserted directly.
 *
 * THE COMPARISON IS ON PATTERNS. `CHECK(got == want)` on the doubles would be
 * RED on all four NaN rows and GREEN on a dropped sign bit; both are wrong and
 * the second is the dangerous one. An all-constant rail owns no wire (I4), so
 * the whole sweep emits ZERO `mz` — which is asserted here too, because a
 * measure that materialised first would round-trip perfectly and cost 64 qubits
 * a call. */
CQ_TEST(an_f64_rail_round_trips_every_pinned_ieee_cell_bit_exactly)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);

    for (size_t i = 0; i < F64_N_CELLS; i++) {
        const double   in  = f64_of(F64_CELLS[i]);
        const int32_t  h   = cqrt_alloc_f64(in);
        const double   out = cqrt_measure_f64(h);

        if (f64_bits(out) != F64_CELLS[i])
            cq_h_fail(__FILE__, __LINE__,
                      "cell %zu: alloc/measure gave %016llx, wanted %016llx",
                      i, (unsigned long long)f64_bits(out),
                      (unsigned long long)F64_CELLS[i]);
    }

    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_MZ), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));

    close_with(&m);
}

/* A QUANTUM f64 RAIL MEASURES ONE `mz` PER WIRE AND RETURNS THE PATTERN, and
 * the mixed mask is what discriminates per-QUBIT from per-BIT — the same
 * fixture the i8 case uses, one width up. The pattern chosen has wires in the
 * EXPONENT and the SIGN as well as the mantissa, because a shim that walked
 * only the low 52 lanes would round-trip every mantissa-only fixture. */
CQ_TEST(measure_f64_emits_one_mz_per_wire_and_returns_the_pattern)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    /* −Inf with the sign, the exponent's low bit and one mantissa bit on
     * wires: 0xFFF0000000000000 under a mask touching lanes 63, 52 and 0. */
    const uint64_t want = CQ_F64_NEG_INF;
    const uint64_t mask = (UINT64_C(1) << 63) | (UINT64_C(1) << 52) | UINT64_C(1);
    const int32_t  h    = cq_bk_reg(ctx, 64u, want, mask);

    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, h), 3);
    cq_mock_reset(&m);

    const double got = cqrt_measure_f64(h);

    CHECK(f64_bits(got) == want);
    CHECK_EQ(cq_mock_count(&m), 3);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_MZ), 3);
    CHECK_EQ(cq_reg_state(&ctx->regs, h), CQ_SLOT_MEASURED);
    CHECK(cq_pc_live_is_exactly(ctx, &h, 1u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* `cqrt_copy_f64` IS PHYSICAL (I2) AND ITS ARGUMENT ORDER IS THE ABI's. Both
 * parameters are int32_t, so nothing a compiler or a linker can do would catch
 * a transposition — it is pinned behaviourally, by which rail gained the wires,
 * exactly as the integer case is. The f64 row goes through the SAME
 * `rail_copy`, so what this case really rules out is a macro instantiation at
 * the wrong WIDTH: `CQ_RAIL_COPY(f64, 32)` copies 32 of 64 lanes and leaves the
 * top half of the destination at its birth constant. */
CQ_TEST(copy_f64_is_physical_at_the_full_sixty_four_lanes)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const uint64_t pat = CQ_F64_QNAN_B;          /* bits above 52 AND below */
    const int32_t  src = cq_bk_reg(ctx, 64u, pat, ~UINT64_C(0));
    const int32_t  dst = cqrt_alloc_f64(0.0);

    uint32_t before[64];
    CHECK_EQ(cq_pc_indices(ctx, src, before, 64u), 64);
    cq_mock_reset(&m);

    cqrt_copy_f64(src, dst);

    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, dst), 64);
    CHECK(cq_pc_value(ctx, dst) == pat);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX), 64);

    /* The SOURCE is untouched: a transposed call leaves `dst` at zero wires and
     * the first half of this case green. */
    uint32_t after[64];
    CHECK_EQ(cq_pc_indices(ctx, src, after, 64u), 64);
    CHECK_EQ(memcmp(before, after, sizeof before), 0);
    CHECK(cq_pc_value(ctx, src) == pat);

    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ src, dst }, 2u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* THE ZERO-CONTROLLED COPY EMITS NOTHING AND THE ONE-CONTROLLED ONE EMITS THE
 * UNCONTROLLED CIRCUIT — §9 row 0 at an fp width, reached through the SAME
 * `cq_shim_region`. It is here rather than left to the i64 case because the
 * macro instantiation is new and because row 0's zero-qubit half needs the
 * region to short-circuit before anything is materialised. */
CQ_TEST(copy_f64_controlled_takes_section_nines_row_zero_at_an_fp_width)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const uint64_t pat  = CQ_F64_MIN_NORMAL;
    const int32_t  off  = cqrt_alloc_i1(false);
    const int32_t  on   = cqrt_alloc_i1(true);
    const int32_t  src  = cq_bk_reg(ctx, 64u, pat, ~UINT64_C(0));
    const int32_t  d0   = cqrt_alloc_f64(0.0);
    const int32_t  d1   = cqrt_alloc_f64(0.0);

    cq_mock_reset(&m);
    const cq_pc_snap before = cq_pc_take(ctx);

    cqrt_copy_f64_controlled(off, src, d0);
    CHECK_EQ(cq_mock_count(&m), 0);                 /* 0 gates, 0 qubits   */
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, d0), 0);

    cqrt_copy_f64_controlled(on, src, d1);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX), 64);   /* verbatim, uncontrolled */
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CCX), 0);
    CHECK(cq_pc_value(ctx, d1) == pat);

    cq_reg_audit(ctx);
    close_with(&m);
}

/* `cqrt_free` OF AN ALL-CONSTANT f64 RAIL REACHES THE POOL WITH NOTHING, which
 * is I4 and D15's "40 loop counters" row arriving at an fp width: the rail owns
 * no qubit, so no evidence reaches the proof at all and the strand counters do
 * not move. `cqrt_free` carries no width token — the ABI frees a handle, not a
 * typed rail — so this is also the whole of "free for an f64 rail". */
CQ_TEST(free_of_an_all_constant_f64_rail_costs_nothing_and_strands_nothing)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const cq_pc_snap before = cq_pc_take(ctx);
    const int32_t h = cqrt_alloc_f64(f64_of(CQ_F64_DEFAULT_NAN));

    cqrt_free(h);

    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    CHECK_EQ(cq_reg_state(&ctx->regs, h), CQ_SLOT_DEAD);

    close_with(&m);
}
