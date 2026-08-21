/* tests/test_unc.c — Step 21. THE UNCOMPUTE AXIS AS ITS OWN SUBJECT.
 *
 * This step builds no module. `_unc` IS the same kernel called again (Rule 7),
 * so there is nothing to write in `src/` and everything to write here — and
 * plan §4's Green column says so: "(thin, in M26)", where M26 is Step 23.
 *
 * WHAT THIS SUITE DOES *NOT* OWN. "L3 green across all kernels" is discharged
 * by tests/support/kerneldrv.c, whose cq_kd_case runs forward -> uncompute ->
 * free on every case of every kernel THE SHARED DRIVER DRIVES — the ten Rule-7
 * modules; K8 is not one of them and restates its levels by hand — and again,
 * at the cheap widths its `*_narrow` bodies reach, under each of §9's four
 * regions. Re-driving those ten here with hand-written call adapters would be
 * a fourth transcription of tests/test_kernel_*.c and would detect nothing
 * they do not.
 *
 * BUT THAT COVERAGE IS AT A FIXED REPRESENTATION, WHICH IS THE WHOLE POINT.
 * cq_kd_case never rotates, so no per-kernel suite has ever run a forward and
 * an uncompute at DIFFERENT bit-kinds — and one of the four regions,
 * CQ_KD_CTRL_ZERO, is §9 row 0's skip, where L3's "forward -> uncompute ->
 * all-zero" is satisfied by an empty circuit. What is left is the axis's own
 * claims, and they are the four groups below.
 *
 * ---------------------------------------------------------------------------
 * THE PRD §10 ASYMMETRY, QUOTED, BECAUSE PLAN §6's RISK R6 REQUIRES IT HERE:
 *
 *   > Do not assert that `out`'s bit-kinds match what the forward produced —
 *   > they often will not. Because we never demote (D6), an in-place general
 *   > `cqrt_ry` applied to a *source* between the forward call and the
 *   > uncompute point materialises bits that were constants at forward time. CQ's reverse-program-order restores the source's **state** before
 *   > our `_unc` runs, but not our **representation** of it, so `_unc`
 *   > legitimately emits a larger gate sequence than the forward did. The XOR
 *   > still cancels — same mathematical `f(a,b)`, different circuit realising
 *   > it.
 *   >
 *   > Consequences: (i) the only sound postcondition is on *values*, never on
 *   > kinds; (ii) L4 must pin forward and `_unc` counts **separately** —
 *   > `unc == forward` is not an invariant; (iii) this is the strongest
 *   > argument for revisiting D6 [...]
 *
 * R6 is that someone "fixes" the asymmetry by asserting equality. Until this
 * step, EVERY statement of it in the tree was a comment: all 399 pinned
 * second-pass pairs — 390 forward/`unc`, plus K8's 9 forward/`reverse`, which
 * is a palindromic replay and not this axis at all — carry identical tuples,
 * necessarily, because L4 is measured at the all-quantum mask, which is the
 * FIXED POINT of the drift. The goldens' separate `pass` column could
 * therefore never have gone red.
 * tests/test_unc_asym.inc is the fixture where the two differ, so the
 * discipline is a passing witness rather than a policy.
 *
 * ---------------------------------------------------------------------------
 * THE FOUR GROUPS.
 *
 *   1. Rule 7's contract along the axis — tests/test_unc_contract.inc.
 *      `dst ^= f` at a dst that is NOT zero: cq_kd_case mints dst as a fresh
 *      all-CQ_BIT_ZERO register, so until now no kernel in this project had
 *      ever been ENTERED with a dst holding anything else. (A `CQ_BIT_ONE` dst
 *      lane arises mid-kernel today — `cq_kernel_xor` with a classical ONE
 *      source is the recorded witness — but never at entry.)
 *
 *   2. The asymmetry — tests/test_unc_asym.inc. See its own header.
 *
 *   3. The free. `_unc` reclaims nothing (PRD §10), so the free is a required
 *      third step; what the axis claims is that the uncompute is what makes it
 *      LEGAL. Asserted here as a predicate (cq_reg_clean before and after);
 *      the abort itself is tests/test_unc_death.c.
 *
 *   4. The record. Every golden that pins a forward pass must pin its second
 *      pass separately — the R6 mitigation, checked over the files rather than
 *      trusted to a comment inside them.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS SUITE CANNOT USE `cq_pc_value` (tests/support/poolcheck.h:63-66).
 * A general `Ry` poisons, and cq_pc_value records a HARNESS FAILURE on a
 * poisoned bit — correctly, for a suite that never rotates. This one rotates
 * on purpose, so it carries `ux_read`, which returns the value AND whether the
 * shadow could still tell. The tempting substitute, tests/test_rotate_table.
 * inc's `rt_value`, returns 0 for a poisoned bit as PRD §7's MEASUREMENT
 * specifies — which would make every "dst is zero after the uncompute"
 * assertion here pass vacuously against a rail that is anything at all.
 *
 * ---------------------------------------------------------------------------
 * TWO SPLITS, BOTH RECORDED RATHER THAN IMPROVISED. This file reached 375 of
 * Rule 12's 300 with all four groups in it. The seams are named in the two
 * .inc headers: `the CONTRACT` against `the CONSEQUENCES`
 * (test_unc_contract.inc), and `the AXIS's COST` as its own subject
 * (test_unc_asym.inc, which carries the round-trip driver nothing else needs).
 */

