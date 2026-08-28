/* tests/test_rotate.c — M22, Step 19. PRD §7's twelve cells, and measurement.
 *
 * THE θ ≡ π ASYMMETRY IS THE POINT OF THIS SUITE (plan §4's Step 19 row): on a
 * CONSTANT bit `Ry(π)` flips it for zero gates and zero qubits, on a QUBIT it
 * emits `X` then `Z`. Everything else here exists to stop that one row from
 * being right by accident.
 *
 * WHAT THIS SUITE CANNOT SEE, said once so no case pretends otherwise. The
 * shadow models no phases at all (src/shadow.h), so nothing here can falsify a
 * global phase: not the `±i` that PRD §7 proves is unreachable for any spelling
 * of the `Z` out of `x` and `rz`, not the `−1` between `Ry(π)` and `Ry(3π)`
 * that §7's row deliberately merges, and not the sign Rule 15 calls global on a
 * definite constant. Those become observable only under §9's control, which is
 * PRD §15 D11 and Step 20. Every case below asserts something the model DOES
 * represent — a gate, an order, a bit kind, a shadow, a qubit index — and the
 * three that look like phase assertions (`the_z_of_a_half_turn_…`) are pinning
 * a CONVENTION, which is stated in the case rather than dressed up as a fact.
 *
 * WHY THERE IS NO GOLDEN FILE, deliberately and against the precedent of the
 * eleven kernels. Three reasons, and the third is decisive. (i) `cq_gold_row`
 * carries `{x, cx, ccx}` and every count that distinguishes an M22 row is
 * `ry`/`rz`/`mz`, so the format cannot hold the answer — a golden would be
 * green with `ry` emitted where `rz` belongs. (ii) `cq_gold_open`'s Bennett
 * commit check is risk R3's whole reason for the file being on disk, and M22
 * has NO upstream construction: §7 is ours (PRD §0), so it would pass NULL and
 * the golden degenerates into a static table kept in a text file. (iii) For a
 * kernel a golden pins a number derived from a closed form in W; here the
 * counts ARE the specification, so writing them to a regenerable file makes
 * `CQOPS_UPDATE_GOLDENS=1` bless exactly the forbidden direction — a row that
 * stopped emitting a rotation. Counts below are therefore computed from each
 * case's own inputs (W, the mask's popcount, cq_reg_owned_qubits) and never
 * read from disk.
 *
 * BOTH SPLIT SEAMS WERE TAKEN AT STEP 19 (Rule 12), and the file is four
 * pieces. It reached 437 lines against the 300-line hard limit — the first
 * suite in the project to need TWO splits at once — so the seams were taken as
 * planned rather than improvised:
 *
 *   test_rotate_table.inc    §7's table as DATA, plus the fixture and the three
 *                            readers. The SPECIFICATION both suites are written
 *                            against, in a different shape from the module.
 *   test_rotate_cells.inc    the table swept cell by cell, and the per-bit
 *                            column dispatch — seam 2, `the table itself ↔ the
 *                            claims ABOUT the table`.
 *   test_rotate_measure.inc  PRD §7's Measurement subsection — seam 1, which
 *                            this file recorded before the split was needed.
 *   this file                what a sweep cannot see: the ORDER of the
 *                            half-turn pair (bd lk0), which rows poison (D12),
 *                            and that the module consults its own tolerance.
 *
 * That leaves 257 here, 163 in cells, 93 in table and 78 in measure. The next case goes to
 * whichever piece owns its subject, not to the shortest one.
 */

#include "rotate.h"

#include "angle.h"
#include "bit.h"
#include "ctx.h"
#include "emit.h"
#include "qubits.h"
#include "reg.h"
#include "shadow.h"

#include "support/bitkinds.h"
#include "support/harness.h"
#include "support/mock_sink.h"
#include "support/poolcheck.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "test_rotate_table.inc"

#include "test_rotate_cells.inc"
#include "test_rotate_measure.inc"

/* --- bd lk0: the Z, and the order ---------------------------------------- */

