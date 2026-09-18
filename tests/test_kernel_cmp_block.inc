/* tests/test_kernel_cmp_block.inc — M16's `eq`/`ne` compute half AS AN
 * EXPORTED STEP BLOCK. Included by tests/test_kernel_cmp.c.
 *
 * THE SUBJECT IS THE EXPORT, NOT THE COMPARATOR. Whether `icmp eq` computes the
 * right bit is the sweep's job next door, against a reference that transcribes
 * `lower_icmp!` a second time. What is new here is that a CONSUMER can drive
 * `lower_eq!`'s compute half itself: PRD-v2 §7.10 names `eq`/`ne` as one of the
 * three step blocks v1 owes the fp port before its first kernel, and PRD-v2 §5's
 * M31 (the IEEE class predicates) and M36 (`fcmp`) rows compose it at W = 64.
 * A consumer cannot call `cq_kernel_eq` — it is a whole sandwich and
 * `cq_sandwich` refuses nesting in both configurations — so the block is the
 * only door, and these three cases are what says it is a real one.
 *
 * EVERY CASE HERE READS NO GOLDEN, and that is the K11 finding applied in
 * advance. `CQOPS_UPDATE_GOLDENS=1` is the documented way to make a red L4
 * green, so a golden blesses a mutant as readily as a fix — and the mutant this
 * particular block invites is K11's shape exactly: the OR-prefix makes each
 * stage's earlier `diff` lanes look redundant, so shortening the reduction
 * reads as an optimisation and is a re-derivation (Rule 1). What is asserted
 * below is the block against what the MODULE says it costs, never against a
 * number written in this file.
 *
 * SCRATCH SPANS, NOT REGISTERS, and the reason is faithfulness rather than a
 * ceiling. Inside a sandwich a block's operands are pre-materialised scratch,
 * not rails (CLAUDE.md's `cq_ult_block` callout, measured at W = 128 where a
 * W+1 carry chain cannot be a register at all). eq's widest span is W, so a
 * register WOULD fit at every width here — which is exactly why the reason is
 * stated instead of the ceiling: a fixture built out of rails would be
 * exercising a calling shape no consumer uses.
 */

/* K9's shipped ladder (opcode_table.yaml:222), which is this block's too.
 * 64 is the width the fp port consumes (PRD-v2 §7.10); 1 is where `orr` is
 * empty and the flag moves to `diff[0]`; 80 is above the reference's 64-bit
 * word seam. */
static const int EQB_WS[] = { 1, 8, 16, 32, 64, 80 };
enum { EQB_W_MAX = 80, EQB_NAMED_MAX = 4 * EQB_W_MAX };

/* ONE FIXTURE, TWO REGIONS, and the split is the block's own contract: `ops`
 * holds what the caller supplies as CONTROLS (`a`, `b`) and `work` holds what
 * the block writes (`diff`, `orr`). Only `work` has to come back to |0>; `a`
 * and `b` carry live values and are nobody's ancilla. Two regions are legal
 * here because no sandwich is open — I6(a)'s extent is a single contiguous
 * range and is armed by cq_sandwich, which this fixture deliberately does not
 * use (see the palindrome case). */
typedef struct {
    cq_ctx      ctx;
    cq_scratch  ops, work;
    cq_eq_block k;
    int32_t     hd;                  /* the one-bit dst a copy-out targets */
    uint32_t    named[EQB_NAMED_MAX];
    uint32_t    n_named;
    cq_pc_snap  pristine;            /* the pool before anything was minted */
} eqb_fx;

/* A span holding `value`, built the one sanctioned way (Rule 5): set the
 * constant, then cq_materialise, which emits the X for a set bit. Those Xs are
 * recorded, so every caller resets its sink afterwards. */
static void eqb_mint(cq_ctx *ctx, cq_bit *v, int n, cq_ref_w value)
{
    for (int i = 0; i < n; i++) {
        v[i] = cq_bit_const(cq_ref_w_bit(value, i));
        cq_materialise(ctx, &v[i]);
    }
}

static void eqb_const(cq_bit *v, int n, cq_ref_w value)
{
    for (int i = 0; i < n; i++) v[i] = cq_bit_const(cq_ref_w_bit(value, i));
}

static uint32_t eqb_collect(const cq_scratch *scr, uint32_t *out, uint32_t n)
{
    for (uint32_t i = 0; i < scr->n; i++)
        if (cq_bit_is_qubit(scr->bits[i])) out[n++] = cq_bit_qindex(scr->bits[i]);
    return n;
}

