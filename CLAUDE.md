# `libcqops` — CQ_lang's Quantum Backend: Directives for AI Agents

`libcqops` is a linkable C library that turns every placeholder call CQ_lang's IR
pass emits (`cqrt_*`, `cq_template_*`) into a stream of logical `X` / `CX` / `CCX`
(+ `Ry` / `Rz`) operations — **with provably classical bits costing zero qubits and
zero gates**. We own exactly one box in the stack: above us CQ_lang decides *what*
to compute and *when to uncompute*; below us `C_quantum_error_correction` decides
*how many physical qubits a logical CX costs*. We decide only **which reversible
gates realise this opcode, on which qubits**.

You are working on a **reversible circuit backend**. The defining hazard of this
codebase is the **clean-trace dirty-ancilla miscompile**: a kernel that computes the
right *value*, prints a plausible trace, passes its differential test — and leaves a
scratch qubit off `|0⟩`, or emits a reverse half that does not cancel. CQ_lang's pass
frees only the named result rail and has no idea our internal scratch exists, so a
routine that leaks a dirty ancilla is a **silent miscompile, not a leak**
(NORTH_STAR §3). Everything below exists to prevent that one class of bug.

> **Authoritative status lives in the three planning docs and the `bd` tracker, not
> here.** This file is the *operating manual*.
>
> | Doc | Role |
> |---|---|
> | [`NORTH_STAR.md`](NORTH_STAR.md) | *Why* — the five commitments, the five finish-line conditions, what this repo is **not** |
> | [`PRD-v1.md`](PRD-v1.md) | *What* — scope, data model, the §3 fold table, the K1–K12 kernel catalogue, invariants I1–I5, open decisions D1–D7 |
> | [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md) | *How and when* — §0 design decisions (incl. I6), the M01–M28 module map, 28 steps (Steps 0 and 1 stand alone; Steps 2–27 form phases A–E), the R1–R7 risk register |
> | `bd` | The tracker. All 28 steps (0–27) are filed, plus **eight** Step 0 sub-tasks: the plan's 0.1–0.6, and 0.7/0.8 for two contradictions found after the plan was written. `bd ready` |
>
> **NOTHING IS BUILT YET (as of 2026-08-14).** The repository contains those three
> markdown documents, this file, `AGENTS.md`, and the beads DB. There is **no**
> `src/`, no `include/`, no `tests/`, no `CMakeLists.txt`, no `third_party/`, and **no
> Bennett.jl checkout anywhere on disk**. Every path named in this file or in the PRD
> is a **plan, not a fact**. Do not claim a file exists because a document names it —
> check (Rule 16).

---

## The Prime Directive — a right answer is not a right circuit

The shadow is a **classical** simulation: two bits per qubit, no amplitudes, no
correlations. It can be exactly right while the circuit is wrong. `cq_measure`
returning what plain C computes proves the **permutation**; it says nothing about a
leaked ancilla, a reverse half that fails to cancel, a `Ry` folded on a bit that was
already a qubit, or a rail freed while entangled.

**Correctness here is established by L1 (value) *and* L2/L3 (pool) *and* L4 (gate
count) together — never by any one of them.** A green differential test is necessary
and never sufficient. Assert the pool; never assume it.

Freeing a rail that is not provably zero is the exact signature of a silent state
collapse (PRD §10). When you cannot *prove* a rail is clean, **fail loud** — a hard
error is a feature; a silently dirty ancilla is the only unforgivable bug.

---

## Non-Negotiable Rules

**Rule 0 — Institutional memory.** Durable, hard-won knowledge goes in `bd remember`
(search with `bd memories <keyword>`). Task tracking goes in `bd`. Design-of-record
goes in the three planning docs. Do **not** use TodoWrite, TaskCreate, markdown TODO
lists, or `MEMORY.md`. Every gotcha and every silent-miscompile root cause must be
written down — future agents (and you, after compaction) depend on it.

