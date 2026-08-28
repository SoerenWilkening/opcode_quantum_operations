/* shim/cq_shim_ctx.h — M26's foundation, Step 23 landing 1.
 *
 * FOUR SUBJECTS, ONE FILE, and the plan's M26 split (IMPLEMENTATION_PLAN §3)
 * puts them here together because they are what EVERY other M26 file needs
 * before it can say anything: the process-global `cq_ctx`, installation of the
 * built-in sinks (`bd utk`), the ONE PRD §9 region bracket (`bd d6m` fix (a)),
 * and the v1 boundary's abort (PRD §1). Nothing here implements a `cqrt_*` or
 * a `cq_shim_*` entry point; those are `cq_runtime_rail.c`, `cq_runtime_gate.c`,
 * `cq_runtime_v2.c` and `cq_template_impl.c`.
 *
 * `cq_ctx` STAYS INTERNAL, AND STEP 23 IS WHERE THAT QUESTION WAS RESERVED TO
 * BE ANSWERED (src/ctx.h:14-19, IMPLEMENTATION_PLAN §"Deviation 4"). The answer
 * is forced rather than chosen:
 *
 *   - THE CONSUMER IS IN THIS REPOSITORY. This file holds the one context and
 *     reaches the internals by include path, the same way tests/ has since
 *     Step 2. An in-repo consumer needs a path, not a public header.
 *   - NO CALLER OUTSIDE IT CAN EVER HOLD ONE. Measured against CQ_lang at
 *     02afdfe: 0 of the 2479 `cq_template_*` declarations and 0 of the 173
 *     `cqrt_*` declarations name a context, a pointer or a struct — the nine
 *     distinct parameter types across both frozen ABIs are all scalars — and
 *     `opcode_table.yaml` contains the string `ctx` zero times.
 *   - THERE IS NO OPAQUE PATH TO PUBLISH. Every `cq_ctx` in the tree is an
 *     object declared by value; there is no `cq_ctx_create`, no heap allocation
 *     of one and no `sizeof(cq_ctx)` anywhere. Publishing it means publishing
 *     the whole struct and the four it contains.
 *   - AND ITS LAYOUT IS CONFIGURATION-DEPENDENT. Measured: `sizeof(cq_ctx)` is
 *     128 without CQOPS_DEBUG_INVARIANTS and 144 with it, because of ctx.h's
 *     `#if`-guarded scratch extent. A public struct whose size depends on a
 *     build flag is a public ABI that depends on a build flag: a caller
 *     compiled against a Release header and linked against a Debug library
 *     would disagree about its size, silently.
 *
 * PRD §14's layout sketch — "public API: context, sink, config" — is the single
 * sentence pointing the other way; it already carries a correction block for
 * three other errors, and CLAUDE.md's standing rule is that the plan's module
 * map supersedes it where they differ. The *sink* and *config* halves are
 * satisfied and always were.
 *
 * ONE CONTEXT PER PROCESS MAKES THE SHIM SINGLE-THREADED BY CONSTRUCTION, not
 * by omission: the frozen ABI has no context parameter, so there is nowhere to
 * put a second one; the sink registry is an unguarded file-static array
 * (src/sink.c) and the handle counter, the qubit pool and the shadow are one
 * mutable object each, with no locking anywhere in the library. CQ_lang's
 * fixtures are single-threaded programs. A caller wanting two independent
 * circuits at once wants two processes.
 */
#ifndef CQ_SHIM_CTX_H
#define CQ_SHIM_CTX_H

#include <stdint.h>

#include "ctx.h"

/* THE process context. Lazily initialised on first use, because the ABI has no
 * init call: `cqrt_alloc_i32` is as early as CQ_lang ever gets, and there is no
 * hook before it. First use registers the built-in sinks and THEN calls
 * cq_ctx_init — the order is load-bearing (see the .c) — so this function never
 * returns a context whose sink is unresolved.
 *
 * Never NULL. Never a second object: the same address for the life of the
 * process, so a caller may cache it, and every `cqrt_*` entry point calls it
 * rather than threading one through. */
cq_ctx *cq_shim_ctx(void);

