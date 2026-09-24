/* src/kernels/fround.h — M38b, K25. The rounding-to-integral family at f64:
 * `soft_trunc`, `soft_floor`, `soft_ceil`, `soft_round` and `soft_round_away`
 * as five UNARY Rule 7 kernels. PRD-v2 §5's M38 row (second half), §7.1-7.4,
 * §7.6, §7.7, §7.11, §7.12; docs/constructions/K25.md.
 *
 * Read K25.md before changing anything here. Every gate is a port of
 * third_party/bennett/src/softfloat/fround.jl — soft_trunc (:16-50),
 * soft_floor (:58-75), soft_ceil (:83-95), soft_round (:141-202) and
 * soft_round_away (:238-295) — transcribed ONE ROW PER OPERATOR OCCURRENCE in
 * source order (§7.2's literal grain) over the step blocks M12, M14, M16 and
 * M17 already export, plus upstream's own bitwise vocabulary, plus ONE
 * hand-off to M33's `soft_fadd` row program (floor/ceil, :73 and :93).
 * Nothing in this module is a construction of ours.
 *
 * THE LLVM-NAME -> soft_* MAPPING IS NOT IDENTITY (PRD-v2 §7.7), and the ABI
 * bead that wires these kernels must read this table, not guess it:
 *
 *     C / LLVM name           soft_*             kernel here
 *     trunc   llvm.trunc      soft_trunc         cq_kernel_ftrunc
 *     floor   llvm.floor      soft_floor         cq_kernel_ffloor
 *     ceil    llvm.ceil       soft_ceil          cq_kernel_fceil
 *     rint    llvm.rint       soft_round  (ties-to-EVEN)  cq_kernel_fround_even
 *     nearbyint llvm.nearbyint soft_round (ties-to-EVEN)  cq_kernel_fround_even
 *     round   llvm.round      soft_round_away (ties-AWAY) cq_kernel_fround_away
 *
 * There is NO `soft_rint`. Julia's `round` is ties-to-even and C's is
 * ties-away, so the obvious pick of `soft_round` for C `round` is the K9 trap
 * in fp dress: right on every input that is not an exact `N.5`, and only an L1
 * anchor at a tie (2.5, -2.5, 0.5, -0.5) against an independent reference can
 * see it.
 *
 * `soft_floor` AND `soft_ceil` CALL TWO OTHER ROUTINES, AND THEY ARE REACHED
 * TWO DIFFERENT WAYS. `soft_trunc` (:62, :87) is THIS module's own row program,
 * INLINED into floor's and ceil's tables by the `FI_TRUNC` macro at a named
 * base row, exactly as M37 inlines `soft_fptosi` into `soft_fptoui` — one flat
 * program with one prefix map, never a nested kernel. `soft_fadd` (:73, :93)
 * is M33's, and it is ONE `CQ_FIOP_FADD` HAND-OFF ROW driven through M33's own
 * EXPORTED step block, `cq_fadd_block` — the twin of `cq_fsub_block`, added
 * to M33 for this caller (src/kernels/fround_fadd.c is the one place fround
 * names it). Its second
 * operand is a CONSTANT: `one_bits | UInt64(0x8000000000000000)` at :70 is an
 * expression over two literals and is a constant-folded constant (K15 §2.0
 * rule 3, K20's `~SIGN_MASK`), NOT an `or` row.
 *
 * SLOTS AND GATES COME APART, as they do in every fp module since M31
 * (D-K18-6): constants are `const cq_bit` SOURCES and a mask or shift by a
 * compile-time constant is a VIEW (§7.3), so classical lanes sit inside block
 * operands at every operand mask including the all-quantum one L4 pins.
 * `cq_fround_steps()` is a SLOT count and never a gate count.
 *
 * NINE VARIABLE SHIFTS, EVERY ONE CLAMPED (PRD-v2 §7.6's fround.jl row): one in
 * soft_trunc (:39, amount in [0, 52]), five in soft_round (:172 :176 :178 :183
 * off `frac_bits` in [1, 52] and :175 off `round_bit_pos` in [0, 51]) and three
 * in soft_round_away (:266 :276 in [1, 52], :269 in [0, 51]). Every amount is
 * below 64 on EVERY input — the clamp is applied unconditionally, not only on
 * the selected branch — so D8's mask-then-saturate and Julia's `<<`/`>>`
 * agree everywhere and the port owes no D8 decision. The clamp's bounds are
 * `Int64` and genuinely negative on the unselected branches (`1075 - exp`
 * reaches -972 at `exp = 0x7FF`), so its compares are SIGNED (`cq_slt_block`);
 * that is §7.6's two-signednesses rule, and `ult` there would let a wrapped
 * amount reach the barrel on every NaN and Inf.
 *
 * FIVE SEAMS, RECORDED IN ADVANCE (bd 9ve.27.2; M39's five-seam shape):
 *
 *   fround_rows.inc   PROGRAM TEXT: the five tables, one row per operator
 *                     occurrence with its line cited. Rule 12's `.inc`
 *                     exemption for a large static table.
 *   fround.c          PROGRAM ACCESSORS: row tables' surface, arity, widths.
 *   fround_step.c     COSTS AND LAYOUT: what a row costs (ASKED of each
 *                     module), the memoised per-program prefix map.
 *   fround_operand.c  OPERAND RESOLUTION: views, constants, block outputs,
 *                     flags.
 *   fround_fadd.c     THE M33 HAND-OFF: `soft_fadd` as a step block.
 *   fround_emit.c     DISPATCH AND SURFACE: which gate a slot emits, the public
 *                     block, the five Rule 7 kernels.
 *   fround_eval.c     THE CLASSICAL BODIES (R9), which emit nothing.
 *
 * W MUST BE 64. PRD-v2 §1 scopes v2 to `f64` and every soft_* here is
 * `(UInt64)`, so another width is a fiction rather than an unimplemented case,
 * and is a hard error in BOTH configurations — M40's posture, copied.
 */
