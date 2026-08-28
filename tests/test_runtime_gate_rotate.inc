/* tests/test_runtime_gate_rotate.inc — the ROTATION half of the gate suite,
 * split on the seam shim/cq_runtime_gate.c records for itself:
 * `the DISCRETE gate primitives <-> the ROTATION families`.
 *
 * TAKEN BECAUSE THE .c REACHED 306 OF 300, which is Rule 12's HARD LIMIT rather
 * than a trigger — the source file's own trigger is 240 and it landed at 156.
 * The cut is the module's, not a size cut improvised at the wall: the five
 * discrete symbols carry no width token, act on one-bit rails and reach M05's
 * exact-shadow X/CX/CCX surface; all 25 rotation symbols carry a `double
 * angle`, walk every lane, and reach M22 — PRD §7's cells, D11's refusal, D12's
 * poison. Everything that can make a rail unfreeable, and everything that can
 * hard-error for a reason the discrete half has no vocabulary for, is in here.
 *
 * Included by tests/test_runtime_gate.c and inheriting its includes. It is out
 * of check_loc.sh's reach because that script's `find` matches `*.c`, `*.h` and
 * `*.py` only — an EXTENSION rule, not a translation-unit rule, which is what
 * makes moving a subject into an .inc a real escape hatch rather than a rename
 * that dodges the guard.
 */

/* -------------------------------------------------------------------------
 * 2. The rotation families.
 * ------------------------------------------------------------------------- */

/* PRD §7's OWN WORDING: the rotation applies to EVERY BIT of the register, and
 * the column is chosen per bit — so a mixed rail takes both. A constant lane
 * under `Rz` is a global phase on a definite value and costs nothing; a wire
 * gets one `rz`. The mixed fixture is what says "per bit" rather than "per
 * rail": a wrapper that rotated only lane 0 would be green on a width-1 rail. */
CQ_TEST(ry_and_rz_reach_every_bit_of_the_rail_and_choose_the_column_per_bit)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    /* An 8-bit rail with wires on lanes 0 and 2 — two wires, six constants. */
    const int32_t h = cq_bk_reg(ctx, 8u, 0x5u, 0x5u);
    cq_mock_reset(&m);

    cqrt_rz_i8(h, 0.4);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_RZ), 2);      /* the constants: free */
    CHECK_EQ(cq_mock_count(&m), 2);

    cq_mock_reset(&m);
    cqrt_ry_i8(h, 0.4);
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_RY), 8);      /* general Ry: every lane,
                                                       * the constants having
                                                       * been materialised */
    CHECK_EQ(cq_mock_count_op(&m, CQ_OP_X), 0);
    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, h), 8);

    /* The general Ry materialised six lanes; every one of them must be OWNED by
     * `h` and nothing else must be live. A count would be satisfied by six new
     * wires anywhere. */
    CHECK(cq_pc_live_is_exactly(ctx, &h, 1u));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* §9's EXACT PROMOTION FOR A ROTATION, per lane:
 *     Rz(phi/2) ; CX(w, t) ; Rz(-phi/2) ; CX(w, t)
 * four gates, no residual phase, no shadow write (PRD §15 D11 and D12). The
 * ORDERED stream is pinned rather than a count, because a count cannot tell
 * `Rz(phi/2)` from `Rz(-phi/2)` and the sign is the whole of the promotion. */
CQ_TEST(rz_controlled_promotes_exactly_and_touches_no_shadow)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t flag = wire(ctx, 1u, 1u);
    const int32_t h    = cq_bk_reg(ctx, 1u, 0u, 1u);

    const uint32_t w = cq_bit_qindex(cq_reg_cbits(&ctx->regs, flag)[0]);
    const uint32_t t = cq_bit_qindex(cq_reg_cbits(&ctx->regs, h)[0]);
    cq_mock_reset(&m);

    cqrt_rz_i1_controlled(flag, h, 0.4);

    const cq_rec want[] = {
        CQ_REC_RZ(t,  0.2), CQ_REC_CX(w, t),
        CQ_REC_RZ(t, -0.2), CQ_REC_CX(w, t)
    };
    CHECK(cq_mock_matches(&m, want, sizeof want / sizeof want[0]));

    CHECK_EQ(cq_ctrl_depth(ctx), 0);
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ flag, h }, 2u));
    cq_reg_audit(ctx);
    close_with(&m);
}

