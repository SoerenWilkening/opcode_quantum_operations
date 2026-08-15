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
> | `bd` | The tracker. All 28 steps (0–27) are filed, plus **sixteen** Step 0 sub-tasks: the plan's 0.1–0.6, then 0.7–0.16 for contradictions and scope gaps found after the plan was written. `bd ready` |
>
> **Steps 1–8 have landed (2026-08-15): Layer 0, the emitter, the handle table
> and the sandwich.** On disk and passing under **both** configurations,
> **67 ctest tests** — and green again under a full ASan + UBSan build with
> Homebrew clang:
>
> | Step | Module | Files | LOC / budget |
> |---|---|---|---|
> | 1 | — | `CMakeLists.txt`, `cmake/`, `include/cqops/cqops.h`, `src/version.c`, `tests/support/harness.[ch]`, `tools/check_loc.sh`, `Makefile` | — |
> | 2 | **M01** | `src/bit.h` — header-only, all `static inline`, **no translation unit** | 64 / 70 |
> | 3 | **M02** | `src/shadow.[ch]` | 114 / 130 |
> | 4 | **M03** | `src/qubits.[ch]`, plus `tests/support/death.[ch]` | 130 / 150 |
> | 5 | **M04** | `src/sink.[ch]` + `cq_sink` in the public header, plus `tests/support/mock_sink.[ch]` | 108 / 90 · 139 / 120 |
> | 6 | **M05** | `src/emit.[ch]` — the §3 fold table — plus `src/ctx.[ch]`, the shared context | 122 / 190 |
> | 7 | **M07** | `src/reg.[ch]` — handle table, tombstones, the sole deallocator, the I2 sweep, D7a/D7b | **283 / 180** — see below |
> | 8 | **M08** | `src/scratch.[ch]` — the `cq_bit` array, no `cq_ctx` in the header, no release | 56 / 90 |
> | 8 | **M09** | `src/sandwich.[ch]` — the driver, I6(a)+(b), `CQ_ZERO_BY_PALINDROME` — plus `cq_shadow_retire` (M02) and `cq_ctx_release_qubit` | 115 / 110 |
>
> **The fold table has landed and is green at 159/159** (155 exhaustive + 4 distinctness
> deaths), so the critical path is behind us. `cq_ctx` now exists: pool + shadow + a
> borrowed sink, the `sandwich_depth` counter, and in Debug the I6 scratch extent.
>
> **M07 overshot its budget by 57% and landed as one module anyway** (54 header + 229
> body, against a 300-line hard limit that is nowhere near). The overshoot is four
> things §3's 180 did not anticipate: a **three-state** slot (§10 needs live / tombstone
> / measured, and a boolean carries two), the `INT32_MAX` handle guard, the **D7a/D7b
> split**, and the two-pass free. Plan §3's recorded seam — *table ↔ invariant
> checking* — is unused and stays available: `cq_reg_audit`, `cq_reg_check_operands`
> and `cq_reg_sources_alias` move to `src/reg_check.c` if `reg.c` passes 240.
> `tests/test_reg.c` did hit the guard and split along the same line, into
> `tests/test_reg_invariants.inc`.
>
> **The sandwich has landed and the `ckd.17a` certificate is on disk, not just
> designed.** `cq_sandwich` pre-materialises (I6(b)), arms the extent for the two
> compute halves only, replays the compute half at descending indices, and releases the
> region through `cq_ctx_release_qubit(ctx, q, CQ_ZERO_BY_PALINDROME)` — the sole
> `proven_zero` constant in `src/`. `cq_reg_free` was rewired through the same joint,
> which closes the Step 7 hazard where a **reused** qubit index kept its stale shadow
> entry. **32 mutations run across two rounds, 31 killed**; the one survivor is
> `cq_scratch_alloc`'s Debug `0xAA` poison, an equivalent mutant in isolation whose
> value is proved by the paired mutation (see below).
>
> **Next is Step 9 — M23 `sink_printf` + M24 `sink_count`,** which completes PRD
> increment 1. Still a plan: no kernel and no controlled axis (M06 is Step 20, not
> Step 8, despite its low module number).
>
> **Step 0 is substantially done, so the references DO now exist on disk:**
>
> | Path | What |
> |---|---|
> | `third_party/bennett/` | Bennett.jl @ `980805de85314b3da7ac25cf6454b56566f8e609` — a stripped snapshot (no `.git`, no `.beads`) plus `COMMIT` and `.provenance/MANIFEST.txt`. Note `git log` inside it reports the **parent** repo's HEAD; read `COMMIT` to check the pin |
> | `third_party/cq_lang/` | `opcode_table.yaml` verbatim @ CQ_lang `a6a92fe`, plus `COMMIT`. **Never edit it** |
> | `docs/constructions/K01..K12.md` | The ported construction specs, each with a gate-count formula in `W` |
> | `docs/constructions/BASELINES.md` | Upstream baselines, and which one Step 12 pins against |
> | `docs/cqrt_census.txt` | The real `cqrt_*` census (**173** symbols) and the resolved template counts |
>
> Everything **else** named in this file or in the PRD is still a plan. Do not claim a
> file exists because a document names it — check (Rule 16). And note the K-docs are
> **not yet mutually consistent**: four of the twelve goldens are contingent on an
> unmade decision — see Step 0.9 under Open blockers.

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

