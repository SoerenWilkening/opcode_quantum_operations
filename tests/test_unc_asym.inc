/* tests/test_unc_asym.inc — Step 21's headline: PRD §10's uncompute asymmetry,
 * MEASURED. Included by tests/test_unc.c, which quotes the §10 note in full.
 *
 * UNTIL THIS FILE THE ASYMMETRY WAS DOCUMENTED IN TWENTY-FIVE PLACES AND
 * EXERCISED BY NOTHING, and it could not have been otherwise: L4 measures at
 * the ALL-QUANTUM mask, which is the fixed point of the drift, so all 399
 * pinned second-pass pairs carry identical tuples and the `pass` column could
 * never have gone red. Risk R6 — "someone fixes the asymmetry by asserting
 * equality" — therefore had no detector at all.
 *
 * ---------------------------------------------------------------------------
 * ONE §7 CELL CAUSES IT, AND IT IS NOT THE ONE THE DOCUMENTS NAMED.
 *
 * PRD §10, CLAUDE.md's Rule 14 and K05/K06.md all SAID an in-place
 * "`cqrt_ry`/`cqrt_rz`" until this step, and K04.md said `cqrt_rz` outright,
 * which is flatly wrong. Only the `Ry` half survives M22 as shipped. Walking
 * §7's twelve cells (src/rotate.c):
 *
 *   IDENTITY / NEG_IDENTITY   — return; the bit is untouched
 *   HALF_TURN / NEG_HALF_TURN — cq_emit_x, whose constant row flips the
 *                               CONSTANT for zero gates and zero qubits
 *   Rz, constant column       — nothing at any phi, at all
 *   Rz, qubit column          — the bit is already a qubit
 *   GENERAL Ry                — cq_materialise, then cq_shadow_rotate
 *
 * So exactly one cell turns a constant into a wire, and it is the same block
 * that poisons the shadow (PRD §15 D12). `no_rz_at_any_angle_can_cause_the_
 * drift` below is that correction made executable. All four documents are
 * corrected; this paragraph is past tense on purpose.
 *
 * ---------------------------------------------------------------------------
 * WHICH IS WHY THE HEADLINE CASE NEEDS A SECOND ROUTE.
 *
 * The general `Ry` materialises AND poisons, and poison travels control ->
 * target through cq_shadow_cx/ccx (`t.unknown |= c.unknown`, with no clearing
 * path), so after the uncompute `dst` is poisoned even though every source
 * appeared only as a control. Nothing in this project can then read `dst`:
 * cq_pc_value fails by design, `rt_value` and cq_measure return 0 for a
 * poisoned bit as PRD §7's MEASUREMENT specifies (so an assertion built on
 * either would pass against a rail holding anything), and Rule 13 forbids a
 * simulator. The cancellation the asymmetry is supposed to preserve would be
 * unwitnessable.
 *
 * THE SECOND ROUTE IS THE OTHER WAY A SOURCE LANE BECOMES A WIRE: a rail
 * written by a self-inverse gate and un-written by re-applying it. That family
 * is the corpus's own — measured over the 239 goldens, 26,376 of 51,696 frees
 * (51%) are on rails last written that way — but the SHAPE this fixture uses is
 * narrower than that statistic, and the statistic must not be quoted for it.
 * The corpus figure is 19,153 `cqrt_toffoli` and 7,223 `cqrt_cnot`; the
 * CNOT-rooted share is 14%, the target is a one-bit flag rail rather than a
 * data lane, and there is not one pair of ADJACENT identical `cqrt_cnot` lines
 * anywhere in the 239 files. What the corpus actually ships is a bracket
 * re-applied in reverse with work in between —
 * `slice_control_select_compound.expected.log:6-8` builds a flag with
 * `cnot(h1,h3); cnot(h2,h3); toffoli(h1,h2,h3)`, `:15-17` strips it with the
 * same three reversed, and `:18` frees `h3`. `CX(p,s); CX(p,s)` is the two-gate
 * degenerate case of that, chosen because it is the SMALLEST perturbation that
 * drifts one lane and restores its value, not because the corpus spells it.
 *
 * The two routes are then a controlled experiment with one variable:
 *
 *   route     lane becomes a wire   shadow survives   uncompute stream
 *   general Ry        yes                 NO           <- identical ->
 *   CX pair           yes                 yes          <- identical ->
 *
 * `the_same_drift_without_the_poison_emits_the_identical_stream` asserts the
 * two uncompute streams are equal GATE FOR GATE INCLUDING OPERANDS, which
 * makes the CX route a legal oracle for the Ry route's cancellation: same
 * circuit, same physical input state, therefore same output — and the CX
 * route's output is provably zero. That is the only instrument this project
 * has on the poisoned surface.
 *
 * TWO PREMISES OF THAT ARGUMENT ARE ASSERTED RATHER THAN ASSUMED, because an
 * adversarial review found both unpinned and both cheap to break.
 *   (i) THE DRIFT IS AN INVERSE PAIR. `Ry(-0.7)Ry(0.7) = I` is a property of
 *       the ANGLES ALONE, and emission in src/ is a pure function of bit KINDS
 *       and qubit indices — so writing `0.7` twice leaves every kind, every
 *       index and every recorded gate byte-identical while the source is left
 *       in a genuine superposition and the XOR does not cancel. The drift
 *       window is therefore recorded and its two `ry` records compared,
 *       including the `angle` field, which is the one field the stream
 *       comparison omits.
 *   (ii) THE SOURCE'S VALUE IS RESTORED. The check for that used to be gated on
 *       `!lane_poisoned` — i.e. it ran on every route EXCEPT the one it was
 *       written for. It is now per-lane, skipping only the drifted lane, so the
 *       other W-1 lanes are verified on the Ry route too.
 *   The helper wire is minted on BOTH routes even though only one uses it: the
 *   comparison is exact only if every other register gets the same indices.
 *
 * ---------------------------------------------------------------------------
 * AND THE COST OF THE ASYMMETRY IS THE FREE. `bd 2cf`, open since 2026-08-15
 * and deferred on the premise that "nothing can rotate anything before M22
 * lands at Step 19" — M22 has landed. The Ry route leaves `dst` physically
 * |0> with no certificate, so cq_reg_free hard-errors on a rail that IS clean.
 * The predicate half is asserted here (clean == 0 on the Ry route, 1 on the CX
 * route, from an identical circuit); the abort is
 * tests/test_unc_death.c:free_of_a_rotation_tainted_uncomputed_rail. Neither
 * is a defect being tolerated — they PIN the current behaviour so that
 * resolving bd ckd.17b at Step 23 turns them red on purpose.
 *
 * THE EXTREME CASE IS `an_all_classical_forward_is_free_and_its_uncompute_is_
 * not`, and it is where the axis stops being a delta at all: on the row L5
 * pins, the forward costs ZERO gates and ZERO qubits, and after one drifted
 * lane the uncompute is the entire sandwich — so a rail that cost nothing
 * acquires wires it can never hand back.
 */

