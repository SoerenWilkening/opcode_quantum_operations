/* tests/test_shim_cert_rules.inc — D15's ENTRY CONDITIONS, on the seam
 * IMPLEMENTATION_PLAN §3 recorded before tests/test_shim_cert.c's first case:
 * `the ENGINE ↔ the ENTRY CONDITIONS`. Included by that file, so it shares its
 * fixture, its `push` helper and its `RED` macro.
 *
 * EVERY CASE HERE DRIVES A REAL `cqrt_*` OR `cq_shim_*` SYMBOL, which is the
 * discriminator: these are the cases that would not exist if D15 had only ever
 * stated ONE rule. The `.c` half drives shim/cq_shim_reduce.c directly and
 * names no entry point.
 *
 * THE ENGINE'S TWO REMAINING GUARDS ARE HERE RATHER THAN IN THE `.c` FOR ONE
 * REASON: `cqrt_cswap` is the only opcode in the v1 model that writes two
 * rails, so the co-written check and the flag-branch commutation can be driven
 * through a real symbol, and driving the real symbol is strictly better
 * evidence than a synthesised history. */

/* -------------------------------------------------------------------------
 * The two guards that need cqrt_cswap.
 * ------------------------------------------------------------------------- */

CQ_TEST(the_cswap_co_written_slot_is_checked_and_it_is_a_miscompile_fix)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    uint32_t b;
    (void)ctx;

    /* THE 2026-08-07 MISCOMPILE, REBUILT. Until upstream added
     * `co_written_stable`, a route-pair cancelled with only the FLAG ever
     * checked, so `cswap(f,q,rail); ry(q,0.75); cswap(f,q,rail); free(rail)`
     * reported reduces-to-identity at exit 0 while the route-back parked
     * `Ry(0.75)|0>` on the rail it then freed. `cqrt_cswap` is modelled
     * reads=(0,) writes=(1,2) — a Fredkin physically reads all three — so `q`
     * is a CO-WRITTEN slot of the pair and not a declared read, and only the
     * co-written check reaches it.
     *
     * Built from the record rather than from the symbol because the shim's
     * `cqrt_cswap` refuses `ctrl == a`, `ctrl == b` and `a == b`, and the shape
     * needs `q` to be a data arg that also moves — which is legal and is
     * exactly what the guard is about. */
    (void)born(7);            /* the flag  */
    (void)born(8);            /* q — the co-written slot */
    b = born(9);              /* the rail being freed    */
    push(CQ_ROP_CSWAP, 7, 8, 9, 0u, 0.0, -1, 0u);
    push(CQ_ROP_RY,    8, -1, -1, 0u, 0.75, -1, 0u);   /* q MOVES */
    push(CQ_ROP_CSWAP, 7, 8, 9, 0u, 0.0, -1, 0u);
    CHECK(!RED(9, b));

    /* THE NEGATIVE CONTROL, and it is what makes the case an assertion about
     * the CO-WRITTEN slot rather than about `cswap` being unpairable: without
     * the intervening rotation the identical two calls reduce. */
    (void)born(10);
    (void)born(11);
    push(CQ_ROP_CSWAP, 7, 10, 11, 0u, 0.0, -1, 0u);
    push(CQ_ROP_CSWAP, 7, 10, 11, 0u, 0.0, -1, 0u);
    CHECK(RED(11, cq_rec_hist(11)->birth_pos));

    close_with(&m);
}

