/* tests/test_kernel_fpround_scan.inc — the FOUR-VALUED slot scan, with its own
 * stream cursor. Included by test_kernel_fpround_slots.inc.
 *
 * THE PREDICTION IS A KIND **OR NOTHING**, AND THE STREAM HAS ITS OWN CURSOR.
 * At the all-quantum mask every v1 kernel emits exactly one gate per slot, so
 * `slot index == stream index` and K12's scan walked them together. That is
 * false here: a control lane that is a constant ZERO makes the gate vanish and
 * a constant ONE demotes it (CCX->CX, CX->X). Both happen at EVERY mask,
 * because they come from the VIEWS and the CONSTANT SPANS rather than from the
 * inputs' bit-kinds. So the scan predicts NONE / X / CX / CCX from the lane
 * table alone and advances the stream cursor only on a non-NONE. Deleting the
 * constant rows from the prediction turns the instrument off rather than
 * simplifying it.
 *
 * WHAT IT SHARES WITH THE MACHINE, SAID PLAINLY, because an oracle sharing a
 * constant is blind to that constant. It reads the ROW TABLE — which is the
 * port, checked by a reader against the Julia and pinned by fpround.c's
 * `_Static_assert` on the four row counts — and it asks each module for its
 * step count, exactly as `the_blocks_cost_what_their_modules_say_they_cost`
 * does and for the same reason (K18 §2.2: reusing a shipped block's schedule
 * is the whole point of composing it). What it does NOT share is
 * fpround_step.c: the view composition, the operand resolution, the span
 * layouts and the dispatch are all re-derived below from the pinned
 * `lower_*!` loops. That is the half a boundary error lives in.
 *
 * THE SEVEN SCHEDULES, each from the pinned loop it ports:
 *   lower_eq!   arith.jl:424-447    lower_ult!  arith.jl:449-463
 *   lower_slt!  arith.jl:465-472    lower_sub!  adder.jl:148-172
 *   lower_add!  adder.jl:1-18       lower_mux!  arith.jl:522-532
 *   lower_var_shl!/lshr!  arith.jl:350-380, plus lower_and!/or!/not1! at
 *   :268-272, :274-282, :474-478.
 * `lower_eq!`, `lower_ult!`, `lower_sub!` and `lower_mux!` already exist in
 * tests/test_kernel_fcmp_slots.inc and tests/test_kernel_divrem_schedule.inc,
 * at two different spellings; `slt`, `add`, the barrel and the three bitwise
 * ops are new here. A SHARED HOME FOR ALL TEN IS OWED and is filed rather than
 * taken in this bead: K17, K16, K15 and K20 all need the same set, and a
 * second transcription of a schedule is a second chance at the same
 * off-by-one (K23 §2.3 note 1).
 */

enum { FPR_NONE = -1 };

_Static_assert(CQ_OP_X == 0 && CQ_OP_CX == 1 && CQ_OP_CCX == 2,
               "the scan demotes a gate by decrementing its op");

/* The constant patterns, transcribed from the Julia literals rather than read
 * out of fpround_step.c's `const_of` — an oracle sharing a constant with the
 * code is blind to exactly that constant. */
static uint64_t fpr_scan_const(int s)
{
    switch (s) {
    case CQ_FR_K_ZERO:     return UINT64_C(0);          /* :72, :102, …      */
    case CQ_FR_K_ONE:      return UINT64_C(1);          /* :176, :180        */
    case CQ_FR_K_FOUR:     return UINT64_C(4);          /* :210              */
    case CQ_FR_K_56:       return UINT64_C(56);         /* :177              */
    case CQ_FR_K_63:       return UINT64_C(63);         /* :178              */
    case CQ_FR_K_7FE:      return UINT64_C(0x7FE);      /* :223              */
    case CQ_FR_K_7FF:      return UINT64_C(0x7FF);      /* :200, :220        */
    case CQ_FR_K_D32:      return UINT64_C(32);         /* :78,  :116        */
    case CQ_FR_K_D16:      return UINT64_C(16);         /* :82,  :120        */
    case CQ_FR_K_D8:       return UINT64_C(8);          /* :86,  :124        */
    case CQ_FR_K_D4:       return UINT64_C(4);          /* :90,  :128        */
    case CQ_FR_K_D2:       return UINT64_C(2);          /* :94,  :132        */
    case CQ_FR_K_D1:       return UINT64_C(1);          /* :98,  :136        */
    case CQ_FR_K_IMPLICIT: return UINT64_C(0x0010000000000000);   /* :9      */
    default:               return UINT64_C(0x7FF0000000000000);   /* :11 INF */
    }
}

