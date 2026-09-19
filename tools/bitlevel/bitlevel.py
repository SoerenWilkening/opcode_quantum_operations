#!/usr/bin/env python3
"""tools/bitlevel/bitlevel.py — drive the one-opcode experiment.

For each spelling (opcode / bitlevel) and each clang -O level, compile
addchain.cq.c through CQ_lang's front end and pass, link it against libcqops,
run it in both modes, and tabulate: what clang left in the IR before the pass
(stage 1), what the pass emitted (stage 2, cqrt_* calls by symbol), the gate
mix, the pool's peak/live/stranded, the residue, the classical answer and the
wall time. No golden, no verdict: the numbers are the deliverable.

Toolchain resolution and `ar x` are l7_run.py's, copied on the same footing
that file copies l6_run.py's (a cross-directory import needs a sys.path line).
"""
import argparse, collections, json, os, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
RUNNER = os.path.join(HERE, "run.sh")
sys.path.insert(0, HERE)
import gen_fixture
GATE = re.compile(r"^(x|cx|ccx|ry|rz|mz)\(", re.M)
CALL = re.compile(r"call\s+[^@]*@((?:cqrt|cq_template)_[A-Za-z0-9_]+)\(")
INST = re.compile(r"^\s*(?:%[\w.]+\s*=\s*)?(\w+)(?:\s+(?:nuw\s+|nsw\s+)*(i\d+|float|double))?", re.M)


