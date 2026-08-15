/* tests/test_reg_death.c — M07's fail-loud paths, Step 7.
 *
 * Two groups, and the split between them is deliberate. Everything down to
 * `d7a_out_aliases_a_source` is a hard error in BOTH configurations: a free of
 * a dirty rail, a use-after-free and a result handle that is also a source are
 * miscompile signatures, not style questions, and Rule 17 pins the L6 fixture
 * run at Step 24 under RELEASE, where a Debug-gated assert simply is not there.
 * The I2 audit cases below them are Debug-only by plan §2.1, which names the
 * owner map, so they report a SKIP in Release rather than a false pass. */

#include "reg.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "qubits.h"
#include "shadow.h"

#include "support/death.h"

#include <string.h>

/* See test_reg.c on why the only zero-proof in the tree is a static in a test
 * file: it is sound only while no kernel exists, and the library ships none. */
static int proof_shadow_pre_kernel_only(const cq_ctx *ctx, int32_t h, uint32_t q)
{
    (void)h;
    return cq_shadow_known_zero(&ctx->shadow, q);
}

static cq_ctx ctx;

/* A DISCARDING sink, and not the "leaked gate" detector test_emit_death.c
 * uses. Every case here reaches its abort through ordinary bookkeeping rather
 * than through the emitter, and most of them emit real CX gates while building
 * the rail they then mis-free — so a sink that treated any emission as a
 * failure would fail the setup instead of the assertion. Passing NULL to
 * cq_ctx_init is not an option: it resolves CQOPS_SINK, and "no default sink
 * registered" is itself a hard error, outside the armed window. */
static void nx(void *u, uint32_t q) { (void)u; (void)q; }
static void ncx(void *u, uint32_t c, uint32_t t) { (void)u; (void)c; (void)t; }
static void nccx(void *u, uint32_t a, uint32_t b, uint32_t t)
{ (void)u; (void)a; (void)b; (void)t; }
static void nry(void *u, uint32_t q, double th) { (void)u; (void)q; (void)th; }
static void nrz(void *u, uint32_t q, double ph) { (void)u; (void)q; (void)ph; }
static void nmz(void *u, uint32_t q) { (void)u; (void)q; }

static cq_sink g_sink;

static void open_ctx(void)
{
    g_sink.x  = nx;  g_sink.cx = ncx; g_sink.ccx = nccx;
    g_sink.ry = nry; g_sink.rz = nrz; g_sink.mz  = nmz;
    g_sink.user = NULL;
    cq_ctx_init(&ctx, &g_sink);
}

/* Materialises bit `i` of `h` from a fresh control; poisons the control first
 * when `poison`, which is the only way to get a rail bit whose shadow is
 * unknown (a CX from a known-0 control leaves the target known-0). */
static uint32_t materialise_bit(int32_t h, uint32_t i, int poison)
{
    uint32_t c = cq_ctx_fresh_qubit(&ctx);
    if (poison) cq_shadow_rotate(&ctx.shadow, c);
    cq_bit ctrl = cq_bit_qubit(c);
    cq_emit_cx(&ctx, &ctrl, &cq_reg_bits(&ctx.regs, h)[i]);
    return cq_bit_qindex(cq_reg_cbits(&ctx.regs, h)[i]);
}

/* --- The free path. Both configurations. --------------------------------- */

/* The ckd.17 refusal, made loud. The library defines no proof, so NULL is the
 * honest state of an unresolved P0 blocker: no evidence, no free. */
static void free_without_proof(void)
{
    open_ctx();
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 4);
    (void)materialise_bit(h, 0, 0);
    CQ_EXPECT_ABORT(cq_reg_free(&ctx, h, NULL));
}

