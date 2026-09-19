/* src/kernels/fconv.c — M37, K19, the ROW TABLES half.
 *
 * Read docs/constructions/K19.md and fconv.h before changing anything here.
 * THE THREE ROW TABLES BELOW ARE THE WHOLE OF THIS FILE AND THEY ARE THE PORT
 * — `soft_fptosi` (third_party/bennett/src/softfloat/fptosi.jl:22-69),
 * `soft_sitofp` (sitofp.jl:15-85) and `soft_fptoui`'s own twelve occurrences
 * (fptoui.jl:28-48) — one row per OPERATOR OCCURRENCE in SOURCE ORDER, with
 * every row citing the line it transcribes. Nothing here emits a gate, names a
 * scratch span or knows what a block costs: that is fconv_step.c, on M36's
 * ROW TABLES <-> STEP MACHINE seam (K18.md D-K18-7, taken again by M32 as
 * K23 D-K23-10 and by M33).
 *
 * THE LITERAL GRAIN IS WHAT A READER CHECKS HERE (PRD-v2 §7.2). One block per
 * operator occurrence as the source spells it; NO common-subexpression
 * sharing — `soft_fptoui` extracts `sign` and `exp` for its own select and
 * each of its two nested `soft_fptosi` calls extracts them AGAIN, so one
 * `fptoui` program carries THREE copies of each and an optimiser would remove
 * two; NO narrowing towards the field width, so `exp` is a 64-lane span
 * holding eleven bits of value and `sign` one holding a single bit. Both are
 * refused deliberately: D9's K12 precedent refused exactly this class of
 * narrowing because it is a RE-DERIVATION rather than a port, and K11's
 * finding stands that the one mutant L1 cannot see is the one that looks like
 * an optimisation.
 *
 * A VIEW ROW IS AN OPERATOR OCCURRENCE THAT COSTS NOTHING (PRD-v2 §7.3 as
 * amended 2026-09-18; D-K23-3). An AND or a shift by a COMPILE-TIME constant
 * is WIRING, and they stay ROWS so the 1:1 correspondence with the source
 * survives — which is the thing a reader checks and the thing the slot scan
 * walks. K19.md's draft predates that amendment and counted `(a >> 63) & 1`,
 * `(a >> 52) & 0x7FF`, `a & FRAC_MASK`, `shifted & 0x3FF` and six more as
 * `and`(64) BLOCKS; every one of them is a view here and the tables are
 * re-counted accordingly. THE ONE `&` IN ALL THREE SOURCES WHOSE SECOND
 * OPERAND IS COMPUTED is `round_bit & (...)` (sitofp.jl:71) and it is a real
 * `and`(64) block.
 *
 * `~x` IS `lower_xor!` WITH AN ALL-ONES CONSTANT, AND THAT IS A PORT RATHER
 * THAN A CHOICE. There is no full-width `lower_not!` in arith.jl — only
 * `lower_not1!` at :474-478, which allocates ONE bit. LLVM spells `~a` as
 * `xor i64 %a, -1`, so both occurrences (fptosi.jl:60, sitofp.jl:22) reach
 * `lower_xor!` (:284-291) with a constant all-ones operand.
 *
 * THE RAW-WIRE CONVENTION IS UPSTREAM'S, NOT OURS (K09.md §5 delta 2, PRD §15
 * D9(b)). `cq_eq_flag` holds `a != b` and `cq_ult_block`'s `carry[W]` holds
 * `a >=u b`, so a `!=` occurrence and a `>=` occurrence read the raw wire and
 * every `==`, `<` and `>` occurrence owes itself one `lower_not1!` — a row of
 * its own, as M33 writes them. K19.md's draft folded those into the
 * comparison's slot count as a "+1"; making them rows is what lets the slot
 * scan predict them and what makes the fourteen not1 occurrences visible to a
 * reader.
 */

#include "kernels/fconv.h"

#include "kernels/kernel.h"

/* One row per occurrence, in the four shapes the ops need. `mask` and `shift`
 * are read only by a VIEW; `s1`/`s2` only up to `cq_fconv_arity`. */
#define RV(s0, sh, mk)                                                         \
    { (short)CQ_FVOP_VIEW, (short)(s0), 0, 0, (short)(sh), (mk) }
#define R1(op, s0)                                                             \
    { (short)(op), (short)(s0), 0, 0, 0, 0 }
#define R2(op, s0, s1)                                                         \
    { (short)(op), (short)(s0), (short)(s1), 0, 0, 0 }
