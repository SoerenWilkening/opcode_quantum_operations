/* tests/test_kernel_fpround_golden.inc — L4, and the sibling that reads no
 * golden. Included by test_kernel_fpround.c.
 *
 * L4 IS PINNED PER (BLOCK, CALLER-SHAPED OPERAND MASK), WHICH IS NEW AT M32
 * (D-K23-7). M31's views fold identically at every operand mask within one
 * kernel; M32's do not, because `fsqrt.jl:103` passes the literal `UInt64(0)`
 * for `result_sign` where the other four callers pass a computed span, and
 * `flog.jl:269` / `fpconv.jl:75` / `fpow.jl:758` pass the literal `Int64(1)`
 * for `e`. A constant argument makes `result_sign << 63` an all-CQ_BIT_ZERO
 * view, so every `or` reading it folds, and it collapses every `sub` in a CLZ
 * ladder. SLOTS are unaffected; gates are not. So two rows per block:
 * `fwd@allq` and `fwd@caller`.
 *
 * THERE IS NO `unc` ROW, AND THAT IS NOT AN OMISSION. Rule 14's forward/`_unc`
 * asymmetry is about a KERNEL whose sources may have been re-materialised
 * between the two passes. M32 ships no kernel: a block's reverse half is the
 * same step indices replayed immediately, over the same operands, with nothing
 * in between — which the palindrome case asserts directly and exactly, gate
 * for gate, rather than as a count.
 *
 * AND A GOLDEN CANNOT BE THE ACCEPTANCE GATE ON ITS OWN. `CQOPS_UPDATE_GOLDENS
 * =1` is the documented way to make a red L4 green, so it blesses a narrowing
 * as readily as a fix. The sibling below reads no golden, and neither does the
 * composition identity, the slot scan or the span scan.
 */

static void fpr_cost(cq_fpround_id id, const int *q, cq_counter *out)
{
    cq_counter cnt;
    cq_sink s;
    uint64_t v[3];

    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);
    (void)fpr_anchor_at(id, 0, v);
    fpr_open(&FX, id, v, q, 0u, &s);
    cq_count_reset(&cnt);                  /* drop the materialising Xs */
    fpr_forward(&FX);
    *out = cnt;
    fpr_close(&FX);
}

static const int FPR_Q_ALL[3]    = { 1, 1, 1 };
static const int FPR_Q_CALLER[3] = { 1, 0, 0 };

CQ_TEST(l4_goldens)
{
    cq_gold g;

    if (!cq_gold_open(&g, CQOPS_GOLDEN_DIR "/fpround.counts",
                      "M32 kernels/fpround*.c — K23, the four shared softfloat "
                      "helpers as step blocks (COMPUTE HALF, no copy-out)",
                      "PER CALLER-SHAPED MASK (D-K23-7): `fwd@allq` is every "
                      "input all-quantum; `fwd@caller` is `wr`/`m` quantum with "
                      "the exponent and the sign CLASSICAL, which is fsqrt's "
                      "and flog's own call shape. W is 64 (f64 only)",
                      CQOPS_BENNETT_COMMIT))
        return;

    g.notes =
        "# M32 ships NO Rule 7 kernel, so these are ONE compute half each and\n"
        "# there is no copy-out and no `unc` row: a block's reverse half is the\n"
        "# same indices replayed over the same operands, which the palindrome\n"
        "# case asserts gate for gate rather than as a count.\n"
        "# The counts are FAR below the slot counts — 41 of K23's 154 rows are\n"
        "# VIEWS and every constant is a `const cq_bit` SOURCE (PRD-v2 §7.3),\n"
        "# so the fold table elides gates at EVERY mask including all-quantum.\n";

    for (int r = 0; r < N_ROWS; r++) {
        cq_counter a, c;

        fpr_cost(ROWS[r].id, FPR_Q_ALL,    &a);
        fpr_cost(ROWS[r].id, FPR_Q_CALLER, &c);

        CHECK_EQ(a.ry + a.rz + a.mz, 0);
        CHECK_EQ(c.ry + c.rz + c.mz, 0);

        cq_gold_check(&g, ROWS[r].name, "fwd@allq",   W64, a.x, a.cx, a.ccx);
        cq_gold_check(&g, ROWS[r].name, "fwd@caller", W64, c.x, c.cx, c.ccx);
    }

    CHECK(cq_gold_close(&g));
}

/* ---- The sibling that reads no golden. ---------------------------------- */

/* D-K23-7's OWN CLAIM, EXECUTED. A classical ZERO operand bit removes gates
 * and a classical ONE only demotes them, so the caller-shaped mask costs
 * strictly LESS than all-quantum wherever the constant it substitutes has a
 * zero lane — which `result_sign = 0` and `e = 1` both do, in 63 lanes. This
 * is what makes "the same block costs different gates in different callers" a
 * measurement rather than an assertion, and it goes red if the two masks ever
 * collapse onto one number — which is exactly what a port that MATERIALISED
 * its constants (upstream's `resolve!` shape) would do.
 *
 * AND SLOTS ARE UNAFFECTED BY THE MASK, which is the other half: the same
 * `cq_*_steps()` drives both runs, so a mask-dependent slot count would be a
 * different defect and is pinned here too. Rule 4's alphabet is asserted in
 * the same breath — every gate M32 emits is an X, a CX or a CCX. */
CQ_TEST(the_caller_shaped_mask_is_cheaper_and_the_alphabet_is_x_cx_ccx)
{
    for (int r = 0; r < N_ROWS; r++) {
        cq_counter a, c;
        uint64_t ta, tc;

        fpr_cost(ROWS[r].id, FPR_Q_ALL,    &a);
        fpr_cost(ROWS[r].id, FPR_Q_CALLER, &c);
        ta = cq_count_total(&a);
        tc = cq_count_total(&c);

        CHECK_EQ(a.ry + a.rz + a.mz, 0);      /* Rule 4: X, CX, CCX only */
        CHECK_EQ(c.ry + c.rz + c.mz, 0);
        CHECK(ta > 0u);
        CHECK(tc < ta);                       /* D-K23-7, measured        */

        /* SLOTS AND GATES COME APART AND THE SLOT COUNT IS MASK-BLIND. */
        CHECK(ta < (uint64_t)fpr_steps_of(ROWS[r].id));

        printf("# L4 %-8s slots %5d  allq (x %llu, cx %llu, ccx %llu) = %llu"
               "  caller (x %llu, cx %llu, ccx %llu) = %llu  scratch %u\n",
               ROWS[r].name, fpr_steps_of(ROWS[r].id),
               (unsigned long long)a.x, (unsigned long long)a.cx,
               (unsigned long long)a.ccx, (unsigned long long)ta,
               (unsigned long long)c.x, (unsigned long long)c.cx,
               (unsigned long long)c.ccx, (unsigned long long)tc,
               fpr_region_of(ROWS[r].id));
    }
    fflush(stdout);
}
