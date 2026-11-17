#!/usr/bin/env python3
"""Independent reference for the angle-ladder index k(a), BACKEND_ABI §1.5 / §1.7.

    k(a) = round(val(a) * 2^N / P) mod 2^N   for finite val(a), P = 4*pi (Ry) or 2*pi (phase)
    k(a) = 0                                 for exponent field 0x7FF (Inf, NaN)

The real-number product, rounded to nearest (P irrational: no ties for val != 0), then
mod 2^N in two's complement. a is any of the 2^64 double bit patterns.

Method, kept deliberately unlike the circuit's (docs/ANGLE_LADDER.md): no 1/pi window, no
truncated product. pi is bracketed, lo <= pi*2^Q <= hi, by Machin's formula in integer
arithmetic; val*2^N/P is bracketed with exact Fractions; the rounding is accepted only when
both ends of the bracket round alike, else Q doubles. So every k it prints is certified.

Usage:
    k_ref.py --selfcheck
    k_ref.py [--N 53] [--P 4pi|2pi|both] [--count 100] [--seed 1] [--bits HEX ...]
    k_ref.py --inv-pi J          # floor(2^J / pi) in hex

Output lines: <a as 16 hex digits> <P> <N> <k in hex>. Lines starting with '#' are comments.
Standard library only.
"""
import argparse
import random
import struct
import sys
from fractions import Fraction

_pi_cache = {}


