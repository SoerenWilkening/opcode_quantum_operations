/* Depth-table measurement; see depth.h. */
#include <string.h>
#include "depth.h"

#define REGEN "cmake --build <build> --target depth_tables"

/* ---- Reference functions, one per registry operation (test code, so not in the runtime). */
static hx_val parity_ref(const hx_val *a, uint32_t w) {
    (void)w;
    uint64_t x = 0;
    for (uint32_t k = 0; k < CQ_LIMBS; k++) x ^= a[0].l[k];
    return (hx_val){ { (uint64_t)__builtin_parityll(x) } };
}
static int bit(const hx_val *a, uint32_t i) { return (int)(a->l[i / 64] >> (i % 64) & 1); }
static hx_val bswap_ref(const hx_val *a, uint32_t w) {
    hx_val r = { { 0 } };
    for (uint32_t i = 0; i < w; i++) r.l[i / 64] |= (uint64_t)bit(a, (w / 8 - 1 - i / 8) * 8 + i % 8) << (i % 64);
    return r;
}
static hx_val bitreverse_ref(const hx_val *a, uint32_t w) {
    hx_val r = { { 0 } };
    for (uint32_t i = 0; i < w; i++) r.l[i / 64] |= (uint64_t)bit(a, w - 1 - i) << (i % 64);
    return r;
}
/* Casts: the operand width of the current run is depth_from (depth.h). */
uint32_t depth_from;
static hx_val zext_ref(const hx_val *a, uint32_t w) { (void)w; return a[0]; }   /* the harness masks to the result width */
static hx_val sext_ref(const hx_val *a, uint32_t w) {
    hx_val r = a[0];
    if (bit(a, depth_from - 1)) for (uint32_t i = depth_from; i < w; i++) r.l[i / 64] |= 1ull << (i % 64);
    return r;
}
/* kmgy.10: x = a + b (sub: a + ~b + 1) on w + 1 bits; bit w is the carry, signed overflow is
 * sign(a) == sign(b') != sign(x). */
static hx_val arith_ref(const hx_val *a, uint32_t w, int sub, int what) {
    hx_val x, b = sub ? hx_xor(a[1], hx_ones(w)) : a[1];
    unsigned c = (unsigned)sub;
    for (uint32_t k = 0; k < CQ_LIMBS; k++) {
        unsigned __int128 t = (unsigned __int128)a[0].l[k] + b.l[k] + c;
        x.l[k] = (uint64_t)t;
        c = (unsigned)(t >> 64);
    }
    int carry = bit(&x, w), sa = bit(a, w - 1), ov = sa == bit(&b, w - 1) && bit(&x, w - 1) != sa;
    hx_val zero = { { 0 } }, smax = hx_ones(w - 1), smin = hx_xor(hx_ones(w), smax);
    switch (what) {
    case 0: return x;
    case 1: return (hx_val){ { (uint64_t)(sub ? !carry : carry) } };
    case 2: return (hx_val){ { (uint64_t)ov } };
    case 3: return sub ? (carry ? x : zero) : (carry ? hx_ones(w) : x);
    default: return ov ? (sa ? smin : smax) : x;
    }
}
#define ARITH_REF_(name, sub, what) static hx_val name##_ref(const hx_val *a, uint32_t w) { return arith_ref(a, w, sub, what); }
ARITH_REF_(add, 0, 0) ARITH_REF_(sub, 1, 0)
ARITH_REF_(uadd_overflow, 0, 1) ARITH_REF_(usub_overflow, 1, 1) ARITH_REF_(sadd_overflow, 0, 2) ARITH_REF_(ssub_overflow, 1, 2)
ARITH_REF_(uadd_sat, 0, 3) ARITH_REF_(usub_sat, 1, 3) ARITH_REF_(sadd_sat, 0, 4) ARITH_REF_(ssub_sat, 1, 4)

