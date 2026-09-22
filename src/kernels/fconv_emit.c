/* src/kernels/fconv_emit.c — M37, K19. THE DISPATCH AND THE SURFACE: which
 * gate a slot emits, the exported `cq_fptosi_block`, and the four Rule 7
 * kernels. The layout and the operand resolution are next door in
 * fconv_step.c, on the seam fconv_int.h records.
 *
 * ONE GATE PER SLOT, AND IT IS FORCED (bd ckd.14a, sandwich.h). cq_sandwich
 * runs the reverse pass by re-calling compute(env, s) with the SAME index, so
 * a step undoes itself only if it is an involution. Every branch of `emit_row`
 * emits exactly one X, CX or CCX, or delegates to a step block that does.
 *
 * COMPOSITE KERNELS CALL THE STEP FUNCTION, NEVER THE KERNEL. cq_kernel_eq,
 * cq_kernel_ult, cq_kernel_add, cq_kernel_shl_var and cq_kernel_fsub are each
 * a whole sandwich, and cq_sandwich refuses nesting in BOTH configurations, so
 * reaching for one here would abort before allocating anything. M12 over M17
 * is the standing witness; M37 is the eighth consumer of the same rule — and
 * it is also why `cq_kernel_fptoui` cannot call `cq_kernel_fptosi`: fptoui is
 * a longer PROGRAM with the fptosi table inlined twice, not a nested kernel.
 *
 * EVERY TARGET BELOW IS A BIT OF THE CALLER'S REGION — an inner block's own
 * internals, a `lower_not1!` wire, an `and`/`or`/`xor` output — which is I6(a)
 * by construction. `dst` is touched exclusively in the copy-out, which the
 * driver runs with the I6 extent deliberately DISARMED (sandwich.h).
 *
 * DO NOT `cqrt_free` ANYTHING INSIDE THE COMPUTE HALF. The whole
 * `result_left` span on the right-shift path, the whole `result_right` span on
 * the left-shift path, `path_a` on fptoui's high-range branch and five of the
 * six CLZ stages' `tmp_k` are all provably dead MATHEMATICALLY and not one of
 * them is provably |0> to the two-bit shadow. The sandwich reverse is the only
 * thing that returns K19's scratch to |0>.
 */

#include "kernels/fconv_int.h"

#include "emit.h"
#include "kernels/add.h"
#include "kernels/cmp.h"
#include "kernels/fadd.h"
#include "kernels/kernel.h"
#include "kernels/mux.h"
#include "kernels/shift_var.h"
#include "sandwich.h"

enum { W64 = CQ_FV_W };

/* --- The dispatch. -------------------------------------------------------- */

