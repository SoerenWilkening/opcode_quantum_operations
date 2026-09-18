/* tests/test_kernel_fpround_value.inc — L1 against the IEEE reference model,
 * the classical row against the same model, the two hand-off encodings, and
 * the signed/unsigned discriminator. Included by test_kernel_fpround.c.
 *
 * THE ORACLE IS tests/support/fpref.c AND SHARES NOTHING WITH THE PORT
 * (PRD-v2 §7.16). Its rounding decision is `G && ((R|S) || odd)` where the
 * port packs the three bits into `grs` and compares against 4; its
 * normalisation is a counting loop where the port is a six-stage binary
 * search; its subnormal shift is one place at a time with an OR-reduced sticky
 * where the port clamps, muxes and barrel-shifts; and every selection is an
 * `if` where the port is a branchless `ifelse` chain.
 *
 * THREE MASK ROWS, AND THE THIRD IS D-K23-7's. All-classical reaches the
 * `ifelse` tree with every lane a constant and is where the fold table elides
 * the most; all-quantum is what L4 pins; and `wr` quantum with the exponent
 * and the sign CLASSICAL is the shape `fsqrt.jl:103` (a literal `UInt64(0)`
 * for `result_sign`) and `flog.jl:269` (a literal `Int64(1)` for `e`) actually
 * call with, which collapses a different set of rows again. Slots are
 * unaffected by any of them; GATES are not, which is why L4 is per
 * (block, caller-shaped mask).
 *
 * A BLOCK HAS NO R9 SHORT-CIRCUIT, so the all-classical row is NOT an L5 row:
 * the region is allocated and pre-materialised before step 0 runs, so a block
 * handed three classical spans still pays full qubits and most of its gates.
 * M33-M35 fold at `soft_*`'s own entry, over the whole routine, never per
 * block — which is what the four `_eval` bodies are for.
 */

typedef struct { uint64_t o[5]; int n; } fpr_exp;

static int fpr_out_w(cq_fpround_id id, int which)
{
    if ((id == CQ_FPR_SUBNORM || id == CQ_FPR_ROUND) && (which == 2 || which == 3))
        return 1;
    return W64;
}

static const char *fpr_out_name(cq_fpround_id id, int which)
{
    static const char *N52[] = { "m_final", "e_final" };
    static const char *CLZN[] = { "wr", "result_exp" };
    static const char *SUB[] = { "wr", "result_exp", "subnormal",
                                 "flush_to_zero", "flushed_result" };
    static const char *RND[] = { "normal_result", "overflow_result",
                                 "exp_overflow", "exp_overflow_after_round" };

    if (id == CQ_FPR_NORM52)  return N52[which];
    if (id == CQ_FPR_CLZ)     return CLZN[which];
    if (id == CQ_FPR_SUBNORM) return SUB[which];
    return RND[which];
}

/* The MODEL. Every helper's outputs in fpround.h's declaration order. */
static fpr_exp fpr_ref(cq_fpround_id id, const uint64_t *v)
{
    fpr_exp r;
    uint64_t a = v[0];
    int64_t  e = fpr_i64(v[1]);

    memset(&r, 0, sizeof r);
    switch (id) {
    case CQ_FPR_NORM52:
        fpref_norm52(&a, &e);
        r.o[0] = a; r.o[1] = fpr_u64(e); r.n = 2;
        return r;
    case CQ_FPR_CLZ:
        fpref_clz(&a, &e);
        r.o[0] = a; r.o[1] = fpr_u64(e); r.n = 2;
        return r;
    case CQ_FPR_SUBNORM: {
        uint64_t fl;
        int sn, fz;

        fpref_subnormal(&a, &e, v[2], &fl, &sn, &fz);
        r.o[0] = a; r.o[1] = fpr_u64(e); r.o[2] = (uint64_t)sn;
        r.o[3] = (uint64_t)fz; r.o[4] = fl; r.n = 5;
        return r; }
    default: {
        uint64_t n, ov;
        int x0, x1;

        fpref_round_and_pack(a, e, v[2], &n, &ov, &x0, &x1);
        r.o[0] = n; r.o[1] = ov; r.o[2] = (uint64_t)x0; r.o[3] = (uint64_t)x1;
        r.n = 4;
        return r; }
    }
}

