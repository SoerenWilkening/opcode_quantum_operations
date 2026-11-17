/* k(a) on the host, the integer pipeline of docs/ANGLE_LADDER.md §5: window of 1/pi selected by
 * the exponent, low CQ_ANGLE_WBITS bits of m * window, N bits plus the round bit, negate. */
#include "angle_k.h"

/* python3 tests/ref/k_ref.py --inv-pi 1216, split into limbs, low first. Its top 1139 bits are
 * the note's C = floor(2^1140 / pi); tests/test_rot.c checks k against tests/ref/k_vectors.txt. */
const uint64_t cq_inv_pi[19] = {
    0x2b0198237e3db5d5ull, 0x35fdafd88fc6ae84ull, 0x9e839cfbc5294975ull,
    0xba93dd63f5f2f8bdull, 0xa7a31fb34f2ff516ull, 0xb69b3f6793e584dbull,
    0xf79788c5ad05368full, 0x8ffc4bffef02cc07ull, 0x4e422fc5defc941dull,
    0x9cc8eb1cc1a99cfaull, 0x74ce38135a2fbf20ull, 0x74411afa975da242ull,
    0x7f0ef58e5894d39full, 0x0324977504e8c90eull, 0xdb92371d2126e970ull,
    0xff28b1d5ef5de2b0ull, 0x6db14acc9e21c820ull, 0xfe13abe8fa9a6ee0ull,
    0x517cc1b727220a94ull,
};

/* Bits b .. b+63 of the n-limb integer x (b >= 0), 0 beyond it. */
static uint64_t chunk(const uint64_t *x, int n, int b) {
    int q = b / 64, r = b % 64;
    uint64_t lo = q < n ? x[q] >> r : 0;
    uint64_t hi = r && q + 1 < n ? x[q + 1] << (64 - r) : 0;
    return lo | hi;
}

static void mask(uint64_t v[4]) {
    for (int i = 0; i < 4; i++) {
        int keep = CQ_ANGLE_WBITS - 64 * i;
        if (keep <= 0) v[i] = 0;
        else if (keep < 64) v[i] &= (1ull << keep) - 1;
    }
}

void cq_angle_window(uint32_t e1, int s, uint64_t w[4]) {
    int t = (int)e1 - 1075 + CQ_LADDER_N - s;
    int shift = CQ_INV_PI_J - t - CQ_ANGLE_L;   /* >= 130 - N > 0 for every finite exponent */
    for (int i = 0; i < 4; i++) w[i] = chunk(cq_inv_pi, 19, shift + 64 * i);
    mask(w);
}

unsigned __int128 cq_angle_k(uint64_t a, int s) {
    uint32_t e = (uint32_t)(a >> 52) & 0x7FF;
    if (e == 0x7FF) return 0;
    uint64_t m = (a & ((1ull << 52) - 1)) | (uint64_t)(e != 0) << 52, w[4], p[4];
    cq_angle_window(e ? e : 1, s, w);           /* a subnormal has the exponent of e = 1 */
    unsigned __int128 carry = 0;
    for (int i = 0; i < 4; i++) {
        carry += (unsigned __int128)m * w[i];
        p[i] = (uint64_t)carry;
        carry >>= 64;
    }
    mask(p);
    const int L = CQ_ANGLE_L;
    unsigned __int128 k = chunk(p, 4, L) | (unsigned __int128)chunk(p, 4, L + 64) << 64;
    k += (p[(L - 1) / 64] >> ((L - 1) % 64)) & 1;   /* round to nearest: P irrational, no tie */
    if (a >> 63) k = -k;
    return CQ_LADDER_N == 128 ? k : k & (((unsigned __int128)1 << (CQ_LADDER_N % 128)) - 1);
}
