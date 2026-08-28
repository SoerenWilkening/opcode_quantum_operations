/* src/rotate.c — M22. PRD §7's twelve cells, and nothing else.
 *
 * The table is walked as CONTROL FLOW here — a switch on M21's row, then a
 * branch on the bit's kind — while tests/test_rotate_table.inc states the same
 * table as a flat list of cells with the counts spelled out. That is
 * deliberate: two shapes cannot be read off one another, so a transcription
 * error is a disagreement rather than a shared answer.
 *
 * Read src/rotate.h before changing anything here. In particular: which rows
 * poison is PRD §15 D12 and is not a local choice; the `Z` being `sink.rz(q,
 * pi)` is bd lk0 and PRD §7; and the sandwich refusal is what stands in for
 * both I6 mechanisms, neither of which reaches a module that bypasses
 * cq_emit_* to emit.
 */

#include "rotate.h"

#include "angle.h"
#include "controlled.h"
#include "emit.h"
#include "reg.h"
#include "shadow.h"
#include "sink.h"

#include <stdio.h>
#include <stdlib.h>

static void cq_rotate_die(const char *what, const char *op)
{
    fprintf(stderr, "libcqops: FATAL: rotate: %s (%s)\n", what, op);
    abort();
}

/* BOTH CONFIGURATIONS. ctx->sandwich_depth is not Debug-gated (src/ctx.h) for
 * exactly this kind of premise, and the failure it prevents has no detector:
 * a compute half that rotates emits a stream that IS a palindrome while the two
 * halves compose to Ry(2.theta) rather than cancelling. */
static void refuse_inside_a_sandwich(const cq_ctx *ctx, const char *op)
{
    if (ctx->sandwich_depth != 0)
        cq_rotate_die("a rotation or measurement inside a sandwich compute "
                      "half: Ry is not an involution and mz is not reversible, "
                      "so the reverse replay cannot cancel (Rule 4, I6)", op);
}

/* --- PRD §15 D11, named row by row ---------------------------------------- */

/* THE STRING IS THE POINT. §9's promotion table says a §7 FOLD row is a hard
 * error in v1, and D11 tabulates the control-side phase each one owes; a
 * refusal that could not say WHICH row would leave the reader to rediscover the
 * table. Naming the half turn's parity is also what bd fna's split bought — the
 * constant column owes pi.b at k = 1 and pi.(1-b) at k = 3, and the qubit
 * column -pi/2 against +pi/2, so the two are different gates and were one class
 * until Step 20. */
static const char *half_turn_row(int neg, int was_qubit)
{
    if (was_qubit)
        return neg ? "Ry, theta = 3pi (mod 4pi), qubit column (alpha = +pi/2)"
                   : "Ry, theta = pi  (mod 4pi), qubit column (alpha = -pi/2)";

    return neg ? "Ry, theta = 3pi (mod 4pi), constant column (alpha = pi.(1-b))"
               : "Ry, theta = pi  (mod 4pi), constant column (alpha = pi.b)";
}

/* --- PRD §7, the Ry column ----------------------------------------------- */

