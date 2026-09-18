/* tests/test_kernel_fcmp_slots.inc — the composition identity, the four-valued
 * slot scan. The layout half — the span-disjointness scan, the palindrome and
 * the two-programs case — is next door in test_kernel_fcmp_spans.inc.
 * Included by test_kernel_fcmp.c. NOTHING BELOW READS A GOLDEN.
 *
 * K11's RECORDED FINDING, APPLIED IN ADVANCE. The one mutant L1 cannot see is
 * the one that looks like an optimisation, and L4 is not a durable detector
 * because `CQOPS_UPDATE_GOLDENS=1` blesses a narrowing as readily as a fix.
 * K18 offers that trade four times over and every offer is tempting: run the
 * `ea == 0x7FF` test at W = 11, run `sa > sb` as a single Toffoli instead of a
 * 64-bit `ult` block, CSE the three copies of the NaN test that `ule` carries,
 * or narrow `abs_a`/`abs_b` to 63 lanes because lane 63 is provably zero. None
 * changes a value, a palindrome or the pool. What catches them is here.
 *
 * AND THE IDENTITY IS OVER SLOTS, NOT GATES — D-K18-6, which K18 wrote down
 * and M31 was the first module to need. A block operand is a view with 1, 12
 * or 53 constant lanes, or a constant span, so the §3 fold table elides gates
 * at EVERY operand mask including the all-quantum one L4 pins.
 * `compute = sum of blocks` is true of slots and false of gates.
 */

/* ---- (a1) The block costs, ASKED of their modules. Runs no kernel. ------ */

/* TWO CASES DELIBERATELY, so a sibling's cost moving reddens the FIRST and
 * NAMES the block rather than reddening the per-predicate identity with a
 * number nobody can attribute (K12.md §3.0's shape). This is the one that runs
 * no kernel. */
CQ_TEST(the_blocks_cost_what_their_modules_say_they_cost)
{
    /* ASKED of M16, M14, M17 and M31 and pinned against their own measured
     * W = 64 rows. K18 composes all five, so if one moves everything below
     * moves with it and this line says so first. */
    CHECK_EQ(cq_eq_steps(CQ_FP64_W),  317);          /* lower_eq!  5W-3   */
    CHECK_EQ(cq_ult_steps(CQ_FP64_W), 385);          /* lower_ult! 6W+1   */
    CHECK_EQ(cq_sub_steps(CQ_FP64_W), 447);          /* lower_sub! 7W-1   */
    CHECK_EQ((int)CQ_MUX_STEPS_PER_BIT, 4);          /* lower_mux! 4 per bit */
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_NAN), 637);  /* M31, 2 eq + not1 + and */

    /* The scratch contracts the layout budgets, each stated by its own header:
     * cmp.h's `2W - 1` for `eq` (diff ++ orr) and `3W + 1` for `ult` (nb ++
     * carry ++ axnb, the carry being W+1 and NOT W); add.h's `3W` for `sub`
     * (nb ++ d ++ c); mux.h's `2` at one bit (r ++ d). */
    CHECK_EQ(2 * CQ_FP64_W - 1, 127);
    CHECK_EQ(3 * CQ_FP64_W + 1, 193);
    CHECK_EQ(3 * CQ_FP64_W,     192);
    CHECK_EQ(cq_fp_class_region(CQ_FP_IS_NAN), 256u);

    /* AND THE WIDTH IS 64, WHICH IS THE WHOLE OF THE NARROWING GUARD. The
     * exponent field is 11 bits, the fraction 52 and a sign comparison 1;
     * PRD-v2 §7.2 forbids narrowing an intermediate towards the field width,
     * so every block runs at the Julia type's width. A port that narrowed
     * would show up below as a predicate no longer being the sum of its
     * blocks at 64. */
    CHECK(cq_eq_steps(CQ_FP64_EXP_W)  < cq_eq_steps(CQ_FP64_W));
    CHECK(cq_ult_steps(1)             < cq_ult_steps(CQ_FP64_W));
    CHECK(cq_sub_steps(1)             < cq_sub_steps(CQ_FP64_W));
}