**Rule 6 — Free only a provably-zero rail.** `cqrt_free` asserts that every
**qubit-carrying** bit is provably `|0⟩` before returning qubits to the pool. **A free
of a dirty rail is a hard error, not a warning** (PRD §10) — it is the exact signature
of a silent state collapse. The scope is the point: this rule used to read "every bit is
`BIT_ZERO` or a known-zero qubit", which rejects a `CQ_BIT_ONE` bit and so aborts on
`int x = 5;` going out of scope — every ordinary classical local, and the exact shape L5
requires to cost zero. By **I4** an all-constant rail owns zero qubits, so nothing can
reach the free list; PRD §10's own "return every qubit `h` still owns" is the operative
wording and this line now matches it. It does **not** let `ckd.18` through — there the
bits *are* materialised qubits. Likewise a qubit on the free list is `|0⟩` (**I3**), and
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
| **L0** | The §3 fold table, exhaustively | **159** = 5 X + 25 CX + 125 CCX = **155** exhaustive over the 5 operand kinds, plus **4** distinctness death-tests (`c==t`; `c1==c2`, `c1==t`, `c2==t`). Each case pins gates emitted, qubits allocated, resulting bit-kind, **and** shadow — four *assertions* per case, not four cases. The table branches on **kind only, never shadow** (that is D6 no-demotion): only `3+9+27 = 39` gate-behaviour classes exist, and the 155 split is there to pin the shadow |
| **L1** | `shadow(dst) == refmodel(a,b)` | Exhaustive at `W ∈ {1,2,4,8}` × bit-kind masks; sampled at `W ∈ {16,32,64}` |
| **L2** | Live-qubit set == exactly `dst`'s qubits | Automatic on every L1 case |
| **L3** | forward → `_unc` → all-zero **and** pool restored | Values and pool state only — see Rule 14 |
| **L4** | `(NOT, CNOT, Toffoli)` per kernel per `W` | Pinned goldens, cross-checked against the Bennett gate-count formula |
| **L5** | The classical short-circuit | **Zero** gates and **zero** qubits fully-classical; exactly 1 qubit / 1 CX for `int a = 0; a \|= b << 3` |
| **L6** | CQ_lang e2e trace diff | The **first** layer that **links**, and the only one with trace goldens (L7 links too) |
| **L7** | Grover | §12 — the acceptance gate |

L1 and L5 are the two that actually catch bugs. L4 is what stops a "harmless"
refactor from silently doubling the T-count.

**Two things about L4 that are counter-intuitive and cost real work to establish:**

1. **Every golden must name the operand mask it was taken at, and that mask is
   all-quantum.** Pre-materialising scratch (I6(b)) removes the *scratch* side's
   dependence on bit-kinds, but **operand folds still fire** — `a + 0`, `x − 1`, an
   all-`ZERO` operand all legitimately emit fewer gates, which is what L5 proves. Counts
   are a function of `(W, operand mask)`. All-quantum is the correct pin because, with no
   demotion (D6), a mask can only drift *towards* `Q`, making it the **fixed point** — the
   one mask where forward and `_unc` agree.
