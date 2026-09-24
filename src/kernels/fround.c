/* src/kernels/fround.c — M38b, K25, the PROGRAM ACCESSORS. The five tables
 * themselves are src/kernels/fround_rows.inc (PROGRAM TEXT); this file is the
 * surface over them and nothing else. Nothing here emits a gate, names a
 * scratch span or knows what a block costs: that is fround_step.c,
 * fround_operand.c, fround_fadd.c and fround_emit.c, on the seams fround.h
 * records.
 */

#include "kernels/fround.h"

#include "kernels/kernel.h"

#include "fround_rows.inc"

enum {
    N_TRUNC = (int)(sizeof TRUNC / sizeof TRUNC[0]),
    N_FLOOR = (int)(sizeof FLOOR / sizeof FLOOR[0]),
    N_CEIL  = (int)(sizeof CEIL  / sizeof CEIL[0]),
    N_EVEN  = (int)(sizeof EVEN  / sizeof EVEN[0]),
    N_AWAY  = (int)(sizeof AWAY  / sizeof AWAY[0])
};

/* THE ENUMERATOR ORDER IS THE TABLE ORDER. A row added to a table and not to
 * its enumerators (or the reverse) must break the BUILD rather than shift
 * every later operand reference by one — an omitted operator occurrence fails
 * by NOT EXISTING, and no gate count, palindrome or pool check can see it. */
_Static_assert(N_TRUNC == T_N && T_N == CQ_FROUND_TRUNC_ROWS,
               "soft_trunc (fround.jl:16-50) is 27 operator occurrences");
_Static_assert(N_FLOOR == F_N && F_N == 33,
               "soft_floor (fround.jl:58-75) is 6 own rows + the 27-row body");
_Static_assert(N_CEIL == C_N && C_N == 34,
               "soft_ceil (fround.jl:83-95) is 7 own rows + the 27-row body");
_Static_assert(N_EVEN == E_N && E_N == 72,
               "soft_round (fround.jl:141-202) is 72 operator occurrences");
_Static_assert(N_AWAY == W_N && W_N == 52,
               "soft_round_away (fround.jl:238-295) is 52 operator occurrences");
_Static_assert(E_N <= CQ_FROUND_MAX_ROWS && C_N <= CQ_FROUND_MAX_ROWS,
               "fround_step.c's memoised prefix map is CQ_FROUND_MAX_ROWS + 1 "
               "entries; a table that outgrows it must fail to build");

const cq_fround_row *cq_fround_rows(cq_fround_prog p, int *n)
{
    const cq_fround_row *r = NULL;
    int k = 0;

    switch (p) {
    case CQ_FROUND_TRUNC: r = TRUNC; k = N_TRUNC; break;
    case CQ_FROUND_FLOOR: r = FLOOR; k = N_FLOOR; break;
    case CQ_FROUND_CEIL:  r = CEIL;  k = N_CEIL;  break;
    case CQ_FROUND_EVEN:  r = EVEN;  k = N_EVEN;  break;
    case CQ_FROUND_AWAY:  r = AWAY;  k = N_AWAY;  break;
    case CQ_FROUND_N_PROG:
    default:
        cq_kernel_die("fround: a program id outside the five");
    }
    if (n != NULL) *n = k;
    return r;
}

int cq_fround_trunc_base(cq_fround_prog p)
{
    switch (p) {
    case CQ_FROUND_TRUNC: return 0;
    case CQ_FROUND_FLOOR: return F_TRUNC;
    case CQ_FROUND_CEIL:  return C_TRUNC;
    case CQ_FROUND_EVEN: case CQ_FROUND_AWAY: return -1;
    case CQ_FROUND_N_PROG:
    default: break;
    }
    cq_kernel_die("fround: a program id outside the five");
    return -1;
}

/* A `switch` WITH EVERY CASE NAMED AND A DYING `default:` (M36's finding 4):
 * using `default:` for the last real case makes an added op silently take that
 * arm. */
int cq_fround_arity(int op)
{
    switch (op) {
    case CQ_FIOP_VIEW: case CQ_FIOP_NOT1:
        return 1;
    case CQ_FIOP_EQ: case CQ_FIOP_SLT: case CQ_FIOP_SUB: case CQ_FIOP_ADD:
    case CQ_FIOP_OR: case CQ_FIOP_AND: case CQ_FIOP_XOR:
    case CQ_FIOP_BSHL: case CQ_FIOP_BLSHR:
    case CQ_FIOP_AND1: case CQ_FIOP_OR1: case CQ_FIOP_FADD:
        return 2;
    case CQ_FIOP_MUX:
        return 3;
    case CQ_FIOP_N_OP:
    default:
        break;
    }
    cq_kernel_die("fround: arity of an unknown row op");
    return 0;
}

/* 64 or 1. It lives HERE because which values are `Bool` is read off the
 * Julia, and the step machine's contract is that it knows none. */
int cq_fround_row_width(const cq_fround_row *rows, int n, int i)
{
    if (rows == NULL || i < 0 || i >= n)
        cq_kernel_die("fround: a row index outside the program");

    switch (rows[i].op) {
    case CQ_FIOP_EQ: case CQ_FIOP_SLT:
    case CQ_FIOP_NOT1: case CQ_FIOP_AND1: case CQ_FIOP_OR1:
        return 1;
    case CQ_FIOP_VIEW: case CQ_FIOP_SUB: case CQ_FIOP_ADD: case CQ_FIOP_MUX:
    case CQ_FIOP_OR: case CQ_FIOP_AND: case CQ_FIOP_XOR:
    case CQ_FIOP_BSHL: case CQ_FIOP_BLSHR: case CQ_FIOP_FADD:
        return CQ_FP64_W;
    case CQ_FIOP_N_OP:
    default:
        break;
    }
    cq_kernel_die("fround: the width of an unknown row op");
    return 0;
}
