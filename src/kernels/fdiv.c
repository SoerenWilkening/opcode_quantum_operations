/* src/kernels/fdiv.c — M35, K17, the ROW TABLES half.
 *
 * Read docs/constructions/K17.md and fdiv.h before changing anything here.
 * THE THREE TABLES BELOW ARE THE WHOLE OF THIS FILE AND THEY ARE THE PORT —
 * `soft_fdiv` (third_party/bennett/src/softfloat/fdiv.jl:44-139) and, where it
 * calls it, `_sf_propagate_nan2` (softfloat_common.jl:23-24) inlined at :136 —
 * ONE ROW PER OPERATOR OCCURRENCE in SOURCE ORDER, with every row citing the
 * line it transcribes. Nothing here emits a gate, names a scratch span or
 * knows what a block costs: that is fdiv_step.c, fdiv_operand.c and
 * fdiv_emit.c.
 *
 * THE MIDDLE TABLE IS AN ITERATION TEMPLATE AND THE PROGRAM IS STILL 478 ROWS.
 * `fdiv.jl:91` is `for i in 0:55` and its body is seven rows, so phases A-G
 * are the PRE table, the body is the ITER template instantiated 56 times, and
 * phases I-N are the POST table. `cq_fdiv_row_at` rebases a template row's
 * operand references onto absolute row indices; every iteration therefore has
 * its own slots and its own spans and shares nothing with its neighbours
 * beyond the source's own data flow. Writing the 392 rows out by hand would be
 * 56 independent chances at the one mistake PRD-v2 §7.1 says a kernel of this
 * shape actually makes.
 *
 * THE LITERAL GRAIN IS WHAT A READER CHECKS HERE (PRD-v2 §7.2). One block per
 * operator occurrence as the source spells it; NO common-subexpression
 * sharing — `ea != 0` gets two whole `eq` rows because :66 and :68 each write
 * it, and `result_sign << 63` gets two view rows for :63 and :64; NO narrowing
 * towards the field width, so `ea` is a 64-lane span holding eleven bits of
 * value and `fits` a one-bit span. Both refusals are D9's K12 precedent: a
 * narrowing here would be a RE-DERIVATION rather than a port, and K11's
 * finding stands that the one mutant L1 cannot see is the one that looks like
 * an optimisation. `r` is provably `< 2^54` for the whole loop and `q` has `t`
 * significant bits at iteration `t`; K17.md §3.4 names both as the offers.
 *
 * A VIEW ROW IS AN OPERATOR OCCURRENCE THAT COSTS NOTHING (PRD-v2 §7.3 as
 * amended 2026-09-18). An AND or a shift by a COMPILE-TIME constant is
 * WIRING — 125 of these 478 rows — and they stay ROWS so the 1:1
 * correspondence with the source survives, which is the thing a reader checks
 * and the thing the slot scan walks.
 *
 * THE `Int64(...)` CASTS AT :68-69 RIDE ON THE MUX ROWS AND ARE NOT ROWS OF
 * THEIR OWN. K17.md's draft §2.2 gives them view rows E8 and E11; M34 decided
 * the other way ("a reinterpret is wiring and there is no signedness
 * consequence, because K16 has no signed compare of its own") and M35 follows
 * the shipped sibling, because a reader comparing two fp kernels will assume
 * one of them is wrong. Neither reading costs a slot or a bit. Dated re-count
 * in K17.md §3.
 *
 * `overflow_result` IS DISCARDED BY THIS CALLER AND IS STILL PROJECTED.
 * `fdiv.jl:118-122` destructures it as `_` in upstream's own words
 * (Bennett-ardf / U138) and §7.6's discarded-arm rule makes it a full `or`(64)
 * inside M32 anyway. The projection row costs nothing and is here so that the
 * four-tuple `:122` destructures is visible as four; deleting it would be the
 * same edit as deleting M34's seven dead rows.
 */

#include "kernels/fdiv.h"

#include "kernels/fpclass.h"
#include "kernels/kernel.h"

/* One row per occurrence, in the five shapes the ops need. `mask` and `shift`
 * are read only by a VIEW; `s1`/`s2` only up to `cq_fdiv_arity`. */
#define RV(s0, sh, mk)                                                         \
    { (short)CQ_FDOP_VIEW, (short)(s0), 0, 0, (short)(sh), (mk) }
#define RO(s0, which)                                                          \
    { (short)CQ_FDOP_OUT, (short)(s0), (short)(which), 0, 0, 0 }
#define R1(op, s0)                                                             \
    { (short)(op), (short)(s0), 0, 0, 0, 0 }
#define R2(op, s0, s1)                                                         \
    { (short)(op), (short)(s0), (short)(s1), 0, 0, 0 }
#define R3(op, s0, s1, s2)                                                     \
    { (short)(op), (short)(s0), (short)(s1), (short)(s2), 0, 0 }

#define ALL ~UINT64_C(0)

