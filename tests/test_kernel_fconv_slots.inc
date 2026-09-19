/* tests/test_kernel_fconv_slots.inc — the TABLE-LEVEL claims and the
 * COMPOSITION IDENTITY, plus the lane machinery the four-valued slot scan next
 * door shares. The scan is in test_kernel_fconv_scan.inc and the layout half —
 * span disjointness, the palindrome, two blocks in one region — in
 * test_kernel_fconv_spans.inc. Included by test_kernel_fconv.c. NOTHING BELOW
 * READS A GOLDEN.
 *
 * K11's RECORDED FINDING, APPLIED IN ADVANCE. The one mutant L1 cannot see is
 * the one that looks like an optimisation, and L4 is not a durable detector
 * because `CQOPS_UPDATE_GOLDENS=1` blesses a narrowing as readily as a fix.
 * K19.md §3.0's box names FOUR tempting narrowings, every one of which leaves
 * L1, L2, L3 and L5 green: `exp` is provably 11 bits and is an operand in nine
 * rows; eight more intermediates hold ONE bit in a 64-lane span; `clz` is
 * provably in [0, 63] so the six ladder adds "could" be 6 bits; and
 * `full_mant` is provably 53 bits, so the two barrels "could" be 53-lane — the
 * largest single saving available. PRD-v2 §7.2 refuses all four on the ground
 * D9 refused K12's, and what catches them is here.
 *
 * AND THE IDENTITY IS OVER SLOTS, NOT GATES — D-K18-6. A block operand is a
 * view with constant fill, or a constant span, so the §3 fold table elides
 * gates at EVERY operand mask including the all-quantum one L4 pins.
 * `compute = sum of rows` is true of slots and false of gates.
 */

/* ---- The lane machinery, shared with the scan and the span walk. -------- */

/* §7.3's constant vocabulary, transcribed a SECOND time from fconv.h's own
 * enum comments rather than called out of fconv_step.c. A wrong constant here
 * shows up as a wrong lane profile in the scan, which is the point of writing
 * it again; a wrong constant THERE is a wrong circuit, which L1 sees. */
static uint64_t fv_const(int s)
{
    switch (s) {
    case CQ_FV_K_0:     return 0ull;
    case CQ_FV_K_1:     return 1ull;
    case CQ_FV_K_2:     return 2ull;
    case CQ_FV_K_4:     return 4ull;
    case CQ_FV_K_8:     return 8ull;
    case CQ_FV_K_16:    return 16ull;
    case CQ_FV_K_32:    return 32ull;
    case CQ_FV_K_63:    return 63ull;
    case CQ_FV_K_3FF:   return 0x3ffull;
    case CQ_FV_K_7FF:   return 0x7ffull;
    case CQ_FV_K_1075:  return 1075ull;
    case CQ_FV_K_1086:  return 1086ull;
    case CQ_FV_K_FRAC:  return 0x000fffffffffffffull;
    case CQ_FV_K_ONES:  return ~0ull;
    case CQ_FV_K_HIBIT: return 0x8000000000000000ull;
    default:            return 0x43e0000000000000ull;    /* CQ_FV_K_BIAS */
    }
}

/* The view chain, collapsed OUTERMOST-IN, re-derived from the public row
 * table. `(((base >> s1) & m1) >> s2) & m2 == (base >> (s1+s2)) &
 * ((m1 >> s2) & m2)`, with a NEGATIVE shift meaning a left shift. */
static uint64_t fv_shr(uint64_t v, int s)
{
    if (s >= 64 || s <= -64) return 0ull;
    return (s >= 0) ? (v >> (unsigned)s) : (v << (unsigned)(-s));
}

static int fv_collapse(const cq_fconv_row *rows, int n, int s, int *shift,
                       uint64_t *mask)
{
    int guard = 0;

    *shift = 0;
    *mask  = ~0ull;
    while (s >= 0 && s < n && rows[s].op == CQ_FVOP_VIEW) {
        *mask   = fv_shr(rows[s].mask, *shift) & *mask;
        *shift += rows[s].shift;
        s       = rows[s].s0;
        CHECK(++guard <= n);
    }
    return s;
}

