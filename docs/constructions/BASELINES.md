# Bennett.jl Published Gate-Count Baselines

**Step 0.4 deliverable.** PRD §11 L4 refers to "Bennett's published gate-count
baselines" without a path. This file supplies the paths, resolves which of the two
competing upstream `x+1 @ i8` figures Step 12 must pin against, and states the closed
forms.

> **Citation form — converted 2026-09-10 (`bd 0a7`).** This file's citations into
> *living* documents (`PRD-v1.md`) are now `§` + a `grep -nF`-unique quoted phrase +
> `file:line @ <sha>`. Measured: a re-measured `PRD-v1.md` line number survives about
> three commits, and four earlier re-measure cycles across the K-docs all went stale
> again. **Verify by grepping the phrase in the current file**; recover the exact
> original with `git show <sha>:<file> | sed -n <N>p`. Citations into `third_party/`
> keep bare line numbers — that tree is pinned and never moves — but the three refs that
> named Bennett's own operating manual by bare basename, and so read as **this repo's**
> `CLAUDE.md` when they are not, are now spelled `third_party/bennett/CLAUDE.md:27` in
> full.

| | |
|---|---|
| Upstream | `https://github.com/tobiasosborne/Bennett.jl` |
| Pinned commit | `980805de85314b3da7ac25cf6454b56566f8e609` (`third_party/bennett/COMMIT`) |
| Vendored | 2026-08-14 |
| All paths below | relative to `third_party/bennett/` unless prefixed |

> ## EVERY FIGURE HERE IS UNVERIFIED-BY-EXECUTION
>
> Julia is not installed on this machine. Nothing in this document was produced by
> running Bennett.jl. Every number is either **transcribed** from an upstream file
> (with `file:line`) or **derived by reading source** with the arithmetic shown
> inline. Numbers I derived rather than transcribed are tagged **[DERIVED]** and are
> the ones most worth re-checking once a Julia toolchain exists.
>
> Per risk **R3**, every L4 golden that cites this file must carry the commit SHA
> above in a header comment, so a golden diff after a re-vendor is attributable.

---

## 1. The headline: 58 vs 100 for `x+1 @ i8` — RESOLVED

Both figures are upstream. They are **not** two measurements of the same circuit.

| | `100 / 4 / 68 / 28` | `58 / 6 / 40 / 12` |
|---|---|---|
| Where | `BENCHMARKS.md:9` | `README.md:114`, `test/test_5kio_sizehint_arithmetic.jl:46` |
| Add strategy | **Cuccaro** (in-place) | **ripple** |
| `fold_constants` | **false** | **true** |
| Status | **STALE** — pre-U27/U28 defaults | **CURRENT** — matches today's defaults *and* today's explicit contract |
| Machine-checked? | No — a generated artifact | **Yes** — two executable test files |
| T-count | 196 (= 28 Toffoli × 7) | 84 (= 12 Toffoli × 7), `README.md:116` |

### Why `BENCHMARKS.md` is stale, in four independent pieces of evidence

**(a) The generator passes no strategy kwargs, so it captured the defaults *of its
day*.** `benchmark/run_benchmarks.jl:49-51` calls `bench!("x+1", f_add, T; ...)`, and
`bench!` at `:30-41` compiles with `reversible_compile(f, types...)` — no `add=`, no
`fold_constants=`. So the row records whatever the defaults were at generation time.

**(b) Those defaults have since flipped — twice.** `CHANGELOG.md:21`:

> Default add strategy flipped `:auto` → `:ripple` (Bennett-spa8 / U27); default
> `fold_constants=true` (U28). Locked baselines: i8 `x+1` = 58 gates / 12 Toffoli

Confirmed in source: `_pick_add_strategy` now returns `:ripple` for `:auto`
unconditionally (`src/lowering/arith.jl:20-26`, with `:auto` → `return :ripple` at
`:25`), and `fold_constants::Bool=true` is the default at
`src/lowering/driver.jl:105` and `src/Bennett.jl:115`.

