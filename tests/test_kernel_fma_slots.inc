/* tests/test_kernel_fma_slots.inc — M39, K20. THE COMPOSITION CHECK, in TWO
 * cases that are different claims, plus the row census and the well-formedness
 * scan. Included by test_kernel_fma.c.
 *
 * WHY TWO CASES AND NOT ONE (bd memory
 * k12s-k11-shaped-mutant-is-caught-and-by-what). The first runs NO KERNEL and
 * pins what each BLOCK costs at W = 64, asked of its own module; the second
 * pins the PROGRAM against what those blocks just measured. So a sibling's
 * cost moving reddens the first and NAMES the block, while a change inside
 * M39 reddens only the second. One combined case would report "fma changed"
 * for a move in M16.
 *
 * AND THE SECOND CASE DOES NOT CALL `cq_fu_row_steps`, WHICH IS THE WHOLE
 * POINT. It walks the row table itself, counts rows per op, and multiplies by
 * the cost it ASKS the owning module for — so it is an INDEPENDENT
 * re-derivation of the same total rather than a second call to the function
 * under test. A step machine that answered `cq_ult_steps(64)` for a
 * `CQ_FUOP_SLT` row would keep `cq_fma_steps()` self-consistent and would go
 * red here. That is exactly the shape bd memory
 * sub-region-offset-needs-a-canary-and-a-shared-step-count-blinds-the-composition-check
 * records: a composition identity whose two sides ask ONE function cannot see
 * that function move.
 *
 * THE PER-OP ROW COUNTS ARE PINNED TOO, and they are the K20 figure a reader
 * checks against the Julia. A row retyped from `slt` to `ult` keeps the row
 * count, keeps well-formedness, keeps the spans disjoint, computes the wrong
 * value on exactly the negative half of the input space — and moves this
 * tally.
 */

/* K20's 395 rows by op, MEASURED at the landing and pinned here. The three
 * zero-cost kinds are listed with the rest because a row that stops being a
 * view is a row that started emitting. */
static const struct { int op; int n; const char *name; } FA_CENSUS[] = {
    { CQ_FUOP_VIEW,    73, "view"    }, { CQ_FUOP_OUT,     15, "out"     },
    { CQ_FUOP_CLASS,    9, "class"   }, { CQ_FUOP_EQ,      35, "eq"      },
    { CQ_FUOP_ULT,      5, "ult"     }, { CQ_FUOP_SLT,     15, "slt"     },
    { CQ_FUOP_ADD,     12, "add"     }, { CQ_FUOP_SUB,     22, "sub"     },
    { CQ_FUOP_MUX,     95, "mux"     }, { CQ_FUOP_AND,      4, "and"     },
    { CQ_FUOP_OR,      30, "or"      }, { CQ_FUOP_XOR,      3, "xor"     },
    { CQ_FUOP_MUL,      4, "mul"     }, { CQ_FUOP_BSHL,     6, "bshl"    },
    { CQ_FUOP_BLSHR,    6, "blshr"   }, { CQ_FUOP_NORM52,   3, "norm52"  },
    { CQ_FUOP_SUBNORM,  1, "subnorm" }, { CQ_FUOP_ROUND,    1, "round"   },
    { CQ_FUOP_NOT1,    34, "not1"    }, { CQ_FUOP_AND1,    12, "and1"    },
    { CQ_FUOP_OR1,     10, "or1"     }
};
enum { N_FA_CENSUS = (int)(sizeof FA_CENSUS / sizeof FA_CENSUS[0]) };

/* The cost of one row of each op, ASKED of the owning module and never written
 * down. The three zero-cost kinds answer 0 by construction. */
