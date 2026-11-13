#!/usr/bin/env python3
"""Paired per-seed comparison of ablation masks on ONE instance.

All runs must share the same seed set (seed-start 1, n-seeds N) so that the
comparison is PAIRED: for each seed we subtract the two objectives, which
cancels the seed-specific difficulty and removes the "seed-set drift" artefact
that destroyed several of our earlier conclusions.

Usage:
    python analyze_paired.py <dir_with_mask_logs> <ref_mask> [mask2 mask3 ...]

Log lines parsed:   [Seed   3] Obj:  30400 | Speed: ...
"""
import sys, os, re, glob
from math import comb

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


def parse_log(path):
    obs = {}
    for line in open(path, encoding="utf-8", errors="ignore"):
        m = re.search(r"\[Seed\s+(\d+)\]\s+Obj:\s*(\d+)", line)
        if m:
            obs[int(m.group(1))] = int(m.group(2))
    return obs


def sign_test(pos, neg):
    """Two-sided exact binomial test on #wins vs #losses (ties excluded)."""
    n = pos + neg
    if n == 0:
        return 1.0
    k = min(pos, neg)
    p = 2.0 * sum(comb(n, i) for i in range(0, k + 1)) / (2.0 ** n)
    return min(1.0, p)


def mean(xs):
    return sum(xs) / len(xs) if xs else 0.0


def main():
    d = sys.argv[1]
    ref = int(sys.argv[2])
    masks = [int(x) for x in sys.argv[3:]]
    if not masks:
        masks = [int(re.search(r"mask_(\d+)", os.path.basename(f)).group(1))
                 for f in glob.glob(os.path.join(d, "mask_*.log"))]

    data, speeds = {}, {}
    for m in ([ref] + masks):
        f = os.path.join(d, f"mask_{m}.log")
        if not os.path.exists(f):
            print(f"!! missing {f}")
            continue
        data[m] = parse_log(f)
        sp = []
        for line in open(f, encoding="utf-8", errors="ignore"):
            mm = re.search(r"Speed:\s*(\d+)", line)
            if mm:
                sp.append(int(mm.group(1)))
        speeds[m] = mean(sp)

    seeds = sorted(set(data.get(ref, {})) )
    hdr = (f"{'mask':>6}{'n':>4}{'mean':>10}{'median':>9}{'min':>8}{'max':>8}"
           f"{'it/s':>8}{'gap%opt':>9}")
    print(f"REFERENCE = mask {ref}   (optimum test_850 = 32012)")
    print(hdr)
    print("-" * len(hdr))
    OPT = 32012
    for m in [ref] + masks:
        if m not in data:
            continue
        v = [data[m][s] for s in seeds if s in data[m]]
        med = sorted(v)[len(v) // 2] if v else 0
        gap = 100.0 * (OPT - mean(v)) / OPT
        print(f"{m:>6}{len(v):>4}{mean(v):>10.1f}{med:>9}{min(v):>8}{max(v):>8}"
              f"{speeds.get(m,0):>8.0f}{gap:>9.2f}")

    if masks:
        hdr2 = (f"{'mask':>6}{'pairedD':>10}{'SE':>8}{'wins':>6}{'loss':>6}"
                f"{'tie':>5}{'sign p':>9}")
        print("\nPAIRED vs mask " + str(ref) + "   (positive = BETTER than reference)")
        print(hdr2)
        print("-" * len(hdr2))
        for m in masks:
            if m not in data or ref not in data:
                continue
            diffs = [data[m][s] - data[ref][s] for s in seeds
                     if s in data[m] and s in data[ref]]
            pos = sum(1 for x in diffs if x > 0)
            neg = sum(1 for x in diffs if x < 0)
            tie = len(diffs) - pos - neg
            mu = mean(diffs)
            sd = (sum((x - mu) ** 2 for x in diffs) / max(1, len(diffs) - 1)) ** 0.5
            se = sd / (len(diffs) ** 0.5) if len(diffs) > 1 else 0.0
            print(f"{m:>6}{mu:>10.1f}{se:>8.1f}{pos:>6}{neg:>6}{tie:>5}"
                  f"{sign_test(pos, neg):>9.4f}")


if __name__ == "__main__":
    main()
