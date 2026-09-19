/* tests/test_kernel_fdiv_scan.inc — the four-valued SLOT SCAN, the per-op
 * schedules it predicts from, and the standalone comparison it delegates the
 * five composite blocks to. Included by test_kernel_fdiv.c AFTER
 * test_kernel_fdiv_slots.inc, whose `fd_ref_row_steps` and `FD_SRC` it reuses,
 * and BEFORE test_kernel_fdiv_spans.inc, which reuses `fd_row_gates`,
 * `fd_std_kinds` and `FD_SCAN_A`/`FD_SCAN_B`.
 *
 * NOTHING BELOW READS A GOLDEN.
 *
 * THE PREDICTION IS A KIND **OR NOTHING**, AND THE STREAM HAS ITS OWN CURSOR.
 * At the all-quantum mask every v1 kernel emits exactly one gate per slot, so
 * `slot index == stream index` and K12's scan walked them together. That is
 * false here: a control lane that is a constant ZERO makes the gate vanish and
 * a constant ONE demotes it (CCX->CX, CX->X). Both happen at every mask,
 * because they come from the VIEWS and the CONSTANT SPANS rather than from the
 * operands' bit-kinds. So the scan predicts NONE / X / CX / CCX from the lane
 * table alone and advances the stream cursor only on a non-NONE.
 *
 * AND IT TAKES `t` AS WELL AS `j`, WHICH K12's DOES NOT — K17.md §2.3, and the
 * reason it is stated there before any code. Two rows of fdiv.jl:91-98 fold
 * differently at iteration 0 under D-K17-4's literal reading: `q = UInt64(0)`
 * makes `q << 1` an ALL-constant view, so :95's `or` keeps one CX per lane and
 * elides the other two, and `r_in(0) = ma` has NO constant lane where every
 * `r_in(t >= 1)` has exactly one, at lane 0. THIS SCAN GETS `t` FOR FREE
 * BECAUSE IT WALKS ABSOLUTE ROWS — `fd_lane` follows the real view chain of
 * the real row, which terminates at a CONSTANT at t = 0 and at an `or` output
 * at t >= 1 — and `iteration_zero_folds_differently_from_every_other` below
 * asserts that the asymmetry was PREDICTED rather than survived. A scan that
 * predicted from `j` alone would go red on iteration 0 and green everywhere
 * else, and the reflex fix — dropping the constant rows from the prediction —
 * turns the instrument off.
 *
 * AND THE FIVE COMPOSITE BLOCK KINDS ARE DELEGATED RATHER THAN RE-DERIVED,
 * which is K17.md §2.3's instruction and not laziness. Re-deriving M32's 154
 * rows or M31's predicate schedule inside this scan would be a SECOND
 * TRANSCRIPTION of them, which is exactly what CLAUDE.md says a slot scan must
 * not be. Instead each `class`, `norm52`, `clz`, `subnorm` and `round` row is
 * checked by RUNNING THAT BLOCK STANDALONE over operands with the same lane
 * profile and comparing the two streams gate kind for gate kind. A reader who
 * finds the interiors unpredicted must not "fix" it by writing M32's schedule
 * out here.
 */

enum { FD_PRED_NONE = -1, FD_STD_CAP = 12000 };

#define FD_DELEGATED(op)                                                       \
    ((op) == CQ_FDOP_CLASS || (op) == CQ_FDOP_NORM52 || (op) == CQ_FDOP_CLZ    \
     || (op) == CQ_FDOP_SUBNORM || (op) == CQ_FDOP_ROUND)

_Static_assert(CQ_OP_X == 0 && CQ_OP_CX == 1 && CQ_OP_CCX == 2,
               "the scan demotes a gate by decrementing its op");

/* The constant vocabulary, transcribed from softfloat_common.jl and fdiv.jl
 * rather than read out of fdiv_operand.c's `const_of` — an oracle that shares
 * a constant with the code is blind to that constant. */