/* --- the round trip ------------------------------------------------------ */

typedef enum {
    UX_DRIFT_NONE = 0,   /* forward, nothing, uncompute                      */
    UX_DRIFT_RY,         /* general Ry(t) then Ry(-t): a wire, and poisoned  */
    UX_DRIFT_CX,         /* CX(p, lane) twice: a wire, shadow intact         */
    UX_DRIFT_RZ          /* Rz(phi) then Rz(-phi): §7 says nothing happens   */
} ux_drift;

enum { UX_DRIFT_CAP = 8, UX_STREAM_CAP = 4096 };

typedef struct {
    struct { uint64_t x, cx, ccx; }
               fwd, unc;      /* per-kind tuples, measured separately        */
    size_t     n_fwd, n_unc;  /* and the totals                              */
    int        lane_is_wire;  /* the perturbed lane after the perturbation   */
    int        lane_poisoned;
    int        dst_determinate;
    uint64_t   dst;
    int        clean;         /* cq_reg_clean with the rotation-free proof   */
    uint32_t   minted_fwd;    /* qubits the FORWARD took                     */
    uint32_t   minted_unc;    /* ...and the uncompute                        */
    uint32_t   dst_qubits;    /* wires dst owns at the end                   */
    size_t     n_drift;       /* the perturbation's own gates                */
    cq_rec     drift[UX_DRIFT_CAP];
} ux_rt;

/* Forward, perturb lane `j` of source `b`, uncompute. `qa` is `a`'s mask —
 * `~0ull` for the ordinary fixture, `0` for the all-classical row, which is a
 * different claim and not merely a different number. `stream`, when non-NULL,
 * receives the UNCOMPUTE's recorded gates. */
