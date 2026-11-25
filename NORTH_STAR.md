# North Star

Draft 1, 2026-11-17.

## Goal

A C library that implements the logical circuit backend of CQ_lang's `spec/BACKEND_ABI.md`:
every `cqrt_*` call and every integer `cq_template_*` call becomes a stream of seven
operations on integer qubit indices. A lowered program links against it like any library.

This repository is independent of CQ_lang. It vendors nothing from it, builds without it and
tests without it. The ABI document is the only interface; where it and CQ_lang's
`CONSTRUCTION_RULES.md` disagree, the construction rules win.

## The seven operations

The library defines none of these. A gate backend linked at build time does.

```c
void __quantum__qis__x__body   (uint32_t q);
void __quantum__qis__cnot__body(uint32_t control, uint32_t target);
void __quantum__qis__ccx__body (uint32_t c1, uint32_t c2, uint32_t target);
void __quantum__qis__ry__body  (double theta, uint32_t q);
void __quantum__qis__ry__ctl   (double theta, uint32_t control, uint32_t target);
void __quantum__qis__rz__body  (double theta, uint32_t q);
int  __quantum__qis__m__body   (uint32_t q);     /* outcome, 0 or 1 */
```

- The names are QIR's (checked against qir-runner's exported symbols). Nothing else of QIR is
  used: no `%Qubit`, no `%Result`, no `%Array`, no runtime functions. A qubit is its index.
  The argument lists therefore differ from QIR's for `ry__ctl` and `m__body`.
- No other gate is ever emitted. No H, no S, no SWAP, no reset. Reset is `m` then `x` on
  outcome 1.
- The ABI's phase gate P(θ) is emitted as `rz`. The two differ by a global phase, and the ABI
  never applies P under a direct control (§1.6, §1.7 bracket it with CCX onto an ancilla).

## Qubits are allocated per bit, when a gate forces them

A rail is an array of bits, each exactly one of `ZERO`, `ONE`, `Q(index)` (`archive:src/bit.h`).
`cqrt_alloc_i64` allocates no qubit. All gates go through one emitter:

| call | controls classical | a control is a qubit |
|---|---|---|
| X(t) | t classical: flip the bit, no gate | |
| CX(c, t) | `if (c) X(t)` | t gets a qubit, CX emitted |
| CCX(a, b, t) | 0: nothing. Both 1: X(t) | one known control: 0 → nothing, 1 → CX. Else CCX; t gets a qubit |
| Ry(t), Ry-ctl | | t gets a qubit |

"Gets a qubit": take an index from the pool, emit X if the bit was `ONE`.

A bit returns to classical in two places only: `cqrt_measure` (the bits become the outcome,
the qubits go back to the pool) and `cqrt_free` (reset, back to the pool). `free` is trusted:
the ABI guarantees a known basis state, and the library keeps no shadow state to prove it.

## Scope

Ours:

- **Core (§1).** alloc, free, measure, ry, ry_qangle, rz_i1, rz_qangle, the i1 logic, copy,
  xorc, their `_controlled` forms, the in-place add/sub/addc (§1.15, integer widths), push/pop, invert_begin/end. Core gates accept the fp width
  tokens as bit patterns.
- **Angle ladder (§1.5, §1.7).** Classical angle rail: k computed on the host, one rotation.
  Quantum angle rail: the exact reduction circuit K ^= k(A), N controlled rotations, the
  reduction again. Both in the first version. Nothing on `archive` implements this
  (`archive:src/angle.c` only classifies a classical θ, and is not carried over); it is new
  work, composed from the integer kernels as §1.5 spells out. It owes, at design time: the
  choice of N, the binary expansion of 1/π to the needed length, and the guard-width search
  that makes k correctly rounded for all 2^64 inputs. Its test is classical: K against k(a)
  from an independent high-precision reference, on sampled doubles.
- **Memory bus (§1.10).** Classical address: direct access. Quantum address: log-depth
  QRAM with the fan-out decoder (depth 2q+1 against the bucket brigade's 3q, so the depth
  rule picks it). Design in `docs/QRAM.md`, gate lists in `docs/QRAM_CONSTRUCTION.md`. No access may fault: a HOST
  cell at an unreadable address reads 0, a store to an unwritable one makes the cell QUANTUM,
  and addresses wrap mod 2^64 (CR §2). The bus therefore probes host memory before touching it.
