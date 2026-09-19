/* shim/cq_template_ternary.c — M26's ARITY-3 ordered call sequence and the
 * eight `cq_shim_fma_*` entry points. PRD-v2 §6.1's vendoring (bead 9ve.24),
 * §7.11's signature note, PRD §15 D7a/D7b, D15 and D21.
 *
 * A FOURTH SEAM OFF cq_template_impl.c, RECORDED IN cq_template_boundary.h
 * BEFORE IT WAS TAKEN, exactly as Wave 8 recorded ARITY-2 <-> ARITY-1. The
 * three sequences share every DOOR — `cq_tpl_width`, `cq_tpl_src`,
 * `cq_tpl_out`, `cq_reg_check_operands`, `cq_tpl_rec_copy`, `cq_shim_free_proof`
 * — and differ only in how many operands they thread through them, which is
 * why this file re-implements none of them. A second copy of a guard is how a
 * deleted one keeps passing (this project's recorded trap, five times over).
 *
 * WHAT ARITY 3 CHANGES, AND IT IS FOUR THINGS.
 *
 * (1) TWO LITERAL LANES. `tpl_req` carries ONE `(lo, hi)` pair and `qll` needs
 *     two, so `tpl3_req` carries one per lane and this file builds TWO `cq_bit`
 *     buffers. One shared buffer would make `fma(a, 2.0, 3.0)` compute
 *     `fma(a, 3.0, 3.0)`: same shape, same gate count, same pool, wrong value,
 *     and only L1 sees it.
 *
 * (2) D7b HAS SIX PAIRS AND `cq_reg_sources_alias` ANSWERS 0/1. At arity 2 that
 *     predicate is exact — a 1 means the two handles are equal and WHICH lane
 *     is copied is not a choice — and `cq_tpl_binary` records that as an
 *     equivalent mutant. At arity 3 it is not: `a == b` with a distinct `c`
 *     needs ONE temporary and `a == b == c` needs TWO, and the predicate cannot
 *     tell them apart. So this file does its own pairwise scan, and the D21
 *     prediction is `cq_reg_count + n_temps` rather than the binary door's
 *     `+ (0 or 1)`.
 *
 * (3) NO §9 REGION. `intrinsic_table.yaml`'s `fma` row has variants
 *     `[fwd_qqq, fwd_qql, fwd_qlq, fwd_qll, unc_*]` and nothing else — no
 *     `controlled`, no `inv` — so there is no control flag to push and no D14
 *     abort to owe. `cq_shim_region` is therefore absent here rather than
 *     passed `CQ_REG_NONE`, which that function treats as a use-after-free.
 *
 * (4) D15's RECORD FILLS ALL FOUR SLOTS. `CQ_REC_SLOTS` is 4 and a ternary
 *     `{out, a, b, c}` is exactly that, with nothing spare — which is why
 *     `CQ_ROP_TPL_FWD`/`TPL_UNC` had to start READING slot 3 (see
 *     shim/cq_shim_record.c). Without that widening `c` never bumps
 *     `last_read`, so a forward, an `X` on `c` and the `_unc` reduce to
 *     identity and RELEASE a rail the two halves no longer cancel on.
 *
 * THE TAG SPACE IS THE EIGHTH, `0x50000000`, and the arithmetic was re-checked
 * against the boundary header's table rather than assumed — that header's own
 * instruction. Sharing a space is a SILENT RELEASE OF A DIRTY RAIL (bd memory
 * a-template-familys-tag-space-is-d15s-twin-identity), and the shape is exact
 * here: `cq_shim_fma_qqq` and `cq_shim_fbin_qq_unc` would agree on opcode pair,
 * width and handles if `fma`'s enumerator and `fadd`'s were both index 0 and
 * the prefixes matched.
 */

#include "cq_shim.h"
#include "cq_shim_ctx.h"
#include "cq_shim_proof.h"
#include "cq_shim_record.h"
#include "cq_shim_trace.h"
#include "cq_template_boundary.h"

