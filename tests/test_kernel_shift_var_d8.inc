/* tests/test_kernel_shift_var_d8.inc — included by test_kernel_shift_var.c.
 *
 * D8 ACROSS THE MODULE BOUNDARY, plus the two entry dispatches. Split off for
 * Rule 12's reason and on the seam the compare suite used: the .c keeps the
 * sweeps and the goldens, the .inc keeps the claims that are about more than
 * one module.
 *
 * THE CROSS-CHECK IS THE POINT OF THE FILE. bd -mf4's note: "ADD A CROSS-CHECK
 * TEST: M11 and M12 on the same (W, k, value) must produce the same VALUE.
 * That is the assertion D8 exists for and neither module's own suite can make
 * it alone." Each suite compares its kernel against a reference that applies
 * the same reduction through the same `cq_shift_stages`, so both can be green
 * while disagreeing with each other about everything the reduction does not
 * cover — which is precisely the saturating interval [W, 2^S) that ckd.16 was
 * filed about and got backwards three times.
 */

/* Runs one shift and returns dst's value. `qk` all-zero drives the constant
 * path; all-ones forces the barrel, which is the only way to make M12 do its
 * own work rather than delegate. */
static cq_ref_w run_shift(cq_kernel_fn f, int W, cq_ref_w va, cq_ref_w qa,
                          cq_ref_w vk, cq_ref_w qk,
                          cq_counter *cnt, uint32_t *peak)
{
    cq_ctx ctx;
    cq_counter c;
    cq_sink sink;
    cq_ref_w out;

    cq_count_reset(&c);
    sink = cq_sink_counter(&c);
    cq_ctx_init(&ctx, &sink);

    int32_t ha = cq_bk_reg_w(&ctx, (uint32_t)W, va, qa);
    int32_t hb = cq_bk_reg_w(&ctx, (uint32_t)W, vk, qk);
    int32_t hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);

    /* Building the operands materialises their set bits, which emits X. */
    cq_pc_snap before = cq_pc_take(&ctx);
    cq_count_reset(&c);

    f(&ctx, cq_reg_bits(&ctx.regs, hd), cq_reg_cbits(&ctx.regs, ha),
      cq_reg_cbits(&ctx.regs, hb), W);

    cq_pc_snap after = cq_pc_take(&ctx);

    if (cnt)  *cnt  = c;
    if (peak) *peak = after.peak - before.peak;

    out = cq_pc_value_w(&ctx, hd);
    cq_ctx_dispose(&ctx);
    return out;
}

/* Every amount in [0, 2^S) up to 32 of them; above that, a stride that always
 * keeps 0, W-1, W, W+1 and 2^S-1 — the five that bracket the saturating
 * interval, which is the whole reason this reduced set is allowed to exist.
 * Returns how many it wrote, and the caller prints it. */
enum { D8_MAX_AMOUNTS = 128 };

static int amount_set(int W, int *out)
{
    int S = cq_shift_stages(W);
    int span = 1 << S;
    int keep = 32, n = 0, stride;

    if (span <= keep) {
        for (int k = 0; k < span; k++) out[n++] = k;
        return n;
    }

    stride = (span + keep - 1) / keep;
    for (int k = 0; k < span; k += stride) out[n++] = k;

    /* The bracket, whatever the stride landed on. Duplicates are harmless —
     * a repeated case costs one run and asserts the same thing. */
    out[n++] = W - 1;
    out[n++] = W;
    if (W + 1 < span) out[n++] = W + 1;
    out[n++] = span - 1;
    return n;
}

/* TWO VALUES ABOVE 32 BITS, THREE BELOW, and both sets carry an asymmetric
 * pattern so a shift in the wrong direction cannot look like a shift in the
 * right one. all-ones is the only value for which shl and lshr agree, so it is
 * never the only one used. */
static int value_set(int W, cq_ref_w *out)
{
    int n = 0;

    out[n++] = cq_ref_w_ones(W);
    out[n++] = cq_ref_w_or(cq_ref_w_setbit(W - 1), cq_ref_w_setbit(0));
    if (W <= 32) out[n++] = cq_ref_w_make(0x5A5A5A5Au, 0u, W);
    return n;
}

/* THE ASSERTION bd -mf4 FILED. Same (W, k, value), the constant path against
 * the barrel, at every shipped width and every amount the construction can
 * distinguish. Note the amount register is handed to M11 as CLASSICAL and to
 * M12 as ALL-QUANTUM: those are the two operand kinds that select the two
 * implementations, and running M12 with a classical amount would only re-test
 * M11 through a delegation. */