#define R3(op, s0, s1, s2)                                                     \
    { (short)(op), (short)(s0), (short)(s1), (short)(s2), 0, 0 }

#define ALL   (~UINT64_C(0))
#define BIT0  UINT64_C(1)

/* ===== FP -> INT =========================================================
 *
 * --- `soft_fptosi` — fptosi.jl:22-69, 31 rows (6 views, 25 emitting). -----
 *
 * ROWS 23, 28 AND 30 ARE THE MUX-ARM-SWAP TRAP AND THEY ARE NOT EQUALLY
 * DANGEROUS. CLAUDE.md's callout is exact: `mux(c,t,f)` and `mux(c,f,t)` emit
 * the identical (X, CX, CCX) tuple at every width and every mask, keep the
 * palindrome and leave scratch clean. Row 23's arms are `result_left` and
 * `result_right`, so transposing it is wrong on essentially every operand and
 * L1 catches it anywhere — that is the BENIGN one. Row 28 transposed is right
 * on every NON-NEGATIVE input and row 30 transposed is right on every input
 * that is not NaN / +-Inf / out of range, so both hide behind a 32-sample draw
 * of ordinary doubles and are caught only by the anchors K19.md §5.4 forces.
 *
 * ROW 17'S RESULT IS DEAD WHENEVER ROW 18'S IS LIVE AND VICE VERSA, AND BOTH
 * ARE EMITTED IN FULL (PRD-v2 §7.6). `fptosi.jl:5` says the body is a
 * "Branchless implementation for reversible circuit compilation" and
 * fadd.jl:7-11 says why (branching breaks Bennett's phi resolution).
 * DISCARDED is a claim about the RESULT and never about the circuit: computing
 * only the selected arm would need a branch, would be a re-derivation
 * (Rule 1) and would strand the mux that selects it.
 *
 * ROWS 11-13 AND 14-16 ARE THE TWO D8 CLAMPS AND THEY LOOK LIKE COMMENTS
 * (K19.md §5.5). Dropping row 13's clamp lets `right_shift` reach the barrel
 * unclamped; it is 1075 at `exp == 0` and exceeds 63 for every `exp <= 1011`,
 * so D8's `amount mod 64` returns `full_mant >> 0` at `exp == 1011` exactly
 * and `fptosi(2^-12)` yields a 53-bit integer instead of 0 — a live miscompile
 * on every operand below 2^-12, every subnormal and every zero, invisible to
 * the gate count, the palindrome and the pool. */
