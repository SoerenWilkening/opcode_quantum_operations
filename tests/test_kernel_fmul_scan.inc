/* tests/test_kernel_fmul_scan.inc — the four-valued SLOT SCAN, the per-op
 * schedules it predicts from, and the standalone comparison it delegates the
 * five composite blocks to. Included by test_kernel_fmul.c AFTER
 * test_kernel_fmul_slots.inc, whose `fm_ref_row_steps`, `fm_lane`-free census
 * and `FM_SRC` it reuses, and BEFORE test_kernel_fmul_spans.inc, which reuses
 * `fm_row_gates`, `fm_std_kinds` and `FM_SCAN_A`/`FM_SCAN_B`.
 *
 * SPLIT OFF slots.inc ON A REAL SEAM — COMPOSITION vs SCHEDULE. Next door is
 * "how many slots are there and what do the blocks cost", which runs no kernel
 * and reads the row table; here is "which of X / CX / CCX does each slot
 * emit", which runs one and walks the recorded stream. M31's measured finding
 * is that the two come apart, and the LAYOUT half next door to that makes
 * three.
 *
 * NOTHING BELOW READS A GOLDEN.
 */

/* ---- (b) The four-valued slot scan. ------------------------------------ */

/* THE PREDICTION IS A KIND **OR NOTHING**, AND THE STREAM HAS ITS OWN CURSOR.
 * At the all-quantum mask every v1 kernel emits exactly one gate per slot, so
 * `slot index == stream index` and K12's scan walked them together. That is
 * false here: a control lane that is a constant ZERO makes the gate vanish and
 * a constant ONE demotes it (CCX->CX, CX->X). Both happen at every mask,
 * because they come from the VIEWS and the CONSTANT SPANS rather than from the
 * operands' bit-kinds. So the scan predicts NONE / X / CX / CCX from the lane
 * table alone and advances the stream cursor only on a non-NONE.
 *
 * AND THE FIVE COMPOSITE BLOCKS ARE DELEGATED RATHER THAN RE-DERIVED, WHICH IS
 * K16.md §2.6's instruction and not laziness. Re-deriving `cq_mul_step`'s
 * 26,336-slot two-level schedule inside this scan would be a SECOND
 * TRANSCRIPTION OF K11, which is exactly what CLAUDE.md says a slot scan must
 * not be, and the same goes for M32's 154 rows. Instead each `mul`, `norm52`,
 * `clz`, `subnorm` and `round` row is checked by RUNNING THAT BLOCK STANDALONE
 * over operands with the same lane profile and comparing the two streams gate
 * kind for gate kind. That is stronger than a boundary check and it reads no
 * golden: it is M18's and M32's own circuits against the same circuits driven
 * through M34's dispatch. A reader who finds the interiors unpredicted must
 * not "fix" it by writing K11's schedule out here. */
enum { FM_PRED_NONE = -1, FM_STD_CAP = 30000 };

/* THE SIX BLOCK TYPES THE SCAN DELEGATES RATHER THAN RE-DERIVES. M18's
 * multiplier, M32's four shared helpers and M31's class predicate: each has a
 * suite of its own that owns its schedule, and re-transcribing one here would
 * make this scan a second copy of it (K16.md §2.6 (2)). */
#define FM_DELEGATED(op)                                                       \
    ((op) == CQ_FMOP_MUL || (op) == CQ_FMOP_NORM52 || (op) == CQ_FMOP_CLZ      \
     || (op) == CQ_FMOP_SUBNORM || (op) == CQ_FMOP_ROUND                       \
     || (op) == CQ_FMOP_CLASS)

_Static_assert(CQ_OP_X == 0 && CQ_OP_CX == 1 && CQ_OP_CCX == 2,
               "the scan demotes a gate by decrementing its op");

/* The constant vocabulary, transcribed from softfloat_common.jl and fmul.jl
 * rather than read out of fmul_step.c's `const_of` — an oracle that shares a
 * constant with the code is blind to that constant. */
