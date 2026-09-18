/* tests/test_kernel_fpfield_views.inc — M31's VIEWS half, and the anchor
 * budget. Included by tests/test_kernel_fpfield.c.
 *
 * A VIEW IS THE ONE THING IN THIS MODULE THAT CANNOT FAIL LOUDLY. It emits no
 * gate and allocates no qubit, so a view addressing the wrong lanes produces a
 * circuit that is well-formed in every structural sense — same slot count,
 * same palindrome, clean scratch, a plausible answer — and is wrong. The
 * measured near-miss is the exponent: lanes 52..62 is `(a >> 52) & 0x7FF`,
 * lanes 52..63 is `a >> 52`, and the two agree on every POSITIVE operand.
 * §7.12's two negative singles (fpanchors.c) are what separate them at L1;
 * these cases separate them one layer down, index for index.
 */

/* One 64-bit rail with a given quantum mask, plus a place to assemble views. */
typedef struct {
    cq_ctx ctx;
    int32_t h;
    cq_bit  v[CQ_FP64_W];
} view_fx;

static void view_open(view_fx *f, const cq_sink *s, cq_ref_w value, cq_ref_w qm)
{
    cq_ctx_init(&f->ctx, s);
    f->h = cq_bk_reg_w(&f->ctx, (uint32_t)CQ_FP64_W, value, qm);
}

/* (a) THE VIEW ITSELF COSTS NOTHING — the claim PRD-v2 §7.10 makes for wiring
 * and the reason there is no `and`(64) block in this module. Measured against
 * both instruments at once: the gate counter says no gate reached a sink, and
 * the pool snapshot says no qubit was minted or freed. */
CQ_TEST(a_view_emits_nothing_and_allocates_nothing)
{
    cq_counter cnt;
    cq_sink s;
    view_fx f;
    cq_pc_snap before, after;
    cq_bit k[CQ_FP64_W];

    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);
    view_open(&f, &s, cq_ref_w_ones(CQ_FP64_W), cq_ref_w_ones(CQ_FP64_W));

    before = cq_pc_take(&f.ctx);
    cq_count_reset(&cnt);            /* drop the rail's materialising Xs */

    cq_fp_view_exp (cq_reg_cbits(&f.ctx.regs, f.h), f.v);
    cq_fp_view_frac(cq_reg_cbits(&f.ctx.regs, f.h), f.v);
    cq_fp_view_sign(cq_reg_cbits(&f.ctx.regs, f.h), f.v);
    cq_fp_const_frac_mask(k);
    cq_fp_const_indef(k);

    after = cq_pc_take(&f.ctx);
    CHECK_EQ(cq_count_total(&cnt), 0u);
    CHECK(cq_pc_same(before, after));

    cq_ctx_dispose(&f.ctx);
}

/* (a) THE LANES, INDEX FOR INDEX AGAINST THE REGISTER. Not "the value is
 * right" — a view holds no value of its own — but "lane i of the view IS lane
 * lo+i of the rail, and the fill is CQ_BIT_ZERO". Compared on the qubit INDEX,
 * which is what makes it a statement about addressing rather than about bits.
 *
 * DELETE THE `lo` OFFSET FROM cq_fp_view AND THIS IS THE ONLY CASE THAT GOES
 * RED in this file; the exponent row is also what an 11-vs-12 lane error
 * moves, and at L1 it is the two negative anchors. */
static void view_lanes(const cq_bit *a, const cq_bit *v, int lo, int n,
                       const char *what)
{
    for (int i = 0; i < n; i++)
        if (!cq_bit_is_qubit(v[i]) ||
            cq_bit_qindex(v[i]) != cq_bit_qindex(a[lo + i]))
            cq_h_fail(__FILE__, __LINE__,
                      "%s view lane %d: wanted rail lane %d", what, i, lo + i);

    for (int i = n; i < CQ_FP64_W; i++)
        if (!cq_bit_is_zero(v[i]))
            cq_h_fail(__FILE__, __LINE__,
                      "%s view lane %d is not the CQ_BIT_ZERO fill", what, i);
}

