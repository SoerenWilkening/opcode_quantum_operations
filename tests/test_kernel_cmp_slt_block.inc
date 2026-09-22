/* tests/test_kernel_cmp_slt_block.inc — M16's SIGNED compare half AS AN
 * EXPORTED STEP BLOCK. Included by tests/test_kernel_cmp.c.
 *
 * THE SUBJECT IS THE EXPORT, AND THE FAULT IT EXISTS TO CATCH IS A SUBSTITUTION
 * RATHER THAN A BUG. K15.md's block table counts FOUR signed compares on a
 * `result_exp` that `fadd.jl` declares `Int64`, and PRD-v2 §5's M32 (`fpround`)
 * owns all four. Under PRD-v2 §7.2's literal grain `Int64` and `UInt64` are the
 * same sixty-four `cq_bit`s, so nothing in a consumer's spans says which
 * comparator a row wants: reach for `cq_ult_block` on a signed row and the
 * circuit is well formed, the gate tuple has the same shape, the palindrome
 * holds, the scratch comes back — and the answer is wrong exactly when
 * `result_exp` is negative, which is the subnormal path. Only an L1-shaped
 * value check against a SIGNED oracle sees it, which is case (c).
 *
 * EVERY CASE HERE READS NO GOLDEN. `CQOPS_UPDATE_GOLDENS=1` is the documented
 * way to make a red L4 green, so a golden blesses a mutant as readily as a fix,
 * and the mutant this block invites is K09.md §5 delta 11's — the identity
 * `a <s b = (a <u b) ⊕ a_msb ⊕ b_msb`, which deletes the whole bias phase for
 * `4W + 2` gates and `2W` qubits and reads exactly like an optimisation. It is
 * NOT ADOPTED (Rule 1: it is not in Bennett), and what holds the line is case
 * (b), which asks the MODULE what the block costs and the kernel what it costs
 * and compares the two — never a number written in this file.
 *
 * SCRATCH SPANS, NOT REGISTERS, for `cq_ult_block`'s measured reason: the carry
 * chain is `W + 1` bits, so at `CQ_REG_WIDTH_MAX` it cannot be a register at
 * all, and inside a sandwich a block's operands are pre-materialised scratch
 * rather than rails anyway.
 */

/* K9's shipped ladder (opcode_table.yaml:222), which is this block's too.
 * 64 is the width M32/M33 consume; 1 is where `af`/`bf` are one bit each and
 * the two MSB flips land on the same lanes the copies just wrote; 80 is above
 * the reference's 64-bit word seam. */
static const int SLTB_WS[] = { 1, 8, 16, 32, 64, 80 };
enum { SLTB_W_MAX = 80, SLTB_NAMED_MAX = 8 * SLTB_W_MAX };

/* The six anchors of case (c), forced INSIDE the shared constant budget rather
 * than stacked on top of it. */
enum { SLTB_ANCHORS = 6 };

/* ONE FIXTURE, TWO REGIONS, and the split is the block's own contract: `ops`
 * holds what the caller supplies as CONTROLS (`a`, `b`) and `work` holds the
 * five spans the block writes. Only `work` has to come back to |0>; `a` and `b`
 * carry live values and are nobody's ancilla. */
typedef struct {
    cq_ctx       ctx;
    cq_scratch   ops, work;
    cq_slt_block k;
    int32_t      hd;                 /* the one-bit dst a copy-out targets */
    uint32_t     named[SLTB_NAMED_MAX];
    uint32_t     n_named;
    cq_pc_snap   pristine;           /* the pool before anything was minted */
} sltb_fx;

static void sltb_mint(cq_ctx *ctx, cq_bit *v, int n, cq_ref_w value)
{
    for (int i = 0; i < n; i++) {
        v[i] = cq_bit_const(cq_ref_w_bit(value, i));
        cq_materialise(ctx, &v[i]);
    }
}

static void sltb_const(cq_bit *v, int n, cq_ref_w value)
{
    for (int i = 0; i < n; i++) v[i] = cq_bit_const(cq_ref_w_bit(value, i));
}

static uint32_t sltb_collect(const cq_scratch *scr, uint32_t *out, uint32_t n)
{
    for (uint32_t i = 0; i < scr->n; i++)
        if (cq_bit_is_qubit(scr->bits[i])) out[n++] = cq_bit_qindex(scr->bits[i]);
    return n;
}

