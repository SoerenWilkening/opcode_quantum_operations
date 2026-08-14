# Complementary Survey — Cross-Disciplinary Ideas for Reversible Memory in Bennett.jl

**Survey date:** 2026-04-12
**Scope:** Adjacent fields whose ideas could be adapted to reversible mutable memory, deliberately avoiding the canonical reversible-memory literature (persistent FDS, QRAM, reversible GC, Bennett pebbling, Enzyme shadow, ReQomp, etc.) covered by the parallel survey.
**Bottom line:** the biggest win is **not** another custom data structure — it is leveraging LLVM's own **Memory SSA + escape/region analysis** to eliminate most stores before they reach the lowering pass. That combined with a Rust-like linearity discipline on functions that *must* mutate plausibly gets us "80% of Julia code" to zero-heap reversibility at negligible gate-count overhead.

---

## 1. Executive Summary — Top Ideas

| # | Idea | Adapted from | One-liner |
|---|------|--------------|-----------|
| 1 | **Reuse LLVM Memory SSA as the reversible IR for memory** | LLVM compiler infrastructure (Choi-Sarkar-Stoltz 1993; Google/LLVM impl.) | Memory SSA already gives us exactly what Bennett needs: stores become `MemoryDef` nodes, loads become `MemoryUse`, phi merges are explicit. Walk its use-def chain instead of inventing reversible pointers. |
| 2 | **Escape analysis + SROA ⇒ zero-heap for most functions** | Choi et al. 1999, Stadler et al. 2014, LLVM `mem2reg`/`SROA` | If every `alloca` is provably non-escaping and decomposable, promote it to SSA *before* lowering. Partial Escape Analysis reduced JVM heap allocation by 58.5% — for Bennett this converts "impossible to compile" into "no memory ops exist". |
| 3 | **Region-based + linear typing discipline on top of Bennett** | Tofte-Talpin 1997, Cyclone, Rust, Koka FP² | Require functions that genuinely mutate to be linear in their heap roots. Linearity yields *mechanical* reversibility of destructive updates: no alias ⇒ swap-and-forget works. |
| 4 | **Bijective Feistel network as the reversible map primitive** | Bijective arithmetic coding, Feistel structure | Any dictionary/hash-map operation that can be expressed as a Feistel permutation (even with non-invertible round functions) is already reversible. Cost ≤ 2× the round function at ~Toffoli depth O(rounds). |
| 5 | **Memoization / Adapton-style DCG as a cold-swap memory** | Acar, Hammer-Dunfield-Headley 2014 | Treat repeated subcomputations as addressable cached wires. Demand-driven recomputation gives us a principled way to "re-materialize" instead of storing — cheaper than keeping everything pebbled. |

These five together form what I believe is the most productive research programme: a **Memory-SSA-aware Bennett compiler with escape-directed promotion, a linear-mutation dialect, and Feistel-backed dictionaries for the residual heap**. Specific implementation priorities appear in §4.

---

## 2. Detailed Treatment of the Ten Angles

### A. Compiler-theoretic memory analysis — THE LARGEST WIN

**Key insight:** LLVM already has an SSA form of memory — we've been ignoring it.

**Memory SSA (LLVM).** LLVM's `MemorySSA` analysis ([llvm.org/docs/MemorySSA.html](https://llvm.org/docs/MemorySSA.html)) builds an SSA representation specifically for memory operations. Every memory-modifying instruction becomes a `MemoryDef`; every memory-reading instruction becomes a `MemoryUse`; joins are `MemoryPhi`. This is precisely the structure Bennett's construction needs — "heap versioning where every time the memory state changes, a new heap version is generated" (quote from LLVM docs). The `MemorySSAWalker` API lets us query "what is the reaching def for this use?" in O(1) amortised.

Foundational paper: Choi, Sarkar, Stoltz, *Automatic Construction of Sparse Data Flow Evaluation Graphs* (POPL 1991) and its follow-up on global value numbering (1993) — these are the predecessors LLVM credits.

**Implication for Bennett.jl.** Instead of inventing our own reversible pointer abstraction, we can:
1. Run `MemorySSA` on the extracted LLVM IR.
2. Treat each `MemoryDef` as a "fresh wire vector" (the new heap version).
3. Treat each `MemoryUse` as a "CNOT-copy from the reaching def".
4. Treat `MemoryPhi` exactly as we already treat value-phi — nested MUX on the guard chain.
5. Bennett's uncomputation then runs the MemoryDefs in reverse, naturally.

**SROA + mem2reg.** LLVM's `SROA` pass ([llvm.org/doxygen/SROA_8cpp.html](https://llvm.org/doxygen/SROA_8cpp.html)) splits aggregate `alloca`s into per-field allocas, then `mem2reg` promotes them to SSA registers. Run these before extraction and a large fraction of what looks like "memory ops" disappears entirely. Bennett.jl already gets this implicitly when `optimize=false` is followed by modest cleanup — but the project has historically avoided optimisation because of IR stability concerns. The correct answer is: **run SROA and mem2reg, but not aggressive optimisation**. This is the minimum IR-stable mix.

