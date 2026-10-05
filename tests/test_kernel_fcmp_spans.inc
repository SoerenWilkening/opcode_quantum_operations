/* tests/test_kernel_fcmp_spans.inc — the LAYOUT's three instruments: the
 * span-disjointness scan, the palindrome at an asymmetric mask, and two
 * programs in one region — and, added 2026-10-05 (bd ifoy (e)), a fourth:
 * two programs of ONE length through one buffer, the only detector for the
 * memoised map's key. Included by test_kernel_fcmp.c AFTER
 * test_kernel_fcmp_slots.inc, whose `fc_scan_slot`, `fc_row_slots` and
 * `FC_SCAN_A`/`FC_SCAN_B` it reuses.
 *
 * SPLIT OFF slots.inc ON A REAL SEAM, not on a line count. Everything next
 * door is about the SLOT SCHEDULE — how many slots a program has and which of
 * X / CX / CCX each one emits. Everything here is about the LAYOUT — which
 * bits each row owns, and whether a program stays inside them. M31's measured
 * finding is that the two come apart: collapsing two blocks onto one offset
 * kept the value, the palindrome, the pool, every gate count AND the slot
 * scan, so a suite with only the schedule half has a hole exactly the shape of
 * its scratch map.
 *
 * NOTHING BELOW READS A GOLDEN.
 */
/* ---- (c) Every row's span is disjoint from every other row's. ---------- */

/* WRITTEN BECAUSE A MEASURED M31 MUTANT SURVIVED EVERYTHING ELSE. Collapsing
 * two `eq` blocks onto one offset left the VALUE right, the palindrome intact,
 * the pool restored, the slot scan green and every gate count identical — and
 * it was equivalent only by an accident of `lower_eq!` leaving its region all
 * zero exactly when its own flag is 0. K18 has up to 42 rows in one flat
 * region against M31's five spans, so the same hazard is here 42 ways.
 *
 * WHAT IS ASSERTED IS THE LAYOUT AND NOT THE ACCIDENT: no qubit index is
 * written by two different ROWS. It reads no golden and is derived from the
 * program table rather than from the kernel. */
enum { FC_MAX_Q = 8192 };

CQ_TEST(the_programs_spans_are_pairwise_disjoint)
{
    cq_mock m;
    static int owner[FC_MAX_Q];

    cq_mock_init(&m);

    for (int i = 0; i < N_FCMP_ROWS; i++) {
        cq_fcmp_row rows[CQ_FCMP_MAX_ROWS];
        int n = cq_fcmp_program(ROWS[i].pred, rows);
        size_t cursor = 0u;
        int touched = 0, bad = 0, seen_rows = 0;
        cq_ref_w q = cq_ref_w_ones(CQ_FP64_W);

        for (int t = 0; t < FC_MAX_Q; t++) owner[t] = -1;

        (void)fcmp_run(&ROWS[i], FC_SCAN_A, FC_SCAN_B, q, q, &m);

        for (int r = 0; r < n && !bad; r++) {
            int hit = 0;

            for (int v = 0; v < fc_row_slots(&rows[r]) && !bad; v++) {
                int dummy = 0;
                uint32_t t;

                if (fc_scan_slot(&rows[r], v, &dummy) == FC_PRED_NONE) continue;

                t = cq_mock_at(&m, cursor++)->t;
                CHECK(t < (uint32_t)FC_MAX_Q);
                touched++;
                hit = 1;

                if (owner[t] >= 0 && owner[t] != r) {
                    cq_h_fail(__FILE__, __LINE__,
                              "%s row %d slot %d: q%u is written by row %d AND "
                              "by row %d — two blocks of one program must not "
                              "share a bit", ROWS[i].spec.name, r, v, t,
                              owner[t], r);
                    bad = 1;
                    break;
                }
                owner[t] = r;
            }
            seen_rows += hit;
        }

        /* NOT VACUOUS: EVERY row must have written something, or a
         * disjointness claim over rows that never ran is no claim at all. */
        CHECK(touched > 0);
        CHECK_EQ(seen_rows, n);
    }

    cq_mock_dispose(&m);
}

/* ---- (d) The palindrome, at an ASYMMETRIC mask. ------------------------ */