/* THE FIVE SPANS ARE LAID OUT BACKWARDS FROM cmp.c's OWN `layout()`, AND THAT
 * IS THE eq FIXTURE'S RULE APPLIED TO A SECOND BLOCK. cmp.c carves one region
 * as `af ++ bf ++ nb ++ carry ++ axnb`; a fixture that copied it would make
 * three adjacencies true by accident — `&carry[W + 1]` would BE `axnb[0]`, so
 * a flag accessor that over-ran by one would read a live scratch bit instead of
 * running off the region, and `af[W]` would BE `bf[0]`, so a bias flip that
 * targeted the wrong copy's MSB would still land on a legal bit. Here the order
 * is `axnb ++ nb ++ bf ++ af ++ carry`: `carry` is LAST, so an over-run leaves
 * the region entirely (ASan convicts in Debug), and `af` and `bf` are adjacent
 * in the OPPOSITE order, so an off-by-`W` lands on a different span than it
 * would in the library. The block imposes no order on its spans, which is
 * exactly why copying the library's silently retires every fault only a
 * different order can reach.
 *
 * `a_quantum` / `b_quantum` are the MASK PAIR, one bit per operand — risk R8's
 * asymmetric witness is inexpressible with a single mask. The work region is
 * materialised whole, which is I6(b). */
static void sltb_open(sltb_fx *f, int W, cq_ref_w va, cq_ref_w vb,
                      int a_quantum, int b_quantum, const cq_sink *sink)
{
    cq_bit  *pa, *pb;
    uint32_t w = (uint32_t)W;

    cq_ctx_init(&f->ctx, sink);
    f->pristine = cq_pc_take(&f->ctx);   /* taken BEFORE the mint — see (a) */

    cq_scratch_alloc(&f->ops,  2u * w);
    cq_scratch_alloc(&f->work, 5u * w + 1u);

    pa          = cq_scratch_span(&f->ops,  0u,          w);
    pb          = cq_scratch_span(&f->ops,  w,           w);
    f->k.axnb   = cq_scratch_span(&f->work, 0u,          w);
    f->k.nb     = cq_scratch_span(&f->work, w,           w);
    f->k.bf     = cq_scratch_span(&f->work, 2u * w,      w);
    f->k.af     = cq_scratch_span(&f->work, 3u * w,      w);
    f->k.carry  = cq_scratch_span(&f->work, 4u * w,      w + 1u);
    f->k.a      = pa;
    f->k.b      = pb;
    f->k.W      = W;

    if (a_quantum) sltb_mint(&f->ctx, pa, W, va);
    else           sltb_const(pa, W, va);
    if (b_quantum) sltb_mint(&f->ctx, pb, W, vb);
    else           sltb_const(pb, W, vb);
    for (uint32_t i = 0; i < f->work.n; i++)
        cq_materialise(&f->ctx, &f->work.bits[i]);

    f->hd = cq_reg_alloc_zero(&f->ctx.regs, 1u);

    f->n_named = sltb_collect(&f->ops,  f->named, 0u);
    f->n_named = sltb_collect(&f->work, f->named, f->n_named);
}

/* The regions go back to nobody — cq_ctx_dispose's intended Rule-6 safe leak
 * (PRD §10) — and the cq_bit entries are reset first because M08's dispose
 * check is a KIND check. A case that wants the qubits BACK calls sltb_reclaim
 * first. */
static void sltb_close(sltb_fx *f)
{
    for (uint32_t i = 0; i < f->ops.n;  i++) f->ops.bits[i]  = cq_bit_zero();
    for (uint32_t i = 0; i < f->work.n; i++) f->work.bits[i] = cq_bit_zero();
    cq_scratch_dispose(&f->ops);
    cq_scratch_dispose(&f->work);
    cq_ctx_dispose(&f->ctx);
}

static void sltb_forward(sltb_fx *f)
{
    for (int u = 0; u < cq_slt_steps(f->k.W); u++) cq_slt_step(&f->ctx, &f->k, u);
}

