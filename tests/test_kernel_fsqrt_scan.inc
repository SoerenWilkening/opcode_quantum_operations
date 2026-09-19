/* tests/test_kernel_fsqrt_scan.inc — the four-valued SLOT SCAN, the per-op
 * schedules it predicts from, and the standalone comparison it delegates the
 * three composite blocks to. Included by test_kernel_fsqrt.c AFTER
 * test_kernel_fsqrt_slots.inc, whose `fq_ref_row_steps` and `FQ_SRC` it
 * reuses, and BEFORE test_kernel_fsqrt_spans.inc, which reuses `fq_row_gates`,
 * `fq_std_kinds` and `fq_lane`.
 * NOTHING BELOW READS A GOLDEN.
 *
 * THE PREDICTION IS A KIND **OR NOTHING**, AND THE STREAM HAS ITS OWN CURSOR.
 * At the all-quantum mask every v1 kernel emits exactly one gate per slot, so
 * `slot index == stream index` and K12's scan walked them together. That is
 * false here: a control lane that is a constant ZERO makes the gate vanish and
 * a constant ONE demotes it (CCX->CX, CX->X). Both happen at every mask,
 * because they come from the VIEWS and the CONSTANT SPANS rather than from the
 * operand's bit-kinds.
 *
 * AND K21 ADDS A CLAUSE NO SIBLING HAS: THE FOLD PATTERN MOVES WITH THE
 * ITERATION INDEX. `a_lo` is a CHAIN of `<< 2` views — `a_lo[t]` is
 * `a_lo[t-1] << 2` — so `a_lo[t]` is `a_lo0`'s span with `2t` literal
 * `CQ_BIT_ZERO` lanes at the bottom, and from t >= 32 it is ENTIRELY constant
 * zero and `(a_hi << 2) | (a_lo >> 62)` folds its second disjunct away
 * completely. THE RADICAND IS BEING CONSUMED; THAT IS WHAT THE LOOP IS FOR.
 * The scan gets this for free because `fq_lane` walks the chain itself rather
 * than predicting from the within-iteration index — a scan carried across from
 * K15 would go red at t = 32 and stay red, and the reflex fix, relaxing the
 * prediction to "or nothing" everywhere, is what turns the instrument off.
 *
 * AND THE THREE COMPOSITE BLOCKS ARE DELEGATED RATHER THAN RE-DERIVED, WHICH
 * IS M34's instruction and not laziness. Re-deriving `cq_norm52_step`'s
 * 8,755-slot six-stage ladder inside this scan would be a SECOND
 * TRANSCRIPTION OF M32, which is exactly what CLAUDE.md says a slot scan must
 * not be. Instead each `class`, `norm52` and `round` row is checked by RUNNING
 * THAT BLOCK STANDALONE over operands with the same lane profile and comparing
 * the two streams gate kind for gate kind. A reader who finds the interiors
 * unpredicted must not "fix" it by writing M32's schedule out here.
 */

enum { FQ_PRED_NONE = -1, FQ_STD_CAP = 12000 };

#define FQ_DELEGATED(op)                                                       \
    ((op) == CQ_FSOP_CLASS || (op) == CQ_FSOP_NORM52 || (op) == CQ_FSOP_ROUND)

_Static_assert(CQ_OP_X == 0 && CQ_OP_CX == 1 && CQ_OP_CCX == 2,
               "the scan demotes a gate by decrementing its op");

/* The constant vocabulary, transcribed from softfloat_common.jl:8-14 and
 * fsqrt.jl's literals rather than read out of fsqrt_operand.c's `const_of` —
 * an oracle that shares a constant with the code is blind to that constant. */
static uint64_t fq_const(int s)
{
    switch (s) {
    case CQ_FS_K_ZERO:     return 0u;
    case CQ_FS_K_ONE:      return 1u;                          /* :86 :89  */
    case CQ_FS_K_BIAS:     return 1023u;                       /* :32      */
    case CQ_FS_K_IMPLICIT: return 0x0010000000000000ull;       /* common:9 */
    case CQ_FS_K_INF:      return 0x7ff0000000000000ull;       /* common:11*/
    case CQ_FS_K_QUIET:    return 0x0008000000000000ull;       /* common:13*/
    default:               return 0xfff8000000000000ull;       /* common:14*/
    }
}

/* The KIND of lane `i` of operand `s`, at the ALL-QUANTUM operand mask. Views
 * are collapsed by this file's OWN walk, so the addressing arithmetic is
 * checked as well as the boundaries; an SVIEW is resolved by its own rule,
 * because its vacated lanes are COPIES OF THE SIGN LANE and not zeros. */
