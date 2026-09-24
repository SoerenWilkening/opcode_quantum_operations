/* tests/test_kernel_ibits_slots.inc — the COMPOSITION identity and the
 * four-valued SLOT SCAN. Included by test_kernel_ibits.c. NOTHING BELOW READS
 * A GOLDEN, and all of it was written before ibits_step.c / ibits_kernel.c.
 *
 * K11's RECORDED FINDING, APPLIED IN ADVANCE: the mutant L1 cannot see is the
 * one that looks like an optimisation, and K26 offers several — run the `ne`
 * of a one-lane `bit` at width 1, drop the closing `add 0` as "a rename",
 * skip ctlz's all-constant seed row, build bitreverse's `or` of two disjoint
 * one-hot views as a CNOT pair. None changes a value, a palindrome or the
 * pool. What catches them is the identity below, whose block COUNTS are read
 * off the Julia a second time and whose block COSTS are asked of each module.
 *
 * AND IT IS OVER SLOTS, NOT GATES (bd memories fp-kernels-slots-and-gates-
 * come-apart-at-the-all-quantum-mask): views and constant sources put
 * classical lanes inside block operands at EVERY mask, so gates fold even at
 * the all-quantum one.
 */

/* ---- (a1) The block costs, ASKED of their modules. Runs no kernel. ------ */

enum { IB_OR_SLOTS = 3 };   /* lower_or!: CNOT, CNOT, Toffoli per lane (arith.jl:276-278) */

CQ_TEST(the_blocks_cost_what_their_modules_say_they_cost)
{
    static const int WS[] = { 1, 2, 8, 64, 128 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        cq_ibits_row r;

        /* Each module's own published closed form (add.h, cmp.h, mux.h). */
        CHECK_EQ(cq_add_steps(W), 5 * W - 2);
        CHECK_EQ(cq_eq_steps(W),  5 * W - 3);
        CHECK_EQ(cq_mux_steps(W), 4 * W);
        CHECK_EQ(cq_add_region(W), 2 * W);
        CHECK_EQ(cq_eq_region(W),  2 * W - 1);
        CHECK_EQ(cq_mux_region(W), 2 * W);

        /* And the step machine's per-row costs ARE those, not a copy. */
        r.op = CQ_IBOP_ADD; CHECK_EQ(cq_ibits_row_steps(&r, W), cq_add_steps(W));
        CHECK_EQ(cq_ibits_row_region(&r, W), (uint32_t)cq_add_region(W));
        r.op = CQ_IBOP_NE;  CHECK_EQ(cq_ibits_row_steps(&r, W), cq_eq_steps(W));
        CHECK_EQ(cq_ibits_row_region(&r, W), (uint32_t)cq_eq_region(W));
        r.op = CQ_IBOP_MUX; CHECK_EQ(cq_ibits_row_steps(&r, W), cq_mux_steps(W));
        CHECK_EQ(cq_ibits_row_region(&r, W), (uint32_t)cq_mux_region(W));
        r.op = CQ_IBOP_OR;  CHECK_EQ(cq_ibits_row_steps(&r, W), IB_OR_SLOTS * W);
        CHECK_EQ(cq_ibits_row_region(&r, W), (uint32_t)W);
    }
}

/* ---- (a2) Each program against what those blocks just cost. ------------ */

/* THE FIVE EXPANSIONS, COUNTED A SECOND TIME straight off the pinned Julia
 * rather than off ibits.c. Only IRBinOp add/or, IRICmp and IRSelect are
 * blocks; every `lshr`/`shl`/`and` by a constant is a view (PRD-v2 §7.3).
 *
 *   ctpop       :4836 loop W-1 `add`, + :4846 `add 0`             W add
 *   ctlz/cttz   :4856 seed `add`, W x (:4864 ne, :4865 select),
 *               + :4868 `add 0`                                  2 add, W ne, W mux
 *   bitreverse  :4908 loop W-1 `or`, + :4919 `add 0`             1 add, W-1 or
 *   bswap       :4933 loop W/8-1 `or`, + :4944 `add 0`           1 add, W/8-1 or */
typedef struct { int add, ne, mux, orr; } ib_blocks;

static ib_blocks ib_blocks_of(cq_ibits_fn f, int W)
{
    ib_blocks b = { 0, 0, 0, 0 };

    switch (f) {
    case CQ_IBF_CTPOP:      b.add = W;                       break;
    case CQ_IBF_CTLZ:
    case CQ_IBF_CTTZ:       b.add = 2; b.ne = W; b.mux = W;  break;
    case CQ_IBF_BITREVERSE: b.add = 1; b.orr = W - 1;        break;
    case CQ_IBF_BSWAP:      b.add = 1; b.orr = W / 8 - 1;    break;
    case CQ_IBF_N:          break;
    }
    return b;
}

