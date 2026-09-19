/* src/kernels/fsqrt.c — M40, K21, the ROW TABLE half.
 *
 * Read docs/constructions/K21.md and fsqrt.h before changing anything here.
 * THE TABLE BELOW IS THE WHOLE OF THIS FILE AND IT IS THE PORT — `soft_fsqrt`
 * (third_party/bennett/src/softfloat/fsqrt.jl:31-118) — ONE ROW PER OPERATOR
 * OCCURRENCE in SOURCE ORDER, with every row citing the line it transcribes.
 * Nothing here emits a gate, names a scratch span or knows what a block costs:
 * that is fsqrt_step.c, fsqrt_operand.c and fsqrt_emit.c, on M36's ROW TABLES
 * <-> STEP MACHINE seam and M32's LAYOUT <-> DISPATCH seam.
 *
 * THE DIGIT LOOP IS ONE 16-ROW TEMPLATE EXPANDED 64 TIMES BY THE PREPROCESSOR,
 * AND THE PROGRAM HOLDS 1,024 DISTINCT ROWS. `fsqrt.jl:80` is `for i in 0:63`
 * over the body at :81-89; §7.2's grain is one block per operator OCCURRENCE,
 * and the loop executes each of its nine emitting operators sixty-four times,
 * so the program has sixty-four rows for each of them with its own spans. The
 * macro is how the table is WRITTEN, not a claim that the iterations share
 * anything: `FS_ITER(t)` names every operand by `t`, and the expansion is a
 * `static const` initialiser, so the table is still compile-time constant.
 *
 * WHAT THE LOOP CARRIES, AND THE THREE TRANSPOSITIONS THAT ARE INVISIBLE TO
 * EVERY STRUCTURAL CHECK (K21.md §2.2's box).
 *
 *   (1) ROWS 0 AND 2 READ THE **OLD** `a_hi`. `:81` takes `top2` off `a_hi`
 *       and `:82` then rewrites it, so the new `a_hi` is row 4 and BOTH
 *       `a_hi >> 62` (row 0) and `a_hi << 2` (row 2) view the carried one. A
 *       port that shifted first and took `top2` from the result reads bits
 *       60-61 instead of 62-63: identical tuple, identical palindrome, clean
 *       scratch, wrong root.
 *   (2) `q` IS SHIFTED BY TWO AT ROW 8 AND BY ONE AT ROW 13, IN THE SAME
 *       ITERATION. `:86`'s `(q << 2) | 1` builds the trial subtrahend and
 *       `:89`'s `(q << 1) | fits` accumulates the root. Transposing them costs
 *       nothing and changes everything.
 *   (3) ROW 12's ARMS. `ifelse(fits, r - t, r)` — `diff` is the TRUE arm.
 *       `mux(c,t,f)` and `mux(c,f,t)` emit the identical (X, CX, CCX) tuple at
 *       every width and mask, keep the palindrome and leave scratch clean, so
 *       swapped the loop subtracts exactly when it must not and only L1 sees
 *       it.
 *
 * `r_0` AND `q_0` ARE THE CONSTANT `CQ_FS_K_ZERO`, AND THAT OVERTURNS K21.md
 * §2.1's D9(d) READING — dated 2026-09-19, with the premise measured FALSE.
 * The draft makes `fsqrt.jl:78-79`'s two `UInt64(0)` bindings pre-materialised
 * 64-bit scratch registers, on the ground that as literals *"the decode would
 * need a `t = 0` branch"*. It does not: the decode is a prefix map over 1,070
 * distinct rows and iteration 0 differs from iteration 1 only in which ROW its
 * operands name, which is a table entry and not a branch. What is left of the
 * draft's argument is that iteration 0 emits fewer GATES than iteration 1 —
 * true, deterministic, identical at every operand mask and forward and
 * backward, and already required of the slot scan by `a_lo`'s view chain,
 * which degenerates with `t` whatever `r_0` is. So the two registers would buy
 * a uniformity the instrument does not need, at 128 qubits, by ADDING scratch
 * the source does not have — which is the direction PRD-v2 §7.3 exists to
 * refuse. D9(d) stands where it was decided, on K12's `t = 0` decode branch.
 *
 * `BIAS = Int64(1023)` (:32) IS A CONSTANT BINDING AND NOT A ROW; it is
 * `CQ_FS_K_BIAS`, read twice, at :57 and :60. K16 records the identical shape
 * for `fmul.jl:16` and K18 for `fcmp.jl:9`.
 */

