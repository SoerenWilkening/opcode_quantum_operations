/* tests/test_kernel_addacc_sandwich.inc — included by test_kernel_addacc.c.
 *
 * K8 IN THE ONLY CONTEXT v1 ACTUALLY USES IT, which no other case here covers.
 * The parent file drives K8 on ordinary registers, which is the right way to
 * get exhaustive value coverage cheaply — but it is not how K8 ships. Every K8
 * call in v1 is a run of steps inside K11's compute half, over a scratch region
 * the sandwich driver pre-materialised at step 0. Three things are only true
 * there, and each is a way K8 can be wrong while every value test stays green:
 *
 *   - I6(a). K8 targets its own addend, so it is the one kernel that cannot
 *     lean on cq_emit_*'s `const cq_bit *` controls. With a sandwich ARMED the
 *     Debug scratch-extent check in emit.c sees every target, and this is the
 *     only case in the project that puts K8 under it.
 *   - THE REVERSE PASS IS THE DRIVER'S, not a loop this file wrote. The parent
 *     file's k8_reverse walks indices backwards by hand; here cq_sandwich does
 *     it, which is what K11 will do, and cq_mock_is_palindrome checks the
 *     ORDERED stream — the only detector with teeth against risk R8, whose
 *     measured witness has a different multiset with an identical total.
 *   - THE ANCILLA IS RELEASED. Standalone, `x` is a register nobody frees.
 *     Here it is a bit of the region, so cq_sandwich's epilogue hands it back
 *     through cq_ctx_release_qubit on the CQ_ZERO_BY_PALINDROME premise — and
 *     cq_shadow_retire's determinate-and-non-zero abort fires in BOTH
 *     configurations if the construction did not in fact clean it.
 *
 * WHAT THIS IS, STRUCTURALLY, IS K11's SKELETON AT ONE ITERATION. K11 is
 * `for j: build pp[j] by Toffolis from a and b; cq_addacc(accum, pp[j], x[j])`
 * (K11.md §2b). Replace the Toffoli partial-product phase with a plain CX load
 * and you have the compute half below. It therefore also answers the question
 * M18 will ask first: can a caller interleave its own steps with K8's in one
 * flat index space? It can, and the offset arithmetic is the whole of it.
 */

/* acc ++ b ++ x, one contiguous region — emit.c's I6(a) check is a pointer
 * RANGE test, so a kernel carves one region and never allocates two. */
typedef struct {
    cq_bit         *dst;
    const cq_bit   *A, *B;
    cq_addacc_block k;
} sw_env;

enum { SW_LOAD_PHASES = 2 };

/* Phase L (2W steps): copy the two real operands into scratch, interleaved one
 * gate per step. Phase A (6W-5 steps): the accumulate. */
static void sw_compute(cq_ctx *ctx, void *env, int s)
{
    sw_env *e = (sw_env *)env;
    int W = e->k.W;

    if (s < SW_LOAD_PHASES * W) {
        int i = s / SW_LOAD_PHASES;

        if (s % SW_LOAD_PHASES == 0) cq_emit_cx(ctx, &e->A[i], &e->k.acc[i]);
        else                         cq_emit_cx(ctx, &e->B[i], &e->k.b[i]);
        return;
    }

    cq_addacc_step(ctx, &e->k, s - SW_LOAD_PHASES * W);
}

static void sw_copyout(cq_ctx *ctx, void *env, int i)
{
    sw_env *e = (sw_env *)env;

    cq_emit_cx(ctx, &e->k.acc[i], &e->dst[i]);
}

/* Runs one sandwiched accumulate and reports what it produced. `m` records the
 * kernel only — the operand registers are built before the mock is reset. */
static void sw_run(cq_mock *m, int W, cq_ref_w va, cq_ref_w vb,
                   cq_ref_w *out, cq_pc_snap *before, cq_pc_snap *after,
                   int *dst_is_only_new)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_ref_w all = cq_ref_w_ones(W);
    cq_scratch scr;
    sw_env e;
    int32_t hA, hB, hd, hs[3];

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);

    hA = cq_bk_reg_w(&ctx, (uint32_t)W, va, all);
    hB = cq_bk_reg_w(&ctx, (uint32_t)W, vb, all);
    hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);

    *before = cq_pc_take(&ctx);
    cq_mock_reset(m);

    cq_scratch_alloc(&scr, (uint32_t)(2 * W + 1));

    e.dst   = cq_reg_bits(&ctx.regs, hd);
    e.A     = cq_reg_cbits(&ctx.regs, hA);
    e.B     = cq_reg_cbits(&ctx.regs, hB);
    e.k.acc = cq_scratch_span(&scr, 0u,                  (uint32_t)W);
    e.k.b   = cq_scratch_span(&scr, (uint32_t)W,         (uint32_t)W);
    e.k.x   = cq_scratch_span(&scr, (uint32_t)(2 * W),   1u);
    e.k.W   = W;

    cq_sandwich(&ctx, &scr, sw_compute,
                SW_LOAD_PHASES * W + cq_addacc_steps(W), sw_copyout, W, &e);
    cq_scratch_dispose(&scr);

    *out   = cq_pc_value_w(&ctx, hd);
    *after = cq_pc_take(&ctx);

    hs[0] = hA; hs[1] = hB; hs[2] = hd;
    *dst_is_only_new = cq_pc_live_is_exactly(&ctx, hs, 3u);

    cq_ctx_dispose(&ctx);
}

