/* tests/test_reg_free.inc — M07's FREE-TIME DISPOSITION. PRD §15 D15 §3/§4.
 *
 * Included by test_reg.c rather than compiled on its own, so it shares that
 * file's fixture, its materialise_bit helper and its zero-proof.
 *
 * THE SEAM IS `the TABLE and its LIFECYCLE ↔ the FREE-TIME DISPOSITION`, and it
 * was recorded in IMPLEMENTATION_PLAN §3 before this file was written (Rule 12:
 * a split is scheduled, never improvised). test_reg.c had 16 lines of headroom
 * against the 300 limit when Step 23 arrived, and the arriving subject is a
 * single coherent one — what a free DECIDES, and what it then DOES — rather
 * than an arbitrary tail. The four pre-existing free-path cases moved here with
 * it; leaving them behind would have made the seam a size cut rather than a
 * subject one. Its source-side sibling is src/reg_check.c, taken the same day
 * on plan §3's OTHER recorded seam (`table ↔ invariant checking`).
 *
 * WHAT THIS FILE IS ABOUT, IN ONE SENTENCE. The epistemic state at a free is
 * THREE-valued — proven-clean, proven-dirty, unproven — and the ACT is
 * TWO-valued: proven-clean releases, and proven-dirty and unproven ALIKE are
 * STRANDED. Keeping those two apart is the whole of D15 §3 as amended by §4's
 * last clause, confirmed 2026-08-22.
 *
 * WHY THE ACT COLLAPSES AND THE VERDICT DOES NOT. Rule 6's hard error is about
 * a RELEASE of a non-|0⟩ index, and neither non-clean row reaches the pool, so
 * nothing is laundered either way — which is the whole of what Rule 6 protects.
 * What the two rows are NOT allowed to lose is their distinctness in the
 * REPORT: D15 §3's residue split is `bd 06t`'s first obligation, and a free
 * path that could not tell a conviction from an absence of evidence could never
 * produce it. So every case below asserts the VERDICT and the ACT separately.
 *
 * DELIBERATELY NOT USING poolcheck's cq_pc_live_is_exactly. A stranded index is
 * not free and, once cq_reg_free tombstones the rail, is owned by nobody — so
 * it reads to L2 as "a leaked ancilla" (`bd evv`). These cases assert the pool
 * by hand and by INDEX, which is sharper than L2 here anyway: L2 would say
 * "something leaked", and what has to be pinned is exactly WHICH index went to
 * the free list and which was stranded. */

/* THE THREE-VALUED PROOF, and it is a strict refinement of test_reg.c's
 * two-valued one rather than a different oracle. cq_shadow_known_zero conflates
 * "unknown" with "known 1" — D15 §3's opening sentence — and reading the raw
 * pair splits them: `unknown` first, always, because an entry poisoned while it
 * happened to hold 0 still carries a zero value byte and calling that CLEAN is
 * the laundering the shadow's own header forbids by name.
 *
 * It is sound only where proof_shadow_pre_kernel_only is sound (see test_reg.c)
 * and for the same reason. What it adds is the CONVICTION: the two-valued form
 * cannot produce D15's proven-dirty row at all, so without this the row would
 * be untestable and the ACT's collapse would be asserted against an empty
 * population. */
static int proof_shadow_three_valued(const cq_ctx *ctx, int32_t h, uint32_t q)
{
    cq_shadow s = cq_shadow_get(&ctx->shadow, q);
    (void)h;
    if (s.unknown)   return CQ_PROOF_UNPROVEN;
    return s.value == 0 ? CQ_PROOF_CLEAN : CQ_PROOF_DIRTY;
}

/* A qubit the library can SEE is not |0⟩: materialise a constant ONE, which
 * emits an X and leaves the shadow determinate-and-non-zero. This is the only
 * shape that reaches D15's proven-dirty row through the shadow, and it is why
 * the row was empty before Step 23 — nothing in the tree built one and then
 * asked for a verdict. Returns the qubit index. */
static uint32_t dirty_bit(fixture *f, int32_t h, uint32_t i)
{
    cq_bit *b = &cq_reg_bits(&f->ctx.regs, h)[i];
    *b = cq_bit_one();
    cq_materialise(&f->ctx, b);
    return cq_bit_qindex(*b);
}

/* --- The four cases that moved with the seam. ---------------------------- */

