#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
plot_phase1_sp.py
Script trực quan hóa chuyên biệt cho nguyên mẫu: Phase 1 (DFS Column Enumeration + Set Packing)
Tuân thủ quy chuẩn AGENT.md: Vẽ đồ thị phân tích DFS, hội tụ cận trên Lagrangian và Gap tối ưu.
"""

import os
import sys
import io
import re
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

# Thiết lập UTF-8 cho console Windows
if sys.stdout.encoding != 'utf-8':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
if sys.stderr.encoding != 'utf-8':
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

def parse_and_visualize(run_dir, testcase_file=None):
    run_dir = os.path.abspath(run_dir)
    print(f"[i] Đang xử lý trực quan hóa Phase 1 Set Packing: {run_dir}")
    
    plots_dir = os.path.join(run_dir, "plots")
    os.makedirs(plots_dir, exist_ok=True)
    
    log_file = os.path.join(run_dir, "log.txt")
    report_file = os.path.join(run_dir, "report.txt")
    result_file = os.path.join(run_dir, "result.txt")

    # 1. Đọc dữ liệu từ log.txt
    taxi_cols = []
    lagr_steps = []
    lagr_bounds = []
    lagr_lbs = []
    sa_steps = []
    sa_curs = []
    sa_bests = []
    
    if os.path.exists(log_file):
        with open(log_file, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                line = line.strip()
                if line.startswith("ENUM_TAXI:"):
                    m = re.search(r"taxi=(\d+)\s+cols_added=(\d+)\s+total_states=(\d+)", line)
                    if m:
                        taxi_cols.append((int(m.group(1)), int(m.group(2))))
                elif line.startswith("LAGR_STEP:"):
                    m = re.search(r"it=(\d+)\s+bound=([\d\.]+)\s+best_lb=(-?\d+)", line)
                    if m:
                        lagr_steps.append(int(m.group(1)))
                        lagr_bounds.append(float(m.group(2)))
                        lagr_lbs.append(int(m.group(3)))
                elif line.startswith("SA_STEP:"):
                    m = re.search(r"it=(\d+)\s+cur=(-?\d+)\s+best=(-?\d+)", line)
                    if m:
                        sa_steps.append(int(m.group(1)))
                        sa_curs.append(int(m.group(2)))
                        sa_bests.append(int(m.group(3)))

    # 2. Đọc kết quả report.txt
    rep_data = {"enum_states": 0, "unique_cols": 0, "greedy_obj": 0, "ub": 0, "best": 0, "proven": False}
    if os.path.exists(report_file):
        with open(report_file, "r", encoding="utf-8", errors="ignore") as f:
            text = f.read()
            m_st = re.search(r"DFS Enum States Explored\s*:\s*(\d+)", text)
            if m_st: rep_data["enum_states"] = int(m_st.group(1))
            m_col = re.search(r"Unique Columns Generated\s*:\s*(\d+)", text)
            if m_col: rep_data["unique_cols"] = int(m_col.group(1))
            m_gr = re.search(r"Greedy Primal Obj\s*:\s*(-?\d+)", text)
            if m_gr: rep_data["greedy_obj"] = int(m_gr.group(1))
            m_ub = re.search(r"Lagrangian Upper Bound UB\s*:\s*(-?\d+)", text)
            if m_ub: rep_data["ub"] = int(m_ub.group(1))
            m_best = re.search(r"Final Best Primal LB\s*:\s*(-?\d+)", text)
            if m_best: rep_data["best"] = int(m_best.group(1))
            m_prov = re.search(r"Proven Globally Optimal\s*:\s*(YES|NO)", text)
            if m_prov: rep_data["proven"] = (m_prov.group(1) == "YES")

    # -------------------------------------------------------------------------
    # ĐỒ THỊ 1: PHÂN BỐ CỘT LỘ TRÌNH THEO XE TAXI
    # -------------------------------------------------------------------------
    if taxi_cols:
        plt.figure(figsize=(10, 5), dpi=300)
        t_nums = [t[0] for t in taxi_cols]
        t_counts = [t[1] for t in taxi_cols]
        plt.bar(t_nums, t_counts, color='#2ca02c', edgecolor='black', linewidth=0.6)
        plt.title(f"Số lượng Cột Lộ trình Hợp lệ sinh ra trên từng Taxi (Tổng {rep_data['unique_cols']:,} cột)", fontsize=12, fontweight='bold')
        plt.xlabel("Chỉ số Xe Taxi (k = 1..K)", fontsize=10)
        plt.ylabel("Số cột lộ trình (Columns)", fontsize=10)
        plt.grid(axis='y', linestyle='--', alpha=0.5)
        
        plot1_path = os.path.join(plots_dir, "columns_per_taxi.png")
        plt.tight_layout()
        plt.savefig(plot1_path)
        plt.close()
        print(f"[+] Đã kết xuất đồ thị: {plot1_path}")

    # -------------------------------------------------------------------------
    # ĐỒ THỊ 2: HỘI TỤ CẬN TRÊN LAGRANGIAN (UB VS LB GAP)
    # -------------------------------------------------------------------------
    if lagr_steps and lagr_bounds:
        plt.figure(figsize=(10, 5), dpi=300)
        plt.plot(lagr_steps, lagr_bounds, color='#d62728', linewidth=2.0, label='Lagrangian Upper Bound (UB)')
        plt.plot(lagr_steps, lagr_lbs, color='#1f77b4', linewidth=1.5, linestyle='--', label='Primal Best (LB)')
        plt.title(f"Hội tụ Cận trên Lagrangian: UB = {rep_data['ub']:,} vs LB = {rep_data['best']:,} (Gap = {rep_data['ub'] - rep_data['best']})", fontsize=12, fontweight='bold')
        plt.xlabel("Vòng lặp Subgradient", fontsize=10)
        plt.ylabel("Giá trị Mục tiêu", fontsize=10)
        plt.grid(True, linestyle='--', alpha=0.5)
        plt.legend(loc='upper right')
        
        plot2_path = os.path.join(plots_dir, "lagrangian_convergence.png")
        plt.tight_layout()
        plt.savefig(plot2_path)
        plt.close()
        print(f"[+] Đã kết xuất đồ thị: {plot2_path}")

    # -------------------------------------------------------------------------
    # ĐỒ THỊ 3: TIẾN TRÌNH CÁC GIAI ĐOẠN PHASE 1
    # -------------------------------------------------------------------------
    if rep_data['ub'] > 0:
        plt.figure(figsize=(7, 5), dpi=300)
        stages = ["Greedy Start", "Set Packing Best", "Lagrangian UB"]
        vals = [rep_data['greedy_obj'], rep_data['best'], rep_data['ub']]
        colors = ['#ff7f0e', '#1f77b4', '#2ca02c']
        bars = plt.bar(stages, vals, color=colors, width=0.5, edgecolor='black', linewidth=0.8)
        for bar in bars:
            yval = bar.get_height()
            plt.text(bar.get_x() + bar.get_width()/2.0, yval + (max(vals)*0.01), f"{yval:,}", ha='center', va='bottom', fontweight='bold')
        plt.title(f"So sánh Các Mốc Lợi nhuận Phase 1 (Proven Optimal: {'YES' if rep_data['proven'] else 'NO'})", fontsize=11, fontweight='bold')
        plt.ylabel("Objective (Net Benefit)", fontsize=10)
        plt.grid(axis='y', linestyle='--', alpha=0.5)
        
        plot3_path = os.path.join(plots_dir, "phase1_milestones.png")
        plt.tight_layout()
        plt.savefig(plot3_path)
        plt.close()
        print(f"[+] Đã kết xuất đồ thị: {plot3_path}")

    print(f"[🎉] Hoàn tất toàn bộ trực quan hóa Phase 1 cho {run_dir}!\n")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Cách sử dụng: python plot_phase1_sp.py <thư_mục_run> [file_testcase]")
        sys.exit(1)
    tc = sys.argv[2] if len(sys.argv) > 2 else None
    parse_and_visualize(sys.argv[1], tc)