/* The library's OWN Julia transcription, for the consumers' R9 rows. Checked
 * against the model above, never used as one (D-K23-9, the Step 18 trap). */
static fpr_exp fpr_eval(cq_fpround_id id, const uint64_t *v)
{
    fpr_exp r;
    uint64_t a = v[0];
    int64_t  e = fpr_i64(v[1]);

    memset(&r, 0, sizeof r);
    switch (id) {
    case CQ_FPR_NORM52:
        cq_normalize_to_bit52_eval(&a, &e);
        r.o[0] = a; r.o[1] = fpr_u64(e); r.n = 2;
        return r;
    case CQ_FPR_CLZ:
        cq_normalize_clz_eval(&a, &e);
        r.o[0] = a; r.o[1] = fpr_u64(e); r.n = 2;
        return r;
    case CQ_FPR_SUBNORM: {
        uint64_t fl;
        int sn, fz;

        cq_handle_subnormal_eval(&a, &e, v[2], &fl, &sn, &fz);
        r.o[0] = a; r.o[1] = fpr_u64(e); r.o[2] = (uint64_t)sn;
        r.o[3] = (uint64_t)fz; r.o[4] = fl; r.n = 5;
        return r; }
    default: {
        uint64_t n, ov;
        int x0, x1;

        cq_round_and_pack_eval(a, e, v[2], &n, &ov, &x0, &x1);
        r.o[0] = n; r.o[1] = ov; r.o[2] = (uint64_t)x0; r.o[3] = (uint64_t)x1;
        r.n = 4;
        return r; }
    }
}

typedef struct { const char *name; int q[3]; } fpr_mask;

static const fpr_mask MASKS[] = {
    { "all-classical",  { 0, 0, 0 } },
    { "all-quantum",    { 1, 1, 1 } },
    { "wr Q, exp/sign classical (fsqrt's and flog's call shape)", { 1, 0, 0 } }
};
enum { N_MASKS = 3 };

/* One tuple through the CIRCUIT, every output read off the shadow and compared
 * against the model. Returns the number of mismatches it reported. */
static int fpr_run_one(cq_fpround_id id, const uint64_t *v, const int *q,
                       const char *what, const cq_sink *sink)
{
    fpr_exp want = fpr_ref(id, v);
    cq_bit buf[CQ_FP64_W];
    int bad = 0;

    fpr_open(&FX, id, v, q, 0u, sink);
    fpr_forward(&FX);

    for (int w = 0; w < want.n; w++) {
        int n = fpr_out_w(id, w), ok;
        uint64_t got = fpr_read(&FX.ctx, fpr_out_at(&FX, 0u, w, buf), n, &ok);

        if (!ok || got != want.o[w]) {
            cq_h_fail(__FILE__, __LINE__,
                      "L1 %s [%s]: %s = %s0x%016llx, the IEEE model says "
                      "0x%016llx  (in = 0x%016llx, 0x%016llx, 0x%016llx)",
                      fpr_out_name(id, w), what, fpr_out_name(id, w),
                      ok ? "" : "unknown/", (unsigned long long)got,
                      (unsigned long long)want.o[w],
                      (unsigned long long)v[0], (unsigned long long)v[1],
                      (unsigned long long)v[2]);
            bad++;
        }
    }
    fpr_close(&FX);
    return bad;
}

/* ---- L1. ---------------------------------------------------------------- */

