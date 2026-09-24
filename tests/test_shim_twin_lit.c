/* tests/test_shim_twin_lit.c — D15's twin identity over the LITERAL LANES of a
 * template call (bead 9ve.39, PRD §15 D15).
 *
 * THE OBLIGATION IS UPSTREAM'S AND IT IS PER ARGUMENT. `free_pairing_check.py`
 * pairs a template forward with its `_unc` on `tuple(w.args[1:]) == fwd_args`
 * — every argument, in its own POSITION, literals included — so `_unc(out, a,
 * 3.0)` never undoes `fwd(a, 2.0)`. The port once carried the literal lanes as
 * ONE folded word (`lo ^ hi`, and for fma `b_lo ^ b_hi ^ c_lo ^ c_hi`) that the
 * twin branch of `adjoint_matches` then never read at all; every literal-lane
 * shape therefore paired whatever its literals were. The record now carries
 * each lane in the slot it occupies, masked to the width, and the twin branch
 * compares them.
 *
 * EVERY CASE IS POISONED, AND SAYS WHICH LAYER DECIDED. On the rotation-free
 * surface the shadow is exact and answers first (bd memory
 * the-shadow-masks-every-certificate-defect-off-the-rotation-taint), so each
 * source goes through a general `Ry` and each case asserts three dispositions
 * before the free: the SHADOW's (UNPROVEN — it knows nothing), the
 * CERTIFICATE's alone (CLEAN for a true pair, UNPROVEN for a false one), and
 * the installed combination's (the same as the certificate's — so it is the
 * certificate, not the shadow, that decides every row here).
 *
 * THE MISMATCHED LITERALS ARE CHOSEN SO THE OLD FOLD AGREES: each pair is the
 * forward's words SWAPPED, so the XOR of all literal words is identical across
 * the two halves while the value the kernel sees differs. */

#include "cq_shim.h"
#include "cq_shim_ctx.h"
#include "cq_shim_proof.h"
#include "cq_shim_record.h"
#include "cq_runtime_abi.h"

#include "ctx.h"
#include "reg.h"
#include "qubits.h"
#include "sink.h"

#include "cqops/cqops.h"

#include "support/harness.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"

#include <stdlib.h>
#include <string.h>

#define F64_ONE   0x3ff0000000000000ull
#define F64_TWO   0x4000000000000000ull
#define F64_THREE 0x4008000000000000ull
#define I_X       0x0000000000000005ull
#define I_Y       0x0000000000000030ull

static void fresh(void)
{
    cq_sink_reset();
    unsetenv("CQOPS_SINK");
    cq_shim_ctx_reset();
}

static cq_ctx *open_with(cq_mock *m, cq_sink *s)
{
    cq_mock_init(m);
    *s = cq_mock_sink(m);
    fresh();
    cqops_set_sink(s);
    return cq_shim_ctx();
}

static void close_with(cq_mock *m)
{
    cq_shim_ctx_reset();
    cqops_set_sink(NULL);
    cq_mock_dispose(m);
}

static double f64_of(uint64_t u)
{
    double d;
    memcpy(&d, &u, sizeof d);
    return d;
}

/* Minted THROUGH THE ABI, so the source has a recorded history (bd memory
 * a-test-minted-rail-has-no-certificate-history), then poisoned. */
static int32_t iwire(void)
{
    int32_t h = cqrt_alloc_i64(3);
    cqrt_ry_i64(h, 0.5);
    return h;
}

static int32_t fwire(void)
{
    int32_t h = cqrt_alloc_f64(f64_of(F64_ONE));
    cqrt_ry_f64(h, 0.5);
    return h;
}

/* One literal-carrying shape. `l` is (lane-1 lo, lane-1 hi, lane-2 lo,
 * lane-2 hi); a one-lane shape reads the first two words only. */
