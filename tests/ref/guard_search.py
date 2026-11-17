#!/usr/bin/env python3
"""Guard-width search for the angle-ladder reduction (docs/ANGLE_LADDER.md §4).

For a double |val| = m * 2^E (m < 2^53) and P = 2^s * pi, y = val*2^N/P = m * alpha_t with
alpha_t = 2^t / pi, t = E + N - s. Truncating 1/pi after bit t + 53 + g lowers y by
delta < 2^-g, so round(y) is safe whenever y is at least 2^-g from every half-integer.

Lower bound on d(t) = min over 1 <= m < 2^53 of || m*alpha_t - 1/2 ||:
  * if || m*alpha - 1/2 || = d <= 1/4 then || m * 2alpha || = 2d, so
        d(t) >= min(1/4, min_m || m * beta || / 2),   beta = frac(2^(t+1) / pi);
  * min over 1 <= m < M of || m*beta || is || q_k*beta || for the largest continued-fraction
    denominator q_k < M (Lagrange: || q*beta || >= || q_k*beta || for 0 < q < q_{k+1});
  * beta is replaced by B/2^Q, B = floor(2^(t+1+Q)/pi) mod 2^Q exactly, which moves
    || m*beta || by less than 2^(53-Q); that margin is subtracted;
  * for t <= -53, y < 2^(53+t)/pi < 1/2 - 0.18, so d(t) > 0.18 directly.
Every exponent t is covered, so the bound holds for all 2^64 patterns (the m range used,
[1, 2^53), contains every significand of every exponent, subnormals included).

A witness is printed for the worst t when p_k is odd (then q_k * alpha_t really is near
1/2). The script then runs the circuit's integer pipeline (window, truncated product,
rounding, negation) against k_ref.py: it must agree at guard g and fail at g - 2 on the
witness. pi here is Stormer's formula, not k_ref.py's Machin formula.
"""
import math
import os
import random
import sys
from fractions import Fraction

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import k_ref  # noqa: E402  (the independent reference; used only for the cross-check)

M = 1 << 53
Q = 256
NMAX = 128
T_LO = (1 - 1075) + 1 - 2          # e' = 1, N = 1, s = 2
T_HI = (2046 - 1075) + NMAX - 1    # e' = 2046, N = NMAX, s = 1
R = T_HI + 1 + Q + 8


