/* The kernel registry: the one place a candidate construction is added. See kernels.h. */
#include <stddef.h>
#include "kernels.h"

#define CANDS(list) list, (int)(sizeof list / sizeof list[0])

static const cq_kernel parity[] = {
    { "chain", cq_k_parity_chain, NULL,
      "CNOT chain, one CX per bit into the target: the textbook parity check "
      "(Nielsen & Chuang, Quantum Computation and Quantum Information, 2000); no page checked" },
    { "tree_inplace", cq_k_parity_tree, NULL,
      "CX tree reducing the operand in place, then undone: parity in O(log n) depth "
      "(Moore & Nilsson, Parallel quantum computation and quantum codes, SIAM J. Comput. 31 (2001), "
      "arXiv:quant-ph/9808027); exists only, not read" },
};

#define WIRING "BACKEND_ABI §2.8 Impl. CX wiring: one CX per bit onto disjoint targets, depth 1, no ancilla. " \
    "No shallower alternative exists: every result bit depends on an operand bit, so depth 0 is impossible"
static const cq_kernel bswap[] = { { "wiring", cq_k_bswap, NULL, WIRING } };
static const cq_kernel bitreverse[] = { { "wiring", cq_k_bitreverse, NULL, WIRING } };

#define CAST_WIRING "BACKEND_ABI §2.6 Impl. (BJ lower_cast!): CX wiring onto o, one CX per copied bit, depth 1, no ancilla. " \
    "No shallower alternative exists: every copied result bit depends on an operand bit"
static const cq_kernel zext[] = { { "wiring", cq_k_cast_copy, NULL, CAST_WIRING } };
static const cq_kernel trunc_[] = { { "wiring", cq_k_cast_copy, NULL, CAST_WIRING } };
static const cq_kernel sext[] = {
    { "chain", cq_k_sext_chain, NULL,
      "BACKEND_ABI §2.6 Impl. (BJ lower_cast!): zext wiring plus CX of the sign bit into every higher o_j, one after the other: depth TO-FROM+1" },
    { "fanout_tree", cq_k_sext_tree, NULL,
      "The sign copied into ancillas by doubling, each copy driving a share of the targets, doubling undone: "
      "the standard CX fan-out (cf. Moore & Nilsson, arXiv:quant-ph/9808027, as for parity); o holds arbitrary bits, "
      "so the written result bits cannot be the copies. Own construction, copy count chosen to minimise depth" },
};

/* kmgy.10: every adder-based op lists the adders in CQ_ADDER_* order (cq_adder_kind relies on it). */
#define ADDER_SRC "; the adder records onto a tape: P = a ^ b and generates out of place, then the carries, " \
    "sum into P, XOR into o, tape undone in reverse (Bennett), so o may hold anything and operands are only controls"
#define ARITH_CANDS_(name, comm, ow) static const cq_kernel arith_##name[] = { \
    { "ripple", cq_k_##name##_ripple, NULL, "Ripple carry, carry chain Z[i+1] ^= p_i Z[i] on ancillae: ours" ADDER_SRC }, \
    { "lookahead", cq_k_##name##_lookahead, NULL, "Carry lookahead: the P-, G- and C-rounds of Draper, Kutin, Rains, Svore, " \
      "A logarithmic-depth quantum carry-lookahead adder, arXiv:quant-ph/0406142, §3 (checked in the paper); ours: carry-in " \
      "folded into Z[1], P^-1 rounds left to the undo" ADDER_SRC }, };
CQ_ARITH_OPS(ARITH_CANDS_)
/* Depth with every operand a qubit does not depend on the values; tests/test_tpl_arith.c checks values. */
#define ARITH_SAMPLES 50
#define ARITH_OP_(name, what) [CQ_KOP_ARITH_##name] = { #name, what, 2, 0, CANDS(arith_##name), 0, ARITH_SAMPLES }
#define ARITH_FLAG_(name, what) [CQ_KOP_ARITH_##name] = { #name, what, 2, 1, CANDS(arith_##name), 0, ARITH_SAMPLES }

/* kmgy.13: bitwise and shifts (src/kernels/bitwise.c, shift.c). */
#define BITWISE_SRC(f) "BACKEND_ABI §2.3 Impl.: " f " per bit straight onto o, no ancilla; a literal is a classical rail the emitter folds"
static const cq_kernel and_[] = { { "ccx", cq_k_and, NULL, BITWISE_SRC("CCX(a_i, b_i, o_i)") ", depth 1" } };
static const cq_kernel or_[] = { { "cx_cx_ccx", cq_k_or, NULL, BITWISE_SRC("CX(a_i), CX(b_i), CCX(a_i, b_i) onto o_i") ", depth 3: "
    "o_i takes three terms. The De Morgan form (X a, X b, CCX, X back, X o_i) is also depth 3 with more gates and "
    "breaks when a and b are one rail, so it is not registered" } };
static const cq_kernel xor_[] = { { "cx_cx", cq_k_xor, NULL, BITWISE_SRC("CX(a_i), CX(b_i) onto o_i") ", depth 2: o_i takes two terms" } };
#define SHIFT_SRC "Ours: one funnel shift of the 2W-bit hi:lo (shl, lshr, ashr as funnels with zero or sign-copy halves); " \
    "r = k mod W (low bits of k at a power-of-two W, else k_j wired to the residues 2^j mod W, a Wallace tree of full adders, " \
    "a ripple add, conditional subtractions of W 2^t); " \
    "barrel of ceil(log2 W) out-of-place mux stages over r, the last onto o, the rest undone in reverse (Bennett). " \
    "Barrel shifter as BACKEND_ABI §2.3 (BJ lower_var_shl!/lower_var_lshr!/lower_var_ashr!, not read here). "