CQ_TEST(complementary_flag_branches_commute_and_nothing_else_does)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    uint32_t b;
    (void)ctx;

    /* COMMUTATION IS AN ACCEPT ARM: dropping it does not admit a wrong build,
     * it REJECTS a correct one — "for a GATE, that is failing a good build".
     * So the case that proves it has to be one that reduces ONLY BECAUSE of
     * commutation, and the negative half has to be the same calls with the
     * admission's precondition removed.
     *
     * THE SHAPE IS THE CORPUS'S OWN two-arm route: `A B A B`, where `A` and `B`
     * are controlled copies into one rail on COMPLEMENTARY branches of one
     * flag, separated by the `¬flag cqrt_x` bracket the emitter puts around the
     * false arm. Neither pair is adjacent, so both pairings need the
     * intervening write to commute past them — and it does, because two
     * operators supported on complementary blocks of one flag commute exactly.
     *
     * NOTE THIS IS AN `A B A B`, WHICH THE OTHER CASE IN THIS SUITE ASSERTS
     * DOES *NOT* REDUCE. Both are right and the difference is the whole
     * admission: there the writes carry no control at all, so there is no flag
     * to be on complementary branches of. Read the two cases together. */
    (void)born(5);                                       /* the flag */
    b = born(6);
    push(CQ_ROP_COPY_CTRL, 5, 2, 6, 0u, 0.0, -1, 0u);    /* A, parity 0 */
    push(CQ_ROP_X,         5, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 5, 3, 6, 0u, 0.0, -1, 0u);    /* B, parity 1 */
    push(CQ_ROP_X,         5, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 5, 2, 6, 0u, 0.0, -1, 0u);    /* A, parity 0 */
    push(CQ_ROP_X,         5, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 5, 3, 6, 0u, 0.0, -1, 0u);    /* B, parity 1 */
    CHECK(RED(6, b));

    /* PARITY IS MOD 2 AND THE FOUR `cqrt_x` CALLS ABOVE PROVE IT, which is a
     * second claim riding on the same stream: the flag is flipped FOUR times,
     * so an unmasked running counter would report the last `A` at 2 and the
     * first at 0 and refuse the pair. The pinned source masks with `& 1` and
     * says why ("only the POLARITY matters, not how many brackets deep it
     * is"); the port had the raw counter until this case was run. */

    /* THE NEGATIVE HALF: the same four copies with the brackets REMOVED. Every
     * write is now on parity 0, so `commutes` finds no complementary branch and
     * the identical interleaving declines. The two halves differ by exactly the
     * admission's precondition, which is what makes this an assertion about
     * commutation rather than about `A B A B` in general. */
    (void)born(12);
    push(CQ_ROP_COPY_CTRL, 5, 2, 12, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 5, 3, 12, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 5, 2, 12, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 5, 3, 12, 0u, 0.0, -1, 0u);
    CHECK(!RED(12, cq_rec_hist(12)->birth_pos));

    /* AND THE ADMISSION IS EXACTLY "NOTHING BUT `cqrt_x` TOUCHED THE FLAG IN
     * BETWEEN". Re-run the positive stream with one non-`cqrt_x` write on the
     * flag inside a window and it declines again — the flag's polarity at the
     * second half is no longer something the parity claim is backed by. */
    (void)born(13);
    push(CQ_ROP_COPY_CTRL, 5, 2, 13, 0u, 0.0, -1, 0u);
    push(CQ_ROP_X,         5, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 5, 3, 13, 0u, 0.0, -1, 0u);
    push(CQ_ROP_RY,        5, -1, -1, 0u, 0.5, -1, 0u);  /* NOT a cqrt_x */
    push(CQ_ROP_X,         5, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 5, 2, 13, 0u, 0.0, -1, 0u);
    push(CQ_ROP_X,         5, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 5, 3, 13, 0u, 0.0, -1, 0u);
    CHECK(!RED(13, cq_rec_hist(13)->birth_pos));

    /* AND THE `NOTHING BUT cqrt_x` CLAUSE HAS ITS OWN DISCRIMINATOR, because
     * without one it is invisible. A mutant that makes `commutes` accept ANY
     * intervening write survived the whole suite: on a minted flag the READS
     * loop recurses into the flag's history one layer down and rejects the pair
     * anyway, so the clause is masked — the recorded "a cheaper assertion hides
     * an expensive one" trap, at a new site.
     *
     * THE GAP BETWEEN THE TWO IS THAT `commutes` DOES NOT REDUCE — it demands
     * that EVERY intervening write to the flag be a `cqrt_x`, paired or not,
     * while the READS check merely demands the flag's history REDUCE. So a
     * CANCELLING pair of non-`cqrt_x` writes on the flag satisfies the second
     * and fails the first, and that is the only shape that separates them.
     *
     * `A B A B` again, so both pairings need commutation, with an `Ry(θ)` and
     * its exact negation sitting inside the first pair's window. Without the
     * clause the stream REDUCES; with it, the first pair is blocked and one
     * write is left unpaired. The mutant's direction is the unsound one — it
     * discharges — which is why this arm is worth its length. */
    (void)born(14);
    push(CQ_ROP_COPY_CTRL, 5, 2, 14, 0u, 0.0, -1, 0u);   /* A, parity 0 */
    push(CQ_ROP_X,         5, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 5, 3, 14, 0u, 0.0, -1, 0u);   /* B, parity 1 */
    push(CQ_ROP_RY,        5, -1, -1, 0u,  0.5, -1, 0u); /* NOT cqrt_x   */
    push(CQ_ROP_RY,        5, -1, -1, 0u, -0.5, -1, 0u); /* ...but it cancels */
    push(CQ_ROP_X,         5, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 5, 2, 14, 0u, 0.0, -1, 0u);   /* A, parity 0 */
    push(CQ_ROP_X,         5, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 5, 3, 14, 0u, 0.0, -1, 0u);   /* B, parity 1 */
    CHECK(!RED(14, cq_rec_hist(14)->birth_pos));

    /* THE NEGATIVE CONTROL, and it is the same nine calls with the two `Ry`s
     * replaced by two `cqrt_x`s — which cancel just as exactly and ARE the
     * admitted opcode. It reduces. Without this the arm above would be
     * satisfied by an engine that had simply stopped reducing anything with a
     * rotation in it. */
    (void)born(16);
    push(CQ_ROP_COPY_CTRL, 5, 2, 16, 0u, 0.0, -1, 0u);
    push(CQ_ROP_X,         5, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 5, 3, 16, 0u, 0.0, -1, 0u);
    push(CQ_ROP_X,         5, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_X,         5, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_X,         5, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 5, 2, 16, 0u, 0.0, -1, 0u);
    push(CQ_ROP_X,         5, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 5, 3, 16, 0u, 0.0, -1, 0u);
    CHECK(RED(16, cq_rec_hist(16)->birth_pos));

    close_with(&m);
}