static uint64_t fd_const(int s)
{
    switch (s) {
    case CQ_FD_K_ZERO:     return 0u;
    case CQ_FD_K_ONE:      return 1u;                          /* :68 :95  */
    case CQ_FD_K_BIAS:     return 1023u;                       /* :45      */
    case CQ_FD_K_IMPLICIT: return 0x0010000000000000ull;       /* common:9 */
    case CQ_FD_K_INF:      return 0x7ff0000000000000ull;       /* common:11*/
    case CQ_FD_K_QUIET:    return 0x0008000000000000ull;       /* common:13*/
    default:               return 0xfff8000000000000ull;       /* common:14*/
    }
}

/* The KIND of lane `i` of operand `s`, at the ALL-QUANTUM operand mask. Views
 * are collapsed by this file's OWN walk, so the addressing arithmetic is
 * checked as well as the boundaries. */
static int fd_lane(int s, int i)
{
    int n = cq_fdiv_n_rows(), guard = 0, shift = 0;
    uint64_t mask = ~UINT64_C(0);
    cq_fdiv_row r;

    while (s >= 0 && s < n) {
        uint64_t m;

        cq_fdiv_row_at(s, &r);
        if (r.op != CQ_FDOP_VIEW) break;
        m = r.mask;
        /* (((base >> s1) & m1) >> s2) & m2 == (base >> (s1+s2))
         *                                      & ((m1 >> s2) & m2) */
        if (shift >= 64 || shift <= -64) m = 0u;
        else if (shift >= 0)             m >>= (unsigned)shift;
        else                             m <<= (unsigned)(-shift);
        mask  &= m;
        shift += r.shift;
        s      = r.s0;
        if (++guard > n) { CHECK(0); return CQ_BIT_ZERO; }
    }
    if (((mask >> (unsigned)i) & 1u) == 0u) return CQ_BIT_ZERO;
    i += shift;
    if (i < 0 || i >= W64) return CQ_BIT_ZERO;

    if (s == CQ_FD_A || s == CQ_FD_B) return CQ_BIT_Q;       /* all-quantum */
    if (s < 0)
        return ((fd_const(s) >> (unsigned)i) & 1u) ? CQ_BIT_ONE : CQ_BIT_ZERO;

    /* A PICKED OUTPUT CAN ITSELF BE A VIEW, AND MODELLING IT AS A SCRATCH SPAN
     * IS WRONG ON 63 OF 64 LANES (bd a-picked-output-can-itself-be-a-view,
     * measured on M33). `flushed_result` is `result_sign << 63`
     * (softfloat_common.jl:183) — an operand of nothing inside M32, so it has
     * no home in the caller's region and `cq_subnorm_flushed` assembles it.
     * Getting this wrong slips the STREAM CURSOR, so the red arrives one row
     * later than the cause and names the wrong row. */
    cq_fdiv_row_at(s, &r);
    if (r.op == CQ_FDOP_OUT && r.s1 == CQ_FD_OUT_FLUSHED) {
        cq_fdiv_row p;

        cq_fdiv_row_at(r.s0, &p);
        if (p.op == CQ_FDOP_SUBNORM)
            return (i == 63) ? fd_lane(p.s2, 0) : CQ_BIT_ZERO;
    }
    return CQ_BIT_Q;             /* a span of the region: I6(b) materialises */
}

/* How many LEADING lanes of operand `s` are quantum, or -1 if the profile is
 * not a prefix. Every composite block's operand in K17 is all-quantum, which
 * is what lets the standalone run below be built out of one `cq_fp_view`. */
static int fd_prefix(int s)
{
    int k = 0;

    while (k < W64 && fd_lane(s, k) == CQ_BIT_Q) k++;
    for (int i = k; i < W64; i++)
        if (fd_lane(s, i) != CQ_BIT_ZERO) return -1;
    return k;
}

