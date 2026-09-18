/* tests/test_kernel_mul_block.inc — included by test_kernel_mul.c.
 *
 * K11's COMPUTE HALF DRIVEN AS A CONSUMER DRIVES IT, which is what PRD-v2 §5's
 * M34 row ("the 53x53 product is M18's `mul` over a span") and §7.10 ("M18
 * exports the count and not the step") ask for. Everything above in this file
 * enters through `cq_kernel_mul`; every case below builds its OWN region, binds
 * a `cq_mul_block` into it at a NON-ZERO offset, and hands `cq_mul_step` to
 * `cq_sandwich` itself. Nothing here reads a golden.
 *
 * WHY THE OFFSET IS NON-ZERO. `cq_kernel_mul` passes `off = 0`, so a block
 * whose accessors ignored `off` would be green everywhere in this suite and
 * wrong for every consumer — M34 puts `soft_fmul`'s sign, exponent and
 * round-and-pack intermediates in the same region (PRD-v2 §7.2: one block per
 * operator occurrence, every intermediate a 64-bit span), so the product's
 * sub-region starts wherever the preceding blocks ended. MB_PAD is that
 * "wherever".
 *
 * AND A NON-ZERO OFFSET ON ONE BLOCK DOES NOT DETECT `off` BEING DROPPED —
 * MEASURED, not reasoned, and it is the sharpest thing in this file. Delete
 * `k->off +` from mul.c's `span` and EVERY accessor loses it together,
 * `cq_mul_product` included, so the whole block slides down by MB_PAD, stays
 * inside the caller's region, computes the right value, mirrors perfectly and
 * returns every qubit. The pad bits it now occupies were pre-materialised by
 * the driver anyway (I6(b)). A single block cannot see its own base address.
 * What sees it is TWO blocks in one region: drop `off` and they land on top of
 * each other, so `the_two_blocks_in_one_region_do_not_collide` below is the
 * ONLY case in this file that goes red, and it is the case M34 is actually
 * shaped like — `soft_fmul` forms FOUR products, not one (fmul.jl:78-81).
 *
 * WHAT THESE CASES CAN SEE THAT THE KERNEL'S CANNOT. Driving the block is the
 * only way to exercise (i) the offset, (ii) a caller-supplied region larger
 * than the block's own, and (iii) the fact that the block has NO R9
 * short-circuit — the all-classical fold lives at `cq_kernel_mul`'s entry, so
 * the block enters the sandwich at every mask and `mb_case` asserts the pool
 * comes back anyway.
 *
 * L2 IS TAKEN AT THREE POINTS, NOT ONE (Step 10's measured finding): a compute
 * half that acquires one ancilla and releases one qubit belonging to a SOURCE
 * nets to zero, so a single reading after the forward call is green while I2
 * and I3 are both lies. `mb_case` reads it after the forward, after the
 * uncompute and after the free.
 */

/* Bits BELOW the block's own region, so `cq_mul_block.off` is never 0 here.
 * Three is arbitrary and deliberately not a multiple of W: an accessor that
 * dropped `off` and one that scaled it would both survive a round number. */
enum { MB_PAD = 3 };

typedef struct {
    cq_bit      *dst;
    cq_mul_block k;
} mb_env;

static void mb_compute(cq_ctx *ctx, void *env, int s)
{
    cq_mul_step(ctx, &((const mb_env *)env)->k, s);
}

/* The "^=" of Rule 7's contract, written by the CONSUMER rather than by M18 —
 * which is the point of `cq_mul_product`: a consumer reads the same wire
 * `cq_kernel_mul`'s own copy-out reads, and reads it as a CONTROL. */
static void mb_copyout(cq_ctx *ctx, void *env, int i)
{
    const mb_env *e = (const mb_env *)env;

    cq_emit_cx(ctx, &cq_mul_product(&e->k)[i], &e->dst[i]);
}

/* One block run: region, bind, sandwich. The operand and dst POINTERS are
 * re-fetched on every call because `cq_reg_bits` hands out an interior pointer
 * into a table that may move. */
