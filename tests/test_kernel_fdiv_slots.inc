/* tests/test_kernel_fdiv_slots.inc — the PORT's census and the composition
 * identity. The SCHEDULE half — the four-valued slot scan and the standalone
 * comparison it delegates the five composite blocks to — is next door in
 * test_kernel_fdiv_scan.inc, and the LAYOUT half — the span-disjointness scan
 * over EVERY iteration, the iteration-uniformity case, the palindrome and the
 * two-programs case — in test_kernel_fdiv_spans.inc. Included by
 * test_kernel_fdiv.c, first of the three.
 * NOTHING BELOW READS A GOLDEN.
 *
 * K11's RECORDED FINDING, APPLIED IN ADVANCE. The one mutant L1 cannot see is
 * the one that looks like an optimisation, and L4 is not a durable detector
 * because `CQOPS_UPDATE_GOLDENS=1` blesses a narrowing as readily as a fix.
 * K17 offers that trade on four separate spans and every offer is tempting:
 * `r` is provably `< 2^54` for the whole loop, so the 56 comparators and
 * subtractors could be narrowed towards 55 lanes; `q` has only `t` significant
 * bits at iteration `t`; `ea` is eleven bits in sixty-four; `sticky` is one.
 * None changes a value, a palindrome or the pool. What catches them is here.
 *
 * AND THE IDENTITY IS OVER SLOTS, NOT GATES — D-K18-6, which K18 wrote down
 * and M31 was the first module to need. A block operand is a view with vacated
 * lanes, or a constant span, so the §3 fold table elides gates at EVERY
 * operand mask including the all-quantum one L4 pins. `compute = sum of
 * blocks` is true of slots and false of gates.
 */

/* ---- (0) The census: the table IS the source, counted a second time. ----- */

/* HAND-COUNTED OFF fdiv.jl AND softfloat_common.jl, NOT OFF src/kernels/
 * fdiv.c. The occurrence counts are the port's own content and are the thing a
 * reader must be able to check against the Julia; everything below multiplies
 * them by costs ASKED of the owning modules.
 *
 *   `==`       1  :106                                  -> eq + not1
 *   `!=`       5  :66 :67 :68 :69 :101                  -> eq each
 *   `>=u`     56  :93 x56          the loop's compare   -> ult, RAW is `>=`
 *   `!`        4  :131 :132 :134 :135                   -> not1 each
 *   `&` Bool   7  :128 :130 :131 :132 :133 :134 :135
 *   `|` Bool   2  :127 :136
 *   `|` UInt64 62 :63 :66 :67 :95 x56 :102 :136 x2
 *   xor        1  :54
 *   `+`        1  :81                                   -> THE only one
 *   `-`       58  :81 :94 x56 :108
 *   ifelse   129  :66 :67 :68 :69 :94 x56 :95 x56 :101 :107 :108
 *                 :127 :128 :130 :131 :132 :133 :134 :135 :136 x2
 *   class      6  :56 :57 :58 :59 :60 :61                -> M31's block
 *   helpers    5  :78 :79 :111 :114 :122                 -> M32's four
 *   views    125  :47 :48 x2 :49 :50 :51 x2 :52 :63 :64
 *                 :95 x56 :97 x56 :106 :107 :126
 *   projections 15  the four helpers' tuples: 2 + 2 + 2 + 5 + 4
 *
 * ZERO `*` AND ZERO SIGNED COMPARES, and both are measurements rather than
 * omissions (K17.md §5.9): `soft_fdiv` has no `*` at all, and every ordered
 * compare in its own body is the loop's UNSIGNED `>=`. The eight signed
 * compares the port performs are inside M32's two helpers. */

/* WHAT A ROW COSTS, ASKED OF THE OWNING MODULES BY THIS FILE AND NOT READ OUT
 * OF fdiv_step.c. That is the independence the composition check rests on: a
 * port that narrowed a block, or mis-sized one, moves `cq_fdiv_steps()` and
 * `cq_fdiv_region()` and leaves these two alone. `lower_or!` is three gates
 * and one bit per lane (arith.jl:274-282), `lower_xor!` two gates and one bit
 * (:284-291), `lower_not1!` two gates and one bit (:474-478), `lower_and!`
 * one gate per lane (:268-272) — transcribed from the pinned source, because
 * no module publishes a cost for them (PRD-v2 §7.10). */