static void sltb_reverse(sltb_fx *f)
{
    for (int u = cq_slt_steps(f->k.W) - 1; u >= 0; u--)
        cq_slt_step(&f->ctx, &f->k, u);
}

/* THE SHADOW IS THE PROOF, DELIBERATELY. cq_sandwich releases its own region
 * under CQ_ZERO_BY_PALINDROME, a literal 1 (src/sandwich.c), which would
 * launder exactly the fault this fixture exists to see; the shadow is EXACT
 * rather than conservative on a rotation-free surface (poolcheck.h). Checked
 * BEFORE releasing: cq_qubits_release aborts on an index not proven |0>, and an
 * abort reports as a crashed binary rather than as a failed case. */
static void sltb_reclaim(sltb_fx *f, cq_scratch *scr, const char *what)
{
    int clean = 1;

    for (uint32_t i = 0; i < scr->n; i++) {
        if (!cq_bit_is_qubit(scr->bits[i])) continue;
        if (!cq_shadow_known_zero(&f->ctx.shadow, cq_bit_qindex(scr->bits[i]))) {
            cq_h_fail(__FILE__, __LINE__,
                      "slt block W=%d: %s bit %u (q%u) is not back at |0> after "
                      "the reverse half", f->k.W, what, i,
                      cq_bit_qindex(scr->bits[i]));
            clean = 0;
        }
    }
    if (!clean) return;

    for (uint32_t i = 0; i < scr->n; i++) {
        if (cq_bit_is_qubit(scr->bits[i]))
            cq_ctx_release_qubit(&f->ctx, cq_bit_qindex(scr->bits[i]), 1);
        scr->bits[i] = cq_bit_zero();
    }
    cq_scratch_dispose(scr);
}

/* L2 AS A SET, SPELLED OVER RAW INDICES. cq_pc_live_is_exactly takes HANDLES,
 * and a block's operands and scratch are owned by no register, so the union
 * form has to be written here: every named index live PLUS live == named is set
 * equality, because the named indices are distinct by construction (I2, I3).
 * A count on its own is the measured L2-masked-by-L3 weakness. */
static void sltb_live_is_exactly(sltb_fx *f, uint32_t n, const char *when)
{
    for (uint32_t i = 0; i < n; i++)
        if (cq_qubits_is_free(&f->ctx.pool, f->named[i]))
            cq_h_fail(__FILE__, __LINE__,
                      "L2 slt block W=%d %s: q%u is on the free list",
                      f->k.W, when, f->named[i]);

    if (cq_qubits_live(&f->ctx.pool) != n)
        cq_h_fail(__FILE__, __LINE__,
                  "L2 slt block W=%d %s: %u live, %u named — an index is live "
                  "that nothing owns", f->k.W, when,
                  cq_qubits_live(&f->ctx.pool), n);
}

/* ---- (a) The block is a palindrome, and it gives the pool back. ---------- */

/* DRIVEN THE WAY A CONSUMER DRIVES IT — a contiguous run of step indices mapped
 * onto [0, cq_slt_steps(W)) — and NOT through cq_sandwich, which would be M09's
 * suite again and would release the region under its own literal-1 proof.
 *
 * THREE MASK ROWS, AND THE TWO ASYMMETRIC ONES ARE THE POINT. `a` all Q with
 * `b` all CQ_BIT_ZERO is risk R8's named witness and is in no symmetric mask
 * set; the MIRROR is a second row and not the same one, because `lower_slt!` is
 * not symmetric in its operands — the inner `lower_ult!` reads `a` twice per
 * stage and `~b` three times (arith.jl:456-459), so a classical ZERO folds a
 * different number of gates on each side (K09.md §3.3.1). An ALL-CLASSICAL pair
 * would not be a palindrome case at all but an L5 one — the stream it asks
 * about does not exist.
 *
 * THE STEP COUNT IS ASKED OF THE MODULE *AND* RE-DERIVED HERE. `n_compute_of`
 * (tests/test_kernel_cmp.c) writes `8W + 3` out as a literal from K09.md §2.3,
 * sharing nothing with `cq_slt_steps`'s `2W + 2 + cq_ult_steps(W)` — which is
 * the step-count detector, because a composition identity whose two sides both
 * ask the function under check cannot see that function move (the measured M12
 * finding). At the all-quantum mask the emitted head is that number too, which
 * is plan §0.4 obligation 1 and what makes index reversal BE gate reversal; at
 * an asymmetric mask the head is MEASURED, because a CQ_BIT_ZERO control folds
 * its gate away and a step there legitimately costs 0.
 *
 * The operands are minted from |0>, so the raw flag is 1 (`0 >=s 0`) and the
 * copy-out leaves `dst` at 1; it is flipped back before the free, which is what
 * lets L3 compare against the pristine pool. The VALUES are irrelevant to the
 * recorded stream — the §3 fold table reads a bit's KIND and never a qubit's
 * value (D6) — and the value claim is case (c)'s. */
