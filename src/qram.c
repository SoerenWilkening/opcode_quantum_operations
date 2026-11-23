/* The memory bus with a quantum address (BACKEND_ABI §1.10, issue kmgy.21): the fan-out decoder
 * and the leaf operations of docs/QRAM_CONSTRUCTION.md §2.2, §2.3 and §3, in that note's names.
 * The reachable set is the 2^q assignments of the address rail's q qubits, or, where the rail's note
 * names objects and that gives fewer leaves, the object bound (§11). Every ancilla is
 * taken from one tape A and released only when the call ends, so no ancilla is reused within the
 * call: a reused qubit would carry its last layer and break the note's schedule. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cq_config.h"
#include "cq_bus.h"
#include "kernels.h"

static void die(const char *msg, unsigned long long v) {
    fprintf(stderr, "cq_runtime: %s (%llu)\n", msg, v);
    abort();
}

static void *alloc(size_t n, size_t size) {
    void *p = calloc(n ? n : 1, size);
    if (!p) die("bus: out of memory, elements", n);
    return p;
}

typedef struct { uint64_t a; uint32_t k; } ref;   /* a cell address and its (leaf, byte) index */
static int by_addr(const void *x, const void *y) {
    uint64_t a = ((const ref *)x)->a, b = ((const ref *)y)->a;
    return (a > b) - (a < b);
}

typedef struct {
    uint32_t N, S, wb;     /* leaves; cells and bits per leaf (wb: 8S for a store, else w) */
    cq_bit **G;            /* the flag plane: G[a*wb + i] */
    cq_bit *cell;          /* 8 bits per distinct reachable cell */
    uint32_t *at;          /* at[a*S + j]: the distinct cell of byte j of leaf a */
    cq_tape *A;            /* owns every ancilla */
} qram;

static cq_bit *kappa(const qram *r, uint32_t a, uint32_t i) {
    return &r->cell[8 * r->at[a * r->S + i / 8] + i % 8];
}

/* t ^= flag ∧ cell bit for every unknown cell bit; X, n (first pass only): the parity tree inputs.
 * A known cell bit takes no gate: a 1 feeds its flag copy into the tree, a 0 is left out. */
static void and_pass(const qram *r, cq_bit *t, cq_bit **X, uint32_t *n) {
    for (uint32_t i = 0; i < r->wb; i++) {
        if (X) n[i] = 0;
        for (uint32_t a = 0; a < r->N; a++) {
            cq_bit *k = kappa(r, a, i), *g = r->G[(size_t)a * r->wb + i], *u = &t[(size_t)a * r->wb + i];
            if (cq_bit_is_q(*k)) cq_ccx(g, k, u);
            if (X && (cq_bit_is_q(*k) || k->kind)) X[(size_t)i * r->N + n[i]++] = cq_bit_is_q(*k) ? u : g;
        }
    }
}

/* LOAD(y): y_i ^= bit i of the flagged leaf. The AND is undone in its forward order, not
 * reversed: with overlapping leaves (§4.4) that is the order the note measured, 2(m-1) -> m-1. */
static void load(const qram *r, cq_bit *y, cq_bit *t, cq_bit **X, uint32_t *n) {
    cq_tape T = { 0 };
    and_pass(r, t, X, n);
    for (uint32_t i = 0; i < r->wb; i++)   /* TREE, in place: x[0] ends as the parity */
        for (uint32_t s = 1; s < n[i]; s *= 2)
            for (uint32_t m = 0; m + s < n[i]; m += 2 * s) cq_tape_cx(&T, X[(size_t)i * r->N + m + s], X[(size_t)i * r->N + m]);
    for (uint32_t i = 0; i < r->wb; i++)
        if (n[i]) cq_cx(X[(size_t)i * r->N], &y[i]);
    cq_tape_undo(&T);
    and_pass(r, t, NULL, NULL);
}

/* BCAST(s): cell bit i of every leaf ^= flag ∧ s_i, s_i copied to N places in U (from A). */
static void bcast(const qram *r, const cq_bit *s, cq_bit *U) {
    cq_tape C = { 0 };
    const cq_bit **u = alloc(r->N, sizeof *u);
    for (uint32_t i = 0; i < r->wb; i++) {
        u[0] = &s[i];
        for (uint32_t a = 1; a < r->N; a++) u[a] = &U[(size_t)i * (r->N - 1) + a - 1];
        for (uint32_t st = 1; st < r->N; st *= 2)
            for (uint32_t j = 0; j < st && j + st < r->N; j++) cq_tape_cx(&C, u[j], (cq_bit *)u[j + st]);
        for (uint32_t a = 0; a < r->N; a++) cq_ccx(r->G[(size_t)a * r->wb + i], u[a], kappa(r, a, i));
    }
    free(u);
    cq_tape_undo(&C);
}