2. **A matching gate count is NOT evidence the sandwich cancelled.** Measured: replaying
   K12's forward list in reverse under the old rules with `a` all `Q`, `b` all `ZERO` gives
   a **different gate multiset with the identical total** (816 at W=8). L1 green *and* L4
   green, circuit wrong, scratch dirty — **only L2/L3 could see it.** The Prime Directive's
   "L1 *and* L2/L3 *and* L4 together" is right, but L4 is weaker than it looks.

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

If your work depends on one of these, resolve it **in the source document** first;
never settle it implicitly in code. `bd show <id>` for the full statement of each.

### Open

**`ckd.17b` — what evidence does `cqrt_free`'s "provably clean" assert read for a
CQ_lang RAIL? Bites Step 23, presents at Step 24.** **`ckd.17a`, the scratch half, is
RESOLVED — see below.** What is left is the half no in-library theorem can reach: a
sandwich certificate covers **none** of the corpus's 51,696 frees, because none of them
is on a scratch region. Measured 2026-08-15 over the 239 goldens: only **25,138 (49%)**
follow a `cq_template_*_unc`; **26,504 (51%)** are rails last written by a bare
`cqrt_toffoli` (19,153) or `cqrt_cnot` (7,223) — Phase-4 control flags that CQ_lang
uncomputes by **re-applying the same self-inverse gate**, with no `_unc` anywhere
(`slice_control_select_compound.expected.log:15-18` is the worked case). Two witnesses
show the general case is not provable here at all: `slice_loop_break.expected.log:10-25`
rests on loop-condition algebra that never reaches us, and
`specialize_transitive_caller.expected.log:3-16` uncomputes by recomputing into a
**different handle**, defeating any handle-keyed matching. At Step 23 M26 picks at one
greppable site between `CQOPS_FREE_ABORT` (default) and `CQOPS_FREE_RETIRE` (tombstone;
the indices leave circulation forever so I3 holds absolutely). **There is no
`CQOPS_FREE_TRUST` and one must never be added.**

**`ckd.18` — the rotation-root free breaks the assert *and* I3 on fixtures CQ_lang
ships as correct. Will abort Step 24.** `alloc_i32(5) → ry(θ) → ry(−θ) → cqrt_free`,
with **no `_unc` anywhere**, so no uncompute certificate can exist. In our model
`alloc(5)` is all-constant with zero qubits (I4); `ry` materialises all 32 bits with
shadow *unknown*; poison is sticky so `ry(−θ)` does not clear it. At the free the rail
is physically `|5⟩`. Suppressing the error is worse — it pushes `|1⟩` qubits onto the
free list, breaking I3.
**Scope measured 2026-08-15, and the bead was filed 25× too narrow in one direction and
12× too wide in the other.** It is **25** frees across many fixtures, not one — and the
partition is perfect: the 25 are exactly the `ry`-rooted ones, every one born from a
**non-zero** `alloc` literal with an exact `(θ, −θ)` history. The other 12
rotation-rooted frees are `rz`-only on rails born `0`, and `Rz` on a definite value is a
**global phase** (Rule 15: "`Rz` on a constant is **nothing** at every φ") — those rails
are genuinely `|0⟩` and freeable; they hard-error only because `cq_shadow_rotate`
poisons unconditionally, which is **M21's** business (§7 angle classification), not this
bead's. **The tempting wrong fix is wrong on all 25:** cancellation restores the *birth
constant*, never zero. The discriminator is `birth-value == 0`, and the fix needs
corrective `X`s driven from the mint record — which is **not** D6, since the value comes
from the `alloc` literal, not from shadow precision. Decide **before Step 19**.

**Smaller, filed** — `ckd.13` K12's quadratic ancilla scheme (32,960 qubits at W=64);
`ckd.15` K10's three sources vs Rule 7's two; `ckd.16` M11/M12 shift-out-of-range disagreement.

### Resolved 2026-08-14 — recorded so they are not re-litigated

