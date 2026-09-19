/* tests/test_kernel_fconv_spans.inc — the LAYOUT's three instruments: the
 * span-disjointness scan, the palindrome at an asymmetric mask, and two
 * exported `fptosi` blocks in one region with a canary. Included by
 * test_kernel_fconv.c AFTER test_kernel_fconv_scan.inc, whose `fv_row_gates`,
 * `fv_sc`, `FV_SCAN_V` and `FV_REF_MAX` it reuses.
 *
 * SPLIT OFF THE SCAN ON A REAL SEAM, not on a line count. Everything next door
 * is about the SLOT SCHEDULE — how many slots a program has and which of
 * X / CX / CCX each one emits. Everything here is about the LAYOUT — which
 * bits each row owns, and whether a program stays inside them. M31's measured
 * finding is that the two come apart: collapsing two blocks onto one offset
 * kept the value, the palindrome, the pool, every gate count AND the slot
 * scan, so a suite with only the schedule half has a hole exactly the shape of
 * its scratch map. K19 has 74 rows in one flat region — including TWO COPIES
 * of one 31-row table, whose spans are the ones most likely to be aliased —
 * so the same hazard is here 74 ways.
 *
 * NOTHING BELOW READS A GOLDEN.
 */

/* The 64-lane value on a span, read out of the shadow. Determinate because
 * nothing in K19 rotates — the shadow is EXACT on the rotation-free surface. */
static uint64_t fv_wire_value(cq_ctx *ctx, const cq_bit *b, int *known)
{
    uint64_t v = 0u;

    *known = 1;
    for (int i = 0; i < CQ_FP64_W; i++) {
        int bit;

        if (cq_bit_is_const(b[i])) bit = cq_bit_value(b[i]);
        else {
            cq_shadow sv = cq_shadow_get(&ctx->shadow, cq_bit_qindex(b[i]));

            if (sv.unknown) { *known = 0; return 0u; }
            bit = (int)sv.value;
        }
        v |= (uint64_t)bit << (unsigned)i;
    }
    return v;
}

/* ---- (c) Every row's span is disjoint from every other row's. ---------- */

/* WRITTEN BECAUSE A MEASURED M31 MUTANT SURVIVED EVERYTHING ELSE. Collapsing
 * two `eq` blocks onto one offset left the VALUE right, the palindrome intact,
 * the pool restored, the slot scan green and every gate count identical — and
 * it was equivalent only by an accident of `lower_eq!` leaving its region all
 * zero exactly when its own flag is 0.
 *
 * IN K19 THE SAME MUTANT HAS A NAME: K19.md §2.3's box warns that giving
 * `fptoui`'s two `soft_fptosi` calls ONE region is "a silent miscompile whose
 * forward value is right on the `in_high_range` branch only". Inlining the
 * table twice makes that unrepresentable — each copy owns its own extent in
 * the prefix walk — and this case is what says so about the STREAM rather than
 * about the table.
 *
 * WHAT IS ASSERTED IS THE LAYOUT AND NOT THE ACCIDENT: no qubit index is
 * written by two different ROWS. It reads no golden and is derived from the
 * program table plus the per-row gate partition the scan already computes. */
enum { FV_MAX_Q = 262144 };

CQ_TEST(the_programs_spans_are_pairwise_disjoint)
{
    static int owner[FV_MAX_Q];
    static cq_op ref[FV_REF_MAX];
    cq_mock m;

    cq_mock_init(&m);

    for (int p = 0; p < N_FCONV_ROWS; p++) {
        cq_fconv_row rows[CQ_FCONV_MAX_ROWS];
        int F = (p == FV_UITOFP) ? 8 : CQ_FP64_W;
        int n = cq_fconv_program(ROWS[p].prog, rows);
        fv_sc c = { rows, n, F };
        cq_ref_w q = cq_ref_w_ones(F);
        size_t cursor = 0u;
        int touched = 0, bad = 0, seen = 0, emitting = 0;

        for (int t = 0; t < FV_MAX_Q; t++) owner[t] = -1;
        (void)fconv_run(&ROWS[p], F, FV_SCAN_V[p], q, &m);

        for (int i = 0; i < n && !bad; i++) {
            size_t g = fv_row_gates(&c, i, ref);

            if (fv_row_slots(&rows[i]) > 0) emitting++;
            if (g > 0u) seen++;

            for (size_t k = 0; k < g && !bad; k++) {
                uint32_t t;

                CHECK(cursor < cq_mock_count(&m));
                t = cq_mock_at(&m, cursor++)->t;
                CHECK(t < (uint32_t)FV_MAX_Q);
                touched++;

                if (owner[t] >= 0 && owner[t] != i) {
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d gate %zu: q%u is written by row %d AND "
                              "by row %d — two blocks of one program must not "
                              "share a bit", ROWS[p].spec.name, i, k, t,
                              owner[t], i);
                    bad = 1;
                    break;
                }
                owner[t] = i;
            }
        }

        /* NOT VACUOUS: EVERY emitting row must have written something, or a
         * disjointness claim over rows that never ran is no claim at all. The
         * view rows are wiring and are expected to contribute nothing, which
         * is why the count is over emitting rows rather than over all. */
        CHECK(touched > 0);
        CHECK_EQ(seen, emitting);
    }

    cq_mock_dispose(&m);
}

