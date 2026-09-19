/* shim/cq_template_dispatch.h — M26's OPCODE DISPATCH, the half of
 * `shim/cq_template_impl.c` that IMPLEMENTATION_PLAN §3 recorded a seam for
 * before either file was written.
 *
 * THE SEAM WAS TAKEN AS SCHEDULED, NOT IMPROVISED. `cq_template_impl.c` came in
 * at 270 counted lines against a 280 budget and a 240 trigger, which is exactly
 * the case Rule 12 describes: "hitting the limit is a scheduled split, never a
 * surprise refactor". The cut is the recorded one —
 * `the opcode DISPATCH TABLE <-> the handle BOUNDARY` — and the discriminator
 * is WHAT MAKES EACH HALF CHANGE. This half is pure transcription of the frozen
 * ABI's three enums and grows when `opcode_table.yaml` gains an opcode, a
 * predicate or a cast pair. The other half is the ordered call sequence, D7a,
 * D7b and the §9 region, and grows when a DECISION changes.
 *
 * IT IS ALSO WHERE EVERY `src/kernels/` INCLUDE NOW LIVES, which is the second
 * argument for the cut: the boundary half names no kernel at all, and so cannot
 * quietly acquire a per-opcode special case.
 */
#ifndef CQ_TEMPLATE_DISPATCH_H
#define CQ_TEMPLATE_DISPATCH_H

#include "cq_shim.h"

#include "bit.h"
#include "ctx.h"
#include "kernels/kernel.h"

/* A CAST DOES NOT FIT `cq_kernel_fn` AND MUST NOT BE MADE TO. Rule 7 fixes the
 * SEMANTICS, not the parameter list, and M13's casts are the recorded departure
 * that "leaves the parameter list": two widths, `(F, T)`, where a binary kernel
 * has one `W`. Widening `cq_kernel_fn` to cover both is what Rule 7 forbids in
 * as many words, so the cast table carries its own pointer type. */
typedef void (*cq_tpl_cast_fn)(cq_ctx *, cq_bit *, const cq_bit *, int, int);

/* Each aborts, in BOTH configurations, on a value outside its enum. That is not
 * defensive decoration: C does not require an enum object to hold one of its
 * enumerators, and an unchecked index reads past the table into whatever
 * follows it in `.rodata` and calls it — silently, with a plausible trace.
 * M28 only ever passes the enumerators themselves. */
cq_kernel_fn   cq_tpl_bin_kernel (cq_shim_op op);
cq_kernel_fn   cq_tpl_cmp_kernel (cq_shim_pred pred);
cq_tpl_cast_fn cq_tpl_cast_kernel(cq_shim_cast_kind kind);

/* M36's fourteen `fcmp` kernels, over the yaml's OWN predicate order. It is a
 * `cq_kernel_fn` like `cq_tpl_cmp_kernel`'s — K9's shape, `dst` one bit and `W`
 * the operand width — and it is a SEPARATE table for the reason `cq_shim.h`
 * gives beside `cq_shim_fpred`: four mnemonics appear in both predicate lists
 * meaning different things, so one table indexed by "a predicate" would
 * dispatch an fp `ult` to the integer comparator. */
cq_kernel_fn   cq_tpl_fcmp_kernel(cq_shim_fpred pred);

/* M33/M34/M35's four fp ARITHMETIC kernels, over `opcode_table.yaml`'s own
 * `fp_arith` order. Each is Rule 7's CANONICAL shape — arity 2, one width, a
 * 64-lane `dst` — so they store in a `cq_kernel_fn` with no adapter, which the
 * one-bit-`dst` compares above also do and for a different reason.
 *
 * `frem` HAS NO ROW AND MUST NOT ACQUIRE ONE UNTIL IT HAS A KERNEL. It is the
 * fifth `fp_arith` binary opcode and there is no `CQ_SHIM_FOP_FREM` to index
 * with, which is what makes the omission a COMPILE error rather than a table
 * one enumerator short. */
cq_kernel_fn   cq_tpl_fbin_kernel(cq_shim_fop op);

/* M37's four CROSS-DOMAIN conversions and M40's `fsqrt`, both in M13's `(F, T)`
 * pointer type. `cq_tpl_fun_kernel` returns that type too, which is an ADAPTER
 * rather than a claim about `cq_kernel_fsqrt`: that kernel is one source and
 * ONE width (Rule 7 fixes the semantics, not the parameter list — `ckd.15`),
 * and the adapter passes `F`, with `F == T` guaranteed by the entry point's own
 * refusal of any width but 64. Reaching for `cq_kernel_fn` instead would be the
 * widening Rule 7 forbids in as many words. */
cq_tpl_cast_fn cq_tpl_fcast_kernel(cq_shim_fcast_kind kind);
cq_tpl_cast_fn cq_tpl_fun_kernel  (cq_shim_fun_op op);

/* THE DISPLAY NAME OF THE OPERATION, for `bd 76r`'s `# STAGE: op begin` payload
 * (PRD §15 D21). It belongs on THIS side of the seam by the seam's own
 * discriminator — the name set grows when `opcode_table.yaml` gains an opcode, a
 * predicate or a cast pair, never when a decision changes — and the tables are
 * bounds-checked and `_Static_assert`ed exactly as the kernel tables are.
 *
 * THE TOKENS ARE THE ABI's OWN OPCODE SPELLINGS and must stay inside handoff
 * §5's charset `[A-Za-z0-9_.$\[\]-]+`; the viewer has NO operation vocabulary,
 * so `add` and `Frobnicate` render identically well and the only thing a name
 * has to be is recognisable to a reader of CQ_lang's IR. */
const char *cq_tpl_bin_name  (cq_shim_op op);
const char *cq_tpl_cmp_name  (cq_shim_pred pred);
const char *cq_tpl_cast_name (cq_shim_cast_kind kind);
const char *cq_tpl_fcmp_name (cq_shim_fpred pred);
const char *cq_tpl_fbin_name (cq_shim_fop op);
const char *cq_tpl_fcast_name(cq_shim_fcast_kind kind);
const char *cq_tpl_fun_name  (cq_shim_fun_op op);

#endif /* CQ_TEMPLATE_DISPATCH_H */