static void mb_sandwich(cq_ctx *ctx, int W, int32_t ha, int32_t hb, int32_t hd)
{
    cq_scratch scr;
    mb_env e;

    cq_scratch_alloc(&scr, (uint32_t)MB_PAD + (uint32_t)cq_mul_region(W));

    e.dst   = cq_reg_bits(&ctx->regs, hd);
    e.k.a   = cq_reg_cbits(&ctx->regs, ha);
    e.k.b   = cq_reg_cbits(&ctx->regs, hb);
    e.k.scr = &scr;
    e.k.off = (uint32_t)MB_PAD;
    e.k.W   = W;

    cq_sandwich(ctx, &scr, mb_compute, cq_mul_steps(W), mb_copyout, W, &e);
    cq_scratch_dispose(&scr);
}

/* ONE CASE, ALL FOUR LEVELS THE BLOCK CAN CARRY, at an explicit mask pair.
 * Returns the FORWARD half's total gate count so a caller can compare masks.
 *
 * L1  dst's VALUE against refmodel.c's independent shift-add — values, never
 *     kinds (Rule 14), and never the shadow (a constant bit has none).
 * L2  no index live that no named register owns, at three points.
 * L3  a second run of the same block returns dst to zero, and freeing it puts
 *     every one of dst's indices back on the free list.
 * The palindrome, around the consumer's own copy-out.
 *
 * `head` IS COMPUTED FROM THE MASK, NOT FROM THE STREAM — compute_half_gates
 * above, shared with the kernel's palindrome case for the reason stated there:
 * deriving it as `(n − W)/2` would assume the very structure the palindrome
 * exists to prove.
 *
 * `fwd_ops` IS CAPTURED HERE AND NOT BY THE CALLER, AND THAT WAS A REAL BUG
 * FIRST: the mock is reset once and the UNCOMPUTE run appends to it, so a
 * caller reading `cq_mock_count_op` after this function returns sees BOTH runs
 * and gets exactly twice the kernel's tuple. It failed loudly — a factor of two
 * on all four widths — but a caller comparing an inequality rather than an
 * equality would have swallowed it. May be NULL. */
static size_t mb_case(int W, cq_ref_w va, cq_ref_w qa, cq_ref_w vb, cq_ref_w qb,
                      cq_mock *m, cq_counter *fwd_ops)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_ref_w want, got;
    uint32_t idx[128];
    uint32_t n_idx;
    size_t head, total;
    int32_t hs[3];

    va = cq_ref_w_make(va.lo, va.hi, W);
    vb = cq_ref_w_make(vb.lo, vb.hi, W);
    want = cq_ref_w_mul(va, vb, W);

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);

    hs[0] = cq_bk_reg_w(&ctx, (uint32_t)W, va, qa);
    hs[1] = cq_bk_reg_w(&ctx, (uint32_t)W, vb, qb);
    hs[2] = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);

    cq_pc_snap before = cq_pc_take(&ctx);

    /* --- forward --- */
    cq_mock_reset(m);                        /* drop the materialising Xs */
    mb_sandwich(&ctx, W, hs[0], hs[1], hs[2]);

    got = cq_pc_value_w(&ctx, hs[2]);
    if (!cq_ref_w_eq(got, want))
        cq_h_fail(__FILE__, __LINE__,
                  "mul block W=%d: dst = 0x%llx%016llx, want 0x%llx%016llx", W,
                  (unsigned long long)got.hi,  (unsigned long long)got.lo,
                  (unsigned long long)want.hi, (unsigned long long)want.lo);

    head  = compute_half_gates(W, va, qa, vb, qb);
    total = cq_mock_count(m);

    if (fwd_ops) {
        cq_count_reset(fwd_ops);
        fwd_ops->x   = (uint64_t)cq_mock_count_op(m, CQ_OP_X);
        fwd_ops->cx  = (uint64_t)cq_mock_count_op(m, CQ_OP_CX);
        fwd_ops->ccx = (uint64_t)cq_mock_count_op(m, CQ_OP_CCX);
    }

    CHECK(cq_mock_is_palindrome(m, head, (size_t)W));
    CHECK_EQ(total, 2u * head + (size_t)W);
    CHECK(total > 0u);                       /* never vacuous */
    CHECK(head <= (size_t)cq_mul_steps(W));  /* a fold removes, never adds */

    CHECK(cq_pc_live_is_exactly(&ctx, hs, 3u));

    /* --- uncompute: the SAME block again, f ^ f = 0 --- */
    mb_sandwich(&ctx, W, hs[0], hs[1], hs[2]);

    CHECK(cq_ref_w_eq(cq_pc_value_w(&ctx, hs[2]), cq_ref_w_zero()));
    CHECK(cq_pc_live_is_exactly(&ctx, hs, 3u));

    /* --- free: L3, named by index before the rail becomes a tombstone --- */
    n_idx = cq_pc_indices(&ctx, hs[2], idx, 128u);
    cq_reg_free(&ctx, hs[2], cq_pc_zero_proof_rotation_free);

    CHECK(cq_pc_indices_are_free(&ctx, idx, n_idx));
    CHECK(cq_pc_same(before, cq_pc_take(&ctx)));
    CHECK(cq_pc_live_is_exactly(&ctx, hs, 2u));

    cq_ctx_dispose(&ctx);
    return total;
}