static int fa_cost_of(int op)
{
    switch (op) {
    case CQ_FUOP_VIEW: case CQ_FUOP_OUT:   return 0;
    case CQ_FUOP_EQ:      return cq_eq_steps(W64);
    case CQ_FUOP_ULT:     return cq_ult_steps(W64);
    case CQ_FUOP_SLT:     return cq_slt_steps(W64);
    case CQ_FUOP_ADD:     return cq_add_steps(W64);
    case CQ_FUOP_SUB:     return cq_sub_steps(W64);
    case CQ_FUOP_MUX:     return cq_mux_steps(W64);
    case CQ_FUOP_AND:     return 1 * W64;
    case CQ_FUOP_OR:      return 3 * W64;
    case CQ_FUOP_XOR:     return 2 * W64;
    case CQ_FUOP_MUL:     return cq_mul_steps(W64);
    case CQ_FUOP_BSHL:    return cq_barrel_steps(W64, CQ_BARREL_SHL);
    case CQ_FUOP_BLSHR:   return cq_barrel_steps(W64, CQ_BARREL_LSHR);
    case CQ_FUOP_NORM52:  return cq_norm52_steps();
    case CQ_FUOP_SUBNORM: return cq_subnorm_steps();
    case CQ_FUOP_ROUND:   return cq_round_steps();
    case CQ_FUOP_NOT1:    return 2;
    case CQ_FUOP_AND1:    return 1;
    case CQ_FUOP_OR1:     return 3;
    default: break;
    }
    return -1;
}

static uint32_t fa_bits_of(int op)
{
    switch (op) {
    case CQ_FUOP_VIEW: case CQ_FUOP_OUT:   return 0u;
    case CQ_FUOP_EQ:      return (uint32_t)cq_eq_region(W64);
    case CQ_FUOP_ULT:     return (uint32_t)cq_ult_region(W64);
    case CQ_FUOP_SLT:     return (uint32_t)cq_slt_region(W64);
    case CQ_FUOP_ADD:     return (uint32_t)cq_add_region(W64);
    case CQ_FUOP_SUB:     return (uint32_t)cq_sub_region(W64);
    case CQ_FUOP_MUX:     return (uint32_t)cq_mux_region(W64);
    case CQ_FUOP_AND: case CQ_FUOP_OR: case CQ_FUOP_XOR: return (uint32_t)W64;
    case CQ_FUOP_MUL:     return (uint32_t)cq_mul_region(W64);
    case CQ_FUOP_BSHL: case CQ_FUOP_BLSHR:
        return (uint32_t)cq_barrel_region(W64);
    case CQ_FUOP_NORM52:  return cq_norm52_region();
    case CQ_FUOP_SUBNORM: return cq_subnorm_region();
    case CQ_FUOP_ROUND:   return cq_round_region();
    case CQ_FUOP_NOT1: case CQ_FUOP_AND1: case CQ_FUOP_OR1: return 1u;
    default: break;
    }
    return 0u;
}

/* ---- The Bool-operand scan, which must REPORT and never abort. ----------
 *
 * `cq_fma_row_width` is the one place the row -> width mapping lives, and it
 * is a LIBRARY guard: it dies on an index outside the program, a projection of
 * a later row or of a non-tuple row, and an unknown op. So a test that hands
 * it a mux whose condition is a constant code (`CQ_FU_K_ZERO`, negative)
 * reports nothing and ABORTS the binary — every later case unrun, and the
 * message a library refusal rather than "row N's condition is a constant"
 * (bd 9ve.41). `fa_width_or_refuse` pre-screens exactly the inputs that
 * function dies on and answers -1 for them; `fa_bool_operand_faults` counts
 * and, when asked, names each fault as a test FAILURE. */
static int fa_width_or_refuse(const cq_fma_row *rows, int n, int i, int s)
{
    if (s < 0 || s >= i || s >= n) return -1;
    if (rows[s].op < 0 || rows[s].op >= CQ_FUOP_N_OP) return -1;
    if (rows[s].op == CQ_FUOP_OUT) {
        int p = rows[s].s0;

        if (p < 0 || p >= s) return -1;
        if (rows[p].op != CQ_FUOP_NORM52 && rows[p].op != CQ_FUOP_SUBNORM
            && rows[p].op != CQ_FUOP_ROUND) return -1;
    }
    return cq_fma_row_width(rows, n, s);
}