#include "support/harness.h"

#include "controlled.h"
#include "ctx.h"
#include "emit.h"
#include "reg.h"
#include "rotate.h"
#include "shadow.h"
#include "support/bitkinds.h"
#include "support/goldens.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"
#include "support/refmodel.h"

#include "kernels/add.h"
#include "kernels/addacc.h"
#include "kernels/bitwise.h"
#include "kernels/cmp.h"
#include "kernels/divrem_u.h"
#include "kernels/mul.h"

#include <stdio.h>
#include <string.h>

/* --- the fixture ---------------------------------------------------------
 *
 * A mock sink rather than a counter, because two of the asymmetry cases need
 * the ORDERED stream and not only the totals — cq_mock carries both
 * (`by_op[]` is the tuple). */
typedef struct {
    cq_ctx  ctx;
    cq_mock mock;
    cq_sink sink;
} ux_fix;

static void ux_open(ux_fix *f)
{
    cq_mock_init(&f->mock);
    f->sink = cq_mock_sink(&f->mock);
    cq_ctx_init(&f->ctx, &f->sink);
}

/* Deliberately frees nothing. A rail holding a value on materialised qubits is
 * genuinely not |0>, and a rotation-tainted one cannot be proven clean at all
 * (bd 2cf); refusing to free either is Rule 6 working. cq_ctx_dispose returns
 * nothing to the pool, which is PRD §10's intended Rule-6 safe leak. */
static void ux_close(ux_fix *f)
{
    cq_ctx_dispose(&f->ctx);
    cq_mock_dispose(&f->mock);
}

/* THE READER. Returns 1 iff every bit of `h` is still determinate, and writes
 * the value it could reconstruct either way. A caller that ignores the return
 * value has written a vacuous assertion — see this file's header. */
static int ux_read(const cq_ctx *ctx, int32_t h, uint64_t *out)
{
    const cq_bit *b = cq_reg_cbits(&ctx->regs, h);
    uint32_t      W = cq_reg_width(&ctx->regs, h);
    uint64_t      v = 0u;
    int           determinate = 1;

    for (uint32_t i = 0; i < W; i++) {
        if (cq_bit_is_const(b[i])) {
            v |= (uint64_t)cq_bit_value(b[i]) << i;
            continue;
        }

        cq_shadow s = cq_shadow_get(&ctx->shadow, cq_bit_qindex(b[i]));
        if (s.unknown) { determinate = 0; continue; }
        v |= (uint64_t)(s.value != 0) << i;
    }

    *out = v;
    return determinate;
}