def toolchain(cqdir, builddir):
    r = subprocess.run(["ctest", "--test-dir", builddir, "--show-only=json-v1"],
                       cwd=cqdir, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("ctest --show-only failed:\n" + r.stderr)
    for t in json.loads(r.stdout)["tests"]:
        c = t["command"]
        if len(c) > 1 and c[1].endswith("run_slice.sh"):
            return dict(clang=c[2], opt=c[3], pas=c[4], inc=c[5])
    sys.exit("no single-file e2e slice registered in " + builddir)


def extract_intrinsics(cqdir, builddir, out):
    src = os.path.join(cqdir, builddir, "runtime", "libcq_templates.a")
    if not os.path.exists(src):
        sys.exit("no cq_templates archive at " + src)
    d = os.path.join(os.path.abspath(out), "_intrinsics")
    if os.path.exists(d):
        shutil.rmtree(d)
    os.makedirs(d)
    # EMPTY SINCE PRD-v2 §6.1's VENDORING (bead 9ve.24): libcqops now defines
    # all 2,880 `cq_template_*` symbols, so extracting CQ_lang's two stub
    # objects and putting them on the link line is a duplicate-symbol hard
    # error. The list is kept rather than deleted so the `ar x` plumbing and
    # its "extracted fresh on every run" discipline survive for the next
    # member that needs it.
    members = []
    r = subprocess.run(["ar", "x", src] + members, cwd=d, capture_output=True,
                       text=True)
    if r.returncode != 0:
        sys.exit("ar x failed: " + r.stderr)
    for m in members:
        if not os.path.exists(os.path.join(d, m)):
            sys.exit("ar x did not produce " + m)
    return d


def read(p):
    try:
        with open(p, "r", errors="replace") as fh:
            return fh.read()
    except OSError:
        return ""


def kv(text):
    out = {}
    for line in text.splitlines():
        k, _, v = line.partition(" ")
        if v.strip().lstrip("-").isdigit():
            out[k] = int(v)
    return out


def ir_profile(text, keep):
    """Instruction mnemonics (with integer type where present) in `main` only."""
    body = text.split("define ", 1)[1] if "define " in text else text
    body = body.split("\n}", 1)[0]
    c = collections.Counter()
    for m in INST.finditer(body):
        op, ty = m.group(1), m.group(2)
        if op in keep:
            c[op + (" " + ty if ty else "")] += 1
    return dict(c)


def timed_run(binary):
    t0 = time.perf_counter()
    subprocess.run([binary], env=dict(os.environ, CQOPS_SINK="printf"),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return time.perf_counter() - t0


def run_one(tc, objdir, archive, out, spelling, olevel, chain, w):
    stem = os.path.join(out, f"{spelling}-{chain}-W{w}-O{olevel}")
    src = os.path.join(out, f"addchain_W{w}.cq.c")
    with open(src, "w") as fh:
        fh.write(gen_fixture.emit(w))
    for f in os.listdir(out):
        if f.startswith(os.path.basename(stem) + "."):
            os.remove(os.path.join(out, f))
    defs = ("-DBITLEVEL=" + ("1" if spelling == "bitlevel" else "0") +
            " -DCHAIN=" + ("1" if chain == "repeat" else "0"))
    r = subprocess.run(["bash", RUNNER, tc["clang"], tc["opt"], tc["pas"],
                        tc["inc"], src, str(olevel), defs, archive, objdir,
                        stem], capture_output=True, text=True)
    row = dict(spelling=spelling, chain=chain, W=w, O=olevel, rc=r.returncode,
               expect=gen_fixture.expect(w))
    if not os.path.exists(stem + ".q.rc"):
        why = (read(stem + ".pass.err") + read(stem + ".link") + r.stderr)
        row["fail"] = "DID NOT BUILD: " + (why.strip().splitlines() or ["?"])[0]
        return row
    s1, s2 = read(stem + ".s1.ll"), read(stem + ".s2.ll")
    row["s1"] = ir_profile(s1, {"add", "sub", "mul", "shl", "lshr", "and", "or",
                                "xor", "select", "zext", "trunc", "icmp", "phi"})
    calls = collections.Counter(CALL.findall(s2))
    row["s2_calls"] = sum(calls.values())
    row["s2_templates"] = sum(n for k, n in calls.items() if k.startswith("cq_t"))
    row["s2_top"] = calls.most_common(14)
    q = read(stem + ".q.out")
    row["gates"] = dict(collections.Counter(GATE.findall(q)))
    row["q_rc"] = int(read(stem + ".q.rc").strip() or -1)
    row["q_secs"] = min(timed_run(stem + ".bin") for _ in range(5))
    row["compile"] = {k: float(v) for k, _, v in
                      (l.partition(" ") for l in read(stem + ".times").splitlines())}
    row["s2_lines"] = s2.count("\n")
    row["q_pool"] = kv(read(stem + ".q.pool"))
    row["q_residue"] = kv(read(stem + ".q.residue"))
    row["q_err"] = read(stem + ".q.err").strip().splitlines()[:3]
    c = read(stem + ".c.out")
    m = re.search(r"addchain -> (\d+)", c)
    row["c_value"] = int(m.group(1)) if m else None
    row["c_gates"] = len(GATE.findall(c))
    row["c_rc"] = int(read(stem + ".c.rc").strip() or -1)
    row["c_pool"] = kv(read(stem + ".c.pool"))
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cqlang", required=True)
    ap.add_argument("--cqbuild", default="build")
    ap.add_argument("--archive", default="build-release/libcqops.a")
    ap.add_argument("--out", required=True)
    ap.add_argument("--olevels", default="0,1,2")
    ap.add_argument("--chains", default="distinct,repeat")
    ap.add_argument("--widths", default="8")
    ap.add_argument("--json", default="")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    archive = os.path.abspath(a.archive)
    tc = toolchain(a.cqlang, a.cqbuild)
    objdir = extract_intrinsics(a.cqlang, a.cqbuild, a.out)
    rows = []
    for w in [int(x) for x in a.widths.split(",")]:
        for chain in a.chains.split(","):
            for o in [int(x) for x in a.olevels.split(",")]:
                for sp in ("opcode", "bitlevel"):
                    rows.append(run_one(tc, objdir, archive, a.out, sp, o, chain, w))
    print("one-opcode experiment")
    hdr = ("spelling", "chain", "W", "O", "ABI calls", "x", "cx", "ccx",
           "peak", "live@exit", "stranded", "unproven", "classical",
           "run s", "clang s", "pass s", "cc s", "IR lines")
    print(" | ".join(hdr))
    for r in rows:
        if "fail" in r:
            print(f"{r['spelling']} | {r['chain']} | W{r['W']} | {r['O']} | {r['fail']}")
            continue
        g, p, s = r["gates"], r["q_pool"], r["q_residue"]
        c = r["compile"]
        print(" | ".join(str(v) for v in (
            r["spelling"], r["chain"], r["W"], r["O"], r["s2_calls"],
            g.get("x", 0), g.get("cx", 0), g.get("ccx", 0), p.get("peak"),
            p.get("live"), p.get("stranded"), s.get("stranded_unproven"),
            f"{r['c_value']} ({'ok' if r['c_value'] == r['expect'] else 'WRONG'},"
            f" {r['c_gates']} gates)", f"{r['q_secs']:.3f}",
            f"{c.get('clang', 0):.2f}", f"{c.get('pass', 0):.2f}",
            f"{c.get('cc', 0):.2f}", r["s2_lines"])))
    for r in rows:
        if "fail" in r:
            continue
        print(f"\n[{r['spelling']} {r['chain']} W{r['W']} -O{r['O']}] stage-1 IR in main: {r['s1']}")
        print(f"  stage-2 calls by symbol: {r['s2_top']}")
        if r["q_err"]:
            print("  quantum stderr:", r["q_err"])
    if a.json:
        with open(a.json, "w") as fh:
            json.dump(rows, fh, indent=1)
    return 0 if all("fail" not in r for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
