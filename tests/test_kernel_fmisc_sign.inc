/* tests/test_kernel_fmisc_sign.inc — the SIGN OPS' own instruments. Included
 * by test_kernel_fmisc.c. NOTHING HERE READS A GOLDEN.
 *
 * WHAT CAN GO WRONG IN A ONE-BLOCK KERNEL is which lane a constant lands on
 * (`fneg` flipping bit 62, `copysign` taking its sign from `x`), whether a
 * Toffoli survives the fold, and whether a gate writes anything but `dst`.
 * The value catches the first only on the anchors that exercise it; the lane
 * scan below catches all three structurally, at every mask it runs, by
 * predicting each SLOT's gate from the lane table alone — the upstream block
 * shape plus PRD §3's fold table — and walking the recorded stream with its
 * own cursor.
 */

enum { FM_Q = 2 };   /* a lane kind beside CQ_BIT_ZERO / CQ_BIT_ONE */

/* The slot shapes, transcribed from the pinned source HERE rather than asked
 * of the module, because no module publishes a cost for an inline block and
 * the point is to check the module's own count against the Julia:
 * `lower_xor!` CNOT, CNOT per lane (arith.jl:284-290); `_emit_copy_out!` one
 * CNOT per lane (:89-96); `lower_or!` CNOT, CNOT, Toffoli (:274-282). */
enum { FS_XOR = 2, FS_COPY = 1, FS_OR = 3 };

typedef enum { FM_FNEG = 0, FM_FABS, FM_COPYSIGN, FM_N_SIGN } fm_sign_op;

static const cq_kd_spec *fm_sign_spec(fm_sign_op op)
{
    return op == FM_FNEG ? &SPEC_FNEG : op == FM_FABS ? &SPEC_FABS
                                                      : &SPEC_COPYSIGN;
}

CQ_TEST(the_sign_ops_cost_their_upstream_blocks_slot_for_slot)
{
    CHECK_EQ(cq_fneg_steps(),     FS_XOR  * W64);
    CHECK_EQ(cq_fabs_steps(),     FS_COPY * W64);
    CHECK_EQ(cq_copysign_steps(), FS_OR   * W64);
}

/* A rail lane's kind at a mask: Q where the mask bit is set, else the value. */
static int fm_lane(uint64_t v, cq_ref_w q, int i)
{
    if (cq_ref_w_bit(q, i)) return FM_Q;
    return ((v >> (unsigned)i) & 1u) ? CQ_BIT_ONE : CQ_BIT_ZERO;
}

/* PRD §3 over one gate: a ZERO control removes it, a ONE control demotes it.
 * The TARGET is a `dst` lane and starts CQ_BIT_ZERO: with no quantum control
 * left the gate folds into a constant rewrite of it (nothing emitted), and a
 * quantum-controlled gate on a constant target first materialises it — which
 * emits an X straight to the sink only if the constant is ONE (Rule 5).
 * Appends the predicted ops to `out`; returns how many. */
static int fm_fold(const int *ctrl, int nc, int *tgt, cq_op *out)
{
    int q = 0, n = 0;

    for (int i = 0; i < nc; i++) {
        if (ctrl[i] == CQ_BIT_ZERO) return 0;
        if (ctrl[i] == FM_Q) q++;
    }
    if (q == 0) {
        if (*tgt == FM_Q) out[n++] = CQ_OP_X;
        else *tgt = (*tgt == CQ_BIT_ONE) ? CQ_BIT_ZERO : CQ_BIT_ONE;
        return n;
    }
    if (*tgt == CQ_BIT_ONE) out[n++] = CQ_OP_X;      /* materialise a ONE */
    *tgt = FM_Q;
    out[n++] = (q == 1) ? CQ_OP_CX : CQ_OP_CCX;
    return n;
}

/* The prediction for one op at one (values, masks). `lane[k]` records which
 * dst lane predicted gate k targets. Returns the gate count. */
