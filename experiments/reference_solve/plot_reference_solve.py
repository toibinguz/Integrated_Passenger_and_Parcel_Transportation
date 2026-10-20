#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
plot_reference_solve.py
Script trực quan hóa chuyên biệt cho nguyên mẫu: reference_solve (Phase 1 SP + Phase 2 SISR/SA)
Tuân thủ quy chuẩn AGENT.md: Xuất biểu đồ và báo cáo chỉ số vào thư mục phiên chạy chỉ định.
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
    """Đọc file hỗ trợ an toàn mọi loại encoding (UTF-8, UTF-8-sig, UTF-16)"""
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

def parse_run_dir(run_dir, testcase_file=None):
    """Phân tích thư mục run và tạo báo cáo + biểu đồ chuyên sâu"""
    run_dir = os.path.abspath(run_dir)
    print(f"[i] Đang xử lý thư mục phiên chạy: {run_dir}")
    
    plots_dir = os.path.join(run_dir, "plots")
    os.makedirs(plots_dir, exist_ok=True)
    
    log_file = os.path.join(run_dir, "log.txt")
    result_file = os.path.join(run_dir, "result.txt")
    report_file = os.path.join(run_dir, "report.txt")
    
    # 1. Đọc dữ liệu từ log.txt (stderr của reference_solve)
    log_data = {
        "phase1_success": False,
        "phase1_states": 0,
        "phase1_cols": 0,
        "phase1_enum_time": 0.0,
        "greedy_obj": None,
        "lagr_ub": None,
        "p1_final_obj": None,
        "p1_sa_iters": 0,
        "p1_proven_optimal": False,
        "phase2_executed": False,
        "p2_iters": 0,
        "p2_final_obj": None
    }
    
    if os.path.exists(log_file):
        with open(log_file, "r", encoding="utf-8", errors="ignore") as f:
            log_text = f.read()
            
        m_enum = re.search(r"\[PHASE 1: ENUMERATION SUCCESS\]\s*enum_states=(\d+)\s*\|\s*unique_cols=(\d+)\s*\|\s*enum_time=([\d\.]+)s", log_text)
        if m_enum:
            log_data["phase1_success"] = True
            log_data["phase1_states"] = int(m_enum.group(1))
            log_data["phase1_cols"] = int(m_enum.group(2))
            log_data["phase1_enum_time"] = float(m_enum.group(3))
            
        m_lagr = re.search(r"\[PHASE 1: LAGRANGIAN\]\s*greedy_obj=(-?\d+)\s*\|\s*post_lagr_obj=(-?\d+)\s*\|\s*lagrangian_UB=(-?\d+)", log_text)
        if m_lagr:
            log_data["greedy_obj"] = int(m_lagr.group(1))
            log_data["lagr_ub"] = int(m_lagr.group(3))
            
        m_p1_sa = re.search(r"\[PHASE 1: SET PACKING SA\]\s*sa_iters=(\d+)\s*\|\s*final_obj=(-?\d+)\s*\|\s*UB=(-?\d+)\s*\|\s*gap=(-?\d+)\s*\|\s*proven_optimal=(YES|NO)", log_text)
        if m_p1_sa:
            log_data["p1_sa_iters"] = int(m_p1_sa.group(1))
            log_data["p1_final_obj"] = int(m_p1_sa.group(2))
            log_data["p1_proven_optimal"] = (m_p1_sa.group(5) == "YES")
            
        m_p2 = re.search(r"\[SUMMARY\] COMPLETED PHASE 2 \(SISR\)\s*\|\s*p2_iters=(\d+)\s*\|\s*final_obj=(-?\d+)", log_text)
        if m_p2:
            log_data["phase2_executed"] = True
            log_data["p2_iters"] = int(m_p2.group(1))
            log_data["p2_final_obj"] = int(m_p2.group(2))

    # 2. Đọc kết quả nghiệm result.txt
    res_tokens = read_file_tokens(result_file)
    declared_obj = int(res_tokens[0]) if res_tokens else 0
    routes = []
    if res_tokens:
        s_idx = 1
        while s_idx < len(res_tokens):
            length = int(res_tokens[s_idx]); s_idx += 1
            rt = [int(res_tokens[s_idx + i]) for i in range(length)]
            s_idx += length
            routes.append(rt)

    # 3. Đọc dữ liệu đề bài nếu có
    tc_tokens = read_file_tokens(testcase_file) if testcase_file and os.path.exists(testcase_file) else []
    K, N, M = 0, 0, 0
    if tc_tokens:
        K = int(tc_tokens[0])
        N = int(tc_tokens[1])
        M = int(tc_tokens[2])

    # 4. Xuất Báo Cáo report.txt
    with open(report_file, "w", encoding="utf-8") as f:
        f.write("=" * 70 + "\n")
        f.write("          BÁO CÁO KỸ THUẬT: REFERENCE SOLVER RUN SESSION\n")
        f.write("=" * 70 + "\n\n")
        f.write(f"  * Thư mục phiên chạy    : {run_dir}\n")
        f.write(f"  * Testcase tham chiếu   : {testcase_file if testcase_file else 'N/A'}\n")
        f.write(f"  * Điểm số Lợi nhuận Net : {declared_obj}\n\n")
        
        f.write("-" * 70 + "\n")
        f.write("THÔNG SỐ PHASE 1 (COLUMN ENUMERATION & SET PACKING):\n")
        f.write("-" * 70 + "\n")
        f.write(f"  * Trạng thái DFS sinh cột: {'HOÀN TẤT THÀNH CÔNG' if log_data['phase1_success'] else 'BỊ HỦY / TIMEOUT'}\n")
        f.write(f"  * Số trạng thái duyệt    : {log_data['phase1_states']:,}\n")
        f.write(f"  * Số cột hợp lệ duy nhất : {log_data['phase1_cols']:,}\n")
        f.write(f"  * Thời gian sinh cột     : {log_data['phase1_enum_time']:.4f}s\n")
        f.write(f"  * Nghiệm Greedy khởi tạo : {log_data['greedy_obj']}\n")
        f.write(f"  * Cận trên Lagrangian UB : {log_data['lagr_ub']}\n")
        f.write(f"  * Số vòng lặp SP SA      : {log_data['p1_sa_iters']:,}\n")
        f.write(f"  * Điểm chốt Phase 1      : {log_data['p1_final_obj']}\n")
        f.write(f"  * Chứng minh tối ưu tuyệt đối: {'CÓ (ĐẠT UPPER BOUND)' if log_data['p1_proven_optimal'] else 'CHƯA'}\n\n")
        
        f.write("-" * 70 + "\n")
        f.write("THÔNG SỐ PHASE 2 (SISR RUIN & RECREATE / SA):\n")
        f.write("-" * 70 + "\n")
        f.write(f"  * Được kích hoạt chạy   : {'CÓ' if log_data['phase2_executed'] else 'KHÔNG (Phase 1 đã giải xong)'}\n")
        if log_data['phase2_executed']:
            f.write(f"  * Số vòng lặp SISR/SA   : {log_data['p2_iters']:,}\n")
            f.write(f"  * Điểm chốt Phase 2     : {log_data['p2_final_obj']}\n\n")
        else:
            f.write("  * Ghi chú               : Thuật toán đạt lời giải tối ưu ngay từ Phase 1.\n\n")
            
        f.write("-" * 70 + "\n")
        f.write("THỐNG KÊ ĐỘI XE & TẢI TRỌNG LỘ TRÌNH:\n")
        f.write("-" * 70 + "\n")
        f.write(f"  * Tổng số xe tham gia   : {len(routes)}\n")
        used_taxis = sum(1 for r in routes if len(r) > 0)
        f.write(f"  * Số xe được điều động  : {used_taxis} / {len(routes)}\n")
        stops = [len(r) for r in routes]
        if stops:
            f.write(f"  * Số điểm dừng trung bình : {np.mean(stops):.1f} điểm/xe\n")
            f.write(f"  * Số điểm dừng lớn nhất   : {max(stops)} điểm (Xe {np.argmax(stops) + 1})\n")
        f.write("=" * 70 + "\n")

    print(f"[+] Đã tạo báo cáo tóm tắt: {report_file}")

    # 5. Vẽ đồ thị chuyên biệt
    # Biểu đồ 1: Phân bố số điểm dừng trên từng taxi
    if routes:
        plt.figure(figsize=(10, 5), dpi=300)
        t_indices = np.arange(1, len(routes) + 1)
        r_lengths = [len(r) for r in routes]
        colors = ['#1f77b4' if l > 0 else '#d3d3d3' for l in r_lengths]
        
        plt.bar(t_indices, r_lengths, color=colors, edgecolor='black', linewidth=0.6)
        plt.title(f"Phân bố Số điểm phục vụ trên từng Taxi (Net Objective = {declared_obj})", fontsize=12, fontweight='bold')
        plt.xlabel("Chỉ số Taxi (k = 1..K)", fontsize=10)
        plt.ylabel("Số điểm phục vụ (Service Points)", fontsize=10)
        plt.grid(axis='y', linestyle='--', alpha=0.5)
        
        plot_path1 = os.path.join(plots_dir, "taxi_workload_distribution.png")
        plt.tight_layout()
        plt.savefig(plot_path1)
        plt.close()
        print(f"[+] Đã kết xuất đồ thị: {plot_path1}")

    # Biểu đồ 2: So sánh Milestone Objective Phase 1 vs UB
    if log_data['greedy_obj'] is not None and log_data['lagr_ub'] is not None:
        plt.figure(figsize=(8, 5), dpi=300)
        stages = ["Greedy", "Lagrangian UB", "Final Best"]
        values = [log_data['greedy_obj'], log_data['lagr_ub'], declared_obj]
        bar_colors = ['#ff7f0e', '#2ca02c', '#1f77b4']
        
        bars = plt.bar(stages, values, color=bar_colors, width=0.5, edgecolor='black', linewidth=0.8)
        for bar in bars:
            yval = bar.get_height()
            plt.text(bar.get_x() + bar.get_width()/2.0, yval + (max(values)*0.01), f"{yval:,}", ha='center', va='bottom', fontweight='bold')
            
        plt.title("Hội tụ Lợi nhuận: Khởi tạo Greedy vs Cận trên UB vs Nghiệm Cuối", fontsize=12, fontweight='bold')
        plt.ylabel("Giá trị Mục tiêu (Net Revenue)", fontsize=10)
        plt.grid(axis='y', linestyle='--', alpha=0.5)
        
        plot_path2 = os.path.join(plots_dir, "objective_milestones.png")
        plt.tight_layout()
        plt.savefig(plot_path2)
        plt.close()
        print(f"[+] Đã kết xuất đồ thị: {plot_path2}")

    print(f"[🎉] Hoàn tất toàn bộ trực quan hóa cho {run_dir}!\n")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Cách sử dụng: python plot_reference_solve.py <thư_mục_run> [file_testcase]")
        sys.exit(1)
    tc = sys.argv[2] if len(sys.argv) > 2 else None
    parse_run_dir(sys.argv[1], tc)
