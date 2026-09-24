/* shim/cq_template_dispatch_unary.c — the ARITY-1 half of M26's opcode
 * DISPATCH: every table whose entries take M13's `(F, T)` pointer type, and
 * their display names — except the fp unary one (see the end of this header).
 *
 * A SECOND SEAM INSIDE THE DISPATCH HALF, RECORDED IN cq_template_dispatch.h
 * AND TAKEN 2026-09-24 (bead 9ve.44) WHEN IT BIT:
 *
 *     the ARITY-2 / ARITY-3 tables <-> the ARITY-1 `(F, T)` tables
 *
 * `cq_template_dispatch.c` stood at 218 counted lines and Wave 10 owed it four
 * integer min/max rows, three fp binary rows, eight fp unary ADAPTERS and a
 * new six-row integer unary table with six more — ~100 lines, past Rule 12's
 * wall. The discriminator is the one `cq_template_boundary.h` already draws
 * one layer up (ARITY-2 <-> ARITY-1 ordered call sequence): what is here is
 * reached ONLY through `cq_tpl_unary`, stores in `cq_tpl_cast_fn`, and grows
 * when a UNARY family lands — and every unary family that is not a cast needs
 * an ADAPTER (Rule 7 fixes the semantics, not the parameter list, `ckd.15`),
 * which is the bulk of what arrives. The other half keeps the tables that
 * store a `cq_kernel_fn` or a `cq_tpl_fma_fn` with no adapter at all.
 *
 * THE MESSAGES MOVED WITH THEIR TABLES, BYTE FOR BYTE. The death cases pin
 * them by FAIL_REGULAR_EXPRESSION against the message and not the module, so a
 * move must not change a sentence — and the `die` below is this translation
 * unit's own copy of the house shape, on `src/reg_check.c`'s precedent.
 *
 * THE NEXT CUT cq_template_dispatch.h RECORDED AT 9ve.44 WAS TAKEN 2026-09-24
 * (bead 6y9), as recorded: the fp UNARY table, its eight adapters and its
 * display names are cq_template_dispatch_funary.c's, on the integer <-> fp
 * domain line. What stays here is the INTEGER width casts, M37's conversions
 * and the INTEGER unary table. No further cut is recorded for this file.
 */

#include "cq_template_dispatch.h"

#include "kernels/cast.h"
#include "kernels/fconv.h"
#include "kernels/ibits.h"
#include "kernels/iminmax.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void cq_dispu_die(const char *what, int32_t v)
{
    fprintf(stderr, "libcqops: FATAL: shim: %s (%d)\n", what, v);
    abort();
}

/* --- the INTEGER width casts ---------------------------------------------- */

cq_tpl_cast_fn cq_tpl_cast_kernel(cq_shim_cast_kind kind)
{
    /* `bitcast` IS `zext` AT `F == T` — M13's recorded identity copy, F CX and
     * nothing else (`trunc` at `F == T` is the same loop). cq_template_unary.c
     * refuses a bitcast whose widths differ BEFORE this pointer is called, so
     * `zext`'s widening is never reachable under that name. */
    static const cq_tpl_cast_fn K[] = {
        cq_kernel_sext, cq_kernel_zext, cq_kernel_trunc, cq_kernel_zext
    };
    _Static_assert(sizeof K / sizeof *K == (size_t)CQ_SHIM_CAST_BITCAST + 1u,
                   "one kernel per cq_shim_cast_kind");

    if ((unsigned)kind > (unsigned)CQ_SHIM_CAST_BITCAST)
        cq_dispu_die("cq_template_* cast kind outside the ABI's enum",
                     (int32_t)kind);
    return K[kind];
}

/* M37's four conversions, in `cq_shim_fcast_kind`'s order. Each takes `(F, T)`
 * exactly as M13's casts do, which is why they share the pointer type and NOT
 * the table: `cq_kernel_sext` and `cq_kernel_sitofp` are both
 * `void (*)(ctx, dst, a, F, T)` and are interchangeable to a compiler, so one
 * table indexed by "a cast kind" would send `sitofp i8 -> f64` to `cq_kernel_zext`
 * — right shape, right widths, a 64-lane result, and a value that is the integer
 * rather than its IEEE encoding. Only L1 sees it. */
