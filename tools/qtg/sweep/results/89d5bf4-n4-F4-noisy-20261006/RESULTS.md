# n=4, four factories: noise and Pauli frame enabled

QEC `89d5bf479ab4397d4cc96702ecb1d5bd2ad4e46a`; cqops `27d9b4cea3e010298982fc1f114211d07a0caf75`. Fresh Release libraries reused from the noiseless sweep.
Seed 42, serial QEC, no trace. Physical sizing error rate 1e-3; pX=pY=pZ=1e-3/3, measurement noise pM=1e-3. Pauli frame enabled; union_find decoder configured on. Total Pauli probability is 1e-3; measurement noise is independent. The pM value was a stated assumption because only X/Y/Z were specified.

| Metric | Noiseless F=4 | This run | Change |
|---|---:|---:|---:|
| Host generation (s) | 14.46 | 32.97 | 2.28x |
| depth | 679,217 | 674,391 | -0.711% |
| gates | 4,237,223,217 | 4,158,897,009 | -1.849% |
| T | 18,840 | 18,840 | +0.000% |
| nphys | 26,585 | 26,585 | +0.000% |
| nlog | 126 | 126 | +0.000% |
| reserved | 186,760 | 186,760 | +0.000% |
| above | 342 | 342 | +0.000% |

CPU: user 32.35 s; sys 0.07 s. Program exit 0; ordered controlled-Ry checks passed. Instrument: /usr/bin/time -p and qtg_depth_phys.c.

## What decoder enabled means here

The report returned decoder=1 but decodes=0; all decoder work counters were zero. Source inspection confirms qec_decoder_uf_decode/qec_decoder_uf_apply are invoked by pl_memory.c, not this QTG generation driver. This run exercises noise sampling, syndrome collection and Pauli frame behavior with the decoder configured, but does not execute the union-find algorithm. It does not measure decoded circuit success or failure probability. A separate memory experiment can exercise the decoder but cannot provide a QTG circuit failure rate.

One seeded run: resource differences with frame enabled are sample-dependent; no statistical estimate of circuit reliability or depth distribution is claimed.

Raw resource and physics rows:
```text
QEC d=9 gates=4158897009 depth=674391 T=18840 CX=2656412388 H=741638826 nphys=26585 nlog=126 reserved=186760 above=342
PHYS bits=714164994 bits_x=361382220 bits_z=352782774 rounds=2959434 round_max=2186 round_min=8 decodes=0 find=0 union=0 grow=0 peel=0 corr=0 p=(0.000333333,0.000333333,0.000333333,0.001) d=9 decoder=1 seed=42 noise_seed=2949826092126892291 outcome_seed=5139283748462763858 shot=0
```

Fault sampling subsequently verified: 717,119 sampled Pauli faults and 713,544 measurement-bit flips. See [fault verification](FAULT_VERIFICATION.md) for diagnostics and model limitations.
