/* tests/support/kerneldrv.c — the shared Phase-B gate, one case at a time.
 *
 * SPLIT AT STEP 11, on the seam plan §2.2 recorded: the sweep SHAPES (which
 * widths, which values, how the masks are assigned) live in kernelsweep.c and
 * the four LEVELS live here. That is the right cut because Step 20 re-runs
 * every kernel under control by re-driving cq_kd_case, and Steps 12-17 vary
 * the sweep without touching a single assertion.
 *
 * VALUES ARE TWO WORDS THROUGHOUT. Casts reach i80 and i128 — they are the
 * only way an i128 register exists at all — and a driver capped at 64 could
 * check 30 of the 55 cast pairs CQ_lang ships. cq_kd_case2 is the one-word
 * convenience wrapper, not a second path.
 */

#include "support/kerneldrv.h"

#include "reg.h"
#include "support/harness.h"
#include "support/poolcheck.h"

void cq_kd_default_shape(int W, cq_kd_shape *out)
{
    out->n_src = 2;
    out->w_dst = W;
    for (int i = 0; i < CQ_KD_MAX_SRC; i++) {
        out->w[i] = W;
        out->classical[i] = 0u;
    }
}

/* Returns 1 if the shape is one this driver can actually drive. A malformed
 * shape RETURNS EARLY rather than recording a failure and pressing on, and the
 * distinction is load-bearing: both refusals below describe a spec the default
 * call path would then dereference wrongly, so continuing turns a diagnosable
 * failure into a crash — or, worse, into a green run.
 *
 * THE DEFAULT CALL PATH IS DEFINED ONLY FOR THE ARITY-2, ONE-WIDTH SHAPE, and
 * before Step 13 that was true by coincidence rather than by construction
 * (bd zwh). call_kernel's default branch is
 *
 *     k->kernel(ctx, dst, src[0], src[1], sh->w_dst);
 *
 * which makes two assumptions the cq_kd_shape type does not enforce. It reads
 * `src[1]`, which cq_kd_case fills only for `i < n_src`; and it passes
 * `w_dst` as the kernel's `W`, although Rule 7's `W` is the OPERAND width.
 * Those agree for every kernel whose result is as wide as its operands, which
 * is all of them but K9 — whose `dst` is one bit, and which therefore supplies
 * a call adapter passing `sh->w[0]`.
 *
 * THE FAILURE MODE IS SILENT AND GREEN, which is why this is a refusal and not
 * a comment. A spec with `w_dst = 1` and no adapter runs the kernel at W=1 and
 * then compares it against a reference computed from the same `w_dst` — so L1
 * agrees, L4 pins whatever it measured, and nothing anywhere notices that
 * fifteen of the sixteen bits were never tested. K10's three-source mux (bd
 * ckd.15) and M12's variable shifts are the next two shapes where the widths
 * stop agreeing.
 *
 * Provoked, and observed to refuse, in tests/test_kerneldrv.c. */
static int shape_of(const cq_kd_spec *k, int W, cq_kd_shape *sh)
{
    cq_kd_default_shape(W, sh);
    if (k->shape) k->shape(W, sh);

    if (sh->n_src < 1 || sh->n_src > CQ_KD_MAX_SRC) {
        cq_h_fail(__FILE__, __LINE__, "%s: shape declares %d sources",
                  k->name, sh->n_src);
        return 0;
    }

    if (!k->call && (sh->n_src != 2 || sh->w_dst != sh->w[0])) {
        cq_h_fail(__FILE__, __LINE__,
                  "%s: shape is n_src=%d, w_dst=%d, w[0]=%d, and the spec "
                  "supplies no call adapter. The default path passes w_dst as "
                  "the kernel's W and reads src[1]; it cannot know which width "
                  "this kernel's W means. Add a `call` (see M13's casts and "
                  "M16's compares)", k->name, sh->n_src, sh->w_dst, sh->w[0]);
        return 0;
    }

    return 1;
}

typedef struct {
    cq_ctx     ctx;
    cq_counter cnt;
    cq_sink    sink;
} fixture;

static void fx_open(fixture *f)
{
    cq_count_reset(&f->cnt);
    f->sink = cq_sink_counter(&f->cnt);
    cq_ctx_init(&f->ctx, &f->sink);
}

static void fx_close(fixture *f)
{
    /* Deliberately does NOT free the operand rails. A rail holding a non-zero
     * value on materialised qubits is genuinely not |0> and refusing to free
     * it is Rule 6 working, not a defect — cq_ctx_dispose returns nothing to
     * the pool, which is the intended Rule-6 safe leak (PRD §10, src/ctx.c). */
    cq_ctx_dispose(&f->ctx);
}

