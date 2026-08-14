# Bennett.jl Literature Survey — Full LLVM IR Reversible Compiler

Survey date: 2026-04-10

---

## Pebbling and Time-Space Tradeoffs

### [BENNETT89] Bennett (1989) — Time/Space Trade-Offs for Reversible Computation
- **arXiv/DOI**: doi:10.1137/0218053 (SIAM J. Computing 18(4):766-776)
- **PDF status**: downloaded (pebbling/Bennett1989_time_space_tradeoffs.pdf)
- **Category**: PEBBLING
- **Key idea**: Reversible computation can simulate irreversible computation with
  polynomial time overhead. Introduces the pebbling game on computation DAGs: a node
  can be pebbled (computed) only if all inputs are pebbled; it can be un-pebbled
  (uncomputed) only if all inputs are still pebbled. Full Bennett = pebble everything,
  copy output, un-pebble everything.
- **Relevance to Bennett.jl**: This is the foundational algorithm we currently implement.
  Understanding the tradeoffs is critical for v0.8 optimization.
- **Verified claims**:
  - **Theorem 1 (p.768)**: "For any ε > 0, any multitape Turing machine running in
    time T and space S can be simulated by a reversible input-saving machine using
    time O(T^{1+ε}) and space O(S · log T)."
  - **Lemma 1 (p.767)**: Full Bennett = linear time O(T) but space O(S+T). "uses an
    extra tape, initially blank, to record all the information that would have been
    thrown away by the irreversible computation being simulated."
  - **Recursive strategy (p.773)**: RS(z,x,n,m,d) = three recursive calls: compute
    first half, compute second half, uncompute first half. "by hierarchically iterating
    the simulation of 2 segments... 2^n segments can be simulated in 3^n stages."
  - **Corollary (p.770)**: Space-only bound O(S^2) for reversible simulation.
- **Cites/cited-by**: [KNILL95], [MEULI19], [PRS15]

### [KNILL95] Knill (1995) — An analysis of Bennett's pebble game
- **arXiv/DOI**: arXiv:math/9508218
- **PDF status**: downloaded (pebbling/Knill1995_bennett_pebble_analysis.pdf)
- **Category**: PEBBLING
- **Key idea**: Provides a recursion for the time-optimal solution of Bennett's pebble
  game given a fixed space bound, and derives an explicit asymptotic expression for
  the best time-space product.
- **Relevance to Bennett.jl**: The recursion is the algorithm for Knill pebbling
  (v0.8 item). Gives the optimal strategy for a given ancilla budget.
- **Verified claims**:
  - **Theorem 2.1**: Exact recursion: F(n,S) = min over m of
    F(m,S) + F(m,S-1) + F(n-m,S-1). Three terms = forward + unforward + continue.
  - **Theorem 2.3**: "F(n,S) < infinity iff n <= 2^{S-1}" — minimum pebbles needed.
  - **Theorem 2.12**: Optimal time-space product: TS(n) = 2^{2√(log n)(1+o(1))} · n.
  - **Practical**: Setting S = c·√(log n) gives polynomial overhead n^{1+ε}.
    For S ≈ 3-8× log(n), time overhead is modest.
  - Key difference from Bennett: gives **exact** recursion (both bounds), not just upper.
- **Cites/cited-by**: [BENNETT89], [PRS15], [MEULI19]

### [MEULI19] Meuli, Soeken, De Micheli (2019) — Reversible Pebbling Game for Quantum Memory Management
- **arXiv/DOI**: arXiv:1904.02121
- **PDF status**: downloaded (pebbling/Meuli2019_reversible_pebbling.pdf)
- **Category**: PEBBLING
- **Key idea**: Casts the reversible pebbling game as a SAT problem. Boolean variables
  p_{v,i} = "node v is pebbled at time step i". Constraints encode pebbling rules +
  resource limits. SAT solver finds optimal strategy for a given ancilla budget.
  Achieved 52.77% average ancilla reduction on benchmarks.
- **Relevance to Bennett.jl**: SAT-based pebbling for v0.8. The dependency graph nodes
  are computation steps; edges are data dependencies. Directly applicable to our
  gate sequences.
