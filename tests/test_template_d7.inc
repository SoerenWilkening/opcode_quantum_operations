/* tests/test_template_d7.inc — the HANDLE BOUNDARY half of tests/test_template.c,
 * on the seam IMPLEMENTATION_PLAN §3 recorded before either file was written:
 * `the OPCODE SURFACE <-> the HANDLE BOUNDARY`.
 *
 * THE DISCRIMINATOR IS WHETHER THE CASE WOULD STILL EXIST IF EVERY SOURCE
 * HANDLE WERE DISTINCT. Everything here is a case that would not: D7b's
 * defensive copy, its un-copy, the temporary's disposition at free, and the
 * ORDER of all three against `cq_ctrl_push`. D7a is the boundary's other half
 * and is a hard error, so it lives in tests/test_template_death.c.
 *
 * WHY THE REMEDY IS HERE AND NOT IN A KERNEL (`bd 493`, risk R2). Rule 7 hands
 * a kernel `cq_bit *` and `W` and never a handle, so a kernel cannot detect
 * operand aliasing at all. What it DOES see is `a[i]` and `b[i]` being the same
 * `cq_bit`, which reaches M05's §3 distinctness assert — in Debug that aborts
 * from inside a kernel seventeen steps from its cause, and in RELEASE the check
 * is compiled out and a malformed `CCX(q,q,t)` reaches the sink as a silent
 * miscompile. Measured at Step 7 over all 239 goldens: D7a is 0 of 25,147 and
 * D7b is 599, ten of them on v1's integer surface — CQ_lang ships a fixture
 * literally named `slice_select_rail_alias_cond`. A blanket abort would fail
 * shipped fixtures at Step 24.
 *
 * AND D7b IS INVISIBLE TO L1 AND L4 TOGETHER, which is why these cases assert
 * the POOL and the GATE MIX rather than the answer: an aliased `mul` emits the
 * identical tuple, the right value at every input, a clean pool and a clean
 * `_unc`. The only defect is W Toffolis with coincident controls, which the
 * classical shadow models as CNOTs and therefore AGREES with.
 */

/* THE TEMPORARY IS SCRATCH AND MUST COME BACK TO |0>, which on a determinate
 * rail this suite can assert EXACTLY: every index it took returns to the free
 * list and nothing strands. That is SHARPER than `bd 493`'s "the strand delta
 * is exactly W", and the two are not in conflict — see the poisoned case
 * below, which is the corpus's own shape and where W is the right number. */
CQ_TEST(d7b_copies_one_aliased_source_and_returns_the_temporary_to_zero)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);

    const int32_t a = qreg(ctx, TPL_W, TPL_A);
    const int32_t r = cq_shim_bin_qq(CQ_SHIM_OP_MUL, (int)TPL_W, a, a);
    const int32_t live[2] = { a, r };

    /* THE VALUE IS THE SQUARE, which is `cq_template_mul_i32(h10, h10)` — the
     * corpus's own shape, at four fixture lines. */
    CHECK(cq_ref_w_eq(cq_pc_value_w(ctx, r),
                      cq_ref_w_mul(cq_ref_w_make(TPL_A, 0u, (int)TPL_W),
                                   cq_ref_w_make(TPL_A, 0u, (int)TPL_W),
                                   (int)TPL_W)));

    /* THE WHOLE POINT, AND THE ONLY DETECTOR FOR A MISSING UN-COPY ON A
     * DETERMINATE RAIL: the temporary is gone. It owned W qubits between the
     * copy and the free; if the un-copy were deleted it would be freed holding
     * `a`, its set lanes would be CONVICTED, and they would STRAND rather than
     * return — so `live` would not come back and no index would reach the free
     * list. L2 sees it as an index nobody owns; `cq_pc_same` sees `live` net of
     * strands fail to return. Neither is a count of the temporary itself,
     * because the temporary's handle is internal and by design unreachable. */
    CHECK(cq_pc_live_is_exactly(ctx, live, 2u));

    /* THE STRAND COUNT IS TAKEN HERE AND NOT AFTER THE FREES, and the
     * difference is not cosmetic. At this instant the only rail that has been
     * freed is the temporary, so ZERO is a statement about it alone. AFTER the
     * frees below it would be a statement about `a` and `r` as well — both
     * determinate and non-zero, so both legitimately CONVICTED and stranded
     * lane by lane — and the number would be `popcount(a) + popcount(a*a)`,
     * which moves with the operand and says nothing about D7b. Measured: that
     * spelling reported 11 for this fixture, on entirely correct code. */
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0u);

    /* AND THE RESULT ROUND-TRIPS, which is the other half: `_unc` the same
     * aliased call back out and `r` is provably |0>, so its indices RELEASE
     * rather than strand and can be named one by one. That runs the defensive
     * copy a SECOND time, on the `_unc` door, and would go red if only the
     * forward door had it. */
    uint32_t idx[TPL_W];
    const uint32_t n = cq_pc_indices(ctx, r, idx, TPL_W);
    cq_shim_bin_qq_unc(CQ_SHIM_OP_MUL, (int)TPL_W, r, a, a);
    CHECK_EQ(cq_pc_value(ctx, r), 0u);
    cqrt_free(r);
    CHECK(cq_pc_indices_are_free(ctx, idx, n));
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0u);

    /* `cq_pc_same` compares `live` NET OF STRANDS, so it holds across `a`'s
     * conviction — which is what it has always meant and is why the exemption
     * bd evv decided is not a weakening: a LEAK moves `live` without moving
     * `n_stranded` and is still caught here. */
    cqrt_free(a);
    CHECK(cq_pc_same(cq_pc_take(ctx), before));
    close_with(&m);
}

