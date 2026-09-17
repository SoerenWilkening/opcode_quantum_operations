/* tests/test_kernel_divrem_common.inc — shared by the M19 and M20 suites.
 *
 * THE SUITE IS SPLIT IN TWO ON THE M19/M20 MODULE SEAM, UP FRONT, and that is
 * `bd mmv`'s option (a) taken before measuring rather than after. The bead
 * records the barrel becoming the longest pole in the project at a TENTH of
 * K12's size, and says in as many words that K11 and K12 would need the split
 * more. One `udiv` case at W = 64 is 2 x (34W^2+5W) = 279,168 gate emissions
 * over 2 x 33,023 qubit acquisitions — 2.6x a `mul` case at the same width, on
 * 8x the region — and there are FOUR opcodes. Two binaries let `ctest -j`
 * overlap them; the golden files are split the same way for the same reason
 * (cq_gold_close requires every row in a file to have been visited, so two
 * binaries cannot share one).
 *
 * What lives here is only what BOTH halves use: the three inner blocks measured
 * at a width, the masked single-case runner behind the palindrome checks, the
 * budgeted sweep, and the R9 classical case. What each half asserts lives in
 * its own .c.
 */

/* ---- The three inner blocks, MEASURED. ---------------------------------- */

/* ONE PRE-MATERIALISED SCRATCH REGION, which is exactly how K12 hands a block
 * its operands: every bit CQ_BIT_Q from step 0 (I6(b)), and every sub-array a
 * span of one contiguous region.
 *
 * REGISTERS CANNOT SERVE HERE, and the reason is a real constraint rather than
 * a preference: the ult block's carry chain is W+1 bits and CQ_REG_WIDTH_MAX is
 * 128, so at the top shipped divrem width a 129-bit register is
 * unrepresentable. Measured, not foreseen — the first draft of this file built
 * the operands as registers and aborted in refmodel at W = 128.
 *
 * The VALUES are irrelevant to a gate count — the §3 fold table dispatches on
 * KIND and never on a qubit's value (D6) — which is why a region of qubits all
 * holding |0> measures the same tuple a live one would. */
typedef struct { cq_ctx ctx; cq_counter cnt; cq_sink sink; } dr_blockfx;

static void dr_bfx_open(dr_blockfx *f)
{
    cq_count_reset(&f->cnt);
    f->sink = cq_sink_counter(&f->cnt);
    cq_ctx_init(&f->ctx, &f->sink);
}

static cq_bit *dr_bfx_region(dr_blockfx *f, cq_scratch *scr, int n)
{
    cq_scratch_alloc(scr, (uint32_t)n);
    for (uint32_t i = 0; i < scr->n; i++) cq_materialise(&f->ctx, &scr->bits[i]);
    return scr->bits;
}

/* The region's qubits go back to nobody — the intended Rule-6 safe leak (PRD
 * §10), which is what cq_ctx_dispose does with every live rail. The cq_bit
 * entries are reset first because M08's dispose check is a KIND check and would
 * otherwise, correctly, refuse a region full of live qubits. */
static void dr_bfx_drop(cq_scratch *scr)
{
    for (uint32_t i = 0; i < scr->n; i++) scr->bits[i] = cq_bit_zero();
    cq_scratch_dispose(scr);
}

static void dr_ult_cost(int W, cq_counter *out)
{
    dr_blockfx f;
    cq_scratch scr;
    cq_ult_block k;
    cq_bit *r;

    dr_bfx_open(&f);
    r = dr_bfx_region(&f, &scr, 5 * W + 1);   /* a b nb carry(W+1) axnb */
    k.a     = r;
    k.b     = r + W;
    k.nb    = r + 2 * W;
    k.carry = r + 3 * W;                      /* W+1 — carry[W] IS the flag */
    k.axnb  = r + 4 * W + 1;
    k.W     = W;

    cq_count_reset(&f.cnt);                   /* drop the materialising Xs */
    for (int u = 0; u < cq_ult_steps(W); u++) cq_ult_step(&f.ctx, &k, u);
    *out = f.cnt;
    dr_bfx_drop(&scr);
    cq_ctx_dispose(&f.ctx);
}

