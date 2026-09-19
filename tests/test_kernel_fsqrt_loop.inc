/* tests/test_kernel_fsqrt_loop.inc — THE DIGIT LOOP'S OWN IDENTITY: that the
 * program holds SIXTY-FOUR iterations of one 16-row template, at SIXTY-FOUR
 * distinct span offsets, chained through the four values the source carries.
 * Included by test_kernel_fsqrt.c AFTER test_kernel_fsqrt_slots.inc, whose
 * `fq_ref_row_steps`, `fq_ref_row_region` and `fq_loop_slots` it reuses.
 * NOTHING BELOW READS A GOLDEN.
 *
 * K21's RISK 1 IN ITS WORST FORM, AND THIS IS THE CHEAPEST DETECTOR FOR IT. An
 * off-by-one inside one iteration's slot arithmetic is ABSORBED BY THE NEXT
 * ITERATION and keeps emitting plausible gates for the remaining sixty-three,
 * so there is no crash and no obviously wrong slot — just a wrong root. K15
 * has six repeating groups and calls them its worst case; K21 has sixty-four.
 * `cq_fsqrt_steps() - n_pre - n_tail == 64 * K` is what says an iteration that
 * is one slot short did not quietly divide into something else.
 *
 * AND THE SPAN SPACING IS THE OTHER HALF. The 63 x 897 bits a shared-span
 * iteration would save is the K12-shaped mutant in this kernel's clothing: the
 * loop is genuinely sequential and `rnext[t]`/`qnext[t]` are the only things
 * iteration t+1 reads, so "reuse one iteration's scratch" looks like the
 * obvious saving and is a RE-DERIVATION of the construction (Rule 1, D9's
 * FLAT). It also stops being merely expensive and starts being wrong, because
 * iteration t's mux would then read the span iteration t-1 wrote AND write it.
 *
 * WHICH CASE SEES WHICH HALF OF THAT WAS MEASURED, NOT ASSUMED (bd 9ve.26's
 * battery, both configurations), AND THE THREE ARE GENUINELY DIFFERENT CLAIMS.
 * The offsets below come from THIS FILE's own prefix walk over the row table,
 * so what they pin is that the TABLE's rows are evenly spaced as the owning
 * modules cost them. A narrowed or mis-sized row is caught one door back, by
 * `the_program_is_the_sum_of_its_blocks`, which compares the library's own
 * totals against the same walk. And a map that computes the right TOTAL while
 * handing two iterations the SAME base — the mutant that is closest to the
 * tempting optimisation — is invisible to both, because neither reads
 * `cq_fs_map_get`: it is `the_rows_spans_are_pairwise_disjoint` and
 * `two_programs_in_one_region_do_not_collide` that see it, in the emitted
 * stream. Do not read this case as covering that one.
 */

/* Which row of the template each of the four carried values comes from, read
 * off `fsqrt.jl:82`, `:83`, `:88` and `:89`. NOT read out of src/kernels/
 * fsqrt.c: `a_hi'` is the `|` of :82, `a_lo'` is the `<< 2` of :83, `r'` is
 * the `ifelse` of :88 and `q'` is the `|` of :89. */
enum { FQ_J_AHI_NEXT = 4, FQ_J_ALO_NEXT = 5, FQ_J_R_NEXT = 12,
       FQ_J_Q_NEXT = 15 };

/* Which rows READ each carried value, in source order: `a_hi` at :81 and :82,
 * `a_lo` at :82 and :83, `r` at :85, `q` at :86 and :89. */
static const int FQ_READS_AHI[] = { 0, 2 };
static const int FQ_READS_ALO[] = { 3, 5 };
static const int FQ_READS_R[]   = { 6 };
static const int FQ_READS_Q[]   = { 8, 13 };

static void fq_chain(const cq_fsqrt_row *rows, int t, const int *reads,
                     int n_reads, int j_next, int t0_code, const char *what)
{
    int want = (t == 0) ? t0_code : cq_fsqrt_loop_row(t - 1, j_next);

    for (int r = 0; r < n_reads; r++) {
        int i = cq_fsqrt_loop_row(t, reads[r]);

        if (rows[i].s0 != want)
            cq_h_fail(__FILE__, __LINE__,
                      "iteration %d row %d reads %s from row %d; the source "
                      "carries it from row %d", t, reads[r], what,
                      (int)rows[i].s0, want);
    }
}

