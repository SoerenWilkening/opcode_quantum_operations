/* tests/test_kernel_fcmp_pred.inc — the PREDICATE TABLE's own instruments: the
 * fourteen-row dispatch, the ordered-vs-unordered family split, the classical
 * transcription against the host, the anchor budget, program well-formedness,
 * and the four operand swaps against their unswapped twin. Included by
 * test_kernel_fcmp.c.
 *
 * NOTHING HERE READS A GOLDEN. K18.md §6.4 ranks a wrong row of §1.1's table
 * as risk 1 precisely because every structural instrument in this project is
 * blind to it — K9's `uge`-meaning-`ule` emitted the identical tuple, kept the
 * palindrome and left scratch clean — so the cases below are the ones that
 * `CQOPS_UPDATE_GOLDENS=1` cannot bless.
 */

/* ---- One forward call, at an explicit value and mask pair. -------------- */

static uint64_t fcmp_run(const fcmp_krow *r, uint64_t va, uint64_t vb,
                         cq_ref_w qa, cq_ref_w qb, cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_kd_shape sh;
    int32_t ha, hb, hd;
    const cq_bit *src[2];
    uint64_t got;

    fcmp_shape(CQ_FP64_W, &sh);
    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);

    ha = cq_bk_reg_w(&ctx, (uint32_t)CQ_FP64_W,
                     cq_ref_w_make(va, 0u, CQ_FP64_W), qa);
    hb = cq_bk_reg_w(&ctx, (uint32_t)CQ_FP64_W,
                     cq_ref_w_make(vb, 0u, CQ_FP64_W), qb);
    hd = cq_reg_alloc_zero(&ctx.regs, 1u);
    src[0] = cq_reg_cbits(&ctx.regs, ha);
    src[1] = cq_reg_cbits(&ctx.regs, hb);

    cq_mock_reset(m);                   /* drop the rails' materialising Xs */
    r->spec.call(&ctx, cq_reg_bits(&ctx.regs, hd), src, &sh);
    got = cq_pc_value(&ctx, hd);
    cq_ctx_dispose(&ctx);
    return got;
}

/* ---- (1) instructions.jl:7698-7730, transcribed a SECOND time. ---------- */

/* READ STRAIGHT OFF THE PINNED EXTRACTOR AND SHARING NOTHING WITH
 * fcmp_pred.c's DISPATCH[]. The K9 precedent is `cq_ref_icmp`, which
 * transcribes `lower_icmp!`'s ten rows again rather than calling the kernel's
 * own table. The two columns that matter are the CALLEE and the SWAP; the
 * third is the name, so a mis-ordered enum shows up as a name mismatch rather
 * than as a silently permuted table. */
static const struct { cq_fcmp_pred p, callee; int swap; const char *name; }
DISPATCH_REF[] = {
    { CQ_FCMP_OEQ, CQ_FCMP_OEQ, 0, "oeq" },   /* :7700-7701 */
    { CQ_FCMP_OGT, CQ_FCMP_OLT, 1, "ogt" },   /* :7706-7708 */
    { CQ_FCMP_OGE, CQ_FCMP_OLE, 1, "oge" },   /* :7709-7711 */
    { CQ_FCMP_OLT, CQ_FCMP_OLT, 0, "olt" },   /* :7698-7699 */
    { CQ_FCMP_OLE, CQ_FCMP_OLE, 0, "ole" },   /* :7702-7703 */
    { CQ_FCMP_ONE, CQ_FCMP_ONE, 0, "one" },   /* :7713-7714 */
    { CQ_FCMP_ORD, CQ_FCMP_ORD, 0, "ord" },   /* :7715-7716 */
    { CQ_FCMP_UNO, CQ_FCMP_UNO, 0, "uno" },   /* :7717-7718 */
    { CQ_FCMP_UEQ, CQ_FCMP_UEQ, 0, "ueq" },   /* :7719-7720 */
    { CQ_FCMP_UGT, CQ_FCMP_ULT, 1, "ugt" },   /* :7721-7723 */
    { CQ_FCMP_UGE, CQ_FCMP_ULE, 1, "uge" },   /* :7724-7726 */
    { CQ_FCMP_ULT, CQ_FCMP_ULT, 0, "ult" },   /* :7727-7728 */
    { CQ_FCMP_ULE, CQ_FCMP_ULE, 0, "ule" },   /* :7729-7730 */
    { CQ_FCMP_UNE, CQ_FCMP_UNE, 0, "une" }    /* :7704-7705 */
};

