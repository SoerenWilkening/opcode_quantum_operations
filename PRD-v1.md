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
| Core runtime, **controlled** | `cqrt_copy_<W>_controlled`, `cqrt_rz_<W>_controlled`, `cqrt_rz_<W>_controlled_inv` | §2.1 |
| Binary arith | `add sub mul sdiv udiv srem urem` | Bennett `adder.jl`, `multiplier.jl`, `divider.jl` |
| Binary bitwise | `and or xor shl lshr ashr` | Bennett `lowering/arith.jl` |
| Compare | `icmp` × 10 predicates | Bennett `lower_eq!/ult!/slt!` |
| Casts | `sext zext trunc` (int↔int) | Bennett `lower_cast!` |
| Shapes | `qq`, `hl`, `lh` | free — a literal is an array of constant bits |
| Axes | forward, `_unc`, `_inv`, `_controlled`, `_controlled_inv` | §9, §10 |

> **`cqrt_h` is struck from this row (Step 0.7 — resolved).** Earlier drafts listed
> `cqrt_x/h/cnot/toffoli`, which contradicted three other places: constraint 1 below forbids
> `H` outright, §8's sink vtable has no `h` entry, and §12 builds `H` out of rotations. The
> evidence settles it and none of the three suspected causes was right. `cqrt_h` **is**
> declared (`cq_runtime.h:226`) and defined (`cq_runtime.c:454`) in CQ_lang — but it is
> **emitted by nothing and called by nothing**: no site in `ir-pass/`, no `.ll` fixture, no
> test, and CQ_lang's own link witness takes the address of `cq_template_*` symbols only.
> `include/CQ.h` exposes exactly three primitive families — `cq_theta` (Ry), `cq_phi` (Rz),
> `cq_measure` — so a CQ program has **no way to write a Hadamard except** §12's composite.
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

- **All floating-point widths** (`f16/f32/f64/f80`, 878 symbols) — **v2**, via Bennett's
  `src/softfloat/` branchless soft-float suite. `gen_shim.py` still emits a body for each
  of the 878: a loud abort naming the symbol, so the link always succeeds and an fp
  program fails with `cqops: cq_template_sitofp_i32_to_f64 not implemented (fp is v2)`
  rather than an undefined-reference wall. Free diagnostics, and it makes the v2 boundary
  visible at runtime instead of at link time.
- **`cqrt_tape_*`** (reversible I/O tape) — no consumer until `printf` on tainted data.
- **`cqrt_qram_*`** — stretch increment (§13, Increment 8). Grover does not need it.
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
both are out of v1 scope, but a second counter for them later would diverge handle
numbering and fail every L6 trace diff while every assert stayed silent.

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
  `f(a,b) ^ f(a,b) = 0`. ✔ Then free `out`'s qubits.
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

