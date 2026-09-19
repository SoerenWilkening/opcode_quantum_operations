/* tests/test_template_fbin.inc — the fp_arith BINARY surface at f64: `fadd`,
 * `fsub`, `fmul`, `fdiv`, over the nine `cq_shim_fbin_*` entry points.
 *
 * `frem` AND `fneg` ARE NOT HERE AND THAT IS THE POINT OF THE FILE'S FIRST
 * CASE. They are `fp_arith` rows too — the yaml gives that family five binary
 * opcodes and its one unary opcode — and neither has a kernel, so both keep
 * their `"fp is v2"` abort. A `LANDED` key of `(family, width)` would have
 * taken all six together; the key is `(opcode, width)` for exactly this
 * reason, and `tests/test_gen_bodies_provoked.py` provokes the coarse version.
 *
 * WHY THE VALUE CLAIMS RIDE THE ALL-CLASSICAL MASK. It is not a shortcut: the
 * §3 fold table dispatches on a bit's KIND and never on a qubit's VALUE (D6),
 * so at the all-quantum mask the emitted circuit is byte-identical across every
 * operand pair and the VALUE reaches the circuit only through classical lanes.
 * An operand-dependent defect — a NaN payload, a ±0 sign, a subnormal — is
 * therefore visible on the classical mask and invisible on the quantum one.
 * The quantum mask is used below only where the claim is about WIRES.
 */

/* M28's generated wrappers. Declared here rather than in a header for
 * tests/test_template_fcmp.inc's reason: they are `cq_template_*` symbols and
 * the compiler-checkable statement of their shape is
 * tests/abi/cq_templates_abi.txt, which tests/test_gen_shim.py diffs. A wrong
 * prototype here is a LINK error, not a silent mismatch. */
int32_t cq_template_fadd_f64(int32_t, int32_t);
int32_t cq_template_fadd_f64_hl(int32_t, double);
void    cq_template_fadd_f64_unc(int32_t, int32_t, int32_t);
void    cq_template_fadd_f64_hl_unc(int32_t, int32_t, double);
int32_t cq_template_fadd_f64_controlled(int32_t, int32_t, int32_t);
int32_t cq_template_fadd_f64_hl_controlled(int32_t, int32_t, double);

int32_t cq_template_fsub_f64(int32_t, int32_t);
int32_t cq_template_fsub_f64_hl(int32_t, double);
int32_t cq_template_fsub_f64_lh(double, int32_t);
void    cq_template_fsub_f64_unc(int32_t, int32_t, int32_t);
void    cq_template_fsub_f64_hl_unc(int32_t, int32_t, double);
void    cq_template_fsub_f64_lh_unc(int32_t, double, int32_t);
int32_t cq_template_fsub_f64_controlled(int32_t, int32_t, int32_t);
int32_t cq_template_fsub_f64_lh_controlled(int32_t, double, int32_t);

int32_t cq_template_fmul_f64(int32_t, int32_t);
int32_t cq_template_fmul_f64_hl(int32_t, double);
void    cq_template_fmul_f64_unc(int32_t, int32_t, int32_t);
void    cq_template_fmul_f64_hl_unc(int32_t, int32_t, double);
int32_t cq_template_fmul_f64_controlled(int32_t, int32_t, int32_t);

int32_t cq_template_fdiv_f64(int32_t, int32_t);
int32_t cq_template_fdiv_f64_hl(int32_t, double);
int32_t cq_template_fdiv_f64_lh(double, int32_t);
void    cq_template_fdiv_f64_unc(int32_t, int32_t, int32_t);
void    cq_template_fdiv_f64_hl_unc(int32_t, int32_t, double);
void    cq_template_fdiv_f64_lh_unc(int32_t, double, int32_t);
int32_t cq_template_fdiv_f64_controlled(int32_t, int32_t, int32_t);
int32_t cq_template_fdiv_f64_lh_controlled(int32_t, double, int32_t);

/* The twenty `_inv` symbols D14 still refuses. Declared with the rest because a
 * wrong prototype here is a LINK error rather than a silent mismatch, and
 * because their SIGNATURES are what the `_inv` case's thunks are written
 * against — `fsub_f64_lh_inv` takes `(double, int32_t)` and its `_controlled`
 * twin `(int32_t, double, int32_t)`, which is the one shape a copy-paste of the
 * `qq` row gets wrong. */
