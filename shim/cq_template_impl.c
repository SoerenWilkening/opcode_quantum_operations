/* shim/cq_template_impl.c — M26's OPCODE surface, Step 23 landing 1 step 6: the
 * FIFTEEN remaining `cq_shim_*` entry points. (The sixteenth,
 * `cq_shim_unsupported`, is `shim/cq_shim_ctx.c`'s.)
 *
 * WHAT M28 LEAVES FOR THIS FILE. `shim/cq_shim.h` fixes the boundary: each of
 * the 992 generated wrappers is one call carrying only the constants the frozen
 * ABI dictates — an opcode / predicate / cast-kind enum, the width in BITS, and
 * a classical literal as a two-word little-endian pattern. EVERYTHING else is
 * here, in one place rather than 992: handle resolution, D7a's refusal, D7b's
 * defensive copy, minting the result rail at the RESULT width, choosing the
 * kernel, and opening a PRD §9 control region through the shim's one bracket.
 *
 * RULE 12, AND THE RECORDED SEAM WAS TAKEN RATHER THAN DEFERRED. Budget 280,
 * trigger 240; the whole file came in at 270 counted lines, which is precisely
 * the case Rule 12 calls "a scheduled split, never a surprise refactor". The
 * cut is the one IMPLEMENTATION_PLAN §3 recorded before either file existed:
 *
 *     the opcode DISPATCH TABLE <-> the handle BOUNDARY
 *
 * and the DISPATCH half is now `shim/cq_template_dispatch.[ch]` — the three
 * tables, their bounds checks and every `src/kernels/` include. What is left
 * here is the BOUNDARY: `tpl_binary`'s ordered call sequence, D7a's refusal,
 * D7b's defensive copy and the §9 region. The discriminator is what makes each
 * half change — the yaml gaining an opcode, versus a DECISION changing.
 *
 * LANDING 2's CERTIFICATE DOES NOT LAND HERE. It lands in
 * `shim/cq_shim_proof.[ch]`, which is why this file frees through
 * `cq_shim_shadow_proof` and names no evidence of its own; the only thing it
 * owes landing 2 is that D7b's temporary go through the same door as every
 * other free, so that converting strands to releases converts this one too.
 */

#include "cq_shim.h"

#include "cq_shim_ctx.h"
#include <string.h>

#include "cq_shim_proof.h"
#include "cq_shim_record.h"
#include "cq_shim_trace.h"
#include "cq_template_dispatch.h"

#include "bit.h"
#include "ctx.h"
#include "kernels/kernel.h"
#include "reg.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* A THIRD static WITH THE SAME `shim:` PREFIX, on `src/reg_check.c`'s and
 * `shim/cq_runtime_rail.c`'s precedent: the `FAIL_REGULAR_EXPRESSION` pins in
 * tests/CMakeLists.txt discriminate on the MESSAGE, not on the module, so every
 * string below is deliberately DISJOINT from the rail file's and the gate
 * file's — `cq_template_*` where those say `cqrt_*`. Two guards spelling the
 * same sentence is how a deleted one keeps passing. */
static void cq_tpl_die(const char *what, int32_t h, uint32_t n)
{
    fprintf(stderr, "libcqops: FATAL: shim: %s (h%d, %u)\n", what, h, n);
    abort();
}

static const char *const TPL_WIDTH =
    "the rail is not the width its cq_template_* symbol names";

/* --- the BOUNDARY half ---------------------------------------------------- */

/* THE WIDTH ARRIVES AS AN `int` IN BITS AND IS VALIDATED AS A RANGE, never as a
 * whitelist: `src/reg.h` records that PRD §2.2's old "1, 8, 16, 32, 64 or 128"
 * is stale against the resolved i80 decision. Checking it HERE rather than
 * letting `cq_reg_alloc_zero` refuse it is what keeps the negative case out of
 * the `(uint32_t)` conversion below. */
static uint32_t tpl_width(int bits)
{
    if (bits < 1 || (unsigned)bits > CQ_REG_WIDTH_MAX)
        cq_tpl_die("cq_template_* width outside the ABI's range", CQ_REG_NONE,
                   (uint32_t)(bits < 0 ? 0 : bits));
    return (uint32_t)bits;
}

