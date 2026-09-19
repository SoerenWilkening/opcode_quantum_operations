/* tests/test_shim_trace_fp.inc — D21's bracket over the ARITY-1 ordered call
 * sequence, which is the one shape `shim/cq_template_unary.c` added that no
 * case in the sibling file can reach (bead 9ve.36).
 *
 * WHY AN `.inc` AND WHY THIS SUBJECT. tests/test_shim_trace.c stood at 292
 * counted lines of Rule 12's 300, and the cut is the discriminator this repo
 * has now used three times — `the INTEGER surface <-> the fp surface`. What is
 * here turns on something only the cross-domain conversions have: a narrow row
 * is a COMPOSITION, so the call mints a WORKSPACE rail before it mints its
 * result, and the bracket has to be opened before either of them.
 */

/* THE PREDICTED OUTPUT HANDLE IS `cq_reg_count + 1` ON A COMPOSED CALL, which
 * is the same off-by-one D7b's temporary creates in the arity-2 sequence and
 * has to be got right independently here.
 *
 * THE BRACKET CANNOT WAIT FOR THE HANDLE TO EXIST. A narrow `fptosi` runs its
 * inner stage INTO the workspace before `dst` is minted at all, so a bracket
 * opened after the mint would leave those gates outside every bracket — a
 * FATAL parse error for the viewer, not a cosmetic gap. So the name is emitted
 * against a handle the shim has not created yet, and getting the `+ 1` wrong
 * names either a rail that is WORKSPACE or one that does not exist.
 *
 * IT RUNS ON AN ALL-CONSTANT SOURCE, WHICH IS THE ONLY WAY THIS FIXTURE CAN
 * REACH THE SHAPE AT ALL. The trace config gives the whole program three
 * logical qubits and an `f64` rail on wires wants sixty-four; by I4 a constant
 * rail owns none, so the composition runs, the handles advance exactly as they
 * would on wires, and the prediction is tested without a single gate. The one
 * quantum bit is there because a trace with brackets and no `#REGISTER` line is
 * a refusal rather than a picture (`trace_header`'s package rule).
 *
 * AND THE WORKSPACE GETS NO `#REGISTER` LINE — handoff §6 rule 3, the same
 * clause D7b's temporary sits under. `n_reg` is ONE: the quantum bit. The f64
 * rail, the workspace and the result are all-constant, which is I4 and
 * therefore L5 made visible rather than a gap.
 */
CQ_TEST(a_composed_conversion_names_the_handle_past_its_workspace)
{
    const char *path = "test_shim_trace_fconv.out";
    cq_conf c;
    int32_t q, a, r;

    trace_begin(path);
    q = a_quantum_bit(0.9);                                  /* h0, q0     */
    a = cqrt_alloc_f64(2.5);                                 /* h1, no qubit */
    r = cq_shim_fcast(CQ_SHIM_FCAST_FPTOSI, 64, 8, a);       /* h2 workspace */
    trace_finish();

    cq_conf_scan(path, &c);
    cq_conf_ok(&c, "the composed conversion");
    CHECK_EQ((long long)q, 0);
    CHECK_EQ((long long)a, 1);
    /* h2 is the workspace, so the RESULT is h3 — the `+ 1` under test. */
    CHECK_EQ((long long)r, 3);
    CHECK_STR_EQ(c.op[c.n_ops - 1], "name=fptosi, in=h1, out=h3");
    CHECK_EQ(c.n_reg, 1);
}

/* AND THE FLAT SHAPE, AS THE DISCRIMINATOR. A 64 -> 64 conversion has NO
 * workspace, so its predicted handle is `cq_reg_count` with no term added — and
 * without this case a sequence that added 1 unconditionally would be green
 * above and wrong on every one of the six flat rows. The two cases differ in
 * exactly the term under test and in nothing else.
 */
CQ_TEST(a_flat_conversion_names_the_handle_with_no_workspace_term)
{
    const char *path = "test_shim_trace_fconv_flat.out";
    cq_conf c;
    int32_t q, a, r;

    trace_begin(path);
    q = a_quantum_bit(0.9);                                  /* h0, q0     */
    a = cqrt_alloc_f64(2.5);                                 /* h1         */
    r = cq_shim_fcast(CQ_SHIM_FCAST_FPTOSI, 64, 64, a);      /* no workspace */
    trace_finish();

    cq_conf_scan(path, &c);
    cq_conf_ok(&c, "the flat conversion");
    CHECK_EQ((long long)q, 0);
    CHECK_EQ((long long)r, 2);
    CHECK_STR_EQ(c.op[c.n_ops - 1], "name=fptosi, in=h1, out=h2");
    CHECK_EQ(c.n_reg, 1);
}