int32_t cq_template_fadd_f64_inv(int32_t, int32_t);
int32_t cq_template_fadd_f64_hl_inv(int32_t, double);
int32_t cq_template_fadd_f64_controlled_inv(int32_t, int32_t, int32_t);
int32_t cq_template_fadd_f64_hl_controlled_inv(int32_t, int32_t, double);
int32_t cq_template_fsub_f64_inv(int32_t, int32_t);
int32_t cq_template_fsub_f64_hl_inv(int32_t, double);
int32_t cq_template_fsub_f64_lh_inv(double, int32_t);
int32_t cq_template_fsub_f64_controlled_inv(int32_t, int32_t, int32_t);
int32_t cq_template_fsub_f64_hl_controlled_inv(int32_t, int32_t, double);
int32_t cq_template_fsub_f64_lh_controlled_inv(int32_t, double, int32_t);
int32_t cq_template_fmul_f64_inv(int32_t, int32_t);
int32_t cq_template_fmul_f64_hl_inv(int32_t, double);
int32_t cq_template_fmul_f64_controlled_inv(int32_t, int32_t, int32_t);
int32_t cq_template_fmul_f64_hl_controlled_inv(int32_t, int32_t, double);
int32_t cq_template_fdiv_f64_inv(int32_t, int32_t);
int32_t cq_template_fdiv_f64_hl_inv(int32_t, double);
int32_t cq_template_fdiv_f64_lh_inv(double, int32_t);
int32_t cq_template_fdiv_f64_controlled_inv(int32_t, int32_t, int32_t);
int32_t cq_template_fdiv_f64_hl_controlled_inv(int32_t, int32_t, double);
int32_t cq_template_fdiv_f64_lh_controlled_inv(int32_t, double, int32_t);

/* The four rows. `fwd_lh`/`unc_lh` are NULL on `fadd` and `fmul`, which is the
 * ABI's own shape and not an omission: `opcode_table.yaml:200,202` give those
 * two ten variants and the non-commutative pair fifteen, exactly as the
 * commutative integer opcodes have no `_lh` either. A row that invented one
 * would be a phantom symbol and a link error. */
typedef struct {
    const char *name;
    cq_shim_fop op;
    uint64_t  (*eval)(uint64_t, uint64_t);
    int32_t   (*fwd)(int32_t, int32_t);
    int32_t   (*fwd_hl)(int32_t, double);
    int32_t   (*fwd_lh)(double, int32_t);
    void      (*unc)(int32_t, int32_t, int32_t);
    void      (*unc_hl)(int32_t, int32_t, double);
    void      (*unc_lh)(int32_t, double, int32_t);
} fbin_row;

static const fbin_row FBIN_ROWS[] = {
    { "fadd", CQ_SHIM_FOP_FADD, cq_fadd_eval,
      cq_template_fadd_f64, cq_template_fadd_f64_hl, NULL,
      cq_template_fadd_f64_unc, cq_template_fadd_f64_hl_unc, NULL },
    { "fsub", CQ_SHIM_FOP_FSUB, cq_fsub_eval,
      cq_template_fsub_f64, cq_template_fsub_f64_hl, cq_template_fsub_f64_lh,
      cq_template_fsub_f64_unc, cq_template_fsub_f64_hl_unc,
      cq_template_fsub_f64_lh_unc },
    { "fmul", CQ_SHIM_FOP_FMUL, cq_fmul_eval,
      cq_template_fmul_f64, cq_template_fmul_f64_hl, NULL,
      cq_template_fmul_f64_unc, cq_template_fmul_f64_hl_unc, NULL },
    { "fdiv", CQ_SHIM_FOP_FDIV, cq_fdiv_eval,
      cq_template_fdiv_f64, cq_template_fdiv_f64_hl, cq_template_fdiv_f64_lh,
      cq_template_fdiv_f64_unc, cq_template_fdiv_f64_hl_unc,
      cq_template_fdiv_f64_lh_unc }
};

#define FBIN_N (sizeof FBIN_ROWS / sizeof FBIN_ROWS[0])

static uint32_t popcount64(uint64_t v)
{
    uint32_t n = 0u;

    while (v != 0u) { n += (uint32_t)(v & 1u); v >>= 1; }
    return n;
}

/* THE DISPATCH ORACLE AND L5 IN ONE CASE. 32 calls, ZERO gates and ZERO
 * qubits — PRD-v2 §3.1 item 3's claim that a fully classical fp program costs
 * nothing, at four opcodes whose circuits are 78k to 295k gates each — and
 * every result compared against the kernel's own `_eval`.
 *
 * THIS IS THE SOLE DETECTOR FOR A TRANSPOSED `cq_tpl_fbin_kernel` ROW, which
 * is the K9 `uge`-meaning-`ule` trap in a fourth column: `fadd` where `fsub`
 * belongs emits the SAME gate tuple at the same width (M33 runs one program
 * under two prologues), keeps the palindrome, leaves scratch clean, and moves
 * no count anywhere. Only a VALUE against a reference that is not the dispatch
 * table tells them apart, and the ±0 and NaN rows are where `fadd` and `fsub`
 * genuinely disagree. */
