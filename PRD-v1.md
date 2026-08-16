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
    uint8_t unknown;  /* set by Ry/Rz, or by a gate with an unknown control */
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
| `Ry/Rz(q,θ)` | `q.unknown = 1` (unless θ is in the classical set, §7) |

Conservative in the safe direction only: the shadow may say *unknown* when the truth is
determinate (it forgets correlations), but never the reverse.

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

**This is the reason v1 uses ripple-carry rather than Cuccaro for the out-of-place
adder**, reversing an earlier recommendation. Cuccaro (`lower_add_cuccaro!`) is
in-place — `(a,b) → (a, a+b)` — so its uncompute is the *reverse circuit*, not a re-run,
which does not match CQ_lang's `_unc(out, src…)` "recompute from the still-live sources"
contract (`docs/backend.md` §9.2). Bennett's `lower_add!` is already XOR-into-target;
it only needs its carry chain cleaned. Cuccaro is still needed — as the *in-place*
accumulator inside the multiplier (§6.4).

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
constant `shl/lshr/ashr`, `sext/zext/trunc`.

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
| K10 | `mux(dst,c,t,f)` | `lower_mux!` | sandwich | also gives variable shifts |
| K11 | `mul(dst,a,b)` | `lower_mul_wide!` | sandwich | shift-add over K8, copy-out, reverse |
| K12 | `divrem(dst,a,b)` | `_soft_udiv_compile` | sandwich | restoring division: W × (K7, K9, K10) |

Two notes on the catalogue:

- **K9 derives 7 of 10 predicates for free** exactly as `lower_icmp!` does:
  `ne = ¬eq`, `ugt = ult(b,a)`, `ule = ¬ult(b,a)`, `uge = ¬ult(a,b)`, and the signed
  trio by flipping sign bits before `ult` (`lower_slt!`).
- **K12 is not a circuit.** Bennett implements division as a *branchless Julia kernel*
  compiled by its own pipeline (`src/divider.jl`). We do the same: an unrolled restoring
  division built from K7/K9/K10. Nothing new to port.
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

`Ry(θ) = [[cos θ/2, −sin θ/2], [sin θ/2, cos θ/2]]`. The special cases, per bit:

| Angle | Bit is constant | Bit is a qubit |
|---|---|---|
| `Ry`, θ ≡ 0 (mod 4π) | nothing | nothing |
| `Ry`, θ ≡ 2π (mod 4π) | nothing (global −1) | nothing (global −1) |
| `Ry`, θ ≡ π (mod 2π) | **flip the constant, 0 gates, 0 qubits** | emit `X` then `Z` |
| `Ry`, otherwise | materialise, then `sink.ry` | `sink.ry` |
| `Rz`, φ ≡ 0 (mod 4π) | nothing | nothing |
| `Rz`, otherwise | **nothing** — diagonal on a definite value is a global phase | `sink.rz` |

The θ ≡ π row is the important one. `Ry(π) = XZ`, i.e. `X` up to a **relative** sign on
|1⟩. On a bit that is already a definite classical constant that sign is *global* and
unobservable, so the bit stays classical and costs nothing. On a bit that is already a
qubit — possibly in superposition, possibly entangled — the sign is observable and must
be emitted. Getting this asymmetry right is what makes classical-mode testing possible
(§11) without making it unsound.

Angle comparison uses an exact-multiple test against a tolerance, configurable, default
`1e-12` relative.

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
  > **Who may stamp: one named literal, in M09.** The sandwich driver owns the scratch
  > qubits end to end — it materialises the region at step 0 (I6(b)) and releases it in its
  > own epilogue, passing `CQ_ZERO_BY_PALINDROME`. That literal is the **sole** `proven_zero`
  > constant in `src/`, and it is irreducible: Rule 13 forbids the library holding the gate
  > stream that would let it *compute* the answer, so "a sandwich cleans its own scratch" is
  > asserted exactly once, by the code that owns the construction. **It rests on three
  > premises — one gate per step (`ckd.14a`), I6(a), and I6(b) — and deleting any one makes
  > it a laundering site.** Not the `_unc` epilogue, not M07, not M08 (which ships no release
  > a kernel can call), and not a kernel — there is no API through which one could.
  >
  > **Verified, never recomputed.** Recomputing the expected value is a simulator, which is
  > forbidden outright. Instead: `cq_shadow_retire` hard-errors in **both** configurations if
  > the entry is determinate and non-zero — a complete detector of a non-cancelling sandwich
  > across the whole rotation-free kernel surface (Steps 10–17), because `Ry`/`Rz` are the
  > only producers of `unknown`. It is **inert on the L6 corpus**, where nearly every rail is
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
| 5 | K10 (mux) + variable shifts; K8 (Cuccaro accumulator); K11 (mul) | L1–L4 green |
| 6 | K12 (div/rem) | L1–L4 green |
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
src/kernels/{bitwise,shift,cast,add,cmp,mux,mul,divrem}.c
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
| **D7b** | **Two sources alias each other** — `mul(h, h)` | **Measured the same way: 599 occurrences, of which 10 are on v1's integer surface.** CQ_lang ships a fixture named for it — `tests/e2e/slice_select_rail_alias_cond.expected.log:4` is `cq_template_icmp_slt_i32(h0, h0) -> h1`, and `:31` is `cq_template_mul_i32(h10, h10) -> h11`; also `spec_newcand_qsq_caller:13,16`, `spec_replan_qpow_caller:13,18`, `slice_i128_mulhi:4`. **This is LEGAL and must NOT abort.** The remedy is now required rather than contingent: a defensive `cqrt_copy` of one aliased source at the **M26 handle boundary** (Step 23), *before* Step 24 runs — one place, not twelve. M07 exposes the predicate; M26 acts on it |

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
