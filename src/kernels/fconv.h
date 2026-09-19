/* src/kernels/fconv.h — M37, K19. `fptosi` / `fptoui` / `sitofp` / `uitofp` at
 * f64, in M13's two-width shape. PRD-v2 §5's M37 row, §7.1-7.7, §7.9, §7.11,
 * §7.12; docs/constructions/K19.md.
 *
 * Read K19.md before changing anything here. Every gate is a port:
 * `soft_fptosi` (third_party/bennett/src/softfloat/fptosi.jl:22-69),
 * `soft_fptoui` (fptoui.jl:28-48) and `soft_sitofp` (sitofp.jl:15-85),
 * transcribed ONE ROW PER OPERATOR OCCURRENCE in source order (§7.2's literal
 * grain) over the step blocks M12, M14, M16, M17 and M33 already export.
 * Nothing in this module is a construction of ours, and the ROUTING — which
 * callee each LLVM opcode reaches — is upstream's too
 * (third_party/bennett/src/extract/instructions.jl:7636-7686).
 *
 * `uitofp` IS `zext` THEN `soft_sitofp`, AND AT `i64` IT IS AN ABORT.
 * instructions.jl:7666-7682 routes BOTH SIToFP and UIToFP to `soft_sitofp`,
 * picking `:sext` or `:zext` for the widening cast at :7677 — so `uitofp`'s
 * row program IS `sitofp`'s, byte for byte, and the whole of the difference is
 * at the OPERAND. When the source is already 64 bits (:7672-7673) there is no
 * widening cast at all and `soft_sitofp` reads bit 63 as a SIGN, so every
 * `u >= 2^63` comes back negative. That is a known miscompile on half the
 * input space (the mirror of the `U31` bug upstream fixed on the fp->int
 * side), correcting it would be a re-derivation (Rule 1), so PRD-v2 §7.9 and
 * bead 9ve.34 keep exactly that width a LOUD ABORT. `cq_kernel_uitofp` with
 * F == 64 names the bead and dies; it is never silently `sitofp`.
 *
 * `fpext` AND `fptrunc` ARE NOT HERE AND STAY ABORTS (PRD-v2 §7.9): they need
 * an `f32` rail, which cannot exist while `cqrt_alloc_f32` aborts. PRD-v2 §5's
 * M37 row lists six symbols and §7.9 removes two of them, so this module is
 * FOUR.
 *
 * THE TWO-WIDTH SHAPE IS M13's AND BOTH WIDTHS ARE NAMED (§7.11, Rule 7's
 * "arity and width are not part of the contract — the semantics are"). The
 * semantics are Rule 7's verbatim: `dst ^= f(a)`, `a` unchanged, every
 * internal ancilla back at |0>. The shipped pairs are
 *
 *     fptosi  f64 -> i64           F = 64, T = 64
 *     fptoui  f64 -> u64           F = 64, T = 64
 *     sitofp  i64 -> f64           F = 64, T = 64
 *     uitofp  i{1,8,16,32} -> f64  F in {1,8,16,32}, T = 64
 *
 * and any other pair is a hard error in BOTH configurations naming the kernel.
 * THE NARROWING `trunc` IS NOT FOLDED INTO THE COPY-OUT HERE, and that is
 * upstream's own shape rather than a gap: instructions.jl:7657 emits the
 * narrowing as a SECOND IR instruction, so `fptosi f64 -> i8` is this kernel
 * at T = 64 followed by `cq_kernel_trunc`, which is where the ABI wiring (bead
 * 9ve.36) puts it. K19.md §2.6 records the fold as available and not taken.
 *
 * ONE SANDWICH PER KERNEL, ONE FLAT REGION, AND `fptoui` INLINES `fptosi`
 * TWICE. A kernel is a whole `cq_sandwich` and the driver refuses nesting in
 * both configurations, so `cq_kernel_fptoui` cannot call `cq_kernel_fptosi`.
 * fptoui.jl:40 and :46 are two separate CALLS of `soft_fptosi`, and under
 * §7.2's literal grain (no CSE) each gets its own 31 rows: `cq_fconv_program`
 * assembles them by copying the fptosi table twice with every internal row
 * reference shifted and `CQ_FV_A` rebased — the mechanism M33 uses to run the
 * fadd body under fsub's prologue. TWO CONSEQUENCES WORTH STATING. (i)
 * K19.md §2.8's two-level decode does not exist: the program is FLAT and there
 * is one prefix sum, so the off-by-one that box warns about is
 * unrepresentable. (ii) K19.md §2.3's "U8's and U10's scratch must be
 * DISJOINT" is satisfied BY CONSTRUCTION — each inlined row owns its own
 * extent in the prefix walk — so the silent miscompile that box names cannot
 * be written here either.
 *
 * SLOTS AND GATES COME APART, AS THEY DO IN M31, M32, M33, M34 AND M36
 * (D-K18-6). §7.3's constants are `const cq_bit` SOURCES and a mask or a shift
 * by a compile-time constant is a VIEW, so classical lanes sit inside block
 * operands at EVERY operand mask including the all-quantum one L4 pins. Every
 * composition identity over this module is over SLOTS; the slot scan's
 * prediction is four-valued {NONE, X, CX, CCX} walked with its own stream
 * cursor.
 *
 * THERE IS NO SIGNED COMPARE ANYWHERE IN K19, AND SAYING SO IS THE FINDING
 * (K19.md §5.3; PRD-v2 §7.6's two signednesses). All five ordering compares in
 * the three sources are on a `UInt64` — fptosi.jl:45, :46, :53, :66 and
 * sitofp.jl:59 — and at :45-46 the UNSIGNED WRAPAROUND IS THE MECHANISM:
 * `left_shift = exp - 1075` wraps for every `exp < 1075` into a value that is
 * `> 63` unsigned and therefore clamps. Routing one of the five through
 * `cq_slt_block` is K9's `uge`-meaning-`ule` in a new dress. K19.md §5.3's box
 * derives that the two spellings happen to AGREE on every input — because each
 * wrapped amount occurs only on the arm fptosi.jl:53 discards — so it is a
 * RECORDED EQUIVALENT MUTANT in value and is caught by the composition
 * identity and L4 alone (`cq_slt_steps(64)` exceeds `cq_ult_steps(64)`).
 * Transcribe it unsigned and do not weaken :53's select to match. What is NOT
 * equivalent is dropping either clamp: that is a live D8 miscompile on every
 * operand below 2^-12 (K19.md §5.5).
 *
 * THE CLZ LADDER IS FIVE UNIFORM STAGES AND A SHORT SIXTH (sitofp.jl:29-50).
 * Stage 6 stops at `clz` — there is no `tmp = ifelse(top1_zero, tmp << 1,
 * tmp)` — so a decode written as `stage = u / stage_len` is wrong by two rows
 * at the end and the error lands in :54's `sub`, whose operand is `clz`. The
 * table below is written out stage by stage for that reason and
 * tests/test_kernel_fconv_slots.inc asserts the 5 + 1 shape directly.
 * EVERY ONE OF THE TEN `+` OCCURRENCES IS `cq_add_block`, NEVER K8: the six
 * ladder rungs read exactly like an in-place accumulate and CLAUDE.md's
 * verdict on that substitution is that the forward value would be right and
 * only the `_unc` wrong. C*j*4's false arm reads the PRE-add value, which is
 * the second, independent reason.
 *
 * FOUR TUs ON THREE MACHINE SEAMS, RECORDED IN ADVANCE, PLUS THE PRD's OWN.
 *
 *   fconv.c        the ROW TABLES and nothing else — the port a reader checks
 *                  against the Julia, plus the program assembly that inlines
 *                  fptosi twice into fptoui.
 *   fconv_step.c   the LAYOUT and the OPERANDS: what a row costs (asked of
 *                  M12, M14, M16, M17 and M33), where its spans lie, and what
 *                  each operand code resolves to. Knows no Julia.
 *   fconv_emit.c   the DISPATCH and the SURFACE: which gate a slot emits, the
 *                  exported `cq_fptosi_block`, and the four Rule 7 kernels.
 *                  `fconv_int.h` is the seam between these two.
 *   fconv_eval.c   the four CLASSICAL bodies, which emit nothing.
 *
 * PRD-v2 §5's recorded INT->FP <-> FP->INT seam holds at the TABLE level and
 * is NOT taken as a file cut. K19.md §2.10 measured it: zero named spans cross
 * it, two independent row tables, two independent barrels, the whole
 * saturation story on one side and the whole CLZ-and-rounding story on the
 * other. What does NOT partition that way is the MACHINE — one op vocabulary,
 * one prefix-sum walk, one operand resolver and one dispatch serve both
 * directions — and K15.md's reason applies verbatim: two prefix-sum tables is
 * two chances to get the decode wrong. The seam is kept as a marked boundary
 * between the two tables in fconv.c. A FOURTH machine seam,
 * COSTS-AND-LAYOUT <-> OPERAND RESOLUTION, is recorded here as available and
 * is not taken: M37's vocabulary is fourteen ops with no destructured tuple
 * and no picked output, where M33 had nineteen with three tuples, so
 * fconv_step.c fits Rule 12's 300 lines without it.
 *
 * NO WIDTH PARAMETER INSIDE, for M31's, M32's and M33's reason: PRD-v2 §1
 * scopes v2 to `f64`, so 64 is a constant here and not a ladder. The SOURCE
 * width of `uitofp` is the one exception and it is an operand fact, not a
 * kernel width: it selects a view and changes no row.
 */
