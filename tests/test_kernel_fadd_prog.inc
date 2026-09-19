/* tests/test_kernel_fadd_prog.inc — the PORT's own instruments: program
 * well-formedness, `fsub` as a rebased `fadd` body, the `Bennett-m63k`
 * discriminator, the classical row against the host, §7.4's three pinned
 * cells, the G/R/S hand-off encoding and the D8 band. Included by
 * test_kernel_fadd.c.
 *
 * NOTHING HERE READS A GOLDEN. K15.md §6.5 ranks the slot arithmetic as risk 1
 * and the M32 hand-off as risk 3 precisely because every structural instrument
 * in this project is blind to them — a wrong G/R/S lane is right on every
 * input that does not round — so the cases below are the ones that
 * `CQOPS_UPDATE_GOLDENS=1` cannot bless.
 */

#define FA_SIGN  0x8000000000000000ull

/* ---- (1) Both programs are well formed. -------------------------------- */

/* EVERY ROW REFERENCE IS STRICTLY BACKWARDS AND EVERY OPERAND IS THE RIGHT
 * WIDTH. A forward reference reads a span the step machine has not written
 * yet, which is a plausible gate on a wire holding |0>; a 1-bit operator
 * handed a 64-lane row reads a wire that is not a flag. Both are caught here
 * on the TABLE; the machine's own refusals are driven in the death file, and
 * bd a-table-driven-kernel-needs-both-operand-refusals records why the two are
 * not the same hazard. */
static int fa_is_flag_row(const cq_fadd_row *rows, int i)
{
    const cq_fadd_row *r = &rows[i];

    switch (r->op) {
    case CQ_FAOP_CLASS: case CQ_FAOP_EQ: case CQ_FAOP_ULT:
    case CQ_FAOP_NOT1:  case CQ_FAOP_AND1: case CQ_FAOP_OR1:
        return 1;
    case CQ_FAOP_PICK:
        if (rows[r->s0].op == CQ_FAOP_SUBNORM)
            return r->shift == CQ_FA_PICK_FLAG || r->shift == CQ_FA_PICK_FTZ;
        if (rows[r->s0].op == CQ_FAOP_ROUND)
            return r->shift == CQ_FA_PICK_EXPOVF
                || r->shift == CQ_FA_PICK_EXPOVFA;
        return 0;
    default: break;
    }
    return 0;
}

/* Which operand slots of an op are ONE bit. Read off fadd.h's op table rather
 * than off the machine. */
static int fa_operand_is_flag(int op, int j)
{
    if (op == CQ_FAOP_MUX)  return j == 0;
    if (op == CQ_FAOP_NOT1) return 1;
    if (op == CQ_FAOP_AND1 || op == CQ_FAOP_OR1) return 1;
    return 0;
}