/* -------------------------------------------------------------------------
 * U1 — the `_unc` shape, through the real template surface.
 * ------------------------------------------------------------------------- */

CQ_TEST(u1_discharges_a_template_forward_and_its_uncompute)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    int32_t a, b2, out;
    uint32_t held[8]; uint32_t nh = 0;
    const cq_bit *ob;

    /* THE HEADLINE. Before landing 2 this free STRANDED every qubit of `out`:
     * the shadow reports `unknown` for anything a kernel wrote through a
     * quantum source, so `cq_shim_shadow_proof` could discharge nothing here.
     * The certificate reads the CALL STREAM instead and clears it. */
    a   = cqrt_alloc_i8(0x5A);
    b2  = cqrt_alloc_i8(0x33);
    cqrt_ry_i8(a, 0.4);       /* make `a` genuinely quantum, and poison it */
    out = cq_shim_bin_qq(CQ_SHIM_OP_AND, 8, a, b2);

    ob = cq_reg_cbits(&ctx->regs, out);
    for (uint32_t i = 0; i < 8u; i++)
        if (cq_bit_is_qubit(ob[i])) held[nh++] = cq_bit_qindex(ob[i]);
    CHECK(nh > 0u);           /* the case is vacuous if it owns no qubit */

    /* THE SHADOW ALONE CANNOT: assert that, so "the certificate discharged it"
     * is a claim about the certificate rather than about an easy rail. */
    CHECK_EQ(cq_reg_disposition(ctx, out, cq_shim_shadow_proof), 0);

    cq_shim_bin_qq_unc(CQ_SHIM_OP_AND, 8, out, a, b2);
    CHECK(cq_reg_disposition(ctx, out, cq_shim_certificate) > 0);

    cqrt_free(out);
    for (uint32_t i = 0; i < nh; i++) {
        CHECK(cq_qubits_is_free(&ctx->pool, held[i]));
        CHECK(!cq_qubits_is_stranded(&ctx->pool, held[i]));
    }
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);

    close_with(&m);
}

CQ_TEST(u1_refuses_the_same_forward_with_no_uncompute)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    int32_t a, b2, out;

    /* THE NAMED NEGATIVE CONTROL, in its M26 spelling. `bd 06t` requires that
     * the certificate not degenerate into "always yes", and names
     * tests/test_unc_death.c:free_without_the_uncompute for it at the kernel
     * layer; this is the same claim at the layer the certificate actually runs.
     * The two streams differ by exactly one call. */
    a   = cqrt_alloc_i8(0x5A);
    b2  = cqrt_alloc_i8(0x33);
    cqrt_ry_i8(a, 0.4);
    out = cq_shim_bin_qq(CQ_SHIM_OP_AND, 8, a, b2);

    CHECK_EQ(cq_reg_disposition(ctx, out, cq_shim_certificate), 0);
    CHECK_EQ(cq_reg_disposition(ctx, out, cq_shim_free_proof), 0);

    /* AND IT STRANDS RATHER THAN ABORTING (D15 §4's confirmed clause), which
     * is the half a death test cannot assert. */
    cqrt_free(out);
    CHECK(cq_qubits_stranded(&ctx->pool) > 0u);
    CHECK_EQ(cq_reg_strand_reports(ctx), 1);
    CHECK_EQ(cq_reg_frees_unproven(ctx), 1);
    CHECK_EQ(cq_reg_frees_dirty(ctx), 0);

    close_with(&m);
}

/* -------------------------------------------------------------------------
 * U2 and U3.
 * ------------------------------------------------------------------------- */

CQ_TEST(u2_discharges_a_self_inverse_bracket_on_an_alloc_zero_rail)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    int32_t f, h;
    uint32_t q;

    /* THE `¬flag cqrt_x` BRACKET, which D15 §2 measures as the corpus's single
     * commonest intervening-write shape and which U1-as-first-written was too
     * strict to admit. It is U2's population: born |0>, written twice by a
     * self-inverse gate on unchanged operands, freed. */
    f = cqrt_alloc_i1(1);
    cqrt_ry_i1(f, 0.3);                    /* a genuinely quantum flag */
    h = cqrt_alloc_i1(0);
    cqrt_cnot(f, h);
    cqrt_cnot(f, h);

    q = cq_bit_qindex(cq_reg_cbits(&ctx->regs, h)[0]);
    CHECK(cq_bit_is_qubit(cq_reg_cbits(&ctx->regs, h)[0]));
    CHECK_EQ(cq_reg_disposition(ctx, h, cq_shim_shadow_proof), 0);   /* blind */
    CHECK(cq_reg_disposition(ctx, h, cq_shim_certificate) > 0);

    cqrt_free(h);
    CHECK(cq_qubits_is_free(&ctx->pool, q));
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0);

    close_with(&m);
}

