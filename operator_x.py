import random
import time

def get_valid_routes(pool_requests, pool_nodes, depot_node, vehicle_cap, data):
    R = len(pool_requests)
    req_of_node = []
    is_dropoff = []
    is_pickup = []
    pickup_idx_of_dropoff = {}
    
    idx = 0
    for req_id, req in enumerate(pool_requests):
        if isinstance(req, tuple):
            req_of_node.extend([req_id, req_id])
            is_pickup.extend([True, False])
            is_dropoff.extend([False, True])
            pickup_idx_of_dropoff[idx + 1] = idx
            idx += 2
        else:
            req_of_node.append(req_id)
            is_pickup.append(False)
            is_dropoff.append(False)
            idx += 1
            
    V = len(pool_nodes)
    prereq = [0] * V
    for i in range(V):
        if is_dropoff[i]:
            prereq[i] = 1 << pickup_idx_of_dropoff[i]
            
    memo = {}
    best_routes = {}
    best_routes[0] = (0, 0, [])
    
    def dfs(mask, u_idx, current_time, current_cost, current_load, current_profit, req_mask, open_parcels, path):
        state_key = (mask, u_idx)
        if state_key in memo:
            state_history = memo[state_key]
            for (t, c) in state_history:
                if t <= current_time and c <= current_cost:
                    return
            state_history[:] = [(t, c) for (t, c) in state_history if not (current_time <= t and current_cost <= c)]
            state_history.append((current_time, current_cost))
        else:
            memo[state_key] = [(current_time, current_cost)]
            
        if open_parcels == 0:
            u_node = pool_nodes[u_idx] if u_idx >= 0 else depot_node
            ret_cost = current_cost + (0 if u_node.type == 'DEPOT' else data.cost_matrix[u_node.node_id][depot_node.node_id])
            ret_ben = current_profit - ret_cost
            
            if req_mask not in best_routes or ret_ben > best_routes[req_mask][0]:
                best_routes[req_mask] = (ret_ben, ret_cost, list(path))
                
        u_node = pool_nodes[u_idx] if u_idx >= 0 else depot_node
        unvisited = (~mask) & ((1 << V) - 1)
        
        while unvisited > 0:
            v_bit = unvisited & -unvisited
            unvisited ^= v_bit
            v = v_bit.bit_length() - 1
            
            if prereq[v] and (mask & prereq[v]) == 0: continue
            
            v_node = pool_nodes[v]
            if current_load + v_node.weight > vehicle_cap or current_load + v_node.weight < 0: continue
            
            arr = current_time + data.time_matrix[u_node.node_id][v_node.node_id]
            if arr > v_node.l: continue
            
            start_time = max(arr, v_node.e)
            nxt_time = start_time + v_node.duration
            nxt_cost = current_cost + data.cost_matrix[u_node.node_id][v_node.node_id] + v_node.int_cost
            nxt_profit = current_profit + v_node.revenue
            
            nxt_req_mask = req_mask
            nxt_open_parcels = open_parcels
            req_id = req_of_node[v]
            
            if is_pickup[v]:
                nxt_open_parcels |= (1 << req_id)
            elif is_dropoff[v]:
                nxt_open_parcels &= ~(1 << req_id)
                nxt_req_mask |= (1 << req_id)
            else:
                nxt_req_mask |= (1 << req_id)
                
            path.append(v)
            dfs(mask | v_bit, v, nxt_time, nxt_cost, current_load + v_node.weight, nxt_profit, nxt_req_mask, nxt_open_parcels, path)
            path.pop()
            
    dfs(0, -1, depot_node.e, 0, 0, 0, 0, 0, [])
    return best_routes

