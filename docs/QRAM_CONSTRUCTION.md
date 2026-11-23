# QRAM construction: gate lists, counts and depth

Status: construction note, 2026-11-17. It completes `docs/QRAM.md` (which fixes the family)
for the quantum-address path of BACKEND_ABI §1.10. Gates: X, CX, CCX only. Every ancilla is
|0⟩ again when the call returns. Every count and depth below was measured by a throwaway
classical simulator (§9); the formulas match it exactly where the table says "= measured".

Provenance marks: **[paper]** means checked in the paper text; **[ours]** means our own
construction or derivation. §8 lists what was read.

## 1. Notation

- q is the number of quantum bits of the address rail. N = 2^q.
- p_0 < … < p_{q-1} are their bit positions. b_k is the address qubit at p_k.
- base is the rail's known bits, with 0 at the quantum positions.
- Leaf a ∈ [0, N) is the address A(a) = base + Σ_k a_k·2^{p_k} mod 2^64, with a_k bit k of a.
  The reachable set R is {A(a)} (QRAM.md "The reachable set").
- S is the number of cells one access touches (ABI store-size). w is the number of bits it
  touches, w ≤ 8S. Leaf bit i (0 ≤ i < w) is bit i mod 8 of cell A(a) + ⌊i/8⌋ mod 2^64,
  which is little-endian. κ(a,i) names that cell bit: a qubit if the cell is QUANTUM, a known
  bit if it is HOST.
- L = ⌈log2 w⌉. For a byte access (w = 8), L = 3.
- c is the control of a `_controlled` form. Without a control, every "CX(c, ·)" below is "X(·)".
- `fresh` takes a new ancilla qubit, which is |0⟩. Every gate list is emitted in the order
  written. Depth is the as-soon-as-possible layer count (a gate goes one layer after the latest
  earlier gate on any of its qubits), which is what the replay backend's depth counter
  measures (NORTH_STAR, Testing).

The classical address bits never index the tree [ours]. They only fix `base`, and they cost no
gate and no qubit. The tree has q levels, and level l reads b_{q-1-l}, so leaf index a
follows the quantum bits in order.

## 2. Decoders: one flag per leaf

Both decoders produce flags F[0..N-1] on ancillae, with F[a] = c ∧ [address = A(a)]. Exactly
one flag is set when c = 1, and none when c = 0. The flag is the control that ABI §1.10 step
(2) gives to the `_hl_controlled` access. The control c costs nothing extra: it is ANDed in by
injecting it as the token.

### 2.1 Bucket brigade (BB)

The tree has q levels. Each bit k (and the token, written k = q) is injected at a fresh root
and routed down levels 0..k-1. Router (l, j) is the qubit that bit l reached at node j of level
l: it holds b_{q-1-l} if node j is on the address path, and 0 otherwise.

```
BB-DECODE:
  for k = 0 .. q:                              # k = q is the token
    x = fresh
    if k < q: CX(b[q-1-k], x)  else: CX(c, x)  # inject
    row = [x]
    for l = 0 .. k-1:                          # route down one level
      next = []
      for j = 0 .. 2^l - 1:                    # all nodes of level l: disjoint, parallel
        y = fresh
        CCX(R[l][j], row[j], y)                # right child := x ∧ router
        CX(y, row[j])                          # x becomes the left child: x ∧ ¬router
        next += [row[j], y]                    # child 2j (bit 0), child 2j+1 (bit 1)
      row = next
    if k < q: R[k] = row                       # bit k is parked: the level-k routers
  F = row                                      # the token, routed to the leaves
```

- The routing operation is two controlled SWAPs on (incident, left, right), one under router
  |0⟩ and one under |1⟩ [paper: Hann et al. Fig. 1(b)]. Address qubits are routed into the
  tree [paper: Hann Fig. 1(d), App. A], and two-level routers start in |0⟩ [paper: Hann §V,
  App. B].
- We use a cheaper move [ours]. The children are always |0⟩ when a bit enters them, so the two
  CSWAPs (6 gates, plus X gates for the open control) reduce to the CCX+CX pair above, and the
  incoming qubit becomes the left child. That is Hann's routing with the qubits relabelled.
