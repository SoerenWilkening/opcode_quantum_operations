/* src/kernels/fma.h — M39, K20. `fma` at f64: `soft_fma` as a THREE-SOURCE
 * kernel. PRD-v2 §5's M39 row, §6.1, §7.1-7.4, §7.6, §7.9, §7.11, §7.12, §7.16.
 *
 * Read docs/constructions/K20.md before changing anything here. Every gate is
 * a port: `soft_fma` (third_party/bennett/src/softfloat/fma.jl:28-211) and the
 * NINE shared helpers it INLINES plus the three M32 owns as blocks
 * (softfloat_common.jl:32-35, :68-105, :174-191, :198-227, :265-292, :299-304,
 * :311-316, :324-329, :336-340, :348-353, :373-412, :433-465), transcribed ONE
 * ROW PER OPERATOR OCCURRENCE in source order (§7.2's literal grain) over the
 * step blocks M12, M14, M16, M17, M18, M31 and M32 already export, plus
 * upstream's own four-gate bitwise vocabulary. Nothing in this module is a
 * construction of ours.
 *
 * `fma` REUSES THE BLOCK VOCABULARY, NOT M34's PRODUCT NOR M33's ALIGN
 * (PRD-v2 §5's M39 row as amended 2026-09-18; bd memory
 * fma-reuses-block-vocabulary-not-fmuls-product-and-128bit-adds-need-no-carry).
 * The product here is `_sf_widemul_u64_to_128`'s 32/32 split, NOT `fmul`'s
 * 27/26 — softfloat_common.jl:261-263 says the 27/26 decomposition "assumes
 * <= 53-bit inputs" and Berkeley's `<<10` scaling makes `fma`'s operands 63
 * bits — and the align is the four-case 128-bit `_shiftRightJam128` twice, not
 * `fadd`'s one-case 64-bit shift. M33 and M34 owe this module nothing and
 * export it nothing; what it reuses is M18/M12/M16/M17/M14/M32's blocks,
 * exactly as M34 does.
 *
 * THE 128-BIT ADDS MATERIALISE THE CARRY AS A VALUE, so `cq_add_block` needs
 * NO carry-in and no carry-out. `_add128` (softfloat_common.jl:299-304) is
 * add + compare + mux + two adds, and `_sub128` and `_neg128` are the same
 * shape. Building a carry-chained 128-bit adder for this kernel would be a
 * re-derivation an implementer will think is owed; it is not (K20's third
 * finding, confirmed by M34's landing).
 *
 * THE SEAM PRD-v2 §5 RECORDS IS THE SINGLE-ROUNDING PATH, between `fma.jl:115`
 * and `:118` — PRODUCT-AND-ALIGN <-> THE SINGLE-ROUNDING PATH — and it is what
 * forbids spelling this kernel `fmul` then `fadd`. `round(a*b + c)` is not
 * `round(round(a*b) + c)`: the two differ at a round-half case (the Kahan
 * single-rounding witness), so a composition of the two shipped kernels would
 * be right on almost every input and wrong on exactly the inputs an `fma`
 * exists for. Like M34's and M35's, that seam is NOT a FILE cut — it is the
 * named row boundary `cq_fma_seam_row()` below, pinned by a case.
 *
 * THE SHAPE IS M36's, M32's, M34's AND M35's: A ROW TABLE PLUS A STEP MACHINE.
 * A row is one upstream lowering function applied once, or a VIEW, or a
 * projection of a hand-off's tuple. The kernel is ONE cq_sandwich over ONE
 * flat region whose step function dispatches a global index into those blocks
 * (PRD-v2 §7.1, src/kernels/divrem_u.c's shape).
 *
 * FIVE SEAMS, ALL RECORDED BEFORE A LINE WAS WRITTEN. M33's landing measured
 * that a row-table fp kernel needs FOUR machine seams rather than two (bd
 * memory a-row-table-fp-kernel-needs-three-seams-not-two); K20's program is
 * 395 rows over 21 ops, so a FIFTH was recorded with them.
 *
 *   fma_rows.inc  the PROGRAM TEXT — the row enumerators and the static table,
 *                 the port a reader checks against the Julia. It is an `.inc`
 *                 because Rule 12's own exemption is for exactly this ("move
 *                 the big static table into an .inc" is "a real escape hatch
 *                 rather than a rename that dodges the guard",
 *                 tools/check_loc.sh), and because the alternative is two
 *                 tables and therefore two chances to get one prefix sum
 *                 wrong — which K16.md §2.2's seam box refuses and M33/M34/M35
 *                 each refused in turn. It is included exactly ONCE, by fma.c.
 *   fma.c         the PROGRAM's ACCESSORS — `cq_fma_rows`, the arity, the row
 *                 widths, the seam row. Julia knowledge, no layout.
 *   fma_step.c    the COSTS AND THE LAYOUT: what a row costs (ASKED of
 *                 M12/M14/M16/M17/M18/M31/M32), and where its spans lie.
 *   fma_operand.c the OPERAND RESOLUTION: what each operand code resolves to —
 *                 the collapsing view chain, the constant vocabulary, the
 *                 block outputs, the hand-off projections and the flags.
 *   fma_emit.c    the DISPATCH and the SURFACE: which gate a slot emits, the
 *                 public block, and the three-source entry point.
 *                 `fma_int.h` is the seam across the middle three.
 *   fma_eval.c    the CLASSICAL body, which emits nothing (M32's `_eval` <->
 *                 THE CIRCUIT seam).
 *
 * SLOTS AND GATES COME APART, AS THEY DO IN M31, M32, M34, M35, M36 AND M40
 * (D-K18-6). §7.3's constants are `const cq_bit` SOURCES and a mask or shift
 * by a compile-time constant is a VIEW, so classical lanes sit inside block
 * operands at EVERY operand mask including the all-quantum one L4 pins.
 * `cq_fma_steps` is a SLOT count and is never a gate count; every composition
 * identity over this module is over slots.
 *
 * W MUST BE 64. PRD-v2 §1 scopes v2 to `f64` and `soft_fma` is
 * `(UInt64, UInt64, UInt64)`, so any other width is a fiction rather than an
 * unimplemented case, and is a hard error in BOTH configurations.
 */