#include "ctx.h"
#include "reg.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

enum { TPL3_LANES = 3 };

static void cq_t3_die(const char *what, int v)
{
    fprintf(stderr, "libcqops: FATAL: shim: %s (%d)\n", what, v);
    abort();
}

/* HOISTED, for cq_template_fparith.c's reason verbatim: `cq_kernel_fma`
 * hard-errors on a width that is not 64 in BOTH configurations — one layer
 * down, AFTER this sequence has minted a rail and opened a D21 bracket that is
 * then never closed, which is a fatal parse error for the viewer rather than a
 * cosmetic gap. The message is DISJOINT from every other family's so a death
 * case can say which layer spoke. */
static uint32_t fma_width(int bits)
{
    if (bits != 64)
        cq_t3_die("the fp ternary symbol names a width v2 does not implement; "
                  "PRD-v2 section 1 scopes the fp port to f64 and soft_fma "
                  "takes three UInt64", bits);
    return 64u;
}

static tpl3_req fma_req(cq_shim_fma_op op, int bits, int32_t a_h, int32_t b_h,
                        int32_t c_h)
{
    tpl3_req r;

    r.w = fma_width(bits);
    r.a_h = a_h; r.b_h = b_h; r.c_h = c_h;
    r.b_lo = r.b_hi = r.c_lo = r.c_hi = 0u;
    r.out = CQ_REG_NONE;
    /* The EIGHTH tag space — see cq_template_boundary.h's re-checked table. */
    r.tag = 0x50000000u + (uint32_t)op * 1024u + r.w + 1u;
    r.name = cq_tpl_fma_name(op);
    r.k = cq_tpl_fma_kernel(op);
    return r;
}

/* --- the sequence --------------------------------------------------------- */

