/* src/kernels/fadd.c — M33, K15, the ROW TABLES half.
 *
 * Read docs/constructions/K15.md and fadd.h before changing anything here.
 * THE TWO ROW TABLES BELOW ARE THE WHOLE OF THIS FILE AND THEY ARE THE PORT —
 * `soft_fadd` (third_party/bennett/src/softfloat/fadd.jl:16-136) and
 * `soft_fsub`'s prologue (fsub.jl:21-24, which calls fneg.jl:6), one row per
 * OPERATOR OCCURRENCE in SOURCE ORDER, with every row citing the line it
 * transcribes. Nothing here emits a gate, names a scratch span or knows what a
 * block costs: that is fadd_step.c, on M36's ROW TABLES <-> STEP MACHINE seam
 * (K18.md D-K18-7, taken again by M32 as K23 D-K23-10).
 *
 * THE LITERAL GRAIN IS WHAT A READER CHECKS HERE (PRD-v2 §7.2). One block per
 * operator occurrence as the source spells it; NO common-subexpression
 * sharing — `sa == sb` gets two independent `eq` blocks because fadd.jl writes
 * it at :39 AND at :44, `ea_ord != 0` two because of :59 and :63, and fsub's
 * NaN test is a third spelling of a predicate M31 already owns; NO narrowing
 * towards the field width, so `ea` is a 64-lane span holding eleven bits of
 * value and `sa` one holding a single bit. All three are refused deliberately:
 * D9's K12 precedent refused exactly this class of narrowing because it is a
 * RE-DERIVATION rather than a port, and K11's finding stands that the one
 * mutant L1 cannot see is the one that looks like an optimisation.
 *
 * A VIEW ROW IS AN OPERATOR OCCURRENCE THAT COSTS NOTHING (PRD-v2 §7.3 as
 * amended; D-K23-3). An AND or a shift by a COMPILE-TIME constant is WIRING,
 * and they stay ROWS so the 1:1 correspondence with the source survives —
 * which is the thing a reader checks and the thing the slot scan walks. The
 * ONE `&` in soft_fadd whose second operand is COMPUTED is `wb & lost_mask`
 * (:78) and it is a real `and`(64) block.
 *
 * A PICK ROW IS A DESTRUCTURING BINDING AND COSTS NOTHING EITHER (fadd.h).
 * `(wr, result_exp) = _sf_normalize_clz(wr, result_exp)` at :111 is one call
 * and two names; the call is the CLZ row and each name is a PICK over it.
 *
 * THE SIX CLASS PREDICATES COMPOSE M31 RATHER THAN RE-TRANSCRIBING IT, WHICH
 * IS RULE 1 APPLIED TO THE CALL GRAPH (D-K18-8, M36's own answer). fpclass.c
 * transcribes fadd.jl:29, :31 and :33 by name — `a_nan`, `a_inf`, `a_zero` —
 * so the port of those three lines already exists in the library and this
 * table calls it once per OCCURRENCE, six times, with no CSE between them.
 *
 * THE ALIGN <-> ADD-AND-NORMALISE BOUNDARY IS MARKED BELOW AND IS NOT A FILE
 * CUT. PRD-v2 §5 recorded it in advance; fadd.h records why the file split
 * went elsewhere. It is still the right place to read the program in two
 * halves, and everything crossing it is named in the marker comment.
 */

#include "kernels/fadd.h"

#include "kernels/fpclass.h"
#include "kernels/kernel.h"

/* One row per occurrence, in the five shapes the ops need. `mask` and `shift`
 * are read only by a VIEW, a PICK or a CLASS; `s1`/`s2` only up to
 * `cq_fadd_arity`. */
#define RV(s0, sh, mk)                                                         \
    { (short)CQ_FAOP_VIEW, (short)(s0), 0, 0, (short)(sh), (mk) }
#define RP(s0, which)                                                          \
    { (short)CQ_FAOP_PICK, (short)(s0), 0, 0, (short)(which), 0 }
#define RC(s0, cls)                                                            \
    { (short)CQ_FAOP_CLASS, (short)(s0), 0, 0, (short)(cls), 0 }
