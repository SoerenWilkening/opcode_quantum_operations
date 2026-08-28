/* tests/test_runtime_rail_write.inc — the CONTENTS half of Step 23 landing 1
 * step 4's rail suite, split on the seam IMPLEMENTATION_PLAN §3 records for
 * shim/cq_runtime_rail.c itself: `the rail's LIFETIME <-> the rail's CONTENTS`.
 *
 * The cut is a subject cut and it is the source file's own. Its discriminator
 * is EMISSION, and it is a DISJOINTNESS: the lifetime half (alloc / measure /
 * free, ELEVEN symbols) emits `mz` and nothing else, and the CONTENTS half in
 * this file (copy / copy_controlled / cswap / addc / xorc, TWENTY-ONE) emits
 * nothing BUT X / CX / CCX.
 *
 * IT IS NOT "only the lifetime half changes a slot state" and NOT "only the
 * lifetime half touches D15's disposition" — `rail_addc` mints two rails,
 * tombstones both and frees both, in THIS half. Nor does this half leave the
 * table as it found it: measured in Release, a quantum-path addc advances the
 * D5 handle counter by 2 for good. (The sentence this paragraph replaced also
 * mis-attributed — it listed the CONTENTS half's four properties and called
 * them "these eleven symbols", which is the LIFETIME half's count, and of which
 * alloc takes no handle, alloc and measure do not return void, and none emits
 * X/CX/CCX.) What IS only the lifetime half's is the SUBJECT: a handle CQ_lang
 * named. Taken on the test side first, exactly as
 * tests/test_shim_ctx_region.inc was — that both sides want the same cut is the
 * argument for the seam.
 *
 * Included by tests/test_runtime_rail.c and inheriting its includes. It is out
 * of check_loc.sh's reach because that script's `find` matches `*.c`, `*.h` and
 * `*.py` only — an EXTENSION rule, not a translation-unit rule.
 *
 * THE COST IS THE SPECIFICATION IN THIS FILE, which is not true of the lifetime
 * half. PRD §2.1 prices `cqrt_cswap` under a constant control at ZERO GATES,
 * and src/reg.h enumerates three routes that give the RIGHT VALUE and the wrong
 * cost. A suite that asserted only values would be green against all three.
 */

/* -------------------------------------------------------------------------
 * 5. copy, and the argument order nothing else in the tree can see.
 * ------------------------------------------------------------------------- */

/* THE ABI IS (src, dst) AND cq_reg_xor_into IS (ctx, dst, src). Both handles
 * are int32_t, so the vendored declaration header cannot see a transposition —
 * C ignores parameter names — and neither can the link, and neither can a gate
 * COUNT, because the swapped call emits a legal stream of its own. What
 * distinguishes them is WHICH RAIL GAINED THE WIRES: copying an all-quantum
 * source into a fresh constant destination materialises the destination, and
 * the swap instead XORs eight constant ZEROs into the source, which folds to
 * nothing at all. So the pin is asymmetric on purpose — it asserts both that
 * `dst` moved and that `src` did not. */
CQ_TEST(copy_xors_the_source_into_the_destination_in_the_abis_own_order)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t src = cq_bk_reg(ctx, 8u, 0xA5u, 0xFFu);   /* eight wires */
    const int32_t dst = cqrt_alloc_i8(0);                   /* all constant */

    uint32_t before[8];
    CHECK_EQ(cq_pc_indices(ctx, src, before, 8u), 8);
    cq_mock_reset(&m);

    cqrt_copy_i8(src, dst);

    /* The destination gained every wire and holds the value. */
    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, dst), 8);
    CHECK_EQ(cq_pc_value(ctx, dst), 0xA5);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX), 8);

    /* The source is untouched — same indices, same value, still eight wires.
     * A transposed call leaves `dst` at zero wires and this half green, which
     * is why both halves are asserted. */
    uint32_t after[8];
    CHECK_EQ(cq_pc_indices(ctx, src, after, 8u), 8);
    CHECK_EQ(memcmp(before, after, sizeof before), 0);
    CHECK_EQ(cq_pc_value(ctx, src), 0xA5);

    /* L2 AS A SET, AND THE I2 SWEEP — AND THIS PAIR IS THE ONE ADDITION IN THIS
     * FILE WITH A MEASURED, CURRENTLY-SURVIVING PROVOCATION BEHIND IT. A COUNT
     * cannot see a NET-ZERO pool swap: acquire a fresh ancilla, release one of
     * `dst`'s own provably-clean indices, and `live` does not move, so
     * `cq_pc_same` is green while `src`/`dst` name an index on the free list and
     * an unowned index is live — I2 and I3 both lies. Measured: that mutant in
     * `rail_copy`, and even a BARE LEAK of one qubit per `cqrt_copy_*` call,
     * survived all 256 tests in BOTH configurations before these two lines.
     *
     * WHY BOTH, AND WHY NEITHER IS A SUPERSET. `cq_reg_audit` is Debug-only
     * (reg.h) — in Release the set check is the whole detector — and it sees
     * what L2 cannot: one index twice WITHIN a register, and an index at or
     * beyond `minted`. The list must be derived from this case's own
     * declarations; a list that omits a live rail fails for a bookkeeping
     * reason, and the tempting fix is to delete the assertion. */
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ src, dst }, 2u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* THE ABI CALLS IT A COPY AND ITS SEMANTICS ARE AN XOR, and CQ_lang's own
 * corpus contradicts CQ_lang's own header comment ("Precondition: dst is a
 * fresh |0> register"). Measured over the 247 goldens on 2026-08-23: 565 of
 * 1,024 integer `cqrt_copy_*_controlled` calls target a destination that has
 * already been written, and no controlled-copy destination in the corpus is
 * written only once. So a future reader who "fixes" this into a real copy, or
 * adds `assert every bit of dst is CQ_BIT_ZERO`, breaks the MAJORITY of the
 * corpus — and nothing else in tests/ copies into a non-zero destination.
 * This case is that detector, at the ABI level; src/reg.c's own is one layer
 * down. Read the corpus figure as a ratio and a date: nothing in this
 * repository pins that corpus. */