#ifndef CQOPS_KERNELS_FMA_H
#define CQOPS_KERNELS_FMA_H

#include "bit.h"
#include "ctx.h"
#include "kernels/fpfield.h"
#include "scratch.h"

#include <stdint.h>

/* --- The program: one row per operator occurrence (PRD-v2 §7.2). ---------- */

/* Each row is ONE upstream lowering function applied once, a VIEW, or a
 * projection. The blocks are M16's `cq_eq_step` (lower_eq!, arith.jl:424-447),
 * `cq_ult_step` (lower_ult!, :449-463) and `cq_slt_step` (lower_slt!,
 * :465-472); M14's `cq_add_step` (lower_add!, adder.jl:1-18) and `cq_sub_step`
 * (lower_sub!, :148-172); M17's `cq_mux_step` (lower_mux!, arith.jl:522-532);
 * M18's `cq_mul_step` (lower_mul!, multiplier.jl:1-33); M12's `cq_barrel_step`
 * (lower_var_shl!, arith.jl:366-380, and lower_var_lshr!, :350-364); M31's
 * class block; M32's `norm52` / `subnorm` / `round` blocks; and the bitwise
 * ops emitted directly one gate per slot as PRD-v2 §7.10 directs — `lower_or!`
 * (arith.jl:274-282), `lower_xor!` (:284-291), `lower_and!` (:268-272) and
 * `lower_not1!` (:474-478).
 *
 * THERE IS NO `clz` OP HERE AND M34 HAS ONE. `soft_fma` does NOT call
 * `_sf_normalize_clz`; its renormalisation is `_sf_clz128_to_hi_bit61`
 * (softfloat_common.jl:433-465), a 128-BIT six-stage ladder that no other
 * routine in the snapshot calls and that M32 therefore does not own. It is
 * INLINED here, six stages of eleven rows, exactly as §7.2's grain requires.
 *
 * THE NINE CLASS PREDICATES COMPOSE M31's BLOCK AND ARE NOT TRANSCRIBED AGAIN.
 * `fma.jl:43-51` is `fmul.jl:31-36` row for row with a third operand, and is
 * exactly M31's `is_nan` / `is_inf` / `is_zero` (K22.md). Same slots, same
 * bits, same gates either way — so this is Rule 1 applied to the call graph
 * and not a cost decision, exactly as M34 and M36 record it.
 *
 * `CQ_FUOP_` AND NOT `CQ_FMA_`: M36 measured that a row-op enum colliding with
 * a neighbouring enum renumbers it silently (C enums share one namespace), so
 * the op names are deliberately unlike everything else here. */