static void emit_row(cq_ctx *ctx, const cq_fv_ctx *x, const uint32_t *off,
                     int i, int u)
{
    const cq_fconv_row *r = &x->rows[i];
    cq_bit v0[CQ_FP64_W], v1[CQ_FP64_W], t0[CQ_FP64_W], t1[CQ_FP64_W];
    uint32_t o = off[i];
    int lane;

    switch (r->op) {
    case CQ_FVOP_EQ: {
        cq_eq_block e;

        e.a = cq_fv_op64(x, off, r->s0, v0, t0);
        e.b = cq_fv_op64(x, off, r->s1, v1, t1);
        e.diff = cq_fv_sp(x, o, (uint32_t)W64);
        e.orr  = cq_fv_sp(x, o + (uint32_t)W64, (uint32_t)W64 - 1u);
        e.W = W64;
        cq_eq_step(ctx, &e, u);
        return; }
    case CQ_FVOP_ULT: {
        cq_ult_block c;

        c.a = cq_fv_op64(x, off, r->s0, v0, t0);
        c.b = cq_fv_op64(x, off, r->s1, v1, t1);
        c.nb    = cq_fv_sp(x, o,                            (uint32_t)W64);
        c.carry = cq_fv_sp(x, o + (uint32_t)W64,            (uint32_t)W64 + 1u);
        c.axnb  = cq_fv_sp(x, o + (uint32_t)(2 * W64) + 1u, (uint32_t)W64);
        c.W = W64;
        cq_ult_step(ctx, &c, u);
        return; }
    case CQ_FVOP_SUB: {
        cq_sub_block s;

        s.a = cq_fv_op64(x, off, r->s0, v0, t0);
        s.b = cq_fv_op64(x, off, r->s1, v1, t1);
        s.nb = cq_fv_sp(x, o,                       (uint32_t)W64);
        s.d  = cq_fv_sp(x, o + (uint32_t)W64,       (uint32_t)W64);
        s.c  = cq_fv_sp(x, o + (uint32_t)(2 * W64), (uint32_t)W64);
        s.W = W64;
        cq_sub_step(ctx, &s, u);
        return; }
    case CQ_FVOP_ADD: {
        cq_add_block a;

        /* K6's OUT-OF-PLACE ripple, TEN TIMES, AND NEVER K8. The six CLZ
         * rungs read exactly like an in-place accumulate and CLAUDE.md's
         * verdict on that substitution is that the forward value would be
         * right and only the `_unc` wrong — a silent miscompile rather than a
         * test failure. The second, independent reason is that each rung's
         * mux reads the PRE-add value as its false arm (sitofp.jl:30), which
         * an in-place accumulate has destroyed. */
        a.a = cq_fv_op64(x, off, r->s0, v0, t0);
        a.b = cq_fv_op64(x, off, r->s1, v1, t1);
        a.t = cq_fv_sp(x, o,                 (uint32_t)W64);
        a.c = cq_fv_sp(x, o + (uint32_t)W64, (uint32_t)W64);
        a.W = W64;
        cq_add_step(ctx, &a, u);
        return; }
    case CQ_FVOP_MUX: {
        cq_mux_block m;

        m.cond = cq_fv_flag_of(x, off, r->s0);
        m.t    = cq_fv_op64(x, off, r->s1, v0, t0);
        m.f    = cq_fv_op64(x, off, r->s2, v1, t1);
        m.r    = cq_fv_sp(x, o,                 (uint32_t)W64);
        m.d    = cq_fv_sp(x, o + (uint32_t)W64, (uint32_t)W64);
        cq_mux_step(ctx, &m, u);
        return; }
    case CQ_FVOP_BSHL: case CQ_FVOP_BLSHR: {
        cq_barrel_block b;

        b.a = cq_fv_op64(x, off, r->s0, v0, t0);
        b.b = cq_fv_op64(x, off, r->s1, v1, t1);
        b.scr = x->scr; b.off = o; b.W = W64;
        b.dir = (r->op == CQ_FVOP_BSHL) ? CQ_BARREL_SHL : CQ_BARREL_LSHR;
        cq_barrel_step(ctx, &b, u);
        return; }
    case CQ_FVOP_FSUB: {
        cq_fsub_block k;

        /* fptoui.jl:45's `soft_fsub(a, 0x43E0000000000000)`, as M33's whole
         * exported compute half. `b` is the CONSTANT KBIAS, so this is the one
         * call site in the project where `fsub` and `fadd(a, fneg(b))`
         * coincide — fsub.jl:23's NaN guard is provably false on a constant
         * that is not a NaN — and therefore the one site where PRD-v2 §5's
         * "do not spell fsub as fadd of fneg" prohibition is INVISIBLE. M33
         * exports the real thing and this row calls it. */
        k.a = cq_fv_op64(x, off, r->s0, v0, t0);
        k.b = cq_fv_op64(x, off, r->s1, v1, t1);
        k.scr = x->scr; k.off = o;
        cq_fsub_step(ctx, &k, u);
        return; }
    /* THE THREE COMMUTATIVE ROWS CARRY M32's RECORDED EQUIVALENT-MUTANT CLASS
     * (bd a-slot-scan-cannot-see-which-operand-a-slot-reads): exchanging the
     * two operands of `and`, `or` or `xor` leaves the value, the target set,
     * the gate tuple, the palindrome and the scratch all correct, because both
     * operators are commutative and no instrument in this repo checks a gate's
     * CONTROL index. DO NOT "SIMPLIFY" EITHER LINE TO MATCH THE MUTANT:
     * `lower_or!` emits CNOT(a), CNOT(b), Toffoli(a, b) in that order
     * (arith.jl:276-278) and Rule 1 is about provenance, not only behaviour. */
    case CQ_FVOP_AND:
        /* arith.jl:270's one Toffoli per lane, at 64 lanes. The ONE `and`
         * occurrence in K19 whose second operand is computed (sitofp.jl:71);
         * every other `&` in the three sources has a compile-time constant
         * mask and is a VIEW (PRD-v2 §7.3 as amended). */
        cq_emit_ccx(ctx, &cq_fv_op64(x, off, r->s0, v0, t0)[u],
                    &cq_fv_op64(x, off, r->s1, v1, t1)[u],
                    &cq_fv_sp(x, o, (uint32_t)W64)[u]);
        return;
    case CQ_FVOP_OR:
        /* arith.jl:276-278, in order: CNOT(a), CNOT(b), Toffoli(a, b). */
        lane = u / 3;
        if (u % 3 == 0)
            cq_emit_cx(ctx, &cq_fv_op64(x, off, r->s0, v0, t0)[lane],
                       &cq_fv_sp(x, o, (uint32_t)W64)[lane]);
        else if (u % 3 == 1)
            cq_emit_cx(ctx, &cq_fv_op64(x, off, r->s1, v1, t1)[lane],
                       &cq_fv_sp(x, o, (uint32_t)W64)[lane]);
        else
            cq_emit_ccx(ctx, &cq_fv_op64(x, off, r->s0, v0, t0)[lane],
                        &cq_fv_op64(x, off, r->s1, v1, t1)[lane],
                        &cq_fv_sp(x, o, (uint32_t)W64)[lane]);
        return;
    case CQ_FVOP_XOR:
        /* arith.jl:287-288, in order: CNOT(a), CNOT(b), per lane. Both
         * occurrences are `~x` (fptosi.jl:60, sitofp.jl:22), which LLVM
         * spells `xor i64 %x, -1` — there is no full-width `lower_not!` — so
         * the second operand is K_ONES and the fold table turns all 64 of the
         * second CNOTs into an X and none of them into nothing. */
        lane = u / 2;
        if (u % 2 == 0)
            cq_emit_cx(ctx, &cq_fv_op64(x, off, r->s0, v0, t0)[lane],
                       &cq_fv_sp(x, o, (uint32_t)W64)[lane]);
        else
            cq_emit_cx(ctx, &cq_fv_op64(x, off, r->s1, v1, t1)[lane],
                       &cq_fv_sp(x, o, (uint32_t)W64)[lane]);
        return;
    case CQ_FVOP_NOT1:
        /* arith.jl:476, in order: CNOT(w[1], r[1]) then NOT(r[1]). */
        if (u == 0) cq_emit_cx(ctx, cq_fv_flag_of(x, off, r->s0),
                               cq_fv_sp(x, o, 1u));
        else        cq_emit_x (ctx, cq_fv_sp(x, o, 1u));
        return;
    case CQ_FVOP_AND1:
        cq_emit_ccx(ctx, cq_fv_flag_of(x, off, r->s0),
                    cq_fv_flag_of(x, off, r->s1), cq_fv_sp(x, o, 1u));
        return;
    default:
        break;
    }
    cq_kernel_die("fconv: unknown op in the program");
}