static void ux_round_trip(const ux_kernel *k, int W, uint64_t va, uint64_t vb,
                          uint64_t qa, int j, ux_drift drift, ux_rt *r,
                          cq_rec *stream, size_t *n_stream)
{
    ux_fix f;
    int wd = ux_wdst(k, W);

    memset(r, 0, sizeof *r);
    ux_open(&f);

    int32_t ha = cq_bk_reg(&f.ctx, (uint32_t)W, va, qa);
    int32_t hb = cq_bk_reg(&f.ctx, (uint32_t)W, vb, 0ull);   /* all classical */

    /* THE POKE WIRE IS MINTED ON EVERY ROUTE, INCLUDING THE ONES THAT DO NOT
     * TOUCH IT. It costs one qubit index, and spending it unconditionally is
     * what keeps `dst` and the drifted lane on the SAME indices across routes
     * — without which the gate-for-gate stream comparison below would compare
     * two correct circuits and report them different. */
    int32_t hp = cq_bk_reg(&f.ctx, 1u, 1u, 1u);
    int32_t hd = cq_reg_alloc_zero(&f.ctx.regs, (uint32_t)wd);

    cq_pc_snap s0 = cq_pc_take(&f.ctx);
    cq_mock_reset(&f.mock);
    k->fn(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
          cq_reg_cbits(&f.ctx.regs, ha), cq_reg_cbits(&f.ctx.regs, hb), W);
    r->n_fwd = cq_mock_count(&f.mock);
    r->fwd.x   = cq_mock_count_op(&f.mock, CQ_OP_X);
    r->fwd.cx  = cq_mock_count_op(&f.mock, CQ_OP_CX);
    r->fwd.ccx = cq_mock_count_op(&f.mock, CQ_OP_CCX);
    cq_pc_snap s1 = cq_pc_take(&f.ctx);
    r->minted_fwd = s1.minted - s0.minted;

    /* --- the perturbation, recorded so its own gates can be asserted ----- */
    cq_bit *bb = cq_reg_bits(&f.ctx.regs, hb);
    cq_mock_reset(&f.mock);
    switch (drift) {
    case UX_DRIFT_NONE:
        break;
    case UX_DRIFT_RY:
        /* 0.7 rad is CQ_ANGLE_GENERAL at every legal tolerance: §7's lattice
         * is pi (angle.c rounds theta/pi), and 0.7 is 0.7 rad from 0 and 2.44
         * from pi — some 223 times the widest legal window, tol_max * pi. */
        cq_rotate_ry_bit(&f.ctx, &bb[j],  0.7);
        cq_rotate_ry_bit(&f.ctx, &bb[j], -0.7);
        break;
    case UX_DRIFT_CX: {
        const cq_bit *p = cq_reg_cbits(&f.ctx.regs, hp);
        cq_emit_cx(&f.ctx, p, &bb[j]);
        cq_emit_cx(&f.ctx, p, &bb[j]);
        break;
    }
    case UX_DRIFT_RZ:
        cq_rotate_rz_bit(&f.ctx, &bb[j],  0.7);
        cq_rotate_rz_bit(&f.ctx, &bb[j], -0.7);
        break;
    }
    r->n_drift = cq_mock_count(&f.mock);
    if (r->n_drift > UX_DRIFT_CAP) {
        cq_h_fail(__FILE__, __LINE__,
                  "%s W=%d: the perturbation emitted %zu gates, over the %d "
                  "the drift buffer holds — a TRUNCATED record would let the "
                  "inverse-pair check pass on a pair it never saw",
                  k->name, W, r->n_drift, (int)UX_DRIFT_CAP);
        r->n_drift = 0u;
    } else {
        for (size_t i = 0; i < r->n_drift; i++)
            r->drift[i] = *cq_mock_at(&f.mock, i);
    }

    r->lane_is_wire = cq_bit_is_qubit(bb[j]);
    r->lane_poisoned = r->lane_is_wire &&
        cq_shadow_get(&f.ctx.shadow, cq_bit_qindex(bb[j])).unknown;

    /* THE SOURCE'S VALUE IS RESTORED, PER LANE, ON EVERY ROUTE — and per lane
     * rather than per register because ux_read reports the whole rail
     * indeterminate if ANY bit is poisoned, which on the Ry route is exactly
     * the route the claim is about. Lane `j` itself is unreadable there and is
     * skipped; the other W-1 are checked on all four routes. */
    for (int i = 0; i < W; i++) {
        int want = (int)((vb >> i) & 1u);
        int got;

        if (i == j && r->lane_poisoned) continue;
        if (cq_bit_is_const(bb[i])) got = cq_bit_value(bb[i]);
        else {
            cq_shadow sh = cq_shadow_get(&f.ctx.shadow, cq_bit_qindex(bb[i]));
            if (sh.unknown) {
                cq_h_fail(__FILE__, __LINE__,
                          "%s W=%d: source lane %d is poisoned and the drift "
                          "was on lane %d", k->name, W, i, j);
                continue;
            }
            got = sh.value != 0;
        }
        if (got != want)
            cq_h_fail(__FILE__, __LINE__,
                      "%s W=%d: the perturbation left source lane %d at %d, "
                      "not %d — `_unc` recomputes f from the LIVE sources, so "
                      "a route that changed one is measuring a different "
                      "FUNCTION rather than a different representation of the "
                      "same one", k->name, W, i, got, want);
    }

    cq_pc_snap s2 = cq_pc_take(&f.ctx);
    cq_mock_reset(&f.mock);
    k->fn(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
          cq_reg_cbits(&f.ctx.regs, ha), cq_reg_cbits(&f.ctx.regs, hb), W);
    r->n_unc = cq_mock_count(&f.mock);
    r->unc.x   = cq_mock_count_op(&f.mock, CQ_OP_X);
    r->unc.cx  = cq_mock_count_op(&f.mock, CQ_OP_CX);
    r->unc.ccx = cq_mock_count_op(&f.mock, CQ_OP_CCX);
    r->minted_unc = cq_pc_take(&f.ctx).minted - s2.minted;

    if (stream) {
        *n_stream = r->n_unc;
        if (r->n_unc > UX_STREAM_CAP) {
            cq_h_fail(__FILE__, __LINE__,
                      "%s W=%d: %zu gates will not fit the %d-record stream "
                      "buffer — a TRUNCATED comparison would silently agree",
                      k->name, W, r->n_unc, (int)UX_STREAM_CAP);
            *n_stream = 0u;
        } else {
            for (size_t i = 0; i < r->n_unc; i++) stream[i] = *cq_mock_at(&f.mock, i);
        }
    }

    r->dst_determinate = ux_read(&f.ctx, hd, &r->dst);
    r->clean = cq_reg_clean(&f.ctx, hd, cq_pc_zero_proof_rotation_free);
    r->dst_qubits = cq_reg_owned_qubits(&f.ctx.regs, hd);
    ux_close(&f);
}

/* The drift really was an inverse pair, asserted from the recorded stream.
 * `angle` is the one field the route-vs-route comparison omits, and it is the
 * only field that can see `Ry(0.7); Ry(0.7)`.
 *
 * THE WINDOW IS TWO OR THREE GATES, NOT ALWAYS TWO, and the first draft of this
 * helper assumed two and went red on exactly the classical-ONE lanes. §7's
 * general row materialises before it rotates, and cq_materialise emits an `X`
 * when the constant was 1 — so a ONE lane's drift is `X, RY, RY` and a ZERO
 * lane's is `RY, RY`. Pinning that explicitly is better than tolerating it:
 * the X's presence is exactly `cq_bit_value` of the lane. */
static void ux_check_ry_pair(const ux_kernel *k, int W, int j, uint64_t vb,
                             const ux_rt *r)
{
    const int lane_is_one = (int)((vb >> j) & 1u);
    const size_t want_n = lane_is_one ? 3u : 2u;
    const cq_rec *a, *b;

    if (r->n_drift != want_n) {
        cq_h_fail(__FILE__, __LINE__,
                  "%s W=%d lane %d (classical %d): the Ry drift emitted %zu "
                  "gates, want %zu — §7's general row materialises first, and "
                  "cq_materialise emits an X only when the constant was 1",
                  k->name, W, j, lane_is_one, r->n_drift, want_n);
        return;
    }

    if (lane_is_one && r->drift[0].op != CQ_OP_X)
        cq_h_fail(__FILE__, __LINE__,
                  "%s W=%d lane %d: a ONE lane's drift must open with the "
                  "materialising X", k->name, W, j);

    a = &r->drift[want_n - 2u];
    b = &r->drift[want_n - 1u];

    if (a->op != CQ_OP_RY || b->op != CQ_OP_RY)
        cq_h_fail(__FILE__, __LINE__, "%s W=%d lane %d: the drift does not end "
                  "in two rotations", k->name, W, j);
    if (a->t != b->t)
        cq_h_fail(__FILE__, __LINE__, "%s W=%d lane %d: the two rotations are "
                  "on q%u and q%u", k->name, W, j, a->t, b->t);

    /* Exact, not approximate: both are the same literal, and negation of a
     * finite double is a sign-bit flip. The `!= 0.0` guard is what stops a
     * pair of zeros satisfying it — 0.0 and -0.0 compare equal in C, which is
     * why mock_sink compares angles bitwise everywhere else. */
    if (a->angle == 0.0 || b->angle != -a->angle)
        cq_h_fail(__FILE__, __LINE__,
                  "%s W=%d lane %d: the drift is Ry(%a) then Ry(%a), which is "
                  "not an inverse pair — the oracle's whole licence is `same "
                  "circuit, SAME PHYSICAL INPUT STATE, therefore same output`, "
                  "and emission reads bit KINDS only, so a non-inverse pair is "
                  "byte-identical to an inverse one everywhere else",
                  k->name, W, j, a->angle, b->angle);
}