CQ_TEST(every_fbin_f64_wrapper_agrees_with_its_kernels_own_eval)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);

    for (size_t r = 0; r < FBIN_N; r++) {
        for (size_t p = 0; p < FP_N_PAIRS; p++) {
            const int32_t ha  = cqrt_alloc_f64(f64_of(FP_PAIRS[p][0]));
            const int32_t hb  = cqrt_alloc_f64(f64_of(FP_PAIRS[p][1]));
            const int32_t out = FBIN_ROWS[r].fwd(ha, hb);
            const uint64_t got  = cq_pc_value(ctx, out);
            const uint64_t want = FBIN_ROWS[r].eval(FP_PAIRS[p][0],
                                                    FP_PAIRS[p][1]);

            if (cq_reg_width(&ctx->regs, out) != 64u)
                cq_h_fail(__FILE__, __LINE__, "%s: result rail is %u bits, not 64",
                          FBIN_ROWS[r].name, cq_reg_width(&ctx->regs, out));
            if (got != want)
                cq_h_fail(__FILE__, __LINE__,
                          "%s(%016llx, %016llx) = %016llx, the kernel's own "
                          "eval says %016llx", FBIN_ROWS[r].name,
                          (unsigned long long)FP_PAIRS[p][0],
                          (unsigned long long)FP_PAIRS[p][1],
                          (unsigned long long)got, (unsigned long long)want);
        }
    }

    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    cq_reg_audit(ctx);
    close_with(&m);
}

/* THE LITERAL DOORS, AND `fsub`/`fdiv` ARE THE ORDER DETECTORS. For `fadd` and
 * `fmul` the `_hl` door is the only one the ABI ships, precisely because a
 * transposition would be invisible there; the two non-commutative opcodes have
 * `_lh` and this case is what pins which side of them the literal lands on.
 *
 * AND THE LITERAL IS A BIT PATTERN, WHICH IS THE ONE THING AN fp WRAPPER HAS
 * NO INTEGER ANALOGUE FOR. `CQ_SHIM_LO(3.5)` is `(uint64_t)(unsigned
 * __int128)3.5` — the integer 3 — and it compiles clean, is silent under
 * -Wconversion because the cast is explicit, links, and emits the identical
 * gate count. This case drives the GENERATED body, so it sees the macro the
 * generator actually chose rather than the one `cq_shim.h` defines. */
CQ_TEST(the_fbin_literal_doors_carry_the_pattern_on_the_side_they_name)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    static const uint64_t FIVE = 0x4014000000000000ull;   /* 5.0  */
    static const uint64_t TWO  = 0x4000000000000000ull;   /* 2.0  */
    const int32_t h5 = cqrt_alloc_f64(f64_of(FIVE));

    CHECK_EQ(cq_pc_value(ctx, cq_template_fsub_f64_hl(h5, 2.0)),
             cq_fsub_eval(FIVE, TWO));
    CHECK_EQ(cq_pc_value(ctx, cq_template_fsub_f64_lh(2.0, h5)),
             cq_fsub_eval(TWO, FIVE));
    CHECK_EQ(cq_pc_value(ctx, cq_template_fdiv_f64_hl(h5, 2.0)),
             cq_fdiv_eval(FIVE, TWO));
    CHECK_EQ(cq_pc_value(ctx, cq_template_fdiv_f64_lh(2.0, h5)),
             cq_fdiv_eval(TWO, FIVE));

    /* `fsub_eval(5,2)` and `fsub_eval(2,5)` differ only in the SIGN BIT, so a
     * transposition moves exactly one lane. Asserted, because two expected
     * values that happened to be equal would make the two lines above a pair
     * of tautologies. */
    CHECK(cq_fsub_eval(FIVE, TWO) != cq_fsub_eval(TWO, FIVE));
    CHECK(cq_fdiv_eval(FIVE, TWO) != cq_fdiv_eval(TWO, FIVE));

    /* AND THE PATTERN REALLY ARRIVED. 3.5 is the discriminator and 1.0 would
     * not be: `(uint64_t)1.0` is 1, so a numeric conversion and a
     * reinterpretation of 1.0 are both "small", while 3.5 converts to 3 and
     * reinterprets to 0x400C000000000000 — and `fadd(+0, x)` is the identity
     * on x in both readings, so the rail's BITS are the whole answer. */
    {
        const int32_t z = cqrt_alloc_f64(f64_of(CQ_F64_POS_ZERO));
        const int32_t r = cq_template_fadd_f64_hl(z, 3.5);

        CHECK_EQ(cq_pc_value(ctx, r), 0x400C000000000000ull);
        CHECK(cq_pc_value(ctx, r) != cq_fadd_eval(CQ_F64_POS_ZERO, 3ull));
    }
    CHECK_EQ(cq_mock_count(&m), 0);
    close_with(&m);
}

