/* tests/test_kernel_fpround_slots.inc — the composition identity in TWO cases,
 * the four-valued slot scan, and the row tables' well-formedness. Included by
 * test_kernel_fpround.c. NOTHING BELOW READS A GOLDEN.
 *
 * K11's RECORDED FINDING, APPLIED IN ADVANCE. The one mutant L1 cannot see is
 * the one that looks like an optimisation, and L4 is not a durable detector
 * because `CQOPS_UPDATE_GOLDENS=1` blesses a narrowing as readily as a fix.
 * M32 offers that trade on essentially every span: `exp_pack` is eleven bits
 * of value in a 64-lane span, `grs` is three, `guard`, `round_bit`,
 * `sticky_bit` and every `need_k` are one, `frac` is 52. Narrowing any of them
 * keeps every value, every palindrome and the whole pool. What catches them is
 * here.
 *
 * AND THE IDENTITY IS OVER SLOTS, NOT GATES — D-K18-6, which M31 was the first
 * module to need and which bites harder here: 41 of K23's 154 rows are VIEWS,
 * so classical lanes sit INSIDE block operands at every operand mask including
 * the all-quantum one L4 pins, and a compute slot may emit NOTHING.
 * `compute = sum of blocks` is true of slots and false of gates.
 *
 * TWO CASES DELIBERATELY (K12.md §3.0's shape). The first runs NO BLOCK and
 * pins each composed block's cost against its own module AND against a
 * re-derivation of that module's loop written here; the second pins each
 * helper against what those just measured. A sibling's cost moving reddens the
 * FIRST and NAMES the block, rather than reddening a per-helper identity with
 * a number nobody can attribute.
 */

/* ---- (a1) The block costs, asked of their modules and re-derived here. --- */

/* THE COMPOSITION IDENTITY BELOW CANNOT SEE `cq_eq_steps` AND FRIENDS MOVE,
 * because both of its sides ask them (bd 9ve.2, measured on M12: eight cases
 * red, the composition case green). These are the detectors for that — each
 * upstream loop counted a SECOND time from its own structure rather than from
 * the closed form copied out of a header. */
static int fpr_n_eq(int W)   { return 2 * W + 3 * (W - 1); }      /* arith.jl:424-447 */
static int fpr_n_ult(int W)  { return 2 * W + 1 + 4 * W; }        /* arith.jl:449-463 */
static int fpr_n_slt(int W)  { return 2 * W + 2 + fpr_n_ult(W); } /* arith.jl:465-472 */
static int fpr_n_mux(int W)  { return 4 * W; }                    /* arith.jl:522-532 */

static int fpr_n_add(int W)                                       /* adder.jl:1-18 */
{
    int n = 0;

    for (int i = 0; i < W; i++) { n += 3; if (i < W - 1) n += 2; }
    return n;
}

static int fpr_n_sub(int W)                                       /* adder.jl:148-172 */
{
    return 2 * W + 1 + fpr_n_add(W);        /* the complement, the seed, the ripple */
}

static int fpr_n_barrel(int W, int S)                             /* arith.jl:350-380 */
{
    int n = W;                                             /* INIT, :351-352 */

    for (int j = 0; j < S; j++) n += (W - (1 << j)) + 4 * W;
    return n;
}

CQ_TEST(the_blocks_cost_what_their_modules_say_they_cost)
{
    /* ASKED of M12, M14, M16 and M17, and each pinned against the loop above.
     * M32 composes all seven, so if one moves everything below moves with it
     * and this line says so first. */
    CHECK_EQ(cq_eq_steps(W64),  fpr_n_eq(W64));
    CHECK_EQ(cq_ult_steps(W64), fpr_n_ult(W64));
    CHECK_EQ(cq_slt_steps(W64), fpr_n_slt(W64));
    CHECK_EQ(cq_sub_steps(W64), fpr_n_sub(W64));
    CHECK_EQ(cq_add_steps(W64), fpr_n_add(W64));
    CHECK_EQ(cq_mux_steps(W64), fpr_n_mux(W64));
    CHECK_EQ(cq_barrel_steps(W64, CQ_BARREL_SHL),  fpr_n_barrel(W64, 6));
    CHECK_EQ(cq_barrel_steps(W64, CQ_BARREL_LSHR), fpr_n_barrel(W64, 6));

    /* The scratch each block occupies, published as a function since bd
     * 9ve.19 rather than only in prose — the whole reason M32 does not write
     * `2W - 1`, `3W + 1`, `5W + 1`, `3W`, `2W` and `2W` down at 154 rows. */
    CHECK_EQ(cq_eq_region(W64),  2 * W64 - 1);
    CHECK_EQ(cq_ult_region(W64), 3 * W64 + 1);
    CHECK_EQ(cq_slt_region(W64), 5 * W64 + 1);
    CHECK_EQ(cq_sub_region(W64), 3 * W64);
    CHECK_EQ(cq_add_region(W64), 2 * W64);
    CHECK_EQ(cq_mux_region(W64), 2 * W64);
    CHECK_EQ(cq_barrel_region(W64), W64 * (3 * 6 + 1));

    /* THE SIGNED COMPARATOR COSTS MORE THAN THE UNSIGNED ONE, which is what
     * makes L4 the detector for M32's ONE deliberately-equivalent row (R15,
     * `grs > UInt64(4)`) and the non-detector for the eight signed ones. */
    CHECK(cq_slt_steps(W64) > cq_ult_steps(W64));

    /* AND THE WIDTH IS 64, WHICH IS THE WHOLE OF THE NARROWING GUARD. PRD-v2
     * §7.2 forbids narrowing an intermediate towards the field width, so every
     * block runs at the Julia type's width — `exp_pack` is eleven bits of
     * value in a 64-lane span and `grs` is three. */
    CHECK(cq_eq_steps(CQ_FP64_EXP_W) < cq_eq_steps(W64));
    CHECK(cq_slt_steps(3)            < cq_slt_steps(W64));
}