CQ_TEST(the_dispatch_is_four_swaps_and_zero_inversions)
{
    int n_swap = 0, n_self = 0;

    for (int i = 0; i < N_FCMP_ROWS; i++) {
        CHECK_EQ((int)DISPATCH_REF[i].p, i);          /* the enum's order    */
        CHECK_EQ((int)cq_fcmp_callee(DISPATCH_REF[i].p),
                 (int)DISPATCH_REF[i].callee);
        CHECK_EQ(cq_fcmp_swapped(DISPATCH_REF[i].p), DISPATCH_REF[i].swap);
        CHECK(strcmp(cq_fcmp_name(DISPATCH_REF[i].p),
                     DISPATCH_REF[i].name) == 0);

        n_swap += DISPATCH_REF[i].swap;
        n_self += (DISPATCH_REF[i].callee == DISPATCH_REF[i].p);
    }

    /* EXACTLY FOUR SWAP AND EXACTLY TEN ARE THEIR OWN CALLEE — softfloat.jl's
     * export list is ten `soft_fcmp_*` and the grid is fourteen. */
    CHECK_EQ(n_swap, 4);
    CHECK_EQ(n_self, 10);

    /* THE SWAP SET IS NOT K9's, AND THE INTERSECTION IS {ugt} ALONE. K9 swaps
     * {ugt, ule, sgt, sle}; this table swaps {ogt, oge, ugt, uge}. `ule` is
     * the near-miss: swapped there, NOT swapped here. */
    CHECK_EQ(cq_fcmp_swapped(CQ_FCMP_UGT), 1);
    CHECK_EQ(cq_fcmp_swapped(CQ_FCMP_ULE), 0);

    /* AND `oge` IS `ole(b,a)`, NOT `olt(b,a)` — the pairing that differs only
     * on a == b and is invisible to every structural check. */
    CHECK_EQ((int)cq_fcmp_callee(CQ_FCMP_OGE), (int)CQ_FCMP_OLE);
    CHECK_EQ((int)cq_fcmp_callee(CQ_FCMP_UGE), (int)CQ_FCMP_ULE);

    /* A swapped row's program IS its callee's, byte for byte: the swap is an
     * argument-order change at the entry point and never a circuit. */
    for (int i = 0; i < N_FCMP_ROWS; i++) {
        cq_fcmp_row a[CQ_FCMP_MAX_ROWS], b[CQ_FCMP_MAX_ROWS];
        int na = cq_fcmp_program(DISPATCH_REF[i].p, a);
        int nb = cq_fcmp_program(DISPATCH_REF[i].callee, b);

        CHECK_EQ(na, nb);
        CHECK(memcmp(a, b, (size_t)na * sizeof a[0]) == 0);
    }
}

/* ---- (2) The NaN column, as LITERALS. ---------------------------------- */

/* §7.12's "for `fcmp`, one unordered pair per predicate" — FOURTEEN
 * assertions, not one, and the only place in this suite where the answer is
 * written down rather than computed. Every `o*` predicate is (the ordered
 * relation) AND NOT unordered and every `u*` is (the ordered relation) OR
 * unordered, so an unordered pair gives 0 for the seven `o*` rows and 1 for
 * the seven `u*` rows — the family split, and the one fact a host oracle
 * agreeing with a wrong family split could not catch. */
static const int NAN_ROW[] = { 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1 };

_Static_assert(sizeof NAN_ROW / sizeof NAN_ROW[0] == 14u,
               "one literal per LLVM predicate");

