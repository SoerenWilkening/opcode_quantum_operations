/* src/kernels/fround_rows.inc — M38b, K25, the PROGRAM TEXT. Included by
 * src/kernels/fround.c and by nothing else.
 *
 * THE FIVE TABLES BELOW ARE THE PORT — third_party/bennett/src/softfloat/
 * fround.jl — ONE ROW PER OPERATOR OCCURRENCE in SOURCE ORDER, every row
 * citing the line it transcribes. An `.inc` under Rule 12's own exemption for
 * a large static table (tools/check_loc.sh), which is M39's PROGRAM TEXT <->
 * PROGRAM ACCESSORS seam: it keeps one table per Julia body in one place.
 *
 * THE RAW-WIRE CONVENTION IS UPSTREAM'S (K09.md §5 delta 2, PRD §15 D9(b)).
 * `cq_eq_flag` holds `a != b` and `cq_slt_block`'s `carry[W]` holds `a >=s b`,
 * so a `!=` and a `>=` read the raw wire and every `==` and `<` owes itself one
 * `lower_not1!` (arith.jl:474-478).
 *
 * EVERY `Int64` COMPARE IS `slt`, EVEN WHERE `exp` IS PROVABLY IN [0, 2047].
 * `exp < Int64(1023)` (:30) is a compare on a non-negative value and `ult`
 * would give the same bit there — a RECORDED EQUIVALENT MUTANT that L4 sees
 * and L1 does not (cq_slt_steps(64) > cq_ult_steps(64)). But the clamp's
 * compares (:37, :168, :262) are on `1075 - exp`, which is NEGATIVE for every
 * `exp > 1075`, and there `ult` is a miscompile: -972 reads as 2^64 - 972, the
 * clamp picks 52 instead of 0, and every NaN/Inf/large input reaches the
 * barrel with the wrong amount. Only the SELECTED branch hides it — the
 * `is_integer`/`is_special` rows discard `normal_result` there — so it is L1-
 * invisible too, and the census (`slt` count per program) is its detector.
 *
 * `clamp` IS D-K23-5's READING, AT ALL THREE SITES: `Base.clamp`'s body is not
 * in the snapshot (K23 §1.6), and M32 transcribes `clamp(x, lo, hi)` as
 * `ifelse(x < lo, lo, ifelse(x > hi, hi, x))` in the row order
 * slt(x, lo), not1, slt(hi, x), not1, mux(hi), mux(lo). This module copies
 * that order verbatim so the two readings cannot drift apart.
 */

#define RV(s0, sh, mk)                                                         \
    { (short)CQ_FIOP_VIEW, (short)(s0), 0, 0, (short)(sh), (mk) }
#define R1(op, s0)                                                             \
    { (short)(op), (short)(s0), 0, 0, 0, 0 }
#define R2(op, s0, s1)                                                         \
    { (short)(op), (short)(s0), (short)(s1), 0, 0, 0 }
#define R3(op, s0, s1, s2)                                                     \
    { (short)(op), (short)(s0), (short)(s1), (short)(s2), 0, 0 }

#define ALL       (~UINT64_C(0))
#define SIGN      UINT64_C(0x8000000000000000)
#define NOT_SIGN  UINT64_C(0x7FFFFFFFFFFFFFFF)
#define FRAC      CQ_FP64_FRAC_MASK
#define A         CQ_FI_A

/* --- `soft_trunc` — fround.jl:16-50, 27 rows, as a BODY at base `b`. -------
 *
 * EVERY ROW REFERENCE IS `b + T_x` AND NEVER A NUMBER: the body is inlined at
 * three bases (0, 2, 3), and a table whose operands are integers is the one
 * mistake PRD-v2 §7.1 says a kernel of this shape makes. */
enum {
    T_SIGN = 0, T_ASH, T_EXP, T_EXPNE, T_SPECIAL, T_FRAC, T_FRACNZ, T_NANIN,
    T_AQ, T_SPECRES, T_GE1023, T_SMALL, T_INTEGER, T_FB, T_CLGE0, T_CLLT0,
    T_CLLE52, T_CLGT52, T_CLHI, T_FBC, T_SHL, T_SHLM1, T_MASK, T_NORMAL,
    T_SEL3, T_SEL2, T_RESULT, T_N
};