/* ---- (a) The block at the two ASYMMETRIC masks. ------------------------- */

/* RISK R8's NAMED WITNESS AND ITS MIRROR. `a` all Q with `b` all CQ_BIT_ZERO is
 * the pair R8 mandates by name and no symmetric mask set contains; the reverse
 * is its mirror, and the two are NOT the same test — phase P's two controls are
 * different slots of the same Toffoli, so a fold that fired on one and not the
 * other would be invisible to whichever one a suite happened to pick.
 *
 * AN ALL-CLASSICAL PAIR IS NOT A PALINDROME CASE, and that is why neither mask
 * here is one: through `cq_kernel_mul` an all-classical pair takes the R9
 * short-circuit and never enters the sandwich, so `cq_mock_count` is 0 and the
 * palindrome is being asked about a stream that does not exist. Driving the
 * BLOCK it would enter — the block has no fold — but the case that says so is
 * about the pool, not about the mirror.
 *
 * THE HEAD IS ASKED OF M15, NOT WRITTEN DOWN. At either asymmetric mask every
 * phase-P Toffoli has a classically-ZERO control and folds away, so the compute
 * half is exactly W accumulates — and what an accumulate COSTS is
 * `cq_addacc_steps(W)`'s to say. Writing `W(6W−5)` here instead would be a
 * second transcription of M15's closed form, and K08.md's W=1 trap is what that
 * costs one layer down. */
CQ_TEST(the_block_mirrors_around_a_consumer_supplied_copyout)
{
    static const int WS[] = { 2, 3, 4, 5, 8, 16, 32, 64 };
    cq_mock m;

    cq_mock_init(&m);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        size_t accs = (size_t)W * (size_t)cq_addacc_steps(W);
        cq_ref_w all = cq_ref_w_ones(W);
        cq_ref_w none = cq_ref_w_zero();
        size_t t_aq, t_qa;

        /* a all Q, b all CQ_BIT_ZERO — R8's named pair. */
        t_aq = mb_case(W, all, all, none, none, &m, NULL);
        CHECK_EQ(t_aq, 2u * accs + (size_t)W);

        /* The mirror: a all CQ_BIT_ZERO, b all Q. Same head, different control
         * slot of the same Toffoli. */
        t_qa = mb_case(W, none, none, all, all, &m, NULL);
        CHECK_EQ(t_qa, 2u * accs + (size_t)W);

        /* A classical ZERO really did remove gates, so neither of the two is a
         * vacuous "≤" against the all-quantum run. */
        CHECK(accs < (size_t)cq_mul_steps(W));
    }

    cq_mock_dispose(&m);
}

/* ---- (b) The composition: the block IS the kernel's forward half. ------- */

/* THE IDENTITY, AND IT READS NO GOLDEN AND NO WRITTEN FORMULA. At the
 * all-quantum mask `cq_kernel_mul`'s forward tuple must be exactly what
 * `cq_mul_steps(W)` step calls emit, twice, plus the W-CX copy-out — and the
 * left side is MEASURED by driving the block rather than derived. So a
 * `cq_kernel_mul` that stopped dispatching through the export, or an export
 * that emitted a different schedule from the kernel's, goes red here while L4
 * stays green: `CQOPS_UPDATE_GOLDENS=1` would re-pin the golden and cannot
 * touch this.
 *
 * IT IS ALSO WHERE "gates == steps" IS TRUE. Away from all-quantum an operand
 * fold makes the gate count strictly smaller than the step count (case (a)
 * above, and (c) below); here no fold can fire, so the block emits exactly one
 * gate per step and the two coincide. That is the same reason L4 pins at this
 * mask and nowhere else. */