CQ_TEST(the_unordered_row_is_the_literal_o_and_u_family_split)
{
    static const uint64_t NAN_PAIRS[][2] = {
        { CQ_F64_DEFAULT_NAN, CQ_F64_ONE           },
        { CQ_F64_ONE,         CQ_F64_DEFAULT_NAN   },
        { CQ_F64_QNAN_A,      CQ_F64_QNAN_B        },
        { CQ_F64_SNAN,        CQ_F64_QNAN_B        },
        { CQ_F64_QNAN_A,      CQ_F64_QNAN_A        },
        { CQ_F64_DEFAULT_NAN, CQ_F64_POS_INF       }
    };
    int np = (int)(sizeof NAN_PAIRS / sizeof NAN_PAIRS[0]);
    cq_mock m;

    cq_mock_init(&m);

    for (int k = 0; k < np; k++)
        for (int i = 0; i < N_FCMP_ROWS; i++) {
            uint64_t a = NAN_PAIRS[k][0], b = NAN_PAIRS[k][1];
            uint64_t want = (uint64_t)NAN_ROW[i];

            /* The host agrees with the literal — IEEE specifies every one of
             * these, so this is a claim about the box as well as about us. */
            CHECK_EQ((uint64_t)host_fcmp(a, b, ROWS[i].pred), want);

            /* The CLASSICAL row (R9's short-circuit, a C transcription). */
            CHECK_EQ(cq_fcmp_eval(a, b, ROWS[i].pred), want);

            /* And the CIRCUIT, with both operands on qubits — the path the
             * short-circuit never takes. */
            CHECK_EQ(fcmp_run(&ROWS[i], a, b, cq_ref_w_ones(CQ_FP64_W),
                              cq_ref_w_ones(CQ_FP64_W), &m), want);
        }

    /* sNaN IS A NaN TO ALL FOURTEEN, because fcmp.jl never consults
     * QUIET_BIT: the exponent is extracted by shift-and-mask and the quiet bit
     * is not read anywhere in the file. A port that "helpfully" tested it
     * would classify an sNaN as ordered, and only this row sees that. */
    CHECK_EQ(cq_fcmp_eval(CQ_F64_SNAN, CQ_F64_ONE, CQ_FCMP_UNO), 1u);
    CHECK_EQ(cq_fcmp_eval(CQ_F64_SNAN, CQ_F64_ONE, CQ_FCMP_ORD), 0u);

    /* AND AN INFINITY IS NOT: `ea == 0x7FF` holds and `fa != 0` does not, so
     * deleting the fraction conjunct turns every Inf into a NaN. */
    CHECK_EQ(cq_fcmp_eval(CQ_F64_POS_INF, CQ_F64_POS_INF, CQ_FCMP_OEQ), 1u);
    CHECK_EQ(cq_fcmp_eval(CQ_F64_NEG_INF, CQ_F64_POS_INF, CQ_FCMP_OLT), 1u);
    CHECK_EQ(cq_fcmp_eval(CQ_F64_POS_INF, CQ_F64_ONE, CQ_FCMP_ORD), 1u);

    /* `both_zero` IS AN OVERRIDE IN `olt` (:44) AND A DISJUNCT IN `oeq`
     * (:76) — opposite polarities in two adjacent functions, and the single
     * most load-bearing asymmetry in the file. Neither error moves a gate. */
    CHECK_EQ(cq_fcmp_eval(CQ_F64_NEG_ZERO, CQ_F64_POS_ZERO, CQ_FCMP_OLT), 0u);
    CHECK_EQ(cq_fcmp_eval(CQ_F64_POS_ZERO, CQ_F64_NEG_ZERO, CQ_FCMP_OEQ), 1u);
    CHECK_EQ(cq_fcmp_eval(CQ_F64_POS_ZERO, CQ_F64_NEG_ZERO, CQ_FCMP_ONE), 0u);
    CHECK_EQ(cq_fcmp_eval(CQ_F64_POS_ZERO, CQ_F64_NEG_ZERO, CQ_FCMP_UNE), 0u);

    cq_mock_dispose(&m);
}

