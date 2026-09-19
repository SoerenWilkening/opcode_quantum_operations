/* src/kernels/fmul.h — M34, K16. `fmul` at f64: `soft_fmul` as a Rule 7
 * kernel. PRD-v2 §5's M34 row, §7.1-7.4, §7.6, §7.12, §7.16.
 *
 * Read docs/constructions/K16.md before changing anything here. Every gate is
 * a port: `soft_fmul` (third_party/bennett/src/softfloat/fmul.jl:14-215) and
 * the five shared helpers it calls (softfloat_common.jl:23-24, :68-105,
 * :113-139, :174-191, :198-227), transcribed ONE ROW PER OPERATOR OCCURRENCE
 * in source order (§7.2's literal grain) over the step blocks M14, M16, M17,
 * M18 and M32 already export, plus upstream's own four-gate bitwise
 * vocabulary. Nothing in this module is a construction of ours.
 *
 * THE SHAPE IS M36's AND M32's: A ROW TABLE PLUS A STEP MACHINE. A row is one
 * upstream lowering function applied once, or a VIEW, or a projection of a
 * hand-off's tuple. The kernel is ONE cq_sandwich over ONE flat region whose
 * step function dispatches a global index into those blocks (PRD-v2 §7.1,
 * src/kernels/divrem_u.c's shape).
 *
 * FOUR SEAMS WERE RECORDED BEFORE A LINE WAS WRITTEN AND THREE WERE TAKEN.
 *
 *   fmul.c        the ROW TABLE and nothing else — the port a reader checks
 *                 against the Julia.
 *   fmul_step.c   the LAYOUT and the OPERANDS: what a row costs (ASKED of
 *                 M14/M16/M17/M18/M32), where its spans lie, and what each
 *                 operand code resolves to. Knows no Julia.
 *   fmul_emit.c   the DISPATCH and the SURFACE: which gate a slot emits, the
 *                 public block, and the Rule 7 entry point. `fmul_int.h` is
 *                 the seam between these two (M32's D-K23-10).
 *   fmul_eval.c   the CLASSICAL body, which emits nothing (M32's `_eval` <->
 *                 THE CIRCUIT seam).
 *
 * PRD-v2 §5's FOURTH recorded seam — SIGNIFICAND PRODUCT <-> EXPONENT/PACK at
 * `fmul.jl:136` — is NOT taken as a FILE seam, and that is a decision with two
 * grounds rather than a line count. (i) Under the row-table shape the port is
 * 150 table lines, so the seam does not bind Rule 12 and splitting the table
 * would put ONE prefix sum in two files, which K16.md §2.2's own seam box
 * refuses ("the prefix-sum table stays in one place"). (ii) Its stated purpose
 * was M39's reuse of `(prod_hi_final, prod_lo_final)`, and K20.md retired that
 * on 2026-09-18: `fma`'s product is `_sf_widemul_u64_to_128`'s 32/32 split and
 * not `fmul`'s 27/26, so "M33/M34 owe it nothing". The seam survives as a
 * NAMED ROW BOUNDARY — `cq_fmul_seam_row()` below — with a case pinning what
 * crosses it, which is the claim the file split was standing in for. M32's own
 * PACK <-> ROUND row was retired the same way and for the same kind of reason.
 *
 * SLOTS AND GATES COME APART, AS THEY DO IN M31, M32 AND M36 (D-K18-6).
 * §7.3's constants are `const cq_bit` SOURCES and a mask or shift by a
 * compile-time constant is a VIEW, so classical lanes sit inside block
 * operands at EVERY operand mask including the all-quantum one L4 pins — 35 of
 * K16's 150 rows are views. `cq_fmul_steps` is a SLOT count and is never a
 * gate count; every composition identity over this module is over slots.
 *
 * W MUST BE 64. PRD-v2 §1 scopes v2 to `f64` and `soft_fmul` is
 * `(UInt64, UInt64)`, so any other width is a fiction rather than an
 * unimplemented case, and is a hard error in BOTH configurations.
 */
#ifndef CQOPS_KERNELS_FMUL_H
#define CQOPS_KERNELS_FMUL_H

#include "bit.h"
#include "ctx.h"
#include "kernels/fpfield.h"
#include "scratch.h"

#include <stdint.h>

/* --- The program: one row per operator occurrence (PRD-v2 §7.2). ---------- */

