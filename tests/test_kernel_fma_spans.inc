/* tests/test_kernel_fma_spans.inc — M39, K20. THE THREE STRUCTURAL CLAIMS THAT
 * ARE NOT ABOUT VALUES: the rows' spans are pairwise disjoint, the compute
 * half is a palindrome, and two programs in one region do not collide.
 * Included by test_kernel_fma.c.
 *
 * EACH CATCHES WHAT THE OTHERS CANNOT, which is why they are three cases and
 * not one (M35's finding, bd memory
 * a-loop-kernel-is-a-template-plus-a-segmented-prefix-map). Disjointness is
 * pure arithmetic over the table and sees a layout collision with no circuit
 * run at all; the palindrome sees a reverse half that stops cancelling and is
 * the ONLY detector with teeth once a rail is rotation-tainted (PRD §10); and
 * two-programs-in-one-region is the SOLE detector for a program that ignores
 * its own `off`, which every other instrument in this suite reads as correct.
 */

enum { FA_MAX_Q = 200000 };

/* ---- (a) The rows' spans are pairwise disjoint and inside the region. --- */

/* A DROPPED OR DOUBLED REGION TERM PUTS TWO ROWS ON ONE QUBIT, and the value
 * can still come out right: the two rows may never be live at the same moment,
 * so the second simply overwrites scratch the first had finished with — until
 * the REVERSE half replays them in the other order and the cancellation is
 * gone. This case runs no circuit; it walks the same prefix sum the step
 * machine walks and checks the intervals. */
CQ_TEST(the_rows_spans_are_pairwise_disjoint)
{
    static uint32_t lo[CQ_FMA_MAX_ROWS], hi[CQ_FMA_MAX_ROWS];
    const cq_fma_row *rows;
    uint32_t o = 0u;
    int n, n_live = 0;
    int idx[CQ_FMA_MAX_ROWS];

    rows = cq_fma_rows(&n);
    for (int i = 0; i < n; i++) {
        uint32_t b = fa_bits_of(rows[i].op);

        if (rows[i].op == CQ_FUOP_CLASS)
            b = cq_fp_class_region((cq_fp_class)rows[i].s1);
        if (b > 0u) { lo[n_live] = o; hi[n_live] = o + b; idx[n_live] = i; n_live++; }
        o += b;
    }
    /* The walk and the module agree on the total — which is the claim a
     * per-row check cannot make and the one a dropped term moves. */
    CHECK_EQ(o, cq_fma_region());
    CHECK(n_live > 0 && n_live <= n);

    for (int i = 0; i < n_live; i++) {
        CHECK(hi[i] > lo[i]);
        CHECK(hi[i] <= cq_fma_region());
        for (int j = i + 1; j < n_live; j++)
            if (lo[i] < hi[j] && lo[j] < hi[i])
                cq_h_fail(__FILE__, __LINE__,
                          "rows %d [%u,%u) and %d [%u,%u) overlap",
                          idx[i], lo[i], hi[i], idx[j], lo[j], hi[j]);
    }
}

/* ---- (b) The compute half reads back in reverse. ----------------------- */

/* AT AN ASYMMETRIC MASK, DELIBERATELY. An all-classical mask pair is an L5
 * case and not a palindrome case: the R9 short-circuit never enters
 * `cq_sandwich`, so `cq_mock_count` is 0 and the palindrome is being asked
 * about a stream that does not exist. Two of the three operands are driven all
 * quantum and one all classical, three ways round, because K20's three sources
 * make three distinct asymmetries where a two-source kernel has one. */
CQ_TEST(the_compute_half_is_a_palindrome_at_an_asymmetric_mask)
{
    cq_mock m;
    cq_ref_w all_q = cq_ref_w_ones(W64), none = cq_ref_w_zero();

    cq_mock_init(&m);

    for (int classical = 0; classical < 3; classical++) {
        cq_ref_w q[3];
        size_t total, head;
        uint64_t got;

        for (int i = 0; i < 3; i++) q[i] = (i == classical) ? none : all_q;
        got = fma_run(FA_X, FA_X, FA_NEG_X2_RND, q, &m);
        CHECK_EQ(got, ref_fma(FA_X, FA_X, FA_NEG_X2_RND));

        total = cq_mock_count(&m);
        CHECK(total > (size_t)W64);            /* it really did sandwich */
        CHECK_EQ((long long)(total % 2u), 0);
        head = (total - (size_t)W64) / 2u;
        for (int i = 0; i < W64; i++)
            CHECK_EQ((int)cq_mock_at(&m, head + (size_t)i)->op, (int)CQ_OP_CX);
        if (!cq_mock_is_palindrome(&m, head, (size_t)W64))
            cq_h_fail(__FILE__, __LINE__,
                      "operand %d classical: the %zu-gate compute half does "
                      "not read back in reverse", classical, head);
    }

    cq_mock_dispose(&m);
}