typedef enum {
    CQ_FUOP_VIEW = 0,  /* 0 slots, 0 bits: `(s0 >> shift) & mask`         */
    CQ_FUOP_OUT,       /* 0 slots, 0 bits: output `s1` of hand-off `s0`   */
    CQ_FUOP_CLASS,     /* M31: `s1` is a cq_fp_class over the 64-lane s0  */
    CQ_FUOP_EQ,        /* 2 x 64 -> 1 bit; the RAW wire is `a != b`       */
    CQ_FUOP_ULT,       /* 2 x 64 -> 1 bit; the RAW wire is `a >=u b`      */
    CQ_FUOP_SLT,       /* 2 x 64 -> 1 bit; the RAW wire is `a >=s b`      */
    CQ_FUOP_ADD,       /* 2 x 64 -> 64                                    */
    CQ_FUOP_SUB,       /* 2 x 64 -> 64                                    */
    CQ_FUOP_MUX,       /* cond(1) + 2 x 64 -> 64                          */
    CQ_FUOP_AND,       /* 2 x 64 -> 64  (a RUNTIME mask; :383, :395)      */
    CQ_FUOP_OR,        /* 2 x 64 -> 64                                    */
    CQ_FUOP_XOR,       /* 2 x 64 -> 64  (also `~x`, an xor with all-ones) */
    CQ_FUOP_MUL,       /* 2 x 64 -> 64, the low W bits (M18, K11)         */
    CQ_FUOP_BSHL,      /* value(64), amount(64) -> 64   (M12, lower_var_shl!)  */
    CQ_FUOP_BLSHR,     /* value(64), amount(64) -> 64   (M12, lower_var_lshr!) */
    CQ_FUOP_NORM52,    /* _sf_normalize_to_bit52  -> (m, e)               */
    CQ_FUOP_SUBNORM,   /* _sf_handle_subnormal    -> five values          */
    CQ_FUOP_ROUND,     /* _sf_round_and_pack      -> four values          */
    CQ_FUOP_NOT1,      /* 1 bit -> 1 bit                                  */
    CQ_FUOP_AND1,      /* 2 x 1  -> 1 bit                                 */
    CQ_FUOP_OR1,       /* 2 x 1  -> 1 bit                                 */
    CQ_FUOP_N_OP
} cq_fma_op;

_Static_assert(CQ_FUOP_VIEW == 0,
               "a zero-initialised row must be the free one; a renumbering "
               "must break a build, not just a comment");

/* An operand slot: `>= 0` names an earlier ROW of the same program, and each
 * negative code below names something that owns no qubit at all — a rail or a
 * constant span (§7.3). A constant and a view are only ever CONTROLS, so they
 * reach the emitter through cq_emit_*'s `const cq_bit *` parameters and can
 * never be materialised: I6(a) by construction.
 *
 * THE VIEW MASKS ARE NOT HERE, AND THAT IS PRD-v2 §7.3 AS AMENDED. `0x7FF`,
 * `FRAC_MASK`, `0xFFFFFFFF`, `0x3F`, `(1<<56)-1` and the six CLZ probe masks
 * are VIEW masks — wiring — not operands of an `and` block. Only the constants
 * that are genuine block operands are codes here.
 *
 * `BIAS` (fma.jl:29) IS ASSIGNED AND NEVER READ — `:79` spells its own
 * `Int64(0x3FE)` — so it is not a row and not a code. K16 records the
 * identical shape for `fmul.jl:15`'s `SIGN_MASK` and K18 for `fcmp.jl:9`. */
