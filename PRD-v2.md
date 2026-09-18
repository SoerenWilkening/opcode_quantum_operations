# PRD — `libcqops` v2: the floating-point opcode surface

Status: **scoping draft, nothing implemented — the port's MECHANICS decided 2026-09-18, §7** · Target: the fp half of the frozen `cq_template_*` grid
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
>
> **RE-MEASURED 2026-09-17 against CQ_lang `b1b1dc02` WITH A DIRTY WORKING TREE — 392 goldens,
> and the fp figures did not move.** The corpus gained 27 fixtures and lost 2 in that range, and
> **not one of the 29 contains a single fp `cq_template_*` call**, so every fp count in this
> document reproduces *exactly* at both revisions; the `607b6fe4` totals were recomputed straight
> out of `git show` to confirm it rather than inferred from the delta. The dirty tree is
> docs-only — `tests/e2e` and `tools/` are clean at `b1b1dc02` — so the measurements are that
> commit's. **What DID move is the golden count itself (367 → 392), and three figures below were
> wrong when written**: §3.3's `fcmp` occurrences, §3.3's `udiv`-at-i64 row, and §1a's `f80`
> pair. Each is corrected in place with **both** values recorded and dated, which is this repo's
> rule everywhere else — an overwritten figure loses the evidence that it was ever checked.

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

### 0a. Re-measured 2026-09-17 — what held, what moved, and the one thing that was never measured

Every row above and every headline count in §2 and §3 was re-run against CQ_lang `b1b1dc02`
(dirty working tree, 392 goldens) and against the same pinned Bennett snapshot.

| Claim | Re-measured | Verdict |
|---|---|---|
| The pinned ABI mirror is current | `shasum -a 256` of our copy against `git show b1b1dc02:tools/opcode_table.yaml` | **CONFIRMED and extended** — still byte-identical, `6245117d…a3e82426`, now through `b1b1dc02`. No re-vendor owed |
| The core ABI has not drifted | `git log 170ede1..b1b1dc02 -- runtime/cq_runtime.h` | **CONFIRMED** — still **0** commits, now of **61** in the range (was 0 of 33) |
| `softfloat/` has 35 files | `ls` the pinned snapshot | **CONFIRMED** — 35, and **no file carries an `f16`/`f32`/`f80` name token** |
| Zero fp opcodes the corpus calls are absent from Bennett | corpus families mapped onto `softfloat.jl`'s `export` list | **CONFIRMED** — and now checked the other way too: the export list carries **exactly 10** `fcmp` predicates and the four the grid adds are `ogt`/`oge`/`ugt`/`uge`, exactly as §3.3 says, and it carries **no `frem`** |
| The grid partition `992 + 603 + 884 = 2479` | Step 22's name rule over our own `shim/generated/*.gen.c` | **CONFIRMED exactly**, all four numbers |
| `196` called / `688` never called | the 392 goldens | **CONFIRMED exactly** |
| `75` out-of-grid fp symbols, `4,913` occurrences | the 392 goldens | **CONFIRMED exactly**, and the per-family split is unchanged to the last symbol (`fma` 4,295 … `nearbyint` 1) |
| `191` fp fixtures, `100` at `f64`-only, `20,795` fp calls | the 392 goldens | **CONFIRMED exactly** |
| D14's `232` / `228` / `143` | the pinned yaml | **CONFIRMED exactly**, and the corpus still emits **zero** `_inv` lines |
| `Total = NOT+CNOT+Toffoli` on the softfloat rows | `BENCHMARKS.md` arithmetic | **CONFIRMED**, and **strengthened**: it holds on **26 of 26** numeric rows of that table, not merely the three softfloat ones |
| Bennett's softfloat takes bit patterns | `grep` the signature | **CONFIRMED** — `soft_fma(a::UInt64, b::UInt64, c::UInt64)::UInt64` |
| §3.3's `fcmp` occurrences | the 392 goldens, and again at `607b6fe4` from `git` | **MOVED — it was a transcription error, not the corpus.** **6,799**, not the 6,789 written here on 2026-09-14, at *both* revisions. The tell was internal: §3.3's own column summed to 20,785 against §3.4's 20,795 |
| §3.3's *"our `udiv` at i64 — 139,456"* | `tests/goldens/divrem_u.counts` | **MOVED — mislabelled.** 139,456 is **`urem`** at i64; **`udiv` at i64 is 139,584**. The i128 row (557,696) *is* `udiv` and is right |
| §1a's *"`f80` — 5,891 calls, 53 fixtures"* | the 392 goldens | **MOVED, and could not be reproduced under any of three definitions.** Measured **5,955** occurrences of symbols carrying an `f80` token (5,850 for symbols whose *only* fp width is `f80`; 4,053 restricting to in-grid). Fixtures: **52**, which is what §3.4's own table already implied — 191 − 139 |
| §1a's *"and their 15 siblings"* | `softfloat.jl`'s `export` list | **MOVED** — **27** transcendental names are exported, not 19. Conclusion unaffected: the corpus calls **none** of them |
| §3.5's bitcast dead set | the pinned grid | **MOVED (arithmetic)** — the dead half is **12**, not the 10 that *"all eight `_inv` and both `f16` symbols"* adds to. Every other bitcast figure (1,511 / 127 / 24 / 12 live / 817 at `f80`) re-confirmed **exactly** |
| **Never measured at all** | — | **The 100 in §3.4 is not reachable without §6.1.** See §6.1 and R13: measured, **23** of those 100 fixtures need nothing outside the pinned grid |

**The last row is the one that matters and it is why §6.1 is now resolved rather than filed.**
Everything else on this list survived contact with a moved corpus; that one was never checked
because nothing in the 2026-09-14 session measured the *fixtures* against the grid, only the
*symbols* against Bennett.

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

**What that buys, measured:** **100** of the goldens become link-run-clean that are not today — of
**191** that touch fp at all. **THAT 100 IS CONDITIONAL ON §6.1 AND THE CONDITION WAS NOT STATED
WHEN IT WAS WRITTEN.** Re-measured 2026-09-17: **76 of the 100 also call a `cq_template_*` symbol
that is not in the pinned grid at all**, so without §6.1's vendoring an `f64`-only port completes
**23**, not 100. Both numbers are real and they answer different questions — 100 is what the
*constructions* reach, 23 is what the *link surface* reaches. §6.1 resolves which one v2 is sized
in.

## 1a. What v2 is not

- **Not the 884 fp symbols.** §2 shows the 884 is the wrong unit in *both* directions and should
  not be used to size, schedule or report v2.