CQ_TEST(the_digit_loop_is_sixty_four_iterations_of_one_template)
{
    const cq_fsqrt_row *rows;
    uint32_t off[CQ_FSQRT_MAX_ROWS], k_bits;
    short tmpl[CQ_FSQRT_ITER_ROWS];
    int n, seam, k_slots = 0, pre_slots = 0, tail_slots = 0;
    int a_hi0, a_lo0;

    rows = cq_fsqrt_rows(&n);
    seam = cq_fsqrt_seam_row();

    /* An INDEPENDENT prefix walk: this file's own, over the row table and the
     * modules' own region functions, never the library's memoised map. */
    {
        uint32_t o = 0u;

        for (int i = 0; i < n; i++) { off[i] = o; o += fq_ref_row_region(&rows[i]); }
        CHECK_EQ(o, cq_fsqrt_region());
    }

    /* The template is read off ITERATION ZERO and then required of the other
     * sixty-three. Writing the sixteen ops down here would be a second
     * transcription of the template; reading one and requiring sixty-four is
     * the claim that matters. */
    for (int j = 0; j < CQ_FSQRT_ITER_ROWS; j++) {
        tmpl[j] = rows[cq_fsqrt_loop_row(0, j)].op;
        k_slots += fq_ref_row_steps(&rows[cq_fsqrt_loop_row(0, j)]);
    }
    k_bits = off[cq_fsqrt_loop_row(1, 0)] - off[cq_fsqrt_loop_row(0, 0)];

    CHECK_EQ(k_slots, 2112);        /* 4 or + ult + sub + 2 mux, at W = 64 */
    CHECK_EQ(k_bits,  897u);
    CHECK_EQ(fq_loop_slots(),  CQ_FSQRT_ITERS * k_slots);
    CHECK_EQ(fq_loop_region(), (uint32_t)CQ_FSQRT_ITERS * k_bits);

    /* The two ends the loop is spliced between, named through the table so an
     * off-by-one in the seam moves this and not only the loop. */
    a_hi0 = rows[cq_fsqrt_loop_row(0, 0)].s0;
    a_lo0 = rows[cq_fsqrt_loop_row(0, 3)].s0;
    CHECK(a_hi0 >= 0 && a_hi0 < seam);
    CHECK(a_lo0 >= 0 && a_lo0 < seam);
    CHECK(a_hi0 != a_lo0);
    CHECK_EQ(rows[a_hi0].op, CQ_FSOP_VIEW);      /* :68 ma_adj >> 6        */
    CHECK_EQ(rows[a_lo0].op, CQ_FSOP_VIEW);      /* :69 (... & 0x3F) << 58 */

    for (int t = 0; t < CQ_FSQRT_ITERS; t++) {
        for (int j = 0; j < CQ_FSQRT_ITER_ROWS; j++) {
            int i = cq_fsqrt_loop_row(t, j);

            /* (i) The op sequence is the template's, at every iteration. */
            if (rows[i].op != tmpl[j])
                cq_h_fail(__FILE__, __LINE__,
                          "iteration %d row %d is op %d; the template's row "
                          "%d is op %d", t, j, (int)rows[i].op, j, (int)tmpl[j]);
            /* (ii) AND ITS SPANS ARE ITS OWN, spaced by exactly one
             * iteration's region. A shared span is the 63 x 897-bit mutant. */
            if (off[i] != off[cq_fsqrt_loop_row(0, j)] + (uint32_t)t * k_bits)
                cq_h_fail(__FILE__, __LINE__,
                          "iteration %d row %d starts at bit %u; iteration 0's "
                          "row %d plus %d x %u is %u — an iteration whose "
                          "spans alias its predecessor's", t, j, off[i], j, t,
                          k_bits, off[cq_fsqrt_loop_row(0, j)]
                                  + (uint32_t)t * k_bits);
        }
        /* (iii) The four carried values come from the PREVIOUS iteration's
         * named rows, and at t == 0 from phase F and from the constant zero of
         * :78-79. This is the chain a "reuse the spans" mutant breaks first. */
        fq_chain(rows, t, FQ_READS_AHI, 2, FQ_J_AHI_NEXT, a_hi0, "a_hi");
        fq_chain(rows, t, FQ_READS_ALO, 2, FQ_J_ALO_NEXT, a_lo0, "a_lo");
        fq_chain(rows, t, FQ_READS_R,   1, FQ_J_R_NEXT, CQ_FS_K_ZERO, "r");
        fq_chain(rows, t, FQ_READS_Q,   2, FQ_J_Q_NEXT, CQ_FS_K_ZERO, "q");
    }

    /* (iv) AND THE LOOP IS EXACTLY 64 x K INSIDE THE PROGRAM. An iteration
     * that is one slot short still divides evenly into SOMETHING, but not into
     * 64 x K with the prologue and the tail accounted separately. */
    for (int i = 0; i < seam; i++) pre_slots += fq_ref_row_steps(&rows[i]);
    for (int i = seam + CQ_FSQRT_ITERS * CQ_FSQRT_ITER_ROWS; i < n; i++)
        tail_slots += fq_ref_row_steps(&rows[i]);
    CHECK_EQ(cq_fsqrt_steps() - pre_slots - tail_slots,
             CQ_FSQRT_ITERS * k_slots);

    /* (v) The tail reads the LAST iteration's outputs and nothing else: `wr`
     * is `q | sticky` off row 15 of iteration 63 and the sticky's `!=` reads
     * row 12 of the same. An off-by-one here is a whole digit. */
    CHECK_EQ(rows[seam + CQ_FSQRT_ITERS * CQ_FSQRT_ITER_ROWS].s0,
             cq_fsqrt_loop_row(CQ_FSQRT_ITERS - 1, FQ_J_R_NEXT));
    CHECK_EQ(rows[seam + CQ_FSQRT_ITERS * CQ_FSQRT_ITER_ROWS + 2].s0,
             cq_fsqrt_loop_row(CQ_FSQRT_ITERS - 1, FQ_J_Q_NEXT));

    printf("# fsqrt loop: %d iterations x (%d slots, %u bits) = %d slots, "
           "%u bits; prologue %d, tail %d\n",
           CQ_FSQRT_ITERS, k_slots, k_bits, fq_loop_slots(), fq_loop_region(),
           pre_slots, tail_slots);
    fflush(stdout);
}
