/* tests/test_kernel_fpfield_slots.inc — the composition check and the slot
 * scan, both written BEFORE the kernel. Included by test_kernel_fpfield.c.
 *
 * K11's RECORDED FINDING, APPLIED IN ADVANCE. The one mutant L1 cannot see is
 * the one that looks like an optimisation, and L4 is not a durable detector
 * because `CQOPS_UPDATE_GOLDENS=1` blesses a narrowing as readily as a fix.
 * M31 offers that trade twice over and both offers are tempting: run the `eq`
 * block at W = 11 over the raw exponent field instead of at 64 over the view
 * (~5x fewer slots), or drop the `eq` block's constant operand and compare
 * against nothing. Neither changes a value, a palindrome or the pool. What
 * catches them is here, and NOTHING BELOW READS A GOLDEN.
 *
 * AND THE IDENTITY IS OVER SLOTS, NOT GATES — D-K18-6, which K18 wrote down
 * and M31 is the first module to need. A block operand is a view with 53 or 12
 * constant lanes, and the other operand is a constant span, so the §3 fold
 * table elides gates at EVERY operand mask including the all-quantum one L4
 * pins. `compute = Σ blocks` is true of slots and false of gates.
 */

/* ---- (b1) The block cost, asked of M16 and pinned against its own doc. --- */

/* TWO CASES DELIBERATELY, so a sibling's cost moving reddens the FIRST and
 * names the block rather than reddening the per-predicate identity with a
 * number nobody can attribute. This is the one that runs no kernel. */
CQ_TEST(the_eq_block_costs_what_m16_says_it_costs)
{
    /* ASKED of M16 (`cq_eq_steps`), compared against K09.md §7.3's MEASURED
     * W = 64 row. M31 composes this block twice per predicate, so if it moves,
     * everything below moves with it and this line says so first. */
    CHECK_EQ(cq_eq_steps(CQ_FP64_W), 317);

    /* cmp.h's scratch contract: a consumer's layout budgets `2W - 1` for
     * `diff ++ orr`. M31 budgets exactly that, twice. */
    CHECK_EQ(2 * CQ_FP64_W - 1, 127);

    /* AND THE WIDTH IS 64, WHICH IS THE WHOLE OF THE NARROWING GUARD. The
     * exponent field is 11 bits and the fraction 52; PRD-v2 §7.2 forbids
     * narrowing an intermediate towards the field width, so the block runs at
     * the Julia type's width. A port that ran it at 11 would show up here as
     * `cq_fp_class_steps` no longer being 2 x this number. */
    CHECK(cq_eq_steps(CQ_FP64_EXP_W) < cq_eq_steps(CQ_FP64_W));
}

/* ---- (b2) Each predicate against what those blocks just cost. ----------- */

/* THE FOUR ROWS, TRANSCRIBED A SECOND TIME AND INDEPENDENTLY. fpclass.c has
 * its own copy; this one is read straight off the pinned Julia and shares
 * nothing with it but the source it came from. That is deliberate — the K9
 * precedent is `cq_ref_icmp`, which transcribes `lower_icmp!`'s ten rows again
 * rather than calling the kernel's own table.
 *
 *   is_nan        (ea == 0x7FF) & (fa != 0)      fadd.jl:29
 *   is_inf        (ea == 0x7FF) & (fa == 0)      fadd.jl:31
 *   is_zero       (ea == 0)     & (fa == 0)      fadd.jl:33
 *   is_subnormal  (ea == 0)     & (fa != 0)      flog.jl:262
 */
typedef struct { uint64_t ce; int eq_e; uint64_t cf; int eq_f; } fp_srow;

static const fp_srow FP_SROWS[N_FP_ROWS] = {
    { CQ_FP64_EXP_ALL, 1, 0u, 0 },
    { CQ_FP64_EXP_ALL, 1, 0u, 1 },
    { 0u,              1, 0u, 1 },
    { 0u,              1, 0u, 0 }
};