static int fd_ref_row_steps(const cq_fdiv_row *r)
{
    switch (r->op) {
    case CQ_FDOP_VIEW: case CQ_FDOP_OUT: return 0;
    case CQ_FDOP_CLASS:   return cq_fp_class_steps((cq_fp_class)r->s1);
    case CQ_FDOP_EQ:      return cq_eq_steps(W64);
    case CQ_FDOP_ULT:     return cq_ult_steps(W64);
    case CQ_FDOP_ADD:     return cq_add_steps(W64);
    case CQ_FDOP_SUB:     return cq_sub_steps(W64);
    case CQ_FDOP_MUX:     return cq_mux_steps(W64);
    case CQ_FDOP_OR:      return 3 * W64;
    case CQ_FDOP_XOR:     return 2 * W64;
    case CQ_FDOP_NORM52:  return cq_norm52_steps();
    case CQ_FDOP_CLZ:     return cq_clz_steps();
    case CQ_FDOP_SUBNORM: return cq_subnorm_steps();
    case CQ_FDOP_ROUND:   return cq_round_steps();
    case CQ_FDOP_NOT1:    return 2;
    case CQ_FDOP_AND1:    return 1;
    default:              return 3;                  /* or1 */
    }
}

static uint32_t fd_ref_row_region(const cq_fdiv_row *r)
{
    switch (r->op) {
    case CQ_FDOP_VIEW: case CQ_FDOP_OUT: return 0u;
    case CQ_FDOP_CLASS:   return cq_fp_class_region((cq_fp_class)r->s1);
    case CQ_FDOP_EQ:      return (uint32_t)cq_eq_region(W64);
    case CQ_FDOP_ULT:     return (uint32_t)cq_ult_region(W64);
    case CQ_FDOP_ADD:     return (uint32_t)cq_add_region(W64);
    case CQ_FDOP_SUB:     return (uint32_t)cq_sub_region(W64);
    case CQ_FDOP_MUX:     return (uint32_t)cq_mux_region(W64);
    case CQ_FDOP_OR: case CQ_FDOP_XOR: return (uint32_t)W64;
    case CQ_FDOP_NORM52:  return cq_norm52_region();
    case CQ_FDOP_CLZ:     return cq_clz_region();
    case CQ_FDOP_SUBNORM: return cq_subnorm_region();
    case CQ_FDOP_ROUND:   return cq_round_region();
    default:              return 1u;                 /* not1, and1, or1 */
    }
}

typedef struct {
    int view, out, klass, eq, ult, add, sub, mux, orr, xorr;
    int n52, clz, subn, rnd, not1, and1, or1;
} fd_census;

/* `klass` is the SIX class predicates through M31's block, and it is what
 * replaces the twenty-eight `eq` + `not1` + `and1` rows a literal
 * transcription of :56-61 would give (fdiv.h, D-K17-5). The six surviving `eq`
 * rows are the five `!=` occurrences plus the one `==` at :106; the five
 * surviving `not1` are :106's polarity fix plus the four Bool `!` of
 * :131-135. */
static const fd_census FD_SRC = {
    /* view */ 125, /* out */ 15, /* class */ 6, /* eq */ 6, /* ult */ 56,
    /* add */  1,   /* sub */ 58, /* mux */ 129, /* or */ 62, /* xor */ 1,
    /* n52 */  2,   /* clz */ 1,  /* subn */ 1,  /* rnd */ 1,
    /* not1 */ 5,   /* and1 */ 7, /* or1 */ 2
};

