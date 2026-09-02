/* shim/cq_runtime_tape.c — v1.1's TAPE surface (PRD §15 D23, `bd 1za`, plan
 * §0.5, Step 28; 2026-09-02). The 11 `cqrt_tape_*` symbols: one token mint and
 * two write families at the five integer widths. Until this file existed they
 * were loud aborts in cq_runtime_v2.c, on a §1 reason that was measured false —
 * six shipped fixtures ARE the consumer §1 said did not exist.
 *
 * WHAT A TAPE WRITE IS, from cq_runtime.h:436-500 (READ-ONLY; CQ_lang is
 * unpinned): `cqrt_tape_write_<W>(tape, src) -> out` APPENDS `src` to the tape
 * as a fresh |0> rail that is KEPT — never freed, never uncomputed, the Bennett
 * output tape — "a CNOT-class basis copy, `src` is PRESERVED". The controlled
 * form commits the content under an i1 flag prepended as argument 0, mirroring
 * `cqrt_copy_<W>_controlled`; the entry is ALWAYS appended and only its content
 * is conditional. So the specification is the copy's, and THIS FILE OWNS NO
 * GATE LOOP: a write is `cq_reg_alloc_zero` plus the SAME `cq_reg_xor_into`
 * that `cqrt_copy_<W>` uses — `0 ^ src == src` — and the controlled write is
 * that inside `cq_shim_region`, the one §9 bracket in the shim. Rule 1 has
 * nothing to look up because there is no construction.
 *
 * THE TAPE HANDLE IS A CLASSICAL TOKEN OWNING ZERO QUBITS, OUT OF THE SHARED D5
 * COUNTER — M07's fourth slot state, `CQ_SLOT_TOKEN` (plan §0.5). It takes a
 * slot ONLY so that it draws from the same counter as every rail: `tape` and
 * `src` are both int32_t, and a token numbered anywhere else would collide with
 * a rail's number, making a rail-handed-as-a-tape undetectable. The goldens
 * show the shared counter directly — `cqrt_tape_alloc() -> t0`, then
 * `cqrt_alloc_i32(4) -> h1`.
 *
 * TWO REFUSALS, TWO OWNERS. A RAIL in the tape slot is THIS file's hard error,
 * by state, in both configurations — M07 has no opinion about which slot a
 * rail was passed in. A TOKEN anywhere a rail is expected — `src` here, the
 * flag, `cqrt_x`, `cqrt_free`, `cqrt_measure`, a template operand — is M07's,
 * through the two funnels every rail accessor passes (reg.c), so nothing here
 * re-implements it. The messages are DISJOINT from cq_runtime_rail.c's on
 * purpose: two copies printing the same string is the recorded masking trap,
 * and tests/CMakeLists.txt's pins discriminate on the message.
 *
 * WHERE THE TOKEN IS NOT. The RECORDER: no history, no mint marker —
 * `cq_rec_hist(token)` is NULL, because a token is never written and never
 * read; the two write rows (`CQ_ROP_TAPE_WRITE[_CTRL]`) name `src`, `out` and
 * the flag only, with `src` a READ, which is what lets a rail written onto the
 * tape between a template forward and its `_unc` still pair and free CLEAN.
 * The D21 ANNOTATION: `cqrt_tape_alloc` joins `cqrt_alloc_i<W>`'s NAMED STATIC
 * exemption and opens no bracket (it takes the register TABLE and cannot reach
 * a `qec_*` call); a write's bracket names `src` (and the flag) in and `out`
 * out, and does NOT name the token — it has no `#REGISTER` line (zero qubits,
 * I4 made visible) and `in=` would spell it `h<N>`, which is a lie.
 *
 * D7b: NO DEFENSIVE COPY. Measured over the six goldens at CQ_lang `893b769`,
 * the flag aliases the source in 0 of 5 controlled writes; and at W = 1 a flag
 * that IS the source is a control coinciding with an inner control, §9 row B,
 * which M06 refuses in both configurations.
 *
 * THE MINT PRECEDES THE BRACKET, and that is fine where it was not for D7b's
 * copy (D21 (iii)): `cq_reg_alloc_zero` emits nothing, so opening the bracket
 * on the real handle loses no gate, and the open-side snapshot of an
 * all-constant `out` is simply empty. MEASURED EQUIVALENT (battery T11,
 * 2026-09-02, both configurations): moving the mint INSIDE the bracket and
 * predicting the handle as `cq_reg_count` survives everything, because the
 * two orders emit the same stream and name the same handle. Recorded at the
 * site rather than "fixed" — the paired mutation that IS killed is the mint
 * at birth ONE (T10), which locates the work in the birth value, not the order.
 *
 * RULE 12. Budget 120, recorded in IMPLEMENTATION_PLAN §3 before this file was
 * written. Seam: `the TOKEN ↔ the WRITE` — `cqrt_tape_alloc` and the token
 * check on one side, the two write families on the other; the discriminator is
 * that a line touching a `cq_bit` is the write's. Moves to
 * shim/cq_runtime_tape_write.c. Trigger 240, the house figure.
 */

#include "cq_runtime_abi.h"

#include "cq_shim_ctx.h"
#include "cq_shim_qram.h"
#include "cq_shim_record.h"
#include "cq_shim_trace.h"

#include "bit.h"
#include "ctx.h"
#include "reg.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The house shape and the shim's prefix, per translation unit (reg_check.c's
 * precedent). Both handles are printed: the refusal names the slot AND what
 * was found in it. */
static void cq_tape_die(const char *what, int32_t h, long k)
{
    fprintf(stderr, "libcqops: FATAL: shim: %s (h%d, %ld)\n", what, h, k);
    abort();
}

/* --- alloc: the token ----------------------------------------------------- */

