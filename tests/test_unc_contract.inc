/* tests/test_unc_contract.inc — Step 21, group 1: Rule 7's contract along the
 * axis. Included by tests/test_unc.c, which carries the fixture, `ux_read` and
 * the catalogue these cases drive.
 *
 * THE SPLIT SEAM, RECORDED HERE BECAUSE THIS FILE IS NEW AND plan §3 HAS NO
 * ROW FOR IT: `the CONTRACT` against `the CONSEQUENCES`. What `dst ^= f(a,b)`
 * means when dst is not zero, and which kernels satisfy it, is this file; what
 * the axis then costs — the free, the interaction with §9's regions, the
 * record in the goldens, and the R6 asymmetry — is the .c and
 * test_unc_asym.inc. tests/test_unc.c crossed 300 the moment all three groups
 * were in it (375 of 300), which is a scheduled split rather than a surprise.
 *
 * WHY THESE THREE BELONG TOGETHER. Each says something about the SHAPE of an
 * uncompute rather than about its cost:
 *
 *   - a kernel XORS into dst, so `_unc` is the same call with dst = out and
 *     the postcondition is "dst's ENTRY value", of which zero is a special
 *     case nothing else in this project had ever left;
 *   - a compare flag is self-inverse at any entry value, which is the property
 *     CQ_lang's compare `_unc` strip rests on (PRD §15 D14);
 *   - K8 is the one kernel for which none of that holds, and its exclusion
 *     from the axis is a claim rather than an omission.
 */

/* =========================================================================
 * GROUP 1 — `dst ^= f(a, b)`, at a dst that is not zero.
 * ========================================================================= */

/* Rule 7's semantics are an XOR INTO dst, and the two ends of the axis are the
 * two special cases of it: the forward has `dst = 0` so `0 ^ f = f`, and the
 * uncompute has `dst = f` so `f ^ f = 0`. Nothing in this project had ever
 * supplied a third value, because cq_kd_case mints dst with
 * cq_reg_alloc_zero. A dst lane holding CQ_BIT_ONE is reached only this way AT
 * ENTRY — it arises mid-kernel today, and CLAUDE.md names the witness
 * (`cq_kernel_xor` with a classical ONE source) — and it is a real fold-table
 * row: cq_emit_x on a constant ONE target rewrites the constant for zero
 * gates, and cq_emit_cx/ccx into one materialises it with an `X` first. */
CQ_TEST(a_kernel_xors_into_dst_rather_than_assigning_to_it)
{
    static const struct { uint64_t d0, dq; const char *what; } START[] = {
        { 0x0u, 0x0u, "all-ZERO   (what every other suite uses)" },
        { 0xFu, 0x0u, "all-ONE    classical" },
        { 0x5u, 0x0u, "mixed      classical" },
        { 0x5u, 0xFu, "all-quantum, value 5" },
        { 0x9u, 0x6u, "mixed kinds and mixed value" },
    };
    const int W = 4;
    const uint64_t va = 0xBu, vb = 0x6u;

    for (int ki = 0; ki < UX_N_K; ki++) {
        const ux_kernel *k = &UX_K[ki];

        for (unsigned s = 0; s < sizeof START / sizeof START[0]; s++) {
            ux_fix f;
            int wd = ux_wdst(k, W);
            uint64_t d0 = START[s].d0 & cq_ref_mask(wd);
            uint64_t dq = START[s].dq & cq_ref_mask(wd);
            uint64_t got;

            ux_open(&f);
            int32_t ha = cq_bk_reg(&f.ctx, (uint32_t)W, va, ~0ull);
            int32_t hb = cq_bk_reg(&f.ctx, (uint32_t)W, vb, ~0ull);
            int32_t hd = cq_bk_reg(&f.ctx, (uint32_t)wd, d0, dq);

            k->fn(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
                  cq_reg_cbits(&f.ctx.regs, ha),
                  cq_reg_cbits(&f.ctx.regs, hb), W);

            uint64_t fv = ux_ref(k, va, vb, W);
            uint64_t want = (d0 ^ fv) & cq_ref_mask(wd);
            CHECK(ux_read(&f.ctx, hd, &got));
            if (got != want)
                cq_h_fail(__FILE__, __LINE__,
                          "%s W=%d dst starting %s: got 0x%llx, want "
                          "dst0 ^ f = 0x%llx ^ 0x%llx", k->name, W,
                          START[s].what, (unsigned long long)got,
                          (unsigned long long)d0, (unsigned long long)fv);

            /* AND THE UNCOMPUTE RETURNS dst TO ITS ENTRY VALUE, NOT TO ZERO.
             * That is the postcondition stated exactly: zero is what the
             * forward's fresh rail happens to make it. */
            k->fn(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
                  cq_reg_cbits(&f.ctx.regs, ha),
                  cq_reg_cbits(&f.ctx.regs, hb), W);
            CHECK(ux_read(&f.ctx, hd, &got));
            if (got != d0)
                cq_h_fail(__FILE__, __LINE__,
                          "%s W=%d dst starting %s: uncompute left 0x%llx, "
                          "want the ENTRY value 0x%llx", k->name, W,
                          START[s].what, (unsigned long long)got,
                          (unsigned long long)d0);
            ux_close(&f);
        }
    }
}