- **`ckd.17a` (scratch) and `ckd.14` — SETTLED 2026-08-15. The certificate is not stored;
  it is an ACT.** Both beads were one mechanism seen from two sides. **(a) One gate per
  step** — forced, because the driver re-calls `compute(env, s)` with the *same* argument
  on the reverse pass, so a step must be an involution; `K06.md:566-586` and
  `K10.md:153-171` each give a worked block that is not. **(b) `cq_sandwich` contains no
  shadow call at all** — it asserts its *own premises* (no nesting; every scratch bit
  `CQ_BIT_ZERO` on entry then `CQ_BIT_Q` after step 1; an order-sensitive region checksum
  unchanged across each half), and K06's "assert-and-reset the scratch shadow" and K11's
  "the driver does not reset, so the free aborts" were both mis-framed. **(c) The write is
  a RETIREMENT, not an un-poison:** `cq_shadow_retire(sh, q)` runs strictly *after*
  `cq_qubits_release` returns, so it never touches a live qubit, and by **I3** `{0, 0}` is
  the *correct* entry for a free-list index — bit-for-bit what `cq_shadow_ensure` writes
  for a fresh one. Birth and retirement are one rule. **(d) One joint, and the order is the
  enforcement:** `cq_ctx_release_qubit(ctx, q, proven_zero)` = release, then retire.
  **(e) One named literal, `CQ_ZERO_BY_PALINDROME`, in M09's epilogue** — the sole
  `proven_zero` constant in `src/`, resting on three premises (one-gate-per-step, I6(a),
  I6(b)). Full statement in PRD §10 and plan §0.1.
  > **Why a live qubit may never be certified, structurally:** a certified-but-live qubit
  > read as a *control* hits `t.unknown |= c.unknown`, so with `c.unknown` freshly zeroed
  > the poison **stops propagating** and the shadow claims determinate downstream of a real
  > superposition. That disqualifies the `_unc` epilogue as a stamper on its own.
  >
  > **BUILT AT STEP 8, AND ONE DETAIL OF (b) CHANGED IN THE BUILDING.** The "order-sensitive
  > region checksum" is checked after **each of the three loops**, the copyout included —
  > the extent is deliberately disarmed there, so the fingerprint is the *only* thing that
  > can see a copyout step reaching back into scratch. The second all-`CQ_BIT_Q` sweep the
  > design implied on entry to the reverse half is **not** implemented: the baseline is
  > taken all-`Q` and an unchanged fingerprint carries that forward, so a second sweep
  > would have no case that distinguishes it. `cq_reg_free` now goes through the same
  > `cq_ctx_release_qubit` joint, which closes the Step 7 hazard about a reused index
  > keeping a stale entry.

- **`_unc` vs `cqrt_free` ownership — `cqrt_free` is the SOLE deallocator.** `_unc`
  zeroes values in place and reclaims **nothing**: no pool operation, no bit-kind
  rewrite, no handle-table change. Forced empirically rather than chosen: CQ_lang
  decides reclamation **per rail** and its only lever is emitting or withholding the
  free, so the *same* `_unc` symbol appears both freed and deliberately never freed —
  the latter on a rail it has proven entangled. Reclaiming at `_unc` would return an
  entangled qubit to the free list. 3 judges, 3–0; 239 goldens, 25,147 `_unc` calls,
  **0** double-frees, and 26,558 freed handles that never saw an `_unc` at all. **A
  rail `_unc`'d and never freed stays allocated for good — that is the intended Rule-6
  safe leak, not a bug.** PRD §10.
  > **The plan mis-stated this as "decides M09's API". It does not.** M09 is
  > `sandwich.[ch]`; its driver runs over **scratch** and never touches a result rail's
  > ownership. Step 8 was never blocked. The `_unc` axis is Step 21, in M26.
- **Sandwich scratch is PRE-MATERIALISED** (`cq_sandwich` step 0), now invariant
  **I6(b)** in plan §0.2, closing risk **R8**. I6 as written constrained only gate
  *targets*; the hazard is on the **control** side — a scratch bit read as a control
  while still `BIT_ZERO` folds to 0 gates forward, and if a later step materialises it
  the reverse replay emits a gate the forward never did, so the sandwich stops
  cancelling **while L1 stays green**. Pre-materialising costs **qubits, never gates**
  (scratch is born 0) and makes kernel gate counts a function of `W` alone — which is
  what makes one L4 golden per `(kernel, W)` sound. New risk **R9**: the all-classical
  path must short-circuit *before* the sandwich or L5 breaks.
- **i80 is IN scope** — integer grid **1595**. **The two sibling yamls are OUT** — M27
  generates from `opcode_table.yaml` only; CQ_lang supplies the other 401, so Step 23's
  gate is "no undefined `cq_template_*` **from the opcode grid**".