#define FI_TRUNC(b)                                                            \
    RV(A, 0, SIGN),                               /* :17 sign             */  \
    RV(A, 52, ALL),                               /* :18 a >> 52          */  \
    RV((b) + T_ASH, 0, CQ_FP64_EXP_ALL),          /* :18 & 0x7ff -> exp   */  \
    R2(CQ_FIOP_EQ, (b) + T_EXP, CQ_FI_K_7FF),     /* :26 exp != 0x7ff     */  \
    R1(CQ_FIOP_NOT1, (b) + T_EXPNE),              /* :26 is_special       */  \
    RV(A, 0, FRAC),                               /* :27 a & FRAC_MASK    */  \
    R2(CQ_FIOP_EQ, (b) + T_FRAC, CQ_FI_K_ZERO),   /* :27 ... != 0         */  \
    R2(CQ_FIOP_AND1, (b) + T_SPECIAL, (b) + T_FRACNZ), /* :27 is_nan_input */ \
    R2(CQ_FIOP_OR, A, CQ_FI_K_QUIET),             /* :28 a | QUIET_BIT    */  \
    R3(CQ_FIOP_MUX, (b) + T_NANIN, (b) + T_AQ, A),/* :28 special_result   */  \
    R2(CQ_FIOP_SLT, (b) + T_EXP, CQ_FI_K_1023),   /* :30 exp >=s 1023     */  \
    R1(CQ_FIOP_NOT1, (b) + T_GE1023),             /* :30 is_small         */  \
    R2(CQ_FIOP_SLT, (b) + T_EXP, CQ_FI_K_1075),   /* :32 is_integer       */  \
    R2(CQ_FIOP_SUB, CQ_FI_K_1075, (b) + T_EXP),   /* :35 frac_bits        */  \
    R2(CQ_FIOP_SLT, (b) + T_FB, CQ_FI_K_ZERO),    /* :37 clamp  >= 0      */  \
    R1(CQ_FIOP_NOT1, (b) + T_CLGE0),              /* :37 clamp  <  0      */  \
    R2(CQ_FIOP_SLT, CQ_FI_K_52, (b) + T_FB),      /* :37 clamp <= 52      */  \
    R1(CQ_FIOP_NOT1, (b) + T_CLLE52),             /* :37 clamp  > 52      */  \
    R3(CQ_FIOP_MUX, (b) + T_CLGT52, CQ_FI_K_52, (b) + T_FB), /* :37 hi arm */ \
    R3(CQ_FIOP_MUX, (b) + T_CLLT0, CQ_FI_K_ZERO, (b) + T_CLHI), /* :37 fbc */ \
    R2(CQ_FIOP_BSHL, CQ_FI_K_ONE, (b) + T_FBC),   /* :39 1 << fbc         */  \
    R2(CQ_FIOP_SUB, (b) + T_SHL, CQ_FI_K_ONE),    /* :39 ... - 1          */  \
    R2(CQ_FIOP_XOR, (b) + T_SHLM1, CQ_FI_K_ONES), /* :39 ~(...) -> mask   */  \
    R2(CQ_FIOP_AND, A, (b) + T_MASK),             /* :42 normal_result    */  \
    R3(CQ_FIOP_MUX, (b) + T_INTEGER, A, (b) + T_NORMAL), /* :47           */  \
    R3(CQ_FIOP_MUX, (b) + T_SMALL, (b) + T_SIGN, (b) + T_SEL3), /* :46    */  \
    R3(CQ_FIOP_MUX, (b) + T_SPECIAL, (b) + T_SPECRES, (b) + T_SEL2) /* :45 */

static const cq_fround_row TRUNC[] = { FI_TRUNC(0) };

/* --- `soft_floor` — fround.jl:58-75, 33 rows. ------------------------------
 *
 * `one_bits` (:69) and `neg_one_bits` (:70) are CONSTANT BINDINGS over two
 * literals and are `CQ_FI_K_NEG_ONE_F`, not rows. `truncated` is the INLINED
 * trunc body's last row. */
