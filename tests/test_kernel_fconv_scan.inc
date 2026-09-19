/* tests/test_kernel_fconv_scan.inc — THE FOUR-VALUED SLOT SCAN. Included by
 * test_kernel_fconv.c AFTER test_kernel_fconv_slots.inc, whose `fv_lane`,
 * `fv_row_slots` and `fv_probe` it reuses. NOTHING HERE READS A GOLDEN.
 *
 * WHAT A K12-SHAPED KERNEL GETS WRONG IS THE SLOT ARITHMETIC, NOT THE GATES
 * (CLAUDE.md's callout, PRD-v2 §7.1), and K19 is K12's shape with 74 row
 * boundaries where K12 has four — and, uniquely in the port so far, with one
 * table INLINED into another twice. The instrument re-derives the op KIND of
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
 * constant ONE demotes it. Both happen at EVERY mask, because they come from
 * §7.3's constant spans, from the view rows, and — in `uitofp` — from the ZEXT
 * VIEW's constant fill. A scan that kept K12's 1:1 assumption goes red on
 * fptosi's row 5 and stays red, and the reflex fix — deleting the constant
 * rows from the prediction — is what turns the instrument off.
 *
 * TWO SOURCES OF PREDICTION, AND THE SPLIT IS FORCED RATHER THAN CHOSEN.
 *   - The ELEVEN carved-span ops (eq, ult, sub, add, mux and the six bitwise
 *     ones) get a SCHEDULE transcribed from the pinned lowering source, as
 *     M33's and M36's scans do, because their operand lanes vary row by row
 *     and the fold depends on them.
 *   - The THREE region ops (M12's two barrel directions and M33's whole
 *     `fsub`) get a REFERENCE RUN: the same block, driven standalone on
 *     operands with the SAME lane kinds, into its own mock sink. Transcribing
 *     M33's 133-row schedule here would be a second port of a module this one
 *     merely calls; running it is a measurement, and it pins the whole
 *     sub-stream rather than a count.
 *
 * WHAT IT CANNOT SEE, RECORDED RATHER THAN PATCHED: which OPERAND a slot
 * reads, when the two have the same lane profile. bd
 * a-slot-scan-cannot-see-which-operand-a-slot-reads has the general statement;
 * here it means fptosi's three mux arm orders — rows 23, 28 and 30 — are
 * invisible to this case and are L1's job, through the saturation anchors.
 */

enum { FV_PRED_NONE = -1, FV_REF_MAX = 100000 };

_Static_assert(CQ_OP_X == 0 && CQ_OP_CX == 1 && CQ_OP_CCX == 2,
               "the scan demotes a gate by decrementing its op");

/* PRD §3's fold table, over one gate's controls. */
static int fv_fold(int kind, const int *ctrl, int nc, int *dem)
{
    for (int i = 0; i < nc; i++) {
        if (ctrl[i] == CQ_BIT_ZERO) return FV_PRED_NONE;
        if (ctrl[i] == CQ_BIT_ONE) { kind--; if (dem) (*dem)++; }
    }
    return kind;
}

/* ---- The eleven transcribed schedules. --------------------------------- */

/* One frame of what a schedule needs: the table, its length and the source
 * width, so `fv_lane` can tell the zext view's constant fill from the rail. */
typedef struct { const cq_fconv_row *rw; int n; int a_w; } fv_sc;

/* `lower_eq!` (arith.jl:424-447): 2n slots of CX building `diff` — the only
 * place an operand lane is a control — then a 3-cycle CX, CX, CCX over the
 * OR-prefix, whose controls are all scratch and so cannot fold. */
static int fv_sched_eq(const fv_sc *c, int s0, int s1, int v, int *dem)
{
    int W = CQ_FP64_W, ct[1];

    if (v < 2 * W) {
        ct[0] = fv_lane(c->rw, c->n, c->a_w, (v % 2 == 0) ? s0 : s1, v / 2);
        return fv_fold(CQ_OP_CX, ct, 1, dem);
    }
    return ((v - 2 * W) % 3 == 2) ? CQ_OP_CCX : CQ_OP_CX;
}

