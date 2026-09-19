/* tests/test_kernel_fma_map.inc — M39, K20. THE PREFIX MAP, AND THE FOUR
 * GUARDS NO DEATH CASE CAN REACH. Added at review round 1. Included by
 * test_kernel_fma.c.
 *
 * (a) THE MAP. `cq_fu_row_at` is the one function in this module that nothing
 *     else in tests/ calls, and until this review nothing called it at all: a
 *     grep for `cq_fu_` across tests/ found only comments. That matters
 *     because the module's own banner names ONE spelling as the trap of this
 *     landing — a first-match binary search, which on a table where 88 rows
 *     own zero slots lands on a VIEW and dispatches the wrong op with the
 *     wrong operands. The first draft then followed the search with a forward
 *     walk that REPAIRED exactly that mutant in silence. The walk is gone and
 *     this case is what replaces it.
 *
 * (a2) THE OPERAND-CODE VOCABULARY, which is here rather than next door
 *     because it is the POSITIVE half of a pair whose other half is a death
 *     case: `CQ_FU_N_CODE` was declared, never referenced and never asserted,
 *     and neither `fma.h`'s new `_Static_assert` nor either case alone pins
 *     it. See that case's own comment for which direction each one catches.
 *
 * (b) FOUR GUARDS THAT NO DEATH CASE CAN DRIVE, COVERED AS POSITIVE CLAIMS
 *     INSTEAD, AND THE DISTINCTION IS REPORTED RATHER THAN PAPERED OVER. Nine
 *     of M39's refusals take a ROW, an OP or an INDEX and are driven directly
 *     in tests/test_kernel_fma_death.c. These four read `cq_fma_rows()`
 *     themselves and refuse a property the SHIPPED table does not have:
 *
 *       fma_operand.c  `a view chain does not terminate`   — needs a CYCLE
 *       fma_emit.c     `unknown op in the program`         — needs an emitting
 *                                                            row with no arm
 *       fma_emit.c     `landed on a zero-slot row`         — needs a broken
 *                                                            `cq_fu_row_at`
 *       fma_emit.c     `result` is a view / not a span     — needs the last
 *                                                            row to be either
 *
 *     THE TEMPTING FIX IS A TABLE-INJECTION SEAM AND IT IS REFUSED. A settable
 *     `cq_fma_rows` override would be mutable module state whose initialisation
 *     a consumer can forget — the precise thing fma_operand.c's header refuses
 *     for cached operands — and `cq_fu_map_get` memoises from the real table,
 *     so an override would desynchronise the map from the program and make
 *     every other case in this suite measure something else. What the three
 *     cases below assert instead is the reachability claim itself: over the
 *     shipped table each of those guards CANNOT fire, and here is the executed
 *     proof. A table edit that made one reachable reddens the case saying it
 *     could not.
 */

/* ---- (a) The prefix map names the LAST row at a boundary. --------------- */

/* THE INDEPENDENT ORACLE IS A LINEAR SCAN, and it shares nothing with the
 * binary search but the predicate: `the last i with step0[i] <= u`. The probe
 * set is every emitting row's FIRST and LAST slot — 2 x 307 boundaries rather
 * than all 241,083 slots, because a search can only be wrong AT a boundary —
 * plus `u = 0` and `u = steps - 1` by name, which are the two a probe set
 * generated from the table could in principle miss.
 *
 * AND THE NON-VACUITY CLAIM IS THE ZERO-SLOT ONE. A boundary where no view
 * precedes the owning row is a boundary both spellings get right, so the case
 * counts the boundaries at which at least one zero-slot row shares the same
 * `step0` and REQUIRES that count to be positive. Measured at the landing: 88
 * rows own zero slots. */
