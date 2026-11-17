# Twenty-item QTG: one measured arm, remaining arms estimated

QEC `89d5bf479ab4397d4cc96702ecb1d5bd2ad4e46a`; cqops `27d9b4cea3e010298982fc1f114211d07a0caf75`. Release, n=20, noiseless, serial QEC, no trace, sequential arms. Apple clang version 17.0.0 (clang-1700.6.4.2).
User stopped the sweep after F=1 completed; F=2 was interrupted, F=4/8/16/32 were not run.

| Factories | Generation estimate (s) | Depth measured/estimated | Status |
|---:|---:|---:|---|
| 1 | 423 | 34,980,350 | measured |
| 2 | 420 | 17,522,907 | estimated |
| 4 | 421 | 8,761,878 | estimated |
| 8 | 429 | 4,308,567 | estimated |
| 16 | 427 | 2,409,226 | estimated |
| 32 | 434 | 2,355,372 | estimated |

Generation estimates scale the measured n=20 F=1 time by the current n=4 per-factory wall-time ratios. Treat all estimates as roughly 7 minutes (about 6.5–7.5 minutes), not precise predictions. More factories shorten circuit depth without dividing host generation work.

Depth estimates apply each factory count's measured n=4 September-to-current depth ratio to its September n=20 depth. This assumes the update has a similar effect at both sizes; only F=1 is measured at n=20. The prior factory knee differed between sizes, so the F=16/32 floor estimates are especially uncertain.

Measured F=1: wall 423.04 s, user 420.32 s, sys 1.02 s; rc=0, rotations passed, d=13 nlog=930 T=168600. Depth 34,980,350 vs September 30,158,209 (+15.99%). Gates 131,088,462,274 vs 123,658,494,600 (+6.01%). Historical wall 3043.58 s (7.19x observed speed ratio); older cqops and concurrency differ.

The interrupted F=2 timing is partial and excluded. No completed result or correctness claim for F=2 is made. Scope is one QTG state preparation, no full QMaxSearch.
