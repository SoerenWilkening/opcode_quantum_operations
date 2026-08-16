/* tests/test_kernel_mul_schedule.inc — included by test_kernel_mul.c.
 *
 * THE FLAT STEP SCHEDULE, CHECKED AGAINST A SECOND DERIVATION OF IT.
 *
 * K11 interleaves two constructions in one index space: W blocks, each `W − j`
 * partial-product Toffolis followed by `6W − 5` accumulate steps. mul.c locates
 * the block by BINARY SEARCH over the closed form
 * `start(j) = j(7W−5) − j(j−1)/2`; this file locates it by WALKING the blocks
 * and subtracting. Two derivations of one schedule, compared per step, per op
 * and per control.
 *
 * WHY THIS EARNS ITS KEEP WHEN L1, L4 AND THE PALINDROME ARE ALL GREEN. Each of
 * those three is blind to a different half of the schedule:
 *
 *   - L1 sees a wrong VALUE. It is blind to any rearrangement that still
 *     computes a·b — and the interleaving has plenty, because the accumulates
 *     commute with nothing but still produce the same sum if a block boundary
 *     moves by a whole block.
 *   - L4 sees a wrong COUNT. It is blind to every permutation of a fixed
 *     multiset, which is exactly what a mis-placed block boundary produces:
 *     `start(j)` off by one still emits W(W+1)/2 Toffolis and W(6W−5)
 *     accumulate gates.
 *   - THE PALINDROME IS BLIND TO ALL OF IT, and this is the part that surprises.
 *     cq_sandwich replays whatever schedule the step function describes, so ANY
 *     total function of `s` mirrors perfectly. The palindrome proves the driver
 *     reversed what the kernel emitted; it cannot prove the kernel emitted the
 *     right thing.
 *
 * So the discriminating fault is: a schedule that is a bijection on
 * [0, n_compute), emits the right multiset, and computes a·b — but hands step
 * `s` to the wrong construction. The binary search is precisely where that
 * lives (an inclusive/exclusive slip in `outer_of`, or a `j−1` in
 * `block_start`), and it is the one thing in mul.c that is not a transcription.
 */

/* K8's op sequence at this width, recorded rather than described. Same fixture
 * as addacc_tuple's — three all-quantum registers, which is the only mask
 * cq_addacc_check accepts. */
static size_t addacc_ops(int W, cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_addacc_block k;
    cq_ref_w ones = cq_ref_w_ones(W);

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);

    int32_t ha = cq_bk_reg_w(&ctx, (uint32_t)W, ones, ones);
    int32_t hb = cq_bk_reg_w(&ctx, (uint32_t)W, ones, ones);
    int32_t hx = cq_bk_reg_w(&ctx, 1u, cq_ref_w_zero(), cq_ref_w_ones(1));

    k.acc = cq_reg_bits(&ctx.regs, ha);
    k.b   = cq_reg_bits(&ctx.regs, hb);
    k.x   = cq_reg_bits(&ctx.regs, hx);
    k.W   = W;

    cq_mock_reset(m);
    cq_kernel_addacc(&ctx, &k);
    cq_ctx_dispose(&ctx);

    return cq_mock_count(m);
}

/* One mul at the all-quantum mask, with the stream recorded and the operand and
 * destination qubit indices reported. The indices are read AFTER the call: dst
 * is minted all-BIT_ZERO and its bits become qubits during the copy-out (at
 * zero materialisation gates, since the constant is 0). */
static void mul_stream(int W, cq_mock *m, uint32_t *qa, uint32_t *qb,
                       uint32_t *qd)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_ref_w ones = cq_ref_w_ones(W);

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);

    int32_t ha = cq_bk_reg_w(&ctx, (uint32_t)W, ones, ones);
    int32_t hb = cq_bk_reg_w(&ctx, (uint32_t)W, ones, ones);
    int32_t hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);

    cq_mock_reset(m);
    cq_kernel_mul(&ctx, cq_reg_bits(&ctx.regs, hd), cq_reg_cbits(&ctx.regs, ha),
                  cq_reg_cbits(&ctx.regs, hb), W);

    for (int i = 0; i < W; i++) {
        qa[i] = cq_bit_qindex(cq_reg_cbits(&ctx.regs, ha)[i]);
        qb[i] = cq_bit_qindex(cq_reg_cbits(&ctx.regs, hb)[i]);
        qd[i] = cq_bit_qindex(cq_reg_cbits(&ctx.regs, hd)[i]);
    }

    cq_ctx_dispose(&ctx);
}