def run_operator_x(routes, unserved_pass, unserved_parc, data, req_dict=None, solver=None):
    import time
    t_start = time.time()
    
    pairs = [(i, j) for i in range(len(routes)) for j in range(i+1, len(routes))]
    random.shuffle(pairs)
    
    if req_dict is None:
        req_dict = {}
        for p in data.passengers: req_dict[p.job_id] = p
        for pick, drop in data.parcels: req_dict[pick.job_id] = (pick, drop)
        
    pairs_evaluated = 0
    dfs_calls = 0
    
    global_best_ben_diff = 0
    global_best_move = None
    
    for r1_idx, r2_idx in pairs:
        r1, r2 = routes[r1_idx], routes[r2_idx]
        
        base_reqs = []
        job_ids = set([n.job_id for n in r1.nodes[1:-1]] + [n.job_id for n in r2.nodes[1:-1]])
        for j_id in job_ids:
            if j_id in req_dict: base_reqs.append(req_dict[j_id])
            
        base_phys = sum(2 if isinstance(r, tuple) else 1 for r in base_reqs)
        MAX_PHYSICAL = 18
        
        if base_phys > MAX_PHYSICAL:
            continue
            
        pairs_evaluated += 1
        
        for pool_iter in range(10): # Tăng số lần pooling lên 10
            pool_reqs = list(base_reqs)
            pool_u_pass = list(unserved_pass)
            pool_u_parc = list(unserved_parc)
            random.shuffle(pool_u_pass)
            random.shuffle(pool_u_parc)
            
            cur_phys = base_phys
            while cur_phys < MAX_PHYSICAL:
                if pool_u_pass and cur_phys + 1 <= MAX_PHYSICAL:
                    pool_reqs.append(pool_u_pass.pop())
                    cur_phys += 1
                elif pool_u_parc and cur_phys + 2 <= MAX_PHYSICAL:
                    pool_reqs.append(pool_u_parc.pop())
                    cur_phys += 2
                else:
                    break
                
            pool_nodes = []
            for req in pool_reqs:
                if isinstance(req, tuple): pool_nodes.extend([req[0], req[1]])
                else: pool_nodes.append(req)
                    
            b1_routes = get_valid_routes(pool_reqs, pool_nodes, r1.nodes[0], r1.capacity, data)
            b2_routes = get_valid_routes(pool_reqs, pool_nodes, r2.nodes[0], r2.capacity, data)
            dfs_calls += 2
            
            best_ben, best_m1, best_m2 = -999999, -1, -1
            
            for m1, (ben1, c1, p1) in b1_routes.items():
                for m2, (ben2, c2, p2) in b2_routes.items():
                    if (m1 & m2) == 0 and ben1 + ben2 > best_ben:
                        best_ben, best_m1, best_m2 = ben1 + ben2, m1, m2
                            
            cur_ben = r1.total_benefit + r2.total_benefit
            ben_diff = best_ben - cur_ben
            if ben_diff > global_best_ben_diff:
                global_best_ben_diff = ben_diff
                global_best_move = (r1_idx, r2_idx, pool_nodes, best_m1, best_m2, b1_routes, b2_routes, cur_ben, best_ben)
                
    if global_best_move is not None:
        r1_idx, r2_idx, pool_nodes, best_m1, best_m2, b1_routes, b2_routes, cur_ben, best_ben = global_best_move
        r1, r2 = routes[r1_idx], routes[r2_idx]
        
        r1.nodes = [r1.nodes[0]] + [pool_nodes[i] for i in b1_routes[best_m1][2]] + [r1.nodes[-1]]
        r2.nodes = [r2.nodes[0]] + [pool_nodes[i] for i in b2_routes[best_m2][2]] + [r2.nodes[-1]]
        r1.update_states(data)
        r2.update_states(data)
        
        served_pass = set([n.job_id for r in routes for n in r.nodes[1:-1] if n.type == 'PASSENGER'])
        served_parc = set([n.job_id for r in routes for n in r.nodes[1:-1] if 'PARCEL' in n.type])
        unserved_pass[:] = [p for p in data.passengers if p.job_id not in served_pass]
        unserved_parc[:] = [(p, d) for p, d in data.parcels if p.job_id not in served_parc]
        
        if solver:
            t_end = time.time()
            solver.log_and_print(f"    [Opt X DFS] Tìm thấy cấu hình cực đại cho Xe {r1_idx} & Xe {r2_idx} sau {t_end - t_start:.3f}s")
            solver.log_and_print(f"    [Opt X DFS] Kỷ lục được X phá vỡ! Benefit cặp xe này: {cur_ben} -> {best_ben}")
            
        return True
            
    if solver:
        t_end = time.time()
        solver.log_and_print(f"    [Opt X DFS] Quét {pairs_evaluated} cặp xe, {dfs_calls} lần gọi đệ quy | Không tìm thấy cải tiến nào. Time: {t_end - t_start:.3f}s")
        
    return False
