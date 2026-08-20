/* tests/test_angle_bounds.inc — the TOLERANCE half of M21's suite.
 *
 * Split from tests/test_angle.c at Step 20, on the seam that file's own header
 * recorded at Step 18 and named in `bd w8j`: the `--- D10's error bound ---`
 * divider. Everything here is about the WINDOW — how wide it is, what it
 * refuses, and the contract PRD §15 D10 states. Everything left behind is
 * about the TABLE — which of §7's rows an angle sits on.
 *
 * THE SPLIT IS MECHANICAL AND CHANGED NOTHING. It was taken as a separate step
 * BEFORE `bd fna`'s enum change, so that the enum change did not land under a
 * red `make lint` and get improvised — which is the failure mode `bd w8j` and
 * `bd zmo` exist to prevent. The cases, their order and their bodies are
 * byte-identical to what test_angle.c carried; only the file boundary moved,
 * and the CQ_CASE list in test_angle.c's CQ_TEST_MAIN is untouched.
 *
 * An .inc is LOC-exempt (tools/check_loc.sh counts .c/.h/.py only), which is
 * what makes this a real escape hatch rather than a rename that dodges Rule 12.
 * It is included from test_angle.c and is not a translation unit of its own.
 */

/* --- D10's error bound, which is the contract ----------------------------- */

CQ_TEST(no_fold_ever_discards_more_than_the_window_says)
{
    /* TWENTY-TWO DECADES OF MAGNITUDE, because the defect this case exists to
     * catch lived in a band the rest of the suite did not reach. An earlier
     * draft tested |θ| up to ~2.5e4 and again from 6.3e13 up, and the window
     * was unsound throughout 1e11 … 1.6e12 — entirely inside the gap. */
    for (int e = -6; e <= 15; e++) {
        for (int m = 1; m <= 10; m++) {
            double mag = (double)m * pow(10.0, (double)e);
            check_sound(mag);
            check_sound(-mag);
            check_sound(round(mag / REF_PI) * REF_PI);   /* snapped to lattice */
        }
    }

    /* The lattice itself, where folds actually happen, so the case is not
     * bounding the error of an empty set. */
    for (long k = -2048; k <= 2048; k++) {
        check_sound((double)k * REF_PI);
        check_sound((double)k * REF_PI * (1.0 + 5e-13));
    }

    unsigned long long s = 0xD1B54A32D192ED03ull;
    for (int i = 0; i < 100000; i++) {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        double u = (double)(s >> 11) / 9007199254740992.0;
        check_sound((u - 0.5) * 2.0 * pow(10.0, (double)(i % 12) - 3.0));
    }

    /* THE DRIFT HALF OF THE BOUND, WHICH EVERYTHING ABOVE IS BLIND TO. Every
     * call so far went through cq_angle_ry_row, i.e. at the module tolerance —
     * and there the refusal caps |θ| at 1.96e4, where the drift term is a
     * thousandth of the bound and only the residual term can be falsified. The
     * drift dominates near the refusal boundary itself, |θ| ≈ tol·π/1.6e-16,
     * which is only large enough to matter at a loose tolerance. So sweep the
     * tolerance to the cap and walk the reach at each one.
     *
     * MEASURED: `CQ_ANGLE_PI_ERROR = 5e-17` really does break D10 — by 1.22× at
     * `lattice(0x1.550f7dca5209cp+39, 0x1.8723a1d588a37p-17)`, and 1.3% of all
     * folds violate it — and before this loop existed the entire suite stayed
     * green except one magnitude pin, which reads identically for a SAFE
     * tightening of the constant and for that unsafe loosening.
     *
     * THE LADDER MUST NOT BE DERIVED FROM THE MODULE'S OWN CONSTANT, and the
     * first version of this loop was — it walked up to `tol·π/1.6e-16` and so
     * STILL missed the mutant it was written for, because a loosened constant
     * makes the module accept a LARGER |θ| than the test ever offered it. That
     * is this file's own headline lesson (an oracle that shares a constant is
     * blind to it) arriving one level down, in the probe RANGE rather than in
     * the comparison. So the ladder starts far past any reach the module could
     * plausibly have and descends 60 octaves. Over-probing is free: anything
     * refused comes back GENERAL and check_sound_at skips it.
     *
     * THE DIVISION OF LABOUR WITH `the_reach_of_the_default_tolerance_is_pinned`
     * IS DELIBERATE, AND SO IS SAYING WHERE THIS CASE STOPS. Measured, one
     * mutant at a time:
     *
     *   PI_ERROR       sound?   caught by
     *   5e-17   (3.2x loose)  no    THIS case AND the reach pin
     *   1e-16   (1.6x loose)  no    the reach pin only
     *   1.45e-16 (1.1x loose) no    the reach pin only
     *   1.51e-16 (tighter)    yes   the reach pin only
     *
     * So the REACH PIN is the guard with complete coverage of the constant —
     * any change to it moves the reach and that case goes red. What this case
     * adds is a failure that names the CONTRACT rather than a magnitude, and it
     * has that only for a gross loosening: a marginal one violates D10 too
     * rarely for a bounded sweep to land on it. Neither case subsumes the
     * other, and it is worth knowing that the cheap one is the complete one. */
    const double tols[] = { 1e-12, 1e-10, 1e-8, 1e-6, 1e-4,
                            CQ_ANGLE_TOLERANCE_MAX };
    for (size_t i = 0; i < sizeof tols / sizeof tols[0]; i++) {
        double far = tols[i] * REF_PI / 1e-18;   /* 160x the shipped reach */
        double w   = tols[i] * REF_PI;
        for (int j = 0; j <= 400; j++) {
            double mag = far * pow(2.0, -(double)j / 8.0);
            double k0  = round(mag / REF_PI);
            /* SEVERAL NEIGHBOURING INDICES PER RUNG, not one. At a lattice
             * point the whole error is the drift plus however the product
             * `k·π_double` happened to round, and that rounding varies
             * pseudo-randomly with k — so one probe per magnitude samples the
             * error distribution once and mostly misses its tail. Measured
             * against the 5e-17 mutant: one probe per rung caught nothing, 24
             * caught it. */
            for (int m = 0; m < 24; m++) {
                double base = (k0 + (double)m) * REF_PI;
                check_sound_at(base,           tols[i]);
                check_sound_at(-base,          tols[i]);
                check_sound_at(base + 0.5 * w, tols[i]);
            }
        }
    }
}