/* EVERY ROW REFERENCE IS A NAME AND NEVER A NUMBER, for M34's reason: a table
 * whose operands are integers is three chances per row at the one mistake
 * PRD-v2 §7.1 names, and an off-by-one in a reference produces a plausible
 * wrong circuit rather than a crash. The enumerator order IS the table order
 * and each table's length is asserted against its enumerator below. */
enum {
    /* Phase A — unpack, fdiv.jl:47-52. Eight views, zero gates. THE EXPONENT
     * IS ELEVEN LANES AND NOT TWELVE (K22 §2.1's measured trap): `a >> 52` is
     * lanes 52..63 and the `& 0x7FF` drops lane 63, the sign. A twelve-lane
     * view is right for every positive operand and wrong for every negative
     * one, and it survives a golden, a palindrome and a positive-specials
     * anchor table.                                                        */
    FR_SA = 0, FR_EA_SH, FR_EA, FR_FA, FR_SB, FR_EB_SH, FR_EB, FR_FB,
    /* Phase B — the result sign, :54.                                     */
    FR_SIGN,
    /* Phase C — the six class predicates, :56-61, through M31's block.    */
    FR_ANAN, FR_BNAN, FR_AINF, FR_BINF, FR_AZERO, FR_BZERO,
    /* Phase D — the two special-case results, :63-64.                     */
    FR_SIGN63, FR_INFRES, FR_ZERORES,
    /* Phase E — mantissas and effective exponents, :66-69.                */
    FR_EANZ1, FR_FAIMPL, FR_MA0, FR_EBNZ1, FR_FBIMPL, FR_MB0,
    FR_EANZ2, FR_EAEFF0, FR_EBNZ2, FR_EBEFF0,
    /* Phase F — the two pre-normalisations, :78-79. Bennett-r6e3.         */
    FR_N52A, FR_MA1, FR_EAEFF1, FR_N52B, FR_MB1, FR_EBEFF1,
    /* Phase G — the result exponent, :81.                                 */
    FR_EXPDIFF, FR_REXP0,
    FR_N_PRE
};

/* ===== PRD-v2 §5's PRE-NORMALISE <-> THE 56-BIT RESTORING LOOP seam, at
 * fdiv.jl:89. Everything above computes (ma, mb, result_exp) and the select
 * chain's inputs; everything from here to FR_POST_BASE is the loop. What
 * crosses is exactly FR_MA1, FR_MB1 and the constants — pinned by a case, not
 * by a file (see fdiv.h on why it is not a file cut). ===================== */
enum {
    FR_LOOP_BASE = FR_N_PRE,
    FR_POST_BASE = FR_LOOP_BASE + CQ_FDIV_ITER_ROWS * CQ_FDIV_N_ITERS
};

/* Row `j` of iteration `t`. */
#define H(t, j) (FR_LOOP_BASE + (t) * CQ_FDIV_ITER_ROWS + (j))

enum {
    /* Phase I — sticky and the working value, :101-102.                   */
    FR_RNZ = FR_POST_BASE, FR_STICKY, FR_WR0,
    /* Phase J — the one-bit re-normalise, :106-108.                       */
    FR_WRHI, FR_NSH_NE, FR_NEEDSH, FR_WRSH, FR_WR1, FR_REXPDEC, FR_REXP1,
    /* Phase K — _sf_normalize_clz, :111.                                  */
    FR_CLZ, FR_CLZ_WR, FR_CLZ_EXP,
    /* Phase L — _sf_handle_subnormal, :114-115.                           */
    FR_SUBN, FR_SUBN_WR, FR_SUBN_EXP, FR_SUBN_FLUSHED, FR_SUBN_FLAG,
    FR_SUBN_FTZ,
    /* Phase M — _sf_round_and_pack, :122-123.                             */
    FR_RND, FR_RND_NORMAL, FR_RND_OVF, FR_RND_EXPOVF, FR_RND_EXPOVFA,
    /* Phase N — the select chain, :126-136.                               */
    FR_RES0, FR_POVF, FR_RES1, FR_PFLUSH, FR_RES2,
    FR_PZZ, FR_RES3, FR_NBZERO, FR_PAZ, FR_RES4,
    FR_NAZERO, FR_PBZ, FR_RES5, FR_PII, FR_RES6,
    FR_NBINF, FR_PAI, FR_RES7, FR_NAINF, FR_PBI, FR_RES8,
    FR_PNAN, FR_AQ, FR_BQ, FR_NANRES, FR_RESULT,
    FR_N_ROWS
};

/* The three relative operand codes the ITERATION TEMPLATE uses, and nothing
 * else in the program does. They are above every absolute row index and below
 * `short`'s range, so a template row is distinguishable from a rebased one by
 * inspection — which is what lets the well-formedness case assert that
 * `cq_fdiv_row_at` left none of them behind. */
