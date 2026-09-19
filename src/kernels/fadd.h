/* src/kernels/fadd.h — M33, K15. `fadd` / `fsub` at f64, Rule 7's canonical
 * two-source one-width kernel. PRD-v2 §5's M33 row, §7.1-7.4, §7.6, §7.11,
 * §7.12; docs/constructions/K15.md.
 *
 * Read K15.md before changing anything here. Every gate is a port: `soft_fadd`
 * (third_party/bennett/src/softfloat/fadd.jl:16-136), `soft_fsub`
 * (fsub.jl:19-25) and `soft_fneg` (fneg.jl:6), transcribed ONE ROW PER
 * OPERATOR OCCURRENCE in source order (§7.2's literal grain) over the step
 * blocks M12, M14, M16, M17, M31 and M32 already export. Nothing in this
 * module is a construction of ours.
 *
 * `fsub` IS NOT `fadd(a, fneg(b))`, AND THE DIFFERENCE IS ONE CELL OF THE
 * INPUT SPACE (PRD-v2 §5's M33 row; `Bennett-m63k`). fsub.jl:8-12 states it:
 * "unconditionally applying `soft_fneg` to a NaN operand would flip its sign
 * bit before `soft_fadd`'s NaN-input passthrough sees it. The fix routes
 * NaN-`b` through `soft_fadd` unchanged so the propagated result preserves the
 * operand's original sign." So `soft_fsub(a,b)` is
 * `soft_fadd(a, ifelse(b_is_nan, b, b ^ SIGN_MASK))` and the guard recomputes
 * the NaN test from `b` with fsub.jl:21's OWN spelling — `(b & EXP_MASK) >> 52`
 * where fadd.jl:25 writes `(b >> 52) & 0x7FF`. Under §7.2 those are two
 * different operator sequences and BOTH are transcribed; sharing them is CSE.
 * The whole of the fix is visible below as the FSUB PROLOGUE's last row
 * becoming the `b` every later row reads, including fadd.jl:133's
 * `_sf_propagate_nan2(a, b, ...)`.
 *
 * ONE SANDWICH, ONE FLAT REGION, AND `fsub` IS NOT A SECOND KERNEL. A kernel
 * is a whole `cq_sandwich` and the driver refuses nesting in both
 * configurations, so `cq_kernel_fsub` cannot call `cq_kernel_fadd`: the fadd
 * body is the SAME ROW PROGRAM run under fsub's prologue, assembled by
 * `cq_fadd_program` with the `b` operand code rebased onto the prologue's
 * `b_eff` row. That is the composite-kernels-call-the-step-function rule M12
 * is the standing witness for, applied to a program rather than to a block.
 *
 * SLOTS AND GATES COME APART, AS THEY DO IN M31, M32 AND M36 (D-K18-6). §7.3's
 * constants are `const cq_bit` SOURCES and a mask or shift by a compile-time
 * constant is a VIEW, so classical lanes sit inside block operands at EVERY
 * operand mask including the all-quantum one L4 pins. `cq_fsub_steps` is a
 * SLOT count and is never a gate count; every composition identity over this
 * module is over slots, and the slot scan's prediction is four-valued
 * {NONE, X, CX, CCX} walked with its own stream cursor.
 *
 * THE FOUR ORDERED COMPARES IN `soft_fadd` ARE ALL UNSIGNED, AND SAYING SO IS
 * THE POINT (§7.6's two signednesses). K15.md's draft put four SIGNED compares
 * in its phases K and L; PRD-v2 §5 then gave `_sf_handle_subnormal` and
 * `_sf_round_and_pack` to M32, so all four went with them and M33's own body
 * keeps only `a_mag < b_mag` (:49), `d >= 64` (:76), `d >= 56` (:81) and
 * `d > 0` (:82) — every one of them on a `UInt64`, every one through
 * `cq_ult_block`. Substituting `cq_slt_block` here is a RECORDED EQUIVALENT
 * MUTANT rather than a miscompile (`a_mag` and `b_mag` have bit 63 clear by
 * construction and `d = ea_eff - eb_eff` is non-negative because the swap at
 * :49-56 puts the larger magnitude first), and it is L4 — not L1 — that sees
 * it, because `cq_slt_steps(64) > cq_ult_steps(64)`. The genuinely wrong
 * substitution lives in M32, where eight of nine ordered compares are signed.
 *
 * THE TWO HAND-OFF ENCODINGS ARE M32's AND NO C TYPE EXPRESSES THEM (K23 §1.7).
 * `wr` crosses into `cq_clz_block`, `cq_subnorm_block` and `cq_round_block` in
 * the G/R/S working format fadd.jl:13-14 states: "mantissa shifted left by 3
 * for guard/round/sticky bits. bit 55 = implicit 1, bits 54-3 = 52-bit
 * fraction, bits 2/1/0 = G/R/S." Build `wr` wrong by one lane and every input
 * that does not round is still right — the tie anchors are the only detector.
 *
 * SIX TUs ON FOUR SEAMS, THREE OF THEM RECORDED IN ADVANCE AND ONE RETIRED.
 *
 *   fadd.c        the ROW TABLES and nothing else — the port a reader checks
 *                 against the Julia, plus the program assembly that rebases
 *                 the fadd body onto fsub's `b_eff`.
 *   fadd_step.c   the LAYOUT and the OPERANDS: what a row costs (asked of M12,
 *                 M14, M16, M17, M31 and M32), where its spans lie, and what
 *                 each operand code resolves to. Knows no Julia.
 *   fadd_emit.c   the DISPATCH and the SURFACE: which gate a slot emits, the
 *                 exported `cq_fsub_block`, and the two Rule 7 kernels.
 *                 `fadd_int.h` is the seam between these two.
 *   fadd_eval.c   the two CLASSICAL bodies, which emit nothing.
 *
 * PRD-v2 §5's recorded ALIGN <-> ADD-AND-NORMALISE seam is RETIRED as a FILE
 * seam and KEPT as a marked boundary inside fadd.c's table (rows 0-63 against
 * 64-123, i.e. fadd.jl:83 against :86). K15.md's own seam box gives the reason
 * it cannot be a file cut: "The prefix-sum table stays in one place, because
 * two tables is two chances to get §2.5 wrong." M32 retired its recorded seam
 * the same way and for the same reason — a row-table module partitions by ROW
 * TABLE / STEP MACHINE / DISPATCH / _eval, not by which line of the source a
 * row came from.
 *
 * NO WIDTH PARAMETER INSIDE, for M31's and M32's reason: PRD-v2 §1 scopes v2
 * to `f64`, so 64 is a constant here and not a ladder. The two Rule 7 entry
 * points still take `W` because `cq_kernel_fn` does, and a `W` that is not 64
 * is a hard error in BOTH configurations rather than a fiction.
 */
