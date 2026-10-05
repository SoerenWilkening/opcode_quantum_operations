/* src/kernels/ibits.h — M41, K26. The integer intrinsics `ctpop`, `ctlz`,
 * `cttz`, `bitreverse`, `bswap` (PRD-v2 §7.14: v1-SHAPED integer kernels,
 * not floating point). docs/constructions/K26.md is the construction spec.
 *
 * WHAT IS PORTED IS AN EXTRACTOR EXPANSION, NOT A LOWERING. Bennett has no
 * `lower_ctpop!`; `third_party/bennett/src/extract/instructions.jl:4826-4946`
 * rewrites each `llvm.<name>.iN` call into a list of IR instructions
 * (IRBinOp / IRICmp / IRSelect), which Bennett then lowers one instruction at
 * a time. We transcribe THAT list, one row per IR instruction (PRD-v2 §7.2's
 * literal grain): no CSE, no narrowing, every intermediate W lanes wide.
 *
 *   IRBinOp :add  -> M14's `cq_add_block`   (lower_add!, adder.jl:2-17)
 *   IRICmp  :ne   -> M16's `cq_eq_block`, its RAW flag `a != b` (see D-K26-2)
 *   IRSelect      -> M17's `cq_mux_step`    (lower_mux!, arith.jl:522-532)
 *   IRBinOp :or   -> `lower_or!` emitted here, one gate per slot (:274-282)
 *   IRBinOp :lshr / :shl / :and by a CONSTANT -> a VIEW (PRD-v2 §7.3 as
 *                    amended): wiring, no block, no scratch, no gate
 *   `iconst(v)`   -> a constant SOURCE span, never scratch (§7.3)
 *
 * `llvm.fshl` / `llvm.fshr` (:4947-4996) ARE NOT HERE. The expansion does not
 * equal LLVM's funnel shift at a shift amount of 0 mod W; K26.md §6 carries
 * the witness. They stay unlanded (the brief's 9ve.34 shape).
 *
 * SEAMS (recorded in advance, K26.md §2.0): ibits.c holds the ROW TABLES —
 * the transcription a reader checks against the Julia — and nothing else;
 * ibits_step.c is the STEP MACHINE (slot/region arithmetic ASKED of M14/M16/
 * M17, operand views, the dispatch) and knows no Julia; ibits_kernel.c is
 * the ENTRY POINTS, the R9 classical short-circuit and the sandwich.
 */
#ifndef CQOPS_KERNELS_IBITS_H
#define CQOPS_KERNELS_IBITS_H

#include "bit.h"
#include "ctx.h"
#include "reg.h"
#include "scratch.h"

#include <stdint.h>

/* --- The five kernels. Rule 7, UNARY and ONE WIDTH: dst ^= f(a). ---------
 *
 * `W` is the operand width and the result width alike (LLVM's `iN` in, `iN`
 * out). 1 <= W <= CQ_REG_WIDTH_MAX; `bswap` additionally requires
 * W % 16 == 0 (LLVM rejects `bswap.i8`; intrinsic_table.yaml:100-102) and
 * refuses every other width in BOTH configurations.
 *
 * `ctlz` / `cttz` TAKE NO `is_zero_poison` OPERAND. The expansion reads only
 * `ops[1]` (instructions.jl:4852-4853, :4874-4875) and its first row seeds
 * the result with `W`, so x == 0 gives W whatever the flag says —
 * deterministic, D3's posture. The shim drops the i1 immarg (K26.md D-K26-4). */
void cq_kernel_ctpop     (cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W);
void cq_kernel_ctlz      (cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W);
void cq_kernel_cttz      (cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W);
void cq_kernel_bitreverse(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W);
void cq_kernel_bswap     (cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W);

/* --- The program: one row per IR instruction of the expansion. ----------- */

typedef enum {
    CQ_IBF_CTPOP = 0, CQ_IBF_CTLZ, CQ_IBF_CTTZ, CQ_IBF_BITREVERSE,
    CQ_IBF_BSWAP,
    CQ_IBF_N
} cq_ibits_fn;

/* Prefixed CQ_IBOP_ and not CQ_IBF_, on purpose: a row-op enum sharing a
 * prefix with the function enum is the recorded fcmp collision
 * (`bd memories fp-kernel-shape-is-a-row-table-plus-a-step-machine`). */
typedef enum {
    CQ_IBOP_ADD = 0,   /* s0 + s1 mod 2^W     region 2W (t ++ c), out t    */
    CQ_IBOP_NE,        /* s0 != s1, one flag  region 2W-1 (diff ++ orr)    */
    CQ_IBOP_MUX,       /* s0 ? s1 : s2        region 2W (r ++ d), out r    */
    CQ_IBOP_OR,        /* s0 | s1             region W, out the W bits     */
    CQ_IBOP_N
} cq_ibits_op;

