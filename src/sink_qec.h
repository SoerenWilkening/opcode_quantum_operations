/* src/sink_qec.h — M25: the QEC sink. PRD §8, §15 D19, D20, D21 (b).
 *
 * NORTH_STAR condition 5: one flag routes the same gate stream into `qec_*`.
 * The flag is `CQOPS_SINK=qec` (or `cqops_set_sink`), exactly as for the other
 * two sinks — there is no second mechanism and no second code path through the
 * library. This is also the ONLY route to a circuit drawing: the drawer is the
 * QEC repo's `scripts/draw_circuit.py`, and Rule 13 is why this repo grows no
 * second one (a gate is gone from our side once emitted; what a consumer does
 * with it is the consumer's business).
 *
 * WHERE THE LIBRARY IS. The name `C_quantum_error_correction` is a REPOSITORY,
 * not an artefact: the library is that repo's `qec/` subdirectory, built as
 * `libqec.a` with its header at `qec/qec.h`, so a `find_library` spelled from
 * the project name finds nothing. Configure with `-DCQOPS_QEC_DIR=<that qec/>`.
 *
 * THIS FILE COMPILES WHETHER OR NOT THE LIBRARY IS PRESENT, and that is not
 * decoration. Without it `cq_sink_qec_register` registers nothing, so
 * `CQOPS_SINK=qec` resolves to nothing and takes src/sink.c's EXISTING hard
 * error — "CQOPS_SINK names an unregistered sink" — rather than silently
 * falling back to printf. Quietly substituting a different sink hands the
 * caller a circuit they did not ask for; that posture is M04's and is inherited
 * here rather than restated in a `#if` at the call site. It also keeps the shim
 * free of build-configuration branches.
 *
 * FOUR OF THE SIX ENTRIES MAP 1:1 — x, cx, ccx, mz onto qec_x, qec_cx, qec_ccx,
 * qec_mz, all `uint32_t` — and `ry`/`rz` are BUILT rather than stubbed (§15
 * D19); see sink_qec_angle.h for the rational conversion and the .c for the Ry
 * conjugation and its emission order.
 *
 * EVERY `qec_*` RETURNS `int` WHERE OUR VTABLE RETURNS `void`, so a −1 aborts
 * through this module's own die. That is not defensive padding: `qec_ccx`
 * REFUSES coincident operands in BOTH configurations, while our §3 distinctness
 * asserts are Debug-gated — CLAUDE.md records a MEASURED Release run emitting
 * `ccx q0 q0 q2` under a D7b alias with no diagnostic. Under this sink that
 * stops being silent.
 *
 * THE INSTALL HOOK SETS TWO POOL MODES, NOT ONE (§15 D21 (b)).
 *
 *   D2's ceiling  = qec_n_logical(ctx). A HARD ceiling, not a formality: the
 *                   shipped configs carry n_logical ∈ {1,3,4,6,7} and the code
 *                   distance is DERIVED from an error budget that splits across
 *                   the logical qubits, so raising it raises d and the fabric
 *                   may not fit. K12's divrem at W = 8 alone wants 79 scratch
 *                   qubits. Exceeding it fails loud (D2).
 *   No recycling  = the trace contract's "every index belongs to at most one
 *                   register" (qec/docs/HOST_LANGUAGE_HANDOFF.md §3). Purely a
 *                   DISPLAY-model constraint: the qec patch is genuinely back at
 *                   |0⟩ after our free and would happily be reused. It is set
 *                   here because the sink needs it, NOT because it completes the
 *                   ceiling — see qubits.h for the tempting second motive that
 *                   was checked and is false.
 *
 * ONE MEASURED FACT ABOUT THE LIBRARY THAT MAKES THE CEILING LOAD-BEARING RATHER
 * THAN ADVISORY (2026-08-28): qec_x(ctx, 99) on an n_logical = 3 context returns
 * 0. There is no range check on a logical index anywhere in the API, so an
 * out-of-range `q` is not refused, it is ACTED ON. What keeps us inside the
 * range is the pool's own `peak == minted` identity plus D2 — which is why the
 * ceiling is set here and not left to a caller.
 *
 * IT WRITES NO TEXT, EVER, AND THAT IS STRUCTURAL (§15 D21). `#REGISTER` lines
 * and `# STAGE: op begin/end` brackets belong at the M26 shim boundary, which
 * knows the opcode, the handles and the widths a sink structurally never sees;
 * every gate line and every `#PATCH` is the LIBRARY's, written by its own
 * execute_gate. What this module owns of the trace is the `FILE*` and its
 * teardown — nothing else. `bd 76r` owns the annotations, and the two hooks at
 * the bottom of this header are the ENTIRE surface it needs: the stream to
 * write its lines to, and a place to hand back the header D21 (a) says can only
 * be assembled once the program has ended.
 */
