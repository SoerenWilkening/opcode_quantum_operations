/* tests/test_template_fcmp.inc — the fp OPCODE surface (PRD-v2 §1, bead
 * 9ve.28): the four `cq_shim_fcmp_*` entry points, the 56 generated wrappers
 * that call them, and the 28 `_inv` bodies D14 still refuses.
 *
 * WHY AN `.inc` AND WHY THIS SUBJECT. tests/test_template.c stood at 268 of
 * Rule 12's 300 and its own header records two seams already; this is a third,
 * on the same discriminator the shim took one file over — `the INTEGER opcode
 * surface <-> the fp opcode surface`. Every case here turns on something only
 * an fp family has: a second predicate enum in a different order, a classical
 * operand that is a BIT PATTERN rather than a number, and a width the ABI names
 * at four values of which v2 implements one.
 *
 * THE ORACLE IS THE HOST'S COMPARISON OPERATORS, AND THAT IS SOUND HERE WHERE
 * IT WOULD NOT BE FOR `fadd`. PRD-v2 §7.4 refuses the host operator for
 * ARITHMETIC because the IEEE-unspecified cells — `Inf − Inf`, `0 · Inf`, which
 * NaN payload survives — are x86's choices and ARM differs on all of them. A
 * COMPARISON has no unspecified cells: IEEE 754 fixes all fourteen predicates on
 * every operand pair including every NaN, and C's `<`, `==`, `<=` are exactly
 * the ordered ones with the unordered answer folded in. So `host_fcmp` below is
 * a genuinely independent statement of LLVM's fourteen rows, it reads the same
 * on every host, and it shares no code with `cq_fcmp_eval` — which matters,
 * because `cq_fcmp_eval` IS the kernel's own classical row and an oracle built
 * from it could not see a wrong dispatch on the all-classical mask.
 *
 * WHAT A WRONG DISPATCH LOOKS LIKE, AND WHY THE SWEEP IS OVER ALL FOURTEEN
 * PREDICATES AND NINE OPERAND PAIRS. `uge` spelled as `ule` emits the same
 * tuple, keeps the palindrome and leaves scratch clean — K9's recorded trap, and
 * `fcmp` has a second one on top of it: `ult`/`ugt`/`ule`/`uge` are mnemonics in
 * BOTH predicate lists, so an fcmp body carrying `CQ_SHIM_PRED_ULT` compiles,
 * links and dispatches the INTEGER comparator on an IEEE pattern. Only a value
 * separates them, and only on a pair the two rows disagree about — which is why
 * the pairs include both orders of every asymmetric shape and every unordered
 * row.
 *
 * THERE IS NO `_controlled` AXIS AND THAT IS THE ABI's, NOT AN OMISSION.
 * `opcode_table.yaml`'s compare rows carry `variants: [fwd_qq, fwd_hl, inv_qq,
 * inv_hl, unc_qq, unc_hl]` and its own note says "Controlled axis is
 * BINARY-ops-only — compares get no controlled grid (nesting combines flags via
 * cqrt_toffoli)". So a `cq_template_fcmp_*_controlled` is a phantom symbol, and
 * a case driving one would not compile. The §9 region IS exercised on this
 * surface, through `cqrt_copy_f64_controlled` in
 * tests/test_runtime_rail_f64.inc.
 */

/* The fourteen predicates in `opcode_table.yaml`'s OWN order, which is
 * `cq_shim_fpred`'s and is NOT LLVM's (src/kernels/fcmp.h is OEQ, OGT, OGE,
 * OLT, … while this is OEQ, UNE, OLT, OGT, …). Only the first row coincides, so
 * the X-macro is the one place the order is written and every table below is
 * generated from it — a hand-kept second list is how thirteen of fourteen rows
 * go silently wrong. */
#define CQ_FCMP_PREDS(X)                                                      \
    X(oeq, OEQ) X(une, UNE) X(olt, OLT) X(ogt, OGT) X(ole, OLE) X(oge, OGE)   \
    X(one, ONE) X(ord, ORD) X(uno, UNO) X(ueq, UEQ) X(ugt, UGT) X(uge, UGE)   \
    X(ult, ULT) X(ule, ULE)

