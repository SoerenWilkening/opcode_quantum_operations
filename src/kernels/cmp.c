/* src/kernels/cmp.c — M16, Step 13. K9: `icmp`, all ten LLVM predicates.
 *
 * Read docs/constructions/K09.md before changing anything here. The three
 * primitives are `lower_eq!` (arith.jl:424-447), `lower_ult!` (:449-463) and
 * `lower_slt!` (:465-472); the ten-row dispatch below is `lower_icmp!`
 * (:409-418) and nothing else. See kernels/cmp.h for the interface, the
 * one-bit `dst`, and why nothing of M14 is reused.
 *
 * ONE GATE PER STEP, AND IT IS FORCED (bd ckd.14a, sandwich.h). cq_sandwich
 * runs the reverse pass by re-calling compute(env, s) with the SAME index, so
 * a step undoes itself only if it is an involution. Taking a step to be a loop
 * BODY is the natural reading and it is wrong here for the same reason it is
 * wrong in M14: `lower_ult!`'s body writes `axnb[i]` with two CNOTs and then
 * reads it as a CONTROL in the following Toffoli, so re-running the block from
 * its post-state does not undo it. Index reversal IS gate reversal only while
 * a step is one gate.
 *
 * I6(a) IS SATISFIED WITH NOTHING LEFT TO CHECK. Every target below is a bit
 * of the scratch region — `diff`, `orr`, `af`, `bf`, `nb`, `carry`, `axnb` —
 * and `a` / `b` reach the emitter only through `const cq_bit *` control
 * parameters, so a source cannot be materialised here by construction. `dst`
 * is touched exclusively in `copyout`, which the driver runs with the extent
 * deliberately disarmed because `dst` is outside scratch by definition.
 *
 * I6(b) IS WHAT MAKES THE COUNTS STABLE, NOT WHAT MAKES K9 CORRECT, and the
 * distinction is recorded because K09.md's first issue got it backwards.
 * `X(carry[0])` used to fold against a BIT_ZERO scratch bit — 0 gates, leaving
 * a classical ONE that then degraded the next Toffoli to a CX. Pre-
 * materialisation stops both, which is the whole +2 per sandwiched compare.
 * But K9 was never an instance of risk R8: `carry[0]` folded to a ONE, which
 * still EMITS when read as a control, and it is never targeted again, so the
 * two halves mirrored even before the decision. The kernels that genuinely
 * need I6(b) are the ones that read scratch before writing it.
 *
 * THE ALL-CLASSICAL SHORT-CIRCUIT IS MANDATORY, NOT AN OPTIMISATION (risk R9,
 * plan §0.2 consequence 2). Pre-materialisation is unconditional, so without
 * the check in `cmp` below a fully classical compare would take 2W-1, 3W+1 or
 * 5W+1 qubits for an operation with no quantum input at all, and L5's "zero
 * gates and zero qubits fully-classical" would be false.
 */

#include "kernels/cmp.h"

#include "emit.h"
#include "kernels/kernel.h"
#include "sandwich.h"
#include "scratch.h"

/* The three constructions. Every predicate is one of these plus at most an
 * operand swap and a trailing negation, exactly as `lower_icmp!` has it. */
enum { PRIM_EQ, PRIM_ULT, PRIM_SLT };

/* `ua` / `ub` are WHAT THE ult RECURRENCE COMPARES, and that is the whole of
 * the sharing between ult and slt: `lower_slt!` biases `a` and `b` into `af`
 * and `bf` and then calls `lower_ult!` on those (arith.jl:471), so slt's
 * Phase C is the ult step function verbatim with its operands re-pointed at
 * scratch. `a` and `b` stay separate because slt's own Phase A needs them. */
typedef struct {
    cq_bit       *dst;
    const cq_bit *a, *b;
    const cq_bit *ua, *ub;
    cq_bit       *diff, *orr;             /* eq  */
    cq_bit       *af, *bf;                /* slt */
    cq_bit       *nb, *carry, *axnb;      /* ult */
    const cq_bit *raw;
    int           W;
} cmp_env;

/* `lower_eq!`, arith.jl:424-447. Phase A builds `diff = a ^ b`; Phase B
 * reduces it with the standard reversible OR — `t ^= x; t ^= y; t ^= x&y` on a
 * zero target is `x | y` — so `orr[W-2]` ends up holding `a != b`.
 *
 * THE W == 1 BRANCH IS IN THE INDEXING, NOT IN THE COUNT. Bennett special-
 * cases it because `or = allocate!(wa, W-1)` is empty and `or[1]` would read
 * out of bounds; here Phase B is simply empty and the raw flag is `diff[0]`,
 * which is also `a != b`. The closed form 5W-3 evaluates to 2 either way, so
 * there is no golden discontinuity (K09.md §5 delta 7). */