/* AN ALL-CLASSICAL MASK PAIR IS NOT A PALINDROME CASE, IT IS AN L5 CASE — the
 * R9 short-circuit never enters cq_sandwich, so cq_mock_count is 0 and the
 * check is being asked about a stream that does not exist. The mask that DOES
 * belong here is risk R8's named witness, `a` all Q and `b` all CQ_BIT_ZERO,
 * which is in no symmetric mask set; the mirror is here too because `fcmp`'s
 * two operands are not interchangeable (the swap set is four rows of fourteen)
 * and a fold that fired only on the second operand would hide in one order.
 *
 * WHY THE HEAD LENGTH IS DERIVED RATHER THAN PREDICTED: at this mask the scan
 * above would have to fold on the operands' bit-kinds as well as on the views,
 * which is a second transcription of the fold table rather than of the
 * schedule. `total = 2*head + 1` with ONE copy-out gate is a structural claim
 * in its own right — an odd total — and the ordered comparison then has teeth
 * that no gate COUNT has (risk R8: K12's reversed forward list has a different
 * multiset with the identical total). */
CQ_TEST(the_compute_half_is_a_palindrome_at_an_asymmetric_mask)
{
    cq_mock m;
    cq_ref_w all_q = cq_ref_w_ones(CQ_FP64_W), none = cq_ref_w_zero();

    cq_mock_init(&m);

    for (int i = 0; i < N_FCMP_ROWS; i++)
        for (int side = 0; side < 2; side++) {
            size_t total, head;
            uint64_t want, got;

            got = fcmp_run(&ROWS[i], FC_SCAN_A, FC_SCAN_B,
                           side == 0 ? all_q : none,
                           side == 0 ? none  : all_q, &m);
            want = (uint64_t)host_fcmp(FC_SCAN_A, FC_SCAN_B, ROWS[i].pred);
            CHECK_EQ(got, want);

            total = cq_mock_count(&m);
            CHECK(total > 1u);                 /* it really did sandwich */
            CHECK_EQ((long long)(total % 2u), 1);
            head = (total - 1u) / 2u;
            CHECK_EQ((int)cq_mock_at(&m, head)->op, (int)CQ_OP_CX);
            if (!cq_mock_is_palindrome(&m, head, 1u))
                cq_h_fail(__FILE__, __LINE__,
                          "%s at the %s asymmetric mask: the %zu-gate compute "
                          "half does not read back in reverse",
                          ROWS[i].spec.name, side == 0 ? "a-quantum"
                                                       : "b-quantum", head);
        }

    cq_mock_dispose(&m);
}

/* ---- (e) Two programs in ONE region, at two offsets. ------------------- */

/* THE SOLE DETECTOR FOR A PROGRAM THAT IGNORES ITS OWN `off`, AND IT HAD TO BE
 * WRITTEN AS A POSITIVE CASE. Every span this module hands out goes through
 * `offsets()` — `row_off()` since 2026-10-05 (bd ifoy (e)), over a memoised
 * base-relative map — which is the ONE place `k->off` is added; drop it and
 * the whole program slides to the bottom of the caller's region. The kernel
 * always passes `off = 0`, so from every other case in this suite the
 * mutation is BEHAVIOURALLY INERT — the value, the palindrome, the pool, the
 * slot scan, the span-disjointness scan and all fourteen goldens stay green.
 * Only a SECOND program at a SECOND offset in one region sees it, because the
 * two then collide. M31 carries the same case for the same reason
 * (two_blocks_in_one_region_do_not_collide), and the death cases do not
 * substitute for it: they drive `check_region`, which is a different line.
 *
 * THE WHOLE REGION IS PRE-MATERIALISED, as cq_sandwich does under I6(b), so
 * every slot emits and the disjointness scan sees the whole stream rather than
 * whatever survived the fold table. */
static int fc_wire_value(cq_ctx *ctx, const cq_bit *b, int *known)
{
    cq_shadow sv;

    if (cq_bit_is_const(*b)) { *known = 1; return cq_bit_value(*b); }
    sv = cq_shadow_get(&ctx->shadow, cq_bit_qindex(*b));
    *known = !sv.unknown;
    return (int)sv.value;
}