/* ---- (d) The palindrome, at an ASYMMETRIC mask. ------------------------ */

/* AN ALL-CLASSICAL MASK IS NOT A PALINDROME CASE, IT IS AN L5 CASE — the R9
 * short-circuit never enters cq_sandwich, so cq_mock_count is the copy-out
 * alone and the check is being asked about a stream that does not exist. K19's
 * kernels are UNARY, so risk R8's named `a` all Q / `b` all ZERO pair has no
 * second operand to make asymmetric; the asymmetry that IS available is the
 * one this module's own structure offers — a HALF-QUANTUM rail, where the low
 * lanes are wires and the high lanes are constants (and, for `uitofp`, the
 * zext view makes that the ordinary case rather than a contrivance).
 *
 * WHY THE HEAD LENGTH IS DERIVED RATHER THAN PREDICTED: at this mask the scan
 * would have to fold on the operand's bit-kinds as well as on the views, which
 * is a second transcription of the fold table rather than of the schedule.
 * `total = 2*head + 64` with a 64-gate copy-out is a structural claim in its
 * own right, and the ordered comparison then has teeth that no gate COUNT has
 * (risk R8: a reversed forward list can have a different multiset with the
 * identical total). */
CQ_TEST(the_compute_half_is_a_palindrome_at_an_asymmetric_mask)
{
    cq_mock m;

    cq_mock_init(&m);

    for (int p = 0; p < N_FCONV_ROWS; p++)
        for (int side = 0; side < 2; side++) {
            int F = (p == FV_UITOFP) ? 16 : CQ_FP64_W;
            /* The low half quantum and the high half classical, and its
             * mirror: for a conversion the two halves are the MANTISSA and
             * the EXPONENT-AND-SIGN, which reach entirely different rows, so a
             * fold that fired on one of them would hide in one order. */
            uint64_t lo = (F >= 64) ? UINT64_C(0xFFFFFFFF)
                                    : (fv_ones(F) >> 1);
            cq_ref_w mask = cq_ref_w_make(side == 0 ? lo : (fv_ones(F) ^ lo), 0u, F);
            size_t total, head;
            uint64_t want, got;

            got  = fconv_run(&ROWS[p], F, FV_SCAN_V[p], mask, &m);
            want = ROWS[p].host(FV_SCAN_V[p], F);
            CHECK_EQ(got, want);

            total = cq_mock_count(&m);
            CHECK(total > (size_t)CQ_FP64_W);      /* it really did sandwich */
            CHECK_EQ((long long)((total - (size_t)CQ_FP64_W) % 2u), 0);
            head = (total - (size_t)CQ_FP64_W) / 2u;
            for (int k = 0; k < CQ_FP64_W; k++)
                CHECK_EQ((int)cq_mock_at(&m, head + (size_t)k)->op,
                         (int)CQ_OP_CX);
            if (!cq_mock_is_palindrome(&m, head, (size_t)CQ_FP64_W))
                cq_h_fail(__FILE__, __LINE__,
                          "%s at the %s-half-quantum mask: the %zu-gate compute "
                          "half does not read back in reverse",
                          ROWS[p].spec.name, side == 0 ? "low" : "high", head);
        }

    cq_mock_dispose(&m);
}

/* ---- (e) Two exported `fptosi` blocks in ONE region, at two offsets. ---- */

/* THE SOLE DETECTOR FOR A PROGRAM THAT IGNORES ITS OWN `off`, AND IT HAD TO BE
 * WRITTEN AS A POSITIVE CASE. Every span this module hands out goes through
 * `cq_fv_arm`, which is the ONE place `k->off` is added; drop it and the whole
 * program slides to the bottom of the caller's region. All four Rule 7 entry
 * points pass `off = 0`, so from every other case in this suite the mutation
 * is BEHAVIOURALLY INERT — the value, the palindrome, the pool, the slot scan,
 * the span-disjointness scan and all ten goldens stay green. Only a SECOND
 * program at a SECOND offset in one region sees it, because the two then
 * collide. M31, M33 and M36 carry the same case for the same reason, and the
 * death cases do not substitute for it: they drive the fit check, a different
 * line. bd a-pure-offset-slide-is-killed-by-one-case-of-seventeen measured
 * that sixteen of M33's seventeen cases stayed green under exactly this
 * mutation.
 *
 * AND THE CANARY IS THE SECOND HALF OF THE CLAIM. A run of bits past both
 * programs must be untouched and provably |0> at the end — which is what
 * catches a program that overruns its own region UPWARDS rather than sliding
 * down, a direction the collision check alone cannot see because there is
 * nothing above the second block to collide with.
 *
 * IT IS ALSO WHY `cq_fptosi_block` IS EXPORTED AT ALL. `cq_kernel_fptoui`
 * inlines the fptosi ROWS rather than calling the block, so without this case
 * the block's non-zero-offset path would ship untested and the offset slide
 * would be unrepresentable anywhere in M37. */
enum { FV_CANARY = 96 };