/* M28's generated wrappers, declared here rather than in a header: they are
 * `cq_template_*` symbols and the only compiler-checkable statement of their
 * shape is tests/abi/cq_templates_abi.txt, which test_gen_shim.py diffs. A
 * wrong prototype here is a LINK error, not a silent mismatch, because C has no
 * name mangling and these are the real definitions. */
#define CQ_FCMP_DECL(lc, UC)                                                  \
    int32_t cq_template_fcmp_##lc##_f64(int32_t, int32_t);                    \
    int32_t cq_template_fcmp_##lc##_f64_hl(int32_t, double);                  \
    void cq_template_fcmp_##lc##_f64_unc(int32_t, int32_t, int32_t);          \
    void cq_template_fcmp_##lc##_f64_hl_unc(int32_t, int32_t, double);        \
    int32_t cq_template_fcmp_##lc##_f64_inv(int32_t, int32_t);
CQ_FCMP_PREDS(CQ_FCMP_DECL)

typedef struct {
    const char *name;
    cq_shim_fpred pred;
    int32_t (*fwd)(int32_t, int32_t);
    int32_t (*fwd_hl)(int32_t, double);
    void    (*unc)(int32_t, int32_t, int32_t);
    void    (*unc_hl)(int32_t, int32_t, double);
    int32_t (*inv)(int32_t, int32_t);
} fcmp_row;

#define CQ_FCMP_ROW(lc, UC) {                                                 \
    "fcmp_" #lc, CQ_SHIM_FPRED_##UC,                                          \
    cq_template_fcmp_##lc##_f64,    cq_template_fcmp_##lc##_f64_hl,           \
    cq_template_fcmp_##lc##_f64_unc, cq_template_fcmp_##lc##_f64_hl_unc,      \
    cq_template_fcmp_##lc##_f64_inv },

static const fcmp_row FCMP_ROWS[] = { CQ_FCMP_PREDS(CQ_FCMP_ROW) };

#define FCMP_N_ROWS (sizeof FCMP_ROWS / sizeof FCMP_ROWS[0])

/* THE HOST ORACLE: LLVM's fourteen rows, spelled in C. `un` is `isunordered`
 * without <math.h> — a NaN is the only value that differs from itself. Every
 * row is one expression and none of them shares a line with the module under
 * test. */
static int host_fcmp(cq_shim_fpred p, double a, double b)
{
    const int un = (a != a) || (b != b);

    switch (p) {
    case CQ_SHIM_FPRED_OEQ: return a == b;
    case CQ_SHIM_FPRED_UNE: return a != b;
    case CQ_SHIM_FPRED_OLT: return a <  b;
    case CQ_SHIM_FPRED_OGT: return a >  b;
    case CQ_SHIM_FPRED_OLE: return a <= b;
    case CQ_SHIM_FPRED_OGE: return a >= b;
    case CQ_SHIM_FPRED_ONE: return (a < b) || (a > b);
    case CQ_SHIM_FPRED_ORD: return !un;
    case CQ_SHIM_FPRED_UNO: return un;
    case CQ_SHIM_FPRED_UEQ: return un || a == b;
    case CQ_SHIM_FPRED_UGT: return un || a >  b;
    case CQ_SHIM_FPRED_UGE: return un || a >= b;
    case CQ_SHIM_FPRED_ULT: return un || a <  b;
    case CQ_SHIM_FPRED_ULE: return un || a <= b;
    }
    /* NO `default:` LABEL, deliberately — `bd fna`'s rule: an enum that grows
     * without its row must break the BUILD, and a `default:` is what stops
     * -Wswitch from saying so. The unreachable return is what C11 requires. */
    return 0;
}

/* The operand pairs, as PATTERNS. Nine rows, and each is here because some pair
 * of the fourteen predicates disagrees about it and about nothing cheaper:
 * both orders of an ordered pair (the four swap rows), an equal pair, the ±0
 * pair (`oeq` is TRUE and the patterns differ — a comparator that compared bit
 * patterns would be green everywhere else), a NaN in each operand position and
 * in both (the nine unordered rows), and the two infinities. */