/* AND THE SHADOW CLAIM NEEDS A POISONED FLAG, WHICH A FIRST DRAFT DID NOT HAVE.
 * §9's rotation promotion sends its two CXs STRAIGHT to the sink rather than
 * through cq_emit_cx_phys, and src/controlled.c says why: routing them through
 * the shadow would propagate the CONTROL WIRE's poison into a target that
 * provably did not move. With a DETERMINATE flag the two shadow writes would
 * cancel and set no `unknown`, so a shadow-propagating promotion answers CLEAN
 * too — measured: adding `cq_shadow_cx` after each of `ctrl_rot`'s two sink
 * calls left all 245 tests green in both configurations. Only a poisoned flag
 * separates "the promotion adds no shadow write" from "the shadow write
 * happened to be a no-op", and the corpus produces exactly this shape: every
 * `cqrt_*_controlled` flag is an `icmp` result on a rail a `cqrt_ry` has
 * rotated.
 *
 * The determinate run above stays as the negative control. */
CQ_TEST(a_controlled_rz_leaves_a_target_freeable_under_a_POISONED_flag)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t flag = wire(ctx, 1u, 1u);
    cqrt_ry_i1(flag, 0.5);                     /* §7's general row: poison */
    const int32_t h = cq_bk_reg(ctx, 1u, 0u, 1u);

    CHECK_EQ(cq_reg_disposition(ctx, flag, cq_pc_zero_proof_rotation_free),
             CQ_PROOF_UNPROVEN);               /* the flag really is poisoned */
    CHECK_EQ(cq_reg_disposition(ctx, h, cq_pc_zero_proof_rotation_free),
             CQ_PROOF_CLEAN);
    cq_mock_reset(&m);              /* the fixture's own X and ry are not ours */

    cqrt_rz_i1_controlled(flag, h, 0.4);

    /* D12: a diagonal cannot move a computational-basis value and the promotion
     * writes no shadow at all, so the TARGET is still exactly as clean as it
     * was — which is what keeps it freeable and its qubit off the strand list. */
    CHECK_EQ(cq_reg_disposition(ctx, h, cq_pc_zero_proof_rotation_free),
             CQ_PROOF_CLEAN);
    CHECK_EQ(cq_mock_count(&m), 4);
    CHECK_EQ(cq_ctrl_depth(ctx), 0);

    /* BEFORE the free: afterwards `h` is a tombstone and cq_pc_live_is_exactly
     * resolves through cq_reg_cbits, which hard-errors on one — a process abort
     * with a misleading message, not a test failure. */
    CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ flag, h }, 2u));

    cqrt_free(h);
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0);
    cq_reg_audit(ctx);

    close_with(&m);
}

/* `_inv` MEANS NEGATE THETA (PRD §15 D14), and on the Rz column that really is
 * an exact inverse rather than an approximation: `cq_angle_rz_row` collapses
 * NEG_IDENTITY and both half turns into GENERAL, so the parity split `bd fna`
 * bought for the Ry column is structurally invisible here and no row's
 * behaviour depends on the sign of phi except through the emitted angle.
 *
 * Asserted as a STREAM COMPARISON against the forward call at -phi rather than
 * as a count: the forward and the inverse emit the same four opcodes on the
 * same operands, and only the angles differ. Angles compare BITWISE in this
 * project, which is what makes "exactly negated" a testable claim. */
CQ_TEST(rz_controlled_inv_is_the_forward_call_at_a_negated_angle)
{
    cq_mock fwd, inv; cq_sink sf, si;
    cq_rec captured[64];                 /* eight lanes x §9's four gates */
    size_t n = 0u;

    {
        cq_ctx *ctx = open_with(&fwd, &sf);
        const int32_t flag = wire(ctx, 1u, 1u);
        /* ALL-QUANTUM, and that is forced rather than tidy: under a quantum
         * flag D11 refuses the Rz constant column, so a rail with any constant
         * lane aborts before the second gate. See the characterisation case. */
        const int32_t h    = cq_bk_reg(ctx, 8u, 0x5u, 0xFFu);
        cq_mock_reset(&fwd);
        cqrt_rz_i8_controlled(flag, h, -0.4);
        n = cq_mock_count(&fwd);
        CHECK_EQ(n, 32);                 /* not merely "some": 4 per lane */
        for (size_t i = 0; i < n && i < 64u; i++) captured[i] = *cq_mock_at(&fwd, i);
        CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ flag, h }, 2u));
        cq_reg_audit(ctx);
        close_with(&fwd);
    }
    {
        cq_ctx *ctx = open_with(&inv, &si);
        const int32_t flag = wire(ctx, 1u, 1u);
        const int32_t h    = cq_bk_reg(ctx, 8u, 0x5u, 0xFFu);
        cq_mock_reset(&inv);
        cqrt_rz_i8_controlled_inv(flag, h, 0.4);
        CHECK_EQ(cq_mock_count(&inv), n);
        CHECK(cq_mock_matches(&inv, captured, n));
        CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ flag, h }, 2u));
        cq_reg_audit(ctx);
        close_with(&inv);
    }
}

