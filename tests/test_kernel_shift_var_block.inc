/* tests/test_kernel_shift_var_block.inc — included by test_kernel_shift_var.c.
 *
 * M12's BARREL AS AN EXPORTED STEP BLOCK. This is the first of the three step
 * exports PRD-v2 §7.10 records that v1 owes the fp port before its first
 * kernel, and every case below drives `cq_barrel_step` DIRECTLY over a region
 * this file supplies — the way M33/M37 will — rather than through
 * `cq_kernel_shl_var`. What a consumer of a step block can get wrong is the
 * SLOT ARITHMETIC and the LAYOUT, not the gates: the gates are M17's mux
 * (`cq_mux_step`) and two CNOT shuffles, each tested in its own suite.
 *
 * NOTHING HERE READS A GOLDEN, deliberately. `CQOPS_UPDATE_GOLDENS=1` is the
 * documented way to make a red L4 green, so a golden blesses a mutant as
 * readily as a fix; the composition identity below asks M12 what its own step
 * schedule costs and compares that against what the KERNEL emits, which no
 * golden can bless.
 *
 * THE FIXTURE RELEASES ITS SCRATCH ON THE SHADOW, NOT ON A CONSTANT, and that
 * is the one detector this file has that the kernel path does not.
 * `CQ_ZERO_BY_PALINDROME` is a literal 1 (src/sandwich.c), so M03's
 * `proven_zero` guard can never fire for a sandwich; here the evidence is
 * `cq_shadow_known_zero`, which is EXACT on the rotation-free surface (PRD §15
 * D12), so a reverse half that does not cancel aborts inside
 * `cq_qubits_release` in BOTH configurations instead of passing quietly.
 *
 * AND IT ARMS THE I6 EXTENT ITSELF, in Debug, around the two compute halves
 * and not around the copy-out — M09's `sw_arm`/`sw_disarm` by hand. Without
 * that the hand-driven fixture would be the one place in the project where a
 * compute-half gate may target something outside scratch with no diagnostic;
 * widening it to cover the copy-out instead is the wrong fix `sandwich.h`
 * records, because it disarms I6(a) for the compute halves too.
 */

/* The block's direction enum and this suite's row indices are the same three
 * values, and that is asserted rather than commented: the `(cq_barrel_dir)dir`
 * casts below are only sound while it holds, and a renumbering must break a
 * BUILD (bit.h's rule for CQ_BIT_ZERO == 0). */
_Static_assert((int)CQ_BARREL_SHL  == DIR_SHL,  "barrel dir row 0 is shl");
_Static_assert((int)CQ_BARREL_LSHR == DIR_LSHR, "barrel dir row 1 is lshr");
_Static_assert((int)CQ_BARREL_ASHR == DIR_ASHR, "barrel dir row 2 is ashr");

/* W <= 64 everywhere in this file: cq_pc_indices writes one entry per qubit of
 * `dst`, and 64 is the width PRD-v2 §7 ports at. */
enum { BL_MAX_W = 64 };

/* ---- The fixture: three rails, and one region the CALLER owns. ---------- */

typedef struct {
    cq_ctx   ctx;
    cq_sink  sink;
    cq_mock *m;
    int32_t  ha, hb, hd;
    int      W, dir;
} bl_fx;

static void bl_open(bl_fx *f, cq_mock *m, int dir, int W,
                    cq_ref_w va, cq_ref_w qa, cq_ref_w vk, cq_ref_w qk)
{
    f->m    = m;
    f->W    = W;
    f->dir  = dir;
    f->sink = cq_mock_sink(m);

    cq_mock_reset(m);
    cq_ctx_init(&f->ctx, &f->sink);
    f->ha = cq_bk_reg_w(&f->ctx, (uint32_t)W, va, qa);
    f->hb = cq_bk_reg_w(&f->ctx, (uint32_t)W, vk, qk);
    f->hd = cq_reg_alloc_zero(&f->ctx.regs, (uint32_t)W);
}

