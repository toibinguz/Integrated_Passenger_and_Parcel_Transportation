import sys
import math
import random
import time
import copy
from validator import validate_tour

import random

SESSION_PREFIX = random.getrandbits(32) << 32
ROUTE_COUNTER = 0

def get_next_version():
    global ROUTE_COUNTER
    ROUTE_COUNTER += 1
    return hex(SESSION_PREFIX | ROUTE_COUNTER)[2:]

class GenericNode:
    def __init__(self, node_id, type, req_id, revenue, e, l, duration, weight):
        self.id = node_id
        self.type = type
        self.request_id = req_id
        self.revenue = revenue
        self.e = e
        self.l = l
        self.duration = duration
        self.weight = weight
        # Để mapping ma trận gốc (Tùy chọn, dùng để validate)
        self.physical_in = -1
        self.physical_out = -1
        # Chi phí nội tuyến: với Passenger = cost(pickup→dropoff), với Parcel/Depot = 0
        self.int_cost = 0

class Data:
    def __init__(self, filename):
        self.filename = filename
        with open(filename, 'r') as f:
            lines = [l.strip() for l in f.readlines() if l.strip()]
        
        tokens = []
        for line in lines:
            tokens.extend(line.split())
            
        self.K = int(tokens[0])
        self.N = int(tokens[1])
        self.M = int(tokens[2])
        V = 2*self.N + 2*self.M + self.K
        
        idx = 3
        self.capacities = []
        for _ in range(self.K):
            self.capacities.append(int(tokens[idx]))
            idx += 1
            
        # Đọc tạm vào mảng trung gian
        passengers_raw = []
        for i in range(self.N):
            T = int(tokens[idx]); E = int(tokens[idx+1]); L = int(tokens[idx+2]); S = int(tokens[idx+3])
            passengers_raw.append((T, E, L, S))
            idx += 4
            
        parcels_raw = []
        for j in range(self.M):
            T = int(tokens[idx]); Ep = int(tokens[idx+1]); Lp = int(tokens[idx+2])
            Ed = int(tokens[idx+3]); Ld = int(tokens[idx+4]); S = int(tokens[idx+5]); w = int(tokens[idx+6])
            parcels_raw.append((T, Ep, Lp, Ed, Ld, S, w))
            idx += 7
            
        orig_time = []
        for _ in range(V):
            orig_time.append([int(x) for x in tokens[idx:idx+V]])
            idx += V
            
        orig_cost = []
        for _ in range(V):
            orig_cost.append([int(x) for x in tokens[idx:idx+V]])
            idx += V

        # --- CHUẨN HÓA SANG GENERIC NODE ---
        self.nodes = []
        self.passengers = []
        self.parcels = []
        self.depots = []
        
        macro_id = 0
        
        # 1. Depot Nodes (K nodes cho K xe)
        for i in range(self.K):
            depot = GenericNode(macro_id, 'DEPOT', i, 0, 0, 1000000, 0, 0)
            depot.physical_in = i; depot.physical_out = i
            self.nodes.append(depot)
            self.depots.append(depot)
            macro_id += 1
        
        # 2. Hành khách
        for i, (rev, E, L, S) in enumerate(passengers_raw):
            in_idx = self.K + i
            out_idx = self.K + self.N + i
            
            # Gộp cost(P, D) vào Revenue
            adj_rev = rev - orig_cost[in_idx][out_idx]
            
            # GARBAGE COLLECTOR: Bỏ qua khách hàng tự thân lỗ vốn
            # adj_rev = T - cost_nội_tuyến — nếu âm thì cước không đủ trả xăng nội tuyến
            if adj_rev < 0:
                continue
                
            dur = 2*S + orig_time[in_idx][out_idx]
            # Dùng revenue gốc (T), int_cost sẽ được cộng vào total_cost riêng trong update_states
            p_node = GenericNode(macro_id, 'PASSENGER', i, rev, E, L, dur, 0)
            p_node.physical_in = in_idx; p_node.physical_out = out_idx
            p_node.int_cost = orig_cost[in_idx][out_idx]
            
            self.nodes.append(p_node)
            self.passengers.append(p_node)
            macro_id += 1
            
        # 3. Hàng hóa
        for j, (rev, Ep, Lp, Ed, Ld, S, w) in enumerate(parcels_raw):
            p_idx = self.K + 2*self.N + j
            d_idx = self.K + 2*self.N + self.M + j
            
            pick = GenericNode(macro_id, 'PARCEL_PICKUP', j, 0, Ep, Lp, S, w)
            pick.physical_in = p_idx; pick.physical_out = p_idx
            self.nodes.append(pick)
            macro_id += 1
            
            drop = GenericNode(macro_id, 'PARCEL_DROPOFF', j, rev, Ed, Ld, S, -w)
            drop.physical_in = d_idx; drop.physical_out = d_idx
            self.nodes.append(drop)
            macro_id += 1
            
            self.parcels.append((pick, drop))
            
        # 4. Trích xuất Ma trận T' và C'
        self.V_mac = len(self.nodes)
        self.time_matrix = [[0]*self.V_mac for _ in range(self.V_mac)]
        self.cost_matrix = [[0]*self.V_mac for _ in range(self.V_mac)]
        
        for u in range(self.V_mac):
            for v in range(self.V_mac):
                p_out = self.nodes[u].physical_out
                p_in = self.nodes[v].physical_in
                
                self.cost_matrix[u][v] = orig_cost[p_out][p_in]
                self.time_matrix[u][v] = orig_time[p_out][p_in]