#ifndef CQOPS_KERNELS_FROUND_H
#define CQOPS_KERNELS_FROUND_H

#include "bit.h"
#include "ctx.h"
#include "kernels/fpfield.h"
#include "scratch.h"

#include <stdint.h>

/* The five programs, one per Julia body. The enumerator order is the order of
 * the bodies in fround.jl. */
typedef enum {
    CQ_FROUND_TRUNC = 0,     /* soft_trunc       fround.jl:16-50           */
    CQ_FROUND_FLOOR,         /* soft_floor       :58-75                    */
    CQ_FROUND_CEIL,          /* soft_ceil        :83-95                    */
    CQ_FROUND_EVEN,          /* soft_round       :141-202  (rint/nearbyint) */
    CQ_FROUND_AWAY,          /* soft_round_away  :238-295  (C round)       */
    CQ_FROUND_N_PROG
} cq_fround_prog;

/* --- The program: one row per operator occurrence (PRD-v2 §7.2). ---------- */

/* `CQ_FIOP_` AND NOT `CQ_FROP_`: M32 fpround owns `CQ_FROP_`/`CQ_FR_`, and M36
 * measured that a row-op enum colliding with a neighbouring enum renumbers it
 * silently. The taken two-letter prefixes were FA FC FD FM FR FS FU FV; FI is
 * "to Integral".
 *
 * The blocks are M16's `cq_eq_step` (lower_eq!, arith.jl:424-447) and
 * `cq_slt_step` (lower_slt!, :465-472); M14's `cq_sub_step` (lower_sub!,
 * adder.jl:148-172) and `cq_add_step` (lower_add!, :1-18); M17's
 * `cq_mux_step` (lower_mux!, arith.jl:522-532); M12's `cq_barrel_step`
 * (lower_var_shl!/lshr!, :366-380, :350-364); M33's `soft_fadd` program; and
 * the bitwise ops emitted directly one gate per slot as PRD-v2 §7.10 directs —
 * `lower_and!` (:268-272), `lower_or!` (:274-282), `lower_xor!` (:284-291) and
 * `lower_not1!` (:474-478). `~x` IS `lower_xor!` against an all-ones constant
 * (LLVM spells it `xor %x, -1`; M37's recorded reading). */