#define R1(op, s0)                                                             \
    { (short)(op), (short)(s0), 0, 0, 0, 0 }
#define R2(op, s0, s1)                                                         \
    { (short)(op), (short)(s0), (short)(s1), 0, 0, 0 }
#define R3(op, s0, s1, s2)                                                     \
    { (short)(op), (short)(s0), (short)(s1), (short)(s2), 0, 0 }

#define ALL       (~UINT64_C(0))
#define NOT_SIGN  UINT64_C(0x7FFFFFFFFFFFFFFF)   /* ~SIGN_MASK, fadd.jl:17  */

/* --- `soft_fadd` — fadd.jl:16-136, 124 rows. ------------------------------
 *
 * THE RAW-WIRE CONVENTION IS UPSTREAM'S, NOT OURS (K09.md §5 delta 2, PRD §15
 * D9(b)). `cq_eq_flag` holds `a != b` and `cq_ult_block`'s `carry[W]` holds
 * `a >=u b`, so a `!=` occurrence and a `>=` occurrence read the raw wire and
 * every `==`, `<` and `>` occurrence owes itself one `lower_not1!`
 * (arith.jl:474-478, a fresh wire: CX then X).
 *
 * ROWS 26-31 ARE THE MUX-ARM-SWAP TRAP, SIX TIMES OVER, AND THEY DIFFER FROM
 * EACH OTHER ONLY IN ARM ORDER. CLAUDE.md's callout is exact: `mux(c,t,f)` and
 * `mux(c,f,t)` emit the identical (X, CX, CCX) tuple at every width and every
 * mask, keep the palindrome and leave scratch clean. A transposed pair is
 * invisible to L4, to the slot scan and to the span scan; the only detector is
 * an L1 anchor with `swap` TRUE against a reference not derived from this
 * table. Row 26 is `ifelse(swap, sb, sa)` — the SWAPPED operand first — and
 * taking the result sign from the wrong operand turns `1.0 - 2.0` into `+1.0`.
 *
 * ROW 57 IS THE DISCARDED ARM AND IS EMITTED IN FULL (PRD-v2 §7.6). `wb >> d`
 * is computed for every `d`, including the `d >= 64` band where D8's
 * mask-then-saturate diverges from Julia's `x >> 64 == 0`, and rows 59 and 63
 * throw it away above 56. DISCARDED is a claim about the RESULT and never
 * about the circuit: narrowing this barrel to 56 lanes, or skipping it, is a
 * re-derivation (Rule 1) and would strand the mux that selects it. */
