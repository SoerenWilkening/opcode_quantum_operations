/* tests/test_kernel_divrem_refmodel.inc — included by test_kernel_divrem.c.
 *
 * THE REFERENCE MODEL'S OWN CROSS-CHECK — no circuit, no qubit, no kernel.
 * Split out on the seam test_kernel_mul.c used for the same purpose: everything
 * else in the parent file tests divrem_u.c, and this tests refmodel.c.
 *
 * IT MATTERS MORE HERE THAN ANYWHERE ELSE IN THE PROJECT, and refmodel.h says
 * why out loud: `cq_ref_w_udiv` is the ONE reference in this codebase that
 * shares its recurrence with the kernel it checks. Every other one is
 * deliberately a different algorithm — word arithmetic against a ripple-carry
 * adder, limb multiplication against bit-serial shift-add, a sign-bit branch
 * against a bias flip — because a reference derived from the implementation
 * proves nothing. A second, genuinely different 128-bit division algorithm
 * (Knuth D on 32-bit limbs) is a large piece of error-prone arithmetic whose
 * own correctness would then need a third model, so the independence is bought
 * a different way, in two layers:
 *
 *   1. UP TO 64 BITS, AGAINST THE HARDWARE. C's `/` and `%` on uint64_t are as
 *      independent a derivation as exists — a different algorithm, in silicon —
 *      and every width the ABI ships below the seam is covered, exhaustively at
 *      W <= 8 and by seeded sampling above it.
 *   2. AT EVERY WIDTH INCLUDING 128, AGAINST THE DIVISION IDENTITY. `a == q*b +
 *      r` exactly, with `r <u b` whenever `b != 0`, checked through
 *      cq_ref_w_mul and cq_ref_w_add — two functions that share nothing with
 *      the divider. This is what carries i128, where no hardware operator
 *      exists and where the reference's 64-bit word seam actually falls inside
 *      a register. Note the identity holds for `b == 0` too, and not by
 *      accident: D3 gives `q = 2^W - 1` and `r = a`, so `q*0 + a = a`.
 */

/* `a == q*b + r`, exact over the integers because `q*b <= a` — so the mod-2^W
 * arithmetic of cq_ref_w_mul and cq_ref_w_add cannot wrap. Plus `r <u b`, the
 * half that says the loop ran to completion rather than stopping early. */
static void divides_exactly(cq_ref_w a, cq_ref_w b, int W)
{
    cq_ref_w q = cq_ref_w_udiv(a, b, W);
    cq_ref_w r = cq_ref_w_urem(a, b, W);
    cq_ref_w back = cq_ref_w_add(cq_ref_w_mul(q, b, W), r, W);

    if (!cq_ref_w_eq(back, a))
        cq_h_fail(__FILE__, __LINE__,
                  "W=%d: q*b + r = 0x%llx%016llx, want a = 0x%llx%016llx", W,
                  (unsigned long long)back.hi, (unsigned long long)back.lo,
                  (unsigned long long)a.hi,    (unsigned long long)a.lo);

    /* `r < b` is meaningless at b = 0 and D3 gives r = a there. Asked of
     * cq_ref_icmp rather than of the divider itself, so the check is not
     * circular. */
    if (!cq_ref_w_is_zero(b))
        CHECK(cq_ref_icmp(CQ_ICMP_ULT, r, b, W));
}

/* And the signed pair against C's own truncating division, wherever C defines
 * it. Sign-magnitude IS truncation toward zero, so this is a real differential
 * and not a restatement. C leaves `typemin / -1` undefined; the kernel wraps to
 * `typemin`, which the M20 suite pins separately. */
