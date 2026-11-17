# QTG factory-size sweep under NOISY EXECUTION — n=4 and n=20, F = 1 … 32

Measured **2026-09-06**, `libqec` @ `CQ_lang_hardware` **6edf92b**, `libcqops` **a78c12b**,
`build-release`. **One run per (n, F) at one error rate**, twelve arms in parallel.
Configs: [`configs/noisy/`](configs/noisy/) — the noiseless sweep's configs plus

```json
{ "seed": 42,
  "noise": { "p_x": 0.001, "p_y": 0.001, "p_z": 0.001, "p_m": 0.001 },
  "decoder": "union_find",
  "frame": true }
```

The noiseless run these compare against is [`RESULTS.md`](RESULTS.md); raw rows and logs
are in [`results/6edf92b-noisy/`](results/6edf92b-noisy/).

## Three things to establish before reading a number

**1. `frame: true` FORCED a config change — it is not an added key.** Schema §5.2's **R-2**
makes `injection_s`, `lattice_merge_byproduct`, `lattice_split_byproduct` and
`and_uncompute_cz` a **hard load refusal** alongside `frame: true`: the frame supersedes
them, so a config setting both "is asking for two different answers to one question". They
are therefore **removed** from the noisy configs, not set to 1.0. `syndrome_x_correction`
and `syndrome_z_correction` are explicitly **not** in the refusal set — they gate the
stab-shape enumeration, which the frame does not touch — and stay at 1.0. Both derived
distances are unchanged: **d=9 at n=4, d=13 at n=20**.

**2. THE DECODER NEVER RAN.** `qec_decoder_uf_decode` / `_append` / `_apply` are called
from **`src/pl_memory.c` and nowhere else**. Every arm reports
`decodes=0 find=0 union=0 grow=0 peel=0 corr=0` while extracting 3.8e8 (n=4) to 1.28e10
(n=20) syndrome bits. `decoder: "union_find"` in a program config buys the graph sizing and
the physics report; **decoding is a `qec_pl_memory` operation**, not something an ordinary
program run does.

**3. THE NOISE CANNOT MOVE THE RESOURCE ROW, AND THE FRAME CAN.** The sampler runs at one
chokepoint in `emit_syndrome_round` but "emits and counts NOTHING and writes no trace byte"
(Law 1). So every delta below is the **frame**: `injection_s` going from a deterministic 1.0
to a fair ½ draw on the outcome stream. **That makes each row ONE SAMPLE of a random
variable**, where the noiseless rows were deterministic — see the warning under the table.

## DEPTH — noisy vs noiseless

| | F=1 | F=2 | F=4 | F=8 | F=16 | F=32 |
|---|---|---|---|---|---|---|
| **n=4** noiseless | 2,335,390 | 1,168,018 | 584,565 | 299,290 | 262,506 | 262,506 |
| **n=4** noisy | 2,335,310 | 1,168,018 | 584,565 | 300,371 | 266,531 | 267,890 |
| Δ | −80 | **0** | **0** | +1,081 | +4,025 | +5,384 |
| Δ % | −0.003 % | 0.000 % | 0.000 % | +0.36 % | +1.53 % | +2.05 % |
| **n=20** noiseless | 30,158,209 | 15,079,865 | 7,540,870 | 3,777,912 | 2,990,073 | 2,990,073 |
| **n=20** noisy | 30,158,093 | 15,079,865 | 7,540,781 | 3,778,245 | 3,013,825 | 3,018,217 |
| Δ | −116 | **0** | −89 | +333 | +23,752 | +28,144 |
| Δ % | −0.0004 % | 0.000 % | −0.001 % | +0.009 % | +0.79 % | +0.94 % |

Total physical gates move by **−0.024 % … +0.005 %** at n=4 and **−0.010 % … −0.001 %** at
n=20. **`T`, `n_physical`, `n_logical`, `n_reserved` and `n_above_reserve` are byte-identical
to the noiseless rows at all twelve arms.**

> **THE F=16 / F=32 DIFFERENCE IS NOT RESOLVABLE FROM ONE SAMPLE.** Noiseless, those two are
> exactly equal at both sizes — F=16 sits on the floor. Noisy, F=32 comes out **larger** than
> F=16 (+0.5 % at n=4, +0.15 % at n=20). With the frame on, depth is a random variable and
> each arm here is a single draw, so a sub-percent ordering between two arms is **not a
> measurement**. Do not read "F=32 is worse than F=16" out of this table; the noiseless run,
> which is deterministic, says they are equal. Resolving it needs an ensemble — many seeds per
> arm — which is exactly what a single run per (n, F) does not buy.

## What the noisy run adds that the noiseless one CANNOT

The classical control-plane volume. `qec_physics_report_get` **refuses** with the decoder
off, so these numbers do not exist for the noiseless arms.