def atan_inv(x, prec):
    p, x2, tot, k, sg = (1 << prec) // x, x * x, 0, 0, 1
    while p:
        tot += sg * (p // (2 * k + 1))
        p //= x2
        k += 1
        sg = -sg
    return tot, 4 * (k + 2)


def pi_bounds(q):
    g = 64
    terms = [(44, 57), (7, 239), (-12, 682), (24, 12943)]  # Stormer 1896: pi/4 = sum c*atan(1/x)
    approx = err = 0
    for c, x in terms:
        a, e = atan_inv(x, q + g)
        approx += 4 * c * a
        err += 4 * abs(c) * e
    return (approx - err) >> g, -((-(approx + err)) >> g)


def inv_pi_floor(J):
    lo, hi = pi_bounds(J + 64)
    a, b = (1 << (J + J + 64)) // hi, (1 << (J + J + 64)) // lo
    assert a == b, "raise precision"
    return a


X = inv_pi_floor(R)                # floor(2^R / pi); floor(2^j/pi) = X >> (R - j) exactly


def floor_pow2_over_pi(j):
    return X >> (R - j) if j >= 0 else 0


def bound(t):
    """(lower bound on d(t) as a Fraction, witness m or None)."""
    if t <= -53:
        lo, hi = pi_bounds(64)
        return Fraction(1, 2) - Fraction(M << 64, hi) * Fraction(2) ** t, None
    B = floor_pow2_over_pi(t + 1 + Q) % (1 << Q)
    # continued fraction of B / 2^Q
    a, b = B, 1 << Q
    p0, q0, p1, q1 = 1, 0, 0, 1    # convergents p_{-1}/q_{-1}, p_{-2}/q_{-2} arranged for the recurrence
    pk = qk = None
    while b:
        c = a // b
        a, b = b, a - c * b
        p0, p1 = c * p0 + p1, p0
        q0, q1 = c * q0 + q1, q0
        if q0 >= M:
            break
        pk, qk = p0, q0
    # a is now irrelevant; pk/qk is the last convergent with qk < M (B/2^Q itself if b hit 0)
    beta = Fraction(B, 1 << Q)
    hom = abs(qk * beta - pk) - Fraction(M, 1 << Q)
    d = min(Fraction(1, 4), hom / 2)
    return d, (qk if pk % 2 else None)


def need_g(d):
    g = 0
    while Fraction(1, 1 << g) > d:
        g += 1
    return g


def k_circuit(bits, N, s, g):
    """The reduction as docs/ANGLE_LADDER.md §5 lays it out, in integers."""
    L = 53 + g
    sign, e, f = bits >> 63, (bits >> 52) & 0x7FF, bits & ((1 << 52) - 1)
    h = int(e != 0)
    m = f | (h << 52)
    e1 = e | (1 - h)                                   # subnormal: exponent of e = 1
    C = floor_pow2_over_pi(N + g + 1024 + 1 - s)       # the one constant, s = 1 or 2
    W = (C >> (2047 - e1)) % (1 << (N + L))            # window: bits t-N+1 .. t+L of 1/pi
    prod = (m * W) % (1 << (N + L))                    # low N+L bits of the product
    top = prod >> (L - 1)                              # N bits + round bit
    K = ((top >> 1) + (top & 1)) % (1 << N)
    if sign:
        K = (-K) % (1 << N)
    return 0 if e == 0x7FF else K                      # copy-out under e != 0x7FF


def main():
    N0 = int(sys.argv[1]) if len(sys.argv) > 1 else 53
    print("t range [%d, %d], Q = %d, m in [1, 2^53)" % (T_LO, T_HI, Q))
    res = {t: bound(t) for t in range(T_LO, T_HI + 1)}
    gt = {t: need_g(res[t][0]) for t in res}

    def worst(N):
        ts = range((1 - 1075) + N - 2, (2046 - 1075) + N - 1 + 1)
        return max(ts, key=lambda t: (gt[t], -res[t][0]))

    for N in sorted({1, 32, 52, N0, 64, 96, NMAX}):
        t = worst(N)
        print("N=%3d  worst t=%5d  d >= 2^%.3f  guard g=%d" % (N, t, math.log2(res[t][0]), gt[t]))
    gmax = max(gt[worst(N)] for N in range(1, NMAX + 1))
    print("max guard over N in [1, %d]: %d" % (NMAX, gmax))
    hist = {}
    for t in res:
        hist[gt[t]] = hist.get(gt[t], 0) + 1
    print("exponents per required guard:", dict(sorted(hist.items())[-8:]))

    g = gt[worst(N0)]
    J = N0 + g + 1024
    C = floor_pow2_over_pi(J)
    print("\nN=%d: guard g=%d, L=53+g=%d, window N+L=%d bits, product mod 2^%d" % (N0, g, 53 + g, N0 + 53 + g, N0 + 53 + g))
    print("1/pi needed to bit %d (J = N+g+1024); C = floor(2^J/pi) has %d bits" % (J, C.bit_length()))
    print("C (hex) =", "%x" % C)
    assert C == k_ref.inv_pi(J), "Stormer and Machin disagree on 1/pi"
    print("C agrees with k_ref.inv_pi(J) (Machin)")

    # witnesses: the near-half cases the bound can see, sorted worst first
    def exact_d(xb, s):
        """|frac(val*2^N0/P) - 1/2|, to 2^-140, from X."""
        sign, m, E = k_ref.decode(xb)
        t = E + N0 - s
        Y = m * floor_pow2_over_pi(t + 200)
        return abs(Fraction(Y % (1 << 200), 1 << 200) - Fraction(1, 2))

    wit = {}
    for t, (d, m) in res.items():
        if m is not None and gt[t] >= g - 3:
            for s in (1, 2):
                E = t - N0 + s
                x = Fraction(m) * Fraction(2) ** E
                if -1074 <= E and E + m.bit_length() <= 1024 and Fraction(float(x)) == x:
                    xb = k_ref.f2b(float(x))
                    wit[(xb, s)] = exact_d(xb, s)
    wit = sorted((d, s, xb) for (xb, s), d in wit.items())
    print("\nhardest witnesses for N=%d (%d distinct, exact distance to a half-integer):" % (N0, len(wit)))
    for d, s, xb in wit[:6]:
        print("  a=%016x (%.17g) P=%s  d=2^%.3f" % (xb, k_ref.decode(xb)[1] * 2.0 ** k_ref.decode(xb)[2],
                                                    "2pi" if s == 1 else "4pi", math.log2(d)))
    wit = [(d, None, s, xb) for d, s, xb in wit]

    rng = random.Random(1)
    xs = k_ref.samples(3000, 11) + [w[3] for w in wit] + [w[3] ^ (1 << 63) for w in wit]
    xs += [rng.getrandbits(64) for _ in range(1000)]
    bad = [(b, s) for b in xs for s in (1, 2)
           if k_circuit(b, N0, s, g) != k_ref.k_ref(b, N0, "2pi" if s == 1 else "4pi")]
    print("\npipeline at g=%d vs k_ref on %d inputs x 2 P: %d mismatches" % (g, len(xs), len(bad)))
    for gg in (g - 1, g - 2, g - 4, g - 8):
        bad2 = [w for w in wit if k_circuit(w[3], N0, w[2], gg) != k_ref.k_ref(w[3], N0, "2pi" if w[2] == 1 else "4pi")]
        print("pipeline at g=%d: %d of %d witnesses wrong" % (gg, len(bad2), len(wit)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
