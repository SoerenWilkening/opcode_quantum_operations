/* tests/test_kernel_fround_slots.inc — the PORT's census per program, the
 * block costs, the composition identity, and the trunc-body inlining
 * identity. Included by test_kernel_fround.c. NOTHING BELOW READS A GOLDEN.
 *
 * K11's RECORDED FINDING, APPLIED IN ADVANCE: the one mutant L1 cannot see is
 * the one that looks like an optimisation — here, narrowing the 64-lane
 * `sub`/`slt`/barrel on `exp` and `frac_bits` (provably < 2^11) towards their
 * value's width, or dropping a DISCARDED arm. `CQOPS_UPDATE_GOLDENS=1` would
 * bless either; the identity below asks each module what a 64-bit block costs
 * and would not.
 *
 * AND THE IDENTITY IS OVER SLOTS, NOT GATES (D-K18-6).
 */

/* ---- (0) The census: the table IS the source, counted a second time. ----- */

/* HAND-COUNTED OFF fround.jl, NOT OFF src/kernels/fround_rows.inc:
 *
 *   soft_trunc  :17 :18 x2 :27 views; `==` :26; `!=` :27; `&` Bool :27;
 *               `|` :28; ifelse :28 :37 x2 :45-47 x3; `<` :30 and :37 x1,
 *               `>` :37 (slt + not1 each); `>=` :32 (slt); `-` :35 :39;
 *               `<<` :39; `~` :39; `&` :42.
 *   soft_floor  + :59 view, :60 `!=`, :65 `!=`, :66 `&` Bool, :73 soft_fadd,
 *               :74 ifelse.
 *   soft_ceil   + :84 view, :85 `==` (eq + not1), :89 `!=`, :90 `&` Bool,
 *               :93 soft_fadd, :94 ifelse.
 *   soft_round  see K25.md §2.3; 14 views, 5 `|` UInt64, 2 `|` Bool.
 *   soft_round_away  see K25.md §2.3; 11 views, no `|` Bool.
 */
typedef struct {
    int view, eq, slt, sub, add, mux, orr, and_, xor_, bshl, blshr, not1,
        and1, or1, fadd;
} fr_census;

static const fr_census FR_SRC[CQ_FROUND_N_PROG] = {
    /*        view eq slt sub add mux or and xor shl shr not1 and1 or1 fadd */
    /* trunc */ { 4,  2, 4, 2, 0,  6, 1, 1, 1, 1, 0, 4, 1, 0, 0 },
    /* floor */ { 5,  4, 4, 2, 0,  7, 1, 1, 1, 1, 0, 4, 2, 0, 1 },
    /* ceil  */ { 5,  4, 4, 2, 0,  7, 1, 1, 1, 1, 0, 5, 2, 0, 1 },
    /* even  */ { 14, 10, 4, 4, 2, 10, 5, 2, 1, 3, 2, 9, 4, 2, 0 },
    /* away  */ { 11, 5, 4, 3, 2, 10, 5, 1, 1, 2, 1, 6, 1, 0, 0 }
};

static const int FR_NROWS[CQ_FROUND_N_PROG] = { 27, 33, 34, 72, 52 };

static void fr_count(const cq_fround_row *rows, int n, fr_census *c)
{
    memset(c, 0, sizeof *c);
    for (int i = 0; i < n; i++)
        switch (rows[i].op) {
        case CQ_FIOP_VIEW:  c->view++;  break;
        case CQ_FIOP_EQ:    c->eq++;    break;
        case CQ_FIOP_SLT:   c->slt++;   break;
        case CQ_FIOP_SUB:   c->sub++;   break;
        case CQ_FIOP_ADD:   c->add++;   break;
        case CQ_FIOP_MUX:   c->mux++;   break;
        case CQ_FIOP_OR:    c->orr++;   break;
        case CQ_FIOP_AND:   c->and_++;  break;
        case CQ_FIOP_XOR:   c->xor_++;  break;
        case CQ_FIOP_BSHL:  c->bshl++;  break;
        case CQ_FIOP_BLSHR: c->blshr++; break;
        case CQ_FIOP_NOT1:  c->not1++;  break;
        case CQ_FIOP_AND1:  c->and1++;  break;
        case CQ_FIOP_OR1:   c->or1++;   break;
        case CQ_FIOP_FADD:  c->fadd++;  break;
        default: cq_h_fail(__FILE__, __LINE__, "row %d: op %d", i, rows[i].op);
        }
}

