/* tests/test_kernel_add_refmodel.inc — included by test_kernel_add.c.
 *
 * THE REFERENCE MODEL'S OWN CROSS-CHECK — no circuit, no qubit, no kernel.
 * Split out 2026-09-10 (`bd iwn`) on the seam `the KERNEL ↔ its REFERENCE
 * MODEL`, which test_kernel_mul.c and test_kernel_divrem.c had already taken
 * for the same purpose and on material carrying the identical section banner:
 * everything else in the parent file tests src/kernels/add.c, and this tests
 * tests/support/refmodel.c's cq_ref_w_add and cq_ref_w_sub.
 *
 * WHY THIS CUT AND NOT THE OTHER TWO, measured before choosing rather than
 * after (the figures are in `bd iwn` and in plan §3's M14 row, and are not
 * repeated here — two copies of a number is how one of them goes stale). The
 * parent stood at 297 of 300 with only Step 12's against-upstream split
 * recorded. The bead's own first candidate — the `x + 1` / `x + 3` constant-
 * increment cases against the two-register ones — is SPENT: every case with a
 * constant second operand already lives in test_kernel_add_upstream.inc, so
 * nothing moves under it. Its second — the R8/R9 mask witnesses — would leave
 * `run_masked`, `gates_of` and `wide_ref` defined in the parent with every
 * remaining caller inside an .inc, and would take away two of the three
 * subjects the parent's own header names as this suite's reason to exist.
 * This cut strands nothing: `ripple_ref` has exactly one caller and travels
 * with it.
 *
 * AND IT IS A SUBJECT CUT IN BOTH DIRECTIONS, which is the test a size cut
 * fails: afterwards the .c is entirely about the kernel and this file is
 * entirely about the oracle the kernel is judged against. An .inc rather than
 * a second binary because these are cases of the same suite — Rule 12's
 * sanctioned hatch, since the guard counts .c/.h/.py and the split is on a
 * subject boundary rather than at an arbitrary line.
 */

/* ---- L1's oracle, crossed against a model that shares nothing with it. --- */

/* THE FIRST VERSION OF THIS CASE COULD NOT FAIL, and the reason is worth
 * keeping because it is the shape this project has now shipped three times.
 * It crossed cq_ref_w_add against a one-word `(a + b) & cq_ref_mask(W)` at
 * widths 1..64 and asserted `.hi == 0`. Both halves are structurally vacuous
 * there: refmodel.c's `hi_mask(W)` returns 0 for every W <= 64, so `.hi` is
 * zeroed whatever the carry does, and `lo_mask` and `cq_ref_mask` are provably
 * the same function on 1..64, so the `.lo` comparison reduces to
 * `(a+b) & m == (a+b) & m`. Deleting the cross-seam carry term from
 * cq_ref_w_add left it green. TWO MODELS THAT SHARE AN IDIOM CROSS-CHECK
 * NOTHING; the one-word pair has been removed rather than kept as decoration.
 *
 * What replaces it is a BIT-SERIAL RIPPLE, and it is deliberately NOT in
 * refmodel.c. L1's oracle must not share the kernel's algorithm — the kernel
 * IS a ripple-carry circuit — so cq_ref_w_add stays word arithmetic and this
 * ripple lives here, as the cross-check only. Between them the two share no
 * operation: one propagates a carry bit by bit, the other adds two 64-bit
 * words and tests for wrap.
 *
 * AND IT SPANS THE SEAM. Widths 63, 64, 65, 80, 100, 127 and 128 put the carry
 * on both sides of bit 64, which is the one thing the old case could not
 * reach and the only place cq_ref_w_add's `(uint64_t)(lo < a.lo)` term does
 * any work. Verified falsifiable: deleting that term turns this case red. */
static cq_ref_w ripple_ref(cq_ref_w a, cq_ref_w b, int W, int invert_b)
{
    cq_ref_w r = cq_ref_w_zero();
    int carry = invert_b;                  /* the +1 of two's complement */

    for (int i = 0; i < W; i++) {
        int x = cq_ref_w_bit(a, i);
        int y = cq_ref_w_bit(b, i) ^ invert_b;

        if (x ^ y ^ carry) r = cq_ref_w_or(r, cq_ref_w_setbit(i));
        carry = (x & y) | (carry & (x ^ y));
    }
    return r;
}

CQ_TEST(l1s_oracle_agrees_with_an_independent_bit_serial_model)
{
    static const int WS[] = { 1, 2, 3, 8, 32, 63, 64, 65, 80, 100, 127, 128 };
    cq_bk_rng rng;

    cq_bk_rng_init(&rng, 0xADDEDull);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        cq_ref_w ones = cq_ref_w_ones(W);
        cq_ref_w zero = cq_ref_w_zero();

        for (int s = 0; s < 64; s++) {
            /* The corners first — all-ones + all-ones is the carry chain at
             * full length, which is where a dropped seam carry shows. */
            cq_ref_w A = (s < 4) ? ((s & 1) ? ones : zero)
                                 : cq_ref_w_make(cq_bk_rng_next(&rng),
                                                 cq_bk_rng_next(&rng), W);
            cq_ref_w B = (s < 4) ? ((s & 2) ? ones : zero)
                                 : cq_ref_w_make(cq_bk_rng_next(&rng),
                                                 cq_bk_rng_next(&rng), W);
            cq_ref_w got, want;

            got  = cq_ref_w_add(A, B, W);
            want = ripple_ref(A, B, W, 0);
            if (!cq_ref_w_eq(got, want))
                cq_h_fail(__FILE__, __LINE__,
                          "add W=%d: word model 0x%llx%016llx, ripple "
                          "0x%llx%016llx", W,
                          (unsigned long long)got.hi,  (unsigned long long)got.lo,
                          (unsigned long long)want.hi, (unsigned long long)want.lo);

            got  = cq_ref_w_sub(A, B, W);
            want = ripple_ref(A, B, W, 1);
            if (!cq_ref_w_eq(got, want))
                cq_h_fail(__FILE__, __LINE__,
                          "sub W=%d: word model 0x%llx%016llx, ripple "
                          "0x%llx%016llx", W,
                          (unsigned long long)got.hi,  (unsigned long long)got.lo,
                          (unsigned long long)want.hi, (unsigned long long)want.lo);
        }
    }
}