enum { TPL_THIS = 4096, TPL_R_IN = 8192, TPL_Q_IN = 8193 };

/* --- The PRE table: fdiv.jl:47-81. --------------------------------------- */

static const cq_fdiv_row PRE[FR_N_PRE] = {
/* --- Phase A — unpack. ---------------------------------------------------- */
    RV(CQ_FD_A, 63, ALL),                            /* :47  sa = a >> 63  */
    RV(CQ_FD_A, 52, ALL),                            /* :48  a >> 52       */
    RV(FR_EA_SH, 0, CQ_FP64_EXP_ALL),                /* :48  & 0x7FF -> ea */
    RV(CQ_FD_A, 0, CQ_FP64_FRAC_MASK),               /* :49  fa            */
    RV(CQ_FD_B, 63, ALL),                            /* :50  sb            */
    RV(CQ_FD_B, 52, ALL),                            /* :51  b >> 52       */
    RV(FR_EB_SH, 0, CQ_FP64_EXP_ALL),                /* :51  -> eb         */
    RV(CQ_FD_B, 0, CQ_FP64_FRAC_MASK),               /* :52  fb            */
/* --- Phase B — the result sign. `sa` and `sb` are one-lane views, so 63 of
 * this xor's 64 lanes are a ZERO^ZERO the fold table deletes outright; the
 * SLOT count is still the whole 2W (§7.2's no-narrowing, in one row). ------ */
    R2(CQ_FDOP_XOR, FR_SA, FR_SB),                   /* :54  result_sign   */
/* --- Phase C — the six class predicates, through M31's own block. These six
 * lines are `fadd.jl:29-34` and `fmul.jl:31-36` row for row — a fact about the
 * sources, not a convenience — so K17 COMPOSES rather than re-transcribes
 * (D-K17-5). Upstream recomputes `ea == 0x7FF` twice and `ea == 0` once more;
 * so do we, and M31 enforces it by construction since each call is its own
 * block instance with its own region. ------------------------------------- */
    R2(CQ_FDOP_CLASS, CQ_FD_A, CQ_FP_IS_NAN),        /* :56  a_nan         */
    R2(CQ_FDOP_CLASS, CQ_FD_B, CQ_FP_IS_NAN),        /* :57  b_nan         */
    R2(CQ_FDOP_CLASS, CQ_FD_A, CQ_FP_IS_INF),        /* :58  a_inf         */
    R2(CQ_FDOP_CLASS, CQ_FD_B, CQ_FP_IS_INF),        /* :59  b_inf         */
    R2(CQ_FDOP_CLASS, CQ_FD_A, CQ_FP_IS_ZERO),       /* :60  a_zero        */
    R2(CQ_FDOP_CLASS, CQ_FD_B, CQ_FP_IS_ZERO),       /* :61  b_zero        */
/* --- Phase D — the special-case results. `zero_result` is a PURE VIEW and
 * costs nothing: IEEE's signed zero is a relabelling of one wire. FR_ZERORES
 * is the SAME EXPRESSION as FR_SIGN63 and gets its own row (§7.2, no CSE);
 * here that is free, which is why the rule is cheap to obey at :64 and
 * expensive to obey at :68. ------------------------------------------------ */
    RV(FR_SIGN, -63, ALL),                           /* :63  rs << 63      */
    R2(CQ_FDOP_OR, FR_SIGN63, CQ_FD_K_INF),          /* :63  inf_result    */
    RV(FR_SIGN, -63, ALL),                           /* :64  zero_result   */
/* --- Phase E — the implicit bit and the effective exponents. FR_EANZ2 IS A
 * REPEAT OF FR_EANZ1 AND IS A SEPARATE BLOCK: PRD-v2 §7.2 names this exact
 * shape as its worked example of what LLVM would CSE and we will not. Two
 * `eq` blocks at 64 bits, two regions, two extents. ----------------------- */
    R2(CQ_FDOP_EQ,  FR_EA, CQ_FD_K_ZERO),            /* :66  ea != 0       */
    R2(CQ_FDOP_OR,  FR_FA, CQ_FD_K_IMPLICIT),        /* :66  fa | IMPLICIT */
    R3(CQ_FDOP_MUX, FR_EANZ1, FR_FAIMPL, FR_FA),     /* :66  ma            */
    R2(CQ_FDOP_EQ,  FR_EB, CQ_FD_K_ZERO),            /* :67  eb != 0       */
    R2(CQ_FDOP_OR,  FR_FB, CQ_FD_K_IMPLICIT),        /* :67  fb | IMPLICIT */
    R3(CQ_FDOP_MUX, FR_EBNZ1, FR_FBIMPL, FR_FB),     /* :67  mb            */
    R2(CQ_FDOP_EQ,  FR_EA, CQ_FD_K_ZERO),            /* :68  ea != 0 (2nd) */
    R3(CQ_FDOP_MUX, FR_EANZ2, FR_EA, CQ_FD_K_ONE),   /* :68  ea_eff        */
    R2(CQ_FDOP_EQ,  FR_EB, CQ_FD_K_ZERO),            /* :69  eb != 0 (2nd) */
    R3(CQ_FDOP_MUX, FR_EBNZ2, FR_EB, CQ_FD_K_ONE),   /* :69  eb_eff        */
/* --- Phase F — pre-normalise BOTH operands so the leading 1 sits at bit 52
 * (:71-79). THIS IS A BUG FIX AND MUST NOT BE ELIDED: Bennett-r6e3 prepends
 * `_sf_normalize_to_bit52` to both `ma` and `mb` because the 56-iteration loop
 * requires ma, mb in [2^52, 2^53), and "without this step the loop overflows
 * and the result's low 52 bits zero out" (:71-77). It looks like dead code on
 * every normal operand, which is every operand a random draw produces, so the
 * only detectors are §5.10's two subnormal anchors. TWO INSTANCES, TWO
 * REGIONS: sharing one would not even be value-equivalent, since F2's inputs
 * are computed from `b`. --------------------------------------------------- */
    R2(CQ_FDOP_NORM52, FR_MA0, FR_EAEFF0),           /* :78                */
    RO(FR_N52A, CQ_FD_OUT_M),                        /* :78  ma            */
    RO(FR_N52A, CQ_FD_OUT_E),                        /* :78  ea_eff        */
    R2(CQ_FDOP_NORM52, FR_MB0, FR_EBEFF0),           /* :79                */
    RO(FR_N52B, CQ_FD_OUT_M),                        /* :79  mb            */
    RO(FR_N52B, CQ_FD_OUT_E),                        /* :79  eb_eff        */
/* --- Phase G — the result exponent. THE `+` AT :81 IS WHY `9ve.30` IS OWED
 * BY THIS BEAD (K17.md §5.9): it is the only `+` in `soft_fdiv`. The two
 * silently wrong substitutes are K15 §5.7's — K8's accumulator (right forward,
 * wrong `_unc`, a miscompile rather than a test failure) and `cq_sub_block`
 * against `-BIAS` (a re-derivation with a different gate list). ------------ */
    R2(CQ_FDOP_SUB, FR_EAEFF1, FR_EBEFF1),           /* :81  ea_eff-eb_eff */
    R2(CQ_FDOP_ADD, FR_EXPDIFF, CQ_FD_K_BIAS)        /* :81  + BIAS        */
};

