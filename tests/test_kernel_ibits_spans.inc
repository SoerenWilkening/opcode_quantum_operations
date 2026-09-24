/* tests/test_kernel_ibits_spans.inc — the LAYOUT's three instruments: the
 * span-disjointness scan, the palindrome at a mixed mask, and two programs in
 * one region. Included by test_kernel_ibits.c AFTER the slots .inc, whose
 * `sc_slot` and scan widths it reuses. NOTHING BELOW READS A GOLDEN.
 *
 * SPLIT ON A REAL SEAM: next door is the SCHEDULE (how many slots, which
 * kind each emits); here is the LAYOUT (which bits each row owns, and whether
 * a program stays inside them). M31 measured the two coming apart — two
 * blocks collapsed onto one offset kept the value, the palindrome, the pool,
 * every count and the slot scan (bd memories a-block-that-ignores-its-own-off-
 * needs-a-two-block-case).
 */

enum { IB_MAX_Q = 8192 };

/* ---- (c) No qubit is written by two different rows. -------------------- */

CQ_TEST(the_programs_spans_are_pairwise_disjoint)
{
    static int owner[IB_MAX_Q];
    cq_mock m;

    cq_mock_init(&m);
    for (int f = 0; f < CQ_IBF_N; f++) {
        const int *ws;
        int nw = scan_widths((cq_ibits_fn)f, &ws);

        for (int wi = 0; wi < nw; wi++) {
            int W = ws[wi], n, seen_rows = 0, bad = 0, dummy = 0;
            cq_ibits_row rows[CQ_IBITS_MAX_ROWS];
            size_t cursor = 0u;

            for (int t = 0; t < IB_MAX_Q; t++) owner[t] = -1;
            n = cq_ibits_program((cq_ibits_fn)f, W, rows);
            (void)ib_run((cq_ibits_fn)f, W, scan_value(W), cq_ref_w_ones(W), &m);

            for (int r = 0; r < n && !bad; r++) {
                int hit = 0;

                for (int s = 0; s < cq_ibits_row_steps(&rows[r], W); s++) {
                    uint32_t t;

                    if (sc_slot((cq_ibits_fn)f, W, r, rows[r].op, s, &dummy)
                        == IB_PRED_NONE) continue;
                    t = cq_mock_at(&m, cursor++)->t;
                    if (t >= (uint32_t)IB_MAX_Q) { CHECK(0); bad = 1; break; }
                    if (owner[t] >= 0 && owner[t] != r) {
                        cq_h_fail(__FILE__, __LINE__, "%s W=%d row %d slot %d: "
                                  "q%u is written by row %d AND row %d",
                                  SPEC[f].name, W, r, s, t, owner[t], r);
                        bad = 1;
                        break;
                    }
                    owner[t] = r;
                    hit = 1;
                }
                seen_rows += hit;
            }
            /* NOT VACUOUS: every row wrote something. */
            if (!bad) CHECK_EQ(seen_rows, n);
        }
    }
    cq_mock_dispose(&m);
}

/* ---- (d) The palindrome, at a MIXED mask. ------------------------------ */

/* An all-classical rail is an L5 case, not a palindrome case. A unary kernel
 * has no R8 operand pair, so the asymmetric masks here are WITHIN the one
 * rail: alternating lanes, and the low half only. The head length is derived
 * rather than predicted — the scan above would have to fold on the rail's
 * kinds too — and `total = 2 * head + W` with W CX copy-out gates is itself a
 * structural claim. */
CQ_TEST(the_compute_half_is_a_palindrome_at_a_mixed_mask)
{
    cq_mock m;

    cq_mock_init(&m);
    for (int f = 0; f < CQ_IBF_N; f++) {
        const int *ws;
        int nw = scan_widths((cq_ibits_fn)f, &ws);

        for (int wi = 0; wi < nw; wi++)
            for (int side = 0; side < 2; side++) {
                int W = ws[wi];
                cq_ref_w v = scan_value(W);
                cq_ref_w q = side == 0
                    ? cq_ref_w_make(0x5555555555555555ull, 0u, W)
                    : cq_ref_w_ones(W > 1 ? W / 2 : 1);
                size_t total, head;

                CHECK_EQ(ib_run((cq_ibits_fn)f, W, v, q, &m).lo,
                         ORACLE[f](v.lo, W));
                total = cq_mock_count(&m);
                CHECK(total > (size_t)W);
                CHECK_EQ((long long)((total - (size_t)W) % 2u), 0);
                head = (total - (size_t)W) / 2u;
                if (!cq_mock_is_palindrome(&m, head, (size_t)W))
                    cq_h_fail(__FILE__, __LINE__, "%s W=%d mask %d: the "
                              "%zu-gate compute half does not read back in "
                              "reverse", SPEC[f].name, W, side, head);
            }
    }
    cq_mock_dispose(&m);
}

/* ---- (e) Two programs in ONE region, at two offsets. ------------------- */

/* THE SOLE DETECTOR FOR A PROGRAM THAT IGNORES ITS OWN `off`: the kernel
 * always passes 0, so from every other case the mutation is inert. The whole
 * region is pre-materialised as cq_sandwich does under I6(b). */