/* PRD §3's fold table, over one gate's controls. */
static int fd_fold(int kind, const int *ctrl, int nc, int *folded, int *dem)
{
    for (int i = 0; i < nc; i++) {
        if (ctrl[i] == CQ_BIT_ZERO) { if (folded) (*folded)++; return FD_PRED_NONE; }
        if (ctrl[i] == CQ_BIT_ONE) { kind--; if (dem) (*dem)++; }
    }
    return kind;
}

typedef struct { int folded, demoted; } fd_tally;

/* `lower_eq!` (arith.jl:424-447): 2W CX building `diff` — the only place an
 * operand lane is a control — then a 3-cycle CX, CX, CCX over the OR-prefix,
 * whose controls are all scratch and so cannot fold. */
static int fd_sched_eq(const cq_fdiv_row *r, int v, fd_tally *t)
{
    if (v < 2 * W64) {
        int i = v / 2, c[1];

        c[0] = fd_lane((v % 2 == 0) ? r->s0 : r->s1, i);
        return fd_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    }
    return ((v - 2 * W64) % 3 == 2) ? CQ_OP_CCX : CQ_OP_CX;
}

/* `lower_ult!` (arith.jl:449-463): 2W alternating CX/X building `~b`, the
 * carry seed, then a 4-cycle per lane. `nb`, `axnb` and `carry` are scratch. */
static int fd_sched_ult(const cq_fdiv_row *r, int v, fd_tally *t)
{
    int c[2], u, i;

    if (v < 2 * W64) {
        if (v % 2 == 0) { c[0] = fd_lane(r->s1, v / 2);
                          return fd_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted); }
        return CQ_OP_X;
    }
    if (v == 2 * W64) return CQ_OP_X;                    /* carry_in = 1 */

    u = v - (2 * W64 + 1);
    i = u / 4;
    switch (u % 4) {
    case 0:  c[0] = fd_lane(r->s0, i);
             return fd_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case 1:  return CQ_OP_CX;                            /* nb[i] -> axnb[i] */
    case 2:  c[0] = fd_lane(r->s0, i); c[1] = CQ_BIT_Q;
             return fd_fold(CQ_OP_CCX, c, 2, &t->folded, &t->demoted);
    default: return CQ_OP_CCX;
    }
}

/* Bennett's carry recurrence (adder.jl:1-18), shared by `add` (y = b) and the
 * tail of `sub` (y = nb, which is scratch). */
static int fd_sched_ripple(int la, int ly, int u, fd_tally *t)
{
    int c[2];

    if (u < 5 * (W64 - 1)) {
        switch (u % 5) {
        case 0:  c[0] = la; return fd_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
        case 1:  c[0] = ly; return fd_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
        case 2:  c[0] = la; c[1] = ly;
                 return fd_fold(CQ_OP_CCX, c, 2, &t->folded, &t->demoted);
        case 3:  return CQ_OP_CCX;                       /* t[i], c[i]: both Q */
        default: return CQ_OP_CX;                        /* c[i] -> t[i]       */
        }
    }
    switch (u - 5 * (W64 - 1)) {
    case 0:  c[0] = la; return fd_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case 1:  c[0] = ly; return fd_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    default: return CQ_OP_CX;                            /* c[W-1] -> t[W-1]  */
    }
}

static int fd_sched_add(const cq_fdiv_row *r, int v, fd_tally *t)
{
    int i = (v < 5 * (W64 - 1)) ? v / 5 : W64 - 1;

    return fd_sched_ripple(fd_lane(r->s0, i), fd_lane(r->s1, i), v, t);
}

/* `lower_sub!` (adder.jl:148-172): the 2W complement, the carry seed, then the
 * same ripple with `nb` — scratch, so its lane never folds. */
