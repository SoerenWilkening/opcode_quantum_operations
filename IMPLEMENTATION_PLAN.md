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

The PRD leaves four mechanisms underspecified in ways that decide whether kernels fit
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

void cq_sandwich(cq_ctx *ctx, cq_scratch *scr,
                 cq_step_fn compute, int n_compute,
                 cq_step_fn copyout, int n_copyout,
                 void *env);
/*  0. no nested sandwich; take the region
 *  1. pre-materialise every bit of `scr`                    <- I6(b)
 *  2. ARM the I6 extent; for s in [0, n_compute):  compute(env, s)
 *  3. DISARM;             for s in [0, n_copyout):  copyout(env, s)
 *  4. RE-ARM;             for s in (n_compute, 0]:  compute(env, s)
 *  5. release `scr`'s qubits                       <- reversal is structural
 */
```

> **This prototype used to omit `scr`, and it could not do its job.** §0.2 requires the
> driver to pre-materialise the whole scratch region at step 0, which it cannot do for a
> region it is never handed. Corrected 2026-08-15 when `ckd.14` was resolved; M09 must be
> written against *this* signature.

**The step granularity contract: ONE GATE PER STEP.** This is `ckd.14(a)`, and it is
forced rather than chosen. The driver re-calls `compute(env, s)` with the **same**
argument on the reverse pass, so a step undoes itself only if it is an **involution** —
and a multi-gate block generally is not. Two independent worked witnesses, both in the
ported construction specs:

- `K06.md:566-586` — re-running the 5-gate ripple-carry block from its post-state leaves
  `c_{i+1} = c·(a ⊕ b ⊕ 1)`, i.e. **dirty whenever `c_i = 1` and `a_i = b_i`**. The correct
  reverse is `g5,g4,g3,g2,g1`, which one-gate-per-step makes the driver's index reversal
  *be*.
- `K10.md:153-171` — the natural 4-gate mux block leaves `r = c·(t ⊕ f)` after two
  applications. (K01's 2-gate block *is* self-inverse — two commuting CXs into one target
  — which is why the trap does not show up there.)

I6(b) does **not** rescue a multi-gate step: pre-materialisation fixes *which* gates a step
emits and says nothing about their *order*. If M09 ever grows a multi-gate step API it must
reverse gate order *within* the step too — and `CQ_ZERO_BY_PALINDROME` below becomes
unfounded while every test stays green.

**The scratch shadow rule: `cq_sandwich` contains NO shadow call.** This is `ckd.14(b)`,
and it resolves K06 §5 D3 against K11 §scratch by finding that both readings were
mis-framed. K06 was right that cleanliness must be established **structurally** and that a
literal Rule-6 shadow check hard-errors on every sandwich kernel; it was wrong that the
driver "asserts and resets the scratch shadow". K11 was right that the driver performs no
shadow reset; it was wrong to conclude the free must therefore abort. What the driver
actually asserts are **its own premises** — no nested sandwich, every scratch bit
`CQ_BIT_ZERO` on entry and `CQ_BIT_Q` after step 1, and an order-sensitive region checksum
unchanged across each half. The shadow entries are retired **as a consequence** of the
qubits going back to the pool, through the same shared path `cq_reg_free` uses. See PRD §10
for the certificate itself.

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

### 0.4 A composite kernel calls another kernel's STEP FUNCTION — the Layer-3 export rule

**Decided 2026-08-16; resolves the open (M19/M20) half of `bd 4tt`.** Three of the twelve
kernels are built out of another kernel's construction, and the rule they follow was set by
precedent at Steps 14 and 16 rather than by this plan. It is written down now, because K12
is the case where getting it wrong costs the most.

> **A kernel that needs another kernel's construction inside its own compute half calls that
> module's exported, indexed STEP function — never the kernel, and never a second
> transcription of the upstream gate list.**

**Why not the kernel.** `cq_kernel_*` is a whole sandwich, and `cq_sandwich` refuses nesting
in both configurations (`ctx->sandwich_depth != 0`), so calling one from inside another
compute half aborts before allocating anything. That refusal is right and stays: an inner
region's release would break the outer palindrome, which is what `CQ_ZERO_BY_PALINDROME`
rests on (PRD §10).

**Why not a second transcription.** Rule 1 says port once. Every re-typing of `lower_mux!`
or `lower_sub!` is a fresh chance to put the Toffoli before the two CNOTs that build its
control — the exact ordering hazard `ckd.14(a)` is about, and one that L1 cannot see.

**The shape, already shipped twice.** A block struct naming the operands, a `_steps(W)` count,
and a one-gate `_step(ctx, block, u)`:

| exporter | export | consumer | since |
|---|---|---|---|
| **M17** `mux` | `cq_mux_block`, `CQ_MUX_STEPS_PER_BIT`, `cq_mux_step` | M12's barrel (`L` copies) | Step 14 |
| **M15** `addacc` | `cq_addacc_block`, `cq_addacc_steps`, `cq_addacc_step`, `cq_addacc_check` | M18's multiplier (`W` copies) | Step 15 |
| **M14** `add` | `cq_sub_block`, `cq_sub_steps`, `cq_sub_step` — `k7_compute` and `adder_env`, today file-static | **M19** (`W` copies) | **Step 17** |
| **M16** `cmp` | `cq_ult_block`, `cq_ult_steps`, `cq_ult_step` — `ult_compute` and the `ult` half of `cmp_env`, today file-static | **M19** (`W` copies) | **Step 17** |

The consumer maps a contiguous run of *its own* step indices onto the block's index, exactly
as M12 and M18 already do. The exporter keeps its own `cq_kernel_*` entry point unchanged;
the export is additive, and `cq_kernel_fn` is **not** touched (Rule 7 — it stays arity-2).

**Four obligations on the exporter**, all of which M15's and M17's headers already discharge
and which M14's and M16's must:

1. **One gate per step.** Non-negotiable (`ckd.14(a)`): the driver replays indices, so a step
   must be an involution.
2. **Every target is a bit the CALLER owns**, inside the caller's one contiguous scratch
   region. The block struct carries pointers; it allocates nothing. (M15 states this as
   "the ancilla is supplied by the caller, not allocated here", and the reason is I6(b) —
   an allocation mid-compute-half is a bit step 0 did not pre-materialise.)
3. **Sub-array operands are the sanctioned calling shape.** A consumer hands the block
   `cq_scratch_span` views of its own region, and may hand it a view that *overlaps* a
   register an earlier step wrote, provided the overlap is read-only. K12 does exactly this:
   its `r_in[t]` view aliases `rnext[t−1]` (K12.md §2.1a). Guards therefore compare **ranges**,
   never base pointers (`kernels/kernel.h`).
4. **The block's operand bindings are the consumer's business.** M16's `ult` block compares
   `ua` against `ub`; K12 binds `ua` to a scratch view rather than to a source operand, which
   is legal precisely because a block never materialises a control.

**The cross-check this buys, and it is the real reason for the rule.** A consumer's L4 golden
can be asserted as `W × (measured cost of each block at this width)` rather than against a
written-down closed form — the assertion that survives `CQOPS_UPDATE_GOLDENS=1` and the only
durable detector for the "shorten the inner loop" mutant that left K11's entire L1/L2/L3/L5
sweep green. K12.md §3.0 carries the worked identity. **Build it before the kernel.**

**What is NOT permitted:** widening `cq_kernel_fn`; an `_unc` or `_controlled` variant of a
step function (Rules 7 and 9); an exporter that allocates; or a consumer that copies the
exporter's gate list "to avoid the dependency".

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
> `16W+8` = **136**, K12 `udiv` `34W²+5W` = **2216**, K11 `13W²−8W` = **768**.
> Qubits: K06 `2W`, K07 `3W`, K09 `ult` `3W+1`, K11 `W²+2W`, K12 `8W²+4W−1` (**543** at W=8).
>
> > **⚠ GAP 1's K12 PAIR WAS INCONSISTENT AND IS NOW CORRECTED [2026-08-16, PRD §15 D9].**
> > This row used to record `34W²+13W` (2280) with `8W²+5W` (552) — the gate count of one
> > layout and the qubit count of another. They could not both hold: keeping K12's `r_0` upper
> > bits as real scratch is exactly what costs those `W−1` qubits, and the gate/qubit pair has
> > to move together. Both halves are now the **measured** pair for the decided design
> > (D9(b)+(d)): `34W²+5W` and `8W²+4W−1`, confirmed by running the construction through the
> > real emitter in both configurations at `W ∈ {1,2,3,4,8,16,32,64,128}`. The intermediate
> > `34W²+13W` / `8W²+6W−1` pair — which this line carried the first half of — is consistent
> > with itself and also reproduces exactly; it is what D9(b) retired. `K12.md` §3 carries all
> > three generations so a golden diff stays attributable.
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
| `bitkinds.[ch]` | 110 | Build a register from `(value, quantum-mask)`; enumerate the mask **pairs** used by L1 |
| `poolcheck.[ch]` | 80 | Snapshot/diff the qubit pool; the automatic L2 and L3 assertions |
| `kerneldrv.[ch]` | **249 landed** | **Added at Step 10.** The shared Phase-B driver §4 below assumes without naming — see the note. Split seam: **the sweep shapes move to `kernelsweep.c`, keeping `cq_kd_case`'s four levels in one file** — trigger at 260 in `kerneldrv.c` |
| `kernelsweep.c` | **128 landed** | **Split from `kerneldrv.c` at Step 11** on this table's own recorded seam: the sweep SHAPES here, the four LEVELS next door |
| `goldens.[ch]` | **221 landed** | **Added at Step 10.** L4's counts as an on-disk artifact: load, check, regenerate, and the risk-R3 commit cross-check. Split seam: **parse/write ↔ check**, i.e. the file format leaves and `cq_gold_check` stays — trigger at 260 in `goldens.c` |

> **The last two are beyond this table's original five, like `death.[ch]` before them, and
> the gap was structural rather than accidental.** §4's Phase B gate says the four levels are
> "applied automatically by the **shared kernel driver** rather than written per kernel", and
> §4 Step 20 says re-running every kernel under control "costs one parameter in the kernel
> test driver, not twelve new suites" — but no file, no budget and no module row was ever
> assigned to that driver, and the same was true of `tests/goldens/`. Both are load-bearing
> for eleven kernel modules, so they are named here now. Neither is exempt from Rule 12; §2.3
> covers `tests/` explicitly.
>
> **A packed `uint64_t` in `refmodel` and `bitkinds` is not an I5 violation.** I5 forbids a
> packed scalar in the *representation* — in a register, a peephole or a kernel — because it
> caps width at 64 and forces a two-word split plus a 128-bit variant of every fold. These
> two are the *reference* and the test-side *specification*: PRD §11 defines L1 as "compare
> against the C operator", which needs a C scalar by construction, and this table's own
> `bitkinds` row prescribes "(value, quantum-mask)". What they build is an ordinary `cq_bit`
> array, tested for I1 like any other. The cap it imposes is real and bounded — W ≤ 64, which
> covers every width L1 tests — and an i80 or i128 kernel would need a two-word reference.
> `cq_ref_mask` aborts rather than truncating if asked for one.

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

### Step 6 status (2026-08-14) — **the fold table is in, 159/159**

M05 `src/emit.[ch]` (122/190) plus `src/ctx.[ch]`, the shared context aggregate. 24
ctest tests green under **both** configurations and under ASan + UBSan; `make lint`
green. **19 mutations of `emit.c`, 19 killed.**

The suite is 155 exhaustive cases — 5 X + 25 CX + 125 CCX over the five operand kinds
— each pinning gate stream, qubits allocated, resulting bit-kind and resulting shadow,
plus the 4 distinctness deaths in `test_emit_death.c`. Three layers of defence against
the oracle and the emitter sharing a misreading of §3: the oracle follows the PRD's own
rows and reductions; the 25 CX rows are **also** pinned as a literal hand-written table
that never runs the oracle; and `check_universal` asserts five things that hold whatever
the fold table says (I1 on the target, controls never modified, ≤ 2 gates, at most one
qubit and only for a materialised target, and no qubit taken when no gate was emitted).

**Deviation 6 — `src/ctx.[ch]` is not in §3's module map.** PRD §3's emitter signature
takes a `cq_ctx *` and M06–M09 all need the same aggregate, so it has to exist by
Step 6. Kept to a struct and three functions with no policy of its own. It is **not**
in the public header yet, despite PRD §14 listing "context" there: the first consumer
of a public context is the shim at M26 (Step 23), and freezing an ABI four steps early
would be guessing.

**Deviation 7 — `cq_materialise` emits its `X` straight to the sink**, not through
`cq_emit_x`. Once M06 makes `cq_emit_x` consult `ctrl_depth` (Step 20), routing through
it would promote the materialising `X` to a `CX` and leave the fresh qubit entangled
with the control rather than in a definite state. Whether that is correct is M06's
call; the code declines to answer it by accident. Filed.

**Two corrections to the design of record, both found by this suite:**

1. **`cq_bit_coincident` had a pointer-identity clause and now does not.** It was
   redundant — two `Q` bits at one address necessarily hold the same index — and
   unsound, because it fired on one constant bit passed in both control slots, which
   PRD §3 says explicitly must never happen. `CCX(o, o, t)` is a legal fold to `X(t)`
   and the clause aborted on it.
2. **PRD §3's "on qubit index (and pointer identity)" contradicted its own next
   clause** and has been corrected to "on qubit index alone", with the finding recorded
   inline.

One test-quality note worth carrying to Step 7. Deleting M05's distinctness check
**still aborted**, because M02's `cq_shadow_cx` carries its own `c != t` assert one
layer down — defence in depth made the mutation survive. What M05 must do is reject
*before* anything reaches the sink, so the death cases now use a sink that **disarms
the abort window on any emission**: a gate that escapes turns the later abort into
exit 4 and fails the case. Expect the same shape wherever two layers guard one
condition.

### Steps 2–5 status (2026-08-14) — **Phase A Layer 0 complete, with five deviations**

M01 `src/bit.h` (64/70, header-only — no translation unit), M02 `src/shadow.[ch]`
(114/130), M03 `src/qubits.[ch]` (130/150), M04 `src/sink.[ch]` **(108/90)** plus
`tests/support/mock_sink.[ch]` **(139/120)**. Every gate met under **both**
configurations and additionally under a full ASan + UBSan build with Homebrew clang;
18 ctest tests; `make lint` green. Each suite was **mutation-tested** rather than
assumed to bite — 8, 10, 15 and 12 mutations respectively, all killed.

**Two §3 budgets overshot, both far under the 300 hard limit and neither worth a
split.** M04 carries a name registry so `cq_sink_register` / `cq_sink_by_name` resolve
`CQOPS_SINK` inside M04; the alternative — M04 returning only the requested *name* and
Step 9 doing the mapping — would be smaller but would reduce Step 5's "env-var default
selection works" gate to "we can read an environment variable". `mock_sink`'s overshoot
is `cq_mock_dump`, which is the "dump on failure" half of §2.2's own spec for it.

**Deviation 1 — `cq_qubits_release` takes the evidence as a parameter.** §4's Step 4 row
says "releasing a qubit whose shadow is not known-0 is a hard error", which reads as
though M03 performs the lookup. It does not: the signature is
`cq_qubits_release(pool, q, int proven_zero)` and the caller passes
`cq_shadow_known_zero(sh, q)`. This is the only shape satisfying both of this plan's own
constraints at once — §3 puts M03 in Layer 0 with **no internal dependencies**, so it
cannot include `shadow.h`; and `ckd.17` (filed after this plan was written) establishes
that the two-bit shadow **cannot** be the free-time oracle, so hard-wiring the lookup
would bake in exactly the mechanism that bead says fails. The parameter lets ckd.17's
structural certificate be substituted without touching M03, and forces every caller to
name its evidence where a grep finds it.

**Deviation 2 — M02 ships no un-poison, deliberately.** Nothing in `shadow.h` can return
an entry to determinate; entries are born known-0 by `cq_shadow_ensure` and that is the
only route to clean. A convenience setter would have settled `ckd.17` by accident, in
the one direction that launders a dirty rail into a provably-clean one. ckd.17 carries a
note saying the sanctioned write goes in `shadow.h` when it is resolved.

**Deviation 3 — a sixth test-support file, `tests/support/death.[ch]`**, beyond §2.2's
list of five, plus `add_cqops_death_test()` in `cmake/CqopsTest.cmake`. Forced by a
measured fact: CTest's `WILL_FAIL` inverts a non-zero *exit code* and does **not**
invert a crash, so it cannot express `abort()` — and every hard error in this project is
one. Steps 4 and 6 need ten deaths between them. See CLAUDE.md *Build & Test*.

**Deviation 4 — tests include internal headers directly.** `tests/CMakeLists.txt` adds
`src/` to `cqops_test_support`'s PUBLIC include path, since §2.1 is one binary per
*module* and the modules are internal. `src/` stays PRIVATE on the `cqops` target.

**Deviation 5 — M04 is split across the public and internal headers.** PRD §14 puts
"context, sink, config" in `include/cqops/cqops.h`, and §8 spells `cqops_set_sink()` as
public — so the `cq_sink` vtable and that setter live in the public header (verbatim
from §8), while `src/sink.h` carries the internal half: the six dispatch helpers and
the registry. Selection is **process-wide**, not per-context, because §8 gives
`cqops_set_sink()` no context argument and because the environment default has to work
with no call into the library at all. It is resolved on every `cq_sink_active()` rather
than cached, so there is no stale-selection state; when `cq_ctx` lands at Step 7 it
should borrow the active sink **once at construction**, not re-resolve per gate.

Dispatch goes through `cq_sink_x(...)` and friends rather than `s->x(s->user, q)` at the
call site for exactly one reason: a NULL vtable entry is caught and named instead of
jumping through a null pointer. §8's qec sink "stubs" `ry`/`rz`, and a stub is a no-op
function, not a hole — silently dropping gates is the failure that leaves every suite
downstream green while verifying nothing.

One finding worth carrying forward, from mutation testing at Step 4: a growable array's
freshly-`realloc`'d tail is only *usually* zero, so an omitted initialiser can pass a
whole suite on allocator luck. M03 now poisons the new tail with `0xAA` under
`CQOPS_DEBUG_INVARIANTS` and validates on read. **M07's handle table and owner map
(Step 7) and M08's scratch (Step 8) have the same shape and want the same treatment.**

### Step 7 status (2026-08-14) — **M07 landed; D7 was measured and split**

`src/reg.[ch]` (54 + 229 = **283** code lines against a **180** budget), plus
`tests/test_reg.c` + `tests/test_reg_invariants.inc` + `tests/test_reg_death.c`.
**45 ctest tests green under both configurations and under a full ASan + UBSan build**
(Homebrew clang 22). `make lint` green. Twelve mutations run; ten killed outright, the
two survivors are equivalent mutants (below).

**The headline is not the module — it is that D7 stopped being a prediction.** Plan §4
scheduled the aliasing question for empirical resolution at Step 24; it was resolvable
now, from the goldens, and the answer contradicts risk R2 as written. Over all 239
goldens — 62,930 `cq_template_*` calls, 25,147 `_unc` — `out` among the sources is
**0**, and two sources aliasing each other is **599**, of which **10** are on v1's
integer surface (`cq_template_mul_i32(h10, h10)` among them; CQ_lang ships
`slice_select_rail_alias_cond.expected.log`). So "assert loud from Step 7" would have
aborted at Step 24 on shipped fixtures. R2 and PRD §15 are amended to D7a/D7b; the
defensive copy is now **required** at M26, filed as `bd -493`.

**Deviations, all recorded rather than hidden:**

1. **The budget overshot by 57%,** and the module landed whole. Four causes §3's 180 did
   not anticipate: a three-state slot (§10 needs live/tombstone/measured), the
   `INT32_MAX` handle guard, the D7a/D7b split, and the two-pass free. The recorded seam
   — *table ↔ invariant checking* — is unused and stays available at a 240-line trigger
   on `reg.c`. `tests/test_reg.c` **did** hit the 300 guard and split along that same
   line into `tests/test_reg_invariants.inc`.
2. **`cq_ctx` gained a struct tag.** It was an anonymous typedef; `reg.h` needs to name
   a `cq_ctx *` without including `ctx.h`, so `reg.h` now carries the single
   `typedef struct cq_ctx cq_ctx;` and `ctx.h` spells `struct cq_ctx { ... };`. The
   graph stays acyclic: `bit.h ← reg.h ← ctx.h ← emit.h`.
3. **`cq_reg_free` takes the zero-proof as a function pointer,** `int (*)(const cq_ctx *,
   int32_t h, uint32_t q)`. This is M03's deviation 1 one level up, and it is a
   **refusal** to settle `ckd.17` rather than an answer: passing both `h` and `q` leaves
   that bead's own "per qubit or per register?" open in both directions. The library
   ships **no** proof; `NULL` fails loud. The only proof in the tree is a `static` in the
   two test files, named `proof_shadow_pre_kernel_only`.
4. **Rule 6's literal wording was aborting on valid input** and has been rescoped in both
   CLAUDE.md and PRD §10 to the rail's *qubit-carrying* bits. "Every bit is `BIT_ZERO` or
   a known-zero qubit" rejects a `CQ_BIT_ONE`, i.e. `int x = 5;` going out of scope — and
   makes L5's zero-cost classical path unreachable. It does **not** let `ckd.18` through.
5. **A death test can now assert WHICH LAYER aborted.** Measured here: deleting M07's
   `cq_reg_clean` call left all 24 reg tests green, because M03's own
   `if (!proven_zero)` fired one layer down and the rail was merely half-returned to the
   pool — precisely the trap §0 recorded when deleting M05's distinctness check still
   aborted. The fix is one CTest property, `FAIL_REGULAR_EXPRESSION` on M03's message,
   which unlike `PASS_REGULAR_EXPRESSION` does **not** displace the exit-code check.
   **Use this wherever two layers guard one condition** — M08's scratch release at Step 8
   is the next instance.

**The two surviving mutants are equivalent, not gaps.** (i) `0xAA` → `0x00` tail fill:
no slot state is numbered 0, so any non-`{1,2,3}` fill is an equally good poison — the
numbering subsumes half the poison's job. (ii) forwarding `proof(...)` → literal `1` in
the release loop: pass 1 has already established every answer is 1, so the two are
behaviourally identical *unless* the clean-check is also removed, and that compound is
killed.

**A 46th test exists because an adversarial review found the audit's filter unpinned.**
The `cq_reg_audit` sweep must skip **only** tombstones — a MEASURED rail keeps its qubits
for the life of the program (§7 makes measurement terminal) and is therefore the one rail
kind that must stay under the map's eye forever, while a tombstone's `bits` array is gone
and its indices are back on the free list. Every `i2_*` death case used two **LIVE** rails,
so narrowing the filter from `skip DEAD` to `skip everything non-LIVE` left all 45 tests
green in **both** configurations — the project's only I2 detector going blind to measured
rails, undetectably. Fixed by a pair that makes the filter observable:
`the_audit_sweeps_a_measured_rail_without_a_false_positive` (ordinary) and
`i2_measured_rail_shares_a_qubit_with_a_live_one` (Debug-only death), the latter verified
by hand to fail under the narrowed filter and pass under the correct one.

> **The general lesson, and it is the same one twice in one step.** A check that two
> layers both perform, or that one state can reach by two routes, is untested until some
> case *distinguishes* them. M07 hit this in the free path (M03's guard masking M07's,
> §deviation 5) and again in the audit's state filter. **Before Step 8's scratch release,
> ask of every new guard: which single case goes red if this exact line is deleted?**

**One hazard found and deliberately NOT fixed here**, appended to `ckd.17`: nothing
un-poisons the shadow entry of a released qubit, and a **reused** index keeps its old
entry (`cq_ctx_fresh_qubit` only ensures up to `minted`, and `cq_shadow_ensure` returns
early). Safe today only because the sole available evidence *is* the shadow; the moment
ckd.17's structural certificate allows a free under a poisoned entry, the pool hands the
next `cq_materialise` a genuinely `|0⟩` qubit carrying a stale determinate entry.

### Step 8 status (2026-08-15) — **M08 + M09 landed; the `ckd.17a` certificate is code**

`src/scratch.[ch]` (13 + 43 = **56** against a 90 budget) and `src/sandwich.[ch]`
(10 + 105 = **115** against 110 — 5 over, no split), plus `cq_shadow_retire` in M02,
`cq_ctx_release_qubit` and a `sandwich_depth` field in `cq_ctx`, and
`cq_mock_is_palindrome` in the test support library. `cq_reg_free` was rewired through
the new joint. **67 ctest tests green under both configurations and under a full
ASan + UBSan build** (Homebrew clang). `make lint` green; `tests/test_sandwich.c` hit the
300-line guard and split into `tests/test_sandwich_certificate.inc` along the
driver-mechanics ↔ certificate seam.

**The headline is not the driver — it is that the same guard called three times was one
mutation away from untested.** 32 mutations were run in two rounds. Round 1 killed 22 of
26; **three of the four survivors were the three `SW_VERIFY` calls**, each individually
deletable because a later call caught what it would have. Round 2 added one mutation per
`sw_arm` call and found the identical shape there. This is the Step 6 and Step 7 lesson
for the third and fourth time, and it now has a general form worth stating:

> **A guard is untested if a LATER COPY OF THE SAME GUARD would catch its cases.** Two
> layers guarding one condition (Steps 6, 7) is the special case; N call sites of one
> guard is the general one. The fix is `FAIL_REGULAR_EXPRESSION` at a finer grain — name
> the *call site*, not just the module — plus a case that reaches the last site, which
> has nothing after it to lean on. **A step function that misbehaves only on the reverse
> pass is the construction that reaches it**, and it is not contrived: it is exactly the
> asymmetry the reverse-half arming exists to catch.

**Deviations from §0.1, all deliberate:**

1. **The fingerprint is checked after the COPYOUT loop too,** which §0.1's "unchanged
   across each half" does not require. It must be: the I6(a) extent is disarmed during
   copyout, so the fingerprint is the only thing that can see a copyout step reaching
   back into the region.
2. **There is no second all-`CQ_BIT_Q` sweep on entry to the reverse half,** which §0.2's
   third enforcement bullet implies. The baseline is taken all-`Q` and an unchanged
   fingerprint carries that forward, so no case distinguishes the second sweep — and by
   the rule above, that makes it a check that survives its own deletion.
3. **`sandwich_depth` is on `cq_ctx` and is NOT Debug-gated,** unlike the extent beside
   it. The nesting premise is `O(1)`, and it covers a case the extent cannot: a sandwich
   attempted from a copyout step, where the extent is deliberately disarmed.
4. **The epilogue carries a both-configuration `cq_bit_is_qubit` check** before
   `cq_bit_qindex`. In Release the fingerprint is compiled out, and a constant's
   canonical `q == 0` would otherwise release someone else's qubit. Its distinguishing
   case passes in Debug and fails in Release — a concrete instance of why Rule 17 makes
   both configurations mandatory rather than advisory.
5. **The region is released in REVERSE index order.** The free list is LIFO (D4), so
   pushing `n-1 .. 0` leaves index 0 on top and the next sandwich re-acquires the same
   indices in the same ascending order. Two identical kernels therefore emit an identical
   stream, which is what keeps an L6 diff quiet and an L4 golden reproducible.

**The one surviving mutant is equivalent in isolation, and its pair proves the point.**
Deleting `cq_scratch_alloc`'s Debug `0xAA` poison changes nothing on its own. But
deleting the *initialiser loop* was killed in Debug **only because the poison was there**
— and passed in Release, where there is no poison and `malloc` happened to return zeros.
An all-zero `cq_bit` is a valid `CQ_BIT_ZERO`, so this is M03's Step 4 allocator-luck
trap in its sharpest form.

**Two things Step 8 changes for later steps.** `cq_reg_free` now retires shadow entries,
which closes the hazard the Step 7 note above left open. And `CQ_ZERO_BY_PALINDROME` is
a literal `1`, so M03's `proven_zero` guard can never fire for a sandwich: the
Release-configuration detector of a non-cancelling compute half is **M02's**
`cq_shadow_retire`, whose reach PRD §10 bounds exactly — complete across the
rotation-free surface (Steps 10–17), inert once a rail is rotation-tainted.

---

### Step 11 status (2026-08-15) — **M11 + M13 landed; `ckd.16` resolved as D8**

**87 ctest tests green in both configurations; `make lint` OK.** M11 is **63 lines** and
M13 **42**, against 90 each.

**The blocker was resolved in the source document first, and correcting the bead's own
facts is what decided it.** `ckd.16` framed the conflict as saturate-versus-mask; the
barrel **also saturates** (each stage zero-fills, and saturating shifts compose), so the
variable path is `sat_shift(a, k mod 2^S)` and the disagreement is a **power-of-two
artefact** — at i80 the two paths agree throughout `[80,128)`, which is the opposite of
what the bead reached for. Full statement in PRD §15 D8; K04.md §5(e) carries a
correction box. Measured over the 239 goldens: 916 shift calls, 895 with a constant
amount, **every one in `[0,W)`**, so D8 defines behaviour CQ_lang has never emitted, and
the cheap side was the right side. **In M11 the mask half is structural, not arithmetic**:
the kernel reads only the low `S` bits of the amount, exactly as the barrel reads only
`b[0..S-1]` as MUX controls.

**Two driver generalisations, both forced by kernels rather than by taste.** K4 needs an
operand CONSTRAINED CLASSICAL (a quantum amount is M12's, not M11's); K5 is UNARY WITH TWO
WIDTHS and its values run to 128 bits. Both went into `cq_kd_spec` as zero-defaulting
trailing members, so Step 10's specs compile untouched, and `cq_kernel_check_dst` grew an
N-ary form because it was **mis-sized for `F != T`** — that guard is the structural
defence against K05.md's named trunc-looks-like-a-slice aliasing.

**The verification pass found more than the code did.** 43 mutants, 27 killed; every one
of the 22 pure-logic mutants died, and the survivors clustered in exactly two places —
guards with no death test, and assertions masked by other assertions. Seven fixes:

1. **L2's set check was masked by L3's COUNT for a whole step.** All three
   `cq_pc_live_is_exactly` calls could be deleted together and the suite stayed green,
   because the provocation moved `live` and `cq_pc_same` caught it. The test named for L2
   was passing on L3. Closed with a fault that **nets to zero** — acquire one ancilla,
   release one belonging to a source — and re-verified by re-running the same mutation.
2. **Two thirds of the L4 tuple was a tautology** in both new suites:
   `CHECK_GATES(0, cx, 0, 0, want_cx, 0)`, literal 0 against literal 0, with the
   unobserved zeros written into 159 golden rows. A regression from Step 10, caused by a
   bespoke measurement helper that returned only `cx`.
3. **`_unc` was not pinned at all** for M11 or M13 — 0 unc rows against `bitwise.counts`'
   21 — because the same helpers called the kernel once.
4. **`sext` had no independent oracle**: `cq_ref_w_sext` is a bit-for-bit transcription of
   the kernel, same bounds and same sign index, so an error made once and repeated was
   invisible to L1 (a wrong-sign-bit mutant passed all 8 cases). `cq_ref_sext`'s
   shift-free `(t ^ sign) - sign` identity was dead code with no caller; it is now the
   oracle.
5. **No death test for either module** — four hard errors between them, all deletable
   with the suite green, including cast.c's width guard, which prevents a heap overrun
   rather than a wrong answer.
6. **The overlap death case overlapped by TWO elements**, so a one-element off-by-one in
   `cq_kernel_overlap2` survived. Now minimal.
7. **i80 and i128 were absent from the shift sweep and goldens** — and i80 is the *only*
   shipped width where D8's saturating branch can fire, carrying 47% of the corpus's 909
   shift calls. Adding them needed a two-word shift reference, which now also
   cross-checks the one-word model below 64.

**~~Carried forward~~ DISCHARGED at Step 14, 2026-08-16.** The obligation was: *"M12
must match D8 exactly. A review proved a verbatim barrel port does — 0 mismatches over
402,444 model cases — but nothing in the repository makes it."* Something in the
repository now does. `m11_and_m12_agree_at_every_width_amount_and_value`
(`tests/test_kernel_shift_var_d8.inc`) drives **both modules** on the same
`(W, k, value)` — the amount classical for M11 and all-quantum for M12, which is what
stops the barrel delegating at every width except `W = 1`, where `L = 0` makes the
delegation test vacuous and the comparison is honestly M11 against itself — over every
shipped width `{1,2,3,4,5,8,16,32,64,80,128}`, every amount the
construction can distinguish below i80 and a strided set with the `[W, 2^S)` bracket
above it, and every direction. Neither module's own suite can make that assertion: each
compares its kernel against a reference applying the same reduction through the same
`cq_shift_stages`, so both can be green while disagreeing with each other about exactly
the interval `ckd.16` was filed over.

---

### Step 10 status (2026-08-15) — **M10 landed, and the shared Phase-B gate is now a thing rather than a plan**

**75 ctest tests green in both configurations; `make lint` OK.** M10 is **40 lines against a
100 budget** — the three kernels really are three loops. Everything else Step 10 cost went
into the machinery the next seven kernel steps inherit.

**The ports are transcriptions and were independently re-verified against the pinned
snapshot**, not against our own emitter: the three fenced excerpts in K01/K02/K03 §1 are
byte-for-byte `arith.jl:284-291`, `:268-272`, `:274-282` (SHA-256 matched on both sides),
each is the sole definition in the tree, the gate field orders in `gates.jl:12-22` and the
`⊻=` semantics in `simulator.jl:1-3` confirm which operand is the target, and the pinned
commit matches. `xor` = 2W CNOT, `and` = W Toffoli, `or` = 2W CNOT + W Toffoli, at every W.

**Five test-support files landed, two of them beyond §2.2's list.** `refmodel`, `bitkinds`
and `poolcheck` as budgeted; `kerneldrv` and `goldens` because §4's Phase B gate assumed
both and assigned neither (§2.2 above now names them). `harness.[ch]` grew
`cq_h_args`/`cq_h_flag` and `CQ_TEST_MAIN_ARGV`, for the one suite that has a mode as well
as a verdict.

**What the four levels became, once they had to be code.** All four rows of §4's gate were
imprecise in ways that only a running driver could expose; the corrected wording is in §4
and PRD §11, and the substance is:

1. **L1 reads a VALUE, not "the shadow".** `shadow(dst)` is undefined for a constant bit,
   and under the all-classical mask — which the same table calls L5 — *every* bit of `dst`
   is one.
2. **L2's "exactly `dst`'s qubits" is false whenever an operand is quantum.** The operative
   claim is the union form, as a SET: no index is live that no named register owns. A count
   is strictly weaker — leak one index and hand back another and the totals agree.
3. **L3 cannot compare `minted` or the free-list length.** Both are monotone. The first
   draft of `cq_pc_same` compared all three and **failed 1,276,416 cases on its first run**
   — a fair sample of what "assert the pool, never assume it" costs when the assertion is
   wrong. What replaced it is *stronger*, not weaker: name `dst`'s indices before the free
   and assert each is back on the free list, which also catches a free that released the
   wrong index.
4. **A mask is a PAIR.** Every normative sentence said "masks" in the singular, but R8's own
   mandated witness — `a` all `Q`, `b` all `ZERO` — is inexpressible that way.

**The W=8 sweep is capped, and the cap is printed by every run.** The full product is 20
mask pairs × 65,536 value pairs × 3 kernels = 3.9M cases, measured at 43 s in Debug against
a 0.8 s suite. So W ≤ 5 runs the **full cross product**, W=8 runs **every value pair** with
the mask rotating (~3,300 values per mask) plus the four corners against every mask, and
W ∈ {16,32,64} is deterministically sampled from a width-seeded xorshift. Total: **3.9 s
Debug, 0.9 s Release.** Coverage was moved, not lost — but it was moved, and a run that says
so in its own output is the only kind of cap this project allows.

**L4's goldens are DATA, and risk R3 now has teeth.** `tests/goldens/bitwise.counts` carries
the Bennett SHA on a `# bennett:` line, and `cq_gold_open` compares it against
`third_party/bennett/COMMIT` — so a re-pinned snapshot whose goldens were not regenerated is
red, which is exactly the silent invalidation R3 describes and which a header comment nobody
reads cannot catch. Note the COMMIT file is a *document*: the SHA is on its `commit:` line,
and reading its first line yields a title that would never change on a re-pin. Forward and
`unc` are pinned as separate rows, and an unvisited row is a failure — a golden that claims
a width is pinned while nothing checks it is a coverage hole reported as coverage.

