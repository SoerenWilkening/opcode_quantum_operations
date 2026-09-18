/* src/kernels/fpround.h — M32, K23. The four SHARED softfloat helpers of
 * third_party/bennett/src/softfloat/softfloat_common.jl, as exported step
 * blocks at f64. PRD-v2 §5's M32 row, §7.1-7.4, §7.6, §7.10, §7.12, §7.16.
 *
 * Read docs/constructions/K23.md before changing anything here. Every gate is
 * a port: `_sf_normalize_to_bit52` (softfloat_common.jl:68-105),
 * `_sf_normalize_clz` (:113-139), `_sf_handle_subnormal` (:174-191) and
 * `_sf_round_and_pack` (:198-227), transcribed ONE ROW PER OPERATOR OCCURRENCE
 * in source order (§7.2's literal grain) over the step blocks M12, M14, M16
 * and M17 already export, plus upstream's own three-gate bitwise vocabulary.
 * Nothing in this module is a construction of ours.
 *
 * M32 SHIPS NO `cqrt_*` SYMBOL AND NO RULE 7 KERNEL. Like M31 it is a
 * vocabulary module: its consumers are M33 `fadd`, M34 `fmul`, M35 `fdiv`,
 * M39 `fma` and M40 `fsqrt`, and each reaches it through a step block, because
 * `cq_sandwich` refuses nesting in both configurations (the
 * composite-kernels-call-the-step-function rule M12 is the witness for).
 * There is therefore NO copy-out here and no R9 short-circuit inside a block:
 * a block IS a compute half, its region is allocated and pre-materialised by
 * the time step 0 runs, so there is nothing left to fold. A consumer's fold
 * belongs at its own entry, over the whole routine, and calls the four
 * `*_eval` bodies at the foot of this file (K23 §2.7, D-K23-9).
 *
 * THE TWO HAND-OFF ENCODINGS, WHICH NO C TYPE EXPRESSES (K23 §1.7, risk 2).
 * Five call sites cross them and every one crosses a bead boundary too, which
 * is where contracts go to be assumed. They are wrong ONLY ON TIES, so the tie
 * anchors are the only detector.
 *
 *   (a) THE G/R/S WORKING FORMAT, into `cq_round_block` and
 *       `cq_subnorm_block`. `fadd.jl:13-14` states it: "Working format:
 *       mantissa shifted left by 3 for guard/round/sticky bits. bit 55 =
 *       implicit 1, bits 54-3 = 52-bit fraction, bits 2/1/0 = G/R/S."
 *       `_sf_round_and_pack` reads exactly that at :204-207. Bits 56-63 of
 *       `wr` are never read by round-and-pack.
 *   (b) LEADING 1 AT BIT 52, into and out of `cq_norm52_block`;
 *       LEADING 1 AT BIT 55, out of `cq_clz_block`. `fdiv.jl:10-14` says why
 *       ("assumes ma, mb in [2^52, 2^53)") and softfloat_common.jl:46-48
 *       states norm52's precondition: `m` has no bits set above bit 52.
 *
 * `result_exp` / `e` ARE `Int64` AND `wr` / `m` / `result_sign` ARE `UInt64`,
 * AND THAT IS THE SAME 64 `cq_bit`s (D-K23-2). The reinterpret is WIRING; what
 * it changes is which comparator the next row reaches for, and EIGHT of M32's
 * nine ordered compares are SIGNED — K23 §5.4 has the per-site table.
 * Substituting `cq_ult_block` for `cq_slt_block` anywhere in that table leaves
 * the gate SHAPE, the palindrome, the slot scan and the scratch all correct
 * and the answer wrong exactly on the underflow path, which is K9's own
 * uge-meaning-ule in a new dress. The one UNSIGNED row, `grs > UInt64(4)` at
 * :210, is a DELIBERATELY EQUIVALENT MUTANT in the other direction: `grs` is
 * built from three one-bit inputs so it never goes negative, but
 * `cq_slt_steps > cq_ult_steps`, so L4 sees that substitution and L1 does not.
 *
 * SLOTS AND GATES COME APART, AS THEY DO IN M31 AND M36 (D-K18-6), AND HERE
 * THEY ALSO COME APART BETWEEN CALLERS (D-K23-7). §7.3's constants are
 * `const cq_bit` SOURCES and a mask or shift by a compile-time constant is a
 * VIEW, so classical lanes sit inside block operands at EVERY operand mask
 * including the all-quantum one L4 pins — 41 of K23's 154 rows are views. And
 * `fsqrt.jl:103` passes the literal `UInt64(0)` for `result_sign` where the
 * other four callers pass a computed span, so the same block costs different
 * GATES in different callers. Every `cq_*_steps` below is a SLOT count and is
 * never a gate count; every composition identity over this module is over
 * slots; L4 is pinned per (block, caller-shaped operand mask).
 *
 * THE BLOCKS ALLOCATE NOTHING (plan §0.4 obligation 2). `scr` is the CALLER's
 * one contiguous region and `off` is where this block's `cq_*_region()` bits
 * start inside it. Every input is a `const cq_bit *` CONTROL and may be a
 * rail, a scratch span, or a view that overlaps a region an earlier step wrote
 * (obligations 3 and 4; K12.md §2.1a) — sound because they reach the emitter
 * only through `cq_emit_*`'s const parameters, so none can ever be a target,
 * which is I6(a) by construction. Guards compare RANGES, never base pointers.
 *
 * ONE INVOLUTION PER SLOT, WHICH IS WHAT MAKES cq_sandwich's INDEX REVERSAL BE
 * GATE REVERSAL (bd ckd.14a, sandwich.h). Every branch of the dispatch emits
 * exactly one X, CX or CCX, or delegates to a step block that does the same.
 *
 * NO WIDTH PARAMETER ANYWHERE, for M31's reason: PRD-v2 §1 scopes v2 to `f64`,
 * so 64 is a constant here and not a ladder. A consumer running these at any
 * other width is asking for a fiction.
 *
 * FOUR TUs ON THREE SEAMS (K23 §5.8, D-K23-10), and the third seam was forced
 * by measurement rather than recorded in advance.
 *
 *   fpround.c        the four ROW TABLES and nothing else — the port a reader
 *                    checks against the Julia.
 *   fpround_step.c   the LAYOUT and the OPERANDS: what a row costs (asked of
 *                    M12, M14, M16 and M17), where its spans lie, and what
 *                    each operand code resolves to. Knows no Julia.
 *   fpround_emit.c   the DISPATCH and the SURFACE: which gate a slot emits,
 *                    and the four public blocks. `fpround_int.h` is the seam
 *                    between these two, and it exists because the step machine
 *                    measured 418 of Rule 12's 300 lines with both halves in
 *                    one file — M36's own second cut, one size up.
 *   fpround_eval.c   the four CLASSICAL bodies, which emit nothing.
 *
 * PRD-v2 §5's recorded PACK <-> ROUND seam predates the module taking four
 * helpers and does not partition it; the seams actually taken are ROW TABLES
 * <-> STEP MACHINE (M36's D-K18-7), LAYOUT-AND-OPERANDS <-> DISPATCH-AND-
 * SURFACE, and `_eval` <-> THE CIRCUIT.
 */