/* WHAT A ROW COSTS, ASKED OF THE OWNING MODULES BY THIS FILE and not read out
 * of fround_step.c. `lower_or!` is three gates and one bit per lane, `lower_xor!`
 * two per lane, `lower_and!` one, `lower_not1!` two gates and one bit
 * (arith.jl:268-291, :474-478) — transcribed from the pinned source because no
 * module publishes a cost for them. M33's `soft_fadd` is asked of M33's map. */
static int fr_ref_steps(const cq_fround_row *r)
{
    switch (r->op) {
    case CQ_FIOP_VIEW:  return 0;
    case CQ_FIOP_EQ:    return cq_eq_steps(W64);
    case CQ_FIOP_SLT:   return cq_slt_steps(W64);
    case CQ_FIOP_SUB:   return cq_sub_steps(W64);
    case CQ_FIOP_ADD:   return cq_add_steps(W64);
    case CQ_FIOP_MUX:   return cq_mux_steps(W64);
    case CQ_FIOP_OR:    return 3 * W64;
    case CQ_FIOP_AND:   return W64;
    case CQ_FIOP_XOR:   return 2 * W64;
    case CQ_FIOP_BSHL:  return cq_barrel_steps(W64, CQ_BARREL_SHL);
    case CQ_FIOP_BLSHR: return cq_barrel_steps(W64, CQ_BARREL_LSHR);
    case CQ_FIOP_NOT1:  return 2;
    case CQ_FIOP_AND1:  return 1;
    case CQ_FIOP_OR1:   return 3;
    default:            return cq_fadd_steps();
    }
}

static uint32_t fr_ref_region(const cq_fround_row *r)
{
    switch (r->op) {
    case CQ_FIOP_VIEW:  return 0u;
    case CQ_FIOP_EQ:    return (uint32_t)cq_eq_region(W64);
    case CQ_FIOP_SLT:   return (uint32_t)cq_slt_region(W64);
    case CQ_FIOP_SUB:   return (uint32_t)cq_sub_region(W64);
    case CQ_FIOP_ADD:   return (uint32_t)cq_add_region(W64);
    case CQ_FIOP_MUX:   return (uint32_t)cq_mux_region(W64);
    case CQ_FIOP_OR: case CQ_FIOP_AND: case CQ_FIOP_XOR: return (uint32_t)W64;
    case CQ_FIOP_BSHL: case CQ_FIOP_BLSHR:
        return (uint32_t)cq_barrel_region(W64);
    case CQ_FIOP_NOT1: case CQ_FIOP_AND1: case CQ_FIOP_OR1: return 1u;
    default:            return cq_fadd_region();
    }
}

CQ_TEST(the_programs_are_the_source_line_for_line)
{
    for (int p = 0; p < CQ_FROUND_N_PROG; p++) {
        const cq_fround_row *rows;
        const fr_census *w = &FR_SRC[p];
        fr_census c;
        int n;

        rows = cq_fround_rows((cq_fround_prog)p, &n);
        fr_count(rows, n, &c);
        CHECK_EQ(n, FR_NROWS[p]);
        CHECK_EQ(c.view, w->view);   CHECK_EQ(c.eq, w->eq);
        CHECK_EQ(c.slt, w->slt);     CHECK_EQ(c.sub, w->sub);
        CHECK_EQ(c.add, w->add);     CHECK_EQ(c.mux, w->mux);
        CHECK_EQ(c.orr, w->orr);     CHECK_EQ(c.and_, w->and_);
        CHECK_EQ(c.xor_, w->xor_);   CHECK_EQ(c.bshl, w->bshl);
        CHECK_EQ(c.blshr, w->blshr); CHECK_EQ(c.not1, w->not1);
        CHECK_EQ(c.and1, w->and1);   CHECK_EQ(c.or1, w->or1);
        CHECK_EQ(c.fadd, w->fadd);

        /* WELL-FORMEDNESS: every row reference is STRICTLY EARLIER, every
         * negative code is one of the thirteen, a 1-bit operator reads 1-bit
         * rows and a mux's condition is a 1-bit row. */
        for (int i = 0; i < n; i++) {
            int ar = cq_fround_arity(rows[i].op), s[3];

            s[0] = rows[i].s0; s[1] = rows[i].s1; s[2] = rows[i].s2;
            for (int j = 0; j < ar; j++) {
                if (s[j] >= i)
                    cq_h_fail(__FILE__, __LINE__, "prog %d row %d operand %d "
                              "names row %d, not strictly earlier", p, i, j,
                              s[j]);
                if (s[j] < -CQ_FI_N_CODE)
                    cq_h_fail(__FILE__, __LINE__, "prog %d row %d operand %d "
                              "is the unknown code %d", p, i, j, s[j]);
            }
            if (rows[i].op == CQ_FIOP_NOT1 || rows[i].op == CQ_FIOP_AND1
                || rows[i].op == CQ_FIOP_OR1)
                for (int j = 0; j < ar; j++)
                    CHECK_EQ(cq_fround_row_width(rows, n, s[j]), 1);
            if (rows[i].op == CQ_FIOP_MUX)
                CHECK_EQ(cq_fround_row_width(rows, n, s[0]), 1);
            if (rows[i].op == CQ_FIOP_VIEW) {
                CHECK(rows[i].shift > -64 && rows[i].shift < 64);
                CHECK(rows[i].mask != 0u);
            }
        }
        /* The LAST row is the result and is a MUX's own span — never a view
         * whose assembled buffer would die with the call that built it. */
        CHECK_EQ(rows[n - 1].op, CQ_FIOP_MUX);
    }

    /* NINE VARIABLE SHIFTS AND NO MORE (PRD-v2 §7.6's fround.jl row): floor
     * and ceil inherit trunc's one and add none. */
    CHECK_EQ(FR_SRC[CQ_FROUND_TRUNC].bshl + FR_SRC[CQ_FROUND_EVEN].bshl
             + FR_SRC[CQ_FROUND_EVEN].blshr + FR_SRC[CQ_FROUND_AWAY].bshl
             + FR_SRC[CQ_FROUND_AWAY].blshr, 9);
}