static const cq_fadd_row FADD[] = {
    /* ---- unpack, fadd.jl:20-26 ---- */
    RV(CQ_FA_A, 63, ALL),                       /*  0 :20  sa               */
    RV(CQ_FA_A, 52, ALL),                       /*  1 :21  a >> 52          */
    RV(1, 0, CQ_FP64_EXP_ALL),                  /*  2 :21  ea               */
    RV(CQ_FA_A, 0, CQ_FP64_FRAC_MASK),          /*  3 :22  fa               */
    RV(CQ_FA_B, 63, ALL),                       /*  4 :24  sb               */
    RV(CQ_FA_B, 52, ALL),                       /*  5 :25  b >> 52          */
    RV(5, 0, CQ_FP64_EXP_ALL),                  /*  6 :25  eb               */
    RV(CQ_FA_B, 0, CQ_FP64_FRAC_MASK),          /*  7 :26  fb               */

    /* ---- class predicates, :29-34 — M31's three rows, six occurrences --- */
    RC(CQ_FA_A, CQ_FP_IS_NAN),                  /*  8 :29  a_nan            */
    RC(CQ_FA_B, CQ_FP_IS_NAN),                  /*  9 :30  b_nan            */
    RC(CQ_FA_A, CQ_FP_IS_INF),                  /* 10 :31  a_inf            */
    RC(CQ_FA_B, CQ_FP_IS_INF),                  /* 11 :32  b_inf            */
    RC(CQ_FA_A, CQ_FP_IS_ZERO),                 /* 12 :33  a_zero           */
    RC(CQ_FA_B, CQ_FP_IS_ZERO),                 /* 13 :34  b_zero           */

    /* ---- special-case results, :39-44 ---- */
    R2(CQ_FAOP_EQ,   0, 4),                     /* 14 :39  sa != sb         */
    R1(CQ_FAOP_NOT1, 14),                       /* 15 :39  sa == sb         */
    R3(CQ_FAOP_MUX,  15, CQ_FA_A, CQ_FA_K_INDEF), /* 16 :39 inf_inf_result  */
    RV(CQ_FA_A, 0, ALL),                        /* 17 :41  inf_finite_a     */
    RV(CQ_FA_B, 0, ALL),                        /* 18 :42  inf_finite_b     */
    R2(CQ_FAOP_EQ,   0, 4),                     /* 19 :44  sa != sb  (2nd)  */
    R1(CQ_FAOP_NOT1, 19),                       /* 20 :44  sa == sb  (2nd)  */
    R3(CQ_FAOP_MUX,  20, CQ_FA_A, CQ_FA_K_ZERO), /* 21 :44 zero_zero_result */

    /* ---- magnitude ordering, :47-56 ---- */
    RV(CQ_FA_A, 0, NOT_SIGN),                   /* 22 :47  a_mag            */
    RV(CQ_FA_B, 0, NOT_SIGN),                   /* 23 :48  b_mag            */
    R2(CQ_FAOP_ULT,  22, 23),                   /* 24 :49  a_mag >=u b_mag  */
    R1(CQ_FAOP_NOT1, 24),                       /* 25 :49  swap             */
    R3(CQ_FAOP_MUX,  25, 4, 0),                 /* 26 :51  sa_ord           */
    R3(CQ_FAOP_MUX,  25, 0, 4),                 /* 27 :52  sb_ord           */
    R3(CQ_FAOP_MUX,  25, 6, 2),                 /* 28 :53  ea_ord           */
    R3(CQ_FAOP_MUX,  25, 2, 6),                 /* 29 :54  eb_ord           */
    R3(CQ_FAOP_MUX,  25, 7, 3),                 /* 30 :55  fa_ord           */
    R3(CQ_FAOP_MUX,  25, 3, 7),                 /* 31 :56  fb_ord           */

    /* ---- implicit bit and effective exponents, :59-65 ---- */
    R2(CQ_FAOP_EQ,   28, CQ_FA_K_ZERO),         /* 32 :59  ea_ord != 0      */
    R2(CQ_FAOP_OR,   30, CQ_FA_K_IMPLICIT),     /* 33 :59  fa_ord|IMPLICIT  */
    R3(CQ_FAOP_MUX,  32, 33, 30),               /* 34 :59  ma               */
    R2(CQ_FAOP_EQ,   29, CQ_FA_K_ZERO),         /* 35 :60  eb_ord != 0      */
    R2(CQ_FAOP_OR,   31, CQ_FA_K_IMPLICIT),     /* 36 :60  fb_ord|IMPLICIT  */
    R3(CQ_FAOP_MUX,  35, 36, 31),               /* 37 :60  mb               */
    R2(CQ_FAOP_EQ,   28, CQ_FA_K_ZERO),         /* 38 :63  ea_ord != 0 (2nd)*/
    R3(CQ_FAOP_MUX,  38, 28, CQ_FA_K_ONE),      /* 39 :63  ea_eff           */
    R2(CQ_FAOP_EQ,   29, CQ_FA_K_ZERO),         /* 40 :64  eb_ord != 0 (2nd)*/
    R3(CQ_FAOP_MUX,  40, 29, CQ_FA_K_ONE),      /* 41 :64  eb_eff           */
    R2(CQ_FAOP_SUB,  39, 41),                   /* 42 :65  d                */

    /* ---- working format, :68-69 ---- */
    RV(34, -3, ALL),                            /* 43 :68  wa = ma << 3     */
    RV(37, -3, ALL),                            /* 44 :69  wb = mb << 3     */

    /* ---- align, :73-83 ---- */
    R2(CQ_FAOP_EQ,   44, CQ_FA_K_ZERO),         /* 45 :73  wb != 0          */
    R3(CQ_FAOP_MUX,  45, CQ_FA_K_ONE, CQ_FA_K_ZERO), /* 46 :73 wb_large     */
    R2(CQ_FAOP_EQ,   42, CQ_FA_K_ZERO),         /* 47 :76  d != 0           */
    R1(CQ_FAOP_NOT1, 47),                       /* 48 :76  d == 0           */
    R2(CQ_FAOP_ULT,  42, CQ_FA_K_64),           /* 49 :76  d >=u 64         */
    R3(CQ_FAOP_MUX,  49, CQ_FA_K_63, 42),       /* 50 :76  inner ifelse     */
    R3(CQ_FAOP_MUX,  48, CQ_FA_K_ONE, 50),      /* 51 :76  d_clamped        */
    R2(CQ_FAOP_BSHL, CQ_FA_K_ONE, 51),          /* 52 :77  1 << d_clamped   */
    R2(CQ_FAOP_SUB,  52, CQ_FA_K_ONE),          /* 53 :77  lost_mask        */
    R2(CQ_FAOP_AND,  44, 53),                   /* 54 :78  wb & lost_mask   */
    R2(CQ_FAOP_EQ,   54, CQ_FA_K_ZERO),         /* 55 :78  … != 0           */
    R3(CQ_FAOP_MUX,  55, CQ_FA_K_ONE, CQ_FA_K_ZERO), /* 56 :78 sticky       */
    R2(CQ_FAOP_BLSHR, 44, 42),                  /* 57 :79  wb >> d DISCARDED
                                                 *         above d = 56      */
    R2(CQ_FAOP_OR,   57, 56),                   /* 58 :79  wb_mid           */
    R2(CQ_FAOP_ULT,  42, CQ_FA_K_56),           /* 59 :81  d >=u 56         */
    R2(CQ_FAOP_ULT,  CQ_FA_K_ZERO, 42),         /* 60 :82  0 >=u d          */
    R1(CQ_FAOP_NOT1, 60),                       /* 61 :82  d > 0            */
    R3(CQ_FAOP_MUX,  61, 58, 44),               /* 62 :82  inner ifelse     */
    R3(CQ_FAOP_MUX,  59, 46, 62),               /* 63 :81  wb_aligned       */

    /* ═══ ALIGN <-> ADD-AND-NORMALISE (PRD-v2 §5), fadd.jl:83 | :86 ═══
     * Everything crossing it is named: `wa` (43), `wb_aligned` (63),
     * `sa_ord` (26), `sb_ord` (27), `ea_eff` (39), the six class flags
     * (8-13) and the two special-case results (16, 21). The ALIGN half is
     * the only half containing a barrel, so D8 lives entirely on one side. */

    /* ---- add, subtract, sign select, :86-100 ---- */
    R2(CQ_FAOP_ADD,  43, 63),                   /* 64 :86  wr_add           */
    R2(CQ_FAOP_SUB,  43, 63),                   /* 65 :87  wr_sub           */
    R2(CQ_FAOP_EQ,   26, 27),                   /* 66 :89  sa_ord != sb_ord */
    R1(CQ_FAOP_NOT1, 66),                       /* 67 :89  same_sign        */
    R3(CQ_FAOP_MUX,  67, 64, 65),               /* 68 :90  wr_raw           */
    R1(CQ_FAOP_NOT1, 67),                       /* 69 :93  !same_sign       */
    R2(CQ_FAOP_EQ,   65, CQ_FA_K_ZERO),         /* 70 :93  wr_sub != 0      */
    R1(CQ_FAOP_NOT1, 70),                       /* 71 :93  wr_sub == 0      */
    R2(CQ_FAOP_AND1, 69, 71),                   /* 72 :93  exact_cancel     */
    RV(26, 0, ALL),                             /* 73 :95  result_sign      */
    R3(CQ_FAOP_MUX,  72, CQ_FA_K_ONE, 68),      /* 74 :99  wr               */
    RV(39, 0, ALL),                             /* 75 :100 Int64(ea_eff)    */

    /* ---- normalise: the addition carry, :103-108 ---- */
    RV(74, 56, ALL),                            /* 76 :103 wr >> 56         */
    R2(CQ_FAOP_EQ,   76, CQ_FA_K_ZERO),         /* 77 :103 overflow         */
    RV(74, 0, UINT64_C(1)),                     /* 78 :104 lost_ov          */
    RV(74, 1, ALL),                             /* 79 :105 wr >> 1          */
    R2(CQ_FAOP_OR,   79, 78),                   /* 80 :105 wr_ov            */
    R2(CQ_FAOP_ADD,  75, CQ_FA_K_ONE),          /* 81 :106 exp_ov           */
    R3(CQ_FAOP_MUX,  77, 80, 74),               /* 82 :107 wr               */
    R3(CQ_FAOP_MUX,  77, 81, 75),               /* 83 :108 result_exp       */

    /* ---- the three shared helpers, M32's blocks, :111-119 ---- */
    R2(CQ_FAOP_CLZ,  82, 83),                   /* 84 :111 _sf_normalize_clz*/
    RP(84, CQ_FA_PICK_WR),                      /* 85 :111 wr               */
    RP(84, CQ_FA_PICK_EXP),                     /* 86 :111 result_exp       */
    R3(CQ_FAOP_SUBNORM, 85, 86, 73),            /* 87 :114 _sf_handle_subn. */
    RP(87, CQ_FA_PICK_WR),                      /* 88 :114 wr               */
    RP(87, CQ_FA_PICK_EXP),                     /* 89 :114 result_exp       */
    RP(87, CQ_FA_PICK_FLUSHED),                 /* 90 :114 flushed_result   */
    RP(87, CQ_FA_PICK_FLAG),                    /* 91 :114 subnormal        */
    RP(87, CQ_FA_PICK_FTZ),                     /* 92 :114 flush_to_zero    */
    R3(CQ_FAOP_ROUND, 88, 89, 73),              /* 93 :118 _sf_round_and_p. */
    RP(93, CQ_FA_PICK_NORMAL),                  /* 94 :118 normal_result    */
    RP(93, CQ_FA_PICK_OVFRES),                  /* 95 :118 overflow_result  */
    RP(93, CQ_FA_PICK_EXPOVF),                  /* 96 :118 exp_overflow     */
    RP(93, CQ_FA_PICK_EXPOVFA),                 /* 97 :118 …_after_round    */

    /* ---- the final select chain, :123-133, priority BOTTOM-UP ---- */
    R2(CQ_FAOP_OR1,  96, 97),                   /* 98  :124                 */
    R3(CQ_FAOP_MUX,  98, 95, 94),               /* 99  :124 res1            */
    R2(CQ_FAOP_AND1, 91, 92),                   /* 100 :125 subn & ftz      */
    R3(CQ_FAOP_MUX,  100, 90, 99),              /* 101 :125 res2            */
    R3(CQ_FAOP_MUX,  72, CQ_FA_K_ZERO, 101),    /* 102 :126 res3            */
    R2(CQ_FAOP_AND1, 12, 13),                   /* 103 :127 a_zero & b_zero */
    R3(CQ_FAOP_MUX,  103, 21, 102),             /* 104 :127 res4            */
    R1(CQ_FAOP_NOT1, 12),                       /* 105 :128 !a_zero         */
    R2(CQ_FAOP_AND1, 13, 105),                  /* 106 :128                 */
    R3(CQ_FAOP_MUX,  106, CQ_FA_A, 104),        /* 107 :128 res5            */
    R1(CQ_FAOP_NOT1, 13),                       /* 108 :129 !b_zero         */
    R2(CQ_FAOP_AND1, 12, 108),                  /* 109 :129                 */
    R3(CQ_FAOP_MUX,  109, CQ_FA_B, 107),        /* 110 :129 res6            */
    R2(CQ_FAOP_AND1, 10, 11),                   /* 111 :130 a_inf & b_inf   */
    R3(CQ_FAOP_MUX,  111, 16, 110),             /* 112 :130 res7            */
    R1(CQ_FAOP_NOT1, 10),                       /* 113 :131 !a_inf          */
    R2(CQ_FAOP_AND1, 11, 113),                  /* 114 :131                 */
    R3(CQ_FAOP_MUX,  114, 18, 112),             /* 115 :131 res8            */
    R1(CQ_FAOP_NOT1, 11),                       /* 116 :132 !b_inf          */
    R2(CQ_FAOP_AND1, 10, 116),                  /* 117 :132                 */
    R3(CQ_FAOP_MUX,  117, 17, 115),             /* 118 :132 res9            */
    R2(CQ_FAOP_OR1,  8, 9),                     /* 119 :133 a_nan | b_nan   */
    R2(CQ_FAOP_OR,   CQ_FA_A, CQ_FA_K_QUIET),   /* 120 :133 a | QUIET_BIT   */
    R2(CQ_FAOP_OR,   CQ_FA_B, CQ_FA_K_QUIET),   /* 121 :133 b | QUIET_BIT   */
    R3(CQ_FAOP_MUX,  8, 120, 121),              /* 122 :133 _sf_prop._nan2  */
    R3(CQ_FAOP_MUX,  119, 122, 118)             /* 123 :133 result          */
};

