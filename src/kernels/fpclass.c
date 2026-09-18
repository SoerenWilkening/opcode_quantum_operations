/* src/kernels/fpclass.c — M31, K22, the PREDICATES half. PRD-v2 §5's seam.
 *
 * Read docs/constructions/K22.md and fpclass.h before changing anything here.
 * The table below IS the port; everything else is slot arithmetic.
 *
 * ONE GATE PER SLOT, AND IT IS FORCED (bd ckd.14a, sandwich.h). cq_sandwich
 * runs the reverse pass by re-calling compute(env, s) with the SAME index, so
 * a step undoes itself only if it is an involution. Every branch below emits
 * exactly one X, CX or CCX, or delegates to cq_eq_step which does the same.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. Every target is a bit of the
 * caller's region — an `eq` block's `diff`/`orr`, a `not1`'s fresh wire, the
 * flag. `a` reaches the emitter only through `const cq_bit *` controls, and so
 * do the views and constant spans built from it, so a source cannot be
 * materialised here by construction. `dst` is touched exclusively in the
 * copy-out, which the driver runs with the extent deliberately disarmed.
 *
 * THE VIEWS AND CONSTANTS ARE REBUILT PER STEP, ON PURPOSE. They could be
 * cached in the block and filled by a bind call; they are not, because the
 * block would then carry state whose initialisation a consumer can forget, and
 * a forgotten bind is a silent wrong circuit rather than a failure. Rebuilding
 * is 128 struct copies against a gate that reaches a sink, it keeps
 * `cq_fp_class_block` exactly the four fields fpclass.h documents, and it
 * means the view code is exercised on every single step rather than once.
 */

#include "kernels/fpclass.h"

#include "emit.h"
#include "kernels/cmp.h"
#include "kernels/kernel.h"
#include "sandwich.h"

/* THE FOUR ROWS, TRANSCRIBED ONCE, AND READ BY BOTH MODES. `ce`/`cf` are the
 * constants the source compares against and `eq_e`/`eq_f` say whether it
 * spells `==` (1) or `!=` (0). The circuit and the classical short-circuit
 * below are both generated from this table, which is PRD-v2 §7.4's whole point
 * — "the same source, evaluated on constants" — so the two modes cannot
 * disagree. L1's oracle is the HOST (isnan/isinf/…), which shares nothing with
 * this table and is what catches a wrong row.
 *
 *   is_nan        (ea == 0x7FF) & (fa != 0)      fadd.jl:29
 *   is_inf        (ea == 0x7FF) & (fa == 0)      fadd.jl:31
 *   is_zero       (ea == 0)     & (fa == 0)      fadd.jl:33
 *   is_subnormal  (ea == 0)     & (fa != 0)      flog.jl:262
 *
 * `!=` COSTS ONE BLOCK AND `==` COSTS TWO, WHICH IS UPSTREAM'S ASYMMETRY AND
 * NOT OURS. `cq_eq_flag`'s wire holds `a != b` — Bennett's trailing NOT is the
 * copy-out's, folded there by K09.md §5 delta 2 — so a `!=` occurrence reads
 * the raw wire and an `==` occurrence needs `lower_not1!` on top. Reading the
 * flag as "equal" gives the same gates, the same count, the same palindrome
 * and clean scratch; only L1 against the host sees it. */
typedef struct {
    uint64_t ce;      /* the exponent test's constant operand */
    int      eq_e;    /* 1 when the source spells `==`        */
    uint64_t cf;      /* the fraction test's constant operand */
    int      eq_f;
} fp_row;

static const fp_row FP_ROWS[CQ_FP_N_CLASS] = {
    { CQ_FP64_EXP_ALL, 1, UINT64_C(0), 0 },
    { CQ_FP64_EXP_ALL, 1, UINT64_C(0), 1 },
    { UINT64_C(0),     1, UINT64_C(0), 1 },
    { UINT64_C(0),     1, UINT64_C(0), 0 }
};

/* `lower_not1!` (arith.jl:474-478) is `CNOT(w, r); NOT(r)` into a FRESH wire,
 * so two slots and one bit. `lower_and!` (arith.jl:268-272) is one Toffoli per
 * lane, so at one lane it is one slot and no bit of its own — it targets the
 * flag. Both are emitted directly here, PRD-v2 §7.10's disposition. */
enum { FP_NOT1_STEPS = 2, FP_AND1_STEPS = 1 };

static const fp_row *row_of(cq_fp_class cls)
{
    if ((int)cls < 0 || (int)cls >= (int)CQ_FP_N_CLASS)
        cq_kernel_die("fpclass: class outside [0, CQ_FP_N_CLASS)");
    return &FP_ROWS[cls];
}

/* One `eq` block's internals, which is what a consumer's layout budgets for
 * it: cmp.h's `2W - 1` (diff W ++ orr W-1), ASKED of nothing because M16
 * publishes it as a contract rather than as a function. The step COUNT is
 * asked — cq_eq_steps — and that is the number a narrowing would move. */
enum { FP_EQ_BITS = 2 * CQ_FP64_W - 1 };