/* Does `h` still hold qubit index `q`? There is no cq_reg_owns_qubit in M07 —
 * cq_reg_owned_qubits returns a COUNT, and a count cannot see an index handed
 * back and another taken. */
static int ux_holds(const cq_ctx *ctx, int32_t h, uint32_t q)
{
    const cq_bit *b = cq_reg_cbits(&ctx->regs, h);
    uint32_t      W = cq_reg_width(&ctx->regs, h);

    for (uint32_t i = 0; i < W; i++)
        if (cq_bit_is_qubit(b[i]) && cq_bit_qindex(b[i]) == q) return 1;
    return 0;
}

/* --- the catalogue, in the one shape this suite drives -------------------
 *
 * ARITY 2, ONE OPERAND WIDTH — and this table is a SUBSET of the kernels that
 * fit it, not the whole of them. Rule 7 fixes the SEMANTICS, not the parameter
 * list (bd ckd.15), so what selects the rows below is only "drivable without a
 * call adapter". FOUR FAMILIES ARE STILL OUTSIDE IT and the enumeration is
 * exact because an earlier draft of this comment miscounted:
 *
 *   M13 casts       two widths, so they leave the parameter list
 *   M17 mux         three sources
 *   M11 shift_const the amount is constrained classical — and a DRIFTED amount
 *                   is a hard error (cq_shift_amount), which is PRD §10's
 *                   asymmetry turning into an abort() in one kernel; bd 8gp
 *   M12 shift_var   fits `ux_fn` exactly, and is deferred rather than absent:
 *                   its drift is a ROUTE CHANGE, not a delta — the forward
 *                   takes the classical short-circuit and the uncompute opens
 *                   a sandwich the forward never opened. bd 8gp
 *   M20 sdiv/srem   fit `ux_fn` exactly; the unsigned pair below carries the
 *                   same loop, so they add width rather than shape
 *   M15 addacc      not a Rule 7 kernel at all, and its EXCLUSION is a claim:
 *                   `an_accumulate_is_not_its_own_uncompute`
 *
 * `pred` is K9's departure as well as its reference: `dst` is one bit and `W`
 * is the OPERAND width (src/kernels/cmp.h), so a harness that passed the dst
 * width as W would run every compare at W=1 and pass. */
typedef void (*ux_fn)(cq_ctx *, cq_bit *, const cq_bit *, const cq_bit *, int);

/* `pred` is the K9 predicate or UX_NOT_A_COMPARE; it doubles as the flag_dst
 * marker, because "dst is one bit" and "this is a compare" are the same fact. */
enum { UX_NOT_A_COMPARE = -1 };

typedef struct {
    const char *name;
    ux_fn       fn;
    int         pred;
} ux_kernel;

