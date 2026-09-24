/* src/kernels/fmisc_sign.c — M38, K24, the SIGN OPS: `fneg`, `fabs`,
 * `copysign` at f64. PRD-v2 §5's M38 seam, SIGN OPS <-> EVERYTHING ELSE.
 *
 * Read docs/constructions/K24.md §2.1 and fmisc.h before changing anything.
 *
 * ONE UPSTREAM BLOCK EACH, EMITTED STRAIGHT ONTO `dst` — K1's and K3's shape
 * (src/kernels/bitwise.c), not a sandwich (K24 D-K24-1). Upstream's block
 * allocates a fresh result wire and writes it; a fresh zero `dst` IS that
 * wire, and the same call on `dst = out` XORs the same value in again, so
 * there is no scratch for Rule 2 to return and no reverse half to run.
 *
 * THE CONSTANTS ARE SOURCES AND THE MASKS ARE VIEWS (PRD-v2 §7.3): the sign
 * bit is a `const cq_bit` span, and `a & 0x7FFF…` / `y & 0x8000…` are views
 * keeping their lanes IN PLACE with CQ_BIT_ZERO elsewhere. The §3 fold table
 * does the rest — a ZERO control removes a gate, a ONE control demotes it —
 * which is why every block below is written over all 64 lanes with no lane
 * skipped by hand: skipping a provably-constant lane is the "optimisation"
 * mutant K11 warns about, and here the fold table already emits nothing for
 * it.
 *
 * NO `_eval` AND NO SHORT-CIRCUIT (K24 D-K24-2). R9's hazard is
 * pre-materialisation, and nothing here materialises anything: on classical
 * operands every gate is a constant-controlled X/CX onto a constant `dst`
 * lane, which the fold table rewrites in place for zero gates and zero
 * qubits. That IS L5, with no C row of its own to disagree with the circuit.
 */

#include "kernels/fmisc.h"

#include "emit.h"
#include "kernels/kernel.h"

enum { W64 = CQ_FP64_W };

#define SG_SIGN  UINT64_C(0x8000000000000000)   /* instructions.jl:7760, :5010 */
#define SG_MAG   UINT64_C(0x7FFFFFFFFFFFFFFF)   /* instructions.jl:5000, :5009 */

/* Slots per lane of each block, transcribed from the pinned source:
 * `lower_xor!` CNOT(a), CNOT(b) (arith.jl:284-290); `_emit_copy_out!` one
 * CNOT (:89-96); `lower_or!` CNOT(a), CNOT(b), Toffoli(a, b) (:274-282). */
enum { SG_XOR = 2, SG_COPY = 1, SG_OR = 3 };

int cq_fneg_steps(void)     { return SG_XOR  * W64; }
int cq_fabs_steps(void)     { return SG_COPY * W64; }
int cq_copysign_steps(void) { return SG_OR   * W64; }

/* An `&` by a compile-time constant: the lanes the mask keeps, IN PLACE. */
static void sg_view(const cq_bit *a, uint64_t mask, cq_bit *out)
{
    for (int i = 0; i < W64; i++)
        out[i] = ((mask >> (unsigned)i) & 1u) ? a[i] : cq_bit_zero();
}

/* PRD-v2 §1: f64 only. Upstream's extractor would build `fneg` and `fabs`
 * at any `w`; an f64 port does not promise that, so another width is a hard
 * error in BOTH configurations (M40's and M39's posture). */
static void sg_width(int W, const char *what)
{
    if (W != W64) cq_kernel_die(what);
}

static void sg_unary_check(cq_bit *dst, const cq_bit *a, int W)
{
    const cq_bit *src[1] = { a };
    int w[1] = { W };

    /* D7a at the kernel boundary; the D7b leg is vacuous at arity 1, which
     * is why this is cq_kernel_check_n and not the arity-2 wrapper. */
    cq_kernel_check_n(dst, W, src, w, 1);
}

/* `fneg` — instructions.jl:7755-7762: IRBinOp(:xor, a, sign_bit), which is
 * `soft_fneg` (fneg.jl:6) verbatim; `lower_xor!` per lane, in order. */
void cq_kernel_fneg(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W)
{
    cq_bit k[CQ_FP64_W];

    sg_width(W, "fmisc: fneg: W must be 64 — v2 is f64 only");
    sg_unary_check(dst, a, W);
    cq_fp_const(k, SG_SIGN);
    for (int i = 0; i < W64; i++) {
        cq_emit_cx(ctx, &a[i], &dst[i]);
        cq_emit_cx(ctx, &k[i], &dst[i]);
    }
}

/* `fabs` — instructions.jl:4997-5001: IRBinOp(:and, a, 0x7FFF…), a VIEW
 * under §7.3, whose value reaches `dst` by Rule 7's `^=` — Bennett's
 * `_emit_copy_out!`, one CNOT per lane. Lane 63 is a constant ZERO. */
void cq_kernel_fabs(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W)
{
    cq_bit v[CQ_FP64_W];

    sg_width(W, "fmisc: fabs: W must be 64 — v2 is f64 only");
    sg_unary_check(dst, a, W);
    sg_view(a, SG_MAG, v);
    for (int i = 0; i < W64; i++) cq_emit_cx(ctx, &v[i], &dst[i]);
}

/* `copysign` — instructions.jl:5003-5017: `mag = x & mag_mask` and
 * `sgn = y & sign_bit` (two VIEWS), then IRBinOp(:or, mag, sgn), which is
 * `lower_or!` per lane, in order. NOT COMMUTATIVE: the magnitude is `x`'s. */
void cq_kernel_copysign(cq_ctx *ctx, cq_bit *dst, const cq_bit *x,
                        const cq_bit *y, int W)
{
    cq_bit m[CQ_FP64_W], s[CQ_FP64_W];

    sg_width(W, "fmisc: copysign: W must be 64 — v2 is f64 only");
    cq_kernel_check_dst(dst, x, y, W);
    sg_view(x, SG_MAG, m);
    sg_view(y, SG_SIGN, s);
    for (int i = 0; i < W64; i++) {
        cq_emit_cx (ctx, &m[i], &dst[i]);
        cq_emit_cx (ctx, &s[i], &dst[i]);
        cq_emit_ccx(ctx, &m[i], &s[i], &dst[i]);
    }
}
