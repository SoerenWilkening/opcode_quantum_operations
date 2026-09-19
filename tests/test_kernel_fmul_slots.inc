/* tests/test_kernel_fmul_slots.inc — the PORT's census, the composition
 * identity. The SCHEDULE half — the four-valued slot scan and the standalone
 * comparison it delegates the five composite blocks to — is next door in
 * test_kernel_fmul_scan.inc, and the LAYOUT half — the span-disjointness
 * scan, the four products' offsets, the 106-bit lane map, the palindrome and
 * the two-programs case — in test_kernel_fmul_spans.inc. Included by
 * test_kernel_fmul.c, first of the three.
 * NOTHING BELOW READS A GOLDEN.
 *
 * K11's RECORDED FINDING, APPLIED IN ADVANCE. The one mutant L1 cannot see is
 * the one that looks like an optimisation, and L4 is not a durable detector
 * because `CQOPS_UPDATE_GOLDENS=1` blesses a narrowing as readily as a fix.
 * K16 offers that trade more richly than any kernel so far and every offer is
 * tempting: narrow the four products from 64 lanes to 27 and 26 (they are
 * provably that wide); narrow `c1` and `c2` from 64-lane spans to the single
 * bits they hold; run `ea == 0x7FF` at eleven lanes; CSE the two `ea == 0x7FF`
 * occurrences, the three `a_zero | b_zero` and the two `cross << 26`; delete
 * the seven dead rows of :94-102. None changes a value, a palindrome or the
 * pool. What catches them is here.
 *
 * AND THE IDENTITY IS OVER SLOTS, NOT GATES — D-K18-6, which K18 wrote down
 * and M31 was the first module to need. A block operand is a view with 2, 8,
 * 14, 22, 26, 38, 53 or 63 constant lanes, or a constant span, so the §3 fold
 * table elides gates at EVERY operand mask including the all-quantum one L4
 * pins. `compute = sum of blocks` is true of slots and false of gates.
 */

/* ---- (0) The census: the table IS the source, counted a second time. ----- */

/* HAND-COUNTED OFF fmul.jl AND softfloat_common.jl, NOT OFF src/kernels/
 * fmul.c. The occurrence counts are the port's own content and are the thing a
 * reader must be able to check against the Julia; everything below multiplies
 * them by costs ASKED of the owning modules.
 *
 *   `==`      10  :31 :32 :33 x2 :34 x2 :35 x2 :36 x2   -> eq + not1 each
 *   `!=`      10  :31 :32 :44 :45 :48 :49 :175 :181 :186 :188 -> eq each
 *   `<`        3  :97 :129 :133                          -> ult + not1 each
 *   `!`        1  :211                                    -> not1
 *   `&` Bool   9  :31 :32 :33 :34 :35 :36 :208 :210 :211
 *   `|` Bool   7  :207 :209 :210 :211 x2 ... :212
 *   `|` UInt64 9  :40 :44 :45 :174 :177 :180 :183 :212 x2
 *   xor        1  :28
 *   `+`        9  :63 :91 :95 :128 :132 :136 x3 :188
 *   `-`        1  :63
 *   `*`        4  :78 :79 :80 :81
 *   ifelse    18  :44 :45 :48 :49 :97 :129 :133 :175 :181 :186 :188
 *                 :207 :208 :209 :210 :211 :212 x2
 *   helpers    5  :57 :58 :195 :198 :202
 *   views     35  :19 :20 x2 :21 :23 :24 x2 :25 :40 :41 :73 :74 :75 :76
 *                 :94 :101 :102 :126 :127 :131 :135 :136 x2 :156 x2 :174 x3
 *                 :180 x3 :175 :181 :191 :206
 *   projections 15  the four helpers' tuples: 2 + 2 + 2 + 5 + 4
 */