/* NO BRACKET (the D21 exemption, decided STATICALLY: `cq_reg_alloc_token` takes
 * the table and cannot emit) and NO RECORD (a token has no history). Zero
 * gates, zero qubits, one number out of the shared counter. */
int32_t cqrt_tape_alloc(void)
{
    return cq_reg_alloc_token(&cq_shim_ctx()->regs);
}

/* --- write: the copy, into a rail born |0> ------------------------------- */

/* THE SLOT IS CHECKED BY STATE, through `cq_reg_state` so that an out-of-range
 * handle is M07's diagnostic and a live rail, a tombstone or a measured rail in
 * the tape slot is THIS one. */
static void tape_slot(const cq_ctx *ctx, int32_t tape, int32_t src)
{
    if (cq_reg_state(&ctx->regs, tape) != CQ_SLOT_TOKEN)
        cq_tape_die("the tape operand is not a tape token: the ABI's first "
                    "operand is the t<N> cqrt_tape_alloc returned, and this "
                    "handle is a rail (live, freed or measured)", tape, (long)src);
    /* TWO OWNERS OF ONE STATE (PRD §15 D24, 2026-09-02): a qram ARRAY is the
     * same CQ_SLOT_TOKEN and passed the check above until qram existed. The
     * payload table is how the two are told apart. */
    if (cq_qram_find(tape))
        cq_tape_die("the tape operand is a qram ARRAY token, not a tape",
                    tape, (long)src);
}

/* The source: a rail this symbol only READS. Resolved through `cq_reg_cbits`
 * FIRST so that a tombstone or a token is M07's diagnostic, then the width —
 * the token is a claim about the rail (cq_runtime_rail.c's rule, and its
 * message is deliberately NOT this one). */
static void tape_src(cq_ctx *ctx, int32_t src, uint32_t w)
{
    uint32_t got;

    (void)cq_reg_cbits(&ctx->regs, src);
    got = cq_reg_width(&ctx->regs, src);
    if (got != w)
        cq_tape_die("the source rail is not the width its cqrt_tape_write "
                    "symbol names", src, (long)got);
}

typedef struct { int32_t src, out; } tape_args;

/* THE BODY IS cqrt_copy's, VERBATIM IN EFFECT: `cq_reg_xor_into` is the one
 * per-lane copy in the library, and under `cq_shim_region` M06 promotes each
 * of its gates — nothing here knows whether a region is open (Rule 9). */
static void tape_body(void *p)
{
    const tape_args *a = (const tape_args *)p;
    cq_reg_xor_into(cq_shim_ctx(), a->out, a->src);
}

/* Validate both operands, then mint the kept rail at birth 0 — the record's
 * marker first, so the write below lands strictly inside its window. */
static int32_t tape_mint(cq_ctx *ctx, uint32_t w, int32_t tape, int32_t src)
{
    int32_t out;

    tape_slot(ctx, tape, src);
    tape_src(ctx, src, w);
    out = cq_reg_alloc_zero(&ctx->regs, w);
    cq_rec_mint(out, w, 0u, 0u, 0);
    return out;
}

/* ONE RECORD PER ENTRY POINT, never per gate (Rule 13) — the four-line helper
 * cq_runtime_rail.c and cq_runtime_gate.c each carry, for the reason they give:
 * the operand ORDER is what a write model gets wrong, and it belongs next to
 * the entry point that knows it. */
static void rec(cq_rop op, int32_t h0, int32_t h1, int32_t h2, int32_t ctrl)
{
    cq_call_rec c;
    memset(&c, 0, sizeof c);
    c.op   = (uint16_t)op;
    c.h[0] = h0; c.h[1] = h1; c.h[2] = h2; c.h[3] = CQ_REG_NONE;
    c.ctrl = ctrl;
    cq_rec_push(&c);
}

/* THE BRACKET IS IN THE MACRO, AT THE OUTERMOST ENTRY POINT (cq_shim_trace.h);
 * `tape_body` is also the controlled family's body, one region deeper, so a
 * bracket there would nest and hard-error. */
#define CQ_TAPE_WRITE(W)                                                      \
    int32_t cqrt_tape_write_i##W(int32_t tape, int32_t src)                   \
    {                                                                         \
        cq_ctx *ctx = cq_shim_ctx();                                          \
        tape_args a = { src, tape_mint(ctx, W##u, tape, src) };               \
        cq_trace_op("tape_write", src, CQ_REG_NONE, CQ_REG_NONE,              \
                    a.out, CQ_REG_NONE);                                      \
        tape_body(&a);                                                        \
        cq_trace_end();                                                       \
        rec(CQ_ROP_TAPE_WRITE, src, a.out, CQ_REG_NONE, CQ_REG_NONE);         \
        return a.out;                                                         \
    }                                                                         \
    int32_t cqrt_tape_write_i##W##_controlled(int32_t ctrl, int32_t tape,     \
                                              int32_t src)                    \
    {                                                                         \
        cq_ctx *ctx = cq_shim_ctx();                                          \
        tape_args a = { src, tape_mint(ctx, W##u, tape, src) };               \
        cq_trace_op("tape_write_ctrl", ctrl, src, CQ_REG_NONE,                \
                    a.out, CQ_REG_NONE);                                      \
        cq_shim_region(ctrl, tape_body, &a);                                  \
        cq_trace_end();                                                       \
        rec(CQ_ROP_TAPE_WRITE_CTRL, ctrl, src, a.out, ctrl);                  \
        return a.out;                                                         \
    }

CQ_TAPE_WRITE(1)
CQ_TAPE_WRITE(8)
CQ_TAPE_WRITE(16)
CQ_TAPE_WRITE(32)
CQ_TAPE_WRITE(64)