/* THE COST IS W QUBITS AND 2W CNOTs, AND IT IS MEASURED AGAINST A SIBLING
 * RATHER THAN WRITTEN DOWN. The unaliased call at the same width and the same
 * (all-quantum) operand mask emits a byte-identical kernel tuple — counts are a
 * function of `(W, mask)` and nothing else — so the DELTA is exactly what the
 * defensive copy costs. `bd 493`'s correction (1) records that this bead's own
 * design omitted the un-copy and priced it at W CNOTs; the `2` is that
 * correction, executed.
 *
 * AND ZERO EXTRA TOFFOLIS IS THE HALF THAT MATTERS. A copy emitted inside an
 * open region with a quantum control routes through `cq_ctrl_promote_cx` and
 * every CX becomes a CCX; outside one it is a plain physical copy. This case is
 * uncontrolled, so the count pins the cheap shape; the controlled case below
 * pins the ORDER that keeps it cheap. */
CQ_TEST(d7b_costs_exactly_two_w_cnots_and_no_toffoli_over_the_unaliased_call)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t a  = qreg(ctx, TPL_W, TPL_A);
    const int32_t a2 = qreg(ctx, TPL_W, TPL_A);

    cq_mock_reset(&m);
    const int32_t plain = cq_shim_bin_qq(CQ_SHIM_OP_MUL, (int)TPL_W, a, a2);
    const size_t p_x   = cq_mock_count_op(&m, CQ_OP_X);
    const size_t p_cx  = cq_mock_count_op(&m, CQ_OP_CX);
    const size_t p_ccx = cq_mock_count_op(&m, CQ_OP_CCX);

    cq_mock_reset(&m);
    const int32_t alias = cq_shim_bin_qq(CQ_SHIM_OP_MUL, (int)TPL_W, a, a);

    CHECK_EQ((int)cq_mock_count_op(&m, CQ_OP_X),   (int)p_x);
    CHECK_EQ((int)cq_mock_count_op(&m, CQ_OP_CX),  (int)(p_cx + 2u * TPL_W));
    CHECK_EQ((int)cq_mock_count_op(&m, CQ_OP_CCX), (int)p_ccx);

    cqrt_free(a); cqrt_free(a2); cqrt_free(plain); cqrt_free(alias);
    close_with(&m);
}

/* D7b IS NOT ONLY THE FORWARD `qq` DOOR. Every shape with TWO handles can
 * alias, and the corpus's ten integer-surface lines include four `_unc` calls
 * and four `icmp`s — `cq_template_icmp_slt_i32(h0, h0)` and its `_unc` at
 * slice_select_rail_alias_cond:4 and :12. A remedy wired into one door only
 * would be green here and would emit malformed Toffolis there. */
CQ_TEST(d7b_reaches_the_unc_and_icmp_doors_too)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);

    const int32_t a = qreg(ctx, TPL_W, TPL_A);

    const int32_t sq = cq_shim_bin_qq(CQ_SHIM_OP_MUL, (int)TPL_W, a, a);
    cq_shim_bin_qq_unc(CQ_SHIM_OP_MUL, (int)TPL_W, sq, a, a);
    CHECK_EQ(cq_pc_value(ctx, sq), 0u);

    /* `slt(a, a)` is FALSE and `sle(a, a)` is TRUE, which is what separates a
     * working aliased compare from one that quietly compared the temporary
     * against itself in the wrong direction. */
    const int32_t lt = cq_shim_icmp_qq(CQ_SHIM_PRED_SLT, (int)TPL_W, a, a);
    const int32_t le = cq_shim_icmp_qq(CQ_SHIM_PRED_SLE, (int)TPL_W, a, a);
    CHECK_EQ(cq_pc_value(ctx, lt), 0u);
    CHECK_EQ(cq_pc_value(ctx, le), 1u);
    cq_shim_icmp_qq_unc(CQ_SHIM_PRED_SLE, (int)TPL_W, le, a, a);
    CHECK_EQ(cq_pc_value(ctx, le), 0u);

    const int32_t live[4] = { a, sq, lt, le };
    CHECK(cq_pc_live_is_exactly(ctx, live, 4u));
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0u);

    cqrt_free(a); cqrt_free(sq); cqrt_free(lt); cqrt_free(le);
    CHECK(cq_pc_same(cq_pc_take(ctx), before));
    close_with(&m);
}

