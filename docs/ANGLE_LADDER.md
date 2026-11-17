# Angle ladder: N, the 1/π constant, the guard width

Status: design note, 2026-11-17 (beads `kmgy.17`). The contract is BACKEND_ABI §1.5 and §1.7,
with CONSTRUCTION_RULES (CR) winning on any conflict. Line numbers below are from
`CQ_lang/spec/BACKEND_ABI.md` and `CQ_lang/spec/CONSTRUCTION_RULES.md` as read on that date.
The circuit itself is `kmgy.18`. This note fixes the numbers it needs.

Result:

| quantity | value | why |
|---|---|---|
| N (ladder precision) | **53** | smallest N whose grid step is no coarser than one ulp of every double in the top binade of the period, for both P (§2) |
| guard width g | **63** | proven by a continued-fraction bound over every exponent; the same g serves every N in [1, 128] (§4) |
| 1/π expansion | bits 1 … **N + g + 1024 = 1140** after the binary point | the highest bit any exponent's window reaches (§3) |
| window / product width | N + 53 + g = **169** bits | §5 |

Scripts: `tests/ref/k_ref.py` (the independent reference) and `tests/ref/guard_search.py`
(the search, the constant, and an integer simulation of §5 checked against the reference).

## 1. What k(a) is

ABI §1.5, lines 277–283:

> A fresh N-qubit register K ^= k(a), where k(a) = round(val(a)·2^N / P) mod 2^N for a finite
> val(a) and 0 for a non-finite one (exponent field all ones). […] The product with 2^N/P is
> the real-number product, taken mod 2^N (two's complement for a negative val:
> k(−x) = −k(x) mod 2^N), then rounded to nearest. P is irrational, so val·2^N/P is never half
> an integer for a finite val ≠ 0: the tie rule never fires, and k(±0) = 0.

and lines 294–299: "*Exact means exact:* k is the correctly rounded value for every one of the
2^64 inputs, so the circuit is a function of N alone and two backends with the same N agree.
[…] No tolerance is granted." P = 4π for Ry (§1.5, line 267) and 2π for the phase (§1.7,
line 326). CR line 161–163 says the same ("reduced exactly mod P (4π for Ry, 2π for a phase)
and rounded to nearest-even onto the grid P·k/2^N"); CR's "nearest-even" and ABI's "tie never
fires" agree, since no tie exists. ABI Q26 (line 1200) records the ruling: exact for every f64,
no tolerance.

Write P = 2^s·π, s = 2 for Ry and s = 1 for the phase, and |val| = m·2^E with m < 2^53. Then

    k(a) = ± round( m · 2^t / π ) mod 2^N,   t = E + N − s.

One constant, 1/π, serves both P; s only shifts the index by one.

The input domain is all 2^64 patterns (ABI line 265: A is an f64 rail, "bit-exact"):

| pattern | k | source |
|---|---|---|
| exponent field 0x7FF (±Inf, every NaN, any payload, any sign) | 0 | ABI line 279; CR line 402 ("non-finite angle … identity") |
| ±0 | 0 | ABI line 283 |
| subnormal (e = 0, f ≠ 0) | m = f, E = −1074 ("a subnormal has the exponent of e = 1", line 286) | the real value, no flush to zero |
| normal | m = f + 2^52, E = e − 1075 | |
| negative | −k(|x|) mod 2^N | line 281; correct because round() never meets a tie |

Every |val| below P/2^(N+1) maps to k = 0 (that includes all subnormals for N ≤ 1000): no
special case is needed for them, the arithmetic gives 0.

## 2. N = 53

The ladder fires P·2^(i−N) for bit i (line 300), so the angle actually applied on a branch is
the grid point P·k/2^N, and rounding to the grid costs at most P/2^(N+1).

But the gate interface takes `double theta` (NORTH_STAR, the seven operations), so the ladder
angles are emitted as fl(P)·2^(i−N), which is exact scaling of the double fl(P). The ladder
then sums to fl(P)·k/2^N, which is off by up to |P − fl(P)| whatever N is:

| P | \|P − fl(P)\| (floor, any N) | P/2^(N+1) at N = 52 | at N = 53 | at N = 64 |
|---|---|---|---|---|
| 4π | 4.90e−16 | 1.40e−15 | 6.98e−16 | 3.4e−19 |
| 2π | 2.45e−16 | 6.98e−16 | 3.49e−16 | 1.7e−19 |

Choice: N = 53, the smallest N for which the grid step P/2^N is at most one ulp of every double
in [P/2, P) (4π ∈ [8, 16), ulp 2^−49 = 1.78e−15, step 1.40e−15; 2π ∈ [4, 8), ulp 2^−50 =
8.9e−16, step 6.98e−16). At N = 52 both steps exceed the ulp. Above 53 the rounding error
falls below the fl(P) floor, so the extra qubits and rotations buy nothing.

The error per branch, from val mod P to the angle applied, is at most
P/2^54 + |P − fl(P)|: **1.19e−15 rad for Ry and 5.9e−16 rad for the phase** at N = 53.

A side effect worth having: a double that is the correctly rounded value of π·2^−j (M_PI/4 and
so on) maps exactly to its grid point, because fl(π)/π = 1 − 3.9e−17 and 2^(N−1−j)·3.9e−17 <
1/2 for N = 53. At N = 64 it does not: k(fl(π)) with P = 2π is 2^63 − 360, not 2^63
(checked in `k_ref.py --selfcheck`).

N stays a build-time setting (NORTH_STAR, last paragraph; CR line 40). Every number in this note
is given as a function of N, and the guard search covers N ∈ [1, 128].

## 3. The 1/π constant

Only bits of 1/π can reach K. With 1/π = Σ b_j 2^−j, bit j contributes m·b_j·2^(t−j):

- j ≤ t − N contributes a multiple of 2^N (m is an integer), so it vanishes mod 2^N;
- j > t + L, with L = 53 + g fraction bits kept, is the cut tail: it lowers y by less than
  m·2^t·2^−(t+L) < 2^(53−L) = 2^−g (§4).

So exponent t needs the window b_(t−N+1) … b_(t+L), N + L bits. The largest t is
e = 2046 (E = 971) with s = 1: t = 970 + N, so the expansion runs to bit
j = 970 + N + 53 + g = **N + g + 1023**. Bits with j ≤ 0 are 0 (1/π < 1). One extra bit
(J = N + g + 1024) lets the Ry window be taken from the same integer shifted by one:

    C = floor(2^J / π),  J = N + g + 1024 = 1140 for N = 53, g = 63      (1139 significant bits)
    window(e', s) = (C >> (2047 − e' + s − 1)) mod 2^(N+L)