static const uint64_t FCMP_PAIRS[][2] = {
    { CQ_F64_ONE,          0x4000000000000000ull },   /* 1.0  <  2.0        */
    { 0x4000000000000000ull, CQ_F64_ONE          },   /* 2.0  >  1.0        */
    { CQ_F64_ONE,          CQ_F64_ONE            },   /* equal              */
    { CQ_F64_POS_ZERO,     CQ_F64_NEG_ZERO       },   /* equal, bits differ */
    { CQ_F64_QNAN_A,       CQ_F64_ONE            },   /* unordered, lhs     */
    { CQ_F64_ONE,          CQ_F64_QNAN_A         },   /* unordered, rhs     */
    { CQ_F64_DEFAULT_NAN,  CQ_F64_SNAN           },   /* unordered, both    */
    { CQ_F64_POS_INF,      CQ_F64_POS_INF        },   /* equal infinities   */
    { CQ_F64_NEG_INF,      CQ_F64_POS_INF        }    /* ordered infinities */
};

#define FCMP_N_PAIRS (sizeof FCMP_PAIRS / sizeof FCMP_PAIRS[0])

static double fcmp_f64_of(uint64_t u)
{
    double d;
    memcpy(&d, &u, sizeof d);
    return d;
}

/* THE ALL-CLASSICAL ROW IS L5 AND IS THE DISPATCH ORACLE AT THE SAME TIME.
 * 126 calls, ZERO gates and ZERO qubits — PRD-v2 §3.1 item 3's claim that an
 * fp rail keeps I4 and that a fully classical fp program costs nothing — and
 * every one of the 126 results compared against the host.
 *
 * THIS IS THE SOLE DETECTOR FOR A WRONG DISPATCH ROW, and there are two kinds:
 * `cq_tpl_fcmp_kernel`'s table written by INDEX rather than by name (thirteen of
 * fourteen rows wrong, because the yaml's order is not LLVM's), and a generated
 * body carrying the integer predicate enum. Neither moves a gate count, a
 * qubit, a signature or a symbol name. */
CQ_TEST(every_fcmp_f64_wrapper_agrees_with_the_host_on_two_constant_rails)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);

    for (size_t r = 0; r < FCMP_N_ROWS; r++) {
        for (size_t p = 0; p < FCMP_N_PAIRS; p++) {
            const double a = fcmp_f64_of(FCMP_PAIRS[p][0]);
            const double b = fcmp_f64_of(FCMP_PAIRS[p][1]);
            const int32_t ha = cqrt_alloc_f64(a);
            const int32_t hb = cqrt_alloc_f64(b);
            const int32_t out = FCMP_ROWS[r].fwd(ha, hb);
            const uint64_t got = cq_pc_value(ctx, out);
            const uint64_t want = (uint64_t)host_fcmp(FCMP_ROWS[r].pred, a, b);

            if (cq_reg_width(&ctx->regs, out) != 1u)
                cq_h_fail(__FILE__, __LINE__, "%s: result rail is %u bits, not 1",
                          FCMP_ROWS[r].name, cq_reg_width(&ctx->regs, out));
            if (got != want)
                cq_h_fail(__FILE__, __LINE__,
                          "%s(%016llx, %016llx) = %llu, host says %llu",
                          FCMP_ROWS[r].name,
                          (unsigned long long)FCMP_PAIRS[p][0],
                          (unsigned long long)FCMP_PAIRS[p][1],
                          (unsigned long long)got, (unsigned long long)want);
        }
    }

    /* L5 at an fp width: 126 compares of 64-bit operands, nothing emitted and
     * nothing allocated. A kernel that entered the sandwich here would cost
     * thousands of gates a call. */
    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    cq_reg_audit(ctx);

    close_with(&m);
}