- **Tape and abort (§1.11).** Tape alloc/fmt/write, abort, effect entries that are classical
  (performed at the call) or deferred output, and the INPUT rows that merge under a quantum
  predicate, length, stream or buffer address. The ABI requires every row and the
  construction rules allow no declines; the INPUT merge rows are built after the rest.
- **Integer templates (§2.2–§2.8).** Arithmetic, bitwise, shifts, compare, select, integer
  casts, overflow, saturating, min/max, abs, bit counting. Each with its `_unc` and its
  literal shapes.
- **Widths.** `i1 i6 i7 i8 i16 i17 i24 i31 i32 i48 i64 i80 i100 i128 i200`, from one list:
  the test widths plus every integer width the lowered float library calls (ABI §2.12). A new
  width is one line.

Not ours:

- Every floating-point template, the fp casts, `fneg`/`fabs`/`copysign` included: the
  compiler's lowered float library (§2.12).
- Clones, the clone table, `__cq_alloc`, `__cq_mem`: the compiler's injection bundle.
- Any gate backend other than the classical replay below. No simulator, no optimiser, no
  error correction, no circuit object.

## Circuits

- Templates are `o ^= f(args)`, operands unchanged, ancillae back to |0⟩: compute into
  scratch, CX into the result, run the computation backwards.
- **Depth first.** For every integer operation and every width, the construction with the
  least circuit depth is the one shipped. Qubit count and gate count are secondary. The
  choice is per width: a ripple circuit that is shallower at a small width wins there, a
  carry-lookahead or tree circuit wins where it is shallower. Depth is measured by the replay
  backend with all operands forced to qubits, and the chosen construction per (operation,
  width) is recorded with its measured depth and its literature source, as `docs/QRAM.md`
  does for the bus.
- From `archive` we carry the bit, the emitter, the pool, the handle table and, as a starting
  point, the integer kernels. Its sink vtable, fp kernels, QEC sink and proof/certificate
  machinery are not carried over.
- Bennett.jl is a reference to read, with care: it is outdated. The ABI pins it at `824458d`,
  `archive` vendors `980805d`, and most of its emitters are ripple circuits that the depth
  rule replaces. Nothing is accepted on its provenance: a kernel is correct when it passes
  its sampled test against CR §2's totalization table, and shipped when it is the shallowest.
- `cq_invert`: while a region is open, the reversible ABI calls are recorded, not executed,
  and replayed in reverse at `end` with rotation angles negated. Recording gates would be
  wrong, because an operation on classical bits emits no gate. Allocations, measurements, bus
  stores and effect entries are not recorded: the ABI requires them to take effect at the
  call.

## Testing

Classical arithmetic only. Rotations are never tested.

- The replay backend is one bit per qubit: `x` flips, `cnot` XORs, `ccx` AND-XORs, `m` reads.
  `ry`, `ry__ctl` and `rz` do nothing; tests call `cqrt_ry_<W>(h, 0.0)` only to force qubits.
- Each operation and width: at most 1000 seeded samples of classical inputs, the measured
  result compared with the C operation (CR §2 totalization for the partial ones).
- Every sample runs with all operands forced to qubits, and with all operands classical. The
  classical run must emit zero gates and take zero qubits.
- After `_unc` the result measures 0 and the operands are unchanged.
- The replay backend counts gates and depth, so "zero gates" and "log depth" are assertions.

## Deliverables

`libcq_backend.a`, `libcq_gates_replay.a` and the test suite. C11, CMake. The one backend archive
replaces the two (`libcq_templates.a`, `libcq_runtime.a`) the ABI used to name; user decision
2026-11-18.

## Done when

1. A program that calls only ABI symbols links against `libcq_backend.a` plus any definition
   of the seven operations (user decision 2026-11-18: one archive, not the ABI's two).
2. Every in-scope symbol at every listed width passes its sampled test, forced-quantum and
   all-classical.
3. All-classical programs emit no gate.
4. A quantum-address bus access passes replay against a host byte array at depth O(q).
5. Every integer operation has its depth table: the candidates measured per width, the
   shallowest shipped.

Backend configuration, fixed at build time: the ladder precision N (chosen when the reduction
circuit is designed), the input bound K2 (default 4096), the cap on quantum address bits.