CQ_TEST(copy_into_a_written_destination_is_an_xor_and_not_a_copy)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t src = cqrt_alloc_i8(9);
    const int32_t dst = cqrt_alloc_i8(3);
    const cq_pc_snap before = cq_pc_take(ctx);

    cqrt_copy_i8(src, dst);

    CHECK_EQ(cq_pc_value(ctx, dst), 9u ^ 3u);      /* 10, not 9 */
    CHECK_EQ(cq_mock_count(&m), 0);                /* and free, both classical */

    /* THE SNAPSHOT IS TAKEN BEFORE THE CALL, WHICH A FIRST DRAFT DID NOT DO: it
     * compared `cq_pc_take(ctx)` against `cq_pc_take(ctx)` — two readings of the
     * SAME instant — which is true of any library at all. */
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ src, dst }, 2u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* -------------------------------------------------------------------------
 * 6. copy_controlled — PRD §9 row 0, and §2.1's stated lowering.
 * ------------------------------------------------------------------------- */

/* ROW 0 IS A DECISION, NOT A CIRCUIT, AND THE `ONE` ROW IS ASSERTED
 * DIFFERENTIALLY. "Emits it uncontrolled, verbatim" is a claim about a STREAM,
 * so it is checked against the same copy with no region rather than against a
 * count — the two contexts are built identically, so the qubit indices agree
 * and the comparison is gate for gate. */
CQ_TEST(copy_controlled_row_0_skips_on_zero_and_is_verbatim_on_one)
{
    cq_mock plain, zero, one; cq_sink sp, sz, so;
    cq_rec captured[16];
    size_t n_plain = 0u;

    /* (a) the uncontrolled copy, recorded. */
    {
        cq_ctx *ctx = open_with(&plain, &sp);
        const int32_t src = cq_bk_reg(ctx, 8u, 0xBu, 0xFu);
        const int32_t dst = cqrt_alloc_i8(0);
        cq_mock_reset(&plain);
        cqrt_copy_i8(src, dst);
        n_plain = cq_mock_count(&plain);

        /* THE BOUND IS THE BUFFER GUARD; THE PIN IS THE ASSERTION, AND WITHOUT
         * BOTH THIS CASE CAN GO VACUOUS. `cq_mock_matches(&one, captured, 0)` is
         * trivially satisfied, so a `<=` alone lets the whole (a)-vs-(c)
         * differential evaporate the day the captured run emits nothing — an
         * implication-shaped assertion passing for free, which this project has
         * recorded before. Four, precisely: `src`'s quantum mask is 0xF, so
         * lanes 0-3 each materialise a ZERO destination lane (no `X`, the
         * constant is 0) and emit one `CX`, and lanes 4-7 are classical ZERO
         * and fold to nothing. */
        CHECK(n_plain <= 16u);
        CHECK_EQ(n_plain, 4);
        for (size_t i = 0; i < n_plain && i < 16u; i++) captured[i] = *cq_mock_at(&plain, i);
        CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ src, dst }, 2u));
        cq_reg_audit(ctx);
        close_with(&plain);
    }

    /* (b) a CQ_BIT_ZERO flag: no gates AND no qubits. The BODY still runs —
     * `cq_shim_region` calls it unconditionally — so what makes the zero-qubit
     * half true on this path is `cq_emit_cx` returning at row 0 before
     * `cq_emit_cx_phys` would materialise the destination. Asserted rather than
     * assumed, because a bracket that leaked one qubit per controlled call
     * would otherwise survive every case in the file. */
    {
        cq_ctx *ctx = open_with(&zero, &sz);
        const int32_t src  = cq_bk_reg(ctx, 8u, 0xBu, 0xFu);
        const int32_t dst  = cqrt_alloc_i8(0);
        const int32_t flag = cq_bk_reg(ctx, 1u, 0u, 0u);      /* CQ_BIT_ZERO */
        cq_mock_reset(&zero);
        const cq_pc_snap before = cq_pc_take(ctx);

        cqrt_copy_i8_controlled(flag, src, dst);

        CHECK_EQ(cq_mock_count(&zero), 0);
        CHECK(cq_pc_same(before, cq_pc_take(ctx)));
        CHECK_EQ(cq_pc_value(ctx, dst), 0);
        CHECK_EQ(cq_ctrl_depth(ctx), 0);
        CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ src, dst, flag }, 3u));
        cq_reg_audit(ctx);
        close_with(&zero);
    }

    /* (c) a CQ_BIT_ONE flag: byte-identical to (a). */
    {
        cq_ctx *ctx = open_with(&one, &so);
        const int32_t src  = cq_bk_reg(ctx, 8u, 0xBu, 0xFu);
        const int32_t dst  = cqrt_alloc_i8(0);
        const int32_t flag = cq_bk_reg(ctx, 1u, 1u, 0u);      /* CQ_BIT_ONE */
        cq_mock_reset(&one);

        cqrt_copy_i8_controlled(flag, src, dst);

        CHECK(cq_mock_matches(&one, captured, n_plain));
        CHECK_EQ(cq_ctrl_depth(ctx), 0);
        CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ src, dst, flag }, 3u));
        cq_reg_audit(ctx);
        close_with(&one);
    }
}

