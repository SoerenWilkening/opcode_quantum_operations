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

/* PRD §15 D15 §3 — CQOPS_FREE_ABORT, a DEVELOPMENT AND CI FLAG, and it is
 * deliberately NOT the default.
 *
 * By default a free that cannot prove a qubit is |0⟩ STRANDS it: never
 * released, never on the free list, counted, and the program continues. That
 * covers both non-clean rows — the ones the library can SEE are dirty and the
 * ones it merely cannot clear — because Rule 6's hard error is about a RELEASE
 * of a non-|0⟩ index and neither row reaches the pool. This flag turns that
 * conviction back into termination, on demand, WITHOUT A REBUILD: it is how a
 * maintainer finds out that a caller stopped pairing its frees, rather than
 * discovering it as a slowly growing pool. Under it NORTH_STAR condition 1 is
 * unreachable by construction, which is the whole reason it is opt-in.
 *
 * MODELLED ON cqops_set_sink, including the part that is easy to drop: an
 * unresolvable CQOPS_FREE_ABORT value is a HARD ERROR, never a quiet
 * substitution. `CQOPS_FREE_ABORT=true` silently meaning OFF would be this
 * flag's worst failure — a maintainer who asked for termination and got
 * silence — so only "0" and "1" resolve and anything else aborts. An unset or
 * empty variable means ABSENT, which is off.
 *
 * `on` is 0 or 1 to force; any NEGATIVE value clears the override and returns
 * to the environment's choice, which is what NULL does for the sink. */
void cqops_set_free_abort(int on);

/* --- D15 §3's RESIDUE, from outside the archive (`bd c55`) ---------------
 *
 * WHAT A LINKED FIXTURE COULD SEE BEFORE THIS EXISTED: one line on stderr,
 * `libcqops: STRANDED: qubit qN of handle hM is …`, and nothing else. The
 * report is one-shot BY DESIGN — the residue spans a great many frees and a
 * line per stranded qubit buries the one line that matters — so the strongest
 * thing an L6 report could say about a fixture was a BOOLEAN: the line fired,
 * or it did not. That is a weaker sentence than D15 §3 shipped the machinery
 * to make, and closing the gap by making the report per-qubit is the forbidden
 * fix, not the missing one.
 *
 * SO THE RESIDUE IS READ, NEVER PRINTED. Modelled on cqops_set_sink and
 * cqops_set_free_abort — one process-wide function, no handle, no context — and
 * it is a pure READ: it adds no state, releases nothing, and cannot launder
 * anything onto the free list. It does not even MINT the process context; with
 * no context yet built, every field is zero, which is the truth (nothing has
 * been freed) rather than a placeholder.
 *
 * THE REJECTED ALTERNATIVE WAS AN atexit DUMP BEHIND AN ENVIRONMENT VARIABLE,
 * and it fails on TWO counts that are recorded on disk rather than reasoned
 * from. (i) It would write bytes into a stream the CALLER owns — and this
 * repository's own L6 driver classifies every fixture from that fixture's
 * stdout and stderr, so the library would be contaminating the artefacts its
 * harness reads. The posture is src/sink_printf.h's: installation is an
 * EXPLICIT ACT and deliberately not automatic. (ii) The natural spelling —
 * arming it from a constructor — is unreliable here for the reason
 * shim/cq_shim_ctx.c measured on this toolchain: libcqops is a STATIC library,
 * an object file nothing references is dropped at link time, and an
 * unreferenced archive member's constructor does not run, so the dump would
 * vanish silently on some link lines and not others. A caller that wants one
 * registers it itself, out of its OWN translation unit, where the linker cannot
 * drop it; tools/l6/l6_residue.c is where this repository does that for its own
 * harness.
 *
 * SIX FIELDS AND NOT ONE NUMBER, BECAUSE THE GRAINS GENUINELY DISAGREE
 * (`bd 06t`: "an implementation that reports a single residue figure has not
 * yet decided which row each free lands on"). Under D15 §4 proven-dirty and
 * unproven take the SAME ACT — never released, never on the free list, counted
 * — so the pool cannot tell them apart and `stranded_qubits` is one total by
 * construction. What survives the collapse is the VERDICT, at two grains:
 *
 *   - the QUBIT pair is D15 §3's residue, one entry per qubit that never came
 *     back;
 *   - the RAIL pair is one entry per free, by the rail's own verdict.
 *
 * NEITHER IS DERIVABLE FROM THE OTHER. A MIXED rail carrying one unproven
 * qubit and one convicted qubit adds to BOTH qubit rows and to the rail-level
 * DIRTY row ALONE, because the disposition's lattice makes dirty absorbing.
 * Collapsing the grains discards exactly that rail — which is the shape the
 * disposition fold's no-early-return was written to reach.
 *
 * THE QUBIT ROWS SUM TO `stranded_qubits`, AND THAT IS AN ASSERTION RATHER
 * THAN A COINCIDENCE: every increment sits beside the strand it describes. A
 * drift means a qubit stranded somewhere that did not go through the free
 * path. THE RAIL ROWS SUM TO NOTHING — a clean free is counted nowhere, since
 * an all-constant rail (I4) never reaches the proof at all, and a denominator
 * that silently included those would report the coverage inflation D15 §2
 * warns about. */
typedef struct {
    uint32_t stranded_dirty;     /* qubits CONVICTED, then stranded          */
    uint32_t stranded_unproven;  /* qubits merely unproven, then stranded    */
    uint32_t frees_dirty;        /* frees whose RAIL verdict was proven dirty*/
    uint32_t frees_unproven;     /* frees whose RAIL verdict was unproven    */
    uint32_t stranded_qubits;    /* the POOL's own total; == the qubit pair  */
    uint32_t strand_reports;     /* 0 before any strand, 1 for ever after    */
} cqops_residue;

/* Fills `out` from the process context. A NULL destination is a hard error,
 * never a silent no-op: a caller that asked for the residue and got nothing
 * back would report "no residue" for a program that leaked. */
void cqops_read_residue(cqops_residue *out);

#ifdef __cplusplus
}
#endif

#endif /* CQOPS_CQOPS_H */
