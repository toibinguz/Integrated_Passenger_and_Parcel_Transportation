import sys
import math
import random
import time
import copy

RELEASE_MODE = True




SESSION_PREFIX = random.getrandbits(32) << 32
ROUTE_COUNTER = 0

def get_next_ver_id():
    global ROUTE_COUNTER
    ROUTE_COUNTER += 1
    return hex(SESSION_PREFIX | ROUTE_COUNTER)[2:]

class GenericNode:
    def __init__(self, node_id, type, job_id, revenue, e, l, duration, weight):
        self.node_id = node_id
        self.type = type
        self.job_id = job_id
        self.revenue = revenue
        self.e = e
        self.l = l
        self.duration = duration
        self.weight = weight
        #  mapping ma trn gc (Ty chn, dng  validate)
        self.physical_in = -1
        self.physical_out = -1
        # Chi ph ni tuyn: vi Passenger = cost(pickupdropoff), vi Parcel/Depot = 0
        self.int_cost = 0

class Data:
    def __init__(self, filename=None):
        self.filename = filename
        import sys
        if globals().get('RELEASE_MODE', False):
            lines = [l.strip() for l in sys.stdin.read().splitlines() if l.strip()]
        else:
            with open(filename, 'r') as f:
                lines = [l.strip() for l in f.readlines() if l.strip()]
        
        tokens = []
        for line in lines:
            if not line.startswith('#'):
                tokens.extend(line.split())
            
        self.K = int(tokens[0])
        self.N = int(tokens[1])
        self.M = int(tokens[2])
        V = 2*self.N + 2*self.M + self.K
        
        idx = 3
        self.capacities = []
        for _ in range(self.K):
            o_k = int(tokens[idx])
            q_k = int(tokens[idx+1])
            self.capacities.append(q_k)
            idx += 2
            
        passengers_raw = []
        for i in range(self.N):
            P_i = int(tokens[idx])
            D_i = int(tokens[idx+1])
            E = int(tokens[idx+2])
            L = int(tokens[idx+3])
            S = int(tokens[idx+4])
            T = int(tokens[idx+5])
            passengers_raw.append((T, E, L, S, P_i - 1, D_i - 1))
            idx += 6
            
        parcels_raw = []
        for j in range(self.M):
            P_j = int(tokens[idx])
            D_j = int(tokens[idx+1])
            w = int(tokens[idx+2])
            Ep = int(tokens[idx+3])
            Lp = int(tokens[idx+4])
            Ed = int(tokens[idx+5])
            Ld = int(tokens[idx+6])
            S = int(tokens[idx+7])
            T = int(tokens[idx+8])
            parcels_raw.append((T, Ep, Lp, Ed, Ld, S, w, P_j - 1, D_j - 1))
            idx += 9
            
        orig_time = []
        for _ in range(V):
            orig_time.append([int(x) for x in tokens[idx:idx+V]])
            idx += V
            
        orig_cost = []
        for _ in range(V):
            orig_cost.append([int(x) for x in tokens[idx:idx+V]])
            idx += V

        # --- CHUN HA SANG GENERIC NODE ---
        self.nodes = []
        self.passengers = []
        self.parcels = []
        self.depots = []
        
        macro_id = 0
        
        # 1. Depot Nodes (K nodes cho K xe)
        for i in range(self.K):
            depot = GenericNode(macro_id, 'DEPOT', self.N + self.M + i, 0, 0, 1000000, 0, 0)
            depot.physical_in = i; depot.physical_out = i
            self.nodes.append(depot)
            self.depots.append(depot)
            macro_id += 1
        
        # 2. Hnh khch
        for i, (rev, E, L, S, in_idx, out_idx) in enumerate(passengers_raw):
            
            # Gp cost(P, D) vo Revenue
            adj_rev = rev - orig_cost[in_idx][out_idx]
            
            # GARBAGE COLLECTOR: B qua khch hng t thn l vn
            # adj_rev = T - cost_ni_tuyn  nu m th cc khng  tr xng ni tuyn
            if adj_rev < 0:
                continue
                
            dur = 2*S + orig_time[in_idx][out_idx]
            # Dng revenue gc (T), int_cost s c cng vo total_cost ring trong update_states
            p_node = GenericNode(macro_id, 'PASSENGER', i, rev, E, L, dur, 0)
            p_node.physical_in = in_idx; p_node.physical_out = out_idx
            p_node.int_cost = orig_cost[in_idx][out_idx]
            
            self.nodes.append(p_node)
            self.passengers.append(p_node)
            macro_id += 1
            
        # 3. Hng ha
        for j, (rev, Ep, Lp, Ed, Ld, S, w, p_idx, d_idx) in enumerate(parcels_raw):
            
            pick = GenericNode(macro_id, 'PARCEL_PICKUP', self.N + j, 0, Ep, Lp, S, w)
            pick.physical_in = p_idx; pick.physical_out = p_idx
            self.nodes.append(pick)
            macro_id += 1
            
            drop = GenericNode(macro_id, 'PARCEL_DROPOFF', self.N + j, rev, Ed, Ld, S, -w)
            drop.physical_in = d_idx; drop.physical_out = d_idx
            self.nodes.append(drop)
            macro_id += 1
            
            self.parcels.append((pick, drop))
            
        # 4. Trch xut Ma trn T' v C'
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
    def __init__(self, veh_id, capacity, depot_node):
        
        self.veh_id = veh_id
        self.capacity = capacity
        # Lun bt u v kt thc ti Depot
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
        
        
        self.ver_id = get_next_ver_id()
        
        self.update_states(None)
        
    def clone(self):
        new_r = Route(self.veh_id, self.capacity, self.nodes[0])
        new_r.nodes = list(self.nodes)
        new_r.is_feasible = self.is_feasible
        new_r.total_benefit = self.total_benefit
        new_r.total_cost = self.total_cost
        
        new_r.arr_time = list(self.arr_time)
        new_r.dep_time = list(self.dep_time)
        new_r.wait_time = list(self.wait_time)
        new_r.max_delay = list(self.max_delay)
        new_r.current_load = list(self.current_load)
        
        new_r.ver_id = self.ver_id
        return new_r
        
    def update_states(self, data):
        self.ver_id = get_next_ver_id()
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
            
            # Open VRP: khng tnh cost chng v Depot
            if curr.type != 'DEPOT':
                self.total_cost += data.cost_matrix[prev.node_id][curr.node_id]
                # Validator cng thm internal cost cho Passenger (cost i t im n n im tr)
                self.total_cost += curr.int_cost
            self.total_benefit += curr.revenue
            
            # Time
            arr = self.dep_time[i-1] + data.time_matrix[prev.node_id][curr.node_id]
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

        
        # Tnh max_delay (O(N) Backward)
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
            
            arr_p = max(p_node.e, route.dep_time[i-1] + data.time_matrix[prev.node_id][p_node.node_id])
            if arr_p > p_node.l: continue
            
            dep_p = arr_p + p_node.duration
            arr_next = dep_p + data.time_matrix[p_node.node_id][next_node.node_id]
            if tabu_edges and ((prev.node_id, p_node.node_id) in tabu_edges or (p_node.node_id, next_node.node_id) in tabu_edges): continue
            delta_time = arr_next - route.arr_time[i]
            
            # O(1) Constraint check
            if delta_time > route.wait_time[i] + route.max_delay[i]: continue
            if route.capacity < 0: continue # Passenger weight=0, load unaffected
            
            c_prev_p = data.cost_matrix[prev.node_id][p_node.node_id]
            # Open VRP: khng tnh cost kt ni v Depot
            if next_node.type == 'DEPOT':
                c_p_next = 0
                c_prev_next = 0
            else:
                c_p_next = data.cost_matrix[p_node.node_id][next_node.node_id]
                c_prev_next = data.cost_matrix[prev.node_id][next_node.node_id]
            
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
            arr_p = max(pick.e, route.dep_time[i-1] + data.time_matrix[prev_i.node_id][pick.node_id])
            if arr_p > pick.l: continue
            
            # Khi to trng thi ban u cho nhnh j (khi j = i)
            curr_dep = arr_p + pick.duration
            curr_node = pick
            sim_cost = data.cost_matrix[prev_i.node_id][pick.node_id]
            orig_cost = 0
            
            # Kim tra xem c vi phm sc cha ngay ti i-1 khng
            if route.current_load[i-1] + pick.weight > route.capacity:
                continue
                
            time_valid = True
            
            for j in range(i, n):
                if not time_valid: break
                
                # Check capacity ti j-1
                if route.current_load[j-1] + pick.weight > route.capacity:
                    break # T y tr i load s lun b cng thm, nn break
                    
                # 1. Ti bc j ny, test th gn Drop vo sau curr_node
                arr_d = max(drop.e, curr_dep + data.time_matrix[curr_node.node_id][drop.node_id])
                if arr_d <= drop.l:
                    sim_cost_drop = sim_cost + data.cost_matrix[curr_node.node_id][drop.node_id]
                    dep_d = arr_d + drop.duration
                    
                    next_j = route.nodes[j]
                    arr_next = dep_d + data.time_matrix[drop.node_id][next_j.node_id]
                    delta_time = arr_next - route.arr_time[j]
                    
                    if delta_time <= route.wait_time[j] + route.max_delay[j]:
                        sim_cost_full = sim_cost_drop
                        if next_j.type != 'DEPOT':
                            sim_cost_full += data.cost_matrix[drop.node_id][next_j.node_id]
                            
                        # Tnh orig_cost cho vic thay th
                        # orig_cost hin ti ang tch ly chi ph t i-1 n j-1 (nu j>i)
                        # Cn cng thm mt xch b ct t l (j-1)->j
                        orig_cost_full = orig_cost
                        if i == j:
                            if next_j.type != 'DEPOT':
                                orig_cost_full = data.cost_matrix[prev_i.node_id][next_j.node_id]
                            else:
                                orig_cost_full = 0
                        else:
                            if next_j.type != 'DEPOT':
                                orig_cost_full += data.cost_matrix[route.nodes[j-1].node_id][next_j.node_id]
                                
                        delta_cost = sim_cost_full - orig_cost_full
                        delta_benefit = drop.revenue - delta_cost
                        
                        is_tabu = False
                        if tabu_edges:
                            p_prev_req = prev_i.node_id
                            p_next_req = drop.node_id if j == i else route.nodes[i].node_id
                            d_prev_req = pick.node_id if j == i else route.nodes[j-1].node_id
                            d_next_req = next_j.node_id
                            
                            is_tabu = (
                                (p_prev_req, pick.node_id) in tabu_edges or
                                (pick.node_id, p_next_req) in tabu_edges or
                                (d_prev_req, drop.node_id) in tabu_edges or
                                (drop.node_id, d_next_req) in tabu_edges
                            )
                            
                        if not is_tabu and delta_benefit > best_benefit:
                            best_benefit = delta_benefit
                            best_p_pos = i
                            best_d_pos = j
                            
                # 2. Cp nht trng thi  chuyn sang j+1 (y node j vo chui gia pick v drop)
                node_j = route.nodes[j]
                
                # Cp nht orig_cost: cng thm cnh ni vo node_j
                if j == i:
                    orig_cost += data.cost_matrix[prev_i.node_id][node_j.node_id]
                else:
                    orig_cost += data.cost_matrix[route.nodes[j-1].node_id][node_j.node_id]
                    
                arr = max(node_j.e, curr_dep + data.time_matrix[curr_node.node_id][node_j.node_id])
                if arr > node_j.l:
                    time_valid = False
                    break
                    
                sim_cost += data.cost_matrix[curr_node.node_id][node_j.node_id]
                curr_dep = arr + node_j.duration
                curr_node = node_j

        return best_benefit, best_p_pos, best_d_pos






