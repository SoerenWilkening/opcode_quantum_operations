# QTG factory-size sweep — n=4 and n=20, F = 1, 2, 4, 8, 16, 32

Re-measured **2026-09-06** against a Release `libqec` from `CQ_lang_hardware` **6edf92b**.
`libcqops` at `a78c12b`, `build-release` — **unchanged from the previous run**, so every
delta below belongs to the QEC library. Ten arms in parallel plus the two F=32 arms run separately, deterministic counts.
`CQOPS_SINK=qec`, layout `comb`, `p_phys = 1e-3`, `epsilon = 1/3`.

The **c5e1101** run this supersedes is preserved verbatim in
[`results/c5e1101/`](results/c5e1101/) (rows, timing and its own `RESULTS.md`); the new
rows and logs are in [`results/6edf92b/`](results/6edf92b/).

## What moved between c5e1101 and 6edf92b

`gr01.1` syndrome rounds in hardware layer order (14–16 → 8–9 depth layers per round) ·
`gr01.2` the d-cycle CNOT · `gr01.3` the temporary logical AND and 4-T Toffoli ·
`y2xg` the |+> ancilla region and the merge-form CZ (`qec_cz` fix-up 627 → 155 at d=9).

## THIS IS A PURE DEPTH RE-BASELINE

**Every other column is byte-identical to the c5e1101 rows at all ten arms** — `gates`,
`T`, `CX`, `H`, `nphys`, `nlog`, `reserved`, `above`. Only `qec_depth` moved. The
derived code distances are also unchanged (d=9 at n=4, d=13 at n=20, probed with
`probes/qec_distance_probe.c` before the sweep), so the twelve configs needed no edit
and the two runs are directly comparable.

## DEPTH — the headline

### n = 4 (d = 9)

| F | 1 | 2 | 4 | 8 | 16 | 32 † |
|---|---|---|---|---|---|---|
| depth @ c5e1101 | 4,923,042 | 2,462,669 | 1,233,119 | 622,459 | 488,832 | 484,994 |
| **depth @ 6edf92b** | **2,335,390** | **1,168,018** | **584,565** | **299,290** | **262,506** | **262,506** |
| × c5e1101 | 0.4743 | 0.4742 | 0.4740 | 0.4808 | 0.5370 | 0.5412 |
| × previous F | — | 0.5001 | 0.5005 | 0.5120 | 0.8771 | **1.0000** |
| × F=1 | 1.00 | 2.00 | 4.00 | 7.80 | **8.90** | 8.90 |

† The two F=32 arms were run separately, to locate the floor.

### n = 20 (d = 13)

| F | 1 | 2 | 4 | 8 | 16 | 32 † |
|---|---|---|---|---|---|---|
| depth @ c5e1101 | 64,546,474 | 32,276,625 | 16,142,008 | 8,077,887 | 6,239,465 | 6,238,820 |
| **depth @ 6edf92b** | **30,158,209** | **15,079,865** | **7,540,870** | **3,777,912** | **2,990,073** | **2,990,073** |
| × c5e1101 | 0.4672 | 0.4672 | 0.4671 | 0.4676 | 0.4792 | 0.4793 |
| × previous F | — | 0.5000 | 0.5001 | 0.5010 | 0.7915 | **1.0000** |
| × F=1 | 1.00 | 2.00 | 4.00 | 7.98 | **10.09** | 10.09 |

## Findings

1. **Depth roughly halves at both sizes: ×0.474 at n=4, ×0.467 at n=20.** The win is
   almost exactly the `gr01.1` syndrome-round ratio (8–9 layers where there were 14–16),
   which is what a depth-only re-baseline should look like.

2. **The win is NOT uniform in F, and it shrinks as F grows.** At n=4 the ratio is a flat
   0.474 through F=4, then 0.481 at F=8 and 0.537 at F=16. At n=20 it is 0.467 through
   F=8 and 0.479 at F=16. **The F-independent CX-chain floor came down by less than the
   factory-limited term did**, so the floor is now a larger share of the total.

