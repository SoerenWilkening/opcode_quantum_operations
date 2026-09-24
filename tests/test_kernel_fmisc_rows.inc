/* tests/test_kernel_fmisc_rows.inc — the fmin/fmax PROGRAM's own instruments:
 * the table against the Julia, its well-formedness, the composition identity
 * over SLOTS, the classical row against the oracle, and the named cells.
 * Included by test_kernel_fmisc.c. NOTHING HERE READS A GOLDEN.
 */

/* ---- (1) fmin.jl:86-106 and :114-133, transcribed a SECOND time. ------- */

/* READ STRAIGHT OFF THE PINNED SOURCE AND SHARING NOTHING WITH fmisc_rows.c.
 * The two differences between the programs are OPERAND ORDER — `olt(b, a)`
 * at :128 and `ifelse(a_neg, b, a)` at :130 — and nothing structural can see
 * either: the gate tuple, the palindrome, the pool and every scan are
 * identical with them reversed. So they are written here as literals. */
static const cq_fx_row REF_FMIN[] = {
    { CQ_FXOP_CLASS_NAN, CQ_FX_A,     0,            0  },  /* :93  */
    { CQ_FXOP_CLASS_NAN, CQ_FX_B,     0,            0  },  /* :94  */
    { CQ_FXOP_AND1,      0,           1,            0  },  /* :95  */
    { CQ_FXOP_EQ,        CQ_FX_ABS_A, CQ_FX_K_ZERO, 0  },  /* :97  */
    { CQ_FXOP_NOT1,      3,           0,            0  },
    { CQ_FXOP_EQ,        CQ_FX_ABS_B, CQ_FX_K_ZERO, 0  },
    { CQ_FXOP_NOT1,      5,           0,            0  },
    { CQ_FXOP_AND1,      4,           6,            0  },
    { CQ_FXOP_EQ,        CQ_FX_SGN_A, CQ_FX_K_ZERO, 0  },  /* :98  */
    { CQ_FXOP_FCMP_OLT,  CQ_FX_A,     CQ_FX_B,      0  },  /* :100 */
    { CQ_FXOP_EQ,        9,           CQ_FX_K_ZERO, 0  },
    { CQ_FXOP_MUX,       10,          CQ_FX_A,      CQ_FX_B },  /* :101 */
    { CQ_FXOP_MUX,       8,           CQ_FX_A,      CQ_FX_B },  /* :102 */
    { CQ_FXOP_MUX,       7,           12,           11 },
    { CQ_FXOP_MUX,       1,           CQ_FX_A,      13 },  /* :104 */
    { CQ_FXOP_MUX,       0,           CQ_FX_B,      14 },
    { CQ_FXOP_MUX,       2,           CQ_FX_K_QNAN, 15 }   /* :105 */
};

enum { FM_NROWS = (int)(sizeof REF_FMIN / sizeof REF_FMIN[0]) };
_Static_assert(FM_NROWS == 17, "fmin.jl:86-106 is 17 operator occurrences");

CQ_TEST(the_programs_are_the_source_line_for_line)
{
    int nmin = 0, nmax = 0, differ = 0;
    const cq_fx_row *mn = cq_fmisc_rows(CQ_FMISC_MIN, &nmin);
    const cq_fx_row *mx = cq_fmisc_rows(CQ_FMISC_MAX, &nmax);
    cq_fx_row ref_max[FM_NROWS];

    memcpy(ref_max, REF_FMIN, sizeof ref_max);
    ref_max[9].s0 = CQ_FX_B;  ref_max[9].s1 = CQ_FX_A;     /* :128 olt(b, a) */
    ref_max[12].s1 = CQ_FX_B; ref_max[12].s2 = CQ_FX_A;    /* :130 (a_neg, b, a) */

    CHECK_EQ(nmin, FM_NROWS);
    CHECK_EQ(nmax, FM_NROWS);
    for (int i = 0; i < FM_NROWS && i < nmin && i < nmax; i++) {
        int n = cq_fmisc_arity(REF_FMIN[i].op);
        short e[3] = { REF_FMIN[i].s0, REF_FMIN[i].s1, REF_FMIN[i].s2 };
        short g[3] = { mn[i].s0, mn[i].s1, mn[i].s2 };
        short ex[3] = { ref_max[i].s0, ref_max[i].s1, ref_max[i].s2 };
        short gx[3] = { mx[i].s0, mx[i].s1, mx[i].s2 };

        CHECK_EQ(mn[i].op, REF_FMIN[i].op);
        CHECK_EQ(mx[i].op, ref_max[i].op);
        for (int j = 0; j < n; j++) {
            if (g[j] != e[j] || gx[j] != ex[j])
                cq_h_fail(__FILE__, __LINE__, "row %d operand %d: fmin has %d "
                          "(want %d), fmax has %d (want %d)", i, j, g[j], e[j],
                          gx[j], ex[j]);
            differ += (e[j] != ex[j]);
        }
    }
    /* EXACTLY the four operand slots of rows 9 and 12 differ. */
    CHECK_EQ(differ, 4);
}