void cq_fv_step(cq_ctx *ctx, const cq_fv_ctx *x, int u)
{
    uint32_t off[CQ_FV_MAXR];
    int slots = 0;

    cq_fv_arm(x, off, &slots);
    if (u < 0 || u >= slots)
        cq_kernel_die("fconv: step index outside [0, steps())");

    for (int i = 0; i < x->n; i++) {
        int n = cq_fv_row_steps(&x->rows[i]);

        if (u < n) { emit_row(ctx, x, off, i, u); return; }
        u -= n;
    }
    cq_kernel_die("fconv: the step dispatch fell off the end of the program");
}

static void fv_step_armed(cq_ctx *ctx, const cq_fv_ctx *x,
                          const cq_fv_map *m, const uint32_t *off, int u)
{
    int i, within;

    if (u < 0 || u >= m->steps)
        cq_kernel_die("fconv: step index outside [0, steps())");
    i = cq_fv_row_at(m, u, &within);
    if (cq_fv_row_steps(&m->rows[i]) <= 0)
        cq_kernel_die("fconv: the step dispatch landed on a zero-slot row");
    emit_row(ctx, x, off, i, within);
}

static void fv_step_mapped(cq_ctx *ctx, const cq_fv_ctx *x,
                           const cq_fv_map *m, int u)
{
    uint32_t off[CQ_FV_MAXR];

    cq_fv_arm_map(x, m, off);
    fv_step_armed(ctx, x, m, off, u);
}

