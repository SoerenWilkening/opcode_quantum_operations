/* src/kernels/fma.c — M39, K20, the PROGRAM's ACCESSORS.
 *
 * Read docs/constructions/K20.md and fma.h before changing anything here. The
 * row table itself is next door in fma_rows.inc, on the fifth seam fma.h
 * records — PROGRAM TEXT <-> PROGRAM ACCESSORS — and is included exactly once,
 * here. What is left in this file is the four functions that know Julia and no
 * layout: the table's two accessors, the arity, the row widths and PRD-v2 §5's
 * seam row.
 *
 * `cq_fma_row_width` IS HERE AND NOT IN THE STEP MACHINE, and the reason is
 * the seam rather than the line count it also fixes. Which hand-off returns a
 * TUPLE and which of its members are `Bool` is read off `softfloat_common.jl`
 * (:104, :190, :226) — it is Julia knowledge, and fma_step.c's contract is
 * that not one line of it has any. M34 records the same split.
 */

#include "kernels/fma.h"

#include "kernels/fpclass.h"
#include "kernels/kernel.h"

#include "kernels/fma_rows.inc"

_Static_assert(sizeof PROGRAM / sizeof PROGRAM[0] == (size_t)FA_N_ROWS,
               "the enumerator order IS the table order; a row added to one "
               "and not the other must break the build, not shift 900 "
               "operand references by one");

_Static_assert(FA_N_ROWS <= CQ_FMA_MAX_ROWS,
               "fma_step.c's prefix-offset array is CQ_FMA_MAX_ROWS entries; "
               "a table that outgrows it must fail to build rather than "
               "overrun a stack array");

const cq_fma_row *cq_fma_rows(int *n)
{
    if (n != NULL) *n = FA_N_ROWS;
    return PROGRAM;
}

int cq_fma_n_rows(void) { return FA_N_ROWS; }

/* 64, 1, or 0 for a hand-off whose value is a TUPLE. A `CQ_FUOP_OUT` row's
 * width is its PRODUCER's, at that output index — softfloat_common.jl:190 and
 * :226 return two Bools apiece and the rest 64-lane words. */
int cq_fma_row_width(const cq_fma_row *rows, int n, int i)
{
    const cq_fma_row *r;

    if (rows == NULL || i < 0 || i >= n)
        cq_kernel_die("fma: a row index outside the program");
    r = &rows[i];

    switch (r->op) {
    case CQ_FUOP_EQ: case CQ_FUOP_ULT: case CQ_FUOP_SLT: case CQ_FUOP_CLASS:
    case CQ_FUOP_NOT1: case CQ_FUOP_AND1: case CQ_FUOP_OR1:
        return 1;
    case CQ_FUOP_NORM52: case CQ_FUOP_SUBNORM: case CQ_FUOP_ROUND:
        return 0;
    case CQ_FUOP_OUT: {
        int p = r->s0, w = r->s1;

        if (p < 0 || p >= i)
            cq_kernel_die("fma: a projection of a row that is not earlier");
        if (rows[p].op == CQ_FUOP_SUBNORM)
            return (w == CQ_FU_OUT_SUBNORMAL || w == CQ_FU_OUT_FTZ)
                 ? 1 : CQ_FP64_W;
        if (rows[p].op == CQ_FUOP_ROUND)
            return (w == CQ_FU_OUT_EXPOVF || w == CQ_FU_OUT_EXPOVF_AFT)
                 ? 1 : CQ_FP64_W;
        if (rows[p].op != CQ_FUOP_NORM52)
            cq_kernel_die("fma: a projection of a row that returns no tuple");
        return CQ_FP64_W; }
    /* THE 64-LANE OPS, NAMED. An earlier draft let `default:` fall through to
     * `return CQ_FP64_W`, which is M36's finding 4 in its quietest form: a
     * twenty-second op would have been silently 64 lanes wide, and 64 is the
     * answer that makes `cq_fu_val64` accept it and `cq_fu_flag_of` reject it
     * — a wrong operand rather than a refusal. Every sibling switch in this
     * module (`cq_fma_arity`) and next door (`cq_fu_row_steps`,
     * `cq_fu_row_region`) already dies on its `default:`; this one now does
     * too, so the three agree. */
    case CQ_FUOP_VIEW:
    case CQ_FUOP_ADD: case CQ_FUOP_SUB: case CQ_FUOP_MUX:
    case CQ_FUOP_AND: case CQ_FUOP_OR:  case CQ_FUOP_XOR:
    case CQ_FUOP_MUL: case CQ_FUOP_BSHL: case CQ_FUOP_BLSHR:
        return CQ_FP64_W;
    case CQ_FUOP_N_OP:
    default:
        break;
    }
    cq_kernel_die("fma: the width of an unknown row op");
    return 0;
}

/* PRD-v2 §5's `fma.jl:115`/`:118` seam, as a row index rather than a file
 * split. `FA_AD_LO` is `_add128`'s first row — the first operator occurrence
 * of the single-rounding path. */
int cq_fma_seam_row(void) { return FA_AD_LO; }

/* A `switch` WITH EVERY CASE NAMED AND A DYING `default:`, because using
 * `default:` for the last real case makes an added op silently take that arm
 * (M36's finding 4). */
int cq_fma_arity(int op)
{
    switch (op) {
    case CQ_FUOP_VIEW: case CQ_FUOP_NOT1:
        return 1;
    case CQ_FUOP_OUT:                        /* s0 is a row, s1 an INDEX   */
    case CQ_FUOP_CLASS:                      /* s0 is 64 lanes, s1 a CLASS */
        return 1;
    case CQ_FUOP_EQ: case CQ_FUOP_ULT: case CQ_FUOP_SLT:
    case CQ_FUOP_ADD: case CQ_FUOP_SUB:
    case CQ_FUOP_AND: case CQ_FUOP_OR: case CQ_FUOP_XOR:
    case CQ_FUOP_MUL: case CQ_FUOP_BSHL: case CQ_FUOP_BLSHR:
    case CQ_FUOP_NORM52:
    case CQ_FUOP_AND1: case CQ_FUOP_OR1:
        return 2;
    case CQ_FUOP_MUX: case CQ_FUOP_SUBNORM: case CQ_FUOP_ROUND:
        return 3;
    case CQ_FUOP_N_OP:
    default:
        break;
    }
    cq_kernel_die("fma: arity of an unknown row op");
    return 0;
}
