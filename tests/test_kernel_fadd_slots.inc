/* tests/test_kernel_fadd_slots.inc — the COMPOSITION IDENTITY, and the lane
 * machinery the four-valued slot scan next door shares. The scan itself is in
 * test_kernel_fadd_scan.inc and the layout half — span disjointness, the
 * palindrome, two blocks in one region — in test_kernel_fadd_spans.inc.
 * Included by test_kernel_fadd.c. NOTHING BELOW READS A GOLDEN.
 *
 * K11's RECORDED FINDING, APPLIED IN ADVANCE. The one mutant L1 cannot see is
 * the one that looks like an optimisation, and L4 is not a durable detector
 * because `CQOPS_UPDATE_GOLDENS=1` blesses a narrowing as readily as a fix.
 * K15 offers that trade on NINETY-ONE spans rather than on one loop, and every
 * offer is tempting: run `ea` at W = 11, run `sa` as one wire, narrow the
 * `wb >> d` barrel of :79 to the 56 lanes the `d >= 56` select can ever use,
 * CSE the seven repeated subexpressions the literal grain pays for. None
 * changes a value, a palindrome or the pool. What catches them is here.
 *
 * AND THE IDENTITY IS OVER SLOTS, NOT GATES — D-K18-6, which K18 wrote down,
 * M31 was the first module to need and M32 measured at 15-28 %. A block
 * operand is a view with constant fill, or a constant span, so the §3 fold
 * table elides gates at EVERY operand mask including the all-quantum one L4
 * pins. `compute = sum of rows` is true of slots and false of gates.
 */

/* ---- The lane machinery, shared with the scan and the span walk. -------- */

/* §7.3's constant vocabulary, transcribed a SECOND time from fadd.h's own enum
 * comments rather than called out of fadd_step.c. A wrong constant here shows
 * up as a wrong lane profile in the scan, which is the point of writing it
 * again; a wrong constant THERE is a wrong circuit, which L1 sees. */
static uint64_t fa_const(int s)
{
    switch (s) {
    case CQ_FA_K_ZERO:     return 0ull;
    case CQ_FA_K_ONE:      return 1ull;
    case CQ_FA_K_56:       return 56ull;
    case CQ_FA_K_63:       return 63ull;
    case CQ_FA_K_64:       return 64ull;
    case CQ_FA_K_7FF:      return 0x7ffull;
    case CQ_FA_K_IMPLICIT: return 0x0010000000000000ull;
    case CQ_FA_K_INDEF:    return 0xfff8000000000000ull;
    case CQ_FA_K_QUIET:    return 0x0008000000000000ull;
    default:               return FA_SIGN;           /* CQ_FA_K_SIGN */
    }
}

/* The view chain, collapsed OUTERMOST-IN, re-derived from the public row
 * table. `(((base >> s1) & m1) >> s2) & m2 == (base >> (s1+s2)) &
 * ((m1 >> s2) & m2)`, with a NEGATIVE shift meaning a left shift. */
static uint64_t fa_shr(uint64_t v, int s)
{
    if (s >= 64 || s <= -64) return 0ull;
    return (s >= 0) ? (v >> (unsigned)s) : (v << (unsigned)(-s));
}

static int fa_collapse(const cq_fadd_row *rows, int n, int s, int *shift,
                       uint64_t *mask)
{
    int guard = 0;

    *shift = 0;
    *mask  = ~0ull;
    while (s >= 0 && s < n && rows[s].op == CQ_FAOP_VIEW) {
        *mask   = fa_shr(rows[s].mask, *shift) & *mask;
        *shift += rows[s].shift;
        s       = rows[s].s0;
        CHECK(++guard <= n);
    }
    return s;
}

/* ONE PICKED OUTPUT IS ITSELF A VIEW AND THE SCAN HAS TO KNOW IT (D-K23-8).
 * `flushed_result = result_sign << 63` (softfloat_common.jl:183) is an operand
 * of nothing inside M32, so it has no span in the caller's region and
 * `cq_subnorm_flushed` ASSEMBLES it — 63 CQ_BIT_ZERO lanes and one live lane
 * at 63. Treating it as an ordinary scratch output (all 64 lanes quantum) is
 * what a reader assumes, and it was measured to slip the stream cursor by one
 * at the very first slot of `fadd.jl:125`'s mux: the `t` arm's lane 0 is ZERO,
 * so the gate folds away and the scan predicted a CX that never existed.
 * Every OTHER picked output — `wr`, `result_exp`, `normal_result`,
 * `overflow_result` and the four flags — is a real span and is all-quantum. */
