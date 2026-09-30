import sys
import time
from ortools.linear_solver import pywraplp
from soict_solver import Data

def solve_milp(filename):
    print(f"[*] Parsing data from {filename}...")
    data = Data(filename)
    
    solver = pywraplp.Solver.CreateSolver('SCIP')
    if not solver:
        print("SCIP solver not available.")
        return
        
    # Chuẩn bị tập hợp
    V_mac = data.V_mac
    sink = V_mac
    K = data.K
    
    # Precompute time and cost matrices with sink
    time_matrix = [[0]*(V_mac+1) for _ in range(V_mac+1)]
    cost_matrix = [[0]*(V_mac+1) for _ in range(V_mac+1)]
    for i in range(V_mac):
        for j in range(V_mac):
            time_matrix[i][j] = data.time_matrix[i][j]
            cost_matrix[i][j] = data.cost_matrix[i][j]
    
    # Từ các node tới sink: cost = 0, time = 0 (Open VRP)
    
    print(f"[*] Building MILP Model (Two-Index Formulation)...")
    print(f"    - Nodes: {V_mac} + Sink")
    print(f"    - Vehicles: {K}")
    
    # Biến x[i][j]: cạnh (i, j)
    x = {}
    arc_count = 0
    for i in range(V_mac):
        for j in range(1, V_mac + 1):
            if i == j: continue
            if i == 0 and j == sink: continue # Không đi thẳng từ start tới sink
            
            # Arc Elimination (Time window constraint)
            if i > 0 and j < sink:
                node_i = data.nodes[i]
                node_j = data.nodes[j]
                if node_i.e + node_i.duration + time_matrix[i][j] > node_j.l:
                    continue # Bỏ qua cạnh vô lý
            
            x[i, j] = solver.IntVar(0, 1, f'x_{i}_{j}')
            arc_count += 1
            
    # Biến y[i][k]: xe k thăm node i
    y = {}
    for i in range(1, V_mac):
        for k in range(K):
            y[i, k] = solver.IntVar(0, 1, f'y_{i}_{k}')
            
    # Biến t[i]: thời gian bắt đầu
    t = {}
    for i in range(V_mac):
        e_val = data.nodes[i].e if i > 0 else 0
        l_val = data.nodes[i].l if i > 0 else 999999
        t[i] = solver.NumVar(e_val, l_val, f't_{i}')
        
    # Biến u[i]: tải trọng
    u = {}
    for i in range(V_mac):
        u[i] = solver.NumVar(0, max(data.capacities), f'u_{i}')
        
    print(f"    - Generated {arc_count} binary x variables (Arc eliminated).")
    
    # Ràng buộc 1: Flow Conservation
    for i in range(1, V_mac):
        # Inflow = Outflow = sum(y_i^k)
        inflow = sum(x[j, i] for j in range(V_mac) if (j, i) in x)
        outflow = sum(x[i, j] for j in range(1, V_mac + 1) if (i, j) in x)
        y_sum = sum(y[i, k] for k in range(K))
        
        solver.Add(inflow == y_sum)
        solver.Add(outflow == y_sum)
        solver.Add(y_sum <= 1)
        
    # Ràng buộc 2: Depot xuất phát tối đa K xe
    solver.Add(sum(x[0, j] for j in range(1, V_mac) if (0, j) in x) <= K)
    
    # Ràng buộc 3: Sync y_i^k và x_ij
    for (i, j) in x:
        if i > 0 and j < sink:
            for k in range(K):
                # Nếu x_ij = 1 thì y_i^k == y_j^k
                solver.Add(y[i, k] - y[j, k] <= 1 - x[i, j])
                solver.Add(y[j, k] - y[i, k] <= 1 - x[i, j])
                
    # Ràng buộc 4: Ghép cặp (Pairing & Precedence)
    for p_node, d_node in data.parcels:
        p = p_node.id
        d = d_node.id
        # Same vehicle
        for k in range(K):
            solver.Add(y[p, k] == y[d, k])
        # Precedence
        solver.Add(t[p] + p_node.duration + time_matrix[p][d] <= t[d])
        
    # Ràng buộc 5: Time Tracking (Big-M)
    M_T = 200000
    for (i, j) in x:
        if j < sink:
            dur_i = data.nodes[i].duration if i > 0 else 0
            solver.Add(t[i] + dur_i + time_matrix[i][j] <= t[j] + M_T * (1 - x[i, j]))
            
    # Ràng buộc 6: Capacity Tracking
    M_Q = max(data.capacities) + 50
    solver.Add(u[0] == 0)
    for (i, j) in x:
        if j < sink:
            w_j = data.nodes[j].weight
            solver.Add(u[i] + w_j <= u[j] + M_Q * (1 - x[i, j]))
            
    for i in range(1, V_mac):
        # Tải trọng không vượt quá sức chứa xe k nếu xe k phục vụ i
        solver.Add(u[i] <= sum(data.capacities[k] * y[i, k] for k in range(K)))
        
    # Hàm mục tiêu
    revenue_term = sum(data.nodes[i].revenue * sum(y[i, k] for k in range(K)) for i in range(1, V_mac))
    cost_term = sum(cost_matrix[i][j] * x[i, j] for (i, j) in x)
    
    solver.Maximize(revenue_term - cost_term)
    
    print("[*] Bắt đầu giải (SCIP)...")
    solver.SetTimeLimit(180000) # 3 phút
    
    start_time = time.time()
    status = solver.Solve()
    end_time = time.time()
    
    if status == pywraplp.Solver.OPTIMAL or status == pywraplp.Solver.FEASIBLE:
        print(f"\n=== MILP SCIP THÀNH CÔNG ===")
        print(f"Trạng thái: {'OPTIMAL' if status == pywraplp.Solver.OPTIMAL else 'FEASIBLE'}")
        print(f"Tổng Benefit: {solver.Objective().Value()}")
        print(f"Thời gian giải: {end_time - start_time:.2f}s")
    else:
        print(f"\n[!] SCIP KHÔNG TÌM ĐƯỢC NGHIỆM. Trạng thái: {status}")

if __name__ == '__main__':
    solve_milp(sys.argv[1])
