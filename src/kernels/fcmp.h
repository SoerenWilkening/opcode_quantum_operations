/* src/kernels/fcmp.h — M36, K18. `fcmp`, all fourteen LLVM predicates at f64.
 * PRD-v2 §5's ORDERED CORE <-> PREDICATE TABLE seam, §7.1-7.4, §7.11, §7.12.
 *
 * Read docs/constructions/K18.md before changing anything here. Every gate is
 * a port: the ten exported `soft_fcmp_*` bodies of
 * third_party/bennett/src/softfloat/fcmp.jl:1-185, transcribed one block per
 * operator occurrence (§7.2's literal grain) over the step blocks M16, M14,
 * M17 and M31 already export. Nothing in this module is a construction of
 * ours, and the fourteen-row dispatch is
 * third_party/bennett/src/extract/instructions.jl:7696-7733, not a derivation.
 *
 * FOUR PREDICATES SWAP AND ZERO INVERT AT THE DISPATCH, AND THAT IS THE
 * HEADLINE DIFFERENCE FROM K9. `ogt = olt(b,a)`, `oge = ole(b,a)`,
 * `ugt = ult(b,a)`, `uge = ule(b,a)`; the other ten call their own callee with
 * the arguments as written and there is no `lower_not1!` arm anywhere in the
 * extractor. The inversion has not vanished — it moved INSIDE the bodies
 * (`une` is `1 - oeq` at fcmp.jl:102, `ord` is `!_either_nan` at :134, `one`
 * carries the same `1 - oeq` at :154) — so the body-level inverting set
 * {ord, one, une} is DISJOINT from the swap set, where K9's two sets overlap
 * in `ugt`/`sgt`. The intersection of K9's swap set {ugt, ule, sgt, sle} with
 * this one is {ugt} ALONE: carrying K9's table over gives `ule` a spurious
 * swap and is wrong in eight rows. Nothing but L1 against a reference not
 * derived from this module can see it (K9's own uge-meaning-ule, measured).
 *
 * K9's SHAPE: `dst` IS ONE BIT AND `W` IS THE OPERAND WIDTH, WHICH MUST BE 64.
 * Upstream calls the callee at width `w` and immediately truncates to i1
 * (instructions.jl:7739-7742), so the source's `UInt64(result)` and the
 * extractor's `trunc` collapse into wiring and the copy-out is ONE CX. PRD-v2
 * §1 scopes v2 to `f64` and every `soft_fcmp_*` is `(UInt64, UInt64)`, so any
 * other `W` is a hard error in BOTH configurations rather than a fiction.
 * A cq_kd_spec therefore needs `w_dst = 1` and a `call` adapter passing
 * `sh->w[0]`; `shape_of` refuses the narrow shape without one.
 *
 * SLOTS AND GATES COME APART HERE, AS THEY DO IN M31 (D-K18-6). §7.3's
 * constants are `const cq_bit` SOURCES and a constant mask or shift is a VIEW,
 * so classical lanes sit INSIDE block operands at every operand mask including
 * the all-quantum one L4 pins, and the §3 fold table elides the gates they
 * control. `cq_fcmp_steps` is a SLOT count and is never a gate count; every
 * composition identity over this module is over slots, and the slot scan's
 * prediction is four-valued {NONE, X, CX, CCX} with its own stream cursor.
 *
 * THE PROGRAM IS A TABLE AND EVERYTHING ELSE IS SLOT ARITHMETIC. `fcmp.c`
 * holds the three ordered cores of fcmp.jl — `B_olt` (:8-47), `B_oeq` (:57-82)
 * and `B_nan` (:117-125) — as row tables plus the machine that turns a row
 * into spans and gates; `fcmp_pred.c` holds the ten one-line compositions
 * (:90, :102, :134, :144, :154, :164, :174, :184), the dispatch table, the
 * fourteen entry points and the classical row. That is PRD-v2 §5's recorded
 * seam, taken because the two halves together exceed Rule 12's 300 lines.
 */
#ifndef CQOPS_KERNELS_FCMP_H
#define CQOPS_KERNELS_FCMP_H

#include "bit.h"
#include "ctx.h"
#include "kernels/fpfield.h"
#include "scratch.h"

#include <stdint.h>

/* LLVM's own numbering is OEQ=1 … UNE=14 (instructions.jl:7696); this enum is
 * that order, zero-based, so a reader can check a row against the extractor by
 * counting. CQ_FCMP_N_PRED is the table length and the guards compare against
 * it, so it is asserted rather than commented (CQ_BIT_ZERO's precedent). */