CQ_TEST(freeing_an_all_constant_rail_needs_no_evidence)
{
    fixture f; fx_open(&f);
    /* `int x = 5;` going out of scope. By I4 the rail owns zero qubits, so
     * nothing can reach the free list and nothing can collapse — which is why
     * Rule 6 is scoped to QUBIT-CARRYING bits. Read literally ("every bit is
     * BIT_ZERO or a known-zero qubit") this line aborts, and L5's zero-cost
     * classical path becomes unreachable. PRD §10 is amended to match.
     *
     * IT MUST KEEP FREEING WITH NO EVIDENCE AT ALL, and that is a Step 23
     * requirement rather than a leftover: `bd 06t` names these rails
     * explicitly — the ones the certificate never reaches, because I4 already
     * silences them — and forbids a certificate from regressing them. */
    int32_t h = cq_reg_alloc_const(&f.ctx.regs, 32, 5u, 0u);
    CHECK(cq_reg_clean(&f.ctx, h, NULL));
    CHECK(cq_reg_disposition(&f.ctx, h, NULL) > 0);
    cq_reg_free(&f.ctx, h, NULL);
    CHECK_EQ(cq_reg_state(&f.ctx.regs, h), CQ_SLOT_DEAD);
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), 0);
    CHECK_EQ(cq_qubits_minted(&f.ctx.pool), 0);
    CHECK_EQ(cq_qubits_free(&f.ctx.pool), 0);
    CHECK_EQ(cq_qubits_stranded(&f.ctx.pool), 0);
    fx_close(&f);
}

CQ_TEST(free_returns_exactly_the_indices_the_rail_held)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 4);
    uint32_t held[4]; uint32_t n = 0;

    for (uint32_t i = 0; i < 4; i++)
        held[n++] = materialise_bit(&f, h, i, CLEAN_CTRL);
    uint32_t before = cq_qubits_free(&f.ctx.pool);

    cq_reg_free(&f.ctx, h, proof_shadow_pre_kernel_only);

    CHECK_EQ(cq_qubits_free(&f.ctx.pool), before + n);
    /* The SET matters, not just the count: clearing a bit before releasing its
     * index erases the index (a constant carries a canonical q == 0) and leaks
     * the qubit in silence, and a count-only assertion stays green. */
    for (uint32_t i = 0; i < n; i++) CHECK(cq_qubits_is_free(&f.ctx.pool, held[i]));
    CHECK_EQ(cq_qubits_stranded(&f.ctx.pool), 0);
    fx_close(&f);
}

CQ_TEST(clean_is_false_when_the_proof_refuses)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 4);
    (void)materialise_bit(&f, h, 2, POISONED_CTRL);   /* shadow now unknown */

    /* THE MUTATION-PROOF ASSERTION, and it is an ORDINARY test rather than a
     * death test on purpose. If M07's evidence forwarding were deleted, a
     * death test would still see an abort — from M03's own
     * `if (proven_zero <= 0) cq_pool_die(...)` one layer down — and would pass
     * on a broken library. This one has no backstop beneath it. */
    CHECK(!cq_reg_clean(&f.ctx, h, proof_shadow_pre_kernel_only));
    CHECK(!cq_reg_clean(&f.ctx, h, NULL));   /* no evidence => not clean */
    fx_close(&f);
}

CQ_TEST(clean_is_total_over_the_rail_before_anything_is_released)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 4);
    (void)materialise_bit(&f, h, 0, CLEAN_CTRL);
    (void)materialise_bit(&f, h, 1, CLEAN_CTRL);
    (void)materialise_bit(&f, h, 3, POISONED_CTRL);   /* the LAST bit is bad */

    /* Two clean qubits precede the bad one, so a predicate that stopped at the
     * first bit — or that never looked past it — would say "clean". */
    CHECK(!cq_reg_clean(&f.ctx, h, proof_shadow_pre_kernel_only));
    CHECK_EQ(cq_qubits_free(&f.ctx.pool), 0);   /* nothing released while asking */
    fx_close(&f);
}

/* --- D15 §3: the VERDICT is three-valued. -------------------------------- */

CQ_TEST(the_disposition_is_three_valued_and_dirty_dominates_unproven)
{
    fixture f; fx_open(&f);

    /* Clean only. */
    int32_t a = cq_reg_alloc_zero(&f.ctx.regs, 4);
    (void)materialise_bit(&f, a, 0, CLEAN_CTRL);
    (void)materialise_bit(&f, a, 1, CLEAN_CTRL);
    CHECK(cq_reg_disposition(&f.ctx, a, proof_shadow_three_valued) > 0);

    /* Clean + unproven — no conviction anywhere, so the rail is UNPROVEN. */
    int32_t b = cq_reg_alloc_zero(&f.ctx.regs, 4);
    (void)materialise_bit(&f, b, 0, CLEAN_CTRL);
    (void)materialise_bit(&f, b, 3, POISONED_CTRL);
    CHECK_EQ(cq_reg_disposition(&f.ctx, b, proof_shadow_three_valued), 0);

    /* Clean + unproven + dirty. DIRTY DOMINATES, and the dirty bit is placed
     * AFTER the unproven one on purpose: a fold that returned at the first
     * non-clean bit would answer UNPROVEN here and would be indistinguishable
     * from the rail above, which is exactly the residue split D15 §3 says must
     * be producible. The reverse order is covered below. */
    int32_t c = cq_reg_alloc_zero(&f.ctx.regs, 4);
    (void)materialise_bit(&f, c, 0, CLEAN_CTRL);
    (void)materialise_bit(&f, c, 1, POISONED_CTRL);
    (void)dirty_bit(&f, c, 3);
    CHECK(cq_reg_disposition(&f.ctx, c, proof_shadow_three_valued) < 0);

    /* Dirty FIRST, unproven after — the same verdict, so the dominance is a
     * property of the fold and not of the bit order. */
    int32_t d = cq_reg_alloc_zero(&f.ctx.regs, 4);
    (void)dirty_bit(&f, d, 0);
    (void)materialise_bit(&f, d, 2, POISONED_CTRL);
    CHECK(cq_reg_disposition(&f.ctx, d, proof_shadow_three_valued) < 0);

    fx_close(&f);
}