CQ_TEST(u3_is_arithmetic_and_convicts_when_the_sum_is_not_zero)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    int32_t h;

    /* U3 IS THE ONE RULE WITH NO REDUCTION IN IT: "born cqrt_alloc_W(L), sum of
     * cqrt_addc immediates is -L, no other write". It is an ENTRY CONDITION —
     * "not this shape" falls through to the reduction — and only the value it
     * computes is an answer.
     *
     * AND IT IS WHERE THE PORT'S OBLIGATION DIFFERS FROM UPSTREAM'S. Upstream's
     * A1 discharges a rail returning to its ALLOC-TIME value, so `alloc(5);
     * addc(-3)` verifies there and lands on |2>. Rule 6 needs |0>, so here the
     * same stream is a CONVICTION. That is not a patch on upstream: it is what
     * makes ckd.18 provable, below. */
    /* ASKED OF THE CERTIFICATE DIRECTLY, NOT THROUGH cq_reg_disposition, and
     * the difference is I4 rather than style: `addc` on an all-classical rail
     * FOLDS (PRD §15 D17 makes that mandatory), so the rail owns no qubits, so
     * cq_reg_disposition skips every bit and answers CLEAN having never
     * consulted the proof at all. Routing this case through it would assert
     * that I4 works — which it does, and which another case already covers —
     * while saying nothing about U3. Measured: the conviction row read as CLEAN
     * and looked like a certificate bug. */
    h = cqrt_alloc_i8(5);
    cqrt_addc_i8(h, (int8_t)-5);
    CHECK(cq_shim_certificate(ctx, h, 0u) > 0);
    CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, h), 0u);   /* I4, said out loud */
    cqrt_free(h);

    h = cqrt_alloc_i8(5);
    cqrt_addc_i8(h, (int8_t)-3);
    CHECK(cq_shim_certificate(ctx, h, 0u) < 0);
    cqrt_free(h);

    /* Both rails are all-classical, so by I4 they own no qubit and NOTHING
     * strands either way — which is the point of the assertion: `bd 06t`
     * forbids the certificate from regressing the I4 frees, and a conviction
     * that stranded a rail owning no qubits would be exactly that regression.
     * The RAIL row still records the conviction, because the verdict survives
     * even where the act has nothing to do. */
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0);

    close_with(&m);
}

CQ_TEST(ckd18s_cancelled_rotation_pair_is_a_conviction_not_an_absence)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    int32_t h, z;
    uint32_t q;

    /* PRD §15 D15 §4's CARVE-OUT, BUILT. `alloc_i32(5); ry(θ); ry(−θ); free` —
     * born from a NON-ZERO literal, `n_ry == 2`, `sum(ry) == 0.0` exactly,
     * physically |5> at the free. D15 says these rails are "not unprovable":
     * the same call stream gives the birth literal, the cancelling pair and the
     * absence of any other write, so the rail is PROVABLY DIRTY. "The shadow
     * could never see this — it reports `unknown`; the certificate can."
     *
     * THE MECHANISM IS THE BIRTH VALUE AND NOTHING ELSE, which is why this case
     * and the one after it differ in a single literal. Same rules, same engine,
     * same reduction — opposite verdicts. */
    h = cqrt_alloc_i8(5);
    cqrt_ry_i8(h,  0.9);
    cqrt_ry_i8(h, -0.9);
    q = cq_bit_qindex(cq_reg_cbits(&ctx->regs, h)[0]);
    CHECK_EQ(cq_reg_disposition(ctx, h, cq_shim_shadow_proof), 0);   /* blind */
    CHECK(cq_reg_disposition(ctx, h, cq_shim_certificate) < 0);      /* SEES  */

    cqrt_free(h);
    CHECK(cq_qubits_is_stranded(&ctx->pool, q));
    CHECK(!cq_qubits_is_free(&ctx->pool, q));
    CHECK_EQ(cq_reg_frees_dirty(ctx), 1);
    CHECK_EQ(cq_reg_frees_unproven(ctx), 0);
    CHECK(cq_reg_stranded_dirty(ctx) > 0u);
    CHECK_EQ(cq_reg_stranded_unproven(ctx), 0);

    /* THE SAME STREAM FROM A ZERO LITERAL CLEARS — and this is the case
     * `bd 06t` asks be INVERTED into U1's positive row: the only place in the
     * tree where the freed rail was never itself rotated is nearby, and here
     * the rail WAS rotated and is still discharged, because the pair cancels
     * and the birth value is zero. */
    z = cqrt_alloc_i8(0);
    cqrt_ry_i8(z,  0.9);
    cqrt_ry_i8(z, -0.9);
    CHECK(cq_reg_disposition(ctx, z, cq_shim_certificate) > 0);

    close_with(&m);
}

