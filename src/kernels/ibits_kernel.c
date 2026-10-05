/* src/kernels/ibits_kernel.c — M41, K26, the ENTRY POINTS: the width door,
 * risk R9's classical short-circuit and Bennett-in-the-small over one flat
 * region. The rows are ibits.c's and the gates ibits_step.c's; nothing here
 * emits a compute-half gate.
 *
 * ONE SANDWICH PER CALL (Rule 8, PRD §5). The compute half is the whole
 * program, the copy-out is W CNOTs from the closing `add`'s `t` into `dst`,
 * and the reverse half is the driver's. Bennett RETURNS the last row's wires
 * as the SSA result and leaves every intermediate for his global reverse; we
 * copy out and reverse locally, which is Rule 2's 2x and not a cost to chase.
 *
 * RISK R9, AND IT IS NOT AN OPTIMISATION. Pre-materialisation is
 * unconditional, so a fully classical `ctlz` at W = 64 would otherwise take
 * 16,576 qubits for an operation with no quantum input. The value comes from
 * a width-generic C loop over the lanes — BIT-SERIAL, NEVER PACKED (I5): a
 * `uint64_t` would cap the kernel at 64 lanes, add.c's reason. It is a
 * statement of what LLVM's intrinsic returns rather than a transcription of
 * the expansion (K26.md D-K26-5), and L1 checks it against an independent
 * oracle on every all-classical case the budget draws.
 */

#include "kernels/ibits.h"

#include "emit.h"
#include "kernels/kernel.h"
#include "sandwich.h"
#include "scratch.h"

typedef struct {
    cq_ibits_block k;
    cq_ibits_map   m;                  /* armed once per call (ibits.h) */
    cq_bit        *dst;
} ibits_env;

static void compute(cq_ctx *ctx, void *env, int s)
{
    const ibits_env *e = (const ibits_env *)env;

    cq_ibits_step_armed(ctx, &e->k, &e->m, s);
}

/* The "^=" of Rule 7's contract and the only place `dst` is written on the
 * sandwich path, which the driver runs with the I6 extent disarmed. */
static void copyout(cq_ctx *ctx, void *env, int s)
{
    const ibits_env *e = (const ibits_env *)env;

    cq_emit_cx(ctx, &cq_ibits_result_armed(&e->k, &e->m)[s], &e->dst[s]);
}

/* dst ^= c, lane by lane. `c` never exceeds W (a count of lanes), so it is
 * below 2^W at every W >= 1 and lanes 31 and up are zero. */
static void xor_count(cq_ctx *ctx, cq_bit *dst, int c, int W)
{
    for (int j = 0; j < W && j < 31; j++)
        if ((c >> j) & 1) cq_emit_x(ctx, &dst[j]);
}

static int lane(const cq_bit *a, int i) { return cq_bit_value(a[i]); }

static void fold_constant(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W,
                          cq_ibits_fn f)
{
    int c = 0;

    switch (f) {
    case CQ_IBF_CTPOP:
        for (int i = 0; i < W; i++) c += lane(a, i);
        xor_count(ctx, dst, c, W);
        return;
    case CQ_IBF_CTLZ:                     /* W when no lane is set (:4856) */
        c = W;
        for (int i = W - 1; i >= 0; i--)
            if (lane(a, i)) { c = W - 1 - i; break; }
        xor_count(ctx, dst, c, W);
        return;
    case CQ_IBF_CTTZ:                     /* W when no lane is set (:4878) */
        c = W;
        for (int i = 0; i < W; i++)
            if (lane(a, i)) { c = i; break; }
        xor_count(ctx, dst, c, W);
        return;
    case CQ_IBF_BITREVERSE:
        for (int j = 0; j < W; j++)
            if (lane(a, W - 1 - j)) cq_emit_x(ctx, &dst[j]);
        return;
    case CQ_IBF_BSWAP:
        for (int j = 0; j < W; j++)
            if (lane(a, 8 * (W / 8 - 1 - j / 8) + j % 8))
                cq_emit_x(ctx, &dst[j]);
        return;
    case CQ_IBF_N:
        break;
    }
    cq_kernel_die("ibits: fold of an unknown intrinsic");
}

static void ibits(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W,
                  cq_ibits_fn f)
{
    cq_ibits_row rows[CQ_IBITS_MAX_ROWS];
    const cq_bit *src[1];
    int w[1], n;
    cq_scratch scr;
    ibits_env e;

    /* The width door FIRST, and it is the program builder's: W in
     * [1, CQ_REG_WIDTH_MAX], and W % 16 == 0 for bswap — refused before any
     * operand is read, in both configurations. */
    n = cq_ibits_program(f, W, rows);

    src[0] = a; w[0] = W;
    cq_kernel_check_n(dst, W, src, w, 1);

    if (cq_bits_all_const(a, W)) {
        fold_constant(ctx, dst, a, W, f);
        return;
    }

    /* ONE CONTIGUOUS REGION, carved by the step machine's prefix walk —
     * emit.c's I6(a) check is a pointer RANGE test, so two regions would put
     * half the targets outside it. */
    cq_scratch_alloc(&scr, cq_ibits_region(rows, n, W));
    e.k.a = a; e.k.scr = &scr; e.k.off = 0u;
    e.k.rows = rows; e.k.n_rows = n; e.k.W = W;
    e.dst = dst;
    cq_ibits_arm(&e.k, &e.m);

    cq_sandwich(ctx, &scr, compute, cq_ibits_steps(rows, n, W), copyout, W, &e);
    cq_scratch_dispose(&scr);
}

void cq_kernel_ctpop(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W)
{ ibits(ctx, dst, a, W, CQ_IBF_CTPOP); }

void cq_kernel_ctlz(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W)
{ ibits(ctx, dst, a, W, CQ_IBF_CTLZ); }

void cq_kernel_cttz(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W)
{ ibits(ctx, dst, a, W, CQ_IBF_CTTZ); }

void cq_kernel_bitreverse(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W)
{ ibits(ctx, dst, a, W, CQ_IBF_BITREVERSE); }

void cq_kernel_bswap(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int W)
{ ibits(ctx, dst, a, W, CQ_IBF_BSWAP); }