- **K11 uses Cuccaro**, a deliberate delta from upstream (Bennett's `multiplier.jl:29`
  calls ripple). Saves ~3× scratch qubits. Cuccaro's `_unc` bar does not apply: the
  accumulator is internal and never exposed to CQ_lang's `_unc` contract.
- **Symbol count.** All three published figures were wrong. Current: **2479** total,
  **884** fp-touching, **1595** purely-integer (**1455** excluding i80). `1732` was a
  stale comment; `1474` was a **phantom** — it matches no revision and no partition.
  PRD §1.
- **L4 golden tuple arity.** No contradiction: Bennett's `gate_count` returns a
  **4-field** NamedTuple `(total, NOT, CNOT, Toffoli)`, so `58/6/40/12` has a redundant
  leading sum — `6+40+12 = 58`. Pin the three-tuple, carry `total` as a checksum, and
  **always match the full tuple** (two unrelated upstream circuits both total 114).
- **Which baseline Step 12 pins against.** `58/6/40/12`, **not** BENCHMARKS.md's
  `100/4/68/28` — that file is stale (pre-U27/U28 defaults, and its generator no longer
  runs). But `x+1` is a *constant increment*; **K6 is a general two-register add** and
  sandwiches to `11W−4` = **84** at i8. Do not pin K6 against 58.
- **`cqrt_h`.** An over-declaration: declared and defined in CQ_lang, **emitted by
  nothing, called by nothing**. Struck from PRD §1. §8's 6-entry vtable is complete as
  printed — **M04 is unblocked**. Grover-from-rotations is forced, not chosen.
- **Fold-table count.** **159** = 155 exhaustive + **4** distinctness death-tests (not
  2 — PRD §3 asks for all three `CCX` pairs). "175/175" was a typo.
- **PRD §3 had a 15-case hole.** It carried a `c1 = ONE` row and no `c2 = ONE`
  counterpart, leaving `(c1 = Q, c2 = ONE)` — 15 of the 125 `CCX` cases — matched by no
  row. `CCX` is symmetric in its controls; fixed by a control swap before dispatch. If
  you are reading a PRD without the `c2 = ONE` row, stop and re-check.
- **PRD §3's emitter prototypes were non-`const`**, which would have silently disarmed
  one of the two mechanisms enforcing I6. Controls are now `const cq_bit *`.

---

## Build & Test

**The build exists as of Step 1.** These commands are real; the `--update-goldens`
one is not, and arrives with the first L4 golden at Step 12.

```bash
# Configure both configurations. Debug defines CQOPS_DEBUG_INVARIANTS
# (I2 owner map, I6 scratch extent, distinctness asserts) + sanitizers.
cmake -S . -B build-debug   -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release

# Tests run under BOTH — the invariant checks are the point of Debug,
# and Release is what gets its gate counts pinned (Rule 17).
ctest --test-dir build-debug   --output-on-failure
ctest --test-dir build-release --output-on-failure

# The 300-line guard (Rule 12). Both spellings run tools/check_loc.sh.
make lint
cmake --build build-debug --target lint

# Everything at once: lint, then both configurations.
make test

# NOT YET REAL — regenerate L4 goldens. Arrives at Step 12 with K6.
ctest --test-dir build-release -R kernel -- --update-goldens
```

