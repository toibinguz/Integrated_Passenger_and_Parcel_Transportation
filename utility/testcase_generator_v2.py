#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
testcase_generator_v2.py
Bộ sinh testcase tinh hoa cho SOICT 2026: Integrated Passenger and Parcel Transportation.
Tập trung vào 7 hình thái địa hình toán học (Fitness Landscapes) của Heuristic:
- Tight Assignment (Cửa sổ hẹp, phân bổ)
- Loose Sequencing (Cửa sổ rộng, hoán vị chuỗi dài)
- Clustered Basins (Hố sâu cục bộ không gian cụm)
- Capacity Bottleneck (Nghẽn tải trọng bưu kiện)
- Asymmetric Random (Đồ thị có hướng bất đối xứng ngẫu nhiên)
- Asymmetric Metric (Đồ thị bất đối xứng có hướng, chuẩn hóa Metric)
- Dense Scale 500 (Quy mô tối đa V=500)

ĐẶC BIỆT: Mọi ma trận t(u, v) và c(u, v) đều được chuẩn hóa qua Floyd-Warshall Metric Closure
nhằm đảm bảo 100% THỎA MÃN BẤT ĐẲNG THỨC TAM GIÁC:
    t(u, w) <= t(u, v) + t(v, w)  và  c(u, w) <= c(u, v) + c(v, w)
