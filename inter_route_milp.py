import time
import random
import copy
from ortools.linear_solver import pywraplp

class SuperNode:
    def __init__(self, id_index, nodes, data):
        self.node_id = id_index
        self.nodes = nodes
        self.is_single = (len(nodes) == 1)
        self.weight = sum(n.weight for n in nodes)
        
        peak = 0
        current = 0
        for n in nodes:
            current += n.weight
            if current > peak: peak = current
        self.peak_weight = peak
        
        self.pickups = []
        self.dropoffs = []
        for n in nodes:
            if n.type == 'PARCEL_PICKUP':
                self.pickups.append(n.job_id)
            elif n.type == 'PARCEL_DROPOFF':
                self.dropoffs.append(n.job_id)
        
        if len(nodes) == 0:
            self.E, self.L, self.duration = 0, 999999, 0
        else:
            self.E = nodes[0].e
            self.L = nodes[0].l
            self.duration = nodes[0].duration
            for i in range(1, len(nodes)):
                t_ij = data.time_matrix[nodes[i-1].node_id][nodes[i].node_id]
                delta_t = self.duration + t_ij
                self.E = max(self.E, nodes[i].e - delta_t)
                self.L = min(self.L, nodes[i].l - delta_t)
                self.duration = delta_t + nodes[i].duration
                
        self.first_id = nodes[0].node_id if nodes else -1
        self.last_id = nodes[-1].node_id if nodes else -1
        
    def cost_to(self, other, data):
        if self.last_id == -1 or other.first_id == -1: return 0
        return data.cost_matrix[self.last_id][other.first_id]
        
    def time_to(self, other, data):
        if self.last_id == -1 or other.first_id == -1: return 0
        return data.time_matrix[self.last_id][other.first_id]

def _extract_supernodes(route_nodes, data, start_id, target_chunks):
    """
    Phân rã ngẫu nhiên bằng bài toán chia kẹo Euler (Stars and Bars).
    Đảm bảo hoàn toàn ngẫu nhiên và công bằng cho mọi vị trí trên xe.
    """
    n = len(route_nodes)
    if n <= 2:
        return []
        
    visits = route_nodes[1:-1]
    n_v = len(visits)
    
    if n_v <= target_chunks:
        return [SuperNode(start_id + i, [visits[i]], data) for i in range(n_v)]
        
    # Lấy k-1 vách ngăn ngẫu nhiên (k = target_chunks)
    splits = sorted(random.sample(range(1, n_v), target_chunks - 1))
    
    sizes = []
    prev = 0
    for split in splits:
        sizes.append(split - prev)
        prev = split
    sizes.append(n_v - prev)
    
    chunks = []
    idx = 0
    for s in sizes:
        chunks.append(visits[idx:idx+s])
        idx += s
        
    supernodes = []
    for i, c in enumerate(chunks):
        supernodes.append(SuperNode(start_id + i, c, data))
        
    return supernodes