/* WHAT A ROW COSTS, ASKED OF THE OWNING MODULES BY THIS FILE AND NOT READ OUT
 * OF fmul_step.c. That is the independence the composition check rests on: a
 * port that narrowed a block, or mis-sized one, moves `cq_fmul_steps()` and
 * `cq_fmul_region()` and leaves these two alone. `lower_or!` is three gates
 * and one bit per lane (arith.jl:274-282), `lower_xor!` two gates and one bit
 * (:284-291), `lower_not1!` two gates and one bit (:474-478), `lower_and!`
 * one gate per lane (:268-272) — transcribed from the pinned source, because
 * no module publishes a cost for them (PRD-v2 §7.10). */
static int fm_ref_row_steps(const cq_fmul_row *r)
{
    switch (r->op) {
    case CQ_FMOP_VIEW: case CQ_FMOP_OUT: return 0;
    case CQ_FMOP_CLASS:   return cq_fp_class_steps((cq_fp_class)r->s1);
    case CQ_FMOP_EQ:      return cq_eq_steps(W64);
    case CQ_FMOP_ULT:     return cq_ult_steps(W64);
    case CQ_FMOP_ADD:     return cq_add_steps(W64);
    case CQ_FMOP_SUB:     return cq_sub_steps(W64);
    case CQ_FMOP_MUX:     return cq_mux_steps(W64);
    case CQ_FMOP_OR:      return 3 * W64;
    case CQ_FMOP_XOR:     return 2 * W64;
    case CQ_FMOP_MUL:     return cq_mul_steps(W64);
    case CQ_FMOP_NORM52:  return cq_norm52_steps();
    case CQ_FMOP_CLZ:     return cq_clz_steps();
    case CQ_FMOP_SUBNORM: return cq_subnorm_steps();
    case CQ_FMOP_ROUND:   return cq_round_steps();
    case CQ_FMOP_NOT1:    return 2;
    case CQ_FMOP_AND1:    return 1;
    default:              return 3;                  /* or1 */
    }
}

static uint32_t fm_ref_row_region(const cq_fmul_row *r)
{
    switch (r->op) {
    case CQ_FMOP_VIEW: case CQ_FMOP_OUT: return 0u;
    case CQ_FMOP_CLASS:   return cq_fp_class_region((cq_fp_class)r->s1);
    case CQ_FMOP_EQ:      return (uint32_t)cq_eq_region(W64);
    case CQ_FMOP_ULT:     return (uint32_t)cq_ult_region(W64);
    case CQ_FMOP_ADD:     return (uint32_t)cq_add_region(W64);
    case CQ_FMOP_SUB:     return (uint32_t)cq_sub_region(W64);
    case CQ_FMOP_MUX:     return (uint32_t)cq_mux_region(W64);
    case CQ_FMOP_OR: case CQ_FMOP_XOR: return (uint32_t)W64;
    case CQ_FMOP_MUL:     return (uint32_t)cq_mul_region(W64);
    case CQ_FMOP_NORM52:  return cq_norm52_region();
    case CQ_FMOP_CLZ:     return cq_clz_region();
    case CQ_FMOP_SUBNORM: return cq_subnorm_region();
    case CQ_FMOP_ROUND:   return cq_round_region();
    default:              return 1u;                 /* not1, and1, or1 */
    }
}

typedef struct {
    int view, out, klass, eq, ult, add, sub, mux, orr, xorr, mul;
    int n52, clz, subn, rnd, not1, and1, or1;
} fm_census;

/* `klass` is the SIX class predicates through M31's block, and it is what
 * replaces the twenty-eight `eq` + `not1` + `and1` rows a literal transcription
 * of :31-36 would give (fmul.h). Same slots, same bits, same gates — the eight
 * surviving `eq` rows are the eight `!=` occurrences OUTSIDE phase C and the
 * four surviving `not1` are the three `<` polarity fixes plus the `!` at
 * :211. */
static const fm_census FM_SRC = {
    /* view */ 35, /* out */ 15, /* class */ 6, /* eq */ 8, /* ult */ 3,
    /* add */  9,  /* sub */ 1,  /* mux */ 18, /* or */  9, /* xor */ 1,
    /* mul */  4,  /* n52 */ 2,  /* clz */ 1,  /* subn */ 1, /* rnd */ 1,
    /* not1 */ 4,  /* and1 */ 3, /* or1 */ 7
};