static const ux_kernel UX_K[] = {
    { "xor",  cq_kernel_xor,  UX_NOT_A_COMPARE },
    { "and",  cq_kernel_and,  UX_NOT_A_COMPARE },
    { "or",   cq_kernel_or,   UX_NOT_A_COMPARE },
    { "add",  cq_kernel_add,  UX_NOT_A_COMPARE },
    { "sub",  cq_kernel_sub,  UX_NOT_A_COMPARE },
    { "mul",  cq_kernel_mul,  UX_NOT_A_COMPARE },
    { "udiv", cq_kernel_udiv, UX_NOT_A_COMPARE },
    { "urem", cq_kernel_urem, UX_NOT_A_COMPARE },

    /* ALL TEN PREDICATES, AND THE FIVE THAT WERE MISSING ARE THE ONES WHOSE
     * DELTA DIFFERS. K9's four SWAPPING predicates put the caller's `b` into
     * `cq_ult_block.a`, which lower_ult! reads TWICE per compute half rather
     * than once — measured (+0,+2,+2) for `ugt`/`ule` against (+0,+2,+0) for
     * `ult`/`uge`. `sgt`/`sle` swap too and do NOT differ, because slt_compute
     * copies both operands into `af`/`bf` first.
     *
     * THIS IS THE FIRST PLACE IN THE PROJECT THAT CAN SEE A WRONG `swap` FLAG
     * STRUCTURALLY. CLAUDE.md records that `uge`-meaning-`ule` "emits the same
     * tuple, keeps the palindrome, and leaves scratch clean — only L1 can tell
     * them apart"; that is true at the ALL-QUANTUM mask, which is the only mask
     * L4 pins. At the drift's mixed mask the tuples differ, and a table built
     * from the five non-swapping predicates alone would have been built so it
     * could not fire. */
    { "eq",   cq_kernel_eq,   CQ_ICMP_EQ  }, { "ne",  cq_kernel_ne,  CQ_ICMP_NE  },
    { "ult",  cq_kernel_ult,  CQ_ICMP_ULT }, { "ugt", cq_kernel_ugt, CQ_ICMP_UGT },
    { "ule",  cq_kernel_ule,  CQ_ICMP_ULE }, { "uge", cq_kernel_uge, CQ_ICMP_UGE },
    { "slt",  cq_kernel_slt,  CQ_ICMP_SLT }, { "sgt", cq_kernel_sgt, CQ_ICMP_SGT },
    { "sle",  cq_kernel_sle,  CQ_ICMP_SLE }, { "sge", cq_kernel_sge, CQ_ICMP_SGE },
};
enum { UX_N_K = (int)(sizeof UX_K / sizeof UX_K[0]) };

static int ux_wdst(const ux_kernel *k, int W)
{
    return k->pred == UX_NOT_A_COMPARE ? W : 1;
}

/* The plain-C answer. The compare rows go through cq_ref_icmp rather than a
 * local `<`, because that function reasons from the two sign bits instead of
 * the kernel's bias flip — tests/support/refmodel.h:125-134 — and a reference
 * that shared the construction could not disagree with it. */
static uint64_t ux_ref(const ux_kernel *k, uint64_t a, uint64_t b, int W)
{
    cq_ref_w wa = cq_ref_w_make(a, 0u, W), wb = cq_ref_w_make(b, 0u, W);

    if (k->pred != UX_NOT_A_COMPARE)
        return (uint64_t)cq_ref_icmp((cq_icmp_pred)k->pred, wa, wb, W);

    if (!strcmp(k->name, "xor"))  return cq_ref_xor(a, b, W);
    if (!strcmp(k->name, "and"))  return cq_ref_and(a, b, W);
    if (!strcmp(k->name, "or"))   return cq_ref_or (a, b, W);
    if (!strcmp(k->name, "add"))  return cq_ref_w_add (wa, wb, W).lo;
    if (!strcmp(k->name, "sub"))  return cq_ref_w_sub (wa, wb, W).lo;
    if (!strcmp(k->name, "mul"))  return cq_ref_w_mul (wa, wb, W).lo;
    if (!strcmp(k->name, "udiv")) return cq_ref_w_udiv(wa, wb, W).lo;
    if (!strcmp(k->name, "urem")) return cq_ref_w_urem(wa, wb, W).lo;

    cq_h_fail(__FILE__, __LINE__, "ux_ref: no reference for %s", k->name);
    return 0u;
}

/* =========================================================================
 * GROUP 3 — the free, and how the axis composes with §9's.
 * ========================================================================= */

/* `_unc` OWNS NO QUBITS AND RECLAIMS NOTHING (PRD §10), so the free is a
 * required third step. What the axis claims is that the uncompute is what
 * makes the free LEGAL — asserted as the predicate here, with the abort itself
 * in tests/test_unc_death.c so that the two configurations agree on which
 * layer speaks. */
