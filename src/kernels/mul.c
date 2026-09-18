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
 * AND SINCE 2026-09-18 THE REGION MAY BE SOMEONE ELSE'S (PRD-v2 §5, §7.10).
 * The compute half is exported as `cq_mul_step` over a `cq_mul_block`, which
 * carries the caller's `cq_scratch *` and an `off` into it; `cq_kernel_mul`
 * binds one at `off = 0` over a region it allocates itself. Every accessor
 * below is relative to `off` and there is ONE body, so the kernel and a
 * consumer cannot drift apart — which is what keeps tests/goldens/mul.counts a
 * measurement of the same function M34's `soft_fmul` will call. The region is
 * still ONE extent for the I6(a) reason above; what changed is who owns it.
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
 *
 * PRD §15 D25 IS WHERE "FAIL LOUD" IS STATED AND TESTED (bd fxz, 2026-09-10),
 * and it corrects one number in the paragraph above: the ceiling a device needs
 * is W² + 3W, not W² + 2W. `dst`'s lanes are materialised by the COPYOUT, which
 * runs while the whole region is still live, so a fabric holding exactly the
 * region does not run this kernel. The refusal itself is cq_qubits_acquire's,
 * inside cq_sandwich's step 1 and BEFORE the first gate reaches the sink, and
 * it aborts — there is no unwind and no return path, which is why the region
 * being half-materialised at that instant is harmless rather than a leak.
 */

#include "kernels/mul.h"

#include "emit.h"
#include "kernels/addacc.h"
#include "kernels/bitwise.h"
#include "kernels/kernel.h"
#include "sandwich.h"
#include "scratch.h"

/* THE KERNEL IS A BLOCK PLUS A `dst`, and that is the whole of what M18's own
 * entry point adds over the export (PRD-v2 §7.10). `cq_kernel_mul` binds a
 * block at `off = 0` over a region it allocates itself; M34 will bind one at
 * whatever offset its preceding blocks ended at, over a region it allocates
 * itself. Neither path holds a second copy of the schedule. */
typedef struct {
    cq_bit      *dst;
    cq_mul_block k;
} mul_env;

/* Every accessor below is RELATIVE to `k->off`, which is what lets a consumer
 * put this block anywhere inside its own region — divrem_u.c's `span`, for the
 * same reason. `cq_scratch_span` bounds-checks the whole run, so a layout
 * off-by-one aborts at the mistake rather than at some later gate. */
static cq_bit *span(const cq_mul_block *k, int off, int len)
{
    return cq_scratch_span(k->scr, k->off + (uint32_t)off, (uint32_t)len);
}

/* `accum` is ONE register updated in place across all W iterations — that is
 * the whole point of the Cuccaro substitution, and it is what collapses ripple's
 * 3W² + W to W² + 2W. `pp[j]` cannot be recycled for j+1: Cuccaro RESTORES its
 * addend (adder.jl:50-51), so `pp[j]` still holds the partial product after the
 * accumulate rather than being cleared. */
static cq_bit *accum_of(const cq_mul_block *k)
{
    return span(k, 0, k->W);
}