Mọi trọng số và chi phí đều nằm trong giới hạn chuẩn: c, t, Q, w <= 1000.
"""

import math
import random
import os
import sys
import argparse
import io
import numpy as np
from scipy.sparse.csgraph import floyd_warshall

# Đảm bảo in console UTF-8 trên Windows
if sys.stdout.encoding != 'utf-8':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
if sys.stderr.encoding != 'utf-8':
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

def generate_curated_instance(filename, profile_key, seed=None):
    if seed is not None:
        random.seed(seed)
        np.random.seed(seed)

    profiles = {
        "tight_assignment": {
            "K": 15, "N": 45, "M": 45, "H": 1600,
            "slack_range": (30, 80), "cap_mult": (1.2, 1.8),
            "graph_type": "symmetric_metric", "parcel_weight": (1, 15)
        },
        "loose_sequencing": {
            "K": 8, "N": 60, "M": 60, "H": 8000,
            "slack_range": (800, 2500), "cap_mult": (1.2, 1.8),
            "graph_type": "symmetric_metric", "parcel_weight": (1, 15)
        },
        "clustered_basins": {
            "K": 10, "N": 60, "M": 60, "H": 3500,
            "slack_range": (150, 450), "cap_mult": (1.2, 1.8),
            "graph_type": "clustered", "parcel_weight": (1, 15)
        },
        "capacity_bottleneck": {
            "K": 10, "N": 40, "M": 80, "H": 3500,
            "slack_range": (150, 500), "cap_mult": (0.7, 1.05), # Rất nghẽn tải trọng
            "graph_type": "symmetric_metric", "parcel_weight": (12, 28)
        },
        "asymmetric_random": {
            "K": 10, "N": 60, "M": 60, "H": 3500,
            "slack_range": (150, 500), "cap_mult": (1.2, 1.8),
            "graph_type": "asymmetric_metric", "parcel_weight": (1, 18)
        },
        "asymmetric_metric": {
            "K": 10, "N": 60, "M": 60, "H": 3500,
            "slack_range": (150, 500), "cap_mult": (1.2, 1.8),
            "graph_type": "asymmetric_flow", "parcel_weight": (1, 18)
        },
        "dense_scale_500": {
            "K": 20, "N": 120, "M": 120, "H": 5000,
            "slack_range": (200, 700), "cap_mult": (1.2, 1.7),
            "graph_type": "symmetric_metric", "parcel_weight": (1, 20)
        }
    }

    if profile_key not in profiles:
        raise ValueError(f"Unknown profile: {profile_key}. Available: {list(profiles.keys())}")

    p = profiles[profile_key]
    K, N, M = p["K"], p["N"], p["M"]
    V = 2 * N + 2 * M + K
    H = p["H"]
    min_slack, max_slack = p["slack_range"]
    gtype = p["graph_type"]

    # 1. Sinh Ma trận Thời gian t(u, v) & Chi phí c(u, v) với Floyd-Warshall Metric Closure
    if gtype == "clustered":
        num_clusters = 4
        centers = [(random.uniform(100, 900), random.uniform(100, 900)) for _ in range(num_clusters)]
        coords = []
        for _ in range(V):
            cx, cy = random.choice(centers)
            coords.append((max(0, min(1000, int(random.gauss(cx, 75)))), max(0, min(1000, int(random.gauss(cy, 75))))))
        coords = np.array(coords, dtype=np.float64)
        diff = coords[:, None, :] - coords[None, :, :]
        d = np.sqrt(np.sum(diff**2, axis=-1))
        t_init = np.maximum(1, np.round(d / 15.0).astype(np.int32))
        c_init = np.maximum(1, np.round(t_init * 2.5 + np.random.uniform(1, 10, size=(V, V))).astype(np.int32))
        is_directed = False
    elif gtype in ("asymmetric_metric", "asymmetric_flow"):
        # Đồ thị có hướng bất đối xứng nhưng thỏa mãn BĐT Tam giác (mô phỏng đường dốc / lưu lượng giao thông)
        coords = np.random.uniform(0, 1000, size=(V, 2))
        diff = coords[:, None, :] - coords[None, :, :]
        d = np.sqrt(np.sum(diff**2, axis=-1))
        t_base = np.maximum(1, np.round(d / 15.0).astype(np.int32))
        
        # Hàm thế độ cao / lưu lượng gió khiến t(u, v) != t(v, u)
        potentials = np.random.uniform(-10, 10, size=V)
        pot_diff = potentials[None, :] - potentials[:, None]
        t_init = np.maximum(1, t_base + np.round(pot_diff).astype(np.int32))
        c_init = np.maximum(1, np.round(t_init * 2.4 + np.random.uniform(1, 10, size=(V, V))).astype(np.int32))
        is_directed = True
    else: # symmetric_metric
        coords = np.random.uniform(0, 1000, size=(V, 2))
        diff = coords[:, None, :] - coords[None, :, :]
        d = np.sqrt(np.sum(diff**2, axis=-1))
        t_init = np.maximum(1, np.round(d / 15.0).astype(np.int32))
        c_init = np.maximum(1, np.round(t_init * 2.5 + np.random.uniform(1, 8, size=(V, V))).astype(np.int32))
        # Đồng bộ hóa đối xứng
        t_init = np.maximum(1, np.round((t_init + t_init.T) / 2.0).astype(np.int32))
        c_init = np.maximum(1, np.round((c_init + c_init.T) / 2.0).astype(np.int32))
        is_directed = False

    # Đặt đường chéo = 0
    np.fill_diagonal(t_init, 0)
    np.fill_diagonal(c_init, 0)

    # FLOYD-WARSHALL METRIC CLOSURE: Đảm bảo 100% Bất đẳng thức Tam giác
    t_mat = floyd_warshall(t_init, directed=is_directed).astype(np.int32)
    c_mat = floyd_warshall(c_init, directed=is_directed).astype(np.int32)

    # Đảm bảo đường chéo = 0, các cạnh ngoài chéo >= 1 và <= 1000
    np.fill_diagonal(t_mat, 0)
    np.fill_diagonal(c_mat, 0)
    for i in range(V):
        for j in range(V):
            if i != j:
                if t_mat[i, j] < 1: t_mat[i, j] = 1
                if c_mat[i, j] < 1: c_mat[i, j] = 1
                if t_mat[i, j] > 1000: t_mat[i, j] = 1000
                if c_mat[i, j] > 1000: c_mat[i, j] = 1000

    # Chuyển về list thuần Python
    t_mat = t_mat.tolist()
    c_mat = c_mat.tolist()

    # 2. Sinh Đội xe & Tải trọng (w <= 50, Q <= 200, đều <= 1000)
    weights = [random.randint(p["parcel_weight"][0], p["parcel_weight"][1]) for _ in range(M)]
    sum_w = sum(weights)
    avg_cap = (sum_w / K) if K > 0 else 40
    c_lo, c_hi = p["cap_mult"]
    capacities = [max(15, min(1000, int(avg_cap * random.uniform(c_lo, c_hi)))) for _ in range(K)]

    # 3. Sinh Yêu cầu Hành khách
    passengers = []
    for i in range(N):
        p_idx = K + i + 1
        d_idx = K + N + i + 1
        t_dir = t_mat[p_idx - 1][d_idx - 1]
        c_dir = c_mat[p_idx - 1][d_idx - 1]
        
        S_i = random.randint(2, 6)
        slack = random.randint(min_slack, max_slack)
        E_i = random.randint(0, max(0, H - t_dir - 2 * S_i - slack))
        L_i = E_i + slack
        
        # Doanh thu đảm bảo phục vụ là có lãi hợp lý (T <= 1000)
        T_i = min(1000, max(20, int(c_dir * random.uniform(1.3, 2.2) + random.randint(20, 100))))
        passengers.append((p_idx, d_idx, E_i, L_i, S_i, T_i))

    # 4. Sinh Yêu cầu Bưu kiện
    parcels = []
    for j in range(M):
        p_idx = K + 2 * N + j + 1
        d_idx = K + 2 * N + M + j + 1
        t_dir = t_mat[p_idx - 1][d_idx - 1]
        c_dir = c_mat[p_idx - 1][d_idx - 1]
        
        w_j = weights[j]
        S_j = random.randint(2, 6)
        slack_p = random.randint(min_slack, max_slack)
        slack_d = random.randint(min_slack, max_slack)
        
        Ep_j = random.randint(0, max(0, H - t_dir - 2 * S_j - slack_p - slack_d))
        Lp_j = Ep_j + slack_p
        
        Ed_j = Ep_j + S_j + t_dir
        Ld_j = max(Ed_j + slack_d, Lp_j + S_j + t_dir + random.randint(20, slack_d))
        
        T_j = min(1000, max(15, int(c_dir * random.uniform(1.2, 2.0) + w_j * 2 + random.randint(15, 80))))
        parcels.append((p_idx, d_idx, w_j, Ep_j, Lp_j, Ed_j, Ld_j, S_j, T_j))

    # 5. Ghi File Chuẩn SOICT 2026
    os.makedirs(os.path.dirname(os.path.abspath(filename)), exist_ok=True)
    with open(filename, "w", encoding="utf-8") as f:
        f.write(f"{K} {N} {M}\n")
        for k in range(K):
            f.write(f"{k + 1} {capacities[k]}\n")
        for pc in passengers:
            f.write(f"{pc[0]} {pc[1]} {pc[2]} {pc[3]} {pc[4]} {pc[5]}\n")
        for pr in parcels:
            f.write(f"{pr[0]} {pr[1]} {pr[2]} {pr[3]} {pr[4]} {pr[5]} {pr[6]} {pr[7]} {pr[8]}\n")
        for r in range(V):
            f.write(" ".join(map(str, t_mat[r])) + "\n")
        for r in range(V):
            f.write(" ".join(map(str, c_mat[r])) + "\n")

    print(f"[+] Đã sinh testcase chuẩn [{profile_key}]: {filename} (V={V}, K={K}, N={N}, M={M})")

def build_all_benchmarks(target_dir="testcases/benchmark"):
    curated_list = [
        ("bench_01_tight_assignment.txt", "tight_assignment", 101),
        ("bench_02_loose_sequencing.txt", "loose_sequencing", 102),
        ("bench_03_clustered_basins.txt", "clustered_basins", 103),
        ("bench_04_capacity_bottleneck.txt", "capacity_bottleneck", 104),
        ("bench_05_asymmetric_random.txt", "asymmetric_random", 105),
        ("bench_06_asymmetric_metric.txt", "asymmetric_metric", 106),
        ("bench_07_dense_scale_500.txt", "dense_scale_500", 107)
    ]
    # Xóa file cũ nếu có tên bench_06_non_euclidean_violation.txt
    old_viol_file = os.path.join(target_dir, "bench_06_non_euclidean_violation.txt")
    if os.path.exists(old_viol_file):
        os.remove(old_viol_file)
        print(f"[*] Đã loại bỏ file vi phạm cũ: {old_viol_file}")

    for fname, pkey, s in curated_list:
        fpath = os.path.join(target_dir, fname)
        generate_curated_instance(fpath, pkey, seed=s)
    print(f"\n[SUCCESS] Hoàn tất sinh toàn bộ 7 testcase tinh hoa vào thư mục: {target_dir}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="SOICT 2026 Curated Benchmark Generator")
    parser.add_argument("--build-all", action="store_true", help="Tự động sinh toàn bộ 7 testcase chuẩn vào testcases/benchmark")
    parser.add_argument("--outdir", default="testcases/benchmark", help="Thư mục lưu testcases")
    parser.add_argument("--profile", choices=[
        "tight_assignment",
        "loose_sequencing",
        "clustered_basins",
        "capacity_bottleneck",
        "asymmetric_random",
        "asymmetric_metric",
        "dense_scale_500"
    ], default=None, help="Chọn profile cụ thể")
    parser.add_argument("--output", default=None, help="Đường dẫn file lưu nếu chọn profile đơn")
    parser.add_argument("--seed", type=int, default=101, help="Hạt giống ngẫu nhiên")

    args = parser.parse_args()
    if args.build_all or (args.profile is None and args.output is None):
        build_all_benchmarks(args.outdir)
    elif args.profile and args.output:
        generate_curated_instance(args.output, args.profile, seed=args.seed)
    else:
        parser.print_help()