_Static_assert(sizeof PRE / sizeof PRE[0] == (size_t)FR_N_PRE,
               "the enumerator order IS the table order; a row added to one "
               "and not the other must break the build, not shift every "
               "operand reference by one");

/* --- The ITERATION TEMPLATE: fdiv.jl:91-98, seven rows. ------------------ *
 *
 *     for i in 0:55
 *         fits = r >= mb                                            :93
 *         r = ifelse(fits, r - mb, r)                               :94
 *         q = (q << 1) | ifelse(fits, UInt64(1), UInt64(0))         :95
 *         r = r << 1                                                :97
 *     end
 *
 * THE MIDDLE THREE LINES ARE OPERATOR-FOR-OPERATOR K12's P1/P2/P3 AND THE TWO
 * ENDS ARE NOT K12's AT ALL (K17.md §1.3). `fits` is `cq_ult_block`'s
 * `carry[W]`, which cmp.h publishes as "`carry[W]` IS `a >=u b`", read with NO
 * polarity gate because the source says `>=`; reading it as `<` would emit the
 * same tuple, keep the palindrome and leave scratch clean, and only L1 against
 * an independent reference would see it (K9's uge-meaning-ule).
 *
 * D-K17-8 — THE MUX ARM ORDER IS `t = diff`, `f = r_in`, AND NOTHING
 * STRUCTURAL CHECKS IT. `r = ifelse(fits, r - mb, r)` binds the SUBTRACTED
 * value to the true arm. A swapped arm still divides *something* — it computes
 * a non-restoring recurrence whose value is wrong only on the iterations where
 * `fits` is true — so the tuple, the palindrome, the scratch and every count
 * survive it. CONFIRMED 2026-09-19 against the pinned line; the anchors that
 * see a swap are the exact-quotient and inexact rows of §5.10.
 *
 * D-K17-4 — `q_0` AND THE `r << 1` FILL LANE ARE LITERAL `CQ_BIT_ZERO`, NOT A
 * PRE-MATERIALISED ZERO REGISTER. `TPL_Q_IN` resolves to `CQ_FD_K_ZERO` at
 * t = 0 and `TPL_R_IN` to `FR_MA1`, which is the whole of the decision and is
 * in ONE place. K12's D9(d) chose the register to keep a loop uniform and
 * K18's D-K18-4 chose literals because it has no loop; K17.md recommends the
 * literal reading and CONFIRMS it here on the ground it gives: the
 * non-uniformity is ONLY at t = 0, the slot count is unaffected either way,
 * and the register would cost 64 qubits for `q_0` plus 56 more for the fill
 * lanes to buy a uniformity the slot scan can take as a parameter instead.
 * The consequence is a GATE difference the scan must predict: at t = 0 the
 * `q << 1` view is all-constant so H6's `or` keeps one CX per lane and elides
 * the other two, and `r_in(0) = ma` has no constant lane where every
 * `r_in(t >= 1)` has exactly one, at lane 0. A scan that predicts from `j`
 * alone goes red on iteration 0 and green everywhere else, and the reflex
 * fix — dropping the constant rows from the prediction — turns the instrument
 * off.
 *
 * `r_in(t)` IS A VIEW OVER THE PREVIOUS ITERATION'S MUX OUTPUT AND MUST BE
 * REBUILT FROM THE STEP INDEX ON EVERY CALL. That is K12 §2.1a's requirement
 * in a new place: the driver replays indices in reverse, so a table carried in
 * mutable state would desynchronise the two halves. `cq_fdiv_row_at` is a pure
 * function of `i`.
 *
 * THE DROPPED TOP LANE. `r << 1` discards `rsel_t[63]`, which fdiv.jl:16-21's
 * invariant makes provably zero and which the two-bit shadow cannot prove. It
 * stays in the region until the sandwich reverse — do NOT free anything inside
 * this compute half. */