static void bl_close(bl_fx *f) { cq_ctx_dispose(&f->ctx); }

/* I6(a)'s extent, armed by hand because cq_sandwich is not in the loop here.
 * Debug-only, exactly as M09 has it — plan §2.1 gates the scratch-extent check
 * on CQOPS_DEBUG_INVARIANTS, so in Release these are both no-ops and the
 * fixture's claim is Release's claim: the values, the pool and the stream. */
static void bl_arm(bl_fx *f, const cq_scratch *scr)
{
#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
    f->ctx.scratch_lo = scr->bits;
    f->ctx.scratch_hi = scr->bits + scr->n;
#else
    (void)f; (void)scr;
#endif
}

static void bl_disarm(bl_fx *f)
{
#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
    f->ctx.scratch_lo = NULL;
    f->ctx.scratch_hi = NULL;
#else
    (void)f;
#endif
}

/* ONE SANDWICH OVER THE BLOCK, BY HAND — cq_sandwich's five steps (plan §0.1)
 * with the DRIVER taken out of the loop, so what is under test is the block's
 * schedule and its layout.
 *
 * THE STEP COUNT IS ASKED OF THE MODULE. This loop bound is
 * `cq_barrel_steps(W, dir)` and never a formula written here; a test that
 * re-derived `5WL + W - 2^L + 1` would be a second transcription of the thing
 * it is checking, and the composition case below would then compare M12
 * against itself.
 *
 * THE TWO LENGTHS ARE MEASURED, NOT ASSUMED, and that is required rather than
 * tidy: at the `a`-all-CQ_BIT_ZERO row the INIT phase's W CNOTs fold away, so
 * the head is SHORTER than the step count and a palindrome check handed the
 * step count would fail on a correct library.
 *
 * `pad` IS THE POINT OF THE `off` FIELD AND IS DRIVEN NON-ZERO ON PURPOSE. M12's
 * own entry points pass `off = 0`, so every gate count, golden and palindrome
 * in this project would stay green with `k->off` dropped from `span` entirely —
 * and M33/M37 will pass `off != 0`, because the barrel is one block of a much
 * larger fp region. Nothing else in the suite can see that field at all.
 *
 * AND AN EMPTY PAD IS NOT ENOUGH — MEASURED, IN RELEASE, BEFORE THE CANARY
 * BELOW EXISTED. With the pad left at |0> the mutant that drops `k->off` from
 * `span` SURVIVED all seventeen cases, because a block that ignores `off` then
 * simply runs in the bottom of a region that is merely larger than it needs:
 * self-consistent, in bounds, right answer. Making a sub-region offset
 * load-bearing needs something BELOW it that must not move, so the pad holds
 * |1> and an off-ignoring block lays `cur0` over it and computes a shift of
 * `a ^ (2^pad - 1)` — which the value check sees at every width.
 *
 * THE CANARY'S OWN GATES ARE KEPT OFF THE RECORDED STREAM, at both ends: the
 * setup runs before the mock is reset, and the teardown goes to a throwaway
 * sink. The palindrome is a claim about the BLOCK's gates, and these are the
 * fixture's. */
