/* src/kernels/fmisc.h — M38, K24, the first half of `fmisc`: the three SIGN
 * OPS (`fneg`, `fabs`, `copysign`) and `fmin` / `fmax`, at f64. PRD-v2 §5's
 * M38 row, §7.1-7.4, §7.7, §7.11, §7.12.
 *
 * Read docs/constructions/K24.md before changing anything here. The rounding
 * half of M38 (K25, `fround*`) is a different bead and a different file set.
 *
 * TWO SHAPES, ON PRD-v2 §5's RECORDED SEAM `SIGN OPS <-> EVERYTHING ELSE`.
 *
 * THE SIGN OPS ARE ONE UPSTREAM BLOCK EACH, EMITTED STRAIGHT ONTO `dst`, AND
 * THAT IS K1/K3's SHAPE (src/kernels/bitwise.c), NOT A SANDWICH. `fneg` is
 * extract/instructions.jl:7755-7762 — an `IRBinOp(:xor, a, sign_bit)`, i.e.
 * `lower_xor!` (lowering/arith.jl:284-290) with a constant second operand, and
 * the same body as `soft_fneg` (softfloat/fneg.jl:6). `fabs` is :4997-5001 —
 * an `and` with the constant `0x7FFF…`, which PRD-v2 §7.3 as amended makes a
 * VIEW, so its whole gate content is Rule 7's `^=` into `dst`: Bennett's
 * `_emit_copy_out!` (arith.jl:89-96), one CNOT per lane. `copysign` is
 * :5003-5017 — two constant `and`s (VIEWS) and one `lower_or!`
 * (arith.jl:274-282) over them. Every block writes its OWN fresh result wire in
 * upstream; a fresh zero `dst` is that wire, and the same call on `dst = out`
 * XORs the same value in again — so no scratch exists, Rule 2 has nothing to
 * return, and no sandwich is owed (K1's and K3's recorded reading). All three
 * are ZERO-Toffoli at every mask: `lower_or!`'s per-lane Toffoli has one
 * control from each of two DISJOINT views, so one of them is a constant ZERO on
 * every lane and the §3 fold table elides it. And all three have a TRUE L5 row
 * with no short-circuit of their own: on classical operands every gate is a
 * constant-controlled X/CX onto a constant `dst` lane, which the fold table
 * rewrites in place for zero gates and zero qubits.
 *
 * `fmin` / `fmax` ARE `soft_fmin` / `soft_fmax` (softfloat/fmin.jl:86-133) —
 * NOT `soft_fminimum` / `soft_fmaximum` in the same file, which are
 * NaN-PROPAGATING (PRD-v2 §7.7). A ROW TABLE per Julia body over step blocks
 * other modules export, one row per operator occurrence (§7.2's literal
 * grain): M31's class block for each `(e == 0x7FF) & (f != 0)`, M16's
 * `cq_eq_block` for each `==` / `!=`, M36's `cq_fcmp_block` for
 * `soft_fcmp_olt` (its STEP function — never a kernel), M17's `cq_mux_block`
 * for each `ifelse`, and upstream's own `lower_not1!` / `lower_and!` at one
 * lane. `fmin(NaN, NaN)` returns the CANONICAL `0x7FF8000000000000` — a
 * constant SOURCE, never a payload — and ±0 tie-breaks towards −0 for `fmin`
 * and +0 for `fmax` (fmin.jl:81-84, :111-112).
 *
 * WIDTH 64 AND NOTHING ELSE, FOR ALL FIVE, AND IT IS A REFUSAL RATHER THAN A
 * GAP. PRD-v2 §1 scopes v2 to f64 and every `soft_*` above takes `UInt64`, so
 * another width is a fiction — a hard error in BOTH configurations, which is
 * M40 fsqrt's and M39 fma's posture. Upstream's extractor would build `fneg`
 * at any `w`; that is not an f64 port's promise to make.
 */
#ifndef CQOPS_KERNELS_FMISC_H
#define CQOPS_KERNELS_FMISC_H