/* THE READ DOOR ADMITS A MEASURED RAIL AND THE WRITE DOOR REFUSES ONE. That
 * asymmetry is M07's (`cq_reg_cbits` vs `cq_reg_bits`) and this file only
 * chooses which door an operand goes through: a source is read, an `_unc`
 * destination is written.
 *
 * THE RESOLVE-BEFORE-COMPARE ORDER IS KEPT AND IS INERT HERE, WHICH IS RECORDED
 * RATHER THAN ARGUED. `shim/cq_runtime_rail.c`'s `rail_w` records the rule —
 * `cq_reg_width` goes through `cq_reg_slot`, which admits a TOMBSTONE on
 * purpose, so hoisting the comparison turns M07's use-after-free message into
 * our width message — and `bd pnu` re-pinned it on four helpers there. In THIS
 * file it cannot be observed, and the reason is a masking layer that is
 * load-bearing rather than redundant: `cq_reg_check_operands` runs FIRST and
 * refuses a handle that is not a live rail, so by the time this function is
 * reached the tombstone row is unreachable and both orders agree on every rail
 * that gets here. MEASURED (mutant T11): the hoist survives the whole suite in
 * both configurations. The PAIRED mutation is what establishes that this is an
 * equivalence and not an untested line — hoist the comparison AND move
 * `cq_reg_check_operands` below these calls, and the death case goes red.
 * Do not "simplify" this into the hoisted form to match the survivor: it would
 * be correct only for as long as the operand check stays where it is. */
static void tpl_src(cq_ctx *ctx, int32_t h, uint32_t w)
{
    uint32_t got;
    (void)cq_reg_cbits(&ctx->regs, h);
    got = cq_reg_width(&ctx->regs, h);
    if (got != w) cq_tpl_die(TPL_WIDTH, h, got);
}

/* IT RETURNS THE POINTER, SO `out` IS RESOLVED EXACTLY ONCE ON EVERY PATH, and
 * that is what makes the WRITE door STRUCTURAL rather than a convention. An
 * earlier draft checked the width here and resolved `out` again at step 5; a
 * mutant swapping THIS door to `cq_reg_cbits` then SURVIVED the whole suite,
 * because the second resolve caught the measured rail one line later with the
 * same M07 message. With one resolve the swap does not compile at all — the
 * const qualifier refuses it — which is `emit.h`'s move (a `const cq_bit *`
 * control cannot be materialised BY CONSTRUCTION) applied here.
 *
 * The pointer is then held across D7b's mint, which is safe and is the one
 * ordering fact worth stating rather than a rule of thumb: a rail's `bits`
 * array is its OWN allocation and is stable for the register's life (reg.h);
 * only `t->slot` is realloc'd on growth, so what must never be held across a
 * mint is a `cq_reg *`, which the public API never exposes. */
static cq_bit *tpl_out(cq_ctx *ctx, int32_t h, uint32_t w)
{
    cq_bit *b = cq_reg_bits(&ctx->regs, h);
    const uint32_t got = cq_reg_width(&ctx->regs, h);

    if (got != w) cq_tpl_die(TPL_WIDTH, h, got);
    return b;
}

/* One binary-or-compare call. `a_h`/`b_h` are CQ_REG_NONE on the lane that
 * carries a literal; `out` is CQ_REG_NONE on a forward symbol, which mints one;
 * `ctrl` is CQ_REG_NONE on an uncontrolled one. */
typedef struct {
    cq_kernel_fn k;
    uint32_t w, wout;
    int32_t  a_h, b_h;
    uint64_t lo, hi;
    int32_t  out, ctrl;
    /* THE TEMPLATE'S OPCODE IDENTITY, for D15's twin match. It is what makes a
     * forward and its `_unc` the SAME operation rather than merely two calls
     * naming the same rail: `cq_shim_bin_qq(ADD,32,a,b)` and
     * `cq_shim_bin_qq_unc(SUB,32,out,a,b)` name identical handles and are not a
     * pair. It is NOT `r.k`: the compare families all share one kernel slot
     * shape and two predicates can collide there, and a function pointer is not
     * a stable identity across a rebuild anyway. */
    uint32_t tag;
    /* THE DISPLAY NAME, for D21's `op begin` payload. It is the DISPATCH half's
     * (cq_template_dispatch.h) for the seam's own reason — a name set that grows
     * with the yaml, not with a decision — and it rides here rather than being
     * re-derived because `tpl_binary` has the opcode only as a kernel pointer by
     * then, and two predicates can share a kernel slot shape. */
    const char *name;
} tpl_req;

