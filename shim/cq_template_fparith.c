/* shim/cq_template_fparith.c — M26's fp ARITHMETIC surface (bead 9ve.36,
 * PRD-v2 §1 / §5 / §7.15).
 *
 * THE SEAM `shim/cq_template_fp.c`'s HEADER RECORDED, TAKEN WHEN IT FIRED:
 *
 *     the COMPARE family <-> the ARITHMETIC families
 *
 * on its own measurement — budget 120, and the thirteen entry points below are
 * ~145 counted lines — and on its own discriminator, which is a SUBJECT cut
 * rather than a size one: a compare's result is ONE BIT and it has no `_lh`
 * shape and no §9 controlled axis on ANY axis (opcode_table.yaml's own note),
 * while every family here has a 64-lane result, a literal on either side, and
 * a controlled grid. Read `shim/cq_template_boundary.h` before adding a
 * family.
 *
 * WHAT IS *NOT* HERE IS STILL THE POINT OF THE FILE. No handle resolution, no
 * D7a refusal, no D7b copy, no mint, no §9 region, no D15 record and no D21
 * bracket: the arity-2 families reach all of that through `cq_tpl_binary` and
 * the arity-1 ones through `cq_tpl_unary`, identically to the integer surface.
 * What a family owns is three things — its kernel, its display name, and a TAG
 * SPACE disjoint from every other family's.
 *
 * THE THREE NEW TAG SPACES ARE `0x10000000` (fp binary), `0x20000000` (fp
 * unary) and `0x30000000` (fp conversion), and the arithmetic that makes them
 * safe is in `cq_template_boundary.h` beside the full table. Sharing a space is
 * a SILENT RELEASE OF A DIRTY RAIL, not a tidiness question: `cq_shim_bin_qq(
 * ADD, 64, a, b)` and `cq_shim_fbin_qq_unc(FADD, 64, out, a, b)` agree on the
 * twin opcodes, the width, all four handle slots, the §9 context and the
 * parities, and both selectors are index 0 — only the prefix keeps them apart.
 *
 * WHY THE WIDTH REFUSALS ARE HOISTED HERE. `cq_kernel_fadd`, `cq_kernel_fdiv`,
 * `cq_kernel_fsqrt` and M37's four conversions all hard-error on a width they
 * do not ship, in BOTH configurations — but one layer down, AFTER the call has
 * minted a rail and opened a D21 bracket that is then never closed, which is a
 * fatal parse error for the viewer rather than a cosmetic gap. The refusal
 * belongs where the claim is about the CALL. Every message below is
 * deliberately DISJOINT from every other `shim:` string in this directory,
 * because the `FAIL_REGULAR_EXPRESSION` pins in tests/CMakeLists.txt
 * discriminate on the MESSAGE and not on the module.
 */

#include "cq_shim.h"

#include "cq_shim_ctx.h"
#include "cq_template_boundary.h"
#include "cq_template_dispatch.h"

#include "kernels/fconv.h"   /* cq_fconv_uitofp_widths — ASK, do not transcribe */
#include "reg.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* A FIFTH static with the house `shim:` prefix, on `src/reg_check.c`'s
 * precedent — two guards spelling the same sentence is how a deleted one keeps
 * passing, so every string here appears nowhere else. */
static void cq_fa_die(const char *what, int v)
{
    fprintf(stderr, "libcqops: FATAL: shim: %s (%d)\n", what, v);
    abort();
}

/* --- fp_arith, the four BINARY opcodes ------------------------------------ */

static uint32_t fbin_width(int bits)
{
    if (bits != 64)
        cq_fa_die("the fp arithmetic symbol names a width v2 does not "
                  "implement; PRD-v2 section 1 scopes the fp port to f64 and "
                  "every soft_fadd / soft_fsub / soft_fmul / soft_fdiv body "
                  "upstream takes two UInt64", bits);
    return 64u;
}

/* `wout` IS THE OPERAND WIDTH HERE, unlike both compare families: an `fadd`
 * returns an f64 and not a flag. `cq_tpl_req` already sets it that way, so the
 * only fields a family fills are the three it owns. */