static uint64_t fm_const(int s)
{
    switch (s) {
    case CQ_FM_K_ZERO:     return 0u;
    case CQ_FM_K_ONE:      return 1u;
    case CQ_FM_K_7FF:      return 0x7ffull;                    /* :31      */
    case CQ_FM_K_BIAS:     return 1023u;                       /* :16      */
    case CQ_FM_K_IMPLICIT: return 0x0010000000000000ull;       /* common:9 */
    case CQ_FM_K_INF:      return 0x7ff0000000000000ull;       /* common:11*/
    case CQ_FM_K_QUIET:    return 0x0008000000000000ull;       /* common:13*/
    default:               return 0xfff8000000000000ull;       /* common:14*/
    }
}

/* The KIND of lane `i` of operand `s`, at the ALL-QUANTUM operand mask. Views
 * are collapsed by this file's OWN walk, so the addressing arithmetic is
 * checked as well as the boundaries. */
static int fm_lane(const cq_fmul_row *rows, int n, int s, int i)
{
    int guard = 0, shift = 0;
    uint64_t mask = ~UINT64_C(0);

    while (s >= 0 && s < n && rows[s].op == CQ_FMOP_VIEW) {
        int sh = rows[s].shift;
        uint64_t m = rows[s].mask;

        /* (((base >> s1) & m1) >> s2) & m2 == (base >> (s1+s2))
         *                                      & ((m1 >> s2) & m2) */
        if (shift >= 64 || shift <= -64) m = 0u;
        else if (shift >= 0)             m >>= (unsigned)shift;
        else                             m <<= (unsigned)(-shift);
        mask  &= m;
        shift += sh;
        s      = rows[s].s0;
        if (++guard > n) { CHECK(0); return CQ_BIT_ZERO; }
    }
    if (((mask >> (unsigned)i) & 1u) == 0u) return CQ_BIT_ZERO;
    i += shift;
    if (i < 0 || i >= W64) return CQ_BIT_ZERO;

    if (s == CQ_FM_A || s == CQ_FM_B) return CQ_BIT_Q;       /* all-quantum */
    if (s < 0)
        return ((fm_const(s) >> (unsigned)i) & 1u) ? CQ_BIT_ONE : CQ_BIT_ZERO;
    if (rows[s].op == CQ_FMOP_OUT && rows[s].s1 == CQ_FM_OUT_FLUSHED
        && rows[rows[s].s0].op == CQ_FMOP_SUBNORM)
        /* `flushed_result = result_sign << 63` (common.jl:183) is a VIEW over
         * the block's third input and not a span of its region. */
        return (i == 63) ? fm_lane(rows, n, rows[rows[s].s0].s2, 0)
                         : CQ_BIT_ZERO;
    return CQ_BIT_Q;             /* a span of the region: I6(b) materialises */
}

/* How many LEADING lanes of operand `s` are quantum, or -1 if the profile is
 * not a prefix. Every composite block's operand in K16 is a prefix, which is
 * what lets the standalone run below be built out of one `cq_fp_view`. */
static int fm_prefix(const cq_fmul_row *rows, int n, int s)
{
    int k = 0;

    while (k < W64 && fm_lane(rows, n, s, k) == CQ_BIT_Q) k++;
    for (int i = k; i < W64; i++)
        if (fm_lane(rows, n, s, i) != CQ_BIT_ZERO) return -1;
    return k;
}

/* PRD §3's fold table, over one gate's controls. */
static int fm_fold(int kind, const int *ctrl, int nc, int *folded, int *dem)
{
    for (int i = 0; i < nc; i++) {
        if (ctrl[i] == CQ_BIT_ZERO) { if (folded) (*folded)++; return FM_PRED_NONE; }
        if (ctrl[i] == CQ_BIT_ONE) { kind--; if (dem) (*dem)++; }
    }
    return kind;
}

typedef struct { int folded, demoted; } fm_tally;

/* `lower_eq!` (arith.jl:424-447): 2W CX building `diff` — the only place an
 * operand lane is a control — then a 3-cycle CX, CX, CCX over the OR-prefix,
 * whose controls are all scratch and so cannot fold. */