/* PRD §2.1's own lowering, executed: "`dst ^= src` under `ctrl`: one
 * `CCX(ctrl, src_i, dst_i)` per bit". Per QUANTUM src bit, precisely — a
 * classical ZERO source lane folds before the promotion (PRD §9 row A: a fold
 * that reads a CONTROL is semantic and survives any control), so the mixed rail
 * is the fixture that says which. */
CQ_TEST(copy_controlled_under_a_quantum_flag_is_one_ccx_per_source_wire)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    /* value 0b0101 in an 8-bit rail, wires on lanes 0 and 2; every other lane
     * is a constant ZERO, which is what makes this per-WIRE rather than
     * per-bit. */
    const int32_t src  = cq_bk_reg(ctx, 8u, 0x5u, 0x5u);
    const int32_t dst  = cqrt_alloc_i8(0);
    const int32_t flag = cq_bk_reg(ctx, 1u, 1u, 1u);          /* CQ_BIT_Q, 1 */
    cq_mock_reset(&m);
    const uint32_t live_before = cq_qubits_live(&ctx->pool);

    cqrt_copy_i8_controlled(flag, src, dst);

    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CCX), 2);   /* one per source WIRE */
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX),  0);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_X),   0);
    /* Two destination lanes became wires; the other SIX are still constants,
     * so I4 still holds for them. */
    CHECK_EQ(cq_qubits_live(&ctx->pool) - live_before, 2);
    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, dst), 2);
    CHECK_EQ(cq_ctrl_depth(ctx), 0);
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ src, dst, flag }, 3u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* -------------------------------------------------------------------------
 * 7. cswap — where the COST is the specification.
 * ------------------------------------------------------------------------- */

/* PRD §2.1 PRICES A CONSTANT CONTROL AT ZERO GATES, AND BOTH ROWS MUST BE
 * TESTED, because they fail differently: ZERO must do nothing, ONE must do the
 * exchange and still cost nothing. A rail-to-rail exchange is a table
 * relabelling, not a circuit — the emitter has no gate that means it — which is
 * why cq_reg_swap_bits exists at all. */
CQ_TEST(cswap_under_a_constant_control_costs_nothing_in_both_rows)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t a = cq_bk_reg(ctx, 4u, 0x3u, 0xFu);   /* wires holding 0011 */
    const int32_t b = cq_bk_reg(ctx, 4u, 0xCu, 0xFu);   /* wires holding 1100 */
    const int32_t off = cq_bk_reg(ctx, 1u, 0u, 0u);
    const int32_t on  = cq_bk_reg(ctx, 1u, 1u, 0u);
    cq_mock_reset(&m);
    const cq_pc_snap before = cq_pc_take(ctx);

    cqrt_cswap(off, a, b);
    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    CHECK_EQ(cq_pc_value(ctx, a), 0x3);                 /* not exchanged */
    CHECK_EQ(cq_pc_value(ctx, b), 0xC);

    cqrt_cswap(on, a, b);
    CHECK_EQ(cq_mock_count(&m), 0);                     /* still zero gates */
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));         /* still zero qubits */
    CHECK_EQ(cq_pc_value(ctx, a), 0xC);                 /* exchanged */
    CHECK_EQ(cq_pc_value(ctx, b), 0x3);

    /* A RELABELLING MUST LEAVE THE OWNERSHIP MAP CONSISTENT, and the exchange
     * is exactly the act that could corrupt it: `cq_reg_swap_bits` moves
     * indices between two live registers, so an off-by-one there leaves an
     * index owned twice or owned by nobody — neither of which moves `live`. */
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ a, b, off, on }, 4u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* A QUANTUM CONTROL IS A FREDKIN PER BIT — `CX(b,a); CCX(ctrl,a,b); CX(b,a)` —
 * emitted through the ORDINARY emitter with the flag as an ordinary operand,
 * never through a §9 region. 2W CX + W CCX at the all-quantum mask, no new
 * qubits, and the exchange really happens. */
CQ_TEST(cswap_under_a_quantum_control_is_a_fredkin_per_bit)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t a    = cq_bk_reg(ctx, 4u, 0x3u, 0xFu);
    const int32_t b    = cq_bk_reg(ctx, 4u, 0xCu, 0xFu);
    const int32_t flag = cq_bk_reg(ctx, 1u, 1u, 1u);          /* CQ_BIT_Q, 1 */
    cq_mock_reset(&m);
    const cq_pc_snap before = cq_pc_take(ctx);

    cqrt_cswap(flag, a, b);

    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX),  8);       /* 2W */
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CCX), 4);       /* W  */
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_X),   0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));         /* no new wires */

    /* The flag's shadow reads 1, so the exchange happened. */
    CHECK_EQ(cq_pc_value(ctx, a), 0xC);
    CHECK_EQ(cq_pc_value(ctx, b), 0x3);
    CHECK_EQ(cq_ctrl_depth(ctx), 0);
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ a, b, flag }, 3u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* THE SHAPE THE CORPUS ACTUALLY PRODUCES, WHICH NEITHER CASE ABOVE REACHES AND
 * WHICH IS THE ONLY ONE THAT PINS THE FREDKIN'S OPERAND ORDER.
 *
 * `CX(b,a); CCX(ctrl,a,b); CX(b,a)` and the same three gates with `a` and `b`
 * exchanged are the SAME unitary, so no value, no palindrome and no all-quantum
 * gate count can tell them apart — measured: swapping the roles left every case
 * above green. They differ in COST at a mixed mask, and the corpus is mixed:
 * in every one of its 37 `cqrt_cswap` calls the swapped-in rail is a
 * `cqrt_alloc_i32(0)` minted on the line before, i.e. all-constant and owning
 * zero qubits by I4. At that shape the shipped order costs `W CX + W CCX` and
 * mints W wires, and the exchanged order costs `2W CX + W CCX` and mints the
 * same W — one extra gate per bit, on the only shape CQ_lang emits.
 *
 * The bracketing pair is the corpus's too (`cswap; rz; cswap` around a work
 * rail, and SWAP is an involution so the route back IS the adjoint), which is
 * what makes the round trip an assertion rather than decoration. */