/* `X` FIRST, THEN `Rz(π)`, PER BIT — the only case that can see the order.
 *
 * A gate COUNT cannot: swapping the two leaves every count, every value, every
 * bit kind and every shadow entry identical, because `ZX = −XZ` and the shadow
 * has no phases. Nor can a per-op count see the interleaving: emitting all W
 * `X`s and then all W `rz`s gives the same totals as the correct per-bit
 * alternation. Only an ORDERED comparison of the recorded stream does, which is
 * why PRD §10 names mock_sink as the detector with teeth on this surface.
 *
 * WHAT IS PINNED HERE IS A CONVENTION, NOT A DERIVED FACT, and PRD §7 says why:
 * the row spans θ ≡ π (mod 2π), which contains both k ≡ 1 and k ≡ 3 (mod 4),
 * and those differ by a global −1 — so NO fixed two-gate spelling is sign-exact
 * for the whole row, and reordering cannot make one. Uncontrolled the choice is
 * unobservable. It stops being a convention at Step 20 (PRD §15 D11). */
CQ_TEST(a_half_turn_on_a_qubit_is_x_then_rz_pi_in_that_order)
{
    fixture f; fx_open(&f);

    const uint32_t W = 3;
    int32_t h = cq_bk_reg(&f.ctx, W, 0u, 0x7u);
    const cq_bit *b = cq_reg_cbits(&f.ctx.regs, h);
    uint32_t q0 = cq_bit_qindex(b[0]), q1 = cq_bit_qindex(b[1]),
             q2 = cq_bit_qindex(b[2]);
    cq_mock_reset(&f.mock);

    cq_rotate_ry(&f.ctx, h, RT_PI);

    const cq_rec want[] = {
        CQ_REC_X(q0), CQ_REC_RZ(q0, RT_PI),
        CQ_REC_X(q1), CQ_REC_RZ(q1, RT_PI),
        CQ_REC_X(q2), CQ_REC_RZ(q2, RT_PI),
    };
    if (!cq_mock_matches(&f.mock, want, 6)) {
        cq_h_fail(__FILE__, __LINE__, "half turn: stream is not X;rz(pi) per bit");
        cq_mock_dump(&f.mock, "half turn");
    }

    fx_close(&f);
}

/* The angle handed to `sink.rz` is the nearest double to π, checked against a
 * SEPARATE spelling of it (RT_PI is `4·atan(1)`, not the module's own
 * CQ_ANGLE_PI). Bitwise, because angles compare bitwise everywhere in this
 * project — `%a` in the printf sink exists for the same reason.
 *
 * The ~1.2e-16 rad by which that double misses true π is forced by the `double`
 * sink ABI and is 10⁴ times inside D10's own 2·tol·π bound. It is not a defect
 * and must not be "fixed". */
CQ_TEST(the_z_of_a_half_turn_is_rz_at_the_nearest_double_to_pi)
{
    fixture f; fx_open(&f);

    int32_t h = cq_bk_reg(&f.ctx, 1, 0u, 0x1u);
    cq_mock_reset(&f.mock);
    cq_rotate_ry(&f.ctx, h, 3.0 * RT_PI);      /* the OTHER residue of the row */

    CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RZ), 1u);
    const cq_rec *r = cq_mock_at(&f.mock, 1);
    CHECK_EQ(r->op, CQ_OP_RZ);
    const double pi = RT_PI;
    CHECK(memcmp(&r->angle, &pi, sizeof pi) == 0);

    /* Not −π, and not the incoming 3π: the emitted Z is always the same gate,
     * whichever residue of the row got us here. */
    CHECK(r->angle > 0.0);

    fx_close(&f);
}

/* A general Ry passes θ through UNCHANGED, bitwise. M21 classifies; it never
 * snaps, and a module that rounded to the lattice would be deleting rotation. */
