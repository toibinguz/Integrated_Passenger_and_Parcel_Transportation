#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
plot_sisr_sa.py
Script trực quan hóa chuyên biệt cho nguyên mẫu: SISR + SA (Phase 2 Standalone)
Tuân thủ quy chuẩn AGENT.md: Xuất "Bộ tứ bắt buộc" và vẽ đồ thị chuyên sâu vào thư mục plots/
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

def read_file_tokens(filepath):
    if not os.path.exists(filepath):
        return []
    encoding = 'utf-8'
    with open(filepath, "rb") as bf:
        head = bf.read(4)
        if head.startswith(b'\xff\xfe') or head.startswith(b'\xfe\xff'):
            encoding = 'utf-16'
        elif head.startswith(b'\xef\xbb\xbf'):
            encoding = 'utf-8-sig'
    with open(filepath, 'r', encoding=encoding, errors='ignore') as f:
        content = f.read()
    tokens = []
    for line in content.splitlines():
        if '#' in line:
            line = line[:line.index('#')]
        tokens.extend(line.split())
    return tokens

def parse_and_visualize(run_dir, testcase_file=None):
    run_dir = os.path.abspath(run_dir)
    print(f"[i] Đang xử lý thư mục phiên chạy SISR+SA: {run_dir}")
    
    plots_dir = os.path.join(run_dir, "plots")
    os.makedirs(plots_dir, exist_ok=True)
    
    log_file = os.path.join(run_dir, "log.txt")
    result_file = os.path.join(run_dir, "result.txt")
    
    # 1. Đọc log.txt (dạng CSV)
    iters = []
    elapsed_secs = []
    temps = []
    deltas = []
    cur_objs = []
    best_objs = []
    statuses = []
    
    if os.path.exists(log_file):
        with open(log_file, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split(",")
                if len(parts) >= 10:
                    try:
                        it = int(parts[0])
                        el = float(parts[1])
                        temp = float(parts[3])
                        delta = int(parts[6])
                        c_obj = int(parts[7])
                        b_obj = int(parts[8])
                        stat = parts[9]
                        
                        iters.append(it)
                        elapsed_secs.append(el)
                        temps.append(temp)
                        deltas.append(delta)
                        cur_objs.append(c_obj)
                        best_objs.append(b_obj)
                        statuses.append(stat)
                    except ValueError:
                        continue

    # 2. Đọc nghiệm result.txt
    routes = []
    declared_obj = 0
    if os.path.exists(result_file):
        tokens = read_file_tokens(result_file)
        if tokens:
            declared_obj = int(tokens[0])
            s_idx = 1
            while s_idx < len(tokens):
                length = int(tokens[s_idx]); s_idx += 1
                rt = [int(tokens[s_idx + i]) for i in range(length)]
                s_idx += length
                routes.append(rt)

    # -------------------------------------------------------------------------
    # ĐỒ THỊ 1: CONVERGENCE & TEMPERATURE COOLING SCHEDULE
    # -------------------------------------------------------------------------
    if elapsed_secs and best_objs:
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 7), sharex=True, dpi=300)
        
        # Panel trên: Objective Progression
        ax1.plot(elapsed_secs, best_objs, color='#1f77b4', linewidth=2.0, label='Global Best Obj')
        ax1.plot(elapsed_secs, cur_objs, color='#ff7f0e', alpha=0.4, linewidth=0.8, label='Current Obj')
        ax1.set_ylabel("Net Objective (Benefit)", fontsize=10, fontweight='bold')
        ax1.set_title(f"SISR + Simulated Annealing Convergence (Final Best = {declared_obj:,})", fontsize=12, fontweight='bold')
        ax1.grid(True, linestyle='--', alpha=0.5)
        ax1.legend(loc='lower right')
        
        # Panel dưới: Nhiệt độ Multi-cycle SA
        ax2.plot(elapsed_secs, temps, color='#d62728', linewidth=1.5, label='SA Temperature T(t)')
        ax2.set_xlabel("Elapsed Wall-Clock Time (seconds)", fontsize=10, fontweight='bold')
        ax2.set_ylabel("Temperature", fontsize=10, fontweight='bold')
        ax2.set_yscale('log')
        ax2.grid(True, linestyle='--', alpha=0.5)
        ax2.legend(loc='upper right')
        
        plot1_path = os.path.join(plots_dir, "convergence_and_temperature.png")
        plt.tight_layout()
        plt.savefig(plot1_path)
        plt.close()
        print(f"[+] Đã kết xuất đồ thị: {plot1_path}")

    # -------------------------------------------------------------------------
    # ĐỒ THỊ 2: DELTA OBJECTIVE & MOVE ACCEPTANCE BREAKDOWN
    # -------------------------------------------------------------------------
    if deltas:
        plt.figure(figsize=(10, 5), dpi=300)
        
        sub_sample = max(1, len(deltas) // 2000)
        sample_x = elapsed_secs[::sub_sample]
        sample_y = deltas[::sub_sample]
        sample_st = statuses[::sub_sample]
        
        color_map = {
            'IMPROVE_GLOBAL': '#2ca02c', # Xanh lá đậm
            'IMPROVE_LOCAL': '#1f77b4',  # Xanh dương
            'SA_ACCEPT': '#ff7f0e',      # Cam (chấp nhận bước lùi)
            'REJECT': '#7f7f7f'          # Xám
        }
        point_colors = [color_map.get(s, '#7f7f7f') for s in sample_st]
        
        plt.scatter(sample_x, sample_y, c=point_colors, s=10, alpha=0.6, edgecolors='none')
        plt.axhline(0, color='black', linestyle='--', linewidth=0.8)
        plt.title("Biến động Delta Objective (Δ Benefit) qua từng bước thử", fontsize=12, fontweight='bold')
        plt.xlabel("Thời gian chạy (giây)", fontsize=10)
        plt.ylabel("Delta Objective", fontsize=10)
        plt.grid(True, linestyle='--', alpha=0.5)
        
        # Custom legend
        from matplotlib.lines import Line2D
        legend_elements = [
            Line2D([0], [0], marker='o', color='w', markerfacecolor='#2ca02c', markersize=7, label='Phá kỷ lục (Record)'),
            Line2D([0], [0], marker='o', color='w', markerfacecolor='#ff7f0e', markersize=7, label='Chấp nhận bước lùi SA'),
            Line2D([0], [0], marker='o', color='w', markerfacecolor='#7f7f7f', markersize=7, label='Bị từ chối (Reject)')
        ]
        plt.legend(handles=legend_elements, loc='upper right')
        
        plot2_path = os.path.join(plots_dir, "acceptance_and_delta.png")
        plt.tight_layout()
        plt.savefig(plot2_path)
        plt.close()
        print(f"[+] Đã kết xuất đồ thị: {plot2_path}")

    # -------------------------------------------------------------------------
    # ĐỒ THỊ 3: TẢI TRỌNG VÀ SỐ ĐIỂM PHỤC VỤ TRÊN TỪNG TAXI
    # -------------------------------------------------------------------------
    if routes:
        plt.figure(figsize=(10, 5), dpi=300)
        t_indices = np.arange(1, len(routes) + 1)
        r_lengths = [len(r) for r in routes]
        bar_colors = ['#1f77b4' if l > 0 else '#e0e0e0' for l in r_lengths]
        
        plt.bar(t_indices, r_lengths, color=bar_colors, edgecolor='black', linewidth=0.6)
        plt.title(f"Phân bố Số điểm phục vụ trên từng Taxi (Tổng {len(routes)} xe)", fontsize=12, fontweight='bold')
        plt.xlabel("Chỉ số Taxi (k = 1..K)", fontsize=10)
        plt.ylabel("Số điểm phục vụ", fontsize=10)
        plt.grid(axis='y', linestyle='--', alpha=0.5)
        
        plot3_path = os.path.join(plots_dir, "taxi_workload.png")
        plt.tight_layout()
        plt.savefig(plot3_path)
        plt.close()
        print(f"[+] Đã kết xuất đồ thị: {plot3_path}")

    print(f"[🎉] Hoàn tất toàn bộ trực quan hóa SISR+SA cho {run_dir}!\n")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Cách sử dụng: python plot_sisr_sa.py <thư_mục_run> [file_testcase]")
        sys.exit(1)
    tc = sys.argv[2] if len(sys.argv) > 2 else None
    parse_and_visualize(sys.argv[1], tc)