/* THE `_hl` SHAPE CARRIES A BIT PATTERN AND NOT A NUMBER, which is the one
 * thing about an fp wrapper that has no integer analogue. `CQ_SHIM_LO(3.5)` is
 * `(uint64_t)(unsigned __int128)3.5` — the integer 3 — and it compiles clean,
 * is silent under -Wconversion because the cast is explicit, links, and emits
 * the identical gate count. This case drives the GENERATED body, so it sees the
 * macro the generator actually chose rather than the one cq_shim.h defines.
 *
 * 3.5 IS THE DISCRIMINATOR AND 1.0 WOULD NOT BE: `(uint64_t)1.0` is 1, and
 * `fcmp_olt(x, 1)` against `fcmp_olt(x, 1.0)` disagree on almost every operand
 * — but the pattern for 1.0 is 0x3FF0…, so the two READINGS differ there too.
 * What 3.5 adds is that the converted value is not even the same integer as any
 * plausible pattern, and that the fractional part is exactly where a truncating
 * conversion loses the information. */
CQ_TEST(the_fcmp_hl_shape_carries_the_ieee_pattern_and_not_a_converted_value)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    static const double LITS[] = { 3.5, 0.5, -0.5, 1.0, 0.0 };

    for (size_t r = 0; r < FCMP_N_ROWS; r++) {
        for (size_t i = 0; i < sizeof LITS / sizeof LITS[0]; i++) {
            const double a = 3.0;
            const int32_t ha  = cqrt_alloc_f64(a);
            const int32_t out = FCMP_ROWS[r].fwd_hl(ha, LITS[i]);
            const uint64_t want = (uint64_t)host_fcmp(FCMP_ROWS[r].pred, a, LITS[i]);

            if (cq_pc_value(ctx, out) != want)
                cq_h_fail(__FILE__, __LINE__,
                          "%s_hl(3.0, %g) = %llu, host says %llu",
                          FCMP_ROWS[r].name, LITS[i],
                          (unsigned long long)cq_pc_value(ctx, out),
                          (unsigned long long)want);
        }
    }

    /* And the literal really did arrive as a pattern: a `cqrt_alloc_f64` of the
     * same double and the `_hl` literal must compare EQUAL under `oeq`, which
     * they cannot if one of them was truncated to 3. */
    const int32_t h35 = cqrt_alloc_f64(3.5);
    CHECK_EQ(cq_pc_value(ctx, cq_template_fcmp_oeq_f64_hl(h35, 3.5)), 1);
    CHECK_EQ(cq_pc_value(ctx, cq_template_fcmp_oeq_f64_hl(h35, 3.0)), 0);
    CHECK_EQ(cq_mock_count(&m), 0);

    close_with(&m);
}

/* TWO QUANTUM RAILS: a real circuit, the right flag, a ONE-BIT result rail, and
 * the pool back where it started after the `_unc` and the free.
 *
 * THE RESULT WIDTH IS THE HALF THE ARITY-2 SIGNATURE ACTIVELY SUGGESTS WRONG.
 * `tpl_binary` mints `dst` at `wout`, and `fcmp_req` is the only thing that sets
 * it to 1; a request that left `wout` at the operand width would still compare
 * equal on the low lane and would leak 63 qubits per compare.
 *
 * ONE PREDICATE PER OPERAND PAIR RATHER THAN THE CROSS PRODUCT, because every
 * case here runs the whole K18 program twice through a recording sink. The
 * dispatch claim is the all-classical sweep's; what this case adds is that the
 * QUANTUM path reaches the same answer and leaves nothing behind. */
