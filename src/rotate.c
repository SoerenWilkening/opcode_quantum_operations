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

/* --- PRD §7, the Ry column ----------------------------------------------- */

void cq_rotate_ry_bit(cq_ctx *ctx, cq_bit *b, double theta)
{
    refuse_inside_a_sandwich(ctx, "ry");

    switch (cq_angle_ry_row(theta)) {
    case CQ_ANGLE_IDENTITY:       /* theta = 0  (mod 4pi): the operator is I  */
    case CQ_ANGLE_NEG_IDENTITY:   /* theta = 2pi (mod 4pi): -I, a global -1   */
        return;                   /* both columns: nothing. D11 under control */

    case CQ_ANGLE_HALF_TURN: {
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
        cq_sink_ry(ctx->sink, q, theta);
        cq_shadow_rotate(&ctx->shadow, q);
    }
}

/* --- PRD §7, the Rz column ----------------------------------------------- */

void cq_rotate_rz_bit(cq_ctx *ctx, cq_bit *b, double phi)
{
    refuse_inside_a_sandwich(ctx, "rz");

    /* §7 gives Rz exactly ONE special row, phi = 0 (mod 4pi). phi = 2pi is -I
     * and phi = pi is -iZ, and neither is folded — cq_angle_rz_row collapses
     * both into GENERAL on purpose, because folding a global phase is wrong the
     * moment §9 controls it (D11). */
    if (cq_angle_rz_row(phi) == CQ_ANGLE_IDENTITY) return;

    /* The constant column at every other phi: nothing. A diagonal on a definite
     * value is a global phase — it is not even representable, since by I4 the
     * bit owns no wire. This is the cell that keeps a classical rail classical
     * through any number of Rz calls. */
    if (cq_bit_is_const(*b)) return;

    /* The qubit column. No cq_shadow_rotate: D12. */
    cq_sink_rz(ctx->sink, cq_bit_qindex(*b), phi);
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