int32_t cq_tpl_ternary(tpl3_req r)
{
    cq_ctx  *ctx = cq_shim_ctx();
    cq_bit   lit_b[CQ_REG_WIDTH_MAX], lit_c[CQ_REG_WIDTH_MAX];
    int32_t *lane[TPL3_LANES];
    int32_t  srcs[TPL3_LANES], tmp[TPL3_LANES], tmp_src[TPL3_LANES];
    uint32_t n = 0u, n_tmp = 0u;
    cq_bit  *dst = NULL;
    const cq_bit *op[TPL3_LANES];
    int      is_unc;

    lane[0] = &r.a_h; lane[1] = &r.b_h; lane[2] = &r.c_h;

    /* (2) D7a, over the HANDLES only — a literal lane contributes nothing to
     * `srcs`, which is `n_handles` and not `n_operands` (cq_template_impl.c). */
    for (int i = 0; i < TPL3_LANES; i++)
        if (*lane[i] != CQ_REG_NONE) srcs[n++] = *lane[i];
    cq_reg_check_operands(&ctx->regs, r.out, srcs, n);

    /* (3) The width doors. The READ door admits a measured rail; the WRITE door
     * refuses one and resolves `out` exactly once. */
    for (int i = 0; i < TPL3_LANES; i++)
        if (*lane[i] != CQ_REG_NONE) cq_tpl_src(ctx, *lane[i], r.w);
    if (r.out != CQ_REG_NONE) dst = cq_tpl_out(ctx, r.out, r.w);

    /* (4) D21's bracket, OPENED BEFORE D7b's `cq_emit_cx` loop — those gates
     * must fall inside a bracket or the viewer's parse fails. The predicted
     * result handle counts the temporaries this call is ABOUT to mint, which
     * needs the pairwise scan below run first. `cq_trace_op` is called directly
     * rather than through `cq_trace_op_tpl` because that helper has two source
     * slots and this family has three; the frame has exactly three in-slots and
     * an uncontrolled ternary is the shape that fills them. */
    for (int i = 1; i < TPL3_LANES; i++)
        for (int j = 0; j < i; j++)
            if (*lane[i] != CQ_REG_NONE && *lane[i] == *lane[j]) { n_tmp++; break; }
    {
        char nm[48];

        snprintf(nm, sizeof nm, "%s%s", r.name,
                 r.out != CQ_REG_NONE ? "_unc" : "");
        cq_trace_op(nm, r.a_h, r.b_h, r.c_h,
                    r.out != CQ_REG_NONE ? r.out
                                         : cq_reg_count(&ctx->regs)
                                           + (int32_t)n_tmp,
                    CQ_REG_NONE);
    }

    /* (5) D7b. A lane equal to an EARLIER lane is redirected to a fresh
     * temporary holding the same value, so the kernel sees three disjoint
     * spans — which its `cq_kernel_check_n` requires in both configurations.
     * The copy is emitted here and un-copied at the end; both halves are
     * recorded so the certificate has a cancelling pair for the temporary. */
    n_tmp = 0u;
    for (int i = 1; i < TPL3_LANES; i++) {
        int dup = 0;

        if (*lane[i] == CQ_REG_NONE) continue;
        for (int j = 0; j < i; j++) if (*lane[i] == *lane[j]) dup = 1;
        if (!dup) continue;
        tmp_src[n_tmp] = *lane[i];
        tmp[n_tmp] = cq_reg_alloc_zero(&ctx->regs, r.w);
        cq_rec_mint(tmp[n_tmp], r.w, 0u, 0u, 0);
        cq_reg_xor_into(ctx, tmp[n_tmp], tmp_src[n_tmp]);
        cq_tpl_rec_copy(tmp_src[n_tmp], tmp[n_tmp]);
        *lane[i] = tmp[n_tmp];
        n_tmp++;
    }

    /* (6) Latched BEFORE the mint overwrites `r.out`. */
    is_unc = (r.out != CQ_REG_NONE);
    if (r.out == CQ_REG_NONE) {
        r.out = cq_reg_alloc_zero(&ctx->regs, r.w);
        dst   = cq_reg_bits(&ctx->regs, r.out);
        cq_rec_mint(r.out, r.w, 0u, 0u, 1);
    }

    /* (7) The literal lanes, ONE BUFFER EACH. `cq_bits_from_words` truncates to
     * the width by construction, so there is no separate mask. */
    if (r.b_h == CQ_REG_NONE) cq_bits_from_words(lit_b, r.w, r.b_lo, r.b_hi);
    if (r.c_h == CQ_REG_NONE) cq_bits_from_words(lit_c, r.w, r.c_lo, r.c_hi);
    op[0] = cq_reg_cbits(&ctx->regs, r.a_h);
    op[1] = (r.b_h == CQ_REG_NONE) ? lit_b : cq_reg_cbits(&ctx->regs, r.b_h);
    op[2] = (r.c_h == CQ_REG_NONE) ? lit_c : cq_reg_cbits(&ctx->regs, r.c_h);

    /* (8) The kernel. No §9 region: this family has no controlled axis. */
    r.k(ctx, dst, op[0], op[1], op[2], (int)r.w);

    /* (9) D15's record, with the POST-D7b lanes — the handles the kernel
     * actually read, which is `cq_tpl_binary`'s convention and is what lets a
     * forward and its `_unc` pair on identical slots. */
    {
        cq_call_rec c;

        memset(&c, 0, sizeof c);
        c.op   = (uint16_t)(is_unc ? CQ_ROP_TPL_UNC : CQ_ROP_TPL_FWD);
        c.h[0] = r.out; c.h[1] = r.a_h; c.h[2] = r.b_h; c.h[3] = r.c_h;
        c.imm  = r.b_lo ^ r.b_hi ^ r.c_lo ^ r.c_hi;
        c.ctrl = CQ_REG_NONE;
        c.tag  = r.tag;
        cq_rec_push(&c);
    }

    /* (10) The un-copies, in reverse, then the frees through the ONE proof
     * door every rail goes through. */
    while (n_tmp > 0u) {
        n_tmp--;
        cq_reg_xor_into(ctx, tmp[n_tmp], tmp_src[n_tmp]);
        cq_tpl_rec_copy(tmp_src[n_tmp], tmp[n_tmp]);
        cq_reg_free(ctx, tmp[n_tmp], cq_shim_free_proof);
        cq_rec_retire(tmp[n_tmp]);
    }
    cq_trace_end();
    return r.out;
}

