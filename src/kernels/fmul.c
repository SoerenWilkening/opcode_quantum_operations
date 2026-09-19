/* src/kernels/fmul.c — M34, K16, the ROW TABLE half.
 *
 * Read docs/constructions/K16.md and fmul.h before changing anything here.
 * THE TABLE BELOW IS THE WHOLE OF THIS FILE AND IT IS THE PORT — `soft_fmul`
 * (third_party/bennett/src/softfloat/fmul.jl:14-215) and, where it calls them,
 * `_sf_propagate_nan2` (softfloat_common.jl:23-24) inlined at :212 and the
 * four shared helpers M32 owns — ONE ROW PER OPERATOR OCCURRENCE in SOURCE
 * ORDER, with every row citing the line it transcribes. Nothing here emits a
 * gate, names a scratch span or knows what a block costs: that is fmul_step.c
 * and fmul_emit.c, on M36's ROW TABLES <-> STEP MACHINE seam (K18.md D-K18-7)
 * and M32's LAYOUT <-> DISPATCH seam (K23 D-K23-10).
 *
 * `cq_fmul_row_width` IS HERE AND NOT IN THE STEP MACHINE, and the reason is
 * the seam rather than the line count it also fixed. Which hand-off returns a
 * TUPLE and which of its members are `Bool` is read off `softfloat_common.jl`
 * (:104, :138, :190, :226) — it is Julia knowledge, and fmul_step.c's contract
 * is that not one line of it has any.
 *
 * THE LITERAL GRAIN IS WHAT A READER CHECKS HERE (PRD-v2 §7.2). One block per
 * operator occurrence as the source spells it; NO common-subexpression
 * sharing — `ea == 0x7FF` gets two whole `eq` rows because :31 and :33 each
 * write it, `a_zero | b_zero` gets three `or` rows for :209, :210 and :211,
 * and `cross << 26` gets two view rows for :94 and :127; NO narrowing towards
 * the field width, so `ea` is a 64-lane span holding eleven bits of value and
 * `c1` a 64-lane span holding one. Both refusals are D9's K12 precedent: a
 * narrowing here would be a RE-DERIVATION rather than a port, and K11's
 * finding stands that the one mutant L1 cannot see is the one that looks like
 * an optimisation.
 *
 * A VIEW ROW IS AN OPERATOR OCCURRENCE THAT COSTS NOTHING (PRD-v2 §7.3 as
 * amended 2026-09-18; M31's cq_fp_view_* are the same claim). An AND or a
 * shift by a COMPILE-TIME constant is WIRING — 35 of these 150 rows — and they
 * stay ROWS so the 1:1 correspondence with the source survives, which is the
 * thing a reader checks and the thing the slot scan walks. K16.md's draft
 * predates that amendment and counts twelve of them as `and`(64) BLOCKS; the
 * re-count is in K16.md §3 as landed.
 *
 * THE SEVEN `(UInt64(1) << k) - UInt64(1)` MASKS ARE CONSTANT-FOLDED AND ARE
 * NOT A SHIFT ROW AND A SUB ROW. `fmul.jl:174`, `:175`, `:180`, `:181` and
 * `:191` each spell a compile-time constant as an expression, with two literal
 * operands, so there is no operator occurrence left to transcribe. They are
 * spelled below exactly as the source spells them rather than as precomputed
 * hex, so a reader checks a mask against the line and not against arithmetic.
 * Transcribing them as live `shl` + `sub` blocks would add five barrel rows to
 * a kernel PRD-v2 §7.6 audits as having ZERO variable shifts.
 *
 * `fmul.jl:94-102` IS DEAD AND IS TRANSCRIBED ANYWAY — the seven `FR_Q_*` rows
 * below. The source abandons the assembly mid-way and says so in its own
 * words (*"Let's redo properly"* :105, *"Restart assembly more carefully"*
 * :107, *"Let's just do it with add-with-carry"* :120), and nothing reads
 * `carry_lo`, `cross_hi` or `pp_hh_shifted`. PRD-v2 §7.6's discarded-arm rule
 * is why they are here: *"A DISCARDED ARM IS STILL EMITTED … DISCARDED is a
 * claim about the RESULT, never about the circuit"*, and PRD-v2 §5's M34 row
 * decides it for K16 by name. Deleting them is a re-derivation; so is keeping
 * them silently. They are emitted, uncomputed by the sandwich reverse, and
 * never selected.
 *
 * `SIGN_MASK` (:15) IS ASSIGNED AND NEVER READ, so it is not a row and not a
 * constant code — every sign operation in the body is spelled `>> 63` or
 * `<< 63`. `BIAS` (:16) IS read, once, at :63. K18 records the identical shape
 * for `fcmp.jl:9` (K16.md §1.4 point 1).
 */

