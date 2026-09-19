/* tests/test_template_fconv.inc — the CROSS-DOMAIN cast families at f64:
 * `fptosi`, `fptoui`, `sitofp`, `uitofp`, over `cq_shim_fcast` /
 * `cq_shim_fcast_unc`.
 *
 * THE NARROW ROWS ARE A COMPOSITION AT THE SHIM, ON UPSTREAM'S OWN SHAPE.
 * `instructions.jl:7649-7658` emits the narrowing as a SECOND IR instruction
 * and `:7677-7679` the widening `sext` as a separate cast, so the ABI rows
 * `fptosi f64 -> i{1,8,16,32}` are M37's kernel at `T = 64` followed by
 * `cq_kernel_trunc`, and `sitofp i{8,16,32} -> f64` are `cq_kernel_sext`
 * followed by the kernel at `F = 64`. `uitofp`'s four narrow rows take `F`
 * directly, because there the `zext` is WIRING inside M37 (PRD-v2 §7.3) and
 * costs nothing.
 *
 * THE WORKSPACE IS D7b's TEMPORARY IN A SECOND DRESS: minted through
 * `cq_reg_alloc_zero`, RECORDED at birth so the certificate has a history for
 * it, written by a FWD/UNC twin pair so the reduction can cancel them, and
 * freed through `cq_shim_free_proof` — the same door every other rail goes
 * through, which is what makes converting strands to releases convert this one
 * too. Its `_unc` half is what returns it to |0>; without it every narrow
 * conversion would leak 64 qubits per call.
 *
 * AND A NAIVE ALTERNATIVE IS WHAT THE VALUE CASES ARE AIMED AT: calling the
 * kernel at `(64, T)` and letting the copy-out drop the top lanes is NOT the
 * same function as `trunc(kernel(a))` in the saturating rows, and a `zext`
 * where the composition wants a `sext` is bead 9ve.34's defect with the sign
 * moved to the other side.
 */

/* The SEVENTEEN shipped pairs. `uitofp i64 -> f64` is deliberately absent —
 * bead 9ve.34 — and is driven by name in its own case below. */
#define CQ_FCONV_PAIRS(X)                                                     \
    X(fptosi, f64, i8,  FPTOSI, 64,  8) X(fptosi, f64, i16, FPTOSI, 64, 16)   \
    X(fptosi, f64, i32, FPTOSI, 64, 32) X(fptosi, f64, i64, FPTOSI, 64, 64)   \
    X(fptoui, f64, i1,  FPTOUI, 64,  1) X(fptoui, f64, i8,  FPTOUI, 64,  8)   \
    X(fptoui, f64, i16, FPTOUI, 64, 16) X(fptoui, f64, i32, FPTOUI, 64, 32)   \
    X(fptoui, f64, i64, FPTOUI, 64, 64)                                       \
    X(sitofp, i8,  f64, SITOFP,  8, 64) X(sitofp, i16, f64, SITOFP, 16, 64)   \
    X(sitofp, i32, f64, SITOFP, 32, 64) X(sitofp, i64, f64, SITOFP, 64, 64)   \
    X(uitofp, i1,  f64, UITOFP,  1, 64) X(uitofp, i8,  f64, UITOFP,  8, 64)   \
    X(uitofp, i16, f64, UITOFP, 16, 64) X(uitofp, i32, f64, UITOFP, 32, 64)

#define CQ_FCONV_DECL(op, f, t, K, FB, TB)                                    \
    int32_t cq_template_##op##_##f##_to_##t(int32_t);                         \
    void    cq_template_##op##_##f##_to_##t##_unc(int32_t, int32_t);          \
    int32_t cq_template_##op##_##f##_to_##t##_inv(int32_t);
CQ_FCONV_PAIRS(CQ_FCONV_DECL)

int32_t cq_template_uitofp_i64_to_f64(int32_t);
void    cq_template_uitofp_i64_to_f64_unc(int32_t, int32_t);
int32_t cq_template_uitofp_i64_to_f64_inv(int32_t);

typedef struct {
    const char *sym;
    cq_shim_fcast_kind kind;
    int f, t;
    int32_t (*fwd)(int32_t);
    void    (*unc)(int32_t, int32_t);
} fconv_row;

