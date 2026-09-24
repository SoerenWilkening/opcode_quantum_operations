/* tests/test_kernel_fmisc_scan.inc — the fmin/fmax LAYOUT's four instruments:
 * the four-valued slot scan, the span-disjointness scan, the palindrome at an
 * asymmetric mask, and two programs in one region. Included by
 * test_kernel_fmisc.c after _rows.inc (whose fm_olt_rows it reuses).
 * NOTHING HERE READS A GOLDEN.
 *
 * SLOTS AND GATES COME APART (D-K18-6): a view's constant lanes and a
 * constant span sit inside block operands at EVERY mask, so the scan
 * predicts NONE / X / CX / CCX per slot and walks the stream with its OWN
 * cursor. The one row it does not predict lane by lane is `soft_fcmp_olt`:
 * that is M36's 24-row program, whose schedule M36's own scan owns, so here
 * its segment is ASKED of M36 — a standalone replay of `cq_fcmp_step` over
 * the same operand kinds — and the claim is that K24's stream contains
 * exactly that segment at exactly that position.
 */

enum { FM_NONE = -1, FM_MAX_Q = 8192 };

/* At the all-quantum mask: a lane of a 64-lane operand code or row. */
static int fm_code_lane(cq_fmisc_prog p, int s, int i)
{
    switch (s) {
    case CQ_FX_A: case CQ_FX_B:         return FM_Q;
    case CQ_FX_ABS_A: case CQ_FX_ABS_B: return i < W64 - 1 ? FM_Q : CQ_BIT_ZERO;
    case CQ_FX_SGN_A:                   return i == W64 - 1 ? FM_Q : CQ_BIT_ZERO;
    case CQ_FX_K_ZERO:                  return CQ_BIT_ZERO;
    case CQ_FX_K_QNAN:                  return (i >= 51 && i <= 62)
                                               ? CQ_BIT_ONE : CQ_BIT_ZERO;
    default: break;
    }
    /* A ROW: a mux's `r` is scratch in every lane; a 1-bit row is its wire
     * zero-extended to the Julia type's width. */
    if (cq_fmisc_row_width(p, s) == W64) return FM_Q;
    return i == 0 ? FM_Q : CQ_BIT_ZERO;
}

/* PRD §3 over controls only — every TARGET is pre-materialised scratch. */
static int fm_ctrl_fold(int kind, const int *c, int nc)
{
    for (int i = 0; i < nc; i++) {
        if (c[i] == CQ_BIT_ZERO) return FM_NONE;
        if (c[i] == CQ_BIT_ONE) kind--;
    }
    return kind;
}

/* `lower_eq!` (arith.jl:424-447): 2W CX building diff — the only operand
 * reads — then a CX, CX, CCX cycle over the OR-prefix, all scratch. */
static int fm_sched_eq(cq_fmisc_prog p, int s0, int s1, int v)
{
    int c[1];

    if (v < 2 * W64) {
        c[0] = fm_code_lane(p, (v % 2 == 0) ? s0 : s1, v / 2);
        return fm_ctrl_fold(CQ_OP_CX, c, 1);
    }
    return ((v - 2 * W64) % 3 == 2) ? CQ_OP_CCX : CQ_OP_CX;
}

/* M31's class block over a whole rail: eq(exp view, 0x7FF), not1, eq(frac
 * view, 0), and. Lane i < 11 of the exponent view is Q, the constant 0x7FF
 * is ONE there; the fraction view is Q below 52 and the constant is 0. */
static int fm_sched_class(int v)
{
    int C = cq_eq_steps(W64), c[1], i;

    for (int half = 0; half < 2; half++) {
        int w = half == 0 ? CQ_FP64_EXP_W : CQ_FP64_FRAC_W;

        if (v < C) {
            if (v >= 2 * W64) return ((v - 2 * W64) % 3 == 2) ? CQ_OP_CCX : CQ_OP_CX;
            i = v / 2;
            c[0] = (i >= w) ? CQ_BIT_ZERO
                 : (v % 2 == 0) ? FM_Q : (half == 0 ? CQ_BIT_ONE : CQ_BIT_ZERO);
            return fm_ctrl_fold(CQ_OP_CX, c, 1);
        }
        v -= C;
        if (half == 0) {
            if (v < 2) return v == 0 ? CQ_OP_CX : CQ_OP_X;       /* the not1 */
            v -= 2;
        }
    }
    return CQ_OP_CCX;                                            /* the and  */
}