CQ_TEST(the_blocks_stream_is_the_kernels_forward_half_gate_for_gate)
{
    static const int WS[] = { 2, 3, 4, 5, 8, 16, 32, 64 };
    cq_mock m;

    cq_mock_init(&m);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];
        cq_ref_w all = cq_ref_w_ones(W);
        cq_counter fwd, unc, blk;
        size_t total = mb_case(W, all, all, all, all, &m, &blk);

        cq_kd_measure(&MUL, W, &fwd, &unc);

        /* The step calls, plus the copy-out. */
        CHECK_EQ(total, 2u * (size_t)cq_mul_steps(W) + (size_t)W);

        /* And gate for gate, per KIND — a total is not an identification. */
        CHECK_GATES(blk.x, blk.cx, blk.ccx, fwd.x, fwd.cx, fwd.ccx);
        CHECK_EQ(total, (size_t)(fwd.x + fwd.cx + fwd.ccx));
        CHECK(total > 0u);
    }

    cq_mock_dispose(&m);
}

/* ---- W = 1: the block does not exist, and the pair says so consistently. - */

/* `cq_mul_region` AND `cq_mul_steps` MUST AGREE AT W = 1 OR A WIDTH-GENERIC
 * CONSUMER LOOP SILENTLY MISCOMPILES. `lower_add_cuccaro!` falls back to the
 * out-of-place `lower_add!` at W <= 1 (adder.jl:66) and stops being an
 * accumulator, so `cq_kernel_mul` delegates i1 to K2 and there is no sandwich
 * and no region. A consumer accumulating `off += cq_mul_region(W)` and
 * `n += cq_mul_steps(W)` therefore gets 0 and 0 — no product, and the CCX is
 * its own to emit. Return `W² + 2W = 3` from one and 0 from the other and the
 * consumer's later blocks all sit at the wrong offset.
 *
 * i1 IS A SHIPPED WIDTH (opcode_table.yaml:186), so this is not hypothetical. */
CQ_TEST(at_w1_the_block_has_no_region_and_no_steps)
{
    CHECK_EQ(cq_mul_region(1), 0);
    CHECK_EQ(cq_mul_steps(1), 0);

    /* And the region really is the K11.md §4 closed form everywhere else — the
     * one place in this file it is written down, because `cq_kd_peak` pins the
     * same number one case over through a completely different instrument. */
    for (int W = 2; W <= 8; W++)
        CHECK_EQ(cq_mul_region(W), W * W + 2 * W);
}

/* ---- TWO blocks, one region: the offset, and M34's actual shape. ------- */

/* `soft_fmul` FORMS FOUR PRODUCTS, NOT ONE (fmul.jl:78-81), so a consumer's
 * region holds several `cq_mul_block`s side by side and `off` is what keeps
 * them apart. This case is two of them — `pp_hl = a_hi * b_lo` and
 * `pp_lh = a_lo * b_hi`, the pair upstream immediately sums as `cross`
 * (fmul.jl:88) — in ONE region, under ONE sandwich, with a 2W-step copy-out.
 *
 * IT IS THE ONLY DETECTOR OF `k->off` IN THE PROJECT (see the file header):
 * with `off` dropped both blocks carve the same `W² + 2W` bits, so block 1's
 * accumulate starts on block 0's product instead of on |0>, block 1's `pp` rows
 * arrive already set, and the reverse half undoes neither. L1 sees the wrong
 * value, the pool sees scratch that did not come back, and the palindrome sees
 * a head of the wrong length — three independent instruments, which is what a
 * layout fault should trip.
 *
 * THE TWO BLOCKS USE FOUR DISTINCT OPERAND RAILS. Binding both to one pair
 * would be D7b at the kernel boundary — a Toffoli whose two controls are one
 * physical qubit — and would be testing the fold table rather than the layout. */
typedef struct {
    cq_bit      *dst0, *dst1;
    cq_mul_block k0, k1;
    int          W, steps;
} mb2_env;