CQ_TEST(a_view_addresses_the_lanes_the_source_names)
{
    cq_counter cnt;
    cq_sink s;
    view_fx f;
    const cq_bit *a;

    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);
    view_open(&f, &s, cq_ref_w_zero(), cq_ref_w_ones(CQ_FP64_W));
    a = cq_reg_cbits(&f.ctx.regs, f.h);

    /* fadd.jl:21 — ELEVEN lanes from 52, so lane 63 (the sign) is NOT in it. */
    cq_fp_view_exp(a, f.v);
    view_lanes(a, f.v, CQ_FP64_EXP_LO, CQ_FP64_EXP_W, "exponent");
    CHECK_EQ(CQ_FP64_EXP_W, 11);

    cq_fp_view_frac(a, f.v);                       /* fadd.jl:22 */
    view_lanes(a, f.v, CQ_FP64_FRAC_LO, CQ_FP64_FRAC_W, "fraction");

    cq_fp_view_sign(a, f.v);                       /* fadd.jl:20 */
    view_lanes(a, f.v, CQ_FP64_SIGN_LO, 1, "sign");

    /* The raw sub-spans are the same addressing by another door. */
    CHECK(cq_fp_span_exp (a) == &a[CQ_FP64_EXP_LO]);
    CHECK(cq_fp_span_frac(a) == &a[CQ_FP64_FRAC_LO]);
    CHECK(cq_fp_span_sign(a) == &a[CQ_FP64_SIGN_LO]);

    cq_ctx_dispose(&f.ctx);
}

/* (a) A view over a CLASSICAL rail is all constant — which is what makes the
 * R9 short-circuit reachable at all, and what makes I4 survive: an
 * all-constant rail owns zero qubits and a view of it owns zero too. */
CQ_TEST(a_view_over_a_classical_rail_is_all_constant)
{
    cq_counter cnt;
    cq_sink s;
    view_fx f;
    const cq_bit *a;

    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);
    view_open(&f, &s, cq_ref_w_make(CQ_F64_NEG_INF, 0u, CQ_FP64_W),
              cq_ref_w_zero());
    a = cq_reg_cbits(&f.ctx.regs, f.h);

    cq_fp_view_exp(a, f.v);
    for (int i = 0; i < CQ_FP64_W; i++) CHECK(cq_bit_is_const(f.v[i]));

    /* And it holds `(−Inf >> 52) & 0x7FF` = 0x7FF, which is the value the
     * classical row's `ea == 0x7FF` reads. The sign bit is NOT in it: with a
     * twelve-lane view this would be 0xFFF. */
    CHECK_EQ(cq_fp_pack(f.v), CQ_FP64_EXP_ALL);

    cq_fp_view_frac(a, f.v);
    CHECK_EQ(cq_fp_pack(f.v), 0u);

    cq_ctx_dispose(&f.ctx);
}

/* (a) The seven softfloat_common.jl constants, as spans. A transcribed table
 * of bit patterns needs an instrument — the same argument fpanchors.c makes
 * for §7.12's values and PRD §15 D11 made for its phase table. */
CQ_TEST(the_constant_spans_are_the_softfloat_patterns)
{
    cq_bit k[CQ_FP64_W];

    cq_fp_const_frac_mask(k); CHECK_EQ(cq_fp_pack(k), CQ_FP64_FRAC_MASK);
    cq_fp_const_implicit (k); CHECK_EQ(cq_fp_pack(k), CQ_FP64_IMPLICIT);
    cq_fp_const_exp_mask (k); CHECK_EQ(cq_fp_pack(k), CQ_FP64_EXP_MASK);
    cq_fp_const_inf_bits (k); CHECK_EQ(cq_fp_pack(k), CQ_FP64_INF_BITS);
    cq_fp_const_qnan     (k); CHECK_EQ(cq_fp_pack(k), CQ_FP64_QNAN);
    cq_fp_const_quiet_bit(k); CHECK_EQ(cq_fp_pack(k), CQ_FP64_QUIET_BIT);
    cq_fp_const_indef    (k); CHECK_EQ(cq_fp_pack(k), CQ_FP64_INDEF);
    cq_fp_const_zero     (k); CHECK_EQ(cq_fp_pack(k), 0u);
    cq_fp_const_exp_all  (k); CHECK_EQ(cq_fp_pack(k), CQ_FP64_EXP_ALL);

    /* softfloat_common.jl:8-14's own arithmetic identities, so a transposed
     * digit in one constant is caught by its neighbour rather than only by a
     * reader. IMPLICIT is the bit just above FRAC_MASK; QNAN is INF_BITS with
     * the quiet bit; INDEF is QNAN with the sign. */
    CHECK_EQ(CQ_FP64_IMPLICIT, CQ_FP64_FRAC_MASK + 1u);
    CHECK_EQ(CQ_FP64_QNAN, CQ_FP64_INF_BITS | CQ_FP64_QUIET_BIT);
    CHECK_EQ(CQ_FP64_INDEF, CQ_FP64_QNAN | CQ_F64_SIGN_MASK);
    CHECK_EQ(CQ_FP64_EXP_MASK, CQ_FP64_EXP_ALL << CQ_FP64_EXP_LO);
}