#include "kernels/fmul.h"

#include "kernels/fpclass.h"
#include "kernels/kernel.h"

/* One row per occurrence, in the five shapes the ops need. `mask` and `shift`
 * are read only by a VIEW; `s1`/`s2` only up to `cq_fmul_arity`. */
#define RV(s0, sh, mk)                                                         \
    { (short)CQ_FMOP_VIEW, (short)(s0), 0, 0, (short)(sh), (mk) }
#define RO(s0, which)                                                          \
    { (short)CQ_FMOP_OUT, (short)(s0), (short)(which), 0, 0, 0 }
#define R1(op, s0)                                                             \
    { (short)(op), (short)(s0), 0, 0, 0, 0 }
#define R2(op, s0, s1)                                                         \
    { (short)(op), (short)(s0), (short)(s1), 0, 0, 0 }
#define R3(op, s0, s1, s2)                                                     \
    { (short)(op), (short)(s0), (short)(s1), (short)(s2), 0, 0 }

#define ALL ~UINT64_C(0)

/* EVERY ROW REFERENCE IS A NAME AND NEVER A NUMBER. A 150-row table whose
 * operands are integers is 450 chances at the one mistake PRD-v2 §7.1 says a
 * kernel of this shape actually makes — "the slot arithmetic, not the gates" —
 * and an off-by-one in a reference produces a plausible wrong circuit rather
 * than a crash. The enumerator order IS the table order and `FR_N_ROWS` is
 * asserted against the table's length below. */
enum {
    /* Phase A — unpack, fmul.jl:19-25. Eight views, zero gates.           */
    FR_SA = 0, FR_EA_SH, FR_EA, FR_FA, FR_SB, FR_EB_SH, FR_EB, FR_FB,
    /* Phase B — the result sign, :28.                                     */
    FR_SIGN,
    /* Phase C — the six class predicates, :31-36, through M31's block.    */
    FR_ANAN, FR_BNAN, FR_AINF, FR_BINF, FR_AZERO, FR_BZERO,
    /* Phase D — the special-case results, :40-41.                         */
    FR_SIGN63, FR_INFRES, FR_ZERORES,
    /* Phase E — the implicit bit and the effective exponents, :44-49.     */
    FR_EANZ1, FR_FAIMPL, FR_MA, FR_EBNZ1, FR_FBIMPL, FR_MB,
    FR_EANZ2, FR_EAEFF, FR_EBNZ2, FR_EBEFF,
    /* Phase F — the two pre-normalisations, :57-58.                       */
    FR_N52A, FR_MA1, FR_EAEFF1, FR_N52B, FR_MB1, FR_EBEFF1,
    /* Phase G — the exponent sum, :63.                                    */
    FR_ESUM, FR_REXP,
    /* Phase H — the 27/26 split, :73-76.                                  */
    FR_ALO, FR_AHI, FR_BLO, FR_BHI,
    /* Phase P — the four significand products, :78-81.                    */
    FR_PPLL, FR_PPLH, FR_PPHL, FR_PPHH,
    /* Phase I — the cross-term sum, :91.                                  */
    FR_CROSS,
    /* Phase Q — the ABANDONED assembly, :94-102. EVERY ROW IS DEAD.       */
    FR_Q_CROSSLO, FR_Q_PRODLO, FR_Q_GE, FR_Q_LT, FR_Q_CARRYLO,
    FR_Q_CROSSHI, FR_Q_PPHHSH,
    /* Phase R — the 106-bit assembly, add-with-carry, :126-136.           */
    FR_ACCLO, FR_TERM2, FR_SUM1, FR_C1_GE, FR_C1_LT, FR_C1,
    FR_TERM3, FR_SUM2, FR_C2_GE, FR_C2_LT, FR_C2,
    FR_PLO, FR_CROSSHI, FR_PPHH12, FR_T1, FR_T2, FR_PHI,
    /* ===== PRD-v2 §5's SIGNIFICAND PRODUCT <-> EXPONENT/PACK seam, at
     * fmul.jl:136. Everything above computes (prod_hi_final, prod_lo_final);
     * everything below turns it into a packed f64. What crosses is exactly
     * FR_PHI, FR_PLO, FR_REXP, FR_SIGN, the six class flags, FR_INFRES,
     * FR_ZERORES and the two rails — pinned by a case, not by a file. ===== */
    /* Phase S — the MSB detect and the two extractions, :156-191.         */
    FR_HI41, FR_MSB, FR_HIMA, FR_HIMA14, FR_PLO50, FR_WR105A,
    FR_LOM50, FR_ST105_NE, FR_ST105, FR_WR105,
    FR_HIMB, FR_HIMB15, FR_PLO49, FR_WR104A,
    FR_LOM49, FR_ST104_NE, FR_ST104, FR_WR104,
    FR_MSB_NE1, FR_WRSEL, FR_MSB_NE2, FR_REXPINC, FR_REXP1, FR_WR,
    /* Phases T, U, V — the three M32 hand-offs, :195, :198, :202.         */
    FR_CLZ, FR_CLZ_WR, FR_CLZ_EXP,
    FR_SUBN, FR_SUBN_WR, FR_SUBN_EXP, FR_SUBN_FLUSHED, FR_SUBN_FLAG,
    FR_SUBN_FTZ,
    FR_RND, FR_RND_NORMAL, FR_RND_OVF, FR_RND_EXPOVF, FR_RND_EXPOVFA,
    /* Phase W — the final select chain, :206-212.                         */
    FR_RES0, FR_POVF, FR_RES1, FR_PFLUSH, FR_RES2,
    FR_PZ1, FR_RES3, FR_PI1, FR_PZ2, FR_PIZ, FR_RES4,
    FR_PI2, FR_PZ3, FR_NZ3, FR_PIF, FR_RES5,
    FR_PNAN, FR_AQ, FR_BQ, FR_NANRES, FR_RESULT,
    FR_N_ROWS
};