enum {
    CQ_FU_A          = -1,   /* the rail `a`                     fma.jl:28  */
    CQ_FU_B          = -2,   /* the rail `b`                                */
    CQ_FU_C          = -3,   /* the rail `c`                                */
    CQ_FU_K_ZERO     = -4,   /* `UInt64(0)` / `Int64(0)`         :61, :112  */
    CQ_FU_K_ONE      = -5,   /* `UInt64(1)` / `Int64(1)`         :64, :183  */
    CQ_FU_K_ONES     = -6,   /* `Int64(-1)`; also `~x`'s xor mask :101, :325 */
    CQ_FU_K_2        = -7,   /* softfloat_common.jl:457                     */
    CQ_FU_K_4        = -8,   /* softfloat_common.jl:452                     */
    CQ_FU_K_8        = -9,   /* softfloat_common.jl:447                     */
    CQ_FU_K_16       = -10,  /* softfloat_common.jl:442                     */
    CQ_FU_K_32       = -11,  /* softfloat_common.jl:437                     */
    CQ_FU_K_63       = -12,  /* `clamp(dist, 1, 63)`  common.jl:380         */
    CQ_FU_K_64       = -13,  /* :149, and common.jl:385, :388, :389, :406   */
    CQ_FU_K_127      = -14,  /* `clamp(dist, 64, 127)` common.jl:388        */
    CQ_FU_K_128      = -15,  /* common.jl:405                               */
    CQ_FU_K_3FE      = -16,  /* `Int64(0x3FE)`                   :79        */
    CQ_FU_K_2P61     = -17,  /* `UInt64(0x2000000000000000)`     :83        */
    CQ_FU_K_IMPLICIT = -18,  /* softfloat_common.jl:9            :61-63     */
    CQ_FU_K_QUIET    = -19,  /* softfloat_common.jl:13           :33-35     */
    CQ_FU_K_INF      = -20,  /* softfloat_common.jl:11           :205       */
    CQ_FU_K_INDEF    = -21,  /* softfloat_common.jl:14           :206-207   */
    CQ_FU_N_CODE     = 21
};

/* `CQ_FU_N_CODE` WAS DECLARED, NEVER REFERENCED AND NEVER ASSERTED until
 * review round 1 — a comment wearing an enumerator's clothes, and a comment is
 * the one thing no test in this project reads. It is now pinned from BOTH
 * sides, because neither side alone is enough and C has no way to name "the
 * last enumerator":
 *
 *   - THIS ASSERT catches the count moving away from the last NAMED code. The
 *     codes run -1 downwards with no gaps, so `-CQ_FU_K_INDEF` IS the count.
 *     Bumping `CQ_FU_N_CODE` for a new code without re-anchoring this line
 *     breaks the BUILD, which is what forces the anchor to follow.
 *   - THE TWO TEST CASES catch the other direction, which this assert cannot
 *     see: a code added at -22 while the count stays 21 leaves `21 ==
 *     -CQ_FU_K_INDEF` true. `tests/test_kernel_fma_map.inc`'s
 *     `every_declared_operand_code_resolves` requires every code in
 *     `[-1, -CQ_FU_N_CODE]` to resolve, and the death case
 *     `an_unknown_64_lane_operand_code` requires `-(CQ_FU_N_CODE + 1)` to
 *     ABORT — which it stops doing the moment an unbumped code occupies it. */
_Static_assert(CQ_FU_N_CODE == -CQ_FU_K_INDEF,
               "CQ_FU_N_CODE must equal the number of negative operand codes; "
               "a new code must break a build, not just a comment");