/* ---- (a2) Each predicate against what those blocks just cost. ---------- */

/* THE TEN BODIES, COUNTED A SECOND TIME AND INDEPENDENTLY, straight off the
 * pinned Julia rather than off fcmp.c's row tables:
 *
 *   B_nan  (fcmp.jl:117-125)  2 class blocks, 1 `or`
 *   B_oeq  (fcmp.jl:57-82)    2 class, 3 `eq`, 4 `not1`, 2 `and`, 2 `or`
 *   B_olt  (fcmp.jl:8-47)     2 class, 4 `eq`, 3 `ult`, 2 `mux`,
 *                             9 `not1`, 3 `and`, 1 `or`
 *
 * B_olt's nine `not1` are the eight `==`/`<`/`>` polarity fixes (:27 twice,
 * :32, :33, :36, :38, :40) plus `!both_zero` and `!either_nan` at :44 — one
 * for each of the ten occurrences minus the two `!=` reads that take the raw
 * wire. Miscount one and the identity below goes red and names the predicate. */
typedef struct { int cls, eq, ult, sub, mux, not1, and1, or1; } fc_blocks;

static const fc_blocks FC_BNAN = { 2, 0, 0, 0, 0, 0, 0, 1 };
static const fc_blocks FC_BOEQ = { 2, 3, 0, 0, 0, 4, 2, 2 };
static const fc_blocks FC_BOLT = { 2, 4, 3, 0, 2, 9, 3, 1 };

static void fc_add(fc_blocks *t, const fc_blocks *b)
{
    t->cls += b->cls; t->eq += b->eq;  t->ult  += b->ult;  t->sub  += b->sub;
    t->mux += b->mux; t->not1 += b->not1; t->and1 += b->and1; t->or1 += b->or1;
}

/* The seven one-line compositions of fcmp.jl, each named by its own line. */
static fc_blocks fc_blocks_of(cq_fcmp_pred callee)
{
    fc_blocks t = { 0, 0, 0, 0, 0, 0, 0, 0 };

    switch (callee) {
    case CQ_FCMP_OEQ: fc_add(&t, &FC_BOEQ); break;
    case CQ_FCMP_OLT: fc_add(&t, &FC_BOLT); break;
    case CQ_FCMP_UNO: fc_add(&t, &FC_BNAN); break;                    /* :144 */
    case CQ_FCMP_ORD: fc_add(&t, &FC_BNAN); t.not1++; break;          /* :134 */
    case CQ_FCMP_UNE: fc_add(&t, &FC_BOEQ); t.sub++;  break;          /* :102 */
    case CQ_FCMP_OLE: fc_add(&t, &FC_BOLT); fc_add(&t, &FC_BOEQ);
                      t.or1++; break;                                  /* :90  */
    case CQ_FCMP_UEQ: fc_add(&t, &FC_BNAN); fc_add(&t, &FC_BOEQ);
                      t.or1++; break;                                  /* :164 */
    case CQ_FCMP_ULT: fc_add(&t, &FC_BNAN); fc_add(&t, &FC_BOLT);
                      t.or1++; break;                                  /* :174 */
    case CQ_FCMP_ULE: fc_add(&t, &FC_BNAN); fc_add(&t, &FC_BOLT);
                      fc_add(&t, &FC_BOEQ); t.or1 += 2; break;         /* :184 */
    default:          fc_add(&t, &FC_BNAN); t.not1++;                  /* :154 */
                      fc_add(&t, &FC_BOEQ); t.sub++; t.and1++; break;
    }
    return t;
}

/* `lower_not1!` is two slots and one bit (arith.jl:474-478), `lower_and!` one
 * slot and no bit of its own at one lane (:268-272), `lower_or!` three slots
 * and one bit (:274-282). Transcribed here from the pinned source, as K18 §2.2
 * does: they are emitted inline by the fp step function and no module
 * publishes a cost for them. */
enum { FS_NOT1 = 2, FS_AND1 = 1, FS_OR1 = 3 };