CQ_TEST(the_two_programs_are_well_formed_and_reference_only_backwards)
{
    static const int WANT_ROWS[2] = { 124, 133 };
    int n_body = 0, n_pre = 0;

    (void)cq_fadd_body_rows(&n_body);
    (void)cq_fsub_prologue_rows(&n_pre);
    CHECK_EQ(n_body, 124);
    CHECK_EQ(n_pre, 9);

    for (int p = 0; p < N_FADD_ROWS; p++) {
        cq_fadd_row rows[CQ_FADD_MAX_ROWS];
        int n = cq_fadd_program(ROWS[p].prog, rows);
        int n_view = 0, n_pick = 0, n_emit = 0, n_handoff = 0;

        CHECK_EQ(n, WANT_ROWS[p]);
        CHECK_EQ(cq_fadd_result_row(ROWS[p].prog), n - 1);

        for (int i = 0; i < n; i++) {
            const short s[3] = { rows[i].s0, rows[i].s1, rows[i].s2 };
            int arity = cq_fadd_arity(rows[i].op);

            if (rows[i].op == CQ_FAOP_VIEW) n_view++;
            else if (rows[i].op == CQ_FAOP_PICK) n_pick++;
            else n_emit++;
            if (rows[i].op == CQ_FAOP_CLZ || rows[i].op == CQ_FAOP_SUBNORM
                || rows[i].op == CQ_FAOP_ROUND) n_handoff++;

            for (int j = 0; j < arity; j++) {
                if (s[j] >= 0 && s[j] >= i)
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d operand %d names row %d — a row "
                              "reference must be strictly backwards",
                              ROWS[p].spec.name, i, j, (int)s[j]);
                if (s[j] < 0) {
                    if (s[j] < -CQ_FA_N_CODE)
                        cq_h_fail(__FILE__, __LINE__,
                                  "%s row %d operand %d: code %d is outside "
                                  "the vocabulary", ROWS[p].spec.name, i, j,
                                  (int)s[j]);
                    continue;
                }
                if (fa_operand_is_flag(rows[i].op, j)
                    != fa_is_flag_row(rows, s[j]))
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d operand %d: a %s operator is handed a "
                              "%s row", ROWS[p].spec.name, i, j,
                              fa_operand_is_flag(rows[i].op, j) ? "1-bit"
                                                               : "64-lane",
                              fa_is_flag_row(rows, s[j]) ? "1-bit" : "64-lane");
            }
        }

        /* THE COUNTS ARE THE PORT'S OWN CONTENT (K15.md §3.1, re-counted under
         * the views amendment and M32's four blocks): fadd.jl is 124 operator
         * occurrences of which 19 are views and 11 are picks, and exactly
         * THREE of them are hand-offs to M32 — one `_sf_normalize_clz`, one
         * `_sf_handle_subnormal`, one `_sf_round_and_pack`. A fourth would
         * mean a helper transcribed here that M32 already owns. */
        CHECK_EQ(n_view, p == 0 ? 19 : 22);
        CHECK_EQ(n_pick, 11);
        CHECK_EQ(n_emit, p == 0 ? 94 : 100);
        CHECK_EQ(n_handoff, 3);
    }
}

/* ---- (2) `fsub` is `fadd`'s body under a prologue, with `b` rebased. ---- */

CQ_TEST(fsub_is_fadds_body_under_a_prologue_and_b_is_rebased)
{
    cq_fadd_row add[CQ_FADD_MAX_ROWS], sub[CQ_FADD_MAX_ROWS];
    int na = cq_fadd_program(CQ_FADD_PROG_ADD, add);
    int ns = cq_fadd_program(CQ_FADD_PROG_SUB, sub);
    int pre = ns - na, n_b = 0, n_a = 0;

    CHECK_EQ(pre, 9);

    /* The prologue's last row IS `b_eff` and every later `b` is it. */
    CHECK_EQ((int)sub[pre - 1].op, (int)CQ_FAOP_MUX);

    for (int i = 0; i < na; i++) {
        const short sa[3] = { add[i].s0, add[i].s1, add[i].s2 };
        const short ss[3] = { sub[pre + i].s0, sub[pre + i].s1, sub[pre + i].s2 };
        int arity = cq_fadd_arity(add[i].op);

        CHECK_EQ((int)sub[pre + i].op, (int)add[i].op);
        CHECK_EQ((int)sub[pre + i].shift, (int)add[i].shift);
        CHECK(sub[pre + i].mask == add[i].mask);

        for (int j = 0; j < arity; j++) {
            short want;

            if (sa[j] >= 0)               want = (short)(sa[j] + pre);
            else if (sa[j] == CQ_FA_B)  { want = (short)(pre - 1); n_b++; }
            else                        { want = sa[j];
                                          if (sa[j] == CQ_FA_A) n_a++; }
            if (ss[j] != want)
                cq_h_fail(__FILE__, __LINE__,
                          "fsub row %d operand %d is %d, the rebase of fadd "
                          "row %d says %d", pre + i, j, (int)ss[j], i,
                          (int)want);
        }
    }

    /* NOT VACUOUS, AND THE COUNTS ARE THE POINT. `b` occurs TEN times in
     * soft_fadd — three unpack views (:24, :25, :26), three class predicates
     * (:30, :32, :34), `b_mag` (:48), `inf_finite_b` (:42), the `a_zero` arm
     * of :129 and `b | QUIET_BIT` at :133 — and every one of them must see `b_eff`. The
     * last two are `Bennett-m63k` itself: if the propagated NaN came from the
     * raw `b` the sign would be right by accident and the value wrong, and if
     * it came from a separately negated `b` the sign would be wrong. */
    CHECK_EQ(n_b, 10);
    CHECK_EQ(n_a, 12);
}

