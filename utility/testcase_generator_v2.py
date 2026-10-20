#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
testcase_generator_v2.py
Bộ sinh testcase đa năng & toàn diện cho SOICT 2026: Integrated Passenger and Parcel Transportation.
Không giới hạn Euclid: Hỗ trợ đồ thị có hướng bất đối xứng, vi phạm bất đẳng thức tam giác,
nghịch biến thời gian - chi phí, bẫy doanh thu âm, áp lực đội xe và quy mô lớn.
"""

import math
import random
import os
import sys
import argparse
import io

# Đảm bảo in console UTF-8 trên Windows
if sys.stdout.encoding != 'utf-8':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
if sys.stderr.encoding != 'utf-8':
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

def generate_instance(
    filename,
    K=10,
    N=50,
    M=50,
    mode="asymmetric_arbitrary",
    seed=None,
    horizon=3000,
    tight_fleet=False,
    negative_traps=False
):
    if seed is not None:
        random.seed(seed)
        
    V = 2 * N + 2 * M + K
    
    # -------------------------------------------------------------------------
    # 1. MA TRẬN THỜI GIAN t(u, v) VÀ CHI PHÍ c(u, v)
    # -------------------------------------------------------------------------
    t_mat = [[0] * V for _ in range(V)]
    c_mat = [[0] * V for _ in range(V)]

    if mode == "asymmetric_arbitrary":
        # Hoàn toàn ngẫu nhiên, không đối xứng, không Euclid
        for i in range(V):
            for j in range(V):
                if i != j:
                    t_mat[i][j] = random.randint(1, 100)
                    c_mat[i][j] = random.randint(5, 500)

    elif mode == "triangle_violation":
        # Sinh ma trận cơ sở, sau đó cố tình thổi phồng 35% cạnh trực tiếp
        # khiến đi vòng qua trung gian rẻ hơn & nhanh hơn đi thẳng u -> w
        for i in range(V):
            for j in range(V):
                if i != j:
                    t_mat[i][j] = random.randint(5, 30)
                    c_mat[i][j] = random.randint(10, 100)
        # Thổi phồng cạnh trực tiếp
        for i in range(V):
            for j in range(V):
                if i != j and random.random() < 0.35:
                    t_mat[i][j] = random.randint(80, 200)   # Đi thẳng chậm hơn gấp 3-5 lần!
                    c_mat[i][j] = random.randint(300, 1000) # Đi thẳng đắt hơn gấp 3-10 lần!

    elif mode == "uncorrelated_tc":
        # Nghịch biến: Đường đi nhanh thì chi phí đắt đỏ (cao tốc); đường đi chậm thì rẻ mạt
        for i in range(V):
            for j in range(V):
                if i != j:
                    if random.random() < 0.5:
                        # Tuyến cao tốc
                        t_mat[i][j] = random.randint(1, 10)
                        c_mat[i][j] = random.randint(300, 800)
                    else:
                        # Tuyến đường làng
                        t_mat[i][j] = random.randint(40, 120)
                        c_mat[i][j] = random.randint(5, 50)

    elif mode == "euclidean_clustered":
        # Tọa độ 2D có cụm đô thị
        num_clusters = random.randint(3, 5)
        centers = [(random.uniform(100, 900), random.uniform(100, 900)) for _ in range(num_clusters)]
        coords = []
        for _ in range(V):
            if random.random() < 0.85:
                cx, cy = random.choice(centers)
                coords.append((max(0, min(1000, int(random.gauss(cx, 100)))), max(0, min(1000, int(random.gauss(cy, 100))))))
            else:
                coords.append((random.randint(0, 1000), random.randint(0, 1000)))
        for i in range(V):
            for j in range(V):
                if i != j:
                    d = math.hypot(coords[i][0] - coords[j][0], coords[i][1] - coords[j][1])
                    t_mat[i][j] = max(1, int(round(d / 15.0)))
                    c_mat[i][j] = max(1, int(round(d * 1.2)))
    else:
        # Standard general
        for i in range(V):
            for j in range(V):
                if i != j:
                    t_mat[i][j] = random.randint(1, 50)
                    c_mat[i][j] = random.randint(5, 200)

    # -------------------------------------------------------------------------
    # 2. ĐỘI XE & TẢI TRỌNG (TAXIS: K)
    # -------------------------------------------------------------------------
    weights = []
    for _ in range(M):
        if mode == "heavy_parcel":
            w = random.randint(10, 45) # Kiện hàng to, nặng
        else:
            w = random.randint(1, 20)
        weights.append(w)
        
    sum_w = sum(weights)
    avg_cap = (sum_w / K) if K > 0 else 50
    
    capacities = []
    for k in range(K):
        if tight_fleet:
            cap = max(10, int(avg_cap * random.uniform(0.5, 0.9))) # Xe ít tải, áp lực cao
        elif mode == "heavy_parcel":
            cap = max(30, int(avg_cap * random.uniform(1.1, 1.8)))
        else:
            cap = max(20, int(avg_cap * random.uniform(1.0, 1.6)))
        capacities.append(cap)

    # -------------------------------------------------------------------------
    # 3. RÀNG BUỘC THỜI GIAN & DOANH THU
    # -------------------------------------------------------------------------
    if mode == "wide_horizon":
        H = max(horizon, 8000)
        min_slack = 800
        max_slack = 3000
    elif mode == "tight_window":
        H = 1500
        min_slack = 20
        max_slack = 80
    else:
        H = horizon
        min_slack = 100
        max_slack = 500

    # Hành khách (Passenger Requests: 1..N)
    passengers = []
    for i in range(N):
        p_idx = K + i + 1
        d_idx = K + N + i + 1
        t_direct = t_mat[p_idx - 1][d_idx - 1]
        c_direct = c_mat[p_idx - 1][d_idx - 1]
        
        S_i = random.randint(2, 8)
        slack = random.randint(min_slack, max_slack)
        E_i = random.randint(0, max(0, H - t_direct - 2 * S_i - slack))
        L_i = E_i + slack
        
        # Doanh thu
        if negative_traps and random.random() < 0.35:
            # Bẫy lỗ vốn: Tiền nhận được ít hơn cả chi phí xăng đi thẳng!
            T_i = max(5, int(c_direct * random.uniform(0.3, 0.85)))
        else:
            T_i = max(20, int(c_direct * random.uniform(1.4, 2.5) + random.randint(30, 200)))
            
        passengers.append((p_idx, d_idx, E_i, L_i, S_i, T_i))

    # Bưu kiện (Parcel Requests: 1..M)
    parcels = []
    for j in range(M):
        p_idx = K + 2 * N + j + 1
        d_idx = K + 2 * N + M + j + 1
        t_direct = t_mat[p_idx - 1][d_idx - 1]
        c_direct = c_mat[p_idx - 1][d_idx - 1]
        
        w_j = weights[j]
        S_j = random.randint(2, 8)
        slack_p = random.randint(min_slack, max_slack)
        slack_d = random.randint(min_slack, max_slack)
        
        Ep_j = random.randint(0, max(0, H - t_direct - 2 * S_j - slack_p - slack_d))
        Lp_j = Ep_j + slack_p
        
        Ed_j = Ep_j + S_j + t_direct
        Ld_j = max(Ed_j + slack_d, Lp_j + S_j + t_direct + random.randint(30, slack_d))
        
        if negative_traps and random.random() < 0.35:
            T_j = max(5, int(c_direct * random.uniform(0.3, 0.85)))
        else:
            T_j = max(15, int(c_direct * random.uniform(1.3, 2.3) + w_j * 3 + random.randint(20, 150)))
            
        parcels.append((p_idx, d_idx, w_j, Ep_j, Lp_j, Ed_j, Ld_j, S_j, T_j))

    # -------------------------------------------------------------------------
    # 4. GHI FILE ĐẦU RA CHUẨN SOICT 2026
    # -------------------------------------------------------------------------
    os.makedirs(os.path.dirname(os.path.abspath(filename)), exist_ok=True)
    with open(filename, "w", encoding="utf-8") as f:
        f.write(f"{K} {N} {M}\n")
        for k in range(K):
            f.write(f"{k + 1} {capacities[k]}\n")
        for p in passengers:
            f.write(f"{p[0]} {p[1]} {p[2]} {p[3]} {p[4]} {p[5]}\n")
        for pc in parcels:
            f.write(f"{pc[0]} {pc[1]} {pc[2]} {pc[3]} {pc[4]} {pc[5]} {pc[6]} {pc[7]} {pc[8]}\n")
        for r in range(V):
            f.write(" ".join(map(str, t_mat[r])) + "\n")
        for r in range(V):
            f.write(" ".join(map(str, c_mat[r])) + "\n")

    print(f"[+] Đã sinh testcase: {filename} (V={V}, K={K}, N={N}, M={M})")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="SOICT 2026 Advanced Testcase Generator v2")
    parser.add_argument("filename", help="Đường dẫn file lưu testcase")
    parser.add_argument("--K", type=int, default=10, help="Số lượng xe taxi")
    parser.add_argument("--N", type=int, default=50, help="Số lượng hành khách")
    parser.add_argument("--M", type=int, default=50, help="Số lượng bưu kiện")
    parser.add_argument("--mode", choices=[
        "asymmetric_arbitrary",
        "triangle_violation",
        "uncorrelated_tc",
        "euclidean_clustered",
        "wide_horizon",
        "tight_window",
        "heavy_parcel"
    ], default="asymmetric_arbitrary", help="Chế độ phân bố bài toán")
    parser.add_argument("--seed", type=int, default=None, help="Hạt giống ngẫu nhiên")
    parser.add_argument("--tight_fleet", action="store_true", help="Kích hoạt áp lực đội xe (K ít tải)")
    parser.add_argument("--negative_traps", action="store_true", help="Kích hoạt bẫy đơn hàng lỗ vốn")
    
    args = parser.parse_args()
    generate_instance(
        args.filename,
        K=args.K,
        N=args.N,
        M=args.M,
        mode=args.mode,
        seed=args.seed,
        tight_fleet=args.tight_fleet,
        negative_traps=args.negative_traps
    )