**`ctest` does not forward trailing arguments, and the documented regeneration command was a
hard error.** `ctest --test-dir build-release -R kernel -- --update-goldens` fails with
`CMake Error: Unknown argument: --` and runs zero tests, on ctest 4.3.2 — the `-- <args>`
idiom belongs to `cmake --build`. Measured four ways (with `--`, without, bare, `--` first);
all four error. The mechanism is `CQOPS_UPDATE_GOLDENS=1` in the environment, which passes
through the existing `ENVIRONMENT` test property untouched; the property must **not** be
taught to set that variable, or it would pin it and make the command-line form inoperative.
`--update-goldens` still works when a test binary is run directly.

**One new hard error, in both configurations: `cq_kernel_check_dst` (D7a at the kernel
boundary).** It is not a duplicate of M07's handle-level check — a kernel is handed three
`cq_bit` arrays and is entered directly by the driver, and will be entered by M26 after
handles are resolved away. Its distinguishing case is `dst == a` with a **classical** `a`:
M05's distinctness check compares qubit indices and cannot fire on constants, so with the
guard deleted the fold table folds happily and the kernel returns a wrong answer in silence,
in *both* configurations. D7b is deliberately not checked — it is legal, and a kernel cannot
see it anyway.

**Two defects found in the K-docs while porting, neither affecting the goldens.** Bennett
runs `_fold_constants` **by default** (`Bennett.jl:146`, applied at `driver.jl:375-377`),
which falsifies the partial-constant *comparison* numbers in K01 §5.2 and K02 §5.2 — at
default options upstream also pays zero Toffoli for `x & 0x0f`. The headline formulas are
untouched, because they are pinned at all-quantum operands where the pass provably does
nothing. And every `PRD-v1.md` / plan line citation in the three K-docs is stale by 95–183
lines. Both filed.

