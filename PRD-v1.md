# PRD — `libcqops` v1: the integer opcode surface

Status: draft for review · Target: CQ_lang integer templates, Grover end-to-end
Companion: [`NORTH_STAR.md`](NORTH_STAR.md)

---

## 0. Sources of record

This document cited both of its upstreams by name and neither by URL. Both are now pinned
on disk (Step 0.1, Step 0.3); the pins are what make the L4 goldens attributable (risk R3).

| Upstream | URL | Pinned at | On disk |
|---|---|---|---|
| **Bennett.jl** — the sole source of circuit constructions (constraint 4) | <https://github.com/tobiasosborne/Bennett.jl> | `980805de85314b3da7ac25cf6454b56566f8e609` (`main`, 2026-08-14) | [`third_party/bennett/`](third_party/bennett/) + [`COMMIT`](third_party/bennett/COMMIT) |
| **CQ_lang** — the frozen ABI we satisfy | local: `/Users/sorenwilkening/Desktop/CQ_lang` | `a6a92feb094c8dd71c36d47c24460f3f0df67b44` (2026-08-13) | [`third_party/cq_lang/`](third_party/cq_lang/) |

Bennett.jl is vendored as a **snapshot**: upstream's `.git/` (322 MB) and `.beads/` (129 MB)
are stripped, every other path is byte-identical to the pinned tree, and
`third_party/bennett/.provenance/MANIFEST.txt` lists all 2317 upstream paths with their blob
hashes so the snapshot is independently verifiable against a fresh clone. Re-vendoring is a
deliberate act: it invalidates every L4 golden, which is why goldens carry this SHA in a
header comment.

`third_party/cq_lang/opcode_table.yaml` is a **verbatim mirror of a frozen ABI**. It is
never edited here. To pick up an ABI change, re-copy it at a new pinned CQ_lang revision.

The extracted construction specs — one per kernel K1–K12, each with a gate-count formula in
`W` — live in [`docs/constructions/`](docs/constructions/). Those formulas *are* the L4
goldens; a kernel with no spec has nothing to assert against.

### Provenance — what is **not** from Bennett

Constraint 4 says Bennett.jl is the sole source of circuit constructions, and Rule 1 says
port rather than re-derive. Both are about **circuits**. A large part of this library is not
a circuit and has no upstream at all. Without the inverted list a later reader cannot tell
which behaviour is bound by upstream (and so must not drift) from which is ours to change
freely — and, worse, may go looking in Bennett for something that was never there.

**The dividing line is sharp, and it is checkable in one line.** Bennett's gate operands are
`const WireIndex = Int` — `NOTGate{target}`, `CNOTGate{control,target}`,
`ToffoliGate{control1,control2,target}` are *all* plain wire integers
(`src/gates.jl:1-22`). **Bennett has no constant/classical operand kind whatsoever.** Every
mechanism below exists because we introduced one.

| Mechanism | Ours or ported | Note |
|---|---|---|
| **§3 fold table** | **ours, entirely** | Bennett cannot have one: no classical operand kind exists upstream. This is why Rule 1 does *not* apply to M05 — do not go looking for it in Bennett |
| **Operand distinctness asserts** | **ours** | A grep for distinctness assertions across all of Bennett's `src/*.jl` returns **nothing** |
| **Tri-valued `cq_bit`; I1, I2, I4, I5** | **ours** | The whole representation |
| **The two-bit shadow** (`{value, unknown}`) and its update rules | **ours** | Bennett has a real simulator; we deliberately have none (constraint 3) |
| **Handle table, tombstones, monotonic handles** (D5) | **ours** | Matches CQ_lang's `h<N>` trace convention, not anything upstream |
| **Qubit pool, LIFO free list, ceiling, I3** | **ours** | Bennett's `WireAllocator` is lowest-index-first; D4 chose LIFO for quieter trace diffs |
| **§7 rotations and the θ ≡ π asymmetry** | **ours** | Bennett is purely classical-reversible; `Ry`/`Rz` have no upstream |
| **§8 sink vtable and all three sinks** | **ours** | Streaming emission (constraint 3) is our architecture; Bennett builds a circuit object |
| **`cqrt_cswap` / Fredkin, incl. the 0-gate constant-control case** | **ours** | Named by CQ_lang's ABI, not by Bennett |
| **Nested-control AND into one wire** | **ours** | The *promotion it feeds* is Bennett's; collapsing two controls to one is ours |
| **Measurement (`mz`, terminal semantics)** | **ours** | CQ_lang's contract |
| — | — | — |
| **§9 controlled promotion** (`NOT→CNOT`, `CNOT→Toffoli`, `Toffoli→` 3-Toffoli + shared ancilla) | **ported, verbatim** | `src/controlled.jl:114-127` |
| **K1–K12 gate sequences** | **ported** | Per-kernel citations in `docs/constructions/` |

**The sandwich (§5) is a subtle case — the algebra is Bennett's, the granularity is ours.**
Upstream applies forward → copy-out → reverse **once, globally**, at
`src/bennett_transform.jl:343-358`, sized `2·|gates| + |output_wires|` — exactly the `2F + W`
shape. We apply that identical algebra **locally, per kernel**, because we have no global
wrap to inherit. So do not "correct" a kernel toward Bennett's structure: for a single-kernel
expression the two coincide exactly, but across `n` kernels Bennett pays `2·Σ Fᵢ + W` while we
pay `Σ (2Fᵢ + Wᵢ)` — strictly more, and deliberately so. The `cq_sandwich` **driver** and
invariant **I6** are ours (`IMPLEMENTATION_PLAN.md` §0.1–§0.2).

---

## 1. Scope

### In scope

The **integer** half of CQ_lang's frozen ABI, at widths `i1, i8, i16, i32, i64, i128`:

| Family | Symbols | Source construction |
|---|---|---|
| Core runtime | `cqrt_alloc/measure/free`, `cqrt_x/cnot/toffoli`, `cqrt_copy_<W>`, `cqrt_cswap`, `cqrt_addc/xorc_<W>`, `cqrt_ry/rz_<W>` | this repo |
| Core runtime, **controlled** | `cqrt_copy_<W>_controlled`, `cqrt_rz_<W>_controlled`, `cqrt_rz_<W>_controlled_inv`, **`cqrt_ry_<W>_controlled`** *(new 2026-09-10 — see the note)*, **`cqrt_ry_<W>_controlled_inv`** (5 integer widths each), **`cqrt_x_controlled`**, **`cqrt_cnot_controlled`** | §2.1, §15 **D16** |
| Binary arith | `add sub mul sdiv udiv srem urem` | Bennett `adder.jl`, `multiplier.jl`, `divider.jl` |
| Binary bitwise | `and or xor shl lshr ashr` | Bennett `lowering/arith.jl` |
| Compare | `icmp` × 10 predicates | Bennett `lower_eq!/ult!/slt!` |
| Casts | `sext zext trunc` (int↔int) | Bennett `lower_cast!` |
| Shapes | `qq`, `hl`, `lh` | free — a literal is an array of constant bits |
| Axes | forward, `_unc`, `_controlled` — plus `_inv` / `_controlled_inv`, which on the **`cq_template_*` DATA grid** are **generated and refused** rather than implemented (§15 **D14**: there `_inv` is `f⁻¹`, and `f⁻¹` does not exist for the non-injective opcodes) | §9, §10, §15 |

> **THE `_inv` ROW MEANT TWO THINGS AND THE TABLE READ AS THOUGH IT MEANT ONE
> (corrected 2026-08-23, Step 23 landing 1 step 4).** The Axes row said `_inv` and
> `_controlled_inv` are "generated and refused", and the Core-runtime-controlled row lists
> `cqrt_rz_<W>_controlled_inv` as IN SCOPE. Both are right and they are about different
> families: §15 **D14**'s whole finding is that the suffix names two unrelated things — on
> the 915 `cq_template_*` DATA symbols it is a fresh-minting inverse OPERATION that does not
> exist for `and`/`or`/`udiv`/`trunc`/`icmp`, and on the 16 controlled ROTATIONS it is
> **θ-negation**, which is total and which D14 says in terms is "Step 23's to implement".
> The Axes row is now scoped to the data grid. Three core symbols were also in NEITHER list
> and are added above — `cqrt_ry_<W>_controlled_inv`, `cqrt_x_controlled` and
> `cqrt_cnot_controlled` — which is what §15 **D16**'s capability rule settles.

> **THE PLAIN `cqrt_ry_<W>_controlled` EXISTS NOW, AND THIS ROW SAID IT DID NOT
> (`bd w9i`, ABI re-vendored 2026-09-10 at CQ_lang `170ede1`).** The row read *"there is NO
> plain `cqrt_ry_<W>_controlled` at any width"*, quoting `cq_runtime.h`'s own note, and that
> was a faithful reading of the ABI **as vendored**. CQ_lang `f92d95e` (2026-09-02) added the
> family at **all nine widths** and retired the note with it. The re-vendor is **purely
> additive** — 173 → 182 declarations, nine pure insertions, **no existing signature moved**,
> and `opcode_table.yaml` is byte-identical, so the 2,479 `cq_template_*` grid of §2.2 did not
> move at all.
>
> **THE DISPOSITION IS §15 D16's CAPABILITY RULE, FAMILY FIRST AND WIDTH SECOND, AND IT SPLITS
> THE NINE 5/4.** The family `ry` is in v1 scope, so the **five integer widths are
> IMPLEMENTED** in `shim/cq_runtime_gate.c` — literally the `_controlled_inv` body without its
> `-angle`, same `cq_shim_region` bracket, same `cq_rotate_ry`, and the same honest qualifier
> that under a **quantum** control §7's folding Ry rows hit **D11**'s refusal, exactly as
> `cqrt_ry_<W>_controlled_inv` already does. The **four fp widths are loud aborts** in
> `shim/cq_runtime_v2.c`, joining `cqrt_ry_f<W>` and `cqrt_rz_f<W>_controlled`.
>
> **AND THE fp HALF IS UNREACHABLE, NOT MERELY DEFERRED — which is why "implement all nine
> uniformly" was weighed and NOT taken.** `cqrt_alloc_f<W>` aborts, so no fp rail handle can
> exist at runtime in v1 and there is nothing to hand an implemented body. Such a body would
> call `cq_rotate_ry`, resolve the handle through M07, and land on M07's **generic** handle
> error instead of `cq_shim_unsupported`'s named-symbol one: strictly worse diagnostics, a
> forward implemented where its own `_inv` is not, and the only implemented fp rotation of any
> kind. The abort's reason string is `"fp is v2"`, which is precisely the claim being made —
> and it is the claim that becomes an implementation when fp lands.
>
> **THE AXIS IS STILL NOT A UNIFORM CROSS PRODUCT.** Measured from the declarations at
> `170ede1`: Ry is 9 forward + **9 controlled** + 7 `controlled_inv` = **25** (the `_inv` half
> still lacks f16 and f80), against Rz's 9 + 9 + 9 = 27. Minting the two missing `_inv`
> symbols to tidy the grid would invent symbols the frozen ABI does not declare.

> **`cqrt_h` is struck from this row (Step 0.7 — resolved).** Earlier drafts listed
> `cqrt_x/h/cnot/toffoli`, which contradicted three other places: constraint 1 below forbids
> `H` outright, §8's sink vtable has no `h` entry, and §12 builds `H` out of rotations. The
> evidence settles it and none of the three suspected causes was right. `cqrt_h` **is**
> declared (`cq_runtime.h:226`) and defined (`cq_runtime.c:454`) in CQ_lang — but it is
> **emitted by nothing and called by nothing**: no site in `ir-pass/`, no `.ll` fixture, no
> test, and CQ_lang's own link witness takes the address of `cq_template_*` symbols only.
> `include/CQ.h` exposes exactly three primitive families — `cq_theta` (Ry), `cq_phi` (Rz),
> `cq_measure` — so a CQ program has **no way to write a Hadamard except** §12's composite.
> (`cq_phi` **stopped being an `Rz` on a REGISTER on 2026-09-05** — it is now a
> register-free BRANCH phase, `void cq_phi(double)`, lowering by kickback onto a minted
> flag; `bd cxp`, and §12's note (iii). **The conclusion here is unaffected and if anything
> stronger**: an intrinsic that takes no register is even less able to spell a Hadamard on
> one, so §8's six-entry vtable and §12's Grover-from-rotations both still follow.)
> It is the over-declaration phenomenon §2.1 already names. Consequences: §1's constraint 1
> stands, §8's 6-entry vtable is **complete and correct as printed** (M04 may freeze it in
> Step 5 with no `h` entry), and §12's Grover-from-rotations is **forced, not a choice**.
> Omitting `cqrt_h` is not a link failure, because an unreferenced declaration produces no
> undefined reference. Recorded so it is not re-litigated: if CQ_lang ever turns on emission,
> a *link* error is the failure mode we want — it fires at build time, names the symbol, and
> cannot produce a wrong circuit. Do not paper over it with a shim stub.

That is the *entire* purely-integer surface, realised by roughly a dozen kernels.

**The symbol counts, recounted (Step 0.3 — supersedes every figure in earlier drafts).**
Three different totals appeared across this document and CQ_lang's own tooling: **1455**,
**1474** and **1732**. All three are wrong for the pinned revision. Counted from
[`third_party/cq_lang/opcode_table.yaml`](third_party/cq_lang/opcode_table.yaml) at CQ_lang
`a6a92fe`, cross-checked three independent ways (direct count of the shipped declaration
layer; by-construction recount replicating the generator's own enumeration; and CQ_lang's
generated witnesses `cq_link_smoke.c` and `core_abi_link_check.py`):

| Figure | Value | Status |
|---|---|---|
| Total `cq_template_*` from `opcode_table.yaml` | **2479** | current |
| fp-touching (out of v1) | **884** | current |
| purely-integer, **including** i80 | **1595** | current |
| purely-integer, **excluding** i80 | **1455** | current — see the i80 scope note below |
| ~~1732~~ | — | stale comment (`opcode_table.yaml:4`, `gen_templates.py:5`); was the correct *total* on 2026-06-20, never an integer count |
| ~~1474~~ | — | **phantom.** Appears at no revision as either a total or an integer count, and matches no alternative partition. A transcription slip from 1455 while drafting §13/§14. Nothing to reconcile — deleted |
| ~~1455 + 878 = 2333~~ | — | a *correct snapshot* of revision `ce3837bc` (2026-07-06), stale by the two i80 increments of 2026-07-15 |

Beware when counting: **240** of the 884 fp-touching symbols are the cross-domain casts
(`sitofp`, `uitofp`, `fptosi`, `fptoui`, `bitcast`), whose names carry *both* an integer and
a floating-point width. They look integer-ish and are not. (This figure was 234 at the
revision §1 was originally written against.)

> **i80 is IN scope — the grid is 1595 (decided 2026-08-14).** The gap between 1595 and 1455
> is exactly the **140** i80 symbols, which is why the original 1455 still looks right: it
> predates i80. `i80` is the `x86_fp80` *bit-pattern* width, an intermediate that exists
> solely to serve `long double` libm — explicitly v2. But all 140 are `and/or/shl/lshr/icmp`,
> `trunc`-from-i80 and `zext`-to-i80: **pure classical permutations already inside v1's kernel
> set**, and I5's width-genericity makes an 80-bit register cost nothing structurally. 80 is
> not a power of two, which is worth a moment's attention when reading a kernel — but every
> kernel loops over `reg->width` and none switches on it, so nothing special is needed.
> Including them avoids a whole class of link failure for free. **The shim grid is 1595.**

> **The two sibling tables are OUT of v1 scope (decided 2026-08-14).** `opcode_table.yaml` is
> only **one of three** tables `gen_templates.py` drives (`:390-398`). `intrinsic_table.yaml`
> (**389** symbols) and `libm_table.yaml` (**12**) emit `cq_template_*` into the *same link
> namespace*, making CQ_lang's true `cq_template_*` link surface **2880** — a figure CQ_lang
> asserts itself at `cq_link_smoke.c:2932`. M27 generates from `opcode_table.yaml` **only**;
> CQ_lang keeps linking its own archives for the other **401**. Consequence for Step 23,
> which must be worded accordingly: its gate is *"no undefined `cq_template_*` symbol **from
> the opcode grid**"*, **not** "`nm` shows no undefined `cq_template_*`" — the latter cannot
> pass while 401 symbols come from elsewhere. If that ever becomes inconvenient, the
> alternative is to vendor both siblings at pinned revisions and widen M27; it is a bigger
> v1 than this document scopes.

**i128 is in scope but is a narrow commitment.** It costs nothing beyond width-genericity
(§2.2), and CQ constrains it hard: 215 symbols over 16 opcodes
(`add sub mul sdiv udiv srem urem and or xor shl lshr ashr` + `sext zext trunc`), with
**no `icmp` at i128** and **no `cqrt_*` core symbol at i128 at all** — no
`cqrt_alloc_i128`, no `cqrt_measure_i128`, no `cqrt_copy_i128`, because the C ABI shreds
`__int128` into `{i64,i64}` at a function boundary. An i128 register can therefore only
be *born from* a `zext`/`sext` and *die at* a `trunc`; it is never allocated, measured,
copied or compared. The only place `__int128` reaches a signature is the `_hl` shape
(`cq_template_add_i128_hl(int32_t, __int128)`) — a compiler extension rather than C11,
but available on every target CQ_lang already requires.

### Out of scope for v1

- **All floating-point widths** (`f16/f32/f64/f80`, **884** symbols — the figure in the
  table above, not the ~~878~~ of the `ce3837bc` snapshot; the six that separate them are
  `bitcast_f80_to_i80{,_inv,_unc}` and `bitcast_i80_to_f80{,_inv,_unc}`) — **v2**, via Bennett's
  `src/softfloat/` branchless soft-float suite. `gen_shim.py` still emits a body for each
  of the 884: a loud abort naming the symbol, so the link always succeeds and an fp
  program fails with `cqops: cq_template_sitofp_i32_to_f64 not implemented (fp is v2)`
  rather than an undefined-reference wall. Free diagnostics, and it makes the v2 boundary
  visible at runtime instead of at link time.
- ~~**`cqrt_tape_*`** (reversible I/O tape) — no consumer until `printf` on tainted data.~~
  **IN SCOPE SINCE 2026-09-02 AS v1.1 — §15 D23.** The stated reason was FALSE: six shipped
  fixtures ARE that consumer (`slice_io`, `slice_io_both_arm`, `slice_io_chained_theta`,
  `slice_io_controlled`, `slice_mixed_value_io`, `slice_control_abort` — measured at Step 24
  and re-derived 2026-09-02 against CQ_lang `893b769`, dirty tree), otherwise v1-integer-clean
  and stopping at `tape is v2`. A tape write is `cqrt_copy_<W>` into a fresh `|0⟩` rail that is
  KEPT — never freed, never uncomputed by CQ_lang — and the tape handle is a classical token
  owning zero qubits. No construction to port, no new gate, D15's certificate untouched.
- ~~**`cqrt_qram_*`** — stretch increment (§13, ~~Increment 8~~ Increment 9 — §1 said 8 and the §13 table said 9; the table is right, 8 is the shim). Grover does not need it.~~
  **IN SCOPE SINCE 2026-09-02 AS v1.2 — §15 D24 (`bd 9zq`, Step 27).** The stretch is
  taken: all **63** `cqrt_qram_*` symbols at all nine widths are implemented
  (`shim/cq_runtime_qram.c` over `shim/cq_shim_qram.[ch]`, K13 `src/kernels/qrom.[ch]`,
  K14 `src/kernels/qstore.[ch]`). An fp-width cell is a BIT PATTERN of that width — the
  ABI's own words (`cq_runtime.h`, `bd 38t2`: "fp-as-bitpattern cell") — and what keeps
  those symbols unreachable on the corpus is v2's fp CORE, not the array. Grover still
  does not need it.
- **Error correction.** We call the QEC library; we do not implement any part of it.
- **Gate-level optimisation** (cancellation, commutation, peephole fusion).

### Non-negotiable constraints

1. Classical opcodes emit **only** `X`, `CX`, `CCX`. No `H`, no `T`, no multi-controlled
   gates above 2 controls.
2. Every routine is **ancilla-clean** on return.
3. No circuit data structure; no statevector.
4. Bennett.jl is the sole source of circuit constructions.

---

## 2. Data model

### 2.1 Core symbols that are easy to miss

Three hand-written core families are emitted by the pass **today** and are not optional —
omitting them is a link failure, not a missing feature. Verified against the pass source,
not against the header:

| Symbol | Emitted by | Lowering |
|---|---|---|
| `cqrt_copy_<W>_controlled(ctrl, src, dst)` | `ControlledSymbols.cpp:106` — `SelectMergeEmit` (tainted `select`), `SwitchDataMuxEmit`, `Specialization` | `dst ^= src` under `ctrl`: one `CCX(ctrl, src_i, dst_i)` per bit |
| `cqrt_rz_<W>_controlled[_inv](ctrl, h, θ)` | `ControlledSymbols.cpp:32`, `RotationLowering.cpp:154` | controlled `Rz` per qubit; `_inv` negates θ. Sink-level, not decomposable in `{X,CX,CCX}` |
| `cqrt_cswap(ctrl, a, b)` | `SwapRouteEmit` (Phase 4 routing) | **constant `ctrl`**: swap the two `cq_bit` arrays, 0 gates. **quantum `ctrl`**: Fredkin per bit — `CX(b,a); CCX(ctrl,a,b); CX(b,a)` |

A tainted `select` is the common path into `cqrt_copy_<W>_controlled`, so this is
reachable from ordinary C, not an exotic corner. Before Increment 8, re-run
`grep -rhoE '"cqrt_[a-z0-9_]*"' ir-pass/src` against the CQ_lang revision being targeted —
the live set is owned by `ControlledSymbols.cpp`, and the header over-declares relative
to what is actually emitted.

### 2.2 Registers and bits

```c
/* A register bit: exactly one of three states. Never two. */
typedef struct {
    uint8_t  kind;   /* CQ_BIT_ZERO | CQ_BIT_ONE | CQ_BIT_Q */
    uint32_t q;      /* qubit index; valid iff kind == CQ_BIT_Q */
} cq_bit;

/* A register behind an i32 handle. `bits` is allocated to exactly `width`
   entries — never a fixed 128 — because handles are never reused and the
   table grows monotonically for the life of the program. An i1 flag costs
   one entry, not 128. Freed on cqrt_free; the table slot becomes a
   tombstone so handle numbering stays monotonic. */
typedef struct {
    uint32_t width;      /* 1..128; validated as a RANGE, not a whitelist */
    cq_bit  *bits;       /* `width` entries, LSB at index 0; NULL once dead */
    uint8_t  state;      /* CQ_SLOT_LIVE | CQ_SLOT_DEAD | CQ_SLOT_MEASURED */
} cq_reg;

/* Per-qubit classical shadow — the whole of our "simulation". */
typedef struct {
    uint8_t value;    /* 0 or 1, meaningful iff !unknown */
    uint8_t unknown;  /* set by a general Ry (§15 D12), or by a gate with an
                       * unknown control. NOT by Rz, and not by the θ≡π row:
                       * a diagonal gate moves no basis value */
} cq_shadow;
```

> **Two Step-7 corrections to the struct above.** (1) The width comment used to read
> "1, 8, 16, 32, 64 or 128", which was stale against the resolved **i80-is-in-scope**
> decision (§1) — a whitelist would reject every i80 rail, so the check is a range,
> `1 ≤ width ≤ 128`. (2) `uint8_t live` became `uint8_t state`, because §10 needs **three**
> distinguishable outcomes and a boolean carries two: *live*; *dead* (a D5 tombstone —
> qubits returned, `bits` freed, the slot kept forever so numbering stays monotonic); and
> *measured*, which §7 makes **terminal** — CQ_lang emits no adjoint and no `cqrt_free`, so
> the qubits are deliberately never reclaimed and the rail must still be swept by the I2
> audit, which a tombstone must not be. Measured over the 239 goldens: 255 `cqrt_measure_*`
> calls, **0** later freed and **0** later referenced. None of the three enumerators is
> numbered 0, so an all-zero slot is not a valid state.

**Handle table**: dense `int32_t` → `cq_reg`, monotonic allocation to match CQ_lang's
existing trace convention (`h0`, `h1`, …). Handles are never reused; qubit *indices* are.
Handle **0 is valid and live** — CQ_lang's counter is `static int32_t next_handle = 0;`
with `return next_handle++;` (`runtime/cq_runtime.c:64,67`) and every golden opens `-> h0`,
so the "no register" sentinel has to be **negative**. The counter is *process-global* and
shared with `cqrt_tape_alloc` (prints `t<N>`) and `cqrt_qram_alloc_<W>` (prints `a<N>`):
~~both are out of v1 scope~~ — **tape is IN since §15 D23 (2026-09-02) and its token DOES take a
slot in this table, as `CQ_SLOT_TOKEN`, precisely so that it draws from this counter**; qram is
still out, and a second counter for it later would diverge handle
numbering — and while the original worry was "fail every L6 trace diff while every
assert stayed silent", **§15 D18 retired that diff, and the divergence has since
happened anyway from a different direction**: `cqrt_addc`'s two transients and D7b's
defensive copy mint rails CQ_lang never sees, so the counter is already ahead of the
stub's. That is not a defect — those rails are ours and the ABI does not name them —
and it is a second, independent reason the goldens are not an oracle for us.

**Qubit pool**: monotonic counter + LIFO free list. A qubit returned to the free list is
asserted to be |0⟩ (§11). The pool has a configurable ceiling so it can be matched
against `qec_n_logical`; exceeding it fails loud.

### Invariants

- **I1** — for every bit, `kind == CQ_BIT_Q` XOR the bit is a known constant. No bit is
  both, no bit is neither.
- **I2** — no qubit index appears in two live registers. Copies are always physical
  (allocate + CX), never aliases. *This is what makes `cqrt_free` sound.*
- **I3** — a qubit on the free list is |0⟩.
- **I4** — a register whose bits are all constants owns zero qubits.
- **I5 — no packed scalar, anywhere.** The classical value and the quantum mask are the
  *same field viewed twice*: a bit is classical iff it is not on a qubit, and the mask is
  just "which entries are `CQ_BIT_Q`." The flat `cq_bit` array holds both, so there is no
  `uint64_t classical` and no `uint64_t qmask` in this codebase — not in a register, not
  in a peephole, not in a kernel. **This is the whole reason i128 is free.** A packed
  scalar caps at 64 and would force a two-word split plus a 128-bit variant of every
  identity peephole (`x + 0`, `x & all-ones`, …); with no scalar, 128 is a loop bound.
  Every kernel is written width-generically over `reg->width`, with no width switch.

---

## 3. The gate emitter — where classical/quantum is decided

The entire classical short-circuit lives in three functions. Everything else in the
library is Bennett.jl transcribed against them.

```c
void cq_emit_x  (cq_ctx*,                    cq_bit *t);
void cq_emit_cx (cq_ctx*, const cq_bit *c,   cq_bit *t);
void cq_emit_ccx(cq_ctx*, const cq_bit *c1,
                          const cq_bit *c2,  cq_bit *t);
```

**Controls are `const`; targets are not.** This is not stylistic — it is one of the two
mechanisms that enforce **I6** (`IMPLEMENTATION_PLAN.md` §0.2). Materialisation mutates a
bit, so a `const` control *cannot be materialised by construction*, and a compute half can
therefore never turn a source into a qubit behind the sandwich's back. The fold table never
materialises a control anyway — constants in control position are folded away — so the
qualifier costs nothing and removes a whole class of R1 miscompile. An earlier draft of this
section declared all three operands non-`const`, which would have silently disarmed it.

**Fold table** — the normative specification:

| Gate | Operand state | Action | Gates emitted |
|---|---|---|---|
| `X(t)` | `t` constant | flip the constant in place | 0 |
| `X(t)` | `t` qubit | `sink.x(t.q)`; shadow flips if known | 1 |
| `CX(c,t)` | `c = ZERO` | nothing | 0 |
| `CX(c,t)` | `c = ONE` | `emit_x(t)` | 0 or 1 |
| `CX(c,t)` | `c = Q`, `t` constant | **materialise** `t`, then `sink.cx` | 1 or 2 |
| `CX(c,t)` | `c = Q`, `t = Q` | `sink.cx` | 1 |
| `CCX(c1,c2,t)` | either control `ZERO` | nothing | 0 |
| `CCX(c1,c2,t)` | `c1 = ONE` | `emit_cx(c2,t)` | ≤ 2 |
| `CCX(c1,c2,t)` | `c2 = ONE`, `c1 = Q` | `emit_cx(c1,t)` | ≤ 2 |
| `CCX(c1,c2,t)` | both controls `Q`, `t` constant | **materialise** `t`, then `sink.ccx` | 1 or 2 |
| `CCX(c1,c2,t)` | both controls `Q`, `t = Q` | `sink.ccx` | 1 |

> **The `c2 = ONE` row was missing from an earlier draft of this table, and its absence left
> 15 of the 125 `CCX` operand combinations matched by no row at all.** Walk the four original
> rows over the control pair `{ZERO, ONE, Q}²`: `(Z,·)`, `(·,Z)`, `(O,O)`, `(O,Q)` and `(Q,Q)`
> are covered, but `(Q,O)` is not — it is not `ZERO`, `c1` is not `ONE`, and `c2` is not `Q`.
> `CCX` is **symmetric in its controls**, so the two `ONE` rows are one rule written twice;
> implement it as a control swap before dispatch, not as two branches. The `(O,Z)` overlap
> between rows 1 and 2 is benign — both yield 0 gates — so row order does not matter.

**Kind, never shadow.** Every row above dispatches on the bit's *kind* (`ZERO`, `ONE`, `Q`)
and never on a qubit's shadow value. A `Q` control whose shadow is known-0 is **not** folded
away; that would be shadow-driven demotion, which D6 excludes from v1. The consequence is
worth stating because it sizes the L0 suite: there are only `3 + 9 + 27 = 39` distinct
gate-emission behaviours, and the five-kind split that yields 155 cases exists to pin the
resulting **shadow**, not to distinguish gate counts.

**Materialisation** (`cq_materialise(bit)`): take a qubit from the pool (guaranteed |0⟩
by I3), emit `X` if the bit's constant was 1, set `kind = CQ_BIT_Q`. This is the *only*
place a qubit is ever allocated for data, and it is exactly the rule "a CX from a tainted
bit into an untainted bit allocates a qubit."

Operand distinctness is asserted, not assumed — a coincident operand is a meaningless
channel and a real miscompile signature. There are **four** constraints, one per operand
pair, because `c1 != c2 != t` is prose shorthand and not valid C semantics:

| Gate | Asserted |
|---|---|
| `CX(c,t)` | `c != t` |
| `CCX(c1,c2,t)` | `c1 != c2`, `c1 != t`, `c2 != t` |

`c1 == t` is exactly as much a miscompile signature as `c1 == c2`, so all three `CCX` pairs
are checked. Note what the assert can actually compare: distinctness is a property of
**bits**, not of kinds. Two different bits may both be `Q unknown` while holding different
qubit indices — a legal, ordinary pair. So the check is **on qubit index alone**, which
means it can only ever fire on `CQ_BIT_Q` operands, and cannot fire on two constants. That
is correct: two constant bits are genuinely independent channels.

> **This sentence used to read "on qubit index (and pointer identity)", and the
> parenthetical contradicted the rest of it.** A pointer-identity clause fires on two
> constants that happen to be the same object, which the same sentence forbids — and it is
> redundant besides, since two `Q` bits at one address necessarily hold the same index.
> Found at Step 6: `CCX(o, o, t)`, one constant `ONE` bit passed as both controls, is a
> legal fold to `X(t)` and the spurious clause aborted on it. Register-level aliasing is a
> separate question and belongs to **D7** and M07.

### Shadow update rules

| Gate | Rule |
|---|---|
| `X(t)` | `t.unknown ? nop : t.value ^= 1` |
| `CX(c,t)` | `t.unknown \|= c.unknown; if (!t.unknown) t.value ^= c.value` |
| `CCX(a,b,t)` | `t.unknown \|= a.unknown \| b.unknown; if (!t.unknown) t.value ^= a.value & b.value` |
| `Ry(q,θ)`, θ off the π-lattice | `q.unknown = 1` |
| `Ry(q,θ)`, θ ≡ π (mod 2π) | `X`'s rule — the row emits `X` then a diagonal, **no poison** |
| `Rz(q,φ)`, any φ | **nothing** — `Rz` is diagonal and moves no basis value |

Conservative in the safe direction only: the shadow may say *unknown* when the truth is
determinate (it forgets correlations), but never the reverse.

> **The last two rows are D12, resolved at Step 19, and they are the reason the rule is
> stated per §7 ROW rather than per emitted gate.** The old single row read
> `q.unknown = 1` "unless θ is in the classical set, §7", which is ambiguous exactly where
> it matters: the θ ≡ π row *does* emit a rotation (the `Z`, as `sink.rz(q, π)`) while
> being a basis permutation, and general `Rz` emits a rotation while changing no basis
> value at all. A diagonal gate cannot move a computational-basis value, so a determinate
> entry stays determinate and the shadow remains **exact** rather than becoming
> conservative. `cq_shadow_rotate` itself is unchanged and still poisons unconditionally —
> what D12 fixes is which rows *call* it.

---

## 4. Kernel contract — XOR into target

> **Every data kernel has the form `void kernel(cq_ctx*, cq_bit *dst, const cq_bit *a, const cq_bit *b, int W)` with the semantics `dst ^= f(a, b)`, leaving `a` and `b` unchanged and every internal ancilla at |0⟩.**

This single shape satisfies all three axes at once:

- **Forward** (`cq_template_add_i32(a,b)`) — allocate a fresh all-`BIT_ZERO` `dst`,
  call the kernel, return `dst`'s handle. `0 ^ f(a,b) = f(a,b)`. ✔
