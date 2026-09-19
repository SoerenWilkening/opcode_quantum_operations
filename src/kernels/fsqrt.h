/* src/kernels/fsqrt.h — M40, K21. `fsqrt` at f64: `soft_fsqrt` as a UNARY
 * Rule 7 kernel. PRD-v2 §5's M40 row, §7.1-7.4, §7.6, §7.11, §7.12, §7.16.
 *
 * Read docs/constructions/K21.md before changing anything here. Every gate is
 * a port: `soft_fsqrt` (third_party/bennett/src/softfloat/fsqrt.jl:31-118) and
 * the TWO shared helpers it calls — `_sf_normalize_to_bit52` at :49 and
 * `_sf_round_and_pack` at :103, both M32's — transcribed ONE ROW PER OPERATOR
 * OCCURRENCE in source order (§7.2's literal grain) over the step blocks M14,
 * M16, M17, M31 and M32 already export, plus upstream's own bitwise
 * vocabulary. Nothing in this module is a construction of ours.
 *
 * `soft_fsqrt` CALLS NEITHER `_sf_normalize_clz` NOR `_sf_handle_subnormal`,
 * and it uses NONE of the 128-bit `(hi, lo)` helpers. Upstream says why for
 * the first pair — fsqrt.jl:27-29, *"No subnormal result path"* — and
 * softfloat_common.jl:255-262 attributes the `(hi, lo)` IDIOM to `fsqrt.jl`
 * rather than claiming `fsqrt` calls the family. Measured: a `grep` over the
 * file returns `:49` and `:103` and nothing else.
 *
 * `cq_divrem_step` IS NOT COMPOSABLE HERE AND PRD-v2 §5 NAMES M14/M16/M17
 * DELIBERATELY (K21.md §5.1). `soft_fdiv`'s loop compares against a
 * loop-INVARIANT `mb`, which is the one divisor `cq_divrem_block` binds;
 * `soft_fsqrt` compares against `(q << 2) | 1`, a function of the partial
 * root, and streams TWO bits per iteration out of a mutated `(a_hi, a_lo)`
 * pair. *"As M35 does"* means compose AN exported step block, not the same
 * one. So the digit loop composes `cq_ult_block`, `cq_sub_block` and
 * `cq_mux_block` per iteration and transcribes no gate list.
 *
 * THE SHAPE IS M34's AND M33's: A ROW TABLE PLUS A STEP MACHINE. A row is one
 * upstream lowering function applied once, a VIEW, an ARITHMETIC-shift view,
 * or a projection of a hand-off's tuple. The kernel is ONE cq_sandwich over
 * ONE flat region whose step function dispatches a global index into those
 * blocks (PRD-v2 §7.1, src/kernels/divrem_u.c's shape).
 *
 * THE 64-ITERATION TABLE IS LOOP-BUILT BY THE PREPROCESSOR AND THE PROGRAM
 * HOLDS 64 x 16 DISTINCT ROWS. `FS_ITER(t)` is a macro expanded 64 times into
 * a `static const` initialiser, so the table is still compile-time constant
 * and still one row per operator occurrence per iteration — every iteration's
 * spans are its own, which is what the span-disjointness scan asserts. A
 * SHARED per-iteration span would be the K12-shaped mutant in this kernel's
 * clothing: 63 x 897 bits cheaper, right value at iteration 0, and wrong
 * everywhere the mux reads the span it writes.
 *
 * FIVE SEAMS, ALL RECORDED BEFORE A LINE WAS WRITTEN (bd 9ve.26, and the
 * three-seams-not-two finding from M33).
 *
 *   fsqrt.c          the ROW TABLES and nothing else — the prologue, the
 *                    16-row ITERATION TEMPLATE, the tail, and the expansion.
 *                    The port a reader checks against the Julia.
 *   fsqrt_step.c     the COSTS and the LAYOUT: what a row costs (ASKED of
 *                    M14/M16/M17/M31/M32), and the memoised prefix map that
 *                    turns a 1,070-row program's decode from O(n) per step
 *                    into a binary search. Knows no Julia.
 *   fsqrt_operand.c  OPERAND RESOLUTION: the collapsing view chain, the
 *                    arithmetic-shift view, the constant vocabulary, the block
 *                    outputs, the one-bit flags and the two hand-offs'
 *                    projections.
 *   fsqrt_emit.c     the DISPATCH and the SURFACE: which gate a slot emits,
 *                    the public block, and the Rule 7 kernel.
 *                    `src/kernels/fsqrt_int.h` is the seam between the last
 *                    three.
 *   fsqrt_eval.c     the CLASSICAL body, which emits nothing.
 *
 * PRD-v2 §5's recorded PRE-NORMALISE <-> THE DIGIT LOOP seam is NOT taken as a
 * FILE seam, for M34's two reasons one size up: the row table is a prologue, a
 * 16-row template and a tail, so Rule 12 never binds on it, and splitting it
 * would put ONE prefix map in two files, which K21.md §2.6 point 3 refuses in
 * advance (*"If the file splits, the two halves must not each own a table"*).
 * It survives as a NAMED ROW BOUNDARY — `cq_fsqrt_seam_row()` — with a case
 * pinning what crosses it, which is the claim the file split stood in for.
 *
 * SLOTS AND GATES COME APART, AS THEY DO IN M31, M32, M33, M34 AND M36
 * (D-K18-6). §7.3's constants are `const cq_bit` SOURCES and a mask or a shift
 * by a compile-time constant is a VIEW, so classical lanes sit inside block
 * operands at EVERY operand mask including the all-quantum one L4 pins — 523
 * of K21's 1,070 rows are views. `cq_fsqrt_steps()` is a SLOT count and is
 * never a gate count; every composition identity over this module is over
 * slots. AND THE LOOP ADDS A CLAUSE NO SIBLING HAS: `a_lo` is a CHAIN of
 * `<< 2` views, so its fold pattern is a function of the ITERATION INDEX and
 * is entirely CQ_BIT_ZERO from t >= 32. The radicand is being consumed; that
 * is what the loop is for. A slot scan predicting from the within-iteration
 * index alone goes red at t = 32 and stays red.
 *
 * W MUST BE 64. PRD-v2 §1 scopes v2 to `f64` and `soft_fsqrt` is `(UInt64)`,
 * so any other width is a fiction rather than an unimplemented case, and is a
 * hard error in BOTH configurations.
 */