static int fd_sched_sub(const cq_fdiv_row *r, int v, fd_tally *t)
{
    int c[1], u, i;

    if (v < 2 * W64) {
        if (v % 2 == 0) { c[0] = fd_lane(r->s1, v / 2);
                          return fd_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted); }
        return CQ_OP_X;
    }
    if (v == 2 * W64) return CQ_OP_X;
    u = v - (2 * W64 + 1);
    i = (u < 5 * (W64 - 1)) ? u / 5 : W64 - 1;
    return fd_sched_ripple(fd_lane(r->s0, i), CQ_BIT_Q, u, t);
}

/* `lower_mux!` (arith.jl:522-532), one gate per slot: CX(f->r), CX(t->d),
 * CX(f->d), CCX(cond, d -> r). `cond` is a one-bit scratch flag. */
static int fd_sched_mux(const cq_fdiv_row *r, int v, fd_tally *t)
{
    int i = v / 4, c[2];

    switch (v % 4) {
    case 0:  c[0] = fd_lane(r->s2, i);
             return fd_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case 1:  c[0] = fd_lane(r->s1, i);
             return fd_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case 2:  c[0] = fd_lane(r->s2, i);
             return fd_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    default: c[0] = CQ_BIT_Q; c[1] = CQ_BIT_Q;
             return fd_fold(CQ_OP_CCX, c, 2, &t->folded, &t->demoted);
    }
}

static int fd_scan_slot(const cq_fdiv_row *r, int v, fd_tally *t)
{
    int c[2], lane;

    switch (r->op) {
    case CQ_FDOP_EQ:  return fd_sched_eq (r, v, t);
    case CQ_FDOP_ULT: return fd_sched_ult(r, v, t);
    case CQ_FDOP_ADD: return fd_sched_add(r, v, t);
    case CQ_FDOP_SUB: return fd_sched_sub(r, v, t);
    case CQ_FDOP_MUX: return fd_sched_mux(r, v, t);
    case CQ_FDOP_OR:                             /* CNOT(a), CNOT(b), CCX  */
        lane = v / 3;
        if (v % 3 == 2) {
            c[0] = fd_lane(r->s0, lane);
            c[1] = fd_lane(r->s1, lane);
            return fd_fold(CQ_OP_CCX, c, 2, &t->folded, &t->demoted);
        }
        c[0] = fd_lane((v % 3 == 0) ? r->s0 : r->s1, lane);
        return fd_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case CQ_FDOP_XOR:                            /* CNOT(a), CNOT(b)       */
        lane = v / 2;
        c[0] = fd_lane((v % 2 == 0) ? r->s0 : r->s1, lane);
        return fd_fold(CQ_OP_CX, c, 1, &t->folded, &t->demoted);
    case CQ_FDOP_NOT1: return (v == 0) ? CQ_OP_CX : CQ_OP_X;
    case CQ_FDOP_AND1: return CQ_OP_CCX;
    default:           return (v == 2) ? CQ_OP_CCX : CQ_OP_CX;   /* or1     */
    }
}

/* One composite block, run STANDALONE over operands with the given lane
 * profiles, recording the op kind of every gate it emits.
 *
 * THREE SEPARATE SOURCE SPANS, AND ONE WOULD NOT BE ENOUGH — M34's measured
 * finding, carried here because the shape is the same: building all three
 * views over ONE 64-lane span makes two operands the same qubit, and M05's §3
 * distinctness assert refuses a gate with coincident controls. That is the
 * fixture being wrong, not the kernel. */
