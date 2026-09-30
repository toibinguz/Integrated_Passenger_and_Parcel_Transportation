import sys
import copy
import time
import random
from validator import validate_tour
from models import GenericNode, Data, Route, Evaluator

# --- CÁC THAM SỐ ĐIỀU KHIỂN (CONTROL PARAMETERS) ---
# Heuristic Parameters
TABU_TENURE = 15                 # Thời gian cấm (iterations) cho Tabu Search
MUTATION_RATE = 0.05            # Tỉ lệ phá hủy ngẫu nhiên (5%)
GRAVITY_ALPHA = 100.0           # Trọng số mật độ (Gravity) trong best_insert

# Tham số MILP
MILP_WARMUP_ITERS = 200         # Tắt MILP trong N iter đầu để Heuristic chạy nhanh
INTER_MILP_ATTEMPTS = 20        # Số lần gieo xúc xắc hoán đổi
INTER_MILP_TIMEOUT_MS = 20000    # Quỹ thời gian / lần (SCIP)


def load_tour(data, tour_filename):
    routes = [Route(i, data.capacities[i], data.depots[i]) for i in range(data.K)]
    
    with open(tour_filename, 'r', encoding='utf-8') as f:
        lines = f.readlines()
        
    current_vehicle = -1
    for line in lines:
        line = line.strip()
        if line.startswith("Xe "):
            parts = line.split()
            current_vehicle = int(parts[1])
        elif line.startswith("[") and current_vehicle != -1:
            tokens = line.split(" -> ")
            for token in tokens:
                token = token.strip("[]")
                type_str, req_id_str = token.split()
                req_id = int(req_id_str)
                
                matched_node = None
                for n in data.nodes:
                    if n.type == type_str and n.request_id == req_id:
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
        self.parcel_dict = {p.request_id: (p, d) for p, d in self.data.parcels}
        
    def log_and_print(self, msg):
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
                if p_node.id not in self.insert_cache: 
                    self.insert_cache[p_node.id] = {}
                
                for r_idx, route in enumerate(routes):
                    cached = self.insert_cache[p_node.id].get(r_idx)
                    if cached is None or cached[0] != route.version:
                        # CACHE PURE BENEFIT (KHÔNG TABU)
                        ben, pos = Evaluator.evaluate_passenger(route, p_node, data, tabu_edges=None)
                        self.insert_cache[p_node.id][r_idx] = (route.version, ben, pos)
                        
                p_best_ben = -999999
                p_best_pos = -1
                p_best_rid = -1
                
                for r_idx, res in self.insert_cache[p_node.id].items():
                    if r_idx < len(routes) and res[0] == routes[r_idx].version:
                        ben = res[1]
                        pos = res[2]
                        
                        if ben > -999999 and active_tabu_edges:
                            # Kiểm tra Tabu trên kết quả cached
                            prev_req = routes[r_idx].nodes[pos-1].request_id
                            next_req = routes[r_idx].nodes[pos].request_id
                            if (prev_req, p_node.request_id) in active_tabu_edges or (p_node.request_id, next_req) in active_tabu_edges:
                                # Nếu dính Tabu, phải tính lại tìm 2nd best (không cache)
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
                if pick.id not in self.insert_cache: 
                    self.insert_cache[pick.id] = {}
                
                for r_idx, route in enumerate(routes):
                    cached = self.insert_cache[pick.id].get(r_idx)
                    if cached is None or cached[0] != route.version:
                        # CACHE PURE BENEFIT
                        ben, p_pos, d_pos = Evaluator.evaluate_parcel(route, pick, drop, data, tabu_edges=None)
                        self.insert_cache[pick.id][r_idx] = (route.version, ben, p_pos, d_pos)
                        
                p_best_ben = -999999
                p_best_p_pos = -1
                p_best_d_pos = -1
                p_best_rid = -1
                
                for r_idx, res in self.insert_cache[pick.id].items():
                    if r_idx < len(routes) and res[0] == routes[r_idx].version:
                        ben = res[1]
                        p_pos = res[2]
                        d_pos = res[3]
                        
                        if ben > -999999 and active_tabu_edges:
                            prev_pick = routes[r_idx].nodes[p_pos-1].request_id
                            next_pick = routes[r_idx].nodes[p_pos].request_id
                            prev_drop = routes[r_idx].nodes[d_pos-1].request_id if d_pos > p_pos else pick.request_id
                            next_drop = routes[r_idx].nodes[d_pos].request_id
                            
                            is_tabu = ((prev_pick, pick.request_id) in active_tabu_edges or
                                       (pick.request_id, next_pick) in active_tabu_edges or
                                       (prev_drop, drop.request_id) in active_tabu_edges or
                                       (drop.request_id, next_drop) in active_tabu_edges)
                            
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

    def intra_route_milp_ls(self, routes, data):
        """
        Sử dụng SCIP MILP để sắp xếp lại chuỗi trong CÙNG MỘT XE.
        Hàm này chạy thay thế hoặc song song với Or-opt k=1.
        """
        from intra_milp import optimize_route_milp
        improved_total = False
        
        if not hasattr(self, 'milp_cache'):
            self.milp_cache = {}
        
        for r_idx, route in enumerate(routes):
            if len(route.nodes) <= 3:
                continue
                
            # Kiểm tra Cache
            if route.vehicle_id in self.milp_cache and self.milp_cache[route.vehicle_id] == route.version:
                continue
                
            improved, new_nodes = optimize_route_milp(route, data, time_limit_ms=2000)
            if improved:
                route.nodes = new_nodes
                route.update_states(data)
                improved_total = True
                self.log_and_print(f"  [MILP] Tối ưu thành công xe {route.vehicle_id} (Giảm cost nội tuyến)")
            
            # Cập nhật Cache. (Ngay cả khi có improved, version bên trong update_states đã nhảy số)
            self.milp_cache[route.vehicle_id] = route.version
                
        return improved_total

    def _delta_cost_remove(self, route, i, data):
        n = len(route.nodes)
        prev = route.nodes[i - 1]
        curr = route.nodes[i]
        nxt  = route.nodes[i + 1]

        cost_remove = data.cost_matrix[prev.id][curr.id] + curr.int_cost
        if nxt.type != 'DEPOT':
            cost_remove += data.cost_matrix[curr.id][nxt.id]
            cost_save   = data.cost_matrix[prev.id][nxt.id]
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

                        cost_remove = data.cost_matrix[prev_s.id][node.id] + node.int_cost
                        if next_s.type != 'DEPOT':
                            cost_remove += data.cost_matrix[node.id][next_s.id]
                            cost_bridge  = data.cost_matrix[prev_s.id][next_s.id]
                        else:
                            cost_bridge = 0

                        gain_remove = cost_remove - cost_bridge

                        for dst in range(1, n - 1):
                            if dst == src or dst == src - 1: continue

                            real_dst_prev = route.nodes[dst - 1] if dst <= src else route.nodes[dst]
                            real_dst_next = route.nodes[dst] if dst <= src else route.nodes[dst + 1]

                            if real_dst_next.type == 'DEPOT':
                                cost_insert = data.cost_matrix[real_dst_prev.id][node.id] + node.int_cost
                            else:
                                cost_insert = (data.cost_matrix[real_dst_prev.id][node.id]
                                               + node.int_cost
                                               + data.cost_matrix[node.id][real_dst_next.id]
                                               - data.cost_matrix[real_dst_prev.id][real_dst_next.id])

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
                        d_idx = next((j for j in range(src+1, n-1) if route.nodes[j].type == 'PARCEL_DROPOFF' and route.nodes[j].request_id == node.request_id), -1)
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

            # Sau khi quét xong (hoặc không có gì thay đổi), xe này đã sạch sẽ đối với OrOpt
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
                            # Không tăng i vì node tiếp theo đã trượt vào vị trí i
                            continue
                            
                    elif node.type == 'PARCEL_PICKUP':
                        d_idx = next((j for j in range(i+1, len(route.nodes)-1)
                                      if route.nodes[j].type == 'PARCEL_DROPOFF'
                                      and route.nodes[j].request_id == node.request_id), -1)
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
                                    if p.request_id == node.request_id:
                                        unserved_parc.append((p, d))
                                        break
                                total_removed += 1
                                route_changed = True
                                route_changed_ever = True
                                continue
                    i += 1
                    
            # Sau khi quét Exhaustive, xe này đã sạch sẽ
            if hasattr(self, 'clean_routes'):
                self.clean_routes.add(r_idx)
            
            pass
        return total_removed


    def _apply_removal(self, routes, unserved_pass, unserved_parc, to_remove_ids, data):
        for r_idx, r in enumerate(routes):
            new_nodes = []
            route_changed = False
            for n in r.nodes:
                if n.type != 'DEPOT' and n.request_id in to_remove_ids:
                    route_changed = True
                    if n.type == 'PASSENGER':
                        unserved_pass.append(n)
                    elif n.type == 'PARCEL_PICKUP':
                        if hasattr(self, 'parcel_dict') and n.request_id in self.parcel_dict:
                            unserved_parc.append(self.parcel_dict[n.request_id])
                else:
                    new_nodes.append(n)
            
            if route_changed:
                r.nodes = new_nodes
                r.update_states(data)

    def random_perturbation(self, routes, unserved_pass, unserved_parc, data, percentage=0.05, tabu_dict=None, current_iter=0, tenure=10):
        # Tính theo số lượng request (mỗi passenger = 1, mỗi parcel pair = 1 request)
        served_reqs = set([n.request_id for r in routes for n in r.nodes[1:-1]])
        if not served_reqs: return 0
        num_remove = max(1, int(len(served_reqs) * percentage))
        to_remove = set(random.sample(list(served_reqs), min(num_remove, len(served_reqs))))
        
        # Ghi nhận các cạnh bị phá vỡ vào Tabu List
        if tabu_dict is not None:
            for r in routes:
                for i in range(1, len(r.nodes)-1):
                    if r.nodes[i].request_id in to_remove:
                        tabu_dict[(r.nodes[i-1].request_id, r.nodes[i].request_id)] = current_iter + tenure
                        tabu_dict[(r.nodes[i].request_id, r.nodes[i+1].request_id)] = current_iter + tenure
                        
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
            
            served_reqs = set([n.request_id for r in routes for n in r.nodes[1:-1]])
            unserved_pass = [p for p in self.data.passengers if p.request_id not in served_reqs]
            unserved_parc = [(p, d) for p, d in self.data.parcels if p.request_id not in served_reqs]
            
            self.best_benefit = sum(r.total_benefit for r in routes)
            self.best_routes = copy.deepcopy(routes)
            self.log_and_print(f"[INIT] Benefit nạp từ file: {self.best_benefit}")
            
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
            self.log_and_print(f"[Khởi tạo] Benefit ban đầu: {self.best_benefit}")
        self.log_and_print(f"\n--- BẮT ĐẦU ILS ({max_iterations} ITERS VỚI TABU SEARCH) ---")
        
        tabu_dict = {}
        tabu_tenure = TABU_TENURE
        
        for it in range(max_iterations):
            local_improved = True
            # --- Cập nhật danh sách Tabu khả dụng ---
            active_tabu_edges = set([edge for edge, exp in tabu_dict.items() if exp >= it])
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
                
                # 2. Xóa và chèn (Một pass duy nhất để nhường quyền cho OrOpt)
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
                
            # --- ĐÁNH GIÁ & CẬP NHẬT KỶ LỤC ---
            current_benefit = sum(r.total_benefit for r in routes)
            
            # CHỈ gọi MILP khi chạm mốc kỷ lục và đã qua giai đoạn Warm-up
            if current_benefit > self.best_benefit and it >= MILP_WARMUP_ITERS:
                # 1. INTRA-ROUTE MILP (Tối ưu nội tuyến)
                t_m_start = time.time()
                if self.intra_route_milp_ls(routes, self.data):
                    old_ben = current_benefit
                    current_benefit = sum(r.total_benefit for r in routes)
                    self.log_and_print(f"  [INTRA-MILP] Kích hoạt lúc tiệm cận đỉnh! Lãi thêm: {current_benefit - old_ben:.0f} điểm (Time: {time.time()-t_m_start:.2f}s)")
                else:
                    self.log_and_print(f"  [INTRA-MILP] Đã quét qua nhưng 100% các xe đều Optimal (Time: {time.time()-t_m_start:.2f}s)")
                    
                # 2. INTER-ROUTE MILP (Giao phấn chéo)
                if len(routes) >= 2:
                    from inter_route_milp import optimize_2_routes_milp
                    import random
                    t_inter_start = time.time()
                    inter_success = 0
                    
                    for _ in range(INTER_MILP_ATTEMPTS):
                        rA, rB = random.sample(routes, 2)
                        improved, _, _ = optimize_2_routes_milp(rA, rB, [], self.data, time_limit_ms=INTER_MILP_TIMEOUT_MS)
                        if improved:
                            inter_success += 1
                            
                    if inter_success > 0:
                        old_ben_inter = current_benefit
                        current_benefit = sum(r.total_benefit for r in routes)
                        self.log_and_print(f"  [INTER-MILP] Giao phấn thành công {inter_success} lần! Lãi thêm: {current_benefit - old_ben_inter:.0f} điểm (Time: {time.time()-t_inter_start:.2f}s)")
            
            stats_str = f"OrOpt: {or_opt_count:2d}, Rem: {remove_count:2d}, Ins: {insert_count:2d} | Ben: {start_ben:5.0f} -> {current_benefit:5.0f}"
            if current_benefit > self.best_benefit:
                self.best_benefit = current_benefit
                self.best_routes = [r.clone() for r in routes]
                self.log_and_print(f"[Iter {it:03d}] KỶ LỤC MỚI: {self.best_benefit:6.0f} | {stats_str} | Unserved: {len(unserved_pass)+len(unserved_parc):2d}")
            else:
                self.log_and_print(f"[Iter {it:03d}] Local Opt : {current_benefit:6.0f} (Best: {self.best_benefit:6.0f}) | {stats_str} | Unserved: {len(unserved_pass)+len(unserved_parc):2d}")
                
            # --- PERTURBATION (PHÁ HỦY NGẪU NHIÊN 5% + ÁP DỤNG TABU) ---
            routes = [r.clone() for r in self.best_routes]
            
            served_reqs = set([n.request_id for r in routes for n in r.nodes[1:-1]])
            unserved_pass = [p for p in self.data.passengers if p.request_id not in served_reqs]
            unserved_parc = [(p, d) for p, d in self.data.parcels if p.request_id not in served_reqs]
            
            num_removed = self.random_perturbation(routes, unserved_pass, unserved_parc, self.data, MUTATION_RATE, tabu_dict, it, tabu_tenure)
            
            # --- REBUILD BẰNG BEST INSERT (Bị giới hạn bởi Tabu) ---
            # Ngay sau khi phá hủy, lấy danh sách tabu mới nhất để chặn best_insert nhét lại vào chỗ cũ
            active_tabu_edges_next = set([edge for edge, exp in tabu_dict.items() if exp >= it])
            self.best_insert(routes, unserved_pass, unserved_parc, self.data, active_tabu_edges=active_tabu_edges_next, use_gravity=False)
            
            self.log_history.append(f"# [Perturbation] Đã xóa {num_removed} requests và Rebuild bằng Best Insert.")

        # --- PHA VDLS ---
        # self.log_and_print("==================================================")
        # self.log_and_print("=== BẮT ĐẦU PHA VDLS (CHUYÊN BIỆT TỐI ƯU CẤU TRÚC K-OPT) ===")
        # self.log_and_print("==================================================")
        # 
        # routes = copy.deepcopy(self.best_routes)
        # vdls_iter = 1
        # while True:
        #     from vdls import run_vdls
        #     t0 = time.time()
        #     found, cycle_gain, cycle_depth, cycle_path = run_vdls(self, routes, self.data)
        #     t_vdls = time.time() - t0
        #     
        #     if not found:
        #         self.log_and_print(f"[VDLS] Không tìm thêm được cycle nào. Đã hội tụ hoàn toàn!")
        #         break
        #         
        #     # Chạy OrOpt để tái phân bổ lại các xe bị ảnh hưởng
        #     t_or0 = time.time()
        #     self.or_opt_k1(routes, self.data)
        #     t_or = time.time() - t_or0
        #     
        #     current_benefit = sum(r.total_benefit for r in routes)
        #     if current_benefit > self.best_benefit:
        #         self.best_benefit = current_benefit
        #         self.best_routes = copy.deepcopy(routes)
        #         self.log_and_print(f"[VDLS Iter {vdls_iter:03d}] KỶ LỤC MỚI: {self.best_benefit:6.0f} | Thời gian: VDLS {t_vdls:.3f}s, OrOpt {t_or:.3f}s")
        #     else:
        #         self.log_and_print(f"[VDLS Iter {vdls_iter:03d}] Cải thiện cục bộ: {current_benefit:6.0f} (Best: {self.best_benefit:6.0f}) | Thời gian: VDLS {t_vdls:.3f}s, OrOpt {t_or:.3f}s")
        #     
        #     # Log chi tiết chain
        #     self.log_and_print(f"  > CHUỖI ĐỔI (Gain: {cycle_gain:.0f}, Depth: {cycle_depth}):")
        #     self.log_and_print(f"  > {cycle_path}")
        #     
        #     vdls_iter += 1
        #     if vdls_iter > 5000:
        #         self.log_and_print("[VDLS] Đạt giới hạn an toàn 5000 iters. Dừng VDLS.")
        #         break

        import time
        self.total_time = time.time() - getattr(self, 't_solve_start', time.time())
        self.log_and_print("--- KẾT QUẢ CUỐI CÙNG ---")
        self.log_and_print(f"Tổng Benefit Tối Đa: {self.best_benefit}")
        self.log_and_print(f"Tổng Thời Gian Chạy: {self.total_time:.2f}s")
        
        timestamp = time.strftime("%H%M%S")
        input_filename = getattr(self.data, 'filename', 'unknown')
        if hasattr(self.data, 'filename'):
            import os
            base = os.path.basename(self.data.filename)
            name_part = os.path.splitext(base)[0]
        else:
            name_part = "tour"
            
        out_filename = f"ls_tour_{timestamp}_{name_part}.txt"
        
        with open(out_filename, 'w', encoding='utf-8') as f:
            f.write(f"Total Benefit: {self.best_benefit}\n")
            for r in self.best_routes:
                f.write(f"Xe {r.vehicle_id} (Capacity {r.capacity}):\n")
                if len(r.nodes) <= 2:
                    f.write("  KHÔNG CHỞ\n")
                    continue
                path_str = " -> ".join([f"[{'PASSENGER' if n.type == 'PASSENGER' else n.type} {n.request_id}]" for n in r.nodes[1:-1]])
                f.write(f"  {path_str}\n")
                
            for msg in self.log_history:
                f.write(f"# {msg}\n")
                
        self.log_and_print(f"\nĐã xuất kết quả ra {out_filename}")
        self.log_and_print("-" * 30)
        
        if hasattr(self.data, 'filename'):
            validate_tour(self.data.filename, out_filename)
        return self.best_benefit

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python solver_ls.py <testcase_file>")
        sys.exit(1)
        
    filename = sys.argv[1]
    initial_tour = None
    if len(sys.argv) >= 3:
        initial_tour = sys.argv[2]
        
    data = Data(filename)
    data.filename = filename
    solver = LSSolver(data)
    solver.solve(max_iterations= 10000, init_fraction= 0.95, initial_tour_file=initial_tour)