CQ_TEST(a_general_ry_emits_the_angle_it_was_given)
{
    const double ANGLES[] = { 0.5, -0.5, 1e-9, RT_PI / 2.0, 3.14 };

    for (int i = 0; i < 5; i++) {
        fixture f; fx_open(&f);
        int32_t h = cq_bk_reg(&f.ctx, 1, 0u, 0x1u);
        cq_mock_reset(&f.mock);

        cq_rotate_ry(&f.ctx, h, ANGLES[i]);

        CHECK_EQ(cq_mock_count(&f.mock), 1u);
        const cq_rec *r = cq_mock_at(&f.mock, 0);
        CHECK_EQ(r->op, CQ_OP_RY);
        CHECK(memcmp(&r->angle, &ANGLES[i], sizeof ANGLES[i]) == 0);
        fx_close(&f);
    }
}

/* THE Rz COLUMN'S (INDEX, ANGLE) PAIR, ORDERED — and it had no assertion at all
 * until a mutation battery asked for one.
 *
 * Every other Rz case in this suite asserts only `cq_mock_count_op(CQ_OP_RZ)`,
 * and `the_register_form_is_the_per_bit_form_in_a_loop` compares the module
 * against itself, so a shared bug cancels. Measured: BOTH
 * `cq_sink_rz(..., q, -phi)` and `cq_sink_rz(..., q & 0u, phi)` survived the
 * whole 174-test suite in both configurations before this case existed.
 *
 * Neither is visible anywhere else in the model. D12 says `Rz` never poisons and
 * never moves a basis value, so the shadow is identical; the counts, kinds and
 * pool are identical; and `Rz(−φ)` is a perfectly plausible gate. It becomes
 * observable only when the rail interferes, or at Step 20 under a control. */
CQ_TEST(a_general_rz_emits_its_angle_on_its_own_qubits)
{
    const double ANGLES[] = { 0.5, -0.5, 3.14, RT_PI / 3.0 };

    for (int i = 0; i < 4; i++) {
        fixture f; fx_open(&f);

        const uint32_t W = 3;
        int32_t h = cq_bk_reg(&f.ctx, W, 0u, 0x7u);
        const cq_bit *b = cq_reg_cbits(&f.ctx.regs, h);
        const cq_rec want[] = {
            CQ_REC_RZ(cq_bit_qindex(b[0]), ANGLES[i]),
            CQ_REC_RZ(cq_bit_qindex(b[1]), ANGLES[i]),
            CQ_REC_RZ(cq_bit_qindex(b[2]), ANGLES[i]),
        };
        cq_mock_reset(&f.mock);

        cq_rotate_rz(&f.ctx, h, ANGLES[i]);

        if (!cq_mock_matches(&f.mock, want, 3)) {
            cq_h_fail(__FILE__, __LINE__, "general Rz: wrong (index, angle) stream");
            cq_mock_dump(&f.mock, "general rz");
        }
        fx_close(&f);
    }
}

/* --- PRD §15 D12: which rows poison -------------------------------------- */

/* THE THIRD SEAM, RECORDED BEFORE IT IS NEEDED (Rule 12: a split is scheduled,
 * never improvised). This file took TWO splits at once at Step 19 — it reached
 * 437 against the 300 limit — and both recorded seams are now spent. Step 23
 * added one case here and it stands at 283, so the next one is named now rather
 * than improvised later: `which rows POISON, and what that costs at the FREE`
 * moves to tests/test_rotate_poison.inc — this section head down to the module
 * tolerance, i.e. only_a_general_ry_poisons_the_shadow,
 * a_rail_the_table_left_determinate_is_still_freeable and
 * a_general_ry_rail_strands_rather_than_aborting. Trigger at 290.
 *
 * It is a subject cut and not a size cut: those three are the only cases in the
 * file whose subject is the SHADOW's disposition rather than the emitted gate
 * stream, they are the only ones that free anything, and they are the ones D15
 * will keep moving as the certificate lands. Everything above them is §7's
 * twelve cells. */