#ifndef CQOPS_KERNELS_FSQRT_H
#define CQOPS_KERNELS_FSQRT_H

#include "bit.h"
#include "ctx.h"
#include "kernels/fpfield.h"
#include "scratch.h"

#include <stdint.h>

/* --- The program: one row per operator occurrence (PRD-v2 §7.2). ---------- */

/* Each row is ONE upstream lowering function applied once, a VIEW, an
 * ARITHMETIC-shift view, or a projection. The blocks are M16's `cq_eq_step`
 * (lower_eq!, arith.jl:424-447) and `cq_ult_step` (lower_ult!, :449-463);
 * M14's `cq_add_step` (lower_add!, adder.jl:1-18) and `cq_sub_step`
 * (lower_sub!, :148-172); M17's `cq_mux_step` (lower_mux!, arith.jl:522-532);
 * M31's class predicate; M32's `cq_norm52_step` and `cq_round_step`; and the
 * bitwise ops emitted directly one gate per slot as PRD-v2 §7.10 directs —
 * `lower_or!` (arith.jl:274-282), `lower_and!` (:268-272) and `lower_not1!`
 * (:474-478).
 *
 * THE THREE CLASS PREDICATES COMPOSE M31's BLOCK AND ARE NOT TRANSCRIBED
 * AGAIN. `fsqrt.jl:39-41` is `fadd.jl:29-33` row for row and is exactly M31's
 * `is_nan` / `is_inf` / `is_zero` (K22.md), so K21 spends three
 * `CQ_FSOP_CLASS` rows on them instead of the fourteen `eq` + `not1` + `and1`
 * rows the literal grain would give. THE SLOTS AND THE BITS ARE IDENTICAL
 * EITHER WAY — `cq_fp_class_steps` is `2*cq_eq_steps(64) + 2*n_eq + 1` — so
 * this is not a cost decision. It is Rule 1 applied to the call graph, M34's
 * own argument: a second copy here would be a second source of truth for a
 * predicate M31's suite already owns.
 *
 * `CQ_FSOP_` AND NOT `CQ_FSQRT_`: M36 measured that a row-op enum colliding
 * with a neighbouring enum renumbers it silently (C enums share one
 * namespace), so the op names are deliberately unlike everything else here. */