/* Each row is ONE upstream lowering function applied once, a VIEW, or a
 * projection. The blocks are M16's `cq_eq_step` (lower_eq!, arith.jl:424-447)
 * and `cq_ult_step` (lower_ult!, :449-463); M14's `cq_add_step` (lower_add!,
 * adder.jl:1-18) and `cq_sub_step` (lower_sub!, :148-172); M17's `cq_mux_step`
 * (lower_mux!, arith.jl:522-532); M18's `cq_mul_step` (lower_mul!,
 * multiplier.jl:1-33); M32's four shared-helper blocks; and the four bitwise
 * ops emitted directly one gate per slot as PRD-v2 §7.10 directs —
 * `lower_or!` (arith.jl:274-282), `lower_xor!` (:284-291), `lower_and!`
 * (:268-272) and `lower_not1!` (:474-478).
 *
 * THE SIX CLASS PREDICATES COMPOSE M31's BLOCK AND ARE NOT TRANSCRIBED AGAIN.
 * `fmul.jl:31-36` is `fadd.jl:29-34` row for row and is exactly M31's `is_nan`
 * / `is_inf` / `is_zero` (K22.md), so K16 spends six `CQ_FMOP_CLASS` rows on
 * them instead of the twenty-eight `eq` + `not1` + `and1` rows the literal
 * grain would give. The SLOTS AND THE BITS ARE IDENTICAL EITHER WAY —
 * `cq_fp_class_steps` is `2·cq_eq_steps(64) + 2·n_eq + 1` and
 * `cq_fp_class_region` is `2·cq_eq_region(64) + n_eq + 1`, which is the same
 * 3,830 slots and 1,540 bits — so this is not a cost decision. It is Rule 1
 * applied to the call graph: M31's suite already owns "is
 * `(ea == 0x7FF) & (fa != 0)` the right NaN test", and a second copy here
 * would be a second source of truth for one predicate, which is the mistake
 * M12-over-M17 is the standing witness against. M36 composes the same block
 * for the same reason.
 *
 * `CQ_FMOP_` AND NOT `CQ_FMUL_`: M36 measured that a row-op enum colliding
 * with a neighbouring enum renumbers it silently (C enums share one
 * namespace), so the op names are deliberately unlike everything else here. */
typedef enum {
    CQ_FMOP_VIEW = 0,  /* 0 slots, 0 bits: `(s0 >> shift) & mask`         */
    CQ_FMOP_OUT,       /* 0 slots, 0 bits: output `s1` of hand-off `s0`   */
    CQ_FMOP_CLASS,     /* M31: `s1` is a cq_fp_class over the 64-lane s0  */
    CQ_FMOP_EQ,        /* 2 x 64 -> 1 bit; the RAW wire is `a != b`       */
    CQ_FMOP_ULT,       /* 2 x 64 -> 1 bit; the RAW wire is `a >=u b`      */
    CQ_FMOP_ADD,       /* 2 x 64 -> 64                                    */
    CQ_FMOP_SUB,       /* 2 x 64 -> 64                                    */
    CQ_FMOP_MUX,       /* cond(1) + 2 x 64 -> 64                          */
    CQ_FMOP_OR,        /* 2 x 64 -> 64                                    */
    CQ_FMOP_XOR,       /* 2 x 64 -> 64                                    */
    CQ_FMOP_MUL,       /* 2 x 64 -> 64, the low W bits (M18, K11)         */
    CQ_FMOP_NORM52,    /* _sf_normalize_to_bit52  -> (m, e)               */
    CQ_FMOP_CLZ,       /* _sf_normalize_clz       -> (wr, exp)            */
    CQ_FMOP_SUBNORM,   /* _sf_handle_subnormal    -> five values          */
    CQ_FMOP_ROUND,     /* _sf_round_and_pack      -> four values          */
    CQ_FMOP_NOT1,      /* 1 bit -> 1 bit                                  */
    CQ_FMOP_AND1,      /* 2 x 1  -> 1 bit                                 */
    CQ_FMOP_OR1,       /* 2 x 1  -> 1 bit                                 */
    CQ_FMOP_N_OP
} cq_fmul_op;

_Static_assert(CQ_FMOP_VIEW == 0,
               "a zero-initialised row must be the free one; a renumbering "
               "must break a build, not just a comment");