static int fc_slots(const fc_blocks *t)
{
    return t->cls  * cq_fp_class_steps(CQ_FP_IS_NAN)
         + t->eq   * cq_eq_steps(CQ_FP64_W)
         + t->ult  * cq_ult_steps(CQ_FP64_W)
         + t->sub  * cq_sub_steps(CQ_FP64_W)
         + t->mux  * CQ_MUX_STEPS_PER_BIT
         + t->not1 * FS_NOT1 + t->and1 * FS_AND1 + t->or1 * FS_OR1;
}

static uint32_t fc_region(const fc_blocks *t)
{
    return (uint32_t)t->cls * cq_fp_class_region(CQ_FP_IS_NAN)
         + (uint32_t)(t->eq  * (2 * CQ_FP64_W - 1))
         + (uint32_t)(t->ult * (3 * CQ_FP64_W + 1))
         + (uint32_t)(t->sub * (3 * CQ_FP64_W))
         + (uint32_t)(t->mux * 2)
         + (uint32_t)(t->not1 + t->and1 + t->or1);
}

CQ_TEST(each_predicate_is_the_sum_of_its_blocks)
{
    for (int i = 0; i < N_FCMP_ROWS; i++) {
        fc_blocks t = fc_blocks_of(cq_fcmp_callee(ROWS[i].pred));
        cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];
        int n = cq_fcmp_program(ROWS[i].pred, rows);
        int n_rows = t.cls + t.eq + t.ult + t.sub + t.mux
                   + t.not1 + t.and1 + t.or1;
        cq_counter fwd, unc;

        /* THE COMPOSITION IDENTITY. Every `C_*` is ASKED of its module and
         * never written down; the block COUNTS are the port's own content and
         * the thing a reader must be able to check against the Julia. */
        CHECK_EQ(n, n_rows);
        CHECK_EQ(cq_fcmp_steps(rows, n),  fc_slots(&t));
        CHECK_EQ(cq_fcmp_region(rows, n), fc_region(&t));

        /* SLOTS AND GATES COME APART, ASSERTED RATHER THAN NARRATED: the
         * sandwiched total is strictly below twice the slot count plus the
         * copy-out, because the views' constant lanes and the constant spans
         * fold. An implementation that had materialised its constants
         * (upstream's `resolve!` shape, PRD-v2 §7.3's refused alternative)
         * would make this an equality. */
        cq_kd_measure(&ROWS[i].spec, CQ_FP64_W, &fwd, &unc);
        CHECK(cq_count_total(&fwd)
              < 2u * (uint64_t)cq_fcmp_steps(rows, n) + 1u);

        printf("# %-9s rows %2d  slots %5d  forward (x %llu, cx %llu, "
               "ccx %llu) = %llu  unc (x %llu, cx %llu, ccx %llu) = %llu  "
               "region %u bits\n",
               ROWS[i].spec.name, n, cq_fcmp_steps(rows, n),
               (unsigned long long)fwd.x, (unsigned long long)fwd.cx,
               (unsigned long long)fwd.ccx,
               (unsigned long long)cq_count_total(&fwd),
               (unsigned long long)unc.x, (unsigned long long)unc.cx,
               (unsigned long long)unc.ccx,
               (unsigned long long)cq_count_total(&unc),
               cq_fcmp_region(rows, n));
    }
    fflush(stdout);
}

/* ---- (b) The four-valued slot scan. ------------------------------------ */

/* THE PREDICTION IS A KIND **OR NOTHING**, AND THE STREAM HAS ITS OWN CURSOR.
 * At the all-quantum mask every v1 kernel emits exactly one gate per slot, so
 * `slot index == stream index` and K12's scan walked them together. That is
 * false here: a control lane that is a constant ZERO makes the gate vanish and
 * a constant ONE demotes it (CCX->CX, CX->X). Both happen at every mask,
 * because they come from M31's VIEWS and from the CONSTANT SPANS rather than
 * from the operands' bit-kinds. So the scan predicts NONE / X / CX / CCX from
 * the lane table alone and advances the stream cursor only on a non-NONE. */
enum { FC_PRED_NONE = -1 };

