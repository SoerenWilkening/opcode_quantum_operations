/* tests/test_kernel_fsqrt_slots.inc — the PORT's census, the composition
 * identity, and the digit loop's own identity. The SCHEDULE half — the
 * four-valued slot scan — is next door in test_kernel_fsqrt_scan.inc, and the
 * LAYOUT half in test_kernel_fsqrt_spans.inc. Included by
 * test_kernel_fsqrt.c, first of the three.
 * NOTHING BELOW READS A GOLDEN.
 *
 * K11's RECORDED FINDING, APPLIED IN ADVANCE, AND K21 OFFERS IT SIXTY-FOUR
 * TIMES. The one mutant L1 cannot see is the one that looks like an
 * optimisation, and L4 is not a durable detector because
 * `CQOPS_UPDATE_GOLDENS=1` blesses a narrowing as readily as a fix. Upstream's
 * own loop invariant (`fsqrt.jl:72`, *"0 <= r <= 2q"*) makes `r`, `q` and `tt`
 * provably narrower than 64 bits at every iteration, so narrowing the
 * comparator, the subtractor and both muxes towards `2t + 2` lanes takes the
 * loop from 64 x 897 scratch bits towards 64 x 500 and the slot count with it.
 * So does sharing ONE iteration's spans across all sixty-four, which is 63 x
 * 897 bits cheaper still. Neither changes a value on iteration 0. What catches
 * them is here and in the span scan.
 *
 * AND THE IDENTITY IS OVER SLOTS, NOT GATES — D-K18-6. A block operand is a
 * view with constant lanes, or a constant span, so the §3 fold table elides
 * gates at EVERY operand mask including the all-quantum one L4 pins.
 * `compute = sum of blocks` is true of slots and false of gates.
 */

/* ---- (0) The census: the table IS the source, counted a second time. ----- */

/* HAND-COUNTED OFF fsqrt.jl, NOT OFF src/kernels/fsqrt.c. The occurrence
 * counts are the port's own content and are the thing a reader must be able to
 * check against the Julia; everything below multiplies them by costs ASKED of
 * the owning modules.
 *
 *   views    522  :34 :35 x2 :36 :58 :59 :68 :69 x2 :112, and eight per
 *                 iteration (:81 x2 :82 x2 :83 :85 :86 :89) x 64 = 512
 *   sview      1  :60   `e_unb >> 1` on an Int64 — ARITHMETIC
 *   `!=`       5  :42 :45 :46 :58 :96                 -> eq each, no not1
 *   `>=`      64  :87 x 64                            -> ult each, no not1
 *   `!`        3  :111 :113 :115                      -> not1
 *   `&` Bool   3  :111 :113 :115
 *   `|` UInt64 259  :45 :96 :116, and four per iteration (:82 :85 :86 :89)
 *   `+`        1  :60
 *   `-`       65  :57, and one per iteration (:88)
 *   ifelse   136  :45 :46 :59 :96 :113 :114 :115 :116, and two per
 *                 iteration (:88 :89)
 *   class      3  :39 :40 :41, through M31's block
 *   helpers    2  :49 :103
 *   projections 6  the two hand-offs' tuples: 2 + 4
 */
/* WHAT A ROW COSTS, ASKED OF THE OWNING MODULES BY THIS FILE AND NOT READ OUT
 * OF fsqrt_step.c. That is the independence the composition check rests on: a
 * port that narrowed a block, or mis-sized one, moves `cq_fsqrt_steps()` and
 * `cq_fsqrt_region()` and leaves these two alone. `lower_or!` is three gates
 * and one bit per lane (arith.jl:274-282), `lower_not1!` two gates and one bit
 * (:474-478) and `lower_and!` one gate per lane (:268-272) — transcribed from
 * the pinned source, because no module publishes a cost for them. */
static int fq_ref_row_steps(const cq_fsqrt_row *r)
{
    switch (r->op) {
    case CQ_FSOP_VIEW: case CQ_FSOP_SVIEW: case CQ_FSOP_OUT: return 0;
    case CQ_FSOP_CLASS:  return cq_fp_class_steps((cq_fp_class)r->s1);
    case CQ_FSOP_EQ:     return cq_eq_steps(W64);
    case CQ_FSOP_ULT:    return cq_ult_steps(W64);
    case CQ_FSOP_ADD:    return cq_add_steps(W64);
    case CQ_FSOP_SUB:    return cq_sub_steps(W64);
    case CQ_FSOP_MUX:    return cq_mux_steps(W64);
    case CQ_FSOP_OR:     return 3 * W64;
    case CQ_FSOP_NORM52: return cq_norm52_steps();
    case CQ_FSOP_ROUND:  return cq_round_steps();
    case CQ_FSOP_NOT1:   return 2;
    default:             return 1;                        /* and1 */
    }
}