- The address is copied in by CX instead of swapped in, so `addr` is only ever a control
  [ours].
- Ancilla: Σ_{k=0}^{q} 2^k = **2N − 1**. Gates: (q+1) injections plus Σ_{k=1}^{q} 2(2^k − 1),
  which is **4N − q − 3** (= measured).

**Depth: D_BB = 3q** [ours] (= measured, q = 1..8).

Proof. All roots are fresh and every b_k is distinct, so all q+1 injections sit in layer 1.
Let T(k,l) be the layer in which bit k is complete at level l (T(k,0) = 1). The move of bit k
at level l has its CCX one layer after the latest of these:

- (i) its own qubit: T(k,l);
- (ii) the router: T(l,l);
- (iii) the CCX of bit k−1 at the same node, which shares the router as control. This applies
  only when k−1 > l.

The right child is fresh, so it imposes no constraint. Hence
T(k,l+1) = max(T(k,l), T(l,l), T(k−1,l+1) − 1) + 2. Induction gives T(1,1) = 3,
T(k,1) = k + 2, and T(k,l+1) = max(k+2l, 3l, k+2l) + 2 = k + 2(l+1) for k > l. So T(l,l) = 3l
and the token finishes at T(q,q) = 3q.

The parallel layers are named as follows. Layer t holds the CCX of every (k, l) with
k + 2l + 1 = t and the CX of every (k, l) with k + 2l + 2 = t, each over all 2^l nodes of level
l. This is Hann's pipelining, where a new address enters while earlier ones are still
descending. Without it the depth is Θ(q²) [paper: Hann App. A, Eq. (A1) gives Σ_l l =
O(log² N) for one-at-a-time injection, and even/odd-level parallelism gives O(log N)]. Our
schedule is not Hann's even/odd one; it is what the ASAP counter gives for the bit-major gate
list above [ours].

### 2.2 Fan-out decoder (FO), the measured alternative

The FO decoder copies each address bit to every node of its level, then splits flags level by
level with the same CCX+CX move.

```
FO-DECODE:
  p = fresh; CX(c, p)                           # token at the root
  for l = 0 .. q-1:                             # copies, all levels in parallel
    B[l][0] = b[q-1-l]; B[l][1 .. 2^l - 1] = fresh
    for s = 0 .. l-1: for i = 0 .. 2^s - 1: CX(B[l][i], B[l][i + 2^s])
  row = [p]
  for l = 0 .. q-1:
    next = []
    for j = 0 .. 2^l - 1:
      y = fresh; CCX(row[j], B[l][j], y); CX(y, row[j]); next += [row[j], y]
    row = next
  F = row
```

- Ancilla: N flags plus Σ_l (2^l − 1) copies, **2N − 1 − q** in all. Gates: **3N − q − 2**
  (= measured).
- **Depth: D_FO = 2q + 1** (= measured) [ours]. The token is placed in layer 1, and B[l] is
  complete by layer l. Level l's CCX is in layer 2l+2 and its CX in layer 2l+3, because its
  inputs are ready by 2l+1 ≥ l. Every B[l][j] is used once, so no control is shared.
- In Hann's fan-out picture, each address qubit controls all routers of its level [paper: Hann
  Fig. 1(c) caption]. The copy tree is what keeps that from serialising in our depth counter
  [ours].
- The archive's unary-iteration node takes three gates (right := parent ∧ idx; left := parent;
  left ^= right). Here the parent qubit becomes the left child, so a node takes two gates
  [ours]. The flag semantics are the archive's: `archive:src/kernels/qrom.c`, `cq_qtree_step`.

### 2.3 The flag plane

```
PLANE:  for a: G[a][0] = F[a]; G[a][1 .. w-1] = fresh
               for s = 0 .. L-1: for i < 2^s with i + 2^s < w: CX(G[a][i], G[a][i + 2^s])
```