/* `lower_mux!` (arith.jl:522-532) per bit: CX(f->r), CX(t->d), CX(f->d),
 * CCX(cond, d->r). `cond` and `d` are scratch. */
static int fm_sched_mux(cq_fmisc_prog p, const cq_fx_row *r, int v)
{
    int i = v / 4, c[1];

    switch (v % 4) {
    case 0: case 2: c[0] = fm_code_lane(p, r->s2, i); return fm_ctrl_fold(CQ_OP_CX, c, 1);
    case 1:         c[0] = fm_code_lane(p, r->s1, i); return fm_ctrl_fold(CQ_OP_CX, c, 1);
    default:        return CQ_OP_CCX;
    }
}

static int fm_row_slots(const cq_fx_row *r)
{
    cq_fcmp_row olt[CQ_FCMP_MAX_ROWS];

    switch (r->op) {
    case CQ_FXOP_CLASS_NAN: return cq_fp_class_steps(CQ_FP_IS_NAN);
    case CQ_FXOP_EQ:        return cq_eq_steps(W64);
    case CQ_FXOP_NOT1:      return 2;
    case CQ_FXOP_AND1:      return 1;
    case CQ_FXOP_MUX:       return cq_mux_steps(W64);
    default:                return cq_fcmp_steps(olt, fm_olt_rows(olt));
    }
}

/* M36's `olt` segment at all-quantum operands: a standalone replay over its
 * own pre-materialised region, reversed and reclaimed. Returns the ops. */
static size_t fm_olt_replay(cq_op *out, size_t cap)
{
    cq_fcmp_row olt[CQ_FCMP_MAX_ROWS];
    int n = fm_olt_rows(olt);
    cq_mock m;
    cq_sink s;
    cq_ctx ctx;
    cq_scratch scr;
    cq_fcmp_block k;
    int32_t ha, hb;
    size_t got;

    cq_mock_init(&m);
    s = cq_mock_sink(&m);
    cq_ctx_init(&ctx, &s);
    ha = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_zero(), cq_ref_w_ones(W64));
    hb = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_zero(), cq_ref_w_ones(W64));
    cq_scratch_alloc(&scr, cq_fcmp_region(olt, n));
    for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&ctx, &scr.bits[i]);
    k.a = cq_reg_cbits(&ctx.regs, ha); k.b = cq_reg_cbits(&ctx.regs, hb);
    k.scr = &scr; k.off = 0u; k.rows = olt; k.n_rows = n;

    cq_mock_reset(&m);
    for (int u = 0; u < cq_fcmp_steps(olt, n); u++) cq_fcmp_step(&ctx, &k, u);
    got = cq_mock_count(&m);
    for (size_t g = 0; g < got && g < cap; g++) out[g] = cq_mock_at(&m, g)->op;
    for (int u = cq_fcmp_steps(olt, n) - 1; u >= 0; u--) cq_fcmp_step(&ctx, &k, u);
    for (uint32_t i = 0; i < scr.n; i++) scr.bits[i] = cq_bit_zero();
    cq_scratch_dispose(&scr);
    cq_ctx_dispose(&ctx);
    cq_mock_dispose(&m);
    return got;
}

/* The slot index, inside its row, of each predicted op (for the olt row: its
 * ordinal inside M36's segment). Written by fm_predict. */
static int fm_slot[65536];

/* The predicted op stream of row `r` into `out`, with the row index of each
 * op into `who`. Returns the count; `*folded` counts NONE slots. */