cq_tpl_cast_fn cq_tpl_fcast_kernel(cq_shim_fcast_kind kind)
{
    static const cq_tpl_cast_fn K[] = {
        cq_kernel_fptosi, cq_kernel_fptoui, cq_kernel_sitofp, cq_kernel_uitofp
    };
    _Static_assert(sizeof K / sizeof *K == (size_t)CQ_SHIM_FCAST_UITOFP + 1u,
                   "one kernel per cq_shim_fcast_kind");

    if ((unsigned)kind > (unsigned)CQ_SHIM_FCAST_UITOFP)
        cq_dispu_die("the fp conversion kind is not one of the ABI enumerators",
                     (int32_t)kind);
    return K[kind];
}

/* THE INTEGER UNARY ADAPTERS. `CQ_UN_ADAPTER` and the reason it may discard
 * `T` are in cq_template_dispatch.h, shared with the fp unary table's eight in
 * cq_template_dispatch_funary.c. */
CQ_UN_ADAPTER(iun_ctpop,      cq_kernel_ctpop)
CQ_UN_ADAPTER(iun_ctlz,       cq_kernel_ctlz)
CQ_UN_ADAPTER(iun_cttz,       cq_kernel_cttz)
CQ_UN_ADAPTER(iun_bitreverse, cq_kernel_bitreverse)
CQ_UN_ADAPTER(iun_bswap,      cq_kernel_bswap)
CQ_UN_ADAPTER(iun_abs,        cq_kernel_abs)

/* THE INTEGER UNARY TABLE (M41 K26, M42 K27). `ctlz` and `cttz` are ONE row
 * machine mirrored, so a transposition emits the same gate tuple and differs
 * only on the value — L1 against an independent reference, at an operand
 * whose leading and trailing zero counts differ, is the only detector. `abs`
 * is M42's and not M41's, which is why the table crosses two headers. */
cq_tpl_cast_fn cq_tpl_iun_kernel(cq_shim_iun_op op)
{
    static const cq_tpl_cast_fn K[] = {
        iun_ctpop, iun_ctlz, iun_cttz, iun_bitreverse, iun_bswap, iun_abs
    };
    _Static_assert(sizeof K / sizeof *K == (size_t)CQ_SHIM_IUN_ABS + 1u,
                   "one kernel per cq_shim_iun_op");

    if ((unsigned)op > (unsigned)CQ_SHIM_IUN_ABS)
        cq_dispu_die("the integer intrinsic opcode is not one of the ABI "
                     "enumerators", (int32_t)op);
    return K[op];
}

/* --- the display names (PRD §15 D21) -------------------------------------- */

/* In the enums' own orders, `_Static_assert`ed for the reason the kernel
 * tables are, and inside handoff §5's charset `[A-Za-z0-9_.$\[\]-]+`. The fp
 * unary names moved with their table to cq_template_dispatch_funary.c. */
const char *cq_tpl_cast_name(cq_shim_cast_kind kind)
{
    static const char *const N[] = { "sext", "zext", "trunc", "bitcast" };
    _Static_assert(sizeof N / sizeof *N == (size_t)CQ_SHIM_CAST_BITCAST + 1u,
                   "one display name per cq_shim_cast_kind");

    if ((unsigned)kind > (unsigned)CQ_SHIM_CAST_BITCAST)
        cq_dispu_die("cq_template_* cast kind outside the ABI's enum",
                     (int32_t)kind);
    return N[kind];
}

const char *cq_tpl_fcast_name(cq_shim_fcast_kind kind)
{
    static const char *const N[] = { "fptosi", "fptoui", "sitofp", "uitofp" };
    _Static_assert(sizeof N / sizeof *N == (size_t)CQ_SHIM_FCAST_UITOFP + 1u,
                   "one display name per cq_shim_fcast_kind");

    if ((unsigned)kind > (unsigned)CQ_SHIM_FCAST_UITOFP)
        cq_dispu_die("the fp conversion kind is not one of the ABI enumerators",
                     (int32_t)kind);
    return N[kind];
}

const char *cq_tpl_iun_name(cq_shim_iun_op op)
{
    static const char *const N[] = {
        "ctpop", "ctlz", "cttz", "bitreverse", "bswap", "abs"
    };
    _Static_assert(sizeof N / sizeof *N == (size_t)CQ_SHIM_IUN_ABS + 1u,
                   "one display name per cq_shim_iun_op");

    if ((unsigned)op > (unsigned)CQ_SHIM_IUN_ABS)
        cq_dispu_die("the integer intrinsic opcode is not one of the ABI "
                     "enumerators", (int32_t)op);
    return N[op];
}