typedef struct {
    cq_ctx *ctx;
    cq_kernel_fn k;
    cq_bit *dst;
    const cq_bit *a, *b;
    int W;
} tpl_body;

/* A `cq_reg_xor_into` is exactly the ABI's `cqrt_copy_<W>(src, dst)` — `dst ^=
 * src`, self-adjoint — so it records as one. The argument ORDER is inverted
 * between the two (the ABI is (src, dst) and ours is (dst, src)) and both sides
 * are int32_t, which is why this wrapper exists rather than an inline record at
 * each site: one place to get the order right. */
static void rec_copy(int32_t src, int32_t dst)
{
    cq_call_rec c;
    memset(&c, 0, sizeof c);
    c.op   = (uint16_t)CQ_ROP_COPY;
    c.h[0] = src; c.h[1] = dst; c.h[2] = CQ_REG_NONE;
    c.ctrl = CQ_REG_NONE;
    cq_rec_push(&c);
}

static void tpl_run_kernel(void *p)
{
    const tpl_body *g = (const tpl_body *)p;
    g->k(g->ctx, g->dst, g->a, g->b, g->W);
}

/* THE ORDERED CALL SEQUENCE. Every binary and compare entry point is this
 * function; the order of its seven steps is the whole of the boundary and each
 * step is here because getting it wrong is silent.
 *
 * 2. `n_handles`, NOT `n_operands`. A LITERAL HAS NO HANDLE, and passing 2 for
 *    an `_hl` shape hands `cq_reg_check_operands` an uninitialised slot to test
 *    for liveness and for D7a. (bd 216 checklist 6.)
 *
 * 3. D7b's copy is emitted BEFORE the region is opened, and step 6's un-copy
 *    AFTER it is closed. `cq_reg_xor_into` is a loop of `cq_emit_cx`, so inside
 *    an open region with a CQ_BIT_Q control it routes to
 *    `cq_ctrl_promote_cx` -> `cq_emit_ccx_phys`: a copy emitted after the push
 *    makes the temporary `ctrl AND a` rather than `a`, and an un-copy emitted
 *    before the pop leaves it holding `a XOR (ctrl AND a)` — which is `a` on
 *    the ctrl = 0 branch, i.e. a scratch rail that does not return to |0>.
 *    BOTH HALVES ARE UNCONDITIONAL BECAUSE THE TEMPORARY IS SCRATCH: Rule 2
 *    says a routine returns its scratch to |0> before it returns, and a
 *    conditional compute needs a conditional uncompute to match it. Doing both
 *    INSIDE the region is also correct — |ctrl=0> keeps the temporary at |0>
 *    and |ctrl=1> pairs the two halves — and it is REJECTED on cost: every
 *    CX becomes a Toffoli, and a constant-ONE lane cannot be rewritten in place
 *    under a quantum control (PRD §9 row A), so it becomes a wire and the copy
 *    costs qubits it did not cost outside. `bd 493`'s note prescribes the
 *    un-copy "after the kernel but before cq_ctrl_pop"; that is the one
 *    placement of the three that is WRONG, and the aliased-controlled case in
 *    tests/test_template_d7.inc is what measures it rather than arguing it.
 *
 * 4. MINT `dst` AT THE RESULT WIDTH, which is not the operand width for two of
 *    the three shapes: `icmp` is one bit (`ir_types.jl:79`, and
 *    `opcode_table.yaml:222-223` gives it `result_type: flag_handle`) and a
 *    cast is `to_bits`. (bd 216 checklist 7.)
 *
 * 5. `out` IS RESOLVED EXACTLY ONCE ON EVERY PATH — by `tpl_out` when the ABI
 *    named it, at the mint when it did not — which is what makes the WRITE
 *    door structural: a second resolve would let the door be swapped for the
 *    read one and still refuse a measured rail, one line later and by
 *    coincidence. Holding that pointer across D7b's mint is safe: a rail's
 *    `bits` array is its own allocation and is stable for the register's life,
 *    and what must never be held across a mint is a `cq_reg *`.
 *
 * 7. THE FREE IS OUTSIDE THE REGION TOO, and for a different reason from the
 *    un-copy's: `shim/cq_shim_ctx.c` records that NO deallocation of any kind
 *    may happen between the push and the pop, because `cq_ctrl_push` copies the
 *    control BIT and a recycled index makes every later promoted gate control
 *    off a wire someone else now owns. */