/* ---- (3) `fsub` against `fadd(a, fneg(b))` — the Bennett-m63k cell. ----- */

/* fsub.jl:8-17 names the bug, the fix and the test that caught it: "Bennett-
 * r84x's NaN canonicalisation audit explicitly skipped fsub on the assumption
 * that fadd∘fneg would inherit correct NaN handling; test_m63k_softfloat_
 * strict_bits.jl Layer 2 caught the resulting sign-flip on `normal − NaN`."
 *
 * SO THE TWO SPELLINGS DIFFER ON EXACTLY ONE CELL, AND THIS ASSERTS BOTH
 * HALVES: they differ wherever `b` is a NaN and `a` is not, and they agree
 * everywhere else. The `a` clause matters — with both operands NaN the
 * first-operand rule returns `a` either way, so a test that only asserted
 * "differ when b is a NaN" would go red on a correct port. */
CQ_TEST(fsub_differs_from_fadd_of_fneg_on_exactly_the_nan_rhs_sign)
{
    uint64_t seed = 0x243F6A8885A308D3ull;
    int n_differ = 0, n_agree = 0, n_anch = fadd_anchors(CQ_FP64_W, -1, NULL);
    cq_mock m;

    cq_mock_init(&m);

    for (int k = -n_anch; k < 6000; k++) {
        uint64_t a, b;
        int want_differ;

        if (k < 0) {
            cq_ref_w v[CQ_KD_MAX_SRC];

            v[0] = cq_ref_w_zero(); v[1] = cq_ref_w_zero();
            v[2] = cq_ref_w_zero();
            CHECK_EQ(fadd_anchors(CQ_FP64_W, k + n_anch, v), 1);
            a = v[0].lo; b = v[1].lo;
        } else {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            a = seed ^ (seed >> 29);
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            b = seed ^ (seed >> 29);
            if ((k & 3) == 0) b = (b & ~CQ_F64_EXP_MASK) | CQ_F64_EXP_MASK;
            if ((k & 7) == 1) a = (a & ~CQ_F64_EXP_MASK) | CQ_F64_EXP_MASK;
        }

        want_differ = fa_is_nan(b) && !fa_is_nan(a);
        if ((cq_fsub_eval(a, b) != cq_fadd_eval(a, b ^ FA_SIGN)) != want_differ)
            cq_h_fail(__FILE__, __LINE__,
                      "fsub(0x%016llx, 0x%016llx) = 0x%016llx vs "
                      "fadd(a, fneg(b)) = 0x%016llx: %s",
                      (unsigned long long)a, (unsigned long long)b,
                      (unsigned long long)cq_fsub_eval(a, b),
                      (unsigned long long)cq_fadd_eval(a, b ^ FA_SIGN),
                      want_differ ? "they should differ on a NaN rhs"
                                  : "they should agree");
        if (want_differ) n_differ++; else n_agree++;
    }

    /* AND THROUGH THE CIRCUIT, which is where it matters: `normal - NaN` on a
     * NEGATIVE payload NaN, at the all-quantum mask. The two spellings return
     * bit patterns differing in exactly bit 63. */
    {
        uint64_t nb = CQ_F64_NEG(CQ_F64_QNAN_A);
        cq_ref_w q = cq_ref_w_ones(CQ_FP64_W);
        uint64_t got_sub = fadd_run(&ROWS[1], CQ_F64_ONE, nb, q, q, &m);
        uint64_t got_neg = fadd_run(&ROWS[0], CQ_F64_ONE, nb ^ FA_SIGN, q, q, &m);

        CHECK_EQ(got_sub, nb | CQ_F64_QUIET_BIT);
        CHECK_EQ(got_sub ^ got_neg, FA_SIGN);
        CHECK_EQ(got_sub, host_fsub(CQ_F64_ONE, nb));
    }

    CHECK(n_differ > 0);
    CHECK(n_agree > 0);
    cq_mock_dispose(&m);
}

/* ---- (4) The classical row against the host, broadly. ------------------- */