| | F=1 | F=2 | F=4 | F=8 | F=16 | F=32 |
|---|---|---|---|---|---|---|
| n=4 syndrome bits | 3.787e8 | 3.727e8 | 3.764e8 | 3.842e8 | 3.818e8 | 3.905e8 |
| n=4 syndrome rounds | 1,472,274 | 1,472,706 | 1,473,039 | 1,473,102 | 1,465,263 | 1,467,162 |
| n=4 bits/round high-water | 1,862 | 1,862 | 2,186 | 2,591 | 3,644 | 4,211 |
| n=20 syndrome bits | 1.279e10 | 1.262e10 | 1.228e10 | 1.225e10 | 1.241e10 | 1.278e10 |
| n=20 syndrome rounds | 19,037,711 | 19,035,657 | 19,034,604 | 19,036,879 | 19,038,036 | 19,039,622 |
| n=20 bits/round high-water | 10,308 | 10,308 | 10,308 | 10,646 | 11,829 | 14,195 |

Per-round low-water is **80** at every n=4 arm and **168** at every n=20 arm.

**Findings.**

1. **Syndrome-round COUNT is F-independent** — 1.465–1.473 M at n=4 (0.5 % spread),
   19.035–19.040 M at n=20 (0.03 %). Rounds are set by the data comb's schedule, not by how
   many factories feed it.
2. **The per-round bit HIGH-WATER is what F moves**: ×2.26 from F=1 to F=32 at n=4 and ×1.38
   at n=20, while the low-water never moves. More factories mean more stabilizers measured in
   the widest round — a **peak classical bandwidth** cost, not a total-volume one.
3. **Total bits are non-monotonic in F** (both sizes dip around F=4–8 and rise again), the
   same routing-transient signature the noiseless total gate count shows.
4. **n=20 extracts 33× the syndrome bits of n=4** — 1.28e10 vs 3.79e8 — against 13× the
   rounds, the rest being the larger fabric at d=13.

## Cost

n=4: 187–203 s wall, 164–181 s CPU, 6.7–18 MB RSS. n=20: 8,055–8,209 s wall but **3,252–3,409 s
CPU** — twelve arms on 6 physical cores, so wall clock here is contention, not work. Against
the noiseless run's 2,838–2,921 s CPU, **sampling the channel costs ~14 % CPU** and buys the
control-plane report. All rc=0.

---

# `pL_memory` at the sweep's two code distances

**One run per instance size at the one error rate** — F does not enter, because `pL_memory`
is a property of a single idling patch at its distance, not of the factory ladder. Driven by
[`probes/qtg_pl_memory.c`](probes/qtg_pl_memory.c) from the same `configs/noisy/` files, so
`d` is **derived by `qec_create`** exactly as the sweep arms derive it rather than hand-set.

| | n=4 | n=20 |
|---|---|---|
| distance | d = 9 | d = 13 |
| rounds (= d) | 9 | 13 |
| shots | 1,000,000 | 1,000,000 |
| **failures** | **2** | **0** |
| **pL** | **2.0e-6** | **0** |
| 95 % Clopper-Pearson | [2.4e-7, 7.2e-6] | [0, 3.7e-6] |
| decodes / find / union / grow / peel | 1e6 / 1.23e7 / 5.54e6 / 3.49e7 / 5.54e6 | 1e6 / 3.40e7 / 1.57e7 / 1.13e8 / 1.57e7 |
| corrections applied | 2,041,143 | 6,231,996 |
| CPU | 156 s | 459 s |

**The two intervals overlap, so this does not measure the d-dependence.** 2 failures against
0 is the right direction and nothing more; separating d=9 from d=13 at p=1e-3 needs a shot
count far beyond one run. Quote the interval or do not quote pL.

**Why upstream's `pl_sweep` was not used.** Its `sweep` and `report` arms take a single
`--p` and set `p_x = p_y = p_z = p/3, p_m = p` (`examples/pl_sweep.c:257`); the channel asked
for here is all four rates equal at 1e-3, which only its `exact` arm can express — and that
arm is the weight-≤2 truncated enumeration, which at d=9 and d=13 has **no failing pattern at
all** (the leading failure weight is `floor((d−1)/2)+1` = 5 and 7). So the rates come from
the config, where they are stated exactly.

**Scope, carried verbatim from `qec.h`'s D16 because it is the whole of the caveat:** this is
a **per-patch MEMORY** logical error rate for an **idling** patch under the **unweighted**
union-find decoder. Gadget- and injection-level error are **unmodelled** — no merge, no
split, no injection and no factory round is sampled. **It must never be composed with the T
count** to produce a circuit-level figure, and it does not feed `compute_code_distance`;
`src/distance.c` remains the single owner of the analytic error model. It is an **upper bound
for that decoder**, not the code's intrinsic performance.

**This is also where the decoder actually runs.** 1e6 decode invocations per arm, against
**`decodes=0` on every one of the twelve QTG arms above** — the sharpest statement of the
difference between the two experiments.