#ifndef CQOPS_KERNELS_FPROUND_H
#define CQOPS_KERNELS_FPROUND_H

#include "bit.h"
#include "ctx.h"
#include "kernels/fpfield.h"
#include "scratch.h"

#include <stdint.h>

/* --- The program: one row per operator occurrence (PRD-v2 §7.2). ---------- */

/* Each row is ONE upstream lowering function applied once, or a VIEW. The
 * blocks are M16's `cq_eq_step` (lower_eq!, arith.jl:424-447), `cq_ult_step`
 * (lower_ult!, :449-463) and `cq_slt_step` (lower_slt!, :465-472); M14's
 * `cq_sub_step` (lower_sub!, adder.jl:148-172) and `cq_add_step` (lower_add!,
 * :1-18); M17's `cq_mux_step` (lower_mux!, arith.jl:522-532); M12's
 * `cq_barrel_step` (lower_var_shl!/lshr!, :366-380, :350-364); and the three
 * bitwise ops emitted directly one gate per slot as PRD-v2 §7.10 directs —
 * `lower_and!` (:268-272), `lower_or!` (:274-282), `lower_not1!` (:474-478).
 *
 * `CQ_FROP_` AND NOT `CQ_FPR_`: M36 measured that a row-op enum colliding with
 * a neighbouring enum renumbers it silently (C enums share one namespace), so
 * the op names are deliberately unlike the block-id names below. */