static void mb2_compute(cq_ctx *ctx, void *env, int s)
{
    const mb2_env *e = (const mb2_env *)env;

    if (s < e->steps) cq_mul_step(ctx, &e->k0, s);
    else              cq_mul_step(ctx, &e->k1, s - e->steps);
}

static void mb2_copyout(cq_ctx *ctx, void *env, int i)
{
    const mb2_env *e = (const mb2_env *)env;

    if (i < e->W) cq_emit_cx(ctx, &cq_mul_product(&e->k0)[i], &e->dst0[i]);
    else          cq_emit_cx(ctx, &cq_mul_product(&e->k1)[i - e->W],
                             &e->dst1[i - e->W]);
}

static void mb2_case(int W, cq_ref_w qa, cq_ref_w qb, cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_scratch scr;
    mb2_env e;
    int32_t hs[6];
    size_t head;
    /* Two DIFFERENT value pairs, so a block reading the other's operands is a
     * wrong answer rather than a coincidence. */
    cq_ref_w v0 = cq_ref_w_and(cq_ref_w_make(0x0BADF00DCAFE5A5Aull, 0ull, W), qa);
    cq_ref_w v1 = cq_ref_w_and(cq_ref_w_make(0x00C0FFEE3C3C7777ull, 0ull, W), qb);
    cq_ref_w v2 = cq_ref_w_and(cq_ref_w_make(0x0123456789ABCDEFull, 0ull, W), qa);
    cq_ref_w v3 = cq_ref_w_and(cq_ref_w_make(0x0FEDCBA987654321ull, 0ull, W), qb);

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);

    hs[0] = cq_bk_reg_w(&ctx, (uint32_t)W, v0, qa);
    hs[1] = cq_bk_reg_w(&ctx, (uint32_t)W, v1, qb);
    hs[2] = cq_bk_reg_w(&ctx, (uint32_t)W, v2, qa);
    hs[3] = cq_bk_reg_w(&ctx, (uint32_t)W, v3, qb);
    hs[4] = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);
    hs[5] = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);

    cq_pc_snap before = cq_pc_take(&ctx);

    cq_scratch_alloc(&scr, (uint32_t)MB_PAD
                           + 2u * (uint32_t)cq_mul_region(W));

    e.W     = W;
    e.steps = cq_mul_steps(W);
    e.dst0  = cq_reg_bits(&ctx.regs, hs[4]);
    e.dst1  = cq_reg_bits(&ctx.regs, hs[5]);

    e.k0.a = cq_reg_cbits(&ctx.regs, hs[0]);
    e.k0.b = cq_reg_cbits(&ctx.regs, hs[1]);
    e.k0.scr = &scr; e.k0.off = (uint32_t)MB_PAD;           e.k0.W = W;

    e.k1.a = cq_reg_cbits(&ctx.regs, hs[2]);
    e.k1.b = cq_reg_cbits(&ctx.regs, hs[3]);
    e.k1.scr = &scr;
    e.k1.off = (uint32_t)MB_PAD + (uint32_t)cq_mul_region(W);
    e.k1.W = W;

    cq_mock_reset(m);
    cq_sandwich(&ctx, &scr, mb2_compute, 2 * e.steps, mb2_copyout, 2 * W, &e);
    cq_scratch_dispose(&scr);

    CHECK(cq_ref_w_eq(cq_pc_value_w(&ctx, hs[4]), cq_ref_w_mul(v0, v1, W)));
    CHECK(cq_ref_w_eq(cq_pc_value_w(&ctx, hs[5]), cq_ref_w_mul(v2, v3, W)));

    head = compute_half_gates(W, v0, qa, v1, qb)
         + compute_half_gates(W, v2, qa, v3, qb);
    CHECK(cq_mock_is_palindrome(m, head, 2u * (size_t)W));
    CHECK_EQ(cq_mock_count(m), 2u * head + 2u * (size_t)W);
    CHECK(cq_mock_count(m) > 0u);
    CHECK(cq_pc_live_is_exactly(&ctx, hs, 6u));

    /* The scratch came back: nothing is live but the six named rails, and the
     * pool's `live` is where it was before the region existed once the two
     * products are freed. */
    cq_reg_free(&ctx, hs[4], cq_pc_zero_proof_rotation_free);
    cq_reg_free(&ctx, hs[5], cq_pc_zero_proof_rotation_free);
    CHECK(cq_pc_same(before, cq_pc_take(&ctx)));
    CHECK(cq_pc_live_is_exactly(&ctx, hs, 4u));

    cq_ctx_dispose(&ctx);
}