_Static_assert(CQ_OP_X == 0 && CQ_OP_CX == 1 && CQ_OP_CCX == 2,
               "the scan demotes a gate by decrementing its op");

/* At the mask this scan runs at, `a` and `b` are all-quantum, so an operand
 * lane is Q inside the field the code names and CQ_BIT_ZERO in the fill. */
static int fc_lane(int s, int i)
{
    switch (s) {
    case CQ_FC_A: case CQ_FC_B:
        return CQ_BIT_Q;
    case CQ_FC_ABS_A: case CQ_FC_ABS_B:            /* a & ABS_MASK: 63 lanes */
        return (i < CQ_FP64_W - 1) ? CQ_BIT_Q : CQ_BIT_ZERO;
    case CQ_FC_SIGN_A: case CQ_FC_SIGN_B:          /* a >> 63: one lane      */
        return (i == 0) ? CQ_BIT_Q : CQ_BIT_ZERO;
    case CQ_FC_K_ZERO:
        return CQ_BIT_ZERO;
    case CQ_FC_K_ONE:
        return (i == 0) ? CQ_BIT_ONE : CQ_BIT_ZERO;
    default: break;
    }
    /* A row's own wire, zero-extended to the Julia type's width. Lane 0 is
     * scratch, so CQ_BIT_Q from step 0 under I6(b). */
    return (i == 0) ? CQ_BIT_Q : CQ_BIT_ZERO;
}

/* PRD §3's fold table, over one gate's controls. */
static int fc_fold(int kind, const int *ctrl, int nc, int *dem)
{
    for (int i = 0; i < nc; i++) {
        if (ctrl[i] == CQ_BIT_ZERO) return FC_PRED_NONE;
        if (ctrl[i] == CQ_BIT_ONE) { kind--; if (dem) (*dem)++; }
    }
    return kind;
}

/* `lower_eq!` (arith.jl:424-447): 2n slots of CX building `diff` — the only
 * place an operand lane is a control — then a 3-cycle CX, CX, CCX over the
 * OR-prefix, whose controls are all scratch and so cannot fold. */
static int fc_sched_eq(int sa, int sb, int v, int *dem)
{
    if (v < 2 * CQ_FP64_W) {
        int i = v / 2, c[1];

        c[0] = (v % 2 == 0) ? fc_lane(sa, i) : fc_lane(sb, i);
        return fc_fold(CQ_OP_CX, c, 1, dem);
    }
    return ((v - 2 * CQ_FP64_W) % 3 == 2) ? CQ_OP_CCX : CQ_OP_CX;
}

/* `lower_ult!` (arith.jl:449-463): 2W alternating CX/X building `~b`, the
 * carry seed, then a 4-cycle per lane. `nb`, `axnb` and `carry` are scratch. */
static int fc_sched_ult(int sa, int sb, int v, int *dem)
{
    int W = CQ_FP64_W, c[2], u, i;

    if (v < 2 * W) {
        if (v % 2 == 0) { c[0] = fc_lane(sb, v / 2);
                          return fc_fold(CQ_OP_CX, c, 1, dem); }
        return CQ_OP_X;
    }
    if (v == 2 * W) return CQ_OP_X;                    /* carry_in = 1 */

    u = v - (2 * W + 1);
    i = u / 4;
    switch (u % 4) {
    case 0:  c[0] = fc_lane(sa, i); return fc_fold(CQ_OP_CX, c, 1, dem);
    case 1:  return CQ_OP_CX;                          /* nb[i] -> axnb[i] */
    case 2:  c[0] = fc_lane(sa, i); c[1] = CQ_BIT_Q;   /* nb[i] is scratch */
             return fc_fold(CQ_OP_CCX, c, 2, dem);
    default: return CQ_OP_CCX;
    }
}

/* `lower_sub!` (adder.jl:149-172): the same 2W complement and seed, then a
 * 5-cycle per lane with the top stage's two Toffolis dropped by upstream's
 * own `if i < W` guard. */
