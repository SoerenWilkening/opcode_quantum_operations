# PRD — `libcqops` v1: the integer opcode surface

Status: draft for review · Target: CQ_lang integer templates, Grover end-to-end
Companion: [`NORTH_STAR.md`](NORTH_STAR.md)

---

## 1. Scope

### In scope

The **integer** half of CQ_lang's frozen ABI, at widths `i1, i8, i16, i32, i64, i128`:

| Family | Symbols | Source construction |
|---|---|---|
| Core runtime | `cqrt_alloc/measure/free`, `cqrt_x/h/cnot/toffoli`, `cqrt_copy_<W>`, `cqrt_cswap`, `cqrt_addc/xorc_<W>`, `cqrt_ry/rz_<W>` | this repo |
| Core runtime, **controlled** | `cqrt_copy_<W>_controlled`, `cqrt_rz_<W>_controlled`, `cqrt_rz_<W>_controlled_inv` | §2.1 |
| Binary arith | `add sub mul sdiv udiv srem urem` | Bennett `adder.jl`, `multiplier.jl`, `divider.jl` |
| Binary bitwise | `and or xor shl lshr ashr` | Bennett `lowering/arith.jl` |
| Compare | `icmp` × 10 predicates | Bennett `lower_eq!/ult!/slt!` |
| Casts | `sext zext trunc` (int↔int) | Bennett `lower_cast!` |
| Shapes | `qq`, `hl`, `lh` | free — a literal is an array of constant bits |
| Axes | forward, `_unc`, `_inv`, `_controlled`, `_controlled_inv` | §9, §10 |

That is **1455 of the 2333** declarations in `runtime/cq_templates.h` — the *entire*
purely-integer surface, 215 of it i128 — realised by roughly a dozen kernels.

The remaining **878 are fp-touching and none of them are in v1.** Beware when counting:
234 of those 878 are the cross-domain casts (`sitofp`, `uitofp`, `fptosi`, `fptoui`,
`bitcast`), whose names carry *both* an integer and a floating-point width. They look
integer-ish and are not. The partition that balances is
1455 purely-integer + 878 fp-touching = 2333.

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
    uint32_t width;      /* 1, 8, 16, 32, 64 or 128                  */
    cq_bit  *bits;       /* `width` entries, LSB at index 0          */
    uint8_t  live;
} cq_reg;

/* Per-qubit classical shadow — the whole of our "simulation". */
typedef struct {
    uint8_t value;    /* 0 or 1, meaningful iff !unknown */
    uint8_t unknown;  /* set by Ry/Rz, or by a gate with an unknown control */
} cq_shadow;
```

**Handle table**: dense `int32_t` → `cq_reg`, monotonic allocation to match CQ_lang's
existing trace convention (`h0`, `h1`, …). Handles are never reused; qubit *indices* are.

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
void cq_emit_x  (cq_ctx*, cq_bit *t);
void cq_emit_cx (cq_ctx*, cq_bit *c,  cq_bit *t);
void cq_emit_ccx(cq_ctx*, cq_bit *c1, cq_bit *c2, cq_bit *t);
```

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
| `CCX(c1,c2,t)` | both controls `Q`, `t` constant | **materialise** `t`, then `sink.ccx` | 1 or 2 |
| `CCX(c1,c2,t)` | both controls `Q`, `t = Q` | `sink.ccx` | 1 |

**Materialisation** (`cq_materialise(bit)`): take a qubit from the pool (guaranteed |0⟩
by I3), emit `X` if the bit's constant was 1, set `kind = CQ_BIT_Q`. This is the *only*
place a qubit is ever allocated for data, and it is exactly the rule "a CX from a tainted
bit into an untainted bit allocates a qubit."

Operand distinctness (`c != t`, `c1 != c2 != t`) is asserted, not assumed — a coincident
operand is a meaningless channel and a real miscompile signature.

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
compute  f into scratch          (Bennett's construction, verbatim)
copy-out scratch → dst           (W CNOTs; this is the "^=")
reverse  the compute             (same gates, reverse order — all three gates are self-inverse)
free     scratch
```

Cost: 2× the compute half. Applies to `add`, `sub`, `eq`, `ult`, `slt`, `mux`,
`mul`, `divrem`. Naturally clean already (no sandwich needed): `and`, `or`, `xor`,
constant `shl/lshr/ashr`, `sext/zext/trunc`.

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

v1 ships three: **printf** (default; one line per gate, the format CQ_lang's golden
traces already use), **counter** (per-kind totals + peak live qubits, matching
`gate_count` / `ancilla_count` in Bennett.jl so baselines are directly comparable), and
**qec** (compiled only when `C_quantum_error_correction` is present; `qec_x`, `qec_cx`,
`qec_ccx`, `qec_mz`, `Ry`/`Rz` stubbed). Selected at runtime via
`cqops_set_sink()`; the default is chosen by environment variable so CQ_lang's existing
fixtures need no changes.

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
  source is still live. Postcondition: every bit of `out` is `BIT_ZERO` or a known-zero
  qubit; its qubits go back to the pool.

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
- **`cqrt_free(h)`** — assert every bit is `BIT_ZERO` or a known-zero qubit, return the
  qubits to the pool, mark the handle dead. A free of a dirty rail is a hard error, not
  a warning: it is the exact signature of a silent state collapse.

---

## 11. Test strategy

The whole point of the tri-valued design is that levels 1–3 need no quantum simulation.

| Level | What | How |
|---|---|---|
| L0 | Fold table | Unit tests over all operand-state combinations in §3 |
| L1 | **Kernel differential** | For each kernel: all `(a,b)` at W ∈ {1,2,4,8} × sampled classical/quantum bit-kind masks, compare shadow result against the C operator. Random sampling at W ∈ {16,32,64} |
| L2 | Ancilla-clean | After every kernel call, assert the live-qubit set equals exactly `dst`'s qubits |
| L3 | Uncompute round-trip | forward → `_unc` → assert `dst` all-zero **and** the pool is back to its pre-call state |
| L4 | Gate-count goldens | Pin per-kernel `(NOT, CNOT, Toffoli)` at each W. Cross-check against Bennett's published baselines where the construction matches (e.g. `x+1` at Int8 = 58/6/40/12; Cuccaro = 6W−5) and document every deliberate delta |
| L5 | Classical short-circuit | Assert **zero** gates and **zero** qubits for the fully-classical case, and exactly 1 qubit / 1 CX for `int a = 0; a \|= b << 3` |
| L6 | CQ_lang e2e | Link against CQ_lang's existing fixtures, diff emitted traces |
| L7 | Grover | §12 |

L1 and L5 are the two that actually catch bugs. L4 is what stops a "harmless" refactor
from silently doubling the T-count.

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
3. The counter sink reports Toffoli count, T-count (7 per Toffoli) and peak qubits,
   and those numbers are stable across runs and pinned as goldens.

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
| 8 | Generated shim over the full 1474-symbol integer grid; CQ_lang link; Grover | L6, L7 green — **v1 done** |
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
shim/cq_templates_impl.c     generated dispatch (1474 thin wrappers)
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
| **D7** | **Operand aliasing** | Nothing in CQ's docs forbids `cq_template_add_i32(h, h)` or `_unc(out, out, b)`. Every kernel assumes distinct registers and I2 assumes distinct qubits. **v1: assert loud** and find out empirically whether the pass ever does it, rather than writing aliasing-safe kernels for a case that may not exist. If it fires, the fix is a defensive `cqrt_copy` of the aliased operand |