CQ_TEST(a_large_theta_is_refused_rather_than_folded)
{
    /* THE MEASURED MISCOMPILE, kept as a regression case with its numbers.
     * Every θ here was classified IDENTITY or NEG_IDENTITY by the θ-relative
     * window, and the figure after each is the TRUE distance from θ to the
     * multiple of π that row named — computed against a 60-digit π, not in
     * doubles. It is not a cliff: the error grows linearly with |θ| from the
     * first magnitude at which the window can reach a lattice point at all,
     * which is why the series below spans six decades rather than naming one
     * bad angle. Each of the first three is a real multiple plus 0.4 of the old
     * window, i.e. an angle that construction folded away. */
    CHECK_RY(4999995.504637527,  CQ_ANGLE_GENERAL);  /* was IDENTITY,  2.0e-6 rad */
    CHECK_RY(999999993.1398191,  CQ_ANGLE_GENERAL);  /* was IDENTITY,  4.0e-4     */
    CHECK_RY(99999999992.56593,  CQ_ANGLE_GENERAL);  /* was IDENTITY,  4.0e-2     */
    CHECK_RY(1e12,               CQ_ANGLE_GENERAL);  /* was IDENTITY,  0.657625   */
    CHECK_RY(-1e12,              CQ_ANGLE_GENERAL);  /* was IDENTITY,  0.657625   */
    CHECK_RY(1.5e12,             CQ_ANGLE_GENERAL);  /* was IDENTITY,  0.986437   */
    CHECK_RY(1.570673279e12,     CQ_ANGLE_GENERAL);  /* was NEG_IDENT, 1.292108   */

    /* And the sibling defect, on the other side of the same term: an EXACT
     * multiple of the module's own π is still refused once the drift between
     * that double and true π exceeds the window. 2^52·π_double has a residual
     * of exactly zero and is 0.551532 rad from any true multiple of 4π. */
    CHECK_RY(4503599627370496.0 * REF_PI, CQ_ANGLE_GENERAL);
    CHECK_RY(1e12 * REF_PI,               CQ_ANGLE_GENERAL);
    CHECK_RY(DBL_MAX,                     CQ_ANGLE_GENERAL);
    CHECK_RY(-DBL_MAX,                    CQ_ANGLE_GENERAL);
}