/* Disposes the context and clears the latch, so the NEXT cq_shim_ctx() builds a
 * fresh one. EXISTS FOR TESTS, exactly as cq_sink_reset does (src/sink.h:59-61),
 * and it is NOT optional decoration.
 *
 * cq_ctx_init borrows the sink ONCE (src/ctx.c) — `cq_sink_active()` is
 * consulted at construction and never again. So in a multi-case test binary a
 * second case's `cqops_set_sink(&mock2)` is IGNORED by an already-latched
 * context and its gates are recorded into MOCK1. Measured against a latch with
 * no reset, byte-identical in Debug, Release and a from-source ASan build:
 * `after case 2: mock1.n=2 mock2.n=0`. Three riders, each worth more than the
 * headline.
 *
 * (i) THE FAILURE IS LOUD FOR AN L4-SHAPED ASSERTION AND SILENT FOR AN
 * L5-SHAPED ONE. "mock2 saw exactly one gate" goes red; "mock2 recorded zero
 * gates" passes, because the gate went to the other recorder — measured in the
 * same program. That matters because zero-gates is the natural spelling for
 * anything about the classical short-circuit, so the assertion a reader reaches
 * for first is the one the bug satisfies.
 *
 * (ii) IT IS NOT ONLY THE SINK THAT IS STALE. Without a reset, case 2 begins
 * with case 1's live qubits still in the pool, so every L2/L3 baseline in it is
 * off by the residue. Measured: `live=1 minted=1` on entry to case 2, against
 * `live=0 minted=0` with a reset.
 *
 * (iii) THE BORROWED SINK DANGLES, WHICH IS A USE-AFTER-FREE RATHER THAN A
 * WRONG ANSWER. cq_mock_sink() returns a cq_sink BY VALUE and the caller owns
 * it, so a case-local sink installed into a process-global context is borrowed
 * for the rest of the process. Measured under a from-source ASan build:
 * `stack-use-after-return ... in cq_sink_x`. Resetting before the sink leaves
 * scope is what bounds the borrow.
 *
 * IT TOUCHES NEITHER THE SINK REGISTRY NOR THE cqops_set_sink OVERRIDE, and
 * that is the second half of the contract: cq_sink_reset() clears both, so a
 * reset that called it would silently discard a test's deliberate override and
 * send the gates to stdout with no diagnostic. What a case must do is set the
 * selection and drop the context BEFORE the next cq_shim_ctx(); the order
 * between those two does not matter, because nothing is latched until that
 * call. (An earlier draft of this paragraph prescribed one order and was
 * wrong.)
 *
 * Aborts, through cq_ctrl_stack_dispose, if a §9 region is still open — the
 * region's flag qubits would still be live. That is cq_ctx_dispose's existing
 * guard reached from here, not a new one. */
void cq_shim_ctx_reset(void);

/* THE ONE PRD §9 REGION BRACKET IN THE WHOLE SHIM (`bd d6m` fix (a)).
 *
 * `ctrl_flag` is the control-flag HANDLE the ABI prepends to a controlled call,
 * and `body` is the single kernel invocation the region wraps. Row 0 is M06's,
 * not ours: a CQ_BIT_ZERO flag skips the body's gates, a CQ_BIT_ONE flag emits
 * them verbatim, and only a CQ_BIT_Q flag promotes. "No region at all" is not a
 * value of this parameter — it is the absence of the `_controlled` symbol, a
 * different entry point — so CQ_REG_NONE reaches M07's handle check and aborts
 * there rather than being read as a fifth row.
 *
 * IT REFUSES A NESTED REGION, IN BOTH CONFIGURATIONS. M06 supports nesting and
 * is tested for it; the SHIM never nests, because CQ_lang's ABI prepends one
 * flag per template call and ANDs multi-condition control in the IR pass
 * (src/controlled.h). Refusing is not a restriction on anything the ABI can
 * express, and it is what keeps `bd d6m`'s hazard at its depth-1 severity —
 * see the .c for why depth 2 is a different and much worse bug.
 *
 * THE FLAG RAIL MUST BE ONE BIT — the only rail guard here, and the .c carries
 * its evidence. A MEASURED rail is deliberately NOT refused: a control is a
 * READ, and reg.h admits a measured rail through cq_reg_cbits on purpose. */
void cq_shim_region(int32_t ctrl_flag, void (*body)(void *), void *arg);

/* PRD §1's v1 boundary, made visible at RUNTIME rather than at link time.
 * Declared in shim/cq_shim.h (Step 22, the M27↔M28 contract) and defined in the
 * .c; this header does not redeclare it, so there is one declaration to drift
 * from.
 *
 * `reason` is the CALLER's string and is not validated. There are two buckets
 * in the generated bodies today — PRD §1's "fp is v2" (884 symbols) and D14's
 * `_inv` (603) — and the v1-scope refusals `bd vxk` and `bd ck6` are expected
 * to add a third from cq_runtime_v2.c, so nothing here may be written against a
 * fixed set of two. */

#endif /* CQ_SHIM_CTX_H */