static int32_t tpl_binary(tpl_req r)
{
    cq_ctx  *ctx = cq_shim_ctx();
    cq_bit   lit[CQ_REG_WIDTH_MAX];
    int32_t  srcs[2];
    uint32_t n = 0u;
    int32_t  tmp = CQ_REG_NONE, tmp_src = CQ_REG_NONE;
    cq_bit  *dst = NULL;
    int      is_unc;
    tpl_body g;

    if (r.a_h != CQ_REG_NONE) srcs[n++] = r.a_h;
    if (r.b_h != CQ_REG_NONE) srcs[n++] = r.b_h;
    cq_reg_check_operands(&ctx->regs, r.out, srcs, n);

    if (r.a_h != CQ_REG_NONE) tpl_src(ctx, r.a_h, r.w);
    if (r.b_h != CQ_REG_NONE) tpl_src(ctx, r.b_h, r.w);
    if (r.out != CQ_REG_NONE) dst = tpl_out(ctx, r.out, r.wout);

    /* D21's ANNOTATION BRACKET, OPENED HERE AND NOT THREE STATEMENTS LOWER.
     * D7b's defensive copy is a loop of `cq_emit_cx` and runs BEFORE the mint,
     * so a bracket opened after the mint would leave those gates outside every
     * bracket — a FATAL parse error for the viewer, not a cosmetic gap. That is
     * why the forward path names an output handle it has not minted yet:
     * `cq_reg_alloc_zero` returns `cq_reg_count` and increments (reg.c), and the
     * aliased path mints the temporary FIRST, so the result rail is
     * `count + alias` exactly. The prediction is not defended by an assert but
     * by execution — tests/test_shim_trace.c compares the emitted `out=` token
     * against the handle the call returns, at both shapes. */
    cq_trace_op_tpl(r.name, r.out != CQ_REG_NONE, r.ctrl, r.a_h, r.b_h,
                    r.out != CQ_REG_NONE
                        ? r.out
                        : cq_reg_count(&ctx->regs) + cq_reg_sources_alias(srcs, n));

    if (cq_reg_sources_alias(srcs, n)) {
        /* `r.a_h` HERE IS A GENUINELY EQUIVALENT MUTANT and is recorded rather
         * than defended against, on M09's `0xAA` precedent: at arity 2 the
         * predicate reports 1 only when the two handles are EQUAL, so which one
         * is copied is not a choice. It is spelled `b_h` because `b_h` is the
         * lane that is then redirected to the temporary. */
        tmp_src = r.b_h;
        tmp     = cq_reg_alloc_zero(&ctx->regs, r.w);
        /* THE TEMPORARY IS A HANDLE CQ_lang NEVER SEES, and it is exactly what
         * D15 means by "since it was MINTED is not since its cqrt_alloc". It is
         * recorded like any other rail; without the mint the certificate has no
         * history for it and it strands on EVERY aliased call. */
        cq_rec_mint(tmp, r.w, 0u, 0u, 0);
        cq_reg_xor_into(ctx, tmp, tmp_src);
        /* THE COPY IS RECORDED, AND THAT IS NOT BOOKKEEPING. An UNRECORDED
         * write is the one direction this whole file must not err in: the
         * certificate would see an EMPTY history, reduce it vacuously, read a
         * zero birth value and answer CLEAN — releasing a rail it never
         * examined. That is the "always yes" degeneration `bd 06t` names a
         * negative control against, arriving through omission rather than
         * through a wrong rule. */
        rec_copy(tmp_src, tmp);
        r.b_h   = tmp;
    }

    /* IS THIS A FORWARD OR AN `_unc`? The discriminator is `r.out`, and it must
     * be read HERE — the next three lines overwrite it with the mint. All six
     * `_unc` entry points set `r.out` and then call this same function, so
     * there is no flag to read and no parameter to add. */
    is_unc = (r.out != CQ_REG_NONE);

    if (r.out == CQ_REG_NONE) {
        r.out = cq_reg_alloc_zero(&ctx->regs, r.wout);
        dst   = cq_reg_bits(&ctx->regs, r.out);
        /* MINTED AT |0>, WHICH IS WHY U1 CLEARS AND ckd.18 CONVICTS. The birth
         * value is the whole of the port's divergence from upstream's
         * obligation (cq_shim_reduce.h, divergence (a)). */
        cq_rec_mint(r.out, r.wout, 0u, 0u, 1);
    }

    /* `cq_bits_from_words` truncates by construction — it reads exactly `width`
     * bits — so a sign-extended negative literal needs no separate mask here,
     * and none should be added (bd 216 checklist 13). */
    if (r.a_h == CQ_REG_NONE || r.b_h == CQ_REG_NONE)
        cq_bits_from_words(lit, r.w, r.lo, r.hi);

    g.ctx = ctx;
    g.k   = r.k;
    g.dst = dst;
    g.a   = (r.a_h == CQ_REG_NONE) ? lit : cq_reg_cbits(&ctx->regs, r.a_h);
    g.b   = (r.b_h == CQ_REG_NONE) ? lit : cq_reg_cbits(&ctx->regs, r.b_h);
    g.W   = (int)r.w;

    if (r.ctrl == CQ_REG_NONE) tpl_run_kernel(&g);
    else                       cq_shim_region(r.ctrl, tpl_run_kernel, &g);

    /* THE ONE RECORD FOR THIS CALL, AND ITS PLACEMENT IS THE WHOLE OF U1.
     * A forward writes `dst ^= f(srcs)` into a rail it just minted at |0>; its
     * `_unc` writes `dst ^= f(srcs)` again. Recording BOTH as writes to `out`
     * with each other as declared twins is what lets ONE engine pair them —
     * which is D15 §2's "there is one reduction engine, not three rules", built
     * rather than restated. An implementation that matched `_unc`s to forwards
     * in a separate checker would be the three-checker shape that gets U1 wrong.
     *
     * THE SOURCE HANDLES GO IN THE RECORD, so the engine's operand-stability
     * check covers them for free: that is upstream's T2 ("an `_inv` recomputes
     * f from the live sources; if a source moved in between it recomputes a
     * DIFFERENT value and leaves the rail dirty") arriving as a consequence
     * rather than as a rule.
     *
     * `r.b_h` IS THE POST-D7b LANE, deliberately: when the sources aliased, the
     * kernel really did read the temporary, and a record naming the original
     * would describe a call that did not happen.
     *
     * AND THE §9 CONTEXT IS RECORDED (D15 §6(i)) — an uncontrolled `_unc` after
     * a controlled forward leaves `dst` at `ctrl · f`, not zero, so a pair whose
     * halves ran under different regions is not a pair. */
    {
        cq_call_rec c;
        memset(&c, 0, sizeof c);
        c.op   = (uint16_t)(is_unc ? CQ_ROP_TPL_UNC : CQ_ROP_TPL_FWD);
        c.h[0] = r.out; c.h[1] = r.a_h; c.h[2] = r.b_h;
        c.imm  = r.lo ^ r.hi;   /* the `_hl`/`_lh` literal, in the identity */
        c.ctrl = r.ctrl;
        c.tag  = r.tag;
        cq_rec_push(&c);
    }

    if (tmp != CQ_REG_NONE) {
        cq_reg_xor_into(ctx, tmp, tmp_src);
        rec_copy(tmp_src, tmp);
        cq_reg_free(ctx, tmp, cq_shim_free_proof);
        cq_rec_retire(tmp);
    }
    /* CLOSED AFTER THE UN-COPY AND THE TEMPORARY'S FREE. The temporary is
     * WORKSPACE (handoff §6 rule 3) — a handle CQ_lang never sees, left out of
     * every `qubits=` list and shown as an extra lane inside this op — and its
     * gates are this operation's, so they belong inside this operation's
     * bracket. */
    cq_trace_end();
    return r.out;
}

