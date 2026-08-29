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
 * condition region (`if (cond) x = cq_phi(x, M_PI);`) moves the decline one
 * guard along —
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
 * MEASURED, both arms, 2026-08-28: quantum emits a real stream (x, cx, ccx, ry,
 * rz, mz all non-zero) and classical emits NO GATE LINE AT ALL and measures 3,
 * which is what plain C computes — §12(2)'s zero-cost half, through the real
 * front end. The classical walk-through: `Ry(π)` flips every bit of the
 * constant rail (§7 / Rule 15), 0 → 255; `+= off` → 2; `2 == 42` is false;
 * `-= off` → 255; `Ry(-π)` flips again → 0; `0 == 0` is true and `cq_phi` on a
 * definite value is a global phase, so nothing is emitted; `+= off` → 3.
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

    if (x == 42u)                           /* O  : mark the target          */
        x = cq_phi(x, M_PI);                /*      Rz(π): the phase flip    */

    x -= off;                               /* S† : un-prepare               */
    x = cq_theta(x, -theta);

    if (x == 0)                             /* R0 : reflect about |0>        */
        x = cq_phi(x, M_PI);

    x += off;                               /* re-prepare                    */

    printf("grover -> %u\n", (unsigned)cq_measure(x));
    return 0;
}