static void fm_count(const cq_fmul_row *rows, int n, fm_census *c)
{
    memset(c, 0, sizeof *c);
    for (int i = 0; i < n; i++)
        switch (rows[i].op) {
        case CQ_FMOP_VIEW:    c->view++; break;
        case CQ_FMOP_OUT:     c->out++;  break;
        case CQ_FMOP_CLASS:   c->klass++; break;
        case CQ_FMOP_EQ:      c->eq++;   break;
        case CQ_FMOP_ULT:     c->ult++;  break;
        case CQ_FMOP_ADD:     c->add++;  break;
        case CQ_FMOP_SUB:     c->sub++;  break;
        case CQ_FMOP_MUX:     c->mux++;  break;
        case CQ_FMOP_OR:      c->orr++;  break;
        case CQ_FMOP_XOR:     c->xorr++; break;
        case CQ_FMOP_MUL:     c->mul++;  break;
        case CQ_FMOP_NORM52:  c->n52++;  break;
        case CQ_FMOP_CLZ:     c->clz++;  break;
        case CQ_FMOP_SUBNORM: c->subn++; break;
        case CQ_FMOP_ROUND:   c->rnd++;  break;
        case CQ_FMOP_NOT1:    c->not1++; break;
        case CQ_FMOP_AND1:    c->and1++; break;
        default:              c->or1++;  break;
        }
}

