#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_phase1_ladder.py
Sinh bộ testcase bậc thang (Ladder Suite) tăng dần quy mô từ V=100 đến V=1200,
thỏa mãn 100% TIÊN QUYẾT TOÁN HỌC của Phase 1:
1. Ma trận Đối xứng & Bất đẳng thức tam giác nghiêm ngặt (2D Euclidean Metric):
   t(u, w) <= t(u, v) + t(v, w) và c(u, w) <= c(u, v) + c(v, w)
2. Tỷ lệ tương quan vật lý T_avg / Horizon ~ 0.25:
   Thời gian di chuyển trung bình ~ 40..80 trong Horizon ~ 250,
   đảm bảo mỗi xe tự nhiên chỉ có thể phục vụ tối đa d = 2..4 requests.
3. Cửa sổ thời gian chính xác từng chặng (Pinpoint time windows: Lp - Ep in [5, 20]).
"""

import math
import random
import os
import sys
import io

if sys.stdout.encoding != 'utf-8':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
if sys.stderr.encoding != 'utf-8':
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

def generate_ladder_instance(filename, K, N, M, seed=101):
    random.seed(seed)
    V = 2 * N + 2 * M + K
    os.makedirs(os.path.dirname(os.path.abspath(filename)), exist_ok=True)
    
    # 1. Tọa độ 2D đối xứng & bất đẳng thức tam giác tuyệt đối
    coords = [(random.randint(0, 1000), random.randint(0, 1000)) for _ in range(V)]
    t_mat = [[0] * V for _ in range(V)]
    c_mat = [[0] * V for _ in range(V)]
    
    for i in range(V):
        for j in range(i + 1, V):
            dist = math.hypot(coords[i][0] - coords[j][0], coords[i][1] - coords[j][1])
            # Tương quan khoảng cách d (0..1414) sang t (15..95) và c (10..750)
            t_val = max(15, min(100, int(round(15 + dist * 80 / 1414.0))))
            c_val = max(10, min(800, int(round(10 + dist * 740 / 1414.0))))
            t_mat[i][j] = t_mat[j][i] = t_val
            c_mat[i][j] = c_mat[j][i] = c_val

    # 2. Đội xe Taxis
    weights = [random.randint(2, 15) for _ in range(M)]
    caps = [max(20, min(60, int(random.gauss(30, 5)))) for _ in range(K)]

    # 3. Cửa sổ thời gian trong Horizon H = 250
    passengers = []
    for i in range(N):
        p_idx = K + i + 1
        d_idx = K + N + i + 1
        c_dir = c_mat[p_idx - 1][d_idx - 1]
        S_i = random.randint(2, 5)
        Ep = random.randint(0, 85)
        Lp = Ep + random.randint(5, 20)
        T_val = min(1000, max(50, int(c_dir * random.uniform(1.3, 2.0) + random.randint(30, 100))))
        passengers.append((p_idx, d_idx, Ep, Lp, S_i, T_val))

    parcels = []
    for j in range(M):
        p_idx = K + 2 * N + j + 1
        d_idx = K + 2 * N + M + j + 1
        c_dir = c_mat[p_idx - 1][d_idx - 1]
        t_dir = t_mat[p_idx - 1][d_idx - 1]
        w_j = weights[j]
        S_j = random.randint(2, 5)
        Ep = random.randint(0, 75)
        Lp = Ep + random.randint(5, 20)
        Ed = Ep + S_j + t_dir
        Ld = min(250, Ed + random.randint(10, 40))
        T_val = min(1000, max(40, int(c_dir * random.uniform(1.2, 1.8) + w_j * 3 + random.randint(20, 80))))
        parcels.append((p_idx, d_idx, w_j, Ep, Lp, Ed, Ld, S_j, T_val))

    # 4. Ghi file
    with open(filename, 'w', encoding='utf-8') as f:
        f.write(f"{K} {N} {M}\n")
        for k in range(K):
            f.write(f"{k+1} {caps[k]}\n")
        for p in passengers:
            f.write(f"{p[0]} {p[1]} {p[2]} {p[3]} {p[4]} {p[5]}\n")
        for pr in parcels:
            f.write(f"{pr[0]} {pr[1]} {pr[2]} {pr[3]} {pr[4]} {pr[5]} {pr[6]} {pr[7]} {pr[8]}\n")
        for r in range(V):
            f.write(" ".join(map(str, t_mat[r])) + "\n")
        for r in range(V):
            f.write(" ".join(map(str, c_mat[r])) + "\n")

    print(f"[+] Đã sinh testcase bậc thang chuẩn Phase 1: {filename} (V={V}, K={K}, N={N}, M={M})")

def build_ladder():
    configs = [
        ("ladder_01_V100.txt", 6, 23, 24, 101),
        ("ladder_02_V200.txt", 10, 47, 48, 102),
        ("ladder_03_V350.txt", 15, 83, 84, 103),
        ("ladder_04_V500.txt", 20, 120, 120, 104),
        ("ladder_05_V650.txt", 25, 156, 156, 105),
        ("ladder_06_V800.txt", 30, 192, 193, 106),
        ("ladder_07_V1000.txt", 40, 240, 240, 107),
        ("ladder_08_V1200.txt", 50, 287, 288, 108),
    ]
    outdir = "testcases/phase1_ladder"
    for fname, k, n, m, s in configs:
        generate_ladder_instance(os.path.join(outdir, fname), k, n, m, seed=s)
    print(f"\n[SUCCESS] Hoàn tất sinh toàn bộ 8 bậc thang chuẩn tiên quyết vào {outdir}!")

if __name__ == '__main__':
    build_ladder()
