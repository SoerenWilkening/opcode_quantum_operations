/* tests/test_kernel_fdiv_spans.inc — the LAYOUT's four instruments: the
 * span-disjointness scan over EVERY ONE of the 478 rows, the loop's own
 * offset arithmetic, the palindrome at an asymmetric mask, and two programs in
 * one region. Included by test_kernel_fdiv.c AFTER test_kernel_fdiv_scan.inc,
 * whose `fd_row_gates`, `fd_lane`, `fd_std_kinds` and `FD_SCAN_A`/`FD_SCAN_B`
 * it reuses.
 *
 * SPLIT OFF ON A REAL SEAM, not on a line count. Everything in scan.inc is
 * about the SLOT SCHEDULE — how many slots a row has and which of X / CX / CCX
 * each emits. Everything here is about the LAYOUT — which bits each row owns,
 * and whether a row stays inside them. M31's measured finding is that the two
 * come apart: collapsing two blocks onto one offset kept the value, the
 * palindrome, the pool, every gate count AND the slot scan.
 *
 * AND K17 IS WHERE THAT MATTERS MOST IN THE CATALOGUE. 338 emitting rows, of
 * which 280 are 56 instances of ONE four-block group. An off-by-one in the
 * per-iteration offset lands inside a DIFFERENT ITERATION of the SAME block —
 * plausible gates, a surviving palindrome, clean scratch, the right op kind at
 * every slot — and only the value moves. That is K17.md §6.5's risk 2 and it
 * is the shape this file exists for.
 *
 * NOTHING BELOW READS A GOLDEN.
 */

/* Room for two whole programs plus two rails and a destination. */
enum { FD_MAX_Q = 160000 };

/* ---- (c) Every row's span is disjoint from every other row's. ---------- */

CQ_TEST(the_rows_spans_are_pairwise_disjoint)
{
    static unsigned char std[FD_STD_CAP];
    static int owner[FD_MAX_Q];
    static size_t gates[CQ_FDIV_MAX_SEG + CQ_FDIV_ITER_ROWS * CQ_FDIV_N_ITERS
                        + CQ_FDIV_MAX_SEG];
    cq_mock m, m2;
    cq_ref_w q;
    size_t cursor = 0u;
    int n = cq_fdiv_n_rows(), bad = 0, seen_rows = 0, emitting = 0;
    long touched = 0;

    CHECK((size_t)n <= sizeof gates / sizeof gates[0]);

    cq_mock_init(&m);
    cq_mock_init(&m2);
    q = cq_ref_w_ones(W64);

    CHECK(fd_row_gates(&m2, std, gates));
    (void)fdiv_run(FD_SCAN_A, FD_SCAN_B, q, q, &m);

    for (int t = 0; t < FD_MAX_Q; t++) owner[t] = -1;

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
            CHECK(tq < (uint32_t)FD_MAX_Q);
            touched++;
            if (owner[tq] >= 0 && owner[tq] != r) {
                cq_h_fail(__FILE__, __LINE__,
                          "row %d gate %zu: q%u is written by row %d AND by "
                          "row %d — two blocks of one program must not share "
                          "a bit, and inside the loop that means iteration "
                          "%d has slid onto iteration %d",
                          r, g, tq, owner[tq], r,
                          (r - cq_fdiv_loop_base_row()) / CQ_FDIV_ITER_ROWS,
                          (owner[tq] - cq_fdiv_loop_base_row())
                          / CQ_FDIV_ITER_ROWS);
                bad = 1;
                break;
            }
            owner[tq] = r;
        }
        seen_rows++;
    }

    if (!bad) {
        /* NOT VACUOUS: every EMITTING row must have written something, or a
         * disjointness claim over rows that never ran is no claim at all. */
        CHECK(touched > 0);
        CHECK_EQ(seen_rows, emitting);
        CHECK_EQ(emitting, n - FD_SRC.view - FD_SRC.out);
        /* And the compute half is exactly what the scan walked, so the
         * copy-out and the reverse half are what is left. */
        CHECK_EQ((long long)cq_mock_count(&m),
                 (long long)(2u * cursor + (size_t)W64));
    }

    cq_mock_dispose(&m2);
    cq_mock_dispose(&m);
}