CQ_TEST(fcmp_f64_on_two_quantum_rails_gives_the_right_flag_and_one_bit)
{
    for (size_t r = 0; r < FCMP_N_ROWS; r++) {
        /* A FRESH CONTEXT PER PREDICATE, because `cq_pc_live_is_exactly` is a
         * SET over every live rail and these rails are deliberately never freed
         * (see below). One context for the whole sweep would make the list grow
         * by two per iteration, and the tempting fix — dropping the set check
         * for a count — is the masking this project has already measured once
         * (`bd remember l2-was-masked-by-l3s-count`). */
        cq_mock m; cq_sink s;
        cq_ctx *ctx = open_with(&m, &s);
        const size_t p = r % FCMP_N_PAIRS;
        const double a = fcmp_f64_of(FCMP_PAIRS[p][0]);
        const double b = fcmp_f64_of(FCMP_PAIRS[p][1]);
        const int32_t ha  = qreg(ctx, 64u, FCMP_PAIRS[p][0]);
        const int32_t hb  = qreg(ctx, 64u, FCMP_PAIRS[p][1]);
        const int32_t out = FCMP_ROWS[r].fwd(ha, hb);
        const uint64_t want = (uint64_t)host_fcmp(FCMP_ROWS[r].pred, a, b);

        CHECK_EQ(cq_reg_width(&ctx->regs, out), 1);
        CHECK_EQ(cq_reg_owned_qubits(&ctx->regs, out), 1);
        if (cq_pc_value(ctx, out) != want)
            cq_h_fail(__FILE__, __LINE__, "%s on wires = %llu, host says %llu",
                      FCMP_ROWS[r].name,
                      (unsigned long long)cq_pc_value(ctx, out),
                      (unsigned long long)want);

        /* The operands are UNCHANGED — Rule 7's second clause, at the handle
         * boundary where a missing D7b copy or a kernel writing a source would
         * show up. */
        CHECK(cq_pc_value(ctx, ha) == FCMP_PAIRS[p][0]);
        CHECK(cq_pc_value(ctx, hb) == FCMP_PAIRS[p][1]);
        CHECK(cq_pc_live_is_exactly(ctx, (const int32_t[]){ ha, hb, out }, 3u));
        cq_reg_audit(ctx);

        /* NOTHING IS FREED HERE AND THAT IS NOT AN OVERSIGHT. These rails hold
         * a NON-ZERO value on real wires, so `cqrt_free` on any of them is a
         * CONVICTION — D15's proven-dirty row — and it STRANDS, loudly and
         * correctly. A rail `_unc`'d and never freed stays allocated for good,
         * and PRD §10 calls that the intended Rule-6 safe leak rather than a
         * bug. Freeing them here would make the next case's strand assertion
         * red for a reason that has nothing to do with the certificate; that is
         * measured, not anticipated — the first draft of this file did free
         * them and `cq_qubits_stranded` came back 11, which is exactly the
         * number of set bits in 1.0 and 2.0. */
        close_with(&m);
    }
}

/* THE `_unc` ZEROES THE OUT RAIL AND THE FREE REACHES THE POOL — the L3 claim
 * at the handle boundary, and the one that D15's certificate has to discharge.
 *
 * THE STRAND COUNT IS THE INSTRUMENT AND IT IS THE POINT OF THIS CASE. A
 * forward and its `_unc` pair only if the record's effect rows describe them as
 * the same operation on the same rail: `CQ_ROP_TPL_FWD` writing slot 0, reading
 * slots 1 and 2, with `CQ_ROP_TPL_UNC` as its declared twin, and the two calls
 * carrying the same TAG. Get any of that wrong — drop the write, swap the twin,
 * give fcmp icmp's tag space — and the reduction cannot pair them, the rail is
 * UNPROVEN, and its qubit STRANDS. Stranding is loud but it is not an abort, so
 * a case that asserted only "it did not crash" would be green on every one of
 * those mutants. `cq_qubits_stranded` and `cq_reg_strand_reports` are what see
 * it, and they must both be ZERO.
 *
 * AND THE SOURCES ARE QUANTUM, which is what makes the free NON-TRIVIAL: on
 * all-classical operands the result rail owns no qubit (I4) and nothing reaches
 * the proof at all, so the case would pass with the certificate deleted. */
