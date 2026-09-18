/* src/kernels/fpround.c — M32, K23, the ROW TABLES half.
 *
 * Read docs/constructions/K23.md and fpround.h before changing anything here.
 * THE FOUR ROW TABLES BELOW ARE THE WHOLE OF THIS FILE AND THEY ARE THE PORT —
 * `_sf_normalize_to_bit52` (third_party/bennett/src/softfloat/
 * softfloat_common.jl:68-105), `_sf_normalize_clz` (:113-139),
 * `_sf_handle_subnormal` (:174-191) and `_sf_round_and_pack` (:198-227), one
 * row per OPERATOR OCCURRENCE in SOURCE ORDER, with every row citing the line
 * it transcribes. Nothing here emits a gate, names a scratch span or knows
 * what a block costs: that is fpround_step.c, on M36's ROW TABLES <-> STEP
 * MACHINE seam (K18.md D-K18-7, K23 §5.8 / D-K23-10).
 *
 * THE LITERAL GRAIN IS WHAT A READER CHECKS HERE (PRD-v2 §7.2). One block per
 * operator occurrence as the source spells it; NO common-subexpression
 * sharing — `result_sign << 63` gets three view rows because the source writes
 * it three times (:183, :201, :224), and the clamp at :178 and the clamp at
 * :223 are two four-row instances and not one; NO narrowing towards the field
 * width, so `exp_pack` is a 64-lane span holding eleven bits of value and
 * `grs` is a 64-lane span holding three. All three are refused deliberately:
 * D9's K12 precedent refused exactly this class of narrowing because it is a
 * RE-DERIVATION rather than a port, and K11's finding stands that the one
 * mutant L1 cannot see is the one that looks like an optimisation.
 *
 * A VIEW ROW IS AN OPERATOR OCCURRENCE THAT COSTS NOTHING (PRD-v2 §7.3 as
 * amended; D-K23-3). An AND or a shift by a COMPILE-TIME constant is WIRING —
 * 41 of these 154 rows — and they stay ROWS so the 1:1 correspondence with the
 * source survives, which is the thing a reader checks and the thing the slot
 * scan walks. The one `&` in M32 whose second operand is COMPUTED is
 * `wr & lost_mask_sub` (:181) and it is a real `and`(64) block.
 *
 * THE TWELVE CLZ MASKS ARE CONSTANT-FOLDED AND ARE NOT SHIFT ROWS. Julia's
 * `(UInt64(0xFFFFFFFF) << 21)` has two literal operands, so there is no
 * operator occurrence left to transcribe; they are spelled here exactly as the
 * source spells them rather than as a precomputed hex constant, so a reader
 * checks a mask against :76 and not against arithmetic.
 *
 * THE SIX STAGES OF A CLZ LADDER ARE ONE SEVEN-ROW SHAPE AT SIX MASKS, and
 * `CLZ_STAGE` writes it once. That is legal and is M31's own answer (K23
 * §2.2.2): §7.2's no-CSE rule is about OCCURRENCES, not about C macros, and
 * what is forbidden is sharing a block INSTANCE — each occurrence gets its own
 * extent, which is what fpround_step.c's prefix sum gives it. The two helpers
 * are NOT one parameterised block: they differ in the six masks and in
 * norm52's four guard/restore rows, and they are two tables.
 */

#include "kernels/fpround.h"

#include "kernels/kernel.h"

/* One row per occurrence, in the four shapes the ops need. `mask` and `shift`
 * are read only by a VIEW; `s1`/`s2` only up to `cq_fpround_arity`. */
#define RV(s0, sh, mk)                                                         \
    { (short)CQ_FROP_VIEW, (short)(s0), 0, 0, (short)(sh), (mk) }
#define R1(op, s0)                                                             \
    { (short)(op), (short)(s0), 0, 0, 0, 0 }
#define R2(op, s0, s1)                                                         \
    { (short)(op), (short)(s0), (short)(s1), 0, 0, 0 }
#define R3(op, s0, s1, s2)                                                     \
    { (short)(op), (short)(s0), (short)(s1), (short)(s2), 0, 0 }