static void fd_count(fd_census *c)
{
    int n = cq_fdiv_n_rows();

    memset(c, 0, sizeof *c);
    for (int i = 0; i < n; i++) {
        cq_fdiv_row r;

        cq_fdiv_row_at(i, &r);
        switch (r.op) {
        case CQ_FDOP_VIEW:    c->view++; break;
        case CQ_FDOP_OUT:     c->out++;  break;
        case CQ_FDOP_CLASS:   c->klass++; break;
        case CQ_FDOP_EQ:      c->eq++;   break;
        case CQ_FDOP_ULT:     c->ult++;  break;
        case CQ_FDOP_ADD:     c->add++;  break;
        case CQ_FDOP_SUB:     c->sub++;  break;
        case CQ_FDOP_MUX:     c->mux++;  break;
        case CQ_FDOP_OR:      c->orr++;  break;
        case CQ_FDOP_XOR:     c->xorr++; break;
        case CQ_FDOP_NORM52:  c->n52++;  break;
        case CQ_FDOP_CLZ:     c->clz++;  break;
        case CQ_FDOP_SUBNORM: c->subn++; break;
        case CQ_FDOP_ROUND:   c->rnd++;  break;
        case CQ_FDOP_NOT1:    c->not1++; break;
        case CQ_FDOP_AND1:    c->and1++; break;
        default:              c->or1++;  break;
        }
    }
}