static uint64_t fpr_scan_shr(uint64_t v, int s)
{
    if (s >= 64 || s <= -64) return UINT64_C(0);
    return (s >= 0) ? (v >> (unsigned)s) : (v << (unsigned)(-s));
}

/* The KIND of lane `i` of operand `s`, at the ALL-QUANTUM input mask. A row's
 * output is scratch and is CQ_BIT_Q from step 0 under I6(b); an input lane is
 * Q at this mask; a constant lane is its own bit; and a view is its base's
 * lane through the collapsed (shift, mask). */
static int fpr_lane(const cq_fpround_row *rows, int n, int s, int i)
{
    int shift = 0, guard = 0, j;
    uint64_t mask = ~UINT64_C(0);

    while (s >= 0 && s < n && rows[s].op == CQ_FROP_VIEW && guard++ <= n) {
        mask   = fpr_scan_shr(rows[s].mask, shift) & mask;
        shift += rows[s].shift;
        s      = rows[s].s0;
    }
    if (((mask >> (unsigned)i) & UINT64_C(1)) == UINT64_C(0)) return CQ_BIT_ZERO;

    j = i + shift;
    if (j < 0 || j >= W64) return CQ_BIT_ZERO;
    if (s >= 0)            return CQ_BIT_Q;     /* a row's own scratch output */
    if (s >= CQ_FR_IN2)    return CQ_BIT_Q;     /* the block's inputs, all Q  */
    return ((fpr_scan_const(s) >> (unsigned)j) & UINT64_C(1)) ? CQ_BIT_ONE
                                                             : CQ_BIT_ZERO;
}

/* PRD §3's fold table, over one gate's controls. */
static int fpr_fold(int kind, const int *ctrl, int nc, int *fold, int *dem)
{
    for (int i = 0; i < nc; i++) {
        if (ctrl[i] == CQ_BIT_ZERO) { if (fold) (*fold)++; return FPR_NONE; }
        if (ctrl[i] == CQ_BIT_ONE)  { kind--; if (dem) (*dem)++; }
    }
    return kind;
}

typedef struct { const cq_fpround_row *rows; int n, *fold, *dem; } fpr_scan;

#define LANE(s, i) fpr_lane(sc->rows, sc->n, (s), (i))
#define FOLD1(k, c) fpr_fold((k), (int[1]){ (c) }, 1, sc->fold, sc->dem)
#define FOLD2(k, c0, c1) fpr_fold((k), (int[2]){ (c0), (c1) }, 2, sc->fold, sc->dem)

/* `lower_eq!`: 2W slots of CX building `diff` — the only place an operand lane
 * is a control — then a 3-cycle CX, CX, CCX over the OR-prefix, whose controls
 * are all scratch and so cannot fold. */
static int fpr_sched_eq(const fpr_scan *sc, int a, int b, int v)
{
    if (v < 2 * W64)
        return FOLD1(CQ_OP_CX, LANE((v % 2 == 0) ? a : b, v / 2));
    return ((v - 2 * W64) % 3 == 2) ? CQ_OP_CCX : CQ_OP_CX;
}

/* `lower_ult!`: 2W alternating CX/X building `~b`, the carry seed, then a
 * 4-cycle per lane. `nb`, `axnb` and `carry` are scratch. */
static int fpr_sched_ult(const fpr_scan *sc, int a, int b, int v)
{
    int u, i;

    if (v < 2 * W64)
        return (v % 2 == 0) ? FOLD1(CQ_OP_CX, LANE(b, v / 2)) : CQ_OP_X;
    if (v == 2 * W64) return CQ_OP_X;                    /* carry_in = 1 */

    u = v - (2 * W64 + 1);
    i = u / 4;
    switch (u % 4) {
    case 0:  return FOLD1(CQ_OP_CX, LANE(a, i));
    case 1:  return CQ_OP_CX;                            /* nb[i] -> axnb[i] */
    case 2:  return FOLD2(CQ_OP_CCX, LANE(a, i), CQ_BIT_Q);
    default: return CQ_OP_CCX;
    }
}

/* `lower_slt!`: the two biased copies, the two MSB flips, then `lower_ult!`
 * over `af`/`bf` — which are SCRATCH, so the inner comparator never folds. */
static int fpr_sched_slt(const fpr_scan *sc, int a, int b, int v)
{
    if (v < 2 * W64)
        return FOLD1(CQ_OP_CX, LANE((v % 2 == 0) ? a : b, v / 2));
    if (v < 2 * W64 + 2) return CQ_OP_X;
    {
        int u = v - (2 * W64 + 2), i;

        if (u < 2 * W64) return (u % 2 == 0) ? CQ_OP_CX : CQ_OP_X;
        if (u == 2 * W64) return CQ_OP_X;
        u -= 2 * W64 + 1;
        i = u % 4;
        return (i == 2 || i == 3) ? CQ_OP_CCX : CQ_OP_CX;
    }
}

