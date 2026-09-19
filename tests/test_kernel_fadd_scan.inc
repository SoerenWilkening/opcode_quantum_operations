/* tests/test_kernel_fadd_scan.inc — THE FOUR-VALUED SLOT SCAN. Included by
 * test_kernel_fadd.c AFTER test_kernel_fadd_slots.inc, whose `fa_lane`,
 * `fa_row_slots` and `fa_probe` it reuses. NOTHING HERE READS A GOLDEN.
 *
 * WHAT A K12-SHAPED KERNEL GETS WRONG IS THE SLOT ARITHMETIC, NOT THE GATES
 * (CLAUDE.md's callout, PRD-v2 §7.1), and K15 is K12's shape with 133 row
 * boundaries where K12 has four. The instrument re-derives the op KIND of
 * every compute-half slot from the blocks' own structure and compares it
 * against the recorded stream at the all-quantum operand mask. It records only
 * which of X / CX / CCX each slot emits — exactly what a boundary error moves
 * and what a gate-list error does not — so it is not a second transcription of
 * anything.
 *
 * THE KIND IS FOUR-VALUED AND THE STREAM HAS ITS OWN CURSOR. At the
 * all-quantum mask every v1 kernel emits exactly one gate per slot, so `slot
 * index == stream index` and K12's scan walked them together. That is false
 * here: a control lane that is a constant ZERO makes the gate vanish and a
 * constant ONE demotes it (CCX->CX, CX->X). Both happen at EVERY mask, because
 * they come from §7.3's constant spans and from the view rows rather than from
 * the operands' bit-kinds. So the scan predicts NONE / X / CX / CCX from the
 * lane table alone and advances the stream cursor only on a non-NONE. A scan
 * that kept K12's 1:1 assumption goes red on row 2 and stays red, and the
 * reflex fix — deleting the constant rows from the prediction — is what turns
 * the instrument off.
 *
 * TWO SOURCES OF PREDICTION, AND THE SPLIT IS FORCED RATHER THAN CHOSEN.
 *   - The ELEVEN carved-span ops (eq, ult, sub, add, mux and the five bitwise
 *     ones) get a SCHEDULE transcribed from the pinned lowering source, as
 *     M36's scan does, because their operand lanes vary row by row and the
 *     fold depends on them.
 *   - The SIX region ops (M31's class block, M12's barrel, M32's clz, subnorm
 *     and round) get a REFERENCE RUN: the same block, driven standalone on
 *     operands with the SAME lane kinds, into its own mock sink. Transcribing
 *     M32's 154-row schedule here would be a second port of a module this one
 *     merely calls; running it is a measurement, and it pins the whole
 *     sub-stream rather than a count.
 *
 * WHAT IT CANNOT SEE, RECORDED RATHER THAN PATCHED: which OPERAND a slot
 * reads, when the two have the same lane profile. bd
 * a-slot-scan-cannot-see-which-operand-a-slot-reads has the general statement;
 * here it means rows 26-31's mux arm order is invisible to this case and is
 * L1's job, through the `swap` anchors.
 */

enum { FA_PRED_NONE = -1, FA_REF_MAX = 12000 };

_Static_assert(CQ_OP_X == 0 && CQ_OP_CX == 1 && CQ_OP_CCX == 2,
               "the scan demotes a gate by decrementing its op");

/* PRD §3's fold table, over one gate's controls. */
static int fa_fold(int kind, const int *ctrl, int nc, int *dem)
{
    for (int i = 0; i < nc; i++) {
        if (ctrl[i] == CQ_BIT_ZERO) return FA_PRED_NONE;
        if (ctrl[i] == CQ_BIT_ONE) { kind--; if (dem) (*dem)++; }
    }
    return kind;
}

/* ---- The eleven transcribed schedules. --------------------------------- */

/* `lower_eq!` (arith.jl:424-447): 2n slots of CX building `diff` — the only
 * place an operand lane is a control — then a 3-cycle CX, CX, CCX over the
 * OR-prefix, whose controls are all scratch and so cannot fold. */
static int fa_sched_eq(const cq_fadd_row *rw, int n, int s0, int s1, int v,
                       int *dem)
{
    int W = CQ_FP64_W, c[1];

    if (v < 2 * W) {
        c[0] = fa_lane(rw, n, (v % 2 == 0) ? s0 : s1, v / 2);
        return fa_fold(CQ_OP_CX, c, 1, dem);
    }
    return ((v - 2 * W) % 3 == 2) ? CQ_OP_CCX : CQ_OP_CX;
}