/* PRD-v2 §7.4 DECIDES THE SHORT-CIRCUIT IS A C TRANSCRIPTION OF THE JULIA AND
 * NOT THE HOST OPERATOR, so the two must agree by construction rather than by
 * a shared belief about IEEE — and this is where that is checked directly,
 * over every anchor and over a seeded pattern sweep. It runs NO circuit, so it
 * is cheap enough to be broad, and it is the only case that would notice a
 * transcription that is right on the specials and wrong in the middle. */
CQ_TEST(the_classical_row_agrees_with_the_host_on_every_anchor)
{
    int n_anch = fadd_anchors(CQ_FP64_W, -1, NULL);
    uint64_t seed = 0x9E3779B97F4A7C15ull;
    int n_specials = 0, n_ordinary = 0;

    for (int j = 0; j < n_anch; j++) {
        cq_ref_w v[CQ_KD_MAX_SRC];

        v[0] = cq_ref_w_zero(); v[1] = cq_ref_w_zero(); v[2] = cq_ref_w_zero();
        CHECK_EQ(fadd_anchors(CQ_FP64_W, j, v), 1);

        for (int i = 0; i < N_FADD_ROWS; i++)
            CHECK_EQ(ROWS[i].eval(v[0].lo, v[1].lo),
                     ROWS[i].host(v[0].lo, v[1].lo));
    }

    /* A SEEDED SWEEP OVER WHOLE-WORD PATTERNS. Uniform 64-bit draws are almost
     * all ordinary normals of wild magnitude — exactly the region the anchors
     * do NOT cover — so the exponent is forced into the special range on some
     * draws and both operands are pushed into the same binade on others, which
     * is where the alignment, the cancellation and the rounding live. */
    for (int k = 0; k < 20000; k++) {
        uint64_t a, b;

        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        a = seed ^ (seed >> 29);
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        b = seed ^ (seed >> 29);
        if ((k & 7) == 0) { a = (a & ~CQ_F64_EXP_MASK) | CQ_F64_EXP_MASK;
                            n_specials++; }
        if ((k & 7) == 1) { b = (b & ~CQ_F64_EXP_MASK) | CQ_F64_EXP_MASK;
                            n_specials++; }
        if ((k & 7) == 2) a &= 0x800fffffffffffffull;     /* subnormal */
        if ((k & 7) == 3) b &= 0x800fffffffffffffull;
        if ((k & 7) >= 4) { b = (b & 0x800fffffffffffffull)
                              | (a & CQ_F64_EXP_MASK);    /* same binade */
                            n_ordinary++; }

        for (int i = 0; i < N_FADD_ROWS; i++) {
            uint64_t got = ROWS[i].eval(a, b), want = ROWS[i].host(a, b);

            if (got != want)
                cq_h_fail(__FILE__, __LINE__,
                          "%s(0x%016llx, 0x%016llx): the C transcription says "
                          "0x%016llx, the host says 0x%016llx",
                          ROWS[i].spec.name, (unsigned long long)a,
                          (unsigned long long)b, (unsigned long long)got,
                          (unsigned long long)want);
        }
    }

    CHECK(n_specials > 0);
    CHECK(n_ordinary > 0);
}

/* ---- (5) §7.4's three reachable cells, as LITERALS, in three modes. ----- */

