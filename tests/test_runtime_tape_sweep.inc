/* tests/test_runtime_tape_sweep.inc — the SWEEP half of tests/test_runtime_tape.c,
 * split on the seam recorded for it in IMPLEMENTATION_PLAN §3's Step 28 row
 * before the file was first measured: `the SWEEP ↔ the ROWS`. This half owns
 * the width-dispatched ABI helpers, the seeded draw and the two L1/L2/L3 sweep
 * cases; the .c keeps the token, the L5 rows, the promotion tuple, the recorder
 * and the corpus shape. Included by the .c and inheriting its includes and its
 * open_with/close_with; out of check_loc.sh's reach by the EXTENSION rule,
 * which is what tests/test_kernel_*_sweep.inc already rely on.
 */

/* --- the ABI, dispatched by width ------------------------------------------ */

/* THE LADDER IS THE ABI's: `widthHasReversibleCopy` is {i1, i8, i16, i32, i64},
 * there is no i128 tape write, and an fp `%f` argument declines in the pass. */
static const uint32_t LADDER[5] = { 1u, 8u, 16u, 32u, 64u };

static uint64_t wmask(uint32_t w)
{
    return (w >= 64u) ? ~UINT64_C(0) : ((UINT64_C(1) << w) - 1u);
}

static int32_t tape_write(uint32_t w, int32_t t, int32_t src)
{
    switch (w) {
    case 1u:  return cqrt_tape_write_i1 (t, src);
    case 8u:  return cqrt_tape_write_i8 (t, src);
    case 16u: return cqrt_tape_write_i16(t, src);
    case 32u: return cqrt_tape_write_i32(t, src);
    default:  return cqrt_tape_write_i64(t, src);
    }
}

static int32_t tape_write_ctrl(uint32_t w, int32_t c, int32_t t, int32_t src)
{
    switch (w) {
    case 1u:  return cqrt_tape_write_i1_controlled (c, t, src);
    case 8u:  return cqrt_tape_write_i8_controlled (c, t, src);
    case 16u: return cqrt_tape_write_i16_controlled(c, t, src);
    case 32u: return cqrt_tape_write_i32_controlled(c, t, src);
    default:  return cqrt_tape_write_i64_controlled(c, t, src);
    }
}

static void copy_w(uint32_t w, int32_t src, int32_t dst)
{
    switch (w) {
    case 1u:  cqrt_copy_i1 (src, dst); break;
    case 8u:  cqrt_copy_i8 (src, dst); break;
    case 16u: cqrt_copy_i16(src, dst); break;
    case 32u: cqrt_copy_i32(src, dst); break;
    default:  cqrt_copy_i64(src, dst); break;
    }
}

static int32_t alloc_zero_w(uint32_t w)
{
    switch (w) {
    case 1u:  return cqrt_alloc_i1 (false);
    case 8u:  return cqrt_alloc_i8 (0);
    case 16u: return cqrt_alloc_i16(0);
    case 32u: return cqrt_alloc_i32(0);
    default:  return cqrt_alloc_i64(0);
    }
}

static unsigned popcount64(uint64_t v)
{
    unsigned n = 0u;
    for (; v; v &= v - 1u) n++;
    return n;
}

/* FNV-1a over the family name, XOR the width — the L1 seed rule (Rule 10). */
static uint64_t seed_for(const char *name, uint32_t w)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (; *name; name++) { h ^= (uint8_t)*name; h *= UINT64_C(0x100000001b3); }
    return h ^ (uint64_t)w;
}

/* -------------------------------------------------------------------------
 * 2. L1 + L2 + L3(src), over random masks at the sample budget.
 * ------------------------------------------------------------------------- */

/* One draw. `a` carries the value at the drawn mask; `src` is an ABI rail that
 * receives it by `cqrt_copy`, so that its history is a self-adjoint pair the
 * certificate can discharge; the tape write sits between the two copies. */
