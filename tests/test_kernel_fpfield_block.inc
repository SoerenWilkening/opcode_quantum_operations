/* tests/test_kernel_fpfield_block.inc — M31's class predicate AS AN EXPORTED
 * STEP BLOCK. Included by tests/test_kernel_fpfield.c.
 *
 * THE SUBJECT IS THE EXPORT, NOT THE PREDICATE. Whether `is_nan` returns the
 * right bit is the sweep's job, against the host. What is new here is that a
 * CONSUMER can drive the compute half itself: M36 `fcmp` needs `a_nan` inside
 * its own compute half and M33 `fadd` needs all three of `a_nan`/`a_inf`/
 * `a_zero`, and neither may call the kernel — it is a whole sandwich and
 * `cq_sandwich` refuses nesting in both configurations. The block is the only
 * door, and these cases are what says it is a real one.
 *
 * EVERY CASE HERE READS NO GOLDEN. `CQOPS_UPDATE_GOLDENS=1` is the documented
 * way to make a red L4 green, so a golden blesses a narrowing as readily as a
 * fix — and this module's narrowing (run the block at 11 lanes, not 64) is
 * exactly K11's "looks like an optimisation" shape.
 *
 * SCRATCH SPANS, NOT REGISTERS, for the reason CLAUDE.md's `cq_ult_block`
 * callout records: inside a sandwich a block's operands are pre-materialised
 * scratch, not rails, so a fixture built out of registers exercises a calling
 * shape no consumer uses.
 */

enum { FPB_REGION_MAX = 2 * (2 * CQ_FP64_W - 1) + 3,
       FPB_NAMED_MAX  = CQ_FP64_W + 2 * FPB_REGION_MAX + 1 };

typedef struct {
    cq_ctx            ctx;
    cq_scratch        ops, work;
    cq_fp_class_block k[2];
    int               nb;               /* how many blocks share `work` */
    int32_t           hd;
    uint32_t          named[FPB_NAMED_MAX];
    uint32_t          n_named;
    cq_pc_snap        pristine;
} fpb_fx;

/* A rail lane is a qubit exactly where the mask says so — Rule 5's one
 * sanctioned route, so a set CLASSICAL bit costs no gate and a set QUANTUM one
 * costs the materialising X every caller then resets away. */
static void fpb_mint(cq_ctx *ctx, cq_bit *v, int n, cq_ref_w value, cq_ref_w qm)
{
    for (int i = 0; i < n; i++) {
        v[i] = cq_bit_const(cq_ref_w_bit(value, i));
        if (cq_ref_w_bit(qm, i)) cq_materialise(ctx, &v[i]);
    }
}

static uint32_t fpb_collect(const cq_scratch *scr, uint32_t *out, uint32_t n)
{
    for (uint32_t i = 0; i < scr->n; i++)
        if (cq_bit_is_qubit(scr->bits[i])) out[n++] = cq_bit_qindex(scr->bits[i]);
    return n;
}

/* `nb` blocks at ASCENDING OFFSETS in ONE region. Two is the case that
 * matters: a single block cannot see its own offset, so dropping `k->off` from
 * the span helper slides the block wholesale and leaves the value, the
 * palindrome and the pool all correct. Only a second block detects it. */
static void fpb_open(fpb_fx *f, const cq_fp_class *cls, int nb,
                     uint64_t bits, cq_ref_w qm, const cq_sink *sink)
{
    cq_bit *pa;
    uint32_t off = 0u;

    cq_ctx_init(&f->ctx, sink);
    f->pristine = cq_pc_take(&f->ctx);     /* BEFORE the mint — see (a) */
    f->nb = nb;

    cq_scratch_alloc(&f->ops, (uint32_t)CQ_FP64_W);
    for (int b = 0; b < nb; b++) off += cq_fp_class_region(cls[b]);
    cq_scratch_alloc(&f->work, off);

    pa = cq_scratch_span(&f->ops, 0u, (uint32_t)CQ_FP64_W);
    fpb_mint(&f->ctx, pa, CQ_FP64_W,
             cq_ref_w_make(bits, 0u, CQ_FP64_W), qm);

    /* I6(b): the whole region is materialised before any gate, which is what
     * makes a step emit the same gate forwards and backwards. */
    for (uint32_t i = 0; i < f->work.n; i++)
        cq_materialise(&f->ctx, &f->work.bits[i]);

    off = 0u;
    for (int b = 0; b < nb; b++) {
        f->k[b].a   = pa;
        f->k[b].scr = &f->work;
        f->k[b].off = off;
        f->k[b].cls = cls[b];
        off += cq_fp_class_region(cls[b]);
    }

    f->hd = cq_reg_alloc_zero(&f->ctx.regs, 1u);

    f->n_named = fpb_collect(&f->ops,  f->named, 0u);
    f->n_named = fpb_collect(&f->work, f->named, f->n_named);
}