/* `lower_not1!` is `CNOT(w, r); NOT(r)` into a fresh wire — two slots, one bit
 * (arith.jl:474-478). `lower_and!` is one Toffoli per lane — one slot at one
 * lane, and no bit of its own (arith.jl:268-272). Transcribed here from the
 * pinned source, as K18 §2.2 does: they are emitted inline by the fp step
 * function and no module publishes a cost for them. */
enum { SC_NOT1 = 2, SC_AND1 = 1 };

CQ_TEST(each_predicate_is_two_eq_blocks_a_not1_per_eq_and_one_and)
{
    for (int i = 0; i < N_FP_ROWS; i++) {
        const fp_srow *r = &FP_SROWS[ROWS[i].cls];
        int n_not1 = r->eq_e + r->eq_f;
        cq_counter fwd, unc;

        /* THE COMPOSITION IDENTITY. `cq_eq_steps(64)` is ASKED of M16 and is
         * never written down; the `!=` / `==` column is the table above, which
         * is the port's own content and the thing a reader must be able to
         * check against the Julia. */
        CHECK_EQ(cq_fp_class_steps(ROWS[i].cls),
                 2 * cq_eq_steps(CQ_FP64_W) + n_not1 * SC_NOT1 + SC_AND1);

        /* The region is the two blocks' internals, one bit per `lower_not1!`,
         * and the flag. cq_kd_peak measures the same number through the pool
         * in the main file, so this is the DECLARED half of a pair. */
        CHECK_EQ(cq_fp_class_region(ROWS[i].cls),
                 (uint32_t)(2 * (2 * CQ_FP64_W - 1) + n_not1 + 1));

        /* SLOTS AND GATES COME APART, ASSERTED RATHER THAN NARRATED: the
         * sandwiched gate total is strictly below twice the slot count plus
         * the copy-out, because the views' constant lanes and the constant
         * spans fold. An implementation that had materialised its constants
         * (upstream's `resolve!` shape, PRD-v2 §7.3's refused alternative)
         * would make this an equality. */
        cq_kd_measure(&ROWS[i].spec, CQ_FP64_W, &fwd, &unc);
        CHECK(cq_count_total(&fwd)
              < 2u * (uint64_t)cq_fp_class_steps(ROWS[i].cls) + 1u);

        printf("# %-16s slots %4d  forward (x %llu, cx %llu, ccx %llu) = %llu  "
               "unc (x %llu, cx %llu, ccx %llu) = %llu  region %u bits\n",
               ROWS[i].spec.name, cq_fp_class_steps(ROWS[i].cls),
               (unsigned long long)fwd.x, (unsigned long long)fwd.cx,
               (unsigned long long)fwd.ccx,
               (unsigned long long)cq_count_total(&fwd),
               (unsigned long long)unc.x, (unsigned long long)unc.cx,
               (unsigned long long)unc.ccx,
               (unsigned long long)cq_count_total(&unc),
               cq_fp_class_region(ROWS[i].cls));
    }
    fflush(stdout);
}

/* ---- (b3) The four-valued slot scan. ------------------------------------ */

/* THE PREDICTION IS A KIND **OR NOTHING**, AND THE STREAM HAS ITS OWN CURSOR.
 * At the all-quantum mask every v1 kernel emits exactly one gate per slot, so
 * `slot index == stream index` and K12's scan could walk them together. That
 * is false here: a control lane that is a constant ZERO makes the gate vanish
 * and a constant ONE demotes it (CCX->CX, CX->X). Both happen at every mask,
 * because they come from the VIEWS and the CONSTANT SPANS rather than from the
 * operand's bit-kinds. So the scan predicts NONE / X / CX / CCX from K22.md
 * §2.1's lane table alone and advances the stream cursor only on a non-NONE. */
enum { PRED_NONE = -1 };

/* At the mask this scan runs at, `a` is all-quantum, so a view's lane is Q
 * inside the field and CQ_BIT_ZERO in the fill. */
static int scan_view_lane(int side, int i)
{
    int n = (side == 0) ? CQ_FP64_EXP_W : CQ_FP64_FRAC_W;

    return (i < n) ? CQ_BIT_Q : CQ_BIT_ZERO;
}