#include "kernels/fsqrt.h"

#include "kernels/fpclass.h"
#include "kernels/kernel.h"

/* One row per occurrence, in the six shapes the ops need. `mask` and `shift`
 * are read only by a VIEW, `shift` alone by an SVIEW; `s1`/`s2` only up to
 * `cq_fsqrt_arity`. */
#define RV(s0, sh, mk)                                                         \
    { (short)CQ_FSOP_VIEW, (short)(s0), 0, 0, (short)(sh), (mk) }
#define RS(s0, sh)                                                             \
    { (short)CQ_FSOP_SVIEW, (short)(s0), 0, 0, (short)(sh), 0 }
#define RO(s0, which)                                                          \
    { (short)CQ_FSOP_OUT, (short)(s0), (short)(which), 0, 0, 0 }
#define R1(op, s0)                                                             \
    { (short)(op), (short)(s0), 0, 0, 0, 0 }
#define R2(op, s0, s1)                                                         \
    { (short)(op), (short)(s0), (short)(s1), 0, 0, 0 }
#define R3(op, s0, s1, s2)                                                     \
    { (short)(op), (short)(s0), (short)(s1), (short)(s2), 0, 0 }

#define ALL ~UINT64_C(0)

/* EVERY ROW REFERENCE IS A NAME AND NEVER A NUMBER, for M34's reason: a
 * 1,070-row table whose operands are integers is three thousand chances at the
 * one mistake PRD-v2 §7.1 says a kernel of this shape actually makes — "the
 * slot arithmetic, not the gates". The enumerator order IS the table order and
 * `FR_N_ROWS` is asserted against the table's length below. */
enum {
    /* Phase A — unpack, fsqrt.jl:34-36. Four views, zero gates.           */
    FR_SA = 0, FR_EA_SH, FR_EA, FR_FA,
    /* Phase B — the special-case predicates, :39-42, three through M31.   */
    FR_ANAN, FR_AINF, FR_AZERO, FR_ANEG,
    /* Phase C — the implicit bit and the effective exponent, :45-46.      */
    FR_EANZ1, FR_FAIMPL, FR_MA, FR_EANZ2, FR_EAEFF,
    /* Phase D — the pre-normalisation, :49. A HAND-OFF to M32.            */
    FR_N52, FR_MA1, FR_EAEFF1,
    /* Phase E — exponent parity and halving, :57-60.                      */
    FR_EUNB, FR_EODDM, FR_EODD, FR_MASHL1, FR_MAADJ, FR_EHALF, FR_REXP,
    /* Phase F — the 112-bit radicand as an (a_hi, a_lo) pair, :68-69.     */
    FR_AHI0, FR_ALOM, FR_ALO0,
    /* ===== PRD-v2 §5's PRE-NORMALISE <-> THE DIGIT LOOP seam, between
     * fsqrt.jl:69 and :80. What crosses it is exactly `a_hi0`, `a_lo0`, the
     * constant zero that is `r`/`q`, and — past the loop — `result_exp`, the
     * four class flags and the rail `a`. Pinned by a case, not by a file. == */
    FR_LOOP,
    FR_TAIL = FR_LOOP + CQ_FSQRT_ITERS * CQ_FSQRT_ITER_ROWS,
    /* Phase H — the sticky bit from a non-zero remainder, :96.            */
    FR_RNZ = FR_TAIL, FR_STICKY, FR_WR,
    /* Phase I — round and pack, :102-103. A HAND-OFF to M32; THREE of the
     * four returned values are discarded and are still emitted (:99-102).  */
    FR_RND, FR_RND_NORMAL, FR_RND_OVF, FR_RND_EXPOVF, FR_RND_EXPOVFA,
    /* Phase J — the special-case select chain, :111-116.                  */
    FR_NAZERO, FR_ANEGNZ, FR_RES0, FR_NANEG, FR_PPINF, FR_RES1, FR_RES2,
    FR_NANAN, FR_PINDEF, FR_RES3, FR_AQ, FR_RESULT,
    FR_N_ROWS
};