CQ_TEST(l1_against_the_ieee_reference_model)
{
    cq_counter cnt;
    cq_sink s;
    int budget = cq_kd_samples();

    cq_count_reset(&cnt);
    s = cq_sink_counter(&cnt);

    for (int r = 0; r < N_ROWS; r++) {
        cq_fpround_id id = ROWS[r].id;

        /* The anchors sit INSIDE the budget, never on top of it — so a budget
         * below the anchor count would DROP a named predicate silently. */
        CHECK(budget >= fpr_anchor_count(id));

        for (int m = 0; m < N_MASKS; m++) {
            cq_bk_rng rng;
            int bad = 0;

            cq_bk_rng_init(&rng, UINT64_C(0x9E3779B97F4A7C15)
                                 ^ ((uint64_t)r << 8) ^ (uint64_t)m);

            for (int i = 0; i < budget; i++) {
                uint64_t v[3];
                const char *what = fpr_tuple(id, i, &rng, v);

                bad += fpr_run_one(id, v, MASKS[m].q, what, &s);
                if (bad > 4) break;      /* four failures locate the fault */
            }
            printf("# L1 %-8s %-58s %2d tuples (%d anchors forced)%s\n",
                   ROWS[r].name, MASKS[m].name, budget,
                   fpr_anchor_count(id), bad ? "  FAILED" : "");
        }
    }
    fflush(stdout);
}

/* ---- The classical row, against the same model. ------------------------- */

/* PRD-v2 §7.4's R9 transcription, over the SAME tuples. It runs no circuit, so
 * it is nearly free and can afford a far wider draw than L1 — which is the
 * point: the two bodies agree on 4,096 tuples per block or one of them is
 * wrong, and neither is allowed to be the other's reference. */
CQ_TEST(the_classical_row_agrees_with_the_ieee_reference_model)
{
    for (int r = 0; r < N_ROWS; r++) {
        cq_fpround_id id = ROWS[r].id;
        cq_bk_rng rng;
        int bad = 0;

        cq_bk_rng_init(&rng, UINT64_C(0xD1B54A32D192ED03) ^ (uint64_t)r);

        for (int i = 0; i < 4096 && bad < 4; i++) {
            uint64_t v[3];
            const char *what = fpr_tuple(id, i, &rng, v);
            fpr_exp want = fpr_ref(id, v);
            fpr_exp got  = fpr_eval(id, v);

            CHECK_EQ(got.n, want.n);
            for (int w = 0; w < want.n; w++)
                if (got.o[w] != want.o[w]) {
                    cq_h_fail(__FILE__, __LINE__,
                              "%s_eval [%s]: %s = 0x%016llx, the IEEE model "
                              "says 0x%016llx (in = 0x%016llx, 0x%016llx, "
                              "0x%016llx)", ROWS[r].name, what,
                              fpr_out_name(id, w), (unsigned long long)got.o[w],
                              (unsigned long long)want.o[w],
                              (unsigned long long)v[0],
                              (unsigned long long)v[1],
                              (unsigned long long)v[2]);
                    bad++;
                }
        }
    }
}

/* ---- The two hand-off encodings (K23 §1.7, risk 2). --------------------- */

/* WRONG ONLY ON TIES, WHICH IS WHY THESE ARE PINNED AS LITERALS AND NOT AS
 * "whatever the model says". A one-bit disagreement about which lane of `wr`
 * is the guard — or about whether the leading 1 lands at bit 52 or 55 — is
 * right on every input that does not round, so an assertion phrased against
 * the model alone would move with the model. These four rows are the CONTRACT
 * M33, M34, M35, M39 and M40 have to satisfy when they build `wr`, written
 * down where a consumer can read it.
 *
 * `fadd.jl:13-14`: "bit 55 = implicit 1, bits 54-3 = 52-bit fraction, bits
 * 2/1/0 = G/R/S". `softfloat_common.jl:46-48`: norm52's leading 1 at bit 52.
 * `fdiv.jl:10-14`: why — the restoring loop assumes [2^52, 2^53). */