CQ_TEST(the_program_is_the_source_line_for_line)
{
    fd_census c;
    int n = cq_fdiv_n_rows();
    int base = cq_fdiv_loop_base_row(), post = cq_fdiv_post_base_row();

    fd_count(&c);

    CHECK_EQ(c.view, FD_SRC.view);   CHECK_EQ(c.out,  FD_SRC.out);
    CHECK_EQ(c.klass, FD_SRC.klass);
    CHECK_EQ(c.eq,   FD_SRC.eq);     CHECK_EQ(c.ult,  FD_SRC.ult);
    CHECK_EQ(c.add,  FD_SRC.add);    CHECK_EQ(c.sub,  FD_SRC.sub);
    CHECK_EQ(c.mux,  FD_SRC.mux);    CHECK_EQ(c.orr,  FD_SRC.orr);
    CHECK_EQ(c.xorr, FD_SRC.xorr);
    CHECK_EQ(c.n52,  FD_SRC.n52);    CHECK_EQ(c.clz,  FD_SRC.clz);
    CHECK_EQ(c.subn, FD_SRC.subn);   CHECK_EQ(c.rnd,  FD_SRC.rnd);
    CHECK_EQ(c.not1, FD_SRC.not1);   CHECK_EQ(c.and1, FD_SRC.and1);
    CHECK_EQ(c.or1,  FD_SRC.or1);
    CHECK_EQ(n, c.view + c.out + c.klass + c.eq + c.ult + c.add + c.sub + c.mux
                + c.orr + c.xorr + c.n52 + c.clz + c.subn + c.rnd
                + c.not1 + c.and1 + c.or1);
    CHECK_EQ(n, 478);

    /* THE THREE SEGMENTS, AND THE LOOP IS EXACTLY 56 BODIES OF SEVEN ROWS.
     * A port that ran the loop 55 or 64 times moves this line first, and it
     * moves NOTHING ELSE structural — every gate would still come from a
     * sibling's step block, the palindrome would survive and the scratch would
     * come back. */
    CHECK_EQ(base, 36);
    CHECK_EQ(post - base, CQ_FDIV_N_ITERS * CQ_FDIV_ITER_ROWS);
    CHECK_EQ(n - post, 50);
    CHECK_EQ(CQ_FDIV_N_ITERS, 56);       /* fdiv.jl:91 `for i in 0:55`     */

    /* ONE `==` OWES A `lower_not1!` AND THE FOUR BOOL `!` OWE ONE EACH — the
     * arithmetic that makes the raw-flag polarity auditable. `cq_eq_flag` is
     * `a != b` and `cq_ult_block`'s carry[W] is `a >=u b`, so a port that read
     * either as the value it wanted would emit the same gates, keep the
     * palindrome, leave scratch clean and be wrong. THE 56 LOOP COMPARES OWE
     * NONE, because :93 says `>=` and that IS the raw wire (K09 §5 delta 2 /
     * PRD §15 D9(b)) — which is exactly why `not1` is 5 here and not 61. */
    CHECK_EQ(c.not1, 1 + 4);
    CHECK_EQ(c.ult, CQ_FDIV_N_ITERS);
    /* The six `eq` rows are the five `!=` occurrences plus :106's `==`. Every
     * `ea == 0x7FF` in soft_fdiv is inside a class predicate. */
    CHECK_EQ(c.eq, 5 + 1);
    CHECK_EQ(c.klass, 3 * 2);
    /* ONE `+` — this is what makes `9ve.30` (cq_add_block) a dependency of
     * this bead and what makes K8's accumulator the silently wrong substitute
     * (right forward, wrong `_unc`). */
    CHECK_EQ(c.add, 1);
    /* AND 58 `-`: :81, the 56 loop subtractions, and :108. */
    CHECK_EQ(c.sub, 1 + CQ_FDIV_N_ITERS + 1);

    /* PRD-v2 §5's PRE-NORMALISE <-> LOOP SEAM, AS A ROW BOUNDARY AND NOT A
     * FILE SPLIT (fdiv.h). What crosses it is a NAMED list, and the claim the
     * file split was standing in for is that nothing below the seam reads a
     * row above it except through that list. Asserted here as the weaker,
     * checkable half: the row just above the seam is phase G's `add` and the
     * first row below it is the loop's `ult`, whose second operand is `mb` —
     * the LAST row of the pre-normalisation block. */
    {
        cq_fdiv_row above, below;

        cq_fdiv_row_at(base - 1, &above);
        cq_fdiv_row_at(base, &below);
        CHECK_EQ(above.op, CQ_FDOP_ADD);           /* :81  + BIAS          */
        CHECK_EQ(below.op, CQ_FDOP_ULT);           /* :93  r >= mb         */
        /* `mb` is a projection of the SECOND norm52, so the seam really does
         * carry the pre-normalised significand and not `mb0`. */
        {
            cq_fdiv_row mb, n52;

            cq_fdiv_row_at(below.s1, &mb);
            CHECK_EQ(mb.op, CQ_FDOP_OUT);
            CHECK_EQ(mb.s1, CQ_FD_OUT_M);
            cq_fdiv_row_at(mb.s0, &n52);
            CHECK_EQ(n52.op, CQ_FDOP_NORM52);
        }
    }

    /* WELL-FORMEDNESS: every row reference is to a STRICTLY EARLIER row, every
     * operand of a 1-bit operator is a 1-bit row, every 64-lane operand is a
     * 64-lane row or a negative code, NO ROW IS A VARIABLE SHIFT — PRD-v2 §7.6
     * audits `fdiv.jl` as having ZERO of those and a barrel appearing here
     * would mean the audit was wrong — and NO RELATIVE TEMPLATE CODE SURVIVED
     * THE REBASE, which is the one thing `cq_fdiv_row_at` can get wrong that
     * nothing else would notice: a stray 4096 would index the row table out of
     * bounds in Debug and read whatever in Release. */
    for (int i = 0; i < n; i++) {
        cq_fdiv_row r;
        int ar, s[3];

        cq_fdiv_row_at(i, &r);
        ar = cq_fdiv_arity(r.op);
        s[0] = r.s0; s[1] = r.s1; s[2] = r.s2;
        for (int j = 0; j < ar; j++) {
            if (r.op == CQ_FDOP_OUT && j == 1) continue;   /* an INDEX     */
            if (r.op == CQ_FDOP_CLASS && j == 1) continue; /* a CLASS      */
            if (s[j] >= n)
                cq_h_fail(__FILE__, __LINE__,
                          "row %d (op %d) operand %d is %d, outside the "
                          "program — an unrebased template code?", i,
                          (int)r.op, j, s[j]);
            if (s[j] >= 0 && s[j] >= i)
                cq_h_fail(__FILE__, __LINE__,
                          "row %d (op %d) operand %d names row %d, which is "
                          "not strictly earlier", i, (int)r.op, j, s[j]);
            if (s[j] < 0 && s[j] < -CQ_FD_N_CODE)
                cq_h_fail(__FILE__, __LINE__,
                          "row %d operand %d is the unknown code %d", i, j,
                          s[j]);
        }
        if (r.op == CQ_FDOP_CLASS) {
            CHECK(r.s0 == CQ_FD_A || r.s0 == CQ_FD_B);
            CHECK(r.s1 >= 0 && r.s1 < CQ_FP_N_CLASS);
            CHECK(r.s1 != CQ_FP_IS_SUBNORMAL);   /* fdiv names none        */
        }
        if (r.op == CQ_FDOP_OUT) {
            CHECK(r.s0 >= 0 && r.s0 < i);
            CHECK(r.s1 >= 0 && r.s1 <= CQ_FD_OUT_FTZ);
            CHECK_EQ(fd_ref_row_steps(&r), 0);
        }
        if (r.op == CQ_FDOP_VIEW) {
            CHECK_EQ(fd_ref_row_steps(&r), 0);
            CHECK_EQ(fd_ref_row_region(&r), 0u);
            CHECK(r.shift > -64 && r.shift < 64);
            CHECK(r.mask != 0u);
        }
        if (r.op == CQ_FDOP_NOT1 || r.op == CQ_FDOP_AND1
            || r.op == CQ_FDOP_OR1)
            for (int j = 0; j < ar; j++)
                CHECK_EQ(cq_fdiv_width_at(s[j]), 1);
        if (r.op == CQ_FDOP_MUX)
            CHECK_EQ(cq_fdiv_width_at(r.s0), 1);
    }
}