static size_t fd_std_kinds(cq_mock *m, int op, int cls, const int *pfx,
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
    case CQ_FDOP_CLASS:   reg = cq_fp_class_region((cq_fp_class)cls);
                          steps = cq_fp_class_steps((cq_fp_class)cls); break;
    case CQ_FDOP_NORM52:  reg = cq_norm52_region();  steps = cq_norm52_steps(); break;
    case CQ_FDOP_CLZ:     reg = cq_clz_region();     steps = cq_clz_steps();  break;
    case CQ_FDOP_SUBNORM: reg = cq_subnorm_region(); steps = cq_subnorm_steps(); break;
    default:              reg = cq_round_region();   steps = cq_round_steps(); break;
    }
    CHECK(steps <= FD_STD_CAP);

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
        case CQ_FDOP_CLASS: {
            cq_fp_class_block c;
            c.a = v[0]; c.scr = &scr; c.off = (uint32_t)(3 * W64);
            c.cls = (cq_fp_class)cls;
            cq_fp_class_step(&ctx, &c, u); break; }
        case CQ_FDOP_NORM52: {
            cq_norm52_block b;
            b.m = v[0]; b.e = v[1]; b.scr = &scr; b.off = (uint32_t)(3 * W64);
            cq_norm52_step(&ctx, &b, u); break; }
        case CQ_FDOP_CLZ: {
            cq_clz_block b;
            b.wr = v[0]; b.result_exp = v[1]; b.scr = &scr;
            b.off = (uint32_t)(3 * W64);
            cq_clz_step(&ctx, &b, u); break; }
        case CQ_FDOP_SUBNORM: {
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
    CHECK(got <= (size_t)FD_STD_CAP);
    for (size_t g = 0; g < got; g++) out[g] = (unsigned char)cq_mock_at(m, g)->op;

    /* Leave the pool as we found it: the scratch region is the caller's here
     * and M08's dispose asserts every bit is back to CQ_BIT_ZERO — a KIND
     * check, which a hand-driven block never restores. */
    for (uint32_t i = 0; i < scr.n; i++) scr.bits[i] = cq_bit_zero();
    cq_scratch_dispose(&scr);
    cq_ctx_dispose(&ctx);
    return got;
}

/* The three lane profiles of a delegated row, in operand order. Returns 0 if
 * one of them is not a prefix, which is the one thing that would make the
 * standalone run unbuildable. */
static int fd_pfx3(const cq_fdiv_row *r, int *pfx)
{
    pfx[0] = fd_prefix(r->s0);
    pfx[1] = (r->op == CQ_FDOP_CLASS) ? W64 : fd_prefix(r->s1);
    pfx[2] = (cq_fdiv_arity(r->op) > 2) ? fd_prefix(r->s2) : W64;
    return pfx[0] >= 1 && pfx[1] >= 1 && pfx[2] >= 1;
}

/* How many GATES each row emits at the all-quantum operand mask, by the SAME
 * prediction and the SAME delegation the scan uses — shared with the span scan
 * next door, which walks the recorded stream with these counts to know which
 * row wrote which qubit. `gates` is cq_fdiv_n_rows() entries. */
static int fd_row_gates(cq_mock *m2, unsigned char *std, size_t *out)
{
    fd_tally t = { 0, 0 };
    int n = cq_fdiv_n_rows();

    for (int i = 0; i < n; i++) {
        cq_fdiv_row r;

        out[i] = 0u;
        cq_fdiv_row_at(i, &r);
        if (r.op == CQ_FDOP_VIEW || r.op == CQ_FDOP_OUT) continue;

        if (FD_DELEGATED(r.op)) {
            int pfx[3];

            if (!fd_pfx3(&r, pfx)) return 0;
            out[i] = fd_std_kinds(m2, r.op, r.s1, pfx, std);
            continue;
        }
        for (int v = 0; v < fd_ref_row_steps(&r); v++)
            if (fd_scan_slot(&r, v, &t) != FD_PRED_NONE) out[i]++;
    }
    return 1;
}

#define FD_SCAN_A  FD_TIEUP_A
#define FD_SCAN_B  FD_POW2_1000

/* ---- (b) The four-valued slot scan. ------------------------------------ */

