/* src/kernels/fpround_emit.c — M32, K23. THE DISPATCH AND THE SURFACE: which
 * gate a slot emits, and the four public blocks' entry points and accessors.
 * The layout and the operand resolution are next door in fpround_step.c, on
 * the seam fpround_int.h records (D-K23-10).
 *
 * ONE GATE PER SLOT, AND IT IS FORCED (bd ckd.14a, sandwich.h). cq_sandwich
 * runs the reverse pass by re-calling compute(env, s) with the SAME index, so
 * a step undoes itself only if it is an involution. Every branch of `emit_row`
 * emits exactly one X, CX or CCX, or delegates to a step block that does.
 *
 * COMPOSITE KERNELS CALL THE STEP FUNCTION, NEVER THE KERNEL. cq_kernel_eq,
 * cq_kernel_slt, cq_kernel_add and cq_kernel_shl_var are each a whole
 * sandwich, and cq_sandwich refuses nesting in BOTH configurations, so
 * reaching for one here would abort before allocating anything. M12 over M17
 * is the standing witness; M32 is the sixth consumer of the same rule.
 *
 * EVERY TARGET BELOW IS A BIT OF THE CALLER'S REGION — an inner block's own
 * internals, a `lower_not1!` wire, an `and`/`or` output — which is I6(a) by
 * construction. There is NO copy-out: M32 ships no Rule 7 kernel, so each
 * block publishes its outputs as accessors and the CONSUMER's sandwich does
 * the one copy-out at the end of the whole fp kernel.
 */

#include "kernels/fpround_int.h"

#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"
#include "kernels/shift_var.h"

/* `CQ_FPR_W` spelled short, for the same reason fcmp_step.c spells it `W64`:
 * every span expression below carries it two or three times. */
enum { W64 = CQ_FPR_W };

/* --- The dispatch. -------------------------------------------------------- */

