/* The handle table: handle h is slot h of a growing array; slot 0 is never used. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cq_gates.h"
#include "cq_rt.h"

typedef struct { uint32_t width; cq_bit *bits; } rail;   /* bits == NULL: free or never made */

static rail   *slot;
/* shortcut: a freed slot keeps its 16 bytes forever; reuse slots if handle churn ever matters. */
static int32_t n = 1, cap;    /* next handle; handles are never reused */

static void die(const char *msg, int64_t v) {
    fprintf(stderr, "cq_runtime: %s (%lld)\n", msg, (long long)v);
    abort();
}

int32_t cq_rail_alloc(uint32_t width, const uint64_t *v) {
    if (width == 0 || width > CQ_RAIL_MAX_WIDTH) die("rail width out of range", width);
    if (n == INT32_MAX) die("out of handles", n);
    if (n >= cap) {
        cap = cap ? 2 * cap : 64;
        if (!(slot = realloc(slot, (size_t)cap * sizeof *slot))) die("out of memory", cap);
    }
    cq_bit *b = malloc(width * sizeof *b);
    if (!b) die("out of memory", width);
    for (uint32_t i = 0; i < width; i++)
        b[i] = cq_bit_const(v && (v[i / 64] >> (i % 64)) & 1);
    slot[n] = (rail){ width, b };
    return n++;
}

static rail *get(int32_t h) {
    if (h <= 0 || h >= n || !slot[h].bits) die("bad or freed handle", h);
    return &slot[h];
}

cq_bit  *cq_rail_bits(int32_t h)  { return get(h)->bits; }
uint32_t cq_rail_width(int32_t h) { return get(h)->width; }

/* ---- Operation notes (cq_rt.h). The defaults; a gate backend's own definitions replace them. */
__attribute__((weak)) int  cq_gates_op_begin(const char *name) { (void)name; return 0; }
__attribute__((weak)) void cq_gates_op_end(void) {}
__attribute__((weak)) void cq_gates_rail(uint32_t h, uint32_t w, const uint32_t *q) { (void)h; (void)w; (void)q; }

static uint32_t op_h[6], op_depth;
static int      op_n, op_on;

static void op_rails(void) {
    for (int j = 0; j < op_n; j++) {
        int32_t h = (int32_t)op_h[j];
        if (h <= 0 || h >= n) continue;
        uint32_t q[CQ_RAIL_MAX_WIDTH], w = slot[h].bits ? slot[h].width : 0;
        for (uint32_t i = 0; i < w; i++) q[i] = cq_bit_is_q(slot[h].bits[i]) ? slot[h].bits[i].q : CQ_GATES_NO_QUBIT;
        cq_gates_rail((uint32_t)h, w, q);
    }
}

void cq_op_begin(const char *name, int unc, const uint32_t *h, int nh) {
    char buf[64];
    if (op_depth++) return;
    if (unc) { snprintf(buf, sizeof buf, "%s_unc", name); name = buf; }
    if (!(op_on = cq_gates_op_begin(name))) return;
    op_n = nh > 6 ? 6 : nh;
    memcpy(op_h, h, (size_t)op_n * sizeof *h);
    op_rails();
}

void cq_op_end(void) {
    if (--op_depth || !op_on) return;
    op_rails();
    cq_gates_op_end();
}

void cq_rail_free(int32_t h) {
    rail *r = get(h);
    cq_op_begin("free", 0, &(uint32_t){ (uint32_t)h }, 1);
    for (uint32_t i = 0; i < r->width; i++) cq_reset_bit(&r->bits[i]);
    free(r->bits);
    r->bits = NULL;
    cq_op_end();
}