CQ_TEST(clean_is_the_positive_row_of_the_disposition_and_its_value_is_one)
{
    fixture f; fx_open(&f);

    /* THE SEVEN EXTERNAL CALL SITES OF cq_reg_clean ARE WHY THIS IS PINNED AS
     * A LITERAL. Two of them read it as a number rather than as a truth value —
     * tests/test_unc_asym.inc does CHECK_EQ(cx.clean, 1), and `if (ry.clean)`
     * there prints a `bd 2cf` REGRESSION message on its true branch. Number the
     * verdicts so that UNPROVEN is anything but 1 and that second site reports a
     * regression that did not happen, with a misleading message, on an unproven
     * rail. CQ_PROOF_CLEAN == 1 and CQ_PROOF_UNPROVEN == 0 keep every one of the
     * seven meaning exactly what it meant before Step 23. */
    CHECK_EQ(CQ_PROOF_CLEAN, 1);
    CHECK_EQ(CQ_PROOF_UNPROVEN, 0);
    CHECK(CQ_PROOF_DIRTY < 0);

    int32_t a = cq_reg_alloc_zero(&f.ctx.regs, 2);
    (void)materialise_bit(&f, a, 0, CLEAN_CTRL);
    CHECK_EQ(cq_reg_clean(&f.ctx, a, proof_shadow_three_valued), 1);

    int32_t b = cq_reg_alloc_zero(&f.ctx.regs, 2);
    (void)materialise_bit(&f, b, 0, POISONED_CTRL);
    CHECK_EQ(cq_reg_clean(&f.ctx, b, proof_shadow_three_valued), 0);

    int32_t c = cq_reg_alloc_zero(&f.ctx.regs, 2);
    (void)dirty_bit(&f, c, 0);
    /* THE ROW THAT WOULD BREAK THE SEVEN. cq_reg_clean is a BOOLEAN and a
     * conviction is a NEGATIVE int, so a wrapper spelled `!= 0` or `return
     * cq_reg_disposition(...)` hands every one of those sites a non-zero
     * "clean" for the dirtiest rail the library can recognise. `> 0` is the
     * only spelling that is right, and this is its detector. */
    CHECK_EQ(cq_reg_clean(&f.ctx, c, proof_shadow_three_valued), 0);

    fx_close(&f);
}

/* --- D15 §4: the ACT is two-valued. -------------------------------------- */

CQ_TEST(an_unproven_rail_strands_every_qubit_it_owns_and_releases_none)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 4);
    uint32_t held[4];

    for (uint32_t i = 0; i < 4; i++)
        held[i] = materialise_bit(&f, h, i, POISONED_CTRL);

    uint32_t live_before = cq_qubits_live(&f.ctx.pool);
    uint32_t free_before = cq_qubits_free(&f.ctx.pool);

    CHECK_EQ(cq_reg_disposition(&f.ctx, h, proof_shadow_three_valued), 0);
    cq_reg_free(&f.ctx, h, proof_shadow_three_valued);

    /* NAMED, NOT COUNTED. Strand the wrong index and every total still agrees;
     * after the free tombstones the rail the index is unrecoverable from the
     * register side, so this predicate is the only thing that can name it. */
    for (uint32_t i = 0; i < 4; i++) {
        CHECK(cq_qubits_is_stranded(&f.ctx.pool, held[i]));
        CHECK(!cq_qubits_is_free(&f.ctx.pool, held[i]));
    }
    CHECK_EQ(cq_qubits_stranded(&f.ctx.pool), 4);

    /* NEITHER STRUCTURAL IDENTITY MOVES, and that is the mechanism rather than
     * a side effect: a stranded index is still LIVE because nobody got it back.
     * A `p->live--` inside cq_qubits_strand is the entire bug D15 §3 measured a
     * third pool bucket into. */
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), live_before);
    CHECK_EQ(cq_qubits_free(&f.ctx.pool), free_before);
    CHECK_EQ(cq_qubits_minted(&f.ctx.pool),
             cq_qubits_live(&f.ctx.pool) + cq_qubits_free(&f.ctx.pool));
    CHECK_EQ(cq_qubits_peak(&f.ctx.pool), cq_qubits_minted(&f.ctx.pool));

    CHECK_EQ(cq_reg_state(&f.ctx.regs, h), CQ_SLOT_DEAD);   /* the program continues */
    fx_close(&f);
}