static int fm_sched_eq(const cq_fmul_row *r, const cq_fmul_row *rows, int n,
                       int v, fm_tally *t)
{
    if (v < 2 * W64) {
        int i = v / 2, c[1];

        c[0] = fm_lane(rows, n, (v % 2 == 0) ? r->s0 : r->s1, i);
        return fm_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    }
    return ((v - 2 * W64) % 3 == 2) ? CQ_OP_CCX : CQ_OP_CX;
}

/* `lower_ult!` (arith.jl:449-463): 2W alternating CX/X building `~b`, the
 * carry seed, then a 4-cycle per lane. `nb`, `axnb` and `carry` are scratch. */
static int fm_sched_ult(const cq_fmul_row *r, const cq_fmul_row *rows, int n,
                        int v, fm_tally *t)
{
    int c[2], u, i;

    if (v < 2 * W64) {
        if (v % 2 == 0) { c[0] = fm_lane(rows, n, r->s1, v / 2);
                          return fm_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted); }
        return CQ_OP_X;
    }
    if (v == 2 * W64) return CQ_OP_X;                    /* carry_in = 1 */

    u = v - (2 * W64 + 1);
    i = u / 4;
    switch (u % 4) {
    case 0:  c[0] = fm_lane(rows, n, r->s0, i);
             return fm_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case 1:  return CQ_OP_CX;                            /* nb[i] -> axnb[i] */
    case 2:  c[0] = fm_lane(rows, n, r->s0, i); c[1] = CQ_BIT_Q;
             return fm_fold(CQ_OP_CCX, c, 2, &t->folded, &t->demoted);
    default: return CQ_OP_CCX;
    }
}

/* Bennett's carry recurrence (adder.jl:1-18), shared by `add` (y = b) and the
 * tail of `sub` (y = nb, which is scratch). */
static int fm_sched_ripple(int la, int ly, int u, fm_tally *t)
{
    int c[2];

    if (u < 5 * (W64 - 1)) {
        switch (u % 5) {
        case 0:  c[0] = la; return fm_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
        case 1:  c[0] = ly; return fm_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
        case 2:  c[0] = la; c[1] = ly;
                 return fm_fold(CQ_OP_CCX, c, 2, &t->folded, &t->demoted);
        case 3:  return CQ_OP_CCX;                       /* t[i], c[i]: both Q */
        default: return CQ_OP_CX;                        /* c[i] -> t[i]       */
        }
    }
    switch (u - 5 * (W64 - 1)) {
    case 0:  c[0] = la; return fm_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case 1:  c[0] = ly; return fm_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    default: return CQ_OP_CX;                            /* c[W-1] -> t[W-1]  */
    }
}

static int fm_sched_add(const cq_fmul_row *r, const cq_fmul_row *rows, int n,
                        int v, fm_tally *t)
{
    int i = (v < 5 * (W64 - 1)) ? v / 5 : W64 - 1;

    return fm_sched_ripple(fm_lane(rows, n, r->s0, i),
                           fm_lane(rows, n, r->s1, i), v, t);
}

/* `lower_sub!` (adder.jl:148-172): the 2W complement, the carry seed, then the
 * same ripple with `nb` — scratch, so its lane never folds. */
static int fm_sched_sub(const cq_fmul_row *r, const cq_fmul_row *rows, int n,
                        int v, fm_tally *t)
{
    int c[1], u, i;

    if (v < 2 * W64) {
        if (v % 2 == 0) { c[0] = fm_lane(rows, n, r->s1, v / 2);
                          return fm_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted); }
        return CQ_OP_X;
    }
    if (v == 2 * W64) return CQ_OP_X;
    u = v - (2 * W64 + 1);
    i = (u < 5 * (W64 - 1)) ? u / 5 : W64 - 1;
    return fm_sched_ripple(fm_lane(rows, n, r->s0, i), CQ_BIT_Q, u, t);
}

/* `lower_mux!` (arith.jl:522-532), one gate per slot: CX(f->r), CX(t->d),
 * CX(f->d), CCX(cond, d -> r). `cond` is a one-bit scratch flag. */