static tpl_req tpl_bin_req(cq_shim_op op, int bits, int32_t a_h, int32_t b_h)
{
    tpl_req r;
    r.k = cq_tpl_bin_kernel(op);
    r.w = r.wout = tpl_width(bits);
    r.a_h = a_h; r.b_h = b_h;
    r.lo = r.hi = 0u;
    r.out = r.ctrl = CQ_REG_NONE;
    /* The width is folded in because the ABI's `_unc` carries it too, and a
     * forward at i32 is not the adjoint of an `_unc` at i64 on the same
     * handles. The `+ 1` keeps the binary family clear of the compare family's
     * tags below, which start from the same enumerator space. */
    r.tag = (uint32_t)op * 1024u + r.w + 1u;
    r.name = cq_tpl_bin_name(op);
    return r;
}

/* The RESULT WIDTH IS ONE BIT AND THE OPERAND WIDTH IS NOT — the one place the
 * two differ inside `tpl_req`, and the reason `wout` exists at all. */
static tpl_req tpl_cmp_req(cq_shim_pred p, int bits, int32_t a_h, int32_t b_h)
{
    tpl_req r = tpl_bin_req(CQ_SHIM_OP_ADD, bits, a_h, b_h);
    r.k    = cq_tpl_cmp_kernel(p);
    r.wout = 1u;
    /* A DISJOINT TAG SPACE FROM THE BINARY FAMILY, not an overlapping one:
     * `tpl_cmp_req` builds on a binary request whose tag already encodes
     * CQ_SHIM_OP_ADD, so without this line every predicate would share `add`'s
     * identity and `icmp_eq_unc` would pair with an `add` forward. */
    r.tag  = 0x40000000u + (uint32_t)p * 1024u + r.w + 1u;
    r.name = cq_tpl_cmp_name(p);
    return r;
}