#ifndef CQOPS_SINK_QEC_H
#define CQOPS_SINK_QEC_H

#include "cqops/cqops.h"
#include "qubits.h"

#include <stdio.h>

/* 1 iff the build found the QEC library. Lets a test SKIP with a reason rather
 * than pass vacuously — a suite that silently does nothing is a green run
 * claiming a link it never made. */
int cq_sink_qec_available(void);

/* Registers the name "qec". CHEAP AND ALWAYS SAFE TO CALL: no config is read,
 * no qec_ctx is created, and nothing is opened, so a process that never selects
 * this sink pays nothing and needs no config file. Idempotent — cq_sink_register
 * replaces by name. Registers nothing at all in a build without the library.
 *
 * MUST RUN BEFORE cq_ctx_init, because a context resolves cq_sink_active() ONCE
 * at construction (src/ctx.c). */
void cq_sink_qec_register(void);

/* The install hook. A NO-OP unless this sink is the one in force, which is what
 * makes "one flag" literally one flag; when it is, this reads CQOPS_QEC_CONFIG,
 * creates the context, opens the trace if CQOPS_QEC_TRACE names one, sets the
 * two pool modes above and arms the atexit teardown.
 *
 * MUST RUN AFTER cq_ctx_init, because the pool it configures is inside the
 * context. The two halves therefore bracket the construction; they are not one
 * call because there is no instant at which both preconditions hold.
 *
 * HARD ERRORS: CQOPS_QEC_CONFIG unset or empty, a config the library refuses,
 * an unopenable trace file, a CQOPS_QEC_PRECISION outside [0, 30]. */
void cq_sink_qec_bind(cq_qubit_pool *pool);

/* THE STREAM M26 WRITES ITS ANNOTATIONS TO, or NULL when this sink is not bound
 * or CQOPS_QEC_TRACE named nothing. Handoff §1 is explicit that the host writes
 * to *the same `FILE*` it passed to `qec_set_trace`* — there is no injector API
 * and none is needed — so this is a BORROW, not a transfer: M26 writes whole
 * lines between `qec_*` calls and never closes it.
 *
 * IT IS ALSO THE ONE ACTIVATION TEST FOR THE WHOLE ANNOTATION LAYER. NULL means
 * inert, which is what keeps M23's printf sink and the qec path disjoint
 * consumers of two different streams (§15 D21): our `cx(q0, q1)` is neither a
 * conformant gate line nor a conformant annotation, and an unrecognised line
 * inside an opted-in trace is a FATAL parse error for the viewer. */
FILE *cq_sink_qec_trace(void);

/* THE HEADER HOOK, AND THE DIRECTION OF THE DEPENDENCY IS THE POINT. §15 D21
 * (a) resolves the static-register conflict by assembling every `#REGISTER`
 * line AT END OF PROGRAM and writing it ahead of the buffered trace — but the
 * register map is M26's (a sink never sees a handle or a width), while the file
 * is this module's. So the composition happens here and the CONTENT arrives
 * through a callback M26 installs, rather than this Layer-4 module calling up
 * into Layer 5.
 *
 * `fn` is handed the FINAL file, open and empty, before a byte of the buffered
 * body is appended. It returns non-zero to SHIP and zero to REFUSE — the refusal
 * row is not decoration: a bracket printed with zero `#REGISTER` lines is a
 * fatal parse error (handoff §6, the two annotation kinds are a package), and
 * §1's posture is that a trace known to be non-conformant must not be shipped to
 * the viewer at all. On a refusal the `.partial` is left on disk and the final
 * name is not created. With no hook installed the teardown renames as before. */
void cq_sink_qec_set_header(int (*fn)(FILE *dst));

/* Destroys the context, closes the trace and renames the `.partial` into place.
 * Idempotent, and what the atexit handler calls. Exposed because a test needs a
 * deterministic teardown between cases, exactly as cq_shim_ctx_reset does.
 *
 * THE TRACE IS A NAMED `.partial` FILE RATHER THAN A tmpfile(), so an abort()
 * mid-program still leaves the partial trace on disk — the same reason M23
 * flushes per line. Only a clean teardown renames it. */
void cq_sink_qec_teardown(void);

/* The live `qec_ctx*` as an opaque pointer, or NULL when nothing is bound. A
 * test that includes <qec/qec.h> casts it to read qec_count / qec_n_logical;
 * this header does not include qec.h, so that the file compiles identically in
 * both builds and no consumer acquires the dependency by accident. */
void *cq_sink_qec_handle(void);

#endif /* CQOPS_SINK_QEC_H */