/* Row `j` of iteration `t`, and the four values the iteration CARRIES. At
 * `t = 0` the carried `a_hi`/`a_lo` are phase F's two views and the carried
 * `r`/`q` are the constant zero of :78-79 (see this file's header). */
#define FS_L(t, j)  (FR_LOOP + CQ_FSQRT_ITER_ROWS * (t) + (j))
#define FS_AHI(t)   ((t) == 0 ? FR_AHI0 : FS_L((t) - 1, 4))
#define FS_ALO(t)   ((t) == 0 ? FR_ALO0 : FS_L((t) - 1, 5))
#define FS_R(t)     ((t) == 0 ? CQ_FS_K_ZERO : FS_L((t) - 1, 12))
#define FS_Q(t)     ((t) == 0 ? CQ_FS_K_ZERO : FS_L((t) - 1, 15))

/* fsqrt.jl:81-89, ONE ITERATION: 16 rows, 8 emitting and 8 views. */
#define FS_ITER(t)                                                             \
    RV(FS_AHI(t), 62, ALL),                     /* :81 a_hi >> 62         */   \
    RV(FS_L(t, 0), 0, UINT64_C(3)),             /* :81 & 3     -> top2    */   \
    RV(FS_AHI(t), -2, ALL),                     /* :82 a_hi << 2          */   \
    RV(FS_ALO(t), 62, ALL),                     /* :82 a_lo >> 62         */   \
    R2(CQ_FSOP_OR, FS_L(t, 2), FS_L(t, 3)),     /* :82 |       -> a_hi'   */   \
    RV(FS_ALO(t), -2, ALL),                     /* :83 a_lo << 2 -> a_lo' */   \
    RV(FS_R(t), -2, ALL),                       /* :85 r << 2             */   \
    R2(CQ_FSOP_OR, FS_L(t, 6), FS_L(t, 1)),     /* :85 | top2  -> rs      */   \
    RV(FS_Q(t), -2, ALL),                       /* :86 q << 2             */   \
    R2(CQ_FSOP_OR, FS_L(t, 8), CQ_FS_K_ONE),    /* :86 | 1     -> tt      */   \
    R2(CQ_FSOP_ULT, FS_L(t, 7), FS_L(t, 9)),    /* :87 fits = rs >=u tt   */   \
    R2(CQ_FSOP_SUB, FS_L(t, 7), FS_L(t, 9)),    /* :88 rs - tt            */   \
    R3(CQ_FSOP_MUX, FS_L(t, 10), FS_L(t, 11), FS_L(t, 7)), /* :88 -> r'   */   \
    RV(FS_Q(t), -1, ALL),                       /* :89 q << 1             */   \
    R3(CQ_FSOP_MUX, FS_L(t, 10), CQ_FS_K_ONE, CQ_FS_K_ZERO), /* :89 qbit  */   \
    R2(CQ_FSOP_OR, FS_L(t, 13), FS_L(t, 14))    /* :89 |       -> q'      */