static const cq_fdiv_row ITER[CQ_FDIV_ITER_ROWS] = {
    R2(CQ_FDOP_ULT, TPL_R_IN, FR_MB1),               /* :93  fits          */
    R2(CQ_FDOP_SUB, TPL_R_IN, FR_MB1),               /* :94  r - mb        */
    R3(CQ_FDOP_MUX, TPL_THIS + 0, TPL_THIS + 1, TPL_R_IN),  /* :94  rsel   */
    RV(TPL_Q_IN, -1, ALL),                           /* :95  q << 1        */
    R3(CQ_FDOP_MUX, TPL_THIS + 0, CQ_FD_K_ONE, CQ_FD_K_ZERO), /* :95 qbit  */
    R2(CQ_FDOP_OR, TPL_THIS + 3, TPL_THIS + 4),      /* :95  q_out         */
    RV(TPL_THIS + 2, -1, ALL)                        /* :97  r << 1        */
};

_Static_assert(sizeof ITER / sizeof ITER[0] == (size_t)CQ_FDIV_ITER_ROWS,
               "fdiv.jl:91-98's body is seven rows; the count is in fdiv.h "
               "and a disagreement must break the build");

/* --- The POST table: fdiv.jl:101-136. ------------------------------------ */

static const cq_fdiv_row POST[FR_N_ROWS - FR_POST_BASE] = {
/* --- Phase I — sticky and the working value. FR_RNZ's OPERAND IS
 * `r_in(56)` — the view the 56th iteration WOULD have consumed — and not
 * `rsel_55`: `:97` runs on every iteration including the last, so the sticky
 * test reads the SHIFTED value. Reading `rsel_55` instead is a factor of two
 * in `r` and is invisible to the sticky test, since `2x != 0 <=> x != 0`, so
 * it is an equivalent mutant AT THIS SITE and a real defect the moment anyone
 * reads `r` for anything else. Recorded here because nothing can measure it. */
    R2(CQ_FDOP_EQ, H(CQ_FDIV_N_ITERS - 1, 6), CQ_FD_K_ZERO), /* :101 r != 0 */
    R3(CQ_FDOP_MUX, FR_RNZ, CQ_FD_K_ONE, CQ_FD_K_ZERO),      /* :101 sticky */
    R2(CQ_FDOP_OR, H(CQ_FDIV_N_ITERS - 1, 5), FR_STICKY),    /* :102 wr     */
/* --- Phase J — the one-bit re-normalise. FR_NEEDSH's POLARITY IS THE ROW THE
 * DOCSTRING'S CLAIM 4 IS ABOUT (:30-31, "a single normalization shift moves
 * the leading 1 from bit 54 to bit 55 if ma < mb"): `cq_eq_flag` is `a != b`,
 * so `(wr >> 55) == 0` is that flag through upstream's own `lower_not1!`.
 *
 * AND THE TWO DIRECTIONS OF GETTING IT WRONG ARE NOT SYMMETRIC — MEASURED
 * 2026-09-19, and the equivalent half is RECORDED AT THE SITE rather than
 * "fixed" (CLAUDE.md, bd equivalent-mutant-record-at-the-site).
 *
 *   SHIFTING LESS THAN IT SHOULD IS VALUE-EQUIVALENT. Making this row read
 *   `wr >> 54` leaves `need_shift` FALSE where it should be TRUE, so `wr`
 *   keeps its leading 1 at bit 54 and `result_exp` keeps its extra one — and
 *   `_sf_normalize_clz` at :111 then shifts LEFT until the leading 1 is at
 *   bit 55, decrementing the exponent as it goes. `(wr, exp)` and
 *   `(wr << 1, exp - 1)` denote the same number, so CLZ maps both to the same
 *   normalised pair. MEASURED: all 170 L1 cases stay green and only the L4
 *   golden moves. :106-108 is a FAST PATH for the row CLZ would otherwise
 *   walk, not an independent correction — and it is still transcribed,
 *   because it is in the source (Rule 1).
 *
 *   SHIFTING MORE THAN IT SHOULD IS A REAL DEFECT. Reading the raw `eq` flag
 *   instead of `need_shift` shifts a quotient whose leading 1 is ALREADY at
 *   bit 55 up to bit 56, and `_sf_normalize_clz` only shifts LEFT, so nothing
 *   downstream can recover it. MEASURED: killed by L1 (and by five others).
 *   That asymmetry is why this row keeps its `not1` and why the paired
 *   mutation is what proves the first half equivalent rather than untested. */
    RV(FR_WR0, 55, ALL),                             /* :106 wr >> 55      */
    R2(CQ_FDOP_EQ, FR_WRHI, CQ_FD_K_ZERO),           /* :106 raw: != 0     */
    R1(CQ_FDOP_NOT1, FR_NSH_NE),                     /* :106 need_shift    */
    RV(FR_WR0, -1, ALL),                             /* :107 wr << 1       */
    R3(CQ_FDOP_MUX, FR_NEEDSH, FR_WRSH, FR_WR0),     /* :107 wr            */
    R2(CQ_FDOP_SUB, FR_REXP0, CQ_FD_K_ONE),          /* :108 result_exp-1  */
    R3(CQ_FDOP_MUX, FR_NEEDSH, FR_REXPDEC, FR_REXP0),/* :108 result_exp    */
/* --- Phases K, L, M — the three M32 hand-offs. TWO CONTRACTS CROSS HERE AND
 * NO C TYPE EXPRESSES EITHER (K23 §1.7): the G/R/S working format going into
 * L and M — implicit 1 at bit 55, fraction 54-3, G/R/S at 2/1/0 — and
 * leading-1-at-bit-52 going into and out of phase F. Wrong by one bit is right
 * on every input that does not round, so only the tie anchors see it. `:114`
 * and `:122` both pass `result_sign`, a COMPUTED 64-lane span, where
 * `fsqrt.jl:103` passes a literal zero — which is D-K23-7, and why K17's L4
 * row is taken at fdiv's operand shape. -------------------------------- */
    R2(CQ_FDOP_CLZ, FR_WR1, FR_REXP1),               /* :111               */
    RO(FR_CLZ, CQ_FD_OUT_WR),                        /* :111 wr            */
    RO(FR_CLZ, CQ_FD_OUT_EXP),                       /* :111 result_exp    */
    R3(CQ_FDOP_SUBNORM, FR_CLZ_WR, FR_CLZ_EXP, FR_SIGN),     /* :114-115   */
    RO(FR_SUBN, CQ_FD_OUT_WR),                       /* :114 wr            */
    RO(FR_SUBN, CQ_FD_OUT_EXP),                      /* :114 result_exp    */
    RO(FR_SUBN, CQ_FD_OUT_FLUSHED),                  /* :114 flushed_result*/
    RO(FR_SUBN, CQ_FD_OUT_SUBNORMAL),                /* :114 subnormal     */
    RO(FR_SUBN, CQ_FD_OUT_FTZ),                      /* :114 flush_to_zero */
    R3(CQ_FDOP_ROUND, FR_SUBN_WR, FR_SUBN_EXP, FR_SIGN),     /* :122-123   */
    RO(FR_RND, CQ_FD_OUT_NORMAL),                    /* :122 normal_result */
    RO(FR_RND, CQ_FD_OUT_OVERFLOW),                  /* :122 `_` DISCARDED */
    RO(FR_RND, CQ_FD_OUT_EXPOVF),                    /* :122 exp_overflow  */
    RO(FR_RND, CQ_FD_OUT_EXPOVF_AFT),                /* :122 ..._after     */
/* --- Phase N — the select chain, :126-136. PRIORITY RUNS BOTTOM-UP: the last
 * `ifelse` written wins, so the NaN row is outermost. TRANSCRIBE THE ORDER, DO
 * NOT RE-DERIVE IT FROM IEEE — `fmul.jl:206-212` and `fadd.jl:123-133` order
 * their chains differently from each other and from this one, because each has
 * different reachable combinations. `INDEF` is x86's and is NEGATIVE
 * (Bennett-r84x / U08 at :129); a port that emitted `QNAN` instead would be
 * right on the exponent and wrong on the sign bit, which no structural check
 * sees. D-K17-6: the four `!` at :131-135 are Julia `Bool` negations and lower
 * through `lower_not1!` (arith.jl:474-478) — NOT `xor`(1) against the constant
 * one, which is how K15's draft spells the same occurrence, and NOT
 * `UInt64(1) - x`, which the maintainer decided is M14's `sub` block for
 * `fcmp.jl`'s different occurrence. CONFIRMED 2026-09-19. ---------------- */
    RV(FR_RND_NORMAL, 0, ALL),                       /* :126 result        */
    R2(CQ_FDOP_OR1, FR_RND_EXPOVF, FR_RND_EXPOVFA),  /* :127               */
    R3(CQ_FDOP_MUX, FR_POVF, FR_INFRES, FR_RES0),    /* :127 res1          */
    R2(CQ_FDOP_AND1, FR_SUBN_FLAG, FR_SUBN_FTZ),     /* :128               */
    R3(CQ_FDOP_MUX, FR_PFLUSH, FR_SUBN_FLUSHED, FR_RES1),    /* :128 res2  */
    R2(CQ_FDOP_AND1, FR_AZERO, FR_BZERO),            /* :130               */
    R3(CQ_FDOP_MUX, FR_PZZ, CQ_FD_K_INDEF, FR_RES2), /* :130 res3          */
    R1(CQ_FDOP_NOT1, FR_BZERO),                      /* :131 !b_zero       */
    R2(CQ_FDOP_AND1, FR_AZERO, FR_NBZERO),           /* :131               */
    R3(CQ_FDOP_MUX, FR_PAZ, FR_ZERORES, FR_RES3),    /* :131 res4          */
    R1(CQ_FDOP_NOT1, FR_AZERO),                      /* :132 !a_zero       */
    R2(CQ_FDOP_AND1, FR_BZERO, FR_NAZERO),           /* :132               */
    R3(CQ_FDOP_MUX, FR_PBZ, FR_INFRES, FR_RES4),     /* :132 res5          */
    R2(CQ_FDOP_AND1, FR_AINF, FR_BINF),              /* :133               */
    R3(CQ_FDOP_MUX, FR_PII, CQ_FD_K_INDEF, FR_RES5), /* :133 res6          */
    R1(CQ_FDOP_NOT1, FR_BINF),                       /* :134 !b_inf        */
    R2(CQ_FDOP_AND1, FR_AINF, FR_NBINF),             /* :134               */
    R3(CQ_FDOP_MUX, FR_PAI, FR_INFRES, FR_RES6),     /* :134 res7          */
    R1(CQ_FDOP_NOT1, FR_AINF),                       /* :135 !a_inf        */
    R2(CQ_FDOP_AND1, FR_BINF, FR_NAINF),             /* :135               */
    R3(CQ_FDOP_MUX, FR_PBI, FR_ZERORES, FR_RES7),    /* :135 res8          */
    R2(CQ_FDOP_OR1, FR_ANAN, FR_BNAN),               /* :136               */
    R2(CQ_FDOP_OR, CQ_FD_A, CQ_FD_K_QUIET),          /* :136 common.jl:24  */
    R2(CQ_FDOP_OR, CQ_FD_B, CQ_FD_K_QUIET),          /* :136 common.jl:24  */
    R3(CQ_FDOP_MUX, FR_ANAN, FR_AQ, FR_BQ),          /* :136 common.jl:24  */
    R3(CQ_FDOP_MUX, FR_PNAN, FR_NANRES, FR_RES8)     /* :136 result        */
};

