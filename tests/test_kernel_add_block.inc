/* tests/test_kernel_add_block.inc — M14's K6 compute half AS AN EXPORTED STEP
 * BLOCK (bd 9ve.30). Included by tests/test_kernel_add.c.
 *
 * THE SUBJECT IS THE EXPORT, NOT THE ADDER. Whether `add` computes the right
 * value is the sweep's job next door, against refmodel.c's two-word reference.
 * What is new here is that a CONSUMER can drive `lower_add!`'s compute half
 * itself: PRD-v2 §5's M32 (`fpround`), M33 (`fadd`) and M39 (`fma`) rows
 * compose it at W = 64 — twelve occurrences in `soft_fma` alone — and a
 * consumer cannot call `cq_kernel_add` instead, because it is a whole sandwich
 * and `cq_sandwich` refuses nesting in both configurations. The block is the
 * only door and these five cases are what say it is a real one.
 *
 * THE SUBSTITUTE THE CONSUMER WILL REACH FOR IS K8, AND NOTHING IN THE SWEEP
 * NEXT DOOR CAN SEE IT. `cq_addacc_step` is in-place: right forward value,
 * wrong `_unc`, a silent miscompile rather than a test failure (CLAUDE.md
 * Rule 7's K8 paragraph). The case that discriminates is (a) — the block is
 * driven forward and then REPLAYED IN REVERSE over the same indices, which is
 * exactly what an fp sandwich does and exactly what an accumulator fails.
 *
 * EVERY CASE HERE READS NO GOLDEN, which is the K11 finding applied in
 * advance: `CQOPS_UPDATE_GOLDENS=1` is the documented way to make a red L4
 * green, so a golden blesses a mutant as readily as a fix. What is asserted
 * below is the block against what the MODULE says it costs, and the module
 * against a re-derivation of `adder.jl`'s own loop written in this file.
 *
 * SCRATCH SPANS, NOT REGISTERS, for the reason the `cq_ult_block` callout
 * records: inside a sandwich a block's operands are pre-materialised scratch,
 * not rails, so a fixture built out of registers would exercise a calling shape
 * no consumer uses.
 */

/* K6's shipped ladder (third_party/cq_lang/opcode_table.yaml:184). 64 is the
 * width the fp port consumes; 1 is where `lower_add!`'s loop is empty and the
 * three-CNOT tail is the whole construction; 128 is the top shipped width and
 * the only one above the reference's 64-bit word seam. */
static const int ADDB_WS[]  = { 1, 8, 16, 32, 64, 128 };
/* The value case adds two narrow widths where the carry chain has structure
 * but is short — a dropped stage is one lane out of two or three there. */
static const int ADDB_VWS[] = { 1, 2, 3, 8, 64, 128 };
enum { ADDB_W_MAX = 128, ADDB_NAMED_MAX = 5 * ADDB_W_MAX };

/* THE INDEPENDENT RE-DERIVATION, AND IT IS THE STEP-COUNT DETECTOR.
 * `bd 9ve.2` measured that a composition identity whose two sides both ask
 * `cq_*_steps(W)` is BLIND to that function moving — eight cases red, the
 * composition case green. So the step count is counted here a second time,
 * from `lower_add!`'s loop structure rather than from the closed form: three
 * unguarded CNOT pushes per iteration (adder.jl:9, :10, :15) and two Toffolis
 * guarded by `if i < W` (adder.jl:11-13). Writing `5 * W - 2` instead would
 * make this a restatement of the module rather than a check on it. */
static int addb_n_compute_of(int W)
{
    int n = 0;

    for (int i = 0; i < W; i++) {          /* adder.jl:8, `for i in 1:W`     */
        n += 3;                            /* :9, :10, :15 — three CNOTs     */
        if (i < W - 1) n += 2;             /* :11-:13 — guarded `if i < W`   */
    }
    return n;
}

/* ONE FIXTURE, TWO REGIONS, and the split is the block's own contract: `ops`
 * holds what the caller supplies as CONTROLS (`a`, `b`) and `work` holds what
 * the block writes (`t`, `c`). Only `work` has to come back to |0>; `a` and `b`
 * carry live values and are nobody's ancilla. Two regions are legal because no
 * sandwich is open — I6(a)'s extent is one contiguous range and is armed by
 * cq_sandwich, which case (a) deliberately does not use. */
typedef struct {
    cq_ctx       ctx;
    cq_scratch   ops, work;
    cq_add_block k;
    int32_t      hd;                 /* the W-bit dst a copy-out targets */
    uint32_t     named[ADDB_NAMED_MAX];
    uint32_t     n_named;
    cq_pc_snap   pristine;           /* the pool before anything was minted */
} addb_fx;