CQ_TEST(two_programs_in_one_region_do_not_collide)
{
    static const cq_fcmp_pred PAIRS[][2] = {
        { CQ_FCMP_UNO, CQ_FCMP_OEQ },
        { CQ_FCMP_OEQ, CQ_FCMP_UNO },
        { CQ_FCMP_ORD, CQ_FCMP_UNO }
    };
    static const uint64_t VALUES[] = { CQ_F64_QNAN_B, CQ_F64_NEG_ZERO,
                                       CQ_F64_POS_INF };
    int npairs = (int)(sizeof PAIRS / sizeof PAIRS[0]);
    int nvals  = (int)(sizeof VALUES / sizeof VALUES[0]);
    static int owner[FC_MAX_Q];
    cq_mock m;

    cq_mock_init(&m);

    for (int p = 0; p < npairs; p++)
        for (int v = 0; v < nvals; v++) {
            cq_fcmp_row rows[2][CQ_FCMP_MAX_ROWS];
            cq_fcmp_block k[2];
            cq_scratch scr;
            cq_ctx ctx;
            cq_sink s = cq_mock_sink(&m);
            int n[2], bad = 0;
            uint32_t r0;
            int32_t ha, hb;

            n[0] = cq_fcmp_program(PAIRS[p][0], rows[0]);
            n[1] = cq_fcmp_program(PAIRS[p][1], rows[1]);
            r0   = cq_fcmp_region(rows[0], n[0]);

            cq_mock_reset(&m);
            cq_ctx_init(&ctx, &s);
            ha = cq_bk_reg_w(&ctx, (uint32_t)CQ_FP64_W,
                             cq_ref_w_make(VALUES[v], 0u, CQ_FP64_W),
                             cq_ref_w_ones(CQ_FP64_W));
            hb = cq_bk_reg_w(&ctx, (uint32_t)CQ_FP64_W,
                             cq_ref_w_make(CQ_F64_ONE, 0u, CQ_FP64_W),
                             cq_ref_w_ones(CQ_FP64_W));
            cq_scratch_alloc(&scr, r0 + cq_fcmp_region(rows[1], n[1]));
            for (uint32_t i = 0; i < scr.n; i++)
                cq_materialise(&ctx, &scr.bits[i]);

            for (int b = 0; b < 2; b++) {
                k[b].a = cq_reg_cbits(&ctx.regs, ha);
                k[b].b = cq_reg_cbits(&ctx.regs, hb);
                k[b].scr = &scr;
                k[b].off = (b == 0) ? 0u : r0;
                k[b].rows = rows[b];
                k[b].n_rows = n[b];
            }

            /* The offsets are distinct and the second is not zero — which is
             * what makes the mutation representable at all. */
            CHECK_EQ(k[0].off, 0u);
            CHECK(k[1].off > 0u);

            for (int q = 0; q < FC_MAX_Q; q++) owner[q] = -1;
            cq_mock_reset(&m);

            for (int b = 0; b < 2 && !bad; b++) {
                size_t before = cq_mock_count(&m);

                for (int u = 0; u < cq_fcmp_steps(rows[b], n[b]); u++)
                    cq_fcmp_step(&ctx, &k[b], u);

                for (size_t g = before; g < cq_mock_count(&m); g++) {
                    uint32_t t = cq_mock_at(&m, g)->t;

                    CHECK(t < (uint32_t)FC_MAX_Q);
                    if (owner[t] >= 0 && owner[t] != b) {
                        cq_h_fail(__FILE__, __LINE__,
                                  "%s at off 0 and %s at off %u: q%u is "
                                  "written by BOTH — a program that ignored "
                                  "its own `off` slides onto its neighbour",
                                  cq_fcmp_name(PAIRS[p][0]),
                                  cq_fcmp_name(PAIRS[p][1]), k[1].off, t);
                        bad = 1;
                        break;
                    }
                    owner[t] = b;
                }
            }

            /* AND THE VALUES, which is the claim a disjointness scan cannot
             * make on its own: each program's flag still holds ITS OWN answer
             * after the other program has run over the same region. */
            for (int b = 0; b < 2 && !bad; b++) {
                int known = 0;
                int got = fc_wire_value(&ctx, cq_fcmp_flag(&k[b]), &known);
                int want = host_fcmp(VALUES[v], CQ_F64_ONE, PAIRS[p][b]);

                if (!known || got != want)
                    cq_h_fail(__FILE__, __LINE__,
                              "two programs (%s at 0, %s at %u) on 0x%016llx: "
                              "program %d's flag = %s%d, want %d",
                              cq_fcmp_name(PAIRS[p][0]),
                              cq_fcmp_name(PAIRS[p][1]), k[1].off,
                              (unsigned long long)VALUES[v], b,
                              known ? "" : "unknown/", got, want);
            }

            /* The reverse halves, in the mirror order, and then the pool claim
             * — every scratch bit provably back at |0> with two programs
             * sharing one region. */
            for (int b = 1; b >= 0; b--)
                for (int u = cq_fcmp_steps(rows[b], n[b]) - 1; u >= 0; u--)
                    cq_fcmp_step(&ctx, &k[b], u);

            for (uint32_t i = 0; i < scr.n; i++) {
                if (!cq_bit_is_qubit(scr.bits[i])) continue;
                if (!cq_shadow_known_zero(&ctx.shadow,
                                          cq_bit_qindex(scr.bits[i])))
                    cq_h_fail(__FILE__, __LINE__,
                              "two programs (%s, %s): scratch bit %u (q%u) is "
                              "not back at |0> after both reverse halves",
                              cq_fcmp_name(PAIRS[p][0]),
                              cq_fcmp_name(PAIRS[p][1]), i,
                              cq_bit_qindex(scr.bits[i]));
                scr.bits[i] = cq_bit_zero();
            }

            cq_scratch_dispose(&scr);
            cq_ctx_dispose(&ctx);
        }

    cq_mock_dispose(&m);
}

