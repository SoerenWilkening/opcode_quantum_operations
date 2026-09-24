/* shim/cq_template_unary.c — M26's ARITY-1 ORDERED CALL SEQUENCE, the third
 * seam `shim/cq_template_boundary.h` records (bead 9ve.36).
 *
 *     the ARITY-2 ordered call sequence <-> the ARITY-1 ordered call sequence
 *
 * WHAT IS *NOT* HERE IS AS LOAD-BEARING AS WHAT IS. There is no second copy of
 * D7a's refusal, of the width doors, of the mint, of D15's record shape or of
 * D21's bracket rule: `cq_reg_check_operands`, `cq_tpl_width`, `cq_tpl_src` and
 * `cq_tpl_out` are `shim/cq_template_impl.c`'s and are reached from here, so a
 * deleted guard cannot be masked by a duplicate one file over. What IS here is
 * the one thing an arity-1 operation has that an arity-2 one does not: a
 * WORKSPACE stage.
 *
 * THE COMPOSITION IS UPSTREAM'S SHAPE, NOT A CONVENIENCE (PRD-v2 §7.9, K19.md
 * §2.6). `instructions.jl:7649-7658` emits a narrowing `fptosi` as a SECOND IR
 * instruction and `:7677-7679` a widening `sitofp`'s `sext` as a separate cast,
 * so the ABI rows compose two operations and this file is where the ABI's one
 * call becomes them:
 *
 *   NARROW   tmp(64) := k(a);  dst(T) ^= trunc(tmp);  tmp ^= k(a);  free tmp
 *   WIDEN    tmp(64) := sext(a);  dst(64) ^= k(tmp);  tmp ^= sext(a);  free tmp
 *   NEITHER  dst ^= k(a)
 *
 * and the three are ONE sequence because the two composed forms are mirror
 * images: an INNER stage from `a` into the workspace, and an OUTER stage into
 * `dst` reading whichever of the two the request names.
 *
 * WHY THE OUTER RECORD NAMES `a` AND NOT THE WORKSPACE. `adjoint_matches`
 * compares ALL FOUR handle slots, and the workspace is a handle minted FRESH on
 * every call (D5: monotonic, never reused) — so a record naming it could never
 * match between a forward and its `_unc`, and every narrow conversion's result
 * would be UNPROVEN and strand. Naming `a` is also the TRUTHFUL model of the
 * composed operation: at the ABI level the call is `out ^= f(a)`, the workspace
 * exists only between the mint and the free inside this one call, and it is a
 * deterministic function of `a` — so upstream's T2 obligation ("an `_inv`
 * recomputes f from the live sources; if a source moved it recomputes a
 * DIFFERENT value") is discharged exactly by requiring `a` to be unchanged,
 * which the engine's operand-stability check does for free.
 *
 * THE WORKSPACE'S OWN PAIR IS SEPARATE AND IS WHAT RELEASES IT. Its two writes
 * carry the INNER stage's tag and the slots `{tmp, a}`, identical across the
 * pair because both happen inside one call. They are a genuine adjoint pair —
 * the same kernel at the same widths from the same unchanged source — so the
 * reduction cancels them, the birth value is zero, and the rail is proven
 * clean. Without the second call the workspace would leak 64 qubits per
 * conversion; without the RECORD it would strand on every poisoned operand.
 *
 * RULE 12. Budget 300, trigger 240. The reserve seam, recorded now rather than
 * when it bites: `the SEQUENCE <-> the INTEGER entry points` ->
 * `shim/cq_template_iun.c`, on this file's own discriminator — the sequence
 * changes when a DECISION about composition changes, the integer entry points
 * (the three casts and, since bead 9ve.44, the intrinsic unary door) when a
 * yaml gains an integer arity-1 opcode.
 */

#include "cq_shim.h"

#include "cq_shim_ctx.h"
#include "cq_shim_proof.h"
#include "cq_shim_record.h"
#include "cq_shim_trace.h"
#include "cq_template_boundary.h"
#include "cq_template_dispatch.h"