static void free_dirty_rail(void)
{
    open_ctx();
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 4);
    /* THE DIRTY BIT IS LAST, on purpose. Two clean qubits precede it, so a
     * one-pass free would release both before discovering the problem and
     * would leave the rail half-returned; the two-pass form releases nothing.
     * The exit code cannot tell those apart — both abort — but the MESSAGE
     * can, and mutation testing checks it: M07 says "not provably clean",
     * M03 says "release of a qubit not proven |0>". A run that reports M03's
     * message means M07's own check has been lost. */
    (void)materialise_bit(h, 0, 0);
    (void)materialise_bit(h, 1, 0);
    (void)materialise_bit(h, 3, 1);      /* poisoned control => unknown shadow */
    CQ_EXPECT_ABORT(cq_reg_free(&ctx, h, proof_shadow_pre_kernel_only));
}

/* None of the three slot states is numbered 0, so a zeroed slot — a memset, a
 * calloc, a zero-initialised struct — is not a valid state and cannot read as
 * a live register with a garbage bits pointer and a garbage width. The 0xAA
 * poison does not cover this: the failure is a zero byte, not a poison byte. */
static void a_zeroed_slot_is_not_a_valid_state(void)
{
    open_ctx();
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 8);
    memset(&ctx.regs.slot[h], 0, sizeof ctx.regs.slot[h]);
    CQ_EXPECT_ABORT((void)cq_reg_state(&ctx.regs, h));
}

static void double_free(void)
{
    open_ctx();
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 8);
    cq_reg_free(&ctx, h, NULL);
    /* M03's double-release cannot fire here: the bits array is already gone,
     * so only M07's tombstone check stands between this and a wild free. */
    CQ_EXPECT_ABORT(cq_reg_free(&ctx, h, NULL));
}

/* Measurement is terminal (PRD §7): 255 measures across the 239 goldens, 0
 * later freed. A measured qubit is also not |0⟩, so this free would launder. */
static void free_of_a_measured_rail(void)
{
    open_ctx();
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 4);
    cq_reg_mark_measured(&ctx.regs, h);
    CQ_EXPECT_ABORT(cq_reg_free(&ctx, h, NULL));
}

static void read_after_free(void)
{
    open_ctx();
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 8);
    cq_reg_free(&ctx, h, NULL);
    /* 0 use-after-free across all 239 goldens, so this really is a bug rather
     * than a shape CQ_lang relies on. */
    CQ_EXPECT_ABORT((void)cq_reg_cbits(&ctx.regs, h));
}

static void write_to_a_measured_rail(void)
{
    open_ctx();
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 4);
    cq_reg_mark_measured(&ctx.regs, h);
    /* Readable through cq_reg_cbits, refused by the mutable accessor. */
    CQ_EXPECT_ABORT((void)cq_reg_bits(&ctx.regs, h));
}

static void handle_out_of_range(void)
{
    open_ctx();
    (void)cq_reg_alloc_zero(&ctx.regs, 8);
    CQ_EXPECT_ABORT((void)cq_reg_state(&ctx.regs, 999));
}

static void handle_negative(void)
{
    open_ctx();
    (void)cq_reg_alloc_zero(&ctx.regs, 8);
    CQ_EXPECT_ABORT((void)cq_reg_state(&ctx.regs, CQ_REG_NONE));
}

static void width_zero(void)
{
    open_ctx();
    CQ_EXPECT_ABORT((void)cq_reg_alloc_zero(&ctx.regs, 0));
}

static void width_over_max(void)
{
    open_ctx();
    /* A RANGE, not a whitelist — 80 is legal, 129 is not. */
    (void)cq_reg_alloc_zero(&ctx.regs, 80);
    CQ_EXPECT_ABORT((void)cq_reg_alloc_zero(&ctx.regs, CQ_REG_WIDTH_MAX + 1u));
}

static void handle_counter_exhausted(void)
{
    open_ctx();
    /* CQ_lang's own `next_handle++` is UB at INT32_MAX (runtime/cq_runtime.c:67).
     * A library whose entire defence is failing loud does not inherit that. */
    ctx.regs.n = INT32_MAX;
    CQ_EXPECT_ABORT((void)cq_reg_alloc_zero(&ctx.regs, 8));
}