static void eq_compute(cq_ctx *ctx, void *env, int s)
{
    const cmp_env *e = (const cmp_env *)env;
    int W = e->W;

    if (s < 2 * W) {
        int i = s / 2;

        cq_emit_cx(ctx, (s % 2 == 0) ? &e->a[i] : &e->b[i], &e->diff[i]);
        return;
    }

    /* k == 0 seeds from diff[0]; every later k folds the previous prefix in.
     * Bennett writes those as two loops over (or[k-1], or[k], diff[k+1]) and
     * (diff[1], diff[2], or[1]); one control differs, and only at k == 0. */
    int u = s - 2 * W, k = u / 3;
    const cq_bit *c1 = (k == 0) ? &e->diff[0] : &e->orr[k - 1];
    const cq_bit *c2 = &e->diff[k + 1];

    switch (u % 3) {
    case 0:  cq_emit_cx (ctx, c1,     &e->orr[k]); break;
    case 1:  cq_emit_cx (ctx, c2,     &e->orr[k]); break;
    default: cq_emit_ccx(ctx, c1, c2, &e->orr[k]); break;
    }
}

/* `lower_ult!`, arith.jl:449-463. Computes a + ~b + 1 = a - b and keeps the
 * carry-out, which is 1 exactly when a >=u b.
 *
 * THIS IS NOT K7's RIPPLE AND MUST NOT BE "SHARED" WITH IT. The two are
 * genuinely different upstream functions: `lower_add!` (adder.jl:8-16) ends
 * each stage with `CNOT(carry[i], result[i])` to produce the SUM bit, which a
 * comparator has no use for, and it writes the partial sum into the same wire
 * the majority Toffoli reads. `lower_ult!` keeps that intermediate in its own
 * `axnb` array and emits four gates per stage rather than five. Porting it is
 * Rule 1 applied literally (bd -4tt).
 *
 * Step 2W is the `+1` of two's complement. Under I6(b) `carry[0]` is already a
 * qubit, so this X IS EMITTED and the j == 3 Toffoli at i == 0 stays a
 * Toffoli; that one step is the entire 12W+2 -> 12W+4 re-issue (K09.md §3). */
static void ult_compute(cq_ctx *ctx, void *env, int s)
{
    const cmp_env *e = (const cmp_env *)env;
    int W = e->W;

    if (s < 2 * W) {
        int i = s / 2;

        if (s % 2 == 0) cq_emit_cx(ctx, &e->ub[i], &e->nb[i]);   /* copy */
        else            cq_emit_x (ctx,            &e->nb[i]);   /* flip */
        return;
    }

    if (s == 2 * W) { cq_emit_x(ctx, &e->carry[0]); return; }

    /* c_out = MAJ(a, ~b, c_in) = (a & ~b) ^ ((a ^ ~b) & c_in), with the two
     * halves as separate Toffolis and `axnb` holding a ^ ~b. THE ORDER IS
     * LOAD-BEARING: j == 3 reads `axnb[i]`, which j == 0 and j == 1 build. */
    int u = s - (2 * W + 1), i = u / 4;

    switch (u % 4) {
    case 0:  cq_emit_cx (ctx, &e->ua[i],               &e->axnb[i]);       break;
    case 1:  cq_emit_cx (ctx, &e->nb[i],               &e->axnb[i]);       break;
    case 2:  cq_emit_ccx(ctx, &e->ua[i],  &e->nb[i],   &e->carry[i + 1]);  break;
    default: cq_emit_ccx(ctx, &e->axnb[i], &e->carry[i], &e->carry[i + 1]); break;
    }
}

/* `lower_slt!`, arith.jl:465-472: copy both operands, flip the MSB of each —
 * the standard signed-to-unsigned bias by 2^(W-1), since
 * a <s b <=> (a ^ 2^(W-1)) <u (b ^ 2^(W-1)) — then run ult on the copies.
 *
 * The copies are NOT avoidable by inverting a control: Rule 4 admits only
 * X/CX/CCX, so there is no negative-control gate to fold the bias into, and
 * flipping `a`'s MSB in place would mutate a source (Rule 7) and would not
 * even compile against cq_emit_*'s const controls. K09.md §5 delta 11 records
 * the identity that WOULD remove this phase — a <s b = (a <u b) ^ a_msb ^
 * b_msb, worth 4W+2 gates and 2W qubits — as NOT ADOPTED: it is not in
 * Bennett, and Rule 1 forbids substituting it on our own authority. */