/* The KIND of lane `i` of operand code `s`, at the ALL-QUANTUM operand mask.
 * A rail lane BELOW `a_w` and an emitting row's lane are CQ_BIT_Q (scratch is
 * pre-materialised under I6(b)); a constant lane is its own bit; a view's
 * vacated lane is CQ_BIT_ZERO.
 *
 * `a_w` IS WHAT MAKES `uitofp` A DIFFERENT CIRCUIT FROM `sitofp` AND THE SAME
 * PROGRAM. Lanes [a_w, 64) of the rail are the zext view's constant fill, so
 * they are CQ_BIT_ZERO at EVERY operand mask and every gate they control folds
 * away — which is why the two kernels share a row table and not a gate count.
 * A lane model that read the rail as 64 quantum lanes would over-predict by
 * exactly the fold and the stream cursor would slip, which is the failure
 * bd a-picked-output-can-itself-be-a-view records one module over. */
static int fv_lane(const cq_fconv_row *rows, int n, int a_w, int s, int i)
{
    int shift, j, b;
    uint64_t mask;

    b = fv_collapse(rows, n, s, &shift, &mask);
    if (((mask >> (unsigned)i) & 1ull) == 0ull) return CQ_BIT_ZERO;
    j = i + shift;
    if (j < 0 || j >= CQ_FP64_W) return CQ_BIT_ZERO;
    if (b >= 0) return CQ_BIT_Q;
    if (b == CQ_FV_A) return (j < a_w) ? CQ_BIT_Q : CQ_BIT_ZERO;
    return ((fv_const(b) >> (unsigned)j) & 1ull) ? CQ_BIT_ONE : CQ_BIT_ZERO;
}

/* ---- The per-row cost, RE-DERIVED and ASKED of the owning module. ------- */

/* Not one number is written down: this is the independent copy of
 * fconv_step.c's arithmetic, and the identity below is what
 * `CQOPS_UPDATE_GOLDENS=1` cannot bless. It sits above the table-level claims
 * because three of them measure with it. */
static int fv_row_slots(const cq_fconv_row *r)
{
    switch (r->op) {
    case CQ_FVOP_VIEW:  return 0;
    case CQ_FVOP_EQ:    return cq_eq_steps(CQ_FP64_W);
    case CQ_FVOP_ULT:   return cq_ult_steps(CQ_FP64_W);
    case CQ_FVOP_SUB:   return cq_sub_steps(CQ_FP64_W);
    case CQ_FVOP_ADD:   return cq_add_steps(CQ_FP64_W);
    case CQ_FVOP_MUX:   return cq_mux_steps(CQ_FP64_W);
    case CQ_FVOP_AND:   return CQ_FP64_W;
    case CQ_FVOP_OR:    return 3 * CQ_FP64_W;
    case CQ_FVOP_XOR:   return 2 * CQ_FP64_W;
    case CQ_FVOP_NOT1:  return 2;
    case CQ_FVOP_AND1:  return 1;
    case CQ_FVOP_BSHL:  return cq_barrel_steps(CQ_FP64_W, CQ_BARREL_SHL);
    case CQ_FVOP_BLSHR: return cq_barrel_steps(CQ_FP64_W, CQ_BARREL_LSHR);
    default:            return cq_fsub_steps();
    }
}

static uint32_t fv_row_region(const cq_fconv_row *r)
{
    switch (r->op) {
    case CQ_FVOP_VIEW:  return 0u;
    case CQ_FVOP_EQ:    return (uint32_t)cq_eq_region(CQ_FP64_W);
    case CQ_FVOP_ULT:   return (uint32_t)cq_ult_region(CQ_FP64_W);
    case CQ_FVOP_SUB:   return (uint32_t)cq_sub_region(CQ_FP64_W);
    case CQ_FVOP_ADD:   return (uint32_t)cq_add_region(CQ_FP64_W);
    case CQ_FVOP_MUX:   return (uint32_t)cq_mux_region(CQ_FP64_W);
    case CQ_FVOP_AND: case CQ_FVOP_OR: case CQ_FVOP_XOR:
        return (uint32_t)CQ_FP64_W;
    case CQ_FVOP_BSHL: case CQ_FVOP_BLSHR:
        return (uint32_t)cq_barrel_region(CQ_FP64_W);
    case CQ_FVOP_FSUB:  return cq_fsub_region();
    default:            return 1u;         /* not1, and1: one output bit */
    }
}

typedef struct { int slots; uint32_t region; int n; } cq_fv_probe;

