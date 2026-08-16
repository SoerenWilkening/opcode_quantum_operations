/* src/kernels/mul.c — M18, Step 16. K11 `mul`, shift-add over K8's Cuccaro.
 *
 * Read docs/constructions/K11.md and this module's header before changing
 * anything. The skeleton is `lower_mul_wide!` (multiplier.jl:13-33) with
 * `result_width = W`; the accumulator is K8 (M15), substituted for upstream's
 * ripple `lower_add!` as a deliberate delta (K11.md §5 delta 4).
 *
 * ONE GATE PER STEP, AND IT IS FORCED (bd ckd.14a, bd rhp). The driver replays
 * the compute half by re-calling `compute(env, s)` at descending indices, so a
 * step must be an involution. The dangerous reading here is not the ripple
 * five-gate block K06 warns about — it is K11.md §2b's ONE-LINE accumulate.
 * Made into one step it would leave `accum + 2·pp[j]` on the way back, with
 * `dst` already copied out and L1 green. `cq_addacc_step` exists for exactly
 * this and is what this file calls.
 *
 * I6(a) HOLDS GATE BY GATE, and the argument has two halves because K8 has only
 * one of them. Phase P targets `pp[j][k+j]` and reaches `a` and `b` solely
 * through `cq_emit_ccx`'s `const cq_bit *` controls, so those two cannot be
 * materialised BY CONSTRUCTION. K8 is the exception in the catalogue — it
 * targets its own addend (adder.jl:100), which is why `cq_addacc_block.b` is
 * non-const — but here that addend is `pp[j]`, a bit of this kernel's own
 * scratch region, so every Cuccaro target is scratch too. What stops a
 * mis-wiring is not the type system and not the Debug extent check (compiled
 * out in Release, and inert at W=2 where no Cuccaro gate targets the addend at
 * all): it is `cq_addacc_check`, which `cq_addacc_step` runs at `u == 0` of
 * every accumulate, in both configurations.
 *
 * ONE REGION, CARVED. `accum[W]`, then `pp[j][W]` for j in [0,W), then `x[j]`
 * for j in [0,W): W² + 2W bits from ONE `cq_scratch_alloc`, because emit.c's
 * I6(a) check is a pointer RANGE test and a kernel that allocated two regions
 * would have half its targets outside the armed extent.
 *
 * WHAT THIS COSTS, SO NOBODY IS SURPRISED AT i128: W² + 2W scratch qubits —
 * 4224 at i64 and 16,640 at i128 — every one pre-materialised by I6(b) and
 * returned by the reverse half. That is already the cheap variant: ripple would
 * have wanted 3W² + W, i.e. 49,280 at i128. Under D2 the pool ceiling is
 * unbounded by default and is set to `qec_n_logical` when the QEC sink is
 * active, where an i128 multiply will fail loud — which is the intended
 * behaviour and not something to soften here (K11.md §5 note 8). The one
 * cheaper form that is known — recycling `pp` by uncomputing it after each
 * accumulate, W² + 2W -> 2W + 1 qubits for W(W+1)/2 more Toffolis per half — is
 * a further re-derivation and is deliberately NOT v1.
 */

#include "kernels/mul.h"

#include "emit.h"
#include "kernels/addacc.h"
#include "kernels/bitwise.h"
#include "kernels/kernel.h"
#include "sandwich.h"
#include "scratch.h"

typedef struct {
    cq_bit       *dst;
    const cq_bit *a, *b;
    cq_scratch   *scr;
    int           W;
} mul_env;

/* `accum` is ONE register updated in place across all W iterations — that is
 * the whole point of the Cuccaro substitution, and it is what collapses ripple's
 * 3W² + W to W² + 2W. `pp[j]` cannot be recycled for j+1: Cuccaro RESTORES its
 * addend (adder.jl:50-51), so `pp[j]` still holds the partial product after the
 * accumulate rather than being cleared. */
static cq_bit *accum_of(const mul_env *e)
{
    return cq_scratch_span(e->scr, 0u, (uint32_t)e->W);
}

static cq_bit *pp_of(const mul_env *e, int j)
{
    return cq_scratch_span(e->scr, (uint32_t)(e->W * (1 + j)), (uint32_t)e->W);
}