The plane gives one copy of each flag per leaf bit, so no flag controls more than one gate per
layer. It costs (w−1)N ancilla, (w−1)N CX and L layers. All flags are ready in the same layer
(the decoder's last layer), so the plane is ready at **P = D + L**.

The alternative is no plane [ours, not measured]. Each flag then controls its w leaf gates in
sequence, so every leaf layer below becomes w layers (load: +2(w−1) − 2L). That saves (w−1)N
ancilla. The plane is the depth-first choice (NORTH_STAR, Circuits).

## 3. Leaf operations

The two shared-rail shapes are the parity tree and the broadcast [ours]. Neither ever puts two
gates on the shared rail in one layer.

```
TREE(xs):                    # in place: afterwards xs[0] = ⊕ xs; TREE reversed restores xs
  for s = 1, 2, 4, … < |xs|:
    for m = 0, 2s, 4s, … < |xs|: if m + s < |xs|: CX(xs[m+s], xs[m])

LOAD(y):                     # y_i ^= cell bit i of the flagged leaf
  for i < w: X_i = []
    for a < N:
      HOST bit 1:   X_i += G[a][i]                              # no gate at all
      HOST bit 0:   (nothing)
      QUANTUM:      t = fresh; emit CCX(G[a][i], κ(a,i), t); X_i += t
  all CCX above; TREE(X_i) for all i; CX(X_i[0], y_i) for nonempty X_i;
  TREE(X_i) reversed for all i; the same CCX again

BCAST(s, U):                 # κ(a,i) ^= G[a][i] ∧ s_i for every leaf; every cell is QUANTUM
  for i < w: U_i[0] = s_i; U_i[1 .. N-1] = U given, or fresh
             for r = 0 .. q-1: for j < 2^r: CX(U_i[j], U_i[j + 2^r])     # copy s_i to N places
  for i < w, a < N: CCX(G[a][i], U_i[a], κ(a,i))
  for i < w: the copy loop reversed
```

The accesses below. Each runs DECODE; PLANE; body; PLANE reversed; DECODE reversed. The
reverses are the same gate lists backwards, because every gate is self-inverse.

| access | body |
|---|---|
| load (h fresh) | `LOAD(h)` |
| load `_unc` | the identical gate list (it is an involution while addr and c hold their forward values) |
| swap | QUANTIZE; `BCAST(h, —) → U`; `LOAD(h)`; `BCAST(h, U)` |
| store (t = fresh tape slot) | QUANTIZE; `LOAD(t)`; `CX(v_i, t_i)` all i; `BCAST(t, —)`; `CX(v_i, t_i)` all i |

QUANTIZE applies the overlay rule (ABI §1.10, Overlay). A swap or store at a quantum address
makes every reachable cell QUANTUM. Each HOST cell in R gets 8 qubits and an X on its 1-bits
(the emitter's "gets a qubit"). These are cell qubits, not ancillae.

Correctness [ours, and checked by §9]:

- **Load.** X_i holds f_a ∧ cell bit for every a (HOST 0-bits contribute 0). One flag is set,
  so ⊕X_i is the flagged cell's bit.
- **Swap.** This is the three-XOR swap, restricted to the flagged leaf: cell ^= h, then h ^= cell
  (so h = old cell), then cell ^= h (so cell = old h). Unflagged leaves see only CCX with a 0
  control.
- **Store.** It follows `archive:src/kernels/qstore.c` sweeps 0–2: tape ^= leaf ∧ cell, then
  cell ^= leaf ∧ tape, then cell ^= leaf ∧ val. Sweeps 1 and 2 are merged into one BCAST of
  t ⊕ v. At the flagged leaf the cell becomes c ⊕ c ⊕ v = v, and t holds the displaced c.
  The swap's first BCAST would be BCAST(t = 0), which is a no-op, so it is dropped.
- **Under c = 0.** Every flag is 0, so no cell changes and h and t stay |0⟩.

## 4. Depth

All of this is [ours], and "= measured" means equal to the simulator for every case run.

Assumptions: every reachable cell is QUANTUM, and the leaves are disjoint (m = 1, §4.4). The
plane is ready at layer P = D + L, with D = 3q (BB) or 2q + 1 (FO).

| access | depth | BB, w = 8 | FO, w = 8 |
|---|---|---|---|
| load | 2P + 2q + 3 | 8q + 9 | 6q + 11 |
| swap | 2P + 2q + 5 | 8q + 11 | 6q + 13 |
| store | 2P + 2q + 4 | 8q + 10 | 6q + 12 |
| load, all HOST | 2P + max_i(2⌈log2 n_i⌉ + 1) | 8q + 7 (every n_i = N) | 6q + 9 |

Here n_i = |X_i|, the number of leaves that feed bit i (HOST 1-bits and QUANTUM bits).
A bit with n_i = 0 has no gates. With both HOST and QUANTUM inputs the load is
2P + max_i(2⌈log2 n_i⌉ + 1 + 2·[bit i has a QUANTUM input]). That mixed case is derived, not
measured.

### 4.1 Load

The steps run in this order:

| layer | step |
|---|---|
| P+1 | t CCX |
| P+2 .. P+q+1 | TREE round r |
| P+q+2 | CX(X_i[0], h_i) |
| P+q+3 .. P+2q+2 | TREE reversed |
| P+2q+3 | t CCX |

Every layer is a single parallel layer:

- all w trees act on disjoint qubits;
- within a tree, round r touches each element at most once;
- h_i is touched once per bit.

The plane and decoder reverses then take L + D. Total: 2P + 2q + 3.

The parity tree's uncompute is the same in-place tree reversed. It costs q, with no second
tree and no extra ancilla. It must finish before the t uncompute, which is why the formula has
2q and not q.

### 4.2 Swap

| layer | step |
|---|---|
| 1 .. q | copy tree of h; independent of the flags, so hidden under the decoder (q ≤ P) |
| P+1 | BCAST CCX (cell ^= f·h) |
| P+2 | load t CCX (needs the cell just written) |
| P+2 .. P+q+1 | h uncopy; h is in every round, so this is the critical use of the shared rail |
| P+3 .. P+q+2 | TREE |
| P+q+3 | CX into h (after both the tree and h's uncopy) |
| P+q+4 .. P+2q+3 | TREE reversed, in parallel with the second copy tree of h, which reuses U |
| P+2q+4 | t CCX |
| P+2q+5 | BCAST CCX (cell ^= f·h) |
| P+2q+6 .. P+3q+5 | uncopy of h, hidden under the plane and decoder reverses (q ≤ P) |

Total: 2P + 2q + 5.

### 4.3 Store

| layer | step |
|---|---|
| P+1 .. P+2q+3 | LOAD(t) |
| P+q+3 | CX(v, t), right after the CX into t |
| P+q+4 .. P+2q+3 | copy tree of t, in parallel with the untree |
| P+2q+4 | BCAST CCX, after the cell's last load use |
| afterwards | uncopy and CX(v, t), hidden (q + 1 ≤ P) |

Total: 2P + 2q + 4.

So every access is **O(q + log w)**, which is O(q) for a fixed access width. No step of the
QRAM.md outline was left super-linear, but two of its rows had to be replaced (§7).

### 4.4 Overlapping leaves

Two reachable leaves share a cell exactly when 2^{p_0} < S. The addresses differ by multiples
of 2^{p_0}, so this needs a multi-cell access with a low quantum bit. Let m be the largest
number of leaves that contain one cell, so m ≤ min(N, ⌈S / 2^{p_0}⌉).

A cell qubit is then a control (load) or a target (BCAST) of m gates, which serialise. Each
cell-touching layer becomes at most m layers:

- load: +2(m−1) at most;
- swap: +4(m−1) at most;
- store: +3(m−1) at most.

Measured with leaf bases one apart (FO):

| m | S | load | swap | store |
|---|---|---|---|---|
| 2 | 2 | +1 | +3 | +2 |
| 2 | 4, 8 (q = 1) | +2 | +4 | +3 |
| 4 | 4 | +3 | +9 | +6 |
| 4 | 8 | +6 | +12 | +9 |
| 8 | 8 | +7 | +21 | +14 |

All of these are within the bound. Since m ≤ 16 (S ≤ 16), the bound stays O(q).

## 5. Counts

Totals cover the whole call: 2·(decoder + plane) + body. Gate totals exclude QUANTIZE (one X
per 1-bit of a converted HOST cell, all in layer 1, depth-neutral). All rows assume every cell
is QUANTUM and the leaves are disjoint. All formulas are = measured.

| | gates | ancilla |
|---|---|---|
| BB decoder | 4N − q − 3 | 2N − 1 |
| FO decoder | 3N − q − 2 | 2N − 1 − q |
| plane | (w−1)N | (w−1)N |
| load body | 4wN − w | wN (t) |
| load body, all HOST | Σ_i (2n_i − 1) over n_i ≥ 1 | 0 |
| swap body | 10wN − 5w | wN (t) + w(N−1) (U, shared by both BCASTs) |
| store body | 7wN − w | wN (t) + w(N−1) (U) |

Ancilla per access, BB (subtract q for FO):

- load: (2w + 1)N − 1;
- swap and store: (3w + 1)N − w − 1;
- all-HOST load: wN + N − 1.

The ancilla cost is therefore O(w·2^q), not O(2^q) as QRAM.md line 21 says. These are
distinct qubits, not a peak. The swap already reuses U between its BCASTs at no depth cost.
Sharing t with U is possible but would add dependencies, and it is not done.

## 6. What known bits remove [ours]

- **Classical address bits** only fix `base` and therefore A(a). They take no tree level, no
  gate and no qubit. The tree's size is set by q alone.
- **HOST cells in a load:**
  - a HOST 1-bit feeds its plane copy G[a][i] straight into the parity tree, with no t and no
    CCX;
  - a HOST 0-bit is left out of the tree;
  - bit i's tree has n_i inputs, so it has depth ⌈log2 n_i⌉ and n_i − 1 CX each way;
  - an all-HOST load drops the two t layers, and an all-HOST load whose bytes are all 0 emits
    nothing.

  Not done, and not measured: plane copies G[a][i] that feed no tree could also be skipped.
  An all-HOST load over a power-of-two block is the ABI's table lookup.
- **HOST cells in a swap or store:** none. The overlay makes every reachable cell QUANTUM
  first (QUANTIZE).
- **A classical control.** c = 1 is the plain form. With c = 0 the call emits nothing at the
  ABI level, so no host byte is read. With a quantum c, the circuit builder must read every
  HOST byte of R to build the gates.
- **q = 0** is the `_hl` path (ABI §1.10, Impl), not this construction.

## 7. Corrections to docs/QRAM.md

1. **Swap row.** "Route h_i down the tree to its leaf slot, CSWAP(f_a; slot_a, cell_a), route
   back" does not stay shallow for a w-bit h. Every router would control w CSWAPs, which is
   O(q·w), or O(q + w) with bits pipelined through single-bit modes. It also needs w·N leaf
   slots. It is replaced by the three-XOR swap of §3 (BCAST, LOAD, BCAST), at depth
   2P + 2q + 5.
2. **Store row.** The XOR of v into the cell is broadcast-shaped (v fanned out and masked by
   flags), not load-shaped (the load's parity tree writes the shared rail, not the cell). Also,
   the swap with the fresh tape slot reduces to a load into t followed by the BCAST.
3. **"Address bit k is routed down k levels".** This is right, but done one bit at a time it is
   Θ(q²). The bit-major list of §2.1 under ASAP gives the pipelined 3q.
4. **"Fan-out decoder: same O(q) depth … fewer gates".** In this logical model FO is also
   shallower: 2q + 1 against 3q, so 2(q − 1) less per access. It also has N − 1 fewer gates
   and q fewer ancillae per decoder pass. BB's advantage is resilience to physical router
   noise [paper: Hann §V; two-level routers are resilient but less so than three-level],
   which a logical circuit does not use. Under the depth-first rule FO is the decoder to
   ship. That is a decision for the user (§10).
5. **Hann "the CSWAP circuit form we follow".** No CSWAP survives. The routing move is CCX+CX
   (§2.1).
6. **"Ancilla cost is O(2^q)".** It is O(w·2^q) (§5).

## 8. Literature actually read

- **Hann, Lee, Girvin, Jiang**, arXiv:2012.05340. Read from the arXiv HTML and PDF text: §II,
  §V, the Fig. 1 caption, App. A and App. B. Checked:
  - the router as two CSWAPs under |0⟩ and |1⟩ (Fig. 1b);
  - fan-out QRAM, where each address qubit controls a whole level (Fig. 1c);
  - address qubits routed into the tree;
  - O(log² N) for one-at-a-time injection, and O(log N) with even/odd-level parallel routing
    (App. A, Eq. A1);
  - two-level routers initialised to |0⟩;
  - data copied by classically controlled gates on all bottom-level modes, with the bus routed
    back out (App. B);
  - two-level routers are polylog-resilient but less so than three-level ones (§V).

  Hann's two-level load uses a |+⟩ bus and Z gates for "no extra copying". That cannot be
  expressed in X/CX/CCX, which is one reason our load uses flags and a parity tree instead.
- **Paler, Oumarou, Basmadjian**, arXiv:2002.09340. Read from the PDF text: §I, §II, §III A–G.
  Checked:
  - a QRAM circuit is FANOUT, QUERY and FANIN;
  - QUERY is Toffolis on distinct (b_i, m_i) pairs with one shared target;
  - their QUERY parallelism comes from a Clifford+T decomposition of Toffolis that share a
    wire (§III B–D);
  - they count CNOTs that share a control as parallel (§III A, Fig. 6).

  Neither holds in our X/CX/CCX depth counter, so **nothing is taken from this paper**. The
  parity tree and BCAST are what keep the shared rail from serialising here.
- Not read beyond abstract, and not relied on: Giovannetti et al. (both), Arunachalam et al.,
  Di Matteo et al., Babbush et al., Low et al., Jaques and Rattew.
- `archive:src/kernels/qrom.c` and `qstore.c` were read. They are the reference for the flag
  semantics and the store sweeps (§2.2, §3).

## 9. Check that was run

A throwaway Python simulator, kept outside the repo, implements:

- bits as 0/1, with X, CX and CCX;
- an ASAP depth counter over all qubits;
- the gate lists of §2 and §3, generated exactly as written.

Checks:

- **Correctness**, for q = 1..4, both decoders, load/swap/store, controlled and uncontrolled,
  S = 1 and S = 2. For S = 2 the leaves overlap (p_0 = 0).
  - Quantum positions are random in 0..63 with a random `base`, so wrap included.
  - Cells are a random half HOST and half QUANTUM.
  - Every basis address, c ∈ {0, 1}, three random contents each.
  - Asserted:
    - load: h = the cells, cells unchanged;
    - swap: h ↔ cells;
    - store: tape = old cells, cells = v;
    - c = 0: nothing changed;
    - every ancilla 0, addr and v unchanged.

  All pass.
- **Formulas.** Depth, gate count and ancilla count were compared with §4/§5 for every access
  kind, both decoders, controlled and not, at w = 8 (q = 1..8), w = 16 and w = 64 (q = 1..4),
  plus the all-HOST (0xff) load at q = 1..8. Zero mismatches.

Measured at w = 8 (S = 1), all cells QUANTUM, uncontrolled (controlled is identical):

| q | load BB | load FO | swap BB | swap FO | store BB | store FO | HOST load BB | HOST load FO |
|---|---|---|---|---|---|---|---|---|
| depth 1 | 17 | 17 | 19 | 19 | 18 | 18 | 15 | 15 |
| depth 2 | 25 | 23 | 27 | 25 | 26 | 24 | 23 | 21 |
| depth 3 | 33 | 29 | 35 | 31 | 34 | 30 | 31 | 27 |
| depth 4 | 41 | 35 | 43 | 37 | 42 | 36 | 39 | 33 |
| gates 1 | 92 | 90 | 156 | 154 | 140 | 138 | 60 | 58 |
| gates 2 | 198 | 192 | 358 | 352 | 294 | 288 | 134 | 128 |
| gates 3 | 412 | 398 | 764 | 750 | 604 | 590 | 284 | 270 |
| gates 4 | 842 | 812 | 1578 | 1548 | 1226 | 1196 | 586 | 556 |
| ancilla 1 | 33 | 32 | 41 | 40 | 41 | 40 | 17 | 16 |
| ancilla 2 | 67 | 65 | 91 | 89 | 91 | 89 | 35 | 33 |
| ancilla 3 | 135 | 132 | 191 | 188 | 191 | 188 | 71 | 68 |
| ancilla 4 | 271 | 267 | 391 | 387 | 391 | 387 | 143 | 139 |

## 10. Open

- **Choice of decoder.** BB as QRAM.md chose it, or FO as the depth-first rule picks (§7.4).
  Filed as kmgy.27.
- **QRAM.md.** Its swap and store rows and the ancilla bound should be brought in line with §7
  by whoever owns that file. This note supersedes them for the construction.

## 11. The object bound [ours]

ABI §1.10 "Objects": where the address rail's note names objects X_1..X_m (end entries dropped)
and that gives fewer leaves than 2^q (and at most 2^CQ_MAX_QADDR_BITS), src/qram.c bounds R by
them; otherwise the bit-pattern path above runs unchanged (same gates, same cap and abort).

- Per object X = [s, s + n): d = addr − s by the adder `sub` ships at i64 (s classical), recorded
  on tape A, k = ⌈log2 n⌉. Which bits of d are known is decided before any gate, by ripple
  carries over {0, 1, unknown}: with independent address qubits and a classical s, a bit is
  unknown exactly where it is not constant, so a known bit holds that constant in every basis
  state (the adder may leave it a qubit; it is then not read). A known 1 at or above k: the
  address is never in X (m = 1: the bit pattern).
- Leaves: offsets L | (assignment of the unknown bits below k) that are < n, L the known low
  bits. The offset grows with the assignment, so the kept ones are a prefix [0, cnt); the decoder
  of §2.2 never splits a node whose 1-child holds no kept leaf. That node's flag then also covers
  offsets ≥ n, which for one object lie outside the domain (CR §0).
- Intersection with the bit pattern: the ripple loses a classical address bit above a quantum
  one (the borrow is unknown), so of the cnt offsets only those whose address s + offset agrees
  with every classical address bit become leaves. A dropped offset's flag is still in the tree
  (two gates, one qubit) but never fires in the domain, reads no cell and marks none QUANTUM.
- m ≥ 2 objects with leaves: the address fanned out, the subtractions side by side, and each root
  token is c ∧ [d <u n] (a tree compare): it stops an address in one object from reaching a leaf
  of another, including through an unsplit node. [d >> k = 0] alone would not.
- All kept leaves form one list: §3's operations, the §4.4 sharing, the overlay (exactly the kept
  leaves' cells) and the load rule's note (kept leaves only) run on it unchanged. The undo of
  tape A takes back decode, compare and subtraction and returns their qubits unmeasured.

Measured (counting backend, one uncontrolled i64 load, address = s + offset as the add
template leaves it with s 8-aligned but not 2^k-aligned: quantum bits 3..k, or 0..k for byte
addresses with s odd; depth / qubits, object path vs bit pattern):

| object | address | leaves | depth | qubits | bit pattern: leaves, depth, qubits |
|---|---|---|---|---|---|
| 2 B | byte | 2 | 30 | 330 | 4, 26, 317 |
| 64 B | low 3 known | 8 | 49 | 735 | 16, 38, 1103 |
| 64 B | byte | 64 | 69 | 4382 | 128, 56, 8383 |
| 100 B | low 3 known | 13 | 55 | 1061 | 32, 44, 2143 |
| 100 B | byte | 100 | 73 | 6722 | 256, 62, 16703 |
| 4096 B | low 3 known | 512 | 89 | 33499 | 1024, 74, 66623 |
| 4096 B | byte | 4096 | 109 | 266465 | 8192, 92, 532543 |
| 64 KiB | low 3 known | 8192 | 113 | 532702 | 16384, 98, 1065023 |
| 2 × 64 B | low 3 known | 16 | 82 | 1662 | 32, 44, 2143 |
| 4 × 64 B | low 3 known | 32 | 87 | 3264 | 64, 50, 4223 |

Depth grows by about 6 per doubling of the leaves, as §4 (6 per address bit); the subtraction
and its undo add a constant 11 to 17 layers for one object; the compare of a union about 30 more.