/* `lower_add!`: five per lane, three on the top one — Bennett's own `i < W`
 * guard drops its two Toffolis. */
static int fpr_sched_add(const fpr_scan *sc, int a, int b, int v)
{
    int i;

    if (v >= 5 * (W64 - 1))
        switch (v - 5 * (W64 - 1)) {
        case 0:  return FOLD1(CQ_OP_CX, LANE(a, W64 - 1));
        case 1:  return FOLD1(CQ_OP_CX, LANE(b, W64 - 1));
        default: return CQ_OP_CX;
        }
    i = v / 5;
    switch (v % 5) {
    case 0:  return FOLD1(CQ_OP_CX, LANE(a, i));
    case 1:  return FOLD1(CQ_OP_CX, LANE(b, i));
    case 2:  return FOLD2(CQ_OP_CCX, LANE(a, i), LANE(b, i));
    case 3:  return CQ_OP_CCX;
    default: return CQ_OP_CX;
    }
}

/* `lower_sub!`: the same 2W complement and seed, then `lower_add!`'s ripple
 * with `nb` — scratch — in place of `b`. */
static int fpr_sched_sub(const fpr_scan *sc, int a, int b, int v)
{
    int u, i;

    if (v < 2 * W64)
        return (v % 2 == 0) ? FOLD1(CQ_OP_CX, LANE(b, v / 2)) : CQ_OP_X;
    if (v == 2 * W64) return CQ_OP_X;

    u = v - (2 * W64 + 1);
    if (u >= 5 * (W64 - 1))
        return (u - 5 * (W64 - 1) == 0) ? FOLD1(CQ_OP_CX, LANE(a, W64 - 1))
                                        : CQ_OP_CX;
    i = u / 5;
    switch (u % 5) {
    case 0:  return FOLD1(CQ_OP_CX, LANE(a, i));
    case 1:  return CQ_OP_CX;                            /* nb[i] -> d[i] */
    case 2:  return FOLD2(CQ_OP_CCX, LANE(a, i), CQ_BIT_Q);
    case 3:  return CQ_OP_CCX;
    default: return CQ_OP_CX;
    }
}

/* `lower_mux!`: CX(f), CX(t), CX(f), CCX(cond, d). `cond` is a Bool ROW here
 * and so is always scratch; `t` and `f` are the folding operands. */
static int fpr_sched_mux(const fpr_scan *sc, int t, int f, int v)
{
    int i = v / 4;

    switch (v % 4) {
    case 0:  return FOLD1(CQ_OP_CX, LANE(f, i));
    case 1:  return FOLD1(CQ_OP_CX, LANE(t, i));
    case 2:  return FOLD1(CQ_OP_CX, LANE(f, i));
    default: return FOLD2(CQ_OP_CCX, CQ_BIT_Q, CQ_BIT_Q);
    }
}

/* `lower_var_shl!` / `lower_var_lshr!`: W INIT CNOTs off `a`, then per stage
 * `W - 2^j` shuffle CNOTs off SCRATCH and 4W mux slots whose Toffoli is
 * controlled by lane `j` of the AMOUNT — the only operand lane the stage
 * loop reads. */
static int fpr_sched_barrel(const fpr_scan *sc, int a, int b, int v)
{
    if (v < W64) return FOLD1(CQ_OP_CX, LANE(a, v));
    v -= W64;
    for (int j = 0; j < 6; j++) {
        int ns = W64 - (1 << j);

        if (v < ns) return CQ_OP_CX;                     /* cur is scratch */
        v -= ns;
        if (v < 4 * W64)
            return (v % 4 == 3) ? FOLD2(CQ_OP_CCX, LANE(b, j), CQ_BIT_Q)
                                : CQ_OP_CX;
        v -= 4 * W64;
    }
    return CQ_OP_CX;                                     /* unreachable */
}