/* --- `soft_fsub`'s prologue — fsub.jl:21-24 + fneg.jl:6, 9 rows. ----------
 *
 * THIS IS `Bennett-m63k` AND IT IS FIVE ROWS OF GUARD PLUS TWO OF ARITHMETIC.
 * Row 8 is `b_eff` and `cq_fadd_program` makes it the `b` of every row of
 * FADD above — including rows 121 and 110, which is the whole of the fix:
 * fadd.jl:133 hands `_sf_propagate_nan2` its `b` argument, and without this
 * guard that `b` is already sign-flipped, so `normal - NaN` comes back with
 * the wrong sign bit (IEEE 754-2019 §6.2.3 requires the sign to propagate).
 *
 * ROWS 0-2 ARE NOT rows 4-7 OF FADD, AND THAT IS THE NO-CSE RULE RATHER THAN
 * AN OVERSIGHT. fsub.jl:21 spells the exponent `(b & EXP_MASK) >> 52` and
 * fadd.jl:25 spells the same value `(b >> 52) & 0x7FF` — two different
 * operator sequences, both transcribed (K15.md §1.4 consequence 2). They are
 * also computed from DIFFERENT operands: fadd's are over `b_eff`. */
static const cq_fadd_row FSUB_PROLOGUE[] = {
    RV(CQ_FA_B, 0, CQ_FP64_EXP_MASK),           /* 0 :21  b & EXP_MASK      */
    RV(0, 52, ALL),                             /* 1 :21  ea_b              */
    RV(CQ_FA_B, 0, CQ_FP64_FRAC_MASK),          /* 2 :22  fa_b              */
    R2(CQ_FAOP_EQ,   1, CQ_FA_K_7FF),           /* 3 :23  ea_b != 0x7FF     */
    R1(CQ_FAOP_NOT1, 3),                        /* 4 :23  ea_b == 0x7FF     */
    R2(CQ_FAOP_EQ,   2, CQ_FA_K_ZERO),          /* 5 :23  fa_b != 0         */
    R2(CQ_FAOP_AND1, 4, 5),                     /* 6 :23  b_is_nan          */
    R2(CQ_FAOP_XOR,  CQ_FA_B, CQ_FA_K_SIGN),    /* 7 :24  soft_fneg(b)      */
    R3(CQ_FAOP_MUX,  6, CQ_FA_B, 7)             /* 8 :24  b_eff             */
};

