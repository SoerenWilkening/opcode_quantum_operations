/* tests/test_kernel_fround_spans.inc — the LAYOUT's three instruments: the
 * span-disjointness scan, the palindrome at an asymmetric mask, and two
 * programs in one region. Included by test_kernel_fround.c AFTER
 * test_kernel_fround_scan.inc, whose `fr_row_gates` and `fr_run` it reuses.
 * NOTHING BELOW READS A GOLDEN.
 *
 * SPLIT OFF THE SCHEDULE HALF ON A REAL SEAM: next door is "which of X / CX /
 * CCX does each slot emit"; here is "which BITS each row owns". M31 measured
 * that the two come apart — two blocks collapsed onto one offset kept the
 * value, the palindrome, the pool, every count and the slot scan.
 */

enum { FR_MAX_Q = 131072 };

/* A 64-lane span's VALUE, through the shadow for a qubit lane and the kind for
 * a constant one. `unknown` is a failure: on the rotation-free surface the
 * shadow is EXACT. */
static uint64_t fr_read(cq_ctx *ctx, const cq_bit *v)
{
    uint64_t got = 0u;

    for (int i = 0; i < W64; i++) {
        cq_shadow sv;

        if (cq_bit_is_const(v[i])) {
            if (cq_bit_value(v[i])) got |= UINT64_C(1) << (unsigned)i;
            continue;
        }
        sv = cq_shadow_get(&ctx->shadow, cq_bit_qindex(v[i]));
        if (sv.unknown)
            cq_h_fail(__FILE__, __LINE__, "lane %d's shadow is UNKNOWN", i);
        if (sv.value) got |= UINT64_C(1) << (unsigned)i;
    }
    return got;
}

/* ---- (a) Every row's span is disjoint from every other row's. ---------- */

static void fr_spans_one(cq_fround_prog p)
{
    static unsigned char std[FR_STD_CAP];
    static int owner[FR_MAX_Q];
    size_t gates[CQ_FROUND_MAX_ROWS];
    const cq_fround_row *rows;
    cq_mock m, m2;
    size_t cursor = 0u;
    int n, bad = 0, emitting = 0;

    cq_mock_init(&m);
    cq_mock_init(&m2);
    rows = cq_fround_rows(p, &n);
    fr_row_gates(&m2, std, rows, n, gates);
    (void)fr_run(p, FR_SCAN_A[p], cq_ref_w_ones(W64), &m);

    for (int t = 0; t < FR_MAX_Q; t++) owner[t] = -1;
    for (int r = 0; r < n && !bad; r++) {
        if (gates[r] == 0u) continue;
        emitting++;
        for (size_t g = 0; g < gates[r] && !bad; g++) {
            uint32_t tq;

            if (cursor >= cq_mock_count(&m)) {
                cq_h_fail(__FILE__, __LINE__, "%s row %d: the stream ran out",
                          FR_SPECS[p]->name, r);
                bad = 1;
                break;
            }
            tq = cq_mock_at(&m, cursor++)->t;
            CHECK(tq < (uint32_t)FR_MAX_Q);
            if (tq >= (uint32_t)FR_MAX_Q) { bad = 1; break; }
            if (owner[tq] >= 0 && owner[tq] != r) {
                cq_h_fail(__FILE__, __LINE__, "%s row %d gate %zu: q%u is "
                          "written by row %d AND by row %d", FR_SPECS[p]->name,
                          r, g, tq, owner[tq], r);
                bad = 1;
                break;
            }
            owner[tq] = r;
        }
    }
    if (!bad) {
        CHECK(emitting > 10);
        CHECK_EQ((long long)cq_mock_count(&m),
                 (long long)(2u * cursor + (size_t)W64));
    }
    cq_mock_dispose(&m2);
    cq_mock_dispose(&m);
}

CQ_TEST(the_rows_spans_are_pairwise_disjoint)
{
    for (int p = 0; p < CQ_FROUND_N_PROG; p++) fr_spans_one((cq_fround_prog)p);
}

/* ---- (b) The palindrome, at an ASYMMETRIC mask. ------------------------ */

/* UNARY, so the asymmetry has to be WITHIN the one rail: the low 32 lanes
 * quantum and the top 32 (the whole exponent and sign) classical, and the
 * mirror. The first runs the special/small/integer predicates over constants
 * while the fraction is a wire; the second makes the exponent a wire. */