CQ_TEST(an_fcmp_f64_unc_zeroes_its_out_rail_and_the_free_strands_nothing)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    for (size_t r = 0; r < FCMP_N_ROWS; r++) {
        const size_t p = r % FCMP_N_PAIRS;
        const int32_t ha = qreg(ctx, 64u, FCMP_PAIRS[p][0]);
        const int32_t hb = qreg(ctx, 64u, FCMP_PAIRS[p][1]);
        const cq_pc_snap before = cq_pc_take(ctx);

        const int32_t out = FCMP_ROWS[r].fwd(ha, hb);
        uint32_t idx[1];
        CHECK_EQ(cq_pc_indices(ctx, out, idx, 1u), 1);

        FCMP_ROWS[r].unc(out, ha, hb);
        CHECK_EQ(cq_pc_value(ctx, out), 0);          /* f ^ f = 0           */

        cqrt_free(out);
        if (cq_qubits_stranded(&ctx->pool) != 0u)
            cq_h_fail(__FILE__, __LINE__,
                      "%s: the free stranded %u qubit(s) — the certificate did "
                      "not pair the forward with its _unc",
                      FCMP_ROWS[r].name, cq_qubits_stranded(&ctx->pool));
        CHECK_EQ(cq_reg_strand_reports(ctx), 0);

        /* `live` restored and the result's index back on the free list — the
         * per-index form, which also catches a free that released the wrong
         * one. Never `minted` or the free-list LENGTH: both are monotone. */
        CHECK(cq_pc_same(before, cq_pc_take(ctx)));
        CHECK(cq_pc_indices_are_free(ctx, idx, 1u));
        cq_reg_audit(ctx);

        /* The OPERANDS are deliberately not freed — they hold a non-zero value
         * on real wires, so their free is D15's proven-dirty row and strands by
         * design. See the case above. The rail this case is about is `out`. */
    }

    close_with(&m);
}

/* THE SAME ROUND TRIP ON A ROTATION-POISONED SOURCE, WHICH IS THE ONLY VERSION
 * OF IT THE CERTIFICATE ACTUALLY ANSWERS — and the case above is masked without
 * this one, which was MEASURED rather than anticipated.
 *
 * `cq_shim_free_proof` is the certificate AND the shadow, and the shadow is
 * EXACT on the rotation-free surface. `fcmp` emits no rotation, so on ordinary
 * operands the shadow proves `out` clean by itself and every defect in D15's
 * effect table is invisible: measured, dropping `CQ_ROP_TPL_FWD`'s `writes:
 * R(0)` left `an_fcmp_f64_unc_zeroes_its_out_rail_and_the_free_strands_nothing`
 * GREEN in Release, killed only by tests/test_shim_cert.c one layer down. That
 * is the "a cheaper assertion hides an expensive one" trap in its exact recorded
 * shape, and the discriminating fixture has to sit in the gap: a general `Ry`
 * off §7's lattice is the ONE row that poisons (PRD §15 D12), so after it the
 * shadow can prove nothing about `out` and the free is the certificate's alone.
 *
 * BOTH SHAPES, because the `_hl` lane carries the literal into D15's twin
 * identity through `r.lo ^ r.hi` and the `qq` lane does not — a tag or `imm`
 * defect can therefore kill one and spare the other. */
CQ_TEST(an_fcmp_f64_unc_on_a_poisoned_source_is_discharged_by_the_certificate)
{
    for (size_t r = 0; r < FCMP_N_ROWS; r++) {
        cq_mock m; cq_sink s;
        cq_ctx *ctx = open_with(&m, &s);
        const size_t p = r % FCMP_N_PAIRS;
        const int32_t ha = qreg(ctx, 64u, FCMP_PAIRS[p][0]);
        const int32_t hb = qreg(ctx, 64u, FCMP_PAIRS[p][1]);

        /* §7's GENERAL row on the f64 surface: every lane rotated, every lane
         * poisoned. It is `cqrt_ry_f64` rather than `cq_rotate_ry` on purpose —
         * the poison has to arrive through the same frozen symbol a CQ_lang
         * fixture would call, which is also what puts a `CQ_ROP_RY` in the
         * record for the engine's operand-stability check to see. */
        cqrt_ry_f64(ha, 0.5);
        cqrt_ry_f64(hb, 0.5);

        const uint32_t strand_before = cq_qubits_stranded(&ctx->pool);

        /* The SHADOW really is helpless here — the negative control that makes
         * the rest of this case a claim about the certificate rather than about
         * nothing. Without it a change that stopped poisoning would leave every
         * assertion below green for the wrong reason. */
        const int32_t probe = FCMP_ROWS[r].fwd(ha, hb);
        CHECK_EQ(cq_reg_disposition(ctx, probe, cq_pc_zero_proof_rotation_free),
                 CQ_PROOF_UNPROVEN);

        FCMP_ROWS[r].unc(probe, ha, hb);
        cqrt_free(probe);
        if (cq_qubits_stranded(&ctx->pool) - strand_before != 0u)
            cq_h_fail(__FILE__, __LINE__,
                      "%s (qq): the certificate did not discharge a poisoned "
                      "round trip — %u qubit(s) stranded", FCMP_ROWS[r].name,
                      cq_qubits_stranded(&ctx->pool) - strand_before);
        CHECK_EQ(cq_reg_strand_reports(ctx), 0u);

        const int32_t h2 = FCMP_ROWS[r].fwd_hl(ha, 2.0);
        FCMP_ROWS[r].unc_hl(h2, ha, 2.0);
        cqrt_free(h2);
        if (cq_qubits_stranded(&ctx->pool) - strand_before != 0u)
            cq_h_fail(__FILE__, __LINE__,
                      "%s (hl): the certificate did not discharge a poisoned "
                      "round trip — %u qubit(s) stranded", FCMP_ROWS[r].name,
                      cq_qubits_stranded(&ctx->pool) - strand_before);
        CHECK_EQ(cq_reg_strand_reports(ctx), 0u);

        cq_reg_audit(ctx);
        close_with(&m);
    }
}

