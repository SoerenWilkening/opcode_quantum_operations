/* tests/test_kerneldrv_third.inc — bd 9ve.40. The THIRD source's mask is drawn,
 * not inherited.
 *
 * Included from tests/test_kerneldrv.c for the reason test_kerneldrv_anchors.inc
 * is: that file is at Rule 12's edge, and an .inc is exempt by design.
 *
 * THE FAULT. cq_kd_case used to build source 2 from `m->q[i < 2 ? i : 0]`, so
 * a three-source kernel's `c` wore operand 0's quantum mask in EVERY sampled
 * case. fma (M39) therefore never saw `c` classical against a quantum `a`
 * outside its hand-driven cases — a whole row of the fold table, green by
 * omission. Found reviewing 9ve.24.
 *
 * The one case below goes red under the old line and under a sampler that
 * stops passing the drawn mask down; it reads the operands' KINDS as the kernel
 * sees them, which is the only place the difference exists.
 */

enum { THIRD_W = 8, THIRD_LOG_MAX = 512 };
static struct { uint32_t qa, qc; } third_log[THIRD_LOG_MAX];
static int third_n;

static uint32_t third_qmask(const cq_bit *p, int W)
{
    uint32_t m = 0u;

    for (int i = 0; i < W && i < 32; i++)
        if (!cq_bit_is_const(p[i])) m |= 1u << i;
    return m;
}

/* Logs the kinds, then computes a ^ b — correct for the ref below whatever
 * `c` holds, so the only thing this case can see is the mask schedule. */
static void third_call(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                       const cq_kd_shape *sh)
{
    if (third_n < THIRD_LOG_MAX) {
        third_log[third_n].qa = third_qmask(src[0], sh->w[0]);
        third_log[third_n].qc = third_qmask(src[2], sh->w[2]);
    }
    third_n++;
    cq_kernel_xor(ctx, dst, src[0], src[1], sh->w[0]);
}

static cq_ref_w third_ref(const cq_ref_w *s, const cq_kd_shape *sh)
{
    return cq_ref_w_make(cq_ref_xor(s[0].lo, s[1].lo, sh->w[0]), 0u,
                         sh->w_dst);
}

static void third_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->n_src = 3;
}

CQ_TEST(a_third_source_draws_its_own_mask)
{
    const cq_kd_spec k = { "third-mask", NULL, NULL, third_shape, third_call,
                           third_ref };
    const uint32_t ones = (1u << THIRD_W) - 1u;
    int differs = 0, c_classical_a_quantum = 0;

    CHECK(cq_kd_samples() >= 8);   /* slots 0, 1 and some random draws */

    third_n = 0;
    CQ_EXPECT_CLEAN("a correct three-source kernel",
                    cq_kd_sample_at(&k, THIRD_W));
    CHECK_EQ(third_n, 2 * cq_kd_samples());

    /* The forced slots keep the anchor contract: slot 0 is all-classical in
     * EVERY source (it IS L5) and slot 1 all-quantum in every source (L4's
     * mask). Two calls per case, forward then uncompute. */
    CHECK_EQ(third_log[0].qc, 0u);
    CHECK_EQ(third_log[2].qa, ones);
    CHECK_EQ(third_log[2].qc, ones);

    for (int j = 0; j < third_n && j < THIRD_LOG_MAX; j++) {
        if (third_log[j].qa != third_log[j].qc) differs++;
        if (third_log[j].qc == 0u && third_log[j].qa != 0u)
            c_classical_a_quantum++;
    }

    /* THE CLAIM. Measured once at the fixed seed this name and width give —
     * deterministic, not flaky. Under the old `q[i < 2 ? i : 0]` both
     * counters are 0. */
    CHECK(differs > 0);
    CHECK(c_classical_a_quantum > 0);

    /* And a HAND-DRIVEN three-source case still means what it meant: no drawn
     * mask, so `c` follows operand 0 — the promise cq_kd_case makes to fma's
     * and mux's own cases. */
    {
        cq_ref_w v[CQ_KD_MAX_SRC] = { cq_ref_w_make(1u, 0u, THIRD_W),
                                      cq_ref_w_make(2u, 0u, THIRD_W),
                                      cq_ref_w_make(3u, 0u, THIRD_W) };

        third_n = 0;
        CQ_EXPECT_CLEAN("hand-driven three-source case",
                        cq_kd_case(&k, THIRD_W, v, &QC));
        CHECK_EQ(third_n, 2);
        CHECK_EQ(third_log[0].qc, third_log[0].qa);
    }
}