/* THE CORPUS'S OWN SHAPE, AND THE ROW LANDING 2 TURNED FROM A STRAND INTO A
 * RELEASE. THE CASE WAS RENAMED AND REPINNED RATHER THAN DELETED, and reading
 * what it used to say is the point of keeping the history in this comment.
 *
 * `bd 493` measured that ALL TEN integer-surface aliased sources are
 * rotation-poisoned: every one traces to a general `cqrt_ry` at 0.5 / 0.25 /
 * 0.1 / 0.05 rad, far outside `tol*pi`. `cq_shadow_cx` is
 * `t.unknown |= c.unknown` with NO clearing path, so a temporary copied from a
 * poisoned source inherits the poison and XOR-ing the source back in does not
 * clear it. Under landing 1's shadow-only evidence the temporary was therefore
 * UNPROVEN and this case pinned a strand delta of exactly `W`.
 *
 * LANDING 2 DISCHARGES IT, AND THAT IS THE DESIGN WORKING RATHER THAN A
 * REGRESSION. D15's certificate reads the CALL STREAM, where the temporary's
 * whole history is: born |0>, `^= src`, read as a kernel source, `^= src`,
 * freed. The two copies are a self-inverse pair on an operand that is a kernel
 * SOURCE and so unwritten between them, the reduction cancels them, and the
 * birth value is zero — so the rail is proven clean and its qubits go back to
 * the pool. The poison is irrelevant to it: the certificate never reads the
 * shadow.
 *
 * WHAT STILL HOLDS FROM THE OLD CASE, and is why the assertion is a DELTA of
 * zero rather than a deleted line: `bd 493`'s note that a correct temporary and
 * a never-uncopied one stranded IDENTICALLY under landing 1 was the reason the
 * determinate case above — not this one — is the detector, and that is
 * unchanged. What moved is only which side of the release/strand line this row
 * falls on, and pinning it at zero is now the sharper claim: a certificate that
 * failed to reach the D7b temporary would put `W` back and go red here.
 *
 * THE OTHER HALF THAT MOVED WITH IT is the stderr report. It fired exactly once
 * under landing 1 because something stranded; nothing strands now, so it must
 * be zero — and asserting that rather than deleting the line is what keeps the
 * one-shot's own claim ("emissions, not a flag") live on this path. */
CQ_TEST(d7b_on_a_rotation_poisoned_source_is_discharged_by_the_certificate)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t a = qreg(ctx, TPL_W, TPL_A);

    /* A GENERAL Ry, off §7's lattice — the only row that poisons (PRD §15 D12).
     * Applied to every lane, so the temporary is unproven in all W. */
    cq_rotate_ry(ctx, a, 0.5);

    const uint32_t strand_before = cq_qubits_stranded(&ctx->pool);
    const int32_t  r = cq_shim_bin_qq(CQ_SHIM_OP_MUL, (int)TPL_W, a, a);

    /* ZERO, WHERE LANDING 1 PINNED `TPL_W`. The subtraction is kept rather than
     * simplified to an absolute: the claim is about THIS call's delta, and a
     * later case in this file stranding first would make an absolute assertion
     * about the wrong thing. */
    CHECK_EQ(cq_qubits_stranded(&ctx->pool) - strand_before, 0u);

    /* AND NOTHING WAS REPORTED, because nothing stranded. Under landing 1 this
     * read 1 — the first occurrence naming its handle on stderr. The report
     * still counts EMISSIONS rather than setting a flag (a flag cannot express
     * "exactly once" and a battery measured that difference surviving), which
     * is exactly why zero here is a real assertion: a flag-shaped report that
     * had been set by some earlier path would read 1 whatever this call did. */
    CHECK_EQ(cq_reg_strand_reports(ctx), 0u);

    cqrt_free(a); cqrt_free(r);
    close_with(&m);
}