/* `shift` is the RIGHT-shift amount and a NEGATIVE value is a left shift, so a
 * chain of views composes to one `(shift, mask)` pair — see fma_operand.c.
 * Both are read only when `op == CQ_FUOP_VIEW`; `s1` doubles as the output
 * index of a `CQ_FUOP_OUT` row and as the class of a `CQ_FUOP_CLASS` row. */
typedef struct {
    short    op;             /* a cq_fma_op                               */
    short    s0, s1, s2;     /* operands; only the first arity(op) read   */
    short    shift;          /* VIEW only                                 */
    uint64_t mask;           /* VIEW only                                 */
} cq_fma_row;

/* The outputs of the three M32 hand-offs, in the order softfloat_common.jl
 * returns them — :104, :190 and :226. `_sf_round_and_pack` returns FOUR
 * although its docstring at :194 names three; the CODE is the specification
 * (K23.md, M32's landing note). A `CQ_FUOP_OUT` row's `s1` is one of these. */
enum {
    CQ_FU_OUT_M          = 0, /* norm52  :104  m_final                     */
    CQ_FU_OUT_E          = 1, /* norm52  :104  e_final                     */
    CQ_FU_OUT_WR         = 0, /* subnorm :190  wr                          */
    CQ_FU_OUT_EXP        = 1, /* subnorm :190  result_exp                  */
    CQ_FU_OUT_FLUSHED    = 2, /* subnorm :190  flushed_result (a VIEW)     */
    CQ_FU_OUT_SUBNORMAL  = 3, /* subnorm :190  subnormal       (1 bit)     */
    CQ_FU_OUT_FTZ        = 4, /* subnorm :190  flush_to_zero   (1 bit)     */
    CQ_FU_OUT_NORMAL     = 0, /* round   :226  normal_result               */
    CQ_FU_OUT_OVERFLOW   = 1, /* round   :226  overflow_result             */
    CQ_FU_OUT_EXPOVF     = 2, /* round   :226  exp_overflow    (1 bit)     */
    CQ_FU_OUT_EXPOVF_AFT = 3  /* round   :226  ..._after_round (1 bit)     */
};

/* `soft_fma` is 395 rows. Pinned so a table that outgrows the prefix-offset
 * buffer fails loudly rather than overrunning a stack array, and asserted in
 * fma.c rather than commented. */
enum { CQ_FMA_MAX_ROWS = 420 };

/* The one program. `n` receives the row count. A row reference is an index
 * into THIS table and is never rebased: `soft_fma` is one whole routine. */
const cq_fma_row *cq_fma_rows(int *n);
int               cq_fma_n_rows(void);

/* How many of s0/s1/s2 a row reads. A VIEW reads one; an OUT reads `s0` as a
 * row plus `s1` as an output INDEX, which is not an operand; a CLASS reads
 * `s0` as 64 lanes plus `s1` as a class, likewise not an operand. */
int cq_fma_arity(int op);

/* The width of row `i`'s value: 64, 1, or 0 for a hand-off row, whose value is
 * a TUPLE and which may only be named through a `CQ_FUOP_OUT` row. */
int cq_fma_row_width(const cq_fma_row *rows, int n, int i);

/* PRD-v2 §5's `fma.jl:115`/`:118` seam, as a NAMED ROW BOUNDARY rather than a
 * file split (see the header note). It is the index of the FIRST row below the
 * seam — `_add128`'s own first row at `:118` — so rows [0, seam) are
 * PRODUCT-AND-ALIGN and the rest are THE SINGLE-ROUNDING PATH. Exported so a
 * test can pin what crosses it without repeating the row index, which is
 * exactly the number a boundary error moves. */
int cq_fma_seam_row(void);

/* --- The block. ---------------------------------------------------------- */

