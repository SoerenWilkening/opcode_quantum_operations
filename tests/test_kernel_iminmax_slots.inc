/* tests/test_kernel_iminmax_slots.inc — the COMPOSITION identity and the
 * four-valued slot scan. Included by test_kernel_iminmax.c. NOTHING BELOW
 * READS A GOLDEN, and all of it was written before src/kernels/iminmax*.c.
 *
 * THE ROW TABLE BELOW IS TRANSCRIBED A SECOND TIME, STRAIGHT OFF THE PINNED
 * JULIA (third_party/bennett/src/extract/instructions.jl:4782-4825), and never
 * off the module. One row per IR instruction the extractor emits, operands in
 * the IR's order, with the compare predicates resolved through `lower_icmp!`
 * (third_party/bennett/src/lowering/arith.jl:409-418) exactly as M16 resolves
 * them: `uge` = ult(a,b) raw, `ule` = ult(b,a) raw, `sge` = slt(a,b) raw,
 * `sle` = slt(b,a) raw. No trailing negation on any of the four.
 *
 * WHAT THIS CATCHES THAT L1 DOES NOT: the "optimisation" mutant (a narrower
 * block, a skipped lane, a CSE'd compare) keeps every value right. What it
 * cannot catch: an operand SWAP inside a block (bd
 * `a-slot-scan-cannot-see-which-operand-a-slot-reads`) — that is L1's.
 */

enum { IM_ROW_ULT = 0, IM_ROW_SLT, IM_ROW_SUB, IM_ROW_MUX };
enum { IM_OP_A = 0, IM_OP_B, IM_OP_ZERO, IM_OP_SCR };   /* operand kinds */
enum { IM_MAX_ROWS = 3 };

typedef struct { int kind, x, y; } im_row;

static int im_rows(int op, im_row *r)
{
    switch (op) {
    case CQ_IMM_UMAX:                                            /* :4786 */
        r[0] = (im_row){ IM_ROW_ULT, IM_OP_A, IM_OP_B };   break;
    case CQ_IMM_UMIN:                                            /* :4794 */
        r[0] = (im_row){ IM_ROW_ULT, IM_OP_B, IM_OP_A };   break;
    case CQ_IMM_SMAX:                                            /* :4802 */
        r[0] = (im_row){ IM_ROW_SLT, IM_OP_A, IM_OP_B };   break;
    case CQ_IMM_SMIN:                                            /* :4810 */
        r[0] = (im_row){ IM_ROW_SLT, IM_OP_B, IM_OP_A };   break;
    default:                                         /* abs, :4821-4823 */
        r[0] = (im_row){ IM_ROW_SUB, IM_OP_ZERO, IM_OP_A };
        r[1] = (im_row){ IM_ROW_SLT, IM_OP_A, IM_OP_ZERO };
        r[2] = (im_row){ IM_ROW_MUX, IM_OP_A, IM_OP_SCR };
        return 3;
    }
    r[1] = (im_row){ IM_ROW_MUX, IM_OP_A, IM_OP_B };     /* :4787 etc. */
    return 2;
}

/* Each row's slot count and region, ASKED of its own module. */
static int im_row_steps(const im_row *r, int W)
{
    switch (r->kind) {
    case IM_ROW_ULT: return cq_ult_steps(W);
    case IM_ROW_SLT: return cq_slt_steps(W);
    case IM_ROW_SUB: return cq_sub_steps(W);
    default:         return cq_mux_steps(W);
    }
}

static int im_row_region(const im_row *r, int W)
{
    switch (r->kind) {
    case IM_ROW_ULT: return cq_ult_region(W);
    case IM_ROW_SLT: return cq_slt_region(W);
    case IM_ROW_SUB: return cq_sub_region(W);
    default:         return cq_mux_region(W);
    }
}

static const int IM_WS[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64 };
enum { IM_NWS = (int)(sizeof IM_WS / sizeof IM_WS[0]) };

/* ---- (a1) The slot and region identity. Runs no kernel. ---------------- */