/* ---- (3) The classical row against the host, broadly. ------------------- */

/* PRD-v2 §7.4 DECIDES THE SHORT-CIRCUIT IS A C TRANSCRIPTION OF THE JULIA AND
 * NOT THE HOST OPERATOR, so the two must agree by construction rather than by
 * a shared belief about IEEE — and this is where that is checked directly,
 * over every anchor and over a seeded pattern sweep. It runs NO circuit, so it
 * is cheap enough to be broad, and it is the only case that would notice a
 * transcription that is right on the specials and wrong in the middle. */
CQ_TEST(the_classical_row_agrees_with_the_host_on_every_anchor)
{
    int n_anch = fcmp_anchors(CQ_FP64_W, -1, NULL);
    uint64_t seed = 0x9E3779B97F4A7C15ull;
    int n_true = 0, n_false = 0;

    for (int j = 0; j < n_anch; j++) {
        cq_ref_w v[CQ_KD_MAX_SRC];

        v[0] = cq_ref_w_zero(); v[1] = cq_ref_w_zero(); v[2] = cq_ref_w_zero();
        CHECK_EQ(fcmp_anchors(CQ_FP64_W, j, v), 1);

        for (int i = 0; i < N_FCMP_ROWS; i++)
            CHECK_EQ(cq_fcmp_eval(v[0].lo, v[1].lo, ROWS[i].pred),
                     (uint64_t)host_fcmp(v[0].lo, v[1].lo, ROWS[i].pred));
    }

    /* A SEEDED SWEEP OVER WHOLE-WORD PATTERNS. Uniform 64-bit draws are almost
     * all ordinary normals of wild magnitude, which is exactly the region the
     * anchors do NOT cover; the exponent is forced into the special range on
     * every fourth draw so NaNs and infinities keep arriving too. */
    for (int k = 0; k < 4000; k++) {
        uint64_t a, b;

        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        a = seed ^ (seed >> 29);
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        b = seed ^ (seed >> 29);
        if ((k & 3) == 0) a = (a & ~CQ_F64_EXP_MASK) | CQ_F64_EXP_MASK;
        if ((k & 3) == 1) b = (b & ~CQ_F64_EXP_MASK) | CQ_F64_EXP_MASK;

        for (int i = 0; i < N_FCMP_ROWS; i++) {
            uint64_t want = (uint64_t)host_fcmp(a, b, ROWS[i].pred);

            if (cq_fcmp_eval(a, b, ROWS[i].pred) != want)
                cq_h_fail(__FILE__, __LINE__,
                          "%s(0x%016llx, 0x%016llx): the C transcription says "
                          "%llu, the host says %llu", ROWS[i].spec.name,
                          (unsigned long long)a, (unsigned long long)b,
                          (unsigned long long)cq_fcmp_eval(a, b, ROWS[i].pred),
                          (unsigned long long)want);
            if (want) n_true++; else n_false++;
        }
    }

    /* NOT VACUOUS IN EITHER DIRECTION: a transcription stuck at 0 or at 1
     * would agree with a host oracle nowhere, but a draw that never produced
     * both answers would not be evidence of that. */
    CHECK(n_true > 0);
    CHECK(n_false > 0);
}

/* ---- (4) The anchor budget, and every anchor through the KERNEL. -------- */