/* `b_quantum` is one bit of the MASK PAIR: `a` is always all-Q and `b` is
 * either all-Q or all-CQ_BIT_ZERO. The work region is materialised whole,
 * which is I6(b) — the one thing that makes a step emit the same gate forwards
 * and backwards. */
static void eqb_open(eqb_fx *f, int W, cq_ref_w va, cq_ref_w vb, int b_quantum,
                     const cq_sink *sink)
{
    cq_bit *pa, *pb;

    cq_ctx_init(&f->ctx, sink);
    f->pristine = cq_pc_take(&f->ctx);   /* taken BEFORE the mint — see (a) */

    cq_scratch_alloc(&f->ops,  (uint32_t)(2 * W));
    cq_scratch_alloc(&f->work, (uint32_t)(2 * W - 1));

    /* `orr` BEFORE `diff`, WHICH IS THE OPPOSITE OF cmp.c's OWN LAYOUT, AND IT
     * IS MEASURED RATHER THAN TIDY. With `diff` first — one contiguous region,
     * `orr` immediately after it — `orr[-1]` IS `diff[0]`, so cq_eq_flag's
     * W == 1 branch is arithmetically inert and deleting it leaves every case
     * in this file green (battery mutant C, Release). The block's contract puts
     * no order on the two spans, so a consumer that lays them out any other way
     * needs that branch; this fixture is the only thing in the project that
     * exercises it. */
    pa        = cq_scratch_span(&f->ops,  0u,                (uint32_t)W);
    pb        = cq_scratch_span(&f->ops,  (uint32_t)W,       (uint32_t)W);
    f->k.orr  = cq_scratch_span(&f->work, 0u,                (uint32_t)(W - 1));
    f->k.diff = cq_scratch_span(&f->work, (uint32_t)(W - 1), (uint32_t)W);
    f->k.a    = pa;
    f->k.b    = pb;
    f->k.W    = W;

    eqb_mint(&f->ctx, pa, W, va);
    if (b_quantum) eqb_mint(&f->ctx, pb, W, vb);
    else           eqb_const(pb, W, vb);
    for (uint32_t i = 0; i < f->work.n; i++)
        cq_materialise(&f->ctx, &f->work.bits[i]);

    f->hd = cq_reg_alloc_zero(&f->ctx.regs, 1u);

    f->n_named = eqb_collect(&f->ops,  f->named, 0u);
    f->n_named = eqb_collect(&f->work, f->named, f->n_named);
}

/* The regions go back to nobody — cq_ctx_dispose's intended Rule-6 safe leak
 * (PRD §10) — and the cq_bit entries are reset first because M08's dispose
 * check is a KIND check and would otherwise, correctly, refuse a region of
 * live qubits. A case that wants the qubits BACK calls eqb_reclaim first. */
static void eqb_close(eqb_fx *f)
{
    for (uint32_t i = 0; i < f->ops.n;  i++) f->ops.bits[i]  = cq_bit_zero();
    for (uint32_t i = 0; i < f->work.n; i++) f->work.bits[i] = cq_bit_zero();
    cq_scratch_dispose(&f->ops);
    cq_scratch_dispose(&f->work);
    cq_ctx_dispose(&f->ctx);
}

static void eqb_forward(eqb_fx *f)
{
    for (int u = 0; u < cq_eq_steps(f->k.W); u++) cq_eq_step(&f->ctx, &f->k, u);
}

static void eqb_reverse(eqb_fx *f)
{
    for (int u = cq_eq_steps(f->k.W) - 1; u >= 0; u--)
        cq_eq_step(&f->ctx, &f->k, u);
}

/* THE SHADOW IS THE PROOF, DELIBERATELY. cq_sandwich releases its own region
 * under CQ_ZERO_BY_PALINDROME, which is a literal 1 (src/sandwich.c) and would
 * launder exactly the fault this fixture exists to see; the shadow is EXACT
 * rather than conservative on a rotation-free surface (poolcheck.h), so here it
 * is a genuine proof. Checked BEFORE releasing: cq_qubits_release aborts on an
 * index not proven |0>, and an abort reports as a crashed binary rather than as
 * a failed case. */
