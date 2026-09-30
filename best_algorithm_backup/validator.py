import sys

sys.stdout.reconfigure(encoding='utf-8')

def validate_tour(input_file, tour_file):
    print(f"=== VALIDATING {tour_file} AGAINST {input_file} ===")
    
    # 1. PARSE INPUT FILE
    with open(input_file, 'r') as f:
        lines = [line.strip() for line in f.readlines() if line.strip()]
    tokens = []
    for line in lines: tokens.extend(line.split())
    
    idx = 0
    K = int(tokens[idx]); idx += 1
    N = int(tokens[idx]); idx += 1
    M = int(tokens[idx]); idx += 1
    V = 2 * N + 2 * M + K
    
    capacities = []
    for _ in range(K):
        capacities.append(int(tokens[idx]))
        idx += 1
        
    requests = {}
    total_available_revenue = 0
    
    for i in range(N):
        u = K + i; v = K + N + i
        T = int(tokens[idx]); idx += 1; E = int(tokens[idx]); idx += 1
        L = int(tokens[idx]); idx += 1; S = int(tokens[idx]); idx += 1
        requests[f"PASSENGER {i}"] = {
            'in': u, 'out': v, 'rev': T, 'e': E, 'l': L, 'dur': S+S, 'w': 0, 'req_id': i
        }
        total_available_revenue += T
        
    for j in range(M):
        u = K + 2*N + j; v = K + 2*N + M + j
        T = int(tokens[idx]); idx += 1; Ep = int(tokens[idx]); idx += 1
        Lp = int(tokens[idx]); idx += 1; Ed = int(tokens[idx]); idx += 1
        Ld = int(tokens[idx]); idx += 1; S = int(tokens[idx]); idx += 1
        w = int(tokens[idx]); idx += 1
        
        requests[f"PARCEL_PICKUP {j}"] = {
            'in': u, 'out': u, 'rev': T, 'e': Ep, 'l': Lp, 'dur': S, 'w': w, 'req_id': j
        }
        requests[f"PARCEL_DROPOFF {j}"] = {
            'in': v, 'out': v, 'rev': 0, 'e': Ed, 'l': Ld, 'dur': S, 'w': -w, 'req_id': j
        }
        total_available_revenue += T
        
    time_matrix = [[0] * V for _ in range(V)]
    for r in range(V):
        for c in range(V):
            time_matrix[r][c] = int(tokens[idx]); idx += 1
            
    cost_matrix = [[0] * V for _ in range(V)]
    for r in range(V):
        for c in range(V):
            cost_matrix[r][c] = int(tokens[idx]); idx += 1
            
    # Phụ gia internal duration/cost
    for k, v in requests.items():
        if k.startswith("PASSENGER"):
            v['dur'] += time_matrix[v['in']][v['out']]
            v['int_cost'] = cost_matrix[v['in']][v['out']]
        else:
            v['int_cost'] = 0

    # 2. PARSE TOUR FILE
    with open(tour_file, 'r', encoding='utf-8') as f:
        tour_lines = [line.strip() for line in f.readlines() if line.strip()]
        
    claimed_benefit = int(tour_lines[0].split(":")[1].strip())
    
    routes = {}
    current_vehicle = -1
    for line in tour_lines[1:]:
        if line.startswith("Xe"):
            # Xe 0 (Capacity 90):
            current_vehicle = int(line.split()[1])
            routes[current_vehicle] = []
        elif line.startswith("[") or line.startswith("KHÔNG"):
            if "KHÔNG CHỞ" in line:
                continue
            # [PASSENGER 4] -> [PARCEL_PICKUP 0]
            nodes = line.split(" -> ")
            for node_str in nodes:
                # Bỏ dấu ngoặc []
                node_str = node_str.strip("[]")
                routes[current_vehicle].append(node_str)

    # 3. VALIDATE LOGIC
    total_cost = 0
    total_revenue_served = 0
    served_set = set()
    
    for v_id, route in routes.items():
        curr_time = 0
        curr_load = 0
        curr_loc = v_id
        cap = capacities[v_id]
        
        picked_parcels = set()
        
        for step, node_name in enumerate(route):
            if node_name not in requests:
                print(f"[ERROR] Xe {v_id}: Không tìm thấy node {node_name}")
                return False
                
            req = requests[node_name]
            
            # Kiểm tra Pick-Drop logic
            if node_name.startswith("PARCEL_PICKUP"):
                picked_parcels.add(req['req_id'])
            elif node_name.startswith("PARCEL_DROPOFF"):
                if req['req_id'] not in picked_parcels:
                    print(f"[ERROR] Xe {v_id}: Thả hàng {req['req_id']} khi chưa lấy!")
                    return False
                picked_parcels.remove(req['req_id'])
                
            if node_name in served_set:
                print(f"[ERROR] Xe {v_id}: Node {node_name} bị phục vụ 2 lần!")
                return False
            served_set.add(node_name)
            
            # Tính toán di chuyển
            t_travel = time_matrix[curr_loc][req['in']]
            c_travel = cost_matrix[curr_loc][req['in']]
            
            arr_time = curr_time + t_travel
            wait = max(0, req['e'] - arr_time)
            start_time = arr_time + wait
            
            # Thời gian kiểm tra Time Window
            if start_time > req['l']:
                print(f"[ERROR] Xe {v_id}, Node {node_name}: VI PHẠM TIME WINDOW! (start={start_time} > l={req['l']})")
                return False
                
            # Kiểm tra tải trọng
            curr_load += req['w']
            if curr_load > cap or curr_load < 0:
                print(f"[ERROR] Xe {v_id}, Node {node_name}: VI PHẠM TẢI TRỌNG! (load={curr_load} > cap={cap})")
                return False
                
            # Cập nhật state
            total_cost += c_travel + req['int_cost']
            total_revenue_served += req['rev']
            curr_time = start_time + req['dur']
            curr_loc = req['out']
            
        if len(picked_parcels) > 0:
            print(f"[ERROR] Xe {v_id}: Kết thúc chuyến nhưng chưa giao hàng {picked_parcels}!")
            return False

    actual_benefit = total_revenue_served - total_cost
    
    print("[SUCCESS] Tất cả ràng buộc Tải trọng, Thời gian, Thứ tự hàng hóa đều HỢP LỆ!")
    print(f"  + Tổng doanh thu : {total_revenue_served}")
    print(f"  - Tổng chi phí   : {total_cost}")
    print(f"  = Lợi nhuận thực : {actual_benefit}")
    print(f"  (Lợi nhuận file báo cáo: {claimed_benefit})")
    
    if actual_benefit != claimed_benefit:
        print("[WARNING] Lợi nhuận thực tế KHÁC với lợi nhuận được báo cáo trong file!")
        return False
        
    return True

if __name__ == '__main__':
    input_f = sys.argv[1] if len(sys.argv) > 1 else 'test_1.txt'
    tour_f = sys.argv[2] if len(sys.argv) > 2 else 'ortools_tour.txt'
    validate_tour(input_f, tour_f)
