/* tests/test_kerneldrv_anchors.inc — bd hkg. The ANCHOR mechanism, made
 * falsifiable, in the file whose whole subject is falsifying the driver.
 *
 * INCLUDED FROM tests/test_kerneldrv.c rather than written into it because
 * that file is at Rule 12's edge (242 countable lines before this landed) and
 * plan §3 says a split is scheduled, never a surprise. An .inc is exempt from
 * the LOC guard by design — "Rule 12 counts .c/.h/.py only, which is what
 * makes 'move the big static table into an .inc' a real escape hatch".
 *
 * FIVE CLAIMS, and each one names the single case that goes red if it is
 * deleted (the question CLAUDE.md says to ask before adding a guard):
 *
 *   1. an anchor is FORCED — a kernel wrong at exactly one 64-bit pattern is
 *      REFUSED with anchors and ACCEPTED without them, at the same seed.
 *   2. the schedule is the one kerneldrv.h documents — anchor index and mask
 *      row, slot by slot, row-major BY ROW.
 *   3. a provider OVERWRITES the operands it names and the sampler does NOT
 *      zero-fill the rest — the cq_kd_case2 trap, re-armed for fp.
 *   4. the budget is UNCHANGED: anchors sit inside cq_kd_samples(), never on
 *      top of it, including when the table is three times too big for it.
 *   5. the f64 constants are what they claim, by FIELD rather than by eye.
 */

/* ---- A kernel that is wrong at exactly one value. ----------------------- */

/* The classical value of an operand, and whether it HAS one. A kernel can only
 * read a lane it can see: at the all-classical mask row both operands are
 * constants, at a-quantum/b-classical only `b` is, and at the all-quantum mask
 * neither is. That asymmetry is the point of the provocation below — an fp
 * value reaches a kernel's special-case tree through the CLASSICAL lanes, which
 * is exactly where a 32-sample uniform draw never goes. */
static uint64_t anch_const_value(const cq_bit *p, int W, int *have)
{
    uint64_t v = 0u;

    *have = 1;
    for (int i = 0; i < W && i < 64; i++) {
        if (!cq_bit_is_const(p[i])) { *have = 0; return 0u; }
        if (cq_bit_value(p[i])) v |= (uint64_t)1 << i;
    }
    return v;
}

/* THE PROVOCATION. A correct xor everywhere except when `a` is classical and
 * holds the default f64 NaN, where it also flips dst[0] — one wrong bit, one
 * value, nothing else disturbed. L5 stays green: cq_emit_x on a constant
 * target rewrites it in place for zero gates and zero qubits, so the extra flip
 * costs nothing and only L1 can see it. */
static void k_wrong_at_one_anchor(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                                  const cq_bit *b, int W)
{
    int have;
    uint64_t va = anch_const_value(a, W, &have);

    cq_kernel_xor(ctx, dst, a, b, W);
    if (have && va == CQ_F64_DEFAULT_NAN) cq_emit_x(ctx, &dst[0]);
}

/* One anchor, naming exactly that value. The second operand is named too —
 * a provider that left it to the draw would still work here, and naming it
 * keeps the case reproducible independently of the RNG. */
static int probe_anchor(int W, int i, cq_ref_w *v)
{
    if (W != 64) return 0;
    if (i < 0) return 1;
    if (i != 0 || v == NULL) return 0;

    v[0] = cq_ref_w_make(CQ_F64_DEFAULT_NAN, 0u, 64);
    v[1] = cq_ref_w_make(CQ_F64_ONE, 0u, 64);
    return 1;
}

static void shape_with_probe_anchor(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->anchors = probe_anchor;
}

/* The negative control's shape. A shape function that sets NOTHING, so the two
 * legs differ in the anchors and in nothing else — not in the spec name (and
 * therefore not in the seed), not in the widths, not in the mask pool. */
static void shape_without_anchors(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
}