static size_t fm_predict(cq_fmisc_prog p, cq_op *out, int *who, size_t cap,
                         int *folded)
{
    static cq_op olt_ops[16384];
    size_t n_olt = fm_olt_replay(olt_ops, 16384u), n = 0u;
    int nrows = 0;
    const cq_fx_row *rows = cq_fmisc_rows(p, &nrows);

    CHECK(n_olt > 0u && n_olt <= 16384u);
    for (int r = 0; r < nrows; r++) {
        if (rows[r].op == CQ_FXOP_FCMP_OLT) {
            for (size_t g = 0; g < n_olt && n < cap; g++) {
                out[n] = olt_ops[g]; fm_slot[n] = (int)g; who[n++] = r;
            }
            *folded += fm_row_slots(&rows[r]) - (int)n_olt;
            continue;
        }
        for (int v = 0; v < fm_row_slots(&rows[r]); v++) {
            int k;

            switch (rows[r].op) {
            case CQ_FXOP_CLASS_NAN: k = fm_sched_class(v); break;
            case CQ_FXOP_EQ:   k = fm_sched_eq(p, rows[r].s0, rows[r].s1, v); break;
            case CQ_FXOP_NOT1: k = v == 0 ? CQ_OP_CX : CQ_OP_X; break;
            case CQ_FXOP_AND1: k = CQ_OP_CCX; break;
            default:           k = fm_sched_mux(p, &rows[r], v); break;
            }
            if (k == FM_NONE) { (*folded)++; continue; }
            if (n < cap) { out[n] = (cq_op)k; fm_slot[n] = v; who[n++] = r; }
        }
    }
    return n;
}

#define FM_SCAN_A  CQ_F64_NEG(CQ_F64_ONE)
#define FM_SCAN_B  CQ_F64_QNAN_B

static const cq_kd_spec *fm_prog_spec(int p)
{
    return p == CQ_FMISC_MIN ? &SPEC_FMIN : &SPEC_FMAX;
}

static uint64_t fm_prog_ref(int p, uint64_t a, uint64_t b)
{
    return p == CQ_FMISC_MIN ? cq_ref_fmin(a, b) : cq_ref_fmax(a, b);
}

CQ_TEST(the_slot_boundaries_match_an_independent_four_valued_scan)
{
    static cq_op pred[65536];
    static int who[65536];
    cq_ref_w q = cq_ref_w_ones(W64);
    cq_mock m;

    cq_mock_init(&m);
    for (int p = 0; p < CQ_FMISC_N_PROG; p++) {
        int folded = 0, demoted = 0;
        size_t n = fm_predict((cq_fmisc_prog)p, pred, who, 65536u, &folded);

        CHECK_EQ(fm_run(fm_prog_spec(p), FM_SCAN_A, FM_SCAN_B, q, q, &m, NULL),
                 fm_prog_ref(p, FM_SCAN_A, FM_SCAN_B));
        CHECK_EQ((long long)cq_mock_count(&m), (long long)(2u * n + (size_t)W64));
        for (size_t g = 0; g < n && g < cq_mock_count(&m); g++)
            if ((int)cq_mock_at(&m, g)->op != (int)pred[g]) {
                cq_h_fail(__FILE__, __LINE__, "%s stream %zu (row %d): emitted "
                          "op %d, the slot decode says %d", fm_prog_spec(p)->name,
                          g, who[g], (int)cq_mock_at(&m, g)->op, (int)pred[g]);
                break;
            }
        for (size_t g = 0; g < n; g++) demoted += (pred[g] == CQ_OP_X);
        /* NOT VACUOUS in either half: views fold slots away, and the 0x7FF
         * constant and K_QNAN demote CX to X. */
        CHECK(folded > 0);
        CHECK(demoted > 0);
        CHECK(cq_mock_is_palindrome(&m, n, (size_t)W64));
        for (size_t g = n; g < n + (size_t)W64 && g < cq_mock_count(&m); g++)
            CHECK_EQ((int)cq_mock_at(&m, g)->op, (int)CQ_OP_CX);
    }
    cq_mock_dispose(&m);
}

/* ---- No qubit is written by two ROWS of one program. ------------------- */

