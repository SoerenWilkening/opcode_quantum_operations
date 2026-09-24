/* tests/test_kernel_fround_scan.inc — the four-valued SLOT SCAN over all five
 * programs, the per-op schedules it predicts from, and the standalone runs it
 * delegates the two REGION ops to. Included by test_kernel_fround.c AFTER
 * test_kernel_fround_slots.inc (whose `fr_ref_steps` it reuses) and BEFORE
 * test_kernel_fround_spans.inc (which reuses `fr_row_gates`).
 * NOTHING BELOW READS A GOLDEN.
 *
 * THE PREDICTION IS A KIND **OR NOTHING**, AND THE STREAM HAS ITS OWN CURSOR:
 * a control lane that is a constant ZERO makes a gate vanish and a constant ONE
 * demotes it (CCX->CX, CX->X), at EVERY mask, because the views and constant
 * spans put classical lanes inside block operands. A cursor slip reports one
 * row LATER than its cause (bd a-picked-output-can-itself-be-a-view): read the
 * first mismatch as "something before here folded differently".
 *
 * THE BARREL AND M33's `soft_fadd` ARE DELEGATED, NOT RE-DERIVED: each such
 * row is checked by RUNNING THAT BLOCK STANDALONE — through M12's and M33's
 * own step functions, never through fround_fadd.c. For fadd that is M33's
 * UNMAPPED `cq_fa_step` (fadd_int.h, which tests may include), deliberately
 * NOT the mapped `cq_fadd_step` the kernel reaches, so the comparison is
 * between two different decode paths of M33's one program — over operands with the
 * same lane profile, and comparing the two streams gate kind for gate kind.
 * Writing M33's 47,815-slot schedule out here would be a second transcription
 * of M33, which is what a slot scan must not be.
 */

enum { FR_PRED_NONE = -1, FR_STD_CAP = 60000 };

#define FR_DELEGATED(op) \
    ((op) == CQ_FIOP_BSHL || (op) == CQ_FIOP_BLSHR || (op) == CQ_FIOP_FADD)

_Static_assert(CQ_OP_X == 0 && CQ_OP_CX == 1 && CQ_OP_CCX == 2,
               "the scan demotes a gate by decrementing its op");

/* The constant vocabulary, transcribed from softfloat_common.jl:8-14 and
 * fround.jl's literals rather than read out of fround_operand.c — an oracle
 * that shares a constant with the code is blind to that constant. */
static uint64_t fr_const(int s)
{
    switch (s) {
    case CQ_FI_K_ZERO:      return 0u;
    case CQ_FI_K_ONE:       return 1u;
    case CQ_FI_K_7FF:       return 0x7ffu;
    case CQ_FI_K_QUIET:     return 0x0008000000000000ull;
    case CQ_FI_K_1022:      return 1022u;
    case CQ_FI_K_1023:      return 1023u;
    case CQ_FI_K_1075:      return 1075u;
    case CQ_FI_K_52:        return 52u;
    case CQ_FI_K_ONES:      return ~0ull;
    case CQ_FI_K_IMPLICIT:  return 0x0010000000000000ull;
    case CQ_FI_K_ONE_F:     return 0x3ff0000000000000ull;   /* 1.0         */
    default:                return 0xbff0000000000000ull;   /* -1.0        */
    }
}

/* The KIND of lane `i` of operand `s` at the ALL-QUANTUM operand mask. Views
 * are collapsed by this file's OWN walk. */