#define CQ_FCONV_ROW(op, f, t, K, FB, TB)                                     \
    { #op "_" #f "_to_" #t, CQ_SHIM_FCAST_##K, FB, TB,                        \
      cq_template_##op##_##f##_to_##t, cq_template_##op##_##f##_to_##t##_unc },
static const fconv_row FCONV_ROWS[] = { CQ_FCONV_PAIRS(CQ_FCONV_ROW) };

#define FCONV_N (sizeof FCONV_ROWS / sizeof FCONV_ROWS[0])

static uint64_t wmask(int W)
{
    return (W >= 64) ? ~(uint64_t)0 : ((((uint64_t)1) << W) - 1u);
}

/* A SIGN EXTENSION OVER `uint64_t`, which is what `cq_kernel_sext` does to the
 * workspace. Written here rather than borrowed from the module under test:
 * an oracle sharing a constant with the code is blind to exactly what that
 * constant gets wrong. */
static uint64_t sext_to_64(uint64_t v, int F)
{
    uint64_t m;

    if (F >= 64) return v;
    v &= wmask(F);
    m = ((uint64_t)1) << (F - 1);
    return (v ^ m) - m;
}

/* The composition, stated in C over `uint64_t` — never a host cast, which for
 * every NaN, every infinity and every out-of-range operand is UNDEFINED
 * BEHAVIOUR (C11 §6.3.1.4) and which a compiler may fold to anything. */
static uint64_t fconv_expect(cq_shim_fcast_kind k, int F, int T, uint64_t a)
{
    switch (k) {
    case CQ_SHIM_FCAST_FPTOSI: return cq_fptosi_eval(a) & wmask(T);
    case CQ_SHIM_FCAST_FPTOUI: return cq_fptoui_eval(a) & wmask(T);
    case CQ_SHIM_FCAST_SITOFP: return cq_sitofp_eval(sext_to_64(a, F));
    case CQ_SHIM_FCAST_UITOFP: return cq_uitofp_eval(a, F);
    }
    /* NO `default:` LABEL — `bd fna`'s rule: an enum that grows without its
     * row must break the BUILD, and a `default:` is what stops -Wswitch saying
     * so. The unreachable return is what C11 requires. */
    return 0u;
}

/* fp SOURCES: §7.12's named cells plus the two rows that only a CONVERSION can
 * see — an operand above `INT64_MAX` (x86 saturation, PRD-v2 §7.5) and one
 * whose integer value overflows the NARROW target but not the 64-bit one, which
 * is where "trunc after the kernel" and "saturate to T" part company. */
static const uint64_t FCONV_SRC_FP[] = {
    CQ_F64_POS_ZERO, CQ_F64_NEG_ZERO, CQ_F64_ONE,
    0xBFF8000000000000ull,          /* -1.5                                 */
    0x4072C00000000000ull,          /*  300.0 — overflows i8, not i64       */
    CQ_F64_POS_INF,  CQ_F64_NEG_INF, CQ_F64_QNAN_A,
    0x43E0000000000000ull           /*  2^63 — the saturation row           */
};

/* INTEGER SOURCES, masked to each row's own F. 0x80.. and ~0 are the rows
 * where `sitofp` and `uitofp` must disagree, which is the whole of 9ve.34 on
 * the other side of the fence. */
static const uint64_t FCONV_SRC_INT[] = {
    0u, 1u, 2u, 0x7Fu, 0x80u, 0xFFFFu, 0x7FFFFFFFu, 0xFFFFFFFFu,
    0x8000000000000000ull, ~(uint64_t)0
};

static int fconv_src_is_fp(cq_shim_fcast_kind k)
{
    return k == CQ_SHIM_FCAST_FPTOSI || k == CQ_SHIM_FCAST_FPTOUI;
}

/* ALL SEVENTEEN PAIRS ON CONSTANT RAILS: the dispatch oracle, the result
 * WIDTH, and L5 at a cast whose circuit is 15k to 111k gates.
 *
 * L5 SURVIVES THE COMPOSITION, which is the non-obvious half: a narrow row
 * mints a 64-bit WORKSPACE even on the classical path, and by I4 that rail
 * owns zero qubits, so the whole thing is still zero gates and zero qubits. A
 * workspace minted with `cq_reg_alloc_zero` at a QUANTUM mask — the obvious
 * way to "make sure it is a real rail" — would cost 64 qubits per constant
 * conversion and this line is what says it does not.
 *
 * AND IT IS THE SOLE DETECTOR FOR A TRANSPOSED `cq_tpl_fcast_kernel` ROW.
 * `sitofp` where `uitofp` belongs is bead 9ve.34's defect exactly, emits the
 * same gate tuple (uitofp IS sitofp's row program), keeps the palindrome and
 * leaves scratch clean — and differs only on operands with bit F-1 set, which
 * is why 0x80 and ~0 are in the source list. */
CQ_TEST(every_shipped_fconv_pair_agrees_with_its_kernels_own_eval)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const cq_pc_snap before = cq_pc_take(ctx);

    CHECK_EQ((int)FCONV_N, 17);
    for (size_t r = 0; r < FCONV_N; r++) {
        const int fp_src = fconv_src_is_fp(FCONV_ROWS[r].kind);
        const size_t n = fp_src ? sizeof FCONV_SRC_FP / sizeof FCONV_SRC_FP[0]
                                : sizeof FCONV_SRC_INT / sizeof FCONV_SRC_INT[0];

        for (size_t i = 0; i < n; i++) {
            const uint64_t v = (fp_src ? FCONV_SRC_FP[i] : FCONV_SRC_INT[i])
                               & wmask(FCONV_ROWS[r].f);
            const int32_t a = fp_src
                ? cqrt_alloc_f64(f64_of(v))
                : cq_reg_alloc_const(&ctx->regs, (uint32_t)FCONV_ROWS[r].f, v, 0u);
            const int32_t out = FCONV_ROWS[r].fwd(a);
            const uint64_t got = cq_pc_value(ctx, out);
            const uint64_t want = fconv_expect(FCONV_ROWS[r].kind,
                                               FCONV_ROWS[r].f,
                                               FCONV_ROWS[r].t, v);

            if (cq_reg_width(&ctx->regs, out) != (uint32_t)FCONV_ROWS[r].t)
                cq_h_fail(__FILE__, __LINE__,
                          "%s: result rail is %u bits, the symbol names %d",
                          FCONV_ROWS[r].sym, cq_reg_width(&ctx->regs, out),
                          FCONV_ROWS[r].t);
            if (got != want)
                cq_h_fail(__FILE__, __LINE__,
                          "%s(%016llx) = %016llx, the composition says %016llx",
                          FCONV_ROWS[r].sym, (unsigned long long)v,
                          (unsigned long long)got, (unsigned long long)want);
        }
    }

    CHECK_EQ(cq_mock_count(&m), 0);
    CHECK(cq_pc_same(before, cq_pc_take(ctx)));
    cq_reg_audit(ctx);
    close_with(&m);
}