/* THE ONE ALL-QUANTUM ROUND TRIP PER FAMILY: the value on wires, L2 after the
 * forward AND after the `_unc` AND after the free, the `_unc` zeroing its out
 * rail, and the pool restored per INDEX rather than by a count.
 *
 * FOUR FORWARDS AND FOUR `_unc`s IS 1.37 MILLION GATES through a recording
 * sink, and that is why there is exactly one pair per family and why the
 * operand pair is the cheapest one that is not a fold — the dispatch claim
 * belongs to the classical case above, which costs nothing.
 *
 * `minted` AND THE FREE-LIST LENGTH ARE NEVER COMPARED: both are monotone, so
 * a round trip that allocates and hands back necessarily leaves them higher.
 * "The pool is restored" is `live`, plus the strictly stronger per-index claim
 * that each of `out`'s indices is back on the free list — which also catches a
 * free that released the WRONG index, as no count can. */
CQ_TEST(an_fbin_f64_round_trip_on_wires_is_the_value_l2_and_the_pool)
{
    for (size_t r = 0; r < FBIN_N; r++) {
        cq_mock m; cq_sink s;
        cq_ctx *ctx = open_with(&m, &s);
        const uint64_t av = FP_PAIRS[0][0], bv = FP_PAIRS[0][1];
        const int32_t ha = qreg(ctx, 64u, av);
        const int32_t hb = qreg(ctx, 64u, bv);
        const cq_pc_snap before = cq_pc_take(ctx);
        const int32_t out = FBIN_ROWS[r].fwd(ha, hb);
        const int32_t live[3] = { ha, hb, out };
        uint32_t idx[64];
        uint32_t n;

        CHECK_EQ(cq_reg_width(&ctx->regs, out), 64);
        if (cq_pc_value(ctx, out) != FBIN_ROWS[r].eval(av, bv))
            cq_h_fail(__FILE__, __LINE__, "%s on wires = %016llx, eval says %016llx",
                      FBIN_ROWS[r].name,
                      (unsigned long long)cq_pc_value(ctx, out),
                      (unsigned long long)FBIN_ROWS[r].eval(av, bv));

        /* Rule 7's second clause at the handle boundary: the sources are
         * unchanged. A kernel writing a source, or a missing D7b copy, shows
         * up here and in nothing else this file runs. */
        CHECK_EQ(cq_pc_value(ctx, ha), av);
        CHECK_EQ(cq_pc_value(ctx, hb), bv);
        CHECK(cq_pc_live_is_exactly(ctx, live, 3u));

        n = cq_pc_indices(ctx, out, idx, 64u);
        FBIN_ROWS[r].unc(out, ha, hb);
        CHECK_EQ(cq_pc_value(ctx, out), 0);
        CHECK(cq_pc_live_is_exactly(ctx, live, 3u));

        cqrt_free(out);
        if (cq_qubits_stranded(&ctx->pool) != 0u)
            cq_h_fail(__FILE__, __LINE__, "%s: the free stranded %u qubit(s)",
                      FBIN_ROWS[r].name, cq_qubits_stranded(&ctx->pool));
        CHECK_EQ(cq_reg_strand_reports(ctx), 0);
        CHECK(cq_pc_same(before, cq_pc_take(ctx)));
        CHECK(cq_pc_indices_are_free(ctx, idx, n));
        cq_reg_audit(ctx);

        /* The OPERANDS are deliberately never freed: they hold a non-zero
         * value on real wires, so their free is D15's proven-dirty row and
         * strands by design — measured once already on this surface, where it
         * made the NEXT case's strand assertion red for a reason that had
         * nothing to do with the certificate. */
        close_with(&m);
    }
}