CQ_TEST(the_slt_block_is_a_palindrome_and_gives_the_pool_back)
{
    static const int MASKS[3][2] = { { 1, 1 }, { 1, 0 }, { 0, 1 } };
    cq_mock m;

    cq_mock_init(&m);

    for (size_t j = 0; j < sizeof SLTB_WS / sizeof SLTB_WS[0]; j++)
        for (size_t mk = 0; mk < 3u; mk++) {
            int W = SLTB_WS[j];
            cq_sink s = cq_mock_sink(&m);
            sltb_fx f;
            cq_pc_snap after;
            size_t head, mid;
            uint32_t held[2], n_held;

            CHECK_EQ((long long)cq_slt_steps(W),
                     (long long)n_compute_of(P_SLT, W));

            sltb_open(&f, W, cq_ref_w_zero(), cq_ref_w_zero(),
                      MASKS[mk][0], MASKS[mk][1], &s);
            sltb_live_is_exactly(&f, f.n_named, "after the MINT");

            cq_mock_reset(&m);
            sltb_forward(&f);
            head = cq_mock_count(&m);
            if (MASKS[mk][0] && MASKS[mk][1])
                CHECK_EQ((long long)head, (long long)cq_slt_steps(W));

            /* Plan §0.4 obligation 2: the block allocates nothing. */
            sltb_live_is_exactly(&f, f.n_named, "after the FORWARD");

            cq_emit_cx(&f.ctx, cq_slt_flag(&f.k), cq_reg_bits(&f.ctx.regs, f.hd));
            mid = cq_mock_count(&m) - head;
            CHECK_EQ((long long)mid, 1);
            n_held = cq_pc_indices(&f.ctx, f.hd, held, 2u);
            CHECK_EQ((long long)n_held, 1);
            f.named[f.n_named] = held[0];

            sltb_reverse(&f);
            CHECK(cq_mock_is_palindrome(&m, head, mid));
            sltb_live_is_exactly(&f, f.n_named + 1u, "after the UNCOMPUTE");

            sltb_reclaim(&f, &f.work, "scratch");
            sltb_reclaim(&f, &f.ops,  "operands");
            if (!cq_pc_live_is_exactly(&f.ctx, &f.hd, 1u))
                cq_h_fail(__FILE__, __LINE__,
                          "L2 slt block W=%d: after the RECLAIM, only dst may "
                          "be live", W);

            /* L3 AGAINST THE PRISTINE POOL, WHICH IS THE ONE SNAPSHOT THAT CAN
             * RETURN — a snapshot taken after the mint counts the fixture's own
             * operands, and reclaiming them is the thing being asserted.
             * `minted`, `peak` and the free-list length are never compared: all
             * three are monotone (src/qubits.h). AND THE INDEX SET IS THE SHARP
             * HALF — a release of the WRONG index restores `live` exactly and
             * no count can see it.
             *
             * `dst` carries the copy-out and must be zeroed before the free,
             * because cq_reg_free releases against the shadow and a rail
             * holding 1 is correctly STRANDED. It is zeroed from its OWN
             * MEASURED VALUE and never from "the flag at |0> operands is 1":
             * writing that constant down would couple this case to a value
             * claim, and, measured, it does — under the step-count mutant the
             * flag comes out 0, the hard-coded X made dst 1, and case (a) went
             * red on L3 for a reason that is case (c)'s. A pool case must fail
             * on the pool. */
            if (cq_pc_value(&f.ctx, f.hd) != 0u)
                cq_emit_x(&f.ctx, cq_reg_bits(&f.ctx.regs, f.hd));
            cq_reg_free(&f.ctx, f.hd, cq_pc_zero_proof_rotation_free);
            after = cq_pc_take(&f.ctx);
            if (!cq_pc_same(f.pristine, after))
                cq_h_fail(__FILE__, __LINE__,
                          "L3 slt block W=%d: pool not restored — live %u -> %u",
                          W, f.pristine.live, after.live);
            if (!cq_pc_indices_are_free(&f.ctx, f.named, f.n_named + 1u))
                cq_h_fail(__FILE__, __LINE__, "L3 slt block W=%d: see above", W);

            sltb_close(&f);
        }

    cq_mock_dispose(&m);
}

