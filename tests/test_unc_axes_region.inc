/* tests/test_unc_axes_region.inc — bd 4jj (c). WHETHER THE DRIFT HAPPENS IS A
 * FUNCTION OF THE REGION. Included by tests/test_unc_axes.c.
 *
 * cq_rotate_ry_bit's first act is §9 row 0's skip (src/rotate.c): under a
 * CQ_BIT_ZERO control the general Ry neither materialises nor poisons, so the
 * representation does not drift and the uncompute is the forward's circuit
 * again. Under ONE it drifts verbatim; under a quantum control the general row
 * PROMOTES (`R(θ/2); CX; R(−θ/2); CX`, §9) and still materialises and poisons
 * — even when the control wire holds |0>, because emission reads kinds, never
 * the shadow (D6). tests/test_controlled_region.inc pins the row-0 skip in
 * isolation; what nothing pinned is its CONSEQUENCE for PRD §10's asymmetry.
 *
 * THREE SHAPES, one fixture. `c1`/`c2` are control KINDS — ZERO, ONE, or a
 * wire holding |0> or |1> — and every row runs on both drift routes.
 *
 *   (i)  UNC_IN_REGION    push c1; fwd; pop.  drift.  push c1; unc; pop.
 *        One push per kernel call: exactly the granularity CQ_lang's ABI can
 *        express (tests/support/kerneldrv.c, cq_kd_call_kernel's comment —
 *        a `_controlled` symbol prepends one flag per template call). The
 *        drift is uncontrolled, so it ALWAYS happens; what c1 decides is
 *        whether the kernel calls do.
 *   (ii) DRIFT_IN_REGION  fwd.  push c1; drift; pop.  unc.
 *        The drift's occurrence alone is conditional.
 *   (iii) NESTED          push c1; fwd; push c2; drift; pop; unc; pop.
 *        The outer region gates the kernel pair; the drift sees c1 AND c2.
 *        No ABI symbol can spell this (there is no push/pop bracket in it);
 *        it is the internal composition, and row 0 composing down the stack
 *        is what it exercises.
 *
 * WHAT IS PREDICTED, from two booleans each row derives:
 *   kernel_runs   = the kernel calls' region is not ZERO
 *   drift_happens = no region the drift sees is ZERO
 * and then, never comparing across the axis except where the second is false:
 *   !kernel_runs                  both calls emit nothing (row 0), dst stays
 *                                 the all-constant rail it was born (I4)
 *   kernel_runs && !drift_happens THE UNCOMPUTE'S TUPLE EQUALS THE FORWARD'S.
 *                                 This is the one row Rule 14 permits it, and
 *                                 it is the claim: the region made the drift
 *                                 provably absent
 *   kernel_runs && drift_happens  the uncompute is strictly larger
 * plus, on every row: Rule 14's durable identity against a fresh forward at
 * the drifted mask under the uncompute's region; the forward's value
 * (ctrl · f); dst == 0 after the uncompute (read where readable, by the
 * differential where not); L2 as a set after the last pop; and L3 wherever the
 * shadow can prove the rail — which, on the Ry route, is exactly the rows
 * where the region suppressed either the drift or the kernel.
 *
 * THE KERNEL IS K6 AT W = 4: a real compute half, Toffolis that exercise §9's
 * lazily-minted ancilla under a quantum control, and a nonzero delta at lane 0
 * (a classical ZERO in 0x6), which test_unc_asym.inc pins at (0,2,2). */

enum { AX_RG_W = 4, AX_RG_LANE = 0 };
static const uint64_t AX_RG_VA = 0xBu, AX_RG_VB = 0x6u;

typedef enum { AX_C_NONE = 0, AX_C_ZERO, AX_C_ONE, AX_C_Q0, AX_C_Q1 } ax_ctl;
static const char *const AX_CTL[] = { "-", "ZERO", "ONE", "Q|0>", "Q|1>" };
static const ax_ctl AX_KINDS[] = { AX_C_ZERO, AX_C_ONE, AX_C_Q0, AX_C_Q1 };
enum { AX_N_KINDS = (int)(sizeof AX_KINDS / sizeof AX_KINDS[0]) };