typedef enum {
    CQ_FROP_VIEW = 0,  /* 0 slots, 0 bits: `(s0 >> shift) & mask`         */
    CQ_FROP_EQ,        /* 2 x 64 -> 1 bit; the RAW wire is `a != b`       */
    CQ_FROP_ULT,       /* 2 x 64 -> 1 bit; the RAW wire is `a >=u b`      */
    CQ_FROP_SLT,       /* 2 x 64 -> 1 bit; the RAW wire is `a >=s b`      */
    CQ_FROP_SUB,       /* 2 x 64 -> 64                                    */
    CQ_FROP_ADD,       /* 2 x 64 -> 64                                    */
    CQ_FROP_MUX,       /* cond(1) + 2 x 64 -> 64                          */
    CQ_FROP_AND,       /* 2 x 64 -> 64  (a RUNTIME mask; D-K23-3)         */
    CQ_FROP_OR,        /* 2 x 64 -> 64                                    */
    CQ_FROP_BSHL,      /* value(64), amount(64) -> 64                     */
    CQ_FROP_BLSHR,     /* value(64), amount(64) -> 64                     */
    CQ_FROP_NOT1,      /* 1 bit -> 1 bit                                  */
    CQ_FROP_AND1,      /* 2 x 1  -> 1 bit                                 */
    CQ_FROP_OR1,       /* 2 x 1  -> 1 bit                                 */
    CQ_FROP_N_OP
} cq_fpround_op;

/* An operand slot: `>= 0` names an earlier ROW of the same program, and each
 * negative code below names something that owns no qubit at all — one of the
 * block's three inputs, or a constant span (§7.3). A constant and a view are
 * only ever CONTROLS, so they reach the emitter through cq_emit_*'s
 * `const cq_bit *` parameters and can never be materialised: I6(a) by
 * construction.
 *
 * `K_D4` AND `K_FOUR` CARRY THE SAME BIT PATTERN AND ARE TWO CODES, because
 * §7.2's no-CSE rule is about OCCURRENCES; naming them apart is free and makes
 * the slot scan's constant-lane accounting readable (K23 §2.1). */
enum {
    CQ_FR_IN0        = -1,   /* `wr`, or `m`                               */
    CQ_FR_IN1        = -2,   /* `result_exp`, or `e`                       */
    CQ_FR_IN2        = -3,   /* `result_sign`                              */
    CQ_FR_K_ZERO     = -4,
    CQ_FR_K_ONE      = -5,
    CQ_FR_K_FOUR     = -6,   /* `grs > 4`, `grs == 4`      :210            */
    CQ_FR_K_56       = -7,   /* `shift_sub >= 56`          :177            */
    CQ_FR_K_63       = -8,   /* clamp's upper bound        :178            */
    CQ_FR_K_7FE      = -9,   /* clamp's upper bound        :223            */
    CQ_FR_K_7FF      = -10,  /* the overflow threshold     :200, :220      */
    CQ_FR_K_D32      = -11,  /* the six CLZ decrements                     */
    CQ_FR_K_D16      = -12,
    CQ_FR_K_D8       = -13,
    CQ_FR_K_D4       = -14,
    CQ_FR_K_D2       = -15,
    CQ_FR_K_D1       = -16,
    CQ_FR_K_IMPLICIT = -17,  /* softfloat_common.jl:9                      */
    CQ_FR_K_INF      = -18,  /* :11                                        */
    CQ_FR_N_CODE     = 18
};

/* `shift` is the RIGHT-shift amount and a NEGATIVE value is a left shift, so a
 * chain of views composes to one `(shift, mask)` pair — see fpround_step.c.
 * Both are read only when `op == CQ_FROP_VIEW`. */
typedef struct {
    short    op;             /* a cq_fpround_op                           */
    short    s0, s1, s2;     /* operands; only the first arity(op) read   */
    short    shift;          /* VIEW only                                 */
    uint64_t mask;           /* VIEW only                                 */
} cq_fpround_row;