#ifndef CQOPS_KERNELS_FADD_H
#define CQOPS_KERNELS_FADD_H

#include "bit.h"
#include "ctx.h"
#include "kernels/fpfield.h"
#include "scratch.h"

#include <stdint.h>

/* --- The program: one row per operator occurrence (PRD-v2 §7.2). ---------- */

/* Each row is ONE upstream lowering function applied once, one hand-off to a
 * shared helper, or a piece of wiring. The blocks are M16's `cq_eq_step`
 * (lower_eq!, arith.jl:424-447) and `cq_ult_step` (lower_ult!, :449-463);
 * M14's `cq_sub_step` (lower_sub!, adder.jl:148-172) and `cq_add_step`
 * (lower_add!, :1-18); M17's `cq_mux_step` (lower_mux!, arith.jl:522-532);
 * M12's `cq_barrel_step` (lower_var_shl!/lshr!, :366-380, :350-364); M31's
 * `cq_fp_class_step`; M32's `cq_clz_step`, `cq_subnorm_step` and
 * `cq_round_step`; and the four bitwise ops emitted directly one gate per slot
 * as PRD-v2 §7.10 directs — `lower_and!` (:268-272), `lower_or!` (:274-282),
 * `lower_xor!` (:284-291) and `lower_not1!` (:474-478).
 *
 * `CQ_FAOP_` AND NOT `CQ_FA_`: M36 measured that a row-op enum colliding with
 * a neighbouring enum renumbers it silently (C enums share one namespace), so
 * the op names are deliberately unlike the operand-code names below. */
