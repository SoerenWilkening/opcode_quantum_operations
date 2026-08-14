# Implementation Plan — `libcqops` v1

Status: draft · Companions: [`NORTH_STAR.md`](NORTH_STAR.md), [`PRD-v1.md`](PRD-v1.md)

Three rules govern everything below.

1. **Test first.** Every step is `Red → Green → Gate`. The test file is written and
   failing before the module exists. No module is "done" without its gate passing.
2. **≤ 300 lines per module.** Counted as non-blank, non-comment lines in any
   hand-written `.c` / `.h` / `.py`. Enforced by CI (§2.3), not by discipline.
3. **Bennett.jl is the spec, and it must be on disk before any kernel is written.**
   Step 0 exists because you cannot port from a citation.

---

## 0. Design decisions this plan adds to the PRD

The PRD leaves three mechanisms underspecified in ways that decide whether kernels fit
in 300 lines. Settling them up front is the difference between a dozen small kernels and
a dozen 600-line ones.

### 0.1 The sandwich is a driver, not a per-kernel pattern

PRD §5 says "reverse the compute" is "replaying the emitted operand triples backwards —
which the kernel knows without storing anything." Taken literally, every sandwich kernel
hand-writes its gate loop twice, forwards and backwards. That doubles each kernel and
puts the correctness of the reverse half in twelve separate places.

Instead, each kernel exposes its compute half as an **indexed step function**, and one
shared driver runs it forwards, copies out, and runs it backwards:

```c
/* sandwich.h */
typedef void (*cq_step_fn)(cq_ctx *ctx, void *env, int step);

void cq_sandwich(cq_ctx *ctx,
                 cq_step_fn compute, int n_compute,
                 cq_step_fn copyout, int n_copyout,
                 void *env);
/*  for s in [0, n_compute):  compute(env, s)
 *  for s in [0, n_copyout):  copyout(env, s)
 *  for s in (n_compute, 0]:  compute(env, s)      <- reversal is structural
 */
```

Reversal becomes impossible to get wrong per-kernel, and each kernel shrinks to a step
function plus an `env` struct. It also lines up exactly with the v2 optimisation recorded
in PRD §9: controlling a sandwich kernel means promoting **only the `copyout` steps**,
which is a one-line change in the driver rather than twelve kernel edits.

### 0.2 Invariant I6 — what makes structural reversal sound

Replay-in-reverse is only correct if step `s` emits the *same* gates on both passes. It
does not in general: if a target bit is `BIT_ONE`, the forward pass materialises it with
an `X` (2 gates) while the reverse pass sees it already on a qubit (1 gate), and the `X`
is never undone.

The saving condition:

> **I6** — inside a `cq_sandwich` compute half, every gate *target* is a bit of the
> scratch region. Scratch is born `BIT_ZERO`, so materialisation there emits no `X`, and
> step `s` therefore emits an identical gate sequence forwards and backwards.

Sources appear only as controls, and controls are never materialised. This holds for
every kernel in PRD §6 — e.g. ripple-carry `add` targets only the carry chain during
compute, and touches `dst` only in `copyout`, which is outside the reversed region.

Two enforcement mechanisms, both cheap:

- **Compile time.** `cq_emit_*` takes controls as `const cq_bit *` and targets as
  `cq_bit *`. Materialisation mutates, so a source can never be materialised by
  construction.
- **Run time (debug).** The context carries the active scratch extent during a compute
  half; `cq_emit_*` asserts the target lies inside it. Costs nothing in release builds.

I6 is the reason a kernel budget of 100–200 lines is realistic.

### 0.3 The controlled axis is an emitter mode, not a kernel rewrite

PRD §9's promotion (`NOT→CNOT`, `CNOT→Toffoli`, `Toffoli→` 3-Toffoli sandwich) is a
gate-level transform. Implement it as a control stack on the context:

```c
void cq_ctrl_push(cq_ctx*, const cq_bit *ctrl);  /* acquires the shared ancilla lazily */
void cq_ctrl_pop (cq_cxt*);                      /* releases it, asserted |0> */
```