CQ_TEST(every_anchor_reaches_the_kernel_and_the_drop_is_printed)
{
    cq_bk_pair pairs[CQ_FP64_W + 12];
    uint32_t np = cq_bk_fixed_pairs((uint32_t)CQ_FP64_W, pairs,
                                    (uint32_t)(CQ_FP64_W + 12));
    cq_kd_shape sh;
    const char *src = "";
    int n_anch = fcmp_anchors(CQ_FP64_W, -1, NULL);
    int budget, room, want, have;

    fcmp_shape(CQ_FP64_W, &sh);
    budget = cq_kd_budget(sh.min_samples, &src);
    room   = budget > 2 ? budget - 2 : 0;
    want   = n_anch * 3;
    have   = want < room ? want : room;

    CHECK_EQ(n_anch, cq_fp_anchors_binary_count() + N_FCMP_PAIRS);
    CHECK(n_anch > cq_fp_anchors_binary_count());   /* the composition is real */
    CHECK_EQ(fcmp_anchors(8, -1, NULL), 0);         /* f64 only               */

    /* The floor TRACKS the provider: adding a pair to the .inc must widen the
     * budget with no edit here, so this is an identity and not a literal. */
    CHECK_EQ(sh.min_samples, 3 * n_anch + 8);

    printf("# fcmp anchors: %d pairs x 3 mask rows = %d wanted, %d run through "
           "the sampler, %d DROPPED at budget %d [%s, shape floor %d] — and "
           "all %d are run explicitly below at the ALL-QUANTUM mask, which is "
           "the one row the sampler's anchor block does not carry\n",
           n_anch, want, have, want - have, budget, src, sh.min_samples,
           n_anch);
    fflush(stdout);

    /* 0 DROPPED — the whole point of the floor (bd 9ve.32). */
    if (strcmp(src, "env") != 0) CHECK_EQ(have, want);

    for (int i = 0; i < N_FCMP_ROWS; i++)
        for (int j = 0; j < n_anch; j++) {
            cq_ref_w v[CQ_KD_MAX_SRC];

            v[0] = cq_ref_w_zero();
            v[1] = cq_ref_w_zero();
            v[2] = cq_ref_w_zero();
            CHECK(fcmp_anchors(CQ_FP64_W, j, v) == 1);
            cq_kd_case(&ROWS[i].spec, CQ_FP64_W, v, &pairs[1]); /* all-quantum */
        }

    CHECK(np >= 2u);
}

/* ---- (5) Program well-formedness, and D8's unreachability. ------------- */

/* THE D8 ROW IS "fcmp.jl HAS NO VARIABLE-AMOUNT SHIFT", AND THERE IS NOTHING
 * TO ASSERT ABOUT D8 ITSELF (K18.md §5.1 records the audit: eight `>>`
 * occurrences, all by an integer literal, no `<<` at all). What IS assertable
 * is the structural consequence — the op vocabulary has no shift member, so
 * M12's barrel is unreachable from here and D8's mask-then-saturate divergence
 * cannot arise — together with the program invariants a mis-built table would
 * break: every operand is either an earlier row or one of the eight named
 * codes, a 1-bit operator never takes a 64-lane code, and the class block only
 * ever sees a whole rail. */
static int scan_arity(int op)
{
    switch (op) {
    case CQ_FCOP_CLASS_NAN: case CQ_FCOP_NOT1:                     return 1;
    case CQ_FCOP_EQ: case CQ_FCOP_ULT: case CQ_FCOP_SUB:
    case CQ_FCOP_AND1: case CQ_FCOP_OR1:                           return 2;
    default:                                                       return 3;
    }
}

static int op_is_wide(int op)
{
    return op == CQ_FCOP_CLASS_NAN || op == CQ_FCOP_EQ
        || op == CQ_FCOP_ULT       || op == CQ_FCOP_SUB;
}