enum {
    F_SIGN = 0, F_ISNEG, F_TRUNC,
    F_TR = F_TRUNC + T_RESULT,
    F_HASFRAC = F_TRUNC + T_N, F_NEEDSUB, F_FADD, F_RESULT, F_N
};

static const cq_fround_row FLOOR[] = {
    RV(A, 0, SIGN),                               /* :59 sign             */
    R2(CQ_FIOP_EQ, F_SIGN, CQ_FI_K_ZERO),         /* :60 is_neg = != 0    */
    FI_TRUNC(F_TRUNC),                            /* :62 soft_trunc(a)    */
    R2(CQ_FIOP_EQ, F_TR, A),                      /* :65 has_frac = !=    */
    R2(CQ_FIOP_AND1, F_ISNEG, F_HASFRAC),         /* :66 need_sub         */
    R2(CQ_FIOP_FADD, F_TR, CQ_FI_K_NEG_ONE_F),    /* :73 soft_fadd        */
    R3(CQ_FIOP_MUX, F_NEEDSUB, F_FADD, F_TR)      /* :74 result           */
};

/* --- `soft_ceil` — fround.jl:83-95, 34 rows. `is_pos = sign == 0` (:85) is
 * the one `==` and owes a `not1`; `one_bits` (:92) is a constant. --------- */
enum {
    C_SIGN = 0, C_SIGNNE, C_ISPOS, C_TRUNC,
    C_TR = C_TRUNC + T_RESULT,
    C_HASFRAC = C_TRUNC + T_N, C_NEEDADD, C_FADD, C_RESULT, C_N
};

static const cq_fround_row CEIL[] = {
    RV(A, 0, SIGN),                               /* :84 sign             */
    R2(CQ_FIOP_EQ, C_SIGN, CQ_FI_K_ZERO),         /* :85 sign != 0        */
    R1(CQ_FIOP_NOT1, C_SIGNNE),                   /* :85 is_pos           */
    FI_TRUNC(C_TRUNC),                            /* :87 soft_trunc(a)    */
    R2(CQ_FIOP_EQ, C_TR, A),                      /* :89 has_frac = !=    */
    R2(CQ_FIOP_AND1, C_ISPOS, C_HASFRAC),         /* :90 need_add         */
    R2(CQ_FIOP_FADD, C_TR, CQ_FI_K_ONE_F),        /* :93 soft_fadd        */
    R3(CQ_FIOP_MUX, C_NEEDADD, C_FADD, C_TR)      /* :94 result           */
};

/* --- `soft_round` — fround.jl:141-202, 72 rows, ties-to-EVEN. --------------
 *
 * ROW E_ROUNDUP's `&` and `|` are on Bools (`and1`, `or1`); every other `&`
 * with a computed operand is a 64-lane `and`. `(round_bit == 1)` and
 * `(lsb_after_trunc == 1)` are `eq` + `not1` each: the grain reads the
 * operator, not what a 0/1 value "means". */
enum {
    E_SIGN = 0, E_ABS, E_ASH, E_EXP, E_EXPNE, E_SPECIAL, E_FRAC1, E_FRACNZ1,
    E_NANIN, E_AQ, E_SPECRES, E_GE1022, E_BELOW,
    E_EQ1022A, E_IS1022A, E_FRAC2, E_FRACNZ2, E_FRACZ2, E_EXHALF,
    E_EQ1022B, E_IS1022B, E_FRAC3, E_FRACNZ3, E_INHALF,
    E_ONESIGN, E_INTEGER, E_FBRAW,
    E_CLGE1, E_CLLT1, E_CLLE52, E_CLGT52, E_CLHI, E_FB, E_RBP,
    E_FRAC4, E_M, E_SHL1, E_SHL1M1, E_TMASK, E_TM,
    E_MSHR, E_RB, E_SHL2, E_SMASK, E_MSM, E_STICKY, E_TMSHR, E_LSB,
    E_RBNE1, E_RBEQ1, E_LSBNE1, E_LSBEQ1, E_OR1, E_ROUNDUP,
    E_INCR, E_INCSEL, E_RM, E_RMSH53, E_CARRY, E_RMSH1, E_FM, E_EXPP1, E_FE,
    E_FESHL, E_OR_A, E_FMFRAC, E_NORMAL,
    E_SELOR, E_SEL4, E_SEL3, E_SEL2, E_RESULT, E_N
};