#### The second pass — a mutation battery and three adversarial reviews

**62 mutants, 37 killed. The split is the whole finding: 22 of 24 LIBRARY mutants died,
and 21 of 25 TEST-ASSERTION mutants survived.** That asymmetry is not noise. Mutating an
assertion to always-true cannot fail on a correct library — there is nothing for it to
catch — so the survivors were not proof of weak assertions, they were proof that **nothing
in the suite had ever watched one fire**. The two library survivors were both ORDERING
changes (`xor`'s two CX swapped, `or`'s two CCX controls swapped): semantically equivalent,
invisible to L1/L2/L3 and to every count, and still a change to a stream `bitwise.c`
claims to keep in Bennett's order "so L6 trace diffs stay attributable".

**Seven fixes went in, and one of them was a measured silent miscompile in the driver:**

1. **L2 ran only after the forward call.** A reviewer built the witness: a kernel that
   acquires one ancilla and releases one qubit belonging to a *source* nets to zero, so
   `live` matches, every value is right, and **the whole suite passed green in both
   configurations** — with a source register naming an index on the free list and an
   unowned ancilla live. I2 and I3 both lies; the next `cq_materialise` hands the same
   physical qubit to unrelated data. L2 now runs three times: after the forward, after the
   uncompute, and after the free.