CQ_TEST(an_anchor_is_forced_and_a_drawn_value_is_not)
{
    const cq_kd_spec anchored = { "anchor-probe", k_wrong_at_one_anchor,
                                  cq_ref_xor, shape_with_probe_anchor,
                                  NULL, NULL };
    const cq_kd_spec drawn    = { "anchor-probe", k_wrong_at_one_anchor,
                                  cq_ref_xor, shape_without_anchors,
                                  NULL, NULL };

    /* The claim in the DECLINE direction first: at the default budget and the
     * seed this kernel's own name gives, thirty-two draws do not reach one
     * named 64-bit pattern. MEASURED, not argued — if a future seed change
     * made this leg red the pattern would have to move, and the case says so
     * rather than quietly becoming vacuous. */
    CQ_EXPECT_CLEAN("32 drawn value pairs at W=64 never reach one named "
                    "64-bit pattern — the whole reason an anchor exists",
                    cq_kd_sample_at(&drawn, 64));

    /* And the same kernel, the same seed, the same everything but the anchor. */
    CQ_EXPECT_CAUGHT("a kernel wrong ONLY at 0xfff8000000000000, with that "
                     "value declared as an anchor",
                     cq_kd_sample_at(&anchored, 64));
}

/* ---- The schedule: which anchor, at which mask row, in which slot. ------ */

/* Three anchors, each spelled as a distinguishable small integer in BOTH
 * operands — so whichever operand a given mask row leaves classical still
 * identifies the anchor. Without that the a-quantum row would be unreadable
 * and the schedule could only be half checked. */
static int three_anchors(int W, int i, cq_ref_w *v)
{
    if (W != 8) return 0;
    if (i < 0) return 3;
    if (i >= 3 || v == NULL) return 0;

    v[0] = cq_ref_w_make((uint64_t)(i + 1), 0u, 8);
    v[1] = cq_ref_w_make((uint64_t)(i + 1), 0u, 8);
    return 1;
}

static void shape_three_anchors(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->anchors = three_anchors;
}

/* One row per KERNEL CALL, so each case appears twice (forward, then the L3
 * uncompute) — asserted rather than divided out, because a driver that stopped
 * running the uncompute would otherwise pass this case unchanged.
 *
 * BOTH operand values are recorded, not just "whichever one was readable".
 * The second case below asks what happened to an operand the PROVIDER DID NOT
 * NAME, and a log that collapsed the two could not answer it. */
enum { ANCH_LOG_MAX = 256 };
static struct { int row, va, vb; } anch_log[ANCH_LOG_MAX];   /* -1: quantum */
static int anch_n;

/* row: 0 both operands classical, 1 only `a` quantum, 2 only `b` quantum,
 * 3 anything else (which is what a random draw's mask row looks like). */
static void k_logs_the_row(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                           const cq_bit *b, int W)
{
    int ha, hb;
    uint64_t va = anch_const_value(a, W, &ha);
    uint64_t vb = anch_const_value(b, W, &hb);

    /* anch_n counts EVERY call and the array holds the first ANCH_LOG_MAX, so
     * the case-count assertion below is honest at any CQOPS_L1_SAMPLES rather
     * than silently becoming "the log filled up". */
    if (anch_n < ANCH_LOG_MAX) {
        anch_log[anch_n].row = ha && hb ? 0 : (hb ? 1 : (ha ? 2 : 3));
        anch_log[anch_n].va  = ha ? (int)va : -1;
        anch_log[anch_n].vb  = hb ? (int)vb : -1;
    }
    anch_n++;
    cq_kernel_xor(ctx, dst, a, b, W);
}