static tpl_req fbin_req(cq_shim_fop op, int bits, int32_t a_h, int32_t b_h)
{
    tpl_req r = cq_tpl_req((int)fbin_width(bits), a_h, b_h);

    r.k    = cq_tpl_fbin_kernel(op);
    r.tag  = 0x10000000u + (uint32_t)op * 1024u + r.w + 1u;
    r.name = cq_tpl_fbin_name(op);
    return r;
}

int32_t cq_shim_fbin_qq(cq_shim_fop op, int bits, int32_t a_handle,
                        int32_t b_handle)
{
    return cq_tpl_binary(fbin_req(op, bits, a_handle, b_handle));
}

int32_t cq_shim_fbin_hl(cq_shim_fop op, int bits, int32_t a_handle,
                        uint64_t lo, uint64_t hi)
{
    tpl_req r = fbin_req(op, bits, a_handle, CQ_REG_NONE);

    r.lo = lo; r.hi = hi;
    return cq_tpl_binary(r);
}

int32_t cq_shim_fbin_lh(cq_shim_fop op, int bits, uint64_t lo, uint64_t hi,
                        int32_t b_handle)
{
    tpl_req r = fbin_req(op, bits, CQ_REG_NONE, b_handle);

    r.lo = lo; r.hi = hi;
    return cq_tpl_binary(r);
}

void cq_shim_fbin_qq_unc(cq_shim_fop op, int bits, int32_t out_handle,
                         int32_t a_handle, int32_t b_handle)
{
    tpl_req r = fbin_req(op, bits, a_handle, b_handle);

    r.out = out_handle;
    (void)cq_tpl_binary(r);
}

void cq_shim_fbin_hl_unc(cq_shim_fop op, int bits, int32_t out_handle,
                         int32_t a_handle, uint64_t lo, uint64_t hi)
{
    tpl_req r = fbin_req(op, bits, a_handle, CQ_REG_NONE);

    r.out = out_handle; r.lo = lo; r.hi = hi;
    (void)cq_tpl_binary(r);
}

void cq_shim_fbin_lh_unc(cq_shim_fop op, int bits, int32_t out_handle,
                         uint64_t lo, uint64_t hi, int32_t b_handle)
{
    tpl_req r = fbin_req(op, bits, CQ_REG_NONE, b_handle);

    r.out = out_handle; r.lo = lo; r.hi = hi;
    (void)cq_tpl_binary(r);
}

int32_t cq_shim_fbin_qq_ctrl(cq_shim_fop op, int bits, int32_t ctrl_flag,
                             int32_t a_handle, int32_t b_handle)
{
    tpl_req r = fbin_req(op, bits, a_handle, b_handle);

    r.ctrl = ctrl_flag;
    return cq_tpl_binary(r);
}

int32_t cq_shim_fbin_hl_ctrl(cq_shim_fop op, int bits, int32_t ctrl_flag,
                             int32_t a_handle, uint64_t lo, uint64_t hi)
{
    tpl_req r = fbin_req(op, bits, a_handle, CQ_REG_NONE);

    r.ctrl = ctrl_flag; r.lo = lo; r.hi = hi;
    return cq_tpl_binary(r);
}

int32_t cq_shim_fbin_lh_ctrl(cq_shim_fop op, int bits, int32_t ctrl_flag,
                             uint64_t lo, uint64_t hi, int32_t b_handle)
{
    tpl_req r = fbin_req(op, bits, CQ_REG_NONE, b_handle);

    r.ctrl = ctrl_flag; r.lo = lo; r.hi = hi;
    return cq_tpl_binary(r);
}

/* --- the CROSS-DOMAIN conversions ----------------------------------------- */

#define FCAST_TAG(kind, f, t) \
    (0x30000000u + (uint32_t)(kind) * 65536u + (uint32_t)(f) * 256u + (uint32_t)(t))

/* `uitofp`'s SHIPPED SOURCE WIDTHS ARE ASKED OF M37 rather than transcribed:
 * `cq_fconv_uitofp_widths` is the one place {1, 8, 16, 32} is written, so a
 * future width joining the kernel cannot leave the shim refusing it — and, more
 * to the point, i64 joining THAT list could not quietly enable the row this
 * file refuses by name one function down. */