/* The ten predicates, at a flag rail that already holds 1.
 *
 * THIS IS WHAT THE `_inv` LINE OF plan §4's STEP 21 ROW IS ACTUALLY ABOUT, and
 * the symbol it names is dead — see PRD §15 D14. CQ_lang's compare flag strip
 * is the VOID in-place `_unc(rail, srcs…)`; the fresh-minting `_inv` twin was
 * removed (CompareLowering.cpp:204-236, bd 8txa) precisely because "a fresh
 * mint frees a dirty rail = a hidden measurement, Rule 6/8". Zero `_inv` calls
 * appear in the 239 pinned goldens. What survives, and is load-bearing, is the
 * PROPERTY the removed symbol rested on and the surviving one still does:
 * `flag ^= pred(a,b)` is a self-inverse permutation of the flag rail at ANY
 * entry value (third_party/cq_lang/opcode_table.yaml:216-218). L3 only ever
 * checks it from 0. */
CQ_TEST(every_compare_predicate_is_self_inverse_at_a_flag_that_is_already_one)
{
    static const struct { const char *name; ux_fn fn; int pred; } P[] = {
        { "eq",  cq_kernel_eq,  CQ_ICMP_EQ  }, { "ne",  cq_kernel_ne,  CQ_ICMP_NE  },
        { "ult", cq_kernel_ult, CQ_ICMP_ULT }, { "ugt", cq_kernel_ugt, CQ_ICMP_UGT },
        { "ule", cq_kernel_ule, CQ_ICMP_ULE }, { "uge", cq_kernel_uge, CQ_ICMP_UGE },
        { "slt", cq_kernel_slt, CQ_ICMP_SLT }, { "sgt", cq_kernel_sgt, CQ_ICMP_SGT },
        { "sle", cq_kernel_sle, CQ_ICMP_SLE }, { "sge", cq_kernel_sge, CQ_ICMP_SGE },
    };
    static const uint64_t V[] = { 0x0u, 0x3u, 0x9u, 0xFu };
    const int W = 4;

    for (unsigned p = 0; p < sizeof P / sizeof P[0]; p++) {
        int moved = 0;

        for (unsigned i = 0; i < sizeof V / sizeof V[0]; i++)
            for (unsigned j = 0; j < sizeof V / sizeof V[0]; j++)
                for (int start = 0; start <= 1; start++) {
                    ux_fix f;
                    uint64_t got, mid;
                    uint64_t pred = (uint64_t)cq_ref_icmp(
                        (cq_icmp_pred)P[p].pred,
                        cq_ref_w_make(V[i], 0u, W),
                        cq_ref_w_make(V[j], 0u, W), W);

                    ux_open(&f);
                    int32_t ha = cq_bk_reg(&f.ctx, (uint32_t)W, V[i], ~0ull);
                    int32_t hb = cq_bk_reg(&f.ctx, (uint32_t)W, V[j], ~0ull);
                    int32_t hf = cq_bk_reg(&f.ctx, 1u, (uint64_t)start, 1u);

                    P[p].fn(&f.ctx, cq_reg_bits(&f.ctx.regs, hf),
                            cq_reg_cbits(&f.ctx.regs, ha),
                            cq_reg_cbits(&f.ctx.regs, hb), W);
                    CHECK(ux_read(&f.ctx, hf, &mid));

                    /* THE `^= pred` HALF, WHICH IS THE HALF THE INVOLUTION
                     * CANNOT SEE. Without this line the only assertion is
                     * `got == start`, and EVERY involution on a one-bit rail
                     * satisfies that — including a predicate that emits
                     * nothing, two predicates exchanged, or `!pred`. Five of
                     * these ten appear nowhere else in this suite. */
                    if (mid != ((uint64_t)start ^ pred))
                        cq_h_fail(__FILE__, __LINE__,
                                  "%s(0x%llx, 0x%llx) from flag=%d: flag went "
                                  "to %llu, want %d ^ %llu", P[p].name,
                                  (unsigned long long)V[i],
                                  (unsigned long long)V[j], start,
                                  (unsigned long long)mid, start,
                                  (unsigned long long)pred);
                    if (mid != (uint64_t)start) moved++;

                    P[p].fn(&f.ctx, cq_reg_bits(&f.ctx.regs, hf),
                            cq_reg_cbits(&f.ctx.regs, ha),
                            cq_reg_cbits(&f.ctx.regs, hb), W);
                    CHECK(ux_read(&f.ctx, hf, &got));

                    if (got != (uint64_t)start)
                        cq_h_fail(__FILE__, __LINE__,
                                  "%s(0x%llx, 0x%llx) from flag=%d: went to "
                                  "%llu and back to %llu, want %d",
                                  P[p].name, (unsigned long long)V[i],
                                  (unsigned long long)V[j], start,
                                  (unsigned long long)mid,
                                  (unsigned long long)got, start);
                    ux_close(&f);
                }

        /* NON-VACUITY PER PREDICATE: "self-inverse" proved by "the flag never
         * moved" is the vacuous half, and V[] is small enough that a predicate
         * false on all sixteen pairs is a real possibility to guard against. */
        if (!moved)
            cq_h_fail(__FILE__, __LINE__,
                      "%s never moved the flag over the whole operand table, "
                      "so its self-inverse claim is vacuous here", P[p].name);
    }
}