/* `lower_ult!` (arith.jl:449-463): 2W alternating CX/X building `~b`, the
 * carry seed, then a 4-cycle per lane. `nb`, `axnb` and `carry` are scratch. */
static int fa_sched_ult(const cq_fadd_row *rw, int n, int s0, int s1, int v,
                        int *dem)
{
    int W = CQ_FP64_W, c[2], u, i;

    if (v < 2 * W) {
        if (v % 2 == 0) { c[0] = fa_lane(rw, n, s1, v / 2);
                          return fa_fold(CQ_OP_CX, c, 1, dem); }
        return CQ_OP_X;
    }
    if (v == 2 * W) return CQ_OP_X;                    /* carry_in = 1 */

    u = v - (2 * W + 1);
    i = u / 4;
    switch (u % 4) {
    case 0:  c[0] = fa_lane(rw, n, s0, i);
             return fa_fold(CQ_OP_CX, c, 1, dem);
    case 1:  return CQ_OP_CX;                          /* nb[i] -> axnb[i] */
    case 2:  c[0] = fa_lane(rw, n, s0, i); c[1] = CQ_BIT_Q;
             return fa_fold(CQ_OP_CCX, c, 2, dem);
    default: return CQ_OP_CCX;
    }
}

/* `lower_sub!` (adder.jl:149-172): the same 2W complement and seed, then
 * `ripple` with `nb` as the addend — so only the `a` lanes can fold. */
static int fa_sched_sub(const cq_fadd_row *rw, int n, int s0, int s1, int v,
                        int *dem)
{
    int W = CQ_FP64_W, c[2], u, i;

    if (v < 2 * W) {
        if (v % 2 == 0) { c[0] = fa_lane(rw, n, s1, v / 2);
                          return fa_fold(CQ_OP_CX, c, 1, dem); }
        return CQ_OP_X;
    }
    if (v == 2 * W) return CQ_OP_X;                    /* carry_in = 1 */

    u = v - (2 * W + 1);
    if (u < 5 * (W - 1)) {
        i = u / 5;
        switch (u % 5) {
        case 0:  c[0] = fa_lane(rw, n, s0, i);
                 return fa_fold(CQ_OP_CX, c, 1, dem);
        case 1:  return CQ_OP_CX;                      /* nb[i] -> d[i] */
        case 2:  c[0] = fa_lane(rw, n, s0, i); c[1] = CQ_BIT_Q;
                 return fa_fold(CQ_OP_CCX, c, 2, dem);
        case 3:  return CQ_OP_CCX;
        default: return CQ_OP_CX;                      /* c[i] -> d[i]  */
        }
    }
    if (u - 5 * (W - 1) == 0) {
        c[0] = fa_lane(rw, n, s0, W - 1);
        return fa_fold(CQ_OP_CX, c, 1, dem);
    }
    return CQ_OP_CX;
}

/* `lower_add!` (adder.jl:1-18), the same `ripple` with `b` itself as the
 * addend — so BOTH operands' lanes fold, which is what separates this
 * schedule from `sub`'s tail (src/kernels/add.c's `ripple`, read on disk). */
static int fa_sched_add(const cq_fadd_row *rw, int n, int s0, int s1, int v,
                        int *dem)
{
    int W = CQ_FP64_W, c[2], i;

    if (v < 5 * (W - 1)) {
        i = v / 5;
        switch (v % 5) {
        case 0:  c[0] = fa_lane(rw, n, s0, i);
                 return fa_fold(CQ_OP_CX, c, 1, dem);
        case 1:  c[0] = fa_lane(rw, n, s1, i);
                 return fa_fold(CQ_OP_CX, c, 1, dem);
        case 2:  c[0] = fa_lane(rw, n, s0, i); c[1] = fa_lane(rw, n, s1, i);
                 return fa_fold(CQ_OP_CCX, c, 2, dem);
        case 3:  return CQ_OP_CCX;                     /* t[i], c[i] */
        default: return CQ_OP_CX;                      /* c[i] -> t[i] */
        }
    }
    switch (v - 5 * (W - 1)) {
    case 0:  c[0] = fa_lane(rw, n, s0, W - 1);
             return fa_fold(CQ_OP_CX, c, 1, dem);
    case 1:  c[0] = fa_lane(rw, n, s1, W - 1);
             return fa_fold(CQ_OP_CX, c, 1, dem);
    default: return CQ_OP_CX;                          /* c[W-1] -> t[W-1] */
    }
}

/* `lower_mux!` (arith.jl:522-532), in src/kernels/mux.c's order: CX(f -> r),
 * CX(t -> d), CX(f -> d), CCX(cond, d -> r). `cond` and `d` are scratch. */