static const cq_fround_row EVEN[] = {
    RV(A, 0, SIGN),                               /* :142 sign            */
    RV(A, 0, NOT_SIGN),                           /* :143 abs_a           */
    RV(E_ABS, 52, ALL),                           /* :144 abs_a >> 52     */
    RV(E_ASH, 0, CQ_FP64_EXP_ALL),                /* :144 & 0x7ff -> exp  */
    R2(CQ_FIOP_EQ, E_EXP, CQ_FI_K_7FF),           /* :147 exp != 0x7ff    */
    R1(CQ_FIOP_NOT1, E_EXPNE),                    /* :147 is_special      */
    RV(E_ABS, 0, FRAC),                           /* :148 abs_a & FRAC    */
    R2(CQ_FIOP_EQ, E_FRAC1, CQ_FI_K_ZERO),        /* :148 != 0            */
    R2(CQ_FIOP_AND1, E_SPECIAL, E_FRACNZ1),       /* :148 is_nan_input    */
    R2(CQ_FIOP_OR, A, CQ_FI_K_QUIET),             /* :149 a | QUIET_BIT   */
    R3(CQ_FIOP_MUX, E_NANIN, E_AQ, A),            /* :149 special_result  */
    R2(CQ_FIOP_SLT, E_EXP, CQ_FI_K_1022),         /* :152 exp >=s 1022    */
    R1(CQ_FIOP_NOT1, E_GE1022),                   /* :152 is_below_half   */
    R2(CQ_FIOP_EQ, E_EXP, CQ_FI_K_1022),          /* :155 exp != 1022     */
    R1(CQ_FIOP_NOT1, E_EQ1022A),                  /* :155 exp == 1022     */
    RV(E_ABS, 0, FRAC),                           /* :155 abs_a & FRAC    */
    R2(CQ_FIOP_EQ, E_FRAC2, CQ_FI_K_ZERO),        /* :155 != 0            */
    R1(CQ_FIOP_NOT1, E_FRACNZ2),                  /* :155 == 0            */
    R2(CQ_FIOP_AND1, E_IS1022A, E_FRACZ2),        /* :155 is_exactly_half */
    R2(CQ_FIOP_EQ, E_EXP, CQ_FI_K_1022),          /* :158 exp != 1022     */
    R1(CQ_FIOP_NOT1, E_EQ1022B),                  /* :158 exp == 1022     */
    RV(E_ABS, 0, FRAC),                           /* :158 abs_a & FRAC    */
    R2(CQ_FIOP_EQ, E_FRAC3, CQ_FI_K_ZERO),        /* :158 != 0            */
    R2(CQ_FIOP_AND1, E_IS1022B, E_FRACNZ3),       /* :158 is_in_half_to_one */
    R2(CQ_FIOP_OR, E_SIGN, CQ_FI_K_ONE_F),        /* :159 one_with_sign   */
    R2(CQ_FIOP_SLT, E_EXP, CQ_FI_K_1075),         /* :162 is_integer      */
    R2(CQ_FIOP_SUB, CQ_FI_K_1075, E_EXP),         /* :167 frac_bits_raw   */
    R2(CQ_FIOP_SLT, E_FBRAW, CQ_FI_K_ONE),        /* :168 clamp  >= 1     */
    R1(CQ_FIOP_NOT1, E_CLGE1),                    /* :168 clamp  <  1     */
    R2(CQ_FIOP_SLT, CQ_FI_K_52, E_FBRAW),         /* :168 clamp <= 52     */
    R1(CQ_FIOP_NOT1, E_CLLE52),                   /* :168 clamp  > 52     */
    R3(CQ_FIOP_MUX, E_CLGT52, CQ_FI_K_52, E_FBRAW), /* :168 hi arm        */
    R3(CQ_FIOP_MUX, E_CLLT1, CQ_FI_K_ONE, E_CLHI),  /* :168 frac_bits     */
    R2(CQ_FIOP_SUB, E_FB, CQ_FI_K_ONE),           /* :169 round_bit_pos   */
    RV(E_ABS, 0, FRAC),                           /* :171 abs_a & FRAC    */
    R2(CQ_FIOP_OR, E_FRAC4, CQ_FI_K_IMPLICIT),    /* :171 m               */
    R2(CQ_FIOP_BSHL, CQ_FI_K_ONE, E_FB),          /* :172 1 << frac_bits  */
    R2(CQ_FIOP_SUB, E_SHL1, CQ_FI_K_ONE),         /* :172 ... - 1         */
    R2(CQ_FIOP_XOR, E_SHL1M1, CQ_FI_K_ONES),      /* :172 trunc_mask      */
    R2(CQ_FIOP_AND, E_M, E_TMASK),                /* :173 truncated_m     */
    R2(CQ_FIOP_BLSHR, E_M, E_RBP),                /* :175 m >> rbp        */
    RV(E_MSHR, 0, UINT64_C(1)),                   /* :175 & 1 round_bit   */
    R2(CQ_FIOP_BSHL, CQ_FI_K_ONE, E_RBP),         /* :176 1 << rbp        */
    R2(CQ_FIOP_SUB, E_SHL2, CQ_FI_K_ONE),         /* :176 sticky_mask     */
    R2(CQ_FIOP_AND, E_M, E_SMASK),                /* :177 m & sticky_mask */
    R2(CQ_FIOP_EQ, E_MSM, CQ_FI_K_ZERO),          /* :177 sticky = != 0   */
    R2(CQ_FIOP_BLSHR, E_TM, E_FB),                /* :178 tm >> frac_bits */
    RV(E_TMSHR, 0, UINT64_C(1)),                  /* :178 & 1 lsb         */
    R2(CQ_FIOP_EQ, E_RB, CQ_FI_K_ONE),            /* :180 round_bit != 1  */
    R1(CQ_FIOP_NOT1, E_RBNE1),                    /* :180 round_bit == 1  */
    R2(CQ_FIOP_EQ, E_LSB, CQ_FI_K_ONE),           /* :181 lsb != 1        */
    R1(CQ_FIOP_NOT1, E_LSBNE1),                   /* :181 lsb == 1        */
    R2(CQ_FIOP_OR1, E_STICKY, E_LSBEQ1),          /* :181 sticky | ...    */
    R2(CQ_FIOP_AND1, E_RBEQ1, E_OR1),             /* :180 round_up        */
    R2(CQ_FIOP_BSHL, CQ_FI_K_ONE, E_FB),          /* :183 incr            */
    R3(CQ_FIOP_MUX, E_ROUNDUP, E_INCR, CQ_FI_K_ZERO), /* :184 ifelse      */
    R2(CQ_FIOP_ADD, E_TM, E_INCSEL),              /* :184 rounded_m       */
    RV(E_RM, 53, ALL),                            /* :187 rounded_m >> 53 */
    R2(CQ_FIOP_EQ, E_RMSH53, CQ_FI_K_ZERO),       /* :187 has_carry       */
    RV(E_RM, 1, ALL),                             /* :188 rounded_m >> 1  */
    R3(CQ_FIOP_MUX, E_CARRY, E_RMSH1, E_RM),      /* :188 final_m         */
    R2(CQ_FIOP_ADD, E_EXP, CQ_FI_K_ONE),          /* :189 exp + 1         */
    R3(CQ_FIOP_MUX, E_CARRY, E_EXPP1, E_EXP),     /* :189 final_exp       */
    RV(E_FE, -52, ALL),                           /* :192 final_exp << 52 */
    R2(CQ_FIOP_OR, E_SIGN, E_FESHL),              /* :191 sign | ...      */
    RV(E_FM, 0, FRAC),                            /* :193 final_m & FRAC  */
    R2(CQ_FIOP_OR, E_OR_A, E_FMFRAC),             /* :191 normal_result   */
    R2(CQ_FIOP_OR1, E_BELOW, E_EXHALF),           /* :197 below | exactly */
    R3(CQ_FIOP_MUX, E_INTEGER, A, E_NORMAL),      /* :199                 */
    R3(CQ_FIOP_MUX, E_INHALF, E_ONESIGN, E_SEL4), /* :198                 */
    R3(CQ_FIOP_MUX, E_SELOR, E_SIGN, E_SEL3),     /* :197                 */
    R3(CQ_FIOP_MUX, E_SPECIAL, E_SPECRES, E_SEL2) /* :196 result          */
};