where e' is the exponent field with 0 replaced by 1. For s = 2 the shift is 2047 − e' + 1; for
s = 1 it is 2047 − e', the bitwise NOT of the 11-bit e'. The shift does not depend on N or g.

Reproduce with `python3 tests/ref/k_ref.py --inv-pi 1140` (Machin's formula, certified floor) or
`tests/ref/guard_search.py 53` (Størmer's formula). Both print, and assert they agree:

```
517cc1b727220a94fe13abe8fa9a6ee06db14acc9e21c820ff28b1d5ef5de2b0db92371d2126e970032497750
4e8c90e7f0ef58e5894d39f74411afa975da24274ce38135a2fbf209cc8eb1cc1a99cfa4e422fc5defc941d8f
fc4bffef02cc07f79788c5ad05368fb69b3f6793e584dba7a31fb34f2ff516ba93dd63f5f2f8bd9e839cfbc52
9497535fdafd88fc6a
```

(one hex string, wrapped). Its first 384 bits match fdlibm's `ipio2[]` table for 2/π, a source
independent of both scripts (`k_ref.py --selfcheck`).

## 4. Guard width g = 63

**The claim.** If the window keeps L = 53 + g fraction bits and 2^−g ≤ d(t) for every t in
range, where d(t) is the least distance from m·2^t/π to a half-integer over the significands m
of that exponent, then the cut never changes k. The cut lowers y by δ ∈ [0, 2^−g), and
floor(y − δ + 1/2) = floor(y + 1/2) unless y lies within δ above a half-integer.

**The search** (`tests/ref/guard_search.py`, every t from −1075 to 1098, which is all exponents
for every N ∈ [1, 128] and both P):

1. Halving. If ‖m·α − 1/2‖ = d ≤ 1/4 then ‖m·2α‖ = 2d. So d(t) ≥ min(1/4, ½·min_m ‖m·β‖)
   with β = frac(2^(t+1)/π). The minimum is over m ∈ [1, 2^53), which contains every
   significand of every exponent, subnormals included.
2. Continued fractions. min over 1 ≤ m < 2^53 of ‖m·β‖ is ‖q_k·β‖ for the largest convergent
   denominator q_k < 2^53 (Lagrange's best-approximation theorem: ‖qβ‖ ≥ ‖q_kβ‖ for
   0 < q < q_(k+1)). The expansion is that of B/2^256, with B = floor(2^(t+257)/π) mod 2^256
   taken exactly from the integer floor(2^R/π). Replacing β by B/2^256 moves ‖mβ‖ by less than
   2^(53−256), and that margin is subtracted.
3. Small t. For t ≤ −53, y < 2^(53+t)/π ≤ 1/π, so d > 0.18 directly (step 1 would be very
   loose there).

Output:

```
N=  1 … 128  worst t=797  d >= 2^-62.539  guard g=63
max guard over N in [1, 128]: 63
```

So **g = 63 is proven sufficient for all 2^64 inputs, both P, every N ≤ 128.** Beyond 128, rerun
the script with a larger `NMAX`.

**What is proven and what is not.** Step 1 is a lower bound: it also counts m with m·α near an
integer, which cannot hurt rounding. So the search does not show that 62 bits fail. It shows two
things:

- The worst case it found is real. When the convergent's p_k is odd, q_k·α really is near 1/2.
  The hardest such double for N = 53 is a = `3d06c6cbc45dc8de` (1.0114818644225033e−14) with
  P = 2π (and 2× that value with P = 4π), at distance 2^−62.138 from a half-integer. A bound of
  2^−62 does not cover it, so 63 is the smallest g this argument can certify.
- The cut is one-sided (δ ≥ 0), and the real δ is usually below its bound. On the 30 witnesses,
  g = 62 happens to give the right k on all of them, while g = 61 gets 2 wrong, g = 59 gets 8 and
  g = 55 gets 10.

g = 63 is the design value. Saving the last bit would need an exact one-sided search
(Lefèvre's algorithm for the minimum of (b − x·a) mod 1). That is not worth one bit of a
169-bit window.

**Cross-check.** The integer pipeline of §5, run in Python with g = 63, matches `k_ref.py` on
4,060 inputs × 2 P (uniform random bit patterns, doubles in [−64, 64], specials, and every
witness with both signs): 0 mismatches.

## 5. The reduction circuit, at kernel level

Widths for N = 53, g = 63, L = 116. A is read only. Every scratch register returns to |0⟩.
Everything is X/CX/CCX (ABI line 162–164).

| step | kernel | width |
|---|---|---|
| 1 | h ^= OR(e_0..e_10) (implicit bit; [e ≠ 0]) | 11 → 1 |
| 2 | z ^= AND(e_0..e_10) ([e = 0x7FF]) | 11 → 1 |
| 3 | e'_0 = e_0 ⊕ ¬h into an ancilla (e = 0 ⇒ e' = 1); e'_1..10 are A's bits | 1 |
| 4 | W ^= window(e', s) = (C >> (2047 − e' + s − 1)) mod 2^169: a QROM over the 2046 values of e' (BJ `emit_qrom!`), or a barrel shift of the classical constant by NOT(e') (BJ `lower_var_shl!`), 11 stages | 169 |
| 5 | R ^= (m · W) mod 2^169, m = f ‖ h (53 bits): the low half of a 53 × 169 product (BJ `lower_mul_wide!`, truncated). The low bits are kept because they carry into the top | 169 |
| 6 | T = R[116..169) + R[115]: an N-bit increment controlled by the round bit (BJ `lower_add!`) | 53 |
| 7 | T = −T mod 2^53 under the sign bit (BJ `_cond_negate_inplace!`) | 53 |
| 8 | K ^= T under ¬z: CCX per bit (this is "0 under e = 0x7FF"; NaN payloads make m ≠ 0, so zeroing has to happen here or at W) | 53 |
| 9 | undo 7 … 1 | |