/* --- the fifteen entry points --------------------------------------------- */

int32_t cq_shim_bin_qq(cq_shim_op op, int bits, int32_t a_handle, int32_t b_handle)
{
    return tpl_binary(tpl_bin_req(op, bits, a_handle, b_handle));
}

int32_t cq_shim_bin_hl(cq_shim_op op, int bits, int32_t a_handle,
                       uint64_t lo, uint64_t hi)
{
    tpl_req r = tpl_bin_req(op, bits, a_handle, CQ_REG_NONE);
    r.lo = lo; r.hi = hi;
    return tpl_binary(r);
}

int32_t cq_shim_bin_lh(cq_shim_op op, int bits, uint64_t lo, uint64_t hi,
                       int32_t b_handle)
{
    tpl_req r = tpl_bin_req(op, bits, CQ_REG_NONE, b_handle);
    r.lo = lo; r.hi = hi;
    return tpl_binary(r);
}

void cq_shim_bin_qq_unc(cq_shim_op op, int bits, int32_t out_handle,
                        int32_t a_handle, int32_t b_handle)
{
    tpl_req r = tpl_bin_req(op, bits, a_handle, b_handle);
    r.out = out_handle;
    (void)tpl_binary(r);
}

void cq_shim_bin_hl_unc(cq_shim_op op, int bits, int32_t out_handle,
                        int32_t a_handle, uint64_t lo, uint64_t hi)
{
    tpl_req r = tpl_bin_req(op, bits, a_handle, CQ_REG_NONE);
    r.out = out_handle; r.lo = lo; r.hi = hi;
    (void)tpl_binary(r);
}

void cq_shim_bin_lh_unc(cq_shim_op op, int bits, int32_t out_handle,
                        uint64_t lo, uint64_t hi, int32_t b_handle)
{
    tpl_req r = tpl_bin_req(op, bits, CQ_REG_NONE, b_handle);
    r.out = out_handle; r.lo = lo; r.hi = hi;
    (void)tpl_binary(r);
}

int32_t cq_shim_bin_qq_ctrl(cq_shim_op op, int bits, int32_t ctrl_flag,
                            int32_t a_handle, int32_t b_handle)
{
    tpl_req r = tpl_bin_req(op, bits, a_handle, b_handle);
    r.ctrl = ctrl_flag;
    return tpl_binary(r);
}

int32_t cq_shim_bin_hl_ctrl(cq_shim_op op, int bits, int32_t ctrl_flag,
                            int32_t a_handle, uint64_t lo, uint64_t hi)
{
    tpl_req r = tpl_bin_req(op, bits, a_handle, CQ_REG_NONE);
    r.ctrl = ctrl_flag; r.lo = lo; r.hi = hi;
    return tpl_binary(r);
}

int32_t cq_shim_bin_lh_ctrl(cq_shim_op op, int bits, int32_t ctrl_flag,
                            uint64_t lo, uint64_t hi, int32_t b_handle)
{
    tpl_req r = tpl_bin_req(op, bits, CQ_REG_NONE, b_handle);
    r.ctrl = ctrl_flag; r.lo = lo; r.hi = hi;
    return tpl_binary(r);
}