/* A span holding `value`, built the one sanctioned way (Rule 5): set the
 * constant, then cq_materialise, which emits the X for a set bit. Those Xs are
 * recorded, so every caller resets its sink afterwards. */
static void addb_mint(cq_ctx *ctx, cq_bit *v, int n, cq_ref_w value)
{
    for (int i = 0; i < n; i++) {
        v[i] = cq_bit_const(cq_ref_w_bit(value, i));
        cq_materialise(ctx, &v[i]);
    }
}

static void addb_const(cq_bit *v, int n, cq_ref_w value)
{
    for (int i = 0; i < n; i++) v[i] = cq_bit_const(cq_ref_w_bit(value, i));
}

static uint32_t addb_collect(const cq_scratch *scr, uint32_t *out, uint32_t n)
{
    for (uint32_t i = 0; i < scr->n; i++)
        if (cq_bit_is_qubit(scr->bits[i])) out[n++] = cq_bit_qindex(scr->bits[i]);
    return n;
}

/* `aq` / `bq` are the MASK PAIR, one bit per operand — risk R8's asymmetric
 * witness is inexpressible with a single mask. The work region is materialised
 * whole, which is I6(b) and the one thing that makes a step emit the same gate
 * forwards and backwards. */
static void addb_open(addb_fx *f, int W, cq_ref_w va, int aq, cq_ref_w vb,
                      int bq, const cq_sink *sink)
{
    cq_bit *pa, *pb;

    cq_ctx_init(&f->ctx, sink);
    f->pristine = cq_pc_take(&f->ctx);   /* taken BEFORE the mint — see (a) */

    cq_scratch_alloc(&f->ops,  (uint32_t)(2 * W));
    cq_scratch_alloc(&f->work, (uint32_t)(2 * W));

    /* `c` BEFORE `t`, WHICH IS THE OPPOSITE OF add.c's OWN LAYOUT (`t ++ c`).
     * The block imposes no order on the two spans, and a fixture that copies
     * the library's silently retires every fault only a different order can
     * reach — measured on M16's eq block, where `orr[-1]` IS `diff[0]` under
     * cmp.c's layout and the W == 1 branch is arithmetically inert. Here the
     * live fault of that shape is an accessor reaching `c[i+1]` at i = W-1 or
     * `t` through a `c`-relative offset: under `t ++ c` those land inside the
     * region and stay self-consistent, under `c ++ t` they cross a span the
     * reverse half must return to |0>. */
    pa       = cq_scratch_span(&f->ops,  0u,          (uint32_t)W);
    pb       = cq_scratch_span(&f->ops,  (uint32_t)W, (uint32_t)W);
    f->k.c   = cq_scratch_span(&f->work, 0u,          (uint32_t)W);
    f->k.t   = cq_scratch_span(&f->work, (uint32_t)W, (uint32_t)W);
    f->k.a   = pa;
    f->k.b   = pb;
    f->k.W   = W;

    if (aq) addb_mint(&f->ctx, pa, W, va); else addb_const(pa, W, va);
    if (bq) addb_mint(&f->ctx, pb, W, vb); else addb_const(pb, W, vb);
    for (uint32_t i = 0; i < f->work.n; i++)
        cq_materialise(&f->ctx, &f->work.bits[i]);

    f->hd = cq_reg_alloc_zero(&f->ctx.regs, (uint32_t)W);

    f->n_named = addb_collect(&f->ops,  f->named, 0u);
    f->n_named = addb_collect(&f->work, f->named, f->n_named);
}

/* The regions go back to nobody — cq_ctx_dispose's intended Rule-6 safe leak
 * (PRD §10) — and the cq_bit entries are reset first because M08's dispose
 * check is a KIND check and would otherwise, correctly, refuse a region of live
 * qubits. A case that wants the qubits BACK calls addb_reclaim first. */
static void addb_close(addb_fx *f)
{
    for (uint32_t i = 0; i < f->ops.n;  i++) f->ops.bits[i]  = cq_bit_zero();
    for (uint32_t i = 0; i < f->work.n; i++) f->work.bits[i] = cq_bit_zero();
    cq_scratch_dispose(&f->ops);
    cq_scratch_dispose(&f->work);
    cq_ctx_dispose(&f->ctx);
}

static void addb_forward(cq_ctx *ctx, const cq_add_block *k)
{
    for (int u = 0; u < cq_add_steps(k->W); u++) cq_add_step(ctx, k, u);
}

