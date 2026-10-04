/* tests/test_unc_axes_route.inc — bd 4jj (a). M12's drift is a ROUTE CHANGE.
 * Included by tests/test_unc_axes.c.
 *
 * barrel() (src/kernels/shift_var.c) asks `cq_bits_all_const(b, L)` on every
 * call, with L = ceil(log2 W) the count of amount lanes the barrel reads as mux
 * controls. A forward with the amount classical in those lanes takes M11's
 * constant path — W-k CX (W for ashr), no scratch, no sandwich. Drift ONE of
 * those lanes and the identical call at the uncompute takes the sandwich: a
 * W(3L+1) scratch region and a compute half the forward never ran. That is the
 * uncompute opening a sandwich the forward did not — the R1 surface at a
 * representation no per-kernel suite reaches, because cq_kd_case never rotates
 * and so its forward and uncompute are always on the same side of the test.
 *
 * WHAT IS ASSERTED, PER (shift, W, lane, route):
 *   - the forward IS the constant path: its tuple is (0, n_land, 0) with
 *     n_land computed here, and it took no qubit `dst` does not now own;
 *   - the uncompute IS the sandwich: it took exactly W(3L+1) qubits beyond
 *     the `dst` lanes its copy-out materialised, its stream is an ordered
 *     palindrome around a W-gate copy-out, and it is at least 3W gates (the
 *     INIT copy in each half plus the copy-out, all on wires because `a` is
 *     all-quantum) against a forward of at most W — so `n_unc > n_fwd` is a
 *     computed inequality, never an equality across the axis (Rule 14);
 *   - the durable identity: the uncompute's tuple equals a FRESH forward's at
 *     the drifted mask;
 *   - `dst` is zero afterwards (read on the CX route; by the differential on
 *     the Ry route), and the pool is exactly the four registers' after the
 *     uncompute and exactly three after the free (L2/L3 as SETS).
 *
 * ALL THREE SHIFTS, because the table shape makes it free: they share the
 * entry signature and differ in the constant path's landing count, which is
 * computed per row rather than pinned. W runs over 2, 4, 5 and 8 — L = 1, 2, 3,
 * 3 — and the amount 0x6 makes the drifted lane a classical ZERO at lane 0 and
 * a ONE above it, and at W = 5 makes k = 6 >= W: D8's saturation, where shl
 * and lshr's forward is ZERO gates and ZERO qubits and the uncompute is the
 * whole barrel. */

typedef struct {
    const char  *name;
    cq_kernel_fn fn;
    uint64_t   (*ref)(uint64_t a, int k, int W);
    int          sign_fill;   /* ashr: the constant path writes every lane */
} ax_shift;

static const ax_shift AX_SHIFT[] = {
    { "shl",  cq_kernel_shl_var,  cq_ref_shl,  0 },
    { "lshr", cq_kernel_lshr_var, cq_ref_lshr, 0 },
    { "ashr", cq_kernel_ashr_var, cq_ref_ashr, 1 },
};
enum { AX_N_SHIFT = (int)(sizeof AX_SHIFT / sizeof AX_SHIFT[0]) };

static const int AX_SH_W[] = { 2, 4, 5, 8 };
enum { AX_N_SH_W = (int)(sizeof AX_SH_W / sizeof AX_SH_W[0]) };

static const uint64_t AX_SH_VA = 0xB5u, AX_SH_VB = 0x6u;

/* ceil(log2 W) for W >= 2, written here rather than asked of
 * cq_shift_stages: the barrel's region is the figure under test, and an
 * oracle that called the module's own helper would move with it. */
static int ax_stages(int W)
{
    int s = 0;

    while ((1 << s) < W) s++;
    return s;
}

typedef struct {
    ax_tuple fwd, unc;
    size_t   n_unc;                /* every record, rotations included     */
    int      fwd_det, unc_det;
    uint64_t dst_fwd, dst;
    uint32_t took_fwd, took_unc;   /* qubits each call acquired            */
    uint32_t dst_q_fwd, dst_q_unc; /* qubits dst owns after each call      */
    int      lane_wire, lane_poisoned;
    int      src_restored;         /* CX route only; 1 on the Ry route     */
    int      palindrome;
    int      l2_fwd, l2_unc, l3;   /* l3: -1 when the rail was not freed   */
    int      disposition;
    size_t   n_stream;
} ax_srt;

/* Forward, drift lane `j` of the amount, uncompute — then free if and only if
 * the shadow can prove the rail. `stream` receives the uncompute. */