CQ_TEST(the_rows_spans_are_pairwise_disjoint)
{
    static cq_op pred[65536];
    static int who[65536];
    static int owner[FM_MAX_Q];
    cq_ref_w q = cq_ref_w_ones(W64);
    cq_mock m;

    cq_mock_init(&m);
    for (int p = 0; p < CQ_FMISC_N_PROG; p++) {
        int folded = 0, seen[FM_NROWS] = { 0 }, nseen = 0;
        size_t n = fm_predict((cq_fmisc_prog)p, pred, who, 65536u, &folded);

        for (int t = 0; t < FM_MAX_Q; t++) owner[t] = -1;
        (void)fm_run(fm_prog_spec(p), FM_SCAN_A, FM_SCAN_B, q, q, &m, NULL);
        CHECK(cq_mock_count(&m) >= n);
        for (size_t g = 0; g < n && g < cq_mock_count(&m); g++) {
            uint32_t t = cq_mock_at(&m, g)->t;

            CHECK(t < (uint32_t)FM_MAX_Q);
            if (t >= (uint32_t)FM_MAX_Q) break;
            if (owner[t] >= 0 && owner[t] != who[g]) {
                cq_h_fail(__FILE__, __LINE__, "%s: q%u is written by row %d AND "
                          "row %d", fm_prog_spec(p)->name, t, owner[t], who[g]);
                break;
            }
            owner[t] = who[g];
            if (!seen[who[g]]) { seen[who[g]] = 1; nseen++; }
        }
        CHECK_EQ(nseen, FM_NROWS);        /* every row wrote something */
    }
    cq_mock_dispose(&m);
}

/* ---- The palindrome at R8's asymmetric masks, both orders. ------------- */

CQ_TEST(the_compute_half_is_a_palindrome_at_an_asymmetric_mask)
{
    cq_ref_w all = cq_ref_w_ones(W64), none = cq_ref_w_zero();
    cq_mock m;

    cq_mock_init(&m);
    for (int p = 0; p < CQ_FMISC_N_PROG; p++)
        for (int side = 0; side < 2; side++) {
            size_t total, head;
            uint64_t got = fm_run(fm_prog_spec(p), FM_SCAN_A, CQ_F64_ONE,
                                  side ? none : all, side ? all : none, &m, NULL);

            CHECK_EQ(got, fm_prog_ref(p, FM_SCAN_A, CQ_F64_ONE));
            total = cq_mock_count(&m);
            CHECK(total > (size_t)W64);
            CHECK_EQ((long long)((total - (size_t)W64) % 2u), 0);
            head = (total - (size_t)W64) / 2u;
            if (!cq_mock_is_palindrome(&m, head, (size_t)W64))
                cq_h_fail(__FILE__, __LINE__, "%s side %d: the %zu-gate compute "
                          "half does not read back in reverse",
                          fm_prog_spec(p)->name, side, head);
        }
    cq_mock_dispose(&m);
}

/* ---- Two programs in ONE region, at two offsets. ----------------------- */

/* THE SOLE DETECTOR FOR A PROGRAM THAT IGNORES ITS OWN `off` (bd a-block-
 * that-ignores-its-own-off-needs-a-two-block-case): the kernel always passes
 * off = 0, so a dropped base is inert from every other case in this file. */