static const cq_bit *fv_result_armed(const cq_fv_ctx *x, cq_fconv_prog p,
                                     const uint32_t *off)
{ return cq_fv_row_out(x, off, cq_fconv_result_row(p)); }

static const cq_bit *fv_result_mapped(const cq_fv_ctx *x, cq_fconv_prog p,
                                      const cq_fv_map *m)
{
    uint32_t off[CQ_FV_MAXR];

    cq_fv_arm_map(x, m, off);
    return fv_result_armed(x, p, off);
}

const cq_bit *cq_fv_result(const cq_fv_ctx *x, cq_fconv_prog p)
{
    uint32_t off[CQ_FV_MAXR];

    cq_fv_arm(x, off, NULL);
    return cq_fv_row_out(x, off, cq_fconv_result_row(p));
}

/* --- The exported `fptosi` compute half. --------------------------------- */

/* The public block carries no row pointer: a consumer cannot forget to bind
 * internal state. The immutable program map is selected here, and a transient
 * context combines it with this invocation's operands and region. */
static cq_fv_ctx fptosi_ctx(const cq_fptosi_block *k, const cq_fv_map *m)
{
    cq_fv_ctx x;

    x.a = k->a; x.a_w = CQ_FV_W; x.scr = k->scr; x.off = k->off;
    x.n = m->n;
    x.rows = m->rows;
    return x;
}

uint32_t cq_fptosi_region(void)
{
    return cq_fv_map_get(CQ_FCONV_PROG_FPTOSI)->region;
}

int cq_fptosi_steps(void)
{
    return cq_fv_map_get(CQ_FCONV_PROG_FPTOSI)->steps;
}

void cq_fptosi_step(cq_ctx *ctx, const cq_fptosi_block *k, int u)
{
    const cq_fv_map *m = cq_fv_map_get(CQ_FCONV_PROG_FPTOSI);
    cq_fv_ctx x = fptosi_ctx(k, m);

    fv_step_mapped(ctx, &x, m, u);
}

const cq_bit *cq_fptosi_result(const cq_fptosi_block *k)
{
    const cq_fv_map *m = cq_fv_map_get(CQ_FCONV_PROG_FPTOSI);
    cq_fv_ctx x = fptosi_ctx(k, m);

    return fv_result_mapped(&x, CQ_FCONV_PROG_FPTOSI, m);
}

/* --- The four Rule 7 kernels: Bennett-in-the-small over one flat region. -- */

typedef struct {
    cq_fv_ctx     x;
    const cq_fv_map *map;
    uint32_t      off[CQ_FV_MAXR];
    cq_fconv_prog p;
    cq_bit       *dst;
    int           T;
} fconv_env;