static int fr_lane(const cq_fround_row *rows, int n, int s, int i)
{
    int guard = 0, shift = 0;
    uint64_t mask = ~UINT64_C(0);

    while (s >= 0 && s < n && rows[s].op == CQ_FIOP_VIEW) {
        uint64_t m = rows[s].mask;

        if (shift >= 64 || shift <= -64) m = 0u;
        else if (shift >= 0)             m >>= (unsigned)shift;
        else                             m <<= (unsigned)(-shift);
        mask  &= m;
        shift += rows[s].shift;
        s      = rows[s].s0;
        if (++guard > n) { CHECK(0); return CQ_BIT_ZERO; }
    }
    if (((mask >> (unsigned)i) & 1u) == 0u) return CQ_BIT_ZERO;
    i += shift;
    if (i < 0 || i >= W64) return CQ_BIT_ZERO;
    if (s == CQ_FI_A) return CQ_BIT_Q;
    if (s < 0) return ((fr_const(s) >> (unsigned)i) & 1u) ? CQ_BIT_ONE
                                                           : CQ_BIT_ZERO;
    return CQ_BIT_Q;             /* a span of the region: I6(b) materialises */
}

typedef struct { const cq_fround_row *rows; int n, fold, dem; } fr_scan;

static int fr_fold(fr_scan *sc, int kind, int c0, int c1, int nc)
{
    int c[2] = { c0, c1 };

    for (int i = 0; i < nc; i++) {
        if (c[i] == CQ_BIT_ZERO) { sc->fold++; return FR_PRED_NONE; }
        if (c[i] == CQ_BIT_ONE)  { kind--; sc->dem++; }
    }
    return kind;
}

#define LANE(s, i)        fr_lane(sc->rows, sc->n, (s), (i))
#define FOLD1(k, c)       fr_fold(sc, (k), (c), 0, 1)
#define FOLD2(k, c0, c1)  fr_fold(sc, (k), (c0), (c1), 2)

/* `lower_eq!` (arith.jl:424-447): 2W CX building `diff`, then CX, CX, CCX over
 * the OR-prefix, whose controls are scratch. */
static int fr_sched_eq(fr_scan *sc, int a, int b, int v)
{
    if (v < 2 * W64) return FOLD1(CQ_OP_CX, LANE((v % 2 == 0) ? a : b, v / 2));
    return ((v - 2 * W64) % 3 == 2) ? CQ_OP_CCX : CQ_OP_CX;
}

/* `lower_slt!` (:465-472): the two biased copies, the two MSB flips, then
 * `lower_ult!` over `af`/`bf` — SCRATCH, so the inner comparator never folds. */
static int fr_sched_slt(fr_scan *sc, int a, int b, int v)
{
    int u;

    if (v < 2 * W64) return FOLD1(CQ_OP_CX, LANE((v % 2 == 0) ? a : b, v / 2));
    if (v < 2 * W64 + 2) return CQ_OP_X;
    u = v - (2 * W64 + 2);
    if (u < 2 * W64) return (u % 2 == 0) ? CQ_OP_CX : CQ_OP_X;
    if (u == 2 * W64) return CQ_OP_X;
    u = (u - (2 * W64 + 1)) % 4;
    return (u == 2 || u == 3) ? CQ_OP_CCX : CQ_OP_CX;
}

/* Bennett's carry recurrence (adder.jl:1-18): five per lane, three on the top
 * one. `y` is `b` for `add` and the scratch `nb` for `sub`. */
static int fr_ripple(fr_scan *sc, int la, int ly, int u)
{
    if (u >= 5 * (W64 - 1))
        switch (u - 5 * (W64 - 1)) {
        case 0:  return FOLD1(CQ_OP_CX, la);
        case 1:  return FOLD1(CQ_OP_CX, ly);
        default: return CQ_OP_CX;
        }
    switch (u % 5) {
    case 0:  return FOLD1(CQ_OP_CX, la);
    case 1:  return FOLD1(CQ_OP_CX, ly);
    case 2:  return FOLD2(CQ_OP_CCX, la, ly);
    case 3:  return CQ_OP_CCX;
    default: return CQ_OP_CX;
    }
}

static int fr_sched_add(fr_scan *sc, int a, int b, int v)
{
    int i = (v < 5 * (W64 - 1)) ? v / 5 : W64 - 1;

    return fr_ripple(sc, LANE(a, i), LANE(b, i), v);
}