static int fa_sched_mux(const cq_fadd_row *rw, int n, int s1, int s2, int v,
                        int *dem)
{
    int c[1], i = v / CQ_MUX_STEPS_PER_BIT;

    switch (v % CQ_MUX_STEPS_PER_BIT) {
    case 0:  c[0] = fa_lane(rw, n, s2, i);
             return fa_fold(CQ_OP_CX, c, 1, dem);
    case 1:  c[0] = fa_lane(rw, n, s1, i);
             return fa_fold(CQ_OP_CX, c, 1, dem);
    case 2:  c[0] = fa_lane(rw, n, s2, i);
             return fa_fold(CQ_OP_CX, c, 1, dem);
    default: return CQ_OP_CCX;
    }
}

static int fa_sched(const cq_fadd_row *rw, int n, int i, int v, int *dem)
{
    const cq_fadd_row *r = &rw[i];
    int c[2], lane;

    switch (r->op) {
    case CQ_FAOP_EQ:  return fa_sched_eq (rw, n, r->s0, r->s1, v, dem);
    case CQ_FAOP_ULT: return fa_sched_ult(rw, n, r->s0, r->s1, v, dem);
    case CQ_FAOP_SUB: return fa_sched_sub(rw, n, r->s0, r->s1, v, dem);
    case CQ_FAOP_ADD: return fa_sched_add(rw, n, r->s0, r->s1, v, dem);
    case CQ_FAOP_MUX: return fa_sched_mux(rw, n, r->s1, r->s2, v, dem);
    case CQ_FAOP_AND:                                  /* arith.jl:270 */
        c[0] = fa_lane(rw, n, r->s0, v); c[1] = fa_lane(rw, n, r->s1, v);
        return fa_fold(CQ_OP_CCX, c, 2, dem);
    case CQ_FAOP_OR:                                   /* arith.jl:276-278 */
        lane = v / 3;
        if (v % 3 == 0) { c[0] = fa_lane(rw, n, r->s0, lane);
                          return fa_fold(CQ_OP_CX, c, 1, dem); }
        if (v % 3 == 1) { c[0] = fa_lane(rw, n, r->s1, lane);
                          return fa_fold(CQ_OP_CX, c, 1, dem); }
        c[0] = fa_lane(rw, n, r->s0, lane); c[1] = fa_lane(rw, n, r->s1, lane);
        return fa_fold(CQ_OP_CCX, c, 2, dem);
    case CQ_FAOP_XOR:                                  /* arith.jl:287-288 */
        lane = v / 2;
        c[0] = fa_lane(rw, n, (v % 2 == 0) ? r->s0 : r->s1, lane);
        return fa_fold(CQ_OP_CX, c, 1, dem);
    case CQ_FAOP_NOT1:                                 /* arith.jl:476 */
        return (v == 0) ? CQ_OP_CX : CQ_OP_X;
    case CQ_FAOP_AND1:
        return CQ_OP_CCX;
    default:                                           /* OR1 */
        return (v == 2) ? CQ_OP_CCX : CQ_OP_CX;
    }
}

static int fa_is_ref_row(int op)
{
    return op == CQ_FAOP_CLASS || op == CQ_FAOP_BSHL || op == CQ_FAOP_BLSHR
        || op == CQ_FAOP_CLZ   || op == CQ_FAOP_SUBNORM || op == CQ_FAOP_ROUND;
}

/* ---- The reference run, for the six region ops. ------------------------ */

static const cq_bit *fa_ref_op(cq_ctx *ctx, const cq_fadd_row *rw, int n,
                               int s, cq_bit *out)
{
    for (int i = 0; i < CQ_FP64_W; i++) {
        int k = fa_lane(rw, n, s, i);

        out[i] = (k == CQ_BIT_ONE) ? cq_bit_one() : cq_bit_zero();
        if (k == CQ_BIT_Q) cq_materialise(ctx, &out[i]);
    }
    return out;
}

/* Drive row `i`'s block STANDALONE, on operands with row `i`'s own lane
 * profile, and record the ops it emits. Returns the gate count. */