#define ALL ~UINT64_C(0)

/* One stage of a six-stage binary-search CLZ — seven occurrences, two of them
 * views. `b` is the stage's first row index, `v` the value input (a row or a
 * code), `x` the exponent input, `mk` the stage's constant mask, `sh` its
 * shift and `kd` its decrement constant. Rows b+4 and b+6 are the stage's two
 * outputs. Written once for softfloat_common.jl:76-78 / :80-82 / :84-86 /
 * :88-90 / :92-94 / :96-98 and for :114-116 / :118-120 / :122-124 / :126-128 /
 * :130-132 / :134-136, which are the same seven lines at two target bits. */
#define CLZ_STAGE(b, v, x, mk, sh, kd)                                         \
    RV((v), 0, (mk)),                                  /* m & (…)           */ \
    R2(CQ_FROP_EQ,   (b) + 0, CQ_FR_K_ZERO),           /* … == 0; raw != 0  */ \
    R1(CQ_FROP_NOT1, (b) + 1),                         /* need_k            */ \
    RV((v), -(sh), ALL),                               /* m << k            */ \
    R3(CQ_FROP_MUX,  (b) + 2, (b) + 3, (v)),           /* ifelse(need, …, m)*/ \
    R2(CQ_FROP_SUB,  (x), (kd)),                       /* e - k             */ \
    R3(CQ_FROP_MUX,  (b) + 2, (b) + 5, (x))            /* ifelse(need, …, e)*/

/* --- `_sf_normalize_to_bit52` — softfloat_common.jl:68-105, 48 rows. ------
 *
 * THE `m == 0` GUARD IS POST-`Bennett-tpg0` AND IS NOT DEAD CODE (:50-63).
 * Pre-tpg0 the function trusted its callers and returned `(0, e - 63)`; it now
 * substitutes IMPLICIT so the ladder is a no-op and restores `(0, e_orig)` at
 * the exit. "Simplifying it away" is re-deriving the pre-tpg0 version, and the
 * callers it breaks are every zero operand — `flog.jl:266` says in its own
 * words that it relies on the guard. The target bit is 52, so the six masks
 * are shifted by 21/37/45/49/51/52 against `_sf_normalize_clz`'s
 * 24/40/48/52/54/55; the docstring states the difference in one line (:65-66)
 * and getting it wrong is a different, plausible, correct-looking circuit. */
static const cq_fpround_row NORM52[] = {
    R2(CQ_FROP_EQ,   CQ_FR_IN0, CQ_FR_K_ZERO),          /* :72  m != 0      */
    R1(CQ_FROP_NOT1, 0),                                /* :72  m_zero      */
    RV(CQ_FR_IN1, 0, ALL),                              /* :73  e_orig = e  */
    R3(CQ_FROP_MUX,  1, CQ_FR_K_IMPLICIT, CQ_FR_IN0),   /* :74              */
    CLZ_STAGE( 4, 3,         CQ_FR_IN1, (UINT64_C(0xFFFFFFFF) << 21), 32, CQ_FR_K_D32),
    CLZ_STAGE(11, 8,         10,        (UINT64_C(0xFFFF)     << 37), 16, CQ_FR_K_D16),
    CLZ_STAGE(18, 15,        17,        (UINT64_C(0xFF)       << 45),  8, CQ_FR_K_D8),
    CLZ_STAGE(25, 22,        24,        (UINT64_C(0xF)        << 49),  4, CQ_FR_K_D4),
    CLZ_STAGE(32, 29,        31,        (UINT64_C(0x3)        << 51),  2, CQ_FR_K_D2),
    CLZ_STAGE(39, 36,        38,        (UINT64_C(1)          << 52),  1, CQ_FR_K_D1),
    R3(CQ_FROP_MUX,  1, CQ_FR_K_ZERO, 43),              /* :102 m_final     */
    R3(CQ_FROP_MUX,  1, 2,            45)               /* :103 e_final     */
};