/* AND THE NEGATIVE HALF: AN `fcmp` `_unc` MUST *NOT* PAIR WITH AN `icmp`
 * FORWARD. This is the case that makes the fp family's own TAG SPACE a tested
 * fact rather than a comment, and it was written because the obvious mutant —
 * `fcmp_req` using `icmp`'s `0x40000000` — SURVIVED the whole selection in
 * Release. Nothing else in the tree produces the shape that separates them.
 *
 * THE SHAPE IS EXACT AND EVERY PART OF IT IS LOAD-BEARING. `adjoint_matches`
 * pairs two records when the earlier one's effect row declares the later one's
 * opcode as its twin AND the tags are equal AND all four handle slots match AND
 * the §9 context and the `cqrt_x` parities match. `cq_shim_icmp_qq(EQ, 64, …)`
 * and `cq_shim_fcmp_qq_unc(OEQ, 64, …)` agree on every one of those — both are
 * TPL_FWD/TPL_UNC, both are at width 64, and `CQ_SHIM_PRED_EQ` and
 * `CQ_SHIM_FPRED_OEQ` are both index 0 — so only the 0x40000000 vs 0xC0000000
 * prefix keeps them apart.
 *
 * THE OPERANDS MUST BE POISONED OR THE SHADOW ANSWERS FIRST. `cq_shim_free_proof`
 * is the certificate AND the shadow with DIRTY dominating; on the rotation-free
 * surface the shadow is exact, sees the rail is non-zero, and strands whatever
 * the certificate says — so the mutant survives. A general `Ry` makes the shadow
 * UNPROVEN, and then CLEAN-from-a-wrong-pair beats UNPROVEN and RELEASES a rail
 * nothing uncomputed. That is the only unforgivable bug, arriving through a
 * four-hex-digit constant.
 *
 * ±0 IS WHY THE TWO PREDICATES DISAGREE AT ALL: `+0.0` and `-0.0` are different
 * bit patterns, so `icmp eq` is 0, and they are numerically equal, so
 * `fcmp oeq` is 1. An equal-pattern fixture would leave the rail at zero and the
 * case would pass under the mutant for the wrong reason. */
CQ_TEST(an_fcmp_unc_does_not_pair_with_an_icmp_forward_on_the_same_handles)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    const int32_t a = qreg(ctx, 64u, CQ_F64_POS_ZERO);
    const int32_t b = qreg(ctx, 64u, CQ_F64_NEG_ZERO);

    cqrt_ry_f64(a, 0.5);                  /* §7's general row: poison       */
    cqrt_ry_f64(b, 0.5);

    const uint32_t strand_before = cq_qubits_stranded(&ctx->pool);
    const int32_t out = cq_shim_icmp_qq(CQ_SHIM_PRED_EQ, 64, a, b);

    /* The `_unc` of a DIFFERENT operation on the same handles. It is a legal
     * call — nothing refuses it, and nothing should: the ABI permits any
     * sequence and the certificate's job is to decline to discharge it. */
    cq_shim_fcmp_qq_unc(CQ_SHIM_FPRED_OEQ, 64, out, a, b);

    cqrt_free(out);

    /* STRANDED, LOUDLY. One qubit never released, and the one-shot report fired
     * — which is the D15 §3 disposition working, not a failure. Releasing here
     * would be the silent state collapse Rule 6 exists to prevent. */
    CHECK_EQ(cq_qubits_stranded(&ctx->pool) - strand_before, 1u);
    CHECK_EQ(cq_reg_strand_reports(ctx), 1u);

    cq_reg_audit(ctx);
    close_with(&m);
}