static void dr_sub_cost(int W, cq_counter *out)
{
    dr_blockfx f;
    cq_scratch scr;
    cq_sub_block k;
    cq_bit *r;

    dr_bfx_open(&f);
    r = dr_bfx_region(&f, &scr, 5 * W);       /* a b nb d c */
    k.a  = r;
    k.b  = r + W;
    k.nb = r + 2 * W;
    k.d  = r + 3 * W;
    k.c  = r + 4 * W;
    k.W  = W;

    cq_count_reset(&f.cnt);
    for (int u = 0; u < cq_sub_steps(W); u++) cq_sub_step(&f.ctx, &k, u);
    *out = f.cnt;
    dr_bfx_drop(&scr);
    cq_ctx_dispose(&f.ctx);
}

static void dr_mux_cost(int W, cq_counter *out)
{
    dr_blockfx f;
    cq_scratch scr;
    cq_mux_block k;
    cq_bit *r;

    dr_bfx_open(&f);
    r = dr_bfx_region(&f, &scr, 4 * W + 1);   /* cond(1) t f r d */
    k.cond = r;
    k.t    = r + 1;
    k.f    = r + W + 1;
    k.r    = r + 2 * W + 1;
    k.d    = r + 3 * W + 1;

    cq_count_reset(&f.cnt);
    for (int u = 0; u < CQ_MUX_STEPS_PER_BIT * W; u++) cq_mux_step(&f.ctx, &k, u);
    *out = f.cnt;
    dr_bfx_drop(&scr);
    cq_ctx_dispose(&f.ctx);
}

/* One iteration of K12.md §2.2's decode, as a tuple, from the three MEASURED
 * blocks plus K12's own two CNOTs: P0's shift-in and P4's quotient bit.
 *
 * THIS IS THE FUNCTION THAT MAKES THE L4 GOLDEN A CONSEQUENCE RATHER THAN A
 * CLAIM. `want_q` drops P4 and nothing else — the whole of the udiv/urem
 * difference, and the one place M19's index arithmetic branches on the opcode. */
static void dr_iter_cost(int W, int want_q, cq_counter *out)
{
    cq_counter u, s, m;

    dr_ult_cost(W, &u);
    dr_sub_cost(W, &s);
    dr_mux_cost(W, &m);

    cq_count_reset(out);
    out->x   = u.x   + s.x   + m.x;
    out->cx  = u.cx  + s.cx  + m.cx + 1u + (want_q ? 1u : 0u);
    out->ccx = u.ccx + s.ccx + m.ccx;
}

/* ---- One case at an explicit mask, with the stream recorded. ------------- */

/* Runs one call at an explicit (value, quantum-mask) pair per operand, checks
 * the VALUE on the way through, and hands the mock back holding the stream. A
 * palindrome case that never looked at the answer would happily pin the gate
 * profile of a wrong circuit. */