/* `lower_sub!` (adder.jl:148-172): the 2W complement, the carry seed, then the
 * ripple with `nb`. */
static int fr_sched_sub(fr_scan *sc, int a, int b, int v)
{
    int u, i;

    if (v < 2 * W64)
        return (v % 2 == 0) ? FOLD1(CQ_OP_CX, LANE(b, v / 2)) : CQ_OP_X;
    if (v == 2 * W64) return CQ_OP_X;
    u = v - (2 * W64 + 1);
    i = (u < 5 * (W64 - 1)) ? u / 5 : W64 - 1;
    return fr_ripple(sc, LANE(a, i), CQ_BIT_Q, u);
}

/* `lower_mux!` (arith.jl:522-532): CX(f), CX(t), CX(f), CCX(cond, d). */
static int fr_sched_mux(fr_scan *sc, int t, int f, int v)
{
    int i = v / 4;

    switch (v % 4) {
    case 0:  return FOLD1(CQ_OP_CX, LANE(f, i));
    case 1:  return FOLD1(CQ_OP_CX, LANE(t, i));
    case 2:  return FOLD1(CQ_OP_CX, LANE(f, i));
    default: return FOLD2(CQ_OP_CCX, CQ_BIT_Q, CQ_BIT_Q);
    }
}

static int fr_scan_slot(fr_scan *sc, const cq_fround_row *r, int v)
{
    int lane;

    switch (r->op) {
    case CQ_FIOP_EQ:  return fr_sched_eq (sc, r->s0, r->s1, v);
    case CQ_FIOP_SLT: return fr_sched_slt(sc, r->s0, r->s1, v);
    case CQ_FIOP_ADD: return fr_sched_add(sc, r->s0, r->s1, v);
    case CQ_FIOP_SUB: return fr_sched_sub(sc, r->s0, r->s1, v);
    case CQ_FIOP_MUX: return fr_sched_mux(sc, r->s1, r->s2, v);
    case CQ_FIOP_AND:                               /* arith.jl:270       */
        return FOLD2(CQ_OP_CCX, LANE(r->s0, v), LANE(r->s1, v));
    case CQ_FIOP_OR:                                /* arith.jl:276-278   */
        lane = v / 3;
        if (v % 3 == 2) return FOLD2(CQ_OP_CCX, LANE(r->s0, lane),
                                     LANE(r->s1, lane));
        return FOLD1(CQ_OP_CX, LANE((v % 3 == 0) ? r->s0 : r->s1, lane));
    case CQ_FIOP_XOR:                               /* arith.jl:287-288   */
        return FOLD1(CQ_OP_CX, LANE((v % 2 == 0) ? r->s0 : r->s1, v / 2));
    case CQ_FIOP_NOT1: return (v == 0) ? CQ_OP_CX : CQ_OP_X;  /* :476    */
    case CQ_FIOP_AND1: return CQ_OP_CCX;
    case CQ_FIOP_OR1:  return (v == 2) ? CQ_OP_CCX : CQ_OP_CX;
    default:
        cq_h_fail(__FILE__, __LINE__, "the scan has no schedule for op %d",
                  r->op);
        return FR_PRED_NONE;
    }
}

/* ---- The delegated rows: the block run standalone. --------------------- */

static const cq_bit *fr_std_op(cq_ctx *ctx, const cq_fround_row *rows, int n,
                               int s, cq_bit *out)
{
    for (int i = 0; i < W64; i++) {
        int k = fr_lane(rows, n, s, i);

        out[i] = (k == CQ_BIT_ONE) ? cq_bit_one() : cq_bit_zero();
        if (k == CQ_BIT_Q) cq_materialise(ctx, &out[i]);
    }
    return out;
}

/* Row `i`'s block, driven standalone on operands with row `i`'s lane profile
 * through M12's or M33's OWN step function. Returns the gate count. */