static uint32_t fq_ref_row_region(const cq_fsqrt_row *r)
{
    switch (r->op) {
    case CQ_FSOP_VIEW: case CQ_FSOP_SVIEW: case CQ_FSOP_OUT: return 0u;
    case CQ_FSOP_CLASS:  return cq_fp_class_region((cq_fp_class)r->s1);
    case CQ_FSOP_EQ:     return (uint32_t)cq_eq_region(W64);
    case CQ_FSOP_ULT:    return (uint32_t)cq_ult_region(W64);
    case CQ_FSOP_ADD:    return (uint32_t)cq_add_region(W64);
    case CQ_FSOP_SUB:    return (uint32_t)cq_sub_region(W64);
    case CQ_FSOP_MUX:    return (uint32_t)cq_mux_region(W64);
    case CQ_FSOP_OR:     return (uint32_t)W64;
    case CQ_FSOP_NORM52: return cq_norm52_region();
    case CQ_FSOP_ROUND:  return cq_round_region();
    default:             return 1u;                 /* not1, and1 */
    }
}

/* The digit loop's own totals, over the 16-row template repeated 64 times and
 * named through `cq_fsqrt_loop_row` rather than through the seam arithmetic —
 * which is exactly the number a boundary error moves. */
static int fq_loop_slots(void)
{
    const cq_fsqrt_row *rows;
    int n, s = 0;

    rows = cq_fsqrt_rows(&n);
    for (int t = 0; t < CQ_FSQRT_ITERS; t++)
        for (int j = 0; j < CQ_FSQRT_ITER_ROWS; j++)
            s += fq_ref_row_steps(&rows[cq_fsqrt_loop_row(t, j)]);
    return s;
}

static uint32_t fq_loop_region(void)
{
    const cq_fsqrt_row *rows;
    uint32_t b = 0u;
    int n;

    rows = cq_fsqrt_rows(&n);
    for (int t = 0; t < CQ_FSQRT_ITERS; t++)
        for (int j = 0; j < CQ_FSQRT_ITER_ROWS; j++)
            b += fq_ref_row_region(&rows[cq_fsqrt_loop_row(t, j)]);
    return b;
}

typedef struct {
    int view, sview, out, klass, eq, ult, add, sub, mux, orr, n52, rnd,
        not1, and1;
} fq_census;

static const fq_census FQ_SRC = {
    /* view */ 522, /* sview */ 1, /* out */ 6, /* class */ 3, /* eq */ 5,
    /* ult */   64, /* add */   1, /* sub */ 65, /* mux */ 136, /* or */ 259,
    /* n52 */    1, /* rnd */   1, /* not1 */ 3, /* and1 */  3
};

static void fq_count(const cq_fsqrt_row *rows, int n, fq_census *c)
{
    memset(c, 0, sizeof *c);
    for (int i = 0; i < n; i++)
        switch (rows[i].op) {
        case CQ_FSOP_VIEW:   c->view++;  break;
        case CQ_FSOP_SVIEW:  c->sview++; break;
        case CQ_FSOP_OUT:    c->out++;   break;
        case CQ_FSOP_CLASS:  c->klass++; break;
        case CQ_FSOP_EQ:     c->eq++;    break;
        case CQ_FSOP_ULT:    c->ult++;   break;
        case CQ_FSOP_ADD:    c->add++;   break;
        case CQ_FSOP_SUB:    c->sub++;   break;
        case CQ_FSOP_MUX:    c->mux++;   break;
        case CQ_FSOP_OR:     c->orr++;   break;
        case CQ_FSOP_NORM52: c->n52++;   break;
        case CQ_FSOP_ROUND:  c->rnd++;   break;
        case CQ_FSOP_NOT1:   c->not1++;  break;
        default:             c->and1++;  break;
        }
}