CQ_TEST(cswap_at_the_corpus_shape_pins_the_fredkins_operand_order)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t a    = cq_bk_reg(ctx, 8u, 0xA5u, 0xFFu);   /* eight wires   */
    const int32_t b    = cqrt_alloc_i8(0);                   /* fresh, I4     */
    const int32_t flag = cq_bk_reg(ctx, 1u, 1u, 1u);         /* an icmp flag  */
    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, b), 0);
    cq_mock_reset(&m);
    const uint32_t live0 = cq_qubits_live(&ctx->pool);

    cqrt_cswap(flag, a, b);

    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX),  8);   /* W, not 2W */
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CCX), 8);   /* W         */
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_X),   0);
    CHECK_EQ(cq_qubits_live(&ctx->pool) - live0, 8);
    CHECK_EQ(cq_pc_value(ctx, a), 0x00);
    CHECK_EQ(cq_pc_value(ctx, b), 0xA5);

    /* The involution, which is how the corpus uses it: swap back and the rails
     * are as they started, in VALUE. Not in representation — `b` keeps the
     * wires it was given, because we never demote (D6). */
    cqrt_cswap(flag, a, b);
    CHECK_EQ(cq_pc_value(ctx, a), 0xA5);
    CHECK_EQ(cq_pc_value(ctx, b), 0x00);
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ a, b, flag }, 3u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* AND copy_controlled AT THE CORPUS'S OWN SOURCE SHAPE. Every integer
 * `cqrt_copy_*_controlled` in the corpus copies a rail CQ_lang just allocated
 * with a literal — all-constant with SET bits — into another one. That is PRD
 * §9 row A's path: a constant ONE source lane is a CONTROL fold, which is
 * semantic and survives the promotion, so it becomes `X -> promote -> CX` and
 * materialises the destination lane, while a constant ZERO lane folds away
 * entirely. The case above uses a quantum source and cannot see either. */
CQ_TEST(copy_controlled_at_the_corpus_source_shape_is_one_cx_per_set_bit)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t src  = cqrt_alloc_i8(107);        /* 0b0110_1011, popcount 5 */
    const int32_t dst  = cqrt_alloc_i8(0);
    const int32_t flag = cq_bk_reg(ctx, 1u, 1u, 1u);
    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, src), 0);
    cq_mock_reset(&m);
    const uint32_t live0 = cq_qubits_live(&ctx->pool);

    cqrt_copy_i8_controlled(flag, src, dst);

    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX),  5);   /* one per SET source bit */
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CCX), 0);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_X),   0);
    CHECK_EQ(cq_qubits_live(&ctx->pool) - live0, 5);
    CHECK_EQ(cq_pc_value(ctx, dst), 107);
    CHECK_EQ(cq_pc_value(ctx, src), 107);           /* the source never moves */
    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, src), 0);
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ src, dst, flag }, 3u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* THE THREE WRONG ROUTES, RUN SIDE BY SIDE WITH THE RIGHT ONE. src/reg.h names
 * them and says they were "verified against the emitter"; this case is what
 * makes that a test rather than a comment, and it is a CHARACTERISATION — the
 * wrong routes are built by hand here and are not reachable through any shim
 * entry point. Every one of them produces the RIGHT VALUE, which is exactly why
 * a value-only suite cannot see them.
 *
 * Note route (iii)'s totals are EQUAL — 3W gates either way — and only the
 * per-kind tuple separates them. A T-count would too (7 per Toffoli), but a
 * total would not, which is CLAUDE.md's "a count is not an identification" in
 * its sharpest form. */