CQ_TEST(the_slot_boundaries_match_an_independent_four_valued_scan)
{
    static unsigned char std[FD_STD_CAP];
    cq_mock m, m2;
    cq_ref_w q;
    fd_tally t = { 0, 0 };
    size_t cursor = 0u, total;
    int n = cq_fdiv_n_rows(), bad = 0, delegated = 0;

    cq_mock_init(&m);
    cq_mock_init(&m2);
    q = cq_ref_w_ones(W64);

    /* The VALUE is checked on the way through: a scan that never looked at the
     * answer would happily pin the shape of a wrong circuit. */
    CHECK_EQ(fdiv_run(FD_SCAN_A, FD_SCAN_B, q, q, &m),
             ref_fdiv(FD_SCAN_A, FD_SCAN_B, W64));

    for (int r = 0; r < n && !bad; r++) {
        cq_fdiv_row row;

        cq_fdiv_row_at(r, &row);
        if (row.op == CQ_FDOP_VIEW || row.op == CQ_FDOP_OUT) continue;

        if (FD_DELEGATED(row.op)) {
            int pfx[3];
            size_t got;

            if (!fd_pfx3(&row, pfx)) {
                cq_h_fail(__FILE__, __LINE__,
                          "row %d (op %d): an operand's quantum lanes are not "
                          "a prefix (%d, %d, %d) — the standalone comparison "
                          "cannot be built", r, (int)row.op, pfx[0], pfx[1],
                          pfx[2]);
                bad = 1;
                break;
            }
            got = fd_std_kinds(&m2, row.op, row.s1, pfx, std);
            CHECK(got > 0u);
            if (cursor + got > cq_mock_count(&m)) {
                cq_h_fail(__FILE__, __LINE__,
                          "row %d (op %d): the kernel's stream has %zu gates "
                          "left and the standalone block emits %zu", r,
                          (int)row.op, cq_mock_count(&m) - cursor, got);
                bad = 1;
                break;
            }
            for (size_t g = 0; g < got && !bad; g++)
                if ((unsigned char)cq_mock_at(&m, cursor + g)->op != std[g]) {
                    cq_h_fail(__FILE__, __LINE__,
                              "row %d (op %d) gate %zu (stream %zu): the "
                              "kernel emits op %d, the same block driven "
                              "standalone emits op %d", r, (int)row.op, g,
                              cursor + g, (int)cq_mock_at(&m, cursor + g)->op,
                              (int)std[g]);
                    bad = 1;
                }
            cursor += got;
            delegated++;
            continue;
        }

        for (int v = 0; v < fd_ref_row_steps(&row) && !bad; v++) {
            int pr = fd_scan_slot(&row, v, &t);
            cq_op got;

            if (pr == FD_PRED_NONE) continue;
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
                          "the slot decode says %d", r, (int)row.op, v,
                          cursor - 1u, (int)got, pr);
                bad = 1;
            }
        }
    }

    if (!bad) {
        /* THE SCAN IS NOT VACUOUS. `folded` is the ZERO half — every view's
         * vacated lanes, iteration 0's all-constant `q << 1`, and every
         * constant span with a clear bit — and `demoted` is the ONE half,
         * reached through `IMPLICIT`, `INF_BITS`, `QUIET_BIT`, `INDEF`, `BIAS`
         * and the literal 1. */
        CHECK(t.folded > 0);
        CHECK(t.demoted > 0);
        CHECK_EQ(delegated, FD_SRC.klass + FD_SRC.n52 + FD_SRC.clz
                            + FD_SRC.subn + FD_SRC.rnd);

        /* The copy-out is exactly W CNOTs — the "^=" of Rule 7's contract —
         * and the reverse half is the same `cursor` gates again. */
        total = cq_mock_count(&m);
        CHECK_EQ((long long)total, (long long)(2u * cursor + (size_t)W64));
        for (int i = 0; i < W64; i++)
            CHECK_EQ((int)cq_mock_at(&m, cursor + (size_t)i)->op, (int)CQ_OP_CX);
        CHECK(cq_mock_is_palindrome(&m, cursor, (size_t)W64));
        printf("# fdiv slot scan: %zu gates forward, %d folded, %d demoted, "
               "%d blocks delegated\n", cursor, t.folded, t.demoted, delegated);
        fflush(stdout);
    }

    cq_mock_dispose(&m2);
    cq_mock_dispose(&m);
}