/* THE MOST IMPORTANT CASE IN THE SUITE, in both directions.
 *
 * Omitting `cq_shadow_rotate` on a general Ry is invisible to every value,
 * every count, every ordered stream and every golden — and it makes the suite
 * QUIETER rather than louder, because a determinate shadow is what every other
 * reader wants. This case is the precise detector; three others go red with it
 * (both column sweeps and the poisoned-measurement case), and so does
 * `ckd18_a_general_ry_rail_cannot_be_freed` in the death suite — but only
 * because that case passes `cq_pc_zero_proof_rotation_free` rather than NULL.
 * With NULL it was blind, which is measured and is why it changed.
 *
 * Calling it on a row that must NOT poison is the mirror image: also correct in
 * value and count, also invisible — and what it costs is D12's actual payoff,
 * the EXACTNESS of cq_pc_zero_proof_rotation_free on every rail that met only
 * rows the table leaves determinate. (This sentence used to price that mutation
 * in "D12's twelve corpus rails", and that was false — those rails were never
 * D12's to lose. The cause is not the rotation, and it is stated once: PRD
 * §10's trap (ii) and §15 D12's own note. Measured 2026-08-22, PRD §15 D15.) */
CQ_TEST(only_a_general_ry_poisons_the_shadow)
{
    for (int c = 0; c < RT_N_CELLS; c++) {
        const rt_cell *cell = &RT_CELLS[c];
        fixture f; fx_open(&f);

        const uint32_t W = 4;
        int32_t h = cq_bk_reg(&f.ctx, W, 0x5u, 0xFu);
        const cq_bit *b = cq_reg_cbits(&f.ctx.regs, h);

        for (uint32_t i = 0; i < W; i++)
            CHECK(!cq_shadow_get(&f.ctx.shadow, cq_bit_qindex(b[i])).unknown);

        rt_apply_reg(&f.ctx, cell, h);

        for (uint32_t i = 0; i < W; i++) {
            cq_shadow s = cq_shadow_get(&f.ctx.shadow, cq_bit_qindex(b[i]));
            CHECK_EQ((int)s.unknown, cell->poisons);
        }
        fx_close(&f);
    }
}

/* D12's payoff, stated as the consequence rather than as the mechanism: a rail
 * that only ever met rows the table left determinate is STILL FREEABLE, and
 * every one of its qubit indices comes back.
 *
 * This is the clean half of `bd ckd.18` (closed; PRD §15 D15). Under the
 * rejected alternative — poison on any emitted rotation — the first two of
 * these three would hard-error. */
CQ_TEST(a_rail_the_table_left_determinate_is_still_freeable)
{
    int examined = 0;

    for (int c = 0; c < RT_N_CELLS; c++) {
        const rt_cell *cell = &RT_CELLS[c];
        if (cell->poisons) continue;
        examined++;

        fixture f; fx_open(&f);
        const uint32_t W = 4;
        const cq_pc_snap before = cq_pc_take(&f.ctx);

        /* Value ZERO, and it has to be: the rail must be provably |0> at the
         * free, and a half turn FLIPS it. So the pre-image of "all zero after
         * the row" is 0 for the rows that do not flip and all-ones for the ones
         * that do. */
        const uint64_t v = cell->c_flip ? 0xFu : 0u;
        int32_t h = cq_bk_reg(&f.ctx, W, v, 0xFu);
        rt_apply_reg(&f.ctx, cell, h);
        CHECK_EQ(rt_value(&f.ctx, h), 0u);

        uint32_t idx[8];
        uint32_t n = cq_pc_indices(&f.ctx, h, idx, 8);
        CHECK_EQ(n, W);

        cq_reg_free(&f.ctx, h, cq_pc_zero_proof_rotation_free);

        CHECK(cq_pc_same(cq_pc_take(&f.ctx), before));
        CHECK(cq_pc_indices_are_free(&f.ctx, idx, n));
        fx_close(&f);
    }

    /* Falsifiability: the loop must actually have run. A table edit that set
     * `poisons` everywhere would otherwise make this case vacuously green. */
    CHECK_EQ(examined, RT_N_CELLS - 2);   /* the two general-Ry rows poison */
}