CQ_TEST(the_three_wrong_cswap_routes_give_the_right_value_and_the_wrong_cost)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const uint32_t W = 4u;

    /* THE RIGHT ROUTE FIRST, THROUGH THE REAL ENTRY POINT, so this case can go
     * red for a change to `cqrt_cswap` at all. Without it the whole case is a
     * characterisation of M05 and M06 and nothing about the shim — substituting
     * any of the three routes for the shipped implementation would leave it
     * green, which is not what its own comment claims. */
    {
        const int32_t a = cq_bk_reg(ctx, W, 0x3u, 0xFu);
        const int32_t b = cq_bk_reg(ctx, W, 0xCu, 0xFu);
        const int32_t f = cq_bk_reg(ctx, 1u, 1u, 1u);
        cq_mock_reset(&m);
        cqrt_cswap(f, a, b);
        CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX),  2u * W);
        CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CCX), W);
        CHECK_EQ(cq_pc_value(ctx, a), 0xC);
    }

    /* (i) the Fredkin with a CQ_BIT_ZERO control: gates 1 and 3 survive,
     *     because only the middle CCX carries the control. PRD prices 0. */
    {
        const int32_t a = cq_bk_reg(ctx, W, 0x3u, 0xFu);
        const int32_t b = cq_bk_reg(ctx, W, 0xCu, 0xFu);
        const int32_t z = cq_bk_reg(ctx, 1u, 0u, 0u);
        cq_bit *pa = cq_reg_bits(&ctx->regs, a), *pb = cq_reg_bits(&ctx->regs, b);
        const cq_bit *f = cq_reg_cbits(&ctx->regs, z);
        cq_mock_reset(&m);
        for (uint32_t i = 0; i < W; i++) {
            cq_emit_cx (ctx, &pb[i], &pa[i]);
            cq_emit_ccx(ctx, &f[0], &pa[i], &pb[i]);
            cq_emit_cx (ctx, &pb[i], &pa[i]);
        }
        CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX), 8);    /* 2W, not 0 */
        CHECK_EQ(cq_pc_value(ctx, a), 0x3);             /* value still right */
    }

    /* (ii) the Fredkin with a CQ_BIT_ONE control, against a destination rail
     *      that is entirely CLASSICAL — the corpus's own shape, where `b` is a
     *      fresh cqrt_alloc rail. The middle Toffoli folds to a CX and W wires
     *      are materialised on a rail that owned none: I4 violated in silence,
     *      where cq_reg_swap_bits costs nothing. */
    {
        const int32_t a = cq_bk_reg(ctx, W, 0x3u, 0xFu);
        const int32_t b = cqrt_alloc_i8(0);
        const int32_t o = cq_bk_reg(ctx, 1u, 1u, 0u);
        cq_bit *pa = cq_reg_bits(&ctx->regs, a), *pb = cq_reg_bits(&ctx->regs, b);
        const cq_bit *f = cq_reg_cbits(&ctx->regs, o);
        cq_mock_reset(&m);
        const uint32_t live0 = cq_qubits_live(&ctx->pool);
        for (uint32_t i = 0; i < W; i++) {
            cq_emit_cx (ctx, &pb[i], &pa[i]);
            cq_emit_ccx(ctx, &f[0], &pa[i], &pb[i]);
            cq_emit_cx (ctx, &pb[i], &pa[i]);
        }
        CHECK(cq_mock_count(&m) > 0);
        CHECK_EQ(cq_qubits_live(&ctx->pool) - live0, W);   /* W wires, spec: 0 */
        CHECK_EQ(cq_pc_value(ctx, b), 0x3);                /* value still right */
    }

    /* (iii) a §9 region around an UNCONTROLLED three-CNOT swap. Every CX
     *       promotes, so it is 3W Toffolis where the Fredkin is 2W CX + W CCX —
     *       the same TOTAL and three times the T-count. */
    {
        const int32_t a    = cq_bk_reg(ctx, W, 0x3u, 0xFu);
        const int32_t b    = cq_bk_reg(ctx, W, 0xCu, 0xFu);
        const int32_t flag = cq_bk_reg(ctx, 1u, 1u, 1u);
        cq_bit *pa = cq_reg_bits(&ctx->regs, a), *pb = cq_reg_bits(&ctx->regs, b);
        const cq_bit *f = cq_reg_cbits(&ctx->regs, flag);
        cq_mock_reset(&m);
        cq_ctrl_push(ctx, &f[0]);
        for (uint32_t i = 0; i < W; i++) {
            cq_emit_cx(ctx, &pb[i], &pa[i]);
            cq_emit_cx(ctx, &pa[i], &pb[i]);
            cq_emit_cx(ctx, &pb[i], &pa[i]);
        }
        cq_ctrl_pop(ctx);
        CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CCX), 12);  /* 3W Toffolis      */
        CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX),  0);
        CHECK_EQ(cq_mock_count(&m), 12);                /* the SAME total as
                                                         * 2W CX + W CCX     */
        CHECK_EQ(cq_pc_value(ctx, a), 0xC);             /* value still right */
    }

    /* Every handle here is block-scoped, so the SET check is not expressible
     * without restructuring; the sweep needs no handle list, and route (ii) —
     * which mints W wires the specification prices at zero — is exactly the
     * shape its owned-twice and on-the-free-list clauses exist for. */
    cq_reg_audit(ctx);

    close_with(&m);
}

/* -------------------------------------------------------------------------
 * 8. xorc and addc — the two classical-immediate families.
 * ------------------------------------------------------------------------- */

/* `h ^= imm` HAS NO CARRY, so it is one cq_emit_x per SET immediate bit and
 * nothing else — correct for a constant target and for a qubit target through
 * the same call, which is PRD §9 row A working for free. On an all-classical
 * rail the target fold rewrites the constant in place, so the whole thing costs
 * nothing: that is what keeps the corpus's classical loop counters in the I4
 * row (PRD §15 D15). A negative immediate is the expensive half and is pinned,
 * because -1 at i8 is eight set bits. */
CQ_TEST(xorc_is_one_x_per_set_immediate_bit_and_free_on_a_classical_rail)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    /* classical: zero gates, zero qubits, and the value moves. */
    const int32_t c = cqrt_alloc_i8(0x33);
    const cq_pc_snap before = cq_pc_take(ctx);
    cqrt_xorc_i8(c, 0x0F);
    CHECK_EQ(cq_pc_value(ctx, c), 0x3C);
    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));

    /* on wires: one X per set bit of the immediate, and no new wires. */
    const int32_t q = cq_bk_reg(ctx, 8u, 0x00u, 0xFFu);
    cq_mock_reset(&m);
    const cq_pc_snap b2 = cq_pc_take(ctx);
    cqrt_xorc_i8(q, 0x0F);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_X), 4);
    CHECK_EQ(cq_mock_count(&m), 4);
    CHECK(cq_pc_same(b2, cq_pc_take(ctx)));
    CHECK_EQ(cq_pc_value(ctx, q), 0x0F);

    /* a negative immediate is its own width's pattern: -1 at i8 is eight bits. */
    cq_mock_reset(&m);
    cqrt_xorc_i8(q, -1);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_X), 8);
    CHECK_EQ(cq_pc_value(ctx, q), 0xF0);

    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ c, q }, 2u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* THE CLASSICAL FOLD IS MANDATORY, NOT AN OPTIMISATION (bd dzj, PRD §15 D17).
 * PRD §15 D15 measured 40 of the corpus's 45 I4 frees on rails written only by
 * cqrt_alloc + cqrt_addc; making addc materialise collapses that to 5. So a
 * classical rail must come out of an add still owning zero qubits. */