- **Not `f80`.** The corpus wants it badly — **5,955 calls across 52 fixtures**, re-measured
  2026-09-17 at `b1b1dc02` (this row read *"5,891 calls, 53 fixtures"* when written on
  2026-09-14 and **neither figure reproduced**; the fixture count was already contradicted by
  §3.4's own table, 191 − 139 = 52) — and the pinned snapshot has **zero** bytes of it. Porting is impossible and re-deriving is what Rule 1 exists to forbid. §6
  records it as the v3 boundary with the one condition that would move it.
- **Not `f32`.** Bennett *appears* to cover it and does not. §3.2.
- **Not the transcendentals — and the ground is the ABI, not the corpus.** `soft_sin`, `soft_exp`,
  `soft_log`, `soft_pow` and their siblings — **27 exported names in all**, re-counted off
  `softfloat.jl`'s `export` list on 2026-09-17 — are in the snapshot and are **not** on any table
  CQ_lang ships. This bullet used to rest on *"the corpus calls none of them"*, which is true at the
  **symbol** level and misleading: **50** `slice_branchless_*` fixtures call `cos`/`exp2`/`atan2`/…
  in their C source and reach us as `fma`/`fmul`/`fadd`/`fcmp` streams, because **CQ_lang compiles
  a `math.h` function by SOURCE INJECTION (its Rule 18)** and names a per-function template
  *"fiction"*. So they arrive through the arithmetic port, at CQ_lang's injected algorithm, and a
  Bennett-backed single-opcode form is a CQ_lang decision. **§7.8**, measured 2026-09-18.
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

**(a) OVER-COVERAGE: only 196 of the 884 are ever called.** Across the goldens, 196 distinct fp
grid symbols appear; **688 appear zero times**. (Re-measured 2026-09-17 at 392 goldens: unchanged.) Sizing v2 at 884 over-states it by 4.5×, and — worse
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

**Consequence for the module map:** there is **no `M__ fpreg` module**. §5's table has no
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
| `fcmp` ×10 predicates | **6,799** | `fcmp.jl` | Bennett exports 10; the grid wants 14. The 4 missing (`ogt`, `oge`, `ugt`, `uge`) are **operand swaps**, exactly K9's shape. *(Read 6,789 until 2026-09-17; that was a transcription error at both revisions, caught because this column summed 10 short of §3.4's 20,795 — the export list and the 4-missing set both re-confirmed exactly)* |
| `fdiv` | 619 | `fdiv.jl` | |
| `fneg` / `fabs` / `copysign` | 1,124 | `fneg.jl` + bit ops | Sign-bit only: **zero Toffoli**, pure X/CX, and `fneg` on a classical rail is an L5 zero-cost row |
| `bitcast` | 1,511 | — | **Identity relabel, zero gates** (D16's third worked example, `bd 3ep`) |
| conversions | 636 | `fpconv.jl`, `fptosi.jl`, `fptoui.jl`, `sitofp.jl` | `uitofp` is `zext`-to-64 then `soft_sitofp` — upstream's own routing, not our invention |
| rounding | 120 | `fround.jl` | `floor`/`ceil`/`trunc`/`round`/`rint`/`nearbyint`, **out of grid**. **The names do not map one-to-one (§7.7, 2026-09-18):** `rint`/`nearbyint` are `soft_round` (ties-to-even), C's `round` is `soft_round_away`; there is **no `soft_rint`** — this row named the right file and implied a function that does not exist |
| `fmin` / `fmax` | 84 | `fmin.jl` | **out of grid**. `llvm.minnum`/`maxnum` are **`soft_fmin`/`soft_fmax`** (NaN-absorbing), NOT the NaN-propagating `soft_fminimum`/`soft_fmaximum` in the same file (§7.7) |
| `fsqrt` | 79 | `fsqrt.jl` | **out of grid**; digit-by-digit restoring root that *"structurally mirrors `soft_fdiv`'s restoring loop"* (`fsqrt.jl:9` (mirror) / `:11-15` (Kahan) — corrected 2026-09-18 by K21.md from `:6-7`, which was the docstring's opening line). **Had no §5 row until 2026-09-18 — now M40** |
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
| *for scale* — our `udiv` at i64 | **139,584** | 40,704 | — |
| *for scale* — our `udiv` at i128 | 557,696 | 163,328 | **131,583** |

> **THE i64 ROW READ 139,456 UNTIL 2026-09-17 AND THAT IS `urem`, NOT `udiv`.** Re-measured off
> `tests/goldens/divrem_u.counts`: `udiv` at i64 is `16,640 + 82,240 + 40,704 = 139,584`, `urem`
> is `16,640 + 82,112 + 40,704 = 139,456`. **The Toffoli column cannot catch this** — the two
> kernels share 40,704 at every width, and they differ only in CNOT. The i128 row is `udiv` and
> is correct. It moves no conclusion (`fadd` is still well under half an i64 `udiv`) and is
> recorded because a *for-scale* figure is quoted more often than a load-bearing one.

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

**Condition 1's population, measured.** 191 goldens touch fp. Under an `f64`-only scope, **100** of
them use no other fp width and no opcode outside Bennett's surface. **That is the number v2 is
worth only if §6.1 vendors the two tables**; the sentence that used to stand here said it was *"the
number v2 is worth"* full stop, and the re-measurement below is why that was half a claim.

| Scope | fp fixtures whose fp widths fit | fp calls covered | **…needing NOTHING outside the pinned grid** |
|---|---:|---:|---:|
| `f64` only | **100** | 8,793 of 20,795 | **23** |
| `f64` + `f32` | 138 | 14,664 | 28 |
| `f64` + `f32` + `f16` | 139 | 14,669 | 29 |
| all four | 191 | 20,795 | 42 |
| *blocked by an opcode Bennett lacks* | **0** | — | — |
| *blocked by a symbol the GRID lacks* | **147** | 4,913 | — |

**THE LAST COLUMN IS THE ONE THAT WAS MISSING, AND THE LAST ROW IS WHY.** Bennett's construction
surface and CQ_lang's *grid* surface are two different fences and only the first was measured on
2026-09-14. **Zero fp opcodes the corpus calls are absent from Bennett** — that row stands, is
re-confirmed, and is the finding §0 turns on. But **147 of the 191 fp fixtures call a
`cq_template_*` symbol that `opcode_table.yaml` does not contain**, so the constructions being
available does not make the fixtures reachable. §6.1.

> **THIS IS NOT A PREDICTION — IT IS VISIBLE IN THE L6 RUN ALREADY ON DISK.** At `b1b1dc02`
> (dirty), Release, sink `counter`, the recorded verdicts are **191 OK / 201 DEFERRED / 1 ABORT**,
> and the runner records *which symbol* each deferral hit. **195 defer on fp** (`cqrt_alloc_f64`
> 98, `cqrt_alloc_f80` 48, `cqrt_alloc_f32` 39, `sitofp_i32_to_f64` 7, `sitofp_i32_to_f80` 2,
> `cqrt_alloc_f16` 1) and **6 defer on `cqrt_alloc_handle`** — D16's loud refusal to mint a
> register-less handle for a CQ_lang intrinsic. Of the **98** fixtures that stop at
> `cqrt_alloc_f64` today, **96** are `f64`-only and **71 of those also call an out-of-grid fp
> symbol**: an `f64`-only port that does not vendor would move their abort from `cqrt_alloc_f64`
> to `cqrt_alloc_handle` and they would still not complete. **25 would.** That is the same finding
> as the table's 23 from a completely different instrument — the two differ only in whether a
> fixture blocked solely by an out-of-grid *integer* intrinsic is counted.

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
D16's third worked example (`bd 3ep`, 2026-09-14). Measured, and every figure here re-confirmed
exactly on 2026-09-17: **1,511** bitcast lines across **127** fixtures, of which **12 of the 24**
symbols are live (`f32↔i32`, `f64↔i64`, `f80↔i80`, each forward and `_unc`). The dead half is
**12**, not 10 — **all eight `_inv`, plus the four non-`_inv` `f16` symbols**
(`f16_to_i16`, `f16_to_i16_unc`, `i16_to_f16`, `i16_to_f16_unc`); this sentence read *"both `f16`
symbols"*, which does not make the 24 close. Under an `f64`-only scope only the
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
| **R13** | ~~**`fma` is scoped out because it is not in the grid, and it is 4,295 calls**~~ — **RETIRED 2026-09-17 as a risk and RESOLVED as a decision.** It was filed as a *sizing* risk and was measured to be an *acceptance* one: without §6.1, an `f64`-only v2 completes **23** fixtures rather than 100, and **6 fixtures with no floating point anywhere** are already blocked by it today | §6.1, now a resolution. **What remains a risk is the inverse and it has its own row: R16.** Do not re-open R13 to re-argue the sizing — the number is measured and the decision is written |
| **R16** | **The vendoring lands AHEAD of the port, and 401 symbols become libcqops aborts for no fixture gain** while the LINK GATE widens from 2,479 to 2,880 and `tools/l6/run_slice_cqops.sh` must stop passing CQ_lang's two stub objects. Both are load-bearing and both have recorded silent-failure modes | §6.1's order clause: **vendor WITH the port of the families it exposes, never ahead of it.** The L6 link-line change is not optional once we define those symbols — the two objects are passed *loose*, so a duplicate definition is a hard link error rather than the silent shadowing `CqopsSymbolSets.cmake` records for the archive form |
| **R14** | **`BENCHMARKS.md`'s magnitudes are reused as if they were L4 goldens.** This repo has a recorded instance of exactly that (`x+1 i8` at `100/4/68/28`) | §3.2's rider (ii). The alphabet conclusion is fold-invariant; **the magnitudes are not and must be re-measured against our own port**, at the all-quantum mask, before any golden is pinned |
| **R15** | **`f80`'s `long double` bit extraction is written as a `sizeof` `memcpy`**, silently hashing unspecified padding into a rail | §3.1's box. Not live while `f80` is out of scope, which is part of why it is out |

---

## 5. Module map — seams recorded in advance

Plan §3's idiom: **every module over ~200 lines gets its split seam recorded before a line is
written** (Rule 12), so hitting the limit is a scheduled split and never a surprise refactor.
Numbering continues v1's M01–M30.

| Module | What | Est. LOC | **Seam, recorded in advance** |
|---|---|---:|---|
| **M31 `fpfield`** | **K22** (number assigned 2026-09-18 so the K-doc exists). The IEEE field views over a `cq_bit` array — sign / exponent / mantissa as `cq_scratch_span`-shaped read-only views, the implicit-bit rules, and the class predicates (`is_nan`, `is_inf`, `is_zero`, `is_subnormal`) as one-bit kernels | 180 | **VIEWS ↔ CLASS PREDICATES.** The views emit nothing and are pure addressing; the predicates are Rule 7 kernels with `dst` one bit, which is **K9's shape** and needs K9's test adapter. Split at the first predicate |
| **M32 `fpround`** | **K23** (assigned 2026-09-18). The shared round-and-pack path — `soft_fround`'s `_sf_round_and_pack`, sticky/guard/round bits, the carry-out that bumps subnormal→normal **Widened 2026-09-18 (K15.md risk 5): M32 also owns the shared normalisation helpers `_sf_normalize_clz` and `_sf_handle_subnormal` (`softfloat_common.jl`), which `fadd`/`fmul`/`fdiv`/`fma` all call — assigned here so they are transcribed once as exported step blocks, not four times** **— and `_sf_normalize_to_bit52` too (K21.md, 2026-09-18): nine call sites in `fsqrt`/`fmul`/`fdiv`/`fma`/`flog`, and `fsqrt` calls it INSTEAD of the two above (`fsqrt.jl:27-29`, no subnormal result path)** | 260 | **PACK ↔ ROUND.** Upstream keeps them in one helper; at our LOC limit the rounding-decision logic splits from the field assembly. Seam is at the point the decision bit is known **K23.md DRAFTED 2026-09-18 (bead `9ve.35`, pre-implementation): four helpers, 154 rows (41 views); it records that PACK ↔ ROUND no longer partitions a four-helper module, and that M32 has NO host oracle so what L1 IS for it is a decision owed before `9ve.19` starts (K23.md §6.5 risk 1).** |
| **M33 `fadd`** | K15 — `soft_fadd` / `soft_fsub` | 280 | **ALIGN ↔ ADD-AND-NORMALISE.** The exponent-difference shift is the half that is `O(W)` in its own right and is the half `fma` reuses. **Do not spell `fsub` as `fadd(a, fneg(b))`** — upstream fixed a NaN-RHS sign bug in exactly that composition (`Bennett-m63k`) |
| **M34 `fmul`** | K16 — `soft_fmul` | 240 | **SIGNIFICAND PRODUCT ↔ EXPONENT/PACK.** The 53×53 product is M18's `mul` over a span; the seam is where the integer kernel ends. **Fixed 2026-09-18 (K16.md): the cut is at `fmul.jl:136`, after the 106-bit `(hi, lo)` assembly — the pair M39 reuses — not at `:81` after the four raw products, so all four `cq_mul_block` offsets stay in one file. Three K16 items settled by this document rather than in code: `ma & 0x03FFFFFF` is a VIEW (§7.3 as amended the same day — constant masks are wiring; this note first said BLOCK for three hours); `fmul.jl:94-102`'s abandoned bindings are TRANSCRIBED and marked dead, on §7.6's discarded-arm rule (the grain is mechanical, and 960 slots is the price of not deciding what is live); K11.md §7.5's `n_a·n_b` product identity does NOT hold inside M34, where a mask block's output is pre-materialised scratch (64 quantum lanes) — slots are unaffected, gates must be measured** |
| **M35 `fdiv`** | K17 — `soft_fdiv` | 240 | **The 56-bit restoring-division loop ↔ pre-normalisation.** The loop is K12's shape and should **compose M19's exported step block, not transcribe it** (plan §0.4's obligation, and the K11/K12 composition-check trap applies verbatim) **K17.md DRAFTED 2026-09-18 (bead `9ve.35`, pre-implementation) and it measures that this row and §7.2 select DIFFERENT constructions: the loop's middle three lines are operator-for-operator K12's P1/P2/P3, its two ends are not, and it runs 56 times over 64-bit spans. K17.md §5.1 states both options; DECISION FOR THE MAINTAINER, pending — do not implement `9ve.25` before it is taken.** |
| **M36 `fcmp`** | K18 — the 10 ported predicates + the 4 operand-swaps | 200 | **ORDERED CORE ↔ PREDICATE TABLE.** K9's exact shape: *four predicates swap operands and five invert the flag, and the two sets are not the same set*. Expect the fp table to differ; **derive it, do not carry K9's over** **LANDED 2026-09-18 (Wave 3, bead `9ve.20`, K18.md): the recorded seam was taken AND a second one, ROW TABLES ↔ STEP MACHINE (K18.md D-K18-7) — `fcmp.c` (the three row tables), `fcmp_step.c` (slot/region arithmetic, operand resolution, dispatch), `fcmp_pred.c` (the ten concatenations, fourteen entry points, the C short-circuit). The ordered-core half alone measured 298/300, so the second cut was forced, not chosen. Every M33+ kernel is bigger than `fcmp` and should record the same two seams IN ADVANCE. Ten predicates × forward/`_unc` pinned in `tests/goldens/fcmp.counts`; slots and gates come apart 25–35 % at the all-quantum mask, exactly as K18 D-K18-6 predicted.** |
| **M37 `fconv`** | K19 — `fptosi` / `fptoui` / `sitofp` / `uitofp` / `fpext` / `fptrunc` | 220 | **INT→FP ↔ FP→INT.** They share nothing but the field views, and only the second has a saturation/undefined story (D3's posture) |
| **M38 `fmisc`** | `fneg` / `fabs` / `copysign` / `fmin` / `fmax` / rounding | 200 | **SIGN OPS ↔ EVERYTHING ELSE.** The sign ops are zero-Toffoli and have a true L5 row; the rest go through M32 |
| **M39 `fma`** | K20 — `soft_fma`. **§6.1 took it (2026-09-17), so this row is no longer conditional** | 260 | **Reuses M33's align and M34's product** — **as VOCABULARY, not as blocks (K20.md, 2026-09-18): `fma`'s product is `_sf_widemul_u64_to_128`'s 32/32 split, not `fmul`'s 27/26 (`softfloat_common.jl:261-263`: 27/26 "assumes ≤53-bit inputs" and Berkeley scaling makes `fma`'s 63), and its align is the four-case 128-bit `_shiftRightJam128` ×2, not `fadd`'s one-case 64-bit shift. What it reuses is M18/M12/M16/M17/M14's step blocks, exactly as M34 does; M33/M34 owe it nothing. Its 128-bit adds materialise the carry as a VALUE (compare + mux + a second add), so `cq_add_block` needs no carry-in or carry-out — a carry-chained 128-bit adder would be a re-derivation. Seam fixed between `fma.jl:115` and `:118`: PRODUCT-AND-ALIGN ↔ THE SINGLE-ROUNDING PATH.** The single-rounding path is the whole point of an `fma` and is what forbids spelling it `fmul` then `fadd` |
| **M40 `fsqrt`** | K21 — `soft_fsqrt`. **Added 2026-09-18**: §3.3 listed it and this table had no row for it | 240 | **PRE-NORMALISE ↔ THE DIGIT LOOP.** The loop is `soft_fdiv`'s restoring shape a second time (`fsqrt.jl:9` (mirror) / `:11-15` (Kahan) — corrected 2026-09-18 by K21.md from `:6-7`, which was the docstring's opening line), two bits per iteration over a 128-bit radicand held as a `(hi, lo)` pair — compose M14/M16/M17's step blocks as M35 does, do not transcribe |
| *(no module)* | **the fp rail representation** | 0 | §3.1 — there is nothing to build |
| `cq_runtime_rail.c` / `cq_runtime_gate.c` | the 34 deferred fp-width core symbols | — | **Already recorded in plan §3's Layer-5 seam table**, and the recorded disposition is *"they become real code by WIDENING what already exists"*. No new seam |

**Three shared obligations. The first two are already recorded traps; the third was found 2026-09-18 and is owed BY v1 modules (§7.10).**

- **Every composite fp kernel calls the other kernel's *step function*, never the kernel**
  (`cq_sandwich` refuses nesting in both configurations). M33/M34/M35 must export compute halves the
  way M14/M16/M17 do, and M39 consumes them.
- **Every kernel whose cost has a closed form needs the composition check written BEFORE the
  kernel.** K11's recorded finding is that the one mutant L1 cannot see is the one that looks like an
  optimisation, and that L4 is not a durable detector because `CQOPS_UPDATE_GOLDENS=1` blesses the
  reduction. fp offers this trade everywhere — every field is provably narrower than its span.
- **Three step blocks do not exist yet and every row above assumes them**: M12's variable-shift
  barrel, M16's `eq`/`ne`, and M18's `cq_mul_step` (only the count `cq_mul_steps` is exported).
  Measured 2026-09-18 over `src/kernels/*.h`; §7.10. **Two more found by K15.md's block table the same day: M14 exports `cq_sub_block` but no `add` block (`soft_fadd` has two `+` occurrences), and M16 exports no `slt` block (four signed compares). Filed as `9ve.30` / `9ve.31`; both have silently-wrong substitutes (K8 for K6 = right forward, wrong `_unc`; unsigned for signed = wrong on a negative `result_exp`).**

---

## 6. Open questions — decide in a document, never in code

**§6.1 is RESOLVED (2026-09-17). The rest are open, and each is a bead.**

### 6.1 Does v2 vendor `intrinsic_table.yaml` and `libm_table.yaml`? — **YES, and it is not only a v2 question**

**DECIDED 2026-09-17: v2 VENDORS BOTH TABLES, at the same pinned-sha256 discipline
`opcode_table.yaml` already carries. The complete `cq_template_*` link surface is 2,880 symbols and
that is the surface a complete shim satisfies.** The sub-question this resolves is **(a) vendoring**
— the shim/link question. **(b), porting `soft_fma` and the rest, is NOT decided here**: it is a
kernel question, it is sized below, and it stays scoped.

**The four measurements that forced it, all 2026-09-17 against CQ_lang `b1b1dc02` (dirty tree,
392 goldens) except where noted.**

1. **IT IS ALREADY BINDING ON v1, AND ON FIXTURES THAT CONTAIN NO FLOATING POINT.** The L6 run on
   disk defers **6** fixtures on `cqrt_alloc_handle` — `slice_funnel_fshl_qq`,
   `slice_intrinsic_bswap`, `slice_intrinsic_ctpop`, `slice_intrinsic_ctlz`,
   `slice_ctlz_idiom_loop`, `specialize_intrinsic_caller`. **Not one of them touches fp.** They
   call `ctlz` / `ctpop` / `bswap` / `fshl` at `i32`, which live in `intrinsic_table.yaml`. No
   amount of fp work reaches them, and they cannot satisfy NORTH_STAR condition 1 while the table
   is unvendored. **This document framed §6.1 as an fp sizing question and it is not one.**
   Corpus-wide: **10 out-of-grid integer symbols, 35 occurrences, 15 fixtures**, 6 of them with no
   fp at all.
2. **WITHOUT IT, v2's HEADLINE DELIVERABLE IS 23, NOT 100** — §3.4. 76 of the 100 `f64`-only fp
   fixtures call an out-of-grid fp symbol, and all 76 call a *minting* one rather than only an
   `_unc`, so all 76 reach `cqrt_alloc_handle` and abort. Publishing 100 against an achievable 23
   is precisely the stale-figure failure this repo's apparatus exists to prevent.
3. **THE COST OF VENDORING IS THE ACT WE HAVE ALREADY PERFORMED ONCE, AT A KNOWN RISK CLASS.**
   Both tables' sha256s have been recorded in `third_party/cq_lang/COMMIT` since 2026-08-14 and
   **both are unchanged at `b1b1dc02`** (verified: `b22cf3a3…` and `c3be3d43…`, corroborated by
   the `yaml-sha256` banners in CQ_lang's own generated headers). The two surfaces are
   **disjoint** — 2,479 ∩ 401 = ∅, 2,479 ∪ 401 = 2,880, measured — so nothing is renegotiated,
   only added.
4. **THE STATUS QUO IS NOT NEUTRAL, IT IS A STUB SERVING 401 SYMBOLS.** `tools/l6/` passes
   `cq_intrinsic_templates.c.o` and `cq_libm_templates.c.o` on the link line *by design*, and
   every minting body in them is `int32_t h = cqrt_alloc_handle(); printf(...); return h;` with
   **no gate emitted anywhere in that translation unit**. D16 / `bd ck6` already refused to let
   that mint a real rail, and refused it for the right reason — *"the rail would be a LIE at any
   width"*, the Prime Directive's clean-trace / wrong-circuit signature. **That refusal is
   correct and is not reopened here.** What this decision says is that the way out of it is to
   *own* those symbols, not to keep deferring them.

**THE ORDER IS PART OF THE DECISION, AND IT IS THE HALF THAT IS EASY TO DROP.** Vendoring alone
unblocks **zero** fixtures: it moves each abort from a CQ_lang stub that calls our refusal to our
own refusal, which is a strictly better diagnostic and not a fixture. So **vendor WITH the port of
the families it exposes, never ahead of it.** Two mechanical consequences follow the moment we
define those 401 symbols, and neither is optional:

- **`cmake/CqopsSymbolSets.cmake`'s `want` set widens from the 2,479 manifest to the 2,880 one.**
  That file asserts the archive defines *exactly* the grid, in **both** directions, precisely
  because an out-of-grid definition otherwise links green — its own header records the measured
  case, one added `cq_template_lrint_f64_to_i64` producing link-order-dependent silent shadowing
  in which the same two archives in opposite orders both exit 0 and return 7 and −1.
- **`tools/l6/run_slice_cqops.sh` must stop passing those two objects.** They are passed *loose*,
  not as archive members, so once libcqops defines the same symbols the result is a hard
  duplicate-symbol error — loud, and at the link stage. That is the good failure mode, but it is
  a change to the L6 driver that has to land in the same commit.

**What was NOT taken.** The coherent alternative is *do not vendor* — hold that libcqops' scope is
`opcode_table.yaml` and nothing else, and that those 155 fixtures are permanently out of scope.
It is defensible and it is what PRD-v1 §1 says today. **It was refused because it is not free:** it
requires restating §3.4's population at **23**, restating §1's *"what that buys"* at 23, and
accepting that **6 integer-only fixtures never meet condition 1** for a reason unrelated to
floating point. If that branch is ever taken, those three restatements are the price and must be
written, not left implied.

**Sizing, now that (a) is decided.** The port (b) that the vendoring exposes is, in corpus-call
order: `fma` (4,295 — `fma.jl`, `soft_fma`, upstream-measured at **247,398 gates / 135,743
ancillae**, under half an i128 `udiv`), `fabs` (192) and `copysign` (143) — sign-bit only, zero
Toffoli — `sqrt` (79, `fsqrt.jl`), the rounding family `floor`/`rint`/`round`/`trunc`/`nearbyint`
(120, `fround.jl`), and `fmin`/`fmax` (84, `fmin.jl`). **Every one is in the pinned snapshot**, so
Rule 1 is satisfiable for all of them; `fabs` and `copysign` are not separate upstream files
because they are bit operations. **M39 is no longer conditional** — §5's *"K20 — `soft_fma`, if
§6.1 takes it"* is now taken, and `fabs`/`copysign`/`fmin`/`fmax`/rounding were already M38's.

**Still open under §6.1, and deliberately:** whether the 401 get their own residual bucket or join
`cq_runtime_v2.c` is question 6 below, not this one; and the integer intrinsics
(`ctlz`/`ctpop`/`bswap`/`fshl`/`umin`/`umax`) are a **v1-shaped** gap that this decision exposes
rather than closes — they need a bead of their own, because they are reachable today and are not
floating point.

### 6.2–6.6 Still open — decide each in a document, never in code

**Numbering is preserved so existing references still resolve.**

2. **Does `f80` ever arrive, and on what condition?** Not by porting — the snapshot has zero bytes of
   it — and not by re-deriving, which Rule 1 forbids. The only clean route is **upstream growing
   80-bit support and this repo re-vendoring at a new pin** (risk R3, invalidating every L4 golden).
   Until then `f80`'s **5,955 calls and 52 fixtures** (re-measured 2026-09-17; this read *"5,891
   calls and 53 fixtures"* and neither reproduced) are the v3 boundary. **It should be stated as a
   dependency, not carried as a gap.**
3. **Does `f32` arrive on upstream's native primitives?** `fpconv.jl` names *"native 24-bit-mantissa
   f32 primitives"* as upstream future work. That — and only that — flips §3.4's box.
4. **Is D14's non-injective set 232 or 460?** §3.5. A re-reading of a decision of record, which
   should be settled in D14 or in a new row, not assumed by an implementer.
5. **What are L1's fp anchors?** R12. **WRITTEN 2026-09-18 in §7.12**; bead `hkg` keeps only the
   sampler change in `tests/support/`. Needs to be written down before M31, because it changes
   `tests/support/`'s sampler and not just a test.
6. **Does the fp surface get its own `cq_runtime_v2.c`-style residual bucket**, or do the 688
   *(and, since §7.9, `lrint`/`llrint`, `fpext`/`fptrunc` and `frem` at `f64` with them)*
   never-called fp symbols stay in the existing one? A bucketing question with the same shape as D16,
   and the answer probably follows D16's *family first, width second* rule.

---

## 7. The port's mechanics — decided 2026-09-18

**Every item below was raised, measured and settled in one session, against Bennett `980805de`
(pinned) and CQ_lang `67ace652` (docs-dirty; not pinned). Each is a decision; the register's pointer
is PRD-v1 §15 D26's amendment of the same date.** The *shape* of the port was never in question —
§3.1 and §5 already say an fp opcode is one `cq_sandwich` whose compute half composes the ported
integer step blocks — but "composes" hid fifteen choices, and a port that makes them silently is
exactly the miscompile shape this repo exists to prevent.

### 7.1 What is being copied: the Julia SOURCE, lowered through OUR blocks — there is no gate stream to copy

The snapshot ships **no** emitted circuit for any softfloat routine (measured: no `.qasm`, no gate
dump, no circuit collection under `third_party/bennett/` beyond three benchmark `.jsonl` result
files). `soft_fadd(a::UInt64, b::UInt64)` is Julia integer code — its own header says *"Uses only
integer operations. Bit-exact with hardware `+`."* — which Bennett's compiler lowers through the
same `lower_*!` primitives K01–K12 already port. The `+` inside it goes through `lower_binop!` to
the ripple adder (`:auto` always resolves to `:ripple`, `arith.jl:9`), which is K6/K7; `ifelse` is
K10's mux (287 occurrences across the f64 files); `<`/`==`/`!=` are K9; the variable shifts are
K4's barrel; `*` in `fmul` is K11; the `fdiv` loop is K12's shape; there is no `leading_zeros`
anywhere, so normalisation is a shift ladder of the same primitives. **So "port `soft_fadd`" means:
transcribe each Julia line as one step block over the operands' spans, and hand `cq_sandwich` a
step function that dispatches a global step index into those blocks — exactly
`src/kernels/divrem_u.c`'s shape, which emits two CNOTs of its own and dispatches everything
else.** Rule 1 is satisfied by construction: no gate in the result is ours.

**What such a kernel can get wrong is the slot arithmetic, not the gates** (CLAUDE.md's K12
callout): a phase boundary off by one, a span aliased to the wrong intermediate, a block fed an
operand of the wrong width. The instrument is K12's — a case that re-derives the op KIND of every
compute-half slot from the blocks' own structure and compares it against the recorded stream at
the all-quantum mask — and it is written **before** the kernel, with the composition check.

### 7.2 Transcription grain: LITERAL — one block per operator occurrence, 64-bit spans, no CSE, no narrowing

Bennett compiles LLVM-optimised IR; we transcribe the Julia source, and the two differ. `ea ==
0x7FF` is computed twice in `soft_fadd` (`a_nan` and `a_inf`) and LLVM would CSE it; every
intermediate is a `UInt64` although the exponent is 11 bits. **Decision: literal.** One block per
operator occurrence as the source spells it; every intermediate a 64-bit span (the Julia type's
width); no common-subexpression sharing; no narrowing towards the field width. **Clarified 2026-09-18 (K18.md D-K18-1): "the Julia type's width" is the rule and "64-bit" is
its value for `UInt64` — a `Bool` intermediate (nine of them in `fcmp.jl`, e.g. `a_nan`, `both_zero`)
is a ONE-bit span, exactly as Bennett gives `i1` one wire; widening it to 64 would be a re-derivation
in the other direction.** **And `UInt64(1) − x` on a 0/1 value (`fcmp.jl:102`, `:154`; the idiom recurs in every
fp file) is M14's `sub` block with the constant `1` as a source — decided by the maintainer
2026-09-18 against K18.md's draft D-K18-2 (`lower_not1!`): one block per OPERATOR occurrence, no
reading of what the operator "means", and the fold table elides nearly all of the sub's gates anyway.** Two grounds. D9's
K12 precedent refused exactly this narrowing (`~17W²` towards `~8.5W²`) because it is a
re-derivation rather than a port; and K11's finding stands that the one mutant L1 cannot see is
the one that looks like an optimisation. The composition check pins `compute = Σ blocks`, each
block's cost **asked** of its module at width 64 and never written down. **Consequence:** our
counts differ from `BENCHMARKS.md` in both directions — up by Rule 2's doubling, down by §7.3 —
and R14 already says those magnitudes are not goldens.

### 7.3 Constants are SOURCES, never scratch

Bennett allocates fresh wires for every constant (`arith.jl:202`: *"Constants are always safe
(their wires are freshly allocated by resolve!)"*), so `INDEF`, `SIGN_MASK`, `0x7FF`, `IMPLICIT`
each cost 64 of its 29,949 wires. We pass them as `const cq_bit` arrays of `CQ_BIT_ZERO`/`ONE`
and the §3 fold table elides every gate they control — a `CCX` with a `ZERO` control is nothing,
with a `ONE` control a `CX`. **Our peak is therefore BELOW upstream's ancilla figure for the same
construction**, and the difference is not a bug to chase in either direction. `a & ~SIGN_MASK`
costs zero gates and zero qubits on classical lanes and one CX per quantum lane. **AMENDED 2026-09-18 (maintainer's decision at the Wave 2 boundary, bead `9ve.18`): an AND or a shift by a
COMPILE-TIME CONSTANT is WIRING, not a block — a VIEW keeping the lanes the mask leaves at ONE and
`CQ_BIT_ZERO` elsewhere, with no copy, no scratch and no gate. So `a & ~SIGN_MASK`, `(a >> 52) & 0x7FF`,
`ma & 0x03FFFFFF` and every `& FRAC_MASK` cost NOTHING; the "one CX per quantum lane" above was the
and-BLOCK reading and is retired. M31's `cq_fp_view_*` are the implementation; K15/K16/K18/K20/K21's
mask rows that were counted as `and`(64) blocks become views (their drafts predate this line). The
literal grain (§7.2) is untouched for every operator whose operands are not compile-time constants.**

### 7.4 The classical short-circuit is a C TRANSCRIPTION of the same body over `uint64_t` — the library never does `double` arithmetic

Every v1 kernel carries risk R9's short-circuit: all-classical operands never enter the sandwich
and `dst` is written as constants from a C computation. For fp the obvious C computation is the
host's `double` operator, and **that is refused, on a measurement** (`cc -std=c11 -O0
-ffp-contract=off`, this box):

| Cell | This box (x86_64) | Bennett |
|---|---|---|
| `Inf − Inf` | `fff8000000000000` | `INDEF = 0xFFF8000000000000` (`softfloat_common.jl:14`, Intel SDM Vol 1 §4.8.3.7) |
| `0 · Inf` | `fff8000000000000` | same |
| `qNaN₁ + qNaN₂` | the FIRST operand's payload, in both orders | first operand |
| `qNaN + sNaN`, both orders | **the FIRST operand, quietened if it was signalling** (corrected 2026-09-18, see below; the 2026-09-18 draft said "the qNaN — no signalling priority") | `_sf_propagate_nan2`: `ifelse(a_nan, a \| QUIET_BIT, b \| QUIET_BIT)` (`softfloat_common.jl:23-24`, "first-operand NaN (x86 SSE rule)") |
| `sqrt(−1)` | `fff8000000000000` | `INDEF` |

Every cell is IEEE-unspecified and every value is x86's choice; ARM's differ on all of them (a
positive default NaN, and signalling priority).

> **CORRECTION 2026-09-18 (bead `9ve.4`, `tests/support/fphost.c`).** The `qNaN + sNaN` row above
> was first measured at a DEGENERATE payload pair: quietening `sNaN 0x7ff0000000000001` gives exactly
> `qNaN 0x7ff8000000000001`, so "the qNaN won" and "the first operand won" print identical bits. At
> `qNaN_B = 0x7ff8000000000002` they separate: `qNaN_B + sNaN = …0002` and `sNaN + qNaN_B = …0001`
> (the quietened sNaN), re-measured with Apple clang 17 and Homebrew clang 22 at `-O0` and `-O3`. The
> rule on this box is **the first operand, quietened** — which is also upstream's, verbatim — so the
> host oracle and the circuit still agree on the cell; only the prose was wrong. "No signalling
> priority" is true and is not the whole rule. The host probe pins the NON-degenerate row for this
> reason (CLAUDE.md: a table of hand-derived cells needs one non-degenerate row to fix its
> convention). `hkg`'s anchors must use distinguishable payloads for the sNaN-vs-qNaN pair. So the host operator agrees with the circuit **on
this box** and would disagree on an arm64 box — a program whose classical mode and quantum mode
return different bits on the same input, on some hosts only. **Decision: the short-circuit is a C
transcription of the Julia body over `uint64_t`** — the same source, evaluated on constants — so
both modes agree bit-for-bit on every host; the library's only contact with a C `double` is the
`memcpy` at `cqrt_alloc_f64`/`cqrt_measure_f64`; PRD §14's "nothing beyond libc" is untouched;
and `-ffast-math`/x87 excess precision cannot reach us. Cost: ~60 lines per kernel, which L1
checks against the circuit on every mixed-mask case.

**L1's oracle stays independent: the host operator, with the IEEE-unspecified cells pinned by
TABLE rather than computed.** An oracle that shares code with the implementation is blind to what
that code gets wrong (the Step 18 trap), so the reference does NOT reuse the transcription; and
the cells above are asserted as literals so an arm64 box reaches the same verdict.
`tests/support/` gains a **host probe** — round-to-nearest, FTZ/DAZ off — of the sanitizer
probe's shape, because a host with FTZ set would fail every subnormal anchor in the oracle rather
than in the port.

### 7.5 `fptosi` / `fptoui` on NaN and out of range: upstream's x86 saturation, pinned; D3's posture

C is undefined and LLVM is poison there, so the C cast cannot be the reference for those cells.
Upstream is explicit: `soft_fptosi` saturates *"any NaN, ±Inf, or out-of-range operand … to
INT_MIN = 0x8000000000000000 per Intel SDM Vol 1 §4.8.3.7"* (`fptosi.jl:6-8`), and `soft_fptoui`
follows x86 `cvttsd2si` with the 2^63 bias, saturating to the same indefinite value and
reinterpreting `(−2^63, 0)` as two's complement — *"honesty over a stricter LLVM-spec
saturation"* (`fptoui.jl`). **Decision: pin exactly those values** — deterministic, documented,
never traps — which is D3 for `sdiv` by zero and D8 for shifts, applied a third time.

### 7.6 Variable-amount shifts go through D8, and each one is audited before its file is ported

Our barrel reads only the low `cq_shift_stages(W)` = `⌈log₂ W⌉` lanes of the amount — **6 at
W=64** — and every stage zero-fills, so the variable path computes `sat_shift(a, k mod 2^6)`
(`src/kernels/shift_var.c`; PRD §15 D8, *"MASK, THEN SATURATE"*). At W=64 the mask is the whole of
it and the saturation never fires, since `k mod 64 < 64 = W`: an amount of exactly **64** becomes a
shift by **0** where Julia's `>>`/`<<` on a `UInt64` gives **0**, and 65..127 become 1..63 where
Julia still gives 0. **D8 and the Julia source therefore agree on every amount in `[0, 63]` and can
disagree at every amount ≥ 64**, which reduces the audit to one question per site: what is that
amount's provable range?

**AUDITED 2026-09-18 over the fifteen `f64` files on the port list — 25 variable-amount shift
operators on 24 lines. Every one is CLAMPED (the source bounds the amount below 64) or DISCARDED
(the out-of-range result is provably never selected). NOT ONE IS EXPOSED, so the port owes no D8
decision on any of these files.** `fadd.jl`'s two sites are the pre-existing entry, re-verified
here. A file with no variable shift gets a row saying so, because "it has none" is the finding.

| File | var. | Sites, the amount's provable range, and what bounds it | Verdict |
|---|---|---|---|
| `fadd.jl` | **2** | `:77` `(UInt64(1) << d_clamped)`, with `d_clamped = ifelse(d == UInt64(0), UInt64(1), ifelse(d >= UInt64(64), UInt64(63), d))` at `:76` → **`[1, 63]`**. `:79` `wb_mid = (wb >> d) \| sticky`, `d = ea_eff - eb_eff` **unclamped** and reaching 2046 — but `:81-83` selects `wb_mid` only for `0 < d < 56` (`ifelse(d >= UInt64(56), wb_large, ifelse(d > UInt64(0), wb_mid, wb))`) | CLAMPED `:77` · **DISCARDED** `:79` |
| `fsub.jl` | **0** | one shift, `:21` `>> 52`. `soft_fsub` is `soft_fadd(a, soft_fneg(b))` behind a NaN guard, so it inherits `fadd`'s two rows and adds none | — |
| `fmul.jl` | **0** | every amount a decimal literal (`12`, `14`, `15`, `26`, `38`, `41`, `42`, `49`, `50`, `52`, `56`, `63`). Inherits `_sf_normalize_to_bit52`, `_sf_normalize_clz`, `_sf_handle_subnormal`, `_sf_round_and_pack` | — |
| `fdiv.jl` | **0** | the restoring-division loop at `:91` is `for i in 0:55` with literal `<< 1` throughout; `:107` `wr << 1` is literal. Same four helpers inherited | — |
| `fma.jl` | **0** | all literal (`6`, `9`, `10`, `56`, `62`, `63`). Its variable alignment is **delegated**: `:99` `_shiftRightJam128(p_hi, p_lo, -expDiff)` and `:110` `_shiftRightJam128(mc_s, UInt64(0), expDiff)` hand an unbounded `expDiff` to a helper that clamps it — see the `softfloat_common.jl` row | — |
| `fsqrt.jl` | **0** | the digit-recurrence loop at `:80` is `for i in 0:63` with literal `<< 2` / `<< 1` / `>> 62`; the radicand set-up at `:68-69` is `>> 6` and `<< 58` | — |
| `fcmp.jl` | **0** | eight shifts, all `>> 52` or `>> 63` | — |
| `fmin.jl` | **0** | eight shifts, all `>> 52` | — |
| `fneg.jl` | **0** | no shift of any kind — the body is one expression, `a ⊻ UInt64(0x8000000000000000)` | — |
| `fptosi.jl` | **2** | `:49` `full_mant >> right_shift_clamped` and `:50` `full_mant << left_shift_clamped`, both off `ifelse(x > UInt64(63), UInt64(63), x)` at `:45-46` → **`[0, 63]`**. The **unsigned wraparound is the mechanism, not an accident**: `left_shift = exp - UInt64(1075)` at `:42` wraps for `exp < 1075` into a value that is `> 63` and so clamps | CLAMPED |
| `fptoui.jl` | **0** | two shifts, `>> 63` and `>> 52`. Calls `soft_fptosi` twice and `soft_fsub` once; inherits their rows | — |
| `sitofp.jl` | **1** | `:60` `magnitude << shift_clamped`, with `shift_clamped = ifelse(clz > UInt64(63), UInt64(63), clz)` at `:59` → **`[0, 63]`**. The clamp is **inert**: the six-stage CLZ at `:29-50` adds at most `32+16+8+4+2+1 = 63`, so `clz ∈ [0, 63]` structurally — 63 being the all-zero case, which `is_zero` overrides at `:83` | CLAMPED |
| `fround.jl` | **9** | `soft_trunc` `:39`, off `frac_bits_clamped = clamp(frac_bits, Int64(0), Int64(52))` → **`[0, 52]`**. `soft_round` `:172` `:176` `:178` `:183` and `soft_round_away` `:266` `:276`, all off `frac_bits = clamp(frac_bits_raw, Int64(1), Int64(52))` → **`[1, 52]`**; `:175` and `:269` off `round_bit_pos = frac_bits - Int64(1)` → **`[0, 51]`**. `soft_floor` and `soft_ceil` add none — they call `soft_trunc` (`:62`, `:87`) | CLAMPED |
| `fpconv.jl` | **3** | `soft_fpext` has none; its normalisation is `_sf_normalize_to_bit52`. `soft_fptrunc` `:156` `(m_full >> shift_sub)`, with `shift_sub = UInt64(clamp(Int64(30) - e_new, Int64(1), Int64(63)))` at `:155` → **`[1, 63]`**; `:157` `>> (shift_sub - UInt64(1))` and `:158` `<< (shift_sub - UInt64(1))` → **`[0, 62]`**, the clamp's lower bound of **1** being exactly what stops that subtraction wrapping | CLAMPED |
| `softfloat_common.jl` | **8** on 7 lines | `_sf_handle_subnormal` `:180` `:182`, off `shift_u = UInt64(ifelse(flush_to_zero, Int64(0), clamp(shift_sub, Int64(0), Int64(63))))` at `:177-179` → **`[0, 63]`**. `_shiftRightJam128` `:382` `:384` and **both** shifts of `:385`, off `dA_u = UInt64(clamp(dist, Int64(1), Int64(63)))` → **`[1, 63]`** — `:385`'s first is `a_hi << (UInt64(64) - dA_u)`, also `[1, 63]`; then `:394` `:398`, off `dB_u = UInt64(clamp(dist, Int64(64), Int64(127)) - Int64(64))` → **`[0, 63]`**. The CLZ helpers `_sf_normalize_to_bit52`, `_sf_normalize_clz`, `_sf_clz128_to_hi_bit61` and the two `by1` shifters are entirely literal | CLAMPED |

**NOTHING IN THESE FILES IS 128 BITS WIDE, SO THE MASK STAYS 6 LANES AND NEVER 7.** `UInt128`
occurs in the fifteen only in prose — `softfloat_common.jl:235-252` records that native `UInt128`
*would* compile cleanly and was declined anyway, because the hand-rolled hi/lo pair is "the direct
ancestor of the gate sequence soft_fma emits". `fsqrt`'s 112-bit radicand and `fma`'s 128-bit
accumulator are both `(hi, lo)` `UInt64` pairs, and `_shiftRightJam128` is a 128-bit shift built
out of 64-bit ones. Every shift the port will see is on a 64-bit span — §7.2's grain — so
`⌈log₂ 64⌉ = 6` throughout.

**UPSTREAM CLAMPS IN TWO DIFFERENT SIGNEDNESSES AND THE PORT MUST NOT PICK ONE FOR ALL OF THEM.**
`fround.jl`, `fpconv.jl` and both helpers in `softfloat_common.jl` clamp an **`Int64`** that is
genuinely negative on the unselected branches (`Int64(1075) - exp` reaches −972 at `exp = 0x7FF`),
so those sites need the **signed** compare. `fadd.jl:76`, `fptosi.jl:45-46` and `sitofp.jl:59`
compare a **`UInt64`** whose wraparound is the mechanism, so those need the **unsigned** one.
Substituting one for the other at `fptosi.jl` inverts the out-of-range guard and lets a wrapped
64-bit amount reach the shift — K9's `uge`-meaning-`ule` in a different dress, and equally
invisible to a gate count.

**A DISCARDED ARM IS STILL EMITTED, WHICH IS §7.2's GRAIN AND NOT A COST TO OPTIMISE AWAY.**
`fadd.jl:79`'s `wb >> d` is computed for every `d` and thrown away by the `ifelse` above 56, so
the D8 divergence never reaches a value — but the barrel is still built, still costs its
`W(3L+1)` scratch qubits, and is still uncomputed. **DISCARDED is a claim about the RESULT, never
about the circuit**; narrowing or skipping the arm would be re-deriving the construction (Rule 1)
and would strand the mux that selects it.

**THE MECHANICAL SWEEP, AND THE OBVIOUS PATTERN IS INCOMPLETE.**
`grep -nE '(<<|>>) *[a-z_]' <file>` misses a **parenthesised** amount: it does not find
`fpconv.jl:157` or `fpconv.jl:158` at all, and it matches `softfloat_common.jl:385` only because
that line's *second* shift happens to be bare. The complete form is
`grep -nE '(<<|>>)[[:space:]]*[^0-9[:space:]]' <file>` — and the character class must exclude the
space as well as the digits, or `[^0-9]` matches the separator and returns every literal shift
too. Over the fifteen files it returns **36** lines, **24** code and 12 comment or docstring, and
those 24 are exactly the table above. No `>>>` occurs anywhere in the set.

### 7.7 The LLVM-name → `soft_*` mapping is NOT identity, and a wrong pick is K9's trap

Measured off `softfloat.jl`'s `export` list and the `fround.jl`/`fmin.jl` docstrings:

| Symbol on the link surface | `soft_*` | Why it is not the obvious name |
|---|---|---|
| `rint`, `nearbyint` | **`soft_round`** — *"IEEE 754 `roundToIntegralTiesToEven`"* | there is **no `soft_rint`**; §3.3 named the right file and implied a function that does not exist |
| `round` | **`soft_round_away`** — *"`roundToIntegralTiesToAway` … `llvm.round`"* | Julia's `round` is ties-to-even; C's is ties-away |
| `fmin`, `fmax` (`llvm.minnum`/`maxnum`) | **`soft_fmin`/`soft_fmax`** — *"minNum … NaN-absorbing"* | `soft_fminimum`/`soft_fmaximum` in the SAME file are NaN-**propagating** (`Base.min`) |
| `floor` `ceil` `trunc` `sqrt` | `soft_floor` `soft_ceil` `soft_trunc` `soft_fsqrt` | identity |
| `fcmp` × 14 | 10 exported, `ogt`/`oge`/`ugt`/`uge` as operand swaps | K9's shape, §3.3 |
| `uitofp` | `zext` to 64 then `soft_sitofp` | upstream's own routing |

`fmin(NaN₁, NaN₂)` returns a **canonical** NaN upstream where C returns one of the payloads — one
more cell for §7.4's table. **Only L1 against a reference NOT derived from the kernel sees a wrong
row** (Step 13's `uge`-meaning-`ule` measurement stands), so every row above gets an anchor case
that distinguishes it from its neighbour.

### 7.8 The transcendentals are OUT on ABI grounds and arrive through the port anyway

The 27 exported `soft_exp`/`soft_log`/`soft_sin`/… are in the snapshot, and §1a's original ground
for leaving them out — *"the corpus calls none of them"* — was true at the SYMBOL level and
misleading: **50** `tests/e2e/slice_branchless_*.c` fixtures call `cos`, `exp2`, `atan2`, `acosh`,
… in their C source. They reach us with no symbol of their own because **CQ_lang's Rule 18
compiles a `math.h` function by SOURCE INJECTION** — an openlibm-style body inlined before its
lowering pass, so the pass sees stock opcodes — and names the per-function `cq_template_<fn>` path
*"fiction"*. Measured on `slice_branchless_exp2.expected.log`: **12 `fma`, 4 `fmul`, 2 `fadd`, 2
`fcmp_olt`, 2 `fptosi`, 2 `sitofp`, 2 `fneg`, 2 `bitcast`, 2 `shl`, 2 `zext`, 2 `add`**, all at
`f64`, and no `exp2` anywhere. So `exp2(x)` in a CQ program runs on our gates the moment §5's
kernels exist, at CQ_lang's injected algorithm — and those fixtures are already inside §3.4's 100,
among the 76 that need `fma` vendored.

**A Bennett-backed single-opcode `exp` is a CQ_lang decision** — a table row plus a recognizer its
own rule forbids — filed there if ever wanted, not here. Two reasons it is not a free win even
then: only `soft_exp`/`soft_exp2` are bit-exact (musl-derived); `soft_log` is ≤1 ulp and
`log2`/`log10` ≤2 ulp against Julia (`flog.jl:409`), and the host libm is a third implementation,
so condition 2's bit-exact oracle would be red, correctly, and the family would need R10's
declared-approximation tier. And `soft_exp` is itself a port of musl `exp.c` (`fexp.jl:1-7`), so
the injected body and Bennett's construction are cousins of the same size — one injected `exp2`
is a dozen `fma`s, on the order of 3M gates before Rule 2 doubles it.

### 7.9 Three more symbols stay aborts at `f64`, each on liveness

- **`lrint`/`llrint`** — the whole of `libm_table.yaml` (12 symbols, `f<W> → i64`). No
  `soft_lrint` upstream and **zero** corpus calls; composable as `soft_round` then `soft_fptosi`
  the day a caller appears.
- **`fpext`/`fptrunc`** — need an `f32` rail, which cannot exist (`cqrt_alloc_f32` aborts).
- **`frem`** — §3.3, unchanged.
- **`uitofp` from `i64` only** (decided 2026-09-18, bead `9ve.34`) — upstream has no `soft_uitofp`;
  `extract/instructions.jl:7666-7677` routes `UIToFP` to `soft_sitofp`, zext-widening narrower sources
  (correct) and passing an `i64` source straight through, so `u ≥ 2^63` comes back NEGATIVE. Porting it
  verbatim is a known miscompile on half the input space (NORTH_STAR condition 2) and correcting it is a
  re-derivation (Rule 1), so the `i64 → f64` symbols stay loud aborts citing that bead and the defect is
  reported upstream; `i1`/`i8`/`i16`/`i32` sources port on the zext path.

### 7.10 What v1 owes the port before the first kernel: three step exports

Only M14 (`cq_sub_step`), M15 (`cq_addacc_step`), M16 (`cq_ult_step`), M17 (`cq_mux_step`), M19
(`cq_divrem_step`) and M29 (`cq_qtree_step`) export a step block (measured over
`src/kernels/*.h`). `fadd` needs the **variable-shift barrel** (M12) and **`eq`/`ne`** (M16
exports `ult` only); `fmul` needs **`cq_mul_step`** (M18 exports the count and not the step).
Bitwise ops are one gate per bit and are emitted directly in the fp step function, as K12 emits
its two CNOTs; casts and constant shifts are wiring. §5's *"M33/M34/M35 must export compute
halves the way M14/M16/M17 do"* assumed these three existed. They are the first three beads.

**LANDED 2026-09-18 (Wave 1, beads `9ve.1`/`9ve.2`/`9ve.3`)**: `cq_eq_block`/`cq_eq_steps`/`cq_eq_step`/`cq_eq_flag`
(M16, the flag is `a != b`), `cq_barrel_block`/`cq_barrel_region`/`cq_barrel_steps(W, dir)`/`cq_barrel_step`/
`cq_barrel_result` (M12, the count takes the direction because `ashr` has `W` shuffle slots per stage where
`shl`/`lshr` have `W − 2^k`), and `cq_mul_block`/`cq_mul_region`/`cq_mul_step`/`cq_mul_product` (M18). All
three keep every L4 golden byte-identical. **Two more turned up in K15.md's block table the same day and are
owed before `fadd`/`fpround`: an out-of-place `add` block (M14 exports `sub` only) and an `slt` block (M16
exports `ult` and now `eq`) — `9ve.30` / `9ve.31`.**

### 7.11 Signatures: no new contract

`fadd`/`fsub`/`fmul`/`fdiv` are Rule 7's canonical two-source, one-width kernel at `W = 64`.
`fcmp` is K9's shape (`dst` one bit, `W` the operand width, the adapter passing `sh->w[0]`). `fma`
declares three sources exactly as the mux does, reached through `cq_kd_spec`'s `call` adapter
with `.kernel` NULL. Conversions are M13's two-width shape. `fneg`/`fabs`/`sqrt`/rounding are
unary, one width. Nothing widens `cq_kernel_fn`.

### 7.12 L1's fp anchors — WRITTEN (answers §6.5; bead `hkg` keeps the sampler change)

The budget stays `cq_kd_samples()` = 32 (§3.4's argument survives). The anchors, **forced into
every draw** rather than pooled, per kernel: **±0** (both signs, and `+0 + −0 = +0`); **±Inf**;
**the default NaN** and **a payload NaN in each operand position, both orders** (§7.4's
first-operand rule); **an sNaN against a qNaN**; **the largest and the smallest subnormal** and
**the smallest normal** (the subnormal→normal carry in round-and-pack); **a tie-to-even case in
each direction** (round bit set, sticky clear); **overflow to Inf**; **underflow to a subnormal**;
and for `fcmp`, **one unordered pair per predicate**. Per kernel the list is refined against the
source's own special-case predicates (`a_nan`, `a_inf`, `a_zero`, `swap`, `d ≥ 56`, …): each named
predicate gets an anchor that makes it true, because each is a branch of the `ifelse` tree that a
random draw of 32 reaches with no guarantee.

### 7.13 Suite cost, stated so nobody reads it as a regression

Upstream's `fadd` is 63,058 gates; Rule 2 makes ours ~126k per case; `fmul` ~300k; `fma` ~500k.
At 32 samples through a recording mock sink, one fp kernel adds roughly one `divrem` pair's worth
of wall clock to each configuration (`bd 97s`'s shape, which already says timings spread 4–5×
between runs). Not a reason to cut the budget.

### 7.14 The integer intrinsics ARE portable, and they are a v1-shaped bead, not fp

§6.1 exposed `ctlz`/`ctpop`/`cttz`/`bswap`/`bitreverse`/`fshl`/`fshr`/`abs`/`smin`/`smax`/
`umin`/`umax`/`*_sat` — the integer half of `intrinsic_table.yaml`. Bennett has **no**
`lower_ctlz!`, but it expands `llvm.ctpop`, `llvm.ctlz`, `llvm.bswap` and `llvm.fshl` in its
extractor (`third_party/bennett/src/extract/instructions.jl:4826-4948`), so Rule 1 is satisfiable
there too. The six integer-only fixtures blocked today (§6.1 item 1) are its acceptance
population.

### 7.15 Order of work

1. Commit the scoping (this file, D26's amendment, D27, lab-report entries 11–13,
   `tools/bitlevel/`).
2. §7.10's three step exports; §7.4's host probe; `hkg`'s sampler.
3. M31 `fpfield`, M32 `fpround`.
4. M36 `fcmp` — the smallest kernel, and the one that exercises the anchors and §7.7's table
   first.
5. M33 `fadd`/`fsub`, M34 `fmul`, M37 `fconv`.
6. M39 `fma` **with** §6.1's vendoring in the same commit (R16); then M38 `fmisc`, whose symbols
   are out-of-grid too.
7. M35 `fdiv`, M40 `fsqrt`.
8. §7.14's integer intrinsics, on their own bead.