static int fa_pick_is_flushed(const cq_fadd_row *rows, int n, int s)
{
    return s >= 0 && s < n && rows[s].op == CQ_FAOP_PICK
        && rows[s].shift == CQ_FA_PICK_FLUSHED
        && rows[rows[s].s0].op == CQ_FAOP_SUBNORM;
}

/* The KIND of lane `i` of operand code `s`, at the ALL-QUANTUM operand mask.
 * A rail lane and an emitting row's lane are CQ_BIT_Q (scratch is
 * pre-materialised under I6(b)); a constant lane is its own bit; a view's
 * vacated lane is CQ_BIT_ZERO. */
static int fa_lane(const cq_fadd_row *rows, int n, int s, int i)
{
    int shift, j, b;
    uint64_t mask;

    if (fa_pick_is_flushed(rows, n, s))
        return (i == CQ_FP64_W - 1)
             ? fa_lane(rows, n, rows[rows[s].s0].s2, 0) : CQ_BIT_ZERO;

    b = fa_collapse(rows, n, s, &shift, &mask);
    if (((mask >> (unsigned)i) & 1ull) == 0ull) return CQ_BIT_ZERO;
    j = i + shift;
    if (j < 0 || j >= CQ_FP64_W) return CQ_BIT_ZERO;
    if (b >= 0 || b == CQ_FA_A || b == CQ_FA_B) return CQ_BIT_Q;
    return ((fa_const(b) >> (unsigned)j) & 1ull) ? CQ_BIT_ONE : CQ_BIT_ZERO;
}

/* ---- (a1) The block costs, ASKED of their modules. Runs no kernel. ------ */

/* TWO CASES DELIBERATELY, so a sibling's cost moving reddens the FIRST and
 * NAMES the block rather than reddening the per-program identity with a number
 * nobody can attribute (K12.md §3.0's shape). This is the one that runs no
 * kernel. */
CQ_TEST(the_blocks_cost_what_their_modules_say_they_cost)
{
    /* ASKED of M12, M14, M16, M17, M31 and M32 and pinned against their own
     * measured W = 64 rows. K15 composes all six, so if one moves everything
     * below moves with it and this line says so first. */
    CHECK_EQ(cq_eq_steps(CQ_FP64_W),  317);          /* lower_eq!   5W-3   */
    CHECK_EQ(cq_ult_steps(CQ_FP64_W), 385);          /* lower_ult!  6W+1   */
    CHECK_EQ(cq_sub_steps(CQ_FP64_W), 447);          /* lower_sub!  7W-1   */
    CHECK_EQ(cq_add_steps(CQ_FP64_W), 318);          /* lower_add!  5W-2   */
    CHECK_EQ(cq_mux_steps(CQ_FP64_W), 256);          /* lower_mux!  4W     */
    CHECK_EQ(cq_barrel_steps(CQ_FP64_W, CQ_BARREL_SHL),  1921);
    CHECK_EQ(cq_barrel_steps(CQ_FP64_W, CQ_BARREL_LSHR), 1921);
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_NAN),  637);
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_INF),  639);
    CHECK_EQ(cq_fp_class_steps(CQ_FP_IS_ZERO), 639);
    CHECK_EQ(cq_clz_steps(),     7668);              /* M32, K23           */
    CHECK_EQ(cq_subnorm_steps(), 9165);
    CHECK_EQ(cq_round_steps(),   6287);

    /* The scratch contracts the layout budgets, each published by its owning
     * module since bd 9ve.19 added the missing `*_region` exports — so a
     * flat-scratch consumer never writes `2W-1` / `3W+1` / `3W` / `2W` down. */
    CHECK_EQ(cq_eq_region(CQ_FP64_W),  127);
    CHECK_EQ(cq_ult_region(CQ_FP64_W), 193);
    CHECK_EQ(cq_sub_region(CQ_FP64_W), 192);
    CHECK_EQ(cq_add_region(CQ_FP64_W), 128);
    CHECK_EQ(cq_mux_region(CQ_FP64_W), 128);
    CHECK_EQ(cq_barrel_region(CQ_FP64_W), 1216);
    CHECK_EQ(cq_fp_class_region(CQ_FP_IS_NAN),  256u);
    CHECK_EQ(cq_fp_class_region(CQ_FP_IS_INF),  257u);
    CHECK_EQ(cq_fp_class_region(CQ_FP_IS_ZERO), 257u);
    CHECK_EQ(cq_clz_region(),     3456u);
    CHECK_EQ(cq_subnorm_region(), 5253u);
    CHECK_EQ(cq_round_region(),   3082u);

    /* AND THE WIDTH IS 64, WHICH IS THE WHOLE OF THE NARROWING GUARD. The
     * exponent field is 11 bits, `d` is provably at most 2046 and the `:79`
     * barrel's result is discarded above 56; PRD-v2 §7.2 forbids narrowing an
     * intermediate towards any of those, so every block runs at the Julia
     * type's width. A port that narrowed would show up below as a program no
     * longer being the sum of its rows at 64. */
    CHECK(cq_eq_steps(CQ_FP64_EXP_W)  < cq_eq_steps(CQ_FP64_W));
    CHECK(cq_sub_steps(12)            < cq_sub_steps(CQ_FP64_W));
    CHECK(cq_barrel_steps(56, CQ_BARREL_LSHR)
          < cq_barrel_steps(CQ_FP64_W, CQ_BARREL_LSHR));
}