**(c) The stale row matches the regression test's own description of the *old*
default, exactly, at all four widths.** `test/test_gate_count_regression.jl:35-36`:

> Post-U27/U28 baselines. Pre-U27 (Cuccaro default): 100/204/412/828 with
> 28/60/124/252 Toffoli

`BENCHMARKS.md:9-12` lists totals 100/204/412/828 and Toffoli 28/60/124/252. Identical.

**(d) The generated file's own internal duplication gives it away.** `BENCHMARKS.md:9`
(`x+1`, defaults) and `BENCHMARKS.md:16` (`x+1 (Cuccaro)`, explicitly
`use_inplace=true` at `run_benchmarks.jl:67`) are byte-identical rows —
`100 | 4 | 68 | 28`. The "default" row *was* the Cuccaro row.

The same staleness shows in two more rows, so this is not an `x+1`-only artifact:
`x²+3x+1 @ i8` is 872 in `BENCHMARKS.md:13` but 482 in
`test_gate_count_regression.jl:75`; `x*y @ i32` is 11202 in `BENCHMARKS.md:15` but
6860 in `test_5kio_sizehint_arithmetic.jl:55`.

**(e) `BENCHMARKS.md` cannot even be regenerated at this commit. [DERIVED]**
`run_benchmarks.jl:31` and `:66` call `Bennett._reset_names!()`. `CHANGELOG.md`
("Removed") records that stub's deletion, and `grep -rn "_reset_names" src/` at this
commit returns **nothing**. A regeneration run would raise `UndefVarError` on the
first `bench!` call. So the file is not merely stale, it is *orphaned* from its
generator. (Static inference — I could not run it.)

### Which number Step 12 pins against

**Step 12 pins the `58 / 6 / 40 / 12` family — and, per the analysis in §4, it is a
legitimate direct comparison for this particular kernel.**

The reasoning is not "58 is the newer number." It is that **Bennett's global wrap and
libcqops's local sandwich are the same algebraic transform, and for a single-kernel
expression like `x+1` they coincide exactly.** From `src/bennett_transform.jl:346-358`
the default wrap is, in order:

```
append!(all_gates, lr.gates)                              # forward:  F gates
_emit_copy_gates!(all_gates, lr.output_wires, copy_wires) # copy-out: n_out CNOTs
for i in length(lr.gates):-1:1; push!(all_gates, lr.gates[i]); end  # reverse: F gates
```

with the size stated outright at `:343-344` as
`2 * length(lr.gates) + length(lr.output_wires) + n_loop`. That is
**`total = 2F + W`** (no loop guards for `x+1`) — precisely libcqops's
"Bennett-in-the-small": compute into scratch, XOR out with W CNOTs, run the compute
backwards.

Because the reverse pass re-pushes `lr.gates[i]` *verbatim* (gates are self-inverse;
only the order reverses), the reverse half has the **same gate-type multiset** as the
forward. Copy-out is all CNOT. So the split is forced:

```
NOT_full     = 2·NOT_f                 →  NOT_f     = NOT_full / 2
Toffoli_full = 2·Toffoli_f             →  Toffoli_f = Toffoli_full / 2
CNOT_full    = 2·CNOT_f + W            →  CNOT_f    = (CNOT_full − W) / 2
```

**Caveat that keeps this honest:** the coincidence holds because `x+1` lowers to *one*
kernel. Bennett wraps the whole function once; libcqops sandwiches *per kernel*. For a
multi-kernel expression these diverge — Bennett pays `2·ΣF_i + W`, libcqops pays
`Σ(2F_i + W_i)`, which is strictly larger. **Do not extend the "58 is directly
comparable" reasoning past single-kernel expressions.** `x²+3x+1 @ i8 = 482` is a
multi-kernel circuit and is *not* a valid libcqops target.

---

## 2. Closed forms — CONFIRMED, with the arithmetic