static const cq_fconv_row FPTOSI[] = {
    /* ---- unpack, fptosi.jl:23-25 ---- */
    RV(CQ_FV_A, 63, ALL),                        /*  0 :23  a >> 63         */
    RV(0, 0, BIT0),                              /*  1 :23  sign            */
    RV(CQ_FV_A, 52, ALL),                        /*  2 :24  a >> 52         */
    RV(2, 0, CQ_FP64_EXP_ALL),                   /*  3 :24  exp             */
    RV(CQ_FV_A, 0, CQ_FP64_FRAC_MASK),           /*  4 :25  mant            */

    /* ---- the implicit bit, :28-29 ---- */
    R2(CQ_FVOP_EQ, 3, CQ_FV_K_0),                /*  5 :28  exp != 0  (raw) */
    R3(CQ_FVOP_MUX, 5, CQ_FV_K_1, CQ_FV_K_0),    /*  6 :28  is_normal       */
    RV(6, -52, ALL),                             /*  7 :29  is_normal << 52 */
    R2(CQ_FVOP_OR, 4, 7),                        /*  8 :29  full_mant       */

    /* ---- the two shift amounts and their clamps, :41-46 ---- */
    R2(CQ_FVOP_SUB, CQ_FV_K_1075, 3),            /*  9 :41  right_shift     */
    R2(CQ_FVOP_SUB, 3, CQ_FV_K_1075),            /* 10 :42  left_shift      */
    R2(CQ_FVOP_ULT, CQ_FV_K_63, 9),              /* 11 :45  63 >=u rs (raw) */
    R1(CQ_FVOP_NOT1, 11),                        /* 12 :45  rs > 63         */
    R3(CQ_FVOP_MUX, 12, CQ_FV_K_63, 9),          /* 13 :45  rs_clamped      */
    R2(CQ_FVOP_ULT, CQ_FV_K_63, 10),             /* 14 :46  63 >=u ls (raw) */
    R1(CQ_FVOP_NOT1, 14),                        /* 15 :46  ls > 63         */
    R3(CQ_FVOP_MUX, 15, CQ_FV_K_63, 10),         /* 16 :46  ls_clamped      */

    /* ---- both arms, :49-54 ---- */
    R2(CQ_FVOP_BLSHR, 8, 13),                    /* 17 :49  result_right    */
    R2(CQ_FVOP_BSHL, 8, 16),                     /* 18 :50  result_left     */
    R2(CQ_FVOP_ULT, 3, CQ_FV_K_1075),            /* 19 :53  exp >=u 1075    */
    R3(CQ_FVOP_MUX, 19, CQ_FV_K_1, CQ_FV_K_0),   /* 20 :53  go_left         */
    R2(CQ_FVOP_EQ, 20, CQ_FV_K_1),               /* 21 :54  go_left != 1    */
    R1(CQ_FVOP_NOT1, 21),                        /* 22 :54  go_left == 1    */
    R3(CQ_FVOP_MUX, 22, 18, 17),                 /* 23 :54  magnitude       */

    /* ---- the sign, :60-61 ---- */
    R2(CQ_FVOP_XOR, 23, CQ_FV_K_ONES),           /* 24 :60  ~magnitude      */
    R2(CQ_FVOP_ADD, 24, CQ_FV_K_1),              /* 25 :60  negated         */
    R2(CQ_FVOP_EQ, 1, CQ_FV_K_1),                /* 26 :61  sign != 1       */
    R1(CQ_FVOP_NOT1, 26),                        /* 27 :61  sign == 1       */
    R3(CQ_FVOP_MUX, 27, 25, 23),                 /* 28 :61  signed_result   */

    /* ---- x86's invalid-operand saturation, :66-67 (PRD-v2 §7.5) ---- */
    R2(CQ_FVOP_ULT, 3, CQ_FV_K_1086),            /* 29 :66  is_invalid      */
    R3(CQ_FVOP_MUX, 29, CQ_FV_K_HIBIT, 28)       /* 30 :67  result          */
};

/* --- `soft_fptoui`'s OWN occurrences — fptoui.jl:29-48, 12 rows. ---------
 *
 * The two `soft_fptosi` calls at :40 and :46 are NOT rows: `cq_fconv_program`
 * inlines the FPTOSI table above at each call site, which is fptoui.jl's own
 * structure under §7.2's no-CSE grain. The two placeholders below mark where
 * they go, and the assembly asserts that the row after each placeholder is the
 * one that reads its result.
 *
 * THE LAST ROW'S ARMS ARE THE `U31` CELL. `ifelse(in_high_range, path_b,
 * path_a)` transposed returns the 2^63-biased answer for every ORDINARY
 * double and the unbiased one for the [2^63, 2^64) half — which is precisely
 * the regression instructions.jl:7637-7639 records upstream fixing ("fptoui
 * must NOT route through fptosi"), and K19.md §5.4 row 21 (`1e19`) is the only
 * anchor that sees it.
 *
 * TWO LOCAL SENTINELS NAME THE INLINED CALLS' RESULTS. `soft_fptosi(a)` at :40
 * and `soft_fptosi(adjusted)` at :46 become 31 rows each, at indices this
 * table cannot know, so the two operands that read them carry codes that are
 * NOT in fconv.h's public enum. `copy_own` resolves both; nothing else can,
 * and fconv_step.c's `const_of` dies on an unknown negative code — so a
 * forgotten fix-up is a LOUD ABORT rather than a silently wrong operand. A
 * placeholder of 0 would have been a legal row index and therefore a wrong
 * circuit. */
enum { FV_CONV_A = -100, FV_CONV_B = -101 };

/* Where the two calls sit INSIDE this table: rows [0, CALL_A) precede the
 * first, the FSUB row at CALL_A is :45, and rows [TAIL, 12) follow the second.
 * Named so `build_fptoui` never carries a bare index. */
enum { FPTOUI_CALL_A = 9, FPTOUI_TAIL = 10 };