CQ_TEST(a_proven_dirty_rail_takes_the_same_act_as_an_unproven_one)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 4);
    uint32_t held[2];

    held[0] = dirty_bit(&f, h, 0);
    held[1] = dirty_bit(&f, h, 2);

    uint32_t live_before = cq_qubits_live(&f.ctx.pool);

    /* D15 §4's LAST CLAUSE, CONFIRMED 2026-08-22 AS (b) STRAND, and this case
     * is what makes the confirmation falsifiable rather than editorial. §3 as
     * drafted made this row a HARD ERROR IN BOTH CONFIGURATIONS; §4's callout
     * supersedes it — "a rail the certificate convicts takes the same act as
     * the unproven row … so the free path has two dispositions, not three".
     * The VERDICT stays distinct and is asserted separately one line down; it
     * is only the ACT that collapses. Take the other side and this case does
     * not merely fail, it cannot run: the process aborts. */
    CHECK(cq_reg_disposition(&f.ctx, h, proof_shadow_three_valued) < 0);
    cq_reg_free(&f.ctx, h, proof_shadow_three_valued);

    for (uint32_t i = 0; i < 2; i++) {
        CHECK(cq_qubits_is_stranded(&f.ctx.pool, held[i]));
        CHECK(!cq_qubits_is_free(&f.ctx.pool, held[i]));
    }
    CHECK_EQ(cq_qubits_stranded(&f.ctx.pool), 2);
    CHECK_EQ(cq_qubits_free(&f.ctx.pool), 0);
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), live_before);
    fx_close(&f);
}

CQ_TEST(the_free_is_per_qubit_so_a_partly_clean_rail_splits)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 4);

    /* D15 §3 says the disposition is PER QUBIT, and this is the only case that
     * can tell a per-qubit pass 2 from a per-rail one. A per-rail act — strand
     * the whole thing because one bit failed — passes every other case in this
     * file, because every other rail is uniform. It also leaks two provably
     * clean qubits per rail on the corpus, which nothing would ever shout
     * about: the pool just grows. */
    uint32_t clean0 = materialise_bit(&f, h, 0, CLEAN_CTRL);
    uint32_t bad1   = materialise_bit(&f, h, 1, POISONED_CTRL);
    uint32_t clean2 = materialise_bit(&f, h, 2, CLEAN_CTRL);
    uint32_t bad3   = dirty_bit(&f, h, 3);

    cq_reg_free(&f.ctx, h, proof_shadow_three_valued);

    CHECK(cq_qubits_is_free(&f.ctx.pool, clean0));
    CHECK(cq_qubits_is_free(&f.ctx.pool, clean2));
    CHECK(!cq_qubits_is_stranded(&f.ctx.pool, clean0));
    CHECK(!cq_qubits_is_stranded(&f.ctx.pool, clean2));

    CHECK(cq_qubits_is_stranded(&f.ctx.pool, bad1));
    CHECK(cq_qubits_is_stranded(&f.ctx.pool, bad3));
    CHECK(!cq_qubits_is_free(&f.ctx.pool, bad1));
    CHECK(!cq_qubits_is_free(&f.ctx.pool, bad3));

    CHECK_EQ(cq_qubits_free(&f.ctx.pool), 2);
    CHECK_EQ(cq_qubits_stranded(&f.ctx.pool), 2);
    fx_close(&f);
}

/* --- CQOPS_FREE_ABORT: how a maintainer turns conviction back into a stop. */

CQ_TEST(free_abort_is_off_by_default_and_the_setter_wins_over_the_environment)
{
    /* MODELLED ON CQOPS_SINK (src/sink.c), including the part that is easy to
     * drop: an unresolvable value is a HARD ERROR, never a quiet substitution.
     * `CQOPS_FREE_ABORT=true` silently meaning OFF would be the worst possible
     * failure of this flag — a maintainer who asked for termination and got
     * silence — so the death case for it lives in test_reg_death.c.
     *
     * NOT THE DEFAULT, and D15 says why in one line: under it NORTH_STAR
     * condition 1 is unreachable by construction, because the corpus's residue
     * would stop every fixture at its first unproven free. */
    unsetenv("CQOPS_FREE_ABORT");
    cqops_set_free_abort(-1);                  /* -1 restores the env default  */
    CHECK(!cq_free_abort_active());            /* unset => absent => off       */

    setenv("CQOPS_FREE_ABORT", "", 1);
    CHECK(!cq_free_abort_active());            /* EMPTY means absent, not "0"  */

    /* THE ENVIRONMENT ON ITS OWN, both spellings. Without these the resolver's
     * whole env branch is untested and `getenv` could return anything. */
    setenv("CQOPS_FREE_ABORT", "1", 1);
    CHECK(cq_free_abort_active());
    setenv("CQOPS_FREE_ABORT", "0", 1);
    CHECK(!cq_free_abort_active());

    /* THE SETTER WINS OVER THE ENVIRONMENT — which is what this case is NAMED
     * for, and what it did not test until the environment was actually set.
     * BOTH DIRECTIONS ARE NEEDED. With the env at "0", only `set(1)` proves the
     * override is consulted; with it at "1", only `set(0)` proves the override
     * can force OFF — and `set(0)` is the direction a `> 0` override test
     * silently loses, because 0 then falls through to an environment that
     * happens to agree. That mutant survives every one-directional version of
     * this case. */
    setenv("CQOPS_FREE_ABORT", "0", 1);
    cqops_set_free_abort(1);
    CHECK(cq_free_abort_active());

    setenv("CQOPS_FREE_ABORT", "1", 1);
    cqops_set_free_abort(0);
    CHECK(!cq_free_abort_active());

    /* And a NEGATIVE argument clears the override, handing control back. */
    cqops_set_free_abort(-1);
    CHECK(cq_free_abort_active());             /* the env's "1" is visible again */

    unsetenv("CQOPS_FREE_ABORT");
    cqops_set_free_abort(-1);
    CHECK(!cq_free_abort_active());
}