/* ---- (a0) Every iteration is the template, at a distinct offset. -------- */

/* THE CLAIM "THE LOOP IS ONE CONSTRUCTION RUN 56 TIMES" REDUCED TO SOMETHING
 * CHECKABLE, and it is the case a K12-shaped slide moves. Two halves that a
 * single scan would conflate:
 *
 *   (i)  SHAPE. Every iteration's seven rows have the template's seven ops and
 *        the template's seven shift/mask pairs, in order.
 *   (ii) WIRING. Each row's operand references are the template's, rebased —
 *        `TPL_THIS + j` to this iteration and `r_in` / `q_in` to the PREVIOUS
 *        one, with iteration 0's two special arms (D-K17-4). Checked here as
 *        the DELTA between consecutive iterations, which is exactly
 *        CQ_FDIV_ITER_ROWS for every reference into the loop and ZERO for
 *        every reference out of it.
 *
 * A port that reused one iteration's spans for the next keeps (i) entirely and
 * breaks (ii) — and the span scan next door then sees the collision. A port
 * that got the rebase arithmetic wrong breaks (ii) here first, with the row
 * named. */
CQ_TEST(every_iteration_is_the_template_at_a_distinct_offset)
{
    const cq_fdiv_row *tpl;
    int base = cq_fdiv_loop_base_row(), ntpl;

    tpl = cq_fdiv_iter_template(&ntpl);
    CHECK_EQ(ntpl, CQ_FDIV_ITER_ROWS);

    for (int t = 0; t < CQ_FDIV_N_ITERS; t++)
        for (int j = 0; j < CQ_FDIV_ITER_ROWS; j++) {
            int i = base + t * CQ_FDIV_ITER_ROWS + j;
            cq_fdiv_row r, prev;

            cq_fdiv_row_at(i, &r);
            CHECK_EQ(r.op, tpl[j].op);
            CHECK_EQ(r.shift, tpl[j].shift);
            CHECK(r.mask == tpl[j].mask);
            CHECK_EQ(fd_ref_row_steps(&r), fd_ref_row_steps(&tpl[j]));

            if (t == 0) continue;
            cq_fdiv_row_at(i - CQ_FDIV_ITER_ROWS, &prev);
            {
                int s[3], p[3];

                s[0] = r.s0;    s[1] = r.s1;    s[2] = r.s2;
                p[0] = prev.s0; p[1] = prev.s1; p[2] = prev.s2;
                for (int k = 0; k < cq_fdiv_arity(r.op); k++) {
                    /* Iteration 1 is where the D-K17-4 arms stop applying, so
                     * its `r_in` and `q_in` are the first that point at a loop
                     * row at all; from t = 2 on the delta is uniform. */
                    if (t == 1 && p[k] < base) continue;
                    if (p[k] < base) { CHECK_EQ(s[k], p[k]); continue; }
                    if (s[k] != p[k] + CQ_FDIV_ITER_ROWS)
                        cq_h_fail(__FILE__, __LINE__,
                                  "iteration %d row %d operand %d is row %d; "
                                  "iteration %d's is row %d, so the delta is "
                                  "%d and not %d — the 56 bodies are not one "
                                  "template at distinct offsets",
                                  t, j, k, s[k], t - 1, p[k],
                                  s[k] - p[k], CQ_FDIV_ITER_ROWS);
                }
            }
        }

    /* D-K17-4, ASSERTED RATHER THAN NARRATED. Iteration 0's `r_in` is `ma` —
     * a projection of the first norm52, not a view — and its `q_in` is the
     * literal CQ_FD_K_ZERO. Every later iteration reads the PREVIOUS body's
     * two view rows. This is the one place the two readings (literal zero vs a
     * pre-materialised zero register) are distinguishable in the table. */
    {
        cq_fdiv_row h0_ult, h0_q, h1_ult, h1_q, ma;

        cq_fdiv_row_at(base + 0, &h0_ult);            /* :93 at t = 0      */
        cq_fdiv_row_at(base + 3, &h0_q);              /* :95 q << 1, t = 0 */
        cq_fdiv_row_at(base + CQ_FDIV_ITER_ROWS + 0, &h1_ult);
        cq_fdiv_row_at(base + CQ_FDIV_ITER_ROWS + 3, &h1_q);

        CHECK_EQ(h0_q.s0, CQ_FD_K_ZERO);              /* q_0 = UInt64(0)   */
        CHECK(h0_ult.s0 >= 0 && h0_ult.s0 < base);    /* r_0 = ma          */
        cq_fdiv_row_at(h0_ult.s0, &ma);
        CHECK_EQ(ma.op, CQ_FDOP_OUT);
        CHECK_EQ(ma.s1, CQ_FD_OUT_M);
        /* ... and it is the FIRST norm52's `m`, not the second's. */
        CHECK(ma.s0 < h1_q.s0);

        CHECK_EQ(h1_q.s0, base + 5);                  /* q_out(0), the or  */
        CHECK_EQ(h1_ult.s0, base + 6);                /* r_in(1), the view */
    }
}

