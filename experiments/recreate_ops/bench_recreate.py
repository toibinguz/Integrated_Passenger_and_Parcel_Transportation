import argparse
import concurrent.futures
import csv
import math
import os
import re
import subprocess

MODES = [
    ("base",    "0"),
    ("wave30",  "1"),
    ("wave100", "4"),
    ("eject50", "2"),
    ("both",    "3"),
]

TESTS = [
    "testcases/benchmark/bench_01_tight_assignment.txt",
    "testcases/benchmark/bench_02_loose_sequencing.txt",
    "testcases/benchmark/bench_03_clustered_basins.txt",
    "testcases/benchmark/bench_04_capacity_bottleneck.txt",
    "testcases/benchmark/bench_06_asymmetric_metric.txt",
    "testcases/benchmark/bench_07_dense_scale_500.txt",
    "testcases/phase1_ladder/ladder_03_V350.txt",
    "testcases/legacy_perturbation/test_500.txt",
    "testcases/legacy_perturbation/test_850.txt",
]

RE_RES = re.compile(r"RESULT:\s*obj=(-?\d+)\s+iters=(\d+)\s+speed=([\d.]+)\s+time=([\d.]+)")

def run_one(args):
    name, mode_arg, tc, seed, time_sec, validate, out_dir = args
    cname = os.path.splitext(os.path.basename(tc))[0]
    cmd = ["experiments/recreate_ops/sisr_recreate.exe", str(time_sec), tc, str(seed), "0", mode_arg]
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    m = RE_RES.search(p.stderr)
    obj = int(m.group(1)) if m else -1
    iters = int(m.group(2)) if m else 0
    speed = float(m.group(3)) if m else 0.0
    val_ok = True
    if validate and p.returncode == 0:
        out_p = os.path.join(out_dir, f"{name}_{cname}_s{seed}.txt")
        with open(out_p, "w", encoding="utf-8") as f:
            f.write(p.stdout)
        v = subprocess.run(["python", "utility/validator.py", tc, out_p], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        val_ok = ("ĐIỂM SỐ KHỚP HOÀN TOÀN" in v.stdout)
        if val_ok and os.path.exists(out_p):
            os.remove(out_p)
    return {
        "testcase": cname,
        "seed": seed,
        "mode": name,
        "obj": obj,
        "iters": iters,
        "speed": speed,
        "exit": p.returncode,
        "val_ok": val_ok,
        "stderr": p.stderr.strip().replace("\n", " | "),
    }

def exact_sign_p(wins, losses):
    n = wins + losses
    if n == 0:
        return 1.0
    k = min(wins, losses)
    p_tail = sum(math.comb(n, i) for i in range(k + 1)) * (0.5 ** n)
    return min(1.0, 2.0 * p_tail)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--time", type=float, default=6.0)
    ap.add_argument("--seeds", type=int, default=6)
    ap.add_argument("--seed-start", type=int, default=None)
    ap.add_argument("--seed-end", type=int, default=None)
    ap.add_argument("--cases", nargs="*", default=None)
    ap.add_argument("--modes", nargs="*", default=None)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--validate", action="store_true")
    ap.add_argument("--csv", default="experiments/recreate_ops/paired_results.csv")
    args = ap.parse_args()

    out_dir = "experiments/recreate_ops/_tmp_val"
    os.makedirs(out_dir, exist_ok=True)
    tests = [t for t in TESTS if os.path.exists(t)]
    if args.cases:
        tests = [t for t in tests if any(c in t for c in args.cases)]
    active_modes = MODES
    if args.modes:
        active_modes = [m for m in MODES if m[0] in args.modes]
    mode_names = [m[0] for m in active_modes]

    if args.seed_start is not None or args.seed_end is not None:
        s_start = 1 if args.seed_start is None else args.seed_start
        s_end = s_start + args.seeds - 1 if args.seed_end is None else args.seed_end
        seed_list = list(range(s_start, s_end + 1))
    else:
        seed_list = list(range(1, args.seeds + 1))

    rows = []
    for seed in seed_list:
        for tc in tests:
            jobs = [(nm, marg, tc, seed, args.time, args.validate, out_dir) for nm, marg in active_modes]
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as ex:
                wave = list(ex.map(run_one, jobs))
            rows.extend(wave)
            cname = os.path.splitext(os.path.basename(tc))[0]
            msg = " | ".join(f"{r['mode']}={r['obj']}({r['speed']:.0f}/s,ok={r['val_ok']})" for r in wave)
            print(f"[s{seed} | {cname}] {msg}", flush=True)

    if os.path.isdir(out_dir) and not os.listdir(out_dir):
        os.rmdir(out_dir)

    with open(args.csv, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["testcase", "seed", "mode", "obj", "iters", "speed", "exit", "val_ok", "stderr"])
        w.writeheader()
        w.writerows(rows)

    by_key = {(r["testcase"], r["seed"], r["mode"]): r for r in rows}
    cnames = [os.path.splitext(os.path.basename(t))[0] for t in tests]

    print("\n=== PER-INSTANCE MEANS ===", flush=True)
    hdr = f"{'Instance':<28} | " + " | ".join(f"{m:>9}" for m in mode_names)
    print(hdr, flush=True)
    print("-" * len(hdr), flush=True)
    for cn in cnames:
        vals = [sum(by_key[(cn, sd, m)]["obj"] for sd in seed_list) / len(seed_list) for m in mode_names]
        print(f"{cn:<28} | " + " | ".join(f"{v:>9.1f}" for v in vals), flush=True)

    if "base" in mode_names:
        print("\n=== PAIRED COMPARISONS vs base (all instance-seed pairs) ===", flush=True)
        for m in mode_names:
            if m == "base":
                continue
            diffs = []
            wins = ties = losses = 0
            for cn in cnames:
                for sd in seed_list:
                    d = by_key[(cn, sd, m)]["obj"] - by_key[(cn, sd, "base")]["obj"]
                    diffs.append(d)
                    if d > 0: wins += 1
                    elif d < 0: losses += 1
                    else: ties += 1
            mean_d = sum(diffs) / len(diffs)
            sd_d = math.sqrt(sum((x - mean_d) ** 2 for x in diffs) / max(1, len(diffs) - 1))
            se_d = sd_d / math.sqrt(len(diffs))
            ci_lo, ci_hi = mean_d - 1.96 * se_d, mean_d + 1.96 * se_d
            pval = exact_sign_p(wins, losses)
            print(f"{m:>8} vs base: W/T/L={wins}/{ties}/{losses} mean_diff={mean_d:+.1f} 95%CI=[{ci_lo:+.1f}, {ci_hi:+.1f}] sign_p={pval:.4g}", flush=True)

if __name__ == "__main__":
    main()