/* ---- (b) The four signed kernels are the block twice plus their copy-out. - */

/* The block at the ALL-QUANTUM mask, measured through the counting sink. */
static void sltb_block_cost(int W, cq_counter *out)
{
    cq_counter cnt;
    cq_sink s;
    sltb_fx f;

    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);
    sltb_open(&f, W, cq_ref_w_zero(), cq_ref_w_zero(), 1, 1, &s);
    cq_count_reset(&cnt);                        /* drop the materialising Xs */
    sltb_forward(&f);
    *out = cnt;
    sltb_close(&f);
}

/* THE ASSERTION THAT SURVIVES `CQOPS_UPDATE_GOLDENS=1`. The four signed
 * predicates are Bennett-in-the-small over this block — compute, copy out,
 * compute backwards (PRD §5) — so each kernel's tuple is exactly twice what the
 * block just cost plus its copy-out, and BOTH terms are MEASURED: the block by
 * driving it, the kernel by cq_kd_measure. Nothing here is a formula, which is
 * what makes it the detector for K09.md §5 delta 11's NOT-ADOPTED shortcut:
 * dropping the bias phase moves the kernel by 4W+2 and the block by 2W+2, and
 * only one of those is a golden.
 *
 * The copy-out is 1 CX always, plus 1 X for the two predicates whose raw flag
 * is the NEGATION of the answer. The raw flag is `a >=s b`, so `slt` and `sgt`
 * pay the X and `sle` and `sge` copy it out unchanged — Bennett's trailing
 * `NOT` folded into the copy-out (arith.jl:461, K09.md §5 delta 2). The operand
 * SWAP that distinguishes `sgt` from `slt` costs nothing at all, which is why
 * all four terms come off one measured block.
 *
 * Forward and `_unc` are measured and asserted SEPARATELY against the same
 * measured block. That is not the R6 mistake: the claim is not
 * `unc == forward` but that each pass, at the all-quantum mask cq_kd_measure
 * drives, is the block twice plus the copy-out.
 *
 * AND IT IS BLIND TO `cq_slt_steps` ITSELF, MEASURED — which is why the step
 * count is pinned in case (a) and not here. Both sides of this identity drive
 * the block for `cq_slt_steps(W)` steps, so shortening that function by one
 * moves them together and this case stays GREEN while eight others go red
 * (measured in Release). That is the M12 export finding verbatim: a composition
 * identity whose two sides ask one function cannot see that function move. */
CQ_TEST(the_slt_kernels_are_the_block_twice_plus_their_copyout)
{
    for (size_t j = 0; j < sizeof SLTB_WS / sizeof SLTB_WS[0]; j++) {
        int W = SLTB_WS[j];
        cq_counter blk;

        sltb_block_cost(W, &blk);
        CHECK_EQ(blk.ry + blk.rz + blk.mz, 0);
        CHECK(blk.x + blk.cx + blk.ccx > 0u);

        for (int i = 6; i <= 9; i++) {      /* ROWS 6..9 = slt, sgt, sle, sge */
            cq_counter fwd, unc;
            uint64_t inv = (uint64_t)(ROWS[i].invert ? 1 : 0);

            cq_kd_measure(&ROWS[i].spec, W, &fwd, &unc);
            CHECK_GATES(fwd.x, fwd.cx, fwd.ccx,
                        2u * blk.x + inv, 2u * blk.cx + 1u, 2u * blk.ccx);
            CHECK_GATES(unc.x, unc.cx, unc.ccx,
                        2u * blk.x + inv, 2u * blk.cx + 1u, 2u * blk.ccx);
        }
    }
}