CQ_TEST(each_program_is_the_sum_of_its_blocks)
{
    for (int f = 0; f < CQ_IBF_N; f++) {
        const int *ws;
        int nw = ladder((cq_ibits_fn)f, &ws);

        for (int i = 0; i < nw; i++) {
            int W = ws[i];
            ib_blocks b = ib_blocks_of((cq_ibits_fn)f, W);
            cq_ibits_row rows[CQ_IBITS_MAX_ROWS];
            int n = cq_ibits_program((cq_ibits_fn)f, W, rows);
            int slots = b.add * cq_add_steps(W) + b.ne * cq_eq_steps(W)
                      + b.mux * cq_mux_steps(W) + b.orr * IB_OR_SLOTS * W;
            uint32_t region = (uint32_t)(b.add * cq_add_region(W)
                            + b.ne * cq_eq_region(W) + b.mux * cq_mux_region(W)
                            + b.orr * W);
            cq_counter fwd, unc;

            CHECK_EQ(n, b.add + b.ne + b.mux + b.orr);
            CHECK_EQ(cq_ibits_steps(rows, n, W), slots);
            CHECK_EQ(cq_ibits_region(rows, n, W), region);

            /* SLOTS AND GATES COME APART, asserted: the sandwiched forward is
             * strictly below 2 x slots + the W-CNOT copy-out, because views'
             * ZERO lanes and constant sources fold at the all-quantum mask. */
            cq_kd_measure(&SPEC[f], W, &fwd, &unc);
            CHECK(cq_count_total(&fwd) < 2u * (uint64_t)slots + (uint64_t)W);

            printf("# %-10s W=%2d rows %3d slots %6d  fwd (%llu, %llu, %llu)  "
                   "unc (%llu, %llu, %llu)  region %u\n", SPEC[f].name, W, n,
                   slots, (unsigned long long)fwd.x, (unsigned long long)fwd.cx,
                   (unsigned long long)fwd.ccx, (unsigned long long)unc.x,
                   (unsigned long long)unc.cx, (unsigned long long)unc.ccx,
                   region);
        }
    }
    fflush(stdout);
}

/* ---- (a3) Well-formedness, at EVERY width the builder accepts. ---------- */

/* A REACHABILITY PROOF for the guards no caller can reach through the shipped
 * tables — `a program outgrew CQ_IBITS_MAX_ROWS` and the step machine's
 * `not an earlier row` / `not an earlier ne row` / `last row is not its
 * closing add` on a real program. ctlz at the maximum width must hit the
 * bound EXACTLY, so the bound is neither loose nor exceeded. */
CQ_TEST(the_program_tables_are_well_formed_at_every_width)
{
    int max_seen = 0;

    for (int f = 0; f < CQ_IBF_N; f++)
        for (int W = 1; W <= (int)CQ_REG_WIDTH_MAX; W++) {
            cq_ibits_row rows[CQ_IBITS_MAX_ROWS];
            int n;

            if (f == CQ_IBF_BSWAP && W % 16 != 0) continue;
            n = cq_ibits_program((cq_ibits_fn)f, W, rows);
            if (n > max_seen) max_seen = n;
            CHECK(n >= 1 && n <= CQ_IBITS_MAX_ROWS);
            CHECK_EQ(rows[n - 1].op, CQ_IBOP_ADD);

            for (int r = 0; r < n; r++)
                for (int s = 0; s < 3; s++) {
                    const cq_ibits_opnd *o = &rows[r].s[s];

                    if (s == 2 && rows[r].op != CQ_IBOP_MUX) continue;
                    CHECK(o->src < r);
                    CHECK(o->src >= CQ_IBS_K);
                    CHECK(o->shr >= 0 && o->shr <= W && o->shl >= 0
                          && o->shl <= W && o->keep >= 0 && o->keep <= W);
                    if (rows[r].op == CQ_IBOP_MUX && s == 0)
                        CHECK(o->src >= 0 && rows[o->src].op == CQ_IBOP_NE);
                    else if (o->src >= 0)
                        CHECK(rows[o->src].op != CQ_IBOP_NE);
                }
        }
    CHECK_EQ(max_seen, CQ_IBITS_MAX_ROWS);
}

/* ---- (b) The four-valued slot scan, with its own stream cursor. -------- */