#ifndef CQOPS_KERNELS_FCONV_H
#define CQOPS_KERNELS_FCONV_H

#include "bit.h"
#include "ctx.h"
#include "kernels/fpfield.h"
#include "scratch.h"

#include <stdint.h>

/* --- The program: one row per operator occurrence (PRD-v2 §7.2). ---------- */

/* Each row is ONE upstream lowering function applied once, one hand-off to a
 * sibling kernel's exported block, or a piece of wiring. The blocks are M16's
 * `cq_eq_step` (lower_eq!, arith.jl:424-447) and `cq_ult_step` (lower_ult!,
 * :449-463); M14's `cq_sub_step` (lower_sub!, adder.jl:148-172) and
 * `cq_add_step` (lower_add!, :1-18); M17's `cq_mux_step` (lower_mux!,
 * arith.jl:522-532); M12's `cq_barrel_step` (lower_var_shl!/lshr!, :366-380,
 * :350-364); M33's `cq_fsub_step`; and the bitwise ops emitted directly one
 * gate per slot as PRD-v2 §7.10 directs — `lower_and!` (:268-272),
 * `lower_or!` (:274-282), `lower_xor!` (:284-291) and `lower_not1!`
 * (:474-478).
 *
 * `CQ_FVOP_` AND NOT `CQ_FC_`: M36 measured that a row-op enum colliding with
 * a neighbouring enum renumbers it silently (C enums share one namespace), and
 * `CQ_FC_` is already M36 `fcmp`'s operand-code prefix in this same directory.
 * The two prefixes are deliberately unlike each other and unlike M33's
 * `CQ_FAOP_` / `CQ_FA_`. */
