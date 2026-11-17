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
      "doubling tree (the standard fan-out, cf. Moore & Nilsson, arXiv:quant-ph/9808027), as is ashr's sign." }, \
    { "barrel_fanout_fold", cq_k_##op##_barrel_fold, NULL, "As barrel_fanout, with another r = k mod W at a W that is no power " \
      "of two (kmgy.32, ours): W = V 2^t, V odd, r keeps the low t bits of k and r >> t = (k >> t) mod V; a bit of weight 2^b " \
      "moves to 2^(b mod P) (2^P = 1 mod V) or the 1-bits of 2^b mod V, a Wallace tree to two rows, the rows fold again while " \
      "the bound shrinks, one lookahead add (cq_adder), then s - qV for every q at once (lookahead subtractions) and a " \
      "one-hot select on the flags [s >= qV]." }, };
SHIFT_CANDS_(shl)
SHIFT_CANDS_(lshr)
SHIFT_CANDS_(ashr)
SHIFT_CANDS_(fshl)
SHIFT_CANDS_(fshr)

/* kmgy.12: divide and remainder (src/kernels/div.c). */
#define DIV_SRC "Restoring long division (the pencil-and-paper algorithm; ABI §2.2 cites BJ _soft_udiv_compile, not read here), " \
    "ours as a circuit: trip n compares the n-bit partial remainder with b by the carry out of R' + ~b + 1, ANDed with " \
    "[b < 2^n] (a Kogge-Stone suffix AND of ~b), and selects the new remainder as R' ^ q(~b ^ carries) through a CX " \
    "fan-out of q; signed forms negate magnitudes in and the result out ((x ^ s) + s); all on one tape, undone after " \
    "the XOR into o (Bennett). "
#define DIV_CANDS_(name) static const cq_kernel div_##name[] = { \
    { "restoring_ripple", cq_k_##name##_ripple, NULL, DIV_SRC "Every adder ripple carry." }, \
    { "restoring_lookahead", cq_k_##name##_lookahead, NULL, DIV_SRC "Every adder the Draper-Kutin-Rains-Svore lookahead " \
      "(arXiv:quant-ph/0406142), so a trip is O(log n) deep and the whole O(W log W)." }, \
    { "radix4_lookahead", cq_k_##name##_r4, NULL, DIV_SRC "Radix 4, lookahead adders: a trip takes two bits of a and compares " \
      "R' with b, 2b, 3b at once (3b precomputed by one adder, the copies of R' by fan-out); the flags' thermometer is the " \
      "digit, its one-hot form selects R' - jb. Ours, after the textbook radix-2^k restoring division with all multiples " \
      "compared in parallel (cf. Parhami, Computer Arithmetic, 2nd ed., ch. 14; not read here)." }, \
    { "radix8_lookahead", cq_k_##name##_r8, NULL, DIV_SRC "Radix 8 as radix 4: three bits a trip, seven multiples (3b, 5b, " \
      "7b by adders, the even ones shifted views)." }, };
CQ_DIV_OPS(DIV_CANDS_)
/* An i200 call is ~10^6 gates; depth does not depend on the values (tests/test_tpl_div.c checks them). */
#define DIV_OP_(name, what) [CQ_KOP_DIV_##name] = { #name, what, 2, 0, CANDS(div_##name), 0, 2 }

/* kmgy.16: bit counting (src/kernels/count.c); listed in CQ_POPCOUNT_* / CQ_CLZ_* order. */
#define COUNT_SRC "; recorded on a tape, the m = bitlen(W) count bits XORed into o, tape undone (Bennett)"
static const cq_kernel ctpop_[] = {
    { "linear", cq_k_ctpop_linear, NULL, "BACKEND_ABI §2.8 Impl. (BJ: the W bits summed by W - 1 adds), here each bit "
      "the carry-in of an m-bit adder (the shipped add) onto the running count" COUNT_SRC },
    { "csa_tree", cq_k_ctpop_csa, NULL, "Carry-save (Wallace) tree of depth-3 full adders per weight column, carries "
      "out of the top column dropped, until two rows remain, then one m-bit adder (the shipped add): Wallace, A suggestion "
      "for a fast multiplier, IEEE Trans. Electron. Comput. EC-13 (1964); not read" COUNT_SRC },
};
#define CLZ_SRC(dir) { "thermometer", cq_k_##dir##_thermo, NULL, "Ours: a Kogge-Stone prefix AND of the negated bits " \
      "from the counted end (Kogge & Stone, IEEE Trans. Comput. C-22 (1973); not read) gives the thermometer N_j = [j < c]; " \
      "bit k of c is the XOR of N_j over (j + 1) % 2^k = 0, one CX tree per bit, no adder" COUNT_SRC }, \
    { "priority_tree", cq_k_##dir##_tree, NULL, "Divide-and-conquer priority encoder: W padded below with ones to 2^L; " \
      "per node z = zH zL and c = zH ? 2^l + cL : cH (a wire and an in-place mux, zH fanned out); the root's c ^= z gives W " \
      "for 0. Standard leading-zero counter (cf. Oklobdzija, IEEE Trans. VLSI 2 (1994); not read)" COUNT_SRC }
