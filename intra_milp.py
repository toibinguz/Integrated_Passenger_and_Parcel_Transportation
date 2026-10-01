import time
import random
import copy
from ortools.linear_solver import pywraplp

# --- CÁC THAM SỐ ĐIỀU KHIỂN (CONTROL PARAMETERS) ---
MAX_EXACT_NODES = 25       # Kích thước tối đa để giải chính xác toàn tuyến
SLIDING_WINDOW_MIN = 15    # Kích thước cửa sổ trượt nhỏ nhất
SLIDING_WINDOW_MAX = 20    # Kích thước cửa sổ trượt lớn nhất
MAX_ATTEMPTS = 15          # Số lần gieo xúc xắc cắt cửa sổ trượt tối đa
EARLY_STOP_STREAK = 3      # Số lần liên tiếp không cải thiện thì dừng sớm

def _optimize_exact(nodes, data, capacity, initial_load, time_limit_ms=2000):
    n = len(nodes)
    if n <= 3: 
        return False, nodes

    V = nodes
    START = 0
    END = n - 1

    solver = pywraplp.Solver.CreateSolver('SCIP')
    if not solver:
        return False, nodes

    solver.SetTimeLimit(time_limit_ms)

    x = {}
    Q = capacity

    T = {}
    for i in range(n):
        T[i] = solver.NumVar(V[i].e, V[i].l, f'T_{i}')

    L = {}
    for i in range(n):
        L[i] = solver.NumVar(-Q, Q, f'L_{i}')

    edge_count = 0
    for i in range(n):
        if i == END: continue
        x[i] = {}
        for j in range(n):
            if j == START: continue
            if i == j: continue

            arr_time_min = V[i].e + V[i].duration + data.time_matrix[V[i].node_id][V[j].node_id]
            if arr_time_min > V[j].l:
                continue 

            if V[i].type.startswith('PARCEL_DROPOFF') and V[j].type.startswith('PARCEL_PICKUP'):
                if V[i].job_id == V[j].job_id:
                    continue

            x[i][j] = solver.IntVar(0, 1, f'x_{i}_{j}')
            edge_count += 1

    solver.Add(solver.Sum([x[START][j] for j in x[START]]) == 1)
    
    end_in_edges = [x[i][END] for i in range(n) if i in x and END in x[i]]
    solver.Add(solver.Sum(end_in_edges) == 1)

    for i in range(1, n - 1):
        in_edges = [x[k][i] for k in range(n) if k in x and i in x[k]]
        solver.Add(solver.Sum(in_edges) == 1)
        
        out_edges = [x[i][j] for j in x[i]]
        solver.Add(solver.Sum(out_edges) == 1)

    for i in range(n):
        if i not in x: continue
        for j in x[i]:
            t_ij = data.time_matrix[V[i].node_id][V[j].node_id]
            dur_i = V[i].duration
            
            M_ij = V[i].l + dur_i + t_ij - V[j].e
            if M_ij < 0: M_ij = 0 
            
            solver.Add(T[j] >= T[i] + dur_i + t_ij - M_ij * (1 - x[i][j]))

    solver.Add(L[START] == initial_load)
    
    for i in range(n):
        if i not in x: continue
        for j in x[i]:
            w_j = V[j].weight
            M_load = Q * 2
            solver.Add(L[j] >= L[i] + w_j - M_load * (1 - x[i][j]))
            solver.Add(L[j] <= L[i] + w_j + M_load * (1 - x[i][j]))
            
    for i in range(n):
        solver.Add(L[i] <= Q)
        solver.Add(L[i] >= 0)

    pickup_nodes = {v.job_id: idx for idx, v in enumerate(V) if v.type == 'PARCEL_PICKUP'}
    dropoff_nodes = {v.job_id: idx for idx, v in enumerate(V) if v.type == 'PARCEL_DROPOFF'}
    
    for job_id in pickup_nodes:
        if job_id in dropoff_nodes:
            p_idx = pickup_nodes[job_id]
            d_idx = dropoff_nodes[job_id]
            t_PD = data.time_matrix[V[p_idx].node_id][V[d_idx].node_id]
            solver.Add(T[d_idx] >= T[p_idx] + V[p_idx].duration + t_PD)

    objective = solver.Objective()
    for i in range(n):
        if i not in x: continue
        for j in x[i]:
            if V[j].type == 'DEPOT':
                cost = 0
            else:
                cost = data.cost_matrix[V[i].node_id][V[j].node_id]
            objective.SetCoefficient(x[i][j], float(cost))
            
    objective.SetMinimization()

    status = solver.Solve()

    if status == pywraplp.Solver.OPTIMAL or status == pywraplp.Solver.FEASIBLE:
        new_nodes = []
        curr = START
        
        while True:
            new_nodes.append(V[curr])
            if curr == END:
                break
                
            next_node = -1
            for j in x[curr]:
                if x[curr][j].solution_value() > 0.5:
                    next_node = j
                    break
                    
            if next_node == -1:
                return False, nodes
                
            curr = next_node
            
        old_cost = 0
        for i in range(1, n):
            prev = nodes[i-1]
            cur = nodes[i]
            if cur.type != 'DEPOT':
                old_cost += data.cost_matrix[prev.node_id][cur.node_id]
                
        new_cost = 0
        for i in range(1, n):
            prev = new_nodes[i-1]
            cur = new_nodes[i]
            if cur.type != 'DEPOT':
                new_cost += data.cost_matrix[prev.node_id][cur.node_id]
                
        if new_cost < old_cost - 1e-4:
            return True, new_nodes
            
    return False, nodes