CQ_TEST(the_anchor_schedule_is_row_major_by_row)
{
    const cq_kd_spec k = { "anchor-schedule", k_logs_the_row, cq_ref_xor,
                           shape_three_anchors, NULL, NULL };
    /* kerneldrv.h's schedule at A=3: slots 2..10 are every anchor at the
     * all-classical row, then every anchor at a-quantum/b-classical, then
     * every anchor at a-classical/b-quantum. */
    static const int WANT_ROW[9] = { 0, 0, 0, 1, 1, 1, 2, 2, 2 };
    static const int WANT_IDX[9] = { 1, 2, 3, 1, 2, 3, 1, 2, 3 };

    /* The schedule below reads slots 2..10, so it needs a budget of at least
     * 11. CQOPS_L1_SAMPLES is documented for RAISING depth; asserted rather
     * than assumed, because the log is a zeroed static and a budget of 2 would
     * otherwise make the first expectation pass against a slot nobody ran. */
    CHECK(cq_kd_samples() >= 11);

    anch_n = 0;
    CQ_EXPECT_CLEAN("a correct kernel under a three-anchor schedule",
                    cq_kd_sample_at(&k, 8));

    /* Two calls per case, and the budget is untouched by the anchors. */
    CHECK_EQ(anch_n, 2 * cq_kd_samples());

    for (int j = 0; j < 9; j++) {
        const int slot = 2 + j;

        /* The forward call and the uncompute see the SAME operands, so the two
         * entries of a slot must agree — a schedule read off only the forward
         * would not notice a driver that re-drew between the two. */
        const int got  = anch_log[2 * slot].va >= 0 ? anch_log[2 * slot].va
                                                    : anch_log[2 * slot].vb;
        const int got2 = anch_log[2 * slot + 1].va >= 0
                             ? anch_log[2 * slot + 1].va
                             : anch_log[2 * slot + 1].vb;

        CHECK_EQ(anch_log[2 * slot].row,     WANT_ROW[j]);
        CHECK_EQ(got,                        WANT_IDX[j]);
        CHECK_EQ(anch_log[2 * slot + 1].row, WANT_ROW[j]);
        CHECK_EQ(got2,                       WANT_IDX[j]);
    }

    /* And slots 0 and 1 are NOT anchors: the all-classical row that IS L5 and
     * the all-quantum row every L4 golden is pinned at keep their places at the
     * head of the budget, which is what "inside the budget" has always meant. */
    CHECK_EQ(anch_log[0].row, 0);
    CHECK_EQ(anch_log[2].row, 3);
}

/* ---- A provider OVERWRITES; it does not FILL. --------------------------- */

/* Names operand 0 only, and leaves operand 1 to the draw. That is the whole
 * contract kerneldrv.h states, and it is the recorded cq_kd_case2 trap wearing
 * a new costume: a sampler that zero-filled the operands a provider did not
 * name would run every anchor case with the other arm pinned at 0 — green, a
 * plausible case count, and half a kernel. M39's `fma` is the first three-source
 * fp kernel and would hit it on day one. */
static int anchor_names_only_a(int W, int i, cq_ref_w *v)
{
    if (W != 8) return 0;
    if (i < 0) return 1;
    if (i != 0 || v == NULL) return 0;

    v[0] = cq_ref_w_make(0xAAu, 0u, 8);
    return 1;
}

static void shape_names_only_a(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->anchors = anchor_names_only_a;
}

CQ_TEST(an_unnamed_operand_is_still_drawn_and_not_zero_filled)
{
    const cq_kd_spec k = { "anchor-partial", k_logs_the_row, cq_ref_xor,
                           shape_names_only_a, NULL, NULL };

    CHECK(cq_kd_samples() >= 4);   /* this case reads slots 2 and 3 */

    anch_n = 0;
    CQ_EXPECT_CLEAN("one anchor naming only operand 0",
                    cq_kd_sample_at(&k, 8));

    /* Slot 2 is the anchor at the all-classical row, so BOTH operands are
     * readable there and slot 3 is the same anchor at a-quantum/b-classical. */
    CHECK_EQ(anch_log[4].row, 0);
    CHECK_EQ(anch_log[4].va, 0xAA);     /* the provider DID write operand 0 */
    CHECK_EQ(anch_log[6].row, 1);
    CHECK_EQ(anch_log[6].va, -1);

    /* AND THE OPERAND IT DID NOT NAME IS A DRAW. Deterministic, not flaky: the
     * seed is a pure function of the spec's name and the width, so "these two
     * drawn bytes are not both zero" is a fact about a fixed stream, measured
     * once. A zero-filling sampler makes both of them 0. */
    CHECK(anch_log[4].vb != 0 || anch_log[6].vb != 0);
}