/* ---- (b2) Iteration 0 folds differently, and the scan predicted it. ---- */

/* D-K17-4's ONLY OBSERVABLE, AND IT IS A GATE COUNT RATHER THAN A SLOT COUNT.
 * The literal reading makes `q_0` an all-constant view and `r_in(0) = ma` a
 * span with no constant lane; a pre-materialised zero register would make both
 * uniform. Slots are identical either way — which is why the composition check
 * cannot see this — so what distinguishes the two readings is exactly the fold
 * pattern of two rows of iteration 0.
 *
 * THIS CASE IS ALSO THE ONE THAT WOULD GO RED IF THE SCAN PREDICTED FROM `j`
 * ALONE. It asserts the lane profiles directly, so the claim "the scan takes
 * `t`" is executed rather than argued. */
CQ_TEST(iteration_zero_folds_differently_from_every_other)
{
    int base = cq_fdiv_loop_base_row();
    cq_fdiv_row h0_or, h1_or, h0_ult, h1_ult;
    fd_tally t0 = { 0, 0 }, t1 = { 0, 0 };
    int g0 = 0, g1 = 0;

    cq_fdiv_row_at(base + 5, &h0_or);                    /* :95 or, t = 0  */
    cq_fdiv_row_at(base + CQ_FDIV_ITER_ROWS + 5, &h1_or);/* :95 or, t = 1  */
    cq_fdiv_row_at(base + 0, &h0_ult);                   /* :93 ult, t = 0 */
    cq_fdiv_row_at(base + CQ_FDIV_ITER_ROWS + 0, &h1_ult);

    /* `q << 1` at t = 0 is a view over the literal zero: EVERY lane constant.
     * At t >= 1 it is a view over the previous `or`'s span: lane 0 vacated by
     * the shift and the other 63 quantum. */
    for (int i = 0; i < W64; i++) {
        CHECK_EQ(fd_lane(h0_or.s0, i), CQ_BIT_ZERO);
        CHECK_EQ(fd_lane(h1_or.s0, i), (i == 0) ? CQ_BIT_ZERO : CQ_BIT_Q);
    }
    /* `r_in(0)` is `ma`, a projection with NO constant lane; `r_in(1)` is a
     * view over `rsel_0` shifted left, so lane 0 is constant. */
    for (int i = 0; i < W64; i++) {
        CHECK_EQ(fd_lane(h0_ult.s0, i), CQ_BIT_Q);
        CHECK_EQ(fd_lane(h1_ult.s0, i), (i == 0) ? CQ_BIT_ZERO : CQ_BIT_Q);
    }

    /* And the GATES differ by exactly that. `lower_or!` is CX(a), CX(b),
     * CCX(a,b) per lane: with `a` constant ZERO the first and third fold, so
     * iteration 0's `or` is W gates and every other one is 1 + 3(W-1). */
    for (int v = 0; v < fd_ref_row_steps(&h0_or); v++)
        if (fd_scan_slot(&h0_or, v, &t0) != FD_PRED_NONE) g0++;
    for (int v = 0; v < fd_ref_row_steps(&h1_or); v++)
        if (fd_scan_slot(&h1_or, v, &t1) != FD_PRED_NONE) g1++;

    CHECK_EQ(g0, W64);
    CHECK_EQ(g1, 1 + 3 * (W64 - 1));
    CHECK(g0 < g1);
    /* The SLOTS are the same, which is the half the composition check sees and
     * the reason this case has to exist at all. */
    CHECK_EQ(fd_ref_row_steps(&h0_or), fd_ref_row_steps(&h1_or));
    CHECK_EQ(t0.folded, 2 * W64);          /* the CX(a) and the CCX, W lanes */
    CHECK_EQ(t1.folded, 2);                /* lane 0 only                    */
}
