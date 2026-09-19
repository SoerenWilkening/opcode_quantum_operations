/* tests/test_kernel_fadd_spans.inc — the LAYOUT's three instruments: the
 * span-disjointness scan, the palindrome at an asymmetric mask, and two
 * exported `fsub` blocks in one region with a canary. Included by
 * test_kernel_fadd.c AFTER test_kernel_fadd_scan.inc, whose `fa_row_gates`,
 * `FA_SCAN_A`/`FA_SCAN_B` and `FA_REF_MAX` it reuses.
 *
 * SPLIT OFF THE SCAN ON A REAL SEAM, not on a line count. Everything next door
 * is about the SLOT SCHEDULE — how many slots a program has and which of
 * X / CX / CCX each one emits. Everything here is about the LAYOUT — which
 * bits each row owns, and whether a program stays inside them. M31's measured
 * finding is that the two come apart: collapsing two blocks onto one offset
 * kept the value, the palindrome, the pool, every gate count AND the slot
 * scan, so a suite with only the schedule half has a hole exactly the shape of
 * its scratch map. K15 has 133 rows in one flat region against M31's five
 * spans, so the same hazard is here 133 ways.
 *
 * NOTHING BELOW READS A GOLDEN.
 */

/* The 64-lane value on a span, read out of the shadow. Determinate because
 * nothing in K15 rotates — the shadow is EXACT on the rotation-free surface. */
static uint64_t fa_wire_value(cq_ctx *ctx, const cq_bit *b, int *known)
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
 * WHAT IS ASSERTED IS THE LAYOUT AND NOT THE ACCIDENT: no qubit index is
 * written by two different ROWS. It reads no golden and is derived from the
 * program table plus the per-row gate partition the scan already computes. */
enum { FA_MAX_Q = 65536 };

