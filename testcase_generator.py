import random
import sys

def generate_testcase(filename, K, N, M):
    V = 2 * N + 2 * M + K
    with open(filename, 'w', encoding='utf-8') as f:
        # Line 1: K N M
        f.write(f"{K} {N} {M}\n")
        
        weights = [random.randint(1, 20) for _ in range(M)]
        sum_w = sum(weights)
        mu_cap = max(50.0, 1.5 * (sum_w / K) if K > 0 else 50.0)
        sigma_cap = mu_cap * 0.2
        capacities = [max(25, int(random.gauss(mu_cap, sigma_cap))) for _ in range(K)]
        
        # Next K lines: O_k Q_k
        # Taxis start at vertices 1..K
        for k in range(K):
            f.write(f"{k+1} {capacities[k]}\n")
            
        def get_revenue():
            if random.random() < 0.3:
                return int(random.gauss(700, 150))
            return int(random.gauss(300, 100))
            
        peaks = [200, 500, 800]
        def get_E():
            peak = random.choice(peaks)
            E = int(random.gauss(peak, 50))
            return max(0, min(1000, E))
            
        time_matrix = [[random.randint(1, 10) if i != j else 0 for j in range(V)] for i in range(V)]
        
        # Next N lines (Passenger requests): P_i D_i E_i L_i S_i T_i
        for i in range(N):
            S = random.randint(1, 10)
            is_time_monster = random.random() < 0.05
            if is_time_monster:
                width = random.randint(1, 10)
                T = int(random.gauss(2500, 500))
            else:
                width = random.randint(50, 150)
                T = get_revenue()
                
            T = max(10, min(10000, T))
            Ep = get_E()
            Lp = Ep + width
            # Passenger vertices: P_i = K + i + 1, D_i = K + N + i + 1
            p_idx = K + i + 1
            d_idx = K + N + i + 1
            f.write(f"{p_idx} {d_idx} {Ep} {Lp} {S} {T}\n")
            
        # Next M lines (Parcel requests): P_j D_j w_j Ep_j Lp_j Ed_j Ld_j S_j T_j
        for j in range(M):
            S = random.randint(1, 10)
            is_time_monster = random.random() < 0.05
            is_weight_monster = random.random() < 0.05
            
            w = random.randint(40, 50) if is_weight_monster else weights[j]
            
            if is_time_monster or is_weight_monster:
                T = int(random.gauss(3000, 500))
                width = random.randint(1, 10) if is_time_monster else random.randint(50, 150)
            else:
                T = get_revenue()
                width = random.randint(50, 150)
                
            T = max(10, min(10000, T))
            Ep = get_E()
            Lp = Ep + width
            
            p_idx = K + 2*N + j + 1
            d_idx = K + 2*N + M + j + 1
            travel_P_D = time_matrix[p_idx-1][d_idx-1]
            
            Ed = Ep + S + travel_P_D
            Ld = Ed + (random.randint(1, 10) if is_time_monster else random.randint(50, 150))
            
            f.write(f"{p_idx} {d_idx} {w} {Ep} {Lp} {Ed} {Ld} {S} {T}\n")
            
        # Time Matrix
        for r in range(V):
            f.write(" ".join(map(str, time_matrix[r])) + "\n")
            
        # Cost Matrix
        for r in range(V):
            costs = [random.randint(1, 1000) if r != c else 0 for c in range(V)]
            f.write(" ".join(map(str, costs)) + "\n")

if __name__ == '__main__':
    import sys
    if len(sys.argv) < 5:
        print("Usage: python testcase_generator.py <filename> <K> <N> <M>")
        sys.exit(1)
    generate_testcase(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]))