CQ_TEST(two_programs_in_one_region_do_not_collide)
{
    static const uint64_t VA[] = { CQ_F64_NEG_ZERO, CQ_F64_QNAN_A, FM_TWO };
    static const uint64_t VB[] = { CQ_F64_POS_ZERO, CQ_F64_ONE,
                                   CQ_F64_NEG(CQ_F64_ONE) };
    static int owner[FM_MAX_Q];
    cq_mock m;

    cq_mock_init(&m);
    for (int v = 0; v < 3; v++)
        for (int order = 0; order < 2; order++) {
            cq_fmisc_block k[2];
            cq_scratch scr;
            cq_ctx ctx;
            cq_sink s = cq_mock_sink(&m);
            cq_fmisc_prog pr[2];
            uint32_t r0;
            int32_t ha, hb;
            int bad = 0;

            pr[0] = order ? CQ_FMISC_MAX : CQ_FMISC_MIN;
            pr[1] = order ? CQ_FMISC_MIN : CQ_FMISC_MAX;
            r0 = cq_fmisc_region(pr[0]);
            cq_mock_reset(&m);
            cq_ctx_init(&ctx, &s);
            ha = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(VA[v], 0u, W64),
                             cq_ref_w_ones(W64));
            hb = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(VB[v], 0u, W64),
                             cq_ref_w_ones(W64));
            cq_scratch_alloc(&scr, r0 + cq_fmisc_region(pr[1]));
            for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&ctx, &scr.bits[i]);
            for (int b = 0; b < 2; b++) {
                k[b].a = cq_reg_cbits(&ctx.regs, ha);
                k[b].b = cq_reg_cbits(&ctx.regs, hb);
                k[b].scr = &scr; k[b].off = b ? r0 : 0u; k[b].prog = pr[b];
            }
            CHECK(k[1].off > 0u);

            for (int t = 0; t < FM_MAX_Q; t++) owner[t] = -1;
            cq_mock_reset(&m);
            for (int b = 0; b < 2 && !bad; b++) {
                size_t before = cq_mock_count(&m);

                for (int u = 0; u < cq_fmisc_steps(pr[b]); u++)
                    cq_fmisc_step(&ctx, &k[b], u);
                for (size_t g = before; g < cq_mock_count(&m); g++) {
                    uint32_t t = cq_mock_at(&m, g)->t;

                    if (t >= (uint32_t)FM_MAX_Q || (owner[t] >= 0 && owner[t] != b)) {
                        cq_h_fail(__FILE__, __LINE__, "two programs at 0 and %u: "
                                  "q%u is written by BOTH", k[1].off, t);
                        bad = 1;
                        break;
                    }
                    owner[t] = b;
                }
            }
            for (int b = 0; b < 2 && !bad; b++)
                CHECK_EQ(fm_read(&ctx, cq_fmisc_result(&k[b])),
                         fm_prog_ref((int)pr[b], VA[v], VB[v]));

            for (int b = 1; b >= 0; b--)
                for (int u = cq_fmisc_steps(pr[b]) - 1; u >= 0; u--)
                    cq_fmisc_step(&ctx, &k[b], u);
            for (uint32_t i = 0; i < scr.n; i++) {
                if (cq_bit_is_qubit(scr.bits[i])
                    && !cq_shadow_known_zero(&ctx.shadow, cq_bit_qindex(scr.bits[i])))
                    cq_h_fail(__FILE__, __LINE__, "scratch bit %u is not back at "
                              "|0> after both reverse halves", i);
                scr.bits[i] = cq_bit_zero();
            }
            cq_scratch_dispose(&scr);
            cq_ctx_dispose(&ctx);
        }
    cq_mock_dispose(&m);
}

/* ---- The operands are wired lane for lane. ----------------------------- */

/* THE SCAN ABOVE COMPARES KINDS, AND A KIND IS NOT A WIRE. Measured on this
 * module's own battery: replacing the in-place view `a & 0x8000…` (:98) with
 * the shifted `a >> 63` moves row 8's one live CX from slot 126 to slot 0 —
 * the same kind, the same count, the same value (`a_neg` is `!= 0` either
 * way) — and every case above stayed green. So this case drives the program
 * over a region it materialised itself, where each scratch bit's qubit is
 * known, and checks the CONTROL and TARGET of three kinds of gate against the
 * table: every live phase-A CX of an `eq` whose operand is a view reads the
 * RAIL lane the view keeps IN PLACE and writes `diff[lane]`; every mux arm
 * that is a rail reads that rail's lane (an arm swap, or fmax's row 12 read
 * the wrong way round, lands on the other rail); and the first gate of the
 * `olt` segment reads lane 52 of the rail its `s0` names (fmax's row 9 swap).
 * The row offsets are a TEST-SIDE prefix sum over each module's own region. */
static uint32_t fm_row_bits(const cq_fx_row *r)
{
    cq_fcmp_row olt[CQ_FCMP_MAX_ROWS];

    switch (r->op) {
    case CQ_FXOP_CLASS_NAN: return cq_fp_class_region(CQ_FP_IS_NAN);
    case CQ_FXOP_EQ:        return (uint32_t)cq_eq_region(W64);
    case CQ_FXOP_MUX:       return (uint32_t)cq_mux_region(W64);
    case CQ_FXOP_FCMP_OLT:  return cq_fcmp_region(olt, fm_olt_rows(olt));
    default:                return 1u;                 /* not1, and1 */
    }
}

static const cq_bit *fm_rail_of(const cq_fmisc_block *k, int s)
{
    if (s == CQ_FX_A || s == CQ_FX_ABS_A || s == CQ_FX_SGN_A) return k->a;
    if (s == CQ_FX_B || s == CQ_FX_ABS_B) return k->b;
    return NULL;
}

