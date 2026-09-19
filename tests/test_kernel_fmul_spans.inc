/* tests/test_kernel_fmul_spans.inc — the LAYOUT's four instruments: the
 * span-disjointness scan, the four products' offsets together with the 106-bit
 * assembly's lane map, the palindrome at an asymmetric mask, and two programs
 * in one region. Included by test_kernel_fmul.c AFTER
 * test_kernel_fmul_slots.inc, whose `fm_row_gates`, `fm_lane`, `fm_std_kinds`
 * and `FM_SCAN_A`/`FM_SCAN_B` it reuses.
 *
 * SPLIT OFF slots.inc ON A REAL SEAM, not on a line count. Everything next
 * door is about the SLOT SCHEDULE — how many slots a row has and which of
 * X / CX / CCX each emits. Everything here is about the LAYOUT — which bits
 * each row owns, and whether a row stays inside them. M31's measured finding
 * is that the two come apart: collapsing two blocks onto one offset kept the
 * value, the palindrome, the pool, every gate count AND the slot scan, so a
 * suite with only the schedule half has a hole exactly the shape of its
 * scratch map. K16 has 78 emitting rows against M31's five spans, and FOUR of
 * them are `cq_mul_block`s whose own module says a second block in the same
 * region is the only thing that can see a dropped `off`.
 *
 * NOTHING BELOW READS A GOLDEN.
 */

/* Room for two whole programs plus two rails and a destination. */
enum { FM_MAX_Q = 131072 };

/* ---- (c) Every row's span is disjoint from every other row's. ---------- */

CQ_TEST(the_rows_spans_are_pairwise_disjoint)
{
    static unsigned char std[FM_STD_CAP];
    static int owner[FM_MAX_Q];
    static size_t gates[CQ_FMUL_MAX_ROWS];
    const cq_fmul_row *rows;
    cq_mock m, m2;
    cq_ref_w q;
    size_t cursor = 0u;
    int n, bad = 0, seen_rows = 0, emitting = 0;
    long touched = 0;

    cq_mock_init(&m);
    cq_mock_init(&m2);
    rows = cq_fmul_rows(&n);
    q = cq_ref_w_ones(W64);

    CHECK(fm_row_gates(&m2, std, rows, n, gates));
    (void)fmul_run(FM_SCAN_A, FM_SCAN_B, q, q, &m);

    for (int t = 0; t < FM_MAX_Q; t++) owner[t] = -1;

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
            CHECK(tq < (uint32_t)FM_MAX_Q);
            touched++;
            if (owner[tq] >= 0 && owner[tq] != r) {
                cq_h_fail(__FILE__, __LINE__,
                          "row %d gate %zu: q%u is written by row %d AND by "
                          "row %d — two blocks of one program must not share "
                          "a bit", r, g, tq, owner[tq], r);
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
        CHECK_EQ(emitting, n - FM_SRC.view - FM_SRC.out);
        /* And the compute half is exactly what the scan walked, so the
         * copy-out and the reverse half are what is left. */
        CHECK_EQ((long long)cq_mock_count(&m),
                 (long long)(2u * cursor + (size_t)W64));
    }

    cq_mock_dispose(&m2);
    cq_mock_dispose(&m);
}

/* ---- (d) The four products, and the 106-bit assembly's lane map. ------- */

/* Trace operand `s`'s collapsed view chain at lane `L` back to the row it
 * actually reads. Returns 1 when the lane is LIVE and fills `*base` with the
 * first non-view row (or a negative operand code) and `*idx` with the index it
 * reads there. This is the file's own walk, not the library's, so the
 * addressing arithmetic is checked rather than reused. */
static int fm_trace(const cq_fmul_row *rows, int n, int s, int L,
                    int *base, int *idx)
{
    int guard = 0, shift = 0;
    uint64_t mask = ~UINT64_C(0);

    while (s >= 0 && s < n && rows[s].op == CQ_FMOP_VIEW) {
        uint64_t mk = rows[s].mask;

        if (shift >= 64 || shift <= -64) mk = 0u;
        else if (shift >= 0)             mk >>= (unsigned)shift;
        else                             mk <<= (unsigned)(-shift);
        mask  &= mk;
        shift += rows[s].shift;
        s      = rows[s].s0;
        if (++guard > n) { CHECK(0); return 0; }
    }
    if (((mask >> (unsigned)L) & 1u) == 0u) return 0;
    L += shift;
    if (L < 0 || L >= W64) return 0;
    *base = s;
    *idx  = L;
    return 1;
}