/* `lower_ult!` (arith.jl:449-463): 2W alternating CX/X building `~b`, the
 * carry seed, then a 4-cycle per lane. `nb`, `axnb` and `carry` are scratch. */
static int fv_sched_ult(const fv_sc *c, int s0, int s1, int v, int *dem)
{
    int W = CQ_FP64_W, ct[2], u, i;

    if (v < 2 * W) {
        if (v % 2 == 0) { ct[0] = fv_lane(c->rw, c->n, c->a_w, s1, v / 2);
                          return fv_fold(CQ_OP_CX, ct, 1, dem); }
        return CQ_OP_X;
    }
    if (v == 2 * W) return CQ_OP_X;                    /* carry_in = 1 */

    u = v - (2 * W + 1);
    i = u / 4;
    switch (u % 4) {
    case 0:  ct[0] = fv_lane(c->rw, c->n, c->a_w, s0, i);
             return fv_fold(CQ_OP_CX, ct, 1, dem);
    case 1:  return CQ_OP_CX;                          /* nb[i] -> axnb[i] */
    case 2:  ct[0] = fv_lane(c->rw, c->n, c->a_w, s0, i); ct[1] = CQ_BIT_Q;
             return fv_fold(CQ_OP_CCX, ct, 2, dem);
    default: return CQ_OP_CCX;
    }
}

/* `lower_sub!` (adder.jl:149-172): the same 2W complement and seed, then
 * `ripple` with `nb` as the addend — so only the `a` lanes can fold. */
static int fv_sched_sub(const fv_sc *c, int s0, int s1, int v, int *dem)
{
    int W = CQ_FP64_W, ct[2], u, i;

    if (v < 2 * W) {
        if (v % 2 == 0) { ct[0] = fv_lane(c->rw, c->n, c->a_w, s1, v / 2);
                          return fv_fold(CQ_OP_CX, ct, 1, dem); }
        return CQ_OP_X;
    }
    if (v == 2 * W) return CQ_OP_X;                    /* carry_in = 1 */

    u = v - (2 * W + 1);
    if (u < 5 * (W - 1)) {
        i = u / 5;
        switch (u % 5) {
        case 0:  ct[0] = fv_lane(c->rw, c->n, c->a_w, s0, i);
                 return fv_fold(CQ_OP_CX, ct, 1, dem);
        case 1:  return CQ_OP_CX;                      /* nb[i] -> d[i] */
        case 2:  ct[0] = fv_lane(c->rw, c->n, c->a_w, s0, i); ct[1] = CQ_BIT_Q;
                 return fv_fold(CQ_OP_CCX, ct, 2, dem);
        case 3:  return CQ_OP_CCX;
        default: return CQ_OP_CX;                      /* c[i] -> d[i]  */
        }
    }
    if (u - 5 * (W - 1) == 0) {
        ct[0] = fv_lane(c->rw, c->n, c->a_w, s0, W - 1);
        return fv_fold(CQ_OP_CX, ct, 1, dem);
    }
    return CQ_OP_CX;
}

/* `lower_add!` (adder.jl:1-18), the same `ripple` with `b` itself as the
 * addend — so BOTH operands' lanes fold, which is what separates this
 * schedule from `sub`'s tail. TEN OF K19's ROWS TAKE IT and none takes K8's:
 * an in-place accumulate would be right forward and wrong in `_unc`, and it
 * would also destroy the pre-add value each CLZ rung's mux reads. */
