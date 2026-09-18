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

/* ONE ENV, THREE BLOCKS, AND `layout` FILLS EXACTLY ONE OF THEM. All three
 * primitives are public types now (cmp.h, plan §0.4 / PRD-v2 §7.10) and all
 * three entry-point families DISPATCH through the matching step function, so
 * this struct is a union in everything but spelling — the two blocks a given
 * `prim` does not use are NULLed and never read.
 *
 * `u.a` / `u.b` are WHAT THE ult RECURRENCE COMPARES. That used to be the whole
 * of the sharing between ult and slt, because slt re-pointed THIS block at its
 * biased copies; since the slt export it does not, and `cq_slt_step` assembles
 * its own inner `cq_ult_block` at `af`/`bf` (arith.jl:471) so that no consumer
 * — this file included — can wire it at the operands instead. Nothing about K9
 * changed with either export: the same gates in the same order, and the
 * goldens did not move. */
typedef struct {
    cq_bit       *dst;
    const cq_bit *a, *b;
    cq_ult_block  u;                      /* ult: a, b, nb, carry, axnb, W */
    cq_eq_block   e;                      /* eq:  a, b, diff, orr, W       */
    cq_slt_block  s;                      /* slt: a, b, af, bf + ult's three */
    const cq_bit *raw;
    int           W;
} cmp_env;

/* `lower_eq!`, arith.jl:424-447. Phase A builds `diff = a ^ b` (:427, :428);
 * Phase B reduces it with the standard reversible OR — `t ^= x; t ^= y;
 * t ^= x&y` on a zero target is `x | y` — so `orr[W-2]` ends up holding
 * `a != b`. The trailing `CNOT(or[W-1], r); NOT(r)` (:445) is NOT part of the
 * compute half: it is the copy-out, which is where Bennett's negation folds
 * (K09.md §5 delta 2).
 *
 * THE W == 1 BRANCH IS IN THE INDEXING, NOT IN THE COUNT. Bennett special-
 * cases it because `or = allocate!(wa, W-1)` is empty and `or[1]` would read
 * out of bounds; here Phase B is simply empty and the raw flag is `diff[0]`,
 * which is also `a != b`. The closed form 5W-3 evaluates to 2 either way, so
 * there is no golden discontinuity (K09.md §5 delta 7).
 *
 * THIS IS A PUBLIC BLOCK AS OF PRD-v2 §7.10 and the two entry points below
 * DISPATCH THROUGH IT — one body, never a second transcription of `lower_eq!`.
 * See cmp.h for who consumes it and why the flag it leaves is `a != b`. */
int cq_eq_steps(int W)
{
    if (W <= 0) cq_kernel_die("eq: width is not positive");
    return 5 * W - 3;                      /* 2W diff + 3(W-1) OR-prefix */
}

int cq_eq_region(int W)
{
    if (W <= 0) cq_kernel_die("eq: region width is not positive");
    return 2 * W - 1;                      /* diff ++ orr, orr being W-1 */
}

void cq_eq_step(cq_ctx *ctx, const cq_eq_block *k, int u)
{
    int W = k->W;

    /* The width guard is cq_eq_steps'; its message is DISJOINT from this one
     * so a death test can say which spoke. This range check is the consumer's,
     * for cq_ult_step's reason: an off-by-one in a mapped run of indices lands
     * in the OR-prefix and emits a plausible wrong gate rather than failing. */
    if (u < 0 || u >= cq_eq_steps(W))
        cq_kernel_die("eq: step index outside [0, cq_eq_steps(W))");

    if (u < 2 * W) {
        int i = u / 2;

        cq_emit_cx(ctx, (u % 2 == 0) ? &k->a[i] : &k->b[i], &k->diff[i]);
        return;
    }

    /* m == 0 seeds from diff[0] (:436-438); every later m folds the previous
     * prefix in (:440-442). Bennett writes those as two blocks over
     * (or[k-1], or[k], diff[k+1]) and (diff[1], diff[2], or[1]); one control
     * differs, and only at m == 0. */
    int v = u - 2 * W, m = v / 3;
    const cq_bit *c1 = (m == 0) ? &k->diff[0] : &k->orr[m - 1];
    const cq_bit *c2 = &k->diff[m + 1];

    switch (v % 3) {
    case 0:  cq_emit_cx (ctx, c1,     &k->orr[m]); break;
    case 1:  cq_emit_cx (ctx, c2,     &k->orr[m]); break;
    default: cq_emit_ccx(ctx, c1, c2, &k->orr[m]); break;
    }
}

