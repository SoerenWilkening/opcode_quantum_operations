/* src/angle.h — M21: which row of PRD §7 does this rotation angle sit on?
 *
 * The whole module is a classification. It emits nothing, allocates nothing,
 * touches no context and has no internal dependencies (plan §3, Layer 4): given
 * a double it answers "is this rotation the identity, minus the identity, a
 * half turn, or a genuine rotation?" — and M22 (Step 19) turns that answer into
 * gates, bit-kind changes, or nothing at all.
 *
 * THE SPLIT IS DELIBERATE AND IT IS WHERE THE POLICY LIVES. §7's table has two
 * columns, constant and qubit, and the θ ≡ π row is the reason the project can
 * test classically at all (Rule 15): on a constant that row is a bit flip
 * costing zero gates and zero qubits, on a qubit it is `X` then `Z`. Choosing
 * between those two columns needs a cq_bit and belongs to M22. Choosing the ROW
 * needs only the angle, and belongs here — where it can be tested against a
 * second, independently derived reduction.
 *
 * THE SAFE DIRECTION IS `CQ_ANGLE_GENERAL`, AND EVERY REFUSAL FALLS THAT WAY.
 * Emitting `sink.ry(q, θ)` is physically correct at *every* θ, including the
 * four special ones; it merely costs a gate the table would have saved. The
 * converse is a miscompile: reporting IDENTITY for a real rotation DELETES it,
 * and reporting HALF_TURN for one REPLACES it with an X. So an angle this
 * module cannot classify inside its stated error bound is reported GENERAL
 * rather than guessed at. This is the shadow's discipline (conservative in one
 * direction only) applied to the analog surface.
 *
 * THE ERROR BOUND, WHICH IS THE WHOLE CONTRACT (PRD §15 D10):
 *
 *     cq_angle_lattice(θ, tol) != CQ_ANGLE_GENERAL  ⟹
 *         θ is within 2·tol·π radians of the multiple of π its row names.
 *
 * So a fold never silently discards more than `2·tol·π` of rotation — 6.3e-12
 * radians at the default tolerance. That bound is what `tol` MEANS here, and
 * both halves of it are tested (tests/test_angle.c carries a double-double
 * oracle that measures the distance to a multiple of TRUE π, not of the double
 * this module rounds it to).
 *
 * WHAT M21 IS NOT. It is not `cq_shadow_rotate`'s decision. The shadow poisons
 * unconditionally and has no opinion about θ — see src/shadow.h — precisely
 * because a rotation that IS classical must never reach it. Which rows call it
 * is M22's, and is PRD §15 D12: only `Ry` at an angle OFF the lattice poisons,
 * because a diagonal gate cannot move a computational-basis value.
 *
 * CORRECTION, 2026-08-17 at Step 19 — AMENDED 2026-08-22, BECAUSE THE
 * CORRECTION WAS ALSO WRONG. Two claims died here in a row; both retractions
 * stay, because the arc is the lesson.
 *
 * (1) "Keeping M21 and the shadow apart lets bd ckd.18's twelve `rz`-rooted
 * frees stay clean: those bits are never materialised and never poisoned" —
 * MEASURED FALSE at Step 19. The Fredkin's `CCX` MATERIALISES the target
 * before the `rz` ever arrives, so §7's Rz-CONSTANT cell never applies to
 * them and this module's split had nothing to do with it.
 *
 * (2) Its replacement, "what actually keeps them freeable is D12" — MEASURED
 * FALSE on 2026-08-22. Physically |0> is right; the SHADOW cannot see it, and
 * the cause is not the rotation: it is stated once, in PRD §10's trap (ii)
 * and in §15 D12's own note. Those rails are discharged by the observed undo
 * certificate over the call stream at M26 — PRD §15 D15, which closed ckd.17b
 * (bd c1a), ckd.18 and 2cf on 2026-08-22. "Twelve" was itself an artefact of
 * modelling `cqrt_cswap` as writing only one of its two data args; ckd.18's
 * true scope is the `ry`-rooted frees, every one born from a NON-ZERO alloc
 * literal, and under the certificate those are provably DIRTY rather than
 * unprovable (D15 §4). Verify before re-asserting either claim.
 */
#ifndef CQOPS_ANGLE_H
#define CQOPS_ANGLE_H

/* PRD §7's rows, as a fact about the angle rather than about the bit.
 *
 * GENERAL IS ZERO ON PURPOSE: a zero-initialised cq_angle_class is the
 * conservative answer, so a caller that forgets to assign one emits a rotation
 * instead of deleting it. The numbering is therefore load-bearing and is pinned
 * below rather than merely asserted in a comment — the same treatment, for the
 * same reason, that bit.h gives CQ_BIT_ZERO == 0. A renumbering must break a
 * build. (It was a mutation battery that asked for this: renumbering GENERAL to
 * 7 was the one mutant of thirty-eight that no test could see.) */