#define SHIFT_CANDS_(op) static const cq_kernel op##_[] = { \
    { "barrel", cq_k_##op##_barrel, NULL, SHIFT_SRC "Each stage's control bit drives its muxes directly." }, \
    { "barrel_fanout", cq_k_##op##_barrel_fan, NULL, SHIFT_SRC "Each stage's control bit first copied to one bit per mux by a CX " \
      "doubling tree (the standard fan-out, cf. Moore & Nilsson, arXiv:quant-ph/9808027), as is ashr's sign." }, };
SHIFT_CANDS_(shl)
SHIFT_CANDS_(lshr)
SHIFT_CANDS_(ashr)
SHIFT_CANDS_(fshl)
SHIFT_CANDS_(fshr)

const cq_kernel_op cq_kernel_ops[CQ_KOP_COUNT] = {
    [CQ_KOP_AND] = { "and", "o ^= a & b (ABI §2.3).", 2, 0, CANDS(and_), 0, 0 },
    [CQ_KOP_OR] = { "or", "o ^= a | b (ABI §2.3).", 2, 0, CANDS(or_), 0, 0 },
    [CQ_KOP_XOR] = { "xor", "o ^= a ^ b (ABI §2.3).", 2, 0, CANDS(xor_), 0, 0 },
    [CQ_KOP_SHL] = { "shl", "o ^= a << (k mod W), k a W-bit rail (ABI §2.3, CR §2).", 2, 0, CANDS(shl_), 0, 20 },
    [CQ_KOP_LSHR] = { "lshr", "o ^= a >> (k mod W), logical (ABI §2.3, CR §2).", 2, 0, CANDS(lshr_), 0, 20 },
    [CQ_KOP_ASHR] = { "ashr", "o ^= a >> (k mod W), arithmetic (ABI §2.3, CR §2).", 2, 0, CANDS(ashr_), 0, 20 },
    [CQ_KOP_FSHL] = { "fshl", "o ^= the high W bits of (a:b) << (k mod W) (ABI §2.3, CR §2).", 3, 0, CANDS(fshl_), 0, 20 },
    [CQ_KOP_FSHR] = { "fshr", "o ^= the low W bits of (a:b) >> (k mod W) (ABI §2.3, CR §2).", 3, 0, CANDS(fshr_), 0, 20 },
    ARITH_OP_(add, "o ^= a + b mod 2^W (ABI §2.2)."),
    ARITH_OP_(sub, "o ^= a - b mod 2^W, as a + ~b + 1 (ABI §2.2)."),
    ARITH_FLAG_(uadd_overflow, "o (i1) ^= the carry out of a + b (ABI §2.7)."),
    ARITH_FLAG_(sadd_overflow, "o (i1) ^= signed overflow of a + b: carry into the sign bit ^ carry out (ABI §2.7)."),
    ARITH_FLAG_(usub_overflow, "o (i1) ^= [a <u b]: no carry out of a + ~b + 1 (ABI §2.7)."),
    ARITH_FLAG_(ssub_overflow, "o (i1) ^= signed overflow of a - b (ABI §2.7)."),
    ARITH_OP_(uadd_sat, "o ^= min(a + b, 2^W - 1): sum OR the fanned-out carry (ABI §2.7)."),
    ARITH_OP_(sadd_sat, "o ^= a + b clamped to the signed range (ABI §2.7)."),
    ARITH_OP_(usub_sat, "o ^= max(a - b, 0): difference AND the fanned-out carry (ABI §2.7)."),
    ARITH_OP_(ssub_sat, "o ^= a - b clamped to the signed range (ABI §2.7)."),
    [CQ_KOP_PARITY] = { "parity",
        "o (i1) ^= XOR of all bits of a. Demo operation of the depth tooling (kmgy.9), not an ABI template.",
        1, 1, CANDS(parity), 0, 0 },
    [CQ_KOP_BSWAP] = { "bswap",
        "o ^= a with its W/8 bytes reversed (ABI §2.8; W a multiple of 16 only).", 1, 0, CANDS(bswap), 16, 0 },
    [CQ_KOP_BITREVERSE] = { "bitreverse",
        "o ^= a with bit i at bit W - 1 - i (ABI §2.8).", 1, 0, CANDS(bitreverse), 0, 0 },
    [CQ_KOP_ZEXT] = { "zext", "o (width TO, the row) ^= a zero-extended from FROM < TO; cells are the worst case over FROM.",
        1, 0, CANDS(zext), CQ_STEP_FROM_BELOW, 0 },
    [CQ_KOP_SEXT] = { "sext", "o (width TO, the row) ^= a sign-extended from FROM < TO; cells are the worst case over FROM.",
        1, 0, CANDS(sext), CQ_STEP_FROM_BELOW, 0 },
    [CQ_KOP_TRUNC] = { "trunc", "o (width TO, the row) ^= the low TO bits of a, FROM > TO; cells are the worst case over FROM.",
        1, 0, CANDS(trunc_), CQ_STEP_FROM_ABOVE, 0 },
};

#define CQ_KW_BITS_(name, bits) bits,
const uint32_t cq_kernel_widths[CQ_NWIDTHS] = { CQ_WIDTHS(CQ_KW_BITS_) };
const uint32_t cq_kernel_nwidths = CQ_NWIDTHS;

const cq_kernel *cq_kernel_pick(int op, uint32_t w) {
    int i = 0;
    /* shortcut: a width not in CQ_WIDTHS takes the next listed width's choice; add it to the list if it matters. */
    while (i < CQ_NWIDTHS - 1 && cq_kernel_widths[i] < w) i++;
    return &cq_kernel_ops[op].cand[cq_kernel_choice[op][i]];
}