**Rule 1 — Bennett.jl is the sole source of circuit constructions; port, never
re-derive.** [Bennett.jl](https://github.com/tobiasosborne/Bennett.jl) has already
solved correct, verified, ancilla-clean reversible constructions for the whole
integer opcode surface. "How should we build a reversible comparator?" is **never an
open question in this repo** — the answer is to look it up, not to invent one
(NORTH_STAR §1). **It must be on disk at a pinned commit before any kernel is
written** (plan rule 3, Step 0.1); you cannot port from a citation, and you cannot
TDD against a specification that is not present. Original reversible-circuit research
does not live here.

**Rule 2 — Ancilla-clean at every boundary.** Every routine returns every scratch
qubit to `|0⟩` before it returns (PRD §1 constraint 2). Where a Bennett construction
is dirty by design — because it relies on Bennett's *global* forward–copy–reverse
wrap — apply the same construction **locally**: compute into scratch, XOR the answer
into the result rail, run the scratch computation backwards, free the scratch
(PRD §5; NORTH_STAR §3 calls this "Bennett-in-the-small"). Cost is 2× the compute
half; that is the price and it is not negotiable down.

**Rule 3 — The tri-valued bit is the only representation, and there is NO packed
scalar anywhere.** Every register bit is exactly one of `CQ_BIT_ZERO`, `CQ_BIT_ONE`,
`CQ_BIT_Q(q)` — never two, never neither (**I1**). The classical value and the
quantum mask are the *same field viewed twice*: a bit is classical iff it is not on a
qubit, and the mask is just "which entries are `CQ_BIT_Q`". So there is **no
`uint64_t classical` and no `uint64_t qmask` in this codebase** — not in a register,
not in a peephole, not in a kernel (**I5**). This is the whole reason i128 is free: a
packed scalar caps at 64 and would force a two-word split plus a 128-bit variant of
every identity peephole. **Every kernel is written width-generically over
`reg->width`, with no width switch.**

**Rule 4 — Classical opcodes emit only `X`, `CX`, `CCX`.** No `H`, no `T`, no
multi-controlled gates above 2 controls (PRD §1 constraint 1). This keeps the whole
integer surface a *classical permutation*, which is what makes the two-bit shadow
exact and classical-mode testing possible at all. `Ry`/`Rz` are sink-level entries
reached only through §7 rotations and `cqrt_rz_<W>_controlled`; they are **not**
decomposable in `{X, CX, CCX}` and no kernel may reach for them.

**Rule 5 — Qubits are allocated in exactly one place.** `cq_materialise(bit)` — take
a qubit from the pool (guaranteed `|0⟩` by **I3**), emit `X` if the constant was 1,
set `kind = CQ_BIT_Q`. That is the *only* place a qubit is ever allocated for data,
and it is exactly the rule "a CX from a tainted bit into an untainted bit allocates a
qubit" (PRD §3). Allocation is **lazy, per bit** — never at declaration, never per
register (NORTH_STAR §2). Copies are always physical (allocate + CX), never aliases
(**I2**) — *that* is what makes `cqrt_free` sound.

**Rule 6 — Free only a provably-zero rail.** `cqrt_free` asserts every bit is
`BIT_ZERO` or a known-zero qubit before returning qubits to the pool. **A free of a
dirty rail is a hard error, not a warning** (PRD §10) — it is the exact signature of
a silent state collapse. Likewise a qubit on the free list is `|0⟩` (**I3**), and
releasing one whose shadow is not known-zero is a hard error. Measurement is
**terminal**: CQ_lang emits no adjoint and no `cqrt_free` for a measured handle, so
we do not reclaim its qubits.

**Rule 7 — The kernel contract is one shape, and it serves all three axes.**

> `void kernel(cq_ctx*, cq_bit *dst, const cq_bit *a, const cq_bit *b, int W)`
> with semantics `dst ^= f(a, b)`, leaving `a` and `b` unchanged and every internal
> ancilla at `|0⟩`.

Forward allocates a fresh all-`BIT_ZERO` `dst` (`0 ^ f = f`); uncompute calls the
*same kernel* with `dst = out` (`f ^ f = 0`); controlled promotes the gates (PRD §4).
**This is why v1 uses ripple-carry, not Cuccaro, for the out-of-place adder** —
Cuccaro is in-place, so its uncompute is the *reverse circuit* rather than a re-run,
which does not match CQ_lang's "recompute from the still-live sources" `_unc`
contract. Cuccaro (K8) is still needed, as the *in-place* accumulator inside the
multiplier. Do not "simplify" K6/K7 into Cuccaro.

**Rule 8 — The sandwich is a DRIVER, not a per-kernel pattern; I6 is what makes it
sound.** Never hand-write a kernel's gate loop twice. Each kernel exposes its compute
half as an indexed step function and the shared `cq_sandwich` driver runs it
forwards, copies out, and runs it backwards — reversal is **structural** and cannot
be got wrong per-kernel (plan §0.1). Replay-in-reverse is only correct under:

> **I6** — inside a `cq_sandwich` compute half, every gate **target** is a bit of the
> scratch region. Scratch is born `BIT_ZERO`, so materialisation there emits no `X`,
> and step `s` emits an identical gate sequence forwards and backwards. Sources
> appear only as controls, and controls are never materialised.

Enforced two ways, both cheap: `cq_emit_*` takes controls as `const cq_bit *` and
targets as `cq_bit *` (a source cannot be materialised **by construction**), and in
Debug the context carries the active scratch extent and asserts the target lies
inside it. Violating I6 makes the reverse half silently non-cancelling — risk **R1**,
and the reason both mechanisms land before any kernel.

**Rule 9 — The controlled axis is an EMITTER MODE, not a kernel rewrite.** PRD §9's
promotion (`NOT→CNOT`, `CNOT→Toffoli`, `Toffoli→` 3-Toffoli sandwich, verbatim from
Bennett's `controlled.jl`) is a gate-level transform. It lives in a control stack on
the context (`cq_ctrl_push`/`cq_ctrl_pop`, one lazily-acquired shared ancilla
returned `|0⟩`); `cq_emit_x/cx/ccx` consult `ctx->ctrl_depth`. **Every kernel becomes
controlled for free and no kernel is aware the axis exists** (plan §0.3). Nested
control ANDs the flags into a single wire, so the promotion never sees more than one
control. Do not add a `_controlled` variant of a kernel.

**Rule 10 — Test first: `Red → Green → Gate`.** The test file is written and failing
before the module exists. No module is "done" without its gate passing (plan rule 1).
The levels, and what each one is actually for:

| | Asserts | Notes |
|---|---|---|
| **L0** | The §3 fold table, exhaustively | Target/control over the 5 operand kinds: 5 X + 25 CX + 125 CCX = **155**, plus the 2 distinctness asserts. Each case pins gates emitted, qubits allocated, resulting bit-kind, **and** shadow. **Count unsettled** — plan §4 gates Step 6 at "175/175"; see Open blockers |
| **L1** | `shadow(dst) == refmodel(a,b)` | Exhaustive at `W ∈ {1,2,4,8}` × bit-kind masks; sampled at `W ∈ {16,32,64}` |
| **L2** | Live-qubit set == exactly `dst`'s qubits | Automatic on every L1 case |
| **L3** | forward → `_unc` → all-zero **and** pool restored | Values and pool state only — see Rule 14 |
| **L4** | `(NOT, CNOT, Toffoli)` per kernel per `W` | Pinned goldens, cross-checked against the Bennett gate-count formula |
| **L5** | The classical short-circuit | **Zero** gates and **zero** qubits fully-classical; exactly 1 qubit / 1 CX for `int a = 0; a \|= b << 3` |
| **L6** | CQ_lang e2e trace diff | The **first** layer that **links**, and the only one with trace goldens (L7 links too) |
| **L7** | Grover | §12 — the acceptance gate |

L1 and L5 are the two that actually catch bugs. L4 is what stops a "harmless"
refactor from silently doubling the T-count.

**Rule 11 — Step 6 (the fold table) is the critical path; over-invest there.** The
fold table is the **only** place classical/quantum is decided. A bug there is a bug
in all twelve kernels simultaneously, and it will present as a kernel bug (plan §5).
Its exhaustive fold-table suite is the most important in the project — everything
above it is Bennett transcribed against three functions. Operand distinctness (`c != t`,
`c1 != c2 != t`) is **asserted, not assumed**: a coincident operand is a meaningless
channel and a real miscompile signature.

**Rule 12 — ≤ 300 lines per hand-written module, enforced by CI, not by discipline.**
Counted as non-blank, non-comment lines in any hand-written `.c` / `.h` / `.py`
(`src/`, `include/`, `shim/*.py`, `tests/`). Exempt: generated `*.gen.c` and
`third_party/`. **Every module over ~200 lines in plan §3's Layer 0–3 tables already
has its split seam recorded** — hitting the limit is a scheduled split, never a
surprise refactor. Two gaps to close before they are written: **M26**
(`cq_runtime_impl.c`, 220) and **M27** (`gen_shim.py`, 280) live in the Layer 5 table,
which has no seam column at all. Large static test tables move to `.inc` files rather
than inflating a test module.

**Rule 13 — Emission is a stream, not a structure.** **The library** holds no circuit
object, no gate list and no statevector (NORTH_STAR §4, PRD §1 constraint 3), and
there is **no simulator anywhere** — not now, not as a test convenience. A gate is
emitted through a function pointer and is gone *from our side*; what a **sink** does
with it is the sink's business, which is exactly why `tests/support/mock_sink` may
record the `(op, operands)` stream and why the sandwich palindrome check is possible.
Nothing in `src/` may hold one. The backend's entire mutable state is the handle
table, the qubit pool with its free list, and one classical shadow bit-pair per qubit.
This is what lets one library serve a gate counter, a printf trace, a QEC driver and a
classical test harness with no duplication, and it is why the library stays small
enough to be obviously correct.

**Rule 14 — Never assert bit-kinds across the uncompute axis.** `_unc` legitimately
emits a **larger** gate sequence than the forward did: because we never demote (D6),
an in-place `cqrt_ry`/`cqrt_rz` on a *source* between the forward call and the
uncompute point materialises bits that were constants at forward time. CQ restores
the source's **state**, not our **representation** of it. The XOR still cancels — the
same `f(a,b)`, a different circuit realising it. Consequences (PRD §10): (i) the only
sound postcondition is on **values**, never on kinds; (ii) L4 pins forward and `_unc`
counts **separately** — `unc == forward` is **not** an invariant. Risk **R6** is that
someone "fixes" this asymmetry by asserting equality; the test file must quote the
PRD §10 note so the next reader knows the inequality is deliberate.

**Rule 15 — The θ ≡ π asymmetry is load-bearing; get it exactly right.**
`Ry(π) = XZ`, i.e. `X` up to a **relative** sign on `|1⟩`. On a bit that is already a
definite classical constant that sign is *global* and unobservable, so the bit stays
classical: **flip the constant, 0 gates, 0 qubits**. On a bit that is already a qubit
— possibly in superposition, possibly entangled — the sign is observable and must be
emitted: `X` then `Z`. `Rz` on a constant is **nothing** at every φ (diagonal on a
definite value is a global phase). Getting this asymmetry right is what makes
classical-mode testing possible **without making it unsound** (PRD §7). Angle
comparison is an exact-multiple test against a configurable tolerance, default
`1e-12` relative; note `Ry` folds mod 4π and mod 2π on different rows.

**Rule 16 — Skepticism; verify, do not recall.** Check the actual document before
citing it, and check the filesystem before naming a file — most paths in these docs
do not exist yet. Do not quote a Bennett construction, gate count, or symbol name
from memory: read it out of the pinned checkout, or say you have not. `bd` memories
reflect what was true *when written*. Subagent output and prior-session claims get
verified, not trusted. All bugs here are deep and interlocked — a fix that passes one
test but violates an invariant elsewhere is not a fix.

**Rule 17 — Report only what you ran.** If you ran the L1 suite, say "L1 green",
never "tests pass". An unqualified green claim about layers you did not execute is a
false statement about verification, and this project's whole defence is that
verification claims are literal. Tests run under **both** configurations: `Debug`
(with `CQOPS_DEBUG_INVARIANTS` and `-fsanitize=address,undefined`) is where the
invariant checks live; `Release` is what gets its gate counts pinned. A count pinned
only in Debug is not pinned.

---

## Invariants — the quick table

| # | Invariant | Enforced at |
|---|---|---|
| **I1** | Every bit is exactly one of three kinds. Never two, never neither | M01 `bit.h`, Step 2 |
| **I2** | No qubit index appears in two live registers. Copies are physical, never aliases. *This is what makes `cqrt_free` sound* | M07 owner map, Step 7 |
| **I3** | A qubit on the free list is `\|0⟩` | M03 pool, Step 4 |
| **I4** | A register whose bits are all constants owns **zero** qubits | M07, Step 7 |
| **I5** | **No packed scalar, anywhere.** No `uint64_t classical`, no `uint64_t qmask`. Width-generic over `reg->width` | Everywhere; Rule 3 |
| **I6** | Inside a `cq_sandwich` compute half, every gate **target** is scratch | M05 + M09, Step 8; Rule 8 |

**Shadow discipline:** conservative in the safe direction **only**. The shadow may say
*unknown* when the truth is determinate (it forgets correlations); it may **never**
say determinate when the truth is unknown. Poison is sticky.

---

## Open blockers — do NOT silently pick a side

Five contradictions are unresolved. If your work depends on one, resolve it **in the
source document** first; never settle it implicitly in code.

**Tracked as Step 0.5** — three PRD-internal contradictions:

1. **Symbol count: 1455 vs 1474.** PRD §1 says 1455 purely-integer (1455 + 878 fp =
   2333); §13 and §14 both say 1474. Recount from the real `opcode_table.yaml`.
2. **`_unc` vs `cqrt_free` qubit ownership — blocks M09's API.** PRD §10 says `_unc`
   returns qubits to the pool *and* leaves bits as "known-zero qubits", which
   double-frees on a following `cqrt_free`. Pick one **before** M09.
3. **L4 golden tuple arity.** `x+1` at Int8 is given as `58/6/40/12` — four numbers
   against a three-tuple `(NOT, CNOT, Toffoli)`.

**Tracked as Step 0.7** — `cqrt_h`. PRD §1 lists `cqrt_h` in the core runtime family,
but the §8 sink vtable has **no `h` entry**, and §12's Grover builds `H` out of
rotations (`φ += π; θ += π/2`) rather than as a primitive. Either `cqrt_h` is realised
via rotations, or it is an over-declaration, or the vtable is short an entry.
Establish which **before M04 freezes the vtable shape in Step 5**; do not add an `h`
sink entry on a guess.

**Tracked as Step 0.8** — the fold-table case count, which gates the critical path.
Plan §4 Step 6 enumerates `5 X + 25 CX + 125 CCX` over the five operand kinds — that
is **155** — and then gates the step at **"175/175"**. PRD §11 gives no count. 155
exhaustive + the 2 distinctness asserts on the same row is 157, and 175 is a digit
transposition of 157, so the plan's figure is most likely a typo. Settle it **in the
plan** before writing `test_emit_fold.c`; do not pin the suite size in code on a guess.

> Steps 0.7 and 0.8 were found after `IMPLEMENTATION_PLAN.md` was written, so they
> exist in the tracker but **not** in the plan's §1 Step 0 table, which still lists
> only 0.1–0.6. Fold them into the plan when Step 0 is next touched.

---

## Build & Test

**There is no build system yet.** Step 1 creates it; until then, none of the commands
below exist. Do not report them as run.

The intended shape (plan §2.1–§2.3):

```bash
# Configure both configurations. Debug defines CQOPS_DEBUG_INVARIANTS
# (I2 owner map, I6 scratch extent, distinctness asserts) + ASan/UBSan.
cmake -S . -B build-debug   -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release

# Tests run under BOTH — the invariant checks are the point of Debug,
# and Release is what gets its gate counts pinned (Rule 17).
ctest --test-dir build-debug   --output-on-failure
ctest --test-dir build-release --output-on-failure

# The 300-line guard (Rule 12). Runs in CI and locally.
make lint            # tools/check_loc.sh

# Regenerate L4 gate-count goldens — deliberately, never by reflex.
ctest --test-dir build-release -R kernel -- --update-goldens
```

C11, `-Wall -Wextra -Werror -Wconversion`. One test binary per module via
`add_cqops_test(name)`. The `tests/support/` harness is hand-rolled (no dependencies
beyond libc): `harness`, `mock_sink` (the workhorse recording fixture), `refmodel`,
`bitkinds`, `poolcheck`.

CI is **in scope** for this project (unlike CQ_lang): it runs `check_loc.sh` and
regenerates-and-diffs the shim from `opcode_table.yaml`.

---

## Hallucination-Risk Callouts (specific things agents get wrong here)

- **Almost nothing named in these docs exists on disk.** No `src/`, no
  `third_party/bennett/`, no `docs/constructions/`. Check before you cite.
- **CQ_lang is a separate repository** at `/Users/sorenwilkening/Desktop/CQ_lang`. Its
  ABI is **frozen and not ours to change**. We satisfy it; we do not negotiate with
  it. `opcode_table.yaml` is *copied in* at a pinned revision, never edited here.
- **i128 has no `cqrt_*` core symbol at all** — no `cqrt_alloc_i128`, no
  `cqrt_measure_i128`, no `cqrt_copy_i128` — and **no `icmp` at i128**, because the C
  ABI shreds `__int128` into `{i64,i64}` at a function boundary. An i128 register is
  born from a `zext`/`sext` and dies at a `trunc`. The only signature it reaches is
  the `_hl` shape.
- **234 of the 878 fp symbols look integer-ish and are not** — the cross-domain casts
  (`sitofp`, `uitofp`, `fptosi`, `fptoui`, `bitcast`) carry *both* an integer and a
  floating-point width. The partition that balances is 1455 + 878 = 2333.
- **`cqrt_cswap` with a CONSTANT control is 0 gates** — swap the two `cq_bit` arrays
  and emit nothing. Only a quantum control becomes a Fredkin per bit
  (`CX(b,a); CCX(ctrl,a,b); CX(b,a)`).
- **A tainted `select` is the common path into `cqrt_copy_<W>_controlled`** — it is
  reachable from ordinary C, not an exotic corner. The three "easy to miss" core
  families in PRD §2.1 are link failures if omitted, not missing features.
- **`_unc` emitting more gates than the forward is CORRECT** (Rule 14).
- **The plan's module map supersedes PRD §14's layout sketch** where they differ — the
  plan splits `shift` into `shift_const`/`shift_var`, `add` into `add`/`addacc`, and
  `divrem` into `divrem_u`/`divrem_s`.
- **Plan §0.3 prints `cq_ctrl_pop(cq_cxt*)`** — that is a typo for `cq_ctx*`.
- **D7 aliasing is unproven, not impossible.** Nothing in CQ's docs forbids
  `cq_template_add_i32(h, h)` or `_unc(out, out, b)`. v1 asserts loud and finds out
  empirically. If it fires, the fix is a defensive `cqrt_copy` of the aliased operand
  — **one place, not twelve** (risk R2).

---

## Key Prohibitions

- **Do NOT invent a reversible construction.** Look it up in Bennett.jl (Rule 1).
- **Do NOT add a simulator anywhere**, and do NOT add a circuit object, gate list, or
  statevector **to the library** (Rule 13). The two-bit shadow is the whole of our
  "simulation". The one sanctioned recording of the gate stream is the `mock_sink`
  test fixture.
- **Do NOT add `H`, `T`, or any gate above 2 controls to the classical path** (Rule 4).
- **Do NOT introduce a packed `uint64_t` classical/qmask scalar** anywhere (I5).
- **Do NOT hand-write or fork the shim.** It is generated from CQ_lang's own
  `opcode_table.yaml` so the symbol grid cannot drift from the ABI it must satisfy.
  Edit **the generator** — never the generated `*.gen.c`, and never the pinned
  `opcode_table.yaml` copy, which is a verbatim mirror of CQ_lang's frozen ABI. To
  pick up an ABI change, re-copy it at a new pinned CQ_lang revision.
- **Do NOT hand-write a reverse pass in a kernel** — use `cq_sandwich` (Rule 8).
- **Do NOT add a `_controlled` variant of a kernel** — the axis is an emitter mode
  (Rule 9).
- **Do NOT free a rail that is not provably zero**, and do not downgrade that hard
  error to a warning (Rule 6).
- **Do NOT assert forward/`_unc` gate-count equality or bit-kind equality** (Rule 14).
- **Do NOT implement gate-level optimisation** (cancellation, commutation, peephole
  fusion) in v1 — and no circuit optimiser before a working baseline.
- **Do NOT implement floating point in v1.** All 878 fp-touching symbols get a loud
  abort naming the symbol, so the link always succeeds and the v2 boundary is visible
  at runtime instead of at link time.
- **Do NOT implement any part of error correction.** We call the QEC library; we do
  not implement it. Angle-representation conversion is the **QEC sink's** problem —
  the `Ry` sink entry stays `double` all the way down.
- **Do NOT implement shadow-driven demotion (D6) in v1** — it makes the qubit count
  depend on shadow precision, which makes L4 goldens fragile. It is recorded as the
  thing that *would* make forward and `_unc` counts agree; revisit only if that
  asymmetry becomes painful.
- **Do NOT add dependencies beyond libc** (PRD §14). The test harness is hand-rolled
  for this reason.
- **Do NOT use TodoWrite / TaskCreate / markdown TODOs / `MEMORY.md`** — use `bd` and
  `bd remember`.

---

## Conventions

- **C11**, CMake, no dependencies beyond libc — mirroring CQ_lang's own build so the
  two link without ceremony.
- **Library `libcqops`.** Prefix `cqops_` for public symbols, `cq_` for internal ones.
  Public API in `include/cqops/cqops.h`.
- **Handles are monotonic and never reused** (D5), matching CQ_lang's existing `h<N>`
  trace convention; a freed slot becomes a **tombstone**. Qubit *indices* are reused.
- **Free-list discipline is LIFO** (D4). Lowest-index-first would give tighter peak
  counts but noisier trace diffs; revisit at L4, not before.
- **Pool ceiling is configurable, default unbounded** (D2); set it to `qec_n_logical`
  when the QEC sink is active. Exceeding it fails loud.
- **`sdiv`/`srem` by zero is deterministic-but-unspecified** (D3), documented, and
  **never traps**. Assert it does not trap; pin whatever it returns.
- **Kernel goldens pin COUNTS, not traces.** Trace goldens live only at L6, where
  CQ_lang owns the format — otherwise they churn on unrelated D4 free-list and D6
  non-demotion changes (risk R5). Goldens carry the Bennett commit in a header
  comment (risk R3).
- **Non-interactive shell flags always** (`cp -f`, `mv -f`, `rm -f`, `rm -rf`) —
  `cp`/`mv`/`rm` may be aliased to interactive `-i` and hang the agent. See
  [`AGENTS.md`](AGENTS.md).

---

<!-- BEGIN BEADS INTEGRATION v:1 profile:minimal hash:1105d646 -->
## Beads Issue Tracker

This project uses **bd (beads)** for issue tracking. Run `bd prime` to see full workflow context and commands.

### Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --claim  # Claim work
bd close <id>         # Complete work
```

### Rules

- Use `bd` for ALL task tracking — do NOT use TodoWrite, TaskCreate, or markdown TODO lists
- Run `bd prime` for detailed command reference and session close protocol
- Use `bd remember` for persistent knowledge — do NOT use MEMORY.md files

**Architecture in one line:** issues live in a local Dolt DB; sync uses `refs/dolt/data` on your git remote; `.beads/issues.jsonl` is a passive export. See https://github.com/gastownhall/beads/blob/main/docs/core-concepts/sync-concepts.md for details and anti-patterns.

## Agent Context Profiles

The managed Beads block is task-tracking guidance, not permission to override repository, user, or orchestrator instructions.

- **Conservative (default)**: Use `bd` for task tracking. Do not run git commits, git pushes, or Dolt remote sync unless explicitly asked. At handoff, report changed files, validation, and suggested next commands.
- **Minimal**: Keep tool instruction files as pointers to `bd prime`; use the same conservative git policy unless active instructions say otherwise.
- **Team-maintainer**: Only when the repository explicitly opts in, agents may close beads, run quality gates, commit, and push as part of session close. A current "do not commit" or "do not push" instruction still wins.

## Session Completion

This protocol applies when ending a Beads implementation workflow. It is subordinate to explicit user, repository, and orchestrator instructions.

1. **File issues for remaining work** - Create beads for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **Handle git/sync by active profile**:
   ```bash
   # Conservative/minimal/default: report status and proposed commands; wait for approval.
   git status

   # Team-maintainer opt-in only, unless current instructions forbid it:
   git pull --rebase
   git push
   git status
   ```
5. **Hand off** - Summarize changes, validation, issue status, and any blocked sync/commit/push step

**Critical rules:**
- Explicit user or orchestrator instructions override this Beads block.
- Do not commit or push without clear authority from the active profile or the current user request.
- If a required sync or push is blocked, stop and report the exact command and error.
<!-- END BEADS INTEGRATION -->

---

## Where Things Live

**Planned** (plan §3 — none of this exists yet):

| Layer | Modules |
|---|---|
| 0 — primitives | M01 `bit.h` · M02 `shadow` · M03 `qubits` · M04 `sink` |
| 1 — emission | M05 `emit` (the fold table — Rule 11) · M06 `controlled` |
| 2 — registers, sandwich | M07 `reg` · M08 `scratch` · M09 `sandwich` |
| 3 — kernels | M10 `bitwise` · M11 `shift_const` · M12 `shift_var` · M13 `cast` · M14 `add` · M15 `addacc` · M16 `cmp` · M17 `mux` · M18 `mul` · M19 `divrem_u` · M20 `divrem_s` |
| 4 — analog, sinks | M21 `angle` · M22 `rotate` · M23 `sink_printf` · M24 `sink_count` · M25 `sink_qec` |
| 5 — shim | M26 `cq_runtime_impl.c` · M27 `gen_shim.py` · M28 generated `*.gen.c` (LOC-exempt) |

Hand-written total ≈ **3,400 LOC** across 27 modules. Kernels M10–M20 are independent
of each other and parallelisable once Step 9 lands.

**Docs map:** [`NORTH_STAR.md`](NORTH_STAR.md) (why) ·
[`PRD-v1.md`](PRD-v1.md) (what — fold table §3, kernels §6, rotations §7, sinks §8,
controlled §9, uncompute §10, tests §11, decisions §15) ·
[`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md) (how — §0 design decisions, §3
module map, §4 schedule, §5 critical path, §6 risks, §7 definition of done) ·
[`AGENTS.md`](AGENTS.md) (shell hygiene + beads) · `bd ready` (the live work queue).