static void signed_matches_c(int64_t sa, int64_t sb, int W)
{
    cq_ref_w a = cq_ref_w_make((uint64_t)sa, 0u, W);
    cq_ref_w b = cq_ref_w_make((uint64_t)sb, 0u, W);
    int64_t va = cq_ref_sext(a.lo, W), vb = cq_ref_sext(b.lo, W);
    int64_t tmin = cq_ref_sext((uint64_t)1 << (W - 1), W);

    if (vb == 0) return;
    if (vb == -1 && va == tmin) return;              /* UB in C; M20 pins it */

    CHECK_EQ(cq_ref_sext(cq_ref_w_sdiv(a, b, W).lo, W), va / vb);
    CHECK_EQ(cq_ref_sext(cq_ref_w_srem(a, b, W).lo, W), va % vb);
}

CQ_TEST(l1s_oracle_agrees_with_the_hardware_divider)
{
    cq_bk_rng rng;

    /* Exhaustive below the seam, b = 0 included. 87,380 pairs, no circuit. */
    for (int W = 1; W <= 8; W++) {
        uint64_t span = cq_ref_mask(W) + 1u;

        for (uint64_t x = 0; x < span; x++)
            for (uint64_t y = 0; y < span; y++) {
                cq_ref_w a = cq_ref_w_make(x, 0u, W);
                cq_ref_w b = cq_ref_w_make(y, 0u, W);

                if (y != 0u) {
                    CHECK_EQ(cq_ref_w_udiv(a, b, W).lo, x / y);
                    CHECK_EQ(cq_ref_w_urem(a, b, W).lo, x % y);
                } else {
                    CHECK_EQ(cq_ref_w_udiv(a, b, W).lo, cq_ref_mask(W));
                    CHECK_EQ(cq_ref_w_urem(a, b, W).lo, x);
                }
                CHECK_EQ(cq_ref_w_udiv(a, b, W).hi, 0u);
                divides_exactly(a, b, W);
                signed_matches_c((int64_t)x, (int64_t)y, W);
            }
    }

    /* Sampled at the wide widths a uint64_t can still hold, plus the corners a
     * uniform sample reaches with probability ~0. */
    cq_bk_rng_init(&rng, 0xD1FD64ull);
    for (int wi = 0; wi < 3; wi++) {
        static const int WS[] = { 16, 32, 64 };
        int W = WS[wi];
        uint64_t mask = cq_ref_mask(W);

        for (int s = 0; s < 4096; s++) {
            uint64_t x = cq_bk_rng_next(&rng) & mask;
            uint64_t y = (s % 8 == 0) ? 0u
                       : (s % 8 == 1) ? 1u
                       : (s % 8 == 2) ? mask
                       : cq_bk_rng_next(&rng) & mask;
            cq_ref_w a = cq_ref_w_make(x, 0u, W);
            cq_ref_w b = cq_ref_w_make(y, 0u, W);

            if (y != 0u) {
                CHECK_EQ(cq_ref_w_udiv(a, b, W).lo, x / y);
                CHECK_EQ(cq_ref_w_urem(a, b, W).lo, x % y);
            }
            divides_exactly(a, b, W);
            signed_matches_c((int64_t)x, (int64_t)y, W);
        }
    }

    /* i128, where no hardware operator exists and the reference's word seam
     * falls inside the register. The identity is the whole oracle here. */
    cq_bk_rng_init(&rng, 0xD1FD128ull);
    for (int s = 0; s < 512; s++) {
        cq_ref_w a = cq_ref_w_make(cq_bk_rng_next(&rng), cq_bk_rng_next(&rng), 128);
        cq_ref_w b = (s % 4 == 0)
                   ? cq_ref_w_make(cq_bk_rng_next(&rng), 0u, 128)   /* low-word b */
                   : cq_ref_w_make(cq_bk_rng_next(&rng), cq_bk_rng_next(&rng), 128);

        divides_exactly(a, b, 128);
    }

    divides_exactly(cq_ref_w_ones(128), cq_ref_w_zero(), 128);
    divides_exactly(cq_ref_w_ones(128), cq_ref_w_setbit(127), 128);
    divides_exactly(cq_ref_w_setbit(127), cq_ref_w_ones(128), 128);
    CHECK(cq_ref_w_eq(cq_ref_w_udiv(cq_ref_w_ones(128), cq_ref_w_zero(), 128),
                      cq_ref_w_ones(128)));
}