static void emit_row(cq_ctx *ctx, const cq_fpr_ctx *x, const uint32_t *off,
                     int i, int u)
{
    const cq_fpround_row *r = &x->rows[i];
    cq_bit v0[CQ_FP64_W], v1[CQ_FP64_W], t0[CQ_FP64_W], t1[CQ_FP64_W];
    uint32_t o = off[i];
    int lane;

    switch (r->op) {
    case CQ_FROP_EQ: {
        cq_eq_block e;

        e.a = cq_fpr_op64(x, off, r->s0, v0, t0);
        e.b = cq_fpr_op64(x, off, r->s1, v1, t1);
        e.diff = cq_fpr_sp(x, o, (uint32_t)W64);
        e.orr  = cq_fpr_sp(x, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        e.W = W64;
        cq_eq_step(ctx, &e, u);
        return; }
    case CQ_FROP_ULT: {
        cq_ult_block c;

        c.a = cq_fpr_op64(x, off, r->s0, v0, t0);
        c.b = cq_fpr_op64(x, off, r->s1, v1, t1);
        c.nb    = cq_fpr_sp(x, o,                           (uint32_t)W64);
        c.carry = cq_fpr_sp(x, o + (uint32_t)W64,           (uint32_t)W64 + 1u);
        c.axnb  = cq_fpr_sp(x, o + (uint32_t)(2 * W64) + 1u, (uint32_t)W64);
        c.W = W64;
        cq_ult_step(ctx, &c, u);
        return; }
    case CQ_FROP_SLT: {
        cq_slt_block c;

        c.a = cq_fpr_op64(x, off, r->s0, v0, t0);
        c.b = cq_fpr_op64(x, off, r->s1, v1, t1);
        c.af    = cq_fpr_sp(x, o,                            (uint32_t)W64);
        c.bf    = cq_fpr_sp(x, o + (uint32_t)W64,            (uint32_t)W64);
        c.nb    = cq_fpr_sp(x, o + (uint32_t)(2 * W64),      (uint32_t)W64);
        c.carry = cq_fpr_sp(x, o + (uint32_t)(3 * W64),      (uint32_t)W64 + 1u);
        c.axnb  = cq_fpr_sp(x, o + (uint32_t)(4 * W64) + 1u, (uint32_t)W64);
        c.W = W64;
        cq_slt_step(ctx, &c, u);
        return; }
    case CQ_FROP_SUB: {
        cq_sub_block s;

        s.a = cq_fpr_op64(x, off, r->s0, v0, t0);
        s.b = cq_fpr_op64(x, off, r->s1, v1, t1);
        s.nb = cq_fpr_sp(x, o,                       (uint32_t)W64);
        s.d  = cq_fpr_sp(x, o + (uint32_t)W64,       (uint32_t)W64);
        s.c  = cq_fpr_sp(x, o + (uint32_t)(2 * W64), (uint32_t)W64);
        s.W = W64;
        cq_sub_step(ctx, &s, u);
        return; }
    case CQ_FROP_ADD: {
        cq_add_block a;

        a.a = cq_fpr_op64(x, off, r->s0, v0, t0);
        a.b = cq_fpr_op64(x, off, r->s1, v1, t1);
        a.t = cq_fpr_sp(x, o,                 (uint32_t)W64);
        a.c = cq_fpr_sp(x, o + (uint32_t)W64, (uint32_t)W64);
        a.W = W64;
        cq_add_step(ctx, &a, u);
        return; }
    case CQ_FROP_MUX: {
        cq_mux_block m;

        m.cond = cq_fpr_flag_of(x, off, r->s0);
        m.t    = cq_fpr_op64(x, off, r->s1, v0, t0);
        m.f    = cq_fpr_op64(x, off, r->s2, v1, t1);
        m.r    = cq_fpr_sp(x, o,                 (uint32_t)W64);
        m.d    = cq_fpr_sp(x, o + (uint32_t)W64, (uint32_t)W64);
        cq_mux_step(ctx, &m, u);
        return; }
    case CQ_FROP_BSHL: case CQ_FROP_BLSHR: {
        cq_barrel_block b;

        b.a = cq_fpr_op64(x, off, r->s0, v0, t0);
        b.b = cq_fpr_op64(x, off, r->s1, v1, t1);
        b.scr = x->scr; b.off = o; b.W = W64;
        b.dir = (r->op == CQ_FROP_BSHL) ? CQ_BARREL_SHL : CQ_BARREL_LSHR;
        cq_barrel_step(ctx, &b, u);
        return; }
    /* THE TWO COMMUTATIVE ROWS CARRY A RECORDED EQUIVALENT-MUTANT CLASS, AND
     * THE PAIRED MUTATION IS WHAT PROVED IT EQUIVALENT RATHER THAN UNTESTED
     * (bd equivalent-mutant-record-at-the-site). Measured 2026-09-18, Release:
     * exchanging `r->s0` and `r->s1` in `and`, and exchanging which operand the
     * `or`'s first two CNOTs read, BOTH SURVIVE every case in
     * tests/test_kernel_fpround.c — the value is unchanged (both operators are
     * commutative), the target set and the gate tuple are unchanged, the
     * palindrome holds and the scratch comes back. The SAME exchange on a row
     * whose operator is NOT commutative — `sub` at `_sf_handle_subnormal`'s
     * `Int64(1) - result_exp` — is killed by FOUR cases including L1, so the
     * survival is a fact about commutativity and not about the instruments.
     *
     * WHY THE SLOT SCAN CANNOT SEE IT, stated so nobody "fixes" the scan by
     * accident: the scan predicts an op KIND per slot and advances its stream
     * cursor only on a non-NONE, so exchanging which of two operands folds
     * moves the fold from one slot of a group to another and the CURSOR sees
     * the same one gate either way. Making it operand-aware would mean
     * checking each recorded gate's CONTROL index, which no scan in this repo
     * does — the span scan next door checks TARGETS. Filed, not built.
     *
     * DO NOT "SIMPLIFY" EITHER LINE TO MATCH THE MUTANT. `lower_or!` emits
     * CNOT(a), CNOT(b), Toffoli(a, b) in that order (arith.jl:276-278) and
     * Rule 1 is about provenance, not only about behaviour. */
    case CQ_FROP_AND:
        /* arith.jl:270's one Toffoli per lane, at 64 lanes. */
        cq_emit_ccx(ctx, &cq_fpr_op64(x, off, r->s0, v0, t0)[u],
                    &cq_fpr_op64(x, off, r->s1, v1, t1)[u],
                    &cq_fpr_sp(x, o, (uint32_t)W64)[u]);
        return;
    case CQ_FROP_OR:
        /* arith.jl:276-278, in order: CNOT(a), CNOT(b), Toffoli(a, b). */
        lane = u / 3;
        if (u % 3 == 0)
            cq_emit_cx(ctx, &cq_fpr_op64(x, off, r->s0, v0, t0)[lane],
                       &cq_fpr_sp(x, o, (uint32_t)W64)[lane]);
        else if (u % 3 == 1)
            cq_emit_cx(ctx, &cq_fpr_op64(x, off, r->s1, v1, t1)[lane],
                       &cq_fpr_sp(x, o, (uint32_t)W64)[lane]);
        else
            cq_emit_ccx(ctx, &cq_fpr_op64(x, off, r->s0, v0, t0)[lane],
                        &cq_fpr_op64(x, off, r->s1, v1, t1)[lane],
                        &cq_fpr_sp(x, o, (uint32_t)W64)[lane]);
        return;
    case CQ_FROP_NOT1:
        /* arith.jl:476, in order: CNOT(w[1], r[1]) then NOT(r[1]). */
        if (u == 0) cq_emit_cx(ctx, cq_fpr_flag_of(x, off, r->s0), cq_fpr_sp(x, o, 1u));
        else        cq_emit_x (ctx,                         cq_fpr_sp(x, o, 1u));
        return;
    case CQ_FROP_AND1:
        cq_emit_ccx(ctx, cq_fpr_flag_of(x, off, r->s0), cq_fpr_flag_of(x, off, r->s1),
                    cq_fpr_sp(x, o, 1u));
        return;
    case CQ_FROP_OR1:
        if (u == 0)      cq_emit_cx(ctx, cq_fpr_flag_of(x, off, r->s0), cq_fpr_sp(x, o, 1u));
        else if (u == 1) cq_emit_cx(ctx, cq_fpr_flag_of(x, off, r->s1), cq_fpr_sp(x, o, 1u));
        else             cq_emit_ccx(ctx, cq_fpr_flag_of(x, off, r->s0),
                                     cq_fpr_flag_of(x, off, r->s1), cq_fpr_sp(x, o, 1u));
        return;
    default:
        break;
    }
    cq_kernel_die("fpround: unknown op in the program");
}

static void fpr_step(cq_ctx *ctx, const cq_fpr_ctx *x, int u)
{
    uint32_t off[CQ_FPR_MAXR];

    if (u < 0 || u >= cq_fpr_steps_of(x))
        cq_kernel_die("fpround: step index outside [0, cq_<helper>_steps())");
    cq_fpr_arm(x, off);

    for (int i = 0; i < x->n; i++) {
        int n = cq_fpr_row_steps(&x->rows[i]);

        if (u < n) { emit_row(ctx, x, off, i, u); return; }
        u -= n;
    }
    cq_kernel_die("fpround: the step dispatch fell off the end of the program");
}

static const cq_bit *fpr_out(const cq_fpr_ctx *x, int which)
{
    uint32_t off[CQ_FPR_MAXR];
    int i = cq_fpround_out_row(x->id, which);

    cq_fpr_arm(x, off);
    if (x->rows[i].op == CQ_FROP_VIEW)
        cq_kernel_die("fpround: that output is a view and has no span");
    return (cq_fpr_op_width(x->rows[i].op) == 1) ? cq_fpr_flag_of(x, off, i)
                                          : cq_fpr_row_out(x, off, i);
}

/* --- The four public blocks. ---------------------------------------------- */

#define N52(k) cq_fpr_ctx_of(CQ_FPR_NORM52,  (k)->m,  (k)->e,  NULL, (k)->scr, (k)->off)
#define CLZ(k) cq_fpr_ctx_of(CQ_FPR_CLZ,     (k)->wr, (k)->result_exp, NULL,   \
                      (k)->scr, (k)->off)