/* `a`, `b` and `c` are CQ_FP64_W lanes each and are CONTROLS ONLY. THE BLOCK
 * ALLOCATES NOTHING: the caller supplies `scr` and `off` and every span is
 * `off`-relative, which is load-bearing rather than tidy — a block cannot see
 * its own offset, so dropping `off` slides the whole program inside the
 * caller's region and leaves the value, the palindrome, the pool, every gate
 * count AND the slot scan all correct (M18's and M31's measured finding). Only
 * a SECOND program at a SECOND offset in one region sees it. */
typedef struct {
    const cq_bit *a, *b, *c;
    cq_scratch   *scr;
    uint32_t      off;
} cq_fma_block;

/* How many bits of the caller's region the program owns, starting at `off` —
 * the sum over rows of each row's own internals, every term ASKED of its
 * module. Never written as a number here or in a test. */
uint32_t cq_fma_region(void);

/* The SLOT count: the sum over rows of each block's step count, asked of its
 * owning module at width 64. ONE INVOLUTION PER SLOT, which is what makes
 * cq_sandwich's index reversal be gate reversal. NOT a gate count. */
int cq_fma_steps(void);

/* One gate of the program, `u` in [0, cq_fma_steps()). Out of range is a hard
 * error in BOTH configurations, for cq_eq_step's reason: an off-by-one lands
 * inside some inner block's OR-prefix, carry chain, barrel stage or
 * partial-product schedule and emits a plausible wrong gate rather than
 * failing. */
void cq_fma_step(cq_ctx *ctx, const cq_fma_block *k, int u);

/* `result` (fma.jl:210) — the last row's span, 64 lanes, inside the caller's
 * own region. `const`, for cq_mul_product's reason: inside one compute half it
 * is a CONTROL for whatever comes next, and a consumer that WROTE into it
 * would make the reverse half non-cancelling. */
const cq_bit *cq_fma_result(const cq_fma_block *k);

/* Risk R9's classical row: the SAME Julia body evaluated in C over `uint64_t`
 * (PRD-v2 §7.4), composing M31's and M32's `*_eval` bodies, NEVER the host
 * `fma()` and never the host `double` operators. Four of §7.4's five
 * IEEE-unspecified cells are reachable from `fma` and every one of them is
 * x86's choice; this way the classical and quantum modes agree bit-for-bit on
 * every host. The 128-bit intermediates are a `(hi, lo)` pair of `uint64_t`
 * exactly as upstream holds them (softfloat_common.jl:235-252 records that
 * native `UInt128` was declined for that reason) — never `unsigned __int128`,
 * which is not C11 and which PRD §14's "nothing beyond libc" has no room for.
 * It is NOT an L1 oracle — that is the host operator with those cells pinned
 * by table (tests/support/fphost.h). */
uint64_t cq_fma_eval(uint64_t a, uint64_t b, uint64_t c);

/* --- The three-source kernel. --------------------------------------------- */

/* `dst ^= soft_fma(a, b, c)`, with `a`, `b` and `c` 64 bits and unchanged and
 * every internal ancilla back at |0>. `W` must be 64.
 *
 * RULE 7 FIXES THE SEMANTICS AND NOT THE ARITY (`ckd.15`, PRD-v2 §7.11:
 * "`fma` declares three sources exactly as the mux does, reached through
 * `cq_kd_spec`'s `call` adapter with `.kernel` NULL"). `cq_kernel_fn` is NOT
 * widened; the guard is `cq_kernel_check_n(dst, W, src, w, 3)`, the N-ary form
 * that sizes every overlap range per operand — `cq_kernel_check_dst`'s arity-2
 * wrapper cannot express three sources at all. There is no `_unc` entry point
 * (uncompute is the same call) and no `_controlled` variant (the axis is an
 * emitter mode). D7b is a hard error here at all THREE pairs, exactly as it is
 * for every other kernel: the defensive copy is the shim's, at the M26 handle
 * boundary, and a kernel that sees an alias is looking at a missing copy. */
void cq_kernel_fma(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, const cq_bit *b,
                   const cq_bit *c, int W);

#endif /* CQOPS_KERNELS_FMA_H */