static void slt_compute(cq_ctx *ctx, void *env, int s)
{
    const cmp_env *e = (const cmp_env *)env;
    int W = e->W;

    if (s < 2 * W) {
        int i = s / 2;

        if (s % 2 == 0) cq_emit_cx(ctx, &e->a[i], &e->af[i]);
        else            cq_emit_cx(ctx, &e->b[i], &e->bf[i]);
        return;
    }

    if (s == 2 * W)     { cq_emit_x(ctx, &e->af[W - 1]); return; }
    if (s == 2 * W + 1) { cq_emit_x(ctx, &e->bf[W - 1]); return; }

    ult_compute(ctx, env, s - (2 * W + 2));
}

/* The "^=" of Rule 7's contract, and the only place `dst` is written on the
 * sandwich path. Step 0 is Bennett's `CNOT(flag, r[1])`; step 1 is its `NOT`,
 * and it runs only for the five predicates whose raw scratch flag is the
 * NEGATION of the answer. The raw flags are `a != b`, `a >=u b` and
 * `a >=s b`, so the other five copy it out unchanged and save an X — Bennett's
 * double negation folding into the copy-out (K09.md §5 delta 2). */
static void copyout(cq_ctx *ctx, void *env, int s)
{
    const cmp_env *e = (const cmp_env *)env;

    if (s == 0) cq_emit_cx(ctx, e->raw, &e->dst[0]);
    else        cq_emit_x (ctx,         &e->dst[0]);
}

static int n_compute_of(int prim, int W)
{
    if (prim == PRIM_EQ)  return 5 * W - 3;      /* 2W + 3(W-1) */
    if (prim == PRIM_ULT) return 6 * W + 1;      /* 2W + 1 + 4W */
    return 8 * W + 3;                            /* 2W + 2 + (6W+1) */
}

/* ONE CONTIGUOUS REGION, carved into named sub-arrays. emit.c's I6(a) check is
 * a pointer RANGE test over cq_bit addresses, so a kernel that allocated two
 * regions would put half its targets outside the extent. `raw` is where the
 * primitive leaves its flag; it is a scratch bit and is read as a control by
 * the copy-out, which is legal — the disarmed extent constrains targets. */
static void layout(cmp_env *e, cq_scratch *scr, int prim)
{
    uint32_t W = (uint32_t)e->W;

    e->diff = e->orr = e->af = e->bf = NULL;
    e->nb = e->carry = e->axnb = NULL;
    e->ua = e->ub = NULL;

    if (prim == PRIM_EQ) {
        cq_scratch_alloc(scr, 2u * W - 1u);
        e->diff = cq_scratch_span(scr, 0u, W);
        e->orr  = cq_scratch_span(scr, W,  W - 1u);      /* empty at W == 1 */
        e->raw  = (W == 1u) ? &e->diff[0] : &e->orr[W - 2u];
        return;
    }

    if (prim == PRIM_ULT) {
        cq_scratch_alloc(scr, 3u * W + 1u);
        e->nb    = cq_scratch_span(scr, 0u,          W);
        e->carry = cq_scratch_span(scr, W,           W + 1u);
        e->axnb  = cq_scratch_span(scr, 2u * W + 1u, W);
        e->ua = e->a;
        e->ub = e->b;
        e->raw = &e->carry[W];
        return;
    }

    cq_scratch_alloc(scr, 5u * W + 1u);
    e->af    = cq_scratch_span(scr, 0u,          W);
    e->bf    = cq_scratch_span(scr, W,           W);
    e->nb    = cq_scratch_span(scr, 2u * W,      W);
    e->carry = cq_scratch_span(scr, 3u * W,      W + 1u);
    e->axnb  = cq_scratch_span(scr, 4u * W + 1u, W);
    e->ua = e->af;                       /* ult runs over the biased copies */
    e->ub = e->bf;
    e->raw = &e->carry[W];
}

static int all_const(const cq_bit *v, int W)
{
    for (int i = 0; i < W; i++)
        if (!cq_bit_is_const(v[i])) return 0;
    return 1;
}

/* The raw flag the compute half would have produced, in plain C: `a != b` for
 * eq, `a >=u b` for ult, `a >=s b` for slt.
 *
 * BIT-SERIAL FROM THE TOP DOWN, AND THAT IS I5 RATHER THAN TASTE. A packed
 * uint64_t here would cap the kernel at 64 bits, and `icmp` ships at i80
 * (opcode_table.yaml:222) — where the width-generic loop costs nothing extra.
 * The signed case is the same scan with the two sign bits read inverted, which
 * is the same bias the circuit applies, evaluated rather than emitted. */