2. **The source-KIND check crossed the uncompute axis** — Rule 14, risk R6 by name. It is
   now `KINDS_TOO` after the forward and `VALUE_ONLY` after the uncompute. Inert today
   (nothing can rotate a source before Step 19) and load-bearing for Step 21, which is the
   step whose entire subject is that asymmetry.
3. **D7a's guard compared base pointers, and its justification was false on this module's
   own calling convention.** `cq_scratch_span` exists so a kernel can be handed sub-arrays
   of one region, and `bitwise.h` says K9 and K12 will do exactly that — so partial
   overlap is representable, and `cq_kernel_xor(ctx, &r[0], &r[2], b, 4)` passed the guard
   and returned a wrong answer with no diagnostic in **both** configurations. Now a range
   comparison through `uintptr_t`.
4. **D7b was wrong in both directions at the kernel boundary.** Measured: Debug aborted
   from M05 with a message naming the fold table rather than the alias; Release returned
   normally having emitted `ccx q0 q0 q2` — a Toffoli whose two controls are one physical
   qubit — straight to the sink. It stays legal at the HANDLE boundary, where M26's
   defensive copy is the remedy; reaching a kernel means that copy is missing, and now
   says so.
5. **The W=8 mask rotation aliased.** `p = (p+1) % np` with `np=20` and `span=256` has
   `gcd = 4`, so each mask only ever met value pairs with `vb ≡ p (mod 4)` — the
   all-quantum mask saw only odd `vb`. Now the all-quantum mask gets the full cross
   product outright and the rest draw from a seeded xorshift.