/* --- `_sf_normalize_clz` — softfloat_common.jl:113-139, 42 rows. ---------- */
static const cq_fpround_row CLZ[] = {
    CLZ_STAGE( 0, CQ_FR_IN0, CQ_FR_IN1, (UINT64_C(0xFFFFFFFF) << 24), 32, CQ_FR_K_D32),
    CLZ_STAGE( 7, 4,         6,         (UINT64_C(0xFFFF)     << 40), 16, CQ_FR_K_D16),
    CLZ_STAGE(14, 11,        13,        (UINT64_C(0xFF)       << 48),  8, CQ_FR_K_D8),
    CLZ_STAGE(21, 18,        20,        (UINT64_C(0xF)        << 52),  4, CQ_FR_K_D4),
    CLZ_STAGE(28, 25,        27,        (UINT64_C(0x3)        << 54),  2, CQ_FR_K_D2),
    CLZ_STAGE(35, 32,        34,        (UINT64_C(1)          << 55),  1, CQ_FR_K_D1)
};

/* --- `_sf_handle_subnormal` — softfloat_common.jl:174-191, 22 rows. -------
 *
 * ROW 19 PRECEDES ROW 20 HERE AND FOLLOWS IT IN THE SOURCE, because :185-187
 * is ONE nested `ifelse` written outside-in and the inner arm must be computed
 * first. That is the evaluation order of the expression, not a reordering of
 * the port, and the slot scan is derived from THIS table rather than from line
 * numbers.
 *
 * FOUR SIGNED COMPARES, ROWS 0, 2, 3 AND 5 — `result_exp` and `shift_sub` are
 * `Int64` and are genuinely negative on the unselected branches, which is what
 * `subnormal` MEANS. `cq_ult_block` here is well formed, identically shaped,
 * palindromic, ancilla-clean and wrong exactly on the underflow path.
 *
 * THE FLUSH BOUNDARY AT ROW 2 IS UPSTREAM'S REFUSED FIX, NOT A BUG (:148-172,
 * `Bennett-xiqt` / U133). Its own review flagged `shift_sub >= 56` as
 * theoretically dropping an RTNE round-up; the disposition is "investigated,
 * doc-only" (:166), pinned upstream by test_xiqt_subnormal_boundary.jl. Rule 1
 * settles what we do: port the line.
 *
 * THE CLAMP AT ROWS 3-8 IS THE ONE CONSTRUCTION IN M32 THAT RULE 1 DOES NOT
 * COVER — `Base.clamp`'s body is not in the snapshot (K23 §1.6, §5.5). The
 * reading is D-K23-5's: `ifelse(x < lo, lo, ifelse(x > hi, hi, x))`, four
 * rows, in this order. Every reading agrees in VALUE wherever `lo <= hi`,
 * which both M32 sites satisfy by construction, so the fork is in the row
 * ORDER and the cost; it is what a slot scan pins, and moving it changes
 * K15.md's phase K too. It is ALSO what keeps the D8 guarantee (PRD-v2 §7.6):
 * mis-order or mis-sign these and rows 11 and 16 see a barrel amount outside
 * [0, 63], where D8's mask-then-saturate diverges from Julia's `x << 64`. */
