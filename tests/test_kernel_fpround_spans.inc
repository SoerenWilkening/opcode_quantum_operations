/* tests/test_kernel_fpround_spans.inc — the LAYOUT half: the span-disjointness
 * scan, the palindrome with L2 as a set at three points, and two blocks at two
 * offsets in one region with a canary below them. Included by
 * test_kernel_fpround.c. NOTHING BELOW READS A GOLDEN.
 *
 * OFF AND BASE ARE INDEPENDENT HAZARDS WITH ONE DETECTOR EACH (bd
 * a-block-that-ignores-its-own-off-needs-a-two-block-case, measured twice —
 * M31 and M36). The span scan walks ONE program and asks that no qubit is
 * written by two different ROWS, which stays true when the whole program
 * SLIDES; the two-block case asks that two programs at two offsets do not
 * collide, which stays true when two rows of one program are collapsed onto
 * one offset. Neither substitutes for the other and the death cases substitute
 * for neither: a region check ("does this block fit here") is a different line
 * from the offset arithmetic.
 *
 * M32 HAS 113 EMITTING ROWS WITH THEIR OWN EXTENTS, against M31's five spans,
 * and the same block type appears up to FIFTEEN times — so an off-by-one in
 * the prefix sum lands inside a DIFFERENT INSTANCE OF THE SAME BLOCK, where
 * the gates are plausible, the palindrome survives and only the value moves.
 * That is K15's risk 1 at 154 boundaries.
 */

/* ---- The span-disjointness scan. ---------------------------------------- */

/* STRONGER THAN "the spans do not overlap", which is arithmetic on the prefix
 * sum and would agree with itself. This walks the RECORDED STREAM: every gate
 * TARGET is mapped back to its position in the region, and that position must
 * lie inside the extent of the row whose slot emitted it. A row writing into a
 * neighbour's extent is then named, in both directions. */
CQ_TEST(every_span_of_every_row_is_pairwise_disjoint)
{
    cq_mock m;
    static const int Q[3] = { 1, 1, 1 };

    cq_mock_init(&m);

    for (int r = 0; r < N_ROWS; r++) {
        cq_fpround_id id = ROWS[r].id;
        cq_sink s = cq_mock_sink(&m);
        uint64_t v[3];
        int n = 0;
        const cq_fpround_row *rows = cq_fpround_rows(id, &n);
        uint32_t *pos;              /* qubit index -> position in `work`      */
        uint32_t lo[CQ_FPROUND_MAX_ROWS], hi[CQ_FPROUND_MAX_ROWS], o = 0u;
        size_t cursor = 0u;
        int bad = 0;

        (void)fpr_anchor_at(id, 0, v);
        fpr_open(&FX, id, v, Q, 0u, &s);

        /* The extents, re-derived here from each block's published region
         * function rather than read out of fpround_step.c's walk. */
        for (int i = 0; i < n; i++) {
            uint32_t w = 0u;

            switch (rows[i].op) {
            case CQ_FROP_VIEW:  w = 0u; break;
            case CQ_FROP_EQ:    w = (uint32_t)cq_eq_region(W64);  break;
            case CQ_FROP_ULT:   w = (uint32_t)cq_ult_region(W64); break;
            case CQ_FROP_SLT:   w = (uint32_t)cq_slt_region(W64); break;
            case CQ_FROP_SUB:   w = (uint32_t)cq_sub_region(W64); break;
            case CQ_FROP_ADD:   w = (uint32_t)cq_add_region(W64); break;
            case CQ_FROP_MUX:   w = (uint32_t)cq_mux_region(W64); break;
            case CQ_FROP_AND: case CQ_FROP_OR: w = (uint32_t)W64; break;
            case CQ_FROP_BSHL: case CQ_FROP_BLSHR:
                w = (uint32_t)cq_barrel_region(W64); break;
            default: w = 1u; break;
            }
            lo[i] = o; hi[i] = o + w; o += w;
        }
        CHECK_EQ((long long)o, (long long)fpr_region_of(id));

        pos = (uint32_t *)calloc(cq_qubits_minted(&FX.ctx.pool) + 1u,
                                 sizeof *pos);
        CHECK(pos != NULL);
        if (pos == NULL) { fpr_close(&FX); continue; }
        for (uint32_t i = 0; i < FX.work.n; i++)
            pos[cq_bit_qindex(FX.work.bits[i])] = i + 1u;   /* 1-based; 0 = not ours */

        cq_mock_reset(&m);
        fpr_forward(&FX);

        for (int i = 0; i < n && !bad; i++) {
            int slots = fpr_row_slots(rows[i].op);

            for (int u = 0; u < slots && !bad; u++) {
                uint32_t p;

                if (fpr_scan_slot_quiet(rows, n, i, u) == FPR_NONE) continue;
                if (cursor >= cq_mock_count(&m)) { bad = 1; break; }
                p = pos[cq_mock_at(&m, cursor++)->t];
                if (p == 0u || p - 1u < lo[i] || p - 1u >= hi[i]) {
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d (op %d) slot %d writes region bit %d, "
                              "outside its own extent [%u, %u)", ROWS[r].name,
                              i, (int)rows[i].op, u, p ? (int)(p - 1u) : -1,
                              lo[i], hi[i]);
                    bad = 1;
                }
            }
        }
        free(pos);
        fpr_close(&FX);
    }

    cq_mock_dispose(&m);
}