/* THE FOUR ROWS WHERE THE COMPOSITION IS NOT THE ONLY PLAUSIBLE ANSWER. Every
 * line here is a pair of readings that a width and a gate count cannot
 * separate, and the second reading is the one somebody writes first.
 *
 *   1. `fptosi f64 -> i8` of 300.0 is `300 & 0xFF` = 44, NOT a saturation to
 *      127. Upstream emits the narrowing as a separate `trunc` instruction, so
 *      the wrap is the specified answer and clamping would be a re-derivation.
 *   2. `sitofp i8 -> f64` of 0xFF is -1.0 and not 255.0 — the workspace is
 *      `sext`ed, and a `zext` there is bead 9ve.34's defect with the sign on
 *      the other side of the fence.
 *   3. `uitofp i8 -> f64` of 0xFF is 255.0 and not -1.0 — the same lane read
 *      the other way, which is what makes the pair a discriminator rather than
 *      two copies of one claim.
 *   4. `fptoui f64 -> i1` of 3.0 is 1: the target really is ONE BIT, and a
 *      result minted at the operand width would agree on the low lane and leak
 *      63 qubits per call. */
CQ_TEST(a_narrow_fconv_composes_the_kernel_with_the_integer_cast)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t h300 = cqrt_alloc_f64(f64_of(0x4072C00000000000ull));
    const int32_t h3   = cqrt_alloc_f64(f64_of(0x4008000000000000ull));
    const int32_t i8ff = cq_reg_alloc_const(&ctx->regs, 8u, 0xFFu, 0u);
    int32_t r;

    r = cq_template_fptosi_f64_to_i8(h300);
    CHECK_EQ(cq_reg_width(&ctx->regs, r), 8);
    CHECK_EQ(cq_pc_value(ctx, r), cq_fptosi_eval(0x4072C00000000000ull) & 0xFFu);
    CHECK_EQ(cq_pc_value(ctx, r), 44u);            /* 300 & 0xFF, not 127 */

    r = cq_template_sitofp_i8_to_f64(i8ff);
    CHECK_EQ(cq_pc_value(ctx, r), cq_sitofp_eval(~(uint64_t)0));
    CHECK_EQ(cq_pc_value(ctx, r), 0xBFF0000000000000ull);      /* -1.0 */

    r = cq_template_uitofp_i8_to_f64(i8ff);
    CHECK_EQ(cq_pc_value(ctx, r), cq_uitofp_eval(0xFFu, 8));
    CHECK_EQ(cq_pc_value(ctx, r), 0x406FE00000000000ull);      /* 255.0 */

    /* The two readings of the same eight lanes really do differ; without this
     * the two lines above could both be green against one wrong function. */
    CHECK(cq_sitofp_eval(~(uint64_t)0) != cq_uitofp_eval(0xFFu, 8));

    r = cq_template_fptoui_f64_to_i1(h3);
    CHECK_EQ(cq_reg_width(&ctx->regs, r), 1);
    CHECK_EQ(cq_pc_value(ctx, r), 1u);

    CHECK_EQ(cq_mock_count(&m), 0);
    close_with(&m);
}