/* (d) WHAT THE ANCHOR BUDGET ACTUALLY DELIVERS, ASSERTED AND PRINTED.
 *
 * The sampler forces anchors ROW-MAJOR BY MASK ROW and truncates at
 * `budget - 2` (kernelsweep.c), so with fifteen singles and three mask rows it
 * wants 45 while the 32-sample default has room for 30 — one whole mask row
 * DROPPED. bd 9ve.32's per-shape floor is what stops that, and `fp_shape`
 * reads the figure off the provider rather than writing it down, so the claim
 * asserted here is `0 DROPPED` and not a number.
 *
 * THE ENVIRONMENT WINS IN BOTH DIRECTIONS, so the assertion is made only on
 * the rows where the floor is in force. `CQOPS_L1_SAMPLES=8` is a maintainer
 * asking for eight cases and must still get eight; a case that demanded full
 * anchor coverage there would make the knob un-bisectable, which is the thing
 * 9ve.32's own precedence note refuses.
 *
 * AND THE ANCHORS ARE RUN HERE DIRECTLY, EVERY ONE OF THEM, AT TWO MASKS — so
 * the coverage does not depend on the budget at all and a future edit to the
 * sampler cannot silently take it away. */
CQ_TEST(every_anchor_reaches_the_kernel_and_the_drop_is_printed)
{
    cq_bk_pair pairs[CQ_FP64_W + 12];
    uint32_t np = cq_bk_fixed_pairs((uint32_t)CQ_FP64_W, pairs,
                                    (uint32_t)(CQ_FP64_W + 12));
    cq_kd_shape sh;
    const char *src = "";
    int n_anch = cq_fp_anchors_unary(CQ_FP64_W, -1, NULL);
    int budget, room, want, have;

    fp_shape(CQ_FP64_W, &sh);
    budget = cq_kd_budget(sh.min_samples, &src);
    room   = budget > 2 ? budget - 2 : 0;
    want   = n_anch * 3;
    have   = want < room ? want : room;

    CHECK_EQ(n_anch, cq_fp_anchors_unary_count());
    CHECK(n_anch > 0);
    CHECK_EQ(cq_fp_anchors_unary(8, -1, NULL), 0);   /* f64 only */

    /* The floor TRACKS the provider: adding a single to fpanchors.c must widen
     * the budget with no edit here, so this is an identity and not a literal. */
    CHECK_EQ(sh.min_samples, 3 * n_anch + 8);

    printf("# fp class anchors: %d singles x 3 mask rows = %d wanted, %d run "
           "through the sampler, %d DROPPED at budget %d [%s, shape floor %d] "
           "— and all %d are run explicitly below at two masks\n",
           n_anch, want, have, want - have, budget, src, sh.min_samples,
           n_anch);
    fflush(stdout);

    /* 0 DROPPED — the whole point of the floor. */
    if (strcmp(src, "env") != 0) CHECK_EQ(have, want);

    for (int i = 0; i < N_FP_ROWS; i++)
        for (int j = 0; j < n_anch; j++) {
            cq_ref_w v[CQ_KD_MAX_SRC];

            v[0] = cq_ref_w_zero();
            v[1] = cq_ref_w_zero();
            v[2] = cq_ref_w_zero();
            CHECK(cq_fp_anchors_unary(CQ_FP64_W, j, v) == 1);

            cq_kd_case(&ROWS[i].spec, CQ_FP64_W, v, &pairs[1]);   /* all-quantum */
            cq_kd_case(&ROWS[i].spec, CQ_FP64_W, v, &pairs[0]);   /* all-classical */
        }

    CHECK(np >= 2u);
}