/* AND THE OTHER HALF OF ckd.18: A GENERAL-Ry RAIL STRANDS. Its death-suite
 * sibling, test_rotate_death.c:ckd18_a_general_ry_rail_cannot_be_freed, keeps
 * the REFUSAL under CQOPS_FREE_ABORT; this is the DEFAULT disposition PRD §15
 * D15 §4 confirmed on 2026-08-22, and without it the default would be the one
 * behaviour ckd.18's own fixture never exercised.
 *
 * WHY THE VERDICT IS UNPROVEN AND NOT DIRTY, which is the thing a reader will
 * get wrong. The corpus's ckd.18 rails are born from a NON-ZERO literal and
 * carry a CANCELLING (θ, −θ) pair, and D15 §4 CONVICTS them on exactly those
 * two facts — read off the CALL STREAM, at M26, by a certificate that does not
 * exist yet (`bd 06t`). This fixture has neither: it is born |0> with one
 * uncancelled Ry, and the only evidence in the tree today is the shadow, which
 * reports `unknown` and can therefore only ever say UNPROVEN. Asserting a
 * conviction here would be claiming evidence the library does not hold — the
 * failure mode `bd 06t` names by hand — so the case asserts what is true now
 * and says what would change it.
 *
 * BOTH ROWS TAKE THE SAME ACT ANYWAY, which is D15 §4's whole point, so the
 * strand assertions below are unaffected either way. */
CQ_TEST(a_general_ry_rail_strands_rather_than_aborting)
{
    fixture f; fx_open(&f);
    const uint32_t W = 4;

    /* BEFORE THE RAIL EXISTS, which is what cq_pc_same means and is easy to get
     * wrong: it asks whether the pool came back to where it started, so a
     * snapshot taken after the qubits are already live compares 4 against 4 and
     * the net-of-strands correction inverts. Taken here, both sides are
     * `live - stranded` == 0, and a qubit that leaked WITHOUT being stranded
     * moves only the first term and is caught. */
    const cq_pc_snap before = cq_pc_take(&f.ctx);

    int32_t h = cq_reg_alloc_zero(&f.ctx.regs, W);

    cq_rotate_ry(&f.ctx, h, 0.5);          /* materialises AND poisons all four */

    uint32_t idx[8];
    uint32_t n = cq_pc_indices(&f.ctx, h, idx, 8);
    CHECK_EQ(n, W);
    CHECK_EQ(cq_reg_disposition(&f.ctx, h, cq_pc_zero_proof_rotation_free), 0);

    uint32_t live_before = cq_qubits_live(&f.ctx.pool);
    cq_reg_free(&f.ctx, h, cq_pc_zero_proof_rotation_free);

    /* NAMED, NOT COUNTED — after the free tombstones the rail these indices are
     * unrecoverable from the register side, so this is the only thing that can
     * say WHICH ones leaked. */
    for (uint32_t i = 0; i < n; i++) {
        CHECK(cq_qubits_is_stranded(&f.ctx.pool, idx[i]));
        CHECK(!cq_qubits_is_free(&f.ctx.pool, idx[i]));
    }
    CHECK_EQ(cq_qubits_stranded(&f.ctx.pool), W);
    CHECK_EQ(cq_qubits_free(&f.ctx.pool), 0);
    CHECK_EQ(cq_qubits_live(&f.ctx.pool), live_before);   /* still live: nobody got them back */

    /* AND THE SAME CLAIM THROUGH poolcheck, which is what gives `bd evv`'s
     * exemption a consumer and therefore a detector. Delete the stranded
     * exemption in cq_pc_live_is_exactly and this line goes red; strand the
     * WRONG index and cq_pc_indices_settled goes red naming it, which is why
     * the exemption is spelled as a SET rather than a count. The hand-rolled
     * assertions above are kept deliberately: they are the independent check
     * that does not go through the predicate being exempted. */
    CHECK(cq_pc_indices_settled(&f.ctx, idx, n, W));
    CHECK(cq_pc_live_is_exactly(&f.ctx, NULL, 0));
    CHECK(cq_pc_same(cq_pc_take(&f.ctx), before));
    fx_close(&f);
}

