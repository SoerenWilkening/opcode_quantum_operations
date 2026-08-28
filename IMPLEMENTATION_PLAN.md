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

> **I6(a) — target side.** Inside a `cq_sandwich` compute half, every gate *target a
> KERNEL NAMES* is a bit of the scratch region. Sources appear only as controls, and
> controls are never materialised.
>
> **I6(b) — control side.** Every bit of the scratch region is `CQ_BIT_Q` for the whole
> duration of the compute half. `cq_sandwich` guarantees this by **pre-materialising the
> entire scratch region at step 0**, before the first compute step runs.

> **THE FOUR WORDS "A KERNEL NAMES" WERE ADDED AT STEP 20, and they are a narrowing rather
> than a loophole.** §9's promotion of a Toffoli is `CCX(w,c1,anc); CCX(anc,c2,t);
> CCX(w,c1,anc)`, and `anc` — M06's shared ancilla — is a target that is not a scratch bit
> and could not be, since it must outlive the region rather than the step. What I6 protects
> is that a step is an INVOLUTION, and the promoted block is: `A` and `B` are each
> self-inverse, so `(ABA)² = I`, and the block is also a palindrome as a sequence, so the
> recorded stream stays one. The enforcement follows the statement — `check_target` runs at
> `cq_emit_*`'s public entry points, on the caller's target, and the promotion's physical
> emitters deliberately do not re-run it. **Widening the extent to cover the ancilla instead
> is the wrong fix and would disarm I6(a) for the whole compute half**, exactly as
> `sandwich.h` already records for the copyout.
>
> **Step 20 also amended the one-gate-per-step premise to ONE INVOLUTION PER STEP** (PRD
> §10). A promoted step is three gates; what `cq_sandwich`'s replay needs is that
> `compute(env, s)` undoes itself, and `A·B·A` does.

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

**BUILT AT STEP 20, and five things about the shipped shape are worth recording.**

1. **`ctrl_depth` is a STACK, not a counter.** Row 0 has three outcomes and a nested push
   has to restore the previous one, so `cq_ctx` carries `cq_ctrl_stack ctrl` — frames of
   `{mode, wire, owns_wire, and_a, and_b, anc, has_anc}` — and the emitter reads only the
   top. The hot accessors are `static inline` over the STACK rather than over the context,
   which is what lets `controlled.h` be included BY `ctx.h` without a cycle (reg.h's trick,
   for reg.h's reason).
2. **Three modes, not two.** `CQ_CTRL_OFF` covers both "no region" and "a region whose
   control was `CQ_BIT_ONE`", because row 0 makes those the same thing to an emitter.
3. **The emitter's clause order is load-bearing and asymmetric** — skip, then the §3 fold
   on the CONTROLS, then the promotion, which comes BEFORE any fold on the TARGET. PRD §9
   row A states why; getting it backwards is a controlled region silently made
   unconditional, and it is invisible at the all-quantum mask.
4. **The promotion emits through `cq_emit_cx_phys` / `cq_emit_ccx_phys`**, a physical tail
   exported from M05 for M06 alone. A promotion that called `cq_emit_*` back would promote
   its own promotion.
5. **The test side is one setter, one push site and one loop** — `cq_kd_set_ctrl`,
   `call_kernel`, `cq_kd_for_each_region` — and the region loop takes the SUITE's own sweep
   body rather than imposing a shape, because a third of the catalogue has a bespoke one
   (a cast sweeps a width PAIR; K10's mux must drive `cq_kd_case` directly, since
   `cq_kd_case2` fills `values[2]` with zero). None of the 71 driver call sites changed.

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
| 0.5 | Resolve the three PRD blockers from review: the **symbol count** (recount from the real yaml — the question was framed as ~~**1455 vs 1474**~~ and **both framings were wrong**. **RESOLVED 2026-08-14, re-verified 2026-08-22:** the opcode grid is **2479 = 1595 purely-integer + 884 fp-touching**; `1474` is a **phantom** matching no revision and no partition, `1732` a stale generated-header comment, and `1455` is the integer count only if i80 is ruled out — it is **not** (PRD §1, `docs/cqrt_census.txt` Part F)); **`_unc` vs `cqrt_free` qubit ownership** (who returns qubits to the pool — this decides M09's API); **L4 golden tuple arity** (`58/6/40/12` is four numbers against a three-tuple). | PRD-v1 edits |
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
>
> **That last clause put the evidence in the wrong module, and the question is settled
> (2026-08-22, `PRD §15 D15`).** M07 carries the *plumbing* — the `proof` function
> pointer, unchanged — while the evidence is built at the **M26 handle boundary**, since
> it is a record over the call stream and M07 never sees a call. `bd 06t` BUILT it at
> Step 23 landing 2 (2026-08-27): `shim/cq_shim_record.[ch]` + `shim/cq_shim_reduce.[ch]`
> + `cq_shim_certificate`, with M07's `proof` pointer unchanged, exactly as this
> paragraph predicted.

### Step 0 status (2026-08-14)

| # | State | Where the answer lives |
|---|---|---|
| 0.1 | **done** | `third_party/bennett/` @ `980805de` + `COMMIT`; URL in PRD §0 |
| 0.2 | **done** | `docs/constructions/K01..K12.md` — *but see GAP 1 below* |
| 0.3 | **done** | `docs/cqrt_census.txt`; `third_party/cq_lang/` @ `a6a92fe` |
| 0.4 | **done** | `docs/constructions/BASELINES.md` |
| 0.5 | **all three done.** Item 1 → PRD §1 (2479 = 1595 int + 884 fp; `1474` phantom). Item 2 → PRD §10, settled 2026-08-14: `cqrt_free` is the sole deallocator, and it does **not** decide M09's API (see the note above). Item 3 → PRD §11 | PRD §1, §10, §11 |
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
| `kerneldrv.[ch]` | ~~249~~ **333 landed** (279 + 54) | **Added at Step 10.** The shared Phase-B driver §4 below assumes without naming — see the note. **SPLIT TWICE, both on recorded seams**: at Step 11 the sweep shapes went to `kernelsweep.c`, and at Step 20 the controlled axis took the body to **345 of 300** and the axis went to `kernelctrl.c`. The second seam is `the four LEVELS` against `the AXIS`, and it holds because `cq_kd_case` gained exactly three things from Step 20 (the control rail among the named registers, a control-aware L1 oracle, a per-mode L5) while everything else the axis needed was new code with no assertion in it. No third seam is recorded; the body is at 279 |
| `kernelsweep.c` | **128 landed** | **Split from `kerneldrv.c` at Step 11** on this table's own recorded seam: the sweep SHAPES here, the four LEVELS next door |
| `kernelctrl.[ch]` | **77 + 9 landed** | **Split from `kerneldrv.c` at Step 20**, when PRD §9's axis took that file past the guard. Carries the region mode, the one-bit control rail it mints, `cq_kd_for_each_region` and §9's gate-tuple transform. Its public surface is declared in `kerneldrv.h` with the rest of the driver — a suite should see one driver, not two |
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
3. **DEVIATION — `Debug` was UBSan-only on the default toolchain. CLOSED 2026-08-27
   (`bd 6wg`); the plan's `-fsanitize=address,undefined` is what Debug now builds
   with.** Apple clang 17 on Darwin 25 / x86_64 has a broken AddressSanitizer runtime:
   a trivial `main` built with `-fsanitize=address` dies with `SIGILL` inside
   `libsystem_pthread` before reaching `main` (re-measured 2026-08-27 on clang
   17.0.0/clang-1700.6.4.2 — still exit 132). Hard-coding the flag would make every
   Debug binary in the project unrunnable on this box. So each sanitizer is **probed**
   — compiled *and run* — via `check_c_source_runs`, and only what works is enabled;
   what is missing gets a CMake warning, and `test_skeleton` cross-checks the build's
   belief against the compiler's `__has_feature` so the gap can never go quiet.
   `CQOPS_SANITIZERS=ON` turns a missing sanitizer into a hard configure error. UBSan
   on Apple clang does genuinely abort, verified via `-fno-sanitize-recover=all`.

   **What closed it is that the COMPILER is now probed too.** A sanitizer probe can
   only choose among flags for a compiler that is already fixed, and here the broken
   thing was the compiler. `cmake/CqopsDebugToolchain.cmake` runs **before
   `project()`** — forced, since `CMAKE_C_COMPILER` is consumed by the C language
   enable — probes the default compiler with `-fsanitize=address`, and only if that
   binary does not RUN searches for one whose does (`brew --prefix llvm`, then the two
   Homebrew prefixes). Probed, never pinned: a hard-coded `/usr/local/opt/llvm` is
   absent on a box without Homebrew LLVM and wrong on Apple Silicon. **Debug only** —
   Release pins gate counts (R5) and has no use for a sanitizer runtime — and an
   explicit `-DCMAKE_C_COMPILER=`/`CC=` always wins. `CQOPS_DEBUG_TOOLCHAIN` is `AUTO`
   (default) / `ON` (a hard configure error if nothing on the host runs ASan) / `OFF`.
   CMake cannot change a build tree's compiler in place, so an existing tree keeps its
   old one and is told to `make clean`.

   **Measured on this box 2026-08-27, Homebrew clang 22.1.5:** the whole tree builds
   clean under `-Wall -Wextra -Werror -Wconversion`, `cqops: Debug sanitizers ACTIVE:
   undefined, address`, and **292/292 ctest green** — i.e. no ASan finding anywhere in
   the tree, which is the first time that claim has been makeable. Rule 17 still
   applies to the *host*: a box with no ASan-capable compiler falls back to UBSan-only
   with a warning, and a run there must say so.

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

> **Vindicated 2026-08-22, and the "cannot" is now measured rather than argued:
> `PRD §15 D15`.** The evidence that replaces the shadow at the CQ_lang free boundary is
> an observed undo certificate read off the *call stream*, and it is substituted exactly
> here, through this parameter, with **no change to M03 and no change to this signature**.
> Read D15 for what the certificate is rather than restating it at a call site.

**Deviation 2 — M02 ships no un-poison of a LIVE qubit, deliberately.** Nothing in
`shadow.h` can return a **live** entry to determinate; entries are born known-0 by
`cq_shadow_ensure` and that is the only route to clean. A convenience setter would have
settled `ckd.17` by accident, in the one direction that launders a dirty rail into a
provably-clean one.

> **Two corrections, both 2026-08-22, and the paragraph above is the amended text.**
> (i) The word **live** was missing and the sentence was false from Step 8 onward:
> `cq_shadow_retire` does write `{0, 0}`, and the comment heading it in `src/shadow.c`
> calls it *"the one write that clears `unknown`"* — but it runs strictly *after*
> `cq_qubits_release` has returned, so it never touches a live qubit. `src/shadow.h`'s
> header note ("THERE IS DELIBERATELY NO UN-POISON OF A **LIVE** QUBIT") has always
> carried the correctly scoped version; this paragraph did not.
> (ii) **The promised sanctioned write is FORECLOSED, not pending.** This paragraph used
> to end *"ckd.17 carries a note saying the sanctioned write goes in `shadow.h` when it is
> resolved"*. It is resolved — `PRD §15 D15` — and D15 needs no such write, because the
> evidence it substitutes is read off the call stream rather than off a shadow entry.
> **No un-poisoning write may be added to `shadow.h`.**

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
   — *table ↔ invariant checking* — ~~is unused and stays available at a 240-line trigger
   on `reg.c`~~ **was TAKEN at Step 23 (2026-08-22), at exactly that trigger and for
   exactly the reason it was recorded**: PRD §15 D15's free-time disposition would have
   put `reg.c` at ~285. `src/reg_check.c` now carries `cq_reg_check_operands`,
   `cq_reg_sources_alias` and `cq_reg_audit`; `reg.h` declares all of it unchanged, so
   the split is a translation-unit boundary and not an API one. `tests/test_reg.c` **did**
   hit the 300 guard and split along that same line into
   `tests/test_reg_invariants.inc`, and at Step 23 took its SECOND recorded seam —
   *the table and its lifecycle ↔ the free-time disposition* — into
   `tests/test_reg_free.inc`. See the M07 row in §3 for both, and for the reserve.
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
   **[Annotated 2026-08-22 rather than rewritten, because the refusal was the right call
   at the time: it is settled as `PRD §15 D15`, which keeps this C signature unchanged and
   is what finally reads the `h` — today's only proof throws it away (the `(void)h;` in
   `cq_pc_zero_proof_rotation_free`, `tests/support/poolcheck.c`). What D15 changes is the *contract*, not the type: the
   disposition at the free becomes three-valued, and the third state is not expressible
   through this boolean, so it lives on the free path rather than in the proof.]**
4. **Rule 6's literal wording was aborting on valid input** and has been rescoped in both
   CLAUDE.md and PRD §10 to the rail's *qubit-carrying* bits. "Every bit is `BIT_ZERO` or
   a known-zero qubit" rejects a `CQ_BIT_ONE`, i.e. `int x = 5;` going out of scope — and
   makes L5's zero-cost classical path unreachable. It does **not** let `ckd.18` through
   — and `ckd.18` closed 2026-08-22 as `PRD §15 D15` §4, where those rails come out
   **provably dirty** rather than unprovable, so they never reach the free list either way.
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

> **CLOSED one step later, at Step 8, and it stays closed under the certificate.**
> `cq_reg_free` was rewired through `cq_ctx_release_qubit`, which releases and *then*
> retires the shadow entry; the comment on `cq_reg_free`'s release loop in `src/reg.c`
> names this exact hazard as what that ordering closes. `PRD §15 D15` does not reopen it — a rail whose certificate does not
> discharge it is **stranded**, never released and never on the free list at all, so no
> index can come back to `cq_materialise` carrying a stale entry.

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

**The sweep is a constant sample at every width, and the budget is printed by every run
(2026-08-21).** This paragraph used to describe a per-width cap schedule — full cross
product at `W ≤ 5`, every value pair with a rotating mask at `W = 8`, width-seeded sampling
above. All of it is replaced by `cq_kd_samples()` cases per `(kernel, width)`, drawn jointly
over masks and values, with the all-classical row (which *is* L5), the all-quantum row
(which L4 pins) and the four value corners forced inside the budget. Coverage was **moved
and, on the named mask rows, genuinely reduced** — those are now sampled rather than
enumerated — and a run that says so in its own output, naming its count, its pool and its
seed, is the only kind of cap this project allows.

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
   all-quantum mask saw only odd `vb`. The fix at the time gave the all-quantum mask the
   full cross product outright; since 2026-08-21 there is no rotation at all — mask and
   value are drawn **jointly** from one seeded xorshift, which cannot alias this way.
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
| M06 | `controlled.[ch]` — §9 promotion, control stack, shared ancilla, nested AND. **LANDED 2026-08-20 at 233 (182 body + 51 header) against 140 — 66% over**, on M07's, M12's and M20's precedent. The overshoot is four things the 140 did not anticipate: row 0 is a whole extra DIMENSION (three modes, and a nested push must restore the previous one, so the state is a growable STACK rather than a counter); the nested AND owns a flag qubit it must uncompute and release, plus an idempotent case for pushing the same wire twice; the coincidence refusal (PRD §9 row B); and §7's two rotation rows plus D11's two refusals, which §3's row placed in M22 and which cannot live there — M22 must not know the axis exists | ~~140~~ **233 landed** | **`the stack and row 0` ↔ `§9's promotion table`** — recorded at Step 20, unused: `cq_ctrl_push`/`_pop`/the AND stay in `controlled.c`, and the three `promote_*`, the two rotations and the two refusals move to `src/controlled_promote.c` if the body passes 240 — the fixes an adversarial review forced took it from 162 to 182, so the headroom is now 58 |