static int fq_lane(const cq_fsqrt_row *rows, int n, int s, int i)
{
    int guard = 0, shift = 0;
    uint64_t mask = ~UINT64_C(0);

    while (s >= 0 && s < n && rows[s].op == CQ_FSOP_VIEW) {
        uint64_t m = rows[s].mask;

        /* (((base >> s1) & m1) >> s2) & m2 == (base >> (s1+s2))
         *                                      & ((m1 >> s2) & m2) */
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

    if (s >= 0 && s < n && rows[s].op == CQ_FSOP_SVIEW) {
        int j = i + rows[s].shift;

        return fq_lane(rows, n, rows[s].s0, (j < W64) ? j : W64 - 1);
    }
    if (s == CQ_FS_A) return CQ_BIT_Q;                       /* all-quantum */
    if (s < 0)
        return ((fq_const(s) >> (unsigned)i) & 1u) ? CQ_BIT_ONE : CQ_BIT_ZERO;
    return CQ_BIT_Q;             /* a span of the region: I6(b) materialises */
}

/* How many LEADING lanes of operand `s` are quantum, or -1 if the profile is
 * not a prefix. ZERO IS A LEGAL ANSWER and is the whole of D-K23-7 for this
 * caller: `fsqrt.jl:103` passes the LITERAL `UInt64(0)` for `result_sign`, so
 * M32's round block runs here over an all-constant third operand and costs
 * fewer GATES than it does in `fadd`, `fmul`, `fdiv` or `fma`. */
static int fq_prefix(const cq_fsqrt_row *rows, int n, int s)
{
    int k = 0;

    while (k < W64 && fq_lane(rows, n, s, k) == CQ_BIT_Q) k++;
    for (int i = k; i < W64; i++)
        if (fq_lane(rows, n, s, i) != CQ_BIT_ZERO) return -1;
    return k;
}

/* PRD §3's fold table, over one gate's controls. */
static int fq_fold(int kind, const int *ctrl, int nc, int *folded, int *dem)
{
    for (int i = 0; i < nc; i++) {
        if (ctrl[i] == CQ_BIT_ZERO) { if (folded) (*folded)++; return FQ_PRED_NONE; }
        if (ctrl[i] == CQ_BIT_ONE) { kind--; if (dem) (*dem)++; }
    }
    return kind;
}

typedef struct { int folded, demoted; } fq_tally;

/* `lower_eq!` (arith.jl:424-447): 2W CX building `diff` — the only place an
 * operand lane is a control — then a 3-cycle CX, CX, CCX over the OR-prefix,
 * whose controls are all scratch and so cannot fold. */
static int fq_sched_eq(const cq_fsqrt_row *r, const cq_fsqrt_row *rows, int n,
                       int v, fq_tally *t)
{
    if (v < 2 * W64) {
        int i = v / 2, c[1];

        c[0] = fq_lane(rows, n, (v % 2 == 0) ? r->s0 : r->s1, i);
        return fq_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    }
    return ((v - 2 * W64) % 3 == 2) ? CQ_OP_CCX : CQ_OP_CX;
}

/* `lower_ult!` (arith.jl:449-463): 2W alternating CX/X building `~b`, the
 * carry seed, then a 4-cycle per lane. `nb`, `axnb` and `carry` are scratch. */
static int fq_sched_ult(const cq_fsqrt_row *r, const cq_fsqrt_row *rows, int n,
                        int v, fq_tally *t)
{
    int c[2], u, i;

    if (v < 2 * W64) {
        if (v % 2 == 0) { c[0] = fq_lane(rows, n, r->s1, v / 2);
                          return fq_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted); }
        return CQ_OP_X;
    }
    if (v == 2 * W64) return CQ_OP_X;                    /* carry_in = 1   */

    u = v - (2 * W64 + 1);
    i = u / 4;
    switch (u % 4) {
    case 0:  c[0] = fq_lane(rows, n, r->s0, i);
             return fq_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case 1:  return CQ_OP_CX;                            /* nb[i] -> axnb[i] */
    case 2:  c[0] = fq_lane(rows, n, r->s0, i); c[1] = CQ_BIT_Q;
             return fq_fold(CQ_OP_CCX, c, 2, &t->folded, &t->demoted);
    default: return CQ_OP_CCX;
    }
}

/* Bennett's carry recurrence (adder.jl:1-18), shared by `add` (y = b) and the
 * tail of `sub` (y = nb, which is scratch). */
