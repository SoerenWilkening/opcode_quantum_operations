/* src/kernels/fcmp.c — M36, K18, the ORDERED CORE half of PRD-v2 §5's seam.
 *
 * Read docs/constructions/K18.md and fcmp.h before changing anything here.
 * THE THREE ROW TABLES BELOW ARE THE WHOLE OF THIS FILE AND THEY ARE THE PORT
 * — `soft_fcmp_olt` (fcmp.jl:8-47), `soft_fcmp_oeq` (:57-82) and
 * `_either_nan` (:117-125), one row per operator occurrence in SOURCE ORDER,
 * with every row citing the line it transcribes. Nothing here emits a gate,
 * names a scratch span or knows what a block costs: that is fcmp_step.c, on
 * the ROW TABLES <-> STEP MACHINE seam (K18.md §2.0 D-K18-7). The ten one-line
 * compositions, the fourteen-row dispatch and the classical row are in
 * fcmp_pred.c, on PRD-v2 §5's own recorded seam.
 *
 * THE LITERAL GRAIN IS WHAT A READER CHECKS HERE (PRD-v2 §7.2). One block per
 * operator occurrence as the source spells it; no common-subexpression sharing
 * — `ea == 0x7FF` is computed as many times as fcmp.jl writes it, and every
 * body builds its own `a_nan`, `b_nan`, `abs_a` and `abs_b`; no narrowing
 * towards the field width, so `sa > sb` is a full 64-bit `ult` block although
 * a single Toffoli computes it. All three are refused deliberately: D9's K12
 * precedent refused exactly this class of narrowing because it is a
 * RE-DERIVATION rather than a port, and K11's finding stands that the one
 * mutant L1 cannot see is the one that looks like an optimisation.
 *
 * A ROW REFERENCE IS RELATIVE TO ITS OWN BODY AND IS SHIFTED ON COPY. That is
 * what lets `ule` carry three bodies at three bases in one flat program while
 * each table below stays readable against the Julia it came from.
 */

#include "kernels/fcmp.h"

#include "kernels/kernel.h"

/* --- The three ordered cores. -------------------------------------------- */

/* `_either_nan` — fcmp.jl:117-125. Two class blocks and one `or`: each
 * `(ea == 0x7FF) & (fa != 0)` occurrence IS M31's CQ_FP_IS_NAN row, ported
 * once in fpclass.c and composed here rather than transcribed a second time
 * (Rule 1 applied to the call graph). No CSE across occurrences: `a_nan` and
 * `b_nan` are two independent blocks, and every body below builds its own. */
static const cq_fcmp_row B_NAN[] = {
    { CQ_FCOP_CLASS_NAN, CQ_FC_A, 0, 0 },          /* fcmp.jl:122  a_nan      */
    { CQ_FCOP_CLASS_NAN, CQ_FC_B, 0, 0 },          /* fcmp.jl:123  b_nan      */
    { CQ_FCOP_OR1,       0,       1, 0 }           /* fcmp.jl:124  either_nan */
};

/* `soft_fcmp_oeq` — fcmp.jl:57-82. Rows 0-2 are :68-70's NaN test; 3-7 are
 * :73's `both_zero`; 8-10 are :76's `(a == b) | both_zero`; 11-12 are :79's
 * `& (!either_nan)`. Row 8 is the ONLY block in K18 whose operands are the raw
 * rails, and it is a BITWISE equality: the numeric part of `oeq` is entirely
 * in row 10's disjunction with `both_zero`, which is IEEE's `+0 == -0`. */