/* ---- (c) The flag is `a >=s b`, which is the row a consumer gets wrong. --- */

/* The six anchors, as (a, b) pairs at width W. They are what separates the
 * SIGNED block from the unsigned one, and a uniform draw does not reach them:
 * at W = 64 a random pair is `a >=s b` and `a >=u b` alike with probability
 * 3/4, so the two families agree on three quarters of the space and disagree
 * only where the sign bits differ. Rows 1-3 and 6 are exactly that disagreement
 * — put `cq_ult_block` behind this flag and every one of them inverts, while
 * rows 0 and 5 (equal operands, and a positive pair) agree under both readings
 * and are there for the other half: equal must read 1, because the flag is
 * `>=` and not `>`, and a uniform draw at W = 64 never produces a == b. */
static void sltb_anchor(int i, int W, cq_ref_w *a, cq_ref_w *b)
{
    cq_ref_w zero = cq_ref_w_zero();
    cq_ref_w mone = cq_ref_w_ones(W);                   /* -1 at every width */
    cq_ref_w imin = cq_ref_w_setbit(W - 1);             /* INT_MIN           */
    cq_ref_w imax = cq_ref_w_andnot(mone, imin);        /* INT_MAX; 0 at W=1 */
    cq_ref_w one  = cq_ref_w_make(1u, 0u, W);

    switch (i) {
    case 0:  *a = mone; *b = mone; break;    /* equal: `>=` must read 1      */
    case 1:  *a = mone; *b = zero; break;    /* -1 >=s 0 is FALSE, >=u TRUE  */
    case 2:  *a = imin; *b = zero; break;    /* the sign bit alone           */
    case 3:  *a = imin; *b = imax; break;    /* the extremes, signed order   */
    case 4:  *a = zero; *b = one;  break;    /* both positive, so both agree */
    default: *a = zero; *b = imin; break;    /* the MIRROR of row 2          */
    }
}

/* BENNETT'S `lower_slt!` RETURNS THE NEGATION OF `slt`. Its trailing NOT is the
 * copy-out's, folded there by K09.md §5 delta 2, so the wire the block leaves
 * holds `a >=s b` and it is `sge` that copies it out unchanged. A consumer that
 * reads cq_slt_flag as "less than" builds a correct-looking circuit computing
 * the complement, and NOTHING STRUCTURAL CAN SEE IT: same gates, same count,
 * same palindrome, clean scratch. That is K9's uge-meaning-ule one layer down.
 *
 * THE ORACLE IS cq_ref_icmp's SGE ROW, which branches on the two sign bits
 * (refmodel.h) rather than repeating the kernel's bias flip — so it shares no
 * arithmetic with the construction under test. It is not cmp.c's `const_raw`
 * either, which applies the same bias the circuit does.
 *
 * `a` IS QUANTUM AND `b` IS CLASSICAL, which is the mask M32 actually runs: its
 * four compares are all `result_exp` against a `const cq_bit[64]` constant
 * (PRD-v2 §7.3, constants are SOURCES). It also keeps the anchors' `b` values
 * off the qubit pool, so a run at W = 80 mints 80 qubits and not 160. */