CQ_TEST(the_uncompute_is_what_makes_the_free_legal)
{
    /* A DIRTY RAIL IS THE PREMISE, so the pair has to make f(a,b) NON-ZERO —
     * and no single pair does that for all thirteen: `eq` needs a == b and
     * `ne` needs a != b, and `ult(11, 6)` is 0. Each kernel takes the first
     * pair that gives it a non-zero result, and a kernel for which none does
     * is a failure rather than a skip. */
    static const uint64_t PAIR[][2] = {
        { 0xBu, 0x6u }, { 0x6u, 0xBu }, { 0x6u, 0x6u }, { 0xFu, 0x1u },
    };
    const int W = 4;

    for (int ki = 0; ki < UX_N_K; ki++) {
        const ux_kernel *k = &UX_K[ki];
        int found = 0;

        for (unsigned p = 0; p < sizeof PAIR / sizeof PAIR[0] && !found; p++) {
            ux_fix f;
            int wd = ux_wdst(k, W);
            uint64_t va = PAIR[p][0], vb = PAIR[p][1], mid;

            if ((ux_ref(k, va, vb, W) & cq_ref_mask(wd)) == 0u) continue;
            found = 1;

            ux_open(&f);
            int32_t ha = cq_bk_reg(&f.ctx, (uint32_t)W, va, ~0ull);
            int32_t hb = cq_bk_reg(&f.ctx, (uint32_t)W, vb, ~0ull);
            int32_t hd = cq_reg_alloc_zero(&f.ctx.regs, (uint32_t)wd);

            cq_pc_snap before = cq_pc_take(&f.ctx);

            k->fn(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
                  cq_reg_cbits(&f.ctx.regs, ha),
                  cq_reg_cbits(&f.ctx.regs, hb), W);

            CHECK(ux_read(&f.ctx, hd, &mid));
            CHECK(mid != 0u);

            /* PRD §10's "`_unc` OWNS NO QUBITS AND RECLAIMS NOTHING" was prose
             * in two files and an assertion in none, and the two places that
             * could have asserted it both name dst's indices AFTER the
             * uncompute — where an `_unc` that returned them to the pool and
             * demoted the bits leaves n_held == 0, every later check vacuous,
             * and the run green in both configurations. Naming them HERE is
             * what makes the sentence falsifiable. */
            uint32_t fwd_held[128];
            uint32_t n_fwd_held = cq_pc_indices(&f.ctx, hd, fwd_held, 128u);
            cq_pc_snap mid_snap = cq_pc_take(&f.ctx);

            if (cq_reg_clean(&f.ctx, hd, cq_pc_zero_proof_rotation_free))
                cq_h_fail(__FILE__, __LINE__,
                          "%s: the rail holds 0x%llx after the FORWARD and "
                          "reports provably clean — freeing it there is the "
                          "silent state collapse Rule 6 exists to refuse",
                          k->name, (unsigned long long)mid);

            k->fn(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
                  cq_reg_cbits(&f.ctx.regs, ha),
                  cq_reg_cbits(&f.ctx.regs, hb), W);

            if (!cq_reg_clean(&f.ctx, hd, cq_pc_zero_proof_rotation_free))
                cq_h_fail(__FILE__, __LINE__,
                          "%s: the rail is NOT provably clean after the "
                          "uncompute", k->name);

            /* ...and it still owns every index it owned before, on the free
             * list nowhere. `live` may only have GROWN (an uncompute can
             * materialise a lane; it can never hand one back). */
            for (uint32_t q = 0u; q < n_fwd_held; q++)
                if (!ux_holds(&f.ctx, hd, fwd_held[q]))
                    cq_h_fail(__FILE__, __LINE__,
                              "%s: q%u was dst's before the uncompute and is "
                              "not after — PRD §10: `_unc` owns no qubits and "
                              "RECLAIMS NOTHING; cqrt_free is the sole "
                              "deallocator", k->name, fwd_held[q]);
            if (cq_pc_take(&f.ctx).live < mid_snap.live)
                cq_h_fail(__FILE__, __LINE__,
                          "%s: live fell across the uncompute", k->name);

            uint32_t held[128];
            uint32_t n_held = cq_pc_indices(&f.ctx, hd, held, 128u);
            cq_reg_free(&f.ctx, hd, cq_pc_zero_proof_rotation_free);

            cq_pc_snap after = cq_pc_take(&f.ctx);
            if (!cq_pc_same(before, after))
                cq_h_fail(__FILE__, __LINE__,
                          "%s: live %u -> %u after the free", k->name,
                          before.live, after.live);
            CHECK(cq_pc_indices_are_free(&f.ctx, held, n_held));

            /* L2 AS A SET, NOT A COUNT. cq_pc_same is `a.live == b.live`, and
             * the provocation CLAUDE.md records — acquire one ancilla, release
             * one index belonging to a SOURCE — nets to zero and passes every
             * count comparison above while `ha` names a qubit on the free list.
             * The two source handles are in scope; asserting them costs a
             * line. */
            const int32_t owners[2] = { ha, hb };
            CHECK(cq_pc_live_is_exactly(&f.ctx, owners, 2u));
            ux_close(&f);
        }

        if (!found)
            cq_h_fail(__FILE__, __LINE__,
                      "%s: no operand pair in the table makes the rail dirty, "
                      "so this kernel was never tested here", k->name);
    }
}

