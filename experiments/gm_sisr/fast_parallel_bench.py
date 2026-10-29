#!/usr/bin/env python3
import sys
import os
import subprocess
import argparse
import time
import re
import math
import random
from concurrent.futures import ProcessPoolExecutor, as_completed

def run_single_seed(binary, testcase, seed, iters, mask, time_sec=60.0):
    cmd = [binary, str(time_sec), testcase, str(seed), str(iters), str(mask)]
    t0 = time.time()
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    wall_t = time.time() - t0
    
    obj = None
    iters_done = iters
    speed = 0.0
    for line in res.stderr.splitlines():
        if "RESULT:" in line:
            m_obj = re.search(r'obj=(\d+)', line)
            m_it = re.search(r'iters=(\d+)', line)
            m_sp = re.search(r'speed=([\d\.]+)', line)
            if m_obj: obj = int(m_obj.group(1))
            if m_it: iters_done = int(m_it.group(1))
            if m_sp: speed = float(m_sp.group(1))
            break
            
    if obj is None and res.stdout:
        first_line = res.stdout.strip().splitlines()[0]
        try:
            obj = int(first_line)
        except ValueError:
            obj = 0
            
    return {
        "seed": seed,
        "obj": obj if obj is not None else 0,
        "iters": iters_done,
        "speed": speed,
        "wall_time": wall_t
    }

def percentile(data, p):
    # p is in [0, 100]
    sorted_d = sorted(data)
    if not sorted_d: return 0.0
    k = (len(sorted_d) - 1) * (p / 100.0)
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return float(sorted_d[int(k)])
    d0 = sorted_d[int(f)] * (c - k)
    d1 = sorted_d[int(c)] * (k - f)
    return float(d0 + d1)

def bootstrap_ci(data, n_boot=5000, ci=0.95):
    n = len(data)
    boot_means = []
    for _ in range(n_boot):
        sample = random.choices(data, k=n)
        boot_means.append(sum(sample) / n)
    boot_means.sort()
    alpha = (1.0 - ci) / 2.0
    low_idx = int(alpha * n_boot)
    high_idx = int((1.0 - alpha) * n_boot)
    return boot_means[low_idx], boot_means[high_idx]

def main():
    parser = argparse.ArgumentParser(description="High-Throughput Multi-Core Benchmark Harness")
    parser.add_argument("--binary", default="experiments/gm_sisr/gm_sisr_clean.exe", help="Path to compiled solver binary")
    parser.add_argument("--testcase", default="testcases/legacy_perturbation/test_850.txt", help="Testcase path")
    parser.add_argument("--time", type=float, default=60.0, help="Wall-clock time limit in seconds (default 60.0)")
    parser.add_argument("--iters", type=int, default=0, help="Iteration budget (0 = use time)")
    parser.add_argument("--n-seeds", type=int, default=30, help="Number of seeds to run")
    parser.add_argument("--seed-start", type=int, default=1, help="Starting seed value")
    parser.add_argument("--workers", type=int, default=5, help="Number of parallel worker processes (optimal: 5 on 4P+4E CPU, 104k it/s)")
    parser.add_argument("--mask", type=int, default=0, help="Ablation mask (0=full base, 1=no regret-recreate, 2=no ruin-port, 4=no tourney, 8=no soft-reheat)")
    args = parser.parse_args()

    seeds = list(range(args.seed_start, args.seed_start + args.n_seeds))
    print(f"================================================================================")
    print(f"PARALLEL BENCHMARK: {args.binary}")
    print(f"Config: Time={args.time}s | Iters={args.iters:,} | Seeds={args.n_seeds} (start={args.seed_start}) | Workers={args.workers}")
    print(f"Testcase: {args.testcase}")
    print(f"================================================================================")
    sys.stdout.flush()

    t_start = time.time()
    results = []

    with ProcessPoolExecutor(max_workers=args.workers) as executor:
        future_map = {
            executor.submit(run_single_seed, args.binary, args.testcase, s, args.iters, args.mask, args.time): s 
            for s in seeds
        }
        for future in as_completed(future_map):
            res = future.result()
            results.append(res)
            print(f"  [Seed {res['seed']:3d}] Obj: {res['obj']:6d} | Speed: {res['speed']:6.0f} it/s | Time: {res['wall_time']:5.1f}s")
            sys.stdout.flush()

    total_wall = time.time() - t_start
    objs = [r["obj"] for r in results]
    n = len(objs)
    
    mean_val = sum(objs) / n
    variance = sum((x - mean_val) ** 2 for x in objs) / max(1, n - 1)
    std_val = math.sqrt(variance)
    median_val = percentile(objs, 50)
    min_val = min(objs)
    max_val = max(objs)
    p10_val = percentile(objs, 10)
    p90_val = percentile(objs, 90)
    ci_low, ci_high = bootstrap_ci(objs)

    print(f"\n============================= STATISTICAL SUMMARY =============================")
    print(f"  * Total Runs        : {n} seeds completed in {total_wall:.1f}s real time")
    print(f"  * Mean Objective    : {mean_val:.2f} (Std: {std_val:.2f})")
    print(f"  * 95% Bootstrap CI  : [{ci_low:.2f}, {ci_high:.2f}] (Margin: ±{(ci_high-ci_low)/2:.2f})")
    print(f"  * Median            : {median_val:.1f}")
    print(f"  * Range (Min - Max) : {min_val} - {max_val}")
    print(f"  * 10th - 90th Pct   : {p10_val:.1f} - {p90_val:.1f}")
    print(f"================================================================================")

if __name__ == "__main__":
    main()
