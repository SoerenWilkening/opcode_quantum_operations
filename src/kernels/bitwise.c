/* src/kernels/bitwise.c — M10, Step 10. K1 xor, K2 and, K3 or.
 *
 * Three ports, each a single straight-line pass over W lanes. Read
 * docs/constructions/K01.md, K02.md and K03.md before changing anything here;
 * Rule 1 is that Bennett.jl is the sole source of constructions and that these
 * are transcribed, never re-derived.
 *
 * WHY THERE IS NO SANDWICH AND NO SHORT-CIRCUIT. Both would be wrong here.
 * A sandwich (PRD §5) exists to clean an intermediate wire, and these three
 * have none — every gate targets `dst`. And the all-classical short-circuit
 * that risk R9 requires is a SANDWICH-ENTRY check: it exists because
 * pre-materialising scratch would allocate qubits for a fully classical
 * operation. With no scratch there is nothing to pre-materialise, and L5 falls
 * out of the §3 fold table for free — a CX with a constant control folds to
 * `emit_x` or to nothing, and an X on a constant target flips it in place for
 * 0 gates. Plan §6 R9 puts that check at Step 12 with the first sandwich
 * kernel, and tests/test_kernel_bitwise.c asserts the zero-cost result here
 * without one.
 */

#include "kernels/bitwise.h"

#include "emit.h"
#include "kernels/kernel.h"

/* K1 — Bennett `lower_xor!` (arith.jl:284-291), verbatim:
 *
 *     for i in 1:W
 *         push!(g, CNOTGate(a[i], r[i]))
 *         push!(g, CNOTGate(b[i], r[i]))
 *     end
 *
 * with `r` replaced by the caller's `dst` (Rule 7). Gate for gate the same
 * loop; only the provenance of the target differs, and that substitution is
 * exactly what lets one function serve forward (`0 ^ f = f`) and uncompute
 * (`f ^ f = 0`). 2W CNOT, 0 NOT, 0 Toffoli, 0 ancillae, at every W. */
void cq_kernel_xor(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W)
{
    cq_kernel_check_dst(dst, a, b, W);

    for (int i = 0; i < W; i++) {
        cq_emit_cx(ctx, &a[i], &dst[i]);
        cq_emit_cx(ctx, &b[i], &dst[i]);
    }
}

/* K2 — Bennett `lower_and!` (arith.jl:268-272), verbatim:
 *
 *     for i in 1:W; push!(g, ToffoliGate(a[i], b[i], r[i])); end
 *
 * W Toffoli, 0 NOT, 0 CNOT, 0 ancillae.
 *
 * THE AND IS CONSUMED IMMEDIATELY, which is the whole reason this is clean.
 * An AND that had to HOLD its result across a later step would leak — that is
 * the general worry, and it is real — but here `a & b` is produced straight
 * onto `dst` by one Toffoli and never lands in a temporary. */
void cq_kernel_and(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W)
{
    cq_kernel_check_dst(dst, a, b, W);

    for (int i = 0; i < W; i++)
        cq_emit_ccx(ctx, &a[i], &b[i], &dst[i]);
}

/* K3 — Bennett `lower_or!` (arith.jl:274-282), verbatim:
 *
 *     for i in 1:W
 *         push!(g, CNOTGate(a[i], r[i]))
 *         push!(g, CNOTGate(b[i], r[i]))
 *         push!(g, ToffoliGate(a[i], b[i], r[i]))
 *     end
 *
 * i.e. `a | b` as the XOR identity `a ^ b ^ (a & b)`, all three gates on the
 * same wire. 2W CNOT + W Toffoli, 0 NOT, 0 ancillae. The `a & b` term is never
 * landed in a temporary, which is again why nothing needs cleaning up.
 *
 * DO NOT REPLACE THIS WITH DE MORGAN. `a | b = ~(~a & ~b)` would cost 5W X on
 * top of the W Toffoli AND would write X onto the SOURCES `a` and `b`,
 * breaking Rule 7's "leaving a and b unchanged" and the
 * controls-are-never-materialised property that lets this kernel be called
 * from inside another's sandwich compute half. It is also not what upstream
 * does, which settles it under Rule 1 (K03.md §5 delta 6).
 *
 * The three gates commute — none of them writes a control — so the order is a
 * convention rather than a correctness constraint. Keep Bennett's, so that L6
 * trace diffs at Step 24 stay attributable. */
void cq_kernel_or(cq_ctx *ctx, cq_bit *dst,
                  const cq_bit *a, const cq_bit *b, int W)
{
    cq_kernel_check_dst(dst, a, b, W);

    for (int i = 0; i < W; i++) {
        cq_emit_cx (ctx, &a[i],        &dst[i]);
        cq_emit_cx (ctx, &b[i],        &dst[i]);
        cq_emit_ccx(ctx, &a[i], &b[i], &dst[i]);
    }
}