In general N: W and R are N + 53 + g wide, the increment and the negation N wide. For s = 1
vs s = 2, only the constant changes (C vs C >> 1). The Ry and the phase forms each get their
own fixed table or constant.

Which of the two step-4 constructions ships is decided in `kmgy.18` by measured depth (NORTH_STAR
depth-first rule). The QROM is O(2046) deep. The barrel shift is 11 controlled-shift layers over
about 1139 + 169 qubits, logarithmic apart from fan-out of each shift bit.

**Built, 2026-11-17 (`kmgy.18`).** The circuit is `src/kernels/ladder.c` (`cq_angle_k_circuit`),
the rotations `src/ladder.c`, the test `tests/test_ladder.c`. Differences from the outline above:

- Steps 6 and 7 are one N-bit add: K = (p_hi ⊕ s) + (p_round ⊕ s), since −(x + r) = ~x + ~r mod
  2^N. Step 8's zero is applied to m instead (m = significand AND NOT [e = 0x7FF], 53 CCX off the
  critical path): m = 0 gives p = 0 and K = 0 with either sign.
- Step 4: the outline listed both constructions and assumed neither. Both are built and
  registered as the kernel operation `angle_k` (i64 only; docs/DEPTH.md). The barrel goes most
  significant shift bit first, so stage b keeps only 169 + 2^b − 1 bits (1192 at the first,
  classical-input stage, which folds to CX); each stage is a mux with its control fanned out,
  emitted gate kind by gate kind. The table is a one-hot decoder of e' and a CX per 1-bit of
  every exponent's window (a QROM without unary iteration). Measured, all 64 bits of A qubits,
  compute + copy into K + uncompute, P = 4π:

  | window | depth | gates | qubits |
  |---|---|---|---|
  | **barrel (ships)** | 245 | 163,127 | 47,860 |
  | table | 1,854 | 340,581 | 44,596 |

  The 53 × 169 multiply is ~31 deep of the barrel's ~120 forward. A whole `ry_<W>_qangle` call is
  depth ~300 (the reduction, N = 53 rounds of controlled Ry, the undo) at every width: for W ≤ N
  target q takes K_((q+r) mod N) in round r; a wider rail takes ⌈W/N⌉ fanned-out copies of K.
