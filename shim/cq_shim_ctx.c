/* shim/cq_shim_ctx.c — M26's foundation. See cq_shim_ctx.h for the contract,
 * for why cq_ctx stays internal, and for why the reset is not optional. */

#include "cq_shim_ctx.h"

#include "cq_shim_record.h"
#include "cq_shim_trace.h"

#include "cq_shim.h"

#include "bit.h"
#include "controlled.h"
#include "reg.h"
#include "qubits.h"
#include "sink.h"
#include "sink_count.h"
#include "sink_printf.h"
#include "sink_qec.h"

#include "cqops/cqops.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* The house shape, one layer per prefix (compare src/sink.c's "sink:" and
 * src/qubits.c's "qubit pool:"), so a red run says WHICH layer refused. */
static void cq_shim_die(const char *what, int32_t h, uint32_t n)
{
    fprintf(stderr, "libcqops: FATAL: shim: %s (h%d, %u)\n", what, h, n);
    abort();
}

/* --- The process context -------------------------------------------------- */

static cq_ctx g_ctx;
static int    g_live;

/* THE ORDER OF THE THREE STATEMENTS IS THE WHOLE OF bd utk. cq_ctx_init passed
 * NULL resolves cq_sink_active() AT CONSTRUCTION, and with no cqops_set_sink
 * override and no CQOPS_SINK that means the name "printf" — which resolved to
 * nothing in any process that had not registered it, and hard-errored with
 * `libcqops: FATAL: sink: no default sink registered (-)`. So the built-ins go
 * in FIRST. The window is one statement wide, not one gate wide.
 *
 * IT INSTALLS PER (RE-)INIT RATHER THAN ONCE PER PROCESS, and that is forced
 * rather than tidy: cq_sink_reset() sets n_registered = 0, so a one-shot
 * install leaves the next context with no "printf" to resolve. Re-installing is
 * free — the registry replaces by name and both instances are function-local
 * statics, so the addresses are stable and the counter is not zeroed by a
 * second call.
 *
 * NEITHER OF THE TWO REJECTED SPELLINGS IS AN OPTION, and bd utk says why in
 * its own words: "Making cq_sink_active() install lazily would defeat
 * tests/test_sink_death.c's no_sink_registered_at_all, whose entire content is
 * that an unresolvable sink is a hard error"; and "a constructor attribute is
 * worse: libcqops is a STATIC library, so an object file that nothing
 * references can be dropped at link time" — measured on this toolchain, where
 * an unreferenced archive member's constructor does not run — "and the
 * registration would vanish silently on some link lines and not others."
 * Registration on first shim call is what is left, and bd utk also fixes the
 * layer: sink_printf and sink_count are Layer 4, so the call belongs in M26
 * (Layer 5) and not in cq_ctx_init, "which would invert the layer order for no
 * gain".
 *
 * The counter's returned instance is deliberately dropped. It is a
 * process-global that cq_sink_counter_register() hands back on every call, so
 * a caller who wants the totals asks for them then; caching one here would be a
 * second name for the same object. */
cq_ctx *cq_shim_ctx(void)
{
    if (!g_live) {
        cq_sink_printf_register();
        (void)cq_sink_counter_register();
        /* M25, Step 26. THE THIRD BUILT-IN, AND THE ONLY ONE THAT NEEDS TWO
         * CALLS — one on each side of the construction, because there is no
         * instant at which both of its preconditions hold. Registration must
         * come first, since cq_ctx_init resolves cq_sink_active() ONCE at
         * construction; the install must come after, because the pool it
         * configures lives inside the context. Neither call is guarded by a
         * build flag: without the QEC library the register is a no-op, so
         * CQOPS_SINK=qec takes src/sink.c's existing "names an unregistered
         * sink" hard error rather than a silent fallback, and the bind has
         * nothing to do. See sink_qec.h. */
        cq_sink_qec_register();
        cq_ctx_init(&g_ctx, NULL);
        cq_sink_qec_bind(&g_ctx.pool);
        /* THE LATCH IS SET BEFORE THE ANNOTATION LAYER IS BOUND, and it has to
         * be: cq_trace_bind reaches back through cq_shim_ctx() -- every
         * cq_trace_* call does, because the register map is read out of THIS
         * context -- so binding it above this line is unbounded recursion. It
         * installs nothing unless the qec sink is bound with a trace open
         * (§15 D21). */
        g_live = 1;
        cq_trace_bind();
    }
    return &g_ctx;
}

