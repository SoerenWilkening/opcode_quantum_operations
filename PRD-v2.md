# PRD — `libcqops` v2: the floating-point opcode surface

Status: **scoping draft, nothing implemented** · Target: the fp half of the frozen `cq_template_*` grid
Companions: [`NORTH_STAR.md`](NORTH_STAR.md) (why) · [`PRD-v1.md`](PRD-v1.md) (v1's what, and §15 where
every decision lives) · [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md) (v1's how)

> **THIS DOCUMENT DECIDES SCOPE AND RECORDS SEAMS. IT IMPLEMENTS NOTHING.** Its one-row
> pointer in the decision register is **PRD-v1 §15 D26**, which is where the decision of
> record lives; this file is the long form D26 points at, in the same relation `K12.md` has
> to D9.
>
> **EVERY FIGURE BELOW WAS MEASURED ON 2026-09-14 AND MUST BE RE-MEASURED, NOT QUOTED
> (Rule 16).** This repo pins neither CQ_lang nor the QEC library. The corpus figures are
> against CQ_lang `607b6fe4` (367 goldens, clean tree); the construction figures are against
> the pinned Bennett snapshot `980805de`, which *is* pinned and does not move. The two are
> marked differently throughout for exactly that reason.

---

## 0. What was verified before any of this was written

Rule 16 applies to a scoping document more than to any other kind, because a scope taken on a
remembered fact is a scope that has to be retaken. Four checks ran first.

| Claim | Method | Result |
|---|---|---|
| The pinned ABI mirror is current | `shasum -a 256` of `third_party/cq_lang/opcode_table.yaml` against `git show 607b6fe4:tools/opcode_table.yaml` | **byte-identical**, `6245117d…a3e82426`. No re-vendor owed |
| The core ABI has not drifted | `git log 170ede1..607b6fe4 -- runtime/cq_runtime.h` | **0 commits**, of 33 in the range |
| Rule 1 is satisfiable | `ls third_party/bennett/src/softfloat/` | **35 files** at the pinned commit |
| Rule 1 is satisfiable **for what the corpus actually calls** | every fp `cq_template_*` in the 367 goldens, base opcode mapped onto `softfloat.jl`'s export list | **zero** fp opcodes the corpus calls are absent from Bennett |

That last row is the finding this whole document turns on, and it is the opposite of what the
file list suggests. See §2.

---

## 1. What v2 is

**v2 is the floating-point opcode surface at `f64`, ported from `third_party/bennett/src/softfloat/`
under Rule 1, over the representation v1 already ships unchanged.**

Three clauses, and each is a decision rather than a description.

1. **`f64` ONLY.** `f16`, `f32` and `f80` keep v1's loud abort. §3 is why: the gap is a *width*
   gap, not a construction gap, and only `f64` is portable. This is the clause most likely to be
   re-litigated and §3.2 is written to be re-read before it is.
2. **A PORT, NOT A RE-DERIVATION.** Every construction comes out of the pinned snapshot. Rule 1's
   *"how should we build a reversible comparator?"* is never an open question here either.
3. **NO REPRESENTATION CHANGE.** An fp rail is v1's register at an fp width. §3.1 establishes that
   this is forced rather than chosen, and the plan already recorded the seam before v1 shipped.

**What that buys, measured:** **100** of the 367 goldens become link-run-clean that are not today —
of **191** that touch fp at all.

## 1a. What v2 is not

- **Not the 884 fp symbols.** §2 shows the 884 is the wrong unit in *both* directions and should
  not be used to size, schedule or report v2.
- **Not `f80`.** The corpus wants it badly (5,891 calls, 53 fixtures) and the pinned snapshot has
  **zero** bytes of it. Porting is impossible and re-deriving is what Rule 1 exists to forbid. §6
  records it as the v3 boundary with the one condition that would move it.
- **Not `f32`.** Bennett *appears* to cover it and does not. §3.2.
- **Not the transcendentals.** `soft_sin`, `soft_exp`, `soft_log`, `soft_pow` and their 15 siblings
  are in the snapshot and are **not** in the frozen grid; they belong to CQ_lang's two
  **non-vendored** tables. §6 files the vendoring question rather than answering it.
- **Not a change to D14.** §5.
- **Not reachable through the QEC sink.** §4 condition 5, and it is arithmetic, not pessimism.
- **Not an optimisation pass.** The five v2-labelled beads (`2kl`, `5zn`, `b8g`, `e05`, `mri`) are
  inputs to this document, not tasks in it. Every one was measured and refused for v1 on Rule 1 or
  on risk R5, and none of those grounds is weakened by fp arriving.

---

## 2. The 884 is the wrong unit, and it is wrong in both directions

The v1 documents partition the grid `992 wrappers + 603 integer aborts + 884 fp aborts = 2479`
(D14, D16, Step 22's name rule). That partition is exact and is **not** in question. What is in
question is reading "v2 = the 884" off it, which three independent measurements refuse.

**(a) OVER-COVERAGE: only 196 of the 884 are ever called.** Across all 367 goldens, 196 distinct fp
grid symbols appear; **688 appear zero times**. Sizing v2 at 884 over-states it by 4.5×, and — worse
— a v2 that "completed the 884" would have spent most of its effort on symbols with no caller and no
test that could witness them.

**(b) UNDER-COVERAGE: 75 fp symbols the corpus calls 4,913 times are not in the grid at all.**

| Out-of-grid family | Occurrences | In Bennett? |
|---|---:|---|
| `fma` | **4,295** | `fma.jl` — `soft_fma` |
| `fabs` | 192 | bit op (clear the sign bit) |
| `copysign` | 143 | bit op (copy the sign bit) |
| `sqrt` | 79 | `fsqrt.jl` — `soft_fsqrt` |
| `floor` | 69 | `fround.jl` — `soft_floor` |
| `fmin` / `fmax` | 42 / 42 | `fmin.jl` — `soft_fmin` / `soft_fmax` |
| `rint` | 31 | `fround.jl` |
| `round` | 12 | `fround.jl` — `soft_round` |
| `trunc` | 7 | `fround.jl` — `soft_trunc` |
| `nearbyint` | 1 | `fround.jl` |

`cq_template_fma_f80_qql` is the **single most-called fp symbol in the entire corpus** (635
occurrences) and `opcode_table.yaml` does not contain the string `fma`. It cannot: the table's
`binary_opcodes` are binary and `fma` is ternary. These come from CQ_lang's `tools/intrinsic_table.yaml`
(389 symbols) and `tools/libm_table.yaml` (12 symbols), which `third_party/cq_lang/COMMIT` already
flags as **deliberately not vendored** and as *"an open scope question, not a settled fact."*

> **THIS IS THE SAME SHAPE AS `bd 3ep`, RUN THE OTHER WAY.** There, a family looked deferrable and
> was measured to be among the most heavily emitted in the grid; the *reachability* test settled it.
> Here a family is not in the grid at all and is the most heavily emitted thing the corpus calls.
> Both findings come from measuring the corpus rather than reading the table, and neither was
> visible from the partition.

**(c) THE 884 SAYS NOTHING ABOUT WIDTH, WHICH IS THE ONLY THING THAT ACTUALLY BINDS.** The grid is
perfectly uniform — 230 symbol-occurrences at each of `f16`, `f32`, `f64`, `f80` — and the corpus is
not, and the pinned snapshot is not. §3.2.

**The unit v2 should be sized, scheduled and reported in is the FIXTURE**, which is what
NORTH_STAR condition 1 is written in and what D23 and D18 already use. §4.

---

## 3. The five questions

### 3.1 What is an fp rail? — *purely a kernel-level interpretation. No representation change.*

**Forced, not chosen, and the seam was recorded before v1 shipped.** Plan §3's Layer-5 seam table
already says of the 34 deferred fp-width core symbols that they are

> *"the SHIPPED families at a width v1 defers — `alloc` / `measure` / `ry` / `rz` /
> `rz_controlled[_inv]` / `ry_controlled_inv` / `copy[_controlled]`, one width token away from
> `cq_runtime_rail.c` and `cq_runtime_gate.c` — and in v2 they become real code by WIDENING what
> already exists."*

Four things make that the right answer rather than merely the recorded one.

1. **Bennett's softfloat never sees a float.** Every entry point takes and returns a raw bit
   pattern — `soft_fadd(a::UInt64, b::UInt64)::UInt64`. The construction being ported is *already*
   integer logic over 64 bits. There is nothing to represent.
2. **I5 is untouched and the sub-field access is already a sanctioned shape.** Sign, exponent and
   mantissa are contiguous spans of the same `cq_bit` array. K12's shifted remainder `r_in[t]` is a
   read-only view over a neighbouring region and is I6-sound because every use after the shift-in is
   a control (`K12.md` §2.1a); an exponent field is the same shape and a strictly easier one, being
   a fixed span rather than a moving one. The recorded trap applies verbatim: **guards compare
   ranges, not base pointers**, and a reflex "assert the operands are disjoint objects" would reject
   the correct layout.
3. **The tri-valued bit is exactly as meaningful.** A classical `f64` rail is 64 `CQ_BIT_ZERO`/`ONE`
   bits holding the IEEE pattern, so **I4 holds and L5 survives**: a fully-classical `fadd` is zero
   gates and zero qubits, and the §3 fold table delivers the value with no circuit at all. That is
   not a bonus — given `fadd`'s size (§3.3) it is the difference between an all-classical fp program
   costing nothing and costing 63,058 gates.
4. **The one genuinely new mechanism is small and is at the ABI edge, not in the representation.**
   `cqrt_alloc_f64(double)` must turn a C `double` into 64 constant bits and `cqrt_measure_f64` must
   invert it. That is a `memcpy` into a `uint64_t` and the existing `cq_reg_alloc_const` path.

> **THE ONE PLACE THIS IS NOT A MEMCPY IS `f80`, AND IT IS ONE MORE REASON `f80` IS OUT.** `long
> double` is an 80-bit value in a 12- or 16-byte object with padding whose contents are not
> specified, so `sizeof` is the wrong length and the padding is not part of the number. `i80`
> already carries the matching fact on the integer side — `opcode_table.yaml` gives it `c_type:
> __int128`. Deferring `f80` defers this too; **taking `f80` later means solving it, and the
> hazard is that the obvious spelling compiles, runs, and hashes padding into the rail.**

**Consequence for the module map:** there is **no `M__ fpreg` module**. §7's table has no
representation row because there is nothing to build.

### 3.2 Does the two-bit shadow survive? — *yes, and it is proven by arithmetic on upstream's own published table*

**The answer is yes and the evidence is better than an argument.** `third_party/bennett/BENCHMARKS.md`
publishes per-construction gate counts in columns `Total | NOT | CNOT | Toffoli | Wires | Ancillae |
T-count`. For all three softfloat rows the total equals the sum of exactly three columns:

| | Total | NOT | CNOT | Toffoli | NOT+CNOT+Toffoli |
|---|---:|---:|---:|---:|---:|
| `soft_fadd` f64 | 63,058 | 10,304 | 40,266 | 12,488 | **63,058** |
| `soft_fmul` f64×f64 | 149,456 | 11,850 | 98,722 | 38,884 | **149,456** |
| `soft_fma` f64×f64×f64 | 247,398 | 26,660 | 165,848 | 54,890 | **247,398** |

Three exact matches. **Bennett's softfloat lowering emits `{X, CX, CCX}` and nothing else** — no
`H`, no `T`, no rotation, no gate above two controls. So:

- **Rule 4 is intact** and fp needs no new gate.
- **D12's rotation-free surface extends over the whole fp kernel surface**, so the shadow stays
  **EXACT rather than conservative** there, and `cq_pc_zero_proof_rotation_free` keeps its teeth.
- **The Prime Directive's instrument set carries over unchanged** — L1 values, L2/L3 pool, L4
  counts, and `cq_mock_is_palindrome` where it is needed.

> **TWO RIDERS, AND THE SECOND IS THE ONE THAT COULD BITE.** (i) The table is a claim about
> *Bennett's* lowering of the Julia source, not about our port; what it establishes is that the
> construction **needs** no gate outside the alphabet, which is the claim Rule 4 actually requires.
> (ii) `BENCHMARKS.md` is upstream's own file and this repo has a **recorded reason not to trust its
> figures for comparison** — CLAUDE.md's callout that the K-doc §5 deltas were `fold_constants=false`
> figures while Bennett folds by default, and Step 12's note that `x+1 i8` is pinned at `58/6/40/12`
> and not at this file's `100/4/68/28`. **The alphabet conclusion is immune to both**: it is a
> statement that three columns sum to a fourth, which no fold setting changes. **The magnitudes in
> §3.3 are NOT immune and are labelled accordingly.**

### 3.3 How many kernels, and what is the ancilla cost? — *fewer kernels than v1, each one to two orders of magnitude larger*

**Kernel count is small, because the corpus's 25 base opcodes collapse onto Bennett's public
surface with nothing left over** (§0's fourth row). Grouped by what a port actually has to build:

| Group | Corpus calls | Bennett source | Note |
|---|---:|---|---|
| `fadd` / `fsub` | 3,017 | `fadd.jl`, `fsub.jl` | `fsub` is `fadd` + sign flip, with a NaN-RHS carve-out upstream fixed (`Bennett-m63k`) — **do not re-derive it as `fadd(a, fneg(b))`** |
| `fmul` | 2,511 | `fmul.jl` | |
| `fma` | 4,295 | `fma.jl` | **out of grid** (§2b) |
| `fcmp` ×10 predicates | 6,789 | `fcmp.jl` | Bennett exports 10; the grid wants 14. The 4 missing (`ogt`, `oge`, `ugt`, `uge`) are **operand swaps**, exactly K9's shape |
| `fdiv` | 619 | `fdiv.jl` | |
| `fneg` / `fabs` / `copysign` | 1,124 | `fneg.jl` + bit ops | Sign-bit only: **zero Toffoli**, pure X/CX, and `fneg` on a classical rail is an L5 zero-cost row |
| `bitcast` | 1,511 | — | **Identity relabel, zero gates** (D16's third worked example, `bd 3ep`) |
| conversions | 636 | `fpconv.jl`, `fptosi.jl`, `fptoui.jl`, `sitofp.jl` | `uitofp` is `zext`-to-64 then `soft_sitofp` — upstream's own routing, not our invention |
| rounding | 120 | `fround.jl` | `floor`/`ceil`/`trunc`/`round`/`rint`/`nearbyint`, **out of grid** |
| `fmin` / `fmax` | 84 | `fmin.jl` | **out of grid** |
| `fsqrt` | 79 | `fsqrt.jl` | **out of grid** |

**`frem` is in the grid (60 symbols) and the corpus calls it ZERO times** — and Bennett does not have
it, listing it as future work (`Bennett-VISION-PRD.md` Tier 2, *"Soft remainder via soft_fdiv"*).
Those two facts cancel: **`frem` keeps the abort in v2 and costs nothing**, and this is the one place
where an in-grid symbol is deliberately left behind on liveness. That is D23's test, applied a third
time.

**Cost, from upstream's published table — and these are the figures §3.2's rider says to treat as
order-of-magnitude:**

| | Gates | Toffoli | **Ancillae** |
|---|---:|---:|---:|
| `soft_fadd` f64 | 63,058 | 12,488 | **29,949** |
| `soft_fmul` f64 | 149,456 | 38,884 | **87,868** |
| `soft_fma` f64 | 247,398 | 54,890 | **135,743** |
| *for scale* — our `udiv` at i64 | 139,456 | 40,704 | — |
| *for scale* — our `udiv` at i128 | 557,696 | 163,328 | **131,583** |

**The order of magnitude is the reassuring part and the ancilla count is the sting.** One `f64`
`fadd` is **less than half an i64 `udiv`** in gates, and one `fma` is **under half an i128 `udiv`**.
fp is not a new regime; it is the regime v1 already ships at its top width. But `fadd`'s **29,949**
ancillae against `udiv` i8's **543** is 55×, and D2/D20's ceiling is a **hard** one.

> **RULE 2 DOUBLES THE GATES AND DOES NOT MOVE THE PEAK.** Upstream's figures are its *global*
> forward–copy–reverse wrap. Bennett-in-the-small applies the same construction locally, so the
> libcqops cost is ~2× the compute half at the **same** ancilla peak. `soft_fadd` ⇒ ~126k gates over
> ~30k qubits. **That is the price and Rule 2 says it is not negotiable down** — and the two
> standing offers to negotiate it (`bd 5zn`'s linear K12 schedule, K11.md §5's `pp` recycling) are
> re-derivations that D25 already refused on Rule 1.

### 3.4 What is the finish line?

**NORTH_STAR's five conditions are not rewritten. Three carry over verbatim, one narrows to a
measurable population, and one becomes explicitly unreachable — which is a scope statement, not a
failure.**

| # | v1 | v2 |
|---|---|---|
| **1. Link** | the fixtures link, run and do not abort (D18; "traces match" retired) | **Unchanged in form, stated over a named population: the 100 f64-clean fp fixtures.** §4 |
| **2. Correct** | every integer opcode differential-tested against C semantics | **Unchanged, and it is the condition that does the work.** §4 |
| **3. Clean** | the pool holds exactly what the live rails own, as a set | **Unchanged.** Rule 2 and the sandwich apply verbatim; §3.2 keeps D12's exactness, so the free-time proof is no weaker than v1's |
| **4. Grover** | Grover compiles, links, oracle verified in classical mode | **Already met and untouched** — §12's oracle is integer. v2 adds no claim here |
| **5. Hardware** | `CQOPS_SINK=qec` routes the stream; QEC reports physical cost | **UNREACHABLE for fp, by arithmetic.** §4 |

**Condition 1's population, measured.** 191 of 367 goldens touch fp. Under an `f64`-only scope,
**100** of them use no other fp width and no opcode outside Bennett's surface. That is the number v2
is worth, and it is the number to re-measure before starting.

| Scope | fp fixtures unblocked | fp calls covered |
|---|---:|---:|
| `f64` only | **100** | 8,793 of 20,795 |
| `f64` + `f32` | 138 | 14,664 |
| `f64` + `f32` + `f16` | 139 | 14,669 |
| all four | 191 | 20,795 |
| *blocked by an opcode Bennett lacks* | **0** | — |

**Condition 2 is what carries correctness, and for fp it has a sharper edge than it did for
integers.** L1's oracle is the C operator, and the port is only differentially testable if the
construction is bit-exact against it. Upstream's `CHANGELOG.md` claims `soft_*` is *"bit-exact
against Julia native at every measured input"* for **binary64**, backed by `test_softfloat.jl` at
~1.2M random pairs. **That claim does not extend to `f32`, upstream says so itself, and that is the
second and decisive reason `f32` is out** — see the box below. Two riders v1 did not have:

- **`cq_kd_samples()`'s 32 samples rest on an argument that has to be re-made.** The budget is
  justified by the §3 fold table dispatching on *kind* and never on a qubit's *value* (D6), so at the
  all-quantum mask the circuit is identical across value pairs and values reach it only through
  classical lanes. **That argument survives** — softfloat is still a classical permutation and D6
  still forbids demotion. **What does not survive is the coverage intuition:** an fp value space has
  structure integers do not (NaN, ±Inf, ±0, subnormals, the rounding boundary), all of it reached
  through *classical* lanes where values do matter. **The anchors must be widened, not the budget**:
  the corners for `f64` are the IEEE special classes, not `0` and `~0`.
- **Bit-exactness is against the *C* operator on this box's `double`.** `-ffp-contract=off` is
  already in `cqops_build_flags` for M21's sake and its reason — a host-dependent classification —
  applies with more force here, where the reference itself is floating-point.

> **`f32` LOOKS COVERED AND IS NOT, AND THE DOCUMENT SAYING SO IS UPSTREAM'S OWN.**
> `third_party/bennett/src/softfloat/fpconv.jl:1-40` states it outright: *"There are no native f32
> arithmetic primitives — `soft_f32_fadd`, etc. do not exist."* An f32 `fadd` is lowered as
> `soft_fpext → soft_fadd(f64) → soft_fptrunc`, which **double-rounds**, and upstream's own
> conclusion is that *"the bit-exact-vs-Julia-native contract … does **NOT** extend to f32
> arithmetic"* and that **`reversible_compile(f, Float32)` is rejected at the validation step.**
> Taking `f32` on the round trip would import a construction upstream declines to compile, into a
> project whose condition 2 is a *bit-exact* differential test. **L1 would be red, correctly.** The
> file names *"native 24-bit-mantissa f32 primitives"* as upstream future work — which is the
> condition that moves this, and §6 files it.

**Condition 5 is unreachable and the arithmetic is short.** D20 measured the shipped QEC configs
carrying `n_logical ∈ {1, 3, 4, 6, 7}`, and raising it raises the code distance. One `f64` `fadd`
needs **29,949** ancillae — four orders of magnitude past it — and at `d = 3` one logical `CCX` is
562,564 physical gates, so `fadd`'s 12,488 Toffolis are ~7.0 × 10⁹ physical gates. **This is fault
tolerance, not a defect in either library**, and it is the same honest scope D20 already recorded for
v1 one width down. **Do not read it as v2 failing condition 5**: condition 5 was met at Step 26 and
is a claim about the *route*, which is unchanged. What is bounded is which programs are inspectable
through it, and fp is not one of them.

### 3.5 What stays deferred? — *D14 is unchanged, and the bitcasts cost nothing*

**D14's 603 integer `_inv` bodies keep their loud abort. v2 changes nothing about them**, and the
ground is D14's own — **specifiability**, not liveness. Measured today against the pinned yaml:

| | Symbols | |
|---|---:|---|
| D14's five **named** opcodes — `and`, `or`, `udiv`, `trunc`, `icmp` | **232** | provably non-injective; **permanent in any version** |
| non-injective by D14's **same argument** but unnamed — `urem`, `sdiv`, `srem`, `shl`, `lshr`, `ashr` | **228** | `shl`/`lshr`/`ashr` discard bits under D8's mask-then-saturate; `sdiv`/`srem`/`urem` by the `udiv` argument |
| genuinely invertible — `add`, `sub`, `mul`, `xor`, `zext`, `sext` | **143** | policy, not impossibility — and `mul` only for odd multipliers |
| | **603** | and the corpus emits **zero** `_inv` lines |

So the honest restatement is **460 permanent and 143 unreachable-by-anything**, and the second
number is why the policy question has never had to be answered. **The 232 figure is D14's named set
and reproduces exactly**; the 228 is a widening this document offers and D14 does not assert — it is
filed as a bead rather than written into D14, because D14 is a decision of record and this is a
re-reading of it.

**The 24 bitcasts become reachable exactly when fp does, and cost ~zero** — an identity relabel, per
D16's third worked example (`bd 3ep`, 2026-09-14). Measured: **1,511** bitcast lines across 127
fixtures, of which 12 of the 24 symbols are live (`f32↔i32`, `f64↔i64`, `f80↔i80`, each forward and
`_unc`) and **all eight `_inv` and both `f16` symbols are dead**. Under an `f64`-only scope only the
`f64↔i64` pair is in scope; the `f80` pair — at **817** occurrences the most-used bitcast — waits on
`f80`.

**The fp abort bucket shrinks and does not vanish.** Even at full four-width fp, **688 of the 884**
have no caller in the corpus. At `f64`-only scope the bucket stays large, and that is correct: v1's
own design makes a deferred symbol a *runtime* abort naming itself so the boundary is visible where
it is reached, rather than a link failure.

---

## 4. Risks

Numbered to continue plan §6's register rather than restart it.

| # | Risk | Mitigation |
|---|---|---|
| **R10** | **The f32 round trip gets taken to widen the fixture count from 100 to 138.** It is one flag away, it looks like a port, and it is a 38-fixture prize | §3.4's box. Upstream rejects the compile; L1 would be red and **correctly** red. If it is ever taken it must be as a **declared approximation with its own test tier**, never inside condition 2 |
| **R11** | **An fp kernel's scratch region trips D2's ceiling and the refusal is read as a libcqops bug.** `fadd` needs ~30k qubits where K12 i8 needs 543 | D25 already resolved the behaviour (abort from the pool during pre-materialisation, unswallowed at the opcode surface) and **D25's prohibition is the live one here: the shim must never grow its own "that will not fit" check** |
| **R12** | **The L1 sample budget is carried over without widening the anchors**, so NaN/±Inf/±0/subnormal/rounding-boundary rows are drawn at random and no run guarantees any of them | §3.4. The 32-sample budget is fine; the **anchors** are what must change. This is the same shape as the note already standing on `cq_kd_samples()` — the named mask rows became pool rows and no single run guarantees one |
| **R13** | **`fma` is scoped out because it is not in the grid, and it is 4,295 calls** — the largest fp family in the corpus, ported from a file that is already on disk | §6's first open question. Sizing v2 without deciding this understates it by the single biggest item |
| **R14** | **`BENCHMARKS.md`'s magnitudes are reused as if they were L4 goldens.** This repo has a recorded instance of exactly that (`x+1 i8` at `100/4/68/28`) | §3.2's rider (ii). The alphabet conclusion is fold-invariant; **the magnitudes are not and must be re-measured against our own port**, at the all-quantum mask, before any golden is pinned |
| **R15** | **`f80`'s `long double` bit extraction is written as a `sizeof` `memcpy`**, silently hashing unspecified padding into a rail | §3.1's box. Not live while `f80` is out of scope, which is part of why it is out |

---

## 5. Module map — seams recorded in advance

Plan §3's idiom: **every module over ~200 lines gets its split seam recorded before a line is
written** (Rule 12), so hitting the limit is a scheduled split and never a surprise refactor.
Numbering continues v1's M01–M30.

| Module | What | Est. LOC | **Seam, recorded in advance** |
|---|---|---:|---|
| **M31 `fpfield`** | The IEEE field views over a `cq_bit` array — sign / exponent / mantissa as `cq_scratch_span`-shaped read-only views, the implicit-bit rules, and the class predicates (`is_nan`, `is_inf`, `is_zero`, `is_subnormal`) as one-bit kernels | 180 | **VIEWS ↔ CLASS PREDICATES.** The views emit nothing and are pure addressing; the predicates are Rule 7 kernels with `dst` one bit, which is **K9's shape** and needs K9's test adapter. Split at the first predicate |
| **M32 `fpround`** | The shared round-and-pack path — `soft_fround`'s `_sf_round_and_pack`, sticky/guard/round bits, the carry-out that bumps subnormal→normal | 260 | **PACK ↔ ROUND.** Upstream keeps them in one helper; at our LOC limit the rounding-decision logic splits from the field assembly. Seam is at the point the decision bit is known |
| **M33 `fadd`** | K15 — `soft_fadd` / `soft_fsub` | 280 | **ALIGN ↔ ADD-AND-NORMALISE.** The exponent-difference shift is the half that is `O(W)` in its own right and is the half `fma` reuses. **Do not spell `fsub` as `fadd(a, fneg(b))`** — upstream fixed a NaN-RHS sign bug in exactly that composition (`Bennett-m63k`) |
| **M34 `fmul`** | K16 — `soft_fmul` | 240 | **SIGNIFICAND PRODUCT ↔ EXPONENT/PACK.** The 53×53 product is M18's `mul` over a span; the seam is where the integer kernel ends |
| **M35 `fdiv`** | K17 — `soft_fdiv` | 240 | **The 56-bit restoring-division loop ↔ pre-normalisation.** The loop is K12's shape and should **compose M19's exported step block, not transcribe it** (plan §0.4's obligation, and the K11/K12 composition-check trap applies verbatim) |
| **M36 `fcmp`** | K18 — the 10 ported predicates + the 4 operand-swaps | 200 | **ORDERED CORE ↔ PREDICATE TABLE.** K9's exact shape: *four predicates swap operands and five invert the flag, and the two sets are not the same set*. Expect the fp table to differ; **derive it, do not carry K9's over** |
| **M37 `fconv`** | K19 — `fptosi` / `fptoui` / `sitofp` / `uitofp` / `fpext` / `fptrunc` | 220 | **INT→FP ↔ FP→INT.** They share nothing but the field views, and only the second has a saturation/undefined story (D3's posture) |
| **M38 `fmisc`** | `fneg` / `fabs` / `copysign` / `fmin` / `fmax` / rounding | 200 | **SIGN OPS ↔ EVERYTHING ELSE.** The sign ops are zero-Toffoli and have a true L5 row; the rest go through M32 |
| **M39 `fma`** | K20 — `soft_fma`, **if §6.1 takes it** | 260 | **Reuses M33's align and M34's product.** Seam is the single-rounding path that is the whole point of an `fma` and is what forbids spelling it `fmul` then `fadd` |
| *(no module)* | **the fp rail representation** | 0 | §3.1 — there is nothing to build |
| `cq_runtime_rail.c` / `cq_runtime_gate.c` | the 34 deferred fp-width core symbols | — | **Already recorded in plan §3's Layer-5 seam table**, and the recorded disposition is *"they become real code by WIDENING what already exists"*. No new seam |

**Two shared obligations, both already recorded traps rather than new ones.**

- **Every composite fp kernel calls the other kernel's *step function*, never the kernel**
  (`cq_sandwich` refuses nesting in both configurations). M33/M34/M35 must export compute halves the
  way M14/M16/M17 do, and M39 consumes them.
- **Every kernel whose cost has a closed form needs the composition check written BEFORE the
  kernel.** K11's recorded finding is that the one mutant L1 cannot see is the one that looks like an
  optimisation, and that L4 is not a durable detector because `CQOPS_UPDATE_GOLDENS=1` blesses the
  reduction. fp offers this trade everywhere — every field is provably narrower than its span.

---

## 6. Open questions — decide in a document, never in code

**None of these is answered here.** Each is a bead.

1. **Does v2 vendor `intrinsic_table.yaml` and `libm_table.yaml`?** This is the largest open item and
   it dominates the sizing: **`fma` alone is 4,295 corpus calls**, more than `fmul`, `fadd` and
   `fdiv` combined, and the complete `cq_template_*` link surface is **2,880 symbols, not 2,479**.
   `third_party/cq_lang/COMMIT` has flagged it since 2026-08-14 and nothing has decided it. Note the
   two sub-questions come apart: **vendoring the tables** is a shim/link question (Step 22's name
   rule and M27's generator), while **porting `fma`** is a kernel question that Bennett already
   answers. The link surface question binds first.
2. **Does `f80` ever arrive, and on what condition?** Not by porting — the snapshot has zero bytes of
   it — and not by re-deriving, which Rule 1 forbids. The only clean route is **upstream growing
   80-bit support and this repo re-vendoring at a new pin** (risk R3, invalidating every L4 golden).
   Until then `f80`'s 5,891 calls and 53 fixtures are the v3 boundary. **It should be stated as a
   dependency, not carried as a gap.**
3. **Does `f32` arrive on upstream's native primitives?** `fpconv.jl` names *"native 24-bit-mantissa
   f32 primitives"* as upstream future work. That — and only that — flips §3.4's box.
4. **Is D14's non-injective set 232 or 460?** §3.5. A re-reading of a decision of record, which
   should be settled in D14 or in a new row, not assumed by an implementer.
5. **What are L1's fp anchors?** R12. Needs to be written down before M31, because it changes
   `tests/support/`'s sampler and not just a test.
6. **Does the fp surface get its own `cq_runtime_v2.c`-style residual bucket**, or do the 688
   never-called fp symbols stay in the existing one? A bucketing question with the same shape as D16,
   and the answer probably follows D16's *family first, width second* rule.