static int fm_sched_mux(const cq_fmul_row *r, const cq_fmul_row *rows, int n,
                        int v, fm_tally *t)
{
    int i = v / 4, c[2];

    switch (v % 4) {
    case 0:  c[0] = fm_lane(rows, n, r->s2, i);
             return fm_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case 1:  c[0] = fm_lane(rows, n, r->s1, i);
             return fm_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case 2:  c[0] = fm_lane(rows, n, r->s2, i);
             return fm_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    default: c[0] = CQ_BIT_Q; c[1] = CQ_BIT_Q;
             return fm_fold(CQ_OP_CCX, c, 2, &t->folded, &t->demoted);
    }
}

static int fm_scan_slot(const cq_fmul_row *r, const cq_fmul_row *rows, int n,
                        int v, fm_tally *t)
{
    int c[2], lane;

    switch (r->op) {
    case CQ_FMOP_EQ:  return fm_sched_eq (r, rows, n, v, t);
    case CQ_FMOP_ULT: return fm_sched_ult(r, rows, n, v, t);
    case CQ_FMOP_ADD: return fm_sched_add(r, rows, n, v, t);
    case CQ_FMOP_SUB: return fm_sched_sub(r, rows, n, v, t);
    case CQ_FMOP_MUX: return fm_sched_mux(r, rows, n, v, t);
    case CQ_FMOP_OR:                             /* CNOT(a), CNOT(b), CCX  */
        lane = v / 3;
        if (v % 3 == 2) {
            c[0] = fm_lane(rows, n, r->s0, lane);
            c[1] = fm_lane(rows, n, r->s1, lane);
            return fm_fold(CQ_OP_CCX, c, 2, &t->folded, &t->demoted);
        }
        c[0] = fm_lane(rows, n, (v % 3 == 0) ? r->s0 : r->s1, lane);
        return fm_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case CQ_FMOP_XOR:                            /* CNOT(a), CNOT(b)       */
        lane = v / 2;
        c[0] = fm_lane(rows, n, (v % 2 == 0) ? r->s0 : r->s1, lane);
        return fm_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case CQ_FMOP_NOT1: return (v == 0) ? CQ_OP_CX : CQ_OP_X;
    case CQ_FMOP_AND1: return CQ_OP_CCX;
    default:           return (v == 2) ? CQ_OP_CCX : CQ_OP_CX;   /* or1     */
    }
}

/* One composite block, run STANDALONE over operands with the given lane
 * profiles, recording the op kind of every gate it emits. The profiles come
 * from `fm_prefix`, so the scratch this block runs over is a fresh region and
 * its operands are views over all-quantum spans — the same kinds the kernel
 * hands it, at different indices.
 *
 * THREE SEPARATE SOURCE SPANS, AND ONE WAS NOT ENOUGH — measured, in Debug, by
 * M05's own §3 distinctness assert. Building all three views over ONE 64-lane
 * span makes `a[i]` and `b[i]` the same qubit, so `cq_mul_block`'s phase-P
 * Toffoli gets two coincident controls and the library refuses it (PRD §9 row
 * B / Rule 11). That is the fixture being wrong, not the kernel: inside K16
 * the four products read views over `ma'` and `mb'`, two different spans,
 * which `the_four_products_and_the_106_bit_lane_map` asserts separately. */