/* ---- The palindrome, and the pool at three points. ---------------------- */

/* DRIVEN THE WAY A CONSUMER DRIVES IT — a contiguous run of step indices — and
 * NOT through cq_sandwich, which would release the region under its own
 * literal CQ_ZERO_BY_PALINDROME and launder exactly the fault this case exists
 * to see (bd step-block-fixture-discipline item 4).
 *
 * TWO MASK ROWS, AND THE ASYMMETRIC ONE IS THE POINT. A block has no R9
 * short-circuit, so even the all-classical row enters the stream — but at the
 * asymmetric row the fold table elides a DIFFERENT set of gates in the forward
 * half than a symmetric mask would, and the reverse half has to cancel against
 * that stream rather than against the all-quantum one. There is NO middle
 * section: M32 ships no kernel and therefore no copy-out, which is why
 * `n_mid` is 0 here and 1 or W everywhere else in this repo.
 *
 * THE INPUTS ARE |0>, WHICH IS ADD-BLOCK'S OWN SHAPE AND IS NOT A WEAKENING.
 * The fold table reads a bit's KIND and never a qubit's VALUE (D6, no
 * demotion), so a region of |0> qubits emits and cancels exactly what a live
 * one would — and this case has to RECLAIM the input region as well as the
 * scratch, which is only meaningful when the inputs are themselves back at
 * |0>. `l1_against_the_ieee_reference_model` next door runs the same blocks
 * over the anchors and a seeded draw, and `the_blocks_output_span` is checked
 * there; what is asserted HERE is the stream and the pool. */
CQ_TEST(the_blocks_are_palindromes_and_give_the_pool_back)
{
    static const int AQ[2][3] = { { 1, 1, 1 }, { 1, 0, 0 } };
    static const uint64_t ZERO3[3] = { 0u, 0u, 0u };
    cq_mock m;

    cq_mock_init(&m);

    for (int r = 0; r < N_ROWS; r++)
        for (int p = 0; p < 2; p++) {
            cq_fpround_id id = ROWS[r].id;
            cq_sink s = cq_mock_sink(&m);
            size_t head;
            cq_pc_snap after;

            fpr_open(&FX, id, ZERO3, AQ[p], 0u, &s);
            fpr_live_is_exactly(&FX, FX.n_named, "after the MINT");

            cq_mock_reset(&m);
            fpr_forward(&FX);
            head = cq_mock_count(&m);
            CHECK(head > 0u);

            /* Plan §0.4 obligation 2: the block allocates nothing. */
            fpr_live_is_exactly(&FX, FX.n_named, "after the FORWARD");

            /* SLOTS AND GATES COME APART, ASSERTED RATHER THAN NARRATED: the
             * emitted head is strictly BELOW the slot count, because 41 view
             * rows and every constant span fold. An implementation that had
             * materialised its constants — upstream's `resolve!` shape,
             * PRD-v2 §7.3's refused alternative — would make this an
             * equality. */
            CHECK(head < (size_t)fpr_steps_of(id));

            fpr_reverse(&FX);
            CHECK(cq_mock_is_palindrome(&m, head, 0u));
            fpr_live_is_exactly(&FX, FX.n_named, "after the UNCOMPUTE");

            fpr_reclaim(&FX, &FX.work, "scratch");
            fpr_reclaim(&FX, &FX.ops,  "inputs");

            /* L3 AGAINST THE PRISTINE POOL, WHICH IS THE ONE SNAPSHOT THAT CAN
             * RETURN — a snapshot taken after the mint counts the fixture's own
             * inputs, and reclaiming them is the thing being asserted.
             * `minted`, `peak` and the free-list length are never compared:
             * all three are monotone (src/qubits.h). The INDEX SET is the
             * sharp half — a release of the WRONG index restores `live`
             * exactly and no count can see it. */
            after = cq_pc_take(&FX.ctx);
            if (!cq_pc_same(FX.pristine, after))
                cq_h_fail(__FILE__, __LINE__,
                          "L3 %s mask %d: pool not restored — live %u -> %u",
                          ROWS[r].name, p, FX.pristine.live, after.live);
            if (!cq_pc_indices_are_free(&FX.ctx, FX.named, FX.n_named))
                cq_h_fail(__FILE__, __LINE__, "L3 %s mask %d: see above",
                          ROWS[r].name, p);

            cq_ctx_dispose(&FX.ctx);
        }

    cq_mock_dispose(&m);
}

