/* shim/cq_runtime_rail.c — M26's rail surface, Step 23 landing 1 step 4. The 32
 * `cqrt_*` symbols that create a rail, read it, destroy it, and write to it.
 *
 * Everything it defines is declared in shim/cq_runtime_abi.h, which is a
 * VERIFIED copy of CQ_lang's own header. That include is not decoration: a
 * `cqrt_*` definition needs no prior declaration, `-Wmissing-prototypes` is not
 * in this project's flag set, and C has no name mangling — so without it a
 * wrong return type or a wrong parameter type would compile clean, link clean,
 * and be discovered at Step 24 as a wrong answer.
 *
 * RULE 12. Budget 220. Split seam, RECORDED HERE BEFORE THE FILE WAS WRITTEN,
 * because Rule 12 names M26 as one of two gaps to close "before they are
 * written" and because a seam first reached for at the limit is exactly the
 * surprise refactor the rule forbids:
 *
 *     the rail's LIFETIME <-> the rail's CONTENTS   ->  shim/cq_runtime_write.c
 *
 * The CONTENTS half is `copy` (5), `copy_controlled` (5), `cswap`, `addc` (5)
 * and `xorc` (5) — 21 symbols; the LIFETIME half is `alloc` (5), `measure` (5)
 * and `free` — 11.
 *
 * IT IS A SUBJECT CUT ON WHOSE RAIL IS AT STAKE, and THREE candidate
 * discriminators have now been tried and refuted against this very file. The
 * first two were `rail_addc`'s: it mints two rails, tombstones both, and reads
 * the free-time disposition twice, so the cut is not "only the lifetime half
 * touches a slot state" and not "the contents half never reads the
 * disposition". The third was their replacement — "the caller's handle space is
 * exactly as they found it afterwards" — and it is FALSE, measured in Release:
 * an `addc` on a rail that already owns qubits advances the D5 counter by
 * exactly 2 and leaves two tombstones in the table for good, so the next
 * `cqrt_alloc_*` returns h4 where it would have returned h2. Nothing anywhere
 * reclaims a slot. "Never observable as a lasting object" was wrong twice over:
 * the slots last, and so does the shift in every later handle.
 *
 * WHAT IS TRUE IS NARROWER, AND IS THE HALF THE SUITE ALREADY PINS. The two
 * rails are DEAD before the call returns and no `cqrt_*` symbol can ever name
 * them; their qubits are back on the free list (pool `live` restored, `minted`
 * and `peak` up by W+1 exactly as for any use of a qubit); and `cq_reg_audit`
 * skips a tombstone, so I2's verdict does not move. The NUMBERING does move,
 * and nothing in libcqops prints a handle — a sink is handed a raw index and
 * emits `q<N>` — so the shift is invisible to a gate trace and visible only to
 * an oracle over handle numbers. Step 24's oracle is `bd 590` and is OPEN;
 * whoever settles it inherits this sentence.
 *
 * SO THE DISCRIMINATOR THAT CUTS IS EMISSION, AND IT IS A DISJOINTNESS RATHER
 * THAN A CONTAINMENT. The lifetime half emits `mz` and nothing else — alloc is
 * all-constant by I4, free releases or strands without emitting, measure emits
 * one `mz` per qubit and none per constant — and the contents half emits
 * nothing BUT X/CX/CCX. Measured with the counter sink over 18 rows in Release:
 * {mz} on one side, {x, cx, ccx} on the other, empty intersection. "Emits
 * nothing but" is the right form and not a hedge, because seven of those rows
 * emit NOTHING at all — addc's classical fold and its `imm == 0` row, cswap's
 * two constant rows, xorc and copy on all-classical rails, and a ZERO-controlled
 * copy — where "emits X/CX/CCX" would be false. The claim is scoped to THIS
 * file's 32 symbols: `cq_runtime_gate.c` emits `ry`/`rz`/`mz` and generalising
 * it one file over is immediately false.
 *
 * AND THE LIFETIME HALF KEEPS ITS SUBJECT: creating, retiring and destroying a
 * handle CQ_lang NAMED, with D15's three-valued verdict, the strand, the
 * `stderr` report and `CQOPS_FREE_ABORT`. `rail_addc` reaches the same free path
 * for two handles CQ_lang never saw, which is exactly why the third
 * `proven_zero` constant lands on the CONTENTS side of the cut and not this one.
 *
 * IF THE CUT IS EVER TAKEN, `rail_r` IS NEEDED ON BOTH SIDES (`rail_measure` in
 * the lifetime half, `rail_copy` in the contents half) and drags `cq_rail_die`
 * and `RAIL_WIDTH` with it. Two copies printing the SAME string is the recorded
 * masking-layer trap, and tests/CMakeLists.txt's pins discriminate on the
 * MESSAGE and not the module — so make the two messages disjoint at the split,
 * exactly as Step 15 did for `cq_addacc_check`.
 *
 * TRIGGER 240, THE HOUSE THRESHOLD (M06, M07, M20, M22, M27), AND THE
 * DERIVATION IS CORRECTED HERE RATHER THAN LEFT AS FIRST DRAFTED. The trigger
 * is measured against Rule 12's 300-line HARD LIMIT and not against the file's
 * budget, which is why four of those five sit ABOVE their own budgets — M07's
 * is 240 against a budget of 180, and it landed at 283 and took the seam as
 * scheduled at Step 23. (M27 is the fifth and its 240 sits BELOW its 280, so
 * "always above" would be false; and M16, which an earlier draft of this list
 * named, has no recorded trigger at all — its seam is "available, at zero
 * headroom".) `shim/cq_shim_ctx.c`'s 120 is the tree's only RATIO-derived
 * trigger, and it is the one this file's first draft copied: 0.8 x this budget
 * is 176, which this file passed the day it landed, and a trigger that fires on
 * landing day is a two-file plan wearing a trigger's clothes rather than a
 * scheduled split. The plan also records 260 for `goldens.c` and `src/reg_free.c`
 * and 290 for `tests/test_rotate_poison.inc`, so 240 is the house figure and not
 * a universal one. A budget is an estimate; the 300 is the wall. As of
 * 2026-08-23 this file counts 199 of that 240.
 *
 * EVERY WIDTH-SUFFIXED SYMBOL CHECKS THAT THE RAIL IS THAT WIDTH, in both
 * configurations, and the check is this file's own rather than any document's.
 * The ABI's width token is a claim about the rail, and nothing below enforces
 * it: `cqrt_measure_i8` on a 32-bit rail would return the low eight bits and
 * mark the whole rail terminal, and `cqrt_addc_i8` on one would add mod 2^8
 * into 32 lanes. Measured over the 247 goldens at CQ_lang `3319975`, 2026-08-23:
 * across every width-suffixed rail family the token matches the rail in every
 * case on v1's integer surface, with the only 18 exceptions all f80-vs-i32
 * inside `slice_libm_real_coshl`, which dies at its first fp call and which
 * `cq_reg_xor_into`'s own width guard would refuse anyway.
 *
 * EVERY CORPUS FIGURE IN THIS FILE CARRIES A COUNT, A DATE AND A CQ_lang SHA,
 * and none of the three is optional. Nothing in this repository pins that
 * corpus and it moved TWICE while this file was being written: it was 243
 * goldens at `02afdfe` when `shim/cq_shim_ctx.c` measured it one step earlier,
 * reached 247 as four UNTRACKED working-tree files — a state no SHA names at
 * all, and one an adversarial reviewer caught mid-review — and was committed to
 * 247 at `3319975` the same afternoon. So `shim/cq_shim_ctx.c`'s "243" and this
 * file's "247" are both honest and they are different corpora. Re-measure before
 * quoting either.
 */

