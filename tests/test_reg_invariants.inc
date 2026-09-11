/* tests/test_reg_invariants.inc — the I2 audit and the D7a/D7b split.
 *
 * Included by test_reg.c rather than compiled on its own, so it shares that
 * file's fixture and its zero-proof. It is a separate file for the reason
 * plan §3 already records as M07's split seam — "table <-> invariant
 * checking" — and because Rule 12's guard counts .c/.h/.py only, which is what
 * makes moving a block into an .inc a real escape hatch rather than a rename
 * that dodges the limit (tools/check_loc.sh says so in as many words). */

/* --- I2, by sweep. ------------------------------------------------------- */

CQ_TEST(the_audit_passes_on_disjoint_registers)
{
    fixture f; fx_open(&f);
    int32_t a = cq_reg_alloc_zero(&f.ctx.regs, 8);
    int32_t b = cq_reg_alloc_zero(&f.ctx.regs, 8);
    for (uint32_t i = 0; i < 8; i++) {
        (void)materialise_bit(&f, a, i, CLEAN_CTRL);
        (void)materialise_bit(&f, b, i, CLEAN_CTRL);
    }
    cq_reg_audit(&f.ctx);       /* no false positive */
    fx_close(&f);
}

CQ_TEST(the_audit_sweeps_a_measured_rail_without_a_false_positive)
{
    fixture f; fx_open(&f);
    int32_t m = cq_reg_alloc_zero(&f.ctx.regs, 4);
    (void)materialise_bit(&f, m, 0, CLEAN_CTRL);
    cq_reg_mark_measured(&f.ctx.regs, m);

    int32_t live = cq_reg_alloc_zero(&f.ctx.regs, 4);
    (void)materialise_bit(&f, live, 0, CLEAN_CTRL);

    /* A MEASURED rail is swept and a DEAD one is not, and the difference is
     * load-bearing: measurement is terminal, so a measured rail's qubits are
     * DELIBERATELY never reclaimed and it goes on owning them for the life of
     * the program — while a tombstone's bits array is gone and its indices are
     * back on the free list. The pair (this case and its death twin) is what
     * makes the filter observable: without them the sweep can be narrowed from
     * "skip DEAD" to "skip everything non-LIVE" with every test still green,
     * which is exactly how a stray edit to this line survived a full 45-test
     * run in both configurations while the project's only I2 detector went
     * blind to the one rail kind that keeps its qubits forever. */
    cq_reg_audit(&f.ctx);
    CHECK_EQ(cq_reg_owned_qubits(&f.ctx.regs, m), 1);
    fx_close(&f);
}

CQ_TEST(the_audit_asserts_only_the_weak_form)
{
    fixture f; fx_open(&f);
    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, 4);
    (void)materialise_bit(&f, h, 0, CLEAN_CTRL);
    /* Qubits that NO register owns — the shape M08's scratch takes at Step 8,
     * once I6(b) pre-materialises the whole scratch region. A strong-form
     * audit asserting `pool.live == sum of owned` passes today and fails at
     * Step 8, presenting as an M08 bug; pinning the weak form here schedules
     * that breakage now instead. */
    for (int i = 0; i < 5; i++) (void)cq_ctx_fresh_qubit(&f.ctx);
    CHECK(cq_qubits_live(&f.ctx.pool) > cq_reg_owned_qubits(&f.ctx.regs, h));
    cq_reg_audit(&f.ctx);
    fx_close(&f);
}

CQ_TEST(the_audit_is_scoped_to_live_and_measured_registers)
{
    fixture f; fx_open(&f);
    int32_t keep = cq_reg_alloc_zero(&f.ctx.regs, 2);
    (void)materialise_bit(&f, keep, 0, CLEAN_CTRL);

    int32_t gone = cq_reg_alloc_zero(&f.ctx.regs, 2);
    uint32_t q = materialise_bit(&f, gone, 0, CLEAN_CTRL);
    cq_reg_free(&f.ctx, gone, proof_shadow_pre_kernel_only);

    /* I2 is scoped to LIVE and MEASURED registers (PRD §2.2): a tombstone's
     * bits array is gone, and the index it held is now on the free list, so an
     * audit that widened to DEAD slots would both read freed memory and fire
     * clause (d) on every legitimately freed rail. */
    CHECK_EQ(cq_reg_state(&f.ctx.regs, gone), CQ_SLOT_DEAD);
    CHECK(cq_qubits_is_free(&f.ctx.pool, q));
    cq_reg_audit(&f.ctx);
    fx_close(&f);
}

/* --- D7, both halves. ---------------------------------------------------- */

