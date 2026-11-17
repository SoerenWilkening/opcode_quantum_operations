/* Depth-table measurement (kmgy.9), shared by tools/depth_table and tests/test_depth. */
#ifndef CQ_DEPTH_H
#define CQ_DEPTH_H
#include <stdio.h>
#include "harness.h"
#include "kernels.h"

extern uint32_t depth_from;   /* cast ops (CQ_STEP_FROM_*): the operand width of the run in progress, for the reference */
#define DEPTH_MAX_CAND 32
typedef struct { hx_stats st; int ok; } depth_cell;   /* ok: passed the sampled check */

/* Every candidate of op at every width of CQ_WIDTHS, forced-quantum, checked against ref.
 * choice[wi]: least depth, then qubits, then gates, then registration order, among valid
 * candidates. Returns -1 if some width has no valid candidate (its choice is then 0). */
int depth_measure(const cq_kernel_op *op, hx_ref_fn ref, depth_cell cell[][CQ_NWIDTHS], uint8_t *choice);

/* The whole registry: writes kernel_choice.c to cf and DEPTH.md to md, the choices to
 * choices (may be NULL). Deterministic. Returns -1 on any failure. */
int depth_generate(FILE *cf, FILE *md, uint8_t choices[CQ_KOP_COUNT][CQ_NWIDTHS]);

#endif