**Sanitizers are probed, not assumed** (`cmake/CqopsSanitizers.cmake`). Apple clang 17
on this dev box (macOS 26 / Darwin 25, x86_64) has a **broken ASan runtime** — a
trivial `main` built with `-fsanitize=address` dies with `SIGILL` in `libsystem_pthread`
before reaching `main`. If a Debug binary SIGILLs at startup, that is the toolchain,
not libcqops. So the build compiles-and-*runs* a probe per sanitizer and enables only
what works, printing a CMake warning for what is missing, and `test_skeleton`
cross-checks the build's belief against the compiler's `__has_feature`. UBSan works on
Apple clang and genuinely aborts (`-fno-sanitize-recover=all`). For full coverage:

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DCQOPS_SANITIZERS=ON -DCMAKE_C_COMPILER=/usr/local/opt/llvm/bin/clang
```

`CQOPS_SANITIZERS` is `AUTO` (default, use what runs), `ON` (require them — a hard
configure error if a sanitizer does not run) or `OFF`. **Debug on the default
toolchain is currently UBSan-only; say so when reporting what was verified.**

C11, `-Wall -Wextra -Werror -Wconversion`. One test binary per module via
`add_cqops_test(name)` — plus an optional `WILL_FAIL`, used by
`tests/test_harness_negative.c` to assert the harness's *failure* path. A `CHECK` that
could not fail would make every suite in the project vacuously green, so that path is
tested rather than assumed; if `test_harness_negative` ever starts passing its own
binary, the harness is broken, not fixed.

**Hard errors get `add_cqops_death_test(name CASES a b c)`, never `WILL_FAIL`.** That
property inverts a non-zero *exit code* and does not invert a crash, so it cannot
express `abort()` at all. A death binary catches `SIGABRT` itself and exits 0 only when
the abort landed inside a `CQ_EXPECT_ABORT` window, which makes it an ordinary test,
makes "nothing aborted" a failure, and stops a sanitizer report from passing for the
death under test. `argv[1]` selects the case, so one file hosts many deaths and CMake
registers one ctest per case (`test_qubits_death.dirty_release`).

```bash
ctest --test-dir build-debug -R death            # every fail-loud path
./build-debug/tests/test_qubits_death             # lists its cases
```

`CQOPS_SINK` picks the default sink by name (PRD §8), so CQ_lang's fixtures select one
without a code change; `cqops_set_sink()` overrides it, and `NULL` returns to the
environment's choice. Unset or empty means absent, and the documented fallback is
`printf`. A name that resolves to nothing is a hard error, never a quiet substitution.

The `tests/support/` harness is hand-rolled (no dependencies beyond libc).
**`harness.[ch]`, `death.[ch]` and `mock_sink.[ch]` exist** — `death.[ch]` is beyond
plan §2.2's list of five, added at Step 4 because Steps 4 and 6 need ten aborts between
them. The other three land across Phase B: `refmodel`, `bitkinds`, `poolcheck`.

**`FAIL_REGULAR_EXPRESSION` now discriminates within one module, not only between two.**
Step 7 used it to prove M07's free aborted rather than M03's. Step 8 uses it to prove
*which of three calls of the same guard* fired — the region fingerprint after the
forward half, after the copyout, or after the reverse half. Same tool, finer grain; see
the callout below for why it was needed.

Some deaths are **Debug-only by design** — plan §2.1 gates the I2 owner map, the I6
scratch-extent check and the §3 distinctness asserts on `CQOPS_DEBUG_INVARIANTS`. Those
cases call `CQ_DEATH_SKIP_WITHOUT_INVARIANTS(...)` and report a **skip** in Release
rather than a pass, so a Release run never claims to have verified something the
configuration compiled out. Say which configuration a death was verified in (Rule 17).

`mock_sink` is the workhorse: it records the `(op, operands)` stream, compares against
an expected sequence and dumps the actual one on failure, and feeds `CHECK_GATES` its
three per-kind counts. **Angles compare bitwise, not with `==`** — `0.0` and `-0.0` are
equal in C but are different gates to emit. Step 8 added `cq_mock_is_palindrome(m,
n_head, n_mid)`, the ordered check PRD §10 names as the **only** detector with teeth on
the rotation-tainted surface, where `cq_shadow_retire` is inert — a gate *count* cannot
see an R8 divergence, because K12's reversed forward list has a different multiset with
the identical total.

**Tests reach internal headers directly** — `tests/CMakeLists.txt` puts `src/` on
`cqops_test_support`'s PUBLIC include path, so `test_bit.c` writes `#include "bit.h"`.
`src/` stays PRIVATE on the `cqops` target itself; the exception lives on the test side
rather than widening the library.

CI is **in scope** for this project (unlike CQ_lang): it runs `check_loc.sh` and
regenerates-and-diffs the shim from `opcode_table.yaml`. **Not wired up yet** — this
repo has no git remote, so there is nowhere for a workflow to run. Filed as its own
issue; `make test` is the local stand-in (lint, then both configurations).

---

## Hallucination-Risk Callouts (specific things agents get wrong here)

- **`cq_sandwich` DISARMS the I6 extent for the copyout, and arms it only for the two
  compute halves.** Copyout targets `dst`, which is *outside* scratch, so an extent armed
  across all three loops makes `src/emit.c`'s I6(a) check fire on every sandwich kernel.
  The plausible wrong fix — widening the extent to cover `dst` — silently disables I6(a)
  for the compute halves too, which is R1 with the detector removed.