def _atan_inv(x, prec):
    """Integer A with |A - atan(1/x) * 2^prec| <= 4 * (terms + 2)."""
    power = (1 << prec) // x
    x2 = x * x
    total, k, sign = 0, 0, 1
    while power:
        total += sign * (power // (2 * k + 1))
        power //= x2
        k += 1
        sign = -sign
    return total, 4 * (k + 2)


def pi_bounds(q):
    """(lo, hi) integers with lo <= pi * 2^q <= hi, hi - lo small."""
    if q not in _pi_cache:
        guard = 64
        a5, e5 = _atan_inv(5, q + guard)
        a239, e239 = _atan_inv(239, q + guard)
        approx = 16 * a5 - 4 * a239          # Machin: pi = 16 atan(1/5) - 4 atan(1/239)
        err = 16 * e5 + 4 * e239
        _pi_cache[q] = ((approx - err) >> guard, -((-(approx + err)) >> guard))
    return _pi_cache[q]


def decode(bits):
    """(sign, m, E) with |val| = m * 2^E, or None for Inf/NaN."""
    sign = bits >> 63
    e = (bits >> 52) & 0x7FF
    f = bits & ((1 << 52) - 1)
    if e == 0x7FF:
        return None
    if e == 0:
        return sign, f, -1074                # subnormal: the exponent of e = 1, no implicit bit
    return sign, f | (1 << 52), e - 1075


def k_ref(bits, N, P):
    """k(a) for the 64-bit pattern `bits`; P is '4pi' or '2pi'."""
    s = {"4pi": 2, "2pi": 1}[P]              # P = 2^s * pi, so val*2^N/P = m * 2^(E+N-s) / pi
    d = decode(bits)
    if d is None:
        return 0
    sign, m, E = d
    if m == 0:
        return 0
    sh = E + N - s
    q = max(64, sh + 53 + 96)
    while True:
        lo, hi = pi_bounds(q)
        num = Fraction(m) * Fraction(2) ** (sh + q)   # y = num / (pi * 2^q)
        half = Fraction(1, 2)
        r_lo = (num / hi + half).__floor__()
        r_hi = (num / lo + half).__floor__()
        if r_lo == r_hi:
            k = r_lo % (1 << N)
            return (-k) % (1 << N) if sign else k
        q *= 2


def inv_pi(J):
    """floor(2^J / pi), certified by the pi bracket."""
    q = J + 64
    while True:
        lo, hi = pi_bounds(q)
        a, b = (1 << (J + q)) // hi, (1 << (J + q)) // lo
        if a == b:
            return a
        q *= 2


def f2b(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def samples(count, seed):
    """Seeded: specials, then alternating uniform 64-bit patterns and doubles in [-64, 64]."""
    out = [0, 1 << 63, 1, (1 << 63) | 1, 0x000FFFFFFFFFFFFF, 0x0010000000000000,
           0x7FEFFFFFFFFFFFFF, 0xFFEFFFFFFFFFFFFF, 0x7FF0000000000000, 0xFFF0000000000000,
           0x7FF8000000000000, 0x7FF0000000000001, f2b(3.141592653589793), f2b(-1.0)]
    rng = random.Random(seed)
    while len(out) < count:
        out.append(rng.getrandbits(64) if len(out) % 2 else f2b(rng.uniform(-64.0, 64.0)))
    return out[:count]


# --- self-check ------------------------------------------------------------------------

def _decimal_k(bits, N, P, prec=520):
    """Second, separate route: decimal module, pi by the Python docs' series (not Machin)."""
    from decimal import Decimal, localcontext
    d = decode(bits)
    if d is None or d[1] == 0:
        return 0
    sign, m, E = d
    s = {"4pi": 2, "2pi": 1}[P]
    with localcontext() as ctx:
        ctx.prec = prec + 2
        three = Decimal(3)
        lasts, t, ssum, n, na, dd, da = 0, three, 3, 1, 0, 0, 24
        while ssum != lasts:
            lasts = ssum
            n, na = n + na, na + 8
            dd, da = dd + da, da + 32
            t = (t * n) / dd
            ssum += t
        y = Decimal(m) * Decimal(2) ** (E + N - s) / ssum
        fl = int(y.to_integral_value(rounding="ROUND_FLOOR"))
        frac = y - fl
        assert abs(frac - Decimal("0.5")) > Decimal(10) ** -100, "decimal route too close to a tie"
        k = (fl + (1 if frac > Decimal("0.5") else 0)) % (1 << N)
    return (-k) % (1 << N) if sign else k


def selfcheck():
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("ok   " if cond else "FAIL ") + what)
        ok &= bool(cond)

    lo, hi = pi_bounds(200)
    check(hi - lo < 1 << 8, "pi bracket at 2^-200 is narrow")
    dig = "31415926535897932384626433832795028841971693993751"
    check(lo * 10 ** 49 >> 200 == int(dig) or hi * 10 ** 49 >> 200 == int(dig),
          "pi = 3.1415926535897932384626433832795028841971693993751 (50 digits)")
    # 2/pi from fdlibm's k_rem_pio2 ipio2[] table, 24 bits per entry (independent source)
    ipio2 = [0xA2F983, 0x6E4E44, 0x1529FC, 0x2757D1, 0xF534DD, 0xC0DB62, 0x95993C, 0x439041,
             0xFE5163, 0xABDEBB, 0xC561B7, 0x246E3A, 0x424DD2, 0xE00649, 0x2EEA09, 0xD1921C]
    want = 0
    for w in ipio2:
        want = (want << 24) | w
    check(inv_pi(24 * len(ipio2) + 1) == want, "2/pi matches fdlibm ipio2[0..15] (384 bits)")

    # hand-derived: fl(pi)/pi = 1 - 3.8981718e-17
    fpi = f2b(3.141592653589793)
    check(k_ref(fpi, 53, "2pi") == 1 << 52, "k(fl(pi)), N=53, P=2pi = 2^52")
    check(k_ref(fpi, 64, "2pi") == (1 << 63) - 360, "k(fl(pi)), N=64, P=2pi = 2^63 - 360")
    check(k_ref(fpi, 53, "4pi") == 1 << 51, "k(fl(pi)), N=53, P=4pi = 2^51")
    check(k_ref(f2b(-3.141592653589793), 53, "4pi") == (1 << 53) - (1 << 51), "k(-fl(pi)) = -2^51")

    for b in (0, 1 << 63, 0x7FF0000000000000, 0xFFF0000000000000, 0x7FF8000000000000,
              0x7FF0000000000001, 0xFFFFFFFFFFFFFFFF):
        check(k_ref(b, 53, "4pi") == 0 and k_ref(b, 53, "2pi") == 0, "k(%016x) = 0" % b)
    check(k_ref(1, 53, "4pi") == 0, "smallest subnormal -> 0")

    xs = samples(600, 7)
    bad = [b for b in xs for P in ("4pi", "2pi")
           if k_ref(b ^ (1 << 63), 53, P) != (-k_ref(b, 53, P)) % (1 << 53)]
    check(not bad, "k(-x) = -k(x) mod 2^N on 600 samples")
    bad = [(b, P, N) for i, b in enumerate(xs) for P in ("4pi", "2pi") for N in (53, 64 + i % 3)
           if k_ref(b, N, P) != _decimal_k(b, N, P)]
    check(not bad, "Fraction/Machin route == decimal/series route on 600 samples x 2 P x 2 N")
    if bad:
        print("  first mismatch:", bad[0])
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--N", type=int, default=53)
    ap.add_argument("--P", choices=["4pi", "2pi", "both"], default="both")
    ap.add_argument("--count", type=int, default=100)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--bits", nargs="*", default=[], help="explicit 64-bit patterns in hex")
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("--inv-pi", type=int, metavar="J")
    a = ap.parse_args()
    if a.selfcheck:
        sys.exit(0 if selfcheck() else 1)
    if a.inv_pi is not None:
        print("%x" % inv_pi(a.inv_pi))
        return
    xs = [int(h, 16) for h in a.bits] if a.bits else samples(a.count, a.seed)
    Ps = ["4pi", "2pi"] if a.P == "both" else [a.P]
    print("# k_ref.py N=%d seed=%d: <a> <P> <N> <k>" % (a.N, a.seed))
    for b in xs:
        for P in Ps:
            print("%016x %s %d %x" % (b, P, a.N, k_ref(b, a.N, P)))


if __name__ == "__main__":
    main()
