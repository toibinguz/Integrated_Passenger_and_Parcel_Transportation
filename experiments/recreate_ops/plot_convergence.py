#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Chạy và vẽ riêng từng ảnh đường cong hội tụ (Global Best Obj + Current Obj + SA Temperature T(t))
Tham khảo bố cục 2-panel của experiments/sisr_sa/plot_sisr_sa.py
"""
import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

MODES = [
    ("base", "0", "#1f77b4", "Base (SISR-SA)"),
    ("eject50", "2", "#2ca02c", "Eject50 (Insert-with-Evict)"),
    ("wave30", "1", "#ff7f0e", "Wave30 (30% Hungarian)"),
    ("wave100", "4", "#d62728", "Wave100 (100% Hungarian)"),
]

TC = "testcases/legacy_perturbation/test_850.txt"


def run_one(args):
    name, marg, _, _, time_sec, seed, step_sec = args
    p = subprocess.run(
        [
            "experiments/recreate_ops/sisr_recreate.exe",
            str(time_sec),
            TC,
            str(seed),
            "0",
            marg,
            str(step_sec),
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    m_res = re.search(r"RESULT:\s*obj=(-?\d+)\s+iters=(\d+)\s+speed=([\d.]+)", p.stderr)
    m_tr = re.search(r"TRACE:\s*(.+)", p.stderr)
    ts, bests, curs, temps = [], [], [], []
    if m_tr:
        for tok in m_tr.group(1).strip().split(","):
            t_s, b_s, c_s, tmp_s = tok.split(":")
            ts.append(float(t_s))
            bests.append(int(b_s))
            curs.append(int(c_s))
            temps.append(max(1e-6, float(tmp_s)))
    return {
        "mode": name,
        "obj": int(m_res.group(1)) if m_res else -1,
        "iters": int(m_res.group(2)) if m_res else 0,
        "speed": float(m_res.group(3)) if m_res else 0.0,
        "t": ts,
        "best": bests,
        "cur": curs,
        "temp": temps,
    }


def plot_single(r, base_r, name, col, title, time_sec, seed, out_png):
    fig, (ax1, ax2) = plt.subplots(
        2, 1, figsize=(10, 6.5), sharex=True, dpi=220,
        gridspec_kw={"height_ratios": [1.8, 1.0]}
    )

    cur_col = "#ff7f0e" if name == "base" else col
    ax1.plot(r["t"], r["cur"], color=cur_col, alpha=0.4, linewidth=0.8, label="Current Obj (cur_obj)")
    ax1.plot(r["t"], r["best"], color=col, linewidth=2.0, label=f"Global Best Obj (final = {r['obj']:,})")
    if name != "base":
        ax1.plot(base_r["t"], base_r["best"], color="#1f77b4", linestyle="--",
                 linewidth=1.2, alpha=0.75, label=f"Base Best ({base_r['obj']:,})")
    ax1.set_ylabel("Net Objective (Benefit)", fontsize=10, fontweight="bold")
    ax1.set_title(
        f"{title} — test_850, {int(time_sec)}s, Seed {seed} | {r['iters']:,} iters ({r['speed']:.0f}/s)",
        fontsize=11, fontweight="bold"
    )
    ax1.set_ylim(27000 if name != "wave100" else 25500, 31800)
    ax1.grid(True, linestyle="--", alpha=0.5)
    ax1.legend(loc="lower right", fontsize=9)

    ax2.plot(r["t"], r["temp"], color="#d62728", linewidth=1.4, label="SA Temperature T(t)")
    ax2.set_xlabel("Elapsed Wall-Clock Time (seconds)", fontsize=10, fontweight="bold")
    ax2.set_ylabel("Temperature", fontsize=10, fontweight="bold")
    ax2.set_yscale("log")
    ax2.grid(True, linestyle="--", alpha=0.5)
    ax2.legend(loc="upper right", fontsize=9)

    plt.tight_layout()
    plt.savefig(out_png)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--time", type=float, default=200.0)
    ap.add_argument("--seed", type=int, default=2028)
    ap.add_argument("--step", type=float, default=0.1)
    ap.add_argument("--out-dir", default="experiments/recreate_ops/plots_200s")
    ap.add_argument("--artifact-dir", default=None)
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    jobs = [(nm, marg, col, title, args.time, args.seed, args.step) for nm, marg, col, title in MODES]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
        results = list(ex.map(run_one, jobs))

    trace_json = os.path.join(args.out_dir, f"trace_850_{int(args.time)}s_s{args.seed}.json")
    with open(trace_json, "w", encoding="utf-8") as f:
        json.dump(results, f)

    by_mode = {r["mode"]: r for r in results}
    base_r = by_mode["base"]
    for name, _, col, title in MODES:
        r = by_mode[name]
        print(f"[{name}] final_obj={r['obj']} iters={r['iters']} ({r['speed']:.0f}/s) pts={len(r['t'])}")
        out_png = os.path.join(args.out_dir, f"conv_{name}_{int(args.time)}s.png")
        plot_single(r, base_r, name, col, title, args.time, args.seed, out_png)
        print(f"[+] Saved: {out_png}")
        if args.artifact_dir:
            os.makedirs(args.artifact_dir, exist_ok=True)
            dst = os.path.join(args.artifact_dir, f"conv_{name}_{int(args.time)}s.png")
            shutil.copy2(out_png, dst)
            print(f"[+] Copied: {dst}")


if __name__ == "__main__":
    main()