/* --- `soft_round_away` — fround.jl:238-295, 52 rows, ties-AWAY. -----------
 *
 * `soft_round` verbatim except at two places (:222-229): the exactly-half arm
 * folds into `is_in_half_to_one_or_half` (:254, ONE `eq`), and `round_up` is
 * `round_bit == 1` alone (:274) — no sticky, no lsb. */
enum {
    W_SIGN = 0, W_ABS, W_ASH, W_EXP, W_EXPNE, W_SPECIAL, W_FRAC1, W_FRACNZ1,
    W_NANIN, W_AQ, W_SPECRES, W_GE1022, W_BELOW, W_EQ1022, W_HALFOR,
    W_ONESIGN, W_INTEGER, W_FBRAW,
    W_CLGE1, W_CLLT1, W_CLLE52, W_CLGT52, W_CLHI, W_FB, W_RBP,
    W_FRAC2, W_M, W_SHL1, W_SHL1M1, W_TMASK, W_TM, W_MSHR, W_RB,
    W_RBNE1, W_ROUNDUP, W_INCR, W_INCSEL, W_RM, W_RMSH53, W_CARRY, W_RMSH1,
    W_FM, W_EXPP1, W_FE, W_FESHL, W_OR_A, W_FMFRAC, W_NORMAL,
    W_SEL4, W_SEL3, W_SEL2, W_RESULT, W_N
};