static void dr_run_masked(const cq_kd_spec *k, int W, cq_ref_w va, cq_ref_w qa,
                          cq_ref_w vb, cq_ref_w qb, cq_mock *m)
{
    cq_ctx ctx;
    cq_sink s = cq_mock_sink(m);
    cq_kd_shape sh;
    cq_ref_w v[CQ_KD_MAX_SRC], want, got;

    cq_kd_default_shape(W, &sh);
    va = cq_ref_w_make(va.lo, va.hi, W);
    vb = cq_ref_w_make(vb.lo, vb.hi, W);
    v[0] = va;
    v[1] = vb;
    v[2] = cq_ref_w_zero();
    want = k->refn(v, &sh);

    cq_mock_reset(m);
    cq_ctx_init(&ctx, &s);

    int32_t ha = cq_bk_reg_w(&ctx, (uint32_t)W, va, qa);
    int32_t hb = cq_bk_reg_w(&ctx, (uint32_t)W, vb, qb);
    int32_t hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);

    cq_mock_reset(m);                        /* drop the materialising Xs */
    k->kernel(&ctx, cq_reg_bits(&ctx.regs, hd), cq_reg_cbits(&ctx.regs, ha),
              cq_reg_cbits(&ctx.regs, hb), W);

    got = cq_pc_value_w(&ctx, hd);
    if (!cq_ref_w_eq(got, want))
        cq_h_fail(__FILE__, __LINE__,
                  "%s W=%d: dst = 0x%llx%016llx, want 0x%llx%016llx", k->name, W,
                  (unsigned long long)got.hi,  (unsigned long long)got.lo,
                  (unsigned long long)want.hi, (unsigned long long)want.lo);

    cq_ctx_dispose(&ctx);
}

/* Is operand bit `i` a CLASSICAL ZERO — the only kind that removes a gate? A
 * classical ONE removes none: `CX(ONE,t)` folds to `X(t)`, one gate for one
 * gate (K09.md §3.3.1, the rule that had to be corrected three times). */
static int dr_zero_bit(cq_ref_w v, cq_ref_w q, int i)
{
    return !cq_ref_w_bit(q, i) && !cq_ref_w_bit(v, i);
}

/* ---- The sweep: the shared constant budget. ----------------------------- */

/* THE BUDGET IS cq_kd_samples(), AND IT IS NO LONGER A GATE BUDGET (2026-08-20).
 *
 * K12 is the largest object in v1 — one i128 `udiv` is 557,696 gates over
 * 131,583 qubits — so this file used to divide a gate-emission budget by the
 * exact per-case cost to derive a case count, then stride the mask pool down to
 * fit it, then add a handful of random pairs at all-quantum because the floor
 * that kept every named mask left `cpp` at 1 and the budget bought only corners.
 * Three interacting knobs, each with its own way of drifting.
 *
 * All three are gone. cq_kd_sample_at draws cq_kd_samples() cases per width,
 * jointly over the mask pool and the value space, with the all-classical pair
 * (which IS L5), the all-quantum pair (which is what L4 pins) and the four value
 * corners taken first — inside the budget rather than on top of it. The
 * pathology the DR_WIDE_SAMPLES tail existed to fix cannot recur: it came from a
 * per-mask floor interacting with a gate budget, and there is no per-mask floor
 * any more. Every width prints its count, its pool and its seed. See
 * tests/support/kernelsweep.c.
 *
 * `b = 0` IS AN ORDINARY DRAWN VALUE HERE AND THAT IS DELIBERATE: D3 says
 * division by zero never traps and returns 2^W-1 / a, so it is an input rather
 * than a special case, and the all-zero value corner drives it at every width.
 *
 * DR_MAX_PAIRS SURVIVES because the two _sweep.inc files size their own
 * cq_bk_fixed_pairs buffers with it. */

enum { DR_MAX_PAIRS = 128 + 12 };