/* ---- (d) The loop's own offset arithmetic. ----------------------------- */

/* THE K12-SHAPED SLIDE, NAMED RATHER THAN INFERRED. Disjointness next door
 * says no two rows share a bit; this says the 56 iterations are laid out at a
 * CONSTANT STRIDE and that the stride is what one iteration owns. The two are
 * different claims and the gap between them is real: a layout that gave
 * iteration `t` an offset of `t * (iter_bits + 1)` is perfectly disjoint, uses
 * 56 more bits than `cq_fdiv_region()` says it does, and would abort in M08 on
 * a caller who sized the region correctly — in a message naming the REGION.
 *
 * TWO INDEPENDENT WALKS, and the second is the sharper one. (i) This file's
 * OWN prefix sum over the row table and the modules' own region functions,
 * never `cq_fd_map_build`'s. (ii) The TARGETS the kernel actually wrote: for
 * every `t >= 2` and every gate of every row, the qubit is exactly
 * `cq_fdiv_iter_region()` above the corresponding gate of the previous
 * iteration. `t = 1` is excluded from the per-gate half and only from it,
 * because D-K17-4 makes iteration 0 fold differently, so its gate LIST is
 * shorter and the correspondence is not gate-for-gate. */
CQ_TEST(the_loop_is_fifty_six_bodies_at_a_constant_stride)
{
    static unsigned char std[FD_STD_CAP];
    static size_t gates[CQ_FDIV_MAX_SEG + CQ_FDIV_ITER_ROWS * CQ_FDIV_N_ITERS
                        + CQ_FDIV_MAX_SEG];
    static size_t first[CQ_FDIV_MAX_SEG + CQ_FDIV_ITER_ROWS * CQ_FDIV_N_ITERS
                        + CQ_FDIV_MAX_SEG];
    cq_mock m, m2;
    cq_ref_w q;
    int n = cq_fdiv_n_rows(), base = cq_fdiv_loop_base_row(), bad = 0;
    uint32_t o = 0u, loop_off = 0u;

    /* (i) THE PREFIX SUM, this file's own. */
    for (int i = 0; i < n; i++) {
        cq_fdiv_row r;

        cq_fdiv_row_at(i, &r);
        if (i == base) loop_off = o;
        if (i >= base && i < cq_fdiv_post_base_row()) {
            int u = i - base, t = u / CQ_FDIV_ITER_ROWS;

            if (u % CQ_FDIV_ITER_ROWS == 0
                && o != loop_off + (uint32_t)t * cq_fdiv_iter_region())
                cq_h_fail(__FILE__, __LINE__,
                          "iteration %d starts at bit %u; a constant stride of "
                          "%u from %u would put it at %u", t, o,
                          cq_fdiv_iter_region(), loop_off,
                          loop_off + (uint32_t)t * cq_fdiv_iter_region());
        }
        o += fd_ref_row_region(&r);
    }
    CHECK_EQ(o, cq_fdiv_region());
    /* The loop really is the bulk of it — 56 strides out of the whole. */
    CHECK_EQ(cq_fdiv_post_base_row() - base,
             CQ_FDIV_ITER_ROWS * CQ_FDIV_N_ITERS);
    CHECK((uint32_t)CQ_FDIV_N_ITERS * cq_fdiv_iter_region()
          < cq_fdiv_region());

    /* (ii) THE TARGETS THE KERNEL WROTE. */
    cq_mock_init(&m);
    cq_mock_init(&m2);
    q = cq_ref_w_ones(W64);
    CHECK(fd_row_gates(&m2, std, gates));
    (void)fdiv_run(FD_SCAN_A, FD_SCAN_B, q, q, &m);

    {
        size_t cursor = 0u;

        for (int i = 0; i < n; i++) { first[i] = cursor; cursor += gates[i]; }
        CHECK_EQ((long long)cq_mock_count(&m),
                 (long long)(2u * cursor + (size_t)W64));
    }

    for (int t = 2; t < CQ_FDIV_N_ITERS && !bad; t++)
        for (int j = 0; j < CQ_FDIV_ITER_ROWS && !bad; j++) {
            int i  = base + t * CQ_FDIV_ITER_ROWS + j;
            int ip = i - CQ_FDIV_ITER_ROWS;

            if (gates[i] != gates[ip]) {
                cq_h_fail(__FILE__, __LINE__,
                          "iteration %d row %d emits %zu gates and iteration "
                          "%d's emits %zu — past t = 0 every body must be the "
                          "same circuit", t, j, gates[i], t - 1, gates[ip]);
                bad = 1;
                break;
            }
            for (size_t g = 0; g < gates[i] && !bad; g++) {
                uint32_t a = cq_mock_at(&m, first[ip] + g)->t;
                uint32_t b = cq_mock_at(&m, first[i] + g)->t;

                if (b != a + cq_fdiv_iter_region()) {
                    cq_h_fail(__FILE__, __LINE__,
                              "iteration %d row %d gate %zu targets q%u; "
                              "iteration %d's targets q%u, a stride of %d "
                              "where one iteration owns %u bits",
                              t, j, g, b, t - 1, a, (int)b - (int)a,
                              cq_fdiv_iter_region());
                    bad = 1;
                }
            }
        }

    cq_mock_dispose(&m2);
    cq_mock_dispose(&m);
}