/* Rule 7's "leaving the sources unchanged", asserted rather than assumed.
 *
 * TWO CLAIMS, AND ONLY ONE MAY CROSS THE UNCOMPUTE AXIS. The VALUE claim
 * always holds. The KIND claim — that a source bit is still classical if the
 * mask said classical — is a statement about our REPRESENTATION, and Rule 14
 * forbids asserting representation across the uncompute axis: `_unc`
 * legitimately sees materialised bits that were constants at forward time,
 * because a rotation on a source between the two calls materialises them and
 * we never demote (D6). Asserting it anyway is risk R6 by name.
 *
 * So `kinds` is a parameter: 1 after the forward, where the claim is exactly
 * right and catches a kernel that materialises a source; 0 after the
 * uncompute. Inert at Step 11 — nothing can rotate before Step 19 — and
 * load-bearing for Step 21, whose whole subject is that asymmetry. */
enum { KINDS_TOO = 1, VALUE_ONLY = 0 };

static void check_source(const cq_ctx *ctx, int idx, int32_t h, cq_ref_w want,
                         cq_ref_w qmask, int W, const char *kname, int kinds)
{
    const cq_bit *bits = cq_reg_cbits(&ctx->regs, h);
    cq_ref_w got = cq_pc_value_w(ctx, h);

    if (!cq_ref_w_eq(got, want))
        cq_h_fail(__FILE__, __LINE__,
                  "%s W=%d: source %d changed value: 0x%llx%016llx -> "
                  "0x%llx%016llx", kname, W, idx,
                  (unsigned long long)want.hi, (unsigned long long)want.lo,
                  (unsigned long long)got.hi,  (unsigned long long)got.lo);

    if (!kinds) return;

    for (int i = 0; i < W; i++) {
        int want_q = cq_ref_w_bit(qmask, i);
        int is_q = cq_bit_is_qubit(bits[i]);

        if (want_q != is_q)
            cq_h_fail(__FILE__, __LINE__,
                      "%s W=%d: source %d bit %d is %s but the mask says %s — "
                      "a source must appear only as a control and must never "
                      "be materialised",
                      kname, W, idx, i, is_q ? "a qubit" : "classical",
                      want_q ? "a qubit" : "classical");
    }
}

static void call_kernel(const cq_kd_spec *k, cq_ctx *ctx, cq_bit *dst,
                        const cq_bit *const *src, const cq_kd_shape *sh)
{
    if (k->call) { k->call(ctx, dst, src, sh); return; }
    k->kernel(ctx, dst, src[0], src[1], sh->w_dst);
}

static cq_ref_w call_ref(const cq_kd_spec *k, const cq_ref_w *v,
                         const cq_kd_shape *sh)
{
    if (k->refn) return k->refn(v, sh);
    return cq_ref_w_make(k->ref(v[0].lo, v[1].lo, sh->w_dst), 0u, sh->w_dst);
}