/* THE SHAPE, ON A CLEAN SHADOW — AND THE SCOPE IS IN THE NAME BECAUSE A NAME
 * OUTLIVES A COMMENT. The Fredkin's Toffoli — two quantum controls, a constant
 * target — MATERIALISES `tmp` before the `rz` arrives, so §7's Rz-CONSTANT
 * cell never applies to this shape at all; and because the `rz` sits on a row
 * D12 leaves determinate, the bracket's own inverse returns `tmp` to |0> with
 * a shadow that still tracks it. That is the claim, end to end, and every
 * assertion below is unchanged.
 *
 * WHAT THIS CASE IS NOT, corrected 2026-08-22 (PRD §15 D15). It was called
 * `the_cswap_phase_kickback_shape_frees_cleanly` and its header called it "THE
 * CORPUS SHAPE, REPRODUCED" — the claim being that CQ_lang's `rz`-rooted
 * corpus frees are this shape and that D12 is what keeps them freeable. The
 * shape half is right; the FREE half is false. This fixture mints BOTH
 * operands with `cq_bk_reg`, i.e. determinate quantum rails, which the corpus
 * never has — and its own comment below concedes as much ("nothing here
 * poisoned"). WHY the corpus rails differ is stated once — PRD §10's trap (ii)
 * and §15 D12's own note — and it is not the rotation: the shadow refuses those
 * rails, and D15's call-stream certificate is what discharges them. D12 itself
 * is untouched: a diagonal rotation genuinely does not poison, which is exactly
 * what this case still pins. It reproduces the SHAPE, not the STATE.
 *
 * `src/angle.h` asserted the opposite about the Rz-CONSTANT cell until Step 19
 * measured it. */
CQ_TEST(an_rz_in_a_cswap_bracket_on_determinate_operands_frees_cleanly)
{
    fixture f; fx_open(&f);

    const uint32_t W = 4;
    const cq_pc_snap before = cq_pc_take(&f.ctx);

    int32_t flag = cq_bk_reg(&f.ctx, 1, 1u, 0x1u);    /* a quantum control     */
    int32_t src  = cq_bk_reg(&f.ctx, W, 0xAu, 0xFu);  /* a quantum data rail   */
    int32_t tmp  = cq_reg_alloc_zero(&f.ctx.regs, W); /* born all-constant 0   */

    const cq_bit *fb = cq_reg_cbits(&f.ctx.regs, flag);

    /* cswap(flag, src, tmp), twice, with the rz between — PRD §2.1's Fredkin
     * per bit: CX(b,a); CCX(ctrl,a,b); CX(b,a). */
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t i = 0; i < W; i++) {
            cq_bit *a = &cq_reg_bits(&f.ctx.regs, src)[i];
            cq_bit *t = &cq_reg_bits(&f.ctx.regs, tmp)[i];
            cq_emit_cx(&f.ctx, t, a);
            cq_emit_ccx(&f.ctx, &fb[0], a, t);
            cq_emit_cx(&f.ctx, t, a);
        }
        if (pass == 0) {
            CHECK_EQ(rt_quantum_bits(&f.ctx, tmp), W);   /* materialised, as measured */
            cq_rotate_rz(&f.ctx, tmp, 0.25);
            CHECK_EQ(rt_poisoned_bits(&f.ctx, tmp), 0u); /* D12                       */
        }
    }

    /* The Fredkin is its own inverse, so `tmp` is back at |0> and `src` is
     * restored — the shadow tracks both exactly, because nothing here poisoned. */
    CHECK_EQ(rt_value(&f.ctx, tmp), 0u);
    CHECK_EQ(rt_value(&f.ctx, src), 0xAu);
    CHECK_EQ(rt_poisoned_bits(&f.ctx, tmp), 0u);

    uint32_t idx[8];
    uint32_t n = cq_pc_indices(&f.ctx, tmp, idx, 8);
    CHECK_EQ(n, W);
    cq_reg_free(&f.ctx, tmp, cq_pc_zero_proof_rotation_free);
    CHECK(cq_pc_indices_are_free(&f.ctx, idx, n));

    /* `src` and `flag` deliberately stay live and are NOT freed: they hold
     * non-zero values, so Rule 6 would refuse them, which is correct and is a
     * different case from this one. `before` is therefore not restorable here
     * and is not asserted — only tmp's W indices came back. */
    (void)before;
    fx_close(&f);
}

