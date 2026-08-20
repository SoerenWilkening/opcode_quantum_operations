/* src/kernels/divrem_s.h — M20, Step 17. K12 signed division and remainder.
 *
 * Read docs/constructions/K12.md §2.3, §3.4 and §5 before changing anything.
 *
 * SIGN-MAGNITUDE, WHICH IS UPSTREAM'S SHAPE AND NOT A CHOICE. Bennett's
 * `lower_divrem!` (aggregate.jl:69-82, 105-117) converts both operands to
 * magnitude with a conditional negate, runs the unsigned kernel, and fixes the
 * sign: `sdiv(a,b) = sign(a)^sign(b) ? -q : q` and `srem(a,b) = sign(a) ? -r : r`.
 * That is exactly C's truncating division, which is what the L1 reference
 * cross-checks it against (tests/test_kernel_divrem_refmodel.inc).
 *
 * THE WRAPPER IS PART OF THE SAME SANDWICH, NOT A SECOND ONE. `cq_sandwich`
 * refuses to nest in both configurations, and there is nothing to nest anyway:
 * the prefix, the unsigned core and the suffix are one flat step space over one
 * region, so the reverse half unwinds the conditional negates as well.
 *
 * `_cond_negate_inplace!` IS TRANSCRIBED HERE, and it is the ONE construction
 * K12 does not get from a sibling module (K12.md §3.0 — no other kernel ships
 * it). Two things about the port:
 *
 *   - IT LEAKS `W+1` WIRES PER CALL, BY DESIGN. aggregate.jl:150-172 documents
 *     the leak, and records that upstream TRIED to free them (:159-167) and it
 *     broke correctness. Three calls per signed op; the sandwich reverse is what
 *     cleans them here. Do not try to free them mid-kernel.
 *   - THE `val` ARGUMENT MUST BE A SCRATCH COPY. This is the most dangerous
 *     line in the whole extraction (K12.md §5 delta 6): `_cond_negate_inplace!`
 *     mutates its argument, and upstream passes its OWN widened `a64`/`b64`, not
 *     the user's operands. A port that "saved the copy" by passing the caller's
 *     `a`/`b` straight in would violate I6, violate Rule 7's "sources
 *     unchanged", and be a silent miscompile whose reverse half still cancels —
 *     so L1 and L3 would both stay green. `cq_emit_*`'s `const cq_bit *`
 *     controls make it a compile error; do not cast that away.
 *
 * TWO CX FEWER THAN A LITERAL GATE-FOR-GATE PORT, AND IT IS DELIBERATE — check
 * this before "fixing" §3.4's numbers. Bennett copies each sign bit into a fresh
 * wire first (aggregate.jl:76-77) because its conditional negate would otherwise
 * clobber its own control. libcqops does not need that: `a[W-1]` and `b[W-1]`
 * are SOURCES, used only as controls, and `sa`/`sb` are separate scratch. So an
 * independent reader counting upstream gets `11W+7` / `11W+5` where §3.4 has
 * `11W+5` / `11W+3`. §3.4's are what this emits. `sdiv`'s own `rs` wire —
 * `sign(a) ^ sign(b)` — IS still needed and IS counted.
 */
#ifndef CQOPS_KERNELS_DIVREM_S_H
#define CQOPS_KERNELS_DIVREM_S_H

#include "bit.h"
#include "ctx.h"

/* K12 signed — `dst ^= a / b` and `dst ^= a % b`, truncating toward zero.
 *
 * Sandwiched cost at the all-quantum operand mask (K12.md §3.4):
 *
 *     sdiv   4W²+4W X   20W²+21W+10 CX   10W²+2W CCX  = 34W²+27W+10
 *     srem   4W²+4W X   20W²+19W+6  CX   10W²+2W CCX  = 34W²+25W+6
 *
 * over `8W²+9W+3` and `8W²+8W+2` scratch qubits. **Those figures were DERIVED
 * and not executed until Step 17** — K12.md §6.1 lists the whole signed path as
 * the one thing its 2026-08-16 measurement pass did not reach — so
 * tests/test_kernel_divrem_signed.inc is where they stopped being arithmetic.
 *
 * D3, and the values are INHERITED rather than chosen. Division by zero never
 * traps: `|0| = 0` and `b_sign = 0`, so `sdiv(a,0) = condneg(2^W-1, sign(a))`,
 * which is -1 for `a >= 0` and +1 for `a < 0`; and `srem(a,0) = a`. The
 * `typemin / -1` case reproduces at width W without upstream's 64-bit widening:
 * `|typemin|` is `typemin` again in two's complement, dividing by 1 leaves it,
 * and the sign fix is a no-op because `sign(a) ^ sign(b) = 1 ^ 1 = 0`. At W = 1
 * the signed values are `{0, -1}` and NOTHING is representable — `sdiv(-1,-1)`
 * is 1, which has no i1 encoding — and upstream never meets the case, because
 * it widens to 64 first. i1 IS a shipped sdiv/srem width
 * (opcode_table.yaml:187,189), so what the construction returns is pinned and
 * documented as inherited, exactly as the `b = 0` row is. */
void cq_kernel_sdiv(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_srem(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W);

/* Compute-half slots at this width — `17W²+13W+5` for sdiv, `17W²+12W+3` for
 * srem. Exported for the suite, not for a caller: `cq_mock_is_palindrome` needs
 * the compute half's exact length, and that ordered check is the only
 * instrument with teeth against a reverse half that does not cancel (a gate
 * COUNT cannot see it — a reversed forward list can have a different multiset
 * with an identical total). */
int cq_sdivrem_steps(int W, int want_q);

/* Scratch bits at this width — `8W²+9W+3` for sdiv, `8W²+8W+2` for srem. */
int cq_sdivrem_region(int W, int want_q);

#endif /* CQOPS_KERNELS_DIVREM_S_H */