static size_t fm_std_kinds(cq_mock *m, int op, int cls, const int *pfx,
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
    case CQ_FMOP_CLASS:   reg = cq_fp_class_region((cq_fp_class)cls);
                          steps = cq_fp_class_steps((cq_fp_class)cls); break;
    case CQ_FMOP_MUL:     reg = (uint32_t)cq_mul_region(W64);
                          steps = cq_mul_steps(W64); break;
    case CQ_FMOP_NORM52:  reg = cq_norm52_region();  steps = cq_norm52_steps(); break;
    case CQ_FMOP_CLZ:     reg = cq_clz_region();     steps = cq_clz_steps();  break;
    case CQ_FMOP_SUBNORM: reg = cq_subnorm_region(); steps = cq_subnorm_steps(); break;
    default:              reg = cq_round_region();   steps = cq_round_steps(); break;
    }
    CHECK(steps <= FM_STD_CAP);

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);
    cq_scratch_alloc(&scr, (uint32_t)(3 * W64) + reg);
    for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&ctx, &scr.bits[i]);
    for (int j = 0; j < 3; j++)
        cq_fp_view(cq_scratch_span(&scr, (uint32_t)(j * W64), (uint32_t)W64),
                   0, pfx[j], v[j]);
    cq_mock_reset(m);

    for (int u = 0; u < steps; u++)
        switch (op) {
        case CQ_FMOP_CLASS: {
            cq_fp_class_block c;
            c.a = v[0]; c.scr = &scr; c.off = (uint32_t)(3 * W64);
            c.cls = (cq_fp_class)cls;
            cq_fp_class_step(&ctx, &c, u); break; }
        case CQ_FMOP_MUL: {
            cq_mul_block b;
            b.a = v[0]; b.b = v[1]; b.scr = &scr;
            b.off = (uint32_t)(3 * W64); b.W = W64;
            cq_mul_step(&ctx, &b, u); break; }
        case CQ_FMOP_NORM52: {
            cq_norm52_block b;
            b.m = v[0]; b.e = v[1]; b.scr = &scr; b.off = (uint32_t)(3 * W64);
            cq_norm52_step(&ctx, &b, u); break; }
        case CQ_FMOP_CLZ: {
            cq_clz_block b;
            b.wr = v[0]; b.result_exp = v[1]; b.scr = &scr;
            b.off = (uint32_t)(3 * W64);
            cq_clz_step(&ctx, &b, u); break; }
        case CQ_FMOP_SUBNORM: {
            cq_subnorm_block b;
            b.wr = v[0]; b.result_exp = v[1]; b.result_sign = v[2];
            b.scr = &scr; b.off = (uint32_t)(3 * W64);
            cq_subnorm_step(&ctx, &b, u); break; }
        default: {
            cq_round_block b;
            b.wr = v[0]; b.result_exp = v[1]; b.result_sign = v[2];
            b.scr = &scr; b.off = (uint32_t)(3 * W64);
            cq_round_step(&ctx, &b, u); break; }
        }

    got = cq_mock_count(m);
    CHECK(got <= (size_t)FM_STD_CAP);
    for (size_t g = 0; g < got; g++) out[g] = (unsigned char)cq_mock_at(m, g)->op;

    /* Leave the pool as we found it: the scratch region is the caller's here
     * and M08's dispose asserts every bit is back to CQ_BIT_ZERO — a KIND
     * check, which a hand-driven block never restores. */
    for (uint32_t i = 0; i < scr.n; i++) scr.bits[i] = cq_bit_zero();
    cq_scratch_dispose(&scr);
    cq_ctx_dispose(&ctx);
    return got;
}

/* How many GATES each row emits at the all-quantum operand mask, by the SAME
 * prediction and the SAME delegation the scan uses — shared with the span
 * scan next door, which walks the recorded stream with these counts to know
 * which row wrote which qubit. Returns 0 if a row's operand profile is not a
 * prefix, which is the one thing that would make the standalone run
 * unbuildable. */
static int fm_row_gates(cq_mock *m2, unsigned char *std,
                        const cq_fmul_row *rows, int n, size_t *out)
{
    fm_tally t = { 0, 0 };

    for (int r = 0; r < n; r++) {
        int op = rows[r].op;

        out[r] = 0u;
        if (op == CQ_FMOP_VIEW || op == CQ_FMOP_OUT) continue;

        if (FM_DELEGATED(op)) {
            int pfx[3];

            pfx[0] = fm_prefix(rows, n, rows[r].s0);
            pfx[1] = (op == CQ_FMOP_CLASS) ? W64
                                           : fm_prefix(rows, n, rows[r].s1);
            pfx[2] = (cq_fmul_arity(op) > 2) ? fm_prefix(rows, n, rows[r].s2)
                                             : W64;
            if (pfx[0] < 1 || pfx[1] < 1 || pfx[2] < 1) return 0;
            out[r] = fm_std_kinds(m2, op, rows[r].s1, pfx, std);
            continue;
        }
        for (int v = 0; v < fm_ref_row_steps(&rows[r]); v++)
            if (fm_scan_slot(&rows[r], rows, n, v, &t) != FM_PRED_NONE)
                out[r]++;
    }
    return 1;
}

