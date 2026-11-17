/* The sampled operation test harness; see harness.h. */
#include <stdio.h>
#include <stdlib.h>
#include "harness.h"
#include "cq_replay.h"
#include "cq_rt.h"
#include "cq_widths.h"

#define HX_SEED   0x6b6d67792e35ull   /* fixed; mixed with op name and width per stream */
#define HX_CAP    1000u               /* samples per (op, width); a sample is (args, o0) */
uint32_t hx_max_samples;
#define HX_TUPLES ((hx_max_samples && hx_max_samples < HX_CAP ? hx_max_samples : HX_CAP) / 2)   /* each arg tuple runs with o0 = 0 and a random nonzero o0 */

hx_val hx_mask(hx_val v, uint32_t w) {
    for (uint32_t k = 0; k < CQ_LIMBS; k++)
        if (w <= 64 * k) v.l[k] = 0;
        else if (w < 64 * (k + 1)) v.l[k] &= (1ull << (w - 64 * k)) - 1;
    return v;
}
hx_val hx_ones(uint32_t w) {
    hx_val v;
    for (uint32_t k = 0; k < CQ_LIMBS; k++) v.l[k] = ~0ull;
    return hx_mask(v, w);
}
int hx_eq(hx_val a, hx_val b) {
    for (uint32_t k = 0; k < CQ_LIMBS; k++) if (a.l[k] != b.l[k]) return 0;
    return 1;
}
hx_val hx_xor(hx_val a, hx_val b) { for (uint32_t k = 0; k < CQ_LIMBS; k++) a.l[k] ^= b.l[k]; return a; }
hx_val hx_and(hx_val a, hx_val b) { for (uint32_t k = 0; k < CQ_LIMBS; k++) a.l[k] &= b.l[k]; return a; }

static int bit_of(hx_val v, uint32_t i) { return (int)((v.l[i / 64] >> (i % 64)) & 1); }

int32_t hx_to_rail(hx_val v, uint32_t w) { return cq_rail_alloc(w, v.l); }

hx_val hx_from_rail(int32_t h) {
    cq_bit *b = cq_rail_bits(h);
    hx_val v = { { 0 } };
    for (uint32_t i = 0, w = cq_rail_width(h); i < w; i++)
        if (cq_measure_bit(&b[i])) v.l[i / 64] |= 1ull << (i % 64);
    return v;
}