`cq_emit_x/cx/ccx` consult `ctx->ctrl_depth`. Every kernel becomes controlled for free —
no kernel is aware the axis exists. Nested control ANDs the flags into a single wire on
the second push, so the promotion never sees more than one control (PRD §9).

---

## 1. Step 0 — ground the references *(no C written)*

The blocker identified in review: `PRD-v1.md` names twelve Bennett symbols, contains zero
URLs, and there is no Bennett.jl checkout anywhere on disk. Kernels cannot be TDD'd
against a specification that is not present.

| # | Task | Output |
|---|---|---|
| 0.1 | Vendor Bennett.jl at a **pinned commit** (submodule or `third_party/bennett/` + `COMMIT`). Add the URL to the PRD. | `third_party/bennett/` |
| 0.2 | **Extract each construction.** For K1–K12: Julia source excerpt, gate sequence as indexed pseudo-code, gate-count formula in `W`, ancilla count. | `docs/constructions/K01..K12.md` |
| 0.3 | Pin CQ_lang: record the revision, copy `tools/opcode_table.yaml`, run the PRD §2.1 census `grep -rhoE '"cqrt_[a-z0-9_]*"' ir-pass/src` and commit the result. | `docs/cqrt_census.txt`, `third_party/cq_lang/opcode_table.yaml` |
| 0.4 | Locate Bennett's published gate-count baselines (referenced by PRD §11 L4 with no path) and record where they live. | `docs/constructions/BASELINES.md` |
| 0.5 | Resolve the three PRD blockers from review: **1455 vs 1474** symbol count (recount from the real yaml); **`_unc` vs `cqrt_free` qubit ownership** (who returns qubits to the pool — this decides M09's API); **L4 golden tuple arity** (`58/6/40/12` is four numbers against a three-tuple). | PRD-v1 edits |
| 0.6 | Write the inverted list — *what is **not** from Bennett*: fold table, shadow, handle table, qubit pool, rotations (§7), sinks (§8), `cswap`/Fredkin, nested-control AND. | PRD §0 "Provenance" |

**Gate:** every K1–K12 has a written construction spec with a gate-count formula in `W`.
Those formulas become the L4 goldens; without them L4 has nothing to assert against.

> 0.5's `_unc`/`free` question is genuinely blocking: PRD §10 says `_unc` returns qubits
> to the pool *and* leaves bits as "known-zero qubits", which double-frees on a following
> `cqrt_free`. Pick one before M09.

---

## 2. Step 1 — skeleton and harness

### 2.1 Build

```
CMakeLists.txt              C11, -Wall -Wextra -Werror -Wconversion
cmake/CqopsTest.cmake       add_cqops_test(name) -> one binary per module + CTest entry
```

Two configurations. `Debug` defines `CQOPS_DEBUG_INVARIANTS` (I2 owner map, I6 scratch
extent check, distinctness asserts) and builds with `-fsanitize=address,undefined`.
`Release` compiles all of it out. **Tests run under both** — the invariant checks are the
point of the debug build, and the release build is what gets its gate counts pinned.

### 2.2 Test harness — `tests/support/`

No dependencies beyond libc (PRD §14), so hand-rolled:

| File | LOC | Purpose |
|---|---|---|
| `harness.[ch]` | 90 | `CHECK/CHECK_EQ/CHECK_GATES`, per-file `main`, TAP output, failure context |
| `mock_sink.[ch]` | 120 | Recording sink: captures the `(op, operands)` stream; compare against expected, dump on failure. **The workhorse fixture** |
| `refmodel.[ch]` | 150 | Plain-C reference semantics per opcode at each `W`, with correct masking and two's-complement edge cases |
| `bitkinds.[ch]` | 110 | Build a register from `(value, quantum-mask)`; enumerate the mask sets used by L1 |
| `poolcheck.[ch]` | 80 | Snapshot/diff the qubit pool; the automatic L2 and L3 assertions |

### 2.3 The 300-line guard

```
tools/check_loc.sh          fail if any hand-written src/shim file exceeds 300
```

Counts non-blank, non-comment lines. Runs in CI and as `make lint`.

- **Applies to:** everything hand-written — `src/`, `shim/*.py`, `include/`, and `tests/`.
- **Exempt:** generated `*.gen.c` (the shim emits one file per opcode family, so no
  single generated unit is unwieldy anyway) and `third_party/`.
- **When a module hits the limit:** split along the seam already named in §3's table.
  Every module over ~200 lines below has its pre-planned split recorded, so hitting 300
  is never a surprise refactor.
- Large static test tables move to `.inc` files rather than inflating a test module.

**Gate:** `ctest` runs one trivially passing test in both configurations; `make lint`
passes; sanitizers are active in `Debug`.

---

## 3. Module map

LOC figures are budgets, not measurements. Every module names the seam it splits on if
it grows.

### Layer 0 — primitives *(no internal dependencies)*

| ID | Module | LOC | Split seam if it grows |
|---|---|---|---|
| M01 | `bit.h` — tri-valued bit, all `static inline` | 70 | — |
| M02 | `shadow.[ch]` — growable per-qubit `{value, unknown}`, the four §3 update rules | 130 | — |
| M03 | `qubits.[ch]` — monotonic counter, LIFO free list, ceiling (D2), peak tracking, I3 | 150 | pool ↔ statistics |
| M04 | `sink.[ch]` — vtable, `cqops_set_sink`, env-var default | 90 | — |

### Layer 1 — emission

| ID | Module | LOC | Split seam |
|---|---|---|---|
| M05 | `emit.[ch]` — §3 fold table, `cq_materialise`, distinctness asserts, I6 check | 190 | fold table ↔ materialisation |
| M06 | `controlled.[ch]` — §9 promotion, control stack, shared ancilla, nested AND | 140 | — |

### Layer 2 — registers and the sandwich

| ID | Module | LOC | Split seam |
|---|---|---|---|
| M07 | `reg.[ch]` — handle table, tombstones (D5), I2 owner map, D7 aliasing asserts | 180 | table ↔ invariant checking |
| M08 | `scratch.[ch]` — acquire/release a `BIT_ZERO` scratch region, assert clean on release | 90 | — |
| M09 | `sandwich.[ch]` — the §0.1 driver, I6 extent tracking | 110 | — |

### Layer 3 — kernels *(all independent of each other; parallelisable)*

| ID | Module | Kernel | LOC | Split seam |
|---|---|---|---|---|
| M10 | `kernels/bitwise.c` | K1 xor, K2 and, K3 or | 100 | — |
| M11 | `kernels/shift_const.c` | K4 constant shl/lshr/ashr | 90 | — |
| M12 | `kernels/shift_var.c` | variable shifts (barrel over K10) | 130 | — |
| M13 | `kernels/cast.c` | K5 sext/zext/trunc | 90 | — |
| M14 | `kernels/add.c` | K6 add, K7 sub | 190 | `add.c` ↔ `sub.c` |
| M15 | `kernels/addacc.c` | K8 Cuccaro in-place accumulator | 120 | — |
| M16 | `kernels/cmp.c` | K9 eq/ult/slt + 7 derived predicates | 200 | primitives ↔ predicate derivation |
| M17 | `kernels/mux.c` | K10 | 90 | — |
| M18 | `kernels/mul.c` | K11 shift-add over K8 | 160 | — |
| M19 | `kernels/divrem_u.c` | K12 unsigned restoring division | 220 | loop body ↔ driver |
| M20 | `kernels/divrem_s.c` | signed wrappers, D3 div-by-zero | 110 | — |

### Layer 4 — analog and sinks

| ID | Module | LOC | Notes |
|---|---|---|---|
| M21 | `angle.[ch]` | 80 | Pure classification of θ against §7's rows, tolerance configurable. Exhaustively testable, zero dependencies |
| M22 | `rotate.[ch]` | 150 | §7 Ry/Rz per bit, the θ≡π asymmetry, measurement |
| M23 | `sink_printf.c` | 70 | Default; CQ_lang's golden-trace format |
| M24 | `sink_count.c` | 100 | Per-kind totals, peak live qubits, T-count = 7×Toffoli |
| M25 | `sink_qec.c` | 100 | Conditional on `C_quantum_error_correction`; Ry/Rz stubbed per §7 |

### Layer 5 — shim

| ID | Module | LOC | Notes |
|---|---|---|---|
| M26 | `shim/cq_runtime_impl.c` | 220 | The `cqrt_*` surface incl. the PRD §2.1 easy-to-miss families |
| M27 | `shim/gen_shim.py` | 280 | Reads `opcode_table.yaml`; emits one `.gen.c` per opcode family + fp aborts |
| M28 | generated `*.gen.c` | exempt | ~1455 integer wrappers + 878 fp abort bodies |

Hand-written total ≈ **3,400 LOC** across 27 modules.

---

## 4. Step-by-step schedule

Each step is `Red` (test written, failing) → `Green` (module) → `Gate` (must pass to
proceed). PRD increment mapping in the right column.

### Phase A — foundation *(strictly sequential)*

| Step | Red | Green | Gate | PRD |
|---|---|---|---|---|
| 2 | `test_bit.c` — I1: every bit is exactly one of three kinds, never two, never none | M01 | I1 holds over all constructors | 1 |
| 3 | `test_shadow.c` — the §3 shadow update table, all operand combinations; poison is sticky; "unknown when determinate" is allowed, the reverse never is | M02 | Table green | 1 |
| 4 | `test_qubits.c` — LIFO order (D4); ceiling exceeded fails loud (D2); releasing a qubit whose shadow is not known-0 is a **hard error** (I3); peak tracking | M03 | All green | 1 |
| 5 | `test_sink.c` + `mock_sink` — every vtable entry dispatches; env-var default selection | M04 + `mock_sink` | Recording sink usable by later tests | 1 |
| 6 | **`test_emit_fold.c` — L0, exhaustive.** Target/control ∈ {const-0, const-1, Q known-0, Q known-1, Q unknown}: 5 X cases, 25 CX, 125 CCX. Each pins **gates emitted, qubits allocated, resulting bit-kind, and shadow**. Plus: distinctness assert fires on `c == t` and `c1 == c2` | M05 | **175/175.** This is the most important suite in the project — everything above it is Bennett transcribed against these three functions | 1 |
| 7 | `test_reg.c` — handles monotonic, never reused (D5); tombstones; I4 (all-constant register owns zero qubits); free of a dirty rail is a hard error; I2 owner map catches a double-owned qubit; **D7 aliasing asserts fire** | M07 | All green | 1 |
| 8 | `test_scratch.c`, `test_sandwich.c` — driver runs compute forwards, copyout, compute backwards; a synthetic step function's recorded stream is a **palindrome around the copyout**; I6 violation (target outside scratch) is caught in Debug | M08, M09 | All green | 1 |
| 9 | `test_sink_printf.c`, `test_sink_count.c` — trace format matches CQ_lang's goldens; counter totals match the mock sink's stream | M23, M24 | **PRD Increment 1 complete** | 1 |

### Phase B — kernels *(M10–M20 are independent after Step 9; build in any order or in parallel)*

Every kernel step uses the **same four-part gate**, applied automatically by the shared
kernel driver rather than written per kernel:

| Level | Assertion | Mechanism |
|---|---|---|
| **L1** | `shadow(dst) == refmodel(a, b)` for all `(a,b)` at `W ∈ {1,2,4,8}`, × bit-kind masks; random sampling at `W ∈ {16,32,64}` | `bitkinds` + `refmodel` |
| **L2** | After the call, the live-qubit set equals **exactly** `dst`'s qubits | `poolcheck`, automatic on every L1 case |
| **L3** | forward → `_unc` → `dst` all-zero **and** pool restored. Asserted on **values and pool state only, never bit-kinds** (PRD §10) | `poolcheck`, automatic |
| **L4** | `(NOT, CNOT, Toffoli)` at each `W` matches the golden, cross-checked against the Step 0.2 formula | `sink_count` + `tests/goldens/`, `--update-goldens` to regenerate |

Bit-kind masks are not purely random. The fixed set always includes: all-classical
(this is **L5** — zero gates, zero qubits), all-quantum, alternating, LSB-only, MSB-only,
and a one-bit-quantum sweep across all `W` positions. Random masks are sampled on top.

| Step | Kernels | Module | PRD |
|---|---|---|---|
| 10 | K1 xor, K2 and, K3 or — naturally clean, no sandwich | M10 | 2 |
| 11 | K4 constant shifts, K5 casts — pure index shuffle, naturally clean | M11, M13 | 2 |
| 12 | K6 add, K7 sub — **first sandwich users.** Ripple-carry per PRD §4, not Cuccaro | M14 | 3 |
| 13 | K9 compares — eq/ult/slt primitives, then the 7 derived predicates (`ne=¬eq`, `ugt=ult(b,a)`, `ule=¬ult(b,a)`, `uge=¬ult(a,b)`, signed trio by sign-bit flip) | M16 | 4 |
| 14 | K10 mux, then variable shifts as a barrel over it | M17, M12 | 5 |
| 15 | K8 Cuccaro accumulator — in-place, self-cleaning, 1 ancilla. L4 golden `6W−5` | M15 | 5 |
| 16 | K11 mul — shift-add over K8 | M18 | 5 |
| 17 | K12 divrem — unrolled restoring division over K7/K9/K10. Unsigned first, then signed + D3 | M19, M20 | 6 |

**Step 12's extra gate:** the first sandwich kernel must pin `x+1` at `i8` against
Bennett's published baseline (PRD §11 L4) and document any deliberate delta. This is the
one place the port is validated against upstream rather than against itself.

**Step 17's extra gate:** D3 — `sdiv`/`srem` by zero is deterministic-but-unspecified,
documented, and never traps. Assert it does not trap; pin whatever it returns.

### Phase C — axes

| Step | Red | Green | Gate | PRD |
|---|---|---|---|---|
| 18 | `test_angle.c` — every row of §7's table incl. mod-4π vs mod-2π boundaries and tolerance edges | M21 | Table green | 7 |
| 19 | `test_rotate.c` — **the θ≡π asymmetry**: `Ry(π)` on a constant bit flips it with **0 gates and 0 qubits**; on a qubit it emits `X` then `Z`. `Rz` on a constant is a no-op at every φ. Measurement returns shadow, 0 for unknown, emits `mz`, is terminal | M22 | Classical mode works end to end | 7 |
| 20 | `test_controlled.c` — promotion table verbatim from `controlled.jl`; ancilla shared across the region and returned |0⟩; nested control uses **one** control wire; **every Phase-B kernel re-runs its L1–L4 suite under `cq_ctrl_push`** | M06 | L1–L4 green under control for all kernels | 7 |
| 21 | `test_unc.c` — `_unc` across every kernel; the PRD §10 asymmetry is **expected**, so forward and `_unc` counts are pinned **separately**. `_inv` for compare flags. `cqrt_free` of a dirty rail is a hard error | (thin, in M26) | **L3 green across all kernels** | 7 |

Step 20's re-run is why the controlled axis is an emitter mode (§0.3): it costs one
parameter in the kernel test driver, not twelve new suites.

### Phase D — shim, link, Grover

| Step | Red | Green | Gate | PRD |
|---|---|---|---|---|
| 22 | `test_gen_shim.py` — generator round-trips the real `opcode_table.yaml`; **emitted symbol count reconciles with Step 0.5**; every fp symbol gets a named abort body | M27 | Generator green | 8 |
| 23 | `test_runtime.c` — the PRD §2.1 families: `cqrt_copy_<W>_controlled`, `cqrt_rz_<W>_controlled[_inv]`, `cqrt_cswap` (constant ctrl = **0 gates**; quantum ctrl = Fredkin per bit) | M26, M28 | Full grid links; `nm` shows no undefined `cq_template_*` | 8 |
| 24 | **L6** — link against CQ_lang's existing fixtures, diff emitted traces | — | Traces match | 8 |
| 25 | **L7** — Grover per PRD §12. (a) compiles through `cqc`, links, emits a gate stream; (b) **classical mode**: `M_PI/2 → M_PI` runs deterministically and `cq_measure` returns what plain C computes; (c) counter sink reports Toffoli count, T-count, peak qubits, stable across runs and pinned | — | **v1 done** | 8 |

### Phase E — optional

| Step | Work | PRD |
|---|---|---|
| 26 | `sink_qec` against `C_quantum_error_correction`; pool ceiling wired to `qec_n_logical` (D2) | 8 |
| 27 | *(stretch)* QRAM — port `qrom.jl` (self-cleaning AND tree, 2(L−1) Toffoli) and `softmem.jl` | 9 |

---

## 5. Critical path and parallelism

```
Step 0 ──► 1 ──► 2 ─► 3 ─► 4 ─► 5 ─► 6 ─► 7 ─► 8 ─► 9          (foundation, sequential)
                                                    │
                    ┌───────────────┬───────────────┼───────────────┐
                    ▼               ▼               ▼               ▼
                 10 (bitwise)   11 (shift/cast)  12 (add/sub)   13 (cmp)
                                                    │               │
                                                    └──► 14 (mux) ◄─┘
                                                          │
                                                    15 (K8) ─► 16 (mul)
                                                          │
                                                    17 (divrem)
                    └───────────────┴───────────────┴───────────────┘
                                                    ▼
                                         18 ─► 19 ─► 20 ─► 21        (axes)
                                                    ▼
                                         22 ─► 23 ─► 24 ─► 25        (shim, link, Grover)
```

**Step 6 is the real critical path.** The fold table is the only place classical/quantum
is decided; a bug there is a bug in all twelve kernels simultaneously, and it will
present as a kernel bug. Over-invest in its exhaustive suite before writing any kernel.

Steps 10–13 are genuinely independent and can run concurrently. Steps 14–17 chain
(mux → Cuccaro → mul, and divrem needs sub + cmp + mux).

---

## 6. Risk register

| # | Risk | Signal | Mitigation |
|---|---|---|---|
| R1 | I6 violated by a kernel — a compute-half target outside scratch makes the reverse half silently non-cancelling | L2 fails, or worse, L2 passes and L3 fails only at some widths | Debug-build extent assert (§0.2) plus `const`-qualified control parameters. Both land in Step 8, before any kernel |
| R2 | **D7 aliasing** — CQ's pass emits `add(h,h)` or `_unc(out,out,b)`. Every kernel assumes distinct registers | Only surfaces at Step 24 (L6), by which point twelve kernels exist | Assert loud from Step 7. If it fires, the fix is a defensive `cqrt_copy` of the aliased operand — one place, not twelve |
| R3 | Bennett.jl drift invalidates L4 goldens silently | Goldens diff after an unrelated pull | Pinned commit (Step 0.1); goldens carry the Bennett commit in a header comment |
| R4 | K12 divrem exceeds 300 lines | `make lint` fails | Pre-planned split (M19/M20) already in the module map |
| R5 | L4 goldens fragile w.r.t. D4 free-list discipline and D6 non-demotion | Goldens churn on unrelated changes | Pin **counts**, not traces, for kernels. Trace goldens only at L6 where CQ_lang owns the format |
| R6 | `_unc` gate count legitimately exceeds forward (PRD §10, consequence ii) | Someone "fixes" the asymmetry by asserting equality | Step 21 pins the two **separately**, with the PRD §10 note quoted in the test file |
| R7 | Generated shim inflates compile time | Slow builds from Step 23 | One `.gen.c` per opcode family; the generator already splits |

---

## 7. Definition of done

v1 ships when NORTH_STAR's five conditions hold, each traced to a step:

| # | NORTH_STAR condition | Step |
|---|---|---|
| 1 | **Link** — CQ_lang's fixtures link against `libcqops` and run | 24 |
| 2 | **Correct** — every integer opcode differential-tested against C semantics, exhaustive at `W ≤ 8`, random at `W ∈ {16,32,64}`, across every mixture of classical and quantum operand bits | 10–17 (L1) |
| 3 | **Clean** — after every template call the pool holds exactly the result rail's qubits; after `_unc`, nothing | 10–17 (L2), 21 (L3) |
| 4 | **Grover** — ordinary C compiles through `cqc`, links, emits a gate stream whose oracle arithmetic is verified exactly in classical mode | 25 |
| 5 | **Hardware** — one flag routes the same stream into `qec_*` | 26 |

Plus the two this plan adds: `make lint` green (no hand-written module over 300 lines),
and every kernel traceable to a `docs/constructions/K*.md` spec extracted from a pinned
Bennett.jl commit.