/* ---- The budget wins, and the real table is bigger than it. ------------- */

static int anch_calls;

static void k_counts_calls(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                           const cq_bit *b, int W)
{
    anch_calls++;
    cq_kernel_xor(ctx, dst, a, b, W);
}

static void shape_fp_anchors(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->anchors = cq_fp_anchors_binary;
}

CQ_TEST(anchors_sit_inside_the_budget_and_never_on_top_of_it)
{
    const cq_kd_spec with = { "anchor-budget-with", k_counts_calls, cq_ref_xor,
                              shape_fp_anchors, NULL, NULL };
    const cq_kd_spec without = { "anchor-budget-without", k_counts_calls,
                                 cq_ref_xor, NULL, NULL, NULL };
    int n_with, n_without;

    /* §7.12's list is 25 ordered pairs and the schedule replays each at three
     * mask rows, so at the default budget of 32 the block WANTS 75 slots and
     * gets 30. That over-subscription is the case: if anchors were added to
     * the budget rather than taken out of it, this leg would run 77 cases. */
    CHECK(3 * cq_fp_anchors_binary_count() > cq_kd_samples());

    anch_calls = 0;
    CQ_EXPECT_CLEAN("the real f64 table, three times over budget",
                    cq_kd_sample_at(&with, 64));
    n_with = anch_calls;

    anch_calls = 0;
    CQ_EXPECT_CLEAN("the same kernel with no anchors at all",
                    cq_kd_sample_at(&without, 64));
    n_without = anch_calls;

    CHECK_EQ(n_with, 2 * cq_kd_samples());
    CHECK_EQ(n_without, 2 * cq_kd_samples());
    CHECK_EQ(n_with, n_without);
}

/* ---- The table is what it claims, by field. ----------------------------- */