static const cq_fconv_row FPTOUI_OWN[] = {
    RV(CQ_FV_A, 63, ALL),                        /*  0 :29  a >> 63         */
    RV(0, 0, BIT0),                              /*  1 :29  sign            */
    RV(CQ_FV_A, 52, ALL),                        /*  2 :30  a >> 52         */
    RV(2, 0, CQ_FP64_EXP_ALL),                   /*  3 :30  exp             */
    R2(CQ_FVOP_EQ, 1, CQ_FV_K_0),                /*  4 :34  sign != 0       */
    R1(CQ_FVOP_NOT1, 4),                         /*  5 :34  sign == 0       */
    R2(CQ_FVOP_EQ, 3, CQ_FV_K_1086),             /*  6 :34  exp != 1086     */
    R1(CQ_FVOP_NOT1, 6),                         /*  7 :34  exp == 1086     */
    R2(CQ_FVOP_AND1, 5, 7),                      /*  8 :34  in_high_range   */
    /*       :40  path_a = soft_fptosi(a)     — the FPTOSI table, inlined    */
    R2(CQ_FVOP_FSUB, CQ_FV_A, CQ_FV_K_BIAS),     /*  9 :45  adjusted        */
    /*       :46  soft_fptosi(adjusted)       — the FPTOSI table, inlined    */
    R2(CQ_FVOP_OR, CQ_FV_K_HIBIT, FV_CONV_B),    /* 10 :46  path_b          */
    R3(CQ_FVOP_MUX, 8, 10, FV_CONV_A)            /* 11 :48  result          */
};

/* ===== the PRD-v2 §5 seam: INT->FP below, FP->INT above ==================
 *
 * ZERO NAMED SPANS CROSS THIS LINE (K19.md §2.10, hand-traced there and
 * asserted in tests/test_kernel_fconv_slots.inc). The two halves share the
 * IEEE field idiom `(a>>63)&1` / `(a>>52)&0x7FF` / `a & FRAC_MASK`, which is
 * M31 `fpfield`'s and not a K19 intermediate, and nothing else: two row
 * tables, two barrels, the whole saturation story above and the whole
 * CLZ-and-rounding story below. It is a marked boundary rather than a file cut
 * for K15.md's reason — the prefix-sum walk stays in one place, because two
 * tables is two chances to get the decode wrong.
 *
 * --- `soft_sitofp` — sitofp.jl:15-85, 75 rows (25 views, 50 emitting). ----
 *
 * `clz = UInt64(0)` at :26 IS A CONSTANT SOURCE AND NOT A ROW (§7.3 rule 3):
 * stage 1's `clz + 32` reads `CQ_FV_K_0`, so the whole ladder's `clz` chain is
 * rooted in a constant span costing zero qubits. `tmp = magnitude` at :27 IS a
 * row, as a zero-cost alias view, so the table reads one-to-one against the
 * source.
 *
 * ROW 8's FALSE ARM IS THE RAIL. `magnitude = ifelse(sign == 1, neg, a)`
 * (:23) takes `a` itself, which for `uitofp` is the ZEXT VIEW over a narrower
 * source — the one place the source width reaches the program at all, and it
 * reaches it as an operand rather than as a row. */