static int fq_sched_ripple(int la, int ly, int u, fq_tally *t)
{
    int c[2];

    if (u < 5 * (W64 - 1)) {
        switch (u % 5) {
        case 0:  c[0] = la; return fq_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
        case 1:  c[0] = ly; return fq_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
        case 2:  c[0] = la; c[1] = ly;
                 return fq_fold(CQ_OP_CCX, c, 2, &t->folded, &t->demoted);
        case 3:  return CQ_OP_CCX;                       /* t[i], c[i]: both Q */
        default: return CQ_OP_CX;                        /* c[i] -> t[i]       */
        }
    }
    switch (u - 5 * (W64 - 1)) {
    case 0:  c[0] = la; return fq_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case 1:  c[0] = ly; return fq_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    default: return CQ_OP_CX;                            /* c[W-1] -> t[W-1]  */
    }
}

static int fq_sched_add(const cq_fsqrt_row *r, const cq_fsqrt_row *rows, int n,
                        int v, fq_tally *t)
{
    int i = (v < 5 * (W64 - 1)) ? v / 5 : W64 - 1;

    return fq_sched_ripple(fq_lane(rows, n, r->s0, i),
                           fq_lane(rows, n, r->s1, i), v, t);
}

/* `lower_sub!` (adder.jl:148-172): the 2W complement, the carry seed, then the
 * same ripple with `nb` — scratch, so its lane never folds. */
static int fq_sched_sub(const cq_fsqrt_row *r, const cq_fsqrt_row *rows, int n,
                        int v, fq_tally *t)
{
    int c[1], u, i;

    if (v < 2 * W64) {
        if (v % 2 == 0) { c[0] = fq_lane(rows, n, r->s1, v / 2);
                          return fq_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted); }
        return CQ_OP_X;
    }
    if (v == 2 * W64) return CQ_OP_X;
    u = v - (2 * W64 + 1);
    i = (u < 5 * (W64 - 1)) ? u / 5 : W64 - 1;
    return fq_sched_ripple(fq_lane(rows, n, r->s0, i), CQ_BIT_Q, u, t);
}

/* `lower_mux!` (arith.jl:522-532), one gate per slot: CX(f->r), CX(t->d),
 * CX(f->d), CCX(cond, d -> r). `cond` is a one-bit scratch flag. */
static int fq_sched_mux(const cq_fsqrt_row *r, const cq_fsqrt_row *rows, int n,
                        int v, fq_tally *t)
{
    int i = v / 4, c[2];

    switch (v % 4) {
    case 0:  c[0] = fq_lane(rows, n, r->s2, i);
             return fq_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case 1:  c[0] = fq_lane(rows, n, r->s1, i);
             return fq_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case 2:  c[0] = fq_lane(rows, n, r->s2, i);
             return fq_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    default: c[0] = CQ_BIT_Q; c[1] = CQ_BIT_Q;
             return fq_fold(CQ_OP_CCX, c, 2, &t->folded, &t->demoted);
    }
}

static int fq_scan_slot(const cq_fsqrt_row *r, const cq_fsqrt_row *rows, int n,
                        int v, fq_tally *t)
{
    int c[2], lane;

    switch (r->op) {
    case CQ_FSOP_EQ:  return fq_sched_eq (r, rows, n, v, t);
    case CQ_FSOP_ULT: return fq_sched_ult(r, rows, n, v, t);
    case CQ_FSOP_ADD: return fq_sched_add(r, rows, n, v, t);
    case CQ_FSOP_SUB: return fq_sched_sub(r, rows, n, v, t);
    case CQ_FSOP_MUX: return fq_sched_mux(r, rows, n, v, t);
    case CQ_FSOP_OR:                             /* CNOT(a), CNOT(b), CCX  */
        lane = v / 3;
        if (v % 3 == 2) {
            c[0] = fq_lane(rows, n, r->s0, lane);
            c[1] = fq_lane(rows, n, r->s1, lane);
            return fq_fold(CQ_OP_CCX, c, 2, &t->folded, &t->demoted);
        }
        c[0] = fq_lane(rows, n, (v % 3 == 0) ? r->s0 : r->s1, lane);
        return fq_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case CQ_FSOP_NOT1: return (v == 0) ? CQ_OP_CX : CQ_OP_X;
    default:           return CQ_OP_CCX;                        /* and1    */
    }
}

/* One composite block, run STANDALONE over operands with the given lane
 * profiles, recording the op kind of every gate it emits. THREE SEPARATE
 * SOURCE SPANS, because building all three views over ONE 64-lane span makes
 * two operands the same qubit and M05's §3 distinctness assert refuses the
 * fixture (M34's measured finding). A profile of ZERO is an all-constant
 * operand, which `cq_fp_view` refuses and `cq_fp_const` is for. */