static size_t fr_std_kinds(cq_mock *m, const cq_fround_row *rows, int n, int i,
                           unsigned char *out)
{
    const cq_fround_row *r = &rows[i];
    cq_sink s = cq_mock_sink(m);
    cq_ctx ctx;
    cq_scratch scr;
    cq_bit o0[CQ_FP64_W], o1[CQ_FP64_W];
    int steps = fr_ref_steps(r);
    size_t got;

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);
    cq_scratch_alloc(&scr, fr_ref_region(r));
    for (uint32_t b = 0; b < scr.n; b++) cq_materialise(&ctx, &scr.bits[b]);
    (void)fr_std_op(&ctx, rows, n, r->s0, o0);
    (void)fr_std_op(&ctx, rows, n, r->s1, o1);
    cq_mock_reset(m);

    for (int u = 0; u < steps; u++) {
        if (r->op == CQ_FIOP_FADD) {
            cq_fa_ctx x;
            const cq_fa_map *mp = cq_fa_map_get(CQ_FADD_PROG_ADD);

            x.a = o0; x.b = o1; x.scr = &scr; x.off = 0u;
            x.rows = mp->rows; x.n = mp->n;
            cq_fa_step(&ctx, &x, u);
        } else {
            cq_barrel_block b;

            b.a = o0; b.b = o1; b.scr = &scr; b.off = 0u; b.W = W64;
            b.dir = (r->op == CQ_FIOP_BSHL) ? CQ_BARREL_SHL : CQ_BARREL_LSHR;
            cq_barrel_step(&ctx, &b, u);
        }
    }
    got = cq_mock_count(m);
    CHECK(got <= (size_t)FR_STD_CAP);
    for (size_t g = 0; g < got && g < (size_t)FR_STD_CAP; g++)
        out[g] = (unsigned char)cq_mock_at(m, g)->op;

    for (uint32_t b = 0; b < scr.n; b++) scr.bits[b] = cq_bit_zero();
    cq_scratch_dispose(&scr);
    cq_ctx_dispose(&ctx);
    return got;
}

/* How many GATES each row emits at the all-quantum mask, by the SAME
 * prediction and delegation the scan uses — shared with the span scan. */
static void fr_row_gates(cq_mock *m2, unsigned char *std,
                         const cq_fround_row *rows, int n, size_t *out)
{
    fr_scan sc = { rows, n, 0, 0 };

    for (int r = 0; r < n; r++) {
        out[r] = 0u;
        if (rows[r].op == CQ_FIOP_VIEW) continue;
        if (FR_DELEGATED(rows[r].op)) {
            out[r] = fr_std_kinds(m2, rows, n, r, std);
            continue;
        }
        for (int v = 0; v < fr_ref_steps(&rows[r]); v++)
            if (fr_scan_slot(&sc, &rows[r], v) != FR_PRED_NONE) out[r]++;
    }
}

/* One run of kernel `p` into a mock, at a value and a quantum mask on `a`.
 * Returns `dst`'s value. Shared by the scans and the palindrome. */
static uint64_t fr_run(cq_fround_prog p, uint64_t va, cq_ref_w qa, cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    int32_t ha, hd;
    uint64_t got;

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);
    ha = cq_bk_reg_w(&ctx, (uint32_t)W64, cq_ref_w_make(va, 0u, W64), qa);
    hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W64);
    cq_mock_reset(m);                    /* drop the rail's materialising Xs */
    cq_kernel_fround(&ctx, p, cq_reg_bits(&ctx.regs, hd),
                     cq_reg_cbits(&ctx.regs, ha), W64);
    got = cq_pc_value(&ctx, hd);
    cq_ctx_dispose(&ctx);
    return got;
}

/* The value each program's structural scans run at: a NORMAL operand with a
 * fraction, so every block of the normal path carries quantum lanes. */
static const uint64_t FR_SCAN_A[CQ_FROUND_N_PROG] = {
    FR_N2P5, FR_N2P5, FR_2P5, FR_2P5, FR_N2P5
};