static int controls_are(const cq_rec *r, uint32_t p, uint32_t q)
{
    return (r->a == p && r->b == q) || (r->a == q && r->b == p);
}

static int in_set(uint32_t q, const uint32_t *set, int n)
{
    for (int i = 0; i < n; i++) if (set[i] == q) return 1;
    return 0;
}

/* I6(a) FROM THE TEST SIDE, IN BOTH CONFIGURATIONS: no gate of either compute
 * half may target a qubit owned by `a`, `b` or `dst`.
 *
 * A BACKSTOP, NOT A SOLE DETECTOR, AND THAT WAS MEASURED RATHER THAN ASSUMED.
 * The fault it is aimed at is the one K11.md §5 delta 4 calls "a trap one line
 * from a CRITICAL I6(a) violation" — wiring a real operand into K8's addend
 * slot, the one slot in the catalogue that is non-`const` and so the one place
 * the type system stops helping. Provoked at Step 16 by
 * `k.b = (cq_bit *)e->a`, in both configurations: THE PROCESS ABORTS FIRST, from
 * M15's `cq_addacc_check`, on the first sweep case whose mask leaves an operand
 * bit classical — and where it does not abort, L1 sees `dst = W·a` instead of
 * `a·b`. This assertion never got to run. So the honest claim is narrow:
 *
 *   - It is the ONLY statement of I6(a) this suite makes in RELEASE. `src/emit.c`'s
 *     extent check is inside `#if CQOPS_DEBUG_INVARIANTS`, and K08.md records
 *     that it is inert at W = 2 in either configuration anyway.
 *   - On the all-quantum fixture this case uses, `cq_addacc_check` is SATISFIED
 *     by the mis-wired input — every bit of `a` is a qubit and `a` is disjoint
 *     from `accum` and `x` — and Cuccaro restores its addend, so `a` is
 *     unchanged at the end of the half. Within this case, nothing else speaks.
 *   - It names the cause. "L1 says 2 instead of 3" and "step 27 targets an
 *     operand qubit" are not equally useful to whoever reads the failure.
 *
 * The qubit indices are safe to compare: scratch is minted at step 1 and
 * released only after the reverse half, and `dst`'s lanes are minted during the
 * copy-out in between, so no index is recycled inside one call. */
static void no_gate_targets_an_operand(const cq_mock *m, int W, size_t head,
                                       const uint32_t *qa, const uint32_t *qb,
                                       const uint32_t *qd)
{
    size_t n = cq_mock_count(m);

    for (size_t s = 0; s < n; s++) {
        const cq_rec *r = cq_mock_at(m, s);
        int copyout = s >= head && s < head + (size_t)W;

        if (copyout) continue;                   /* dst IS the copy-out target */

        if (in_set(r->t, qa, W) || in_set(r->t, qb, W) || in_set(r->t, qd, W))
            cq_h_fail(__FILE__, __LINE__,
                      "I6(a) mul W=%d step %llu (%s half): the gate targets q%u, "
                      "which belongs to a, b or dst — a compute-half target must "
                      "be a bit of the scratch region", W,
                      (unsigned long long)s, s < head ? "forward" : "reverse",
                      r->t);
    }
}