CQ_TEST(addc_folds_on_an_all_classical_rail_at_zero_cost)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const cq_pc_snap before = cq_pc_take(ctx);

    /* -56 IS 200 AT i8, and it has to be spelled that way: the ABI's parameter
     * is int8_t, so `cqrt_alloc_i8(200)` is a -Werror,-Wconstant-conversion
     * diagnostic before it is anything else. The rail is a bit pattern. */
    const int32_t h = cqrt_alloc_i8(-56);
    cqrt_addc_i8(h, 100);                       /* (200 + 100) mod 256 */
    CHECK_EQ(cq_pc_value(ctx, h), 44);

    const int32_t k = cqrt_alloc_i32(5);
    cqrt_addc_i32(k, -1);                       /* the corpus's own shape */
    CHECK_EQ(cq_pc_value(ctx, k), 4);

    const int32_t w = cqrt_alloc_i64(0);
    cqrt_addc_i64(w, -1);
    CHECK(cq_pc_value(ctx, w) == UINT64_C(0xFFFFFFFFFFFFFFFF));

    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, h), 0);

    /* At an all-classical mask `minted` is 0, so this is a restatement of I4
     * rather than an L2 set check — it is here for the sweep beside it and to
     * keep the file uniform, and it must not be counted as leak coverage. */
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ h, k, w }, 3u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* TWO ROWS SHORT-CIRCUIT BEFORE THE ACCUMULATOR AND BOTH ARE PORTS RATHER THAN
 * PEEPHOLES. `imm == 0` is upstream's own identity peephole
 * (Bennett arith.jl's _try_identity_peephole!), and `W == 1` is `h ^= imm`,
 * because addition mod 2 has no carry — which is also why cqrt_addc_i1 takes a
 * `bool`. Both must hold on a QUBIT-OWNING rail, where the general path would
 * otherwise mint W+1 transients. */
CQ_TEST(addc_short_circuits_a_zero_immediate_and_a_one_bit_rail)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t q = cq_bk_reg(ctx, 8u, 0x0Bu, 0xFFu);
    cq_mock_reset(&m);
    const cq_pc_snap before = cq_pc_take(ctx);

    cqrt_addc_i8(q, 0);
    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    CHECK_EQ(cq_pc_value(ctx, q), 0x0B);

    /* W == 1 on a wire: exactly one X, no ancilla, no immediate rail. */
    const int32_t one = cq_bk_reg(ctx, 1u, 0u, 1u);
    cq_mock_reset(&m);
    const cq_pc_snap b2 = cq_pc_take(ctx);
    cqrt_addc_i1(one, true);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_X), 1);
    CHECK_EQ(cq_mock_count(&m), 1);
    CHECK(cq_pc_same(b2, cq_pc_take(ctx)));
    CHECK_EQ(cq_pc_value(ctx, one), 1);

    /* Both short-circuits must leave the pool alone as a SET, not only as a
     * count: the path they skip mints W+1 transients and frees them, which is
     * precisely a net-zero shape if it ever half-ran. */
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ q, one }, 2u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* THE QUANTUM PATH IS M15's CUCCARO ACCUMULATOR, IN PLACE AND NOT IN A
 * SANDWICH — bd dzj's trap 1, and the reason it matters is that Rule 8's driver
 * would replay the compute half in reverse and UNDO the in-place write, leaving
 * `h` unchanged behind a perfectly palindromic trace.
 *
 * THE COST IS ASKED OF M15 RATHER THAN WRITTEN DOWN, and it has THREE terms
 * rather than two — the third was missing from the first draft of this case and
 * from nothing else, because every fixture it had made the term zero:
 *
 *     cq_addacc_steps(W)  +  2 * popcount(imm)  +  popcount(h's classical ONE lanes)
 *
 * The first is M15's own (writing `6W-5` here would be a second transcription of
 * a formula that is already M15's, and would not go red if the construction
 * moved). The second is the immediate rail materialised up and driven back down.
 * The THIRD is one X per classical lane of `h` that holds a ONE — because
 * `cq_materialise` emits the birth X for a set constant — and it is why the
 * mixed fixture below carries ONES rather than the all-ZERO classical lanes a
 * first draft used. With value 0x0B under mask 0x0F every classical lane happens
 * to be zero, so a materialisation that DROPPED the lane's value was
 * indistinguishable from the correct one: measured, that mutant survived the
 * whole 245-test suite. */
