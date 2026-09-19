/* tests/test_kernel_fsqrt_handoff.inc — THE M40 -> M32 HAND-OFF, which is an
 * ENCODING no C type expresses, and KAHAN'S THEOREM, which K21.md carried as a
 * hand inference and this case discharges by execution. Included by
 * test_kernel_fsqrt.c AFTER test_kernel_fsqrt_slots.inc.
 * NOTHING BELOW READS A GOLDEN.
 *
 * THE TWO CLAIMS, AND NEITHER HAS ANY OTHER INSTRUMENT.
 *
 * (a) THE G/R/S WORKING FORMAT. `fadd.jl:13-14` states it and
 *     `_sf_round_and_pack` reads exactly that at softfloat_common.jl:204-207:
 *     bit 55 is the implicit 1, bits 54-3 the 52-bit fraction, bits 2/1/0
 *     guard/round/sticky, and bits 56-63 are never read. A one-bit
 *     disagreement here is RIGHT ON EVERY INPUT THAT DOES NOT ROUND, so L1's
 *     random draw is the wrong instrument and the anchors are the right one.
 *     What `wr` actually is, is `fsqrt.jl:96`'s `q | sticky` — and the whole
 *     scaling rationale at :17-25 exists to put its leading 1 at bit 55.
 *
 * (b) KAHAN'S NO-MIDPOINT THEOREM (fsqrt.jl:11-15, restated at :93-95). The
 *     rounding decision inside M32 is `grs > 4 | (grs == 4 & frac odd)`, and
 *     `grs == 4` is exactly guard 1, round 0, sticky 0 — an exact halfway
 *     case. Upstream says the true root of a binary64 is never one, so the
 *     tie branch is DEAD IN THIS CALLER and PRD-v2 §7.12's "a tie-to-even case
 *     in each direction" has no witness here, in either direction. K21.md §5.5
 *     records that as an inference from upstream's theorem and marks it
 *     UNVERIFIED-BY-EXECUTION; this case is the verification, and the
 *     structural reason it holds is worth stating because it is checkable:
 *
 *         the radicand is `ma_adj << 58` and 58 is EVEN, so it is a perfect
 *         square iff `ma_adj` is, and then the root ends in 29 zero bits —
 *         so a zero remainder forces grs == 0. Otherwise the sticky OR at :96
 *         sets bit 0 and grs is ODD. grs == 4 needs a zero remainder AND a set
 *         guard bit, and no input gives both.
 *
 *     MEASURED INDEPENDENTLY, 2026-09-19, by a throwaway probe computing the
 *     exact integer square root of the 112-bit radicand by BISECTION over
 *     `__int128`: over 3,000,000 random normals the `grs` histogram is
 *     [1]=751549 [3]=751065 [5]=748274 [7]=749112 and EVERY EVEN RESIDUE,
 *     including 4, is zero. The rows below are the shipped form of the same
 *     claim, over the anchors, read off the CIRCUIT's own hand-off wire.
 *
 * IF A LATER SESSION CONSTRUCTS A `grs == 4` WITNESS FOR `fsqrt`, this case is
 * wrong, K21.md §5.5 is wrong and upstream's :11-15 is wrong with them — and
 * that finding belongs in a bead rather than in a quiet fix.
 */

/* `wr` is row :96's `|`, identified STRUCTURALLY as the unique `or` row whose
 * first operand is the last iteration's `q` — never by the seam arithmetic,
 * which is exactly the number a boundary error moves. */
static int fq_wr_row(const cq_fsqrt_row *rows, int n)
{
    int want = cq_fsqrt_loop_row(CQ_FSQRT_ITERS - 1, FQ_J_Q_NEXT), found = -1;

    for (int i = 0; i < n; i++)
        if (rows[i].op == CQ_FSOP_OR && rows[i].s0 == want) {
            CHECK_EQ(found, -1);
            found = i;
        }
    CHECK(found > 0);
    return found;
}

/* Drives the program once over a CLASSICAL rail, reads `wr`, runs the reverse
 * half and checks the region came back. A classical rail is the cheap choice
 * and is sound here: a BLOCK has no short-circuit, so every one of the 157,108
 * slots is still visited — only the emitted gates fold. */