void cq_kd_case(const cq_kd_spec *k, int W, const cq_ref_w *values,
                const cq_bk_pair *m)
{
    fixture f;
    cq_kd_shape sh;
    int32_t h[CQ_KD_MAX_SRC];
    const cq_bit *src[CQ_KD_MAX_SRC];
    cq_ref_w v[CQ_KD_MAX_SRC], qm[CQ_KD_MAX_SRC];
    int32_t hs[CQ_KD_MAX_SRC + 1];
    int classical = 1;

    /* Before fx_open, so a refused shape allocates nothing to leak. */
    if (!shape_of(k, W, &sh)) return;
    fx_open(&f);

    for (int i = 0; i < sh.n_src; i++) {
        /* The mask pair carries two entries; a third source reuses the first,
         * which is fine because K10's cond is one bit and its arms are the
         * pair. `classical[i]` is then subtracted, so a constrained operand
         * (K4's shift amount) is never handed a qubit. */
        cq_ref_w raw = m->q[i < 2 ? i : 0];
        cq_ref_w cls = cq_ref_w_make(sh.classical[i], sh.classical[i] ? ~0ull : 0ull,
                                     sh.w[i]);

        qm[i] = cq_ref_w_andnot(cq_ref_w_and(raw, cq_ref_w_ones(sh.w[i])), cls);
        v[i]  = cq_ref_w_make(values[i].lo, values[i].hi, sh.w[i]);
        if (!cq_ref_w_is_zero(qm[i])) classical = 0;

        h[i] = cq_bk_reg_w(&f.ctx, (uint32_t)sh.w[i], v[i], qm[i]);
    }

    /* dst is minted BEFORE the operand pointers are taken, mirroring the order
     * M26 will use (src/reg.h: "the shim mints dst before resolving a and b").
     * The bits arrays are separate allocations and stable for a register's
     * life, so the pointers survive later mints — but taking them in this
     * order means a future mistake there is caught here rather than in the
     * shim. */
    int32_t hd = cq_reg_alloc_zero(&f.ctx.regs, (uint32_t)sh.w_dst);
    cq_bit *dst = cq_reg_bits(&f.ctx.regs, hd);

    for (int i = 0; i < sh.n_src; i++) {
        src[i] = cq_reg_cbits(&f.ctx.regs, h[i]);
        hs[i] = h[i];
    }
    hs[sh.n_src] = hd;

    /* Everything up to here is scaffolding — materialising an operand bit that
     * held 1 emits an X. The counter is zeroed so L5 measures the KERNEL. */
    cq_pc_snap before = cq_pc_take(&f.ctx);
    cq_count_reset(&f.cnt);

    call_kernel(k, &f.ctx, dst, src, &sh);

    /* ---- L1: the value, against plain C. --------------------------------- */
    cq_ref_w want = call_ref(k, v, &sh);
    cq_ref_w got  = cq_pc_value_w(&f.ctx, hd);

    if (!cq_ref_w_eq(got, want))
        cq_h_fail(__FILE__, __LINE__,
                  "L1 %s W=%d [%s] a=0x%llx%016llx: dst = 0x%llx%016llx, want "
                  "0x%llx%016llx", k->name, W, m->name,
                  (unsigned long long)v[0].hi, (unsigned long long)v[0].lo,
                  (unsigned long long)got.hi,  (unsigned long long)got.lo,
                  (unsigned long long)want.hi, (unsigned long long)want.lo);

    for (int i = 0; i < sh.n_src; i++)
        check_source(&f.ctx, i, h[i], v[i], qm[i], sh.w[i], k->name, KINDS_TOO);

    /* ---- L2: no qubit is live that an operand or dst does not own. ------- */
    if (!cq_pc_live_is_exactly(&f.ctx, hs, (uint32_t)sh.n_src + 1u))
        cq_h_fail(__FILE__, __LINE__, "L2 %s W=%d [%s]: after the FORWARD call",
                  k->name, W, m->name);

    /* ---- L5: fully classical costs nothing at all. ----------------------- */
    if (classical) {
        cq_pc_snap now = cq_pc_take(&f.ctx);

        if (cq_count_total(&f.cnt) != 0u)
            cq_h_fail(__FILE__, __LINE__,
                      "L5 %s W=%d: all-classical operands emitted %llu gates "
                      "(x %llu, cx %llu, ccx %llu)", k->name, W,
                      (unsigned long long)cq_count_total(&f.cnt),
                      (unsigned long long)f.cnt.x, (unsigned long long)f.cnt.cx,
                      (unsigned long long)f.cnt.ccx);

        if (now.minted != before.minted || now.live != before.live)
            cq_h_fail(__FILE__, __LINE__,
                      "L5 %s W=%d: all-classical operands allocated qubits "
                      "(live %u -> %u, minted %u -> %u); I4 says an "
                      "all-constant register owns zero", k->name, W,
                      before.live, now.live, before.minted, now.minted);
    }

    /* ---- L3: uncompute IS the same kernel, then the free. ---------------- */
    call_kernel(k, &f.ctx, dst, src, &sh);

    cq_ref_w after_unc = cq_pc_value_w(&f.ctx, hd);
    if (!cq_ref_w_is_zero(after_unc))
        cq_h_fail(__FILE__, __LINE__,
                  "L3 %s W=%d [%s]: dst = 0x%llx%016llx after uncompute, want 0",
                  k->name, W, m->name, (unsigned long long)after_unc.hi,
                  (unsigned long long)after_unc.lo);

    /* VALUE ONLY — the kind claim may not cross the uncompute axis. */
    for (int i = 0; i < sh.n_src; i++)
        check_source(&f.ctx, i, h[i], v[i], qm[i], sh.w[i], k->name, VALUE_ONLY);

    /* NOT REDUNDANT — it closes a hole MEASURED to pass the whole suite green
     * in both configurations. With the set check only after the forward, a
     * kernel that acquires one ancilla and releases one qubit belonging to a
     * SOURCE nets to zero: `live` matches, every value is right, and the run is
     * green — while the source register names an index on the free list and an
     * unowned ancilla is live. I2 and I3 are both lies at that point. */
    if (!cq_pc_live_is_exactly(&f.ctx, hs, (uint32_t)sh.n_src + 1u))
        cq_h_fail(__FILE__, __LINE__, "L2 %s W=%d [%s]: after the UNCOMPUTE",
                  k->name, W, m->name);

    /* _unc reclaims nothing (PRD §10), so the free is a required third step
     * and not a tidy-up. Name dst's indices first — after the free the rail is
     * a tombstone and they are unrecoverable. */
    uint32_t held[128];
    uint32_t n_held = cq_pc_indices(&f.ctx, hd, held, 128u);

    cq_reg_free(&f.ctx, hd, cq_pc_zero_proof_rotation_free);

    cq_pc_snap after = cq_pc_take(&f.ctx);
    if (!cq_pc_same(before, after))
        cq_h_fail(__FILE__, __LINE__,
                  "L3 %s W=%d [%s]: pool not restored — live %u -> %u",
                  k->name, W, m->name, before.live, after.live);

    if (!cq_pc_indices_are_free(&f.ctx, held, n_held))
        cq_h_fail(__FILE__, __LINE__, "L3 %s W=%d [%s]: see above",
                  k->name, W, m->name);

    if (!cq_pc_live_is_exactly(&f.ctx, hs, (uint32_t)sh.n_src))
        cq_h_fail(__FILE__, __LINE__, "L2 %s W=%d [%s]: after the FREE",
                  k->name, W, m->name);

    fx_close(&f);
}