CQ_TEST(addc_on_a_qubit_owning_rail_is_cuccaro_in_place)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t h = cq_bk_reg(ctx, 8u, 0x0Bu, 0xFFu);   /* 11, all wires */
    cq_mock_reset(&m);

    cqrt_addc_i8(h, 3);

    CHECK_EQ(cq_pc_value(ctx, h), 14);
    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, h), 8);      /* still in place */
    CHECK_EQ(cq_mock_count(&m),
             (size_t)cq_addacc_steps(8) + 2u * 2u);       /* popcount(3) == 2 */

    /* THE MIXED ROW, WITH CLASSICAL ONES. 0xFB is 1111_1011 and the mask leaves
     * lanes 4..7 classical, so FOUR of them hold a ONE: the sum must carry them
     * (0xFB + 3 = 0xFE) and the cost must show four extra materialising X. */
    const int32_t mixed = cq_bk_reg(ctx, 8u, 0xFBu, 0x0Fu);
    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, mixed), 4);
    cq_mock_reset(&m);

    cqrt_addc_i8(mixed, 3);

    CHECK_EQ(cq_pc_value(ctx, mixed), 0xFE);
    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, mixed), 8);
    CHECK_EQ(cq_mock_count(&m),
             (size_t)cq_addacc_steps(8) + 2u * 2u + 4u);
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ h, mixed }, 2u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* THE TRANSIENTS ARE CERTIFIED BY THE CONSTRUCTION AND COME BACK — ALWAYS, AND
 * THAT IS THE WHOLE CASE. `rail_addc` mints W+1 wires it did not have, and
 * `CQ_ZERO_BY_CUCCARO_RESTORE` is the third and last `proven_zero` constant in
 * the library: the ancilla is Bennett's hygiene contract 3, the addend is
 * contract 1 plus the shim's own X-down loop.
 *
 * THE ROTATION-POISONED ROW IS THE ONE THAT MATTERS, and it is where the first
 * draft of this file was wrong in three ways at once. It disposed the transients
 * with the ordinary shadow proof, which on a poisoned rail cannot clear what a
 * general Ry touched — so W-1 provably-clean qubits STRANDED per call, on the
 * path PRD §15 D17 says is the common one, and PERMANENTLY, because D17 also
 * records that landing 2's certificate does not reach these two rails. Worse,
 * the strand consumed D15 §3's ONE-SHOT report on an internal handle, so the
 * single line the program ever prints named a rail the caller's program does
 * not contain and the caller's OWN rail then stranded in silence; and worse
 * again, `CQOPS_FREE_ABORT` killed the process inside an arithmetic opcode.
 *
 * So the assertions are: the pool is RESTORED exactly (not "some strands
 * happened", which is what an inequality would say and which a per-rail
 * disposition of the transients would satisfy), the strand count is EXACTLY
 * zero, and the one-shot report is still UNSPENT — the last is what proves the
 * shim did not hijack the caller's diagnostic, and it goes red the moment a
 * transient strands even if the pool bookkeeping happens to balance. */
CQ_TEST(addcs_transients_are_certified_by_the_construction_and_come_back)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    /* (a) determinate. */
    const int32_t h = cq_bk_reg(ctx, 8u, 0x0Bu, 0xFFu);
    const cq_pc_snap before = cq_pc_take(ctx);
    cqrt_addc_i8(h, 3);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));      /* W+1 out, W+1 back */
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0);
    CHECK(cq_pc_live_is_exactly(ctx, &h, 1u));
    cq_reg_audit(ctx);

    /* (b) rotation-poisoned — the row the shadow cannot clear. */
    const int32_t p = cq_bk_reg(ctx, 8u, 0x00u, 0xFFu);
    cqrt_ry_i8(p, 0.5);
    const cq_pc_snap b2 = cq_pc_take(ctx);
    cqrt_addc_i8(p, 3);
    CHECK(cq_pc_same(b2, cq_pc_take(ctx)));
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);         /* the caller's to spend */
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ h, p }, 2u));
    cq_reg_audit(ctx);

    /* (c) AND THE CALLER'S OWN FREE IS STILL THE ONE THAT SPEAKS. Freeing the
     *     poisoned rail is a genuine unproven strand; it must name THAT handle,
     *     and it can only do so because (b) left the one-shot unspent. */
    cqrt_free(p);
    CHECK_EQ(cq_reg_strand_reports(ctx), 1);
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 8);

    /* (d) AND THE HANDLE TABLE IS *NOT* RESTORED, WHICH IS THE HALF EVERY POOL
     *     CHECK ABOVE IS BLIND TO. D5 handles are monotonic and a tombstone is
     *     never reclaimed, so the quantum path advances the counter by exactly
     *     2 and leaves two DEAD slots for good, while the classical fold
     *     advances it by 0 — D17's fold rule seen from the TABLE rather than
     *     from the gate count. This is a CHARACTERISATION, not a guard: the
     *     fold rule itself is already killed by
     *     `addc_folds_on_an_all_classical_rail_at_zero_cost`. Its value is that
     *     the seam paragraph in shim/cq_runtime_rail.c is careful about exactly
     *     this sentence, and a shipped comment asserting an unmeasured fact is
     *     this project's own recorded failure mode. */
    {
        const int32_t n0 = cq_reg_count(&ctx->regs);
        const int32_t c  = cq_bk_reg(ctx, 8u, 0x0Bu, 0x00u);   /* all classical */
        cqrt_addc_i8(c, 3);
        CHECK_EQ(cq_reg_count(&ctx->regs), n0 + 1);            /* the rail only */

        const int32_t n1 = cq_reg_count(&ctx->regs);
        cqrt_addc_i8(h, 3);                                    /* qubit-owning  */
        CHECK_EQ(cq_reg_count(&ctx->regs), n1 + 2);
        CHECK_EQ(cq_reg_state(&ctx->regs, n1),     CQ_SLOT_DEAD);
        CHECK_EQ(cq_reg_state(&ctx->regs, n1 + 1), CQ_SLOT_DEAD);
    }

    close_with(&m);
}