CQ_TEST(distinct_operands_pass_the_d7_check)
{
    fixture f; fx_open(&f);
    int32_t out = cq_reg_alloc_zero(&f.ctx.regs, 8);
    int32_t s[2] = { cq_reg_alloc_zero(&f.ctx.regs, 8),
                     cq_reg_alloc_zero(&f.ctx.regs, 8) };
    cq_reg_check_operands(&f.ctx.regs, out, s, 2);
    cq_reg_check_operands(&f.ctx.regs, CQ_REG_NONE, s, 2);   /* mints nothing */
    CHECK(!cq_reg_sources_alias(s, 2));
    fx_close(&f);
}

CQ_TEST(d7b_two_aliased_sources_are_legal_and_must_not_abort)
{
    fixture f; fx_open(&f);
    int32_t out = cq_reg_alloc_zero(&f.ctx.regs, 32);
    int32_t h   = cq_reg_alloc_zero(&f.ctx.regs, 32);
    int32_t s[2] = { h, h };

    /* THE MOST IMPORTANT CASE IN THIS FILE, and it asserts that something does
     * NOT happen. Measured at Step 7 over all 239 CQ_lang goldens (62,930
     * cq_template_* calls, 25,147 _unc): two sources aliasing each other
     * occurs 599 times, TEN of them on v1's integer surface —
     *   slice_select_rail_alias_cond.expected.log:4   icmp_slt_i32(h0, h0)
     *   slice_select_rail_alias_cond.expected.log:12  its _unc twin
     *   slice_select_rail_alias_cond.expected.log:31  mul_i32(h10, h10)
     *   slice_select_rail_alias_cond.expected.log:36  icmp_eq_i32(h12, h12)
     *   slice_select_rail_alias_cond.expected.log:44  its _unc twin
     *   spec_newcand_qsq_caller.expected.log:13, :16  mul_i32(h2, h2) + _unc
     *   spec_replan_qpow_caller.expected.log:13, :18  mul_i32(h0, h0) + _unc
     *   slice_i128_mulhi.expected.log:4               mul_i128(h1, h1)
     * CQ_lang ships a fixture literally named for it. "Strengthening" this
     * into a blanket pairwise-distinctness abort — which is how PRD §15's old
     * D7 row and plan R2 both read — fails those ten lines at Step 24. The
     * remedy is a defensive copy at the M26 boundary (bd -493), which is what
     * cq_reg_sources_alias is the predicate for. */
    cq_reg_check_operands(&f.ctx.regs, out, s, 2);
    CHECK(cq_reg_sources_alias(s, 2));
    fx_close(&f);
}

/* --- The operand check's two doors: `out` is written, a source is read. --- */

/* A MEASURED RAIL IS A LEGAL SOURCE, AND THIS CASE IS THE WHOLE OF bd tgx.
 * cq_reg_check_operands tested BOTH slots for liveness until 2026-09-11, which
 * refused it — and the check's own stated purpose is to catch a use-after-free,
 * which a measured rail is not: its qubits still exist, it is still swept by
 * cq_reg_audit, and every other door in the stack had already taken this
 * position (cq_reg_cbits admits one where cq_reg_bits refuses it;
 * shim/cq_runtime_rail.c's rail_r for a source; shim/cq_shim_ctx.c for a §9
 * flag; tests/test_runtime_gate.c for a control and a target).
 *
 * IT IS NOT A HYPOTHETICAL: CQ_lang @ 0c7380c emits exactly this shape once,
 * `cq_template_icmp_ne_i32_hl(h2, 0)` one line after `cqrt_measure_i32(h2)` in
 * slice_control_select_bool_round2_window.expected.log:14, and that fixture
 * ABORTED at L6 under the old predicate. The end-to-end half is
 * tests/test_template.c; this is the M07 claim on its own, with no shim layer
 * that could answer for it. */
CQ_TEST(a_measured_rail_is_a_legal_operand_source)
{
    fixture f; fx_open(&f);
    int32_t out = cq_reg_alloc_zero(&f.ctx.regs, 32);
    int32_t m   = cq_reg_alloc_zero(&f.ctx.regs, 32);
    (void)materialise_bit(&f, m, 0, CLEAN_CTRL);
    cq_reg_mark_measured(&f.ctx.regs, m);
    CHECK_EQ(cq_reg_state(&f.ctx.regs, m), CQ_SLOT_MEASURED);

    int32_t s[1] = { m };
    cq_reg_check_operands(&f.ctx.regs, out, s, 1);          /* must not abort */

    /* The READ door one layer down, asserted in the same case so that the two
     * cannot drift apart again: this function and cq_reg_cbits must agree
     * about a measured rail, and they disagreed for eleven weeks. */
    CHECK(cq_reg_cbits(&f.ctx.regs, m) != NULL);

    /* AND THE SOURCE DOOR IS THE ONLY ONE THAT WIDENED. The rail is still not
     * writable and still not freeable — asserted in test_reg_death.c
     * (write_to_a_measured_rail, free_of_a_measured_rail,
     * a_measured_rail_as_the_result_handle), because those are aborts. */
    fx_close(&f);
}