static int fm_sign_predict(fm_sign_op op, uint64_t va, uint64_t vb,
                           cq_ref_w qa, cq_ref_w qb, cq_op *ops, int *lane)
{
    int n = 0;

    for (int i = 0; i < W64; i++) {
        int tgt = CQ_BIT_ZERO, c[2], before = n, k;
        int sign = (i == W64 - 1);
        int a = fm_lane(va, qa, i);

        switch (op) {
        case FM_FNEG:                          /* CX(a[i]), CX(SIGN[i])      */
            c[0] = a;                               n += fm_fold(c, 1, &tgt, ops + n);
            c[0] = sign ? CQ_BIT_ONE : CQ_BIT_ZERO; n += fm_fold(c, 1, &tgt, ops + n);
            break;
        case FM_FABS:                          /* CX((a & ABS)[i])           */
            c[0] = sign ? CQ_BIT_ZERO : a;          n += fm_fold(c, 1, &tgt, ops + n);
            break;
        default: {                             /* CX(m), CX(s), CCX(m, s)    */
            int m = sign ? CQ_BIT_ZERO : a;
            int s = sign ? fm_lane(vb, qb, i) : CQ_BIT_ZERO;

            c[0] = m;               n += fm_fold(c, 1, &tgt, ops + n);
            c[0] = s;               n += fm_fold(c, 1, &tgt, ops + n);
            c[0] = m; c[1] = s;     n += fm_fold(c, 2, &tgt, ops + n);
            break; }
        }
        for (k = before; k < n; k++) lane[k] = i;
    }
    return n;
}

/* The masks the scan runs at: all-quantum, both asymmetric pairs, and an
 * alternating pair, at values whose sign lanes are SET so a classical lane 63
 * is a ONE (the case that makes `fneg`'s constant X fold). */
CQ_TEST(the_sign_ops_match_an_independent_lane_scan_and_write_only_dst)
{
    static const uint64_t VA = 0xc00921fb54442d18ull;   /* −pi   */
    static const uint64_t VB = 0xbff0000000000001ull;   /* −(1+ulp) */
    cq_ref_w all = cq_ref_w_ones(W64), none = cq_ref_w_zero();
    cq_ref_w alt = cq_ref_w_make(0xaaaaaaaaaaaaaaaaull, 0u, W64);
    cq_ref_w alt2 = cq_ref_w_make(0x5555555555555555ull, 0u, W64);
    const cq_ref_w QA[] = { all, all,  none, alt,  alt2 };
    const cq_ref_w QB[] = { all, none, all,  alt2, alt  };
    static cq_op ops[4 * CQ_FP64_W];
    static int lane[4 * CQ_FP64_W];
    uint32_t dst_q[CQ_FP64_W];
    cq_mock m;
    int any_x = 0;

    cq_mock_init(&m);
    for (int op = 0; op < FM_N_SIGN; op++)
        for (int k = 0; k < 5; k++) {
            const cq_kd_spec *sp = fm_sign_spec((fm_sign_op)op);
            int n = fm_sign_predict((fm_sign_op)op, VA, VB, QA[k], QB[k],
                                    ops, lane);
            uint64_t got = fm_run(sp, VA, VB, QA[k], QB[k], &m, dst_q);
            uint64_t want = op == FM_FNEG ? cq_ref_fneg(VA)
                          : op == FM_FABS ? cq_ref_fabs(VA)
                                          : cq_ref_copysign(VA, VB);

            CHECK_EQ(got, want);
            CHECK_EQ((long long)cq_mock_count(&m), (long long)n);
            for (int g = 0; g < n && g < (int)cq_mock_count(&m); g++) {
                const cq_rec *r = cq_mock_at(&m, (size_t)g);

                if ((int)r->op != (int)ops[g] || r->t != dst_q[lane[g]])
                    cq_h_fail(__FILE__, __LINE__,
                              "%s mask %d gate %d: emitted op %d on q%u, the "
                              "lane scan says op %d on dst lane %d (q%u)",
                              sp->name, k, g, (int)r->op, r->t, (int)ops[g],
                              lane[g], dst_q[lane[g]]);
                any_x += (r->op == CQ_OP_X);
            }
        }
    /* NOT VACUOUS: `fneg`'s sign lane at a quantum `a[63]` is the one X the
     * scan must predict, and it is emitted at the all-quantum mask. */
    CHECK(any_x > 0);
    cq_mock_dispose(&m);
}

/* ---- L5, as a claim about every classical anchor, not one draw. -------- */

static uint64_t fm_sign_ref(fm_sign_op op, uint64_t a, uint64_t b)
{
    return op == FM_FNEG ? cq_ref_fneg(a) : op == FM_FABS ? cq_ref_fabs(a)
                                                          : cq_ref_copysign(a, b);
}

static int fm_sign_full(fm_sign_op op, int i, cq_ref_w *v)
{
    if (op == FM_FNEG) return fm_full_unary(fneg_anchors, i, v);
    if (op == FM_FABS) return fm_full_unary(fabs_anchors, i, v);
    return fm_full_binary(copysign_anchors, i, v);
}