/* An operand slot: `>= 0` names an earlier ROW of the same program, and each
 * negative code below names something that owns no qubit at all — a rail or a
 * constant span (§7.3). A constant and a view are only ever CONTROLS, so they
 * reach the emitter through cq_emit_*'s `const cq_bit *` parameters and can
 * never be materialised: I6(a) by construction.
 *
 * THE MASKS ARE NOT HERE, AND THAT IS PRD-v2 §7.3 AS AMENDED.
 * `FRAC_MASK`, `0x03FFFFFF`, `(1<<42)-1`, `(1<<50)-1`, `(1<<49)-1` and
 * `(1<<56)-1` are VIEW masks — wiring — not operands of an `and` block. Only
 * the constants that are genuine block operands are codes here.
 *
 * `0x7FF` IS BOTH, AND THAT IS THE ONE CONSTANT IN THIS FILE THAT NEEDS
 * SAYING TWICE. At `:20` and `:24` it is the MASK of `(a >> 52) & 0x7FF` and
 * is wiring; at `:31`, `:32`, `:33` and `:34` it is the right OPERAND of an
 * `==` and is a real 64-lane constant span. A port that carried only the mask
 * reading would compare `ea` against nothing; one that carried only the
 * operand reading would spend four `and`(64) blocks on the unpack. */
enum {
    CQ_FM_A          = -1,   /* the rail `a`                    fmul.jl:14  */
    CQ_FM_B          = -2,   /* the rail `b`                                */
    CQ_FM_K_ZERO     = -3,   /* `UInt64(0)`                     :31-36, :44 */
    CQ_FM_K_ONE      = -4,   /* `UInt64(1)` / `Int64(1)`        :48, :97    */
    CQ_FM_K_7FF      = -5,   /* `UInt64(0x7FF)`                 :31-34      */
    CQ_FM_K_BIAS     = -6,   /* `BIAS = Int64(1023)`            :16, :63    */
    CQ_FM_K_IMPLICIT = -7,   /* softfloat_common.jl:9           :44, :45    */
    CQ_FM_K_INF      = -8,   /* softfloat_common.jl:11          :40         */
    CQ_FM_K_QUIET    = -9,   /* softfloat_common.jl:13          :212        */
    CQ_FM_K_INDEF    = -10,  /* softfloat_common.jl:14          :210        */
    CQ_FM_N_CODE     = 10
};

/* `shift` is the RIGHT-shift amount and a NEGATIVE value is a left shift, so a
 * chain of views composes to one `(shift, mask)` pair — see fmul_step.c. Both
 * are read only when `op == CQ_FMOP_VIEW`; `s1` doubles as the output index of
 * a `CQ_FMOP_OUT` row. */
typedef struct {
    short    op;             /* a cq_fmul_op                              */
    short    s0, s1, s2;     /* operands; only the first arity(op) read   */
    short    shift;          /* VIEW only                                 */
    uint64_t mask;           /* VIEW only                                 */
} cq_fmul_row;

/* The outputs of the four M32 hand-offs, in the order softfloat_common.jl
 * returns them — :104, :138, :190 and :226. `_sf_round_and_pack` returns FOUR
 * although its docstring at :194 names three; the CODE is the specification
 * (K16.md §1.4 point 2). A `CQ_FMOP_OUT` row's `s1` is one of these. */
enum {
    CQ_FM_OUT_M          = 0, /* norm52  :104  m_final                     */
    CQ_FM_OUT_E          = 1, /* norm52  :104  e_final                     */
    CQ_FM_OUT_WR         = 0, /* clz/subnorm   wr                          */
    CQ_FM_OUT_EXP        = 1, /* clz/subnorm   result_exp                  */
    CQ_FM_OUT_FLUSHED    = 2, /* subnorm :190  flushed_result (a VIEW)     */
    CQ_FM_OUT_SUBNORMAL  = 3, /* subnorm :190  subnormal       (1 bit)     */
    CQ_FM_OUT_FTZ        = 4, /* subnorm :190  flush_to_zero   (1 bit)     */
    CQ_FM_OUT_NORMAL     = 0, /* round   :226  normal_result               */
    CQ_FM_OUT_OVERFLOW   = 1, /* round   :226  overflow_result             */
    CQ_FM_OUT_EXPOVF     = 2, /* round   :226  exp_overflow    (1 bit)     */
    CQ_FM_OUT_EXPOVF_AFT = 3  /* round   :226  ..._after_round (1 bit)     */
};

/* `soft_fmul` is 150 rows. Pinned so a table that outgrows the prefix-offset
 * buffer fails loudly rather than overrunning a stack array, and asserted in
 * fmul.c rather than commented. */