/* --- cq_reg_xor_into really is an XOR, and the ABI comment says otherwise. */

CQ_TEST(xor_into_a_nonzero_dst_is_an_xor_and_not_a_copy)
{
    fixture f; fx_open(&f);

    /* THE HAZARD IS THAT EVERY EXISTING TEST STAYS GREEN. Nothing else in
     * tests/ copies into a dst that is not |0>, so `dst = src` and `dst ^= src`
     * agree everywhere the suite looks, and a future agent "fixing"
     * cq_reg_xor_into into a real copy — or adding an assert that dst is zero —
     * would break nothing here and would miscompile the corpus.
     *
     * CQ_LANG'S OWN FIXTURE IS THE WITNESS AND ITS SOURCE SAYS "XOR-delta" IN
     * AS MANY WORDS. tests/e2e/slice_control_cond_onward_phase: `r` is preset
     * to 3 and the tainted switch emits three CONTROLLED COPIES of rails born
     * 4, 10 and 8 — which are exactly 7^3, 9^3 and 11^3, the deltas for its
     * three arms. A copy would leave `r` holding 4/10/8 instead of 7/9/11. So
     * the XOR is load-bearing on the shipped integer surface, not a convenience.
     *
     * The ABI's own header comment spells the trace as "cqrt_copy_<W>(h,h)" and
     * calls it a copy, which is what makes this worth pinning rather than
     * assuming: the NAME says copy and the SEMANTICS are XOR. */
    int32_t dst = cq_reg_alloc_const(&f.ctx.regs, 8, 3u, 0u);    /* r = 3     */
    int32_t src = cq_reg_alloc_const(&f.ctx.regs, 8, 4u, 0u);    /* 7 ^ 3 = 4 */

    cq_reg_xor_into(&f.ctx, dst, src);

    /* 3 ^ 4 == 7, the value the fixture's `case 0: r = 7` needs. A copy would
     * give 4. Both are all-classical, so I4 holds either way and no pool or
     * gate assertion can tell them apart — only the value can. */
    for (uint32_t i = 0; i < 8; i++)
        CHECK_EQ(cq_reg_cbits(&f.ctx.regs, dst)[i].kind,
                 ((7u >> i) & 1u) ? CQ_BIT_ONE : CQ_BIT_ZERO);
    CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, dst), 0);   /* I4: still classical */

    /* AND IT IS SELF-INVERSE, which a copy is not: xoring the same source in
     * twice restores the destination. That is the property Rule 7's whole
     * uncompute axis rests on, one layer down. */
    cq_reg_xor_into(&f.ctx, dst, src);
    for (uint32_t i = 0; i < 8; i++)
        CHECK_EQ(cq_reg_cbits(&f.ctx.regs, dst)[i].kind,
                 ((3u >> i) & 1u) ? CQ_BIT_ONE : CQ_BIT_ZERO);
    fx_close(&f);
}

/* --- cqrt_cswap's constant-ONE row: 0 gates, 0 qubits. ------------------- */

