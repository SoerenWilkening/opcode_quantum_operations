/* src/sink.c — M04. Six dispatch helpers, a name registry, CQOPS_SINK. */

#include "sink.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Hard errors, in BOTH configurations. Every one of them ends with a gate not
 * reaching a sink, which is the failure that leaves suites green while
 * verifying nothing — Rule 6's posture, applied to emission. */
static void cq_sink_die(const char *what, const char *detail)
{
    fprintf(stderr, "libcqops: FATAL: sink: %s (%s)\n", what,
            detail ? detail : "-");
    abort();
}

static void cq_sink_check(const cq_sink *s, const void *entry, const char *op)
{
    if (!s)     cq_sink_die("no sink", op);
    if (!entry) cq_sink_die("vtable entry is NULL", op);
}

void cq_sink_x(const cq_sink *s, uint32_t q)
{
    cq_sink_check(s, (const void *)s->x, "x");
    s->x(s->user, q);
}

void cq_sink_cx(const cq_sink *s, uint32_t c, uint32_t t)
{
    cq_sink_check(s, (const void *)s->cx, "cx");
    s->cx(s->user, c, t);
}

void cq_sink_ccx(const cq_sink *s, uint32_t a, uint32_t b, uint32_t t)
{
    cq_sink_check(s, (const void *)s->ccx, "ccx");
    s->ccx(s->user, a, b, t);
}

void cq_sink_ry(const cq_sink *s, uint32_t q, double theta)
{
    cq_sink_check(s, (const void *)s->ry, "ry");
    s->ry(s->user, q, theta);   /* double all the way down; no conversion here */
}

void cq_sink_rz(const cq_sink *s, uint32_t q, double phi)
{
    cq_sink_check(s, (const void *)s->rz, "rz");
    s->rz(s->user, q, phi);
}

void cq_sink_mz(const cq_sink *s, uint32_t q)
{
    cq_sink_check(s, (const void *)s->mz, "mz");
    s->mz(s->user, q);
}

/* --- Selection ----------------------------------------------------------- */

static struct {
    const char    *name;
    const cq_sink *sink;
} registry[CQ_SINK_MAX];

static size_t         n_registered;
static const cq_sink *override_sink;

void cq_sink_register(const char *name, const cq_sink *s)
{
    if (!name || !s) cq_sink_die("register needs a name and a sink", name);

    for (size_t i = 0; i < n_registered; i++) {
        if (strcmp(registry[i].name, name) == 0) {
            registry[i].sink = s;      /* replace: how a test substitutes one */
            return;
        }
    }

    if (n_registered == CQ_SINK_MAX)
        cq_sink_die("registry full", name);

    registry[n_registered].name = name;
    registry[n_registered].sink = s;
    n_registered++;
}

const cq_sink *cq_sink_by_name(const char *name)
{
    if (!name) return NULL;
    for (size_t i = 0; i < n_registered; i++)
        if (strcmp(registry[i].name, name) == 0) return registry[i].sink;
    return NULL;
}

void cqops_set_sink(const cq_sink *s)
{
    override_sink = s;                 /* NULL restores the env-var default */
}

const cq_sink *cq_sink_active(void)
{
    if (override_sink) return override_sink;

    const char *want = getenv("CQOPS_SINK");
    if (want && want[0] == '\0') want = NULL;   /* empty means absent */

    const cq_sink *s = cq_sink_by_name(want ? want : "printf");
    if (!s)
        cq_sink_die(want ? "CQOPS_SINK names an unregistered sink"
                         : "no default sink registered", want);
    return s;
}

void cq_sink_reset(void)
{
    n_registered = 0;
    override_sink = NULL;
}