static uint64_t ib_read(cq_ctx *ctx, const cq_bit *b, int W, int *known)
{
    uint64_t v = 0u;

    *known = 1;
    for (int j = 0; j < W; j++) {
        cq_shadow sv;

        if (cq_bit_is_const(b[j])) { v |= (uint64_t)cq_bit_value(b[j]) << j;
                                     continue; }
        sv = cq_shadow_get(&ctx->shadow, cq_bit_qindex(b[j]));
        if (sv.unknown) *known = 0;
        v |= (uint64_t)sv.value << j;
    }
    return v;
}

CQ_TEST(two_programs_in_one_region_do_not_collide)
{
    static const struct { cq_ibits_fn f0, f1; int W; } PAIRS[] = {
        { CQ_IBF_CTPOP, CQ_IBF_CTTZ, 8 },
        { CQ_IBF_CTLZ, CQ_IBF_BITREVERSE, 5 },
        { CQ_IBF_BSWAP, CQ_IBF_CTLZ, 16 }
    };
    static const uint64_t VALUES[] = { 0x5Au, 0x0u, 0x8001u };
    static int owner[IB_MAX_Q];
    cq_mock m;

    cq_mock_init(&m);
    for (size_t p = 0; p < sizeof PAIRS / sizeof PAIRS[0]; p++)
        for (size_t vi = 0; vi < sizeof VALUES / sizeof VALUES[0]; vi++) {
            int W = PAIRS[p].W, n[2], bad = 0;
            cq_ibits_fn fn[2] = { PAIRS[p].f0, PAIRS[p].f1 };
            cq_ibits_row rows[2][CQ_IBITS_MAX_ROWS];
            uint64_t va = VALUES[vi] & cq_ref_mask(W);
            cq_ibits_block k[2];
            cq_scratch scr;
            cq_ctx ctx;
            cq_sink s = cq_mock_sink(&m);
            uint32_t r0;
            int32_t ha;

            n[0] = cq_ibits_program(fn[0], W, rows[0]);
            n[1] = cq_ibits_program(fn[1], W, rows[1]);
            r0 = cq_ibits_region(rows[0], n[0], W);

            cq_ctx_init(&ctx, &s);
            ha = cq_bk_reg_w(&ctx, (uint32_t)W, cq_ref_w_make(va, 0u, W),
                             cq_ref_w_ones(W));
            cq_scratch_alloc(&scr, r0 + cq_ibits_region(rows[1], n[1], W));
            for (uint32_t i = 0; i < scr.n; i++)
                cq_materialise(&ctx, &scr.bits[i]);
            for (int b = 0; b < 2; b++) {
                k[b].a = cq_reg_cbits(&ctx.regs, ha);
                k[b].scr = &scr;
                k[b].off = (b == 0) ? 0u : r0;
                k[b].rows = rows[b];
                k[b].n_rows = n[b];
                k[b].W = W;
            }
            CHECK(k[1].off > 0u);

            for (int q = 0; q < IB_MAX_Q; q++) owner[q] = -1;
            cq_mock_reset(&m);
            for (int b = 0; b < 2 && !bad; b++) {
                size_t before = cq_mock_count(&m);

                for (int u = 0; u < cq_ibits_steps(rows[b], n[b], W); u++)
                    cq_ibits_step(&ctx, &k[b], u);
                for (size_t g = before; g < cq_mock_count(&m); g++) {
                    uint32_t t = cq_mock_at(&m, g)->t;

                    if (t >= (uint32_t)IB_MAX_Q) { CHECK(0); bad = 1; break; }
                    if (owner[t] >= 0 && owner[t] != b) {
                        cq_h_fail(__FILE__, __LINE__, "%s at 0 and %s at %u: "
                                  "q%u is written by BOTH",
                                  cq_ibits_name(fn[0]), cq_ibits_name(fn[1]),
                                  k[1].off, t);
                        bad = 1;
                        break;
                    }
                    owner[t] = b;
                }
            }

            /* Each program's result still holds ITS OWN answer. */
            for (int b = 0; b < 2 && !bad; b++) {
                int known = 0;
                uint64_t got = ib_read(&ctx, cq_ibits_result(&k[b]), W, &known);

                if (!known || got != ORACLE[fn[b]](va, W))
                    cq_h_fail(__FILE__, __LINE__, "two programs (%s, %s) W=%d "
                              "on 0x%llx: program %d = %s0x%llx, want 0x%llx",
                              cq_ibits_name(fn[0]), cq_ibits_name(fn[1]), W,
                              (unsigned long long)va, b, known ? "" : "unknown/",
                              (unsigned long long)got,
                              (unsigned long long)ORACLE[fn[b]](va, W));
            }

            /* The reverse halves in mirror order, then every scratch bit
             * provably back at |0>. */
            for (int b = 1; b >= 0; b--)
                for (int u = cq_ibits_steps(rows[b], n[b], W) - 1; u >= 0; u--)
                    cq_ibits_step(&ctx, &k[b], u);
            for (uint32_t i = 0; i < scr.n; i++) {
                if (cq_bit_is_qubit(scr.bits[i])
                    && !cq_shadow_known_zero(&ctx.shadow,
                                             cq_bit_qindex(scr.bits[i])))
                    cq_h_fail(__FILE__, __LINE__, "two programs (%s, %s): "
                              "scratch bit %u is not back at |0>",
                              cq_ibits_name(fn[0]), cq_ibits_name(fn[1]), i);
                scr.bits[i] = cq_bit_zero();
            }
            cq_scratch_dispose(&scr);
            cq_ctx_dispose(&ctx);
        }
    cq_mock_dispose(&m);
}