/* ---- (f) Two programs of ONE length, alternated through ONE buffer. ----- */

/* THE ONLY DETECTOR FOR fcmp_step.c's MAP KEY (bd ifoy (e), 2026-10-05). The
 * step machine memoises each program's layout, keyed by its FULL ROW TEXT.
 * Every program cq_fcmp_program builds has a length no other one has, so a
 * key cut to the length alone, or to the caller's pointer plus the length,
 * passed every other case here and in the fmisc and template suites —
 * measured. This case builds two programs of the SAME length with DIFFERENT
 * ops — `a != b` then `lower_not1!` (so `a == b`), and M31's NaN class of `a`
 * then `lower_not1!` (so `a` is not a NaN) — and alternates them, slot by
 * slot, through ONE buffer rewritten before every call, which is what a
 * caller's stack array does between two kernel calls. Each one's slot and
 * region totals are asked of the sibling modules, never of M36; each flag is
 * checked against a reference sharing nothing with the circuit (a bitwise
 * `==`, and the host's isunordered); and both reverse halves must leave the
 * shared region at |0>. */
static void fc_load(cq_fcmp_row *buf, int which)
{
    buf[0].op = (short)(which == 0 ? CQ_FCOP_EQ : CQ_FCOP_CLASS_NAN);
    buf[0].s0 = CQ_FC_A;
    buf[0].s1 = (short)(which == 0 ? CQ_FC_B : 0);
    buf[0].s2 = 0;
    buf[1].op = CQ_FCOP_NOT1;
    buf[1].s0 = 0; buf[1].s1 = 0; buf[1].s2 = 0;
}