/* THE HANDLE ARITHMETIC D21's PREDICTION RESTS ON, asserted where it can
 * actually be RUN.
 *
 * `cq_tpl_unary` opens its bracket BEFORE the workspace is minted — a narrow
 * row runs its inner stage into the workspace before `dst` exists at all, and
 * a bracket opened after the mint would leave those gates outside every
 * bracket, which is a FATAL parse error for the viewer rather than a cosmetic
 * gap. So the `out=` token names a handle that does not exist yet, computed as
 * `cq_reg_count + (there is a workspace)`. tests/test_shim_trace_fp.inc pins
 * the emitted token against the returned handle, but that suite is opt-in
 * behind `-DCQOPS_QEC_DIR=` and does not run on a box without the QEC library.
 *
 * WHAT RUNS EVERYWHERE IS THE ARITHMETIC ITSELF, and the two rows differ in
 * exactly the term under test: a COMPOSED call must return `count + 1` and a
 * FLAT one `count`. A sequence that added the term unconditionally, or never,
 * is red in exactly one of these two lines. It is all-CONSTANT, so it costs
 * nothing and says nothing about wires — the handle counter is monotonic (D5)
 * whatever the operands are. */
CQ_TEST(a_composed_conversion_mints_its_workspace_before_its_result)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t a = cqrt_alloc_f64(f64_of(0x4072C00000000000ull));
    int32_t before, r;

    before = cq_reg_count(&ctx->regs);
    r = cq_template_fptosi_f64_to_i8(a);          /* NARROW: a workspace */
    CHECK_EQ((long long)r, (long long)(before + 1));

    before = cq_reg_count(&ctx->regs);
    r = cq_template_fptosi_f64_to_i64(a);         /* FLAT: none          */
    CHECK_EQ((long long)r, (long long)before);

    before = cq_reg_count(&ctx->regs);
    r = cq_template_sitofp_i8_to_f64(
            cq_reg_alloc_const(&ctx->regs, 8u, 0x12u, 0u));
    /* The source rail took `before`; the workspace `before + 1`; the result
     * `before + 2`. Spelled that way rather than as a bare `+ 2` because the
     * term under test is the workspace's ONE and the source's is the fixture's
     * own doing. */
    CHECK_EQ((long long)r, (long long)(before + 1 + 1));

    CHECK_EQ(cq_mock_count(&m), 0);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* THE COMPOSITION'S THREE RECORDS, READ OUT OF D15's STREAM — and this case
 * exists because a MUTANT SURVIVED without it.
 *
 * Dropping BOTH of the workspace's `urec` calls while leaving its gates in
 * place left the whole fp suite GREEN, in Release. The reason is the one
 * `shim/cq_template_impl.c` states for D7b's copy and that `bd remember
 * strict-window-off-by-one-at-the-mint` states for the mint: an UNRECORDED
 * write gives the certificate an EMPTY history, which reduces VACUOUSLY, reads
 * a zero birth value and answers CLEAN — releasing a rail it never examined.
 * Here the release happens to be correct, because the gates really do cancel;
 * combined with any defect that stops them cancelling it is the only
 * unforgivable bug. The strand counter cannot see it in either direction, so
 * the instrument has to be the RECORD itself.
 *
 * IT ALSO PINS THE OUTER RECORD'S SOURCE SLOT, which is the other mutant with
 * no other detector. `adjoint_matches` compares all four handle slots, and the
 * workspace is minted FRESH on every call (D5: handles are monotonic and never
 * reused) — so an outer record naming the workspace instead of `a` could never
 * match between a forward and its `_unc`, and every narrow conversion's result
 * would be UNPROVEN and strand. The poisoned case one below would catch that
 * one; this one catches it too and says WHICH SLOT is wrong.
 *
 * THE INNER TAG IS NOT WRITTEN DOWN, IT IS ASKED OF A FLAT CALL. The inner
 * stage IS the `f64 -> i64` row of the same kind, so its tag must be the tag a
 * flat `cq_template_fptosi_f64_to_i64` emits — an independent derivation
 * rather than a second transcription of `FCAST_TAG`.
 *
 * ALL-CONSTANT, so it costs no gates and no qubits: the record stream does not
 * depend on the operand mask. */
CQ_TEST(the_composed_conversion_records_three_calls_and_names_a_in_the_outer)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t a = cqrt_alloc_f64(f64_of(0x4072C00000000000ull));
    uint32_t p, flat_tag = 0u, n_inner = 0u, n_outer = 0u;
    uint32_t inner_tag = 0u, base;
    int32_t flat, ws, r;
    uint16_t inner_op[2];

    /* The flat row first: ONE record, and its tag is what the inner stage of
     * the narrow row must carry. */
    base = cq_rec_len();
    flat = cq_template_fptosi_f64_to_i64(a);
    for (p = base; p < cq_rec_len(); p++) {
        const cq_call_rec *c = cq_rec_at(p);
        if (c->op == (uint16_t)CQ_ROP_TPL_FWD && c->h[0] == flat) flat_tag = c->tag;
    }
    CHECK(flat_tag != 0u);

    base = cq_rec_len();
    r    = cq_template_fptosi_f64_to_i8(a);
    ws   = r - 1;                       /* the workspace; see the case above */

    for (p = base; p < cq_rec_len(); p++) {
        const cq_call_rec *c = cq_rec_at(p);

        if (c->op != (uint16_t)CQ_ROP_TPL_FWD && c->op != (uint16_t)CQ_ROP_TPL_UNC)
            continue;
        if (c->h[0] == ws) {
            if (n_inner < 2u) inner_op[n_inner] = c->op;
            if (n_inner == 0u) inner_tag = c->tag;
            /* Both halves must agree on the TAG and on every slot, or
             * `adjoint_matches` cannot pair them and the workspace strands. */
            CHECK_EQ(c->tag, inner_tag);
            CHECK_EQ((long long)c->h[1], (long long)a);
            CHECK_EQ((long long)c->h[2], (long long)CQ_REG_NONE);
            CHECK_EQ((long long)c->ctrl, (long long)CQ_REG_NONE);
            n_inner++;
        } else if (c->h[0] == r) {
            /* THE OUTER RECORD NAMES `a`, NEVER THE WORKSPACE. */
            CHECK_EQ((long long)c->h[1], (long long)a);
            CHECK(c->h[1] != ws);
            CHECK(c->tag != inner_tag);
            n_outer++;
        }
    }

    /* EXACTLY two writes to the workspace and EXACTLY one to the result —
     * a FORWARD then its declared twin, in that order. */
    CHECK_EQ(n_inner, 2u);
    CHECK_EQ(n_outer, 1u);
    CHECK_EQ((int)inner_op[0], (int)CQ_ROP_TPL_FWD);
    CHECK_EQ((int)inner_op[1], (int)CQ_ROP_TPL_UNC);
    /* ...and the inner stage really is the flat row of the same kind. */
    CHECK_EQ(inner_tag, flat_tag);

    CHECK_EQ(cq_mock_count(&m), 0);
    cq_reg_audit(ctx);
    close_with(&m);
}

/* THE WORKSPACE ON WIRES. Both directions, because they are different
 * compositions — a narrowing puts the workspace on the RESULT side of the
 * kernel and a widening on the OPERAND side — and only running both says the
 * `_unc` half of each is emitted.
 *
 * THE POOL CLAIM IS `live` AND THE PER-INDEX ONE, never `minted` or the
 * free-list length: both are monotone and cannot return. What it catches is a
 * workspace that was minted and never freed, which is 64 qubits per call and
 * which no value, no gate count and no palindrome can see.
 *
 * `fptosi f64 -> i8` AND `sitofp i8 -> f64` ARE THE TWO CHEAPEST NARROW ROWS
 * (15,364 and 19,800 gates); `fptoui`'s 110,948 buys nothing this does not,
 * because the composition is the same code with a different kernel pointer. */
CQ_TEST(a_narrow_fconv_on_wires_frees_its_workspace_and_restores_the_pool)
{
    static const struct {
        const char *sym; int f, t; uint64_t v;
        int32_t (*fwd)(int32_t); void (*unc)(int32_t, int32_t);
        cq_shim_fcast_kind kind;
    } ROWS[] = {
        { "fptosi_f64_to_i8", 64, 8, 0x4072C00000000000ull,
          cq_template_fptosi_f64_to_i8, cq_template_fptosi_f64_to_i8_unc,
          CQ_SHIM_FCAST_FPTOSI },
        { "sitofp_i8_to_f64", 8, 64, 0xFFu,
          cq_template_sitofp_i8_to_f64, cq_template_sitofp_i8_to_f64_unc,
          CQ_SHIM_FCAST_SITOFP }
    };

    for (size_t r = 0; r < sizeof ROWS / sizeof ROWS[0]; r++) {
        cq_mock m; cq_sink s;
        cq_ctx *ctx = open_with(&m, &s);
        const int32_t a = qreg(ctx, (uint32_t)ROWS[r].f, ROWS[r].v);
        const cq_pc_snap before = cq_pc_take(ctx);
        const int32_t out = ROWS[r].fwd(a);
        const int32_t live[2] = { a, out };
        uint32_t idx[64];
        uint32_t n;

        CHECK_EQ(cq_reg_width(&ctx->regs, out), (uint32_t)ROWS[r].t);
        if (cq_pc_value(ctx, out) != fconv_expect(ROWS[r].kind, ROWS[r].f,
                                                  ROWS[r].t, ROWS[r].v))
            cq_h_fail(__FILE__, __LINE__, "%s on wires = %016llx, want %016llx",
                      ROWS[r].sym, (unsigned long long)cq_pc_value(ctx, out),
                      (unsigned long long)fconv_expect(ROWS[r].kind, ROWS[r].f,
                                                       ROWS[r].t, ROWS[r].v));
        CHECK_EQ(cq_pc_value(ctx, a), ROWS[r].v);

        /* L2: the workspace is GONE. If it were still live it would be an
         * index no named register owns, and this set check is the only thing
         * in the project that can say so. */
        CHECK(cq_pc_live_is_exactly(ctx, live, 2u));
        CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0u);

        n = cq_pc_indices(ctx, out, idx, 64u);
        ROWS[r].unc(out, a);
        CHECK_EQ(cq_pc_value(ctx, out), 0);
        CHECK(cq_pc_live_is_exactly(ctx, live, 2u));

        cqrt_free(out);
        CHECK_EQ(cq_qubits_stranded(&ctx->pool), 0u);
        CHECK_EQ(cq_reg_strand_reports(ctx), 0);
        CHECK(cq_pc_same(before, cq_pc_take(ctx)));
        CHECK(cq_pc_indices_are_free(ctx, idx, n));
        cq_reg_audit(ctx);
        close_with(&m);
    }
}

/* THE SAME COMPOSITION WITH THE SOURCE POISONED — AND THIS IS THE ONE THAT
 * PROVES THE WORKSPACE'S OWN FWD/UNC RECORDS PAIR.
 *
 * On the rotation-free surface the shadow is EXACT, so the workspace returns
 * to |0> visibly and `cq_shim_free_proof` releases it whatever the record
 * says: every defect in the workspace's twin identity is masked. After a
 * general `Ry` the shadow can prove nothing and the workspace's free is the
 * certificate's alone — so the strand delta ACROSS THE FORWARD ALONE is the
 * instrument, taken before the `_unc` runs, because the workspace is minted
 * and freed INSIDE each call and a later count could not attribute it.
 *
 * THE NEGATIVE CONTROL IS WHAT MAKES IT A CLAIM: the probe rail really is
 * UNPROVEN under the rotation-free predicate, so a change that stopped
 * poisoning cannot make the rest of the case vacuous. */
CQ_TEST(a_narrow_fconv_unc_on_a_poisoned_source_is_discharged_by_the_certificate)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    const int32_t a = qreg(ctx, 64u, 0x4072C00000000000ull);
    uint32_t strand_before;
    int32_t out;

    cqrt_ry_f64(a, 0.5);
    strand_before = cq_qubits_stranded(&ctx->pool);

    out = cq_template_fptosi_f64_to_i8(a);
    /* THE WORKSPACE WAS MINTED AND FREED INSIDE THAT CALL. A twin identity the
     * reduction cannot pair leaves its 64 lanes UNPROVEN, and unproven strands
     * — loudly, and here. */
    if (cq_qubits_stranded(&ctx->pool) - strand_before != 0u)
        cq_h_fail(__FILE__, __LINE__,
                  "the narrow row's workspace stranded %u qubit(s): its FWD and "
                  "UNC records did not pair",
                  cq_qubits_stranded(&ctx->pool) - strand_before);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0u);
    CHECK_EQ(cq_reg_disposition(ctx, out, cq_pc_zero_proof_rotation_free),
             CQ_PROOF_UNPROVEN);

    cq_template_fptosi_f64_to_i8_unc(out, a);
    cqrt_free(out);
    if (cq_qubits_stranded(&ctx->pool) - strand_before != 0u)
        cq_h_fail(__FILE__, __LINE__,
                  "the narrow row's result stranded %u qubit(s)",
                  cq_qubits_stranded(&ctx->pool) - strand_before);
    CHECK_EQ(cq_reg_strand_reports(ctx), 0u);

    cq_reg_audit(ctx);
    close_with(&m);
}

/* THE NEGATIVE HALF: AN fp CAST'S `_unc` MUST NOT PAIR WITH AN INTEGER CAST'S
 * FORWARD. The fp conversions claim `0x30000000` and the integer width casts
 * hold `0x80000000`; both fold `kind * 65536 + f * 256 + t`, so `sext i64 ->
 * i64` (kind 0) and `fptosi f64 -> i64` (kind 0) agree on every term BELOW the
 * prefix — same widths, same kind index, same handle slots, same TPL_FWD /
 * TPL_UNC pair, same §9 context, same parities. Only the prefix separates
 * them, and this case is what says so.
 *
 * (64, 64) IS THE ONE WIDTH PAIR BOTH FAMILIES ACCEPT. A narrowing `trunc`
 * with `T > F` and a widening `sext` with `T < F` are hard errors one layer
 * down (`src/kernels/cast.c`), so a fixture at any other pair would be testing
 * that refusal instead.
 *
 * THE SOURCE MUST BE POISONED OR THE SHADOW ANSWERS FIRST, and the two
 * operations must DISAGREE on the operand — `sext` at 64->64 is the identity
 * and `fptosi` of 1.5 is 1, so the rail ends far from zero. */
CQ_TEST(an_fconv_unc_does_not_pair_with_an_integer_cast_on_the_same_handles)
{
    cq_mock m; cq_sink s;
    cq_ctx *ctx = open_with(&m, &s);
    static const uint64_t ONE_AND_A_HALF = 0x3FF8000000000000ull;
    const int32_t a = qreg(ctx, 64u, ONE_AND_A_HALF);
    uint32_t strand_before;
    int32_t out;

    CHECK(cq_fptosi_eval(ONE_AND_A_HALF) != ONE_AND_A_HALF);

    cqrt_ry_f64(a, 0.5);
    strand_before = cq_qubits_stranded(&ctx->pool);

    out = cq_shim_cast(CQ_SHIM_CAST_SEXT, 64, 64, a);
    cq_shim_fcast_unc(CQ_SHIM_FCAST_FPTOSI, 64, 64, out, a);
    cqrt_free(out);

    CHECK(cq_qubits_stranded(&ctx->pool) > strand_before);
    CHECK_EQ(cq_reg_strand_reports(ctx), 1u);

    cq_reg_audit(ctx);
    close_with(&m);
}

/* BEAD 9ve.34, AT THE ABI EDGE. All THREE `uitofp i64 -> f64` symbols abort,
 * and all three carry the BEAD's reason rather than `"fp is v2"` or D14's.
 *
 * WHY ALL THREE AND NOT TWO PLUS AN `_inv`. D14's reason attaches to a LANDED
 * family-width — it is what a `_inv` row of a family whose forward SHIPS is
 * refused for. This pair has not landed at all, so its `_inv` is not a D14
 * row, and printing D14's sentence there would tell a caller that the data
 * family has no uniform definition when the true and narrower fact is that
 * upstream's routing is wrong at this one width. `"fp is v2"` would be worse
 * still: f64 IS v2, and it is already shipping for the other seventeen pairs.
 *
 * THE STRING IS COMPARED BYTE FOR BYTE because a reason that merely mentions
 * the bead is not the same as a reason a maintainer can act on, and because
 * the generator is where it is chosen — a symbol swept into the wrong bucket
 * must say which bucket it landed in. */
static void uitofp_i64_fwd(void *p) { (void)p; (void)cq_template_uitofp_i64_to_f64(0); }
static void uitofp_i64_unc(void *p) { (void)p; cq_template_uitofp_i64_to_f64_unc(0, 0); }
static void uitofp_i64_inv(void *p) { (void)p; (void)cq_template_uitofp_i64_to_f64_inv(0); }

CQ_TEST(the_uitofp_i64_row_aborts_naming_bead_9ve34_on_all_three_symbols)
{
    static const struct { const char *sym; void (*fn)(void *); } ROWS[] = {
        { "cq_template_uitofp_i64_to_f64",      uitofp_i64_fwd },
        { "cq_template_uitofp_i64_to_f64_unc",  uitofp_i64_unc },
        { "cq_template_uitofp_i64_to_f64_inv",  uitofp_i64_inv }
    };
    char msg[512];

    for (size_t i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
        char want[512];
        const int aborted = cq_child_capture(CQ_CHILD_STDERR, ROWS[i].fn,
                                             NULL, NULL, 0u, msg, sizeof msg);
        CHECK(aborted);
        (void)snprintf(want, sizeof want,
                       "cqops: %s not implemented (uitofp i64 -> f64 is "
                       "REFUSED, not unported: upstream routes UIToFP to "
                       "soft_sitofp with no bias correction at this width, so "
                       "every u >= 2^63 would convert as a negative number "
                       "(bead 9ve.34, PRD-v2 7.9))\n", ROWS[i].sym);
        if (strcmp(msg, want) != 0)
            cq_h_fail(__FILE__, __LINE__, "%s said `%s`, wanted `%s`",
                      ROWS[i].sym, msg, want);
    }
}

/* THE SEVENTEEN `_inv` BODIES OF THE LANDED PAIRS. D14's reason, not the fp
 * one: a conversion is non-injective at every width and in every version —
 * `fptosi` of 1.4 and of 1.6 are both 1 — so "fp is v2" there would name a
 * release that will never define it. */
#define CQ_FCONV_INV_THUNK(op, f, t, K, FB, TB)                               \
    static void inv_##op##_##f##_##t(void *p)                                 \
    { (void)p; (void)cq_template_##op##_##f##_to_##t##_inv(0); }
CQ_FCONV_PAIRS(CQ_FCONV_INV_THUNK)

#define CQ_FCONV_INV_ENTRY(op, f, t, K, FB, TB)                               \
    { #op "_" #f "_to_" #t "_inv", inv_##op##_##f##_##t },
static const struct { const char *sym; void (*fn)(void *); } FCONV_INVS[] = {
    CQ_FCONV_PAIRS(CQ_FCONV_INV_ENTRY)
};

CQ_TEST(every_fconv_inv_aborts_naming_itself_and_d14s_reason)
{
    char msg[512];

    CHECK_EQ((int)(sizeof FCONV_INVS / sizeof FCONV_INVS[0]), 17);
    for (size_t i = 0; i < sizeof FCONV_INVS / sizeof FCONV_INVS[0]; i++) {
        char want[320];
        const int aborted = cq_child_capture(CQ_CHILD_STDERR, FCONV_INVS[i].fn,
                                             NULL, NULL, 0u, msg, sizeof msg);
        CHECK(aborted);
        (void)snprintf(want, sizeof want,
                       "cqops: cq_template_%s not implemented "
                       "(_inv is f-inverse, not f; the data family has no "
                       "uniform definition (PRD 15 D14))\n", FCONV_INVS[i].sym);
        if (strcmp(msg, want) != 0)
            cq_h_fail(__FILE__, __LINE__, "%s said `%s`, wanted `%s`",
                      FCONV_INVS[i].sym, msg, want);
    }
}
