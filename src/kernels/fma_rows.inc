/* src/kernels/fma_rows.inc — M39, K20. THE PROGRAM TEXT: the row enumerators
 * and the static row table, and nothing else. Included exactly ONCE, by
 * src/kernels/fma.c, which owns the accessors over it.
 *
 * WHY AN `.inc` — IT IS RULE 12's OWN EXEMPTION, NOT A DODGE. tools/check_loc.sh
 * exempts `*.inc` in so many words: "Rule 12 counts .c/.h/.py only, which is
 * what makes 'move the big static table into an .inc' a real escape hatch
 * rather than a rename that dodges the guard". K20's program is 395 rows over
 * 21 ops — 3.1x M34's and 2.6x M32's — and the enumerator list alone is a
 * hundred lines. The alternative is two tables, which means ONE prefix sum
 * living in two files: K16.md §2.2's seam box refuses that outright ("the
 * prefix-sum table stays in one place, because two tables is two chances to
 * get the decode wrong") and M33, M34, M35 and M40 each refused it in turn.
 * So the fifth seam is PROGRAM TEXT <-> PROGRAM ACCESSORS, recorded in fma.h
 * before a line was written, and it is a file boundary that moves no logic.
 *
 * THE TABLE IS THE PORT — `soft_fma` (fma.jl:28-211) plus the NINE helpers it
 * INLINES, ONE ROW PER OPERATOR OCCURRENCE in SOURCE ORDER, every row citing
 * the line it transcribes. A citation `c:NNN` is softfloat_common.jl; a bare
 * `:NNN` is fma.jl. Nothing here emits a gate, names a scratch span or knows
 * what a block costs.
 *
 * THE LITERAL GRAIN IS WHAT A READER CHECKS HERE (PRD-v2 §7.2). One block per
 * operator occurrence as the source spells it; NO common-subexpression
 * sharing — `a_zero | b_zero` gets TWO whole `or1` rows because `:55` and
 * `:58` each write it (FA_PZ_OR and FA_PRODZERO), `!same_sign` gets THREE
 * `not1` rows for `:101`, `:125` and `:137` — `:55`'s FA_PZ_NOT is a `not1`
 * too but over a DIFFERENT expression, `!(a_zero|b_zero)`, which is the kind
 * of near-miss this paragraph exists to make a reader check — and
 * `sign_prod == sc` gets two
 * `eq` rows for `:90` and `:195`; NO narrowing towards the field width, so
 * `ea` is a 64-lane span holding eleven bits of value and `mid_carry` a
 * 64-lane span holding one. Both refusals are D9's K12 precedent: a narrowing
 * here would be a RE-DERIVATION rather than a port.
 *
 * A VIEW ROW IS AN OPERATOR OCCURRENCE THAT COSTS NOTHING (PRD-v2 §7.3 as
 * amended 2026-09-18). An AND or a shift by a COMPILE-TIME constant is WIRING,
 * and they stay ROWS so the 1:1 correspondence with the source survives —
 * which is the thing a reader checks and the thing the slot scan walks.
 *
 * A REBINDING IS A VIEW ROW. `hiB = UInt64(0)` (c:397) and `hiC = UInt64(0)`
 * (c:401) name a literal, and `result = normal_result` (:199) names an earlier
 * row; each is an occurrence a reader looks for and each costs nothing. M35
 * settled this shape for `result = normal_result` and it is followed here.
 *
 * THREE HELPER BODIES ARE MACROS, AND THE MACRO IS HOW THEY ARE WRITTEN
 * RATHER THAN A CLAIM THAT THE COPIES SHARE ANYTHING — M40's `FS_ITER(t)`
 * exactly. `_shiftRightJam128` occurs twice (:99, :110), `_shr128jam_by1`
 * three times (:100, :154, :162) and `_sf_clz128_to_hi_bit61`'s stage six
 * times (c:434-462), and each expansion gets its OWN enumerators and its OWN
 * spans. Writing 94 + 18 + 66 rows out by hand would be 178 more chances at
 * the one mistake PRD-v2 §7.1 says a kernel of this shape actually makes.
 */

/* One row per occurrence, in the five shapes the ops need. `mask` and `shift`
 * are read only by a VIEW; `s1`/`s2` only up to `cq_fma_arity`. */
#define RV(s0, sh, mk)                                                         \
    { (short)CQ_FUOP_VIEW, (short)(s0), 0, 0, (short)(sh), (mk) }
#define RO(s0, which)                                                          \
    { (short)CQ_FUOP_OUT, (short)(s0), (short)(which), 0, 0, 0 }
#define R1(op, s0)                                                             \
    { (short)(op), (short)(s0), 0, 0, 0, 0 }
#define R2(op, s0, s1)                                                         \
    { (short)(op), (short)(s0), (short)(s1), 0, 0, 0 }
#define R3(op, s0, s1, s2)                                                     \
    { (short)(op), (short)(s0), (short)(s1), (short)(s2), 0, 0 }

#define ALL ~UINT64_C(0)

/* --- The three macro'd helper bodies. ------------------------------------- */

/* `_shr128jam_by1(a_hi, a_lo)` — softfloat_common.jl:348-353. Six rows, ALL
 * views but the two ORs: a 128-bit right shift by one with the bit that falls
 * off the bottom OR-ed back into bit 0. */
