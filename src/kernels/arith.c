/* BACKEND_ABI §2.2 add, sub and §2.7 overflow and saturating forms (kmgy.10): o ^= f(a, b).
 * Each records an adder on a tape (kernels.h), XORs into o, and undoes the tape, so o may
 * hold anything on entry and the operands are only ever controls (they may alias). One
 * kernel per (op, adder); the registry lists them in CQ_ADDER_* order. */
#include "cq_rt.h"
#include "kernels.h"

enum { ADD, SUB, UADDO, SADDO, USUBO, SSUBO, UADDSAT, SADDSAT, USUBSAT, SSUBSAT };

static void arith(int op, int kind, int32_t o, const int32_t *args, uint32_t w) {
    static const cq_bit one = { CQ_BIT_ONE, 0 };
    cq_bit *ob = cq_rail_bits(o), *a = cq_rail_bits(args[0]), *b = cq_rail_bits(args[1]);
    int sub = op == SUB || op == USUBO || op == SSUBO || op == USUBSAT || op == SSUBSAT;   /* a + ~b + 1 */
    int want = (op == ADD || op == SUB ? 0 : CQ_ADD_COUT) | (op == ADD || op == SUB || op >= UADDSAT ? CQ_ADD_SUM : 0);
    cq_tape t = { 0 };
    cq_sum r = cq_adder(kind, &t, a, b, sub, sub ? &one : NULL, w, want);
    const cq_bit *s = r.s, *cout = &r.c[w];
    switch (op) {
    case ADD: case SUB:
        for (uint32_t i = 0; i < w; i++) cq_cx(&s[i], &ob[i]);
        break;
    case UADDO: cq_cx(cout, ob); break;
    case USUBO: cq_cx(cout, ob); cq_x(ob); break;          /* borrow = no carry out */
    case SADDO: case SSUBO:                                /* carry into the sign bit ^ carry out of it */
        cq_cx(cout, ob); cq_cx(&r.c[w - 1], ob); break;
    case UADDSAT: case USUBSAT: {                          /* v fanned out so the w writes run in parallel */
        const cq_bit *f = cq_tape_fanout(&t, cout, w - 1);
        for (uint32_t i = 0; i < w; i++) {
            const cq_bit *v = i ? &f[i - 1] : cout;
            if (op == UADDSAT) { cq_cx(&s[i], &ob[i]); cq_cx(v, &ob[i]); }   /* s | v */
            cq_ccx(&s[i], v, &ob[i]);                                         /* uadd: s & v; usub: s & no borrow */
        }
        break;
    }
    case SADDSAT: case SSUBSAT: {
        /* On overflow the sign of s is the complement of the clamp's, so o = v ? clamp : s is
         * top: s_top ^ v; below: s_i ^ v (s_i ^ s_top). */
        cq_bit *v = cq_tape_bits(&t, 1);
        cq_tape_cx(&t, cout, v);
        cq_tape_cx(&t, &r.c[w - 1], v);
        cq_bit *fv = cq_tape_fanout(&t, v, w - 1), *e = cq_tape_fanout(&t, &s[w - 1], w - 1);
        for (uint32_t i = 0; i + 1 < w; i++) cq_tape_cx(&t, &s[i], &e[i]);
        for (uint32_t i = 0; i + 1 < w; i++) { cq_cx(&s[i], &ob[i]); cq_ccx(&fv[i], &e[i], &ob[i]); }
        cq_cx(&s[w - 1], &ob[w - 1]);
        cq_cx(v, &ob[w - 1]);
        break;
    }
    }
    cq_tape_undo(&t);
}

#define ARITH_K_(name, OP) \
    void cq_k_##name##_ripple(int32_t o, const int32_t *a, uint32_t w) { arith(OP, CQ_ADDER_RIPPLE, o, a, w); } \
    void cq_k_##name##_lookahead(int32_t o, const int32_t *a, uint32_t w) { arith(OP, CQ_ADDER_LOOKAHEAD, o, a, w); }
ARITH_K_(add, ADD)
ARITH_K_(sub, SUB)
ARITH_K_(uadd_overflow, UADDO)
ARITH_K_(sadd_overflow, SADDO)
ARITH_K_(usub_overflow, USUBO)
ARITH_K_(ssub_overflow, SSUBO)
ARITH_K_(uadd_sat, UADDSAT)
ARITH_K_(sadd_sat, SADDSAT)
ARITH_K_(usub_sat, USUBSAT)
ARITH_K_(ssub_sat, SSUBSAT)