enum { CQ_FMUL_MAX_ROWS = 160 };

/* The one program. `n` receives the row count. A row reference is an index
 * into THIS table and is never rebased: `soft_fmul` is one whole routine,
 * unlike M36 where a predicate concatenates bodies. */
const cq_fmul_row *cq_fmul_rows(int *n);
int                cq_fmul_n_rows(void);

/* How many of s0/s1/s2 a row reads. A VIEW reads one and an OUT reads `s0`
 * as a row plus `s1` as an output INDEX, which is not an operand. */
int cq_fmul_arity(int op);

/* The width of row `i`'s value: 64, 1, or 0 for a hand-off row, whose value is
 * a TUPLE and which may only be named through a `CQ_FMOP_OUT` row. */
int cq_fmul_row_width(const cq_fmul_row *rows, int n, int i);

/* PRD-v2 §5's `fmul.jl:136` seam, as a NAMED ROW BOUNDARY rather than a file
 * split (see the header note). It is the index of the FIRST row below the
 * seam — `prod_hi_final >> 41` at :156 — so rows [0, seam) are the SIGNIFICAND
 * PRODUCT half and the rest are EXPONENT/PACK. Exported so a test can pin what
 * crosses it without repeating the row index, which is exactly the number a
 * boundary error moves. */
int cq_fmul_seam_row(void);

/* --- The block. ---------------------------------------------------------- */

/* `a` and `b` are CQ_FP64_W lanes each and are CONTROLS ONLY. THE BLOCK
 * ALLOCATES NOTHING: the caller supplies `scr` and `off` and every span is
 * `off`-relative, which is load-bearing rather than tidy — a block cannot see
 * its own offset, so dropping `off` slides the whole program inside the
 * caller's region and leaves the value, the palindrome, the pool, every gate
 * count AND the slot scan all correct (M18's and M31's measured finding). Only
 * a SECOND program at a SECOND offset in one region sees it. */
typedef struct {
    const cq_bit *a, *b;
    cq_scratch   *scr;
    uint32_t      off;
} cq_fmul_block;

/* How many bits of the caller's region the program owns, starting at `off` —
 * the sum over rows of each row's own internals, every term ASKED of its
 * module. Never written as a number here or in a test. */
uint32_t cq_fmul_region(void);

/* The SLOT count: the sum over rows of each block's step count, asked of its
 * owning module at width 64. ONE INVOLUTION PER SLOT, which is what makes
 * cq_sandwich's index reversal be gate reversal. NOT a gate count. */
int cq_fmul_steps(void);

/* One gate of the program, `u` in [0, cq_fmul_steps()). Out of range is a hard
 * error in BOTH configurations, for cq_eq_step's reason: an off-by-one lands
 * inside some inner block's OR-prefix, carry chain or partial-product schedule
 * and emits a plausible wrong gate rather than failing. */
void cq_fmul_step(cq_ctx *ctx, const cq_fmul_block *k, int u);

/* `result` (fmul.jl:214) — the last row's span, 64 lanes, inside the caller's
 * own region. `const`, for cq_mul_product's reason: inside one compute half it
 * is a CONTROL for whatever comes next, and a consumer that WROTE into it
 * would make the reverse half non-cancelling. */
const cq_bit *cq_fmul_result(const cq_fmul_block *k);

/* Risk R9's classical row: the SAME Julia body evaluated in C over `uint64_t`
 * (PRD-v2 §7.4), composing M32's four `*_eval` bodies, NEVER the host `double`
 * `*`. Four of §7.4's five IEEE-unspecified cells are reachable from `fmul`
 * and every one of them is x86's choice; this way the classical and quantum
 * modes agree bit-for-bit on every host. It is NOT an L1 oracle — that is the
 * host operator with those cells pinned by table (tests/support/fphost.h). */
uint64_t cq_fmul_eval(uint64_t a, uint64_t b);

/* --- The Rule 7 kernel. --------------------------------------------------- */

/* `dst ^= soft_fmul(a, b)`, with `a` and `b` 64 bits and unchanged and every
 * internal ancilla back at |0>. `W` must be 64. There is no `_unc` entry point
 * (uncompute is the same call) and no `_controlled` variant (the axis is an
 * emitter mode). Rule 7's canonical shape, unchanged, so it is storable in a
 * `cq_kernel_fn` and the shared Phase-B driver drives it with no adapter. */
void cq_kernel_fmul(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W);

#endif /* CQOPS_KERNELS_FMUL_H */
