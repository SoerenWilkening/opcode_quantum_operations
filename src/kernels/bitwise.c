/* BACKEND_ABI §2.3 and, or, xor: per bit, straight onto o, no ancilla (ABI §2.3 Impl.).
 * A literal operand is a classical rail, so the emitter folds it to the ABI's _hl circuits:
 * and is CX at the literal's 1-bits; xor CX everywhere and X at its 1-bits; or (below) X at
 * its 1-bits and CX at its 0-bits. */
#include "cq_rt.h"
#include "kernels.h"

/* Depth 1: one CCX per bit. */
void cq_k_and(int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o), *a = cq_rail_bits(args[0]), *b = cq_rail_bits(args[1]);
    for (uint32_t i = 0; i < w; i++) cq_ccx(&a[i], &b[i], &ob[i]);
}

/* Depth 3: a ^ b ^ ab = a | b. A classical 1 on either side is X(o): without the check the
 * fold would give CX, X, CX, three gates for one. */
void cq_k_or(int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o), *a = cq_rail_bits(args[0]), *b = cq_rail_bits(args[1]);
    for (uint32_t i = 0; i < w; i++) {
        if (a[i].kind == CQ_BIT_ONE || b[i].kind == CQ_BIT_ONE) { cq_x(&ob[i]); continue; }
        cq_cx(&a[i], &ob[i]);
        cq_cx(&b[i], &ob[i]);
        cq_ccx(&a[i], &b[i], &ob[i]);
    }
}

/* Depth 2: two CX per bit. */
void cq_k_xor(int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o), *a = cq_rail_bits(args[0]), *b = cq_rail_bits(args[1]);
    for (uint32_t i = 0; i < w; i++) { cq_cx(&a[i], &ob[i]); cq_cx(&b[i], &ob[i]); }
}