typedef enum {
    CQ_FIOP_VIEW = 0,  /* 0 slots, 0 bits: `(s0 >> shift) & mask`          */
    CQ_FIOP_EQ,        /* 2 x 64 -> 1 bit; the RAW wire is `a != b`        */
    CQ_FIOP_SLT,       /* 2 x 64 -> 1 bit; the RAW wire is `a >=s b`       */
    CQ_FIOP_SUB,       /* 2 x 64 -> 64                                     */
    CQ_FIOP_ADD,       /* 2 x 64 -> 64                                     */
    CQ_FIOP_MUX,       /* cond(1) + 2 x 64 -> 64                           */
    CQ_FIOP_OR,        /* 2 x 64 -> 64                                     */
    CQ_FIOP_AND,       /* 2 x 64 -> 64  (a RUNTIME mask)                   */
    CQ_FIOP_XOR,       /* 2 x 64 -> 64  (`~x` against CQ_FI_K_ONES)        */
    CQ_FIOP_BSHL,      /* value(64), amount(64) -> 64                      */
    CQ_FIOP_BLSHR,     /* value(64), amount(64) -> 64                      */
    CQ_FIOP_NOT1,      /* 1 bit -> 1 bit                                   */
    CQ_FIOP_AND1,      /* 2 x 1  -> 1 bit                                  */
    CQ_FIOP_OR1,       /* 2 x 1  -> 1 bit                                  */
    CQ_FIOP_FADD,      /* M33 soft_fadd(s0, s1) -> 64                      */
    CQ_FIOP_N_OP
} cq_fround_op;

_Static_assert(CQ_FIOP_VIEW == 0,
               "a zero-initialised row must be the free one; a renumbering "
               "must break a build, not just a comment");

/* An operand slot: `>= 0` names an earlier ROW of the same program; each
 * negative code names something that owns no qubit — the rail, or a constant
 * span (§7.3). Constants and views are only ever CONTROLS. The MASKS are not
 * here (FRAC_MASK, 0x7FF, SIGN_MASK, `& 1`): they are VIEW masks, §7.3 as
 * amended. */
enum {
    CQ_FI_A          = -1,   /* the rail `a`                                */
    CQ_FI_K_ZERO     = -2,   /* `UInt64(0)` / `Int64(0)`                    */
    CQ_FI_K_ONE      = -3,   /* `UInt64(1)` / `Int64(1)`                    */
    CQ_FI_K_7FF      = -4,   /* `Int64(0x7ff)`       :26 :147 :244          */
    CQ_FI_K_QUIET    = -5,   /* QUIET_BIT, softfloat_common.jl:13           */
    CQ_FI_K_1022     = -6,   /* `Int64(1022)`        :152 :155 :158 ...     */
    CQ_FI_K_1023     = -7,   /* `Int64(1023)`        :30                    */
    CQ_FI_K_1075     = -8,   /* `Int64(1075)`        :32 :35 :162 :167 ...  */
    CQ_FI_K_52       = -9,   /* clamp's upper bound  :37 :168 :262          */
    CQ_FI_K_ONES     = -10,  /* the all-ones operand of `~x`                */
    CQ_FI_K_IMPLICIT = -11,  /* IMPLICIT, softfloat_common.jl:9             */
    CQ_FI_K_ONE_F    = -12,  /* reinterpret(UInt64, 1.0)  :92 :159 :255     */
    CQ_FI_K_NEG_ONE_F = -13, /* one_bits | SIGN, :69-70, constant-folded    */
    CQ_FI_N_CODE     = 13
};

/* `shift` is the RIGHT-shift amount and a NEGATIVE value is a left shift, so a
 * chain of views composes to one `(shift, mask)` pair. Both are read only by a
 * VIEW. */
typedef struct {
    short    op;             /* a cq_fround_op                            */
    short    s0, s1, s2;     /* operands; only the first arity(op) read   */
    short    shift;          /* VIEW only                                 */
    uint64_t mask;           /* VIEW only                                 */
} cq_fround_row;

