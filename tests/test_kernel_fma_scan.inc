/* tests/test_kernel_fma_scan.inc — M39, K20. THE FOUR-VALUED DELEGATING SLOT
 * SCAN (bd 9ve.38), M34's instrument (tests/test_kernel_fmul_scan.inc) carried
 * over to `soft_fma`. Included by test_kernel_fma.c AFTER
 * test_kernel_fma_slots.inc, whose `fa_cost_of` and `FA_CENSUS` it reuses, and
 * after `fma_run`.
 *
 * WHAT IT SEES THAT NOTHING ELSE IN THIS SUITE DOES: INTRA-ROW GATE ORDER.
 * `lower_or!` is CNOT(a), CNOT(b), Toffoli(a, b) (arith.jl:274-282) and
 * `lower_not1!` is CNOT(w, r) then NOT(r) (:474-478). All three gates of an
 * `or` target one `r[lane]`, so permuting them keeps the value, the (x, cx,
 * ccx) tuple, the golden, the spans and — because each slot is its own
 * involution and the driver reverses slot indices — the palindrome. Only a
 * prediction of the KIND PER SLOT sees the permutation.
 *
 * THE PREDICTION IS A KIND OR NOTHING, AND THE STREAM HAS ITS OWN CURSOR. A
 * constant ZERO control lane makes a gate vanish and a ONE demotes it, at
 * every operand mask, because views and constant spans put classical lanes
 * inside block operands. So each slot predicts NONE / X / CX / CCX and the
 * cursor advances only on a non-NONE.
 *
 * ONLY THE SIX OPS fma_emit.c EMITS ONE GATE PER SLOT ITSELF ARE PREDICTED —
 * `and`, `or`, `xor`, `not1`, `and1`, `or1`. EVERY STEP BLOCK IS DELEGATED:
 * the row is run STANDALONE, through its own module's step function, over
 * operands with the SAME LANE PROFILE, and the two streams are compared gate
 * kind for gate kind. Re-deriving `cq_mul_step`'s or M32's schedules here
 * would be a second transcription of them, which a slot scan must not be.
 *
 * ONE SOURCE SPAN PER OPERAND IN THE STANDALONE FIXTURE, AND IT IS FORCED:
 * three views over ONE span make `a[i]` and `b[i]` the same qubit, and M05's
 * distinctness assert refuses `cq_mul_block`'s phase-P Toffoli (M34's measured
 * finding). A lane profile here is per LANE, not a prefix: `0x3FE`, `<< 10`
 * views and `flushed_result` are not prefixes, which M34's `fm_prefix` could
 * assume and this file cannot.
 *
 * BLIND SPOTS, RECORDED: a kind scan cannot see WHICH operand a slot reads
 * (bd a-slot-scan-cannot-see-which-operand-a-slot-reads) nor a view that moves
 * a lane (bd a-kind-scan-cannot-see-a-view-that-moves-a-lane).
 *
 * NOTHING BELOW READS A GOLDEN.
 */

enum { FS_NONE = -1, FS_FLD = W64 + 1 };

typedef struct { int folded, demoted; } fs_tally;

/* The constant vocabulary, transcribed from softfloat_common.jl:9-14 and
 * fma.jl:79/:83 rather than read out of fma_operand.c's `const_of` — an oracle
 * that shares a constant with the code is blind to that constant. */
static uint64_t fs_const(int s)
{
    switch (s) {
    case CQ_FU_K_ZERO:     return 0u;
    case CQ_FU_K_ONE:      return 1u;
    case CQ_FU_K_ONES:     return ~UINT64_C(0);
    case CQ_FU_K_2:        return 2u;
    case CQ_FU_K_4:        return 4u;
    case CQ_FU_K_8:        return 8u;
    case CQ_FU_K_16:       return 16u;
    case CQ_FU_K_32:       return 32u;
    case CQ_FU_K_63:       return 63u;
    case CQ_FU_K_64:       return 64u;
    case CQ_FU_K_127:      return 127u;
    case CQ_FU_K_128:      return 128u;
    case CQ_FU_K_3FE:      return 0x3FEu;                      /* fma:79    */
    case CQ_FU_K_2P61:     return 0x2000000000000000ull;       /* fma:83    */
    case CQ_FU_K_IMPLICIT: return 0x0010000000000000ull;       /* common:9  */
    case CQ_FU_K_QUIET:    return 0x0008000000000000ull;       /* common:13 */
    case CQ_FU_K_INF:      return 0x7ff0000000000000ull;       /* common:11 */
    case CQ_FU_K_INDEF:    return 0xfff8000000000000ull;       /* common:14 */
    default: break;
    }
    cq_h_fail(__FILE__, __LINE__, "operand code %d is no constant", s);
    return 0u;
}