static int fv_sched_add(const fv_sc *c, int s0, int s1, int v, int *dem)
{
    int W = CQ_FP64_W, ct[2], i;

    if (v < 5 * (W - 1)) {
        i = v / 5;
        switch (v % 5) {
        case 0:  ct[0] = fv_lane(c->rw, c->n, c->a_w, s0, i);
                 return fv_fold(CQ_OP_CX, ct, 1, dem);
        case 1:  ct[0] = fv_lane(c->rw, c->n, c->a_w, s1, i);
                 return fv_fold(CQ_OP_CX, ct, 1, dem);
        case 2:  ct[0] = fv_lane(c->rw, c->n, c->a_w, s0, i);
                 ct[1] = fv_lane(c->rw, c->n, c->a_w, s1, i);
                 return fv_fold(CQ_OP_CCX, ct, 2, dem);
        case 3:  return CQ_OP_CCX;                     /* t[i], c[i] */
        default: return CQ_OP_CX;                      /* c[i] -> t[i] */
        }
    }
    switch (v - 5 * (W - 1)) {
    case 0:  ct[0] = fv_lane(c->rw, c->n, c->a_w, s0, W - 1);
             return fv_fold(CQ_OP_CX, ct, 1, dem);
    case 1:  ct[0] = fv_lane(c->rw, c->n, c->a_w, s1, W - 1);
             return fv_fold(CQ_OP_CX, ct, 1, dem);
    default: return CQ_OP_CX;                          /* c[W-1] -> t[W-1] */
    }
}

/* `lower_mux!` (arith.jl:522-532), in src/kernels/mux.c's order: CX(f -> r),
 * CX(t -> d), CX(f -> d), CCX(cond, d -> r). `cond` and `d` are scratch. */
static int fv_sched_mux(const fv_sc *c, int s1, int s2, int v, int *dem)
{
    int ct[1], i = v / CQ_MUX_STEPS_PER_BIT;

    switch (v % CQ_MUX_STEPS_PER_BIT) {
    case 0:  ct[0] = fv_lane(c->rw, c->n, c->a_w, s2, i);
             return fv_fold(CQ_OP_CX, ct, 1, dem);
    case 1:  ct[0] = fv_lane(c->rw, c->n, c->a_w, s1, i);
             return fv_fold(CQ_OP_CX, ct, 1, dem);
    case 2:  ct[0] = fv_lane(c->rw, c->n, c->a_w, s2, i);
             return fv_fold(CQ_OP_CX, ct, 1, dem);
    default: return CQ_OP_CCX;
    }
}

static int fv_sched(const fv_sc *c, int i, int v, int *dem)
{
    const cq_fconv_row *r = &c->rw[i];
    int ct[2], lane;

    switch (r->op) {
    case CQ_FVOP_EQ:  return fv_sched_eq (c, r->s0, r->s1, v, dem);
    case CQ_FVOP_ULT: return fv_sched_ult(c, r->s0, r->s1, v, dem);
    case CQ_FVOP_SUB: return fv_sched_sub(c, r->s0, r->s1, v, dem);
    case CQ_FVOP_ADD: return fv_sched_add(c, r->s0, r->s1, v, dem);
    case CQ_FVOP_MUX: return fv_sched_mux(c, r->s1, r->s2, v, dem);
    case CQ_FVOP_AND:                                  /* arith.jl:270 */
        ct[0] = fv_lane(c->rw, c->n, c->a_w, r->s0, v);
        ct[1] = fv_lane(c->rw, c->n, c->a_w, r->s1, v);
        return fv_fold(CQ_OP_CCX, ct, 2, dem);
    case CQ_FVOP_OR:                                   /* arith.jl:276-278 */
        lane = v / 3;
        if (v % 3 == 0) { ct[0] = fv_lane(c->rw, c->n, c->a_w, r->s0, lane);
                          return fv_fold(CQ_OP_CX, ct, 1, dem); }
        if (v % 3 == 1) { ct[0] = fv_lane(c->rw, c->n, c->a_w, r->s1, lane);
                          return fv_fold(CQ_OP_CX, ct, 1, dem); }
        ct[0] = fv_lane(c->rw, c->n, c->a_w, r->s0, lane);
        ct[1] = fv_lane(c->rw, c->n, c->a_w, r->s1, lane);
        return fv_fold(CQ_OP_CCX, ct, 2, dem);
    case CQ_FVOP_XOR:                                  /* arith.jl:287-288 */
        lane = v / 2;
        ct[0] = fv_lane(c->rw, c->n, c->a_w,
                        (v % 2 == 0) ? r->s0 : r->s1, lane);
        return fv_fold(CQ_OP_CX, ct, 1, dem);
    case CQ_FVOP_NOT1:                                 /* arith.jl:476 */
        return (v == 0) ? CQ_OP_CX : CQ_OP_X;
    default:                                           /* AND1 */
        return CQ_OP_CCX;
    }
}

