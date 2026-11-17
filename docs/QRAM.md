# QRAM: what we build and where it comes from

Status: design outline, corrected 2026-11-17. The contract is BACKEND_ABI §1.10 (one bus, 2^64
byte cells, HOST/QUANTUM overlay). This file fixes the decoder family and lists the literature.
Gate lists, counts and depth proofs are in `docs/QRAM_CONSTRUCTION.md` and are not repeated
here; where this file and that note differ, the note wins. Decision (user, 2026-11-17, kmgy.27):
the fan-out decoder ships.

## Requirements

1. Gates: X, CX, CCX only. Every ancilla back to |0⟩ before the call returns.
2. Depth O(q) for an access whose address rail has q quantum bits (N = 2^q reachable cells).
   Linear-depth decoders (unary iteration) are not acceptable for the quantum-address path.
3. Cells may hold classical bytes (HOST) or qubits (QUANTUM); load, store and swap share one
   decoder.
4. Verified by classical replay only: sampled addresses and contents, result compared with a
   host byte array. Depth is checked by the replay backend's depth counter, not by argument.

## The reachable set

An address rail is 64 tri-valued bits. Its classical bits are known, so the cells it can reach
are the 2^q assignments of its q quantum bits. q = 0 is the `_hl` path: no decoder at all.
q above a configured cap is a hard error. The cap is a build-time resource limit of ours, not
part of the ABI: ancilla cost is O(w·2^q) for an access touching w bits.

This bound is ours (it follows from the lazy bit, ABI §0 and §1.10 "the bound it can prove").
It is not from the literature.

## Decoder: fan-out

Chosen: the fan-out decoder. Each address bit is copied to every node of its tree level by a
CX tree, and a |1⟩ token (the control, if any) is split level by level with a CCX+CX move. The
result is one flag per leaf, exactly one set when the control is 1. The tree is unloaded in
reverse after the cell operations. We emit logical circuits, so the bucket brigade's only
advantage, resilience to physical router noise, is not used. The project rule is that the
shallowest construction ships.

Cell operations, all leaves in parallel, flag f_a as control (ABI §1.10 `_hl_controlled`):

| access | what it does |
|---|---|
| load | per leaf and bit, t ^= f_a ∧ cell bit (HOST cell: the flag copy at the 1-bits, no gate); the parity of the t over all leaves is XORed into h_i by an in-place CX tree, then uncomputed |
| swap | three XOR steps restricted to the flagged leaf: cell ^= h, h ^= cell, cell ^= h. No routing of h down the tree |
| store | load of the old cell into the fresh tape slot, then v XORed in and the result broadcast to the cells: the XOR into the cell is broadcast-shaped (v fanned out, masked by flags), not load-shaped |

The broadcast and the parity tree are what keep the shared rail h from serialising the leaves.
Each address bit is loaded by a pipelined schedule, not one bit at a time: one at a time is
O(q²). A per-leaf flag copy plane keeps any one flag from controlling more than one gate per
layer. No CSWAP survives in the circuit; the routing move is CCX+CX. These are our composition
of standard parts, not a circuit taken from one paper. Gate lists and the depth proof:
QRAM_CONSTRUCTION.md §2 to §4.

Measured comparison per decoder pass (QRAM_CONSTRUCTION.md §2, §5, all = measured):

| | depth | gates | ancillae |
|---|---|---|---|
| fan-out | 2q + 1 | 3N − q − 2 | 2N − 1 − q |
| bucket brigade | 3q | 4N − q − 3 | 2N − 1 |

Fan-out is 2(q − 1) shallower per access, with N − 1 fewer gates and q fewer ancillae per
decoder pass. Whole-access depth at w = 8: load 6q + 11 against 8q + 9, swap 6q + 13 against
8q + 11, store 6q + 12 against 8q + 10 (§4). Ancilla per access is O(w·2^q) for both, not
O(2^q) (§5).

Alternative kept on file: the bucket brigade (two-level routers, address bit k routed down k
levels and parked in the level-k routers, in the pipelined order that gives depth 3q). Its
advantage is resilience to physical router noise (Hann §V), which a logical circuit does not
use. It is not shipped; the note's §2.1 has its gate list if the decision is ever reopened.

Not used on the quantum-address path: unary iteration (linear depth, O(q) ancilla). It stays
the reference for flag semantics and is what `archive:src/kernels/qrom.c` and `qstore.c`
implement (from Bennett.jl `src/qrom.jl`, `src/shadow_memory.jl`). Those two files are the
starting point for the leaf operations and the tests, not for the decoder.

## References

Verified to exist under these identifiers on 2026-11-17. "Used for" says what was checked, per
QRAM_CONSTRUCTION.md §8. Two papers were read in part; the rest are verified to exist only and
nothing is relied on from them.

| ref | used for |
|---|---|
| Giovannetti, Lloyd, Maccone, *Quantum random access memory*, PRL 100, 160501 (2008), arXiv:0708.1879 | the bucket-brigade architecture. Exists only; not read beyond abstract |
| Giovannetti, Lloyd, Maccone, *Architectures for a quantum random access memory*, arXiv:0807.4994 | fan-out vs bucket brigade. Exists only; not read beyond abstract |
| Hann, Lee, Girvin, Jiang, *Resilience of quantum random access memory to generic noise*, PRX Quantum 2, 020311 (2021), arXiv:2012.05340 | read (§II, §V, Fig. 1 caption, App. A, App. B). Checked: router as two CSWAPs; fan-out QRAM, each address qubit controlling a whole level; address qubits routed into the tree; O(log² N) for one-at-a-time injection and O(log N) with even/odd-level routing; two-level routers start in \|0⟩; two-level routers less noise-resilient than three-level. We do not use its CSWAP form (no CSWAP survives) or its \|+⟩ bus with Z gates (not expressible in X/CX/CCX) |
| Arunachalam, Gheorghiu, Jochym-O'Connor, Mosca, Srinivasan, *On the robustness of bucket brigade quantum RAM*, arXiv:1502.03450 | exists only; not read beyond abstract |
| Di Matteo, Gheorghiu, Mosca, *Fault tolerant resource estimation of quantum random-access memories*, IEEE TQE (2020), arXiv:1902.01329 | exists only; not read beyond abstract. Intended as a cross-check for our counts, not yet done |
| Paler, Oumarou, Basmadjian, *Parallelizing the queries in a bucket-brigade quantum RAM*, PRA 102, 032608 (2020), arXiv:2002.09340 | read (§I, §II, §III A–G). Its query parallelism relies on a Clifford+T decomposition and on counting CNOTs that share a control as parallel; neither holds in our X/CX/CCX depth counter. Nothing is taken from it |
| Babbush, Gidney, Berry, Wiebe, McClean, Paler, Fowler, Neven, *Encoding electronic spectra in quantum circuits with linear T complexity*, arXiv:1805.03662, §III.A, §III.C | unary iteration and QROM: flag semantics, the archive kernels. Not read beyond abstract; reached via Bennett.jl `src/qrom.jl` |
| Low, Kliuchnikov, Schaeffer, *Trading T gates for dirty qubits in state preparation and unitary synthesis*, Quantum 8, 1375 (2024), arXiv:1812.00954 | select-swap; noted, not planned. Exists only |
| Jaques, Rattew, *QRAM: a survey and critique*, Quantum (2025), arXiv:2305.10310 | overview of the above. Exists only; not read beyond abstract |