#include "cq_runtime_abi.h"

#include "cq_shim_ctx.h"
#include <string.h>

#include "cq_shim_proof.h"
#include "cq_shim_record.h"
#include "cq_shim_trace.h"

#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "kernels/addacc.h"
#include "reg.h"
#include "rotate.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* A SECOND static WITH THE SAME PREFIX, on src/reg_check.c's precedent: the
 * FAIL_REGULAR_EXPRESSION pins in tests/CMakeLists.txt discriminate on the
 * MESSAGE and not on the module, so a per-translation-unit copy keeps them
 * working and costs less than a private header for one five-line function. */
static void cq_rail_die(const char *what, int32_t h, uint32_t n)
{
    fprintf(stderr, "libcqops: FATAL: shim: %s (h%d, %u)\n", what, h, n);
    abort();
}

static const char *const RAIL_WIDTH =
    "the rail is not the width its cqrt_* symbol names";

static uint64_t width_mask(uint32_t w)
{
    return (w >= 64u) ? ~UINT64_C(0) : ((UINT64_C(1) << w) - 1u);
}

/* A rail this symbol may WRITE. `cq_reg_bits` refuses a tombstone AND a
 * measured rail — the mutable accessor is where measurement's terminality is
 * enforced — and it runs FIRST so that a use-after-free is M07's diagnostic
 * rather than ours: `cq_reg_width` goes through `cq_reg_slot`, which admits a
 * tombstone on purpose so a use-after-free message can name its width. */
static cq_bit *rail_w(cq_ctx *ctx, int32_t h, uint32_t w)
{
    cq_bit *b = cq_reg_bits(&ctx->regs, h);
    const uint32_t got = cq_reg_width(&ctx->regs, h);

    if (got != w) cq_rail_die(RAIL_WIDTH, h, got);
    return b;
}

/* A rail this symbol only READS, and the asymmetry is deliberate:
 * `cq_reg_cbits` admits a MEASURED rail because a read is legal on one
 * (src/reg.h), which is the same position shim/cq_shim_ctx.c takes for a
 * control flag. Where a measured rail must still be refused — a second
 * `cqrt_measure` — the refusal is M07's, inside `cq_reg_mark_measured`. */
static const cq_bit *rail_r(cq_ctx *ctx, int32_t h, uint32_t w)
{
    const cq_bit *b = cq_reg_cbits(&ctx->regs, h);
    const uint32_t got = cq_reg_width(&ctx->regs, h);

    if (got != w) cq_rail_die(RAIL_WIDTH, h, got);
    return b;
}

/* --- alloc: the mint, and I4 -------------------------------------------- */

/* `(uint64_t)(uintW_t)value` AND NOT `(uint64_t)value`. The ABI hands us a
 * SIGNED literal and a register is a bit pattern, so the conversion has to
 * reinterpret at the symbol's own width first: the naive spelling
 * sign-extends — measured, int8_t -56 becomes 0xFFFFFFFFFFFFFFC8 — and is
 * harmless only because `cq_bits_from_words` reads exactly `width` bits. That
 * is a load-bearing accident, so the mask is explicit.
 *
 * AND NO COMPILER BACKS IT UP, which is the half a first draft of this comment
 * got wrong and is worth saying plainly. An EXPLICIT cast to uint64_t silences
 * -Wsign-conversion outright, so `(uint64_t)value` compiles clean under this
 * project's exact flags — measured. Only the CAST-FREE spelling warns (that is
 * what bd 216's checklist item 8 is about, and its attribution of
 * -Wimplicit-int-conversion at i8/i16 is wrong for this direction: all four
 * integer widths give -Wsign-conversion). The inner mask is therefore defended
 * by nothing but this paragraph and the case that pins the resulting bits, and
 * it is unobservable at every width the ABI can name, because cq_bits_from_words
 * reads exactly `width` bits and sign extension only touches bits at or above
 * it. It stays because the accident is one width-argument slip away from
 * mattering, not because anything would catch its removal. */