typedef enum {
    CQ_FAOP_VIEW = 0,  /* 0 slots, 0 bits: `(s0 >> shift) & mask`          */
    CQ_FAOP_PICK,      /* 0 slots, 0 bits: output `shift` of block row s0  */
    CQ_FAOP_CLASS,     /* 1 x 64 -> 1 bit; `shift` is the cq_fp_class      */
    CQ_FAOP_EQ,        /* 2 x 64 -> 1 bit; the RAW wire is `a != b`        */
    CQ_FAOP_ULT,       /* 2 x 64 -> 1 bit; the RAW wire is `a >=u b`       */
    CQ_FAOP_SUB,       /* 2 x 64 -> 64                                     */
    CQ_FAOP_ADD,       /* 2 x 64 -> 64                                     */
    CQ_FAOP_MUX,       /* cond(1) + 2 x 64 -> 64                           */
    CQ_FAOP_AND,       /* 2 x 64 -> 64  (a RUNTIME mask; :78 is the one)   */
    CQ_FAOP_OR,        /* 2 x 64 -> 64                                     */
    CQ_FAOP_XOR,       /* 2 x 64 -> 64  (soft_fneg, fneg.jl:6)             */
    CQ_FAOP_BSHL,      /* value(64), amount(64) -> 64                      */
    CQ_FAOP_BLSHR,     /* value(64), amount(64) -> 64                      */
    CQ_FAOP_NOT1,      /* 1 bit -> 1 bit                                   */
    CQ_FAOP_AND1,      /* 2 x 1  -> 1 bit                                  */
    CQ_FAOP_OR1,       /* 2 x 1  -> 1 bit                                  */
    CQ_FAOP_CLZ,       /* M32 _sf_normalize_clz    (wr, rexp)              */
    CQ_FAOP_SUBNORM,   /* M32 _sf_handle_subnormal (wr, rexp, rsign)       */
    CQ_FAOP_ROUND,     /* M32 _sf_round_and_pack   (wr, rexp, rsign)       */
    CQ_FAOP_N_OP
} cq_fadd_op;

/* A PICK NAMES ONE ELEMENT OF A DESTRUCTURED TUPLE, AND IT IS WIRING EXACTLY
 * AS A VIEW IS. fadd.jl:111, :114-115 and :118-119 bind two, five and four
 * names out of three calls; each name is a row so the table reads one-to-one
 * against the source and so every later row can reference it by index. A PICK
 * costs 0 slots and 0 bits: what it resolves to is the callee's own accessor.
 * `shift` carries the index, in the order fpround.h declares the accessors. */
enum {
    CQ_FA_PICK_WR       = 0,   /* clz / subnorm                            */
    CQ_FA_PICK_EXP      = 1,   /* clz / subnorm                            */
    CQ_FA_PICK_FLAG     = 2,   /* subnorm `subnormal`      (ONE bit)       */
    CQ_FA_PICK_FTZ      = 3,   /* subnorm `flush_to_zero`  (ONE bit)       */
    CQ_FA_PICK_FLUSHED  = 4,   /* subnorm `flushed_result` (assembled view)*/
    CQ_FA_PICK_NORMAL   = 0,   /* round `normal_result`                    */
    CQ_FA_PICK_OVFRES   = 1,   /* round `overflow_result`                  */
    CQ_FA_PICK_EXPOVF   = 2,   /* round `exp_overflow`     (ONE bit)       */
    CQ_FA_PICK_EXPOVFA  = 3    /* round `..._after_round`  (ONE bit)       */
};