typedef enum {
    CQ_FSOP_VIEW = 0,  /* 0 slots, 0 bits: `(s0 >> shift) & mask`         */
    CQ_FSOP_SVIEW,     /* 0 slots, 0 bits: an ARITHMETIC `s0 >> shift`    */
    CQ_FSOP_OUT,       /* 0 slots, 0 bits: output `s1` of hand-off `s0`   */
    CQ_FSOP_CLASS,     /* M31: `s1` is a cq_fp_class over the 64-lane s0  */
    CQ_FSOP_EQ,        /* 2 x 64 -> 1 bit; the RAW wire is `a != b`       */
    CQ_FSOP_ULT,       /* 2 x 64 -> 1 bit; the RAW wire is `a >=u b`      */
    CQ_FSOP_ADD,       /* 2 x 64 -> 64                                    */
    CQ_FSOP_SUB,       /* 2 x 64 -> 64                                    */
    CQ_FSOP_MUX,       /* cond(1) + 2 x 64 -> 64                          */
    CQ_FSOP_OR,        /* 2 x 64 -> 64                                    */
    CQ_FSOP_NORM52,    /* _sf_normalize_to_bit52  -> (m, e)               */
    CQ_FSOP_ROUND,     /* _sf_round_and_pack      -> four values          */
    CQ_FSOP_NOT1,      /* 1 bit -> 1 bit                                  */
    CQ_FSOP_AND1,      /* 2 x 1  -> 1 bit                                 */
    CQ_FSOP_N_OP
} cq_fsqrt_op;

_Static_assert(CQ_FSOP_VIEW == 0,
               "a zero-initialised row must be the free one; a renumbering "
               "must break a build, not just a comment");

/* AN ARITHMETIC RIGHT SHIFT IS A SECOND KIND OF VIEW AND IT IS K21's ALONE SO
 * FAR. `fsqrt.jl:60` is `result_exp = (e_unb >> 1) + BIAS` with `e_unb` an
 * `Int64`, and `:53-56` states the requirement in upstream's own words: *"Use
 * arithmetic right shift to handle negative e_unb correctly (floor toward -inf
 * preserves the identity sqrt(m * 2^e) = sqrt(m or 2m) * 2^((e or e-1)/2))"*.
 * `e_unb` is NEGATIVE for every operand below 1.0 and for every subnormal, so
 * the sign replication is load-bearing rather than defensive.
 *
 * A `CQ_FSOP_VIEW` zero-FILLS its vacated lanes, so it cannot express this: a
 * logical shift is right for every `a >= 1.0` and wrong for every `a < 1.0`,
 * and it costs nothing either way, so NO COUNT CAN SEE THE DIFFERENCE. A
 * `CQ_FSOP_SVIEW` fills lane `i` with `s0[min(i + shift, 63)]`, so the vacated
 * lanes are COPIES OF A SCRATCH BIT rather than literal `CQ_BIT_ZERO` — which
 * is why it is the one view in K21 whose top lane does not fold, and why it
 * cannot join a collapsing `(shift, mask)` chain. Its operand must therefore
 * name a SPAN and not another view; that is a hard error in both
 * configurations. */

/* An operand slot: `>= 0` names an earlier ROW of the same program, and each
 * negative code below names something that owns no qubit at all — the rail, or
 * a constant span (§7.3). A constant and a view are only ever CONTROLS, so
 * they reach the emitter through cq_emit_*'s `const cq_bit *` parameters and
 * can never be materialised: I6(a) by construction.
 *
 * THE MASKS ARE NOT HERE, AND THAT IS PRD-v2 §7.3 AS AMENDED. `FRAC_MASK`,
 * `0x7FF`, `0x3F`, `3` and the `& Int64(1)` of :58 are VIEW masks — wiring —
 * not operands of an `and` block. Only the constants that are genuine block
 * operands are codes here. K21.md's draft counts five of them as `and`(64)
 * blocks and is superseded by the amendment; `& UInt64(3)` alone would have
 * been 64 blocks and 4,096 scratch qubits, because it runs once per iteration.
 *
 * `CQ_FS_K_ZERO` IS ALSO `r_0` AND `q_0` (fsqrt.jl:78-79), and that is a
 * DELIBERATE OVERTURN of K21.md §2.1's D9(d) reading — see fsqrt.c. */