static void bl_round(bl_fx *f, int pad, size_t *n_head, size_t *n_mid)
{
    cq_scratch scr;
    cq_barrel_block k;
    int W = f->W, n = cq_barrel_steps(W, (cq_barrel_dir)f->dir);
    cq_bit *res, *dst;

    cq_scratch_alloc(&scr, (uint32_t)(pad + cq_barrel_region(W)));
    k.a   = cq_reg_cbits(&f->ctx.regs, f->ha);
    k.b   = cq_reg_cbits(&f->ctx.regs, f->hb);
    k.scr = &scr;
    k.off = (uint32_t)pad;
    k.W   = W;
    k.dir = (cq_barrel_dir)f->dir;

    /* Step 1 — I6(b). Born CQ_BIT_ZERO, so this emits nothing. */
    for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&f->ctx, &scr.bits[i]);

    /* The canary, below the block's own bits and never touched by it. */
    for (int i = 0; i < pad; i++) cq_emit_x(&f->ctx, &scr.bits[i]);

    cq_mock_reset(f->m);
    bl_arm(f, &scr);
    for (int u = 0; u < n; u++) cq_barrel_step(&f->ctx, &k, u);
    *n_head = cq_mock_count(f->m);

    /* Step 3 — copy out with the extent DISARMED: `dst` is outside scratch by
     * construction, which is why M09 disarms here too. */
    bl_disarm(f);
    res = cq_barrel_result(&k);
    dst = cq_reg_bits(&f->ctx.regs, f->hd);
    for (int i = 0; i < W; i++) cq_emit_cx(&f->ctx, &res[i], &dst[i]);
    *n_mid = cq_mock_count(f->m) - *n_head;

    bl_arm(f, &scr);
    for (int u = n - 1; u >= 0; u--) cq_barrel_step(&f->ctx, &k, u);
    bl_disarm(f);

    /* The canary back to |0> so the release below can prove it, off the
     * recorded stream. A correct block never touched it; an off-ignoring one
     * restored it on its own reverse half, which is why the canary is a
     * detector through the VALUE and not through this. */
    if (pad > 0) {
        cq_counter aside;
        cq_sink quiet = cq_sink_counter(&aside);
        const cq_sink *saved = f->ctx.sink;

        cq_count_reset(&aside);
        f->ctx.sink = &quiet;
        for (int i = 0; i < pad; i++) cq_emit_x(&f->ctx, &scr.bits[i]);
        f->ctx.sink = saved;
    }

    /* Step 5 — release, LIFO, on the SHADOW rather than on a constant. */
    for (uint32_t i = scr.n; i-- > 0; ) {
        uint32_t q = cq_bit_qindex(scr.bits[i]);

        cq_ctx_release_qubit(&f->ctx, q,
                             cq_shadow_known_zero(&f->ctx.shadow, q));
        scr.bits[i] = cq_bit_zero();
    }
    cq_scratch_dispose(&scr);
}

/* The suite's own reference, at the suite's own D8 reduction — `eff_k` and
 * SPEC[dir].refn, shared with the L1 sweep so the block and the kernel cannot
 * be checked against two different specifications. */
static cq_ref_w bl_want(int dir, int W, cq_ref_w va, cq_ref_w vk)
{
    cq_kd_shape sh;
    cq_ref_w v[CQ_KD_MAX_SRC];

    cq_kd_default_shape(W, &sh);
    v[0] = va;
    v[1] = vk;
    v[2] = cq_ref_w_zero();
    return SPEC[dir].refn(v, &sh);
}

static const int BL_WS[] = { 1, 2, 3, 4, 5, 8, 16, 32, 64 };
enum { BL_NW = (int)(sizeof BL_WS / sizeof BL_WS[0]) };

/* ---- (a) The stream and the pool, at two ASYMMETRIC mask rows. ---------- */

/* AN ALL-CLASSICAL PAIR IS NOT A PALINDROME CASE AND A SYMMETRIC ONE TESTS THE
 * DIAGONAL, so both rows here are asymmetric — risk R8's mandated witness
 * shape, one per operand:
 *
 *   row 0   `a` all CQ_BIT_Q, the amount all CQ_BIT_ZERO. Every mux control is
 *           a classical ZERO, so all WL Toffolis fold away and the 3W CNOTs of
 *           each mux stage do not. The amount is 1 — IN RANGE at every width,
 *           which matters because an all-ones amount saturates under D8 at
 *           every non-power-of-two width and a saturated barrel can emit
 *           nothing at all (the recorded K4 trap), making a palindrome vacuous.
 *   row 1   the amount all CQ_BIT_Q, `a` all CQ_BIT_ZERO. The INIT phase's W
 *           CNOTs fold away and nothing else does.
 *
 * ROW 1 ALSO RUNS AT `off = 7`, so the block's sub-region offset is exercised
 * by every assertion in this case rather than only by the value check — a
 * misplaced span shows up as a dirty scratch bit at the release, which is
 * caught before any value is read.
 *
 * NON-VACUITY IS ASSERTED, and the one cell of the grid where it legitimately
 * fails is pinned rather than skipped: at W = 1 there are no stages (D8's
 * S = 0), so row 1 is one INIT copy of a constant zero and emits nothing.
 *
 * NO FORWARD/UNCOMPUTE COUNT EQUALITY IS ASSERTED ANYWHERE BELOW (Rule 14).
 * The second round's head length is measured for its own palindrome and is
 * never compared with the first's. */