def optimize_2_routes_milp(routeA, routeB, unserved_items, data, time_limit_ms=3000):
    n_A = max(0, len(routeA.nodes) - 2)
    n_B = max(0, len(routeB.nodes) - 2)
    n_total = n_A + n_B
    
    TARGET_V = 20
    
    if n_total == 0:
        return False, None, None
        
    if n_total <= TARGET_V:
        target_A = n_A
        target_B = n_B
    else:
        target_A = max(1, int(round(TARGET_V * (n_A / n_total))))
        target_B = TARGET_V - target_A
        
        if target_A > n_A:
            target_A = n_A
            target_B = TARGET_V - target_A
        elif target_B > n_B:
            target_B = n_B
            target_A = TARGET_V - target_B
            
    sn_A = _extract_supernodes(routeA.nodes, data, start_id=0, target_chunks=target_A)
    sn_B = _extract_supernodes(routeB.nodes, data, start_id=len(sn_A), target_chunks=target_B)
    
    max_sA = max([len(sn.nodes) for sn in sn_A]) if sn_A else 0
    max_sB = max([len(sn.nodes) for sn in sn_B]) if sn_B else 0
    print(f"    [DEBUG] Xe A tách thành {len(sn_A)} supernodes (Max size: {max_sA}) | Xe B tách thành {len(sn_B)} supernodes (Max size: {max_sB})")
    
    sn_U = []
    idx = len(sn_A) + len(sn_B)
    for item in unserved_items:
        if type(item) is tuple:
            sn_U.append(SuperNode(idx, [item[0]], data))
            sn_U.append(SuperNode(idx+1, [item[1]], data))
            idx += 2
        else:
            sn_U.append(SuperNode(idx, [item], data))
            idx += 1
            
    V = sn_A + sn_B + sn_U
    N = len(V)

    # Tan du code rac
    if N > 25:
        return False, None, None

    precedence = []
    for i in range(N):
        for drop_req in V[i].dropoffs:
            for j in range(N):
                if i == j: continue
                if drop_req in V[j].pickups:
                    precedence.append((j, i))

    solver = pywraplp.Solver.CreateSolver('SCIP')
    if not solver: return False, None, None
    solver.SetTimeLimit(time_limit_ms)

    START_A, END_A = N, N+1
    START_B, END_B = N+2, N+3
    
    x = {0: {}, 1: {}}
    
    T = {}
    L = {}
    for i in range(N):
        T[i] = solver.NumVar(V[i].E, V[i].L, f'T_{i}')
        L[i] = solver.NumVar(0, max(routeA.capacity, routeB.capacity), f'L_{i}')
        
    T[START_A] = solver.NumVar(routeA.nodes[0].e, routeA.nodes[0].l, 'T_SA')
    T[START_B] = solver.NumVar(routeB.nodes[0].e, routeB.nodes[0].l, 'T_SB')

    def add_arc(v, i, j, max_t=999999):
        if i not in x[v]: x[v][i] = {}
        x[v][i][j] = solver.IntVar(0, 1, f'x_{v}_{i}_{j}')
        
    all_nodes = list(range(N))
    for v, start, end in [(0, START_A, END_A), (1, START_B, END_B)]:
        for i in all_nodes + [start]: x[v][i] = {}
            
        for i in all_nodes:
            add_arc(v, start, i)
            add_arc(v, i, end)
            for j in all_nodes:
                if i != j: add_arc(v, i, j)
        add_arc(v, start, end)

    for v, start, end in [(0, START_A, END_A), (1, START_B, END_B)]:
        solver.Add(solver.Sum(x[v][start].values()) == 1)
        
        end_in = []
        for i in x[v]:
            if end in x[v][i]: end_in.append(x[v][i][end])
        solver.Add(solver.Sum(end_in) == 1)

    for i in range(N):
        in_edges = []
        out_edges = []
        for v in [0, 1]:
            for k in x[v]:
                if i in x[v][k]: in_edges.append(x[v][k][i])
            out_edges.extend(x[v][i].values())
            
            in_v = [x[v][k][i] for k in x[v] if i in x[v][k]]
            out_v = list(x[v][i].values())
            solver.Add(solver.Sum(in_v) == solver.Sum(out_v))
            
        solver.Add(solver.Sum(in_edges) == 1)
        solver.Add(solver.Sum(out_edges) == 1)

    Cap = {0: routeA.capacity, 1: routeB.capacity}
    DepotNode = {0: routeA.nodes[0], 1: routeB.nodes[0]}
    
    for v in [0, 1]:
        start = START_A if v == 0 else START_B
        
        for i in x[v]:
            for j in x[v][i]:
                if j == END_A or j == END_B: continue
                
                if i == start:
                    t_ij = data.time_matrix[DepotNode[v].node_id][V[j].first_id]
                    dur_i = 0
                    w_j = V[j].weight
                    peak_j = V[j].peak_weight
                    
                    M_time = DepotNode[v].l + t_ij - V[j].E
                    if M_time < 0: M_time = 0
                    solver.Add(T[j] >= T[start] + t_ij - M_time * (1 - x[v][i][j]))
                    
                    M_load = Cap[v]
                    solver.Add(L[j] >= w_j - M_load * (1 - x[v][i][j]))
                    solver.Add(L[j] <= w_j + M_load * (1 - x[v][i][j]))
                    solver.Add(w_j <= Cap[v] + M_load * (1 - x[v][i][j]))
                    solver.Add(peak_j <= Cap[v] + M_load * (1 - x[v][i][j])) 
                else:
                    t_ij = V[i].time_to(V[j], data)
                    dur_i = V[i].duration
                    w_j = V[j].weight
                    peak_j = V[j].peak_weight
                    
                    M_time = V[i].L + dur_i + t_ij - V[j].E
                    if M_time < 0: M_time = 0
                    solver.Add(T[j] >= T[i] + dur_i + t_ij - M_time * (1 - x[v][i][j]))
                    
                    M_load = Cap[v] * 2
                    solver.Add(L[j] >= L[i] + w_j - M_load * (1 - x[v][i][j]))
                    solver.Add(L[j] <= L[i] + w_j + M_load * (1 - x[v][i][j]))
                    solver.Add(L[i] + peak_j <= Cap[v] + M_load * (1 - x[v][i][j]))

    for p_idx, d_idx in precedence:
        t_PD = V[p_idx].time_to(V[d_idx], data)
        solver.Add(T[d_idx] >= T[p_idx] + V[p_idx].duration + t_PD)
        
        for v in [0, 1]:
            in_p = [x[v][k][p_idx] for k in x[v] if p_idx in x[v][k]]
            in_d = [x[v][k][d_idx] for k in x[v] if d_idx in x[v][k]]
            solver.Add(solver.Sum(in_p) == solver.Sum(in_d))

    objective = solver.Objective()
    for v in [0, 1]:
        start, end = (START_A, END_A) if v == 0 else (START_B, END_B)
        for i in x[v]:
            for j in x[v][i]:
                if j == end: cost = 0
                elif i == start: cost = data.cost_matrix[DepotNode[v].node_id][V[j].first_id]
                else: cost = V[i].cost_to(V[j], data)
                objective.SetCoefficient(x[v][i][j], float(cost))
                
    objective.SetMinimization()

    status = solver.Solve()
    
    if status == pywraplp.Solver.OPTIMAL or status == pywraplp.Solver.FEASIBLE:
        # Tái tạo 2 route
        new_routes = []
        for v, start, end, route_obj in [(0, START_A, END_A, routeA), (1, START_B, END_B, routeB)]:
            curr = start
            new_nodes = [route_obj.nodes[0]]
            
            while True:
                next_node = -1
                for j in x[v][curr]:
                    if x[v][curr][j].solution_value() > 0.5:
                        next_node = j
                        break
                
                if next_node == -1 or next_node == end:
                    break
                    
                new_nodes.extend(V[next_node].nodes)
                curr = next_node
                
            new_nodes.append(route_obj.nodes[-1])
            new_routes.append(new_nodes)
            
        rA_test = copy.copy(routeA)
        rA_test.nodes = new_routes[0]
        rA_test.update_states(data)
        
        rB_test = copy.copy(routeB)
        rB_test.nodes = new_routes[1]
        rB_test.update_states(data)
        
        # Valid độc lập
        if rA_test.is_feasible and rB_test.is_feasible:
            # So sánh tổng Benefit. Nếu tổng Lợi nhuận mới > cũ thì chấp nhận!
            old_ben = routeA.total_benefit + routeB.total_benefit
            new_ben = rA_test.total_benefit + rB_test.total_benefit
                    
            if new_ben > old_ben + 1e-4:
                routeA.nodes = rA_test.nodes
                routeA.update_states(data)
                routeB.nodes = rB_test.nodes
                routeB.update_states(data)
                return True, routeA, routeB
                
    return False, None, None