- **Verified claims**:
  - Variables p_{v,i} = "node v pebbled at time i". K+1 configurations, K transitions.
  - Move clauses: "(p_{v,i} XOR p_{v,i+1}) → (p_{w,i} AND p_{w,i+1})" for edges (v,w).
  - Cardinality: at most P pebbles per step.
  - "Average percentage reduction of pebbles = 52.77%", "average multiplicative
    factor for number of steps = 2.68."
  - Benchmarks: 8 to 1151 nodes, runtimes 0.01s to ~134s, 2-minute timeout.
  - SAT solver: Z3.
  - DAG nodes = operations, edges = data dependencies, primary inputs not in DAG.
- **Cites/cited-by**: [BENNETT89], [KNILL95], [PRS15]

### [PRS15] Parent, Roetteler, Svore (2015) — Reversible circuit compilation with space constraints
- **arXiv/DOI**: arXiv:1510.00377
- **PDF status**: downloaded (pebbling/ParentRoettelerSvore2015_space_constraints.pdf)
- **Category**: PEBBLING
- **Key idea**: Introduces REVS compiler (F# → Toffoli networks) with Mutable Data
  Dependency graph (MDD). MDD distinguishes data dependencies (dashed arrows) from
  mutations (solid arrows). Two cleanup strategies: EAGER (uncompute as soon as
  dependents done) and INCREM (incremental checkpointing). Achieves ~4x space reduction
  over Bennett with same gate count.
- **Relevance to Bennett.jl**: MOST DIRECTLY RELEVANT PAPER. The MDD concept maps to
  our wire tracking. EAGER cleanup is the next optimization to implement. The REVS
  architecture (AST → MDD → pebbling → Toffoli) parallels our pipeline (LLVM IR →
  ParsedIR → lower → bennett).
- **Verified claims**:
  - "the eager cleanup method comes within a space overhead of roughly 33% over the
    hand optimized adder which is better than the overhead of roughly 66% for
    Bennett's method" (Table I caption, p.15, verified)
  - SHA-2 10 rounds: Bennett 1856 qubits vs Eager 353 qubits = 5.3x reduction
    (Table II, p.18, verified)
  - "the number of gates turns out to be the same for all three methods" for adders
    (Table I, p.15, verified — all show 154 gates for n=40)
  - Algorithm 1: MDD construction from AST, O(n) (p.7, verified)
  - Algorithm 2: EAGER cleanup, reverse topological order (p.11, verified)
  - Algorithm 3: INCREM cleanup with checkpointing (p.13, verified)
  - Theorem 2: Correctness of eager cleanup for pairwise one-way dependent mutation
    paths (p.12, verified)
  - Gate types: RTOFF (Toffoli), RCNOT, RNOT — same as Bennett.jl (p.13, verified)
  - In-place addition: (a,b,0) → (a,b,a+b) needs 3n+1 qubits; in-place:
    (a,b) → (a,a+b) needs fewer (p.14, verified)
- **Cites/cited-by**: [BENNETT89], [KNILL95]

### [REQOMP24] Paradis et al. (2024) — Reqomp: Space-constrained Uncomputation for Quantum Circuits
- **arXiv/DOI**: arXiv:2205.00724, doi:10.22331/q-2024-02-19-1258
- **PDF status**: downloaded (pebbling/Reqomp2024_uncomputation.pdf)
- **Category**: PEBBLING
- **Key idea**: Lifetime-guided uncomputation that reduces ancilla qubits by analyzing
  when intermediate values are no longer needed. Claims up to 96% qubit reduction.
- **Relevance to Bennett.jl**: Complementary to MDD eager cleanup. Could be applied
  after our gate sequence is generated.
- **Verified claims**: (awaiting agent reading — to be filled)
- **Cites/cited-by**: [BENNETT89], [PRS15], [MEULI19]

---

## Memory Model and Functional Data Structures

### [OKASAKI99] Okasaki (1999) — Red-Black Trees in a Functional Setting
- **arXiv/DOI**: J. Functional Programming 9(4):471-477
- **PDF status**: downloaded (memory/Okasaki1999_redblack.pdf)
- **Category**: FUNCTIONAL_DS
- **Key idea**: Elegant functional implementation of red-black trees using pattern
  matching. Insert creates a new tree version preserving the old — persistent by
  construction. Balance maintained via 4-case pattern match on red-red violations.
- **Relevance to Bennett.jl**: Persistent red-black trees are the leading candidate
  for reversible memory model. Every store creates a new version; the old version is
  the ancilla state for Bennett uncomputation. O(log N) per operation.
- **Verified claims**:
  - Data type: `data Color = R | B; data Tree elt = E | T Color (Tree elt) elt (Tree elt)`
  - Insert: `ins` creates red node, `balance` handles 4 red-red cases with single RHS:
    `T R (T B a x b) y (T B c z d)`.
  - "every tree is balanced... the longest possible path... is no more than twice as
    long as the shortest possible path" → O(log n) operations.
  - Persistence: "create new nodes rather than modifying old ones" — old tree intact.
  - Maps to Bennett: each insert = new tree version = "pebble", uncompute = drop version.
    O(log n) path-copying per step gives space-efficient checkpointing.
- **Cites/cited-by**: [AG13], [AG18]

### [AG13] Axelsen, Glück (2013) — Reversible Representation and Manipulation of Constructor Terms in the Heap
- **arXiv/DOI**: doi:10.1007/978-3-642-38986-3_9
- **PDF status**: downloaded (memory/AxelsenGluck2013_reversible_heap.pdf)
- **Category**: MEMORY_MODEL
- **Key idea**: Shows how to represent and manipulate algebraic data types (lists,
  trees) in a reversible heap. Allocation/deallocation are reversible operations.
  Constructor terms stored with explicit pointers; deallocation reverses allocation.
- **Relevance to Bennett.jl**: CRITICAL for memory model design. Shows that heap
  operations CAN be made reversible for structured data. Directly addresses our
  load/store challenge.
- **Verified claims**:
  - Heap cells are 3 words: constructor field + left/right child pointers.
  - **Linearity**: "each cons cell has reference count exactly one, i.e., the heap
    is linear." Guaranteed by RFUN's distinct-variable binding.
  - **EXCH (swap)** replaces load/store: "exchanges contents of register and memory
    location" — preserves information bidirectionally.
  - Allocation: `get_free()` from free list or grow heap. Deallocation is inverse.
  - Key invariant: "last element of free list must never be cons cell immediately
    above heap pointer" — provides orthogonalizing condition for reversible if-then-fi.
  - "garbage collection will be automatically performed simply by maintaining the
    heap structure across updates" via linearity + reversibility.
  - Free list can be eliminated via Bennett trick.
- **Cites/cited-by**: [AG18], [THOMSEN12]

### [AG18] Axelsen, Glück (2018) — Reversible Garbage Collection for Reversible Functional Languages
- **arXiv/DOI**: doi:10.1007/s00354-018-0037-3
- **PDF status**: downloaded (memory/AxelsenGluck2018_reversible_gc.pdf)
- **Category**: MEMORY_MODEL
- **Key idea**: Reversible garbage collection — reclaiming memory without destroying
  information. Shows that clean reversible simulation of injective programs is possible
  without returning the input as additional output.
- **Relevance to Bennett.jl**: Addresses the fundamental problem: how to free ancillae
  (= garbage collect) in a reversible context. Bridges functional programming GC with
  Bennett's uncomputation.
- **Verified claims**:
  - NOTE: Paper is by Mogensen 2018 (extends Axelsen/Gluck's heap manager).
  - **Maximal sharing**: "If a newly constructed node is identical to an already
    existing node, we return a pointer to the existing node (increasing its reference
    count) instead of allocating a new node."
  - Deallocation = running `cons` in reverse. "Whenever a node is taken apart in a
    pattern, the node can immediately be freed."
  - Target language: RFUN → RIL (Reversible Intermediate Language, Janus-variant).
  - **Hash-consing**: Jenkins' 96-bit reversible mix function for O(1) amortized lookup.
  - Performance: best case 71 instructions per `cons`, worst case 15b+58.
  - Subsequences benchmark: 24,500 cons-cells fit in 2^14-node heap with sharing
    vs 2^n without — maximal sharing is essential.
- **Cites/cited-by**: [AG13], [THOMSEN12]

### [THOMSEN12] Thomsen (2012) — Towards a Reversible Functional Language
- **arXiv/DOI**: doi:10.1007/978-3-642-29517-1_2
- **PDF status**: downloaded (memory/Thomsen2012_reversible_functional_lang.pdf)
- **Category**: MEMORY_MODEL
- **Key idea**: Describes domain-specific languages for reversible logic at different
  abstraction levels. Garbage-free methods to translate between levels.
- **Relevance to Bennett.jl**: Language-level patterns for reversible programming that
  could inform our LLVM IR → reversible gate translation.
- **Verified claims**: (awaiting agent reading — to be filled)
- **Cites/cited-by**: [AG13], [AG18]

---

## Automatic Differentiation (Enzyme)

### [ENZYME20] Moses, Churavy (2020) — Instead of Rewriting Foreign Code for Machine Learning, Automatically Synthesize Fast Gradients
- **arXiv/DOI**: arXiv:2010.01709
- **PDF status**: downloaded (enzyme/Moses2020_enzyme.pdf)
- **Category**: ENZYME_AD
- **Key idea**: LLVM-level automatic differentiation. Key patterns: (1) Activity analysis
  classifies instructions as active/inactive. (2) Forward pass mirrors original code;
  reverse pass inverts instructions. (3) Tape/cache stores forward values needed by
  reverse pass. (4) Works post-optimization for better performance.
- **Relevance to Bennett.jl**: Design patterns directly transferable: activity analysis
  → constant wire elimination; tape → ancilla management; reverse pass → Bennett
  uncomputation; function augmentation → IRCall inlining.
- **Verified claims**:
  - **Activity analysis**: "An instruction is active iff it can propagate a differential
    value to its return or another memory location." Uses LLVM alias + type analysis.
    Integer values always have adjoint zero (cannot carry differentials).
  - **Shadow memory**: "for every active pointer, a parallel shadow allocation stores
    gradients." Duplication-with-replacement construction.
  - **Tape/cache**: Forward values cached when needed by reverse pass. Minimized via:
    recomputation proofs, "to be recorded" analysis, equivalent-value reuse.
    Static bounds → single allocation; dynamic → reallocation.
  - **Function calls**: Forward and reverse in same function. Callee augmented to
    return cached values. "augmented-forward-with-cache-return" pattern.
  - **Control flow reversal**: For every block BB, create reverse_BB. Emit adjoints
    in reverse order. Branch to reverse of predecessor. Phi node adjoints
    conditionally accumulate based on original branch direction (1 bit per branch).
  - **Type tree**: maps byte offsets to types via fixed-point abstract interpretation.
- **Transferable to Bennett.jl**:
  - Activity analysis → constant wire elimination (skip ancillae for constants)
  - Shadow memory → ancilla register (parallel undo wires)
  - Tape = pebbling tradeoff (store vs recompute)
  - Augmented forward = subroutine returns result + ancillae tuple
  - Block reversal = Bennett uncomputation block-by-block
  - Branch recording (1 bit) = our path predicates!
- **Cites/cited-by**: Referenced by [PRS15] conceptually

---

## Additional References

### [MEMRECYCLE25] (2025) — Scalable Memory Recycling for Large Quantum Programs
- **arXiv/DOI**: arXiv:2503.00822
- **PDF status**: downloaded (reversible_synthesis/ScalableMemoryRecycling2025.pdf)
- **Category**: REVERSIBLE_SYNTHESIS
- **Key idea**: Models quantum code as control flow graph, searches for topological
  sorts that maximize qubit reuse opportunities.
- **Relevance to Bennett.jl**: Wire reuse heuristics applicable to our ancilla management.
- **Verified claims**: (to be filled)

---

## SURVEY SUMMARY
- Papers found: 11
- Papers downloaded: 11
- Top 3 most relevant to Bennett.jl: [PRS15], [AG13], [MEULI19]
- Key insight for implementation: The REVS compiler (PRS15) is the closest existing
  system. Its MDD graph + EAGER cleanup achieves 5.3x qubit reduction over full Bennett
  on SHA-2 with zero gate overhead for simple arithmetic. This should be the primary
  architecture target for Bennett.jl v0.8. The Axelsen/Glück work (AG13, AG18) provides
  the theoretical foundation for reversible memory operations needed in v0.7.
