/* shim/cq_shim_trace.h — M26's ANNOTATION half (`bd 76r`, PRD §15 D21).
 *
 * THE DIVISION OF LABOUR IS THE WHOLE SHAPE AND IT IS THE FIRST THING TO GET
 * RIGHT: THIS LAYER EMITS ANNOTATIONS FOR OPERATIONS ONLY AND PRINTS NOTHING
 * FOR A GATE. `#REGISTER` lines and `# STAGE: op begin (…)` / `# STAGE: op end`
 * brackets are ours; every gate line, every `#PATCH`, and every
 * `# STAGE: logical CX begin/end`, merge, split and syndrome round is the QEC
 * LIBRARY's, written by its own `execute_gate` when we call `qec_cx`.
 * `qec/docs/HOST_LANGUAGE_HANDOFF.md` §7 states it as a prohibition — never
 * print gate lines — and two consequences follow.
 *
 *   1. THE ANNOTATION IS NOT A SINK AND MUST NOT BE ONE. M25's six vtable
 *      entries call `qec_*` and write no text at all. A `cq_sink` is handed a
 *      raw `uint32_t` and structurally never sees a handle, a width or an
 *      opcode name (PRD §8) — and `#REGISTER name=h5 type=i32 qubits=0,1,2` and
 *      `op begin (name=add, in=h1|h2, out=h5)` need exactly those three. So it
 *      lives HERE, at the shim boundary, which already knows all of them.
 *   2. THE HAZARD IS CONCRETE AND CHEAP TO HIT: pointing M23's printf sink at
 *      the same `FILE*` as the qec trace CORRUPTS EVERY TRACE. Our `cx(q0, q1)`
 *      is neither a conformant gate line (the library owns those, spelled
 *      `CX 0 17`) nor a conformant annotation, and handoff §9 makes an
 *      unrecognised line inside an opted-in trace a FATAL parse error — the
 *      pipeline aborts citing the line, no JSON, no HTML, no degraded render.
 *      The two are disjoint consumers and must never share a stream. What keeps
 *      them apart is that this layer's ONE activation test is
 *      `cq_sink_qec_trace()`, which is NULL under every other sink.
 *
 * WHAT THE CONTRACT DEMANDS, and each clause has a line below that satisfies it:
 * `#REGISTER name=<tok> type=<tok> qubits=<d>(,<d>)*`, ALL of them before the
 * first bracket; one flat `op begin` / bare `op end` pair around the ENTIRE
 * compiled expansion of each language-level operation; tokens matching
 * `[A-Za-z0-9_.$\[\]-]+`; payload exactly `(k=v, k=v)`; and FULL COVERAGE — a
 * gate line outside any bracket is a fatal parse error.
 *
 * THE THREE COLLISIONS WITH THIS REPO'S DESIGN ARE RESOLVED IN PRD §15 D21, AND
 * THE RESOLUTIONS ARE WHAT THIS FILE IMPLEMENTS RATHER THAN RE-DERIVES:
 *
 * (a) THE CONTRACT'S REGISTER MODEL IS STATIC AND OURS IS LAZY — resolved as
 *     LAZY WINS and the header is TWO-PASS. The conflict was never
 *     lazy-versus-eager: §3 wants EVERY `#REGISTER` before the FIRST bracket and
 *     CQ_lang emits `cqrt_alloc` throughout a program, so no runtime with
 *     mid-program allocation can satisfy it in one pass. The header is assembled
 *     at END OF PROGRAM and written ahead of the buffered body (M25's
 *     `.partial`), and once that is so, laziness costs nothing extra. Three
 *     rules follow and all three are here: a rail's line lists its FINAL index
 *     set (D6 never demotes, so the set only grows and is well defined at the
 *     end, and a bit not yet materialised during an early op simply contributes
 *     no lane, §6 rule 4); an ALL-CONSTANT rail (I4) gets NO LINE AT ALL, which
 *     is L5 made visible rather than a gap; and `cqrt_addc`'s transients and
 *     D7b's defensive copy stay UNREGISTERED — workspace, §6 rule 3 — so only
 *     handles CQ_lang received back get a line.
 *
 * (b) D4 RECYCLES INDICES AND THE CONTRACT FORBIDS IT ("every index belongs to
 *     at most one register") — resolved as THE CONTRACT WINS: under this sink
 *     indices are never reused, which M25's install hook sets as a POOL MODE
 *     next to D2's ceiling. That is not this file's doing and it is not
 *     optional: with recycling on, one index would appear in two `#REGISTER`
 *     lines and the viewer would fail loudly.
 *
 * (c) OPERATIONS DO NOT NEST (handoff §4, v3 policy), so the SANDWICH IS NOT
 *     EXPRESSIBLE — and under the division of labour above that is CORRECT
 *     rather than a loss. The forward / copyout / reverse halves are CIRCUIT
 *     STRUCTURE, and circuit structure is what the library narrates: they are
 *     already visible as the `# STAGE: logical CX begin/end` fences the viewer
 *     nests inside our one block. What we describe is the OPERATION. Brackets
 *     therefore go at the OUTERMOST shim entry point and nowhere else, and
 *     `cq_trace_op` hard-errors on a second open bracket rather than letting the
 *     violation reach the viewer.
 *
 * COVERAGE IS WIDER THAN `cq_template_*`. Every logical `qec_*` call including
 * `qec_mz` must sit inside a bracket, so `cq_runtime_gate.c`'s 30 direct-gate
 * symbols, `cqrt_free`'s cleanup, `cqrt_addc`'s transients and D7b's copy are
 * all inside one too. The rule this file is written to, and the reason it is
 * stated as a rule rather than applied case by case, is that it is CHECKABLE:
 * every `cqrt_*` and `cq_shim_*` entry point opens exactly one bracket. "Bracket
 * the ones that emit" is not checkable — `cqrt_addc` on an all-classical rail
 * emits nothing and on a poisoned one emits `6W−5` gates.
 */
