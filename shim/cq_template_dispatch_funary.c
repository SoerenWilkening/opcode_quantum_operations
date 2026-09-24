/* shim/cq_template_dispatch_funary.c — the fp UNARY table of M26's opcode
 * DISPATCH: `cq_tpl_fun_kernel`, its eight one-width ADAPTERS, and its display
 * names.
 *
 * A THIRD SEAM INSIDE THE DISPATCH HALF, RECORDED IN cq_template_dispatch.h AT
 * bead 9ve.44 AND TAKEN 2026-09-24 (bead 6y9) AS RECORDED:
 *
 *     the fp unary table and its adapters <-> the integer one
 *
 * on the domain line `cq_template_boundary.h` already draws between the
 * integer and fp surfaces. `cq_template_dispatch_unary.c` keeps the integer
 * width casts, M37's conversions and the INTEGER unary table (M41/M42); what
 * is here is reached only through `cq_shim_fun` / `cq_shim_fun_unc` and grows
 * when an fp unary row lands. A PURE MOVE: the table, the adapters, the name
 * table and every message are byte for byte what they were in the sibling;
 * `CQ_UN_ADAPTER` moved to cq_template_dispatch.h so both files use one
 * definition, and the `die` below is this translation unit's own copy of the
 * house shape, on `src/reg_check.c`'s precedent. The death cases pin the
 * messages by FAIL_REGULAR_EXPRESSION against the message and not the module.
 *
 * THE NEXT CUT, recorded now, TRIGGER 240 counted lines: the ONE-width fp
 * unary table <-> any fp unary row with a RESULT WIDTH OF ITS OWN. The first
 * candidates are the libm pair `lrint`/`llrint` (f64 -> iN, deferred in
 * shim/gen_reasons.py's DEFER_REASON as "soft_round then soft_fptosi"); such
 * a row cannot use `CQ_UN_ADAPTER` — which discards `T` — and would take its
 * own table and adapters to a new `..._dispatch_ftoi.c`, beside M37's
 * conversions rather than in this file.
 */

#include "cq_template_dispatch.h"

#include "kernels/fmisc.h"
#include "kernels/fround.h"
#include "kernels/fsqrt.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void cq_dispf_die(const char *what, int32_t v)
{
    fprintf(stderr, "libcqops: FATAL: shim: %s (%d)\n", what, v);
    abort();
}

CQ_UN_ADAPTER(fun_fsqrt,      cq_kernel_fsqrt)
CQ_UN_ADAPTER(fun_fneg,       cq_kernel_fneg)
CQ_UN_ADAPTER(fun_fabs,       cq_kernel_fabs)
CQ_UN_ADAPTER(fun_floor,      cq_kernel_ffloor)
CQ_UN_ADAPTER(fun_ceil,       cq_kernel_fceil)
CQ_UN_ADAPTER(fun_trunc,      cq_kernel_ftrunc)
CQ_UN_ADAPTER(fun_round_away, cq_kernel_fround_away)
CQ_UN_ADAPTER(fun_round_even, cq_kernel_fround_even)

/* THE fp UNARY TABLE, AND THE LLVM-NAME -> soft_* MAP LIVES IN IT (PRD-v2 7.7,
 * K25.md §1.2, src/kernels/fround.h's own table). It is NOT identity, and each
 * wrong pick is K9's trap in fp dress — right on every input but a few, the
 * same gate count, the palindrome intact, only L1 against an INDEPENDENT
 * reference at the few inputs sees it:
 *
 *     ROUND      C / llvm.round      soft_round_away   ties AWAY from zero
 *     RINT       llvm.rint           soft_round        ties to EVEN
 *     NEARBYINT  llvm.nearbyint      soft_round        ties to EVEN
 *
 * There is no `soft_rint`. Julia's `round` is ties-to-even and C's is
 * ties-away, so the obvious pick of `soft_round` for `ROUND` is wrong at
 * exactly 0.5, 2.5, -2.5 and their kin. `fneg`/`fabs` are M38's sign ops
 * (K24), which are ONE upstream block each and zero-Toffoli. */
cq_tpl_cast_fn cq_tpl_fun_kernel(cq_shim_fun_op op)
{
    static const cq_tpl_cast_fn K[] = {
        fun_fsqrt,                              /* FSQRT      soft_fsqrt      */
        fun_fneg, fun_fabs,                     /* FNEG FABS  sign ops (K24)  */
        fun_floor, fun_ceil, fun_trunc,         /* soft_floor/ceil/trunc      */
        fun_round_away,                         /* ROUND      soft_round_away */
        fun_round_even,                         /* RINT       soft_round      */
        fun_round_even                          /* NEARBYINT  soft_round      */
    };
    _Static_assert(sizeof K / sizeof *K == (size_t)CQ_SHIM_FUN_NEARBYINT + 1u,
                   "one kernel per cq_shim_fun_op");

    if ((unsigned)op > (unsigned)CQ_SHIM_FUN_NEARBYINT)
        cq_dispf_die("the fp unary opcode is not one of the ABI enumerators",
                     (int32_t)op);
    return K[op];
}

/* --- the display name (PRD §15 D21) --------------------------------------- */

/* The fp unary names are the ABI's own opcode tokens — `rint` and `round` are
 * two NAMES even where the kernels would say one thing, because the viewer
 * shows what CQ_lang's IR called, not which soft_* ran. */
const char *cq_tpl_fun_name(cq_shim_fun_op op)
{
    static const char *const N[] = {
        "fsqrt", "fneg", "fabs", "floor", "ceil", "trunc", "round",
        "rint", "nearbyint"
    };
    _Static_assert(sizeof N / sizeof *N == (size_t)CQ_SHIM_FUN_NEARBYINT + 1u,
                   "one display name per cq_shim_fun_op");

    if ((unsigned)op > (unsigned)CQ_SHIM_FUN_NEARBYINT)
        cq_dispf_die("the fp unary opcode is not one of the ABI enumerators",
                     (int32_t)op);
    return N[op];
}
