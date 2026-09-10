/* tests/test_kernel_mux_dispatch.inc — included by test_kernel_mux.c.
 *
 * THE ENTRY DISPATCH, WHICH IS THE ONE PART OF M17 THAT IS NOT A PORT.
 * K10.md §5 delta 4 says a classical `cond` must not go through the sandwich
 * and calls it "a decision, not a port — record it before pinning the L5
 * goldens"; mux.c records it and these cases execute it. Split into an .inc
 * for Rule 12's reason and on the seam the compare suite used: the .c keeps
 * the sweeps and the goldens, the .inc keeps the behaviour that only exists
 * because we decided it.
 *
 * WHY IT IS NOT MERELY AN OPTIMISATION, in one line each:
 *   - without it, R9 fails: pre-materialisation is unconditional, so a fully
 *     classical select would take 2W qubits for an operation with no quantum
 *     input, and L5's "zero gates AND zero qubits" is a conjunction.
 *   - with it, a constant condition costs W CX instead of 7W CX + 2W CCX, and
 *     `cqrt_cswap` with a constant control is already specified at zero gates
 *     (PRD §2.1, "swap the two `cq_bit` arrays, 0 gates" —
 *     PRD-v1.md:265 @ 961905f), so the shape is precedented, not invented.
 */

/* One mux call with an explicit (value, quantum-mask) per operand, including
 * `dst` — because the interesting classical case is a classical fold landing in
 * a dst that is ALREADY on qubits, where "zero gates" would be wrong. */
typedef struct {
    int      W;
    uint32_t w_cond;      /* 1 everywhere but the bit-zero case below */
    cq_ref_w vc, qc;
    cq_ref_w vt, qt;
    cq_ref_w vf, qf;
    cq_ref_w qdst;
} mux_run;

static cq_ref_w run_mux(const mux_run *r, cq_counter *cnt, uint32_t *peak)
{
    cq_ctx ctx;
    cq_counter c;
    cq_sink sink;
    cq_ref_w out;

    cq_count_reset(&c);
    sink = cq_sink_counter(&c);
    cq_ctx_init(&ctx, &sink);

    int32_t hc = cq_bk_reg_w(&ctx, r->w_cond,          r->vc, r->qc);
    int32_t ht = cq_bk_reg_w(&ctx, (uint32_t)r->W,     r->vt, r->qt);
    int32_t hf = cq_bk_reg_w(&ctx, (uint32_t)r->W,     r->vf, r->qf);
    int32_t hd = cq_bk_reg_w(&ctx, (uint32_t)r->W,
                             cq_ref_w_zero(), r->qdst);

    /* Everything above is scaffolding — materialising a set bit emits an X.
     * Snapshot and zero the counter so what is measured is the KERNEL. */
    cq_pc_snap before = cq_pc_take(&ctx);
    cq_count_reset(&c);

    cq_kernel_mux(&ctx, cq_reg_bits(&ctx.regs, hd),
                  cq_reg_cbits(&ctx.regs, hc), cq_reg_cbits(&ctx.regs, ht),
                  cq_reg_cbits(&ctx.regs, hf), r->W);

    cq_pc_snap after = cq_pc_take(&ctx);

    if (cnt)  *cnt  = c;
    if (peak) *peak = after.peak - before.peak;

    out = cq_pc_value_w(&ctx, hd);
    cq_ctx_dispose(&ctx);
    return out;
}

/* Two distinct arm values at any width >= 1: 0x5A and 0xA3 differ in bit 0,
 * so the selection stays observable all the way down to W = 1. Asserted, not
 * assumed — a test whose two arms coincide passes whichever one it picks. */
static void two_arms(int W, cq_ref_w *vt, cq_ref_w *vf)
{
    *vt = cq_ref_w_make(0x5Au, 0u, W);
    *vf = cq_ref_w_make(0xA3u, 0u, W);
    CHECK(!cq_ref_w_eq(*vt, *vf));
}

/* W CX, no Toffoli, and — the part R9 is about — NOT ONE SCRATCH QUBIT. The
 * peak is exactly dst's own W, so the sandwich was never entered. */
