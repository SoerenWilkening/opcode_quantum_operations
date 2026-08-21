# North Star — `libcqops`, the quantum backend for CQ_lang

## The one-sentence goal

**A linkable C library that turns every placeholder call CQ_lang's IR pass emits into
a stream of logical `X` / `CX` / `CCX` / `Ry` / `Rz` operations — with provably
classical bits costing zero qubits and zero gates.**

When this is done, a plain C program compiled by `cqc` and linked against `libcqops`
prints (or hands to [`C_quantum_error_correction`](https://github.com/SoerenWilkening/C_quantum_error_correction))
the logical gate sequence of the quantum algorithm it describes — and Grover's algorithm,
written as ordinary C, runs end to end.

---

## The stack, and where we sit in it

```
        plain C source  (int a = 3; a = cq_theta(a, 0.5); int c = a + b;)
                │
                │  clang -O1 -emit-llvm   +  CQ.h
                ▼
        stock LLVM IR with @cq_theta / @cq_phi / @cq_measure calls
                │
                │  opt -passes=cq-lowering        ← CQ_lang, already built
                ▼
        calls to cqrt_* and cq_template_*         ← the ABI, already frozen
                │
                │  link                            ← ██ THIS REPO ██
                ▼
        libcqops: reversible circuits over X / CX / CCX (+ Ry / Rz)
                │
                │  gate sink (vtable)
                ▼
        printf  ·  gate counter  ·  qec_x / qec_cx / qec_ccx / qec_rz
                                                   ← C_quantum_error_correction
```

We own exactly one box. Above us the compiler decides *what* to compute and *when to
uncompute*; below us the QEC library decides *how many physical qubits a logical CX
costs*. We decide only: **which reversible gates realise this opcode, on which qubits.**

---

## The five commitments

### 1. Bennett.jl is the source of truth for every circuit

[Bennett.jl](https://github.com/tobiasosborne/Bennett.jl) has already solved the hard
part — correct, verified, ancilla-clean reversible constructions for the whole integer
opcode surface. We **port**, we do not re-derive. `src/adder.jl`, `src/multiplier.jl`,
`src/lowering/arith.jl`, `src/qrom.jl`, `src/controlled.jl` are specifications, and
their pinned gate counts are our regression baselines.

Corollary: "how should we build a reversible comparator?" is never an open question in
this repo. If a construction is missing, the answer is to look it up in Bennett.jl, not
to invent one.

### 2. One abstraction: the tri-valued bit

Every register bit is in exactly one of three states:

```c
BIT_ZERO            /* provably 0 — costs nothing        */
BIT_ONE             /* provably 1 — costs nothing        */
BIT_Q(qubit_index)  /* lives on a qubit                  */
```

A register is an array of these plus a width. **Qubits are allocated lazily, per bit,
at the moment a gate with a quantum control targets a classical bit** — never at
declaration, never per register.

This single representation *is* the classical shadow, *is* the quantum bitmask, and
*is* the wire vector Bennett.jl's circuits operate on. Bennett's `lower_add!` transcribes
line for line; the classical short-circuit falls out of the gate emitter, which folds
any gate whose controls are constants.

The payoff, concretely — `bool b` tainted, then `int a = 0; a |= b << 3;`:
one qubit allocated, one CX emitted, thirty-one bits free.

### 3. Ancilla-clean at every boundary

Every routine returns every scratch qubit to |0⟩ before it returns. CQ_lang's IR pass
frees only the named result rail and has no idea internal scratch exists, so a routine
that leaks a dirty ancilla is a silent miscompile — not a leak.

Where a Bennett construction is dirty by design (it relies on Bennett's *global*
forward–copy–reverse wrap), we apply the same construction **locally**: compute into
scratch, XOR the answer into the result rail, run the scratch computation backwards.
Bennett-in-the-small.

### 4. Emission is a stream, not a structure

No circuit object. No gate list. No statevector. A gate is emitted through a function
pointer and is gone. The backend's entire mutable state is: the handle table, the qubit
pool with its free list, and one classical shadow bit-pair per qubit.

This is what lets the same library serve a gate counter, a printf trace, a QEC driver,
and a classical test harness with no code duplication — and it is why the library stays
small enough to be obviously correct.

### 5. Classically testable by construction

Every classical opcode compiles to `X`/`CX`/`CCX` only. Those three gates are
*classical* permutations. So the backend carries a two-bit shadow per qubit —
`value` and `unknown` — that `X`/`CX`/`CCX` update exactly and that `Ry`/`Rz` poison.

Run any CQ program with rotation angles restricted to the classical set (θ ∈ {0, π}),
and `cq_measure` returns the **real answer**. Every adder, comparator, multiplier and
divider can therefore be differential-tested against plain C semantics — in practice over
a small constant number of seeded random samples per width — without ever simulating a
quantum state.

That is not a testing convenience bolted on afterwards. It is the reason this design is
buildable at all.

---

## What this repo is not

| Not | Where it lives instead |
|---|---|
| An error-correction layer | `C_quantum_error_correction` — we only call it |
| A simulator | Nowhere, by choice. Two bits per qubit, no amplitudes |
| A circuit optimiser | Later, and only above a working baseline |
| A compiler | CQ_lang owns taint, lowering, and the uncompute schedule |
| Original reversible-circuit research | Bennett.jl |

---

## The finish line

`libcqops` is done when all five hold:

1. **Link.** `libcq_templates`'s printf placeholders are gone; CQ_lang's existing
   end-to-end fixtures link against `libcqops` and run.
2. **Correct.** Every integer opcode is differential-tested against C semantics at every
   width on its shipped ladder, over a small constant number of seeded random
   `(bit-kind mask, value)` samples per width — with the all-classical mask (which is
   also the zero-cost claim) and the all-quantum mask forced into every draw.
3. **Clean.** After every template call the qubit pool contains exactly the result
   rail's qubits — asserted, not assumed. After `_unc` the rail's **value** is zero but
   its qubits are still held: `_unc` reclaims nothing, and `cqrt_free` is the sole
   deallocator (PRD §10). The pool is empty after the **free**, not after the `_unc` —
   and for a rail CQ_lang deliberately never frees, it stays held for good, which is the
   intended safe leak rather than a failure of this condition.
4. **Grover.** A Grover search written in ordinary C compiles through `cqc`, links, and
   emits a gate stream whose oracle arithmetic is verified exactly in classical mode.
5. **Hardware.** Flipping one flag routes the same stream into `qec_x` / `qec_cx` /
   `qec_ccx` / `qec_rz`, and the QEC library reports physical resource costs for a
   program nobody wrote a circuit for.

Point 4 is the one that matters. Everything else is scaffolding for it.