CQ_TEST(the_compute_half_is_a_palindrome_at_an_asymmetric_mask)
{
    static const uint64_t MASKS[] = { 0x00000000ffffffffull,
                                      0xffffffff00000000ull };
    cq_mock m;

    cq_mock_init(&m);
    for (int p = 0; p < CQ_FROUND_N_PROG; p++)
        for (int side = 0; side < 2; side++) {
            uint64_t a = (p == CQ_FROUND_TRUNC) ? FR_0P3 : FR_SCAN_A[p];
            size_t total, head;

            CHECK_EQ(fr_run((cq_fround_prog)p, a,
                            cq_ref_w_make(MASKS[side], 0u, W64), &m),
                     ref_fround((cq_fround_prog)p, a));
            total = cq_mock_count(&m);
            CHECK(total > (size_t)W64);
            CHECK_EQ((long long)(total % 2u), 0);
            head = (total - (size_t)W64) / 2u;
            if (!cq_mock_is_palindrome(&m, head, (size_t)W64))
                cq_h_fail(__FILE__, __LINE__, "%s mask %016llx: the %zu-gate "
                          "compute half does not read back in reverse",
                          FR_SPECS[p]->name, (unsigned long long)MASKS[side],
                          head);
        }
    cq_mock_dispose(&m);
}

/* ---- (c) Two programs in ONE region, at two offsets. ------------------- */

/* THE SOLE DETECTOR FOR A PROGRAM THAT IGNORES ITS OWN `off`. Every span goes
 * through `cq_fi_sp`, the ONE place `k->off` is added; drop it and the program
 * slides to the bottom of the caller's region, and from every kernel-level
 * case the mutation is behaviourally inert. Each program runs at off 0 with
 * its NEIGHBOUR at a second offset above it, so every program is also driven
 * at a non-zero offset and two DIFFERENT programs share one region. */
CQ_TEST(two_programs_in_one_region_do_not_collide)
{
    static int owner[2 * FR_MAX_Q];
    cq_mock m;

    cq_mock_init(&m);
    for (int p = 0; p < CQ_FROUND_N_PROG; p++) {
        cq_fround_block k[2];
        cq_scratch scr;
        cq_ctx ctx;
        cq_sink s = cq_mock_sink(&m);
        uint64_t va = FR_SCAN_A[p];
        int32_t ha;
        int bad = 0;

        cq_mock_reset(&m);
        cq_ctx_init(&ctx, &s);
        ha = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(va, 0u, W64),
                         cq_ref_w_ones(W64));
        k[0].prog = (cq_fround_prog)p;
        k[1].prog = (cq_fround_prog)((p + 1) % CQ_FROUND_N_PROG);
        cq_scratch_alloc(&scr, cq_fround_region(k[0].prog)
                               + cq_fround_region(k[1].prog));
        for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&ctx, &scr.bits[i]);
        for (int b = 0; b < 2; b++) {
            k[b].a = cq_reg_cbits(&ctx.regs, ha);
            k[b].scr = &scr;
        }
        k[0].off = 0u;
        k[1].off = cq_fround_region(k[0].prog);

        for (int t = 0; t < 2 * FR_MAX_Q; t++) owner[t] = -1;
        cq_mock_reset(&m);
        for (int b = 0; b < 2 && !bad; b++) {
            size_t before = cq_mock_count(&m);

            for (int u = 0; u < cq_fround_steps(k[b].prog); u++)
                cq_fround_step(&ctx, &k[b], u);
            for (size_t g = before; g < cq_mock_count(&m); g++) {
                uint32_t tq = cq_mock_at(&m, g)->t;

                CHECK(tq < (uint32_t)(2 * FR_MAX_Q));
                if (tq >= (uint32_t)(2 * FR_MAX_Q)) { bad = 1; break; }
                if (owner[tq] >= 0 && owner[tq] != b) {
                    cq_h_fail(__FILE__, __LINE__, "%s at off 0 and %s at off "
                              "%u: q%u is written by BOTH", FR_SPECS[p]->name,
                              FR_SPECS[k[1].prog]->name, k[1].off, tq);
                    bad = 1;
                    break;
                }
                owner[tq] = b;
            }
        }
        /* AND THE VALUES: each program's result holds its own answer after
         * the other has run over the same region. */
        for (int b = 0; b < 2 && !bad; b++)
            CHECK_EQ(fr_read(&ctx, cq_fround_result(&k[b])),
                     ref_fround(k[b].prog, va));

        for (int b = 1; b >= 0; b--)
            for (int u = cq_fround_steps(k[b].prog) - 1; u >= 0; u--)
                cq_fround_step(&ctx, &k[b], u);
        for (uint32_t i = 0; i < scr.n; i++) {
            if (!cq_bit_is_qubit(scr.bits[i])) continue;
            if (!cq_shadow_known_zero(&ctx.shadow, cq_bit_qindex(scr.bits[i])))
                cq_h_fail(__FILE__, __LINE__, "%s + %s: scratch bit %u is not "
                          "back at |0> after both reverse halves",
                          FR_SPECS[p]->name, FR_SPECS[k[1].prog]->name, i);
            scr.bits[i] = cq_bit_zero();
        }
        cq_scratch_dispose(&scr);
        cq_ctx_dispose(&ctx);
    }
    cq_mock_dispose(&m);
}
