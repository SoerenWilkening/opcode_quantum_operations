/* Emitter, pool and handle table against the replay backend. Always asserts. */
#undef NDEBUG
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cq_gates.h"
#include "cq_replay.h"
#include "cq_rt.h"
#include "cq_widths.h"

enum { X, CX, CCX, RY, RYC, RZ };
static const cq_gate_kind kind_of[] = { CQ_G_X, CQ_G_CNOT, CQ_G_CCX, CQ_G_RY, CQ_G_RY_CTL, CQ_G_RZ };

static int val(cq_bit b) { return cq_bit_is_q(b) ? cq_replay_get_bit(b.q) : b.kind; }

/* One row: operand kinds as a string over {0,1,Q} (controls first, target last),
 * the expected per-kind gate counts {x, cnot, ccx, ry, ry_ctl, rz}, qubits taken. */
static void row(int op, const char *ops, const int want[6], uint32_t taken) {
    int n = (int)strlen(ops), nq = 0, qpos[3];
    for (int i = 0; i < n; i++) if (ops[i] == 'Q') qpos[nq++] = i;
    for (int m = 0; m < (1 << nq); m++) {          /* every value of the qubit operands */
        cq_bit b[3];
        for (int i = 0; i < n; i++) b[i] = cq_bit_const(ops[i] == '1');
        for (int k = 0; k < nq; k++) cq_get_qubit(&b[qpos[k]]);
        cq_replay_reset();
        for (int k = 0; k < nq; k++) cq_replay_set_bit(b[qpos[k]].q, (m >> k) & 1);
        int in[3];
        for (int i = 0; i < n; i++) in[i] = val(b[i]);
        uint32_t live0 = cq_pool_live();
        cq_bit *t = &b[n - 1];
        int expect = in[n - 1];
        switch (op) {
        case X:   cq_x(t); expect ^= 1; break;
        case CX:  cq_cx(&b[0], t); expect ^= in[0]; break;
        case CCX: cq_ccx(&b[0], &b[1], t); expect ^= in[0] & in[1]; break;
        case RY:  cq_ry(0.0, t); break;
        case RYC: cq_ry_ctl(0.0, &b[0], t); break;
        case RZ:  cq_rz(0.0, t); break;
        }
        for (int k = 0; k < 6; k++)
            if (cq_replay_gates_of(kind_of[k]) != (uint64_t)want[k]) {
                fprintf(stderr, "op %d ops %s: kind %d: got %llu want %d\n", op, ops, k,
                        (unsigned long long)cq_replay_gates_of(kind_of[k]), want[k]);
                assert(0);
            }
        assert(cq_replay_gates_of(CQ_G_M) == 0);
        assert(cq_pool_live() - live0 == taken);
        assert(val(*t) == expect);
        assert(cq_bit_is_q(*t) == (ops[n - 1] == 'Q' || taken));
        for (int i = 0; i < n - 1; i++)                        /* controls untouched */
            assert(val(b[i]) == in[i] && cq_bit_is_q(b[i]) == (ops[i] == 'Q'));
        for (int i = 0; i < n; i++) cq_reset_bit(&b[i]);
        assert(cq_pool_live() == 0);
    }
}

#define G(x, cx, ccx, ry, ryc, rz) ((const int[6]){x, cx, ccx, ry, ryc, rz})
#define NONE G(0, 0, 0, 0, 0, 0)

static int aborts(void (*f)(void)) {
    fflush(NULL);
    pid_t p = fork();
    if (p == 0) { fclose(stderr); f(); _exit(0); }
    int st;
    waitpid(p, &st, 0);
    return WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT;
}
static void cx_self(void)     { cq_bit a = cq_bit_const(0); cq_get_qubit(&a); cq_cx(&a, &a); }
static void cx_self_cl(void)  { cq_bit a = cq_bit_const(1); cq_cx(&a, &a); }
static void ccx_ct(void)      { cq_bit a = cq_bit_const(0), b = cq_bit_const(0); cq_get_qubit(&a); cq_get_qubit(&b); cq_ccx(&a, &b, &b); }
static void ccx_ct_idx(void)  { cq_bit a = cq_bit_const(0), b = cq_bit_const(1); cq_get_qubit(&a); cq_bit t = a; cq_ccx(&b, &a, &t); }
static void ryc_self(void)    { cq_bit a = cq_bit_const(0); cq_get_qubit(&a); cq_ry_ctl(0.0, &a, &a); }
static void bad_handle(void)  { cq_rail_bits(0); }
static void freed_handle(void){ int32_t h = cq_rail_alloc(8, NULL); cq_rail_free(h); cq_rail_bits(h); }
static void too_wide(void)    { cq_rail_alloc(CQ_RAIL_MAX_WIDTH + 1, NULL); }
static void double_give(void) { uint32_t q = cq_pool_take(); cq_pool_give(q); cq_pool_give(q); }