typedef struct {
    const char *name;
    int fp;
    int32_t (*fwd)(int32_t a, const uint64_t l[4]);
    void    (*unc)(int32_t out, int32_t a, const uint64_t l[4]);
} shape;

static int32_t bin_hl(int32_t a, const uint64_t l[4])
{ return cq_shim_bin_hl(CQ_SHIM_OP_ADD, 64, a, l[0], l[1]); }
static void bin_hl_u(int32_t o, int32_t a, const uint64_t l[4])
{ cq_shim_bin_hl_unc(CQ_SHIM_OP_ADD, 64, o, a, l[0], l[1]); }
static int32_t bin_lh(int32_t b, const uint64_t l[4])
{ return cq_shim_bin_lh(CQ_SHIM_OP_SUB, 64, l[0], l[1], b); }
static void bin_lh_u(int32_t o, int32_t b, const uint64_t l[4])
{ cq_shim_bin_lh_unc(CQ_SHIM_OP_SUB, 64, o, l[0], l[1], b); }
static int32_t fbin_hl(int32_t a, const uint64_t l[4])
{ return cq_shim_fbin_hl(CQ_SHIM_FOP_FADD, 64, a, l[0], l[1]); }
static void fbin_hl_u(int32_t o, int32_t a, const uint64_t l[4])
{ cq_shim_fbin_hl_unc(CQ_SHIM_FOP_FADD, 64, o, a, l[0], l[1]); }
static int32_t fbin_lh(int32_t b, const uint64_t l[4])
{ return cq_shim_fbin_lh(CQ_SHIM_FOP_FSUB, 64, l[0], l[1], b); }
static void fbin_lh_u(int32_t o, int32_t b, const uint64_t l[4])
{ cq_shim_fbin_lh_unc(CQ_SHIM_FOP_FSUB, 64, o, l[0], l[1], b); }
static int32_t fma_qll(int32_t a, const uint64_t l[4])
{ return cq_shim_fma_qll(CQ_SHIM_FMA_FMA, 64, a, l[0], l[1], l[2], l[3]); }
static void fma_qll_u(int32_t o, int32_t a, const uint64_t l[4])
{ cq_shim_fma_qll_unc(CQ_SHIM_FMA_FMA, 64, o, a, l[0], l[1], l[2], l[3]); }

/* `qlq` needs a live third source; it is minted per call and never freed
 * (it is poisoned and nonzero — freeing it would strand by design and muddy
 * the count). Both halves must name the SAME `c`, so it is cached. */
static int32_t g_c = -1;
static int32_t fma_qlq(int32_t a, const uint64_t l[4])
{ g_c = fwire(); return cq_shim_fma_qlq(CQ_SHIM_FMA_FMA, 64, a, l[0], l[1], g_c); }
static void fma_qlq_u(int32_t o, int32_t a, const uint64_t l[4])
{ cq_shim_fma_qlq_unc(CQ_SHIM_FMA_FMA, 64, o, a, l[0], l[1], g_c); }

static const shape SHAPES[] = {
    { "bin_hl",  0, bin_hl,  bin_hl_u  },
    { "bin_lh",  0, bin_lh,  bin_lh_u  },
    { "fbin_hl", 1, fbin_hl, fbin_hl_u },
    { "fbin_lh", 1, fbin_lh, fbin_lh_u },
    { "fma_qll", 1, fma_qll, fma_qll_u },
    { "fma_qlq", 1, fma_qlq, fma_qlq_u },
};
#define N_SHAPES (sizeof SHAPES / sizeof SHAPES[0])

/* The forward's literals and the `_unc`'s, per shape. Swapped words: the old
 * fold's XOR agrees, the value does not. */