/* ---- (a1) The block costs, ASKED of their modules. Runs no kernel. ------ */

/* TWO CASES DELIBERATELY, so a sibling's cost moving reddens the FIRST and
 * NAMES the block rather than reddening the program-level identity with a
 * number nobody can attribute (K12.md §3.0's shape, K16's copy). This is the
 * one that runs no kernel. */
CQ_TEST(the_blocks_cost_what_their_modules_say_they_cost)
{
    /* ASKED of M14, M16, M17, M31 and M32 and pinned against their own
     * measured W = 64 rows. K17 composes eight block types, so if one moves
     * everything below moves with it and this line says so first. */
    CHECK_EQ(cq_eq_steps(W64),  317);          /* lower_eq!   5W-3        */
    CHECK_EQ(cq_ult_steps(W64), 385);          /* lower_ult!  6W+1        */
    CHECK_EQ(cq_add_steps(W64), 318);          /* lower_add!  5W-2        */
    CHECK_EQ(cq_sub_steps(W64), 447);          /* lower_sub!  7W-1        */
    CHECK_EQ(cq_mux_steps(W64), 256);          /* lower_mux!  4W          */
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_NAN),  637);
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_INF),  639);
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_ZERO), 639);
    CHECK_EQ(cq_norm52_steps(), 8755);         /* M32, common.jl:68-105   */
    CHECK_EQ(cq_clz_steps(),    7668);         /* M32, :113-139           */
    CHECK_EQ(cq_subnorm_steps(), 9165);        /* M32, :174-191           */
    CHECK_EQ(cq_round_steps(),  6287);         /* M32, :198-227           */

    CHECK_EQ(cq_eq_region(W64),  127);         /* diff ++ orr   2W-1      */
    CHECK_EQ(cq_ult_region(W64), 193);         /* nb ++ carry ++ axnb 3W+1*/
    CHECK_EQ(cq_add_region(W64), 128);         /* t ++ c        2W        */
    CHECK_EQ(cq_sub_region(W64), 192);         /* nb ++ d ++ c  3W        */
    CHECK_EQ(cq_mux_region(W64), 128);         /* r ++ d        2W        */
    CHECK_EQ(cq_norm52_region(),  3968u);
    CHECK_EQ(cq_clz_region(),     3456u);
    CHECK_EQ(cq_subnorm_region(), 5253u);
    CHECK_EQ(cq_round_region(),   3082u);

    /* THE LOOP IDENTITY ON ITS OWN, because a wrong `loop_K` is a wrong step
     * decode at 56 places at once — and the segmented dispatch would then
     * still land inside SOME row of SOME iteration and emit a plausible gate.
     * `C_ult + C_sub + 2*C_mux + C_or`, every term asked (K17.md §2.7). */
    CHECK_EQ(cq_fdiv_iter_steps(),
             cq_ult_steps(W64) + cq_sub_steps(W64) + 2 * cq_mux_steps(W64)
             + 3 * W64);
    CHECK_EQ(cq_fdiv_iter_region(),
             (uint32_t)(cq_ult_region(W64) + cq_sub_region(W64)
                        + 2 * cq_mux_region(W64) + W64));

    /* AND THE WIDTH IS 64, WHICH IS THE WHOLE OF THE NARROWING GUARD. `r` is
     * provably below 2^54 for the whole loop, `q` has `t` significant bits at
     * iteration `t` and `ea` is eleven bits in sixty-four; PRD-v2 §7.2 forbids
     * narrowing an intermediate towards its value's width, so every block runs
     * at the Julia type's width. A port that narrowed shows up below as the
     * program no longer being the sum of its blocks at 64. THE LOOP IS WHERE
     * THIS IS WORTH THE MOST — 62 % of the slots live in 56 copies of four
     * blocks. */
    CHECK(cq_ult_steps(55) < cq_ult_steps(W64));
    CHECK(cq_sub_steps(55) < cq_sub_steps(W64));
    CHECK(cq_eq_steps(11)  < cq_eq_steps(W64));
}