/* --- the cases ----------------------------------------------------------- */

/* THE DELTA IS PINNED AS A PER-KIND TUPLE, NEVER AS A TOTAL (Rule 10: two
 * unrelated circuits can share a total), and the tuple is what distinguishes
 * K9's swapping predicates from its non-swapping ones.
 *
 *      delta  =  R x P
 *
 * where R is how many times the kernel reads that lane as a CONTROL in ONE
 * compute half, and P is how many times the compute half runs: 1 for a flat
 * kernel and 2 for a sandwiched one, because Rule 8's driver replays the
 * compute half in reverse. THE FACTOR OF 2 IS EASY TO DROP AND THIS COMMENT
 * DROPPED IT ON ITS FIRST DRAFT — calling add's +4 "four reads" when `ripple`
 * reads the addend twice per half, and naming `mux` and `quotient` among
 * udiv's readers when neither touches `b` at all. Every row below is read out
 * of the kernel:
 *
 *   xor  1 x 1   bitwise.c: one CX(b[i], dst[i]) per lane, no sandwich
 *   and  1 x 1   one CCX(a[i], b[i], dst[i])
 *   or   2 x 1   `a|b = a ^ b ^ (a&b)`: a CX and a CCX
 *   sub  1 x 2   add.c's cq_sub_step reads `b` ONCE, in the complement copy
 *                CX(b[i], nb[i]); everything downstream reads `nb`
 *   add  2 x 2   K6 passes `y = b` straight into `ripple`, which reads y[i]
 *                twice for i < W-1 — the CX of phase 1 and the CCX of phase 2
 *   eq/ne     1 x 2   lower_eq!'s one CX into diff[i]. NOT lower_ult!, which
 *                     these two never reach
 *   ult/uge   1 x 2   cq_ult_step's complement copy; `b` sits in block.b
 *   ugt/ule   2 x 2   the SWAP puts the caller's `b` into block.a, which
 *                     lower_ult! reads twice — measured (+0,+2,+2)
 *   slt/sgt/sle/sge  1 x 2   PRIM_SLT copies both operands into af/bf first,
 *                     so its swap is invisible: 2 either way
 *   mul  (W-j) x 2   mul.c's partial product is CCX(a[u], b[j], pp[j][u+j])
 *                     for u < W-j, so the delta is 2(W-j) and NOT a per-kernel
 *                     constant. The table pins lane 0, where it is 2W
 *   udiv/urem 2 x 2 x W   W iterations, each reading the divisor twice: once
 *                     in M16's comparator complement and once in M14's. The
 *                     mux and the quotient CX never reference `b`
 *
 * TWO ROWS ARE LANE-DEPENDENT and the table pins lane 0, so the profile is
 * pinned separately below: add is 2x2 only for j < W-1 (ripple's tail reads
 * the top lane once), and mul is 2(W-j). W = 1 is its own column because mul
 * delegates to K2 there and add has no ripple stage at all. */
CQ_TEST(an_uncompute_after_a_general_ry_on_a_source_emits_more_gates)
{
    /* (x, cx, ccx) delta at LANE 0 — a classical ZERO, since vb = 0x…6. */
    static const struct {
        const char *name;
        uint64_t d[3][3];        /* W = 1, 4, 8 */
    } D[] = {
        { "xor",  { {0,1,0}, {0,1,0}, {0,1,0} } },
        { "and",  { {0,0,1}, {0,0,1}, {0,0,1} } },
        { "or",   { {0,1,1}, {0,1,1}, {0,1,1} } },
        { "add",  { {0,2,0}, {0,2,2}, {0,2,2} } },
        { "sub",  { {0,2,0}, {0,2,0}, {0,2,0} } },
        { "mul",  { {0,0,1}, {0,0,8}, {0,0,16} } },
        { "udiv", { {0,4,0}, {0,16,0}, {0,32,0} } },
        { "urem", { {0,4,0}, {0,16,0}, {0,32,0} } },
        { "eq",   { {0,2,0}, {0,2,0}, {0,2,0} } },
        { "ne",   { {0,2,0}, {0,2,0}, {0,2,0} } },
        { "ult",  { {0,2,0}, {0,2,0}, {0,2,0} } },
        { "ugt",  { {0,2,2}, {0,2,2}, {0,2,2} } },
        { "ule",  { {0,2,2}, {0,2,2}, {0,2,2} } },
        { "uge",  { {0,2,0}, {0,2,0}, {0,2,0} } },
        { "slt",  { {0,2,0}, {0,2,0}, {0,2,0} } },
        { "sgt",  { {0,2,0}, {0,2,0}, {0,2,0} } },
        { "sle",  { {0,2,0}, {0,2,0}, {0,2,0} } },
        { "sge",  { {0,2,0}, {0,2,0}, {0,2,0} } },
    };
    static const int WID[3] = { 1, 4, 8 };
    const uint64_t va = 0xBu, vb = 0x6u;
    int rows = 0;

    for (int ki = 0; ki < UX_N_K; ki++) {
        const ux_kernel *k = &UX_K[ki];
        int row = -1;

        for (unsigned d = 0; d < sizeof D / sizeof D[0]; d++)
            if (!strcmp(D[d].name, k->name)) row = (int)d;
        if (row < 0) {
            cq_h_fail(__FILE__, __LINE__,
                      "%s has no pinned delta — a kernel added to UX_K without "
                      "a row here is untested by this case", k->name);
            continue;
        }

        for (int wi = 0; wi < 3; wi++) {
            const int W = WID[wi];
            ux_rt r;

            ux_round_trip(k, W, va, vb, ~0ull, 0, UX_DRIFT_RY, &r, NULL, NULL);
            rows++;

            CHECK(r.lane_is_wire);
            CHECK(r.lane_poisoned);
            ux_check_ry_pair(k, W, 0, vb, &r);

            CHECK_GATES(r.unc.x - r.fwd.x, r.unc.cx - r.fwd.cx,
                        r.unc.ccx - r.fwd.ccx,
                        D[row].d[wi][0], D[row].d[wi][1], D[row].d[wi][2]);

            /* THE R6 ASSERTION ITSELF, stated as the inequality rather than as
             * a delta, because that is the sentence someone would "fix". */
            if (r.n_unc <= r.n_fwd)
                cq_h_fail(__FILE__, __LINE__,
                          "%s W=%d: the uncompute did NOT emit more gates than "
                          "the forward (%zu vs %zu). PRD §10 consequence (ii): "
                          "`unc == forward` is not an invariant, and this "
                          "fixture exists to be the case where they differ",
                          k->name, W, r.n_unc, r.n_fwd);
        }
    }
    CHECK_EQ(rows, UX_N_K * 3);
}

