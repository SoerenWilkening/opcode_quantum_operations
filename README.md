# opcode_quantum_operations

A C library that implements the logical circuit backend of CQ_lang's `spec/BACKEND_ABI.md`.
A program lowered by CQ_lang calls `cqrt_*` (runtime) and `cq_template_*` (integer operations);
this library turns each call into a stream of seven gate operations on integer qubit indices:

```c
void __quantum__qis__x__body   (uint32_t q);
void __quantum__qis__cnot__body(uint32_t control, uint32_t target);
void __quantum__qis__ccx__body (uint32_t c1, uint32_t c2, uint32_t target);
void __quantum__qis__ry__body  (double theta, uint32_t q);
void __quantum__qis__ry__ctl   (double theta, uint32_t control, uint32_t target);
void __quantum__qis__rz__body  (double theta, uint32_t q);
int  __quantum__qis__m__body   (uint32_t q);
```

The library does not define these. Whatever is linked last does: a simulator, a resource
counter, a hardware compiler. The repository ships three definitions: a classical replay backend
used by the tests, a trace backend that prints the circuit, and a counting backend.

The repository is independent of CQ_lang: it vendors nothing from it and builds without it.
Scope and goals are in [NORTH_STAR.md](NORTH_STAR.md); what is verified and what is not is in
[docs/COMPLETENESS.md](docs/COMPLETENESS.md).

## Build

Requirements: a C11 compiler and CMake 3.20 or newer. Nothing else.

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build -j4        # optional, about 40 s
```

This produces two static archives in `build/`:

| archive | contents |
|---|---|
| `libcq_backend.a` | the `cq_template_*` integer operations and the `cqrt_*` runtime: allocation, measurement, rotations, memory bus, tape, invert regions |
| `libcq_gates_replay.a` | a classical definition of the seven operations, with gate and depth counters |
| `libcq_gates_trace.a` | a definition that keeps no state and prints a qec-trace-viewer trace: to the file named by `CQ_TRACE`, else stdout |
| `libcq_gates_count.a` | a definition that stores nothing but counters and reports gates, depth and qubits: to the file named by `CQ_COUNT`, else stdout |

The build type defaults to Release with asserts on. Build-time settings (`-D<name>=<value>` at
configure time): `CQ_LADDER_N` (angle precision, default 53), `CQ_K2` (input bound, default
4096), `CQ_MAX_QADDR_BITS` (the default of Q, 22). Q is the cap on quantum address bits and the
memory size (ABI §1.10): the registered objects hold at most 2^Q cells. It is a run-time setting,
the environment variable `CQ_MAX_QADDR_BITS` (0..24).

The calling-convention tests compile LLVM IR with LLVM 19.1.7, the version CQ_lang pins. They
look for it at `/usr/local/opt/llvm@19` (override with `-DCQ_LLVM_PREFIX=<prefix>`) and are
skipped when it is missing.

Tested on x86-64 macOS only.

## Install

```sh
cmake --install build --prefix /usr/local
```

This copies the four archives to `<prefix>/lib` and the public headers (`cqrt.h`,
`cq_template.h`, `cq_gates.h`, `cq_replay.h`, `cq_widths.h`, `cq_cast_pairs.h`) to
`<prefix>/include`.

## Link

Link a lowered program against the backend archive, followed by a definition of the seven
operations:

```sh
clang program.o -L<prefix>/lib -lcq_backend -lcq_gates_replay -o program
```

To draw the circuit instead of replaying it, link `-lcq_gates_trace` and run with
`CQ_TRACE=program.trace`; qec-trace-viewer's `verify_view.py program.trace` renders it. That
backend measures 0 always, writes no angles, and its controlled-`ry` line (`CRY c t`) is not
in the viewer's contract yet, so the viewer skips it. Each ABI call is one collapsible box
(`add`, `ry`, `measure`) over registers named after their handles (`h1`, `h2`); the backend
tells the gate backend about them through three optional functions in `cq_gates.h`, which
other gate backends can ignore. The trace is written when the program exits.

### Counting gates, depth and qubits

To get the cost of a program without running or drawing it, link the counting backend:

```sh
clang program.o -L<prefix>/lib -lcq_backend -lcq_gates_count -o program
./program                      # the report goes to stdout when the program exits
CQ_COUNT=counts.txt ./program  # or to a file
```

The report is one `name value` line each:

```
gates 88
x 0
cnot 40
ccx 18
ry 12
ry_ctl 0
rz 0
m 18
depth 19
qubits 29
```

`gates` is the total and the seven lines after it are the count per operation. `depth` is the
number of layers when every gate is placed as early as its qubits allow. `qubits` is the number
of distinct qubit indices a gate touched; the backend reuses the index of a freed qubit, so
this is the width of the circuit, not the number of allocations. To read one number in a
script: `awk '$1 == "depth" { print $2 }' counts.txt`.

The backend keeps one number per qubit and no circuit, so memory does not grow with the gate
count. It measures 0 always, so a program that branches on a measurement is counted along the
0 branch, and a program that aborts writes no report.

Replace `-lcq_gates_replay` with your own gate backend. The order matters with GNU ld:
the backend calls the seven operations. On Linux add
`-ldl -lm`.

## How the circuits are built

- **Qubits are allocated lazily.** Every bit of a value is one of three things: a known 0, a
  known 1, or a qubit. Allocating a value takes no qubit. A gate whose controls are all known
  is folded away at once: a CNOT with a known-1 control becomes an X, and an X on a known bit
  just flips it. A bit becomes a qubit only when a gate with a quantum control targets it. A
  program on classical data therefore emits no gates at all.
- **Operations are out-of-place and clean up after themselves.** A template computes
  `o ^= f(a, b)`: it builds `f` in scratch qubits, copies the result into `o` with CNOTs, then
  runs the computation backwards so every scratch qubit returns to |0⟩ and the operands are
  unchanged.
- **Depth first.** Each operation has several constructions (for example a ripple-carry and a
  carry-lookahead adder). Their depth is measured per width with every operand quantum, and the
  shallowest one ships. The measurements and the choice are in
  [docs/DEPTH.md](docs/DEPTH.md).
- **Rotations and memory.** A rotation by a quantum angle reduces the angle to an integer
  with a reversible circuit, applies one controlled rotation per bit, and undoes the reduction
  ([docs/ANGLE_LADDER.md](docs/ANGLE_LADDER.md)). Memory with a quantum address goes through a
  fan-out QRAM of logarithmic depth ([docs/QRAM.md](docs/QRAM.md)).
- **Testing is classical.** The replay backend keeps one classical bit per qubit, so every
  circuit is checked against the plain C operation on sampled inputs, once with all operands
  forced to qubits and once with all of them classical. Rotation angles are not simulated.