CQ_TEST(k8_inside_a_sandwich_is_a_palindrome_and_leaves_scratch_clean)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8, 16 };
    cq_mock m;

    cq_mock_init(&m);

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j];
        size_t head = (size_t)(SW_LOAD_PHASES * W + cq_addacc_steps(W));
        cq_ref_w got;
        cq_pc_snap before, after;
        int only_new = 0;

        sw_run(&m, W, cq_ref_w_make(0xD3u, 0u, W), cq_ref_w_make(0x8Fu, 0u, W),
               &got, &before, &after, &only_new);

        /* [compute] [copy-out] [compute reversed]. A gate COUNT cannot see a
         * divergence here; only the ordered comparison can. */
        CHECK(cq_mock_is_palindrome(&m, head, (size_t)W));

        /* 8W-5 forward + W copy-out + 8W-5 reverse. Stated as a literal so a
         * change in either phase's length has to be acknowledged. */
        CHECK_EQ(cq_mock_count(&m), 2u * head + (size_t)W);
        CHECK_EQ(head, (size_t)(8 * W - 5));

        /* The region went back: `live` is dst's W qubits over the baseline,
         * and no scratch index stayed live. */
        CHECK_EQ(after.live, before.live + (uint32_t)W);
        CHECK(only_new);
    }

    cq_mock_dispose(&m);
}

/* THE VALUE, THROUGH THE DRIVER. The parent file proves `acc += b` on ordinary
 * registers; this proves the same construction survives being run as somebody
 * else's compute half — pre-materialised operands, driver-owned reversal,
 * driver-owned release — and still lands `A + B` in `dst`.
 *
 * Note what the sandwich makes this: an out-of-place adder built out of an
 * in-place one, `dst ^= (A + B) mod 2^W` at 2(8W-5) + W = 17W-10 gates. K6's
 * ripple does it in 11W-4, which is cheaper for every W >= 1 here and is
 * exactly why K8 must NEVER be substituted for K6 (K08.md §5 point 4). This
 * case is a test of the composition, not a proposal to use it. */
CQ_TEST(a_sandwiched_k8_computes_the_sum_of_two_real_operands)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8 };
    cq_mock m;

    cq_mock_init(&m);

    /* THE SHARED CONSTANT BUDGET (2026-08-20): sw_run builds the region, runs
     * the sandwiched accumulator and reads dst back, so each iteration is a full
     * circuit execution. This used to enumerate every (a, b) pair below the
     * 4-bit span at six widths. The carry corners are forced first — all-ones
     * plus one is the full-length ripple, which is what a dropped §3.5 Toffoli
     * gets wrong while low-weight values stay right — and the rest is drawn. */
    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j];
        uint64_t vmask = cq_ref_mask(W);
        const uint64_t ca[4] = { 0u, vmask, vmask, 1u };
        const uint64_t cb[4] = { 0u, vmask, 1u,    vmask };
        cq_bk_rng rng;
        int n = cq_kd_samples();

        cq_bk_rng_init(&rng, 0x5A9D31C8ull ^ (uint64_t)W);

        for (int s = 0; s < n; s++) {
            uint64_t a = (s < 4) ? ca[s] : (cq_bk_rng_next(&rng) & vmask);
            uint64_t b = (s < 4) ? cb[s] : (cq_bk_rng_next(&rng) & vmask);
            cq_ref_w va = cq_ref_w_make(a, 0u, W);
            cq_ref_w vb = cq_ref_w_make(b, 0u, W);
            cq_ref_w got;
            cq_pc_snap before, after;
            int only_new = 0;

            sw_run(&m, W, va, vb, &got, &before, &after, &only_new);

            if (!cq_ref_w_eq(got, cq_ref_w_add(va, vb, W)))
                cq_h_fail(__FILE__, __LINE__,
                          "sandwiched addacc W=%d: dst != a+b "
                          "(a=%llu b=%llu got=%llu)", W,
                          (unsigned long long)a, (unsigned long long)b,
                          (unsigned long long)got.lo);
        }
    }

    cq_mock_dispose(&m);
}