static cq_fv_probe fv_probe(cq_fconv_prog p, cq_fconv_row *rows)
{
    cq_fv_probe t = { 0, 0u, 0 };

    t.n = cq_fconv_program(p, rows);
    for (int i = 0; i < t.n; i++) {
        t.slots  += fv_row_slots(&rows[i]);
        t.region += fv_row_region(&rows[i]);
    }
    return t;
}

/* ---- The table-level claims: the port's own shape. ---------------------- */

/* EVERY ROW REFERENCE IS TO A STRICTLY EARLIER ROW, and for a table assembled
 * by inlining one program into another twice that is the first thing to
 * check: a reference that pointed FORWARD would read a span whose block has
 * not run, which is a plausible gate and a wrong value. It also pins the two
 * placeholder operands `cq_fconv_program` fills in, whose failure mode is a
 * row index of 0 rather than an abort. */
CQ_TEST(the_three_programs_are_well_formed_and_reference_only_backwards)
{
    static const cq_fconv_prog PROGS[] = {
        CQ_FCONV_PROG_FPTOSI, CQ_FCONV_PROG_FPTOUI, CQ_FCONV_PROG_SITOFP
    };
    static const char *const NAMES[] = { "fptosi", "fptoui", "sitofp" };
    static const int WANT_ROWS[] = { 31, 74, 75 };
    static const int WANT_VIEWS[] = { 6, 16, 25 };

    for (int p = 0; p < 3; p++) {
        cq_fconv_row rows[CQ_FCONV_MAX_ROWS];
        int n = cq_fconv_program(PROGS[p], rows);
        int views = 0;

        CHECK_EQ(n, WANT_ROWS[p]);
        for (int i = 0; i < n; i++) {
            short s[3] = { rows[i].s0, rows[i].s1, rows[i].s2 };
            int arity = cq_fconv_arity(rows[i].op);

            if (rows[i].op == CQ_FVOP_VIEW) views++;
            for (int j = 0; j < arity; j++) {
                if (s[j] >= i)
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d (op %d) operand %d names row %d, "
                              "which is not strictly earlier",
                              NAMES[p], i, (int)rows[i].op, j, (int)s[j]);
                if (s[j] < 0 && s[j] < CQ_FV_K_BIAS)
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d operand %d is code %d, outside the "
                              "vocabulary — a placeholder was not filled in",
                              NAMES[p], i, j, (int)s[j]);
            }
            /* A MUX's cond and an AND1's operands must be Bool rows and a
             * 64-lane operand must not be; the library refuses both at run
             * time and this asserts it of the TABLE, where it is a property of
             * the port rather than of a call. */
            if (rows[i].op == CQ_FVOP_MUX || rows[i].op == CQ_FVOP_NOT1
                || rows[i].op == CQ_FVOP_AND1) {
                int nb = (rows[i].op == CQ_FVOP_AND1) ? 2 : 1;

                for (int j = 0; j < nb; j++) {
                    int r = s[j];

                    CHECK(r >= 0 && r < i);
                    CHECK(rows[r].op == CQ_FVOP_EQ || rows[r].op == CQ_FVOP_ULT
                          || rows[r].op == CQ_FVOP_NOT1
                          || rows[r].op == CQ_FVOP_AND1);
                }
            }
        }
        CHECK_EQ(views, WANT_VIEWS[p]);
        CHECK_EQ(cq_fconv_result_row(PROGS[p]), n - 1);
    }
}

/* THE LADDER IS FIVE UNIFORM STAGES AND A SHORT SIXTH, AND THIS IS THE ONLY
 * INSTRUMENT THAT CAN SEE IT (K19.md §2.4's box, §6.5 risk 2). sitofp.jl:49-50
 * stops at `clz` — no `tmp = ifelse(top1_zero, tmp << 1, tmp)` — so a table
 * written as six uniform stages is TWO ROWS LONG and every row after it shifts,
 * with the error landing in :54's `sub` whose operand is `clz`. The exponent
 * then comes out off by a power of two on every magnitude whose highest set bit
 * is at an odd position, which is a plausible answer.
 *
 * WHAT IS ASSERTED IS THE STAGE STRUCTURE ITSELF, re-derived from the row ops
 * rather than from a count: five groups of (VIEW, EQ, NOT1, ADD, MUX, VIEW,
 * MUX) and one of (VIEW, EQ, NOT1, ADD, MUX). The increments are checked
 * against sitofp.jl's own 32/16/8/4/2/1 and the probes against its
 * 32/48/56/60/62/63, because a stage transcribed with the wrong `k` or the
 * wrong `p` is caught by no structural check at all — only by its own CLZ rung
 * anchor. */