#include "bit.h"
#include "ctx.h"
#include "reg.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The house shape, this translation unit's own copy (`src/reg_check.c`'s
 * precedent): every string below appears nowhere else in shim/, because the
 * death pins discriminate on the MESSAGE and not on the module. */
static void cq_un_die(const char *what, int v)
{
    fprintf(stderr, "libcqops: FATAL: shim: %s (%d)\n", what, v);
    abort();
}

/* One stage's D15 record: `h[0]` is the rail it WROTE and `h[1]` the handle the
 * ABI says it read. `CQ_ROP_TPL_FWD`/`TPL_UNC` model operand SLOTS and not
 * opcodes (shim/cq_shim_record.h), so an arity-1 operation needs no new effect
 * row — slot 2 is simply absent. */
static void urec(int is_unc, int32_t out, int32_t src, uint32_t tag)
{
    cq_call_rec c;

    memset(&c, 0, sizeof c);
    c.op   = (uint16_t)(is_unc ? CQ_ROP_TPL_UNC : CQ_ROP_TPL_FWD);
    c.h[0] = out; c.h[1] = src; c.h[2] = CQ_REG_NONE; c.h[3] = CQ_REG_NONE;
    c.ctrl = CQ_REG_NONE;
    c.tag  = tag;
    cq_rec_push(&c);
}

/* Runs `st` from `src` into the rail `dst_h` and records it. The bits pointer
 * is resolved HERE rather than held by the caller, because the workspace mint
 * may sit between two calls of this function and only a `cq_reg *` would be
 * invalidated by that — a rail's `bits` array is its own allocation and is
 * stable for the register's life (reg.h), but resolving late costs nothing and
 * removes the question. */
static void ustage(cq_ctx *ctx, const tpl_ustage *st, int32_t dst_h,
                   const cq_bit *src, int is_unc, int32_t src_h)
{
    st->fn(ctx, cq_reg_bits(&ctx->regs, dst_h), src, (int)st->f, (int)st->t);
    urec(is_unc, dst_h, src_h, st->tag);
}

int32_t cq_tpl_unary(tpl_ureq r)
{
    cq_ctx *ctx = cq_shim_ctx();
    int32_t srcs[1];
    int32_t tmp = CQ_REG_NONE;
    int     is_unc;

    srcs[0] = r.a_h;
    cq_reg_check_operands(&ctx->regs, r.out, srcs, 1u);
    cq_tpl_src(ctx, r.a_h, r.w);
    /* THE WRITE DOOR RUNS BEFORE THE BRACKET OPENS. Its refusal is about the
     * CALL, and a bracket opened first would be left unclosed — which is a
     * FATAL parse error for the viewer rather than a cosmetic gap. The pointer
     * it returns is deliberately discarded: the stages below resolve `out`
     * again through `cq_reg_bits`, and what matters is that the WRITE door was
     * the one this rail went through (a measured rail is refused here and
     * admitted by `cq_tpl_src`). */
    if (r.out != CQ_REG_NONE) (void)cq_tpl_out(ctx, r.out, r.wout);

    /* D21's ANNOTATION BRACKET, OPENED BEFORE THE WORKSPACE MINT so that the
     * composition's gates are inside this operation's bracket. The predicted
     * result handle therefore carries the workspace's `+ 1`, exactly as the
     * arity-2 path carries D7b's: `cq_reg_alloc_zero` returns `cq_reg_count`
     * and increments, and the workspace is minted FIRST. The prediction is not
     * defended by an assert but by execution — tests/test_shim_trace.c compares
     * the emitted `out=` token against the handle the call returns, at a narrow
     * shape as well as a flat one. */
    cq_trace_op_tpl(r.name, r.out != CQ_REG_NONE, CQ_REG_NONE, r.a_h,
                    CQ_REG_NONE,
                    r.out != CQ_REG_NONE
                        ? r.out
                        : cq_reg_count(&ctx->regs) + (r.inner.fn != NULL));

    if (r.inner.fn != NULL) {
        tmp = cq_reg_alloc_zero(&ctx->regs, r.inner.t);
        /* A HANDLE CQ_lang NEVER SEES, and exactly what D15 means by "since it
         * was MINTED is not since its cqrt_alloc". Without the mint the
         * certificate has no history for it and it strands on EVERY composed
         * call; the mint marker also occupies a stream position, which is what
         * keeps the reduction's strictly-open window from excluding the rail's
         * own first write. */
        cq_rec_mint(tmp, r.inner.t, 0u, 0u, 0);
        ustage(ctx, &r.inner, tmp, cq_reg_cbits(&ctx->regs, r.a_h), 0, r.a_h);
    }

    /* IS THIS A FORWARD OR AN `_unc`? The discriminator is `r.out`, read HERE
     * because the next three lines overwrite it with the mint. */
    is_unc = (r.out != CQ_REG_NONE);
    if (r.out == CQ_REG_NONE) {
        r.out = cq_reg_alloc_zero(&ctx->regs, r.wout);
        /* MINTED AT |0>, which is why U1 clears and ckd.18 convicts: the birth
         * value is the port's one divergence from upstream's obligation. */
        cq_rec_mint(r.out, r.wout, 0u, 0u, 1);
    }

    ustage(ctx, &r.outer, r.out,
           cq_reg_cbits(&ctx->regs, tmp != CQ_REG_NONE ? tmp : r.a_h),
           is_unc, r.a_h);

    if (tmp != CQ_REG_NONE) {
        ustage(ctx, &r.inner, tmp, cq_reg_cbits(&ctx->regs, r.a_h), 1, r.a_h);
        cq_reg_free(ctx, tmp, cq_shim_free_proof);
        cq_rec_retire(tmp);
    }
    /* CLOSED AFTER THE WORKSPACE'S FREE. The workspace is WORKSPACE in handoff
     * §6 rule 3's sense — a handle CQ_lang never sees, left out of every
     * `qubits=` list and shown as extra lanes inside this op — so its gates and
     * its teardown belong inside this operation's bracket. */
    cq_trace_end();
    return r.out;
}