static const cq_fconv_row SITOFP[] = {
    /* ---- the zero test and the magnitude, :17-23 ---- */
    R2(CQ_FVOP_EQ, CQ_FV_A, CQ_FV_K_0),          /*  0 :17  a != 0   (raw)  */
    R1(CQ_FVOP_NOT1, 0),                         /*  1 :17  is_zero         */
    RV(CQ_FV_A, 63, ALL),                        /*  2 :20  a >> 63         */
    RV(2, 0, BIT0),                              /*  3 :20  sign            */
    R2(CQ_FVOP_XOR, CQ_FV_A, CQ_FV_K_ONES),      /*  4 :22  ~a              */
    R2(CQ_FVOP_ADD, 4, CQ_FV_K_1),               /*  5 :22  neg             */
    R2(CQ_FVOP_EQ, 3, CQ_FV_K_1),                /*  6 :23  sign != 1       */
    R1(CQ_FVOP_NOT1, 6),                         /*  7 :23  sign == 1       */
    R3(CQ_FVOP_MUX, 7, 5, CQ_FV_A),              /*  8 :23  magnitude       */
    RV(8, 0, ALL),                               /*  9 :27  tmp = magnitude */

    /* ---- CLZ stage 1, :29-31 — k = 32, probe 32 ---- */
    RV(9, 32, ALL),                              /* 10 :29  tmp >> 32       */
    R2(CQ_FVOP_EQ, 10, CQ_FV_K_0),               /* 11 :29  != 0     (raw)  */
    R1(CQ_FVOP_NOT1, 11),                        /* 12 :29  top32_zero      */
    R2(CQ_FVOP_ADD, CQ_FV_K_0, CQ_FV_K_32),      /* 13 :30  clz + 32        */
    R3(CQ_FVOP_MUX, 12, 13, CQ_FV_K_0),          /* 14 :30  clz             */
    RV(9, -32, ALL),                             /* 15 :31  tmp << 32       */
    R3(CQ_FVOP_MUX, 12, 15, 9),                  /* 16 :31  tmp             */

    /* ---- CLZ stage 2, :33-35 — k = 16, probe 48 ---- */
    RV(16, 48, ALL),                             /* 17 :33  tmp >> 48       */
    R2(CQ_FVOP_EQ, 17, CQ_FV_K_0),               /* 18 :33  != 0     (raw)  */
    R1(CQ_FVOP_NOT1, 18),                        /* 19 :33  top16_zero      */
    R2(CQ_FVOP_ADD, 14, CQ_FV_K_16),             /* 20 :34  clz + 16        */
    R3(CQ_FVOP_MUX, 19, 20, 14),                 /* 21 :34  clz             */
    RV(16, -16, ALL),                            /* 22 :35  tmp << 16       */
    R3(CQ_FVOP_MUX, 19, 22, 16),                 /* 23 :35  tmp             */

    /* ---- CLZ stage 3, :37-39 — k = 8, probe 56 ---- */
    RV(23, 56, ALL),                             /* 24 :37  tmp >> 56       */
    R2(CQ_FVOP_EQ, 24, CQ_FV_K_0),               /* 25 :37  != 0     (raw)  */
    R1(CQ_FVOP_NOT1, 25),                        /* 26 :37  top8_zero       */
    R2(CQ_FVOP_ADD, 21, CQ_FV_K_8),              /* 27 :38  clz + 8         */
    R3(CQ_FVOP_MUX, 26, 27, 21),                 /* 28 :38  clz             */
    RV(23, -8, ALL),                             /* 29 :39  tmp << 8        */
    R3(CQ_FVOP_MUX, 26, 29, 23),                 /* 30 :39  tmp             */

    /* ---- CLZ stage 4, :41-43 — k = 4, probe 60 ---- */
    RV(30, 60, ALL),                             /* 31 :41  tmp >> 60       */
    R2(CQ_FVOP_EQ, 31, CQ_FV_K_0),               /* 32 :41  != 0     (raw)  */
    R1(CQ_FVOP_NOT1, 32),                        /* 33 :41  top4_zero       */
    R2(CQ_FVOP_ADD, 28, CQ_FV_K_4),              /* 34 :42  clz + 4         */
    R3(CQ_FVOP_MUX, 33, 34, 28),                 /* 35 :42  clz             */
    RV(30, -4, ALL),                             /* 36 :43  tmp << 4        */
    R3(CQ_FVOP_MUX, 33, 36, 30),                 /* 37 :43  tmp             */

    /* ---- CLZ stage 5, :45-47 — k = 2, probe 62 ---- */
    RV(37, 62, ALL),                             /* 38 :45  tmp >> 62       */
    R2(CQ_FVOP_EQ, 38, CQ_FV_K_0),               /* 39 :45  != 0     (raw)  */
    R1(CQ_FVOP_NOT1, 39),                        /* 40 :45  top2_zero       */
    R2(CQ_FVOP_ADD, 35, CQ_FV_K_2),              /* 41 :46  clz + 2         */
    R3(CQ_FVOP_MUX, 40, 41, 35),                 /* 42 :46  clz             */
    RV(37, -2, ALL),                             /* 43 :47  tmp << 2        */
    R3(CQ_FVOP_MUX, 40, 43, 37),                 /* 44 :47  tmp             */

    /* ---- CLZ stage 6, :49-50 — k = 1, probe 63. FOUR ROWS, NOT SEVEN. ----
     *
     * THE LADDER IS FIVE UNIFORM STAGES AND A SHORT SIXTH, AND THIS IS THE
     * OFF-BY-ONE K19.md §2.4 ranks as the likeliest in the document.
     * sitofp.jl:49-50 stops at `clz`: there is no `tmp = ifelse(top1_zero,
     * tmp << 1, tmp)` because nothing reads `tmp` after stage 6. A decode
     * written as `stage = u / stage_len` is wrong by two rows at the end and
     * the error lands in :54's `sub`, whose operand is `clz` — so the
     * exponent comes out off by a power of two on every magnitude whose
     * highest set bit is at an odd position. Row 44 (stage 5's `tmp`) is the
     * last write to `tmp` and NOTHING below reads it. */
    RV(44, 63, ALL),                             /* 45 :49  tmp >> 63       */
    R2(CQ_FVOP_EQ, 45, CQ_FV_K_0),               /* 46 :49  != 0     (raw)  */
    R1(CQ_FVOP_NOT1, 46),                        /* 47 :49  top1_zero       */
    R2(CQ_FVOP_ADD, 42, CQ_FV_K_1),              /* 48 :50  clz + 1         */
    R3(CQ_FVOP_MUX, 47, 48, 42),                 /* 49 :50  clz  (FINAL)    */

    /* ---- exponent and the alignment shift, :54-60 ---- */
    R2(CQ_FVOP_SUB, CQ_FV_K_1086, 49),           /* 50 :54  exponent        */
    R2(CQ_FVOP_ULT, CQ_FV_K_63, 49),             /* 51 :59  63 >=u clz      */
    R1(CQ_FVOP_NOT1, 51),                        /* 52 :59  clz > 63        */
    R3(CQ_FVOP_MUX, 52, CQ_FV_K_63, 49),         /* 53 :59  shift_clamped   */
    R2(CQ_FVOP_BSHL, 8, 53),                     /* 54 :60  shifted         */

    /* ---- the mantissa and round-to-nearest-even, :63-72 ----
     *
     * THIS IS sitofp.jl's OWN RTNE AND IT IS NOT `_sf_round_and_pack`
     * (K19.md §1.6). M32's block reads guard/round/sticky at bits 2/1/0 of a
     * `wr << 3` working value and `frac` at `wr >> 3`; this reads a SINGLE
     * round bit at 10, a 10-bit sticky at `& 0x3FF` and `frac` at
     * `shifted >> 11`. Substituting M32's block would be right on every input
     * that does not round and wrong on the ties. */
    RV(54, 11, ALL),                             /* 55 :63  shifted >> 11   */
    RV(55, 0, CQ_FP64_FRAC_MASK),                /* 56 :63  mantissa        */
    RV(54, 10, ALL),                             /* 57 :68  shifted >> 10   */
    RV(57, 0, BIT0),                             /* 58 :68  round_bit       */
    RV(54, 0, UINT64_C(0x3FF)),                  /* 59 :69  sticky          */
    R2(CQ_FVOP_EQ, 59, CQ_FV_K_0),               /* 60 :71  sticky != 0     */
    R3(CQ_FVOP_MUX, 60, CQ_FV_K_1, CQ_FV_K_0),   /* 61 :71  sticky_flag     */
    RV(56, 0, BIT0),                             /* 62 :71  mantissa & 1    */
    R2(CQ_FVOP_OR, 61, 62),                      /* 63 :71  the `|` term    */
    R2(CQ_FVOP_AND, 58, 63),                     /* 64 :71  round_up        */
    R2(CQ_FVOP_ADD, 56, 64),                     /* 65 :72  mantissa + up   */

    /* ---- the rounding carry-out, :75-77 ---- */
    RV(65, 52, ALL),                             /* 66 :75  mantissa >> 52  */
    RV(66, 0, BIT0),                             /* 67 :75  mant_overflow   */
    R2(CQ_FVOP_ADD, 50, 67),                     /* 68 :76  exponent + ovf  */
    RV(65, 0, CQ_FP64_FRAC_MASK),                /* 69 :77  mantissa & FRAC */

    /* ---- pack, :80, and the zero override, :83 ----
     *
     * `|` IS LEFT-ASSOCIATIVE IN JULIA, so :80 is TWO `or` occurrences and not
     * one three-way one. Rows 70 and 71 are views over SCRATCH spans, so their
     * vacated lanes are that span's own |0> QUBITS and nothing folds — a port
     * that "saw" the three fields were disjoint and assembled `result` by
     * relabelling would have re-derived the construction (Rule 1) and skipped
     * gates the reverse half is expecting. */
    RV(3, -63, ALL),                             /* 70 :80  sign << 63      */
    RV(68, -52, ALL),                            /* 71 :80  exponent << 52  */
    R2(CQ_FVOP_OR, 70, 71),                      /* 72 :80  first `|`       */
    R2(CQ_FVOP_OR, 72, 69),                      /* 73 :80  second `|`      */
    R3(CQ_FVOP_MUX, 1, CQ_FV_K_0, 73)            /* 74 :83  result          */
};