CQ_TEST(the_clz_ladder_is_five_uniform_stages_and_a_short_sixth)
{
    static const int INC[] = { CQ_FV_K_32, CQ_FV_K_16, CQ_FV_K_8,
                               CQ_FV_K_4,  CQ_FV_K_2,  CQ_FV_K_1 };
    static const int PROBE[] = { 32, 48, 56, 60, 62, 63 };
    static const int SHIFT[] = { 32, 16, 8, 4, 2 };
    cq_fconv_row rows[CQ_FCONV_MAX_ROWS];
    int n = cq_fconv_program(CQ_FCONV_PROG_SITOFP, rows);
    int at = 10;                       /* the ladder starts after `tmp` */

    CHECK_EQ(n, 75);
    CHECK(rows[9].op == CQ_FVOP_VIEW);              /* :27  tmp = magnitude */

    for (int st = 0; st < 6; st++) {
        int last = (st == 5);

        CHECK(rows[at + 0].op == CQ_FVOP_VIEW);     /* tmp >> p             */
        CHECK_EQ((int)rows[at + 0].shift, PROBE[st]);
        CHECK(rows[at + 1].op == CQ_FVOP_EQ);       /* == 0, raw is `!=`    */
        CHECK_EQ((int)rows[at + 1].s1, CQ_FV_K_0);
        CHECK(rows[at + 2].op == CQ_FVOP_NOT1);     /* topN_zero            */
        CHECK(rows[at + 3].op == CQ_FVOP_ADD);      /* clz + k              */
        CHECK_EQ((int)rows[at + 3].s1, INC[st]);
        CHECK(rows[at + 4].op == CQ_FVOP_MUX);      /* clz                  */
        CHECK_EQ((int)rows[at + 4].s0, at + 2);
        CHECK_EQ((int)rows[at + 4].s1, at + 3);     /* t = the INCREMENTED  */
        CHECK_EQ((int)rows[at + 4].s2, rows[at + 3].s0);  /* f = the PRE-add */

        if (last) { at += 5; break; }

        CHECK(rows[at + 5].op == CQ_FVOP_VIEW);     /* tmp << k             */
        CHECK_EQ((int)rows[at + 5].shift, -SHIFT[st]);
        CHECK(rows[at + 6].op == CQ_FVOP_MUX);      /* tmp                  */
        CHECK_EQ((int)rows[at + 6].s0, at + 2);
        at += 7;
    }

    /* Stage 6 is FOUR rows shorter than a uniform one, so the ladder ends at
     * 10 + 5*7 + 5 = 50 and `:54`'s `sub` is the very next row, reading the
     * ladder's LAST mux. Both are what a `u / stage_len` decode gets wrong. */
    CHECK_EQ(at, 50);
    CHECK(rows[50].op == CQ_FVOP_SUB);
    CHECK_EQ((int)rows[50].s0, CQ_FV_K_1086);
    CHECK_EQ((int)rows[50].s1, 49);

    /* NOTHING READS `tmp` AFTER STAGE 5, which is why stage 6 has no `tmp`
     * update to transcribe. Asserted rather than asserted-by-comment: row 44
     * is stage 5's `tmp` mux and the only later reference to it is stage 6's
     * own probe view. */
    for (int i = 46; i < n; i++) {
        short s[3] = { rows[i].s0, rows[i].s1, rows[i].s2 };

        for (int j = 0; j < cq_fconv_arity(rows[i].op); j++)
            if (s[j] == 44)
                cq_h_fail(__FILE__, __LINE__,
                          "row %d reads the ladder's last `tmp` (row 44); "
                          "sitofp.jl:49-50 is the last stage and nothing after "
                          "it uses `tmp`", i);
    }
}

