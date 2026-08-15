/* src/sink_count.c — M24. Six counters and two derived numbers.
 *
 * Everything interesting about this module is in sink_count.h: what it counts,
 * what `total` deliberately leaves out, and why peak qubits is cq_qubits_peak()
 * in M03 rather than anything here. */

#include "sink_count.h"

#include "sink.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ONE GUARD, AND IT IS IN cq_sink_counter, NOT HERE. A counter sink with
 * nowhere to count is a gate dropped on the floor, which is the failure mode
 * that leaves every suite downstream green while verifying nothing — so it is
 * a hard error, exactly as src/sink.c treats a NULL vtable entry. But the six
 * entries below are `static`: the only way to install them is
 * cq_sink_counter, which refuses a NULL counter, so a second check here would
 * be unreachable. No test could reach it either, and this project does not
 * keep a guard that no case can turn red (CLAUDE.md's callout on guards that
 * a later — or here, an earlier — copy already catches). */
static cq_counter *at(void *u)
{
    return (cq_counter *)u;
}

static void ct_x  (void *u, uint32_t q) { (void)q; at(u)->x++; }
static void ct_mz (void *u, uint32_t q) { (void)q; at(u)->mz++; }

static void ct_cx (void *u, uint32_t c, uint32_t t)
{
    (void)c; (void)t;
    at(u)->cx++;
}

static void ct_ccx(void *u, uint32_t a, uint32_t b, uint32_t t)
{
    (void)a; (void)b; (void)t;
    at(u)->ccx++;
}

static void ct_ry (void *u, uint32_t q, double theta)
{
    (void)q; (void)theta;
    at(u)->ry++;
}

static void ct_rz (void *u, uint32_t q, double phi)
{
    (void)q; (void)phi;
    at(u)->rz++;
}

void cq_count_reset(cq_counter *c)
{
    if (!c) {
        fprintf(stderr, "libcqops: FATAL: counter reset on NULL\n");
        abort();
    }
    memset(c, 0, sizeof *c);
}

cq_sink cq_sink_counter(cq_counter *c)
{
    if (!c) {
        fprintf(stderr, "libcqops: FATAL: counter sink built on NULL\n");
        abort();
    }

    cq_sink s;
    s.x  = ct_x;  s.cx = ct_cx; s.ccx = ct_ccx;
    s.ry = ct_ry; s.rz = ct_rz; s.mz  = ct_mz;
    s.user = c;
    return s;
}

cq_counter *cq_sink_counter_register(void)
{
    /* Static for the same reason as M23's: the registry borrows, never copies,
     * so both the sink and the counter it points at must outlive it. */
    static cq_counter global;
    static cq_sink    s;

    s = cq_sink_counter(&global);
    cq_sink_register("counter", &s);
    return &global;
}

/* x + cx + ccx, and NOT ry/rz/mz — Bennett's circuits contain no such gate, so
 * folding them in would break the baseline comparison PRD §8 asks for, and
 * would break it only once §7 rotations fire. See sink_count.h. */
uint64_t cq_count_total(const cq_counter *c)
{
    return c->x + c->cx + c->ccx;
}

uint64_t cq_count_t(const cq_counter *c)
{
    return 7u * c->ccx;
}