/* THE TWO LANE-DEPENDENT ROWS, pinned as a profile so "2W" is never read as a
 * per-kernel constant. At W=4 with vb = 0x6, lanes 1 and 2 are classical ONEs
 * and cost nothing; lane 0 and lane 3 are the ZEROs. */
CQ_TEST(the_delta_depends_on_which_lane_drifted_not_only_on_the_kernel)
{
    static const struct { const char *name; long long d[4]; } P[] = {
        /* mul: 2(W-j) — 8 at the bottom lane, 2 at the top                  */
        { "mul",  { 8, 0, 0, 2 } },
        /* add: ripple reads y[i] twice for i < W-1 and once at the top      */
        { "add",  { 4, 0, 0, 2 } },
        /* udiv: every iteration reads the whole divisor, so no lane profile */
        { "udiv", { 16, 0, 0, 16 } },
        /* ugt: the swap, and it is flat in the lane                         */
        { "ugt",  { 4, 0, 0, 4 } },
    };
    const int W = 4;
    const uint64_t va = 0xBu, vb = 0x6u;

    for (unsigned p = 0; p < sizeof P / sizeof P[0]; p++) {
        const ux_kernel *k = NULL;
        for (int ki = 0; ki < UX_N_K; ki++)
            if (!strcmp(UX_K[ki].name, P[p].name)) k = &UX_K[ki];
        CHECK(k != NULL);
        if (!k) continue;

        for (int j = 0; j < W; j++) {
            ux_rt r;
            ux_round_trip(k, W, va, vb, ~0ull, j, UX_DRIFT_RY, &r, NULL, NULL);
            CHECK(r.lane_is_wire);

            long long got = (long long)r.n_unc - (long long)r.n_fwd;
            if (got != P[p].d[j])
                cq_h_fail(__FILE__, __LINE__,
                          "%s W=%d lane %d: delta %+lld, want %+lld — the "
                          "delta is (reads of THIS lane) x (compute halves), "
                          "and both factors can depend on the lane",
                          k->name, W, j, got, P[p].d[j]);
        }
    }
}

/* THE ORACLE. Same circuit, one poisoned and one not. See the file header. */
CQ_TEST(the_same_drift_without_the_poison_emits_the_identical_stream)
{
    static cq_rec s_ry[UX_STREAM_CAP], s_cx[UX_STREAM_CAP];
    const uint64_t va = 0xBu, vb = 0x6u;

    for (int ki = 0; ki < UX_N_K; ki++) {
        const ux_kernel *k = &UX_K[ki];
        for (int W = 2; W <= 5; W++)
            for (int j = 0; j < W; j++) {
                ux_rt ry, cx;
                size_t n_ry = 0u, n_cx = 0u;

                ux_round_trip(k, W, va, vb, ~0ull, j, UX_DRIFT_RY, &ry,
                              s_ry, &n_ry);
                ux_round_trip(k, W, va, vb, ~0ull, j, UX_DRIFT_CX, &cx,
                              s_cx, &n_cx);

                /* Both routes drift the lane; only one keeps the shadow. */
                CHECK(ry.lane_is_wire && cx.lane_is_wire);
                CHECK(ry.lane_poisoned);
                CHECK(!cx.lane_poisoned);
                ux_check_ry_pair(k, W, j, vb, &ry);

                if (n_ry != n_cx) {
                    cq_h_fail(__FILE__, __LINE__,
                              "%s W=%d lane %d: the two routes' uncomputes are "
                              "%zu and %zu gates", k->name, W, j, n_ry, n_cx);
                    continue;
                }
                if (n_ry == 0u) {
                    cq_h_fail(__FILE__, __LINE__,
                              "%s W=%d lane %d: the uncompute emitted nothing, "
                              "so the streams match vacuously", k->name, W, j);
                    continue;
                }

                for (size_t i = 0; i < n_ry; i++)
                    if (s_ry[i].op != s_cx[i].op || s_ry[i].a != s_cx[i].a ||
                        s_ry[i].b != s_cx[i].b || s_ry[i].t != s_cx[i].t) {
                        cq_h_fail(__FILE__, __LINE__,
                                  "%s W=%d lane %d: gate %zu differs between "
                                  "the routes (op %d/%d, %u,%u->%u vs "
                                  "%u,%u->%u) — the CX route is only an oracle "
                                  "for the Ry route while the circuits are the "
                                  "same one", k->name, W, j, i,
                                  (int)s_ry[i].op, (int)s_cx[i].op,
                                  s_ry[i].a, s_ry[i].b, s_ry[i].t,
                                  s_cx[i].a, s_cx[i].b, s_cx[i].t);
                        break;
                    }

                /* WHAT THE ORACLE BUYS: the CX route's dst is readable and is
                 * zero, so the Ry route's — same gates, same input state — is
                 * zero too, though nothing in this project can read it. */
                CHECK(cx.dst_determinate);
                CHECK_EQ(cx.dst, 0u);
                CHECK(!ry.dst_determinate);

                /* AND WHAT IT COSTS: bd 2cf. Identical circuits, and only one
                 * of the two rails can be freed. */
                CHECK_EQ(cx.clean, 1);
                if (ry.clean)
                    cq_h_fail(__FILE__, __LINE__,
                              "%s W=%d lane %d: the rotation-tainted rail "
                              "reports provably clean. bd 2cf is that it does "
                              "NOT — if this has become true, the shadow has "
                              "started clearing `unknown`, which is the one "
                              "direction the discipline forbids",
                              k->name, W, j);
            }
    }
}