enum {
    N_FPTOSI     = (int)(sizeof FPTOSI     / sizeof FPTOSI[0]),
    N_FPTOUI_OWN = (int)(sizeof FPTOUI_OWN / sizeof FPTOUI_OWN[0]),
    N_SITOFP     = (int)(sizeof SITOFP     / sizeof SITOFP[0])
};

/* ASSERTED RATHER THAN COMMENTED, as M33 asserts its 124. The failure mode of
 * a mis-transcribed table is to SHORTEN the program — an omitted operator
 * occurrence fails by NOT EXISTING, and no gate count, palindrome or pool
 * check can observe it. */
_Static_assert(N_FPTOSI == 31 && N_FPTOUI_OWN == 12 && N_SITOFP == 75,
               "fptosi.jl:22-69 is 31 operator occurrences under §7.2's grain "
               "(6 views, 25 emitting), fptoui.jl:29-48's own are 12 and "
               "sitofp.jl:15-85 is 75 (25 views, 50 emitting); changing one is "
               "a re-reading of the source");

_Static_assert(N_FPTOUI_OWN + 2 * N_FPTOSI <= CQ_FCONV_MAX_ROWS,
               "fptoui's program must fit the prefix-offset buffer");

const cq_fconv_row *cq_fptosi_rows(int *n)
{
    if (n == NULL) cq_kernel_die("fconv: no row-count output");
    *n = N_FPTOSI;
    return FPTOSI;
}