/* splitmix64 */
static uint64_t next(uint64_t *s) {
    uint64_t z = (*s += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
static hx_val rnd(uint64_t *s, uint32_t w) {
    hx_val v;
    for (uint32_t k = 0; k < CQ_LIMBS; k++) v.l[k] = next(s);
    return hx_mask(v, w);
}

/* Forced-quantum inputs get their value by replay set_bit, not X, so the op's counters start at 0. */
static int32_t prep(hx_val v, uint32_t w, int quantum) {
    if (!quantum) return hx_to_rail(v, w);
    int32_t h = cq_rail_alloc(w, NULL);
    cq_bit *b = cq_rail_bits(h);
    for (uint32_t i = 0; i < w; i++) { cq_get_qubit(&b[i]); cq_replay_set_bit(b[i].q, bit_of(v, i)); }
    return h;
}

static void pr(hx_val v) {
    uint32_t k = CQ_LIMBS - 1;
    while (k && !v.l[k]) k--;
    fprintf(stderr, "0x%llx", (unsigned long long)v.l[k]);
    while (k--) fprintf(stderr, "%016llx", (unsigned long long)v.l[k]);
}

static const char *fail_name[] = { "ok", "wrong result", "operand changed", "ancilla leak",
                                   "classical run emitted a gate or took a qubit", "result not back to o0 after unc" };

typedef struct { const hx_op *op; uint32_t w, ow; uint64_t seed; uint32_t sample; const uint32_t *aw; } ctx;

hx_val hx_sample[HX_MAX_ARGS];

static uint32_t arg_w(const ctx *c, int j) { return c->aw && c->aw[j] ? c->aw[j] : c->w; }

/* 0, 1, all-ones, sign bit = min signed, max signed */
static hx_val edge(uint32_t k, uint32_t w) {
    hx_val ones = hx_ones(w), sign = { { 0 } };
    sign.l[(w - 1) / 64] = 1ull << ((w - 1) % 64);
    const hx_val e[] = { { { 0 } }, { { 1 } }, ones, sign, hx_xor(ones, sign) };
    return e[k];
}

static hx_fail run1(const ctx *c, const hx_val *a, hx_val o0, int quantum, int with_unc, hx_stats *st) {
    const hx_op *op = c->op;
    cq_replay_reset();
    uint32_t base = cq_pool_live();
    int32_t o = prep(o0, c->ow, quantum), h[HX_MAX_ARGS];
    for (int j = 0; j < op->nargs; j++) h[j] = prep(a[j], arg_w(c, j), quantum);
    for (int j = 0; j < op->nargs; j++) hx_sample[j] = a[j];

    hx_fail f = HX_OK;
    op->op(o, h, c->w);
    if (quantum && st) {
        if (cq_replay_gates() > st->gates) st->gates = cq_replay_gates();
        if (cq_replay_depth() > st->depth) st->depth = cq_replay_depth();
        if (cq_replay_qubits() > st->qubits) st->qubits = cq_replay_qubits();
    }
    if (!quantum && (cq_replay_gates() || cq_pool_live() != base)) f = HX_CLASSICAL_GATE;
    if (with_unc) {
        op->unc(o, h, c->w);
        if (!f && !quantum && (cq_replay_gates() || cq_pool_live() != base)) f = HX_CLASSICAL_GATE;
    }

    hx_val got = hx_from_rail(o), want = with_unc ? o0 : hx_xor(o0, hx_mask(op->ref(a, c->w), c->ow));
    if (!f && !hx_eq(got, want)) f = with_unc ? HX_UNC_RESULT : HX_WRONG_RESULT;
    int bad_arg = -1;
    hx_val arg_got = { { 0 } };
    for (int j = 0; j < op->nargs; j++) {
        hx_val m = hx_from_rail(h[j]);
        if (!f && !hx_eq(m, a[j])) { f = HX_OPERAND_CHANGED; bad_arg = j; arg_got = m; }
    }
    cq_rail_free(o);
    for (int j = 0; j < op->nargs; j++) cq_rail_free(h[j]);
    if (!f && cq_pool_live() != base) f = HX_LEAK;

    if (f) {
        fprintf(stderr, "harness: FAIL (%s) op=%s width=i%u mode=%s%s seed=0x%llx sample=%u o0=",
                fail_name[f], op->name, c->w, quantum ? "forced-quantum" : "all-classical",
                with_unc ? " +unc" : "", (unsigned long long)c->seed, c->sample);
        pr(o0);
        for (int j = 0; j < op->nargs; j++) { fprintf(stderr, " a%d=", j); pr(a[j]); }
        if (f == HX_WRONG_RESULT || f == HX_UNC_RESULT) { fprintf(stderr, " got="); pr(got); fprintf(stderr, " want="); pr(want); }
        if (f == HX_OPERAND_CHANGED) { fprintf(stderr, " a%d now=", bad_arg); pr(arg_got); }
        if (f == HX_LEAK) fprintf(stderr, " live=%u base=%u", cq_pool_live(), base);
        fprintf(stderr, "\n");
    }
    return f;
}

hx_fail hx_run(const hx_op *op, uint32_t w, int flags, hx_stats *st) { return hx_run_w(op, w, NULL, flags, st); }

hx_fail hx_run_w(const hx_op *op, uint32_t w, const uint32_t *aw, int flags, hx_stats *st) {
    if (op->nargs < 0 || op->nargs > HX_MAX_ARGS || w == 0 || w > CQ_RAIL_MAX_WIDTH) {
        fprintf(stderr, "harness: bad op %s or width %u\n", op->name, w);
        abort();
    }
    ctx c = { op, w, op->out_width ? op->out_width : w, HX_SEED ^ ((uint64_t)w << 32), 0, aw };
    for (const char *p = op->name; *p; p++) c.seed = (c.seed ^ (uint8_t)*p) * 0x100000001b3ull;   /* FNV-1a */
    uint64_t s = c.seed;
    if (st) *st = (hx_stats){ 0, 0, 0, 0 };

    uint32_t n = (uint32_t)op->nargs, bits = 0;
    for (uint32_t j = 0; j < n; j++) bits += arg_w(&c, (int)j);
    int exhaustive = bits <= 8 && (1u << bits) <= HX_TUPLES;
    uint32_t ntuples = exhaustive ? 1u << bits : HX_TUPLES, nedge = 1;
    for (uint32_t j = 0; j < n && nedge < HX_TUPLES / 2; j++) nedge *= 5;
    if (nedge > HX_TUPLES / 2) nedge = HX_TUPLES / 2;

    for (uint32_t t = 0; t < ntuples; t++) {
        hx_val a[HX_MAX_ARGS];
        for (uint32_t j = 0, d = t, sh = 0; j < n; j++, d /= 5) {
            uint32_t wj = arg_w(&c, (int)j);
            if (exhaustive) a[j] = (hx_val){ { (t >> sh) & ((1u << wj) - 1) } };
            else if (t < nedge) a[j] = edge(d % 5, wj);
            else a[j] = rnd(&s, wj);
            sh += wj;
        }
        for (int oi = 0; oi < 2; oi++) {
            hx_val o0 = { { 0 } };
            if (oi) { o0 = rnd(&s, c.ow); if (hx_eq(o0, (hx_val){ { 0 } })) o0.l[0] = 1; }
            c.sample = 2 * t + (uint32_t)oi;
            if (st) st->samples++;
            for (int unc = 0; unc <= (op->unc != NULL); unc++)
                for (int quantum = 1; quantum >= 0; quantum--) {
                    hx_fail f = run1(&c, a, o0, quantum, unc, st);
                    if (f && !(flags & HX_NOABORT)) abort();
                    if (f) return f;
                }
        }
    }
    return HX_OK;
}

void hx_check_all(const hx_op *op) {
#define X(name, bits) hx_run(op, bits, 0, NULL);
    CQ_WIDTHS(X)
#undef X
}