/* ---- (c) Two programs in ONE region, at two offsets. ------------------- */

/* THE SOLE DETECTOR FOR A PROGRAM THAT IGNORES ITS OWN `off`, AND IT HAD TO BE
 * WRITTEN AS A POSITIVE CASE. Every span this module hands out goes through
 * `cq_fu_arm`, which is the ONE place `k->off` is added; drop it and the whole
 * program slides to the bottom of the caller's region. The kernel always
 * passes `off = 0`, so from every other case in this suite the mutation is
 * BEHAVIOURALLY INERT — the value, the palindrome, the pool, the
 * span-disjointness scan and the golden all stay green. Only a SECOND program
 * at a SECOND offset in one region sees it, because the two then collide. M18,
 * M31, M32, M34 and M36 all carry the same case for the same reason, and the
 * death cases do not substitute for it: they drive the fit check, which is a
 * different line. */
CQ_TEST(two_programs_in_one_region_do_not_collide)
{
    static int owner[FA_MAX_Q];
    cq_fma_block k[2];
    cq_scratch scr;
    cq_ctx ctx;
    cq_mock m;
    cq_sink s;
    uint32_t reg = cq_fma_region();
    int32_t ha, hb, hc;
    int bad = 0;

    cq_mock_init(&m);
    s = cq_mock_sink(&m);
    cq_ctx_init(&ctx, &s);
    ha = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(FA_X, 0u, W64),
                     cq_ref_w_ones(W64));
    hb = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(FA_X, 0u, W64),
                     cq_ref_w_ones(W64));
    hc = cq_bk_reg_w(&ctx, (uint32_t)W64,
                     cq_ref_w_make(FA_NEG_X2_RND, 0u, W64), cq_ref_w_ones(W64));
    cq_scratch_alloc(&scr, 2u * reg);
    for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&ctx, &scr.bits[i]);

    for (int b = 0; b < 2; b++) {
        k[b].a = cq_reg_cbits(&ctx.regs, ha);
        k[b].b = cq_reg_cbits(&ctx.regs, hb);
        k[b].c = cq_reg_cbits(&ctx.regs, hc);
        k[b].scr = &scr;
        k[b].off = (b == 0) ? 0u : reg;
    }
    /* The offsets are distinct and the second is not zero — which is what
     * makes the mutation representable at all. */
    CHECK_EQ(k[0].off, 0u);
    CHECK(k[1].off > 0u);

    for (int t = 0; t < FA_MAX_Q; t++) owner[t] = -1;
    cq_mock_reset(&m);

    for (int b = 0; b < 2 && !bad; b++) {
        size_t before = cq_mock_count(&m);

        for (int u = 0; u < cq_fma_steps(); u++) cq_fma_step(&ctx, &k[b], u);

        for (size_t g = before; g < cq_mock_count(&m); g++) {
            uint32_t tq = cq_mock_at(&m, g)->t;

            CHECK(tq < (uint32_t)FA_MAX_Q);
            if (owner[tq] >= 0 && owner[tq] != b) {
                cq_h_fail(__FILE__, __LINE__,
                          "two fma programs at off 0 and off %u: q%u is "
                          "written by BOTH — a program that ignored its own "
                          "`off` slides onto its neighbour", k[1].off, tq);
                bad = 1;
                break;
            }
            owner[tq] = b;
        }
    }

    /* AND THE VALUES, which is the claim a disjointness scan cannot make on
     * its own: each program's `result` still holds its own answer after the
     * other has run over the same region. */
    for (int b = 0; b < 2 && !bad; b++) {
        const cq_bit *res = cq_fma_result(&k[b]);
        uint64_t want = ref_fma(FA_X, FA_X, FA_NEG_X2_RND), got = 0u;

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
                      "two programs: program %d's result = %016llx, want "
                      "%016llx", b, (unsigned long long)got,
                      (unsigned long long)want);
    }

    /* The reverse halves, in the mirror order, and then the pool claim —
     * every scratch bit provably back at |0> with two programs sharing one
     * region. */
    for (int b = 1; b >= 0; b--)
        for (int u = cq_fma_steps() - 1; u >= 0; u--)
            cq_fma_step(&ctx, &k[b], u);

    for (uint32_t i = 0; i < scr.n; i++) {
        if (!cq_bit_is_qubit(scr.bits[i])) continue;
        if (!cq_shadow_known_zero(&ctx.shadow, cq_bit_qindex(scr.bits[i])))
            cq_h_fail(__FILE__, __LINE__,
                      "two fma programs: scratch bit %u (q%u) is not back at "
                      "|0> after both reverse halves", i,
                      cq_bit_qindex(scr.bits[i]));
        scr.bits[i] = cq_bit_zero();
    }

    cq_scratch_dispose(&scr);
    cq_ctx_dispose(&ctx);
    cq_mock_dispose(&m);
}