/* ---- Two blocks at two offsets in one region, over a canary. ------------ */

/* A BLOCK THAT IGNORES ITS OWN `off` IS BEHAVIOURALLY INERT FROM EVERY OTHER
 * CASE IN THIS FILE, and a lone non-zero offset proves nothing: an
 * off-ignoring block runs in the bottom of a merely-larger region — in bounds,
 * self-consistent, right answer, because the ACCESSORS lose the offset with it
 * (bd sub-region-offset-needs-a-canary; bd
 * a-step-block-cannot-see-its-own-offset-and-inherits-the-kernels-w1-hole).
 *
 * TWO DETECTORS HERE, AND THEY FAIL DIFFERENTLY. (1) The CANARY: 8 bits below
 * `off`, pre-materialised and flipped to |1>, so a slid block runs its
 * construction over scratch that does not start at |0> and gets a wrong VALUE.
 * Its own X gates are emitted BEFORE the mock reset and undone AFTER the
 * palindrome check, so the recorded stream is the block's alone. (2) TWO
 * BLOCKS, at `pad` and `pad + region`, with DIFFERENT inputs — so a collapse
 * onto one offset is a wrong answer rather than a coincidence, and both
 * reverse halves have to return every bit of the shared region. */
static void fpr_two_blocks(cq_fpround_id id, cq_mock *m, const char *name)
{
    static const int Q[3] = { 1, 1, 1 };
    enum { PAD = 8 };
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_scratch ops, work;
    cq_bit *in0[3], *in1[3], buf[CQ_FP64_W];
    uint32_t reg = fpr_region_of(id);
    uint64_t v0[3], v1[3], got0, got1, e0, e1;
    fpr_exp w0, w1;
    size_t head;
    int pick, ok0 = 0, ok1 = 0, oe0 = 0, oe1 = 0;

    /* THE SECOND TUPLE IS CHOSEN, NOT INDEXED, and that is a correction rather
     * than a flourish. A collapse onto one offset must be a WRONG ANSWER and
     * not a coincidence — and several anchors of a block legitimately share an
     * output: every single-bit `m` normalises to `1 << 52`, `_sf_normalize_clz`
     * sends every single-bit `wr` to `1 << 55`, `_sf_handle_subnormal`'s first
     * two both pass `wr` through untouched, and `_sf_round_and_pack`'s first
     * three all pack to 1.0. Indexing anchor 1 gave `clz` and `subnorm` a pair
     * whose first output is IDENTICAL, so their half of this case was asserting
     * a coincidence. The walk below takes the first anchor whose first TWO
     * outputs BOTH differ from anchor 0's, and refuses if there is none. */
    (void)fpr_anchor_at(id, 0, v0);
    w0 = fpr_ref(id, v0);
    pick = 0;
    for (int i = 1; i < fpr_anchor_count(id); i++) {
        (void)fpr_anchor_at(id, i, v1);
        w1 = fpr_ref(id, v1);
        if (w1.o[0] != w0.o[0] && w1.o[1] != w0.o[1]) { pick = i; break; }
    }
    if (pick == 0) {
        cq_h_fail(__FILE__, __LINE__,
                  "two %s blocks: no anchor distinguishes itself from anchor 0 "
                  "in BOTH of its first two outputs, so this case would assert "
                  "a coincidence", name);
        return;
    }

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);
    cq_scratch_alloc(&ops,  (uint32_t)(2 * N_IN * W64));
    cq_scratch_alloc(&work, (uint32_t)PAD + 2u * reg);

    for (int i = 0; i < N_IN; i++) {
        in0[i] = cq_scratch_span(&ops, (uint32_t)(i * W64), (uint32_t)W64);
        in1[i] = cq_scratch_span(&ops, (uint32_t)((N_IN + i) * W64), (uint32_t)W64);
        fpr_mint(&ctx, in0[i], v0[i], Q[i]);
        fpr_mint(&ctx, in1[i], v1[i], Q[i]);
    }
    for (uint32_t i = 0; i < work.n; i++) cq_materialise(&ctx, &work.bits[i]);

    /* The canary, before the reset so its gates are not in the recorded head. */
    for (int i = 0; i < PAD; i++) cq_emit_x(&ctx, &work.bits[i]);

    {
        /* Both blocks are built here rather than through FX, because the
         * fixture holds one `work` and one input triple and this case is
         * about two of each sharing one region. */
        cq_subnorm_block k0 = { in0[0], in0[1], in0[2], &work, (uint32_t)PAD };
        cq_subnorm_block k1 = { in1[0], in1[1], in1[2], &work,
                                (uint32_t)PAD + reg };
        cq_round_block   r0 = { in0[0], in0[1], in0[2], &work, (uint32_t)PAD };
        cq_round_block   r1 = { in1[0], in1[1], in1[2], &work,
                                (uint32_t)PAD + reg };
        cq_norm52_block  n0 = { in0[0], in0[1], &work, (uint32_t)PAD };
        cq_norm52_block  n1 = { in1[0], in1[1], &work, (uint32_t)PAD + reg };
        cq_clz_block     c0 = { in0[0], in0[1], &work, (uint32_t)PAD };
        cq_clz_block     c1 = { in1[0], in1[1], &work, (uint32_t)PAD + reg };
        int n = fpr_steps_of(id);

        cq_mock_reset(m);
        for (int u = 0; u < n; u++)
            switch (id) {
            case CQ_FPR_NORM52:  cq_norm52_step(&ctx, &n0, u);  break;
            case CQ_FPR_CLZ:     cq_clz_step(&ctx, &c0, u);     break;
            case CQ_FPR_SUBNORM: cq_subnorm_step(&ctx, &k0, u); break;
            default:             cq_round_step(&ctx, &r0, u);   break;
            }
        for (int u = 0; u < n; u++)
            switch (id) {
            case CQ_FPR_NORM52:  cq_norm52_step(&ctx, &n1, u);  break;
            case CQ_FPR_CLZ:     cq_clz_step(&ctx, &c1, u);     break;
            case CQ_FPR_SUBNORM: cq_subnorm_step(&ctx, &k1, u); break;
            default:             cq_round_step(&ctx, &r1, u);   break;
            }
        head = cq_mock_count(m);

        switch (id) {
        case CQ_FPR_NORM52:
            got0 = fpr_read(&ctx, cq_norm52_m(&n0), W64, &ok0);
            got1 = fpr_read(&ctx, cq_norm52_m(&n1), W64, &ok1);
            e0   = fpr_read(&ctx, cq_norm52_e(&n0), W64, &oe0);
            e1   = fpr_read(&ctx, cq_norm52_e(&n1), W64, &oe1); break;
        case CQ_FPR_CLZ:
            got0 = fpr_read(&ctx, cq_clz_wr(&c0), W64, &ok0);
            got1 = fpr_read(&ctx, cq_clz_wr(&c1), W64, &ok1);
            e0   = fpr_read(&ctx, cq_clz_exp(&c0), W64, &oe0);
            e1   = fpr_read(&ctx, cq_clz_exp(&c1), W64, &oe1); break;
        case CQ_FPR_SUBNORM:
            got0 = fpr_read(&ctx, cq_subnorm_wr(&k0), W64, &ok0);
            got1 = fpr_read(&ctx, cq_subnorm_wr(&k1), W64, &ok1);
            e0   = fpr_read(&ctx, cq_subnorm_exp(&k0), W64, &oe0);
            e1   = fpr_read(&ctx, cq_subnorm_exp(&k1), W64, &oe1); break;
        default:
            got0 = fpr_read(&ctx, cq_round_normal(&r0), W64, &ok0);
            got1 = fpr_read(&ctx, cq_round_normal(&r1), W64, &ok1);
            e0   = fpr_read(&ctx, cq_round_overflow_result(&r0), W64, &oe0);
            e1   = fpr_read(&ctx, cq_round_overflow_result(&r1), W64, &oe1); break;
        }
        (void)buf;
        if (!ok0 || !ok1 || got0 != w0.o[0] || got1 != w1.o[0])
            cq_h_fail(__FILE__, __LINE__,
                      "two %s blocks (anchors 0 and %d): output 0 came out "
                      "0x%016llx / 0x%016llx, want 0x%016llx / 0x%016llx",
                      name, pick,
                      (unsigned long long)got0, (unsigned long long)got1,
                      (unsigned long long)w0.o[0], (unsigned long long)w1.o[0]);
        if (!oe0 || !oe1 || e0 != w0.o[1] || e1 != w1.o[1])
            cq_h_fail(__FILE__, __LINE__,
                      "two %s blocks (anchors 0 and %d): output 1 came out "
                      "0x%016llx / 0x%016llx, want 0x%016llx / 0x%016llx",
                      name, pick,
                      (unsigned long long)e0, (unsigned long long)e1,
                      (unsigned long long)w0.o[1], (unsigned long long)w1.o[1]);

        /* Both reversed, innermost first — a consumer's own step space. */
        for (int u = n - 1; u >= 0; u--)
            switch (id) {
            case CQ_FPR_NORM52:  cq_norm52_step(&ctx, &n1, u);  break;
            case CQ_FPR_CLZ:     cq_clz_step(&ctx, &c1, u);     break;
            case CQ_FPR_SUBNORM: cq_subnorm_step(&ctx, &k1, u); break;
            default:             cq_round_step(&ctx, &r1, u);   break;
            }
        for (int u = n - 1; u >= 0; u--)
            switch (id) {
            case CQ_FPR_NORM52:  cq_norm52_step(&ctx, &n0, u);  break;
            case CQ_FPR_CLZ:     cq_clz_step(&ctx, &c0, u);     break;
            case CQ_FPR_SUBNORM: cq_subnorm_step(&ctx, &k0, u); break;
            default:             cq_round_step(&ctx, &r0, u);   break;
            }
        CHECK(cq_mock_is_palindrome(m, head, 0u));
    }

    /* Every bit of the SHARED region must be back, which is what a collision
     * breaks — and the canary is put back by hand, after the palindrome check
     * so its gates are outside the recorded stream in both directions. */
    for (int i = 0; i < PAD; i++) cq_emit_x(&ctx, &work.bits[i]);
    for (uint32_t i = 0; i < work.n; i++)
        if (!cq_shadow_known_zero(&ctx.shadow, cq_bit_qindex(work.bits[i])))
            cq_h_fail(__FILE__, __LINE__,
                      "two %s blocks: region bit %u (q%u) is not back at |0>",
                      name, i, cq_bit_qindex(work.bits[i]));

    for (uint32_t i = 0; i < ops.n;  i++) ops.bits[i]  = cq_bit_zero();
    for (uint32_t i = 0; i < work.n; i++) work.bits[i] = cq_bit_zero();
    cq_scratch_dispose(&ops);
    cq_scratch_dispose(&work);
    cq_ctx_dispose(&ctx);
}

CQ_TEST(two_blocks_in_one_region_do_not_collide)
{
    cq_mock m;

    cq_mock_init(&m);
    for (int r = 0; r < N_ROWS; r++)
        fpr_two_blocks(ROWS[r].id, &m, ROWS[r].name);
    cq_mock_dispose(&m);
}
