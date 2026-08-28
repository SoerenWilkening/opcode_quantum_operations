/* shim/cq_template_dispatch.c — see cq_template_dispatch.h for the seam this
 * file is one half of and for why a cast carries its own function-pointer type.
 */

#include "cq_template_dispatch.h"

#include "kernels/add.h"
#include "kernels/bitwise.h"
#include "kernels/cast.h"
#include "kernels/cmp.h"
#include "kernels/divrem_s.h"
#include "kernels/divrem_u.h"
#include "kernels/mul.h"
#include "kernels/shift_var.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* A per-translation-unit copy of the house shape, on `src/reg_check.c`'s and
 * `shim/cq_runtime_rail.c`'s precedent. The `FAIL_REGULAR_EXPRESSION` pins in
 * tests/CMakeLists.txt discriminate on the MESSAGE and not on the module, so
 * every string in this file is DISJOINT from `cq_template_impl.c`'s — these
 * three name an ENUM, that file's name a WIDTH or a HANDLE. */
static void cq_disp_die(const char *what, int32_t v)
{
    fprintf(stderr, "libcqops: FATAL: shim: %s (%d)\n", what, v);
    abort();
}

/* EACH TABLE'S LENGTH IS TIED TO ITS LAST ENUMERATOR BY A `_Static_assert`, so
 * an enum that grows without its row breaks the BUILD. A runtime check could
 * not see it: the bounds test below would pass for every value the table does
 * cover and the new one would read past the end. */
cq_kernel_fn cq_tpl_bin_kernel(cq_shim_op op)
{
    static const cq_kernel_fn K[] = {
        cq_kernel_add,  cq_kernel_sub,  cq_kernel_mul,      cq_kernel_sdiv,
        cq_kernel_udiv, cq_kernel_srem, cq_kernel_urem,     cq_kernel_and,
        cq_kernel_or,   cq_kernel_xor,  cq_kernel_shl_var,  cq_kernel_lshr_var,
        cq_kernel_ashr_var
    };
    _Static_assert(sizeof K / sizeof *K == (size_t)CQ_SHIM_OP_ASHR + 1u,
                   "one kernel per cq_shim_op, in the enum's own order");

    if ((unsigned)op > (unsigned)CQ_SHIM_OP_ASHR)
        cq_disp_die("cq_template_* opcode outside the ABI's enum", (int32_t)op);
    return K[op];
}

/* ALL THREE SHIFTS TAKE THE BARREL, `_hl` AND `_lh` ALIKE (bd 216 checklist
 * 12). `src/kernels/shift_var.c`'s classical short-circuit to M11 is
 * unconditional and sits after `cq_kernel_check_dst`, so a constant AMOUNT
 * still costs M11's price through this door; and `_lh` — a classical VALUE with
 * a quantum amount — genuinely needs the barrel. Reaching for M11 here would be
 * a second dispatch decision in a second place, and the wrong one for `_lh`. */

/* THE PREDICATE ORDER IS `opcode_table.yaml`'s OWN and nothing here re-derives
 * it; `tests/test_gen_shim.py` is what pins the enum against the yaml, so a
 * re-pin that reorders the list breaks there rather than here. FOUR of the ten
 * predicates SWAP their operands and FIVE invert the flag, and the two sets are
 * different sets — that arithmetic is M16's and is already tested in its own
 * suite. This table must name each predicate's own kernel and must NOT try to
 * canonicalise: `uge` spelled as `ule` emits the same tuple, keeps the
 * palindrome and leaves scratch clean, so only an L1 against an independent
 * reference can see it. */
cq_kernel_fn cq_tpl_cmp_kernel(cq_shim_pred pred)
{
    static const cq_kernel_fn K[] = {
        cq_kernel_eq,  cq_kernel_ne,  cq_kernel_slt, cq_kernel_sgt,
        cq_kernel_sle, cq_kernel_sge, cq_kernel_ult, cq_kernel_ugt,
        cq_kernel_ule, cq_kernel_uge
    };
    _Static_assert(sizeof K / sizeof *K == (size_t)CQ_SHIM_PRED_UGE + 1u,
                   "one kernel per cq_shim_pred, in opcode_table.yaml's order");

    if ((unsigned)pred > (unsigned)CQ_SHIM_PRED_UGE)
        cq_disp_die("cq_template_icmp_* predicate outside the ABI's enum",
                    (int32_t)pred);
    return K[pred];
}

cq_tpl_cast_fn cq_tpl_cast_kernel(cq_shim_cast_kind kind)
{
    static const cq_tpl_cast_fn K[] = {
        cq_kernel_sext, cq_kernel_zext, cq_kernel_trunc
    };
    _Static_assert(sizeof K / sizeof *K == (size_t)CQ_SHIM_CAST_TRUNC + 1u,
                   "one kernel per cq_shim_cast_kind");

    if ((unsigned)kind > (unsigned)CQ_SHIM_CAST_TRUNC)
        cq_disp_die("cq_template_* cast kind outside the ABI's enum",
                    (int32_t)kind);
    return K[kind];
}

/* --- the display names (PRD §15 D21) -------------------------------------- */

/* THREE TABLES IN THE ENUMS' OWN ORDER, tied to their last enumerator by a
 * `_Static_assert` for the reason the kernel tables above give: an enum that
 * grows without its row must break the BUILD, because a bounds test passes for
 * every value the table does cover and the new one reads past the end — and
 * here it would read past the end into whatever `.rodata` holds and print it as
 * a token, which is a fatal parse error for the viewer rather than a crash. */
const char *cq_tpl_bin_name(cq_shim_op op)
{
    static const char *const N[] = {
        "add",  "sub", "mul", "sdiv", "udiv", "srem", "urem",
        "and",  "or",  "xor", "shl",  "lshr", "ashr"
    };
    _Static_assert(sizeof N / sizeof *N == (size_t)CQ_SHIM_OP_ASHR + 1u,
                   "one display name per cq_shim_op, in the enum's own order");

    if ((unsigned)op > (unsigned)CQ_SHIM_OP_ASHR)
        cq_disp_die("cq_template_* opcode outside the ABI's enum", (int32_t)op);
    return N[op];
}

const char *cq_tpl_cmp_name(cq_shim_pred pred)
{
    static const char *const N[] = {
        "icmp_eq",  "icmp_ne",  "icmp_slt", "icmp_sgt", "icmp_sle",
        "icmp_sge", "icmp_ult", "icmp_ugt", "icmp_ule", "icmp_uge"
    };
    _Static_assert(sizeof N / sizeof *N == (size_t)CQ_SHIM_PRED_UGE + 1u,
                   "one display name per cq_shim_pred, in the yaml's order");

    if ((unsigned)pred > (unsigned)CQ_SHIM_PRED_UGE)
        cq_disp_die("cq_template_icmp_* predicate outside the ABI's enum",
                    (int32_t)pred);
    return N[pred];
}

const char *cq_tpl_cast_name(cq_shim_cast_kind kind)
{
    static const char *const N[] = { "sext", "zext", "trunc" };
    _Static_assert(sizeof N / sizeof *N == (size_t)CQ_SHIM_CAST_TRUNC + 1u,
                   "one display name per cq_shim_cast_kind");

    if ((unsigned)kind > (unsigned)CQ_SHIM_CAST_TRUNC)
        cq_disp_die("cq_template_* cast kind outside the ABI's enum",
                    (int32_t)kind);
    return N[kind];
}