/* `soft_fsqrt` — fsqrt.jl:31-118, 1,070 rows: 546 emitting, 523 views (one of
 * them ARITHMETIC) and 6 projections of the two hand-offs' tuples.
 *
 * `!=` IS ONE `eq` ROW AND `>=u` IS ONE `ult` ROW, because the raw wires ARE
 * those values. `lower_eq!` ends `CNOT(or[W-1], r); NOT(r)` with the trailing
 * NOT folded into the copy-out (K09.md §5 delta 2), so `cq_eq_flag` is
 * `a != b`; `cq_ult_block`'s `carry[W]` is `a >=u b`. MEASURED over this file:
 * every comparison `soft_fsqrt` writes is a `!=` or the one `>=` at :87, so
 * K21 owes NO `lower_not1!` from a comparison at all — the three `not1` rows
 * below are the three `!` of :111, :113 and :115. A port that reached for
 * `not1(ult(...))` because Bennett's generic `lower_icmp!` dispatcher does
 * would spend 4W gates and 2W qubits SIXTY-FOUR times to reproduce an IR shape
 * libcqops already declines (PRD §15 D9(b)).
 *
 * AND NO `slt` ANYWHERE. `Int64(ea)` at :46 is where the signedness turns
 * over, but `soft_fsqrt`'s only consumers of a signed value are the `-` at
 * :57, the `&` at :58, the arithmetic `>>` at :60 and the `+` at :60 — not one
 * of them a comparison. Every signed compare K21 reaches is inside
 * `_sf_round_and_pack`, on M32's side of the phase-I hand-off. */