CQ_TEST(the_two_blocks_in_one_region_do_not_collide)
{
    static const int WS[] = { 2, 3, 5, 8, 32 };
    cq_mock m;

    cq_mock_init(&m);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        int W = WS[i];

        mb2_case(W, cq_ref_w_ones(W), cq_ref_w_ones(W), &m);
    }

    /* And at M34's own width and mask: `pp_hl` (a_hi 27 lanes x b_lo 26) beside
     * `pp_lh` (26 x 27), which is the pair fmul.jl:88 sums as `cross`. */
    mb2_case(64, cq_ref_w_ones(27), cq_ref_w_ones(26), &m);

    cq_mock_dispose(&m);
}

/* ---- (c) M34's shape: fmul's 27/26-bit halves in a 64-bit span. --------- */

/* THE CONSUMER, MEASURED RATHER THAN IMAGINED. `soft_fmul` splits each 53-bit
 * significand into a high 27 and a low 26 bits and forms FOUR UInt64 x UInt64
 * products — `pp_ll = a_lo * b_lo`, `pp_lh`, `pp_hl`, `pp_hh`
 * (third_party/bennett/src/softfloat/fmul.jl:74-81). Under PRD-v2 §7.2's
 * LITERAL transcription grain each of those is one `cq_mul_block` at W = 64
 * whose operand's top 37 or 38 lanes are `CQ_BIT_ZERO`, so this is the mask
 * M34 will actually run and the one worth knowing the cost of.
 *
 * THE COUNT MUST BE STRICTLY BELOW THE ALL-QUANTUM COUNT, and the rule that
 * makes it so is narrow: a classical ZERO operand bit removes gates and a
 * classical ONE does not (CLAUDE.md; K09.md §3.3.1 asserted the opposite three
 * times). The top lanes here are ZERO, which is why `<` is assertable at all —
 * against a mask of classical ONES the correct assertion would be `==`.
 *
 * AND THE SAVING IS SMALL, WHICH IS THE FINDING. Only phase P sees the mask:
 * the W accumulates run in full whatever the operands are, because I6(b) makes
 * every scratch bit `CQ_BIT_Q` from step 0. So the halves buy back only the
 * folded partial products out of a compute half the accumulator dominates. The
 * exact figures are re-measured into K11.md's M34 section rather than written
 * here. */
CQ_TEST(the_fmul_half_width_operands_fold_phase_p_and_nothing_else)
{
    static const int LANES[4][2] = { {26,26}, {26,27}, {27,26}, {27,27} };
    const int W = 64;
    size_t all_q = 2u * (size_t)cq_mul_steps(W) + (size_t)W;
    size_t accs  = 2u * (size_t)W * (size_t)cq_addacc_steps(W) + (size_t)W;
    cq_mock m;

    cq_mock_init(&m);

    for (size_t i = 0; i < 4u; i++) {
        int na = LANES[i][0], nb = LANES[i][1];
        cq_ref_w qa = cq_ref_w_ones(na);
        cq_ref_w qb = cq_ref_w_ones(nb);
        /* Values inside the quantum lanes only: a bit set OUTSIDE the mask
         * would be a classical ONE, which folds nothing and would make the
         * strict inequality below assert the wrong rule. */
        cq_ref_w va = cq_ref_w_and(cq_ref_w_make(0x0BADF00DCAFE5A5Aull,
                                                 0ull, W), qa);
        cq_ref_w vb = cq_ref_w_and(cq_ref_w_make(0x00C0FFEE3C3C7777ull,
                                                 0ull, W), qb);
        size_t total = mb_case(W, va, qa, vb, qb, &m, NULL);

        /* Strictly cheaper than all-quantum — the folded phase-P Toffolis. */
        CHECK(total < all_q);

        /* And strictly DEARER than the accumulates alone, so some partial
         * product survived: an implication with no surviving gate would be
         * vacuous, and a block that emitted no phase P at all would pass a
         * bare `<`. */
        CHECK(total > accs);
    }

    cq_mock_dispose(&m);
}
