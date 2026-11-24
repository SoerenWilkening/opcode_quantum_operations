/* Derivation notes (BACKEND_ABI §1.10 "Objects", Derivation): the value rule, the union and the template
 * rules; the copy rule is in src/core.c, the load and cell rules in src/bus.c and src/qram.c. The data flag ("Data") is here too. A stored note is valid
 * only while the rail's bits equal its snapshot and no gate has targeted one of its qubits since (the
 * emitter's stamps), so every operation without a rule drops it, in place ones included. */
#include <stdio.h>
#include <stdlib.h>
#include "cqrt.h"
#include "cq_bus.h"
#include "kernels.h"

/* t: the address note (64-bit rails). p, alt: the content ("Data"): what the rail can hold, n = 0 data,
 * n > 0 may hold bytes of a pointer into those objects, no content or n < 0 unknown. dead: cqrt_qram_unbound;
 * ptr: cqrt_qram_pointer. b: the rail's bits when noted; stamp 0: never. */
typedef struct { cq_note t, p; uint64_t stamp; int dead, alt, ptr, dat; cq_bit b[]; } stored;   /* dat: cqrt_qram_data */
static stored *slot(int32_t h) {
    stored **s = (stored **)cq_rail_note(h);
    if (!*s && !(*s = calloc(1, sizeof **s + cq_rail_width(h) * sizeof(cq_bit)))) { fprintf(stderr, "cq_runtime: note: out of memory\n"); abort(); }
    return *s;
}
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

/* What was stored beside h still describes it: the bits are the snapshot's and no gate targeted one since. */
static int valid(int32_t h, const stored *s) {
    const cq_bit *b = cq_rail_bits(h);
    if (!s || !s->stamp) return 0;
    for (uint32_t i = 0, w = cq_rail_width(h); i < w; i++)
        if (b[i].kind != s->b[i].kind || (cq_bit_is_q(b[i]) && (b[i].q != s->b[i].q || cq_qubit_stamp(b[i].q) >= s->stamp)))
            return 0;
    return 1;
}
static void snap(int32_t h, stored *s) {
    s->stamp = cq_gate_stamp() + 1;
    for (uint32_t i = 0, w = cq_rail_width(h); i < w; i++) s->b[i] = cq_rail_bits(h)[i];
}
/* 1 and the value if h is classical-valued (its low 64 bits). */
static int classical(int32_t h, uint64_t *v) {
    const cq_bit *b = cq_rail_bits(h);
    *v = 0;
    for (uint32_t i = 0, w = cq_rail_width(h); i < w; i++) {
        if (cq_bit_is_q(b[i])) return 0;
        if (i < 64) *v |= (uint64_t)(b[i].kind & 1) << i;
    }
    return 1;
}

cq_note cq_note_get(int32_t h) {
    const stored *s = *cq_rail_note(h);
    uint64_t v;
    if (cq_rail_width(h) != 64 || (s && s->dead)) return none;
    if (classical(h, &v)) return cq_note_value(v);
    return valid(h, s) ? s->t : none;
}

/* ABI §1.10 "Data". A classical 64-bit rail: by its value, data if it is in no object. Otherwise what a
 * load, copy, cast, select or template left beside it; without that a classical rail is a seed (data) and
 * a rail with a qubit is unknown. */
cq_note cq_note_content(int32_t h) {
    const stored *s = *cq_rail_note(h);
    const cq_note data = { 0, { { 0, 0 } } };
    uint64_t v;
    int cl = classical(h, &v);
    if (s && s->dat) return data;   /* the compiler said so: whatever the derivation gave */
    if (cl && cq_rail_width(h) == 64) { cq_note t = cq_note_value(v); return t.n > 0 ? t : data; }
    if (s && s->alt && valid(h, s)) return s->p;
    return cl ? data : none;
}
int cq_data(int32_t h) { return cq_note_content(h).n == 0; }
void cqrt_qram_data(uint32_t h) { slot((int32_t)h)->dat = 1; }

void cq_note_set(int32_t h, const cq_note *t) {
    stored *s = slot(h);
    if (s->dead) return;
    s->t = *t;
    s->alt = 0;
    snap(h, s);
}

/* After cq_note_set where the rail has an address note too. cqrt_qram_pointer makes the content the
 * address note ("A pointer load"), whichever of the two comes first (inside a cq_invert region the load
 * runs after the call). */
void cq_content_set(int32_t h, const cq_note *c) {
    stored *s = slot(h);
    if (!valid(h, s)) { s->t = none; snap(h, s); }
    s->p = *c; s->alt = 1;
    if (s->ptr && !s->dead) s->t = *c;
}
cq_note cq_content_all(const uint32_t *h, int n) {
    cq_note t = { 0, { { 0, 0 } } };
    for (int i = 0; i < n; i++) if (!cq_data((int32_t)h[i])) return none;
    return t;
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

void cqrt_qram_pointer(uint32_t h) {
    if (cq_rail_width((int32_t)h) != 64) return;
    stored *s = slot((int32_t)h);
    s->ptr = 1;
    if (s->alt && !s->dead) s->t = s->p;
}

/* ABI §1.10 "No object": h carries no note from now on, whatever its value or a later rule says. */
void cqrt_qram_unbound(uint32_t h) {
    if (cq_rail_width((int32_t)h) != 64) return;
    cq_note_set((int32_t)h, &none);
    (*(stored **)cq_rail_note((int32_t)h))->dead = 1;
}