/* Disposes and clears the latch, and touches NOTHING about sink SELECTION —
 * see the header. cq_ctx_dispose is what aborts if a §9 region is still open,
 * and it releases no qubits: a rail that was never cqrt_free'd stays counted as
 * live right up to the last statement, which is PRD §10's intended safe leak
 * and not a tidy-up to do here. */
void cq_shim_ctx_reset(void)
{
    /* THE RECORD RESETS WITH THE CONTEXT. D15's certificate is keyed by handle
     * and handles restart at 0 with a fresh table, so a second test case would
     * otherwise inherit the first case's write history under the SAME handle
     * numbers — and would answer about the wrong rail while every width, state
     * and pool check stayed green. Same argument as the sink latch above, one
     * subject over. */
    cq_rec_reset();
    /* And the register map with it, for the same reason one subject over: it is
     * keyed by handle, handles restart at 0, and two cases' index sets under
     * one handle number is an OVERLAPPING #REGISTER rather than a wrong
     * picture. */
    cq_trace_reset();

    if (!g_live) return;
    cq_ctx_dispose(&g_ctx);
    g_live = 0;
}

/* --- D15 §3's residue, read from outside the archive (`bd c55`) ----------- */

/* THE PUBLIC READ. Its contract, and the reason it is six fields at two grains
 * rather than one number, is in include/cqops/cqops.h; what belongs here is why
 * it lives in M26 at all and why it does not touch g_live.
 *
 * IT LIVES HERE BECAUSE THE COUNTERS ARE PER CONTEXT AND THIS FILE OWNS THE ONE
 * CONTEXT. src/reg.c already exports all four, and cqops_set_free_abort sits
 * there because the flag it sets is a file-static of that module. The residue
 * is not: it is state of a `cq_ctx`, and the only cq_ctx a caller outside this
 * repository can ever reach is g_ctx — the frozen ABI has no context parameter,
 * so there is nowhere for a second one to come from (see the header). A
 * cqops_* function in src/ would have to reach UP into shim/ to find it.
 *
 * IT DOES NOT CALL cq_shim_ctx(), AND THAT IS THE WHOLE OF "IT IS A READ".
 * cq_shim_ctx() is a lazy CONSTRUCTOR: it registers the built-in sinks, resolves
 * CQOPS_SINK, builds the pool and binds the annotation layer. Reaching for it
 * here would make asking how much leaked into a call that installs a sink — and
 * `CQOPS_SINK=nonesuch` would turn the question into a hard error, which is the
 * opposite of what a diagnostic read is for. With no context there has been no
 * free, so every field is zero and that is the truth rather than a placeholder.
 *
 * THE POOL TOTAL IS READ FROM THE POOL, NEVER SUMMED FROM THE TWO ROWS ABOVE
 * IT. src/reg.h states that the qubit pair sums to cq_qubits_stranded() as an
 * ASSERTION about the free path — every increment sits beside the strand it
 * describes — and a caller must be able to check it. Computing the field as
 * `dirty + unproven` here would make that check a tautology and hide exactly
 * the drift it exists to catch: a qubit stranded by something that did not go
 * through cq_reg_free. */
void cqops_read_residue(cqops_residue *out)
{
    /* The house shape wants a handle and a number and there is neither here, so
     * the `(h0, 0)` this prints names nothing — the text is what identifies the
     * refusal. Kept in the shape anyway so every shim abort greps alike. */
    if (!out)
        cq_shim_die("cqops_read_residue with nowhere to put the answer", 0, 0);

    out->stranded_dirty    = 0;
    out->stranded_unproven = 0;
    out->frees_dirty       = 0;
    out->frees_unproven    = 0;
    out->stranded_qubits   = 0;
    out->strand_reports    = 0;
    if (!g_live) return;

    out->stranded_dirty    = cq_reg_stranded_dirty(&g_ctx);
    out->stranded_unproven = cq_reg_stranded_unproven(&g_ctx);
    out->frees_dirty       = cq_reg_frees_dirty(&g_ctx);
    out->frees_unproven    = cq_reg_frees_unproven(&g_ctx);
    out->stranded_qubits   = cq_qubits_stranded(&g_ctx.pool);
    out->strand_reports    = cq_reg_strand_reports(&g_ctx);
}