**The `Ry` sink entry stays `double` all the way down.** `qec_rz` currently takes an
exact rational `(p, q_denom, precision)` and there is no `qec_ry` at all; converting
angle representations is the *QEC sink's* problem, not the kernel layer's. Until QEC
grows float `Rz` and a logical `Ry`, the QEC sink's `ry`/`rz` entries are stubs that
record the call — the printf sink is the v1 default.

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
**qec** (compiled only when `C_quantum_error_correction` is present; `qec_x`, `qec_cx`,
`qec_ccx`, `qec_mz`, `Ry`/`Rz` stubbed). Selected at runtime via
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
> and nothing for our stream to collide *with*. The open question is not which
> stream M23 writes to; it is what Step 24 compares against, given that
> IMPLEMENTATION_PLAN's "diff emitted traces / Traces match" and NORTH_STAR's
> "link and run" are not the same criterion.

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
  > will not.** Because we never demote (D6), an in-place `cqrt_ry`/`cqrt_rz` applied to
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
- **`_inv(srcs…)`** — CQ_lang's spine no longer emits `_inv` for data templates; only
  `CompareLowering` emits it, for Phase-4 control flags. A compare's forward is
  `flag ^= pred(a,b)`, which is its own inverse, so `_inv` allocates a fresh rail and
  runs the forward. Low priority.
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
  > **This does NOT resolve `ckd.18`.** There the rail's bits *are* qubits — `ry` at
  > arbitrary θ materialises all 32 of them — physically holding `|5⟩` with an unknown
  > shadow. The scope clarification exempts constants, not materialised bits, so the
  > rotation-root free still hard-errors and `ckd.18` stays open.

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
  > **`ckd.17b` — a CQ_lang rail at `cqrt_free` — is NOT settled and is filed separately.**
  > A sandwich certificate reaches *none* of the corpus's 51,696 frees, because none of them
  > is on a scratch region; and no in-library theorem can cover the general case
  > (`slice_loop_break.expected.log:10-25` rests on loop-condition algebra that never reaches
  > us; `specialize_transitive_caller.expected.log:3-16` uncomputes by recomputing into a
  > *different* handle, defeating any handle-keyed matching). At Step 23 M26 chooses at one
  > greppable call site between **`CQOPS_FREE_ABORT`** (the default — Rule 6 as written) and
  > **`CQOPS_FREE_RETIRE`** (tombstone the handle and take its indices out of circulation
  > forever, so they never reach the free list and I3 holds absolutely — PRD §10's
  > already-blessed safe leak, applied at the free instead of at a withheld one).
  > **There is no `CQOPS_FREE_TRUST` and one must never be added:** releasing unproven
  > indices to the pool is laundering under another name.
  >
  > **AND THAT PROHIBITION NOW RESTS ON 25 NAMED FREES RATHER THAN ON AN ARGUMENT
  > (`ckd.18`, re-measured at Step 19).** Classifying all **51,696** frees by how the rail
  > was *last written*: 25,138 `cq_template_*_unc`, 19,153 `cqrt_toffoli`, 7,223
  > `cqrt_cnot`, 91 `cqrt_copy`, 40 `cqrt_addc`, **25 `cqrt_ry`**, 15 `cqrt_cswap`, 11
  > `cqrt_qram_load_unc`, and **0 `cqrt_rz`**. The 25 are exactly the rails born from a
  > **non-zero** `cqrt_alloc` literal whose only writes are a cancelling `(θ, −θ)` `ry`
  > pair, and they are physically `|birth-literal⟩` at the free — so `TRUST` would push
  > `|1⟩` qubits onto the free list on all 25 and break **I3** outright. The positive
  > control is in the same corpus: of the **65** freed rails born from a non-zero literal,
  > the other **40** are returned to `|0⟩` by an explicit `cqrt_addc_<W>(h, −L)` first, and
  > `sum(addc) == −L` in **40/40**.
  >
  > **`ckd.18` therefore has no disposition of its own and folds into this bullet.** No
  > in-library evidence can reconstruct those 25 — cancellation restores the *birth
  > constant*, never zero — and no mechanism is worth building for 25 frees out of the
  > **51,651 of 51,696 (99.91%)** that are rotation-tainted once M22 lands. Fixing the 25
  > perfectly would still leave 51,626 frees hard-erroring, so `ckd.17b`'s disposition is
  > the gating decision and `ckd.18` is one of its counterexample sets. Measured cost of
  > `CQOPS_FREE_RETIRE` as an upper bound: **93,593** permanently-retired qubits across the
  > whole corpus (worst fixture 31,334), **3,028** across the 56 integer-only fixtures — set
  > against 131,583 for a single i128 `udiv`, and D2's pool is unbounded by default.
  >
  > **Two traps recorded with it.** (i) The bead's older "37 rotation-rooted frees, of which
  > 12 are `rz`-rooted on rails born 0 and therefore stay classical" is **false**: those 12
  > are `alloc(0); cswap(qflag,·,tmp); rz; cswap; free`, and the Fredkin **materialises**
  > `tmp` before the `rz` arrives, so §7's `Rz`-constant cell never applies to them. They
  > are physically `|0⟩` at the free and are `ckd.17b` cases, not `ckd.18` ones — and under
  > **D12** they free cleanly, which is that decision's measured payoff. (ii)
  > `CQOPS_FREE_RETIRE` must **not** go through `cq_ctx_release_qubit`: `cq_shadow_retire`
  > does not fire on a poisoned entry, so it would silently clear poison on a **still-live**
  > qubit — the one write the shadow's discipline forbids structurally. The name collision
  > with `cq_shadow_retire` is an active trap and the two mean opposite things.

  > **What "provably clean" reads is NOT settled by this bullet, and it is not obvious.** It
  > cannot be the two-bit shadow **on a tainted rail**: §3's `CX` rule propagates `unknown`,
  > so an uncomputed *rotation-tainted* rail is all-`Q unknown` and a literal shadow check
  > would hard-error on it. There the evidence has to be **structural** — the §4 kernel
  > contract plus I6 palindromic reversal — which means one sanctioned un-poisoning write,
  > the sole exception to §3's "conservative in the safe direction only". This bites M08's
  > "assert clean on release" at **Step 8, before any kernel exists**. Tracked separately;
  > **do not** weaken this hard error to a warning to make a fixture pass.
  >
  > **CORRECTED 2026-08-15 at Step 10 — the paragraph above said "every legitimate sandwich
  > kernel" and that over-generalised, in a way that contradicted this same section three
  > pages up.** `Ry`/`Rz` are the ONLY producers of `unknown` (`src/shadow.c:121` is the sole
  > writer of `e[q].unknown = 1`); `CX` and `CCX` merely propagate what is already there. So
  > **on the rotation-free surface — Steps 10 through 17, every kernel and no rotation —
  > nothing is tainted, every shadow entry is determinate, and `cq_shadow_known_zero` is not
  > conservative but EXACT.** It is a genuine free-time proof there, which is exactly the
  > reach this section already claims two paragraphs earlier for `cq_shadow_retire`
  > ("a complete detector … across the whole rotation-free kernel surface (Steps 10–17),
  > because `Ry`/`Rz` are the only producers of `unknown`"). Read literally, the older
  > wording said Step 10's L3 free must hard-error, and it does not: Step 10 frees its result
  > rails through `cq_reg_free` with
  > `tests/support/poolcheck.c:cq_pc_zero_proof_rotation_free`, and 75 ctest tests pass in
  > both configurations. **The scope is the whole content of that proof and its name says so**
  > — it becomes a laundering device the moment M22 lands at Step 19, which is what `ckd.17b`
  > and `ckd.18` are about, and neither is answered by it.

---

## 11. Test strategy

The whole point of the tri-valued design is that levels 1–3 need no quantum simulation.

| Level | What | How |
|---|---|---|
| L0 | Fold table | Unit tests over all operand-state combinations in §3 |
| L1 | **Kernel differential** | For each kernel, compare the result register's **value** against the C operator: the **full cross product** — every `(a,b)` × every bit-kind mask **pair** — at W ∈ {1,2,3,4,5}; **structured corners + seeded sampling, crossed with every mask pair**, at W = 8 and above. **Not value-exhaustive at W = 8** — see the note below |
| L2 | Ancilla-clean | After every kernel call, assert **no index is live that no named register owns**, and that every index a named register owns is live |
| L3 | Uncompute round-trip | forward → `_unc` → assert `dst`'s **values** are all-zero. The pool is **not** restored yet — `_unc` reclaims nothing (§10). The harness then frees `dst` explicitly and asserts **`live` is back to its pre-call value and every index `dst` held is back on the free list** |
| L4 | Gate-count goldens | Pin per-kernel `(NOT, CNOT, Toffoli)` at each W. Cross-check against Bennett's published baselines where the construction matches, and document every deliberate delta. See the arity and staleness notes below — both bit an earlier draft |
| L5 | Classical short-circuit | Assert **zero** gates and **zero** qubits for the fully-classical case, and exactly 1 qubit / 1 CX for `int a = 0; a \|= b << 3` |
| L6 | CQ_lang e2e | Link against CQ_lang's existing fixtures, diff emitted traces |
| L7 | Grover | §12 |

L1 and L5 are the two that actually catch bugs. L4 is what stops a "harmless" refactor
from silently doubling the T-count.

> **L1 IS NOT VALUE-EXHAUSTIVE AT W = 8, corrected 2026-08-16, and the reason is
> structural rather than a concession to runtime.** This row read "all `(a,b)` at
> W ∈ {1,2,4,8}", and the `8` cost 131,152 cases per kernel — 63% of the compare suite and
> about half the add suite. It bought nothing the rest of the sweep did not already have.
> The §3 fold table dispatches on a bit's **kind**, never on a qubit's value (§15 D6, no
> demotion), and every kernel is width-generic over `reg->width` with **no width switch**
> (I5). So **at the all-quantum mask the emitted circuit is byte-for-byte identical across
> all 65,536 value pairs** — the suite ran one fixed gate sequence 65,536 times through the
> classical shadow. Any fault surviving the W ≤ 5 full cross product, which is exhaustive
> over the *product* of values and masks, must be **width**-dependent — a loop bound, an
> MSB boundary, a carry that only exists above some length — and those are caught by
> covering widths and by L4's closed forms, the sandwich palindrome and the qubit peak.
>
> **Values reach the circuit only through classical lanes, and only as one bit per lane:**
> a classical `ZERO` control folds its gate away, a classical `ONE` rewrites it (`CX`→`X`,
> `CCX`→`CX`) and removes none. Named corners — 0, max, MSB, equal pairs, `v`/`v±1` in both
> orders, `2^i` and `2^i − 1` for every `i`, alternating — exercise that directly.
>
> **The replacement is broader, not just cheaper.** Exhaustion spent its entire budget on
> **two** masks (all-quantum, plus one random draw per pair); the structured set plus seeded
> sampling is crossed with **every** mask pair, so the arithmetic corners now meet the
> asymmetric masks risk R8 names — which no value pair ever did. **Verified rather than
> argued:** the 20-mutant battery over `src/kernels/cmp.c` was re-run against the reduced
> sweep and kills the same set, and every run still prints its own case counts, so the
> no-silent-caps property is unchanged.

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
`58/6/40/12` in `README.md:114` / `CLAUDE.md:27`. **`BENCHMARKS.md` is stale** — it was
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

v1 is accepted when:

1. This compiles through `cqc`, links against `libcqops`, and emits a gate stream.
2. Replacing `M_PI/2` with `M_PI` (classical mode) makes the whole program run
   deterministically and `cq_measure` returns the value plain C would compute — proving
   the oracle's arithmetic circuits are correct, with no quantum simulation anywhere.
3. The counter sink reports Toffoli count and T-count (7 per Toffoli); **peak qubits
   comes from `cq_qubits_peak()`, not from the sink** — see the §8 correction for why
   a streaming sink cannot compute it and the pool already has it exactly. Those
   numbers are stable across runs and pinned as goldens.

---

## 13. Delivery increments

| # | Increment | Exit criterion |
|---|---|---|
| 1 | Foundation: `cq_bit`, register/handle table, qubit pool + free list, shadow, sink vtable, printf + counter sinks, emitter fold table, `cqrt_alloc/measure/free`, `cqrt_x/cnot/toffoli` | L0 green; alloc→measure round-trips; fold table exhaustively tested |
| 2 | K1–K5 (bitwise, constant shifts, casts) + `_hl`/`_lh` folding | L1/L2/L5 green for these ops |
| 3 | K6–K7 (add, sub) with carry uncompute | L1–L4 green; `x+1` @ i8 count pinned |
| 4 | K9 (compares, all 10 predicates) | L1–L4 green |
| 5 | K10 (mux) + variable shifts; K8 (Cuccaro accumulator); K11 (mul) | L1–L4 green — **COMPLETE 2026-08-16** (Steps 14, 15, 16). K8 is the exception the criterion did not anticipate: it is not a Rule 7 kernel, so its levels are restated by hand and **L5 does not apply to it at all** (K08.md §5 D7) |
| 6 | K12 (div/rem) — **COMPLETE 2026-08-16 (M19, M20)** | L1–L4 green, **plus L5** (the all-classical short-circuit is not optional here — pre-materialisation would otherwise take `8W²+4W−1` qubits for an operation with no quantum input; risk R9) **and D3** (`sdiv`/`srem` by zero never traps and whatever it returns is pinned). L4 at `W ∈ {1,8,16,32,64,128}` — **i128 is a shipped `divrem` width**. All four opcodes green in both configurations; every figure in `K12.md` §3 and §4 reproduced on the first run, **including the four signed columns that had never been executed** |
| 7 | `_unc` / `_inv` / `_controlled` axes; rotations + θ special cases; measurement | L3 green across all kernels; classical mode works |
| 8 | Generated shim over the full integer grid (**1595**, or **1455** if i80 is ruled out of scope — §1); CQ_lang link; Grover | L6, L7 green — **v1 done** |
| 9 | *(stretch)* QRAM — port Bennett's QROM (`src/qrom.jl`, self-cleaning AND tree, 2(L−1) Toffoli) and Shadow (`src/softmem.jl`) behind `cqrt_qram_*` | load/store round-trip |

Increments 2–6 are independent after 1 and can be built in any order or in parallel.

---

## 14. Layout, naming, build

```
include/cqops/cqops.h        public API: context, sink, config
src/bit.[ch] reg.[ch] qubits.[ch] emit.[ch] rotate.[ch] controlled.[ch]
src/sink_printf.c sink_count.c sink_qec.c
src/kernels/{bitwise,shift,cast,add,addacc,cmp,mux,mul,divrem}.c
shim/gen_shim.py             reads CQ_lang's tools/opcode_table.yaml
shim/cq_runtime_impl.c       the cqrt_* surface
shim/cq_templates_impl.c     generated dispatch (1595 thin wrappers; see §1 on i80)
tests/
```

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
> L1-exhaustive green at `W ≤ 4` over every `(a,b)` including `b = 0`, pool restored and
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
> **Measured payoff, and it is why the exact side is the right side.** The corpus's twelve
> `rz`-rooted rails are `alloc(0); cswap(qflag,·,tmp); rz(tmp,φ); cswap; free` — the Fredkin
> materialises `tmp` *before* the `rz` arrives (a `CCX` with two `Q` controls and a constant
> target materialises), so under a poisoning rule all twelve become unfreeable, and under
> D12 the `cswap` involution restores their shadow to zero and they free cleanly. `Ry(π)` on
> a qubit likewise keeps its rail freeable. The alternative costs `ckd.18` twelve extra
> frees and buys nothing.
>
> **The honest limit.** The argument is *derived* from "a determinate `value` means a
> definite computational-basis value", which holds today because every gate below Layer 4 is
> a basis permutation; it is not measured, and Rule 13 forbids the simulator that would
> measure it. What bounds the risk is that §9's controlled axis cannot break it either — a
> controlled diagonal puts a *relative* phase on the **control**, whose own basis value is
> likewise unmoved — and that D11 refuses the quantum-controlled fold rows outright.