enum {
    CQ_FS_A          = -1,   /* the rail `a`                    fsqrt.jl:31 */
    CQ_FS_K_ZERO     = -2,   /* `UInt64(0)`      :41 :45 :58 :78 :79 :96 :103 */
    CQ_FS_K_ONE      = -3,   /* `UInt64(1)` / `Int64(1)`        :46 :86 :89 */
    CQ_FS_K_BIAS     = -4,   /* `BIAS = Int64(1023)`            :32 :57 :60 */
    CQ_FS_K_IMPLICIT = -5,   /* softfloat_common.jl:9           :45         */
    CQ_FS_K_INF      = -6,   /* softfloat_common.jl:11          :113        */
    CQ_FS_K_QUIET    = -7,   /* softfloat_common.jl:13          :116        */
    CQ_FS_K_INDEF    = -8,   /* softfloat_common.jl:14          :115        */
    CQ_FS_N_CODE     = 8
};

/* `shift` is the RIGHT-shift amount and a NEGATIVE value is a left shift, so a
 * chain of views composes to one `(shift, mask)` pair — see fsqrt_operand.c.
 * Both are read only by a VIEW; an SVIEW reads `shift` alone; `s1` doubles as
 * the output index of a `CQ_FSOP_OUT` row and as the class of a
 * `CQ_FSOP_CLASS` row. */
typedef struct {
    short    op;             /* a cq_fsqrt_op                             */
    short    s0, s1, s2;     /* operands; only the first arity(op) read   */
    short    shift;          /* VIEW and SVIEW only                       */
    uint64_t mask;           /* VIEW only                                 */
} cq_fsqrt_row;

/* The outputs of the two M32 hand-offs, in the order softfloat_common.jl
 * returns them — :104 and :226. `_sf_round_and_pack` returns FOUR although its
 * docstring at :194 names three; the CODE is the specification, and
 * `fsqrt.jl:102` destructures four. THREE OF THE FOUR ARE DISCARDED HERE and
 * are still emitted, which is PRD-v2 §7.6's discarded-arm rule and upstream's
 * own statement at `fsqrt.jl:99-102`. */
enum {
    CQ_FS_OUT_M          = 0, /* norm52 :104  m_final                      */
    CQ_FS_OUT_E          = 1, /* norm52 :104  e_final                      */
    CQ_FS_OUT_NORMAL     = 0, /* round  :226  normal_result                */
    CQ_FS_OUT_OVERFLOW   = 1, /* round  :226  overflow_result       DEAD   */
    CQ_FS_OUT_EXPOVF     = 2, /* round  :226  exp_overflow    (1 b) DEAD   */
    CQ_FS_OUT_EXPOVF_AFT = 3  /* round  :226  ..._after_round (1 b) DEAD   */
};

/* The loop's shape, pinned rather than commented: `for i in 0:63`
 * (fsqrt.jl:80) over a 16-row body (:81-89). An iteration count of 63 or a
 * body of 15 rows must break a build assertion and a test, never just a
 * comment. */
enum { CQ_FSQRT_ITERS = 64, CQ_FSQRT_ITER_ROWS = 16 };

/* `soft_fsqrt` is 1,070 rows. Pinned so a table that outgrows the memoised
 * prefix map fails loudly rather than overrunning it, and asserted in fsqrt.c
 * rather than commented. */
enum { CQ_FSQRT_MAX_ROWS = 1080 };

/* The one program. `n` receives the row count. A row reference is an index
 * into THIS table and is never rebased: `soft_fsqrt` is one whole routine. */
const cq_fsqrt_row *cq_fsqrt_rows(int *n);
int                 cq_fsqrt_n_rows(void);

/* How many of s0/s1/s2 a row reads. A VIEW and an SVIEW read one; an OUT reads
 * `s0` as a row plus `s1` as an output INDEX, which is not an operand. */
int cq_fsqrt_arity(int op);

/* The width of row `i`'s value: 64, 1, or 0 for a hand-off row, whose value is
 * a TUPLE and which may only be named through a `CQ_FSOP_OUT` row. */
int cq_fsqrt_row_width(const cq_fsqrt_row *rows, int n, int i);

/* PRD-v2 §5's PRE-NORMALISE <-> THE DIGIT LOOP seam, as a NAMED ROW BOUNDARY
 * rather than a file split (see the header note). It is the index of the FIRST
 * row of iteration 0 — `a_hi >> 62` at :81 — so rows [0, seam) are the
 * PRE-NORMALISE half. `cq_fsqrt_loop_row(t, j)` is row `j` of iteration `t`,
 * exported so a test names a row rather than repeating the arithmetic, which
 * is exactly the number a boundary error moves. */