static int scan_const_lane(uint64_t pattern, int i)
{
    return ((pattern >> i) & 1u) ? CQ_BIT_ONE : CQ_BIT_ZERO;
}

/* PRD §3's fold table, over one gate's controls. CQ_OP_X / CX / CCX are 0 / 1
 * / 2, so "demote" is a decrement — pinned by the assert below rather than
 * left as a comment, on CQ_BIT_ZERO's precedent. */
_Static_assert(CQ_OP_X == 0 && CQ_OP_CX == 1 && CQ_OP_CCX == 2,
               "the scan demotes a gate by decrementing its op");

static int scan_fold(int kind, const int *ctrl, int nc, int *demoted)
{
    for (int i = 0; i < nc; i++) {
        if (ctrl[i] == CQ_BIT_ZERO) return PRED_NONE;
        if (ctrl[i] == CQ_BIT_ONE) { kind--; if (demoted) (*demoted)++; }
    }
    return kind;
}

/* `lower_eq!`'s own schedule (arith.jl:424-447): 2n slots of CX building
 * `diff`, then a 3-cycle CX, CX, CCX over the OR-prefix. Phase B's controls
 * are all scratch and so are all CQ_BIT_Q — nothing there can fold. */
static int scan_eq(int side, uint64_t konst, int v, int *demoted)
{
    if (v < 2 * CQ_FP64_W) {
        int i = v / 2;
        int c[1];

        c[0] = (v % 2 == 0) ? scan_view_lane(side, i)
                            : scan_const_lane(konst, i);
        return scan_fold(CQ_OP_CX, c, 1, demoted);
    }
    return ((v - 2 * CQ_FP64_W) % 3 == 2) ? CQ_OP_CCX : CQ_OP_CX;
}

/* The boundaries, from the row table and `cq_eq_steps` alone — never from a
 * closed form and never from the kernel. */
static int scan_slot(cq_fp_class cls, int u, int *demoted)
{
    const fp_srow *r = &FP_SROWS[cls];
    int C = cq_eq_steps(CQ_FP64_W);

    for (int side = 0; side <= 1; side++) {
        if (u < C) return scan_eq(side, side == 0 ? r->ce : r->cf, u, demoted);
        u -= C;

        if (side == 0 ? r->eq_e : r->eq_f) {
            if (u < SC_NOT1) return (u == 0) ? CQ_OP_CX : CQ_OP_X;
            u -= SC_NOT1;
        }
    }
    return CQ_OP_CCX;                      /* `&` — one Toffoli into the flag */
}

/* Which of the five spans slot `u` writes into. Same walk as scan_slot, and
 * deliberately a SECOND function rather than an out-parameter: this one is
 * about the LAYOUT and that one is about the fold table, and only one of them
 * has anything to say about K22.md §2.2's block table. */
enum { SPAN_EQ_E = 0, SPAN_NE_E, SPAN_EQ_F, SPAN_NE_F, SPAN_FLAG, N_SPANS };

static int scan_span(cq_fp_class cls, int u)
{
    const fp_srow *r = &FP_SROWS[cls];
    int C = cq_eq_steps(CQ_FP64_W);

    for (int side = 0; side <= 1; side++) {
        if (u < C) return side == 0 ? SPAN_EQ_E : SPAN_EQ_F;
        u -= C;

        if (side == 0 ? r->eq_e : r->eq_f) {
            if (u < SC_NOT1) return side == 0 ? SPAN_NE_E : SPAN_NE_F;
            u -= SC_NOT1;
        }
    }
    return SPAN_FLAG;
}

/* One forward call at the ALL-QUANTUM mask, recorded. The value is checked on
 * the way through: a scan that never looked at the answer would happily pin
 * the shape of a wrong circuit. */