static void lits(size_t i, uint64_t f[4], uint64_t u[4])
{
    const int fp = SHAPES[i].fp;
    const uint64_t x = fp ? F64_TWO : I_X, y = fp ? F64_THREE : I_Y;

    if (strcmp(SHAPES[i].name, "fma_qll") == 0) {
        f[0] = x; f[1] = 0u; f[2] = y; f[3] = 0u;   /* b = x, c = y */
        u[0] = y; u[1] = 0u; u[2] = x; u[3] = 0u;   /* b = y, c = x */
    } else {
        f[0] = x; f[1] = y; f[2] = 0u; f[3] = 0u;   /* lo = x, hi = y */
        u[0] = y; u[1] = x; u[2] = 0u; u[3] = 0u;   /* lo = y, hi = x */
    }
}

/* One round trip on a poisoned source, and the verdict of each layer. */
static void round_trip(const shape *s, const uint64_t lf[4],
                       const uint64_t lu[4], int want_pair)
{
    cq_mock m; cq_sink sk;
    cq_ctx *ctx = open_with(&m, &sk);
    const int32_t a = s->fp ? fwire() : iwire();
    const uint32_t strand0 = cq_qubits_stranded(&ctx->pool);
    const int want = want_pair ? CQ_PROOF_CLEAN : CQ_PROOF_UNPROVEN;
    int32_t out;

    out = s->fwd(a, lf);
    s->unc(out, a, lu);

    /* THE SHADOW KNOWS NOTHING — the negative control that makes the rest a
     * claim about the certificate. */
    if (cq_reg_disposition(ctx, out, cq_shim_shadow_proof) != CQ_PROOF_UNPROVEN)
        cq_h_fail(__FILE__, __LINE__, "%s: the shadow decided", s->name);
    if (cq_reg_disposition(ctx, out, cq_shim_certificate) != want)
        cq_h_fail(__FILE__, __LINE__, "%s: certificate said %d, wanted %d",
                  s->name, cq_reg_disposition(ctx, out, cq_shim_certificate),
                  want);
    CHECK_EQ(cq_reg_disposition(ctx, out, cq_shim_free_proof), want);

    cqrt_free(out);
    if (want_pair) {
        CHECK_EQ(cq_qubits_stranded(&ctx->pool) - strand0, 0u);
        CHECK_EQ(cq_reg_strand_reports(ctx), 0u);
    } else {
        /* STRANDED, LOUDLY: counted, reported once, never on the free list.
         * D15 §3 working, not failing. */
        if (!(cq_qubits_stranded(&ctx->pool) > strand0))
            cq_h_fail(__FILE__, __LINE__, "%s: released a mismatched pair",
                      s->name);
        CHECK_EQ(cq_reg_strand_reports(ctx), 1u);
    }
    cq_reg_audit(ctx);
    close_with(&m);
}

/* (1) Swapped literals: must NOT pair, on every literal-carrying shape. */
CQ_TEST(an_unc_with_swapped_literals_does_not_pair_with_its_forward)
{
    for (size_t i = 0; i < N_SHAPES; i++) {
        uint64_t f[4], u[4];
        lits(i, f, u);
        round_trip(&SHAPES[i], f, u, 0);
    }
}

/* (2) Identical literals still pair and release. */
CQ_TEST(an_unc_with_identical_literals_pairs_with_its_forward)
{
    for (size_t i = 0; i < N_SHAPES; i++) {
        uint64_t f[4], u[4];
        lits(i, f, u);
        round_trip(&SHAPES[i], f, f, 1);
    }
}

/* (1b) A literal differing in the ONE word the fold would also have seen:
 * the old twin branch never read the fold at all, so even this paired. */
CQ_TEST(an_fp_unc_at_a_different_literal_does_not_pair)
{
    const uint64_t f[4] = { F64_TWO, 0u, 0u, 0u };
    const uint64_t u[4] = { F64_THREE, 0u, 0u, 0u };
    round_trip(&SHAPES[2], f, u, 0);
}

/* (2b) THE IDENTITY IS THE LITERAL THE KERNEL SEES — its W low bits — and not
 * the words it arrived in. `cq_bits_from_words` reads exactly W bits, so two
 * encodings equal mod 2^W compute the same f and ARE a pair; refusing them
 * would strand a clean rail (safe, but a regression of completeness). */