static int fcast_uitofp_source_ships(uint32_t f)
{
    int n = 0;
    const int *ws = cq_fconv_uitofp_widths(&n);

    for (int i = 0; i < n; i++) if ((uint32_t)ws[i] == f) return 1;
    return 0;
}

/* THE SEVENTEEN PAIRS, K19.md §1.1's grid minus bead 9ve.34's one row. The two
 * fp->int kinds differ only in whether i1 is a target (`fptosi` excludes it —
 * there is no signed bool, and Clang uses `fptoui` for `_Bool`). */
static int fcast_pair_ships(cq_shim_fcast_kind k, uint32_t f, uint32_t t)
{
    const int narrow = (t == 8u || t == 16u || t == 32u || t == 64u);
    const int wide   = (f == 8u || f == 16u || f == 32u || f == 64u);

    switch (k) {
    case CQ_SHIM_FCAST_FPTOSI: return f == 64u && narrow;
    case CQ_SHIM_FCAST_FPTOUI: return f == 64u && (narrow || t == 1u);
    case CQ_SHIM_FCAST_SITOFP: return t == 64u && wide;
    case CQ_SHIM_FCAST_UITOFP: return t == 64u && fcast_uitofp_source_ships(f);
    }
    /* NO `default:` LABEL — `bd fna`'s rule: an enum that grows without its row
     * must break the BUILD, and a `default:` is what stops -Wswitch saying so.
     * The unreachable return is what C11 requires, and the bounds check in
     * `cq_tpl_fcast_kernel` has already run by the time this is reached. */
    return 0;
}

static tpl_ureq fcast_req(cq_shim_fcast_kind kind, int from_bits, int to_bits,
                          int32_t out, int32_t a_handle)
{
    tpl_ureq r;
    /* THE ORDER OF THESE FOUR CHECKS IS THE WHOLE OF THE FENCE. The RANGE goes
     * first so a wild width never reaches the `(uint32_t)` conversion or a
     * table index; the ENUM next, because `fcast_pair_ships` switches on it;
     * bead 9ve.34's row THIRD, because (64, 64) is also not a shipped `uitofp`
     * pair and the generic sentence would otherwise answer and hide the bead;
     * and the pair last. */
    const uint32_t f = cq_tpl_width(from_bits), t = cq_tpl_width(to_bits);
    const cq_tpl_cast_fn k = cq_tpl_fcast_kernel(kind);

    if (kind == CQ_SHIM_FCAST_UITOFP && f == 64u)
        cq_fa_die("uitofp from i64 is refused at the ABI edge, bead 9ve.34 and "
                  "PRD-v2 section 7.9 — upstream routes UIToFP to soft_sitofp "
                  "with no bias correction at this one width, so every "
                  "u >= 2^63 would convert as a negative number, and it must "
                  "never fall through to sitofp", (int)f);
    if (!fcast_pair_ships(kind, f, t))
        cq_fa_die("the cross-domain conversion names a width pair v2 does not "
                  "ship; seventeen pairs ship (PRD-v2 section 7.9): fptosi and "
                  "fptoui from f64, sitofp and uitofp to f64", (int)t);

    r.inner.fn = NULL; r.inner.f = r.inner.t = 0u; r.inner.tag = 0u;
    r.outer.fn = k; r.outer.f = f; r.outer.t = t;
    r.outer.tag = FCAST_TAG(kind, f, t);
    r.w = f; r.wout = t;
    r.a_h = a_handle; r.out = out;
    r.name = cq_tpl_fcast_name(kind);

    if ((kind == CQ_SHIM_FCAST_FPTOSI || kind == CQ_SHIM_FCAST_FPTOUI)
        && t != 64u) {
        /* NARROWING: the kernel at T = 64 into the workspace, then a `trunc`
         * into `dst`. `instructions.jl:7649-7658` emits the second instruction
         * itself; folding the narrowing into the copy-out would be a
         * re-derivation (Rule 1) and would also change the VALUE — the shipped
         * composition WRAPS (300.0 -> i8 is 44) where a saturating narrow
         * would clamp. */
        r.inner.fn  = k;
        r.inner.f   = 64u; r.inner.t = 64u;
        r.inner.tag = FCAST_TAG(kind, 64u, 64u);
        r.outer.fn  = cq_tpl_cast_kernel(CQ_SHIM_CAST_TRUNC);
        r.outer.f   = 64u; r.outer.t = t;
    } else if (kind == CQ_SHIM_FCAST_SITOFP && f != 64u) {
        /* WIDENING: a `sext` into the workspace, then the kernel at F = 64.
         * `:7677-7679` picks `:sext` for SIToFP specifically — a `zext` here
         * is bead 9ve.34's defect with the sign on the other side of the
         * fence, and it would convert every negative source as a large
         * positive. The inner tag is the INTEGER cast's, because the stage IS
         * a `sext i<F> -> i64`; it cannot collide with a real one, whose `out`
         * is a handle CQ_lang holds and never the workspace. */
        r.inner.fn  = cq_tpl_cast_kernel(CQ_SHIM_CAST_SEXT);
        r.inner.f   = f; r.inner.t = 64u;
        r.inner.tag = 0x80000000u + (uint32_t)CQ_SHIM_CAST_SEXT * 65536u
                      + f * 256u + 64u;
        r.outer.f   = 64u; r.outer.t = 64u;
    }
    /* `uitofp`'s four narrow rows take `F` DIRECTLY and need no workspace: the
     * widening there is WIRING inside M37 under PRD-v2 §7.3 — lanes [F, 64)
     * are CQ_BIT_ZERO entries of a read-only view, zero gates and zero qubits
     * — so there is nothing for a stage to do. */
    return r;
}