int32_t cq_shim_icmp_qq(cq_shim_pred pred, int bits, int32_t a_handle,
                        int32_t b_handle)
{
    return tpl_binary(tpl_cmp_req(pred, bits, a_handle, b_handle));
}

int32_t cq_shim_icmp_hl(cq_shim_pred pred, int bits, int32_t a_handle,
                        uint64_t lo, uint64_t hi)
{
    tpl_req r = tpl_cmp_req(pred, bits, a_handle, CQ_REG_NONE);
    r.lo = lo; r.hi = hi;
    return tpl_binary(r);
}

void cq_shim_icmp_qq_unc(cq_shim_pred pred, int bits, int32_t out_handle,
                         int32_t a_handle, int32_t b_handle)
{
    tpl_req r = tpl_cmp_req(pred, bits, a_handle, b_handle);
    r.out = out_handle;
    (void)tpl_binary(r);
}

void cq_shim_icmp_hl_unc(cq_shim_pred pred, int bits, int32_t out_handle,
                         int32_t a_handle, uint64_t lo, uint64_t hi)
{
    tpl_req r = tpl_cmp_req(pred, bits, a_handle, CQ_REG_NONE);
    r.out = out_handle; r.lo = lo; r.hi = hi;
    (void)tpl_binary(r);
}

/* A CAST IS ARITY-1, SO IT HAS NEITHER A D7b LANE NOR A CONTROLLED AXIS, and it
 * is the second shape whose result width is not its operand width. It does not
 * go through `tpl_binary`: its kernel takes `(F, T)` where a binary kernel
 * takes `W`, and widening `cq_kernel_fn` to reach it is exactly what Rule 7
 * forbids. D7a is still checked, because `_unc` names an `out`. */
static int32_t tpl_cast(cq_shim_cast_kind kind, int from_bits, int to_bits,
                        int32_t out, int32_t a_handle)
{
    cq_ctx  *ctx = cq_shim_ctx();
    uint32_t f = tpl_width(from_bits), t = tpl_width(to_bits);
    int32_t  srcs[1];
    int      is_unc = 1;

    cq_bit  *dst;

    srcs[0] = a_handle;
    cq_reg_check_operands(&ctx->regs, out, srcs, 1u);
    tpl_src(ctx, a_handle, f);

    /* Arity 1, so there is no D7b lane and no §9 axis; the predicted handle is
     * `cq_reg_count` with no alias term. */
    cq_trace_op_tpl(cq_tpl_cast_name(kind), out != CQ_REG_NONE, CQ_REG_NONE,
                    a_handle, CQ_REG_NONE,
                    out != CQ_REG_NONE ? out : cq_reg_count(&ctx->regs));

    if (out != CQ_REG_NONE) {
        dst = tpl_out(ctx, out, t);
    } else {
        out = cq_reg_alloc_zero(&ctx->regs, t);
        dst = cq_reg_bits(&ctx->regs, out);
        cq_rec_mint(out, t, 0u, 0u, 1);
        is_unc = 0;
    }

    cq_tpl_cast_kernel(kind)(ctx, dst, cq_reg_cbits(&ctx->regs, a_handle),
                             (int)f, (int)t);

    /* THE SAME TWIN SHAPE AS THE BINARY FAMILY, with `b` absent. A cast's tag
     * folds BOTH widths in: `zext i8->i32` is not the adjoint of `zext i8->i64`
     * on the same handles, and `trunc` is not `zext`'s. */
    {
        cq_call_rec c;
        memset(&c, 0, sizeof c);
        c.op   = (uint16_t)(is_unc ? CQ_ROP_TPL_UNC : CQ_ROP_TPL_FWD);
        c.h[0] = out; c.h[1] = a_handle; c.h[2] = CQ_REG_NONE;
        c.ctrl = CQ_REG_NONE;
        c.tag  = 0x80000000u + (uint32_t)kind * 65536u + f * 256u + t;
        cq_rec_push(&c);
    }
    cq_trace_end();
    return out;
}

int32_t cq_shim_cast(cq_shim_cast_kind kind, int from_bits, int to_bits,
                     int32_t a_handle)
{
    return tpl_cast(kind, from_bits, to_bits, CQ_REG_NONE, a_handle);
}

void cq_shim_cast_unc(cq_shim_cast_kind kind, int from_bits, int to_bits,
                      int32_t out_handle, int32_t a_handle)
{
    (void)tpl_cast(kind, from_bits, to_bits, out_handle, a_handle);
}