/* THE ORDER CASE, AND IT IS THE ONE `bd 216` STEP 6's RED COLUMN NAMES BY NAME:
 * `cq_shim_bin_qq_ctrl(op, W, ctrl_Q, h, h)`.
 *
 * `cq_reg_xor_into` is a loop of `cq_emit_cx`, so INSIDE an open region with a
 * CQ_BIT_Q control it routes to `cq_ctrl_promote_cx` -> `cq_emit_ccx_phys`.
 * Three placements are therefore possible and they are NOT equivalent:
 *
 *   copy OUTSIDE, un-copy OUTSIDE  — the temporary holds `a`, and returns to
 *                                    |0> unconditionally. W CX each way.
 *   copy INSIDE,  un-copy INSIDE   — CORRECT but expensive: the temporary is
 *                                    `ctrl AND a`, |ctrl=0> leaves it at |0>
 *                                    and |ctrl=1> pairs the halves. Every CX
 *                                    becomes a CCX, and under PRD §9 row A a
 *                                    constant-ONE lane cannot be rewritten in
 *                                    place, so it becomes a WIRE and the copy
 *                                    costs qubits it did not cost outside.
 *   copy OUTSIDE, un-copy INSIDE   — WRONG. The temporary ends holding
 *                                    `a XOR (ctrl AND a)`, which is `a` on the
 *                                    ctrl = 0 branch: scratch that does not
 *                                    return to |0>, i.e. Rule 2 violated.
 *
 * `bd 493`'s note prescribes the third ("the un-copy AFTER the kernel but
 * BEFORE cq_ctrl_pop") and that is the one placement of the three that does not
 * work; this case is what measures it rather than arguing it. THE FLAG'S SHADOW
 * VALUE IS 0 ON PURPOSE — that is the branch the wrong placement leaves dirty,
 * and at flag value 1 all three placements agree. */
CQ_TEST(the_d7b_copy_brackets_the_region_from_outside_on_both_sides)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);

    const int32_t a    = qreg(ctx, TPL_W, TPL_A);
    const int32_t a2   = qreg(ctx, TPL_W, TPL_A);
    const int32_t flag = qreg(ctx, 1u, 0u);      /* CQ_BIT_Q, shadow value 0 */

    /* THE UNALIASED SIBLING AT THE SAME WIDTH, THE SAME MASK AND THE SAME
     * REGION, so the DELTA is the defensive copy and nothing else. Writing the
     * expected tuple down instead would pin the multiplier's cost as well and
     * would move for reasons that have nothing to do with D7b. */
    cq_mock_reset(&m);
    const int32_t plain =
        cq_shim_bin_qq_ctrl(CQ_SHIM_OP_MUL, (int)TPL_W, flag, a, a2);
    const size_t p_cx  = cq_mock_count_op(&m, CQ_OP_CX);
    const size_t p_ccx = cq_mock_count_op(&m, CQ_OP_CCX);

    cq_mock_reset(&m);
    const int32_t r = cq_shim_bin_qq_ctrl(CQ_SHIM_OP_MUL, (int)TPL_W, flag, a, a);
    const int32_t live[5] = { a, a2, flag, plain, r };

    /* 1. THE UN-COPY IS OUTSIDE: the temporary came back. With it inside, the
     *    ctrl = 0 branch leaves it holding `a`, its set lanes are CONVICTED and
     *    STRAND, and this line names the number. */
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0u);
    CHECK(cq_pc_live_is_exactly(ctx, live, 5u));

    /* 2. THE COPY IS OUTSIDE: EXACTLY 2W more CX and EXACTLY as many Toffolis.
     *    That is the "the temporary holds `a` and not `ctrl AND a`" claim
     *    expressed as the cost it implies, and it is the only form available —
     *    the VALUE cannot express it, because at flag value 0 the promoted
     *    kernel emits nothing the shadow can see under any of the three
     *    placements. Inside the region each copy CX becomes a CCX, so
     *    both-inside moves BOTH numbers by 2W in opposite directions and a `>=`
     *    would have passed. */
    CHECK_EQ((int)cq_mock_count_op(&m, CQ_OP_CX),  (int)(p_cx + 2u * TPL_W));
    CHECK_EQ((int)cq_mock_count_op(&m, CQ_OP_CCX), (int)p_ccx);

    /* 3. AND THE REGION CLOSED. A free between the push and the pop is the
     *    `bd d6m` hazard, and the temporary's free is exactly such a
     *    deallocation if it is placed inside. */
    CHECK_EQ(cq_ctrl_depth(ctx), 0);

    cqrt_free(a); cqrt_free(a2); cqrt_free(flag);
    cqrt_free(plain); cqrt_free(r);
    CHECK(cq_pc_same(cq_pc_take(ctx), before));
    close_with(&m);
}