/* FO-DECODE of the leaves x < n of the q bits b[0..q-1] (level l splits on b[l], so leaf x's bit q-1-l
 * is b[l]) from the token f[0]: f[x] = token ∧ [bits = x]. A node whose leaves are all >= n is never
 * split: it reads nothing and takes no gate. With n = 2^q this is the tree of §2.2 gate for gate. */
static void decode(cq_tape *A, cq_bit **f, const cq_bit *const *b, uint32_t q, uint32_t n) {
    cq_bit *fb = cq_tape_bits(A, n);
    const cq_bit **B = alloc(n, sizeof *B);
    for (uint32_t l = 0, nl = 1, k = 0; l < q; l++) {
        uint32_t sh = q - 1 - l, nx = (uint32_t)((n + (1ull << sh) - 1) >> sh), ns = nx - nl;   /* nodes next level; splits */
        const cq_bit *cp = ns > 1 ? cq_tape_fanout(A, b[l], ns - 1) : NULL;
        B[0] = b[l];
        for (uint32_t j = 1; j < ns; j++) B[j] = &cp[j - 1];
        for (uint32_t j = nl; j-- > 0;) {   /* per node y = f ∧ bit, f ^= y: f becomes child 2j (bit 0), y child 2j+1 */
            if (j < ns) {
                cq_bit *y = &fb[k++];
                cq_tape_ccx(A, f[j], B[j], y);
                cq_tape_cx(A, y, f[j]);
                f[2 * j + 1] = y;
            }
            f[2 * j] = f[j];
        }
        nl = nx;
    }
    free(B);
}

/* The object bound (ABI §1.10 "Objects"): per object X = [s, s + n) of the address's note,
 * d = addr - s, k = ceil(log2 n). Gate-free, the bits of d are tracked the way the adder folds them:
 * ripple carries over 0/1/unknown, unknown exactly where the bit is not constant in the address
 * qubits, so a bit known here is that constant in every basis state. The leaves are the offsets
 * L | (assignment of the unknown bits below k) that are < n: a prefix of the assignments, cnt of them.
 * The ripple loses classical address bits above a quantum one, so of those cnt only the keep leaves
 * whose address agrees with every classical address bit are built (the intersection the ABI states). */
typedef struct { uint64_t s, n, L, cnt, keep; uint32_t k, qn, P[64]; } object;
int cq_qram_force_bits;   /* test only: every access takes the bit-pattern bound */
uint32_t cq_qram_leaves;  /* test only: the leaves the last access built */

static uint64_t spread(uint64_t x, const object *o) {
    uint64_t v = o->L;
    for (uint32_t j = 0; j < o->qn; j++) v |= (x >> j & 1) << o->P[j];
    return v;
}

/* The objects that bound addr (0: none, take the bit pattern); *tot their leaves. */
static int fits(const object *o, uint64_t x, uint64_t base, uint64_t qm) { return !(((o->s + spread(x, o)) ^ base) & ~qm); }