/* An operand is a VIEW over a base: `((base >> shr) & low(keep)) << shl`,
 * lane j of the W-lane result being base[j - shl + shr] when
 * 0 <= j - shl < keep and j - shl + shr < W, and CQ_BIT_ZERO otherwise.
 * That is exactly the IR's `lshr k` then `and (2^keep - 1)` then `shl k`
 * with Bennett's constant-shift semantics (a shift by W empties the vector,
 * arith.jl:305-320) — wiring, never a block (PRD-v2 §7.3).
 *
 * The base is the rail `a` (CQ_IBS_A), a constant `iconst(k)` (CQ_IBS_K), or
 * an EARLIER row (>= 0), whose output span it reads. A CQ_IBOP_NE row has a
 * ONE-bit output and may only be a MUX condition (s0). */
enum { CQ_IBS_A = -1, CQ_IBS_K = -2 };

typedef struct {
    short src;              /* CQ_IBS_A, CQ_IBS_K, or an earlier row       */
    short shr, keep, shl;   /* the view; keep == W and 0, 0 is the identity */
    short k;                /* the constant's value when src == CQ_IBS_K    */
} cq_ibits_opnd;

typedef struct {
    short         op;       /* a cq_ibits_op                                */
    cq_ibits_opnd s[3];     /* only the first arity(op) are read            */
} cq_ibits_row;

/* `ctlz`/`cttz` are the longest program: a seed, W (`ne`, `select`) pairs and
 * the closing `add 0` — 2W + 2 rows. */
enum { CQ_IBITS_MAX_ROWS = 2 * (int)CQ_REG_WIDTH_MAX + 2 };

/* Builds `f`'s program at width W into `out` (CQ_IBITS_MAX_ROWS rows) and
 * returns its row count. Refuses an unknown `f`, a width outside
 * [1, CQ_REG_WIDTH_MAX] and a `bswap` width that is not a multiple of 16. */
int cq_ibits_program(cq_ibits_fn f, int W, cq_ibits_row *out);
const char *cq_ibits_name(cq_ibits_fn f);

/* Slot and region totals of a program, every block's cost ASKED of its own
 * module. One gate per slot (bd ckd.14a), so a slot is a step. */
int      cq_ibits_steps (const cq_ibits_row *rows, int n, int W);
uint32_t cq_ibits_region(const cq_ibits_row *rows, int n, int W);

/* A slot's cost per row, exposed for the composition check and the scan. */
int      cq_ibits_row_steps (const cq_ibits_row *r, int W);
uint32_t cq_ibits_row_region(const cq_ibits_row *r, int W);

/* --- The block. ---------------------------------------------------------
 *
 * `a` is W lanes and CONTROLS ONLY. `scr` is the caller's ONE region and
 * `off` this program's base inside it; the program's spans are laid out in
 * row order by one prefix walk, `off` being added in exactly one place.
 * The block allocates nothing (plan §0.4 obligation 2). */
typedef struct {
    const cq_bit       *a;
    cq_scratch         *scr;
    uint32_t            off;
    const cq_ibits_row *rows;
    int                 n_rows;
    int                 W;
} cq_ibits_block;

/* One gate of the program, `u` in [0, cq_ibits_steps(...)). Out of range is
 * a hard error in BOTH configurations. */
void cq_ibits_step(cq_ctx *ctx, const cq_ibits_block *k, int u);

/* The last row's output — the value the copy-out reads, W lanes inside the
 * caller's region. The last row is always an `add` (`dest = prev + 0`). */
const cq_bit *cq_ibits_result(const cq_ibits_block *k);

/* THE LAYOUT OF ONE BOUND BLOCK, BUILT ONCE PER CALL RATHER THAN ONCE PER
 * GATE — M34's fmul_step_armed shape (bd
 * a-395-row-flat-table-needs-the-memoised-prefix-map). Until 2026-10-05 every
 * step re-tallied the whole program and walked it linearly to its row; at
 * ctlz/cttz's 2W + 2 rows that was the top of test_kernel_ibits' self time
 * (macOS `sample`). Every row's ABSOLUTE offset — `cq_ibits_arm` is the one
 * place `off` is added — and first slot, plus the totals: a pure function of
 * the block's rows, W and `off`, holding no operand and no circuit state
 * (Rule 13). The arm performs every refusal `cq_ibits_step` and
 * `cq_ibits_result` perform before they read a row; the Rule 7 kernel arms
 * once and keeps the map in its env. `step0` is non-decreasing, so the
 * dispatch searches for the LAST row whose `step0 <= u`. */
typedef struct {
    uint32_t off[CQ_IBITS_MAX_ROWS];
    int      step0[CQ_IBITS_MAX_ROWS + 1];
    int      steps;
    uint32_t bits;
} cq_ibits_map;

void          cq_ibits_arm(const cq_ibits_block *k, cq_ibits_map *m);
void          cq_ibits_step_armed(cq_ctx *ctx, const cq_ibits_block *k,
                                  const cq_ibits_map *m, int u);
const cq_bit *cq_ibits_result_armed(const cq_ibits_block *k,
                                    const cq_ibits_map *m);

#endif /* CQOPS_KERNELS_IBITS_H */
