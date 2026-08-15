/* src/sink_printf.h — M23: the default sink. PRD §8.
 *
 * One line per gate, in CQ_lang's lexical convention: a lowercase op name, a
 * parenthesised comma-space-separated operand list, `%a` for angles, newline,
 * flush. That convention is CQ_lang's (runtime/cq_runtime.c:449-466); the
 * CONTENT is not, and the difference matters enough to state plainly.
 *
 * THIS IS NOT CQ_LANG'S TRACE, AND MUST NOT LOOK LIKE IT. CQ_lang's goldens in
 * its tests/e2e `.expected.log` files are HANDLE-level runtime calls — `cqrt_cnot(h1,
 * h3)`, `cq_template_add_i32_hl(h6, -6) -> h7` — and they are the output of a
 * **stub**: `CQ_lang/runtime/cq_runtime.c` opens "trace-only runtime stub", and
 * printing what it was asked to do is the whole of its behaviour. Those are the
 * calls that come INTO us. What goes out of us is one level below: the gates
 * those calls decompose to.
 *
 * We satisfy CQ_lang's `cqrt_*` ABI; we do not inherit its trace. The coupling
 * to CQ_lang is the frozen ABI and nothing else — libcqops is a linkable C
 * library and CQ_lang is one caller of it. So this format answers to us. A sink
 * sees a raw `uint32_t` qubit index and never sees a handle at all
 * (src/sink.h), which settles it on its own: `h<N>` is unavailable, and would
 * be a lie if it were available, because handles are monotonic and never
 * reused (D5) while qubit indices are recycled through the LIFO free list (D4).
 * Hence `x` / `cx` / `ccx` / `ry` / `rz` / `mz`, spelled exactly like the
 * vtable entries they come from, with operands `q<N>`.
 *
 * `%a` IS LOAD-BEARING, not a style choice. Angles are `double` all the way
 * down and are compared BITWISE, never with `==` (tests/support/mock_sink.h) —
 * 0.0 and -0.0 are equal in C and are different gates to emit. `%a` is exact
 * and round-trips through strtod for every finite double, subnormals included;
 * `%f` and `%g` do not. It is also what CQ_lang prints for its own angles, and
 * for the same reason: `%f` at its default six digits is not injective, so
 * 0x1p-128 and +0.0 both render as `0.000000` and a miscompile between them is
 * invisible in a golden diff.
 *
 * ONE RESIDUE, INHERITED AND MEASURED, so this is not read as a total
 * guarantee: `%a` renders EVERY NaN encoding as the bare string `nan`, sign,
 * quiet bit and payload alike. Verified on this toolchain. Infinities do
 * survive (`inf` / `-inf`). A NaN angle is a malformed program that §7's
 * classifier (M21, Step 19) should reject long before it reaches a sink — but
 * if one ever arrives here, the trace cannot tell you which NaN it was.
 */
#ifndef CQOPS_SINK_PRINTF_H
#define CQOPS_SINK_PRINTF_H

#include "cqops/cqops.h"

#include <stdio.h>

/* A printf sink writing to `f`. NULL means stdout, resolved at EMIT time
 * rather than here, so a caller that later reopens stdout gets the new one —
 * which is what CQ_lang's fixtures do when they redirect a run to a file. */
cq_sink cq_sink_printf(FILE *f);

/* Registers a stdout instance under the name "printf" (PRD §8's documented
 * default, and what cq_sink_active falls back to when CQOPS_SINK is unset).
 *
 * INSTALLATION IS AN EXPLICIT ACT, and deliberately not automatic. Nothing
 * calls this yet: the library's first real entry point is M26's `cqrt_*` shim
 * at Step 23, and that is where the call belongs. Making cq_sink_active()
 * install it lazily instead would defeat tests/test_sink_death.c's
 * `no_sink_registered_at_all`, whose whole content is that an unresolvable
 * sink is a hard error rather than a quiet substitution. Idempotent:
 * cq_sink_register replaces by name. */
void cq_sink_printf_register(void);

#endif /* CQOPS_SINK_PRINTF_H */
