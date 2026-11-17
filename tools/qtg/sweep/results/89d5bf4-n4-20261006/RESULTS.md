# Four-item QTG generation timing — 2026-10-06

QEC `89d5bf479ab4397d4cc96702ecb1d5bd2ad4e46a`; cqops `27d9b4cea3e010298982fc1f114211d07a0caf75`. Both built fresh in Release.
Compiler: Apple clang version 17.0.0 (clang-1700.6.4.2).
Instrument: /usr/bin/time -p; Python perf_counter end-to-end. Release, n=4, noiseless, serial QEC, no trace, sequential arms.
Existing n=4 configs unchanged; their SHA256 hashes are in measurement.json. Precision 3; d=9; 126 logical qubits. All six runs return 0 and pass the ordered rotation checks.

| Factories | Generation wall (s) | CPU user (s) | CPU sys (s) | Sept 6 wall (s) | Historical wall ratio | QEC depth | Sept 6 depth |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 14.52 | 14.46 | 0.03 | 132.19 | 9.10x | 2,713,662 | 2,335,390 |
| 2 | 14.42 | 14.35 | 0.03 | 130.57 | 9.05x | 1,357,245 | 1,168,018 |
| 4 | 14.46 | 14.39 | 0.03 | 131.85 | 9.12x | 679,217 | 584,565 |
| 8 | 14.72 | 14.65 | 0.03 | 134.88 | 9.16x | 341,329 | 299,290 |
| 16 | 14.64 | 14.57 | 0.03 | 136.24 | 9.31x | 211,512 | 262,506 |
| 32 | 14.90 | 14.82 | 0.03 | — | — | 206,784 | 262,506 |

Generation wall time is host computation time; QEC depth is circuit execution layers. They measure different things. One timing sample per arm; arms were sequential on a machine with load average 9.33/7.83/5.05 near the start. The September timings used ten concurrent arms and an older cqops revision, so the historical ratio is observational and does not isolate QEC changes.

Gate/resource counts also changed: this is not a depth-only update. Raw rows for both revisions are preserved for comparison. T remains 18,840 in each current arm.

The optional QEC worker API is not used: it requires two-level factories; these unchanged n=4 configs select one-level factories.

Scope: one QTG state preparation; no amplitude amplification or full QMaxSearch.

The initial `/usr/bin/time -l` attempt produced a valid F=1 circuit and passed its checks, but the timing wrapper returned 1 after `sysctl kern.clockrate: Operation not permitted`. It is retained in logs and excluded from the table; all table rows use `time -p`.
