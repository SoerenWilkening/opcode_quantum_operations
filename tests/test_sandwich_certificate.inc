/* tests/test_sandwich_certificate.inc — the ckd.17a half of Step 8's M09 suite.
 *
 * SPLIT OUT OF test_sandwich.c AT THE 300-LINE GUARD (Rule 12), along the seam
 * the module itself splits on: DRIVER MECHANICS stay in the .c — control flow,
 * the palindrome, the extent window — and the STRUCTURAL ZERO CERTIFICATE
 * comes here: what happens to the qubits and to their shadow entries when the
 * region is released. Rule 12 counts .c/.h/.py only, which is what makes an
 * .inc a real escape hatch rather than a rename that dodges the guard. Included
 * once, from test_sandwich.c, after the fixture and the demo kernel it uses.
 *
 * These are the L2/L3-shaped cases. They are the ones R8 could not fool: a
 * value can be right and a stream can look plausible while a scratch qubit is
 * left off |0>, and only the pool and the retired shadow entries can see it.
 */

/* ------------------------------------------------------------------------
 * 3. The pool and the certificate (ckd.17a).
 * ------------------------------------------------------------------------ */

/* L2/L3-shaped, and the pair that R8 could not fool. The scratch qubits go
 * back, the region's bits are CQ_BIT_ZERO again, and the only qubits still
 * live are the ones the operands and `dst` own. */
CQ_TEST(the_scratch_qubits_come_back_and_the_bits_are_zero_again)
{
    fixture f; fx_open(&f);
    cq_scratch scr; demo_env e;
    cq_bit a[DEMO_W], b[DEMO_W], dst[DEMO_W];

    run_demo(&f, &scr, &e, a, b, dst, 0xBu, 0x6u, 0);

    /* 2W operands + W scratch + W dst minted; scratch is the only part freed. */
    CHECK_EQ(cq_qubits_minted(&f.ctx.pool), (uint32_t)(4 * DEMO_W));
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), (uint32_t)(3 * DEMO_W));
    CHECK_EQ(cq_qubits_free(&f.ctx.pool), (uint32_t)DEMO_W);

    for (int i = 0; i < DEMO_W; i++) {
        cq_bit *s = cq_scratch_span(&scr, (uint32_t)i, 1u);
        CHECK(cq_bit_is_zero(*s));
        CHECK(cq_qubits_is_free(&f.ctx.pool, e.scr_index[i]));
    }

    cq_scratch_dispose(&scr);
    fx_close(&f);
}

/* ckd.17a, the act rather than the thing. The sources are POISONED here, so
 * §3's sticky-poison CX rule leaves every scratch entry `unknown` at the
 * epilogue — which is the state every real kernel's scratch is in. The entries
 * come back known-zero anyway, because cq_shadow_retire runs strictly AFTER
 * cq_qubits_release has returned for the index, and by I3 an index on the free
 * list IS |0>, so {0, 0} is the correct entry for it (PRD §10).
 *
 * This is NOT an un-poisoning of a live qubit and must never become one: a
 * certified-but-live qubit read as a control hits `t.unknown |= c.unknown`, so
 * the poison would stop propagating and the shadow would claim determinate
 * downstream of a real superposition. The live operands below are checked to
 * be still poisoned for exactly that reason. */
CQ_TEST(the_released_indices_are_retired_and_the_live_ones_are_not)
{
    fixture f; fx_open(&f);
    cq_scratch scr; demo_env e;
    cq_bit a[DEMO_W], b[DEMO_W], dst[DEMO_W];

    run_demo(&f, &scr, &e, a, b, dst, 0xBu, 0x6u, 1);

    for (int i = 0; i < DEMO_W; i++) {
        CHECK(cq_qubits_is_free(&f.ctx.pool, e.scr_index[i]));
        CHECK(cq_shadow_known_zero(&f.ctx.shadow, e.scr_index[i]));

        /* Still live, still poisoned — retirement never reaches these. */
        CHECK(cq_shadow_get(&f.ctx.shadow, cq_bit_qindex(a[i])).unknown);
        CHECK(cq_shadow_get(&f.ctx.shadow, cq_bit_qindex(dst[i])).unknown);
    }

    cq_scratch_dispose(&scr);
    fx_close(&f);
}

/* PRD §10: "Both bytes are always written." A bit poisoned while it held 1 has
 * a FROZEN value byte — cq_shadow_x is a no-op under poison, and cx/ccx update
 * `value` only when !unknown — so clearing `unknown` alone at retirement would
 * publish a stale 1 as determinate, and the next cq_materialise of that index
 * would hand unrelated data a shadow that reads |1>.
 *
 * Building the state takes two steps in this order and no other: a DETERMINATE
 * control first, to set the value byte, then a POISONED one, to freeze it
 * there. The circuit is honest — both steps are one self-inverse gate, so the
 * reverse half really does cancel and the qubit really is |0> at the epilogue.
 * Only the shadow's bookkeeping is in the awkward state. */
typedef struct { const cq_bit *det, *poi; cq_bit *s; } frozen_env;

static void frozen_step(cq_ctx *ctx, void *env, int s)
{
    frozen_env *e = env;
    cq_emit_cx(ctx, s == 0 ? e->det : e->poi, e->s);
}