static void fconv_compute(cq_ctx *ctx, void *env, int s) {
    const fconv_env *e = (const fconv_env *)env;

    fv_step_armed(ctx, &e->x, e->map, e->off, s);
}

static void fconv_copyout(cq_ctx *ctx, void *env, int s) {
    const fconv_env *e = (const fconv_env *)env;

    cq_emit_cx(ctx, &fv_result_armed(&e->x, e->p, e->off)[s], &e->dst[s]);
}

/* The F lanes of an ALL-CLASSICAL source as a `uint64_t`, for risk R9. M31's
 * `cq_fp_pack` reads exactly 64 and `uitofp`'s source is narrower, so this is
 * the narrow form with the same refusal. It is local rather than an addition
 * to M31 because the fp FIELD geometry is M31's subject and an integer source
 * rail has none. */
static uint64_t pack_w(const cq_bit *a, int F) {
    uint64_t v = UINT64_C(0);

    for (int i = 0; i < F; i++) {
        if (!cq_bit_is_const(a[i]))
            cq_kernel_die("fconv: pack of a lane that is a qubit — the R9 "
                          "short-circuit runs only on an all-classical rail");
        v |= (uint64_t)cq_bit_value(a[i]) << (unsigned)i;
    }
    return v;
}

static void fconv(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T,
                  cq_fconv_prog p, uint64_t (*ev)(uint64_t, int))
{
    cq_scratch scr;
    fconv_env e;
    const cq_bit *src[1];
    int w[1];

    src[0] = a; w[0] = F;
    cq_kernel_check_n(dst, T, src, w, 1);

    /* RISK R9, AND IT IS NOT AN OPTIMISATION (plan §0.2 consequence 2).
     * Pre-materialisation is unconditional, so without this a fully classical
     * `fptoui` would take ~35,000 qubits for an operation with no quantum
     * input at all. The value comes from the SAME Julia body the circuit runs
     * (PRD-v2 §7.4), never from a host cast — which for every NaN, every
     * infinity and every out-of-range operand is UNDEFINED BEHAVIOUR in C. */
    if (cq_bits_all_const(a, F)) {
        uint64_t v = ev(pack_w(a, F), F);

        for (int i = 0; i < T; i++)
            if (((v >> (unsigned)i) & UINT64_C(1)) != 0u) cq_emit_x(ctx, &dst[i]);
        return;
    }

    e.map = cq_fv_map_get(p);
    e.x.a = a; e.x.a_w = F; e.x.off = 0u; e.x.scr = NULL;
    e.x.n = e.map->n;
    e.x.rows = e.map->rows;
    e.p = p;
    e.dst = dst;
    e.T = T;

    /* ONE CONTIGUOUS REGION, carved into named sub-arrays, because emit.c's
     * I6(a) check is a pointer RANGE test over cq_bit addresses — a kernel
     * that allocated two regions would put half its targets outside it. */
    cq_scratch_alloc(&scr, e.map->region);
    e.x.scr = &scr;
    cq_fv_arm_map(&e.x, e.map, e.off);

    cq_sandwich(ctx, &scr, fconv_compute, e.map->steps,
                fconv_copyout, T, &e);
    cq_scratch_dispose(&scr);
}

static uint64_t ev_fptosi(uint64_t a, int F) { (void)F; return cq_fptosi_eval(a); }
static uint64_t ev_fptoui(uint64_t a, int F) { (void)F; return cq_fptoui_eval(a); }
static uint64_t ev_sitofp(uint64_t a, int F) { (void)F; return cq_sitofp_eval(a); }
static uint64_t ev_uitofp(uint64_t a, int F) { return cq_uitofp_eval(a, F); }

/* PRD-v2 §1 scopes v2 to f64 and all three Julia bodies are `(UInt64)`, so a
 * width pair that is not one of the four shipped ones is a FICTION rather than
 * an unimplemented case. Hard error in BOTH configurations, naming the kernel.
 */