/* THE CONTROL FOR THE CAUSE. Materialisation is not what costs; losing a
 * ZERO-lane fold is.
 *
 * AND "A ONE LANE COSTS NOTHING" IS TRUE OF THE TOTAL AND FALSE OF THE TUPLE —
 * measured here, after the first draft of this case asserted the tuple was
 * unchanged and went red. What a classical ONE control does is FOLD ITS GATE
 * DOWN ONE CONTROL LEVEL: `CX(ONE,t)` folds to `X(t)` and `CCX(ONE,c,t)` to
 * `CX(c,t)`. Materialising the lane un-folds it, so every affected gate is
 * PROMOTED back — X -> CX, CX -> CCX — one for one. At `sub`, W=8, that is
 * (19,33,16) -> (17,35,16): two X become two CX, total 68 either way.
 *
 * So the assertion is the promotion ladder itself, which is strictly sharper
 * than either "the total is unchanged" (blind to the shift) or "the tuple is
 * unchanged" (false):
 *
 *      total equal   AND   unc.x <= fwd.x   AND   unc.ccx >= fwd.ccx
 *
 * — gates may only move UP the ladder, and none may appear or vanish.
 *
 * SCOPE, BECAUSE THE FOLD IS NOT GATE-LOCAL. `CX(ONE, t)` folds to `X(t)` only
 * when `t` is already a WIRE; cq_emit_x on a CONSTANT target rewrites it in
 * place for ZERO gates, so with an all-classical `a` a ONE lane costs 2 rather
 * than 0. This fixture holds `a` all-quantum, which materialises every dst lane
 * before the ONE lane's gate arrives. The all-classical row is its own case. */
CQ_TEST(only_a_classical_zero_lane_costs_the_uncompute_anything)
{
    const uint64_t va = 0xBu;
    int zero_lanes = 0, one_lanes = 0;

    for (int ki = 0; ki < UX_N_K; ki++)
        for (int W = 4; W <= 8; W += 4)
            for (int j = 0; j < W; j++) {
                const uint64_t vb = 0x6u;
                int lane_is_one = (int)((vb >> j) & 1u);
                ux_rt r;

                ux_round_trip(&UX_K[ki], W, va, vb, ~0ull, j, UX_DRIFT_RY, &r,
                              NULL, NULL);
                CHECK(r.lane_is_wire);

                if (lane_is_one) {
                    one_lanes++;
                    if (r.n_unc != r.n_fwd || r.unc.x > r.fwd.x ||
                        r.unc.ccx < r.fwd.ccx)
                        cq_h_fail(__FILE__, __LINE__,
                                  "%s W=%d lane %d held a classical ONE: "
                                  "(%llu,%llu,%llu) -> (%llu,%llu,%llu). A ONE "
                                  "control FOLDS its gate down one level, so "
                                  "materialising the lane promotes it back "
                                  "one for one — the total must not move, no "
                                  "X may appear and no Toffoli may vanish",
                                  UX_K[ki].name, W, j,
                                  (unsigned long long)r.fwd.x,
                                  (unsigned long long)r.fwd.cx,
                                  (unsigned long long)r.fwd.ccx,
                                  (unsigned long long)r.unc.x,
                                  (unsigned long long)r.unc.cx,
                                  (unsigned long long)r.unc.ccx);
                } else {
                    zero_lanes++;
                    if (r.n_unc <= r.n_fwd)
                        cq_h_fail(__FILE__, __LINE__,
                                  "%s W=%d: lane %d held a classical ZERO and "
                                  "the uncompute did not grow (%zu -> %zu)",
                                  UX_K[ki].name, W, j, r.n_fwd, r.n_unc);
                }
            }

    /* Both arms must have run, or the case proves half of what it says. */
    CHECK(zero_lanes > 0);
    CHECK(one_lanes > 0);
}

/* THE DURABLE FORM, AND IT READS NO GOLDEN.
 *
 * `_unc` is not "the forward again". It is the SAME KERNEL AT A DIFFERENT
 * REPRESENTATION, and its cost is a function of the representation at call
 * time and of nothing else — not of which pass it is, not of what the rail
 * held on entry, not of history. So the uncompute's tuple must equal a FRESH
 * forward's tuple at the mask the drift produced. Whoever "fixes" R6 by
 * asserting `unc == forward` is asserting this identity at the wrong mask.
 *
 * Nothing here can be blessed by CQOPS_UPDATE_GOLDENS=1: both sides are
 * measured in the same run. Which is also why it needs a floor — an equality
 * between two measurements is satisfied by a kernel that emits nothing. */