static int const_raw(const cq_bit *a, const cq_bit *b, int W, int prim)
{
    if (prim == PRIM_EQ) {
        for (int i = 0; i < W; i++)
            if (cq_bit_value(a[i]) != cq_bit_value(b[i])) return 1;
        return 0;
    }

    for (int i = W - 1; i >= 0; i--) {
        int x = cq_bit_value(a[i]), y = cq_bit_value(b[i]);

        if (prim == PRIM_SLT && i == W - 1) { x ^= 1; y ^= 1; }
        if (x != y) return x;              /* the top differing bit decides */
    }
    return 1;                              /* equal, and equal is >= */
}

enum { NO_SWAP = 0, SWAP = 1 };
enum { KEEP    = 0, INVERT = 1 };

/* `lower_icmp!`'s body, once. `swap` is its operand reversal and `invert` is
 * its `lower_not1!`; between them the ten predicates are three constructions. */
static void cmp(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, const cq_bit *b,
                int W, int prim, int swap, int invert)
{
    cq_scratch scr;
    cmp_env e;
    const cq_bit *src[2];
    int w[2];

    /* dst is ONE bit — K9 is the only kernel where |dst| != W — so the widths
     * are not uniform and cq_kernel_check_dst's arity-2 form would size every
     * range with the same W. Checked before the swap; overlap is symmetric. */
    src[0] = a; src[1] = b;
    w[0] = W;   w[1] = W;
    cq_kernel_check_n(dst, 1, src, w, 2);

    if (swap) { const cq_bit *t = a; a = b; b = t; }

    if (all_const(a, W) && all_const(b, W)) {
        if (const_raw(a, b, W, prim) ^ invert) cq_emit_x(ctx, &dst[0]);
        return;
    }

    e.dst = dst;
    e.a   = a;
    e.b   = b;
    e.W   = W;
    layout(&e, &scr, prim);

    cq_sandwich(ctx, &scr,
                (prim == PRIM_EQ)  ? eq_compute :
                (prim == PRIM_ULT) ? ult_compute : slt_compute,
                n_compute_of(prim, W), copyout, invert ? 2 : 1, &e);
    cq_scratch_dispose(&scr);
}

/* THE TEN ROWS OF `lower_icmp!` (arith.jl:409-418), TRANSCRIBED ONCE. Read
 * them against K09.md §1.1: four predicates swap their operands (`ugt`, `ule`,
 * `sgt`, `sle`) and five invert the raw flag (`eq`, `ult`, `ugt`, `slt`,
 * `sgt`), and the two sets are NOT the same set — that near-miss is what makes
 * this table worth reading twice. Anything outside the ten is an ArgumentError
 * upstream; here it is unrepresentable, because there is no predicate
 * parameter to get wrong. */
void cq_kernel_eq(cq_ctx *ctx, cq_bit *dst,
                  const cq_bit *a, const cq_bit *b, int W)
{ cmp(ctx, dst, a, b, W, PRIM_EQ, NO_SWAP, INVERT); }

void cq_kernel_ne(cq_ctx *ctx, cq_bit *dst,
                  const cq_bit *a, const cq_bit *b, int W)
{ cmp(ctx, dst, a, b, W, PRIM_EQ, NO_SWAP, KEEP); }

void cq_kernel_ult(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W)
{ cmp(ctx, dst, a, b, W, PRIM_ULT, NO_SWAP, INVERT); }

void cq_kernel_ugt(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W)
{ cmp(ctx, dst, a, b, W, PRIM_ULT, SWAP, INVERT); }

void cq_kernel_ule(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W)
{ cmp(ctx, dst, a, b, W, PRIM_ULT, SWAP, KEEP); }

void cq_kernel_uge(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W)
{ cmp(ctx, dst, a, b, W, PRIM_ULT, NO_SWAP, KEEP); }

void cq_kernel_slt(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W)
{ cmp(ctx, dst, a, b, W, PRIM_SLT, NO_SWAP, INVERT); }

void cq_kernel_sgt(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W)
{ cmp(ctx, dst, a, b, W, PRIM_SLT, SWAP, INVERT); }

void cq_kernel_sle(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W)
{ cmp(ctx, dst, a, b, W, PRIM_SLT, SWAP, KEEP); }

void cq_kernel_sge(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W)
{ cmp(ctx, dst, a, b, W, PRIM_SLT, NO_SWAP, KEEP); }
