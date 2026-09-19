/* tests/test_kernel_fsqrt_spans.inc — the LAYOUT's three instruments: the
 * span-disjointness scan over EVERY iteration's spans, the palindrome at an
 * asymmetric mask, and two programs in one region. Included by
 * test_kernel_fsqrt.c AFTER test_kernel_fsqrt_scan.inc, whose `fq_row_gates`
 * and `fq_std_kinds` it reuses.
 * NOTHING BELOW READS A GOLDEN.
 *
 * SPLIT OFF THE SCHEDULE HALF ON A REAL SEAM. Next door is "which of X / CX /
 * CCX does each slot emit"; here is "which BITS each row owns, and whether a
 * row stays inside them". M31's measured finding is that the two come apart:
 * collapsing two blocks onto one offset kept the value, the palindrome, the
 * pool, every gate count AND the slot scan, so a suite with only the schedule
 * half has a hole exactly the shape of its scratch map. K21 has 546 emitting
 * rows, 265 of which are a comparator, a subtractor or a mux inside the digit
 * loop — and sharing ONE iteration's spans across all sixty-four is the
 * cheapest mutant this kernel offers.
 */

/* Room for two whole programs plus a rail and a destination. */
enum { FQ_MAX_Q = 262144 };

/* ---- (c) Every row's span is disjoint from every other row's. ---------- */

CQ_TEST(the_rows_spans_are_pairwise_disjoint)
{
    static unsigned char std[FQ_STD_CAP];
    static int owner[FQ_MAX_Q];
    static size_t gates[CQ_FSQRT_MAX_ROWS];
    const cq_fsqrt_row *rows;
    cq_mock m, m2;
    size_t cursor = 0u;
    int n, bad = 0, seen_rows = 0, emitting = 0;
    long touched = 0;

    cq_mock_init(&m);
    cq_mock_init(&m2);
    rows = cq_fsqrt_rows(&n);

    CHECK(fq_row_gates(&m2, std, rows, n, gates));
    (void)fsqrt_run(FQ_SCAN_A, cq_ref_w_ones(W64), &m);

    for (int t = 0; t < FQ_MAX_Q; t++) owner[t] = -1;

    for (int r = 0; r < n && !bad; r++) {
        if (gates[r] == 0u) continue;
        emitting++;
        for (size_t g = 0; g < gates[r] && !bad; g++) {
            uint32_t tq;

            if (cursor >= cq_mock_count(&m)) {
                cq_h_fail(__FILE__, __LINE__,
                          "row %d: the stream ran out at %zu gates", r, cursor);
                bad = 1;
                break;
            }
            tq = cq_mock_at(&m, cursor++)->t;
            CHECK(tq < (uint32_t)FQ_MAX_Q);
            touched++;
            if (owner[tq] >= 0 && owner[tq] != r) {
                cq_h_fail(__FILE__, __LINE__,
                          "row %d gate %zu: q%u is written by row %d AND by "
                          "row %d — two rows of one program must not share a "
                          "bit, and two ITERATIONS least of all", r, g, tq,
                          owner[tq], r);
                bad = 1;
                break;
            }
            owner[tq] = r;
        }
        seen_rows++;
    }

    if (!bad) {
        /* NOT VACUOUS: every EMITTING row must have written something, or a
         * disjointness claim over rows that never ran is no claim at all. An
         * `or` row whose every lane folds away is still counted as emitting by
         * `fq_row_gates`, which returns ZERO for it — so `emitting` is the
         * number of rows that emitted at least one gate, not the number of
         * non-view rows. */
        CHECK(touched > 0);
        CHECK_EQ(seen_rows, emitting);
        CHECK(emitting > CQ_FSQRT_ITERS * 4);
        /* And the compute half is exactly what the scan walked, so the
         * copy-out and the reverse half are what is left. */
        CHECK_EQ((long long)cq_mock_count(&m),
                 (long long)(2u * cursor + (size_t)W64));
    }

    cq_mock_dispose(&m2);
    cq_mock_dispose(&m);
}

/* ---- (d) The palindrome, at an ASYMMETRIC mask. ------------------------ */

/* AN ALL-CLASSICAL MASK IS NOT A PALINDROME CASE, IT IS AN L5 CASE — the R9
 * short-circuit never enters cq_sandwich, so cq_mock_count is 0 and the check
 * is being asked about a stream that does not exist. `fsqrt` is UNARY, so
 * there is no second operand to make asymmetric and the mask has to be
 * asymmetric WITHIN the one rail: HALF THE LANES QUANTUM AND HALF CLASSICAL,
 * driven both ways round.
 *
 * WHICH HALF IS NOT ARBITRARY. `0x00000000ffffffff` leaves lanes 0-31 quantum
 * and lanes 32-63 — the whole exponent field and the top twenty fraction bits
 * — constant, so the class predicates, the `ea != 0` muxes and the parity path
 * all run over classical lanes while the radicand's low half is a wire.
 * `0xffffffff00000000` is the mirror, and it is the row where `a_lo` — bits
 * 0-5 of `ma_adj` — is quantum while the exponent is not. A fold that fired
 * only on one side of that boundary would hide in one order. */