CQ_TEST(the_grs_and_bit52_handoff_encodings_are_what_the_source_says)
{
    cq_counter cnt;
    cq_sink s = (cq_count_reset(&cnt), cq_sink_counter(&cnt));
    static const int Q[3] = { 1, 1, 1 };
    cq_bit buf[CQ_FP64_W];
    /* (wr, rexp, rsign) -> normal_result, at rexp = 1023 (the value 1.0). */
    static const struct { uint64_t wr, want; const char *why; } T[] = {
        { UINT64_C(0x0080000000000004), UINT64_C(0x3ff0000000000000),
          "TIE DOWN: G set, R and S clear, frac even" },
        { UINT64_C(0x008000000000000C), UINT64_C(0x3ff0000000000002),
          "TIE UP: G set, R and S clear, frac odd" },
        { UINT64_C(0x0080000000000005), UINT64_C(0x3ff0000000000001),
          "the STICKY bit alone carries the tie upward" },
        { UINT64_C(0x0080000000000002), UINT64_C(0x3ff0000000000000),
          "the ROUND bit alone does not round: the guard is bit 2" },
        { UINT64_C(0x0080000000000003), UINT64_C(0x3ff0000000000000),
          "round and sticky without the guard still do not round" }
    };
    int ok;

    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        uint64_t v[3] = { T[i].wr, 0u, 0u };
        uint64_t got;

        v[1] = fpr_u64(1023);
        fpr_open(&FX, CQ_FPR_ROUND, v, Q, 0u, &s);
        fpr_forward(&FX);
        got = fpr_read(&FX.ctx, fpr_out_at(&FX, 0u, 0, buf), W64, &ok);
        if (!ok || got != T[i].want)
            cq_h_fail(__FILE__, __LINE__,
                      "G/R/S: wr = 0x%016llx gave 0x%016llx, the encoding says "
                      "0x%016llx — %s", (unsigned long long)T[i].wr,
                      (unsigned long long)got, (unsigned long long)T[i].want,
                      T[i].why);
        fpr_close(&FX);
    }

    /* The bit-52 hand-off: `m = 1` normalises to `1 << 52` and the exponent
     * falls by exactly 52. A ladder targeting 55 would give `1 << 55` and −55,
     * which is a plausible, well-formed, palindromic, ancilla-clean, wrong
     * circuit; `_sf_normalize_clz` in the next block is the row that DOES
     * target 55, and the two sit side by side here so a copied mask table is
     * visible. */
    {
        uint64_t v[3] = { UINT64_C(1), 0u, 0u };
        uint64_t m, e;

        v[1] = fpr_u64(0);
        fpr_open(&FX, CQ_FPR_NORM52, v, Q, 0u, &s);
        fpr_forward(&FX);
        m = fpr_read(&FX.ctx, fpr_out_at(&FX, 0u, 0, buf), W64, &ok);
        e = fpr_read(&FX.ctx, fpr_out_at(&FX, 0u, 1, buf), W64, &ok);
        CHECK_EQ((long long)m, (long long)(UINT64_C(1) << 52));
        CHECK_EQ((long long)fpr_i64(e), -52);
        fpr_close(&FX);

        fpr_open(&FX, CQ_FPR_CLZ, v, Q, 0u, &s);
        fpr_forward(&FX);
        m = fpr_read(&FX.ctx, fpr_out_at(&FX, 0u, 0, buf), W64, &ok);
        e = fpr_read(&FX.ctx, fpr_out_at(&FX, 0u, 1, buf), W64, &ok);
        CHECK_EQ((long long)m, (long long)(UINT64_C(1) << 55));
        CHECK_EQ((long long)fpr_i64(e), -55);
        fpr_close(&FX);
    }
}

/* ---- The signed/unsigned discriminator (K23 §5.4). ---------------------- */