CQ_TEST(the_block_is_a_palindrome_over_caller_supplied_scratch)
{
    cq_mock m;

    cq_mock_init(&m);

    for (int dir = 0; dir < N_DIRS; dir++)
        for (int j = 0; j < BL_NW; j++)
            for (int row = 0; row < 2; row++) {
                int W = BL_WS[j];
                cq_ref_w ones = cq_ref_w_ones(W);
                cq_ref_w zero = cq_ref_w_zero();
                cq_ref_w va = row == 0 ? cq_ref_w_make(0x5Au, 0u, W) : zero;
                cq_ref_w qa = row == 0 ? ones : zero;
                cq_ref_w vk = row == 0 ? cq_ref_w_make(1u, 0u, W) : zero;
                cq_ref_w qk = row == 0 ? zero : ones;
                uint32_t idx[BL_MAX_W];
                size_t h1 = 0, m1 = 0, h2 = 0, m2 = 0;
                uint32_t nq;
                bl_fx f;
                int32_t live3[3], live2[2];

                bl_open(&f, &m, dir, W, va, qa, vk, qk);
                live3[0] = f.ha; live3[1] = f.hb; live3[2] = f.hd;
                live2[0] = f.ha; live2[1] = f.hb;

                bl_round(&f, row * 7, &h1, &m1);

                /* The stream: head, the W-CNOT copy-out, head reversed. */
                CHECK(cq_mock_is_palindrome(&m, h1, m1));
                CHECK_EQ(m1, (size_t)W);
                if (W == 1 && row == 1) CHECK_EQ(h1, 0u);
                else                    CHECK(h1 > 0u);

                /* The value, so the profile above is not that of a wrong
                 * circuit, and L2 with the region already handed back. */
                CHECK(cq_ref_w_eq(cq_pc_value_w(&f.ctx, f.hd),
                                  bl_want(dir, W, va, vk)));
                CHECK(cq_pc_live_is_exactly(&f.ctx, live3, 3u));

                /* The uncompute is the same block a second time. */
                bl_round(&f, row * 7, &h2, &m2);
                CHECK(cq_mock_is_palindrome(&m, h2, m2));
                CHECK(cq_ref_w_is_zero(cq_pc_value_w(&f.ctx, f.hd)));
                CHECK(cq_pc_live_is_exactly(&f.ctx, live3, 3u));

                /* L3: name dst's indices BEFORE the free tombstones the rail. */
                nq = cq_pc_indices(&f.ctx, f.hd, idx, (uint32_t)BL_MAX_W);
                cq_reg_free(&f.ctx, f.hd, cq_pc_zero_proof_rotation_free);
                CHECK(cq_pc_live_is_exactly(&f.ctx, live2, 2u));
                CHECK(cq_pc_indices_are_free(&f.ctx, idx, nq));

                bl_close(&f);
            }

    cq_mock_dispose(&m);
}

/* ---- (b) The composition identity. ------------------------------------- */