/* ---- (2) Well-formedness: the table cannot reach the machine's refusals. */

/* THE REACHABILITY PROOF THAT STANDS IN FOR A DEATH CASE (bd a-guard-that-
 * reads-its-own-table-has-no-death-case). The block names a PROGRAM, not a
 * row pointer, so the machine's "unknown operand code" / "not a row" / "a
 * 64-lane row as a 1-bit operand" refusals are reachable only from a
 * malformed table — and these are the properties that make them unreachable
 * from the two shipped ones. The refusals themselves are driven directly
 * through fmisc_int.h in tests/test_kernel_fmisc_death.c. */
CQ_TEST(the_programs_are_well_formed)
{
    for (int p = 0; p < CQ_FMISC_N_PROG; p++) {
        int n = 0;
        const cq_fx_row *r = cq_fmisc_rows((cq_fmisc_prog)p, &n);

        CHECK_EQ(cq_fmisc_row_width((cq_fmisc_prog)p, n - 1), W64);
        CHECK_EQ((int)r[n - 1].op, (int)CQ_FXOP_MUX);   /* the result is a span */
        for (int i = 0; i < n; i++) {
            short s[3] = { r[i].s0, r[i].s1, r[i].s2 };

            CHECK(r[i].op >= 0 && r[i].op < (int)CQ_FXOP_N_OP);
            CHECK_EQ(cq_fmisc_row_width((cq_fmisc_prog)p, i),
                     r[i].op == CQ_FXOP_MUX ? W64 : 1);
            for (int j = 0; j < cq_fmisc_arity(r[i].op); j++) {
                /* The operand is ONE bit for a mux's condition and for the
                 * one-lane `not1`/`and1`; 64 lanes everywhere else. */
                int one_bit = (r[i].op == CQ_FXOP_NOT1 || r[i].op == CQ_FXOP_AND1
                               || (r[i].op == CQ_FXOP_MUX && j == 0));

                if (s[j] >= 0) {
                    CHECK(s[j] < i);                 /* strictly earlier */
                    if (one_bit)
                        CHECK_EQ(cq_fmisc_row_width((cq_fmisc_prog)p, s[j]), 1);
                } else {
                    CHECK(!one_bit);
                    CHECK(s[j] >= CQ_FX_K_QNAN && s[j] <= CQ_FX_A);
                }
            }
            /* A class block and an `olt` read a WHOLE RAIL, never a view. */
            if (r[i].op == CQ_FXOP_CLASS_NAN || r[i].op == CQ_FXOP_FCMP_OLT)
                CHECK(s[0] == CQ_FX_A || s[0] == CQ_FX_B);
            if (r[i].op == CQ_FXOP_FCMP_OLT)
                CHECK((s[0] == CQ_FX_A && s[1] == CQ_FX_B)
                      || (s[0] == CQ_FX_B && s[1] == CQ_FX_A));
        }
    }
}

/* ---- (3) The block costs, ASKED of their modules. Runs no kernel. ------ */

static int fm_olt_rows(cq_fcmp_row *rows)
{
    return cq_fcmp_program(CQ_FCMP_OLT, rows);
}

CQ_TEST(the_blocks_cost_what_their_modules_say_they_cost)
{
    cq_fcmp_row olt[CQ_FCMP_MAX_ROWS];
    int n = fm_olt_rows(olt);

    /* Pinned against the owning modules' own measured W = 64 rows, so a
     * sibling's cost moving reddens THIS line and names the block. */
    CHECK_EQ(cq_eq_steps(W64), 317);
    CHECK_EQ(cq_mux_steps(W64), 256);
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_NAN), 637);
    CHECK_EQ(n, 24);                         /* M36's B_OLT (fcmp.jl:8-47) */
    CHECK(cq_fcmp_steps(olt, n) > 0);
    CHECK_EQ(cq_eq_region(W64), 127);
    CHECK_EQ(cq_mux_region(W64), 128);
    CHECK_EQ(cq_fp_class_region(CQ_FP_IS_NAN), 256u);

    /* §7.2: every block at the Julia type's width. A narrowed `eq` or mux
     * would make the identity below fail at 64. */
    CHECK(cq_eq_steps(1) < cq_eq_steps(W64));
    CHECK(cq_mux_steps(1) < cq_mux_steps(W64));
}