static void copy_into_itself(void)
{
    open_ctx();
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 8);
    CQ_EXPECT_ABORT(cq_reg_xor_into(&ctx, h, h));
}

static void copy_between_mismatched_widths(void)
{
    open_ctx();
    int32_t a = cq_reg_alloc_zero(&ctx.regs, 8);
    int32_t b = cq_reg_alloc_zero(&ctx.regs, 16);
    CQ_EXPECT_ABORT(cq_reg_xor_into(&ctx, a, b));
}

/* --- D7a. Both configurations, and deliberately no SKIP macro. ----------- */

static void d7a_out_aliases_a_source(void)
{
    open_ctx();
    int32_t h0 = cq_reg_alloc_zero(&ctx.regs, 32);
    int32_t h1 = cq_reg_alloc_zero(&ctx.regs, 32);
    int32_t s[2] = { h0, h1 };
    /* Measured over all 239 goldens: 0 of 25,147 `_unc` calls put the result
     * handle among its own sources, and it breaks Rule 7's dst ^= f(a,b)
     * outright. The ABSENCE of CQ_DEATH_SKIP_WITHOUT_INVARIANTS is the point —
     * R2's whole value is firing during the Step 24 fixture run, which Rule 17
     * pins under Release. Contrast test_reg.c's D7b case, which asserts that
     * source-source aliasing does NOT abort. */
    CQ_EXPECT_ABORT(cq_reg_check_operands(&ctx.regs, h0, s, 2));
}

static void operand_use_after_free(void)
{
    open_ctx();
    int32_t out = cq_reg_alloc_zero(&ctx.regs, 8);
    int32_t src = cq_reg_alloc_zero(&ctx.regs, 8);
    cq_reg_free(&ctx, src, NULL);
    int32_t s[1] = { src };
    CQ_EXPECT_ABORT(cq_reg_check_operands(&ctx.regs, out, s, 1));
}

/* --- I2, by sweep. Debug-only by plan §2.1; SKIP in Release. ------------- */

/* A double-owned qubit can only be built by writing a bits array directly:
 * cq_materialise cannot produce one, which is exactly why the map has to be
 * swept rather than maintained. None of these three cases releases anything,
 * so M03's double-release guard cannot mask a deleted M07 check. */
static void i2_double_owned(void)
{
    open_ctx();
    int32_t a = cq_reg_alloc_zero(&ctx.regs, 4);
    uint32_t q = materialise_bit(a, 0, 0);
    int32_t b = cq_reg_alloc_zero(&ctx.regs, 4);
    cq_reg_bits(&ctx.regs, b)[1] = cq_bit_qubit(q);

    CQ_DEATH_SKIP_WITHOUT_INVARIANTS("the I2 owner map is CQOPS_DEBUG_INVARIANTS-gated (plan §2.1)");
    CQ_EXPECT_ABORT(cq_reg_audit(&ctx));
}

static void i2_self_double_owned(void)
{
    open_ctx();
    int32_t a = cq_reg_alloc_zero(&ctx.regs, 4);
    uint32_t q = materialise_bit(a, 0, 0);
    /* One register holding the same index twice: a distinct bug from
     * cross-register aliasing, and it double-releases at free. */
    cq_reg_bits(&ctx.regs, a)[3] = cq_bit_qubit(q);

    CQ_DEATH_SKIP_WITHOUT_INVARIANTS("the I2 owner map is CQOPS_DEBUG_INVARIANTS-gated (plan §2.1)");
    CQ_EXPECT_ABORT(cq_reg_audit(&ctx));
}