static void fpb_close(fpb_fx *f)
{
    for (uint32_t i = 0; i < f->ops.n;  i++) f->ops.bits[i]  = cq_bit_zero();
    for (uint32_t i = 0; i < f->work.n; i++) f->work.bits[i] = cq_bit_zero();
    cq_scratch_dispose(&f->ops);
    cq_scratch_dispose(&f->work);
    cq_ctx_dispose(&f->ctx);
}

static void fpb_forward(fpb_fx *f, int b)
{
    for (int u = 0; u < cq_fp_class_steps(f->k[b].cls); u++)
        cq_fp_class_step(&f->ctx, &f->k[b], u);
}

static void fpb_reverse(fpb_fx *f, int b)
{
    for (int u = cq_fp_class_steps(f->k[b].cls) - 1; u >= 0; u--)
        cq_fp_class_step(&f->ctx, &f->k[b], u);
}

/* THE SHADOW IS THE PROOF, DELIBERATELY. cq_sandwich releases its own region
 * under CQ_ZERO_BY_PALINDROME, a literal 1, which would launder exactly the
 * fault this fixture exists to see; the shadow is EXACT rather than
 * conservative on a rotation-free surface. Checked BEFORE releasing, because
 * cq_qubits_release aborts on an index not proven |0> and an abort reports as
 * a crashed binary rather than as a failed case. */