CQ_TEST(swap_bits_exchanges_two_rails_for_zero_gates_and_zero_qubits)
{
    fixture f; fx_open(&f);
    int32_t a = cq_reg_alloc_const(&f.ctx.regs, 8, 0xA5u, 0u);
    int32_t b = cq_reg_alloc_zero(&f.ctx.regs, 8);
    uint32_t qb = materialise_bit(&f, b, 3, CLEAN_CTRL);

    uint32_t minted_before = cq_qubits_minted(&f.ctx.pool);
    cq_mock_reset(&f.mock);

    cq_reg_swap_bits(&f.ctx.regs, a, b);

    /* THE WHOLE POINT, AND THE TRAP IS THAT THREE WRONG ROUTES GIVE THE RIGHT
     * VALUE. PRD §2.1 says a cqrt_cswap with a CONSTANT control is ZERO gates.
     * Route the Fredkin through cq_emit_* with ctrl == CQ_BIT_ZERO and gates 1
     * and 3 survive the fold — two gates where the spec says none. With ctrl ==
     * CQ_BIT_ONE the middle Toffoli folds to a CX and you get the three-CNOT
     * SWAP plus three freshly materialised wires on a rail that was entirely
     * classical — three gates and three qubits where the spec says none, and
     * I4 quietly violated. And pushing a §9 region around an uncontrolled SWAP
     * promotes it to THREE TOFFOLIS rather than 2·CX + 1·CCX. A constant
     * control is a DECISION, not a circuit; this is that decision. */
    CHECK_GATES(cq_mock_count_op(&f.mock, CQ_OP_X),
                cq_mock_count_op(&f.mock, CQ_OP_CX),
                cq_mock_count_op(&f.mock, CQ_OP_CCX), 0, 0, 0);
    CHECK_EQ(cq_mock_count(&f.mock), 0);   /* including rotations and mz */
    CHECK_EQ(cq_qubits_minted(&f.ctx.pool), minted_before);

    /* The exchange itself, by value AND by kind: `a` now carries b's wire and
     * `b` carries a's constants. */
    CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, a), 1);
    CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, b), 0);
    CHECK(cq_bit_is_qubit(cq_reg_cbits(&f.ctx.regs, a)[3]));
    CHECK_EQ(cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, a)[3]), qb);
    for (uint32_t i = 0; i < 8; i++)
        CHECK_EQ(cq_reg_cbits(&f.ctx.regs, b)[i].kind,
                 ((0xA5u >> i) & 1u) ? CQ_BIT_ONE : CQ_BIT_ZERO);

    /* Involutive: swapping back restores both rails exactly. */
    cq_reg_swap_bits(&f.ctx.regs, a, b);
    CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, b), 1);
    CHECK_EQ(cq_bit_qindex(cq_reg_cbits(&f.ctx.regs, b)[3]), qb);
    fx_close(&f);
}

/* --- D15 §3's report: "the FIRST occurrence names the handle on stderr". -- */

CQ_TEST(only_the_first_strand_reports_and_the_program_continues)
{
    fixture f; fx_open(&f);

    /* WHY THE ONE-SHOT MATTERS. Stranding is LOUD BY DESIGN — it is the whole
     * difference between a mechanism and an early return — but the corpus
     * strands its residue across tens of thousands of frees, and a line per
     * qubit would bury the first one under its own noise and would make a
     * maintainer turn the report off. So the requirement is exactly "the
     * first", and that is a claim with a failure mode: report every time, or
     * report never, and nothing else in the tree would notice.
     *
     * PER CONTEXT, NOT PER PROCESS, and the difference is testability. A
     * file-static one-shot is unobservable from the second case onward in any
     * binary, so every case after the first would be asserting nothing. The
     * shim's context is process-global anyway (Step 23), so per-context is a
     * superset of the behaviour D15 describes rather than a weakening.
     *
     * AND IT IS A COUNT OF EMISSIONS, NOT A FLAG — the correction a mutation
     * battery forced. This case read `cq_reg_strand_reported` and asserted it
     * was 1; deleting the one-shot early return in report_first_strand made the
     * line fire once per stranded qubit and STILL set the flag, so the case
     * stayed green in both configurations. It was the only survivor of the
     * battery, and the hole was in this case rather than in the module. An
     * assertion on a flag answers "did it ever report"; the claim is "it
     * reported exactly once".
     *
     * WHAT THIS CASE DOES NOT ASSERT, said out loud rather than implied: the
     * TEXT of the line. Restoring `stderr` after a freopen has no portable
     * C11 spelling, and redirecting it would also swallow a sanitizer report
     * in the one configuration where that matters most. The text is reviewed,
     * not tested; what is tested is that it happens, that it happens once, and
     * that the free path carries on afterwards. */
    CHECK_EQ(cq_reg_strand_reports(&f.ctx), 0);

    int32_t a = cq_reg_alloc_zero(&f.ctx.regs, 2);
    (void)materialise_bit(&f, a, 0, POISONED_CTRL);
    cq_reg_free(&f.ctx, a, proof_shadow_three_valued);
    CHECK_EQ(cq_reg_strand_reports(&f.ctx), 1);

    /* A SECOND rail, and a second STRANDED QUBIT inside the first one above —
     * both routes must leave the flag at 1 rather than counting. */
    int32_t b = cq_reg_alloc_zero(&f.ctx.regs, 2);
    (void)materialise_bit(&f, b, 0, POISONED_CTRL);
    (void)materialise_bit(&f, b, 1, POISONED_CTRL);
    cq_reg_free(&f.ctx, b, proof_shadow_three_valued);
    /* STILL 1 AFTER THREE STRANDED QUBITS ACROSS TWO RAILS. This is the
     * assertion the flag could not make: with the one-shot deleted the count
     * would be 3 here, and the case that read a flag stayed green — measured,
     * it was the single survivor of a 19-mutant battery. */
    CHECK_EQ(cq_reg_strand_reports(&f.ctx), 1);
    CHECK_EQ(cq_qubits_stranded(&f.ctx.pool), 3);

    /* A CLEAN free after a stranding one still works. The strand is not a
     * latched failure state: D15 says the program continues, and "continues"
     * has to include freeing correctly. */
    int32_t c = cq_reg_alloc_zero(&f.ctx.regs, 2);
    uint32_t q = materialise_bit(&f, c, 1, CLEAN_CTRL);
    cq_reg_free(&f.ctx, c, proof_shadow_three_valued);
    CHECK(cq_qubits_is_free(&f.ctx.pool, q));
    CHECK_EQ(cq_qubits_stranded(&f.ctx.pool), 3);

    fx_close(&f);
}

