#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
plot_gm_sisr.py
Trực quan hóa chuyên sâu cho Guided Multi-Basin SISR-SA (GM-SISR).
Xuất các đồ thị phân tích động học tìm kiếm & ablation theo quy chuẩn AGENT.md:
  1. convergence_and_temperature.png: Quỹ đạo hội tụ hàm mục tiêu và chu kỳ nhiệt độ bất biến thang đo.
  2. ablation_analysis.png: Đóng góp của 3 chế độ Ruin & 3 chế độ sắp xếp Recreate.
  3. acceptance_and_records.png: Động thái delta và phân bố chấp nhận bước đi.
"""

import os
import sys
import io
import glob
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

if sys.stdout.encoding != 'utf-8':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
if sys.stderr.encoding != 'utf-8':
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

plt.style.use('seaborn-v0_8-whitegrid' if 'seaborn-v0_8-whitegrid' in plt.style.available else 'default')
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Arial', 'Helvetica']
plt.rcParams['axes.edgecolor'] = '#d0d0d0'
plt.rcParams['axes.linewidth'] = 0.8
plt.rcParams['grid.color'] = '#eeeeee'
plt.rcParams['grid.linestyle'] = '--'

def parse_and_visualize(run_dir, testcase_file=None):
    run_dir = os.path.abspath(run_dir)
    print(f"[i] Đang xử lý thư mục phiên chạy GM-SISR: {run_dir}")
    
    plots_dir = os.path.join(run_dir, "plots")
    os.makedirs(plots_dir, exist_ok=True)
    
    log_file = os.path.join(run_dir, "log.txt")
    report_file = os.path.join(run_dir, "report.txt")
    
    if not os.path.exists(log_file):
        print(f"[!] Không tìm thấy log.txt trong {run_dir}")
        return

    cols_14 = ['iter', 'elapsed_s', 'cycle', 'temp', 'ruin_mode', 'n_removed', 'delta', 'cur_obj', 'best_obj', 'status', 'b_sat', 'r_worse', 'r_imp', 'r_neutral']
    cols_10 = ['iter', 'elapsed_s', 'cycle', 'temp', 'ruin_mode', 'n_removed', 'delta', 'cur_obj', 'best_obj', 'status']
    try:
        df = pd.read_csv(log_file, comment='#', header=None)
        if df.shape[1] >= 14:
            df = df.iloc[:, :14]
            df.columns = cols_14
        else:
            df = df.iloc[:, :10]
            df.columns = cols_10
    except Exception as e:
        print(f"[!] Lỗi đọc log.txt: {e}")
        return
        
    if df.empty:
        print(f"[!] File log.txt rỗng: {log_file}")
        return

    has_thermo_cols = ('b_sat' in df.columns)
    init_obj = df['best_obj'].iloc[0]
    final_obj = df['best_obj'].iloc[-1]
    net_gain = final_obj - init_obj
    imp_global = df[df['status'] == 'IMPROVE_GLOBAL']
    cycles = sorted(df['cycle'].unique())
    
    # -------------------------------------------------------------------------
    # ĐỒ THỊ 1: CONVERGENCE, B_SAT, MULTI-CYCLE TEMPERATURE & ACCEPTANCE RATES
    # -------------------------------------------------------------------------
    if has_thermo_cols:
        fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(11.5, 9.8), sharex=True, dpi=250, gridspec_kw={'height_ratios': [2.2, 1.0, 1.1]})
    else:
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(11, 7.5), sharex=True, dpi=250, gridspec_kw={'height_ratios': [2.2, 1]})
        ax3 = None
    
    sub_step = max(1, len(df) // 3000)
    df_sample = df.iloc[::sub_step]
    
    ax1.plot(df_sample['elapsed_s'], df_sample['cur_obj'], color='#bdc3c7', alpha=0.60, linewidth=0.8, label='Trạng thái tức thời (Current Obj)')
    if has_thermo_cols:
        ax1.plot(df_sample['elapsed_s'], df_sample['b_sat'], color='#e67e22', alpha=0.90, linewidth=1.5, linestyle='--', label='Đỉnh sóng hiện tại (Wave Peak)')
    ax1.plot(df['elapsed_s'], df['best_obj'], color='#1f77b4', linewidth=2.0, label=f'Kỷ lục tốt nhất (Best Obj: {init_obj:,} -> {final_obj:,})')
    
    if not imp_global.empty:
        ax1.scatter(imp_global['elapsed_s'], imp_global['best_obj'], color='#e74c3c', s=22, zorder=5, label=f'Phá kỷ lục ({len(imp_global)} lần)')
        
    ax1.set_ylabel("Hàm mục tiêu Net Profit", fontsize=10.5, fontweight='bold')
    ax1.set_title(f"Quỹ đạo Hội tụ GM-SISR (Lợi nhuận ròng: +{net_gain:,} / +{(net_gain/init_obj*100.0 if init_obj else 0):.1f}%)", fontsize=12, fontweight='bold', pad=10)
    ax1.legend(loc='lower right', frameon=True, fontsize=9.2)
    
    ax2.plot(df['elapsed_s'], df['temp'], color='#8e44ad', linewidth=1.5, label='Nhiệt độ luyện kim T(t)')
    ax2.set_ylabel("Nhiệt độ T", fontsize=10.5, fontweight='bold')
    ax2.set_yscale('log')
    ax2.legend(loc='upper right', frameon=True, fontsize=9.2)

    if ax3 is not None:
        ax3.plot(df['elapsed_s'], df['r_worse'] * 100.0, color='#e74c3c', linewidth=1.3, label='Chấp nhận giảm r_worse (Δ < 0) [%]')
        ax3.plot(df['elapsed_s'], df['r_imp'] * 100.0, color='#2ecc71', linewidth=1.3, label='Chấp nhận tăng r_imp (Δ > 0) [%]')
        ax3_twin = ax3.twinx()
        ax3_twin.plot(df['elapsed_s'], df['r_neutral'] * 100.0, color='#7f8c8d', linewidth=1.1, linestyle=':', alpha=0.85, label='Chấp nhận không đổi r_neutral (Δ = 0) [%]')
        ax3.set_ylabel("r_worse & r_imp (%)", fontsize=9.8, fontweight='bold', color='#c0392b')
        ax3_twin.set_ylabel("r_neutral (%)", fontsize=9.8, fontweight='bold', color='#555555')
        ax3_twin.grid(False)
        ax3.set_xlabel("Thời gian chạy thực tế (giây)", fontsize=10.5, fontweight='bold')
        lines_1, labels_1 = ax3.get_legend_handles_labels()
        lines_2, labels_2 = ax3_twin.get_legend_handles_labels()
        ax3.legend(lines_1 + lines_2, labels_1 + labels_2, loc='upper right', frameon=True, fontsize=8.8)
    else:
        ax2.set_xlabel("Thời gian chạy thực tế (giây)", fontsize=10.5, fontweight='bold')
    
    if 1 < len(cycles) <= 15:
        for c in cycles:
            c_sub = df[df['cycle'] == c]
            t_start = c_sub['elapsed_s'].min()
            if t_start > 0.05:
                ax1.axvline(t_start, color='#7f8c8d', linestyle=':', alpha=0.7, lw=1.0)
                ax2.axvline(t_start, color='#7f8c8d', linestyle=':', alpha=0.7, lw=1.0)
                ax2.text(t_start + 0.03, ax2.get_ylim()[1]*0.4, f"C{c}", fontsize=8.5, color='#555555', fontweight='bold')

    plt.tight_layout()
    plt.savefig(os.path.join(plots_dir, "convergence_and_temperature.png"))
    plt.close()

    # -------------------------------------------------------------------------
    # ĐỒ THỊ 2: ABLATION ANALYSIS (RUIN MODES & CANDIDATE SORTS)
    # -------------------------------------------------------------------------
    fig, (bx1, bx2) = plt.subplots(1, 2, figsize=(11, 4.8), dpi=250)
    
    # Ruin mode records from log
    mode_names = {1: 'Mode 1: String Ruin', 2: 'Mode 2: Unserved-Cavity', 3: 'Mode 3: Slack-Bottleneck'}
    mode_counts = imp_global['ruin_mode'].value_counts()
    
    m_labels = [mode_names.get(m, f"Mode {m}") for m in [1, 2, 3]]
    m_vals = [mode_counts.get(m, 0) for m in [1, 2, 3]]
    colors_ruin = ['#3498db', '#e67e22', '#2ecc71']
    
    bars1 = bx1.bar(m_labels, m_vals, color=colors_ruin, width=0.55, edgecolor='#333333', linewidth=0.8)
    bx1.set_ylabel("Số lần phá kỷ lục toàn cục (Records)", fontsize=10, fontweight='bold')
    bx1.set_title("Hiệu quả Đóng góp Kỷ lục của 3 Toán tử Ruin", fontsize=11, fontweight='bold')
    for bar in bars1:
        yval = bar.get_height()
        pct = (yval / max(1, sum(m_vals))) * 100.0
        bx1.text(bar.get_x() + bar.get_width()/2.0, yval + max(1, sum(m_vals)*0.02), f"{yval} ({pct:.1f}%)", ha='center', va='bottom', fontsize=9, fontweight='bold')
    bx1.set_ylim(0, max(10, max(m_vals) * 1.25))

    # Parse report.txt for Sort Mode records if available
    sort_vals = [0, 0, 0]
    if os.path.exists(report_file):
        with open(report_file, 'r', encoding='utf-8', errors='replace') as rf:
            content = rf.read()
            for line in content.splitlines():
                if "Sort Records (0/1/2)" in line and ":" in line:
                    try:
                        parts = line.split(":")[-1].strip().split("/")
                        sort_vals[0] = int(parts[0].strip())
                        sort_vals[1] = int(parts[1].strip())
                        sort_vals[2] = int(parts[2].strip())
                    except: pass
                elif "Sort Mode 0 (Regret)" in line and "Record Moves =" in line:
                    try: sort_vals[0] = int(line.split("Record Moves =")[-1].strip())
                    except: pass
                elif "Sort Mode 1 (Density)" in line and "Record Moves =" in line:
                    try: sort_vals[1] = int(line.split("Record Moves =")[-1].strip())
                    except: pass
                elif "Sort Mode 2 (Shuffle)" in line and "Record Moves =" in line:
                    try: sort_vals[2] = int(line.split("Record Moves =")[-1].strip())
                    except: pass

    sort_labels = ['Mode 0: Noisy Regret', 'Mode 1: Noisy Density', 'Mode 2: Random Shuffle']
    colors_sort = ['#9b59b6', '#1abc9c', '#95a5a6']
    bars2 = bx2.bar(sort_labels, sort_vals, color=colors_sort, width=0.55, edgecolor='#333333', linewidth=0.8)
    bx2.set_ylabel("Số lần phá kỷ lục toàn cục (Records)", fontsize=10, fontweight='bold')
    bx2.set_title("Hiệu quả Đóng góp của 3 Chế độ Sắp xếp Cand", fontsize=11, fontweight='bold')
    for bar in bars2:
        yval = bar.get_height()
        pct = (yval / max(1, sum(sort_vals))) * 100.0
        bx2.text(bar.get_x() + bar.get_width()/2.0, yval + max(1, sum(sort_vals)*0.02), f"{yval} ({pct:.1f}%)", ha='center', va='bottom', fontsize=9, fontweight='bold')
    bx2.set_ylim(0, max(10, max(sort_vals) * 1.25))

    plt.tight_layout()
    plt.savefig(os.path.join(plots_dir, "ablation_analysis.png"))
    plt.close()

    # -------------------------------------------------------------------------
    # ĐỒ THỊ 3: ACCEPTANCE PROFILE & DELTA DYNAMICS
    # -------------------------------------------------------------------------
    fig, cx = plt.subplots(figsize=(10, 4.5), dpi=250)
    
    status_counts = df['status'].value_counts()
    stat_labels = ['REJECT', 'NEUTRAL', 'SA_ACCEPT', 'IMPROVE_LOCAL', 'IMPROVE_GLOBAL']
    stat_vals = [status_counts.get(s, 0) for s in stat_labels]
    stat_colors = ['#e74c3c', '#95a5a6', '#f39c12', '#2ecc71', '#2980b9']
    
    bars3 = cx.bar(stat_labels, stat_vals, color=stat_colors, width=0.5, edgecolor='#333333', linewidth=0.8)
    cx.set_ylabel("Số vòng lặp (Log scale)", fontsize=10, fontweight='bold')
    cx.set_yscale('log')
    cx.set_title("Phân bố Trạng thái Chấp nhận Bước đi (Search Acceptance Profile)", fontsize=11, fontweight='bold')
    for bar in bars3:
        yval = bar.get_height()
        pct = (yval / max(1, len(df))) * 100.0
        cx.text(bar.get_x() + bar.get_width()/2.0, yval * 1.15, f"{yval:,}\n({pct:.1f}%)", ha='center', va='bottom', fontsize=8.5, fontweight='bold')
    
    plt.tight_layout()
    plt.savefig(os.path.join(plots_dir, "acceptance_and_records.png"))
    plt.close()
    
    print(f"[✓] Đã tạo thành công 3 biểu đồ trong {plots_dir}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Sử dụng: python plot_gm_sisr.py <thư_mục_run> [<file_testcase>]")
        sys.exit(1)
    run_dir = sys.argv[1]
    test_file = sys.argv[2] if len(sys.argv) > 2 else None
    parse_and_visualize(run_dir, test_file)