/* ---- (a2) Each program against what those blocks just cost. ------------- */

/* THE SLOT AND REGION COST OF ONE ROW, RE-DERIVED FROM THE PUBLIC TABLE AND
 * ASKED OF THE OWNING MODULE. Not one number is written down: this is the
 * independent copy of fadd_step.c's arithmetic, and the identity below is what
 * `CQOPS_UPDATE_GOLDENS=1` cannot bless. */
enum { FA_NOT1 = 2, FA_AND1 = 1, FA_OR1 = 3 };

static int fa_row_slots(const cq_fadd_row *r)
{
    switch (r->op) {
    case CQ_FAOP_VIEW: case CQ_FAOP_PICK: return 0;
    case CQ_FAOP_CLASS:   return cq_fp_class_steps((cq_fp_class)r->shift);
    case CQ_FAOP_EQ:      return cq_eq_steps(CQ_FP64_W);
    case CQ_FAOP_ULT:     return cq_ult_steps(CQ_FP64_W);
    case CQ_FAOP_SUB:     return cq_sub_steps(CQ_FP64_W);
    case CQ_FAOP_ADD:     return cq_add_steps(CQ_FP64_W);
    case CQ_FAOP_MUX:     return cq_mux_steps(CQ_FP64_W);
    case CQ_FAOP_AND:     return CQ_FP64_W;
    case CQ_FAOP_OR:      return 3 * CQ_FP64_W;
    case CQ_FAOP_XOR:     return 2 * CQ_FP64_W;
    case CQ_FAOP_BSHL:    return cq_barrel_steps(CQ_FP64_W, CQ_BARREL_SHL);
    case CQ_FAOP_BLSHR:   return cq_barrel_steps(CQ_FP64_W, CQ_BARREL_LSHR);
    case CQ_FAOP_NOT1:    return FA_NOT1;
    case CQ_FAOP_AND1:    return FA_AND1;
    case CQ_FAOP_OR1:     return FA_OR1;
    case CQ_FAOP_CLZ:     return cq_clz_steps();
    case CQ_FAOP_SUBNORM: return cq_subnorm_steps();
    default:              return cq_round_steps();
    }
}

static uint32_t fa_row_region(const cq_fadd_row *r)
{
    switch (r->op) {
    case CQ_FAOP_VIEW: case CQ_FAOP_PICK: return 0u;
    case CQ_FAOP_CLASS:   return cq_fp_class_region((cq_fp_class)r->shift);
    case CQ_FAOP_EQ:      return (uint32_t)cq_eq_region(CQ_FP64_W);
    case CQ_FAOP_ULT:     return (uint32_t)cq_ult_region(CQ_FP64_W);
    case CQ_FAOP_SUB:     return (uint32_t)cq_sub_region(CQ_FP64_W);
    case CQ_FAOP_ADD:     return (uint32_t)cq_add_region(CQ_FP64_W);
    case CQ_FAOP_MUX:     return (uint32_t)cq_mux_region(CQ_FP64_W);
    case CQ_FAOP_AND: case CQ_FAOP_OR: case CQ_FAOP_XOR:
        return (uint32_t)CQ_FP64_W;
    case CQ_FAOP_BSHL: case CQ_FAOP_BLSHR:
        return (uint32_t)cq_barrel_region(CQ_FP64_W);
    case CQ_FAOP_CLZ:     return cq_clz_region();
    case CQ_FAOP_SUBNORM: return cq_subnorm_region();
    case CQ_FAOP_ROUND:   return cq_round_region();
    default:              return 1u;      /* not1, and1, or1: one output bit */
    }
}