CQ_TEST(a_classical_cond_never_enters_the_sandwich)
{
    static const int WS[] = { 1, 2, 5, 8, 16 };

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j];

        for (int c = 0; c < 2; c++) {
            mux_run r;
            cq_counter cnt;
            uint32_t peak = 0;
            cq_ref_w got;

            two_arms(W, &r.vt, &r.vf);
            r.W = W;
            r.w_cond = 1u;
            r.vc = c ? cq_ref_w_setbit(0) : cq_ref_w_zero();
            r.qc = cq_ref_w_zero();                 /* classical cond */
            r.qt = cq_ref_w_ones(W);
            r.qf = cq_ref_w_ones(W);
            r.qdst = cq_ref_w_zero();

            got = run_mux(&r, &cnt, &peak);

            CHECK(cq_ref_w_eq(got, c ? r.vt : r.vf));
            CHECK_GATES(cnt.x, cnt.cx, cnt.ccx, 0u, (uint64_t)W, 0u);
            CHECK_EQ(peak, (uint32_t)W);
        }
    }
}

/* THE FOLD IS A COPY, AND A COPY IS NOT NOTHING. With every operand classical
 * but `dst` already on qubits, `dst ^= t` has to emit a real X per set bit of
 * the selected arm — the fold table turns CX(ONE, q) into X(q). A dispatch
 * that returned early on "all classical" without emitting would be green on L5
 * (which builds a classical dst) and silently wrong here. */
CQ_TEST(a_classical_cond_still_writes_a_real_gate_into_a_quantum_dst)
{
    static const int WS[] = { 4, 8, 16 };

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j];
        mux_run r;
        cq_counter cnt;
        uint32_t peak = 0;
        cq_ref_w got;

        two_arms(W, &r.vt, &r.vf);
        r.W = W;
        r.w_cond = 1u;
        r.vc = cq_ref_w_setbit(0);
        r.qc = cq_ref_w_zero();
        r.qt = cq_ref_w_zero();
        r.qf = cq_ref_w_zero();
        r.qdst = cq_ref_w_ones(W);          /* dst is already W qubits at |0> */

        got = run_mux(&r, &cnt, &peak);

        CHECK(cq_ref_w_eq(got, r.vt));
        CHECK_GATES(cnt.x, cnt.cx, cnt.ccx,
                    (uint64_t)cq_ref_w_popcount(r.vt), 0u, 0u);
        CHECK_EQ(peak, 0u);                 /* nothing new was taken */
    }
}

/* L5 in its strict form: zero gates AND zero qubits, at both conditions. */
CQ_TEST(r9_all_classical_operands_cost_nothing_at_all)
{
    static const int WS[] = { 1, 3, 8, 32 };

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++)
        for (int c = 0; c < 2; c++) {
            int W = WS[j];
            mux_run r;
            cq_counter cnt;
            uint32_t peak = 0;
            cq_ref_w got;

            two_arms(W, &r.vt, &r.vf);
            r.W = W;
            r.w_cond = 1u;
            r.vc = c ? cq_ref_w_setbit(0) : cq_ref_w_zero();
            r.qc = cq_ref_w_zero();
            r.qt = cq_ref_w_zero();
            r.qf = cq_ref_w_zero();
            r.qdst = cq_ref_w_zero();

            got = run_mux(&r, &cnt, &peak);

            CHECK(cq_ref_w_eq(got, c ? r.vt : r.vf));
            CHECK_GATES(cnt.x, cnt.cx, cnt.ccx, 0u, 0u, 0u);
            CHECK_EQ(peak, 0u);
        }
}

/* THE ARM SWAP, WHICH NO GATE COUNT CAN SEE. `mux(c, t, f)` and `mux(c, f, t)`
 * emit the identical tuple at every width and every mask, keep the palindrome,
 * and leave the scratch clean — the four gates are symmetric in the arms up to
 * which one reaches `r` first. This is the K09 finding (`uge` meaning `ule`) in
 * its K10 form, and L1 against an independent reference is the only detector.
 * Run at BOTH kinds of cond, because the two paths through mux.c pick the arm
 * in completely different code. */