6. **The R3 remedy could not be executed.** The commit-mismatch abort fired before the
   writer on the update path, so a checking run and an update run failed identically and
   the only way to follow the instruction the message printed was to hand-edit the
   `# bennett:` line — the exact silent re-pinning the check exists to prevent. The update
   run is now exempt and adopts the snapshot's SHA from a live measurement. Verified end
   to end by faking a re-pin.
7. **`the_three_kernels_allocate_only_dst` asserted nothing about allocation** — a
   not-equal-to-one-constant test on a total `check_counts` had already pinned exactly. It
   now snapshots the pool and asserts `minted` and **`peak`** both move by exactly `W`,
   which is the "0 ancillae" claim and is a strictly different one from L2's: L2 looks
   after the call, so a kernel that took scratch and tidily gave it back passes it.

**And the assertions are now falsifiable — `tests/test_kerneldrv.c`, 8 cases.** Five
deliberately broken kernels (wrong value, leaked ancilla, materialised source, cost on the
classical path) plus direct unit tests of the poolcheck helpers, each asserting the driver
**refuses**, with a `CQ_EXPECT_CLEAN` control asserting it accepts a correct one. This is
`test_harness_negative`'s argument one level up, and it caught its own first bug: the
source-materialising fault searched only `a`, which under its mask had no constant bit, so
the provocation never provoked and the case correctly reported the assertion as vacuous.

