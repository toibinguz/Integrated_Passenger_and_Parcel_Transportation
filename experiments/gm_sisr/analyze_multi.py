#!/usr/bin/env python3
"""Aggregate multi-instance Phase B results against internal BKS.

Usage: python analyze_multi.py <out_dir> <maskA> <maskB>
Reads  <out_dir>/bks_*.txt  and  <out_dir>/champ_<name>_m<mask>.log
"""
import sys, os, re, glob, math

def parse_bks(out_dir):
    bks = {}
    for f in glob.glob(os.path.join(out_dir, "bks_*.txt")):
        for line in open(f, encoding="utf-8", errors="ignore"):
            m = re.match(r"BKS\s+(\S+)\s+(\d+)", line.strip())
            if m:
                bks[m.group(1)] = int(m.group(2))
    return bks

def parse_champ(out_dir, mask):
    res = {}
    for f in glob.glob(os.path.join(out_dir, f"champ_*_m{mask}.log")):
        name = re.match(r"champ_(.+)_m\d+\.log", os.path.basename(f)).group(1)
        objs = [int(m.group(1)) for m in
                (re.search(r"Obj:\s*(\d+)", l) for l in
                 open(f, encoding="utf-8", errors="ignore")) if m]
        if objs:
            res[name] = objs
    return res

def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "experiments/gm_sisr/output/multi"
    mA = int(sys.argv[2]) if len(sys.argv) > 2 else 768
    mB = int(sys.argv[3]) if len(sys.argv) > 3 else 896
    bks = parse_bks(out_dir)
    A = parse_champ(out_dir, mA)
    B = parse_champ(out_dir, mB)

    hdr = f"{'instance':<32}{'BKS':>8}{'mA_mean':>10}{'mA_gap%':>9}{'mB_mean':>10}{'mB_gap%':>9}{'winner':>8}"
    print(hdr); print("-" * len(hdr))
    wins_a = wins_b = ties = 0
    for name in sorted(bks):
        if name not in A or name not in B:
            print(f"{name:<32}{bks[name]:>8}   (missing runs)"); continue
        a, b = A[name], B[name]
        ma, mb = sum(a) / len(a), sum(b) / len(b)
        ga = 100.0 * (bks[name] - ma) / bks[name]
        gb = 100.0 * (bks[name] - mb) / bks[name]
        if ma > mb: w = f"A+{ma-mb:.0f}"; wins_a += 1
        elif mb > ma: w = f"B+{mb-ma:.0f}"; wins_b += 1
        else: w = "tie"; ties += 1
        print(f"{name:<32}{bks[name]:>8}{ma:>10.1f}{ga:>9.2f}{mb:>10.1f}{gb:>9.2f}{w:>8}")
    print("-" * len(hdr))
    print(f"WIN-RATE  mask{mA}: {wins_a}/{wins_a+wins_b+ties}   mask{mB}: {wins_b}/{wins_a+wins_b+ties}   ties: {ties}")
    print(f"(A + = mask {mA} better on that instance)")

if __name__ == "__main__":
    main()