static size_t fa_ref_stream(const cq_fadd_row *rw, int n, int i, cq_op *out)
{
    cq_mock m;
    cq_sink sk;
    cq_ctx ctx;
    cq_scratch scr;
    cq_bit o0[CQ_FP64_W], o1[CQ_FP64_W], o2[CQ_FP64_W];
    const cq_fadd_row *r = &rw[i];
    size_t got;

    cq_mock_init(&m);
    sk = cq_mock_sink(&m);
    cq_ctx_init(&ctx, &sk);
    cq_scratch_alloc(&scr, fa_row_region(r));
    for (uint32_t b = 0; b < scr.n; b++) cq_materialise(&ctx, &scr.bits[b]);
    (void)fa_ref_op(&ctx, rw, n, r->s0, o0);
    if (cq_fadd_arity(r->op) > 1) (void)fa_ref_op(&ctx, rw, n, r->s1, o1);
    if (cq_fadd_arity(r->op) > 2) (void)fa_ref_op(&ctx, rw, n, r->s2, o2);
    cq_mock_reset(&m);                   /* drop the setup's materialising Xs */

    for (int u = 0; u < fa_row_slots(r); u++) {
        if (r->op == CQ_FAOP_CLASS) {
            cq_fp_class_block c;

            c.a = o0; c.scr = &scr; c.off = 0u; c.cls = (cq_fp_class)r->shift;
            cq_fp_class_step(&ctx, &c, u);
        } else if (r->op == CQ_FAOP_BSHL || r->op == CQ_FAOP_BLSHR) {
            cq_barrel_block b;

            b.a = o0; b.b = o1; b.scr = &scr; b.off = 0u; b.W = CQ_FP64_W;
            b.dir = (r->op == CQ_FAOP_BSHL) ? CQ_BARREL_SHL : CQ_BARREL_LSHR;
            cq_barrel_step(&ctx, &b, u);
        } else if (r->op == CQ_FAOP_CLZ) {
            cq_clz_block b;

            b.wr = o0; b.result_exp = o1; b.scr = &scr; b.off = 0u;
            cq_clz_step(&ctx, &b, u);
        } else if (r->op == CQ_FAOP_SUBNORM) {
            cq_subnorm_block b;

            b.wr = o0; b.result_exp = o1; b.result_sign = o2;
            b.scr = &scr; b.off = 0u;
            cq_subnorm_step(&ctx, &b, u);
        } else {
            cq_round_block b;

            b.wr = o0; b.result_exp = o1; b.result_sign = o2;
            b.scr = &scr; b.off = 0u;
            cq_round_step(&ctx, &b, u);
        }
    }

    got = cq_mock_count(&m);
    CHECK(got <= (size_t)FA_REF_MAX);
    for (size_t g = 0; g < got && g < (size_t)FA_REF_MAX; g++)
        out[g] = cq_mock_at(&m, g)->op;

    /* The reverse half, so the region comes back to |0> and the probe leaves
     * no dirty ancilla of its own — Rule 2 inside the instrument. */
    for (int u = fa_row_slots(r) - 1; u >= 0; u--) {
        if (r->op == CQ_FAOP_CLASS) {
            cq_fp_class_block c;

            c.a = o0; c.scr = &scr; c.off = 0u; c.cls = (cq_fp_class)r->shift;
            cq_fp_class_step(&ctx, &c, u);
        } else if (r->op == CQ_FAOP_BSHL || r->op == CQ_FAOP_BLSHR) {
            cq_barrel_block b;

            b.a = o0; b.b = o1; b.scr = &scr; b.off = 0u; b.W = CQ_FP64_W;
            b.dir = (r->op == CQ_FAOP_BSHL) ? CQ_BARREL_SHL : CQ_BARREL_LSHR;
            cq_barrel_step(&ctx, &b, u);
        } else if (r->op == CQ_FAOP_CLZ) {
            cq_clz_block b;

            b.wr = o0; b.result_exp = o1; b.scr = &scr; b.off = 0u;
            cq_clz_step(&ctx, &b, u);
        } else if (r->op == CQ_FAOP_SUBNORM) {
            cq_subnorm_block b;

            b.wr = o0; b.result_exp = o1; b.result_sign = o2;
            b.scr = &scr; b.off = 0u;
            cq_subnorm_step(&ctx, &b, u);
        } else {
            cq_round_block b;

            b.wr = o0; b.result_exp = o1; b.result_sign = o2;
            b.scr = &scr; b.off = 0u;
            cq_round_step(&ctx, &b, u);
        }
    }
    for (uint32_t b = 0; b < scr.n; b++) scr.bits[b] = cq_bit_zero();
    cq_scratch_dispose(&scr);
    cq_ctx_dispose(&ctx);
    cq_mock_dispose(&m);
    return got;
}

/* How many GATES row `i` emits — the reference run's count for a region op,
 * the non-NONE slots for a scheduled one. Shared with the span walk next door,
 * which needs the same partition of the stream by row. */
