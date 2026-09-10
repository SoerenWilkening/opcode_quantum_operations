/* tests/test_kernel_mul_ceiling.inc — `bd fxz`: K11's scratch against D2's
 * pool ceiling. PRD §15 D25, and the POSITIVE half of it.
 *
 * WHAT IS BEING TESTED IS NOT M03. `test_qubits_death.ceiling_exceeded` has
 * pinned "an acquire past the ceiling aborts" since Step 4, on a bare pool with
 * four hand-written acquires. What no case anywhere covered is the claim `bd
 * fxz` actually asks for: that a KERNEL whose scratch region does not fit meets
 * that refusal — that D2's bound reaches K11's W² + 2W at all, rather than
 * being lifted, deferred or consulted only for a rail. M18's `mul` at i128 is
 * 16,640 scratch qubits (mul.c's header) and M19's `udiv` is 131,583, both past
 * any `qec_n_logical` the shipped configs carry (§15 D20), so this is the
 * boundary a real device meets first.
 *
 * THIS FILE HOLDS ONLY THE CASES THAT RETURN. The refusal itself is a death and
 * lives in tests/test_kernel_mul_death.c, where a case can be one process; the
 * two halves are a PAIR and neither is worth much alone. The death case proves
 * a region one qubit too large is refused; this one proves a region that FITS
 * is not — i.e. that the bound is tight and located, and not a kernel that has
 * simply stopped running under any ceiling at all.
 *
 * THE BUDGET IS MEASURED, NEVER WRITTEN DOWN. `mul_budget` runs the kernel once
 * on an unbounded pool and reads `peak`, so the cases below re-derive nothing:
 * K11.md's W² + 3W is asserted as a closed form ONE case up
 * (the_sandwich_takes_its_scratch_and_gives_it_back) and that is the right
 * place for it. Here the formula moving must not silently un-tighten the
 * boundary these cases sit on — the same reason the composition check asks M15
 * what an accumulate costs rather than writing 6W − 5.
 */

/* Two all-quantum W-bit operands and an all-zero `dst`, over a counting sink.
 *
 * VALUE ZERO, and it is load-bearing rather than lazy: `cq_bk_reg` materialises
 * a masked bit and emits an X only for a constant ONE, so a zero operand costs
 * the pool 2W qubits and the sink NOTHING — which is what lets the sibling
 * death case install a sink that aborts on any gate at all. The circuit is
 * unaffected: the §3 fold table branches on a bit's KIND and never on a qubit's
 * value (D6), so at the all-quantum mask every value pair emits the same
 * stream. */
static int32_t ceiling_fixture(cq_ctx *ctx, cq_sink *s, cq_counter *c, int W,
                               int32_t *ha, int32_t *hb)
{
    cq_count_reset(c);
    *s = cq_sink_counter(c);
    cq_ctx_init(ctx, s);

    *ha = cq_bk_reg(ctx, (uint32_t)W, 0u, cq_ref_mask(W));
    *hb = cq_bk_reg(ctx, (uint32_t)W, 0u, cq_ref_mask(W));
    return cq_reg_alloc_zero(&ctx->regs, (uint32_t)W);
}

/* How many qubits ONE `cq_kernel_mul` needs beyond what its operands already
 * hold, and how many of those are the scratch region. `peak == minted`
 * (src/qubits.h), so the high-water mark DURING the call is readable after it. */
static void mul_budget(int W, uint32_t *peak_delta, uint32_t *scratch)
{
    cq_ctx ctx; cq_sink s; cq_counter c;
    int32_t ha, hb;
    int32_t hd = ceiling_fixture(&ctx, &s, &c, W, &ha, &hb);

    uint32_t before = cq_qubits_live(&ctx.pool);

    cq_kernel_mul(&ctx, cq_reg_bits(&ctx.regs, hd),
                  cq_reg_cbits(&ctx.regs, ha),
                  cq_reg_cbits(&ctx.regs, hb), W);

    *peak_delta = cq_qubits_peak(&ctx.pool) - before;
    *scratch    = *peak_delta - cq_reg_owned_qubits(&ctx.regs, hd);
    cq_ctx_dispose(&ctx);
}

/* THE CEILING IS TIGHT, AND THE TIGHT VALUE IS THE PEAK — not the region, and
 * not what the rail ends up owning. `dst`'s lanes are materialised by the
 * COPYOUT, which runs while the whole scratch region is still live (step 3 of
 * plan §0.1), so a device that can hold W² + 2W but not W² + 3W does not run
 * this kernel. That is the number a caller has to size against and it is the
 * one this case pins as sufficient.
 *
 * IT ALSO PINS THAT THE CEILING WAS STILL SET WHEN THE KERNEL RETURNED. A
 * kernel that "handled" a tight ceiling by lifting it would pass every other
 * assertion here — right value, pool returned — and would have disarmed D2 for
 * everything downstream of it, which is exactly the softening `bd fxz` and §15
 * D20 forbid. */