static const cq_fsqrt_row PROGRAM[FR_N_ROWS] = {
/* --- Phase A — unpack. ---------------------------------------------------- */
    RV(CQ_FS_A, 63, ALL),                            /* :34 sa             */
    RV(CQ_FS_A, 52, ALL),                            /* :35 a >> 52        */
    RV(FR_EA_SH, 0, CQ_FP64_EXP_ALL),                /* :35 & 0x7FF -> ea  */
    RV(CQ_FS_A, 0, CQ_FP64_FRAC_MASK),               /* :36 fa             */
/* --- Phase B — the special-case predicates. The first three are `fadd.jl:
 * 29-33` row for row and so are M31's own `is_nan`/`is_inf`/`is_zero`; K21
 * COMPOSES rather than re-transcribes (fsqrt.h's note). `a_neg` is not one of
 * M31's four and is transcribed here, one `eq` row reading the raw `!=`. ---- */
    R2(CQ_FSOP_CLASS, CQ_FS_A, CQ_FP_IS_NAN),        /* :39 a_nan          */
    R2(CQ_FSOP_CLASS, CQ_FS_A, CQ_FP_IS_INF),        /* :40 a_inf          */
    R2(CQ_FSOP_CLASS, CQ_FS_A, CQ_FP_IS_ZERO),       /* :41 a_zero         */
    R2(CQ_FSOP_EQ, FR_SA, CQ_FS_K_ZERO),             /* :42 a_neg = sa!=0  */
/* --- Phase C — the implicit bit and the effective exponent. The `Int64(...)`
 * casts at :46 ride on the mux row: a reinterpret is wiring, which is M34's
 * disposition for `fmul.jl:48-49` and is followed here. ------------------- */
    R2(CQ_FSOP_EQ,  FR_EA, CQ_FS_K_ZERO),            /* :45 ea != 0        */
    R2(CQ_FSOP_OR,  FR_FA, CQ_FS_K_IMPLICIT),        /* :45 fa | IMPLICIT  */
    R3(CQ_FSOP_MUX, FR_EANZ1, FR_FAIMPL, FR_FA),     /* :45 ma             */
    R2(CQ_FSOP_EQ,  FR_EA, CQ_FS_K_ZERO),            /* :46 ea != 0 (2nd)  */
    R3(CQ_FSOP_MUX, FR_EANZ2, FR_EA, CQ_FS_K_ONE),   /* :46 ea_eff         */
/* --- Phase D — pre-normalise so the leading 1 sits at bit 52 before the digit
 * recurrence (:48). `m == 0` returns `(0, e)` unchanged, post-Bennett-tpg0. - */
    R2(CQ_FSOP_NORM52, FR_MA, FR_EAEFF),             /* :49                */
    RO(FR_N52, CQ_FS_OUT_M),                         /* :49 ma             */
    RO(FR_N52, CQ_FS_OUT_E),                         /* :49 ea_eff         */
/* --- Phase E — exponent parity and halving. FR_EHALF is the ARITHMETIC right
 * shift of :60, and :53-56 is upstream's own statement of why it must be one:
 * `e_unb` is negative for every operand below 1.0. ------------------------ */
    R2(CQ_FSOP_SUB, FR_EAEFF1, CQ_FS_K_BIAS),        /* :57 e_unb          */
    RV(FR_EUNB, 0, UINT64_C(1)),                     /* :58 e_unb & 1      */
    R2(CQ_FSOP_EQ, FR_EODDM, CQ_FS_K_ZERO),          /* :58 e_is_odd       */
    RV(FR_MA1, -1, ALL),                             /* :59 ma << 1        */
    R3(CQ_FSOP_MUX, FR_EODD, FR_MASHL1, FR_MA1),     /* :59 ma_adj         */
    RS(FR_EUNB, 1),                                  /* :60 e_unb >>arith 1*/
    R2(CQ_FSOP_ADD, FR_EHALF, CQ_FS_K_BIAS),         /* :60 result_exp     */
/* --- Phase F — the radicand A = ma_adj << 58, split as (a_hi, a_lo). Three
 * views and no gate: `ma_adj` has at most 54 bits, so bits 6..53 land in
 * a_hi[0..47] and bits 0..5 in a_lo[58..63] (:64-67). ---------------------- */
    RV(FR_MAADJ, 6, ALL),                            /* :68 a_hi           */
    RV(FR_MAADJ, 0, UINT64_C(0x3F)),                 /* :69 ma_adj & 0x3F  */
    RV(FR_ALOM, -58, ALL),                           /* :69 << 58 -> a_lo  */
/* --- Phase G — the restoring digit recurrence, :80-90. SIXTY-FOUR distinct
 * iterations of the 16-row template above. -------------------------------- */
    FS_ITER(0),  FS_ITER(1),  FS_ITER(2),  FS_ITER(3),
    FS_ITER(4),  FS_ITER(5),  FS_ITER(6),  FS_ITER(7),
    FS_ITER(8),  FS_ITER(9),  FS_ITER(10), FS_ITER(11),
    FS_ITER(12), FS_ITER(13), FS_ITER(14), FS_ITER(15),
    FS_ITER(16), FS_ITER(17), FS_ITER(18), FS_ITER(19),
    FS_ITER(20), FS_ITER(21), FS_ITER(22), FS_ITER(23),
    FS_ITER(24), FS_ITER(25), FS_ITER(26), FS_ITER(27),
    FS_ITER(28), FS_ITER(29), FS_ITER(30), FS_ITER(31),
    FS_ITER(32), FS_ITER(33), FS_ITER(34), FS_ITER(35),
    FS_ITER(36), FS_ITER(37), FS_ITER(38), FS_ITER(39),
    FS_ITER(40), FS_ITER(41), FS_ITER(42), FS_ITER(43),
    FS_ITER(44), FS_ITER(45), FS_ITER(46), FS_ITER(47),
    FS_ITER(48), FS_ITER(49), FS_ITER(50), FS_ITER(51),
    FS_ITER(52), FS_ITER(53), FS_ITER(54), FS_ITER(55),
    FS_ITER(56), FS_ITER(57), FS_ITER(58), FS_ITER(59),
    FS_ITER(60), FS_ITER(61), FS_ITER(62), FS_ITER(63),
/* --- Phase H — the sticky bit. Kahan's theorem (:11-15, :93-95) is why one
 * OR-ed bit is enough and no Markstein or Tuckerman correction follows. ---- */
    R2(CQ_FSOP_EQ, FS_L(63, 12), CQ_FS_K_ZERO),      /* :96 r != 0         */
    R3(CQ_FSOP_MUX, FR_RNZ, CQ_FS_K_ONE, CQ_FS_K_ZERO), /* :96 sticky      */
    R2(CQ_FSOP_OR, FS_L(63, 15), FR_STICKY),         /* :96 wr = q | ...   */
/* --- Phase I — round and pack. `result_sign` is the LITERAL UInt64(0) at
 * :103, so `result_sign << 63` inside M32 is an all-CQ_BIT_ZERO view and every
 * `or` reading it folds — the one caller of M32 whose gate count differs from
 * its siblings' for a reason neither module names (D-K23-7). ------------- */
    R3(CQ_FSOP_ROUND, FR_WR, FR_REXP, CQ_FS_K_ZERO), /* :103               */
    RO(FR_RND, CQ_FS_OUT_NORMAL),                    /* :102 normal_result */
    RO(FR_RND, CQ_FS_OUT_OVERFLOW),                  /* :102 _overflow DEAD*/
    RO(FR_RND, CQ_FS_OUT_EXPOVF),                    /* :102 _exp_ovf DEAD */
    RO(FR_RND, CQ_FS_OUT_EXPOVF_AFT),                /* :102 _after   DEAD */
/* --- Phase J — the select chain. Priority runs BOTTOM-UP: the last `ifelse`
 * written wins, so the NaN row is outermost. UPSTREAM ANNOTATED THE ORDER AND
 * IT IS LOAD-BEARING (:107-110): a NEGATIVE NaN satisfies `a_neg_nonzero`, so
 * running the NaN row before the INDEF row returns INDEF and loses the
 * payload. And `sqrt(-0) = -0` by TWO independent mechanisms — the `a_zero`
 * row selects the input, and `!a_zero` excludes it from `a_neg_nonzero` — so
 * a port keeping only one of the two is still correct, which is exactly why a
 * port keeping NEITHER can look reviewed. ---------------------------------- */
    R1(CQ_FSOP_NOT1, FR_AZERO),                      /* :111 !a_zero       */
    R2(CQ_FSOP_AND1, FR_ANEG, FR_NAZERO),            /* :111 a_neg_nonzero */
    RV(FR_RND_NORMAL, 0, ALL),                       /* :112 result        */
    R1(CQ_FSOP_NOT1, FR_ANEG),                       /* :113 !a_neg        */
    R2(CQ_FSOP_AND1, FR_AINF, FR_NANEG),             /* :113 a_inf & ...   */
    R3(CQ_FSOP_MUX, FR_PPINF, CQ_FS_K_INF, FR_RES0), /* :113 +Inf -> +Inf  */
    R3(CQ_FSOP_MUX, FR_AZERO, CQ_FS_A, FR_RES1),     /* :114 +-0 -> +-0    */
    R1(CQ_FSOP_NOT1, FR_ANAN),                       /* :115 !a_nan        */
    R2(CQ_FSOP_AND1, FR_ANEGNZ, FR_NANAN),           /* :115 & ...         */
    R3(CQ_FSOP_MUX, FR_PINDEF, CQ_FS_K_INDEF, FR_RES2), /* :115 -> INDEF   */
    R2(CQ_FSOP_OR, CQ_FS_A, CQ_FS_K_QUIET),          /* :116 a | QUIET_BIT */
    R3(CQ_FSOP_MUX, FR_ANAN, FR_AQ, FR_RES3)         /* :116 result        */
};