typedef enum {
    CQ_FVOP_VIEW = 0,  /* 0 slots, 0 bits: `(s0 >> shift) & mask`          */
    CQ_FVOP_EQ,        /* 2 x 64 -> 1 bit; the RAW wire is `a != b`        */
    CQ_FVOP_ULT,       /* 2 x 64 -> 1 bit; the RAW wire is `a >=u b`       */
    CQ_FVOP_SUB,       /* 2 x 64 -> 64                                     */
    CQ_FVOP_ADD,       /* 2 x 64 -> 64                                     */
    CQ_FVOP_MUX,       /* cond(1) + 2 x 64 -> 64                           */
    CQ_FVOP_AND,       /* 2 x 64 -> 64  (a RUNTIME mask; sitofp.jl:71)     */
    CQ_FVOP_OR,        /* 2 x 64 -> 64                                     */
    CQ_FVOP_XOR,       /* 2 x 64 -> 64  (`~x`, an xor with all-ones)       */
    CQ_FVOP_NOT1,      /* 1 bit -> 1 bit                                   */
    CQ_FVOP_AND1,      /* 2 x 1  -> 1 bit  (fptoui.jl:34)                  */
    CQ_FVOP_BSHL,      /* value(64), amount(64) -> 64                      */
    CQ_FVOP_BLSHR,     /* value(64), amount(64) -> 64                      */
    CQ_FVOP_FSUB,      /* M33's whole soft_fsub (fptoui.jl:45)             */
    CQ_FVOP_N_OP
} cq_fconv_op;

/* An operand slot: `>= 0` names an earlier ROW of the same program, and each
 * negative code below names something that owns no qubit at all — the single
 * rail or a constant span (§7.3). A constant and a view are only ever
 * CONTROLS, so they reach the emitter through cq_emit_*'s `const cq_bit *`
 * parameters and can never be materialised: I6(a) by construction.
 *
 * `CQ_FV_A` IS THE ONE CODE A PROGRAM MAY REBASE, and `cq_fconv_program` does
 * it for fptoui.jl:46's `soft_fptosi(adjusted)` — the second inlined copy of
 * the fptosi table reads the FSUB row's output where the first reads the rail.
 *
 * SIXTEEN CONSTANT TABLES, ZERO QUBITS (K19.md §2.1). The five CLZ increments
 * are separate codes rather than one parameterised code because each is a
 * distinct operator occurrence in sitofp.jl:30/34/38/42/46 and the table reads
 * one-to-one against the source. */