/* --- The one PRD §9 region bracket in the shim (bd d6m fix (a)) ----------- */

/* FIVE THINGS THIS COMMENT SAYS THAT NOTHING IN src/ SAYS, because bd d6m's
 * preferred fix is a comment and a test rather than a mechanism.
 *
 * 1. cq_ctrl_push COPIES the control bit into the frame — deliberately, so the
 *    region is controlled by the value that bit had at entry and does not
 *    silently follow a later materialisation. But a CQ_BIT_Q copy carries a
 *    qubit INDEX, and indices are recycled through the LIFO free list (D4)
 *    while handles are monotonic and never reused (D5).
 *
 * 2. THEREFORE NO DEALLOCATION OF ANY KIND MAY HAPPEN BETWEEN THE PUSH AND THE
 *    POP — not cqrt_free, not landing 2's three-valued free, not a scratch
 *    release. Free the flag rail inside the region and every promoted gate for
 *    the rest of it controls off a wire whoever allocates next now owns.
 *
 * 3. THE FAILURE IS SILENT IN BOTH CONFIGURATIONS, and the THREE guards that
 *    look as though they would catch it each miss it for a DIFFERENT reason —
 *    which is why no one of them can be strengthened into a fix.
 *      - M07's cq_reg_check_operands compares HANDLES and liveness and never
 *        sees a qubit index at all (src/reg_check.c), so it is structurally
 *        blind to a wire; and the control rail is not among the operands it is
 *        handed anyway.
 *      - M06's coincidence refusal compares the wire's INDEX against the gate's
 *        operands, and a recycled index is distinct from them, so it cannot
 *        fire.
 *      - The Debug I2 sweep DOES look for "a register holding an index that is
 *        on the pool's free list" (reg.h) — the laundering signature — and is
 *        blind because the wire belongs to NO register: it is a copy inside a
 *        control frame, and the rail that held it is a tombstone by then.
 *    No L2 set check names it either, for the same reason. Measured: the region
 *    returns, the pop returns, exit 0, no diagnostic in either configuration.
 *    tests/test_shim_ctx.c:a_region_outliving_its_control_rail_is_caught_by_nothing
 *    pins exactly that, as a CHARACTERISATION — it inverts into a death test
 *    the day option (b) or (c) below is taken.
 *
 *    That is the DEPTH-1 consequence, which is the whole of the shim's exposure
 *    because this function refuses to open a nested region — see the refusal
 *    below for what depth >= 2 would cost instead.
 *
 * 4. ONE KERNEL CALL WIDE IS FREE RATHER THAN A RESTRICTION. CQ_lang's ABI
 *    prepends exactly one control-flag handle per template call and has no
 *    push/pop bracket at all, so a region is one call wide whatever we do;
 *    tests/test_gen_bodies.py's one_call() pins each generated controlled body
 *    at exactly one statement and names this bead while doing it. Taking `body`
 *    as a callback is what makes that structural here rather than a convention
 *    every future entry point has to remember.
 *
 * 5. THE ESCALATION PATH IS DELIBERATELY NOT TAKEN. (b) M07 refusing
 *    cq_reg_free while ctx->ctrl.n != 0 is one comparison but puts M06
 *    knowledge in M07; (c) M06 registering the wire with the pool so a release
 *    of it aborts needs an owner notion M03 does not have and says it does not
 *    have. Do not reach for (c) without re-reading why.
 *
 * THE WIDTH GUARD IS SEPARATE FROM ALL FIVE, and it is the only rail guard here:
 * a MEASURED rail is deliberately NOT refused. `cq_reg_cbits` admits one on
 * purpose (reg.h: "A MEASURED rail is readable through cq_reg_cbits and is
 * refused by cq_reg_bits"), a control is a READ, and every Rule-7 kernel and
 * `cq_reg_xor_into` already read measured rails without a diagnostic — so
 * singling out the bracket would be an inconsistency rather than a guard.
 * Measured on the 243 goldens: 86 `cqrt_measure_*` calls and 0 measured handles
 * referenced afterwards in any capacity, which is a stronger fence than the
 * width case has.
 *
 * ONE BIT: measured over all 243 e2e goldens of CQ_lang at 02afdfe, every one
 * of the 4,944 `cqrt_*_controlled` calls — ALL symbols, not just the `copy`
 * family, which is why this is not src/controlled.h's 4,918: that figure is
 * copy-only at the 239-golden corpus and is exact for what it names — carries a
 * flag handle minted by
 * `cqrt_alloc_i1` (4,096) or returned by an `icmp` (532) or `fcmp` (316)
 * template. AND THAT MEASUREMENT IS OF THE `cqrt_*` FAMILY, NOT OF THE ONE
 * THESE WRAPPERS WILL USE: the corpus contains ZERO `cq_template_*_controlled`
 * calls, so for the 214-symbol integer controlled grid the claim rests on the
 * ABI's own declaration rather than on the corpus —
 * `third_party/cq_lang/opcode_table.yaml:222-223` gives both `icmp` and `fcmp`
 * `result_type: flag_handle`, and `ControlledSymbols.h` states the one-flag
 * invariant. Both figures are corpus measurements and the corpus is NOT pinned
 * by anything in this repo (PRD §15 D15 §0): read them as a ratio and a date,
 * never as an invariant. Without the guard the region would silently READ THE
 * LSB of a wider rail — which is not "promote", and the difference matters: a
 * classical LSB would make it row 0's SKIP and emit nothing at all. */