/* --- The module tolerance ------------------------------------------------ */

/* M22 MUST ASK M21 AT THE MODULE TOLERANCE, not at the default.
 *
 * This is the Step 18 finding arriving one layer up: `cq_angle_rz_row` ignoring
 * the module tolerance survived M21's entire 39-mutant battery, because every
 * other case ran at the default. A suite that never moves the tolerance cannot
 * tell `cq_angle_ry_row(θ)` from `cq_angle_lattice(θ, CQ_ANGLE_TOLERANCE_DEFAULT)`
 * — and the assertion has to be on the EMISSION, not on the classification,
 * because the classification is M21's and is already covered. */
CQ_TEST(the_module_tolerance_is_consulted_rather_than_the_default)
{
    /* 1.5927e-3 rad short of π: GENERAL at the default (5.1e8 windows away),
     * HALF_TURN at the cap (inside 3.1416e-3). */
    const double near_pi = 3.14;

    {   /* default: a real rotation */
        fixture f; fx_open(&f);
        cq_angle_set_tolerance(CQ_ANGLE_TOLERANCE_DEFAULT);
        int32_t h = cq_bk_reg(&f.ctx, 1, 0u, 0x1u);
        cq_mock_reset(&f.mock);
        cq_rotate_ry(&f.ctx, h, near_pi);
        CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RY), 1u);
        CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_X),  0u);
        fx_close(&f);
    }
    {   /* at the cap: the same angle folds to a half turn and becomes X;rz */
        fixture f; fx_open(&f);
        cq_angle_set_tolerance(CQ_ANGLE_TOLERANCE_MAX);
        int32_t h = cq_bk_reg(&f.ctx, 1, 0u, 0x1u);
        cq_mock_reset(&f.mock);
        cq_rotate_ry(&f.ctx, h, near_pi);
        CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RY), 0u);
        CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_X),  1u);
        CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RZ), 1u);
        fx_close(&f);
    }
    {   /* tol = 0 admits only θ == 0, so even π is a general rotation */
        fixture f; fx_open(&f);
        cq_angle_set_tolerance(0.0);
        int32_t h = cq_bk_reg(&f.ctx, 1, 0u, 0x1u);
        cq_mock_reset(&f.mock);
        cq_rotate_ry(&f.ctx, h, RT_PI);
        CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RY), 1u);
        CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RZ), 0u);
        fx_close(&f);
    }

    /* THE Rz HALF, AND IT WAS MISSING UNTIL A MUTATION BATTERY ASKED FOR IT.
     * `cq_angle_rz_row` reads the module tolerance too, and every Rz case above
     * uses an angle whose row is the same at every tolerance — so replacing the
     * call with `cq_angle_lattice(phi, CQ_ANGLE_TOLERANCE_DEFAULT)` survived the
     * whole suite. This is the Step 18 finding in its Rz form, one layer up: the
     * discriminator has to be an angle whose IDENTITY classification MOVES with
     * the tolerance. `4π + 2e-3` is 2e-3 from the lattice point, inside
     * MAX·π = 3.1416e-3 and 6.4e8 windows outside the default's. */
    {
        const double near_4pi = 4.0 * RT_PI + 2e-3;

        fixture f; fx_open(&f);
        cq_angle_set_tolerance(CQ_ANGLE_TOLERANCE_MAX);
        int32_t h = cq_bk_reg(&f.ctx, 1, 0u, 0x1u);
        cq_mock_reset(&f.mock);
        cq_rotate_rz(&f.ctx, h, near_4pi);
        CHECK_EQ(cq_mock_count(&f.mock), 0u);          /* folds to nothing */
        fx_close(&f);

        fixture g; fx_open(&g);
        cq_angle_set_tolerance(CQ_ANGLE_TOLERANCE_DEFAULT);
        int32_t k = cq_bk_reg(&g.ctx, 1, 0u, 0x1u);
        cq_mock_reset(&g.mock);
        cq_rotate_rz(&g.ctx, k, near_4pi);
        CHECK_EQ(cq_mock_count_op(&g.mock, CQ_OP_RZ), 1u);   /* a real rotation */
        fx_close(&g);
    }

    cq_angle_set_tolerance(CQ_ANGLE_TOLERANCE_DEFAULT);
}