/* ---- (0b) The three table-reading guards are unreachable, EXECUTED. ----- */

/* THREE GUARDS READ THIS MODULE'S OWN TABLES and so cannot have a death case
 * (bd a-guard-that-reads-its-own-table-has-no-death-case): "a view chain does
 * not terminate", "the step dispatch landed on a zero-slot row" and
 * "`result` is not a mux's own span". What replaces each is the reachability
 * claim itself: a table edit that made one reachable reddens THIS case. */
CQ_TEST(the_table_reading_guards_are_unreachable)
{
    int deepest = 0;

    for (int p = 0; p < CQ_FROUND_N_PROG; p++) {
        const cq_fround_row *rows;
        const cq_fi_map *m;
        cq_fround_block k;
        cq_scratch scr;
        const cq_bit *res;
        int n;

        rows = cq_fround_rows((cq_fround_prog)p, &n);
        m = cq_fi_map_get((cq_fround_prog)p);
        for (int i = 0; i < n; i++) {
            /* A row owns slots IF AND ONLY IF it is not a view — stated as the
             * complement, not a second transcription of the cost switch. */
            CHECK_EQ(m->slot[i + 1] > m->slot[i], rows[i].op != CQ_FIOP_VIEW);
            if (rows[i].op == CQ_FIOP_VIEW) {
                int s = i, depth = 0, shift;
                uint64_t mask;

                while (s >= 0 && rows[s].op == CQ_FIOP_VIEW && depth <= n) {
                    s = rows[s].s0;
                    depth++;
                }
                CHECK(depth <= n);
                CHECK_EQ(cq_fi_view_collapse((cq_fround_prog)p, i, &shift,
                                             &mask), s);
                if (depth > deepest) deepest = depth;
            }
        }
        /* `result` resolves to a span INSIDE the caller's region. */
        cq_scratch_alloc(&scr, cq_fround_region((cq_fround_prog)p));
        k.a = NULL; k.scr = &scr; k.off = 0u; k.prog = (cq_fround_prog)p;
        res = cq_fround_result(&k);
        CHECK(res >= scr.bits && res + W64 <= scr.bits + scr.n);
        cq_scratch_dispose(&scr);
    }
    /* Not vacuous: at least one chain nests (`(abs_a >> 52) & 0x7ff`). */
    CHECK(deepest >= 3);
}

/* ---- (1) floor and ceil INLINE soft_trunc's body at a named base. ------- */

/* THE COMPOSITE IS THE OTHER BODY'S ROW PROGRAM, NEVER A NESTED KERNEL, and
 * this is the case that says so: rows [base, base + 27) of floor and ceil are
 * soft_trunc's table with every internal reference shifted by `base`, and the
 * rows after the body read its LAST row as `truncated`. A body inlined at the
 * wrong base, or a floor reading the trunc body's `sign` view instead of its
 * result, keeps every count right. */
