#!/usr/bin/env python3
"""Time-balanced (interleaved) multi-mask benchmark.

Why: in a sequential sweep each mask occupies its own time block, so machine
drift (thermal throttling, background load) inflates "differences" between
masks — we measured it/s drifting by 20-40% *within* a single mask's own three
waves, and by up to 3x between first and last mask of a sweep.

Fix: for every seed, launch ALL masks at the same instant (one process each),
so mask-to-mask comparison is paired in time as well as in seed. Also records
the run folder reported by the binary so the mask label is authoritative
(never inferred from timestamps) and the permanent STRUCT telemetry from
report.txt can be joined exactly.

Usage:
    python interleaved_bench.py --binary B --testcase T --time 60 \
        --n-seeds 18 --seed-start 1 --masks 768 896 4864 4992 2944 7040 \
        --out experiments/gm_sisr/output/t850_inter
"""
import argparse, os, re, subprocess, sys, time, csv
import concurrent.futures as cf

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


def parse_report(path):
    try:
        t = open(path, encoding="utf-8", errors="ignore").read()
    except OSError:
        return {}
    def g(pat, cast=str, default=None):
        m = re.search(pat, t)
        return cast(m.group(1)) if m else default
    return {
        "obj_verified": g(r"Final Best Objective\s*:\s*(\d+)", int),
        "iters": g(r"Total Iterations\s*:\s*(\d+)", int, 0),
        "it_s": g(r"Total Iterations\s*:\s*\d+\s*\(([\d.]+)", float, 0.0),
        "acc_pct": g(r"Accepted Moves\s*:\s*\d+\s*\(([\d.]+)%", float, 0.0),
        "cycles": g(r"Total SA Cycles\s*:\s*(\d+)", int, 0),
        "noop_pct": g(r"STRUCT no-op \(delta==0\):\s*\d+\s*/\s*\d+\s*\(([\d.]+)%\)", float, 0.0),
        "downhill_pct": g(r"downhill:\s*([\d.]+)%", float, 0.0),
        "ruinempty_pct": g(r"STRUCT ruin-empty\s*:\s*\d+\s*\(([\d.]+)%\)", float, 0.0),
        "h1_ins": g(r"H1 Soft-Inserts\s*:\s*(\d+)", int, 0),
        "m1": g(r"Mode 1 \(String Ruin\)\s*:\s*Calls=(\d+)", int, 0),
        "m2": g(r"Mode 2 \(Unserved-Cav\)\s*:\s*Calls=(\d+)", int, 0),
        "m3": g(r"Mode 3 \(Bottleneck\)\s*:\s*Calls=(\d+)", int, 0),
    }


def run_one(binary, testcase, seed, time_sec, mask, cwd):
    cmd = [binary, str(time_sec), testcase, str(seed), "0", str(mask)]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       text=True, cwd=cwd)
    obj = None
    folder = None
    for line in r.stderr.splitlines():
        if "RESULT:" in line:
            m = re.search(r"obj=(\d+)", line)
            if m:
                obj = int(m.group(1))
        if "Initialized run folder:" in line:
            folder = line.split("Initialized run folder:", 1)[1].strip()
    if obj is None and r.stdout:
        try:
            obj = int(r.stdout.strip().splitlines()[0])
        except (ValueError, IndexError):
            obj = None
    row = {"seed": seed, "mask": mask, "obj": obj, "folder": folder}
    if folder:
        rep = os.path.join(folder, "report.txt")
        row.update(parse_report(rep))
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--testcase", required=True)
    ap.add_argument("--time", type=float, default=60.0)
    ap.add_argument("--n-seeds", type=int, default=18)
    ap.add_argument("--seed-start", type=int, default=1)
    ap.add_argument("--masks", type=int, nargs="+", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--cwd", default="Z:/Desktop/250926")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    rows = []
    t0 = time.time()
    for s in range(a.seed_start, a.seed_start + a.n_seeds):
        wave_t = time.time()
        with cf.ThreadPoolExecutor(max_workers=len(a.masks)) as ex:
            futs = {ex.submit(run_one, a.binary, a.testcase, s, a.time, m, a.cwd): m
                    for m in a.masks}
            got = []
            for f in cf.as_completed(futs):
                try:
                    got.append(f.result())
                except Exception as e:  # noqa: BLE001
                    got.append({"seed": s, "mask": futs[f], "obj": None, "err": str(e)})
        got.sort(key=lambda r: a.masks.index(r["mask"]))
        rows.extend(got)
        line = " ".join(f"{r['mask']}:{r.get('obj')}" for r in got)
        print(f"[seed {s:>3}] {line}  ({time.time()-wave_t:.1f}s)", flush=True)

    csv_path = os.path.join(a.out, "inter.csv")
    keys = ["seed", "mask", "obj", "obj_verified", "iters", "it_s", "acc_pct",
            "cycles", "noop_pct", "downhill_pct", "ruinempty_pct", "h1_ins",
            "m1", "m2", "m3", "folder"]
    with open(csv_path, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=keys, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow(r)
    print(f"\nwrote {csv_path}  ({len(rows)} rows, {time.time()-t0:.0f}s total)")


if __name__ == "__main__":
    main()