static size_t fa_row_gates(const cq_fadd_row *rw, int n, int i, cq_op *ref)
{
    size_t g = 0u;
    int dummy = 0;

    if (fa_is_ref_row(rw[i].op)) return fa_ref_stream(rw, n, i, ref);
    for (int v = 0; v < fa_row_slots(&rw[i]); v++)
        if (fa_sched(rw, n, i, v, &dummy) != FA_PRED_NONE) g++;
    return g;
}

/* ---- The scan. --------------------------------------------------------- */

/* A payload NaN in one operand and an ordinary tie in the other would take the
 * NaN branch and never reach the arithmetic, so the scan runs on a pair that
 * reaches EVERY phase: a normal addition with a non-trivial alignment, a
 * sticky bit, a rounding decision and a live select chain. The KINDS do not
 * depend on the values (D6: the fold table reads a bit's kind, never a qubit's
 * value) — the value is checked on the way through so that a scan pinning the
 * shape of a wrong circuit cannot pass. */
#define FA_SCAN_A  CQ_F64_ONE
#define FA_SCAN_B  FA_STICKY

CQ_TEST(the_slot_boundaries_match_an_independent_four_valued_scan)
{
    static cq_op ref[FA_REF_MAX];
    cq_mock m;
    int any_demoted = 0, any_folded = 0;

    cq_mock_init(&m);

    for (int p = 0; p < N_FADD_ROWS; p++) {
        cq_fadd_row rows[CQ_FADD_MAX_ROWS];
        int n = cq_fadd_program(ROWS[p].prog, rows);
        cq_ref_w q = cq_ref_w_ones(CQ_FP64_W);
        size_t cursor = 0u, total;
        int folded = 0, demoted = 0, bad = 0;
        uint64_t want = ROWS[p].host(FA_SCAN_A, FA_SCAN_B);

        CHECK_EQ(fadd_run(&ROWS[p], FA_SCAN_A, FA_SCAN_B, q, q, &m), want);

        for (int i = 0; i < n && !bad; i++) {
            if (fa_is_ref_row(rows[i].op)) {
                size_t g = fa_ref_stream(rows, n, i, ref);

                for (size_t k = 0; k < g && !bad; k++) {
                    if (cursor >= cq_mock_count(&m)
                        || (int)cq_mock_at(&m, cursor)->op != (int)ref[k]) {
                        cq_h_fail(__FILE__, __LINE__,
                                  "%s row %d (op %d) gate %zu (stream %zu): the "
                                  "standalone reference says op %d",
                                  ROWS[p].spec.name, i, (int)rows[i].op, k,
                                  cursor, (int)ref[k]);
                        bad = 1;
                        break;
                    }
                    cursor++;
                }
                folded += fa_row_slots(&rows[i]) - (int)g;
                continue;
            }
            for (int v = 0; v < fa_row_slots(&rows[i]) && !bad; v++) {
                int pr = fa_sched(rows, n, i, v, &demoted);
                cq_op got;

                if (pr == FA_PRED_NONE) { folded++; continue; }
                if (cursor >= cq_mock_count(&m)) {
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d slot %d: the stream ran out at %zu "
                              "gates", ROWS[p].spec.name, i, v, cursor);
                    bad = 1;
                    break;
                }
                got = cq_mock_at(&m, cursor++)->op;
                if ((int)got != pr) {
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d (op %d) slot %d (stream %zu): emitted "
                              "op %d, the slot decode says %d",
                              ROWS[p].spec.name, i, (int)rows[i].op, v,
                              cursor - 1u, (int)got, pr);
                    bad = 1;
                }
            }
        }
        if (bad) continue;

        /* THE SCAN IS NOT VACUOUS. `folded` is the ZERO half and `demoted` the
         * ONE half, and both are reached by both programs: every view row's
         * vacated lanes are CQ_BIT_ZERO and every constant span but `K_ZERO`
         * carries at least one set lane. */
        CHECK(folded > 0);
        CHECK(demoted > 0);
        any_folded += folded;
        any_demoted += demoted;

        /* The copy-out is exactly 64 CNOTs — the "^=" of Rule 7's contract —
         * and the reverse half is the same `cursor` gates again. */
        total = cq_mock_count(&m);
        CHECK_EQ((long long)total, (long long)(2u * cursor + CQ_FP64_W));
        for (int k = 0; k < CQ_FP64_W; k++)
            CHECK_EQ((int)cq_mock_at(&m, cursor + (size_t)k)->op, (int)CQ_OP_CX);
        CHECK(cq_mock_is_palindrome(&m, cursor, (size_t)CQ_FP64_W));
    }

    CHECK(any_folded > 0);
    CHECK(any_demoted > 0);
    cq_mock_dispose(&m);
}