CQ_TEST(the_reach_of_the_default_tolerance_is_pinned)
{
    /* The refusal is |θ|·1.6e-16 ≤ tol·π, so at the default it admits
     * |k| ≤ 6250 and refuses above. Pinning both sides is what makes a change
     * to CQ_ANGLE_PI_ERROR or to the default a change that has to be argued. */
    CHECK_RY(6000.0 * REF_PI, CQ_ANGLE_IDENTITY);   /* 6000 ≡ 0 (mod 4)  */
    CHECK_RY(6001.0 * REF_PI, CQ_ANGLE_HALF_TURN);
    CHECK_RY(6500.0 * REF_PI, CQ_ANGLE_GENERAL);    /* past the reach    */
    CHECK_RY(-6000.0 * REF_PI, CQ_ANGLE_IDENTITY);
    CHECK_RY(-6500.0 * REF_PI, CQ_ANGLE_GENERAL);

    /* A looser tolerance buys proportionally more reach, which is the point of
     * tying the refusal to the window rather than to a fixed magnitude. */
    CHECK_LATTICE(6500.0 * REF_PI, 1e-11, CQ_ANGLE_IDENTITY);
    CHECK_LATTICE(1e6   * REF_PI,  1e-11, CQ_ANGLE_GENERAL);
    CHECK_LATTICE(1e6   * REF_PI,  1e-9,  CQ_ANGLE_IDENTITY);
}

CQ_TEST(the_largest_rotation_a_fold_discards_is_the_window)
{
    /* The window is ABSOLUTE — tol·π ≈ 3.1416e-12 rad at the default — so the
     * threshold sits at the same place whatever |θ| is. THAT is what a relative
     * window fails: at k = 6001 it would be 6001× wider. */
    const double w = cq_angle_tolerance() * REF_PI;

    CHECK_RY(w * 0.9,  CQ_ANGLE_IDENTITY);     /* inside: folded away  */
    CHECK_RY(-w * 0.9, CQ_ANGLE_IDENTITY);
    CHECK_RY(w * 1.5,  CQ_ANGLE_GENERAL);      /* outside: emitted     */
    CHECK_RY(-w * 1.5, CQ_ANGLE_GENERAL);
    CHECK_RY(1e-13,    CQ_ANGLE_IDENTITY);
    CHECK_RY(1e-300,   CQ_ANGLE_IDENTITY);
    CHECK_RY(1e-11,    CQ_ANGLE_GENERAL);

    /* The same absolute width, at four magnitudes. A window scaled by |θ| makes
     * every "outside" row below pass as HALF_TURN.
     *
     * THE LIST STOPS AT k = 1001 FOR A REAL REASON, found by this case going
     * red at k = 6001. An absolute window eventually becomes finer than the
     * double grid: ulp(θ) reaches tol·π at |θ| = tol·π·2^52 ≈ 1.4e4, and above
     * that the only representable angle inside the window is the lattice point
     * itself, so `base ± 0.9·w` rounds straight back out of it. Probing at
     * k = 6001 was therefore asserting something about the grid, not about the
     * module. The band from there to the refusal at |k| = 6250 is exact-match
     * territory, and `the_reach_of_the_default_tolerance_is_pinned` covers it. */
    const long odd[] = { 1, 3, 101, 1001 };
    for (size_t i = 0; i < sizeof odd / sizeof odd[0]; i++) {
        double base = (double)odd[i] * REF_PI;
        /* row_of_index, not a literal — the half turn splits by k mod 4 since
         * bd fna, and `odd` carries both parities (1 and 101 are k ≡ 1, 3 is
         * k ≡ 3). This case is about the WINDOW, so it stays parity-agnostic
         * rather than growing two literals. */
        CHECK_RY(base,           row_of_index(odd[i]));
        CHECK_RY(base + w * 0.9, row_of_index(odd[i]));
        CHECK_RY(base - w * 0.9, row_of_index(odd[i]));
        CHECK_RY(base + w * 1.5, CQ_ANGLE_GENERAL);
        CHECK_RY(base - w * 1.5, CQ_ANGLE_GENERAL);
    }
}