CQ_TEST(the_blocks_cost_what_their_modules_say_they_cost)
{
    for (int op = 0; op < CQ_IMM_N_OP; op++)
        for (int w = 0; w < IM_NWS; w++) {
            im_row r[IM_MAX_ROWS];
            int n = im_rows(op, r), W = IM_WS[w], steps = 0, region = 0;

            for (int i = 0; i < n; i++) {
                steps  += im_row_steps(&r[i], W);
                region += im_row_region(&r[i], W);
            }
            CHECK_EQ(cq_imm_steps(op, W), steps);
            CHECK_EQ((int)cq_imm_region(op, W), region);
        }

    /* The block sizes the rows above rest on, pinned at W = 8 against each
     * module's published formula, so a sibling moving reddens THIS line and
     * names the block (K12.md §3.0's shape). */
    CHECK_EQ(cq_ult_steps(8), 49);  CHECK_EQ(cq_ult_region(8), 25);
    CHECK_EQ(cq_slt_steps(8), 67);  CHECK_EQ(cq_slt_region(8), 41);
    CHECK_EQ(cq_sub_steps(8), 55);  CHECK_EQ(cq_sub_region(8), 24);
    CHECK_EQ(cq_mux_steps(8), 32);  CHECK_EQ(cq_mux_region(8), 16);
}

/* ---- (a2) The gate identity: each block RUN ALONE through its own module. -
 *
 * The block cost is not written down anywhere in this file: each row is
 * driven through M14/M16/M17's own step function on a fresh, pre-materialised
 * region (I6(b), as cq_sandwich does), with the program's operand kinds — `a`
 * and `b` all-quantum, the constant 0 a CQ_BIT_ZERO source — and counted. The
 * kernel's forward pass must then be exactly twice the sum plus W copy-out
 * CNOTs, and so must its uncompute. A narrowed, skipped or CSE'd block breaks
 * the equality whatever value it computes. */
static cq_bit IM_ZEROS[64];

static const cq_bit *im_src(int kind, const cq_bit *a, const cq_bit *b,
                            const cq_bit *scr)
{
    switch (kind) {
    case IM_OP_A:    return a;
    case IM_OP_B:    return b;
    case IM_OP_ZERO: return IM_ZEROS;
    default:         return scr;
    }
}

static void im_row_measure(const im_row *r, int W, cq_counter *out)
{
    cq_ctx ctx;
    cq_counter c;
    cq_sink s = cq_sink_counter(&c);
    cq_scratch scr;
    const cq_bit *a, *b, *x, *y;
    cq_bit *g;
    int n = im_row_steps(r, W);

    cq_ctx_init(&ctx, &s);
    a = cq_reg_cbits(&ctx.regs, cq_bk_reg_w(&ctx, (uint32_t)W, cq_ref_w_zero(),
                                            cq_ref_w_ones(W)));
    b = cq_reg_cbits(&ctx.regs, cq_bk_reg_w(&ctx, (uint32_t)W, cq_ref_w_zero(),
                                            cq_ref_w_ones(W)));
    /* A W-bit scratch operand (abs's `neg`) plus a one-bit `cond`, then the
     * row's own region. All materialised: every lane but the constant is Q. */
    cq_scratch_alloc(&scr, (uint32_t)(W + 1 + im_row_region(r, W)));
    for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&ctx, &scr.bits[i]);
    x = im_src(r->x, a, b, scr.bits);
    y = im_src(r->y, a, b, scr.bits);
    g = scr.bits + W + 1;
    cq_count_reset(&c);

    for (int u = 0; u < n; u++) {
        if (r->kind == IM_ROW_ULT) {
            cq_ult_block k = { x, y, g, g + W, g + 2 * W + 1, W };
            cq_ult_step(&ctx, &k, u);
        } else if (r->kind == IM_ROW_SLT) {
            cq_slt_block k = { x, y, g, g + W, g + 2 * W, g + 3 * W,
                               g + 4 * W + 1, W };
            cq_slt_step(&ctx, &k, u);
        } else if (r->kind == IM_ROW_SUB) {
            cq_sub_block k = { x, y, g, g + W, g + 2 * W, W };
            cq_sub_step(&ctx, &k, u);
        } else {
            cq_mux_block k = { &scr.bits[W], x, y, g, g + W };
            cq_mux_step(&ctx, &k, u);
        }
    }
    *out = c;
    for (uint32_t i = 0; i < scr.n; i++) scr.bits[i] = cq_bit_zero();
    cq_scratch_dispose(&scr);
    cq_ctx_dispose(&ctx);
}