static void fp_run(const fp_krow *r, uint64_t bits, cq_ref_w qm, cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_kd_shape sh;
    cq_ref_w v = cq_ref_w_make(bits, 0u, CQ_FP64_W);
    uint64_t want = (uint64_t)host_class(bits, r->cls);
    int32_t ha, hd;
    const cq_bit *src[1];

    fp_shape(CQ_FP64_W, &sh);
    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);

    ha = cq_bk_reg_w(&ctx, (uint32_t)CQ_FP64_W, v, qm);
    hd = cq_reg_alloc_zero(&ctx.regs, 1u);
    src[0] = cq_reg_cbits(&ctx.regs, ha);

    cq_mock_reset(m);                  /* drop the rail's materialising Xs */
    r->spec.call(&ctx, cq_reg_bits(&ctx.regs, hd), src, &sh);

    if (cq_pc_value(&ctx, hd) != want)
        cq_h_fail(__FILE__, __LINE__,
                  "%s(0x%016llx): dst = %llu, the host says %llu",
                  r->spec.name, (unsigned long long)bits,
                  (unsigned long long)cq_pc_value(&ctx, hd),
                  (unsigned long long)want);

    cq_ctx_dispose(&ctx);
}

static int scan_popcount(uint64_t v)
{
    int n = 0;

    for (int i = 0; i < CQ_FP64_W; i++) n += (int)((v >> i) & 1u);
    return n;
}

CQ_TEST(the_slot_boundaries_match_an_independent_four_valued_scan)
{
    cq_mock m;
    int any_demoted = 0;

    cq_mock_init(&m);

    for (int i = 0; i < N_FP_ROWS; i++) {
        const fp_srow *r = &FP_SROWS[ROWS[i].cls];
        int n = cq_fp_class_steps(ROWS[i].cls);
        size_t cursor = 0u;
        int folded = 0, demoted = 0, bad = 0;

        /* A payload NaN: every one of the four predicates has a determinate
         * answer on it, and none is 0 for all four, so the copy-out below is
         * reached with a live flag somewhere in the set. */
        fp_run(&ROWS[i], CQ_F64_QNAN_B, cq_ref_w_ones(CQ_FP64_W), &m);

        for (int s = 0; s < n && !bad; s++) {
            int want = scan_slot(ROWS[i].cls, s, &demoted);
            cq_op got;

            if (want == PRED_NONE) { folded++; continue; }

            if (cursor >= cq_mock_count(&m)) {
                cq_h_fail(__FILE__, __LINE__,
                          "%s slot %d: the stream ran out at %zu gates",
                          ROWS[i].spec.name, s, cursor);
                bad = 1;
                break;
            }

            got = cq_mock_at(&m, cursor++)->op;
            if ((int)got != want) {
                cq_h_fail(__FILE__, __LINE__,
                          "%s slot %d (stream %zu): emitted op %d, the slot "
                          "decode says %d", ROWS[i].spec.name, s, cursor - 1u,
                          (int)got, want);
                bad = 1;
            }
        }
        if (bad) continue;

        /* THE SCAN IS NOT VACUOUS, AND THE TWO HALVES OF THE FOLD ROW ARE
         * ASSERTED SEPARATELY AND EXACTLY.
         *
         * `folded` is the ZERO half and is non-empty for every class: a view
         * fills 53 or 12 lanes with CQ_BIT_ZERO whatever the mask.
         *
         * `demoted` is the ONE half — a `CX` whose control is a constant ONE
         * becomes an `X`, one gate either way — and it is NOT non-empty for
         * every class, which is the thing a blanket `demoted > 0` got wrong
         * when this case was first written. Exactly one demotion happens per
         * SET lane of a constant operand, so `is_zero` and `is_subnormal`
         * (both constants zero) legitimately demote NOTHING. Pinned as the
         * popcount rather than as an inequality: it is the sharper claim and
         * it is the one that would notice a constant span silently becoming
         * all-zero, which is the cheapest way to break this module. */
        CHECK(folded > 0);
        CHECK_EQ(demoted, scan_popcount(r->ce) + scan_popcount(r->cf));
        any_demoted += demoted;

        /* The copy-out is exactly one CNOT — the "^=" of Rule 7's contract —
         * and the reverse half is the same `cursor` gates again. */
        CHECK_EQ((long long)cq_mock_count(&m), (long long)(2u * cursor + 1u));
        CHECK_EQ((int)cq_mock_at(&m, cursor)->op, (int)CQ_OP_CX);
        CHECK(cq_mock_is_palindrome(&m, cursor, 1u));
    }

    /* ACROSS THE FOUR CLASSES the ONE half of the fold row IS reached — the
     * claim the per-class popcount above cannot make on its own, and the one
     * that goes red if every constant operand were lost at once. */
    CHECK(any_demoted > 0);

    cq_mock_dispose(&m);
}