CQ_TEST(the_operands_are_wired_lane_for_lane)
{
    static cq_op pred[65536];
    static int who[65536];
    cq_mock m;

    cq_mock_init(&m);
    for (int p = 0; p < CQ_FMISC_N_PROG; p++) {
        int folded = 0, nrows = 0, n_view = 0, n_arm = 0, n_olt = 0;
        size_t n = fm_predict((cq_fmisc_prog)p, pred, who, 65536u, &folded);
        const cq_fx_row *rows = cq_fmisc_rows((cq_fmisc_prog)p, &nrows);
        uint32_t off[FM_NROWS], o = 0u;
        cq_sink s = cq_mock_sink(&m);
        cq_fmisc_block k;
        cq_scratch scr;
        cq_ctx ctx;
        int32_t ha, hb;

        for (int r = 0; r < nrows; r++) { off[r] = o; o += fm_row_bits(&rows[r]); }
        cq_ctx_init(&ctx, &s);
        ha = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_zero(), cq_ref_w_ones(W64));
        hb = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_zero(), cq_ref_w_ones(W64));
        cq_scratch_alloc(&scr, o);
        for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&ctx, &scr.bits[i]);
        k.a = cq_reg_cbits(&ctx.regs, ha); k.b = cq_reg_cbits(&ctx.regs, hb);
        k.scr = &scr; k.off = 0u; k.prog = (cq_fmisc_prog)p;

        cq_mock_reset(&m);
        for (int u = 0; u < cq_fmisc_steps((cq_fmisc_prog)p); u++)
            cq_fmisc_step(&ctx, &k, u);
        CHECK_EQ((long long)cq_mock_count(&m), (long long)n);

        for (size_t g = 0; g < n && g < cq_mock_count(&m); g++) {
            const cq_rec *rec = cq_mock_at(&m, g);
            const cq_fx_row *row = &rows[who[g]];
            int v = fm_slot[g], i;
            const cq_bit *rail;
            uint32_t want_a = UINT32_MAX, want_t = UINT32_MAX;

            if (row->op == CQ_FXOP_EQ && v < 2 * W64 && v % 2 == 0
                && (rail = fm_rail_of(&k, row->s0)) != NULL) {
                i = v / 2;
                want_a = cq_bit_qindex(rail[i]);
                want_t = cq_bit_qindex(scr.bits[off[who[g]] + (uint32_t)i]);
                n_view++;
            } else if (row->op == CQ_FXOP_MUX && v % 4 != 3
                       && (rail = fm_rail_of(&k, v % 4 == 1 ? row->s1 : row->s2))
                          != NULL) {
                want_a = cq_bit_qindex(rail[v / 4]);
                n_arm++;
            } else if (row->op == CQ_FXOP_FCMP_OLT && v == 0) {
                want_a = cq_bit_qindex(fm_rail_of(&k, row->s0)[CQ_FP64_EXP_LO]);
                n_olt++;
            } else {
                continue;
            }
            if (rec->a != want_a || (want_t != UINT32_MAX && rec->t != want_t)) {
                cq_h_fail(__FILE__, __LINE__, "%s row %d slot %d: gate reads q%u "
                          "and writes q%u; the table says q%u -> q%u",
                          fm_prog_spec(p)->name, who[g], v, rec->a, rec->t,
                          want_a, want_t);
                break;
            }
        }
        /* NOT VACUOUS: 63 + 63 + 1 view lanes, 5 rail arms x 64 lanes, and one
         * `olt` segment, all counted off the table rather than assumed. */
        CHECK_EQ(n_view, 2 * (W64 - 1) + 1);
        CHECK(n_arm > 0);
        CHECK_EQ(n_olt, 1);

        for (int u = cq_fmisc_steps((cq_fmisc_prog)p) - 1; u >= 0; u--)
            cq_fmisc_step(&ctx, &k, u);
        for (uint32_t i = 0; i < scr.n; i++) scr.bits[i] = cq_bit_zero();
        cq_scratch_dispose(&scr);
        cq_ctx_dispose(&ctx);
    }
    cq_mock_dispose(&m);
}