/* K8 IS THE ONE KERNEL THIS AXIS DOES NOT REACH, AND THAT IS A CLAIM RATHER
 * THAN AN OMISSION. Rule 7 and tests/support/kerneldrv.h both state it in
 * prose — "a second call gives `acc + 2b`", which is why K8 may never be
 * substituted for K6/K7: the forward value would be right and only the `_unc`
 * wrong, a silent miscompile rather than a test failure. Nothing ran it. This
 * does, and it is the negative control for the whole axis: `_unc` IS the same
 * kernel again for every OTHER kernel in the catalogue precisely because their
 * dst is written by XOR, and K8's is not. */
CQ_TEST(an_accumulate_is_not_its_own_uncompute)
{
    /* W = 1 IS ITS OWN ROW, AND IT IS THE OPPOSITE ROW. K8's W=1 path is a
     * re-derivation rather than a port (K08.md §5 D1): one CX,
     * `acc[0] ^= b[0]`, no ancilla — which IS self-inverse. So at W=1 a second
     * call DOES return acc to its entry value, and this case's whole claim is
     * false there. That is not a defect and the exclusion still stands: K11
     * delegates W=1 to K2 (mul.c), so no composite ever reaches K8 at that
     * width. Asserting the opposite explicitly is what stops the next reader
     * from "extending" the loop down to 1 and finding the suite red. The same
     * shape as K8's extent defence being inert at W=2, which CLAUDE.md
     * records. */
    {
        ux_fix f;
        uint64_t got;
        const uint64_t acc0 = 1u, b0 = 1u;

        ux_open(&f);
        int32_t ha = cq_bk_reg(&f.ctx, 1u, acc0, ~0ull);
        int32_t hb = cq_bk_reg(&f.ctx, 1u, b0,   ~0ull);
        int32_t hx = cq_bk_reg(&f.ctx, 1u, 0u, 1u);
        cq_addacc_block k1 = { cq_reg_bits(&f.ctx.regs, ha),
                               cq_reg_bits(&f.ctx.regs, hb),
                               cq_reg_bits(&f.ctx.regs, hx), 1 };

        cq_kernel_addacc(&f.ctx, &k1);
        cq_kernel_addacc(&f.ctx, &k1);
        CHECK(ux_read(&f.ctx, ha, &got));
        if (got != acc0)
            cq_h_fail(__FILE__, __LINE__,
                      "K8 at W=1 is ONE CX and is therefore its own inverse; a "
                      "second call left acc at %llu, not %llu",
                      (unsigned long long)got, (unsigned long long)acc0);
        ux_close(&f);
    }

    for (int W = 2; W <= 5; W++)
        for (uint64_t acc0 = 0u; acc0 < 4u; acc0++) {
            ux_fix f;
            uint64_t got;
            const uint64_t b0 = 3u;
            const uint64_t m = cq_ref_mask(W);

            /* THE FIXTURE MUST DISCRIMINATE, and this is the assertion that
             * says so rather than a guard that cannot fire: an earlier draft
             * tested `got == acc0` after pinning `got == acc0 + 2b0`, which is
             * arithmetically unreachable for every (W >= 2, b0 = 3). What has
             * to be true is that `acc + 2b` and `acc` are DIFFERENT at this
             * width and addend — otherwise a K8 that were self-inverse would
             * satisfy the pin below. */
            CHECK(((acc0 + 2u * b0) & m) != (acc0 & m));

            ux_open(&f);
            int32_t ha = cq_bk_reg(&f.ctx, (uint32_t)W, acc0, ~0ull);
            int32_t hb = cq_bk_reg(&f.ctx, (uint32_t)W, b0,   ~0ull);
            int32_t hx = cq_bk_reg(&f.ctx, 1u, 0u, 1u);

            cq_addacc_block k = { cq_reg_bits(&f.ctx.regs, ha),
                                  cq_reg_bits(&f.ctx.regs, hb),
                                  cq_reg_bits(&f.ctx.regs, hx), W };

            cq_kernel_addacc(&f.ctx, &k);
            CHECK(ux_read(&f.ctx, ha, &got));
            CHECK_EQ(got, (acc0 + b0) & m);

            cq_kernel_addacc(&f.ctx, &k);
            CHECK(ux_read(&f.ctx, ha, &got));
            CHECK_EQ(got, (acc0 + 2u * b0) & m);
            ux_close(&f);
        }
}