static const cq_fround_row AWAY[] = {
    RV(A, 0, SIGN),                               /* :239 sign            */
    RV(A, 0, NOT_SIGN),                           /* :240 abs_a           */
    RV(W_ABS, 52, ALL),                           /* :241 abs_a >> 52     */
    RV(W_ASH, 0, CQ_FP64_EXP_ALL),                /* :241 & 0x7ff -> exp  */
    R2(CQ_FIOP_EQ, W_EXP, CQ_FI_K_7FF),           /* :244 exp != 0x7ff    */
    R1(CQ_FIOP_NOT1, W_EXPNE),                    /* :244 is_special      */
    RV(W_ABS, 0, FRAC),                           /* :245 abs_a & FRAC    */
    R2(CQ_FIOP_EQ, W_FRAC1, CQ_FI_K_ZERO),        /* :245 != 0            */
    R2(CQ_FIOP_AND1, W_SPECIAL, W_FRACNZ1),       /* :245 is_nan_input    */
    R2(CQ_FIOP_OR, A, CQ_FI_K_QUIET),             /* :246 a | QUIET_BIT   */
    R3(CQ_FIOP_MUX, W_NANIN, W_AQ, A),            /* :246 special_result  */
    R2(CQ_FIOP_SLT, W_EXP, CQ_FI_K_1022),         /* :249 exp >=s 1022    */
    R1(CQ_FIOP_NOT1, W_GE1022),                   /* :249 is_below_half   */
    R2(CQ_FIOP_EQ, W_EXP, CQ_FI_K_1022),          /* :254 exp != 1022     */
    R1(CQ_FIOP_NOT1, W_EQ1022),                   /* :254 ..._or_half     */
    R2(CQ_FIOP_OR, W_SIGN, CQ_FI_K_ONE_F),        /* :255 one_with_sign   */
    R2(CQ_FIOP_SLT, W_EXP, CQ_FI_K_1075),         /* :258 is_integer      */
    R2(CQ_FIOP_SUB, CQ_FI_K_1075, W_EXP),         /* :261 frac_bits_raw   */
    R2(CQ_FIOP_SLT, W_FBRAW, CQ_FI_K_ONE),        /* :262 clamp  >= 1     */
    R1(CQ_FIOP_NOT1, W_CLGE1),                    /* :262 clamp  <  1     */
    R2(CQ_FIOP_SLT, CQ_FI_K_52, W_FBRAW),         /* :262 clamp <= 52     */
    R1(CQ_FIOP_NOT1, W_CLLE52),                   /* :262 clamp  > 52     */
    R3(CQ_FIOP_MUX, W_CLGT52, CQ_FI_K_52, W_FBRAW), /* :262 hi arm        */
    R3(CQ_FIOP_MUX, W_CLLT1, CQ_FI_K_ONE, W_CLHI),  /* :262 frac_bits     */
    R2(CQ_FIOP_SUB, W_FB, CQ_FI_K_ONE),           /* :263 round_bit_pos   */
    RV(W_ABS, 0, FRAC),                           /* :265 abs_a & FRAC    */
    R2(CQ_FIOP_OR, W_FRAC2, CQ_FI_K_IMPLICIT),    /* :265 m               */
    R2(CQ_FIOP_BSHL, CQ_FI_K_ONE, W_FB),          /* :266 1 << frac_bits  */
    R2(CQ_FIOP_SUB, W_SHL1, CQ_FI_K_ONE),         /* :266 ... - 1         */
    R2(CQ_FIOP_XOR, W_SHL1M1, CQ_FI_K_ONES),      /* :266 trunc_mask      */
    R2(CQ_FIOP_AND, W_M, W_TMASK),                /* :267 truncated_m     */
    R2(CQ_FIOP_BLSHR, W_M, W_RBP),                /* :269 m >> rbp        */
    RV(W_MSHR, 0, UINT64_C(1)),                   /* :269 & 1 round_bit   */
    R2(CQ_FIOP_EQ, W_RB, CQ_FI_K_ONE),            /* :274 round_bit != 1  */
    R1(CQ_FIOP_NOT1, W_RBNE1),                    /* :274 round_up        */
    R2(CQ_FIOP_BSHL, CQ_FI_K_ONE, W_FB),          /* :276 incr            */
    R3(CQ_FIOP_MUX, W_ROUNDUP, W_INCR, CQ_FI_K_ZERO), /* :277 ifelse      */
    R2(CQ_FIOP_ADD, W_TM, W_INCSEL),              /* :277 rounded_m       */
    RV(W_RM, 53, ALL),                            /* :280 rounded_m >> 53 */
    R2(CQ_FIOP_EQ, W_RMSH53, CQ_FI_K_ZERO),       /* :280 has_carry       */
    RV(W_RM, 1, ALL),                             /* :281 rounded_m >> 1  */
    R3(CQ_FIOP_MUX, W_CARRY, W_RMSH1, W_RM),      /* :281 final_m         */
    R2(CQ_FIOP_ADD, W_EXP, CQ_FI_K_ONE),          /* :282 exp + 1         */
    R3(CQ_FIOP_MUX, W_CARRY, W_EXPP1, W_EXP),     /* :282 final_exp       */
    RV(W_FE, -52, ALL),                           /* :285 final_exp << 52 */
    R2(CQ_FIOP_OR, W_SIGN, W_FESHL),              /* :284 sign | ...      */
    RV(W_FM, 0, FRAC),                            /* :286 final_m & FRAC  */
    R2(CQ_FIOP_OR, W_OR_A, W_FMFRAC),             /* :284 normal_result   */
    R3(CQ_FIOP_MUX, W_INTEGER, A, W_NORMAL),      /* :292                 */
    R3(CQ_FIOP_MUX, W_HALFOR, W_ONESIGN, W_SEL4), /* :291                 */
    R3(CQ_FIOP_MUX, W_BELOW, W_SIGN, W_SEL3),     /* :290                 */
    R3(CQ_FIOP_MUX, W_SPECIAL, W_SPECRES, W_SEL2) /* :289 result          */
};

#undef A