typedef enum {
    CQ_ANGLE_GENERAL = 0,    /* none of the below, within tolerance          */
    CQ_ANGLE_IDENTITY,       /* θ ≡ 0  (mod 4π) — the operator is I          */
    CQ_ANGLE_NEG_IDENTITY,   /* θ ≡ 2π (mod 4π) — the operator is −I         */
    CQ_ANGLE_HALF_TURN,      /* θ ≡ π  (mod 4π) — Ry: +XZ.  Rz: −iZ          */
    CQ_ANGLE_NEG_HALF_TURN   /* θ ≡ 3π (mod 4π) — Ry: −XZ.  Rz: +iZ          */
} cq_angle_class;

_Static_assert(CQ_ANGLE_GENERAL == 0,
               "a zero-initialised cq_angle_class must be the safe row");
_Static_assert(CQ_ANGLE_NEG_HALF_TURN == 4,
               "the numbering is read by M06 as well as M21 and M22");

/* PRD §7's default, and PRD §15 D10's upper bound on it.
 *
 * `tol` is a fraction of the lattice modulus π, NOT of θ: the window is the
 * absolute angle `tol·π`, so the default 1e-12 means "within 3.1e-12 radians of
 * a half turn". §7 said only "1e-12 relative" and left the scale unstated; D10
 * fixes it, and fixes it this way because a window proportional to |θ| grows
 * without bound and starts folding angles that are nowhere near a special one.
 * Measured: at θ = 1e12 such a window is a full radian wide, and 1e12 — which
 * is 0.657 radians from the nearest multiple of π — folds to the identity.
 *
 * The cap exists so that one bound serves three purposes: it keeps the window
 * far below the π/2 at which two lattice points could match at once, keeps the
 * step index inside the range where a double is an exact integer, and keeps
 * "tolerance" meaning "these are the same angle". */
#define CQ_ANGLE_TOLERANCE_DEFAULT 1e-12
#define CQ_ANGLE_TOLERANCE_MAX     1e-3

/* The nearest double to π — the lattice modulus this module's whole contract is
 * stated in, and the angle M22 hands to `sink.rz` for §7's `Z` (PRD §7, bd lk0).
 * It lived in angle.c until Step 19 and moved here rather than being spelled a
 * second time in rotate.c: one constant, one home. M_PI is POSIX, not C11, and
 * this build is -std=c11 with CMAKE_C_EXTENSIONS OFF, so it is written out. The
 * literal is over-long on purpose — the compiler rounds it correctly and a
 * shorter one would not be the same double. NOTE tests/test_angle_oracles.inc
 * keeps its OWN copy of these digits deliberately: that is an independent
 * double-double π, and it must not be made to include this header. */
#define CQ_ANGLE_PI 3.14159265358979323846264338327950288

/* The numeric core, pure and reentrant: which point of the π-lattice is θ on,
 * to within the window `tol` names?  See src/angle.c for the predicate and for
 * the single refusal that bounds it. A tolerance outside
 * [0, CQ_ANGLE_TOLERANCE_MAX] — negative, NaN, infinite or merely too loose —
 * is a hard error in both configurations. */
cq_angle_class cq_angle_lattice(double theta, double tol);

/* §7's Ry column: all four rows, at the module tolerance. */
cq_angle_class cq_angle_ry_row(double theta);

/* §7's Rz column: CQ_ANGLE_IDENTITY or CQ_ANGLE_GENERAL, NEVER the other two.
 *
 * That is §7 as written, and the collapse is the whole reason this is a
 * separate function rather than a second call to cq_angle_ry_row. The table
 * gives Rz exactly one special row, φ ≡ 0 (mod 4π); φ ≡ 2π (mod 4π) is −I and
 * falls in "otherwise", so a QUBIT under it gets a real `sink.rz`. Emitting it
 * is sound (§7's constant column is where the folding happens, and that column
 * is "nothing" at every φ), and folding it would NOT be: under Rule 9's
 * controlled axis a global −1 becomes a RELATIVE phase on the control. */
cq_angle_class cq_angle_rz_row(double phi);

/* The module tolerance. `set` hard-errors outside [0, CQ_ANGLE_TOLERANCE_MAX];
 * zero is legal, and by D10 it admits exactly one angle — see angle.c. */
double cq_angle_tolerance(void);
void   cq_angle_set_tolerance(double rel);

#endif /* CQOPS_ANGLE_H */