/* ---- (e) The palindrome, at an ASYMMETRIC mask. ------------------------ */

/* AN ALL-CLASSICAL MASK PAIR IS NOT A PALINDROME CASE, IT IS AN L5 CASE — the
 * R9 short-circuit never enters cq_sandwich, so cq_mock_count is 0 and the
 * check is being asked about a stream that does not exist. The mask that DOES
 * belong here is risk R8's named witness, `a` all Q and `b` all CQ_BIT_ZERO,
 * which is in no symmetric mask set; the mirror is here too because `fdiv`'s
 * two operands are emphatically not interchangeable — one is the dividend that
 * seeds `r_0` and the other is the loop-invariant subtrahend, and the NaN row
 * reads `a` first — so a fold that fired only on the second operand would hide
 * in one order. */
CQ_TEST(the_compute_half_is_a_palindrome_at_an_asymmetric_mask)
{
    cq_mock m;
    cq_ref_w all_q = cq_ref_w_ones(W64), none = cq_ref_w_zero();

    cq_mock_init(&m);

    for (int side = 0; side < 2; side++) {
        size_t total, head;
        uint64_t got, want;

        got = fdiv_run(FD_SCAN_A, FD_SCAN_B, side == 0 ? all_q : none,
                       side == 0 ? none : all_q, &m);
        want = ref_fdiv(FD_SCAN_A, FD_SCAN_B, W64);
        CHECK_EQ(got, want);

        total = cq_mock_count(&m);
        CHECK(total > (size_t)W64);            /* it really did sandwich */
        CHECK_EQ((long long)(total % 2u), 0);
        head = (total - (size_t)W64) / 2u;
        for (int i = 0; i < W64; i++)
            CHECK_EQ((int)cq_mock_at(&m, head + (size_t)i)->op, (int)CQ_OP_CX);
        if (!cq_mock_is_palindrome(&m, head, (size_t)W64))
            cq_h_fail(__FILE__, __LINE__,
                      "%s asymmetric mask: the %zu-gate compute half does not "
                      "read back in reverse",
                      side == 0 ? "a-quantum" : "b-quantum", head);
    }

    cq_mock_dispose(&m);
}

/* ---- (f) Two programs in ONE region, at two offsets. ------------------- */

/* THE SOLE DETECTOR FOR A PROGRAM THAT IGNORES ITS OWN `off`, AND IT HAD TO BE
 * WRITTEN AS A POSITIVE CASE. Every span this module hands out goes through
 * `cq_fd_map_build`, which is the ONE place `k->off` is added; drop it and the
 * whole program slides to the bottom of the caller's region. The kernel always
 * passes `off = 0`, so from every other case in this suite the mutation is
 * BEHAVIOURALLY INERT — the value, the palindrome, the pool, the slot scan,
 * the span-disjointness scan, the stride case and the golden all stay green.
 * Only a SECOND program at a SECOND offset in one region sees it. M18, M31,
 * M32, M34 and M36 all carry the same case for the same reason. */