The task proposed `total(W) = 7W + 2` and `Toffoli(W) = 2W − 4`, and asked for `NOT(W)`
and `CNOT(W)`. All four are **confirmed**, and — better than a fit — `NOT` and `CNOT`
turn out to be **pinned upstream at all four widths**, not just at `W = 8`.

The decisive source is `test/test_5kio_sizehint_arithmetic.jl:46-49`, which asserts the
full 4-tuple at every width (and note it compiles with **default** kwargs at `:41-44`,
which is what makes it independent evidence for §1):

```julia
@test gate_count(c8)  == (total = 58,  NOT = 6, CNOT = 40,  Toffoli = 12)
@test gate_count(c16) == (total = 114, NOT = 6, CNOT = 80,  Toffoli = 28)
@test gate_count(c32) == (total = 226, NOT = 6, CNOT = 160, Toffoli = 60)
@test gate_count(c64) == (total = 450, NOT = 6, CNOT = 320, Toffoli = 124)
```

| W | total | NOT | CNOT | Toffoli | Toffoli-depth | wires |
|---|---|---|---|---|---|---|
| 8 | 58 | 6 | 40 | 12 | 12 | 41 |
| 16 | 114 | 6 | 80 | 28 | 28 | 81 |
| 32 | 226 | 6 | 160 | 60 | 60 | 161 |
| 64 | 450 | 6 | 320 | 124 | 124 | 321 |

Toffoli-depth from `test_gate_count_regression.jl:61-64`; wires from
`benchmark/regression_baselines.jsonl:5-8`.

### The four closed forms

```
total(W)   = 7W + 2      CONFIRMED
NOT(W)     = 6           CONFIRMED (constant)
CNOT(W)    = 5W          CONFIRMED
Toffoli(W) = 2W − 4      CONFIRMED
wires(W)   = 5W + 1      CONFIRMED
```

**Evaluation at each width:**

| W | 7W+2 | vs pinned | 5W | vs pinned | 2W−4 | vs pinned |
|---|---|---|---|---|---|---|
| 8 | 58 | 58 ✓ | 40 | 40 ✓ | 12 | 12 ✓ |
| 16 | 114 | 114 ✓ | 80 | 80 ✓ | 28 | 28 ✓ |
| 32 | 226 | 226 ✓ | 160 | 160 ✓ | 60 | 60 ✓ |
| 64 | 450 | 450 ✓ | 320 | 320 ✓ | 124 | 124 ✓ |

**Sum check — the three parts must total, at every width:**

```
NOT + CNOT + Toffoli = 6 + 5W + (2W − 4) = 7W + 2 = total(W)   ✓ identically in W

W = 8:   6 +  40 +  12 =  58  ✓
W = 16:  6 +  80 +  28 = 114  ✓
W = 32:  6 + 160 +  60 = 226  ✓
W = 64:  6 + 320 + 124 = 450  ✓
```

**Doubling rules from `third_party/bennett/CLAUDE.md:27` / `CHANGELOG.md:21`, checked
against the forms:**

```
total(2W) = 7(2W) + 2 = 14W + 2
2·total(W) − 2 = 2(7W + 2) − 2 = 14W + 4 − 2 = 14W + 2      ✓ identical

T(2W) = 2(2W) − 4 = 4W − 4
2·T(W) + 4 = 2(2W − 4) + 4 = 4W − 8 + 4 = 4W − 4            ✓ identical
```

Both doubling rules are *consequences* of the closed forms, not extra information.
`test_gate_count_regression.jl:53-55` pins the total rule executably.

### Why `NOT(W) = 6` is constant — and it is plausible

Grounded in source, not hand-waving. Constants are materialised by
`resolve!(::ConstOperand)` at `src/lowering/operand.jl:36-41`:

```julia
val = unsigned(op.value) & _wmask(width)
for i in 1:width
    if (val >> (i - 1)) & UInt64(1) == UInt64(1)
        push!(gates, NOTGate(wires[i]))
    end
end
```