#define SUB(k) cq_fpr_ctx_of(CQ_FPR_SUBNORM, (k)->wr, (k)->result_exp,         \
                      (k)->result_sign, (k)->scr, (k)->off)
#define RND(k) cq_fpr_ctx_of(CQ_FPR_ROUND,   (k)->wr, (k)->result_exp,         \
                      (k)->result_sign, (k)->scr, (k)->off)

#define FPR_NULLARY(id)                                                        \
    cq_fpr_ctx x = cq_fpr_ctx_of((id), NULL, NULL, NULL, NULL, 0u)

uint32_t cq_norm52_region (void) { FPR_NULLARY(CQ_FPR_NORM52);  return cq_fpr_region_of(&x); }
uint32_t cq_clz_region    (void) { FPR_NULLARY(CQ_FPR_CLZ);     return cq_fpr_region_of(&x); }
uint32_t cq_subnorm_region(void) { FPR_NULLARY(CQ_FPR_SUBNORM); return cq_fpr_region_of(&x); }
uint32_t cq_round_region  (void) { FPR_NULLARY(CQ_FPR_ROUND);   return cq_fpr_region_of(&x); }

int cq_norm52_steps (void) { FPR_NULLARY(CQ_FPR_NORM52);  return cq_fpr_steps_of(&x); }
int cq_clz_steps    (void) { FPR_NULLARY(CQ_FPR_CLZ);     return cq_fpr_steps_of(&x); }
int cq_subnorm_steps(void) { FPR_NULLARY(CQ_FPR_SUBNORM); return cq_fpr_steps_of(&x); }
int cq_round_steps  (void) { FPR_NULLARY(CQ_FPR_ROUND);   return cq_fpr_steps_of(&x); }