static const cq_fcmp_row B_OEQ[] = {
    { CQ_FCOP_CLASS_NAN, CQ_FC_A,      0,            0 },  /* :68            */
    { CQ_FCOP_CLASS_NAN, CQ_FC_B,      0,            0 },  /* :69            */
    { CQ_FCOP_OR1,       0,            1,            0 },  /* :70 either_nan */
    { CQ_FCOP_EQ,        CQ_FC_ABS_A,  CQ_FC_K_ZERO, 0 },  /* :73 abs_a != 0 */
    { CQ_FCOP_NOT1,      3,            0,            0 },  /* :73 abs_a == 0 */
    { CQ_FCOP_EQ,        CQ_FC_ABS_B,  CQ_FC_K_ZERO, 0 },  /* :73 abs_b != 0 */
    { CQ_FCOP_NOT1,      5,            0,            0 },  /* :73 abs_b == 0 */
    { CQ_FCOP_AND1,      4,            6,            0 },  /* :73 both_zero  */
    { CQ_FCOP_EQ,        CQ_FC_A,      CQ_FC_B,      0 },  /* :76 a != b     */
    { CQ_FCOP_NOT1,      8,            0,            0 },  /* :76 a == b     */
    { CQ_FCOP_OR1,       9,            7,            0 },  /* :76 result     */
    { CQ_FCOP_NOT1,      2,            0,            0 },  /* :79 !either_nan*/
    { CQ_FCOP_AND1,      10,           11,           0 }   /* :79 oeq_flag   */
};

/* `soft_fcmp_olt` — fcmp.jl:8-47. THE RAW-WIRE CONVENTION IS D-K18-5 AND IT
 * IS UPSTREAM'S, NOT OURS: `cq_eq_flag` holds `a != b` and `cq_ult_block`'s
 * `carry[W]` holds `a >=u b`, so a `!=` occurrence reads the raw wire and
 * every `==`, `<` and `>` occurrence owes one `lower_not1!` on top (K09.md §5
 * delta 2, ratified as PRD §15 D9(b)).
 *
 * ROW 12 IS WHERE THE SWAP IS EASIEST TO INVERT. `sa > sb` (:36) is TRUE
 * exactly when sa = 1 and sb = 0, which as an `ult` block is `ult(sb, sa)` —
 * operands EXCHANGED — whose raw wire is `sb >=u sa` and whose complement is
 * the answer. Writing `ult(sa, sb)` instead yields the exact NEGATION: every
 * same-sign pair right and every mixed-sign pair backwards, with the identical
 * gate tuple, an intact palindrome and clean scratch. Only an L1 anchor with
 * sa != sb sees it. Rows 8 and 10 carry the same hazard for `<` against `>`. */
static const cq_fcmp_row B_OLT[] = {
    { CQ_FCOP_CLASS_NAN, CQ_FC_A,      0,            0 },  /* :22            */
    { CQ_FCOP_CLASS_NAN, CQ_FC_B,      0,            0 },  /* :23            */
    { CQ_FCOP_OR1,       0,            1,            0 },  /* :24 either_nan */
    { CQ_FCOP_EQ,        CQ_FC_ABS_A,  CQ_FC_K_ZERO, 0 },  /* :27 abs_a != 0 */
    { CQ_FCOP_NOT1,      3,            0,            0 },  /* :27 abs_a == 0 */
    { CQ_FCOP_EQ,        CQ_FC_ABS_B,  CQ_FC_K_ZERO, 0 },  /* :27 abs_b != 0 */
    { CQ_FCOP_NOT1,      5,            0,            0 },  /* :27 abs_b == 0 */
    { CQ_FCOP_AND1,      4,            6,            0 },  /* :27 both_zero  */
    { CQ_FCOP_ULT,       CQ_FC_ABS_A,  CQ_FC_ABS_B,  0 },  /* :32 a >=u b    */
    { CQ_FCOP_NOT1,      8,            0,            0 },  /* :32 pos_lt     */
    { CQ_FCOP_ULT,       CQ_FC_ABS_B,  CQ_FC_ABS_A,  0 },  /* :33 b >=u a    */
    { CQ_FCOP_NOT1,      10,           0,            0 },  /* :33 neg_lt     */
    { CQ_FCOP_ULT,       CQ_FC_SIGN_B, CQ_FC_SIGN_A, 0 },  /* :36 sb >=u sa  */
    { CQ_FCOP_NOT1,      12,           0,            0 },  /* :36 diff_sign  */
    { CQ_FCOP_EQ,        CQ_FC_SIGN_A, CQ_FC_SIGN_B, 0 },  /* :38 sa != sb   */
    { CQ_FCOP_NOT1,      14,           0,            0 },  /* :38 same_sign  */
    { CQ_FCOP_EQ,        CQ_FC_SIGN_A, CQ_FC_K_ZERO, 0 },  /* :40 sa != 0    */
    { CQ_FCOP_NOT1,      16,           0,            0 },  /* :40 sa == 0    */
    { CQ_FCOP_MUX,       17,           9,            11 }, /* :40 inner      */
    { CQ_FCOP_MUX,       15,           18,           13 }, /* :39-41 result  */
    { CQ_FCOP_NOT1,      7,            0,            0 },  /* :44 !both_zero */
    { CQ_FCOP_AND1,      19,           20,           0 },  /* :44            */
    { CQ_FCOP_NOT1,      2,            0,            0 },  /* :44 !either_nan*/
    { CQ_FCOP_AND1,      21,           22,           0 }   /* :44 olt_flag   */
};