void cq_shim_region(int32_t ctrl_flag, void (*body)(void *), void *arg)
{
    cq_ctx *ctx = cq_shim_ctx();

    if (!body)
        cq_shim_die("cq_shim_region with no body: a region wraps exactly one "
                    "kernel call and there is nothing to wrap", ctrl_flag, 0u);

    /* A CHECKED PREMISE, ON cq_sandwich's PRECEDENT, NOT A FIX FOR A LIVE
     * DEFECT. M09 refuses a nested sandwich in both configurations because
     * "no nesting" is one of the driver's own premises even though no caller
     * can violate it; this is the same move. Measured: cq_shim_region has zero
     * non-test callers, every generated `_controlled` body is exactly one call,
     * and none of the ten *.gen.c names this function — so only a future M26
     * file could nest, and this is what would catch it.
     *
     * WHAT IT COSTS IF IT EVER HAPPENS, measured rather than argued. At depth
     * >= 2 a push mints an AND flag and computes `w' = wA & wB` with one
     * unpromoted Toffoli, and the pop re-emits that Toffoli and releases w'
     * with the literal CQ_ZERO_BY_CTRL_UNCOMPUTE. Free wA's rail inside the
     * region and let its index be recycled and driven, and the uncompute does
     * not cancel: on a determinate wire the shadow catches it
     * ("retire of a determinate NON-ZERO entry"), and on a POISONED one the
     * process exits 0 having put a dirty index on the free list. M06 already
     * catches the general route — check_operands walks the whole stack
     * including and_a/and_b and its message names this exact consequence — so
     * the silent residue is one narrow sub-route: cq_materialise's BIRTH `X`
     * on a constant-ONE rail, which bypasses cq_emit_x by `bd skh` / D13. */
    if (cq_ctrl_depth(ctx) != 0)
        cq_shim_die("cq_shim_region inside an open region: the shim never "
                    "nests, because the ABI prepends one flag per call and ANDs "
                    "multi-condition control in the IR pass", ctrl_flag,
                    (uint32_t)cq_ctrl_depth(ctx));

    /* RESOLVE THROUGH cq_reg_cbits FIRST, and the order is the diagnostic.
     * cq_reg_width goes through cq_reg_slot, which admits a TOMBSTONE — reg.c
     * says so in as many words ("width survives the tombstone, so a
     * use-after-free diagnostic can name it") — so checking the width first
     * would report a freed 8-bit rail as a width bug. cq_reg_cbits goes through
     * cq_reg_readable, which refuses the tombstone and names it. CQ_REG_NONE
     * lands here too, which is right: "no region at all" is the absence of the
     * _controlled symbol, not a value of this parameter, and the shim must not
     * invent a sentinel row the ABI does not have. */
    const cq_bit *flag = cq_reg_cbits(&ctx->regs, ctrl_flag);

    const uint32_t w = cq_reg_width(&ctx->regs, ctrl_flag);
    if (w != 1u)
        cq_shim_die("the control flag rail is not one bit wide; the ABI "
                    "prepends one flag handle per controlled call and every "
                    "flag CQ_lang emits is i1", ctrl_flag, w);

    cq_ctrl_push(ctx, &flag[0]);

    body(arg);

    /* A DEPTH COMPARISON, AND A CONTENT COMPARISON WOULD BE WORSE RATHER THAN
     * BETTER. This is a count, and this project's rule is that a count is not
     * an identification — a body that popped this frame and pushed one of its
     * own leaves the depth at 1 and passes. Comparing the frame's CONTENTS
     * instead does not close the class and was measured not to: a body doing
     * `pop; emit; push(the SAME flag)` is byte-identical in mode and wire, so
     * it passes a content check while having emitted an UNCONTROLLED gate
     * inside a region the caller asked to be promoted — a strictly worse
     * miscompile waved through by the "stronger" guard. cq_ctrl_frame carries
     * no serial or epoch, so no content comparison can identify a frame at all.
     *
     * What actually forbids that class is one layer up and is structural:
     * tests/test_shim_ctx.c's third d6m arm asserts that the set of files under
     * shim/ CALLING cq_ctrl_push or cq_ctrl_pop is exactly this one, so no body
     * can pop. What is left for this check is the plausible slip — a body that
     * pushed and forgot to pop — which it catches, and which would otherwise
     * surface one layer away at cq_ctx_dispose. */
    if (cq_ctrl_depth(ctx) != 1)
        cq_shim_die("the region body left the control stack unbalanced",
                    ctrl_flag, (uint32_t)cq_ctrl_depth(ctx));

    cq_ctrl_pop(ctx);
}

