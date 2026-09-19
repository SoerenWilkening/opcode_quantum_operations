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
#include "kernels/fadd.h"
#include "kernels/fcmp.h"
#include "kernels/fconv.h"
#include "kernels/fdiv.h"
#include "kernels/fmul.h"
#include "kernels/fma.h"
#include "kernels/fsqrt.h"
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

/* THE ROW ORDER IS `opcode_table.yaml`'s `predicates: fcmp:` LIST AND NOT LLVM's,
 * AND THE TWO GENUINELY DISAGREE. `src/kernels/fcmp.h`'s `cq_fcmp_pred` is LLVM's
 * numbering (OEQ, OGT, OGE, OLT, OLE, ONE, ORD, UNO, UEQ, UGT, UGE, ULT, ULE,
 * UNE) and `cq_shim_fpred` is the yaml's (OEQ, UNE, OLT, OGT, OLE, OGE, ONE,
 * ORD, UNO, UEQ, UGT, UGE, ULT, ULE). Only the first row coincides. A table
 * written by INDEX rather than by name — or a `(cq_fcmp_pred)pred` cast, which
 * compiles clean — sends `une` to `ogt`, `olt` to `oge` and so on for thirteen of
 * fourteen rows. Nothing structural can see it: every row emits from the same
 * program machine, keeps the palindrome and leaves scratch clean, exactly as
 * K9's `uge`-meaning-`ule` does. Only L1 against `cq_fcmp_eval` tells them apart.
 *
 * AND `cq_kernel_fcmp_*` IS WHAT THIS NAMES, NEVER `cq_fcmp_program` — the
 * kernel is the Rule 7 entry point, sandwich and all; the program is its table. */
cq_kernel_fn cq_tpl_fcmp_kernel(cq_shim_fpred pred)
{
    static const cq_kernel_fn K[] = {
        cq_kernel_fcmp_oeq, cq_kernel_fcmp_une, cq_kernel_fcmp_olt,
        cq_kernel_fcmp_ogt, cq_kernel_fcmp_ole, cq_kernel_fcmp_oge,
        cq_kernel_fcmp_one, cq_kernel_fcmp_ord, cq_kernel_fcmp_uno,
        cq_kernel_fcmp_ueq, cq_kernel_fcmp_ugt, cq_kernel_fcmp_uge,
        cq_kernel_fcmp_ult, cq_kernel_fcmp_ule
    };
    _Static_assert(sizeof K / sizeof *K == (size_t)CQ_SHIM_FPRED_ULE + 1u,
                   "one kernel per cq_shim_fpred, in opcode_table.yaml's order");

    if ((unsigned)pred > (unsigned)CQ_SHIM_FPRED_ULE)
        cq_disp_die("cq_template_fcmp_* predicate outside the ABI's enum",
                    (int32_t)pred);
    return K[pred];
}

/* THE fp ARITHMETIC ROW ORDER IS `opcode_table.yaml:200-204`'s OWN — fadd,
 * fsub, fmul, fdiv — with `frem` absent because it has no kernel. Unlike the
 * predicate tables above, the names here coincide with the enumerators, so a
 * transposition is a visible swap rather than a silent renumbering; the reason
 * to write it out row by row anyway is the `_Static_assert`, which is what makes
 * a FIFTH opcode arriving without its kernel break the BUILD.
 *
 * `cq_kernel_fsub` IS NOT `cq_kernel_fadd`, AND NOTHING STRUCTURAL SEES THE
 * DIFFERENCE. M33 runs ONE row program under two prologues, so the two emit the
 * same gate tuple at the same width, keep the palindrome and leave scratch
 * clean. Only an L1 against `cq_fsub_eval` on operands where they disagree —
 * ±0 and the NaN rows — tells them apart. K9's `uge`-meaning-`ule`, again. */
cq_kernel_fn cq_tpl_fbin_kernel(cq_shim_fop op)
{
    static const cq_kernel_fn K[] = {
        cq_kernel_fadd, cq_kernel_fsub, cq_kernel_fmul, cq_kernel_fdiv
    };
    _Static_assert(sizeof K / sizeof *K == (size_t)CQ_SHIM_FOP_FDIV + 1u,
                   "one kernel per cq_shim_fop, in opcode_table.yaml's order");

    if ((unsigned)op > (unsigned)CQ_SHIM_FOP_FDIV)
        cq_disp_die("the fp arithmetic opcode is not one of the ABI enumerators",
                    (int32_t)op);
    return K[op];
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
        cq_disp_die("the fp conversion kind is not one of the ABI enumerators",
                    (int32_t)kind);
    return K[kind];
}

/* THE UNARY ADAPTER, AND IT IS WHERE `ckd.15` IS PAID FOR. `cq_kernel_fsqrt` is
 * `(ctx, dst, a, W)` — one source, ONE width — so it does not fit either
 * pointer type the tables above use. Rule 7 fixes the SEMANTICS and not the
 * parameter list, so the kernel is right and the boundary adapts: the arity-1
 * ordered call sequence is written over `(F, T)` because the CASTS need two
 * widths, and a one-width operation passes the same number twice.
 *
 * `T` IS DISCARDED AND THAT IS SAFE HERE AND ONLY HERE. `cq_shim_fun` sets both
 * widths from its single `bits` argument after refusing anything but 64, so the
 * two cannot disagree; a future unary family with a result width of its own
 * would need its own adapter rather than a `T`-aware version of this one. */