enum { IB_PRED_NONE = -1 };

_Static_assert(CQ_OP_X == 0 && CQ_OP_CX == 1 && CQ_OP_CCX == 2,
               "the scan demotes a gate by decrementing its op");

static int onehot(int j, int at) { return j == at ? CQ_BIT_Q : CQ_BIT_ZERO; }

static int kbit(int v, int j)
{
    return (j < 15 && ((v >> j) & 1)) ? CQ_BIT_ONE : CQ_BIT_ZERO;
}

/* Operand `s` of row `r`, lane `j`, at an ALL-QUANTUM rail — re-derived from
 * the Julia per expansion and NOT from the row descriptors, so a view with a
 * wrong shift or keep is a disagreement here rather than a shared belief.
 * A row's own output is scratch, CQ_BIT_Q from step 0 under I6(b). */
static int sc_lane(cq_ibits_fn f, int W, int r, int s, int j)
{
    int nb = W / 8, it, i;

    switch (f) {
    case CQ_IBF_CTPOP:                         /* acc_r = prev + bit_{r+1} */
        if (s == 1) return (r == W - 1) ? CQ_BIT_ZERO : onehot(j, 0);
        return (r == 0) ? onehot(j, 0) : CQ_BIT_Q;
    case CQ_IBF_CTLZ:
    case CQ_IBF_CTTZ:
        if (r == 0)         return s == 0 ? kbit(W, j) : CQ_BIT_ZERO;
        if (r == 2 * W + 1) return s == 0 ? CQ_BIT_Q : CQ_BIT_ZERO;
        it = (r - 1) / 2;
        i = (f == CQ_IBF_CTLZ) ? it : W - 1 - it;
        if ((r - 1) % 2 == 0) return s == 0 ? onehot(j, 0) : CQ_BIT_ZERO;
        if (s == 1) return kbit(f == CQ_IBF_CTLZ ? W - 1 - i : i, j);
        return CQ_BIT_Q;                       /* the cond, and `prev` */
    case CQ_IBF_BITREVERSE:
        if (r == W - 1) return s == 0 ? (W == 1 ? onehot(j, 0) : CQ_BIT_Q)
                                      : CQ_BIT_ZERO;
        if (s == 1) return onehot(j, W - 2 - r);
        return (r == 0) ? onehot(j, W - 1) : CQ_BIT_Q;
    case CQ_IBF_BSWAP:
        if (r == nb - 1) return s == 0 ? CQ_BIT_Q : CQ_BIT_ZERO;
        if (s == 1) {
            int lo = (nb - 2 - r) * 8;
            return (j >= lo && j < lo + 8) ? CQ_BIT_Q : CQ_BIT_ZERO;
        }
        return (r == 0) ? ((j >= (nb - 1) * 8) ? CQ_BIT_Q : CQ_BIT_ZERO)
                        : CQ_BIT_Q;
    case CQ_IBF_N:
        break;
    }
    return CQ_BIT_Q;
}

/* PRD §3's fold table, over one gate's controls. */
static int sc_fold(int kind, const int *c, int nc, int *dem)
{
    for (int i = 0; i < nc; i++) {
        if (c[i] == CQ_BIT_ZERO) return IB_PRED_NONE;
        if (c[i] == CQ_BIT_ONE) { kind--; (*dem)++; }
    }
    return kind;
}

/* One slot `v` of row `r`: each block's schedule, transcribed from the pinned
 * lowering (adder.jl:8-16, arith.jl:424-447, :522-532, :274-282). */