_Static_assert(sizeof POST / sizeof POST[0]
               == (size_t)(FR_N_ROWS - FR_POST_BASE),
               "the enumerator order IS the table order; a row added to one "
               "and not the other must break the build");

_Static_assert(FR_N_PRE <= CQ_FDIV_MAX_SEG
               && FR_N_ROWS - FR_POST_BASE <= CQ_FDIV_MAX_SEG,
               "fdiv_step.c's prefix arrays are CQ_FDIV_MAX_SEG entries per "
               "segment; a segment that outgrows one must fail to build "
               "rather than overrun a stack array");

/* --- The program as one indexed sequence. -------------------------------- */

int cq_fdiv_n_rows(void)         { return FR_N_ROWS; }
int cq_fdiv_loop_base_row(void)  { return FR_LOOP_BASE; }
int cq_fdiv_post_base_row(void)  { return FR_POST_BASE; }

const cq_fdiv_row *cq_fdiv_iter_template(int *n)
{
    if (n != NULL) *n = CQ_FDIV_ITER_ROWS;
    return ITER;
}

/* Rebase one template operand onto an absolute row index, at iteration `t`.
 * THE TWO `t == 0` ARMS ARE D-K17-4 AND THEY ARE THE ONLY PLACE IT LIVES. */
static short rebase(short s, int t)
{
    if (s == (short)TPL_R_IN)
        return (short)((t == 0) ? FR_MA1 : H(t - 1, 6));     /* :90 / :97 */
    if (s == (short)TPL_Q_IN)
        return (short)((t == 0) ? CQ_FD_K_ZERO : H(t - 1, 5)); /* :89 / :95 */
    if (s >= (short)TPL_THIS)
        return (short)H(t, s - (short)TPL_THIS);
    return s;                            /* a negative code or a PRE row */
}