/* THE `_hl` `_unc` IS ITS OWN PATH AND MUST PAIR TOO. It reaches `cq_tpl_binary`
 * with `b_h == CQ_REG_NONE` and the literal in `r.lo`, and D15's twin match
 * folds `r.lo ^ r.hi` into the record's `imm` — so a forward and an `_unc` at
 * DIFFERENT literals are correctly NOT a pair, and at the same literal they
 * are. Driving only the `qq` shape would leave that lane untested. */
CQ_TEST(an_fcmp_f64_hl_unc_pairs_with_its_forward_at_the_same_literal)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);

    for (size_t r = 0; r < FCMP_N_ROWS; r++) {
        const int32_t ha = qreg(ctx, 64u, CQ_F64_ONE);
        const cq_pc_snap before = cq_pc_take(ctx);
        const int32_t out = FCMP_ROWS[r].fwd_hl(ha, 2.0);

        FCMP_ROWS[r].unc_hl(out, ha, 2.0);
        CHECK_EQ(cq_pc_value(ctx, out), 0);

        cqrt_free(out);
        CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0);
        CHECK_EQ(cq_reg_strand_reports(ctx), 0);
        CHECK(cq_pc_same(before, cq_pc_take(ctx)));
        cq_reg_audit(ctx);
    }

    close_with(&m);
}

/* ALL 28 `_inv` BODIES ABORT, AND THE REASON IS D14's RATHER THAN "fp is v2".
 * That distinction is a FALSEHOOD PRINTED AT RUNTIME if it goes the other way:
 * an `fcmp` is non-injective at every width and in every version — two 64-bit
 * operands to one bit — so `cq_template_fcmp_oeq_f64_inv` saying "fp is v2"
 * would tell a caller to wait for a release that will never define it. The
 * `_inv` bucket is decided by `opcode_table.yaml`'s row (the variant exists) and
 * by D14 (no uniform definition of the data family is possible in ANY version).
 *
 * IT FORKS RATHER THAN BEING A DEATH CASE, for tests/test_runtime_v2.c's
 * reason: a death case can say only "it aborted", and the interesting failure
 * here is an abort that names the wrong SYMBOL or carries the wrong BUCKET.
 *
 * THE THUNK TAKES AN IGNORED-BY-VALUE INDEX RATHER THAN A CAST FUNCTION
 * POINTER: `childcap.h`'s one callback type is `void (*)(void *)`, and calling a
 * nullary function through it is undefined behaviour that THIS build catches
 * (Debug's UBSan includes `function` for C since clang 17). */
static void fcmp_call_inv(void *p)
{
    (void)FCMP_ROWS[*(const size_t *)p].inv(0, 0);
}

CQ_TEST(every_fcmp_f64_inv_aborts_naming_itself_and_d14s_reason)
{
    char msg[512];

    for (size_t r = 0; r < FCMP_N_ROWS; r++) {
        char want[256];     /* D14's reason is 82 bytes; 128 TRUNCATED it */
        size_t i = r;

        const int aborted = cq_child_capture(CQ_CHILD_STDERR, fcmp_call_inv,
                                             &i, NULL, 0u, msg, sizeof msg);
        CHECK(aborted);

        (void)snprintf(want, sizeof want,
                       "cqops: cq_template_%s_f64_inv not implemented "
                       "(_inv is f-inverse, not f; the data family has no "
                       "uniform definition (PRD 15 D14))\n",
                       FCMP_ROWS[r].name);
        if (strcmp(msg, want) != 0)
            cq_h_fail(__FILE__, __LINE__, "row %zu said `%s`, wanted `%s`",
                      r, msg, want);
    }
}