/* ---- (4) Each program against what those blocks just cost. ------------- */

/* THE BLOCK COUNT, COUNTED OFF fmin.jl BY HAND: two class blocks (:93-94),
 * four `==`/`!=` occurrences (:97 twice, :98, :100's `!= 0`), two `lower_not1!`
 * (the two `==` at :97; the other two spell `!=` and read the raw wire), two
 * `&` (:95, :97), ONE `soft_fcmp_olt` (:100), six `ifelse` (:101, :102 x2,
 * :104 x2, :105). `lower_not1!` is 2 slots and 1 bit, `lower_and!` 1 slot and
 * 1 bit at one lane (arith.jl:474-478, :268-272). */
enum { FM_CLS = 2, FM_EQ = 4, FM_NOT1 = 2, FM_AND1 = 2, FM_OLT = 1, FM_MUX = 6 };

CQ_TEST(each_program_is_the_sum_of_its_blocks)
{
    cq_fcmp_row olt[CQ_FCMP_MAX_ROWS];
    int n = fm_olt_rows(olt);
    int slots = FM_CLS * cq_fp_class_steps(CQ_FP_IS_NAN)
              + FM_EQ * cq_eq_steps(W64) + FM_NOT1 * 2 + FM_AND1 * 1
              + FM_OLT * cq_fcmp_steps(olt, n) + FM_MUX * cq_mux_steps(W64);
    uint32_t bits = (uint32_t)FM_CLS * cq_fp_class_region(CQ_FP_IS_NAN)
                  + (uint32_t)(FM_EQ * cq_eq_region(W64))
                  + (uint32_t)(FM_NOT1 + FM_AND1)
                  + (uint32_t)FM_OLT * cq_fcmp_region(olt, n)
                  + (uint32_t)(FM_MUX * cq_mux_region(W64));
    const cq_kd_spec *sp[2] = { &SPEC_FMIN, &SPEC_FMAX };

    CHECK_EQ(FM_CLS + FM_EQ + FM_NOT1 + FM_AND1 + FM_OLT + FM_MUX, FM_NROWS);
    for (int p = 0; p < CQ_FMISC_N_PROG; p++) {
        cq_counter fwd, unc;

        CHECK_EQ(cq_fmisc_steps((cq_fmisc_prog)p), slots);
        CHECK_EQ(cq_fmisc_region((cq_fmisc_prog)p), bits);

        /* SLOTS AND GATES COME APART, asserted: the views' constant lanes
         * and the constant spans fold at the all-quantum mask too. */
        cq_kd_measure(sp[p], W64, &fwd, &unc);
        CHECK(cq_count_total(&fwd) < 2u * (uint64_t)slots + (uint64_t)W64);
        printf("# %s rows %d slots %d region %u bits  forward (x %llu, cx %llu,"
               " ccx %llu)  unc (x %llu, cx %llu, ccx %llu)\n", sp[p]->name,
               FM_NROWS, slots, bits, (unsigned long long)fwd.x,
               (unsigned long long)fwd.cx, (unsigned long long)fwd.ccx,
               (unsigned long long)unc.x, (unsigned long long)unc.cx,
               (unsigned long long)unc.ccx);
    }
    fflush(stdout);
}

/* ---- (5) The classical row against the ORACLE, broadly. ---------------- */

/* NO CIRCUIT RUNS HERE, so it can be broad: the complete classical table
 * (generic §7.12 pairs + the circuit set), then a seeded sweep that forces
 * NaN exponents on every fourth draw and zeros on every eighth. */
CQ_TEST(the_classical_row_agrees_with_the_oracle)
{
    uint64_t seed = 0x243f6a8885a308d3ull;
    int n = fm_full_binary(fminmax_anchors, -1, NULL), n_a = 0, n_b = 0;

    for (int i = 0; i < n; i++) {
        cq_ref_w v[CQ_KD_MAX_SRC];

        memset(v, 0, sizeof v);
        CHECK_EQ(fm_full_binary(fminmax_anchors, i, v), 1);
        CHECK_EQ(cq_fmin_eval(v[0].lo, v[1].lo), cq_ref_fmin(v[0].lo, v[1].lo));
        CHECK_EQ(cq_fmax_eval(v[0].lo, v[1].lo), cq_ref_fmax(v[0].lo, v[1].lo));
    }
    for (int k = 0; k < 20000; k++) {
        uint64_t a, b, r;

        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        a = seed ^ (seed >> 29);
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        b = seed ^ (seed >> 31);
        if ((k & 3) == 0) a |= CQ_F64_EXP_MASK;
        if ((k & 3) == 1) b |= CQ_F64_EXP_MASK;
        if ((k & 7) == 2) { a &= CQ_F64_SIGN_MASK; b &= CQ_F64_SIGN_MASK; }
        if ((k & 15) == 3) b = a ^ CQ_F64_SIGN_MASK;

        r = cq_ref_fmin(a, b);
        if (cq_fmin_eval(a, b) != r || cq_fmax_eval(a, b) != cq_ref_fmax(a, b))
            cq_h_fail(__FILE__, __LINE__, "fmin/fmax(%016llx, %016llx): the "
                      "transcription and the oracle disagree",
                      (unsigned long long)a, (unsigned long long)b);
        n_a += (r == a); n_b += (r == b && a != b);
    }
    CHECK(n_a > 0 && n_b > 0);               /* both arms were exercised */
}