void cq_fdiv_row_at(int i, cq_fdiv_row *out)
{
    if (out == NULL || i < 0 || i >= FR_N_ROWS)
        cq_kernel_die("fdiv: a row index outside the program");

    if (i < FR_N_PRE)      { *out = PRE[i]; return; }
    if (i >= FR_POST_BASE) { *out = POST[i - FR_POST_BASE]; return; }

    {
        int u = i - FR_LOOP_BASE;
        int t = u / CQ_FDIV_ITER_ROWS, j = u % CQ_FDIV_ITER_ROWS;

        *out = ITER[j];
        out->s0 = rebase(out->s0, t);
        out->s1 = rebase(out->s1, t);
        out->s2 = rebase(out->s2, t);
    }
}

/* A `switch` WITH EVERY CASE NAMED AND A DYING `default:`, because using
 * `default:` for the last real case makes an added op silently take that arm
 * (M36's finding 4). */
int cq_fdiv_arity(int op)
{
    switch (op) {
    case CQ_FDOP_VIEW: case CQ_FDOP_NOT1:
        return 1;
    case CQ_FDOP_OUT:                        /* s0 is a row, s1 an INDEX   */
    case CQ_FDOP_CLASS:                      /* s0 is 64 lanes, s1 a CLASS */
        return 1;
    case CQ_FDOP_EQ: case CQ_FDOP_ULT: case CQ_FDOP_ADD: case CQ_FDOP_SUB:
    case CQ_FDOP_OR: case CQ_FDOP_XOR:
    case CQ_FDOP_NORM52: case CQ_FDOP_CLZ:
    case CQ_FDOP_AND1: case CQ_FDOP_OR1:
        return 2;
    case CQ_FDOP_MUX: case CQ_FDOP_SUBNORM: case CQ_FDOP_ROUND:
        return 3;
    case CQ_FDOP_N_OP:
    default:
        break;
    }
    cq_kernel_die("fdiv: arity of an unknown row op");
    return 0;
}