void cq_kd_case2(const cq_kd_spec *k, int W, uint64_t va, uint64_t vb,
                 const cq_bk_pair *m)
{
    cq_ref_w v[CQ_KD_MAX_SRC];

    v[0] = cq_ref_w_make(va, 0u, W);
    v[1] = cq_ref_w_make(vb, 0u, W);
    v[2] = cq_ref_w_zero();
    cq_kd_case(k, W, v, m);
}

/* --- L4's measurement, and the peak. ------------------------------------- */

/* Both build the same all-quantum fixture, so it lives once. Returns dst's
 * handle, or -1 for a shape this driver refuses — in which case the fixture was
 * never opened and the caller must not close it. */
static int32_t measure_setup(fixture *f, const cq_kd_spec *k, int W,
                             cq_kd_shape *sh, cq_bit **dst,
                             const cq_bit **src)
{
    if (!shape_of(k, W, sh)) return -1;
    fx_open(f);

    int32_t h[CQ_KD_MAX_SRC];

    for (int i = 0; i < sh->n_src; i++) {
        cq_ref_w all = cq_ref_w_ones(sh->w[i]);
        cq_ref_w cls = cq_ref_w_make(sh->classical[i],
                                     sh->classical[i] ? ~0ull : 0ull, sh->w[i]);
        /* Values all-ones so no lane can be quiet, masks all-quantum except
         * where the shape forbids it. */
        h[i] = cq_bk_reg_w(&f->ctx, (uint32_t)sh->w[i], all,
                           cq_ref_w_andnot(all, cls));
    }

    int32_t hd = cq_reg_alloc_zero(&f->ctx.regs, (uint32_t)sh->w_dst);
    *dst = cq_reg_bits(&f->ctx.regs, hd);
    for (int i = 0; i < sh->n_src; i++)
        src[i] = cq_reg_cbits(&f->ctx.regs, h[i]);

    return hd;
}

void cq_kd_measure(const cq_kd_spec *k, int W, cq_counter *forward,
                   cq_counter *unc)
{
    fixture f;
    cq_kd_shape sh;
    cq_bit *dst;
    const cq_bit *src[CQ_KD_MAX_SRC];
    int32_t hd = measure_setup(&f, k, W, &sh, &dst, src);

    if (hd < 0) { cq_count_reset(forward); cq_count_reset(unc); return; }

    cq_count_reset(&f.cnt);
    call_kernel(k, &f.ctx, dst, src, &sh);
    *forward = f.cnt;

    cq_count_reset(&f.cnt);
    call_kernel(k, &f.ctx, dst, src, &sh);
    *unc = f.cnt;

    cq_reg_free(&f.ctx, hd, cq_pc_zero_proof_rotation_free);
    fx_close(&f);
}

uint32_t cq_kd_peak(const cq_kd_spec *k, int W, uint32_t *peak_delta)
{
    fixture f;
    cq_kd_shape sh;
    cq_bit *dst;
    const cq_bit *src[CQ_KD_MAX_SRC];
    int32_t hd = measure_setup(&f, k, W, &sh, &dst, src);

    if (hd < 0) { *peak_delta = 0u; return 0u; }

    cq_pc_snap before = cq_pc_take(&f.ctx);
    call_kernel(k, &f.ctx, dst, src, &sh);
    cq_pc_snap after = cq_pc_take(&f.ctx);

    /* peak == minted (src/qubits.h), so the high-water mark DURING the call is
     * exactly `minted` after it — which is what makes a transient scratch
     * allocation visible even though it was tidily released. L2 looks after
     * the call and cannot see that at all. */
    *peak_delta = after.peak - before.peak;
    uint32_t owned = cq_reg_owned_qubits(&f.ctx.regs, hd);

    fx_close(&f);
    return owned;
}