typedef enum {
    CQ_FPR_NORM52 = 0,   /* _sf_normalize_to_bit52   :68-105,  48 rows */
    CQ_FPR_CLZ,          /* _sf_normalize_clz        :113-139, 42 rows */
    CQ_FPR_SUBNORM,      /* _sf_handle_subnormal     :174-191, 22 rows */
    CQ_FPR_ROUND,        /* _sf_round_and_pack       :198-227, 42 rows */
    CQ_FPR_N_BLOCK
} cq_fpround_id;

_Static_assert(CQ_FPR_NORM52 == 0 && CQ_FPR_N_BLOCK == 4,
               "the four row tables of fpround.c are indexed by this enum; "
               "adding one is a deliberate act and must break a build, not "
               "just a comment");

/* `_sf_normalize_to_bit52` is the longest table at 48 rows. Pinned so a table
 * that outgrows the prefix-offset buffer fails loudly rather than overrunning
 * a stack array, and asserted in fpround.c rather than commented. */
enum { CQ_FPROUND_MAX_ROWS = 48 };

/* The row table of one helper, in SOURCE ORDER. `n` receives the row count.
 * The table is static and shared: a row reference is an index into THIS table
 * and is never rebased, because a helper is one whole program (unlike M36,
 * where a predicate concatenates bodies). */
const cq_fpround_row *cq_fpround_rows(cq_fpround_id id, int *n);

/* How many of s0/s1/s2 a row reads. */
int cq_fpround_arity(int op);

/* Which ROW of `id`'s table holds output `which`, in the order this header
 * declares the accessors. A hard error on an unknown pair in both
 * configurations. Exposed so a test can name an output without repeating the
 * row index, which is exactly the number a boundary error moves. */
int cq_fpround_out_row(cq_fpround_id id, int which);

/* --- The four blocks. ----------------------------------------------------- */

/* `m` has no bits set above bit 52 (softfloat_common.jl:46-48) or is ZERO, in
 * which case the tpg0 guard at :69-74 and :100-103 returns `(0, e)` unchanged.
 * Both are 64 lanes and both are CONTROLS ONLY. */
typedef struct {
    const cq_bit *m, *e;
    cq_scratch   *scr;
    uint32_t      off;
} cq_norm52_block;

/* `wr`'s leading 1 is at or below bit 55; `result_exp` is an `Int64`. */
typedef struct {
    const cq_bit *wr, *result_exp;
    cq_scratch   *scr;
    uint32_t      off;
} cq_clz_block;

/* `wr` is the G/R/S working format, `result_exp` an `Int64`, `result_sign` a
 * `UInt64` of which only lane 0 is ever read (`result_sign << 63`, :183). */
typedef struct {
    const cq_bit *wr, *result_exp, *result_sign;
    cq_scratch   *scr;
    uint32_t      off;
} cq_subnorm_block;

typedef struct {
    const cq_bit *wr, *result_exp, *result_sign;
    cq_scratch   *scr;
    uint32_t      off;
} cq_round_block;

/* The bits of the caller's region each block owns, starting at `off` — the sum
 * over rows of each row's own internals, every term ASKED of its module.
 * Never written as a number here or in a test. */
uint32_t cq_norm52_region (void);
uint32_t cq_clz_region    (void);
uint32_t cq_subnorm_region(void);
uint32_t cq_round_region  (void);

/* The SLOT count: the sum over rows of each block's step count, asked of its
 * owning module at width 64. NOT a gate count — see the header note. */
int cq_norm52_steps (void);
int cq_clz_steps    (void);
int cq_subnorm_steps(void);
int cq_round_steps  (void);

/* One gate, `u` in `[0, cq_*_steps())`. Out of range is a hard error in BOTH
 * configurations, for cq_eq_step's reason: an off-by-one lands inside some
 * inner block's OR-prefix or carry chain and emits a plausible WRONG gate
 * rather than failing. */
void cq_norm52_step (cq_ctx *ctx, const cq_norm52_block  *k, int u);
void cq_clz_step    (cq_ctx *ctx, const cq_clz_block     *k, int u);
void cq_subnorm_step(cq_ctx *ctx, const cq_subnorm_block *k, int u);
void cq_round_step  (cq_ctx *ctx, const cq_round_block   *k, int u);