CQ_TEST(the_program_is_the_source_line_for_line)
{
    const cq_fsqrt_row *rows;
    fq_census c;
    int n, seam;

    rows = cq_fsqrt_rows(&n);
    fq_count(rows, n, &c);

    CHECK_EQ(c.view, FQ_SRC.view);   CHECK_EQ(c.sview, FQ_SRC.sview);
    CHECK_EQ(c.out,  FQ_SRC.out);    CHECK_EQ(c.klass, FQ_SRC.klass);
    CHECK_EQ(c.eq,   FQ_SRC.eq);     CHECK_EQ(c.ult,   FQ_SRC.ult);
    CHECK_EQ(c.add,  FQ_SRC.add);    CHECK_EQ(c.sub,   FQ_SRC.sub);
    CHECK_EQ(c.mux,  FQ_SRC.mux);    CHECK_EQ(c.orr,   FQ_SRC.orr);
    CHECK_EQ(c.n52,  FQ_SRC.n52);    CHECK_EQ(c.rnd,   FQ_SRC.rnd);
    CHECK_EQ(c.not1, FQ_SRC.not1);   CHECK_EQ(c.and1,  FQ_SRC.and1);
    CHECK_EQ(n, c.view + c.sview + c.out + c.klass + c.eq + c.ult + c.add
                + c.sub + c.mux + c.orr + c.n52 + c.rnd + c.not1 + c.and1);
    CHECK_EQ(n, 1070);
    CHECK_EQ(n, cq_fsqrt_n_rows());

    /* NO `lower_not1!` FROM A COMPARISON, AND THAT IS THE RAW-FLAG POLARITY
     * MADE AUDITABLE. `cq_eq_flag` is `a != b` and `cq_ult_block`'s carry[W]
     * is `a >=u b`, and every comparison `soft_fsqrt` writes wants exactly
     * those — five `!=` and one `>=` per iteration. So the THREE `not1` rows
     * are the three `!` of :111, :113 and :115 and nothing else. A port that
     * spelled `r >= t` as `not1(ult(...))` because Bennett's generic
     * `lower_icmp!` dispatcher does would show up here as 67 not1 rows, and
     * would cost 4W gates and 2W qubits sixty-four times. */
    CHECK_EQ(c.not1, 3);
    CHECK_EQ(c.and1, 3);
    /* One `ult` per iteration and no more: `>=` occurs once, at :87. */
    CHECK_EQ(c.ult, CQ_FSQRT_ITERS);
    /* One `-` per iteration PLUS the one at :57; one `+` in the whole file. */
    CHECK_EQ(c.sub, CQ_FSQRT_ITERS + 1);
    CHECK_EQ(c.add, 1);

    /* THE SEAM IS A ROW BOUNDARY AND NOT A FILE SPLIT (PRD-v2 §5's M40 row,
     * as amended in fsqrt.h). Rows [0, seam) are the PRE-NORMALISE half and
     * the row AT the seam is iteration 0's first — a view of `a_hi`, whose
     * operand is the LAST row above the seam, `(ma_adj & 0x3F) << 58`'s
     * sibling `ma_adj >> 6`. */
    seam = cq_fsqrt_seam_row();
    CHECK(seam > 0 && seam < n);
    CHECK_EQ(seam, cq_fsqrt_loop_row(0, 0));
    CHECK_EQ(rows[seam - 1].op, CQ_FSOP_VIEW);   /* :69 a_lo               */
    CHECK_EQ(rows[seam].op, CQ_FSOP_VIEW);       /* :81 a_hi >> 62         */
    CHECK_EQ(n - (seam + CQ_FSQRT_ITERS * CQ_FSQRT_ITER_ROWS), 20);

    /* WELL-FORMEDNESS: every row reference is to a STRICTLY EARLIER row, every
     * operand of a 1-bit operator is a 1-bit row, every negative code is one
     * of the eight, and an SVIEW names a SPAN rather than another view. A
     * FORWARD reference is what this case exists for; the two operand
     * REFUSALS it cannot see are library guards with their own death cases. */
    for (int i = 0; i < n; i++) {
        int ar = cq_fsqrt_arity(rows[i].op);
        int s[3];

        s[0] = rows[i].s0; s[1] = rows[i].s1; s[2] = rows[i].s2;
        for (int j = 0; j < ar; j++) {
            if (s[j] >= 0 && s[j] >= i)
                cq_h_fail(__FILE__, __LINE__,
                          "row %d (op %d) operand %d names row %d, which is "
                          "not strictly earlier", i, (int)rows[i].op, j, s[j]);
            if (s[j] < 0 && s[j] < CQ_FS_K_INDEF)
                cq_h_fail(__FILE__, __LINE__,
                          "row %d operand %d is the unknown code %d", i, j,
                          s[j]);
        }
        if (rows[i].op == CQ_FSOP_CLASS) {
            CHECK_EQ(rows[i].s0, CQ_FS_A);
            CHECK(rows[i].s1 >= 0 && rows[i].s1 < CQ_FP_N_CLASS);
            CHECK(rows[i].s1 != CQ_FP_IS_SUBNORMAL);  /* fsqrt names none  */
        }
        if (rows[i].op == CQ_FSOP_OUT) {
            CHECK(rows[i].s0 >= 0 && rows[i].s0 < i);
            CHECK(rows[i].s1 >= 0 && rows[i].s1 <= CQ_FS_OUT_EXPOVF_AFT);
            CHECK_EQ(fq_ref_row_steps(&rows[i]), 0);
        }
        if (rows[i].op == CQ_FSOP_VIEW) {
            CHECK_EQ(fq_ref_row_steps(&rows[i]), 0);
            CHECK_EQ(fq_ref_row_region(&rows[i]), 0u);
            CHECK(rows[i].shift > -64 && rows[i].shift < 64);
            CHECK(rows[i].mask != 0u);
        }
        /* AN ARITHMETIC-SHIFT VIEW TERMINATES A CHAIN AND CANNOT JOIN ONE:
         * its vacated lanes are copies of its operand's sign lane, which the
         * `(shift, mask)` algebra cannot express. */
        if (rows[i].op == CQ_FSOP_SVIEW) {
            int b = rows[i].s0;

            CHECK(rows[i].shift > 0 && rows[i].shift < W64);
            CHECK(b >= 0 && b < i);
            CHECK(rows[b].op != CQ_FSOP_VIEW);
            CHECK(rows[b].op != CQ_FSOP_SVIEW);
        }
        if (rows[i].op == CQ_FSOP_NOT1 || rows[i].op == CQ_FSOP_AND1)
            for (int j = 0; j < ar; j++)
                CHECK_EQ(cq_fsqrt_row_width(rows, n, s[j]), 1);
        if (rows[i].op == CQ_FSOP_MUX)
            CHECK_EQ(cq_fsqrt_row_width(rows, n, rows[i].s0), 1);
    }
}

