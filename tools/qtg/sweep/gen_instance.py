import random, math, itertools, sys

def bits(v): return v.bit_length()          # floor(log2 v)+1 for v>=1

def greedy(w, p, c):
    """Section IV greedy: sort by decreasing density, pack; x' in ITEM-INDEX order."""
    order = sorted(range(len(w)), key=lambda i: (-p[i]/w[i], i))
    x = [0]*len(w); rem = c
    for i in order:
        if w[i] <= rem:
            x[i] = 1; rem -= w[i]
    return x, rem, sum(p[i] for i in range(len(w)) if x[i])

def optimum(w, p, c):
    """Exact DP over integer capacity."""
    best = [0]*(c+1)
    for wi, pi in zip(w, p):
        for cc in range(c, wi-1, -1):
            v = best[cc-wi]+pi
            if v > best[cc]: best[cc] = v
    return best[c]

N = 20
rnd = random.Random(20260904)          # fixed seed -- the instance is reproducible

best = None
for trial in range(200000):
    w = [rnd.randint(2, 14) for _ in range(N)]
    p = [rnd.randint(1, 11) for _ in range(N)]
    if sum(p) > 127: continue           # |P_bin| <= 7: the profit rail cannot overflow
    c = sum(w)//2
    if not (64 <= c <= 127): continue    # |c_bin| == 7 exactly, no widening
    x, rem, gp = greedy(w, p, c)
    if sum(x) < 8: continue              # a path that actually branches a lot
    opt = optimum(w, p, c)
    # want greedy to be GOOD but not trivially optimal -- that is the interesting regime
    if gp == opt: continue
    gap = (opt-gp)/opt
    if not (0.01 <= gap <= 0.06): continue
    cand = (abs(sum(p)-120), trial, w, p, c, x, rem, gp, opt)
    if best is None or cand[0] < best[0]: best = cand
    if best[0] == 0: break

_, trial, w, p, c, x, rem, gp, opt = best
WC, WP = bits(c), bits(sum(p))
print("trial", trial)
print("w =", w, "sum", sum(w))
print("p =", p, "sum", sum(p))
print("c =", c, "WC", WC, "WP", WP)
print("x' =", x, "selected", sum(x))
print("greedy cap left", rem, "greedy profit", gp, "OPT", opt, "gap %.3f%%" % (100*(opt-gp)/opt))

# index-order walk must be feasible (it is, for a feasible set) -- assert it
cap = c
for m in range(N):
    if x[m]:
        assert cap >= w[m], (m, cap)
        cap -= w[m]
assert cap == rem and sum(p[m] for m in range(N) if x[m]) == gp
print("index-order walk OK; final cap", cap)