static void addb_reverse(cq_ctx *ctx, const cq_add_block *k)
{
    for (int u = cq_add_steps(k->W) - 1; u >= 0; u--) cq_add_step(ctx, k, u);
}

/* THE SHADOW IS THE PROOF, DELIBERATELY. cq_sandwich releases its own region
 * under CQ_ZERO_BY_PALINDROME, a literal 1 (src/sandwich.c), which would
 * launder exactly the fault this fixture exists to see; the shadow is EXACT
 * rather than conservative on a rotation-free surface (poolcheck.h), so here it
 * is a genuine proof. Checked BEFORE releasing: cq_qubits_release aborts on an
 * index not proven |0>, and an abort reports as a crashed binary rather than as
 * a failed case. */
static void addb_reclaim(addb_fx *f, cq_scratch *scr, const char *what)
{
    int clean = 1;

    for (uint32_t i = 0; i < scr->n; i++) {
        if (!cq_bit_is_qubit(scr->bits[i])) continue;
        if (!cq_shadow_known_zero(&f->ctx.shadow, cq_bit_qindex(scr->bits[i]))) {
            cq_h_fail(__FILE__, __LINE__,
                      "add block W=%d: %s bit %u (q%u) is not back at |0> after "
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

/* THE SHADOW IS THE READER, and every lane is determinate: the surface is
 * rotation-free, so cq_shadow_known_zero and cq_shadow_get are EXACT rather
 * than conservative (poolcheck.h). `ok` carries the unknown row out so a
 * poisoned lane fails loudly instead of reading as 0. */
static cq_ref_w addb_read(const cq_ctx *ctx, const cq_bit *v, int W, int *ok)
{
    uint64_t lo = 0u, hi = 0u;

    *ok = 1;
    for (int i = 0; i < W; i++) {
        cq_shadow sv = cq_shadow_get(&ctx->shadow, cq_bit_qindex(v[i]));

        if (sv.unknown) { *ok = 0; continue; }
        if (sv.value) {
            if (i < 64) lo |= (uint64_t)1 << i;
            else        hi |= (uint64_t)1 << (i - 64);
        }
    }
    return cq_ref_w_make(lo, hi, W);
}

/* The block at the ALL-QUANTUM mask, measured through the counting sink. The
 * VALUES are irrelevant to a gate count — the fold table reads kind, never a
 * qubit's value (D6) — so a region of |0> qubits measures what a live one
 * would. */
static void addb_block_cost(int W, cq_counter *out)
{
    cq_counter cnt;
    cq_sink s;
    addb_fx f;

    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);
    addb_open(&f, W, cq_ref_w_zero(), 1, cq_ref_w_zero(), 1, &s);
    cq_count_reset(&cnt);                        /* drop the materialising Xs */
    addb_forward(&f.ctx, &f.k);
    *out = cnt;
    addb_close(&f);
}

/* L2 AS A SET, SPELLED OVER RAW INDICES. cq_pc_live_is_exactly takes HANDLES,
 * and a block's operands and scratch are owned by no register — so the union
 * form has to be written here. It is the same claim at the same strength: the
 * named indices are distinct by construction (I2, I3), so "every named index is
 * live" PLUS "the live count equals the named count" is set equality. A count
 * on its own is strictly weaker, which is the measured L2-masked-by-L3 finding,
 * and the pair is what closes it. */
static void addb_live_is_exactly(addb_fx *f, uint32_t n, const char *when)
{
    for (uint32_t i = 0; i < n; i++)
        if (cq_qubits_is_free(&f->ctx.pool, f->named[i]))
            cq_h_fail(__FILE__, __LINE__,
                      "L2 add block W=%d %s: q%u is on the free list",
                      f->k.W, when, f->named[i]);

    if (cq_qubits_live(&f->ctx.pool) != n)
        cq_h_fail(__FILE__, __LINE__,
                  "L2 add block W=%d %s: %u live, %u named — an index is live "
                  "that nothing owns", f->k.W, when,
                  cq_qubits_live(&f->ctx.pool), n);
}

/* ---- (a) The block is a palindrome, and it gives the pool back. ---------- */

/* DRIVEN THE WAY A CONSUMER DRIVES IT — a contiguous run of step indices mapped
 * onto [0, cq_add_steps(W)) — and NOT through cq_sandwich, which would be M09's
 * suite over again and would release the region under its own literal-1 proof.
 *
 * THREE MASK PAIRS, AND THE TWO ASYMMETRIC ONES ARE THE POINT. `a` all Q with
 * `b` all CQ_BIT_ZERO is risk R8's named witness and is in no symmetric mask
 * set; its MIRROR is a different gate list at the same length, because the
 * folds fall on j=0/j=2 rather than j=1/j=2. The kernel's own palindrome case
 * next door runs all-quantum on both operands, so this is the only place K6's
 * reverse half is replayed against a folding operand. An ALL-CLASSICAL pair
 * would not be a palindrome case at all but an L5 one — the stream it asks
 * about does not exist.
 *
 * AT THE ALL-QUANTUM MASK THE HEAD IS ALSO cq_add_steps(W): one gate per step,
 * plan §0.4 obligation 1, which is what makes index reversal BE gate reversal.
 * At a folding mask the head is MEASURED, and asserted STRICTLY SHORTER —
 * a classical ZERO operand bit removes gates where a classical ONE would not
 * (CLAUDE.md), so `<` is assertable here and would be wrong against ones. */
CQ_TEST(the_add_block_is_a_palindrome_and_gives_the_pool_back)
{
    static const int AQ[] = { 1, 1, 0 };
    static const int BQ[] = { 1, 0, 1 };
    cq_mock m;

    cq_mock_init(&m);

    for (size_t j = 0; j < sizeof ADDB_WS / sizeof ADDB_WS[0]; j++)
        for (size_t p = 0; p < sizeof AQ / sizeof AQ[0]; p++) {
            int W = ADDB_WS[j];
            cq_sink s = cq_mock_sink(&m);
            addb_fx f;
            cq_pc_snap after;
            size_t head, mid;
            uint32_t held[ADDB_W_MAX];
            uint32_t n_held;

            addb_open(&f, W, cq_ref_w_zero(), AQ[p], cq_ref_w_zero(), BQ[p], &s);
            addb_live_is_exactly(&f, f.n_named, "after the MINT");

            cq_mock_reset(&m);
            addb_forward(&f.ctx, &f.k);
            head = cq_mock_count(&m);

            if (AQ[p] && BQ[p]) CHECK_EQ((long long)head,
                                         (long long)cq_add_steps(W));
            else {
                CHECK(head > 0u);
                CHECK(head < (size_t)cq_add_steps(W));
            }

            /* Plan §0.4 obligation 2: the block allocates nothing. */
            addb_live_is_exactly(&f, f.n_named, "after the FORWARD");

            /* The consumer's own "^=", W CX off the block's output span. */
            for (int i = 0; i < W; i++)
                cq_emit_cx(&f.ctx, &f.k.t[i],
                           &cq_reg_bits(&f.ctx.regs, f.hd)[i]);
            mid = cq_mock_count(&m) - head;
            CHECK_EQ((long long)mid, (long long)W);

            n_held = cq_pc_indices(&f.ctx, f.hd, held, (uint32_t)ADDB_W_MAX);
            CHECK_EQ((long long)n_held, (long long)W);
            for (uint32_t i = 0; i < n_held; i++)
                f.named[f.n_named + i] = held[i];

            addb_reverse(&f.ctx, &f.k);
            CHECK(cq_mock_is_palindrome(&m, head, mid));
            addb_live_is_exactly(&f, f.n_named + n_held, "after the UNCOMPUTE");

            addb_reclaim(&f, &f.work, "scratch");
            addb_reclaim(&f, &f.ops,  "operands");
            if (!cq_pc_live_is_exactly(&f.ctx, &f.hd, 1u))
                cq_h_fail(__FILE__, __LINE__,
                          "L2 add block W=%d: after the RECLAIM, only dst may "
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
                          "L3 add block W=%d: pool not restored — live %u -> %u",
                          W, f.pristine.live, after.live);
            if (!cq_pc_indices_are_free(&f.ctx, f.named, f.n_named + n_held))
                cq_h_fail(__FILE__, __LINE__, "L3 add block W=%d: see above", W);

            addb_close(&f);
        }

    cq_mock_dispose(&m);
}

/* ---- (b) The step count, asked of the module and re-derived here. -------- */

/* THE COMPOSITION IDENTITY BELOW CANNOT SEE `cq_add_steps` MOVE, because both
 * of its sides ask it (bd 9ve.2, measured on M12: eight cases red, the
 * composition case green). This is the detector for that, and the
 * re-derivation it compares against is `addb_n_compute_of` at the top of this
 * file — the Julia loop counted a second time, not the closed form copied.
 *
 * W = 1 IS PINNED AS A TUPLE, NOT AS A TOTAL, and that is the one width where
 * a reader's reflex is wrong in the neighbouring module: K8's `6W - 5` is
 * accidentally right at W = 1 as a TOTAL while its per-type split gives a
 * NEGATIVE Toffoli count (K08.md §5 D1). K6 has no such hole — the loop is
 * empty and the three-CNOT tail is the whole construction — so `(0, 3, 0)` is
 * asserted by MEASUREMENT, which is what says the block is non-degenerate at
 * the bottom of the ladder rather than merely consistent there. */
CQ_TEST(the_add_blocks_step_count_is_bennetts_loop_counted_twice)
{
    static const int WS[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64, 80, 128 };
    cq_counter blk;

    for (size_t j = 0; j < sizeof WS / sizeof WS[0]; j++)
        CHECK_EQ(cq_add_steps(WS[j]), addb_n_compute_of(WS[j]));

    addb_block_cost(1, &blk);
    CHECK_GATES(blk.x, blk.cx, blk.ccx, 0, 3, 0);
    CHECK_EQ(cq_add_steps(1), 3);
}

/* ---- (c) The kernel is the block twice plus its copy-out. ---------------- */

/* THE ASSERTION THAT SURVIVES `CQOPS_UPDATE_GOLDENS=1`. `cq_kernel_add` is
 * Bennett-in-the-small over this block — compute, copy out, compute backwards
 * (PRD §5) — so its tuple is exactly twice what the block just cost plus a
 * W-CX copy-out, and BOTH terms are MEASURED: the block by driving it, the
 * kernel by cq_kd_measure. Nothing here is a formula, and nothing here reads
 * tests/goldens/add.counts.
 *
 * What it catches is the kernel drifting from its block — a value-neutral gate
 * on either side reddens it. What it CANNOT catch is cq_add_steps itself being
 * one short, because both sides ask it; case (b) above is that detector.
 *
 * Forward and `_unc` are measured and asserted SEPARATELY against the same
 * measured block. That is not the R6 mistake: the claim is not
 * `unc == forward` but that each pass, at the all-quantum mask cq_kd_measure
 * drives, is the block twice plus the copy-out. */
CQ_TEST(the_add_kernel_is_the_block_twice_plus_its_copyout)
{
    for (size_t j = 0; j < sizeof ADDB_WS / sizeof ADDB_WS[0]; j++) {
        int W = ADDB_WS[j];
        cq_counter blk, fwd, unc;

        addb_block_cost(W, &blk);
        CHECK_EQ(blk.ry + blk.rz + blk.mz, 0);
        CHECK(blk.cx + blk.ccx > 0u);           /* never vacuously equal */

        cq_kd_measure(&ADD, W, &fwd, &unc);
        CHECK_GATES(fwd.x, fwd.cx, fwd.ccx,
                    2u * blk.x, 2u * blk.cx + (uint64_t)W, 2u * blk.ccx);
        CHECK_GATES(unc.x, unc.cx, unc.ccx,
                    2u * blk.x, 2u * blk.cx + (uint64_t)W, 2u * blk.ccx);
    }
}

/* ---- (d) Two blocks in one region do not collide. ----------------------- */

/* MANDATORY FOR EVERY `cq_*_block` EXPORT (bd 9ve.3, measured on M18): a lone
 * block in a region of its own is self-consistent under faults that move the
 * whole block together, and only a SECOND block beside it can see them. For a
 * pointer-carved block like this one the fault is a step reaching outside its
 * named spans — `c[i+1]` at the top stage, or a `t`-relative walk off the end —
 * which under one block lands in the same region and stays green.
 *
 * TWO DIFFERENT VALUE PAIRS, so a block reading the other's operands is a wrong
 * ANSWER rather than a coincidence, and the two sums are checked independently.
 * The region is `c0 ++ t0 ++ c1 ++ t1`, so block 1's carry chain sits
 * immediately above block 0's output: a write past `t0` lands in `c1` and a
 * write below `c1` lands in `t0`, in both directions. */
static void addb_two_blocks(int W, cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_scratch ops, work;
    cq_add_block k0, k1;
    cq_ref_w v[4], want0, want1;
    size_t head;
    int ok0, ok1;

    v[0] = cq_ref_w_make(0x0BADF00DCAFE5A5Aull, 0x1234ull, W);
    v[1] = cq_ref_w_make(0x00C0FFEE3C3C7777ull, 0xF0F0ull, W);
    v[2] = cq_ref_w_make(0x0123456789ABCDEFull, 0x5A5Aull, W);
    v[3] = cq_ref_w_make(0x0FEDCBA987654321ull, 0x0F0Full, W);
    want0 = cq_ref_w_add(v[0], v[1], W);
    want1 = cq_ref_w_add(v[2], v[3], W);

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);
    cq_scratch_alloc(&ops,  (uint32_t)(4 * W));
    cq_scratch_alloc(&work, (uint32_t)(4 * W));

    for (int i = 0; i < 4; i++)
        addb_mint(&ctx, cq_scratch_span(&ops, (uint32_t)(i * W), (uint32_t)W),
                  W, v[i]);
    for (uint32_t i = 0; i < work.n; i++) cq_materialise(&ctx, &work.bits[i]);

    k0.a = cq_scratch_span(&ops, 0u,               (uint32_t)W);
    k0.b = cq_scratch_span(&ops, (uint32_t)W,      (uint32_t)W);
    k0.c = cq_scratch_span(&work, 0u,              (uint32_t)W);
    k0.t = cq_scratch_span(&work, (uint32_t)W,     (uint32_t)W);
    k0.W = W;

    k1.a = cq_scratch_span(&ops, (uint32_t)(2 * W), (uint32_t)W);
    k1.b = cq_scratch_span(&ops, (uint32_t)(3 * W), (uint32_t)W);
    k1.c = cq_scratch_span(&work, (uint32_t)(2 * W), (uint32_t)W);
    k1.t = cq_scratch_span(&work, (uint32_t)(3 * W), (uint32_t)W);
    k1.W = W;

    cq_mock_reset(m);
    addb_forward(&ctx, &k0);
    addb_forward(&ctx, &k1);
    head = cq_mock_count(m);
    CHECK_EQ((long long)head, (long long)(2 * cq_add_steps(W)));

    CHECK(cq_ref_w_eq(addb_read(&ctx, k0.t, W, &ok0), want0));
    CHECK(cq_ref_w_eq(addb_read(&ctx, k1.t, W, &ok1), want1));
    CHECK(ok0 && ok1);

    /* Both reversed, innermost first — a consumer's own step space. Every bit
     * of BOTH work spans must come back, which is what a collision breaks. */
    addb_reverse(&ctx, &k1);
    addb_reverse(&ctx, &k0);
    CHECK(cq_mock_is_palindrome(m, head, 0u));

    for (uint32_t i = 0; i < work.n; i++)
        if (!cq_shadow_known_zero(&ctx.shadow, cq_bit_qindex(work.bits[i])))
            cq_h_fail(__FILE__, __LINE__,
                      "two add blocks W=%d: work bit %u (q%u) is not back at "
                      "|0>", W, i, cq_bit_qindex(work.bits[i]));

    for (uint32_t i = 0; i < ops.n;  i++) ops.bits[i]  = cq_bit_zero();
    for (uint32_t i = 0; i < work.n; i++) work.bits[i] = cq_bit_zero();
    cq_scratch_dispose(&ops);
    cq_scratch_dispose(&work);
    cq_ctx_dispose(&ctx);
}

CQ_TEST(the_two_blocks_in_one_region_do_not_collide)
{
    static const int WS[] = { 1, 2, 3, 5, 8, 32, 64 };
    cq_mock m;

    cq_mock_init(&m);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++)
        addb_two_blocks(WS[i], &m);

    cq_mock_dispose(&m);
}

/* ---- (e) The block's output span really is a + b. ----------------------- */

/* THE ORACLE IS refmodel.c's TWO-WORD `cq_ref_w_add`, which shares nothing with
 * `ripple` — it is a word-at-a-time carry, cross-checked in its own right by
 * `l1s_oracle_agrees_with_an_independent_bit_serial_model` at the foot of this
 * suite. Reading the block's own recurrence back as the reference would make
 * this case a restatement.
 *
 * FOUR ANCHORS INSIDE cq_kd_samples()' BUDGET, the way L1's corners are, and
 * the first two are what a dropped or mis-indexed stage loses:
 *
 *   i == 0   ones(W) + 1 — the FULL carry propagation, every stage feeding the
 *            next, landing on 0. A ripple broken at any single lane gives a
 *            non-zero answer here and nowhere else in a uniform draw.
 *   i == 1   (ones(W) without its top lane) + 1 — one carry ripples the whole
 *            way and STOPS in the top lane, so the answer is `setbit(W-1)`.
 *            That is the top-stage anchor: the `i < W` guard's last CNOT has
 *            to run, and a loop bound one short drops exactly this bit.
 *   i == 2   setbit(W-1) + setbit(W-1) — the top lane wrapping with no
 *            carry-out, which is the block's declared contract.
 *   i == 3   0 + 0, the identity row, where every lane must stay |0>.
 *
 * At W == 1 the first two collapse onto 1+1 and 0+1; both still hold and both
 * still exercise the three-CNOT tail, which is the whole construction there. */
CQ_TEST(the_add_blocks_output_span_is_a_plus_b)
{
    cq_counter cnt;
    cq_sink s;

    /* A sink is REQUIRED: cq_ctx_init(NULL) takes cq_sink_active(), which is a
     * hard error rather than a quiet substitution (M04). Nothing reads it. */
    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);

    for (size_t j = 0; j < sizeof ADDB_VWS / sizeof ADDB_VWS[0]; j++) {
        int W = ADDB_VWS[j];
        cq_bk_rng rng;
        int n = cq_kd_samples();

        cq_bk_rng_init(&rng, 0xA24BAED4963EE407ull ^ (uint64_t)W);

        for (int i = 0; i < n; i++) {
            addb_fx f;
            cq_ref_w va = cq_ref_w_make(cq_bk_rng_next(&rng),
                                        cq_bk_rng_next(&rng), W);
            cq_ref_w vb = cq_ref_w_make(cq_bk_rng_next(&rng),
                                        cq_bk_rng_next(&rng), W);
            cq_ref_w got, want;
            int ok;

            if (i == 0) { va = cq_ref_w_ones(W);
                          vb = cq_ref_w_setbit(0); }
            if (i == 1) { va = cq_ref_w_andnot(cq_ref_w_ones(W),
                                               cq_ref_w_setbit(W - 1));
                          vb = cq_ref_w_setbit(0); }
            if (i == 2) { va = cq_ref_w_setbit(W - 1);
                          vb = cq_ref_w_setbit(W - 1); }
            if (i == 3) { va = cq_ref_w_zero(); vb = cq_ref_w_zero(); }
            want = cq_ref_w_add(va, vb, W);

            addb_open(&f, W, va, 1, vb, 1, &s);
            addb_forward(&f.ctx, &f.k);
            got = addb_read(&f.ctx, f.k.t, W, &ok);

            if (!ok || !cq_ref_w_eq(got, want))
                cq_h_fail(__FILE__, __LINE__,
                          "add block W=%d draw %d: t = %s0x%llx%016llx, want "
                          "0x%llx%016llx (a = 0x%llx%016llx, b = 0x%llx%016llx)",
                          W, i, ok ? "" : "unknown/",
                          (unsigned long long)got.hi,  (unsigned long long)got.lo,
                          (unsigned long long)want.hi, (unsigned long long)want.lo,
                          (unsigned long long)va.hi,   (unsigned long long)va.lo,
                          (unsigned long long)vb.hi,   (unsigned long long)vb.lo);

            /* Ancilla-clean with LIVE operands, which case (a) at |0> operands
             * cannot claim: the reverse half has to cancel against whatever `a`
             * and `b` actually hold. */
            addb_reverse(&f.ctx, &f.k);
            addb_reclaim(&f, &f.work, "scratch");
            addb_close(&f);
        }
    }
}