enum {
    CQ_FV_A        = -1,
    CQ_FV_K_0      = -2,   /* fptosi.jl:28; sitofp.jl:17, :26, :71, :83    */
    CQ_FV_K_1      = -3,   /* fptosi.jl:23, :54, :60, :61; sitofp.jl:22    */
    CQ_FV_K_2      = -4,   /* sitofp.jl:46  — CLZ stage 5                  */
    CQ_FV_K_4      = -5,   /* sitofp.jl:42  — CLZ stage 4                  */
    CQ_FV_K_8      = -6,   /* sitofp.jl:38  — CLZ stage 3                  */
    CQ_FV_K_16     = -7,   /* sitofp.jl:34  — CLZ stage 2                  */
    CQ_FV_K_32     = -8,   /* sitofp.jl:30  — CLZ stage 1                  */
    CQ_FV_K_63     = -9,   /* fptosi.jl:45-46; sitofp.jl:59                */
    CQ_FV_K_3FF    = -10,  /* sitofp.jl:69  — the 10-bit sticky field      */
    CQ_FV_K_7FF    = -11,  /* fptosi.jl:24; fptoui.jl:30                   */
    CQ_FV_K_1075   = -12,  /* fptosi.jl:41-42, :53                         */
    CQ_FV_K_1086   = -13,  /* fptosi.jl:66; fptoui.jl:34; sitofp.jl:54     */
    CQ_FV_K_FRAC   = -14,  /* fptosi.jl:25; sitofp.jl:63, :77              */
    CQ_FV_K_ONES   = -15,  /* `~x` is lower_xor! with an all-ones constant */
    CQ_FV_K_HIBIT  = -16,  /* fptosi.jl:67's INT_MIN AND fptoui.jl:46's OR */
    CQ_FV_K_BIAS   = -17,  /* fptoui.jl:45  — 2^63 as a double             */
    CQ_FV_N_CODE   = 17
};

/* `shift` is the RIGHT-shift amount and a NEGATIVE value is a left shift, so a
 * chain of views composes to one `(shift, mask)` pair — see fconv_step.c.
 * `mask` is read only by a VIEW. */
typedef struct {
    short    op;             /* a cq_fconv_op                             */
    short    s0, s1, s2;     /* operands; only the first arity(op) read   */
    short    shift;          /* VIEW only                                 */
    uint64_t mask;           /* VIEW only                                 */
} cq_fconv_row;

/* `fptoui` is the longest program at 74 rows — its own twelve plus TWO inlined
 * copies of the 31-row fptosi table. Pinned so a table that outgrows the
 * prefix-offset buffer fails loudly rather than overrunning a stack array, and
 * asserted in fconv.c rather than commented. */
enum { CQ_FCONV_MAX_ROWS = 88 };

/* `uitofp` is NOT a fourth program: it IS `CQ_FCONV_PROG_SITOFP`, and the zext
 * lives at the operand (instructions.jl:7677-7679). Giving it an enumerator
 * would be a second table to keep in step with the first. */
typedef enum {
    CQ_FCONV_PROG_FPTOSI = 0,
    CQ_FCONV_PROG_FPTOUI,
    CQ_FCONV_PROG_SITOFP
} cq_fconv_prog;

/* The whole program for one conversion, written into `out`
 * (CQ_FCONV_MAX_ROWS entries) and returning the row count. */
int cq_fconv_program(cq_fconv_prog p, cq_fconv_row *out);

/* The two leaf tables, unrebased, and fptoui's own twelve rows — for a reader
 * and for the tests that check the assembled programs against them. */
const cq_fconv_row *cq_fptosi_rows(int *n);
const cq_fconv_row *cq_sitofp_rows(int *n);
const cq_fconv_row *cq_fptoui_own_rows(int *n);

/* How many of s0/s1/s2 a row reads. */
int cq_fconv_arity(int op);

/* Which ROW of `p`'s program holds its result. Exposed so a test can name the
 * answer without repeating the row index, which is exactly the number a
 * boundary error moves. */
int cq_fconv_result_row(cq_fconv_prog p);

/* --- The exported `fptosi` compute half. --------------------------------- */