**One NOT per set bit of the constant.** The count is the constant's Hamming weight,
which for the literal `1` is exactly 1 **regardless of `W`** — the added high bits are
all zero and emit nothing. The carry chain that *does* scale with `W` contributes only
CNOTs and Toffolis (`src/adder.jl:8-16`). Hence NOT is pinned to the operand, CNOT and
Toffoli to the width. This is the same rule as libcqops Rule 5 — `cq_materialise`
emits `X` only when the constant bit is 1 — so a correct libcqops port should
reproduce the width-independence structurally.

**Honest gap:** this explains *why NOT is constant in W*; it does **not** fully account
for the specific value 6 (compute-half 3). `popcount(1) = 1`, so two further NOTs per
compute half come from somewhere I did not trace — I did not follow the actual LLVM IR
for `x -> x + Int8(1)` through `_fold_constants` into the add lowering, and I will not
guess. It does not block Step 12: the totals are pinned executably at all four widths.
Logged in §6.

---

## 3. Compute-half vs sandwiched — the split libcqops actually needs [DERIVED]

Applying the inversion from §1 to the pinned full-wrap table. **All numbers in this
section are derived by me from the wrap structure, not transcribed from upstream** —
upstream publishes only the full-wrap figures for `x+1`.

**Compute half** (what Bennett's lowering emits once; what libcqops's indexed step
function must reproduce):

| W | F total | NOT | CNOT | Toffoli | check `2F + W` |
|---|---|---|---|---|---|
| 8 | 25 | 3 | 16 | 6 | 2(25) + 8 = 58 ✓ |
| 16 | 49 | 3 | 32 | 14 | 2(49) + 16 = 114 ✓ |
| 32 | 97 | 3 | 64 | 30 | 2(97) + 32 = 226 ✓ |
| 64 | 193 | 3 | 128 | 62 | 2(193) + 64 = 450 ✓ |

Worked once, at `W = 8`: `NOT_f = 6/2 = 3`; `Toffoli_f = 12/2 = 6`;
`CNOT_f = (40 − 8)/2 = 16`; `F = 3 + 16 + 6 = 25`. Every division is exact at every
width — a good consistency signal for the `2F + W` model.

**Compute-half closed forms:**

```
NOT_f(W)     = 3
CNOT_f(W)    = 2W
Toffoli_f(W) = W − 2
F(W)         = 3W + 1        (= 3 + 2W + W − 2 ✓)

sandwiched   = 2(3W + 1) + W = 7W + 2  ✓ recovers total(W)
```

**Both numbers Step 12 should pin, for `x+1 @ i8`:**

| Axis | Value |
|---|---|
| **(a) compute half** | 25 gates — NOT 3, CNOT 16, Toffoli 6 |
| **(b) sandwiched total** | 58 gates — NOT 6, CNOT 40, Toffoli 12 |

Pin **both**. (b) alone cannot distinguish a correct sandwich from a compute half that
is wrong in two compensating ways.

### Note on the L4 golden tuple arity (libcqops Step 0.5 item 3)

The recorded open blocker is that `x+1 @ i8` is given as `58/6/40/12` — "four numbers
against a three-tuple `(NOT, CNOT, Toffoli)`". **This resolves cleanly.**
`gate_count` returns a **4-field** NamedTuple, `src/diagnostics.jl:25`:

```julia
return (total=length(c.gates), NOT=n, CNOT=cn, Toffoli=tf)
```

So `58/6/40/12` is `(total, NOT, CNOT, Toffoli)` — total first. libcqops's three-tuple
`(NOT, CNOT, Toffoli)` is `(6, 40, 12)`; the 58 is the redundant sum. No contradiction,
just a 4-tuple quoted against a 3-tuple. Settle it in `PRD-v1.md` (Step 0.5), not here.

---

## 4. LOUD CAVEAT — `x+1` is a CONSTANT INCREMENT. K6 is a GENERAL TWO-REGISTER ADD.