/* ---- (a2) Each helper against what those blocks just cost. -------------- */

/* THE FOUR HELPERS, COUNTED A SECOND TIME AND INDEPENDENTLY, straight off the
 * pinned Julia rather than off fpround.c's row tables:
 *
 *   norm52  (:68-105)  1 eq + 1 not1 + 1 mux (the tpg0 guard), six stages of
 *                      (1 eq + 1 not1 + 1 mux + 1 sub + 1 mux), 2 mux (the
 *                      restore), 13 views
 *   clz     (:113-139) the same six stages and nothing else, 12 views
 *   subnorm (:174-191) 4 slt, 2 not1, 2 sub, 7 mux, 1 and, 1 eq, 1 or,
 *                      1 shl, 1 lshr, 2 views
 *   round   (:198-227) 4 slt, 1 ult, 5 not1, 3 eq, 2 add, 5 mux, 5 or(64),
 *                      2 and(1), 1 or(1), 14 views
 *
 * Miscount one and the identity goes red and NAMES the helper. */
typedef struct {
    int eq, ult, slt, sub, add, mux, and64, or64, bshl, blshr;
    int not1, and1, or1, view;
} fpr_tally;

static const fpr_tally T_NORM52  = { 7, 0, 0, 6, 0, 15, 0, 0, 0, 0, 7, 0, 0, 13 };
static const fpr_tally T_CLZ     = { 6, 0, 0, 6, 0, 12, 0, 0, 0, 0, 6, 0, 0, 12 };
static const fpr_tally T_SUBNORM = { 1, 0, 4, 2, 0,  7, 1, 1, 1, 1, 2, 0, 0,  2 };
static const fpr_tally T_ROUND   = { 3, 1, 4, 0, 2,  5, 0, 5, 0, 0, 5, 2, 1, 14 };

enum { FR_NOT1 = 2, FR_AND1 = 1, FR_OR1 = 3 };

static const fpr_tally *fpr_tally_of(cq_fpround_id id)
{
    switch (id) {
    case CQ_FPR_NORM52:  return &T_NORM52;
    case CQ_FPR_CLZ:     return &T_CLZ;
    case CQ_FPR_SUBNORM: return &T_SUBNORM;
    default:             return &T_ROUND;
    }
}

static int fpr_tally_rows(const fpr_tally *t)
{
    return t->eq + t->ult + t->slt + t->sub + t->add + t->mux + t->and64
         + t->or64 + t->bshl + t->blshr + t->not1 + t->and1 + t->or1 + t->view;
}

static int fpr_tally_slots(const fpr_tally *t)
{
    return t->eq  * cq_eq_steps(W64)  + t->ult * cq_ult_steps(W64)
         + t->slt * cq_slt_steps(W64) + t->sub * cq_sub_steps(W64)
         + t->add * cq_add_steps(W64) + t->mux * cq_mux_steps(W64)
         + t->and64 * W64 + t->or64 * 3 * W64
         + t->bshl  * cq_barrel_steps(W64, CQ_BARREL_SHL)
         + t->blshr * cq_barrel_steps(W64, CQ_BARREL_LSHR)
         + t->not1 * FR_NOT1 + t->and1 * FR_AND1 + t->or1 * FR_OR1;
}

static uint32_t fpr_tally_region(const fpr_tally *t)
{
    return (uint32_t)(t->eq  * cq_eq_region(W64)  + t->ult * cq_ult_region(W64)
         + t->slt * cq_slt_region(W64) + t->sub * cq_sub_region(W64)
         + t->add * cq_add_region(W64) + t->mux * cq_mux_region(W64)
         + (t->and64 + t->or64) * W64
         + (t->bshl + t->blshr) * cq_barrel_region(W64)
         + t->not1 + t->and1 + t->or1);
}