/* THE SAME ROUND TRIP WITH THE SOURCES ROTATION-POISONED, WHICH IS THE ONLY
 * VERSION OF IT THE CERTIFICATE ACTUALLY ANSWERS.
 *
 * `cq_shim_free_proof` is the certificate AND the shadow, and the shadow is
 * EXACT wherever no general `Ry` has run. `fadd` emits no rotation, so on
 * ordinary operands the shadow proves `out` clean by itself and every defect
 * in D15's twin identity is invisible — measured on the fcmp surface, where
 * dropping `CQ_ROP_TPL_FWD`'s write row left the unpoisoned case GREEN. A
 * general `Ry` off §7's lattice is the one row that poisons (PRD §15 D12), and
 * after it the free is the certificate's alone.
 *
 * THE NEGATIVE CONTROL IS WHAT MAKES THE REST OF THE CASE A CLAIM. Without the
 * `CQ_PROOF_UNPROVEN` assertion a change that stopped poisoning would leave
 * every line below green for the wrong reason.
 *
 * BOTH LANES, because the `_hl` literal enters D15's twin identity through
 * `r.lo ^ r.hi` and the `qq` lane does not, so an `imm` or tag defect can kill
 * one and spare the other. */
CQ_TEST(an_fbin_f64_unc_on_a_poisoned_source_is_discharged_by_the_certificate)
{
    for (size_t r = 0; r < FBIN_N; r++) {
        cq_mock m; cq_sink s;
        cq_ctx *ctx = open_with(&m, &s);
        const int32_t ha = qreg(ctx, 64u, FP_PAIRS[0][0]);
        const int32_t hb = qreg(ctx, 64u, FP_PAIRS[0][1]);
        uint32_t strand_before;
        int32_t probe;

        /* §7's GENERAL row on the f64 surface, through the frozen symbol a
         * CQ_lang fixture would call — which is also what puts a `CQ_ROP_RY`
         * in the record for the engine's operand-stability check to see. */
        cqrt_ry_f64(ha, 0.5);
        cqrt_ry_f64(hb, 0.5);
        strand_before = cq_qubits_stranded(&ctx->pool);

        probe = FBIN_ROWS[r].fwd(ha, hb);
        CHECK_EQ(cq_reg_disposition(ctx, probe, cq_pc_zero_proof_rotation_free),
                 CQ_PROOF_UNPROVEN);
        FBIN_ROWS[r].unc(probe, ha, hb);
        cqrt_free(probe);
        if (cq_qubits_stranded(&ctx->pool) - strand_before != 0u)
            cq_h_fail(__FILE__, __LINE__,
                      "%s (qq): the certificate did not discharge a poisoned "
                      "round trip — %u qubit(s) stranded", FBIN_ROWS[r].name,
                      cq_qubits_stranded(&ctx->pool) - strand_before);
        CHECK_EQ(cq_reg_strand_reports(ctx), 0u);

        probe = FBIN_ROWS[r].fwd_hl(ha, 2.0);
        FBIN_ROWS[r].unc_hl(probe, ha, 2.0);
        cqrt_free(probe);
        if (cq_qubits_stranded(&ctx->pool) - strand_before != 0u)
            cq_h_fail(__FILE__, __LINE__,
                      "%s (hl): the certificate did not discharge a poisoned "
                      "round trip — %u qubit(s) stranded", FBIN_ROWS[r].name,
                      cq_qubits_stranded(&ctx->pool) - strand_before);
        CHECK_EQ(cq_reg_strand_reports(ctx), 0u);

        cq_reg_audit(ctx);
        close_with(&m);
    }
}

/* THE NEGATIVE HALF: AN `fadd` `_unc` MUST NOT PAIR WITH AN `add i64` FORWARD.
 * This is what makes the fp binary family's own TAG SPACE a tested fact rather
 * than a comment, and it is the fcmp-versus-icmp case one family over.
 *
 * THE SHAPE IS EXACT AND EVERY PART OF IT IS LOAD-BEARING. `adjoint_matches`
 * pairs two records when the earlier one's effect row declares the later's
 * opcode as its twin AND the tags are equal AND all four handle slots match
 * AND the §9 context and the `cqrt_x` parities match. `cq_shim_bin_qq(ADD, 64,
 * a, b)` and `cq_shim_fbin_qq_unc(FADD, 64, out, a, b)` agree on every one of
 * those — both are TPL_FWD/TPL_UNC, both at width 64, and `CQ_SHIM_OP_ADD` and
 * `CQ_SHIM_FOP_FADD` are both index 0 — so only the `0x00000000` versus
 * `0x10000000` prefix keeps them apart.
 *
 * THE OPERANDS MUST BE POISONED OR THE SHADOW ANSWERS FIRST: on the
 * rotation-free surface the shadow sees the rail is non-zero and strands
 * whatever the certificate says, so the mutant survives. With a general `Ry`
 * the shadow is UNPROVEN and CLEAN-from-a-wrong-pair beats it and RELEASES a
 * rail nothing uncomputed — the only unforgivable bug, arriving through a
 * four-hex-digit constant.
 *
 * 1.0 + 1.0 IS WHY THE TWO OPERATIONS DISAGREE. The integer `add` of
 * 0x3FF0000000000000 twice is 0x7FE0000000000000; `fadd` of 1.0 and 1.0 is
 * 0x4000000000000000. An operand pair the two agreed about would leave the
 * rail at zero and the case would pass under the mutant for the wrong
 * reason. */