enum {
    N_FADD = (int)(sizeof FADD / sizeof FADD[0]),
    N_FSUB_PRE = (int)(sizeof FSUB_PROLOGUE / sizeof FSUB_PROLOGUE[0])
};

/* A row lost to an editing accident must break the BUILD rather than quietly
 * shorten the program — an omitted operator occurrence fails by NOT EXISTING,
 * and no gate count, palindrome or pool check can observe it. */
_Static_assert(N_FADD == 124 && N_FSUB_PRE == 9,
               "fadd.jl:16-136 is 124 operator occurrences under §7.2's grain "
               "(19 views, 11 picks, 94 emitting) and fsub.jl:21-24 is 9; "
               "changing one is a re-reading of the source");

_Static_assert(N_FSUB_PRE + N_FADD <= CQ_FADD_MAX_ROWS,
               "fsub's program must fit the prefix-offset buffer");

const cq_fadd_row *cq_fadd_body_rows(int *n)
{
    if (n == NULL) cq_kernel_die("fadd: no row-count output");
    *n = N_FADD;
    return FADD;
}

const cq_fadd_row *cq_fsub_prologue_rows(int *n)
{
    if (n == NULL) cq_kernel_die("fadd: no row-count output");
    *n = N_FSUB_PRE;
    return FSUB_PROLOGUE;
}