static void one_draw(uint32_t w, uint64_t v, uint64_t q, int ctrl)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    int32_t hs[4];
    uint32_t src_idx[64];
    const int32_t t   = cqrt_tape_alloc();
    const int32_t a   = cq_bk_reg(ctx, w, v, q);
    const int32_t src = alloc_zero_w(w);
    int32_t flag = CQ_REG_NONE, out;
    uint32_t n_src;

    copy_w(w, a, src);                       /* src := v, wires exactly at q */
    if (ctrl) flag = cq_bk_reg(ctx, 1u, 1u, 1u);       /* CQ_BIT_Q, value 1 */
    cq_mock_reset(&m);

    out = ctrl ? tape_write_ctrl(w, flag, t, src) : tape_write(w, t, src);

    /* L1: the kept rail holds the source's value and the source still does. */
    CHECK_EQ(cq_pc_value(ctx, out), v);
    CHECK_EQ(cq_pc_value(ctx, src), v);
    CHECK_EQ(cq_reg_width(&ctx->regs, out), w);

    /* THE COST IS THE COPY'S, per source WIRE: one CX uncontrolled, one CCX
     * under a quantum flag, and NOTHING on a classical lane — the ONE lanes of
     * `src` are constants after the copy folded them, so under no control they
     * rewrite `out`'s constant in place, and under a quantum flag they cost one
     * CX each (the constant row cannot fire inside a promoted region). */
    if (!ctrl) {
        CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX),  popcount64(q));
        CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CCX), 0);
        CHECK_EQ(cq_mock_count_op(&m, CQ_OP_X),   0);
        CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, out), popcount64(q));
    } else {
        CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CCX), popcount64(q));
        CHECK_EQ(cq_mock_count_op(&m, CQ_OP_CX),  popcount64(v & ~q));
        CHECK_EQ(cq_mock_count_op(&m, CQ_OP_X),   0);
        CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, out),
                 popcount64(q) + popcount64(v & ~q));
        CHECK_EQ(cq_ctrl_depth(ctx), 0);
    }

    /* L2, as the SET, after the write. */
    hs[0] = a; hs[1] = src; hs[2] = out; hs[3] = flag;
    CHECK(cq_pc_live_is_exactly(ctx, hs, ctrl ? 4u : 3u));
    cq_reg_audit(ctx);

    /* L3 ON THE SOURCE: undo the copy and free it. CLEAN only if the write was
     * recorded as a READ of `src` — see the file header. The kept rail is
     * untouched by any of this and is still live at the end. */
    copy_w(w, a, src);
    n_src = cq_pc_indices(ctx, src, src_idx, 64u);
    cqrt_free(src);
    CHECK(cq_pc_indices_are_free(ctx, src_idx, n_src));
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    hs[1] = out; hs[2] = flag;
    CHECK(cq_pc_live_is_exactly(ctx, hs, ctrl ? 3u : 2u));
    CHECK_EQ(cq_reg_is_live(&ctx->regs, out), 1);
    CHECK_EQ(cq_pc_value(ctx, out), v);
    cq_reg_audit(ctx);

    close_with(&m);
}

/* THE SWEEP. `cq_kd_samples()` draws per width, each a (value, mask) pair from
 * one seeded RNG, with the four anchors INSIDE the budget: all-classical (which
 * IS L5), all-quantum (what a golden would pin), and the two value corners at
 * the all-quantum mask. Widths are enumerated, never sampled. */
static void sweep(const char *name, int ctrl)
{
    const int n = cq_kd_samples();

    for (size_t wi = 0; wi < 5u; wi++) {
        const uint32_t w = LADDER[wi];
        const uint64_t mask = wmask(w);
        const uint64_t seed = seed_for(name, w);
        cq_bk_rng rng;

        cq_bk_rng_init(&rng, seed);
        printf("# %s W=%u seed=0x%016llx samples=%d\n", name, w,
               (unsigned long long)seed, n);
        for (int i = 0; i < n; i++) {
            uint64_t v = cq_bk_rng_next(&rng) & mask;
            uint64_t q = cq_bk_rng_next(&rng) & mask;
            if (i == 0) q = 0u;                       /* all-classical: L5   */
            if (i == 1) q = mask;                     /* all-quantum         */
            if (i == 2) { v = mask; q = mask; }       /* the two corners     */
            if (i == 3) { v = 0u;   q = mask; }
            one_draw(w, v, q, ctrl);
        }
    }
}

CQ_TEST(the_kept_rail_holds_the_sources_value_over_random_masks)
{
    sweep("tape_write", 0);
}

CQ_TEST(the_controlled_write_holds_the_sources_value_under_a_set_quantum_flag)
{
    sweep("tape_write_controlled", 1);
}