class Route:
    def __init__(self, vehicle_id, capacity, depot_node):
        
        self.vehicle_id = vehicle_id
        self.capacity = capacity
        # Luôn bắt đầu và kết thúc tại Depot
        self.nodes = [depot_node, depot_node]
        self.is_feasible = True
        self.total_benefit = 0
        self.total_cost = 0
        
        # State arrays
        self.arr_time = []
        self.dep_time = []
        self.wait_time = []
        self.max_delay = []
        self.current_load = []
        
        
        self.version = get_next_version()
        
        self.update_states(None)
        
    def clone(self):
        new_r = Route(self.vehicle_id, self.capacity, self.nodes[0])
        new_r.nodes = list(self.nodes)
        new_r.is_feasible = self.is_feasible
        new_r.total_benefit = self.total_benefit
        new_r.total_cost = self.total_cost
        
        new_r.arr_time = list(self.arr_time)
        new_r.dep_time = list(self.dep_time)
        new_r.wait_time = list(self.wait_time)
        new_r.max_delay = list(self.max_delay)
        new_r.current_load = list(self.current_load)
        
        new_r.version = self.version
        return new_r
        
    def update_states(self, data):
        self.version = get_next_version()
        n = len(self.nodes)
        self.arr_time = [0] * n
        self.dep_time = [0] * n
        self.wait_time = [0] * n
        self.max_delay = [0] * n
        self.current_load = [0] * n
        self.is_feasible = True
        
        if data is None: return
        
        self.total_cost = 0
        self.total_benefit = 0
        load = 0
        
        for i in range(1, n):
            prev = self.nodes[i-1]
            curr = self.nodes[i]
            
            # Open VRP: không tính cost chặng về Depot
            if curr.type != 'DEPOT':
                self.total_cost += data.cost_matrix[prev.id][curr.id]
                # Validator cộng thêm internal cost cho Passenger (cost đi từ điểm đón đến điểm trả)
                self.total_cost += curr.int_cost
            self.total_benefit += curr.revenue
            
            # Time
            arr = self.dep_time[i-1] + data.time_matrix[prev.id][curr.id]
            self.arr_time[i] = arr
            self.wait_time[i] = max(0, curr.e - arr)
            start = max(arr, curr.e)
            
            if start > curr.l:
                self.is_feasible = False
                
            self.dep_time[i] = start + curr.duration
            
            # Load
            load += curr.weight
            self.current_load[i] = load
            if load > self.capacity or load < 0:
                self.is_feasible = False
                
        self.total_benefit -= self.total_cost

        
        # Tính max_delay (O(N) Backward)
        self.max_delay[n-1] = self.nodes[n-1].l - max(self.arr_time[n-1], self.nodes[n-1].e)
        for i in range(n-2, -1, -1):
            start = max(self.arr_time[i], self.nodes[i].e)
            limit1 = self.nodes[i].l - start
            limit2 = self.wait_time[i+1] + self.max_delay[i+1]
            self.max_delay[i] = min(limit1, limit2)