/* `soft_fmul` — fmul.jl:14-215, 128 rows: 78 emitting, 35 views, 15
 * projections of the four hand-offs' tuples.
 *
 * `==` IS AN `eq` ROW PLUS A `lower_not1!` ROW AND `<` IS A `ult` ROW PLUS
 * ONE, because the raw wires are the NEGATIONS. `lower_eq!` ends
 * `CNOT(or[W-1], r); NOT(r)` with that trailing NOT folded into the copy-out
 * (K09.md §5 delta 2), so `cq_eq_flag` is `a != b`; `cq_ult_block`'s
 * `carry[W]` is `a >=u b`. Upstream's own `lower_not1!` (arith.jl:474-478) is
 * what restores the polarity — two slots and a fresh wire, not an X in place —
 * which is M31's and M36's shape and is why `!=` rows cost one row and `==`
 * rows cost two. All three `<` in `soft_fmul` are on `UInt64`, so all three
 * are `ult` and NONE is `slt` (K16.md §2.0; the `Int64` compares all live
 * inside M32's helpers). */
static const cq_fmul_row PROGRAM[FR_N_ROWS] = {
/* --- Phase A — unpack. ---------------------------------------------------- */
    RV(CQ_FM_A, 63, ALL),                            /* :19  sa = a >> 63  */
    RV(CQ_FM_A, 52, ALL),                            /* :20  a >> 52       */
    RV(FR_EA_SH, 0, CQ_FP64_EXP_ALL),                /* :20  & 0x7FF -> ea */
    RV(CQ_FM_A, 0, CQ_FP64_FRAC_MASK),               /* :21  fa            */
    RV(CQ_FM_B, 63, ALL),                            /* :23  sb            */
    RV(CQ_FM_B, 52, ALL),                            /* :24  b >> 52       */
    RV(FR_EB_SH, 0, CQ_FP64_EXP_ALL),                /* :24  -> eb         */
    RV(CQ_FM_B, 0, CQ_FP64_FRAC_MASK),               /* :25  fb            */
/* --- Phase B — the result sign. `sa` and `sb` are one-lane views, so 63 of
 * this xor's 64 lanes are a ZERO^ZERO the fold table deletes outright; the
 * SLOT count is still the whole 2W (§7.2's no-narrowing, in one row). ------ */
    R2(CQ_FMOP_XOR, FR_SA, FR_SB),                   /* :28  result_sign   */
/* --- Phase C — the six class predicates, through M31's own block. These six
 * lines are `fadd.jl:29-34` row for row — a fact about the sources and not a
 * convenience — and they are exactly M31's `is_nan`/`is_inf`/`is_zero`, so
 * K16 COMPOSES rather than re-transcribes. Same slots, same bits, same gates;
 * what it buys is one source of truth for the predicate (fmul.h's note). --- */
    R2(CQ_FMOP_CLASS, CQ_FM_A, CQ_FP_IS_NAN),        /* :31  a_nan         */
    R2(CQ_FMOP_CLASS, CQ_FM_B, CQ_FP_IS_NAN),        /* :32  b_nan         */
    R2(CQ_FMOP_CLASS, CQ_FM_A, CQ_FP_IS_INF),        /* :33  a_inf         */
    R2(CQ_FMOP_CLASS, CQ_FM_B, CQ_FP_IS_INF),        /* :34  b_inf         */
    R2(CQ_FMOP_CLASS, CQ_FM_A, CQ_FP_IS_ZERO),       /* :35  a_zero        */
    R2(CQ_FMOP_CLASS, CQ_FM_B, CQ_FP_IS_ZERO),       /* :36  b_zero        */
/* --- Phase D — the special-case results. `zero_result` is a PURE VIEW and
 * costs nothing: IEEE's signed zero is a relabelling of one wire. ---------- */
    RV(FR_SIGN, -63, ALL),                           /* :40  rs << 63      */
    R2(CQ_FMOP_OR, FR_SIGN63, CQ_FM_K_INF),          /* :40  inf_result    */
    RV(FR_SIGN, -63, ALL),                           /* :41  zero_result   */
/* --- Phase E — the implicit bit and the effective exponents. The two
 * `Int64(...)` casts at :48-49 ride on the mux rows: a reinterpret is wiring
 * and there is no signedness consequence, because K16 has no signed compare
 * of its own (K16.md §2.0). ------------------------------------------------ */
    R2(CQ_FMOP_EQ,  FR_EA, CQ_FM_K_ZERO),            /* :44  ea != 0       */
    R2(CQ_FMOP_OR,  FR_FA, CQ_FM_K_IMPLICIT),        /* :44  fa | IMPLICIT */
    R3(CQ_FMOP_MUX, FR_EANZ1, FR_FAIMPL, FR_FA),     /* :44  ma            */
    R2(CQ_FMOP_EQ,  FR_EB, CQ_FM_K_ZERO),            /* :45  eb != 0       */
    R2(CQ_FMOP_OR,  FR_FB, CQ_FM_K_IMPLICIT),        /* :45  fb | IMPLICIT */
    R3(CQ_FMOP_MUX, FR_EBNZ1, FR_FBIMPL, FR_FB),     /* :45  mb            */
    R2(CQ_FMOP_EQ,  FR_EA, CQ_FM_K_ZERO),            /* :48  ea != 0       */
    R3(CQ_FMOP_MUX, FR_EANZ2, FR_EA, CQ_FM_K_ONE),   /* :48  ea_eff        */
    R2(CQ_FMOP_EQ,  FR_EB, CQ_FM_K_ZERO),            /* :49  eb != 0       */
    R3(CQ_FMOP_MUX, FR_EBNZ2, FR_EB, CQ_FM_K_ONE),   /* :49  eb_eff        */
/* --- Phase F — pre-normalise, so the leading 1 sits at bit 52 before the
 * 53x53 multiply (:51-56, Bennett-xy4j / U06). Everything after this assumes
 * it: phase S's whole two-case extraction rests on it and :53-54 says so.
 * `m == 0` returns `(0, e)` unchanged, post-Bennett-tpg0. ------------------ */
    R2(CQ_FMOP_NORM52, FR_MA, FR_EAEFF),             /* :57                */
    RO(FR_N52A, CQ_FM_OUT_M),                        /* :57  ma            */
    RO(FR_N52A, CQ_FM_OUT_E),                        /* :57  ea_eff        */
    R2(CQ_FMOP_NORM52, FR_MB, FR_EBEFF),             /* :58                */
    RO(FR_N52B, CQ_FM_OUT_M),                        /* :58  mb            */
    RO(FR_N52B, CQ_FM_OUT_E),                        /* :58  eb_eff        */
/* --- Phase G — the exponent sum. ----------------------------------------- */
    R2(CQ_FMOP_ADD, FR_EAEFF1, FR_EBEFF1),           /* :63  ea+eb         */
    R2(CQ_FMOP_SUB, FR_ESUM, CQ_FM_K_BIAS),          /* :63  result_exp    */
/* --- Phase H — the 26 low / 27 high split (:68-72). `a_lo` and `a_hi` are
 * BOTH views now (§7.3 as amended), so neither costs a gate and the four
 * products below run at the 26/38-lane operand masks the underlying scratch
 * leaves — NOT K11.md §7.5's 26/27, whose `n_a + n_b <= 54` precondition
 * lapses for `a_hi * b_hi`. Measure the gates; do not copy that table. ----- */
    RV(FR_MA1, 0, UINT64_C(0x03FFFFFF)),             /* :73  a_lo          */
    RV(FR_MA1, 26, ALL),                             /* :74  a_hi          */
    RV(FR_MB1, 0, UINT64_C(0x03FFFFFF)),             /* :75  b_lo          */
    RV(FR_MB1, 26, ALL),                             /* :76  b_hi          */
/* --- Phase P — the four significand products, 64% of the region and 65% of
 * the slots. FOUR `cq_mul_block`s in ONE region at FOUR offsets, which is the
 * shape K11.md §7.5 says is the only thing that detects a dropped `off`. --- */
    R2(CQ_FMOP_MUL, FR_ALO, FR_BLO),                 /* :78  pp_ll         */
    R2(CQ_FMOP_MUL, FR_ALO, FR_BHI),                 /* :79  pp_lh         */
    R2(CQ_FMOP_MUL, FR_AHI, FR_BLO),                 /* :80  pp_hl         */
    R2(CQ_FMOP_MUL, FR_AHI, FR_BHI),                 /* :81  pp_hh         */
/* --- Phase I — the cross-term sum. --------------------------------------- */
    R2(CQ_FMOP_ADD, FR_PPLH, FR_PPHL),               /* :91  cross         */
/* --- Phase Q — THE ABANDONED ASSEMBLY, :94-102. EVERY ROW HERE IS DEAD:
 * `carry_lo`, `cross_hi` and `pp_hh_shifted` are read by nothing, and the live
 * assembly at :126-136 recomputes `cross << 26`, `pp_hh << 52` and
 * `cross >> 38` from scratch. Transcribed and marked dead on PRD-v2 §7.6's
 * discarded-arm rule and PRD-v2 §5's M34 row; see this file's header. ------ */
    RV(FR_CROSS, -26, ALL),                          /* :94  cross_lo DEAD */
    R2(CQ_FMOP_ADD, FR_PPLL, FR_Q_CROSSLO),          /* :95  prod_lo  DEAD */
    R2(CQ_FMOP_ULT, FR_Q_PRODLO, FR_PPLL),           /* :97  >=u      DEAD */
    R1(CQ_FMOP_NOT1, FR_Q_GE),                       /* :97  <        DEAD */
    R3(CQ_FMOP_MUX, FR_Q_LT, CQ_FM_K_ONE, CQ_FM_K_ZERO), /* :97 carry DEAD */
    RV(FR_CROSS, 38, ALL),                           /* :101 cross_hi DEAD */
    RV(FR_PPHH, -52, ALL),                           /* :102 pp_hh<<52 DEAD*/
/* --- Phase R — the 106-bit assembly. `:136` IS FOUR OPERANDS AND THEREFORE
 * THREE ADD ROWS: Julia's `+` is left-associative and binary, so
 * `(cross>>38) + (pp_hh>>12) + c1 + c2` is three occurrences. Collapsing them
 * into one four-input adder would be a re-derivation with no upstream
 * construction behind it. `c1` and `c2` are full 64-lane spans carrying one
 * bit each, which is §7.2's no-narrowing costing `2 x C_add(64)` visibly. -- */
    RV(FR_PPLL, 0, ALL),                             /* :126 acc_lo        */
    RV(FR_CROSS, -26, ALL),                          /* :127 term2         */
    R2(CQ_FMOP_ADD, FR_ACCLO, FR_TERM2),             /* :128 sum1          */
    R2(CQ_FMOP_ULT, FR_SUM1, FR_ACCLO),              /* :129 sum1 >=u acc  */
    R1(CQ_FMOP_NOT1, FR_C1_GE),                      /* :129 sum1 <  acc   */
    R3(CQ_FMOP_MUX, FR_C1_LT, CQ_FM_K_ONE, CQ_FM_K_ZERO), /* :129 c1       */
    RV(FR_PPHH, -52, ALL),                           /* :131 term3         */
    R2(CQ_FMOP_ADD, FR_SUM1, FR_TERM3),              /* :132 sum2          */
    R2(CQ_FMOP_ULT, FR_SUM2, FR_SUM1),               /* :133 sum2 >=u sum1 */
    R1(CQ_FMOP_NOT1, FR_C2_GE),                      /* :133 sum2 <  sum1  */
    R3(CQ_FMOP_MUX, FR_C2_LT, CQ_FM_K_ONE, CQ_FM_K_ZERO), /* :133 c2       */
    RV(FR_SUM2, 0, ALL),                             /* :135 prod_lo_final */
    RV(FR_CROSS, 38, ALL),                           /* :136 cross >> 38   */
    RV(FR_PPHH, 12, ALL),                            /* :136 pp_hh >> 12   */
    R2(CQ_FMOP_ADD, FR_CROSSHI, FR_PPHH12),          /* :136 + (1 of 3)    */
    R2(CQ_FMOP_ADD, FR_T1, FR_C1),                   /* :136 + (2 of 3)    */
    R2(CQ_FMOP_ADD, FR_T2, FR_C2),                   /* :136 prod_hi_final */
/* ===== fmul.jl:136 — PRD-v2 §5's recorded seam. See fmul.h. ============== */
/* --- Phase S — the MSB detect and the two extractions. The product's leading
 * 1 is at bit 104 or 105 (:139-153); the two 56-bit extractions are computed
 * unconditionally and selected at :186, which is §7.6's discarded-arm rule
 * again — one of the two is always thrown away. ---------------------------- */
    RV(FR_PHI, 41, ALL),                             /* :156 hi >> 41      */
    RV(FR_HI41, 0, UINT64_C(1)),                     /* :156 msb_at_105    */
    RV(FR_PHI, 0, (UINT64_C(1) << 42) - UINT64_C(1)),/* :174 hi & (1<<42)-1*/
    RV(FR_HIMA, -14, ALL),                           /* :174 << 14         */
    RV(FR_PLO, 50, ALL),                             /* :174 lo >> 50      */
    R2(CQ_FMOP_OR, FR_HIMA14, FR_PLO50),             /* :174 wr_105        */
    RV(FR_PLO, 0, (UINT64_C(1) << 50) - UINT64_C(1)),/* :175 lo & (1<<50)-1*/
    R2(CQ_FMOP_EQ, FR_LOM50, CQ_FM_K_ZERO),          /* :175 != 0          */
    R3(CQ_FMOP_MUX, FR_ST105_NE, CQ_FM_K_ONE, CQ_FM_K_ZERO), /* :175 sticky*/
    R2(CQ_FMOP_OR, FR_WR105A, FR_ST105),             /* :177 wr_105 |      */
    RV(FR_PHI, 0, (UINT64_C(1) << 42) - UINT64_C(1)),/* :180 (2nd)         */
    RV(FR_HIMB, -15, ALL),                           /* :180 << 15         */
    RV(FR_PLO, 49, ALL),                             /* :180 lo >> 49      */
    R2(CQ_FMOP_OR, FR_HIMB15, FR_PLO49),             /* :180 wr_104        */
    RV(FR_PLO, 0, (UINT64_C(1) << 49) - UINT64_C(1)),/* :181 lo & (1<<49)-1*/
    R2(CQ_FMOP_EQ, FR_LOM49, CQ_FM_K_ZERO),          /* :181 != 0          */
    R3(CQ_FMOP_MUX, FR_ST104_NE, CQ_FM_K_ONE, CQ_FM_K_ZERO), /* :181 sticky*/
    R2(CQ_FMOP_OR, FR_WR104A, FR_ST104),             /* :183 wr_104 |      */
    R2(CQ_FMOP_EQ, FR_MSB, CQ_FM_K_ZERO),            /* :186 msb != 0      */
    R3(CQ_FMOP_MUX, FR_MSB_NE1, FR_WR105, FR_WR104), /* :186 wr            */
    R2(CQ_FMOP_EQ, FR_MSB, CQ_FM_K_ZERO),            /* :188 msb != 0 (2nd)*/
    R2(CQ_FMOP_ADD, FR_REXP, CQ_FM_K_ONE),           /* :188 result_exp+1  */
    R3(CQ_FMOP_MUX, FR_MSB_NE2, FR_REXPINC, FR_REXP),/* :188 result_exp    */
    RV(FR_WRSEL, 0, (UINT64_C(1) << 56) - UINT64_C(1)), /* :191 & 56 bits  */
/* --- Phases T, U, V — the three M32 hand-offs. What crosses is an ENCODING
 * no C type expresses (K23 §1.7): `wr` is the G/R/S working format, bit 55 the
 * leading 1, bits 54-3 the fraction and bits 2/1/0 guard/round/sticky. A
 * one-bit disagreement is right on every input that does not round and wrong
 * on the ties, so the tie anchors are the only detector. ------------------- */
    R2(CQ_FMOP_CLZ, FR_WR, FR_REXP1),                /* :195               */
    RO(FR_CLZ, CQ_FM_OUT_WR),                        /* :195 wr            */
    RO(FR_CLZ, CQ_FM_OUT_EXP),                       /* :195 result_exp    */
    R3(CQ_FMOP_SUBNORM, FR_CLZ_WR, FR_CLZ_EXP, FR_SIGN), /* :198-199       */
    RO(FR_SUBN, CQ_FM_OUT_WR),                       /* :198 wr            */
    RO(FR_SUBN, CQ_FM_OUT_EXP),                      /* :198 result_exp    */
    RO(FR_SUBN, CQ_FM_OUT_FLUSHED),                  /* :198 flushed_result*/
    RO(FR_SUBN, CQ_FM_OUT_SUBNORMAL),                /* :198 subnormal     */
    RO(FR_SUBN, CQ_FM_OUT_FTZ),                      /* :198 flush_to_zero */
    R3(CQ_FMOP_ROUND, FR_SUBN_WR, FR_SUBN_EXP, FR_SIGN), /* :202-203       */
    RO(FR_RND, CQ_FM_OUT_NORMAL),                    /* :202 normal_result */
    RO(FR_RND, CQ_FM_OUT_OVERFLOW),                  /* :202 overflow_res  */
    RO(FR_RND, CQ_FM_OUT_EXPOVF),                    /* :202 exp_overflow  */
    RO(FR_RND, CQ_FM_OUT_EXPOVF_AFT),                /* :202 ..._after     */
/* --- Phase W — the final select chain, :206-212. Priority runs BOTTOM-UP:
 * the last `ifelse` written wins, so the NaN row is outermost. `INDEF` at
 * :210 is NOT `result_sign`-signed — `Inf x 0` is 0xFFF8000000000000 whatever
 * the operand signs are (Bennett-r84x), and "tidying" it into
 * `(result_sign << 63) | QNAN` is wrong on exactly half its inputs. -------- */
    RV(FR_RND_NORMAL, 0, ALL),                       /* :206 result        */
    R2(CQ_FMOP_OR1, FR_RND_EXPOVF, FR_RND_EXPOVFA),  /* :207               */
    R3(CQ_FMOP_MUX, FR_POVF, FR_RND_OVF, FR_RES0),   /* :207 res1          */
    R2(CQ_FMOP_AND1, FR_SUBN_FLAG, FR_SUBN_FTZ),     /* :208               */
    R3(CQ_FMOP_MUX, FR_PFLUSH, FR_SUBN_FLUSHED, FR_RES1), /* :208 res2     */
    R2(CQ_FMOP_OR1, FR_AZERO, FR_BZERO),             /* :209               */
    R3(CQ_FMOP_MUX, FR_PZ1, FR_ZERORES, FR_RES2),    /* :209 res3          */
    R2(CQ_FMOP_OR1, FR_AINF, FR_BINF),               /* :210               */
    R2(CQ_FMOP_OR1, FR_AZERO, FR_BZERO),             /* :210 (2nd)         */
    R2(CQ_FMOP_AND1, FR_PI1, FR_PZ2),                /* :210               */
    R3(CQ_FMOP_MUX, FR_PIZ, CQ_FM_K_INDEF, FR_RES3), /* :210 res4          */
    R2(CQ_FMOP_OR1, FR_AINF, FR_BINF),               /* :211 (2nd)         */
    R2(CQ_FMOP_OR1, FR_AZERO, FR_BZERO),             /* :211 (3rd)         */
    R1(CQ_FMOP_NOT1, FR_PZ3),                        /* :211 !(...)        */
    R2(CQ_FMOP_AND1, FR_PI2, FR_NZ3),                /* :211               */
    R3(CQ_FMOP_MUX, FR_PIF, FR_INFRES, FR_RES4),     /* :211 res5          */
    R2(CQ_FMOP_OR1, FR_ANAN, FR_BNAN),               /* :212               */
    R2(CQ_FMOP_OR, CQ_FM_A, CQ_FM_K_QUIET),          /* :212 a | QUIET_BIT */
    R2(CQ_FMOP_OR, CQ_FM_B, CQ_FM_K_QUIET),          /* :212 b | QUIET_BIT */
    R3(CQ_FMOP_MUX, FR_ANAN, FR_AQ, FR_BQ),          /* :212 common.jl:24  */
    R3(CQ_FMOP_MUX, FR_PNAN, FR_NANRES, FR_RES5)     /* :212 result        */
};