CQ_TEST(the_program_is_the_source_line_for_line)
{
    const cq_fmul_row *rows;
    fm_census c;
    int n, seam;

    rows = cq_fmul_rows(&n);
    fm_count(rows, n, &c);

    CHECK_EQ(c.view, FM_SRC.view);   CHECK_EQ(c.out,  FM_SRC.out);
    CHECK_EQ(c.klass, FM_SRC.klass);
    CHECK_EQ(c.eq,   FM_SRC.eq);     CHECK_EQ(c.ult,  FM_SRC.ult);
    CHECK_EQ(c.add,  FM_SRC.add);    CHECK_EQ(c.sub,  FM_SRC.sub);
    CHECK_EQ(c.mux,  FM_SRC.mux);    CHECK_EQ(c.orr,  FM_SRC.orr);
    CHECK_EQ(c.xorr, FM_SRC.xorr);   CHECK_EQ(c.mul,  FM_SRC.mul);
    CHECK_EQ(c.n52,  FM_SRC.n52);    CHECK_EQ(c.clz,  FM_SRC.clz);
    CHECK_EQ(c.subn, FM_SRC.subn);   CHECK_EQ(c.rnd,  FM_SRC.rnd);
    CHECK_EQ(c.not1, FM_SRC.not1);   CHECK_EQ(c.and1, FM_SRC.and1);
    CHECK_EQ(c.or1,  FM_SRC.or1);
    CHECK_EQ(n, c.view + c.out + c.klass + c.eq + c.ult + c.add + c.sub + c.mux
                + c.orr + c.xorr + c.mul + c.n52 + c.clz + c.subn + c.rnd
                + c.not1 + c.and1 + c.or1);
    CHECK_EQ(n, 128);
    CHECK_EQ(n, cq_fmul_n_rows());

    /* THREE `<` EACH OWE A `lower_not1!`, PLUS ONE `!` AT :211 — and this is
     * the arithmetic that makes the raw-flag polarity auditable. `cq_eq_flag`
     * is `a != b` and `cq_ult_block`'s carry[W] is `a >=u b`, so a port that
     * read either as the value it wanted would emit the same gates, keep the
     * palindrome, leave scratch clean and be wrong — K9's own uge-meaning-ule,
     * and nothing structural sees it. The TEN `==` of :31-36 owe theirs too
     * and pay it INSIDE M31's class block, which is why `not1` is 4 here and
     * not 14 (fmul.h). */
    CHECK_EQ(c.not1, 3 + 1);
    /* The eight surviving `eq` rows are the eight `!=` occurrences outside
     * phase C: :44 :45 :48 :49 :175 :181 :186 :188. Every `==` in soft_fmul is
     * inside a class predicate. */
    CHECK_EQ(c.eq, 8);
    /* And the six class rows are two per predicate kind, one per operand. */
    CHECK_EQ(c.klass, 3 * 2);

    /* THE SEAM IS A ROW BOUNDARY AND NOT A FILE SPLIT (PRD-v2 §5's M34 row,
     * as amended in fmul.h). What crosses `fmul.jl:136` is a NAMED list, and
     * the claim the file split was standing in for is that nothing below the
     * seam reads a row above it except through that list. Asserted here as the
     * weaker, checkable half: the seam is where the last adder of the assembly
     * ends, so the row just below it is a VIEW of that adder's output and the
     * row just above it is an `add`. */
    seam = cq_fmul_seam_row();
    CHECK(seam > 0 && seam < n);
    CHECK_EQ(rows[seam - 1].op, CQ_FMOP_ADD);   /* :136 prod_hi_final     */
    CHECK_EQ(rows[seam].op, CQ_FMOP_VIEW);      /* :156 prod_hi >> 41     */
    CHECK_EQ(rows[seam].s0, seam - 1);

    /* WELL-FORMEDNESS: every row reference is to a STRICTLY EARLIER row, every
     * operand of a 1-bit operator is a 1-bit row, every 64-lane operand is a
     * 64-lane row or a negative code, and no row is a variable shift — PRD-v2
     * §7.6 audits `fmul.jl` as having ZERO of those and a barrel appearing
     * here would mean the audit was wrong. A FORWARD reference is what this
     * case exists for; the two operand REFUSALS it cannot see are library
     * guards with their own death cases (bd
     * a-table-driven-kernel-needs-both-operand-refusals). */
    for (int i = 0; i < n; i++) {
        int ar = cq_fmul_arity(rows[i].op);
        int s[3];

        s[0] = rows[i].s0; s[1] = rows[i].s1; s[2] = rows[i].s2;
        for (int j = 0; j < ar; j++) {
            if (s[j] >= 0 && s[j] >= i)
                cq_h_fail(__FILE__, __LINE__,
                          "row %d (op %d) operand %d names row %d, which is "
                          "not strictly earlier", i, (int)rows[i].op, j, s[j]);
            if (s[j] < 0 && s[j] < CQ_FM_K_INDEF)
                cq_h_fail(__FILE__, __LINE__,
                          "row %d operand %d is the unknown code %d", i, j,
                          s[j]);
        }
        if (rows[i].op == CQ_FMOP_CLASS) {
            /* `s1` is a cq_fp_class, not an operand, so `cq_fmul_arity` does
             * not reach it and only this line does. An out-of-range class is
             * caught one layer down by M31's own guard, which is why the
             * claim is made HERE as a property of the TABLE. */
            CHECK(rows[i].s0 == CQ_FM_A || rows[i].s0 == CQ_FM_B);
            CHECK(rows[i].s1 >= 0 && rows[i].s1 < CQ_FP_N_CLASS);
            CHECK(rows[i].s1 != CQ_FP_IS_SUBNORMAL);   /* fmul names none */
        }
        if (rows[i].op == CQ_FMOP_OUT) {
            CHECK(rows[i].s0 >= 0 && rows[i].s0 < i);
            CHECK(rows[i].s1 >= 0 && rows[i].s1 <= CQ_FM_OUT_FTZ);
            CHECK_EQ(fm_ref_row_steps(&rows[i]), 0);
        }
        if (rows[i].op == CQ_FMOP_VIEW) {
            CHECK_EQ(fm_ref_row_steps(&rows[i]), 0);
            CHECK_EQ(fm_ref_row_region(&rows[i]), 0u);
            CHECK(rows[i].shift > -64 && rows[i].shift < 64);
            CHECK(rows[i].mask != 0u);
        }
        /* The 1-bit operators take 1-bit rows and the 64-lane ones do not. */
        if (rows[i].op == CQ_FMOP_NOT1 || rows[i].op == CQ_FMOP_AND1
            || rows[i].op == CQ_FMOP_OR1)
            for (int j = 0; j < ar; j++)
                CHECK_EQ(cq_fmul_row_width(rows, n, s[j]), 1);
        if (rows[i].op == CQ_FMOP_MUX)
            CHECK_EQ(cq_fmul_row_width(rows, n, rows[i].s0), 1);
    }
}