/* fptoui.jl:40 AND :46 ARE TWO SEPARATE CALLS, AND THE PROGRAM SAYS SO.
 * K19.md §2.3's box: "U8's and U10's scratch must be DISJOINT ... A port that
 * gives them one `off` is a silent miscompile whose forward value is right on
 * the `in_high_range` branch only — the branch that a 32-sample draw of
 * ordinary doubles reaches essentially never." Inlining makes them disjoint by
 * construction; this asserts that the inlining happened AT ALL and that the
 * second copy reads the fsub row rather than the rail, which is the one rebase
 * in the module and the difference between `fptosi(adjusted)` and
 * `fptosi(a)`. */
CQ_TEST(fptoui_inlines_fptosi_twice_at_two_disjoint_offsets)
{
    cq_fconv_row ui[CQ_FCONV_MAX_ROWS], si[CQ_FCONV_MAX_ROWS];
    int n_ui = cq_fconv_program(CQ_FCONV_PROG_FPTOUI, ui);
    int n_si = cq_fconv_program(CQ_FCONV_PROG_FPTOSI, si);
    int own = 0, base[2], k = 0;

    (void)cq_fptoui_own_rows(&own);
    CHECK_EQ(n_ui, own + 2 * n_si);

    /* The two copies begin at the two places the own-table's call sites are:
     * the first straight after the select flag, the second straight after the
     * FSUB row. Found by op signature rather than by a written index. */
    for (int i = 0; i < n_ui; i++)
        if (ui[i].op == CQ_FVOP_FSUB) {
            CHECK_EQ(k, 1);                    /* one copy already placed  */
            base[1] = i + 1;
            k = 2;
        } else if (k == 0 && ui[i].op == CQ_FVOP_AND1) {
            base[0] = i + 1;
            k = 1;
        }
    CHECK_EQ(k, 2);
    CHECK_EQ(base[0], 9);
    CHECK_EQ(base[1], 41);

    /* Each copy is the fptosi table, row for row, with references shifted by
     * its own base — and the SECOND copy's `CQ_FV_A` rebased onto the fsub
     * row. Anything else is either a shared instance or a wrong rebase. */
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < n_si; i++) {
            const cq_fconv_row *a = &si[i], *b = &ui[base[c] + i];
            short sa[3] = { a->s0, a->s1, a->s2 };
            short sb[3] = { b->s0, b->s1, b->s2 };

            CHECK_EQ((int)b->op, (int)a->op);
            CHECK_EQ((int)b->shift, (int)a->shift);
            CHECK_EQ(b->mask, a->mask);
            for (int j = 0; j < cq_fconv_arity(a->op); j++) {
                if (sa[j] >= 0)               CHECK_EQ(sb[j], sa[j] + base[c]);
                else if (sa[j] == CQ_FV_A)    CHECK_EQ((int)sb[j],
                                                       c == 0 ? CQ_FV_A
                                                              : base[1] - 1);
                else                          CHECK_EQ(sb[j], sa[j]);
            }
        }

    /* AND THE TAIL READS BOTH COPIES, which is what makes them two instances
     * rather than one computed twice and thrown away. */
    CHECK(ui[n_ui - 2].op == CQ_FVOP_OR);
    CHECK_EQ((int)ui[n_ui - 2].s1, base[1] + n_si - 1);     /* path_b's conv */
    CHECK(ui[n_ui - 1].op == CQ_FVOP_MUX);
    CHECK_EQ((int)ui[n_ui - 1].s1, n_ui - 2);               /* t = path_b    */
    CHECK_EQ((int)ui[n_ui - 1].s2, base[0] + n_si - 1);     /* f = path_a    */
}

/* `uitofp` IS `sitofp`'s PROGRAM, BYTE FOR BYTE, AND THE ZEXT IS AT THE
 * OPERAND (instructions.jl:7666-7682). This is the case the task asks for
 * directly: same rows, same slots, same region at every shipped source width,
 * with only the view differing — so a port that grew a second table, or that
 * widened the source with a real `zext` block, is red here even though L1
 * would stay green. */
