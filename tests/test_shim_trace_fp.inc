/* tests/test_shim_trace_fp.inc — D21's bracket over the ordered call sequences
 * the fp surface added and the sibling file cannot reach: the ARITY-1 shape
 * `shim/cq_template_unary.c` brought (bead 9ve.36) and, at the end of the file,
 * the ARITY-3 shape `shim/cq_template_ternary.c` brought (bead 9ve.24). Each
 * has its own header where its cases begin.
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

/* D21 OVER THE ARITY-3 SEQUENCE (bead 9ve.24, review round 1), which is the one
 * shape `shim/cq_template_ternary.c` added that nothing above can reach.
 *
 * WHAT IS NEW AND WHY IT NEEDS ITS OWN PAIR. `cq_tpl_ternary` calls
 * `cq_trace_op` DIRECTLY rather than through `cq_trace_op_tpl`, because that
 * helper has two source slots and this family has three — so the frame's three
 * in-slots are filled here for the first time by a template, and nothing the
 * arity-2 cases assert covers the composition of `in=h1|h2|h3`.
 *
 * AND THE `+ n_tmp` TERM IS A COUNT HERE, NOT A FLAG. The arity-2 door predicts
 * `cq_reg_count + (0 or 1)` and `cq_reg_sources_alias` answers 0/1, which at two
 * operands is exact. At three it is not: `fma(a, a, c)` needs ONE temporary and
 * `fma(a, a, a)` needs TWO, and that predicate cannot tell them apart — so the
 * ternary door runs its own pairwise scan BEFORE the bracket opens and feeds the
 * scan's count into the prediction. These two cases differ in exactly that term
 * and in nothing else, and they are written so that both predict `h4` by
 * DIFFERENT arithmetic (4 + 0 against 3 + 1), which is what makes a prediction
 * that dropped the term, or one that added it unconditionally, go red on exactly
 * one of them rather than on neither.
 *
 * BOTH RUN ON ALL-CONSTANT f64 RAILS, for the `.inc`'s standing reason: the
 * trace config gives the whole program three logical qubits and one f64 rail on
 * wires wants sixty-four. By I4 a constant rail owns none, so the handles advance
 * exactly as they would on wires and the prediction is tested without a gate.
 * `n_reg` is ONE — the quantum bit that keeps `trace_header`'s package rule
 * satisfied; the f64 rails, the D7b temporary (§6 rule 3, workspace) and the
 * result are all constant and get no line.
 */
CQ_TEST(a_ternary_call_names_the_handle_with_no_temporary_term)
{
    const char *path = "test_shim_trace_fma.out";
    cq_conf c;
    int32_t q, a, b, f, r;

    trace_begin(path);
    q = a_quantum_bit(0.9);                              /* h0, q0        */
    a = cqrt_alloc_f64(2.5);                             /* h1            */
    b = cqrt_alloc_f64(0.5);                             /* h2            */
    f = cqrt_alloc_f64(1.5);                             /* h3            */
    r = cq_shim_fma_qqq(CQ_SHIM_FMA_FMA, 64, a, b, f);   /* no temporary  */
    trace_finish();

    cq_conf_scan(path, &c);
    cq_conf_ok(&c, "the unaliased ternary call");
    CHECK_EQ((long long)q, 0);
    CHECK_EQ((long long)f, 3);
    /* Three distinct handles, so `cq_reg_count` with no term added. */
    CHECK_EQ((long long)r, 4);
    CHECK_STR_EQ(c.op[c.n_ops - 1], "name=fma, in=h1|h2|h3, out=h4");
    CHECK_EQ(c.n_reg, 1);
}

/* AND THE ALIASED SHAPE, WHERE THE TERM IS ONE. `fma(a, a, c)` makes lane 1
 * equal to lane 0, so D7b mints a temporary before the result rail — and the
 * bracket still names the lanes AS THE CALLER SPELLED THEM, because
 * `cq_trace_op` runs before the redirect. That ordering is deliberate: the
 * algorithm view describes the operation CQ_lang asked for, and the temporary is
 * workspace it never saw. A bracket opened after the redirect would print
 * `in=h1|h3|h2` and name a rail that does not exist at the algorithm level. */
CQ_TEST(an_aliased_ternary_call_names_the_handle_past_its_one_temporary)
{
    const char *path = "test_shim_trace_fma_alias.out";
    cq_conf c;
    int32_t q, a, f, r;

    trace_begin(path);
    q = a_quantum_bit(0.9);                              /* h0, q0        */
    a = cqrt_alloc_f64(2.5);                             /* h1            */
    f = cqrt_alloc_f64(1.5);                             /* h2            */
    r = cq_shim_fma_qqq(CQ_SHIM_FMA_FMA, 64, a, a, f);   /* h3 temporary  */
    trace_finish();

    cq_conf_scan(path, &c);
    cq_conf_ok(&c, "the aliased ternary call");
    CHECK_EQ((long long)q, 0);
    CHECK_EQ((long long)f, 2);
    /* `cq_reg_count` was 3 and one temporary is about to be minted. */
    CHECK_EQ((long long)r, 4);
    CHECK_STR_EQ(c.op[c.n_ops - 1], "name=fma, in=h1|h1|h2, out=h4");
    CHECK_EQ(c.n_reg, 1);
}
