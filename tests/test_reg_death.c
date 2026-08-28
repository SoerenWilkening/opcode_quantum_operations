/* tests/test_reg_death.c — M07's fail-loud paths, Step 7.
 *
 * Two groups, and the split between them is deliberate. Everything down to
 * `d7a_out_aliases_a_source` is a hard error in BOTH configurations: a free
 * that cannot prove its rail clean, a use-after-free and a result handle that
 * is also a source are miscompile signatures, not style questions, and Rule 17
 * pins the L6 fixture run at Step 24 under RELEASE, where a Debug-gated assert
 * simply is not there.
 *
 * "A FREE OF A DIRTY RAIL" WAS THE FIRST OF THOSE, UNCONDITIONALLY, UNTIL
 * PRD §15 D15 §4's last clause was confirmed 2026-08-22 and shipped at Step 23.
 * The ACT is now stranding, so the two non-clean rows terminate only under
 * CQOPS_FREE_ABORT — which their block below sets per case, in C, and never
 * through ctest's ENVIRONMENT property. What survives unconditionally in both
 * configurations is one layer down: cq_qubits_release on an index not proven
 * |0>.
 * The I2 audit cases below them are Debug-only by plan §2.1, which names the
 * owner map, so they report a SKIP in Release rather than a false pass. */

#include "reg.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "qubits.h"
#include "shadow.h"

#include "cqops/cqops.h"

#include "support/death.h"

#include <stdlib.h>
#include <string.h>

/* See test_reg.c on why a zero-proof is a static in a test file: it is sound
 * only while no kernel exists, and the library ships none.
 *
 * THIS FILE'S COPY IS THREE-VALUED AND ITS TWO-VALUED PREDECESSOR IS GONE, and
 * -Werror is what noticed: once free_dirty_rail was reshaped onto the CONVICTED
 * row, nothing here read cq_shadow_known_zero any more and the old static went
 * unused. Keeping both would have left a reader guessing which one a case meant
 * to use, on the one question the whole file is now about. The split is by SIGN
 * (PRD §15 D15 §3, src/reg.h): the two-valued form cannot reach the
 * proven-dirty row at all, because cq_shadow_known_zero answers 0 for "unknown"
 * and for "known 1" alike. `unknown` first, always — an entry poisoned while it
 * happened to hold 0 still carries a zero value byte. */
static int proof_shadow_three_valued(const cq_ctx *ctx, int32_t h, uint32_t q)
{
    cq_shadow s = cq_shadow_get(&ctx->shadow, q);
    (void)h;
    if (s.unknown)   return CQ_PROOF_UNPROVEN;
    return s.value == 0 ? CQ_PROOF_CLEAN : CQ_PROOF_DIRTY;
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

/* A MISSING ARGUMENT, AND IT SURVIVED STEP 23 UNCHANGED WHILE ITS FOUR
 * NEIGHBOURS MOVED. PRD §15 D15 §3 makes a free the library cannot clear
 * STRAND rather than abort, and this case does not touch that row: "the caller
 * supplied no oracle" and "the oracle cannot tell" are different facts, and
 * only the second is D15's unproven row. Aborting on the first is strictly
 * more conservative than D15 requires — aborting is not recycling — so it is
 * kept, and it needs no CQOPS_FREE_ABORT to fire. Measured: this case and
 * test_rotate_death.c:a_null_proof_still_refuses_a_materialised_rail were the
 * only two of the six free-time deaths that stayed green when the free path
 * became three-valued. */
static void free_without_proof(void)
{
    open_ctx();
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 4);
    (void)materialise_bit(h, 0, 0);
    CQ_EXPECT_ABORT(cq_reg_free(&ctx, h, NULL));
}

/* --- The two non-clean rows, under CQOPS_FREE_ABORT. --------------------- */

/* NOTHING WAS DELETED HERE AND NOTHING STOPPED BEING TESTED, which is the whole
 * reason these two run under the flag. D15's DEFAULT act for both rows is to
 * strand — asserted positively, by index, in tests/test_reg_free.inc — and
 * CQOPS_FREE_ABORT is the documented way to turn that conviction back into
 * termination without a rebuild. Running the deaths under it keeps the two-pass
 * ordering, the layer discrimination and the flag itself all tested by the same
 * fixtures, and it means the flag has a caller rather than being a promise.
 *
 * THE FLAG IS SET PER CASE, NOT PER BINARY. A ctest ENVIRONMENT property would
 * put it out of sight of the reader and would silently arm every OTHER case in
 * this file — including free_without_proof, whose whole point is that it aborts
 * for a different reason. The C setter wins over the environment by design, so
 * this is also the spelling that cannot be defeated by a stale shell variable. */