CQ_TEST(the_slt_blocks_flag_is_a_greater_equal_s_b)
{
    /* A sink is REQUIRED: cq_ctx_init(NULL) takes cq_sink_active(), and an
     * unresolvable default is a hard error rather than a quiet substitution
     * (M04). The counter is the cheapest one that exists; nothing reads it. */
    cq_counter cnt;
    cq_sink s;

    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);

    for (size_t j = 0; j < sizeof SLTB_WS / sizeof SLTB_WS[0]; j++) {
        int W = SLTB_WS[j];
        cq_bk_rng rng;
        int n = cq_kd_samples();

        cq_bk_rng_init(&rng, 0xC2B2AE3D27D4EB4Fu ^ (uint64_t)W);

        for (int i = 0; i < n; i++) {
            sltb_fx f;
            cq_shadow sv;
            cq_ref_w va = cq_ref_w_make(cq_bk_rng_next(&rng),
                                        cq_bk_rng_next(&rng), W);
            cq_ref_w vb = cq_ref_w_make(cq_bk_rng_next(&rng),
                                        cq_bk_rng_next(&rng), W);
            int want;

            if (i < SLTB_ANCHORS) sltb_anchor(i, W, &va, &vb);
            want = cq_ref_icmp(CQ_ICMP_SGE, va, vb, W);

            sltb_open(&f, W, va, vb, 1, 0, &s);
            sltb_forward(&f);

            sv = cq_shadow_get(&f.ctx.shadow, cq_bit_qindex(*cq_slt_flag(&f.k)));
            if (sv.unknown || (int)sv.value != want)
                cq_h_fail(__FILE__, __LINE__,
                          "slt block W=%d draw %d: flag = %s%u, want a >=s b = "
                          "%d (a = 0x%llx%016llx, b = 0x%llx%016llx)", W, i,
                          sv.unknown ? "unknown/" : "", sv.value, want,
                          (unsigned long long)va.hi, (unsigned long long)va.lo,
                          (unsigned long long)vb.hi, (unsigned long long)vb.lo);

            /* Ancilla-clean with LIVE operands, which the palindrome case (at
             * |0> operands) cannot claim: the reverse half has to cancel
             * against whatever `a` actually holds. */
            sltb_reverse(&f);
            sltb_reclaim(&f, &f.work, "scratch");
            sltb_close(&f);
        }
    }
}

/* ---- (d) Two blocks, one region. ---------------------------------------- */

/* MANDATORY FOR EVERY `cq_*_block` EXPORT (the measured M18 finding: a single
 * block cannot see its own offset, so a block that slid wholesale kept L1–L5
 * green and only a SECOND block in the same region collided with it).
 *
 * WHAT IT CATCHES HERE IS LESS THAN THAT, AND SAYING SO IS THE POINT. M18's
 * and M12's blocks carry an `off` into a shared region, which is the field a
 * mutant drops; `cq_slt_block` carries no offset and no state — all five spans
 * are POINTERS the caller supplies — so the fault that case is the sole
 * detector for is not representable here. Measured over seven Release mutants:
 * this case kills four of them and is the UNIQUE killer of none. It is kept
 * anyway, for two reasons that are not coverage arithmetic. (i) It is the only
 * fixture in the project that runs two `cq_slt_block`s out of ONE region, which
 * is the shape M32 runs — `_sf_round_and_pack` has four signed compares inside
 * one sandwich — so it is the calling shape rather than a contrivance. (ii) It
 * is the detector waiting for the first mutable-state "optimisation": a cached
 * or file-static inner `cq_ult_block`, built once and reused, is inert with one
 * block and catastrophic with two, and is exactly the hazard K15.md forbids for
 * `off[]` one layer up.
 *
 * The two blocks are given operand pairs whose ANSWERS DIFFER — 0 vs the MSB
 * (`0 >=s INT_MIN` is true) and its mirror (`INT_MIN >=s 0` is false) — so a
 * collision cannot be masked by the two flags agreeing. The four operand spans
 * are distinct too, so a block reaching its neighbour's `a` reads a different
 * value rather than the same one.
 *
 * TWO REGIONS ARE NOT WHAT IS TESTED HERE: both blocks are carved out of ONE
 * `work` region, at offsets 0 and 5W+1, which is the shape M32 runs — four
 * signed compares inside one `fpround` sandwich. */
static void sltb_bind(cq_slt_block *k, cq_scratch *scr, uint32_t base, int W,
                      const cq_bit *a, const cq_bit *b)
{
    uint32_t w = (uint32_t)W;

    k->axnb  = cq_scratch_span(scr, base,          w);
    k->nb    = cq_scratch_span(scr, base + w,      w);
    k->bf    = cq_scratch_span(scr, base + 2u * w, w);
    k->af    = cq_scratch_span(scr, base + 3u * w, w);
    k->carry = cq_scratch_span(scr, base + 4u * w, w + 1u);
    k->a     = a;
    k->b     = b;
    k->W     = W;
}

