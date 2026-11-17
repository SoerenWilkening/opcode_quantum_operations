# `tools/qtg/sweep` — the QTG factory-size sweep

A **measurement harness**, not library code. Nothing under `src/` or `shim/` links any
of it, `make test` does not build it, and no golden in this repo depends on a number
produced here. It answers one question: **how does `qec_depth` respond to
`hardware.factory_count`, and does that response change with the size of the knapsack?**

Results and the reading of them: **[`RESULTS.md`](RESULTS.md)** (noiseless) and
**[`RESULTS_NOISY.md`](RESULTS_NOISY.md)** (noisy execution + the `pL_memory` runs).

## What is here

| Path | What |
|---|---|
| `n20/instance.h` | The **KP_20** instance — 20 items, 7-bit capacity and profit rails. Shadows the shipped `tools/qtg/instance.h` by include order |
| `gen_instance.py` | The seeded generator that produced it (seed 20260904), with the exact-DP optimum it is checked against |
| `probes/qtg_depth.c` | The sweep driver: `tools/qtg`'s gate stream through the qec sink, reporting the resource row |
| `probes/qtg_mint.c` | The **minted** logical-qubit count — what `circuit.n_logical` must be |
| `probes/qec_distance_probe.c` | `qec_create` + `qec_distance`, for checking a config's derived distance in a second rather than by starting an 80-minute arm |
| `configs/qtg_n{4,20}_F{1,2,4,8,16,32}.json` | The twelve noiseless configs |
| `configs/noisy/` | The same twelve with `seed`/`noise`/`decoder`/`frame` added and R-2's four `correction_probs` keys REMOVED (a hard load refusal under `frame: true`) |
| `probes/qtg_depth_phys.c` | `qtg_depth.c` plus the S9 control-plane volume report; the noisy arms use it |
| `probes/qtg_pl_memory.c` | The `pL_memory` experiment at the config's derived distance |
| `results/*.row` | The raw `QEC d=… gates=… depth=…` rows for the CURRENT library, one file per arm |
| `results/<sha>/` | One directory per `CQ_lang_hardware` revision measured — rows, `timing.txt`, `logs/` and that run's own `RESULTS.md`. `c5e1101` is the 2026-09-04/05 run, `6edf92b` the 2026-09-06 re-measurement |
| `results/timing.txt` | Wall clock and peak RSS. A RANKING, never a result (`bd 97s`) |
| `run_sweep.sh` | Builds everything and runs every arm in parallel |

## Reproducing

```sh
cmake --build build-release --target cqops          # the archive the driver links
zsh tools/qtg/sweep/run_sweep.sh /tmp/qtgsweep      # ~80 min: the n=20 arms dominate
zsh tools/qtg/sweep/run_sweep.sh /tmp/q4 "" n4      # ~2 min: the n=4 arms alone
```

`run_sweep.sh` takes the `CQ_lang_hardware` tree as its second argument and builds a
Release `libqec` from it. **Neither dependency is pinned by this repo**, so a row
without the two SHAs beside it is not a report — the runner writes both into
`logs/progress.log` on the build line.

## The three things that are easy to get wrong

**1. The config's `circuit` block is MEASURED, not estimated.** The recipe reproduces
the shipped `tools/qtg/qtg_qec.json` to the digit:

```
n_clifford_estimate = x + cx                 n_gates_hint = x + cx + ccx + ry
n_t_estimate        = 7*ccx + 10*ry          n_logical    = MINTED, recycling OFF
```

The `10` is Ross-Selinger at `CQOPS_QEC_PRECISION=3`, which `tools/qtg` derives from
`QTG_RY_EPSILON` — it is **not** a config field, and the `rotation` block the schema
accepts would be silently inert (`instance.h` says so at length).

**2. `n_t_estimate` does not derive the code distance.** `compute_code_distance`
(`qec/src/distance.c`) takes it and discards it: the distance falls out of
`per_clifford_error = (epsilon/2)/(n_clifford * n_logical)`. `n_t_estimate` selects the
factory **level** (1-level here at both sizes). So an over-stated T count moves the
level and not the distance, and an over-stated Clifford count or `n_logical` moves the
distance and not the level. `d_max` must be odd and `>=` the derived distance or
`qec_create` returns NULL — measured d=9 at n=4, **d=13 at n=20**.

**3. The instance is selected by `-I` ORDER, not by a `#define`.** `qtg.c` says
`#include "instance.h"`; `run_sweep.sh` puts `sweep/n20/` ahead of `tools/qtg/` for the
n=20 driver and omits it for n=4. This is the same trick `CQ_lang_hardware`'s own n=6
probe uses, and it is why `tools/qtg/qtg.c` needed no edit for any of this.

## Scope

**State preparation only** — `G = prod_m U_m` on `|0>|c>|0>`, no amplitude
amplification and no QMaxSearch, exactly as `tools/qtg/qtg.c` documents. So the depths
here are per QTG application, not per full QMaxSearch run.