CQ_TEST(the_compute_half_is_a_palindrome_at_an_asymmetric_mask)
{
    static const uint64_t MASKS[] = { 0x00000000ffffffffull,
                                      0xffffffff00000000ull };
    cq_mock m;

    cq_mock_init(&m);

    for (int side = 0; side < 2; side++) {
        size_t total, head;
        uint64_t got, want;

        got = fsqrt_run(FQ_SCAN_A, cq_ref_w_make(MASKS[side], 0u, W64), &m);
        want = ref_fsqrt(FQ_SCAN_A, 0u, W64);
        CHECK_EQ(got, want);

        total = cq_mock_count(&m);
        CHECK(total > (size_t)W64);            /* it really did sandwich */
        CHECK_EQ((long long)(total % 2u), 0);
        head = (total - (size_t)W64) / 2u;
        for (int i = 0; i < W64; i++)
            CHECK_EQ((int)cq_mock_at(&m, head + (size_t)i)->op, (int)CQ_OP_CX);
        if (!cq_mock_is_palindrome(&m, head, (size_t)W64))
            cq_h_fail(__FILE__, __LINE__,
                      "mask %016llx: the %zu-gate compute half does not read "
                      "back in reverse", (unsigned long long)MASKS[side], head);
    }

    cq_mock_dispose(&m);
}

/* ---- (e) Two programs in ONE region, at two offsets. ------------------- */

/* THE SOLE DETECTOR FOR A PROGRAM THAT IGNORES ITS OWN `off`, AND IT HAD TO BE
 * WRITTEN AS A POSITIVE CASE. Every span this module hands out goes through
 * `cq_fs_sp`, which is the ONE place `k->off` is added; drop it and the whole
 * program slides to the bottom of the caller's region. The kernel always
 * passes `off = 0`, so from every other case in this suite the mutation is
 * BEHAVIOURALLY INERT — the value, the palindrome, the pool, the slot scan,
 * the span-disjointness scan and the golden all stay green. Only a SECOND
 * program at a SECOND offset in one region sees it, because the two then
 * collide. M18, M31, M32, M34 and M36 all carry the same case for the same
 * reason, and the death cases do not substitute for it: they drive the fit
 * check, which is a different line. */
CQ_TEST(two_programs_in_one_region_do_not_collide)
{
    static int owner[FQ_MAX_Q];
    static const uint64_t VA[] = { FSQ_PI, FSQ_TENTH };
    int nv = (int)(sizeof VA / sizeof VA[0]);
    cq_mock m;

    cq_mock_init(&m);

    for (int v = 0; v < nv; v++) {
        cq_fsqrt_block k[2];
        cq_scratch scr;
        cq_ctx ctx;
        cq_sink s = cq_mock_sink(&m);
        uint32_t reg = cq_fsqrt_region();
        int32_t ha;
        int bad = 0;

        cq_mock_reset(&m);
        cq_ctx_init(&ctx, &s);
        ha = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(VA[v], 0u, W64),
                         cq_ref_w_ones(W64));
        cq_scratch_alloc(&scr, 2u * reg);
        for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&ctx, &scr.bits[i]);

        for (int b = 0; b < 2; b++) {
            k[b].a = cq_reg_cbits(&ctx.regs, ha);
            k[b].scr = &scr;
            k[b].off = (b == 0) ? 0u : reg;
        }
        CHECK_EQ(k[0].off, 0u);
        CHECK(k[1].off > 0u);

        for (int t = 0; t < FQ_MAX_Q; t++) owner[t] = -1;
        cq_mock_reset(&m);

        for (int b = 0; b < 2 && !bad; b++) {
            size_t before = cq_mock_count(&m);

            for (int u = 0; u < cq_fsqrt_steps(); u++) cq_fsqrt_step(&ctx, &k[b], u);

            for (size_t g = before; g < cq_mock_count(&m); g++) {
                uint32_t tq = cq_mock_at(&m, g)->t;

                CHECK(tq < (uint32_t)FQ_MAX_Q);
                if (owner[tq] >= 0 && owner[tq] != b) {
                    cq_h_fail(__FILE__, __LINE__,
                              "two fsqrt programs at off 0 and off %u: q%u is "
                              "written by BOTH — a program that ignored its "
                              "own `off` slides onto its neighbour",
                              k[1].off, tq);
                    bad = 1;
                    break;
                }
                owner[tq] = b;
            }
        }

        /* AND THE VALUES, which is the claim a disjointness scan cannot make
         * on its own: each program's `result` still holds its own answer after
         * the other has run over the same region. */
        for (int b = 0; b < 2 && !bad; b++) {
            uint64_t want = ref_fsqrt(VA[v], 0u, W64);
            uint64_t got = fq_read(&ctx, cq_fsqrt_result(&k[b]));

            if (got != want)
                cq_h_fail(__FILE__, __LINE__,
                          "two programs on sqrt(%016llx): program %d's result "
                          "= %016llx, want %016llx",
                          (unsigned long long)VA[v], b,
                          (unsigned long long)got, (unsigned long long)want);
        }

        /* The reverse halves, in the mirror order, and then the pool claim —
         * every scratch bit provably back at |0> with two programs sharing one
         * region. */
        for (int b = 1; b >= 0; b--)
            for (int u = cq_fsqrt_steps() - 1; u >= 0; u--)
                cq_fsqrt_step(&ctx, &k[b], u);

        for (uint32_t i = 0; i < scr.n; i++) {
            if (!cq_bit_is_qubit(scr.bits[i])) continue;
            if (!cq_shadow_known_zero(&ctx.shadow, cq_bit_qindex(scr.bits[i])))
                cq_h_fail(__FILE__, __LINE__,
                          "two fsqrt programs: scratch bit %u (q%u) is not "
                          "back at |0> after both reverse halves", i,
                          cq_bit_qindex(scr.bits[i]));
            scr.bits[i] = cq_bit_zero();
        }

        cq_scratch_dispose(&scr);
        cq_ctx_dispose(&ctx);
    }

    cq_mock_dispose(&m);
}