CQ_TEST(the_arms_are_not_interchangeable)
{
    static const int WS[] = { 1, 2, 3, 5, 8, 16, 32 };

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++)
        for (int cond_q = 0; cond_q < 2; cond_q++)
            for (int c = 0; c < 2; c++) {
                int W = WS[j];
                mux_run r;
                cq_ref_w got;

                two_arms(W, &r.vt, &r.vf);
                r.W = W;
                r.w_cond = 1u;
                r.vc = c ? cq_ref_w_setbit(0) : cq_ref_w_zero();
                r.qc = cond_q ? cq_ref_w_setbit(0) : cq_ref_w_zero();
                r.qt = cq_ref_w_ones(W);
                r.qf = cq_ref_w_ones(W);
                r.qdst = cq_ref_w_zero();

                got = run_mux(&r, NULL, NULL);
                CHECK(cq_ref_w_eq(got, c ? r.vt : r.vf));
            }
}

/* THE POSITIVE CONTROL FOR THE PER-OPERAND EXTENT, which no death test can be.
 *
 * `cq_kernel_check_n` sizes each overlap range by ITS OWN operand's width, and
 * `cond` is one bit while the arms are W. A death test cannot prove that: a
 * guard wrongly using `W` for `cond` computes a range that is too WIDE, and a
 * too-wide range only ever refuses MORE, so every aborting case still aborts.
 * The discriminating case is therefore one that must be ACCEPTED and would be
 * refused by the mis-sized guard.
 *
 * Here `cond` is bit 0 of a (W+1)-bit register and `t` is its remaining W bits
 * — adjacent, non-overlapping, and a sanctioned calling shape, since
 * `cq_scratch_span` exists precisely so a caller can carve one region into
 * named sub-arrays (kernel.h says so at length). Sized correctly, `cond` is
 * [r+0, r+1) and `t` is [r+1, r+1+W): disjoint, accepted. Sized with `W`,
 * `cond` becomes [r+0, r+W) and swallows `t`, and the call is refused as a
 * false D7b.
 *
 * MEASURED, and note the SHAPE of the failure. Replacing the `1` in mux.c's
 * `int w[3] = { 1, W, W }` with `W` makes this case abort the binary — SIGABRT,
 * exit 134, which ctest reports as a failed test — while all eleven death cases
 * stay green, because a too-wide extent only ever refuses more. So it is a red
 * test rather than a reported CHECK failure, and grepping the TAP output for
 * "not ok" will not find it: the process is gone before the line is printed. */
CQ_TEST(a_one_bit_cond_adjacent_to_an_arm_is_accepted)
{
    static const int WS[] = { 2, 4, 8 };

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j];
        cq_ctx ctx;
        cq_counter c;
        cq_sink sink;
        cq_ref_w vt, vf;

        two_arms(W, &vt, &vf);
        cq_count_reset(&c);
        sink = cq_sink_counter(&c);
        cq_ctx_init(&ctx, &sink);

        /* One register: bit 0 is the condition, bits 1..W are the `t` arm. */
        int32_t hr = cq_bk_reg_w(&ctx, (uint32_t)(W + 1),
                                 cq_ref_w_make((vt.lo << 1) | 1u, 0u, W + 1),
                                 cq_ref_w_ones(W + 1));
        int32_t hf = cq_bk_reg_w(&ctx, (uint32_t)W, vf, cq_ref_w_ones(W));
        int32_t hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);
        const cq_bit *r = cq_reg_cbits(&ctx.regs, hr);

        cq_kernel_mux(&ctx, cq_reg_bits(&ctx.regs, hd), &r[0], &r[1],
                      cq_reg_cbits(&ctx.regs, hf), W);

        /* cond is 1, so the answer is `t` — which is `vt` read out of lanes
         * 1..W. Checking the VALUE as well as the acceptance, so a guard that
         * let the call through but mis-sliced the arm would still be caught. */
        CHECK(cq_ref_w_eq(cq_pc_value_w(&ctx, hd),
                          cq_ref_w_make(vt.lo, 0u, W)));
        cq_ctx_dispose(&ctx);
    }
}