#include "bit.h"
#include "ctx.h"
#include "kernels/fpfield.h"
#include "scratch.h"

#include <stdint.h>

/* === SIGN OPS ============================================================ */

/* The SLOT count of each sign op — its one block's gate positions before the
 * fold table, NOT a gate count. `lower_xor!` is two per lane, `lower_or!`
 * three, `_emit_copy_out!` one; transcribed from the pinned source because no
 * module publishes a cost for a block emitted inline (PRD-v2 §7.10). */
int cq_fneg_steps(void);
int cq_fabs_steps(void);
int cq_copysign_steps(void);

/* `dst ^= a ^ 0x8000000000000000` / `dst ^= a & 0x7FFF…FFFF`. UNARY, ONE WIDTH
 * (PRD-v2 §7.11): the signature departs from Rule 7's parameter list and
 * names every operand, exactly as `cq_kernel_fsqrt` does, and is reached from
 * a test through `cq_kd_spec`'s `call` adapter. `W` must be 64. */
void cq_kernel_fneg(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W);
void cq_kernel_fabs(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W);

/* `dst ^= (x & 0x7FFF…) | (y & 0x8000…)` — Rule 7's canonical arity-2 shape.
 * NOT COMMUTATIVE: the magnitude comes from the FIRST operand. `W` must be
 * 64. */
void cq_kernel_copysign(cq_ctx *ctx, cq_bit *dst, const cq_bit *x,
                        const cq_bit *y, int W);

/* === fmin / fmax ========================================================= */

typedef enum { CQ_FMISC_MIN = 0, CQ_FMISC_MAX, CQ_FMISC_N_PROG } cq_fmisc_prog;

_Static_assert(CQ_FMISC_MIN == 0 && CQ_FMISC_N_PROG == 2,
               "fmin.jl:86-106 and :114-133 are the two programs; adding one "
               "is a deliberate act and must break a build");

/* One row is ONE upstream lowering function applied once. The prefix is FX
 * because FA FC FD FM FR FS FU FV are taken (bd m39-fma-landed-...): a
 * collision between two modules' op enums is M36's recorded trap. */
typedef enum {
    CQ_FXOP_CLASS_NAN = 0, /* M31: (e == 0x7FF) & (f != 0) over a whole rail  */
    CQ_FXOP_EQ,            /* M16 lower_eq!: 64-lane spans; raw wire is a!=b  */
    CQ_FXOP_NOT1,          /* lower_not1! at one lane (arith.jl:474-478)      */
    CQ_FXOP_AND1,          /* lower_and!  at one lane (arith.jl:268-272)      */
    CQ_FXOP_FCMP_OLT,      /* M36: soft_fcmp_olt(s0, s1), its 1-bit flag      */
    CQ_FXOP_MUX,           /* M17 lower_mux!: cond 1 bit, t and f 64 lanes    */
    CQ_FXOP_N_OP
} cq_fx_op;

/* An operand: `>= 0` names an earlier ROW of the same program; each negative
 * code names something that owns no qubit — a rail, a VIEW (a constant `and`,
 * PRD-v2 §7.3) or a constant SPAN. A view or a constant is only ever a
 * CONTROL, so it reaches the emitter through cq_emit_*'s `const cq_bit *`
 * parameters and can never be materialised: I6(a) by construction. */
enum {
    CQ_FX_A      = -1,   /* the rail `a`                                    */
    CQ_FX_B      = -2,   /* the rail `b`                                    */
    CQ_FX_ABS_A  = -3,   /* a & 0x7FFF…FFFF — a VIEW (fmin.jl:87, :115)     */
    CQ_FX_ABS_B  = -4,   /* b & 0x7FFF…FFFF — a VIEW (fmin.jl:88, :116)     */
    CQ_FX_SGN_A  = -5,   /* a & 0x8000…0000 — a VIEW, IN PLACE (:98, :126)  */
    CQ_FX_K_ZERO = -6,   /* UInt64(0)                — a constant SPAN      */
    CQ_FX_K_QNAN = -7    /* UInt64(0x7FF8000000000000) — a constant SPAN    */
};