- **M08 ships no release a kernel can call; M09 owns the scratch qubits end to end.**
  M08 owns the `cq_bit` array and its dispose asserts every bit is back to `CQ_BIT_ZERO`
  — a **kind** check, never a shadow read. That is what plan §3's "assert clean on
  release" actually becomes, and unlike a shadow reading it is implementable.
- **Layers 0–2 exist; Layer 3 and above do not.** `src/bit.h`, `src/shadow.[ch]`,
  `src/qubits.[ch]`, `src/sink.[ch]`, `src/ctx.[ch]`, `src/emit.[ch]`, `src/reg.[ch]`,
  `src/scratch.[ch]` and `src/sandwich.[ch]` are real as of Step 8 — but there is **no
  kernel**, no sink beyond the vtable (M23–M25 are Step 9 and Phase D), and **no
  controlled axis** (M06 is Step 20, despite its low module number). Check before you
  cite — and read `third_party/bennett/COMMIT` rather than running `git log` inside it,
  which reports the *parent* repo's HEAD because the snapshot has no `.git`.
- **THE SAME GUARD CALLED THREE TIMES IS ONE MUTATION AWAY FROM UNTESTED, and Step 8
  measured it.** `cq_sandwich` verifies its region fingerprint after each of its three
  loops. With the obvious two death cases in place, deleting **any one of the three**
  left all 65 tests green — a later call caught what the deleted one would have. This is
  the Step 6 and Step 7 lesson for the third time, and the fix is the same tool at a
  finer grain: a CTest `FAIL_REGULAR_EXPRESSION` naming the **loop** that must catch each
  case, plus a case that reaches the last check (a step that misbehaves only on the
  reverse pass, which the forward pass cannot reach). The identical shape holds for the
  two `sw_arm` calls. **Before adding a guard, ask which single case goes red if this
  exact line is deleted — and if a later copy of the same guard would catch it, the
  answer is "none".**
- **`CQ_ZERO_BY_PALINDROME` is a literal `1`, so M03's `proven_zero` guard can never fire
  for a sandwich.** The Release-configuration detector of a non-cancelling compute half
  is `cq_shadow_retire`'s determinate-and-non-zero check in **M02**, not the pool and not
  the Debug-gated fingerprint. PRD §10 bounds its reach exactly: complete across the
  rotation-free kernel surface, **inert** once a rail is rotation-tainted — so never
  report an L6 run as evidence the certificate held. On the poisoned surface the only
  detector with teeth is `cq_mock_is_palindrome`, which lives in `tests/`.
- **`cq_bit_coincident` has NO pointer-identity clause, and adding one breaks the fold
  table.** PRD §3 says the distinctness check "can only ever fire on `CQ_BIT_Q`
  operands, and cannot fire on two constants" — a pointer clause fires on one constant
  bit passed in both control slots, which is a legal fold (`CCX(o,o,t)` → `X(t)`). It is
  redundant besides: two `Q` bits at one address necessarily share an index. The PRD's
  own parenthetical said otherwise and was corrected at Step 6.
- **`cq_materialise` emits its `X` straight to the sink, not through `cq_emit_x`.**
  Deliberate and load-bearing for Step 20: once M06 makes `cq_emit_x` consult
  `ctrl_depth`, routing through it would promote the materialising `X` to a `CX` and
  leave the fresh qubit entangled with the control instead of in a definite state.
  Whether that is right is M06's call, and the current code declines to answer it.
- **The §8 vtable is frozen at six entries and there is no `h`.** Settled at Step 5 on
  Step 0.7's resolution: `cqrt_h` was an over-declaration in CQ_lang, so §12's Grover
  builds H out of rotations. Adding a seventh entry forks us from a frozen ABI.
  Relatedly, `cq_sink_active()` **never returns NULL** — an unresolvable `CQOPS_SINK`
  is a hard error, because quietly substituting a different sink hands the caller a
  circuit they did not ask for.
- **`cq_qubits_release` does NOT read a shadow.** Plan §4's Step 4 row says "releasing
  a qubit whose shadow is not known-0 is a hard error", which reads as though M03 looks
  it up. It does not, and must not: the signature is
  `cq_qubits_release(pool, q, int proven_zero)` and the caller supplies the evidence, as
  `cq_qubits_release(p, q, cq_shadow_known_zero(sh, q))`. Two reasons, one conclusion —
  plan §3 puts M03 in Layer 0 with no internal dependencies, and `ckd.17` establishes
  that the shadow **cannot** be the free-time oracle, so hard-wiring the lookup would
  bake in the very thing that bead says fails. Do not "tidy" it into a shadow read.