typedef enum { AX_UNC_IN_REGION, AX_DRIFT_IN_REGION, AX_NESTED } ax_shape;
static const char *const AX_SHAPE[] = { "(i)", "(ii)", "(iii)" };

static int ax_ctl_is_q(ax_ctl c) { return c == AX_C_Q0 || c == AX_C_Q1; }
static int ax_ctl_on(ax_ctl c)   { return c == AX_C_ONE || c == AX_C_Q1; }

/* A one-bit register of the asked-for KIND, checked — the lesson
 * test_unc.c records: degrade a quantum control to a classical one and row 0
 * folds the region away while every value assertion stays green. Every claim
 * under a region is conditional on the region it says it opened. */
static int32_t ax_ctl_reg(cq_ctx *ctx, ax_ctl c, const char *row)
{
    if (c == AX_C_NONE) return CQ_REG_NONE;

    int32_t h = cq_bk_reg(ctx, 1u, (uint64_t)ax_ctl_on(c),
                          ax_ctl_is_q(c) ? 1u : 0u);
    const cq_bit *b = cq_reg_cbits(&ctx->regs, h);

    AX_EXPECT(ax_ctl_is_q(c) ? cq_bit_is_qubit(*b)
                             : (cq_bit_is_const(*b) &&
                                cq_bit_value(*b) == ax_ctl_on(c)),
              "%s: the %s control was not built as that kind", row,
              AX_CTL[c]);
    return h;
}

static void ax_push(cq_ctx *ctx, int32_t h)
{
    if (h != CQ_REG_NONE) cq_ctrl_push(ctx, cq_reg_cbits(&ctx->regs, h));
}

static void ax_pop(cq_ctx *ctx, int32_t h)
{
    if (h != CQ_REG_NONE) cq_ctrl_pop(ctx);
}

static void ax_add(ax_fix *f, int32_t hd, int32_t ha, int32_t hb)
{
    cq_kernel_add(&f->ctx, cq_reg_bits(&f->ctx.regs, hd),
                  cq_reg_cbits(&f->ctx.regs, ha),
                  cq_reg_cbits(&f->ctx.regs, hb), AX_RG_W);
}

/* What the row predicts, from its shape and kinds alone. */
typedef struct {
    ax_ctl kernel_ctl;       /* the region around each kernel call        */
    int    kernel_runs, kernel_on;
    int    drift_happens, drift_promoted;
} ax_pred;

static ax_pred ax_predict(ax_shape s, ax_ctl c1, ax_ctl c2)
{
    ax_pred p;
    const ax_ctl d1 = (s == AX_UNC_IN_REGION) ? AX_C_NONE : c1;
    const ax_ctl d2 = (s == AX_NESTED) ? c2 : AX_C_NONE;

    p.kernel_ctl     = (s == AX_DRIFT_IN_REGION) ? AX_C_NONE : c1;
    p.kernel_runs    = p.kernel_ctl != AX_C_ZERO;
    p.kernel_on      = p.kernel_ctl == AX_C_NONE || ax_ctl_on(p.kernel_ctl);
    p.drift_happens  = d1 != AX_C_ZERO && d2 != AX_C_ZERO;
    p.drift_promoted = p.drift_happens && (ax_ctl_is_q(d1) || ax_ctl_is_q(d2));
    return p;
}

typedef struct {
    ax_tuple fwd, unc;
    int      fwd_det, unc_det;
    uint64_t dst_fwd, dst;
    uint32_t dst_q;          /* qubits dst owns at the end                 */
    int      lane_wire, lane_poisoned;
    int      src_ok;         /* the CX pair restored b; 1 on the Ry route */
    int      drift_live;     /* live after the drift minus before          */
    size_t   n_drift;        /* records in the drift window                */
    int      l2, l3, disposition;
    size_t   n_stream;
} ax_rrt;