/* ---- (a2) The program against what those blocks just cost. -------------- */

static int fd_slots(const fd_census *c)
{
    return c->klass / 3 * (cq_fp_class_steps(CQ_FP_IS_NAN)
                           + cq_fp_class_steps(CQ_FP_IS_INF)
                           + cq_fp_class_steps(CQ_FP_IS_ZERO))
         + c->eq   * cq_eq_steps(W64)
         + c->ult  * cq_ult_steps(W64)
         + c->add  * cq_add_steps(W64)
         + c->sub  * cq_sub_steps(W64)
         + c->mux  * cq_mux_steps(W64)
         + c->orr  * (3 * W64)                 /* lower_or!  arith.jl:274 */
         + c->xorr * (2 * W64)                 /* lower_xor! arith.jl:284 */
         + c->n52  * cq_norm52_steps()
         + c->clz  * cq_clz_steps()
         + c->subn * cq_subnorm_steps()
         + c->rnd  * cq_round_steps()
         + c->not1 * 2 + c->and1 * 1 + c->or1 * 3;
}

static uint32_t fd_region(const fd_census *c)
{
    return (uint32_t)(c->klass / 3) * (cq_fp_class_region(CQ_FP_IS_NAN)
                                       + cq_fp_class_region(CQ_FP_IS_INF)
                                       + cq_fp_class_region(CQ_FP_IS_ZERO))
         + (uint32_t)(c->eq   * cq_eq_region(W64))
         + (uint32_t)(c->ult  * cq_ult_region(W64))
         + (uint32_t)(c->add  * cq_add_region(W64))
         + (uint32_t)(c->sub  * cq_sub_region(W64))
         + (uint32_t)(c->mux  * cq_mux_region(W64))
         + (uint32_t)((c->orr + c->xorr) * W64)
         + (uint32_t)c->n52  * cq_norm52_region()
         + (uint32_t)c->clz  * cq_clz_region()
         + (uint32_t)c->subn * cq_subnorm_region()
         + (uint32_t)c->rnd  * cq_round_region()
         + (uint32_t)(c->not1 + c->and1 + c->or1);
}