static const cq_fpround_row SUBNORM[] = {
    R2(CQ_FROP_SLT,  CQ_FR_K_ZERO, CQ_FR_IN1),      /*  0 :175 subnormal     */
    R2(CQ_FROP_SUB,  CQ_FR_K_ONE,  CQ_FR_IN1),      /*  1 :176 shift_sub     */
    R2(CQ_FROP_SLT,  1, CQ_FR_K_56),                /*  2 :177 flush_to_zero */
    R2(CQ_FROP_SLT,  1, CQ_FR_K_ZERO),              /*  3 :178 clamp  >= 0   */
    R1(CQ_FROP_NOT1, 3),                            /*  4 :178 clamp  < 0    */
    R2(CQ_FROP_SLT,  CQ_FR_K_63, 1),                /*  5 :178 clamp <= 63   */
    R1(CQ_FROP_NOT1, 5),                            /*  6 :178 clamp  > 63   */
    R3(CQ_FROP_MUX,  6, CQ_FR_K_63,   1),           /*  7 :178 clamp hi arm  */
    R3(CQ_FROP_MUX,  4, CQ_FR_K_ZERO, 7),           /*  8 :178 shift_clamped */
    R3(CQ_FROP_MUX,  2, CQ_FR_K_ZERO, 8),           /*  9 :179 ifelse(ftz,…) */
    RV(9, 0, ALL),                                  /* 10 :179 shift_u       */
    R2(CQ_FROP_BSHL, CQ_FR_K_ONE, 10),              /* 11 :180 1 << shift_u  */
    R2(CQ_FROP_SUB,  11, CQ_FR_K_ONE),              /* 12 :180 lost_mask_sub */
    R2(CQ_FROP_AND,  CQ_FR_IN0, 12),                /* 13 :181 wr & mask     */
    R2(CQ_FROP_EQ,   13, CQ_FR_K_ZERO),             /* 14 :181 … != 0        */
    R3(CQ_FROP_MUX,  14, CQ_FR_K_ONE, CQ_FR_K_ZERO),/* 15 :181 lost_sub      */
    R2(CQ_FROP_BLSHR, CQ_FR_IN0, 10),               /* 16 :182 wr >> shift_u */
    R2(CQ_FROP_OR,   16, 15),                       /* 17 :182 wr_sub_result */
    RV(CQ_FR_IN2, -63, ALL),                        /* 18 :183 flushed_result*/
    R3(CQ_FROP_MUX,  2, CQ_FR_IN0, 17),             /* 19 :186 inner ifelse  */
    R3(CQ_FROP_MUX,  0, 19, CQ_FR_IN0),             /* 20 :185 wr            */
    R3(CQ_FROP_MUX,  0, CQ_FR_K_ZERO, CQ_FR_IN1)    /* 21 :188 result_exp    */
};

/* --- `_sf_round_and_pack` — softfloat_common.jl:198-227, 42 rows. ---------
 *
 * THE RAW-WIRE CONVENTION IS UPSTREAM'S, NOT OURS (K09.md §5 delta 2, PRD §15
 * D9(b)). `cq_eq_flag` holds `a != b`, `cq_ult_block`'s carry-out holds
 * `a >=u b` and `cq_slt_block`'s holds `a >=s b` — so a `!=` occurrence and a
 * `>=` occurrence read the raw wire, and every `==`, `<` and `>` occurrence
 * owes itself one `lower_not1!` (arith.jl:474-478, a fresh wire: CX then X).
 * Rows 0, 30 read raw; rows 3, 31 are `<` and rows 5, 33 are `>`, so each owes
 * the NOT1 beside it. M31 SHIPPED that spelling and M32 follows it (D-K23-4):
 * two spellings of one polarity in the library is what D9(b) refused.
 *
 * ROW 14 IS THE ONE UNSIGNED COMPARE IN M32 AND IS A RECORDED EQUIVALENT
 * MUTANT. `grs = (guard << 2) | (round_bit << 1) | sticky_bit` (:204-209) is
 * built from three ONE-BIT inputs, so `grs` is in [0, 7] and is never negative
 * as an `Int64`: substituting `slt` for `ult` here agrees in VALUE on every
 * reachable input. It is therefore not a coverage hole and must not be "fixed"
 * by weakening the site to match it. It is not invisible to everything —
 * `cq_slt_steps(64) > cq_ult_steps(64)`, so L4 sees it and L1 does not, the
 * exact inverse of the eight signed rows. Recorded on cq_rotate_rz_bit's
 * precedent (bd equivalent-mutant-record-at-the-site).
 *
 * ROWS 1 AND 38 ARE THE SAME JULIA EXPRESSION WRITTEN TWICE (:201 and :224),
 * and rows 3-9, 10, 11 and 18 are the five masks by a literal `1` or by
 * FRAC_MASK — views under D-K23-3, which K15's and K21's drafts count as
 * `and`(64) blocks and which are worth 320 slots and 320 bits between them. */