typedef struct { int slots; uint32_t region; int n; } cq_fa_probe;

static cq_fa_probe fa_probe(cq_fadd_prog p, cq_fadd_row *rows)
{
    cq_fa_probe t = { 0, 0u, 0 };

    t.n = cq_fadd_program(p, rows);
    for (int i = 0; i < t.n; i++) {
        t.slots  += fa_row_slots(&rows[i]);
        t.region += fa_row_region(&rows[i]);
    }
    return t;
}

CQ_TEST(each_program_is_the_sum_of_its_rows)
{
    cq_fadd_row add[CQ_FADD_MAX_ROWS], sub[CQ_FADD_MAX_ROWS];
    cq_fa_probe pa = fa_probe(CQ_FADD_PROG_ADD, add);
    cq_fa_probe ps = fa_probe(CQ_FADD_PROG_SUB, sub);
    cq_counter fwd, unc;

    /* THE COMPOSITION IDENTITY, against the ONE count M33 exports. Every `C_*`
     * is ASKED of its module and never written down; the row table is the
     * port's own content and the thing a reader checks against the Julia. */
    CHECK_EQ(cq_fsub_steps(),  ps.slots);
    CHECK_EQ(cq_fsub_region(), ps.region);

    /* `fadd` HAS NO EXPORTED COUNT — fptoui composes `fsub` and nothing
     * composes `fadd` (PRD-v2 §5's M39 row: "M33/M34 owe it nothing") — so its
     * identity is stated on the TABLE and confirmed on the STREAM. The table
     * half is that fsub is exactly fadd plus the nine-row prologue; the stream
     * half is the slot scan next door, which walks every one of fadd's rows
     * against the recorded gates and would run out of stream or leave gates
     * over if the library's count disagreed. */
    {
        int n_pre = 0, pre_slots = 0;
        const cq_fadd_row *pre = cq_fsub_prologue_rows(&n_pre);
        uint32_t pre_region = 0u;

        for (int i = 0; i < n_pre; i++) {
            pre_slots  += fa_row_slots(&pre[i]);
            pre_region += fa_row_region(&pre[i]);
        }
        CHECK_EQ(ps.slots,  pa.slots  + pre_slots);
        CHECK_EQ(ps.region, pa.region + pre_region);
        CHECK(pre_slots > 0 && pre_region > 0u);
    }

    /* SLOTS AND GATES COME APART, ASSERTED RATHER THAN NARRATED: the
     * sandwiched total is strictly below twice the slot count plus the
     * copy-out, because the views' constant lanes and the constant spans fold.
     * An implementation that had materialised its constants (upstream's
     * `resolve!` shape, §7.3's refused alternative) would make this an
     * equality. */
    for (int i = 0; i < N_FADD_ROWS; i++) {
        cq_fa_probe t = (i == 0) ? pa : ps;

        cq_kd_measure(&ROWS[i].spec, CQ_FP64_W, &fwd, &unc);
        CHECK(cq_count_total(&fwd)
              < 2u * (uint64_t)t.slots + (uint64_t)CQ_FP64_W);

        printf("# %-5s rows %3d  slots %6d  region %6u bits  forward "
               "(x %llu, cx %llu, ccx %llu) = %llu  unc = %llu\n",
               ROWS[i].spec.name, t.n, t.slots, t.region,
               (unsigned long long)fwd.x, (unsigned long long)fwd.cx,
               (unsigned long long)fwd.ccx,
               (unsigned long long)cq_count_total(&fwd),
               (unsigned long long)cq_count_total(&unc));
    }
    fflush(stdout);
}

/* ---- dst owns 64 qubits and the scratch comes back. -------------------- */

CQ_TEST(dst_owns_its_lanes_and_the_scratch_comes_back)
{
    for (int i = 0; i < N_FADD_ROWS; i++) {
        cq_fadd_row rows[CQ_FADD_MAX_ROWS];
        cq_fa_probe t = fa_probe(ROWS[i].prog, rows);
        uint32_t peak = 0u;
        uint32_t owned = cq_kd_peak(&ROWS[i].spec, CQ_FP64_W, &peak);

        CHECK_EQ(owned, (uint32_t)CQ_FP64_W);
        /* The whole program's region plus dst's 64 lanes. L2 looks after the
         * call and cannot see scratch that was taken and tidily released, so
         * the high-water mark is the only instrument for it. */
        CHECK_EQ(peak, t.region + (uint32_t)CQ_FP64_W);
    }
}