/* --- the INTEGER width casts --------------------------------------------- */

/* Arity 1, no D7b lane, no §9 axis, no classical operand, and the second shape
 * whose result width is not its operand width. No composition either: `sext`,
 * `zext` and `trunc` are single kernels, so `inner.fn` stays NULL and the
 * sequence collapses to one stage.
 *
 * A CAST'S TAG FOLDS BOTH WIDTHS IN: `zext i8->i32` is not the adjoint of
 * `zext i8->i64` on the same handles, and `trunc` is not `zext`'s.
 *
 * `bitcast` (bead s8g) IS THE FOURTH KIND AND THE SAME ONE-STAGE SHAPE: M13's
 * `zext` at `F == T`, a physical F-CX copy. Its one rule is hoisted here,
 * AFTER the range and the enum (so a wild width or kind gets its own sentence)
 * and BEFORE the bracket and the mint: a bitcast whose widths differ is
 * refused, because `zext` would silently WIDEN at `(32, 64)` and refuse
 * `(64, 32)` only one layer down, after a rail is minted and a D21 bracket
 * opened. Both directions share one kind and therefore one tag — they are the
 * same function on the bits, so a forward in one direction is truthfully
 * undone by the other's `_unc` (tests/test_template_bitcast_cert.inc). */
static tpl_ureq cast_req(cq_shim_cast_kind kind, int from_bits, int to_bits,
                         int32_t out, int32_t a_handle)
{
    tpl_ureq r;
    const uint32_t f = cq_tpl_width(from_bits), t = cq_tpl_width(to_bits);

    r.inner.fn = NULL; r.inner.f = r.inner.t = 0u; r.inner.tag = 0u;
    r.outer.fn = cq_tpl_cast_kernel(kind);
    if (kind == CQ_SHIM_CAST_BITCAST && f != t)
        cq_un_die("bitcast is a same-width reinterpretation and its two "
                  "widths differ; the from-width is", (int)f);
    r.outer.f = f; r.outer.t = t;
    r.outer.tag = 0x80000000u + (uint32_t)kind * 65536u + f * 256u + t;
    r.w = f; r.wout = t;
    r.a_h = a_handle; r.out = out;
    r.name = cq_tpl_cast_name(kind);
    return r;
}