/* The KIND of lane `i` of 64-lane operand `s` at the ALL-QUANTUM rail mask.
 * The view chain is collapsed by this file's OWN walk. */
static int fs_lane(const cq_fma_row *rows, int n, int s, int i)
{
    int guard = 0, shift = 0;
    uint64_t mask = ~UINT64_C(0);

    while (s >= 0 && s < n && rows[s].op == CQ_FUOP_VIEW) {
        uint64_t m = rows[s].mask;

        /* (((base >> s1) & m1) >> s2) & m2
         *     == (base >> (s1+s2)) & ((m1 >> s2) & m2) */
        if (shift >= 64 || shift <= -64) m = 0u;
        else if (shift >= 0)             m >>= (unsigned)shift;
        else                             m <<= (unsigned)(-shift);
        mask  &= m;
        shift += rows[s].shift;
        s      = rows[s].s0;
        if (++guard > n) { CHECK(0); return CQ_BIT_ZERO; }
    }
    if (((mask >> (unsigned)i) & 1u) == 0u) return CQ_BIT_ZERO;
    i += shift;
    if (i < 0 || i >= W64) return CQ_BIT_ZERO;

    if (s == CQ_FU_A || s == CQ_FU_B || s == CQ_FU_C) return CQ_BIT_Q;
    if (s < 0)
        return ((fs_const(s) >> (unsigned)i) & 1u) ? CQ_BIT_ONE : CQ_BIT_ZERO;
    if (rows[s].op == CQ_FUOP_OUT && rows[s].s1 == CQ_FU_OUT_FLUSHED
        && rows[rows[s].s0].op == CQ_FUOP_SUBNORM)
        /* `flushed_result = result_sign << 63` (common.jl:183) is a VIEW over
         * the hand-off's third input, not a span of its region. */
        return (i == 63) ? fs_lane(rows, n, rows[rows[s].s0].s2, 0)
                         : CQ_BIT_ZERO;
    return CQ_BIT_Q;             /* a span of the region: I6(b) materialises */
}

/* A 1-bit operand is a Bool ROW, hence a bit of the region, hence quantum. */
static int fs_flag(const cq_fma_row *rows, int n, int s)
{
    if (s < 0 || s >= n || cq_fma_row_width(rows, n, s) != 1)
        cq_h_fail(__FILE__, __LINE__, "operand %d is not a Bool row", s);
    return CQ_BIT_Q;
}

/* PRD §3's fold table over one gate's controls. */
static int fs_fold(int kind, const int *ctrl, int nc, fs_tally *t)
{
    for (int i = 0; i < nc; i++) {
        if (ctrl[i] == CQ_BIT_ZERO) { t->folded++; return FS_NONE; }
        if (ctrl[i] == CQ_BIT_ONE)  { kind--; t->demoted++; }
    }
    return kind;
}

/* The six directly emitted ops, one gate per slot, in the PORT's order:
 * lower_and! (arith.jl:268-272), lower_or! (:274-282), lower_xor!
 * (:284-291), lower_not1! (:474-478), and the 1-bit and1/or1. */