# --- CC THAM S IU KHIN (CONTROL PARAMETERS) ---
# Heuristic Parameters
TIME_LIMIT = 280.0               # Thi gian chy ti da (giy)
TABU_TENURE = 20                # Thi gian cm (iterations) cho Tabu Search
MUTATION_RATE = 0.05            # T l ph hy ngu nhin (5%)
GRAVITY_ALPHA = 100.0           # Trng s mt  (Gravity) trong best_insert


def load_tour(data, tour_filename):
    routes = [Route(i, data.capacities[i], data.depots[i]) for i in range(data.K)]
    
    with open(tour_filename, 'r', encoding='utf-8') as f:
        lines = [l.strip() for l in f.readlines() if l.strip() and not l.startswith('#')]
        
    for current_vehicle in range(data.K):
        # Skip Benefit line
        line = lines[current_vehicle + 1]
        parts = line.split()
        m_k = int(parts[0])
        if m_k > 0:
            for v_str in parts[1:]:
                v = int(v_str) - 1
                matched_node = None
                
                if data.K <= v < data.K + data.N:
                    for n in data.nodes:
                        if n.type == 'PASSENGER' and n.physical_in == v:
                            matched_node = n
                            break
                elif data.K + data.N <= v < data.K + 2*data.N:
                    # Ignore passenger dropoff since it is handled by the passenger supernode
                    continue
                else:
                    for n in data.nodes:
                        if n.type != 'DEPOT' and n.type != 'PASSENGER' and n.physical_in == v:
                            matched_node = n
                            break
                            
                if matched_node:
                    routes[current_vehicle].nodes.insert(-1, matched_node)
                    
    for r in routes:
        r.update_states(data)
        
    return routes