_Static_assert(sizeof PROGRAM / sizeof PROGRAM[0] == (size_t)FR_N_ROWS,
               "the enumerator order IS the table order; a row added to one "
               "and not the other must break the build, not shift three "
               "thousand operand references by one");

_Static_assert(FR_N_ROWS <= CQ_FSQRT_MAX_ROWS,
               "fsqrt_step.c's memoised prefix map is CQ_FSQRT_MAX_ROWS + 1 "
               "entries; a table that outgrows it must fail to build rather "
               "than overrun it");

_Static_assert(FR_TAIL - FR_LOOP == CQ_FSQRT_ITERS * CQ_FSQRT_ITER_ROWS,
               "fsqrt.jl:80 is `for i in 0:63` over a 16-row body; an "
               "iteration count or a body length that drifts must break a "
               "build");

const cq_fsqrt_row *cq_fsqrt_rows(int *n)
{
    if (n != NULL) *n = FR_N_ROWS;
    return PROGRAM;
}

int cq_fsqrt_n_rows(void) { return FR_N_ROWS; }

int cq_fsqrt_seam_row(void) { return FR_LOOP; }

int cq_fsqrt_loop_row(int t, int j)
{
    if (t < 0 || t >= CQ_FSQRT_ITERS || j < 0 || j >= CQ_FSQRT_ITER_ROWS)
        cq_kernel_die("fsqrt: a loop row outside the 64 x 16 digit recurrence");
    return FS_L(t, j);
}