CQ_TEST(retirement_clears_a_frozen_value_byte_not_just_the_poison)
{
    fixture f; fx_open(&f);
    cq_scratch scr;
    cq_scratch_alloc(&scr, 1u);

    cq_bit det = q_operand(&f, 1);
    cq_bit poi = q_operand(&f, 1);
    cq_shadow_rotate(&f.ctx.shadow, cq_bit_qindex(poi));

    frozen_env e;
    e.det = &det; e.poi = &poi;
    e.s = cq_scratch_span(&scr, 0u, 1u);

    cq_sandwich(&f.ctx, &scr, frozen_step, 2, NULL, 0, &e);

    /* Ask for the raw pair, not just the predicate: a mutation that clears
     * `unknown` and leaves `value` at 1 has to fail on the value byte by
     * name, not on a compound answer that could go wrong for another reason. */
    uint32_t q = cq_qubits_minted(&f.ctx.pool) - 1u;   /* the region's one index */
    cq_shadow sh = cq_shadow_get(&f.ctx.shadow, q);
    CHECK_EQ(sh.unknown, 0);
    CHECK_EQ(sh.value, 0);
    CHECK(cq_shadow_known_zero(&f.ctx.shadow, q));

    cq_scratch_dispose(&scr);
    fx_close(&f);
}

/* THE RETIREMENT PATH IS NOT SCRATCH-SPECIFIC, and this is the M07 half of it:
 * cq_reg_free goes through the same cq_ctx_release_qubit joint, so a freed
 * rail's shadow entries are retired exactly as a released scratch region's
 * are. That closes the hazard Step 7 recorded and left open — nothing used to
 * un-poison a released index, and a REUSED index kept its stale entry.
 *
 * THE PROOF BELOW WOULD BE LAUNDERING ANYWHERE ELSE. It answers 1 without
 * looking, which is precisely what PRD §10 forbids in src/ and what
 * CQOPS_FREE_TRUST would have been — and PRD §15 D15 makes that contrast
 * STRONGER rather than weaker: D15 §3 NARROWS `CQOPS_FREE_TRUST`'s prohibition
 * without lifting it, so releasing an unproven index to the pool stays
 * forbidden outright. The annotation is the only thing that changed here; the
 * code is unchanged and must stay so.
 *
 * It is sound HERE only because the case builds the |0> it asserts about: the
 * bit is materialised from a fresh index and never gated, so it is physically
 * |0> and only the SHADOW is poisoned. Without a proof of that shape this path
 * is unobservable — every proof the tree actually has is
 * `cq_shadow_known_zero`, under which a freed entry is already {0, 0} and
 * retirement is a no-op. M26 supplies the real one at Step 23 — D15's observed
 * undo certificate over the call stream (reg.h; bd 06t, the mechanism bd c1a
 * handed off to when it closed). */
static int proof_this_case_built_the_zero_itself(const cq_ctx *ctx,
                                                 int32_t h, uint32_t q)
{
    (void)ctx; (void)h; (void)q;
    return 1;
}

CQ_TEST(freeing_a_rail_retires_its_shadow_entries_too)
{
    fixture f; fx_open(&f);

    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 1u);
    cq_bit *bits = cq_reg_bits(&f.ctx.regs, h);
    cq_materialise(&f.ctx, &bits[0]);            /* fresh index, physically |0> */
    uint32_t q = cq_bit_qindex(bits[0]);

    cq_shadow_rotate(&f.ctx.shadow, q);          /* shadow poisoned, qubit is not */
    CHECK(!cq_shadow_known_zero(&f.ctx.shadow, q));

    cq_reg_free(&f.ctx, h, proof_this_case_built_the_zero_itself);

    CHECK(cq_qubits_is_free(&f.ctx.pool, q));
    CHECK(cq_shadow_known_zero(&f.ctx.shadow, q));

    fx_close(&f);
}

/* D4 is LIFO, and the driver releases the region in REVERSE index order so the
 * stack hands the same indices back in the same ascending order next time. Two
 * identical sandwiches therefore emit an identical gate stream, which is what
 * keeps an L6 trace diff quiet and an L4 golden reproducible. */
CQ_TEST(a_second_sandwich_reuses_the_same_indices_in_the_same_order)
{
    fixture f; fx_open(&f);
    cq_scratch scr; demo_env e1, e2;
    cq_bit a[DEMO_W], b[DEMO_W], dst[DEMO_W];

    run_demo(&f, &scr, &e1, a, b, dst, 0xBu, 0x6u, 0);
    cq_scratch_dispose(&scr);

    e2 = e1; e2.captured = 0;
    cq_scratch_alloc(&scr, (uint32_t)DEMO_W);
    e2.scr = cq_scratch_span(&scr, 0u, (uint32_t)DEMO_W);
    cq_sandwich(&f.ctx, &scr, demo_compute, 2 * DEMO_W, demo_copyout, DEMO_W, &e2);

    for (int i = 0; i < DEMO_W; i++)
        CHECK_EQ(e2.scr_index[i], e1.scr_index[i]);
    CHECK_EQ(cq_qubits_minted(&f.ctx.pool), (uint32_t)(4 * DEMO_W));  /* none fresh */

    cq_scratch_dispose(&scr);
    fx_close(&f);
}