CQ_TEST(the_prefix_map_names_the_last_row_at_a_zero_slot_boundary)
{
    const cq_fu_map *m = cq_fu_map_get();
    const cq_fma_row *rows;
    int n, n_probe = 0, n_shared = 0, n_zero = 0;

    rows = cq_fma_rows(&n);
    CHECK_EQ(m->n, n);
    CHECK_EQ(m->steps, cq_fma_steps());
    for (int i = 0; i < n; i++)
        if (cq_fu_row_steps(&rows[i]) == 0) n_zero++;
    CHECK(n_zero > 0);

    for (int i = -1; i < n; i++) {
        int c = cq_fu_row_steps(i < 0 ? &rows[0] : &rows[i]);
        int probe[2], n_here = 0;

        if (i < 0) { probe[n_here++] = 0; probe[n_here++] = m->steps - 1; }
        else if (c > 0) { probe[n_here++] = m->step0[i];
                          probe[n_here++] = m->step0[i] + c - 1; }

        for (int p = 0; p < n_here; p++) {
            int u = probe[p], want = 0, within = -1, got, shared = 0;

            /* The oracle: a plain scan for the LAST index satisfying the
             * predicate. It asks the map for `step0` and nothing else. */
            for (int j = 0; j < n; j++) if (m->step0[j] <= u) want = j;
            for (int j = 0; j < n; j++)
                if (m->step0[j] == m->step0[want] && j != want
                    && cq_fu_row_steps(&rows[j]) == 0) shared = 1;

            got = cq_fu_row_at(m, u, &within);
            if (got != want)
                cq_h_fail(__FILE__, __LINE__,
                          "slot %d: cq_fu_row_at said row %d, the last row "
                          "with step0 <= u is %d — a first-match search "
                          "dispatches the wrong op", u, got, want);
            CHECK_EQ(within, u - m->step0[want]);
            /* The row returned must OWN the slot, which is the property the
             * dispatch's own zero-slot refusal exists to catch downstream. */
            CHECK(cq_fu_row_steps(&rows[got]) > 0);
            n_probe++;
            n_shared += shared;
        }
    }
    CHECK(n_probe > 2);
    /* The discriminating boundaries exist: at least one probe sits where a
     * zero-slot row shares the owning row's `step0`. Without this the case
     * could pass against a table with no views at all. */
    CHECK(n_shared > 0);
}

/* ---- (a2) The operand-code vocabulary, from the other side. ------------- */

/* `CQ_FU_N_CODE` IS LOAD-BEARING FROM HERE AND NOT FROM THE ENUM. The
 * `_Static_assert` in fma.h pins the count against the last NAMED code, which
 * cannot see a code added at -22 while the count stays 21; this case and the
 * death case `an_unknown_64_lane_operand_code` are the pair that can. Here:
 * every code the count DECLARES must resolve, so a count that is too large
 * reaches `const_of`'s `default:` and aborts. There: `-(N_CODE + 1)` must
 * abort, so a count that is too small stops the abort and goes red.
 *
 * THE THREE RAILS ARE REAL ARRAYS HERE, unlike everywhere else in this file,
 * because `CQ_FU_A`/`_B`/`_C` are codes too and `base64` refuses a NULL rail
 * with its own message — which would pass this case for the wrong reason. */
CQ_TEST(every_declared_operand_code_resolves)
{
    static cq_bit rail[3][CQ_FP64_W];
    cq_bit buf[CQ_FP64_W], tmp[CQ_FP64_W];
    uint32_t off[CQ_FMA_MAX_ROWS];
    cq_fma_block k;
    cq_scratch scr;
    int n_ok = 0;

    for (int r = 0; r < 3; r++)
        for (int i = 0; i < W64; i++) rail[r][i] = cq_bit_zero();
    cq_scratch_alloc(&scr, cq_fma_region());
    k.a = rail[0]; k.b = rail[1]; k.c = rail[2]; k.scr = &scr; k.off = 0u;
    cq_fu_arm(&k, off);

    for (int s = -1; s >= -CQ_FU_N_CODE; s--) {
        const cq_bit *p = cq_fu_op64(&k, off, s, buf, tmp);

        CHECK(p != NULL);
        n_ok++;
    }
    CHECK_EQ(n_ok, CQ_FU_N_CODE);
    cq_scratch_dispose(&scr);
}

/* ---- (b) The three reachability proofs. -------------------------------- */

/* EVERY VIEW CHAIN TERMINATES WELL INSIDE THE GUARD. `cq_fu_view_collapse`
 * counts hops and dies past `n`, which can only happen on a CYCLE — so the
 * claim that makes that refusal unreachable is a bound on the longest chain.
 * The bound is asserted against `n` (the guard's own threshold) and the
 * measured maximum is required to be at least 2, because a table with no
 * NESTED views would satisfy the bound vacuously and K20 nests them three deep
 * in `_shr128jam_by1` and in the CLZ ladder. */