static void fpb_reclaim(fpb_fx *f, cq_scratch *scr, const char *what)
{
    int clean = 1;

    for (uint32_t i = 0; i < scr->n; i++) {
        if (!cq_bit_is_qubit(scr->bits[i])) continue;
        if (!cq_shadow_known_zero(&f->ctx.shadow, cq_bit_qindex(scr->bits[i]))) {
            cq_h_fail(__FILE__, __LINE__,
                      "fp class block: %s bit %u (q%u) is not back at |0> after "
                      "the reverse half", what, i, cq_bit_qindex(scr->bits[i]));
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

/* L2 AS A SET, OVER RAW INDICES. cq_pc_live_is_exactly takes HANDLES and a
 * block's operands and scratch are owned by no register, so the union form is
 * written here: every named index live, PLUS the live count equal to the named
 * count, which together are set equality. A count on its own is strictly
 * weaker — the measured L2-masked-by-L3 finding — and the pair closes it. */
static void fpb_live_is_exactly(fpb_fx *f, uint32_t n, const char *when)
{
    for (uint32_t i = 0; i < n; i++)
        if (cq_qubits_is_free(&f->ctx.pool, f->named[i]))
            cq_h_fail(__FILE__, __LINE__,
                      "L2 fp class block %s: q%u is on the free list",
                      when, f->named[i]);

    if (cq_qubits_live(&f->ctx.pool) != n)
        cq_h_fail(__FILE__, __LINE__,
                  "L2 fp class block %s: %u live, %u named — an index is live "
                  "that nothing owns", when, cq_qubits_live(&f->ctx.pool), n);
}

/* THREE MASKS, AND THE THIRD IS THE POINT. All-quantum is what L4 pins;
 * all-classical on the OPERAND is legal for a BLOCK (the R9 short-circuit is
 * the kernel's, not the block's) and is where every view lane folds; and
 * EXPONENT-LANES-ONLY is the asymmetric row — the fraction view goes entirely
 * classical while the exponent view stays live, which is the only mask that
 * can see the two `eq` blocks' operands swapped. */
static cq_ref_w fpb_mask(int which)
{
    if (which == 0) return cq_ref_w_ones(CQ_FP64_W);
    if (which == 1) return cq_ref_w_zero();
    return cq_ref_w_make(CQ_FP64_EXP_MASK & ~CQ_F64_SIGN_MASK, 0u, CQ_FP64_W);
}

static const char *fpb_mask_name(int which)
{
    return which == 0 ? "all-quantum" : which == 1 ? "all-classical"
                                                   : "exponent-lanes-only";
}

/* ---- (c1) The block is a palindrome, and it gives the pool back. --------- */

/* DRIVEN THE WAY A CONSUMER DRIVES IT — a contiguous run of step indices — and
 * NOT through cq_sandwich, which would be M09's suite again and would release
 * the region under its own literal-1 proof.
 *
 * THE HEAD IS MEASURED AT EVERY MASK, INCLUDING ALL-QUANTUM, and that is what
 * parts this fixture from M16's. There, one slot is one gate at the all-quantum
 * mask, so the head IS the step count. Here the views' constant lanes and the
 * constant spans fold at every mask, so the head is strictly below the step
 * count everywhere and asserting equality would be asserting the narrowing
 * this module refuses. */
CQ_TEST(the_class_block_is_a_palindrome_and_gives_the_pool_back)
{
    cq_mock m;

    cq_mock_init(&m);

    for (int i = 0; i < N_FP_ROWS; i++)
        for (int mk = 0; mk < 3; mk++) {
            cq_fp_class cls = ROWS[i].cls;
            cq_sink s = cq_mock_sink(&m);
            fpb_fx f;
            cq_pc_snap after;
            size_t head, mid;
            uint32_t held[2], n_held;

            /* THE OPERAND IS |0>, AND IT COSTS THIS CASE NOTHING. The §3 fold
             * table dispatches on a bit's KIND and never on a qubit's VALUE
             * (D6), so the recorded stream is the one any value gives — the
             * value claim is (c3)'s. What |0> buys is that the operand region
             * can be RECLAIMED: fpb_reclaim's proof is the shadow, and a rail
             * minted at QNAN_B holds eleven ones in its exponent that the
             * block never touches and so never clears. */
            fpb_open(&f, &cls, 1, 0ull, fpb_mask(mk), &s);
            fpb_live_is_exactly(&f, f.n_named, "after the MINT");

            cq_mock_reset(&m);
            fpb_forward(&f, 0);
            head = cq_mock_count(&m);

            if (head == 0u || head >= (size_t)cq_fp_class_steps(cls))
                cq_h_fail(__FILE__, __LINE__,
                          "%s [%s]: head is %zu gates against %d slots — it "
                          "must be non-empty and STRICTLY below the slot count "
                          "(the views' constant lanes fold at every mask)",
                          ROWS[i].spec.name, fpb_mask_name(mk), head,
                          cq_fp_class_steps(cls));

            /* Plan §0.4 obligation 2: the block allocates nothing. */
            fpb_live_is_exactly(&f, f.n_named, "after the FORWARD");

            cq_emit_cx(&f.ctx, cq_fp_class_flag(&f.k[0]),
                       cq_reg_bits(&f.ctx.regs, f.hd));
            mid = cq_mock_count(&m) - head;
            CHECK_EQ((long long)mid, 1);
            n_held = cq_pc_indices(&f.ctx, f.hd, held, 2u);
            CHECK_EQ((long long)n_held, 1);
            f.named[f.n_named] = held[0];

            fpb_reverse(&f, 0);
            CHECK(cq_mock_is_palindrome(&m, head, mid));
            fpb_live_is_exactly(&f, f.n_named + 1u, "after the UNCOMPUTE");

            fpb_reclaim(&f, &f.work, "scratch");
            fpb_reclaim(&f, &f.ops,  "operands");
            if (!cq_pc_live_is_exactly(&f.ctx, &f.hd, 1u))
                cq_h_fail(__FILE__, __LINE__,
                          "L2 %s [%s]: after the RECLAIM, only dst may be live",
                          ROWS[i].spec.name, fpb_mask_name(mk));

            /* L3 AGAINST THE PRISTINE POOL, the one snapshot that can return —
             * one taken after the mint counts the fixture's own operands, and
             * reclaiming those is the thing being asserted. `minted`, `peak`
             * and the free-list length are never compared; all three are
             * monotone. The INDEX SET is the sharp half: a release of the
             * WRONG index restores `live` exactly and no count can see it.
             *
             * THE FIXTURE CLEARS ITS OWN dst FIRST, and that is not a fudge:
             * the copy-out above wrote the predicate into it, `is_zero(0)` is
             * TRUE, and a rail holding 1 STRANDS under D15 rather than
             * returning its qubit — which would be a fixture artefact reported
             * as a pool failure. The block's contract ends at the flag. */
            if (cq_pc_value(&f.ctx, f.hd) != 0u)
                cq_emit_x(&f.ctx, cq_reg_bits(&f.ctx.regs, f.hd));
            cq_reg_free(&f.ctx, f.hd, cq_pc_zero_proof_rotation_free);
            after = cq_pc_take(&f.ctx);
            if (!cq_pc_same(f.pristine, after))
                cq_h_fail(__FILE__, __LINE__,
                          "L3 %s [%s]: pool not restored — live %u -> %u",
                          ROWS[i].spec.name, fpb_mask_name(mk),
                          f.pristine.live, after.live);
            if (!cq_pc_indices_are_free(&f.ctx, f.named, f.n_named + 1u))
                cq_h_fail(__FILE__, __LINE__, "L3 %s [%s]: see above",
                          ROWS[i].spec.name, fpb_mask_name(mk));

            fpb_close(&f);
        }

    cq_mock_dispose(&m);
}

/* ---- (c2) Two blocks, one region, two offsets. -------------------------- */

/* THE ONLY DETECTOR OF A DROPPED `off`. With `k->off` gone from the span
 * helper both blocks occupy the SAME bits: the palindrome still holds (the
 * forward pair is undone by the reverse pair in exact reverse order), the pool
 * still comes back, and every gate is well formed — so the value is the whole
 * of the evidence. Block 0's flag is corrupted by block 1's gates, and the two
 * classes are chosen so that their constants differ and the corruption cannot
 * cancel. */
CQ_TEST(two_blocks_in_one_region_do_not_collide)
{
    static const uint64_t VALUES[] = {
        CQ_F64_QNAN_B, CQ_F64_POS_INF, CQ_F64_NEG_ZERO,
        CQ_F64_MIN_SUBNORMAL, CQ_F64_ONE
    };
    cq_counter cnt;
    cq_sink s;

    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);

    for (int i = 0; i < N_FP_ROWS; i++)
        for (int j = 0; j < N_FP_ROWS; j++) {
            cq_fp_class cls[2];

            if (i == j) continue;
            cls[0] = ROWS[i].cls;
            cls[1] = ROWS[j].cls;

            for (size_t v = 0; v < sizeof VALUES / sizeof VALUES[0]; v++) {
                fpb_fx f;

                fpb_open(&f, cls, 2, VALUES[v], cq_ref_w_ones(CQ_FP64_W), &s);

                /* The offsets are distinct and the second is not zero — which
                 * is what makes the mutant representable at all. */
                CHECK_EQ(f.k[0].off, 0u);
                CHECK(f.k[1].off > 0u);

                fpb_forward(&f, 0);
                fpb_forward(&f, 1);

                for (int b = 0; b < 2; b++) {
                    cq_shadow sv = cq_shadow_get(&f.ctx.shadow,
                                       cq_bit_qindex(*cq_fp_class_flag(&f.k[b])));
                    int want = host_class(VALUES[v], f.k[b].cls);

                    if (sv.unknown || (int)sv.value != want)
                        cq_h_fail(__FILE__, __LINE__,
                                  "two blocks (%s at %u, %s at %u) on "
                                  "0x%016llx: block %d's flag = %s%u, want %d "
                                  "— a block that ignored its own `off` writes "
                                  "over its neighbour",
                                  ROWS[i].spec.name, f.k[0].off,
                                  ROWS[j].spec.name, f.k[1].off,
                                  (unsigned long long)VALUES[v], b,
                                  sv.unknown ? "unknown/" : "", sv.value, want);
                }

                fpb_reverse(&f, 1);
                fpb_reverse(&f, 0);
                fpb_reclaim(&f, &f.work, "scratch");
                fpb_close(&f);
            }
        }
}

/* ---- (c3) The flag is the PREDICATE, not its negation. ------------------ */

/* `cq_eq_flag`'s wire holds `a != b`, so every `==` occurrence in the four
 * rows owes itself a `lower_not1!`. Drop one and the block computes the
 * complement of one conjunct: same gates minus two, same palindrome, clean
 * scratch, and a plausible answer. The oracle is the HOST, which shares
 * nothing with either transcription.
 *
 * DRIVEN OVER §7.12's SINGLES, at two masks. A uniform draw at 64 bits never
 * produces a NaN, an infinity or a subnormal, so without the anchor list three
 * of the four predicates would be exercised on their FALSE branch only — and a
 * block that returned a constant 0 would pass. */
CQ_TEST(the_blocks_flag_is_the_predicate_not_its_negation)
{
    cq_counter cnt;
    cq_sink s;
    int n_anch = cq_fp_anchors_unary(CQ_FP64_W, -1, NULL);
    int seen_true = 0;

    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);

    for (int i = 0; i < N_FP_ROWS; i++)
        for (int mk = 0; mk <= 2; mk += 2)          /* all-quantum, exp-only */
            for (int j = 0; j < n_anch; j++) {
                cq_fp_class cls = ROWS[i].cls;
                cq_ref_w v[CQ_KD_MAX_SRC];
                fpb_fx f;
                cq_shadow sv;
                uint64_t bits;
                int want;

                v[0] = cq_ref_w_zero();
                CHECK(cq_fp_anchors_unary(CQ_FP64_W, j, v) == 1);
                bits = v[0].lo;
                want = host_class(bits, cls);
                seen_true += want;

                fpb_open(&f, &cls, 1, bits, fpb_mask(mk), &s);
                fpb_forward(&f, 0);

                sv = cq_shadow_get(&f.ctx.shadow,
                                   cq_bit_qindex(*cq_fp_class_flag(&f.k[0])));
                if (sv.unknown || (int)sv.value != want)
                    cq_h_fail(__FILE__, __LINE__,
                              "%s [%s] on 0x%016llx: flag = %s%u, the host "
                              "says %d", ROWS[i].spec.name, fpb_mask_name(mk),
                              (unsigned long long)bits,
                              sv.unknown ? "unknown/" : "", sv.value, want);

                /* Ancilla-clean with LIVE operands, which the palindrome case
                 * at a single value cannot claim on its own: the reverse half
                 * has to cancel against whatever `a` actually holds. */
                fpb_reverse(&f, 0);
                fpb_reclaim(&f, &f.work, "scratch");
                fpb_close(&f);
            }

    /* NOT VACUOUS IN THE DIRECTION THAT MATTERS: an implementation returning a
     * constant 0 would pass every FALSE row, and the anchors are what make the
     * TRUE rows exist at all. */
    CHECK(seen_true > 0);
}

/* ---- (c4) The kernels are the block twice plus their copy-out. ---------- */

static void fpb_block_cost(cq_fp_class cls, cq_counter *out)
{
    cq_counter cnt;
    cq_sink s;
    fpb_fx f;

    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);
    fpb_open(&f, &cls, 1, CQ_F64_QNAN_B, cq_ref_w_ones(CQ_FP64_W), &s);
    cq_count_reset(&cnt);                        /* drop the materialising Xs */
    fpb_forward(&f, 0);
    *out = cnt;
    fpb_close(&f);
}