static int fv_is_ref_row(int op)
{
    return op == CQ_FVOP_BSHL || op == CQ_FVOP_BLSHR || op == CQ_FVOP_FSUB;
}

/* ---- The reference run, for the three region ops. ---------------------- */

static const cq_bit *fv_ref_op(cq_ctx *ctx, const fv_sc *c, int s, cq_bit *out)
{
    for (int i = 0; i < CQ_FP64_W; i++) {
        int k = fv_lane(c->rw, c->n, c->a_w, s, i);

        out[i] = (k == CQ_BIT_ONE) ? cq_bit_one() : cq_bit_zero();
        if (k == CQ_BIT_Q) cq_materialise(ctx, &out[i]);
    }
    return out;
}

static void fv_ref_drive(cq_ctx *ctx, const cq_fconv_row *r, cq_scratch *scr,
                         const cq_bit *o0, const cq_bit *o1, int u)
{
    if (r->op == CQ_FVOP_FSUB) {
        cq_fsub_block b;

        b.a = o0; b.b = o1; b.scr = scr; b.off = 0u;
        cq_fsub_step(ctx, &b, u);
    } else {
        cq_barrel_block b;

        b.a = o0; b.b = o1; b.scr = scr; b.off = 0u; b.W = CQ_FP64_W;
        b.dir = (r->op == CQ_FVOP_BSHL) ? CQ_BARREL_SHL : CQ_BARREL_LSHR;
        cq_barrel_step(ctx, &b, u);
    }
}

/* Drive row `i`'s block STANDALONE, on operands with row `i`'s own lane
 * profile, and record the ops it emits. Returns the gate count. */
static size_t fv_ref_stream(const fv_sc *c, int i, cq_op *out)
{
    cq_mock m;
    cq_sink sk;
    cq_ctx ctx;
    cq_scratch scr;
    cq_bit o0[CQ_FP64_W], o1[CQ_FP64_W];
    const cq_fconv_row *r = &c->rw[i];
    int n = fv_row_slots(r);
    size_t got;

    cq_mock_init(&m);
    sk = cq_mock_sink(&m);
    cq_ctx_init(&ctx, &sk);
    cq_scratch_alloc(&scr, fv_row_region(r));
    for (uint32_t b = 0; b < scr.n; b++) cq_materialise(&ctx, &scr.bits[b]);
    (void)fv_ref_op(&ctx, c, r->s0, o0);
    (void)fv_ref_op(&ctx, c, r->s1, o1);
    cq_mock_reset(&m);                   /* drop the setup's materialising Xs */

    for (int u = 0; u < n; u++) fv_ref_drive(&ctx, r, &scr, o0, o1, u);

    got = cq_mock_count(&m);
    CHECK(got <= (size_t)FV_REF_MAX);
    for (size_t g = 0; g < got && g < (size_t)FV_REF_MAX; g++)
        out[g] = cq_mock_at(&m, g)->op;

    /* The reverse half, so the region comes back to |0> and the probe leaves
     * no dirty ancilla of its own — Rule 2 inside the instrument. */
    for (int u = n - 1; u >= 0; u--) fv_ref_drive(&ctx, r, &scr, o0, o1, u);
    for (uint32_t b = 0; b < scr.n; b++) scr.bits[b] = cq_bit_zero();
    cq_scratch_dispose(&scr);
    cq_ctx_dispose(&ctx);
    cq_mock_dispose(&m);
    return got;
}