void cq_rotate_ry_bit(cq_ctx *ctx, cq_bit *b, double theta)
{
    refuse_inside_a_sandwich(ctx, "ry");

    /* §9 row 0: a CQ_BIT_ZERO control skips the region, 0 gates and 0 qubits.
     * It has to be tested HERE rather than left to cq_emit_x, because the
     * general row below materialises before it emits — so an M22 that relied on
     * the emitter alone would take W qubits for a rotation that does not
     * happen. Row 0's CQ_BIT_ONE case needs nothing: the region is the
     * uncontrolled one verbatim, which is what this function already is. */
    if (cq_ctrl_skipping(&ctx->ctrl)) return;

    const cq_angle_class row = cq_angle_ry_row(theta);

    switch (row) {
    case CQ_ANGLE_IDENTITY:       /* theta = 0  (mod 4pi): the operator is I  */
        return;                   /* controlled-I is I; exempt from D11       */

    case CQ_ANGLE_NEG_IDENTITY:   /* theta = 2pi (mod 4pi): -I, a global -1   */
        /* A global phase is only global until something controls it: this cell
         * owes Rz(pi) on the control wire, PER BIT — a W-bit Ry(2pi) is
         * (-1)^W, so one Z per register is a miscompile at every even W. */
        cq_ctrl_refuse_fold_row(ctx,
            "Ry, theta = 2pi (mod 4pi), either column (alpha = pi)");
        return;                   /* both columns: nothing                    */

    /* THE TWO PARITIES FALL THROUGH TO ONE BODY, and that is the point of the
     * split rather than an oversight (bd fna). Uncontrolled they are the same
     * cell — Ry(pi) and Ry(3pi) differ by a global -1 no instrument here can
     * see — so M22 must keep emitting the identical pair for both;
     * tests/test_rotate_table.inc pins k = 1, k = 3 and k = -1 as byte-
     * identical and is the regression guard for exactly that. What M21's new
     * class buys is that D11 can NAME which parity it is refusing. */
    case CQ_ANGLE_HALF_TURN:
    case CQ_ANGLE_NEG_HALF_TURN: {
        /* Rule 15's asymmetry, and cq_emit_x IS the split: on a constant it
         * flips in place for zero gates and zero qubits, on a qubit it emits
         * the gate and updates the shadow. The kind is CAPTURED because the
         * `Z` is owed only on the qubit column. Capturing it AFTER the call
         * would be equivalent today — cq_emit_x never changes a bit's kind, a
         * constant flipping in place and a Q bit staying Q — and a mutant that
         * reads it after survives the whole suite, deliberately. It stops
         * being equivalent the moment the flip can allocate, which is exactly
         * what M06 makes it do at Step 20 (cq_emit_x promotes to cq_emit_cx,
         * and a quantum control materialises a constant target). */
        const int was_qubit = cq_bit_is_qubit(*b);

        /* BOTH COLUMNS FOLD, so both refuse — the constant one by emitting
         * nothing at all, the qubit one by realising the row only up to the
         * `+i` of Rz(pi).X = Y. §9's wording ("a §7 fold row") is deliberately
         * broader than "the zero-gate rows" for exactly this cell. */
        cq_ctrl_refuse_fold_row(ctx,
            half_turn_row(row == CQ_ANGLE_NEG_HALF_TURN, was_qubit));

        cq_emit_x(ctx, b);
        if (was_qubit)
            cq_sink_rz(ctx->sink, cq_bit_qindex(*b), CQ_ANGLE_PI);   /* bd lk0 */
        return;                                        /* D12: no poison here */
    }

    case CQ_ANGLE_GENERAL:
        break;
    }

    /* The general row, and the ONLY cell in the table that allocates or
     * poisons. A constant is materialised first (Rule 5's single allocator),
     * which is what makes `Ry` on a classical rail cost W qubits. */
    if (cq_bit_is_const(*b)) cq_materialise(ctx, b);
    {
        const uint32_t q = cq_bit_qindex(*b);

        /* THE ONE §7 ROW THAT NEEDS NO REFUSAL: it does not fold, so §9's
         * `R(theta/2); CX; R(-theta/2); CX` promotes it EXACTLY, with no
         * residual phase to hand-derive. cq_ctrl_ry is that, and is one
         * cq_sink_ry when no region is open. The shadow effect stays here
         * because it is D12's decision, not the emitter's — and the promotion
         * adds none of its own, since its two CXs cancel. */
        cq_ctrl_ry(ctx, q, theta);
        cq_shadow_rotate(&ctx->shadow, q);
    }
}

/* --- PRD §7, the Rz column ----------------------------------------------- */