static void eqb_reclaim(eqb_fx *f, cq_scratch *scr, const char *what)
{
    int clean = 1;

    for (uint32_t i = 0; i < scr->n; i++) {
        if (!cq_bit_is_qubit(scr->bits[i])) continue;
        if (!cq_shadow_known_zero(&f->ctx.shadow, cq_bit_qindex(scr->bits[i]))) {
            cq_h_fail(__FILE__, __LINE__,
                      "eq block W=%d: %s bit %u (q%u) is not back at |0> after "
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
 * and a block's operands and scratch are owned by no register — so the union
 * form has to be written here. It is the same claim at the same strength: the
 * named indices are distinct by construction (I2, I3), so "every named index is
 * live" PLUS "the live count equals the named count" is set equality. A count
 * on its own is strictly weaker, which is the measured L2-masked-by-L3 finding,
 * and the pair is what closes it. */
static void eqb_live_is_exactly(eqb_fx *f, uint32_t n, const char *when)
{
    for (uint32_t i = 0; i < n; i++)
        if (cq_qubits_is_free(&f->ctx.pool, f->named[i]))
            cq_h_fail(__FILE__, __LINE__,
                      "L2 eq block W=%d %s: q%u is on the free list",
                      f->k.W, when, f->named[i]);

    if (cq_qubits_live(&f->ctx.pool) != n)
        cq_h_fail(__FILE__, __LINE__,
                  "L2 eq block W=%d %s: %u live, %u named — an index is live "
                  "that nothing owns", f->k.W, when,
                  cq_qubits_live(&f->ctx.pool), n);
}

/* ---- (a) The block is a palindrome, and it gives the pool back. ---------- */

/* DRIVEN THE WAY A CONSUMER DRIVES IT — a contiguous run of step indices mapped
 * onto [0, cq_eq_steps(W)) — and NOT through cq_sandwich, which would be M09's
 * suite over again and would release the region under its own literal-1 proof.
 *
 * TWO MASKS, AND THE ASYMMETRIC ONE IS THE POINT. `a` all Q with `b` all
 * CQ_BIT_ZERO is risk R8's named witness and is in no symmetric mask set; the
 * kernel's own palindrome case next door runs all-quantum on both operands, so
 * this is the only place eq's reverse half is replayed against a folding
 * operand. An ALL-CLASSICAL pair would not be a palindrome case at all but an
 * L5 one — the stream it asks about does not exist.
 *
 * AT THE ALL-QUANTUM MASK THE HEAD IS ALSO cq_eq_steps(W): one gate per step,
 * plan §0.4 obligation 1, which is what makes index reversal BE gate reversal.
 * At the asymmetric mask the head is MEASURED instead, because a CQ_BIT_ZERO
 * control folds its gate away and a step there legitimately costs 0.
 *
 * The operands are minted from |0>, so the raw flag is 0 and `dst` frees clean.
 * That costs nothing: the §3 fold table dispatches on a bit's KIND and never on
 * a qubit's value (D6), so the recorded stream is the one any value gives. The
 * value claim is case (c)'s. */
CQ_TEST(the_eq_block_is_a_palindrome_and_gives_the_pool_back)
{
    cq_mock m;

    cq_mock_init(&m);

    for (size_t j = 0; j < sizeof EQB_WS / sizeof EQB_WS[0]; j++)
        for (int bq = 1; bq >= 0; bq--) {
            int W = EQB_WS[j];
            cq_sink s = cq_mock_sink(&m);
            eqb_fx f;
            cq_pc_snap after;
            size_t head, mid;
            uint32_t held[2], n_held;

            eqb_open(&f, W, cq_ref_w_zero(), cq_ref_w_zero(), bq, &s);
            eqb_live_is_exactly(&f, f.n_named, "after the MINT");

            cq_mock_reset(&m);
            eqb_forward(&f);
            head = cq_mock_count(&m);
            if (bq) CHECK_EQ((long long)head, (long long)cq_eq_steps(W));

            /* Plan §0.4 obligation 2: the block allocates nothing. */
            eqb_live_is_exactly(&f, f.n_named, "after the FORWARD");

            cq_emit_cx(&f.ctx, cq_eq_flag(&f.k), cq_reg_bits(&f.ctx.regs, f.hd));
            mid = cq_mock_count(&m) - head;
            CHECK_EQ((long long)mid, 1);
            n_held = cq_pc_indices(&f.ctx, f.hd, held, 2u);
            CHECK_EQ((long long)n_held, 1);
            f.named[f.n_named] = held[0];

            eqb_reverse(&f);
            CHECK(cq_mock_is_palindrome(&m, head, mid));
            eqb_live_is_exactly(&f, f.n_named + 1u, "after the UNCOMPUTE");

            eqb_reclaim(&f, &f.work, "scratch");
            eqb_reclaim(&f, &f.ops,  "operands");
            if (!cq_pc_live_is_exactly(&f.ctx, &f.hd, 1u))
                cq_h_fail(__FILE__, __LINE__,
                          "L2 eq block W=%d: after the RECLAIM, only dst may "
                          "be live", W);

            /* L3 AGAINST THE PRISTINE POOL, WHICH IS THE ONE SNAPSHOT THAT CAN
             * RETURN. A snapshot taken after the mint cannot: `live` there
             * counts the fixture's own operands, and reclaiming them is the
             * thing being asserted. `minted`, `peak` and the free-list length
             * are never compared — all three are monotone (src/qubits.h).
             *
             * AND THE INDEX SET IS THE SHARP HALF: every index this fixture
             * ever minted — both regions and dst — is named and must be back on
             * the free list. A release of the WRONG index restores `live`
             * exactly and no count can see it. */
            cq_reg_free(&f.ctx, f.hd, cq_pc_zero_proof_rotation_free);
            after = cq_pc_take(&f.ctx);
            if (!cq_pc_same(f.pristine, after))
                cq_h_fail(__FILE__, __LINE__,
                          "L3 eq block W=%d: pool not restored — live %u -> %u",
                          W, f.pristine.live, after.live);
            if (!cq_pc_indices_are_free(&f.ctx, f.named, f.n_named + 1u))
                cq_h_fail(__FILE__, __LINE__, "L3 eq block W=%d: see above", W);

            eqb_close(&f);
        }

    cq_mock_dispose(&m);
}

/* ---- (b) The two kernels are the block twice plus their copy-out. -------- */

/* The block at the ALL-QUANTUM mask, measured through the counting sink. The
 * VALUES are irrelevant to a gate count — the fold table reads kind, never a
 * qubit's value (D6) — so a region of |0> qubits measures what a live one
 * would. */
static void eqb_block_cost(int W, cq_counter *out)
{
    cq_counter cnt;
    cq_sink s;
    eqb_fx f;

    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);
    eqb_open(&f, W, cq_ref_w_zero(), cq_ref_w_zero(), 1, &s);
    cq_count_reset(&cnt);                        /* drop the materialising Xs */
    eqb_forward(&f);
    *out = cnt;
    eqb_close(&f);
}

/* THE ASSERTION THAT SURVIVES `CQOPS_UPDATE_GOLDENS=1`, and the one M31/M36
 * will need in their own shape. `cq_kernel_eq` and `cq_kernel_ne` are
 * Bennett-in-the-small over this block — compute, copy out, compute backwards
 * (PRD §5) — so each kernel's tuple is exactly twice what the block just cost
 * plus its copy-out, and BOTH terms are MEASURED: the block by driving it, the
 * kernel by cq_kd_measure. Nothing here is a formula.
 *
 * The copy-out is 1 CX always, plus 1 X for `eq` and none for `ne`, and that
 * one gate is the whole of the two predicates' difference — Bennett's trailing
 * `NOT` folded into the copy-out (`arith.jl:445`, K09.md §5 delta 2). The raw
 * flag is `a != b`, so `ne` is the one that copies it out unchanged.
 *
 * Forward and `_unc` are measured and asserted SEPARATELY against the same
 * measured block. That is not the R6 mistake: the claim is not
 * `unc == forward` but that each pass, at the all-quantum mask cq_kd_measure
 * drives, is the block twice plus the copy-out. */
CQ_TEST(the_eq_kernels_are_the_block_twice_plus_their_copyout)
{
    for (size_t j = 0; j < sizeof EQB_WS / sizeof EQB_WS[0]; j++) {
        int W = EQB_WS[j];
        cq_counter blk;

        eqb_block_cost(W, &blk);
        CHECK_EQ(blk.ry + blk.rz + blk.mz, 0);

        for (int i = 0; i <= 1; i++) {          /* ROWS[0] = eq, ROWS[1] = ne */
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

/* ---- (c) The flag is `a != b`, which is the row a consumer gets wrong. --- */

/* BENNETT'S `lower_eq!` RETURNS THE NEGATION OF `eq`. Its trailing NOT is the
 * copy-out's, folded there by K09.md §5 delta 2, so the wire the block leaves
 * behind holds `a != b` and it is `ne` that copies it out unchanged. An fp
 * consumer that reads cq_eq_flag as "equal" builds a correct-looking circuit
 * computing the complement, and NOTHING STRUCTURAL CAN SEE IT: same gates,
 * same count, same palindrome, clean scratch. That is K9's uge-meaning-ule one
 * layer down, and this case is the only instrument for it on the block.
 *
 * THE ORACLE IS cq_ref_w_eq ON THE DRAWN VALUES — the comparison itself, not
 * cq_ref_icmp's transcription of `lower_icmp!` and certainly not cmp.c's own
 * const_raw, so it shares nothing with what it checks.
 *
 * cq_kd_samples() draws per width, never a product, and THE EQUAL PAIR IS
 * FORCED INSIDE THE BUDGET: a uniform draw at W = 64 never produces a == b, so
 * without the anchor the flag would be exercised on one side only and a block
 * that returned a constant 1 would pass at every width above 1. */
CQ_TEST(the_eq_blocks_flag_is_a_not_equal_b)
{
    /* A sink is REQUIRED: cq_ctx_init(NULL) takes cq_sink_active(), and an
     * unresolvable default is a hard error rather than a quiet substitution
     * (M04) — measured here, not assumed. The counter is the cheapest one that
     * exists; nothing in this case reads it. */
    cq_counter cnt;
    cq_sink s;

    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);

    for (size_t j = 0; j < sizeof EQB_WS / sizeof EQB_WS[0]; j++) {
        int W = EQB_WS[j];
        cq_bk_rng rng;
        int n = cq_kd_samples();

        cq_bk_rng_init(&rng, 0x9E3779B97F4A7C15u ^ (uint64_t)W);

        for (int i = 0; i < n; i++) {
            eqb_fx f;
            cq_shadow sv;
            cq_ref_w va = cq_ref_w_make(cq_bk_rng_next(&rng),
                                        cq_bk_rng_next(&rng), W);
            cq_ref_w vb = (i == 0) ? va
                                   : cq_ref_w_make(cq_bk_rng_next(&rng),
                                                   cq_bk_rng_next(&rng), W);
            int want;

            /* FIVE ANCHORS INSIDE THE BUDGET, the way L1's corners are, and the
             * last two were put there by a measurement rather than by taste.
             * The equal pair (i == 0, above) and the two extremes are the
             * obvious ones; a pair differing in exactly ONE lane is what the
             * OR-prefix can lose. Measured (battery mutant E, Release): with
             * `c2` reading `diff[m]` instead of `diff[m+1]` the prefix drops the
             * TOP lane entirely, and none of the first three anchors — nor any
             * uniform draw at W >= 8 — can see it, because they differ in every
             * lane or in none. The MSB row is the one that does. */
            if (i == 1) { va = cq_ref_w_zero();  vb = cq_ref_w_ones(W); }
            if (i == 2) { va = cq_ref_w_ones(W); vb = cq_ref_w_ones(W); }
            if (i == 3) { va = cq_ref_w_zero();  vb = cq_ref_w_setbit(W - 1); }
            if (i == 4) { va = cq_ref_w_zero();  vb = cq_ref_w_setbit(0); }
            want = !cq_ref_w_eq(va, vb);

            eqb_open(&f, W, va, vb, 1, &s);
            eqb_forward(&f);

            sv = cq_shadow_get(&f.ctx.shadow, cq_bit_qindex(*cq_eq_flag(&f.k)));
            if (sv.unknown || (int)sv.value != want)
                cq_h_fail(__FILE__, __LINE__,
                          "eq block W=%d draw %d: flag = %s%u, want a != b = %d "
                          "(a = 0x%llx%016llx, b = 0x%llx%016llx)", W, i,
                          sv.unknown ? "unknown/" : "", sv.value, want,
                          (unsigned long long)va.hi, (unsigned long long)va.lo,
                          (unsigned long long)vb.hi, (unsigned long long)vb.lo);

            /* Ancilla-clean with LIVE operands, which the palindrome case (at
             * |0> operands) cannot claim: the reverse half has to cancel
             * against whatever `a` and `b` actually hold. */
            eqb_reverse(&f);
            eqb_reclaim(&f, &f.work, "scratch");
            eqb_close(&f);
        }
    }
}
