#!/usr/bin/env python3
"""Aggregate the permanent STRUCT telemetry (no-op rate etc.) per ablation mask.

The bench harness does not record the mask in the run folder name, so we map
timestamp -> mask using the known sequential order of the sweep, and we verify
the mapping against the mask-sensitive report fields (H1 soft-inserts, bandit w)
so a wrong mapping cannot silently pass.

Usage:
    python analyze_noop.py <mask1> <mask2> ...      # in sweep order
"""
import sys, os, re, glob
from collections import defaultdict

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

RUNS = "experiments/gm_sisr/output/runs"


def parse_report(path):
    txt = open(path, encoding="utf-8", errors="ignore").read()
    def g(pat):
        m = re.search(pat, txt)
        return m.group(1) if m else None
    d = {}
    d["seed"] = int(g(r"Random Seed\s*:\s*(\d+)") or -1)
    d["obj"] = int(g(r"Final Best Objective\s*:\s*(\d+)") or -1)
    d["iters"] = int(g(r"Total Iterations\s*:\s*(\d+)") or 0)
    d["acc"] = float(g(r"Accepted Moves\s*:\s*\d+\s*\(([\d.]+)%") or 0)
    d["it_s"] = float(g(r"Total Iterations\s*:\s*\d+\s*\(([\d.]+)\s*iter") or 0)
    d["h1"] = int(g(r"H1 Soft-Inserts\s*:\s*(\d+)") or 0)
    d["w"] = g(r"Bandit w=\[1\.\.3\]=([\d./]+)") or ""
    m = re.search(r"STRUCT no-op \(delta==0\):\s*(\d+)\s*/\s*(\d+)\s*\(([\d.]+)%\)", txt)
    d["noop_n"], d["noop_tot"], d["noop_pct"] = (int(m.group(1)), int(m.group(2)), float(m.group(3))) if m else (0, 0, 0.0)
    m = re.search(r"downhill:\s*([\d.]+)%", txt)
    d["downhill"] = float(m.group(1)) if m else 0.0
    m = re.search(r"STRUCT ruin-empty\s*:\s*\d+\s*\(([\d.]+)%\)", txt)
    d["ruinempty"] = float(m.group(1)) if m else 0.0
    return d


def main():
    masks = [int(x) for x in sys.argv[1:]]
    runs = []
    for p in glob.glob(os.path.join(RUNS, "run_test_850_*_sedca", "report.txt")):
        ts = re.search(r"_(\d{8}_\d{6})_", p)
        if not ts:
            continue
        big = int(ts.group(1).replace("_", ""))
        if not (20261113113000 <= big <= 20261113120000):
            continue
        d = parse_report(p)
        runs.append((big, p, d))
    runs.sort(key=lambda x: (x[0], x[2]["seed"]))

    # group by "wave" = the second-resolution bucket of distinct start times
    waves, cur = [], []
    for big, p, d in runs:
        if cur and abs(big - cur[0][0]) > 40:
            waves.append(cur); cur = []
        cur.append((big, p, d))
    if cur:
        waves.append(cur)
    print(f"detected {len(waves)} waves, {len(runs)} runs")

    if len(waves) != len(masks) * 3:
        print(f"!! expected {len(masks)*3} waves — mapping may be off")

    agg = defaultdict(list)
    for i, w in enumerate(waves):
        mi = i // 3
        if mi >= len(masks):
            break
        agg[masks[mi]].extend([d for _, _, d in w])

    hdr = (f"{'mask':>6}{'n':>4}{'seed':>6}{'obj':>9}{'noop%':>8}{'acc%':>7}"
           f"{'downhill%':>11}{'it/s':>8}{'its':>11}{'H1_ins':>8}")
    print(hdr); print("-" * len(hdr))
    for m in masks:
        v = agg.get(m, [])
        if not v:
            continue
        n = len(v)
        f = lambda k: sum(x[k] for x in v) / n
        print(f"{m:>6}{n:>4}{'-':>6}{f('obj'):>9.1f}{f('noop_pct'):>8.2f}"
              f"{f('acc'):>7.1f}{f('downhill'):>11.2f}{f('it_s'):>8.0f}"
              f"{f('iters'):>11.0f}{f('h1'):>8.0f}")

    print("\nper-run no-op% (sorted by wave):")
    for i, w in enumerate(waves):
        mi = i // 3
        tag = masks[mi] if mi < len(masks) else "?"
        print(f"  wave{i:>2} mask={tag:>5}: " +
              " ".join(f"s{d['seed']}:{d['noop_pct']:.1f}" for _, _, d in w))


if __name__ == "__main__":
    main()