static void ax_shift_round_trip(const ax_shift *sh, int W, int j,
                                ax_route route, ax_srt *r, cq_rec *stream,
                                const char *row)
{
    ax_fix f;

    memset(r, 0, sizeof *r);
    r->l3 = -1;
    r->src_restored = 1;
    ax_open(&f);

    int32_t ha = cq_bk_reg(&f.ctx, (uint32_t)W, AX_SH_VA, ~0ull);
    int32_t hb = cq_bk_reg(&f.ctx, (uint32_t)W, AX_SH_VB, 0ull);
    /* Minted on BOTH routes so every later index agrees between them. */
    int32_t hp = cq_bk_reg(&f.ctx, 1u, 1u, 1u);
    int32_t hd = cq_reg_alloc_zero(&f.ctx.regs, (uint32_t)W);
    const int32_t own[4] = { ha, hb, hp, hd };
    cq_bit *bb = cq_reg_bits(&f.ctx.regs, hb);

    AX_EXPECT(cq_bits_all_const(bb, W), "%s: the amount is not born "
              "classical, so the forward cannot take the constant path", row);

    cq_pc_snap s0 = cq_pc_take(&f.ctx);
    cq_mock_reset(&f.mock);
    sh->fn(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
           cq_reg_cbits(&f.ctx.regs, ha), cq_reg_cbits(&f.ctx.regs, hb), W);
    r->fwd       = ax_tuple_of(&f.mock);
    r->took_fwd  = cq_pc_take(&f.ctx).minted - s0.minted;
    r->dst_q_fwd = cq_reg_owned_qubits(&f.ctx.regs, hd);
    r->fwd_det   = ax_read(&f.ctx, hd, &r->dst_fwd);
    r->l2_fwd    = cq_pc_live_is_exactly(&f.ctx, own, 4u);

    cq_mock_reset(&f.mock);
    ax_drift(&f.ctx, &bb[j], cq_reg_cbits(&f.ctx.regs, hp), route);
    if (route == AX_RY) ax_check_ry_window(&f.mock, 2u, row);
    r->lane_wire     = cq_bit_is_qubit(bb[j]);
    r->lane_poisoned = r->lane_wire &&
        cq_shadow_get(&f.ctx.shadow, cq_bit_qindex(bb[j])).unknown;
    if (route == AX_CX) {
        uint64_t vb;
        r->src_restored = ax_read(&f.ctx, hb, &vb) &&
                          vb == (AX_SH_VB & cq_ref_mask(W));
    }

    /* `minted` counts acquisitions only while nothing is on the free list —
     * which is this fixture's premise until the uncompute, and is checked
     * rather than assumed: nothing above released an index. */
    cq_pc_snap s2 = cq_pc_take(&f.ctx);
    AX_EXPECT(s2.n_free == 0u, "%s: %u indices on the free list before the "
              "uncompute, so `minted` no longer counts what it takes", row,
              s2.n_free);

    cq_mock_reset(&f.mock);
    sh->fn(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
           cq_reg_cbits(&f.ctx.regs, ha), cq_reg_cbits(&f.ctx.regs, hb), W);
    r->unc       = ax_tuple_of(&f.mock);
    r->n_unc     = cq_mock_count(&f.mock);
    r->took_unc  = cq_pc_take(&f.ctx).minted - s2.minted;
    r->dst_q_unc = cq_reg_owned_qubits(&f.ctx.regs, hd);

    /* [compute half] [W-gate copy-out] [compute half reversed]. The head is
     * derived from the total; the length check inside the predicate makes a
     * wrong derivation a failure rather than a truncation. */
    if (r->n_unc > (size_t)W && (r->n_unc - (size_t)W) % 2u == 0u)
        r->palindrome = cq_mock_is_palindrome(&f.mock,
                                              (r->n_unc - (size_t)W) / 2u,
                                              (size_t)W);
    r->n_stream = ax_keep_stream(&f.mock, stream, row);

    r->unc_det     = ax_read(&f.ctx, hd, &r->dst);
    r->l2_unc      = cq_pc_live_is_exactly(&f.ctx, own, 4u);
    r->disposition = cq_reg_disposition(&f.ctx, hd,
                                        cq_pc_zero_proof_rotation_free);
    if (r->disposition > 0) r->l3 = ax_free_l3(&f, hd, own, 3u);
    ax_close(&f);
}

/* The same operands with lane `j` of the amount quantum FROM THE START —
 * Rule 14's durable identity reads its right-hand side here. */
