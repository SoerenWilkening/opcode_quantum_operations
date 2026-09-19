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
 * when it bites: `the SEQUENCE <-> the INTEGER cast entry points` ->
 * `shim/cq_template_cast.c`, on this file's own discriminator — the sequence
 * changes when a DECISION about composition changes, the three integer cast
 * entry points when `opcode_table.yaml` gains a cast kind.
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
#include <string.h>

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
 * `zext i8->i64` on the same handles, and `trunc` is not `zext`'s. */
static tpl_ureq cast_req(cq_shim_cast_kind kind, int from_bits, int to_bits,
                         int32_t out, int32_t a_handle)
{
    tpl_ureq r;
    const uint32_t f = cq_tpl_width(from_bits), t = cq_tpl_width(to_bits);

    r.inner.fn = NULL; r.inner.f = r.inner.t = 0u; r.inner.tag = 0u;
    r.outer.fn = cq_tpl_cast_kernel(kind);
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
