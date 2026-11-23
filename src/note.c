/* Derivation notes (BACKEND_ABI §1.10 "Objects", Derivation): the value rule, the union and the template
 * rules; the copy rule is in src/core.c, the load rule in src/bus.c and src/qram.c. A stored note is valid
 * only while the rail's bits equal its snapshot and no gate has targeted one of its qubits since (the
 * emitter's stamps), so every operation without a rule drops it, in place ones included. */
#include <stdio.h>
#include <stdlib.h>
#include "cq_bus.h"
#include "kernels.h"

typedef struct { cq_note t; uint64_t stamp; cq_bit b[64]; } stored;
static const cq_note none = { -1, { { 0, 0 } } };

int cq_rail_zero(int32_t h) {
    const cq_bit *b = cq_rail_bits(h);
    for (uint32_t i = 0, w = cq_rail_width(h); i < w; i++) if (b[i].kind != CQ_BIT_ZERO) return 0;
    return 1;
}

cq_note cq_note_value(uint64_t v) {
    cq_note t = { 0, { { 0, 0 } } };
    uint64_t s, n;
    if (!v) return t;
    if (cq_object_at(v, &s, &n)) t.e[t.n++] = (cq_note_ent){ s, 0 };
    if (cq_object_ending_at(v, &s, &n)) t.e[t.n++] = (cq_note_ent){ s, 1 };
    return t.n ? t : none;
}

static uint64_t obj(uint64_t a) { uint64_t s, n; return cq_object_at(a, &s, &n) ? s : a; }   /* merges since noted */

cq_note cq_note_union(cq_note a, cq_note b) {
    if (a.n < 0 || b.n < 0) return none;
    for (int j = 0; j < b.n; j++) {
        int i = 0;
        while (i < a.n && obj(a.e[i].a) != obj(b.e[j].a)) i++;
        if (i < a.n) a.e[i].end &= b.e[j].end;   /* same object: end only if both have it */
        else if (a.n == CQ_NOTE_MAX) return none;
        else a.e[a.n++] = b.e[j];
    }
    return a;
}

cq_note cq_note_get(int32_t h) {
    const cq_bit *b = cq_rail_bits(h);
    if (cq_rail_width(h) != 64) return none;
    uint64_t v = 0;
    int q = 0;
    for (int i = 0; i < 64; i++) { q |= cq_bit_is_q(b[i]); v |= (uint64_t)(b[i].kind & 1) << i; }
    if (!q) return cq_note_value(v);
    const stored *s = *cq_rail_note(h);
    if (!s) return none;
    for (int i = 0; i < 64; i++)
        if (b[i].kind != s->b[i].kind || (cq_bit_is_q(b[i]) && (b[i].q != s->b[i].q || cq_qubit_stamp(b[i].q) > s->stamp)))
            return none;
    return s->t;
}

void cq_note_set(int32_t h, const cq_note *t) {
    stored **s = (stored **)cq_rail_note(h);
    if (!*s && !(*s = malloc(sizeof **s))) { fprintf(stderr, "cq_runtime: note: out of memory\n"); abort(); }
    (*s)->t = *t;
    (*s)->stamp = cq_gate_stamp();
    for (int i = 0; i < 64; i++) (*s)->b[i] = cq_rail_bits(h)[i];
}

/* add keeps the note of its one noted operand (null and unknown are not noted), the end flags only when
 * the other operand's bit 63 is classical 0; sub the minuend's, flags cleared; select the arms' union. */
cq_note cq_note_tpl(int op, const uint32_t *args) {
    if (op == CQ_KOP_SELECT) return cq_note_union(cq_note_get((int32_t)args[1]), cq_note_get((int32_t)args[2]));
    if (op != CQ_KOP_ARITH_add && op != CQ_KOP_ARITH_sub) return none;
    cq_note a = cq_note_get((int32_t)args[0]), b = cq_note_get((int32_t)args[1]);
    if ((a.n > 0) == (b.n > 0) || (op == CQ_KOP_ARITH_sub && b.n > 0)) return none;
    int keep = op == CQ_KOP_ARITH_add && cq_rail_bits((int32_t)args[a.n > 0])[63].kind == CQ_BIT_ZERO;
    cq_note t = a.n > 0 ? a : b;
    for (int i = 0; i < t.n; i++) t.e[i].end &= keep;
    return t;
}

cq_note cq_note_cells(const cq_bit *rb, int quantum) {
    uint64_t v = 0;
    for (int i = 0; i < 64; i++) v |= (uint64_t)(rb[i].kind & 1) << i;   /* HOST cells: classical bits */
    return quantum ? none : cq_note_value(v);
}