static int objects(int32_t ah, const cq_bit *addr, uint64_t base, uint64_t qm, object *X, uint64_t *tot) {
    cq_note nt = cq_note_get(ah);
    int m = 0;
    *tot = 0;
    for (int e = 0; e < nt.n && !cq_qram_force_bits; e++) {   /* an end entry is never inside its object */
        uint64_t s, n;
        if (nt.e[e].end) continue;
        if (!cq_object_at(nt.e[e].a, &s, &n)) return 0;
        int dup = 0;
        for (int i = 0; i < m; i++) dup |= X[i].s == s;   /* registrations merged since noted */
        if (dup) continue;
        object *o = &X[m++];
        *o = (object){ s, n, 0, 0, 0, n > 1 ? 64 - (uint32_t)__builtin_clzll(n - 1) : 0, 0, { 0 } };
        int c = 1, out = 0;   /* d = addr + ~s + 1; 2: unknown */
        for (uint32_t i = 0; i < 64; i++) {
            int a = cq_bit_is_q(addr[i]) ? 2 : addr[i].kind, b = !(s >> i & 1), d = a == 2 || c == 2 ? 2 : a ^ b ^ c;
            c = a == 2 ? (c == b ? c : 2) : a == b ? a : c;
            if (i >= o->k) out |= d == 1;
            else if (d == 2) o->P[o->qn++] = i;
            else o->L |= (uint64_t)d << i;
        }
        if (out || o->L >= n) continue;   /* the address is never in X: no leaf */
        if (o->qn > 62) return 0;   /* a merged range of 2^63 cells or more: the bit pattern */
        uint64_t lo = 1, hi = 1ull << o->qn;   /* spread(0) = L < n; cnt: the first x with spread >= n */
        while (lo < hi) { uint64_t x = lo + (hi - lo) / 2; if (spread(x, o) < n) lo = x + 1; else hi = x; }
        o->cnt = o->keep = lo;   /* above the cap nothing is built: no need to count */
        if (lo <= 1ull << CQ_MAX_QADDR_BITS) for (uint64_t x = o->keep = 0; x < lo; x++) o->keep += fits(o, x, base, qm);
        *tot += o->keep;
    }
    return *tot ? m : 0;
}