static void need_64_64(const char *who, int F, int T)
{
    if (F != CQ_FP64_W || T != CQ_FP64_W)
        cq_kernel_die(who);
}

void cq_kernel_fptosi(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T)
{
    need_64_64("fptosi: the only shipped pair is f64 -> i64 — v2 is f64 only, "
               "and a narrower target is cq_kernel_trunc after this kernel "
               "(instructions.jl:7657 emits it as a second instruction)", F, T);
    fconv(ctx, dst, a, F, T, CQ_FCONV_PROG_FPTOSI, ev_fptosi);
}

void cq_kernel_fptoui(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T)
{
    need_64_64("fptoui: the only shipped pair is f64 -> u64 — v2 is f64 only, "
               "and a narrower target is cq_kernel_trunc after this kernel "
               "(instructions.jl:7657 emits it as a second instruction)", F, T);
    fconv(ctx, dst, a, F, T, CQ_FCONV_PROG_FPTOUI, ev_fptoui);
}

void cq_kernel_sitofp(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T)
{
    /* instructions.jl:7672-7673 passes a 64-bit source straight through and
     * :7677-7679 `sext`s a narrower one. Only the 64-bit pair ships, so this
     * kernel never builds a widening view and there is no `sext` anywhere in
     * M37 — which is why the one view `base64` does build is a `zext`. */
    need_64_64("sitofp: the only shipped pair is i64 -> f64 — v2 is f64 only, "
               "and a narrower signed source is cq_kernel_sext before this "
               "kernel (instructions.jl:7677-7679 emits it as a separate cast)",
               F, T);
    fconv(ctx, dst, a, F, T, CQ_FCONV_PROG_SITOFP, ev_sitofp);
}

void cq_kernel_uitofp(cq_ctx *ctx, cq_bit *dst, const cq_bit *a, int F, int T)
{
    const int *ws;
    int nw, ok = 0;

    /* BEAD 9ve.34, AND IT IS A REFUSAL RATHER THAN A GAP (PRD-v2 §7.9).
     * instructions.jl:7666-7670 routes UIToFP to `soft_sitofp` and :7672-7673
     * returns the BARE call when the source is already 64 bits, so there is no
     * widening cast to make :7677's `:zext` mean anything and sitofp.jl:20
     * reads bit 63 as a SIGN. Every `u >= 2^63` would come back negative —
     * the exact mirror of the `U31` bug upstream fixed on the fp->int side
     * (fptoui.jl:10-12). Porting it verbatim is a known miscompile on half the
     * input space (NORTH_STAR condition 2) and correcting it would be original
     * reversible-circuit work in this repo (Rule 1), so this ONE width dies
     * loudly. It must NEVER fall through to `sitofp`: the check is before the
     * table below precisely so that adding 64 to that table cannot enable it.
     */
    if (F == CQ_FP64_W)
        cq_kernel_die("uitofp: i64 -> f64 is a loud abort, bead 9ve.34 / "
                      "PRD-v2 §7.9 — upstream routes UIToFP to soft_sitofp "
                      "with NO bias correction at this one width, so every "
                      "u >= 2^63 would convert as a negative number. i1, i8, "
                      "i16 and i32 sources port on the zext path");

    ws = cq_fconv_uitofp_widths(&nw);
    for (int i = 0; i < nw; i++) if (F == ws[i]) ok = 1;
    if (!ok || T != CQ_FP64_W)
        cq_kernel_die("uitofp: the shipped source widths are i1, i8, i16 and "
                      "i32 and the target is f64");

    /* The ZEXT IS AT THE OPERAND and the row program is `sitofp`'s, byte for
     * byte — instructions.jl:7677-7679's widening cast, which under PRD-v2
     * §7.3's amendment is WIRING: lanes [F, 64) are CQ_BIT_ZERO entries of a
     * read-only view, zero gates and zero qubits. */
    fconv(ctx, dst, a, F, T, CQ_FCONV_PROG_SITOFP, ev_uitofp);
}