class Evaluator:
    @staticmethod
    def evaluate_passenger(route, p_node, data, tabu_edges=None):
        best_benefit = -999999
        best_pos = -1
        
        n = len(route.nodes)
        for i in range(1, n):
            prev = route.nodes[i-1]
            next_node = route.nodes[i]
            
            arr_p = max(p_node.e, route.dep_time[i-1] + data.time_matrix[prev.id][p_node.id])
            if arr_p > p_node.l: continue
            
            dep_p = arr_p + p_node.duration
            arr_next = dep_p + data.time_matrix[p_node.id][next_node.id]
            if tabu_edges and ((prev.id, p_node.id) in tabu_edges or (p_node.id, next_node.id) in tabu_edges): continue
            delta_time = arr_next - route.arr_time[i]
            
            # O(1) Constraint check
            if delta_time > route.wait_time[i] + route.max_delay[i]: continue
            if route.capacity < 0: continue # Passenger weight=0, load unaffected
            
            c_prev_p = data.cost_matrix[prev.id][p_node.id]
            # Open VRP: không tính cost kết nối về Depot
            if next_node.type == 'DEPOT':
                c_p_next = 0
                c_prev_next = 0
            else:
                c_p_next = data.cost_matrix[p_node.id][next_node.id]
                c_prev_next = data.cost_matrix[prev.id][next_node.id]
            
            delta_cost = c_prev_p + c_p_next - c_prev_next + p_node.int_cost
            delta_benefit = p_node.revenue - delta_cost
            
            if delta_benefit > best_benefit:
                best_benefit = delta_benefit
                best_pos = i
                
        return best_benefit, best_pos

    @staticmethod
    def evaluate_parcel(route, pick, drop, data, tabu_edges=None):
        best_benefit = -999999
        best_p_pos = -1
        best_d_pos = -1
        
        n = len(route.nodes)
        for i in range(1, n):
            prev_i = route.nodes[i-1]
            arr_p = max(pick.e, route.dep_time[i-1] + data.time_matrix[prev_i.id][pick.id])
            if arr_p > pick.l: continue
            
            # Khởi tạo trạng thái ban đầu cho nhánh j (khi j = i)
            curr_dep = arr_p + pick.duration
            curr_node = pick
            sim_cost = data.cost_matrix[prev_i.id][pick.id]
            orig_cost = 0
            
            # Kiểm tra xem có vi phạm sức chứa ngay tại i-1 không
            if route.current_load[i-1] + pick.weight > route.capacity:
                continue
                
            time_valid = True
            
            for j in range(i, n):
                if not time_valid: break
                
                # Check capacity tại j-1
                if route.current_load[j-1] + pick.weight > route.capacity:
                    break # Từ đây trở đi load sẽ luôn bị cộng thêm, nên break
                    
                # 1. Tại bước j này, test thử gắn Drop vào sau curr_node
                arr_d = max(drop.e, curr_dep + data.time_matrix[curr_node.id][drop.id])
                if arr_d <= drop.l:
                    sim_cost_drop = sim_cost + data.cost_matrix[curr_node.id][drop.id]
                    dep_d = arr_d + drop.duration
                    
                    next_j = route.nodes[j]
                    arr_next = dep_d + data.time_matrix[drop.id][next_j.id]
                    delta_time = arr_next - route.arr_time[j]
                    
                    if delta_time <= route.wait_time[j] + route.max_delay[j]:
                        sim_cost_full = sim_cost_drop
                        if next_j.type != 'DEPOT':
                            sim_cost_full += data.cost_matrix[drop.id][next_j.id]
                            
                        # Tính orig_cost cho việc thay thế
                        # orig_cost hiện tại đang tích lũy chi phí từ i-1 đến j-1 (nếu j>i)
                        # Cần cộng thêm mắt xích bị cắt đứt là (j-1)->j
                        orig_cost_full = orig_cost
                        if i == j:
                            if next_j.type != 'DEPOT':
                                orig_cost_full = data.cost_matrix[prev_i.id][next_j.id]
                            else:
                                orig_cost_full = 0
                        else:
                            if next_j.type != 'DEPOT':
                                orig_cost_full += data.cost_matrix[route.nodes[j-1].id][next_j.id]
                                
                        delta_cost = sim_cost_full - orig_cost_full
                        delta_benefit = drop.revenue - delta_cost
                        
                        is_tabu = False
                        if tabu_edges:
                            p_prev_req = prev_i.id
                            p_next_req = drop.id if j == i else route.nodes[i].id
                            d_prev_req = pick.id if j == i else route.nodes[j-1].id
                            d_next_req = next_j.id
                            
                            is_tabu = (
                                (p_prev_req, pick.id) in tabu_edges or
                                (pick.id, p_next_req) in tabu_edges or
                                (d_prev_req, drop.id) in tabu_edges or
                                (drop.id, d_next_req) in tabu_edges
                            )
                            
                        if not is_tabu and delta_benefit > best_benefit:
                            best_benefit = delta_benefit
                            best_p_pos = i
                            best_d_pos = j
                            
                # 2. Cập nhật trạng thái để chuyển sang j+1 (đẩy node j vào chuỗi giữa pick và drop)
                node_j = route.nodes[j]
                
                # Cập nhật orig_cost: cộng thêm cạnh nối vào node_j
                if j == i:
                    orig_cost += data.cost_matrix[prev_i.id][node_j.id]
                else:
                    orig_cost += data.cost_matrix[route.nodes[j-1].id][node_j.id]
                    
                arr = max(node_j.e, curr_dep + data.time_matrix[curr_node.id][node_j.id])
                if arr > node_j.l:
                    time_valid = False
                    break
                    
                sim_cost += data.cost_matrix[curr_node.id][node_j.id]
                curr_dep = arr + node_j.duration
                curr_node = node_j

        return best_benefit, best_p_pos, best_d_pos