CQ_TEST(each_kernel_is_twice_the_sum_of_its_blocks_plus_the_copyout)
{
    for (int op = 0; op < CQ_IMM_N_OP; op++)
        for (int w = 0; w < IM_NWS; w++) {
            im_row r[IM_MAX_ROWS];
            int n = im_rows(op, r), W = IM_WS[w];
            uint64_t x = 0u, cx = 0u, ccx = 0u;
            cq_counter fwd, unc;

            for (int i = 0; i < n; i++) {
                cq_counter c;

                im_row_measure(&r[i], W, &c);
                x += c.x; cx += c.cx; ccx += c.ccx;
            }
            cq_kd_measure(IM_SPECS[op], W, &fwd, &unc);

            CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, 2u * x, 2u * cx + (uint64_t)W,
                        2u * ccx);
            CHECK_GATES(unc.x, unc.cx, unc.ccx, 2u * x, 2u * cx + (uint64_t)W,
                        2u * ccx);

            /* SLOTS AND GATES COME APART ONLY WHERE A CONSTANT IS A SOURCE:
             * abs's `0` folds W lanes of `sub` and W of `slt` at EVERY mask. An
             * implementation that materialised the constant (upstream's
             * `resolve!` shape) would make abs an equality here too. */
            if (op == CQ_IMM_ABS)
                CHECK(x + cx + ccx < (uint64_t)cq_imm_steps(op, W));
            else
                CHECK_EQ(x + cx + ccx, (uint64_t)cq_imm_steps(op, W));
        }
}

/* ---- (b) The four-valued slot scan, with its OWN stream cursor. --------- */

enum { IM_NONE = -1 };

_Static_assert(CQ_OP_X == 0 && CQ_OP_CX == 1 && CQ_OP_CCX == 2,
               "the scan names gates by cq_op");

/* A CQ_BIT_ZERO control removes the gate (PRD §3); only the constant 0 is
 * one here, and at the scan's all-quantum mask nothing is a ONE. */
static int im_gate(int ctl_kind, int op)
{
    return ctl_kind == IM_OP_ZERO ? IM_NONE : op;
}

/* `lower_ult!` (arith.jl:449-463) over (x, y): 2W alternating CX(y)/X, the
 * carry seed, then a 4-cycle per lane; nb/axnb/carry are scratch. */
static int im_sched_ult(int x, int y, int W, int v)
{
    if (v < 2 * W) return (v % 2 == 0) ? im_gate(y, CQ_OP_CX) : CQ_OP_X;
    if (v == 2 * W) return CQ_OP_X;
    switch ((v - (2 * W + 1)) % 4) {
    case 0:  return im_gate(x, CQ_OP_CX);
    case 1:  return CQ_OP_CX;
    case 2:  return im_gate(x, CQ_OP_CCX);
    default: return CQ_OP_CCX;
    }
}

/* `lower_slt!` (arith.jl:465-472): CX(x->af), CX(y->bf) per lane, two MSB
 * flips, then `lower_ult!` over the biased copies — scratch, never folded. */
static int im_sched_slt(int x, int y, int W, int v)
{
    if (v < 2 * W) return im_gate(v % 2 == 0 ? x : y, CQ_OP_CX);
    if (v < 2 * W + 2) return CQ_OP_X;
    return im_sched_ult(IM_OP_SCR, IM_OP_SCR, W, v - (2 * W + 2));
}

/* `lower_sub!` (third_party/bennett/src/adder.jl:149-172): the complement and
 * seed, a 5-cycle per lane, and the top lane's two Toffolis dropped (`i < W`). */
static int im_sched_sub(int x, int y, int W, int v)
{
    int u;

    if (v < 2 * W) return (v % 2 == 0) ? im_gate(y, CQ_OP_CX) : CQ_OP_X;
    if (v == 2 * W) return CQ_OP_X;
    u = v - (2 * W + 1);
    if (u < 5 * (W - 1))
        switch (u % 5) {
        case 0:  return im_gate(x, CQ_OP_CX);
        case 1:  return CQ_OP_CX;
        case 2:  return im_gate(x, CQ_OP_CCX);
        case 3:  return CQ_OP_CCX;
        default: return CQ_OP_CX;
        }
    return (u - 5 * (W - 1) == 0) ? im_gate(x, CQ_OP_CX) : CQ_OP_CX;
}