/* --- The outputs. One accessor per OUTPUT span, never "read span X inline". */

/* `_sf_normalize_to_bit52` returns `(m_final, e_final)` — :104. */
const cq_bit *cq_norm52_m(const cq_norm52_block *k);   /* 64 */
const cq_bit *cq_norm52_e(const cq_norm52_block *k);   /* 64 */

/* `_sf_normalize_clz` returns `(wr, result_exp)` — :138. */
const cq_bit *cq_clz_wr (const cq_clz_block *k);       /* 64 */
const cq_bit *cq_clz_exp(const cq_clz_block *k);       /* 64 */

/* `_sf_handle_subnormal` returns `(wr, result_exp, flushed_result, subnormal,
 * flush_to_zero)` — :190. `subnormal` and `flush_to_zero` are the RAW flags of
 * H1's and H3's `slt` blocks, and they are `const` because a consumer writing
 * into one would make M32's reverse half non-cancelling. `fdiv.jl:128`,
 * `fadd.jl:125` and `fmul.jl:207` all read both. */
const cq_bit *cq_subnorm_wr  (const cq_subnorm_block *k);   /* 64 */
const cq_bit *cq_subnorm_exp (const cq_subnorm_block *k);   /* 64 */
const cq_bit *cq_subnorm_flag(const cq_subnorm_block *k);   /*  1, `subnormal` */
const cq_bit *cq_subnorm_ftz (const cq_subnorm_block *k);   /*  1, `flush_to_zero` */

/* `flushed_result = result_sign << 63` (:183) is a VIEW over an operand, not a
 * scratch span, so it has no home in the caller's region and its accessor
 * ASSEMBLES it into a caller-supplied array (D-K23-8) — exactly as
 * `cq_fp_view_exp` does. Materialising it instead would cost 64 bits and a
 * 64-gate copy to buy a pointer, which is the shape §7.3 exists to refuse. */
void cq_subnorm_flushed(const cq_subnorm_block *k, cq_bit out[CQ_FP64_W]);

/* `_sf_round_and_pack` returns FOUR values — :226 — although its own docstring
 * at :194 names three. The CODE is the specification; the docstring is stale
 * upstream and every one of the five callers destructures four. */
const cq_bit *cq_round_normal          (const cq_round_block *k);  /* 64 */
const cq_bit *cq_round_overflow_result (const cq_round_block *k);  /* 64 */
const cq_bit *cq_round_exp_overflow    (const cq_round_block *k);  /*  1 */
const cq_bit *cq_round_exp_overflow_aft(const cq_round_block *k);  /*  1 */

/* --- The four CLASSICAL rows (D-K23-9, PRD-v2 §7.4). ---------------------- */

/* The SAME four bodies evaluated in C over `uint64_t`, for the R9
 * short-circuit at each consumer's entry. NEVER the host `double` operator
 * (§7.4's measurement), and NEVER an L1 oracle for this module: an oracle
 * sharing shape with the implementation is blind to what it gets wrong, which
 * is the Step 18 trap. PRD-v2 §7.16 decides L1 here is a hand-written IEEE-754
 * reference model instead (tests/support/fpref.h).
 *
 * Exported rather than left to the consumers because five modules would
 * otherwise transcribe the same thirty lines, which is K15 §5.4's argument
 * applied to the C side, where it is EASIER to get subtly wrong because
 * nothing structural checks it. M31's `cq_fp_class_eval` set the precedent.
 *
 * The exponents are `int64_t` and every arithmetic step inside goes through
 * `uint64_t`, because `rexp + 1` at INT64_MAX is UB in C and the circuit wraps
 * mod 2^64 without comment. */
void cq_normalize_to_bit52_eval(uint64_t *m, int64_t *e);
void cq_normalize_clz_eval     (uint64_t *wr, int64_t *rexp);
void cq_handle_subnormal_eval  (uint64_t *wr, int64_t *rexp, uint64_t rsign,
                                uint64_t *flushed, int *subnormal, int *ftz);
void cq_round_and_pack_eval    (uint64_t wr, int64_t rexp, uint64_t rsign,
                                uint64_t *normal, uint64_t *overflow,
                                int *exp_ovf, int *exp_ovf_after);

#endif /* CQOPS_KERNELS_FPROUND_H */