static int fa_bool_operand_faults(const cq_fma_row *rows, int n, int report)
{
    int faults = 0;

    for (int i = 0; i < n; i++) {
        int op = rows[i].op, slots;
        short s[2];

        if (op == CQ_FUOP_MUX || op == CQ_FUOP_NOT1) slots = 1;
        else if (op == CQ_FUOP_AND1 || op == CQ_FUOP_OR1) slots = 2;
        else continue;
        s[0] = rows[i].s0; s[1] = rows[i].s1;
        for (int j = 0; j < slots; j++) {
            int w = fa_width_or_refuse(rows, n, i, s[j]);

            if (w == 1) continue;
            faults++;
            if (!report) continue;
            if (s[j] < 0)
                cq_h_fail(__FILE__, __LINE__,
                          "row %d (op %d) slot %d is operand code %d, a "
                          "constant or a rail, not a Bool row", i, op, j,
                          (int)s[j]);
            else if (w < 0)
                cq_h_fail(__FILE__, __LINE__,
                          "row %d (op %d) slot %d names row %d, which is not "
                          "a well-formed earlier row", i, op, j, (int)s[j]);
            else
                cq_h_fail(__FILE__, __LINE__,
                          "row %d (op %d) slot %d names row %d of width %d, "
                          "not a Bool row", i, op, j, (int)s[j], w);
        }
    }
    return faults;
}

/* THE SCAN'S OWN PROVOCATION: a hand-built table with one fault of each kind
 * it names, plus one well-formed mux as the negative control. Reaching the
 * CHECK at all is the claim that nothing aborted. */
CQ_TEST(the_bool_operand_scan_reports_rather_than_aborts)
{
    static const cq_fma_row bad[] = {
        { CQ_FUOP_EQ,   CQ_FU_A,      CQ_FU_B, 0,       0, 0 }, /* 0: Bool   */
        { CQ_FUOP_MUX,  CQ_FU_K_ZERO, CQ_FU_A, CQ_FU_B, 0, 0 }, /* 1: const  */
        { CQ_FUOP_MUX,  0,            CQ_FU_A, CQ_FU_B, 0, 0 }, /* 2: fine   */
        { CQ_FUOP_NOT1, 9,            0,       0,       0, 0 }, /* 3: later  */
        { CQ_FUOP_MUX,  1,            CQ_FU_A, CQ_FU_B, 0, 0 }, /* 4: 64-bit */
        { CQ_FUOP_OUT,  CQ_FU_A,      0,       0,       0, 0 }, /* 5: bad    */
        { CQ_FUOP_AND1, 0,            5,       0,       0, 0 }, /* 6: via 5  */
    };
    int n = (int)(sizeof bad / sizeof bad[0]);

    CHECK_EQ(fa_bool_operand_faults(bad, n, 0), 4);
    CHECK_EQ(fa_bool_operand_faults(bad, 3, 0), 1); /* rows 0-2: only row 1 */
}

/* ---- The port, checked against itself structurally. --------------------- */

