/* libcqops — public API.
 *
 * The surface named in PRD §14 (context, sink, config) is filled in as the
 * modules land: the qubit pool and its ceiling at Step 4 (D2), the sink vtable
 * at Step 5 (PRD §8). At Step 1 this header carries only the version and the
 * documented build-configuration contract, which is what the skeleton test
 * links against.
 *
 * Naming (PRD §14): `cqops_` for public symbols, `cq_` for internal ones.
 */
#ifndef CQOPS_CQOPS_H
#define CQOPS_CQOPS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CQOPS_VERSION_MAJOR 0
#define CQOPS_VERSION_MINOR 1
#define CQOPS_VERSION_PATCH 0
#define CQOPS_VERSION_STRING "0.1.0"

/* CQOPS_DEBUG_INVARIANTS
 *
 * Defined to 1 by the Debug configuration only (IMPLEMENTATION_PLAN §2.1). It
 * gates the *checking* machinery — the I2 owner map, the I6 scratch-extent
 * assert, the operand-distinctness asserts — and nothing else.
 *
 * It must never gate behaviour. Debug and Release have to emit the identical
 * gate stream, because Release is the configuration whose gate counts get
 * pinned (CLAUDE.md, Rule 17) while Debug is where the invariants are
 * enforced; if the two diverged, neither claim would cover the other.
 */

/* The version this library was built as; always equals CQOPS_VERSION_STRING.
 * Comparing the two is how a caller catches a stale libcqops.a on the link
 * line against a newer header. */
const char *cqops_version_string(void);

/* --- Sinks (PRD §8, M04) -------------------------------------------------
 *
 * The one place a gate leaves the library. Emission is a stream, not a
 * structure (NORTH_STAR §4): libcqops holds no circuit object, no gate list
 * and no statevector, and a gate pushed through one of these pointers is gone
 * from our side. What the sink does with it — print it, count it, hand it to
 * the QEC layer, record it for a test — is the sink's business, and that is
 * what lets one small library serve all four with no duplication.
 *
 * THIS SHAPE IS FROZEN (Step 0.7). Six entries, and there is no `h`: cqrt_h
 * proved to be an over-declaration in CQ_lang — declared and defined there,
 * emitted by nothing and called by nothing — so it is struck from PRD §1, and
 * §12's Grover builds H out of rotations rather than as a primitive. Adding a
 * seventh entry on a guess would fork us from CQ_lang's frozen ABI.
 *
 * `ry`/`rz` carry a raw `double` all the way down. Converting an angle to
 * whatever representation a backend wants is the SINK's problem — angle
 * representation is explicitly not ours (Key Prohibitions). `mz` is terminal:
 * CQ_lang emits no adjoint and no cqrt_free for a measured handle. */
typedef struct {
    void (*x)  (void *u, uint32_t q);
    void (*cx) (void *u, uint32_t c, uint32_t t);
    void (*ccx)(void *u, uint32_t a, uint32_t b, uint32_t t);
    void (*ry) (void *u, uint32_t q, double theta);
    void (*rz) (void *u, uint32_t q, double phi);
    void (*mz) (void *u, uint32_t q);
    void *user;
} cq_sink;

/* Selects the active sink explicitly, overriding the CQOPS_SINK default.
 * NULL clears the override and returns to the environment's choice. The sink
 * is borrowed, not copied: it must outlive its use. */
void cqops_set_sink(const cq_sink *s);

#ifdef __cplusplus
}
#endif

#endif /* CQOPS_CQOPS_H */
