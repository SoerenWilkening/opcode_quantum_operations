/* src/kernels/fmisc_int.h — M38, K24: the seams BETWEEN the fmin/fmax files,
 * recorded in docs/constructions/K24.md §3 before a line was written.
 *
 *   fmisc_rows.c    ROW TABLES — soft_fmin / soft_fmax, one row per operator
 *                   occurrence, and their accessors. Knows no costs.
 *   fmisc_step.c    STEP MACHINE — costs and layout ASKED of each module,
 *                   operand resolution, the one-gate dispatch, the block API.
 *                   Knows no Julia.
 *   fmisc_minmax.c  DISPATCH-AND-SURFACE — the two Rule 7 kernels and the
 *                   classical row.
 *
 * Everything below is exported for those files and for the death binary,
 * which drives each refusal DIRECTLY: the block names a program rather than
 * carrying a row pointer, so no caller can hand the machine a malformed
 * table, and a refusal that takes a row, an op or a code is the only kind
 * that can have a death case at all (bd a-guard-that-reads-its-own-table-
 * has-no-death-case). Not a public API.
 */
#ifndef CQOPS_KERNELS_FMISC_INT_H
#define CQOPS_KERNELS_FMISC_INT_H

#include "kernels/fcmp.h"
#include "kernels/fmisc.h"

/* The longest program. Pinned so a table that outgrows it fails loudly
 * rather than overrunning the offset array. */
enum { CQ_FX_MAX_ROWS = 17 };

/* Slots and bits of ONE row, every term asked of its owning module at width
 * 64. An unknown op is a hard error in both configurations. */
int      cq_fx_row_steps (const cq_fx_row *r);
uint32_t cq_fx_row_region(const cq_fx_row *r);

/* M36's `soft_fcmp_olt` program (fcmp.jl:8-47), into a CQ_FCMP_MAX_ROWS
 * buffer; returns its row count. Rebuilt per call — no cache. */
int cq_fx_olt(cq_fcmp_row *rows);

/* The block's premises — a program, a region it fits, two operands. Hard
 * errors in both configurations. */
void cq_fx_arm(const cq_fmisc_block *k);

/* The prefix walk: `off[i]` is row i's ABSOLUTE base. The one place `k->off`
 * is applied. */
void cq_fx_offsets(const cq_fmisc_block *k, uint32_t *off);

/* A RAIL operand — `CQ_FX_A` or `CQ_FX_B` and nothing else. */
const cq_bit *cq_fx_rail(const cq_fmisc_block *k, int s);

/* Row `i`'s ONE-BIT wire. `i` must be a row of the program AND a one-bit row;
 * a negative code or a MUX row is a hard error. */
const cq_bit *cq_fx_flag_of(const cq_fmisc_block *k, const uint32_t *off, int i);

/* A 64-LANE operand: a rail, a view (into `buf`), a constant span (into
 * `buf`), a MUX row's `r`, or a one-bit row zero-extended (into `buf`). An
 * unknown negative code or a row outside the program is a hard error. */
const cq_bit *cq_fx_op64(const cq_fmisc_block *k, const uint32_t *off, int s,
                         cq_bit *buf);

#endif /* CQOPS_KERNELS_FMISC_INT_H */