> ### Do not pin a general-adder golden against the constant-increment baseline.
>
> `58 / 6 / 40 / 12` is `x + 1` with **`fold_constants=true`**. The literal `1` is a
> compile-time constant, and folding it **collapses most of the carry chain** — a
> Toffoli with one control known false degenerates to a CNOT or vanishes. That is
> exactly why the totals are "~2× smaller" (`test_gate_count_regression.jl:35-39`).
>
> **libcqops K6 is `add(dst, a, b)` over two live registers** — PRD §6,
> "| K6 | `add(dst,a,b)`" (PRD-v1.md:625 @ 961905f). Its
> carry chain does **not** collapse. Pinning K6 against 58 would silently under-count.

**How far apart they are.** The general out-of-place ripple adder is
`lower_add!`, `src/adder.jl:1-18`. Reading the loop directly:

```julia
for i in 1:W
    push!(gates, CNOTGate(a[i], result[i]))          # 1 CNOT
    push!(gates, CNOTGate(b[i], result[i]))          # 1 CNOT
    if i < W
        push!(gates, ToffoliGate(a[i], b[i], carry[i + 1]))          # 1 Toffoli
        push!(gates, ToffoliGate(result[i], carry[i], carry[i + 1])) # 1 Toffoli
    end
    push!(gates, CNOTGate(carry[i], result[i]))      # 1 CNOT
end
```

3 CNOTs every iteration; 2 Toffolis only when `i < W`; no NOTs. **[DERIVED]**

```
CNOT_f    = 3W
Toffoli_f = 2(W − 1) = 2W − 2
NOT_f     = 0
F(W)      = 3W + 2W − 2 = 5W − 2
```