/* `soft_fptosi(a)` as a step block, on the shipped block shape. `a` is
 * CQ_FP64_W lanes and is a CONTROL ONLY — it may be a rail, a scratch span, or
 * a view that overlaps a region an earlier step wrote (plan §0.4 obligations
 * 2, 3 and 4). THE BLOCK ALLOCATES NOTHING: the caller supplies `scr` and
 * `off` and every span is `off`-relative, which is load-bearing rather than
 * tidy — a block cannot see its own offset, so dropping `off` slides the whole
 * program inside the caller's region and leaves the value, the palindrome and
 * the pool all correct (M31's measured finding; the two-blocks-in-one-region
 * case is the sole detector).
 *
 * IT IS EXPORTED EVEN THOUGH `cq_kernel_fptoui` INLINES THE ROWS INSTEAD. Two
 * reasons, and neither is "a consumer asked". A block at a NON-ZERO offset is
 * the only representable form of the offset-slide mutation, which every Rule 7
 * entry point (all of which pass off = 0) is blind to; and K19.md §2.3 designs
 * fptoui around exactly this block, so a reader checking the K-doc against the
 * code finds it. */
typedef struct {
    const cq_bit *a;
    cq_scratch   *scr;
    uint32_t      off;
} cq_fptosi_block;

/* The bits of the caller's region this block owns, starting at `off` — the sum
 * over rows of each row's own internals, every term ASKED of its module. Never
 * written as a number here or in a test. */
uint32_t cq_fptosi_region(void);

/* The SLOT count: the sum over rows of each block's step count, asked of its
 * owning module at width 64. ONE INVOLUTION PER SLOT, which is what makes
 * cq_sandwich's index reversal be gate reversal. NOT a gate count. */
int cq_fptosi_steps(void);

/* One gate, `u` in `[0, cq_fptosi_steps())`. Out of range is a hard error in
 * BOTH configurations, for cq_eq_step's reason: an off-by-one lands inside
 * some inner block's OR-prefix or carry chain and emits a plausible WRONG gate
 * rather than failing. */
void cq_fptosi_step(cq_ctx *ctx, const cq_fptosi_block *k, int u);

/* The 64-lane result span, inside the caller's own region. */
const cq_bit *cq_fptosi_result(const cq_fptosi_block *k);

/* --- Risk R9's classical rows (PRD-v2 §7.4, and here FORCED twice over). -- */

/* The SAME four bodies evaluated in C over `uint64_t` — NEVER a host cast.
 * §7.4 refuses the host operator because its answers on the IEEE-unspecified
 * cells are x86's choices; for K19 the ground is stronger, because for every
 * NaN, every infinity and every out-of-range operand `(int64_t)d` and
 * `(uint64_t)d` are UNDEFINED BEHAVIOUR in C (C11 §6.3.1.4) and a compiler is
 * entitled to fold a compile-time one to anything at all. PRD-v2 §7.5's
 * decision is to pin upstream's x86 saturation — `INT_MIN =
 * 0x8000000000000000` for any NaN / +-Inf / out-of-range — which is D3's
 * posture applied a third time: deterministic, documented, never traps.
 *
 * NOT an L1 oracle. An oracle sharing shape with the implementation is blind
 * to exactly what that implementation gets wrong (the Step 18 trap); L1's
 * reference is the HOST cast where C defines it and §7.5's table as LITERALS
 * where C does not.
 *
 * `cq_uitofp_eval` takes the source width because the zext is upstream's
 * (instructions.jl:7677-7679) and because F == 64 has no zext at all — which
 * is the width that aborts. */
uint64_t cq_fptosi_eval(uint64_t a);
uint64_t cq_fptoui_eval(uint64_t a);
uint64_t cq_sitofp_eval(uint64_t a);
uint64_t cq_uitofp_eval(uint64_t a, int F);

/* --- The four Rule 7 kernels, in M13's two-width shape. ------------------ */

/* `dst ^= f(a)` over the bit patterns, with `a` F bits and unchanged and every
 * internal ancilla back at |0>. There is no `_unc` entry point (uncompute is
 * the same call) and no `_controlled` variant (the axis is an emitter mode).
 *
 * Every width pair but the four shipped ones is a hard error in BOTH
 * configurations, and `uitofp` at F == 64 has its OWN message naming bead
 * 9ve.34 — it is a REFUSAL of a known-wrong upstream routing, not an
 * unimplemented case, and it must never quietly become `sitofp`. */
void cq_kernel_fptosi(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T);
void cq_kernel_fptoui(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T);
void cq_kernel_sitofp(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T);
void cq_kernel_uitofp(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T);

/* The shipped `uitofp` source widths, in one place so a test enumerates rather
 * than repeats them. i64 is ABSENT and that is bead 9ve.34. */
enum { CQ_FCONV_N_UITOFP_W = 4 };
const int *cq_fconv_uitofp_widths(int *n);

#endif /* CQOPS_KERNELS_FCONV_H */