CQ_TEST(the_programs_spans_are_pairwise_disjoint)
{
    static int owner[FA_MAX_Q];
    static cq_op ref[FA_REF_MAX];
    cq_mock m;

    cq_mock_init(&m);

    for (int p = 0; p < N_FADD_ROWS; p++) {
        cq_fadd_row rows[CQ_FADD_MAX_ROWS];
        int n = cq_fadd_program(ROWS[p].prog, rows);
        cq_ref_w q = cq_ref_w_ones(CQ_FP64_W);
        size_t cursor = 0u;
        int touched = 0, bad = 0, seen = 0, emitting = 0;

        for (int t = 0; t < FA_MAX_Q; t++) owner[t] = -1;
        (void)fadd_run(&ROWS[p], FA_SCAN_A, FA_SCAN_B, q, q, &m);

        for (int i = 0; i < n && !bad; i++) {
            size_t g = fa_row_gates(rows, n, i, ref);

            if (fa_row_slots(&rows[i]) > 0) emitting++;
            if (g > 0u) seen++;

            for (size_t k = 0; k < g && !bad; k++) {
                uint32_t t;

                CHECK(cursor < cq_mock_count(&m));
                t = cq_mock_at(&m, cursor++)->t;
                CHECK(t < (uint32_t)FA_MAX_Q);
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
         * 19 view rows and 11 pick rows of `fadd` are wiring and are expected
         * to contribute nothing, which is why the count is over emitting rows
         * rather than over all of them. */
        CHECK(touched > 0);
        CHECK_EQ(seen, emitting);
    }

    cq_mock_dispose(&m);
}

/* ---- (d) The palindrome, at an ASYMMETRIC mask. ------------------------ */

/* AN ALL-CLASSICAL MASK PAIR IS NOT A PALINDROME CASE, IT IS AN L5 CASE — the
 * R9 short-circuit never enters cq_sandwich, so cq_mock_count is the copy-out
 * alone and the check is being asked about a stream that does not exist. The
 * mask that DOES belong here is risk R8's named witness, `a` all Q and `b` all
 * CQ_BIT_ZERO, which is in no symmetric mask set; the mirror is here too
 * because `fadd`'s two operands are not interchangeable — `_sf_propagate_nan2`
 * prefers the first, the swap at :49-56 orders them and `fsub` negates only
 * the second — so a fold that fired on one operand would hide in one order.
 *
 * WHY THE HEAD LENGTH IS DERIVED RATHER THAN PREDICTED: at this mask the scan
 * would have to fold on the operands' bit-kinds as well as on the views, which
 * is a second transcription of the fold table rather than of the schedule.
 * `total = 2*head + 64` with a 64-gate copy-out is a structural claim in its
 * own right, and the ordered comparison then has teeth that no gate COUNT has
 * (risk R8: K12's reversed forward list has a different multiset with the
 * identical total). */
CQ_TEST(the_compute_half_is_a_palindrome_at_an_asymmetric_mask)
{
    cq_mock m;
    cq_ref_w all_q = cq_ref_w_ones(CQ_FP64_W), none = cq_ref_w_zero();

    cq_mock_init(&m);

    for (int p = 0; p < N_FADD_ROWS; p++)
        for (int side = 0; side < 2; side++) {
            size_t total, head;
            uint64_t want, got;

            got = fadd_run(&ROWS[p], FA_SCAN_A, FA_SCAN_B,
                           side == 0 ? all_q : none,
                           side == 0 ? none  : all_q, &m);
            want = ROWS[p].host(FA_SCAN_A, FA_SCAN_B);
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
                          "%s at the %s asymmetric mask: the %zu-gate compute "
                          "half does not read back in reverse",
                          ROWS[p].spec.name,
                          side == 0 ? "a-quantum" : "b-quantum", head);
        }

    cq_mock_dispose(&m);
}

/* ---- (e) Two exported `fsub` blocks in ONE region, at two offsets. ----- */

/* THE SOLE DETECTOR FOR A PROGRAM THAT IGNORES ITS OWN `off`, AND IT HAD TO BE
 * WRITTEN AS A POSITIVE CASE. Every span this module hands out goes through
 * `cq_fa_arm`, which is the ONE place `k->off` is added; drop it and the whole
 * program slides to the bottom of the caller's region. Both Rule 7 kernels
 * pass `off = 0`, so from every other case in this suite the mutation is
 * BEHAVIOURALLY INERT — the value, the palindrome, the pool, the slot scan,
 * the span-disjointness scan and both goldens stay green. Only a SECOND
 * program at a SECOND offset in one region sees it, because the two then
 * collide. M31 and M36 carry the same case for the same reason, and the death
 * cases do not substitute for it: they drive the fit check, a different line.
 *
 * AND THE CANARY IS THE SECOND HALF OF THE CLAIM. A run of bits past both
 * programs must be untouched and provably |0> at the end — which is what
 * catches a program that overruns its own region UPWARDS rather than sliding
 * down, a direction the collision check alone cannot see because there is
 * nothing above the second block to collide with.
 *
 * THIS IS THE EXPORT K19's `fptoui` COMPOSES (fptoui.jl:45), so the case is
 * also the standing witness that `cq_fsub_block` works at a non-zero offset
 * inside someone else's region — which is the only way a consumer ever uses
 * it. */
enum { FA_CANARY = 96 };

CQ_TEST(two_fsub_blocks_in_one_region_do_not_collide)
{
    static const uint64_t VALUES[][2] = {
        { CQ_F64_ONE,    CQ_F64_NEG(CQ_F64_QNAN_A) },   /* the m63k cell    */
        { FA_TWO,        CQ_F64_ONE                },   /* an ordinary sub  */
        { CQ_F64_ONE,    FA_STICKY                 }    /* a rounding one   */
    };
    int nvals = (int)(sizeof VALUES / sizeof VALUES[0]);
    static int owner[FA_MAX_Q];
    uint32_t canary[FA_CANARY];
    cq_mock m;

    cq_mock_init(&m);

    for (int v = 0; v < nvals; v++) {
        cq_fsub_block k[2];
        cq_scratch scr;
        cq_ctx ctx;
        cq_sink s = cq_mock_sink(&m);
        uint32_t r0 = cq_fsub_region();
        int32_t ha, hb;
        int bad = 0;

        cq_mock_reset(&m);
        cq_ctx_init(&ctx, &s);
        ha = cq_bk_reg_w(&ctx, (uint32_t)CQ_FP64_W,
                         cq_ref_w_make(VALUES[v][0], 0u, CQ_FP64_W),
                         cq_ref_w_ones(CQ_FP64_W));
        hb = cq_bk_reg_w(&ctx, (uint32_t)CQ_FP64_W,
                         cq_ref_w_make(VALUES[v][1], 0u, CQ_FP64_W),
                         cq_ref_w_ones(CQ_FP64_W));
        cq_scratch_alloc(&scr, 2u * r0 + (uint32_t)FA_CANARY);
        for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&ctx, &scr.bits[i]);
        for (int i = 0; i < FA_CANARY; i++)
            canary[i] = cq_bit_qindex(scr.bits[2u * r0 + (uint32_t)i]);

        for (int b = 0; b < 2; b++) {
            k[b].a = cq_reg_cbits(&ctx.regs, ha);
            k[b].b = cq_reg_cbits(&ctx.regs, hb);
            k[b].scr = &scr;
            k[b].off = (b == 0) ? 0u : r0;
        }

        /* The offsets are distinct and the second is not zero — which is what
         * makes the mutation representable at all. */
        CHECK_EQ(k[0].off, 0u);
        CHECK(k[1].off > 0u);

        for (int t = 0; t < FA_MAX_Q; t++) owner[t] = -1;
        cq_mock_reset(&m);

        for (int b = 0; b < 2 && !bad; b++) {
            size_t before = cq_mock_count(&m);

            for (int u = 0; u < cq_fsub_steps(); u++)
                cq_fsub_step(&ctx, &k[b], u);

            for (size_t g = before; g < cq_mock_count(&m); g++) {
                uint32_t t = cq_mock_at(&m, g)->t;

                CHECK(t < (uint32_t)FA_MAX_Q);
                if (owner[t] >= 0 && owner[t] != b) {
                    cq_h_fail(__FILE__, __LINE__,
                              "fsub at off 0 and fsub at off %u: q%u is written "
                              "by BOTH — a program that ignored its own `off` "
                              "slides onto its neighbour", k[1].off, t);
                    bad = 1;
                    break;
                }
                owner[t] = b;
            }
        }

        /* THE CANARY: nothing above the two programs may be written. */
        for (int i = 0; i < FA_CANARY && !bad; i++)
            if (owner[canary[i]] >= 0) {
                cq_h_fail(__FILE__, __LINE__,
                          "the canary bit q%u past both programs was written "
                          "by row-group %d", canary[i], owner[canary[i]]);
                bad = 1;
            }

        /* AND THE VALUES, which is the claim a disjointness scan cannot make
         * on its own: each program's result still holds ITS OWN answer after
         * the other has run over the same region. */
        for (int b = 0; b < 2 && !bad; b++) {
            int known = 0;
            uint64_t got = fa_wire_value(&ctx, cq_fsub_result(&k[b]), &known);
            uint64_t want = host_fsub(VALUES[v][0], VALUES[v][1]);

            if (!known || got != want)
                cq_h_fail(__FILE__, __LINE__,
                          "two fsub blocks (0 and %u) on (0x%016llx, "
                          "0x%016llx): block %d's result = %s0x%016llx, want "
                          "0x%016llx", k[1].off,
                          (unsigned long long)VALUES[v][0],
                          (unsigned long long)VALUES[v][1], b,
                          known ? "" : "unknown/", (unsigned long long)got,
                          (unsigned long long)want);
        }

        /* The reverse halves, in the mirror order, and then the pool claim —
         * every scratch bit provably back at |0> with two programs sharing one
         * region, the canary included. */
        for (int b = 1; b >= 0; b--)
            for (int u = cq_fsub_steps() - 1; u >= 0; u--)
                cq_fsub_step(&ctx, &k[b], u);

        for (uint32_t i = 0; i < scr.n; i++) {
            if (!cq_bit_is_qubit(scr.bits[i])) continue;
            if (!cq_shadow_known_zero(&ctx.shadow, cq_bit_qindex(scr.bits[i])))
                cq_h_fail(__FILE__, __LINE__,
                          "two fsub blocks: scratch bit %u (q%u) is not back at "
                          "|0> after both reverse halves", i,
                          cq_bit_qindex(scr.bits[i]));
            scr.bits[i] = cq_bit_zero();
        }

        cq_scratch_dispose(&scr);
        cq_ctx_dispose(&ctx);
    }

    cq_mock_dispose(&m);
}
