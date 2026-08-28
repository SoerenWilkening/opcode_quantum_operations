/* src/rotate.h — M22, Step 19. PRD §7: what an angle plus a bit KIND becomes,
 * and the terminal measurement.
 *
 * THIS IS WHERE THE ROTATION-FREE SURFACE ENDS. Steps 1-18 emit nothing but
 * X / CX / CCX (Rule 4), which is why the two-bit shadow is EXACT there and why
 * tests/support/poolcheck.c's cq_pc_zero_proof_rotation_free is a genuine proof
 * rather than a heuristic. cq_shadow_rotate is the only producer of `unknown`
 * in the library and this file is its FIRST caller in src/ — on exactly two of
 * §7's twelve cells — the general-Ry row, BOTH columns — which is PRD §15 D12.
 *
 * THE SPLIT WITH M21 IS WHERE THE POLICY LIVES. M21 answers "which ROW of §7's
 * table is this angle on?" and emits nothing; this module answers "given that
 * row and this bit's kind, what happens?" — which is the CELL, and needs a
 * cq_bit. Rule 15's asymmetry is the reason the two are separate: on a constant
 * a half turn is a bit flip costing zero gates and zero qubits, on a qubit it
 * is `X` then `Z`, and only the second half of that sentence needs a wire.
 *
 * KIND, NEVER SHADOW (D6). Every cell dispatches on cq_bit's kind. A qubit
 * whose shadow happens to read known-0 still takes the QUBIT column, exactly as
 * the §3 fold table does — otherwise the emitted gate count would depend on
 * shadow precision.
 *
 * D12 — WHICH ROWS POISON. Only `Ry` at an angle OFF the π-lattice. Every `Rz`
 * is diagonal, and so is the `Z` of the half-turn row, and a diagonal gate maps
 * |v> to e^{i.alpha}|v>: it cannot move a computational-basis value, so a
 * determinate shadow entry stays CORRECT rather than merely conservative. The
 * half-turn row is `X` followed by a diagonal and therefore takes `X`'s shadow
 * rule. Measured payoff: a rail that only ever met a diagonal row, or a half
 * turn, keeps a DETERMINATE entry and stays freeable on evidence the shadow
 * can actually give. This sentence used to add "the corpus's twelve
 * `rz`-rooted rails stay freeable"; that half was measured FALSE on
 * 2026-08-22. The cause is not the rotation, and it is stated once — in
 * PRD §10's trap (ii) and in §15 D12's own note. Those rails are discharged
 * by the observed undo certificate at M26 (PRD §15 D15), not by the shadow.
 * D12 itself is unaffected. Full argument, and its honest limit, in
 * PRD §15 D12.
 *
 * bd lk0 — THE `Z` IS `sink.rz(q, pi)`, NOT A SEVENTH VTABLE ENTRY. §7 named a
 * gate that has none, and Rule 4 forbids inventing one; `Rz(pi) = diag(-i, +i)
 * = -i.Z`, so two existing entries do it. The emitted pair is `Y = +i.Ry(pi)`,
 * and the residual `±i` is UNREACHABLE for any spelling out of `x` and `rz` —
 * PRD §7 gives the determinant argument. Do not reorder to chase it.
 *
 * EMISSION GOES STRAIGHT TO THE SINK, WITH ONE DELIBERATE EXCEPTION. The `X` of
 * the half-turn row goes through cq_emit_x, because that function IS §7's
 * constant/qubit split for a bit flip — flip the constant for nothing, or emit
 * the gate and update the shadow — so writing it out here would be a second
 * copy of the fold table's first row. Everything else (`ry`, `rz`, `mz`) has no
 * cq_emit_* to go through: src/emit.h exports x/cx/ccx and nothing more, and
 * the vtable is frozen at six.
 *
 * THIS MODULE IS NOT VALID INSIDE A §9 CONTROLLED REGION IN v1, and PRD §15 D11
 * is why: four of §7's rows act by emitting nothing, and `controlled-(e^{i.a}I)`
 * is `Rz(a)` on the control wire, so those rows are wrong the moment a quantum
 * control exists. D11 records the exact correction for each and decides that v1
 * REFUSES rather than emitting a hand-derived phase this project has no
 * instrument to check. M06 (Step 20) owns that refusal; there is no ctrl_depth
 * in cq_ctx today and this module deliberately takes no position on the axis.
 *
 * NOT REACHABLE FROM A SANDWICH, and that is enforced rather than assumed. M22
 * bypasses cq_emit_* for its rotations, so NEITHER I6 mechanism covers it — not
 * the `const cq_bit *` control typing, and not the Debug scratch-extent check,
 * which is armed only from inside cq_emit_*. And `Ry(theta)` is not an
 * involution, so cq_sandwich's reverse pass would compose it to `Ry(2.theta)`
 * while the recorded stream stayed a PERFECT palindrome — risk R1 with every
 * detector blind. Hence a hard error on ctx->sandwich_depth, in both
 * configurations (that counter is not Debug-gated).
 */
#ifndef CQOPS_ROTATE_H
#define CQOPS_ROTATE_H

#include <stdint.h>

#include "bit.h"
#include "ctx.h"

/* §7's table, one bit at a time. The register forms below are these in a loop,
 * and tests/test_rotate.c asserts that structurally rather than trusting it. */
void cq_rotate_ry_bit(cq_ctx *ctx, cq_bit *b, double theta);
void cq_rotate_rz_bit(cq_ctx *ctx, cq_bit *b, double phi);

/* `cqrt_ry_i<W>(h, theta)` and `cqrt_rz_i<W>(h, phi)`: the rotation applies to
 * EVERY bit of the register, per PRD §7's own wording, and the column is chosen
 * per bit — a mixed rail takes both. */
void cq_rotate_ry(cq_ctx *ctx, int32_t h, double theta);
void cq_rotate_rz(cq_ctx *ctx, int32_t h, double phi);

/* `cqrt_measure_i<W>(h)`. Emits one `sink.mz` per QUBIT — a constant bit owns
 * no wire to measure (I4) — and returns the register's value: the shadow value
 * for every bit whose shadow is known, and ZERO for any bit whose shadow is
 * not. A poisoned entry still HOLDS its last determinate byte, frozen, so
 * returning it would be publishing a stale byte as a measurement.
 *
 * TERMINAL (PRD §7): the rail becomes CQ_SLOT_MEASURED, its qubits are
 * deliberately never reclaimed, and a later `cqrt_free` is a hard error. That
 * is not a leak to tidy up — CQ_lang emits no adjoint and no free for a
 * measured handle, so there is no point at which reclaiming would be sound.
 *
 * TWO WORDS OUT, mirroring cq_reg_alloc_const's two words in. A register
 * reaches CQ_REG_WIDTH_MAX = 128 bits and a one-word return would truncate at
 * the 64-bit seam in silence — the exact trap I5 exists to prevent. Nothing
 * above i64 reaches this through the frozen ABI (there is no cqrt_measure_i80
 * or _i128), but a library entry point with a hidden ceiling is a defect
 * whether or not today's shim can reach it. */
void cq_measure(cq_ctx *ctx, int32_t h, uint64_t *lo, uint64_t *hi);

#endif /* CQOPS_ROTATE_H */