CQ_TEST(the_program_is_the_sum_of_its_blocks)
{
    cq_counter fwd, unc;
    fd_census c;

    fd_count(&c);

    /* THE COMPOSITION IDENTITY. Every `C_*` is ASKED of its module and never
     * written down; the block COUNTS are the port's own content. This is the
     * detector K11's finding says L4 cannot be: `CQOPS_UPDATE_GOLDENS=1` would
     * bless a narrowed comparator and this line would still go red, because it
     * asks M16 what a 64-bit compare costs rather than reading a snapshot. */
    CHECK_EQ(cq_fdiv_steps(),  fd_slots(&c));
    CHECK_EQ(cq_fdiv_region(), fd_region(&c));

    /* AND THE LOOP IS SPELLED AS A COUNT, NOT AS A CLOSED FORM: 56 times what
     * one iteration costs, with `56` read from the source's own `for i in
     * 0:55` rather than from a formula in `W`. `cq_divrem_steps(W, q)` is
     * `W(17W+2)` and would give 64 iterations at W = 64 — the shape PRD-v2
     * §7.16 declined. */
    CHECK_EQ(CQ_FDIV_N_ITERS * cq_fdiv_iter_steps(),
             c.ult * cq_ult_steps(W64) + CQ_FDIV_N_ITERS * cq_sub_steps(W64)
             + 2 * CQ_FDIV_N_ITERS * cq_mux_steps(W64)
             + CQ_FDIV_N_ITERS * 3 * W64);
    /* The loop is the single largest contribution on both axes — asserted as a
     * RATIO so the case survives a sibling's cost moving, and with the two
     * axes at DIFFERENT thresholds because they genuinely disagree. */
    CHECK(CQ_FDIV_N_ITERS * cq_fdiv_iter_steps() > cq_fdiv_steps() / 2);
    CHECK((uint32_t)CQ_FDIV_N_ITERS * cq_fdiv_iter_region()
          > cq_fdiv_region() / 2u);

    /* SLOTS AND GATES COME APART, ASSERTED RATHER THAN NARRATED: the
     * sandwiched total is strictly below twice the slot count plus the
     * copy-out, because the views' constant lanes and the constant spans fold.
     * An implementation that had materialised its constants (upstream's
     * `resolve!` shape, PRD-v2 §7.3's refused alternative) would make this an
     * equality. */
    cq_kd_measure(&SPEC, W64, &fwd, &unc);
    CHECK(cq_count_total(&fwd) < 2u * (uint64_t)cq_fdiv_steps() + (uint64_t)W64);

    printf("# fdiv rows %d (%d views, %d projections)  slots %d  region %u  "
           "iter %d slots / %u bits x %d  "
           "forward (x %llu, cx %llu, ccx %llu) = %llu  "
           "unc (x %llu, cx %llu, ccx %llu) = %llu\n",
           cq_fdiv_n_rows(), c.view, c.out, cq_fdiv_steps(), cq_fdiv_region(),
           cq_fdiv_iter_steps(), cq_fdiv_iter_region(), CQ_FDIV_N_ITERS,
           (unsigned long long)fwd.x, (unsigned long long)fwd.cx,
           (unsigned long long)fwd.ccx,
           (unsigned long long)cq_count_total(&fwd),
           (unsigned long long)unc.x, (unsigned long long)unc.cx,
           (unsigned long long)unc.ccx,
           (unsigned long long)cq_count_total(&unc));
    fflush(stdout);
}