const cq_bit *cq_eq_flag(const cq_eq_block *k)
{
    return (k->W == 1) ? &k->diff[0] : &k->orr[k->W - 2];
}

static void eq_compute(cq_ctx *ctx, void *env, int s)
{
    cq_eq_step(ctx, &((const cmp_env *)env)->e, s);
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
int cq_ult_steps(int W)
{
    if (W <= 0) cq_kernel_die("ult: width is not positive");
    return 6 * W + 1;                      /* 2W complement + 1 seed + 4W */
}

int cq_ult_region(int W)
{
    if (W <= 0) cq_kernel_die("ult: region width is not positive");
    return 3 * W + 1;                      /* nb ++ carry(W+1) ++ axnb */
}

void cq_ult_step(cq_ctx *ctx, const cq_ult_block *k, int u)
{
    int W = k->W;

    /* The width guard is cq_ult_steps'; its message is DISJOINT from this one
     * so a death test can say which spoke. This range check is the consumer's:
     * M19 maps a contiguous run of its own indices onto [0, 6W+1), and an
     * off-by-one would land in the stage loop and emit a plausible wrong gate
     * rather than fail. */
    if (u < 0 || u >= cq_ult_steps(W))
        cq_kernel_die("ult: step index outside [0, cq_ult_steps(W))");

    if (u < 2 * W) {
        int i = u / 2;

        if (u % 2 == 0) cq_emit_cx(ctx, &k->b[i], &k->nb[i]);   /* copy */
        else            cq_emit_x (ctx,           &k->nb[i]);   /* flip */
        return;
    }

    if (u == 2 * W) { cq_emit_x(ctx, &k->carry[0]); return; }

    /* c_out = MAJ(a, ~b, c_in) = (a & ~b) ^ ((a ^ ~b) & c_in), with the two
     * halves as separate Toffolis and `axnb` holding a ^ ~b. THE ORDER IS
     * LOAD-BEARING: j == 3 reads `axnb[i]`, which j == 0 and j == 1 build. */
    int v = u - (2 * W + 1), i = v / 4;

    switch (v % 4) {
    case 0:  cq_emit_cx (ctx, &k->a[i],                &k->axnb[i]);       break;
    case 1:  cq_emit_cx (ctx, &k->nb[i],               &k->axnb[i]);       break;
    case 2:  cq_emit_ccx(ctx, &k->a[i],    &k->nb[i],  &k->carry[i + 1]);  break;
    default: cq_emit_ccx(ctx, &k->axnb[i], &k->carry[i], &k->carry[i + 1]); break;
    }
}

static void ult_compute(cq_ctx *ctx, void *env, int s)
{
    cq_ult_step(ctx, &((const cmp_env *)env)->u, s);
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
 * Bennett, and Rule 1 forbids substituting it on our own authority.
 *
 * THIS IS A PUBLIC BLOCK AS OF PRD-v2 §7.10 and the four signed entry points
 * DISPATCH THROUGH IT — one body, never a second transcription of `lower_slt!`,
 * which is what `cq_ult_step` did to `ult_compute` at Step 17. See cmp.h for
 * who consumes it, why the five spans are flat rather than an embedded
 * cq_ult_block, and why the flag it leaves is `a >=s b`. */
int cq_slt_steps(int W)
{
    if (W <= 0) cq_kernel_die("slt: width is not positive");
    return 2 * W + 2 + cq_ult_steps(W);    /* 2W copy + 2 bias + the inner ult */
}

int cq_slt_region(int W)
{
    if (W <= 0) cq_kernel_die("slt: region width is not positive");
    return 2 * W + cq_ult_region(W);       /* af ++ bf ++ the inner ult */
}

void cq_slt_step(cq_ctx *ctx, const cq_slt_block *k, int u)
{
    int W = k->W;

    /* The width guard is cq_slt_steps'; its message is DISJOINT from this one
     * so a death test can say which spoke. This range check is the consumer's,
     * for cq_ult_step's reason — and here a deleted one is answered by
     * cq_ult_step's own guard one layer DOWN, which is why the death cases pin
     * that message as forbidden rather than resting on the exit code. */
    if (u < 0 || u >= cq_slt_steps(W))
        cq_kernel_die("slt: step index outside [0, cq_slt_steps(W))");

    if (u < 2 * W) {
        int i = u / 2;

        if (u % 2 == 0) cq_emit_cx(ctx, &k->a[i], &k->af[i]);
        else            cq_emit_cx(ctx, &k->b[i], &k->bf[i]);
        return;
    }

    if (u == 2 * W)     { cq_emit_x(ctx, &k->af[W - 1]); return; }
    if (u == 2 * W + 1) { cq_emit_x(ctx, &k->bf[W - 1]); return; }

    /* `lower_ult!(g, wa, af, bf, W)` (arith.jl:471) — over the BIASED COPIES,
     * never over `a`/`b`. Assembled here rather than carried in the struct so a
     * consumer cannot wire it the other way; that mutant is `slt` silently
     * meaning `ult`, with the same gate tuple and a wrong answer only on a
     * negative operand (cmp.h). */
    {
        cq_ult_block in = { k->af, k->bf, k->nb, k->carry, k->axnb, W };

        cq_ult_step(ctx, &in, u - (2 * W + 2));
    }
}

const cq_bit *cq_slt_flag(const cq_slt_block *k)
{
    return &k->carry[k->W];
}

static void slt_compute(cq_ctx *ctx, void *env, int s)
{
    cq_slt_step(ctx, &((const cmp_env *)env)->s, s);
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
    if (prim == PRIM_EQ)  return cq_eq_steps(W);         /* 5W - 3          */
    if (prim == PRIM_ULT) return cq_ult_steps(W);        /* 6W + 1          */
    return cq_slt_steps(W);                              /* 8W + 3          */
}

/* ONE CONTIGUOUS REGION, carved into named sub-arrays. emit.c's I6(a) check is
 * a pointer RANGE test over cq_bit addresses, so a kernel that allocated two
 * regions would put half its targets outside the extent. `raw` is where the
 * primitive leaves its flag; it is a scratch bit and is read as a control by
 * the copy-out, which is legal — the disarmed extent constrains targets. */
static void layout(cmp_env *e, cq_scratch *scr, int prim)
{
    uint32_t W = (uint32_t)e->W;

    e->u.nb = e->u.carry = e->u.axnb = NULL;
    e->u.a  = e->u.b = NULL;
    e->u.W  = e->W;
    e->e.diff = e->e.orr = NULL;
    e->e.a  = e->e.b = NULL;
    e->e.W  = e->W;
    e->s.af = e->s.bf = e->s.nb = e->s.carry = e->s.axnb = NULL;
    e->s.a  = e->s.b = NULL;
    e->s.W  = e->W;

    if (prim == PRIM_EQ) {
        cq_scratch_alloc(scr, 2u * W - 1u);
        e->e.diff = cq_scratch_span(scr, 0u, W);
        e->e.orr  = cq_scratch_span(scr, W,  W - 1u);    /* empty at W == 1 */
        e->e.a    = e->a;
        e->e.b    = e->b;
        e->raw    = cq_eq_flag(&e->e);   /* `a != b`; the W == 1 branch is here */
        return;
    }

    if (prim == PRIM_ULT) {
        cq_scratch_alloc(scr, 3u * W + 1u);
        e->u.nb    = cq_scratch_span(scr, 0u,          W);
        e->u.carry = cq_scratch_span(scr, W,           W + 1u);
        e->u.axnb  = cq_scratch_span(scr, 2u * W + 1u, W);
        e->u.a = e->a;
        e->u.b = e->b;
        e->raw = &e->u.carry[W];
        return;
    }

    /* The five spans in cq_slt_block's own declaration order. `cq_slt_step`
     * wires the inner comparator at `af`/`bf` itself (cmp.h), so nothing here
     * names a cq_ult_block at all. */
    cq_scratch_alloc(scr, 5u * W + 1u);
    e->s.af    = cq_scratch_span(scr, 0u,          W);
    e->s.bf    = cq_scratch_span(scr, W,           W);
    e->s.nb    = cq_scratch_span(scr, 2u * W,      W);
    e->s.carry = cq_scratch_span(scr, 3u * W,      W + 1u);
    e->s.axnb  = cq_scratch_span(scr, 4u * W + 1u, W);
    e->s.a = e->a;
    e->s.b = e->b;
    e->raw = cq_slt_flag(&e->s);
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

    if (cq_bits_all_const(a, W) && cq_bits_all_const(b, W)) {
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