_Static_assert(sizeof PROGRAM / sizeof PROGRAM[0] == (size_t)FR_N_ROWS,
               "the enumerator order IS the table order; a row added to one "
               "and not the other must break the build, not shift 450 "
               "operand references by one");

_Static_assert(FR_N_ROWS <= CQ_FMUL_MAX_ROWS,
               "fmul_step.c's prefix-offset array is CQ_FMUL_MAX_ROWS entries; "
               "a table that outgrows it must fail to build rather than "
               "overrun a stack array");

const cq_fmul_row *cq_fmul_rows(int *n)
{
    if (n != NULL) *n = FR_N_ROWS;
    return PROGRAM;
}

int cq_fmul_n_rows(void) { return FR_N_ROWS; }

/* 64, 1, or 0 for a hand-off whose value is a TUPLE. It lives HERE and not in
 * the step machine because it is a fact about the PROGRAM rather than about
 * the layout: `softfloat_common.jl:104` and `:138` return two values,
 * `:190` five and `:226` four, and which of them are `Bool` is read off the
 * Julia. fmul_step.c's contract is that not one line of it knows any Julia. */
/* 64, 1, or 0 for a hand-off whose value is a TUPLE. A `CQ_FMOP_OUT` row's
 * width is its PRODUCER's, at that output index — softfloat_common.jl:190 and
 * :226 return two Bools apiece and the rest 64-lane words. */