CQ_TEST(the_two_slt_blocks_in_one_region_do_not_collide)
{
    static const int WS[] = { 1, 2, 5, 8, 64 };
    cq_counter cnt;
    cq_sink s;

    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++) {
        int W = WS[j];
        uint32_t w = (uint32_t)W;
        cq_ctx ctx;
        cq_scratch ops, work;
        cq_slt_block k[2];
        cq_bit *v[4];
        cq_pc_snap before;
        int want[2];

        cq_ctx_init(&ctx, &s);
        before = cq_pc_take(&ctx);

        cq_scratch_alloc(&ops,  4u * w);
        cq_scratch_alloc(&work, 2u * (5u * w + 1u));
        for (int i = 0; i < 4; i++)
            v[i] = cq_scratch_span(&ops, (uint32_t)i * w, w);

        /* block 0: 0 >=s INT_MIN is TRUE; block 1: INT_MIN >=s 0 is FALSE. */
        sltb_mint(&ctx, v[0], W, cq_ref_w_zero());
        sltb_mint(&ctx, v[1], W, cq_ref_w_setbit(W - 1));
        sltb_mint(&ctx, v[2], W, cq_ref_w_setbit(W - 1));
        sltb_mint(&ctx, v[3], W, cq_ref_w_zero());
        for (uint32_t i = 0; i < work.n; i++)
            cq_materialise(&ctx, &work.bits[i]);

        sltb_bind(&k[0], &work, 0u,          W, v[0], v[1]);
        sltb_bind(&k[1], &work, 5u * w + 1u, W, v[2], v[3]);

        want[0] = cq_ref_icmp(CQ_ICMP_SGE, cq_ref_w_zero(),
                              cq_ref_w_setbit(W - 1), W);
        want[1] = cq_ref_icmp(CQ_ICMP_SGE, cq_ref_w_setbit(W - 1),
                              cq_ref_w_zero(), W);
        CHECK(want[0] != want[1] || W == 1);

        for (int b = 0; b < 2; b++)
            for (int u = 0; u < cq_slt_steps(W); u++) cq_slt_step(&ctx, &k[b], u);

        for (int b = 0; b < 2; b++) {
            cq_shadow sv = cq_shadow_get(&ctx.shadow,
                                         cq_bit_qindex(*cq_slt_flag(&k[b])));

            if (sv.unknown || (int)sv.value != want[b])
                cq_h_fail(__FILE__, __LINE__,
                          "two slt blocks W=%d: block %d's flag = %s%u, want "
                          "%d — a block that reached outside its own five "
                          "spans writes over its neighbour", W, b,
                          sv.unknown ? "unknown/" : "", sv.value, want[b]);
        }

        for (int b = 1; b >= 0; b--)
            for (int u = cq_slt_steps(W) - 1; u >= 0; u--)
                cq_slt_step(&ctx, &k[b], u);

        /* Both halves ran, so the whole SHARED region must be back at |0> — a
         * collision that cancelled in the stream but not in the state shows up
         * here and nowhere else. The operands are NOT released: they hold live
         * values (INT_MIN has a bit set), and handing cq_ctx_release_qubit a
         * proven_zero of 1 for them would be the lie Rule 6 exists to forbid.
         * They go back to nobody, which is cq_ctx_dispose's intended safe leak,
         * so the pool claim is the union form against `before` PLUS the four
         * operand spans rather than cq_pc_same. */
        for (uint32_t i = 0; i < work.n; i++) {
            if (!cq_bit_is_qubit(work.bits[i])) continue;
            if (!cq_shadow_known_zero(&ctx.shadow, cq_bit_qindex(work.bits[i])))
                cq_h_fail(__FILE__, __LINE__,
                          "two slt blocks W=%d: work bit %u is not back at |0>",
                          W, i);
            cq_ctx_release_qubit(&ctx, cq_bit_qindex(work.bits[i]), 1);
            work.bits[i] = cq_bit_zero();
        }
        for (uint32_t i = 0; i < ops.n; i++) ops.bits[i] = cq_bit_zero();
        cq_scratch_dispose(&work);
        cq_scratch_dispose(&ops);

        if (cq_qubits_live(&ctx.pool) != before.live + 4u * w)
            cq_h_fail(__FILE__, __LINE__,
                      "L2 two slt blocks W=%d: %u live, want %u — the shared "
                      "region did not come back", W,
                      cq_qubits_live(&ctx.pool), before.live + 4u * w);
        cq_ctx_dispose(&ctx);
    }
}