/* How many of s0/s1/s2 a row reads. Unread slots are never rebased and never
 * resolved, which is what lets a 1-operand row leave them zero. */
int cq_fadd_arity(int op)
{
    switch (op) {
    case CQ_FAOP_VIEW: case CQ_FAOP_PICK: case CQ_FAOP_CLASS:
    case CQ_FAOP_NOT1:
        return 1;
    case CQ_FAOP_EQ:  case CQ_FAOP_ULT:  case CQ_FAOP_SUB: case CQ_FAOP_ADD:
    case CQ_FAOP_AND: case CQ_FAOP_OR:   case CQ_FAOP_XOR:
    case CQ_FAOP_BSHL: case CQ_FAOP_BLSHR:
    case CQ_FAOP_AND1: case CQ_FAOP_OR1: case CQ_FAOP_CLZ:
        return 2;
    case CQ_FAOP_MUX: case CQ_FAOP_SUBNORM: case CQ_FAOP_ROUND:
        return 3;
    default: break;
    }
    cq_kernel_die("fadd: unknown op in the program");
    return 0;
}

/* Copy `src` into `out` shifting every internal row reference by `base` and
 * rebasing `CQ_FA_B` onto `b_row` when one is given. A negative code other
 * than `CQ_FA_B` names a rail or a constant and is untouched. */
