/* tests/test_kernel_iminmax_spans.inc — the LAYOUT's three instruments: the
 * span-disjointness scan, the palindrome at an asymmetric mask, and two blocks
 * in one region. Included by test_kernel_iminmax.c AFTER the slots .inc, whose
 * row table, schedule and `im_run` it reuses. NOTHING BELOW READS A GOLDEN.
 *
 * SPLIT ON THE SEAM M36 RECORDED: the slots file is the SCHEDULE (how many
 * slots, which gate kind each emits); this one is the LAYOUT (which bits each
 * row owns). Collapsing two rows onto one offset keeps the schedule, so a
 * suite with only the schedule half has a hole exactly the shape of its
 * scratch map (bd `assert-is-a-third-abort-source-and-a-slot-scan-does-not-see-the-layout`).
 */

enum { IM_MAX_Q = 4096 };

/* ---- (c) No bit is written by two different rows. ---------------------- */

CQ_TEST(the_rows_spans_are_pairwise_disjoint)
{
    static int owner[IM_MAX_Q];
    cq_mock m;

    cq_mock_init(&m);
    for (int op = 0; op < CQ_IMM_N_OP; op++)
        for (int w = 0; w < IM_NWS; w++) {
            im_row r[IM_MAX_ROWS];
            int n = im_rows(op, r), W = IM_WS[w], bad = 0, seen = 0;
            size_t cursor = 0u;
            cq_ref_w q = cq_ref_w_ones(W);

            for (int t = 0; t < IM_MAX_Q; t++) owner[t] = -1;
            (void)im_run(op, W, IM_SCAN_A, IM_SCAN_B, q, q, &m);

            for (int i = 0; i < n && !bad; i++) {
                int hit = 0;

                for (int v = 0; v < im_row_steps(&r[i], W) && !bad; v++) {
                    uint32_t t;

                    if (im_sched(&r[i], W, v) == IM_NONE) continue;
                    t = cq_mock_at(&m, cursor++)->t;
                    CHECK(t < (uint32_t)IM_MAX_Q);
                    hit = 1;
                    if (owner[t] >= 0 && owner[t] != i) {
                        cq_h_fail(__FILE__, __LINE__, "%s W=%d: q%u is written "
                                  "by row %d AND row %d", IM_SPECS[op]->name,
                                  W, t, owner[t], i);
                        bad = 1;
                    }
                    owner[t] = i;
                }
                seen += hit;
            }
            /* NOT VACUOUS: every row wrote something. */
            CHECK_EQ(seen, n);
        }
    cq_mock_dispose(&m);
}

/* ---- (d) The palindrome at R8's asymmetric masks. ----------------------- */

/* `a` all Q with `b` all CQ_BIT_ZERO, and the mirror — the pair no symmetric
 * mask set contains, and the one where a fold that fired on only one operand
 * would hide in the other order. The head length is DERIVED: the total is
 * 2*head + W with the W copy-out CNOTs in the middle. abs has one operand, so
 * its two rows are `a` all-Q and `a` with only the sign lane Q. */
CQ_TEST(the_compute_half_is_a_palindrome_at_an_asymmetric_mask)
{
    static const int WS[] = { 1, 2, 3, 5, 8, 16 };
    cq_mock m;

    cq_mock_init(&m);
    for (int op = 0; op < CQ_IMM_N_OP; op++)
        for (size_t w = 0; w < sizeof WS / sizeof WS[0]; w++)
            for (int side = 0; side < 2; side++) {
                int W = WS[w];
                cq_ref_w all = cq_ref_w_ones(W), none = cq_ref_w_zero();
                cq_ref_w qa = side == 0 ? all : none, qb = side == 0 ? none : all;
                size_t total, head;

                if (op == CQ_IMM_ABS) {
                    qa = side == 0 ? all : cq_ref_w_setbit(W - 1);
                    qb = none;
                }
                CHECK_EQ(im_run(op, W, IM_SCAN_A, IM_SCAN_B, qa, qb, &m),
                         im_ref(op, IM_SCAN_A, IM_SCAN_B, W));
                total = cq_mock_count(&m);
                CHECK(total > (size_t)W);             /* it really sandwiched */
                CHECK_EQ((long long)((total - (size_t)W) % 2u), 0);
                head = (total - (size_t)W) / 2u;
                if (!cq_mock_is_palindrome(&m, head, (size_t)W))
                    cq_h_fail(__FILE__, __LINE__, "%s W=%d side %d: the "
                              "%zu-gate compute half does not read back in "
                              "reverse", IM_SPECS[op]->name, W, side, head);
            }
    cq_mock_dispose(&m);
}

/* ---- (e) Two blocks in ONE region, at two offsets. ---------------------- */