/* THE ASSERTION THAT SURVIVES `CQOPS_UPDATE_GOLDENS=1`. Each kernel is
 * Bennett-in-the-small over this block — compute, copy out, compute backwards
 * (PRD §5) — so its tuple is exactly twice what the block just cost plus one
 * CNOT, and BOTH terms are MEASURED: the block by driving it, the kernel by
 * cq_kd_measure. Nothing here is a formula, and it is what says the kernel's
 * compute half IS the exported block rather than a second transcription.
 *
 * Forward and `_unc` are asserted SEPARATELY against the same measured block.
 * That is not the R6 mistake: the claim is not `unc == forward` but that each
 * pass, at the mask cq_kd_measure drives, is the block twice plus the copy-out. */
CQ_TEST(the_kernels_are_the_block_twice_plus_their_copyout)
{
    for (int i = 0; i < N_FP_ROWS; i++) {
        cq_counter blk, fwd, unc;

        fpb_block_cost(ROWS[i].cls, &blk);
        CHECK_EQ(blk.ry + blk.rz + blk.mz, 0);
        CHECK(cq_count_total(&blk) > 0u);

        cq_kd_measure(&ROWS[i].spec, CQ_FP64_W, &fwd, &unc);
        CHECK_GATES(fwd.x, fwd.cx, fwd.ccx,
                    2u * blk.x, 2u * blk.cx + 1u, 2u * blk.ccx);
        CHECK_GATES(unc.x, unc.cx, unc.ccx,
                    2u * blk.x, 2u * blk.cx + 1u, 2u * blk.ccx);
    }
}