static const cq_kernel ctlz_[] = { CLZ_SRC(ctlz) };
static const cq_kernel cttz_[] = { CLZ_SRC(cttz) };
#define COUNT_SAMPLES 50   /* depth does not depend on the values; tests/test_tpl_count.c checks them */

/* kmgy.11: multiply (src/kernels/mul.c), listed in CQ_MUL_* order. */
#define MUL_SRC "; recorded on a tape, signed products as the modified Baugh-Wooley array (Baugh & Wooley, IEEE Trans. " \
    "Comput. C-22 (1973); not read: complemented sign-row terms plus a constant), result XORed into o, tape undone (Bennett)"
#define MUL_CANDS_(name, what) static const cq_kernel name##_[] = { \
    { "shift_add", cq_k_##name##_shift_add, NULL, "Shift-and-add as BACKEND_ABI §2.2 Impl. (BJ lower_mul_wide!, not read): " \
      "one CCX row of partial products per multiplier bit (bits fanned out), each added by the shipped adder" what MUL_SRC }, \
    { "dadda_tree", cq_k_##name##_tree, NULL, "Tree multiplier: operand bits fanned out (CX doubling), every partial product " \
      "in one CCX layer, Dadda reduction (Dadda, Some schemes for parallel multipliers, Alta Frequenza 34 (1965); not read) " \
      "by depth-3 full adders and half adders, earliest bits first, then one shipped adder" what MUL_SRC }, };
MUL_CANDS_(mul, "; only the columns below W")
MUL_CANDS_(umul_overflow, "; the 2W columns, o ^= OR of the high W bits (NOT of an AND tree)")
MUL_CANDS_(smul_overflow, "; the 2W columns, o ^= OR over W-1 <= i < 2W-1 of p_i ^ p_(i+1) (all of bits W-1 .. 2W-1 equal)")
/* An i200 call is ~10^6 gates and ~10^5 scratch bits; depth does not depend on the values (tests/test_tpl_mul.c checks them). */
#define MUL_SAMPLES 4