/* ---- (6) The named cells: each row against its neighbour, as LITERALS. -- */

/* Written down rather than computed, so the ORACLE is on trial here too:
 * each line is one of §7.7 / §7.12's neighbour distinctions. Checked against
 * the oracle, the classical row AND the circuit at the all-quantum mask. */
CQ_TEST(the_named_cells_separate_each_row_from_its_neighbour)
{
    static const struct { uint64_t a, b, mn, mx; } CELLS[] = {
        /* fmin vs fminimum: a NaN operand is ABSORBED, not propagated. */
        { CQ_F64_QNAN_A,   CQ_F64_ONE,      CQ_F64_ONE,      CQ_F64_ONE      },
        { CQ_F64_ONE,      CQ_F64_SNAN,     CQ_F64_ONE,      CQ_F64_ONE      },
        { CQ_F64_SNAN,     FM_TWO,          FM_TWO,          FM_TWO          },
        /* both NaN: CANONICAL and POSITIVE — neither payload, not INDEF. */
        { CQ_F64_QNAN_A,   CQ_F64_QNAN_B,   0x7ff8000000000000ull,
                                            0x7ff8000000000000ull            },
        { CQ_F64_DEFAULT_NAN, CQ_F64_SNAN,  0x7ff8000000000000ull,
                                            0x7ff8000000000000ull            },
        /* ±0, both orders: −0 for min, +0 for max. */
        { CQ_F64_POS_ZERO, CQ_F64_NEG_ZERO, CQ_F64_NEG_ZERO, CQ_F64_POS_ZERO },
        { CQ_F64_NEG_ZERO, CQ_F64_POS_ZERO, CQ_F64_NEG_ZERO, CQ_F64_POS_ZERO },
        /* ONE zero: `both_zero` is FALSE although `abs_a` (or `abs_b`) is 0. */
        { CQ_F64_POS_ZERO, FM_TWO,          CQ_F64_POS_ZERO, FM_TWO          },
        { CQ_F64_ONE,      CQ_F64_NEG_ZERO, CQ_F64_NEG_ZERO, CQ_F64_ONE      },
        /* fmin vs fmax, both signs. */
        { CQ_F64_ONE,      FM_TWO,          CQ_F64_ONE,      FM_TWO          },
        { CQ_F64_NEG(CQ_F64_ONE), CQ_F64_NEG(FM_TWO),
          CQ_F64_NEG(FM_TWO), CQ_F64_NEG(CQ_F64_ONE)                         },
        { CQ_F64_NEG_INF,  CQ_F64_POS_INF,  CQ_F64_NEG_INF,  CQ_F64_POS_INF  }
    };
    int nc = (int)(sizeof CELLS / sizeof CELLS[0]);
    cq_ref_w q = cq_ref_w_ones(W64);
    cq_mock m;

    cq_mock_init(&m);
    for (int i = 0; i < nc; i++) {
        CHECK_EQ(cq_ref_fmin(CELLS[i].a, CELLS[i].b), CELLS[i].mn);
        CHECK_EQ(cq_ref_fmax(CELLS[i].a, CELLS[i].b), CELLS[i].mx);
        CHECK_EQ(cq_fmin_eval(CELLS[i].a, CELLS[i].b), CELLS[i].mn);
        CHECK_EQ(cq_fmax_eval(CELLS[i].a, CELLS[i].b), CELLS[i].mx);
        CHECK_EQ(fm_run(&SPEC_FMIN, CELLS[i].a, CELLS[i].b, q, q, &m, NULL),
                 CELLS[i].mn);
        CHECK_EQ(fm_run(&SPEC_FMAX, CELLS[i].a, CELLS[i].b, q, q, &m, NULL),
                 CELLS[i].mx);
    }
    cq_mock_dispose(&m);
}