typedef enum {
    CQ_FCMP_OEQ = 0, CQ_FCMP_OGT, CQ_FCMP_OGE, CQ_FCMP_OLT, CQ_FCMP_OLE,
    CQ_FCMP_ONE,     CQ_FCMP_ORD, CQ_FCMP_UNO, CQ_FCMP_UEQ, CQ_FCMP_UGT,
    CQ_FCMP_UGE,     CQ_FCMP_ULT, CQ_FCMP_ULE, CQ_FCMP_UNE,
    CQ_FCMP_N_PRED
} cq_fcmp_pred;

_Static_assert(CQ_FCMP_OEQ == 0 && CQ_FCMP_N_PRED == 14,
               "the fourteen rows of instructions.jl:7697-7733 are indexed by "
               "this enum; adding one is a deliberate act and must break a "
               "build, not just a comment");

/* --- The program: one row per operator occurrence (PRD-v2 §7.2). ---------- */

/* Each row is ONE upstream lowering function applied once. `CLASS_NAN` is
 * M31's `(ea == 0x7FF) & (fa != 0)` block, which is the whole of fcmp.jl:22,
 * :23, :68, :69, :122 and :123 and is reached through `cq_fp_class_step`
 * because a kernel is a whole sandwich and cq_sandwich refuses nesting. The
 * other rows are M16's `cq_eq_step` (lower_eq!, arith.jl:424-447), M16's
 * `cq_ult_step` (lower_ult!, :449-463), M14's `cq_sub_step` (lower_sub!,
 * adder.jl:149-172), M17's `cq_mux_step` (lower_mux!, arith.jl:522-532), and
 * the three bitwise ops emitted directly one gate per slot as PRD-v2 §7.10
 * directs — `lower_not1!` (:474-478), `lower_and!` (:268-272), `lower_or!`
 * (:274-282). */
typedef enum {
    CQ_FCOP_CLASS_NAN = 0,   /* 1 operand:  a 64-lane span                   */
    CQ_FCOP_EQ,              /* 2 operands: 64-lane spans; raw wire is a!=b  */
    CQ_FCOP_ULT,             /* 2 operands: 64-lane spans; raw is a >=u b    */
    CQ_FCOP_SUB,             /* 2 operands: 64-lane spans; output is d[0]    */
    CQ_FCOP_MUX,             /* 3 operands: 1-bit rows, cond then t then f   */
    CQ_FCOP_NOT1,            /* 1 operand:  a 1-bit row                      */
    CQ_FCOP_AND1,            /* 2 operands: 1-bit rows                       */
    CQ_FCOP_OR1,             /* 2 operands: 1-bit rows                       */
    CQ_FCOP_N_OP
} cq_fcmp_op;

/* An operand slot: `>= 0` names an earlier ROW of the same program, and each
 * negative code below names something that owns no qubit at all — a rail, one
 * of M31's views, or a constant span (§7.3). A view is only ever a CONTROL, so
 * it reaches the emitter through cq_emit_*'s `const cq_bit *` parameters and
 * can never be materialised; that is I6(a) by construction. */
enum {
    CQ_FC_A      = -1,   /* the rail `a`                                     */
    CQ_FC_B      = -2,   /* the rail `b`                                     */
    CQ_FC_ABS_A  = -3,   /* a & ABS_MASK   — a VIEW  (fcmp.jl:18, :64)       */
    CQ_FC_ABS_B  = -4,   /* b & ABS_MASK   — a VIEW  (fcmp.jl:19, :65)       */
    CQ_FC_SIGN_A = -5,   /* a >> 63        — a VIEW  (fcmp.jl:12)            */
    CQ_FC_SIGN_B = -6,   /* b >> 63        — a VIEW  (fcmp.jl:13)            */
    CQ_FC_K_ZERO = -7,   /* UInt64(0)      — a constant SPAN                 */
    CQ_FC_K_ONE  = -8    /* UInt64(1)      — a constant SPAN (fcmp.jl:102)   */
};

typedef struct {
    short op;            /* a cq_fcmp_op                                     */
    short s0, s1, s2;    /* operands; only the first `arity(op)` are read    */
} cq_fcmp_row;

/* `ule` is the longest program at 42 rows. Pinned so a program that outgrows
 * the buffer fails loudly rather than overrunning a caller's stack array. */
enum { CQ_FCMP_MAX_ROWS = 48 };

/* The three ordered cores, for a consumer that composes them (fcmp_pred.c is
 * the only one today). Writes the body's rows into `out`, with every internal
 * row reference shifted by `base`; returns the row count. `cq_fcmp_body_flag`
 * is the index of the row holding that body's answer. */
typedef enum { CQ_FCMP_B_NAN = 0, CQ_FCMP_B_OEQ, CQ_FCMP_B_OLT } cq_fcmp_body_id;

int cq_fcmp_body(cq_fcmp_body_id id, int base, cq_fcmp_row *out);
int cq_fcmp_body_flag(cq_fcmp_body_id id, int base);