/* THE CORPUS'S OWN `3.14` IS AN `rz` ANGLE, and the Rz column has no half-turn
 * row to fall into — so it is a real rotation at EVERY legal tolerance,
 * including the cap. Three documents said otherwise (PRD §15 D10, CLAUDE.md's
 * Rule 15, and tests/test_angle.c, which pinned the Ry column and had no Rz
 * case at all); the cap is still right, but its witness was on the wrong
 * column. Corrected at Step 19. */
CQ_TEST(the_corpus_rz_angle_is_a_real_rotation_at_every_legal_tolerance)
{
    static const double TOLS[] = { 0.0, CQ_ANGLE_TOLERANCE_DEFAULT, 1e-6,
                                   CQ_ANGLE_TOLERANCE_MAX };

    for (int i = 0; i < 4; i++) {
        fixture f; fx_open(&f);
        cq_angle_set_tolerance(TOLS[i]);

        int32_t h = cq_bk_reg(&f.ctx, 1, 0u, 0x1u);
        cq_mock_reset(&f.mock);
        cq_rotate_rz(&f.ctx, h, 3.14);

        CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_RZ), 1u);
        CHECK_EQ(cq_mock_count_op(&f.mock, CQ_OP_X),  0u);
        CHECK_EQ(cq_angle_rz_row(3.14), CQ_ANGLE_GENERAL);
        fx_close(&f);
    }
    cq_angle_set_tolerance(CQ_ANGLE_TOLERANCE_DEFAULT);
}

CQ_TEST_MAIN(
    CQ_CASE(the_constant_column_of_every_prd7_row),
    CQ_CASE(the_qubit_column_of_every_prd7_row),
    CQ_CASE(a_half_turn_on_a_qubit_is_x_then_rz_pi_in_that_order),
    CQ_CASE(the_z_of_a_half_turn_is_rz_at_the_nearest_double_to_pi),
    CQ_CASE(a_general_ry_emits_the_angle_it_was_given),
    CQ_CASE(a_general_rz_emits_its_angle_on_its_own_qubits),
    CQ_CASE(the_column_is_chosen_per_bit_not_per_register),
    CQ_CASE(a_general_ry_on_a_constant_materialises_once_then_rotates),
    CQ_CASE(only_a_general_ry_poisons_the_shadow),
    CQ_CASE(a_rail_the_table_left_determinate_is_still_freeable),
    CQ_CASE(a_general_ry_rail_strands_rather_than_aborting),
    CQ_CASE(an_rz_in_a_cswap_bracket_on_determinate_operands_frees_cleanly),
    CQ_CASE(the_module_tolerance_is_consulted_rather_than_the_default),
    CQ_CASE(the_corpus_rz_angle_is_a_real_rotation_at_every_legal_tolerance),
    CQ_CASE(the_rows_are_width_generic_to_the_widest_register),
    CQ_CASE(the_register_form_is_the_per_bit_form_in_a_loop),
    CQ_CASE(measurement_returns_the_shadow_and_zero_for_unknown),
    CQ_CASE(measurement_of_a_poisoned_bit_is_zero_not_the_stale_value),
    CQ_CASE(measurement_spans_both_words_at_the_widest_register),
    CQ_CASE(a_measured_rail_keeps_its_qubits_forever)
)