/* THE SOLE DETECTOR FOR A BLOCK THAT IGNORES ITS OWN `off` (bd
 * `a-block-that-ignores-its-own-off-needs-a-two-block-case`): the kernel always
 * passes off = 0, so dropping it is inert from every kernel-level case. Two
 * blocks in one pre-materialised region must write disjoint bits, each must
 * hold ITS OWN answer after the other ran, and both reverse halves must leave
 * every scratch bit provably |0>. */
static uint64_t im_out_value(cq_ctx *ctx, const cq_bit *out, int W, int *known)
{
    uint64_t v = 0u;

    *known = 1;
    for (int i = 0; i < W; i++) {
        cq_shadow sv = cq_shadow_get(&ctx->shadow, cq_bit_qindex(out[i]));

        if (!cq_bit_is_qubit(out[i]) || sv.unknown) *known = 0;
        v |= (uint64_t)sv.value << i;
    }
    return v;
}

CQ_TEST(two_blocks_in_one_region_do_not_collide)
{
    static const int PAIRS[][2] = {
        { CQ_IMM_UMAX, CQ_IMM_ABS  }, { CQ_IMM_ABS,  CQ_IMM_SMIN },
        { CQ_IMM_SMAX, CQ_IMM_UMIN }, { CQ_IMM_UMIN, CQ_IMM_UMAX }
    };
    static const int WS[] = { 1, 3, 8 };
    static int owner[IM_MAX_Q];
    cq_mock m;

    cq_mock_init(&m);
    for (size_t p = 0; p < sizeof PAIRS / sizeof PAIRS[0]; p++)
        for (size_t w = 0; w < sizeof WS / sizeof WS[0]; w++) {
            int W = WS[w], bad = 0;
            cq_ctx ctx;
            cq_sink s = cq_mock_sink(&m);
            cq_scratch scr;
            cq_imm_block k[2];
            uint32_t r0 = cq_imm_region(PAIRS[p][0], W);
            int32_t ha, hb;

            cq_ctx_init(&ctx, &s);
            ha = cq_bk_reg_w(&ctx, (uint32_t)W, cq_ref_w_make(IM_SCAN_A, 0u, W),
                             cq_ref_w_ones(W));
            hb = cq_bk_reg_w(&ctx, (uint32_t)W, cq_ref_w_make(IM_SCAN_B, 0u, W),
                             cq_ref_w_ones(W));
            cq_scratch_alloc(&scr, r0 + cq_imm_region(PAIRS[p][1], W));
            for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&ctx, &scr.bits[i]);

            for (int b = 0; b < 2; b++) {
                k[b].a = cq_reg_cbits(&ctx.regs, ha);
                k[b].b = cq_reg_cbits(&ctx.regs, hb);
                k[b].scr = &scr;
                k[b].off = b == 0 ? 0u : r0;
                k[b].op = PAIRS[p][b];
                k[b].W = W;
            }
            CHECK(k[1].off > 0u);

            for (int q = 0; q < IM_MAX_Q; q++) owner[q] = -1;
            cq_mock_reset(&m);
            for (int b = 0; b < 2 && !bad; b++) {
                size_t before = cq_mock_count(&m);

                for (int u = 0; u < cq_imm_steps(k[b].op, W); u++)
                    cq_imm_step(&ctx, &k[b], u);
                for (size_t g = before; g < cq_mock_count(&m) && !bad; g++) {
                    uint32_t t = cq_mock_at(&m, g)->t;

                    CHECK(t < (uint32_t)IM_MAX_Q);
                    if (owner[t] >= 0 && owner[t] != b) {
                        cq_h_fail(__FILE__, __LINE__, "%s at 0 and %s at %u, "
                                  "W=%d: q%u is written by both",
                                  IM_SPECS[k[0].op]->name,
                                  IM_SPECS[k[1].op]->name, k[1].off, W, t);
                        bad = 1;
                    }
                    owner[t] = b;
                }
            }
            for (int b = 0; b < 2 && !bad; b++) {
                int known;
                uint64_t got = im_out_value(&ctx, cq_imm_out(&k[b]), W, &known);

                CHECK(known);
                CHECK_EQ(got, im_ref(k[b].op, IM_SCAN_A, IM_SCAN_B, W));
            }
            for (int b = 1; b >= 0; b--)
                for (int u = cq_imm_steps(k[b].op, W) - 1; u >= 0; u--)
                    cq_imm_step(&ctx, &k[b], u);
            for (uint32_t i = 0; i < scr.n; i++) {
                if (!cq_shadow_known_zero(&ctx.shadow, cq_bit_qindex(scr.bits[i])))
                    cq_h_fail(__FILE__, __LINE__, "%s + %s W=%d: scratch bit %u "
                              "is not |0> after both reverse halves",
                              IM_SPECS[k[0].op]->name, IM_SPECS[k[1].op]->name,
                              W, i);
                scr.bits[i] = cq_bit_zero();
            }
            cq_scratch_dispose(&scr);
            cq_ctx_dispose(&ctx);
        }
    cq_mock_dispose(&m);
}