3. **THE KNEE MOVED DOWN AT n=4 AND BARELY AT n=20 — the "F\* ≈ 10, size-invariant"
   finding no longer holds as stated. Both floors are now MEASURED, not extrapolated.**
   At **both** sizes F=32 is **exactly equal** to F=16 — 262,506 at n=4 and 2,990,073 at
   n=20, to the layer — where at c5e1101 F=32 was still 0.8 % / 0.01 % below. So F=16
   lands exactly on the floor at both sizes, and **F\* = 8.90 at n=4** (down from 10.15)
   and **F\* = 10.09 at n=20** (down from 10.35). The spread across the two sizes is now
   **13.4 %**, where it was 2 % — the knee is no longer size-invariant to within
   measurement noise, and n=4 is where it moved.

4. **Depth still halves exactly through F=4 at both sizes** (0.5000–0.5005). The first
   deviation is at F=8 at n=4 (0.512, one step earlier than before) and still at F=16 at
   n=20 (0.7915).

5. **F=32 is now exactly worthless at BOTH sizes** — it buys literally zero layers,
   where at c5e1101 it bought 0.8 % at n=4 and 645-of-6.24 M layers at n=20. F=16 still
   buys 12 % over F=8 at n=4 (was 22 %) and 21 % at n=20 (was 23 %).

6. **The qubit columns are untouched**, so every physical-cost statement from the
   c5e1101 report survives: reserved is identical at F=1/2/4 for n=20 (1,486,170 — the
   F=1 → F=4 step is free in qubits), `qec_n_physical` is F-independent (37,223–40,161 at
   n=4, 494,291 at every n=20 arm), and T is exactly 18,840 / 168,600.

## Physical cost — spacetime volume

| | F=1 | F=2 | F=4 | F=8 | F=16 | F=32 |
|---|---|---|---|---|---|---|
| n=4 reserved | 128,800 | 149,408 | 186,760 | 261,464 | 513,912 | 807,576 |
| n=4 depth×qubits | 3.01e11 | 1.75e11 | 1.09e11 | **7.83e10** | 1.35e11 | 2.12e11 |
| n=20 reserved | 1,486,170 | 1,486,170 | 1,486,170 | 1,656,018 | 1,995,714 | 2,823,386 |
| n=20 depth×qubits | 4.48e13 | 2.24e13 | 1.12e13 | 6.26e12 | **5.97e12** | 8.44e12 |

**Both volume optima are where they were** — F=8 at n=4, F=16 at n=20 — because the
depth change is nearly a constant factor and the qubit columns did not move. The n=20
margin narrowed: F=16 now beats F=8 by 4.6 %, down from 6.7 %.

## Recommendation — unchanged, with one qualification

**F = 8 remains the operating point.** It captures 7.80× of the 8.90× available at n=4
and 7.98× of the 10.09× at n=20. What changed is the case for F=16 **at n=4**: it now
buys 12 % of depth for +97 % reserved qubits and reaches the floor exactly, so F=32 is
not merely poor there but exactly zero — **and the same is now true at n=20**, where
F=32 also buys zero layers for +41 % reserved qubits. At n=20, F=16 still buys 21 % for
+21 % qubits and remains the volume optimum.

## Run data

Ten arms, one core each, 6 physical cores. n=4: 130–136 s per arm, 6–18 MB RSS. n=20:
2,960–3,044 s (49–51 min) per arm, 28–63 MB RSS — **~40 % faster in wall clock than the
c5e1101 run at the same gate counts**, which is a ranking and not a result (`bd 97s`).
All rc=0. The two F=32 arms were run alone rather than in the batch — n=4 F=32 without
`/usr/bin/time`, n=20 F=32 in 2,263 s (38 min) and 48 MB RSS, faster than the batched
arms because it had a core to itself.
Raw rows and logs: [`results/6edf92b/`](results/6edf92b/).