void cq_norm52_step(cq_ctx *ctx, const cq_norm52_block *k, int u)
{ cq_fpr_ctx x = N52(k); fpr_step(ctx, &x, u); }

void cq_clz_step(cq_ctx *ctx, const cq_clz_block *k, int u)
{ cq_fpr_ctx x = CLZ(k); fpr_step(ctx, &x, u); }

void cq_subnorm_step(cq_ctx *ctx, const cq_subnorm_block *k, int u)
{ cq_fpr_ctx x = SUB(k); fpr_step(ctx, &x, u); }

void cq_round_step(cq_ctx *ctx, const cq_round_block *k, int u)
{ cq_fpr_ctx x = RND(k); fpr_step(ctx, &x, u); }

const cq_bit *cq_norm52_m(const cq_norm52_block *k)
{ cq_fpr_ctx x = N52(k); return fpr_out(&x, 0); }

const cq_bit *cq_norm52_e(const cq_norm52_block *k)
{ cq_fpr_ctx x = N52(k); return fpr_out(&x, 1); }

const cq_bit *cq_clz_wr(const cq_clz_block *k)
{ cq_fpr_ctx x = CLZ(k); return fpr_out(&x, 0); }

const cq_bit *cq_clz_exp(const cq_clz_block *k)
{ cq_fpr_ctx x = CLZ(k); return fpr_out(&x, 1); }

const cq_bit *cq_subnorm_wr(const cq_subnorm_block *k)
{ cq_fpr_ctx x = SUB(k); return fpr_out(&x, 0); }

const cq_bit *cq_subnorm_exp(const cq_subnorm_block *k)
{ cq_fpr_ctx x = SUB(k); return fpr_out(&x, 1); }

const cq_bit *cq_subnorm_flag(const cq_subnorm_block *k)
{ cq_fpr_ctx x = SUB(k); return fpr_out(&x, 2); }

const cq_bit *cq_subnorm_ftz(const cq_subnorm_block *k)
{ cq_fpr_ctx x = SUB(k); return fpr_out(&x, 3); }

/* D-K23-8: a returned VIEW has no home in the caller's region, so its accessor
 * ASSEMBLES it. `off` is unused here — the view is over an operand — but the
 * region check is not, because a consumer that mis-sized its offset must hear
 * about it from the same place for every output. */
void cq_subnorm_flushed(const cq_subnorm_block *k, cq_bit out[CQ_FP64_W])
{
    cq_fpr_ctx x = SUB(k);
    uint32_t off[CQ_FPR_MAXR];
    int shift, b;
    uint64_t mask;
    cq_bit tmp[CQ_FP64_W];

    if (out == NULL) cq_kernel_die("fpround: no output array for the view");
    cq_fpr_arm(&x, off);
    b = cq_fpr_view_collapse(&x, cq_fpround_out_row(CQ_FPR_SUBNORM, 4), &shift, &mask);
    cq_fpr_view_fill(cq_fpr_base64(&x, off, b, tmp), shift, mask, out);
}

const cq_bit *cq_round_normal(const cq_round_block *k)
{ cq_fpr_ctx x = RND(k); return fpr_out(&x, 0); }

const cq_bit *cq_round_overflow_result(const cq_round_block *k)
{ cq_fpr_ctx x = RND(k); return fpr_out(&x, 1); }

const cq_bit *cq_round_exp_overflow(const cq_round_block *k)
{ cq_fpr_ctx x = RND(k); return fpr_out(&x, 2); }

const cq_bit *cq_round_exp_overflow_aft(const cq_round_block *k)
{ cq_fpr_ctx x = RND(k); return fpr_out(&x, 3); }
