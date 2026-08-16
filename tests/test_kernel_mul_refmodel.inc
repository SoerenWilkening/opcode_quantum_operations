/* ---- L1's oracle, crossed against a model that shares nothing with it. --- */

/* THREE DERIVATIONS OF ONE FUNCTION, and none of them may share a recurrence
 * with the circuit. cq_ref_w_mul is limb multiplication with an explicit high
 * word; mul.c's classical fold is column accumulation; this is bit-serial
 * shift-add over cq_ref_w_add. The kernel is bit-serial shift-add over a
 * CUCCARO carry chain, so this model is the closest of the three to it and is
 * deliberately the CROSS-CHECK rather than the oracle — refmodel.h's rule.
 *
 * AND IT SPANS THE SEAM. Widths 63, 64, 65, 100, 127 and 128 put partial
 * products on both sides of bit 64, which is the only place cq_ref_w_mul's
 * `a.lo*b.hi + a.hi*b.lo` term and mul64's `mid` carry do any work at all.
 * Verified falsifiable: drop either term and this case goes red. It costs no
 * circuit — no qubit is allocated here — which is why i128's real coverage
 * lives in this case and in the L4 golden rather than in a sweep that would
 * emit 212k gates per call. */
static cq_ref_w shift_add_ref(cq_ref_w a, cq_ref_w b, int W)
{
    cq_ref_w acc = cq_ref_w_zero();

    for (int j = 0; j < W; j++) {
        cq_ref_w term = cq_ref_w_zero();

        if (!cq_ref_w_bit(b, j)) continue;
        for (int k = 0; k + j < W; k++)
            if (cq_ref_w_bit(a, k)) term = cq_ref_w_or(term, cq_ref_w_setbit(k + j));
        acc = cq_ref_w_add(acc, term, W);
    }
    return acc;
}

CQ_TEST(l1s_oracle_agrees_with_an_independent_shift_add)
{
    static const int WS[] = { 1, 2, 3, 8, 32, 63, 64, 65, 80, 100, 127, 128 };
    cq_bk_rng rng;

    cq_bk_rng_init(&rng, 0x3D17ull);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        cq_ref_w ones = cq_ref_w_ones(W);
        cq_ref_w zero = cq_ref_w_zero();

        for (int s = 0; s < 64; s++) {
            cq_ref_w A = (s < 4) ? ((s & 1) ? ones : zero)
                                 : cq_ref_w_make(cq_bk_rng_next(&rng),
                                                 cq_bk_rng_next(&rng), W);
            cq_ref_w B = (s < 4) ? ((s & 2) ? ones : zero)
                                 : cq_ref_w_make(cq_bk_rng_next(&rng),
                                                 cq_bk_rng_next(&rng), W);
            cq_ref_w got = cq_ref_w_mul(A, B, W);
            cq_ref_w want = shift_add_ref(A, B, W);

            if (!cq_ref_w_eq(got, want))
                cq_h_fail(__FILE__, __LINE__,
                          "mul W=%d: limb model 0x%llx%016llx, shift-add "
                          "0x%llx%016llx", W,
                          (unsigned long long)got.hi,  (unsigned long long)got.lo,
                          (unsigned long long)want.hi, (unsigned long long)want.lo);
        }
    }
}