CQ_TEST(an_fadd_unc_does_not_pair_with_an_add_i64_forward_on_the_same_handles)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t a = qreg(ctx, 64u, CQ_F64_ONE);
    const int32_t b = qreg(ctx, 64u, CQ_F64_ONE);
    uint32_t strand_before;
    int32_t out;

    /* The two operations really do disagree here — asserted rather than
     * asserted-by-eye, because if they agreed the rail would end at zero and
     * the shadow would release it under the mutant too. */
    CHECK(cq_fadd_eval(CQ_F64_ONE, CQ_F64_ONE) != (CQ_F64_ONE + CQ_F64_ONE));

    cqrt_ry_f64(a, 0.5);
    cqrt_ry_f64(b, 0.5);
    strand_before = cq_qubits_stranded(&ctx->pool);

    out = cq_shim_bin_qq(CQ_SHIM_OP_ADD, 64, a, b);
    /* The `_unc` of a DIFFERENT operation on the same handles. It is a legal
     * call — nothing refuses it and nothing should; the certificate's job is
     * to decline to discharge it. */
    cq_shim_fbin_qq_unc(CQ_SHIM_FOP_FADD, 64, out, a, b);
    cqrt_free(out);

    /* STRANDED, LOUDLY, one qubit per lane the two operations disagree about,
     * and the one-shot report fired. That is D15 §3's disposition working, not
     * a failure: releasing here would be the silent state collapse Rule 6
     * exists to prevent. */
    CHECK(cq_qubits_stranded(&ctx->pool) > strand_before);
    CHECK_EQ(cq_reg_strand_reports(ctx), 1u);

    cq_reg_audit(ctx);
    close_with(&m);
}

/* PRD §9 ROW 0 ON THE fp SURFACE — a decision, not a circuit. A CQ_BIT_ZERO
 * flag skips the region entirely (0 gates AND 0 qubits, which needs
 * `cq_sandwich`'s own short-circuit as well as the emitter's, because the
 * driver pre-materialises its whole scratch region before any gate is
 * emitted), and a CQ_BIT_ONE flag emits it VERBATIM.
 *
 * COMPARES HAVE NO CONTROLLED AXIS AND `fp_arith` DOES, which is exactly the
 * difference `shim/cq_template_fp.c`'s recorded COMPARE<->ARITHMETIC seam
 * turns on — so this is the first fp case in the project that opens a §9
 * region at all. `fadd` because it is the cheapest of the four. */
CQ_TEST(a_controlled_fbin_call_obeys_row_zero_on_a_classical_flag)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t a = qreg(ctx, 64u, FP_PAIRS[0][0]);
    const int32_t b = qreg(ctx, 64u, FP_PAIRS[0][1]);
    const int32_t zero = cq_reg_alloc_const(&ctx->regs, 1u, 0u, 0u);
    const int32_t one  = cq_reg_alloc_const(&ctx->regs, 1u, 1u, 0u);
    const cq_pc_snap pre = cq_pc_take(ctx);
    int32_t rz, ro, rp;
    size_t promoted;

    cq_mock_reset(&m);
    rz = cq_template_fadd_f64_controlled(zero, a, b);
    CHECK_EQ((int)cq_mock_count(&m), 0);
    CHECK_EQ(cq_pc_value(ctx, rz), 0u);
    /* `live` moved by nothing: the result rail is all-constant, so by I4 it
     * owns no qubit. A region that emitted its body and then folded would move
     * it by 64. */
    CHECK(cq_pc_same(cq_pc_take(ctx), pre));

    cq_mock_reset(&m);
    ro = cq_template_fadd_f64_controlled(one, a, b);
    promoted = cq_mock_count(&m);

    cq_mock_reset(&m);
    rp = cq_template_fadd_f64(a, b);
    CHECK_EQ((int)promoted, (int)cq_mock_count(&m));
    CHECK_EQ(cq_pc_value(ctx, ro), cq_pc_value(ctx, rp));
    CHECK_EQ(cq_ctrl_depth(ctx), 0);

    cq_reg_audit(ctx);
    close_with(&m);
}