/* 64, 1, or 0 for a hand-off whose value is a TUPLE. It lives HERE and not in
 * the step machine because it is a fact about the PROGRAM rather than about
 * the layout: `softfloat_common.jl:104` returns two values and `:226` four,
 * and which of them are `Bool` is read off the Julia. The step machine's
 * contract is that not one line of it knows any Julia. */
int cq_fsqrt_row_width(const cq_fsqrt_row *rows, int n, int i)
{
    const cq_fsqrt_row *r;

    if (rows == NULL || i < 0 || i >= n)
        cq_kernel_die("fsqrt: a row index outside the program");
    r = &rows[i];

    switch (r->op) {
    case CQ_FSOP_EQ: case CQ_FSOP_ULT: case CQ_FSOP_CLASS:
    case CQ_FSOP_NOT1: case CQ_FSOP_AND1:
        return 1;
    case CQ_FSOP_NORM52: case CQ_FSOP_ROUND:
        return 0;
    case CQ_FSOP_OUT: {
        int p = r->s0, w = r->s1;

        if (p < 0 || p >= i)
            cq_kernel_die("fsqrt: a projection of a row that is not earlier");
        if (rows[p].op == CQ_FSOP_ROUND)
            return (w == CQ_FS_OUT_EXPOVF || w == CQ_FS_OUT_EXPOVF_AFT)
                 ? 1 : CQ_FP64_W;
        if (rows[p].op != CQ_FSOP_NORM52)
            cq_kernel_die("fsqrt: a projection of a row that returns no tuple");
        return CQ_FP64_W; }
    default: break;
    }
    return CQ_FP64_W;
}

/* A `switch` WITH EVERY CASE NAMED AND A DYING `default:`, because using
 * `default:` for the last real case makes an added op silently take that arm
 * (M36's finding 4). */
int cq_fsqrt_arity(int op)
{
    switch (op) {
    case CQ_FSOP_VIEW: case CQ_FSOP_SVIEW: case CQ_FSOP_NOT1:
        return 1;
    case CQ_FSOP_OUT:                        /* s0 is a row, s1 an INDEX   */
    case CQ_FSOP_CLASS:                      /* s0 is 64 lanes, s1 a CLASS */
        return 1;
    case CQ_FSOP_EQ: case CQ_FSOP_ULT: case CQ_FSOP_ADD: case CQ_FSOP_SUB:
    case CQ_FSOP_OR: case CQ_FSOP_NORM52: case CQ_FSOP_AND1:
        return 2;
    case CQ_FSOP_MUX: case CQ_FSOP_ROUND:
        return 3;
    case CQ_FSOP_N_OP:
    default:
        break;
    }
    cq_kernel_die("fsqrt: arity of an unknown row op");
    return 0;
}