/* The whole program for a predicate, with the dispatch's operand swap already
 * resolved to its callee — so `ogt` yields `olt`'s program and the CALLER
 * exchanges the operands. Returns the row count; `out` is CQ_FCMP_MAX_ROWS. */
int cq_fcmp_program(cq_fcmp_pred p, cq_fcmp_row *out);

/* instructions.jl:7697-7733, as two functions. `cq_fcmp_callee` is the
 * `soft_fcmp_*` the row routes to and `cq_fcmp_swapped` is whether that row
 * carries `op1, op2 = op2, op1`. Exactly four rows swap and none inverts. */
cq_fcmp_pred cq_fcmp_callee (cq_fcmp_pred p);
int          cq_fcmp_swapped(cq_fcmp_pred p);
const char  *cq_fcmp_name   (cq_fcmp_pred p);

/* --- The block. ---------------------------------------------------------- */

/* `a` and `b` are CQ_FP64_W lanes each and are CONTROLS ONLY — the operand
 * swap has already been applied by the caller, so these are the CALLEE's
 * operands. THE BLOCK ALLOCATES NOTHING: the caller supplies `scr` and `off`
 * and every span is `off`-relative, which is load-bearing rather than tidy —
 * a block cannot see its own offset, so dropping `off` slides the whole
 * program inside the caller's region and leaves the value, the palindrome and
 * the pool all correct (M31's measured finding). */
typedef struct {
    const cq_bit      *a, *b;
    cq_scratch        *scr;
    uint32_t           off;
    const cq_fcmp_row *rows;
    int                n_rows;
} cq_fcmp_block;

/* How many bits of the caller's region the program owns, starting at `off` —
 * the sum over rows of each row's own internals. Never written as a number
 * here or in a test. */
uint32_t cq_fcmp_region(const cq_fcmp_row *rows, int n);

/* The SLOT count: the sum over rows of each block's step count, ASKED of its
 * owning module at width 64. ONE INVOLUTION PER SLOT, which is what makes
 * cq_sandwich's index reversal be gate reversal. NOT a gate count. */
int cq_fcmp_steps(const cq_fcmp_row *rows, int n);

/* One gate of the program, `u` in [0, cq_fcmp_steps(rows, n)). Out of range is
 * a hard error in BOTH configurations, for cq_eq_step's reason: an off-by-one
 * lands inside some inner block's OR-prefix or carry chain and emits a
 * plausible wrong gate rather than failing. */
void cq_fcmp_step(cq_ctx *ctx, const cq_fcmp_block *k, int u);

/* The last row's wire — the predicate itself, not its negation: every `==`,
 * `<` and `>` occurrence had its polarity settled inside the program by
 * upstream's own `lower_not1!`, so a consumer copies this out unchanged. */
const cq_bit *cq_fcmp_flag(const cq_fcmp_block *k);

/* Risk R9's classical row: the SAME ten bodies evaluated in C over `uint64_t`
 * (PRD-v2 §7.4), NEVER the host `<` / `==` / `isunordered`. Takes the LLVM
 * predicate and the ORIGINAL operand order, and applies the dispatch's swap
 * itself, so it and the circuit resolve the table in one place each and a test
 * can drive it with the same arguments the kernel got. */
uint64_t cq_fcmp_eval(uint64_t a, uint64_t b, cq_fcmp_pred p);

/* --- The fourteen Rule 7 kernels. ---------------------------------------- */

/* `dst[0] ^= (a <predicate> b)`, with `a` and `b` 64 bits and unchanged and
 * every internal ancilla back at |0>. `W` must be 64. There is no `_unc` entry
 * point (uncompute is the same call) and no `_controlled` variant (the axis is
 * an emitter mode). */
void cq_kernel_fcmp_oeq(cq_ctx *c, cq_bit *d, const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_fcmp_ogt(cq_ctx *c, cq_bit *d, const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_fcmp_oge(cq_ctx *c, cq_bit *d, const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_fcmp_olt(cq_ctx *c, cq_bit *d, const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_fcmp_ole(cq_ctx *c, cq_bit *d, const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_fcmp_one(cq_ctx *c, cq_bit *d, const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_fcmp_ord(cq_ctx *c, cq_bit *d, const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_fcmp_uno(cq_ctx *c, cq_bit *d, const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_fcmp_ueq(cq_ctx *c, cq_bit *d, const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_fcmp_ugt(cq_ctx *c, cq_bit *d, const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_fcmp_uge(cq_ctx *c, cq_bit *d, const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_fcmp_ult(cq_ctx *c, cq_bit *d, const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_fcmp_ule(cq_ctx *c, cq_bit *d, const cq_bit *a, const cq_bit *b, int W);
void cq_kernel_fcmp_une(cq_ctx *c, cq_bit *d, const cq_bit *a, const cq_bit *b, int W);

#endif /* CQOPS_KERNELS_FCMP_H */