/* CQOPS_FREE_ABORT MUST NOT FIRE INSIDE AN ARITHMETIC OPCODE, and nothing else
 * in the tree exercises the flag through the shim at all. Under it, a free the
 * library cannot clear terminates — that is the point of the flag — so an addc
 * whose internal transients were disposed on shadow evidence would kill the
 * process on the corpus's common path, naming a handle the caller never made.
 * The certificate is what makes this row green, and this case is what would go
 * red if anyone reverted it to the shadow proof. */
CQ_TEST(addc_survives_cqops_free_abort_on_a_rotation_tainted_rail)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t p = cq_bk_reg(ctx, 8u, 0x00u, 0xFFu);
    cqrt_ry_i8(p, 0.5);

    cqops_set_free_abort(1);
    cqrt_addc_i8(p, 3);                 /* would abort on any strand */
    cqops_set_free_abort(-1);           /* negative returns to the environment */

    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0);
    CHECK(cq_pc_live_is_exactly(ctx, &p, 1u));

    close_with(&m);
}

/* THE QUANTUM PATH AT MORE THAN ONE WIDTH, because W = 8 is the only one the
 * cases above use and the construction is width-generic. i1 is the short-circuit
 * row, i16/i32/i64 are the general one, and the top-bit-set immediate is the
 * expensive half D17 names — `addc(h, -1)` at i32 is 0xFFFFFFFF, so 32 X up and
 * 32 down. */
CQ_TEST(addc_is_width_generic_and_a_negative_immediate_is_the_expensive_half)
{
    static const uint32_t WS[] = { 16u, 32u, 64u };
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    /* EVERY EARLIER WIDTH'S RAIL STAYS LIVE, so the L2 set claim ACCUMULATES —
     * a single-handle list would be wrong from the second iteration on, and
     * "wrong" here means red for a bookkeeping reason, whose tempting fix is to
     * delete the assertion. */
    int32_t hs[4]; uint32_t nh = 0u;

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        const uint32_t w = WS[i];
        const int32_t h = cq_bk_reg(ctx, w, 0x0Bu, (w >= 64u) ? ~UINT64_C(0)
                                                             : ((UINT64_C(1) << w) - 1u));
        hs[nh++] = h;
        const cq_pc_snap before = cq_pc_take(ctx);
        cq_mock_reset(&m);

        if (w == 16u)      cqrt_addc_i16(h, 3);
        else if (w == 32u) cqrt_addc_i32(h, 3);
        else               cqrt_addc_i64(h, 3);

        CHECK_EQ(cq_pc_value(ctx, h), 14);
        CHECK_EQ(cq_mock_count(&m), (size_t)cq_addacc_steps((int)w) + 4u);
        CHECK(cq_pc_same(before, cq_pc_take(ctx)));
        CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0);
        CHECK(cq_pc_live_is_exactly(ctx, hs, nh));
    }

    /* -1 at i32: every immediate bit set, so 2*32 extra gates and a wrap. */
    const int32_t k = cq_bk_reg(ctx, 32u, 0x5u, 0xFFFFFFFFu);
    hs[nh++] = k;
    cq_mock_reset(&m);
    cqrt_addc_i32(k, -1);
    CHECK_EQ(cq_pc_value(ctx, k), 4);
    CHECK_EQ(cq_mock_count(&m), (size_t)cq_addacc_steps(32) + 2u * 32u);

    CHECK(cq_pc_live_is_exactly(ctx, hs, nh));
    cq_reg_audit(ctx);
    close_with(&m);
}

/* A MEASURED RAIL IS A LEGAL COPY SOURCE, WHICH IS THE READ HALF OF `rail_r`
 * AGAINST `rail_w`. `cq_reg_cbits` admits a MEASURED rail and `cq_reg_bits`
 * refuses one, so on this surface the asymmetry is spelled by WHICH HELPER an
 * operand goes through: `rail_copy` sends `src` to `rail_r` and `dst` to
 * `rail_w`, and that one letter is the whole of measurement's terminality here.
 *
 * IT HAD TO BE A POSITIVE CASE. Measured 2026-08-27 (`bd pnu`, mutant
 * rail-R05): hardening `rail_r` to `cq_reg_bits` survived the whole 258-test
 * suite in both configurations, because nothing anywhere read a measured rail
 * through the shim — and no DEATH case can convict a mutant whose effect is to
 * refuse something legal. The WRITE half is the death case
 * `test_runtime_rail_death.xorc_of_a_measured_rail_is_a_write_refusal`
 * (mutant rail-R04, which survived too); neither direction is pinned alone.
 *
 * THE COPY IS ASSERTED BY WHAT MOVED, not merely by a count: `dst` is all
 * constants and gains one wire per source lane, and the value it ends up
 * holding is the source's. A refusal would abort, so the case's first claim is
 * simply that the process is still running — which is why it also pins the
 * stream, so that "it did not abort" cannot be the whole of what it checks. */
CQ_TEST(a_measured_rail_is_a_legal_copy_source_and_an_illegal_destination)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t src = cq_bk_reg(ctx, 8u, 0xA5u, 0xFFu);   /* eight wires */
    const int32_t dst = cqrt_alloc_i8(0);                   /* all constant */

    CHECK_EQ((int)cqrt_measure_i8(src), (int)(int8_t)0xA5);
    CHECK_EQ(cq_reg_state(&ctx->regs, src), CQ_SLOT_MEASURED);

    cq_mock_reset(&m);
    cqrt_copy_i8(src, dst);

    CHECK_EQ(cq_mock_count(&m), 8);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX), 8);
    CHECK_EQ(cq_pc_value(ctx, dst), 0xA5u);
    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, dst), 8u);
    CHECK_EQ(cq_reg_state(&ctx->regs, src), CQ_SLOT_MEASURED);

    close_with(&m);
}