/* THE TWO AXES COMPOSE ONLY WHEN THE REGION BRACKETS BOTH CALLS, and
 * tests/support/kerneldrv.c:159-165 records this as the reason `call_kernel`
 * wraps each call rather than cq_kd_case wrapping the pair. It had no
 * executable statement. An uncontrolled uncompute after a controlled forward
 * leaves dst at `ctrl · f(a,b)` — right for a control that is on, WRONG for
 * one that is off, and the failure is a rail that never returns to |0> while
 * every gate in it was individually correct. */
CQ_TEST(an_uncompute_outside_the_forwards_region_does_not_cancel)
{
    for (int ctrl_on = 0; ctrl_on <= 1; ctrl_on++)
        for (int bracket_both = 0; bracket_both <= 1; bracket_both++) {
            ux_fix f;
            const int W = 4;
            const uint64_t va = 0xBu, vb = 0x6u;
            uint64_t got;

            ux_open(&f);
            int32_t ha = cq_bk_reg(&f.ctx, (uint32_t)W, va, ~0ull);
            int32_t hb = cq_bk_reg(&f.ctx, (uint32_t)W, vb, ~0ull);
            int32_t hd = cq_reg_alloc_zero(&f.ctx.regs, (uint32_t)W);
            int32_t hc = cq_bk_reg(&f.ctx, 1u, (uint64_t)ctrl_on, 1u);
            const cq_bit *ctrl = cq_reg_cbits(&f.ctx.regs, hc);

            /* THE MODE'S NAME AGAINST THE RAIL'S KIND, which is Step 20's own
             * recorded lesson arriving one layer up: degrade this `1u` qmask to
             * `0u` and the control becomes CLASSICAL, §9 row 0 folds the region
             * away, and ALL FOUR rows below still produce exactly the values
             * they expect — a ZERO control skips both calls (0 == 0), a ONE
             * control emits both verbatim (f ^ f == 0), and the mismatched row
             * gets `f` from the uncontrolled uncompute either way. The case
             * would then be green with no promotion anywhere in it. Every
             * assertion under a region is conditional on the region it says it
             * opened. */
            CHECK(cq_bit_is_qubit(*ctrl));

            cq_ctrl_push(&f.ctx, ctrl);
            CHECK_EQ(cq_ctrl_depth(&f.ctx), 1);
            cq_kernel_and(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
                          cq_reg_cbits(&f.ctx.regs, ha),
                          cq_reg_cbits(&f.ctx.regs, hb), W);
            if (cq_mock_count(&f.mock) == 0u)
                cq_h_fail(__FILE__, __LINE__,
                          "ctrl=%d: the forward emitted nothing. A PROMOTED "
                          "region emits on both branches — an empty stream "
                          "means row 0 skipped it, i.e. the control was not a "
                          "wire", ctrl_on);

            if (!bracket_both) cq_ctrl_pop(&f.ctx);

            if (bracket_both) {
                cq_kernel_and(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
                              cq_reg_cbits(&f.ctx.regs, ha),
                              cq_reg_cbits(&f.ctx.regs, hb), W);
                cq_ctrl_pop(&f.ctx);
            } else {
                cq_kernel_and(&f.ctx, cq_reg_bits(&f.ctx.regs, hd),
                              cq_reg_cbits(&f.ctx.regs, ha),
                              cq_reg_cbits(&f.ctx.regs, hb), W);
            }

            /* Bennett's controlled() contract is
             * `(ctrl, x, 0) -> (ctrl, x, ctrl ? f(x) : 0)`. Bracketing both
             * calls gives `ctrl·f ^ ctrl·f = 0` on either branch. Bracketing
             * only the forward gives `ctrl·f ^ f`, which is 0 when the control
             * is ON and f when it is OFF. */
            /* NON-VACUITY, and it has to come FIRST: if `a & b` were zero,
             * every one of the four rows would expect 0 and the case would
             * prove nothing at all. */
            uint64_t fv = cq_ref_and(va, vb, W);
            CHECK(fv != 0u);

            uint64_t want = bracket_both ? 0u
                          : (ctrl_on ? 0u : fv);

            CHECK(ux_read(&f.ctx, hd, &got));
            if (got != want)
                cq_h_fail(__FILE__, __LINE__,
                          "ctrl=%d, region brackets %s: dst = 0x%llx, want "
                          "0x%llx", ctrl_on,
                          bracket_both ? "both calls" : "the forward only",
                          (unsigned long long)got, (unsigned long long)want);
            ux_close(&f);
        }
}