uint32_t cq_fp_class_region(cq_fp_class cls)
{
    const fp_row *r = row_of(cls);

    return (uint32_t)(2 * FP_EQ_BITS + r->eq_e + r->eq_f + 1);
}

int cq_fp_class_steps(cq_fp_class cls)
{
    const fp_row *r = row_of(cls);

    return 2 * cq_eq_steps(CQ_FP64_W)
         + (r->eq_e + r->eq_f) * FP_NOT1_STEPS
         + FP_AND1_STEPS;
}

/* EVERY SPAN GOES THROUGH HERE AND EVERY SPAN IS `off`-RELATIVE. Dropping the
 * `k->off` below slides the block wholesale inside the caller's region: right
 * value, palindrome intact, clean pool, every case green. The only detector is
 * two blocks at two offsets in one region — they collide — which is what
 * tests/test_kernel_fpfield_block.inc's two-block case exists for. */
static cq_bit *span(const cq_fp_class_block *k, uint32_t rel, uint32_t len)
{
    return cq_scratch_span(k->scr, k->off + rel, len);
}

/* The region check is the BLOCK's, and it has to be the block's: without it
 * the first out-of-region span aborts in M08 with "scratch: …", which names
 * the region rather than the consumer that mis-sized its offset. Disjoint
 * message, hard error in both configurations, and cheap enough to run on every
 * step so slot 0 catches it. */
static void check_region(const cq_fp_class_block *k)
{
    if (k->scr == NULL)
        cq_kernel_die("fpclass: the block has no region");
    if ((uint64_t)k->off + cq_fp_class_region(k->cls)
        > (uint64_t)cq_scratch_size(k->scr))
        cq_kernel_die("fpclass: the block's region does not fit at its offset");
}

/* Side 0 is the EXPONENT test and side 1 the FRACTION test — source order
 * (fadd.jl:29), which is also the order the slots run in.
 *
 * THE BLOCK RUNS AT W = 64 OVER THE ASSEMBLED VIEW, NEVER AT 11 OVER THE RAW
 * FIELD. PRD-v2 §7.2 forbids narrowing towards the field width; D9's K12
 * precedent refused the same trade; and K11's finding is that the one mutant
 * L1 cannot see is the one that looks like an optimisation. What the narrowing
 * would buy is visible in the emitted counts — 53 of the exponent view's lanes
 * are constant and fold — and that saving is already had for free. */
static void eq_layout(const cq_fp_class_block *k, int side, cq_eq_block *e)
{
    const fp_row *r = row_of(k->cls);
    uint32_t base = (side == 0) ? 0u : (uint32_t)(FP_EQ_BITS + r->eq_e);

    e->a    = NULL;
    e->b    = NULL;
    e->diff = span(k, base,                       (uint32_t)CQ_FP64_W);
    e->orr  = span(k, base + (uint32_t)CQ_FP64_W, (uint32_t)CQ_FP64_W - 1u);
    e->W    = CQ_FP64_W;
}

/* The two operands, assembled into the CALLER's storage: a view over `a`'s
 * field and a constant span. Both are read-only and own nothing. */
static void eq_operands(const cq_fp_class_block *k, int side,
                        cq_bit *view, cq_bit *konst, cq_eq_block *e)
{
    const fp_row *r = row_of(k->cls);

    if (side == 0) { cq_fp_view_exp (k->a, view); cq_fp_const(konst, r->ce); }
    else           { cq_fp_view_frac(k->a, view); cq_fp_const(konst, r->cf); }

    e->a = view;
    e->b = konst;
}

/* `lower_not1!`'s output wire for side `side`, which exists only where that
 * side's occurrence is `==`. */
static cq_bit *not1_bit(const cq_fp_class_block *k, int side)
{
    const fp_row *r = row_of(k->cls);
    uint32_t rel = (side == 0) ? (uint32_t)FP_EQ_BITS
                               : (uint32_t)(2 * FP_EQ_BITS + r->eq_e);
    return span(k, rel, 1u);
}

static cq_bit *flag_bit(const cq_fp_class_block *k)
{
    const fp_row *r = row_of(k->cls);

    return span(k, (uint32_t)(2 * FP_EQ_BITS + r->eq_e + r->eq_f), 1u);
}

const cq_bit *cq_fp_class_flag(const cq_fp_class_block *k)
{
    check_region(k);
    return flag_bit(k);
}

/* The wire holding one side's own predicate: the `not1` output where the
 * source spells `==`, and the `eq` block's raw `a != b` wire where it spells
 * `!=`. READ THROUGH cq_eq_flag, never by the rule — it is `orr[W-2]` in
 * general and `diff[0]` at W == 1, and a consumer spelling the rule inline
 * reads `orr[-1]` at the bottom of the ladder (cmp.h). */
static const cq_bit *eq_raw(const cq_fp_class_block *k, int side)
{
    cq_eq_block e;

    eq_layout(k, side, &e);
    return cq_eq_flag(&e);
}

static int side_is_eq(const cq_fp_class_block *k, int side)
{
    const fp_row *r = row_of(k->cls);

    return side == 0 ? r->eq_e : r->eq_f;
}

