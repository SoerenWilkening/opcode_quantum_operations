/* BACKEND_ABI §2.6 integer casts: CX wiring onto o. The registry key is the RESULT width w
 * (= TO); the operand's width FROM is that of its rail. */
#include "cq_rt.h"
#include "kernels.h"

/* zext and trunc: o_i ^= a_i for i < min(FROM, TO): depth 1, min gates, no ancilla. */
void cq_k_cast_copy(int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o), *a = cq_rail_bits(args[0]);
    uint32_t from = cq_rail_width(args[0]);
    for (uint32_t i = 0; i < (from < w ? from : w); i++) cq_cx(&a[i], &ob[i]);
}

/* sext, chain: the low bits as zext, then the sign bit a_(FROM-1) into every o_j, j >= FROM-1
 * (N = TO-FROM+1 targets, one after the other): depth N, no ancilla. */
void cq_k_sext_chain(int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o), *a = cq_rail_bits(args[0]);
    uint32_t from = cq_rail_width(args[0]);
    for (uint32_t i = 0; i + 1 < from; i++) cq_cx(&a[i], &ob[i]);
    for (uint32_t j = from - 1; j < w; j++) cq_cx(&a[from - 1], &ob[j]);
}

/* sext, fan-out tree: o holds arbitrary bits on entry (o ^= f), so the written result bits
 * cannot serve as controls. Instead copy the sign s into c-1 ancillas by doubling, let each of
 * the c copies drive about N/c targets, undo the doubling: depth 2*ceil(log2 c) + ceil(N/c),
 * c chosen to minimise it (c = 1 is the chain). A classical sign needs no copies. */
void cq_k_sext_tree(int32_t o, const int32_t *args, uint32_t w) {
    cq_bit *ob = cq_rail_bits(o), *a = cq_rail_bits(args[0]);
    uint32_t from = cq_rail_width(args[0]), n = w - from + 1, c = 1, best = n;
    for (uint32_t cc = 2; cc <= n && cq_bit_is_q(a[from - 1]); cc++) {
        uint32_t lg = 0;
        while ((1u << lg) < cc) lg++;
        uint32_t d = 2 * lg + (n + cc - 1) / cc;
        if (d < best) { best = d; c = cc; }
    }
    for (uint32_t i = 0; i + 1 < from; i++) cq_cx(&a[i], &ob[i]);
    cq_bit anc[CQ_RAIL_MAX_WIDTH], *ctl[CQ_RAIL_MAX_WIDTH];
    uint32_t round[32], nr = 0, have = 1;
    ctl[0] = &a[from - 1];
    while (have < c) {
        uint32_t m = c - have < have ? c - have : have;
        for (uint32_t i = 0; i < m; i++) {
            anc[have + i] = cq_bit_const(0);
            cq_cx(ctl[i], &anc[have + i]);
            ctl[have + i] = &anc[have + i];
        }
        round[nr++] = m;
        have += m;
    }
    for (uint32_t t = 0; t < n; t++) cq_cx(ctl[t % c], &ob[from - 1 + t]);
    while (nr) {
        uint32_t m = round[--nr];
        have -= m;
        for (uint32_t i = 0; i < m; i++) { cq_cx(ctl[i], &anc[have + i]); cq_pool_give(anc[have + i].q); }
    }
}