/* EIGHT OF M32's NINE ORDERED COMPARES ARE SIGNED, and substituting
 * `cq_ult_block` for `cq_slt_block` leaves the gate SHAPE, the palindrome, the
 * slot scan and the scratch all correct — K9's `uge`-meaning-`ule` in a new
 * dress. This case is what goes red, and it names exactly which rows it can
 * and cannot reach, because the honest coverage claim is not "all eight".
 *
 * REACHED HERE, by a NEGATIVE `result_exp`: H1 (`result_exp <= 0`, which
 * decides `subnormal` and therefore both of the block's 64-lane outputs), R1
 * (`result_exp >= 0x7FF`, the `exp_overflow` output), R31 (the same on
 * `exp_after_round`), R32 and R34 (round-and-pack's clamp, both arms, which
 * reach `normal_result` through `exp_pack`). And by a LARGE POSITIVE
 * `result_exp`, which makes `shift_sub = 1 - result_exp` negative: H3, whose
 * `flush_to_zero` is an output of its own.
 *
 * NOT REACHED BY ANY VALUE COMPARISON, AND THIS IS THE FINDING: H4 and H6,
 * `_sf_handle_subnormal`'s own clamp. `shift_clamped` reaches `shift_u` only
 * when `subnormal` is true and `flush_to_zero` false, i.e. `shift_sub` in
 * [1, 55], where BOTH clamp arms are the identity whatever their signedness —
 * so the clamp is a discarded computation on every reachable input (PRD-v2
 * §7.6's discarded-arm rule). What sees a substitution there is L4: at W = 64
 * `cq_slt_steps` is 515 slots and `cq_ult_steps` is 385, so the golden moves
 * by 130 slots' worth of gates per row. The pair is the exact inverse of row
 * R15, the one UNSIGNED compare, which L4 sees and L1 cannot. */
CQ_TEST(a_signed_compare_swapped_to_unsigned_goes_red_here)
{
    cq_counter cnt;
    cq_sink s = (cq_count_reset(&cnt), cq_sink_counter(&cnt));
    static const int Q[3] = { 1, 1, 1 };
    cq_bit buf[CQ_FP64_W];
    uint64_t v[3];
    int ok;

    /* H1: result_exp = -5 is subnormal and the unsigned reading says it is
     * not, so `wr` comes out shifted by 6 with a sticky under the signed
     * reading and UNCHANGED under the unsigned one. */
    v[0] = UINT64_C(0x00FFFFFFFFFFFFFF); v[1] = fpr_u64(-5); v[2] = 0u;
    fpr_open(&FX, CQ_FPR_SUBNORM, v, Q, 0u, &s);
    fpr_forward(&FX);
    CHECK_EQ((long long)fpr_read(&FX.ctx, fpr_out_at(&FX, 0u, 2, buf), 1, &ok), 1);
    CHECK_EQ((long long)fpr_read(&FX.ctx, fpr_out_at(&FX, 0u, 0, buf), W64, &ok),
             (long long)UINT64_C(0x0003FFFFFFFFFFFF));
    CHECK(ok);
    fpr_close(&FX);

    /* H3: result_exp = 100 makes `shift_sub` = -99. Signed says that is not
     * >= 56; unsigned reads it as 2^64 - 99 and says it is. */
    v[0] = UINT64_C(0x0080000000000000); v[1] = fpr_u64(100); v[2] = 0u;
    fpr_open(&FX, CQ_FPR_SUBNORM, v, Q, 0u, &s);
    fpr_forward(&FX);
    CHECK_EQ((long long)fpr_read(&FX.ctx, fpr_out_at(&FX, 0u, 3, buf), 1, &ok), 0);
    CHECK(ok);
    fpr_close(&FX);

    /* R1 and R31: a negative exponent is not >= 0x7FF, and the unsigned
     * reading says both flags are set. R32 and R34: the clamp's lower arm
     * fires under the signed reading and the upper arm under the unsigned one,
     * so `normal_result`'s exponent field is 0 rather than 0x7FE. */
    v[0] = UINT64_C(0x0080000000000000); v[1] = fpr_u64(-1000); v[2] = 0u;
    fpr_open(&FX, CQ_FPR_ROUND, v, Q, 0u, &s);
    fpr_forward(&FX);
    CHECK_EQ((long long)fpr_read(&FX.ctx, fpr_out_at(&FX, 0u, 2, buf), 1, &ok), 0);
    CHECK_EQ((long long)fpr_read(&FX.ctx, fpr_out_at(&FX, 0u, 3, buf), 1, &ok), 0);
    CHECK_EQ((long long)fpr_read(&FX.ctx, fpr_out_at(&FX, 0u, 0, buf), W64, &ok),
             (long long)UINT64_C(0));
    CHECK(ok);
    fpr_close(&FX);
}