CQ_TEST(the_four_products_and_the_106_bit_lane_map)
{
    const cq_fmul_row *rows;
    uint32_t off[CQ_FMUL_MAX_ROWS];
    int mul[4], orr[8], n_mul = 0, n_or = 0, n, seam;
    int hi_base = -1, lo_base = -1, idx;

    rows = cq_fmul_rows(&n);
    seam = cq_fmul_seam_row();

    /* An INDEPENDENT prefix walk: this file's own, over the row table and the
     * modules' own region functions, never `cq_fm_arm`'s. */
    {
        uint32_t o = 0u;

        for (int i = 0; i < n; i++) {
            off[i] = o;
            o += fm_ref_row_region(&rows[i]);
            if (rows[i].op == CQ_FMOP_MUL) { CHECK(n_mul < 4); mul[n_mul++] = i; }
            if (rows[i].op == CQ_FMOP_OR && i >= seam && n_or < 8) orr[n_or++] = i;
        }
        CHECK_EQ(o, cq_fmul_region());
    }

    /* THE FOUR PRODUCTS ARE FOUR BLOCKS AT FOUR OFFSETS IN ONE REGION, which
     * K11.md §7.5 names as the shape a dropped `off` needs to be visible at
     * all: a lone block that lost its base slides wholesale, computes the
     * right value, mirrors perfectly and returns every qubit. */
    CHECK_EQ(n_mul, 4);
    for (int i = 0; i < 4; i++) {
        CHECK_EQ(fm_ref_row_region(&rows[mul[i]]), (uint32_t)cq_mul_region(W64));
        for (int j = i + 1; j < 4; j++) {
            uint32_t ai = off[mul[i]], aj = off[mul[j]];
            uint32_t w = (uint32_t)cq_mul_region(W64);

            if (ai < aj + w && aj < ai + w)
                cq_h_fail(__FILE__, __LINE__,
                          "products %d and %d overlap: [%u, %u) and [%u, %u)",
                          i, j, ai, ai + w, aj, aj + w);
        }
    }
    /* THEIR OPERANDS ARE DISJOINT BY RANGE TOO, which mul.h says the block
     * does NOT check: `a` and `b` bound to one span emits phase-P Toffolis
     * whose two controls are one physical qubit. Here they are views over two
     * different normalised significands, so the claim is that the two chains
     * reach DIFFERENT base rows. */
    for (int i = 0; i < 4; i++) {
        int ba = -1, bb = -1;

        CHECK(fm_trace(rows, n, rows[mul[i]].s0, 0, &ba, &idx));
        CHECK(fm_trace(rows, n, rows[mul[i]].s1, 0, &bb, &idx));
        if (ba == bb)
            cq_h_fail(__FILE__, __LINE__,
                      "product %d reads the same base row %d for both "
                      "operands — D7b at the kernel boundary", i, ba);
    }

    /* THE 106-BIT ASSEMBLY'S LANE MAP, CHECKED BIT BY BIT AGAINST THE SOURCE'S
     * OWN ARITHMETIC (fmul.jl:158-183). The product is (prod_hi : prod_lo) with
     * prod_hi holding bits [64, 105]. With the MSB at 105 the working value is
     * product bits [105:50] right-justified, so product bit N must land at lane
     * N-50; with the MSB at 104 it is bits [104:49] and bit N lands at N-49.
     * An off-by-one lane here is one binary exponent on roughly half of all
     * inputs and is invisible to L4, to the palindrome and to the slot scan. */
    CHECK(n_or >= 4);
    CHECK(fm_trace(rows, n, rows[orr[0]].s0, 14, &hi_base, &idx));
    CHECK_EQ(idx, 0);                       /* product bit 64 at lane 14   */
    CHECK_EQ(hi_base, seam - 1);            /* ... and it IS prod_hi_final */
    CHECK(fm_trace(rows, n, rows[orr[0]].s1, 0, &lo_base, &idx));
    CHECK_EQ(idx, 50);                      /* product bit 50 at lane 0    */
    CHECK(lo_base != hi_base);
    CHECK_EQ(rows[lo_base].op, CQ_FMOP_ADD);   /* prod_lo_final IS sum2    */

    for (int N = 50; N <= 105; N++) {
        int L = N - 50, b0 = -1, b1 = -1, i0 = -1, i1 = -1;
        int live0 = fm_trace(rows, n, rows[orr[0]].s0, L, &b0, &i0);
        int live1 = fm_trace(rows, n, rows[orr[0]].s1, L, &b1, &i1);

        if (N >= 64) {
            if (!live0 || b0 != hi_base || i0 != N - 64 || live1)
                cq_h_fail(__FILE__, __LINE__,
                          "wr_105 lane %d should be prod_hi[%d]: hi live=%d "
                          "base=%d idx=%d, lo live=%d", L, N - 64, live0, b0,
                          i0, live1);
        } else if (!live1 || b1 != lo_base || i1 != N || live0) {
            cq_h_fail(__FILE__, __LINE__,
                      "wr_105 lane %d should be prod_lo[%d]: lo live=%d "
                      "base=%d idx=%d, hi live=%d", L, N, live1, b1, i1, live0);
        }
    }
    /* Above lane 55 the 105-extraction is empty, which is what `wr & 56 bits`
     * at :191 then relies on. */
    for (int L = 56; L < W64; L++) {
        int b, i2;

        CHECK(!fm_trace(rows, n, rows[orr[0]].s0, L, &b, &i2));
        CHECK(!fm_trace(rows, n, rows[orr[0]].s1, L, &b, &i2));
    }

    for (int N = 49; N <= 105; N++) {
        int L = N - 49, b0 = -1, b1 = -1, i0 = -1, i1 = -1;
        int live0 = fm_trace(rows, n, rows[orr[2]].s0, L, &b0, &i0);
        int live1 = fm_trace(rows, n, rows[orr[2]].s1, L, &b1, &i1);

        if (N >= 64) {
            if (!live0 || b0 != hi_base || i0 != N - 64 || live1)
                cq_h_fail(__FILE__, __LINE__,
                          "wr_104 lane %d should be prod_hi[%d]: hi live=%d "
                          "base=%d idx=%d, lo live=%d", L, N - 64, live0, b0,
                          i0, live1);
        } else if (!live1 || b1 != lo_base || i1 != N || live0) {
            cq_h_fail(__FILE__, __LINE__,
                      "wr_104 lane %d should be prod_lo[%d]: lo live=%d "
                      "base=%d idx=%d, hi live=%d", L, N, live1, b1, i1, live0);
        }
    }

    /* AND THE TWO EXTRACTIONS REALLY ARE ONE LANE APART, which is the whole of
     * `msb_at_105`: a port that built both the same way would be right on half
     * its inputs and wrong on the other half, and the two anchors that see it
     * are 1.5*1.5 and 1.0*1.5. */
    {
        int b0 = -1, b1 = -1, i0 = -1, i1 = -1;

        CHECK(fm_trace(rows, n, rows[orr[0]].s0, 20, &b0, &i0));
        CHECK(fm_trace(rows, n, rows[orr[2]].s0, 20, &b1, &i1));
        CHECK_EQ(i0, i1 + 1);
    }
}