/* ---- (a1) The block costs, ASKED of their modules. Runs no kernel. ------ */

/* TWO CASES DELIBERATELY, so a sibling's cost moving reddens the FIRST and
 * NAMES the block rather than reddening the program-level identity with a
 * number nobody can attribute (K12.md §3.0's shape). This is the one that runs
 * no kernel. */
CQ_TEST(the_blocks_cost_what_their_modules_say_they_cost)
{
    CHECK_EQ(cq_eq_steps(W64),  317);          /* lower_eq!   5W-3        */
    CHECK_EQ(cq_ult_steps(W64), 385);          /* lower_ult!  6W+1        */
    CHECK_EQ(cq_add_steps(W64), 318);          /* lower_add!  5W-2        */
    CHECK_EQ(cq_sub_steps(W64), 447);          /* lower_sub!  7W-1        */
    CHECK_EQ(cq_mux_steps(W64), 256);          /* lower_mux!  4W          */
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_NAN),  637);
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_INF),  639);
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_ZERO), 639);
    CHECK_EQ(cq_norm52_steps(), 8755);         /* M32, common.jl:68-105   */
    CHECK_EQ(cq_round_steps(),  6287);         /* M32, :198-227           */

    CHECK_EQ(cq_eq_region(W64),  127);         /* diff ++ orr   2W-1      */
    CHECK_EQ(cq_ult_region(W64), 193);         /* nb ++ carry ++ axnb 3W+1*/
    CHECK_EQ(cq_add_region(W64), 128);         /* t ++ c        2W        */
    CHECK_EQ(cq_sub_region(W64), 192);         /* nb ++ d ++ c  3W        */
    CHECK_EQ(cq_mux_region(W64), 128);         /* r ++ d        2W        */
    CHECK_EQ(cq_fp_class_region(CQ_FP_IS_NAN),  256u);
    CHECK_EQ(cq_fp_class_region(CQ_FP_IS_INF),  257u);
    CHECK_EQ(cq_fp_class_region(CQ_FP_IS_ZERO), 257u);
    CHECK_EQ(cq_norm52_region(), 3968u);
    CHECK_EQ(cq_round_region(),  3082u);

    /* AND THE WIDTH IS 64, WHICH IS THE WHOLE OF THE NARROWING GUARD.
     * Upstream's own invariant at fsqrt.jl:72 makes `r`, `q` and `tt`
     * provably narrower than 64 lanes at every iteration; PRD-v2 §7.2 forbids
     * narrowing an intermediate towards its value's width, so every block
     * runs at the Julia type's width. A port that narrowed shows up below as
     * the program no longer being the sum of its blocks at 64. */
    CHECK(cq_ult_steps(34) < cq_ult_steps(W64));
    CHECK(cq_sub_steps(34) < cq_sub_steps(W64));
    CHECK(cq_mux_steps(34) < cq_mux_steps(W64));
}