/* --- PRD §1's v1 boundary ------------------------------------------------- */

/* THE MESSAGE IS THE CONTRACT AND ITS PREFIX IS DELIBERATELY NOT THE HOUSE ONE.
 * Every other hard error in libcqops prints `libcqops: FATAL: <layer>: ...`;
 * PRD §1 fixes this one verbatim as
 *
 *     cqops: cq_template_sitofp_i32_to_f64 not implemented (fp is v2)
 *
 * and tests/test_gen_bodies.py has been pinning those bytes since Step 22 —
 * against a stub it writes for itself, because M26 did not exist. This is the
 * definition that stub stood in for, and tests/test_shim_ctx.c forks to read
 * its stderr so the two are pinned to the same string rather than to each
 * other. The difference in prefix is also what lets the death case assert
 * negatively that no OTHER layer spoke.
 *
 * `reason` is the CALLER's string and is not validated. Two buckets reach here
 * from the generated bodies today — PRD §1's "fp is v2" (884 symbols) and
 * D14's `_inv` (603) — and bd vxk and bd ck6 are expected to add a third from
 * cq_runtime_v2.c for the qram / alloc_handle refusals (tape is v1.1, D23), so nothing here
 * may be written against a fixed set of two.
 *
 * THE NULL SUBSTITUTES FOLLOW src/sink.c's PRECEDENT — cq_sink_die writes "-"
 * for a missing detail — rather than inventing a louder placeholder here.
 * Nothing in the tree passes NULL, since all 1,487 generated abort bodies pass
 * string literals, so this is exactly the defensive branch this project
 * distrusts; it is exercised by tests/test_shim_ctx_region.inc rather than left
 * as code nobody has seen run.
 *
 * No fflush(stdout), and that is a decision rather than an omission: M23
 * flushes per line so our own trace is never buffered, src/sink.c's cq_sink_die
 * is the house precedent for a hard error and does not flush either, and the
 * stub these bytes are pinned against does not. */
_Noreturn void cq_shim_unsupported(const char *symbol, const char *reason)
{
    fprintf(stderr, "cqops: %s not implemented (%s)\n",
            symbol ? symbol : "?", reason ? reason : "?");
    abort();
}