/* ---- (b4) The five spans are pairwise disjoint. ------------------------- */

/* WRITTEN BECAUSE A MEASURED MUTANT SURVIVED EVERYTHING ELSE. Collapsing both
 * `eq` blocks onto base 0 — so the fraction test overwrites the exponent
 * test's `diff` and `orr` — leaves the VALUE right, the palindrome intact, the
 * pool restored, the slot scan green and every gate count identical. Measured
 * in both configurations, 2026-09-18.
 *
 * AND IT IS EQUIVALENT FOR A REASON NO ASSERTION STATED, WHICH IS WHY IT NEEDS
 * ONE. `lower_eq!` leaves `diff` and `orr` ALL ZERO exactly when its own flag
 * is 0, i.e. when the operands were equal; and the `&` that closes every one of
 * the four rows masks the second conjunct away in precisely the other case. So
 * the second block sees a clean region whenever its answer can matter. That
 * accident is one edit from evaporating — a row spelled `|` instead of `&`, or
 * a consumer reading the second block's raw wire directly, and the same layout
 * is a silent miscompile.
 *
 * WHAT IS ASSERTED IS THE LAYOUT AND NOT THE ACCIDENT: no qubit index is
 * written by two different spans. It reads no golden, it is derived from
 * K22.md §2.2's block table rather than from the kernel, and it is the only
 * thing in this file that would notice two blocks sharing a region. */
CQ_TEST(the_blocks_five_spans_are_pairwise_disjoint)
{
    cq_mock m;

    cq_mock_init(&m);

    for (int i = 0; i < N_FP_ROWS; i++) {
        int n = cq_fp_class_steps(ROWS[i].cls);
        size_t cursor = 0u;
        int seen[N_SPANS];
        int owner[4096];
        int touched = 0;

        for (int s = 0; s < N_SPANS; s++) seen[s] = 0;
        for (int q = 0; q < 4096; q++) owner[q] = -1;

        fp_run(&ROWS[i], CQ_F64_QNAN_B, cq_ref_w_ones(CQ_FP64_W), &m);

        for (int s = 0; s < n; s++) {
            int dummy = 0;
            int span = scan_span(ROWS[i].cls, s);
            uint32_t t;

            if (scan_slot(ROWS[i].cls, s, &dummy) == PRED_NONE) continue;

            t = cq_mock_at(&m, cursor++)->t;
            CHECK(t < 4096u);
            seen[span] = 1;
            touched++;

            if (owner[t] >= 0 && owner[t] != span) {
                cq_h_fail(__FILE__, __LINE__,
                          "%s slot %d: q%u is written by span %d AND by span %d "
                          "— the two eq blocks, their not1 wires and the flag "
                          "must not share a bit", ROWS[i].spec.name, s, t,
                          owner[t], span);
                break;
            }
            owner[t] = span;
        }

        /* NOT VACUOUS: every span the class HAS must have been reached, or a
         * disjointness claim over spans that never ran is no claim at all. */
        CHECK(touched > 0);
        CHECK(seen[SPAN_EQ_E] && seen[SPAN_EQ_F] && seen[SPAN_FLAG]);
        CHECK_EQ(seen[SPAN_NE_E], FP_SROWS[ROWS[i].cls].eq_e);
        CHECK_EQ(seen[SPAN_NE_F], FP_SROWS[ROWS[i].cls].eq_f);
    }

    cq_mock_dispose(&m);
}