CQ_TEST(an_uncompute_costs_what_a_forward_at_the_same_representation_costs)
{
    const uint64_t va = 0xBu, vb = 0x6u;
    int checked = 0;

    for (int ki = 0; ki < UX_N_K; ki++) {
        const ux_kernel *k = &UX_K[ki];
        for (int W = 2; W <= 5; W++)
            for (int j = 0; j < W; j++) {
                ux_fix f;
                ux_rt r;
                int wd = ux_wdst(k, W);

                ux_round_trip(k, W, va, vb, ~0ull, j, UX_DRIFT_RY, &r,
                              NULL, NULL);

                /* The same operands, with lane j quantum FROM THE START. */
                ux_open(&f);
                int32_t ha = cq_bk_reg(&f.ctx, (uint32_t)W, va, ~0ull);
                int32_t hb = cq_bk_reg(&f.ctx, (uint32_t)W, vb, 1ull << j);
                int32_t hd = cq_reg_alloc_zero(&f.ctx.regs, (uint32_t)wd);

                cq_mock_reset(&f.mock);
                k->fn(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
                      cq_reg_cbits(&f.ctx.regs, ha),
                      cq_reg_cbits(&f.ctx.regs, hb), W);

                CHECK(r.unc.x + r.unc.cx + r.unc.ccx > 0u);
                CHECK_GATES(r.unc.x, r.unc.cx, r.unc.ccx,
                            cq_mock_count_op(&f.mock, CQ_OP_X),
                            cq_mock_count_op(&f.mock, CQ_OP_CX),
                            cq_mock_count_op(&f.mock, CQ_OP_CCX));
                checked++;
                ux_close(&f);
            }
    }
    CHECK_EQ(checked, UX_N_K * (2 + 3 + 4 + 5));
}

/* THE EXTREME CASE, AND IT IS NOT A DELTA AT ALL.
 *
 * Eight of the kernels below carry risk R9's all-classical short-circuit, and
 * it is re-evaluated PER CALL. On that row the forward is L5 — zero gates, zero
 * qubits, `dst` written by cq_emit_x's constant-flip — and one drifted lane
 * makes the operands no longer all-constant, so the uncompute enters the whole
 * sandwich. A rail that cost NOTHING acquires wires; and on the Ry route it can
 * never hand them back, because the same block that materialised the lane
 * poisoned it.
 *
 * cq_kd_case sweeps this mask on every kernel and can never see this: it does
 * not rotate, so its forward and its uncompute are always on the same side of
 * the short-circuit. And `the_uncompute_is_what_makes_the_free_legal` asserts
 * the OPPOSITE of what happens here — there the uncompute is what makes the
 * free legal; here it is what makes it impossible. */
CQ_TEST(an_all_classical_forward_is_free_and_its_uncompute_is_not)
{
    const uint64_t va = 0x9u, vb = 0x6u;
    const int W = 4;
    int rows = 0;

    for (int ki = 0; ki < UX_N_K; ki++) {
        const ux_kernel *k = &UX_K[ki];
        ux_rt ry, cx;

        /* `qa` = 0: BOTH operands classical, which is L5's own row. */
        ux_round_trip(k, W, va, vb, 0ull, 0, UX_DRIFT_RY, &ry, NULL, NULL);
        ux_round_trip(k, W, va, vb, 0ull, 0, UX_DRIFT_CX, &cx, NULL, NULL);

        /* L5, restated here because it is this case's premise: the forward
         * costs nothing and takes nothing (I4). A kernel without the
         * short-circuit would fail here and should. */
        CHECK_GATES(ry.fwd.x, ry.fwd.cx, ry.fwd.ccx, 0u, 0u, 0u);
        CHECK_EQ(ry.minted_fwd, 0u);

        CHECK(ry.lane_is_wire && ry.lane_poisoned);
        CHECK(cx.lane_is_wire && !cx.lane_poisoned);
        ux_check_ry_pair(k, W, 0, vb, &ry);

        /* ...and the uncompute is the whole construction. */
        if (ry.n_unc == 0u) {
            cq_h_fail(__FILE__, __LINE__,
                      "%s: the uncompute emitted nothing after one lane "
                      "drifted, so the short-circuit did not re-evaluate",
                      k->name);
            continue;
        }
        rows++;

        CHECK_GATES(ry.unc.x, ry.unc.cx, ry.unc.ccx,
                    cx.unc.x, cx.unc.cx, cx.unc.ccx);

        /* THE RAIL. Zero qubits after the forward (I4); wires after the
         * uncompute; and on the Ry route no certificate for any of them. */
        if (ry.dst_qubits == 0u)
            cq_h_fail(__FILE__, __LINE__,
                      "%s: dst still owns no qubits after the drifted "
                      "uncompute, so this case cannot see the leak", k->name);
        CHECK_EQ(cx.dst_qubits, ry.dst_qubits);
        CHECK(cx.dst_determinate);
        CHECK_EQ(cx.dst, 0u);
        CHECK_EQ(cx.clean, 1);

        if (ry.clean)
            cq_h_fail(__FILE__, __LINE__,
                      "%s: the rail is provably clean on the Ry route — bd 2cf "
                      "is that it is not, and this row is its worst form: the "
                      "forward cost nothing, so the %u qubits the uncompute "
                      "took are pure loss", k->name, ry.dst_qubits);
    }
    CHECK(rows >= 8);
}

/* THE DOCUMENTATION CORRECTION, MADE EXECUTABLE. PRD §10, CLAUDE.md Rule 14
 * and K05/K06.md named "`cqrt_ry`/`cqrt_rz`" jointly, and K04.md stated the
 * `Rz` version outright: "If CQ then applies an in-place `cqrt_rz` to `a`
 * before calling `_unc`, `a[3]` is now `CQ_BIT_Q` (we never demote, D6)".
 * Against M22 as shipped it is not: §7's Rz constant cell does nothing at any
 * phi, which is "the cell that keeps a classical rail classical through any
 * number of Rz calls" (src/rotate.c). This case is what turns red if the claim
 * is ever restored. */