CQ_TEST(two_programs_of_one_length_each_get_their_own_layout)
{
    static const uint64_t VALUES[] = { CQ_F64_ONE, CQ_F64_QNAN_B,
                                       CQ_F64_POS_INF };
    const int steps[2] = { cq_eq_steps(CQ_FP64_W) + FS_NOT1,
                           cq_fp_class_steps(CQ_FP_IS_NAN) + FS_NOT1 };
    const uint32_t region[2] = { (uint32_t)cq_eq_region(CQ_FP64_W) + 1u,
                                 cq_fp_class_region(CQ_FP_IS_NAN) + 1u };
    int nvals = (int)(sizeof VALUES / sizeof VALUES[0]);
    cq_fcmp_row buf[2];
    cq_mock m;

    /* The premise: the costs that tell the two programs apart do differ. */
    CHECK(steps[0] != steps[1]);
    CHECK(region[0] != region[1]);

    cq_mock_init(&m);

    for (int v = 0; v < nvals; v++) {
        cq_fcmp_block k[2];
        cq_scratch scr;
        cq_ctx ctx;
        cq_sink s = cq_mock_sink(&m);
        int bad = 0, top;
        int32_t ha, hb;

        /* The layout, through the one buffer: A, B, A, B. */
        for (int r = 0; r < 4; r++) {
            int got_s;
            uint32_t got_r;

            fc_load(buf, r & 1);
            got_s = cq_fcmp_steps(buf, 2);
            got_r = cq_fcmp_region(buf, 2);
            if (got_s != steps[r & 1] || got_r != region[r & 1]) {
                cq_h_fail(__FILE__, __LINE__,
                          "program %d of two of length 2 (call %d): %d slots "
                          "and %u bits, want %d and %u — the map served "
                          "another program's layout", r & 1, r, got_s, got_r,
                          steps[r & 1], region[r & 1]);
                bad = 1;
            }
        }
        if (bad) break;

        cq_mock_reset(&m);
        cq_ctx_init(&ctx, &s);
        ha = cq_bk_reg_w(&ctx, (uint32_t)CQ_FP64_W,
                         cq_ref_w_make(VALUES[v], 0u, CQ_FP64_W),
                         cq_ref_w_ones(CQ_FP64_W));
        hb = cq_bk_reg_w(&ctx, (uint32_t)CQ_FP64_W,
                         cq_ref_w_make(CQ_F64_ONE, 0u, CQ_FP64_W),
                         cq_ref_w_ones(CQ_FP64_W));
        cq_scratch_alloc(&scr, region[0] + region[1]);
        for (uint32_t i = 0; i < scr.n; i++)
            cq_materialise(&ctx, &scr.bits[i]);

        for (int b = 0; b < 2; b++) {
            k[b].a = cq_reg_cbits(&ctx.regs, ha);
            k[b].b = cq_reg_cbits(&ctx.regs, hb);
            k[b].scr = &scr;
            k[b].off = (b == 0) ? 0u : region[0];
            k[b].rows = buf;
            k[b].n_rows = 2;
        }

        /* Forward with the two programs' slots INTERLEAVED, the buffer
         * rewritten before every call, so every lookup alternates. */
        top = (steps[0] > steps[1]) ? steps[0] : steps[1];
        for (int u = 0; u < top; u++)
            for (int b = 0; b < 2; b++)
                if (u < steps[b]) {
                    fc_load(buf, b);
                    cq_fcmp_step(&ctx, &k[b], u);
                }

        for (int b = 0; b < 2; b++) {
            int known = 0, got, want;

            fc_load(buf, b);
            got  = fc_wire_value(&ctx, cq_fcmp_flag(&k[b]), &known);
            want = (b == 0) ? (VALUES[v] == CQ_F64_ONE)
                            : !host_fcmp(VALUES[v], VALUES[v], CQ_FCMP_UNO);
            if (!known || got != want)
                cq_h_fail(__FILE__, __LINE__,
                          "program %d on 0x%016llx: flag = %s%d, want %d", b,
                          (unsigned long long)VALUES[v],
                          known ? "" : "unknown/", got, want);
        }

        /* The exact mirror of the forward order, then the pool claim. */
        for (int u = top - 1; u >= 0; u--)
            for (int b = 1; b >= 0; b--)
                if (u < steps[b]) {
                    fc_load(buf, b);
                    cq_fcmp_step(&ctx, &k[b], u);
                }

        for (uint32_t i = 0; i < scr.n; i++) {
            if (cq_bit_is_qubit(scr.bits[i])
                && !cq_shadow_known_zero(&ctx.shadow,
                                         cq_bit_qindex(scr.bits[i])))
                cq_h_fail(__FILE__, __LINE__,
                          "two programs of one length: scratch bit %u (q%u) "
                          "is not back at |0>", i, cq_bit_qindex(scr.bits[i]));
            scr.bits[i] = cq_bit_zero();
        }

        cq_scratch_dispose(&scr);
        cq_ctx_dispose(&ctx);
    }

    cq_mock_dispose(&m);
}