/* --- D15 §3's RESIDUE SPLIT — `bd 06t`'s first obligation. --------------- */

CQ_TEST(the_residue_splits_by_verdict_although_the_act_does_not)
{
    fixture f; fx_open(&f);

    /* THE CLAIM: "an implementation that reports a single residue figure has
     * not yet decided which row each free lands on" (`bd 06t`, PRD §15 D15 §3).
     * Under §4's confirmed clause the ACT collapses — proven-dirty and unproven
     * alike are stranded — so `cq_qubits_stranded()` genuinely cannot tell them
     * apart. What must survive the collapse is the VERDICT, and this case is
     * where that is a tested claim rather than a design intention.
     *
     * WHY THE ASSERTIONS ARE PAIRWISE AND NOT A TOTAL. The mutant this exists
     * to kill is not "the counters are missing" — it is "the counters are the
     * SAME counter", i.e. both rows incremented from the rail's verdict, or
     * both from `p < 0`, or the split inverted. Every one of those keeps the
     * sum right. So each step below moves exactly ONE row and the other is
     * asserted UNCHANGED; a total would be satisfied by all four mutants. */
    CHECK_EQ(cq_reg_stranded_dirty(&f.ctx), 0);
    CHECK_EQ(cq_reg_stranded_unproven(&f.ctx), 0);
    CHECK_EQ(cq_reg_frees_dirty(&f.ctx), 0);
    CHECK_EQ(cq_reg_frees_unproven(&f.ctx), 0);

    /* (1) A PURELY UNPROVEN rail: one poisoned qubit, nothing convictable. */
    int32_t a = cq_reg_alloc_zero(&f.ctx.regs, 2);
    (void)materialise_bit(&f, a, 0, POISONED_CTRL);
    CHECK_EQ(cq_reg_disposition(&f.ctx, a, proof_shadow_three_valued), 0);
    cq_reg_free(&f.ctx, a, proof_shadow_three_valued);
    CHECK_EQ(cq_reg_stranded_unproven(&f.ctx), 1);
    CHECK_EQ(cq_reg_stranded_dirty(&f.ctx), 0);      /* UNMOVED */
    CHECK_EQ(cq_reg_frees_unproven(&f.ctx), 1);
    CHECK_EQ(cq_reg_frees_dirty(&f.ctx), 0);         /* UNMOVED */

    /* (2) A PURELY CONVICTED rail: one determinate-ONE qubit. This row was
     * empty in the whole tree before Step 23 — the shadow can only reach it
     * through a materialised constant ONE. */
    int32_t b = cq_reg_alloc_zero(&f.ctx.regs, 2);
    (void)dirty_bit(&f, b, 0);
    CHECK(cq_reg_disposition(&f.ctx, b, proof_shadow_three_valued) < 0);
    cq_reg_free(&f.ctx, b, proof_shadow_three_valued);
    CHECK_EQ(cq_reg_stranded_dirty(&f.ctx), 1);
    CHECK_EQ(cq_reg_stranded_unproven(&f.ctx), 1);   /* UNMOVED */
    CHECK_EQ(cq_reg_frees_dirty(&f.ctx), 1);
    CHECK_EQ(cq_reg_frees_unproven(&f.ctx), 1);      /* UNMOVED */

    /* (3) THE ROW THAT MAKES TWO GRAINS NECESSARY RATHER THAN TIDY.
     * A MIXED rail: one CLEAN qubit, one UNPROVEN qubit, one CONVICTED qubit.
     * The qubit rows each gain one; the rail lands on DIRTY *and only* DIRTY,
     * because cq_reg_disposition's lattice makes dirty absorbing. So the two
     * grains DISAGREE about this free — the qubit view says "one of each", the
     * rail view says "dirty" — and neither is derivable from the other.
     *
     * IT IS ALSO THE SHAPE THE FOLD'S NO-EARLY-RETURN EXISTS FOR: the unproven
     * lane comes FIRST and the conviction comes LAST, so a fold that stopped at
     * the first non-clean bit would call this rail UNPROVEN and move the free
     * onto the wrong rail-level row while every qubit-level count stayed
     * right. That is the mutant `tests/CMakeLists.txt` already pins for the
     * disposition itself, arriving here at a different grain. */
    int32_t c = cq_reg_alloc_zero(&f.ctx.regs, 4);
    uint32_t c_clean = materialise_bit(&f, c, 0, CLEAN_CTRL);
    uint32_t c_unpr  = materialise_bit(&f, c, 1, POISONED_CTRL);
    uint32_t c_dirty = dirty_bit(&f, c, 2);
    CHECK(cq_reg_disposition(&f.ctx, c, proof_shadow_three_valued) < 0);
    cq_reg_free(&f.ctx, c, proof_shadow_three_valued);

    CHECK_EQ(cq_reg_stranded_dirty(&f.ctx), 2);      /* +1 */
    CHECK_EQ(cq_reg_stranded_unproven(&f.ctx), 2);   /* +1 */
    CHECK_EQ(cq_reg_frees_dirty(&f.ctx), 2);         /* +1 */
    CHECK_EQ(cq_reg_frees_unproven(&f.ctx), 1);      /* UNMOVED — the point */

    /* And the ACT really did collapse, by INDEX rather than by count: the
     * clean lane went back to the pool and BOTH non-clean lanes stranded. A
     * count could not tell a released-the-wrong-one from this. */
    CHECK(cq_qubits_is_free(&f.ctx.pool, c_clean));
    CHECK(!cq_qubits_is_stranded(&f.ctx.pool, c_clean));
    CHECK(cq_qubits_is_stranded(&f.ctx.pool, c_unpr));
    CHECK(cq_qubits_is_stranded(&f.ctx.pool, c_dirty));

    /* (4) THE SUM IS AN ASSERTION, NOT A COINCIDENCE. Every increment sits on
     * the same branch as the cq_qubits_strand call it describes, so a drift
     * means a strand happened somewhere that did not go through cq_reg_free —
     * which is a fact worth a red test rather than a silent discrepancy. */
    CHECK_EQ(cq_reg_stranded_dirty(&f.ctx) + cq_reg_stranded_unproven(&f.ctx),
             cq_qubits_stranded(&f.ctx.pool));

    /* (5) A CLEAN free is counted on NO rail row, deliberately. D15 §2 is
     * explicit that the I4 frees are not certificate coverage; a rail total
     * that silently included them would report exactly the inflation that
     * warning is about, and "frees" would mean two different things in two
     * readings. */
    int32_t d = cq_reg_alloc_zero(&f.ctx.regs, 2);
    (void)materialise_bit(&f, d, 0, CLEAN_CTRL);
    cq_reg_free(&f.ctx, d, proof_shadow_three_valued);
    int32_t e = cq_reg_alloc_const(&f.ctx.regs, 8, 5u, 0u);   /* I4: no qubits */
    cq_reg_free(&f.ctx, e, NULL);
    CHECK_EQ(cq_reg_frees_dirty(&f.ctx), 2);         /* both UNMOVED */
    CHECK_EQ(cq_reg_frees_unproven(&f.ctx), 1);
    CHECK_EQ(cq_reg_stranded_dirty(&f.ctx), 2);
    CHECK_EQ(cq_reg_stranded_unproven(&f.ctx), 2);

    fx_close(&f);
}