static cq_bit *pp_of(const cq_mul_block *k, int j)
{
    return span(k, k->W * (1 + j), k->W);
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
static cq_bit *x_of(const cq_mul_block *k, int j)
{
    return span(k, k->W * (1 + k->W) + j, 1);
}

/* WHERE THE PRODUCT IS AT THE END OF THE COMPUTE HALF — `accum`, read as a
 * CONTROL by whoever copies it out. `const` on the way out is the enforcement
 * rather than the convention: a consumer that WROTE into it inside the same
 * compute half would make this block's reverse half non-cancelling (mul.h). */
const cq_bit *cq_mul_product(const cq_mul_block *k)
{
    if (k->W <= 0) cq_kernel_die("mul: width is not positive");
    return accum_of(k);
}

/* THE REGION, and the W = 1 row is a decision rather than an evaluation: the
 * block IS the sandwiched construction and `cq_kernel_mul` delegates i1 to K2,
 * so there is no region to ask for. 0 here and 0 from `cq_mul_steps` keep a
 * width-generic consumer's two running totals consistent (mul.h). */
int cq_mul_region(int W)
{
    if (W <= 0) cq_kernel_die("mul: width is not positive");
    return W == 1 ? 0 : W * W + 2 * W;
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

/* THE RANGE GUARD IS NEW AT THE EXPORT, AND IT REPLACES A "NO GUARD HERE,
 * DELIBERATELY" THAT WAS RIGHT WHILE THIS FUNCTION WAS FILE-STATIC. The old
 * reasoning was that `cq_sandwich` only ever calls it with `s` in
 * [0, n_compute), and that any `s` past the end runs off the last block into
 * `cq_addacc_step`'s own index guard one layer down — both still true, and
 * neither covers a NEGATIVE index or a consumer's own arithmetic. At `s < 0`
 * the binary search clamps to `j = 0`, `block_start(W, 0)` is 0 so `u == s`,
 * the phase-P branch is taken, and `a[s]` and `pp[0][s]` are read out of
 * bounds: undefined behaviour, silent in Release. That is the fault `cq_sub_step` and `cq_ult_step` already
 * refuse for M19, and PRD-v2 §7.1 names it as the thing an fp consumer can get
 * wrong — "the slot arithmetic, not the gates".
 *
 * ITS MESSAGE IS DISJOINT FROM `cq_addacc_step`'s so a death case can say which
 * layer spoke, and the width guard it rides on is `cq_mul_steps`'. BOTH ENDS
 * ARE DRIVEN, by `test_kernel_mul_death.mul_step_index_past_the_end` and
 * `.mul_step_index_is_negative`, and NEITHER CASE IS CARRIED BY ITS EXIT CODE:
 * measured 2026-09-18 with this line made unreachable, past-the-end still
 * aborts — from `addacc: step index out of range` one layer down — and the
 * negative index aborts too, from `shadow: qubit index out of range` in
 * Release and from an ASan `heap-buffer-overflow` in Debug. What turns both red
 * is the FAIL_REGULAR_EXPRESSION naming those layers, in tests/CMakeLists.txt.
 * Delete either the line or that regex and the cases go back to passing while
 * verifying nothing. */
void cq_mul_step(cq_ctx *ctx, const cq_mul_block *k_blk, int s)
{
    int W = k_blk->W, j, u;
    cq_addacc_block k;

    if (s < 0 || s >= cq_mul_steps(W))
        cq_kernel_die("mul: step index outside [0, cq_mul_steps(W))");

    j = outer_of(W, s);
    u = s - block_start(W, j);

    /* Phase P — the partial product for multiplier bit b[j], weight-shifted by
     * j. Upstream is `ToffoliGate(a[k], b[i], pp[dest])` with `dest = k + shift`
     * (multiplier.jl:27, 0-indexed here — this read `:24`, the inner `for k in
     * 1:W` header, until it was re-counted against the pinned file on
     * 2026-09-18), and `dest >= W` breaks at :26, so this block
     * is W − j gates and `pp[j][0..j-1]` is never written. Those low lanes hold
     * the VALUE zero for the whole compute half and are nonetheless CQ_BIT_Q
     * qubits under I6(b), so every Cuccaro gate touching them is emitted
     * physically — a real, deliberate cost and the price of a W-only golden. */
    if (u < W - j) {
        cq_emit_ccx(ctx, &k_blk->a[u], &k_blk->b[j], &pp_of(k_blk, j)[u + j]);
        return;
    }

    /* Phase A — the accumulate. THE ARGUMENT ORDER IS THE ONE THING TO GET
     * RIGHT: Bennett's call is `lower_add_cuccaro!(a = pp[j], b = accum)`, his
     * `b` is overwritten with the sum (adder.jl:62) and his `a` is the restored
     * addend — and libcqops inverts the letters, so our `acc` is his `b` and our
     * `b` is his `a` (addacc.h). Assigning positionally instead puts the product
     * in `pp[j]` and never accumulates: `dst` comes out 0, which L1 sees, but
     * the module still looks like the source. */
    k.acc = accum_of(k_blk);
    k.b   = pp_of(k_blk, j);
    k.x   = x_of(k_blk, j);
    k.W   = W;
    cq_addacc_step(ctx, &k, u - (W - j));
}

/* THE KERNEL'S COMPUTE HALF IS THE EXPORT, dispatched. There is ONE body, so a
 * consumer and `cq_kernel_mul` cannot drift apart, and the L4 golden this
 * module pins is a measurement of the same function M34 will call. */
static void compute(cq_ctx *ctx, void *env, int s)
{
    cq_mul_step(ctx, &((const mul_env *)env)->k, s);
}

/* The "^=" of Rule 7's contract, and the only place `dst` is written on the
 * sandwich path — which is why the driver runs it with the I6 extent DISARMED.
 * It reads the product through the same accessor a consumer does. */
static void copyout(cq_ctx *ctx, void *env, int i)
{
    const mul_env *e = (const mul_env *)env;

    cq_emit_cx(ctx, &cq_mul_product(&e->k)[i], &e->dst[i]);
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

    if (cq_bits_all_const(a, W) && cq_bits_all_const(b, W)) {
        fold_constant(ctx, dst, a, b, W);
        return;
    }

    /* ASKED OF cq_mul_region, NOT WRITTEN DOWN — the same binding the suite's
     * composition check makes one layer up, so the kernel and every consumer
     * size the region from one expression. */
    cq_scratch_alloc(&scr, (uint32_t)cq_mul_region(W));

    e.dst     = dst;
    e.k.a     = a;
    e.k.b     = b;
    e.k.scr   = &scr;
    e.k.off   = 0u;            /* the kernel owns the whole region */
    e.k.W     = W;

    cq_sandwich(ctx, &scr, compute, cq_mul_steps(W), copyout, W, &e);
    cq_scratch_dispose(&scr);
}
