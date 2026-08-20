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

/* ---- The budgeted sweep. ------------------------------------------------ */

enum { DR_MAX_PAIRS = 128 + 12, DR_NAMED_PAIRS = 12, DR_WIDE_SAMPLES = 4 };

/* How many cases the budget buys, never fewer than the named mask pairs. */
static int dr_cases(uint64_t per_case, uint64_t budget)
{
    uint64_t n = budget / (per_case ? per_case : 1u);

    return n < (uint64_t)DR_NAMED_PAIRS ? DR_NAMED_PAIRS : (int)n;
}

/* As many distinct mask pairs as the budget has cases for, named rows first.
 * NO NAMED PAIR IS EVER DROPPED: all-classical (which IS L5), all-quantum
 * (which is what L4 pins), alternating, lsb-only, msb-only and risk R8's six
 * asymmetric rows run at every width; only the one-bit-quantum sweep rows are
 * strided, and the top lane is kept whatever the stride landed on, because an
 * off-by-one at the MSB is one of the two faults a sweep exists to catch. */
static uint32_t dr_pairs(int W, cq_bk_pair *out, int keep_n)
{
    uint32_t np = cq_bk_fixed_pairs((uint32_t)W, out, DR_MAX_PAIRS);
    uint32_t keep = (uint32_t)keep_n, swept, stride, n;

    if (np <= keep) return np;

    swept  = keep - DR_NAMED_PAIRS;
    stride = (np - DR_NAMED_PAIRS + swept) / (swept ? swept : 1u);
    n = DR_NAMED_PAIRS;
    for (uint32_t i = 0; i + 1u < np - DR_NAMED_PAIRS && n + 1u < keep; i += stride)
        out[n++] = out[DR_NAMED_PAIRS + i];

    out[n++] = out[np - 1u];
    return n;
}

/* Above the widths the shared driver can afford. The four value corners come
 * first — 0 and all-ones are where a masking bug lives, and uniform sampling
 * reaches them with probability ~0 at W = 64 — ROTATED BY PAIR INDEX so that
 * when the budget allows fewer than four cases per mask the corners are still
 * spread across the mask set instead of every pair seeing the same one.
 *
 * `b = 0` IS ONE OF THOSE CORNERS AND THAT IS DELIBERATE: D3 says division by
 * zero never traps and returns 2^W-1 / a, so it is an ordinary input here
 * rather than a special case, and the sweep drives it at every width. */
static void dr_sweep(const cq_kd_spec *k, int W, uint64_t per_case, int region,
                     uint64_t budget)
{
    cq_bk_pair pairs[DR_MAX_PAIRS];
    int n_cases = dr_cases(per_case, budget);
    uint32_t np = dr_pairs(W, pairs, n_cases);
    int cpp = n_cases / (int)np;
    cq_ref_w ones = cq_ref_w_ones(W);
    cq_ref_w zero = cq_ref_w_zero();
    cq_bk_rng rng;
    uint64_t cases = 0;
    clock_t t0 = clock();

    if (cpp < 1) cpp = 1;
    cq_bk_rng_init(&rng, 0xD1FDE13Aull ^ (uint64_t)W);

    for (uint32_t p = 0; p < np; p++)
        for (int i = 0; i < cpp; i++) {
            cq_ref_w v[CQ_KD_MAX_SRC];

            if (i < 4) {
                int c = (int)((p + (uint32_t)i) & 3u);

                v[0] = (c & 1) ? ones : zero;
                v[1] = (c & 2) ? ones : zero;
            } else {
                v[0] = cq_ref_w_make(cq_bk_rng_next(&rng), cq_bk_rng_next(&rng), W);
                v[1] = cq_ref_w_make(cq_bk_rng_next(&rng), cq_bk_rng_next(&rng), W);
            }
            v[2] = cq_ref_w_zero();
            cq_kd_case(k, W, v, &pairs[p]);
            cases++;
        }

    /* THE FLOOR THAT KEEPS EVERY NAMED MASK CAN LEAVE `cpp` AT 1, AND THEN THE
     * BUDGET BUYS ONLY CORNERS — measured at W = 32 and 64, where the loop above
     * runs 13 masks x 1 case and every one of those cases is drawn from
     * {0, all-ones} x {0, all-ones}. So at the two widest sweep widths NOTHING
     * NON-TRIVIAL IS EVER DIVIDED: `0/0`, `-1/-1`, `0/-1` and `-1/0` are all D3
     * or degenerate. The corners are worth keeping — a masking bug lives there —
     * but they are not a division test.
     *
     * The fix is targeted rather than a bigger budget: a handful of seeded
     * random pairs at the ALL-QUANTUM mask, which is the mask L4 pins and the
     * one where every lane is live. Four cases at W = 64 is 1.1M gate emissions
     * — cheap next to the corner block's 3.6M — and it is the only place the
     * kernel is asked to divide a real number above W = 16. */
    /* Pair 1 is all-quantum by construction in cq_bk_fixed_pairs. Asserted
     * rather than assumed, and BEFORE it is used, for kernelsweep.c's reason:
     * it is the mask L4 pins, so a reordering there must not move it silently. */
    if (!cq_ref_w_eq(pairs[1].q[0], cq_ref_w_ones(W)))
        cq_h_fail(__FILE__, __LINE__, "fixed pair 1 is [%s], not all-quantum",
                  pairs[1].name);

    for (int s = 0; s < DR_WIDE_SAMPLES; s++) {
        cq_ref_w v[CQ_KD_MAX_SRC];

        v[0] = cq_ref_w_make(cq_bk_rng_next(&rng), cq_bk_rng_next(&rng), W);
        v[1] = cq_ref_w_make(cq_bk_rng_next(&rng), cq_bk_rng_next(&rng), W);
        v[2] = cq_ref_w_zero();
        cq_kd_case(k, W, v, &pairs[1]);
        cases++;
    }

    printf("# %s W=%3d sampled: %u of %u mask pairs (12 named ALWAYS + a strided "
           "one-bit sweep) x %d cases each, plus %d random pairs at all-quantum "
           "= %llu cases, %llu gate emissions against a %llu budget (seed "
           "0xD1FDE13A^W) — %.1f s. One case is %llu gates over %d scratch "
           "qubits\n",
           k->name, W, np, (uint32_t)W + 12u, cpp, DR_WIDE_SAMPLES,
           (unsigned long long)cases,
           (unsigned long long)(cases * per_case), (unsigned long long)budget,
           (double)(clock() - t0) / CLOCKS_PER_SEC,
           (unsigned long long)per_case, region);
    fflush(stdout);
}

/* The shared driver at the exhaustive widths, timed the same way, so the cheap
 * widths are on the record next to the capped ones. */
static void dr_sweep_small(const cq_kd_spec *k, int W)
{
    clock_t t0 = clock();

    cq_kd_sweep_at(k, W, 1);
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