const cq_fconv_row *cq_sitofp_rows(int *n)
{
    if (n == NULL) cq_kernel_die("fconv: no row-count output");
    *n = N_SITOFP;
    return SITOFP;
}

const cq_fconv_row *cq_fptoui_own_rows(int *n)
{
    if (n == NULL) cq_kernel_die("fconv: no row-count output");
    *n = N_FPTOUI_OWN;
    return FPTOUI_OWN;
}

int cq_fconv_arity(int op)
{
    switch (op) {
    case CQ_FVOP_VIEW: case CQ_FVOP_NOT1:
        return 1;
    case CQ_FVOP_EQ:   case CQ_FVOP_ULT:  case CQ_FVOP_SUB: case CQ_FVOP_ADD:
    case CQ_FVOP_AND:  case CQ_FVOP_OR:   case CQ_FVOP_XOR:
    case CQ_FVOP_AND1: case CQ_FVOP_BSHL: case CQ_FVOP_BLSHR:
    case CQ_FVOP_FSUB:
        return 2;
    case CQ_FVOP_MUX:
        return 3;
    default: break;
    }
    cq_kernel_die("fconv: unknown op in the program");
    return 0;
}

/* Copy `src` into `out` shifting every internal row reference by `base` and
 * rebasing `CQ_FV_A` onto `a_row` when one is given. A negative code other
 * than `CQ_FV_A` names a constant and is untouched. */
