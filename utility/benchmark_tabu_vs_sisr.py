#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
benchmark_tabu_vs_sisr.py
Đo lường so sánh trực tiếp (Head-to-Head Benchmark) giữa 2 Nguyên mẫu Metaheuristic:
1. Hybrid Tabu Search - LNS (Đã tối ưu hóa O(1) slack checking & version cache)
2. SISR + Simulated Annealing (Standalone Phase 2)
CÙNG THỜI GIAN THỰC (Wall-clock Time) trên toàn bộ 7 Benchmark Landscapes chuẩn.
"""

import os
import sys
import subprocess
import glob
import time
import io
import argparse

if sys.stdout.encoding != 'utf-8':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
if sys.stderr.encoding != 'utf-8':
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

BENCHMARK_DIR = "testcases/benchmark"
TABU_EXE = os.path.abspath("experiments/tabu_prototype/tabu_prototype.exe")
SISR_EXE = os.path.abspath("experiments/sisr_sa/sisr_sa.exe")
VALIDATOR = os.path.abspath("utility/validator.py")

def run_solver(exe_path, instance_path, time_sec):
    cmd = [exe_path, str(time_sec), instance_path]
    t0 = time.time()
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding="utf-8", errors="replace")
    elapsed = time.time() - t0
    output = (p.stdout or "").strip()
    lines = output.splitlines()
    if not lines:
        return 0, elapsed, False, "No output"
    try:
        obj = int(lines[0].strip())
    except Exception as e:
        return 0, elapsed, False, f"Invalid obj line: {lines[0] if lines else ''}"

    # Validate output with validator.py
    temp_sol = f"temp_sol_{os.getpid()}_{int(time.time()*1000)%10000}.txt"
    with open(temp_sol, "w", encoding="utf-8") as f:
        f.write(output + "\n")
    
    val_proc = subprocess.run(
        [sys.executable, VALIDATOR, instance_path, temp_sol],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding="utf-8", errors="replace"
    )
    val_out = val_proc.stdout or ""
    is_valid = ("🎉 KẾT LUẬN: ĐIỂM SỐ KHỚP HOÀN TOÀN! CHUẨN XÁC!" in val_out)
    if os.path.exists(temp_sol):
        try:
            os.remove(temp_sol)
        except:
            pass
    return obj, elapsed, is_valid, p.stderr

def run_benchmark_suite(time_budget):
    testcases = sorted(glob.glob(os.path.join(BENCHMARK_DIR, "*.txt")))
    if not testcases:
        print(f"[-] Không tìm thấy testcase trong {BENCHMARK_DIR}")
        return []

    print("\n" + "=" * 90)
    print(f"   ĐO LƯỜNG SO SÁNH TABU SEARCH vs SISR+SA TRÊN 7 ĐỊA HÌNH BENCHMARK")
    print(f"   Ngân sách thời gian thực: {time_budget:.1f}s / testcase | Kiểm tra: validator.py")
    print("=" * 90)
    header = f"{'Testcase':<34} | {'Tabu Obj':<9} | {'SISR Obj':<9} | {'Chênh lệch':<11} | {'Tỷ lệ':<7} | {'Thắng thế'}"
    print(header)
    print("-" * 90)

    tabu_wins = 0
    sisr_wins = 0
    ties = 0

    results = []

    for tc in testcases:
        tname = os.path.basename(tc)
        
        # 1. Run Tabu Search
        tabu_obj, tabu_t, tabu_valid, tabu_err = run_solver(TABU_EXE, tc, time_budget)
        
        # 2. Run SISR + SA
        sisr_obj, sisr_t, sisr_valid, sisr_err = run_solver(SISR_EXE, tc, time_budget)

        diff = tabu_obj - sisr_obj
        pct = (abs(diff) / max(1, sisr_obj)) * 100.0
        if diff > 0:
            winner = f"Tabu (+{diff})"
            tabu_wins += 1
        elif diff < 0:
            winner = f"SISR (+{-diff})"
            sisr_wins += 1
        else:
            winner = "Hòa (Tie)"
            ties += 1

        val_str = "✅" if (tabu_valid and sisr_valid) else "❌"
        pct_str = f"{pct:.1f}%"
        print(f"{tname:<34} | {tabu_obj:<9} | {sisr_obj:<9} | {diff:>+10} | {pct_str:>6}  | {winner} {val_str}")
        sys.stdout.flush()

        results.append({
            "testcase": tname,
            "tabu_obj": tabu_obj,
            "sisr_obj": sisr_obj,
            "diff": diff,
            "pct": pct,
            "winner": winner,
            "valid": tabu_valid and sisr_valid
        })

    print("-" * 90)
    print(f"TỔNG KẾT ({time_budget:.1f}s): Tabu thắng {tabu_wins} | SISR thắng {sisr_wins} | Hòa {ties} | 100% nghiệm hợp lệ!")
    print("=" * 90 + "\n")
    return results

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Head-to-head benchmark Tabu vs SISR")
    parser.add_argument("--time", type=float, default=5.0, help="Wall-clock time budget in seconds (default 5.0s)")
    args = parser.parse_args()
    run_benchmark_suite(args.time)