int32_t cq_shim_fcast(cq_shim_fcast_kind kind, int from_bits, int to_bits,
                      int32_t a_handle)
{
    return cq_tpl_unary(fcast_req(kind, from_bits, to_bits, CQ_REG_NONE,
                                  a_handle));
}

void cq_shim_fcast_unc(cq_shim_fcast_kind kind, int from_bits, int to_bits,
                       int32_t out_handle, int32_t a_handle)
{
    (void)cq_tpl_unary(fcast_req(kind, from_bits, to_bits, out_handle,
                                 a_handle));
}

/* --- the UNARY door -------------------------------------------------------- */

static uint32_t fun_width(int bits)
{
    if (bits != 64)
        cq_fa_die("the fp unary symbol names a width v2 does not implement; "
                  "PRD-v2 section 1 scopes the fp port to f64 and soft_fsqrt "
                  "takes one UInt64", bits);
    return 64u;
}

/* ONE STAGE, BOTH WIDTHS THE SAME. That is what lets `cq_tpl_fun_kernel`'s
 * adapter discard `T`: the two can only disagree if this function sets them
 * apart, and it sets both from one `bits`. */
static tpl_ureq fun_req(cq_shim_fun_op op, int bits, int32_t out,
                        int32_t a_handle)
{
    tpl_ureq r;
    const uint32_t w = fun_width(bits);

    r.inner.fn = NULL; r.inner.f = r.inner.t = 0u; r.inner.tag = 0u;
    r.outer.fn = cq_tpl_fun_kernel(op);
    r.outer.f  = w; r.outer.t = w;
    r.outer.tag = 0x20000000u + (uint32_t)op * 1024u + w + 1u;
    r.w = r.wout = w;
    r.a_h = a_handle; r.out = out;
    r.name = cq_tpl_fun_name(op);
    return r;
}

int32_t cq_shim_fun(cq_shim_fun_op op, int bits, int32_t a_handle)
{
    return cq_tpl_unary(fun_req(op, bits, CQ_REG_NONE, a_handle));
}

void cq_shim_fun_unc(cq_shim_fun_op op, int bits, int32_t out_handle,
                     int32_t a_handle)
{
    (void)cq_tpl_unary(fun_req(op, bits, out_handle, a_handle));
}