CQ_TEST(uitofps_row_program_is_sitofps_byte_for_byte_at_every_width)
{
    cq_fconv_row rows[CQ_FCONV_MAX_ROWS];
    int n = cq_fconv_program(CQ_FCONV_PROG_SITOFP, rows);
    const int *ws;
    int nw, slots0 = 0;
    uint32_t region0 = 0u;

    for (int i = 0; i < n; i++) {
        slots0  += fv_row_slots(&rows[i]);
        region0 += fv_row_region(&rows[i]);
    }

    ws = cq_fconv_uitofp_widths(&nw);
    for (int w = 0; w < nw; w++) {
        cq_counter fwd, unc;
        uint32_t peak = 0u;

        fconv_arm(&ROWS[FV_UITOFP], ws[w]);
        CHECK_EQ(cq_kd_peak(&ROWS[FV_UITOFP].spec, ws[w], &peak),
                 (uint32_t)CQ_FP64_W);
        CHECK_EQ(peak, region0 + (uint32_t)CQ_FP64_W);

        /* THE GATE COUNTS DIFFER AND THE SLOT COUNT DOES NOT, which is the
         * whole of "slots and gates come apart" in one comparison: a narrower
         * source puts MORE constant lanes inside the operand spans, so a
         * narrower width emits STRICTLY FEWER gates on the same program. */
        cq_kd_measure(&ROWS[FV_UITOFP].spec, ws[w], &fwd, &unc);
        if (w > 0) {
            cq_counter prev_f, prev_u;

            fconv_arm(&ROWS[FV_UITOFP], ws[w - 1]);
            cq_kd_measure(&ROWS[FV_UITOFP].spec, ws[w - 1], &prev_f, &prev_u);
            CHECK(cq_count_total(&prev_f) < cq_count_total(&fwd));
            CHECK(cq_count_total(&prev_u) < cq_count_total(&unc));
        }
    }
    CHECK(slots0 > 0);
}

/* PRD-v2 §5's seam, ASSERTED. K19.md §2.10 claims zero named spans cross
 * INT->FP <-> FP->INT; here that is the statement that the two row tables
 * share no row and that neither reaches the other's vocabulary — `fptosi` has
 * both barrel directions and the saturation constant, `sitofp` has one barrel
 * and the CLZ increments, and neither has the other's. A future edit that
 * "shared" the field extraction between them would be the first thing to
 * break the seam, and it would be invisible to every count. */
CQ_TEST(no_named_span_crosses_the_int_to_fp_seam)
{
    cq_fconv_row si[CQ_FCONV_MAX_ROWS], fp[CQ_FCONV_MAX_ROWS];
    int n_si = cq_fconv_program(CQ_FCONV_PROG_FPTOSI, si);
    int n_fp = cq_fconv_program(CQ_FCONV_PROG_SITOFP, fp);
    int si_shl = 0, si_lshr = 0, fp_shl = 0, fp_lshr = 0;
    int si_hibit = 0, fp_hibit = 0, si_inc = 0, fp_inc = 0;

    for (int i = 0; i < n_si; i++) {
        short s[3] = { si[i].s0, si[i].s1, si[i].s2 };

        if (si[i].op == CQ_FVOP_BSHL)  si_shl++;
        if (si[i].op == CQ_FVOP_BLSHR) si_lshr++;
        for (int j = 0; j < cq_fconv_arity(si[i].op); j++) {
            if (s[j] == CQ_FV_K_HIBIT) si_hibit++;
            if (s[j] == CQ_FV_K_32 || s[j] == CQ_FV_K_16 || s[j] == CQ_FV_K_8
                || s[j] == CQ_FV_K_4 || s[j] == CQ_FV_K_2) si_inc++;
        }
    }
    for (int i = 0; i < n_fp; i++) {
        short s[3] = { fp[i].s0, fp[i].s1, fp[i].s2 };

        if (fp[i].op == CQ_FVOP_BSHL)  fp_shl++;
        if (fp[i].op == CQ_FVOP_BLSHR) fp_lshr++;
        for (int j = 0; j < cq_fconv_arity(fp[i].op); j++) {
            if (s[j] == CQ_FV_K_HIBIT) fp_hibit++;
            if (s[j] == CQ_FV_K_32 || s[j] == CQ_FV_K_16 || s[j] == CQ_FV_K_8
                || s[j] == CQ_FV_K_4 || s[j] == CQ_FV_K_2) fp_inc++;
        }
    }
    CHECK_EQ(si_shl, 1);  CHECK_EQ(si_lshr, 1);   /* fptosi: both directions */
    CHECK_EQ(fp_shl, 1);  CHECK_EQ(fp_lshr, 0);   /* sitofp: shl only        */
    CHECK_EQ(si_hibit, 1);                        /* the saturation constant */
    CHECK_EQ(fp_hibit, 0);                        /* and it is not over here */
    CHECK_EQ(si_inc, 0);                          /* the CLZ increments are  */
    CHECK_EQ(fp_inc, 5);                          /* only on the int->fp side */

    /* NEITHER HALF REACHES M33. `fsub` is fptoui's alone, which is why K19 has
     * one cross-module hand-off and not two. */
    for (int i = 0; i < n_si; i++) CHECK(si[i].op != CQ_FVOP_FSUB);
    for (int i = 0; i < n_fp; i++) CHECK(fp[i].op != CQ_FVOP_FSUB);
}