CQ_TEST(no_rz_at_any_angle_can_cause_the_drift)
{
    static const double PHI[] = { 0.7, 3.14159, 6.2831853, 1e-9, -2.5, 1e6 };
    const uint64_t va = 0xBu, vb = 0x6u;
    const int W = 4;

    /* The count claim, once per kernel: an Rz between the two calls moves
     * nothing at all. */
    for (int ki = 0; ki < UX_N_K; ki++)
        for (int j = 0; j < W; j++) {
            ux_rt r;
            ux_round_trip(&UX_K[ki], W, va, vb, ~0ull, j, UX_DRIFT_RZ, &r,
                          NULL, NULL);
            CHECK(!r.lane_is_wire);
            CHECK_GATES(r.unc.x, r.unc.cx, r.unc.ccx,
                        r.fwd.x, r.fwd.cx, r.fwd.ccx);
            CHECK(r.dst_determinate);
            CHECK_EQ(r.dst, 0u);
            CHECK_EQ(r.clean, 1);
        }

    /* The angle sweep on the bit alone, which is about §7's cell rather than
     * about any kernel — so it runs ONCE, not once per kernel. */
    for (int j = 0; j < W; j++)
        for (unsigned p = 0; p < sizeof PHI / sizeof PHI[0]; p++) {
            ux_fix f;

            ux_open(&f);
            int32_t hb = cq_bk_reg(&f.ctx, (uint32_t)W, vb, 0ull);
            cq_bit *bb = cq_reg_bits(&f.ctx.regs, hb);
            cq_pc_snap before = cq_pc_take(&f.ctx);

            cq_mock_reset(&f.mock);
            for (int rep = 0; rep < 3; rep++)
                cq_rotate_rz_bit(&f.ctx, &bb[j], PHI[p]);

            if (cq_bit_is_qubit(bb[j]))
                cq_h_fail(__FILE__, __LINE__,
                          "Rz(%g) x3 materialised a classical lane — §7's Rz "
                          "constant cell is a no-op at every phi, and the D6 "
                          "representation drift has exactly ONE cause, the "
                          "general Ry", PHI[p]);
            CHECK_EQ(cq_mock_count(&f.mock), 0u);
            CHECK_EQ(cq_pc_take(&f.ctx).minted, before.minted);
            ux_close(&f);
        }
}

/* WHY THE L4 GOLDENS CANNOT SHOW ANY OF THIS, stated executably rather than in
 * the header comment cq_gold_close writes into every file. At the all-quantum
 * mask there is nothing left to materialise, so no drift is representable and
 * forward == unc necessarily. That is a claim about the FIXTURE, not about the
 * axis, and confusing the two is exactly how `unc == forward` gets promoted
 * into an invariant. */
CQ_TEST(the_all_quantum_mask_is_the_fixed_point_the_goldens_are_pinned_at)
{
    const uint64_t va = 0xBu, vb = 0x6u;

    for (int ki = 0; ki < UX_N_K; ki++)
        for (int W = 2; W <= 5; W++)
            for (int j = 0; j < W; j++) {
                ux_fix f;
                int wd = ux_wdst(&UX_K[ki], W);
                struct { uint64_t x, cx, ccx; } c1, c2;

                ux_open(&f);
                int32_t ha = cq_bk_reg(&f.ctx, (uint32_t)W, va, ~0ull);
                int32_t hb = cq_bk_reg(&f.ctx, (uint32_t)W, vb, ~0ull);
                int32_t hd = cq_reg_alloc_zero(&f.ctx.regs, (uint32_t)wd);
                cq_bit *bb = cq_reg_bits(&f.ctx.regs, hb);

                cq_mock_reset(&f.mock);
                UX_K[ki].fn(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
                            cq_reg_cbits(&f.ctx.regs, ha),
                            cq_reg_cbits(&f.ctx.regs, hb), W);
                c1.x   = cq_mock_count_op(&f.mock, CQ_OP_X);
                c1.cx  = cq_mock_count_op(&f.mock, CQ_OP_CX);
                c1.ccx = cq_mock_count_op(&f.mock, CQ_OP_CCX);
                CHECK(cq_mock_count(&f.mock) > 0u);

                /* THE SAME GENERAL Ry THAT DRIFTS A CLASSICAL LANE, and the
                 * assertions below are on the ROTATION as much as on the
                 * counts: delete the two calls and a `minted` comparison alone
                 * would still pass, because nothing was going to be minted. */
                cq_pc_snap before = cq_pc_take(&f.ctx);
                cq_mock_reset(&f.mock);
                cq_rotate_ry_bit(&f.ctx, &bb[j],  0.7);
                cq_rotate_ry_bit(&f.ctx, &bb[j], -0.7);
                CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RY), 2u);
                CHECK_EQ(cq_pc_take(&f.ctx).minted, before.minted);
                CHECK(cq_shadow_get(&f.ctx.shadow,
                                    cq_bit_qindex(bb[j])).unknown);

                cq_mock_reset(&f.mock);
                UX_K[ki].fn(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
                            cq_reg_cbits(&f.ctx.regs, ha),
                            cq_reg_cbits(&f.ctx.regs, hb), W);
                c2.x   = cq_mock_count_op(&f.mock, CQ_OP_X);
                c2.cx  = cq_mock_count_op(&f.mock, CQ_OP_CX);
                c2.ccx = cq_mock_count_op(&f.mock, CQ_OP_CCX);

                CHECK_GATES(c2.x, c2.cx, c2.ccx, c1.x, c1.cx, c1.ccx);
                ux_close(&f);
            }
}
