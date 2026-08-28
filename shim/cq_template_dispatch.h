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
const char *cq_tpl_bin_name (cq_shim_op op);
const char *cq_tpl_cmp_name (cq_shim_pred pred);
const char *cq_tpl_cast_name(cq_shim_cast_kind kind);

#endif /* CQ_TEMPLATE_DISPATCH_H */