CQ_TEST(two_fptosi_blocks_in_one_region_do_not_collide)
{
    static const uint64_t VALUES[] = {
        UINT64_C(0x400921FB54442D18),   /* pi — the right-shift arm        */
        UINT64_C(0x43DFFFFFFFFFFFFF),   /* prevfloat(2^63) — the left arm  */
        UINT64_C(0x7FF8000000000000)    /* a qNaN — the saturating arm     */
    };
    int nvals = (int)(sizeof VALUES / sizeof VALUES[0]);
    static int owner[FV_MAX_Q];
    uint32_t canary[FV_CANARY];
    cq_mock m;

    cq_mock_init(&m);

    for (int v = 0; v < nvals; v++) {
        cq_fptosi_block k[2];
        cq_scratch scr;
        cq_ctx ctx;
        cq_sink s = cq_mock_sink(&m);
        uint32_t r0 = cq_fptosi_region();
        int32_t ha;
        int bad = 0;

        cq_mock_reset(&m);
        cq_ctx_init(&ctx, &s);
        ha = cq_bk_reg_w(&ctx, (uint32_t)CQ_FP64_W,
                         cq_ref_w_make(VALUES[v], 0u, CQ_FP64_W),
                         cq_ref_w_ones(CQ_FP64_W));
        cq_scratch_alloc(&scr, 2u * r0 + (uint32_t)FV_CANARY);
        for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&ctx, &scr.bits[i]);
        for (int i = 0; i < FV_CANARY; i++)
            canary[i] = cq_bit_qindex(scr.bits[2u * r0 + (uint32_t)i]);

        for (int b = 0; b < 2; b++) {
            k[b].a = cq_reg_cbits(&ctx.regs, ha);
            k[b].scr = &scr;
            k[b].off = (b == 0) ? 0u : r0;
        }

        /* The offsets are distinct and the second is not zero — which is what
         * makes the mutation representable at all. */
        CHECK_EQ(k[0].off, 0u);
        CHECK(k[1].off > 0u);

        for (int t = 0; t < FV_MAX_Q; t++) owner[t] = -1;
        cq_mock_reset(&m);

        for (int b = 0; b < 2 && !bad; b++) {
            size_t before = cq_mock_count(&m);

            for (int u = 0; u < cq_fptosi_steps(); u++)
                cq_fptosi_step(&ctx, &k[b], u);

            for (size_t g = before; g < cq_mock_count(&m); g++) {
                uint32_t t = cq_mock_at(&m, g)->t;

                CHECK(t < (uint32_t)FV_MAX_Q);
                if (owner[t] >= 0 && owner[t] != b) {
                    cq_h_fail(__FILE__, __LINE__,
                              "fptosi at off 0 and fptosi at off %u: q%u is "
                              "written by BOTH — a program that ignored its own "
                              "`off` slides onto its neighbour", k[1].off, t);
                    bad = 1;
                    break;
                }
                owner[t] = b;
            }
        }

        /* THE CANARY: nothing above the two programs may be written. */
        for (int i = 0; i < FV_CANARY && !bad; i++)
            if (owner[canary[i]] >= 0) {
                cq_h_fail(__FILE__, __LINE__,
                          "the canary bit q%u past both programs was written "
                          "by block %d", canary[i], owner[canary[i]]);
                bad = 1;
            }

        /* AND THE VALUES, which is the claim a disjointness scan cannot make
         * on its own: each program's result still holds ITS OWN answer after
         * the other has run over the same region. */
        for (int b = 0; b < 2 && !bad; b++) {
            int known = 0;
            uint64_t got = fv_wire_value(&ctx, cq_fptosi_result(&k[b]), &known);
            uint64_t want = host_fptosi(VALUES[v], CQ_FP64_W);

            if (!known || got != want)
                cq_h_fail(__FILE__, __LINE__,
                          "two fptosi blocks (0 and %u) on 0x%016llx: block "
                          "%d's result = %s0x%016llx, want 0x%016llx",
                          k[1].off, (unsigned long long)VALUES[v], b,
                          known ? "" : "unknown/", (unsigned long long)got,
                          (unsigned long long)want);
        }

        /* The reverse halves, in the mirror order, and then the pool claim —
         * every scratch bit provably back at |0> with two programs sharing one
         * region, the canary included. */
        for (int b = 1; b >= 0; b--)
            for (int u = cq_fptosi_steps() - 1; u >= 0; u--)
                cq_fptosi_step(&ctx, &k[b], u);

        for (uint32_t i = 0; i < scr.n; i++) {
            if (!cq_bit_is_qubit(scr.bits[i])) continue;
            if (!cq_shadow_known_zero(&ctx.shadow, cq_bit_qindex(scr.bits[i])))
                cq_h_fail(__FILE__, __LINE__,
                          "two fptosi blocks: scratch bit %u (q%u) is not back "
                          "at |0> after both reverse halves", i,
                          cq_bit_qindex(scr.bits[i]));
            scr.bits[i] = cq_bit_zero();
        }

        cq_scratch_dispose(&scr);
        cq_ctx_dispose(&ctx);
    }

    cq_mock_dispose(&m);
}