CQ_TEST(the_f64_anchor_constants_are_what_they_claim)
{
    cq_ref_w v[CQ_KD_MAX_SRC];

    /* Inf: exponent all ones, significand ZERO. NaN: exponent all ones,
     * significand NON-zero — and the quiet bit separates the two NaN kinds. */
    CHECK_EQ(CQ_F64_POS_INF & CQ_F64_EXP_MASK,  CQ_F64_EXP_MASK);
    CHECK_EQ(CQ_F64_POS_INF & CQ_F64_MANT_MASK, 0u);
    CHECK_EQ(CQ_F64_NEG_INF, CQ_F64_NEG(CQ_F64_POS_INF));

    CHECK_EQ(CQ_F64_DEFAULT_NAN & CQ_F64_EXP_MASK, CQ_F64_EXP_MASK);
    CHECK(  (CQ_F64_DEFAULT_NAN & CQ_F64_MANT_MASK) != 0u);
    CHECK(  (CQ_F64_DEFAULT_NAN & CQ_F64_QUIET_BIT) != 0u);
    /* x86's INDEF is NEGATIVE; ARM's default NaN is positive. This is the cell
     * PRD-v2 §7.4 pins by table rather than by host measurement. */
    CHECK(  (CQ_F64_DEFAULT_NAN & CQ_F64_SIGN_MASK) != 0u);

    CHECK(  (CQ_F64_QNAN_A & CQ_F64_QUIET_BIT) != 0u);
    CHECK(  (CQ_F64_QNAN_B & CQ_F64_QUIET_BIT) != 0u);
    CHECK(   CQ_F64_QNAN_A != CQ_F64_QNAN_B);   /* a swap must be visible */

    /* An sNaN is the one that is easy to spell as an infinity by accident. */
    CHECK_EQ(CQ_F64_SNAN & CQ_F64_EXP_MASK, CQ_F64_EXP_MASK);
    CHECK_EQ(CQ_F64_SNAN & CQ_F64_QUIET_BIT, 0u);
    CHECK(  (CQ_F64_SNAN & CQ_F64_MANT_MASK) != 0u);

    /* THE DEGENERATE PAIR, PINNED AS AN IDENTITY SO IT CANNOT COME BACK. The
     * propagation rule is "the first operand, quietened if signalling"
     * (`_sf_propagate_nan2`, softfloat_common.jl:23-24), and quietening THIS
     * sNaN lands exactly on QNAN_A — so an sNaN/QNAN_A anchor prints the same
     * bits under that rule and under the rival "the qNaN wins" reading, which
     * is how PRD-v2 §7.4's row came to be wrong (bd 9ve.4). The table must
     * therefore pair the sNaN against QNAN_B, and this asserts BOTH halves:
     * that the degeneracy is real, and that the table avoided it. */
    CHECK_EQ(CQ_F64_SNAN_QUIET, CQ_F64_SNAN | CQ_F64_QUIET_BIT);
    CHECK_EQ(CQ_F64_SNAN_QUIET, CQ_F64_QNAN_A);
    CHECK(   CQ_F64_SNAN_QUIET != CQ_F64_QNAN_B);
    for (int i = 0; i < cq_fp_anchors_binary_count(); i++) {
        CHECK_EQ(cq_fp_anchors_binary(64, i, v), 1);
        CHECK(!(v[0].lo == CQ_F64_SNAN && v[1].lo == CQ_F64_QNAN_A));
        CHECK(!(v[0].lo == CQ_F64_QNAN_A && v[1].lo == CQ_F64_SNAN));
    }

    /* The subnormal band: exponent field zero, significand non-zero; and the
     * smallest normal is the next pattern up, 1 << 52. */
    CHECK_EQ(CQ_F64_MIN_SUBNORMAL, 1u);
    CHECK_EQ(CQ_F64_MAX_SUBNORMAL, CQ_F64_MANT_MASK);
    CHECK_EQ(CQ_F64_MIN_SUBNORMAL & CQ_F64_EXP_MASK, 0u);
    CHECK_EQ(CQ_F64_MAX_SUBNORMAL & CQ_F64_EXP_MASK, 0u);
    CHECK_EQ(CQ_F64_MIN_NORMAL, (uint64_t)1 << 52);
    CHECK_EQ(CQ_F64_MIN_NORMAL, CQ_F64_MAX_SUBNORMAL + 1u);
    CHECK_EQ(CQ_F64_MAX, CQ_F64_POS_INF - 1u);

    /* The tie-to-even witnesses. 1.0's trailing significand bit is EVEN and
     * 1+2^-52's is ODD, which is the whole of "a tie in each direction"; the
     * addend is exactly half an ulp of 1.0, i.e. 53 binades down. */
    CHECK_EQ(CQ_F64_ONE_PLUS_ULP, CQ_F64_ONE + 1u);
    CHECK_EQ(CQ_F64_ONE & 1u, 0u);
    CHECK_EQ(CQ_F64_ONE_PLUS_ULP & 1u, 1u);
    CHECK_EQ((CQ_F64_ONE >> 52) - (CQ_F64_HALF_ULP_OF_1 >> 52), 53u);
    CHECK_EQ(CQ_F64_HALF_ULP_OF_1 & CQ_F64_MANT_MASK, 0u);

    CHECK_EQ(CQ_F64_NEG(CQ_F64_POS_ZERO), CQ_F64_NEG_ZERO);

    /* The provider's own contract: a count at f64, nothing anywhere else, and
     * a refusal outside the range it declared. */
    CHECK_EQ(cq_fp_anchors_binary(64, -1, NULL), cq_fp_anchors_binary_count());
    CHECK_EQ(cq_fp_anchors_binary(32, -1, NULL), 0);
    CHECK_EQ(cq_fp_anchors_binary(8,  -1, NULL), 0);
    CHECK_EQ(cq_fp_anchors_binary(64, cq_fp_anchors_binary_count(), v), 0);
    CHECK_EQ(cq_fp_anchors_binary(64, 0, v), 1);
    CHECK_EQ(v[0].lo, CQ_F64_POS_ZERO);
    CHECK_EQ(v[1].lo, CQ_F64_POS_ZERO);
    CHECK_EQ(v[0].hi, 0u);
}