/* 64, 1, or 0 for a hand-off whose value is a TUPLE. It lives HERE and not in
 * the step machine because it is a fact about the PROGRAM rather than about
 * the layout: `softfloat_common.jl:104` and `:138` return two values, `:190`
 * five and `:226` four, and which of them are `Bool` is read off the Julia.
 * fdiv_step.c's contract is that not one line of it has any. */
static int width_of(const cq_fdiv_row *r, int producer)
{
    switch (r->op) {
    case CQ_FDOP_EQ: case CQ_FDOP_ULT: case CQ_FDOP_CLASS:
    case CQ_FDOP_NOT1: case CQ_FDOP_AND1: case CQ_FDOP_OR1:
        return 1;
    case CQ_FDOP_NORM52: case CQ_FDOP_CLZ:
    case CQ_FDOP_SUBNORM: case CQ_FDOP_ROUND:
        return 0;
    case CQ_FDOP_OUT:
        if (producer == CQ_FDOP_SUBNORM)
            return (r->s1 == CQ_FD_OUT_SUBNORMAL || r->s1 == CQ_FD_OUT_FTZ)
                 ? 1 : CQ_FP64_W;
        if (producer == CQ_FDOP_ROUND)
            return (r->s1 == CQ_FD_OUT_EXPOVF || r->s1 == CQ_FD_OUT_EXPOVF_AFT)
                 ? 1 : CQ_FP64_W;
        if (producer != CQ_FDOP_NORM52 && producer != CQ_FDOP_CLZ)
            cq_kernel_die("fdiv: a projection of a row that returns no tuple");
        return CQ_FP64_W;
    default: break;
    }
    return CQ_FP64_W;
}

int cq_fdiv_row_width(const cq_fdiv_row *rows, int n, int i)
{
    if (rows == NULL || i < 0 || i >= n)
        cq_kernel_die("fdiv: a row index outside the program");
    if (rows[i].op == CQ_FDOP_OUT) {
        int p = rows[i].s0;

        if (p < 0 || p >= i)
            cq_kernel_die("fdiv: a projection of a row that is not earlier");
        return width_of(&rows[i], rows[p].op);
    }
    return width_of(&rows[i], CQ_FDOP_N_OP);
}

int cq_fdiv_width_at(int i)
{
    cq_fdiv_row r, p;

    cq_fdiv_row_at(i, &r);
    if (r.op != CQ_FDOP_OUT) return width_of(&r, CQ_FDOP_N_OP);
    if (r.s0 < 0 || r.s0 >= i)
        cq_kernel_die("fdiv: a projection of a row that is not earlier");
    cq_fdiv_row_at(r.s0, &p);
    return width_of(&r, p.op);
}