/* ---- (e) The palindrome, at an ASYMMETRIC mask. ------------------------ */

/* AN ALL-CLASSICAL MASK PAIR IS NOT A PALINDROME CASE, IT IS AN L5 CASE — the
 * R9 short-circuit never enters cq_sandwich, so cq_mock_count is 0 and the
 * check is being asked about a stream that does not exist. The mask that DOES
 * belong here is risk R8's named witness, `a` all Q and `b` all CQ_BIT_ZERO,
 * which is in no symmetric mask set; the mirror is here too because `fmul`'s
 * two operands are not interchangeable in the circuit — `a_lo * b_hi` and
 * `a_hi * b_lo` are two different blocks and the NaN row reads `a` first — so
 * a fold that fired only on the second operand would hide in one order. */
CQ_TEST(the_compute_half_is_a_palindrome_at_an_asymmetric_mask)
{
    cq_mock m;
    cq_ref_w all_q = cq_ref_w_ones(W64), none = cq_ref_w_zero();

    cq_mock_init(&m);

    for (int side = 0; side < 2; side++) {
        size_t total, head;
        uint64_t got, want;

        got = fmul_run(FM_SCAN_A, FM_SCAN_B, side == 0 ? all_q : none,
                       side == 0 ? none : all_q, &m);
        want = ref_fmul(FM_SCAN_A, FM_SCAN_B, W64);
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
 * `cq_fm_arm`, which is the ONE place `k->off` is added; drop it and the whole
 * program slides to the bottom of the caller's region. The kernel always
 * passes `off = 0`, so from every other case in this suite the mutation is
 * BEHAVIOURALLY INERT — the value, the palindrome, the pool, the slot scan,
 * the span-disjointness scan and the golden all stay green. Only a SECOND
 * program at a SECOND offset in one region sees it, because the two then
 * collide. M18, M31, M32 and M36 all carry the same case for the same reason,
 * and the death cases do not substitute for it: they drive the fit check,
 * which is a different line.
 *
 * AND IT IS THE PURE SLIDE THAT MATTERS (bd a-discarded-arm-and-an-output-
 * only-view-are-invisible-to-l1's rider): a mutant that keeps the region total
 * right so no guard fires is killed by this case and by nothing else. */
CQ_TEST(two_programs_in_one_region_do_not_collide)
{
    static int owner[FM_MAX_Q];
    static const uint64_t VA[] = { FM_GRS4_A, FM_TIEUP_A, CQ_F64_MIN_SUBNORMAL };
    static const uint64_t VB[] = { FM_GRS4_B, FM_TIEUP_B, FM_TWO };
    int nv = (int)(sizeof VA / sizeof VA[0]);
    cq_mock m;

    cq_mock_init(&m);

    for (int v = 0; v < nv; v++) {
        cq_fmul_block k[2];
        cq_scratch scr;
        cq_ctx ctx;
        cq_sink s = cq_mock_sink(&m);
        uint32_t reg = cq_fmul_region();
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
        /* The offsets are distinct and the second is not zero — which is what
         * makes the mutation representable at all. */
        CHECK_EQ(k[0].off, 0u);
        CHECK(k[1].off > 0u);

        for (int t = 0; t < FM_MAX_Q; t++) owner[t] = -1;
        cq_mock_reset(&m);

        for (int b = 0; b < 2 && !bad; b++) {
            size_t before = cq_mock_count(&m);

            for (int u = 0; u < cq_fmul_steps(); u++) cq_fmul_step(&ctx, &k[b], u);

            for (size_t g = before; g < cq_mock_count(&m); g++) {
                uint32_t tq = cq_mock_at(&m, g)->t;

                CHECK(tq < (uint32_t)FM_MAX_Q);
                if (owner[tq] >= 0 && owner[tq] != b) {
                    cq_h_fail(__FILE__, __LINE__,
                              "two fmul programs at off 0 and off %u: q%u is "
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
            const cq_bit *res = cq_fmul_result(&k[b]);
            uint64_t want = ref_fmul(VA[v], VB[v], W64), got = 0u;

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
                          "two programs on %016llx * %016llx: program %d's "
                          "result = %016llx, want %016llx",
                          (unsigned long long)VA[v], (unsigned long long)VB[v],
                          b, (unsigned long long)got,
                          (unsigned long long)want);
        }

        /* The reverse halves, in the mirror order, and then the pool claim —
         * every scratch bit provably back at |0> with two programs sharing one
         * region. */
        for (int b = 1; b >= 0; b--)
            for (int u = cq_fmul_steps() - 1; u >= 0; u--)
                cq_fmul_step(&ctx, &k[b], u);

        for (uint32_t i = 0; i < scr.n; i++) {
            if (!cq_bit_is_qubit(scr.bits[i])) continue;
            if (!cq_shadow_known_zero(&ctx.shadow, cq_bit_qindex(scr.bits[i])))
                cq_h_fail(__FILE__, __LINE__,
                          "two fmul programs: scratch bit %u (q%u) is not back "
                          "at |0> after both reverse halves", i,
                          cq_bit_qindex(scr.bits[i]));
            scr.bits[i] = cq_bit_zero();
        }

        cq_scratch_dispose(&scr);
        cq_ctx_dispose(&ctx);
    }

    cq_mock_dispose(&m);
}