/* How many GATES row `i` emits — the reference run's count for a region op,
 * the non-NONE slots for a scheduled one. Shared with the span walk next door,
 * which needs the same partition of the stream by row. */
static size_t fv_row_gates(const fv_sc *c, int i, cq_op *ref)
{
    size_t g = 0u;
    int dummy = 0;

    if (fv_is_ref_row(c->rw[i].op)) return fv_ref_stream(c, i, ref);
    for (int v = 0; v < fv_row_slots(&c->rw[i]); v++)
        if (fv_sched(c, i, v, &dummy) != FV_PRED_NONE) g++;
    return g;
}

/* ---- The scan. --------------------------------------------------------- */

/* THE KINDS DO NOT DEPEND ON THE VALUES (D6: the fold table reads a bit's
 * kind, never a qubit's value), so any operand reaching the sandwich will do —
 * but the value is checked on the way through so that a scan pinning the shape
 * of a WRONG circuit cannot pass. The operand chosen per kernel reaches every
 * phase: an ordinary in-range double for the fp-source pair, and a magnitude
 * with a live CLZ rung and a live rounding decision for the int-source pair.
 *
 * `uitofp` IS SCANNED AT A NARROW WIDTH ON PURPOSE. That is the only place the
 * zext view's constant fill enters a block operand, and a lane model that read
 * the rail as 64 quantum lanes would over-predict by exactly the fold — which
 * presents as a CURSOR SLIP, so the red arrives one row after the cause
 * (bd a-picked-output-can-itself-be-a-view's rider). */
static const uint64_t FV_SCAN_V[] = {
    UINT64_C(0x400921FB54442D18),   /* fptosi: pi, an ordinary right shift */
    UINT64_C(0x43E158E460913D00),   /* fptoui: 1e19, the biased half       */
    UINT64_C(0x0020000000000003),   /* sitofp: 2^53+3, a live tie          */
    UINT64_C(0x00000000000000A5)    /* uitofp: an 8-bit magnitude          */
};

CQ_TEST(the_slot_boundaries_match_an_independent_four_valued_scan)
{
    static cq_op ref[FV_REF_MAX];
    cq_mock m;
    int any_demoted = 0, any_folded = 0;

    cq_mock_init(&m);

    for (int p = 0; p < N_FCONV_ROWS; p++) {
        cq_fconv_row rows[CQ_FCONV_MAX_ROWS];
        int F = (p == FV_UITOFP) ? 8 : CQ_FP64_W;
        int n = cq_fconv_program(ROWS[p].prog, rows);
        fv_sc c = { rows, n, F };
        cq_ref_w q = cq_ref_w_ones(F);
        size_t cursor = 0u, total;
        int folded = 0, demoted = 0, bad = 0;
        uint64_t want = ROWS[p].host(FV_SCAN_V[p], F);

        CHECK_EQ(fconv_run(&ROWS[p], F, FV_SCAN_V[p], q, &m), want);

        for (int i = 0; i < n && !bad; i++) {
            if (fv_is_ref_row(rows[i].op)) {
                size_t g = fv_ref_stream(&c, i, ref);

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
                folded += fv_row_slots(&rows[i]) - (int)g;
                continue;
            }
            for (int v = 0; v < fv_row_slots(&rows[i]) && !bad; v++) {
                int pr = fv_sched(&c, i, v, &demoted);
                cq_op got;

                if (pr == FV_PRED_NONE) { folded++; continue; }
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
         * ONE half, and both are reached by every program: every view row's
         * vacated lanes are CQ_BIT_ZERO and every constant span but `K_0`
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