class LSSolver:
    def __init__(self, data):
        self.data = data
        self.best_routes = []
        self.best_benefit = -999999
        self.log_history = []
        self.parcel_dict = {p.job_id: (p, d) for p, d in self.data.parcels}
        
    def log_and_print(self, msg):
        if not globals().get('RELEASE_MODE', False):
            print(msg)
            self.log_history.append(msg)

    def best_insert(self, routes, unserved_passengers, unserved_parcels, data, active_tabu_edges=None, use_gravity=False, only_profitable=False):
        inserted_count = 0
        
        if not hasattr(self, 'insert_cache'):
            self.insert_cache = {}
            
        threshold = 0 if only_profitable else -999999
        
        while True:
            best_ben = -999999
            best_type, best_idx, best_r_idx, best_p1, best_p2 = None, -1, -1, -1, -1
            
            # --- PASSENGERS ---
            for idx, p_node in enumerate(unserved_passengers):
                if p_node.node_id not in self.insert_cache: 
                    self.insert_cache[p_node.node_id] = {}
                
                for r_idx, route in enumerate(routes):
                    cached = self.insert_cache[p_node.node_id].get(r_idx)
                    if cached is None or cached[0] != route.ver_id:
                        # CACHE PURE BENEFIT (KHNG TABU)
                        ben, pos = Evaluator.evaluate_passenger(route, p_node, data, tabu_edges=None)
                        self.insert_cache[p_node.node_id][r_idx] = (route.ver_id, ben, pos)
                        
                p_best_ben = -999999
                p_best_pos = -1
                p_best_rid = -1
                
                for r_idx, res in self.insert_cache[p_node.node_id].items():
                    if r_idx < len(routes) and res[0] == routes[r_idx].ver_id:
                        ben = res[1]
                        pos = res[2]
                        
                        if ben > -999999 and active_tabu_edges:
                            # Kim tra Tabu trn kt qu cached
                            prev_req = routes[r_idx].nodes[pos-1].job_id
                            next_req = routes[r_idx].nodes[pos].job_id
                            if (prev_req, p_node.job_id) in active_tabu_edges or (p_node.job_id, next_req) in active_tabu_edges:
                                ben, pos = Evaluator.evaluate_passenger(routes[r_idx], p_node, data, active_tabu_edges)
                                
                        if ben > -999999 and use_gravity: 
                            ben += GRAVITY_ALPHA / len(routes[r_idx].nodes)
                        if ben > p_best_ben:
                            p_best_ben = ben
                            p_best_pos = pos
                            p_best_rid = r_idx
                        
                if p_best_ben > best_ben:
                    best_ben = p_best_ben
                    best_type = 'PASSENGER'
                    best_idx = idx
                    best_r_idx = p_best_rid
                    best_p1 = p_best_pos
                    
            # --- PARCELS ---
            for idx, (pick, drop) in enumerate(unserved_parcels):
                if pick.node_id not in self.insert_cache: 
                    self.insert_cache[pick.node_id] = {}
                
                for r_idx, route in enumerate(routes):
                    cached = self.insert_cache[pick.node_id].get(r_idx)
                    if cached is None or cached[0] != route.ver_id:
                        # CACHE PURE BENEFIT
                        ben, p_pos, d_pos = Evaluator.evaluate_parcel(route, pick, drop, data, tabu_edges=None)
                        self.insert_cache[pick.node_id][r_idx] = (route.ver_id, ben, p_pos, d_pos)
                        
                p_best_ben = -999999
                p_best_p_pos = -1
                p_best_d_pos = -1
                p_best_rid = -1
                
                for r_idx, res in self.insert_cache[pick.node_id].items():
                    if r_idx < len(routes) and res[0] == routes[r_idx].ver_id:
                        ben = res[1]
                        p_pos = res[2]
                        d_pos = res[3]
                        
                        if ben > -999999 and active_tabu_edges:
                            prev_pick = routes[r_idx].nodes[p_pos-1].job_id
                            next_pick = routes[r_idx].nodes[p_pos].job_id
                            prev_drop = routes[r_idx].nodes[d_pos-1].job_id if d_pos > p_pos else pick.job_id
                            next_drop = routes[r_idx].nodes[d_pos].job_id
                            
                            is_tabu = ((prev_pick, pick.job_id) in active_tabu_edges or
                                       (pick.job_id, next_pick) in active_tabu_edges or
                                       (prev_drop, drop.job_id) in active_tabu_edges or
                                       (drop.job_id, next_drop) in active_tabu_edges)
                            
                            if is_tabu:
                                ben, p_pos, d_pos = Evaluator.evaluate_parcel(routes[r_idx], pick, drop, data, active_tabu_edges)
                                
                        if ben > -999999 and use_gravity: 
                            ben += GRAVITY_ALPHA / len(routes[r_idx].nodes)
                        if ben > p_best_ben:
                            p_best_ben = ben
                            p_best_p_pos = p_pos
                            p_best_d_pos = d_pos
                            p_best_rid = r_idx
                        
                if p_best_ben > best_ben:
                    best_ben = p_best_ben
                    best_type = 'PARCEL'
                    best_idx = idx
                    best_r_idx = p_best_rid
                    best_p1 = p_best_p_pos
                    best_p2 = p_best_d_pos
                    
            # --- APPLY MOVE ---
            if best_ben > threshold:
                r = routes[best_r_idx]
                if best_type == 'PASSENGER':
                    p_node = unserved_passengers.pop(best_idx)
                    r.nodes.insert(best_p1, p_node)
                else:
                    pick, drop = unserved_parcels.pop(best_idx)
                    r.nodes.insert(best_p1, pick)
                    r.nodes.insert(best_p2 + 1 if best_p2 >= best_p1 else best_p2, drop)
                    
                r.update_states(data)
                inserted_count += 1
                
                if hasattr(self, 'clean_routes'):
                    self.clean_routes.discard(best_r_idx)
                if hasattr(self, 'oropt_clean_routes'):
                    self.oropt_clean_routes.discard(best_r_idx)
            else:
                break
                
        return inserted_count

    def _delta_cost_remove(self, route, i, data):
        n = len(route.nodes)
        prev = route.nodes[i - 1]
        curr = route.nodes[i]
        nxt  = route.nodes[i + 1]

        cost_remove = data.cost_matrix[prev.node_id][curr.node_id] + curr.int_cost
        if nxt.type != 'DEPOT':
            cost_remove += data.cost_matrix[curr.node_id][nxt.node_id]
            cost_save   = data.cost_matrix[prev.node_id][nxt.node_id]
        else:
            cost_save = 0

        return (cost_remove - cost_save) - curr.revenue

    def or_opt_k1(self, routes, data):
        improved_total = False
        for r_idx, route in enumerate(routes):
            if hasattr(self, 'oropt_clean_routes') and r_idx in self.oropt_clean_routes:
                continue
                
            route_changed = False
            improved = True
            safe_counter = 0
            while improved and safe_counter < 100:
                safe_counter += 1
                improved = False
                n = len(route.nodes)
                if n <= 3:
                    break

                best_delta = 0
                best_move = None

                for src in range(1, n - 1):
                    node = route.nodes[src]
                    if node.type == 'PARCEL_DROPOFF': continue

                    if node.type == 'PASSENGER':
                        prev_s = route.nodes[src - 1]
                        next_s = route.nodes[src + 1]

                        cost_remove = data.cost_matrix[prev_s.node_id][node.node_id] + node.int_cost
                        if next_s.type != 'DEPOT':
                            cost_remove += data.cost_matrix[node.node_id][next_s.node_id]
                            cost_bridge  = data.cost_matrix[prev_s.node_id][next_s.node_id]
                        else:
                            cost_bridge = 0

                        gain_remove = cost_remove - cost_bridge

                        for dst in range(1, n - 1):
                            if dst == src or dst == src - 1: continue

                            real_dst_prev = route.nodes[dst - 1] if dst <= src else route.nodes[dst]
                            real_dst_next = route.nodes[dst] if dst <= src else route.nodes[dst + 1]

                            if real_dst_next.type == 'DEPOT':
                                cost_insert = data.cost_matrix[real_dst_prev.node_id][node.node_id] + node.int_cost
                            else:
                                cost_insert = (data.cost_matrix[real_dst_prev.node_id][node.node_id]
                                               + node.int_cost
                                               + data.cost_matrix[node.node_id][real_dst_next.node_id]
                                               - data.cost_matrix[real_dst_prev.node_id][real_dst_next.node_id])

                            delta = gain_remove - cost_insert

                            if delta > best_delta:
                                test_nodes = list(route.nodes)
                                test_nodes.pop(src)
                                insert_at = dst if dst < src else dst
                                test_nodes.insert(insert_at, node)
                                test_route = copy.copy(route)
                                test_route.nodes = test_nodes
                                test_route.update_states(data)
                                if test_route.is_feasible and test_route.total_benefit > route.total_benefit:
                                    best_delta = test_route.total_benefit - route.total_benefit
                                    best_move = (src, dst, False, test_nodes, test_route.total_benefit)

                    elif node.type == 'PARCEL_PICKUP':
                        d_idx = next((j for j in range(src+1, n-1) if route.nodes[j].type == 'PARCEL_DROPOFF' and route.nodes[j].job_id == node.job_id), -1)
                        if d_idx == -1: continue

                        drop = route.nodes[d_idx]
                        orig_ben = route.total_benefit
                        base_nodes = [route.nodes[k] for k in range(n) if k != src and k != d_idx]

                        test_route = copy.copy(route)
                        test_route.nodes = base_nodes
                        test_route.update_states(data)
                            
                        ben_insert, p_pos, d_pos = Evaluator.evaluate_parcel(test_route, node, drop, data)
                        new_total_ben = test_route.total_benefit + ben_insert
                        if new_total_ben > orig_ben:
                            delta = new_total_ben - orig_ben
                            # To avoid inserting back to same pos, we check if it is really better
                            if delta > best_delta:
                                best_delta = delta
                                test_nodes = list(base_nodes)
                                test_nodes.insert(p_pos, node)
                                test_nodes.insert(d_pos + 1 if d_pos >= p_pos else d_pos, drop)
                                best_move = (src, d_idx, True, test_nodes, ben_insert)

                if best_move:
                    _, _, _, new_nodes, _ = best_move
                    route.nodes = new_nodes
                    route.update_states(data)
                    improved = True
                    route_changed = True
                    improved_total = True

            # Sau khi qut xong (hoc khng c g thay i), xe ny  sch s i vi OrOpt
            if hasattr(self, 'oropt_clean_routes'):
                self.oropt_clean_routes.add(r_idx)

        return improved_total

    def exhaustive_remove(self, routes, unserved_pass, unserved_parc, data):
        total_removed = 0
        for r_idx, route in enumerate(routes):
            if hasattr(self, 'clean_routes') and r_idx in self.clean_routes:
                continue
                
            route_changed_ever = False
            route_changed = True
            while route_changed:
                route_changed = False
                i = 1
                while i < len(route.nodes) - 1:
                    node = route.nodes[i]
                    if node.type == 'DEPOT' or node.type == 'PARCEL_DROPOFF':
                        i += 1
                        continue
                        
                    if node.type == 'PASSENGER':
                        delta = self._delta_cost_remove(route, i, data)
                        if delta > 0:
                            route.nodes.pop(i)
                            route.update_states(data)
                            unserved_pass.append(node)
                            total_removed += 1
                            route_changed = True
                            route_changed_ever = True
                            continue
                            
                    elif node.type == 'PARCEL_PICKUP':
                        d_idx = next((j for j in range(i+1, len(route.nodes)-1)
                                      if route.nodes[j].type == 'PARCEL_DROPOFF'
                                      and route.nodes[j].job_id == node.job_id), -1)
                        if d_idx != -1:
                            orig_ben = route.total_benefit
                            drop_node = route.nodes[d_idx]
                            test_nodes = [n for n in route.nodes if n is not node and n is not drop_node]
                            test_route = copy.copy(route)
                            test_route.nodes = test_nodes
                            test_route.update_states(data)

                            if test_route.total_benefit > orig_ben:
                                route.nodes = test_nodes
                                route.update_states(data)
                                for p, d in data.parcels:
                                    if p.job_id == node.job_id:
                                        unserved_parc.append((p, d))
                                        break
                                total_removed += 1
                                route_changed = True
                                route_changed_ever = True
                                continue
                    i += 1
                    
            # Sau khi qut Exhaustive, xe ny  sch s
            if hasattr(self, 'clean_routes'):
                self.clean_routes.add(r_idx)
            
            pass
        return total_removed


    def _apply_removal(self, routes, unserved_pass, unserved_parc, to_remove_ids, data):
        for r_idx, r in enumerate(routes):
            new_nodes = []
            route_changed = False
            for n in r.nodes:
                if n.type != 'DEPOT' and n.job_id in to_remove_ids:
                    route_changed = True
                    if n.type == 'PASSENGER':
                        unserved_pass.append(n)
                    elif n.type == 'PARCEL_PICKUP':
                        if hasattr(self, 'parcel_dict') and n.job_id in self.parcel_dict:
                            unserved_parc.append(self.parcel_dict[n.job_id])
                else:
                    new_nodes.append(n)
            
            if route_changed:
                r.nodes = new_nodes
                r.update_states(data)

    def random_perturbation(self, routes, unserved_pass, unserved_parc, data, percentage=0.05, tabu_dict=None, current_iter=0, tenure=10):
        # Tnh theo s lng request (mi passenger = 1, mi parcel pair = 1 request)
        served_reqs = set([n.job_id for r in routes for n in r.nodes[1:-1]])
        if not served_reqs: return 0
        num_remove = max(1, int(len(served_reqs) * percentage))
        to_remove = set(random.sample(list(served_reqs), min(num_remove, len(served_reqs))))
        
        # Ghi nhn cc cnh b ph v vo Tabu List
        if tabu_dict is not None:
            for r in routes:
                for i in range(1, len(r.nodes)-1):
                    if r.nodes[i].job_id in to_remove:
                        tabu_dict[(r.nodes[i-1].node_id, r.nodes[i].node_id)] = current_iter + tenure
                        tabu_dict[(r.nodes[i].node_id, r.nodes[i+1].node_id)] = current_iter + tenure
                        
        self._apply_removal(routes, unserved_pass, unserved_parc, to_remove, data)
        return len(to_remove)

    def solve(self, max_iterations=100, init_fraction=1.0, initial_tour_file=None):
        import time
        self.t_solve_start = time.time()
        self.log_history = []
        self.clean_routes = set()
        self.oropt_clean_routes = set()

        if initial_tour_file:
            self.log_and_print(f"[INIT] Nạp tour từ file: {initial_tour_file}")
            routes = load_tour(self.data, initial_tour_file)
            
            served_pass_reqs = set([n.job_id for r in routes for n in r.nodes[1:-1] if n.type == 'PASSENGER'])
            served_parc_reqs = set([n.job_id for r in routes for n in r.nodes[1:-1] if 'PARCEL' in n.type])
            unserved_pass = [p for p in self.data.passengers if p.job_id not in served_pass_reqs]
            unserved_parc = [(p, d) for p, d in self.data.parcels if p.job_id not in served_parc_reqs]
            
            self.best_benefit = sum(r.total_benefit for r in routes)
            self.best_routes = copy.deepcopy(routes)
        self.log_and_print(f"Total Max Benefit: {self.best_benefit}")
            
        else:
            routes = [Route(i, self.data.capacities[i], self.data.depots[i]) for i in range(self.data.K)]
            for r in routes: r.update_states(self.data)
            
            unserved_pass = list(self.data.passengers)
            unserved_parc = list(self.data.parcels)
            
            self.log_and_print("Khởi tạo Greedy Tour...")
            
            hidden_pass = []
            hidden_parc = []
            if init_fraction < 1.0:
                num_hide_pass = int(len(unserved_pass) * (1 - init_fraction))
                num_hide_parc = int(len(unserved_parc) * (1 - init_fraction))
                
                import random
                random.shuffle(unserved_pass)
                random.shuffle(unserved_parc)
                
                hidden_pass = unserved_pass[:num_hide_pass]
                unserved_pass = unserved_pass[num_hide_pass:]
                
                hidden_parc = unserved_parc[:num_hide_parc]
                unserved_parc = unserved_parc[num_hide_parc:]
                
            self.best_insert(routes, unserved_pass, unserved_parc, self.data, use_gravity=True)
            
            unserved_pass.extend(hidden_pass)
            unserved_parc.extend(hidden_parc)
            if init_fraction < 1.0:
                self.best_insert(routes, unserved_pass, unserved_parc, self.data, use_gravity=False)
                
            self.best_benefit = sum(r.total_benefit for r in routes)
            self.best_routes = copy.deepcopy(routes)
        self.log_and_print(f"Total Max Benefit: {self.best_benefit}")
        self.log_and_print(f"\n--- BẮT ĐẦU ILS ({max_iterations} ITERS VỚI TABU SEARCH) ---")
        
        tabu_dict = {}
        tabu_tenure = TABU_TENURE
        
        for it in range(max_iterations):
            if time.time() - self.t_solve_start > TIME_LIMIT:
                self.log_and_print(f"\n[TIME LIMIT] Dừng đột ngột do vượt quá {TIME_LIMIT}s tại Iter {it}")
                break
                
            local_improved = True
            # --- Cp nht danh sch Tabu kh dng ---
            # Xa cc cnh  ht hn khi dictionary  trnh r r b nh
            expired_edges = [edge for edge, exp in tabu_dict.items() if exp < it]
            for edge in expired_edges:
                del tabu_dict[edge]
            active_tabu_edges = set(tabu_dict.keys())
            self.log_history.append(f"\n# --- Iter {it:03d} --- (Active Tabu Edges: {len(active_tabu_edges)})")
            
            start_ben = sum(r.total_benefit for r in routes)
            or_opt_count = 0
            remove_count = 0
            insert_count = 0
            
            # --- LOCAL SEARCH ---
            while local_improved:
                local_improved = False
                
                # 1. OR-OPT K=1
                import time
                t0 = time.time()
                if self.or_opt_k1(routes, self.data):
                    local_improved = True
                    or_opt_count += 1
                t_or = time.time() - t0
                
                # 2. Xa v chn (Mt pass duy nht  nhng quyn cho OrOpt)
                t0 = time.time()
                removed = self.exhaustive_remove(routes, unserved_pass, unserved_parc, self.data)
                t_rem_total = time.time() - t0
                
                t0 = time.time()
                inserted = self.best_insert(routes, unserved_pass, unserved_parc, self.data, active_tabu_edges, use_gravity=False, only_profitable=True)
                t_ins_total = time.time() - t0
                
                if removed > 0: remove_count += removed
                insert_count += inserted
                
                if removed > 0 or inserted > 0:
                    local_improved = True
                        
                self.log_and_print(f"  [LS Profile] OrOpt: {t_or:.3f}s | Remove: {t_rem_total:.3f}s | Insert: {t_ins_total:.3f}s")
                
            # --- NH GI & CP NHT K LC ---
            current_benefit = sum(r.total_benefit for r in routes)
            
            # CH gi MILP khi chm mc k lc v  qua giai on Warm-up
                          
            stats_str = f"OrOpt: {or_opt_count:2d}, Rem: {remove_count:2d}, Ins: {insert_count:2d} | Ben: {start_ben:5.0f} -> {current_benefit:5.0f}"
            if current_benefit > self.best_benefit:
                self.best_benefit = current_benefit
                self.best_routes = [r.clone() for r in routes]
                self.log_and_print(f"[Iter {it:03d}] NEW RECORD: {self.best_benefit:6.0f} | {stats_str} | Curr Unserved: {len(unserved_pass)+len(unserved_parc):2d} | Best Unserved: {getattr(self, 'best_unserved', 0):2d}")
            else:
                self.log_and_print(f"[Iter {it:03d}] Local Opt : {current_benefit:6.0f} (Best: {self.best_benefit:6.0f}) | {stats_str} | Curr Unserved: {len(unserved_pass)+len(unserved_parc):2d} | Best Unserved: {getattr(self, 'best_unserved', 0):2d}")
                
            # --- PERTURBATION (PH HY NGU NHIN 5% + P DNG TABU) ---
            routes = [r.clone() for r in self.best_routes]
            
            served_pass_reqs = set([n.job_id for r in routes for n in r.nodes[1:-1] if n.type == 'PASSENGER'])
            served_parc_reqs = set([n.job_id for r in routes for n in r.nodes[1:-1] if 'PARCEL' in n.type])
            unserved_pass = [p for p in self.data.passengers if p.job_id not in served_pass_reqs]
            unserved_parc = [(p, d) for p, d in self.data.parcels if p.job_id not in served_parc_reqs]
            
            num_removed = self.random_perturbation(routes, unserved_pass, unserved_parc, self.data, MUTATION_RATE, tabu_dict, it, tabu_tenure)
            
            # --- REBUILD BNG BEST INSERT (B gii hn bi Tabu) ---
            # Ngay sau khi ph hy, ly danh sch tabu mi nht  chn best_insert nht li vo ch c
            #  pha ny tabu_dict ch va thm  mi vo (cha b qu hn thm), nn ly ton b
            active_tabu_edges_next = set(tabu_dict.keys())
            self.best_insert(routes, unserved_pass, unserved_parc, self.data, active_tabu_edges=active_tabu_edges_next, use_gravity=False)
            
            self.log_history.append(f"# [Perturbation]  xa {num_removed} requests v Rebuild bng Best Insert.")


        import time
        self.total_time = time.time() - getattr(self, 't_solve_start', time.time())
        self.log_and_print("--- FINAL RESULTS ---")
        self.log_and_print(f"Total Max Benefit: {self.best_benefit}")
        self.log_and_print(f"Total Run Time: {self.total_time:.2f}s")
        
        from datetime import datetime
        import os
        
        if globals().get('RELEASE_MODE', False):
            # IN TRUC TIEP RA STDOUT
            print(f"{self.best_benefit}")
            for r in self.best_routes:
                if len(r.nodes) <= 2:
                    print("0")
                else:
                    path = []
                    for n in r.nodes[1:-1]:
                        path.append(str(n.physical_in + 1))
                        if n.type == 'PASSENGER':
                            path.append(str(n.physical_out + 1))
                    print(f"{len(path)} {' '.join(path)}")
            return self.best_benefit

        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        input_filename = getattr(self.data, 'filename', 'unknown')
        if hasattr(self.data, 'filename'):
            base = os.path.basename(self.data.filename)
            name_part = os.path.splitext(base)[0]
        else:
            name_part = "tour"
            
        run_name = f"ls_tour_{timestamp}_{name_part}"
        out_dir = os.path.join("output", run_name)
        os.makedirs(out_dir, exist_ok=True)
        
        out_tour_filename = os.path.join(out_dir, f"{run_name}_tour.txt")
        out_log_filename = os.path.join(out_dir, f"{run_name}.log")
        out_report_filename = os.path.join(out_dir, f"{run_name}_report.txt")
          
        with open(out_tour_filename, 'w', encoding='utf-8') as f:
            f.write(f"{self.best_benefit}\n")
            for r in self.best_routes:
                if len(r.nodes) <= 2:
                      f.write("0\n")
                else:
                    path = []
                    for n in r.nodes[1:-1]:
                        path.append(str(n.physical_in + 1))
                        if n.type == 'PASSENGER':
                            path.append(str(n.physical_out + 1))
                    f.write(f"{len(path)} {' '.join(path)}\n")
                    
        with open(out_log_filename, 'w', encoding='utf-8') as f:
            f.write("\n".join(self.log_history))
                      
        self.log_and_print(f"\nSaved results to directory: {out_dir}")
        self.log_and_print("-" * 30)
          
        # if hasattr(self.data, 'filename'):
        #     validate_tour(self.data.filename, out_tour_filename, out_report_filename)
        return self.best_benefit
  
if __name__ == "__main__":
    import sys
    filename = None
    initial_tour = None
    
    if not RELEASE_MODE:
        if len(sys.argv) < 2:
            print("Usage: python solver_ls.py <testcase_file>")
            sys.exit(1)
        filename = sys.argv[1]
        if len(sys.argv) >= 3:
            initial_tour = sys.argv[2]
            
    data = Data(filename)
    data.filename = filename
    solver = LSSolver(data)
    solver.solve(max_iterations= 10000 , initial_tour_file=initial_tour)