/* THE THREE DEAD PARAMETERS ARE GONE (2026-09-17, bd 16s), ON bd aei's
 * PRECEDENT RATHER THAN AGAINST IT. Until today the signature read
 * `dr_sweep(k, W, uint64_t per_case, int region, uint64_t budget)` with all
 * three `(void)`-cast here, and the comment kept them "on cq_kd_sweep_at's
 * precedent" — a precedent REVERSED on 2026-09-11 (bd aei), when that
 * function's dead `exhaustive` parameter was removed outright so that a
 * caller's stale belief is a COMPILE error rather than a `1` that does
 * nothing.
 *
 * WHAT WAS ARGUED FOR KEEPING THEM, AND WHY IT DID NOT SURVIVE MEASUREMENT.
 * bd 16s recorded the counter-argument and it is the better half of the case:
 * unlike a literal `1` meaning "exhaustive", each argument was a REAL
 * measurement — the callers spelled them udiv_per_case / sdiv_per_case and
 * cq_divrem_region / cq_sdivrem_region — so a reader of the call site learned
 * the per-case gate cost and the scratch region at that width. MEASURED
 * 2026-09-17, and the removal is better justified than that concession makes
 * it sound: nothing PRINTED them at those widths and dr_sweep discarded all
 * three, so the call site showed a number computed and thrown away. The two
 * halves survive in DIFFERENT places and neither of them was the call site.
 * udiv_per_case / sdiv_per_case survive at dr_i128_case / ds_i128_case, which
 * print them. The REGION was never carried by these call sites at all: each
 * suite already ASSERTS cq_divrem_region / cq_sdivrem_region against its
 * closed form at nine widths, in
 * the_sandwich_takes_its_scratch_and_gives_it_back, and M19/M20 call the same
 * function to size their scratch on every case. The one swept width that
 * assertion ladder omits is W = 5 — and at W = 5 the kernel evaluates it on
 * every sweep case anyway. Removing the parameters loses no measurement.
 *
 * `budget` DECIDED IT, AND IT WAS THE STRONGEST OF THE THREE. DR_GATE_BUDGET
 * and DS_GATE_BUDGET were 600000 and read at the call site as a cap on a sweep
 * that HAS no cap: the coverage knob has been cq_kd_samples() /
 * CQOPS_L1_SAMPLES at every width since 2026-08-20, and both sweep headers
 * already spend a paragraph disclaiming a cap their own call sites went on
 * spelling in capitals. That is bd aei's failure mode exactly —
 * test_kernel_cast.c claimed exhaustive coverage for three weeks after the
 * flag went dead, because no test reads a comment. Both enums were deleted
 * WITH the parameters: C does not warn on an unused enum constant, so an
 * orphan left behind would have been silent in both configurations.
 *
 * NOTHING THE SWEEP DOES MOVED — the same cq_kd_sample_at call, the same case
 * counts, the same per-width output, the same ctest entries, and
 * tests/goldens/ untouched. */
static void dr_sweep(const cq_kd_spec *k, int W)
{
    clock_t t0 = clock();

    cq_kd_sample_at(k, W);
    printf("#   ^ %s W=%d took %.1f s\n", k->name, W,
           (double)(clock() - t0) / CLOCKS_PER_SEC);
    fflush(stdout);
}