CQ_TEST(the_rail_row_is_tallied_even_when_the_free_aborts)
{
    fixture f; fx_open(&f);

    /* THE ORDERING CLAIM, AND IT HAS A REAL FAILURE MODE. cq_reg_free tallies
     * the rail row BEFORE consulting CQOPS_FREE_ABORT, because under the flag
     * the next statement does not return. A tally placed after it is
     * permanently zero in exactly the configuration a maintainer reaches for
     * when they want to know what is happening — and the ordinary suite, which
     * never sets the flag, could not see that.
     *
     * This case cannot observe the abort itself (that is a death binary's job,
     * and tests/test_reg_death.c owns it). What it CAN observe is the tally
     * arriving on the pre-abort side of the branch, which is the half that
     * moves if the two statements are transposed. */
    int32_t a = cq_reg_alloc_zero(&f.ctx.regs, 2);
    (void)materialise_bit(&f, a, 0, POISONED_CTRL);

    /* The flag stays OFF here; what is asserted is that the tally is already
     * done by the time the disposition is known — i.e. that it reads the same
     * `verdict` the abort branch reads, and not a second, later computation
     * that a mutant could silently desynchronise. */
    int verdict = cq_reg_disposition(&f.ctx, a, proof_shadow_three_valued);
    CHECK_EQ(verdict, 0);
    CHECK_EQ(cq_reg_frees_unproven(&f.ctx), 0);      /* nothing yet */
    cq_reg_free(&f.ctx, a, proof_shadow_three_valued);
    CHECK_EQ(cq_reg_frees_unproven(&f.ctx), 1);

    fx_close(&f);
}