void cq_rotate_rz_bit(cq_ctx *ctx, cq_bit *b, double phi)
{
    refuse_inside_a_sandwich(ctx, "rz");
    if (cq_ctrl_skipping(&ctx->ctrl)) return;                    /* §9 row 0 */

    /* §7 gives Rz exactly ONE special row, phi = 0 (mod 4pi). phi = 2pi is -I
     * and phi = pi is -iZ, and neither is folded — cq_angle_rz_row collapses
     * both into GENERAL on purpose, because folding a global phase is wrong the
     * moment §9 controls it (D11). */
    if (cq_angle_rz_row(phi) == CQ_ANGLE_IDENTITY) return;

    /* The constant column at every other phi: nothing. A diagonal on a definite
     * value is a global phase — it is not even representable, since by I4 the
     * bit owns no wire. This is the cell that keeps a classical rail classical
     * through any number of Rz calls. */
    if (cq_bit_is_const(*b)) {
        cq_ctrl_refuse_fold_row(ctx,
            "Rz, otherwise, constant column (alpha = (2b-1).phi/2)");
        return;
    }

    /* The qubit column. No cq_shadow_rotate: D12 — and the promotion adds none
     * either, because controlled-Rz is diagonal exactly as Rz is, so the whole
     * four-gate block still cannot move a computational-basis value. What that
     * buys is that D12 stays EXACT under §9 rather than merely conservative.
     *
     * IT DOES NOT BUY THE CORPUS'S RAILS WHOSE LAST ROTATION IS AN rz THEIR
     * FREE, and this comment said it did — "that is what keeps the corpus's
     * twelve rz-rooted rails freeable under the axis" — until 2026-08-22,
     * when that was measured FALSE. ("rz-rooted" is itself an artefact of
     * mis-modelling cqrt_cswap's write set.) The cause is not the rotation
     * and is stated once, in PRD §10's trap (ii) and §15 D12's own note;
     * what discharges those rails is the observed undo certificate over the
     * call stream at M26 (PRD §15 D15), not the shadow. D12 itself is
     * unaffected: a diagonal genuinely does not poison. */
    cq_ctrl_rz(ctx, cq_bit_qindex(*b), phi);
}

/* --- The register forms -------------------------------------------------- */

/* cq_reg_bits refuses a tombstone and refuses a MEASURED rail, so both of those
 * lifetime errors are M07's hard error reached through here rather than a
 * second copy of the check. The bits array is its own allocation and is stable
 * for the register's life, so materialising inside the loop cannot move it. */
void cq_rotate_ry(cq_ctx *ctx, int32_t h, double theta)
{
    cq_bit *bits = cq_reg_bits(&ctx->regs, h);
    const uint32_t w = cq_reg_width(&ctx->regs, h);

    for (uint32_t i = 0; i < w; i++) cq_rotate_ry_bit(ctx, &bits[i], theta);
}

void cq_rotate_rz(cq_ctx *ctx, int32_t h, double phi)
{
    cq_bit *bits = cq_reg_bits(&ctx->regs, h);
    const uint32_t w = cq_reg_width(&ctx->regs, h);

    for (uint32_t i = 0; i < w; i++) cq_rotate_rz_bit(ctx, &bits[i], phi);
}

/* --- Measurement (PRD §7) ------------------------------------------------ */

void cq_measure(cq_ctx *ctx, int32_t h, uint64_t *lo, uint64_t *hi)
{
    refuse_inside_a_sandwich(ctx, "measure");
    cq_ctrl_refuse_measurement(ctx);

    /* MARK FIRST, AND THE ORDER IS THE POINT. cq_reg_mark_measured refuses any
     * state but LIVE, so a second measure — or a measure of a tombstone —
     * aborts having emitted ZERO gates. Marking afterwards would push W `mz`
     * gates at the sink and then abort. Nothing below can fail on a
     * well-formed rail, so there is no half-marked state to worry about. */
    cq_reg_mark_measured(&ctx->regs, h);

    /* cq_reg_cbits reads a MEASURED rail; cq_reg_bits (mutable) refuses it,
     * which forecloses ever writing a bit here. Measurement must not. */
    const cq_bit  *b = cq_reg_cbits(&ctx->regs, h);
    const uint32_t w = cq_reg_width(&ctx->regs, h);
    uint64_t v[2] = { 0u, 0u };

    for (uint32_t i = 0; i < w; i++) {
        int bit;

        if (cq_bit_is_qubit(b[i])) {
            const uint32_t   q = cq_bit_qindex(b[i]);
            const cq_shadow  s = cq_shadow_get(&ctx->shadow, q);

            cq_sink_mz(ctx->sink, q);
            bit = s.unknown ? 0 : (int)s.value;
        } else {
            /* A constant owns no wire (I4), so there is nothing to measure and
             * no `mz` to emit — its kind IS its value. */
            bit = cq_bit_value(b[i]);
        }

        if (bit) v[i >> 6] |= (uint64_t)1 << (i & 63u);
    }

    *lo = v[0];
    *hi = v[1];
}