/* A QUANTUM `cond` WITH FULLY CLASSICAL ARMS, WHICH NO SWEEP CAN REACH.
 *
 * `cq_kd_case` gives source `i` the mask `m->q[i < 2 ? i : 0]`, so the mux's
 * third source — `f` — shares `q[0]` with `cond`. `cond` is quantum exactly
 * when bit 0 of `q[0]` is set, and that same bit makes `f[0]` quantum. So
 * "quantum `cond`, both arms entirely classical" is *structurally* unreachable
 * through the driver, at every width and every fixed mask pair. It is a real
 * corner and not a curiosity: it is the only shape where the sandwich runs with
 * both arms folding, so phases 1 and 2 become `X`-or-nothing on `d[i]` while
 * phase 3 stays a genuine Toffoli, and the answer still has to come out.
 *
 * COUNTED HERE RATHER THAN ASSUMED. With both arms classical the compute half
 * emits, per bit: phase 0 `CX(f[i] -> r[i])` folds to `X` if `f[i]` is 1 and
 * nothing if 0; phases 1 and 2 likewise on `d[i]`; phase 3 is always a CCX
 * because `cond` and `d[i]` are both qubits. Copy-out is `W` CX from `r`. So
 * across the sandwich: `2 * (popcount(f) + popcount(t) + popcount(f))` X,
 * `W` CX, `2W` CCX. The doubling is the reverse half. */
CQ_TEST(a_quantum_cond_with_classical_arms_is_a_shape_no_sweep_reaches)
{
    static const int WS[] = { 1, 2, 5, 8, 16 };

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j];
        mux_run r;
        cq_counter cnt;
        uint32_t peak = 0;
        uint64_t px, expect_x;

        two_arms(W, &r.vt, &r.vf);
        r.W = W;
        r.w_cond = 1u;
        r.vc = cq_ref_w_setbit(0);
        r.qc = cq_ref_w_setbit(0);      /* cond QUANTUM */
        r.qt = cq_ref_w_zero();         /* arms fully CLASSICAL */
        r.qf = cq_ref_w_zero();
        r.qdst = cq_ref_w_zero();

        px = (uint64_t)cq_ref_w_popcount(r.vt)
           + 2u * (uint64_t)cq_ref_w_popcount(r.vf);
        expect_x = 2u * px;

        CHECK(cq_ref_w_eq(run_mux(&r, &cnt, &peak), r.vt));
        CHECK_GATES(cnt.x, cnt.cx, cnt.ccx,
                    expect_x, (uint64_t)W, (uint64_t)(2 * W));
        CHECK_EQ(peak, (uint32_t)(3 * W));   /* the sandwich DID run */
    }
}

/* `cond` IS ONE BIT AND IT IS BIT ZERO. Bennett's `lower_mux!` takes a vector
 * and reads only `cond[1]` (arith.jl:529), so a port that read a wider slice —
 * or that OR-reduced the condition register — would agree with this suite
 * everywhere `cond` happens to be one bit wide, which is everywhere else.
 * Here the condition register is W bits with every HIGH bit set and bit 0
 * clear: the answer must still be `f`. */
CQ_TEST(the_condition_is_read_from_bit_zero_only)
{
    static const int WS[] = { 2, 8, 16 };

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j];
        mux_run r;
        cq_ref_w hi = cq_ref_w_andnot(cq_ref_w_ones(W), cq_ref_w_setbit(0));

        two_arms(W, &r.vt, &r.vf);
        r.W = W;
        r.w_cond = (uint32_t)W;             /* a WIDE condition register */
        r.qt = cq_ref_w_ones(W);
        r.qf = cq_ref_w_ones(W);
        r.qdst = cq_ref_w_zero();

        /* Every high bit set, bit 0 clear — classical, then quantum. */
        for (int cond_q = 0; cond_q < 2; cond_q++) {
            r.vc = hi;
            r.qc = cond_q ? cq_ref_w_ones(W) : cq_ref_w_zero();
            CHECK(cq_ref_w_eq(run_mux(&r, NULL, NULL), r.vf));

            r.vc = cq_ref_w_or(hi, cq_ref_w_setbit(0));
            CHECK(cq_ref_w_eq(run_mux(&r, NULL, NULL), r.vt));
        }
    }
}
