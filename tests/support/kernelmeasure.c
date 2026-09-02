/* tests/support/kernelmeasure.c — L4's instrument, and the peak.
 *
 * Split from kerneldrv.c 2026-09-02 (bd f8c) on the third seam plan §2.2
 * records for the driver: the per-case GATE (cq_kd_case — L1/L2/L3/L5, every
 * line of it an assertion) against the INSTRUMENT (this file — cq_kd_measure
 * and cq_kd_peak, which assert NOTHING). A reading taken here is pinned
 * somewhere else: the (x, cx, ccx) tuples by the L4 goldens, the peak by each
 * suite's zero-ancilla claim, the controlled tuple by kernelctrl.c's promotion
 * identity. That is what makes it a subject cut rather than a size cut — the
 * file kerneldrv.c's own header describes as "the four LEVELS" never held L4,
 * only L4's thermometer.
 *
 * The fixture and the one push site both halves share are declared in
 * kernelfix.h and defined in kerneldrv.c.
 */

#include "support/kernelfix.h"

#include "support/kernelctrl.h"
#include "support/poolcheck.h"

/* Both build the same all-quantum fixture, so it lives once. Returns dst's
 * handle, or -1 for a shape this driver refuses — in which case the fixture was
 * never opened and the caller must not close it. */
static int32_t measure_setup(cq_kd_fixture *f, const cq_kd_spec *k, int W,
                             cq_kd_shape *sh, cq_bit **dst,
                             const cq_bit **src)
{
    if (!cq_kd_shape_of(k, W, sh)) return -1;
    cq_kd_fx_open(f);

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
    cq_kd_ctrl_rail(&f->ctx);
    for (int i = 0; i < sh->n_src; i++)
        src[i] = cq_reg_cbits(&f->ctx.regs, h[i]);

    return hd;
}

void cq_kd_measure(const cq_kd_spec *k, int W, cq_counter *forward,
                   cq_counter *unc)
{
    cq_kd_fixture f;
    cq_kd_shape sh;
    cq_bit *dst;
    const cq_bit *src[CQ_KD_MAX_SRC];
    int32_t hd = measure_setup(&f, k, W, &sh, &dst, src);

    if (hd < 0) { cq_count_reset(forward); cq_count_reset(unc); return; }

    cq_count_reset(&f.cnt);
    cq_kd_call_kernel(k, &f.ctx, dst, src, &sh);
    *forward = f.cnt;

    cq_count_reset(&f.cnt);
    cq_kd_call_kernel(k, &f.ctx, dst, src, &sh);
    *unc = f.cnt;

    cq_reg_free(&f.ctx, hd, cq_pc_zero_proof_rotation_free);
    cq_kd_fx_close(&f);
}

uint32_t cq_kd_peak(const cq_kd_spec *k, int W, uint32_t *peak_delta)
{
    cq_kd_fixture f;
    cq_kd_shape sh;
    cq_bit *dst;
    const cq_bit *src[CQ_KD_MAX_SRC];
    int32_t hd = measure_setup(&f, k, W, &sh, &dst, src);

    if (hd < 0) { *peak_delta = 0u; return 0u; }

    cq_pc_snap before = cq_pc_take(&f.ctx);
    cq_kd_call_kernel(k, &f.ctx, dst, src, &sh);
    cq_pc_snap after = cq_pc_take(&f.ctx);

    /* peak == minted (src/qubits.h), so the high-water mark DURING the call is
     * exactly `minted` after it — which is what makes a transient scratch
     * allocation visible even though it was tidily released. L2 looks after
     * the call and cannot see that at all. */
    *peak_delta = after.peak - before.peak;
    uint32_t owned = cq_reg_owned_qubits(&f.ctx.regs, hd);

    cq_kd_fx_close(&f);
    return owned;
}