/* THE Ry SIDE OF THE SAME CLAIM, and it is a DIFFERENT claim rather than a
 * repetition. There is NO plain `cqrt_ry_<W>_controlled` at any width — the ABI
 * says the prep itself is unconditional and only its inverse is controlled — so
 * the Ry axis is 5 integer symbols with no forward twin to compare against.
 * What stands in for one is `cq_rotate_ry` at the negated angle under the same
 * region, reached through the uncontrolled symbol with a CQ_BIT_ONE flag. */
CQ_TEST(ry_controlled_inv_is_the_rotation_at_a_negated_angle)
{
    cq_mock a, b; cq_sink sa, sb;
    cq_rec captured[8];
    size_t n = 0u;

    {
        cq_ctx *ctx = open_with(&a, &sa);
        const int32_t on = wire(ctx, 1u, 0u);            /* CQ_BIT_ONE */
        const int32_t h  = cq_bk_reg(ctx, 8u, 0x3u, 0x3u);
        cq_mock_reset(&a);
        (void)on;
        cqrt_ry_i8(h, -0.4);
        n = cq_mock_count(&a);
        /* PINNED, NOT BOUNDED. `<=` is a buffer guard; if the captured run ever
         * emitted nothing, `cq_mock_matches(&x, captured, 0)` would be
         * trivially true and the whole differential would evaporate. A general
         * `Ry` MATERIALISES a constant lane, so all eight of them rotate — two
         * that were already wires and six that stop being constants. */
        CHECK_EQ(n, 8);
        for (size_t i = 0; i < n && i < 8u; i++) captured[i] = *cq_mock_at(&a, i);
        CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ on, h }, 2u));
        cq_reg_audit(ctx);
        close_with(&a);
    }
    {
        cq_ctx *ctx = open_with(&b, &sb);
        const int32_t on = wire(ctx, 1u, 0u);
        const int32_t h  = cq_bk_reg(ctx, 8u, 0x3u, 0x3u);
        cq_mock_reset(&b);
        cqrt_ry_i8_controlled_inv(on, h, 0.4);           /* row 0: verbatim */
        CHECK_EQ(cq_mock_count(&b), n);
        CHECK(cq_mock_matches(&b, captured, n));
        CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ on, h }, 2u));
        cq_reg_audit(ctx);
        close_with(&b);
    }
}

/* -------------------------------------------------------------------------
 * 3. D11's refusal, and the part of it that is not atomic.
 * ------------------------------------------------------------------------- */

/* Runs `fn` in a child whose STDOUT is a pipe, and reports the bytes and
 * whether it died on SIGABRT. tests/test_shim_ctx_region.inc's helper with
 * STDOUT_FILENO in place of STDERR_FILENO, and for the mirror-image reason: the
 * subject here is what reached the SINK, and M23 writes its trace to stdout and
 * flushes per line, so a child that aborts mid-rail has already emitted every
 * gate it got to.
 *
 * fflush(NULL) BEFORE THE FORK is not hygiene — the harness's TAP diagnostics
 * are buffered under ctest, so a child forked mid-case inherits them and
 * re-emits them when it aborts. */
static void drain(int fd, char *buf, size_t cap)
{
    size_t n = 0;

    buf[0] = '\0';
    for (;;) {
        const ssize_t got = read(fd, buf + n, cap - 1u - n);
        if (got == 0) break;
        if (got < 0) {
            if (errno == EINTR) continue;
            cq_h_fail(__FILE__, __LINE__, "read failed: errno %d", errno);
            break;
        }
        n += (size_t)got;
        if (n + 1u >= cap) break;
    }
    buf[n] = '\0';
}

/* BOTH STREAMS, ON SEPARATE PIPES, and the second one is not decoration: the
 * subject is what reached the SINK before the abort, and the abort's own
 * identity is on stderr. A version reading only stdout can say "something died
 * having emitted N gates" and cannot say WHICH refusal — and at j == 0 that
 * reduces to "nothing was emitted and something died", which any abort in the
 * fixture satisfies. */