CQ_TEST(floor_and_ceil_inline_the_trunc_body_row_for_row)
{
    const cq_fround_row *tr;
    int nt;

    tr = cq_fround_rows(CQ_FROUND_TRUNC, &nt);
    CHECK_EQ(nt, CQ_FROUND_TRUNC_ROWS);
    CHECK_EQ(cq_fround_trunc_base(CQ_FROUND_TRUNC), 0);
    CHECK_EQ(cq_fround_trunc_base(CQ_FROUND_EVEN), -1);
    CHECK_EQ(cq_fround_trunc_base(CQ_FROUND_AWAY), -1);

    for (int p = CQ_FROUND_FLOOR; p <= CQ_FROUND_CEIL; p++) {
        const cq_fround_row *rows;
        int n, b = cq_fround_trunc_base((cq_fround_prog)p);
        int tres, fadd = -1;

        rows = cq_fround_rows((cq_fround_prog)p, &n);
        CHECK(b > 0 && b + nt < n);
        for (int i = 0; i < nt; i++) {
            const cq_fround_row *x = &tr[i], *y = &rows[b + i];
            int ar = cq_fround_arity(x->op), s[3], t[3];

            CHECK_EQ(y->op, x->op);
            CHECK_EQ(y->shift, x->shift);
            CHECK_EQ((long long)y->mask, (long long)x->mask);
            s[0] = x->s0; s[1] = x->s1; s[2] = x->s2;
            t[0] = y->s0; t[1] = y->s1; t[2] = y->s2;
            for (int j = 0; j < ar; j++)
                CHECK_EQ(t[j], s[j] >= 0 ? s[j] + b : s[j]);
        }
        tres = b + nt - 1;
        for (int i = b + nt; i < n; i++)
            if (rows[i].op == CQ_FIOP_FADD) fadd = i;
        CHECK(fadd > 0);
        /* :73 / :93 — `soft_fadd(truncated, ±one)`, the constant second. */
        CHECK_EQ(rows[fadd].s0, tres);
        CHECK_EQ(rows[fadd].s1, p == CQ_FROUND_FLOOR ? CQ_FI_K_NEG_ONE_F
                                                     : CQ_FI_K_ONE_F);
        /* :74 / :94 — `ifelse(need, fadd, truncated)`: the fadd arm TRUE. */
        CHECK_EQ(rows[n - 1].s1, fadd);
        CHECK_EQ(rows[n - 1].s2, tres);
        /* :65 / :89 — `has_frac = truncated != a`. */
        CHECK_EQ(rows[b + nt].op, CQ_FIOP_EQ);
        CHECK_EQ(rows[b + nt].s0, tres);
        CHECK_EQ(rows[b + nt].s1, CQ_FI_A);
    }
}

/* ---- (2) The block costs, ASKED of their modules. Runs no kernel. ------- */

CQ_TEST(the_blocks_cost_what_their_modules_say_they_cost)
{
    CHECK_EQ(cq_eq_steps(W64),  317);          /* lower_eq!   5W-3        */
    CHECK_EQ(cq_slt_steps(W64), 515);          /* lower_slt!  2W+2+ult    */
    CHECK_EQ(cq_sub_steps(W64), 447);          /* lower_sub!  7W-1        */
    CHECK_EQ(cq_add_steps(W64), 318);          /* lower_add!  5W-2        */
    CHECK_EQ(cq_mux_steps(W64), 256);          /* lower_mux!  4W          */
    CHECK_EQ(cq_barrel_steps(W64, CQ_BARREL_SHL),  1921);
    CHECK_EQ(cq_barrel_steps(W64, CQ_BARREL_LSHR), 1921);
    CHECK_EQ(cq_eq_region(W64),  127);
    CHECK_EQ(cq_slt_region(W64), 321);
    CHECK_EQ(cq_sub_region(W64), 192);
    CHECK_EQ(cq_add_region(W64), 128);
    CHECK_EQ(cq_mux_region(W64), 128);
    CHECK_EQ(cq_barrel_region(W64), 1216);
    CHECK_EQ(cq_fadd_steps(), 47815);   /* K15 §0 */
    CHECK_EQ(cq_fadd_region(), 23585u);

    /* THE WIDTH IS 64, WHICH IS THE NARROWING GUARD: `exp` and `frac_bits` are
     * provably below 2^11 and §7.2 forbids narrowing towards that. */
    CHECK(cq_slt_steps(12) < cq_slt_steps(W64));
    CHECK(cq_sub_steps(12) < cq_sub_steps(W64));
    CHECK(cq_barrel_steps(16, CQ_BARREL_SHL) < cq_barrel_steps(W64, CQ_BARREL_SHL));
    /* And `slt` is dearer than `ult`, which is what makes a `ult` substituted
     * on the clamp visible to this identity at all. */
    CHECK(cq_slt_steps(W64) > cq_ult_steps(W64));
}