static void ax_region_round_trip(ax_shape s, ax_ctl c1, ax_ctl c2,
                                 ax_route route, ax_rrt *r, cq_rec *stream,
                                 const char *row)
{
    const ax_pred p = ax_predict(s, c1, c2);
    ax_fix f;

    memset(r, 0, sizeof *r);
    r->l3 = -1;
    ax_open(&f);

    int32_t ha  = cq_bk_reg(&f.ctx, AX_RG_W, AX_RG_VA, ~0ull);
    int32_t hb  = cq_bk_reg(&f.ctx, AX_RG_W, AX_RG_VB, 0ull);
    int32_t hp  = cq_bk_reg(&f.ctx, 1u, 1u, 1u);   /* both routes; see (a) */
    int32_t hc1 = ax_ctl_reg(&f.ctx, c1, row);
    int32_t hc2 = ax_ctl_reg(&f.ctx, s == AX_NESTED ? c2 : AX_C_NONE, row);
    int32_t hd  = cq_reg_alloc_zero(&f.ctx.regs, AX_RG_W);
    cq_bit *bb  = cq_reg_bits(&f.ctx.regs, hb);

    /* dst last, so the free leaves the others as one contiguous prefix. */
    int32_t  own[6] = { ha, hb, hp, hc1 };
    uint32_t n_own = 4u;
    if (hc2 != CQ_REG_NONE) own[n_own++] = hc2;
    own[n_own++] = hd;

    /* --- the forward ---------------------------------------------------- */
    if (s == AX_NESTED) ax_push(&f.ctx, hc1);
    if (s == AX_UNC_IN_REGION) ax_push(&f.ctx, hc1);
    cq_mock_reset(&f.mock);
    ax_add(&f, hd, ha, hb);
    r->fwd = ax_tuple_of(&f.mock);
    if (s == AX_UNC_IN_REGION) ax_pop(&f.ctx, hc1);
    r->fwd_det = ax_read(&f.ctx, hd, &r->dst_fwd);

    /* --- the drift, under whatever the shape opens around it ------------ */
    const uint32_t live0 = cq_pc_take(&f.ctx).live;
    cq_mock_reset(&f.mock);
    if (s == AX_DRIFT_IN_REGION) ax_push(&f.ctx, hc1);
    if (s == AX_NESTED) ax_push(&f.ctx, hc2);
    ax_drift(&f.ctx, &bb[AX_RG_LANE], cq_reg_cbits(&f.ctx.regs, hp), route);
    if (s == AX_NESTED) ax_pop(&f.ctx, hc2);
    if (s == AX_DRIFT_IN_REGION) ax_pop(&f.ctx, hc1);
    r->n_drift = cq_mock_count(&f.mock);
    if (route == AX_RY)
        ax_check_ry_window(&f.mock, !p.drift_happens ? 0u
                                    : p.drift_promoted ? 4u : 2u, row);
    r->drift_live = (int)cq_pc_take(&f.ctx).live - (int)live0;
    r->lane_wire = cq_bit_is_qubit(bb[AX_RG_LANE]);
    r->lane_poisoned = r->lane_wire &&
        cq_shadow_get(&f.ctx.shadow, cq_bit_qindex(bb[AX_RG_LANE])).unknown;
    r->src_ok = 1;
    if (route == AX_CX) {
        uint64_t vb;
        r->src_ok = ax_read(&f.ctx, hb, &vb) && vb == AX_RG_VB;
    }

    /* --- the uncompute -------------------------------------------------- */
    if (s == AX_UNC_IN_REGION) ax_push(&f.ctx, hc1);
    cq_mock_reset(&f.mock);
    ax_add(&f, hd, ha, hb);
    r->unc = ax_tuple_of(&f.mock);
    r->n_stream = ax_keep_stream(&f.mock, stream, row);
    if (s == AX_UNC_IN_REGION) ax_pop(&f.ctx, hc1);
    if (s == AX_NESTED) ax_pop(&f.ctx, hc1);

    /* After the LAST pop (an unbalanced push is cq_ctx_dispose's own abort): a region's AND wire and lazy ancilla are live
     * until it closes, and are nobody's register. */
    r->unc_det     = ax_read(&f.ctx, hd, &r->dst);
    r->dst_q       = cq_reg_owned_qubits(&f.ctx.regs, hd);
    r->l2          = cq_pc_live_is_exactly(&f.ctx, own, n_own);
    r->disposition = cq_reg_disposition(&f.ctx, hd,
                                        cq_pc_zero_proof_rotation_free);
    if (r->disposition > 0) r->l3 = ax_free_l3(&f, hd, own, n_own - 1u);
    ax_close(&f);
}