int cq_fmul_row_width(const cq_fmul_row *rows, int n, int i)
{
    const cq_fmul_row *r;

    if (rows == NULL || i < 0 || i >= n)
        cq_kernel_die("fmul: a row index outside the program");
    r = &rows[i];

    switch (r->op) {
    case CQ_FMOP_EQ: case CQ_FMOP_ULT: case CQ_FMOP_CLASS:
    case CQ_FMOP_NOT1: case CQ_FMOP_AND1: case CQ_FMOP_OR1:
        return 1;
    case CQ_FMOP_NORM52: case CQ_FMOP_CLZ:
    case CQ_FMOP_SUBNORM: case CQ_FMOP_ROUND:
        return 0;
    case CQ_FMOP_OUT: {
        int p = r->s0, w = r->s1;

        if (p < 0 || p >= i)
            cq_kernel_die("fmul: a projection of a row that is not earlier");
        if (rows[p].op == CQ_FMOP_SUBNORM)
            return (w == CQ_FM_OUT_SUBNORMAL || w == CQ_FM_OUT_FTZ) ? 1 : CQ_FP64_W;
        if (rows[p].op == CQ_FMOP_ROUND)
            return (w == CQ_FM_OUT_EXPOVF || w == CQ_FM_OUT_EXPOVF_AFT)
                 ? 1 : CQ_FP64_W;
        if (rows[p].op != CQ_FMOP_NORM52 && rows[p].op != CQ_FMOP_CLZ)
            cq_kernel_die("fmul: a projection of a row that returns no tuple");
        return CQ_FP64_W; }
    default: break;
    }
    return CQ_FP64_W;
}