static int fs_predict(const cq_fma_row *r, const cq_fma_row *rows, int n,
                      int v, fs_tally *t)
{
    int c[2];

    switch (r->op) {
    case CQ_FUOP_AND:
        c[0] = fs_lane(rows, n, r->s0, v); c[1] = fs_lane(rows, n, r->s1, v);
        return fs_fold(CQ_OP_CCX, c, 2, t);
    case CQ_FUOP_OR:                             /* CX(a), CX(b), CCX(a,b) */
        c[0] = fs_lane(rows, n, r->s0, v / 3);
        c[1] = fs_lane(rows, n, r->s1, v / 3);
        if (v % 3 == 2) return fs_fold(CQ_OP_CCX, c, 2, t);
        return fs_fold(CQ_OP_CX, &c[v % 3], 1, t);
    case CQ_FUOP_XOR:                            /* CX(a), CX(b)           */
        c[0] = fs_lane(rows, n, (v % 2 == 0) ? r->s0 : r->s1, v / 2);
        return fs_fold(CQ_OP_CX, c, 1, t);
    case CQ_FUOP_NOT1:                           /* CX(w, r), X(r)         */
        c[0] = fs_flag(rows, n, r->s0);
        return (v == 0) ? fs_fold(CQ_OP_CX, c, 1, t) : CQ_OP_X;
    case CQ_FUOP_AND1:
        c[0] = fs_flag(rows, n, r->s0); c[1] = fs_flag(rows, n, r->s1);
        return fs_fold(CQ_OP_CCX, c, 2, t);
    default:                                     /* or1: CX, CX, CCX       */
        c[0] = fs_flag(rows, n, r->s0); c[1] = fs_flag(rows, n, r->s1);
        if (v == 2) return fs_fold(CQ_OP_CCX, c, 2, t);
        return fs_fold(CQ_OP_CX, &c[v], 1, t);
    }
}

static int fs_is_predicted(int op)
{
    return op == CQ_FUOP_AND || op == CQ_FUOP_OR || op == CQ_FUOP_XOR
        || op == CQ_FUOP_NOT1 || op == CQ_FUOP_AND1 || op == CQ_FUOP_OR1;
}

static int fs_is_delegated(int op)
{
    return op != CQ_FUOP_VIEW && op != CQ_FUOP_OUT && !fs_is_predicted(op);
}

/* Row `r` run STANDALONE into `m`, over a fresh region: operand j is built
 * lane for lane from `fs_lane` over ITS OWN 64-lane source span, a mux's
 * condition is one more fresh qubit, and the block's own internals start at
 * `base`. For the pointer-carved blocks each field gets W+1 bits at its own
 * slot — kinds are all this compares, and every scratch bit is quantum
 * whatever its layout. Returns the number of gates the block emitted. */