/* kmgy.13: bitwise; shifts by r = k mod W (CR §2) as funnels of the 2W-bit hi:lo. */
static hx_val and_ref(const hx_val *a, uint32_t w) { (void)w; return hx_and(a[0], a[1]); }
static hx_val xor_ref(const hx_val *a, uint32_t w) { (void)w; return hx_xor(a[0], a[1]); }
static hx_val or_ref(const hx_val *a, uint32_t w) { (void)w; return hx_xor(hx_xor(a[0], a[1]), hx_and(a[0], a[1])); }
static hx_val funnel_ref(hx_val hi, hx_val lo, const hx_val *k, uint32_t w, int left) {
    uint32_t r = 0;
    for (uint32_t i = w; i--;) r = (2 * r + (uint32_t)bit(k, i)) % w;
    hx_val o = { { 0 } };
    for (uint32_t i = 0; i < w; i++) {
        uint32_t j = left ? w + i - r : i + r;
        if (j < w ? bit(&lo, j) : bit(&hi, j - w)) o.l[i / 64] |= 1ull << (i % 64);
    }
    return o;
}
static const hx_val zero_;
static hx_val shl_ref(const hx_val *a, uint32_t w) { return funnel_ref(a[0], zero_, &a[1], w, 1); }
static hx_val lshr_ref(const hx_val *a, uint32_t w) { return funnel_ref(zero_, a[0], &a[1], w, 0); }
static hx_val ashr_ref(const hx_val *a, uint32_t w) { return funnel_ref(bit(&a[0], w - 1) ? hx_ones(w) : zero_, a[0], &a[1], w, 0); }
static hx_val fshl_ref(const hx_val *a, uint32_t w) { return funnel_ref(a[0], a[1], &a[2], w, 1); }
static hx_val fshr_ref(const hx_val *a, uint32_t w) { return funnel_ref(a[0], a[1], &a[2], w, 0); }

static const hx_ref_fn ref_of[CQ_KOP_COUNT] = {
    [CQ_KOP_AND] = and_ref, [CQ_KOP_OR] = or_ref, [CQ_KOP_XOR] = xor_ref,
    [CQ_KOP_SHL] = shl_ref, [CQ_KOP_LSHR] = lshr_ref, [CQ_KOP_ASHR] = ashr_ref,
    [CQ_KOP_FSHL] = fshl_ref, [CQ_KOP_FSHR] = fshr_ref,
#define ARITH_REF_OF_(name, comm, ow) [CQ_KOP_ARITH_##name] = name##_ref,
    CQ_ARITH_OPS(ARITH_REF_OF_)
    [CQ_KOP_PARITY] = parity_ref,
    [CQ_KOP_BSWAP] = bswap_ref,
    [CQ_KOP_BITREVERSE] = bitreverse_ref,
    [CQ_KOP_ZEXT] = zext_ref,
    [CQ_KOP_SEXT] = sext_ref,
    [CQ_KOP_TRUNC] = zext_ref,
};

static int better(const hx_stats *a, const hx_stats *b) {   /* strict: ties keep the earlier one */
    if (a->depth != b->depth) return a->depth < b->depth;
    if (a->qubits != b->qubits) return a->qubits < b->qubits;
    return a->gates < b->gates;
}

int depth_measure(const cq_kernel_op *op, hx_ref_fn ref, depth_cell cell[][CQ_NWIDTHS], uint8_t *choice) {
    if (op->ncand < 1 || op->ncand > DEPTH_MAX_CAND) {
        fprintf(stderr, "depth: %s: %d candidates (1..%d)\n", op->name, op->ncand, DEPTH_MAX_CAND);
        return -1;
    }
    int rc = 0;
    for (int wi = 0; wi < CQ_NWIDTHS; wi++) {
        int best = -1;
        choice[wi] = 0;
        if (!cq_kernel_has_width(op, cq_kernel_widths[wi])) continue;   /* the op does not exist here */
        for (int c = 0; c < op->ncand; c++) {
            const cq_kernel *k = &op->cand[c];
            hx_op h = { op->name, op->nargs, op->out_width, k->emit, k->unc ? k->unc : k->emit, ref };
            depth_cell *d = &cell[c][wi];
            if (op->width_step >= CQ_STEP_FROM_ABOVE) {   /* every operand width FROM on the other side of w: worst case of each cost */
                d->ok = 1;
                hx_max_samples = 100;   /* 105 operand widths per cell */
                d->st = (hx_stats){ 0 };
                for (int fi = 0; fi < CQ_NWIDTHS; fi++) {
                    if (op->width_step == CQ_STEP_FROM_BELOW ? fi >= wi : fi <= wi) continue;
                    hx_stats s = { 0 };
                    uint32_t aw[HX_MAX_ARGS] = { depth_from = cq_kernel_widths[fi] };
                    d->ok = d->ok && hx_run_w(&h, cq_kernel_widths[wi], aw, HX_NOABORT, &s) == HX_OK;
                    if (s.gates > d->st.gates) d->st.gates = s.gates;
                    if (s.depth > d->st.depth) d->st.depth = s.depth;
                    if (s.qubits > d->st.qubits) d->st.qubits = s.qubits;
                }
                hx_max_samples = 0;
            } else {
                hx_max_samples = op->samples;
                d->ok = hx_run(&h, cq_kernel_widths[wi], HX_NOABORT, &d->st) == HX_OK;
                hx_max_samples = 0;
            }
            if (d->ok && (best < 0 || better(&d->st, &cell[best][wi].st))) best = c;
        }
        if (best < 0) {
            fprintf(stderr, "depth: %s at i%u: no valid candidate\n", op->name, cq_kernel_widths[wi]);
            rc = -1;
            best = 0;
        }
        choice[wi] = (uint8_t)best;
    }
    return rc;
}