static size_t fq_std_kinds(cq_mock *m, int op, int cls, const int *pfx,
                           unsigned char *out)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_scratch scr;
    cq_bit v[3][CQ_FP64_W];
    uint32_t reg;
    size_t got;
    int steps;

    switch (op) {
    case CQ_FSOP_CLASS:  reg = cq_fp_class_region((cq_fp_class)cls);
                         steps = cq_fp_class_steps((cq_fp_class)cls); break;
    case CQ_FSOP_NORM52: reg = cq_norm52_region(); steps = cq_norm52_steps(); break;
    default:             reg = cq_round_region();  steps = cq_round_steps();  break;
    }
    CHECK(steps <= FQ_STD_CAP);

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);
    cq_scratch_alloc(&scr, (uint32_t)(3 * W64) + reg);
    for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&ctx, &scr.bits[i]);
    for (int j = 0; j < 3; j++) {
        if (pfx[j] > 0)
            cq_fp_view(cq_scratch_span(&scr, (uint32_t)(j * W64), (uint32_t)W64),
                       0, pfx[j], v[j]);
        else
            cq_fp_const(v[j], UINT64_C(0));
    }
    cq_mock_reset(m);

    for (int u = 0; u < steps; u++)
        switch (op) {
        case CQ_FSOP_CLASS: {
            cq_fp_class_block c;
            c.a = v[0]; c.scr = &scr; c.off = (uint32_t)(3 * W64);
            c.cls = (cq_fp_class)cls;
            cq_fp_class_step(&ctx, &c, u); break; }
        case CQ_FSOP_NORM52: {
            cq_norm52_block b;
            b.m = v[0]; b.e = v[1]; b.scr = &scr; b.off = (uint32_t)(3 * W64);
            cq_norm52_step(&ctx, &b, u); break; }
        default: {
            cq_round_block b;
            b.wr = v[0]; b.result_exp = v[1]; b.result_sign = v[2];
            b.scr = &scr; b.off = (uint32_t)(3 * W64);
            cq_round_step(&ctx, &b, u); break; }
        }

    got = cq_mock_count(m);
    CHECK(got <= (size_t)FQ_STD_CAP);
    for (size_t g = 0; g < got; g++) out[g] = (unsigned char)cq_mock_at(m, g)->op;

    /* Leave the pool as we found it: M08's dispose asserts every bit is back
     * to CQ_BIT_ZERO — a KIND check, which a hand-driven block never
     * restores. */
    for (uint32_t i = 0; i < scr.n; i++) scr.bits[i] = cq_bit_zero();
    cq_scratch_dispose(&scr);
    cq_ctx_dispose(&ctx);
    return got;
}

static void fq_pfx_of(const cq_fsqrt_row *rows, int n, int r, int *pfx)
{
    int op = rows[r].op;

    pfx[0] = fq_prefix(rows, n, rows[r].s0);
    pfx[1] = (op == CQ_FSOP_CLASS) ? W64 : fq_prefix(rows, n, rows[r].s1);
    pfx[2] = (cq_fsqrt_arity(op) > 2) ? fq_prefix(rows, n, rows[r].s2) : W64;
}

/* How many GATES each row emits at the all-quantum operand mask, by the SAME
 * prediction and the SAME delegation the scan uses — shared with the span scan
 * next door, which walks the recorded stream with these counts to know which
 * row wrote which qubit. */
static int fq_row_gates(cq_mock *m2, unsigned char *std,
                        const cq_fsqrt_row *rows, int n, size_t *out)
{
    fq_tally t = { 0, 0 };

    for (int r = 0; r < n; r++) {
        int op = rows[r].op;

        out[r] = 0u;
        if (op == CQ_FSOP_VIEW || op == CQ_FSOP_SVIEW || op == CQ_FSOP_OUT)
            continue;

        if (FQ_DELEGATED(op)) {
            int pfx[3];

            fq_pfx_of(rows, n, r, pfx);
            if (pfx[0] < 0 || pfx[1] < 0 || pfx[2] < 0) return 0;
            out[r] = fq_std_kinds(m2, op, rows[r].s1, pfx, std);
            continue;
        }
        for (int v = 0; v < fq_ref_row_steps(&rows[r]); v++)
            if (fq_scan_slot(&rows[r], rows, n, v, &t) != FQ_PRED_NONE)
                out[r]++;
    }
    return 1;
}

