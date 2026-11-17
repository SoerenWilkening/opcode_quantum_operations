# QTG factory-size sweep — n=4 and n=20, F = 1, 2, 4, 8, 16, 32

Measured 2026-09-04/05. Release `libqec` built from `CQ_lang_hardware` **c5e1101**
(post-`ubcf`/`o4i2`: the §9.17 Bell-pair CNOT on both the data path and the factory
decoder). `libcqops` at `a78c12b`, `build-release`. Six arms per size, in parallel,
deterministic counts. `CQOPS_SINK=qec`, layout `comb`, `p_phys = 1e-3`, `epsilon = 1/3`.

## Instances

|  | n=4 (shipped `tools/qtg`) | n=20 (new) |
|---|---|---|
| instance | KP_4(p,w;7), Box 1 of arXiv:2310.06623 | KP_20(p,w;72), seed 20260904 |
| w | 2,2,1,5 | 7,5,9,13,8,6,10,2,14,8,3,9,3,11,5,8,4,4,8,8 (sum 145) |
| p | 6,2,1,2 | 2,4,9,8,5,11,4,3,10,8,8,2,9,3,7,3,5,3,6,10 (sum 120) |
| rails | 4-bit (|c_bin|=3 WIDENED to 4) | **7-bit both, no widening** (|72|=7, |120|=7) |
| greedy x' | 1110, profit 9 = optimum | 13 items, profit 85 vs DP optimum 87 (2.3 % gap) |
| logical gates | 571 (103 X, 292 CX, 168 CCX, 8 Ry) | **4898** (786 X, 2512 CX, 1560 CCX, 40 Ry) |
| logical qubits minted | 126 | **930** |
| derived code distance | d = 9 | **d = 13** |

Config fields are MEASURED, not guessed: `n_clifford = x+cx`, `n_t = 7*ccx + 10*ry`,
`n_gates_hint = x+cx+ccx+ry`, `n_logical` = minted with recycling OFF. Reproduces the
shipped n=4 config to the digit. `compute_code_distance` uses `n_clifford` and
`n_logical` only; `n_t` selects the factory LEVEL (1-level on both).

## DEPTH — the headline

### n = 4 (d = 9)

| F | 1 | 2 | 4 | 8 | 16 | 32 |
|---|---|---|---|---|---|---|
| `qec_depth` | 4,923,042 | 2,462,669 | 1,233,119 | 622,459 | 488,832 | 484,994 |
| × previous | — | 0.5002 | 0.5007 | 0.5048 | 0.7853 | **0.9921** |
| × F=1 | 1.00 | 2.00 | 3.99 | 7.91 | 10.07 | 10.15 |

F=2/4/8/16 reproduce the published `ubcf` rows exactly. F=1 and F=32 are new.

### n = 20 (d = 13)

| F | 1 | 2 | 4 | 8 | 16 | 32 |
|---|---|---|---|---|---|---|
| `qec_depth` | 64,546,474 | 32,276,625 | 16,142,008 | 8,077,887 | 6,239,465 | 6,238,820 |
| × previous | — | 0.5001 | 0.5001 | 0.5004 | 0.7724 | **0.9999** |
| × F=1 | 1.00 | 2.00 | 4.00 | 7.99 | 10.34 | 10.35 |

## Findings

1. **Depth halves EXACTLY with F through F=8 at both sizes** (0.5001–0.5048). The
   post-`ubcf` CX chain no longer sets an F-independent floor anywhere in that range.

2. **The saturation knee is F* ≈ 10 at BOTH sizes** — 10.15 at n=4, 10.35 at n=20,
   where F* = depth(F=1) / chain floor. A 5× larger instance (8.6× the logical gates,
   7.4× the logical qubits, d 9→13) moves it by 2 %. Both the T-throughput term and
   the CX chain scale with the same n·W factor, so their ratio is invariant. **F* is a
   property of the QTG's shape, not of the instance size.**

3. **F=32 is worthless for depth at both sizes**: 0.8 % at n=4, **0.01 % (645 layers
   out of 6.24 M) at n=20**. F=16 is the last factory count that buys anything, and it
   buys 22–23 % over F=8.

4. **At n=20, F=1 → F=4 is FREE in qubits.** Reserved physical qubits are identical
   (1,486,170) at F=1, 2 and 4 — the data comb already has the room, so the factories
   cost nothing extra — while depth drops 4.00×. At n=4 the same step costs +45 %
   qubits. The larger the instance, the more of the factory ladder is free.

5. **Spacetime volume (depth × reserved qubits) optimises at F=8 for n=4 and F=16 for
   n=20.** The depth knee does not move with size but the volume optimum does, because
   the fabric becomes data-dominated.

6. Depth scales **13.1×** from n=4 to n=20 at every F (8.58× logical gates × ~1.44×
   for d 9→13); gates scale **33×** (the d³ merge-area term).

## Physical cost

| | F=1 | F=2 | F=4 | F=8 | F=16 | F=32 |
|---|---|---|---|---|---|---|
| n=4  reserved | 128,800 | 149,408 | 186,760 | 261,464 | 513,912 | 807,576 |
| n=4  depth×qubits | 6.34e11 | 3.68e11 | 2.30e11 | **1.63e11** | 2.51e11 | 3.92e11 |
| n=20 reserved | 1,486,170 | 1,486,170 | 1,486,170 | 1,656,018 | 1,995,714 | 2,823,386 |
| n=20 depth×qubits | 9.59e13 | 4.80e13 | 2.40e13 | 1.34e13 | **1.25e13** | 1.76e13 |

`qec_n_physical` (touched, not reserved) is F-independent: 37,223–40,161 at n=4 and
**494,291 at every n=20 arm**. Total gates are non-monotonic in F (1.184–1.237e11 at
n=20) — routing transients, as `FLOORPLAN_AND_DEPTH.md` records; T is exactly 168,600
at every n=20 arm and 18,840 at every n=4 arm.

## Recommendation

**F = 8 is the operating point.** It captures 7.99× of the 10.35× available depth
reduction, and at n=20 it costs 11 % more qubits than F=1. F=16 buys a further 23 %
of depth for another 20 % of qubits — worth it only if depth is the binding
constraint. F=32 should not be used: it is measurably zero.

## Run data

Six arms per size, one core each, 6 physical cores. n=4: 107–117 s per arm, 6–18 MB
RSS. n=20: 4,598–5,010 s (77–84 min) per arm, 26–59 MB RSS. All rc=0.
Raw rows: `logs/n4v2_F*.log`, `logs/n20_F*.log`. The n=4 rows were measured twice on
two driver builds and are byte-identical.