CQ_TEST(bits_above_the_width_are_not_part_of_a_literals_identity)
{
    const uint64_t f[4] = { I_X, 0u, 0u, 0u };
    const uint64_t u[4] = { I_X, ~0ull, 0u, 0u };
    round_trip(&SHAPES[0], f, u, 1);
}

/* (3) THE RECORD WALK: the lane lands in the slot it occupies, masked, with
 * every handle slot's lane zero and `imm` untouched. A strand counter cannot
 * see a lane written to the wrong slot while both halves agree on it (bd
 * memory a-dropped-template-record-is-seen-only-by-a-record-walk). All-classical
 * rails: the record is written whatever the operands' kinds. */
static const cq_call_rec *last_tpl(void)
{
    for (uint32_t p = cq_rec_len(); p-- > 0u;) {
        const cq_call_rec *c = cq_rec_at(p);
        if (c->op == CQ_ROP_TPL_FWD || c->op == CQ_ROP_TPL_UNC) return c;
    }
    return NULL;
}

static void want_lits(const cq_call_rec *c, const uint64_t w[CQ_REC_SLOTS][2])
{
    CHECK(c != NULL);
    if (!c) return;
    CHECK_EQ(c->imm, 0u);
    for (unsigned i = 0; i < CQ_REC_SLOTS; i++) {
        CHECK_EQ(c->lit[i][0], w[i][0]);
        CHECK_EQ(c->lit[i][1], w[i][1]);
    }
}

CQ_TEST(each_literal_lane_is_recorded_in_its_own_slot_masked_to_the_width)
{
    cq_mock m; cq_sink sk;
    cq_ctx *ctx = open_with(&m, &sk);
    const int32_t i8 = cqrt_alloc_i8(7), f = cqrt_alloc_f64(f64_of(F64_ONE));
    const int32_t i64 = cqrt_alloc_i64(9);

    (void)ctx;
    (void)cq_shim_bin_hl(CQ_SHIM_OP_ADD, 8, i8, 0x1234u, 0x55u);
    want_lits(last_tpl(), (const uint64_t[CQ_REC_SLOTS][2]){
                  {0u, 0u}, {0u, 0u}, {0x34u, 0u}, {0u, 0u} });
    (void)cq_shim_bin_lh(CQ_SHIM_OP_SUB, 64, I_X, I_Y, i64);
    want_lits(last_tpl(), (const uint64_t[CQ_REC_SLOTS][2]){
                  {0u, 0u}, {I_X, 0u}, {0u, 0u}, {0u, 0u} });
    (void)cq_shim_fma_qll(CQ_SHIM_FMA_FMA, 64, f, F64_TWO, 1u, F64_THREE, 2u);
    want_lits(last_tpl(), (const uint64_t[CQ_REC_SLOTS][2]){
                  {0u, 0u}, {0u, 0u}, {F64_TWO, 0u}, {F64_THREE, 0u} });
    (void)cq_shim_fma_qql(CQ_SHIM_FMA_FMA, 64, f, f, F64_TWO, 0u);
    want_lits(last_tpl(), (const uint64_t[CQ_REC_SLOTS][2]){
                  {0u, 0u}, {0u, 0u}, {0u, 0u}, {F64_TWO, 0u} });
    close_with(&m);
}

CQ_TEST_MAIN(
    CQ_CASE(an_unc_with_swapped_literals_does_not_pair_with_its_forward),
    CQ_CASE(an_unc_with_identical_literals_pairs_with_its_forward),
    CQ_CASE(an_fp_unc_at_a_different_literal_does_not_pair),
    CQ_CASE(bits_above_the_width_are_not_part_of_a_literals_identity),
    CQ_CASE(each_literal_lane_is_recorded_in_its_own_slot_masked_to_the_width)
)