/* --- the eight entry points ----------------------------------------------- */

int32_t cq_shim_fma_qqq(cq_shim_fma_op op, int bits, int32_t a_handle,
                        int32_t b_handle, int32_t c_handle)
{
    return cq_tpl_ternary(fma_req(op, bits, a_handle, b_handle, c_handle));
}

int32_t cq_shim_fma_qql(cq_shim_fma_op op, int bits, int32_t a_handle,
                        int32_t b_handle, uint64_t c_lo, uint64_t c_hi)
{
    tpl3_req r = fma_req(op, bits, a_handle, b_handle, CQ_REG_NONE);

    r.c_lo = c_lo; r.c_hi = c_hi;
    return cq_tpl_ternary(r);
}

int32_t cq_shim_fma_qlq(cq_shim_fma_op op, int bits, int32_t a_handle,
                        uint64_t b_lo, uint64_t b_hi, int32_t c_handle)
{
    tpl3_req r = fma_req(op, bits, a_handle, CQ_REG_NONE, c_handle);

    r.b_lo = b_lo; r.b_hi = b_hi;
    return cq_tpl_ternary(r);
}

int32_t cq_shim_fma_qll(cq_shim_fma_op op, int bits, int32_t a_handle,
                        uint64_t b_lo, uint64_t b_hi,
                        uint64_t c_lo, uint64_t c_hi)
{
    tpl3_req r = fma_req(op, bits, a_handle, CQ_REG_NONE, CQ_REG_NONE);

    r.b_lo = b_lo; r.b_hi = b_hi; r.c_lo = c_lo; r.c_hi = c_hi;
    return cq_tpl_ternary(r);
}

void cq_shim_fma_qqq_unc(cq_shim_fma_op op, int bits, int32_t out_handle,
                         int32_t a_handle, int32_t b_handle, int32_t c_handle)
{
    tpl3_req r = fma_req(op, bits, a_handle, b_handle, c_handle);

    r.out = out_handle;
    (void)cq_tpl_ternary(r);
}

void cq_shim_fma_qql_unc(cq_shim_fma_op op, int bits, int32_t out_handle,
                         int32_t a_handle, int32_t b_handle,
                         uint64_t c_lo, uint64_t c_hi)
{
    tpl3_req r = fma_req(op, bits, a_handle, b_handle, CQ_REG_NONE);

    r.c_lo = c_lo; r.c_hi = c_hi; r.out = out_handle;
    (void)cq_tpl_ternary(r);
}

void cq_shim_fma_qlq_unc(cq_shim_fma_op op, int bits, int32_t out_handle,
                         int32_t a_handle, uint64_t b_lo, uint64_t b_hi,
                         int32_t c_handle)
{
    tpl3_req r = fma_req(op, bits, a_handle, CQ_REG_NONE, c_handle);

    r.b_lo = b_lo; r.b_hi = b_hi; r.out = out_handle;
    (void)cq_tpl_ternary(r);
}

void cq_shim_fma_qll_unc(cq_shim_fma_op op, int bits, int32_t out_handle,
                         int32_t a_handle, uint64_t b_lo, uint64_t b_hi,
                         uint64_t c_lo, uint64_t c_hi)
{
    tpl3_req r = fma_req(op, bits, a_handle, CQ_REG_NONE, CQ_REG_NONE);

    r.b_lo = b_lo; r.b_hi = b_hi; r.c_lo = c_lo; r.c_hi = c_hi;
    r.out = out_handle;
    (void)cq_tpl_ternary(r);
}
