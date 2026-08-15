/* tests/support/poolcheck.c — plan §2.2. L2 and L3, asserted on the pool. */

#include "poolcheck.h"

#include "bit.h"
#include "harness.h"
#include "qubits.h"
#include "reg.h"
#include "shadow.h"
#include "support/refmodel.h"

#include <stdlib.h>

cq_pc_snap cq_pc_take(const cq_ctx *ctx)
{
    cq_pc_snap s;
    s.live   = cq_qubits_live(&ctx->pool);
    s.minted = cq_qubits_minted(&ctx->pool);
    s.n_free = cq_qubits_free(&ctx->pool);
    s.peak   = cq_qubits_peak(&ctx->pool);
    return s;
}

int cq_pc_same(cq_pc_snap a, cq_pc_snap b)
{
    return a.live == b.live;   /* see the header: the rest are monotone */
}

uint32_t cq_pc_indices(const cq_ctx *ctx, int32_t h, uint32_t *out, uint32_t cap)
{
    const cq_bit *bits = cq_reg_cbits(&ctx->regs, h);
    uint32_t W = cq_reg_width(&ctx->regs, h);
    uint32_t n = 0;

    for (uint32_t i = 0; i < W; i++) {
        if (!cq_bit_is_qubit(bits[i])) continue;
        if (n >= cap) {
            cq_h_fail(__FILE__, __LINE__,
                      "cq_pc_indices: buffer of %u too small for h%d",
                      cap, (int)h);
            abort();
        }
        out[n++] = cq_bit_qindex(bits[i]);
    }
    return n;
}

int cq_pc_indices_are_free(const cq_ctx *ctx, const uint32_t *idx, uint32_t n)
{
    int ok = 1;

    for (uint32_t i = 0; i < n; i++)
        if (!cq_qubits_is_free(&ctx->pool, idx[i])) {
            cq_h_fail(__FILE__, __LINE__,
                      "L3: q%u was freed but is not on the free list — the "
                      "pool did not get that index back", idx[i]);
            ok = 0;
        }

    return ok;
}

cq_ref_w cq_pc_value_w(const cq_ctx *ctx, int32_t h)
{
    const cq_bit *bits = cq_reg_cbits(&ctx->regs, h);
    uint32_t W = cq_reg_width(&ctx->regs, h);
    cq_ref_w v = cq_ref_w_zero();

    for (uint32_t i = 0; i < W; i++) {
        int one;

        if (cq_bit_is_const(bits[i])) {
            one = cq_bit_value(bits[i]);
        } else {
            uint32_t q = cq_bit_qindex(bits[i]);
            cq_shadow sh = cq_shadow_get(&ctx->shadow, q);

            if (sh.unknown) {
                cq_h_fail(__FILE__, __LINE__,
                          "cq_pc_value: h%d bit %u is on qubit q%u with an "
                          "UNKNOWN shadow — nothing on the rotation-free "
                          "surface can poison, so this is a real finding",
                          (int)h, i, q);
                one = 0;
            } else {
                one = sh.value != 0;
            }
        }

        if (one) v = cq_ref_w_or(v, cq_ref_w_setbit((int)i));
    }

    return v;
}

uint64_t cq_pc_value(const cq_ctx *ctx, int32_t h)
{
    cq_ref_w v = cq_pc_value_w(ctx, h);

    if (v.hi != 0u)
        cq_h_fail(__FILE__, __LINE__,
                  "cq_pc_value: h%d has bits above 64; use cq_pc_value_w",
                  (int)h);
    return v.lo;
}

int cq_pc_live_is_exactly(const cq_ctx *ctx, const int32_t *hs, uint32_t n)
{
    uint32_t minted = cq_qubits_minted(&ctx->pool);
    uint8_t *owned;
    int ok = 1;

    if (minted == 0u) {
        /* Nothing minted: the claim is that nothing is owned either. */
        for (uint32_t k = 0; k < n; k++)
            if (cq_reg_owned_qubits(&ctx->regs, hs[k]) != 0u) {
                cq_h_fail(__FILE__, __LINE__,
                          "cq_pc_live_is_exactly: h%d owns qubits but the pool "
                          "has minted none", (int)hs[k]);
                ok = 0;
            }
        return ok;
    }

    owned = calloc(minted, 1);
    if (!owned) { cq_h_fail(__FILE__, __LINE__, "poolcheck: out of memory"); return 0; }

    for (uint32_t k = 0; k < n; k++) {
        const cq_bit *bits = cq_reg_cbits(&ctx->regs, hs[k]);
        uint32_t W = cq_reg_width(&ctx->regs, hs[k]);

        for (uint32_t i = 0; i < W; i++) {
            if (!cq_bit_is_qubit(bits[i])) continue;
            uint32_t q = cq_bit_qindex(bits[i]);

            if (q >= minted) {
                cq_h_fail(__FILE__, __LINE__,
                          "cq_pc_live_is_exactly: h%d bit %u holds q%u, which "
                          "was never minted", (int)hs[k], i, q);
                ok = 0;
                continue;
            }

            /* I2 from the other side: an index in two named registers, or
             * twice in one, double-releases at free. cq_reg_audit sweeps for
             * this too, but only in Debug — this runs in both. */
            if (owned[q]) {
                cq_h_fail(__FILE__, __LINE__,
                          "cq_pc_live_is_exactly: q%u is owned twice (I2); "
                          "second sighting in h%d bit %u", q, (int)hs[k], i);
                ok = 0;
            }
            owned[q] = 1;
        }
    }

    for (uint32_t q = 0; q < minted; q++) {
        int live = !cq_qubits_is_free(&ctx->pool, q);

        if (live && !owned[q]) {
            cq_h_fail(__FILE__, __LINE__,
                      "L2: q%u is LIVE but no named register owns it — a "
                      "leaked ancilla", q);
            ok = 0;
        } else if (!live && owned[q]) {
            cq_h_fail(__FILE__, __LINE__,
                      "L2: q%u is owned by a live register but sits on the "
                      "free list (I3 is now a lie about it)", q);
            ok = 0;
        }
    }

    free(owned);
    return ok;
}

int cq_pc_zero_proof_rotation_free(const cq_ctx *ctx, int32_t h, uint32_t q)
{
    (void)h;   /* per-qubit evidence; the rail plays no part. See the header. */
    return cq_shadow_known_zero(&ctx->shadow, q);
}