/* ---- (3) The program against what those blocks just cost. -------------- */

static int fr_slots(const fr_census *c)
{
    return c->eq * cq_eq_steps(W64) + c->slt * cq_slt_steps(W64)
         + c->sub * cq_sub_steps(W64) + c->add * cq_add_steps(W64)
         + c->mux * cq_mux_steps(W64) + c->orr * 3 * W64
         + c->and_ * W64 + c->xor_ * 2 * W64
         + c->bshl * cq_barrel_steps(W64, CQ_BARREL_SHL)
         + c->blshr * cq_barrel_steps(W64, CQ_BARREL_LSHR)
         + c->not1 * 2 + c->and1 + c->or1 * 3
         + c->fadd * cq_fadd_steps();
}

static uint32_t fr_region(const fr_census *c)
{
    return (uint32_t)(c->eq * cq_eq_region(W64) + c->slt * cq_slt_region(W64)
         + c->sub * cq_sub_region(W64) + c->add * cq_add_region(W64)
         + c->mux * cq_mux_region(W64) + (c->orr + c->and_ + c->xor_) * W64
         + (c->bshl + c->blshr) * cq_barrel_region(W64)
         + c->not1 + c->and1 + c->or1)
         + (uint32_t)c->fadd * cq_fadd_region();
}

CQ_TEST(the_programs_are_the_sum_of_their_blocks)
{
    for (int p = 0; p < CQ_FROUND_N_PROG; p++) {
        const cq_fround_row *rows;
        cq_counter fwd, unc;
        fr_census c;
        int n, s = 0;
        uint32_t b = 0u;

        rows = cq_fround_rows((cq_fround_prog)p, &n);
        fr_count(rows, n, &c);

        /* THE COMPOSITION IDENTITY, twice: once from the census (the port's
         * own content times what each module says a block costs) and once
         * row by row through THIS file's cost function. */
        CHECK_EQ(cq_fround_steps((cq_fround_prog)p), fr_slots(&c));
        CHECK_EQ(cq_fround_region((cq_fround_prog)p), fr_region(&c));
        for (int i = 0; i < n; i++) {
            s += fr_ref_steps(&rows[i]);
            b += fr_ref_region(&rows[i]);
        }
        CHECK_EQ(cq_fround_steps((cq_fround_prog)p), s);
        CHECK_EQ(cq_fround_region((cq_fround_prog)p), b);

        /* floor/ceil = the trunc body + ONE fadd + their own rows, EXACTLY:
         * floor's own are two `!=`, one `&` Bool and one ifelse (:60 :65 :66
         * :74); ceil adds the `not1` of :85's `==`. */
        if (p == CQ_FROUND_FLOOR || p == CQ_FROUND_CEIL)
            CHECK_EQ(cq_fround_steps((cq_fround_prog)p),
                     cq_fround_steps(CQ_FROUND_TRUNC)
                     + cq_fadd_steps()
                     + 2 * cq_eq_steps(W64) + 1 + cq_mux_steps(W64)
                     + (p == CQ_FROUND_CEIL ? 2 : 0));

        cq_kd_measure(FR_SPECS[p], W64, &fwd, &unc);
        /* SLOTS AND GATES COME APART, asserted: an implementation that had
         * materialised its constants would make this an equality. */
        CHECK(cq_count_total(&fwd)
              < 2u * (uint64_t)cq_fround_steps((cq_fround_prog)p) + (uint64_t)W64);
        CHECK_EQ(fwd.ry + fwd.rz + fwd.mz, 0);
        printf("# %s rows %d (%d views)  slots %d  region %u  forward (x %llu, "
               "cx %llu, ccx %llu) = %llu  unc = %llu\n",
               FR_SPECS[p]->name, n, c.view,
               cq_fround_steps((cq_fround_prog)p),
               cq_fround_region((cq_fround_prog)p),
               (unsigned long long)fwd.x, (unsigned long long)fwd.cx,
               (unsigned long long)fwd.ccx,
               (unsigned long long)cq_count_total(&fwd),
               (unsigned long long)cq_count_total(&unc));
        fflush(stdout);
    }
}