static int fpr_scan_slot(const fpr_scan *sc, int i, int v)
{
    const cq_fpround_row *r = &sc->rows[i];

    switch (r->op) {
    case CQ_FROP_EQ:    return fpr_sched_eq (sc, r->s0, r->s1, v);
    case CQ_FROP_ULT:   return fpr_sched_ult(sc, r->s0, r->s1, v);
    case CQ_FROP_SLT:   return fpr_sched_slt(sc, r->s0, r->s1, v);
    case CQ_FROP_SUB:   return fpr_sched_sub(sc, r->s0, r->s1, v);
    case CQ_FROP_ADD:   return fpr_sched_add(sc, r->s0, r->s1, v);
    case CQ_FROP_MUX:   return fpr_sched_mux(sc, r->s1, r->s2, v);
    case CQ_FROP_BSHL: case CQ_FROP_BLSHR:
                        return fpr_sched_barrel(sc, r->s0, r->s1, v);
    case CQ_FROP_AND:   return FOLD2(CQ_OP_CCX, LANE(r->s0, v), LANE(r->s1, v));
    case CQ_FROP_OR: {
        int lane = v / 3;

        if (v % 3 == 0) return FOLD1(CQ_OP_CX, LANE(r->s0, lane));
        if (v % 3 == 1) return FOLD1(CQ_OP_CX, LANE(r->s1, lane));
        return FOLD2(CQ_OP_CCX, LANE(r->s0, lane), LANE(r->s1, lane)); }
    case CQ_FROP_NOT1:  return (v == 0) ? CQ_OP_CX : CQ_OP_X;
    case CQ_FROP_AND1:  return CQ_OP_CCX;
    default:            return (v == 2) ? CQ_OP_CCX : CQ_OP_CX;   /* or(1) */
    }
}

/* The same prediction with no counters, for the span scan next door, which
 * needs to know WHICH slots emit and not how many folded. */
static int fpr_scan_slot_quiet(const cq_fpround_row *rows, int n, int i, int v)
{
    fpr_scan sc;

    sc.rows = rows; sc.n = n; sc.fold = NULL; sc.dem = NULL;
    return fpr_scan_slot(&sc, i, v);
}

CQ_TEST(the_slot_boundaries_match_an_independent_four_valued_scan)
{
    cq_mock m;
    static const int Q[3] = { 1, 1, 1 };
    int any_folded = 0, any_demoted = 0;

    cq_mock_init(&m);

    for (int r = 0; r < N_ROWS; r++) {
        cq_fpround_id id = ROWS[r].id;
        cq_sink s = cq_mock_sink(&m);
        fpr_scan sc;
        uint64_t v[3];
        size_t cursor = 0u;
        int folded = 0, demoted = 0, bad = 0;

        /* An anchor rather than |0>: a scan that never looked at a live
         * circuit would happily pin the shape of a wrong one. Anchor 0 of each
         * block is a real input, and the VALUE is irrelevant to the prediction
         * because the fold table reads a bit's KIND and never a qubit's value
         * (D6, no demotion). */
        (void)fpr_anchor_at(id, 0, v);
        sc.rows = cq_fpround_rows(id, &sc.n);
        sc.fold = &folded;
        sc.dem  = &demoted;

        fpr_open(&FX, id, v, Q, 0u, &s);
        cq_mock_reset(&m);
        fpr_forward(&FX);

        for (int i = 0; i < sc.n && !bad; i++)
            for (int u = 0; u < fpr_row_slots(sc.rows[i].op) && !bad; u++) {
                int pr = fpr_scan_slot(&sc, i, u);
                cq_op got;

                if (pr == FPR_NONE) continue;
                if (cursor >= cq_mock_count(&m)) {
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d slot %d: the stream ran out at %zu "
                              "gates", ROWS[r].name, i, u, cursor);
                    bad = 1;
                    break;
                }
                got = cq_mock_at(&m, cursor++)->op;
                if ((int)got != pr) {
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d (op %d) slot %d (stream %zu): emitted "
                              "op %d, the slot decode says %d", ROWS[r].name, i,
                              (int)sc.rows[i].op, u, cursor - 1u, (int)got, pr);
                    bad = 1;
                }
            }

        if (!bad) {
            /* THE SCAN IS NOT VACUOUS, and the claim is per helper and EXACT
             * rather than `demoted > 0` blanket — K22 §2.3 measured that a
             * blanket inequality goes red on the classes whose constant is
             * zero. Every helper has both: a CLZ stage's mask view leaves
             * `64 - popcount(mask)` lanes at ZERO and every `== 0` compares
             * against the all-zero span, while `K_ONE`, `K_56`, `K_63`,
             * `K_7FE`, `K_7FF`, the six decrements and `K_IMPLICIT` all carry
             * set lanes that DEMOTE a CX to an X. */
            CHECK(folded  > 0);
            CHECK(demoted > 0);
            CHECK_EQ((long long)cq_mock_count(&m), (long long)cursor);
            any_folded  += folded;
            any_demoted += demoted;
            printf("# scan %-8s slots %5d  emitted %5zu  folded %5d  "
                   "demoted %4d\n", ROWS[r].name, fpr_steps_of(id),
                   cq_mock_count(&m), folded, demoted);
        }
        fpr_close(&FX);
    }

    CHECK(any_folded  > 0);
    CHECK(any_demoted > 0);
    fflush(stdout);
    cq_mock_dispose(&m);
}

#undef LANE
#undef FOLD1
#undef FOLD2
