/* src/reg_check.c — M07's invariant checking, split from reg.c on the seam
 * IMPLEMENTATION_PLAN §3 recorded before either half was written: `table ↔
 * invariant checking`, triggered when reg.c passes 240. Step 23 is what
 * triggered it — PRD §15 D15's free-time disposition (bd 06t) lands in the
 * table half and took it past the threshold — so this file is the scheduled
 * split and not a surprise refactor (Rule 12).
 *
 * WHAT IS HERE: the D7a/D7b operand checks and the I2 owner-map sweep. What
 * they have in common, and what the seam actually is: nothing in this file
 * mutates anything. They read the table and abort. reg.c owns the table, the
 * mint path, the accessors, the free path and the physical copy.
 *
 * cq_reg_xor_into stayed in reg.c deliberately, though it sits next to
 * cq_reg_check_operands in the header: it EMITS, and D7b's defensive copy
 * (bd 493) lands on it at Step 23. That is a mutation, and the seam is
 * mutation ↔ inspection. */

#include "reg.h"

#include "ctx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(CQOPS_DEBUG_INVARIANTS) && CQOPS_DEBUG_INVARIANTS
#  define CQ_REG_DEBUG 1
#else
#  define CQ_REG_DEBUG 0
#endif

/* The same shape and the same "reg:" prefix as reg.c's own die, so the
 * FAIL_REGULAR_EXPRESSION pins in tests/CMakeLists.txt — which discriminate on
 * the MESSAGE, not on the module — are unaffected by the split. A second
 * static in a second translation unit is the cheapest way to keep that true;
 * the alternative is a private header for one five-line function. */
static void cq_regchk_die(const char *what, long a, long b)
{
    fprintf(stderr, "libcqops: FATAL: reg: %s (%ld, %ld)\n", what, a, b);
    abort();
}

/* --- D7. Both halves, and they come out opposite ways (reg.h). ----------- */

void cq_reg_check_operands(const cq_reg_table *t, int32_t out,
                           const int32_t *srcs, uint32_t n)
{
    if (out != CQ_REG_NONE && !cq_reg_is_live(t, out))
        cq_regchk_die("operand check: the result handle is not a live rail", out, 0);

    for (uint32_t i = 0; i < n; i++) {
        if (!cq_reg_is_live(t, srcs[i]))
            cq_regchk_die("operand check: a source handle is not a live rail",
                          srcs[i], (long)i);
        /* D7a only. Source-source aliasing is D7b and is LEGAL — CQ_lang ships
         * ten integer-surface fixture lines that do it. See reg.h. */
        if (out != CQ_REG_NONE && srcs[i] == out)
            cq_regchk_die("D7a: the result handle is also a source", out, (long)i);
    }
}

int cq_reg_sources_alias(const int32_t *srcs, uint32_t n)
{
    for (uint32_t i = 0; i + 1u < n; i++)
        for (uint32_t j = i + 1u; j < n; j++)
            if (srcs[i] == srcs[j]) return 1;
    return 0;
}

/* --- I2, by sweep. Debug-only by plan §2.1. ------------------------------ */

void cq_reg_audit(const cq_ctx *ctx)
{
#if CQ_REG_DEBUG
    const cq_reg_table *t = &ctx->regs;
    uint32_t minted = cq_qubits_minted(&ctx->pool);
    int32_t *owner = NULL;

    if (minted > 0u) {
        owner = malloc((size_t)minted * sizeof *owner);
        if (!owner) cq_regchk_die("out of memory building the I2 owner map", (long)minted, 0);
        memset(owner, 0xFF, (size_t)minted * sizeof *owner);   /* == CQ_REG_NONE */
    }

    /* MEASURED slots are swept and DEAD ones are not: a measured rail still
     * owns its qubits (they are deliberately never reclaimed), while a
     * tombstone's bits array is gone. I2 is scoped to live registers. */
    int32_t n_slots = cq_reg_count(t);
    for (int32_t h = 0; h < n_slots; h++) {
        if (cq_reg_state(t, h) == CQ_SLOT_DEAD) continue;

        /* THROUGH THE PUBLIC ACCESSORS, not reg.c's static slot reader, and
         * that is an improvement rather than a concession to the split: they
         * are the validating path (a zeroed slot aborts there), so the sweep
         * now inherits the same state check every other reader gets instead of
         * carrying a second copy of it. Duplicating the reader here would be
         * the masking-layer trap plan §0 records — a guard whose deletion is
         * hidden by an identical copy one file over. */
        uint32_t      w    = cq_reg_width(t, h);
        const cq_bit *bits = cq_reg_cbits(t, h);

        for (uint32_t i = 0; i < w; i++) {
            cq_bit b = bits[i];
            if (!cq_bit_valid(b))
                cq_regchk_die("I1: malformed bit — a constant carrying a qubit index", h, (long)i);
            if (!cq_bit_is_qubit(b)) continue;

            uint32_t q = cq_bit_qindex(b);
            if (q >= minted)
                cq_regchk_die("register holds a qubit index that was never minted", h, (long)q);
            if (cq_qubits_is_free(&ctx->pool, q))
                cq_regchk_die("register holds a qubit that is on the free list", h, (long)q);
            if (owner[q] == h)
                cq_regchk_die("I2: one register holds the same qubit index twice", h, (long)q);
            if (owner[q] != CQ_REG_NONE)
                cq_regchk_die("I2: qubit index held by two live registers", owner[q], (long)q);
            owner[q] = h;
        }
    }

    free(owner);
#else
    (void)ctx;
#endif
}