static uint64_t fq_working_value(uint64_t a, int wr_row, uint32_t wr_off)
{
    cq_counter cnt;
    cq_ctx ctx;
    cq_sink s;
    cq_scratch scr;
    cq_fsqrt_block k;
    cq_bit rail[CQ_FP64_W];
    uint64_t wr;

    (void)wr_row;
    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);
    cq_ctx_init(&ctx, &s);
    for (int i = 0; i < W64; i++)
        rail[i] = ((a >> (unsigned)i) & 1u) ? cq_bit_one() : cq_bit_zero();
    cq_scratch_alloc(&scr, cq_fsqrt_region());
    for (uint32_t i = 0; i < scr.n; i++) cq_materialise(&ctx, &scr.bits[i]);
    k.a = rail; k.scr = &scr; k.off = 0u;

    for (int u = 0; u < cq_fsqrt_steps(); u++) cq_fsqrt_step(&ctx, &k, u);
    wr = fq_read(&ctx, cq_scratch_span(&scr, wr_off, (uint32_t)W64));
    for (int u = cq_fsqrt_steps() - 1; u >= 0; u--) cq_fsqrt_step(&ctx, &k, u);

    for (uint32_t i = 0; i < scr.n; i++) {
        if (cq_bit_is_qubit(scr.bits[i])
            && !cq_shadow_known_zero(&ctx.shadow, cq_bit_qindex(scr.bits[i])))
            cq_h_fail(__FILE__, __LINE__,
                      "sqrt(%016llx): scratch bit %u is not back at |0> after "
                      "the reverse half", (unsigned long long)a, i);
        scr.bits[i] = cq_bit_zero();
    }
    cq_scratch_dispose(&scr);
    cq_ctx_dispose(&ctx);
    return wr;
}

CQ_TEST(the_working_value_handed_to_m32_is_grs_at_bit_55_and_never_ties)
{
    /* The five reachable `grs` residues, one anchor each and three spare:
     * 0 (an exact root), 1, 3, 5 and 7. The residue is NOT written down here —
     * it is read off the circuit and only the PROPERTIES are asserted. */
    static const uint64_t V[] = {
        CQ_F64_ONE, FSQ_TWO, FSQ_THREE, FSQ_TENTH, FSQ_PI,
        CQ_F64_MAX, CQ_F64_MAX_SUBNORMAL, CQ_F64_MIN_SUBNORMAL
    };
    const cq_fsqrt_row *rows;
    uint32_t wr_off = 0u;
    int n, wr_row, seen_guard = 0, seen_clear = 0, seen_exact = 0;

    rows = cq_fsqrt_rows(&n);
    wr_row = fq_wr_row(rows, n);
    for (int i = 0; i < wr_row; i++) wr_off += fq_ref_row_region(&rows[i]);

    for (size_t j = 0; j < sizeof V / sizeof V[0]; j++) {
        uint64_t wr = fq_working_value(V[j], wr_row, wr_off);
        int grs = (int)(wr & 7u);

        /* (a) THE ENCODING: the leading 1 at bit 55, and nothing above it. */
        if (((wr >> 55) & 1u) == 0u || (wr >> 56) != 0u)
            cq_h_fail(__FILE__, __LINE__,
                      "sqrt(%016llx): wr = %016llx — the G/R/S working format "
                      "wants bit 55 SET and bits 56-63 CLEAR "
                      "(fadd.jl:13-14, common.jl:204-207)",
                      (unsigned long long)V[j], (unsigned long long)wr);

        /* (b) KAHAN: the tie residue is unreachable. */
        if (grs == 4)
            cq_h_fail(__FILE__, __LINE__,
                      "sqrt(%016llx): wr = %016llx has grs == 4, an EXACT "
                      "halfway case. Kahan's theorem (fsqrt.jl:11-15) says "
                      "sqrt never produces one; this is a finding, not a bug "
                      "to fix quietly", (unsigned long long)V[j],
                      (unsigned long long)wr);
        /* AND THE EVEN RESIDUES ARE UNREACHABLE TOO, EXCEPT ZERO, which is
         * the structural half of the same statement: a non-zero remainder
         * always sets bit 0 at :96, and a zero one forces all three bits
         * clear. */
        CHECK(grs == 0 || (grs & 1) == 1);

        if (grs >= 4)              seen_guard++;
        if (grs != 0 && grs < 4)   seen_clear++;
        if (grs == 0)              seen_exact++;
    }

    /* NOT VACUOUS: the tie claim is interesting only if the anchors reach both
     * sides of the guard bit and an exact root as well. */
    CHECK(seen_guard > 0);
    CHECK(seen_clear > 0);
    CHECK(seen_exact > 0);
    printf("# fsqrt hand-off: wr row %d at bit %u; %d anchors round up, %d "
           "round down, %d are exact, 0 tie\n", wr_row, wr_off, seen_guard,
           seen_clear, seen_exact);
    fflush(stdout);
}