static int fc_sched_sub(int sa, int sb, int v, int *dem)
{
    int W = CQ_FP64_W, c[2], u, i;

    if (v < 2 * W) {
        if (v % 2 == 0) { c[0] = fc_lane(sb, v / 2);
                          return fc_fold(CQ_OP_CX, c, 1, dem); }
        return CQ_OP_X;
    }
    if (v == 2 * W) return CQ_OP_X;                    /* carry_in = 1 */

    u = v - (2 * W + 1);
    if (u < 5 * (W - 1)) {
        i = u / 5;
        switch (u % 5) {
        case 0:  c[0] = fc_lane(sa, i); return fc_fold(CQ_OP_CX, c, 1, dem);
        case 1:  return CQ_OP_CX;                      /* nb[i] -> d[i] */
        case 2:  c[0] = fc_lane(sa, i); c[1] = CQ_BIT_Q;
                 return fc_fold(CQ_OP_CCX, c, 2, dem);
        case 3:  return CQ_OP_CCX;
        default: return CQ_OP_CX;                      /* c[i] -> d[i]  */
        }
    }
    if (u - 5 * (W - 1) == 0) {
        c[0] = fc_lane(sa, W - 1);
        return fc_fold(CQ_OP_CX, c, 1, dem);
    }
    return CQ_OP_CX;
}

/* M31's class block: an `eq` over the EXPONENT view against the constant
 * 0x7FF, a `lower_not1!` (the source spells `==`), an `eq` over the FRACTION
 * view against zero (the source spells `!=`, so no not1), and the `and`.
 * The exponent view is lanes 52..62 and NOT 52..63 — a view that swallowed
 * the sign lane computes `ea == 0x7FF` correctly for every POSITIVE operand. */
static int fc_sched_class(int s, int v, int *dem)
{
    int C = cq_eq_steps(CQ_FP64_W), c[1], i;

    if (v < C) {
        if (v < 2 * CQ_FP64_W) {
            i = v / 2;
            c[0] = (v % 2 == 0)
                 ? ((i < CQ_FP64_EXP_W) ? fc_lane(s, CQ_FP64_EXP_LO + i)
                                        : CQ_BIT_ZERO)
                 : ((i < CQ_FP64_EXP_W) ? CQ_BIT_ONE : CQ_BIT_ZERO);
            return fc_fold(CQ_OP_CX, c, 1, dem);
        }
        return ((v - 2 * CQ_FP64_W) % 3 == 2) ? CQ_OP_CCX : CQ_OP_CX;
    }
    v -= C;
    if (v < FS_NOT1) return (v == 0) ? CQ_OP_CX : CQ_OP_X;
    v -= FS_NOT1;
    if (v < C) {
        if (v < 2 * CQ_FP64_W) {
            i = v / 2;
            c[0] = (v % 2 == 0)
                 ? ((i < CQ_FP64_FRAC_W) ? fc_lane(s, CQ_FP64_FRAC_LO + i)
                                         : CQ_BIT_ZERO)
                 : CQ_BIT_ZERO;                        /* the constant 0 */
            return fc_fold(CQ_OP_CX, c, 1, dem);
        }
        return ((v - 2 * CQ_FP64_W) % 3 == 2) ? CQ_OP_CCX : CQ_OP_CX;
    }
    return CQ_OP_CCX;                                  /* the `and` */
}

static int fc_row_slots(const cq_fcmp_row *r)
{
    switch (r->op) {
    case CQ_FCOP_CLASS_NAN: return cq_fp_class_steps(CQ_FP_IS_NAN);
    case CQ_FCOP_EQ:        return cq_eq_steps(CQ_FP64_W);
    case CQ_FCOP_ULT:       return cq_ult_steps(CQ_FP64_W);
    case CQ_FCOP_SUB:       return cq_sub_steps(CQ_FP64_W);
    case CQ_FCOP_MUX:       return CQ_MUX_STEPS_PER_BIT;
    case CQ_FCOP_NOT1:      return FS_NOT1;
    case CQ_FCOP_AND1:      return FS_AND1;
    default:                return FS_OR1;
    }
}