/* ---- (f) The consumer's real shape: two blocks inside ONE sandwich. ------ */

/* WHAT NO OTHER CASE IN THIS FILE REACHES. (a), (d) and (e) drive the block by
 * hand, which is deliberate — a hand drive is the only way to replay the
 * reverse half against a shadow proof rather than under `cq_sandwich`'s literal
 * `CQ_ZERO_BY_PALINDROME`. But a hand drive arms nothing: I6(a)'s scratch
 * extent is armed by `cq_sandwich` and by nothing else, so until this case the
 * only thing exercising the block under an armed extent was `cq_kernel_add`,
 * at ONE block and at add.c's own span order.
 *
 * THIS IS THE SHAPE M32 / M33 / M39 WILL WRITE: many blocks, one flat step
 * space, one region, one sandwich — `soft_fma` composes twelve `add` blocks
 * among others (K20.md's block table). The two blocks are laid out
 * `c0 ++ t0 ++ c1 ++ t1`, so block 1's carry chain sits immediately above block
 * 0's output and a write past `t0` lands in `c1` — the collision (d) tests, now
 * with I6(a), I6(b) and M09's region fingerprint all live.
 *
 * The operands live in their OWN region, pre-materialised before the sandwich
 * opens, because they are controls and must not be inside the extent the driver
 * arms; `dst` is two W-bit rails, which is what the copy-out writes. */