static void fr_scan_one(cq_fround_prog p)
{
    static unsigned char std[FR_STD_CAP];
    const cq_fround_row *rows;
    cq_mock m, m2;
    fr_scan sc;
    size_t cursor = 0u, total;
    int n, bad = 0, delegated = 0, want_deleg = 0;

    cq_mock_init(&m);
    cq_mock_init(&m2);
    rows = cq_fround_rows(p, &n);
    sc.rows = rows; sc.n = n; sc.fold = 0; sc.dem = 0;

    CHECK_EQ(fr_run(p, FR_SCAN_A[p], cq_ref_w_ones(W64), &m),
             ref_fround(p, FR_SCAN_A[p]));

    for (int r = 0; r < n && !bad; r++) {
        int op = rows[r].op;

        if (op == CQ_FIOP_VIEW) continue;
        if (FR_DELEGATED(op)) {
            size_t got = fr_std_kinds(&m2, rows, n, r, std);

            want_deleg++;
            CHECK(got > 0u);
            if (cursor + got > cq_mock_count(&m)) {
                cq_h_fail(__FILE__, __LINE__, "%s row %d: the stream has %zu "
                          "gates left and the standalone block emits %zu",
                          FR_SPECS[p]->name, r, cq_mock_count(&m) - cursor, got);
                bad = 1;
                break;
            }
            for (size_t g = 0; g < got && !bad; g++)
                if ((unsigned char)cq_mock_at(&m, cursor + g)->op != std[g]) {
                    cq_h_fail(__FILE__, __LINE__, "%s row %d (op %d) gate %zu: "
                              "the kernel emits op %d, standalone emits %d",
                              FR_SPECS[p]->name, r, op, g,
                              (int)cq_mock_at(&m, cursor + g)->op, (int)std[g]);
                    bad = 1;
                }
            cursor += got;
            delegated++;
            continue;
        }
        for (int v = 0; v < fr_ref_steps(&rows[r]) && !bad; v++) {
            int pr = fr_scan_slot(&sc, &rows[r], v);

            if (pr == FR_PRED_NONE) continue;
            if (cursor >= cq_mock_count(&m)) {
                cq_h_fail(__FILE__, __LINE__, "%s row %d slot %d: the stream "
                          "ran out at %zu gates", FR_SPECS[p]->name, r, v,
                          cursor);
                bad = 1;
                break;
            }
            if ((int)cq_mock_at(&m, cursor)->op != pr) {
                cq_h_fail(__FILE__, __LINE__, "%s row %d (op %d) slot %d "
                          "(stream %zu): emitted op %d, the decode says %d",
                          FR_SPECS[p]->name, r, op, v, cursor,
                          (int)cq_mock_at(&m, cursor)->op, pr);
                bad = 1;
            }
            cursor++;
        }
    }

    if (!bad) {
        /* NOT VACUOUS: both halves of the fold table fire, and every region op
         * was compared against its standalone run. */
        CHECK(sc.fold > 0);
        CHECK(sc.dem > 0);
        CHECK_EQ(delegated, want_deleg);
        CHECK(delegated > 0);
        total = cq_mock_count(&m);
        CHECK_EQ((long long)total, (long long)(2u * cursor + (size_t)W64));
        for (int i = 0; i < W64; i++)
            CHECK_EQ((int)cq_mock_at(&m, cursor + (size_t)i)->op, (int)CQ_OP_CX);
        CHECK(cq_mock_is_palindrome(&m, cursor, (size_t)W64));
        printf("# %s slot scan: %zu gates forward, %d folded, %d demoted, "
               "%d blocks delegated\n", FR_SPECS[p]->name, cursor, sc.fold,
               sc.dem, delegated);
        fflush(stdout);
    }
    cq_mock_dispose(&m2);
    cq_mock_dispose(&m);
}

CQ_TEST(the_slot_boundaries_match_an_independent_four_valued_scan)
{
    for (int p = 0; p < CQ_FROUND_N_PROG; p++) fr_scan_one((cq_fround_prog)p);
}