static const cq_fpround_row ROUND[] = {
    R2(CQ_FROP_SLT,  CQ_FR_IN1, CQ_FR_K_7FF),       /*  0 :200 exp_overflow  */
    RV(CQ_FR_IN2, -63, ALL),                        /*  1 :201 sign << 63    */
    R2(CQ_FROP_OR,   1, CQ_FR_K_INF),               /*  2 :201 overflow_res  */
    RV(CQ_FR_IN0, 2, ALL),                          /*  3 :204 wr >> 2       */
    RV(3, 0, UINT64_C(1)),                          /*  4 :204 guard         */
    RV(CQ_FR_IN0, 1, ALL),                          /*  5 :205 wr >> 1       */
    RV(5, 0, UINT64_C(1)),                          /*  6 :205 round_bit     */
    RV(CQ_FR_IN0, 0, UINT64_C(1)),                  /*  7 :206 sticky_bit    */
    RV(CQ_FR_IN0, 3, ALL),                          /*  8 :207 wr >> 3       */
    RV(8, 0, CQ_FP64_FRAC_MASK),                    /*  9 :207 frac          */
    RV(4, -2, ALL),                                 /* 10 :209 guard << 2    */
    RV(6, -1, ALL),                                 /* 11 :209 round << 1    */
    R2(CQ_FROP_OR,   10, 11),                       /* 12 :209               */
    R2(CQ_FROP_OR,   12, 7),                        /* 13 :209 grs           */
    R2(CQ_FROP_ULT,  CQ_FR_K_FOUR, 13),             /* 14 :210 4 >=u grs     */
    R1(CQ_FROP_NOT1, 14),                           /* 15 :210 grs > 4       */
    R2(CQ_FROP_EQ,   13, CQ_FR_K_FOUR),             /* 16 :210 grs != 4      */
    R1(CQ_FROP_NOT1, 16),                           /* 17 :210 grs == 4      */
    RV(9, 0, UINT64_C(1)),                          /* 18 :210 frac & 1      */
    R2(CQ_FROP_EQ,   18, CQ_FR_K_ZERO),             /* 19 :210 … != 0        */
    R2(CQ_FROP_AND1, 17, 19),                       /* 20 :210               */
    R2(CQ_FROP_OR1,  15, 20),                       /* 21 :210 round_up      */
    R2(CQ_FROP_ADD,  9, CQ_FR_K_ONE),               /* 22 :212 frac_rounded  */
    R2(CQ_FROP_EQ,   22, CQ_FR_K_IMPLICIT),         /* 23 :213 … != IMPLICIT */
    R1(CQ_FROP_NOT1, 23),                           /* 24 :213 mant_overflow */
    R3(CQ_FROP_MUX,  24, CQ_FR_K_ZERO, 22),         /* 25 :215 inner ifelse  */
    R3(CQ_FROP_MUX,  21, 25, 9),                    /* 26 :214 frac_final    */
    R2(CQ_FROP_AND1, 21, 24),                       /* 27 :217 round&mant_ov */
    R2(CQ_FROP_ADD,  CQ_FR_IN1, CQ_FR_K_ONE),       /* 28 :218 rexp + 1      */
    R3(CQ_FROP_MUX,  27, 28, CQ_FR_IN1),            /* 29 :217 exp_after_rnd */
    R2(CQ_FROP_SLT,  29, CQ_FR_K_7FF),              /* 30 :220 ovf_after_rnd */
    R2(CQ_FROP_SLT,  29, CQ_FR_K_ZERO),             /* 31 :223 clamp  >= 0   */
    R1(CQ_FROP_NOT1, 31),                           /* 32 :223 clamp  < 0    */
    R2(CQ_FROP_SLT,  CQ_FR_K_7FE, 29),              /* 33 :223 clamp <= 7FE  */
    R1(CQ_FROP_NOT1, 33),                           /* 34 :223 clamp  > 7FE  */
    R3(CQ_FROP_MUX,  34, CQ_FR_K_7FE,  29),         /* 35 :223 clamp hi arm  */
    R3(CQ_FROP_MUX,  32, CQ_FR_K_ZERO, 35),         /* 36 :223 clamped       */
    RV(36, 0, ALL),                                 /* 37 :223 exp_pack      */
    RV(CQ_FR_IN2, -63, ALL),                        /* 38 :224 sign << 63    */
    RV(37, -52, ALL),                               /* 39 :224 exp_pack << 52*/
    R2(CQ_FROP_OR,   38, 39),                       /* 40 :224               */
    R2(CQ_FROP_OR,   40, 26)                        /* 41 :224 normal_result */
};