static int emit_body(const cq_fconv_row *src, int n, int base, int a_row,
                     cq_fconv_row *out)
{
    for (int i = 0; i < n; i++) {
        /* Copied OUT and back rather than indexed through `&out[i].s0`:
         * walking off one member into the next is undefined even where the
         * layout obliges, and this file compiles under -fsanitize=undefined. */
        short s[3] = { src[i].s0, src[i].s1, src[i].s2 };
        int arity = cq_fconv_arity(src[i].op);

        for (int j = 0; j < arity; j++) {
            if (s[j] >= 0)                          s[j] = (short)(s[j] + base);
            else if (s[j] == CQ_FV_A && a_row >= 0) s[j] = (short)a_row;
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

/* One chunk of FPTOUI_OWN, whose rows are laid down in THREE non-contiguous
 * pieces — so a single base shift is wrong and `map` is what is right. `map`
 * carries, for every own-row index already placed, the index it landed on;
 * the two sentinels resolve to the inlined calls' result rows. */
static int copy_own(int si, int cnt, int dst, const int *map,
                    cq_fconv_row *out)
{
    for (int i = 0; i < cnt; i++) {
        const cq_fconv_row *r = &FPTOUI_OWN[si + i];
        short s[3] = { r->s0, r->s1, r->s2 };
        int arity = cq_fconv_arity(r->op);

        for (int j = 0; j < arity; j++) {
            if (s[j] >= 0) {
                if (s[j] >= si + i)
                    cq_kernel_die("fconv: fptoui's table references a row that "
                                  "is not strictly earlier");
                s[j] = (short)map[s[j]];
            } else if (s[j] == FV_CONV_A || s[j] == FV_CONV_B) {
                s[j] = (short)map[N_FPTOUI_OWN + (s[j] == FV_CONV_A ? 0 : 1)];
            }
        }
        out[dst + i]    = *r;
        out[dst + i].s0 = s[0];
        out[dst + i].s1 = s[1];
        out[dst + i].s2 = s[2];
    }
    return dst + cnt;
}

/* fptoui.jl:28-48 with its two `soft_fptosi` calls INLINED at :40 and :46.
 * Each copy gets its own 31 rows — §7.2's no-CSE grain — and therefore its own
 * extent in the prefix walk, which is what makes K19.md §2.3's "U8's and U10's
 * scratch must be DISJOINT" true by construction rather than by care. */
static int build_fptoui(cq_fconv_row *out)
{
    int map[N_FPTOUI_OWN + 2];
    int n = 0;

    /* :29-34 — the sign, the exponent and the select flag. */
    for (int i = 0; i < FPTOUI_CALL_A; i++) map[i] = n + i;
    n = copy_own(0, FPTOUI_CALL_A, n, map, out);

    /* :40  path_a = soft_fptosi(a) — `a` is still the rail, so no rebase. */
    n = emit_body(FPTOSI, N_FPTOSI, n, -1, out);
    map[N_FPTOUI_OWN + 0] = n - 1;                        /* FV_CONV_A */

    /* :45  adjusted = soft_fsub(a, KBIAS) — laid down BEFORE the second
     * fptosi copy, because that copy reads it. */
    map[FPTOUI_CALL_A] = n;
    n = copy_own(FPTOUI_CALL_A, 1, n, map, out);

    /* :46  soft_fptosi(adjusted) — the SAME table with `CQ_FV_A` rebased onto
     * the fsub row. That is the one rebase in the module, and it is
     * fptoui.jl's own second call rather than a sharing of the first. */
    n = emit_body(FPTOSI, N_FPTOSI, n, map[FPTOUI_CALL_A], out);
    map[N_FPTOUI_OWN + 1] = n - 1;                        /* FV_CONV_B */

    /* :46 and :48 — the `or` and the select. */
    for (int i = FPTOUI_TAIL; i < N_FPTOUI_OWN; i++) map[i] = n + (i - FPTOUI_TAIL);
    n = copy_own(FPTOUI_TAIL, N_FPTOUI_OWN - FPTOUI_TAIL, n, map, out);
    return n;
}

int cq_fconv_program(cq_fconv_prog p, cq_fconv_row *out)
{
    if (out == NULL) cq_kernel_die("fconv: no output buffer for a program");

    if (p == CQ_FCONV_PROG_FPTOSI) return emit_body(FPTOSI, N_FPTOSI, 0, -1, out);
    if (p == CQ_FCONV_PROG_SITOFP) return emit_body(SITOFP, N_SITOFP, 0, -1, out);
    if (p == CQ_FCONV_PROG_FPTOUI) return build_fptoui(out);

    cq_kernel_die("fconv: program id names no conversion");
    return 0;
}

int cq_fconv_result_row(cq_fconv_prog p)
{
    if (p == CQ_FCONV_PROG_FPTOSI) return N_FPTOSI - 1;
    if (p == CQ_FCONV_PROG_SITOFP) return N_SITOFP - 1;
    if (p == CQ_FCONV_PROG_FPTOUI) return N_FPTOUI_OWN + 2 * N_FPTOSI - 1;

    cq_kernel_die("fconv: program id names no conversion");
    return 0;
}

/* instructions.jl:7677-7679 widens a narrower source and :7672-7673 does not
 * widen a 64-bit one — which is why 64 is absent here and is bead 9ve.34.
 * `opcode_table.yaml`'s `uitofp` row ships {i1,i8,i16,i32,i64} -> f64, so the
 * i64 pair exists in the ABI and is an abort rather than a gap. */
static const int UITOFP_W[] = { 1, 8, 16, 32 };

_Static_assert(sizeof UITOFP_W / sizeof UITOFP_W[0] == CQ_FCONV_N_UITOFP_W,
               "the shipped uitofp source widths are i1/i8/i16/i32; i64 is "
               "bead 9ve.34's refusal and must not be added here");

const int *cq_fconv_uitofp_widths(int *n)
{
    if (n == NULL) cq_kernel_die("fconv: no width-count output");
    *n = CQ_FCONV_N_UITOFP_W;
    return UITOFP_W;
}