typedef struct {
    short op;            /* a cq_fx_op                                      */
    short s0, s1, s2;    /* operands; only the first `arity(op)` are read   */
} cq_fx_row;

/* The program's rows, in SOURCE ORDER, and how many there are. The pointer is
 * to a static table; nothing is copied and nothing is cached. */
const cq_fx_row *cq_fmisc_rows(cq_fmisc_prog p, int *n);

/* How many of s0/s1/s2 a row reads. Unknown op: hard error. */
int cq_fmisc_arity(int op);

/* The width of row `i`'s value — 64 for a MUX, 1 for every other op. An index
 * outside the program is a hard error. */
int cq_fmisc_row_width(cq_fmisc_prog p, int i);

/* --- The block. ---------------------------------------------------------- */

/* `a` and `b` are CQ_FP64_W lanes each and are CONTROLS ONLY. THE BLOCK
 * ALLOCATES NOTHING: the caller supplies `scr` and `off` and every span is
 * `off`-relative, which is load-bearing rather than tidy — a block cannot see
 * its own offset, so dropping `off` slides the whole program and leaves the
 * value, the palindrome, the pool, every count and the slot scan correct
 * (M31's, M36's and M18's measured finding). Only a SECOND program at a
 * SECOND offset in one region sees it. The block names its PROGRAM, not a row
 * pointer, so no consumer can hand the machine a malformed table. */
typedef struct {
    const cq_bit  *a, *b;
    cq_scratch    *scr;
    uint32_t       off;
    cq_fmisc_prog  prog;
} cq_fmisc_block;

/* How many bits of the caller's region the program owns, starting at `off` —
 * the sum over rows of each row's own internals, every term ASKED of its
 * module. Never written as a number here or in a test. */
uint32_t cq_fmisc_region(cq_fmisc_prog p);

/* The SLOT count, every term asked of its owning module at width 64. ONE
 * INVOLUTION PER SLOT, which is what makes cq_sandwich's index reversal be
 * gate reversal. NOT a gate count. */
int cq_fmisc_steps(cq_fmisc_prog p);

/* One gate of the program, `u` in [0, cq_fmisc_steps(k->prog)). Out of range
 * is a hard error in BOTH configurations, for cq_eq_step's reason: an
 * off-by-one lands inside an inner block's OR-prefix or carry chain and emits
 * a plausible wrong gate rather than failing. */
void cq_fmisc_step(cq_ctx *ctx, const cq_fmisc_block *k, int u);

/* The last row's `r` span — 64 lanes inside the caller's region. `const`: a
 * consumer that WROTE into it would make the reverse half non-cancelling. */
const cq_bit *cq_fmisc_result(const cq_fmisc_block *k);

/* Risk R9's classical row: the SAME Julia body over `uint64_t` (PRD-v2 §7.4),
 * composing M31's and M36's own classical rows — NEVER the host `fmin()`,
 * whose two-NaN answer is a payload where upstream's is canonical. It is NOT
 * the L1 oracle; that is tests/support/fmiscref.c, written from IEEE
 * minNum/maxNum and the docstring's two extra rules. */
uint64_t cq_fmin_eval(uint64_t a, uint64_t b);
uint64_t cq_fmax_eval(uint64_t a, uint64_t b);

/* `dst ^= soft_fmin(a, b)` / `soft_fmax(a, b)`, `a` and `b` unchanged and every
 * internal ancilla back at |0>. Rule 7's canonical arity-2 shape; `W` must be
 * 64. No `_unc` entry point and no `_controlled` variant. */
void cq_kernel_fmin(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                    const cq_bit *b, int W);
void cq_kernel_fmax(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                    const cq_bit *b, int W);

#endif /* CQOPS_KERNELS_FMISC_H */
