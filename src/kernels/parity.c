/* Demo operation of the depth tooling (kmgy.9): o[0] ^= parity of a. Not an ABI template. */
#include "cq_rt.h"
#include "kernels.h"

/* Depth w, w gates, no ancilla. */
void cq_k_parity_chain(int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o), *a = cq_rail_bits(args[0]);
    for (uint32_t i = 0; i < w; i++) cq_cx(&a[i], &ob[0]);
}

/* XOR-reduce a into a[0] by a CX tree, CX a[0] out, undo the tree:
 * depth 2*ceil(log2 w) + 1, 2w - 1 gates, no ancilla. a ends with its value.
 * shortcut: with a mixed classical/quantum operand a classical bit that receives a CX
 * stays a qubit (value kept); use an out-of-place tree if that ever matters. */
void cq_k_parity_tree(int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o), *a = cq_rail_bits(args[0]);
    uint32_t top = 1;
    while (top < w) top *= 2;
    for (uint32_t s = 1; s < w; s *= 2)
        for (uint32_t i = 0; i + s < w; i += 2 * s) cq_cx(&a[i + s], &a[i]);
    cq_cx(&a[0], &ob[0]);
    for (uint32_t s = top / 2; s > 0; s /= 2)
        for (uint32_t i = 0; i + s < w; i += 2 * s) cq_cx(&a[i + s], &a[i]);
}