int main(void) {
    /* X */
    row(X, "0", NONE, 0);
    row(X, "1", NONE, 0);
    row(X, "Q", G(1,0,0,0,0,0), 0);

    /* CX(c, t) */
    const char *cl[] = { "0", "1", "Q" };
    for (int t = 0; t < 3; t++) { char s[3] = { '0', cl[t][0], 0 }; row(CX, s, NONE, 0); }
    row(CX, "10", NONE, 0);
    row(CX, "11", NONE, 0);
    row(CX, "1Q", G(1,0,0,0,0,0), 0);
    row(CX, "Q0", G(0,1,0,0,0,0), 1);
    row(CX, "Q1", G(1,1,0,0,0,0), 1);
    row(CX, "QQ", G(0,1,0,0,0,0), 0);

    /* CCX(a, b, t): 27 rows */
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++)
            for (int t = 0; t < 3; t++) {
                char s[4] = { cl[a][0], cl[b][0], cl[t][0], 0 };
                if (a == 0 || b == 0) { row(CCX, s, NONE, 0); continue; }
                int nq = (a == 2) + (b == 2);
                if (nq == 0)      row(CCX, s, t == 2 ? G(1,0,0,0,0,0) : NONE, 0);
                else if (nq == 1) row(CCX, s, t == 0 ? G(0,1,0,0,0,0) : t == 1 ? G(1,1,0,0,0,0) : G(0,1,0,0,0,0), t < 2);
                else              row(CCX, s, t == 0 ? G(0,0,1,0,0,0) : t == 1 ? G(1,0,1,0,0,0) : G(0,0,1,0,0,0), t < 2);
            }

    /* Rotations */
    row(RY, "0", G(0,0,0,1,0,0), 1);
    row(RY, "1", G(1,0,0,1,0,0), 1);
    row(RY, "Q", G(0,0,0,1,0,0), 0);
    row(RZ, "0", G(0,0,0,0,0,1), 1);
    row(RZ, "1", G(1,0,0,0,0,1), 1);
    row(RZ, "Q", G(0,0,0,0,0,1), 0);
    row(RYC, "00", NONE, 0);
    row(RYC, "01", NONE, 0);
    row(RYC, "0Q", NONE, 0);
    row(RYC, "10", G(0,0,0,1,0,0), 1);
    row(RYC, "11", G(1,0,0,1,0,0), 1);
    row(RYC, "1Q", G(0,0,0,1,0,0), 0);
    row(RYC, "Q0", G(0,0,0,0,1,0), 1);
    row(RYC, "Q1", G(1,0,0,0,1,0), 1);
    row(RYC, "QQ", G(0,0,0,0,1,0), 0);

    /* Coinciding controls fold: CCX(a, a, t) = CX(a, t), by pointer and by index. */
    {
        cq_bit a = cq_bit_const(1), t = cq_bit_const(0);
        cq_get_qubit(&a);
        cq_bit a2 = a;
        cq_replay_reset(); cq_replay_set_bit(a.q, 1);
        cq_ccx(&a, &a, &t);
        cq_ccx(&a, &a2, &t);
        assert(cq_replay_gates() == 2 && cq_replay_gates_of(CQ_G_CNOT) == 2 && val(t) == 0);
        cq_bit c = cq_bit_const(1), u = cq_bit_const(0);   /* classical: X(t) */
        cq_ccx(&c, &c, &u);
        assert(cq_replay_gates() == 2 && val(u) == 1);
        cq_reset_bit(&a); cq_reset_bit(&t);
    }
    /* Control == target, and pool misuse, are hard errors. */
    assert(aborts(cx_self) && aborts(cx_self_cl) && aborts(ccx_ct) && aborts(ccx_ct_idx) && aborts(ryc_self));
    assert(aborts(bad_handle) && aborts(freed_handle) && aborts(double_give) && aborts(too_wide));

    /* An all-classical sequence emits nothing and takes nothing. */
    {
        uint32_t minted0 = cq_pool_minted();
        cq_replay_reset();
        cq_bit b[4] = { cq_bit_const(0), cq_bit_const(1), cq_bit_const(1), cq_bit_const(0) };
        for (int i = 0; i < 100; i++) {
            cq_x(&b[i % 4]);
            cq_cx(&b[(i + 1) % 4], &b[(i + 2) % 4]);
            cq_ccx(&b[(i + 1) % 4], &b[(i + 2) % 4], &b[(i + 3) % 4]);
            if (b[0].kind == CQ_BIT_ONE) cq_x(&b[0]);
            cq_ry_ctl(0.0, &b[0], &b[1]);   /* a known-0 control: nothing */
            assert(cq_measure_bit(&b[i % 4]) == b[i % 4].kind);
        }
        for (int i = 0; i < 4; i++) assert(!cq_bit_is_q(b[i]));
        assert(cq_replay_gates() == 0 && cq_pool_minted() == minted0 && cq_pool_live() == 0);
    }

    /* "Gets a qubit": x only for ONE. */
    {
        cq_bit z = cq_bit_const(0), o = cq_bit_const(1);
        cq_replay_reset();
        cq_get_qubit(&z);
        assert(cq_replay_gates() == 0 && cq_pool_live() == 1);
        cq_get_qubit(&o);
        assert(cq_replay_gates() == 1 && cq_replay_gates_of(CQ_G_X) == 1 && cq_pool_live() == 2);
        assert(val(z) == 0 && val(o) == 1);
        cq_get_qubit(&o);                                   /* already a qubit: no-op */
        assert(cq_replay_gates() == 1 && cq_pool_live() == 2);

        /* Measure: index back to the pool, bit becomes the outcome; the pool reuses it (LIFO). */
        uint32_t qo = o.q, qz = z.q;
        assert(cq_measure_bit(&o) == 1 && o.kind == CQ_BIT_ONE && cq_pool_live() == 1);
        assert(cq_replay_gates_of(CQ_G_M) == 1 && cq_measure_bit(&o) == 1);  /* classical re-read: no gate */
        assert(cq_replay_gates_of(CQ_G_M) == 1);
        cq_bit n = cq_bit_const(0);
        cq_get_qubit(&n);
        assert(n.q == qo);

        /* Reset: m, x on 1, index back; the pool hands it out again. */
        cq_x(&n);
        cq_replay_reset(); cq_replay_set_bit(n.q, 1);
        cq_reset_bit(&n);
        assert(n.kind == CQ_BIT_ZERO && cq_replay_gates_of(CQ_G_M) == 1 && cq_replay_gates_of(CQ_G_X) == 1);
        assert(cq_replay_get_bit(qo) == 0 && cq_pool_live() == 1);
        cq_reset_bit(&z);                                    /* outcome 0: m only */
        assert(cq_replay_gates_of(CQ_G_M) == 2 && cq_replay_gates_of(CQ_G_X) == 1 && cq_pool_live() == 0);
        uint32_t minted = cq_pool_minted();
        cq_bit r1 = cq_bit_const(0), r2 = cq_bit_const(0);
        cq_get_qubit(&r1); cq_get_qubit(&r2);
        assert(r1.q == qz && r2.q == qo && cq_pool_minted() == minted);
        cq_reset_bit(&r1); cq_reset_bit(&r2);
    }

    /* Handle table, every width of the list. */
    {
        int32_t last = 0;
        uint64_t v[CQ_LIMBS];                                    /* distinct pattern per limb */
        for (uint32_t k = 0; k < CQ_LIMBS; k++) v[k] = 0xF0E1D2C3B4A59687u * (2 * k + 1) ^ k;
#define W(name, bits) {                                                          \
        cq_replay_reset();                                                       \
        int32_t h = cq_rail_alloc(bits, v);                                      \
        assert(h > last); last = h;                                              \
        assert(cq_rail_width(h) == bits);                                        \
        cq_bit *r = cq_rail_bits(h);                                             \
        for (uint32_t i = 0; i < bits; i++) {                                    \
            assert(!cq_bit_is_q(r[i]));                                          \
            assert(r[i].kind == (int)((v[i / 64] >> (i % 64)) & 1));             \
        }                                                                        \
        assert(cq_pool_live() == 0 && cq_replay_gates() == 0);                   \
        cq_ry(0.0, &r[0]); cq_ry(0.0, &r[bits - 1]);                             \
        assert(cq_pool_live() == (bits > 1 ? 2u : 1u));                          \
        int32_t h2 = cq_rail_alloc(bits, NULL);                                  \
        assert(h2 == h + 1 && cq_rail_bits(h) == r); last = h2;                  \
        cq_rail_free(h);                                                         \
        assert(cq_pool_live() == 0);                                             \
        cq_rail_free(h2);                                                        \
    }
        CQ_WIDTHS(W)
#undef W
        /* Many handles: the table grows and earlier bit pointers stay valid. */
        int32_t h0 = cq_rail_alloc(128, (const uint64_t[]){ 1, 1 });   /* reads 2 limbs only */
        cq_bit *b0 = cq_rail_bits(h0);
        for (int i = 0; i < 1000; i++) cq_rail_alloc(1, NULL);
        assert(cq_rail_bits(h0) == b0 && b0[0].kind == 1 && b0[64].kind == 1 && b0[1].kind == 0);
        /* The widest width is the largest of the list, and bits above width are ignored. */
        uint32_t wmax = 0;
#define W(name, bits) if (bits > wmax) wmax = bits;
        CQ_WIDTHS(W)
#undef W
        assert(CQ_RAIL_MAX_WIDTH == wmax && CQ_LIMBS * 64 >= wmax && (CQ_LIMBS - 1) * 64 < wmax);
        int32_t h1 = cq_rail_alloc(65, (const uint64_t[]){ 0, ~0ull });
        cq_bit *b1 = cq_rail_bits(h1);
        assert(b1[63].kind == 0 && b1[64].kind == 1);
        cq_rail_free(h1);
    }
    puts("test_emit: ok");
    return 0;
}