CQ_TEST(a_read_after_a_rotation_is_unproven_and_upstream_lets_it_through)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    int32_t h, k;

    /* THE ONE PLACE THIS PORT IS SOUNDER THAN THE ANALYSIS IT WAS PORTED FROM,
     * and it is worth a case of its own because that is a strong claim.
     *
     * PRD §15 D15 §5 demonstrates the hole with a runnable input: adding one
     * `cqrt_cnot(%h,%k)` between the two `ry` calls of upstream's own
     * must-verify positive fixture leaves its verdict at exit 0 while exact
     * two-qubit simulation gives P(h ≠ |0>) = 0.1149. The reduction reasons
     * over WRITES to a rail and never asks what READ it, so the two rotations
     * still pair.
     *
     * Upstream patches two shapes of this with an ad-hoc rule layered on top
     * (R1), computed by scanning the WHOLE function for "kept carriers" —
     * information that does not exist when `cqrt_free` arrives. So the port
     * cannot inherit R1 and must not simply drop it, which would WIDEN us. It
     * refuses instead on a PAST-ONLY witness: a rail carrying a non-diagonal
     * rotation that was READ after it. That is strictly stronger than R1 (a
     * rail reaches `recorded` only by being copied or taped, which is a read)
     * and it covers this shape, which R1 does not.
     *
     * WHAT IT STILL DOES NOT COVER, said out loud: the same entanglement with
     * no rotation anywhere. Rule 13 forecloses the instrument that would see
     * that, D15 §5 says so, and nothing here claims otherwise. */
    h = cqrt_alloc_i1(0);
    k = cqrt_alloc_i1(0);
    cqrt_ry_i1(h, 0.75);
    cqrt_cnot(h, k);              /* the READ that entangles */
    cqrt_ry_i1(h, -0.75);

    /* The rotations DO still pair — the engine is faithful — so this is the
     * substitute rule firing and not the reduction failing. Asserting both is
     * what makes it an assertion about the rule. */
    CHECK(cq_reduce_to_identity(h, cq_rec_hist(h)->birth_pos, cq_rec_len()));
    CHECK_EQ(cq_reg_disposition(ctx, h, cq_shim_certificate), 0);

    /* AND A READ *BEFORE* THE ROTATION IS FINE, which is what makes the rule a
     * rule about ordering rather than a blanket refusal of every read rail. */
    {
        int32_t g = cqrt_alloc_i1(0), j = cqrt_alloc_i1(0);
        cqrt_cnot(g, j);
        cqrt_cnot(g, j);
        cqrt_ry_i1(g, 0.75);
        cqrt_ry_i1(g, -0.75);
        CHECK(cq_reg_disposition(ctx, g, cq_shim_certificate) > 0);
    }

    close_with(&m);
}

/* -------------------------------------------------------------------------
 * The two properties the certificate must NOT have.
 * ------------------------------------------------------------------------- */

CQ_TEST(a_handle_the_recorder_never_saw_is_unproven_never_clean)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    int32_t h;

    /* THE "ALWAYS YES" DEGENERATION, IN ITS QUIETEST FORM. Not every rail comes
     * through a recorded entry point — this case allocates through M07
     * directly, exactly as a kernel suite does — and a certificate that read an
     * empty history, reduced it vacuously and answered CLEAN would release a
     * rail it had never examined. The answer must be UNPROVEN.
     *
     * It is the same failure an OMITTED hook produces, which is why the record
     * is taken at every write site rather than at the interesting ones. */
    h = cq_reg_alloc_zero(&ctx->regs, 4);
    cq_materialise(ctx, &cq_reg_bits(&ctx->regs, h)[0]);
    CHECK_EQ(cq_shim_certificate(ctx, h, 0u), 0);
    CHECK_EQ(cq_reg_disposition(ctx, h, cq_shim_certificate), 0);

    /* The SHADOW still discharges it — it is a determinate zero — so the
     * combined proof releases. That is the combination working: two sound
     * predicates with different completeness, either one's proof sufficing. */
    CHECK(cq_reg_disposition(ctx, h, cq_shim_free_proof) > 0);

    close_with(&m);
}

CQ_TEST(the_d7b_temporary_is_discharged_rather_than_stranded)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    int32_t a, out;

    /* D7b IS LEGAL AND MEASURED IN THE CORPUS (599 occurrences, 10 on v1's
     * integer surface), and its defensive copy mints a rail CQ_lang never sees
     * — D15's "since it was MINTED is not since its cqrt_alloc" in the shim's
     * own code. Its history is: born |0>, `⊕= src`, read as a kernel source,
     * `⊕= src`, freed. Before landing 2 that free stranded W qubits on every
     * aliased call; the two copies pair, the source is a kernel SOURCE and so
     * unchanged, and the birth value is zero, so the certificate clears it.
     *
     * `a` IS ROTATED FIRST so the temporary genuinely owns qubits — with an
     * all-classical source the copy is I4 and the case is vacuous. */
    a = cqrt_alloc_i8(0x5A);
    cqrt_ry_i8(a, 0.4);
    out = cq_shim_bin_qq(CQ_SHIM_OP_AND, 8, a, a);   /* D7b fires */

    /* The temporary was minted, used and freed inside that one call, so the
     * only observable is the pool: nothing may have stranded. Before landing 2
     * this was exactly W. */
    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0);
    CHECK(out != CQ_REG_NONE);

    close_with(&m);
}