/* A QUANTUM FLAG, AND L5's "ZERO GATES, ZERO QUBITS" IS FALSE HERE — WHICH IS
 * THE CORRECT ANSWER RATHER THAN A REGRESSION. Under a quantum control the
 * kernel's classical short-circuit still writes `dst` through `cq_emit_x`,
 * whose constant row cannot fire (rewriting a bit in place would run on BOTH
 * branches — PRD §9 row A, the single most dangerous defect the axis can
 * carry), so every set lane of the result becomes a wire driven by a real CX.
 *
 * THE OPERANDS ARE CONSTANT ON PURPOSE. This is the shape CLAUDE.md names as
 * the only fixture that can see a controlled region silently made
 * unconditional, and it costs `popcount(result)` gates instead of the 235,568
 * an all-quantum `fdiv` would — so all four opcodes can run it. */
CQ_TEST(a_quantum_flag_makes_the_classical_short_circuit_a_wire)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t flag = qreg(ctx, 1u, 1u);

    for (size_t r = 0; r < FBIN_N; r++) {
        const uint64_t want = FBIN_ROWS[r].eval(FP_PAIRS[0][0], FP_PAIRS[0][1]);
        const uint32_t lanes = popcount64(want);
        const int32_t ha = cqrt_alloc_f64(f64_of(FP_PAIRS[0][0]));
        const int32_t hb = cqrt_alloc_f64(f64_of(FP_PAIRS[0][1]));
        const uint32_t live_before = cq_qubits_live(&ctx->pool);
        int32_t out;

        CHECK(lanes > 0u);      /* the claim is vacuous on an all-zero result */
        cq_mock_reset(&m);
        out = cq_shim_fbin_qq_ctrl(FBIN_ROWS[r].op, 64, flag, ha, hb);

        if (cq_mock_count_op(&m, CQ_OP_CX) != lanes ||
            cq_mock_count_op(&m, CQ_OP_X) != 0u ||
            cq_mock_count_op(&m, CQ_OP_CCX) != 0u)
            cq_h_fail(__FILE__, __LINE__,
                      "%s under a quantum flag emitted (x %zu, cx %zu, ccx %zu); "
                      "expected (0, %u, 0) — one CX per set lane of the result",
                      FBIN_ROWS[r].name, cq_mock_count_op(&m, CQ_OP_X),
                      cq_mock_count_op(&m, CQ_OP_CX),
                      cq_mock_count_op(&m, CQ_OP_CCX), lanes);
        CHECK_EQ(cq_qubits_live(&ctx->pool) - live_before, lanes);
        CHECK_EQ(cq_pc_value(ctx, out), want);
        CHECK_EQ(cq_ctrl_depth(ctx), 0);
    }
    cq_reg_audit(ctx);
    close_with(&m);
}

/* D7b ON THE fp DOORS. Two sources aliasing each other is LEGAL at the handle
 * boundary and its remedy is M26's defensive copy, in ONE place — this case is
 * what says the fp families reach that one place rather than growing their
 * own.
 *
 * THE INSTRUMENT IS THE GATE MIX AGAINST AN UNALIASED SIBLING AT THE SAME
 * WIDTH AND THE SAME MASK, because writing the expected tuple down would pin
 * M33's cost as well and would move for reasons that have nothing to do with
 * D7b. EXACTLY 2W more CX and EXACTLY as many Toffolis: a copy emitted INSIDE
 * the §9 region would move both numbers by 2W in opposite directions, and a
 * `>=` would have passed.
 *
 * THE SOURCE IS DETERMINATE, NOT POISONED, AND THAT IS THE SHARPER FIXTURE.
 * With a poisoned source a correct temporary and a never-uncopied one both
 * strand W and are indistinguishable; with a determinate one a correct
 * temporary is RELEASED, so `cq_qubits_stranded() == 0` is the claim — and it
 * is taken BEFORE any other rail is freed, because a determinate non-zero rail
 * is convicted lane by lane and would move the count for an unrelated
 * reason. */
CQ_TEST(d7b_on_the_fp_doors_costs_exactly_two_w_cnots_over_an_unaliased_call)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t a  = qreg(ctx, 64u, FP_PAIRS[0][0]);
    const int32_t a2 = qreg(ctx, 64u, FP_PAIRS[0][0]);
    size_t p_cx, p_ccx;
    int32_t plain, r;

    cq_mock_reset(&m);
    plain = cq_template_fadd_f64(a, a2);
    p_cx  = cq_mock_count_op(&m, CQ_OP_CX);
    p_ccx = cq_mock_count_op(&m, CQ_OP_CCX);

    cq_mock_reset(&m);
    r = cq_template_fadd_f64(a, a);

    CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0u);
    CHECK_EQ((int)cq_mock_count_op(&m, CQ_OP_CX),  (int)(p_cx + 2u * 64u));
    CHECK_EQ((int)cq_mock_count_op(&m, CQ_OP_CCX), (int)p_ccx);
    /* The value is the same function of one operand twice, and the temporary
     * is gone: L2 sees no index that no named register owns. */
    CHECK_EQ(cq_pc_value(ctx, r), cq_pc_value(ctx, plain));
    {
        const int32_t live[4] = { a, a2, plain, r };
        CHECK(cq_pc_live_is_exactly(ctx, live, 4u));
    }
    cq_reg_audit(ctx);
    close_with(&m);
}