int cq_fsqrt_seam_row(void);
int cq_fsqrt_loop_row(int t, int j);

/* --- The block. ---------------------------------------------------------- */

/* `a` is CQ_FP64_W lanes and is CONTROLS ONLY. THE BLOCK ALLOCATES NOTHING:
 * the caller supplies `scr` and `off` and every span is `off`-relative, which
 * is load-bearing rather than tidy — a block cannot see its own offset, so
 * dropping `off` slides the whole program inside the caller's region and
 * leaves the value, the palindrome, the pool, every gate count AND the slot
 * scan all correct (M18's, M31's and M34's measured finding). Only a SECOND
 * program at a SECOND offset in one region sees it. */
typedef struct {
    const cq_bit *a;
    cq_scratch   *scr;
    uint32_t      off;
} cq_fsqrt_block;

/* How many bits of the caller's region the program owns, starting at `off` —
 * the sum over rows of each row's own internals, every term ASKED of its
 * module. Never written as a number here or in a test. */
uint32_t cq_fsqrt_region(void);

/* The SLOT count: the sum over rows of each block's step count, asked of its
 * owning module at width 64. ONE INVOLUTION PER SLOT, which is what makes
 * cq_sandwich's index reversal be gate reversal. NOT a gate count. */
int cq_fsqrt_steps(void);

/* One gate of the program, `u` in [0, cq_fsqrt_steps()). Out of range is a
 * hard error in BOTH configurations, for cq_eq_step's reason: an off-by-one
 * lands inside some inner block's OR-prefix or carry chain and emits a
 * plausible wrong gate rather than failing. */
void cq_fsqrt_step(cq_ctx *ctx, const cq_fsqrt_block *k, int u);

/* `result` (fsqrt.jl:117) — the last row's span, 64 lanes, inside the caller's
 * own region. `const`, for cq_fmul_result's reason: inside one compute half it
 * is a CONTROL for whatever comes next, and a consumer that WROTE into it
 * would make the reverse half non-cancelling. */
const cq_bit *cq_fsqrt_result(const cq_fsqrt_block *k);

/* Risk R9's classical row: the SAME Julia body evaluated in C over `uint64_t`
 * (PRD-v2 §7.4), composing M31's class eval and M32's two, NEVER the host
 * `sqrt()`. One of §7.4's five IEEE-unspecified cells is reachable from
 * `fsqrt` BY NAME — `sqrt(-1)` — and the NaN passthrough is two more; every
 * one is x86's choice, so this way the classical and quantum modes agree
 * bit-for-bit on every host. It is NOT an L1 oracle — that is the host
 * `sqrt()` with those cells pinned by table (tests/support/fphost.h). */
uint64_t cq_fsqrt_eval(uint64_t a);

/* --- The Rule 7 kernel. --------------------------------------------------- */

/* `dst ^= soft_fsqrt(a)`, with `a` 64 bits and unchanged and every internal
 * ancilla back at |0>. `W` must be 64.
 *
 * UNARY, ONE WIDTH — PRD-v2 §7.11, *"`fneg`/`fabs`/`sqrt`/rounding are unary,
 * one width"*. That departs from Rule 7's canonical parameter list and so
 * DECLARES ITS OWN SIGNATURE AND NAMES EVERY OPERAND EXPLICITLY; what Rule 7
 * fixes — `dst ^= f(a)`, `a` unchanged, every ancilla back at |0> — is
 * satisfied unchanged, which is `ckd.15`'s resolution applied again.
 * `cq_kernel_fn` stays arity-2 and is not widened, so K21 is reached through
 * `cq_kd_spec`'s `call` adapter with `.kernel` NULL, exactly as M31's
 * predicates and M17's mux are. There is no `_unc` entry point (uncompute is
 * the same call) and no `_controlled` variant (the axis is an emitter mode).
 *
 * `W` IS KEPT IN THE PARAMETER LIST ALTHOUGH IT CAN ONLY BE 64, because the
 * refusal is the point: a caller that believes in a 32-bit `soft_fsqrt` must
 * be told so rather than have its argument silently ignored. */
void cq_kernel_fsqrt(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W);

#endif /* CQOPS_KERNELS_FSQRT_H */