def optimize_route_milp(route, data, time_limit_ms=2000):
    """
    Sử dụng MILP (SCIP) để tìm chuỗi đi qua các node hiện tại của route sao cho tối ưu nhất.
    """
    nodes = route.nodes
    n = len(nodes)
    
    if n <= 3:
        return False, nodes
        
    if n <= MAX_EXACT_NODES:
        return _optimize_exact(nodes, data, route.capacity, 0, time_limit_ms)
        
    improved_total = False
    current_nodes = list(nodes)
    capacity = route.capacity
    
    no_improve_streak = 0
    
    for attempt in range(MAX_ATTEMPTS):
        W = random.randint(SLIDING_WINDOW_MIN, SLIDING_WINDOW_MAX)
        if W >= len(current_nodes) - 2:
            W = len(current_nodes) - 2
            
        # [0] là Depot đầu, [N-1] là Depot cuối
        # Chọn start_idx sao cho chunk có đủ chỗ cho Depot giả (start_idx-1) và Depot giả kết (start_idx+W)
        start_idx = random.randint(1, len(current_nodes) - 1 - W)
        
        # Đảm bảo KHÔNG cắt đôi cặp Parcel đang nằm sát nhau
        # Thử dời start_idx hoặc thu hẹp/mở rộng cửa sổ một chút
        valid_chunk = True
        chunk_reqs = set()
        for i in range(start_idx, start_idx + W):
            if current_nodes[i].type != 'PASSENGER' and current_nodes[i].type != 'DEPOT':
                chunk_reqs.add(current_nodes[i].job_id)
                
        for job_id in chunk_reqs:
            p_idx = -1
            d_idx = -1
            for i in range(len(current_nodes)):
                if current_nodes[i].job_id == job_id:
                    if current_nodes[i].type == 'PARCEL_PICKUP': p_idx = i
                    elif current_nodes[i].type == 'PARCEL_DROPOFF': d_idx = i
            
            # Nếu một trong hai điểm nằm ngoài cửa sổ -> Có nguy cơ MILP xếp sai thứ tự (vì thiếu 1 nửa)
            # Dù có Start_Depot và End_Depot ép thứ tự cục bộ, ta tạm bỏ qua chunk này để an toàn 100%
            if p_idx < start_idx or p_idx >= start_idx + W or d_idx < start_idx or d_idx >= start_idx + W:
                valid_chunk = False
                break
                
        if not valid_chunk:
            continue
            
        virtual_start = current_nodes[start_idx - 1]
        visits = current_nodes[start_idx : start_idx + W]
        virtual_end = current_nodes[start_idx + W]
        
        chunk = [virtual_start] + visits + [virtual_end]
        
        # Tính Tải khởi điểm tại virtual_start
        initial_load = 0
        for i in range(1, start_idx):
            initial_load += current_nodes[i].weight
            
        improved, new_chunk = _optimize_exact(chunk, data, capacity, initial_load, time_limit_ms)
        
        if improved:
            test_nodes = current_nodes[:start_idx - 1] + new_chunk + current_nodes[start_idx + W + 1:]
            
            # Xác thực chéo (Cross-Validation) bằng Evaluator chuẩn
            test_route = copy.copy(route)
            test_route.nodes = test_nodes
            test_route.update_states(data)
            
            if test_route.is_feasible and test_route.total_benefit > route.total_benefit:
                route.nodes = test_nodes
                route.update_states(data)
                current_nodes = list(test_nodes)
                improved_total = True
                no_improve_streak = 0
                continue
                
        no_improve_streak += 1
        if no_improve_streak >= EARLY_STOP_STREAK:
            break
            
    if improved_total:
        return True, route.nodes
    else:
        return False, nodes