static int fc_scan_slot(const cq_fcmp_row *r, int v, int *dem)
{
    switch (r->op) {
    case CQ_FCOP_CLASS_NAN: return fc_sched_class(r->s0, v, dem);
    case CQ_FCOP_EQ:        return fc_sched_eq (r->s0, r->s1, v, dem);
    case CQ_FCOP_ULT:       return fc_sched_ult(r->s0, r->s1, v, dem);
    case CQ_FCOP_SUB:       return fc_sched_sub(r->s0, r->s1, v, dem);
    case CQ_FCOP_MUX:       return (v % 4 == 3) ? CQ_OP_CCX : CQ_OP_CX;
    case CQ_FCOP_NOT1:      return (v == 0) ? CQ_OP_CX : CQ_OP_X;
    case CQ_FCOP_AND1:      return CQ_OP_CCX;
    default:                return (v == 2) ? CQ_OP_CCX : CQ_OP_CX;   /* or */
    }
}

/* A payload NaN against a finite: every predicate has a determinate answer on
 * it, and across the fourteen both answers occur, so the copy-out is reached
 * with a live flag somewhere in the set. */
#define FC_SCAN_A  CQ_F64_QNAN_B
#define FC_SCAN_B  CQ_F64_ONE

CQ_TEST(the_slot_boundaries_match_an_independent_four_valued_scan)
{
    cq_mock m;
    int any_demoted = 0;

    cq_mock_init(&m);

    for (int i = 0; i < N_FCMP_ROWS; i++) {
        cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];
        int n = cq_fcmp_program(ROWS[i].pred, rows);
        size_t cursor = 0u;
        int folded = 0, demoted = 0, bad = 0;
        uint64_t want = (uint64_t)host_fcmp(FC_SCAN_A, FC_SCAN_B, ROWS[i].pred);
        cq_ref_w q = cq_ref_w_ones(CQ_FP64_W);

        /* The VALUE is checked on the way through: a scan that never looked at
         * the answer would happily pin the shape of a wrong circuit. */
        CHECK_EQ(fcmp_run(&ROWS[i], FC_SCAN_A, FC_SCAN_B, q, q, &m), want);

        for (int r = 0; r < n && !bad; r++)
            for (int v = 0; v < fc_row_slots(&rows[r]) && !bad; v++) {
                int pr = fc_scan_slot(&rows[r], v, &demoted);
                cq_op got;

                if (pr == FC_PRED_NONE) { folded++; continue; }

                if (cursor >= cq_mock_count(&m)) {
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d slot %d: the stream ran out at %zu "
                              "gates", ROWS[i].spec.name, r, v, cursor);
                    bad = 1;
                    break;
                }
                got = cq_mock_at(&m, cursor++)->op;
                if ((int)got != pr) {
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d (op %d) slot %d (stream %zu): emitted "
                              "op %d, the slot decode says %d",
                              ROWS[i].spec.name, r, (int)rows[r].op, v,
                              cursor - 1u, (int)got, pr);
                    bad = 1;
                }
            }
        if (bad) continue;

        /* THE SCAN IS NOT VACUOUS. `folded` is the ZERO half and is non-empty
         * for every predicate: every class block's exponent view fills 53
         * lanes with CQ_BIT_ZERO and its fraction view 12, whatever the mask.
         * `demoted` is the ONE half — a CX whose control is a constant ONE
         * becomes an X — and it is reached by every predicate too, because the
         * `ea == 0x7FF` side of every class block compares against a constant
         * with eleven set lanes. */
        CHECK(folded > 0);
        CHECK(demoted > 0);
        any_demoted += demoted;

        /* The copy-out is exactly one CNOT — the "^=" of Rule 7's contract —
         * and the reverse half is the same `cursor` gates again. */
        CHECK_EQ((long long)cq_mock_count(&m), (long long)(2u * cursor + 1u));
        CHECK_EQ((int)cq_mock_at(&m, cursor)->op, (int)CQ_OP_CX);
        CHECK(cq_mock_is_palindrome(&m, cursor, 1u));
    }

    CHECK(any_demoted > 0);
    cq_mock_dispose(&m);
}
