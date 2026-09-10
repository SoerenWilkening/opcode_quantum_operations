#!/usr/bin/env python3
"""Emit the lab report's figure data from what is ON DISK, never from a formula.

Every number a labreport figure plots arrives through this script, and the
reason is `bd j75`: a figure quoted from a remembered formula is one nobody
re-derives, and the one we found was wrong in the direction that flattered its
own argument. So:

  goldens.dat  <- tests/goldens/*.counts, the L4 pins themselves
  scratch.dat  <- a probe RUN against build-release/libcqops.a

A missing input is a loud skip that names what is missing, never a silent
fallback to a hard-coded table.
"""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT  = os.path.join(ROOT, "docs", "labreport", "data")
REL  = os.path.join(ROOT, "build-release")


def sh(*cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)


def provenance():
    """The SHA the data was taken at — a figure without one pins nothing."""
    r = sh("git", "rev-parse", "--short", "HEAD")
    head = r.stdout.strip() or "unknown"
    dirty = " (dirty)" if sh("git", "status", "--porcelain").stdout.strip() else ""
    return head + dirty


def goldens(head):
    """The L4 gate-count pins, one row per (kernel, pass, W)."""
    src = os.path.join(ROOT, "tests", "goldens")
    if not os.path.isdir(src):
        return "no tests/goldens/ on disk"
    rows = []
    for name in sorted(os.listdir(src)):
        if not name.endswith(".counts"):
            continue
        for line in open(os.path.join(src, name)):
            if line.startswith("#") or not line.strip():
                continue
            f = line.split()
            if len(f) >= 6:
                rows.append((f[0], f[1], int(f[2]), int(f[3]), int(f[4]), int(f[5])))
    if not rows:
        return "tests/goldens/ held no data rows"
    with open(os.path.join(OUT, "goldens.dat"), "w") as fh:
        fh.write("# kernel pass W not cnot toffoli\n")
        fh.write("# tests/goldens/*.counts verbatim, at %s.\n" % head)
        fh.write("# ALL-QUANTUM operand mask — a count is a function of\n"
                 "# (W, mask) and a golden that does not say which pins nothing.\n")
        for r in rows:
            fh.write("%s %s %d %d %d %d\n" % r)
    return None


def toffoli(head):
    """Toffoli count vs W, one column per kernel — the same golden rows, pivoted
    so pgfplots can read them. `nan` where a kernel does not ship that width:
    the ladders genuinely differ (icmp is i80 and not i128; divrem the reverse),
    and filling a gap would invent a width."""
    src = os.path.join(OUT, "goldens.dat")
    if not os.path.exists(src):
        return "data/goldens.dat not written"
    cell, widths, kernels = {}, set(), set()
    for line in open(src):
        if line.startswith("#") or not line.strip():
            continue
        k, p_, W, _n, _c, t = line.split()
        if p_ != "forward":
            continue
        cell[(k, int(W))] = int(t)
        widths.add(int(W)); kernels.add(k)
    if not cell:
        return "no forward rows in data/goldens.dat"
    ks = sorted(kernels)
    with open(os.path.join(OUT, "toffoli.dat"), "w") as fh:
        fh.write("# Toffoli count vs W, FORWARD pass, ALL-QUANTUM mask.\n")
        fh.write("# Pivoted from data/goldens.dat at %s. nan = width not shipped.\n" % head)
        fh.write("W " + " ".join(ks) + "\n")
        for W in sorted(widths):
            fh.write("%d " % W + " ".join(
                str(cell.get((k, W), "nan")) for k in ks) + "\n")
    return None


def scratch(head):
    """K11/K12 peak and region, MEASURED by running the probe."""
    lib = os.path.join(REL, "libcqops.a")
    sup = os.path.join(REL, "tests", "libcqops_test_support.a")
    for p in (lib, sup):
        if not os.path.exists(p):
            return "%s absent — configure and build build-release first" % \
                   os.path.relpath(p, ROOT)
    exe = os.path.join(OUT, ".probe_scratch")
    cc = os.environ.get("CC", "cc")
    r = sh(cc, "-I", "src", "-I", "include", "-I", "tests", "-I", "tests/support",
           "-o", exe, "tools/labreport/probe_scratch.c", sup, lib)
    if r.returncode:
        return "probe did not build:\n" + r.stderr.strip()
    dat = os.path.join(OUT, "scratch.dat")
    r = subprocess.run([exe, dat], capture_output=True, text=True)
    os.unlink(exe)
    if r.returncode:
        return "probe did not run: " + r.stderr.strip()
    with open(dat) as fh:
        body = fh.read()
    with open(dat, "w") as fh:
        fh.write(body.replace("build-release/libcqops.a.",
                              "build-release/libcqops.a at %s.\n#" % head, 1))
    return None


def main():
    os.makedirs(OUT, exist_ok=True)
    head = provenance()
    bad = False
    for label, fn in (("goldens.dat", goldens), ("toffoli.dat", toffoli),
                      ("scratch.dat", scratch)):
        why = fn(head)
        if why:
            print("labreport: SKIPPED %s — %s" % (label, why), file=sys.stderr)
            bad = True
        else:
            print("labreport: wrote data/%s (at %s)" % (label, head))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