**78 ctest tests green in both configurations, `make lint` OK. Debug is UBSan-only on this
host** (Apple clang's ASan runtime, already documented), so the ASan half of Debug coverage
was absent for the battery too — Rule 17.

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
| M07 | `reg.[ch]` — handle table, tombstones (D5), I2 owner-map **sweep**, D7a abort + D7b query | ~~180~~ **283 landed** | table ↔ invariant checking — unused; trigger at 240 in `reg.c` |
| M08 | `scratch.[ch]` — allocate/dispose the `cq_bit` **array**; dispose asserts every bit is back to `CQ_BIT_ZERO` (a **kind** check, never a shadow read). **Ships no release a kernel can call** | 90 | — |
| M09 | `sandwich.[ch]` — the §0.1 driver. **Owns the scratch qubits end to end**: pre-materialises at step 0 (I6(b)) and releases in its own epilogue. I6 extent armed for the **compute halves only** | 110 | the checksum helper moves to `scratch.c` — **never** the release, which would reopen "a kernel can call it" |

### Layer 3 — kernels *(all independent of each other; parallelisable)*

| ID | Module | Kernel | LOC | Split seam |
|---|---|---|---|---|
| M10 | `kernels/bitwise.[ch]` | K1 xor, K2 and, K3 or | **40 / 100** | — |
| M11 | `kernels/shift_const.[ch]` | K4 constant shl/lshr/ashr, **D8** | **63 / 90** | — |
| M12 | `kernels/shift_var.[ch]` | variable shifts (barrel over K10) | ~~130~~ **138 landed** | `mux block ↔ barrel schedule` — **taken**: the four-gate block is M17's `cq_mux_step`, called, not transcribed |
| M13 | `kernels/cast.[ch]` | K5 sext/zext/trunc; unary, **two widths** | **42 / 90** | — |
| M14 | `kernels/add.[ch]` | K6 add, K7 sub. **LANDED 2026-08-16 at 114 against 190**, so the seam is unused. **Step 17's export SHIPPED, at 124 body + 18 header** (plan §0.4, PRD §15 D9(e)): `k7_compute` and the sub half of `adder_env` are now `cq_sub_block` / `cq_sub_steps` / `cq_sub_step`, on M15's and M17's shape — additive, `cq_kernel_fn` untouched, and K6/K7 emit the same gates in the same order | 190 | `add.c` ↔ `sub.c` |
| M15 | `kernels/addacc.[ch]` | K8 Cuccaro in-place accumulator. **LANDED 2026-08-16 at 108 (93 body + 15 header) against 120, so no seam is owed.** Ships a `.h` because M18 consumes its block and step function, exactly as M17 does for M12 | 120 | — |
| M16 | `kernels/cmp.[ch]` | K9 eq/ult/slt + 7 derived predicates. **LANDED at 190/200 — the tightest module in the tree. Step 17's export SHIPPED and it lands at EXACTLY 200/200**: `ult_compute` and the `ult` half of `cmp_env` are now `cq_ult_block` / `cq_ult_steps` / `cq_ult_step`. **The seam was NOT taken** — the export cost 10 lines, not 30, because `cq_ult_step` replaced `ult_compute` rather than being added beside it. The next line added to this module takes it; `src/kernels/cmp_prim.c` is where the three primitives go | 200 | primitives ↔ predicate derivation — **still available, and now at zero headroom** |
| M17 | `kernels/mux.[ch]` | K10 select — **three sources**, `cond` is 1 bit (`ckd.15`) | **65 / 90** | — (unused; `cq_mux_step` is already exported for M12) |
| M18 | `kernels/mul.[ch]` | K11 shift-add over K8. **LANDED 2026-08-16 at 110 (102 body + 8 header) against 160, so no seam is owed** — the module is small because the accumulator is M15's and the reversal is M09's, which is Rule 8 and Rule 1 paying off in the same file. Ships a `.h` for `cq_mul_steps`, which the suite needs as the palindrome's head length | 160 | — |
| M19 | `kernels/divrem_u.[ch]` | K12 unsigned restoring division, **PRD §15 D9**: FLAT scratch, one sandwich, `W` iterations of {shift-in CX, M16's `cq_ult_step`, M14's `cq_sub_step`, M17's `cq_mux_step`, quotient CX}. **LANDED 2026-08-16 at 229 (202 body + 27 header) against 220**, and it **transcribes no gate list at all** — it is a step-index map plus a scratch layout (the contiguous remainder tape, K12.md §2.1a) plus the L5 short-circuit. `34W²+5W` gates over `8W²+4W−1` qubits, measured. Ships a `.h` because M20 composes its block, the way M15 does for M18 | 220 | loop body ↔ driver — **unused; the body is 202** |
| M20 | `kernels/divrem_s.[ch]` | signed wrappers, D3 div-by-zero. **LANDED 2026-08-16 at 169 (158 body + 11 header) against 110 — 44% over**, on M12's and M07's precedent. The overshoot is three things the 110 did not anticipate: `condneg` is the ONE gate list K12 does not get from a sibling and needs its own step decode; the wrapper has four sub-regions to lay out above M19's; and the classical fold is a second, signed one. The recorded seam is `condneg ↔ the wrapper's step decode`, and it is available if this ever passes 240 | 110 | **`condneg` ↔ wrapper decode** (recorded here at Step 17; §3 had none) |

### Layer 4 — analog and sinks

| ID | Module | LOC | Notes |
|---|---|---|---|
| M21 | `angle.[ch]` | 80 | **DONE, Step 18 — 69/80, split 19 + 50 since Step 19 moved `CQ_ANGLE_PI` into the header so M22 has one home for it (18 + 51 before that).** Pure classification of θ against §7's rows, tolerance configurable. Exhaustively testable, zero dependencies. What §7 left open is now **PRD §15 D10**: the window is `tol·π` (ABSOLUTE), one refusal `\|θ\|·1.6e-16 ≤ tol·π` carries the error bound `\|θ − k·π\| ≤ 2·tol·π`, and `tol` is capped at `1e-3`. The θ-relative reading was built first and is a miscompile |
| M22 | `rotate.[ch]` | 150 | §7 Ry/Rz per bit, the θ≡π asymmetry, measurement. **DONE, Step 19 — 96/150 (85 + 11 at the step's close; 83 + 11 after the adversarial review trimmed a guard).** THE FIRST CALLER OF `cq_shadow_rotate` IN `src/`, on exactly two of §7's twelve cells — the general-`Ry` row, both columns (**PRD §15 D12**). Carries `bd lk0`'s resolution — the `Z` is `sink.rz(q, π)`, no seventh vtable entry — and refuses to run inside a sandwich compute half, which is the only guard it owns outright. Split seam recorded: **`§7's rotation table ↔ measurement`**, `cq_measure` moving to `src/measure.c` if `rotate.c` passes 240 |
| M23 | `sink_printf.c` | 70 | Default; CQ_lang's golden-trace **convention**, not its lines — the goldens are handle-level and a sink sees only qubit indices. `x`/`cx`/`ccx`/`ry`/`rz`/`mz`, operands `q<N>`, `%a` angles, flush per line. See the PRD §8 correction |
| M24 | `sink_count.c` | 100 | Per-kind totals, T-count = 7×Toffoli. **NOT peak qubits** — Bennett's `peak_live_wires` is a simulator (Rule 13) and `cq_qubits_peak()` in M03 already has the number exactly. `total` = x+cx+ccx only. See the PRD §8 correction |
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
| 7 | `test_reg.c` + `test_reg_invariants.inc` + `test_reg_death.c` — handles monotonic, never reused (D5); tombstones; I4 (all-constant register owns zero qubits); free of a dirty rail is a hard error **in M07, not merely somewhere**; I2 owner map catches a double-owned qubit; **D7a aborts and D7b deliberately does not** | M07 | **DONE** — 45 ctest green both configurations + ASan/UBSan | 1 |
| 8 | `test_scratch.c`, `test_sandwich.c` — driver runs compute forwards, copyout, compute backwards; a synthetic step function's recorded stream is a **palindrome around the copyout**; I6 violation (target outside scratch) is caught in Debug | M08, M09 | All green | 1 |
| 9 | `test_sink_printf.c`, `test_sink_count.c` — the trace format is line-exact and its angles round-trip bit-exactly; counter totals match the mock sink's stream. **The gate as written was unsatisfiable and is corrected:** "matches CQ_lang's goldens" cannot hold, because all 239 goldens are handle-level and contain no gate lines at all — what M23 matches is the *convention* (PRD §8) | M23, M24 | **DONE** — 69 ctest green both configurations, 22/22 mutants killed. PRD Increment 1 complete | 1 |

### Phase B — kernels *(M10–M20 are independent after Step 9; build in any order or in parallel)*

Every kernel step uses the **same four-part gate**, applied automatically by the shared
kernel driver rather than written per kernel:

| Level | Assertion | Mechanism |
|---|---|---|
| **L1** | `value(dst) == refmodel(a, b)`: the **full cross product** — every `(a,b)` × every bit-kind mask **pair** — at `W ∈ {1,2,3,4,5}`; **structured corners + seeded sampling × every mask pair** at `W ∈ {8}` and above | `bitkinds` + `refmodel` |
| **L2** | After the call, **no index is live that no named register owns**, and every index a named register owns is live | `poolcheck`, automatic on every L1 case |
| **L3** | forward → `_unc` → `dst`'s **values** all-zero; then an explicit free → **`live` restored and every index `dst` held back on the free list**. Asserted on **values and pool state only, never bit-kinds** (PRD §10). Note `_unc` alone does **not** restore the pool — it reclaims nothing, so the free is a required third step, not a tidy-up | `poolcheck`, automatic |
| **L4** | `(NOT, CNOT, Toffoli)` at each `W` matches the golden, cross-checked against the Step 0.2 formula | `sink_count` + `tests/goldens/`, `CQOPS_UPDATE_GOLDENS=1` to regenerate |

Bit-kind masks are not purely random, and they are **pairs — one mask per operand**. The
fixed set always includes: all-classical (this is **L5** — zero gates, zero qubits),
all-quantum, alternating, LSB-only, MSB-only, a one-bit-quantum sweep across all `W`
positions, **and the asymmetric pairs risk R8 names, which no symmetric set can express**.
Random masks are sampled on top.

> **CORRECTED 2026-08-16: L1's row said "all `(a,b)` at `W ∈ {1,2,4,8}`", and the `8` was
> 63% of the compare suite and about half the add suite for almost no coverage.** The
> exhaustion at `W = 8` — 65,536 value pairs at the all-quantum mask, then 65,536 again at
> a random mask — was measured at 131,152 cases per kernel. It bought nothing the rest of
> the sweep did not already have, and the reason is structural rather than statistical:
> the §3 fold table dispatches on a bit's **kind** and never on a qubit's value (D6, no
> demotion), and every kernel is width-generic over `reg->width` with **no width switch**
> (I5, Rule 3). So **at the all-quantum mask the emitted circuit is identical for all
> 65,536 pairs** — one fixed gate sequence, run 65,536 times through the classical shadow.
> A fault that survives the `W ≤ 5` full cross product must be *width*-dependent, and
> widths are covered by covering widths and by the structural checks (closed-form gate
> counts, the palindrome, the peak), not by more values at one width. Values reach the
> circuit only through classical lanes, and only as one bit per lane — a classical `ZERO`
> folds its gate away, a classical `ONE` rewrites it and removes none — which named
> corners exercise directly.
>
> **The replacement is broader as well as ~55× cheaper.** Exhaustion spent its whole
> budget on **two** masks; the structured set (0, max, MSB, equal pairs, `v`/`v±1` both
> orders, `2^i` and `2^i − 1` for every `i`, alternating) plus seeded sampling is crossed
> with **every** mask pair, so the arithmetic corners now meet the asymmetric masks R8
> names, which no value pair previously did. **Verified, not asserted:** the 20-mutant
> battery over `src/kernels/cmp.c` was re-run against the reduced sweep and kills the same
> set. `tests/support/kernelsweep.c:structured_pairs` carries the argument in place, and
> every run still prints its own case counts — the no-silent-caps property is unchanged.

> **Four corrections landed in this table at Step 10**, when the first kernel forced each
> from prose into code. **L1 does not read "the shadow"** — `shadow(dst)` is undefined for a
> constant bit, and under the all-classical mask *every* bit of `dst` is one; the oracle is
> the register's **value**. **L2's "exactly `dst`'s qubits"** was literally false whenever an
> operand was quantum. **L3 cannot compare `minted` or the free-list length** — both are
> monotone, so a round trip that allocates and returns necessarily leaves `minted` higher; an
> earlier draft of the driver compared them and failed 1,276,416 cases on its first run. And
> **`ctest` does not forward trailing arguments to test binaries**: `ctest … -- --update-goldens`
> is not merely unimplemented, it is a hard error (`CMake Error: Unknown argument: --`,
> measured on ctest 4.3.2) that runs zero tests. The `--` idiom belongs to `cmake --build`.
> The mechanism is the environment variable above; `--update-goldens` still works when a test
> binary is run directly. Full statements in PRD §11.
>
> **L3's row used to name `cqrt_free`, which does not exist until Step 23** (M26). What
> exists from Step 7 is `cq_reg_free(ctx, h, proof)`, and the library ships **no** proof —
> `NULL` means "no evidence" and fails loud. Step 10 passes
> `tests/support/poolcheck.c:cq_pc_zero_proof_rotation_free`, whose name is its scope: sound
> on the rotation-free surface (Steps 10–17) because `cq_shadow_rotate` is the only producer
> of `unknown`, and a laundering device the moment M22 lands at Step 19. It does **not**
> answer `ckd.17b` or `ckd.18`.

| Step | Kernels | Module | PRD |
|---|---|---|---|
| 10 | K1 xor, K2 and, K3 or — naturally clean, no sandwich | M10 | 2 |
| 11 | K4 constant shifts, K5 casts — pure index shuffle, naturally clean | M11, M13 | 2 |
| 12 | K6 add, K7 sub — **first sandwich users.** Ripple-carry per PRD §4, not Cuccaro | M14 | 3 |
| 13 | K9 compares — eq/ult/slt primitives, then the 7 derived predicates (`ne=¬eq`, `ugt=ult(b,a)`, `ule=¬ult(b,a)`, `uge=¬ult(a,b)`, signed trio by sign-bit flip) | M16 | 4 |
| 14 | K10 mux, then variable shifts as a barrel over it | M17, M12 | 5 |
| 15 | K8 Cuccaro accumulator — in-place, self-cleaning, 1 **caller-supplied** ancilla. L4 golden `6W−5` **for `W ≥ 2` only** — at `W = 1` the closed form's components are `(0, 2, −1)` and the correct pin is `(0, 1, 0)`, re-derived rather than ported (K08.md §5 D1). **NOT a Rule 7 kernel and NOT drivable by the shared Phase-B gate** — `acc += b` is destructive and its inverse is the reverse circuit, so `test_kernel_addacc.c` restates L1/L2/L3/L4 by hand and **L5 does not apply** (K08.md §5 D7) | M15 | 5 |
| 16 | K11 mul — shift-add over K8. **`W = 1` is a DELEGATION TO K2, not the closed form** — `lower_add_cuccaro!` is out of domain at `W ≤ 1` (adder.jl:66), and `13W² − 8W` evaluates to the right TOTAL (5) with the wrong split twice over: the formula says `(0,5,0)`, the uniform path emits `(0,3,2)`, and `a·b mod 2 = a ∧ b` is `(0,0,1)` (K11.md §3, §5 note 9). **The accumulate is `6W−5` STEPS, never one** — M18 calls `cq_addacc_step`, and a whole `cq_kernel_addacc` as one step would make the reverse half re-accumulate with `dst` already copied out (bd rhp). **Its L4 golden is SELF-PINNED**: shift-add over Cuccaro exists in no Bennett source, so Step 12's against-upstream gate has no analogue here and what replaces it is a decomposition check binding M18's per-accumulate cost to M15's *measured* one | M18 | 5 |
| 17 | K12 divrem — unrolled restoring division. **PRD §15 D9 settles the shape before a line is written**: FLAT scratch (`8W²+4W−1` qubits, `34W²+5W` gates — 2216/543 at i8, and **i128 is a shipped `divrem` width**, 557 696/131 583); `fits` read straight off the comparator carry-out, not `not1(ult(…))`; quotient bit one CX; `r_0`'s upper bits real scratch. **M19 composes M16's, M14's and M17's EXPORTED step blocks and transcribes nothing** (plan §0.4). Every unsigned figure is already MEASURED through the real emitter in both configurations at nine widths; **the signed wrapper is not** — mark it in the test file. **Pin L4 at `W ∈ {1,8,16,32,64,128}`** — not 8/16/32/64, which K12.md said until 2026-08-16. Build the §3.0 composition check BEFORE the kernel: it is the only L4 assertion that survives `CQOPS_UPDATE_GOLDENS=1`, and K12's "narrow the blocks to `t+2` bits" mutant is K11's, three times bigger. **LANDED 2026-08-16, 153/153 in both configurations, and NOT ONE PINNED NUMBER MOVED — including all four signed columns, which had never been executed.** The suite is split in two on the M19/M20 seam (bd `mmv` option (a), taken up front) and the goldens with it | M19, M20 | 6 |

**Step 12's extra gate:** the first sandwich kernel must pin `x+1` at `i8` against
Bennett's published baseline (PRD §11 L4) and document any deliberate delta. This is the
one place the port is validated against upstream rather than against itself.

**Step 17's extra gate:** D3 — `sdiv`/`srem` by zero is deterministic-but-unspecified,
documented, and never traps. Assert it does not trap; pin whatever it returns.

### Phase C — axes

| Step | Red | Green | Gate | PRD |
|---|---|---|---|---|
| 18 | `test_angle.c` — every row of §7's table incl. mod-4π vs mod-2π boundaries and tolerance edges. **The gate as written is not achievable in M21 and is corrected:** §7's table is six rows × TWO columns, and the constant/qubit split is Rule 15's whole point — choosing a column needs a `cq_bit`, so M21 pins the **ROW** and M22 owns the cells at Step 19 | M21 | **DONE** — 19 cases + 8 deaths green in both configurations, 39/39 real mutants killed with 4 deliberately-equivalent controls alive, then a 29-agent adversarial review whose 9 surviving findings added 3 cases and `-ffp-contract=off` | 7 |
| 19 | `test_rotate.c` — **the θ≡π asymmetry**: `Ry(π)` on a constant bit flips it with **0 gates and 0 qubits**; on a qubit it emits `X` then `Z`. `Rz` on a constant is a no-op at every φ. Measurement returns shadow, 0 for unknown, emits `mz`, is terminal. **Two clarifications the step forced.** The `Z` had no vtable entry and no `cq_emit_*`; it is `sink.rz(q, π)` (**PRD §7**, `bd lk0`) — and note "`Ry(π) = XZ`" is a MATRIX product while "emit `X` then `Z`" is a CIRCUIT, so the emitted pair is `Z·X = Ry(3π)`, which is the same row up to a global −1 that no fixed spelling can remove. **Which rows poison is a decision, not a detail** — PRD §15 **D12**: only `Ry` off the lattice | M22 | **DONE** — 19 cases + 12 deaths green in both configurations at 94/150 LOC (83 + 11), 35/35 real mutants killed across three rounds | 7 |
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
| 25 | **L7** — Grover per PRD §12. (a) compiles through `cqc`, links, emits a gate stream; (b) **classical mode**: `M_PI/2 → M_PI` runs deterministically and `cq_measure` returns what plain C computes; (c) counter sink reports Toffoli count and T-count, **peak qubits from `cq_qubits_peak()` rather than from the sink** (PRD §8 correction), stable across runs and pinned | — | **v1 done** | 8 |

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
| R2 | **D7 aliasing** — CQ's pass emits `add(h,h)` or `_unc(out,out,b)`. Every kernel assumes distinct registers | ~~Only surfaces at Step 24 (L6)~~ — **measured at Step 7 instead**, against all 239 goldens, which is 17 steps earlier than this row expected | **Split; the two halves came out opposite ways** (PRD §15 D7a/D7b). **D7a** (`out` among the sources): **0** of 25,147 `_unc` calls → hard error in *both* configurations. **D7b** (two sources aliasing): **599** occurrences, **10** on v1's integer surface including `cq_template_mul_i32(h10,h10)` → **legal, must not abort**. The defensive `cqrt_copy` is therefore **required, not contingent**, at the M26 handle boundary in Step 23 — still one place, not twelve. ~~Assert loud from Step 7~~ would abort at Step 24 on shipped fixtures |
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