/* =========================================================================
 * GROUP 4 — the record. R6's mitigation, checked over the files.
 * ========================================================================= */

/* plan §6's R6 row says "Step 21 pins the two separately". They already are —
 * cq_gold_row carries a `pass` column — and the risk is that a later edit
 * quietly stops pinning one of them, which no kernel suite would notice
 * because cq_gold_close only complains about rows nobody LOOKED at, never
 * about a pass nobody WROTE.
 *
 * Every forward row must therefore have a second-pass sibling at the same
 * (kernel, W). K8's file is the sanctioned exception AND names itself: its
 * second pass is `reverse`, not `unc`, because K8 has no uncompute axis at all
 * (see an_accumulate_is_not_its_own_uncompute above). */
CQ_TEST(every_golden_pins_a_second_pass_beside_its_forward)
{
    static const struct { const char *file; const char *second; } G[] = {
        { "bitwise.counts",     "unc"     }, { "shift_const.counts", "unc" },
        { "cast.counts",        "unc"     }, { "add.counts",         "unc" },
        { "cmp.counts",         "unc"     }, { "mux.counts",         "unc" },
        { "shift_var.counts",   "unc"     }, { "mul.counts",         "unc" },
        { "divrem_u.counts",    "unc"     }, { "divrem_s.counts",    "unc" },
        { "addacc.counts",      "reverse" },
    };
    enum { N_GOLDEN = (int)(sizeof G / sizeof G[0]) };
    int opened = 0, total_forward = 0, total_rows = 0;

    /* THE COUNT IS PART OF THE ASSERTION. A twelfth `.counts` file added by a
     * later kernel step and not added here would be audited by nothing, and a
     * global row floor cannot see that — `total_forward` only ever goes UP.
     * This is the one line that forces a visit to the table. */
    CHECK_EQ(N_GOLDEN, 11);

    for (int i = 0; i < N_GOLDEN; i++) {
        char path[512];
        cq_gold g;
        int file_forward = 0;

        snprintf(path, sizeof path, "%s/%s", CQOPS_GOLDEN_DIR, G[i].file);
        if (!cq_gold_open(&g, path, "step 21 pass-pairing audit",
                          CQOPS_BENNETT_COMMIT))
            continue;
        opened++;

        for (size_t r = 0; r < g.n; r++) {
            g.v[r].visited = 1;              /* read-only pass; see below */
            total_rows++;
            if (strcmp(g.v[r].pass, "forward")) continue;
            file_forward++;

            int paired = 0;
            for (size_t s2 = 0; s2 < g.n; s2++)
                if (g.v[s2].W == g.v[r].W &&
                    !strcmp(g.v[s2].kernel, g.v[r].kernel) &&
                    !strcmp(g.v[s2].pass, G[i].second)) { paired = 1; break; }

            if (!paired)
                cq_h_fail(__FILE__, __LINE__,
                          "%s pins %s/forward W=%d with no `%s` sibling — "
                          "PRD §10 consequence (ii) requires the two passes "
                          "pinned SEPARATELY, and a forward-only row records "
                          "the uncompute as equal by omission",
                          G[i].file, g.v[r].kernel, g.v[r].W, G[i].second);
        }

        /* PER FILE, NOT A GLOBAL FLOOR. The floor this replaced was
         * `total_forward >= 300` against a true 399 — and dropping the largest
         * golden, shift_const's 99 rows, leaves EXACTLY 300, so the guard could
         * not detect the loss of any single file. A per-file assertion names
         * the file that went quiet. */
        if (!file_forward)
            cq_h_fail(__FILE__, __LINE__,
                      "%s contributed no `forward` row to the audit", G[i].file);
        total_forward += file_forward;

        /* THIS SUITE READS GOLDENS AND MUST NEVER WRITE ONE. A golden belongs
         * to the suite that MEASURED it; if test_unc rewrote these files under
         * CQOPS_UPDATE_GOLDENS=1 it would bless rows it never ran a kernel to
         * produce. Clearing `updating` makes cq_gold_close a pure free. */
        g.updating = 0;
        CHECK(cq_gold_close(&g));
    }

    CHECK_EQ(opened, N_GOLDEN);

    /* Every row is one pass of some pair, so the file set is exactly half
     * forward. A missing second-pass row is caught by the pairing loop above;
     * a missing FORWARD row is caught only here. */
    CHECK_EQ(total_rows, total_forward * 2);
}

