/* src/kernels/fmisc_rows.c — M38, K24, the ROW TABLES of `soft_fmin` and
 * `soft_fmax` (third_party/bennett/src/softfloat/fmin.jl:86-106, :114-133).
 *
 * Read docs/constructions/K24.md §2.2 and fmisc.h before changing anything
 * here. THE TWO TABLES ARE THE PORT: one row per operator occurrence in SOURCE
 * ORDER (PRD-v2 §7.2's literal grain), every row citing its line. Nothing
 * here emits a gate or knows what a block costs — that is fmisc_step.c, on
 * the ROW TABLES <-> STEP MACHINE seam (K24.md §3).
 *
 * WHAT IS NOT A ROW, AND WHY. `abs_a`, `abs_b` (:87-88), `ea`, `eb`, `fa`,
 * `fb` (:89-92) and the mask at :98 are `&`s and `>>`s by COMPILE-TIME
 * CONSTANTS, which PRD-v2 §7.3 as amended makes WIRING — views, zero gates,
 * zero scratch. `ea`..`fb` are not even named here: each `(e == 0x7FF) &
 * (f != 0)` occurrence (:93, :94) is M31's CQ_FP_IS_NAN block, which builds
 * its own views, exactly as M36 composes it (K18 D-K18-8).
 *
 * `soft_fcmp_olt(a, b) != UInt64(0)` (:100) IS TWO OCCURRENCES — the call and
 * the `!=` — and the port keeps both: M36's 24-row program yields a one-bit
 * flag, which is zero-extended to the Julia type's width and compared against
 * the constant 0 by M16's `eq` block, whose raw wire already IS `!=`.
 *
 * `fmax` DIFFERS IN EXACTLY TWO ROWS, BOTH BY OPERAND ORDER: row 9 is
 * `olt(b, a)` (:128) and row 12 is `ifelse(a_neg, b, a)` (:130). Every count,
 * palindrome, pool check and scan is blind to both; the anchors of K24.md §6
 * and tests/test_kernel_fmisc_rows.inc's literal table are the instruments.
 */

#include "kernels/fmisc_int.h"

#include "kernels/kernel.h"

/* soft_fmin — fmin.jl:86-106. */
static const cq_fx_row R_FMIN[] = {
    { CQ_FXOP_CLASS_NAN, CQ_FX_A,     0,            0  }, /*  0 :93 a_nan       */
    { CQ_FXOP_CLASS_NAN, CQ_FX_B,     0,            0  }, /*  1 :94 b_nan       */
    { CQ_FXOP_AND1,      0,           1,            0  }, /*  2 :95 both_nan    */
    { CQ_FXOP_EQ,        CQ_FX_ABS_A, CQ_FX_K_ZERO, 0  }, /*  3 :97 abs_a != 0  */
    { CQ_FXOP_NOT1,      3,           0,            0  }, /*  4 :97 abs_a == 0  */
    { CQ_FXOP_EQ,        CQ_FX_ABS_B, CQ_FX_K_ZERO, 0  }, /*  5 :97 abs_b != 0  */
    { CQ_FXOP_NOT1,      5,           0,            0  }, /*  6 :97 abs_b == 0  */
    { CQ_FXOP_AND1,      4,           6,            0  }, /*  7 :97 both_zero   */
    { CQ_FXOP_EQ,        CQ_FX_SGN_A, CQ_FX_K_ZERO, 0  }, /*  8 :98 a_neg (!=)  */
    { CQ_FXOP_FCMP_OLT,  CQ_FX_A,     CQ_FX_B,      0  }, /*  9 :100 olt(a, b)  */
    { CQ_FXOP_EQ,        9,           CQ_FX_K_ZERO, 0  }, /* 10 :100 != 0       */
    { CQ_FXOP_MUX,       10,          CQ_FX_A,      CQ_FX_B }, /* 11 :101 base  */
    { CQ_FXOP_MUX,       8,           CQ_FX_A,      CQ_FX_B }, /* 12 :102 inner */
    { CQ_FXOP_MUX,       7,           12,           11 }, /* 13 :102 base       */
    { CQ_FXOP_MUX,       1,           CQ_FX_A,      13 }, /* 14 :104 inner      */
    { CQ_FXOP_MUX,       0,           CQ_FX_B,      14 }, /* 15 :104 res        */
    { CQ_FXOP_MUX,       2,           CQ_FX_K_QNAN, 15 }  /* 16 :105 canon NaN  */
};