/* Cuccaro's ancilla, ONE PER CALL AND OWNED HERE. Bennett allocates it inside
 * the adder (`X = allocate!(wa, 1)`, adder.jl:69); we cannot, because an
 * allocation in the middle of a compute half is a scratch bit step 1 did not
 * pre-materialise — and `x` is precisely the wire K11.md §2b's R8 trace indicts,
 * read as a control at adder.jl:104 while still BIT_ZERO and materialised as a
 * target at :142. A shared single `x` across all W calls would work and would
 * save W qubits against a W²-sized total (0.06% at i32); it is not taken,
 * because it makes the scratch extent non-uniform for no measurable gain
 * (K11.md §4). */
static cq_bit *x_of(const mul_env *e, int j)
{
    return cq_scratch_span(e->scr, (uint32_t)(e->W * (1 + e->W) + j), 1u);
}

/* WHERE OUTER ITERATION `j` STARTS IN THE FLAT INDEX SPACE. Block j is (W − j)
 * phase-P Toffolis — the truncation at multiplier.jl:26 is what makes it shrink
 * — then 6W − 5 accumulate steps, so its length is 7W − 5 − j and
 *
 *     start(j) = Σ_{i<j} (7W − 5 − i) = j(7W − 5) − j(j−1)/2.
 *
 * Strictly increasing on [0, W] because every length is positive there, which
 * is what makes the binary search below well defined; and start(W) is both
 * W(W+1)/2 + W(6W−5) and (13W² − 9W)/2, which is the identity cq_mul_steps
 * rests on. A LINEAR SCAN OVER j WOULD ALSO WORK and is what M12's barrel does,
 * but the barrel has L ≤ 7 stages where this has W ≤ 128 blocks over ~10⁵
 * steps: O(W) per step is O(W³) per half. tests/test_kernel_mul.c checks this
 * decomposition against a brute-force linear scan rather than trusting it. */
static int block_start(int W, int j)
{
    return j * (7 * W - 5) - j * (j - 1) / 2;
}

static int outer_of(int W, int s)
{
    int lo = 0, hi = W - 1;

    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;

        if (block_start(W, mid) <= s) lo = mid;
        else                          hi = mid - 1;
    }
    return lo;
}

int cq_mul_steps(int W)
{
    if (W <= 0) cq_kernel_die("mul: width is not positive");

    /* 0, NOT (13 − 9)/2 = 2. At W = 1 there is no sandwich and no scratch: the
     * kernel is K2 with one Toffoli (see the header on why the closed form is
     * wrong there in a way that looks right). Returning the formula would hand
     * a caller a compute-half length for a compute half that never runs. */
    return W == 1 ? 0 : block_start(W, W);
}

/* NO OUT-OF-RANGE GUARD HERE, DELIBERATELY. `cq_sandwich` only ever calls this
 * with s in [0, n_compute), and for any s past the end `u` runs off the last
 * block and `cq_addacc_step`'s own index guard aborts one layer down. A second
 * copy would be a guard no single case can turn red, which is the shape
 * CLAUDE.md says to ask about before adding one. */
static void compute(cq_ctx *ctx, void *env, int s)
{
    const mul_env *e = (const mul_env *)env;
    int W = e->W, j = outer_of(W, s), u = s - block_start(W, j);
    cq_addacc_block k;

    /* Phase P — the partial product for multiplier bit b[j], weight-shifted by
     * j. Upstream is `ToffoliGate(a[k], b[i], pp[dest])` with `dest = k + shift`
     * (multiplier.jl:24, 0-indexed here), and `dest >= W` breaks, so this block
     * is W − j gates and `pp[j][0..j-1]` is never written. Those low lanes hold
     * the VALUE zero for the whole compute half and are nonetheless CQ_BIT_Q
     * qubits under I6(b), so every Cuccaro gate touching them is emitted
     * physically — a real, deliberate cost and the price of a W-only golden. */
    if (u < W - j) {
        cq_emit_ccx(ctx, &e->a[u], &e->b[j], &pp_of(e, j)[u + j]);
        return;
    }

    /* Phase A — the accumulate. THE ARGUMENT ORDER IS THE ONE THING TO GET
     * RIGHT: Bennett's call is `lower_add_cuccaro!(a = pp[j], b = accum)`, his
     * `b` is overwritten with the sum (adder.jl:62) and his `a` is the restored
     * addend — and libcqops inverts the letters, so our `acc` is his `b` and our
     * `b` is his `a` (addacc.h). Assigning positionally instead puts the product
     * in `pp[j]` and never accumulates: `dst` comes out 0, which L1 sees, but
     * the module still looks like the source. */
    k.acc = accum_of(e);
    k.b   = pp_of(e, j);
    k.x   = x_of(e, j);
    k.W   = W;
    cq_addacc_step(ctx, &k, u - (W - j));
}