#include "test_unc_contract.inc"
#include "test_unc_asym.inc"

CQ_TEST_MAIN(
    CQ_CASE(a_kernel_xors_into_dst_rather_than_assigning_to_it),
    CQ_CASE(every_compare_predicate_is_self_inverse_at_a_flag_that_is_already_one),
    CQ_CASE(an_accumulate_is_not_its_own_uncompute),
    CQ_CASE(the_uncompute_is_what_makes_the_free_legal),
    CQ_CASE(an_uncompute_outside_the_forwards_region_does_not_cancel),
    CQ_CASE(every_golden_pins_a_second_pass_beside_its_forward),

    /* tests/test_unc_asym.inc — the R6 asymmetry, MEASURED. */
    CQ_CASE(an_uncompute_after_a_general_ry_on_a_source_emits_more_gates),
    CQ_CASE(the_delta_depends_on_which_lane_drifted_not_only_on_the_kernel),
    CQ_CASE(the_same_drift_without_the_poison_emits_the_identical_stream),
    CQ_CASE(only_a_classical_zero_lane_costs_the_uncompute_anything),
    CQ_CASE(an_uncompute_costs_what_a_forward_at_the_same_representation_costs),
    CQ_CASE(an_all_classical_forward_is_free_and_its_uncompute_is_not),
    CQ_CASE(no_rz_at_any_angle_can_cause_the_drift),
    CQ_CASE(the_all_quantum_mask_is_the_fixed_point_the_goldens_are_pinned_at)
)