#define SHRJ_E(P)  P##_STICKY, P##_HI, P##_LO1, P##_HI63, P##_LOA, P##_LO
#define SHRJ_R(P, HI, LO)                                                      \
    RV(LO, 0, UINT64_C(1)),                     /* c:349 sticky            */  \
    RV(HI, 1, ALL),                             /* c:350 hi                */  \
    RV(LO, 1, ALL),                             /* c:351 a_lo >> 1         */  \
    RV(HI, -63, ALL),                           /* c:351 a_hi << 63        */  \
    R2(CQ_FUOP_OR, P##_LO1, P##_HI63),          /* c:351 | (1 of 2)        */  \
    R2(CQ_FUOP_OR, P##_LOA, P##_STICKY)         /* c:351 lo                */

/* One stage of `_sf_clz128_to_hi_bit61` — softfloat_common.jl:433-465, six
 * stages of eleven rows. `MK` is the probe mask spelled as the source spells
 * it, `N` the shift distance, `KN` the exponent decrement's constant code.
 *
 * THE PROBE IS A VIEW AND THE `== 0` IS TWO ROWS. `hi & (0xFFFFFFFF << 30)` is
 * an AND by a compile-time constant, so §7.3 makes it wiring; `== UInt64(0)`
 * is an `eq` block whose RAW wire is `!=`, plus upstream's own `lower_not1!`
 * to restore the polarity. */
#define CLZ_E(P)                                                               \
    P##_MSK, P##_NE, P##_NEED, P##_SHL, P##_SHR, P##_CAT, P##_HI,              \
    P##_LSH, P##_LO, P##_ESUB, P##_E
#define CLZ_R(P, HI, LO, E, MK, N, KN)                                         \
    RV(HI, 0, (MK)),                            /* c:434 hi & (mask << k)  */  \
    R2(CQ_FUOP_EQ, P##_MSK, CQ_FU_K_ZERO),      /* c:434 != 0              */  \
    R1(CQ_FUOP_NOT1, P##_NE),                   /* c:434 needN             */  \
    RV(HI, -(N), ALL),                          /* c:435 hi << N           */  \
    RV(LO, 64 - (N), ALL),                      /* c:435 lo >> (64-N)      */  \
    R2(CQ_FUOP_OR, P##_SHL, P##_SHR),           /* c:435 |                 */  \
    R3(CQ_FUOP_MUX, P##_NEED, P##_CAT, HI),     /* c:435 hi                */  \
    RV(LO, -(N), ALL),                          /* c:436 lo << N           */  \
    R3(CQ_FUOP_MUX, P##_NEED, P##_LSH, LO),     /* c:436 lo                */  \
    R2(CQ_FUOP_SUB, E, KN),                     /* c:437 e - N             */  \
    R3(CQ_FUOP_MUX, P##_NEED, P##_ESUB, E)      /* c:437 e                 */

/* `_shiftRightJam128(a_hi, a_lo, dist)` — softfloat_common.jl:373-412. 47
 * rows: a four-case branchless 128-bit right shift with sticky jam, with all
 * four cases COMPUTED and selected at c:407-410. `D` is the distance operand.
 *
 * SIX OF THE PORT'S TWELVE VARIABLE-AMOUNT SHIFTS ARE IN HERE (PRD-v2 §7.6's
 * `softfloat_common.jl` row), and every one is CLAMPED: c:382 and c:385's
 * left shift take `dA in [1, 63]`, c:384 and c:385's right shift take the
 * same, c:394 and c:398 take `dB_u in [0, 63]`. None is DISCARDED, so K15's
 * `d >= 64` reasoning does not carry here — and at W = 64 D8's mask is the
 * whole of it and the saturation never fires.
 *
 * BOTH CLAMPS ARE ON A GENUINELY NEGATIVE `Int64` AND ARE THEREFORE SIGNED
 * (PRD-v2 §7.6's "upstream clamps in two different signednesses"). `dist` is
 * `-expDiff` at :99 and `expDiff` at :110, both of which reach large negative
 * values; substituting `cq_ult_block` for `cq_slt_block` at any of these
 * ten compares inverts the guard and is K9's `uge`-meaning-`ule` a fourth
 * time. `clamp` itself is the one construction here Rule 1 does not cover
 * (`Base.clamp`'s body is not in the snapshot) — the six-row transcription is
 * M32's, followed verbatim rather than re-derived (K23 §5.5). */
#define SRJ_E(P)                                                               \
    P##_NONPOS,                                                                \
    P##_DA_GE, P##_DA_LT, P##_DA_LE, P##_DA_GT, P##_DA_HI, P##_DA,             \
    P##_MA_SH, P##_LOSTA, P##_ANDA, P##_STA_NE, P##_STICKYA,                   \
    P##_HIA, P##_SUB64, P##_SHLHI, P##_SHRLO, P##_LOA1, P##_LOA,               \
    P##_DB_GE, P##_DB_LT, P##_DB_LE, P##_DB_GT, P##_DB_HI, P##_DB, P##_DBU,    \
    P##_MB_SH, P##_LOSTB, P##_LONZ, P##_ANDB, P##_STB_NE, P##_STB_OR,          \
    P##_STICKYB, P##_HIB, P##_SHRHIB, P##_LOB,                                 \
    P##_HIC, P##_ORHL, P##_LOC_NE, P##_LOC,                                    \
    P##_BIG, P##_MID, P##_HISEL1, P##_HISEL, P##_LOSEL1, P##_LOSEL,            \
    P##_HI, P##_LO
#define SRJ_R(P, HI, LO, D)                                                    \
    R2(CQ_FUOP_SLT, CQ_FU_K_ZERO, D),           /* c:377 dist <= 0         */  \
    R2(CQ_FUOP_SLT, D, CQ_FU_K_ONE),            /* c:380 clamp  >= 1       */  \
    R1(CQ_FUOP_NOT1, P##_DA_GE),                /* c:380 clamp   < 1       */  \
    R2(CQ_FUOP_SLT, CQ_FU_K_63, D),             /* c:380 clamp <= 63       */  \
    R1(CQ_FUOP_NOT1, P##_DA_LE),                /* c:380 clamp   > 63      */  \
    R3(CQ_FUOP_MUX, P##_DA_GT, CQ_FU_K_63, D),  /* c:380 clamp hi arm      */  \
    R3(CQ_FUOP_MUX, P##_DA_LT, CQ_FU_K_ONE, P##_DA_HI), /* c:380 dA        */  \
    R2(CQ_FUOP_BSHL, CQ_FU_K_ONE, P##_DA),      /* c:382 1 << dA_u         */  \
    R2(CQ_FUOP_SUB, P##_MA_SH, CQ_FU_K_ONE),    /* c:382 lostA_mask        */  \
    R2(CQ_FUOP_AND, LO, P##_LOSTA),             /* c:383 a_lo & lostA_mask */  \
    R2(CQ_FUOP_EQ, P##_ANDA, CQ_FU_K_ZERO),     /* c:383 != 0              */  \
    R3(CQ_FUOP_MUX, P##_STA_NE, CQ_FU_K_ONE, CQ_FU_K_ZERO), /* c:383 stkyA */  \
    R2(CQ_FUOP_BLSHR, HI, P##_DA),              /* c:384 hiA               */  \
    R2(CQ_FUOP_SUB, CQ_FU_K_64, P##_DA),        /* c:385 64 - dA_u         */  \
    R2(CQ_FUOP_BSHL, HI, P##_SUB64),            /* c:385 a_hi << (64-dA_u) */  \
    R2(CQ_FUOP_BLSHR, LO, P##_DA),              /* c:385 a_lo >> dA_u      */  \
    R2(CQ_FUOP_OR, P##_SHLHI, P##_SHRLO),       /* c:385 | (1 of 2)        */  \
    R2(CQ_FUOP_OR, P##_LOA1, P##_STICKYA),      /* c:385 loA               */  \
    R2(CQ_FUOP_SLT, D, CQ_FU_K_64),             /* c:388 clamp  >= 64      */  \
    R1(CQ_FUOP_NOT1, P##_DB_GE),                /* c:388 clamp   < 64      */  \
    R2(CQ_FUOP_SLT, CQ_FU_K_127, D),            /* c:388 clamp <= 127      */  \
    R1(CQ_FUOP_NOT1, P##_DB_LE),                /* c:388 clamp   > 127     */  \
    R3(CQ_FUOP_MUX, P##_DB_GT, CQ_FU_K_127, D), /* c:388 clamp hi arm      */  \
    R3(CQ_FUOP_MUX, P##_DB_LT, CQ_FU_K_64, P##_DB_HI), /* c:388 dB         */  \
    R2(CQ_FUOP_SUB, P##_DB, CQ_FU_K_64),        /* c:389 dB_u              */  \
    R2(CQ_FUOP_BSHL, CQ_FU_K_ONE, P##_DBU),     /* c:394 1 << dB_u         */  \
    R2(CQ_FUOP_SUB, P##_MB_SH, CQ_FU_K_ONE),    /* c:394 hi_lost_mask_B    */  \
    R2(CQ_FUOP_EQ, LO, CQ_FU_K_ZERO),           /* c:395 a_lo != 0         */  \
    R2(CQ_FUOP_AND, HI, P##_LOSTB),             /* c:395 a_hi & mask       */  \
    R2(CQ_FUOP_EQ, P##_ANDB, CQ_FU_K_ZERO),     /* c:395 != 0              */  \
    R2(CQ_FUOP_OR1, P##_LONZ, P##_STB_NE),      /* c:395 |                 */  \
    R3(CQ_FUOP_MUX, P##_STB_OR, CQ_FU_K_ONE, CQ_FU_K_ZERO), /* c:395 stkyB */  \
    RV(CQ_FU_K_ZERO, 0, ALL),                   /* c:397 hiB = 0           */  \
    R2(CQ_FUOP_BLSHR, HI, P##_DBU),             /* c:398 a_hi >> dB_u      */  \
    R2(CQ_FUOP_OR, P##_SHRHIB, P##_STICKYB),    /* c:398 loB               */  \
    RV(CQ_FU_K_ZERO, 0, ALL),                   /* c:401 hiC = 0           */  \
    R2(CQ_FUOP_OR, HI, LO),                     /* c:402 a_hi | a_lo       */  \
    R2(CQ_FUOP_EQ, P##_ORHL, CQ_FU_K_ZERO),     /* c:402 != 0              */  \
    R3(CQ_FUOP_MUX, P##_LOC_NE, CQ_FU_K_ONE, CQ_FU_K_ZERO), /* c:402 loC   */  \
    R2(CQ_FUOP_SLT, D, CQ_FU_K_128),            /* c:405 dist >= 128       */  \
    R2(CQ_FUOP_SLT, D, CQ_FU_K_64),             /* c:406 dist >= 64        */  \
    R3(CQ_FUOP_MUX, P##_MID, P##_HIB, P##_HIA), /* c:407 inner ifelse      */  \
    R3(CQ_FUOP_MUX, P##_BIG, P##_HIC, P##_HISEL1), /* c:407 hi_sel         */  \
    R3(CQ_FUOP_MUX, P##_MID, P##_LOB, P##_LOA), /* c:408 inner ifelse      */  \
    R3(CQ_FUOP_MUX, P##_BIG, P##_LOC, P##_LOSEL1), /* c:408 lo_sel         */  \
    R3(CQ_FUOP_MUX, P##_NONPOS, HI, P##_HISEL), /* c:409 hi                */  \
    R3(CQ_FUOP_MUX, P##_NONPOS, LO, P##_LOSEL)  /* c:410 lo                */

/* --- The enumerators. EVERY ROW REFERENCE IS A NAME AND NEVER A NUMBER. ----
 *
 * A 395-row table whose operands are integers is 900 chances at the one
 * mistake PRD-v2 §7.1 says a kernel of this shape actually makes — "the slot
 * arithmetic, not the gates" — and an off-by-one in a reference produces a
 * plausible wrong circuit rather than a crash. The enumerator order IS the
 * table order and `FA_N_ROWS` is asserted against the table's length in
 * fma.c. */
enum {
    /* Phase A — unpack, fma.jl:32-40. Twelve views, zero gates.          */
    FA_SA = 0, FA_EA_SH, FA_EA, FA_FA,
    FA_SB, FA_EB_SH, FA_EB, FA_FB,
    FA_SC, FA_EC_SH, FA_EC, FA_FC,
    /* Phase B — the nine class predicates, :43-51, through M31's block.  */
    FA_ANAN, FA_BNAN, FA_CNAN, FA_AINF, FA_BINF, FA_CINF,
    FA_AZERO, FA_BZERO, FA_CZERO,
    /* Phase C — the derived flags, :53-58.                              */
    FA_SIGNPROD,
    FA_ANY1, FA_ANYNAN,
    FA_PI_OR, FA_PZ_OR, FA_PZ_NOT, FA_PRODINF,
    FA_ITZ1, FA_ITZ2, FA_INFTIMESZERO,
    FA_IC1, FA_IC_NE, FA_INFCLASH,
    FA_PRODZERO,
    /* Phase D — the implicit bit, the effective exponents and the three
     * pre-normalisations, :61-69.                                       */
    FA_EANZ, FA_FAIMPL, FA_MA,
    FA_EBNZ, FA_FBIMPL, FA_MB,
    FA_ECNZ, FA_FCIMPL, FA_MC,
    FA_EANZ2, FA_EAEFF, FA_EBNZ2, FA_EBEFF, FA_ECNZ2, FA_ECEFF,
    FA_N52A, FA_MA1, FA_EAEFF1,
    FA_N52B, FA_MB1, FA_EBEFF1,
    FA_N52C, FA_MC1, FA_ECEFF1,
    /* Phase E — Berkeley's scaling, :72-74. Three views.                */
    FA_MAS, FA_MBS, FA_MCS,
    /* Phase F — `_sf_widemul_u64_to_128`, :77 / c:265-292.              */
    FA_WM_ALO, FA_WM_AHI, FA_WM_BLO, FA_WM_BHI,
    FA_WM_PPLL, FA_WM_PPLH, FA_WM_PPHL, FA_WM_PPHH,
    FA_WM_MID, FA_WM_MCGE, FA_WM_MCLT, FA_WM_MCARRY,
    FA_WM_MIDLO, FA_WM_MIDHIA, FA_WM_MCSH, FA_WM_MIDHI,
    FA_WM_LO, FA_WM_LCGE, FA_WM_LCLT, FA_WM_LCARRY,
    FA_WM_HI1, FA_WM_HI,
    /* Phase G — the exponent sum, :79.                                  */
    FA_ESUM, FA_EXPZ,
    /* Phase H — Berkeley line 122's unconditional doubling, :83-87.     */
    FA_PLL_GE, FA_PLL,
    FA_SL_HI1, FA_SL_LO63, FA_SL_HI, FA_SL_LO,
    FA_PHI, FA_PLO, FA_EXPZ1S, FA_EXPZ1,
    /* Phase I — the sign and exponent comparison, :90-92.               */
    FA_SS_NE, FA_SAMESIGN, FA_EXPDIFF, FA_EDN_GE, FA_EXPDIFFNEG,
    /* Phase J — the PRODUCT side of the alignment, :99-106.             */
    FA_NEGED,
    SRJ_E(FA_PRJ),
    SHRJ_E(FA_PS1),
    FA_USS_NE, FA_USS_EQ, FA_USS_NSS, FA_USESHORT,
    FA_PRIGHT_HI, FA_PRIGHT_LO, FA_PSIDE_HI, FA_PSIDE_LO,
    /* Phase K — the C side of the alignment and the exponent frame,
     * :110-115.                                                         */
    SRJ_E(FA_CRJ),
    FA_CSIDE_HI, FA_CSIDE_LO, FA_EXPR,
    /* ===== PRD-v2 §5's PRODUCT-AND-ALIGN <-> SINGLE-ROUNDING seam, at
     * fma.jl:115/:118. Everything above builds the two aligned 128-bit
     * addends; everything below adds them ONCE and rounds ONCE. What
     * crosses is exactly FA_PSIDE_HI/LO, FA_CSIDE_HI/LO, FA_EXPR,
     * FA_SAMESIGN, FA_SIGNPROD, the nine class flags, FA_PRODINF,
     * FA_INFCLASH, FA_INFTIMESZERO, FA_PRODZERO, FA_ANYNAN and the three
     * rails — pinned by a case, not by a file. ======================== */
    /* Phase L — `_add128` and `_sub128`, :118-121.                      */
    FA_AD_LO, FA_AD_CGE, FA_AD_CLT, FA_AD_CARRY, FA_AD_HI1, FA_AD_HI,
    FA_SB_LO, FA_SB_BGE, FA_SB_BLT, FA_SB_BOR, FA_SB_HI1, FA_SB_HI,
    FA_WRHI0, FA_WRLO0,
    /* Phase M — the opposite-sign underflow and `_neg128`, :125-134.    */
    FA_UF_NSS, FA_UF_SH, FA_UF_NE, FA_UNDERFLOW,
    FA_NG_NLO, FA_NG_LO, FA_NG_CNE, FA_NG_CEQ, FA_NG_CARRY,
    FA_NG_NHI, FA_NG_HI,
    FA_WRHI1, FA_WRLO1, FA_RESULTSIGN,
    /* Phase N — complete cancellation, :137.                            */
    FA_CC_NSS, FA_CC_HNE, FA_CC_HEQ, FA_CC_A, FA_CC_LNE, FA_CC_LEQ,
    FA_COMPLETECANCEL,
    /* Phase O — renormalisation stages A, B and C, :146-165.            */
    FA_HZ_NE, FA_HIZERO, FA_WRHIF0, FA_WRLOF0, FA_EXPR_M64, FA_EXPR0,
    FA_B63_SH, FA_HIBIT63,
    SHRJ_E(FA_S63),
    FA_WRHIF1, FA_WRLOF1, FA_EXPR_P1A, FA_EXPR1,
    FA_B62_SH, FA_HIBIT62,
    SHRJ_E(FA_S62),
    FA_WRHIF2, FA_WRLOF2, FA_EXPR_P1B, FA_EXPR2,
    /* Phase P — stage D, the 128-bit CLZ, :172-175 / c:433-465.         */
    FA_BZ_HNE, FA_BZ_HEQ, FA_BZ_LNE, FA_BZ_LEQ, FA_BOTHZERO,
    FA_CLZHI_IN, FA_CLZLO_IN,
    CLZ_E(FA_Z32), CLZ_E(FA_Z16), CLZ_E(FA_Z8),
    CLZ_E(FA_Z4), CLZ_E(FA_Z2), CLZ_E(FA_Z1),
    /* Phase Q — the collapse to the 56-bit working format, :181-184.    */
    FA_LOW6, FA_LOW6NZ, FA_LONZ, FA_STK_OR, FA_STICKY,
    FA_WR6, FA_WR56M, FA_WR56,
    /* Phase R — the two M32 hand-offs, :187-190.                        */
    FA_SUBN, FA_SUBN_WR, FA_SUBN_EXP, FA_SUBN_FLUSHED, FA_SUBN_FLAG,
    FA_SUBN_FTZ,
    FA_RND, FA_RND_NORMAL, FA_RND_OVF, FA_RND_EXPOVF, FA_RND_EXPOVFA,
    /* Phase S — the zero-product result, :195-196.                      */
    FA_PZC_NE, FA_PZC_EQ, FA_PZCZ, FA_PZRES,
    /* Phase T — the final priority select chain, :199-208.              */
    FA_RES0, FA_OVF_OR, FA_RES1, FA_FLUSH_AND, FA_RES2, FA_RES3, FA_RES4,
    FA_CI_NPI, FA_CI_AND, FA_RES5,
    FA_PI_SH, FA_PI_RES, FA_RES6, FA_RES7, FA_RES8,
    FA_NAN_AQ, FA_NAN_BQ, FA_NAN_CQ, FA_NAN_IN, FA_NANRES,
    FA_RESULT,
    FA_N_ROWS
};

/* --- The table. ----------------------------------------------------------
 *
 * `==` IS AN `eq` ROW PLUS A `lower_not1!` ROW AND `<` IS A `ult`/`slt` ROW
 * PLUS ONE, because the raw wires are the NEGATIONS. `lower_eq!` ends
 * `CNOT(or[W-1], r); NOT(r)` with that trailing NOT folded into the copy-out
 * (K09.md §5 delta 2), so `cq_eq_flag` is `a != b`; `cq_ult_block`'s
 * `carry[W]` is `a >=u b` and `cq_slt_block`'s is `a >=s b`. Upstream's own
 * `lower_not1!` (arith.jl:474-478) is what restores the polarity — two slots
 * and a fresh wire, not an X in place — so `!=` and `>=` rows cost one row and
 * `==`, `<`, `>` and `<=` rows cost two.
 *
 * `a <= b` IS ONE ROW AND NOT TWO, and that is `lower_icmp!`'s own table.
 * `:sle` SWAPS its operands and hands the result straight back
 * (arith.jl:417's `lower_slt!(gates, wa, b, a, W)` under a `lower_not1!` that
 * pairs with the raw wire's inversion), so `dist <= 0` is `slt(0, dist)`'s RAW
 * flag. The four predicates that swap and the five that invert are not the
 * same set — K9's recorded trap, and the reason every such row below names
 * which it is.
 *
 * FIFTEEN SIGNED COMPARES, AND EVERY ONE IS ON A GENUINELY NEGATIVE `Int64`.
 * Ten are the two `_shiftRightJam128` clamps, four are `c:405`/`c:406`'s range
 * tests and one is `:92`'s `expDiff < 0`. Substituting `cq_ult_block` for any
 * of them keeps the value right on the positive half of the input space and
 * wrong on the other, emits a smaller gate tuple, keeps the palindrome and
 * leaves scratch clean. */
static const cq_fma_row PROGRAM[FA_N_ROWS] = {
/* --- Phase A — unpack, :32-40. Twelve views, zero gates. ------------------ */
    RV(CQ_FU_A, 63, ALL),                            /* :32  sa            */
    RV(CQ_FU_A, 52, ALL),                            /* :33  a >> 52       */
    RV(FA_EA_SH, 0, CQ_FP64_EXP_ALL),                /* :33  & 0x7FF -> ea */
    RV(CQ_FU_A, 0, CQ_FP64_FRAC_MASK),               /* :34  fa            */
    RV(CQ_FU_B, 63, ALL),                            /* :35  sb            */
    RV(CQ_FU_B, 52, ALL),                            /* :36  b >> 52       */
    RV(FA_EB_SH, 0, CQ_FP64_EXP_ALL),                /* :36  -> eb         */
    RV(CQ_FU_B, 0, CQ_FP64_FRAC_MASK),               /* :37  fb            */
    RV(CQ_FU_C, 63, ALL),                            /* :38  sc            */
    RV(CQ_FU_C, 52, ALL),                            /* :39  c >> 52       */
    RV(FA_EC_SH, 0, CQ_FP64_EXP_ALL),                /* :39  -> ec         */
    RV(CQ_FU_C, 0, CQ_FP64_FRAC_MASK),               /* :40  fc            */
/* --- Phase B — the nine class predicates, through M31's own block. These
 * nine lines are `fmul.jl:31-36` with a third operand, and they are exactly
 * M31's `is_nan`/`is_inf`/`is_zero` (K22.md), so K20 COMPOSES rather than
 * re-transcribes. Same slots, same bits, same gates; what it buys is one
 * source of truth for the predicate (fma.h's note). ----------------------- */
    R2(CQ_FUOP_CLASS, CQ_FU_A, CQ_FP_IS_NAN),        /* :43  a_nan         */
    R2(CQ_FUOP_CLASS, CQ_FU_B, CQ_FP_IS_NAN),        /* :44  b_nan         */
    R2(CQ_FUOP_CLASS, CQ_FU_C, CQ_FP_IS_NAN),        /* :45  c_nan         */
    R2(CQ_FUOP_CLASS, CQ_FU_A, CQ_FP_IS_INF),        /* :46  a_inf         */
    R2(CQ_FUOP_CLASS, CQ_FU_B, CQ_FP_IS_INF),        /* :47  b_inf         */
    R2(CQ_FUOP_CLASS, CQ_FU_C, CQ_FP_IS_INF),        /* :48  c_inf         */
    R2(CQ_FUOP_CLASS, CQ_FU_A, CQ_FP_IS_ZERO),       /* :49  a_zero        */
    R2(CQ_FUOP_CLASS, CQ_FU_B, CQ_FP_IS_ZERO),       /* :50  b_zero        */
    R2(CQ_FUOP_CLASS, CQ_FU_C, CQ_FP_IS_ZERO),       /* :51  c_zero        */
/* --- Phase C — the derived flags. `sa` and `sb` are one-lane views, so 63 of
 * :53's xor lanes are a ZERO^ZERO the fold table deletes outright; the SLOT
 * count is still the whole 2W (§7.2's no-narrowing, in one row). ---------- */
    R2(CQ_FUOP_XOR, FA_SA, FA_SB),                   /* :53  sign_prod     */
    R2(CQ_FUOP_OR1, FA_ANAN, FA_BNAN),               /* :54  | (1 of 2)    */
    R2(CQ_FUOP_OR1, FA_ANY1, FA_CNAN),               /* :54  any_nan       */
    R2(CQ_FUOP_OR1, FA_AINF, FA_BINF),               /* :55  a_inf|b_inf   */
    R2(CQ_FUOP_OR1, FA_AZERO, FA_BZERO),             /* :55  a_zr|b_zr     */
    R1(CQ_FUOP_NOT1, FA_PZ_OR),                      /* :55  !(...)        */
    R2(CQ_FUOP_AND1, FA_PI_OR, FA_PZ_NOT),           /* :55  prod_is_inf   */
    R2(CQ_FUOP_AND1, FA_AINF, FA_BZERO),             /* :56  a_inf&b_zero  */
    R2(CQ_FUOP_AND1, FA_BINF, FA_AZERO),             /* :56  b_inf&a_zero  */
    R2(CQ_FUOP_OR1, FA_ITZ1, FA_ITZ2),               /* :56  inf_times_0   */
    R2(CQ_FUOP_AND1, FA_PRODINF, FA_CINF),           /* :57  & (1 of 2)    */
    R2(CQ_FUOP_EQ, FA_SIGNPROD, FA_SC),              /* :57  sign_prod!=sc */
    R2(CQ_FUOP_AND1, FA_IC1, FA_IC_NE),              /* :57  inf_clash     */
    R2(CQ_FUOP_OR1, FA_AZERO, FA_BZERO),             /* :58  prod_is_zero  */
/* --- Phase D — the implicit bit, the effective exponents and the three
 * pre-normalisations. The three `Int64(...)` casts at :64-66 ride on the mux
 * rows: a reinterpret is wiring and is NOT a row (M34's reading, which M35 and
 * M40 follow). `_sf_normalize_to_bit52` is M32's block and `m == 0` returns
 * `(0, e)` unchanged, post-Bennett-tpg0. ---------------------------------- */
    R2(CQ_FUOP_EQ,  FA_EA, CQ_FU_K_ZERO),            /* :61  ea != 0       */
    R2(CQ_FUOP_OR,  FA_FA, CQ_FU_K_IMPLICIT),        /* :61  fa | IMPLICIT */
    R3(CQ_FUOP_MUX, FA_EANZ, FA_FAIMPL, FA_FA),      /* :61  ma            */
    R2(CQ_FUOP_EQ,  FA_EB, CQ_FU_K_ZERO),            /* :62  eb != 0       */
    R2(CQ_FUOP_OR,  FA_FB, CQ_FU_K_IMPLICIT),        /* :62  fb | IMPLICIT */
    R3(CQ_FUOP_MUX, FA_EBNZ, FA_FBIMPL, FA_FB),      /* :62  mb            */
    R2(CQ_FUOP_EQ,  FA_EC, CQ_FU_K_ZERO),            /* :63  ec != 0       */
    R2(CQ_FUOP_OR,  FA_FC, CQ_FU_K_IMPLICIT),        /* :63  fc | IMPLICIT */
    R3(CQ_FUOP_MUX, FA_ECNZ, FA_FCIMPL, FA_FC),      /* :63  mc            */
    R2(CQ_FUOP_EQ,  FA_EA, CQ_FU_K_ZERO),            /* :64  ea != 0 (2nd) */
    R3(CQ_FUOP_MUX, FA_EANZ2, FA_EA, CQ_FU_K_ONE),   /* :64  ea_eff        */
    R2(CQ_FUOP_EQ,  FA_EB, CQ_FU_K_ZERO),            /* :65  eb != 0 (2nd) */
    R3(CQ_FUOP_MUX, FA_EBNZ2, FA_EB, CQ_FU_K_ONE),   /* :65  eb_eff        */
    R2(CQ_FUOP_EQ,  FA_EC, CQ_FU_K_ZERO),            /* :66  ec != 0 (2nd) */
    R3(CQ_FUOP_MUX, FA_ECNZ2, FA_EC, CQ_FU_K_ONE),   /* :66  ec_eff        */
    R2(CQ_FUOP_NORM52, FA_MA, FA_EAEFF),             /* :67                */
    RO(FA_N52A, CQ_FU_OUT_M),                        /* :67  ma            */
    RO(FA_N52A, CQ_FU_OUT_E),                        /* :67  ea_eff        */
    R2(CQ_FUOP_NORM52, FA_MB, FA_EBEFF),             /* :68                */
    RO(FA_N52B, CQ_FU_OUT_M),                        /* :68  mb            */
    RO(FA_N52B, CQ_FU_OUT_E),                        /* :68  eb_eff        */
    R2(CQ_FUOP_NORM52, FA_MC, FA_ECEFF),             /* :69                */
    RO(FA_N52C, CQ_FU_OUT_M),                        /* :69  mc            */
    RO(FA_N52C, CQ_FU_OUT_E),                        /* :69  ec_eff        */
/* --- Phase E — Berkeley's scaling. `ma,mb << 10` puts each leading 1 at bit
 * 62 so the 128-bit product's lands at bit 124 or 125; `mc << 9` matches the
 * frame (:71's own comment). Three views, zero gates. --------------------- */
    RV(FA_MA1, -10, ALL),                            /* :72  ma_s          */
    RV(FA_MB1, -10, ALL),                            /* :73  mb_s          */
    RV(FA_MC1, -9, ALL),                             /* :74  mc_s          */
/* --- Phase F — `_sf_widemul_u64_to_128`, inlined (c:265-292). THE 32/32
 * SPLIT IS NOT `fmul`'s 27/26 and the difference is upstream's own: c:261-263
 * says the 27/26 decomposition "assumes <= 53-bit inputs", and Berkeley
 * scaling makes these 63. K11.md §7.5's `n_a * n_b` Toffoli identity is scoped
 * to `n_a + n_b < W` and 32 + 32 sits exactly at the bound, so
 * `multiplier.jl:26`'s truncation DOES drop products here — measure the
 * Toffolis, do not copy that table. ---------------------------------------- */
    RV(FA_MAS, 0, UINT64_C(0xFFFFFFFF)),             /* c:266 a_lo         */
    RV(FA_MAS, 32, ALL),                             /* c:267 a_hi         */
    RV(FA_MBS, 0, UINT64_C(0xFFFFFFFF)),             /* c:268 b_lo         */
    RV(FA_MBS, 32, ALL),                             /* c:269 b_hi         */
    R2(CQ_FUOP_MUL, FA_WM_ALO, FA_WM_BLO),           /* c:271 pp_ll        */
    R2(CQ_FUOP_MUL, FA_WM_ALO, FA_WM_BHI),           /* c:272 pp_lh        */
    R2(CQ_FUOP_MUL, FA_WM_AHI, FA_WM_BLO),           /* c:273 pp_hl        */
    R2(CQ_FUOP_MUL, FA_WM_AHI, FA_WM_BHI),           /* c:274 pp_hh        */
    R2(CQ_FUOP_ADD, FA_WM_PPLH, FA_WM_PPHL),         /* c:281 mid          */
    R2(CQ_FUOP_ULT, FA_WM_MID, FA_WM_PPLH),          /* c:282 mid >=u pplh */
    R1(CQ_FUOP_NOT1, FA_WM_MCGE),                    /* c:282 mid <  pplh  */
    R3(CQ_FUOP_MUX, FA_WM_MCLT, CQ_FU_K_ONE, CQ_FU_K_ZERO), /* c:282 carry */
    RV(FA_WM_MID, -32, ALL),                         /* c:284 mid_lo       */
    RV(FA_WM_MID, 32, ALL),                          /* c:285 mid >> 32    */
    RV(FA_WM_MCARRY, -32, ALL),                      /* c:285 carry << 32  */
    R2(CQ_FUOP_OR, FA_WM_MIDHIA, FA_WM_MCSH),        /* c:285 mid_hi       */
    R2(CQ_FUOP_ADD, FA_WM_PPLL, FA_WM_MIDLO),        /* c:287 lo           */
    R2(CQ_FUOP_ULT, FA_WM_LO, FA_WM_PPLL),           /* c:288 lo >=u pp_ll */
    R1(CQ_FUOP_NOT1, FA_WM_LCGE),                    /* c:288 lo <  pp_ll  */
    R3(CQ_FUOP_MUX, FA_WM_LCLT, CQ_FU_K_ONE, CQ_FU_K_ZERO), /* c:288 carry */
    R2(CQ_FUOP_ADD, FA_WM_PPHH, FA_WM_MIDHI),        /* c:290 + (1 of 2)   */
    R2(CQ_FUOP_ADD, FA_WM_HI1, FA_WM_LCARRY),        /* c:290 hi           */
/* --- Phase G — the exponent sum. `Int64(0x3FE)` is spelled as the source
 * spells it and NOT as `BIAS - 1`: `BIAS` is bound at :29 and never read. -- */
    R2(CQ_FUOP_ADD, FA_EAEFF1, FA_EBEFF1),           /* :79  ea_eff+eb_eff */
    R2(CQ_FUOP_SUB, FA_ESUM, CQ_FU_K_3FE),           /* :79  expZ          */
/* --- Phase H — Berkeley line 122's unconditional doubling, :83-87. The
 * doubled pair is computed for EVERY input and thrown away when the leading 1
 * is already at bit 125, which is §7.6's discarded-arm rule: DISCARDED is a
 * claim about the RESULT, never about the circuit. --------------------- */
    R2(CQ_FUOP_ULT, FA_WM_HI, CQ_FU_K_2P61),         /* :83  p_hi >=u 2^61 */
    R1(CQ_FUOP_NOT1, FA_PLL_GE),                     /* :83  prod_lead_low */
    RV(FA_WM_HI, -1, ALL),                           /* c:337 a_hi << 1    */
    RV(FA_WM_LO, 63, ALL),                           /* c:337 a_lo >> 63   */
    R2(CQ_FUOP_OR, FA_SL_HI1, FA_SL_LO63),           /* c:337 hi           */
    RV(FA_WM_LO, -1, ALL),                           /* c:338 lo           */
    R3(CQ_FUOP_MUX, FA_PLL, FA_SL_HI, FA_WM_HI),     /* :85  p_hi          */
    R3(CQ_FUOP_MUX, FA_PLL, FA_SL_LO, FA_WM_LO),     /* :86  p_lo          */
    R2(CQ_FUOP_SUB, FA_EXPZ, CQ_FU_K_ONE),           /* :87  expZ - 1      */
    R3(CQ_FUOP_MUX, FA_PLL, FA_EXPZ1S, FA_EXPZ),     /* :87  expZ          */
/* --- Phase I — the sign and exponent comparison. `:92`'s `expDiff < 0` is the
 * one signed compare outside `_shiftRightJam128`, and it is the row the whole
 * alignment turns on. ----------------------------------------------------- */
    R2(CQ_FUOP_EQ, FA_SIGNPROD, FA_SC),              /* :90  != (raw)      */
    R1(CQ_FUOP_NOT1, FA_SS_NE),                      /* :90  same_sign     */
    R2(CQ_FUOP_SUB, FA_EXPZ1, FA_ECEFF1),            /* :91  expDiff       */
    R2(CQ_FUOP_SLT, FA_EXPDIFF, CQ_FU_K_ZERO),       /* :92  >=s 0 (raw)   */
    R1(CQ_FUOP_NOT1, FA_EDN_GE),                     /* :92  expDiff_neg   */
/* --- Phase J — the PRODUCT side of the alignment, :99-106. `-expDiff` is
 * LLVM's `sub i64 0, %x`, which is `lower_sub!` with a constant zero on the
 * left — the same reading that makes `~x` an xor with all-ones (M37's, which
 * this follows). Both the right-jam and the Berkeley one-bit special are
 * computed unconditionally and one is thrown away. ------------------------ */
    R2(CQ_FUOP_SUB, CQ_FU_K_ZERO, FA_EXPDIFF),       /* :99  -expDiff      */
    SRJ_R(FA_PRJ, FA_PHI, FA_PLO, FA_NEGED),         /* :99                */
    SHRJ_R(FA_PS1, FA_PHI, FA_PLO),                  /* :100               */
    R2(CQ_FUOP_EQ, FA_EXPDIFF, CQ_FU_K_ONES),        /* :101 != -1 (raw)   */
    R1(CQ_FUOP_NOT1, FA_USS_NE),                     /* :101 == -1         */
    R1(CQ_FUOP_NOT1, FA_SAMESIGN),                   /* :101 !same_sign    */
    R2(CQ_FUOP_AND1, FA_USS_EQ, FA_USS_NSS),         /* :101 use_short     */
    R3(CQ_FUOP_MUX, FA_USESHORT, FA_PS1_HI, FA_PRJ_HI), /* :102 p_right_hi */
    R3(CQ_FUOP_MUX, FA_USESHORT, FA_PS1_LO, FA_PRJ_LO), /* :103 p_right_lo */
    R3(CQ_FUOP_MUX, FA_EXPDIFFNEG, FA_PRIGHT_HI, FA_PHI), /* :105 p_side_hi */
    R3(CQ_FUOP_MUX, FA_EXPDIFFNEG, FA_PRIGHT_LO, FA_PLO), /* :106 p_side_lo */
/* --- Phase K — the C side, :110-115. The low limb handed in is the literal
 * `UInt64(0)`, so a third of this expansion folds at every operand mask —
 * which is why the composition identity is over SLOTS and never gates. ---- */
    SRJ_R(FA_CRJ, FA_MCS, CQ_FU_K_ZERO, FA_EXPDIFF), /* :110               */
    R3(CQ_FUOP_MUX, FA_EXPDIFFNEG, FA_MCS, FA_CRJ_HI),   /* :111 c_side_hi */
    R3(CQ_FUOP_MUX, FA_EXPDIFFNEG, CQ_FU_K_ZERO, FA_CRJ_LO), /* :112 c_s_lo */
    R3(CQ_FUOP_MUX, FA_EXPDIFFNEG, FA_ECEFF1, FA_EXPZ1), /* :115 expR      */
/* ===== fma.jl:115 / :118 — PRD-v2 §5's recorded seam. See fma.h. ========= */
/* --- Phase L — `_add128` (c:299-304) and `_sub128` (c:311-316), BOTH
 * computed and one selected at :120-121. THE CARRY IS A VALUE, NOT A WIRE:
 * `cq_add_block` takes no carry-in and exposes no carry-out, and upstream's
 * own 128-bit add materialises the carry through a compare and a mux for
 * exactly that reason. Building a carry-chained 128-bit adder here would be a
 * re-derivation (K20's third finding). ------------------------------------ */
    R2(CQ_FUOP_ADD, FA_PSIDE_LO, FA_CSIDE_LO),       /* c:300 lo           */
    R2(CQ_FUOP_ULT, FA_AD_LO, FA_PSIDE_LO),          /* c:301 >=u (raw)    */
    R1(CQ_FUOP_NOT1, FA_AD_CGE),                     /* c:301 lo <  a_lo   */
    R3(CQ_FUOP_MUX, FA_AD_CLT, CQ_FU_K_ONE, CQ_FU_K_ZERO), /* c:301 carry  */
    R2(CQ_FUOP_ADD, FA_PSIDE_HI, FA_CSIDE_HI),       /* c:302 + (1 of 2)   */
    R2(CQ_FUOP_ADD, FA_AD_HI1, FA_AD_CARRY),         /* c:302 hi           */
    R2(CQ_FUOP_SUB, FA_PSIDE_LO, FA_CSIDE_LO),       /* c:312 lo           */
    R2(CQ_FUOP_ULT, FA_PSIDE_LO, FA_CSIDE_LO),       /* c:313 >=u (raw)    */
    R1(CQ_FUOP_NOT1, FA_SB_BGE),                     /* c:313 a_lo < b_lo  */
    R3(CQ_FUOP_MUX, FA_SB_BLT, CQ_FU_K_ONE, CQ_FU_K_ZERO), /* c:313 borrow */
    R2(CQ_FUOP_SUB, FA_PSIDE_HI, FA_CSIDE_HI),       /* c:314 - (1 of 2)   */
    R2(CQ_FUOP_SUB, FA_SB_HI1, FA_SB_BOR),           /* c:314 hi           */
    R3(CQ_FUOP_MUX, FA_SAMESIGN, FA_AD_HI, FA_SB_HI),/* :120 wr_hi         */
    R3(CQ_FUOP_MUX, FA_SAMESIGN, FA_AD_LO, FA_SB_LO),/* :121 wr_lo         */
/* --- Phase M — the opposite-sign underflow and `_neg128` (c:324-329). `~x`
 * is `lower_xor!` with an all-ones constant: there is no full-width
 * `lower_not!` in arith.jl, only `lower_not1!` at :474-478, and LLVM spells
 * `~a` as `xor i64 %a, -1` (M37's reading, followed). -------------------- */
    R1(CQ_FUOP_NOT1, FA_SAMESIGN),                   /* :125 !same_sign    */
    RV(FA_WRHI0, 63, ALL),                           /* :125 wr_hi >> 63   */
    R2(CQ_FUOP_EQ, FA_UF_SH, CQ_FU_K_ZERO),          /* :125 != 0          */
    R2(CQ_FUOP_AND1, FA_UF_NSS, FA_UF_NE),           /* :125 underflow     */
    R2(CQ_FUOP_XOR, FA_WRLO0, CQ_FU_K_ONES),         /* c:325 ~a_lo        */
    R2(CQ_FUOP_ADD, FA_NG_NLO, CQ_FU_K_ONE),         /* c:325 lo           */
    R2(CQ_FUOP_EQ, FA_NG_LO, CQ_FU_K_ZERO),          /* c:326 != 0 (raw)   */
    R1(CQ_FUOP_NOT1, FA_NG_CNE),                     /* c:326 lo == 0      */
    R3(CQ_FUOP_MUX, FA_NG_CEQ, CQ_FU_K_ONE, CQ_FU_K_ZERO), /* c:326 carry  */
    R2(CQ_FUOP_XOR, FA_WRHI0, CQ_FU_K_ONES),         /* c:327 ~a_hi        */
    R2(CQ_FUOP_ADD, FA_NG_NHI, FA_NG_CARRY),         /* c:327 hi           */
    R3(CQ_FUOP_MUX, FA_UNDERFLOW, FA_NG_HI, FA_WRHI0), /* :127 wr_hi       */
    R3(CQ_FUOP_MUX, FA_UNDERFLOW, FA_NG_LO, FA_WRLO0), /* :128 wr_lo       */
    R3(CQ_FUOP_MUX, FA_UNDERFLOW, FA_SC, FA_SIGNPROD), /* :134 result_sign */
/* --- Phase N — complete cancellation, :137. Left-associative, so it is
 * `(!same_sign & (wr_hi == 0)) & (wr_lo == 0)` and NOT one three-input and. - */
    R1(CQ_FUOP_NOT1, FA_SAMESIGN),                   /* :137 !same_sign    */
    R2(CQ_FUOP_EQ, FA_WRHI1, CQ_FU_K_ZERO),          /* :137 != 0 (raw)    */
    R1(CQ_FUOP_NOT1, FA_CC_HNE),                     /* :137 wr_hi == 0    */
    R2(CQ_FUOP_AND1, FA_CC_NSS, FA_CC_HEQ),          /* :137 & (1 of 2)    */
    R2(CQ_FUOP_EQ, FA_WRLO1, CQ_FU_K_ZERO),          /* :137 != 0 (raw)    */
    R1(CQ_FUOP_NOT1, FA_CC_LNE),                     /* :137 wr_lo == 0    */
    R2(CQ_FUOP_AND1, FA_CC_A, FA_CC_LEQ),            /* :137 complete_canc */
/* --- Phase O — renormalisation stages A, B and C, :146-165. --------------- */
    R2(CQ_FUOP_EQ, FA_WRHI1, CQ_FU_K_ZERO),          /* :146 != 0 (raw)    */
    R1(CQ_FUOP_NOT1, FA_HZ_NE),                      /* :146 hi_zero       */
    R3(CQ_FUOP_MUX, FA_HIZERO, FA_WRLO1, FA_WRHI1),  /* :147 wr_hi_folded  */
    R3(CQ_FUOP_MUX, FA_HIZERO, CQ_FU_K_ZERO, FA_WRLO1), /* :148 wr_lo_fold */
    R2(CQ_FUOP_SUB, FA_EXPR, CQ_FU_K_64),            /* :149 expR - 64     */
    R3(CQ_FUOP_MUX, FA_HIZERO, FA_EXPR_M64, FA_EXPR),/* :149 expR          */
    RV(FA_WRHIF0, 63, ALL),                          /* :153 >> 63         */
    R2(CQ_FUOP_EQ, FA_B63_SH, CQ_FU_K_ZERO),         /* :153 hi_bit63      */
    SHRJ_R(FA_S63, FA_WRHIF0, FA_WRLOF0),            /* :154               */
    R3(CQ_FUOP_MUX, FA_HIBIT63, FA_S63_HI, FA_WRHIF0), /* :155             */
    R3(CQ_FUOP_MUX, FA_HIBIT63, FA_S63_LO, FA_WRLOF0), /* :156             */
    R2(CQ_FUOP_ADD, FA_EXPR0, CQ_FU_K_ONE),          /* :157 expR + 1      */
    R3(CQ_FUOP_MUX, FA_HIBIT63, FA_EXPR_P1A, FA_EXPR0), /* :157 expR       */
    RV(FA_WRHIF1, 62, ALL),                          /* :161 >> 62         */
    R2(CQ_FUOP_EQ, FA_B62_SH, CQ_FU_K_ZERO),         /* :161 hi_bit62      */
    SHRJ_R(FA_S62, FA_WRHIF1, FA_WRLOF1),            /* :162               */
    R3(CQ_FUOP_MUX, FA_HIBIT62, FA_S62_HI, FA_WRHIF1), /* :163             */
    R3(CQ_FUOP_MUX, FA_HIBIT62, FA_S62_LO, FA_WRLOF1), /* :164             */
    R2(CQ_FUOP_ADD, FA_EXPR1, CQ_FU_K_ONE),          /* :165 expR + 1      */
    R3(CQ_FUOP_MUX, FA_HIBIT62, FA_EXPR_P1B, FA_EXPR1), /* :165 expR       */
/* --- Phase P — stage D, the 128-bit CLZ. `(1, 0)` is substituted for the
 * all-zero pair to keep the helper inside its precondition domain (:169-171);
 * `complete_cancel` overrides the garbage in the select chain. The six probe
 * masks are spelled as the source spells them so a reader checks a mask
 * against the line and not against arithmetic. --------------------------- */
    R2(CQ_FUOP_EQ, FA_WRHIF2, CQ_FU_K_ZERO),         /* :172 != 0 (raw)    */
    R1(CQ_FUOP_NOT1, FA_BZ_HNE),                     /* :172 hi == 0       */
    R2(CQ_FUOP_EQ, FA_WRLOF2, CQ_FU_K_ZERO),         /* :172 != 0 (raw)    */
    R1(CQ_FUOP_NOT1, FA_BZ_LNE),                     /* :172 lo == 0       */
    R2(CQ_FUOP_AND1, FA_BZ_HEQ, FA_BZ_LEQ),          /* :172 both_zero     */
    R3(CQ_FUOP_MUX, FA_BOTHZERO, CQ_FU_K_ONE, FA_WRHIF2),  /* :173         */
    R3(CQ_FUOP_MUX, FA_BOTHZERO, CQ_FU_K_ZERO, FA_WRLOF2), /* :174         */
    CLZ_R(FA_Z32, FA_CLZHI_IN, FA_CLZLO_IN, FA_EXPR2,
          UINT64_C(0xFFFFFFFF) << 30, 32, CQ_FU_K_32),     /* c:434-437    */
    CLZ_R(FA_Z16, FA_Z32_HI, FA_Z32_LO, FA_Z32_E,
          UINT64_C(0xFFFF) << 46, 16, CQ_FU_K_16),         /* c:439-442    */
    CLZ_R(FA_Z8, FA_Z16_HI, FA_Z16_LO, FA_Z16_E,
          UINT64_C(0xFF) << 54, 8, CQ_FU_K_8),             /* c:444-447    */
    CLZ_R(FA_Z4, FA_Z8_HI, FA_Z8_LO, FA_Z8_E,
          UINT64_C(0xF) << 58, 4, CQ_FU_K_4),              /* c:449-452    */
    CLZ_R(FA_Z2, FA_Z4_HI, FA_Z4_LO, FA_Z4_E,
          UINT64_C(0x3) << 60, 2, CQ_FU_K_2),              /* c:454-457    */
    CLZ_R(FA_Z1, FA_Z2_HI, FA_Z2_LO, FA_Z2_E,
          UINT64_C(1) << 61, 1, CQ_FU_K_ONE),              /* c:459-462    */
/* --- Phase Q — the collapse to the 56-bit working format, :181-184. What
 * crosses into M32 is an ENCODING no C type expresses (K23 §1.7): bit 55 the
 * leading 1, bits 54-3 the fraction, bits 2/1/0 guard/round/sticky. A one-bit
 * disagreement is right on every input that does not round and wrong on the
 * ties, so the tie anchors are the only detector. ------------------------- */
    RV(FA_Z1_HI, 0, UINT64_C(0x3F)),                 /* :181 & 0x3F        */
    R2(CQ_FUOP_EQ, FA_LOW6, CQ_FU_K_ZERO),           /* :181 low6_nonzero  */
    R2(CQ_FUOP_EQ, FA_Z1_LO, CQ_FU_K_ZERO),          /* :182 lo_nonzero    */
    R2(CQ_FUOP_OR1, FA_LOW6NZ, FA_LONZ),             /* :183 |             */
    R3(CQ_FUOP_MUX, FA_STK_OR, CQ_FU_K_ONE, CQ_FU_K_ZERO), /* :183 sticky  */
    RV(FA_Z1_HI, 6, ALL),                            /* :184 >> 6          */
    RV(FA_WR6, 0, (UINT64_C(1) << 56) - UINT64_C(1)),/* :184 & 56 bits     */
    R2(CQ_FUOP_OR, FA_WR56M, FA_STICKY),             /* :184 wr_56         */
/* --- Phase R — the two M32 hand-offs, :187-190. ------------------------- */
    R3(CQ_FUOP_SUBNORM, FA_WR56, FA_Z1_E, FA_RESULTSIGN), /* :187-188      */
    RO(FA_SUBN, CQ_FU_OUT_WR),                       /* :187 wr_56         */
    RO(FA_SUBN, CQ_FU_OUT_EXP),                      /* :187 expR          */
    RO(FA_SUBN, CQ_FU_OUT_FLUSHED),                  /* :187 flushed       */
    RO(FA_SUBN, CQ_FU_OUT_SUBNORMAL),                /* :187 subnormal     */
    RO(FA_SUBN, CQ_FU_OUT_FTZ),                      /* :187 flush_to_zero */
    R3(CQ_FUOP_ROUND, FA_SUBN_WR, FA_SUBN_EXP, FA_RESULTSIGN), /* :189-190 */
    RO(FA_RND, CQ_FU_OUT_NORMAL),                    /* :189 normal_result */
    RO(FA_RND, CQ_FU_OUT_OVERFLOW),                  /* :189 overflow_res  */
    RO(FA_RND, CQ_FU_OUT_EXPOVF),                    /* :189 exp_overflow  */
    RO(FA_RND, CQ_FU_OUT_EXPOVF_AFT),                /* :189 ..._after     */
/* --- Phase S — the zero-product result, :195-196. Under round-to-nearest-
 * even `fma(-0, +0, +0)` is `+0` and `fma(-0, +0, -0)` is `-0`, which is what
 * the signed-zero combine at :195 is for (:193-194). --------------------- */
    R2(CQ_FUOP_EQ, FA_SIGNPROD, FA_SC),              /* :195 != (raw, 2nd) */
    R1(CQ_FUOP_NOT1, FA_PZC_NE),                     /* :195 sign_prod==sc */
    R3(CQ_FUOP_MUX, FA_PZC_EQ, CQ_FU_C, CQ_FU_K_ZERO), /* :195             */
    R3(CQ_FUOP_MUX, FA_CZERO, FA_PZCZ, CQ_FU_C),     /* :196 prod_zero_res */
/* --- Phase T — the final priority select chain, :199-208. Priority runs
 * BOTTOM-UP: the last `ifelse` written wins, so the NaN row is outermost.
 * `INDEF` at :206-207 is NOT `result_sign`-signed — `Inf x 0` and the
 * `Inf - Inf` clash are 0xFFF8000000000000 whatever the operand signs are
 * (Bennett-r84x), and "tidying" either into `(result_sign << 63) | QNAN` is
 * wrong on exactly half its inputs.
 *
 * `_sf_propagate_nan3` (c:32-35) IS a > b > c PRECEDENCE, matching Intel
 * VFMADD*'s order — a NESTED pair of `ifelse`s, so the inner mux is written
 * first and the outer second. --------------------------------------------- */
    RV(FA_RND_NORMAL, 0, ALL),                       /* :199 result        */
    R2(CQ_FUOP_OR1, FA_RND_EXPOVF, FA_RND_EXPOVFA),  /* :200 |             */
    R3(CQ_FUOP_MUX, FA_OVF_OR, FA_RND_OVF, FA_RES0), /* :200 result        */
    R2(CQ_FUOP_AND1, FA_SUBN_FLAG, FA_SUBN_FTZ),     /* :201 &             */
    R3(CQ_FUOP_MUX, FA_FLUSH_AND, FA_SUBN_FLUSHED, FA_RES1), /* :201       */
    R3(CQ_FUOP_MUX, FA_COMPLETECANCEL, CQ_FU_K_ZERO, FA_RES2), /* :202     */
    R3(CQ_FUOP_MUX, FA_PRODZERO, FA_PZRES, FA_RES3), /* :203 result        */
    R1(CQ_FUOP_NOT1, FA_PRODINF),                    /* :204 !prod_is_inf  */
    R2(CQ_FUOP_AND1, FA_CINF, FA_CI_NPI),            /* :204 &             */
    R3(CQ_FUOP_MUX, FA_CI_AND, CQ_FU_C, FA_RES4),    /* :204 result        */
    RV(FA_SIGNPROD, -63, ALL),                       /* :205 sign_prod<<63 */
    R2(CQ_FUOP_OR, FA_PI_SH, CQ_FU_K_INF),           /* :205 | INF_BITS    */
    R3(CQ_FUOP_MUX, FA_PRODINF, FA_PI_RES, FA_RES5), /* :205 result        */
    R3(CQ_FUOP_MUX, FA_INFCLASH, CQ_FU_K_INDEF, FA_RES6),      /* :206     */
    R3(CQ_FUOP_MUX, FA_INFTIMESZERO, CQ_FU_K_INDEF, FA_RES7),  /* :207     */
    R2(CQ_FUOP_OR, CQ_FU_A, CQ_FU_K_QUIET),          /* c:33 a | QUIET_BIT */
    R2(CQ_FUOP_OR, CQ_FU_B, CQ_FU_K_QUIET),          /* c:34 b | QUIET_BIT */
    R2(CQ_FUOP_OR, CQ_FU_C, CQ_FU_K_QUIET),          /* c:35 c | QUIET_BIT */
    R3(CQ_FUOP_MUX, FA_BNAN, FA_NAN_BQ, FA_NAN_CQ),  /* c:34 inner ifelse  */
    R3(CQ_FUOP_MUX, FA_ANAN, FA_NAN_AQ, FA_NAN_IN),  /* c:33 outer ifelse  */
    R3(CQ_FUOP_MUX, FA_ANYNAN, FA_NANRES, FA_RES8)   /* :208 result        */
};

#undef RV
#undef RO
#undef R1
#undef R2
#undef R3
#undef ALL
#undef SHRJ_E
#undef SHRJ_R
#undef CLZ_E
#undef CLZ_R
#undef SRJ_E
#undef SRJ_R