static ax_tuple ax_shift_fresh(const ax_shift *sh, int W, int j)
{
    ax_fix f;
    ax_tuple t;

    ax_open(&f);
    int32_t ha = cq_bk_reg(&f.ctx, (uint32_t)W, AX_SH_VA, ~0ull);
    int32_t hb = cq_bk_reg(&f.ctx, (uint32_t)W, AX_SH_VB, 1ull << j);
    int32_t hd = cq_reg_alloc_zero(&f.ctx.regs, (uint32_t)W);

    cq_mock_reset(&f.mock);
    sh->fn(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
           cq_reg_cbits(&f.ctx.regs, ha), cq_reg_cbits(&f.ctx.regs, hb), W);
    t = ax_tuple_of(&f.mock);
    ax_close(&f);
    return t;
}

/* What every row asserts about one route's forward and pool, whichever side
 * of the barrel's test the uncompute lands on. */
static void ax_shift_common(const ax_shift *sh, int W, ax_route route,
                            const ax_srt *r, const char *row)
{
    const int L = ax_stages(W);
    const int k = (int)(AX_SH_VB & (((uint64_t)1 << L) - 1u));   /* D8 mask */
    const uint64_t n_land = sh->sign_fill ? (uint64_t)W
                          : (k < W ? (uint64_t)(W - k) : 0u);

    /* THE FORWARD IS M11's CONSTANT PATH: one CX per lane of `a` that lands
     * in `dst`, and nothing acquired that `dst` does not now hold. */
    CHECK_GATES(r->fwd.x, r->fwd.cx, r->fwd.ccx, 0u, n_land, 0u);
    AX_EXPECT(r->took_fwd == r->dst_q_fwd, "%s: the forward took %u qubits "
              "and dst holds %u — the constant path opens no scratch", row,
              r->took_fwd, r->dst_q_fwd);
    AX_EXPECT(r->fwd_det && r->dst_fwd == sh->ref(AX_SH_VA & cq_ref_mask(W),
                                                  k, W),
              "%s: the forward wrote 0x%llx", row,
              (unsigned long long)r->dst_fwd);
    AX_EXPECT(r->l2_fwd, "%s: L2 after the forward", row);

    AX_EXPECT(r->lane_wire, "%s: the drift left the lane classical", row);
    AX_EXPECT(r->lane_poisoned == (route == AX_RY), "%s: poisoned=%d on the "
              "%s route", row, r->lane_poisoned, AX_ROUTE[route]);
    AX_EXPECT(r->src_restored, "%s: the CX pair did not restore the amount — "
              "`_unc` recomputes from the LIVE source, so that would be a "
              "different function, not a different representation", row);

    AX_EXPECT(r->l2_unc, "%s: L2 after the uncompute — an index is live that "
              "no register owns, or owned and not live", row);
}