/* THIS CASE WAS MISNAMED UNTIL STEP 23 AND IS NOW RESHAPED TO MATCH ITS NAME.
 * Its bad bit used to be materialise_bit(h, 3, POISONED), whose shadow reads
 * `unknown` — that is D15's UNPROVEN row, not the dirty one, and the two are
 * the distinction the whole decision turns on. Renaming it would have been the
 * cheap fix and would have deleted the detector; the bit is instead built
 * determinate-and-non-zero, which is the only shape the shadow can CONVICT.
 * Its unproven sibling is directly below, so the row it used to test is not
 * lost either.
 *
 * THE DIRTY BIT IS LAST, on purpose, and that is what this case is really for.
 * Two clean qubits precede it, so a one-pass free would release both before
 * discovering the problem and would leave the rail half-returned; the two-pass
 * form releases nothing. That property became MORE important at Step 23, not
 * less: under stranding a partly-released rail is the correct outcome, so
 * pass 1's only remaining job is to be the place CQOPS_FREE_ABORT stops — and
 * if it were deleted, the flag would half-return a rail, silently, and only
 * under the flag. The exit code cannot see that; the MESSAGE can, and the
 * FAIL_REGULAR_EXPRESSION in tests/CMakeLists.txt checks it: M07 says
 * "CQOPS_FREE_ABORT: free of a rail PROVEN not to be |0>", M03 says "release of
 * a qubit not proven |0>". A run reporting M03's message means M07's own
 * pre-pass has been lost. */
static void free_dirty_rail(void)
{
    open_ctx();
    cqops_set_free_abort(1);
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 4);
    (void)materialise_bit(h, 0, 0);
    (void)materialise_bit(h, 1, 0);

    /* Determinate and non-zero: materialising a constant ONE emits an X and
     * leaves the shadow {value 1, unknown 0}. */
    cq_bit *b = &cq_reg_bits(&ctx.regs, h)[3];
    *b = cq_bit_one();
    cq_materialise(&ctx, b);

    CQ_EXPECT_ABORT(cq_reg_free(&ctx, h, proof_shadow_three_valued));
}

/* THE ROW free_dirty_rail USED TO OCCUPY. Same act, DIFFERENT VERDICT, and the
 * two are separable for the first time at Step 23 because the messages differ —
 * before it, both rows produced the identical "not provably clean" string and
 * no regex could have told them apart. That separability is not cosmetic: D15
 * §3's residue split is `bd 06t`'s first obligation and it is stated in exactly
 * these two rows. */
static void free_unproven_rail(void)
{
    open_ctx();
    cqops_set_free_abort(1);
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 4);
    (void)materialise_bit(h, 0, 0);
    (void)materialise_bit(h, 3, 1);      /* poisoned control => unknown shadow */
    CQ_EXPECT_ABORT(cq_reg_free(&ctx, h, proof_shadow_three_valued));
}

/* THE FLAG'S OWN FAIL-LOUD PATH, on cqops_set_sink's precedent: an
 * unresolvable value is a hard error, never a quiet substitution. This is the
 * clause that is easy to drop and the one whose absence would hurt most —
 * `CQOPS_FREE_ABORT=true` silently meaning OFF hands a maintainer who asked for
 * termination exactly the silence they were trying to break. */
static void an_unrecognised_free_abort_spelling_is_a_hard_error(void)
{
    open_ctx();
    cqops_set_free_abort(-1);            /* clear the override; read the env */
    /* setenv is POSIX rather than C11, and this file already links against a
     * platform libc; the alternative is a putenv with a mutable buffer, which
     * is worse. */
    setenv("CQOPS_FREE_ABORT", "true", 1);
    CQ_EXPECT_ABORT((void)cq_free_abort_active());
}

/* M03's GUARD, UNDER THE THREE-VALUED CONTRACT. A conviction is a NEGATIVE int,
 * so the pre-Step-23 spelling `if (!proven_zero)` read the strongest refusal
 * the library can make as PROOF and would have put a dirty index on the free
 * list — the one unforgivable bug arriving through the very guard written to
 * prevent it. Nothing above M03 can police this for it: cq_qubits_release never
 * sees a proof function, only a value. */
static void release_of_a_convicted_qubit_is_refused(void)
{
    open_ctx();
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 1);
    uint32_t q = materialise_bit(h, 0, 0);
    CQ_EXPECT_ABORT(cq_qubits_release(&ctx.pool, q, CQ_PROOF_DIRTY));
}

/* --- cqrt_cswap's constant-control row. ---------------------------------- */

static void swap_a_rail_with_itself(void)
{
    open_ctx();
    int32_t h = cq_reg_alloc_zero(&ctx.regs, 8);
    CQ_EXPECT_ABORT(cq_reg_swap_bits(&ctx.regs, h, h));
}

static void swap_between_mismatched_widths(void)
{
    open_ctx();
    int32_t a = cq_reg_alloc_zero(&ctx.regs, 8);
    int32_t b = cq_reg_alloc_zero(&ctx.regs, 16);
    CQ_EXPECT_ABORT(cq_reg_swap_bits(&ctx.regs, a, b));
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
    CQ_DEATH_CASE(free_unproven_rail),
    CQ_DEATH_CASE(an_unrecognised_free_abort_spelling_is_a_hard_error),
    CQ_DEATH_CASE(release_of_a_convicted_qubit_is_refused),
    CQ_DEATH_CASE(swap_a_rail_with_itself),
    CQ_DEATH_CASE(swap_between_mismatched_widths),
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