/* `lower_mux!` (arith.jl:522-532): CX, CX, CX, CCX per lane. */
static int im_sched(const im_row *r, int W, int v)
{
    switch (r->kind) {
    case IM_ROW_ULT: return im_sched_ult(r->x, r->y, W, v);
    case IM_ROW_SLT: return im_sched_slt(r->x, r->y, W, v);
    case IM_ROW_SUB: return im_sched_sub(r->x, r->y, W, v);
    default:         return (v % 4 == 3) ? CQ_OP_CCX : CQ_OP_CX;
    }
}

/* One forward call of a kernel into `m`, operands minted with `qa`/`qb` masks,
 * the mint's own X gates dropped. Returns dst's value. */
static uint64_t im_run(int op, int W, uint64_t va, uint64_t vb,
                       cq_ref_w qa, cq_ref_w qb, cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    int32_t ha, hb, hd;
    uint64_t got;

    cq_ctx_init(&ctx, &s);
    ha = cq_bk_reg_w(&ctx, (uint32_t)W, cq_ref_w_make(va, 0u, W), qa);
    hb = cq_bk_reg_w(&ctx, (uint32_t)W, cq_ref_w_make(vb, 0u, W), qb);
    hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);
    cq_mock_reset(m);
    if (op == CQ_IMM_ABS)
        cq_kernel_abs(&ctx, cq_reg_bits(&ctx.regs, hd),
                      cq_reg_cbits(&ctx.regs, ha), W);
    else
        IM_SPECS[op]->kernel(&ctx, cq_reg_bits(&ctx.regs, hd),
                             cq_reg_cbits(&ctx.regs, ha),
                             cq_reg_cbits(&ctx.regs, hb), W);
    got = cq_pc_value(&ctx, hd);
    cq_ctx_dispose(&ctx);
    return got;
}

static uint64_t im_ref(int op, uint64_t a, uint64_t b, int W)
{
    return op == CQ_IMM_ABS ? ref_abs(a, W) : IM_SPECS[op]->ref(a, b, W);
}

#define IM_SCAN_A 0xA5C3F00Fu
#define IM_SCAN_B 0x5A3C0FF1u

CQ_TEST(the_slot_boundaries_match_an_independent_four_valued_scan)
{
    cq_mock m;

    cq_mock_init(&m);
    for (int op = 0; op < CQ_IMM_N_OP; op++)
        for (int w = 0; w < IM_NWS; w++) {
            im_row r[IM_MAX_ROWS];
            int n = im_rows(op, r), W = IM_WS[w], bad = 0, folded = 0;
            size_t cursor = 0u;
            cq_ref_w q = cq_ref_w_ones(W);

            CHECK_EQ(im_run(op, W, IM_SCAN_A, IM_SCAN_B, q, q, &m),
                     im_ref(op, IM_SCAN_A, IM_SCAN_B, W));

            for (int i = 0; i < n && !bad; i++)
                for (int v = 0; v < im_row_steps(&r[i], W) && !bad; v++) {
                    int pr = im_sched(&r[i], W, v);

                    if (pr == IM_NONE) { folded++; continue; }
                    if (cursor >= cq_mock_count(&m)
                        || (int)cq_mock_at(&m, cursor)->op != pr) {
                        cq_h_fail(__FILE__, __LINE__, "%s W=%d row %d slot %d "
                                  "(stream %zu): the slot decode says op %d",
                                  IM_SPECS[op]->name, W, i, v, cursor, pr);
                        bad = 1;
                    }
                    cursor++;
                }
            if (bad) continue;

            /* The constant 0 is abs's alone, so only abs folds — and it must. */
            CHECK(op == CQ_IMM_ABS ? folded > 0 : folded == 0);
            /* W copy-out CNOTs, then the same `cursor` gates in reverse. */
            CHECK_EQ((long long)cq_mock_count(&m),
                     (long long)(2u * cursor + (size_t)W));
            for (int i = 0; i < W; i++)
                CHECK_EQ((int)cq_mock_at(&m, cursor + (size_t)i)->op,
                         (int)CQ_OP_CX);
            CHECK(cq_mock_is_palindrome(&m, cursor, (size_t)W));
        }
    cq_mock_dispose(&m);
}