CQ_TEST(the_flat_step_schedule_matches_a_brute_force_scan)
{
    static const int WS[] = { 2, 3, 4, 5, 8 };
    cq_mock m, k8;
    uint32_t qa[8], qb[8], qd[8];

    cq_mock_init(&m);
    cq_mock_init(&k8);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        size_t n_k8 = addacc_ops(W, &k8);
        size_t head = (size_t)cq_mul_steps(W);
        size_t s = 0;
        uint64_t n_pp = 0, n_acc = 0;

        CHECK_EQ(n_k8, (size_t)cq_addacc_steps(W));
        mul_stream(W, &m, qa, qb, qd);
        CHECK_EQ(cq_mock_count(&m), 2u * head + (size_t)W);

        /* THE SCAN. Walk the blocks; mul.c binary-searches them. */
        for (int j = 0; j < W; j++) {
            for (int k = 0; k < W - j; k++, s++) {
                const cq_rec *r = cq_mock_at(&m, s);

                n_pp++;
                if (r->op != CQ_OP_CCX)
                    cq_h_fail(__FILE__, __LINE__,
                              "mul W=%d step %llu: phase P for (j=%d, k=%d) is "
                              "op %d, want CCX", W, (unsigned long long)s, j, k,
                              (int)r->op);
                else if (!controls_are(r, qa[k], qb[j]))
                    cq_h_fail(__FILE__, __LINE__,
                              "mul W=%d step %llu: phase P (j=%d, k=%d) controls "
                              "are (q%u, q%u), want a[%d]=q%u and b[%d]=q%u", W,
                              (unsigned long long)s, j, k, r->a, r->b,
                              k, qa[k], j, qb[j]);
            }

            for (size_t u = 0; u < n_k8; u++, s++) {
                const cq_rec *r = cq_mock_at(&m, s);

                n_acc++;
                if (r->op != cq_mock_at(&k8, u)->op)
                    cq_h_fail(__FILE__, __LINE__,
                              "mul W=%d step %llu: accumulate j=%d u=%llu is op "
                              "%d, but K8's own step %llu is op %d", W,
                              (unsigned long long)s, j, (unsigned long long)u,
                              (int)r->op, (unsigned long long)u,
                              (int)cq_mock_at(&k8, u)->op);
            }
        }

        /* The scan itself, made falsifiable: it must have consumed exactly the
         * compute half, and its two phases must split the way K11.md §3 derives
         * them — W(W+1)/2 partial products and W accumulates. */
        CHECK_EQ(s, head);
        CHECK_EQ(n_pp, (uint64_t)W * (uint64_t)(W + 1) / 2u);
        CHECK_EQ(n_acc, (uint64_t)W * (uint64_t)n_k8);

        /* The copy-out is W CX out of scratch into dst, in lane order — the
         * "^=" of Rule 7's contract, and the only place dst is written. Its
         * CONTROLS are checked too, as far as they can be without hard-coding
         * the pool's LIFO allocation order (D4): each must be a qubit that is
         * neither an operand's nor dst's, and the W of them must be pairwise
         * distinct. A copy-out reading the wrong SCRATCH register survives that
         * and is L1's to catch. */
        for (int t = 0; t < W; t++) {
            const cq_rec *r = cq_mock_at(&m, head + (size_t)t);

            if (r->op != CQ_OP_CX || r->t != qd[t])
                cq_h_fail(__FILE__, __LINE__,
                          "mul W=%d copy-out %d: op %d target q%u, want CX into "
                          "dst[%d]=q%u", W, t, (int)r->op, r->t, t, qd[t]);

            if (in_set(r->a, qa, W) || in_set(r->a, qb, W) || in_set(r->a, qd, W))
                cq_h_fail(__FILE__, __LINE__,
                          "mul W=%d copy-out %d: reads q%u, which is an operand "
                          "or dst qubit rather than scratch", W, t, r->a);

            for (int u = 0; u < t; u++)
                if (cq_mock_at(&m, head + (size_t)u)->a == r->a)
                    cq_h_fail(__FILE__, __LINE__,
                              "mul W=%d copy-out: lanes %d and %d both read q%u",
                              W, u, t, r->a);
        }

        no_gate_targets_an_operand(&m, W, head, qa, qb, qd);
    }

    cq_mock_dispose(&k8);
    cq_mock_dispose(&m);
}