- **Uncompute** (`cq_template_add_i32_unc(out,a,b)`) — call the *same kernel* with
  `dst = out`. Since `out` holds `f(a,b)` and `a,b` are still live,
  `f(a,b) ^ f(a,b) = 0`. ✔ **It reclaims NOTHING** — see §10. The retired clause here
  read *"Then free `out`'s qubits"*, which §10 contradicts outright (*"`_unc` owns no
  qubits and reclaims nothing … `cqrt_free` is the **sole** place a qubit ever goes back
  to the pool"*), and §10 is the operative statement: it is dated, reasoned, and backed
  by measurement over the corpus. **A generated `_unc` body written from the retired
  clause double-frees.** Corrected 2026-08-22 at Step 22, when M27's bodies had to be
  written against one of the two.
- **Controlled** — promote the kernel's gates (§9).

> **ARITY AND WIDTH ARE NOT PART OF THE CONTRACT — THE SEMANTICS ARE. Resolved
> 2026-08-16 at Step 14 (`bd ckd.15`).** The form above is the canonical two-source,
> one-width kernel and the C type the test driver and the shim share. The
> **normative** clause is the other half of the sentence: `dst ^= f(sources)`, sources
> unchanged, every internal ancilla at |0⟩. Three kernels in the §6 catalogue depart
> from the literal form above, in two different ways, and none is an exception to the
> rule:
>
> - **K5** (casts) is **unary with two widths** — `F` in, `T` out. It leaves the
>   parameter list.
> - **K9** (`icmp`) **keeps the parameter list and the type exactly**, and breaks only
>   the unwritten assumption that `|dst| == W`: its `dst` is one bit while `W` is the
>   operand width. `icmp` is `i1` (`ir_types.jl:79`), and K9 is the only kernel that
>   keeps the single-`W` signature while its result is a different width — which is
>   why the test driver has to be told through `cq_kd_shape`'s `w_dst` rather than
>   through the signature.
> - **K10** (`mux`) takes **three** sources — §6 already writes it `mux(dst,c,t,f)` —
>   with `c` **one bit, not `W`**, because `lower_mux!` takes `cond` as a vector and
>   reads only `cond[1]` (`arith.jl:529`) and the barrel passes the singleton
>   `[b[k+1]]` (`arith.jl:361`, `:377`, `:397`).
>
> Such a kernel **declares its own signature and names every operand explicitly**.
> What is *not* permitted: a fourth parameter carrying hidden state, an `_unc` entry
> point (§10 — uncompute is the same kernel), or a `_controlled` variant (§9 — the
> axis is an emitter mode). Inside a sandwich the extra operands live in the kernel's
> own `env` struct, which is where every sandwich kernel's scratch pointers already
> live. The D7a/D7b guard is N-ary for this reason — `cq_kernel_check_n` in
> `src/kernels/kernel.h` sizes each overlap range **per operand**, so a differing
> arity or width cannot produce a mis-sized guard, and a mis-sized guard is the I2
> defence with the wrong bounds.

**This is the reason v1 uses ripple-carry rather than Cuccaro for the out-of-place
adder**, reversing an earlier recommendation. Cuccaro (`lower_add_cuccaro!`) is
in-place — `(a,b) → (a, a+b)` — so its uncompute is the *reverse circuit*, not a re-run,
which does not match CQ_lang's `_unc(out, src…)` "recompute from the still-live sources"
contract (`docs/backend.md` §9.2). Bennett's `lower_add!` is already XOR-into-target;
it only needs its carry chain cleaned. Cuccaro is still needed — as the *in-place*
accumulator inside the multiplier (**§6**'s catalogue, K8 and K11; the "§6.4" this
line used to cite is dangling — §6 is a flat table with no numbered subsections).

---

## 5. Making dirty constructions clean

Most of Bennett's constructions leave scratch dirty, because Bennett's global
forward–copy–reverse wrap cleans up afterwards. We have no global wrap, so we apply the
same construction locally:

```
short-circuit                    if every operand bit is classical: fold to a constant,
                                 emit nothing, allocate nothing, and DO NOT enter here
pre-materialise scratch          all scratch bits -> CQ_BIT_Q. 0 gates, W_scratch qubits
compute  f into scratch          (Bennett's construction, verbatim)
copy-out scratch → dst           (|dst| CNOTs, + 1 X where the raw flag is inverted;
                                  this is the "^=" — see the note below on |dst| != W)
reverse  the compute             (same gates, reverse order — all three gates are self-inverse)
free     scratch
```

Cost: 2× the compute half. Applies to `add`, `sub`, `eq`, `ult`, `slt`, `mux`,
`mul`, `divrem`. Naturally clean already (no sandwich needed): `and`, `or`, `xor`,
constant `shl/lshr/ashr`, `sext/zext/trunc`, **and `addacc` (K8)** — added
2026-08-16 at Step 15, where both lists turned out to omit it although §6's
catalogue row has always carried a ✔ in its Clean? column.

> **AND K8 IS THE ONE KERNEL WITH NO CLASSICAL SHORT-CIRCUIT AT ALL — decided at
> Step 15, because no document settled it.** The paragraph above ("if every
> operand bit is classical: fold to a constant, emit nothing, allocate nothing,
> and DO NOT enter here") is scoped to kernels CQ_lang can call on constants. K8
> has **no `cqrt_*` symbol** — `opcode_table.yaml` routes no opcode to it — is
> reachable only from K11 (M18, Step 16), and is handed pre-materialised scratch
> by its only caller. A classical operand there is not a cheap case but an active
> R1/R8 hazard, because K8 writes its own addend: a classical `b[i]` gets
> materialised mid-construction and the reverse replay stops cancelling while L1
> stays green. **K8 REFUSES a non-`CQ_BIT_Q` operand, loudly, in both
> configurations** (`cq_addacc_check`), and `tests/test_kernel_addacc_death.c` is
> what carries the coverage L5 carries elsewhere. Full statement in K08.md §5 D7.

> **CORRECTED 2026-08-16 at Step 13: the copy-out is `|dst|` CNOTs, not `W`, and the two
> differ for K9.** This sketch read "W CNOTs" and that is right for every kernel whose
> result is as wide as its operands — which is all of them but one. `icmp` produces a
> **one-bit** flag (`ir_types.jl:79`; §6's K9 row already says "1-bit result"), so K9's
> copy-out is **1 CNOT**, plus **1 X** for the five predicates whose raw scratch flag is
> the negation of the answer. M09's driver never assumed otherwise — `cq_sandwich` takes
> `n_copyout` as a parameter — but a reader sizing a copy-out loop from this line would
> read off the end of a one-bit register, and a test harness doing the same would run
> every compare at `W = 1` and pass. Measured and pinned in
> `tests/test_kernel_cmp.c`; the general statement is `n_copyout` is the kernel's, and
> only the compute half is replayed.

**The pre-materialise step is not an optimisation — it is what makes the reverse half
cancel** (`IMPLEMENTATION_PLAN.md` §0.2, invariant **I6(b)**, risk **R8**). Scratch is born
`BIT_ZERO`, and the fold table dispatches on *kind*: a scratch bit read as a **control**
while still `BIT_ZERO` folds to zero gates on the forward pass, and if a later step
materialises it, the reverse replay sees `CQ_BIT_Q` and emits a gate the forward never did.
The halves stop mirroring and scratch is left dirty — while **L1 stays green**, because the
value is still right. Materialising the whole region up front makes every scratch bit `Q`
for the entire compute half, so the two passes are identical by construction.

Two consequences worth stating here because they show up in the goldens:

- It costs **qubits, never gates** — `cq_materialise` emits an `X` only if the constant was
  1, and scratch is always born 0.
- Kernel **qubit** counts become a function of **`W` alone**, independent of the operand
  bit-kind mask, because pre-materialisation is unconditional and never consults the
  operand kinds.

  > **CORRECTED 2026-08-15, and the earlier wording said `gate` counts. It was false, and
  > the licence it granted — that an L4 golden may be taken at any mask — was the dangerous
  > half.** Pre-materialisation removes the *scratch* side's dependence on bit-kinds; it
  > does nothing to the *operand* side, and it must not, because `a + 0`, `x − 1` and the
  > `x + 1` constant-increment case all legitimately emit fewer gates — which is exactly
  > what **L5** proves. A gate count is a function of `(W, operand mask)`. **Measured at
  > Step 12**, not argued: K6 at `W = 3` with `a = b = {Q, ZERO, ZERO}` emits **19** gates
  > sandwiched, against `11W − 4 = 29` at all-quantum
  > (`tests/test_kernel_add.c`, `r8_the_mixed_kind_witness_mirrors_exactly_at_w3`;
  > K06.md §3.7 derives the same number by hand).
  >
  > What I6(b) actually buys is **pass-symmetry at every mask** — forward and reverse emit
  > the identical sequence — which is what makes a golden well-defined at all. One L4
  > golden per `(kernel, W)` is sound because it is pinned at **one specific, reproducible
  > mask**: all-quantum, which is the *fixed point* of the drift, since with no demotion
  > (D6) a mask can only move towards `Q` between a forward call and its `_unc`. A golden
  > at any other mask must pin that mask's own number. The last paragraph of this section
  > already said "a deterministic function of `(W, operand bit-kinds)`" and was right;
  > IMPLEMENTATION_PLAN §0.2 carries the corrected wording. Risk R5 is answered by pinning
  > counts rather than traces, not by mask-independence.

The **short-circuit** line is equally load-bearing in the other direction: without it,
pre-materialisation would allocate scratch for a fully-classical operation and break **L5**'s
"zero gates and zero qubits". An all-classical kernel call never reaches the sandwich.

Because `X`, `CX` and `CCX` are each self-inverse, "reverse the compute" is literally
replaying the emitted operand triples backwards — which the kernel knows without storing
anything, since the gate sequence is a deterministic function of `(W, operand bit-kinds)`.

---

## 6. Kernel catalogue

| # | Kernel | Bennett source | Clean? | Notes |
|---|---|---|---|---|
| K1 | `xor(dst,a,b)` | `lower_xor!` | ✔ | 2W CNOT |
| K2 | `and(dst,a,b)` | `lower_and!` | ✔ | W CCX |
| K3 | `or(dst,a,b)` | `lower_or!` | ✔ | 2W CNOT + W CCX |
| K4 | `shl/lshr/ashr(dst,a,k)` | `lower_shl!/lshr!/ashr!` | ✔ | pure index shuffle; ≤ W CNOT |
| K5 | `sext/zext/trunc(dst,a)` | `lower_cast!` | ✔ | ≤ T CNOT |
| K6 | `add(dst,a,b)` | `lower_add!` + carry uncompute | sandwich | `dst ^= a+b` mod 2^W |
| K7 | `sub(dst,a,b)` | `lower_sub!` + uncompute | sandwich | two's complement via K6 |
| K8 | `addacc(acc,b)` in-place | `lower_add_cuccaro!` | ✔ | `acc += b`; 1 ancilla, self-cleaning |
| K9 | `eq/ult/slt(dst,a,b)` | `lower_eq!/ult!/slt!` | sandwich | 1-bit result; other 7 predicates derive |
| K10 | `mux(dst,c,t,f)` | `lower_mux!` | sandwich | **three sources, `c` is 1 bit** — see §4; also gives variable shifts |
| K11 | `mul(dst,a,b)` | `lower_mul_wide!` | sandwich | shift-add over K8, copy-out, reverse. **`W = 1` DELEGATES TO K2** — `lower_add_cuccaro!` is out of domain at `W ≤ 1` and the closed form's total is accidentally right there with the wrong split (K11.md §3). Rule 7's canonical shape, unchanged: the first kernel since K6/K7 that needs no departure from the parameter list |
| K12 | `divrem(dst,a,b)` | `_soft_udiv_compile` | sandwich | restoring division: W × (K9's `ult` **step block**, K7's `sub` **step block**, K10's `mux` **step block**) + 2 CX. **D9** |

Two notes on the catalogue:

- **K9 derives 7 of 10 predicates for free** exactly as `lower_icmp!` does:
  `ne = ¬eq`, `ugt = ult(b,a)`, `ule = ¬ult(b,a)`, `uge = ¬ult(a,b)`, and the signed
  trio by flipping sign bits before `ult` (`lower_slt!`).
- **K12 is not a circuit.** Bennett implements division as a *branchless Julia kernel*
  compiled by its own pipeline (`src/divider.jl`). We do the same: an unrolled restoring
  division built from K7/K9/K10. Nothing new to port — and as of **D9(e)** that is literal:
  M19 calls M16's, M14's and M17's **exported step functions** and transcribes no gate list
  of its own (plan §0.4). Its scratch scheme and the three kernel-shape choices that ride
  with it are **D9**; its cost is `udiv` `34W²+5W` over `8W²+4W−1` qubits, and it ships at
  **i128**, where that is 557,696 gates over 131,583 qubits.
- **K11 uses Cuccaro, and this is a deliberate delta from upstream** (decided 2026-08-14).
  Bennett's `multiplier.jl:29` calls `lower_add!` — *ripple*, not Cuccaro — so "shift-add
  over K8" is our choice, not a port. It is the right one: scratch drops from `3W²+W` to
  `W²+2W` (**1088 vs 3104 qubits at W=32**, which matters once D2's pool ceiling is wired to
  `qec_n_logical`) and Toffolis from `5W²−3W` to `5W²−5W`. K8 exists in this catalogue
  precisely as the multiplier's in-place accumulator. **The Rule 7 objection to Cuccaro does
  not apply here:** Cuccaro is barred from K6/K7 because its uncompute is the reverse circuit
  rather than a re-run, which does not match CQ_lang's `_unc` contract — but inside the
  multiplier the accumulator is internal to the kernel and never exposed to `_unc`. Note the
  cost of the choice: *ripple is the only variant cross-checkable against an upstream
  published figure*, so K11's golden is self-pinned. `docs/constructions/K11.md` keeps the
  ripple figures as a labelled appendix for exactly that reason.

---

## 7. Rotations, and the θ special cases

```c
void cqrt_ry_i<W>(int32_t h, double theta);   /* Ry(θ) on every qubit of the register */
void cqrt_rz_i<W>(int32_t h, double phi);
```

**Both matrices are stated, because both sign conventions are load-bearing.**

```
Ry(θ) = [[cos θ/2, −sin θ/2], [sin θ/2, cos θ/2]]      = exp(−iθY/2)
Rz(φ) = diag(e^{−iφ/2}, e^{+iφ/2})                     = exp(−iφZ/2)
```

The `Rz` line was unwritten until Step 19 and is **not** free to choose: the `Rz, φ ≡ 0
(mod 4π)` row below has period **4π**, which is the half-angle form above and not the
phase-gate `diag(1, e^{iφ})`, whose identity row would be mod 2π. M21 already encodes the
same convention (`src/angle.h` — "`Rz: ∓iZ`" for the half turn, i.e. `Rz(π) = diag(−i, i)`),
and every control-side angle in **D11** is read straight off these two lines. Neither
matrix has an upstream: Bennett is purely classical-reversible and
`third_party/bennett/src/controlled.jl` contains no rotation and no phase at all, so §7
and §9's rotation rows are **stated here** rather than ported (Rule 1 has nothing to
supply).

The special cases, per bit. **`kind` is read, never the shadow** (D6), and the shadow
column is the rule this row imposes on M02 — see **D12**:

| Angle | Bit is constant | Bit is a qubit | Shadow |
|---|---|---|---|
| `Ry`, θ ≡ 0 (mod 4π) | nothing | nothing | untouched |
| `Ry`, θ ≡ 2π (mod 4π) | nothing (global −1) | nothing (global −1) | untouched |
| `Ry`, θ ≡ π (mod 2π) | **flip the constant, 0 gates, 0 qubits** | emit `X`, then `Z` as `sink.rz(q, π)` | `cq_shadow_x` — **no poison** |
| `Ry`, otherwise | materialise, then `sink.ry` | `sink.ry` | **`cq_shadow_rotate` — poison** |
| `Rz`, φ ≡ 0 (mod 4π) | nothing | nothing | untouched |
| `Rz`, otherwise | **nothing** — diagonal on a definite value is a global phase | `sink.rz` | **no poison** (D12) |

The θ ≡ π row is the important one. `Ry(π) = XZ`, i.e. `X` up to a **relative** sign on
|1⟩. On a bit that is already a definite classical constant that sign is *global* and
unobservable, so the bit stays classical and costs nothing. On a bit that is already a
qubit — possibly in superposition, possibly entangled — the sign is observable and must
be emitted. Getting this asymmetry right is what makes classical-mode testing possible
(§11) without making it unsound.

> **`Ry(π) = XZ` IS A MATRIX PRODUCT AND "emit `X` then `Z`" IS A CIRCUIT, AND THE TWO
> READ IN OPPOSITE ORDERS.** Applying `X` first and `Z` second is the matrix product
> `Z·X`, and `XZ = −ZX` — so the emitted circuit realises `Ry(3π) = −Ry(π)`, not `Ry(π)`.
> Neither sentence is wrong; they are the same two symbols in the two conventions, and
> earlier drafts of this section, `IMPLEMENTATION_PLAN` §4's Step 19 row and CLAUDE.md's
> Rule 15 all carried both without saying so.
>
> **There is no sign to "fix", because the row spans both parities.** `θ ≡ π (mod 2π)`
> contains `k ≡ 1` and `k ≡ 3 (mod 4)`, whose operators differ by exactly that global −1,
> so **no fixed two-gate spelling is sign-exact for the whole row**. What §7 states is
> therefore the honest claim: the emitted pair realises the half turn **up to a global
> phase**, which is unobservable on the uncontrolled axis this table is written for. The
> parity is recoverable — M21 computes `k mod 4` and discards it — and recovering it is
> **D11**'s obligation at Step 20, not this row's.

> ### `bd lk0` — the `Z` is `sink.rz(q, π)`, and the vtable stays frozen at six
>
> §7 named a `Z` that has no §8 vtable entry, no `cq_emit_*` and no Bennett source, and
> Rule 4 forbids reaching for a new gate on the classical path. It needs none:
>
> ```
> Rz(π) = diag(e^{−iπ/2}, e^{+iπ/2}) = diag(−i, +i) = −i·Z        so   Z = i·Rz(π)
> ```
>
> The qubit cell is therefore `sink.x(q)` then `sink.rz(q, π)` — **two existing entries**,
> no seventh slot, no fork from a frozen ABI. Its exact operator is
> `Rz(π)·X = −i·(Z·X) = +i·Ry(π) = −i·Ry(3π)`, i.e. the Pauli `Y`.
>
> **The residual `±i` is unreachable, not a spelling accident, and no reordering removes
> it.** `det Ry(θ) = det Rz(φ) = 1` at every angle while `det X = −1`; any product of `x`
> and `rz` gates needs an odd number of `x` to be antidiagonal like `Ry(π)`, so its
> determinant is `−1`, so if it equals `c·Ry(π)` then `c² = −1` and `c = ±i`. All four
> orderings of `{x, rz(±π)}` land on `±i` and nothing does better. Do not "tidy" the order
> to chase the phase; do reorder only for the reason **D11** gives.
>
> **The angle handed to `sink.rz` is `CQ_ANGLE_PI`, the nearest double to π**, so the
> emitted `Z` is off by ~1.2e-16 rad. That is forced by the `double` sink ABI below and is
> 10⁴ times inside D10's own `2·tol·π` bound. It is not a defect and must not be "fixed"
> with a long double.
>
> **Why not simply `sink.ry(q, π)`, which is one gate and phase-exact?** Because the two
> named entries are what a QEC backend can act on: `x` is exactly Clifford and `rz` takes
> an exact rational `(p, q_denom, precision)` in which π is `1/1`, while **there is no
> `qec_ry` at all** (see the QEC note below). Recorded because the one-gate spelling is
> otherwise strictly better on paper, and someone will propose it.

Angle comparison uses an exact-multiple test against a tolerance, configurable, default
`1e-12`. **What "relative" is relative to was unstated here until Step 18, and the
obvious reading is a miscompile — see §15 D10, which fixes it.** The window is the
absolute angle `tol · π`; the classification is refused above the magnitude at which the
representation error alone exceeds it; and the contract M21 satisfies is

> a fold names a multiple of π that θ really is within `2·tol·π` radians of,

so a rotation is never silently discarded by more than `6.3e-12` radians at the default.
`src/angle.h` states it, `tests/test_angle.c` measures it against a double-double π.

**The table above is stated for the UNCONTROLLED axis, and what it does inside a §9
controlled region is now settled — see D11 and §9.** In one line: a **classical** control
folds the region away or unrolls it uncontrolled, so this table applies verbatim and
Rule 15's "0 gates, 0 qubits" survives untouched; a **quantum** control makes every §7 row
that FOLDS wrong — the four cells that act by emitting nothing, because
`controlled-(e^{iα}·I)` is `Rz(α)` on the control wire, **plus the half-turn's qubit
cell**, which does emit gates but only realises the row up to the `+i` of
`Rz(π)·X = Y`. §9's promotion table states the operative scope in one line — *a §7 fold
row → HARD ERROR in v1* — and it is deliberately broader than "the zero-gate rows": an
M06 that refused only those would let the one row carrying a `∓π/2` residual through.
D11 gives the exact correction for each and **v1 refuses rather than emitting it**.
`cqrt_ry_<W>_controlled_inv` and `cqrt_rz_<W>_controlled[_inv]` are both in the frozen ABI
(`docs/cqrt_census.txt:251-255, 280-293`), so this is not hypothetical — but measured
2026-08-17 over all 239 goldens, **not one controlled rotation is emitted today**: all
4,933 `cqrt_*_controlled*` calls are `copy` (4,918), `tape_write` (5) and `qram_store`
(10), and `CQ_lang/runtime/cq_runtime.h:159-160` says the pass *begins* emitting the
controlled `rz` in WP3.

**The `Ry` sink entry stays `double` all the way down.** `qec_rz` takes an exact
rational `(long p, long q_denom, int precision)` — θ = π·p/q_denom, ε = 2^−precision —
and there is no `qec_ry` entry at all; converting angle representations is the *QEC
sink's* problem, not the kernel layer's. **Both halves of that sentence were read as
"therefore the qec sink stubs `ry`/`rz`" and that inference is now retired — see §15
D19.** The rational is not a clumsy spelling of a double we could hand over: p and
q_denom are separate `long`s that reach `gs_rz_synthesize` unmodified, at
`4·precision + 96` bits of working precision, and the angle is in **units of π**, so
`(1, 4)` is exactly π/4 where no double is. And `Ry` is constructible from what the
API does have. D19 gives both, with the denominator-cap band and the emission order
measured; the printf sink remains the v1 default.

### Measurement

`cqrt_measure_i<W>(h)` returns the shadow value for every bit whose shadow is known, and
**0** for any bit whose shadow is unknown. It emits `sink.mz` on each qubit. Per
CQ_lang's contract, measurement is terminal: the pass emits no adjoint and no `cqrt_free`
for a measured handle, so we do not reclaim its qubits.

---

## 8. Sinks

```c
typedef struct {
    void (*x)  (void *u, uint32_t q);
    void (*cx) (void *u, uint32_t c, uint32_t t);
    void (*ccx)(void *u, uint32_t a, uint32_t b, uint32_t t);
    void (*ry) (void *u, uint32_t q, double theta);
    void (*rz) (void *u, uint32_t q, double phi);
    void (*mz) (void *u, uint32_t q);
    void *user;
} cq_sink;
```

v1 ships three: **printf** (default; one line per gate, in the *lexical convention*
CQ_lang's golden traces use — see the correction below), **counter** (per-kind totals
and T-count, matching `gate_count` / `t_count` in Bennett.jl so baselines are directly
comparable), and
**qec** (compiled only when `C_quantum_error_correction` is present — the name is
right, but it names a REPOSITORY and not the artefact: the library is that repo's `qec/`
subdirectory, built as **`libqec.a`** with its header at **`qec/qec.h`**, so a
`find_library` spelled from the project name finds nothing. The local clone is
`~/Desktop/CQ_lang_hardware`, whose directory name differs from the repo's; `qec_x`, `qec_cx`, `qec_ccx`, `qec_mz` map 1:1, and `ry`/`rz` are **no longer
stubs** — §15 D19).

> **SHIPPED AT STEP 26, and one sentence above needs qualifying: "compiled only when
> the library is present" is NOT what was built.** `src/sink_qec.c` compiles either way,
> and its no-library arm **registers nothing** — so `CQOPS_SINK=qec` in a build without
> the library takes §8's own hard error, `CQOPS_SINK names an unregistered sink (qec)`
> (verified by hand in a no-library Release build), rather than falling back to `printf`
> and handing the caller a circuit they did not ask for. Only the *library* is
> conditional, through `-DCQOPS_QEC_DIR=<a built qec/>`; the *file* is not, which is also
> what keeps the M26 shim free of a build-configuration branch.
>
> Three environment inputs join `CQOPS_SINK`, all with §8's empty-means-absent rule:
> **`CQOPS_QEC_CONFIG`** (required when the sink binds — there is no default, because one
> would silently pick a code distance and an `n_logical`), **`CQOPS_QEC_TRACE`**
> (optional; written to a NAMED `<path>.partial` and renamed only by a clean teardown, so
> an `abort()` still leaves the partial trace on disk) and **`CQOPS_QEC_PRECISION`**
> (default 20, refused above 30 — above that the accuracy would be set by M25b's rational
> approximation rather than by the synthesis, silently).
>
> **THE SINK WRITES NO TEXT, EVER (§15 D21).** `#REGISTER` lines and `# STAGE: op
> begin/end` brackets are the M26 shim's, which knows the opcode, the handles and the
> widths a sink structurally never sees; every gate line and every `#PATCH` is the QEC
> library's own. What M25 owns of the trace is the `FILE*` and its `atexit` teardown —
> plus, since `bd 76r` landed, two hooks: `cq_sink_qec_trace()`, the borrowed stream that
> is the annotation layer's ONE activation test, and `cq_sink_qec_set_header()`, which is
> how D21 (a)'s end-of-program header reaches the front of the buffered body without a
> Layer-4 module calling up into Layer 5.

Selected at runtime via
`cqops_set_sink()`; the default is chosen by environment variable so CQ_lang's existing
fixtures need no changes.

> **Two corrections, made at Step 9 when the two sinks were built. Both sentences
> above were wrong as originally written, in ways that would have produced a
> confident wrong number.**
>
> **(1) "The format CQ_lang's golden traces already use" can only mean the lexical
> CONVENTION, never the lines.** CQ_lang's 239 goldens are HANDLE-level runtime
> call traces — `cqrt_cnot(h1, h3)`, `cq_template_add_i32_hl(h6, -6) -> h7` — and
> contain zero gate-level lines. Those calls come *into* us; what goes *out* is one
> level below. A `cq_sink` is handed a raw `uint32_t` qubit index and a `double`
> and never sees a handle, a width or a symbol name (§8's vtable is the whole of
> its input), so it could not print a golden line if we wanted it to. What M23
> borrows is the convention: lowercase op name, `(`, operands separated by `", "`,
> `)`, **no** `->` (that arrow exists only to carry a return value, and all six
> entries are `void`), newline, flush per line, `%a` for angles. What it supplies
> is its own content — op names spelled exactly like the vtable entries they come
> from (`x`, `cx`, `ccx`, `ry`, `rz`, `mz`) and operands `q<N>`, because a qubit
> index is **not** a handle: handles are monotonic and never reused (D5) while
> qubit indices are recycled through the LIFO free list (D4), so `h<N>` would
> assert an identity that is false and would collide with M26's own numbering.
>
> `%a` is load-bearing rather than stylistic: angles are compared **bitwise**
> throughout this project — `0.0` and `-0.0` are equal in C and are different gates
> to emit — and `%a` is exact and round-trips through `strtod` for every double,
> subnormals included. `%f` and `%g` do not.
>
> **(2) The counter sink does NOT report peak qubits, and never could.** Bennett's
> `peak_live_wires` is a **simulation**: it walks a `Vector{Bool}` sized to the
> circuit, applies every gate and counts simultaneously non-zero wires
> (`diagnostics.jl:206-220`). Rule 13 forbids a simulator *anywhere*. It is also
> ill-defined for us even setting that aside — it measures the all-zero-input run
> (`bits = zeros(Bool, c.n_wires)`), our operands are routinely `CQ_BIT_ONE`, and a
> qubit in superposition has no Bool value to be non-zero at all. Bennett's
> `ancilla_count` is `length(c.ancilla_wires)`, a property of a circuit **object**,
> and by Rule 13 we hold none.
>
> The number is not lost — it was already delivered at Step 4, by the pool. M03
> records the identity `peak == minted`: a fresh index is minted only when the free
> list is empty, i.e. only when `live` has already reached `minted`, so the
> monotonic counter **is** the high-water mark. `cq_qubits_peak()` returns it
> exactly, and §12's acceptance criterion below reads it from there. (The value is
> maintained in both configurations; only the *assert* that the identity holds is
> Debug-gated, so under Rule 17 do not call the identity verified from a Release
> run.)
>
> **But it is not Bennett's number, and moving the computation does not make it
> one.** `cq_qubits_peak()` is peak **allocated**; `peak_live_wires` is peak
> **non-zero**, a function of the wire *values*, so a wire sitting at 0 does not
> count towards it at all. The gap is not a rounding error on our circuits:
> `cq_sandwich` pre-materialises the entire scratch region at zero gates (I6(b))
> and every one of those qubits is born |0⟩. "Matching `gate_count` /
> `ancilla_count` in Bennett.jl" was therefore a category error for this figure
> however it is computed, which is why the sentence above drops the Bennett
> comparison for the qubit count rather than re-pointing it at the pool. The
> gate-count comparison is unaffected and stands.
>
> **A sink could only guess, and the guess would be low.** The single quantity
> derivable from a gate stream is `max operand index + 1`, and that is a *lower
> bound*: `cq_materialise` takes a qubit from the pool and emits **no** gate when
> the constant was 0 (Rule 5), and `cq_sandwich` pre-materialises the whole scratch
> region from `BIT_ZERO` (I6(b)) — so a qubit can be allocated, held and released
> without ever appearing in a gate. Do not add such a field to M24. A number
> silently smaller than the truth is worse than no number, because the D2 pool
> ceiling is what stands between us and over-committing a QEC device.
>
> **`total` spans the Bennett triple only.** `gate_count` returns
> `(total, NOT, CNOT, Toffoli)` with `total` the redundant sum of the other three,
> and Bennett circuits contain no `Ry`, `Rz` or `Mz` at all. `cq_count_total` is
> therefore `x + cx + ccx` and deliberately excludes the rotations and the
> measurement — folding them in would break the comparison this sink exists to make
> possible, and would break it only once §7 fires, i.e. long after the goldens were
> pinned.
>
> **The stream is stdout, and that is not a concession to CQ_lang.** libcqops is a
> linkable C library; CQ_lang is one caller. Our coupling to it is the frozen
> `cqrt_*` ABI and nothing else — we satisfy that ABI, and we do not inherit its
> trace. A library with a trace to emit and no other instruction writes it to
> stdout, and `cq_sink_printf(FILE *)` lets any caller say otherwise in one line.
>
> **What this does disturb is Step 24's oracle, and that is filed as bd `590`.**
> CQ_lang's fixture runner is `"$TMP/slice" | diff -u - "$GOLDEN"`
> (`run_slice.sh:83`) — it diffs the binary's *entire* stdout against a golden that
> was produced by `CQ_lang/runtime/cq_runtime.c`, a file whose own first line calls
> it a **"trace-only runtime stub"**. Those goldens are CQ_lang's regression oracle
> for CQ_lang's IR pass, captured against a placeholder backend. Once the real
> backend is linked, the placeholder is gone — NORTH_STAR's finish-line condition 1
> says exactly that, and asks only that the fixtures "link against `libcqops` and
> run" — so there is no longer anything in the process that would emit those bytes,
> and nothing for our stream to collide *with*. The question that WAS open — what
> Step 24 compares against, given that IMPLEMENTATION_PLAN's "diff emitted traces /
> Traces match" and NORTH_STAR's "link and run" are not the same criterion — is
> **CLOSED as §15 D18 (2026-08-27): NORTH_STAR's criterion wins and "traces match"
> is retired.** Read D18 before reopening it; it also records that `bd 590`'s own
> fallback oracle (the goldens' `cqrt_measure_*` values) was measured FALSE, and
> that our handle numbering already diverges from the stub's on purpose.

---

## 9. The controlled axis

Per instruction: **Bennett's `controlled()` promotion, verbatim** (`src/controlled.jl`):

```
NOT     → CNOT(ctrl, t)
CNOT    → Toffoli(ctrl, c, t)
Toffoli → Toffoli(ctrl, c1, anc); Toffoli(anc, c2, t); Toffoli(ctrl, c1, anc)
```

with one reusable ancilla shared across the whole promoted region. This keeps the
alphabet at `{X, CX, CCX}` and costs ≤ 3× on the Toffolis.

Nested control (a doubly-nested tainted branch) ANDs the control flags into a single
flag qubit with one Toffoli, promotes against that, and uncomputes the AND — so the
promotion never needs more than one control wire.

> **v2 optimisation, recorded here so it is not rediscovered:** because every sandwich
> kernel (§5) is `compute → copy-out → reverse`, the compute and reverse halves cancel
> when the control is 0. **Only the copy-out CNOTs need controlling.** That turns a
> controlled adder from ~3× into ~1× plus W Toffolis. Not in v1, by instruction.

### The three rows Bennett does not supply — §7, and row 0

The block above covers the three classical gates and it is all `controlled.jl` has:
`third_party/bennett/src/controlled.jl` is 207 lines containing **no rotation and no
phase**, and its contract is `(ctrl, x, 0) → (ctrl, x, ctrl ? f(x) : 0)`, purely classical.
**Rule 1 has nothing to port here**, which is why the rows below are *stated* — and why
**D11** records their arithmetic in full rather than leaving Step 20 to re-derive it.

```
row 0.  ctrl is CQ_BIT_ZERO  → the whole region is skipped: 0 gates, 0 qubits
        ctrl is CQ_BIT_ONE   → the region is emitted UNCONTROLLED, verbatim
        ctrl is CQ_BIT_Q     → promote, per the rows above and D11

Ry(θ) on qubit t →  Ry(θ/2)ₜ ; CX(ctrl,t) ; Ry(−θ/2)ₜ ; CX(ctrl,t)      (exact)
Rz(φ) on qubit t →  Rz(φ/2)ₜ ; CX(ctrl,t) ; Rz(−φ/2)ₜ ; CX(ctrl,t)      (exact)
a §7 fold row    →  HARD ERROR in v1 — see D11.  BUILT at Step 20, in M06, at
                    one greppable site (cq_ctrl_refuse_fold_row), naming the row
```

**Row 0 is what keeps every zero-cost claim in this document true**, and it is the §3 fold
table's own posture one level up: a classical control is a *decision*, not a circuit. §11's
L5 shapes, §7's "0 gates, 0 qubits" constant flip and §12's classical mode all survive the
existence of this axis untouched; only a genuinely quantum control costs anything.

The two rotation rows are exact rather than up-to-phase — at `ctrl = 0` the half-rotations
cancel and at `ctrl = 1` the identity `X·R(α)·X = R(−α)` makes them add — and they stay
inside §8's frozen six entries, so **the controlled axis needs no seventh entry either**.

### Three more rows this side owes, added when M06 was built (Step 20)

`controlled.jl` cannot supply any of these either, and unlike the rotations it is not
because Bennett is classical — it is because upstream's promotion is defined only for a
control wire *disjoint from the inner circuit*, and ours is an ordinary data bit.
`controlled()` allocates `ctrl_wire = n_wires + 1` and asserts every inner gate stays
within `1:n_wires`, so the question below simply cannot arise there.

```
row A.  FOLD ON CONTROLS FIRST; PROMOTE BEFORE FOLDING ON THE TARGET
row B.  the control wire coincides with an operand of the gate it promotes
            → HARD ERROR in v1, both configurations
row C.  cq_materialise's X is NOT promoted
```

**Row A is the whole correctness of the emitter and it is not symmetric.** A §3 fold that
reads a gate's **control** is a *semantic* simplification and survives any control:
`CX(ZERO, t)` is the identity, and controlled-identity is the identity, so folding first
and never promoting is right — promoting first would emit `CCX(w, ZERO, t)` for a gate that
is not there. A fold that reads a gate's **target** is a *representation* choice and is
invalid under a quantum control: `cq_emit_x` on a constant target rewrites the constant in
place for zero gates, **unconditionally**, and inside a promoted region that runs on both
branches. Getting this one backwards is a controlled region silently made unconditional,
and it is **invisible at the all-quantum operand mask** — the only mask on which a kernel
reaches that row is the all-classical one, which is L5's. `cq_kernel_xor` at an ordinary
mixed mask is the live witness that a `CQ_BIT_ONE` target arises mid-kernel at all: a
classical ONE source bit in control position folds `dst` from ZERO to ONE for zero gates.

**Row B is well defined on one half and undefined on the other, and v1 refuses both.**
Coincidence with the **target** requests a non-injective map — `if (q) q ^= 1` sends both
|0⟩ and |1⟩ to |0⟩ — and in the Toffoli case additionally leaves the shared ancilla dirty,
because gate 3 reads a control gate 2 has already flipped. There is no correct spelling.
Coincidence with an inner **control** is by contrast exactly `q ∧ q = q`, so the right
answer is to drop the duplicate and emit the gate one promotion level down:
`CX(c,t)` under `ctrl == c` is `CX(c,t)` itself, and `CCX(c1,c2,t)` under `ctrl == c1` is
`CCX(c1,c2,t)` uncontrolled — correct *and* ancilla-clean. **v1 refuses it anyway.**
Measured over all 239 CQ_lang goldens: in every one of the 4,918 `cqrt_*_controlled` calls
the control handle is distinct from every other operand, so the collapse would be untested
behaviour sitting in the tree, and a hard error naming this row makes a re-pin that starts
emitting it loud. The arithmetic is recorded here so enabling it later is an implementation
rather than a re-derivation — the same posture D11 takes for the phases.

**Row C is `bd skh`, and it is forced rather than chosen — see D13.**

---

## 10. The uncompute axis

- **`_unc(out, srcs…)`** — re-run the kernel with `dst = out` (§4). Precondition: every
  source is still live. Postcondition, **on values only**: `out` holds
  `f(srcs) ^ f(srcs) = 0`. **`_unc` owns no qubits and reclaims nothing** — no pool
  operation, no bit-kind rewrite, no handle-table change. Every `CQ_BIT_Q` bit of `out`
  still owns its qubit index when `_unc` returns, and `cqrt_free` is the **sole** place a
  qubit ever goes back to the pool.

  > **Why `_unc` must not reclaim — Step 0.5 item 2, settled 2026-08-14.** An earlier draft
  > said `_unc` returns the qubits to the pool *and* leaves the bits as "known-zero qubits",
  > which double-frees on a following `cqrt_free`. The resolution is not a coin-toss between
  > the two halves: **CQ_lang decides reclamation per rail, and its only lever is emitting or
  > withholding the `cqrt_free`.** The adjoint is emitted unconditionally
  > (`ir-pass/src/Uncomputation.cpp:259`) and only the free is skipped (`:297-305`; `:268` —
  > *"THE FREEZE SUPPRESSES THE FREE, NEVER THE ADJOINT"*). So the **same symbol** carries
  > both dispositions: `cq_template_add_i32_hl_unc` is `_unc`'d **then freed** at
  > `ir-pass/test/lowering_io_tape_derived_uncompute.ll:57-58`, and `_unc`'d and
  > **deliberately never freed** at `tests/e2e/slice_io_chained_theta.expected.log:8`, where
  > the rail was recorded onto a kept output tape and is *entangled at the free point*
  > (`DagTraversal.cpp:343-355`). We see only the call stream and **cannot tell those two
  > apart**, so reclaiming at `_unc` would return an entangled qubit to the free list —
  > breaking I3 and reintroducing precisely the miscompile CQ_lang had already fixed, with
  > CQ_lang having no mechanism to stop us.
  >
  > Measured over the 239 pinned goldens: **25,147** `cq_template_*_unc` calls, **9** whose
  > rail is never freed, **0** double-frees — and **26,558** freed handles whose rail
  > received *no* `_unc` at all. `cqrt_free` therefore keeps full reclamation logic under
  > either rule, which is a second reason to put ownership there and nowhere else.
  >
  > **A rail that is `_unc`'d and never freed stays allocated forever. That is the intended
  > Rule-6 safe leak, not a bug to fix.**

  > **Do not assert that `out`'s bit-kinds match what the forward produced — they often
  > will not.** Because we never demote (D6), an in-place general `cqrt_ry` applied to
  > a *source* between the forward call and the uncompute point materialises bits that
  > were constants at forward time. CQ's reverse-program-order restores the source's
  > **state** before our `_unc` runs, but not our **representation** of it, so `_unc`
  > legitimately emits a larger gate sequence than the forward did. The XOR still
  > cancels — same mathematical `f(a,b)`, different circuit realising it.
  >
  > Consequences: (i) the only sound postcondition is on *values*, never on kinds;
  > (ii) L4 must pin forward and `_unc` counts **separately** — `unc == forward` is not
  > an invariant; (iii) this is the strongest argument for revisiting D6, since
  > shadow-driven demotion would restore representation stability as well as state.
  >
  > **MEASURED AT STEP 21 (2026-08-21), AND TWO THINGS ABOUT IT ARE SHARPER THAN THIS
  > NOTE USED TO BE.** *(1) It is `cqrt_ry` alone, and only off the π-lattice.* This
  > paragraph said "`cqrt_ry`/`cqrt_rz`" until Step 21; against M22 as shipped, §7's `Rz`
  > **constant** cell does nothing at any φ, the two identity rows return, and the two
  > half-turn rows flip the *constant* through `cq_emit_x` — so the general `Ry` row is
  > the **one cell of §7's twelve** that turns a constant into a wire. `K04.md` stated the
  > `Rz` version outright and was flatly wrong; `K05.md`, `K06.md` and CLAUDE.md's Rule 14
  > named the pair. All four are corrected, and the `Rz` half is now pinned *negatively*,
  > in `tests/test_unc_asym.inc:no_rz_at_any_angle_can_cause_the_drift`.
  >
  > *(2) The one cell that causes the asymmetry is the same block that poisons, so the
  > asymmetry and `bd 2cf` are inseparable.* `cq_shadow_cx/ccx` carry `unknown` from a
  > control into its target with no clearing path — which is the direction a kernel's
  > sources travel — so the `_unc` that emits the larger circuit also leaves `dst`
  > poisoned, and the following `cqrt_free` hard-errors on a rail that is physically
  > `|0⟩`. Nothing in this project can read that rail: `cq_pc_value` fails by design,
  > `rt_value` and `cq_measure` return 0 for a poisoned bit exactly as §7's *measurement*
  > specifies, and Rule 13 forbids the simulator. **The instrument that does exist is a
  > DIFFERENTIAL**, and it is what makes the cancellation witnessable at all: a source
  > lane can also be materialised by `CX(q, lane)` applied twice, which drifts the
  > representation identically and does **not** poison. **That exact SHAPE is narrower than
  > anything the corpus ships** — `tests/test_unc_asym.inc`'s header records that no pair of
  > adjacent identical `cqrt_cnot` lines exists anywhere in it, so the statistic below must
  > not be quoted for the fixture. It is the same FAMILY as the corpus's commonest free:
  > roughly half of all frees are on rails whose LAST WRITE is a bare self-inverse gate —
  > the `cqrt_toffoli` and `cqrt_cnot` rows of the last-write classification in this
  > section's `ckd.18` block, most of it `cqrt_toffoli`. **That is a DIFFERENT CUT from
  > §15 D15 §2's `U2`**, whose entry condition reduces a rail's whole write history rather
  > than reading its last write; the two populations happen to sit within about forty of
  > each other, which is exactly why neither may be quoted for the other. Measured over
  > 18 kernels × `W ∈ {2..5}` × every lane: the two routes emit the uncompute **gate for
  > gate including operands**, and only the un-poisoned one can be freed. Same circuit,
  > same input state, therefore same output — and that output is provably zero.
  >
  > *Why no golden can show any of this:* L4 measures at the all-quantum mask, where there
  > is nothing left to materialise. All 399 pinned `(kernel, W)` pairs are equal and always
  > will be, so consequence (ii)'s separate `pass` column had no passing witness until
  > Step 21 built one.
- **`_inv(srcs…)`** — **a declared-but-uncalled family on the data path. Both halves of
  the sentence that used to stand here were stale, and the mechanism it prescribed is the
  one CQ_lang deleted as a Rule-6 hazard — see §15 D14.** The retired text read: *"CQ_lang's
  spine no longer emits `_inv` for data templates; only `CompareLowering` emits it, for
  Phase-4 control flags. A compare's forward is `flag ^= pred(a,b)`, which is its own
  inverse, so `_inv` allocates a fresh rail and runs the forward."* Measured 2026-08-21 at
  Step 21: `CompareLowering` emits the **void in-place `_unc`** (`CompareLowering.cpp:204-236`,
  CQ_lang's bd `8txa`), and **`_inv` appears on zero lines of all 239 pinned goldens** while
  `cq_template_icmp_*_unc` appears on 21,323. The **premise** survives and is load-bearing —
  `flag ^= pred(a,b)` really is self-inverse at *any* entry value, which is what makes a
  recompute-and-XOR strip sound (`opcode_table.yaml:216-218`) — but the ABI consequence
  drawn from it was the opposite of the right one: the strip must be an in-place
  `_unc(rail, srcs…)` that **names the rail it restores**, not a fresh mint. The
  self-inverse property is pinned in `tests/test_unc_contract.inc`; the 915 `_inv` symbols
  still in the frozen grid are §15 D14's.
- **`cqrt_free(h)`** — **the one and only operation that returns qubits to the pool.**
  Assert the rail is provably clean, return every qubit `h` still owns, and mark the handle
  dead (a tombstone, D5). A free of a rail that is not provably clean is a hard error, not a
  warning: it is the exact signature of a silent state collapse. Because `_unc` reclaims
  nothing, a rail's qubits are returned **exactly once** — here — whether or not an `_unc`
  preceded it.

  > **The assert is scoped to the rail's QUBIT-CARRYING bits, and that scope is
  > load-bearing.** Read literally, "every bit is `BIT_ZERO` or a known-zero qubit"
  > rejects a `CQ_BIT_ONE` bit — which aborts on `int x = 5;` going out of scope, i.e. on
  > every ordinary classical local, and would make §11's L5 (zero gates, zero qubits,
  > fully classical) unreachable. By **I4** an all-constant rail owns zero qubits, so
  > nothing can reach the free list and nothing can collapse; a constant bit carries a
  > canonical `q == 0` and owns no index at all. The operative wording is this bullet's
  > own "return every qubit `h` still owns". Settled at Step 7; CLAUDE.md's Rule 6 was the
  > imprecise restatement and has been corrected to match.
  >
  > **This does NOT reach `ckd.18`, which is §15 D15's carve-out.** There the rail's bits
  > *are* qubits — `ry` at arbitrary θ materialises all of them — physically holding
  > `|birth-literal⟩` with an unknown shadow, so the scope clarification exempts constants
  > and not materialised bits. `ckd.18` **closed 2026-08-22**: under D15 those rails are
  > **provably dirty** rather than unprovable, because the certificate reads the birth
  > literal and the cancelling pair that the shadow cannot see. **A proven-dirty rail is
  > STRANDED, not aborted — D15 §4's last clause, confirmed 2026-08-22 at the start of
  > Step 23.** Read it there; do not restate the argument here.

  > **THE STRUCTURAL ZERO CERTIFICATE — `ckd.17a`, settled 2026-08-15.**
  >
  > **It is not stored, because it is not a thing. It is an act.** `ckd.17` asked whether
  > the certificate lives per qubit in the shadow or per register in M07; both presuppose
  > storage and both are wrong. What lands is a **retirement** of the shadow entry of an
  > index that has *already gone back to the pool*:
  >
  > ```c
  > void cq_shadow_retire(cq_shadow_table *sh, uint32_t q);   /* bd ckd.17 */
  > ```
  >
  > **The governing rule, which decides every candidate stamper mechanically:** *a
  > certificate may only be written on a qubit that has already left data use* — the write
  > runs strictly **after** `cq_qubits_release` has returned for that index. One joint
  > enforces it, and the order is the enforcement:
  >
  > ```c
  > void cq_ctx_release_qubit(cq_ctx *ctx, uint32_t q, int proven_zero)
  > {
  >     cq_qubits_release(&ctx->pool, q, proven_zero);   /* aborts unless proven */
  >     cq_shadow_retire(&ctx->shadow, q);               /* reached only if it did */
  > }
  > ```
  >
  > **This is therefore NOT the "sanctioned exception to shadow conservatism" the bead
  > feared.** It never runs on a live qubit, so poison stays sticky for every qubit any live
  > bit holds. And by **I3** an index on the free list *is* `|0⟩`, so `{value 0, unknown 0}`
  > is the **correct** entry for it — bit-for-bit what `cq_shadow_ensure` already writes for
  > a freshly *minted* index. Birth and retirement are one rule; only the fact that `minted`
  > never decreases had hidden that. Both bytes are always written: a bit poisoned while it
  > held 1 has a **frozen** value byte (`X` is a no-op under poison, and `cx`/`ccx` update
  > `value` only when `!unknown`), so clearing `unknown` alone would publish a stale byte as
  > determinate.
  >
  > A qubit that is still live must **never** carry a certificate, and the reason is
  > structural rather than statistical: a certified-but-live qubit read as a **control** hits
  > `t.unknown |= c.unknown`, so with `c.unknown` freshly zeroed **the poison stops
  > propagating** and the shadow starts claiming determinate downstream of a genuine
  > superposition — the one direction the shadow discipline forbids. That alone disqualifies
  > the `_unc` epilogue as a stamper, before any counting argument.
  >
  > **Who may stamp: one named literal per CONSTRUCTION, and until Step 20 there was
  > exactly one — M09's.** The sandwich driver owns the scratch
  > qubits end to end — it materialises the region at step 0 (I6(b)) and releases it in its
  > own epilogue, passing `CQ_ZERO_BY_PALINDROME`. That literal is the **first** `proven_zero`
  > constant in `src/`, and it is irreducible: Rule 13 forbids the library holding the gate
  > stream that would let it *compute* the answer, so "a sandwich cleans its own scratch" is
  > asserted exactly once, by the code that owns the construction. **It rests on three
  > premises — one gate per step (`ckd.14a`), I6(a), and I6(b) — and deleting any one makes
  > it a laundering site.** Not the `_unc` epilogue, not M07, not M08 (which ships no release
  > a kernel can call), and not a kernel — there is no API through which one could.
  >
  > **STEP 20 ADDED A SECOND ONE, AND AMENDED TWO OF THOSE PREMISES.**
  > `CQ_ZERO_BY_CTRL_UNCOMPUTE`, in M06, covers §9's shared Toffoli ancilla and the nested
  > AND flag. It rests on three premises of its own: each wire is targeted ONLY by an
  > identical PAIR of Toffolis (gates 1 and 3 of one promoted block, or the AND at push and
  > its twin at pop) and a `CCX` is its own inverse; nothing else can target either, since
  > they live in no register and no scratch region and `src/controlled.c` is their only
  > writer; and they come off the pool, so they are born |0⟩ by I3.
  >
  > **It may NOT be `cq_shadow_known_zero`, and the trap is that the wrong choice passes.**
  > The shadow's `CCX` rule is `t.unknown |= a.unknown | b.unknown` with no clearing path,
  > so a POISONED control wire leaves the ancilla at `{value 0, unknown 1}` and a shadow
  > check would refuse to release a qubit that is provably |0⟩ by construction — in both
  > configurations. On the whole surface Step 20's own gate exercises, every wire is
  > determinate (no kernel suite rotates), so the unsound evidence agrees with the sound one
  > and the abort waits for the corpus. `tests/test_controlled_region.inc` drives a poisoned
  > wire on purpose for exactly that reason.
  >
  > **The two amended premises.** (1) *One gate per step* becomes **one INVOLUTION per
  > step**: a promoted Toffoli is three gates, and the block `A·B·A` is a palindrome of
  > self-inverse gates, so `(ABA)² = I` and the driver's replay-at-the-same-index still
  > cancels — and the recorded stream stays a palindrome, since a palindromic block reversed
  > is itself. (2) *I6(a), every compute-half gate target is a scratch bit*, becomes **every
  > gate target A KERNEL NAMES is a scratch bit**: the promotion introduces one target that
  > is not, M06's own ancilla, and it is sound there because the pair of Toffolis that touch
  > it is self-inverse within one step. `cq_emit_cx_phys`/`cq_emit_ccx_phys` therefore do not
  > re-run `check_target`, which the public entry points have already done on the caller's
  > target. Widening the extent to cover the ancilla instead would silently disarm I6(a) for
  > the whole compute half — the same wrong fix `sandwich.h` already records for the copyout.
  >
  > **Verified, never recomputed.** Recomputing the expected value is a simulator, which is
  > forbidden outright. Instead: `cq_shadow_retire` hard-errors in **both** configurations if
  > the entry is determinate and non-zero — a complete detector of a non-cancelling sandwich
  > across the whole rotation-free kernel surface (Steps 10–17), because a general `Ry` is
  > the only producer of `unknown` (D12). It is **inert on the L6 corpus**, where nearly every rail is
  > rotation-tainted, so never report an L6 run as evidence that the certificate held. The
  > ordered-stream palindrome check in `mock_sink` is the only thing with teeth on the
  > poisoned surface, and it lives in `tests/`, where Rule 13 permits the recording.
  >
  > **`ckd.17b` — a CQ_lang rail at `cqrt_free` — IS SETTLED, as of 2026-08-22, and it is
  > §15 D15. Read that decision; this paragraph is its SUMMARY and carries conclusions
  > only** — every argument behind them, and every count **except one**, lives in D15's
  > numbered sections and is not repeated here. The exception is the last-write
  > classification below, which is measured nowhere else in the tree: §10 is its HOME, not a
  > restatement of it, and deleting it as a duplicate loses it. What else belongs to §10 is
  > the negative result: a sandwich
  > certificate reaches *none* of the corpus's frees, because none of them is on a scratch
  > region.
  >
  > **The resolution is a LAYERING statement first: the `|0⟩` obligation is CQ_lang's, and
  > it cannot be discharged at our layer at all.** Upstream states the precondition as a
  > non-negotiable principle and discharges it at the **IR layer**, where the rail is an SSA
  > name and the loop algebra is still present; it also states the limit from our side, that
  > no trace label can witness "freed at `|0⟩`" **at any ABI**. So v1 does not duplicate a
  > proof it cannot perform. What v1 owes is the *operation* — and Rule 7's involution is
  > what L3 tests on every case of every kernel. Both quotations, with line references, are
  > **§15 D15 §1**.
  >
  > **What v1 holds instead is an OBSERVED UNDO CERTIFICATE over the call stream** — the
  > rail's write history reduces to the identity (U1 an `_unc` naming the rail; U2 a
  > self-inverse pair-off; U3 an `addc` sum) — which discharges the overwhelming majority of
  > the corpus's frees, where the shadow discharges **none**. The three entry conditions,
  > their preconditions and the two ways of misreading them are **§15 D15 §2**, which also
  > carries the ratio and the residue; the revision they are pinned against is **§0**.
  >
  > **The EPISTEMIC STATE is THREE-VALUED and the ACT is TWO-VALUED, and the residue is
  > STRANDED rather than released.** Proven-clean releases; **proven-dirty and unproven ALIKE
  > are STRANDED** — never released, never on the free list, counted, first occurrence named
  > on `stderr`, and the program continues. (This paragraph said *"proven-dirty is a hard
  > error in both configurations"* until 2026-08-22, which was **§3 as drafted** and is
  > superseded by **§4's last clause**, confirmed the same day: a rail the certificate
  > convicts takes the same ACT as one it cannot reach, so the free path has two dispositions
  > and the three rows survive only in the REPORT. Rule 6's hard error is unmoved where it
  > was aimed — a *release* of a non-`|0⟩` index — which is the row that can no longer arise.) **The layering licenses NOT ABORTING; it does not license
  > RECYCLING**, so `CQOPS_FREE_TRUST`'s prohibition is **narrowed and KEPT**, not lifted:
  > releasing an unproven index to the pool stays forbidden. **`CQOPS_FREE_ABORT` survives as
  > a development flag** and is not the default. Why each of those is the right side, and why
  > stranding is "never release" rather than a third pool bucket, is **§15 D15 §3** — argued
  > there once, against `src/qubits.h`'s own release contract; do not re-derive it here.
  >
  > **`ckd.18` FOLDS IN AS THE CARVE-OUT, AND ITS PREMISE WAS WRONG.** Classifying all
  > **51,696** frees by how the rail was *last written* — **a measurement recorded nowhere
  > else in the tree**, which is why this block carries it rather than citing one: 25,138
  > `cq_template_*_unc`, 19,153 `cqrt_toffoli`, 7,223 `cqrt_cnot`, 91 `cqrt_copy`, 40
  > `cqrt_addc`, **25 `cqrt_ry`**, 15 `cqrt_cswap`, 11 `cqrt_qram_load_unc`, and **0
  > `cqrt_rz`**. **That total is what DATES the classification**: it is the pre-`397c67c`
  > snapshot, and §15 D15 §0 records the corpus moving past it — three times, in three
  > separate golden-set changes (this read "twice" until 2026-09-10, `bd 1ti`) — inside
  > one day, and records that nothing in this repo pins CQ_lang. Re-measure before quoting
  > any row of it.
  > The `cqrt_ry` row is exactly the rails born from a **non-zero** `cqrt_alloc` literal
  > whose only writes are a cancelling `(θ, −θ)` `ry` pair, physically `|birth-literal⟩` at
  > the free — and the bead's
  > premise, that the `|0⟩` requirement there was *"OURS (Rule 6, I3)"*, is **false**;
  > upstream states it (D15 §1). They are also **not unprovable**: the certificate reads the
  > birth literal, the cancelling pair and the absence of any other write, which makes them
  > **provably dirty** rather than unknown, so they never reach the free list. **That row is
  > STRANDED rather than aborted — D15 §4's last clause, confirmed 2026-08-22. Read it
  > there; do not restate the argument here.** The positive control is in the same corpus: the *other* freed rails
  > born from a non-zero literal are returned to `|0⟩` by an explicit `cqrt_addc_<W>(h, −L)`,
  > which is the certificate's **U3** (D15 §2). That control was **checked, not assumed** —
  > the immediates summed to `−L` in every one of them, with no exceptions to explain away.
  >
  > **Two traps recorded with it, and the second was itself a false claim.** The rest of the
  > tree cites them by the literal labels below, so do not renumber them or insert a third
  > between them.
  >
  > **Trap (i).** The bead's
  > older *"37 rotation-rooted frees, of which 12 are `rz`-rooted on rails born 0"* is
  > **false** — a write-model error, reproduced to the unit by executing the counterfactual.
  > The rule it violated is **§15 D15 §6(ii)**: `cqrt_cswap` writes BOTH data args
  > (`cq_runtime.h:258`), and a certificate that misses it silently widens every rule, so
  > enumerate a write set from `cq_runtime.h` and never from the symbol names.
  >
  > **Trap (ii).** **The claim that those twelve rails "free CLEANLY under D12" is FALSE.**
  > (The "twelve" is trap (i)'s artefact; the true population is the thirty named below.)
  > It was
  > carried in about a dozen sites across the tree — including this section, §15 D12's own
  > note, three in `src/`, and a TEST NAME, which outlives a comment — and all of them were
  > cleared on 2026-08-22; `bd 06t`'s notes hold the list, which is where it belongs rather
  > than in a document that would have to be re-verified to stay true. All 30 rails whose
  > last rotation is an `rz` are unprovable by the shadow: the shape is
  > `alloc(0); cswap(qflag, src, tmp); rz; cswap; free`, and
  > `cq_shadow_ccx`'s `t.unknown |= a.unknown | b.unknown` carries the **swapped-in data
  > rail's** poison into `tmp` whatever the flag is. The test that pins it,
  > `tests/test_rotate.c`'s `an_rz_in_a_cswap_bracket_on_determinate_operands_frees_cleanly`,
  > mints **both** operands with `cq_bk_reg` — determinate
  > quantum rails, which the corpus never has — and its own comment says so: *"the shadow
  > tracks both exactly, because nothing here poisoned."* **D12 itself is unaffected**: an
  > `Rz` genuinely does not poison. What is false is the corollary that D12 buys these rails
  > their free — and under D15 they are discharged by **U2**, not by the shadow. **This
  > mechanism is stated in full in exactly two places — here and in §15 D12's own note.**
  > Cite one of them; do not restate it a third time.

  > **What "provably clean" reads is SETTLED — it is §15 D15 — and the answer is different
  > on the two surfaces.** The retired text opened *"NOT settled by this bullet, and it is
  > not obvious"*, and its diagnosis was right even though its remedy was wrong: it cannot
  > be the two-bit shadow **on a tainted rail**, because §3's `CX` rule propagates `unknown`
  > and an uncomputed rotation-tainted rail is all-`Q unknown`. Where it went wrong was in
  > concluding that the evidence therefore needs *"one sanctioned un-poisoning write, the
  > sole exception to §3's conservative-in-the-safe-direction-only"*. **No such write is
  > added, and none may be.** The evidence is an OBSERVED UNDO CERTIFICATE at the M26
  > handle boundary, which reads the call stream rather than the shadow and so needs no
  > exception to anything. **Do not** weaken the STRAND to a release to make a fixture pass —
  > that, and not the abort, is the clause this sentence exists to protect. (It read *"do not
  > weaken the proven-dirty hard error to a warning"* until 2026-08-22. §4's last clause
  > settled that row as STRAND, so the hard error it named is no longer the default
  > disposition; what survives absolutely is that nothing unproven or convicted reaches the
  > pool. `CQOPS_FREE_ABORT` restores termination on demand.)
  >
  > **CORRECTED 2026-08-15 at Step 10, and the correction is still operative.** A general
  > `Ry` is the ONLY producer of `unknown` (`src/shadow.c` is its sole writer, and since
  > **D12** its only caller is M22's general-`Ry` row); `CX` and `CCX` merely propagate what
  > is already there. So **on the rotation-free surface — Steps 10 through 17, every kernel
  > and no rotation — nothing is tainted, every shadow entry is determinate, and
  > `cq_shadow_known_zero` is not conservative but EXACT.** It is a genuine free-time proof
  > there, and it stays the proof the kernel suites use: Step 10 onward frees its result
  > rails through `cq_reg_free` with
  > `tests/support/poolcheck.c:cq_pc_zero_proof_rotation_free`, in both configurations.
  >
  > **What changed at D15 is the OTHER surface, and the measurement is what settles it.**
  > `cq_pc_zero_proof_rotation_free`'s name has always been its scope, and it becomes a
  > laundering device the moment a rail is rotation-tainted. Measured across the whole
  > corpus: **not one qubit reaching a `cqrt_free` is ever determinate, at any point in any
  > trace** — the shadow discharges **none** of them and convicts **none** (§15 D15 §2; the
  > counts and the revision they are pinned against are §0). So the two surfaces
  > do not overlap at all. The shadow is exact and sufficient where no rotation has
  > happened; the certificate is what carries L6, where one always has.

---

## 11. Test strategy

The whole point of the tri-valued design is that levels 1–3 need no quantum simulation.

| Level | What | How |
|---|---|---|
| L0 | Fold table | Unit tests over all operand-state combinations in §3 |
| L1 | **Kernel differential** | For each kernel, compare the result register's **value** against the C operator, over **a small constant number of random samples**: `cq_kd_samples()`, default **32**, per `(kernel, width)`, nothing scaling in `W`. Each case draws a bit-kind mask **pair** *and* a value tuple **jointly** from one seeded RNG, with the **all-classical** pair (which *is* L5), the **all-quantum** pair (which is what L4 pins) and the four value corners taken first — **inside** the budget, not on top of it. Widths are enumerated, never sampled. **A sample, not a product** — see the note below |
| L2 | Ancilla-clean | After every kernel call, assert **no index is live that no named register owns**, and that every index a named register owns is live |
| L3 | Uncompute round-trip | forward → `_unc` → assert `dst`'s **values** are all-zero. The pool is **not** restored yet — `_unc` reclaims nothing (§10). The harness then frees `dst` explicitly and asserts **`live` is back to its pre-call value and every index `dst` held is back on the free list** |
| L4 | Gate-count goldens | Pin per-kernel `(NOT, CNOT, Toffoli)` at each W. Cross-check against Bennett's published baselines where the construction matches, and document every deliberate delta. See the arity and staleness notes below — both bit an earlier draft |
| L5 | Classical short-circuit | Assert **zero** gates and **zero** qubits for the fully-classical case, and exactly 1 qubit / 1 CX for `int a = 0; a \|= b << 3` |
| L6 | CQ_lang e2e | **Link against CQ_lang's existing fixtures, RUN them, and do not abort** (§15 **D18**). ~~diff emitted traces~~ — the goldens are CQ_lang's regression oracle for its OWN IR pass, captured against a *"trace-only runtime stub"* that stops existing once the real backend is linked. Measured before retiring it: every stub measure body returns a LITERAL, so all 266 golden measure lines read `-> 0` whatever the circuit computes, and our handle numbering already diverges because `cqrt_addc` and D7b mint rails the ABI does not name. Correctness is carried by L1–L5; L6 adds the one claim they cannot make |
| L7 | Grover | §12, and **its three claims run in different modes** (§15 **D22**). (1) is L6-shaped, opt-in behind `-DCQOPS_CQLANG_DIR=`, and adds *emits a non-empty gate stream* to D18's gate. (2) is CLASSICAL mode: the value equals plain C **at zero gates and zero qubits** — L5 at program scale, not a proof of the oracle's circuits. (3) is QUANTUM mode: Toffoli count from the counting sink, peak from `cq_qubits_peak()`, and the T-count from **M25's `qec_count(QEC_GATE_T)`**, not from `cq_count_t` |

L1 and L5 are the two that actually catch bugs. L4 is what stops a "harmless" refactor
from silently doubling the T-count.

> **L1 IS A SAMPLE, NOT A PRODUCT (2026-08-21). ONE CONSTANT, AND NOTHING IN IT SCALES
> WITH `W`.** This row read "the full cross product at W ∈ {1,2,3,4,5}; structured corners
> + seeded sampling, crossed with every mask pair, at W = 8 and above". **Both** factors of
> that product grew. The value factor was `span²` below W = 6 — 1,024 pairs at W = 5 — and
> a structured set of ~`5W+14` at W = 8. The **mask** factor is `cq_bk_fixed_pairs`, which
> is 12 named rows **plus a one-bit-quantum sweep across all `W` positions**, so it is
> `O(W)`. Measured across the suite before the change: **~2,100,000 L1 cases** — 522,080 in
> the compare suite alone, 372,660 in shift, 363,204 in bitwise — for a Debug run of
> **63.8 s** against a Release run of 4.1 s.
>
> Every L1 case runs a real circuit and reads `dst`'s value back through the shadow, so the
> case count **is** the suite's wall clock. The budget is now `cq_kd_samples()` per
> `(kernel, width)`: **~28,400 cases, Debug 22.6 s, Release 2.4 s**, 195/195 green in both
> configurations. `CQOPS_L1_SAMPLES` overrides the constant in the **environment** — never
> in a test's CMake `ENVIRONMENT` property, which would win over the shell, the same trap
> `CQOPS_UPDATE_GOLDENS` has.
>
> **FOUR ANCHORS SIT INSIDE THE BUDGET, NOT ON TOP OF IT**, each supplying something a draw
> cannot. **Case 0** is the all-classical mask pair: that row *is* L5, and the driver
> asserts zero gates and zero qubits on it, so leaving it to a 1-in-`(W+12)` draw would make
> L5 run only sometimes. **Case 1** is the all-quantum pair — the mask every L4 golden is
> pinned at, and the fixed point of §15 D6's no-demotion rule. **Cases 2–5** are the four
> value corners: a masking bug lives at 0 and all-ones, and a uniform draw reaches them with
> probability ~0 at W = 64.
>
> **WHAT THIS GAVE UP, DELIBERATELY.** The named mask rows other than all-classical and
> all-quantum — alternating, lsb-only, msb-only and **risk R8's six asymmetric pairs** — and
> the one-bit-quantum lane sweep are no longer *enumerated* at every width; they are rows in
> the pool the draw samples from. Across the width ladder and §9's four control regions each
> is still drawn many times, but **no single run guarantees any one of them.** This was an
> explicit instruction, not drift; do not restore the enumeration without asking.
>
> **WIDTHS ARE ENUMERATED, NEVER SAMPLED.** Every kernel is width-generic over `reg->width`
> with no width switch (I5), so what a wide width exercises that a narrow one does not is a
> loop bound, an MSB boundary or a carry that only exists above some length — exactly the
> faults a sweep exists to catch. Sampling widths would leave those to the draw. Every
> suite's ladder is preserved, i80, i128 and the cast width **pairs** included.
>
> **WHY A SMALL CONSTANT IS ENOUGH — the 2026-08-16 argument, which still holds.** The §3
> fold table dispatches on a bit's **kind**, never on a qubit's value (§15 D6), and kernels
> are width-generic, so **at the all-quantum mask the emitted circuit is byte-for-byte
> identical across all 65,536 value pairs at W = 8** — the old suite ran one fixed gate
> sequence 65,536 times through the classical shadow. **Values reach the circuit only
> through classical lanes, and only as one bit per lane:** a classical `ZERO` control folds
> its gate away, a classical `ONE` rewrites it (`CX`→`X`, `CCX`→`CX`) and removes none.
>
> **VERIFIED RATHER THAN ARGUED:** the suite is green at `CQOPS_L1_SAMPLES=256`, eight times
> the default depth, so 32 is not masking a failure. Seeds are `FNV-1a(kernel name) ^ W`, so
> a red case reproduces from a bare re-run and no two kernels or widths share a sequence, and
> every width prints its case count, its mask-pool size and its seed — the no-silent-caps
> property is unchanged.

> **Three corrections made at Step 10, when the first kernel forced each of them from
> prose into code. All three rows above are the corrected wording.**
>
> **L1 does not read "the shadow".** `shadow(dst)` is undefined for the bits of `dst` that
> are still constants — and under the all-classical mask, which this same table calls L5,
> *every* bit of `dst` is a constant and there is no shadow to read at all. The oracle is
> the register's **value**: a constant bit contributes its kind, a qubit-carrying bit its
> shadow value. `cqrt_measure` cannot serve — it emits `sink.mz`, which would pollute L4's
> counts, and it is terminal (§7), which would make L3's free impossible.
> `tests/support/poolcheck.c:cq_pc_value` is the reader.
>
> **L2 as originally worded was literally false whenever an operand was quantum.** An
> operand register with any `CQ_BIT_Q` bit owns live qubits at the moment of the assertion,
> and they are nobody's leak; only by I4 does "exactly `dst`'s qubits" ever hold. The
> operative claim is the union form above. A **count** comparison is strictly weaker and
> must not be substituted — leak one index and hand back another and the totals still
> agree. `cq_pc_live_is_exactly` implements the set form.
>
> **L3 cannot compare `minted` or the free-list length.** Both are monotone
> (`src/qubits.h`: "minted == live + free", "peak == minted"), so a round trip that
> allocates `dst`'s qubits and returns them necessarily leaves `minted` higher. Requiring
> them to match is requiring the kernel never to allocate — an earlier draft of the Step 10
> driver did exactly that and failed 1,276,416 cases on its first run. What is both correct
> and *stronger* is the per-index check: name `dst`'s indices before the free, and assert
> each is back on the free list after. That also catches a free that released the wrong
> index, which no count can see.
>
> **A mask is a PAIR, one per operand.** Every normative sentence here and in the plan says
> "masks" in the singular, but risk R8's own mandated fixed witness is "`a` all `Q`, `b` all
> `ZERO`" — an asymmetric pair, inexpressible if one mask applies to both operands. The two
> operands are independent channels and the §3 fold table treats them so; a suite that
> varies them together tests the diagonal of the space and calls it the space.

**L0's size is 159** — `5 X + 25 CX + 125 CCX = 155` exhaustive cases (the full Cartesian
product over the five operand kinds) plus the **4** distinctness death-tests of §3. Each of
the 155 pins a *4-tuple* — gates emitted, qubits allocated, resulting bit-kind, resulting
shadow — which is four **assertions** per case, not four cases. See `IMPLEMENTATION_PLAN.md`
§4 Step 6.

**L4's golden tuple arity (Step 0.5 item 3 — resolved).** `x+1` at Int8 was quoted as
`58/6/40/12`, four numbers against the three-tuple `(NOT, CNOT, Toffoli)`. There is no
contradiction: Bennett's `gate_count` returns a **4-field NamedTuple**
`(total, NOT, CNOT, Toffoli)` (`src/diagnostics.jl:25`, and printed that way at
`README.md:114`), so the leading `58` is the redundant sum — `6 + 40 + 12 = 58`. Pin the
three-tuple; carry `total` only as a checksum. **Always match the full tuple, never `total`
alone:** two unrelated upstream circuits both total 114 (`x+1` at i16 is `114/6/80/28`, while
a 4-entry QROM s-box is `114/10/96/8`).

**Which upstream baseline to cross-check against (Step 0.4 — resolved).** Bennett publishes
*two* figures for `x+1` at i8 and they disagree: `100/4/68/28` in `BENCHMARKS.md:9` and
`58/6/40/12` in `README.md:114` / `third_party/bennett/CLAUDE.md:27`. **`BENCHMARKS.md` is stale** — it was
generated under the pre-U27/U28 defaults (`add=:cuccaro`, `fold_constants=false`), its own
regression test names those as the old defaults, and its generator cannot even run at the
pinned commit (it calls a `_reset_names!` that has since been deleted). Pin against
**`58/6/40/12`**. The comparison is legitimate rather than approximate because Bennett's
*global* wrap is `2F + W` (`bennett_transform.jl:343-358`) — algebraically identical to our
*local* sandwich — so for a single-kernel expression the two coincide exactly. It does **not**
extend to multi-kernel expressions, where Bennett pays `2·Σ Fᵢ + W` and we pay `Σ (2Fᵢ + Wᵢ)`,
strictly more. Full derivation and the confirmed closed forms
(`total = 7W+2`, `NOT = 6`, `CNOT = 5W`, `Toffoli = 2W−4`) are in
[`docs/constructions/BASELINES.md`](docs/constructions/BASELINES.md).

> **`x+1` is a *constant increment with folding on*; K6 is a *general two-register add*.**
> Do not pin K6 against 58. Read straight off `adder.jl`, the general adder's compute half is
> `5W−2` and its sandwiched total `11W−4` — **84** at i8, not 58, and `4W−4` Toffolis rather
> than `2W−4`. Pinning K6 against the constant-increment baseline would under-count by 26
> gates and 16 Toffolis, more than half the T-count. K1–K5, K9, K10 and K12 have **no**
> published upstream counts at all, so their goldens are necessarily self-pinned from our own
> port — which is exactly why `x+1` @ i8 is the *one* place the port is validated against
> upstream rather than against itself.

---

## 12. Acceptance — Grover in plain C

```c
#include <CQ.h>
int main(void) {
    unsigned x = 0;
    x = cq_theta(x, M_PI/2);            /* Ry(π/2) on each bit: |0…0> → uniform */
    for (int it = 0; it < ITERS; it++) {
        unsigned y = f(x);              /* plain C arithmetic — the oracle       */
        _Bool hit  = (y == TARGET);     /* icmp → flag handle                    */
        hit = cq_phi(hit, M_PI);        /* Rz(π): phase flip on the marked state */
        /* diffusion: H on every bit — Sturm's H! = (φ += π; θ += π/2)           */
        x = cq_phi(x, M_PI);
        x = cq_theta(x, M_PI/2);
        /* … */
    }
    return cq_measure(x);
}
```

> **THIS LISTING IS THE SOURCE INTENT, NOT THE ACCEPTANCE ARTEFACT, AND SAYING SO IS
> `bd ye7`'s FIRST HALF (resolved 2026-08-28 as §15 D22).** Two things about it were
> measured at Step 25 and neither is a wording nit. **(i) It does not lower.** CQ_lang's
> own pass declines it — `cq rotation lowering: non-diagonal gate over a live derived
> record` — because `y` and `hit` are records derived from `x` that are still live when
> the diffusion's `cq_theta(x, …)` fires. That is an upstream guard doing its job, and
> the spelling that lowers today is the one CQ_lang's own
> `tests/e2e/slice_control_seq_grover.c` uses: the mark is a **tainted-condition region**
> (`if (cond) cq_phi(M_PI);`) whose flag and work rail the region teardown strips
> and frees before the non-diagonal gate. **(ii) The three claims below do not share a
> mode**, so a step written to the listing as printed pins (3) against a classical run and
> reports a vacuously green gate. Read (1), (2) and (3) as three separate programs run in
> two different modes, not as three readings of one run.
>
> **(iii) SINCE 2026-09-05 THE LISTING ABOVE DOES NOT COMPILE EITHER, AND THAT IS A
> STRONGER STATEMENT THAN (i)** (`bd cxp`, measured 2026-09-10 at CQ_lang `5a26204`).
> CQ_lang retired the 15-width `cq_phi_<sfx>(x, angle)` family **outright**, with
> deliberately no migration shim, and ships one register-free `void cq_phi(double)` in
> its place — a **branch** phase, which under a tainted condition lowers by KICKBACK onto
> the minted flag (`cqrt_rz_i1`, uncontrolled) and touches no data register.
> `cq_theta(x, angle)` is UNCHANGED, so this is one intrinsic changing shape rather than
> an API break. **The listing keeps both `cq_phi(hit, M_PI)` and `cq_phi(x, M_PI)` as
> source intent, per this note's own first sentence — but a reader transcribing it now
> gets `too many arguments to function call`, not a decline.** The spelling that lowers is
> `cq_phi(M_PI);` as a statement, and it is carried by two files: `tools/l7/grover.cq.c`
> (migrated `bd 2tm`, its header derives the whole thing) and `tests/test_grover.c`, which
> spelled the kickback form `cqrt_rz_i1(hit, M_PI)` through the frozen ABI from the start.
>
> **AND THE CHANGE IS SEMANTIC, NOT A SIGNATURE** — upstream's finding, not ours
> (`include/CQ.h` at `cq_phi`). The retired spelling emitted a **TENSORED**
> `cqrt_rz_<W>` — `Rz` on every qubit of the register — whose marked phase is
> `(−1)^popcount(target)` and which, being diagonal per bit, **FIXES `|0…0>`**. So a
> reflection about `|0>` could never put −1 there: *"the oracle marks the target"* and
> *"reflect about `|0>`"* were **true of the source and false of the emitted circuit**,
> and every Grover golden built on that spelling was the identity on `x`. It becomes true
> from the migration on. **Nothing in L7 could see this** — a tensored `Rz` emits MORE
> `rz` lines than the kickback form, so D22's own non-empty gate MIX was green throughout;
> what separates the two circuits is WHICH QUBIT, now asserted as `l7_run.py`'s clause 6
> (every `rz` target disjoint from the `mz` targets) and recorded at
> `bd remember l7-gate-mix-could-not-see-the-tensored-rz`.

v1 is accepted when:

1. **[quantum mode, through CQ_lang]** This compiles through `cqc` — CQ_lang's front end
   and its `cq-lowering` pass — links against `libcqops` **and against no CQ_lang
   runtime archive**, runs, does not abort, and **emits a non-empty gate stream**. That
   last clause is what makes this claim more than L6's: PRD §15 D18's gate is *link, run,
   do not abort*, which a program that folded away entirely would also satisfy. It needs
   CQ_lang, so it is L6-shaped and **opt-in behind `-DCQOPS_CQLANG_DIR=`**; a report that
   cannot name the CQ_lang revision it ran against is not a report.
2. **[classical mode]** Replacing `M_PI/2` with `M_PI` makes the whole program run
   deterministically and `cq_measure` returns the value plain C would compute, **at zero
   gates and zero qubits**. ~~proving the oracle's arithmetic circuits are correct~~ —
   **that parenthetical is RETIRED (§15 D22)**: with `x` all-constant every kernel takes
   its R9 short-circuit and the §3 fold table produces the value outright, so the mul and
   the icmp are never BUILT and nothing about their circuits was tested by that run. What
   this row actually proves is **L5 at program scale**, which is worth asserting and is
   why the "zero gates and zero qubits" half is now part of the criterion rather than a
   remark. The oracle's circuits are verified by **L1**, per kernel, at mixed bit-kind
   masks — NORTH_STAR condition 2, met at Steps 10–17.
3. **[quantum mode]** The counter sink reports the **Toffoli count** and **`cq_qubits_peak()`
   reports peak qubits** — **not** the sink, see the §8 correction for why a streaming
   sink cannot compute it and the pool already has it exactly — and both are stable
   across runs and pinned as goldens. Classical mode's counts are all zero, so a golden
   taken there is a tuple of zeroes; **(3) is measured in the quantum mode and only
   there.** **The T-COUNT IS NOT `cq_count_t` (§15 D22, `bd qi9`).** That counter is
   `7 × ccx`, ported from Bennett's `t_count`, and it is a **lower bound** the moment §7's
   general rows fire — which is exactly what quantum mode does. Its honest home is **M25**,
   `qec_count(ctx, QEC_GATE_T)`, and what M25 supplies is the term `cq_count_t` cannot
   see: the **rotation** term. Measured at Step 25, the whole rotation alphabet this
   program emits is `{Ry(π/2), Rz(π)}` and **both are T-free** through the qec sink, so
   for *this* program the bound is **tight** and the pinned T-count is exactly `7 × ccx`.
   That is a measurement, not an assumption, and it is what the acceptance gate asserts —
   never `7 × ccx` on its own, which would be the assumption wearing the measurement's
   clothes.

---

## 13. Delivery increments

> **THE INCREMENTS ARE THE COARSER VIEW; PLAN §4's PER-STEP GATE GOVERNS (2026-09-10,
> `bd eh9`).** This table groups the work into shippable units and says what each unit owes
> *as a unit*. It is neither the schedule nor the per-kernel test gate: CLAUDE.md gives
> `IMPLEMENTATION_PLAN.md` the *how and when* role, and plan §4's Phase B applies the **same
> four-part gate — L1, L2, L3, L4 — to every kernel step**, "applied automatically by the
> shared kernel driver rather than written per kernel", with **L5** riding inside L1's
> all-classical mask pair. So where a row below names *fewer* levels than that gate, or schedules
> a level the gate has already been applying, the plan wins and the row is stale rather than
> permissive. **Two rows were stale that way and are corrected in place: 2** (which named three
> levels where five were delivered) **and 7** (which scheduled L3 that the gate discharges from
> increment 2). Rows 3–5 name a subset of what their steps deliver — L5 is green for every Rule 7
> kernel in them, and row 5 already records K8 as the one kernel it cannot apply to — and row 6
> names L5 outright; none of the four is rewritten, because what each of them asserts is true.

| # | Increment | Exit criterion |
|---|---|---|
| 1 | Foundation: `cq_bit`, register/handle table, qubit pool + free list, shadow, sink vtable, printf + counter sinks, emitter fold table, `cqrt_alloc/measure/free`, `cqrt_x/cnot/toffoli` | L0 green; alloc→measure round-trips; fold table exhaustively tested |
| 2 | K1–K5 (bitwise, constant shifts, casts) + `_hl`/`_lh` folding | ~~L1/L2/L5 green for these ops~~ **L1–L5 green for these ops — corrected 2026-09-10 (`bd eh9`) to what Steps 10 and 11 actually delivered and every kernel step has carried since.** The retired criterion omitted **L3** and **L4**, and it was the only place in either document that made L4-at-Step-10 look optional. It was reasonable when drafted only because this table deferred L3 to increment 7 — a deferral row 7 now retires — and L4 was never deferred anywhere: plan §4's Phase B gate names it at every kernel step. Neither level is a separate suite here. `tests/support/kerneldrv.c:cq_kd_case` runs L1, L2, L3 **and** L5 on every case of every kernel, and L4 is each suite's own `l4_goldens` case against `tests/goldens/` |
| 3 | K6–K7 (add, sub) with carry uncompute | L1–L4 green; `x+1` @ i8 count pinned |
| 4 | K9 (compares, all 10 predicates) | L1–L4 green |
| 5 | K10 (mux) + variable shifts; K8 (Cuccaro accumulator); K11 (mul) | L1–L4 green — **COMPLETE 2026-08-16** (Steps 14, 15, 16). K8 is the exception the criterion did not anticipate: it is not a Rule 7 kernel, so its levels are restated by hand and **L5 does not apply to it at all** (K08.md §5 D7) |
| 6 | K12 (div/rem) — **COMPLETE 2026-08-16 (M19, M20)** | L1–L4 green, **plus L5** (the all-classical short-circuit is not optional here — pre-materialisation would otherwise take `8W²+4W−1` qubits for an operation with no quantum input; risk R9) **and D3** (`sdiv`/`srem` by zero never traps and whatever it returns is pinned). L4 at `W ∈ {1,8,16,32,64,128}` — **i128 is a shipped `divrem` width**. All four opcodes green in both configurations; every figure in `K12.md` §3 and §4 reproduced on the first run, **including the four signed columns that had never been executed** |
| 7 | `_unc` and `_controlled` axes; rotations + θ special cases; measurement. (`_inv` is **not** implemented — §15 D14) | ~~L3 green across all kernels~~ **the axis's own claims green — the forward/`_unc` count asymmetry MEASURED, `dst ^= f` at a `dst` that is not zero, and the free** (corrected 2026-09-10, `bd eh9`); classical mode works. **L3 does not first come due here, and in the plan it never did**: it is discharged from increment 2 onward by the shared kernel driver, "on every case of every kernel under each of §9's four regions" (plan §4's Step 21 row, which records this clause as one of two "not satisfiable as written" and corrected *in this document* — this row is where that correction was owed; §15 D15 §1 already says the same, that Rule 7's involution "is what L3 tests on every case of every kernel"). The deferral was reasonable when drafted and is backwards: `cq_shadow_rotate` is the only producer of `unknown`, so nothing is rotation-tainted **until this increment lands M22**, and `tests/support/poolcheck.c:cq_pc_zero_proof_rotation_free` is an *exact* free-time oracle across increments 2–6 precisely because it runs before them |
| 8 | Generated shim over the full integer grid (**1595** — i80 is IN scope, decided 2026-08-14; the ~~1455~~ hedge this row carried is residual drafting, not a live option, and §1 says so twice), of which **992 are wrappers and 603 are `_inv` abort bodies** (§15 D14), **plus the 884 fp abort bodies from the same generator** — 2479 in all; CQ_lang link; Grover | L6, L7 green — **v1 done** |
| 9 | *(stretch)* QRAM — port Bennett's QROM (`src/qrom.jl`, self-cleaning AND tree, 2(L−1) Toffoli) and Shadow (`src/shadow_memory.jl` — **not** `softmem.jl`, which is the Julia-level MUX model that Bennett lowers through its own pipeline and has no gate construction to port; the gate-level store is `emit_shadow_store!` / `emit_shadow_store_guarded!`, measured 2026-09-02) behind `cqrt_qram_*` | load/store round-trip — **v1.2, §15 D24, `bd 9zq`, Step 27 (2026-09-02)**: K13 + K14 with L0–L5 at the kernel layer, the 63 symbols at the ABI, the six integer corpus fixtures end to end; **the ABI's `store`/`store_unc` round trip restores every cell AND releases the tape slot CLEAN through D15's certificate**, which is the clause a value check alone cannot make |

Increments 2–6 are independent after 1 and can be built in any order or in parallel.

---

## 14. Layout, naming, build

```
include/cqops/cqops.h        public API: context, sink, config
src/bit.[ch] reg.[ch] qubits.[ch] emit.[ch] rotate.[ch] controlled.[ch]
src/sink_printf.c sink_count.c sink_qec.c
src/kernels/{bitwise,shift,cast,add,addacc,cmp,mux,mul,divrem}.c
shim/gen_shim.py             M27 grid: reads third_party/cq_lang/opcode_table.yaml
shim/gen_bodies.py           M27 bodies: the only file naming a libcqops/M26 symbol
shim/cq_shim.h               the M26 <-> M28 contract (declarations only)
shim/cq_runtime_impl.c       M26: the cqrt_* surface
shim/generated/*.gen.c       M28: **one file per opcode family**, all 2479 bodies —
                             992 thin wrappers + 603 `_inv` aborts + 884 fp aborts
tests/
```

> **A FOURTH CORRECTION, 2026-08-23 AT STEP 23: `include/cqops/cqops.h` DOES NOT CARRY A
> CONTEXT, AND WILL NOT.** The sketch's first line says *"public API: context, sink,
> config"*; `src/ctx.h` reserved that question by name for Step 23, on the ground that the
> only consumer of a public context would be M26. M26 landed and does not want one, and the
> answer is forced rather than chosen. **M26 IS IN THIS REPOSITORY** — `shim/cq_shim_ctx.c`
> holds the one process-global `cq_ctx` and reaches the internals by include path, exactly as
> `tests/` has since Step 2. **NO CALLER OUTSIDE IT CAN EVER HOLD ONE:** measured, 0 of
> CQ_lang's 2479 `cq_template_*` declarations and 0 of its 173 `cqrt_*` declarations name a
> context, a pointer or a struct — every parameter in both frozen ABIs is a scalar, and
> `opcode_table.yaml` contains the string `ctx` zero times. **THERE IS NO OPAQUE PATH TO
> PUBLISH** (every `cq_ctx` in the tree is by value; no `cq_ctx_create`, no heap allocation,
> no `sizeof(cq_ctx)`), so publishing it means publishing the whole struct — **whose LAYOUT IS
> CONFIGURATION-DEPENDENT**, 128 bytes without `CQOPS_DEBUG_INVARIANTS` and 144 with it, which
> would put a Debug/Release-varying layout into the one surface §14 documents as the place
> that define must not reach. The *sink* and *config* halves of the line are satisfied and
> always were. Full statement in `src/ctx.h` and `shim/cq_shim_ctx.h`.
>
> **THIS SKETCH WAS WRONG IN THREE WAYS AND IS CORRECTED ABOVE (2026-08-22, at Step 22,
> when it had to be built).** It said `gen_shim.py` reads *"CQ_lang's
> `tools/opcode_table.yaml`"* — the un-vendored sibling repo; the source of record is the
> pinned `third_party/cq_lang/opcode_table.yaml` (§1, Rule 1). It sketched **one**
> generated TU, `cq_templates_impl.c`, where plan §3 and risk **R7** require **one
> `.gen.c` per opcode family** (ten of them; four are MIXED wrapper/abort families, which
> is why "wrappers ↔ aborts" is not the file seam). And it accounted only for the integer
> half — *"992 + 603 = 1595"* — giving the **884** fp abort bodies **no file at all**,
> while §1 above requires the same generator to emit every one of them. CLAUDE.md's
> standing rule settles the general case: *the plan's module map supersedes PRD §14's
> layout sketch where they differ.*

C11, CMake, no dependencies beyond libc — mirroring CQ_lang's own build so the two link
without ceremony. Library `libcqops`, prefix `cqops_` for public symbols and `cq_` for
internal ones.

The shim is **generated from CQ_lang's own `opcode_table.yaml`**, not hand-written and
not forked, so the symbol grid cannot drift from the ABI it must satisfy. CI regenerates
and diffs.

---

## 15. Open decisions

| # | Question | Working answer |
|---|---|---|
| ~~D1~~ | ~~Widths above 64~~ | **Resolved.** i128 is in scope; `bits` is heap-allocated to `width` and every kernel is width-generic (I5). Nothing above 128 exists in the ABI |
| D2 | Qubit pool ceiling | Configurable, default unbounded; set it to `qec_n_logical` when the QEC sink is active |
| D3 | `sdiv`/`srem` by zero | Follow Bennett: deterministic-but-unspecified (LLVM poison-equivalent), documented, never a trap |
| D4 | Free-list discipline | LIFO. Lowest-index-first (Bennett's `WireAllocator`) would give tighter peak counts but noisier trace diffs; revisit at L4 |
| D5 | Handle reuse | Never — monotonic, matching CQ_lang's existing `h<N>` trace convention |
| D6 | Shadow-driven demotion | A qubit whose shadow is *known* could be X'd to \|0⟩, freed, and folded back to a constant bit. Sound, and a real saving. Deliberately **not** in v1 — it makes the qubit count depend on shadow precision, which would make L4 goldens fragile. But see §10: it is also what would make forward and `_unc` gate counts agree, so revisit if that asymmetry becomes painful |
| **D7a** | **Result aliases a source** — `_unc(out, out, b)` | **Measured 2026-08-14 at Step 7 over all 239 goldens: 0 occurrences in 25,147 `_unc` calls.** It also breaks §4's `dst ^= f(a,b)` outright. **Hard error, in BOTH configurations** — R2's whole value is firing during the L6 fixture run at Step 24, which Rule 17 pins under Release |
| **D8** | **Shift amount out of range** — `x << k` with `k ≥ W` | **Resolved 2026-08-15 at Step 11 (bd `ckd.16`). MASK, THEN SATURATE — one formula for both paths:** `dst ^= sat_shift(a, k mod 2^⌈log₂W⌉)`, where `sat_shift` zero-fills (`shl`/`lshr`) or sign-fills (`ashr`) and so yields 0 / all-sign once the effective amount reaches `W`. **Deterministic, documented, never traps** — D3's posture, and for D3's reason: `k` is decoded from `W` classical bits, so its domain is `[0, 2^W)` and ordinary C reaches M11 with `k = 40` at i32. See the note below for why this is the *cheap* side |
| **D9** | **K12's scratch scheme, and the three kernel-shape choices that ride with it** | **Resolved 2026-08-16 at Step 0 (bd `ckd.13`, and the M19/M20 half of bd `4tt`) and SHIPPED as M19/M20 the same day. Five parts, all measured — see the note below.** **(a) FLAT** scratch: one sandwich for the whole kernel, fresh per-iteration scratch, no per-iteration uncompute. **(b)** `fits` is read straight off the comparator carry-out `ucar[t][W]`, not materialised as `not1(ult(…))` — which is what **M16 already ships**. **(c)** the quotient bit is one `CX(fits → q[i])`, not Bennett's `or`+`mux`. **(d)** the initial remainder's upper `W−1` bits are a **pre-materialised scratch register**, so every iteration costs the same. **(e)** M14 and M16 **export their compute halves** as indexed step blocks and M19 composes them with M17's — K12 re-transcribes nothing (plan §0.4). Pinned: `udiv` `34W²+5W` gates over `8W²+4W−1` qubits — **2216 / 543 at i8, 557 696 / 131 583 at i128** |
| **D10** | **What §7's angle tolerance is relative TO** | **Resolved 2026-08-17 at Step 18 (bd `dl7`) and SHIPPED as M21 the same day. The window is `tol · π` — ABSOLUTE, a fraction of the lattice modulus, not of θ — plus one refusal, `\|θ\| · 1.6e-16 ≤ tol·π`, and a cap `0 ≤ tol ≤ 1e-3`. §7 said only "1e-12 relative" and left the scale unstated; the natural reading, relative to `\|θ\|`, was built first and is a MISCOMPILE. See the note below** |
| **D7b** | **Two sources alias each other** — `mul(h, h)` | **Measured the same way: 599 occurrences, of which 10 are on v1's integer surface.** CQ_lang ships a fixture named for it — `tests/e2e/slice_select_rail_alias_cond.expected.log:4` is `cq_template_icmp_slt_i32(h0, h0) -> h1`, and `:31` is `cq_template_mul_i32(h10, h10) -> h11`; also `spec_newcand_qsq_caller:13,16`, `spec_replan_qpow_caller:13,18`, `slice_i128_mulhi:4`. **This is LEGAL and must NOT abort.** The remedy is now required rather than contingent: a defensive `cqrt_copy` of one aliased source at the **M26 handle boundary** (Step 23), *before* Step 24 runs — one place, not twelve. M07 exposes the predicate; M26 acts on it |
| **D11** | **What §7's zero-gate and global-phase rows do inside a §9 controlled region** | **Resolved 2026-08-17 at Step 19 (bd `pf4`) and BUILT at Step 20 in M06 — the refusal is one function, `cq_ctrl_refuse_fold_row`, and all five folding cells call it. A CONSTANT control folds the region away (§9 row 0), so §7 applies verbatim and Rule 15's zero-cost claim is untouched. A QUANTUM control makes every folding row wrong — the four zero-gate cells by exactly `Rz(α)` on the control wire, and the half-turn's qubit cell, which emits but realises the row only up to a phase — per-bit, with α given below — and `v1 REFUSES rather than emitting it`: M06 hard-errors at Step 20 when a §7 fold row is reached under a quantum control. The arithmetic is recorded here so Step 20 implements rather than re-derives. See the note below** |
| **D12** | **Which §7 rows poison the shadow** | **Resolved 2026-08-17 at Step 19 and SHIPPED as M22 the same day. ONLY `Ry` at an angle off the π-lattice poisons. A diagonal gate — every `Rz`, and the `Z` of the θ ≡ π row — cannot move a computational-basis value, so a determinate entry stays determinate and `cq_shadow_known_zero` remains EXACT rather than becoming conservative. `cq_shadow_rotate` is unchanged; what this decides is which rows call it. See the note below** |
| **D13** | **Whether `cq_materialise`'s `X` is promoted inside a §9 region** (`bd skh`, deferred at Step 6) | **Resolved 2026-08-20 at Step 20: NO — materialisation is UNPROMOTED, and it is forced rather than chosen. M06 hooks `cq_emit_x/cx/ccx` and nothing else, so `cq_materialise`'s deliberate bypass to the sink is already the correct behaviour and Step 20 changed no code — only the comment, from a deferral into the decision. D11's constant column is stated to be contingent on this answer. See the note below** |
| **D14** | **What the `_inv` axis is, and what a v1 body does** | **Resolved 2026-08-21 at Step 21. `_inv` MEANS TWO DIFFERENT THINGS ON TWO FAMILIES and this project's documents used both senses without flagging it: on the controlled ROTATIONS it is θ-negation (16 `cqrt_*_controlled_inv` symbols, PRD §2.1's family, Step 23's to implement); on the DATA TEMPLATES it is a fresh-minting inverse OPERATION — 915 `cq_template_*_inv` symbols, 603 purely-integer. **NEITHER IS EMITTED BY ANYTHING at the pinned revision** — 0 `_inv` lines across all 239 goldens — so liveness does not distinguish them and is not the ground. **The ground is SPECIFIABILITY.** `_inv` is `f⁻¹`, not `f`: `ir-pass/test/lowering_invert_add_i32.ll:25` pins a bracketed `add %a, 2` as lowering to `cq_template_add_i32_hl_inv(%a, 2)` — i.e. SUBTRACT. And `f⁻¹` **does not exist** for `and`, `or`, `udiv`, `trunc` or any `icmp`: they are not injective. No uniform definition of the data family is possible in **any** version. **v1 gives it a loud abort naming the symbol**; the obligation lands on Step 22, not Step 23. See the note below** |
| **D15** | **What evidence `cqrt_free` reads for a CQ_lang rail, and what happens when there is none** (`bd c1a` / `ckd.17b`, open since 2026-08-15) | **Resolved 2026-08-22. THE `\|0⟩` PROOF OBLIGATION IS CQ_LANG'S, STATED IN ITS OWN WORDS, AND IT CANNOT BE DISCHARGED AT OUR LAYER AT ALL.** `CQ_lang/ROADMAP.md:381-386` makes it a non-negotiable principle: `cqrt_free` is emitted *"only on a proven-`\|0⟩` rail"*, and *"a rail not provably `\|0⟩` is **left allocated** … never freed"*; `CQ_lang/tests/e2e/run_slice.sh:10-13` states the limit from the other side — *"no trace label can say whether a rail was ACTUALLY `\|0⟩` at the free, **at any ABI**"*. So v1 does not duplicate that proof. What it holds instead is an **OBSERVED UNDO CERTIFICATE** over the call stream — measured to discharge the overwhelming majority of the corpus's frees, ~99.75%, where the shadow discharges **none** (every figure is pinned and dated in the note's §0, and the corpus is not frozen) — and a rule at the free whose EPISTEMIC state is **three-valued** while its ACT is **two-valued**: proven-clean releases, and **proven-dirty and unproven alike are STRANDED** — never released, never on the free list, counted. (That clause read *"proven-dirty is a hard error in both configurations"* until 2026-08-22 and was the pre-correction half of this very cell, whose own tail already said otherwise — the recorded pattern of a correction propagating to the paragraph being edited and stopping there.) **The layering licenses NOT ABORTING; it does not license RECYCLING**, so `CQOPS_FREE_TRUST`'s prohibition is **narrowed and KEPT** rather than lifted (the first draft released the residue and an adversarial review refuted it the same day). `CQOPS_FREE_ABORT` survives as a development flag. `ckd.18`'s `ry`-cancelled rails are the one carve-out — the certificate makes them **provably dirty** rather than unprovable, so they never reach the free list; **that row is STRANDED rather than aborted — the last clause of this decision, confirmed 2026-08-22 at the start of Step 23, so the whole of D15 now stands**. Note what that leaves: the free path has **two** dispositions (release, or never-release-and-count) while the *epistemic* state stays three-valued and is reported separately, and Rule 6's hard error survives verbatim for the row it was written about — a RELEASE of a non-`\|0⟩` index — which is the row that can no longer arise. See the note below** |
| **D16** | **Which of the 173 `cqrt_*` symbols get a body at all** (`bd vxk`, `bd r3y`; PRD §1 and `docs/cqrt_census.txt` gave OPPOSITE answers for `cqrt_h`) | **Resolved 2026-08-23 at Step 23 landing 1 step 4. THE DISCRIMINATOR IS CAPABILITY, NOT LIVENESS, and "is it minted by the pass?" — the discriminator both documents implied — is measurably the wrong one.** Define every symbol libcqops COULD serve: implemented where v1 is in scope, a loud `cq_shim_unsupported` naming the symbol where v1 defers. Leave UNDEFINED only what libcqops could not serve at any point in v1. **That is exactly two symbols — `cqrt_h` and `cqrt_h_controlled`** — because Rule 4 forbids `H` on the classical path and §8's vtable is frozen at six entries with no `h` slot. The 11 never-minted symbols are not one population: `cqrt_x_controlled` and `cqrt_cnot_controlled` are IMPLEMENTED (measured — the `int32_t` operand of `cqrt_x`/`cqrt_cnot`/`cqrt_toffoli` is a rail HANDLE and not a raw wire, so the two are literally a `CX` and a `CCX`), and the five integer `cqrt_ry_<W>_controlled_inv` are IMPLEMENTED as D14 requires — with the honest qualifier that under a QUANTUM control three of §7's Ry rows hit D11's refusal, which `cqrt_rz_<W>_controlled[_inv]` already carries and PRD §2.1 already ships. The **34 fp-width core symbols** (`bd r3y`) join the **63 `qram`**, ~~**11 `tape`**~~ *(in scope since D23, 2026-09-02)* and `cqrt_alloc_handle` in `shim/cq_runtime_v2.c` as loud aborts, and the ground is **LINKAGE rather than scope**: `libcq_runtime.a` is one object holding all 173, so leaving a REFERENCED symbol undefined produces a duplicate-symbol wall naming symbols we implement correctly — or, measured and worse, a SILENT successful link in which CQ_lang's trace-only stub serves every call. `cqrt_h`'s omission is free only because nothing references it, which is PRD §1's own antecedent and is true of `cqrt_h` alone. See the note below |
| **D17** | **How `cqrt_addc_<W>` adds in place when libcqops has no in-place adder that can serve it** (`bd dzj`, P0, filed 2026-08-22) | **Resolved 2026-08-22 by measurement, BUILT 2026-08-23 at Step 23 landing 1 step 4, and recorded here because a bead is not a design-of-record — `addc` appears 8 times across this file and the plan and not one of those lines stated the decision.** FOLD when the rail is all-classical; otherwise materialise the immediate into a fresh W-bit rail, materialise the rail's remaining classical lanes, take ONE ancilla, and run **M15's Cuccaro accumulator in place** — `acc = h`, `b = immediate`, `x = ancilla` — then drive the immediate rail back to `\|0⟩` with one `cq_emit_x` per set bit and free it. Two rows short-circuit first and both are PORTS rather than peepholes: `imm == 0` emits nothing (upstream's own identity peephole) and `W == 1` is `h ^= imm`, one gate, because addition mod 2 has no carry. **THE CLASSICAL FOLD IS MANDATORY, NOT AN OPTIMISATION**: D15 measured 40 of the corpus's 45 I4 frees on rails written only by `cqrt_alloc` + `cqrt_addc`, and making this path materialise collapses that to 5. **AND THE QUANTUM PATH CANNOT BE DEFERRED**, which was the open question: most corpus `cqrt_addc_i32` calls land on a rail that already owns qubits, through a chain rooted at a general `cqrt_ry`. Rule 1 is satisfied on both halves and nothing is re-derived — Bennett ships NO add-constant construction at all, and what upstream does for `x + const` is MATERIALISE it (`operand.jl` allocates and emits one NOT per set bit) and then run the general adder. See the note below for the four traps, the transient disposition and what it costs |
| **D18** | **What Step 24's ORACLE is — NORTH_STAR says the fixtures "link against libcqops and run", the plan and §11 say "diff emitted traces / Traces match", and those are not the same criterion** (`bd 590`, P0, filed at Step 9) | **Resolved 2026-08-27. STEP 24 IS NORTH_STAR CONDITION 1 VERBATIM: the fixtures LINK, RUN, and do not abort. "Traces match" is RETIRED from the plan and from §11.** The retired criterion named an oracle that stops existing the moment condition 1 is satisfied: CQ_lang's `runtime/cq_runtime.c` calls itself a *"trace-only runtime stub"*, its e2e goldens are CQ_lang's regression oracle for CQ_lang's **own IR pass** captured against that placeholder, and once the real backend is linked nothing in the process emits those bytes. **AND THE BEAD'S OWN FALLBACK ORACLE IS FALSE, measured 2026-08-27 before it was written down.** `bd 590` proposed that correctness be carried by `cqrt_measure_*` return values, *"which ARE part of the ABI and ARE checkable against the goldens' measure lines"*. They are not: every measure body in the stub is `printf("… -> 0\n"); return false;` — a LITERAL — so all **266** measure lines across **244** goldens read `-> 0` whatever the circuit computes. Diffing against them would pass vacuously where the true value is 0 and fail where it is not, and the failure would be **us being right and the golden being a placeholder artefact**. **A SECOND, INDEPENDENT REASON THE GOLDENS CANNOT BE DIFFED IN ANY COLUMN: our handle numbering already diverges, by construction and on purpose.** `cqrt_addc` on a rail that owns qubits mints two transients CQ_lang never sees, so the D5 counter advances by 2 and the next `cqrt_alloc_*` returns `h4` where the stub returned `h2` — measured in Release and recorded at `shim/cq_runtime_rail.c`; D7b's defensive copy (`bd 493`) does the same on an aliased call. §2's own warning about a second counter diverging handle numbering has already come true from a different direction, and it is not a defect: those rails are ours, and the ABI does not name them. **SO WHAT CARRIES CORRECTNESS AT STEP 24 IS L1–L5 AND THE LINK, and that is the honest statement rather than a weakened one.** L1 (value against the C operator), L2/L3 (the pool as a SET), L4 (pinned counts) and L5 (the classical short-circuit) all run today over the kernel surface, in both configurations. What Step 24 adds is the only thing they cannot: that the **frozen ABI's 173 + 2479 symbols resolve against a real backend and that a real CQ_lang program drives them end to end without aborting** — which is exactly what NORTH_STAR condition 1 asks and is a claim no unit test can make. The **residue report** (D15 §3) is read alongside it as an observation, never as a gate: a fixture that strands is running correctly under a certificate that could not clear every rail. **THE ARCHITECTURAL POINT THIS TURNS ON, and it governs more than this decision: libcqops is a standalone linkable C library and CQ_lang is ONE CALLER of it. Our coupling is the frozen `cqrt_*` ABI and NOTHING ELSE — not its trace format, not its harness, not its golden corpus.** No libcqops module may be designed around what CQ_lang's harness happens to diff. Two riders. **(i) M23's default stream stays STDOUT** — ordinary library behaviour, not a concession, and `cq_sink_printf(FILE *)` redirects it in one line; Step 9's format decision (`x`/`cx`/`ccx`, `q<N>`, `%a`) is untouched, having rested on a sink structurally never seeing a handle. **(ii) Candidate (b) — pinning OUR gate stream against OUR OWN goldens — was weighed and NOT taken.** It is a real regression check and it is what "diff emitted traces" most plausibly meant, but trace goldens churn on D4 free-list and D6 non-demotion changes, which is **risk R5** and is exactly why kernel goldens pin COUNTS and not traces. It is filed for v2 rather than refused: what makes it affordable later is a stability claim about D4/D6 that v1 does not have |
| **D19** | **What the qec sink's `ry` and `rz` entries do, given that `qec_rz` refuses a float and `qec_ry` does not exist** (Step 26 / M25; filed 2026-08-28 after reading the API rather than the plan) | **Resolved 2026-08-28. NEITHER IS A STUB.** §7's *"the QEC sink's `ry`/`rz` entries are stubs that record the call"* is retired: it was inferred from two true API facts, and neither fact implies it. **(1) `rz` — the double becomes an exact rational by CONTINUED FRACTIONS on θ/π, and the DENOMINATOR CAP is D10's tolerance wearing a different hat.** `qec_rz(ctx, q, long p, long q_denom, int precision)` means θ = π·p/q_denom exactly, ε = 2^−precision; p and q_denom reach `gs_rz_synthesize` as integers at `4·precision + 96` bits of working precision and are never divided, so the pair is not a spelling of a double — `(1, 4)` is exactly π/4, which no double is. Best-rational approximation supplies the conversion, and the cap has a failure mode at BOTH ends, MEASURED 2026-08-28: **too small and the corpus's own `3.14` becomes `1/1`, a silent 1.593e-03 rad rewrite to π** — D10's named miscompile arriving through a second door — **and too large and `fl(π/4)` stops being `1/4` and becomes `365555973729107575/1462223894916430357`, the same double and the same round-trip.** **CORRECTION, MEASURED 2026-08-28 AT STEP 26 THROUGH THE LIBRARY ITSELF rather than inferred from the paper: the upper end's cost claim — *"a generic rotation costing ~3·log₂(1/ε) T gates instead of ONE"* — IS FALSE FOR π/4.** On `config_ccx.json` at precision 20, `qec_rz(q, 1, 4, 20)` costs **1,200** physical `T`, and the 2^62 convergent costs **exactly the same 1,200**. This driver's Ross-Selinger Lemma-7.2 degenerate branch catches multiples of **π/2** and not of π/4: `(1,1)`, `(1,2)`, `(2,1)` and `(0,·)` are T-FREE at every precision, and those — not π/4 — are precisely §7's folding rows, which is why the cheap angles stay cheap regardless. **The BAND is unaffected**, because it was established by bisection on the CONVERSION rather than on the emitted string, and the **lower end remains a real miscompile**; what changes is only which angle illustrates the upper end. The general shape survives verbatim — the two conversions round-trip to the identical double, so no value check can separate them — and `test_sink_qec.c` therefore asserts the T-free rows it MEASURED and not the one it was told. Both survive any value check: the round-trip to the input double is bit-exact on both sides, so **only `qec_count(QEC_GATE_T)` can tell them apart** — the Prime Directive's "a right answer is not a right circuit", relocated into the angle conversion. The band is wide and was measured, not guessed: π/4, π/8 and π snap to `1/4`, `1/8`, `1/1` for every cap up to **2^53.5** (3π/4 to 2^51.9), while arbitrary doubles round-trip exactly from cap **2^27–2^30** up, so **any cap in [2^32, 2^48]** buys both with two decades of margin. The snapping is a mechanism rather than luck — a double near a nice rational has a huge partial quotient, so the convergent holds across an enormous cap range. Two riders: θ/π must be formed against π at MORE than double precision or the division by the rounded π reinjects the |θ|·1.2e-16 drift `src/angle.h` already fights (`tests/test_angle.c`'s double-double `PI_HI + PI_LO` is the technique, and it moves library-side); and §7's folding rows need no search at all, since M21 already recovers the integer `k` and those are `(k, 1)`. `qec_rz`'s own guard, `q_denom > LONG_MAX/2`, is ~2^62 and protects nothing. **(2) `ry` — CONSTRUCTED, not stubbed: `Ry(θ) = S·H·Rz(θ)·H·S†`, and the EMISSION ORDER IS THE REVERSE.** `Y = S X S†` gives `Ry(θ) = S Rx(θ) S†` and `Rx(θ) = H Rz(θ) H`; verified numerically exact (max deviation 2.220e-16 at θ ∈ {0.1, π/4, 3.14, π, −2.5, 1e-9}) and **exactly, not up to a global phase** — unlike §7's half-turn row. `qec_s`, `qec_h` and `qec_sdg` all exist. **The circuit is `sdg; h; rz; h; s`**: a matrix product applies its leftmost factor LAST, and emitting `s` first is wrong by 6.858e-01 — this is §7's own *"a matrix product and a circuit read in opposite orders"* trap, so M25 must pin the order with an executed check and not with the identity as written. **The conjugation is T-FREE**: `QEC_GATE_T` is produced only by `qec_t`, reached only from the gridsynth string walk (`qec.c:1749`), so `Ry(θ)` costs exactly `Rz(θ)`'s T-count plus four Clifford gadgets. **This unblocks §12**, which had no other route — `cqrt_h` is an over-declaration, so Grover-from-rotations is forced, and a stubbed `ry` made the qec sink structurally unable to run v1's acceptance gate. **§7's half-turn spelling is UNCHANGED**: `x; rz(q, π)` is two Clifford ops against five, so the preference survives even though its stated reason (*"there is no `qec_ry` at all"*) is now true only of the API and not of what the sink can build. **This is a sink-level single-qubit Clifford conjugation, below the kernel layer, so it is outside Rule 1's scope** — Rule 1 governs reversible constructions for the integer surface, ported from Bennett.jl — but it is a DECISION and belongs here rather than in a comment |
| **D20** | **What the qec sink is reached FOR, and what bounds it** (Step 26 / M25; filed 2026-08-28) | **Resolved 2026-08-28 by reading the QEC library's configs and MEASURING its own `trace` driver.** **(1) `n_logical` IS A CONFIG CONSTANT AND RAISING IT RAISES THE CODE DISTANCE.** `qec_n_logical(ctx)` is documented as *"the valid range for every `q` argument"* and comes from the loaded JSON; the shipped example configs carry n_logical ∈ {1, 3, 4, 6, 7}, and one states outright that *"at n_logical=5 the derived d rises to 5 and a d=5 fabric at this F does not fit"* — the error budget splits across the logical qubits. So D2's *"set the pool ceiling to `qec_n_logical`"* is a HARD ceiling, not a formality: K12's `divrem` at W = 8 alone takes **543** scratch qubits before operands and M18's `mul` at i128 takes 16,640 (`bd fxz`), so exceeding it must fail loud rather than grow. **THAT 543 IS A CORRECTION, MADE 2026-09-10 (`bd j75`); this row read `W²+2W−1 = 79` until then, and `W²+2W−1` is K12's REMAINDER TAPE — one component of the region, not the region.** The region is **D9**(a)'s FLAT `8W²+4W−1`, and `cq_divrem_region(8, 1)` returns 543, MEASURED rather than derived (143 / 219 / 311 / 419 / **543** at W = 4..8, and **131,583** at i128). **The error ran in the direction that WEAKENED this row's own argument** — 543 makes the ceiling seven times harder to clear than 79 did — which is why it is corrected rather than left standing; nothing downstream had read it, **D25**'s own figures having been measured independently. **AND THE HONEST BOUND IS HARDER STILL, BECAUSE IT IS THE PEAK AND NOT THE REGION (D25(a))**: `dst`'s lanes are materialised by the COPYOUT, which runs while the whole region is still live, so `udiv` at W = 8 peaks at `8W²+5W−1` = **551** — pinned alongside the region in `tests/test_kernel_divrem.c`, which is where both figures are read from rather than written down. **(2) WHAT IS VISIBLE THROUGH THE SINK IS BOUNDED BY TOFFOLI COUNT, AND THE BOUND IS SHARP.** `scripts/draw_circuit.py` parses the `execute_gate` trace, which is PHYSICAL — post-distillation, post-lattice-surgery. MEASURED with the library's own `trace` driver, one LOGICAL gate: at **d = 3** (`config_ccx.json`) a `CX` is **1,830** physical gates and a `CCX` is **562,564**; at **d = 11** (`config.json`) a `CX` is **112,134** and a `CCX` is **34,417,428**. The gap is magic-state distillation — §7's `Ry`/`Rz` land in the same place through gridsynth. **So a Toffoli-FREE region at small width is genuinely inspectable** (`cq_kernel_xor` at W = 4 is 4 CX ≈ 7,300 physical gates at d = 3, well inside the drawer's `--qubits` / `--max-gates` / `--pixel-art` handling) **and anything carrying Toffolis is not** — K6's `add` at W = 4 is already ~12 Toffolis ≈ 6.7M gates. That is a property of fault tolerance, not a defect in either library, and it is the honest scope for NORTH_STAR condition 5. **THE `4 CX ≈ 7,300` IN THAT PARENTHETICAL IS WRONG, AND IT IS KEPT VERBATIM ABOVE BECAUSE IT IS WHAT THIS ROW SAID ON 2026-08-28 — AMENDED 2026-09-10 (`bd y1i`), not rewritten.** K1 is `dst ^= a ^ b`, one CX per SOURCE per lane, so the count is `2W` and not `W`: `tests/goldens/bitwise.counts` pins `xor forward 4 → NOT 0 / CNOT 8 / Toffoli 0` — and `unc` the same — at the ALL-QUANTUM mask, re-read 2026-09-10. At this row's own **1,830** per logical `CX` at d = 3 that is `8 × 1,830 = 14,640` ≈ **14,600** physical gates, twice the figure printed. **NEITHER THE ARITHMETIC NOR THE INSTRUMENT WAS AT FAULT** — `4 × 1,830 = 7,320` is self-consistent, and the per-`CX` cost is untouched by this note (it stays this row's 2026-08-28 measurement with the QEC repo's own `trace` driver on `examples/config_ccx.json`; that library is unpinned here, so it was NOT re-measured and the recomputation reuses the figure as recorded). **The OPERAND COUNT was a prose figure typed from a remembered formula, which is the one input nothing in this row measured.** **THE CONCLUSION IS UNTOUCHED, AND THE DIRECTION IS WHY:** the error understates a cost in a clause arguing that the cost is SMALL ENOUGH TO DRAW, so 14,640 is still far inside the drawer's `--qubits` / `--max-gates` handling and (2) stands as printed — the OPPOSITE direction to `bd j75`'s in (1) above, which understated a bound that row needed to be large. **THE NEIGHBOURING FIGURE WAS RE-CHECKED IN THE SAME PASS AND IS RIGHT:** `tests/goldens/add.counts` pins `add forward 4 → NOT 0 / CNOT 28 / Toffoli 12`, so K6's *"~12 Toffolis"* is exact and `12 × 562,564 = 6,750,768` ≈ 6.7M stands — **the row is not uniformly stale, which is worth knowing before rewriting one.** **AND WHAT FOUND IT WAS NOT A CAREFUL RE-READING:** `docs/labreport/data/toffoli.dat` is pivoted straight out of the goldens by `tools/labreport/gen_data.py`, so plotting the catalogue put the pinned count beside the prose one. That is the argument for GENERATING figure data rather than typing it, and it is the same instrument `bd j75` motivated one row up. **THE SINK IS THE ONLY SANCTIONED ROUTE TO A DRAWING AND THIS REPO GROWS NO SECOND ONE.** Rule 13 is the reason: emission is a stream, a gate is gone from our side once emitted, and what a consumer does with it is the consumer's business — the drawer is the QEC repo's, reached by routing the same stream into `qec_*` under one flag. A libcqops-side drawer was proposed while resolving this and REJECTED: it would fork a second visualisation path, duplicate a shipped capability, and put a structure where §1 constraint 3 says there is none. **A rider that evaporated with it, recorded so it is not rediscovered as an objection:** the drawer's parser reads `parts[1]` and `parts[2]` only, which would silently drop a three-operand `ccx` target — but `qec_gate_kind` is H/S/X/Z/Y/CX/MZ/MX/T, every one at most two qubits, so `execute_gate` never emits a three-operand line and the parser is exactly right for its own input. The catch existed only for the rejected design |
| **D21** | **Whether libcqops emits the QEC repo's INSTRUCTION-level trace annotations, and where that code lives** (Step 26 / M25 + M26; filed 2026-08-28) | **Resolved 2026-08-28: (a) LAZY ALLOCATION WINS and the header is TWO-PASS; (b) ONE INDEX PER REGISTER WINS and the qec sink DISABLES RECYCLING; (c) is not a loss. Recorded in full because two earlier answers in this file were WRONG and the contract is normative. READ THE DIVISION OF LABOUR FIRST, IT IS THE WHOLE SHAPE: libcqops emits annotations for OPERATIONS ONLY and prints NOTHING for a gate. `#REGISTER` lines and `# STAGE: op begin`/`op end` brackets are ours; every gate line, every `#PATCH`, and every `# STAGE: logical CX begin/end`, merge, split and syndrome round is the LIBRARY's, written by its own `execute_gate` when we call `qec_cx`. §7 says so as a prohibition — *never print gate lines* — and the consequence is that the annotation is NOT a sink and MUST NOT be one: the six vtable entries call `qec_*` and write no text at all, while M26 writes two line kinds around them. **THE HAZARD THIS FORBIDS IS CONCRETE AND CHEAP TO HIT: pointing M23's printf sink at the same `FILE*` as the qec trace corrupts EVERY trace.** Our `cx(q0, q1)` is neither a conformant gate line (the library owns those, spelled `CX 0 17`) nor a conformant annotation, and §9's table makes an unrecognised line inside an opted-in trace a **fatal parse error** — the pipeline aborts, no JSON, no HTML, no degraded render. M23 and the qec path are disjoint consumers of one stream and must never share a stream.** `qec/docs/HOST_LANGUAGE_HANDOFF.md` is a **273-line NORMATIVE contract, v4, approved 2026-08-27**, whose stated audience is *"the team building the higher-level quantum programming language that compiles data-structure operations down to `qec_*` calls"* — **that is us**, and it is the complete specification for the print statements our runtime must emit so the viewer can render at the algorithm level. **CORRECTION 1: there is no injector API and none is needed.** §1 — the host writes its annotation lines with ordinary `fprintf` to **the same `FILE*` it passed to `qec_set_trace`**, whole lines, only BETWEEN `qec_*` calls, at `QEC_TRACE_FULL`. So this is our obligation, not an upstream feature request. **CORRECTION 2: it is NOT the sink's job, and that is structural.** A `cq_sink` is handed a raw `uint32_t` and structurally never sees a handle, a width or an opcode name (§8) — but `#REGISTER name=a type=int qubits=0,1,2,3` and `# STAGE: op begin (name=Add, in=a|b, out=b)` need exactly those three. **They live at the M26 shim boundary**, which already knows the opcode, the handles and the widths. M25 stays the six vtable entries; the annotation is a separate concern at a different layer. **WHAT THE CONTRACT DEMANDS**: `#REGISTER` lines, ALL of them before the first bracket, `qubits=` a comma-separated list of the same `uint32_t` we pass to `qec_*`; one flat `# STAGE: op begin (name=…, in=…|…, out=…)` / bare `# STAGE: op end` pair around the ENTIRE compiled expansion of each language-level operation; tokens matching `[A-Za-z0-9_.$\[\]-]+`; and full coverage — **a gate line outside any bracket is a FATAL parse error**, no degraded render. **THREE GENUINE COLLISIONS WITH THIS REPO'S DESIGN, and they are why this is a DECISION and not a formatting pass. (a) THE CONTRACT'S REGISTER MODEL IS STATIC AND OURS IS LAZY.** §3: *"Registers are static: declare once, never re-declare, free, re-bind, or slice"*, and every `#REGISTER` must precede the first bracket. But Rule 5 / NORTH_STAR §2 allocate **lazily, per bit, never at declaration**, and **I4** gives an all-constant rail **zero** qubits — so at the moment the register map must be printed we do not know a rail's indices, and for a rail that stays classical there never are any. Pre-materialising to fix it would destroy L5. **(b) D4 RECYCLES INDICES AND THE CONTRACT FORBIDS IT.** §3: *"Every index belongs to at most one register"*; but after `cqrt_free` a LIFO index returns to the pool and `cq_materialise` hands it to an unrelated rail, so over one program an index legitimately belongs to several. Declaring both is an overlapping `#REGISTER` — a fatal parse error. **(c) OPERATIONS DO NOT NEST (v3 policy), so the SANDWICH CANNOT BE BRACKETED — and under the division above that is CORRECT rather than a loss.** The forward / copyout / reverse halves are circuit structure, and circuit structure is what the library narrates: they are already visible as the sequence of `# STAGE: logical CX begin/end` fences the viewer nests inside our one block. What we describe is the OPERATION — `Add`, `Measure` — not how we compiled it. Brackets therefore go at the OUTERMOST shim entry point and nowhere else, which is what the contract wants and what our own layering wants; the temptation to bracket the sandwich halves is the same category error as printing gate lines. **ONE THING THAT FITS WELL:** §6 rule 3 — *"workspace qubits stay unregistered"* — is exactly our scratch, left out of every `qubits=` list and shown as extra lanes inside the op, so the scratch region needs no register and (a) bites only on rails. **COVERAGE IS WIDER THAN `cq_template_*`:** every logical `qec_*` including `qec_mz`/`qec_mx`/`qec_idle` must be inside a bracket, so `cq_runtime_gate.c`'s 30 direct-gate `cqrt_*`, `cqrt_free`'s cleanup, `cqrt_addc`'s transients and D7b's defensive copy all need one too. A reference producer (`examples/circuit_demo_registers.c`) and the viewer pipeline (`scripts/verifyview/`) both ship **RESOLUTION OF (a): the conflict was never lazy-vs-eager, and the contract's static header is unsatisfiable by ANY runtime with mid-program allocation.** §3 requires EVERY `#REGISTER` before the FIRST `op begin`, and CQ_lang emits `cqrt_alloc` throughout a program — so even eager allocation at `cqrt_alloc` cannot put a rail allocated after the first operation ahead of it. The header is therefore ASSEMBLED AT END OF PROGRAM and written ahead of the buffered trace, regardless of allocation policy — and once that is so, lazy allocation costs nothing extra. Three rules follow. The `#REGISTER` line lists the rail's FINAL index set: D6 never demotes, so the set only grows and is well-defined at the end, and a bit not yet materialised during an early op simply *"contributes no lane"* there (§6 rule 4). An all-constant rail (I4) gets NO line: it owns zero qubits, so it is not a quantum register, and the viewer showing zero rails for it IS L5 made visible. `cqrt_addc`'s transients and D7b's copy stay UNREGISTERED — workspace (§6 rule 3), shown as extra lanes inside the op — so only handles CQ_lang received back get a line. Mechanically the shim owns the trace `FILE*` (it must anyway, for `qec_set_trace`), writes to a NAMED `.partial` file rather than `tmpfile()` so an `abort()` mid-program still leaves the partial trace on disk (the same reason M23 flushes per line), and at teardown writes the header then appends it. Teardown is `atexit`: the frozen ABI has no shutdown symbol (`docs/cqrt_census.txt`) and the shim registers nothing today. **RESOLUTION OF (b): the contract wins, and the consequence is that INDICES ARE NEVER REUSED UNDER THE QEC SINK** — monotonic, exactly like D5's handles, which is exactly what the contract's static model presupposes. The alternatives do not survive: recycling SCRATCH only is worse, not better — an index used as workspace in op 1 and materialised into rail `h5` in op 3 renders as `h5`'s lane doing work in an op before `h5` existed, a confident wrong picture; partitioning rail and scratch index spaces is the v2 refinement. Two things make it cheap. The pool already has the mechanism: `cq_qubits_strand` keeps the index in `live`, so `minted == live + free` holds and this is NOT the third bucket D15 §3 rejected (`src/qubits.h:23-25`); retiring a CLEAN rail needs only its own counter so the D15 residue report never confuses *"retired for the trace contract"* with *"could not prove `|0⟩`"*. And it is a DISPLAY-MODEL constraint, not a physical one — qec's patch holds `|0⟩` after our free and would happily be reused — which is why it is a sink-mode toggle set at install time, alongside D2's ceiling, and NOT a change to D4. Cost: `qec_n_logical` now bounds TOTAL MINTED rather than peak, which under D20's Toffoli-free-small-width scope is not what limits anything. **WHERE IT LANDS:** Step 26 (`bd k26`) owns the six vtable entries, D19's `ry`/`rz`, the trace `FILE*` and its `atexit` teardown, and the install hook that now sets TWO pool modes — ceiling and no-recycle; `bd 76r` owns the header assembly and the `op begin`/`op end` brackets at the shim entry points, which is the bulk and is the deliverable — without it the viewer shows only the v2 logical-qubit skeleton. **BOTH HALVES ARE NOW BUILT and `bd 76r` is discharged (2026-08-28), in `shim/cq_shim_trace.[ch]` plus one bracket at each of M26's `cqrt_*` and `cq_shim_*` entry points.** Four things the build settled that this decision did not, recorded so they are not re-derived. (i) **THE HEADER IS PREPENDED BY A COPY, NOT A RENAME, AND THE DEPENDENCY POINTS DOWNWARD**: the register map is M26's and the file is M25's, so the composition happens in `cq_sink_qec_teardown` and the CONTENT arrives through a callback M26 installs (`cq_sink_qec_set_header`) — a Layer-4 module calling up into Layer 5 would be the alternative. (ii) **THE SNAPSHOT REPLACES RATHER THAN UNIONS.** "The set only grows" is the obvious reading of (a) and it is right for every row but one: `cqrt_cswap` with a CLASSICAL ONE flag exchanges two rails' BIT ARRAYS for zero gates, so a union would have both handles claiming both index sets — an OVERLAPPING `#REGISTER`, which §9 makes a fatal parse error rather than a merely wrong picture. Replacing at every touch makes the recorded lanes follow the bits, exactly as `cq_rec_swap` makes the birth value follow them. (iii) **A FORWARD TEMPLATE BRACKET NAMES A HANDLE IT HAS NOT MINTED YET.** D7b's defensive copy is a loop of `cq_emit_cx` and runs BEFORE the mint, so a bracket opened after the mint would leave those gates outside every bracket; the predicted handle is `cq_reg_count + (sources alias ? 1 : 0)`, checked by execution rather than by an assert. (iv) **THE PACKAGE RULE HAS A REACHABLE REFUSAL ROW.** §6 makes an op bracket with zero `#REGISTER` lines fatal, and a program whose every rail stays classical produces exactly that — so the teardown REFUSES to ship it, leaving the `.partial` on disk and saying so on `stderr`. Nothing is withheld: a gate needs a materialised bit and a materialised bit is a register, so such a trace has no gate lines either. **MEASURED END TO END 2026-08-28** against the QEC repo's own `scripts/verify_view.py`: a six-operation program produced 3 `#REGISTER` lines and 12 `level: "OP"` units in the rendered document, with the all-constant `i8` rail correctly absent — which is L5 made visible. **AMENDED 2026-08-28 (the user's call): `cqrt_alloc_i<W>` OPENS NO BRACKET, and it is the ONLY exemption.** An alloc bracket is an OP unit carrying no gate — noise in the algorithm view — and it says nothing the `#REGISTER` header does not. **The rule stays checkable because the exemption is a NAMED FAMILY decided STATICALLY, not a runtime "did it emit?" test**, which is still rejected for the reason `cq_shim_trace.h` gives: `cqrt_addc` emits nothing on an all-classical rail and `6W−5` gates on a poisoned one, so no reader can tell from the source which sites would qualify. `cqrt_alloc_i<W>` is different in KIND — by **I4** an all-constant rail owns zero qubits and `cq_reg_alloc_const` takes the register TABLE rather than the context, so it cannot reach a `qec_*` call at any value or any width. **Nothing is lost from the header**: `cq_trace_op`'s snapshot at an alloc was necessarily EMPTY, a rail that later owns a lane is named by whatever op materialised it, and a rail named by nothing else stays all-constant and gets no line either way. **`cqrt_free` KEEPS its bracket** — its open is where a rail's final index set is taken before the tombstone, so it is load-bearing for the header rather than for coverage. One benign consequence: a program of NOTHING BUT allocs now opens zero brackets, so (iv)'s refusal no longer fires for it and an empty, un-opted-in trace ships; any program that does anything at all still opens a bracket and still hits the refusal. Pinned as an ABSENCE by name rather than by count in `tests/test_shim_trace.c` (a count alone cannot distinguish restoring the alloc bracket from deleting a different one), and the mutant that restores it is killed by five checks |
| **D22** | **What §12(3) MEASURES, in which MODE, and where its T-count comes from** (`bd ye7`, filed 2026-08-28; `bd qi9`, filed 2026-08-20) | **Resolved 2026-08-28 at Step 25, by measurement.** §12 as printed reads as three claims about ONE run. It is not: **the three claims do not share a mode, and one of them does not compile.** **(1) NEEDS CQ_lang AND IS L6-SHAPED.** It is opt-in behind `-DCQOPS_CQLANG_DIR=`, and its gate is D18's *link, run, do not abort* **plus one clause D18 does not have** — the program must emit a **non-empty gate stream**. That clause is the whole reason (1) is not subsumed by L6: a program that folded entirely away links, runs and does not abort. **AND §12's LISTING DOES NOT LOWER — MEASURED.** CQ_lang declines it with `cq rotation lowering: non-diagonal gate over a live derived record`, because `y` and `hit` are records derived from `x` that are still live when the diffusion's `cq_theta(x, …)` fires; the region-shaped rewrite (`if (cond) x = cq_phi(x, M_PI);`) then declines one guard later with `cq controlled lowering: condition predates a non-diagonal gate under quantum control` as soon as the condition is DERIVED arithmetic rather than a direct compare on the wire. Both are UPSTREAM guards doing their job (CQ_lang's own `bd lom0`), not libcqops defects, and CQ_lang ships the accepted spelling itself in `tests/e2e/slice_control_seq_grover.c`: state prep, a **tainted-condition region** whose flag and work rail the teardown strips and frees, an ordinary arithmetic step, the inverse prep, and a reflection region. **So the listing is SOURCE INTENT and the acceptance artefact is the spelling that lowers** — recorded here rather than "fixed" in the listing, because the listing is what §12 has always meant and the divergence is the fact worth carrying. **(2) IS CLASSICAL MODE AND ITS PARENTHETICAL IS RETIRED.** Measured: with `x` all-constant the whole program emits **zero gates and zero qubits** — every kernel takes its R9 short-circuit and the §3 fold table produces the value outright — so *"proving the oracle's arithmetic circuits are correct"* is false: the mul and the icmp were never BUILT. The value claim itself HOLDS and is worth keeping (`Ry(π)` on a constant bit flips it, §7 / Rule 15, so the run is deterministic and equals plain C). What the row proves is **L5 at program scale**, and the zero-cost half is now part of the criterion rather than a remark. The circuits are verified by **L1** at mixed bit-kind masks — condition 2, met at Steps 10–17. **(3) IS QUANTUM MODE AND ONLY QUANTUM MODE.** Classical mode's counts are all zero, so a golden pinned there is a tuple of zeroes and a vacuously green acceptance gate — which is exactly what a step written to §12 as printed would ship. **THE T-COUNT DOES NOT COME FROM `cq_count_t` (`bd qi9`).** That counter is `7 × ccx`, ported verbatim from Bennett's `t_count`, exact only while nothing emits a rotation, and a **lower bound** the moment §7's general rows fire — which is precisely what quantum mode does. Its honest home is **M25**, which now exists: `qec_count(ctx, QEC_GATE_T)`. Do NOT "fix" `cq_count_t` by folding `ry`/`rz` into the Toffoli term — that breaks the one Bennett comparison the counter exists to make, exactly as `cq_count_total`'s own header explains for its exclusion. **AND THE MEASUREMENT MAKES THE BOUND TIGHT HERE:** §12's program emits exactly two distinct rotations, `Ry(π/2)` and `Rz(π)`, and **both are T-FREE** through the qec sink — `Ry` is D19's Clifford conjugation around an `Rz(1,2)`, and `(1,1)` and `(1,2)` are two of the four rows this driver's Ross–Selinger degenerate branch catches. So the pinned T-count IS `7 × ccx`, **as a measured consequence rather than as the assumption `cq_count_t` would have handed over**, and the gate asserts BOTH halves: the rotation ALPHABET (from the counting sink, which alone cannot cost it) and each letter's T-cost (from M25, which alone cannot know the alphabet). **PEAK QUBITS IS UNCHANGED** — `cq_qubits_peak()`, never a sink (§8's correction). **WHY THE WHOLE PROGRAM IS NOT RUN THROUGH THE QEC SINK:** condition 4 asks for a gate stream and a classical-mode value, not a fault-tolerant run. `qec_n_logical` is a HARD ceiling (D20) and this program peaks near a hundred logical qubits, while one Toffoli at `d = 3` is 562,564 physical gates — that is fault tolerance, not a defect, and condition 5 was the qec one and is already met at Step 26. **AMENDED 2026-09-10 (`bd cxp`, from `bd 2tm`), and the amendment is that the ARTEFACT MOVED WHILE THE DECISION HELD.** The `if (cond) x = cq_phi(x, M_PI);` above is the spelling MEASURED on 2026-08-28 and stays as the record of that measurement; it is **no longer the spelling that lowers, and no longer one that compiles.** CQ_lang retired the 15-width `cq_phi_<sfx>(x, angle)` family outright on 2026-09-05 with deliberately no migration shim, replacing it with a register-free `void cq_phi(double)` — a BRANCH phase that lowers by KICKBACK onto the minted flag (`cqrt_rz_i1`, uncontrolled) and touches no data register; `cq_theta` is unchanged. **This row's structure is untouched by that** — the listing is still source intent, (1) still needs the non-empty clause, (2) is still classical mode, (3) is still quantum mode, and the T-count argument is unaffected because `Rz(π)` is `(1,1)` on a flag exactly as it was on a register. **What it DOES change is a claim (1) never made and could not have made:** the retired spelling emitted a TENSORED `cqrt_rz_<W>`, `Rz` on every qubit, whose marked phase is `(−1)^popcount(target)` and which FIXES `|0…0>` — so the reflection about `|0>` marked nothing and the circuit was the identity on `x`, **while the gate MIX this row installed as (1)'s gate was green throughout**, a tensored `Rz` being MORE `rz` lines than the kickback form and not fewer. The mix is a count and a count does not identify. `tools/l7/l7_run.py` now carries a sixth clause — every `rz` target disjoint from the `mz` targets — which reads no golden and was provoked in both directions; see §12's note (iii) and `bd remember l7-gate-mix-could-not-see-the-tensored-rz` |
| **D23** | **Whether `cqrt_tape_*` is in scope, and how a zero-qubit token sits in M07's handle table** (`bd 1za`, filed 2026-08-22) | **Resolved 2026-09-02: TAPE IS IN SCOPE, as a deliberate v1.1 extension, and §1's stated reason for deferring it was FALSE.** §1 said *no consumer until `printf` on tainted data*. Measured at Step 24 and re-derived this session against CQ_lang `893b769` (2026-08-30, DIRTY working tree — `runtime/cq_runtime.h` itself is among the modified files, so carry the SHA and the shape, never the counts): **16 `cqrt_tape_*` calls across 6 fixtures** — `slice_control_abort`, `slice_io`, `slice_io_both_arm`, `slice_io_chained_theta`, `slice_io_controlled`, `slice_mixed_value_io` — 7 allocs, 4 uncontrolled writes (3 `i32`, 1 `i1`), 5 controlled writes (all `i32`). They ARE that consumer, they are otherwise v1-integer-clean, and every one of them ran to `tape is v2` and stopped. The deferral's argument was gone before the decision was; the decision goes with it. **THE COST IS THE COPY'S AND NOTHING ELSE.** `cq_runtime.h:436-500` (READ-ONLY; CQ_lang is unpinned) defines `cqrt_tape_write_<W>(tape, src) -> out` as *a CNOT-class basis copy — `src` is PRESERVED … the kept `out` rail is special: never freed, never uncomputed (the Bennett output tape)*, and the controlled form as `cqrt_copy_<W>_controlled`'s mirror with the flag as argument 0. So a write is `cq_reg_alloc_zero` plus the SAME per-lane `cq_reg_xor_into` that `cqrt_copy_<W>` uses — no hand-written CX loop — and the controlled write is that inside the ONE §9 region bracket, exactly as `cqrt_copy_<W>_controlled`. No new kernel, no new gate, nothing for Rule 1 to look up: there is no construction. **D15's CERTIFICATE IS UNTOUCHED, AND THE ROW THAT PROVES IT IS THE SOURCE'S FREE.** The kept rail's history is a birth at `|0⟩` plus ONE write that pairs with nothing; CQ_lang never frees it, so no window over it ever opens; and a caller that frees it anyway gets a lone unpaired write, which the reduction cannot discharge — the rail STRANDS, never releases. `src` is READ by the write (its `last_read` moves; no write is recorded against it), so a rail written onto the tape between a template forward and its `_unc` — or between a self-adjoint `cqrt_copy` pair — still pairs and still frees CLEAN. That is asserted (`tests/test_runtime_tape.c`), and it is the row that goes wrong if the effect table puts the write on the wrong operand. Two rows in `shim/cq_shim_record.c`: `CQ_ROP_TAPE_WRITE` reads slot 0 (`src`) and writes slot 1 (`out`); `CQ_ROP_TAPE_WRITE_CTRL` reads slots 0 and 1 (`ctrl`, `src`), controls slot 0, writes slot 2 — `cqrt_copy`'s two rows with the destination minted rather than named. Every pairing flag is 0: a tape write pairs with nothing, and `self_adjoint = 1` would be a lie twice over (a second write mints a DIFFERENT `out`). The token is represented in the record layer by its ABSENCE — no history, no mint marker, `cq_rec_hist(token) == NULL` — because a token is never written and never read. **THE TAPE HANDLE IS A CLASSICAL TOKEN OWNING ZERO QUBITS, AND IT MUST COME FROM THE SHARED D5 COUNTER.** `cq_runtime.h` says so (*a CLASSICAL resource token (like a file descriptor), NOT a qubit and NOT tainted … drawn from the same monotonic counter as every qubit handle but PRINTS with a `t` prefix*) and every golden shows it: `cqrt_tape_alloc() -> t0`, then `cqrt_alloc_i32(4) -> h1`. It HAS to share the counter: `tape` and `src` are both `int32_t`, so a token drawn from a second counter would collide with a rail number and *a rail handed as a tape* would be undetectable — the same hazard D16 records for `cqrt_alloc_handle`. Therefore it occupies a slot in M07's table, and the representation is **plan §0.5**: a FOURTH slot state, `CQ_SLOT_TOKEN`, width 0, `bits == NULL`, minted by `cq_reg_alloc_token`. NOT a width-0 register — `cq_reg_mint` refuses width 0 (measured, `register width out of range`), and a zero-width rail would `cqrt_free` CLEAN and `cqrt_measure` to 0, where a token handed to either must be a HARD ERROR. NOT a shim-side token set — it cannot draw from the counter without a slot, and every rail refusal would then be re-implemented at 62 entry points instead of inherited from M07's two funnels. **WHAT EVERY RAIL ENTRY POINT DOES WITH A TOKEN: A HARD ERROR IN BOTH CONFIGURATIONS, FROM M07, NAMING THE HANDLE AND SAYING IT IS A TOKEN.** Every rail accessor passes through `cq_reg_readable` (reads) or `cq_reg_slot_mut` (writes, free, measure), and both refuse the state — so `cqrt_x`, `cqrt_free`, `cqrt_measure_*`, `cqrt_copy_*`, a template operand and a §9 flag all land on one message with no per-entry-point code. `cq_reg_width` and `cq_reg_is_live` stay permissive (width 0, not live), which is what lets `cq_reg_audit` and the D21 snapshot SKIP a token as they skip a tombstone; `cq_reg_check_operands` refuses it BY NAME rather than as *not a live rail*. The shim's own refusal is the converse: a RAIL in the `tape` slot of `cqrt_tape_write_*` is `FATAL: shim:` naming both handles. **D21's ALLOC EXEMPTION WIDENS BY ONE NAMED SYMBOL.** `cqrt_tape_alloc` opens no bracket, for the same STATIC reason as `cqrt_alloc_i<W>`: `cq_reg_alloc_token` takes the register TABLE, not the context, and cannot reach a `qec_*` call. The exemption is still a named family decided statically — *the five `cqrt_alloc_i<W>` and `cqrt_tape_alloc`* — and *bracket the ones that emit* stays rejected. The write's bracket names `src` (and `ctrl`) in and `out` out, and does NOT name the token: a token has no `#REGISTER` line (zero qubits — I4 made visible) and `in=` would spell it `h<N>`, which is a lie. **D7b: NO DEFENSIVE COPY.** Measured over the six goldens, the control aliases the source in 0 of 5 controlled writes; and the shape is refused anyway — at `W = 1` a flag that IS the source is a control coinciding with an inner control, §9 row B, M06's hard error in both configurations. **D16's TABLE MOVES BY ONE ROW**: `tape` 11 → implemented, `shim/cq_runtime_tape.c`; the abort population is **98 = 34 fp + 63 `qram` + `cqrt_alloc_handle`**; `nm` still shows **171** defined `cqrt_*`. **WHAT THIS RESERVES FOR QRAM (`bd 9zq`, Step 27) — settled here as if it will be reused, because it will.** `cqrt_qram_alloc_<W>(count) -> a<N>` is the SAME classical handle in the SAME counter, and it takes the same `CQ_SLOT_TOKEN`. What qram needs beyond identity — a payload: `count`, the element width, the entries — goes in a SHIM-SIDE side table keyed by the token handle, never in a field on `cq_reg`: M07 carries IDENTITY and nothing else. A tape carries no payload in v1.1 because nothing reads a tape back (the ABI has no `tape_read`; *the measurement is DEFERRED to tape inspection (a backend concern)* and this backend's inspection is the kept rail itself, live at the end of the program). **Do not add a payload now, and do not add a fifth state for qram** — one token state, two owners |
| **D24** | **QRAM: what an array IS in M07 terms, which Bennett constructions the read and the store port, and how the array, the cells and the per-store tape appear to D15's certificate** (`bd 9zq`, Step 27; the three questions the step's brief named, none settled in code) | **Resolved 2026-09-02, v1.2 — QRAM IS IN SCOPE at all nine widths, and every clause below was written before a file existed.** **(a) AN ARRAY IS D23's TOKEN PLUS `count` REGISTERS.** `cqrt_qram_alloc_<W>(count) -> a<N>` mints the SAME `CQ_SLOT_TOKEN` a tape does, out of the shared D5 counter (§0.5 — one token state, two owners, no fifth state), and then `count` ordinary M07 registers of width `W` through `cq_reg_alloc_zero`, consecutively — handles `a+1 … a+count` — all-constant at birth (I4: ZERO qubits at alloc, whatever `count`), lazily materialised per bit by the fold table as stores write them, and **NEVER freed**: the ABI has no qram free (63 = 9 widths × {alloc, load, load_unc, store, store_unc, store_controlled, store_controlled_unc}), so the cells are the program's persistent state, held to the end exactly as a tape's kept rail is (NORTH_STAR condition 3's first intended-safe-leak population). The token OWNS them through a SHIM-SIDE payload table keyed by the token handle — `shim/cq_shim_qram.[ch]`: element width, `count`, the first cell handle, and the per-array LIFO tape stack — never a field on `cq_reg`. **A cell is a REGISTER and not a shim-owned `cq_bit` array, and the reason is L2 and I2.** Both are stated over REGISTERS: `cq_reg_audit`'s owner sweep and `cq_pc_live_is_exactly`'s union form would read a qubit materialised into a shim-owned array as *live and owned by no named register* — a leak — and both would have to learn about the payload. As registers the cells cost L2 NOTHING: the union form holds verbatim and a test's live set simply names every cell. A single `count·W`-wide register was rejected on `CQ_REG_WIDTH_MAX = 128`: the corpus's only integer shape is `8 × 32 = 256`. What CQ_lang sees is the D5 counter advancing by `1 + count` per alloc, which is the numbering divergence D18 already retired. `count ≤ 0` is a hard error at alloc, and so is `count > 2^28`: the sandwich's step index is an `int` (plan §0.1) and the tree has `3·(2^n − 1) + 1` compute steps. **(b) THE READ IS BENNETT'S UNARY-ITERATION QROM WITH A TOFFOLI FAN-OUT; THE STORE IS BENNETT'S SHADOW STORE AT A QUANTUM INDEX UNDER THE SAME TREE; BOTH RUN THROUGH `cq_sandwich`.** K13 (`docs/constructions/K13.md`, `src/kernels/qrom.[ch]`) ports `emit_qrom!` / `_qrom_tree!` (`qrom.jl:41-133`, pinned `980805de`): a complete binary AND tree over the low `n = ⌈log₂ count⌉` lanes of `idx` produces `Lp = 2^n` leaf flags of which exactly one is set; Bennett fans each leaf out with data-dependent CNOTs because ITS data is a compile-time constant, and OURS is a rail, so the leaf fan-out is one Toffoli per (cell, lane), `CCX(leaf_j, cell_j[i], dst[i])` — and the §3 fold table makes a constant cell lane cost exactly what Bennett's constant data costs (a ZERO lane nothing, a ONE lane one `CX(leaf, dst)`), so on a memset-0 array the port IS Bennett's QROM gate for gate. It is a Rule 7 kernel — `dst ^= mem[idx]`, sources unchanged, scratch clean — so `_unc` is the same kernel with `dst = out`, and it is driven by `cq_sandwich` (Rule 8): compute = the tree, copyout = the `count·W` fan-out, reverse = the tree. **The tree is FLAT, not Bennett's DFS, and that is the one delta, taken on D9's precedent.** Bennett walks the tree depth-first, computing two child flags, recursing, and uncomputing them on the way back, so its peak is `2n + 1` wires; that interleaves compute, fan-out and uncompute per node, which the driver's compute→copyout→reverse shape cannot express, and hand-writing the per-node uncompute is what Rule 8 forbids. The flat form computes all `2·Lp − 1` flags breadth-first as one compute half. The GATE COUNT is identical — `2 X`, `4(Lp−1) CX`, `2(Lp−1) CCX` for the tree, the bead's `2(L−1)` Toffoli with `L = Lp` — and the qubit peak is not (`2·Lp − 1` against `2n + 1`); the DFS form is filed for v2. **The index is truncated, not bounds-checked**: the kernel reads lanes `0 … n−1` of `idx` and ignores the rest (`qrom.jl:171`, `idx_full[1:n]`), a non-power-of-two `count` is padded to `Lp` with leaves that own no cell (`qrom.jl:165-168`, "padded entries return zero"), so an index in `[count, Lp)` reads 0 and stores nothing, and the frozen ABI's own contract is `idx ∈ [0, N)`, OOB undefined (`QramLowering.cpp:166`) — this is our choice inside that contract, not a widening of it. `count == 1` takes Bennett's `L == 1` branch (`qrom.jl:69-79`): no tree, no scratch, a plain `CX` copy. K14 (`K14.md`, `src/kernels/qstore.[ch]`) ports `emit_shadow_store!` (`shadow_memory.jl:38-55` — three CNOT sweeps, `cell → T`, `T → cell`, `val → cell`) at a quantum index by guarding every sweep gate with the leaf flag, which is `emit_shadow_store_guarded!` (`:98-121`) with `pred_wire = leaf_j`, under the SAME tree block K13 exports — Rule 1 applied to the call graph, as M12 and M19 do. **ONE `W`-bit tape slot per store, not one per cell, and that is the second delta**: upstream's `shadow_checkpoint` checkpoints every slot (CQ_lang PRD-7.5 §2.4: "n per-slot tapes + n equality wires"), but under unary iteration exactly one leaf is set, so `T[i] ^= leaf_j ∧ cell_j[i]` summed over every `j` lands `cell[idx]` in ONE slot — `W` tape qubits per store instead of `count·W`, the same `3·count·W` Toffolis. K14 is **NOT a Rule 7 kernel** and is recorded against the rule's text as D17 recorded `addc`: it is in place, destructive on the cells and on the tape slot, and its inverse is the REVERSE CIRCUIT — the three sweeps in the opposite order — so a second call would not cancel and it may never be substituted for K13's shape. It still runs under `cq_sandwich`: the tree is the compute half and its reversal is structural; the sweeps are the copyout; the ONLY thing that differs between `cq_kernel_qstore_push` and `_pop` is the copyout's index order, which is a map and not a second gate list. Cost at all-quantum: the tree as K13 plus `3·count·W CCX`. **The controlled store is Rule 9 verbatim** — `cq_shim_region(pred, body)`, every gate promoted by M06, `(x, cx, ccx) → (0, x, cx + 3·ccx)` at the all-quantum mask, and §9 row 0 for a classical `pred`: ZERO skips the body (the tape slot is still pushed, all-constant, zero qubits by I4, so the `_controlled_unc` pops symmetrically), ONE runs it verbatim. The cheaper spelling — the guard as the tree's ROOT flag, `CX(pred, root)` for `X(root)`, `+0` gates, Bennett's `parent_flag` parameter with `pred` in it — is REJECTED for v1.2 and filed: it would make K14 the one kernel with an opinion about the axis, and Rule 9's "no kernel is aware the axis exists" is worth a 3× on the sweep Toffolis until a program pays it. **(c) THE ARRAY IS ONE RECORDED HANDLE — THE TOKEN, WITH A HISTORY; THE CELLS HAVE NONE; THE TAPE SLOT IS A RECORDED RAIL FREED THROUGH D15's DISPOSITION.** Unlike a tape token, the array token IS minted in the record (`cq_rec_mint` with a marker at alloc), is WRITTEN by every store row and READ by every load and store row. The reason is the load pair: `out`'s certificate reduces `pair_operands_unchanged` over `(load, load_unc)`, which asks whether the ARRAY's writes in that window reduce to identity — and with no history on the token a store between a load and its `_unc` would be INVISIBLE and `out` would be RELEASED DIRTY, the one unforgivable direction. That case is asserted (`a_store_between_a_load_and_its_unc_strands_the_out_rail`). The CELLS are never an operand of any recorded call and CQ_lang can never free one (it never receives the handle), so they carry no history; a cell handle reaching `cqrt_free` is UNPROVEN to the certificate and answers to the shadow alone — CLEAN if nothing ever stored into it, STRANDED otherwise — which is the "a lone store leaves the cells stranded if anyone could free them" clause, for free. The TAPE SLOT is minted at birth `|0⟩` on the push, recorded as WRITTEN by the push and by the pop (its declared twin), and released at the pop through `cq_reg_free(T, cq_shim_free_proof)` — D15's disposition, in one place: CLEAN iff the pair reduces, which by the engine's own rules means `idx`, `val` (and `pred`, through the record's `ctrl` slot) unchanged over the window and the array's intervening writes — the nested LIFO pairs — reducing recursively; a pop whose operands moved in between STRANDS the slot, counted and named on `stderr`, and a lone push is never popped and strands nothing because nothing ever frees it. **Six rows** in `shim/cq_shim_record.c`: `QRAM_LOAD` / `_UNC` (`out, arr, idx`; reads `arr|idx`, writes `out`; twins — TPL_FWD/UNC's shape with the array in a source slot), `QRAM_STORE` / `_UNC` (`arr, idx, val, T`; the forward reads `arr|idx|val` and writes `arr|T`, the pop reads `T` as well; twins), and the two `_CTRL` rows, the same with `pred` in `ctrl`. `controls = 0` on all six: no commutation is claimed for a memory op, which is the decline direction. **The record widens from THREE operand slots to FOUR** (`CQ_REC_SLOTS`), because a store names four handles and a twin pair must carry identical slots; every existing `rec` helper sets slot 3 to `CQ_REG_NONE`. **The tape stack is per array and LIFO, and a pop VERIFIES**: the top entry's `(idx, val, pred, W)` must equal the call's — handles are D5-monotonic, so equality is identity — and a mismatch or an empty stack is a hard error in both configurations naming the array, on §2.8(ii)'s own words. **D21**: `cqrt_qram_alloc_<W>` joins the alloc exemption on the same STATIC ground (`cq_reg_alloc_token` and `cq_reg_alloc_zero` take the table, so it cannot emit); a load's bracket names `idx` in and `out` out, a store's names `idx`, `val` (and `pred`) in and NOTHING out — the array is a token (`in=` would spell it `h<N>`) and the cells and the tape slot are handles CQ_lang never received, rule 3, so they get no `#REGISTER` line and their gates appear under the load's or store's bracket as `cqrt_addc`'s transients do. They are persistent state, not workspace, and the algorithm view cannot attribute them; a per-cell line `a<N>.<j>` needs a trace API for unreceived handles and is filed for v2. **D7b, MEASURED**: over all 31 qram fixtures at CQ_lang `893b769` (dirty), 0 of the 16 store-family calls alias any two of `(pred, idx, val)`, and a load's `out` is minted fresh — no defensive copy; a kernel-boundary alias is `cq_kernel_check_n`'s hard error as everywhere. **TWO OWNERS OF ONE STATE, AND THE TAPE FILE LEARNS IT**: a qram token in `cqrt_tape_write`'s tape slot passed the state check until this decision — it refuses one by name now, and a tape token in a qram slot is the qram file's refusal. **THE COUNTS THAT MOVE**: `cq_runtime_v2.c` 98 → **35**; the census's v1 list 76 → **139**; `nm` still 171. **What CQ_lang PRD-7.5 promised and this decision keeps**: the strategy never reaches a signature; the tape is ours; `_unc` has the forward's argument order and pops; an escaped array's tapes leak (§2.9) and here that is the tape slot staying a live rail for good |
| **D25** | **What a kernel whose scratch region does not fit the D2 ceiling DOES, and WHERE that refusal reaches CQ_lang** (`bd fxz`, filed 2026-08-16 against M18, widened to M19/M20 the same day; §15 D20 cites it and leaves it open) | **Resolved 2026-09-10, by measurement, and the resolution is that NOTHING IS IMPLEMENTED — the behaviour was already correct and was untested.** `bd fxz` asks two questions and they have different answers. **(1) WHAT IT DOES: it aborts, from the POOL, DURING PRE-MATERIALISATION.** `cq_sandwich` step 1 takes the whole region before any gate is emitted (plan §0.1, I6(b)), so the refusal is `cq_qubits_acquire`'s D2 hard error and it lands before the first gate of the construction reaches the sink — *`libcqops: FATAL: qubit pool: ceiling exceeded (32, 31)`*, MEASURED at K11 W = 4 on an 8-qubit operand pair. **`bd fxz`'s own "half-allocate and leave the pool inconsistent" cannot arise, and the reason is not that the region is unwound — it is not.** Bits 0..k−1 of the region ARE materialised when bit k trips the bound; what makes that harmless is that `cq_pool_die` calls `abort()`, so there is no continuation in which the pool is read again. A ceiling refusal that RETURNED would be the defect, and neither M03 nor any caller has an error return to make one out of. **(2) WHERE IT SURFACES: at the opcode surface, unswallowed and untranslated.** M26 installs no handler, catches nothing, and every `cq_template_*` either returns a handle or does not return, so the program stops at the operation that asked for the region and names the pool. **THE SHIM MUST NEVER GROW ITS OWN "that will not fit" CHECK** — that is this repo's recorded masking-layer trap, and the pool's message is the only one that carries the two numbers a caller can act on. **THREE CONSEQUENCES THAT ARE EASY TO GET WRONG.** **(a) THE TIGHT BOUND IS THE PEAK, NOT THE REGION.** `dst`'s lanes are materialised by the COPYOUT, which runs while the whole region is still live, so K11 at W needs `W² + 3W` and not `W² + 2W`; a device holding exactly the region does not run the kernel. **(b) R9 IS WHAT KEEPS A BOUNDED DEVICE ABLE TO MULTIPLY AT ALL.** L5's short-circuit is stated everywhere as "zero gates, zero qubits"; in D2 terms it is the stronger claim that an all-classical `mul` runs at ANY width under a ceiling that admits nothing new, because it never enters `cq_sandwich`. So i128 `mul` is not simply fatal on a small fabric — only its QUANTUM path is. **(c) `cq_qubits_set_ceiling(p, 0)` LIFTS the ceiling** (D2: 0 means unbounded), so "admits nothing new" is inexpressible on an empty pool and the obvious writing of that test asserts nothing. **WHAT WAS OWED WAS A TEST, AND IT IS A PAIR PER KERNEL** — the refusal is a death and the sufficiency is not, and neither half is worth much alone: `tests/test_kernel_mul_death.c` (a ceiling one short of the region, with a sink that ABORTS ON ANY GATE, which is what turns "fail loud rather than half-allocate" into a checked claim rather than a bare "it aborted"), `tests/test_kernel_divrem_death.c` for K12 — the largest object in the catalogue, 131,583 at i128 — `tests/test_template_death.c` for (2), and `tests/test_kernel_mul_ceiling.inc` for the sufficiency half and for (b). **NOT ADOPTED, AND NOT AS A WAY TO CLOSE THIS**: K11.md §5 note 8's `pp` recycling (`W² + 2W → 2W + 1` qubits) and `bd 5zn`'s linear K12 schedule (`13W + 3` = 1,667 at i128) both move the number and are RE-DERIVATIONS, which Rule 1 puts outside v1 |