static int emit_body(const cq_fadd_row *src, int n, int base, int b_row,
                     cq_fadd_row *out)
{
    for (int i = 0; i < n; i++) {
        /* Copied OUT and back rather than indexed through `&out[i].s0`:
         * walking off one member into the next is undefined even where the
         * layout obliges, and this file compiles under -fsanitize=undefined. */
        short s[3] = { src[i].s0, src[i].s1, src[i].s2 };
        int arity = cq_fadd_arity(src[i].op);

        for (int j = 0; j < arity; j++) {
            if (s[j] >= 0)                   s[j] = (short)(s[j] + base);
            else if (s[j] == CQ_FA_B && b_row >= 0) s[j] = (short)b_row;
        }
        out[base + i].op = src[i].op;
        out[base + i].s0 = s[0];
        out[base + i].s1 = s[1];
        out[base + i].s2 = s[2];
        out[base + i].shift = src[i].shift;
        out[base + i].mask  = src[i].mask;
    }
    return base + n;
}

int cq_fadd_program(cq_fadd_prog p, cq_fadd_row *out)
{
    if (out == NULL) cq_kernel_die("fadd: no output buffer for a program");

    if (p == CQ_FADD_PROG_ADD)
        return emit_body(FADD, N_FADD, 0, -1, out);
    if (p == CQ_FADD_PROG_SUB) {
        int n = emit_body(FSUB_PROLOGUE, N_FSUB_PRE, 0, -1, out);

        /* fsub.jl:25's `soft_fadd(a, b_eff)`: the SAME row program, with the
         * prologue's last row standing in for the second argument. */
        return emit_body(FADD, N_FADD, n, n - 1, out);
    }
    cq_kernel_die("fadd: program id is neither ADD nor SUB");
    return 0;
}

int cq_fadd_result_row(cq_fadd_prog p)
{
    if (p == CQ_FADD_PROG_ADD) return N_FADD - 1;
    if (p == CQ_FADD_PROG_SUB) return N_FSUB_PRE + N_FADD - 1;
    cq_kernel_die("fadd: program id is neither ADD nor SUB");
    return 0;
}
