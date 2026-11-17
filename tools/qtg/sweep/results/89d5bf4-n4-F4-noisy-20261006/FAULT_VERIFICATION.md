# Fault verification for the noisy n=4 F=4 QTG

The initial report did not expose sampled-fault counters. Zero decode invocations did not mean zero faults. A scratch driver reads the existing internal qec_noise_diag_take and qec_noise_census_take counters after the circuit and physics report complete. No QEC/library source was changed.

Same config hash, libraries and seed as the original run; resource and PHYS rows are byte-identical. Program exit 0 and ordered rotation checks passed.

| Diagnostic | Count |
|---|---:|
| sampled_rounds | 2,959,434 |
| faults | 717,119 |
| measurement_flips | 713,544 |
| readouts | 354,795 |
| readout_flips | 213,643 |
| logical_paulis_propagated | 205,367 |
| dropped_accumulator_bits | 12,490 |
| unmodelled_readout_flips | 8,276 |
| injections | 20,096 |
| injection_tdg_flips | 13,582 |

The noise sampler definitely ran and sampled hundreds of thousands of faults. The decoder still ran zero times. Physical Pauli bookkeeping and propagated readout errors do not establish decoded circuit success.

The census also exposes model limitations: dropped_accumulator_bits counts error bits cleared on reclaim without a readout fold; unmodelled_readout_flips counts readout flips with consequences outside the represented Pauli propagation; injection_tdg_flips counts noisy magic-resource branch changes recorded but not acted on. These are the existing gaps documented in QEC src/noise.h. None of these numbers is a QTG logical failure probability.

Instrument: scratch qtg_faults.c + current internal QEC noise diagnostics. Release QEC 89d5bf479ab4397d4cc96702ecb1d5bd2ad4e46a, cqops 27d9b4cea3e010298982fc1f114211d07a0caf75, seed42, pX=pY=pZ=1e-3/3 pM=1e-3, frame=true decoder=union_find.
Repeat timing: wall 59.66s, user 58.36s. Timing varied relative to the original; the fault counts and circuit/physics rows are deterministic.

```text
NOISE sampled_rounds=2959434 faults=717119 measurement_flips=713544 readouts=354795 readout_flips=213643 logical_paulis_propagated=205367 dropped_accumulator_bits=12490 unmodelled_readout_flips=8276 injections=20096 injection_tdg_flips=13582
```
