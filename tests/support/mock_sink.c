/* tests/support/mock_sink.c — see mock_sink.h. */

#include "support/mock_sink.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void mock_die(const char *what)
{
    fprintf(stderr, "mock_sink: FATAL: %s\n", what);
    abort();
}

static void push(cq_mock *m, cq_rec r)
{
    if (m->n == m->cap) {
        size_t cap = m->cap ? m->cap * 2u : 256u;
        cq_rec *v = realloc(m->v, cap * sizeof *v);
        if (!v) mock_die("out of memory recording the stream");
        m->v = v;
        m->cap = cap;
    }
    m->v[m->n++] = r;
    m->by_op[r.op]++;
}

/* The six §8 entries. `u` is the cq_mock; nothing here is global, which is
 * what lets a suite hold several recorders at once. */
static void on_x  (void *u, uint32_t q)
{ cq_rec r = { CQ_OP_X, 0u, 0u, q, 0.0 }; push(u, r); }

static void on_cx (void *u, uint32_t c, uint32_t t)
{ cq_rec r = { CQ_OP_CX, c, 0u, t, 0.0 }; push(u, r); }

static void on_ccx(void *u, uint32_t a, uint32_t b, uint32_t t)
{ cq_rec r = { CQ_OP_CCX, a, b, t, 0.0 }; push(u, r); }

static void on_ry (void *u, uint32_t q, double theta)
{ cq_rec r = { CQ_OP_RY, 0u, 0u, q, theta }; push(u, r); }

static void on_rz (void *u, uint32_t q, double phi)
{ cq_rec r = { CQ_OP_RZ, 0u, 0u, q, phi }; push(u, r); }

static void on_mz (void *u, uint32_t q)
{ cq_rec r = { CQ_OP_MZ, 0u, 0u, q, 0.0 }; push(u, r); }

void cq_mock_init(cq_mock *m)
{
    m->v = NULL;
    m->n = 0;
    m->cap = 0;
    memset(m->by_op, 0, sizeof m->by_op);
}

void cq_mock_dispose(cq_mock *m)
{
    free(m->v);
    cq_mock_init(m);
}

void cq_mock_reset(cq_mock *m)
{
    m->n = 0;
    memset(m->by_op, 0, sizeof m->by_op);
}

cq_sink cq_mock_sink(cq_mock *m)
{
    cq_sink s;
    s.x = on_x;
    s.cx = on_cx;
    s.ccx = on_ccx;
    s.ry = on_ry;
    s.rz = on_rz;
    s.mz = on_mz;
    s.user = m;
    return s;
}

size_t cq_mock_count(const cq_mock *m) { return m->n; }

size_t cq_mock_count_op(const cq_mock *m, cq_op op)
{
    if (op < 0 || op >= CQ_N_OPS) mock_die("count_op: bad op");
    return m->by_op[op];
}

const cq_rec *cq_mock_at(const cq_mock *m, size_t i)
{
    if (i >= m->n) mock_die("at(): index past the end of the stream");
    return &m->v[i];
}

/* Angles compare bitwise: -0.0 and 0.0 are == in C but are different gates to
 * emit, and a golden that could not tell them apart would be no golden. */
static int rec_eq(const cq_rec *a, const cq_rec *b)
{
    return a->op == b->op && a->a == b->a && a->b == b->b && a->t == b->t
        && memcmp(&a->angle, &b->angle, sizeof a->angle) == 0;
}

int cq_mock_matches(const cq_mock *m, const cq_rec *want, size_t n)
{
    if (m->n != n) return 0;                 /* length is part of the claim */
    for (size_t i = 0; i < n; i++)
        if (!rec_eq(&m->v[i], &want[i])) return 0;
    return 1;
}

void cq_mock_dump(const cq_mock *m, const char *label)
{
    printf("# %s: %zu gate(s)\n", label ? label : "stream", m->n);
    for (size_t i = 0; i < m->n; i++) {
        const cq_rec *r = &m->v[i];
        switch (r->op) {
        case CQ_OP_X:   printf("#  %4zu  x   q%u\n",         i, r->t); break;
        case CQ_OP_CX:  printf("#  %4zu  cx  q%u -> q%u\n",  i, r->a, r->t); break;
        case CQ_OP_CCX: printf("#  %4zu  ccx q%u,q%u -> q%u\n",
                               i, r->a, r->b, r->t); break;
        case CQ_OP_RY:  printf("#  %4zu  ry  q%u, %.17g\n",  i, r->t, r->angle); break;
        case CQ_OP_RZ:  printf("#  %4zu  rz  q%u, %.17g\n",  i, r->t, r->angle); break;
        case CQ_OP_MZ:  printf("#  %4zu  mz  q%u\n",         i, r->t); break;
        default:        printf("#  %4zu  <bad op %d>\n",     i, (int)r->op); break;
        }
    }
    printf("# by kind: x=%zu cx=%zu ccx=%zu ry=%zu rz=%zu mz=%zu\n",
           m->by_op[CQ_OP_X], m->by_op[CQ_OP_CX], m->by_op[CQ_OP_CCX],
           m->by_op[CQ_OP_RY], m->by_op[CQ_OP_RZ], m->by_op[CQ_OP_MZ]);
}