/* --- D15's certificate: RECORDING the call stream ------------------------ */

/* ONE RECORD PER ENTRY POINT, NEVER ONE PER GATE (Rule 13). These calls are the
 * only thing landing 2 adds to this file, and they are placed at the ABI
 * boundary rather than beside the emissions on purpose: a W-bit `cqrt_cswap` is
 * ONE call and 3W gates, and D15 §2 says the evidence is the CALL STREAM.
 *
 * THE §9 CONTROL CONTEXT IS RECORDED WITH EVERY CALL (D15 §6(i)). An
 * uncontrolled `_unc` after a CONTROLLED forward leaves `dst` at `ctrl · f`, not
 * zero, so a pair whose halves ran under different regions is NOT a pair. There
 * is no corpus witness — the goldens contain zero `cq_template_*_controlled`
 * calls — so the guard is for callers, not for CQ_lang, and it is exactly the
 * kind of guard that is free to add now and expensive to retrofit.
 *
 * THE FLAG HANDLE IS AVAILABLE AT EXACTLY ONE PLACE IN THE SHIM and this is not
 * it: `cq_shim_region` copies the control BIT into its frame and the handle is
 * never stored, so inside a region there is no way to recover which rail the
 * region is controlled by. Every recorded call therefore takes its `ctrl` from
 * the entry point's OWN parameter, which is the only place it exists. */
static void rec(cq_rop op, int32_t h0, int32_t h1, int32_t h2,
                uint64_t imm, double angle, int32_t ctrl)
{
    cq_call_rec c;
    memset(&c, 0, sizeof c);
    c.op   = (uint16_t)op;
    c.h[0] = h0; c.h[1] = h1; c.h[2] = h2;
    c.imm  = imm;
    c.ctrl = ctrl;
    /* THE ANGLE IS CARRIED AS ITS BIT PATTERN, not as a double, because angles
     * compare BITWISE everywhere in this project (mock_sink.h) and because the
     * adjoint test is "the sign bit flipped and nothing else". A `==` would
     * pair 0.0 with -0.0 and would pair a NaN with nothing in a way that
     * depends on the comparison's direction. */
    memcpy(&c.angle, &angle, sizeof c.angle);
    cq_rec_push(&c);
}

/* EVERY BRACKET IN THIS FILE GOES THROUGH THIS MACRO, and it exists to make the
 * arity uniform rather than to save typing: `cq_trace_op` takes three read slots
 * and two written ones because `cqrt_cswap` needs exactly that, and every other
 * symbol here writes ONE rail. Spelling the unused fifth slot at 30 call sites
 * is how a transposed argument gets in. */
#define CQ_TRACE_OP(name, i0, i1, i2, o0) \
    cq_trace_op((name), (i0), (i1), (i2), (o0), CQ_REG_NONE)
#define CQ_TRACE_OP2 cq_trace_op   /* cqrt_cswap alone writes two rails */

/* THIS IS THE ONE ENTRY POINT THAT OPENS NO ANNOTATION BRACKET, and the
 * exemption is STATIC rather than a runtime "did it emit?" test — which is what
 * keeps cq_shim_trace.h's coverage rule checkable. By I4 a rail whose bits are
 * all constants owns zero qubits, and `cq_reg_alloc_const` takes the TABLE and
 * not the context (reg.h), so `cqrt_alloc_i<W>` structurally cannot allocate a
 * qubit or reach a `qec_*` call — at ANY value and ANY width. Contrast
 * `cqrt_addc`, which emits nothing on an all-classical rail and `6W-5` gates on
 * a poisoned one: that one is contingent and must stay bracketed.
 *
 * IT USED TO BRACKET (Step 26 / bd 76r) AND THE BRACKET WAS CONFORMANT — handoff
 * §6 rule 1 wants even a zero-round `qec_idle(q, 0)` inside one. It was dropped
 * 2026-08-28 on the user's call because an empty OP unit is noise in the
 * algorithm view: it carries no gate, and everything it said is already in the
 * `#REGISTER` header.
 *
 * NOTHING IS LOST FROM THE HEADER. `cq_trace_op` snapshots its named handles, so
 * the question is whether a rail can reach the header only through its alloc —
 * and it cannot: the snapshot taken here was necessarily EMPTY (I4), and a rail
 * that later owns a lane is named by whatever op materialised it. A rail named
 * by nothing else stays all-constant, and an all-constant rail gets no
 * `#REGISTER` line either way. */