CQ_TEST(each_helper_is_the_sum_of_its_rows)
{
    for (int r = 0; r < N_ROWS; r++) {
        cq_fpround_id id = ROWS[r].id;
        const fpr_tally *t = fpr_tally_of(id);
        int n = 0;

        (void)cq_fpround_rows(id, &n);

        /* THE COMPOSITION IDENTITY. Every `C_*` is ASKED of its module and
         * never written down; the block COUNTS are the port's own content and
         * the thing a reader must be able to check against the Julia. */
        CHECK_EQ(n, fpr_tally_rows(t));
        CHECK_EQ(fpr_steps_of(id),  fpr_tally_slots(t));
        CHECK_EQ((long long)fpr_region_of(id), (long long)fpr_tally_region(t));

        printf("# %-8s rows %3d  views %2d  slots %5d  scratch %5u bits\n",
               ROWS[r].name, n, t->view, fpr_steps_of(id), fpr_region_of(id));
    }
    fflush(stdout);
}

/* Which rows carry a Bool, and what each row costs in SLOTS. Both are used
 * by the well-formedness case below and by the two scans next door, so they
 * sit here rather than in either. */
static int fpr_is_flag(int op)
{
    return op == CQ_FROP_EQ || op == CQ_FROP_ULT || op == CQ_FROP_SLT
        || op == CQ_FROP_NOT1 || op == CQ_FROP_AND1 || op == CQ_FROP_OR1;
}

static int fpr_row_slots(int op)
{
    switch (op) {
    case CQ_FROP_VIEW:  return 0;
    case CQ_FROP_EQ:    return cq_eq_steps(W64);
    case CQ_FROP_ULT:   return cq_ult_steps(W64);
    case CQ_FROP_SLT:   return cq_slt_steps(W64);
    case CQ_FROP_SUB:   return cq_sub_steps(W64);
    case CQ_FROP_ADD:   return cq_add_steps(W64);
    case CQ_FROP_MUX:   return cq_mux_steps(W64);
    case CQ_FROP_AND:   return W64;
    case CQ_FROP_OR:    return 3 * W64;
    case CQ_FROP_BSHL:  return cq_barrel_steps(W64, CQ_BARREL_SHL);
    case CQ_FROP_BLSHR: return cq_barrel_steps(W64, CQ_BARREL_LSHR);
    case CQ_FROP_NOT1:  return FR_NOT1;
    case CQ_FROP_AND1:  return FR_AND1;
    default:            return FR_OR1;
    }
}

/* ---- The row tables' well-formedness. ---------------------------------- */

/* A FORWARD ROW REFERENCE IS A DIFFERENT HAZARD FROM AN OUT-OF-RANGE ONE, and
 * only this case sees it: row 4 reading row 5 resolves to a span that exists,
 * so the machine's refusals never fire and only the VALUE moves. It is also
 * where the 1-bit / 64-lane split is asserted on the TABLE, which the machine
 * asserts on the MACHINE — two readings of one contract, and M36 measured that
 * the table-side case cannot see a machine-side defect (bd
 * a-table-driven-kernel-needs-both-operand-refusals). */
CQ_TEST(the_row_tables_are_well_formed)
{
    for (int r = 0; r < N_ROWS; r++) {
        int n = 0;
        const cq_fpround_row *rows = cq_fpround_rows(ROWS[r].id, &n);

        for (int i = 0; i < n; i++) {
            short s[3] = { rows[i].s0, rows[i].s1, rows[i].s2 };
            int ar = cq_fpround_arity(rows[i].op);
            int one_bit = (rows[i].op == CQ_FROP_NOT1
                        || rows[i].op == CQ_FROP_AND1
                        || rows[i].op == CQ_FROP_OR1);

            for (int j = 0; j < ar; j++) {
                if (s[j] >= i)
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d operand %d names row %d, which is not "
                              "STRICTLY earlier", ROWS[r].name, i, j, s[j]);
                if (s[j] < 0 && s[j] < CQ_FR_K_INF)
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d operand %d is an unknown code %d",
                              ROWS[r].name, i, j, s[j]);
                /* A 1-bit operator's operands, and a mux's CONDITION, must be
                 * rows whose output is a Bool. */
                if ((one_bit || (rows[i].op == CQ_FROP_MUX && j == 0))
                    && (s[j] < 0 || !fpr_is_flag(rows[s[j]].op)))
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d operand %d is not a Bool row",
                              ROWS[r].name, i, j);
            }
        }
        /* The last row is the helper's own first output in every table but
         * `_sf_handle_subnormal`'s, whose five outputs are scattered — so the
         * claim that is true of all four is that every declared output names a
         * row inside the table. */
        for (int w = 0; w < 5; w++) {
            int ok = 1, idx = -1;

            if (ROWS[r].id == CQ_FPR_NORM52 && w >= 2) continue;
            if (ROWS[r].id == CQ_FPR_CLZ    && w >= 2) continue;
            if (ROWS[r].id == CQ_FPR_ROUND  && w >= 4) continue;
            idx = cq_fpround_out_row(ROWS[r].id, w);
            if (idx < 0 || idx >= n) ok = 0;
            if (!ok) cq_h_fail(__FILE__, __LINE__,
                               "%s output %d names row %d, outside [0, %d)",
                               ROWS[r].name, w, idx, n);
        }
    }
}

#include "test_kernel_fpround_scan.inc"