static size_t fs_standalone(cq_mock *m, const cq_fma_row *rows, int n, int r)
{
    const cq_fma_row *row = &rows[r];
    const uint32_t base = (uint32_t)(3 * W64) + 1u;
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_scratch scr;
    cq_bit v[3][CQ_FP64_W], *f[5], *cond;
    short src[3];
    uint32_t reg;
    int op = row->op, steps, first = (op == CQ_FUOP_MUX) ? 1 : 0;

    switch (op) {
    case CQ_FUOP_CLASS:   reg = cq_fp_class_region((cq_fp_class)row->s1); break;
    case CQ_FUOP_MUL:     reg = (uint32_t)cq_mul_region(W64);    break;
    case CQ_FUOP_BSHL: case CQ_FUOP_BLSHR:
                          reg = (uint32_t)cq_barrel_region(W64); break;
    case CQ_FUOP_NORM52:  reg = cq_norm52_region();  break;
    case CQ_FUOP_SUBNORM: reg = cq_subnorm_region(); break;
    case CQ_FUOP_ROUND:   reg = cq_round_region();   break;
    default:              reg = 5u * (uint32_t)FS_FLD; break;
    }
    steps = (op == CQ_FUOP_CLASS) ? cq_fp_class_steps((cq_fp_class)row->s1)
                                  : fa_cost_of(op);
    CHECK(steps > 0);

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);
    cq_scratch_alloc(&scr, base + reg);
    for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&ctx, &scr.bits[i]);

    src[0] = row->s0; src[1] = row->s1; src[2] = row->s2;
    for (int j = 0; j < 3; j++) {
        const cq_bit *sp = cq_scratch_span(&scr, (uint32_t)(j * W64),
                                           (uint32_t)W64);
        int k = first + j, live = (k < 3)
            && (op == CQ_FUOP_CLASS ? k == 0 : k < cq_fma_arity(op));

        for (int i = 0; i < W64; i++) {
            int kind = live ? fs_lane(rows, n, src[k], i) : CQ_BIT_Q;

            v[j][i] = (kind == CQ_BIT_Q)   ? sp[i]
                    : (kind == CQ_BIT_ONE) ? cq_bit_one() : cq_bit_zero();
        }
    }
    if (first) (void)fs_flag(rows, n, row->s0);
    cond = cq_scratch_span(&scr, (uint32_t)(3 * W64), 1u);
    for (int j = 0; j < 5; j++)
        f[j] = (reg >= 5u * (uint32_t)FS_FLD)
             ? cq_scratch_span(&scr, base + (uint32_t)(j * FS_FLD),
                               (uint32_t)FS_FLD)
             : NULL;
    cq_mock_reset(m);

    for (int u = 0; u < steps; u++)
        switch (op) {
        case CQ_FUOP_CLASS: {
            cq_fp_class_block b = { .a = v[0], .scr = &scr, .off = base,
                                    .cls = (cq_fp_class)row->s1 };
            cq_fp_class_step(&ctx, &b, u); break; }
        case CQ_FUOP_EQ: {
            cq_eq_block b = { .a = v[0], .b = v[1], .diff = f[0], .orr = f[1],
                              .W = W64 };
            cq_eq_step(&ctx, &b, u); break; }
        case CQ_FUOP_ULT: {
            cq_ult_block b = { .a = v[0], .b = v[1], .nb = f[0],
                               .carry = f[1], .axnb = f[2], .W = W64 };
            cq_ult_step(&ctx, &b, u); break; }
        case CQ_FUOP_SLT: {
            cq_slt_block b = { .a = v[0], .b = v[1], .af = f[0], .bf = f[1],
                               .nb = f[2], .carry = f[3], .axnb = f[4],
                               .W = W64 };
            cq_slt_step(&ctx, &b, u); break; }
        case CQ_FUOP_ADD: {
            cq_add_block b = { .a = v[0], .b = v[1], .t = f[0], .c = f[1],
                               .W = W64 };
            cq_add_step(&ctx, &b, u); break; }
        case CQ_FUOP_SUB: {
            cq_sub_block b = { .a = v[0], .b = v[1], .nb = f[0], .d = f[1],
                               .c = f[2], .W = W64 };
            cq_sub_step(&ctx, &b, u); break; }
        case CQ_FUOP_MUX: {
            cq_mux_block b = { .cond = cond, .t = v[0], .f = v[1], .r = f[0],
                               .d = f[1] };
            cq_mux_step(&ctx, &b, u); break; }
        case CQ_FUOP_MUL: {
            cq_mul_block b = { .a = v[0], .b = v[1], .scr = &scr, .off = base,
                               .W = W64 };
            cq_mul_step(&ctx, &b, u); break; }
        case CQ_FUOP_BSHL: case CQ_FUOP_BLSHR: {
            cq_barrel_block b = { .a = v[0], .b = v[1], .scr = &scr,
                                  .off = base, .W = W64,
                                  .dir = (op == CQ_FUOP_BSHL) ? CQ_BARREL_SHL
                                                              : CQ_BARREL_LSHR };
            cq_barrel_step(&ctx, &b, u); break; }
        case CQ_FUOP_NORM52: {
            cq_norm52_block b = { .m = v[0], .e = v[1], .scr = &scr,
                                  .off = base };
            cq_norm52_step(&ctx, &b, u); break; }
        case CQ_FUOP_SUBNORM: {
            cq_subnorm_block b = { .wr = v[0], .result_exp = v[1],
                                   .result_sign = v[2], .scr = &scr,
                                   .off = base };
            cq_subnorm_step(&ctx, &b, u); break; }
        default: {
            cq_round_block b = { .wr = v[0], .result_exp = v[1],
                                 .result_sign = v[2], .scr = &scr,
                                 .off = base };
            cq_round_step(&ctx, &b, u); break; }
        }

    /* A hand-driven block never restores the KINDS M08's dispose asserts. */
    for (uint32_t i = 0; i < scr.n; i++) scr.bits[i] = cq_bit_zero();
    cq_scratch_dispose(&scr);
    cq_ctx_dispose(&ctx);
    return cq_mock_count(m);
}