int32_t cq_shim_cast(cq_shim_cast_kind kind, int from_bits, int to_bits,
                     int32_t a_handle)
{
    return cq_tpl_unary(cast_req(kind, from_bits, to_bits, CQ_REG_NONE, a_handle));
}

void cq_shim_cast_unc(cq_shim_cast_kind kind, int from_bits, int to_bits,
                      int32_t out_handle, int32_t a_handle)
{
    (void)cq_tpl_unary(cast_req(kind, from_bits, to_bits, out_handle, a_handle));
}

/* --- the INTEGER UNARY intrinsics (bead 9ve.44) --------------------------- */

/* M41's ctpop/ctlz/cttz/bitreverse/bswap and M42's abs. ONE stage, one width,
 * a result as wide as the operand, no composition — so `inner.fn` stays NULL
 * and the sequence is the cast's one-stage shape with the width given once.
 *
 * THE NINTH TAG SPACE, `0x60000000 | op * 1024 + w + 1`, and it is disjoint
 * from every other by prefix (the arithmetic is re-checked in
 * cq_template_boundary.h). It matters for a reason the record makes exact: an
 * integer unary `_unc` records `{out, a, NONE, NONE}` with `imm` 0, which is
 * what a binary `_hl` forward with a zero literal records too — so `ctpop i8`
 * and `add i8, 0` would pair on every field but the tag if they shared a
 * space, and a free after `add` then `ctpop_unc` would RELEASE a rail holding
 * `a ^ ctpop(a)`.
 *
 * THE ORDER THAT MATTERS IS RANGE BEFORE THE BSWAP RULE: a wild width (129
 * fails both) must get the range door's sentence, and the rule must never
 * read an unvalidated `bits` (tests/test_template_intr_death.c's
 * `an_iun_width_outside_the_abis_range_is_refused` is its detector). The ENUM
 * lookup's place relative to the range is NOT load-bearing and is recorded as
 * such rather than defended: it indexes by `op` alone, so swapping the two is
 * an equivalent mutant (measured, bead 9ve.44). bswap's 16-bit rule is last.
 * `cq_kernel_bswap` refuses the same widths one layer down, AFTER the call has
 * minted a rail and opened a D21 bracket that is then never closed, which is
 * a fatal parse error for the viewer; the refusal belongs where the claim is
 * about the CALL. The ABI has no `bswap` symbol at i8 at all
 * (intrinsic_table.yaml: `bswap.i8` is rejected by LLVM), so no generated
 * body can reach this line — a direct caller can. */
static tpl_ureq iun_req(cq_shim_iun_op op, int bits, int32_t out,
                        int32_t a_handle)
{
    tpl_ureq r;
    const uint32_t w = cq_tpl_width(bits);
    const cq_tpl_cast_fn k = cq_tpl_iun_kernel(op);

    if (op == CQ_SHIM_IUN_BSWAP && w % 16u != 0u)
        cq_un_die("bswap is refused at a width that is not a multiple of "
                  "16 bits; llvm.bswap swaps whole BYTE PAIRS and "
                  "intrinsic_table.yaml lists it at i16, i32 and i64 only "
                  "(K26.md)", (int)w);

    r.inner.fn = NULL; r.inner.f = r.inner.t = 0u; r.inner.tag = 0u;
    r.outer.fn = k;
    r.outer.f  = w; r.outer.t = w;
    r.outer.tag = 0x60000000u + (uint32_t)op * 1024u + w + 1u;
    r.w = r.wout = w;
    r.a_h = a_handle; r.out = out;
    r.name = cq_tpl_iun_name(op);
    return r;
}

int32_t cq_shim_iun(cq_shim_iun_op op, int bits, int32_t a_handle)
{
    return cq_tpl_unary(iun_req(op, bits, CQ_REG_NONE, a_handle));
}

void cq_shim_iun_unc(cq_shim_iun_op op, int bits, int32_t out_handle,
                     int32_t a_handle)
{
    (void)cq_tpl_unary(iun_req(op, bits, out_handle, a_handle));
}