/* The same at the narrow widths. Identical to dr_sweep now that the budget is
 * one constant at every width; kept as a separate name because both callers
 * use it and because the narrow/wide distinction still reads in their ladders.
 *
 * THAT FIRST SENTENCE IS TRUE, AND IS RE-MEASURED HERE (2026-09-17, bd 16s)
 * BECAUSE IT HAS NOW BEEN READ AS FALSE TWICE. cq_kd_sweep_at is a ONE-LINE
 * ALIAS of cq_kd_sample_at, and IT HAS NEVER HELD A WIDTH LADDER at any
 * revision. The ladder belongs to cq_kd_sweep — the neighbouring function
 * WITHOUT the `_at`, which nothing in either divrem suite calls — so a
 * citation spanning both function bodies reads as though dr_sweep_small sweeps
 * a ladder while dr_sweep does not. It does not: the two differ only in that
 * dr_sweep reaches cq_kd_sample_at directly and dr_sweep_small reaches it
 * through the alias. Check it against cq_kd_sweep_at's own BODY, never the
 * lines after it.
 *
 * AND THE HISTORY CLAUSE THAT USED TO BE IN THIS PARAGRAPH WAS ITSELF
 * UNMEASURED — the src/angle.h shape exactly, a shipped comment asserting a
 * history nobody checked, in the one paragraph whose whole job is to stop the
 * next reader getting this history wrong. It read "before bd aei it was a
 * three-way dispatch on `exhaustive` rather than a loop". MEASURED
 * 2026-09-17: the dispatch (sweep_full_cross / sweep_values / sweep_sampled)
 * is the body at e4a7a24, 5b18ae4 and e628ed0 ONLY. 5b57e4f replaced it with
 * `(void)exhaustive; cq_kd_sample_at(k, W);` on 2026-08-21, and that one-liner
 * is still the body at b4a4657^ — so for the three weeks before bd aei it was
 * already the alias plus a dead cast. bd aei removed the PARAMETER, not the
 * dispatch. Nothing the paragraph above rests on moves: no ladder, at any of
 * those four revisions or at HEAD.
 *
 * THE COST bd 16s ACCEPTED, RECORDED RATHER THAN ARGUED AWAY (F1 of its review
 * round, filed as bd cmu). Until 2026-09-17 these two names differed in ARITY,
 * so putting dr_sweep_small on a wide width — or dr_sweep on a narrow one —
 * was a COMPILE error. They are now the same signature with provably identical
 * behaviour, and MEASURED in review, substituting one for the other builds
 * clean with no warning and prints byte-identical stdout. bd 16s removed a dead
 * parameter on the principle that a dead thing carrying a live promise must
 * break a BUILD rather than a comment, and took the only compile-time
 * discriminator between this pair with it. INERT while cq_kd_sweep_at has no
 * behaviour of its own; live the moment it regains any, which it HAD as
 * recently as 5b57e4f. Not merged and not reversed — the pair carries the
 * narrow/wide distinction for a reader, and the remedy if it ever bites is to
 * give cq_kd_sweep_at behaviour or the pair a type, never to re-add a
 * parameter nothing reads. */
static void dr_sweep_small(const cq_kd_spec *k, int W)
{
    clock_t t0 = clock();

    cq_kd_sweep_at(k, W);
    printf("#   ^ %s W=%d took %.1f s\n", k->name, W,
           (double)(clock() - t0) / CLOCKS_PER_SEC);
    fflush(stdout);
}


/* ---- R9: the all-classical short-circuit. ------------------------------- */

/* NOT AN OPTIMISATION, AND THIS KERNEL HAS THE MOST TO LOSE OF ANY IN THE
 * CATALOGUE. Pre-materialisation is unconditional once cq_sandwich is entered,
 * so a fully classical divide without the fold would draw 8W^2+4W-1 qubits —
 * 33,023 at i64 and 131,583 at i128 — for an operation with no quantum input at
 * all (plan §0.2 consequence 2, K12.md §4).
 *
 * `minted` is the instrument, not `live`: a sandwich that took scratch and gave
 * it back leaves `live` exactly where it found it, and only a monotone counter
 * records that it was ever entered. */
static void dr_classical_case(const cq_kd_spec *k, int W, uint64_t va,
                              uint64_t vb, uint64_t want)
{
    cq_ctx ctx;
    cq_counter cnt;
    cq_sink sink;

    cq_count_reset(&cnt);
    sink = cq_sink_counter(&cnt);
    cq_ctx_init(&ctx, &sink);

    int32_t ha = cq_bk_reg_w(&ctx, (uint32_t)W, cq_ref_w_make(va, 0u, W),
                             cq_ref_w_zero());
    int32_t hb = cq_bk_reg_w(&ctx, (uint32_t)W, cq_ref_w_make(vb, 0u, W),
                             cq_ref_w_zero());
    int32_t hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);

    cq_pc_snap before = cq_pc_take(&ctx);
    cq_count_reset(&cnt);

    k->kernel(&ctx, cq_reg_bits(&ctx.regs, hd), cq_reg_cbits(&ctx.regs, ha),
              cq_reg_cbits(&ctx.regs, hb), W);

    cq_pc_snap after = cq_pc_take(&ctx);

    CHECK_EQ(cq_count_total(&cnt), 0);
    CHECK_EQ(after.minted, before.minted);
    CHECK_EQ(cq_pc_value(&ctx, hd), want);

    cq_ctx_dispose(&ctx);
}