void cq_qram_access(int op, const cq_bit *c, uint32_t ah, uint32_t hh, uint32_t w) {
    const cq_bit *addr = cq_rail_bits((int32_t)ah);
    cq_bit *h = cq_rail_bits((int32_t)hh);
    uint32_t pos[64], q = 0;
    uint64_t base = 0, qm = 0, tot, tree = 0;
    for (uint32_t i = 0; i < 64; i++)
        if (cq_bit_is_q(addr[i])) { pos[q++] = i; qm |= 1ull << i; } else base |= (uint64_t)addr[i].kind << i;
    object ob[CQ_NOTE_MAX];
    cq_bit av[CQ_NOTE_MAX][64], sb[CQ_NOTE_MAX][64], nb[CQ_NOTE_MAX][64];   /* operands the tape reads again at the undo */
    int m = objects((int32_t)ah, addr, base, qm, ob, &tot);
    if (m && ((q < 64 && tot >= 1ull << q) || tot > 1ull << CQ_MAX_QADDR_BITS)) m = 0;   /* not fewer leaves, or above the cap */
    if (!m && q > CQ_MAX_QADDR_BITS) {
        fprintf(stderr, "cq_runtime: bus: the address rail has %u quantum bits, above the cap CQ_MAX_QADDR_BITS = %d. "
                "The cap is this library's build-time resource limit (ancilla cost is O(w 2^q)), which ABI §1.10 permits\n",
                q, CQ_MAX_QADDR_BITS);
        abort();
    }
    uint32_t S = (w + 7) / 8, N = m ? (uint32_t)tot : 1u << q;
    cq_tape A = { 0 };
    qram r = { N, S, op == CQ_BUS_STORE ? 8 * S : w, NULL, NULL, NULL, &A };
    for (int j = 0; j < m; j++) if (ob[j].keep) tree += ob[j].cnt;   /* decode fills cnt flags before the keep are picked */
    uint64_t *la = alloc(N, sizeof *la);   /* leaf addresses */
    cq_bit **f = alloc(m ? (size_t)tree : N, sizeof *f);      /* their flags */

    if (!m) {   /* the bit pattern: token at the root, address bit q-1-l at level l */
        const cq_bit *b[64];
        for (uint32_t l = 0; l < q; l++) b[l] = &addr[pos[q - 1 - l]];
        f[0] = cq_tape_bits(&A, 1);
        if (c) cq_tape_cx(&A, c, f[0]); else cq_tape_x(&A, f[0]);
        decode(&A, f, b, q, N);
        for (uint32_t a = 0; a < N; a++) {
            la[a] = base;
            for (uint32_t k = 0; k < q; k++) la[a] |= (uint64_t)(a >> k & 1) << pos[k];
        }
    } else {
        /* Per object: d = addr - s by the adder sub ships at i64, recorded on A so the undo below
         * returns its qubits to the pool unmeasured. Several objects: the address fanned out so the
         * subtractions run side by side, and each root token also carries [d <u n]: an address in
         * one object can reach a leaf of another, or a node decode left unsplit (its 1-child pruned
         * covers both halves). [d >> k = 0] alone would not do for the latter. Disjoint objects:
         * one root fires. One object: in the domain d < n, so neither can happen. */
        static const cq_bit one = { CQ_BIT_ONE, 0 };
        int sub = (int)(cq_kernel_pick(CQ_KOP_ARITH_sub, 64) - cq_kernel_ops[CQ_KOP_ARITH_sub].cand), live = 0;
        for (int j = 0; j < m; j++) live += ob[j].keep > 0;
        for (uint32_t i = 0; i < 64; i++) {
            const cq_bit *cp = live > 1 && cq_bit_is_q(addr[i]) ? cq_tape_fanout(&A, &addr[i], (uint32_t)live - 1) : NULL;
            for (int j = 0; j < live; j++) av[j][i] = cp && j ? cp[j - 1] : addr[i];
        }
        uint32_t off = 0;
        for (int j = 0, u = 0; j < m; j++) {
            const object *o = &ob[j];
            if (!o->keep) continue;
            for (uint32_t i = 0; i < 64; i++) { sb[u][i] = cq_bit_const((int)(o->s >> i & 1)); nb[u][i] = cq_bit_const((int)(o->n >> i & 1)); }
            const cq_bit *d = cq_adder(sub, &A, av[u], sb[u], 1, &one, 64, CQ_ADD_SUM).s, *b[64];
            f[off] = cq_tape_bits(&A, 1);
            if (live > 1) {
                cq_bit *g = cq_tape_bits(&A, 1);
                cq_cmp(cq_cmp_kind(CQ_CMP_ULT, 64), &A, CQ_CMP_ULT, d, nb[u], 64, g, 1);
                if (c) cq_tape_ccx(&A, c, g, f[off]); else cq_tape_cx(&A, g, f[off]);
            } else if (c) cq_tape_cx(&A, c, f[off]); else cq_tape_x(&A, f[off]);
            u++;
            for (uint32_t l = 0; l < o->qn; l++) b[l] = &d[o->P[o->qn - 1 - l]];
            decode(&A, &f[off], b, o->qn, (uint32_t)o->cnt);
            for (uint32_t x = 0, o0 = off; x < o->cnt; x++)   /* a dropped leaf's flag never fires and reads nothing */
                if (fits(o, x, base, qm)) { f[off] = f[o0 + x]; la[off++] = o->s + spread(x, o); }

        }
    }
    cq_qram_leaves = N;
    if (getenv("CQ_QRAM_DEBUG")) {
        fprintf(stderr, "cq_qram: %s w=%u q=%u objects", op == CQ_BUS_LOAD ? "load" : op == CQ_BUS_STORE ? "store" : "swap", w, q);
        for (int j = 0; j < m; j++) if (ob[j].keep) fprintf(stderr, " [0x%llx +%llu]", (unsigned long long)ob[j].s, (unsigned long long)ob[j].n);
        fprintf(stderr, "%s leaves=%u\n", m ? "" : " none (bit pattern)", N);
    }

    /* The reachable cells, each read once: leaves that share a cell (§4.4) share its bits. */
    ref *rs = alloc((size_t)N * S, sizeof *rs);
    for (uint32_t a = 0; a < N; a++)
        for (uint32_t j = 0; j < S; j++) rs[a * S + j] = (ref){ la[a] + j, a * S + j };   /* wraps mod 2^64 */
    qsort(rs, (size_t)N * S, sizeof *rs, by_addr);
    r.cell = alloc((size_t)8 * N * S, sizeof *r.cell);
    r.at = alloc((size_t)N * S, sizeof *r.at);
    uint32_t nc = 0;
    uint8_t *cq = alloc((size_t)N * S, 1);   /* per distinct cell: QUANTUM */
    for (size_t k = 0; k < (size_t)N * S; k++) {
        if (!k || rs[k].a != rs[k - 1].a) { cq[nc] = (uint8_t)cq_cell_read(rs[k].a, &r.cell[8 * nc]); nc++; }
        r.at[rs[k].k] = nc - 1;
    }
    /* The load rule (cq_rt.h): into a |0> h, the union over the leaves of their 8 cells' notes. */
    int note = op == CQ_BUS_LOAD && w == 64 && cq_rail_zero((int32_t)hh);
    cq_note nt = { 0 };
    for (uint32_t a = 0; note && a < N && nt.n >= 0; a++) {
        cq_bit v[64];
        int quantum = 0;
        for (uint32_t i = 0; i < 64; i++) { v[i] = *kappa(&r, a, i); quantum |= cq[r.at[a * S + i / 8]]; }
        nt = cq_note_union(nt, cq_note_cells(v, quantum));
    }
    free(cq);

    /* PLANE: wb copies of each flag, so no flag controls two gates in one layer. */
    r.G = alloc((size_t)N * r.wb, sizeof *r.G);
    for (uint32_t a = 0; a < N; a++) {
        cq_bit *cp = r.wb > 1 ? cq_tape_fanout(&A, f[a], r.wb - 1) : NULL;
        r.G[(size_t)a * r.wb] = f[a];
        for (uint32_t i = 1; i < r.wb; i++) r.G[(size_t)a * r.wb + i] = &cp[i - 1];
    }
    free(f); free(la);

    cq_bit **X = alloc((size_t)N * r.wb, sizeof *X), *t = cq_tape_bits(&A, N * r.wb);
    uint32_t *n = alloc(r.wb, sizeof *n);
    if (op == CQ_BUS_LOAD) load(&r, h, t, X, n);
    else {
        /* Every reachable cell becomes QUANTUM (Overlay); a HOST bit gets its qubit from the
         * emitter when a gate first targets it. */
        cq_bit *U = cq_tape_bits(&A, r.wb * (N - 1));
        if (op == CQ_BUS_SWAP) { bcast(&r, h, U); load(&r, h, t, X, n); bcast(&r, h, U); }
        else {   /* store: ts = old cells, then cells ^= ts ^ v = old ^ v, so they hold v */
            cq_bit *ts = cq_rail_bits(cq_rail_alloc(8 * S, NULL));
            load(&r, ts, t, X, n);
            for (uint32_t i = 0; i < w; i++) cq_cx(&h[i], &ts[i]);
            bcast(&r, ts, U);
            for (uint32_t i = 0; i < w; i++) cq_cx(&h[i], &ts[i]);
        }
        for (size_t k = 0; k < (size_t)N * S; k++)
            if (!k || rs[k].a != rs[k - 1].a) cq_cell_write(rs[k].a, &r.cell[8 * r.at[rs[k].k]], 1);
    }
    cq_tape_undo(&A);   /* PLANE and DECODE reversed; every ancilla back to the pool */
    if (note) cq_note_set((int32_t)hh, &nt);
    free(X); free(n); free(r.G); free(r.cell); free(r.at); free(rs);
}