CQ_TEST(the_three_unspecified_cells_are_the_pinned_literals)
{
    cq_ref_w q = cq_ref_w_ones(CQ_FP64_W);
    cq_mock m;

    cq_mock_init(&m);

    /* `Inf - Inf` -> INDEF, which is NEGATIVE (softfloat_common.jl:14, Intel
     * SDM Vol 1 §4.8.3.7). ARM's default NaN is positive, so this literal is
     * what makes an arm64 box disagree LOUDLY rather than quietly. */
    CHECK_EQ(cq_fadd_eval(CQ_F64_POS_INF, CQ_F64_NEG_INF), CQ_FPHOST_INDEF);
    CHECK_EQ(cq_fsub_eval(CQ_F64_POS_INF, CQ_F64_POS_INF), CQ_FPHOST_INDEF);
    CHECK_EQ(host_fadd(CQ_F64_POS_INF, CQ_F64_NEG_INF), CQ_FPHOST_INDEF);
    CHECK_EQ(fadd_run(&ROWS[0], CQ_F64_POS_INF, CQ_F64_NEG_INF, q, q, &m),
             CQ_FPHOST_INDEF);

    /* TWO NaNs: the FIRST operand's payload wins, in both orders. A table
     * whose two NaNs were equal could not see a swap (fpanchors.h). */
    CHECK_EQ(cq_fadd_eval(CQ_F64_QNAN_A, CQ_F64_QNAN_B), CQ_F64_QNAN_A);
    CHECK_EQ(cq_fadd_eval(CQ_F64_QNAN_B, CQ_F64_QNAN_A), CQ_F64_QNAN_B);
    CHECK_EQ(fadd_run(&ROWS[0], CQ_F64_QNAN_A, CQ_F64_QNAN_B, q, q, &m),
             CQ_F64_QNAN_A);

    /* sNaN vs qNaN AT A NON-DEGENERATE PAYLOAD PAIR: the first operand wins,
     * QUIETENED if it was signalling. Paired against QNAN_B and not QNAN_A
     * because quietening CQ_F64_SNAN gives exactly QNAN_A, and on that pair
     * "the qNaN won" and "the first operand won" print the same bits. */
    CHECK_EQ(cq_fadd_eval(CQ_F64_SNAN, CQ_F64_QNAN_B), CQ_F64_SNAN_QUIET);
    CHECK_EQ(cq_fadd_eval(CQ_F64_QNAN_B, CQ_F64_SNAN), CQ_F64_QNAN_B);
    CHECK(CQ_F64_SNAN_QUIET != CQ_F64_QNAN_B);
    CHECK_EQ(fadd_run(&ROWS[0], CQ_F64_SNAN, CQ_F64_QNAN_B, q, q, &m),
             CQ_F64_SNAN_QUIET);

    /* AND `fsub`'s NaN-`b` KEEPS ITS SIGN — the m63k cell again, as a literal
     * rather than as a comparison against the other spelling. */
    CHECK_EQ(cq_fsub_eval(CQ_F64_ONE, CQ_F64_NEG(CQ_F64_QNAN_A)),
             CQ_F64_NEG(CQ_F64_QNAN_A) | CQ_F64_QUIET_BIT);

    cq_mock_dispose(&m);
}

/* ---- (6) The G/R/S working format handed to M32. ----------------------- */

/* fadd.jl:13-14 IS A CONTRACT NO C TYPE EXPRESSES (K23 §1.7, K15.md §6.5 risk
 * 3): "bit 55 = implicit 1, bits 54-3 = 52-bit fraction, bits 2/1/0 = G/R/S."
 * Build `wr` one lane off and every input that does not round is still right,
 * so the ONLY detector is a set of inputs whose answer depends on the three
 * low bits. These four do, and they separate the three bits from each other:
 * the first pair moves only the ROUND bit's tie-break (`frac & 1`), and the
 * second pair moves only the STICKY bit. All four expectations come from the
 * HOST, which shares nothing with the encoding. */
CQ_TEST(the_working_format_handed_to_m32_is_grs_in_bits_2_1_0)
{
    static const struct { uint64_t a, b; } TIES[] = {
        { CQ_F64_ONE,          CQ_F64_HALF_ULP_OF_1 },  /* G=1 R=0 S=0, even */
        { CQ_F64_ONE_PLUS_ULP, CQ_F64_HALF_ULP_OF_1 },  /* G=1 R=0 S=0, odd  */
        { CQ_F64_ONE,          FA_STICKY            },  /* G=1 R=0 S=1       */
        { CQ_F64_ONE_PLUS_ULP, FA_STICKY            }   /* G=1 R=0 S=1, odd  */
    };
    cq_ref_w q = cq_ref_w_ones(CQ_FP64_W), z = cq_ref_w_zero();
    cq_mock m;
    int n = (int)(sizeof TIES / sizeof TIES[0]);
    int moved_by_parity = 0, moved_by_sticky = 0;

    cq_mock_init(&m);

    for (int k = 0; k < n; k++)
        for (int side = 0; side < 2; side++) {
            uint64_t want = host_fadd(TIES[k].a, TIES[k].b);

            CHECK_EQ(cq_fadd_eval(TIES[k].a, TIES[k].b), want);
            CHECK_EQ(fadd_run(&ROWS[0], TIES[k].a, TIES[k].b,
                              side ? q : z, side ? z : q, &m), want);
        }

    /* NOT VACUOUS IN EITHER DIRECTION. The tie-break must actually MOVE the
     * answer with the significand's parity, and the sticky bit must actually
     * MOVE it too, or the four rows above would pass against a kernel that
     * never rounded at all. */
    moved_by_parity = (host_fadd(TIES[0].a, TIES[0].b) == TIES[0].a)   /* down */
                   && (host_fadd(TIES[1].a, TIES[1].b) != TIES[1].a);  /* up   */
    moved_by_sticky = (host_fadd(TIES[2].a, TIES[2].b)
                       != host_fadd(TIES[0].a, TIES[0].b));
    CHECK(moved_by_parity);
    CHECK(moved_by_sticky);

    cq_mock_dispose(&m);
}

