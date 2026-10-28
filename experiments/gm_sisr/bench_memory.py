#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bench_memory.py — Giao thức benchmark chuẩn cho GM-SISR (16 testcase x nhiều seed).

Ví dụ:
  py -3.14 experiments/gm_sisr/bench_memory.py --label base --flags ""
  py -3.14 experiments/gm_sisr/bench_memory.py --label tabu --flags tabu --compare base

Mỗi cấu hình ghi experiments/gm_sisr/bench/<label>.csv; chỉ số chính:
  gap% = (best - init) / |init| * 100  (cải thiện so với nghiệm tham lam ban đầu)
So sánh với --compare: chênh lệch tương đối của trung vị best theo từng instance,
kèm độ nhiễu giữa các seed để biết chênh lệch có vượt nhiễu hay không.
"""
import argparse, csv, glob, os, statistics, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXE = os.path.join(ROOT, "experiments", "gm_sisr", "gm_sisr.exe")
BENCH_DIR = os.path.join(ROOT, "experiments", "gm_sisr", "bench")

if sys.stdout.encoding != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def instances():
    files = sorted(glob.glob(os.path.join(ROOT, "testcases", "benchmark", "*.txt")))
    files += sorted(glob.glob(os.path.join(ROOT, "testcases", "legacy_perturbation", "*.txt")),
                    key=lambda p: int(os.path.basename(p).split("_")[1].split(".")[0]))
    return files


def run_one(args):
    path, seed, tlim, flags = args
    fl = ("quiet," + flags) if flags else "quiet"
    p = subprocess.run([EXE, str(tlim), path, str(seed), fl], capture_output=True, text=True, cwd=ROOT)
    rec = {"instance": os.path.splitext(os.path.basename(path))[0], "seed": seed}
    for line in p.stderr.splitlines():
        if line.startswith("SUMMARY"):
            for kv in line.split()[1:]:
                k, v = kv.split("=", 1)
                rec[k] = v
    if "best" not in rec:
        raise RuntimeError(f"Run failed: {path} seed={seed}\n{p.stderr[-500:]}")
    return rec


def load(label):
    with open(os.path.join(BENCH_DIR, f"{label}.csv"), newline="") as f:
        return list(csv.DictReader(f))


def by_instance(rows):
    d = {}
    for r in rows:
        d.setdefault(r["instance"], []).append(r)
    return d


def gap(r):
    i, b = int(r["init"]), int(r["best"])
    return (b - i) / max(1, abs(i)) * 100.0


def summarize(label, rows, base_rows=None):
    inst = by_instance(rows)
    base = by_instance(base_rows) if base_rows else None
    print(f"\n=== {label} ===")
    hdr = f"{'instance':32s} {'gap%med':>8s} {'gap%sd':>7s} {'ident%':>7s} {'kit/s':>7s}"
    if base:
        hdr += f" {'vsBase%':>8s} {'noise%':>7s}"
    print(hdr)
    gaps, diffs, wins, losses = [], [], 0, 0
    for name, rs in inst.items():
        g = [gap(r) for r in rs]
        ident = statistics.mean(int(r["identical"]) / max(1, int(r["iters"])) * 100 for r in rs)
        kips = statistics.mean(int(r["iters"]) / max(1e-9, float(r["time"])) / 1000 for r in rs)
        line = f"{name:32s} {statistics.median(g):8.2f} {statistics.pstdev(g):7.2f} {ident:7.1f} {kips:7.1f}"
        gaps.append(statistics.median(g))
        if base and name in base:
            mb = statistics.median(int(r["best"]) for r in base[name])
            mc = statistics.median(int(r["best"]) for r in rs)
            d = (mc - mb) / max(1, abs(mb)) * 100.0
            noise = statistics.pstdev([int(r["best"]) for r in base[name]]) / max(1, abs(mb)) * 100.0
            diffs.append(d)
            if d > noise: wins += 1
            elif d < -noise: losses += 1
            line += f" {d:+8.3f} {noise:7.3f}"
        print(line)
    print(f"-> Trung bình gap% (trung vị theo instance): {statistics.mean(gaps):.3f}")
    if base:
        print(f"-> So với base: chênh lệch TB {statistics.mean(diffs):+.3f}% | "
              f"thắng vượt nhiễu {wins} / thua vượt nhiễu {losses} / trong nhiễu {len(diffs) - wins - losses}")
    bad = [r for r in rows if r["best"] != r["verified"]]
    if bad:
        print(f"!! {len(bad)} run có best != verified")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", required=True)
    ap.add_argument("--flags", default="")
    ap.add_argument("--time", type=float, default=4.0)
    ap.add_argument("--seeds", type=int, default=5)
    ap.add_argument("--workers", type=int, default=6)
    ap.add_argument("--compare", default=None)
    ap.add_argument("--only-summary", action="store_true")
    a = ap.parse_args()
    os.makedirs(BENCH_DIR, exist_ok=True)

    if not a.only_summary:
        jobs = [(p, s, a.time, a.flags) for p in instances() for s in range(1, a.seeds + 1)]
        with ThreadPoolExecutor(a.workers) as ex:
            rows = list(ex.map(run_one, jobs))
        keys = sorted({k for r in rows for k in r}, key=lambda k: (k not in ("instance", "seed"), k))
        with open(os.path.join(BENCH_DIR, f"{a.label}.csv"), "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=keys)
            w.writeheader()
            w.writerows(rows)
    rows = load(a.label)
    summarize(a.label, rows, load(a.compare) if a.compare else None)


if __name__ == "__main__":
    main()