/* `soft_round` is the longest program at 72 rows. Pinned so a table that
 * outgrows the prefix map fails to BUILD rather than overrunning it. */
enum { CQ_FROUND_MAX_ROWS = 80 };

/* `soft_trunc`'s body is 27 rows, inlined at a base row into floor (2) and
 * ceil (3). Pinned so a body that drifts breaks the build. */
enum { CQ_FROUND_TRUNC_ROWS = 27 };

/* Program `p`'s table. `n` receives the row count. A program id outside the
 * five is a hard error in both configurations. */
const cq_fround_row *cq_fround_rows(cq_fround_prog p, int *n);

/* The row at which the INLINED `soft_trunc` body starts inside program `p` —
 * 0 for trunc, 2 for floor, 3 for ceil — or -1 for the two programs that do
 * not call it. Exported so a test names the base rather than repeating it,
 * which is exactly the number a boundary error moves. */
int cq_fround_trunc_base(cq_fround_prog p);

/* How many of s0/s1/s2 a row reads. */
int cq_fround_arity(int op);

/* The width of row `i`'s value: 64 or 1. */
int cq_fround_row_width(const cq_fround_row *rows, int n, int i);

/* --- The block. ---------------------------------------------------------- */

/* `a` is CQ_FP64_W lanes and CONTROLS ONLY. THE BLOCK ALLOCATES NOTHING: the
 * caller supplies `scr` and `off` and every span is `off`-relative — a block
 * cannot see its own offset, so dropping `off` slides the program and leaves
 * every kernel-level instrument green; only two programs in one region see it.
 * `prog` is a SELECTOR over a compile-time table, M31's `cls` shape, and is
 * range-checked on every entry point. */
typedef struct {
    const cq_bit  *a;
    cq_scratch    *scr;
    uint32_t       off;
    cq_fround_prog prog;
} cq_fround_block;

/* Bits of the caller's region program `p` owns — the sum over rows of each
 * row's own internals, every term ASKED of its module. */
uint32_t cq_fround_region(cq_fround_prog p);

/* The SLOT count (not a gate count): one involution per slot. */
int cq_fround_steps(cq_fround_prog p);

/* One gate, `u` in `[0, cq_fround_steps(k->prog))`. Out of range is a hard
 * error in BOTH configurations. */
void cq_fround_step(cq_ctx *ctx, const cq_fround_block *k, int u);

/* The last row's 64-lane span, inside the caller's region. `const`: inside a
 * compute half it is a CONTROL for whatever comes next. */
const cq_bit *cq_fround_result(const cq_fround_block *k);

/* R9's classical row: the SAME Julia body evaluated in C over `uint64_t`
 * (PRD-v2 §7.4), composing M33's `cq_fadd_eval` for floor/ceil — NEVER the
 * host `trunc()`/`floor()`/`ceil()`/`rint()`/`round()`. NOT an L1 oracle. */
uint64_t cq_fround_eval(cq_fround_prog p, uint64_t a);

/* --- The five Rule 7 kernels. -------------------------------------------- */

/* `dst ^= soft_*(a)`, with `a` 64 bits and unchanged and every internal
 * ancilla back at |0>. `W` must be 64. UNARY, ONE WIDTH (PRD-v2 §7.11):
 * `cq_kernel_fn` stays arity-2, so each is reached through `cq_kd_spec`'s
 * `call` adapter, exactly as M40's `cq_kernel_fsqrt` is. No `_unc` entry point
 * and no `_controlled` variant. */
void cq_kernel_ftrunc      (cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W);
void cq_kernel_ffloor      (cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W);
void cq_kernel_fceil       (cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W);
void cq_kernel_fround_even (cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W);
void cq_kernel_fround_away (cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W);

/* The same five behind one selector — what the five above are. */
void cq_kernel_fround(cq_ctx *ctx, cq_fround_prog p, cq_bit *dst,
                      const cq_bit *a, int W);

#endif /* CQOPS_KERNELS_FROUND_H */