/* An operand slot: `>= 0` names an earlier ROW of the same program, and each
 * negative code below names something that owns no qubit at all — one of the
 * two rails or a constant span (§7.3). A constant and a view are only ever
 * CONTROLS, so they reach the emitter through cq_emit_*'s `const cq_bit *`
 * parameters and can never be materialised: I6(a) by construction.
 *
 * `CQ_FA_B` IS THE ONE CODE A PROGRAM MAY REBASE. `cq_fadd_program` replaces
 * every occurrence of it with the fsub prologue's `b_eff` row, which is how
 * one row table serves both kernels and how `Bennett-m63k`'s guard reaches
 * fadd.jl:133's `_sf_propagate_nan2(a, b, ...)` as well as the arithmetic. */
enum {
    CQ_FA_A          = -1,
    CQ_FA_B          = -2,
    CQ_FA_K_ZERO     = -3,
    CQ_FA_K_ONE      = -4,   /* :73, :76, :78, :99, :106                   */
    CQ_FA_K_56       = -5,   /* fadd.jl:81                                 */
    CQ_FA_K_63       = -6,   /* fadd.jl:76                                 */
    CQ_FA_K_64       = -7,   /* fadd.jl:76                                 */
    CQ_FA_K_7FF      = -8,   /* fsub.jl:23                                 */
    CQ_FA_K_IMPLICIT = -9,   /* softfloat_common.jl:9,  fadd.jl:59-60      */
    CQ_FA_K_INDEF    = -10,  /* softfloat_common.jl:14, fadd.jl:39         */
    CQ_FA_K_QUIET    = -11,  /* softfloat_common.jl:13, :24                */
    CQ_FA_K_SIGN     = -12,  /* fneg.jl:6                                  */
    CQ_FA_N_CODE     = 12
};

/* `shift` is the RIGHT-shift amount and a NEGATIVE value is a left shift, so a
 * chain of views composes to one `(shift, mask)` pair — see fadd_step.c. On a
 * PICK it is the output index and on a CLASS it is the `cq_fp_class`; `mask`
 * is read only by a VIEW. */
typedef struct {
    short    op;             /* a cq_fadd_op                              */
    short    s0, s1, s2;     /* operands; only the first arity(op) read   */
    short    shift;          /* VIEW / PICK / CLASS only                  */
    uint64_t mask;           /* VIEW only                                 */
} cq_fadd_row;

/* `fsub` is the longer program at 133 rows. Pinned so a table that outgrows
 * the prefix-offset buffer fails loudly rather than overrunning a stack
 * array, and asserted in fadd.c rather than commented. */
enum { CQ_FADD_MAX_ROWS = 144 };

typedef enum { CQ_FADD_PROG_ADD = 0, CQ_FADD_PROG_SUB } cq_fadd_prog;

/* The whole program for one kernel, written into `out` (CQ_FADD_MAX_ROWS
 * entries) and returning the row count. For `SUB` this is fsub.jl's nine-row
 * prologue followed by soft_fadd's body with every internal row reference
 * shifted and every `CQ_FA_B` rebased onto the prologue's `b_eff`. */
int cq_fadd_program(cq_fadd_prog p, cq_fadd_row *out);

/* soft_fadd's own table, unrebased, and fsub.jl's prologue — for a reader and
 * for the tests that check the two programs against each other. */
const cq_fadd_row *cq_fadd_body_rows(int *n);
const cq_fadd_row *cq_fsub_prologue_rows(int *n);

/* How many of s0/s1/s2 a row reads. */
int cq_fadd_arity(int op);

/* Which ROW of `p`'s program holds its result. Exposed so a test can name the
 * answer without repeating the row index, which is exactly the number a
 * boundary error moves. */