### Layer 2 — registers and the sandwich

| ID | Module | LOC | Split seam |
|---|---|---|---|
| M07 | `reg.[ch]` — handle table, tombstones (D5), the free path, the physical copy and `cq_reg_swap_bits`; **`reg_check.c`** carries the I2 owner-map **sweep** and the D7a abort + D7b query | ~~180~~ ~~283 landed~~ **`reg.c` 230 + `reg_check.c` 73 + `reg.h` 67 = 370 across two TUs** | **`table ↔ invariant checking` — TAKEN 2026-08-22 at Step 23, at the recorded 240 trigger.** PRD §15 D15's free-time disposition (`bd 06t`) added ~57 lines to the table half and would have put `reg.c` at ~285, so the seam was taken as scheduled rather than improvised (Rule 12). The moved half is the part that **mutates nothing** — it reads the table and aborts — and `cq_reg_xor_into` stayed behind on purpose although the header lists it beside `cq_reg_check_operands`: it EMITS, and D7b's defensive copy (`bd 493`) lands on it. The audit now walks the table through the **public validating accessors** rather than reg.c's static slot reader, which is a strict improvement — a duplicated reader would have been the masking-layer trap plan §0 records. **Reserve seam, table side: `the free-time disposition ↔ the table`** → `src/reg_free.c`, trigger 260; landing 2's certificate plugs into `cq_reg_disposition`'s caller and is the thing likely to take it. **Test side, two seams, both recorded before either file was written:** `table ↔ invariant checking` gave `tests/test_reg_invariants.inc` at Step 7, and **`the table and its lifecycle ↔ the FREE-TIME DISPOSITION` gave `tests/test_reg_free.inc` at Step 23** — the four pre-existing free-path cases moved with it, because leaving them behind would have made the seam a size cut rather than a subject one |
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
| M21 | `angle.[ch]` | 80 | **DONE, Step 18; `bd fna`'s `k mod 4` split landed at Step 20 (73/80: 51 body + 22 header), so `CQ_ANGLE_HALF_TURN` is now `k ≡ 1 (mod 4)` and `CQ_ANGLE_NEG_HALF_TURN` is `k ≡ 3` — the two differ by a global −1 that nothing uncontrolled can observe, which is why M22 still emits the identical pair for both and `test_rotate_table.inc` pins that. It became load-bearing three ways at once (PRD §15 D11): the constant column owes `π·b` against `π·(1−b)`, the qubit column `∓π/2`, and `_inv` swaps the two while fixing 0 and 2. Step 18's own figure was 69/80, split 19 + 50 since Step 19 moved `CQ_ANGLE_PI` into the header so M22 has one home for it (18 + 51 before that).** Pure classification of θ against §7's rows, tolerance configurable. Exhaustively testable, zero dependencies. What §7 left open is now **PRD §15 D10**: the window is `tol·π` (ABSOLUTE), one refusal `\|θ\|·1.6e-16 ≤ tol·π` carries the error bound `\|θ − k·π\| ≤ 2·tol·π`, and `tol` is capped at `1e-3`. The θ-relative reading was built first and is a miscompile |
| M22 | `rotate.[ch]` | 150 | §7 Ry/Rz per bit, the θ≡π asymmetry, measurement. **Step 20 took it to 119/150 (108 body + 11 header)**: row 0's skip at the top of each bit function (which has to be HERE, not in the emitter, because the general row materialises before it emits — so an M22 relying on `cq_emit_*` alone would take W qubits for a region that does not run), D11's refusal at the three folding sites with `half_turn_row` naming the parity, `cq_ctrl_ry`/`cq_ctrl_rz` for the two general rows, and a measurement refusal. **DONE, Step 19 — 96/150 (85 + 11 at the step's close; 83 + 11 after the adversarial review trimmed a guard).** THE FIRST CALLER OF `cq_shadow_rotate` IN `src/`, on exactly two of §7's twelve cells — the general-`Ry` row, both columns (**PRD §15 D12**). Carries `bd lk0`'s resolution — the `Z` is `sink.rz(q, π)`, no seventh vtable entry — and refuses to run inside a sandwich compute half, which is the only guard it owns outright. Split seam recorded: **`§7's rotation table ↔ measurement`**, `cq_measure` moving to `src/measure.c` if `rotate.c` passes 240. **TEST SIDE, THIRD SEAM RECORDED 2026-08-22 AT STEP 23, BEFORE IT IS NEEDED: `which rows POISON, and what that costs at the FREE` → `tests/test_rotate_poison.inc`, trigger 290.** Both of this suite's earlier seams were spent at Step 19, when it reached 437 of 300 and took two splits at once; Step 23 added the `ckd.18` default-disposition case (a general-`Ry` rail STRANDS, by index) and it stands at **283**, so the next cut is named rather than improvised. It is a subject cut: those three cases are the only ones in the file whose subject is the SHADOW's disposition rather than the emitted gate stream, the only ones that free anything, and the ones D15's certificate will keep moving |
| M23 | `sink_printf.c` | 70 | Default; CQ_lang's golden-trace **convention**, not its lines — the goldens are handle-level and a sink sees only qubit indices. `x`/`cx`/`ccx`/`ry`/`rz`/`mz`, operands `q<N>`, `%a` angles, flush per line. See the PRD §8 correction |
| M24 | `sink_count.c` | 100 | Per-kind totals, T-count = 7×Toffoli. **NOT peak qubits** — Bennett's `peak_live_wires` is a simulator (Rule 13) and `cq_qubits_peak()` in M03 already has the number exactly. `total` = x+cx+ccx only. See the PRD §8 correction |
| M25 | `sink_qec.c` | 140 | **SHIPPED at Step 26 (129 lines).** NOT conditional on the library being present — the file always compiles, and its no-library arm registers NOTHING, so `CQOPS_SINK=qec` takes M04's existing "names an unregistered sink" hard error instead of a silent fallback (and the shim needs no build-configuration branch). The library is found through `-DCQOPS_QEC_DIR=<a built qec/>`, opt-in exactly as `CQOPS_CQLANG_DIR` is. **THE INSTALL IS TWO CALLS, ONE ON EACH SIDE OF `cq_ctx_init`, and there is no instant at which both preconditions hold**: `cq_sink_qec_register()` must precede it because a context resolves `cq_sink_active()` ONCE at construction, and `cq_sink_qec_bind(&ctx->pool)` must follow it because the pool it configures is inside the context. Runtime inputs are `CQOPS_QEC_CONFIG` (required; no default, since one would silently pick a code distance and an n_logical), `CQOPS_QEC_TRACE` (optional; written as a NAMED `.partial` and renamed only by a clean teardown) and `CQOPS_QEC_PRECISION` (default 20, refused above 30 — see M25b). Conditional on `C_quantum_error_correction` being present. **That name is a REPOSITORY, not the artefact** (PRD §15 D19): the library is its `qec/` subdirectory, built as `libqec.a` with header `qec/qec.h`, so a `find_library` spelled from the project name finds nothing. `x`/`cx`/`ccx`/`mz` map 1:1 onto `qec_x`/`qec_cx`/`qec_ccx`/`qec_mz`; every `qec_*` returns `int` where our vtable returns `void`, so −1 aborts through `cq_sink_die`. ~~Ry/Rz stubbed~~ — **D19 retired the stub**: `rz` converts the double to `(p, q_denom)` and `ry` is emitted `sdg; h; rz; h; s` |
| M25b | `sink_qec_angle.c` | 120 | **SHIPPED at Step 26 (74 lines), and it is the half that is BUILT AND TESTED ON EVERY BOX** — it depends on `<math.h>` and nothing else, so a tree with no QEC library still runs its seven cases and four deaths. Cap `2^40`, inside D19's measured `[2^32, 2^48]`. MEASURED CONTRACT, IN THREE BANDS, because one bound over one range provably could not pin the double-double: **A** shipped cap, 1e-4 … 1e5 — 0.00243 ulp against 0.35055 for the same continued fraction with the `PI_LO` term dropped, bound 0.05, and this is the band that pins it; **B** cap `2^30`, 1e0 … 1e12 — 0.06337 against 0.37224, bound 0.15, reached by TURNING THE CAP DOWN so `p` stays under 2^53 and the identical arithmetic can be checked seven decades higher; **C** shipped cap, 1e-6 … 1e-5 — both variants 0.44325, bound 0.50, because there the CAP binds and nothing about the quotient is under test. The single 0.5-ulp bound the case shipped with first killed a fully naive quotient and **survived** the `PI_LO`-only deletion, which is what a mutation battery found. Above ~1e15 the **`long p` bound, not the cap**, is what stops the expansion; below ~2.9e-12 rad the ratio folds to `(0, 1)`, which is why the precision knob is capped at 30. **SPLIT SEAM RECORDED IN ADVANCE (Rule 12).** The double → `(p, q_denom)` continued-fraction conversion, the denominator cap and the >double-precision π. It is a self-contained numeric concern with its own suite, it is where D19's measured cap band lives, and it is the half that has nothing to do with a vtable |

### Layer 5 — shim

| ID | Module | LOC | Notes |
|---|---|---|---|
| M26 | `shim/cq_runtime_impl.c` | 220 | The `cqrt_*` surface incl. the PRD §2.1 easy-to-miss families. **Three scopes have been assigned to M26 since this budget was written — `PRD §15 D15`'s undo certificate and three-valued free (`bd 06t`), D7b's defensive copy (`bd 493`) and sink registration (`bd utk`) — so the 220 is very likely wrong. It is left AS WRITTEN rather than guessed at: a Rule-12 budget and a split seam are design decisions, and none of the three beads has been built, so there is nothing to measure yet. Set it, with a recorded seam, when M26's shape is known.** **ANSWERED 2026-08-22 AT STEP 23, once the surface had been measured rather than guessed at. The paragraph below stands as the record of the two times the question was asked and refused, and the hypothesis it floated is REJECTED — tested, not overridden.**

> **THE SEAM IS WHO CALLS IT, NOT WHEN IT IS BUILT.** The hypothesis below,
> `the cqrt_* gate surface ↔ the free-time certificate`, cuts M26 where the *schedule* cuts
> it. Measured, the subject boundary that actually exists is
> **`cqrt_*` (hand-written, CQ_lang's IR pass calls it) ↔ the sixteen `cq_shim_*` entry
> points (M28's 992 generated wrappers call them)**. Those two surfaces share nothing but a
> context: different arities, different error dispositions, different ABIs, different owners
> upstream. The certificate is a *third* subject and sits under both.
>
> **THE 220 IS OFF BY ROUGHLY 4×, AND THE CAUSE IS THE ABI, NOT SCOPE CREEP.** M26 must
> define **173** `cqrt_*` symbols (65 in v1 scope, 34 fp aborts, 63 `qram`, 11 `tape`) plus
> the sixteen entry points. C macros are what keep that inside Rule 12 — nine widths of a
> family are one macro definition plus nine one-line invocations, and `check_loc.sh` counts
> non-blank non-comment lines, so a family costs ~15 lines rather than ~90. That is not a
> trick; it is the only way 173 frozen-ABI signatures fit a 300-line rule, and generating
> them instead is out of **M27's** scope, which reads `opcode_table.yaml` **only**.
>
> **THE SPLIT, five files, seamed by subject** (estimates, to be replaced by measurements as
> each lands):
>
> | file | subject | est. | budget |
> |---|---|---|---|
> | `shim/cq_shim_ctx.[ch]` | the process-global context, sink installation (`bd utk`), the one §9 region bracket (`bd d6m` fix (a)), `cq_shim_unsupported` | ~~90 · 45~~ **LANDED 2026-08-23: 64 · 8** | 150 |
> | `shim/cq_runtime_rail.c` | the rail lifecycle: `alloc` / `measure` / `free` / `copy[_controlled]` / `cswap` / `addc` / `xorc` | ~~170~~ **LANDED 2026-08-23: 199** (193 at first draft; the adversarial review's fixes added six). **250 after `bd 76r`'s brackets (2026-08-28). The 240 trigger has fired; the split was REVIEWED AND DECLINED the same day (`bd 2z1`, closed) — 250 against Rule 12's 300 WALL, `check_loc.sh` green, and a trigger is a scheduling prompt rather than the limit. The `LIFETIME ↔ CONTENTS` seam stays recorded here and at the top of the file, with the two things it must not get wrong, for whoever next needs the room** | 220 |
> | `shim/cq_runtime_gate.c` | the gate surface: `x` / `cnot` / `toffoli` / `x_controlled` / `cnot_controlled` / `ry` / `rz` / `rz_controlled[_inv]` / `ry_controlled_inv` | ~~140~~ **LANDED 2026-08-23: 151** (156 at first draft; the review retired a guard whose comment was false) | 200 |
> | `shim/cq_runtime_v2.c` | everything v1 defers: 34 fp widths, 63 `qram`, 11 `tape`, `alloc_handle`. One abort shape, macro-expanded | ~~90~~ **LANDED 2026-08-27: 96**, and `bd r3y`'s worry that the 90 was written before either population was counted turned out to be unfounded — twelve macro bodies cover all 109 | 150 |
> | `shim/cq_template_impl.c` | the FIFTEEN remaining `cq_shim_*` entry points (the sixteenth, `cq_shim_unsupported`, is `cq_shim_ctx.c`'s): handle resolution, D7a refusal, D7b copy, minting `dst`, kernel selection, opening the region | ~~230~~ **LANDED 2026-08-27: 232, AND THE RECORDED SEAM WAS TAKEN** — the file came in at **270** against the 280 budget and the 240 trigger, which is Rule 12's "a scheduled split, never a surprise refactor" exactly | 280 |
> | **`shim/cq_template_dispatch.[ch]`** | **LANDED 2026-08-27, on the seam recorded below before either file existed.** The three tables indexed by the frozen ABI's own enums — 13 opcodes, 10 predicates, 3 cast kinds — each with a `_Static_assert` tying its length to the last enumerator, each with a bounds check (C does not require an enum object to hold one of its enumerators, and an unchecked index calls whatever follows the table in `.rodata`), and **every `src/kernels/` include in the shim**. That last is the second argument for the cut: the boundary half names no kernel and so cannot quietly acquire a per-opcode special case. A cast carries its own function-pointer type — two widths `(F, T)` where a binary kernel has one `W` — because widening `cq_kernel_fn` to cover both is what Rule 7 forbids | **57 · 11** | 280 |
> | **`shim/cq_shim_proof.[ch]`** | **LANDED 2026-08-23, AND IT IS WHERE THIS TABLE'S LANDING-2 ROW NOW GOES.** The free-time EVIDENCE: `cq_shim_shadow_proof`, a `cq_zero_proof` reading the shadow three-valued. It exists because `cqrt_free` could not otherwise be written at all — the library shipped no proof and M07 hard-errors on a NULL one for any qubit-owning rail | **10 · 6** | 120 |
> | **`shim/cq_shim_trace.[ch]`** | **LANDED 2026-08-28 (`bd 76r`, PRD §15 D21).** M26's ANNOTATION half: the `#REGISTER` header, assembled at END of program because handoff §3 wants every one before the FIRST bracket and CQ_lang allocates throughout, and the flat `op begin`/`op end` brackets every `cqrt_*` and `cq_shim_*` entry point opens. It prints NOTHING for a gate — those, and every `#PATCH`, are the QEC library's own — so it is NOT a sink and must not become one. Its ONE activation test is `cq_sink_qec_trace()`, which is NULL under every other sink and in a build without the library; that is what keeps M23's printf sink and the qec trace disjoint consumers of two streams, which D21 names as the concrete hazard. The call sites cost the three surface files **+27 (rail), +23 (gate), +13 (template)** — one bracket per entry point, uniformly, because "bracket the ones that emit" is not a checkable rule | **143 · 13** | 200 |
> | **`shim/cq_runtime_abi.h`** | **LANDED 2026-08-23.** CQ_lang's 173 `cqrt_*` declarations, VERIFIED-verbatim, on `tests/abi/cq_templates_abi.txt`'s precedent but under `shim/` because the LIBRARY includes it. Without it a wrong return or parameter type in one of the 62 definitions compiles clean, links clean (C has no name mangling) and surfaces at Step 24 as a wrong answer; `-Wmissing-prototypes` is not in the flag set | **184** | — |
>
> **Reserve seam, recorded before a line is written:** `cq_template_impl.c` at **240** splits
> on `the opcode DISPATCH TABLE ↔ the handle BOUNDARY`, into `shim/cq_template_dispatch.c`.
> **TAKEN 2026-08-27 AS RECORDED.** The discriminator turned out to be *what makes each half
> change*: the dispatch half is transcription of the frozen ABI and grows when the yaml gains
> an opcode; the boundary half is `tpl_binary`'s ordered call sequence, D7a, D7b and the §9
> region, and grows when a DECISION changes. A `.h` was needed as well, at 11 lines, because
> the two halves are separate translation units — which is also what gives each file a
> DISJOINT set of refusal strings (the dispatch half's three name an ENUM, the boundary
> half's name a WIDTH or a HANDLE), so a swapped message is caught by a
> `FAIL_REGULAR_EXPRESSION` rather than by a reviewer.
> **Landing 2** adds D15's ported reduction engine and the per-handle record — and it lands
> in **`shim/cq_shim_proof.[ch]`**, which is the same file under a name that is true in both
> landings. This row said `shim/cq_certificate.[ch]` until 2026-08-23; the file arrived early
> because landing 1's `cqrt_free` needs a supplier, and naming it for the certificate it does
> not yet hold would have meant a rename at landing 2 rather than an addition. Its own
> reserve seam is recorded in its header: `the EVIDENCE ↔ the RECORD` →
> `shim/cq_shim_record.[ch]`.
>
> **THE TWO NEW SEAMS, RECORDED BEFORE EITHER FILE WAS WRITTEN, AND BOTH TRIGGERS ARE THE
> HOUSE 240** — measured against Rule 12's 300-line hard limit and not against the file's
> budget, which is why M06's (140), M07's (180), M20's (110) and M22's (150) all sit above
> their own budgets and why M07's fired correctly at Step 23. `shim/cq_shim_ctx.c`'s 120 is
> the single ratio-derived trigger in the tree; both new files' first drafts copied it, and
> `0.8 × 220 = 176` would have fired the rail seam on the day the file landed at 193 — a
> two-file plan wearing a trigger's clothes rather than a scheduled split. A budget is an
> estimate; the 300 is the wall.
>
> | file | seam | moves to | landed |
> |---|---|---|---|
> | `shim/cq_runtime_rail.c` | **`the rail's LIFETIME ↔ the rail's CONTENTS`** — the discriminator is EMISSION and it is a DISJOINTNESS: the lifetime half (`alloc`, `measure`, `free`; 11 symbols) emits `mz` and nothing else, the contents half (`copy`, `copy_controlled`, `cswap`, `addc`, `xorc`; 21) takes live handles, returns void and emits nothing BUT X/CX/CCX. **NOT** "only the lifetime half changes a slot state" and **NOT** "only it reads D15's disposition" — `rail_addc` mints two rails, tombstones both and frees both, and measured in Release it advances the D5 handle counter by 2 permanently. What is only the lifetime half's is the SUBJECT: a handle CQ_lang named. A producer/consumer boundary, so landing 2's growth lands on one side. **At the split, `rail_r` is needed on BOTH sides and drags `cq_rail_die` and `RAIL_WIDTH` with it — make the two messages disjoint, as Step 15 did for `cq_addacc_check`, or the CMake pins (which discriminate on the MESSAGE, not the module) mask a deleted guard** | `shim/cq_runtime_write.c` | **199 of 240** (re-measured 2026-08-27) |
> | `shim/cq_runtime_gate.c` | **`the DISCRETE gate primitives ↔ the ROTATION families`** — the five discrete symbols carry no width token, act on one-bit rails and reach M05's exact-shadow surface; all 25 rotations carry a `double angle`, walk every lane and reach M22, which is the only thing in the shim that can make a rail unfreeable or hard-error on D11. Rejected: `uncontrolled ↔ controlled`, a size cut wearing a subject's clothes — Rule 9 makes the axis an emitter MODE, so there is no controlled machinery to move | `shim/cq_runtime_rotate.c` | **151 of 240** (re-measured 2026-08-27) |
> | `shim/cq_runtime_v2.c` | **`the DEFERRED CORE WIDTHS ↔ the ADDRESSABLE-MEMORY families`** — recorded 2026-08-27, before a line of the file was written, because `bd r3y` measured the file over its budget with **no seam owned by anyone**. The 34 fp-width symbols are the SHIPPED families at a width v1 defers — `alloc` / `measure` / `ry` / `rz` / `rz_controlled[_inv]` / `ry_controlled_inv` / `copy[_controlled]`, one width token away from `cq_runtime_rail.c` and `cq_runtime_gate.c` — and in v2 they become real code by WIDENING what already exists. The 74 `qram` + `tape` symbols are a different DATA MODEL: an addressable quantum array and an append-only tape, neither of which libcqops has any representation for at any width, so they never become code by widening anything and need a subsystem. `cqrt_alloc_handle` goes with the fp half and NOT with memory — it is about the shared D5 handle counter, and it is the one body in the file that is REACHABLE today (CQ_lang's own template archives reference it), which is a property of that half's callers rather than of its subject. **Rejected:** `by reason string`, which is this table's cut wearing a implementation detail's clothes, and `fp ↔ integer`, which would put `cqrt_qram_alloc_f32` on the fp side of a family that is undecidable at every width | `shim/cq_runtime_v2_mem.c` | **96 of 240** (landed 2026-08-27) |
>
> **THE TEST SIDE TOOK THE GATE SEAM IMMEDIATELY**, at the 300 HARD LIMIT rather than at a
> trigger: `tests/test_runtime_gate.c` reached 306 and the rotation half moved to
> `tests/test_runtime_gate_rotate.inc`. The rail suite took its seam pre-emptively —
> `tests/test_runtime_rail_write.inc` — on `tests/test_shim_ctx.c`'s precedent that both
> sides wanting the same cut is the argument for the seam.
>
> **AND THE DEATH SUITE TAKES THE SAME GATE SEAM, RECORDED 2026-08-27 BEFORE ITS NEXT CASE
> WAS WRITTEN (Rule 12: a split is scheduled, never improvised).**
> `tests/test_runtime_gate_death.c` stood at **233 of 300** with **no seam recorded anywhere**
> — not in its own header and not in this table — and `bd pnu`'s fixes add five cases, which
> crosses the house **240** trigger. The cut is the module's own, the third file to want it:
> `the DISCRETE gate primitives ↔ the ROTATION families` → **`tests/test_runtime_gate_death_rotate.inc`**.
> It is a subject cut and not a size cut, and on this side the discriminator is even sharper
> than on the source side, because a death case's subject *is* which layer refuses: everything
> that moves refuses through `gate_rot_width` or through **M22** (D11's two refusals, the
> tombstone/measured ORDER pair), and everything that stays refuses through `gate_target` /
> `gate_ctrl` / `gate_distinct` — M05's and M06's surface, where the two-bit shadow is exact
> and where PRD §9 **row 0** is the row the shim is the SOLE detector on. The `.c` keeps the
> one-bit guard, the coincidence guard and row 0; the `.inc` keeps every symbol carrying a
> `double angle`. `CQ_DEATH_MAIN`'s registry stays in the `.c`, which is what makes the
> include order load-bearing (the `.inc` goes above it).
>
> **The rail death suite does NOT take a seam yet and that is deliberate**: at **169 of 300**
> with two `bd pnu` cases to add it is nowhere near the trigger, and its seam — the same
> `LIFETIME ↔ CONTENTS` cut `tests/test_runtime_rail_write.inc` already took on the ordinary
> side — is already recorded above.
>
> **`tests/test_template.c`'s SEAM, RECORDED 2026-08-27 BEFORE ITS FIRST CASE WAS WRITTEN**
> (Rule 12: a split is scheduled, never improvised; and the `.c` did not exist when this
> line was written, which is the whole point of recording it here rather than in its header).
> Step 23.6's Red column asks for **one case per entry point across fifteen of them** plus
> D7a on the six `_unc` shapes, D7b, and the aliased-CONTROLLED case — which is more subject
> than one file holds. The cut is **the same one `shim/cq_template_impl.c` already records**,
> and that both sides want it is the argument for it (`tests/test_shim_ctx.c`'s precedent):
>
> | file | seam | moves to | trigger |
> |---|---|---|---|
> | `tests/test_template.c` | **`the OPCODE SURFACE ↔ the HANDLE BOUNDARY`** — the surface half is what the fifteen entry points COMPUTE: one case per shape, the value through `cq_pc_value_w`, the result WIDTH (`icmp`→1, `cast`→`to_bits`), the pool round trip, and the classical-operand `_hl`/`_lh` lanes. The boundary half is what an entry point does to its HANDLES on the way in and out — D7a's refusal, D7b's defensive copy and its un-copy, the strand/settle accounting for the temporary, and the aliased-CONTROLLED case whose whole subject is the ORDER of the copy against `cq_ctrl_push`. The discriminator is whether the case would still exist if every source handle were distinct: the surface half would, the boundary half is exactly the cases that would not | `tests/test_template_d7.inc` | **240** |
>
> **AND A SECOND SEAM, RECORDED THE SAME DAY BEFORE IT WAS TAKEN.** With the boundary half
> already in the `.inc`, the surface half still reached **298 of 300** once the predicate
> fixture grew the three operand pairs a strict/non-strict mutant needs (measured: with one
> pair, `cq_kernel_ule` in the `ult` row survived the whole suite). The cut is the module's
> own and is the property `tpl_req.wout` exists for:
>
> | file | seam | moves to | trigger |
> |---|---|---|---|
> | `tests/test_template.c` | **`the OPERAND-WIDTH shapes ↔ the RESULT-WIDTH shapes`** — the nine binary entry points (three operand shapes × forward / `_unc` / `_controlled`) all mint `dst` at the operand width, and everything that can go wrong with them is about which LANE carries what. `icmp` and `cast` are the two families where the result width is NOT the operand width — one bit at any width, and `to_bits` — which is the trap the arity-2 signature actively suggests otherwise, and they are the only two that would still be red if every operand lane were correct. `cast` joins `icmp` rather than the binary nine because it is the second instance of exactly that property, not because it is small | `tests/test_template_width.inc` | **240** |
>
> **The DEATH cases are a separate binary from the start and that is not a seam**: D7a is a
> hard error in both configurations, and `add_cqops_death_test` needs its own `CQ_DEATH_MAIN`
> translation unit — `tests/test_template_death.c`, on `tests/test_runtime_rail_death.c`'s
> precedent. Its own reserve seam is the same cut, to `tests/test_template_death_d7.inc`.
>
> **`cq_shim_ctx.c`'s OWN reserve seam, recorded 2026-08-23 before the file was written:**
> `the process CONTEXT ↔ the BOUNDARY VOCABULARY`. The context half is the global `cq_ctx`,
> `cq_shim_ctx_reset` and sink installation — one stateful subject, and the one every other
> M26 file touches. The boundary half is what an entry point calls on its way *in* and on its
> way *out*: `cq_shim_region` and `cq_shim_unsupported`, both stateless, and with different
> callers (the three controlled shapes; M28's 1,487 abort bodies plus `cq_runtime_v2.c`). It
> moves to `shim/cq_shim_region.c`. **Trigger at 120**, which is the house 240/300 ratio
> applied to this file's 150 budget rather than the house figure copied across a smaller
> budget. Measured at landing: **64**, so it is not close.
>
> **THE TEST SIDE TOOK THAT SAME SEAM IMMEDIATELY, and it is recorded here rather than
> improvised in the file.** `tests/test_shim_ctx.c` reached **410 of 300** on its first run —
> `make lint` red — and the cut is the module's, not a size cut: `tests/test_shim_ctx_region.inc`
> carries the §9 bracket's three arms (`bd d6m`) and PRD §1's boundary, and the `.c` keeps the
> context, the reset and sink installation. That the two halves want the same seam on both
> sides is the argument for the seam.
>
> **`tests/test_shim_cert.c`'s SEAM AND `shim/cq_shim_record.[ch]`'s, BOTH RECORDED
> 2026-08-27 BEFORE LANDING 2's FIRST CASE WAS WRITTEN** (Rule 12: a split is scheduled,
> never improvised; and neither file existed when this line was written, which is the whole
> point of recording it here). The test side of landing 2 had **no seam recorded anywhere** —
> not in a header and not in this table — which is the same gap `tests/test_runtime_gate_death.c`
> was in at **233 of 300** two paragraphs above, and it is closed the same way and for the
> same reason. `tests/support/refmodel.c` is the standing counter-example: **284 of 300 with
> no seam** (`bd zmo`), and it got there one line at a time.
>
> | file | seam | moves to | trigger |
> |---|---|---|---|
> | `shim/cq_shim_record.c` | **`the RECORD ↔ the REDUCTION`** — the record half is the per-handle write history and the EFFECT TABLE that says, per `cqrt_*` / `cq_template_*` opcode, which operands are read, which are written, which are controls and which are classical immediates. It is transcription of the frozen ABI and it changes only when the ABI does. The reduction half is the PORTED engine — `reduces_to_identity` and its four guards — which is transcription of `third_party/cq_free_pairing/free_pairing_check.py` and changes only when that pin moves. The discriminator is **which pinned artefact a line answers to**: a wrong read/write set is an ABI transcription bug that `cq_runtime.h` settles, a wrong pairing is an analysis bug that the vendored checker settles, and no line answers to both. That is a sharper cut than "data ↔ algorithm" and it is why this seam is not a size cut | `shim/cq_shim_reduce.[ch]` | **240** |
> | `tests/test_shim_cert.c` | **`the ENGINE ↔ the ENTRY CONDITIONS`** — the engine half drives `cq_shim_reduce` directly on synthesised histories: pairing, the strict-open interval, the top-down scan, an unpaired write, the two `commutes` intervals, `co_written_stable` on `cqrt_cswap`, and both narrowing tripwires. Those cases name no `cqrt_*` entry point and would still exist if D15 had only ever stated ONE rule. The entry-condition half drives real `cqrt_*` / `cq_template_*` call sequences through the shim and asserts the VERDICT: U1's `_unc` shape, U2's self-inverse brackets, U3's `addc` arithmetic, and the birth-value split that convicts `ckd.18`. The discriminator is whether the case would survive D15 losing U1/U2/U3 and keeping only "the write history reduces to the identity": the engine half would, the entry half is exactly the cases that would not | `tests/test_shim_cert_rules.inc` | **240** |
>
> **The rejected cut, named so it is not re-proposed: `the CLEAN row ↔ the two non-clean rows`.**
> It is a size cut wearing a subject's clothes. Every certificate case asserts a verdict on the
> three-valued lattice and the interesting ones are exactly the pairs that DISAGREE — a rail whose
> history reduces and whose birth value is 5 is CLEAN under upstream's own A-rules and DIRTY under
> ours, and splitting the rows puts the two halves of that single fact in two files. The two seams
> above both keep such a pair together.
>
> **AND THE DEATH CASES ARE A SEPARATE BINARY FROM THE START, WHICH IS NOT A SEAM** —
> `tests/test_shim_cert_death.c`, on `tests/test_template_death.c`'s and
> `tests/test_runtime_rail_death.c`'s precedent: `CQ_DEATH_MAIN` needs its own translation
> unit. Its reserve seam is the same `ENGINE ↔ ENTRY CONDITIONS` cut, to
> `tests/test_shim_cert_death_rules.inc`.

M26 appears to have **three** subjects rather than two — the handle boundary (resolve, D7a refusal, D7b copy), the gate surface (`x`/`cnot`/`toffoli`/`copy`/`cswap`/`addc`/`xorc`/`ry`/`rz` + the controlled families), and the rail lifecycle (`alloc`/`measure`/`free` with D15's certificate) — and it is the third that is new and unbuilt, which is exactly why the seam cannot be named yet. A document-grounded *hypothesis* to test when M26 is designed, **not** a derived result: `the cqrt_* gate surface ↔ the free-time certificate`, on the grounds that Step 23's row below calls D15's mechanism "the largest thing M26 owes" and a stateful per-rail analysis over the call stream is a different subject from ABI transcription. **THE HYPOTHESIS WAS TESTED AT LANDING 2 AND IS CONFIRMED, which is the first of the three M26 hypotheses in this table to survive contact.** The certificate is three files that touch no `cqrt_*` body — `shim/cq_shim_record.[ch]` (the per-handle call history and the effect table), `shim/cq_shim_reduce.[ch]` (the PORTED reduction) and `cq_shim_certificate` in `shim/cq_shim_proof.c` — and what the gate and rail surfaces gained is ONE `rec(...)` line per entry point. **The two `rec` helpers are duplicated across `cq_runtime_rail.c` and `cq_runtime_gate.c` deliberately rather than hoisted**: each is four lines and `static`, and a shared helper would put the ABI's operand ORDER one indirection away from the entry point that knows it — and operand order is exactly what D15 §6(ii) says a write model gets wrong. `shim/cq_shim_record.[ch]`'s own reserve seam, `the RECORD ↔ the REDUCTION`, was recorded before either file was written and TAKEN immediately; the discriminator is **which pinned artefact a line answers to** — `cq_runtime.h` for a read/write set, `third_party/cq_free_pairing/` for a pairing — and no line answers to both. |
| M27 | `shim/gen_shim.py` | 280 | Reads `opcode_table.yaml`; emits one `.gen.c` per opcode family, **plus BOTH abort buckets — the 884 fp AND the 603 integer `_inv` bodies (PRD §15 D14, `bd w1c`)**; the pre-D14 wording said "fp aborts" and named only half the bucket. **Split seam recorded 2026-08-22, before a line is written (Rule 12: a split is scheduled, never improvised): `the ABI grid ↔ the emitted bodies`.** `gen_shim.py` keeps everything the frozen ABI dictates and stays the path CI invokes — the yaml load and its sha, the `:35-59` naming-rule expander (**`:39-58` until 2026-08-22 — it starts four lines INSIDE the block, omitting `:36-38`'s three forward-form rules, i.e. the base symbol shape itself; a generator built from it has no `fwd_qq`/`_hl`/`_lh` spelling at all**), the row model, `signature(row)` (with the classical operand's `c_type` read out of the yaml's own `widths` map, never a hand-kept table), each symbol's v1 bucket, PRD §1's partition audit, R7's per-family bucketing, the banner and `--check`. `shim/gen_bodies.py` takes a row and returns the text between the braces, and is **the only file in M27 that names a libcqops or M26 symbol**. Import is one-way; the row record is the whole interface. The seam is where Step 22's gate stops and Step 23's begins, and it is what lets the round-trip oracle — 2479 names AND 2479 signatures diffed against CQ_lang's own generated `runtime/cq_templates.h` — run from the grid half alone with no body emitted. **Trigger it at 240**, the house threshold (M07, M06, M20, M22). **LANDED 2026-08-22 AT STEP 22, PRE-SPLIT, AND THE PROTOTYPE'S FIGURE WAS THE GOOD ESTIMATE: `gen_shim.py` **183** and `gen_bodies.py` **56** under `check_loc.sh`'s py rule — 239 together against the prototype's 190 and the "plausibly ~290" caveat.** The TEST side is where the room went instead, and its seam is recorded in its own header: `tests/test_gen_bodies.py` reached **259 of 300** after an adversarial review, and `the ASSERTIONS ↔ the PROVOCATIONS` moves to `tests/test_gen_bodies_provoked.py` — a THIRD registered ctest test, because Python has no `.inc` escape hatch. The split was taken **before** the trigger and that is the plan working as written rather than an exception: this row already calls the seam's case *ownership, not churn*, and the ownership boundary is exactly where Step 22's gate stops and Step 23's begins. Three artefacts landed with it that the row did not anticipate and that are recorded here so they are not re-derived: **`shim/cq_shim.h`** (40 lines) is the M26↔M28 *contract* — a generator cannot emit calls into a vacuum, so Step 22 ships the sixteen declarations and Step 23 ships their definitions; **`tests/abi/cq_templates_abi.txt`** is the round-trip oracle *vendored* rather than read live out of an unpinned CQ_lang, carrying the yaml-sha256 that proves it is the expansion of the yaml we actually pin (CQ_lang's HEAD has already moved past `third_party/cq_lang/COMMIT` while the yaml has not); and **`cmake/CqopsPython.cmake`** probes for a python3 that can `import yaml` in the compile-and-*run* style of `CqopsSanitizers.cmake`, warning and declining to register the two suites rather than registering them to pass — the reserve seam below is what the warning is warning about. **Reserve seam, grid side: `the reader ↔ the expander`** — PyYAML is present on this box's `python3` and absent from `/usr/bin/python3`, so if CI forces a hand-rolled reader it lands here (~+60–90) and moves to `shim/yamlmin.py`. Two riders: `check_loc.sh` counts every line of a multi-line string literal as code and Python has no `.inc` escape hatch, so emitted C templates cost the bodies half a line each — a second, independent reason that half grows first; and a re-pin costs **no code on either side** (simulated: a new width, predicate and cast pair took 2479 → 2528 with zero edits), so the seam's case is ownership, not churn |
| M28 | generated `*.gen.c` | exempt | **992** integer wrappers + **603** integer `_inv` abort bodies + **884** fp abort bodies (PRD §1 and §15 **D14**; the 1595 integer symbols split 992/603 because `_inv` is `f⁻¹` and `f⁻¹` does not exist for `and`/`or`/`udiv`/`trunc`/`icmp`. 1455 integer if i80 is ruled out of scope) |

Hand-written total ≈ **3,400 LOC** across 27 modules.

### Layer 6 — the L6 harness *(Step 24; not a library module, and `tools/` IS linted)*

`tools/check_loc.sh`'s roots are `src include tests shim tools`, so a `.py` under `tools/`
is subject to Rule 12 exactly as a kernel is. Both seams below are recorded **before**
either file needs one.

| file | LOC | Split seam |
|---|---|---|
| `tools/l6/run_slice_cqops.sh` | **106 raw** (shell is not counted by `check_loc.sh`) | Not applicable, and deliberately so: this is CQ_lang's own four-stage pipeline with ONE line changed — the link — and the whole value of it is that it stays diffable against `CQ_lang/tests/e2e/run_slice{,_multi}.sh`. Splitting it would defeat the review that keeps it faithful |
| `tools/l6/l6_run.py` | **142 of 300** (landed 2026-08-27) | **`the FIXTURE LIST ↔ the VERDICT`** → `tools/l6/l6_report.py`, trigger **240**. The fixture half reads `ctest --show-only=json-v1` out of an UNPINNED CQ_lang and answers to whatever CQ_lang registers; the verdict half (`classify`, the `DEFERRED` regex, the residue observation) answers to OUR abort texts in `shim/cq_shim_ctx.c` and `src/reg.c`. The discriminator is **which repository a line breaks with**, and no line answers to both — the same test this table applies to `shim/cq_shim_record.c`. Rejected: `run ↔ report`, which sounds like the same cut and is not, because `--reuse` already makes the report runnable without the run and a cut there would only move `main`'s loop |

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
| 7 | `test_reg.c` + `test_reg_invariants.inc` + `test_reg_death.c` — handles monotonic, never reused (D5); tombstones; I4 (all-constant register owns zero qubits); free of a dirty rail is refused **in M07, not merely somewhere** (~~a hard error~~ — `PRD §15 D15` §4 made the ACT stranding, confirmed 2026-08-22 and shipped at Step 23; the gate's content, that M07 and not M03 is the layer that refuses, is unchanged, and `CQOPS_FREE_ABORT` restores the stop); I2 owner map catches a double-owned qubit; **D7a aborts and D7b deliberately does not** | M07 | **DONE** — 45 ctest green both configurations + ASan/UBSan | 1 |
| 8 | `test_scratch.c`, `test_sandwich.c` — driver runs compute forwards, copyout, compute backwards; a synthetic step function's recorded stream is a **palindrome around the copyout**; I6 violation (target outside scratch) is caught in Debug | M08, M09 | All green | 1 |
| 9 | `test_sink_printf.c`, `test_sink_count.c` — the trace format is line-exact and its angles round-trip bit-exactly; counter totals match the mock sink's stream. **The gate as written was unsatisfiable and is corrected:** "matches CQ_lang's goldens" cannot hold, because all 239 goldens are handle-level and contain no gate lines at all — what M23 matches is the *convention* (PRD §8) | M23, M24 | **DONE** — 69 ctest green both configurations, 22/22 mutants killed. PRD Increment 1 complete | 1 |

### Phase B — kernels *(M10–M20 are independent after Step 9; build in any order or in parallel)*

Every kernel step uses the **same four-part gate**, applied automatically by the shared
kernel driver rather than written per kernel:

| Level | Assertion | Mechanism |
|---|---|---|
| **L1** | `value(dst) == refmodel(a, b)`, over **a small constant number of random samples**: `cq_kd_samples()`, default **32**, per `(kernel, width)`, nothing scaling in `W`. Each case draws a mask **pair** and a value tuple **jointly**; the all-classical pair (which *is* L5), the all-quantum pair (which L4 pins) and the four value corners are taken first, **inside** the budget. Widths enumerated, never sampled | `bitkinds` + `refmodel` |
| **L2** | After the call, **no index is live that no named register owns**, and every index a named register owns is live | `poolcheck`, automatic on every L1 case |
| **L3** | forward → `_unc` → `dst`'s **values** all-zero; then an explicit free → **`live` restored and every index `dst` held back on the free list**. Asserted on **values and pool state only, never bit-kinds** (PRD §10). Note `_unc` alone does **not** restore the pool — it reclaims nothing, so the free is a required third step, not a tidy-up | `poolcheck`, automatic |
| **L4** | `(NOT, CNOT, Toffoli)` at each `W` matches the golden, cross-checked against the Step 0.2 formula | `sink_count` + `tests/goldens/`, `CQOPS_UPDATE_GOLDENS=1` to regenerate |

Bit-kind masks are not purely random, and they are **pairs — one mask per operand**. The
fixed set always includes: all-classical (this is **L5** — zero gates, zero qubits),
all-quantum, alternating, LSB-only, MSB-only, a one-bit-quantum sweep across all `W`
positions, **and the asymmetric pairs risk R8 names, which no symmetric set can express**.
Random masks are sampled on top.

> **SUPERSEDED 2026-08-21: L1 IS NOW A SAMPLE, NOT A PRODUCT.** The 2026-08-16 correction
> below dropped value *exhaustion* at `W = 8`; this one drops the **product** entirely.
> Both of its factors grew — the value factor was `span²` below `W = 6` and ~`5W+14` at
> `W = 8`, and the **mask** factor is `cq_bk_fixed_pairs`, 12 named rows **plus a
> one-bit-quantum sweep over all `W` positions**, so `O(W)`. Measured across the suite:
> **~2,100,000 L1 cases**, Debug **63.8 s**. It is now `cq_kd_samples()` per
> `(kernel, width)` — default 32, `CQOPS_L1_SAMPLES` to override — giving **~28,400 cases,
> Debug 22.6 s, Release 2.4 s**, 195/195 green in both configurations, and also green at
> `CQOPS_L1_SAMPLES=256`. The **all-classical** pair (which *is* L5), the **all-quantum**
> pair (which L4 pins) and the four value corners are forced **inside** the budget. What
> was given up: the other named mask rows — alternating, lsb, msb, **R8's six asymmetric
> pairs** — and the lane sweep are sampled rather than enumerated, so no single run
> guarantees any one of them. **Widths are still enumerated, never sampled**, because a
> width-generic kernel's remaining faults are loop bounds and MSB boundaries. The
> paragraph below is kept because its argument is *why* a small constant suffices.
>
> **CORRECTED 2026-08-16: L1's row said "all `(a,b)` at `W ∈ {1,2,4,8}`", and the `8` was
> 63% of the compare suite and about half the add suite for almost no coverage.** The
> exhaustion at `W = 8` — 65,536 value pairs at the all-quantum mask, then 65,536 again at
> a random mask — was measured at 131,152 cases per kernel. It bought nothing the rest of
> the sweep did not already have, and the reason is structural rather than statistical:
> the §3 fold table dispatches on a bit's **kind** and never on a qubit's value (D6, no
> demotion), and every kernel is width-generic over `reg->width` with **no width switch**
> (I5, Rule 3). So **at the all-quantum mask the emitted circuit is identical for all
> 65,536 pairs** — one fixed gate sequence, run 65,536 times through the classical shadow.
> Values reach the circuit only through classical lanes, and only as one bit per lane — a
> classical `ZERO` folds its gate away, a classical `ONE` rewrites it and removes none.

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
> answer `ckd.17b` or `ckd.18` — **nor is it meant to.** Both were settled 2026-08-22 as
> `PRD §15 D15` (`bd c1a`, `bd ckd.18`), by an observed undo certificate at the **M26
> handle boundary** rather than by this predicate, which stays exactly what its name says
> it is. "The library ships no proof" stayed true until M26 supplied that certificate at
> Step 23 landing 2 (`bd 06t`, closed 2026-08-27); what `cqrt_free` installs now is
> `cq_shim_free_proof`, the certificate AND this predicate, and the sentence is history.

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
| 20 | `test_controlled.c` — promotion table verbatim from `controlled.jl`; ancilla shared across the region and returned |0⟩; nested control uses **one** control wire; **every Phase-B kernel re-runs its L1–L4 suite under `cq_ctrl_push`**. **Three decisions came due with it and all three are now in the PRD rather than in code**: `bd skh` is **D13** (materialisation is UNPROMOTED, forced by one cell of an eight-row truth table), `bd pf4`'s D11 refusal is BUILT at one greppable site, and `bd fna`'s `k mod 4` split shipped first so the refusal can NAME the parity it refuses. **§9 grew three libcqops-side rows** (A: fold on controls first, promote before folding on the target; B: a control coinciding with an operand is a hard error, measured 0/4,918 in the corpus; C: D13) | M06 | **DONE** — 20 cases + 19 deaths green in both configurations, **195 ctest tests**; all ten Rule-7 kernel modules re-run L1/L2/L3/L5 under four regions and assert §9's tuple transform at every shipped width. **Two mutation batteries proposed 67 mutants; 56 landed, 51 killed in both configurations, 5 survived** — three of the five were real holes in the SUITE (D11's identity rows had no exemption case, the refusal MESSAGE was unread so `half_turn_row`'s parity was unpinned, and the driver never checked that a mode it calls quantum minted a WIRE) and are closed and re-verified by hand; the other two are genuinely equivalent and recorded at their sites on M09's `0xAA` precedent | 7 |
| 21 | `test_unc.c` — `_unc` across every kernel; the PRD §10 asymmetry is **expected**, so forward and `_unc` counts are pinned **separately**. `_inv` for compare flags. ~~`cqrt_free` of a dirty rail is a hard error~~ — `PRD §15 D15` §4 made the ACT **stranding**, so what Step 21's two death cases assert is the REFUSAL, each arming `CQOPS_FREE_ABORT` in C. **Two clauses of that gate were not satisfiable as written and are corrected in the PRD rather than worked around.** (i) *"`_unc` across every kernel"* is already discharged by `cq_kd_case`, whose L3 runs on every case of every kernel under each of §9's four regions; a twelfth re-drive with hand-written call adapters would detect nothing. (ii) *"`_inv` for compare flags"* names a symbol **CQ_lang no longer emits** — `CompareLowering` emits the void in-place `_unc` twin (its bd `8txa`), and `_inv` appears on **0** lines of all 239 goldens against 21,323 `cq_template_icmp_*_unc` — so what is testable is the self-inverse PROPERTY, and the symbol family becomes **PRD §15 D14**. What Step 21 therefore builds is the axis's own claims: the asymmetry MEASURED, `dst ^= f` at a dst that is not zero, and the free | ~~(thin, in M26)~~ **no module; the free is M26's and is NOT thin — `PRD §15 D15`** | **DONE** — 12 cases + 2 deaths green in both configurations, **198 ctest tests** | 7 |

Step 20's re-run is why the controlled axis is an emitter mode (§0.3): it costs one
parameter in the kernel test driver, not twelve new suites. **Measured: the driver's own
change is one setter, one push site in `call_kernel` and one region loop, and none of the
71 driver call sites across twelve `.c` and eight `.inc` files moved.** What each suite adds
is a sweep body and a `CQ_CASE`.

**The controlled sweep is deliberately THINNER than the uncontrolled one, and it says so in
its own output** (`bd mmv`). The promotion is a property of the emitter — per gate, and
width-independent — so what the axis adds is its interaction with the §3 fold table, which
is exercised where the sample budget runs: the four regions run the sweep at
`W ≤ 5` (`W ≤ 4` for the barrel and `W ≤ 3` for K12, the two poles), and every shipped width
is still covered by `cq_kd_check_promotion` at two kernel calls apiece. Measured on this box
in Debug, kernel CPU across the eleven binaries went **171 s → 279 s (+63%)**, against the
**~3.8×** a naive full re-run of every mode at every width was estimated to cost; the pole is
still `test_kernel_shift_var`, 68 s → 95 s. Re-measure rather than quoting these (`bd 97s`).

### Phase D — shim, link, Grover

| Step | Red | Green | Gate | PRD |
|---|---|---|---|---|
| 22 | **LANDED 2026-08-22.** `test_gen_shim.py` (the GRID) **and `test_gen_bodies.py` (the BODIES), split on the module's own recorded seam** — generator round-trips the real `opcode_table.yaml`; **the emitted symbol SET reconciles with PRD §1's resolved partition** — **2479 = 992 integer wrappers + 603 integer `_inv` abort bodies + 884 fp abort bodies**, asserted as a set difference in both directions against the names expanded from the pinned `opcode_table.yaml` (a count is not an identification, and 20 same-size variant transpositions leave all three counts exact: swapping `controlled_inv_qq` ↔ `controlled_qq`, **82 each**, aborts 82 of the integer `_controlled` grid Step 20 built M06 for and turns nothing in the tree red — **this row said "the whole 214-symbol grid" until 2026-08-22 and was overstated by 132**: a single-token swap leaves `controlled_hl` (82) + `controlled_lh` (50) live, and aborting all 214 takes the COMPOSITE three-token swap, which is not one of the 20 and so needs its own negative control). **Scope is `opcode_table.yaml` ONLY** (PRD §1, decided 2026-08-14): CQ_lang's `intrinsic_table.yaml` (389) and `libm_table.yaml` (12) emit `cq_template_*` into the same link namespace and stay CQ_lang's, which is why Step 23's gate reads *from the opcode grid*. Do **not** delegate these figures to Step 0.5's row, which framed the question as the ~~1474~~ phantom; every fp symbol gets a named abort body — **and so does every integer `_inv` symbol (PRD §15 D14, `bd w1c`), with the integer abort set asserted to be EXACTLY the 603 `_inv` names** so nothing else is swept into the bucket unobserved. This is the gate D14 lands on, not Step 23's: an uncalled symbol produces no undefined reference, so a missing `_inv` body cannot turn `nm` red | M27 | Generator green | 8 |
| 23 | `test_runtime.c` — the PRD §2.1 families: `cqrt_copy_<W>_controlled`, `cqrt_rz_<W>_controlled[_inv]`, `cqrt_cswap` (constant ctrl = **0 gates**; quantum ctrl = Fredkin per bit) | M26, M28 | Full grid links; `nm` shows no undefined `cq_template_*` **from the opcode grid**. The 401 intrinsic/libm symbols come from CQ_lang's own archives and are deliberately *not* ours (PRD §1) — an unqualified "no undefined `cq_template_*`" cannot pass. **PLUS D15's MECHANISM, WHICH THIS GATE AS FIRST WRITTEN COULD NOT NOTICE AT ALL and which is now the largest thing M26 owes** (`PRD §15 D15`, `bd 06t`): the observed undo certificate and the three-valued free must be built and *exercised*, with every free landing in one of D15 §3's three rows rather than being reported as a single residue figure. A link check and a coverage percentage are both blind to a certificate that has degenerated into "always yes", so **the gate must carry the negative control**: the certificate must REFUSE `tests/test_unc_death.c:free_without_the_uncompute`, a rail with no `_unc`, no pair-off and no `addc` sum. Do **not** pin a corpus percentage here — the CQ_lang corpus is unpinned and D15 §0 records it moving repeatedly inside a single day, so any coverage figure is an order of magnitude and a ratio. **LANDING 1's FIRST INCREMENT IS IN (2026-08-22): the THREE-VALUED FREE PATH IS BUILT AND EXERCISED, WITH NO EVIDENCE PLUGGED IN.** `cq_reg_disposition` splits the verdict by SIGN, `cq_reg_clean` is its positive row, the act is per QUBIT, and `CQOPS_FREE_ABORT` is reachable without a rebuild — so every qubit-owning rail is UNPROVEN and strands, which is monotone (landing 2 adds evidence and converts strands to releases). **209 ctest tests green in both configurations.** What the gate still wants from this clause is the CERTIFICATE and the RESIDUE SPLIT; the negative control named above now asserts the VERDICT (`< 0`, proven dirty) rather than the act, and its sibling asserts `== 0`, so the two cases DISAGREE — before this they both asserted only "it aborts", and a certificate degenerated into "discharge nothing" would have left both green, which is the exact failure this control exists to prevent. **LANDING 1's STEP 3 IS IN (2026-08-23): `shim/cq_shim_ctx.[ch]` — 64 + 8 lines against a 150 budget — THE FIRST THING UNDER `shim/` CMAKE COMPILES.** The process-global `cq_ctx` and `cq_shim_ctx_reset`; the built-in sinks installed per (re-)init before `cq_ctx_init` (`bd utk`, CLOSED); the ONE §9 region bracket with `bd d6m` fix (a)'s comment and three arms (CLOSED); and PRD §1's `cq_shim_unsupported`, whose message is now read from the definition libcqops ships rather than from a stub the Python suite writes for itself. **219 ctest tests green in both configurations** (+1 suite, +9 deaths). It also ANSWERS the public-context ABI question `src/ctx.h` reserved by name for this step: `cq_ctx` stays INTERNAL, forced rather than chosen — see that header, `shim/cq_shim_ctx.h` and PRD §14's fourth correction. **LANDING 1's STEP 4 IS IN (2026-08-23): `shim/cq_runtime_rail.c` 199/220 AND `shim/cq_runtime_gate.c` 151/200 — libcqops NOW DEFINES 62 OF THE 173 `cqrt_*` SYMBOLS** (32 rail, 30 gate), plus `shim/cq_shim_proof.[ch]` (the free-time evidence, without which `cqrt_free` could not be written — M07 hard-errors on a NULL proof for any qubit-owning rail) and `shim/cq_runtime_abi.h` (CQ_lang's 173 declarations verbatim, because C has no name mangling). **35 cases + 37 deaths green in both configurations, 258 ctest tests.** It also BUILDS **PRD §15 D17** (`bd dzj`): `cqrt_addc` folds on an all-classical rail and otherwise runs M15's Cuccaro accumulator IN PLACE, never sandwiched, disposing its two transients through `CQ_ZERO_BY_CUCCARO_RESTORE` — the third and last `proven_zero` constant and the first outside `src/`. **What this step does NOT discharge:** the two remaining M26 files (`cq_runtime_v2.c`, `cq_template_impl.c`), the other 111 `cqrt_*` symbols, the link gate (step 8), and clause 3 — landing 2's certificate. **THE BATTERY WAS RE-RUN 2026-08-27** — it was launched on 2026-08-23 and its results were never read, the session having ended mid-run. **63 mutants proposed, 63 LANDED AND RAN (zero NOOP, zero NOCOMPILE, zero PERLFAIL), 51 KILLED IN BOTH CONFIGURATIONS, 4 REAL SURVIVORS, 8 deliberate equivalent controls alive, and no configuration asymmetry.** Each survivor was re-confirmed against the FULL 258-test suite in both configurations rather than the 49-test subset the battery ran for speed. **All four were predicted by name before the run and all four are TEST-SIDE holes, so no mutant can turn them green** (`bd pnu`, which now blocks this step): three are a live, load-bearing guard tested in only ONE direction (`gate_rot_width`'s width compare, both directions of `gate_ctrl`'s read/write accessor asymmetry, and `rail_w`'s resolve-before-compare order — whose own comment forbids the swap and for which the gate file has two cases and the rail file none); the fourth is the recorded masking-layer trap **introduced by a FIXTURE improvement rather than a code change**, where `one_bit()` now mints a real `CQ_BIT_Q` wire so M06 catches a coincidence one layer down with a string the case's `FAIL_REGULAR_EXPRESSION` does not name — the review had measured that same mutant as KILLED, so a mutant's verdict has a shelf life. **LANDING 1's STEP 5 IS IN (2026-08-27): `shim/cq_runtime_v2.c` 96/150** — all 109 symbols v1 defers, as loud `cq_shim_unsupported` bodies through twelve macros (`PRD §15 D16`; `bd vxk`, `bd r3y`, `bd ck6` CLOSED). **LANDING 1's STEPS 6, 7, 8 AND 9 ARE IN (2026-08-27), AND STEP 23 IS STILL NOT DONE** — its gate has three clauses, landing 1 discharges two, and clause 3 (D15's certificate, `bd 06t`) is untouched. **Step 6:** `shim/cq_template_impl.c` **232** and `shim/cq_template_dispatch.[ch]` **57 · 11** — the FIFTEEN remaining `cq_shim_*` entry points, with the recorded 240 seam TAKEN rather than deferred (the file came in at 270 against a 280 budget). **Step 7:** the ten `*.gen.c` listed EXPLICITLY in `CMakeLists.txt` — never `file(GLOB)`, which does not re-glob when a family file disappears — so `libcqops.a` now defines **2479** `cq_template_*` and **171** `cqrt_*` where `grep -c cq_template` on the archive was **0**. **Step 8:** the LINK GATE, `cmake/CqopsLinkWitness.cmake` **93** + `cmake/CqopsSymbolSets.cmake` **64** — pure CMake with no PyYAML in the path, the R3 `file(SHA256)` guard inside the generator, two EXTERNAL-LINKAGE address tables `_Static_assert`ed at 2479 and 171 and read at a RUNTIME index so `-Wl,-dead_strip` and plain `-flto` cannot fold them, and a target in `all`. **289 ctest tests green in both configurations, `make lint` clean, `third_party/` 0 modified.** **FIVE PROVOCATIONS OBSERVED, each naming its symbol**: delete one `.gen.c` body → link RED naming `_cq_template_add_i1`; delete one `cqrt_*` definition → link RED naming `_cqrt_qram_alloc_i8`; RENAME one definition → the archive still holds exactly 2479 and the link is GREEN, so only the set arm fires; ADD one out-of-grid definition → the link is GREEN and `test_link_smoke` PASSES, so again only the set arm; corrupt the manifest's sha → configure RED. Plus the negative control, the complete archive linking and the witness printing its two row counts. **TWO OF STEP 8's OWN REQUIREMENTS WERE NOT SATISFIABLE AS WRITTEN**, measured rather than worked around: `docs/cqrt_census.txt` holds 98 distinct `cqrt_*` TOKENS, most of them PREFIXES, and never spells the 173 expanded names, so table 2 reads `shim/cq_runtime_abi.h` — with the census cross-check already living in `tests/test_runtime_abi.inc`, both directions — and CMake's regex has no `{n}` repetition, so the obvious sha extraction matches `a256` out of the word `yaml-sha256` itself. **THE BATTERY: 37 mutants over four rounds, 34 KILLED IN BOTH CONFIGURATIONS, 2 deliberate equivalents alive, 1 made UNREPRESENTABLE.** All three round-1 survivors were real: `tpl_out`'s WRITE door was swappable because `out` was resolved a second time at the kernel call, and is now fixed STRUCTURALLY (it returns the pointer, so the swap does not compile — verified by applying it); the predicate fixture drove ONE operand pair, and `ult`/`ule` agree at every pair where the operands differ, so it now sweeps three including `a == b`; and `tpl_src`'s resolve-before-compare order is a GENUINE equivalent, established by the PAIRED mutation rather than left untested, because `cq_reg_check_operands` runs first and makes the tombstone row unreachable **LANDING 2 IS IN (2026-08-27) AND CLAUSE 3 IS DISCHARGED, SO STEP 23 IS DONE.** `shim/cq_shim_record.[ch]` **192 · 62**, `shim/cq_shim_reduce.[ch]` **112 · 8**, `cq_shim_certificate` + `cq_shim_free_proof` in `shim/cq_shim_proof.c` **57**, `tests/test_shim_cert.c` **199** + `tests/test_shim_cert_death.c` **40**, and the RESIDUE SPLIT in `src/reg.[ch]` + `src/ctx.[ch]` (`reg.c` **238** against its 260 trigger, so the reserved `src/reg_free.c` seam was NOT needed). **`shim/cq_shim_record.c`'s OWN reserve seam, recorded at landing rather than when it bites: `the STREAM ↔ the EFFECT TABLE`** — the append-only call log, the per-handle index and the parity counter stay; the `EFF` table with its `controls ⊆ reads` and completeness asserts moves to `shim/cq_shim_effects.[ch]`. Discriminator: a line that answers to `cq_runtime.h` goes with the table, a line that answers to how a stream is stored stays. Trigger **240**; it is at 192, and v2's 63 `qram` and 11 `tape` rows are what would take it. `cqrt_free` installs the certificate AND the shadow, DIRTY dominating. **290 ctest tests green in BOTH configurations, `make lint` clean, `third_party/` 0 modified.** **BEFORE ANY CODE: RULE 1 WAS NOT SATISFIED FOR THE PORT `bd 06t` MANDATES.** None of the four upstream guards it names appeared anywhere under `third_party/` — they are in CQ_lang, which this repo does not pin — so the analysis was VENDORED to `third_party/cq_free_pairing/` with its own COMMIT and a configure-time sha guard (`cmake/CqopsFreePairingPin.cmake`, five arms provoked against a SCRATCH COPY of the tree, never the pinned bytes). Reading the pin immediately caught two port bugs no citation could have: upstream masks the flag parity **`& 1`**, and freezes it on **control slots only**. **THE ENGINE IS ONE ENGINE BY A RECORDING CHOICE**: the `cq_template_*` forward is recorded as a WRITE to the rail it mints with its `_unc` as the declared twin, so U1 needs no matching step and upstream's T2 falls out of the operand check. **THE BIRTH VALUE decides the sign**, which is the port's one divergence from upstream's OBLIGATION (upstream proves a *known classical basis state*; Rule 6 needs `|0⟩`) and is what makes `ckd.18` a CONVICTION. **THE BATTERY: 45 mutants over three rounds, both configurations, 43 KILLED IN BOTH, 1 recorded ACCEPT ARM alive, 1 NOOP.** Four round-1 survivors were real holes and one failed in the UNSOUND direction — deleting the `cqrt_cswap` QUANTUM-row record leaves a Fredkin-written rail with an empty history that reduces vacuously and RELEASES. Fixing it also corrected a MODELLING error a test found: `cqrt_cswap`'s three rows are three different physical events, and the first draft recorded a write on all of them — row 0 does nothing and row 1 relabels ownership for zero gates, so only the histories move. `co_written_stable`'s `v == target` skip is the one live ACCEPT ARM with no killer at the v1 op surface, recorded at the site with the reason rather than deleted | 8 |
| 24 | **L6** — link CQ_lang's existing fixtures against `libcqops` and RUN them (**PRD §15 D18**; ~~diff emitted traces~~) | — | **THE FIXTURES LINK, RUN, AND DO NOT ABORT — NORTH_STAR condition 1 verbatim. "Traces match" is RETIRED (`bd 590` CLOSED 2026-08-27 as PRD §15 D18).** It named an oracle that stops existing the moment condition 1 is satisfied: the goldens are CQ_lang's regression oracle for CQ_lang's OWN IR pass, captured against a *"trace-only runtime stub"* that the real backend replaces. **AND `bd 590`'s OWN FALLBACK ORACLE WAS MEASURED FALSE BEFORE IT WAS ADOPTED**: it proposed carrying correctness on `cqrt_measure_*` return values "checkable against the goldens' measure lines", but every stub measure body prints a LITERAL and returns `false`, so all **266** measure lines across **244** goldens read `-> 0` whatever the circuit computes — a diff that passes vacuously where the true value is 0 and fails where it is not, with the failure being US being right. A second, independent reason the goldens are not an oracle in ANY column: **our handle numbering already diverges on purpose** — `cqrt_addc`'s two transients and D7b's defensive copy mint rails the ABI does not name, so the D5 counter runs ahead of the stub's (measured in Release, recorded at `shim/cq_runtime_rail.c`). **WHAT CARRIES CORRECTNESS IS L1–L5, WHICH ARE GREEN TODAY IN BOTH CONFIGURATIONS; what this step adds is the one claim they cannot make** — that the frozen ABI's 173 + 2479 symbols resolve against a real backend and that a real CQ_lang program drives them end to end without aborting. The D15 §3 **residue report** is read alongside as an OBSERVATION, never as a gate. **CANDIDATE (b) — pinning OUR gate stream against OUR OWN goldens — was weighed and NOT taken**, and is filed for v2 rather than refused: trace goldens churn on D4 free-list and D6 non-demotion changes, which is **risk R5** and is exactly why kernel goldens pin COUNTS. This row's "run" was also conditional on `PRD §15 D15` §4's last clause, CONFIRMED 2026-08-22 as (b) STRAND, so that condition is discharged too: a provably-dirty free strands rather than aborting, nothing is laundered, and the shipped fixtures those rails live in run. Reading (a) — hard error, taking some of them with it — is preserved in D15 §4 as the argument, not as a live option, and `CQOPS_FREE_ABORT` gives a maintainer it on demand. **`bd 493`'s acceptance lands here**: its code shipped at Step 23.6 and what remains is its ten integer-surface fixture lines running end to end, seven of which are reachable. **LANDED 2026-08-27. MEASURED AGAINST CQ_lang `134e625` (2026-08-27), tracked tree clean, 249 goldens — CQ_lang is NOT pinned by this repo, so carry the SHA and the ratios, never the counts.** Of its **250** registered e2e fixtures (230 single-file + 20 multi-file, read out of `ctest --show-only=json-v1` rather than out of its CMakeLists), **every one LINKED and RAN**: **43** ran to completion with exit 0, **207** aborted at a LOUD, NAMED v1 deferral — 168 fp, 27 `qram`, 6 `tape`, 6 `cqrt_alloc_handle` (PRD §15 **D16**; the six are the purely-integer INTRINSIC-bearing fixtures, exactly the six `bd 216`'s recon predicted) — and **0** failed to build or link, **0** aborted for a reason we do not own, **0** failed at run time and **0** linked against a CQ_lang archive. The casualty list is fp- and intrinsic-bearing and nothing else; `bd 216`'s "≥11 of 52" is superseded. **THE RUNS ARE NOT VACUOUS**: through the printf sink `slice_i128_mulhi` emits **204,113** gates (131,008 `cx` + 72,960 `ccx` + 17 `x`, plus 64 `ry` and 64 `mz`) and `control-seq-grover` 2,374 including 64 `ry` + 64 `rz` + 32 `mz`. **`bd 493` is CLOSED**: 7 of its 7 reachable aliased lines ran, across three opcodes and both doors, and its own correction (3) is sharpened — the stopper in `slice_select_rail_alias_cond` is `cqrt_alloc_f64` at golden line 15, not `fcmp_olt_f64` at 18. **THE RESIDUE, read as an OBSERVATION**: 13 of 250 fixtures printed D15 §3's one-shot `STRANDED` line, 12 of the 43 that complete — so the certificate discharges every rail in **31 of those 43**. Harness: `tools/l6/run_slice_cqops.sh` (CQ_lang's four-stage pipeline with ONE line changed, the link) + `tools/l6/l6_run.py`, registered as an OPT-IN ctest entry behind `-DCQOPS_CQLANG_DIR=` because this repo neither pins nor can build CQ_lang | 8 |
| 25 | **L7** — Grover per PRD §12. (a) compiles through `cqc`, links, emits a gate stream; (b) **classical mode**: `M_PI/2 → M_PI` runs deterministically and `cq_measure` returns what plain C computes; (c) counter sink reports Toffoli count and T-count, **peak qubits from `cq_qubits_peak()` rather than from the sink** (PRD §8 correction), stable across runs and pinned | — | **v1 done** | 8 |

### Phase E — optional

| Step | Work | PRD |
|---|---|---|
| 26 | **LANDED 2026-08-28.** `sink_qec` against `C_quantum_error_correction` — whose library is its `qec/` subdirectory, `libqec.a` + `qec/qec.h`, NOT an artefact bearing the project name; pool ceiling wired to `qec_n_logical` (D2 — and D20 measures that ceiling as a HARD one, n_logical ∈ {1,3,4,6,7} across the shipped configs, with the derived code distance RISING as it grows). **This step is also the ONLY route to a circuit drawing** — the drawer is the QEC repo's and this repo grows no second one (Rule 13); D20 measures what is visible through it (Toffoli-free at small width, yes; one logical CCX is 562,564 physical gates at d = 3); `ry`/`rz` built rather than stubbed per D19 | 14. **PULLED AHEAD OF STEP 25 on 2026-08-28**: the `k26 → kt9` edge was scheduling, not technical — M25 needs M04's registry (Step 5), M22 (Step 19) and D2's `cq_qubits_set_ceiling` (Step 4), nothing from Grover — and the qec sink plus `bd 76r`'s annotations (D21) are how this repo's work is INSPECTED, which is worth having before the acceptance gate rather than after. The install hook now sets TWO pool modes, ceiling AND no-recycle (D21 (b)). **WHAT LANDED**: `src/sink_qec.[ch]` + `src/sink_qec_angle.[ch]` on the recorded seam; M03 gained a third disposition, RETIREMENT — `cq_qubits_set_recycle` plus its own mark and counter, kept apart from stranding because a retired qubit WAS proven `|0⟩` and `bd 06t`'s residue report must not call it a leak; two calls in `shim/cq_shim_ctx.c` bracketing `cq_ctx_init`; and suites in both build arms — `test_sink_qec_angle` (+4 deaths) always, `test_sink_qec` (+4 deaths) only under `-DCQOPS_QEC_DIR=`, plus one cross-check in `test_sink.c` that runs in BOTH. **307/307 Debug and 306/306 Release with the library, 301/301 Release without it.** Rule 17: the L6 corpus was not re-run and no golden moved. **Two document claims were MEASURED FALSE and corrected in place**: D19's *"π/4 costs ONE T gate"* (it costs 1,200, the same as the 2^62 convergent — this driver's degenerate branch catches multiples of π/2, which are exactly §7's folding rows), and a motive for no-recycle that reached two header comments before being checked — `peak == minted` means D2's ceiling already bounds the INDEX with recycling ON |
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
| R6 | `_unc` gate count legitimately exceeds forward (PRD §10, consequence ii) | Someone "fixes" the asymmetry by asserting equality | **CLOSED at Step 21, and the mitigation as written was NOT ENOUGH.** The §10 note is quoted in `tests/test_unc.c`, as this row asked — but a quotation is not a detector, and "pins the two separately" was **vacuous**: L4 measures at the all-quantum mask, the FIXED POINT of the drift, so all 399 pinned `(kernel, W)` golden pairs are equal and the `pass` column could never have gone red. `tests/test_unc_asym.inc` is the fixture where they differ — **18** kernels x `W in {1,4,8}`, the delta pinned as a per-kind TUPLE at each (a total is not an identification, Rule 10), and a classical ONE lane as the control for the cause: it keeps the TOTAL and **promotes** each gate one control level, `X -> CX` and `CX -> CCX`. The durable form is an EQUALITY at the right mask: `count(unc) == count(a fresh forward at the drifted mask)`, 252 fixtures, reading no golden. The extreme row is `an_all_classical_forward_is_free_and_its_uncompute_is_not` — L5's own mask, where the forward costs **0 gates and 0 qubits** and one drifted lane makes the uncompute the entire sandwich |
| R7 | Generated shim inflates compile time | Slow builds from Step 23 | One `.gen.c` per opcode family; the generator already splits |

---

## 7. Definition of done

v1 ships when NORTH_STAR's five conditions hold, each traced to a step:

| # | NORTH_STAR condition | Step |
|---|---|---|
| 1 | **MET 2026-08-27** — CQ_lang's fixtures link against `libcqops`, **RUN, and do not abort** (`PRD §15 D18`). Measured against CQ_lang `134e625`: all **250** registered e2e fixtures linked and ran, **43** to completion and **207** stopping at a loud, named v1 deferral, with **0** build/link failures, **0** unexplained aborts and **0** provenance failures, identical in both configurations. Re-run it with `-DCQOPS_CQLANG_DIR=`; CQ_lang is not pinned here, so re-measure rather than quoting. ~~Not unconditional: `PRD §15 D15` §4 leaves open whether a *provably dirty* free aborts or is stranded~~ — **that clause was CONFIRMED 2026-08-22 as (b) STRAND, so this row is unconditional now**: proven-dirty and unproven alike are stranded, nothing is laundered, and the fixtures those rails live in reach the end. Under `CQOPS_FREE_ABORT`, a development flag rather than the default, this condition is unreachable by construction — never run L6 with it on and then report the abort as a libcqops defect | 24 |
| 2 | **Correct** — every integer opcode differential-tested against C semantics at every width on its shipped ladder, over a small constant number of seeded random `(bit-kind mask, value)` samples per width, with the all-classical and all-quantum masks and the value corners forced into every draw | 10–17 (L1) |
| 3 | **Clean** — after every template call the pool holds exactly the result rail's qubits; after `_unc`, nothing | 10–17 (L2), 21 (L3) |
| 4 | **Grover** — ordinary C compiles through `cqc`, links, emits a gate stream whose oracle arithmetic is verified exactly in classical mode | 25 |
| 5 | **Hardware** — one flag routes the same stream into `qec_*` | 26 |

Plus the two this plan adds: `make lint` green (no hand-written module over 300 lines),
and every kernel traceable to a `docs/constructions/K*.md` spec extracted from a pinned
Bennett.jl commit.