#define CQ_RAIL_ALLOC(W, CT, UT)                                              \
    int32_t cqrt_alloc_i##W(CT value)                                         \
    {                                                                         \
        int32_t h = cq_reg_alloc_const(&cq_shim_ctx()->regs, W##u,            \
                                       (uint64_t)(UT)value, 0u);              \
        cq_rec_mint(h, W##u, (uint64_t)(UT)value, 0u, 0);                     \
        return h;                                                             \
    }

CQ_RAIL_ALLOC(1,  bool,    bool)
CQ_RAIL_ALLOC(8,  int8_t,  uint8_t)
CQ_RAIL_ALLOC(16, int16_t, uint16_t)
CQ_RAIL_ALLOC(32, int32_t, uint32_t)
CQ_RAIL_ALLOC(64, int64_t, uint64_t)

/* --- measure: terminal, per PRD §7 --------------------------------------- */

/* M22 already does all three jobs — it marks the rail MEASURED first (so a
 * second measure aborts having emitted zero gates), emits one `mz` per QUBIT
 * and none per constant, and returns the value in two words. All this layer
 * adds is the width check and the ABI's signed reading of the pattern.
 *
 * `hi` is never read and that is a property of the ABI rather than an
 * oversight: there is no `cqrt_measure` above i64, so the high word is always
 * zero here. The two-word signature exists because a register reaches 128 bits
 * through the template surface. */
static uint64_t rail_measure(int32_t h, uint32_t w)
{
    cq_ctx *ctx = cq_shim_ctx();
    uint64_t lo = 0u, hi = 0u;

    (void)rail_r(ctx, h, w);
    cq_measure(ctx, h, &lo, &hi);
    return lo;
}

/* `(RT)(UT)lo` — reinterpret at the width, then let the return type impose the
 * ABI's signed reading. `cqrt_measure_i8` returns int8_t, which cannot hold a
 * sign extension; CQ_lang's header settles the intent ("Width-only ABI: the
 * runtime sees widths, not signedness ... signedness is implicit in the LLVM
 * opcode at the call site") and its trace stub cannot, because every measure
 * body there returns a literal 0. */
#define CQ_RAIL_MEASURE(W, RT, UT)                                            \
    RT cqrt_measure_i##W(int32_t handle)                                      \
    {                                                                         \
        uint64_t v;                                                           \
        CQ_TRACE_OP("measure", handle, CQ_REG_NONE, CQ_REG_NONE, handle);     \
        v = rail_measure(handle, W##u);                                       \
        cq_trace_end();                                                       \
        return (RT)(UT)v;                                                     \
    }

/* i1 is written out rather than macro'd: a conversion to _Bool is a boolean
 * TEST and not a narrowing, so `(bool)(uint8_t)lo` would be true for any
 * non-zero low byte and would say nothing about which bit was read. */
bool cqrt_measure_i1(int32_t handle)
{
    uint64_t v;

    CQ_TRACE_OP("measure", handle, CQ_REG_NONE, CQ_REG_NONE, handle);
    v = rail_measure(handle, 1u);
    cq_trace_end();
    return (v & 1u) != 0u;
}

CQ_RAIL_MEASURE(8,  int8_t,  uint8_t)
CQ_RAIL_MEASURE(16, int16_t, uint16_t)
CQ_RAIL_MEASURE(32, int32_t, uint32_t)
CQ_RAIL_MEASURE(64, int64_t, uint64_t)

/* --- free: D15's three-valued verdict, two-valued act -------------------- */

/* THE EVIDENCE IS `cq_shim_shadow_proof` AND NEVER NULL. A NULL proof is a
 * MISSING ARGUMENT rather than an epistemic state and M07 hard-errors on it for
 * any rail that owns a qubit, so there is no third option — see
 * shim/cq_shim_proof.h for why the supplier lives at M26 and why the two
 * shipped sentences forbidding it in `src/` are honoured rather than overridden.
 *
 * NO WIDTH CHECK, because `cqrt_free` carries no width token: the ABI frees a
 * handle, not a typed rail. Everything else about the disposition is M07's —
 * proven-clean releases, proven-dirty and unproven alike STRAND. */
void cqrt_free(int32_t handle)
{
    /* THE ONLY BRACKET WHOSE OPEN IS LOAD-BEARING FOR THE HEADER RATHER THAN FOR
     * COVERAGE. `cq_trace_op` snapshots its named handles at BOTH ends, and this
     * rail is a tombstone by the time the close runs — so the open is where its
     * final index set is taken. The `mz`-free path emits no gate here, but the
     * bracket is unconditional for the reason cq_shim_trace.h gives: "bracket
     * the ones that emit" is not a checkable rule. */
    CQ_TRACE_OP("free", handle, CQ_REG_NONE, CQ_REG_NONE, CQ_REG_NONE);

    /* THE EVIDENCE IS NOW THE CERTIFICATE AND THE SHADOW TOGETHER (landing 2).
     * Both are sound in all three rows and differ only in COMPLETENESS, so the
     * combination is a strict improvement on either — see cq_shim_proof.h for
     * why DIRTY dominates rather than first-non-zero-wins. */
    cq_reg_free(cq_shim_ctx(), handle, cq_shim_free_proof);

    /* AFTER the free, never before: cq_reg_free consults the proof TWICE per
     * qubit (src/reg.h requires purity), so retiring the history first would
     * make the second consult answer UNPROVEN on a rail the first cleared —
     * a half-released rail, silently. */
    cq_rec_retire(handle);
    cq_trace_end();
}

/* --- copy: the ABI's order is (src, dst) and ours is (dst, src) ----------- */

/* THE ARGUMENT ORDER IS INVERTED AND BOTH SIDES ARE int32_t, so nothing a
 * compiler or a linker can do will catch a transposition here — C ignores
 * parameter names. tests/test_runtime_rail_write.inc pins it behaviourally, by
 * asserting which rail gained the wires.
 *
 * AND IT REALLY IS AN XOR. The ABI comment says "Precondition: dst is a fresh
 * |0> register" and CQ_lang's own corpus contradicts it: measured over the 247
 * goldens at CQ_lang `3319975`, 565 of 1,024 integer `cqrt_copy_*_controlled`
 * calls target a destination that has already been written, and no
 * controlled-copy destination in the corpus is written only once. So `assert dst is all
 * CQ_BIT_ZERO` is not a defensible guard to add later — it would refuse the
 * majority of the corpus — and the honest statement is that the precondition is
 * a runtime property libcqops cannot see. */
static void rail_copy(uint32_t w, int32_t src, int32_t dst)
{
    cq_ctx *ctx = cq_shim_ctx();

    (void)rail_r(ctx, src, w);
    (void)rail_w(ctx, dst, w);
    cq_reg_xor_into(ctx, dst, src);
}

/* THE BRACKET IS IN THE MACRO AND NOT IN `rail_copy`, AND THAT IS THE RULE THIS
 * FILE FOLLOWS EVERYWHERE: a bracket belongs to the OUTERMOST entry point.
 * `rail_copy` is also the controlled family's body, one region deeper, so a
 * bracket there would nest inside the one below and hard-error. */
#define CQ_RAIL_COPY(W)                                                       \
    void cqrt_copy_i##W(int32_t src, int32_t dst)                             \
    {                                                                         \
        CQ_TRACE_OP("copy", src, CQ_REG_NONE, CQ_REG_NONE, dst);              \
        rail_copy(W##u, src, dst);                                            \
        cq_trace_end();                                                       \
        rec(CQ_ROP_COPY, src, dst, CQ_REG_NONE, 0u, 0.0, CQ_REG_NONE);        \
    }

CQ_RAIL_COPY(1)
CQ_RAIL_COPY(8)
CQ_RAIL_COPY(16)
CQ_RAIL_COPY(32)
CQ_RAIL_COPY(64)

/* THE REGION IS OPENED BY THE ONE BRACKET IN THE SHIM AND TAKES THE BODY AS A
 * CALLBACK, which is what makes "a region is exactly one kernel call wide"
 * structural rather than a convention each entry point has to remember. The
 * bracket owns row 0, the nesting refusal, the one-bit flag guard and the
 * balance check; nothing here re-guards them. */
typedef struct { uint32_t w; int32_t src, dst; } rail_copy_args;

static void rail_copy_body(void *p)
{
    const rail_copy_args *a = (const rail_copy_args *)p;
    rail_copy(a->w, a->src, a->dst);
}

#define CQ_RAIL_COPY_CTRL(W)                                                  \
    void cqrt_copy_i##W##_controlled(int32_t ctrl, int32_t src, int32_t dst)  \
    {                                                                         \
        rail_copy_args a = { W##u, src, dst };                                \
        CQ_TRACE_OP("copy_ctrl", ctrl, src, CQ_REG_NONE, dst);                \
        cq_shim_region(ctrl, rail_copy_body, &a);                             \
        cq_trace_end();                                                       \
        rec(CQ_ROP_COPY_CTRL, ctrl, src, dst, 0u, 0.0, ctrl);                 \
    }

CQ_RAIL_COPY_CTRL(1)
CQ_RAIL_COPY_CTRL(8)
CQ_RAIL_COPY_CTRL(16)
CQ_RAIL_COPY_CTRL(32)
CQ_RAIL_COPY_CTRL(64)

/* --- cswap: where the COST is the specification -------------------------- */

/* PRD §2.1 PRICES A CONSTANT CONTROL AT ZERO GATES, AND THAT IS WHY THE BRANCH
 * ON THE FLAG'S KIND IS HERE RATHER THAN IN THE EMITTER. There is no gate whose
 * meaning is "exchange two rails" — the constant row is a table relabelling —
 * and the Fredkin's outer two CXs carry NO control at all, so no per-gate fold
 * inside cq_emit_* could ever remove them. src/reg.h enumerates three routes
 * that give the RIGHT VALUE and the wrong cost and this function is the answer
 * to all three; tests/test_runtime_rail_write.inc runs them side by side.
 *
 * THE QUANTUM ROW DOES NOT USE cq_shim_region, and that is the third route: a
 * region promotes EVERY gate inside it, so bracketing an uncontrolled 3-CNOT
 * swap gives 3W Toffolis where the Fredkin is 2W CX + W CCX — the same TOTAL
 * and three times the T-count, which is why a gate count cannot see it.
 *
 * THREE REFUSALS, IN BOTH CONFIGURATIONS, AND NONE OF THEM IS REDUNDANT.
 * `a == b` is CQ_lang's own `B2-two-written-rails-alias` and is degenerate
 * rather than non-unitary; `ctrl == a` or `ctrl == b` is its `B1`, an input
 * aliasing a written rail, which no unitary implements. M05's §3 distinctness
 * check catches all three — in DEBUG ONLY. Measured in Release: all three
 * return 0 having emitted malformed gates, `cx q0 q0` and `ccx q0 q1 q1`,
 * straight to the sink. And leaning on `cq_reg_swap_bits`'s own `a == b`
 * refusal would cover the ONE row alone, leaving the ZERO row a silent no-op
 * and the quantum row a silent Release miscompile. Measured over the 247
 * goldens at CQ_lang `3319975`: 37 `cqrt_cswap` calls, 0 with any two operands
 * equal, and every control an i1 flag returned by an `icmp` template.
 *
 * THE OPERANDS ARE VALIDATED BEFORE THE BRANCH, so the zero row refuses a
 * tombstone and a width mismatch exactly as the other two do. A guard that
 * fires on one branch is a guard that is not there. */
void cqrt_cswap(int32_t ctrl, int32_t a, int32_t b)
{
    cq_ctx *ctx = cq_shim_ctx();

    if (a == b)
        cq_rail_die("cqrt_cswap of a rail with itself: the ABI names two "
                    "written rails and no router can have meant one", a, 0u);
    if (ctrl == a || ctrl == b)
        cq_rail_die("cqrt_cswap whose control aliases a rail it writes: "
                    "cswap(c, c, b) is not injective, so no unitary "
                    "implements it", ctrl, 0u);

    /* Resolve the flag through cq_reg_cbits BEFORE comparing its width, so a
     * freed flag is M07's use-after-free and not our width bug — the order
     * shim/cq_shim_ctx.c records for the region bracket. A MEASURED flag is
     * deliberately admitted: a control is a READ. */
    const cq_bit *f = cq_reg_cbits(&ctx->regs, ctrl);
    const uint32_t fw = cq_reg_width(&ctx->regs, ctrl);
    if (fw != 1u)
        cq_rail_die("the cqrt_cswap control flag rail is not one bit wide",
                    ctrl, fw);

    const uint32_t w = cq_reg_width(&ctx->regs, a);
    cq_bit *pa = cq_reg_bits(&ctx->regs, a);
    cq_bit *pb = cq_reg_bits(&ctx->regs, b);
    if (w != cq_reg_width(&ctx->regs, b))
        cq_rail_die("cqrt_cswap between rails of different widths", a,
                    cq_reg_width(&ctx->regs, b));

    /* THE THREE ROWS RECORD THREE DIFFERENT THINGS, AND A FIRST DRAFT RECORDED
     * ONE. It pushed a `CQ_ROP_CSWAP` write against both data args on EVERY
     * row, on the reading that D15 §6(ii) — "`cqrt_cswap` WRITES BOTH DATA
     * ARGS, and a write-tracker that misses one silently widens every rule" —
     * is about the SYMBOL. It is about the symbol's QUANTUM row, and the
     * classical rows are a different physical event. A test caught it.
     *
     * ROW 0 (ZERO flag): nothing happens. No gate, no qubit, no rail changed.
     * Recording a write would put an unpairable entry into two histories that
     * never moved and would STRAND both — the under-discharge direction.
     *
     * ROW 1 (ONE flag): `cq_reg_swap_bits` exchanges the two rails' BIT ARRAYS
     * for zero gates, which is what PRD §2.1 prices this row at. NO QUBIT'S
     * STATE CHANGES — a permutation of ownership, not an operation — so this is
     * not a write either. What DOES have to move is the history: it is keyed by
     * handle, the bits moved between handles, and a history left behind
     * describes the wrong rail. That is `cq_rec_swap`, and the BIRTH VALUE
     * travels with it, correctly: `birth` describes the bits, and the bits are
     * what moved.
     *
     * ROW Q: a Fredkin per bit. Both data args are genuinely written, and this
     * is the row D15 §6(ii) is about. */
    /* THE ONLY SYMBOL IN THE SHIM THAT WRITES TWO RAILS, which is why
     * `cq_trace_op` has two written slots at all, and the only one with three
     * exits — so the close is repeated per row rather than hoisted. It is opened
     * AFTER the four refusals: a bracket around a call that aborts is a bracket
     * the viewer never sees closed, and the refusals are statements about the
     * CALL rather than about the operation.
     *
     * ROW 1's CLOSE IS AFTER `cq_reg_swap_bits`, DELIBERATELY. The snapshot
     * replaces rather than unions (cq_shim_trace.c), so closing after the
     * exchange is what makes each handle's recorded lanes follow its BITS — the
     * same thing `cq_rec_swap` does for the birth value, one subject over. */
    CQ_TRACE_OP2("cswap", ctrl, a, b, a, b);

    if (cq_bit_is_zero(f[0])) { cq_trace_end(); return; }  /* 0 gates, 0 qubits */
    if (cq_bit_is_one(f[0])) {                    /* 0 gates, 0 qubits        */
        cq_reg_swap_bits(&ctx->regs, a, b);
        cq_rec_swap(a, b);
        cq_trace_end();
        return;
    }

    rec(CQ_ROP_CSWAP, ctrl, a, b, 0u, 0.0, CQ_REG_NONE);

    for (uint32_t i = 0; i < w; i++) {            /* a Fredkin per bit        */
        cq_emit_cx (ctx, &pb[i], &pa[i]);
        cq_emit_ccx(ctx, &f[0], &pa[i], &pb[i]);
        cq_emit_cx (ctx, &pb[i], &pa[i]);
    }
    cq_trace_end();
}

/* --- xorc: no carry, so no adder ----------------------------------------- */

/* `h ^= imm` is one `cq_emit_x` per SET immediate bit and nothing else. That
 * single function IS §7's constant/qubit split for a bit flip — flip the
 * constant for nothing, or emit the gate — so an all-classical rail costs zero
 * gates and zero qubits and stays in the I4 row, which is what keeps PRD §15
 * D15's classical loop counters freeable. There is no `cqrt_xorc_*_controlled`
 * in the ABI, so PRD §9 row A's target-fold hazard cannot arise here. */
static void rail_xorc(uint32_t w, int32_t h, uint64_t imm)
{
    cq_ctx *ctx = cq_shim_ctx();
    cq_bit *b = rail_w(ctx, h, w);

    /* REDUNDANT TODAY AND SAID SO RATHER THAN LEFT TO BE MISREAD. Every caller
     * is a CQ_RAIL_IMM instantiation, which already narrows through
     * `(uint64_t)(UT)imm` with UT exactly W bits, so this line cannot change a
     * bit at any of the five shipped widths — measured: deleting both copies
     * turns nothing red. What DOES make a negative immediate safe is that cast
     * in the macro, not this. It stays as the precondition restated for a
     * future non-macro caller; do not read it as the width discipline. */
    imm &= width_mask(w);
    for (uint32_t i = 0; i < w; i++)
        if ((imm >> i) & UINT64_C(1)) cq_emit_x(ctx, &b[i]);
    rec(CQ_ROP_XORC, h, CQ_REG_NONE, CQ_REG_NONE, imm, 0.0, CQ_REG_NONE);
}

/* --- addc: the one family with no adder of its own ----------------------- */

/* PRD §15 D17 (bd dzj) — READ IT BEFORE CHANGING A LINE. `cqrt_addc_<W>` is
 * `h := (h + imm) mod 2^W`, IN PLACE, and libcqops had no in-place adder that
 * could serve it: M14's ripple is OUT of place, and M15's Cuccaro accumulator
 * hard-errors unless every bit of both operands and the ancilla is already a
 * qubit. The decision is: FOLD when the rail is all-classical, and otherwise
 * materialise the immediate into a fresh rail, materialise the rail's remaining
 * classical lanes, take ONE ancilla, and run M15 in place.
 *
 * THE FOLD IS MANDATORY, NOT AN OPTIMISATION. PRD §15 D15's U3 rule — born
 * `cqrt_alloc_W(L)`, the sum of `cqrt_addc` immediates is −L, no other write —
 * measured 40 of the corpus's ~45 I4 frees on that shape; making this path
 * materialise collapses it to 5. (`cqrt_xorc` is NOT part of U3, though it is
 * kind-preserving on a classical rail for the same reason.)
 *
 * AND THE QUANTUM PATH CANNOT BE DEFERRED, which was the open question dzj
 * settled by measurement: most `cqrt_addc_i32` calls in the corpus land on a
 * rail that already owns qubits, through a chain that starts at a general
 * `cqrt_ry`.
 *
 * FOUR TRAPS, THE FIRST FATAL TO A READER WHO KNOWS RULE 8.
 *
 *  1. DO NOT WRAP THIS IN cq_sandwich. The driver replays the compute half in
 *     reverse, which UNDOES the in-place write: `h` comes back unchanged behind
 *     a perfect palindrome. M20 applies its conditional negate to a SCRATCH
 *     COPY inside a sandwich for exactly this reason. With no sandwich there is
 *     no I6(b), so every materialisation here is manual INCLUDING the ancilla —
 *     `cq_addacc_check` refuses a non-qubit `x[0]` at every W.
 *  2. THE NAMING IS INVERTED FROM BENNETT. libcqops `acc` is Bennett's `b`, the
 *     register that is OVERWRITTEN; libcqops `b` is Bennett's `a`, the addend,
 *     which is RESTORED. So `h` goes in `acc` and the immediate in `b`. Swapped,
 *     the circuit computes into the wrong register and still looks like the
 *     source.
 *  3. THE TRANSIENTS ARE CERTIFIED BY THE CONSTRUCTION, NOT BY THE SHADOW, and
 *     `CQ_ZERO_BY_CUCCARO_RESTORE` above is that certificate — the third and
 *     last `proven_zero` constant in the library. The ancilla is Bennett's
 *     hygiene contract 3; the ADDEND is contract 1 ("a preserved") plus this
 *     function's own X-down loop, which is ours rather than upstream's. Handing
 *     these two rails to the shadow instead was the first draft and it costs
 *     three separate things — a permanent `W-1`-qubit leak per poisoned call, a
 *     hijacked one-shot strand report, and a CQOPS_FREE_ABORT that fires inside
 *     an arithmetic opcode. The constant's own comment has the measurements.
 *  4. cq_addacc_check COMPARES RANGES, NOT BASE POINTERS, and requires every
 *     bit of both operands and the ancilla to be CQ_BIT_Q. A mixed-kind `h`
 *     reaching K8 unmaterialised is a hard error, not a fold.
 *
 * The `cq_bit *` taken before the two mints stays valid: `cq_reg_grow` reallocs
 * only the slot array, and a register's bits array is its own allocation and is
 * stable for its life (src/reg.h). Do not add a re-fetch to guard a hazard this
 * code is not exposed to; the real constraint is that no `cq_reg *` escapes. */
/* THE THIRD `proven_zero` CONSTANT IN THE LIBRARY, and the first outside `src/`.
 *
 * PRD §10's rule is that a constant may exist only where the code that RUNS a
 * construction can assert that construction's premises. `rail_addc` runs one:
 * M15's Cuccaro accumulator, whose preconditions `cq_addacc_check` verifies at
 * step 0 in both configurations, and whose upstream hygiene contracts are that
 * the ADDEND comes back holding its input (contract 1, "a preserved") and the
 * ANCILLA comes back at |0> (contract 3, the ancilla clause — it covers the
 * ancilla ALONE, and the addend's return to |0> is contract 1 plus this
 * function's own X-down loop, which is ours rather than upstream's). The addend's input was the materialised immediate,
 * and the loop below drives its set bits back down — so both transients are
 * |0> for ANY input state, by linearity, and that is a theorem about the
 * circuit rather than a guess about the data.
 *
 * IT IS NOT A LAUNDERING SITE, AND THE DIFFERENCE IS WHOSE RAIL IT IS. What
 * `bd 216`'s checklist forbids is handing a blanket-clean proof to a CALLER's
 * free, where one wrong `1` launders a rail the library knows nothing about.
 * These two rails were minted eleven lines up, written by exactly one
 * construction, and destroyed here; nothing else can ever hold them. That is
 * the same standing `CQ_ZERO_BY_PALINDROME` (M09) and `CQ_ZERO_BY_CTRL_UNCOMPUTE`
 * (M06) have, and there is no fourth.
 *
 * AND IT INHERITS THEIR BACKSTOP FOR FREE. `cq_ctx_release_qubit` retires the
 * shadow entry immediately after the release, and `cq_shadow_retire` hard-errors
 * on a determinate NON-ZERO entry in both configurations — so if K8 ever stopped
 * restoring, the release of a transient the shadow can still see would abort
 * rather than put a dirty index on the free list. On a poisoned rail the shadow
 * cannot see, which is exactly where the theorem is doing the work.
 *
 * WHAT PASSING `cq_shim_shadow_proof` HERE INSTEAD WOULD COST — measured, and it
 * is three things rather than one. (i) `W-1` provably-clean qubits STRAND on
 * every call whose rail is rotation-poisoned, which PRD §15 D17 says is the
 * common corpus path, and permanently: D17 records that landing 2's certificate
 * does NOT reach these two rails, because their write history matches none of
 * D15's three rules. (ii) The transients consume D15 §3's ONE-SHOT strand
 * report, so the single line the program ever prints names an internal handle
 * that exists nowhere in the caller's program — and the caller's own rail then
 * strands in silence. (iii) `CQOPS_FREE_ABORT` kills the process inside an
 * arithmetic opcode the caller never asked to free anything, naming that same
 * unreachable handle, which makes the flag unusable across the whole L6 corpus.
 * All three are gone because there is nothing left to strand. */
enum { CQ_ZERO_BY_CUCCARO_RESTORE = 1 };

static int addc_transient_proof(const cq_ctx *ctx, int32_t h, uint32_t q)
{
    /* Per-qubit by signature and per-CONSTRUCTION in fact: the argument is the
     * circuit just run, not anything about this index. See above. */
    (void)ctx; (void)h; (void)q;
    return CQ_ZERO_BY_CUCCARO_RESTORE;
}

static void rail_addc(uint32_t w, int32_t h, uint64_t imm)
{
    cq_ctx *ctx = cq_shim_ctx();
    cq_bit *acc = rail_w(ctx, h, w);

    imm &= width_mask(w);    /* redundant today — see rail_xorc's note */

    /* RECORDED BEFORE THE IDENTITY SHORT-CIRCUIT AND BEFORE THE TWO TRANSIENT
     * MINTS, and both halves of that placement are deliberate. `imm == 0` is a
     * genuine no-op, so it must NOT appear as a write — recording it would make
     * a rail that never moved carry an unpairable entry and would strand it.
     * And the transients are minted BELOW: they are handles CQ_lang never sees
     * (D15's "since it was minted is not since its cqrt_alloc"), so the record
     * for THIS call has to be taken while `g_n` still points at this opcode's
     * own position rather than at a mint the ABI cannot name. */
    if (imm != 0u)
        rec(CQ_ROP_ADDC, h, CQ_REG_NONE, CQ_REG_NONE, imm, 0.0, CQ_REG_NONE);

    /* A PORT, not a peephole: upstream's own identity short-circuit. */
    if (imm == 0u) return;

    /* Addition mod 2 has no carry, which is also why cqrt_addc_i1 takes a
     * bool — after the row above the only surviving W=1 immediate is 1. */
    if (w == 1u) { cq_emit_x(ctx, &acc[0]); return; }

    if (cq_reg_owned_qubits(&ctx->regs, h) == 0u) {
        uint64_t v = 0u;
        for (uint32_t i = 0; i < w; i++)
            if (cq_bit_value(acc[i])) v |= UINT64_C(1) << i;
        cq_bits_from_words(acc, w, (v + imm) & width_mask(w), 0u);
        return;
    }

    {
        const int32_t h_imm = cq_reg_alloc_const(&ctx->regs, w, imm, 0u);
        const int32_t h_anc = cq_reg_alloc_zero(&ctx->regs, 1u);
        cq_bit *addend = cq_reg_bits(&ctx->regs, h_imm);
        cq_bit *anc    = cq_reg_bits(&ctx->regs, h_anc);
        cq_addacc_block k;

        for (uint32_t i = 0; i < w; i++) cq_materialise(ctx, &addend[i]);
        for (uint32_t i = 0; i < w; i++)
            if (!cq_bit_is_qubit(acc[i])) cq_materialise(ctx, &acc[i]);
        cq_materialise(ctx, &anc[0]);

        k.acc = acc; k.b = addend; k.x = anc; k.W = (int)w;
        cq_kernel_addacc(ctx, &k);

        /* K8 restores the addend, so driving the set bits back down returns the
         * rail to |0> — one X per set bit, the mirror of the materialisation. */
        for (uint32_t i = 0; i < w; i++)
            if ((imm >> i) & UINT64_C(1)) cq_emit_x(ctx, &addend[i]);

        cq_reg_free(ctx, h_imm, addc_transient_proof);
        cq_reg_free(ctx, h_anc, addc_transient_proof);
    }
}

/* BOTH BRACKETS ARE HERE RATHER THAN IN THE TWO HELPERS, on the entry-point rule
 * this file states at CQ_RAIL_COPY. `rail_addc` has four exits — the identity
 * short-circuit, the W = 1 row, the I4 fold and the Cuccaro path — and putting
 * the close at the one place all four return through is what keeps them from
 * drifting apart. Both are `h := f(h, imm)`, so `h` is read and written. */
#define CQ_RAIL_IMM(W, CT, UT)                                                \
    void cqrt_addc_i##W(int32_t h, CT imm)                                    \
    {                                                                         \
        CQ_TRACE_OP("addc", h, CQ_REG_NONE, CQ_REG_NONE, h);                  \
        rail_addc(W##u, h, (uint64_t)(UT)imm);                                \
        cq_trace_end();                                                       \
    }                                                                         \
    void cqrt_xorc_i##W(int32_t h, CT imm)                                    \
    {                                                                         \
        CQ_TRACE_OP("xorc", h, CQ_REG_NONE, CQ_REG_NONE, h);                  \
        rail_xorc(W##u, h, (uint64_t)(UT)imm);                                \
        cq_trace_end();                                                       \
    }

CQ_RAIL_IMM(1,  bool,    bool)
CQ_RAIL_IMM(8,  int8_t,  uint8_t)
CQ_RAIL_IMM(16, int16_t, uint16_t)
CQ_RAIL_IMM(32, int32_t, uint32_t)
CQ_RAIL_IMM(64, int64_t, uint64_t)