CQ_TEST(m11_and_m12_agree_at_every_width_amount_and_value)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 80, 128 };
    uint64_t cases = 0;

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j];
        int amounts[D8_MAX_AMOUNTS + 8];
        cq_ref_w vals[4];
        int na = amount_set(W, amounts);
        int nv = value_set(W, vals);

        for (int dir = 0; dir < N_DIRS; dir++) {
            if (dir == DIR_ASHR && W == 80) continue;   /* not shipped */

            for (int i = 0; i < na; i++)
                for (int v = 0; v < nv; v++) {
                    cq_ref_w vk = cq_ref_w_make((uint64_t)amounts[i], 0u, W);
                    cq_ref_w konst = run_shift(CONST_PATH[dir], W, vals[v],
                                               cq_ref_w_ones(W), vk,
                                               cq_ref_w_zero(), NULL, NULL);
                    cq_ref_w barrel = run_shift(SPEC[dir].kernel, W, vals[v],
                                                cq_ref_w_ones(W), vk,
                                                cq_ref_w_ones(W), NULL, NULL);

                    if (!cq_ref_w_eq(konst, barrel))
                        cq_h_fail(__FILE__, __LINE__,
                                  "D8 %s W=%d k=%d: M11 gives 0x%llx%016llx, "
                                  "M12 gives 0x%llx%016llx",
                                  SPEC[dir].name, W, amounts[i],
                                  (unsigned long long)konst.hi,
                                  (unsigned long long)konst.lo,
                                  (unsigned long long)barrel.hi,
                                  (unsigned long long)barrel.lo);
                    cases++;
                }
        }

        printf("# D8 cross-check W=%3d: %d amounts (of %d) x %d values\n",
               W, na, 1 << cq_shift_stages(W), nv);
    }

    printf("# D8 cross-check: %llu (W, dir, amount, value) cases\n",
           (unsigned long long)cases);
    fflush(stdout);
    CHECK(cases > 0u);
}

/* THE HALF OF D8 THAT ckd.16 GOT BACKWARDS. The bead asserted the barrel takes
 * the amount mod 2^S and stops there — a rotate — which would leave an
 * amount in [W, 2^S) shifting by more than the register is wide. It does not:
 * each stage zero-fills or sign-fills what it shifts in, and saturating shifts
 * compose, so the payload is annihilated exactly as the constant path
 * annihilates it. W = 5 (S = 3) and W = 3 (S = 2) are the smallest widths where
 * the interval is non-empty; at every power-of-two W it is empty, which is why
 * a suite that only swept 8/16/32/64 would never see this at all. */
CQ_TEST(d8_the_barrel_saturates_without_being_asked_to)
{
    cq_ref_w all5 = cq_ref_w_ones(5);
    cq_ref_w low4 = cq_ref_w_make(0x0Fu, 0u, 5);
    cq_ref_w zero = cq_ref_w_zero();
    cq_ref_w ones = cq_ref_w_ones(5);

    CHECK_EQ(cq_shift_stages(5), 3);

    /* Amounts 5, 6 and 7 all reach W and must annihilate. QUANTUM amount, so
     * this is the barrel and not a delegation. */
    for (uint64_t k = 5; k < 8; k++) {
        cq_ref_w vk = cq_ref_w_make(k, 0u, 5);

        CHECK(cq_ref_w_eq(run_shift(cq_kernel_shl_var, 5, all5,
                                    cq_ref_w_ones(5), vk, cq_ref_w_ones(5),
                                    NULL, NULL), zero));
        CHECK(cq_ref_w_eq(run_shift(cq_kernel_lshr_var, 5, all5,
                                    cq_ref_w_ones(5), vk, cq_ref_w_ones(5),
                                    NULL, NULL), zero));
        /* ashr fills with the sign: all-ones stays all-ones, 0x0F (sign 0)
         * goes to zero. Both are saturation, in opposite directions. */
        CHECK(cq_ref_w_eq(run_shift(cq_kernel_ashr_var, 5, all5,
                                    cq_ref_w_ones(5), vk, cq_ref_w_ones(5),
                                    NULL, NULL), ones));
        CHECK(cq_ref_w_eq(run_shift(cq_kernel_ashr_var, 5, low4,
                                    cq_ref_w_ones(5), vk, cq_ref_w_ones(5),
                                    NULL, NULL), zero));
    }

    /* W=3, S=2: the interval is exactly {3}. */
    CHECK_EQ(cq_shift_stages(3), 2);
    CHECK(cq_ref_w_eq(run_shift(cq_kernel_shl_var, 3, cq_ref_w_ones(3),
                                cq_ref_w_ones(3), cq_ref_w_make(3u, 0u, 3),
                                cq_ref_w_ones(3), NULL, NULL),
                      cq_ref_w_zero()));
}

/* THE OTHER HALF: at a power-of-two width, `x << W` is the IDENTITY, because
 * W mod 2^S is 0. This is the knowing delta from Bennett's constant path
 * (PRD §15) and it must NOT be "fixed" to zero. The sharpest case the bead
 * named, now run against the barrel rather than against M11. */