/* ---- (a1) The block costs, ASKED of their modules. Runs no kernel. ------ */

/* TWO CASES DELIBERATELY, so a sibling's cost moving reddens the FIRST and
 * NAMES the block rather than reddening the program-level identity with a
 * number nobody can attribute (K12.md §3.0's shape, K18's copy). This is the
 * one that runs no kernel. */
CQ_TEST(the_blocks_cost_what_their_modules_say_they_cost)
{
    /* ASKED of M14, M16, M17, M18 and M32 and pinned against their own
     * measured W = 64 rows. K16 composes all nine block types, so if one moves
     * everything below moves with it and this line says so first. */
    CHECK_EQ(cq_eq_steps(W64),  317);          /* lower_eq!   5W-3        */
    CHECK_EQ(cq_ult_steps(W64), 385);          /* lower_ult!  6W+1        */
    CHECK_EQ(cq_add_steps(W64), 318);          /* lower_add!  5W-2        */
    CHECK_EQ(cq_sub_steps(W64), 447);          /* lower_sub!  7W-1        */
    CHECK_EQ(cq_mux_steps(W64), 256);          /* lower_mux!  4W          */
    CHECK_EQ(cq_mul_steps(W64), 26336);        /* K11 (13W^2-9W)/2        */
    /* M31's block is 2*cq_eq_steps(64) + 2 per `==` + the `and`, so the NaN
     * row (one `==`) is 637 and the inf/zero rows (two) are 639. The six of
     * them together are 3,830 slots and 1,540 bits — EXACTLY what the
     * twenty-eight rows a literal transcription would give, which is why
     * composing is not a cost decision (fmul.h). */
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_NAN),  637);
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_INF),  639);
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_ZERO), 639);
    CHECK_EQ(2 * (cq_fp_class_steps(CQ_FP_IS_NAN)
                  + cq_fp_class_steps(CQ_FP_IS_INF)
                  + cq_fp_class_steps(CQ_FP_IS_ZERO)),
             12 * cq_eq_steps(W64) + 10 * 2 + 6 * 1);
    CHECK_EQ(2u * (cq_fp_class_region(CQ_FP_IS_NAN)
                   + cq_fp_class_region(CQ_FP_IS_INF)
                   + cq_fp_class_region(CQ_FP_IS_ZERO)),
             (uint32_t)(12 * cq_eq_region(W64) + 10 + 6));
    CHECK_EQ(cq_norm52_steps(), 8755);         /* M32, common.jl:68-105   */
    CHECK_EQ(cq_clz_steps(),    7668);         /* M32, :113-139           */
    CHECK_EQ(cq_subnorm_steps(), 9165);        /* M32, :174-191           */
    CHECK_EQ(cq_round_steps(),  6287);         /* M32, :198-227           */

    CHECK_EQ(cq_eq_region(W64),  127);         /* diff ++ orr   2W-1      */
    CHECK_EQ(cq_ult_region(W64), 193);         /* nb ++ carry ++ axnb 3W+1*/
    CHECK_EQ(cq_add_region(W64), 128);         /* t ++ c        2W        */
    CHECK_EQ(cq_sub_region(W64), 192);         /* nb ++ d ++ c  3W        */
    CHECK_EQ(cq_mux_region(W64), 128);         /* r ++ d        2W        */
    CHECK_EQ(cq_mul_region(W64), 4224);        /* accum ++ pp ++ x  W^2+2W*/
    CHECK_EQ(cq_norm52_region(),  3968u);
    CHECK_EQ(cq_clz_region(),     3456u);
    CHECK_EQ(cq_subnorm_region(), 5253u);
    CHECK_EQ(cq_round_region(),   3082u);

    /* AND THE WIDTH IS 64, WHICH IS THE WHOLE OF THE NARROWING GUARD. `ea` is
     * eleven bits, `a_lo` twenty-six, `a_hi` twenty-seven and `c1` exactly
     * one; PRD-v2 §7.2 forbids narrowing an intermediate towards its value's
     * width, so every block runs at the Julia type's width. A port that
     * narrowed shows up below as the program no longer being the sum of its
     * blocks at 64. THE MULTIPLIER IS WHERE THIS IS WORTH THE MOST — 27 lanes
     * against 64 is a factor of five on the largest block in the kernel. */
    CHECK(cq_eq_steps(11)  < cq_eq_steps(W64));
    CHECK(cq_mul_steps(27) < cq_mul_steps(W64) / 4);
    CHECK(cq_add_steps(1)  < cq_add_steps(W64));
}