static void fun_fsqrt(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T)
{
    (void)T;
    cq_kernel_fsqrt(ctx, dst, a, F);
}

cq_tpl_cast_fn cq_tpl_fun_kernel(cq_shim_fun_op op)
{
    static const cq_tpl_cast_fn K[] = { fun_fsqrt };
    _Static_assert(sizeof K / sizeof *K == (size_t)CQ_SHIM_FUN_FSQRT + 1u,
                   "one kernel per cq_shim_fun_op");

    if ((unsigned)op > (unsigned)CQ_SHIM_FUN_FSQRT)
        cq_disp_die("the fp unary opcode is not one of the ABI enumerators",
                    (int32_t)op);
    return K[op];
}

/* THE ARITY-3 TABLE. One row, and the `_Static_assert` is what makes a second
 * ternary opcode arriving upstream break the BUILD rather than read past the
 * end — the reason every other table here carries one. */
cq_tpl_fma_fn cq_tpl_fma_kernel(cq_shim_fma_op op)
{
    static const cq_tpl_fma_fn K[] = { cq_kernel_fma };
    _Static_assert(sizeof K / sizeof *K == (size_t)CQ_SHIM_FMA_FMA + 1u,
                   "one kernel per cq_shim_fma_op");

    if ((unsigned)op > (unsigned)CQ_SHIM_FMA_FMA)
        cq_disp_die("the fp ternary opcode is not one of the ABI enumerators",
                    (int32_t)op);
    return K[op];
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

/* The yaml's order again, and the `fcmp_` prefix keeps a D21 `op begin` payload
 * distinguishable from `icmp_ult`'s in a viewer that has no type information. */
const char *cq_tpl_fcmp_name(cq_shim_fpred pred)
{
    static const char *const N[] = {
        "fcmp_oeq", "fcmp_une", "fcmp_olt", "fcmp_ogt", "fcmp_ole",
        "fcmp_oge", "fcmp_one", "fcmp_ord", "fcmp_uno", "fcmp_ueq",
        "fcmp_ugt", "fcmp_uge", "fcmp_ult", "fcmp_ule"
    };
    _Static_assert(sizeof N / sizeof *N == (size_t)CQ_SHIM_FPRED_ULE + 1u,
                   "one display name per cq_shim_fpred, in the yaml's order");

    if ((unsigned)pred > (unsigned)CQ_SHIM_FPRED_ULE)
        cq_disp_die("cq_template_fcmp_* predicate outside the ABI's enum",
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

/* The three fp families' D21 payload names, in their enums' own orders. They
 * are the ABI's own opcode spellings and stay inside handoff §5's charset; the
 * viewer has no operation vocabulary, so all a name has to be is recognisable
 * to a reader of CQ_lang's IR. */
const char *cq_tpl_fbin_name(cq_shim_fop op)
{
    static const char *const N[] = { "fadd", "fsub", "fmul", "fdiv" };
    _Static_assert(sizeof N / sizeof *N == (size_t)CQ_SHIM_FOP_FDIV + 1u,
                   "one display name per cq_shim_fop, in the yaml's order");

    if ((unsigned)op > (unsigned)CQ_SHIM_FOP_FDIV)
        cq_disp_die("the fp arithmetic opcode is not one of the ABI enumerators",
                    (int32_t)op);
    return N[op];
}

const char *cq_tpl_fcast_name(cq_shim_fcast_kind kind)
{
    static const char *const N[] = { "fptosi", "fptoui", "sitofp", "uitofp" };
    _Static_assert(sizeof N / sizeof *N == (size_t)CQ_SHIM_FCAST_UITOFP + 1u,
                   "one display name per cq_shim_fcast_kind");

    if ((unsigned)kind > (unsigned)CQ_SHIM_FCAST_UITOFP)
        cq_disp_die("the fp conversion kind is not one of the ABI enumerators",
                    (int32_t)kind);
    return N[kind];
}

const char *cq_tpl_fma_name(cq_shim_fma_op op)
{
    static const char *const N[] = { "fma" };
    _Static_assert(sizeof N / sizeof *N == (size_t)CQ_SHIM_FMA_FMA + 1u,
                   "one display name per cq_shim_fma_op");

    if ((unsigned)op > (unsigned)CQ_SHIM_FMA_FMA)
        cq_disp_die("the fp ternary opcode is not one of the ABI enumerators",
                    (int32_t)op);
    return N[op];
}

const char *cq_tpl_fun_name(cq_shim_fun_op op)
{
    static const char *const N[] = { "fsqrt" };
    _Static_assert(sizeof N / sizeof *N == (size_t)CQ_SHIM_FUN_FSQRT + 1u,
                   "one display name per cq_shim_fun_op");

    if ((unsigned)op > (unsigned)CQ_SHIM_FUN_FSQRT)
        cq_disp_die("the fp unary opcode is not one of the ABI enumerators",
                    (int32_t)op);
    return N[op];
}