static int sc_slot(cq_ibits_fn f, int W, int r, int op, int v, int *dem)
{
    int c[2], i;

    switch (op) {
    case CQ_IBOP_ADD:                          /* lower_add!                */
        if (v < 5 * (W - 1)) {
            i = v / 5;
            switch (v % 5) {
            case 0: c[0] = sc_lane(f, W, r, 0, i); return sc_fold(CQ_OP_CX, c, 1, dem);
            case 1: c[0] = sc_lane(f, W, r, 1, i); return sc_fold(CQ_OP_CX, c, 1, dem);
            case 2: c[0] = sc_lane(f, W, r, 0, i); c[1] = sc_lane(f, W, r, 1, i);
                    return sc_fold(CQ_OP_CCX, c, 2, dem);
            case 3: return CQ_OP_CCX;          /* t[i], c[i]: scratch       */
            default: return CQ_OP_CX;          /* c[i] -> t[i]              */
            }
        }
        v -= 5 * (W - 1);
        if (v == 2) return CQ_OP_CX;
        c[0] = sc_lane(f, W, r, v, W - 1);
        return sc_fold(CQ_OP_CX, c, 1, dem);
    case CQ_IBOP_NE:                           /* lower_eq!                 */
        if (v < 2 * W) {
            c[0] = sc_lane(f, W, r, v % 2, v / 2);
            return sc_fold(CQ_OP_CX, c, 1, dem);
        }
        return ((v - 2 * W) % 3 == 2) ? CQ_OP_CCX : CQ_OP_CX;
    case CQ_IBOP_MUX:                          /* lower_mux!: f, t, f, cond */
        i = v / 4;
        if (v % 4 == 3) return CQ_OP_CCX;
        c[0] = sc_lane(f, W, r, (v % 4 == 1) ? 1 : 2, i);
        return sc_fold(CQ_OP_CX, c, 1, dem);
    default:                                   /* lower_or!                 */
        i = v / 3;
        if (v % 3 < 2) {
            c[0] = sc_lane(f, W, r, v % 3, i);
            return sc_fold(CQ_OP_CX, c, 1, dem);
        }
        c[0] = sc_lane(f, W, r, 0, i); c[1] = sc_lane(f, W, r, 1, i);
        return sc_fold(CQ_OP_CCX, c, 2, dem);
    }
}

/* The scan's widths: every narrow one, and two ABI ones. */
static const int SCAN_W[]       = { 1, 2, 3, 5, 8, 16 };
static const int SCAN_BSWAP_W[] = { 16, 32 };

static int scan_widths(cq_ibits_fn f, const int **ws)
{
    if (f == CQ_IBF_BSWAP) { *ws = SCAN_BSWAP_W; return 2; }
    *ws = SCAN_W;
    return 6;
}

/* A value with set and clear lanes at every narrow width. */
static cq_ref_w scan_value(int W) { return cq_ref_w_make(0xB5A5C3D2u, 0u, W); }

CQ_TEST(the_slot_boundaries_match_an_independent_four_valued_scan)
{
    cq_mock m;

    cq_mock_init(&m);
    for (int f = 0; f < CQ_IBF_N; f++) {
        const int *ws;
        int nw = scan_widths((cq_ibits_fn)f, &ws);

        for (int wi = 0; wi < nw; wi++) {
            int W = ws[wi], n, folded = 0, demoted = 0, bad = 0;
            cq_ibits_row rows[CQ_IBITS_MAX_ROWS];
            cq_ref_w v = scan_value(W);
            size_t cursor = 0u;

            n = cq_ibits_program((cq_ibits_fn)f, W, rows);
            CHECK_EQ(ib_run((cq_ibits_fn)f, W, v, cq_ref_w_ones(W), &m).lo,
                     ORACLE[f](v.lo, W));

            for (int r = 0; r < n && !bad; r++)
                for (int s = 0; s < cq_ibits_row_steps(&rows[r], W); s++) {
                    int pr = sc_slot((cq_ibits_fn)f, W, r, rows[r].op, s,
                                     &demoted);

                    if (pr == IB_PRED_NONE) { folded++; continue; }
                    if (cursor >= cq_mock_count(&m)
                        || (int)cq_mock_at(&m, cursor)->op != pr) {
                        cq_h_fail(__FILE__, __LINE__, "%s W=%d row %d (op %d) "
                                  "slot %d (stream %zu): emitted %d, the scan "
                                  "says %d", SPEC[f].name, W, r, rows[r].op, s,
                                  cursor, cursor < cq_mock_count(&m)
                                  ? (int)cq_mock_at(&m, cursor)->op : -1, pr);
                        bad = 1;
                        break;
                    }
                    cursor++;
                }
            if (bad) continue;

            /* NOT VACUOUS: every program folds (the one-lane `bit` views and
             * the `add 0` constant), and ctlz/cttz DEMOTE — their seed `W` and
             * their `select` arms are constants with set lanes. */
            CHECK(folded > 0);
            if (f == CQ_IBF_CTLZ || f == CQ_IBF_CTTZ) CHECK(demoted > 0);

            /* The copy-out is W CNOTs and the reverse half is the same
             * `cursor` gates again. */
            CHECK_EQ((long long)cq_mock_count(&m),
                     (long long)(2u * cursor + (size_t)W));
            for (int j = 0; j < W; j++)
                CHECK_EQ((int)cq_mock_at(&m, cursor + (size_t)j)->op,
                         (int)CQ_OP_CX);
            CHECK(cq_mock_is_palindrome(&m, cursor, (size_t)W));
        }
    }
    cq_mock_dispose(&m);
}