/* Objects (ABI §1.10 "Objects"): disjoint [start, end) ranges sorted by start, so ends are sorted too.
 * A registration that reaches 2^64 (start + size wraps) is ignored, not clamped: no C object does, and
 * a missing object only costs qubits. So end = start + size always fits and is > 0. */
typedef struct { uint64_t s, e; } obj;
static obj *O;
static size_t nO, capO;

static size_t end_above(uint64_t a) {   /* the first object with end > a */
    size_t lo = 0, hi = nO;
    while (lo < hi) { size_t m = (lo + hi) / 2; if (O[m].e > a) hi = m; else lo = m + 1; }
    return lo;
}

void cqrt_qram_object(int64_t addr, int64_t size) {
    uint64_t s = (uint64_t)addr, e = s + (uint64_t)size;
    if (size <= 0 || e < s) return;
    size_t i = end_above(s), j = i;   /* O[i..j): the overlapping ones; touching ones stay separate */
    while (j < nO && O[j].s < e) j++;
    if (i < j) { if (O[i].s < s) s = O[i].s; if (O[j - 1].e > e) e = O[j - 1].e; }
    else if (nO == capO) O = realloc(O, (capO = capO ? 2 * capO : 64) * sizeof *O);
    if (!O) die("bus: out of memory, objects", nO);
    memmove(&O[i + 1], &O[j], (nO - j) * sizeof *O);
    nO += 1 - (j - i);
    O[i] = (obj){ s, e };
}

int cq_object_at(uint64_t a, uint64_t *start, uint64_t *size) {
    size_t i = end_above(a);
    if (i == nO || O[i].s > a) return 0;
    *start = O[i].s; *size = O[i].e - O[i].s;
    return 1;
}

int cq_object_ending_at(uint64_t a, uint64_t *start, uint64_t *size) {
    size_t i = a ? end_above(a - 1) : nO;
    if (i == nO || O[i].e != a) return 0;
    *start = O[i].s; *size = O[i].e - O[i].s;
    return 1;
}