typedef struct {
    cq_bit      *dst0, *dst1;
    cq_add_block k0, k1;
    int          W, steps;
} addb2_env;

static void addb2_compute(cq_ctx *ctx, void *env, int s)
{
    const addb2_env *e = (const addb2_env *)env;

    if (s < e->steps) cq_add_step(ctx, &e->k0, s);
    else              cq_add_step(ctx, &e->k1, s - e->steps);
}

static void addb2_copyout(cq_ctx *ctx, void *env, int i)
{
    const addb2_env *e = (const addb2_env *)env;

    if (i < e->W) cq_emit_cx(ctx, &e->k0.t[i],        &e->dst0[i]);
    else          cq_emit_cx(ctx, &e->k1.t[i - e->W], &e->dst1[i - e->W]);
}

static void addb_two_blocks_sandwiched(int W, cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_scratch ops, work;
    addb2_env e;
    int32_t hd[2];
    cq_pc_snap before;
    cq_ref_w v[4];

    v[0] = cq_ref_w_make(0x0BADF00DCAFE5A5Aull, 0x1234ull, W);
    v[1] = cq_ref_w_make(0x00C0FFEE3C3C7777ull, 0xF0F0ull, W);
    v[2] = cq_ref_w_make(0x0123456789ABCDEFull, 0x5A5Aull, W);
    v[3] = cq_ref_w_make(0x0FEDCBA987654321ull, 0x0F0Full, W);

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);

    cq_scratch_alloc(&ops, (uint32_t)(4 * W));
    for (int i = 0; i < 4; i++)
        addb_mint(&ctx, cq_scratch_span(&ops, (uint32_t)(i * W), (uint32_t)W),
                  W, v[i]);
    hd[0] = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);
    hd[1] = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);
    before = cq_pc_take(&ctx);

    cq_scratch_alloc(&work, (uint32_t)(4 * W));       /* c0 ++ t0 ++ c1 ++ t1 */

    e.W     = W;
    e.steps = cq_add_steps(W);
    e.dst0  = cq_reg_bits(&ctx.regs, hd[0]);
    e.dst1  = cq_reg_bits(&ctx.regs, hd[1]);

    e.k0.a = cq_scratch_span(&ops,  0u,                (uint32_t)W);
    e.k0.b = cq_scratch_span(&ops,  (uint32_t)W,       (uint32_t)W);
    e.k0.c = cq_scratch_span(&work, 0u,                (uint32_t)W);
    e.k0.t = cq_scratch_span(&work, (uint32_t)W,       (uint32_t)W);
    e.k0.W = W;

    e.k1.a = cq_scratch_span(&ops,  (uint32_t)(2 * W), (uint32_t)W);
    e.k1.b = cq_scratch_span(&ops,  (uint32_t)(3 * W), (uint32_t)W);
    e.k1.c = cq_scratch_span(&work, (uint32_t)(2 * W), (uint32_t)W);
    e.k1.t = cq_scratch_span(&work, (uint32_t)(3 * W), (uint32_t)W);
    e.k1.W = W;

    cq_mock_reset(m);
    cq_sandwich(&ctx, &work, addb2_compute, 2 * e.steps,
                addb2_copyout, 2 * W, &e);
    cq_scratch_dispose(&work);

    CHECK(cq_ref_w_eq(cq_pc_value_w(&ctx, hd[0]), cq_ref_w_add(v[0], v[1], W)));
    CHECK(cq_ref_w_eq(cq_pc_value_w(&ctx, hd[1]), cq_ref_w_add(v[2], v[3], W)));

    /* The head is BOTH blocks' compute halves, at the all-quantum mask where
     * one gate per step holds; the middle is the consumer's own 2W copy-out. */
    CHECK(cq_mock_is_palindrome(m, (size_t)(2 * e.steps), (size_t)(2 * W)));
    CHECK_EQ(cq_mock_count(m), (size_t)(4 * e.steps) + (size_t)(2 * W));

    /* The region came back: `live` is where it was before `work` existed once
     * the two sums are freed. `minted`, `peak` and the free-list length are
     * never compared — all three are monotone (src/qubits.h). */
    cq_reg_free(&ctx, hd[0], cq_pc_zero_proof_rotation_free);
    cq_reg_free(&ctx, hd[1], cq_pc_zero_proof_rotation_free);
    if (!cq_pc_same(before, cq_pc_take(&ctx)))
        cq_h_fail(__FILE__, __LINE__,
                  "two sandwiched add blocks W=%d: pool not restored", W);

    for (uint32_t i = 0; i < ops.n; i++) ops.bits[i] = cq_bit_zero();
    cq_scratch_dispose(&ops);
    cq_ctx_dispose(&ctx);
}

CQ_TEST(the_two_blocks_run_inside_one_sandwich)
{
    static const int WS[] = { 1, 2, 3, 5, 8, 32, 64 };
    cq_mock m;

    cq_mock_init(&m);

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++)
        addb_two_blocks_sandwiched(WS[i], &m);

    cq_mock_dispose(&m);
}