/* THE DISCRIMINATING CASE FOR THE SWEEP'S FILTER, and the one the suite was
 * missing. Every other i2_* case uses two LIVE rails, so all of them stay green
 * if the filter is narrowed from "skip DEAD" to "skip everything non-LIVE" —
 * measured 2026-08-14, when exactly that edit survived a full 45-test run in
 * both configurations. A measured rail is the one kind whose qubits are
 * deliberately never reclaimed (PRD §7), so it is precisely the rail that must
 * stay under the map's eye for the rest of the program. */
static void i2_measured_rail_shares_a_qubit_with_a_live_one(void)
{
    open_ctx();
    int32_t m = cq_reg_alloc_zero(&ctx.regs, 4);
    uint32_t q = materialise_bit(m, 0, 0);
    cq_reg_mark_measured(&ctx.regs, m);

    int32_t live = cq_reg_alloc_zero(&ctx.regs, 4);
    cq_reg_bits(&ctx.regs, live)[2] = cq_bit_qubit(q);

    CQ_DEATH_SKIP_WITHOUT_INVARIANTS("the I2 owner map is CQOPS_DEBUG_INVARIANTS-gated (plan §2.1)");
    CQ_EXPECT_ABORT(cq_reg_audit(&ctx));
}

static void i2_owns_a_freed_qubit(void)
{
    open_ctx();
    int32_t a = cq_reg_alloc_zero(&ctx.regs, 4);
    uint32_t q = materialise_bit(a, 0, 0);
    cq_qubits_release(&ctx.pool, q, cq_shadow_known_zero(&ctx.shadow, q));
    /* The laundering signature: a live register still naming an index that is
     * back on the free list, so the next cq_materialise hands it to unrelated
     * data. No other module can see this. */
    CQ_DEATH_SKIP_WITHOUT_INVARIANTS("the I2 owner map is CQOPS_DEBUG_INVARIANTS-gated (plan §2.1)");
    CQ_EXPECT_ABORT(cq_reg_audit(&ctx));
}

static void i2_invalid_bit(void)
{
    open_ctx();
    int32_t a = cq_reg_alloc_zero(&ctx.regs, 4);
    /* I1's second clause: a constant carrying a stale qubit index. Left
     * unchecked, a later fold hands a rail owned by someone else to the sink
     * while I4 still reports the register owns nothing. */
    cq_reg_bits(&ctx.regs, a)[2] = (cq_bit){ CQ_BIT_ZERO, 7u };

    CQ_DEATH_SKIP_WITHOUT_INVARIANTS("the I2 owner map is CQOPS_DEBUG_INVARIANTS-gated (plan §2.1)");
    CQ_EXPECT_ABORT(cq_reg_audit(&ctx));
}

CQ_DEATH_MAIN(
    CQ_DEATH_CASE(free_without_proof),
    CQ_DEATH_CASE(free_dirty_rail),
    CQ_DEATH_CASE(a_zeroed_slot_is_not_a_valid_state),
    CQ_DEATH_CASE(double_free),
    CQ_DEATH_CASE(free_of_a_measured_rail),
    CQ_DEATH_CASE(read_after_free),
    CQ_DEATH_CASE(write_to_a_measured_rail),
    CQ_DEATH_CASE(handle_out_of_range),
    CQ_DEATH_CASE(handle_negative),
    CQ_DEATH_CASE(width_zero),
    CQ_DEATH_CASE(width_over_max),
    CQ_DEATH_CASE(handle_counter_exhausted),
    CQ_DEATH_CASE(copy_into_itself),
    CQ_DEATH_CASE(copy_between_mismatched_widths),
    CQ_DEATH_CASE(d7a_out_aliases_a_source),
    CQ_DEATH_CASE(operand_use_after_free),
    CQ_DEATH_CASE(i2_double_owned),
    CQ_DEATH_CASE(i2_self_double_owned),
    CQ_DEATH_CASE(i2_measured_rail_shares_a_qubit_with_a_live_one),
    CQ_DEATH_CASE(i2_owns_a_freed_qubit),
    CQ_DEATH_CASE(i2_invalid_bit)
)
