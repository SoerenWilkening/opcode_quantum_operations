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

#ifdef __cplusplus
}
#endif

#endif /* CQOPS_CQOPS_H */