/* ---- (7) The D8 band is DISCARDED, and the select that discards it runs.  */

/* PRD-v2 §7.6's row for fadd.jl:79: `d` is unclamped and reaches 2046, our
 * barrel shifts by `d mod 64`, and the divergence never reaches a value
 * because :81-83 selects `wb_mid` only for `0 < d < 56`. THE AUDIT'S OWN
 * CONCLUSION IS WHAT STOPS ANYONE LOOKING (K15.md §6.5 risk 2), so the three
 * boundary values are driven through the CIRCUIT and the mid path is shown to
 * be live — if :81's select were mis-wired the divergence would go live and
 * only these rows would see it. */
CQ_TEST(the_d8_band_is_discarded_and_the_select_that_discards_it_is_live)
{
    static const uint64_t B[] = {
        FA_D56,                  /* d == 56 exactly — wb_large takes over  */
        FA_D64,                  /* d == 64 exactly — the D8 boundary      */
        CQ_F64_MIN_SUBNORMAL,    /* d far past 64                          */
        0x3fc0000000000000ull,   /* d == 3  — the mid path, and it matters */
        0x3c80000000000000ull    /* d == 55 — the last mid value           */
    };
    int n = (int)(sizeof B / sizeof B[0]);
    cq_ref_w q = cq_ref_w_ones(CQ_FP64_W);
    cq_mock m;
    int n_moved = 0;

    cq_mock_init(&m);

    for (int k = 0; k < n; k++) {
        uint64_t want = host_fadd(CQ_F64_ONE, B[k]);

        CHECK_EQ(cq_fadd_eval(CQ_F64_ONE, B[k]), want);
        CHECK_EQ(fadd_run(&ROWS[0], CQ_F64_ONE, B[k], q, q, &m), want);
        if (want != CQ_F64_ONE) n_moved++;
    }

    /* THE SELECT IS LIVE, AND THE ROWS ARE NAMED RATHER THAN COUNTED. The
     * three `d >= 56` rows come back as `a` itself, and so does the `d == 55`
     * row — an alignment below half an ulp does not move 1.0 whichever arm is
     * taken, which is exactly why a COUNT of "unchanged" rows says nothing
     * here. What says something is the `d == 3` row, where `wb_large` would
     * give 1.0 and `wb_mid` gives 1.125: :81's mux is choosing rather than
     * always taking one arm. A kernel that always took `wb_large` makes
     * n_moved zero; one that always took `wb_mid` is still right here, which
     * is why the D8 band's own rows are checked by VALUE above. */
    CHECK_EQ(host_fadd(CQ_F64_ONE, B[0]), CQ_F64_ONE);   /* d == 56 */
    CHECK_EQ(host_fadd(CQ_F64_ONE, B[1]), CQ_F64_ONE);   /* d == 64 */
    CHECK_EQ(host_fadd(CQ_F64_ONE, B[2]), CQ_F64_ONE);   /* d  > 64 */
    CHECK(host_fadd(CQ_F64_ONE, B[3]) != CQ_F64_ONE);    /* d ==  3 */
    CHECK_EQ(n_moved, 1);
    cq_mock_dispose(&m);
}