int cq_fadd_result_row(cq_fadd_prog p);

/* --- The exported compute half (bd 9ve.23 / K19: fptoui composes it). ----- */

/* `soft_fsub(a, b)` as a step block, on the shipped block shape. K19's
 * `soft_fptoui` calls it at fptoui.jl:45 with `b` the constant KBIAS — the one
 * call site where fsub and fadd(fneg) coincide, and therefore the one site
 * where PRD-v2 §5's prohibition is invisible. It is exported anyway, because a
 * consumer that reached for `fadd(a, fneg(b))` there would have built the
 * machinery to get it wrong everywhere else.
 *
 * `a` and `b` are CQ_FP64_W lanes each and are CONTROLS ONLY — they may be
 * rails, scratch spans, or views that overlap a region an earlier step wrote
 * (plan §0.4 obligations 2, 3 and 4). THE BLOCK ALLOCATES NOTHING: the caller
 * supplies `scr` and `off` and every span is `off`-relative, which is
 * load-bearing rather than tidy — a block cannot see its own offset, so
 * dropping `off` slides the whole program inside the caller's region and
 * leaves the value, the palindrome and the pool all correct (M31's measured
 * finding; the two-blocks-in-one-region case is the sole detector). */
typedef struct {
    const cq_bit *a, *b;
    cq_scratch   *scr;
    uint32_t      off;
} cq_fsub_block;

/* The bits of the caller's region this block owns, starting at `off` — the sum
 * over rows of each row's own internals, every term ASKED of its module.
 * Never written as a number here or in a test. */
uint32_t cq_fsub_region(void);

/* The SLOT count: the sum over rows of each block's step count, asked of its
 * owning module at width 64. ONE INVOLUTION PER SLOT, which is what makes
 * cq_sandwich's index reversal be gate reversal. NOT a gate count. */
int cq_fsub_steps(void);

/* One gate, `u` in `[0, cq_fsub_steps())`. Out of range is a hard error in
 * BOTH configurations, for cq_eq_step's reason: an off-by-one lands inside
 * some inner block's OR-prefix or carry chain and emits a plausible WRONG gate
 * rather than failing. */
void cq_fsub_step(cq_ctx *ctx, const cq_fsub_block *k, int u);

/* The 64-lane result span, inside the caller's own region. */
const cq_bit *cq_fsub_result(const cq_fsub_block *k);

/* --- Risk R9's classical rows (PRD-v2 §7.4). ----------------------------- */

/* The SAME two bodies evaluated in C over `uint64_t`, composing M32's four
 * `*_eval` helpers — NEVER the host `double` operator. §7.4 measured why: five
 * IEEE-unspecified cells (`Inf - Inf`, the two NaN-payload orders, sNaN
 * quietening) are x86's choices and ARM answers three of them differently, so
 * a host-operator short-circuit would give a program whose classical mode and
 * quantum mode return different bits on the same input, on some hosts only.
 *
 * NOT an L1 oracle. An oracle sharing shape with the implementation is blind
 * to exactly what that implementation gets wrong (the Step 18 trap); L1's
 * reference is the host `+` / `-` with those cells pinned by TABLE from
 * tests/support/fphost.h. */
uint64_t cq_fadd_eval(uint64_t a, uint64_t b);
uint64_t cq_fsub_eval(uint64_t a, uint64_t b);

/* --- The two Rule 7 kernels. --------------------------------------------- */

/* `dst ^= a (+|-) b` over IEEE binary64 bit patterns, with `a` and `b` 64 bits
 * and unchanged and every internal ancilla back at |0>. `W` must be 64. There
 * is no `_unc` entry point (uncompute is the same call) and no `_controlled`
 * variant (the axis is an emitter mode). */
void cq_kernel_fadd(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_fsub(cq_ctx *ctx, cq_bit *dst,
                    const cq_bit *a, const cq_bit *b, int W);

#endif /* CQOPS_KERNELS_FADD_H */