/* ---- (a2) The program against what those blocks just cost. -------------- */

static int fm_slots(const fm_census *c)
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
         + c->mul  * cq_mul_steps(W64)
         + c->n52  * cq_norm52_steps()
         + c->clz  * cq_clz_steps()
         + c->subn * cq_subnorm_steps()
         + c->rnd  * cq_round_steps()
         + c->not1 * 2 + c->and1 * 1 + c->or1 * 3;
}

static uint32_t fm_region(const fm_census *c)
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
         + (uint32_t)(c->mul  * cq_mul_region(W64))
         + (uint32_t)c->n52  * cq_norm52_region()
         + (uint32_t)c->clz  * cq_clz_region()
         + (uint32_t)c->subn * cq_subnorm_region()
         + (uint32_t)c->rnd  * cq_round_region()
         + (uint32_t)(c->not1 + c->and1 + c->or1);
}

CQ_TEST(the_program_is_the_sum_of_its_blocks)
{
    const cq_fmul_row *rows;
    cq_counter fwd, unc;
    fm_census c;
    int n;

    rows = cq_fmul_rows(&n);
    fm_count(rows, n, &c);

    /* THE COMPOSITION IDENTITY. Every `C_*` is ASKED of its module and never
     * written down; the block COUNTS are the port's own content. This is the
     * detector K11's finding says L4 cannot be: `CQOPS_UPDATE_GOLDENS=1` would
     * bless a narrowed multiplier and this line would still go red, because it
     * asks M18 what a 64-bit product costs rather than reading a snapshot. */
    CHECK_EQ(cq_fmul_steps(),  fm_slots(&c));
    CHECK_EQ(cq_fmul_region(), fm_region(&c));

    /* AND THE FOUR PRODUCTS ARE THE SINGLE LARGEST CONTRIBUTION ON BOTH AXES —
     * measured, 65% of the slots and 38% of the region — which is why
     * narrowing them is the attractive mutant. Asserted as a RATIO so the case
     * survives a sibling's cost moving, and with the two axes at DIFFERENT
     * thresholds because they genuinely disagree: M32's four helpers are 27%
     * of the bits and only 14% of the slots. */
    CHECK(4 * cq_mul_steps(W64) > cq_fmul_steps() / 2);
    CHECK(4 * (uint32_t)cq_mul_region(W64) > cq_fmul_region() / 3);

    /* SLOTS AND GATES COME APART, ASSERTED RATHER THAN NARRATED: the
     * sandwiched total is strictly below twice the slot count plus the
     * copy-out, because the views' constant lanes and the constant spans fold.
     * An implementation that had materialised its constants (upstream's
     * `resolve!` shape, PRD-v2 §7.3's refused alternative) would make this an
     * equality. */
    cq_kd_measure(&SPEC, W64, &fwd, &unc);
    CHECK(cq_count_total(&fwd) < 2u * (uint64_t)cq_fmul_steps() + (uint64_t)W64);

    printf("# fmul rows %d (%d views, %d projections)  slots %d  region %u  "
           "forward (x %llu, cx %llu, ccx %llu) = %llu  "
           "unc (x %llu, cx %llu, ccx %llu) = %llu\n",
           n, c.view, c.out, cq_fmul_steps(), cq_fmul_region(),
           (unsigned long long)fwd.x, (unsigned long long)fwd.cx,
           (unsigned long long)fwd.ccx,
           (unsigned long long)cq_count_total(&fwd),
           (unsigned long long)unc.x, (unsigned long long)unc.cx,
           (unsigned long long)unc.ccx,
           (unsigned long long)cq_count_total(&unc));
    fflush(stdout);
}