CQ_TEST(the_slot_kinds_match_a_delegating_four_valued_scan)
{
    const cq_fma_row *rows;
    cq_mock m, m2;
    cq_ref_w q[3];
    fs_tally t = { 0, 0 };
    size_t cursor = 0u, total;
    int n, bad = 0, delegated = 0, predicted = 0, want_del = 0, want_pred = 0;

    cq_mock_init(&m);
    cq_mock_init(&m2);
    rows = cq_fma_rows(&n);
    for (int i = 0; i < 3; i++) q[i] = cq_ref_w_ones(W64);

    /* The VALUE on the way through: a scan that never looked at the answer
     * would happily pin the shape of a wrong circuit. */
    CHECK_EQ(fma_run(FA_X, FA_X, FA_NEG_X2_RND, q, &m),
             ref_fma(FA_X, FA_X, FA_NEG_X2_RND));

    for (int r = 0; r < n && !bad; r++) {
        int op = rows[r].op;

        if (op == CQ_FUOP_VIEW || op == CQ_FUOP_OUT) continue;
        if (fs_is_delegated(op)) {
            size_t got = fs_standalone(&m2, rows, n, r);

            CHECK(got > 0u);
            if (cursor + got > cq_mock_count(&m)) {
                cq_h_fail(__FILE__, __LINE__, "row %d (op %d): %zu gates "
                          "left, the standalone block emits %zu", r, op,
                          cq_mock_count(&m) - cursor, got);
                bad = 1;
                break;
            }
            for (size_t g = 0; g < got && !bad; g++)
                if (cq_mock_at(&m, cursor + g)->op != cq_mock_at(&m2, g)->op) {
                    cq_h_fail(__FILE__, __LINE__, "row %d (op %d) gate %zu "
                              "(stream %zu): the kernel emits op %d, the same "
                              "block driven standalone emits op %d", r, op, g,
                              cursor + g, (int)cq_mock_at(&m, cursor + g)->op,
                              (int)cq_mock_at(&m2, g)->op);
                    bad = 1;
                }
            cursor += got;
            delegated++;
            continue;
        }
        for (int v = 0; v < fa_cost_of(op) && !bad; v++) {
            int pr = fs_predict(&rows[r], rows, n, v, &t);
            int got;

            if (pr == FS_NONE) continue;
            if (cursor >= cq_mock_count(&m)) {
                cq_h_fail(__FILE__, __LINE__, "row %d slot %d: the stream ran "
                          "out at %zu gates", r, v, cursor);
                bad = 1;
                break;
            }
            got = (int)cq_mock_at(&m, cursor++)->op;
            if (got != pr) {
                cq_h_fail(__FILE__, __LINE__, "row %d (op %d) slot %d (stream "
                          "%zu): emitted op %d, the port's order says %d", r,
                          op, v, cursor - 1u, got, pr);
                bad = 1;
            }
        }
        predicted++;
    }

    if (!bad) {
        /* EVERY EMITTING ROW WAS SEEN, counted against slots.inc's census
         * rather than against this loop's own classification. */
        for (int c = 0; c < N_FA_CENSUS; c++) {
            if (fs_is_predicted(FA_CENSUS[c].op)) want_pred += FA_CENSUS[c].n;
            else if (fs_is_delegated(FA_CENSUS[c].op)) want_del += FA_CENSUS[c].n;
        }
        CHECK_EQ(delegated, want_del);
        CHECK_EQ(predicted, want_pred);
        /* NOT VACUOUS: both halves of the fold table are reached. */
        CHECK(t.folded > 0);
        CHECK(t.demoted > 0);

        /* The copy-out is exactly W CNOTs and the reverse half is the same
         * `cursor` gates again. */
        total = cq_mock_count(&m);
        CHECK_EQ((long long)total, (long long)(2u * cursor + (size_t)W64));
        for (int i = 0; i < W64; i++)
            CHECK_EQ((int)cq_mock_at(&m, cursor + (size_t)i)->op, (int)CQ_OP_CX);
        CHECK(cq_mock_is_palindrome(&m, cursor, (size_t)W64));
        printf("# fma slot scan: %zu gates forward, %d rows predicted "
               "(%d folded, %d demoted), %d blocks delegated\n", cursor,
               predicted, t.folded, t.demoted, delegated);
        fflush(stdout);
    }

    cq_mock_dispose(&m2);
    cq_mock_dispose(&m);
}
