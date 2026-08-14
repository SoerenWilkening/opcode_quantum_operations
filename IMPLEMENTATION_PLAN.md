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

The saving condition, in two parts. **Part (a) alone is what an earlier draft said, and it
is not sufficient** — see the control-side hazard below.

> **I6(a) — target side.** Inside a `cq_sandwich` compute half, every gate *target* is a
> bit of the scratch region. Sources appear only as controls, and controls are never
> materialised.
>
> **I6(b) — control side.** Every bit of the scratch region is `CQ_BIT_Q` for the whole
> duration of the compute half. `cq_sandwich` guarantees this by **pre-materialising the
> entire scratch region at step 0**, before the first compute step runs.

Together these make step `s` emit an identical gate sequence forwards and backwards, so
replay-in-reverse cancels exactly.

**Why (a) is not enough — the hazard, and why it is nasty.** The old justification was
"scratch is born `BIT_ZERO`, so materialisation there emits no `X`." True, and it covers
targets. But the fold table dispatches on *kind*, and a scratch bit can be read as a
**control** while it is still `BIT_ZERO`:

```
step 1:  CCX(c_0, a_1, c_1)     c_0 is still BIT_ZERO  ->  "either control ZERO" -> 0 gates
step 2:  CX (a_0, c_0)          materialises c_0       ->  c_0 is now CQ_BIT_Q
         ... reverse ...
step 2:  CX (a_0, c_0)          c_0 already Q          ->  1 CX      (matches forward)
step 1:  CCX(c_0, a_1, c_1)     c_0 is now Q           ->  1 CCX     (forward emitted NOTHING)
```

The reverse half is no longer the mirror of the forward half. The sandwich does not cancel,
scratch is left dirty, and **L1 stays green the whole way** — the value is right and the
trace looks plausible. Only L2/L3 can see it, and only for bit-kind masks that trigger the
late materialisation (`{ZERO,Q}`-only masks first fail at `W=5`). This is risk **R8**, and it
was found independently by two of the Step 0.2 extractions.