CQ_TEST(the_programs_are_well_formed_and_reach_no_variable_shift)
{
    int n_wide = 0, n_narrow = 0;

    /* No shift op exists in the vocabulary at all. A renumbering or an added
     * member must break the build, not just a comment. */
    CHECK_EQ((int)CQ_FCOP_N_OP, 8);

    for (int i = 0; i < N_FCMP_ROWS; i++) {
        cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];
        int n = cq_fcmp_program(ROWS[i].pred, rows);

        CHECK(n > 0 && n <= CQ_FCMP_MAX_ROWS);

        for (int r = 0; r < n; r++) {
            short s[3] = { rows[r].s0, rows[r].s1, rows[r].s2 };
            int wide = op_is_wide(rows[r].op);

            CHECK(rows[r].op >= 0 && rows[r].op < (int)CQ_FCOP_N_OP);

            for (int j = 0; j < scan_arity(rows[r].op); j++) {
                if (s[j] >= 0) {
                    /* A row may only read a STRICTLY EARLIER row: the program
                     * is a straight line and the step dispatch runs it in
                     * order, so a forward reference would read a wire that has
                     * not been built yet and would still emit a plausible
                     * gate. */
                    CHECK(s[j] < r);
                    n_narrow++;
                } else {
                    CHECK(s[j] >= CQ_FC_K_ONE && s[j] <= CQ_FC_A);
                    /* A 1-bit operator never takes a 64-lane code, and a wide
                     * one may take either (a row reference is zero-extended to
                     * the Julia type's width — fcmp.jl:102's subtrahend). */
                    CHECK(wide);
                    n_wide++;
                }
            }
            /* The class block is `(ea == 0x7FF) & (fa != 0)` over a WHOLE
             * operand; a view or a constant there would be a different
             * predicate wearing the same name. */
            if (rows[r].op == CQ_FCOP_CLASS_NAN)
                CHECK(s[0] == CQ_FC_A || s[0] == CQ_FC_B);
        }
    }

    CHECK(n_wide > 0);
    CHECK(n_narrow > 0);
}

/* ---- (6) The four swaps against their unswapped twin. ------------------ */

/* THE SWAP COSTS NOTHING AND CHANGES THE ANSWER, AND BOTH HALVES NEED SAYING.
 * `ogt(a,b)` and `olt(a,b)` run the SAME program on the same mask, so their
 * gate tuples are identical — which is exactly why no count can see a swap
 * that was dropped or added. What separates them is the VALUE, and only on
 * pairs where the relation is asymmetric. */
CQ_TEST(the_four_swapped_predicates_are_their_twin_on_exchanged_operands)
{
    static const int SWAPPED[] = { CQ_FCMP_OGT, CQ_FCMP_OGE,
                                   CQ_FCMP_UGT, CQ_FCMP_UGE };
    cq_mock m;
    int n_diff = 0;

    cq_mock_init(&m);

    for (int k = 0; k < 4; k++) {
        const fcmp_krow *sw = &ROWS[SWAPPED[k]];
        const fcmp_krow *tw = &ROWS[cq_fcmp_callee(sw->pred)];
        cq_counter a, b, ua, ub;

        /* (i) The TUPLE is identical, at the all-quantum mask L4 pins. */
        cq_kd_measure(&sw->spec, CQ_FP64_W, &a, &ua);
        cq_kd_measure(&tw->spec, CQ_FP64_W, &b, &ub);
        CHECK_EQ(a.x, b.x); CHECK_EQ(a.cx, b.cx); CHECK_EQ(a.ccx, b.ccx);
        CHECK_EQ(ua.x, ub.x); CHECK_EQ(ua.cx, ub.cx); CHECK_EQ(ua.ccx, ub.ccx);

        /* (ii) The VALUE is the twin's on exchanged operands, and it differs
         * from the twin's on the SAME operands somewhere — otherwise the
         * identity above would be satisfied by dropping the swap entirely. */
        for (int j = 0; j < N_FCMP_PAIRS; j++) {
            uint64_t x = FCMP_PAIRS[j].a, y = FCMP_PAIRS[j].b;
            cq_ref_w q = cq_ref_w_ones(CQ_FP64_W);
            uint64_t got   = fcmp_run(sw, x, y, q, q, &m);
            uint64_t twin  = fcmp_run(tw, y, x, q, q, &m);
            uint64_t plain = fcmp_run(tw, x, y, q, q, &m);

            CHECK_EQ(got, twin);
            CHECK_EQ(got, (uint64_t)host_fcmp(x, y, sw->pred));
            CHECK_EQ(cq_fcmp_eval(x, y, sw->pred), got);
            if (got != plain) n_diff++;
        }
    }

    CHECK(n_diff > 0);
    cq_mock_dispose(&m);
}