/* ---- (a2) The program against what those blocks just cost. -------------- */

static int fq_slots(const fq_census *c)
{
    return cq_fp_class_steps(CQ_FP_IS_NAN) + cq_fp_class_steps(CQ_FP_IS_INF)
         + cq_fp_class_steps(CQ_FP_IS_ZERO)
         + c->eq   * cq_eq_steps(W64)
         + c->ult  * cq_ult_steps(W64)
         + c->add  * cq_add_steps(W64)
         + c->sub  * cq_sub_steps(W64)
         + c->mux  * cq_mux_steps(W64)
         + c->orr  * (3 * W64)                 /* lower_or!  arith.jl:274 */
         + c->n52  * cq_norm52_steps()
         + c->rnd  * cq_round_steps()
         + c->not1 * 2 + c->and1 * 1;
}

static uint32_t fq_region(const fq_census *c)
{
    return cq_fp_class_region(CQ_FP_IS_NAN) + cq_fp_class_region(CQ_FP_IS_INF)
         + cq_fp_class_region(CQ_FP_IS_ZERO)
         + (uint32_t)(c->eq  * cq_eq_region(W64))
         + (uint32_t)(c->ult * cq_ult_region(W64))
         + (uint32_t)(c->add * cq_add_region(W64))
         + (uint32_t)(c->sub * cq_sub_region(W64))
         + (uint32_t)(c->mux * cq_mux_region(W64))
         + (uint32_t)(c->orr * W64)
         + (uint32_t)c->n52 * cq_norm52_region()
         + (uint32_t)c->rnd * cq_round_region()
         + (uint32_t)(c->not1 + c->and1);
}

CQ_TEST(the_program_is_the_sum_of_its_blocks)
{
    const cq_fsqrt_row *rows;
    cq_counter fwd, unc;
    fq_census c;
    int n;

    rows = cq_fsqrt_rows(&n);
    fq_count(rows, n, &c);

    /* THE COMPOSITION IDENTITY. Every `C_*` is ASKED of its module and never
     * written down; the block COUNTS are the port's own content. This is the
     * detector K11's finding says L4 cannot be: `CQOPS_UPDATE_GOLDENS=1` would
     * bless a narrowed comparator and this line would still go red, because it
     * asks M16 what a 64-bit `ult` costs rather than reading a snapshot. */
    CHECK_EQ(cq_fsqrt_steps(),  fq_slots(&c));
    CHECK_EQ(cq_fsqrt_region(), fq_region(&c));

    /* AND THE DIGIT LOOP IS 86% OF THE SLOTS AND 85% OF THE BITS — measured,
     * and asserted as a RATIO so the case survives a sibling's cost moving.
     * The peak is not driven by the IEEE special-case machinery, by the class
     * predicates or by the select chain; it is driven by keeping every
     * iteration's comparator, subtractor and two muxes live simultaneously,
     * which is what FLAT means (PRD §15 D9). */
    CHECK(4 * fq_loop_slots() > 3 * cq_fsqrt_steps());
    CHECK(4u * fq_loop_region() > 3u * cq_fsqrt_region());

    cq_kd_measure(&SPEC, W64, &fwd, &unc);
    /* SLOTS AND GATES COME APART, ASSERTED RATHER THAN NARRATED: the
     * sandwiched total is strictly below twice the slot count plus the
     * copy-out, because the views' constant lanes and the constant spans fold.
     * An implementation that had materialised its constants (upstream's
     * `resolve!` shape, PRD-v2 §7.3's refused alternative) would make this an
     * equality. */
    CHECK(cq_count_total(&fwd) < 2u * (uint64_t)cq_fsqrt_steps() + (uint64_t)W64);

    printf("# fsqrt rows %d (%d views + 1 sview, %d projections)  slots %d  "
           "region %u  forward (x %llu, cx %llu, ccx %llu) = %llu  "
           "unc (x %llu, cx %llu, ccx %llu) = %llu\n",
           n, c.view, c.out, cq_fsqrt_steps(), cq_fsqrt_region(),
           (unsigned long long)fwd.x, (unsigned long long)fwd.cx,
           (unsigned long long)fwd.ccx,
           (unsigned long long)cq_count_total(&fwd),
           (unsigned long long)unc.x, (unsigned long long)unc.cx,
           (unsigned long long)unc.ccx,
           (unsigned long long)cq_count_total(&unc));
    fflush(stdout);
}