/* ---- (a1) The block costs, ASKED of their modules. Runs no kernel. ------ */

/* TWO CASES DELIBERATELY, so a sibling's cost moving reddens the FIRST and
 * NAMES the block rather than reddening the per-program identity with a number
 * nobody can attribute (K12.md §3.0's shape). This is the one that runs no
 * kernel. */
CQ_TEST(the_blocks_cost_what_their_modules_say_they_cost)
{
    /* ASKED of M12, M14, M16, M17 and M33 and pinned against their own
     * measured W = 64 rows. K19 composes all five, so if one moves everything
     * below moves with it and this line says so first. */
    CHECK_EQ(cq_eq_steps(CQ_FP64_W),  317);          /* lower_eq!   5W-3   */
    CHECK_EQ(cq_ult_steps(CQ_FP64_W), 385);          /* lower_ult!  6W+1   */
    CHECK_EQ(cq_sub_steps(CQ_FP64_W), 447);          /* lower_sub!  7W-1   */
    CHECK_EQ(cq_add_steps(CQ_FP64_W), 318);          /* lower_add!  5W-2   */
    CHECK_EQ(cq_mux_steps(CQ_FP64_W), 256);          /* lower_mux!  4W     */
    CHECK_EQ(cq_barrel_steps(CQ_FP64_W, CQ_BARREL_SHL),  1921);
    CHECK_EQ(cq_barrel_steps(CQ_FP64_W, CQ_BARREL_LSHR), 1921);
    CHECK_EQ(cq_fsub_steps(), 48836);                /* M33, K15           */

    CHECK_EQ(cq_eq_region(CQ_FP64_W),  127);
    CHECK_EQ(cq_ult_region(CQ_FP64_W), 193);
    CHECK_EQ(cq_sub_region(CQ_FP64_W), 192);
    CHECK_EQ(cq_add_region(CQ_FP64_W), 128);
    CHECK_EQ(cq_mux_region(CQ_FP64_W), 128);
    CHECK_EQ(cq_barrel_region(CQ_FP64_W), 1216);
    CHECK_EQ(cq_fsub_region(), 24033u);

    /* THERE IS NO SIGNED COMPARE ANYWHERE IN K19 AND THE COST GAP IS WHAT
     * WOULD SEE ONE (K19.md §5.3). All five ordering compares are on a
     * `UInt64`; substituting `cq_slt_block` at any of them changes no value on
     * any input — each wrapped amount occurs only on the arm fptosi.jl:53
     * discards — so L1 cannot see it and this gap, through the composition
     * identity below, is what can. */
    CHECK(cq_slt_steps(CQ_FP64_W) > cq_ult_steps(CQ_FP64_W));
    CHECK(cq_slt_region(CQ_FP64_W) > cq_ult_region(CQ_FP64_W));

    /* AND THE WIDTH IS 64, WHICH IS THE WHOLE OF THE NARROWING GUARD. `exp` is
     * 11 bits, `clz` is provably in [0, 63] and `full_mant` is 53; PRD-v2 §7.2
     * forbids narrowing an intermediate towards any of them, so every block
     * runs at the Julia type's width. A port that narrowed would show up below
     * as a program no longer being the sum of its rows at 64. */
    CHECK(cq_eq_steps(CQ_FP64_EXP_W) < cq_eq_steps(CQ_FP64_W));
    CHECK(cq_add_steps(6)            < cq_add_steps(CQ_FP64_W));
    CHECK(cq_barrel_steps(53, CQ_BARREL_SHL)
          < cq_barrel_steps(CQ_FP64_W, CQ_BARREL_SHL));
}

/* ---- (a2) Each program against what those blocks just cost. ------------- */