#ifndef CQ_SHIM_TRACE_H
#define CQ_SHIM_TRACE_H

#include <stdint.h>

/* Installs the header hook if — and only if — the qec sink is bound with a
 * trace open. Called from cq_shim_ctx() immediately after cq_sink_qec_bind,
 * which is the one instant at which that is knowable and nothing has been
 * emitted yet. Inert in every other configuration and in a build without the
 * QEC library. */
void cq_trace_bind(void);

/* Drops the register map. EXISTS FOR TESTS, and for the same reason
 * cq_rec_reset does: the map is keyed by HANDLE, handles restart at 0 with a
 * fresh cq_ctx, and a second case would otherwise inherit the first case's
 * index sets under the same handle numbers — overlapping `qubits=` lists, which
 * is a fatal parse error rather than a wrong picture. */
void cq_trace_reset(void);

/* OPEN one operation bracket. `i0..i2` are the operands READ and `o0..o1` the
 * operands WRITTEN, as HANDLES; `CQ_REG_NONE` slots are skipped, and a key with
 * no surviving element is omitted entirely (handoff §4: a present-but-empty
 * `in=` is a fatal parse error). A handle may legitimately appear on both
 * sides — `cqrt_addc` is `h := h + imm`.
 *
 * THREE SLOTS IN AND TWO OUT IS THE MEASURED MAXIMUM, not a guess: `cqrt_cswap`
 * reads three (`ctrl`, `a`, `b`) and writes two, and a controlled template reads
 * three (`a`, `b`, the §9 flag) and writes one.
 *
 * IT ALSO SNAPSHOTS EVERY NAMED HANDLE, at BOTH ends of the bracket, and that is
 * how the D21 (a) header gets its content. At the CLOSE because that is when a
 * rail has finished materialising; at the OPEN because `cqrt_free` destroys its
 * operand inside the bracket, and a rail's final index set has to be taken while
 * the rail still exists. */
void cq_trace_op(const char *name, int32_t i0, int32_t i1, int32_t i2,
                 int32_t o0, int32_t o1);

/* The template family's opener. It exists so `shim/cq_template_impl.c` spends
 * one line per bracket instead of composing a name: the VARIANT — `_unc`, and
 * the §9 `_ctrl` — belongs in the display name, because an uncompute that
 * rendered identically to its forward would hide half of what the algorithm
 * view is for, and the two are otherwise indistinguishable at this level (they
 * name the same handles and run the same kernel). `ctrl` doubles as the third
 * read operand and as the suffix's discriminator. */
void cq_trace_op_tpl(const char *base, int is_unc, int32_t ctrl,
                     int32_t a, int32_t b, int32_t out);

/* CLOSE it. The closer is exactly `# STAGE: op end` — bare, no parens, no
 * payload; `op end (name=add)` matches nothing and breaks the trace (§4). */
void cq_trace_end(void);

/* Read-backs for tests. `cq_trace_open()` is the bracket depth, 0 or 1;
 * `cq_trace_ops()` counts brackets opened since the last reset. */
int      cq_trace_open(void);
uint32_t cq_trace_ops(void);

#endif /* CQ_SHIM_TRACE_H */