/* The "^=" of Rule 7's contract, and the only place `dst` is written on the
 * sandwich path — which is why the driver runs it with the I6 extent DISARMED. */
static void copyout(cq_ctx *ctx, void *env, int i)
{
    const mul_env *e = (const mul_env *)env;

    cq_emit_cx(ctx, &accum_of(e)[i], &e->dst[i]);
}

static int all_const(const cq_bit *v, int W)
{
    for (int i = 0; i < W; i++)
        if (!cq_bit_is_const(v[i])) return 0;
    return 1;
}

/* Risk R9's short-circuit: `dst ^= (a·b) mod 2^W` with every operand bit a
 * constant. Zero gates and zero qubits when `dst` is classical too, which is
 * L5; a real X per set bit of the product when `dst` already sits on qubits,
 * which is what makes this a correct implementation of `^=` rather than merely
 * a cheap one.
 *
 * COLUMN-WISE, WITH NO ARRAY AND NO PACKED SCALAR. I5 forbids a `uint64_t
 * product` here — `mul` ships at i128 (opcode_table.yaml:186), where one would
 * silently truncate — and a W-bit temporary array would need a VLA or a malloc
 * for a kernel that is otherwise allocation-free. Column i of the schoolbook
 * product is Σ_{k≤i} a[k]·b[i−k] plus the carry out of column i−1; the low bit
 * is the product bit and the rest carries. `col` is bounded: carry_i ≤ i by
 * induction, so col ≤ i + (i+1) ≤ 2W − 1, i.e. 255 at i128.
 *
 * It is also a DIFFERENT ALGORITHM from the circuit above, which matters: the
 * sandwich path is bit-serial shift-add with a Cuccaro carry chain, this is
 * column accumulation, and a fold that shared the kernel's recurrence would
 * share its mistakes. */
static void fold_constant(cq_ctx *ctx, cq_bit *dst, const cq_bit *a,
                          const cq_bit *b, int W)
{
    int carry = 0;

    for (int i = 0; i < W; i++) {
        int col = carry;

        for (int k = 0; k <= i; k++)
            col += cq_bit_value(a[k]) & cq_bit_value(b[i - k]);

        if (col & 1) cq_emit_x(ctx, &dst[i]);
        carry = col >> 1;
    }
}

void cq_kernel_mul(cq_ctx *ctx, cq_bit *dst,
                   const cq_bit *a, const cq_bit *b, int W)
{
    cq_scratch scr;
    mul_env e;

    cq_kernel_check_dst(dst, a, b, W);

    /* i1: `a·b mod 2` is `a ∧ b`, so this is K2 with one Toffoli — a delegation
     * to a catalogue entry, not a new construction. It comes BEFORE the R9 fold
     * because it subsumes it: K2 folds an all-classical operand to nothing on
     * its own. See the header for why the uniform path is not merely wasteful
     * here but out of domain (adder.jl:66). */
    if (W == 1) { cq_kernel_and(ctx, dst, a, b, 1); return; }

    if (all_const(a, W) && all_const(b, W)) {
        fold_constant(ctx, dst, a, b, W);
        return;
    }

    cq_scratch_alloc(&scr, (uint32_t)(W * W + 2 * W));

    e.dst = dst;
    e.a   = a;
    e.b   = b;
    e.scr = &scr;
    e.W   = W;

    cq_sandwich(ctx, &scr, compute, cq_mul_steps(W), copyout, W, &e);
    cq_scratch_dispose(&scr);
}