/* -------------------------------------------------------------------------
 * The guards a first battery found UNTESTED. Every case below exists because a
 * mutant survived, and each says which one.
 * ------------------------------------------------------------------------- */

CQ_TEST(a_gate_on_the_complementary_branch_of_its_flag_is_not_the_adjoint)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    uint32_t b;
    (void)ctx;

    /* THE MUTANT: delete `adjoint_matches`'s parity comparison. It SURVIVED the
     * first battery, and finding out why is worth more than the case that kills
     * it.
     *
     * PARITY IS DEFENCE IN DEPTH OVER THE READS CHECK FOR EVERY MINTED FLAG,
     * and that is a consequence of `controls ⊆ reads` rather than a coincidence.
     * Two writes carry DIFFERENT parities exactly when an ODD number of
     * `cqrt_x` calls separates them; an odd number of self-inverse writes
     * cannot reduce; the flag is a declared READ, so `pair_operands_unchanged`
     * recurses into its history and rejects the pair anyway. Deleting the
     * parity test therefore changes nothing on any stream whose flag this
     * stream minted — which is every stream the first battery ran.
     *
     * THE ROW WHERE IT IS NOT REDUNDANT is a flag the recorder never saw. The
     * READS loop SKIPS an operand with no history — "its write history is not
     * this stream's to know" — so nothing else is left to notice that the two
     * writes sit on opposite branches. That is not a contrived row: a control
     * flag reaching us from outside a recorded region is exactly the shape the
     * §9 bracket's own parameter exists for.
     *
     * This is the recorded "the masking copy of a guard can be EARLIER in the
     * call chain" lesson, at a new site: the guard is real, its coverage is one
     * row wide, and a battery that never builds that row reports it as dead. */
    b = born(20);
    push(CQ_ROP_COPY_CTRL, 99, 98, 20, 0u, 0.0, -1, 0u);   /* flag 99: unminted */
    push(CQ_ROP_X,         99, -1, -1, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 99, 98, 20, 0u, 0.0, -1, 0u);   /* opposite branch  */
    CHECK(!RED(20, b));

    /* THE NEGATIVE CONTROL: the identical two writes with NO flip between them
     * DO pair, on the same unminted flag. Without it "not the adjoint" is
     * indistinguishable from "an unminted flag refuses everything". */
    (void)born(21);
    push(CQ_ROP_COPY_CTRL, 99, 98, 21, 0u, 0.0, -1, 0u);
    push(CQ_ROP_COPY_CTRL, 99, 98, 21, 0u, 0.0, -1, 0u);
    CHECK(RED(21, cq_rec_hist(21)->birth_pos));

    close_with(&m);
}

CQ_TEST(cqrt_cnot_controlled_reads_its_inner_control_and_writes_only_arg_two)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    int32_t f, tc, t;

    /* THE MUTANT: give `cqrt_cnot_controlled` the read set its NAME suggests —
     * drop `target_ctrl` from `reads`. It survived because NOTHING in the tree
     * drove that symbol through the certificate, which is unsurprising: the
     * corpus contains zero calls to it. An unexercised effect-table row is an
     * unchecked transcription of the frozen ABI, and D15 §6(ii) is entirely
     * about exactly that class of error.
     *
     * THE WORD "target" INSIDE A PARAMETER NAMED `target_ctrl` IS THE INNER
     * CONTROL. `cq_runtime.h` declares `cqrt_cnot_controlled(ctrl,
     * target_ctrl, tgt)` and the write is arg 2. With `target_ctrl` missing
     * from the read set, a pair of these calls cancels without ever asking
     * whether the inner control moved in between — and here it does. */
    f  = cqrt_alloc_i1(0);
    tc = cqrt_alloc_i1(0);
    t  = cqrt_alloc_i1(0);
    cqrt_ry_i1(f, 0.3);            /* a genuinely quantum outer flag */

    cqrt_cnot_controlled(f, tc, t);
    cqrt_x(tc);                    /* the INNER CONTROL moves */
    cqrt_cnot_controlled(f, tc, t);
    CHECK_EQ(cq_shim_certificate(ctx, t, 0u), 0);

    /* THE NEGATIVE CONTROL: leave the inner control alone and the same two
     * calls reduce. */
    {
        int32_t t2 = cqrt_alloc_i1(0);
        cqrt_cnot_controlled(f, tc, t2);
        cqrt_cnot_controlled(f, tc, t2);
        CHECK(cq_shim_certificate(ctx, t2, 0u) > 0);
    }

    close_with(&m);
}

