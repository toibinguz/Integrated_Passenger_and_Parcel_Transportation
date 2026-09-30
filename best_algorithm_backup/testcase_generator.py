import random
import sys
import numpy as np

def generate_testcase(filename, K, N, M):
    V = 2 * N + 2 * M + K
    with open(filename, 'w') as f:
        # Phần 1: Cấu hình chung K, N, M
        f.write(f"{K} {N} {M}\n")
        
        # Sinh trọng lượng hàng hóa trước để tính Tải trọng xe
        weights = [random.randint(1, 20) for _ in range(M)]
        sum_w = sum(weights)
        
        # Phần 2: Capacities (Phân phối chuẩn, đảm bảo đủ chứa nhiều hàng)
        mu_cap = max(50.0, 1.5 * (sum_w / K) if K > 0 else 50.0)
        sigma_cap = mu_cap * 0.2
        capacities = [max(25, int(random.gauss(mu_cap, sigma_cap))) for _ in range(K)]
        f.write(" ".join(map(str, capacities)) + "\n")
        
        # Hàm sinh Revenue theo Bimodal Normal (Tốt: 700, Bình dân: 300)
        def get_revenue():
            if random.random() < 0.3: # 30% Hàng VIP
                rev = int(random.gauss(700, 150))
            else: # 70% Hàng bình dân
                rev = int(random.gauss(300, 100))
            return max(10, min(1000, rev))
            
        # Hàm sinh Thời gian mở cửa E (Clustered Peaks: 200, 500, 800)
        peaks = [200, 500, 800]
        def get_E():
            peak = random.choice(peaks)
            E = int(random.gauss(peak, 50))
            return max(0, min(1000, E))
            
        # Sinh ma trận thời gian trước (để tính Ed cho Parcels)
        # t(u,v) ngẫu nhiên [1, 10]
        time_matrix = [[random.randint(1, 10) if i != j else 0 for j in range(V)] for i in range(V)]
        
        # Phần 3: Hành khách
        for i in range(N):
            S = random.randint(1, 10)
            
            # Anomaly Injection (5% Time Window hẹp)
            is_time_monster = random.random() < 0.05
            
            if is_time_monster:
                width = random.randint(1, 10) # Bóp nghẹt thời gian
                T = int(random.gauss(2500, 500)) # Doanh thu khổng lồ
            else:
                width = random.randint(50, 150)
                T = get_revenue()
                
            T = max(10, min(10000, T))
            Ep = get_E()
            Lp = Ep + width
            f.write(f"{T} {Ep} {Lp} {S}\n")
            
        # Phần 4: Hàng hóa
        for j in range(M):
            S = random.randint(1, 10)
            
            is_time_monster = random.random() < 0.05
            is_weight_monster = random.random() < 0.05
            
            # Nếu là Weight Monster, bốc trọng lượng từ 40 đến 50 (gần bằng nguyên cái xe)
            w = random.randint(40, 50) if is_weight_monster else weights[j]
            
            # Nếu là dị nhân bất kỳ, doanh thu x3, x4
            if is_time_monster or is_weight_monster:
                T = int(random.gauss(3000, 500))
                width = random.randint(1, 10) if is_time_monster else random.randint(50, 150)
            else:
                T = get_revenue()
                width = random.randint(50, 150)
                
            T = max(10, min(10000, T))
            Ep = get_E()
            Lp = Ep + width
            
            p_idx = K + 2*N + j
            d_idx = K + 2*N + M + j
            travel_P_D = time_matrix[p_idx][d_idx]
            
            Ed = Ep + S + travel_P_D
            # Ld cũng bị bóp hẹp lại nếu là Time Monster
            Ld = Ed + (random.randint(1, 10) if is_time_monster else random.randint(50, 150))
            
            f.write(f"{T} {Ep} {Lp} {Ed} {Ld} {S} {w}\n")
            
        # Phần 5: Time Matrix
        for r in range(V):
            f.write(" ".join(map(str, time_matrix[r])) + "\n")
            
        # Phần 6: Cost Matrix (Random [1, 1000] - Chênh lệch cực lớn với Time)
        for r in range(V):
            costs = [random.randint(1, 1000) if r != c else 0 for c in range(V)]
            f.write(" ".join(map(str, costs)) + "\n")

if __name__ == '__main__':
    if len(sys.argv) < 5:
        print("Usage: python testcase_generator.py <filename> <K> <N> <M>")
        sys.exit(1)
    filename = sys.argv[1]
    K = int(sys.argv[2])
    N = int(sys.argv[3])
    M = int(sys.argv[4])
    generate_testcase(filename, K, N, M)
    print(f"Generated standardized testcase {filename} (V = {2*N + 2*M + K})")