/* ---- Every anchor reaches its block and makes its predicate true. ------- */

/* A CASE COUNT CANNOT WITNESS "NOTHING WAS DROPPED" (bd 9ve.32's rider): the
 * budget can be right while an anchor row never runs. So each anchor is driven
 * through the MODEL and the predicate it claims to force is asserted directly
 * — and the two flush rows, the two tie rows and the carry-out row are the
 * ones a uniform draw of 32 reaches essentially never. */
CQ_TEST(every_anchor_reaches_its_block_and_makes_its_predicate_true)
{
    int seen_tie_down = 0, seen_tie_up = 0, seen_bump = 0, seen_ftz = 0;
    int seen_subnormal = 0, seen_mzero = 0, seen_ovf = 0, seen_after_only = 0;

    for (int r = 0; r < N_ROWS; r++) {
        cq_fpround_id id = ROWS[r].id;

        for (int i = 0; i < fpr_anchor_count(id); i++) {
            uint64_t v[3];
            const char *what = fpr_anchor_at(id, i, v);
            fpr_exp e = fpr_ref(id, v);
            uint64_t frac_in;

            (void)what;
            if (id == CQ_FPR_NORM52 && v[0] == 0u && e.o[0] == 0u
                && e.o[1] == v[1]) seen_mzero++;
            if (id == CQ_FPR_SUBNORM) {
                if (e.o[2]) seen_subnormal++;
                if (e.o[3]) seen_ftz++;
            }
            if (id == CQ_FPR_ROUND) {
                frac_in = (v[0] >> 3) & CQ_FP64_FRAC_MASK;
                if ((v[0] & 7u) == 4u && (frac_in & 1u) == 0u
                    && (e.o[0] & CQ_FP64_FRAC_MASK) == frac_in) seen_tie_down++;
                if ((v[0] & 7u) == 4u && (frac_in & 1u) != 0u
                    && (e.o[0] & CQ_FP64_FRAC_MASK) == frac_in + 1u) seen_tie_up++;
                if (frac_in == CQ_FP64_FRAC_MASK
                    && (e.o[0] & CQ_FP64_FRAC_MASK) == 0u) seen_bump++;
                if (e.o[2]) seen_ovf++;
                if (e.o[3] && !e.o[2]) seen_after_only++;
            }
        }
    }

    CHECK(seen_mzero      > 0);   /* tpg0's guard                            */
    CHECK(seen_subnormal  > 0);   /* §5.10 row 6                             */
    CHECK(seen_ftz        > 0);   /* row 7 — live in fdiv/fmul/fma, DEAD in fadd */
    CHECK(seen_tie_down   > 0);   /* row 3                                   */
    CHECK(seen_tie_up     > 0);   /* row 4                                   */
    CHECK(seen_bump       > 0);   /* row 5, the subnormal->normal carry       */
    CHECK(seen_ovf        > 0);   /* row 8                                   */
    CHECK(seen_after_only > 0);   /* row 9 — the ONLY row separating the two flags */
}