#define FM_SCAN_A  FM_GRS4_A
#define FM_SCAN_B  FM_GRS4_B

CQ_TEST(the_slot_boundaries_match_an_independent_four_valued_scan)
{
    static unsigned char std[FM_STD_CAP];
    const cq_fmul_row *rows;
    cq_mock m, m2;
    cq_ref_w q;
    fm_tally t = { 0, 0 };
    size_t cursor = 0u, total;
    int n, bad = 0, delegated = 0;

    cq_mock_init(&m);
    cq_mock_init(&m2);
    rows = cq_fmul_rows(&n);
    q = cq_ref_w_ones(W64);

    /* The VALUE is checked on the way through: a scan that never looked at the
     * answer would happily pin the shape of a wrong circuit. */
    CHECK_EQ(fmul_run(FM_SCAN_A, FM_SCAN_B, q, q, &m),
             ref_fmul(FM_SCAN_A, FM_SCAN_B, W64));

    for (int r = 0; r < n && !bad; r++) {
        int op = rows[r].op;

        if (op == CQ_FMOP_VIEW || op == CQ_FMOP_OUT) continue;

        if (FM_DELEGATED(op)) {
            int pfx[3];
            size_t got;

            pfx[0] = fm_prefix(rows, n, rows[r].s0);
            pfx[1] = (op == CQ_FMOP_CLASS) ? W64
                                           : fm_prefix(rows, n, rows[r].s1);
            pfx[2] = (cq_fmul_arity(op) > 2) ? fm_prefix(rows, n, rows[r].s2)
                                             : W64;
            if (pfx[0] < 1 || pfx[1] < 1 || pfx[2] < 1) {
                cq_h_fail(__FILE__, __LINE__,
                          "row %d (op %d): an operand's quantum lanes are not "
                          "a prefix (%d, %d, %d) — the standalone comparison "
                          "cannot be built", r, op, pfx[0], pfx[1], pfx[2]);
                bad = 1;
                break;
            }
            got = fm_std_kinds(&m2, op, rows[r].s1, pfx, std);
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
                              (int)cq_mock_at(&m, cursor + g)->op,
                              (int)std[g]);
                    bad = 1;
                }
            cursor += got;
            delegated++;
            continue;
        }

        for (int v = 0; v < fm_ref_row_steps(&rows[r]) && !bad; v++) {
            int pr = fm_scan_slot(&rows[r], rows, n, v, &t);
            cq_op got;

            if (pr == FM_PRED_NONE) continue;
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
         * vacated lanes and every constant span with a clear bit — and
         * `demoted` is the ONE half, reached through `0x7FF`, `IMPLICIT`,
         * `INF_BITS`, `QUIET_BIT`, `INDEF`, `BIAS` and the literal 1. */
        CHECK(t.folded > 0);
        CHECK(t.demoted > 0);
        CHECK_EQ(delegated, FM_SRC.mul + FM_SRC.n52 + FM_SRC.clz
                            + FM_SRC.subn + FM_SRC.rnd + FM_SRC.klass);

        /* The copy-out is exactly W CNOTs — the "^=" of Rule 7's contract —
         * and the reverse half is the same `cursor` gates again. */
        total = cq_mock_count(&m);
        CHECK_EQ((long long)total, (long long)(2u * cursor + (size_t)W64));
        for (int i = 0; i < W64; i++)
            CHECK_EQ((int)cq_mock_at(&m, cursor + (size_t)i)->op, (int)CQ_OP_CX);
        CHECK(cq_mock_is_palindrome(&m, cursor, (size_t)W64));
        printf("# fmul slot scan: %zu gates forward, %d folded, %d demoted, "
               "%d blocks delegated\n", cursor, t.folded, t.demoted, delegated);
        fflush(stdout);
    }

    cq_mock_dispose(&m2);
    cq_mock_dispose(&m);
}