CQ_TEST(a_constant_one_cswap_swaps_the_histories_with_the_contents)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    int32_t f, a, b2;

    /* THE MUTANT: delete `cq_rec_swap`'s body. It survived because nothing
     * drove `cqrt_cswap`'s CONSTANT-ONE row and then asked the certificate
     * about either rail.
     *
     * `cq_reg_swap_bits` EXCHANGES THE TWO RAILS' BIT ARRAYS — zero gates, zero
     * qubits, which is what PRD §2.1 prices the constant row at — so after it
     * handle `a` holds what `b` held. A history keyed by handle must move with
     * the contents or the certificate reasons about the wrong rail, SILENTLY:
     * both rails are still live, still the right width, and every pool check
     * still passes.
     *
     * The shape below makes the two rails DISTINGUISHABLE to the certificate:
     * `a` carries a cancelling `cqrt_x` bracket (its history reduces) and `b`
     * carries a single uncancelled one (its history does not). After the swap
     * the verdicts must have followed the bits. */
    f  = cqrt_alloc_i1(1);         /* a CONSTANT ONE flag — the row under test */
    a  = cqrt_alloc_i1(0);
    b2 = cqrt_alloc_i1(0);

    cqrt_ry_i1(a, 0.3);            /* materialise both, so neither is I4 */
    cqrt_ry_i1(a, -0.3);
    cqrt_ry_i1(b2, 0.3);

    CHECK(cq_shim_certificate(ctx, a, 0u) > 0);    /* reduces */
    CHECK_EQ(cq_shim_certificate(ctx, b2, 0u), 0); /* does not */

    cqrt_cswap(f, a, b2);

    /* THE VERDICTS FOLLOWED THE BITS. Without cq_rec_swap they would not have
     * moved at all, and both assertions above would still read the same way —
     * which is exactly why the mutant was invisible. */
    CHECK_EQ(cq_shim_certificate(ctx, a, 0u), 0);
    CHECK(cq_shim_certificate(ctx, b2, 0u) > 0);

    close_with(&m);
}

CQ_TEST(the_birth_value_is_read_at_the_full_register_width)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    (void)ctx;

    /* THE MUTANT: read only `birth_lo` and ignore `birth_hi`. It survived
     * because no rail in the tree reaches the certificate carrying a high word:
     * the ABI has no `cqrt_alloc_i128` at all — i128 has no `cqrt_*` core
     * symbol — and a register only reaches 128 bits through the template
     * surface, where the mint is at |0>.
     *
     * SO THE ROW IS UNREACHABLE FROM THE ENTRY POINTS TODAY AND IS TESTED FROM
     * THE RECORD, deliberately. It is not hypothetical: `cq_reg_alloc_const`
     * takes two words because PRD §14's register reaches 128 bits, and the day
     * a caller mints a wide rail with a high-word literal, a `birth_lo`-only
     * certificate CLEARS it — a release, which is the unsound direction. */
    cq_rec_mint(30, 128u, 0u, 1u, 0);          /* |2^64>, low word zero */
    CHECK(cq_shim_certificate(ctx, 30, 0u) < 0);

    cq_rec_mint(31, 128u, 0u, 0u, 0);          /* the same shape at zero */
    CHECK(cq_shim_certificate(ctx, 31, 0u) > 0);

    close_with(&m);
}

CQ_TEST(u3_distinguishes_xorc_from_addc_and_the_two_differ_on_a_carry)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    int32_t h;

    /* THE MUTANT: fold `cqrt_xorc` into U3's arithmetic as an ADD. It survived
     * because the only U3 case drove `addc` alone — and `^` and `+` agree on
     * every operand pair with no carry, which is most of them.
     *
     * THE ROW THAT SEPARATES THEM IS A CARRY. 3 + 3 = 6 and 3 ^ 3 = 0 at any
     * width, so a rail born at 3 and xorc'd by 3 is CLEAN while the same rail
     * addc'd by 3 is not — opposite verdicts from one substitution. Both are
     * upstream's A1 population and both must be walked, because separating them
     * would make a rail's verdict depend on which classical opcode CQ_lang
     * happened to emit. */
    h = cqrt_alloc_i8(3);
    cqrt_xorc_i8(h, (int8_t)3);
    CHECK(cq_shim_certificate(ctx, h, 0u) > 0);
    cqrt_free(h);

    h = cqrt_alloc_i8(3);
    cqrt_addc_i8(h, (int8_t)3);
    CHECK(cq_shim_certificate(ctx, h, 0u) < 0);
    cqrt_free(h);

    /* AND THE MIXED WALK, since U3 admits both in one history: 3, +5, ^8 is
     * (3+5)^8 = 0 — clean — while reading the `^` as a `+` gives 16. */
    h = cqrt_alloc_i8(3);
    cqrt_addc_i8(h, (int8_t)5);
    cqrt_xorc_i8(h, (int8_t)8);
    CHECK(cq_shim_certificate(ctx, h, 0u) > 0);
    cqrt_free(h);

    close_with(&m);
}