/* kmgy.14: compare, select, min/max, abs (src/kernels/cmp.c); compare kinds in CQ_CMP_* order, select in CQ_SEL_*. */
#define CMP_TAPE "; scratch recorded on a tape, the flag's last gates onto o, tape undone (Bennett)"
#define CMP_EQ_CANDS_(p) \
    { "chain", cq_k_icmp_##p##_chain, NULL, "BACKEND_ABI §2.4 Impl. (BJ lower_eq!, not read): XNOR of the operands per bit " \
      "(CX, CX, X into scratch), then a linear AND chain of CCX" CMP_TAPE }, \
    { "tree", cq_k_icmp_##p##_tree, NULL, "XNOR per bit as chain, then a balanced CCX AND tree, depth ceil(log2 W): ours" CMP_TAPE },
#define CMP_ORD_CANDS_(p) \
    { "ripple", cq_k_icmp_##p##_ripple, NULL, "BACKEND_ABI §2.4 Impl. (BJ lower_ult!, not read): the carry out of a + ~b + 1 " \
      "by the ripple adder (kmgy.10), carry only, no sum bits; ult is its complement, slt also XORs both sign bits in" CMP_TAPE }, \
    { "lookahead", cq_k_icmp_##p##_lookahead, NULL, "As ripple with the Draper-Kutin-Rains-Svore lookahead adder " \
      "(arXiv:quant-ph/0406142), carry out only" CMP_TAPE }, \
    { "tree", cq_k_icmp_##p##_tree, NULL, "Comparator tree, ours: per bit lt_i = ~a_i b_i and eq_i = XNOR, nodes combined by " \
      "lt = lt_hi ^ eq_hi lt_lo (in place), eq = eq_hi eq_lo, depth about 2 log2 W, no sum logic (the binary-tree comparator, " \
      "cf. the prefix formulation of Kogge & Stone, IEEE Trans. Comput. C-22 (1973); not read); signed: the sign bit's leaf swapped" CMP_TAPE },
#define CMP_CANDS_EQ_(p, base, swap, neg) static const cq_kernel icmp_##p##_[] = { CMP_EQ_CANDS_(p) };
#define CMP_CANDS_ORD_(p, base, swap, neg) static const cq_kernel icmp_##p##_[] = { CMP_ORD_CANDS_(p) };
CQ_ICMP_EQ_PREDS(CMP_CANDS_EQ_)
CQ_ICMP_ORD_PREDS(CMP_CANDS_ORD_)
#define SEL_SRC "BACKEND_ABI §2.5 Impl. (BJ lower_mux!, not read): o ^= a2 ^ c (a1 ^ a2); "
#define SEL_FAN "; c first copied to W bits by the CX doubling fan-out (cf. Moore & Nilsson, arXiv:quant-ph/9808027), undone after"
static const cq_kernel select_[] = {
    { "ccx2", cq_k_select_ccx2, NULL, SEL_SRC "per bit CX(a2), CCX(c, a1), CCX(c, a2) onto o, no ancilla; c drives all 2W CCX" },
    { "ccx2_fanout", cq_k_select_ccx2_fanout, NULL, SEL_SRC "as ccx2" SEL_FAN },
    { "diff", cq_k_select_diff, NULL, SEL_SRC "d = a1 ^ a2 in scratch, per bit CX(a2), CCX(c, d) onto o; c drives all W CCX" },
    { "diff_fanout", cq_k_select_diff_fanout, NULL, SEL_SRC "as diff" SEL_FAN },
};
#define MINMAX_SRC "BACKEND_ABI §2.7 Impl. (compare + select): f = [a < b] on the tape by the "
#define MINMAX_SEL " compare candidate of icmp, o ^= the select (diff) of a and b under f"
#define MINMAX_CANDS_(name, base, max) static const cq_kernel name##_[] = { \
    { "ripple", cq_k_##name##_ripple, NULL, MINMAX_SRC "ripple" MINMAX_SEL CMP_TAPE }, \
    { "ripple_fanout", cq_k_##name##_ripple_fanout, NULL, MINMAX_SRC "ripple" MINMAX_SEL ", f fanned out" CMP_TAPE }, \
    { "lookahead", cq_k_##name##_lookahead, NULL, MINMAX_SRC "lookahead" MINMAX_SEL CMP_TAPE }, \
    { "lookahead_fanout", cq_k_##name##_lookahead_fanout, NULL, MINMAX_SRC "lookahead" MINMAX_SEL ", f fanned out" CMP_TAPE }, \
    { "tree", cq_k_##name##_tree, NULL, MINMAX_SRC "tree" MINMAX_SEL CMP_TAPE }, \
    { "tree_fanout", cq_k_##name##_tree_fanout, NULL, MINMAX_SRC "tree" MINMAX_SEL ", f fanned out" CMP_TAPE }, };
CQ_MINMAX_OPS(MINMAX_CANDS_)
#define ABS_SRC "Conditional negate (a ^ s) + s, s the sign bit: x = a ^ s in scratch, then the adder (kmgy.10) adds s as its " \
    "carry-in to x + 0; BACKEND_ABI §2.7 Impl. builds select(a >=s 0, a, 0 - a), this needs no compare or select"
static const cq_kernel abs_[] = {
    { "ripple", cq_k_abs_ripple, NULL, ABS_SRC ", ripple adder, s drives every XOR" CMP_TAPE },
    { "ripple_fanout", cq_k_abs_ripple_fanout, NULL, ABS_SRC ", ripple adder, s fanned out (CX doubling)" CMP_TAPE },
    { "lookahead", cq_k_abs_lookahead, NULL, ABS_SRC ", lookahead adder, s drives every XOR" CMP_TAPE },
    { "lookahead_fanout", cq_k_abs_lookahead_fanout, NULL, ABS_SRC ", lookahead adder, s fanned out (CX doubling)" CMP_TAPE },
};
/* Depth does not depend on the values; tests/test_tpl_cmp.c checks them. */
#define CMP_SAMPLES 20
#define ICMP_OP_(p, base, swap, neg) [CQ_KOP_ICMP_##p] = { "icmp_" #p, "o (i1) ^= [a " #p " b] (ABI §2.4).", 2, 1, CANDS(icmp_##p##_), 0, CMP_SAMPLES },
#define MINMAX_OP_(name, base, max) [CQ_KOP_##name] = { #name, "o ^= " #name "(a, b) (ABI §2.7).", 2, 0, CANDS(name##_), 0, CMP_SAMPLES },

const cq_kernel_op cq_kernel_ops[CQ_KOP_COUNT] = {
    CQ_ICMP_PREDS(ICMP_OP_)
    [CQ_KOP_SELECT] = { "select", "o ^= c ? a1 : a2, c an i1 rail (ABI §2.5).", 3, 0, CANDS(select_), 0, CMP_SAMPLES },
    CQ_MINMAX_OPS(MINMAX_OP_)
    [CQ_KOP_ABS] = { "abs", "o ^= |a|, abs(INT_MIN) = INT_MIN (ABI §2.7).", 1, 0, CANDS(abs_), 0, CMP_SAMPLES },
    [CQ_KOP_MUL] = { "mul", "o ^= a * b mod 2^W (ABI §2.2).", 2, 0, CANDS(mul_), 0, MUL_SAMPLES },
    [CQ_KOP_UMUL_OVERFLOW] = { "umul_overflow", "o (i1) ^= [a * b >= 2^W], unsigned (ABI §2.7).", 2, 1, CANDS(umul_overflow_), 0, MUL_SAMPLES },
    [CQ_KOP_SMUL_OVERFLOW] = { "smul_overflow", "o (i1) ^= [the signed a * b is outside [-2^(W-1), 2^(W-1))] (ABI §2.7).",
        2, 1, CANDS(smul_overflow_), 0, MUL_SAMPLES },
    [CQ_KOP_AND] = { "and", "o ^= a & b (ABI §2.3).", 2, 0, CANDS(and_), 0, 0 },
    [CQ_KOP_OR] = { "or", "o ^= a | b (ABI §2.3).", 2, 0, CANDS(or_), 0, 0 },
    [CQ_KOP_XOR] = { "xor", "o ^= a ^ b (ABI §2.3).", 2, 0, CANDS(xor_), 0, 0 },
    [CQ_KOP_SHL] = { "shl", "o ^= a << (k mod W), k a W-bit rail (ABI §2.3, CR §2).", 2, 0, CANDS(shl_), 0, 20 },
    [CQ_KOP_LSHR] = { "lshr", "o ^= a >> (k mod W), logical (ABI §2.3, CR §2).", 2, 0, CANDS(lshr_), 0, 20 },
    [CQ_KOP_ASHR] = { "ashr", "o ^= a >> (k mod W), arithmetic (ABI §2.3, CR §2).", 2, 0, CANDS(ashr_), 0, 20 },
    [CQ_KOP_FSHL] = { "fshl", "o ^= the high W bits of (a:b) << (k mod W) (ABI §2.3, CR §2).", 3, 0, CANDS(fshl_), 0, 20 },
    [CQ_KOP_FSHR] = { "fshr", "o ^= the low W bits of (a:b) >> (k mod W) (ABI §2.3, CR §2).", 3, 0, CANDS(fshr_), 0, 20 },
    [CQ_KOP_CTPOP] = { "ctpop", "o ^= the number of set bits of a (ABI §2.8).", 1, 0, CANDS(ctpop_), 0, COUNT_SAMPLES },
    [CQ_KOP_CTLZ] = { "ctlz", "o ^= the number of leading zeros of a, W for 0 (ABI §2.8, CR §2).", 1, 0, CANDS(ctlz_), 0, COUNT_SAMPLES },
    [CQ_KOP_CTTZ] = { "cttz", "o ^= the number of trailing zeros of a, W for 0 (ABI §2.8, CR §2): ctlz on the reversed bits.",
        1, 0, CANDS(cttz_), 0, COUNT_SAMPLES },
    DIV_OP_(udiv,"o ^= a / b unsigned; b = 0 gives 2^W - 1 (ABI §2.2, CR §2)."),
    DIV_OP_(urem, "o ^= a mod b unsigned; b = 0 gives a (ABI §2.2, CR §2)."),
    DIV_OP_(sdiv, "o ^= a / b signed, truncating; b = 0 gives -1 (a >= 0) or +1, INT_MIN / -1 gives INT_MIN (ABI §2.2, CR §2)."),
    DIV_OP_(srem, "o ^= a rem b signed, sign of a; b = 0 gives a, INT_MIN rem -1 gives 0 (ABI §2.2, CR §2)."),
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
