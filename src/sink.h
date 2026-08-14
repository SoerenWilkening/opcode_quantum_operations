/* src/sink.h — M04: dispatch and selection. PRD §8.
 *
 * The `cq_sink` vtable and `cqops_set_sink` are public and live in
 * <cqops/cqops.h>; what is here is the internal half — the six dispatch
 * helpers every emitter call goes through, and the name registry the
 * CQOPS_SINK default resolves against.
 *
 * WHY DISPATCH IS A FUNCTION AND NOT `s->x(s->user, q)` AT THE CALL SITE. One
 * reason only, and it is worth a call: a NULL vtable entry is caught and named
 * instead of jumping through a null pointer. PRD §8's qec sink "stubs" ry/rz,
 * and a stub is a no-op function — a hole in the table is a construction bug,
 * and dropping gates on the floor is the failure mode that leaves every suite
 * downstream green while verifying nothing.
 *
 * Layer 0, no internal dependencies (plan §3). Nothing here knows what a bit,
 * a qubit or a register is; a sink sees raw indices and angles.
 *
 * Selection is process-wide because PRD §8 spells it `cqops_set_sink()` with
 * no context argument, and because the environment default has to work with
 * no call into the library at all — CQ_lang's existing fixtures set an env var
 * and run. When `cq_ctx` arrives it borrows the active sink at construction;
 * it does not re-resolve per gate.
 */
#ifndef CQOPS_SINK_H
#define CQOPS_SINK_H

#include "cqops/cqops.h"

/* Room for PRD §8's three shipped sinks plus test doubles. Overflow is a hard
 * error rather than a silent grow: the set of sinks is fixed at build time,
 * so exceeding it means a caller is registering in a loop. */
#define CQ_SINK_MAX 8

/* The six §8 entries. Each aborts if the sink is NULL or its entry is. */
void cq_sink_x  (const cq_sink *s, uint32_t q);
void cq_sink_cx (const cq_sink *s, uint32_t c, uint32_t t);
void cq_sink_ccx(const cq_sink *s, uint32_t a, uint32_t b, uint32_t t);
void cq_sink_ry (const cq_sink *s, uint32_t q, double theta);
void cq_sink_rz (const cq_sink *s, uint32_t q, double phi);
void cq_sink_mz (const cq_sink *s, uint32_t q);

/* Registry. A sink is borrowed, never copied, so it must outlive the
 * registration. Registering a name twice replaces it — that is how a test
 * substitutes a recorder for the printf sink. */
void            cq_sink_register(const char *name, const cq_sink *s);
const cq_sink  *cq_sink_by_name(const char *name);

/* The sink in force: the cqops_set_sink override if there is one, else the
 * one CQOPS_SINK names, else "printf" (PRD §8's documented default). An
 * unset or empty CQOPS_SINK means "absent". Resolved on every call rather
 * than cached, so there is no stale-selection state to invalidate.
 *
 * Never returns NULL: a name that resolves to nothing is a hard error, since
 * quietly substituting a different sink would give the caller a circuit they
 * did not ask for, and returning NULL would only move the crash somewhere
 * less informative. */
const cq_sink *cq_sink_active(void);

/* Clears the registry and any override. Exists for tests, which need each
 * case to start from a known selection state. */
void cq_sink_reset(void);

#endif /* CQOPS_SINK_H */