CQ_TEST(a_fredkin_cswap_is_a_write_and_leaves_the_rail_unproven)
{
    cq_mock m; cq_sink s; cq_ctx *ctx = open_with(&m, &s);
    int32_t f, a, b2;

    /* THE MUTANT: delete the `rec(CQ_ROP_CSWAP, ...)` on the QUANTUM row. It
     * survived a whole battery, and it is the one survivor that failed in the
     * UNSOUND direction: with the call unrecorded, a rail a Fredkin has just
     * rewritten has an EMPTY history, which reduces vacuously, and a zero birth
     * value then RELEASES it. That is a non-|0> index handed to the pool — the
     * one unforgivable bug — reached not through a wrong rule but through a
     * missing line.
     *
     * IT SURVIVED BECAUSE THE ROWS WERE ONLY EVER TESTED FOR WHAT THEY EMIT.
     * The case one above drives the CONSTANT-ONE row and asserts the histories
     * swap; nothing drove the row that emits gates and then asked the
     * certificate. So the assertion here is the plainest one in the file: after
     * a Fredkin, the rail is NOT discharged.
     *
     * AND THE THREE ROWS ARE ASSERTED TOGETHER, because the modelling error
     * this pair of cases exists for is precisely treating them as one call. */
    f  = cqrt_alloc_i1(0);
    cqrt_ry_i1(f, 0.3);            /* a genuinely QUANTUM flag: the Fredkin row */
    a  = cqrt_alloc_i8(0);
    b2 = cqrt_alloc_i8(0);
    cqrt_ry_i8(a, 0.2);            /* both rails own qubits */
    cqrt_ry_i8(a, -0.2);
    cqrt_ry_i8(b2, 0.2);
    cqrt_ry_i8(b2, -0.2);

    /* Both reduce BEFORE the swap — so the assertion after it is about the
     * cswap and not about a rail that was never clean. */
    CHECK(cq_shim_certificate(ctx, a, 0u) > 0);
    CHECK(cq_shim_certificate(ctx, b2, 0u) > 0);

    cqrt_cswap(f, a, b2);

    /* ONE UNPAIRED WRITE EACH. Not a conviction — a Fredkin on two |0> rails
     * really does leave |0> — which is the engine being SUFFICIENT and never
     * necessary, and is why this is UNPROVEN rather than DIRTY. */
    CHECK_EQ(cq_shim_certificate(ctx, a, 0u), 0);
    CHECK_EQ(cq_shim_certificate(ctx, b2, 0u), 0);

    /* AND A SECOND, IDENTICAL cswap PAIRS WITH THE FIRST and clears both —
     * which is what makes the assertion above "the write was recorded" rather
     * than "cswap poisons a rail forever". The flag is unchanged between them
     * and the co-written partner is each rail's own history, so the pair
     * reduces on exactly the guards the miscompile-fix case exercises. */
    cqrt_cswap(f, a, b2);
    CHECK(cq_shim_certificate(ctx, a, 0u) > 0);
    CHECK(cq_shim_certificate(ctx, b2, 0u) > 0);

    close_with(&m);
}

CQ_TEST(the_effect_table_models_a_write_for_every_opcode)
{
    /* THE CLAIM THAT USED TO BE A RUNTIME GUARD IN THE CERTIFICATE, moved here
     * because there it could never fail. `cq_shim_certificate` consulted
     * `cq_rec_table_is_complete()` on every free; the predicate is TRUE for
     * every table this codebase can compile, so the branch was unreachable and
     * a mutant deleting the call survived — correctly and permanently. An
     * untestable runtime guard is worse than none: it reads as coverage.
     *
     * THE CLAIM ITSELF IS REAL AND THIS IS WHERE IT CAN FAIL. A row left
     * implicitly zero is the INERT row — "reads nothing, writes nothing" — which
     * is the WIDEST possible model of an opcode and therefore the direction that
     * discharges frees it should not. Adding a `cq_rop` enumerator without a
     * table row turns this case red at once, which is the whole point: the enum
     * and the table are two lists that must agree and C will not check them.
     *
     * `CQ_ROP_MINT` IS THE ONE EXEMPTION AND IT IS BY NAME, not by the loop
     * happening not to reach it — a mint is the rail's BIRTH, and the birth
     * value is what the history is measured against rather than part of it. */
    CHECK(cq_rec_table_is_complete());

    /* AND THE INERT ROW IS ROW ZERO, which is what makes a `memset` record the
     * REFUSAL rather than an `x` against handle 0. The record's own
     * _Static_assert pins the numbering; this reads it back the other way. */
    CHECK_EQ((int)CQ_ROP_NONE, 0);
    CHECK_EQ(cq_rec_effect((uint16_t)CQ_ROP_NONE)->writes, 0u);
    CHECK(cq_rec_effect((uint16_t)CQ_ROP__N) == NULL);
}
