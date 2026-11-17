/* BACKEND_ABI §2.8 bswap and bitreverse: wire permutations, so o ^= f(a) is one CX per bit
 * onto disjoint targets. Depth 1, W gates, no ancilla; a classical a emits no gate. */
#include "cq_rt.h"
#include "kernels.h"

/* o_(8j+r) ^= a_(8(n-1-j)+r), n = w/8 bytes; the op is registered for w a multiple of 16. */
void cq_k_bswap(int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o), *a = cq_rail_bits(args[0]);
    for (uint32_t i = 0; i < w; i++) cq_cx(&a[w - 8 - (i & ~7u) + (i & 7u)], &ob[i]);
}

/* o_i ^= a_(w-1-i) */
void cq_k_bitreverse(int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o), *a = cq_rail_bits(args[0]);
    for (uint32_t i = 0; i < w; i++) cq_cx(&a[w - 1 - i], &ob[i]);
}