CQ_TEST(a_drift_on_an_amount_lane_the_barrel_reads_changes_the_route)
{
    static cq_rec s[2][AX_STREAM_CAP];
    int rows = 0;

    for (int si = 0; si < AX_N_SHIFT; si++)
        for (int wi = 0; wi < AX_N_SH_W; wi++) {
            const ax_shift *sh = &AX_SHIFT[si];
            const int W = AX_SH_W[wi], L = ax_stages(W);

            for (int j = 0; j < L; j++) {
                ax_srt r[2];
                char row[96];

                for (int rt = AX_RY; rt <= AX_CX; rt++) {
                    snprintf(row, sizeof row, "%s W=%d lane %d (%s)",
                             sh->name, W, j, AX_ROUTE[rt]);
                    ax_shift_round_trip(sh, W, j, (ax_route)rt, &r[rt], s[rt],
                                        row);
                    ax_shift_common(sh, W, (ax_route)rt, &r[rt], row);

                    /* THE UNCOMPUTE IS THE SANDWICH. Beyond the dst lanes
                     * its copy-out materialised, it took the barrel's region
                     * — W(3L+1), computed here — and its stream is ordered as
                     * one. */
                    const uint32_t region = (uint32_t)(W * (3 * L + 1));
                    AX_EXPECT(r[rt].took_unc - (r[rt].dst_q_unc -
                                                r[rt].dst_q_fwd) == region,
                              "%s: the uncompute took %u qubits (%u into dst), "
                              "want the W(3L+1) = %u region", row,
                              r[rt].took_unc,
                              r[rt].dst_q_unc - r[rt].dst_q_fwd, region);
                    AX_EXPECT(r[rt].palindrome, "%s: the uncompute stream "
                              "(%zu gates) is not [head][%d copy-out][head "
                              "reversed]", row, r[rt].n_unc, W);

                    /* R6, AS THE INEQUALITY AND WITH A COMPUTED FLOOR. */
                    AX_EXPECT(r[rt].n_unc >= 3u * (size_t)W &&
                              r[rt].n_unc > ax_total(r[rt].fwd),
                              "%s: the uncompute is %zu gates against a "
                              "forward of %llu — the floor is 3W = %d", row,
                              r[rt].n_unc,
                              (unsigned long long)ax_total(r[rt].fwd), 3 * W);

                    ax_tuple fr = ax_shift_fresh(sh, W, j);
                    CHECK_GATES(r[rt].unc.x, r[rt].unc.cx, r[rt].unc.ccx,
                                fr.x, fr.cx, fr.ccx);
                }

                /* THE ORACLE: same circuit on both routes, and only the CX
                 * route's dst can be read — it is zero, so the Ry route's is.
                 * And the price: only the CX route's rail can be freed. */
                snprintf(row, sizeof row, "%s W=%d lane %d", sh->name, W, j);
                AX_EXPECT(r[AX_RY].n_stream == r[AX_CX].n_stream &&
                          r[AX_RY].n_stream > 0u &&
                          ax_first_diff(s[AX_RY], s[AX_CX], r[AX_RY].n_stream)
                              == r[AX_RY].n_stream,
                          "%s: the two routes' uncomputes differ", row);
                AX_EXPECT(r[AX_CX].unc_det && r[AX_CX].dst == 0u,
                          "%s: CX route dst = 0x%llx after the uncompute", row,
                          (unsigned long long)r[AX_CX].dst);
                AX_EXPECT(!r[AX_RY].unc_det, "%s: the Ry route's dst is "
                          "readable, so the poison did not travel", row);
                AX_EXPECT(r[AX_CX].disposition == 1 && r[AX_CX].l3 == 1,
                          "%s: CX route free: disposition %d, L3 %d", row,
                          r[AX_CX].disposition, r[AX_CX].l3);
                AX_EXPECT(r[AX_RY].disposition == 0, "%s: the Ry route's rail "
                          "is %s, want UNPROVEN — it is physically |0> and the "
                          "shadow cannot say so (bd 2cf)", row,
                          r[AX_RY].disposition > 0 ? "proven clean"
                                                   : "proven dirty");
                rows++;
            }
        }

    CHECK_EQ(rows, AX_N_SHIFT * (1 + 2 + 3 + 3));
}

/* THE NEGATIVE CONTROL. A lane at or above L is never a mux control, so the
 * barrel does not scan it (shift_var.c: "THE COUNT IS `L`, NOT `W`") and a
 * drift there must leave the uncompute on the constant path. Without this row
 * a barrel that took the sandwich on ANY quantum amount lane would pass the
 * case above. The claim is the ROUTE — no qubit acquired — and Rule 14's
 * identity; the forward/uncompute equality it implies is not asserted. And
 * the constant path never reads `b`, so even the Ry route's rail stays
 * readable and freeable: the drift costs nothing at the free either. */
CQ_TEST(a_drift_on_an_amount_lane_the_barrel_never_reads_changes_nothing)
{
    static cq_rec s[2][AX_STREAM_CAP];
    int rows = 0;

    for (int si = 0; si < AX_N_SHIFT; si++)
        for (int wi = 0; wi < AX_N_SH_W; wi++) {
            const ax_shift *sh = &AX_SHIFT[si];
            const int W = AX_SH_W[wi], j = ax_stages(W);   /* first unread */

            for (int rt = AX_RY; rt <= AX_CX; rt++) {
                ax_srt r;
                char row[96];

                snprintf(row, sizeof row, "%s W=%d unread lane %d (%s)",
                         sh->name, W, j, AX_ROUTE[rt]);
                ax_shift_round_trip(sh, W, j, (ax_route)rt, &r, s[rt], row);
                ax_shift_common(sh, W, (ax_route)rt, &r, row);

                AX_EXPECT(r.took_unc == 0u, "%s: the uncompute took %u qubits "
                          "for a lane the barrel never reads", row, r.took_unc);
                ax_tuple fr = ax_shift_fresh(sh, W, j);
                CHECK_GATES(r.unc.x, r.unc.cx, r.unc.ccx, fr.x, fr.cx, fr.ccx);
                AX_EXPECT(r.unc_det && r.dst == 0u, "%s: dst = 0x%llx", row,
                          (unsigned long long)r.dst);
                AX_EXPECT(r.disposition == 1 && r.l3 == 1, "%s: free: "
                          "disposition %d, L3 %d", row, r.disposition, r.l3);
                rows++;
            }
        }

    CHECK_EQ(rows, AX_N_SHIFT * AX_N_SH_W * 2);
}