/* THE KERNEL IS THE BLOCK TWICE PLUS A W-CNOT COPY-OUT, AND EVERY TERM IS
 * MEASURED. This case asks M12 what `cq_barrel_steps(W, dir)` step calls emit —
 * by running them — and pins `cq_kernel_*_var`'s forward tuple against that. It
 * reads no golden, so `CQOPS_UPDATE_GOLDENS=1` cannot bless a kernel that has
 * drifted away from the block it is supposed to be made of.
 *
 * WHAT IT DOES *NOT* CATCH, MEASURED RATHER THAN ASSUMED (2026-09-18, Release).
 * The obvious claim to make here is K11's — "a narrowing shortens the block and
 * this stays red where the golden gets regenerated" — and it is FALSE for this
 * shape. Both sides of the identity ask `cq_barrel_steps`, so a mutant that
 * makes the export itself one step shorter moves the measurement and the
 * expectation together and this case stayed GREEN while eight others went red.
 * The detectors for a step-count change are `l4_goldens` (blessable) and
 * `the_stream_is_a_palindrome_around_the_copyout`, whose `n_compute_of` is an
 * INDEPENDENT re-derivation of the schedule in the test file (not blessable).
 * What this case does hold is the RELATION: a value-neutral gate added on the
 * kernel side only — two cancelling X in the copy-out — reddens exactly
 * `l4_goldens`, `the_w8_column...`, the palindrome and this case, and leaves
 * both of the block cases below green. It sits in the kernel-vs-block gap and
 * nowhere else; do not restate K11's claim over it.
 *
 * `pad` IS 0 HERE, DELIBERATELY, because the comparison is against a kernel
 * that passes `off = 0`. The `off` field is exercised by the two cases either
 * side of this one.
 *
 * ONE GATE PER STEP IS MEASURED HERE TOO, at the mask where it is a claim:
 * all-quantum, where no fold can fire, so the head length IS the step count
 * (K10.md §2.2's re-issue).
 *
 * W = 1 IS THE DELEGATED ROW AND HAS ITS OWN ARM. L is 0, `barrel()` hands the
 * call to M11 before any scratch exists, and the kernel emits W CX with no
 * sandwich and no copy-out — so the identity does not apply and asserting it
 * would be asserting the barrel runs where it provably does not. */
CQ_TEST(the_kernel_is_the_exported_block_twice_plus_the_copyout)
{
    cq_mock m;

    cq_mock_init(&m);

    for (int dir = 0; dir < N_DIRS; dir++)
        for (int j = 0; j < BL_NW; j++) {
            int W = BL_WS[j];
            cq_ref_w all = cq_ref_w_ones(W);
            size_t h = 0, mid = 0, hx = 0, hcx = 0, hccx = 0;
            cq_counter fwd, unc;
            bl_fx f;

            /* The block alone, at the ALL-QUANTUM mask on both operands —
             * which is the mask every L4 golden is pinned at and the fixed
             * point of no-demotion (D6). */
            bl_open(&f, &m, dir, W, all, all, all, all);
            bl_round(&f, 0, &h, &mid);

            for (size_t i = 0; i < h; i++)
                switch (cq_mock_at(&m, i)->op) {
                case CQ_OP_X:   hx++;   break;
                case CQ_OP_CX:  hcx++;  break;
                default:        hccx++; break;
                }

            CHECK_EQ(h, (size_t)cq_barrel_steps(W, (cq_barrel_dir)dir));
            CHECK_EQ(mid, (size_t)W);
            bl_close(&f);

            cq_kd_measure(&SPEC[dir], W, &fwd, &unc);

            if (cq_shift_stages(W) == 0) {
                CHECK_GATES(fwd.x, fwd.cx, fwd.ccx, 0u, (uint64_t)W, 0u);
                CHECK_EQ(hx + hccx, 0u);
                CHECK_EQ(hcx, (size_t)W);
            } else {
                CHECK_GATES(fwd.x, fwd.cx, fwd.ccx,
                            2u * (uint64_t)hx,
                            2u * (uint64_t)hcx + (uint64_t)W,
                            2u * (uint64_t)hccx);
            }
        }

    cq_mock_dispose(&m);
}

/* ---- (c) The value, over a drawn sample. -------------------------------- */