CQ_TEST(the_corpus_angles_are_all_general_and_3_14_is_the_close_one)
{
    /* CQ_lang's 239 goldens carry 410 rotation calls and 26 distinct angles,
     * and NOT ONE lands on a special row (measured 2026-08-17; PRD §15 D10).
     * They are all small decimals, so what is worth pinning is not the list but
     * the closest approach.
     *
     * `3.14` appears in two fixtures and is 1.5927e-3 rad short of π — 5.1e8
     * times the default window, which is the headroom this classification runs
     * with in practice. It is also INSIDE the window at the loosest legal
     * tolerance, where it becomes an X. That is the cap doing its job at the
     * order where "tolerance" stops meaning "the same angle", and it is why
     * CQ_ANGLE_TOLERANCE_MAX must not be raised. */
    CHECK_RY(3.14,  CQ_ANGLE_GENERAL);
    CHECK_RY(0.5,   CQ_ANGLE_GENERAL);     /* 256 of the 410 calls */
    CHECK_RY(0.25,  CQ_ANGLE_GENERAL);
    CHECK_RY(-0.5,  CQ_ANGLE_GENERAL);
    CHECK_RZ(0.75,  CQ_ANGLE_GENERAL);

    CHECK_LATTICE(3.14, CQ_ANGLE_TOLERANCE_MAX,       CQ_ANGLE_HALF_TURN);
    CHECK_LATTICE(3.14, CQ_ANGLE_TOLERANCE_MAX / 10.0, CQ_ANGLE_GENERAL);
}

CQ_TEST(both_boundaries_are_inclusive_at_a_non_zero_threshold)
{
    /* BOTH `<=` IN cq_angle_lattice ARE INCLUSIVE, and until this case existed
     * the only thing that said so was θ = 0 at tol = 0 — where residual and
     * window are both ZERO, so `<` and `<=` differ only in a degenerate tie and
     * the suite proved nothing about either operator at a real threshold.
     *
     * These two pairs are constructed so the comparison is an EXACT tie with
     * both sides non-zero, which is why the tolerances are spelled in hex and
     * must not be "tidied" into decimals — a decimal that merely rounds near
     * them turns the tie into an ordinary inequality and the case goes quiet.
     *
     *   residual tie: window == residual == 0x1p-50, θ one ulp off π
     *   refusal  tie: window == |θ|·1.6e-16 == 0x1.21c2c25e4127dp-51, residual 0
     *
     * Verified by execution: with `<=` both are HALF_TURN; change EITHER to `<`
     * and exactly its own line becomes GENERAL. */
    CHECK_LATTICE(0x1.921fb54442d1ap+1, 0x1.45f306dc9c883p-52, CQ_ANGLE_HALF_TURN);
    CHECK_LATTICE(0x1.921fb54442d18p+1, 0x1.70ef54646d497p-53, CQ_ANGLE_HALF_TURN);
}

CQ_TEST(a_zero_tolerance_admits_only_zero)
{
    /* Not "only the exact double lattice" — only ZERO. π is irrational, so no
     * non-zero double is exactly a multiple of it, and with no window at all
     * the refusal admits |θ| ≤ 0. That is the mathematically exact answer and
     * it is worth having as a legal setting for exactly that reason. */
    CHECK_LATTICE(0.0,  0.0, CQ_ANGLE_IDENTITY);
    CHECK_LATTICE(-0.0, 0.0, CQ_ANGLE_IDENTITY);
    CHECK_LATTICE(REF_PI,       0.0, CQ_ANGLE_GENERAL);
    CHECK_LATTICE(2.0 * REF_PI, 0.0, CQ_ANGLE_GENERAL);
    CHECK_LATTICE(4.0 * REF_PI, 0.0, CQ_ANGLE_GENERAL);
    CHECK_LATTICE(DBL_TRUE_MIN, 0.0, CQ_ANGLE_GENERAL);

    /* Index 0 admits exactly one angle at every tolerance, which is the other
     * half of "the window is absolute": it does not shrink to nothing near
     * zero, and it does not open up either. */
    const double tols[] = { 0.0, 1e-15, CQ_ANGLE_TOLERANCE_DEFAULT, 1e-6 };
    for (size_t i = 0; i < sizeof tols / sizeof tols[0]; i++) {
        CHECK_LATTICE(0.0,  tols[i], CQ_ANGLE_IDENTITY);
        CHECK_LATTICE(-0.0, tols[i], CQ_ANGLE_IDENTITY);
    }
}