enum {
    N_NORM52  = (int)(sizeof NORM52  / sizeof NORM52[0]),
    N_CLZ     = (int)(sizeof CLZ     / sizeof CLZ[0]),
    N_SUBNORM = (int)(sizeof SUBNORM / sizeof SUBNORM[0]),
    N_ROUND   = (int)(sizeof ROUND   / sizeof ROUND[0])
};

/* A row lost to an editing accident must break the BUILD rather than quietly
 * shorten a helper — an omitted operator occurrence fails by NOT EXISTING, and
 * no gate count, palindrome or pool check can observe it. */
_Static_assert(N_NORM52 == 48 && N_CLZ == 42 && N_SUBNORM == 22
               && N_ROUND == 42,
               "softfloat_common.jl:68-105 is 48 operator occurrences, "
               ":113-139 is 42, :174-191 is 22 and :198-227 is 42; changing "
               "one is a re-reading of the source");

const cq_fpround_row *cq_fpround_rows(cq_fpround_id id, int *n)
{
    if (n == NULL) cq_kernel_die("fpround: no row-count output");
    switch (id) {
    case CQ_FPR_NORM52:  *n = N_NORM52;  return NORM52;
    case CQ_FPR_CLZ:     *n = N_CLZ;     return CLZ;
    case CQ_FPR_SUBNORM: *n = N_SUBNORM; return SUBNORM;
    case CQ_FPR_ROUND:   *n = N_ROUND;   return ROUND;
    default: break;
    }
    cq_kernel_die("fpround: block id outside the four helpers");
    return NULL;
}

/* How many of s0/s1/s2 a row reads. Unread slots are never resolved, which is
 * what lets a 1-operand row leave them zero. */
int cq_fpround_arity(int op)
{
    switch (op) {
    case CQ_FROP_VIEW: case CQ_FROP_NOT1:
        return 1;
    case CQ_FROP_EQ:  case CQ_FROP_ULT:  case CQ_FROP_SLT:
    case CQ_FROP_SUB: case CQ_FROP_ADD:  case CQ_FROP_AND:
    case CQ_FROP_OR:  case CQ_FROP_BSHL: case CQ_FROP_BLSHR:
    case CQ_FROP_AND1: case CQ_FROP_OR1:
        return 2;
    case CQ_FROP_MUX:
        return 3;
    default: break;
    }
    cq_kernel_die("fpround: unknown op in the program");
    return 0;
}

/* Which row holds output `which`, in fpround.h's declaration order. Kept HERE,
 * beside the tables, because it is a fact about the port — a boundary error
 * moves exactly these indices — and a consumer or a test that wrote one down
 * would be transcribing the table a second time. */
int cq_fpround_out_row(cq_fpround_id id, int which)
{
    /* norm52: m_final, e_final (:102, :103).
     * clz:    wr, result_exp — the last stage's two mux outputs (:135, :136).
     * subnorm: wr, result_exp, subnormal, flush_to_zero, flushed_result.
     * round:  normal_result, overflow_result, exp_overflow,
     *         exp_overflow_after_round. */
    static const short OUT[CQ_FPR_N_BLOCK][5] = {
        { 46, 47, -1, -1, -1 },
        { 39, 41, -1, -1, -1 },
        { 20, 21,  0,  2, 18 },
        { 41,  2,  0, 30, -1 }
    };

    if (id < 0 || id >= CQ_FPR_N_BLOCK || which < 0 || which >= 5
        || OUT[id][which] < 0)
        cq_kernel_die("fpround: no such output on this block");
    return OUT[id][which];
}