- **`WILL_FAIL` cannot express a death here.** CTest's `WILL_FAIL` inverts a non-zero
  *exit code* and does **not** invert a crash, and every hard error in this codebase is
  an `abort()`. Use `add_cqops_death_test(name CASES ...)` and `CQ_EXPECT_ABORT` from
  `tests/support/death.h`. `tests/test_harness_negative.c` is not a counter-example —
  it works because it exits non-zero *normally*.
- **A Debug binary that dies with `SIGILL` before `main` is the ASan runtime, not our
  code.** Apple clang 17 on Darwin 25 / x86_64 is broken this way; the build works
  around it by probing. `bd memories asan` has the details.
- **CQ_lang is a separate repository** at `/Users/sorenwilkening/Desktop/CQ_lang`. Its
  ABI is **frozen and not ours to change**. We satisfy it; we do not negotiate with
  it. `opcode_table.yaml` is *copied in* at a pinned revision, never edited here.
- **i128 has no `cqrt_*` core symbol at all** — no `cqrt_alloc_i128`, no
  `cqrt_measure_i128`, no `cqrt_copy_i128` — and **no `icmp` at i128**, because the C
  ABI shreds `__int128` into `{i64,i64}` at a function boundary. An i128 register is
  born from a `zext`/`sext` and dies at a `trunc`. The only signature it reaches is
  the `_hl` shape.
- **240 of the 884 fp symbols look integer-ish and are not** — the cross-domain casts
  (`sitofp`, `uitofp`, `fptosi`, `fptoui`, `bitcast`) carry *both* an integer and a
  floating-point width. The partition that balances is **1595 + 884 = 2479**. (The
  older `234` and `1455 + 878 = 2333` were correct at the revision the PRD was drafted
  against and are stale by the i80 increments — see PRD §1 and `docs/cqrt_census.txt`.)
- **The prescribed `cqrt_*` census command does not work.**
  `grep -rhoE '"cqrt_[a-z0-9_]*"' ir-pass/src` returns **18 results, and they are
  PREFIXES** (`"cqrt_addc_"`, `"cqrt_alloc_"`, …) — the pass concatenates the width
  suffix at emit time, so no expansion of that grep can yield a symbol count, and two
  symbols (`cqrt_h`, `cqrt_h_controlled`) are unreachable by it entirely. The real
  surface is **173**, established from the declaration layer and cross-checked against
  CQ_lang's own `core_abi_link_check.py`. See `docs/cqrt_census.txt` — and note the
  trap recorded there: `cq_runtime.h` is column-aligned, so the obvious regex silently
  drops 49 declarations and returns a plausible-looking 124.
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
- **D7 aliasing is no longer unproven — it was measured at Step 7, and the two halves
  came out OPPOSITE ways.** Over all 239 goldens (62,930 template calls, 25,147 `_unc`):
  **D7a**, `out` among the sources, is **0** — hard error, *both* configurations.
  **D7b**, two sources aliasing each other, is **599**, of which **10** are on v1's
  integer surface (`cq_template_mul_i32(h10, h10)` at
  `tests/e2e/slice_select_rail_alias_cond.expected.log:31`, and nine more) — **legal, and
  a blanket abort would fail those shipped fixtures at Step 24**. The defensive
  `cqrt_copy` is therefore *required*, at the M26 handle boundary in Step 23, before
  Step 24 runs — still **one place, not twelve** (risk R2, PRD §15 D7a/D7b). A kernel
  cannot do it: kernels see `cq_bit *` and `W`, never handles.

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

Plan §3's module map. **Bold = on disk; everything else is still a plan** (Rule 16 —
check, do not assume, and update this table when a step lands):

| Layer | Modules |
|---|---|
| 0 — primitives | **M01 `bit.h`** · **M02 `shadow`** · **M03 `qubits`** · **M04 `sink`** |
| 1 — emission | **M05 `emit`** (the fold table — Rule 11) · M06 `controlled` |
| 2 — registers, sandwich | **M07 `reg`** · **M08 `scratch`** · **M09 `sandwich`** |
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