/* Rule 14's right-hand side: the same kernel on FRESH operands whose drifted
 * lane is quantum from birth iff the drift happened, under the region the
 * uncompute ran in. */
static ax_tuple ax_region_fresh(const ax_pred *p, const char *row)
{
    ax_fix f;
    ax_tuple t;

    ax_open(&f);
    int32_t ha = cq_bk_reg(&f.ctx, AX_RG_W, AX_RG_VA, ~0ull);
    int32_t hb = cq_bk_reg(&f.ctx, AX_RG_W, AX_RG_VB,
                           p->drift_happens ? 1ull << AX_RG_LANE : 0ull);
    int32_t hc = ax_ctl_reg(&f.ctx, p->kernel_ctl, row);
    int32_t hd = cq_reg_alloc_zero(&f.ctx.regs, AX_RG_W);

    ax_push(&f.ctx, hc);
    cq_mock_reset(&f.mock);
    ax_add(&f, hd, ha, hb);
    t = ax_tuple_of(&f.mock);
    ax_pop(&f.ctx, hc);
    ax_close(&f);
    return t;
}

static void ax_region_check(ax_shape s, ax_ctl c1, ax_ctl c2)
{
    static cq_rec st[2][AX_STREAM_CAP];
    const ax_pred p = ax_predict(s, c1, c2);
    const uint64_t f_ab = (AX_RG_VA + AX_RG_VB) & cq_ref_mask(AX_RG_W);
    ax_rrt r[2];
    char row[96];

    /* Non-vacuity first: with f(a,b) = 0 every value row expects 0. */
    CHECK(f_ab != 0u);

    for (int rt = AX_RY; rt <= AX_CX; rt++) {
        const ax_rrt *q = &r[rt];

        snprintf(row, sizeof row, "%s c1=%s c2=%s (%s)", AX_SHAPE[s],
                 AX_CTL[c1], AX_CTL[s == AX_NESTED ? c2 : AX_C_NONE],
                 AX_ROUTE[rt]);
        ax_region_round_trip(s, c1, c2, (ax_route)rt, &r[rt], st[rt], row);

        /* Bennett's controlled() contract: dst = ctrl ? f : 0. */
        AX_EXPECT(q->fwd_det &&
                  q->dst_fwd == (p.kernel_runs && p.kernel_on ? f_ab : 0u),
                  "%s: the forward left dst at 0x%llx", row,
                  (unsigned long long)q->dst_fwd);

        /* THE DRIFT, AS THE REGION DECIDED: a wire and one more live qubit
         * iff it happened, poisoned iff it happened on the Ry route, and not
         * one record emitted when row 0 skipped it. */
        AX_EXPECT(q->lane_wire == p.drift_happens &&
                  q->drift_live == p.drift_happens &&
                  q->lane_poisoned == (p.drift_happens && rt == AX_RY),
                  "%s: after the drift the lane is wire=%d poisoned=%d, live "
                  "moved %+d; predicted drift=%d", row, q->lane_wire,
                  q->lane_poisoned, q->drift_live, p.drift_happens);
        if (!p.drift_happens) CHECK_EQ(q->n_drift, 0u);
        AX_EXPECT(q->src_ok, "%s: the CX pair did not restore b", row);

        if (!p.kernel_runs) {
            CHECK_GATES(q->fwd.x, q->fwd.cx, q->fwd.ccx, 0u, 0u, 0u);
            CHECK_GATES(q->unc.x, q->unc.cx, q->unc.ccx, 0u, 0u, 0u);
            CHECK_EQ(q->dst_q, 0u);
        } else if (!p.drift_happens) {
            /* THE ONE EQUALITY ACROSS THE AXIS RULE 14 PERMITS, AND HERE IT
             * IS THE CLAIM: a ZERO control made the drift provably absent,
             * so the uncompute sees the forward's representation. */
            CHECK(ax_total(q->fwd) > 0u);
            CHECK_GATES(q->unc.x, q->unc.cx, q->unc.ccx,
                        q->fwd.x, q->fwd.cx, q->fwd.ccx);
        } else {
            AX_EXPECT(ax_total(q->unc) > ax_total(q->fwd), "%s: the drift "
                      "happened and the uncompute did not grow (%llu -> %llu)",
                      row, (unsigned long long)ax_total(q->fwd),
                      (unsigned long long)ax_total(q->unc));
        }

        ax_tuple fr = ax_region_fresh(&p, row);
        CHECK_GATES(q->unc.x, q->unc.cx, q->unc.ccx, fr.x, fr.cx, fr.ccx);

        /* READABLE EXACTLY WHERE THE REGION SUPPRESSED THE DRIFT OR THE
         * KERNEL, and freeable exactly there; elsewhere UNPROVEN (bd 2cf),
         * never proven dirty — the rail is physically |0>. */
        const int readable = !(rt == AX_RY && p.drift_happens &&
                               p.kernel_runs);
        if (readable) {
            AX_EXPECT(q->unc_det && q->dst == 0u, "%s: dst = 0x%llx after "
                      "the uncompute", row, (unsigned long long)q->dst);
            AX_EXPECT(q->disposition == 1 && q->l3 == 1, "%s: free: "
                      "disposition %d, L3 %d", row, q->disposition, q->l3);
        } else {
            AX_EXPECT(!q->unc_det && q->disposition == 0, "%s: det=%d "
                      "disposition=%d, want an unreadable UNPROVEN rail", row,
                      q->unc_det, q->disposition);
        }
        AX_EXPECT(q->l2, "%s: L2 after the last pop", row);
    }

    /* THE ORACLE, as in (a): the same uncompute on both routes. */
    AX_EXPECT(r[AX_RY].n_stream == r[AX_CX].n_stream &&
              (r[AX_RY].n_stream > 0u) == p.kernel_runs &&
              ax_first_diff(st[AX_RY], st[AX_CX], r[AX_RY].n_stream)
                  == r[AX_RY].n_stream,
              "%s c1=%s c2=%s: the two routes' uncomputes differ",
              AX_SHAPE[s], AX_CTL[c1], AX_CTL[c2]);
}

CQ_TEST(a_drifted_uncompute_inside_its_own_region)
{
    for (int i = 0; i < AX_N_KINDS; i++)
        ax_region_check(AX_UNC_IN_REGION, AX_KINDS[i], AX_C_NONE);
}

CQ_TEST(a_drift_inside_a_region_happens_only_if_the_region_runs)
{
    for (int i = 0; i < AX_N_KINDS; i++)
        ax_region_check(AX_DRIFT_IN_REGION, AX_KINDS[i], AX_C_NONE);
}

CQ_TEST(nested_regions_across_the_pair_gate_the_drift_and_the_kernel_apart)
{
    for (int i = 0; i < AX_N_KINDS; i++)
        for (int k = 0; k < AX_N_KINDS; k++)
            ax_region_check(AX_NESTED, AX_KINDS[i], AX_KINDS[k]);
}