CQ_TEST(the_slot_boundaries_match_an_independent_four_valued_scan)
{
    static unsigned char std[FQ_STD_CAP];
    const cq_fsqrt_row *rows;
    cq_mock m, m2;
    fq_tally t = { 0, 0 };
    size_t cursor = 0u, total;
    int n, bad = 0, delegated = 0;

    cq_mock_init(&m);
    cq_mock_init(&m2);
    rows = cq_fsqrt_rows(&n);

    /* The VALUE is checked on the way through: a scan that never looked at the
     * answer would happily pin the shape of a wrong circuit. */
    CHECK_EQ(fsqrt_run(FQ_SCAN_A, cq_ref_w_ones(W64), &m),
             ref_fsqrt(FQ_SCAN_A, 0u, W64));

    for (int r = 0; r < n && !bad; r++) {
        int op = rows[r].op;

        if (op == CQ_FSOP_VIEW || op == CQ_FSOP_SVIEW || op == CQ_FSOP_OUT)
            continue;

        if (FQ_DELEGATED(op)) {
            int pfx[3];
            size_t got;

            fq_pfx_of(rows, n, r, pfx);
            if (pfx[0] < 0 || pfx[1] < 0 || pfx[2] < 0) {
                cq_h_fail(__FILE__, __LINE__,
                          "row %d (op %d): an operand's quantum lanes are not "
                          "a prefix (%d, %d, %d) — the standalone comparison "
                          "cannot be built", r, op, pfx[0], pfx[1], pfx[2]);
                bad = 1;
                break;
            }
            got = fq_std_kinds(&m2, op, rows[r].s1, pfx, std);
            CHECK(got > 0u);
            if (cursor + got > cq_mock_count(&m)) {
                cq_h_fail(__FILE__, __LINE__,
                          "row %d (op %d): the kernel's stream has %zu gates "
                          "left and the standalone block emits %zu", r, op,
                          cq_mock_count(&m) - cursor, got);
                bad = 1;
                break;
            }
            for (size_t g = 0; g < got && !bad; g++)
                if ((unsigned char)cq_mock_at(&m, cursor + g)->op != std[g]) {
                    cq_h_fail(__FILE__, __LINE__,
                              "row %d (op %d) gate %zu (stream %zu): the "
                              "kernel emits op %d, the same block driven "
                              "standalone emits op %d", r, op, g, cursor + g,
                              (int)cq_mock_at(&m, cursor + g)->op, (int)std[g]);
                    bad = 1;
                }
            cursor += got;
            delegated++;
            continue;
        }

        for (int v = 0; v < fq_ref_row_steps(&rows[r]) && !bad; v++) {
            int pr = fq_scan_slot(&rows[r], rows, n, v, &t);
            cq_op got;

            if (pr == FQ_PRED_NONE) continue;
            if (cursor >= cq_mock_count(&m)) {
                cq_h_fail(__FILE__, __LINE__,
                          "row %d slot %d: the stream ran out at %zu gates",
                          r, v, cursor);
                bad = 1;
                break;
            }
            got = cq_mock_at(&m, cursor++)->op;
            if ((int)got != pr) {
                cq_h_fail(__FILE__, __LINE__,
                          "row %d (op %d) slot %d (stream %zu): emitted op %d, "
                          "the slot decode says %d", r, (int)rows[r].op, v,
                          cursor - 1u, (int)got, pr);
                bad = 1;
            }
        }
    }

    if (!bad) {
        /* THE SCAN IS NOT VACUOUS. `folded` is the ZERO half — every view's
         * vacated lanes, `a_lo`'s degeneration and every constant span with a
         * clear bit — and `demoted` is the ONE half, reached through `1`,
         * `BIAS`, `IMPLICIT`, `INF_BITS`, `QUIET_BIT` and `INDEF`. */
        CHECK(t.folded > 0);
        CHECK(t.demoted > 0);
        CHECK_EQ(delegated, FQ_SRC.klass + FQ_SRC.n52 + FQ_SRC.rnd);

        /* The copy-out is exactly W CNOTs — the "^=" of Rule 7's contract —
         * and the reverse half is the same `cursor` gates again. */
        total = cq_mock_count(&m);
        CHECK_EQ((long long)total, (long long)(2u * cursor + (size_t)W64));
        for (int i = 0; i < W64; i++)
            CHECK_EQ((int)cq_mock_at(&m, cursor + (size_t)i)->op, (int)CQ_OP_CX);
        CHECK(cq_mock_is_palindrome(&m, cursor, (size_t)W64));
        printf("# fsqrt slot scan: %zu gates forward, %d folded, %d demoted, "
               "%d blocks delegated\n", cursor, t.folded, t.demoted, delegated);
        fflush(stdout);
    }

    cq_mock_dispose(&m2);
    cq_mock_dispose(&m);
}