/* ALL TWENTY `_inv` BODIES ABORT, AND THE REASON IS D14's RATHER THAN "fp is
 * v2". That distinction is a FALSEHOOD PRINTED AT RUNTIME if it goes the other
 * way: `fadd` at f64 SHIPS, so telling a caller of `cq_template_fadd_f64_inv`
 * to wait for the fp release is pointing at a release that has already
 * happened. What D14 actually says is that `_inv` is f-INVERSE and the data
 * family has no uniform definition — true of `fadd` (whose inverse exists and
 * is `fsub`, i.e. a DIFFERENT symbol) and of `fdiv` alike.
 *
 * IT FORKS RATHER THAN BEING A DEATH CASE, for tests/test_runtime_v2.c's
 * reason: a death case can say only "it aborted", and the interesting failure
 * is an abort naming the wrong SYMBOL or carrying the wrong BUCKET. */
#define FBIN_INV_ROWS(X)                                                      \
    X(fadd_f64_inv,                  (0, 0))                                  \
    X(fadd_f64_hl_inv,               (0, 0.0))                                \
    X(fadd_f64_controlled_inv,       (0, 0, 0))                               \
    X(fadd_f64_hl_controlled_inv,    (0, 0, 0.0))                             \
    X(fsub_f64_inv,                  (0, 0))                                  \
    X(fsub_f64_hl_inv,               (0, 0.0))                                \
    X(fsub_f64_lh_inv,               (0.0, 0))                                \
    X(fsub_f64_controlled_inv,       (0, 0, 0))                               \
    X(fsub_f64_hl_controlled_inv,    (0, 0, 0.0))                             \
    X(fsub_f64_lh_controlled_inv,    (0, 0.0, 0))                             \
    X(fmul_f64_inv,                  (0, 0))                                  \
    X(fmul_f64_hl_inv,               (0, 0.0))                                \
    X(fmul_f64_controlled_inv,       (0, 0, 0))                               \
    X(fmul_f64_hl_controlled_inv,    (0, 0, 0.0))                             \
    X(fdiv_f64_inv,                  (0, 0))                                  \
    X(fdiv_f64_hl_inv,               (0, 0.0))                                \
    X(fdiv_f64_lh_inv,               (0.0, 0))                                \
    X(fdiv_f64_controlled_inv,       (0, 0, 0))                               \
    X(fdiv_f64_hl_controlled_inv,    (0, 0, 0.0))                             \
    X(fdiv_f64_lh_controlled_inv,    (0, 0.0, 0))

#define FBIN_INV_THUNK(sym, args)                                             \
    static void inv_call_##sym(void *p)                                       \
    { (void)p; (void)cq_template_##sym args; }
FBIN_INV_ROWS(FBIN_INV_THUNK)

#define FBIN_INV_ENTRY(sym, args) { #sym, inv_call_##sym },
static const struct { const char *sym; void (*fn)(void *); } FBIN_INVS[] = {
    FBIN_INV_ROWS(FBIN_INV_ENTRY)
};

#define FBIN_N_INVS (sizeof FBIN_INVS / sizeof FBIN_INVS[0])

CQ_TEST(every_fbin_f64_inv_aborts_naming_itself_and_d14s_reason)
{
    char msg[512];

    CHECK_EQ((int)FBIN_N_INVS, 20);
    for (size_t i = 0; i < FBIN_N_INVS; i++) {
        char want[320];
        const int aborted = cq_child_capture(CQ_CHILD_STDERR, FBIN_INVS[i].fn,
                                             NULL, NULL, 0u, msg, sizeof msg);
        CHECK(aborted);
        (void)snprintf(want, sizeof want,
                       "cqops: cq_template_%s not implemented "
                       "(_inv is f-inverse, not f; the data family has no "
                       "uniform definition (PRD 15 D14))\n", FBIN_INVS[i].sym);
        if (strcmp(msg, want) != 0)
            cq_h_fail(__FILE__, __LINE__, "%s said `%s`, wanted `%s`",
                      FBIN_INVS[i].sym, msg, want);
    }
}