static const cq_bit *side_flag(const cq_fp_class_block *k, int side)
{
    return side_is_eq(k, side) ? not1_bit(k, side) : eq_raw(k, side);
}

void cq_fp_class_step(cq_ctx *ctx, const cq_fp_class_block *k, int u)
{
    int C = cq_eq_steps(CQ_FP64_W);
    cq_bit view[CQ_FP64_W], konst[CQ_FP64_W];
    cq_eq_block e;

    if (u < 0 || u >= cq_fp_class_steps(k->cls))
        cq_kernel_die("fpclass: step index outside [0, cq_fp_class_steps(cls))");
    check_region(k);

    for (int side = 0; side <= 1; side++) {
        if (u < C) {
            eq_layout  (k, side, &e);
            eq_operands(k, side, view, konst, &e);
            cq_eq_step(ctx, &e, u);
            return;
        }
        u -= C;

        if (side_is_eq(k, side)) {
            if (u < FP_NOT1_STEPS) {
                /* arith.jl:476, in order: CNOT(w[1], r[1]) then NOT(r[1]).
                 * The INPUT is the eq block's RAW wire — `a != b` — and the
                 * pair of gates is what turns it into `a == b`. */
                if (u == 0) cq_emit_cx(ctx, eq_raw(k, side), not1_bit(k, side));
                else        cq_emit_x (ctx, not1_bit(k, side));
                return;
            }
            u -= FP_NOT1_STEPS;
        }
    }

    /* `&` — arith.jl:270's one Toffoli, at one lane, into the flag. */
    cq_emit_ccx(ctx, side_flag(k, 0), side_flag(k, 1), flag_bit(k));
}

int cq_fp_class_eval(uint64_t a, cq_fp_class cls)
{
    const fp_row *r = row_of(cls);
    uint64_t ea = (a >> CQ_FP64_EXP_LO) & CQ_FP64_EXP_ALL;
    uint64_t fa = a & CQ_FP64_FRAC_MASK;

    return (r->eq_e ? ea == r->ce : ea != r->ce)
        && (r->eq_f ? fa == r->cf : fa != r->cf);
}

/* --- The four kernels: Bennett-in-the-small over the block. --------------- */

typedef struct {
    cq_fp_class_block k;
    cq_bit           *dst;
} fp_env;

static void class_compute(cq_ctx *ctx, void *env, int s)
{
    cq_fp_class_step(ctx, &((const fp_env *)env)->k, s);
}

/* The "^=" of Rule 7's contract, and the only place `dst` is written. One CX
 * and no X: the flag already holds the predicate rather than its negation,
 * because each `==` occurrence's polarity was settled inside the block by
 * upstream's own lower_not1!. */
static void class_copyout(cq_ctx *ctx, void *env, int s)
{
    const fp_env *e = (const fp_env *)env;

    (void)s;
    cq_emit_cx(ctx, cq_fp_class_flag(&e->k), &e->dst[0]);
}

static void fp_class_kernel(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                            cq_fp_class cls)
{
    cq_scratch scr;
    fp_env e;
    const cq_bit *src[1] = { a };
    int w[1] = { CQ_FP64_W };

    /* `dst` is ONE bit and `a` is 64, so the arity-2 wrapper would size every
     * range with one width; the D7b leg is vacuous at arity 1 (kernel.h). */
    cq_kernel_check_n(dst, 1, src, w, 1);

    /* RISK R9, AND IT IS NOT AN OPTIMISATION (plan §0.2 consequence 2).
     * Pre-materialisation is unconditional, so without this a fully classical
     * `is_nan` would take 256 qubits for an operation with no quantum input at
     * all, and L5's "zero gates and zero qubits fully-classical" would be
     * false. The value comes from the SAME four rows the circuit runs. */
    if (cq_bits_all_const(a, CQ_FP64_W)) {
        if (cq_fp_class_eval(cq_fp_pack(a), cls)) cq_emit_x(ctx, &dst[0]);
        return;
    }

    cq_scratch_alloc(&scr, cq_fp_class_region(cls));
    e.k.a   = a;
    e.k.scr = &scr;
    e.k.off = 0u;
    e.k.cls = cls;
    e.dst   = dst;

    cq_sandwich(ctx, &scr, class_compute, cq_fp_class_steps(cls),
                class_copyout, 1, &e);
    cq_scratch_dispose(&scr);
}

void cq_kernel_fp_is_nan(cq_ctx *ctx, cq_bit *dst, const cq_bit *a)
{ fp_class_kernel(ctx, dst, a, CQ_FP_IS_NAN); }

void cq_kernel_fp_is_inf(cq_ctx *ctx, cq_bit *dst, const cq_bit *a)
{ fp_class_kernel(ctx, dst, a, CQ_FP_IS_INF); }

void cq_kernel_fp_is_zero(cq_ctx *ctx, cq_bit *dst, const cq_bit *a)
{ fp_class_kernel(ctx, dst, a, CQ_FP_IS_ZERO); }

void cq_kernel_fp_is_subnormal(cq_ctx *ctx, cq_bit *dst, const cq_bit *a)
{ fp_class_kernel(ctx, dst, a, CQ_FP_IS_SUBNORMAL); }