CQ_TEST(the_program_is_the_source_line_for_line)
{
    const cq_fma_row *rows;
    int n, total = 0;

    rows = cq_fma_rows(&n);
    CHECK_EQ(n, 395);
    CHECK_EQ(cq_fma_n_rows(), n);
    CHECK(n <= CQ_FMA_MAX_ROWS);

    /* THE PER-OP TALLY. A row retyped from one op to another keeps the row
     * count and moves exactly this. The three class predicates COMPOSE M31's
     * block (nine rows, not the ninety a literal transcription of :43-51
     * would give), which is Rule 1 applied to the call graph. */
    for (int c = 0; c < N_FA_CENSUS; c++) {
        int seen = 0;

        for (int i = 0; i < n; i++) if (rows[i].op == FA_CENSUS[c].op) seen++;
        if (seen != FA_CENSUS[c].n)
            cq_h_fail(__FILE__, __LINE__,
                      "op `%s`: %d rows, expected %d", FA_CENSUS[c].name,
                      seen, FA_CENSUS[c].n);
        total += FA_CENSUS[c].n;
    }
    CHECK_EQ(total, n);        /* the census covers every op, with no hole */

    /* WELL-FORMEDNESS: every operand that names a row names a STRICTLY EARLIER
     * one. That is what makes the prefix-offset walk a valid topological
     * order, and an off-by-one in a reference is the mistake PRD-v2 §7.1 says
     * a kernel of this shape actually makes. */
    for (int i = 0; i < n; i++) {
        short s[3];
        int ar = cq_fma_arity(rows[i].op);

        s[0] = rows[i].s0; s[1] = rows[i].s1; s[2] = rows[i].s2;
        for (int j = 0; j < ar; j++)
            if (s[j] >= i)
                cq_h_fail(__FILE__, __LINE__,
                          "row %d slot %d names row %d, which is not earlier",
                          i, j, (int)s[j]);
        if (rows[i].op == CQ_FUOP_OUT && (rows[i].s0 < 0 || rows[i].s0 >= i))
            cq_h_fail(__FILE__, __LINE__,
                      "row %d is a projection of row %d", i, (int)rows[i].s0);
    }
    /* A MUX's condition and the three 1-bit ops' operands must be Bool rows.
     * `cq_fma_row_width` is the one place that mapping lives, reached through
     * a pre-screen so a constant condition FAILS here instead of aborting in
     * the library guard (bd 9ve.41). */
    CHECK_EQ(fa_bool_operand_faults(rows, n, 1), 0);

    /* PRD-v2 §5's seam, as a row index. What must be ABOVE it is the whole
     * product-and-align half and what must be BELOW it is the single
     * rounding — so the seam sits between the last alignment mux and
     * `_add128`'s first row, and both of those are named rather than counted. */
    CHECK(cq_fma_seam_row() > 0 && cq_fma_seam_row() < n);
    CHECK_EQ(rows[cq_fma_seam_row()].op, CQ_FUOP_ADD);
    CHECK_EQ(rows[cq_fma_seam_row() - 1].op, CQ_FUOP_MUX);
    /* Exactly ONE round-and-pack and ONE subnormal hand-off, both BELOW the
     * seam: a single rounding is the defining property of an `fma`, and a
     * second `cq_round_block` anywhere in this table would be the `fmul`-then-
     * `fadd` spelling PRD-v2 §5 forbids. */
    {
        int n_round = 0, n_sub = 0;

        for (int i = 0; i < n; i++) {
            if (rows[i].op == CQ_FUOP_ROUND)
                { n_round++; CHECK(i > cq_fma_seam_row()); }
            if (rows[i].op == CQ_FUOP_SUBNORM)
                { n_sub++;   CHECK(i > cq_fma_seam_row()); }
            if (rows[i].op == CQ_FUOP_MUL) CHECK(i < cq_fma_seam_row());
        }
        CHECK_EQ(n_round, 1);
        CHECK_EQ(n_sub, 1);
    }
}

/* ---- (1) What the blocks cost, asked of their own modules. -------------- */