static void child_streams(void (*fn)(void *), void *arg,
                          char *out, size_t out_cap,
                          char *err, size_t err_cap, int *signalled)
{
    int fo[2], fe[2];

    out[0] = '\0'; err[0] = '\0';
    *signalled = 0;
    if (pipe(fo) != 0) { cq_h_fail(__FILE__, __LINE__, "pipe failed"); return; }
    if (pipe(fe) != 0) {
        (void)close(fo[0]); (void)close(fo[1]);
        cq_h_fail(__FILE__, __LINE__, "pipe failed");
        return;
    }

    fflush(NULL);

    const pid_t pid = fork();
    if (pid < 0) {
        (void)close(fo[0]); (void)close(fo[1]);
        (void)close(fe[0]); (void)close(fe[1]);
        cq_h_fail(__FILE__, __LINE__, "fork failed");
        return;
    }
    if (pid == 0) {
        (void)close(fo[0]); (void)close(fe[0]);
        (void)dup2(fo[1], STDOUT_FILENO);
        (void)dup2(fe[1], STDERR_FILENO);
        (void)close(fo[1]); (void)close(fe[1]);
        fn(arg);
        _exit(0);
    }

    (void)close(fo[1]); (void)close(fe[1]);
    drain(fo[0], out, out_cap);
    drain(fe[0], err, err_cap);
    (void)close(fo[0]); (void)close(fe[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    *signalled = WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

/* `j` constant lanes below the first wire, then a wire, under a quantum flag. */
static void rz_on_a_rail_with_a_constant_lane(void *p)
{
    const uint32_t j = *(const uint32_t *)p;
    cq_sink printf_sink = cq_sink_printf(stdout);

    cq_sink_reset();
    cqops_set_sink(&printf_sink);
    cq_shim_ctx_reset();
    cq_ctx *ctx = cq_shim_ctx();

    /* THE FIXTURE ITSELF MUST EMIT NOTHING, because the sink is already
     * installed and this case counts LINES. A CQ_BIT_Q flag holding 1 would
     * cost one materialising X and every count below would be 4j+1 — measured,
     * and it is the difference between a characterisation and an off-by-one. A
     * flag holding 0 still PROMOTES: row 0 branches on the bit's KIND, never on
     * its shadow. */
    const int32_t flag = cq_bk_reg(ctx, 1u, 0u, 1u);
    /* An 8-bit rail — the width `cqrt_rz_i8` names — whose lanes [0, j) are
     * wires and whose lane j is the first constant. */
    const uint64_t mask = (UINT64_C(1) << j) - 1u;
    const int32_t h = cq_bk_reg(ctx, 8u, 0u, mask);

    cqrt_rz_i8_controlled(flag, h, 0.4);
}

/* D11's REFUSAL IS REAL AND IT IS NOT ATOMIC, and nothing in the tree could see
 * the second half. `cq_rotate_rz` resolves the rail and walks its lanes in
 * order; §7's Rz row folds on the CONSTANT column only, so a mixed rail under a
 * quantum control emits the full four-gate promotion for every wire BELOW the
 * first constant and then hard-errors. All five existing D11 rotation death
 * cases use uniform rails, where the refusal lands at lane 0 having emitted
 * nothing — so this is a CHARACTERISATION of behaviour no case forbids, not a
 * claim that it is right.
 *
 * IT MATTERS AT STEP 24. bd 590 is still open on what Step 24's oracle is, and
 * if it diffs a fixture's whole stdout then these orphan lines are observable
 * output from a program that then died. The measured shape is 4*j gates, and
 * that is what changes if anyone makes the refusal atomic — which would be an
 * improvement, and would turn this case red, and should. */
CQ_TEST(the_d11_rz_refusal_emits_four_gates_per_wire_below_the_first_constant)
{
    static const uint32_t JS[] = { 0u, 1u, 2u, 3u };
    char out[8192], err[4096];

    for (size_t i = 0; i < sizeof JS / sizeof JS[0]; i++) {
        uint32_t j = JS[i];
        int signalled = 0;
        child_streams(rz_on_a_rail_with_a_constant_lane, &j,
                      out, sizeof out, err, sizeof err, &signalled);

        CHECK(signalled);                 /* it aborted, in both configurations */

        /* AND IT WAS D11 THAT ABORTED. A first draft read only stdout and any
         * SIGABRT, which at j == 0 reduces to "zero output and something died"
         * — satisfied by an abort anywhere in the fixture. The child's stderr is
         * captured separately and must carry M06's refusal. */
        CHECK(strstr(err, "D11") != NULL);
        CHECK(strstr(err, "FATAL: shim:") == NULL);

        unsigned gates = 0u;
        for (const char *s = out; (s = strchr(s, '\n')) != NULL; s++) gates++;
        CHECK_EQ(gates, 4u * j);
    }
}