/* L1 FOR THE BLOCK, at cq_kd_samples() cases per (direction, width), drawing
 * the value and the bit-kind mask PER OPERAND from one seeded RNG — the shared
 * budget's shape (CLAUDE.md's L1 row), not a product, because every case runs a
 * real barrel over a W(3S+1)-qubit region.
 *
 * THREE CASES ARE FORCED INSIDE THE BUDGET RATHER THAN ON TOP OF IT, and all
 * three are D8 at the BLOCK level:
 *
 *   case 0   the raw amount is exactly W. At a power-of-two width that reduces
 *            to 0 and the shift is the IDENTITY — the knowing delta from
 *            Bennett's constant path (PRD §15 D8), which must not be "fixed"
 *            to zero; at W = 3 and W = 5 it reduces to W and SATURATES.
 *   case 1   the raw amount is 2^S - 1, the top of the reduced range.
 *   case 2   the raw amount is 0, the identity copy.
 *
 * The amount is all-quantum in the three forced cases, so the mux controls are
 * real wires and the answer comes out of the circuit rather than out of a fold.
 *
 * THE REFERENCE IS THE SUITE'S OWN (bl_want -> SPEC[dir].refn -> eff_k), so the
 * block and the kernel are checked against ONE specification of D8's
 * reduction; a second implementation of ceil(log2 W) here would be a second
 * chance to get it wrong. */
CQ_TEST(the_blocks_result_span_is_the_reference_shift)
{
    cq_mock m;
    uint64_t cases = 0;

    cq_mock_init(&m);

    for (int dir = 0; dir < N_DIRS; dir++)
        for (int j = 0; j < BL_NW; j++) {
            int W = BL_WS[j], S = cq_shift_stages(W);
            uint64_t span = (uint64_t)1 << S;
            uint64_t seed = 0x9E3779B97F4A7C15ull ^ ((uint64_t)W << 8)
                            ^ (uint64_t)dir;
            cq_bk_rng rng;
            int n = cq_kd_samples();

            cq_bk_rng_init(&rng, seed);

            for (int c = 0; c < n; c++) {
                cq_ref_w ones = cq_ref_w_ones(W);
                cq_ref_w va, qa, vk, qk, got;
                size_t h = 0, mid = 0;
                bl_fx f;

                if (c < 3) {
                    uint64_t raw = (c == 0) ? (uint64_t)W
                                 : (c == 1) ? span - 1u : 0u;

                    va = cq_ref_w_make(cq_bk_rng_next(&rng),
                                       cq_bk_rng_next(&rng), W);
                    qa = ones;
                    vk = cq_ref_w_make(raw, 0u, W);
                    qk = ones;
                } else {
                    va = cq_ref_w_make(cq_bk_rng_next(&rng),
                                       cq_bk_rng_next(&rng), W);
                    qa = cq_ref_w_make(cq_bk_rng_next(&rng),
                                       cq_bk_rng_next(&rng), W);
                    vk = cq_ref_w_make(cq_bk_rng_next(&rng),
                                       cq_bk_rng_next(&rng), W);
                    qk = cq_ref_w_make(cq_bk_rng_next(&rng),
                                       cq_bk_rng_next(&rng), W);
                }

                bl_open(&f, &m, dir, W, va, qa, vk, qk);
                bl_round(&f, (c % 2) * 3, &h, &mid);
                got = cq_pc_value_w(&f.ctx, f.hd);

                if (!cq_ref_w_eq(got, bl_want(dir, W, va, vk)))
                    cq_h_fail(__FILE__, __LINE__,
                              "block %s W=%d amount=0x%llx: got 0x%llx%016llx",
                              SPEC[dir].name, W,
                              (unsigned long long)vk.lo,
                              (unsigned long long)got.hi,
                              (unsigned long long)got.lo);
                bl_close(&f);
                cases++;
            }

            printf("# barrel block %s W=%3d: %d cases (3 D8 corners forced), "
                   "seed 0x%llx\n", SPEC[dir].name, W, n,
                   (unsigned long long)seed);
        }

    fflush(stdout);
    CHECK(cases > 0u);
    cq_mock_dispose(&m);
}