/* soft_fmax — fmin.jl:114-133. Rows 9 and 12 are the only differences. */
static const cq_fx_row R_FMAX[] = {
    { CQ_FXOP_CLASS_NAN, CQ_FX_A,     0,            0  }, /*  0 :121 a_nan      */
    { CQ_FXOP_CLASS_NAN, CQ_FX_B,     0,            0  }, /*  1 :122 b_nan      */
    { CQ_FXOP_AND1,      0,           1,            0  }, /*  2 :123 both_nan   */
    { CQ_FXOP_EQ,        CQ_FX_ABS_A, CQ_FX_K_ZERO, 0  }, /*  3 :125            */
    { CQ_FXOP_NOT1,      3,           0,            0  }, /*  4 :125            */
    { CQ_FXOP_EQ,        CQ_FX_ABS_B, CQ_FX_K_ZERO, 0  }, /*  5 :125            */
    { CQ_FXOP_NOT1,      5,           0,            0  }, /*  6 :125            */
    { CQ_FXOP_AND1,      4,           6,            0  }, /*  7 :125 both_zero  */
    { CQ_FXOP_EQ,        CQ_FX_SGN_A, CQ_FX_K_ZERO, 0  }, /*  8 :126 a_neg      */
    { CQ_FXOP_FCMP_OLT,  CQ_FX_B,     CQ_FX_A,      0  }, /*  9 :128 olt(b, a)  */
    { CQ_FXOP_EQ,        9,           CQ_FX_K_ZERO, 0  }, /* 10 :128 != 0       */
    { CQ_FXOP_MUX,       10,          CQ_FX_A,      CQ_FX_B }, /* 11 :129 base  */
    { CQ_FXOP_MUX,       8,           CQ_FX_B,      CQ_FX_A }, /* 12 :130 inner */
    { CQ_FXOP_MUX,       7,           12,           11 }, /* 13 :130 base       */
    { CQ_FXOP_MUX,       1,           CQ_FX_A,      13 }, /* 14 :131 inner      */
    { CQ_FXOP_MUX,       0,           CQ_FX_B,      14 }, /* 15 :131 res        */
    { CQ_FXOP_MUX,       2,           CQ_FX_K_QNAN, 15 }  /* 16 :132 canon NaN  */
};

enum {
    N_FMIN = (int)(sizeof R_FMIN / sizeof R_FMIN[0]),
    N_FMAX = (int)(sizeof R_FMAX / sizeof R_FMAX[0])
};

/* A row lost to an editing accident must break the BUILD — an omitted
 * operator occurrence fails by not existing. */
_Static_assert(N_FMIN == 17 && N_FMAX == 17 && N_FMIN <= CQ_FX_MAX_ROWS,
               "fmin.jl:86-106 and :114-133 are 17 operator occurrences each");

const cq_fx_row *cq_fmisc_rows(cq_fmisc_prog p, int *n)
{
    switch (p) {
    case CQ_FMISC_MIN: if (n) *n = N_FMIN; return R_FMIN;
    case CQ_FMISC_MAX: if (n) *n = N_FMAX; return R_FMAX;
    case CQ_FMISC_N_PROG: break;
    }
    cq_kernel_die("fmisc: a program outside {MIN, MAX}");
    return NULL;
}

int cq_fmisc_arity(int op)
{
    switch (op) {
    case CQ_FXOP_CLASS_NAN: case CQ_FXOP_NOT1:                     return 1;
    case CQ_FXOP_EQ: case CQ_FXOP_AND1: case CQ_FXOP_FCMP_OLT:     return 2;
    case CQ_FXOP_MUX:                                              return 3;
    default: break;
    }
    cq_kernel_die("fmisc: arity of an unknown op");
    return 0;
}

int cq_fmisc_row_width(cq_fmisc_prog p, int i)
{
    int n = 0;
    const cq_fx_row *rows = cq_fmisc_rows(p, &n);

    if (i < 0 || i >= n)
        cq_kernel_die("fmisc: a row index outside the program");
    (void)cq_fmisc_arity(rows[i].op);          /* refuses an unknown op */
    return rows[i].op == CQ_FXOP_MUX ? CQ_FP64_W : 1;
}