CQ_TEST(each_program_is_the_sum_of_its_rows)
{
    cq_fconv_row si[CQ_FCONV_MAX_ROWS], ui[CQ_FCONV_MAX_ROWS];
    cq_fconv_row fp[CQ_FCONV_MAX_ROWS];
    cq_fv_probe psi = fv_probe(CQ_FCONV_PROG_FPTOSI, si);
    cq_fv_probe pui = fv_probe(CQ_FCONV_PROG_FPTOUI, ui);
    cq_fv_probe pfp = fv_probe(CQ_FCONV_PROG_SITOFP, fp);

    /* THE COMPOSITION IDENTITY, against the ONE count M37 exports. Every `C_*`
     * is ASKED of its module and never written down; the row tables are the
     * port's own content and the thing a reader checks against the Julia. */
    CHECK_EQ(cq_fptosi_steps(),  psi.slots);
    CHECK_EQ(cq_fptosi_region(), psi.region);

    /* K19.md §3.4's THIRD case, and the one that would go red if the two
     * inlined fptosi instances were collapsed into one: fptoui is TWICE
     * fptosi, plus M33's whole fsub, plus its own eight emitting rows. The
     * `own` term is derived from the table rather than written, so adding or
     * removing one of fptoui's own rows reddens this line and names it. */
    {
        int own = 0, own_slots = 0;
        uint32_t own_region = 0u;
        const cq_fconv_row *o = cq_fptoui_own_rows(&own);

        for (int i = 0; i < own; i++) {
            if (o[i].op == CQ_FVOP_FSUB) continue;    /* counted separately */
            own_slots  += fv_row_slots(&o[i]);
            own_region += fv_row_region(&o[i]);
        }
        CHECK_EQ(pui.slots,
                 2 * psi.slots + cq_fsub_steps() + own_slots);
        CHECK_EQ(pui.region,
                 2u * psi.region + cq_fsub_region() + own_region);
        CHECK(own_slots > 0 && own_region > 0u);
    }

    /* SLOTS AND GATES COME APART, ASSERTED RATHER THAN NARRATED: the
     * sandwiched total is strictly below twice the slot count plus the
     * copy-out, because the views' constant lanes and the constant spans fold.
     * An implementation that had materialised its constants (upstream's
     * `resolve!` shape, §7.3's refused alternative) would make this an
     * equality. */
    for (int i = 0; i < N_FCONV_ROWS; i++) {
        cq_counter fwd, unc;
        int F = (i == FV_UITOFP) ? 32 : CQ_FP64_W;
        cq_fv_probe t = (i == 0) ? psi : (i == 1) ? pui : pfp;

        fconv_arm(&ROWS[i], F);
        cq_kd_measure(&ROWS[i].spec, F, &fwd, &unc);
        CHECK(cq_count_total(&fwd)
              < 2u * (uint64_t)t.slots + (uint64_t)CQ_FP64_W);

        printf("# %-6s F %2d  rows %3d  slots %6d  region %6u bits  forward "
               "(x %llu, cx %llu, ccx %llu) = %llu  unc = %llu\n",
               ROWS[i].spec.name, F, t.n, t.slots, t.region,
               (unsigned long long)fwd.x, (unsigned long long)fwd.cx,
               (unsigned long long)fwd.ccx,
               (unsigned long long)cq_count_total(&fwd),
               (unsigned long long)cq_count_total(&unc));
    }
    fflush(stdout);
}

/* ---- dst owns 64 qubits and the scratch comes back. -------------------- */

CQ_TEST(dst_owns_its_lanes_and_the_scratch_comes_back)
{
    for (int i = 0; i < N_FCONV_ROWS; i++) {
        cq_fconv_row rows[CQ_FCONV_MAX_ROWS];
        cq_fv_probe t = fv_probe(ROWS[i].prog, rows);
        int F = (i == FV_UITOFP) ? 16 : CQ_FP64_W;
        uint32_t peak = 0u, owned;

        fconv_arm(&ROWS[i], F);
        owned = cq_kd_peak(&ROWS[i].spec, F, &peak);
        CHECK_EQ(owned, (uint32_t)CQ_FP64_W);
        /* The whole program's region plus dst's 64 lanes. L2 looks after the
         * call and cannot see scratch that was taken and tidily released, so
         * the high-water mark is the only instrument for it. */
        CHECK_EQ(peak, t.region + (uint32_t)CQ_FP64_W);
    }
}