/* THE SIGN OPS HAVE NO SHORT-CIRCUIT AND NEED NONE (K24 D-K24-2): on
 * classical operands every slot is a constant-controlled gate onto a
 * constant `dst` lane, which the fold table rewrites in place. So this is the
 * whole L5 claim for them, over the COMPLETE classical table: zero gates, and
 * `dst` still owns no qubit (I4) — and the value is the oracle's, which makes
 * this also the classical-row-against-the-oracle case. */
CQ_TEST(the_sign_ops_have_a_true_l5_row_on_every_classical_anchor)
{
    cq_mock m;
    uint32_t dst_q[CQ_FP64_W];

    cq_mock_init(&m);
    for (int op = 0; op < FM_N_SIGN; op++) {
        int n = fm_sign_full((fm_sign_op)op, -1, NULL);

        CHECK(n > 8);
        for (int i = 0; i < n; i++) {
            cq_ref_w v[CQ_KD_MAX_SRC];
            uint64_t got, want;

            memset(v, 0, sizeof v);
            CHECK_EQ(fm_sign_full((fm_sign_op)op, i, v), 1);
            want = fm_sign_ref((fm_sign_op)op, v[0].lo, v[1].lo);
            got = fm_run(fm_sign_spec((fm_sign_op)op), v[0].lo, v[1].lo,
                         cq_ref_w_zero(), cq_ref_w_zero(), &m, dst_q);
            if (got != want)
                cq_h_fail(__FILE__, __LINE__, "%s(%016llx, %016llx) = %016llx,"
                          " the oracle says %016llx",
                          fm_sign_spec((fm_sign_op)op)->name,
                          (unsigned long long)v[0].lo,
                          (unsigned long long)v[1].lo,
                          (unsigned long long)got, (unsigned long long)want);
            CHECK_EQ((long long)cq_mock_count(&m), 0);
            for (int l = 0; l < W64; l++) CHECK_EQ(dst_q[l], UINT32_MAX);
        }
    }

    /* The cells a sign op must NOT touch, written down rather than computed:
     * an sNaN stays signalling under all three (IEEE 754 §5.5.1 — quiet
     * operations), and a NaN's payload survives. */
    CHECK_EQ(cq_ref_fneg(CQ_F64_SNAN), 0xfff0000000000001ull);
    CHECK_EQ(cq_ref_fabs(FM_NEG_SNAN), CQ_F64_SNAN);
    CHECK_EQ(cq_ref_fabs(CQ_F64_DEFAULT_NAN), 0x7ff8000000000000ull);
    CHECK_EQ(cq_ref_copysign(CQ_F64_ONE, CQ_F64_NEG_ZERO),
             CQ_F64_NEG(CQ_F64_ONE));
    CHECK_EQ(cq_ref_copysign(CQ_F64_NEG_ZERO, CQ_F64_ONE), CQ_F64_POS_ZERO);
    cq_mock_dispose(&m);
}

/* ---- Zero Toffoli, at every mask pair the fixed set names. ------------- */

/* PRD-v2 §5: "the sign ops are zero-Toffoli". `copysign`'s `lower_or!` DOES
 * carry a Toffoli per lane; it vanishes only because its two controls come
 * from DISJOINT views, so one is a constant ZERO on every lane. An
 * implementation that materialised the views (upstream's `resolve!` shape)
 * would emit 64 of them. */
CQ_TEST(the_sign_ops_are_zero_toffoli_at_every_scanned_mask)
{
    static cq_bk_pair pairs[256];
    uint32_t np = cq_bk_fixed_pairs((uint32_t)W64, pairs, 256u);
    cq_mock m;
    uint64_t total = 0u;

    cq_mock_init(&m);
    CHECK(np > 4u);
    for (int op = 0; op < FM_N_SIGN; op++)
        for (uint32_t p = 0; p < np; p++) {
            uint64_t va = 0xfff0000000000001ull ^ (uint64_t)p * 0x9e3779b97f4a7c15ull;
            uint64_t vb = ~va;
            uint64_t got = fm_run(fm_sign_spec((fm_sign_op)op), va, vb,
                                  pairs[p].q[0], pairs[p].q[1], &m, NULL);

            CHECK_EQ(got, fm_sign_ref((fm_sign_op)op, va, vb));
            CHECK_EQ((long long)m.by_op[CQ_OP_CCX], 0);
            total += cq_mock_count(&m);
        }
    CHECK(total > 0u);
    cq_mock_dispose(&m);
}
