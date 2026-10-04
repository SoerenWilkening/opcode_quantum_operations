/* tests/test_kerneldrv_shard.inc — bd 5mqp. `--shard I/K` partitions the
 * unsharded sample set: the same samples, each exactly once, in the same order.
 *
 * Included from tests/test_kerneldrv.c for the reason the other two .inc files
 * are: that file is at Rule 12's edge, and an .inc is exempt by design.
 *
 * THE FAULT IT IS FOR. cq_kd_sample_at's shard check skips the CIRCUIT and
 * must never skip the DRAW: every slot consumes the value RNG, and a random
 * slot of a three-source kernel the mask RNG too, so a skip placed before the
 * draws re-seeds the rest of the run by omission. Each shard would then run a
 * different, equally plausible sample set, every entry would be green, and the
 * union would cover samples the unsharded run never drew while dropping ones
 * it did — the fma split's whole equivalence claim, gone without a sound. A
 * count cannot see it (each shard still runs ~N/K cases) and neither can a
 * seed (it is printed unchanged); only a sample-by-sample identity can.
 *
 * So this logs every case's IDENTITY — the three operand masks as the kernel
 * sees them, and the three values the reference is handed — once unsharded and
 * once per shard, and requires shard I's j-th case to BE unsharded case
 * I + j*K. Three sources and a two-row anchor provider, so the third source's
 * mask stream (bd 9ve.40) and the anchor block (bd hkg) are both on the path.
 */

#include <stdio.h>

enum { SHARD_W = 8, SHARD_LOG_MAX = 256 };
/* f[0..2] the three masks, f[3..5] the three values. One type, no padding. */
typedef struct { uint64_t f[6]; } shard_id;
static shard_id shard_log[SHARD_LOG_MAX];
static int shard_calls, shard_refs;

static void shard_call(cq_ctx *ctx, cq_bit *dst, const cq_bit *const *src,
                       const cq_kd_shape *sh)
{
    /* Two calls per case, forward then uncompute; the forward's kinds are
     * the case's masks. */
    if ((shard_calls & 1) == 0 && shard_calls / 2 < SHARD_LOG_MAX)
        for (int i = 0; i < 3; i++)
            shard_log[shard_calls / 2].f[i] = third_qmask(src[i], sh->w[i]);
    shard_calls++;
    cq_kernel_xor(ctx, dst, src[0], src[1], sh->w[0]);
}

static cq_ref_w shard_ref(const cq_ref_w *s, const cq_kd_shape *sh)
{
    if (shard_refs < SHARD_LOG_MAX)
        for (int i = 0; i < 3; i++) shard_log[shard_refs].f[3 + i] = s[i].lo;
    shard_refs++;
    return cq_ref_w_make(cq_ref_xor(s[0].lo, s[1].lo, sh->w[0]), 0u,
                         sh->w_dst);
}

/* Names a and b and leaves c drawn, as the anchor contract requires. */
static int shard_anchors(int W, int i, cq_ref_w *v)
{
    if (i < 0) return 2;
    if (i >= 2 || v == NULL) return 0;
    v[0] = cq_ref_w_make((uint64_t)(0x11 * (i + 1)), 0u, W);
    v[1] = cq_ref_w_make((uint64_t)(0x5a + i), 0u, W);
    return 1;
}

static void shard_shape(int W, cq_kd_shape *out)
{
    cq_kd_default_shape(W, out);
    out->n_src   = 3;
    out->anchors = shard_anchors;
}

/* Installs a shard through the public parse, as `--shard` on argv would. */
static void shard_install(const char *spec)
{
    const cq_test_case one[] = { { "x", NULL } };
    char *argv[] = { "t", "--shard", (char *)spec, NULL };
    unsigned char run[1];

    CHECK_EQ(cq_h_select(one, 1, spec ? 3 : 1, argv, run), 0);
}

static int shard_run(const cq_kd_spec *k, shard_id *out, int cap)
{
    shard_calls = shard_refs = 0;
    CQ_EXPECT_CLEAN("a correct three-source kernel, sharded or not",
                    cq_kd_sample_at(k, SHARD_W));
    CHECK_EQ(shard_calls, 2 * shard_refs);
    for (int j = 0; j < shard_refs && j < cap && j < SHARD_LOG_MAX; j++)
        out[j] = shard_log[j];
    return shard_refs;
}

CQ_TEST(the_shards_partition_the_unsharded_samples_one_for_one)
{
    const cq_kd_spec k = { "shard-partition", NULL, NULL, shard_shape,
                           shard_call, shard_ref };
    static shard_id all[SHARD_LOG_MAX], part[SHARD_LOG_MAX];
    char spec[16];
    const int n = cq_kd_samples();
    int ran;

    CHECK(n <= SHARD_LOG_MAX);
    shard_install(NULL);
    CHECK_EQ(shard_run(&k, all, SHARD_LOG_MAX), n);

    for (int kk = 2; kk <= 5; kk++) {
        int total = 0;

        for (int i = 0; i < kk; i++) {
            snprintf(spec, sizeof spec, "%d/%d", i, kk);
            shard_install(spec);
            ran = shard_run(&k, part, SHARD_LOG_MAX);
            total += ran;
            CHECK_EQ(ran, (n - i + kk - 1) / kk);  /* #{ s < n : s % kk == i } */
            for (int j = 0; j < ran && i + j * kk < n; j++)
                for (int f = 0; f < 6; f++)
                    if (part[j].f[f] != all[i + j * kk].f[f])
                        cq_h_fail(__FILE__, __LINE__,
                                  "shard %s case %d is not unsharded sample "
                                  "%d: field %d is 0x%llx, want 0x%llx", spec,
                                  j, i + j * kk, f,
                                  (unsigned long long)part[j].f[f],
                                  (unsigned long long)all[i + j * kk].f[f]);
        }
        CHECK_EQ(total, n);
    }

    shard_install(NULL);          /* nothing after this case runs sharded */
    CHECK_EQ(cq_h_shard(NULL), 1);
}