enum {
    N_B_NAN = (int)(sizeof B_NAN / sizeof B_NAN[0]),
    N_B_OEQ = (int)(sizeof B_OEQ / sizeof B_OEQ[0]),
    N_B_OLT = (int)(sizeof B_OLT / sizeof B_OLT[0])
};

/* A row lost to an editing accident must break the BUILD rather than quietly
 * shorten a body — an omitted operator occurrence fails by not existing, and
 * no gate count, palindrome or pool check can observe it. */
_Static_assert(N_B_NAN == 3 && N_B_OEQ == 13 && N_B_OLT == 24,
               "fcmp.jl:117-125 is 3 operator occurrences, :57-82 is 13 and "
               ":8-47 is 24; changing one is a re-reading of the source");

static const cq_fcmp_row *body_rows(cq_fcmp_body_id id, int *n)
{
    switch (id) {
    case CQ_FCMP_B_NAN: *n = N_B_NAN; return B_NAN;
    case CQ_FCMP_B_OEQ: *n = N_B_OEQ; return B_OEQ;
    case CQ_FCMP_B_OLT: *n = N_B_OLT; return B_OLT;
    default: break;
    }
    cq_kernel_die("fcmp: body id outside {B_NAN, B_OEQ, B_OLT}");
    return NULL;
}

/* How many of s0/s1/s2 a row reads. Unread slots are never offset and never
 * resolved, which is what lets a 1-operand row leave them zero. */
static int op_arity(int op)
{
    switch (op) {
    case CQ_FCOP_CLASS_NAN: case CQ_FCOP_NOT1:                    return 1;
    case CQ_FCOP_EQ: case CQ_FCOP_ULT: case CQ_FCOP_SUB:
    case CQ_FCOP_AND1: case CQ_FCOP_OR1:                          return 2;
    case CQ_FCOP_MUX:                                             return 3;
    default: break;
    }
    cq_kernel_die("fcmp: unknown op in the program");
    return 0;
}

int cq_fcmp_body(cq_fcmp_body_id id, int base, cq_fcmp_row *out)
{
    int n = 0;
    const cq_fcmp_row *src = body_rows(id, &n);

    if (out == NULL) cq_kernel_die("fcmp: no output buffer for a body");
    if (base < 0 || base + n > CQ_FCMP_MAX_ROWS)
        cq_kernel_die("fcmp: the program outgrew CQ_FCMP_MAX_ROWS");

    for (int i = 0; i < n; i++) {
        /* Copied OUT and back rather than indexed through `&out[i].s0`:
         * walking off one member into the next is undefined even where the
         * layout obliges, and this file compiles under -fsanitize=undefined. */
        short s[3] = { src[i].s0, src[i].s1, src[i].s2 };

        /* An internal reference is an index into the body and is shifted; a
         * negative code names a rail, a view or a constant and is not. */
        for (int j = 0; j < op_arity(src[i].op); j++)
            if (s[j] >= 0) s[j] = (short)(s[j] + base);

        out[i].op = src[i].op;
        out[i].s0 = s[0]; out[i].s1 = s[1]; out[i].s2 = s[2];
    }
    return n;
}

int cq_fcmp_body_flag(cq_fcmp_body_id id, int base)
{
    int n = 0;

    (void)body_rows(id, &n);
    return base + n - 1;               /* every body ends on its own answer */
}