CQ_TEST(a_ceiling_at_the_measured_peak_admits_the_kernel_and_is_not_lifted)
{
    static const int WS[] = { 2, 3, 4, 8 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        const int W = WS[i];
        uint32_t peak_delta = 0u, scratch = 0u;
        cq_ctx ctx; cq_sink s; cq_counter c;
        int32_t ha, hb;
        int32_t hd;

        mul_budget(W, &peak_delta, &scratch);
        CHECK_EQ(scratch, (uint32_t)(W * W + 2 * W));

        hd = ceiling_fixture(&ctx, &s, &c, W, &ha, &hb);
        cq_qubits_set_ceiling(&ctx.pool, cq_qubits_live(&ctx.pool) + peak_delta);

        cq_kernel_mul(&ctx, cq_reg_bits(&ctx.regs, hd),
                      cq_reg_cbits(&ctx.regs, ha),
                      cq_reg_cbits(&ctx.regs, hb), W);

        /* Right answer (0 · 0), scratch handed back, and the promise still in
         * force for the next caller. */
        CHECK_EQ(cq_pc_value(&ctx, hd), 0u);
        CHECK_EQ(cq_qubits_live(&ctx.pool), (uint32_t)(3 * W));
        CHECK_EQ(cq_qubits_ceiling(&ctx.pool),
                 (uint32_t)(2 * W) + peak_delta);
        cq_ctx_dispose(&ctx);
    }
}

/* AND R9 IS WHAT KEEPS A CEILING-BOUNDED DEVICE ABLE TO MULTIPLY AT ALL. L5's
 * short-circuit is stated everywhere as "zero gates, zero qubits"; stated in D2
 * terms it is stronger and is the reason i128 `mul` is not simply fatal — an
 * all-classical multiply runs under a ceiling that admits NOTHING NEW, at any
 * width, because it never enters `cq_sandwich` and so never asks the pool for
 * the W² + 2W the quantum path needs.
 *
 * `minted` is the instrument, not `live` (the sibling R9 case's argument): a
 * sandwich that took scratch and gave it back leaves `live` where it found it,
 * and only a monotone counter records that it was ever entered. Here the
 * ceiling would have caught it too — but `minted` says WHY it did not fire,
 * where the ceiling alone would leave "the fold ran" and "the region fitted"
 * indistinguishable.
 *
 * THE DEVICE HOLDS ONE UNRELATED QUBIT AND THE CEILING IS ONE, which is a
 * fixture detail with a trap in it: `cq_qubits_set_ceiling(p, 0)` LIFTS the
 * ceiling (D2 — 0 means unbounded), so on an empty pool "admits nothing new"
 * cannot be spelled at all and the obvious writing of this case would be
 * asserting nothing. */
CQ_TEST(an_all_classical_mul_runs_under_a_ceiling_that_admits_nothing_new)
{
    static const int WS[] = { 4, 8, 32 };

    for (size_t i = 0; i < sizeof WS / sizeof WS[0]; i++) {
        const int W = WS[i];
        cq_ctx ctx; cq_sink s; cq_counter c;
        int32_t hq, ha, hb, hd;

        cq_count_reset(&c);
        s = cq_sink_counter(&c);
        cq_ctx_init(&ctx, &s);

        hq = cq_bk_reg(&ctx, 1u, 0u, 1u);            /* the device's own data */
        /* Every bit a constant, so I4 gives all three rails zero qubits. */
        ha = cq_bk_reg(&ctx, (uint32_t)W, 0x0Du, 0u);
        hb = cq_bk_reg(&ctx, (uint32_t)W, 0x0Bu, 0u);
        hd = cq_reg_alloc_zero(&ctx.regs, (uint32_t)W);

        CHECK_EQ(cq_qubits_live(&ctx.pool), 1u);
        cq_qubits_set_ceiling(&ctx.pool, 1u);        /* nothing new, at all */

        cq_kernel_mul(&ctx, cq_reg_bits(&ctx.regs, hd),
                      cq_reg_cbits(&ctx.regs, ha),
                      cq_reg_cbits(&ctx.regs, hb), W);

        CHECK_EQ(cq_pc_value(&ctx, hd), (0x0Dull * 0x0Bull) & cq_ref_mask(W));
        CHECK_EQ(cq_qubits_minted(&ctx.pool), 1u);
        CHECK_EQ(cq_count_total(&c), 0u);
        CHECK_EQ(cq_reg_owned_qubits(&ctx.regs, hq), 1u);
        cq_ctx_dispose(&ctx);
    }
}