`5W − 2` matches the upstream comment at `src/adder.jl:6` ("3 CNOTs per i + 2 Toffolis
for i<W = 5W - 2 gates") — an independent upstream check on my reading.

Sandwiched under libcqops's contract, `2F + W`:

```
K6 sandwiched(W) = 2(5W − 2) + W = 11W − 4
  NOT     = 0
  CNOT    = 2(3W) + W = 7W
  Toffoli = 2(2W − 2) = 4W − 4
  check: 0 + 7W + 4W − 4 = 11W − 4  ✓
```

**Side by side at i8 — the gap this caveat exists to prevent:**

| | compute half | sandwiched | NOT | CNOT | Toffoli |
|---|---|---|---|---|---|
| `x+1` constant increment (folded) | 25 | **58** | 6 | 40 | 12 |
| K6 general two-register add | 38 | **84** | 0 | 56 | 28 |

**84, not 58.** Pinning K6 at 58 would under-count by 26 gates and by **16 Toffolis** —
i.e. by more than half the T-count — at i8 alone.

### What is and is not comparable

**Comparable — pin against `58/6/40/12`:**
- A libcqops increment where the addend is a **classical constant** and the fold table
  collapses it (Rule 3 / PRD §3), at `W ∈ {8,16,32,64}`, single kernel.

**NOT comparable — do not pin against it:**
- **K6 / K7 general two-register `add` / `sub`.** Use `11W − 4` **[DERIVED]** as the
  sandwiched expectation, and treat it as a *derivation to re-verify*, not an upstream
  golden. No upstream figure pins a general two-register ripple add end-to-end.
- **Any multi-kernel expression.** One global wrap ≠ per-kernel sandwiches (§1).
- **`x + 3`** (`total = 64`, `test_gate_count_regression.jl:86`) — a *different*
  constant. `popcount(3) = 2`, so its NOT count differs. Constant-increment baselines
  are per-constant, not a family.
- **Anything at `add=:cuccaro`.** K8 is the in-place accumulator — PRD §6,
  "| K8 | `addacc(acc,b)` in-place" (PRD-v1.md:627 @ 961905f) — and
  is self-cleaning — **not** sandwiched. Its own formulas are in §5.

### Collision trap — two different circuits both total 114

`grep`-ing upstream for a total of `114` returns **two unrelated circuits**:

| Source | Gate counts | What it is |
|---|---|---|
| `test_5kio_sizehint_arithmetic.jl:47` | 114 / **6** / **80** / **28** | `x+1 @ i16` — ours |
| `docs/src/howto/reversible_memory.md:86` | 114 / **10** / **96** / **8** | a 4-entry **QROM s-box on UInt8** — not ours |

Same total, completely different breakdown. **Always match on the full 4-tuple, never
on `total` alone.**

---

## 5. Baseline index for K1–K12

`✔` = kernel is direct-emission (no sandwich); `sandwich` = Bennett-in-the-small
applies. Kernel roles from PRD §6, "## 6. Kernel catalogue" (PRD-v1.md:616 @ 961905f);
the catalogue table itself is PRD §6, "| # | Kernel | Bennett source | Clean? | Notes |"
(PRD-v1.md:618-631 @ 961905f).

| K | Kernel | Upstream baseline | `file:line` | Kind | Notes |
|---|---|---|---|---|---|
| K1 | `xor` | — none | — | ✔ | No pinned count. PRD predicts `2W CNOT`. |
| K2 | `and` | — none | — | ✔ | PRD predicts `W CCX`. |
| K3 | `or` | — none | — | ✔ | PRD predicts `2W CNOT + W CCX`. |
| K4 | shifts | — none | — | ✔ | Index shuffle; `src/lowering/arith.jl:310-329` is pure CNOT. |
| K5 | casts | — none | — | ✔ | `src/lowering/arith.jl:536+`, pure CNOT. Cited `arith.jl:352+` until 2026-09-10 (`bd wf8` / `bd 0a7`) — a **wrong pinned line**, not rot: `:352` is `_shift_stages` / `lower_var_lshr!`, and `function lower_cast!` is at `:536`. |
| **K6** | **`add`** | **none end-to-end**; `5W − 2` compute half **[DERIVED]** | `src/adder.jl:1-18`, comment `:6` | sandwich | §4. Sandwiched `11W − 4`. |
| K7 | `sub` | none; two's complement via K6 | `src/adder.jl:152-154` | sandwich | Comment: `~7W` total incl. `2W` for `~b` + 1 NOT carry-in. |
| **K8** | **`addacc` (Cuccaro)** | **`Toffoli 2W−3, CNOT 4W−2, NOT 0, total 6W−5`** | `test/test_op6a_cuccaro_gate_count.jl:38-41`; docstring `src/adder.jl:33-40` | ✔ self-cleaning | **Best baseline in the repo** — see below. |
| K9 | `eq/ult/slt` | — none | — | sandwich | 7 of 10 predicates derive — PRD §6, "**K9 derives 7 of 10 predicates for free**" (PRD-v1.md:635 @ 961905f). |
| K10 | `mux` | — none | — | sandwich | — |
| K11 | `mul` | `x*y @ i32 = 6860 / 2856 Toffoli` | `test/test_5kio_sizehint_arithmetic.jl:55-56` | sandwich | Defaults. **Full-wrap**; not a kernel-local figure. |
| K11 | `mul` | `x*x @ i8 Toffoli 144, depth 62`; `@ i16 Toffoli 664, depth 208` | `test/test_gate_count_regression.jl:105-108` | sandwich | Explicit `mul=:shift_add, fold_constants=true`. |
| K12 | `divrem` | — none | — | sandwich | "Not a circuit" — branchless Julia kernel; PRD §6, "**K12 is not a circuit.**" (PRD-v1.md:638 @ 961905f). |

### K8 is the one pristine compute-half baseline upstream publishes

`test_op6a_cuccaro_gate_count.jl` calls `lower_add_cuccaro!` **directly** on a bare gate
vector — **no Bennett wrap** — and counts by gate type at `W ∈ {2,3,4,8,16,32,64}`
(`:26`, `:38-41`):

```julia
@test toffs == 2 * W - 3
@test cnots == 4 * W - 2
@test nots  == 0
@test length(gates) == 6 * W - 5
```

At `W = 8`: Toffoli 13, CNOT 30, NOT 0, total 43 (`13 + 30 = 43` ✓). Because K8 is
in-place and self-cleaning it is **not** sandwiched, so this raw figure is directly the
libcqops target. Two upstream carry-out caveats, both in the docstring
(`src/adder.jl:33-42`): this is the **mod-2^W carry-suppressed** variant, and the "2n
NOT" of the original Cuccaro 2004 paper belongs to the **carry-out** form, which this
is not. `W = 1` falls back to `lower_add!` and the formulas do **not** apply (`:45-56`).

### Other integer figures worth knowing (context, not K-targets)

| Circuit | Figure | `file:line` | Status |
|---|---|---|---|
| `x+3 @ i8` | total 64, Toffoli-depth 12 | `test_gate_count_regression.jl:86-87` | current, explicit ripple+fold |
| `x²+3x+1 @ i8` | total 482, Toffoli-depth 36 | `test_gate_count_regression.jl:75-76` | current; **multi-kernel** |
| `x²+3x+1 @ i8` | total 482, Toffoli 168 | `benchmark/regression_baselines.jsonl:9` | current |
| `x*y @ i8` | 690 / 2 / 392 / 296 | `BENCHMARKS.md:14` | **STALE** |
| `x*y @ i32` | 11202 / 2 / 6176 / 5024 | `BENCHMARKS.md:15` | **STALE** (current: 6860 / 2856) |

### Where the baselines live, ranked by authority

1. **`test/test_gate_count_regression.jl`** — executable, explicit strategy kwargs.
   The contract. `third_party/bennett/CLAUDE.md:27` names it as the pin site.
2. **`test/test_5kio_sizehint_arithmetic.jl`** — executable; the **only** source giving
   the full `(total, NOT, CNOT, Toffoli)` 4-tuple at all four widths. Uses *defaults*,
   so it doubles as proof of what today's defaults produce.
3. **`test/test_op6a_cuccaro_gate_count.jl`** — executable; the only **raw
   compute-half** pin (K8).
4. **`benchmark/regression_baselines.jsonl`** — machine-checked by
   `benchmark/regression_check.jl`; adds **wires** and compile-time canaries.
5. **`third_party/bennett/CLAUDE.md:27` / `CHANGELOG.md:21`** — prose statements of the
   same baselines.
6. **`README.md:114-117`, `docs/src/**`** — doc-comment echoes; ~14 files repeat
   `58/6/40/12`. Consistent, but derivative.
7. **`BENCHMARKS.md`** — **STALE, and orphaned from its generator.** Do not pin against
   it. Useful only for the `Published` external-literature column — and note even that
   is a hardcoded format string (`run_benchmarks.jl:50`,
   `"Cuccaro 2004: $(2*W) Toff (in-place)"`), i.e. a citation of Cuccaro et al. 2004
   (arXiv:quant-ph/0410184), **not** a Bennett measurement.

---

## 6. Open questions

1. **The 2 unexplained compute-half NOTs.** `popcount(1) = 1`, but the compute half has
   3 NOTs. I did not trace `x -> x + Int8(1)` through LLVM IR → `_fold_constants` → the
   add lowering. Does not block Step 12 (totals are pinned at all four widths), but the
   K6 port should explain it. **Do not guess it into a golden.**
2. **No upstream end-to-end pin for a general two-register add.** Every `x+1` figure is
   constant-folded. `11W − 4` (§4) is **my derivation**, not upstream. This is the
   single most load-bearing unverified number in this document.
3. **K1–K5, K9, K10, K12 have no published gate counts at all.** Their L4 goldens will
   be self-pinned from our own port. `x+1 @ i8` really is the **one** place the port is
   validated against upstream rather than against itself — worth stating in the PRD.
4. **`BENCHMARKS.md` cannot be regenerated** at this commit (`_reset_names!` removed,
   §1(e)). If a future re-vendor picks up a fixed generator, the `x+1` row should
   become 58 — treat that as **confirmation**, not a regression.
5. Everything here is **UNVERIFIED-BY-EXECUTION**. Re-verify §3 and §4 first when Julia
   is available: they are the derived ones.