/* PRD-v2 §5's `fmul.jl:136` seam, as a row index rather than a file split. */
int cq_fmul_seam_row(void) { return FR_HI41; }

/* A `switch` WITH EVERY CASE NAMED AND A DYING `default:`, because using
 * `default:` for the last real case makes an added op silently take that arm
 * (M36's finding 4). */
int cq_fmul_arity(int op)
{
    switch (op) {
    case CQ_FMOP_VIEW: case CQ_FMOP_NOT1:
        return 1;
    case CQ_FMOP_OUT:                        /* s0 is a row, s1 an INDEX */
    case CQ_FMOP_CLASS:                      /* s0 is 64 lanes, s1 a CLASS */
        return 1;
    case CQ_FMOP_EQ: case CQ_FMOP_ULT: case CQ_FMOP_ADD: case CQ_FMOP_SUB:
    case CQ_FMOP_OR: case CQ_FMOP_XOR: case CQ_FMOP_MUL:
    case CQ_FMOP_NORM52: case CQ_FMOP_CLZ:
    case CQ_FMOP_AND1: case CQ_FMOP_OR1:
        return 2;
    case CQ_FMOP_MUX: case CQ_FMOP_SUBNORM: case CQ_FMOP_ROUND:
        return 3;
    case CQ_FMOP_N_OP:
    default:
        break;
    }
    cq_kernel_die("fmul: arity of an unknown row op");
    return 0;
}