> ### D10 — the window is absolute, and the relative reading was built and measured first
>
> §7 says "an exact-multiple test against a tolerance, configurable, default `1e-12`
> relative" and never says relative to what. There are two readings and they are not
> close.
>
> **Relative to |θ| — the natural one, and a miscompile.** The window `tol·|θ|` grows
> without bound while the lattice spacing stays `π`, so above `|θ| ≈ 1e11` it starts
> swallowing whole lattice cells. Built, shipped into a 16-case suite, and green.
> Measured against a 60-digit π, and the figure after each θ is the true distance from θ to
> the multiple of π that the returned row **named**: at `θ = 1e12` the window is a **full
> radian**, and `Ry(1e12)` classifies as `CQ_ANGLE_IDENTITY` while sitting **0.657625 rad**
> away — so M22 would emit **nothing at all**. `Ry(1.570673279e12)` reports `−I` at
> **1.292108 rad**. **It is not a cliff, it is graded**, because the window is linear in
> `|θ|`: `4999995.504637527` folds to `IDENTITY` at `2.0e-6 rad`, `999999993.1398191` at
> `4.0e-4`, `99999999992.56593` at `4.0e-2`. 63.6% of angles sampled near `1e12` fold to
> some special row. **This is the forbidden direction** (§7's other rows all cost a gate;
> this one deletes a rotation), and it was invisible to the suite for two independent
> reasons worth carrying: the magnitude band `2.5e4 … 6.3e13` was untested, and the suite's
> "independent" reference shared the same window, so it **agreed with the bug**.
>
> **Relative to the modulus π — pinned.** `window = tol · π`, so a fold discards a bounded
> angle at every magnitude. The apparent argument against it is that a caller spelling
> `k*M_PI` accumulates error proportional to `k`, so the window must grow to keep
> recognising large multiples. **That argument is false, and measured false:** the
> residual tested is `|θ − fl(k·π_double)|`, and for θ spelled `k*M_PI` that is **exactly
> zero at every k**, because both sides are the same rounded product. The relative reading
> bought nothing for what it cost.
>
> **One refusal carries the whole error bound.** What the residual cannot see is
> proportional to `|θ|`: half an ulp from rounding the product (`2^-53 = 1.1103e-16`) plus
> the drift of the double π from π (`(π − π_double)/π = 0.389817e-16`), summing to
> `1.500040e-16`, rounded up to `1.6e-16`. So `|θ|·1.6e-16 ≤ tol·π` is refused, giving the
> contract **`|θ − k·π| ≤ 2·tol·π`** and a reach of `|k| ≤ 6250` at the default. Without
> it, `θ = 2^52·π_double` has a residual of **exactly 0** and is **0.551532 rad** from any
> true multiple of 4π — the same defect from the other side, and one that an earlier draft
> of the test suite *asserted as correct*.
>
> **And a cap, `CQ_ANGLE_TOLERANCE_MAX = 1e-3`,** which is one bound doing three jobs: it
> keeps the window 500× under the `π/2` at which two lattice points could match at once,
> keeps `|k|` under `2^53` so `(long long)round(θ/π)` is both defined and exact, and keeps
> "tolerance" meaning "these are the same angle". A tolerance outside `[0, 1e-3]` is a hard
> error in both configurations. `tol = 0` is legal and admits **exactly one angle**: π is
> irrational, so zero is the only double that is exactly a multiple of it.
>
> **Nothing in the corpus is affected**, which is why the cheap side is again the right
> side. Measured 2026-08-17 over all 239 goldens: **410 rotation calls** (343 `ry`, 67
> `rz`), **26 distinct angles**, and **not one lands on any of §7's four special rows**.
> Every one is a small decimal — `0.5` alone accounts for 256 calls. The rows are
> exercised by §12's Grover (`M_PI/2 → M_PI`, where the residual is 0) and by M21's own
> suite, and by nothing else in v1.
>
> **The corpus's closest approach is `3.14`, and it is the reason the cap sits where it
> does.** Two fixtures pass a literal `3.14` (`0x1.91eb851eb851fp+1`), which is `1.5927e-3`
> radians short of π — `5.1e8` times the default window, so the default has enormous
> headroom. But it is *inside* `CQ_ANGLE_TOLERANCE_MAX·π = 3.1416e-3`: at the loosest legal
> tolerance a `Ry(3.14)` folds to a half turn and becomes an `X`. That is the cap doing
> exactly what a cap should — `1e-3` is the order at which "tolerance" stops meaning "the
> same angle" and starts meaning "near enough", and a caller who sets it there is asking
> for that. It is also why the cap must not be raised. Pinned in `tests/test_angle.c`.
>
> > **CORRECTED AT STEP 19: THE CAP'S WITNESS IS ON THE WRONG COLUMN, THOUGH THE CAP IS
> > RIGHT.** Both corpus occurrences of `3.14` are **`cqrt_rz_i32`** calls —
> > `spec_select_caller.expected.log:12` and `spec_twoarm_caller.expected.log:30` — and the
> > `Rz` column has no half-turn row at all: `cq_angle_rz_row` collapses `HALF_TURN` into
> > `GENERAL`, so the corpus's own `3.14` classifies as `GENERAL` **at every legal
> > tolerance, including the cap**, and never becomes an `X`. The sentence above (and
> > CLAUDE.md's Rule 15, which repeats it) states the cap's justification against a
> > *hypothetical* `Ry(3.14)`; that hypothetical is real enough — a caller may pass 3.14 to
> > `cqrt_ry_i32` — but it is not something the corpus does. `tests/test_angle.c` pins
> > `CHECK_RY(3.14, GENERAL)` and `CHECK_LATTICE(3.14, MAX, HALF_TURN)` and had **no**
> > `CHECK_RZ(3.14, …)` at all, i.e. the one column the corpus actually exercises was the
> > untested one. Covered at Step 19 by
> > `tests/test_rotate.c:the_corpus_rz_angle_is_a_real_rotation_at_every_legal_tolerance`,
> > which sweeps `tol ∈ {0, default, 1e-6, MAX}` and asserts the EMISSION — one `sink.rz`,
> > no `X` — rather than only the classification. It is placed in M22's suite rather than
> > M21's on purpose: the claim that matters is what gets emitted, and `tests/test_angle.c`
> > is at 294 of 300 with its own second seam already recorded (`bd w8j`).
>
> **D7 used to be one row reading "v1: assert loud and find out empirically whether the
> pass ever does it".** Step 7 did the measurement, and the two halves came out in
> opposite directions — so the single row is now two. A blanket pairwise-distinctness
> abort would fire at Step 24 on ten shipped CQ_lang fixture lines, and the reflex fix
> for that would be to delete the whole check, losing the D7a detection that does matter.
>
> Why D7b cannot be left to the kernels: a kernel sees `cq_bit *` and `W` (§4), never
> handles, so it cannot detect *handle* aliasing at all — and the remedy, a defensive
> copy, is a handle-level act. What a kernel *does* see is `a[i]` and `b[i]` being the
> **same bit**, which reaches §3's `CCX` distinctness assert — an abort from inside a
> kernel, seventeen steps from its cause, in Debug; and in Release that check is compiled
> out and a malformed `CCX(q,q,t)` reaches the sink as a silent miscompile. **Both halves
> of that were MEASURED at Step 10 and are worse than this paragraph implied**, which is
> why `cq_kernel_check_dst` now aborts on `a` and `b` overlapping *as arrays*, in both
> configurations, with a message naming the missing M26 copy. That is a backstop on the
> remedy, not a substitute for it: the remedy still sits at the handle boundary, and it is
> what guarantees a kernel never sees the alias in the first place.
>
> **D8's asymmetry, since it is the whole reason for the answer.** Bennett's *constant*
> path throws outside `0 ≤ k ≤ W`, so upstream never has to reconcile the two; we cannot
> throw, so we must. The two paths were thought to differ by masking-versus-saturating,
> and they do not: **the barrel saturates too.** Each of its `⌈log₂W⌉` stages zero-fills
> the positions it shifts in (`arith.jl:355-400` — the destination is a fresh all-zero
> register and out-of-range copies are simply not emitted), and saturating shifts compose,
> so the variable path computes `sat_shift(a, k mod 2^S)` and not a rotate. It therefore
> saturates unasked on the whole interval `[W, 2^S)`. Consequences, all measured
> 2026-08-15:
>
> - **The conflict is a power-of-two artefact.** At `W ∈ {8,16,32,64,128}`, `2^S = W`, that
>   interval is empty, and the paths differ at *every* `k ≥ W`. At **i80** they agree
>   throughout `[80,128)` and first differ at `k = 128`. `ckd.16`'s worked i80 example
>   (`k = 100`) does **not** reproduce — both paths give 0. Its `W=32, k=32` example does,
>   and sharply: the constant path annihilates the value, the variable path returns it
>   untouched.
> - **Masking alone is not enough, which is a correction to `K04.md` §5(e) option 2.** At
>   `W = 80`, masking to 7 bits leaves amounts in `[80,128)`, and a constant path that only
>   masked would emit a shift by 100 on an 80-bit register. The rule is mask **and then**
>   saturate — still zero gates and zero ancillae, just two operations rather than one.
> - **The cost is roughly 200:1.** Making the *variable* path saturate needs an OR-reduction
>   over the `W − S` amount bits it currently never reads, plus a controlled force of the
>   result: `+156` gates and `+26` qubits at `W=32`, `+432` and `+72` at `W=80` — about
>   `+19%` on the barrel, paid on every variable shift. Making the *constant* path mask and
>   saturate is two classical operations on `k`.
> - **Nothing in the corpus is affected.** 916 shift calls across the 239 goldens, 895 with
>   a constant amount: **every one in `[0, W)`, none negative.** D8 defines behaviour for a
>   case CQ_lang has never emitted, which is why the cheap side is the right side.
>
> **The deliberate delta, recorded so it is not mistaken for a bug.** D8 disagrees with
> Bennett's constant path at exactly `k == W`, where `lower_shl!` returns 0 and D8 returns
> `x` (for power-of-two `W`). Upstream *pins* that row — `test/test_zmw3_shift_bounds.jl`
> asserts `isempty(gates_shl)` at `k == W` — so this is a knowing deviation, sibling to
> K11's Cuccaro choice. It is forced: the alternative is to deviate from the *variable*
> construction instead, at 19% of its gate count, and upstream pins nothing there at all
> (`lower_var_*` is called by no test in Bennett's repository).
>
> **Two traps for the port, both from the same reading.** Bennett's `k` is
> `ConstOperand.value::Int` built through `LLVMConstIntGetSExtValue`, so an i32 shift
> amount of `0x80000000` arrives **negative**; ours is decoded from `cq_bit`s and has no
> sign, so M11 must not inherit a signed `k` or the sign test that goes with it. And the
> `s >= W && break` guard in all three `lower_var_*` functions is **unreachable** given
> `_shift_stages`'s own bound (checked for every `W` in `[1,4096]`) — porting it as live
> logic would be porting a branch upstream never takes.

> ### D9 — why FLAT, with the alternative measured rather than dismissed
>
> `bd ckd.13` was filed as *"flat is quadratic (33,151 qubits at W=64), nested is `O(W)`, and
> a quadratic-ancilla divrem may simply not fit on hardware"*. **Three schemes were built and
> run** — not argued about — at `W ∈ {1,2,3,4,8,16,32,64,128}` in both configurations, each
> L1 green (exhaustive at `W ≤ 4` over every `(a,b)` including `b = 0` when Step 17 landed;
> a constant sample as of 2026-08-21, where `b = 0` is an ordinary drawn value reached by
> the all-zero corner at every width), pool restored and
> I6-clean (`K12.md` §4.1, §6.0a):
>
> | scheme | qubits | gates | i8 | i64 | i128 |
> |---|---|---|---|---|---|
> | **FLAT — pinned** | `8W²+4W−1` | `34W²+5W` | 543 / 2 216 | 33 023 / 139 584 | 131 583 / 557 696 |
> | nested | `W²+10W` | `64W²+5W` | 144 / 4 136 | 4 736 / 262 464 | 17 664 / 1 049 216 |
> | linear | `13W+3` | `90W²−3W` | 107 / 5 736 | 835 / 368 448 | 1 667 / 1 474 176 |
>
> - **The bead's "nested is `O(W)`" is false** — per-iteration uncompute reclaims the
>   comparator, subtractor and mux scratch (`7W+1` bits) but not the `W`-per-iteration
>   remainder chain, so nesting alone is `W² + 10W`.
> - **A genuinely linear scheme nonetheless exists**, and it is worth stating precisely
>   because the plausible argument that it *cannot* (— "reclaiming the remainder needs a
>   controlled add, and the controlled axis is M06 at Step 20") **is wrong**: a
>   single-qubit-controlled add of a register is already a *ported* libcqops construction —
>   `mul.c`'s own shape, `multiplier.jl:22-29` — mask the addend with `W` Toffolis, then run
>   an uncontrolled adder. Since `r_in[t] = rnext[t] + fits_t·b` exactly, that reclaims the
>   remainder and the tape collapses to two alternating blocks.
> - **FLAT is still the v1 answer**, for reasons that are about consistency rather than
>   possibility: it is the *ported* shape (Bennett's divider allocates per iteration and frees
>   nothing, cleaned by one global wrap — PRD §5 verbatim), whereas the linear schedule is an
>   in-repo scheduling invention; it is 2.64× cheaper in gates; its step decode is four
>   phases against eleven, in the module R4 already names as the likeliest 300-line breach;
>   and **this repo has taken the same call twice already** — `mul.c` records `pp` recycling
>   (`W²+2W → 2W+1`) and `bd b8g` records the barrel's `sh_k` region, both measured and both
>   deliberately not taken in v1. **What it costs is stated plainly: 131,583 qubits per i128
>   `udiv` against the linear scheme's 1,667.** If the QEC target ever makes that binding,
>   the linear design is filed, measured and confined to M19's step decode and its goldens.
> - **None of the three needs a depth-aware `cq_sandwich`** (`bd 4tt` item (b)). Per-iteration
>   uncompute is *scheduled as extra step indices inside the one flat step space*; it is not
>   an inner sandwich. Both alternatives run through the **unmodified** driver. M09 needs no
>   change under any option considered.
>
> **(b), (c) and (d) are not savings looked for; they are consistency with what already
> ships.** (b) K12 composes *libcqops kernels*, not Bennett's IR shape — and libcqops's `uge`
> is M16's, which reads the carry-out and folds Bennett's double negation into the copy-out
> (`K09.md` §5 delta 2). Keeping `not1(ult(…))` inside K12 would have put two different
> `uge`s in one library, with the more expensive one in the only kernel that invokes it `W`
> times per call. (c) `q[i]` is provably zero and written exactly once, so `^=` *is* `:=`;
> the alternative costs `7W²` extra gates for the same permutation. (d) with the remainder's
> upper bits as real scratch, iteration `t = 0` costs what every other iteration costs, the
> step decode stays a pure function of `s` (Rule 8), and **the composition identity holds** —
> `compute = W · (2 + C_ult + C_sub + C_mux)`, with each `C` obtained by asking M16, M14 and
> M17 what they cost at this width. That identity is K12's only L4 assertion that survives
> `CQOPS_UPDATE_GOLDENS=1`, and K12 offers a bigger version of the "shorten the inner loop"
> mutant that left K11's entire L1/L2/L3/L5 sweep green. Build it before M19.
>
> **What D9 costs, stated plainly.** `divrem` ships at **i128** (`opcode_table.yaml:187-190`,
> full 15-variant grid including the bare `qq` shape — so this is not an `_hl`-only surface),
> and one i128 `udiv` is **557,696 gates over 131,583 scratch qubits**: the largest object in
> the v1 catalogue by 8×, and where D2's ceiling bites first. That failure is already loud —
> `cq_qubits_acquire` fails hard on the ceiling — and M19 owes nothing beyond not swallowing
> it. The nested schedule stays available as a v2 change that touches M19's step decode and
> its goldens and nothing else.
>
> **SHIPPED 2026-08-16, and all five parts held.** `src/kernels/divrem_u.c` (202 lines) and
> `src/kernels/divrem_s.c` (158) are on disk and green in both configurations. (a) FLAT: one
> `cq_sandwich`, peak `8W²+4W−1` measured at nine widths. (b) `fits` is `ucar[t][W]`, read by
> P3 and P4 directly; no `ult` wire exists in the kernel. (c) the quotient bit is one CX.
> (d) `r0_hi` is a real scratch register and iteration `t = 0` costs what every other
> iteration costs. (e) M19 contains no gate list: `cq_ult_step`, `cq_sub_step` and
> `cq_mux_step` do all the emitting, and the composition identity
> `compute = W·(2 + C_ult + C_sub + C_mux)` is asserted against what those three MEASURE at
> each width, which is the one L4 claim that survives `CQOPS_UPDATE_GOLDENS=1`.

> ### D11 — a global phase is only global until something controls it
>
> Four CELLS of §7's table act by emitting nothing — `Ry` at θ ≡ 2π on both columns, the
> half-turn's constant column, and the `Rz` constant column — and a FIFTH row folds while
> still emitting: the half-turn's qubit cell, whose `x` + `rz(π)` spelling realises the row
> only up to a global phase. (The two identity rows also emit nothing and are exempt:
> `controlled-I` is `I`.) Uncontrolled all of this is exactly right and it is what makes
> classical-mode testing possible (Rule 15). Under §9 **all five** are wrong. The four
> zero-gate cells are wrong in the same way and for the same reason:
> **`controlled-(e^{iα}·I)` is `Rz(α)` on the control
> wire**, because `Rz(α) = e^{−iα/2}·diag(1, e^{iα})` and the residue `e^{−iα/2}` is
> unconditional, hence genuinely global — including under nesting, since §9 ANDs nested
> controls into **one** wire before the region runs.
>
> **The corrections, per bit, with `b` the bit's value BEFORE the row acts and
> `k = round(θ/π)`:**
>
> | §7 row | column | phase `α` on the control wire |
> |---|---|---|
> | `Ry`, θ ≡ 2π (mod 4π) | either | `π` |
> | `Ry`, θ ≡ π (mod 4π), i.e. `k mod 4 == 1` | constant | `π·b` |
> | `Ry`, θ ≡ 3π (mod 4π), i.e. `k mod 4 == 3` | constant | `π·(1−b)` |
> | `Ry`, θ ≡ π (mod 4π), `k mod 4 == 1` | qubit | `−π/2` |
> | `Ry`, θ ≡ 3π (mod 4π), `k mod 4 == 3` | qubit | `+π/2` |
> | `Rz`, otherwise | constant | `(2b−1)·φ/2` |
>
> **THE QUBIT HALF-TURN SPLITS BY PARITY TOO, and an earlier draft of this table got it
> wrong in the one direction that matters.** It carried a single `π/2` "the `−i` of
> `Rz(π) = −i·Z`", which credits the residual entirely to `Rz(π)` and silently drops the
> `−1` of `Z·X = −Ry(π)` that §7's own callout states. Worked through: the emitted pair is
> `Rz(π)·X = Y = i·Ry(π)`, so promoting gate by gate gives `controlled-(i·Ry(π))`, which
> **overshoots** the wanted `controlled-Ry(π)` by `+i` — and `α` in this table is what is
> **EMITTED**, not the residual, so the control owes the negative. Hence `−π/2` at `k ≡ 1`,
> and `+π/2` at `k ≡ 3`, where the wanted operator is `Ry(3π) = −Ry(π)` and the sign flips.
>
> **The `Rz` constant row is what fixes that convention, and it is the only row that can.**
> Rows 1-3 are sign-degenerate — every `α` is `0` or `π`, and `−π ≡ π (mod 2π)` — so they
> read the same under either reading. `(2b−1)·φ/2` depends on both `sign(φ)` and `b`, so it
> is the sole non-degenerate discriminator. Verified numerically, all five rows, as 4×4
> matrices in §7's stated conventions.
>
> The two **general** rows need no phase at all: they promote exactly, with no residual,
> by the standard identity — which stays inside §8's frozen six entries, so the controlled
> axis needs no seventh either.
>
> ```
> controlled-Ry(θ) on (c,t) = Ry(θ/2)ₜ ; CX(c,t) ; Ry(−θ/2)ₜ ; CX(c,t)
> controlled-Rz(φ) on (c,t) = Rz(φ/2)ₜ ; CX(c,t) ; Rz(−φ/2)ₜ ; CX(c,t)
> ```
>
> **THE PHASE IS PER BIT, AND THAT IS NOT A STYLE CHOICE.** `cqrt_ry_i<W>` applies `Ry(θ)`
> to *every* qubit of the register, so a `W`-bit `Ry(2π)` contributes `(−1)^W` and the
> control-side operator is `Z^W` — a `Z` at i1 and **nothing** at i8/i16/i32/i64. `W`
> per-bit phases compose to `Rz(Wπ)` and get that right for free; **one `Z` per register is
> a miscompile at every even width.** Collapsing the `W` gates is a v2 peephole, excluded
> with every other gate-level optimisation.
>
> **§9 also grows a row 0, and it is what keeps every zero-cost claim in this document
> true.** A `CQ_BIT_ZERO` control skips the region entirely (0 gates, 0 qubits); a
> `CQ_BIT_ONE` control emits it **uncontrolled, verbatim**; only a `CQ_BIT_Q` control
> promotes. A classical control is a decision, not a circuit — the §3 fold table's own
> posture. So §11's L5 shapes, §7's "0 gates, 0 qubits" flip and §12's classical mode are
> all untouched by the existence of the controlled axis.
>
> **V1 REFUSES THE QUANTUM-CONTROL CASE RATHER THAN EMITTING IT, and that is the decision,
> not a gap.** The five phase terms above are hand-derived, and **this project has no
> instrument that can see a wrong phase**: the shadow models no phases at all, L1 compares
> values, `cq_mock_is_palindrome` is order-only, and a wrong angle inside an `rz` on a
> control is a *plausible* gate rather than a malformed one. Measured demand is **zero** —
> no controlled rotation appears anywhere in the 239 goldens. So M06 hard-errors at Step 20
> when a §7 fold row is reached under a `CQ_BIT_Q` control, at one greppable site, naming
> the row — the same loud-v1-boundary pattern the 884 floating-point aborts already use.
> The arithmetic is recorded here so that enabling it later is an implementation rather
> than a re-derivation, and so that a wrong sign cannot arrive silently in the meantime.
>
> **Three things Step 20 must carry with it.** (i) M21 discards `k mod 4`, so
> `CQ_ANGLE_HALF_TURN` cannot today distinguish `Ry(π)` from `Ry(3π)`; the split is owed
> before the `π·b` / `π·(1−b)` constant rows **or the `∓π/2` qubit rows** can be emitted, and
> it also matters for the `_inv` axis,
> since negating θ swaps `k ≡ 1 ↔ 3` while fixing `0` and `2`. (ii) The constant column's
> *flip* needs no new mechanism at all: `cq_emit_x` on a constant target is already the
> zero-gate flip and `cq_emit_cx` with a quantum control and a constant target already
> materialises and emits the `CX`, so the moment M06 promotes `cq_emit_x` that half is
> correct — **provided M22 spelled the flip `cq_emit_x` and not `cq_bit_flip_const`**, which
> it does. (iii) That last correctness is contingent on `bd skh` resolving `cq_materialise`'s
> own `X` as **unpromoted**; if it resolves the other way the chain breaks silently, so D11
> does **not** settle `skh` by implication.
>
> **What was rejected, and why.** *Refusing to fold inside a controlled region* — always
> taking §7's general row — is sound and needs no phase arithmetic, and it is the
> conservative direction M21 argues for everywhere else. It loses on two counts. It cannot
> be expressed without M22 knowing it is inside a controlled region, which is the one thing
> plan §0.3 keeps out of every module above the emitter; and on the `Ry` side it
> *materialises* a rail the pass expects to stay classical, so a controlled `Ry` on an
> all-classical i64 rail would take 64 qubits and 64 poisoned shadow entries where the
> correct answer is one gate on the control — and a poisoned rail is `ckd.18`'s hard error
> at `cqrt_free`. A **seventh vtable entry** (`sink.z` / `sink.phase`) was rejected outright:
> it forks a frozen ABI for something `Rz(π)` already expresses (`lk0`).

> ### D13 — materialisation changes a bit's ENCODING, and an encoding is not conditional
>
> `cq_materialise` emits its `X` straight to the sink rather than through `cq_emit_x`
> (`src/emit.c`), which was recorded at Step 6 as the *conservative* choice and explicitly
> not a settled one: once M06 makes `cq_emit_x` promote, routing through it would emit
> `CX(ctrl, fresh)` instead of `X(fresh)` and leave the new wire entangled with the control.
> `bd skh` framed that as a genuine trade — "the whole routine should be a no-op when the
> control is clear" against "materialisation is a pure representation change".
>
> **It is not a trade. The algebra decides it, and the bead's own framing of the tension is
> false.** Let `b` be the rail's classical value before the region, `c` an inner control and
> `k` the control branch. The required semantics is `b ⊕ (k ∧ c)`.
>
> | | fresh wire holds | after the promoted `CX → CCX` |
> |---|---|---|
> | **unpromoted** | `b` | `b ⊕ (k ∧ c)` — exactly the requirement |
> | promoted | `k ∧ b` | `k ∧ (b ⊕ c)` |
>
> The two agree at `b = 0` everywhere and at `b = 1, k = 1`. They disagree in exactly one
> cell — **`b = 1, k = 0`** — where the promoted choice yields 0 and the rail must still
> read 1. That is the `ctrl = 0` branch, which is the branch row 0 exists to leave alone.
>
> **"A side effect when the control is clear" is the wrong reading of what changes.**
> Unpromoted materialisation changes the rail's REPRESENTATION — constant → qubit, so the
> rail now owns a qubit it did not (I4) — while leaving its VALUE identical on both
> branches. The promoted choice is the one with a state-visible effect, and it is a value
> corruption. The "no-op when clear" premise is delivered at a different layer and only for
> a classical control: row 0 skips the whole region for a `CQ_BIT_ZERO` one, so nothing is
> materialised at all. For a genuinely quantum control there is no clear-control *case*,
> only a branch — and on that branch the rail must still read its old value, which means it
> must become a wire, because `k` is not known at emit time. Corroboration from the frozen
> ABI: `cqrt_alloc` has no `_controlled` axis at any width, so "allocate under control" is
> not an operation CQ_lang can ask for.
>
> **It has a shipped witness, so it is not hypothetical.**
> `slice_control_cond_onward_phase.expected.log` (and `slice_control_switch_datamux`) copy a
> rail born `10` into one born `3` under a quantum control, and `10 & 3` has bit 1 set in
> **both** — a constant-ONE target materialised inside a controlled region.
>
> **What can and cannot falsify it.** Not a gate count: both choices emit exactly two gates
> and differ only in the KIND of the first. Not the L4 goldens: they are pinned at the
> all-quantum mask, where `dst` is minted all-ZERO and no materialising `X` occurs at all.
> Not "every Phase-B kernel green under `cq_ctrl_push`" *at the all-quantum mask*, for the
> same reason. The fixture that decides it is a **constant-ONE target under a control whose
> shadow is ZERO**, asserted on the resulting VALUE — at ctrl shadow 1 the two choices
> agree. `tests/test_controlled_region.inc:materialisation_is_not_promoted` is that fixture,
> and the driver's `CQ_KD_CTRL_Q0` row reaches the same cell through every kernel.
>
> ### D14 — `_inv` is `f⁻¹`, and `f⁻¹` does not exist

> **Resolved 2026-08-21 at Step 21, against CQ_lang at the pinned revision and against all
> 239 `tests/e2e/*.expected.log` goldens. An adversarial review of the first draft of this
> decision found its stated ground false in three places; what survives is stronger, and the
> answer did not change.**
>
> **THE SUFFIX NAMES TWO UNRELATED THINGS.** On the controlled **rotations**, `_inv` means
> *negate θ*: `cqrt_ry_<W>_controlled_inv` (7 widths — the gap is real, `docs/cqrt_census.txt`
> D2) and `cqrt_rz_<W>_controlled_inv` (9). That is the sense plan §4's M21 row and
> CLAUDE.md's `bd fna` paragraph use. On the **data templates** it is a fresh-minting inverse
> *operation*: expanding `opcode_table.yaml`'s own naming rules (`:39-44`) gives **915**
> `cq_template_*_inv` symbols in six suffix shapes, **603** of them purely-integer.
>
> **NEITHER FAMILY IS EMITTED, AND THE FIRST DRAFT OF THIS ROW SAID THE ROTATION ONE WAS.**
> It is not. `ir-pass/src/ControlledSymbols.cpp:32` hard-codes the stem `"cqrt_rz_"` and can
> never produce a `cqrt_ry_*` name at all; its `inverse` parameter defaults to false and its
> one live caller, `RotationLowering.cpp:158`, takes the default. Nothing in `ir-pass/`
> constructs a `cqrt_ry_…_controlled` name (`grep '"cqrt_ry_'` → only `WidthDispatch.cpp:393`,
> the uncontrolled forward). Measured: **0** `_inv` lines and **0** `cqrt_*_controlled*`
> rotation calls in the corpus, against 21,323 `cq_template_icmp_*_unc`. §7 at `:694-699`
> already recorded that no controlled rotation is emitted today; the first draft of this note
> contradicted it, which is the `src/angle.h` failure mode — a corpus claim in shipped prose
> that was never measured — reproduced inside the decision written to fix one.
>
> **SO LIVENESS CANNOT BE THE GROUND, and the same evidence proves it: the data-template
> `_controlled` grid is also declared-and-uncalled** — 0 `cq_template_*_controlled*` calls in
> the corpus, 214 purely-integer symbols — and Rule 9/M06 serves every one of them. "Declared
> + 0 corpus calls ⇒ abort" applied consistently would abort the axis Step 20 just built.
>
> **THE GROUND IS SPECIFIABILITY, AND IT IS PERMANENT RATHER THAN A v1 CONVENIENCE.**
> `_inv` is the inverse OPERATION. `runtime/cq_templates.h:49` declares
> `int32_t cq_template_add_i32_hl_inv(int32_t a_handle, int32_t b_classical)` — it names no
> rail and returns a fresh handle (`cq_templates.c:247-252` calls `cqrt_alloc_handle()`) — and
> `ir-pass/test/lowering_invert_add_i32.ll:25` pins a bracketed `add %a, 2` as lowering to
> `cq_template_add_i32_hl_inv(%a, 2)`, i.e. **subtract 2**. Three tiers follow, and only the
> first is a v1 statement:
>
> 1. **Non-injective opcodes have no inverse at all.** `and` (28 `_inv` symbols), `or` (28),
>    `udiv` (36), `trunc` (20) and every `icmp` predicate (12 each) are not injective, so
>    `f⁻¹` does not exist. The yaml generates the symbols mechanically anyway.
> 2. **Where an inverse exists it is `f⁻¹`, not `f`.** An earlier draft of this note said
>    "defining `_inv` as the forward, which for `dst ^= f` it numerically is" — that is wrong
>    at this ABI and is the sentence most likely to be quoted back as licence. It would ship
>    `and_i32_inv = and_i32`, a wrong **value**, not merely a misleading name.
> 3. **Where `f⁻¹ == f` — the compares, by exactly the self-inverse property
>    `tests/test_unc_contract.inc` now pins — it is still a guaranteed rail leak**: a fresh
>    flag rail holding `pred(a,b)`, never zero, never freeable. That is CQ_lang's bd `8txa`
>    read from our side, and it is why "define it as the forward" is refused even where it is
>    arithmetically exact.
>
> **THE DECISION: the 603 integer data-template `_inv` bodies get a loud abort naming the
> symbol.** The link succeeds, the boundary is visible at runtime, and Step 24 cannot reach
> it. `bd w1c` carries the mechanism.
>
> **AND THE `cqrt_h` PRECEDENT AT §1 CUTS THE OTHER WAY, so it has to be distinguished
> rather than ignored.** §1 says of `cqrt_h`: *"if CQ_lang ever turns on emission, a link
> error is the failure mode we want — it fires at build time, names the symbol, and cannot
> produce a wrong circuit. Do not paper over it with a shim stub."* Two facts separate them.
> `cqrt_h` is two hand-written M26 symbols **outside** the generated grid, so declining to
> define it is a decision at one site; the 603 sit **inside** a grid M27 generates
> mechanically, and carving an exception into the generator is the "never hand-write or fork
> the shim" hazard. And CQ_lang ships `runtime/cq_link_smoke.c`, which *"takes the address of
> every generated `cq_template_*` symbol so the linker MUST resolve each one"* — so for any
> consumer that includes it the undefined-reference wall is real rather than hypothetical.
>
> **THE GATE IS STEP 22, NOT STEP 23.** An uncalled symbol produces no undefined reference,
> and the corpus calls zero `_inv`, so plan §4's Step 23 row (*"`nm` shows no undefined
> `cq_template_*` from the opcode grid"*) could not go red if the 603 were simply missing.
> What would fail is Step 22's symbol-count clause — which then read *"emitted symbol
> count reconciles with Step 0.5"* and was **reworded on 2026-08-22** to name the partition
> outright (2479 = 992 wrappers + 603 `_inv` aborts + 884 fp aborts, `opcode_table.yaml`
> only), because Step 0.5's row named the ~~1474~~ phantom and a gate may not delegate its
> numbers to a question. Step 22's
> other clause needs correcting too: the abort bucket is no longer 884 fp but **884 fp + 603
> integer**, and the integer grid is **992 wrappers + 603 aborts**, not 1595 wrappers. Step 22
> should assert that the integer abort set is **exactly** the `_inv` symbols, so nothing else
> is swept into it unobserved — the corpus cannot notice, since it calls neither `_inv` nor
> `_controlled`.
>
> **WHAT STEP 21 OWNS OF THIS.** The decision, and the one thing about `_inv` that is
> *testable today*: the property the compare sub-family rests on. `flag ^= pred(a,b)` is a
> self-inverse permutation of the flag rail **at any entry value**, which is why
> recompute-and-XOR strips it (`opcode_table.yaml:216-218`) — and L3 only ever exercised it
> from zero, because `cq_kd_case` mints `dst` with `cq_reg_alloc_zero`.
> `tests/test_unc_contract.inc` runs all ten predicates from a flag that already holds 1, and
> asserts the intermediate value rather than only the involution.
>
> **THE TRAP, AND IT IS UPSTREAM'S DESIGN-OF-RECORD RATHER THAN A FIXTURE COMMENT.**
> `docs/backend.md:841-843` still reads *"the `_inv` symbols are generated but no longer
> emitted by the spine … (the icmp/fcmp `_inv` strips are still emitted directly by
> `CompareLowering` for Phase-4 control flags)"* — which is §10's retired sentence, nearly
> verbatim, still shipped. `backend.md:836` is stale the same way. A fixture comment carries
> the same claim (`ir-pass/test/control_swap_select_qqcond_ule.ll:106-116`, whose executable
> `CHECK-NEXT` at `:155` says `_unc`), but citing the design-of-record is what makes "Rule 16
> applies to upstream prose exactly as it applies to ours" land.
>
> **Reproducing the measurements.** `grep -c` prints a count PER FILE over 239 files and does
> not answer the question; the forms that do are
> `grep -h '_inv' tests/e2e/*.expected.log | wc -l` → **0** and
> `grep -ho 'cq_template_icmp_[a-z0-9_]*_unc' tests/e2e/*.expected.log | wc -l` → **21,323**.
> The grid expansion is PyYAML over `opcode_table.yaml` applying `:39-44`'s rules with the
> `predicates` sets and the cast pair lists; it totals **2479 = 1595 int + 884 fp**, which is
> the cross-check that it is faithful to §1's published partition.

> ### D12 — a diagonal rotation does not poison, and that keeps the shadow exact
>
> `cq_shadow_rotate` poisons unconditionally and is the **only** producer of `unknown`
> (§10), so through Step 18 every shadow entry is determinate and `cq_shadow_known_zero` is
> exact rather than conservative. M22 ends that — but only for the rows that genuinely
> earn it.
>
> **The rule: a row poisons iff it can move a computational-basis value off a basis state.**
> Only `Ry` at an angle off the π-lattice can. Every `Rz` is diagonal, and the `Z` of the
> θ ≡ π row is diagonal, and a diagonal gate maps `|v⟩ → e^{iα}|v⟩`: the basis value is
> unchanged. If the shadow says an entry is determinate then — by its own one-way
> discipline — the qubit really is in a definite basis state, hence unentangled, so that
> `e^{iα}` factors out of the whole state and the entry stays **correct**. The θ ≡ π row is
> `X` followed by a diagonal, so it takes `X`'s rule: `cq_shadow_x`, which flips a known
> value and is already a no-op under existing poison.
>
> **This is not D6 and does not reopen it.** D6 is the fold table *reading* a shadow to
> decide which gate to emit; D12 is which shadow update a given gate *implies*, which is
> §3's rule table and has always been per-gate. No gate count depends on it, no L4 golden
> moves, and "kind, never shadow" is untouched.
>
> **Measured payoff — and the corollary this note used to draw was FALSE, corrected
> 2026-08-22 when D15 measured it.** What D12 buys is real and is a property of the SHADOW:
> a rail that met only a diagonal row keeps a determinate entry, which is what makes
> `cq_pc_zero_proof_rotation_free` exact across Steps 10–17, and `Ry(π)` on a qubit likewise
> keeps its rail freeable.
>
> **What it does NOT buy is the corpus's `rz`-rooted frees, and this note asserted that it
> did.** The retired text read: *"the corpus's twelve `rz`-rooted rails are
> `alloc(0); cswap(qflag,·,tmp); rz(tmp,φ); cswap; free` … under D12 the `cswap` involution
> restores their shadow to zero and they free cleanly."* Measured: **all 30 rails whose last
> rotation is an `rz` are unprovable by the shadow.** `cq_shadow_ccx`'s
> `t.unknown |= a.unknown | b.unknown` carries the **swapped-in DATA rail's** poison into
> `tmp` whatever the flag holds, and in the corpus that data rail is always the
> generally-rotated one. With `cqrt_cswap` correctly modelled as writing **both** data args
> there are **0** `cqrt_rz`-rooted frees in the last-write classification at all — the
> "twelve" were an artefact of the same modelling error that produced the historical "37".
> Those rails are discharged by **D15**'s U2 reduction, not by D12.
>
> The test that appeared to pin the retired claim,
> `tests/test_rotate.c:an_rz_in_a_cswap_bracket_on_determinate_operands_frees_cleanly`
> (renamed 2026-08-22 — its old name asserted the refuted claim, and a name outlives a
> comment), mints **both** operands determinate — a state the corpus never has — and says so itself: *"the shadow
> tracks both exactly, because nothing here poisoned."* It reproduces the SHAPE and not the
> STATE. **D12 is unaffected; only its stated corpus payoff was wrong.**
>
> **The honest limit.** The argument is *derived* from "a determinate `value` means a
> definite computational-basis value", which holds today because every gate below Layer 4 is
> a basis permutation; it is not measured, and Rule 13 forbids the simulator that would
> measure it. What bounds the risk is that §9's controlled axis cannot break it either — a
> controlled diagonal puts a *relative* phase on the **control**, whose own basis value is
> likewise unmoved — and that D11 refuses the quantum-controlled fold rows outright.

> ### D15 — the proof is CQ_lang's; ours is a certificate, and what it cannot cover is STRANDED rather than recycled
>
> **Resolved 2026-08-22, closing `bd c1a` (`ckd.17b`), `bd ckd.18` and `bd 2cf`. The first
> draft of this decision released the residue to the free list and an adversarial review
> refuted it the same day; the reversal is recorded in §3 because the distinction it turns
> on is the whole of I3 and is easy to lose again.**
>
> > **BUILT 2026-08-27 AT STEP 23 LANDING 2, AND THREE THINGS THE BUILD SETTLED THAT THIS
> > DECISION LEFT OPEN. `bd 06t` is discharged.** The free path landed at landing 1 (§3);
> > landing 2 added the residue split and the certificate. `cqrt_free` installs
> > `cq_shim_free_proof` — the certificate AND the shadow, **DIRTY dominating, then CLEAN,
> > then UNPROVEN** — because both are sound in all three rows and differ only in
> > COMPLETENESS, so either one's proof suffices and either one's conviction stands. A
> > best-evidence-first rule would have made the answer depend on consultation order.
> >
> > **(i) RULE 1 WAS NOT SATISFIED FOR THE PORT THIS SECTION MANDATES, and nobody had
> > checked.** §2 says "port the reduction, do not re-derive the parity" and names four
> > upstream guards. Measured before any code: **none of those four strings appeared anywhere
> > under `third_party/`** — they are in CQ_lang, which this repository does not pin. Resolved
> > by vendoring `tools/free_pairing_check.py` into `third_party/cq_free_pairing/` at a pinned
> > revision with its own COMMIT and a **configure-time** sha guard
> > (`cmake/CqopsFreePairingPin.cmake`), in its own directory so that no byte of any existing
> > `third_party/` file changed. Reading the pin paid immediately: upstream's flag parity is
> > masked **`& 1`** and is frozen on **control slots only**, neither of which is derivable
> > from the four names, and getting either wrong makes the certificate discharge almost
> > nothing.
> >
> > **(ii) ONE ENGINE IS BUILT AS ONE ENGINE, and the mechanism is a RECORDING choice rather
> > than a rule shape.** The `cq_template_*` FORWARD is recorded as a WRITE to the rail it
> > mints, with its `_unc` as the declared TWIN. So U1 needs no matching step of its own: the
> > same `reduces_to_identity` that serves U2 pairs a forward with its uncompute, and upstream's
> > T2 (operands unchanged between the halves) falls out of `pair_operands_unchanged`'s READS
> > loop rather than being a rule. U3 is the classical-immediate fast path and is the only
> > entry condition that does not call the engine at all.
> >
> > **(iii) THE BIRTH VALUE IS WHAT MAKES §4's CARVE-OUT A MECHANISM RATHER THAN A CASE, and it
> > is the port's one deliberate divergence from upstream's OBLIGATION.** Upstream's rules prove
> > "a known classical basis STATE, unentangled" — its A-rules explicitly accept a rail
> > returning to its ALLOC-TIME value `v`, because ITS free is a reset that collapses nothing on
> > a basis state. Rule 6 needs `|0⟩`. **A faithful transcription would have handed `|v⟩`
> > indices to the free list.** Carrying the birth value alongside the reduction fixes that AND
> > produces §4's conviction for free: a reduced history returns the rail to what it was MINTED
> > holding, so a template rail (born `|0⟩`) clears and `alloc_i32(5); ry(θ); ry(−θ)` — the same
> > reduction, the same engine — is CONVICTED. §4's carve-out and §3's residue split are the
> > same fact read twice.
> >
> > **AND TWO PLACES THE PORT IS DELIBERATELY *NOT* UPSTREAM, both recorded at
> > `shim/cq_shim_reduce.h`.** Upstream's **R1** needs whole-function look-ahead that does not
> > exist when `cqrt_free` arrives; dropping it would WIDEN us, so it is replaced by a
> > **past-only** witness — a rail carrying a non-diagonal rotation that was READ after it is
> > UNPROVEN — which is strictly stronger than R1 and additionally closes the hole §5
> > demonstrates upstream leaving open. And upstream's escalation of "minted, no reversal" to a
> > VIOLATION is **not inherited**: it is sound for upstream's obligation and would be an
> > over-claim for ours, where a conviction means the library can SEE the rail is not `|0⟩`.
> > That shape answers UNPROVEN here. The ACT is identical either way; what would have been
> > damaged is the residue split.
>
> #### 0. Corpus figures in this decision are PINNED AND ALREADY MOVING
>
> Figures below are pinned **where stated**, and **where a figure predates the pin it says
> so** — §2's "51,651 of 51,696" and §4's "25 … at the measured revision" are both
> pre-`397c67c`, and §6(ii)'s counterfactual rows are §10's pre-`397c67c` classification.
> The pin is CQ_lang at **`397c67c`**, where `tests/e2e` holds **240** goldens and **51,698**
> `cqrt_free` calls. **The CQ_lang corpus is not frozen and
> nothing in this repo pins it** — `third_party/bennett/COMMIT` has no counterpart on the
> CQ_lang side. Measured within one session on 2026-08-22 the corpus went 239 → 240 → 241
> fixtures and 51,696 → 51,698 → 51,705 frees, and a **26th** `ckd.18`-shaped rail appeared
> in `slice_intrinsic_ctlz.expected.log`. Treat every count here as an ORDER OF MAGNITUDE
> and a RATIO; re-measure before quoting one, and never let a golden-derived figure become a
> gate without a pin beside it.
>
> > **AMENDED 2026-09-10 (`bd 1ti`) BY RE-READING THE MEASUREMENT.** The enumeration above is
> > kept: it is a dated measurement and it is exactly right as far as it goes. Two
> > corrections and one resolution.
> >
> > **(a) THE FIRST SENTENCE READ "Everything measured below is against CQ_lang at
> > `397c67c`, where `tests/e2e` holds 241 goldens and 51,705 `cqrt_free` calls" UNTIL THIS
> > AMENDMENT, AND BOTH HALVES WERE WRONG.** The blanket did not hold — §2 and §4 already
> > quoted pre-`397c67c` figures when it was written, which is the second half of what
> > `bd 1ti` filed. And **241 / 51,705 is `7385c4d`**, the NEXT CQ_lang commit, 44 minutes
> > later; at `397c67c` itself it is **240 / 51,698**. **A SCOPE SENTENCE IS CORRECTED; A
> > MEASUREMENT IS AMENDED** — which is why this differs from **D20**'s `7,300` one section
> > up, a derived figure inside a dated narrative that is amended in place rather than
> > rewritten. `397c67c` is an immutable object, so the pair was never an aged measurement:
> > it was attached to the wrong revision from the start. **The pin's IDENTITY was never in
> > doubt** and §4 independently confirms it: the `ckd.18` shape — a rail born from a
> > non-zero literal that takes exactly two `cqrt_ry` and is then freed — counts
> > **25 / 26 / 27** at `1fa1573` / `397c67c` / `7385c4d`, and §4 says 25 at the measured
> > revision and 26 at `397c67c`. Only the pair of numbers was off by one commit, which is
> > why they are corrected in place rather than re-pinned.
> >
> > **(b) THE ENUMERATION STOPS ONE COMMIT EARLY.** Re-measured over CQ_lang's own history
> > (`git log --date=iso -- tests/e2e` across 2026-08-21..23, then, per revision,
> > `git ls-tree -r --name-only <rev> -- tests/e2e | grep -c '\.expected\.log$'` and
> > `grep -c '^cqrt_free('` summed over those files — one `cqrt_free(hN)` per line, the only
> > `free`-bearing token in the corpus):
> >
> > | CQ_lang rev | 2026-08-22 | goldens | `cqrt_free` | golden-set change |
> > |---|---|---|---|---|
> > | `1fa1573` | 11:28 | 239 | 51,696 | *(the day's starting state, inherited from 08-20)* |
> > | `397c67c` | 12:09 | 240 | 51,698 | `+slice_intrinsic_ctlz` |
> > | `7385c4d` | 12:53 | 241 | 51,705 | `+slice_ctlz_idiom_loop` |
> > | `dc8f6c6` | 14:18 | **243** | **51,737** | `+slice_libm_real_ilogb{,f}` |
> > | `5caaaf8` | 16:00 | 243 | 51,737 | none — a `.c` fixture only |
> >
> > Every transition is a pure ADDITION; not one golden was modified. The row the paragraph
> > above lacks is `dc8f6c6`, three hours after the pin.
> >
> > **(c) "THE CORPUS MOVED FOUR TIMES ON 2026-08-22 ALONE" IS TRUE OF THE DIRECTORY AND NOT
> > OF THE GOLDENS.** That claim — carried by `src/reg.h`, `tests/test_unc_asym.inc` and
> > `bd 06t`, all three pointing here, and filed by `bd 1ti` as underivable from this
> > section — counts COMMITS: `tests/e2e` took four distinct tree states that day
> > (`git ls-tree <rev> -- tests/e2e`: `27a295` → `44e05a` → `4b1731` → `e206f3`, from
> > `c66df1` on 08-20). Only **THREE** of the four changed the golden set or the free count;
> > the fourth edited a `.c` fixture and left every golden byte-identical. So the reading is
> > `bd 1ti`'s (b) — **this section's enumeration was incomplete, not the citers'
> > arithmetic** — and the honest number beside a GOLDEN-derived figure is **three**. The
> > two IN-TREE citers now say so; **`bd 06t`'s description is left as the historical record**
> > and carries a dated appended note instead.
> >
> > **AND `CLAUDE.md`, WHICH `bd 1ti` NAMES AS THE FOURTH CITER, NEVER CARRIED THE STRING —
> > do not go hunting for it.** `git log -S 'moved four' -- CLAUDE.md` is empty over the whole
> > history, and the only `four times` that file ever held was an unrelated remark about
> > timing spread on an unchanged tree (`bd 97s`), itself since removed.
> >
> > **AND THE POINT OF THIS SECTION IS MADE BY HOW FAR IT HAS RUN SINCE — INCLUDING WHILE
> > THIS NOTE WAS BEING WRITTEN.** At CQ_lang `170ede1`, the revision `bd w9i` re-vendored the
> > ABI at and HEAD when this was first measured, `tests/e2e` holds **323** goldens and
> > **60,376** `cqrt_free` calls — **+83 goldens and +8,678 frees** against the pin, measured
> > two independent ways (`git show` per path, and `git archive` into a scratch tree) with
> > identical results. **CQ_lang then moved twice more the same afternoon**: `a34cd22` (14:53)
> > and `8a8f8cf` (15:06, which landed mid-measurement), both **332** goldens and **60,382**
> > frees, i.e. **+92 / +8,684** against the pin. **Read none of these three as "HEAD".**
> >
> > **AND §4's CORPUS WITNESS HAS GONE TO ZERO FOR A REASON THAT IS NOT THE OBVIOUS ONE.** At
> > `170ede1` the `ckd.18` shape counts **zero** — but the rotations did not stop. Measured
> > over all widths, classifying a birth by the literal's TEXT: **3,146** rails are born from
> > a non-zero literal, **383** of them take at least one `cqrt_ry`, **20** take exactly two,
> > and **9** of those twenty are a cancelling pair. What has changed is the FREE: of the
> > **94** non-zero-born rails that are freed at all, **not one** takes any rotation. **The
> > shape's population is zero because the rotated rails are never freed**, which is a
> > different fact from the corpus having stopped rotating, and only the second would let §4
> > be retired. §2's five `slice_loop_break*` fixtures are meanwhile down to **four**.
> >
> > **A PARSING TRAP, RECORDED BECAUSE THIS NOTE FELL INTO IT.** An earlier draft said "no
> > rail born from a non-zero literal takes any `cqrt_ry` at all, though the corpus still has
> > 950 such allocs" — **both halves wrong, from ONE cause**: the census classified the birth
> > literal with awk's numeric coercion, and awk silently evaluates the corpus's hex-float
> > literals (`cqrt_alloc_f64(-0x1.921fb54442d18p+0)`) to **zero**, so every float-born rail
> > was counted as born-zero and dropped. `950` is that undercount; the text test gives 3,146.
> > The integer figures were never affected — `cqrt_alloc_i1` is 26,712 lines all spelling
> > `0`, and non-zero integer births are 402 (370 of them `i32`, against 966 `i32` allocs) —
> > **which is exactly why the error survived a plausibility check.** Classify a literal by
> > its text; the total `cqrt_ry` line count, **654**, is unaffected and reproduces.
> > Re-measure; do not carry a number out of this section.
>
> #### 1. The `|0⟩` obligation is upstream's, and it is stated upstream
>
> Every earlier draft assumed the requirement that a freed rail be provably `|0⟩` was
> **ours** — `ckd.18`'s note says so in terms ("the requirement … is OURS (Rule 6, I3)") on
> the ground that `cq_runtime.h:40-41` calls `cqrt_free` a "width-irrelevant lifetime hook;
> declared but unused" and states no precondition. **That ground is false.** The
> precondition is stated, in CQ_lang's own design-of-record, as a non-negotiable principle:
>
> > **No added measurements — ever.** … Its one teeth: `cqrt_free` is emitted **only on a
> > proven-`|0⟩` rail** — freeing a dirty/entangled rail is a hidden reset = measurement
> > that collapses `q`. A rail not provably `|0⟩` is **left allocated** (a qubit "leak,"
> > accepted in the qubits-not-scarce regime), never freed. This is precisely what the
> > struck "v1" got wrong — it freed dirty rails; the fix is *don't free dirty*, not *free
> > carefully*. — `CQ_lang/ROADMAP.md:381-386`
>
> And CQ_lang states the limit from the other side, in `run_slice.sh` — one of TWO runners,
> and the check runs unconditionally in both — **stage 2b** in `run_slice.sh:65`, and the
> identically-placed **Stage 3b** in `run_slice_multi.sh:61`, which numbers its stages
> differently; "stage 2b" below is `run_slice.sh`'s name for it:
> *"The golden byte-diff cannot witness 'freed at `|0⟩`' — **no trace label can say whether
> a rail was ACTUALLY `|0⟩` at the free, at any ABI**"* (`tests/e2e/run_slice.sh:10-13`). It
> therefore discharges Rule 6 **at the IR layer**, in `tools/free_pairing_check.py`, run as
> stage 2b of every slice, where the rail identity is an SSA name and the loop algebra is
> still present.
>
> This is NORTH_STAR's layering stated by the other side of the interface. **v1 does not
> duplicate a proof that cannot be performed at our layer**, and Rule 13 forecloses the only
> instrument that could. What v1 owes is the *operation* — that `f` followed by `f` returns
> the rail to `|0⟩`. That is Rule 7's involution, and `cq_kd_case`'s L3 re-runs the same
> kernel on every case of every kernel under each of §9's four regions, asserts `dst == 0`,
> frees, and asserts the pool came back.
>
> #### 2. The evidence is an OBSERVED UNDO CERTIFICATE, not the shadow
>
> **The shadow cannot be the free-time oracle, and that is now a measurement.**
> `cq_shadow_rotate` is the only PRODUCER of `unknown` — `cq_shadow_cx`/`ccx` write the byte
> too, but only ever propagate what is already there — and `cq_shadow_cx` carries it
> control→target with no clearing path — the direction a kernel's sources travel — so on
> **every free that reaches the shadow at all it reports `unknown`**: the shadow discharges
> **0** and convicts **0**.
>
> **The vacuity in that sentence is worth stating, because the first draft hid it.** ~45
> frees never reach the shadow: their rails are all-constant at the free, so by **I4** they
> own no qubits and `cq_reg_clean` (`src/reg.c:189-199`) skips every bit without invoking the
> proof. Those are discharged today, with a NULL proof and no certificate — they are the
> complement of the recorded "51,651 of 51,696 rotation-tainted", 9 in each of the five
> `slice_loop_break*` fixtures, and they are U3's population plus one all-classical compare
> flag. **Poison is not what silences the shadow there; I4 is.** The shadow stays exact, and
> stays used, on the rotation-free kernel surface; it is empty at L6 for everything that
> owns a qubit.
>
> > **DATED NOTE, 2026-09-10 (`bd 1ti`): THE FOUR ESTIMATES OF THAT `~45` DO NOT RECONCILE,
> > AND THE SENTENCE IS LEFT STANDING RATHER THAN REPAIRED.** `51,696 − 51,651 = 45` and
> > `9 × 5 = 45` agree; **U3's population is 40 in this section's own table, and `40 + 1 = 41`,
> > not 45.** One of the four is wrong and nothing here says which. **The figure PREDATES
> > `397c67c`** — the `51,696` total dates it, and re-measurement puts that total at CQ_lang
> > `1fa1573` (2026-08-22 11:28), the commit before the pin (§0's table).
> >
> > **WHAT WAS RE-CHECKED AND WHAT COULD NOT BE.** From the goldens alone at `1fa1573`: the
> > total **51,696** reproduces exactly, and there really are **five** `slice_loop_break*`
> > goldens there (52 / 44 / 44 / 44 / 44 = 228 frees between them, so "9 in each" is 9 OF
> > each fixture's frees, not each fixture's total, and every one of the five can supply 9).
> > The other three estimates — the `51,651` rotation-tainted count, the per-fixture 9, and
> > U3's 40 — are **runtime classifications the shadow and the certificate perform over the
> > call stream**, not properties of the golden text, so re-deriving any of them means
> > running L6. **L6 was not run for this note** (Rule 17), and no reconciliation is invented
> > here. Whoever next runs the certificate against a pinned CQ_lang revision should
> > re-measure all four together and replace the sentence outright — and note that §0's
> > amendment records the corpus is down to **four** `slice_loop_break*` fixtures at HEAD, so
> > the `9 × 5` decomposition has already lost its fifth term.
>
> What M26 *can* see is the call stream. The certificate is a per-handle record — **not a
> gate list** (Rule 13): the rail's producer, its operand handles, a per-handle write epoch,
> and the active §9 control context. **Its size is bounded by the live-handle count and is
> small — tens of bytes per live handle against a corpus peak of ~1,165 simultaneously-live
> rails — but no byte figure here has been reconstructed twice, so derive it from the record
> you actually build rather than quoting one.** Three rules, measured disjoint:
>
> | | Rule | Frees | Why the rail is `\|0⟩` |
> |---|---|---|---|
> | **U1** | rail minted by `cq_template_F(srcs) -> R`; a later `cq_template_F_unc(R, srcs)` names it with the same handles; **`R`'s write history between the two, and every source's, itself REDUCES TO THE IDENTITY** (not merely "is empty" — see below); and both halves ran under the same §9 control context | ~25,110 | `dst ^= f` is an involution and both applications see the same operand values, so the net effect on `R` is the identity |
> | **U2** | every write to `R` since it was minted reduces to the identity: applications of a self-inverse gate pair off, **and every operand of a pair is unwritten between its two halves, and every write between them commutes with it** | ~26,419 | A self-inverse gate applied twice on unchanged operands is the identity |
> | **U3** | born `cqrt_alloc_W(L)`, `sum` of `cqrt_addc` immediates is `−L`, no other write | 40 | Upstream's own rule (A1): a known classical state plus a classical offset |
>
> **Measured: ~99.75% of frees, zero overlap, residue ~127** — falling to ~54 under
> upstream's own relaxations (a diagonal `Rz` is transparent, which is **D12**, and
> `Ry(θ)/Ry(−θ)` is an adjoint pair), of which **~50 are in the five `slice_loop_break*`
> fixtures**. See §3's warning about exactly those five.
>
> **THE RESIDUE IS NOT ONE POPULATION AND §3 TREATS ITS HALVES OPPOSITELY.** Some of it is
> `ckd.18`-shaped — the certificate reads enough to convict, so those rails are
> **proven dirty** (§4). The rest is genuinely evidence-free and is **stranded** (§3). An
> implementation that reports a single residue figure has not yet decided which row each
> free lands on, and that split is the first thing bd `06t` owes.
>
> > **U2'S PARITY CLAUSE IS NOT SUFFICIENT AND THE FIRST DRAFT OMITTED THE REST OF IT.**
> > Written as a bare *"even number of applications of the same self-inverse gate on the same
> > operands"*, U2 states a **parity** where soundness needs a **nested reduction with
> > operand stability**. Measured against a tracker built literally from that text: **82
> > rails in the shipped corpus are discharged on a premise the trace violates** — 474
> > intervening writes, all `cqrt_copy_<W>_controlled` whose control flag was flipped by
> > `cqrt_x` in between, plus 10 `cqrt_cswap` whose swap partner moved. And the ORDER matters
> > independently of the parity: reordering a reverse pass from `A B B A` to `A B A B` leaves
> > every quantity the parity rule inspects bit-identical while leaving the rail at
> > `h1 ⊕ h2`. **Upstream had this exact bug and fixed it on 2026-08-07**, with four
> > dedicated guards (`pair_operands_unchanged`, `co_written_stable`, `commutes`,
> > `unchanged_over`) after a live miscompile in which `cswap/ry/cswap/free` reported
> > `A2-reduces-to-identity` at exit 0 with `Ry(0.75)|0⟩` parked on the freed rail
> > (`free_pairing_check.py:96-120`, `:197-209`). **Port the reduction, do not re-derive the
> > parity** — Rule 1's posture applied to an analysis rather than a circuit.
> >
> > **And U2's "since it was minted" must not be read as "since its `cqrt_alloc`".**
> > `cqrt_qram_load_<W>` mints a rail with no allocator anywhere in the trace, holding a cell
> > value and entangled with a quantum index; 11 shipped frees are on such rails.
>
> > **U1 CONTAINS U2'S REDUCTION — THERE IS ONE ENGINE, NOT THREE RULES, and the first draft
> > got this wrong in a way that would have under-discharged the corpus.** U1 was written as
> > *"neither `R` nor any source is written between"*, and measured against the corpus that
> > is too strict: **425 shipped frees have at least one intervening write to `R`** — 397 the
> > `¬flag` `cqrt_x` bracket (`classical_call_under_control_caller.expected.log:3-11`), 20 the
> > `rz` keep-alive bracket (`slice_intrinsic_ctlz.expected.log:7-11`,
> > `slice_uncompute_dead_dag.expected.log:10-14`), 8 controlled qram/tape writes — plus 10
> > with an intervening write to a SOURCE, all `cqrt_cswap`. Those are the corpus's ordinary
> > shapes, not corner cases, so U1-as-written could not have produced ~25,110.
> >
> > **The repair is to say what is true: the emptiness condition is the REDUCTION condition.**
> > There is one reduction engine — ported once from upstream's
> > `reduces_to_identity` / `unchanged_over` / `pair_operands_unchanged` / `co_written_stable`
> > — and U1, U2 and U3 are three ENTRY CONDITIONS into it, not three independent rules.
> > "Three rules, measured disjoint" partitions the free POPULATION, never the mechanism, and
> > an implementation that builds three separate checkers will get U1 wrong in exactly the
> > way this paragraph records.
>
> **SCOPE THE CERTIFICATE AS "THE OBSERVED WRITE HISTORY REDUCES TO THE IDENTITY", OF WHICH
> `_unc` IS ONE CLAUSE.** The natural framing — match the `_unc` to its forward — cuts the
> population in the wrong place: U2 is **larger by count** than U1. Scoped to `_unc` the
> certificate leaves roughly a third of the at-risk qubits stranded that the full rule
> reclaims. **Both percentages a reader may find in the working notes are in an
> UNDEFINED metric** — qubits-at-risk was never given a definition that survives the
> stranding disposition — so re-derive the ratio before quoting one.
> (Do **not** repeat the first draft's attribution of U2's whole population to
> `cqrt_alloc_i1` conjunction flags: there are fewer `alloc_i1` rails in the corpus than U2
> discharges, so the sentence is arithmetically impossible as written.)
>
> #### 3. The disposition is THREE-VALUED — and the residue is STRANDED, not released
>
> `cq_shadow_known_zero` returns 0 both for *"unknown"* and for *"known 1"*, and those two
> deserve opposite treatment. At the free, per qubit:
>
> - **proven clean** — release to the pool. This is a real proof, not a courtesy: §2's
>   certificate, or the shadow on the rotation-free surface.
> - **proven dirty** — ~~**hard error, in both configurations.**~~ **STRANDED, exactly as the
>   row below.** Rule 6's real content is that the library can *see* the rail is not `|0⟩`;
>   what that buys is a distinct VERDICT, not a distinct ACT. Under the shadow this row was
>   empty; under the certificate `ckd.18`'s rails land here. **§4's last clause — confirmed
>   2026-08-22, so nothing here is open any more — settled the act as (b) STRAND**, and this
>   bullet is kept in its original wording, struck, because the abort is the reading a fresh
>   reader will reconstruct on their own and it must be refused explicitly.
> - **unproven** — **STRANDED. The qubit is never released, never reaches the free list, and
>   is counted.** The program continues.
>
> **SO THE EPISTEMIC STATE IS THREE-VALUED AND THE ACT IS TWO-VALUED**, and keeping those apart
> is the whole of this section as amended. The two non-clean rows must stay distinguishable in
> the REPORT — that is what makes this section's own residue split producible at all, and it is
> the first thing `bd 06t` owes — while taking the same disposition. `CQOPS_FREE_ABORT` is what
> turns a conviction back into termination, on demand, without a rebuild.
>
> > **THE FIRST DRAFT RELEASED THIS ROW, AND THAT WAS THE ERROR — RECORDED BECAUSE THE STEP
> > IS EASY TO REPEAT.** §1's layering establishes that the `|0⟩` proof is CQ_lang's and that
> > we cannot perform it. **That licenses NOT ABORTING. It does not license RECYCLING**, and
> > the two have different failure modes: not-aborting costs a leaked qubit if the caller was
> > wrong, while recycling hands a non-`|0⟩` index to the next `cq_materialise` and corrupts
> > an unrelated rail. `src/qubits.h`'s `cq_qubits_release` contract names that second one
> > *"the one unforgivable bug in this project"*. The draft quoted upstream's own remedy
> > for exactly this epistemic state — *"left allocated (a qubit 'leak'), never freed"* —
> > in §1, and then did not apply it. **Stranding IS that remedy, one layer down.**
> >
> > **So `CQOPS_FREE_TRUST`'s prohibition is NOT lifted — it is narrowed and kept.**
> > Releasing an unproven index to the pool stays forbidden. What the layering buys is the
> > *abort*, not the *recycle*, and that is the whole of the change.
> >
> > **Three facts make stranding the right side rather than the timid one.** (i) The residue
> > is ~0.25% of frees, so the leak is small and bounded. (ii) It costs only qubits, and D2's
> > pool is unbounded by default — upstream calls this "the qubits-not-scarce regime" and
> > accepts the same trade. (iii) **The residue is precisely the population upstream itself
> > declines to certify**: the ~50 `slice_loop_break*` frees sit in the only five e2e entries
> > that pass **`--allow-unproven`** to the very stage-2b checker §1 cites as the discharge
> > (`tests/e2e/CMakeLists.txt:2744, 2764, 2783, 2803, 2818`). Releasing them would be
> > trusting a proof that upstream explicitly did not complete.
>
> **`CQOPS_FREE_ABORT` survives as a development and CI flag** — it is how a maintainer finds
> out that a caller stopped pairing its frees — and must stay reachable without a rebuild.
> It is not the default: under it, NORTH_STAR condition 1 is unreachable by construction.
>
> **The stranding mechanism is "never release", NOT a third pool bucket.** Measured, a
> `retired` state breaks **both** of `qubits.h`'s documented identities —
> `minted == live + free` *and* `peak == minted`, which is §8's entire qubit metric — and a
> retired index reads as a leaked ancilla to `cq_pc_live_is_exactly`, which runs on every L1
> case of every kernel. Simply not calling `cq_ctx_release_qubit` keeps both identities, and
> keeps `cq_shadow_retire` away from a still-live poisoned entry, which is the trap
> `ckd.18` recorded. What is added is observability, not a bucket: one explicit
> `cq_qubits_strand(pool, q)` that pushes nothing, marks the index so a double-strand or a
> later release is caught, and increments a counter the caller can read.
>
> #### 4. `ckd.18`'s rails are the carve-out, and the certificate is what finds them
>
> `alloc_i32(5); ry(θ); ry(−θ); free` — 25 such frees at the measured revision (26 at
> `397c67c`), all born from a **non-zero** literal, all with `n_ry == 2` and `sum(ry) == 0.0`
> exactly, physically `|birth-literal⟩` at the free. They are **not unprovable**: the same
> call stream gives the birth literal, the cancelling pair and the absence of any other
> write, so the rail is **provably dirty**. The shadow could never see this — it reports
> `unknown`; the certificate can.
>
> > **DATED NOTE, 2026-09-10 (`bd 1ti`): "the measured revision" IS CQ_lang `1fa1573`**
> > (2026-08-22 11:28, the commit before the pin — §0's table). Both figures reproduce from
> > the goldens alone: counting rails born from a non-zero literal that take exactly two
> > `cqrt_ry` and are then freed gives **25** at `1fa1573` and **26** at `397c67c`, which is
> > what independently confirms §0's pin IDENTITY. **This shape's corpus population has since
> > gone to zero** — §0's amendment carries that figure and the re-measure warning; do not
> > copy it here.
>
> **They are also a genuine upstream contract violation.** `ROADMAP.md:383` forbids exactly
> this; upstream's checker lets it through because its rule (A2) discharges a rail
> *"returning … to its alloc-time classical `v`"* rather than to zero, and
> `slice_uncompute_dead_dag` is a registered e2e test — its `add_test` block in
> `tests/e2e/CMakeLists.txt` passes no `FREECHECK_FLAGS`, so stage 2b runs on it strict, and
> the descriptive comment ABOVE that block reads *"SIX frees, one per rail, each after its
> last reverse-use"*. Three of those six rails sit at `|2⟩`, `|2⟩` and `|5⟩`, verified from
> the golden. **We do not negotiate with the frozen ABI**
> (Rule 1).
>
> > **CONFIRMED 2026-08-22 AS (b) STRAND, at the start of Step 23. This callout is kept as
> > the record of what was weighed, not as a live question** — an implementer reading it
> > must not re-open it, and `bd 06t`'s "do not implement that clause until it is confirmed"
> > is now discharged. What that means concretely: a rail the certificate **convicts** takes
> > the *same act* as the unproven row — never released, never on the free list, counted,
> > first occurrence named on `stderr`, program continues — so **the free path has two
> > dispositions, not three**, while the *epistemic* state stays three-valued and is
> > reported separately (§3's residue split is still owed, and is still the first thing
> > `bd 06t` owes). `CQOPS_FREE_ABORT` is what turns conviction into termination, on demand,
> > without a rebuild. **Rule 6's "hard error" survives verbatim for the row it was written
> > about — a *release* of a non-`|0⟩` index — which is the row that never happens now,
> > because neither convicted nor unproven qubits reach the pool.** The reading below is
> > preserved because the argument for (a) is the honest one and a future v2 with a
> > different fixture posture may take it.
> >
> > §3's proven-dirty row says
> > *hard error*, and applied to these rails that means **13 rails in 7 v1-in-scope fixtures
> > abort at Step 24** — including `slice_uncompute_dead_dag`, which CQ_lang ships as
> > passing. (An earlier draft said "13 fixtures". The rails live in 13 fixtures *in total*,
> > but 6 of those are fp-bearing and v1 aborts on them long before any free, so the
> > in-scope cost is 7 fixtures / 13 rails. 10 of the 25 rails are fp rails and never reach
> > a v1 free at all. **A rails count and a fixtures count are not interchangeable and
> > neither is a proxy for the other** — that conflation overstated this cost twofold.) Two readings, and the choice
> > is a judgement about whether failing a shipped fixture is acceptable, not something
> > further measurement decides:
> >
> > **(a) Abort.** Rule 6 as written, and correct under `ROADMAP.md:383` as well as under our
> > I3. The library reports a true positive. Costs Step 24 those fixtures.
> > **(b) Strand — RECOMMENDED.** Same act as §3's unproven row: never released, counted,
> > first occurrence named on `stderr`. I3 holds absolutely and nothing is laundered — which
> > is the whole of what Rule 6's hard error protects — and the fixtures run.
> >
> > (b) is recommended because Rule 6's *rationale* is laundering, not termination, and
> > because `CQOPS_FREE_ABORT` already gives a maintainer (a) on demand. ~~**Until this is
> > confirmed the rest of D15 stands and this clause does not.**~~ **Confirmed as (b) — see
> > the head of this callout. The whole of D15 now stands.**
>
> #### 5. What v1 does NOT claim
>
> **The certificate reasons over WRITES to a rail; entanglement is created by READS of it.**
> Upstream documents this about itself and scopes its checker accordingly
> (`free_pairing_check.py:436-448`): it is *"permutation-only BECAUSE that is the class the
> oracle can decide, and that deliberately excludes the superposition class"*, and
>
> > a `cqrt_ry(R,t) … <a CNOT-class READ of R> … cqrt_ry(R,-t)` pair cancels here while **R
> > is left entangled with the reader**, because the reduction reasons over WRITES to R and
> > never asks what read it.
>
> Demonstrated rather than argued: adding one `cqrt_cnot(%h,%k)` between the two `ry` calls
> of `ROTATED_NO_TAPE` — a committed **must-verify positive** fixture of upstream's own gate
> — leaves the verdict at exit 0 while exact two-qubit simulation gives `P(h ≠ |0⟩) = 0.1149`,
> purity `0.885`.
>
> **This is not a defect v1 can close.** Detecting it needs a simulator (Rule 13 forbids one
> *anywhere*), and a rail arriving at `cqrt_free` entangled has already violated
> `ROADMAP.md:383` before the call reaches us. It is also the reason §3 strands rather than
> releases: a certificate that cannot see the entangling class must not be spent on
> recycling.
>
> > **AND DO NOT OVERSTATE U1'S GROUND.** The first draft called the composite *"the identity
> > operator on the joint space, not merely the value is zero"*. That is true when both
> > applications see the same operand values and nothing read `R` in between — which is what
> > U1's preconditions are for — and it is **false in general**: at
> > `classical_call_under_control_caller.expected.log:3-11` the composite deliberately leaves
> > the rail correlated with its source. State U1 as its preconditions, not as a slogan.
>
> #### 6. Two mechanism notes, both traps
>
> **(i) The certificate must record the §9 CONTROL CONTEXT.** An uncontrolled `_unc` after a
> controlled forward leaves `dst` at `ctrl · f(a,b)`, not zero —
> `tests/support/kerneldrv.c:163-165` records this on the test side. A U1 match whose halves
> ran under different regions is not a match. (No corpus witness: there are **zero**
> `cq_template_*_controlled` calls in the goldens. The guard is for callers, not for CQ_lang.)
>
> **(ii) `cqrt_cswap` writes BOTH data args** (`cq_runtime.h:258`; upstream models it
> `reads=(0,) writes=(1,2)`). A write-tracker that misses it silently widens every rule —
> the same modelling error that produced the historical "37 rotation-rooted frees" figure,
> **reproduced to the unit by executing the counterfactual: dropping that write turns the 15
> `cswap`-rooted frees into 12 `rz`-rooted plus 3 alloc-rooted, i.e. `25 + 12 = 37`, which is
> where both the "37" and the "twelve `rz`-rooted rails" came from.** All four counts there
> are rows of §10's pre-`397c67c` classification, not `397c67c` figures — §0's blanket
> dating does not reach them. That reconstruction is the evidence for the retraction and is
> recorded here only; §10's trap (i) cites it.
> Enumerate the write set from `cq_runtime.h`, never from the symbol names.
>
> #### 7. What this decision retires, and what it leaves standing
>
> - **The `cq_zero_proof` C signature does not change** — it is already
>   `int (*)(const cq_ctx *, int32_t h, uint32_t q)`, and the certificate is what finally
>   uses the `h`; today's only proof throws it away (`(void)h;`,
>   `tests/support/poolcheck.c:181`). **Its CONTRACT does change**: it becomes three-valued,
>   and the stranding disposition is not expressible through a boolean, so the free path — not
>   the proof — is where the third state lives.
> - **`ckd.18` and `2cf` have no disposition of their own** and close with this decision.
>   `2cf`'s two regression markers were written to go red once `cqrt_free` had evidence other
>   than the shadow; that is now what happens, on purpose, and they become the positive cases
>   for U1 rather than being deleted.
> - **The "sanctioned un-poisoning write" is foreclosed.** PRD §10's older text, and the
>   plan's Deviation 2, both anticipated one exception to §3's conservative-in-the-safe-
>   direction-only rule, living in `shadow.h`. D15 needs none: the certificate reads the call
>   stream, not the shadow. **No such write may be added.**
> - **`ckd.17b`'s "no in-library theorem can cover the general case" survives, narrowed.** It
>   is true of the *general* case — `slice_loop_break`'s flag rail rests on loop algebra that
>   never reaches us — and false of 99.75% of the corpus. Both halves matter: the first is why
>   §3 has a residue at all, the second is why the residue is small enough to strand.


> ### D16 — the discriminator is CAPABILITY, and "is it minted?" strikes four symbols we can serve
>
> **THE CONTRADICTION THIS SETTLES IS BETWEEN TWO OF OUR OWN DOCUMENTS.** §1's struck-`cqrt_h`
> callout says omitting it "is not a link failure, because an unreferenced declaration
> produces no undefined reference … **DO NOT PAPER OVER IT WITH A SHIM STUB**".
> `docs/cqrt_census.txt`'s E1 says the opposite in as many words — implement `cqrt_h` and
> `cqrt_h_controlled` "AS DEFINED-BUT-UNREACHABLE BODIES". `bd vxk` filed the contradiction
> and forbade settling it by editing one document to match the other; this is the decision
> that settles it, and the census now records that its E1 is superseded.
>
> **§1 WINS, AND ITS ARGUMENT IS STRICTLY STRONGER: for a symbol whose EMISSION IS ITSELF
> THE BUG, a build-time failure dominates a run-time one.** It fires before anything runs,
> it names the symbol, and it cannot produce a wrong circuit. A loud abort can only fire
> after the pass has already emitted a gate we have no vtable entry for.
>
> **BUT THE DISCRIMINATOR BOTH DOCUMENTS REACH FOR — "is it minted by the pass?" — IS
> MEASURABLY WRONG, and that is the part neither of them states.** Measured over
> `ir-pass/`: eleven of the 173 have no minter anywhere, and `cqrt_alloc_handle` is a
> twelfth of a different kind. (`bd vxk`'s DESCRIPTION says 13 and its own enumeration lists
> 11; the 13 is a phantom.) Four of those eleven — `cqrt_x_controlled`,
> `cqrt_cnot_controlled` and, on the rotation side, the family `ControlledSymbols.cpp:32`
> cannot name because it hard-codes the stem `"cqrt_rz_"` — are symbols **libcqops can serve
> correctly today**. Striking them for want of a minter would leave the ABI's own
> declarations undefined for no reason at all.
>
> A second, independent refutation of the same discriminator: only 65 of the 173 appear
> anywhere in the corpus at all, and the 108 absentees include `cqrt_alloc_i8`,
> `cqrt_copy_i1` and `cqrt_xorc_i16`. **Corpus absence is a property of the fixture set, not
> of the ABI.**
>
> **CAPABILITY IS THE RULE, AND IT PARTITIONS THE 173 EXACTLY.**
>
> | population | n | disposition | why |
> |---|---|---|---|
> | rail surface | 32 | implemented, `shim/cq_runtime_rail.c` | v1 scope |
> | gate surface | ~~30~~ **35** | implemented, `shim/cq_runtime_gate.c` | v1 scope; includes the two `_controlled` primitives, the five integer `ry_*_controlled_inv`, and — **since the 2026-09-10 re-vendor, `bd w9i`** — the five integer **`ry_*_controlled`** |
> | fp widths | ~~34~~ **38** | loud abort, `shim/cq_runtime_v2.c` | §1 puts fp in v2; the four **`ry_f<W>_controlled`** joined 2026-09-10, and `cqrt_alloc_f<W>`'s own abort makes them **unreachable** rather than merely deferred |
> | `qram` | 63 | ~~loud abort, `shim/cq_runtime_v2.c`~~ **IMPLEMENTED 2026-09-02, `shim/cq_runtime_qram.c` (v1.2, D24)** | ~~emitted by the pass, so a link error would block every fixture~~ six shipped integer fixtures drive it; the array is D23's token plus `count` cells, the read is Bennett's unary-iteration tree, the store is Bennett's shadow store at a quantum index |
> | `tape` | 11 | ~~loud abort, `shim/cq_runtime_v2.c`~~ **IMPLEMENTED 2026-09-02, `shim/cq_runtime_tape.c` (v1.1, D23)** | ~~same~~ six shipped fixtures are the consumer §1 said did not exist; a write is a kept copy and the handle is a zero-qubit token |
> | `cqrt_alloc_handle` | 1 | `shim/cq_runtime_v2.c`, and it is **not** an ordinary abort — see `bd ck6` | REFERENCED by CQ_lang's own template archives |
>
> **ALL 109 SHIPPED 2026-08-27 (Step 23 landing 1 step 5), and two things about them are
> worth stating because neither is derivable from the table above.** *(98 since 2026-09-02:
> D23 moved the 11 `tape` bodies out of `cq_runtime_v2.c` and into scope; **35 since later
> the same day: D24 moved the 63 `qram` bodies out too**, so the file held the 34 fp-width
> core symbols and `cqrt_alloc_handle` and nothing else. **39 since 2026-09-10**, when the
> re-vendor added the four `cqrt_ry_f<W>_controlled` — the first time this bucket has GROWN.
> `nm` shows **180** defined `cqrt_*`, up from 171 by the nine: five real bodies and four
> aborts.)*
>
> **THE 2026-09-10 RE-VENDOR IS THE RULE'S SECOND WORKED EXAMPLE, AND IT RUNS THE OTHER WAY
> (`bd w9i`).** CQ_lang `f92d95e` added `cqrt_ry_<W>_controlled` at all nine widths, taking the
> ABI 173 → 182. Family first: `ry` is a rotation and rotations are v1 scope, so the question
> is not settled by the family. Width second: five integer widths **implemented**
> (`cq_runtime_gate.c`), four fp widths **deferred** (`cq_runtime_v2.c`). Where
> `cqrt_qram_alloc_f32` shows a family overriding an fp token, this shows an fp token deciding
> a family that is otherwise in scope — the two examples together are what the rule means.
>
> **AND HERE THE DEFERRAL IS FORCED RATHER THAN CHOSEN, which is worth recording because
> "implement all nine uniformly" is the obvious tidy alternative and was weighed.**
> `cqrt_alloc_f<W>` is itself an abort, so **no fp rail handle can exist at runtime in v1**.
> An implemented `cqrt_ry_f32_controlled` would resolve its handle through M07 and take M07's
> **generic** error rather than `cq_shim_unsupported`'s named one — worse diagnostics for a
> path nothing can reach, a forward implemented where its own `_inv` is not, and the only
> implemented fp rotation of any kind. **The abort is not a refusal of the capability; it is
> the statement `"fp is v2"`, which is what the four bodies become when fp lands.** The count
> moves 34 → 38 and `nm` on the archive moves 171 → 180 (182 minus the two `cqrt_h*`).
>
> **The buckets are by FAMILY first and by WIDTH second.** `cqrt_qram_alloc_f32` carries an
> fp token *and* a qram family, and it is QRAM's: there is no addressable quantum array in
> libcqops at `i1` either, so the fp width is not what defers it. This is not in tension with
> `bd 819`'s name rule ("a symbol is fp-touching iff its name carries an `f16/f32/f64/f80`
> token") — that rule partitions the 2,479 `cq_template_*` names expanded from
> `opcode_table.yaml`, a different population reached through a different header. `bd vxk`'s
> measured trap is about FIXTURE attribution rather than symbol attribution: 21 of the 33
> fixtures that reach `qram` before any fp call hit `cqrt_qram_alloc_f{32,64,80}` FIRST, so at
> the fixture level the same line dies either way and only the reason string changes.
>
> **The suite reads the MESSAGE, not the abort, and that is forced rather than stylistic.**
> With 109 bodies coming out of twelve macros, the interesting failure is not a missing abort
> — it is one that NAMES THE WRONG SYMBOL or carries the WRONG BUCKET, and a death case plus a
> `FAIL_REGULAR_EXPRESSION` is blind to both. `tests/test_runtime_v2.c` forks each of the 109
> and compares stderr byte for byte, with the name set checked BOTH WAYS against
> `shim/cq_runtime_abi.h` — an oracle neither the shim's macros nor the test's thunk macros
> can reach. Measured: shortening one stringified literal so four symbols print a name that is
> already another symbol's leaves the abort, the format and the bucket all correct, and is
> caught by that set check alone.
> | `cqrt_h`, `cqrt_h_controlled` | 2 | **UNDEFINED** | Rule 4 forbids `H`; §8's vtable has no `h`; nothing references them |
>
> **THE 34 fp WIDTHS ARE DECIDED ON LINKAGE, NOT ON SCOPE, AND THE TWO GROUNDS DO NOT HAVE
> TO AGREE** — `bd r3y` asked that the choice be explicit rather than inherited from
> `qram`'s. §1 alone would license leaving them undefined, since fp is out of v1 either way.
> Linkage forbids it: `libcq_runtime.a` is a single object holding all 173 as `T`, so any
> undefined REFERENCED symbol pulls the whole member and produces a duplicate-symbol wall
> naming `cqrt_alloc_i32` and `cqrt_free` — symbols we implement correctly — with the actual
> culprit named nowhere. Measured, and with a failure mode the bead does not record: under
> the other candidate link line the same omission links **silently and successfully**, with
> CQ_lang's trace-only stub serving every call. Defining them makes `libcq_runtime.a` inert
> under both, so the decision does not wait on `bd 590`.
>
> **AND `cqrt_h`'s OMISSION IS SAFE FOR A REASON NARROWER THAN §1 STATES.** §1's antecedent
> — "an unreferenced declaration produces no undefined reference" — is TRUE, and the
> conclusion it is used to justify holds only while nothing references the symbol. Measured
> both ways with real link experiments: with `cqrt_h` and `cqrt_h_controlled` undefined the
> link succeeds under BOTH candidate lines, and the duplicate-symbol wall is triggered by
> `cqrt_alloc_handle` instead — a symbol CQ_lang's template archives really do reference
> (`bd ck6`). So `cqrt_h`'s disposition is **not** contingent on `bd 590`, and §1 is stronger
> than its own note claimed. `cq_runtime.h` reserves `cqrt_h` for a later phase, so if the
> pass ever starts emitting it the link error is the intended outcome and this row is what
> produces it.
>
> **THE HONEST QUALIFIER ON THE FIVE `cqrt_ry_<W>_controlled_inv`.** "Implemented" here means
> implemented-with-D11's-refusal: under a quantum control, §7's `−I` row and both half-turn
> rows hard-error, so only the identity and general rows go through. That is not a new
> liability — `cqrt_rz_<W>_controlled[_inv]`, which §2.1 has listed as in scope since before
> M06 existed, carries the identical refusal — and D11 is where the decision to refuse rather
> than emit five hand-derived phases lives.
>
> ### D17 — `cqrt_addc` folds or runs Cuccaro in place, and it is never sandwiched
>
> **THE GAP NO DOCUMENT NAMED.** `cqrt_addc_<W>(h, imm)` is `h := (h + imm) mod 2^W`, IN
> PLACE. libcqops has exactly two adders and neither serves it: **M14's ripple is OUT of
> place**, so using it means minting a temporary, computing `tmp = h + imm`, and then needing
> `h := tmp` — with the old `h` still live and correlated, and freeing it needing a proof it
> has not got. **M15's Cuccaro accumulator is in place** and is the only one, and
> `cq_addacc_check` hard-errors in BOTH configurations unless every bit of both operands and
> the ancilla is already `CQ_BIT_Q`.
>
> **THE DECISION.** Fold when the rail is all-classical. Otherwise: materialise the immediate
> into a fresh W-bit rail, materialise `h`'s remaining classical lanes, allocate ONE ancilla,
> call M15 **in place** with `acc = h`, `b = immediate`, `x = ancilla`, then drive the
> immediate rail back to `|0⟩` with one `cq_emit_x` per set bit and free it. Two rows
> short-circuit before any of that: `imm == 0` (a PORT — upstream's `_try_identity_peephole!`
> does the same) and `W == 1`, which is `h ^= imm`.
>
> **FOUR TRAPS, THE FIRST FATAL TO A READER WHO KNOWS RULE 8.**
>
> 1. **DO NOT WRAP IT IN `cq_sandwich`.** The driver replays the compute half in reverse,
>    which UNDOES the in-place write: `h` comes back unchanged behind a perfect palindrome
>    and a plausible gate count. M20 applies `condneg` to a SCRATCH COPY inside a sandwich
>    for exactly this reason. With no sandwich there is no I6(b), so **every materialisation
>    is manual including the ancilla** — `cq_addacc_check` refuses a non-qubit `x[0]` at
>    every W, including W = 1 where no gate touches it.
> 2. **THE NAMING IS INVERTED FROM BENNETT.** libcqops `acc` is Bennett's `b`, the register
>    that is OVERWRITTEN; libcqops `b` is Bennett's `a`, the addend, which is RESTORED. So
>    `h` goes in `acc`. Swapped, the circuit computes into the wrong register and still looks
>    like the source.
> 3. **THE TRANSIENTS GET NO PRIVILEGE, AND THAT IS A DELIBERATE COST.** Both really are back
>    at `|0⟩` by construction — Bennett's own hygiene contract 3 — so a third `proven_zero`
>    literal beside `CQ_ZERO_BY_PALINDROME` and `CQ_ZERO_BY_CTRL_UNCOMPUTE` is tempting and
>    is refused: a blanket-clean proof is the laundering site D15 §3 and `bd 216`'s checklist
>    both forbid. They are freed with the ordinary evidence, and whatever it cannot clear
>    STRANDS. On a determinate rail it clears all of it. On a rotation-poisoned one it does
>    not, and the split is **width-dependent** rather than uniform — `bd dzj`'s trap 3 says
>    "whenever `h` is", which is false at W ≤ 2: K8 targets the addend only inside the full
>    MAJ/UMA blocks, so at W ≥ 3 the poison reaches lanes `0 … W−3` plus the ancilla and two
>    of the `W+1` transients still come back.
> 4. **`cq_addacc_check` COMPARES RANGES, NOT BASE POINTERS.** A mixed-kind `h` reaching K8
>    unmaterialised is a hard error, not a fold.
>
> **COST**, per quantum call at width W: `W+1` transient qubits, `cq_addacc_steps(W)` gates
> for the accumulate plus `2·popcount(imm)` for the immediate rail up and down, plus one
> materialisation per classical lane of `h`. **Negative immediates are the expensive half** —
> `addc(h, −1)` at i32 is `0xFFFFFFFF`, so 32 X up and 32 down — and a substantial share of
> the corpus's quantum calls carry one.
>
> **WHAT LANDING 2 OWES BECAUSE OF THIS, AND IT IS NEW.** The two transient rails have write
> histories that match none of D15's three certificate rules as written: U1 needs a
> `cq_template_F` / `_unc` pair, U2 needs self-inverse gates pairing off, and U3 needs
> `cqrt_alloc` plus `cqrt_addc` immediates summing to `−L`. The immediate rail's history is
> materialise → K8 → X-down, which is an identity by **Bennett's theorem about K8** and not
> by any rule D15 states. D15's residue measurement was taken over the CQ_lang corpus, which
> contains none of these internal frees, so the certificate does not cover them and the
> residue figure does not include them.
>
> **AMENDMENT THIS FORCED.** `src/kernels/addacc.h` said K8 "has no `cqrt_*` opcode, is never
> entered from CQ_lang" and that its ancilla "goes back to the pool through `cq_sandwich`'s
> epilogue". Both became false the moment this landed, in four places across that header and
> its `.c`, and they are amended rather than worked around. K8 still gets **no L5** — the
> classical fold lives in M26's wrapper, which is where the ABI boundary is.
