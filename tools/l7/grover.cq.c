/* tools/l7/grover.cq.c — PRD §12's Grover, as a CQ translation unit.
 *
 * This is §12(1)'s artefact: ordinary C, compiled by CQ_lang's front end
 * (`-include CQ.h`), lowered by its `cq-lowering` pass, and linked against
 * `libcqops` and NO CQ_lang runtime archive. `tools/l7/l7_run.py` drives it.
 *
 * ─── WHY THIS IS NOT §12's LISTING VERBATIM, AND IT IS NOT A WORKAROUND ─────
 *
 * MEASURED 2026-08-28 (PRD §15 D22): §12's listing DOES NOT LOWER. CQ_lang's
 * own pass declines it —
 *
 *     cq rotation lowering: non-diagonal gate over a live derived record
 *
 * — because `y` and `hit` are records derived from `x` that are still live when
 * the diffusion's `cq_theta(x, …)` fires. Rewriting the mark as a tainted-
 * condition region (`if (cond) cq_phi(M_PI);`) moves the decline one guard
 * along —
 *
 *     cq controlled lowering: condition predates a non-diagonal gate
 *                             under quantum control
 *
 * — as soon as the condition is DERIVED arithmetic rather than a direct compare
 * on the wire. Both are UPSTREAM guards doing their job (CQ_lang's `bd lom0`),
 * not libcqops defects: an in-place `Ry` rotates the basis a live record was
 * computed against, and a record nobody has uncomputed is exactly what Rule 2
 * exists to forbid one layer down.
 *
 * THE SHAPE CQ_lang ACCEPTS IS ITS OWN, and it ships it: read
 * `CQ_lang/tests/e2e/slice_control_seq_grover.c`, whose header derives the
 * accept in detail. State prep, an oracle REGION whose flag and work rail the
 * region teardown strips and frees, an ordinary arithmetic step, the inverse
 * prep, and a reflection about |0>. That is a Grover iteration; what it is not
 * is a multi-stage arithmetic oracle, which upstream cannot yet lower with a
 * non-diagonal gate after it.
 *
 * SO WHAT IS LOST AND WHAT IS NOT. Lost: the mul. The oracle's condition here
 * is a compare against a constant rather than `3x + 1 == 42`. NOT lost: the
 * claim §12(1) actually makes — a real CQ program compiles, links against this
 * archive alone, runs, and emits a non-empty gate stream containing Toffolis
 * (the compare and the `+= off` both build circuits). The mul's circuit is
 * verified by L1 and its cost is pinned by `tests/test_grover.c`, which drives
 * the FULL two-stage oracle through the frozen ABI directly and therefore does
 * not depend on CQ_lang's front end at all. The two files are complementary on
 * purpose: this one proves the toolchain, that one proves the program.
 *
 * ─── WHY `cq_phi` TAKES NO REGISTER, AND WHY THAT IS A FIX RATHER THAN A ────
 * ─── PORT (`bd 2tm`, migrated 2026-09-10) ──────────────────────────────────
 *
 * Until 2026-09-05 both marks above were spelled `x = cq_phi(x, M_PI);`, and
 * that is a COMPILE ERROR against the current `include/CQ.h` — CQ_lang retired
 * the 15-width `cq_phi_<sfx>(x, angle)` family outright and ships one
 * register-free `void cq_phi(double)` in its place, with **deliberately no
 * migration shim** (its own `bd test_C_libtooling-3qlq` Inc 1). `cq_theta` is
 * UNCHANGED and still takes two arguments, so this is one intrinsic changing
 * shape, not an API break. THE EDIT HERE IS UPSTREAM'S OWN, COPIED: CQ_lang
 * migrated `tests/e2e/slice_control_seq_grover.c` the same way and says so in
 * its first line — Rule 1's habit applied to CQ_lang, which is the source of
 * record for what CQ_lang accepts.
 *
 * `cq_phi(θ)` now means "multiply the amplitude of the CURRENT BRANCH by
 * e^{iθ}". Under a tainted condition it lowers by KICKBACK: the compare mints
 * the flag into a |0> ancilla, the phase goes on the FLAG as an UNCONTROLLED
 * `cqrt_rz_i1`, the compare's `_unc` returns the flag to |0> on every branch,
 * and the flag is freed. Nothing on the data register is touched.
 *
 * AND THAT CHANGES WHAT THIS ARTEFACT DEMONSTRATES, upstream's finding rather
 * than ours (`CQ.h`'s own block at `cq_phi`; its
 * `grover-goldens-are-identity-tensored-rz-2026-09-05`): the retired spelling
 * emitted a TENSORED `cqrt_rz_<W>` — Rz on EVERY qubit of the register — whose
 * marked phase is (-1)^popcount(target) and which, being diagonal per bit,
 * FIXES |0…0>. So a reflection about |0> could never put −1 there, and "the
 * oracle marks the target" was true of the SOURCE and false of the EMITTED
 * CIRCUIT. It becomes true from this migration on. The old count is not
 * re-measurable here — the old spelling no longer compiles — so what is
 * asserted is the new stream, below; the reading of the old one is upstream's.
 *
 * `tests/test_grover.c` already drove the frozen ABI this way, at
 * `cqrt_rz_i1(hit, M_PI)`, since Step 25. This file has caught up with it.
 *
 * ─── WHY `off` IS VOLATILE ─────────────────────────────────────────────────
 *
 * Inherited verbatim from upstream's slice, and load-bearing: written as a
 * literal pair, `x += 3; … x -= 3;` is reassociated by `clang -O1` and cancelled
 * on the straight-through edge, the `+ 3` disappearing into the compare. Two
 * volatile loads are two values LLVM cannot prove equal, so the pair survives
 * canonicalization and reaches the pass as a real add/sub on the register.
 *
 * ─── WHY THE ANGLE IS A RUNTIME VALUE ──────────────────────────────────────
 *
 * §12(2) is "replace `M_PI/2` with `M_PI`", i.e. the SAME program in two modes.
 * Reading the angle off `argc` makes that one lowered artefact and two runs
 * rather than two compilations, so the classical and quantum arms provably
 * share a circuit — which is the whole content of the claim. `argc` is
 * classical, so the ternary is classical, and `cq_theta` is the taint source
 * whatever its angle.
 *
 * MEASURED, both arms, 2026-09-10 at CQ_lang 5a26204 (the 2026-08-28 figures
 * this block used to carry were taken at the retired `cq_phi` spelling and are
 * not re-measurable). QUANTUM: 429 gate lines, mix `x 38, cx 270, ccx 95,
 * ry 16, rz 2, mz 8` — §12(1)'s clause 4, which the runner asserts as a MIX.
 * CLASSICAL: NO GATE LINE AT ALL, and it measures 3, which is what plain C
 * computes — §12(2)'s zero-cost half, through the real front end.
 *
 * THE `rz` COUNT IS 2, NOT 16, AND THAT IS THE KICKBACK BEING VISIBLE. One per
 * branch phase, each on a SINGLE qubit (`q23`, `q22`) outside the data rail the
 * eight `mz` name (`q0`…`q7`), and each sitting at the exact palindrome centre
 * of its compare's compute/uncompute sandwich. A per-register `Rz` on this
 * 8-bit rail would be 8 lines per phase, because §7 is PER BIT.
 *
 * The classical walk-through: `Ry(π)` flips every bit of the constant rail
 * (§7 / Rule 15), 0 → 255; `+= off` → 2; `2 == 42` is false; `-= off` → 255;
 * `Ry(-π)` flips again → 0; `0 == 0` is true, and a branch phase on a branch
 * taken CLASSICALLY is a global phase, so nothing is emitted. `+= off` → 3.
 * NOTE WHICH LAYER DELETES IT NOW: uncontrolled or classically-controlled,
 * `cq_phi` is DELETED BY CQ_lang as a documented semantic (its D2), so no call
 * reaches us at all — where the retired spelling reached §7's `Rz`-on-a-
 * constant row and was folded away HERE. Same zero, one layer up.
 */
#include <math.h>
#include <stdio.h>

volatile unsigned char off = 3;

int main(int argc, char **argv)
{
    /* argc > 1 selects CLASSICAL mode — §12(2)'s `M_PI/2` → `M_PI`. */
    double        theta = (argc > 1) ? M_PI : M_PI / 2.0;
    unsigned char x     = 0;

    (void)argv;

    x = cq_theta(x, theta);                 /* S  : |0…0> → uniform          */
    x += off;

    if (x == 42u) {                         /* O  : mark the target          */
        cq_phi(M_PI);                       /*      the BRANCH phase flip    */
    }

    x -= off;                               /* S† : un-prepare               */
    x = cq_theta(x, -theta);

    if (x == 0) {                           /* R0 : reflect about |0>        */
        cq_phi(M_PI);
    }

    x += off;                               /* re-prepare                    */

    printf("grover -> %u\n", (unsigned)cq_measure(x));
    return 0;
}