CQ_TEST(d8_a_shift_by_exactly_the_width_is_the_identity)
{
    static const int WS[] = { 8, 16, 32, 64 };

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j];
        cq_ref_w v = cq_ref_w_or(cq_ref_w_setbit(W - 1), cq_ref_w_setbit(0));
        cq_ref_w k = cq_ref_w_make((uint64_t)W, 0u, W);

        for (int dir = 0; dir < N_DIRS; dir++)
            CHECK(cq_ref_w_eq(run_shift(SPEC[dir].kernel, W, v,
                                        cq_ref_w_ones(W), k, cq_ref_w_ones(W),
                                        NULL, NULL), v));
    }
}

/* W=1 has ZERO stages, so no bit of the amount is read and the barrel never
 * runs — `amount_is_classical` is vacuously true and M11 answers. Pinned
 * because it is the one width where the whole construction degenerates, and
 * because a ceil(log2 1) = 1 slip would show up here and nowhere else. */
CQ_TEST(d8_a_width_of_one_ignores_the_amount_entirely)
{
    CHECK_EQ(cq_shift_stages(1), 0);

    for (uint64_t k = 0; k < 2; k++)
        for (int dir = 0; dir < N_DIRS; dir++) {
            cq_counter cnt;
            uint32_t peak = 0;
            cq_ref_w got = run_shift(SPEC[dir].kernel, 1,
                                     cq_ref_w_setbit(0), cq_ref_w_ones(1),
                                     cq_ref_w_make(k, 0u, 1), cq_ref_w_ones(1),
                                     &cnt, &peak);

            CHECK(cq_ref_w_eq(got, cq_ref_w_setbit(0)));
            CHECK_GATES(cnt.x, cnt.cx, cnt.ccx, 0u, 1u, 0u);
            CHECK_EQ(peak, 1u);          /* dst's one qubit and nothing else */
        }
}

/* A QUANTUM BIT AT OR ABOVE S IS NOT M12's BUSINESS EITHER. The barrel never
 * makes such a bit a mux control, so an amount whose low S bits are classical
 * still delegates — no scratch, no sandwich — and the answer is the constant
 * shift. The mirror of M11's `a_quantum_bit_above_the_stages_is_fine`, which
 * proves the same bit does not make M11 abort. */
CQ_TEST(a_quantum_bit_above_the_stages_does_not_force_the_barrel)
{
    int W = 8;                                   /* S = 3, so bits 3..7 are free */
    cq_ref_w v  = cq_ref_w_make(0x0Fu, 0u, W);
    cq_ref_w hi = cq_ref_w_andnot(cq_ref_w_ones(W),
                                  cq_ref_w_make(0x07u, 0u, W));

    CHECK_EQ(cq_shift_stages(W), 3);

    for (uint64_t k = 0; k < 8u; k++) {
        cq_counter cnt;
        uint32_t peak = 0;
        cq_ref_w got = run_shift(cq_kernel_shl_var, W, v, cq_ref_w_ones(W),
                                 cq_ref_w_make(k, 0u, W), hi, &cnt, &peak);

        CHECK(cq_ref_w_eq(got, cq_ref_w_shl(v, (int)k, W)));
        /* The delegation, proved by the pool rather than by the count: the
         * region would be W(3L+1) = 80 qubits, and dst owns at most 8. */
        CHECK(peak <= (uint32_t)W);
    }
}

/* L5 and R9 together. A classical amount must not enter the sandwich at all;
 * a fully classical pair must cost nothing whatsoever. */
CQ_TEST(r9_a_classical_amount_never_enters_the_sandwich)
{
    static const int WS[] = { 2, 5, 8, 32 };

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j];
        cq_ref_w v = cq_ref_w_ones(W);
        cq_ref_w k = cq_ref_w_make(3u, 0u, W);

        for (int dir = 0; dir < N_DIRS; dir++) {
            cq_counter cnt;
            uint32_t peak = 0;

            /* Quantum value, classical amount: the constant path, whose cost
             * is at most W CX and whose peak is dst alone. */
            run_shift(SPEC[dir].kernel, W, v, cq_ref_w_ones(W), k,
                      cq_ref_w_zero(), &cnt, &peak);
            CHECK_EQ(cnt.ccx, 0u);
            CHECK(cnt.cx <= (uint64_t)W);
            CHECK(peak <= (uint32_t)W);

            /* Everything classical: zero gates AND zero qubits. */
            run_shift(SPEC[dir].kernel, W, v, cq_ref_w_zero(), k,
                      cq_ref_w_zero(), &cnt, &peak);
            CHECK_GATES(cnt.x, cnt.cx, cnt.ccx, 0u, 0u, 0u);
            CHECK_EQ(peak, 0u);
        }
    }
}