int depth_generate(FILE *cf, FILE *md, uint8_t choices[CQ_KOP_COUNT][CQ_NWIDTHS]) {
    static depth_cell cell[DEPTH_MAX_CAND][CQ_NWIDTHS];
    int rc = 0;
    fprintf(cf, "/* GENERATED by tools/depth_table from src/kernels/registry.c; do not edit.\n"
                " * Measurements: docs/DEPTH.md. Regenerate: " REGEN " */\n"
                "#include \"kernels.h\"\n\n"
                "const uint8_t cq_kernel_choice[CQ_KOP_COUNT][CQ_NWIDTHS] = {\n");
    fprintf(md, "# Depth tables\n\n"
                "Generated by `tools/depth_table` from `src/kernels/registry.c`; do not edit. "
                "Regenerate this file and `src/kernels/kernel_choice.c` with `" REGEN "`; "
                "the `depth` test fails while either is stale.\n\n"
                "**How to add a candidate:** write `void k(int32_t o, const int32_t *args, uint32_t w)` "
                "(o ^= f(args)) in `src/kernels/`, declare it in `kernels.h`, add "
                "`{ \"name\", k, NULL, \"source\" }` to its operation's list in `registry.c`, regenerate. "
                "A new operation also needs one enum line in `kernels.h`, one entry in `cq_kernel_ops`, "
                "and its reference function in `tools/depth.c`.\n\n"
                "Rule (NORTH_STAR, Depth first): per (operation, width) the least depth ships; ties go to "
                "fewer qubits, then fewer gates, then registration order. Cells are "
                "depth / gates / qubits of the operation alone, all operands forced to qubits, from the "
                "replay backend through the sampled harness (`hx_run`); **bold** is shipped; FAIL failed its "
                "sampled check and is never shipped.\n");
    for (int op = 0; op < CQ_KOP_COUNT; op++) {
        const cq_kernel_op *o = &cq_kernel_ops[op];
        uint8_t choice[CQ_NWIDTHS] = { 0 };
        if (!ref_of[op]) { fprintf(stderr, "depth: %s has no reference in tools/depth.c\n", o->name); rc = -1; }
        else if (depth_measure(o, ref_of[op], cell, choice)) rc = -1;
        if (choices) memcpy(choices[op], choice, sizeof choice);
        if (rc) continue;

        fprintf(cf, "    { /* %s */\n", o->name);
        fprintf(md, "\n## %s\n\n%s\n\n| width |", o->name, o->what);
        for (int c = 0; c < o->ncand; c++) fprintf(md, " %s |", o->cand[c].name);
        fprintf(md, "\n|---|");
        for (int c = 0; c < o->ncand; c++) fprintf(md, "---|");
        fprintf(md, "\n");
        for (int wi = 0; wi < CQ_NWIDTHS; wi++) {
            if (!cq_kernel_has_width(o, cq_kernel_widths[wi])) {   /* no row: the op does not exist at this width */
                fprintf(cf, "        0, /* i%u: not defined */\n", cq_kernel_widths[wi]);
                continue;
            }
            fprintf(cf, "        %u, /* i%u %s */\n", choice[wi], cq_kernel_widths[wi], o->cand[choice[wi]].name);
            fprintf(md, "| i%u |", cq_kernel_widths[wi]);
            for (int c = 0; c < o->ncand; c++) {
                const depth_cell *d = &cell[c][wi];
                const char *b = c == choice[wi] ? "**" : "";
                if (!d->ok) fprintf(md, " FAIL |");
                else fprintf(md, " %s%u / %llu / %u%s |", b, d->st.depth, (unsigned long long)d->st.gates, d->st.qubits, b);
            }
            fprintf(md, "\n");
        }
        fprintf(cf, "    },\n");
        fprintf(md, "\nSources:\n\n");
        for (int c = 0; c < o->ncand; c++) fprintf(md, "- %s: %s\n", o->cand[c].name, o->cand[c].source);
    }
    fprintf(cf, "};\n");
    return rc;
}