**Decision (2026-08-14): pre-materialise.** `cq_sandwich` materialises the whole scratch
region up front. Rationale: it restores the property this whole design exists for —
reversal is *structural* and cannot be got wrong per-kernel (§0.1). The alternative
(document an ordering obligation, "never read a scratch bit as a control before it is
materialised") is cheaper in gates but puts reverse-half correctness back into twelve
separate kernels, and it is unenforceable by the `const`-qualification mechanism, which
only guards targets.

Two consequences, both load-bearing:

1. **L4 goldens stop depending on the *scratch* side — but NOT on the operand side.**
   No fold on a scratch bit can fire, so the scratch half of the count is a function of `W`
   alone. **Operand folds still fire**, and must: `a + 0`, `x − 1`, an all-`ZERO` operand
   and the `x+1` constant-increment case all legitimately emit fewer gates, and that is
   what L5 exists to prove. So a kernel's count is a function of `(W, operand mask)`, and
   **every L4 golden must name the mask it was taken at.**
   *Pin at the all-quantum mask.* It is the right choice for a specific reason, not by
   convention: because we never demote (D6), an operand mask can only drift **towards** `Q`
   between a forward call and its `_unc` — so the all-quantum mask is the **fixed point** of
   that drift, and it is the one mask at which forward and `_unc` emit the same count. That
   is what makes a single stable golden per `(kernel, W)` possible at all, and it is
   consistent with Rule 14: forward and `_unc` counts are pinned separately *because* they
   differ at every other mask.
2. **A kernel must short-circuit *before* entering the sandwich when every operand bit is
   classical.** Otherwise pre-materialisation would allocate scratch qubits for a fully
   classical operation and break **L5**'s "zero gates and zero qubits". The all-classical
   path never enters `cq_sandwich` at all — it folds to a constant result directly.

Three enforcement mechanisms, all cheap:

- **Compile time.** `cq_emit_*` takes controls as `const cq_bit *` and targets as
  `cq_bit *`. Materialisation mutates, so a source can never be materialised by
  construction. *(PRD §3's prototypes contradicted this and have been corrected.)*
- **Run time (debug).** The context carries the active scratch extent during a compute
  half; `cq_emit_*` asserts the target lies inside it. Costs nothing in release builds.
- **Run time (debug), I6(b).** On entry to a compute half, assert every scratch bit is
  already `CQ_BIT_Q`. This is a one-line loop and it makes a regression in the
  pre-materialisation step fail loudly instead of silently.

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
| 0.7 | Resolve **`cqrt_h`**: listed in PRD §1's core family, absent from §8's sink vtable, forbidden by constraint 1, and built from rotations in §12. Decide before M04 freezes the vtable in Step 5. | PRD-v1 edits |
| 0.8 | Resolve the **fold-table case count**: §4 Step 6 enumerates 155 and then gates at "175/175". Settle it *in this plan* before `test_emit_fold.c` is written. | this plan, §4 Step 6 |

*(0.7 and 0.8 were found after this plan was first written and existed only in `bd`; folded
in here 2026-08-14.)*

**Gate:** every K1–K12 has a written construction spec with a gate-count formula in `W`.
Those formulas become the L4 goldens; without them L4 has nothing to assert against.

> **0.5's `_unc`/`free` question — SETTLED 2026-08-14: `cqrt_free` is the sole
> deallocator.** `_unc` zeroes values in place and reclaims nothing. Forced empirically,
> not chosen: CQ_lang decides reclamation *per rail* and its only lever is emitting or
> withholding the free, so the same `_unc` symbol appears both freed and deliberately
> never freed (on a rail it has proven entangled). Reclaiming at `_unc` would return an
> entangled qubit to the free list. Three independent judges, 3–0, over 239 pinned
> goldens: 25,147 `_unc` calls, 0 double-frees. Full statement in PRD §10.
>
> **This plan mis-stated the consequence, twice** (the 0.5 row above, and this note):
> it does **not** decide M09's API. M09 is `sandwich.[ch]`, whose driver runs
> compute/copy-out/compute-reversed over **scratch** and never touches a result rail's
> ownership; its signature is unchanged under either rule and **Step 8 was never
> blocked**. The `_unc` axis is Step 21 and lives in M26. The real deltas are: M26's
> `_unc` epilogue calls nothing in the pool; M03 keeps exactly **one** release site,
> reachable only from `cqrt_free`; and M07 must carry whatever evidence `cqrt_free`'s
> assert reads — which is its own open question, below.

### Step 0 status (2026-08-14)

| # | State | Where the answer lives |
|---|---|---|
| 0.1 | **done** | `third_party/bennett/` @ `980805de` + `COMMIT`; URL in PRD §0 |
| 0.2 | **done** | `docs/constructions/K01..K12.md` — *but see GAP 1 below* |
| 0.3 | **done** | `docs/cqrt_census.txt`; `third_party/cq_lang/` @ `a6a92fe` |
| 0.4 | **done** | `docs/constructions/BASELINES.md` |
| 0.5 | items 1 and 3 **done** (PRD §1, PRD §11); item 2 **open** | — |
| 0.6 | **done** | PRD §0 "Provenance" |
| 0.7 | **done** — over-declaration; struck from PRD §1. Vtable unaffected, M04 unblocked | PRD §1 |
| 0.8 | **done** — **159** (155 + 4). Uncovered a 15-case hole in PRD §3's `CCX` table, now fixed | §4 Step 6; PRD §3 |

> **GAP 1 — DECIDED 2026-08-14: pre-materialise.** The Step 0.2 catalogue was internally
> split: K06/K07/K09/K12 pinned goldens assuming scratch bits stay classical until
> materialised, while K11 pinned the golden that pre-materialisation implies. Both were
> self-consistent and mutually incompatible, so M14 and M18 would have been written against
> two different emitter designs. The underlying hazard is real and is now **I6(b)** plus
> risk **R8** — see §0.2. `cq_sandwich` pre-materialises the whole scratch region at step 0;
> K11 was already consistent, and §3 of K06, K07, K09 and K12 is re-issued against it —
> every re-derived figure independently confirmed by an adversarial verifier.
>
> Goldens at W=8, all at the **all-quantum mask**: K06 `11W−4` = **84** (was 80),
> K07 `15W−2` = **118** (was 116), K09 `ult` `12W+4` = **100** (was 98) and `slt`
> `16W+8` = **136**, K12 `udiv` `34W²+13W` = **2280** (was 2166), K11 `13W²−8W` = **768**.
> Qubits: K06 `2W`, K07 `3W`, K09 `ult` `3W+1`, K11 `W²+2W`,
> K12 `8W²+6W−1` (**559** at W=8; **not** `8W²+5W` — killing K12's third fold costs a
> further `W−1` qubits, and the gate/qubit pair must move together).
>
> **The decision fixed a latent bug in K12, it did not merely re-price it.** K12's own
> re-issue initially claimed its halves already mirrored correctly under the old rules.
> They did not: replaying its forward list in reverse diverges at `W ≥ 3` for any classical
> `b` — `lower_sub!` reads `result[i]` as a *control* and then targets it again, so a write
> whose sources all fold leaves the target classical and the reverse half sees `Q`. Smallest
> witness: `W=3`, `a` all `Q`, `b` all `ZERO`.

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

### Step 1 status (2026-08-14) — **done, with one deviation**

On disk: `CMakeLists.txt`, `cmake/CqopsTest.cmake`, `cmake/CqopsSanitizers.cmake`,
`include/cqops/cqops.h`, `src/version.c`, `tests/CMakeLists.txt`,
`tests/support/harness.[ch]`, `tests/test_skeleton.c`,
`tests/test_harness_negative.c`, `tools/check_loc.sh`, root `Makefile`.

Gate met: both configurations configure and build **warning-free** under
`-Wall -Wextra -Werror -Wconversion`, `ctest` is 2/2 in each, `make lint` passes.

Three things worth knowing:

1. **Only `harness.[ch]` of §2.2 exists.** It is the one support file with no
   dependency on an unbuilt module. `mock_sink` needs M04's vtable (Step 5);
   `refmodel` / `bitkinds` / `poolcheck` land across Phase B. `CHECK_GATES` is
   nonetheless in `harness.h` now, taking six plain counts, so Step 5's `mock_sink`
   only has to feed it a tuple.
2. **`src/version.c` is scaffolding, not a module.** It exists so the Step 1 gate
   proves a real link — include path, archive, link line — rather than proving only
   that the harness runs. It is not in the §3 module map and carries no design weight.
3. **DEVIATION — `Debug` is UBSan-only on the default toolchain.** Apple clang 17 on
   Darwin 25 / x86_64 has a broken AddressSanitizer runtime: a trivial `main` built
   with `-fsanitize=address` dies with `SIGILL` inside `libsystem_pthread` before
   reaching `main`. Hard-coding the plan's `-fsanitize=address,undefined` would make
   every Debug binary in the project unrunnable on this box. So each sanitizer is
   **probed** — compiled *and run* — via `check_c_source_runs`, and only what works is
   enabled; what is missing gets a CMake warning, and `test_skeleton` cross-checks the
   build's belief against the compiler's `__has_feature` so the gap can never go
   quiet. `CQOPS_SANITIZERS=ON` turns a missing sanitizer into a hard configure error;
   `-DCMAKE_C_COMPILER=/usr/local/opt/llvm/bin/clang` (Homebrew LLVM) restores both,
   verified. UBSan on Apple clang does genuinely abort, verified via
   `-fno-sanitize-recover=all`. **Until ASan is available by default, a Debug run
   verifies less than the plan assumes — Rule 17 applies when reporting it.**

Also added beyond the letter of §2.2, because the harness is the foundation every
later verification claim rests on: `tests/test_harness_negative.c`, a binary registered
`WILL_FAIL` that asserts a failing `CHECK` really does report and exit non-zero. A
`CHECK` that could not fail would make every suite green while verifying nothing.

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
| M28 | generated `*.gen.c` | exempt | **1595** integer wrappers + **884** fp abort bodies (PRD §1; 1455 if i80 is ruled out of scope) |

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
| 6 | **`test_emit_fold.c` — L0, exhaustive.** Target/control ∈ {const-0, const-1, Q known-0, Q known-1, Q unknown}: `5 X + 25 CX + 125 CCX` = **155** cases, the full Cartesian product. Each pins **gates emitted, qubits allocated, resulting bit-kind, and shadow** — four *assertions* per case, not four cases. Plus **4** distinctness death-tests: `c == t` (CX) and `c1 == c2`, `c1 == t`, `c2 == t` (CCX). Each death-test needs **Q** operands, since the assert compares qubit indices and cannot fire on constants | M05 | **159/159** (155 + 4). This is the most important suite in the project — everything above it is Bennett transcribed against these three functions | 1 |
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
| **L3** | forward → `_unc` → `dst`'s **values** all-zero; then an explicit `cqrt_free` → pool restored. Asserted on **values and pool state only, never bit-kinds** (PRD §10). Note `_unc` alone does **not** restore the pool — it reclaims nothing, so the free is a required third step, not a tidy-up | `poolcheck`, automatic |
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
| 23 | `test_runtime.c` — the PRD §2.1 families: `cqrt_copy_<W>_controlled`, `cqrt_rz_<W>_controlled[_inv]`, `cqrt_cswap` (constant ctrl = **0 gates**; quantum ctrl = Fredkin per bit) | M26, M28 | Full grid links; `nm` shows no undefined `cq_template_*` **from the opcode grid**. The 401 intrinsic/libm symbols come from CQ_lang's own archives and are deliberately *not* ours (PRD §1) — an unqualified "no undefined `cq_template_*`" cannot pass | 8 |
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
| **R8** | **I6's *control* side — the hazard R1 does not describe.** A scratch bit read as a **control** while still `BIT_ZERO` folds to 0 gates forward; if a later step materialises it, the reverse replay emits a gate the forward never did. The sandwich stops cancelling and scratch is left dirty | **L1 stays GREEN — and in at least one regime L4 stays green too.** Replaying K12's forward list in reverse with `a` all `Q`, `b` all `ZERO` yields a *different gate multiset with the identical total count* (116/204/318/458/816 at W=3/4/5/6/8), so the count golden matches while the circuit is wrong. **Only L2/L3 can see that case at all.** Other regimes are luckier: `a` all `ONE` with `b` quantum breaks only gate *order* (a benign commuting reorder). `{ZERO,Q}`-only masks first fail at W=5 | **Closed by I6(b)** (§0.2): `cq_sandwich` pre-materialises the whole scratch region at step 0, so no fold on a scratch bit can fire and the two halves are identical by construction. Debug-asserted on entry to every compute half. Lands in **Step 8**, before Step 12. Still put the witnesses (`W=3`, `a=b={Q,ZERO,ZERO}`; `{ZERO,Q}`-only at `W=5`) in the fixed L1 mask set — a regression must not depend on random masks to be caught |
| R9 | Pre-materialisation defeats **L5** — a fully-classical operation allocates scratch qubits it never needed | L5 fails: non-zero gates or qubits for the all-constant case | The all-classical path **short-circuits before `cq_sandwich` is entered** and folds to a constant directly (§0.2, consequence 2). This is a kernel-entry check, so it lands with the first sandwich kernel in Step 12 and is covered by L5's existing all-classical mask |
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