CQ_TEST(every_view_chain_terminates_well_inside_the_guard)
{
    const cq_fma_row *rows;
    cq_fma_block k;
    cq_scratch scr;
    int n, deepest = 0, n_views = 0;

    cq_scratch_alloc(&scr, cq_fma_region());
    k.a = NULL; k.b = NULL; k.c = NULL; k.scr = &scr; k.off = 0u;

    rows = cq_fma_rows(&n);
    for (int i = 0; i < n; i++) {
        int hops = 0, s0 = i, shift = 0, term;
        uint64_t mask = 0u;

        if (rows[i].op != CQ_FUOP_VIEW) continue;
        n_views++;
        while (s0 >= 0 && s0 < n && rows[s0].op == CQ_FUOP_VIEW) {
            s0 = rows[s0].s0;
            if (++hops > n) {
                cq_h_fail(__FILE__, __LINE__,
                          "view row %d does not terminate in %d hops", i, n);
                break;
            }
        }
        if (hops > deepest) deepest = hops;
        /* The module's own walk agrees on where the chain ends — so the
         * bound above is a bound on what `cq_fu_view_collapse` walks and not
         * on a re-derivation that happens to look like it. */
        term = cq_fu_view_collapse(&k, i, &shift, &mask);
        CHECK_EQ(term, s0);
    }
    CHECK(n_views > 0);
    CHECK(deepest >= 2);
    CHECK(deepest < n);
    cq_scratch_dispose(&scr);
}

/* A ROW EMITS IF AND ONLY IF ITS OP HAS A DISPATCH ARM, which is what makes
 * `emit_row`'s trailing `cq_kernel_die` and `cq_fma_step`'s zero-slot refusal
 * unreachable over this table. The complement is stated rather than the
 * nineteen handled ops, deliberately: listing the arms here would be a second
 * transcription of the switch, and the switch's fall-through set is exactly
 * {VIEW, OUT, N_OP}. */
CQ_TEST(a_row_emits_exactly_when_its_op_has_a_dispatch_arm)
{
    const cq_fma_row *rows;
    int n, n_zero = 0, n_emit = 0;

    rows = cq_fma_rows(&n);
    for (int i = 0; i < n; i++) {
        int zero = (rows[i].op == CQ_FUOP_VIEW || rows[i].op == CQ_FUOP_OUT);

        CHECK(rows[i].op >= 0 && rows[i].op < (short)CQ_FUOP_N_OP);
        if (cq_fu_row_steps(&rows[i]) == 0) {
            if (!zero)
                cq_h_fail(__FILE__, __LINE__,
                          "row %d owns no slot and is not a view or a "
                          "projection — the dispatch would refuse it", i);
            n_zero++;
        } else {
            if (zero)
                cq_h_fail(__FILE__, __LINE__,
                          "row %d is a view or a projection and owns slots — "
                          "`emit_row` has no arm for it", i);
            n_emit++;
        }
    }
    CHECK(n_zero > 0);
    CHECK(n_emit > 0);
    CHECK_EQ(n_zero + n_emit, n);
}

/* AND THE LAST ROW IS A SPAN, which is what `cq_fma_result`'s two refusals
 * exist to enforce and what no caller can provoke: it is a property of the
 * table's final row (`:203`'s outermost `mux`) and of nothing a caller passes.
 * Asserted as the positive claim plus the executed consequence — the pointer
 * `cq_fma_result` returns lies inside the caller's region, which is the whole
 * of "a span" and is exactly what the `p == buf` refusal rules out. */
CQ_TEST(the_result_row_is_a_span_inside_the_caller_s_region)
{
    const cq_fma_row *rows;
    const cq_bit *p;
    cq_fma_block k;
    cq_scratch scr;
    int n;

    rows = cq_fma_rows(&n);
    CHECK(rows[n - 1].op != CQ_FUOP_VIEW);
    CHECK(rows[n - 1].op != CQ_FUOP_OUT);
    CHECK_EQ(cq_fma_row_width(rows, n, n - 1), W64);

    cq_scratch_alloc(&scr, cq_fma_region());
    k.a = NULL; k.b = NULL; k.c = NULL; k.scr = &scr; k.off = 0u;
    p = cq_fma_result(&k);
    CHECK(p >= scr.bits);
    CHECK(p + W64 <= scr.bits + scr.n);
    cq_scratch_dispose(&scr);
}