- K matches `k_ref.py` on all 2172 vectors of `tests/ref/k_vectors.txt` with A fully quantum, with
  both window constructions (`CQ_LADDER_FULL=1`; the default ctest run takes a fixed subset).

The classical-valued A path (ABI line 304–305) computes the same k on the host. It must use this
same arithmetic or `k_ref.py`'s values, not `round(x * 2^N / P)` in double: that is wrong for
large |x|.

## 6. Open questions

1. **Ladder angles are doubles.** ABI line 268–270 says the ladder is "exact when val lies on the
   grid", but the seven-operation interface only carries `double`, so P·2^(i−N) is emitted as
   fl(P)·2^(i−N), off by |P − fl(P)| in total (§2). The ABI does not say which angle to emit.
   This note assumes fl(P)·2^(i−N), an exact power-of-two scaling. To confirm with the ABI
   owner. *Closed 2026-11-17:* decided as fl(P)·2^(i−N) (beads `kmgy.18`) and built so.
2. **The one-rotation classical path.** It emits Ry(P·k/2^N) as a double, one more rounding,
   fl(fl(P)·k·2^−N). It can differ from the quantum ladder's total by half an ulp. The ABI
   requires both to use the same k, which they do. It does not require the same double.
   *Measured 2026-11-17:* on 199 sampled finite doubles (P = 4π) the one rotation and the exact sum of
   the ladder angles K selects differ by at most 8.8e−16 rad, 0.495 ulp of the angle.
3. **N above 128.** Covered by rerunning `guard_search.py` with a larger `NMAX`. Nothing is
   assumed about it here.
4. **g = 62 is likely enough** (one-sided error, see §4) but not proven. Kept at 63.
5. **The ABI leaves to the backend** (line 284): N, the circuit, the window cut, and the QROM
   versus barrel-shift choice. It fixes k as a function of the 64 bits and N, so none of these
   choices is visible to a program. *2026-11-17:* the barrel shift ships, by measured depth (§5).

## 7. Reference for tests

`tests/ref/k_ref.py`, standard library only, no 1/π window and no truncated product. It brackets
π by Machin's formula in integers, brackets val·2^N/P with exact `Fraction`s, and accepts a
rounding only when both ends agree (otherwise it doubles the precision). So every printed k is
certified. `--selfcheck` checks π against 50 known digits and 2/π against fdlibm's table, the
hand-derived k(fl(π)) values, the non-finite and zero cases, k(−x) = −k(x), and agreement with a
second route (the `decimal` module, π from a different series) on 600 samples × 2 P × 2 N.

```
$ python3 tests/ref/k_ref.py --selfcheck          # all "ok", exit 0, ~5 s
$ python3 tests/ref/k_ref.py --N 53 --count 1000 --seed 1 > vectors.txt
# k_ref.py N=53 seed=1: <a> <P> <N> <k>
7fefffffffffffff 4pi 53 7fcc3ea616b1b
7fefffffffffffff 2pi 53 ff987d4c2d636
$ python3 tests/ref/k_ref.py --bits 3d06c6cbc45dc8de      # explicit patterns
```

Tests in `kmgy.18` should feed the §4 witnesses explicitly (`guard_search.py` prints them),
since random sampling almost never lands near a boundary.