CQ_TEST(two_programs_in_one_region_do_not_collide)
{
    static int owner[FD_MAX_Q];
    static const uint64_t VA[] = { FD_TIEUP_A, FD_MANTOVF_A, CQ_F64_ONE };
    static const uint64_t VB[] = { FD_POW2_1000, FD_POW2_1000, FD_THREE };
    int nv = (int)(sizeof VA / sizeof VA[0]);
    cq_mock m;

    cq_mock_init(&m);

    for (int v = 0; v < nv; v++) {
        cq_fdiv_block k[2];
        cq_scratch scr;
        cq_ctx ctx;
        cq_sink s = cq_mock_sink(&m);
        uint32_t reg = cq_fdiv_region();
        int32_t ha, hb;
        int bad = 0;

        cq_mock_reset(&m);
        cq_ctx_init(&ctx, &s);
        ha = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(VA[v], 0u, W64),
                         cq_ref_w_ones(W64));
        hb = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(VB[v], 0u, W64),
                         cq_ref_w_ones(W64));
        cq_scratch_alloc(&scr, 2u * reg);
        for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&ctx, &scr.bits[i]);

        for (int b = 0; b < 2; b++) {
            k[b].a = cq_reg_cbits(&ctx.regs, ha);
            k[b].b = cq_reg_cbits(&ctx.regs, hb);
            k[b].scr = &scr;
            k[b].off = (b == 0) ? 0u : reg;
        }
        CHECK_EQ(k[0].off, 0u);
        CHECK(k[1].off > 0u);

        for (int t = 0; t < FD_MAX_Q; t++) owner[t] = -1;
        cq_mock_reset(&m);

        for (int b = 0; b < 2 && !bad; b++) {
            size_t before = cq_mock_count(&m);

            for (int u = 0; u < cq_fdiv_steps(); u++) cq_fdiv_step(&ctx, &k[b], u);

            for (size_t g = before; g < cq_mock_count(&m); g++) {
                uint32_t tq = cq_mock_at(&m, g)->t;

                CHECK(tq < (uint32_t)FD_MAX_Q);
                if (owner[tq] >= 0 && owner[tq] != b) {
                    cq_h_fail(__FILE__, __LINE__,
                              "two fdiv programs at off 0 and off %u: q%u is "
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
            const cq_bit *res = cq_fdiv_result(&k[b]);
            uint64_t want = ref_fdiv(VA[v], VB[v], W64), got = 0u;

            for (int i = 0; i < W64; i++) {
                cq_shadow sv;

                if (cq_bit_is_const(res[i])) {
                    if (cq_bit_value(res[i])) got |= UINT64_C(1) << (unsigned)i;
                    continue;
                }
                sv = cq_shadow_get(&ctx.shadow, cq_bit_qindex(res[i]));
                if (sv.unknown) { got = ~want; break; }
                if (sv.value) got |= UINT64_C(1) << (unsigned)i;
            }
            if (got != want)
                cq_h_fail(__FILE__, __LINE__,
                          "two programs on %016llx / %016llx: program %d's "
                          "result = %016llx, want %016llx",
                          (unsigned long long)VA[v], (unsigned long long)VB[v],
                          b, (unsigned long long)got,
                          (unsigned long long)want);
        }

        /* The reverse halves, in the mirror order, and then the pool claim —
         * every scratch bit provably back at |0> with two programs sharing one
         * region. */
        for (int b = 1; b >= 0; b--)
            for (int u = cq_fdiv_steps() - 1; u >= 0; u--)
                cq_fdiv_step(&ctx, &k[b], u);

        for (uint32_t i = 0; i < scr.n; i++) {
            if (!cq_bit_is_qubit(scr.bits[i])) continue;
            if (!cq_shadow_known_zero(&ctx.shadow, cq_bit_qindex(scr.bits[i])))
                cq_h_fail(__FILE__, __LINE__,
                          "two fdiv programs: scratch bit %u (q%u) is not back "
                          "at |0> after both reverse halves", i,
                          cq_bit_qindex(scr.bits[i]));
            scr.bits[i] = cq_bit_zero();
        }

        cq_scratch_dispose(&scr);
        cq_ctx_dispose(&ctx);
    }

    cq_mock_dispose(&m);
}