**Escape analysis.** Choi, Gupta, Serrano, *Escape Analysis for Java* ([faculty.cc.gatech.edu/~harrold/6340/cs6340_fall2009/Readings/choi99escape.pdf](https://faculty.cc.gatech.edu/~harrold/6340/cs6340_fall2009/Readings/choi99escape.pdf), POPL 1999) proves objects non-escaping and stack-allocates them. Stadler, Würthinger, Mössenböck, *Partial Escape Analysis and Scalar Replacement for Java* ([dl.acm.org/doi/10.1145/2581122.2544157](https://dl.acm.org/doi/10.1145/2581122.2544157), CGO 2014) pushes this further with control-flow sensitivity — reducing allocations by 58.5% and improving runtime by 33%. The GoLLVM-based MEA² framework reduced heap allocation sites by 7.9% on average (up to 25.7%).

**Julia has its own.** Julia's `EscapeAnalysis` ([docs.julialang.org/en/v1/devdocs/EscapeAnalysis/](https://docs.julialang.org/en/v1/devdocs/EscapeAnalysis/)) operates on Julia IR before LLVM lowering — fully backward analysis from usages to definitions. We can query it directly from the macro or entry point of `reversible_compile`. This is a relatively low-hanging fruit: when escape analysis returns "no escape", we emit a compile-time assertion that the object is purely virtual and skip `store`/`load` entirely.

**Region-based memory management.** Tofte and Talpin, *Region-Based Memory Management* ([Info. & Comp. 132(2), 1997](https://web.cs.ucla.edu/~palsberg/tba/papers/tofte-talpin-iandc97.pdf)) infers region lifetimes via a type-and-effect system and maps allocations onto a stack of regions. The ML Kit shows this works for real Standard ML programs. Cyclone ([Grossman, Morrisett et al., PLDI 2002](https://www.cs.umd.edu/projects/cyclone/papers/cyclone-regions.pdf)) proves it extends to C. For reversibility the *nested LIFO discipline* of regions **is exactly Bennett-compatible**: when a region closes, all of its allocations uncompute in reverse order. No garbage; no late-arriving state.

**Alias analysis.** Andersen (inclusion-based, O(n³), precise) and Steensgaard (equality-based, near-linear, coarser) are the two landmarks ([cs.cornell.edu/courses/cs711/2005fa/papers/steensgaard-popl96.pdf](https://www.cs.cornell.edu/courses/cs711/2005fa/papers/steensgaard-popl96.pdf)). A practical strategy: run Steensgaard first to partition, Andersen on the reduced partition. For Bennett the goal is narrow: *did two pointers ever alias at a given program point?* If no, we can stack-allocate both reversibly.

**Adaptation cost.** Hooking LLVM MemorySSA into our `ir_extract.jl` is a small change — we already use the C API. Roughly a week of engineering. Gate-count impact: for any function whose memory operations promote to SSA, **zero extra gates** beyond what we emit today for pure numeric code. For the residual stores, we fall back to a heap-allocation strategy (see §C).

**Specific recommendation:** Add a new pass in `ir_extract.jl` that runs (i) SROA, (ii) mem2reg, (iii) MemorySSA construction — then treat Memory* nodes as first-class IR operands with the same phi-merging rules as value phis. This is probably the single highest-leverage thing we can do.

---

### B. Immutable / versioned storage systems

The parallel agent covers Okasaki-style functional data structures. What's complementary here is the systems-level analogues.

**Git's object store.** Git is a content-addressable filesystem on top of a Merkle DAG ([git internals](https://dev.to/__whyd_rf/a-deep-dive-into-git-internals-blobs-trees-and-commits-1doc)). Every object is identified by SHA-1 of its contents; trees reference subtrees and blobs; commits reference trees and parents. Deduplication and delta compression give sub-linear storage for histories with small diffs. Partial updates rehash only affected nodes.

**Why it's interesting for Bennett.** Git's structure is *exactly* the shape of a good reversible history: each commit is a snapshot that shares all unchanged substructure with its parent. A reversible "write" would (a) compute the hash of the new tree, (b) link a new commit object that points at the changed subtree, (c) leave the old tree intact. To reverse, walk from child commit to parent via the pointer — no hashing needed.

**Caveat.** Content-addressing isn't intrinsically reversible: if two distinct states hash to the same node, we cannot distinguish them from the hash alone. In practice this is a non-issue (SHA-1 collisions are vanishingly rare), but for a *provably reversible* compiler we would need to store the pre-image explicitly — which defeats the purpose. Only useful if we treat the hash as a lookup key and keep the actual data in ancilla.

**ZFS / log-structured filesystems.** ZFS snapshots ([open-e.com/blog/copy-on-write-snapshots/](https://www.open-e.com/blog/copy-on-write-snapshots/)) use copy-on-write at the block layer. New blocks are written to free locations; old blocks are reachable via the snapshot pointer; a Block Reference Table (BRT) tracks block clones. This is structurally identical to Okasaki's path-copy persistent trees, just at a coarser granularity. Nothing genuinely new — but the COW discipline confirms that "never overwrite, always redirect" is *the* industrial-strength pattern for versioned state.

**LSM trees.** Log-Structured Merge trees ([wikipedia.org/wiki/Log-structured_merge-tree](https://en.wikipedia.org/wiki/Log-structured_merge-tree)) buffer writes in memory, flush sorted runs to disk, and periodically compact. Compaction is irreversible by design — it discards obsolete key versions — and incurs write amplification.

**For Bennett, LSM trees are a cautionary tale.** Compaction is the analogue of uncomputation. An LSM compaction merges several sorted runs into one, discarding superseded entries; this is exactly what we *cannot* do reversibly (it erases information). If we want a "dictionary" data structure with bounded space, we must avoid LSM-style compaction entirely and instead use a Feistel-backed hash table (§D) or an Okasaki persistent tree (canonical survey).

**Event sourcing / CQRS.** Event sourcing ([microservices.io/patterns/data/event-sourcing.html](https://microservices.io/patterns/data/event-sourcing.html)) stores events, derives state by replay. Snapshots cache intermediate states for performance. In Bennett terms this is a deliberate choice to store *inputs* rather than *outputs* — equivalent to checkpointing under Bennett pebbling with a fixed segmentation. Nothing new algorithmically, but it confirms the economic rationale: industry has independently rediscovered that replay + snapshot beats mutable state for auditability. Good external validation for our pebbling work.

**Datomic / XTDB.** Bitemporal databases ([xtdb.com](https://www.xtdb.com/)) treat all data as immutable, versioned along both transaction time and valid time. Both use a log as the source of truth. The interesting idea is that *every query is a time-travel query* — "as-of" semantics are the default. If Bennett.jl exposed a similar model, users could write `f(state, t)` and the compiler would emit a circuit that queries the appropriate version. Useful concept but no new primitive.

**CRDTs.** Conflict-free replicated data types require merge to be a monotone join on a semilattice ([arxiv.org/pdf/2310.18220](https://arxiv.org/pdf/2310.18220)). The join must be commutative, associative, idempotent. This is *weaker* than reversibility (monotone ≠ bijective) and therefore incompatible: once you've joined two states, you cannot in general reverse to recover the two inputs. CRDTs tell us how to *not* solve our problem — their mechanism for conflict resolution is lossy.

**Adaptation recommendation.** Steal Git's Merkle-DAG structural sharing for persistent maps where we already use Okasaki; steal nothing from LSM or CRDTs. Event sourcing is a conceptual model that maps to pebbling, not a concrete algorithm.

---

### C. Linear and ownership type systems as memory discipline — HIGH LEVERAGE

This angle is promising because it attacks the root of the problem: most reversibility pain comes from *aliasing*, and linear/ownership types forbid aliasing by construction.

**Canonical references.** Wadler's linear types, Clean language's uniqueness types ([en.wikipedia.org/wiki/Uniqueness_type](https://en.wikipedia.org/wiki/Uniqueness_type)), Rust's ownership and borrow checker ([arxiv.org/pdf/1903.00982](https://arxiv.org/pdf/1903.00982) — Oxide formalization), Cyclone's `unique` pointers, Granule and Linear Haskell.

**Key fact.** Linearity and uniqueness are *duals*: linear types say "you *must* use exactly once", uniqueness says "you *may* use destructively because no one else holds a reference". From the compiler's perspective, uniqueness enables in-place updates, linearity enables inlining/fusion. For reversibility, the mechanism we want is **uniqueness**: an aliased value cannot be mutated reversibly because the other alias's view changes behind its back.

**Koka FP² (fully-in-place).** Reinking et al., *FP²: Fully In-Place Functional Programming* ([microsoft.com/en-us/research/uploads/prod/2023/05/fbip.pdf](https://www.microsoft.com/en-us/research/uploads/prod/2023/05/fbip.pdf), ICFP 2023) enables safe in-place updates in a functional language using reference counting + uniqueness checks. **Their `fip` functions run in constant stack space, no heap allocation.** This is the strongest result in the literature for "destructive updates in a pure semantics" — and it maps directly to reversible computation. A linear + unique function is already *almost* a reversible function; the only missing piece is that the "forward" computation discards no information.

**Rust as a design template.** Rust proves that linearity can be hidden behind an ergonomic syntax via borrow checking. The borrow checker's dataflow analysis populates regions with liveness and solves constraints to determine loan validity. Rust's *non-lexical lifetimes* demonstrate that we can go beyond simple LIFO regions.

**AG13 connection.** Axelsen-Glück 2013 (reversible heap) already notes that linearity gives automatic GC. The broader lesson: any reversible language that wants a heap needs linearity. We can go further — reversibility + linearity + regions = no GC needed at all.

**Adaptation proposal for Bennett.jl.** Introduce a Julia macro `@reversible_mut` that:
1. Parses a Julia function.
2. Runs a lightweight uniqueness analysis (one pass over the AST).
3. Rejects programs where a mutable value is aliased across branches.
4. If accepted, emits LLVM IR where every mutation is a "swap new for old" operation.
5. Bennett's uncomputation naturally reverses these.

This is strictly more permissive than "no mutation at all" (the current Bennett default) and strictly more restrictive than "arbitrary Julia" (which has no hope).

**Gate-cost estimate.** A uniqueness-checked function has the same gate count as a pure SSA function — the linearity discipline *erases* the cost of memory. For simple cases (integer update in a tight loop), we expect 0% overhead over pure SSA. This is an enormous win if we can make the type system acceptably ergonomic.

---

### D. Reversible cryptography and reversible cellular automata

**Feistel networks.** A Feistel round is `(L, R) → (R, L ⊕ F(R, K))` — invertible regardless of F. Quipper and others have long used Feistel to implement reversible versions of block ciphers ([arxiv.org/pdf/2305.01269](https://arxiv.org/pdf/2305.01269) — quantum LBlock implementation). Recent work on quantum SM4 ([link.springer.com/article/10.1007/s11128-024-04394-x](https://link.springer.com/article/10.1007/s11128-024-04394-x)) uses only 260 qubits for 128-bit plaintext — the Feistel structure avoids *all* ancillae beyond the plaintext itself.

**Bijective integer hashes.** rHashGen ([github.com/yoann-dufresne/rHashGen](https://github.com/yoann-dufresne/rHashGen)), Invertible integer hashes ([gist.github.com/lh3/974ced188be2f90422cc](https://gist.github.com/lh3/974ced188be2f90422cc)) — take 64-bit integers to 64-bit integers via XOR, multiply-by-odd, shift compositions. These are Feistel-like and well-studied for Bloom filter / hash-table slot computation.

**Why this matters for reversible memory.** If we want an associative map `M[k] = v` operation to be reversible, we must *not* overwrite the old value. The standard trick is: `(k, v_new) → (k, v_new, v_old)` with the old value kept in ancilla. Feistel rounds can implement the *lookup* phase (find the slot for key k) with no ancilla at all — because a Feistel round is its own inverse after the rounds are swapped. If our hash is `H(k) = slot`, we cannot reverse `H`; but a Feistel permutation `P(k) = (slot, garbage)` is fully reversible and uses garbage as the ancilla.

**Gate-cost estimate.** A 4-round Feistel with F being three XOR-rotations costs roughly 4 × (width × (2 CNOTs + 1 rotation)) = ~12 × width Toffolis per lookup. For width 32, ~400 gates — well under the 20K/op budget. Compare to Okasaki persistent hash table: ~71K for a 3-node insert.

**Reversible cellular automata.** Toffoli's gate itself was designed so that 3D cellular automata are both universal and reversible ([arxiv.org/pdf/nlin/0501022](https://qiniu.pattern.swarma.org/pdf/arxiv/nlin/0501022.pdf)). Margolus neighbourhoods and block CAs (Critters rule, BBM) are reversible by construction. Their memory model: the lattice *is* the memory; each step shifts information in a deterministic bijection.

**For Bennett.** A reversible CA as memory is an aesthetically beautiful idea (the entire state space is a giant permutation) but practically unworkable: addressing costs are not O(log n), they're O(n) as data crystallises out of the CA's spreading wavefront. Margolus's own analysis shows that CA-based memory is cheap *per cell* but expensive *per access*. Reject for now; revisit if we ever build FPGA/ASIC reversible hardware.

**Quantum dot cellular automata (QCA).** Tougaw-Lent 1993 show classical reversible logic in 2D quantum dot arrays with extremely low power. Hardware-level, not algorithmic. Recent work demonstrates reversible ALUs in QCA ([mdpi.com/2079-4991/13/17/2445](https://www.mdpi.com/2079-4991/13/17/2445)). Relevant to the green-compute motivation but not actionable at the compiler level.

**Adaptation recommendation.** Implement a Feistel-backed reversible dictionary as an alternative to Okasaki trees for use cases with fixed-width keys (the vast majority). Expected gate count per op: 10–20× smaller than Okasaki for small tables.

---

### E. Concurrent and distributed reversibility

**CCSK and reversible π-calculus.** Phillips-Ulidowski CCSK ([link.springer.com/article/10.1007/s00236-019-00346-6](https://link.springer.com/article/10.1007/s00236-019-00346-6)), the parametric framework ([mrg.doc.ic.ac.uk/publications/a-parametric-framework-for-reversible-pi-calculi/express-sos-18.pdf](http://mrg.doc.ic.ac.uk/publications/a-parametric-framework-for-reversible-pi-calculi/express-sos-18.pdf)), the recent axiomatic theory ([dl.acm.org/doi/10.1145/3648474](https://dl.acm.org/doi/10.1145/3648474)). The key conceptual contribution is **causal consistency**: actions reverse iff everything they caused has already reversed. This is weaker than strict LIFO and permits reversing multiple branches of parallel computation in parallel.

**For Bennett.** Our current Bennett construction is strictly sequential: forward, copy, reverse. But if our computation decomposes into independent subcircuits, we could in principle reverse them in *parallel*. The CCSK notion of causal consistency would let us formally justify this. Not a gate-count win but a *depth* win for circuits with substantial parallelism.

**Chandy-Lamport distributed snapshot** ([lamport.azurewebsites.net/pubs/chandy.pdf](https://lamport.azurewebsites.net/pubs/chandy.pdf)). A decentralised protocol for consistent global state capture. Each process records its state and sends markers. This maps to *multi-checkpoint pebbling*: if different subcircuits can independently checkpoint their state, we can roll back to the snapshot frontier rather than an earlier global state.

**HPC checkpointing (DMTCP, BLCR).** Industrial systems for saving/restoring process state at arbitrary points. The infrastructure is not reversible per se, but the *discipline* of periodic snapshot + diff-to-latest matches Bennett's recursive segmentation.

**STM (software transactional memory).** Shadow updates + undo logs — this is *literally* Bennett's forward/reverse pattern for concurrent settings. The undo log is the tape of ancillae.

**Adaptation recommendation.** When extending Bennett.jl with parallel composition of circuits (which Sturm.jl will eventually need for multi-qubit control), adopt CCSK-style causal consistency as the correctness condition. Concrete implementation: a dependency-DAG annotation on the circuit that permits out-of-order reversal when no data dependency demands sequential order.

---

### F. Incremental computation and memoization

**Adapton.** Hammer, Dunfield, Headley et al., *Adapton: Composable, Demand-Driven Incremental Computation* ([cs.tufts.edu/~jfoster/papers/cs-tr-5027.pdf](https://www.cs.tufts.edu/~jfoster/papers/cs-tr-5027.pdf), PLDI 2014). Builds a *demanded computation graph* (DCG) — a hierarchical dependency graph of memoised subcomputations. When a cell changes, dirty transitively, recompute lazily on demand. Gives reliable speedups over eager IC approaches.

**Why it's interesting.** Adapton's DCG is structurally identical to the computation DAG underlying Bennett pebbling. The difference is in *policy*: Adapton dirties-and-recomputes, Bennett pebbles-and-uncomputes. But the underlying graph structure is the same.

**Concrete proposal.** Use Adapton-style memoisation tables as a form of "reversible memory" for repeated subcomputations. When a function is called with the same arguments, instead of recomputing and later uncomputing, CNOT-copy from the memo table. The memo table itself is permanent ancilla (not uncomputed during the Bennett pass). This trades space for time *reversibly*.

**Self-adjusting computation.** Acar's PhD thesis ([cs.cmu.edu/~rwh/students/acar.pdf](https://www.cs.cmu.edu/~rwh/students/acar.pdf)) introduces this formally. Δ-ML ([software.imdea.org/~rleywild/publications/afp08/afp08.pdf](https://software.imdea.org/~rleywild/publications/afp08/afp08.pdf)) gives a statically-typed language embedding. The key insight: *change propagation* is a compiler transformation that takes a program P and produces a program P' that updates the output given an input diff.

**Adaptation feasibility.** Moderate. Memoisation tables need to be addressable — we would need a reversible dictionary (see §D) to back them. The win: for programs with heavy recomputation, we save both forward and reverse gates.

**Gate-cost estimate.** If a subcomputation appears k times with cost C gates, Bennett today pays 2kC (forward + reverse). Adapton-style memoisation pays C + k × (dictionary lookup) ≈ C + k × 400 gates (Feistel-backed). Break-even at k ≈ 2 for C > 400. Significant for loopy numerical code.

---

### G. Hardware-level reversible memory (complements parallel agent's survey of adiabatic CMOS / SFQ)

**Memristor crossbars.** Stateful logic in memristor arrays ([nature.com/articles/s41598-019-51039-6](https://www.nature.com/articles/s41598-019-51039-6)) uses resistance as memory; SET/RESET operations are physically symmetric but not *logically* reversible (the driving voltage determines direction, consuming energy). Recent work on programmable threshold logic crossbars ([pubs.acs.org/doi/10.1021/acs.nanolett.3c04073](https://pubs.acs.org/doi/10.1021/acs.nanolett.3c04073)) demonstrates 4-bit ripple adders in 32×32 arrays. Write endurance and programming accuracy remain practical blockers.

**Phase-change / spintronic memory.** Similar story: physically reversible at the atomic level, logically irreversible because of the driving energy asymmetry.

**DNA storage.** Enzymatic synthesis with reversible terminators ([pnas.org/doi/10.1073/pnas.2410164121](https://www.pnas.org/doi/10.1073/pnas.2410164121), PNAS 2024) — DNA-DISK demonstrates end-to-end automated storage. The enzyme extends strands reversibly using blocked nucleotides; the block is cleaved between extensions. Reversibility here is at the *synthesis* level, not the *access* level: once the strand is synthesized, random access is destructive. Not useful as reversible compute memory.

**Neuromorphic chips (Loihi, TrueNorth).** Spiking neural networks; state updates are integration + leak, not bijective. Hardware for dissipative compute, not reversibility.

**QCA (revisited).** As in §D, QCA is intrinsically reversible but has no practical fab pipeline.

**Optical / photonic reversible logic.** Bennett's original motivation was photonic; recent work continues but no practical memory cells. Interferometric reversibility is a well-studied physics phenomenon that has not yielded a scalable memory cell.

**Bottom line.** None of the non-standard memory hardware gives us anything the parallel survey's adiabatic CMOS + SFQ treatment doesn't already. Reject as hardware focus area for the compiler; flag for future hardware-focused papers.

---

### H. Non-standard computing that "gets reversibility for free"

**Theseus** ([legacy.cs.indiana.edu/~sabry/papers/theseus.pdf](https://legacy.cs.indiana.edu/~sabry/papers/theseus.pdf)) is a reversible functional language based on Π (James-Sabry). Well-typed terminating programs are type isomorphisms. Pattern-matching is the control structure.

**RFun** ([topps.diku.dk/pirc/?id=rfun](https://topps.diku.dk/pirc/?id=rfun)) is a reversible functional language with a Haskell-like syntax. Has a self-interpreter. Proves that reversibility is expressive enough to host its own metacircular semantics.

**Π / Π⁻ / Π⁰.** James-Sabry's *Information Effects* ([dl.acm.org/doi/10.1145/2103656.2103667](https://dl.acm.org/doi/10.1145/2103656.2103667), POPL 2012). Reversible computation is captured by type isomorphisms; irreversibility appears as an *effect*. "Information is treated as a linear resource that can neither be duplicated nor erased."

**Key syntactic restrictions that make memory "free" in these languages:**
1. No shared references — all functions are linear.
2. Destructive pattern-matching — the value you pattern on is *consumed*.
3. No escape hatches — no raw pointers, no `unsafePerformIO`.
4. All recursion is structural (guarded by pattern match on smaller value).

**Can we impose this as a Julia subset?** Yes — with a macro. A `@reversible_subset` macro could:
- Forbid `Ref{T}` except as a linearly-used ancilla.
- Forbid mutation of named struct fields; replace with "setfield → new struct".
- Force loops to be `while_decreasing(i) do ... end` with a compiler-checked variant.
- Forbid global state.

This is essentially a Julia port of Theseus. It's a significant frontend effort (~3-6 months) but gives us a guarantee: programs in the subset compile to zero-memory reversible circuits.

**Adaptation recommendation.** Explore as a long-term frontend; in the short term, keep Bennett.jl's "any Julia" promise and use linearity checks only for functions that mutate.

---

### I. Information-theoretic and error-correction angles

**Lossless compression as bijection.** Arithmetic coding is provably bijective when designed correctly ([sympatico.ca/mt0000/biacode/biacode.html](http://www3.sympatico.ca/mt0000/biacode/biacode.html)). Asymmetric Numeral Systems (ANS, Duda 2013 — [arxiv.org/abs/1311.2540](https://arxiv.org/abs/1311.2540)) are explicitly bijective and near-Shannon-optimal. Huffman coding is a special case of arithmetic coding limited to powers-of-two intervals.

**Why this is interesting.** If we need to pack a dataset onto ancillae before a computation, a bijective compression turns (data, padding) ↔ (compressed, more_padding) reversibly. No information loss. For example, a list of 128 Int8 values with a known distribution (say, integers biased toward small values) compresses to significantly fewer bits via Huffman.

**Gate-cost estimate.** A Huffman decoder is a prefix-tree walk. Depth log(alphabet_size). Each step is a conditional branch. For 256-symbol alphabet: 8 Toffolis × depth = ~200 gates per symbol — *expensive*. Only pays off when ancilla wire cost matters more than gate count (e.g., qubit-limited NISQ era).

**LDPC and fountain codes.** Low-density parity-check codes add parity bits; can recover from erasures. Fountain codes (LT, Raptor — [arxiv.org/pdf/2310.18220](https://arxiv.org/pdf/2310.18220)) let the decoder reconstruct from *any* sufficient subset. Encoding is a sparse XOR graph; decoding is a form of Gaussian elimination.

**For Bennett.** If we could store state as a fountain-encoded representation, reading k random wires would reconstruct the full state. This sounds promising but: (a) the decoder is irreversible (Gaussian elimination destructs the reduction row); (b) the encoder is linear XOR which is already reversible but gives no *new* capability — we're just XORing bits.

**Rejection.** Fountain codes solve a different problem (erasure recovery). Reversibility already treats every wire as "guaranteed delivered". No adaptation.

**Huffman/ANS adaptation.** Potentially worth prototyping for structured data compression in simulator-verified circuits. Low priority.

---

### J. Specific compiler infrastructure

**MLIR memref dialect and bufferization** ([mlir.llvm.org/docs/Dialects/MemRef/](https://mlir.llvm.org/docs/Dialects/MemRef/), [mlir.llvm.org/docs/Bufferization/](https://mlir.llvm.org/docs/Bufferization/)). MLIR's `memref` is the abstraction layer between tensor IR and raw memory. The One-Shot Bufferize pass converts tensor IR to memref IR, making in-place bufferization decisions by analysing SSA use-def chains on tensors.

**Why this matters.** The bufferization pass is solving *exactly* our problem: given an IR that is "functional" (tensor semantics, no in-place update), produce IR that reuses buffers where safe. A reversible bufferization pass would make the *opposite* choice: never reuse a buffer unless the value is provably recomputable or the old value is explicitly preserved in ancilla.

**Adaptation.** We could literally adopt MLIR's bufferization analysis, flipping the decision criterion. For every `tensor<T> → memref<T>` opportunity:
- MLIR asks: can I overwrite?
- Bennett-MLIR asks: can I reuse this wire vector reversibly?

This is a significant engineering investment (moving Bennett to MLIR from LLVM C API) but might be worth it for the Sturm.jl integration, where we want to compose reversible quantum control with non-reversible classical code.

**Polyhedral optimization (Polly, ISL, Tiramisu).** ([polly.llvm.org](https://polly.llvm.org/), [commit.csail.mit.edu/papers/2018/tiramisu_paper.pdf](https://commit.csail.mit.edu/papers/2018/tiramisu_paper.pdf)). Represents loop nests as integer polytopes; schedule transformations become polytope operations. Tiramisu has a Julia backend.

**For reversibility.** The polyhedral model tells us *exactly* when two iterations of a loop can be reordered — namely, when the dependence vector is non-negative in the new schedule. This is relevant because reversing a loop is a schedule transformation: if we can prove via polyhedral analysis that iteration `i` does not depend on iteration `i+1`, the reverse schedule is valid. A polyhedral-aware Bennett compiler could emit circuits that are *shorter* than the forward + reverse Bennett pattern for data-parallel loops — running them once and annotating with direction bits.

**Gate-cost estimate.** For a loop with no loop-carried dependence, polyhedral analysis + reverse scheduling eliminates 50% of the Bennett cost (no reverse pass needed, only copy-out). For loops with dependencies (the common case), no saving. Conservatively 10-20% of realistic programs benefit substantially.

**Halide's algorithm/schedule separation.** Halide ([halide-lang.org](https://halide-lang.org/)) decouples *what* is computed from *how* it is scheduled. This is the right model for Bennett: the algorithm is a reversible circuit; the schedule is a pebbling strategy. A Halide-like DSL for reversible circuits would let users explore time/space tradeoffs without rewriting the algorithm.

**LLVM SCEV + dependence analysis** ([npopov.com/2023/10/03/LLVM-Scalar-evolution.html](https://www.npopov.com/2023/10/03/LLVM-Scalar-evolution.html)). Recognises induction variables and computes precise trip counts + access patterns. LLVM's `LoopAccessAnalysis` uses SCEV to prove that memory accesses follow predictable patterns.

**For Bennett.** SCEV can prove that an `alloca` inside a loop is accessed in a pattern that never aliases across iterations — meaning we can scalarise it (SROA per-iteration) and skip the memory altogether. Probably already partially subsumed by `mem2reg` but worth understanding explicitly for the loop-heavy benchmark programs.

**Adaptation recommendation.** Short-term: stay in LLVM C API, add MemorySSA + SCEV-backed scalarisation as analysis passes. Long-term (6+ months): evaluate MLIR migration for Sturm.jl integration. Polyhedral tools only needed if we develop large-array reversible circuits.

---

## 3. Hybrid Proposals

### Hybrid H1: "Rust-like linearity + Memory SSA + Escape analysis ⇒ zero-heap for 80% of Julia"

**Components:**
- Julia macro for uniqueness checking (lightweight static analysis — §C).
- LLVM SROA + mem2reg on extracted IR (§A).
- LLVM MemorySSA for residual stores (§A).
- Escape analysis to confirm stack-only allocation (§A).

**Pipeline:**
```
Julia @reversible function → uniqueness check →
  LLVM IR (optimize=O1, narrow passes) → SROA → mem2reg →
  MemorySSA → Bennett extract → lower → bennett construction
```

**Expected behaviour:**
- ~80% of Julia functions have no escaping heap objects. For these, the compiler emits circuits with zero memory ops — identical gate count to today's "pure numeric" path.
- ~15% have small bounded non-escaping objects (tuples, small structs). These go through SROA and are scalarised.
- ~5% have genuinely escaping heap or dynamic-sized allocations. These need a residual reversible memory strategy (§D Feistel tables, or Okasaki from the canonical survey).

**Implementation effort:** 4-6 weeks for the analysis passes; 2-4 weeks for the Julia macro; 1-2 weeks for the Feistel fallback. Total 8-12 weeks.

**Expected win:** Transforms Bennett.jl from "cannot handle any memory" into "handles most real Julia code". This is the single largest expected impact on the paper.

### Hybrid H2: "MemorySSA-phi = Bennett reverse-phi"

**Observation.** LLVM's `MemoryPhi` nodes are exactly the memory analogue of value phis. Our existing phi-resolution algorithm in `lower.jl` handles value phis. MemoryPhi needs the same treatment: for each predecessor, MUX-select the memory state.

**Adaptation.** Extend `lower.jl`'s phi handling to accept MemoryPhi nodes. The "value" being MUXed is now a whole memory version (a tuple of wire vectors). Implementation: reuse the nested-MUX construction verbatim.

**Caveat.** The false-path sensitization bug noted in CLAUDE.md reappears here — MemoryPhi is just as vulnerable as value Phi. Must apply the same dominating-guard analysis.

**Implementation effort:** 1-2 weeks, heavily building on existing phi-resolution code.

**Gate-cost estimate.** Zero extra cost compared to explicit per-wire phi resolution; this is just structuring existing capability.

### Hybrid H3: "Feistel-backed dictionaries + Adapton memo for cached subcomputations"

**Components:**
- Feistel permutation lookup (§D).
- Adapton-style demanded-computation-graph for memoisation (§F).

**Use case:** programs that repeatedly look up the same value (e.g., LUT-based algorithms, dynamic programming).

**Pipeline:**
1. Compiler detects pure functions called with few distinct argument tuples.
2. Instead of re-emitting the circuit, emit a lookup in a pre-populated memo table.
3. Memo table stored as Feistel-backed reversible hash.
4. At circuit end, the memo table is itself an ancilla that is *not* uncomputed (acts as a shared resource).

**Expected win:** for DP-heavy algorithms, 10-100× gate reduction.

**Implementation effort:** 6-8 weeks (memo detection + table layout + Feistel integration).

### Hybrid H4: "Region-LIFO + Bennett recursion"

**Idea.** Use Tofte-Talpin region inference to identify nested scopes where all allocations share a LIFO lifetime. Within each region, apply Bennett's recursive strategy (Knill pebbling). Across regions, uncomputation is free because the entire region is discarded.

**Why this works.** Bennett's recursion already exploits LIFO structure. Regions make this explicit and nest-able.

**Expected win:** For recursive algorithms (Bennett.jl's current weak spot), gives tighter space bounds than Knill's global analysis.

**Implementation effort:** 4-6 weeks (region inference + integration with pebbling.jl).

### Hybrid H5: "Halide-style schedule DSL for Bennett pebbling"

**Idea.** Decouple the reversible algorithm from its pebbling schedule. Users write the algorithm once; explore pebbling via a schedule DSL.

**Example:**
```julia
alg = @reversible_circuit quicksort(arr)
schedule = PebblingSchedule(:bennett_recursive, depth=3, max_ancillae=100)
circuit = compile(alg, schedule)
```

**Why this is useful.** Reviewer-facing: the paper can present gate-count tradeoffs as a Pareto frontier, not a single point.

**Implementation effort:** 3-4 weeks. Mostly a surface API over existing pebbling infrastructure.

---

## 4. Implementation Priorities

Ranked by **expected-gain ÷ implementation-cost**, assuming Q3-Q4 2026 timeline:

| Priority | Project | Effort | Paper impact |
|----------|---------|--------|--------------|
| **P1** | LLVM MemorySSA + SROA integration (§A, H2) | 4-6 weeks | *Enormous* — unlocks all LLVM memory ops |
| **P2** | Julia uniqueness macro (§C, H1 frontend) | 2-4 weeks | Large — makes the compiler practical |
| **P3** | Feistel-backed reversible dictionary (§D) | 2-3 weeks | Medium — beats Okasaki in gate count for fixed keys |
| **P4** | Escape analysis pass (§A, H1) | 2 weeks | Medium — quantifies H1 benefit |
| **P5** | CCSK-style parallel reversal (§E) | 3-4 weeks | Medium — depth reductions for parallel circuits |
| **P6** | Adapton-style memoisation (§F, H3) | 6-8 weeks | Medium — for DP-heavy benchmarks |
| **P7** | Polyhedral loop scheduling (§J) | 6-8 weeks | Small — niche loop patterns only |
| **P8** | Halide-like schedule DSL (H5) | 3-4 weeks | Presentational — helps the paper Pareto plot |
| **P9** | Region inference (§A, H4) | 4-6 weeks | Small — overlaps with H1 |
| P10 | MLIR migration (§J) | 3-6 months | Long-term — Sturm.jl integration only |

**Recommended kick-off:** start P1 immediately; in parallel, scope P2. P3 is an independent workstream that can be done by a separate agent.

---

## 5. What's Impossible or Bad Ideas

### Rejected: Content-addressing (Git-style Merkle DAG) as a reversible memory primitive
**Reason:** Hash functions are not bijections. Reversibility requires preserving the pre-image, which defeats the purpose of hashing. Only useful as a *lookup key* with pre-image kept separately — no gate savings over explicit storage.

### Rejected: CRDTs as reversible data structures
**Reason:** Monotone joins on a semilattice are *lossy* by construction. Once merged, inputs are unrecoverable. Incompatible with reversibility at a fundamental level.

### Rejected: LSM trees / compaction-based storage
**Reason:** Compaction discards superseded entries. Irreversible by design. Appending to a log without compaction is fine (= event sourcing) but then is just Bennett's own ancilla tape.

### Rejected: Fountain codes / LDPC as reversible memory
**Reason:** Encoding is linear XOR (already reversible but adds no capability). Decoding is Gaussian elimination (irreversible). Solves the wrong problem — erasure recovery, not reversible access.

### Rejected: Neuromorphic / memristor / phase-change / spintronic / DNA as compiler targets
**Reason:** Physical reversibility at the atomic level doesn't imply logical reversibility. The driving energy makes the write asymmetric. Interesting hardware for future but no compiler-level adaptation.

### Rejected: Cellular-automata-based memory
**Reason:** O(n) addressing because of CA wavefront crystallisation time. Fails the per-op budget.

### Rejected: Cryptographic hash functions (SHA, MD5) as reversible primitives
**Reason:** Designed to be non-invertible. The Feistel *structure* is useful; the specific cryptographic construction is not.

### Risky (not rejected): MLIR migration
**Reason:** High effort (months), high switching cost. Beneficial long-term for Sturm.jl integration, but a distraction from short-term paper priorities. Defer to 2027.

### Risky (not rejected): Full Theseus-style linear language as Julia frontend
**Reason:** Linguistically pure but ergonomically painful. The uniqueness-macro approach (H1) captures 80% of the win at 20% of the design cost.

### Risky (not rejected): Polyhedral optimisation
**Reason:** Large dependency surface (ISL, isl-python, Polly). Only helps niche loop patterns. Low priority until we have large-array reversible benchmarks.

---

## 6. Unknowns and Caveats

- **Quantifying "80% of Julia code is non-escaping":** I'm citing Stadler et al.'s 58.5% allocation reduction on JVM benchmarks as evidence that escape analysis works at scale. Julia-specific numbers would be stronger — a quick benchmark against Base Julia test programs would calibrate H1.
- **Feistel lookup gate counts:** my ~400-gate estimate assumes a 4-round Feistel with cheap round functions (XOR + rotate). More rounds or more sophisticated rounds (multiplicative) push this toward 1-2K gates. Still well under budget but not a dramatic win over small Okasaki trees.
- **LLVM MemorySSA stability:** the analysis is mature but the API has evolved. Our `ir_extract.jl` uses a specific LLVM.jl version; confirm MemorySSA bindings exist.
- **CCSK parallel reversal correctness:** the causal-consistency condition is well-understood theoretically but not, to my knowledge, implemented in a compiler. Prototype risk is real.
- **Uniqueness macro ergonomics:** The biggest risk for H1's frontend is that the error messages are unacceptable. Rust spent years polishing borrow-check errors. Our first pass will be rough.

---

## 7. Key References (with download status)

The parallel agent has likely pulled Okasaki, Driscoll-Sarnak-Sleator-Tarjan, Mogensen, Enzyme, ReQomp, QRAM. The complementary references that are most worth download:

| Paper | URL | Relevance |
|-------|-----|-----------|
| Choi, Gupta, Serrano, *Escape Analysis for Java* (POPL 1999) | [faculty.cc.gatech.edu/~harrold/6340/cs6340_fall2009/Readings/choi99escape.pdf](https://faculty.cc.gatech.edu/~harrold/6340/cs6340_fall2009/Readings/choi99escape.pdf) | §A — foundational |
| Stadler et al., *Partial Escape Analysis and Scalar Replacement for Java* (CGO 2014) | [dl.acm.org/doi/10.1145/2581122.2544157](https://dl.acm.org/doi/10.1145/2581122.2544157) | §A — state of the art |
| Tofte, Talpin, *Region-Based Memory Management* (Info & Comp 1997) | [web.cs.ucla.edu/~palsberg/tba/papers/tofte-talpin-iandc97.pdf](https://web.cs.ucla.edu/~palsberg/tba/papers/tofte-talpin-iandc97.pdf) | §A — regions |
| Reinking et al., *FP²: Fully In-Place Functional Programming* (ICFP 2023) | [microsoft.com/en-us/research/uploads/prod/2023/05/fbip.pdf](https://www.microsoft.com/en-us/research/uploads/prod/2023/05/fbip.pdf) | §C — linearity + reuse |
| Weiss et al., *Oxide: The Essence of Rust* | [arxiv.org/pdf/1903.00982](https://arxiv.org/pdf/1903.00982) | §C — borrow semantics |
| James, Sabry, *Information Effects* (POPL 2012) | [dl.acm.org/doi/10.1145/2103656.2103667](https://dl.acm.org/doi/10.1145/2103656.2103667) | §H — reversible lambda |
| James, Sabry, *Theseus* | [legacy.cs.indiana.edu/~sabry/papers/theseus.pdf](https://legacy.cs.indiana.edu/~sabry/papers/theseus.pdf) | §H — reversible functional language |
| Hammer, Dunfield, Headley et al., *Adapton* (PLDI 2014) | [cs.tufts.edu/~jfoster/papers/cs-tr-5027.pdf](https://www.cs.tufts.edu/~jfoster/papers/cs-tr-5027.pdf) | §F — demanded computation |
| Peduri, Bhat, Grosser, *QSSA* (CC 2022) | [arxiv.org/abs/2109.02409](https://arxiv.org/abs/2109.02409) | §A — quantum SSA analogue |
| Phillips, Ulidowski, *Reversible structural operational semantics* (also Static vs dynamic reversibility in CCS) | [link.springer.com/article/10.1007/s00236-019-00346-6](https://link.springer.com/article/10.1007/s00236-019-00346-6) | §E — causal consistency |
| Chandy, Lamport, *Distributed Snapshots* | [lamport.azurewebsites.net/pubs/chandy.pdf](https://lamport.azurewebsites.net/pubs/chandy.pdf) | §E — checkpointing |
| Ragan-Kelley et al., *Halide* (PLDI 2013) | [people.csail.mit.edu/jrk/halide-pldi13.pdf](https://people.csail.mit.edu/jrk/halide-pldi13.pdf) | §J — algorithm/schedule |
| LLVM MemorySSA documentation | [llvm.org/docs/MemorySSA.html](https://llvm.org/docs/MemorySSA.html) | §A — the centrepiece |
| Steensgaard, *Points-to Analysis in Almost Linear Time* (POPL 1996) | [cs.cornell.edu/courses/cs711/2005fa/papers/steensgaard-popl96.pdf](https://www.cs.cornell.edu/courses/cs711/2005fa/papers/steensgaard-popl96.pdf) | §A — alias analysis |

I did not download PDFs in this session; the parallel agent may already hold some. Recommend prioritising MemorySSA docs, Choi escape analysis, Tofte-Talpin regions, and FP² for immediate download.

---

## 8. Closing Take

If I had to bet the paper on one thing from this report: **it's Memory SSA plus a uniqueness macro (Hybrid H1).** Every other idea is either marginal (H3-H5), speculative (§I fountain codes), or hardware (§G). The industry-scale compiler literature already solved "analyse memory precisely in SSA form" and "forbid aliasing via types" — we need to adopt, not invent.

The non-obvious connection I'd highlight: **LLVM's MemorySSA is already a reversible IR**. Its `MemoryDef`/`MemoryUse`/`MemoryPhi` nodes are exactly the structure we've been hand-building in our IR types. We should be walking MemorySSA directly, not extracting memory ops and rebuilding the dataflow. This reframes "Bennett supports memory" from "write a lot of new code" to "hook up an existing analysis".

Time spent on this survey: ~50 minutes of real research. Word count: ~7000.