CQ_TEST(the_blocks_cost_what_their_modules_say_they_cost)
{
    /* ASKED of M12, M14, M16, M17, M18, M31 and M32 and pinned against their
     * own measured W = 64 rows. K20 composes every block type in the
     * catalogue, so if one moves everything below moves with it and this line
     * says so first. */
    CHECK_EQ(cq_eq_steps(W64),  317);          /* lower_eq!   5W-3        */
    CHECK_EQ(cq_ult_steps(W64), 385);          /* lower_ult!  6W+1        */
    CHECK_EQ(cq_slt_steps(W64), 515);          /* lower_slt!  2W+2+ult    */
    CHECK_EQ(cq_add_steps(W64), 318);          /* lower_add!  5W-2        */
    CHECK_EQ(cq_sub_steps(W64), 447);          /* lower_sub!  7W-1        */
    CHECK_EQ(cq_mux_steps(W64), 256);          /* lower_mux!  4W          */
    CHECK_EQ(cq_mul_steps(W64), 26336);        /* K11 (13W^2-9W)/2        */
    CHECK_EQ(cq_barrel_steps(W64, CQ_BARREL_SHL),  1921);   /* 5WS+W-2^S+1 */
    CHECK_EQ(cq_barrel_steps(W64, CQ_BARREL_LSHR), 1921);
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_NAN),  637);
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_INF),  639);
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_ZERO), 639);
    CHECK_EQ(cq_norm52_steps(), 8755);         /* M32, common.jl:68-105   */
    CHECK_EQ(cq_subnorm_steps(), 9165);        /* M32, :174-191           */
    CHECK_EQ(cq_round_steps(),  6287);         /* M32, :198-227           */

    CHECK_EQ(cq_eq_region(W64),  127);         /* diff ++ orr   2W-1      */
    CHECK_EQ(cq_ult_region(W64), 193);         /* nb ++ carry ++ axnb 3W+1*/
    CHECK_EQ(cq_slt_region(W64), 321);         /* af ++ bf ++ ult's three */
    CHECK_EQ(cq_add_region(W64), 128);         /* t ++ c        2W        */
    CHECK_EQ(cq_sub_region(W64), 192);         /* nb ++ d ++ c  3W        */
    CHECK_EQ(cq_mux_region(W64), 128);         /* r ++ d        2W        */
    CHECK_EQ(cq_mul_region(W64), 4224);        /* accum ++ pp ++ x  W^2+2W*/
    CHECK_EQ(cq_barrel_region(W64), 1216);     /* W(3S+1), S = 6          */
    CHECK_EQ(cq_norm52_region(),  3968u);
    CHECK_EQ(cq_subnorm_region(), 5253u);
    CHECK_EQ(cq_round_region(),   3082u);

    /* THE SIGNED AND THE UNSIGNED COMPARATOR COST DIFFERENT AMOUNTS, which is
     * the one thing that makes a `slt`-for-`ult` substitution visible to L4 at
     * all. L1 sees it only on a negative operand — and fifteen of K20's twenty
     * comparisons are on a genuinely negative Int64 (PRD-v2 §7.6). */
    CHECK(cq_slt_steps(W64) > cq_ult_steps(W64));

    /* AND THE WIDTH IS 64, WHICH IS THE WHOLE OF THE NARROWING GUARD. `ea` is
     * eleven bits, `expDiff` twelve and `mid_carry` exactly one; PRD-v2 §7.2
     * forbids narrowing an intermediate towards its value's width, so every
     * block runs at the Julia type's width. A port that narrowed shows up
     * below as the program no longer being the sum of its blocks at 64. */
    CHECK(cq_eq_steps(12)  < cq_eq_steps(W64));
    CHECK(cq_mul_steps(32) < cq_mul_steps(W64) / 3);
}

/* ---- (2) The program is the sum of its blocks. -------------------------- */

CQ_TEST(the_program_is_the_sum_of_its_blocks)
{
    const cq_fma_row *rows;
    long slots = 0;
    uint64_t bits = 0u;
    int n;

    rows = cq_fma_rows(&n);

    /* WALKED FROM THE TABLE AND COSTED FROM THE MODULES — `cq_fu_row_steps` is
     * never called here, which is what makes this an INDEPENDENT derivation
     * rather than a second call to the function under test. The class rows
     * ask M31 per class, because the NaN row is one `==` and the inf/zero rows
     * are two. */
    for (int i = 0; i < n; i++) {
        int c;

        if (rows[i].op == CQ_FUOP_CLASS) {
            slots += cq_fp_class_steps((cq_fp_class)rows[i].s1);
            bits  += cq_fp_class_region((cq_fp_class)rows[i].s1);
            continue;
        }
        c = fa_cost_of(rows[i].op);
        if (c < 0) cq_h_fail(__FILE__, __LINE__,
                             "row %d has an op this case does not cost", i);
        slots += c;
        bits  += fa_bits_of(rows[i].op);
    }

    CHECK_EQ((long)cq_fma_steps(), slots);
    CHECK_EQ((uint64_t)cq_fma_region(), bits);

    /* AND THE MEASURED TOTALS, so a change that moved BOTH sides together —
     * a block cost and the table in the same direction — still reddens
     * something. These are the K20 landing figures. */
    CHECK_EQ(cq_fma_steps(), 241083);
    CHECK_EQ(cq_fma_region(), 84606u);

    /* THE FOUR PRODUCTS AND THE TWELVE BARRELS ARE WHERE THE COST IS, and
     * saying so as a RATIO rather than a number is what keeps this line true
     * when a sibling moves. `_sf_widemul_u64_to_128`'s 32/32 split is four
     * `cq_mul_block`s and `_shiftRightJam128` x2 is twelve barrels. */
    CHECK(4L * cq_mul_steps(W64) * 100L / slots >= 40L);
    CHECK(12L * cq_barrel_steps(W64, CQ_BARREL_SHL) * 100L / slots >= 8L);
}
