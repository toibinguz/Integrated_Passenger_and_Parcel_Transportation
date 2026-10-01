import sys

sys.stdout.reconfigure(encoding='utf-8')

def validate_tour(input_file, tour_file):
    print(f"=== VALIDATING {tour_file} AGAINST {input_file} ===")
    
    # 1. PARSE INPUT FILE
    with open(input_file, 'r', encoding='utf-8') as f:
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
            'in': u, 'out': v, 'rev': T, 'e': E, 'l': L, 'dur': S+S, 'w': 0, 'job_id': i, 'type': 'PASSENGER'
        }
        total_available_revenue += T
        
    for j in range(M):
        u = K + 2*N + j; v = K + 2*N + M + j
        T = int(tokens[idx]); idx += 1; Ep = int(tokens[idx]); idx += 1
        Lp = int(tokens[idx]); idx += 1; Ed = int(tokens[idx]); idx += 1
        Ld = int(tokens[idx]); idx += 1; S = int(tokens[idx]); idx += 1
        w = int(tokens[idx]); idx += 1
        
        requests[f"PARCEL_PICKUP {N+j}"] = {
            'in': u, 'out': u, 'rev': T, 'e': Ep, 'l': Lp, 'dur': S, 'w': w, 'job_id': N+j, 'type': 'PARCEL_PICKUP'
        }
        requests[f"PARCEL_DROPOFF {N+j}"] = {
            'in': v, 'out': v, 'rev': 0, 'e': Ed, 'l': Ld, 'dur': S, 'w': -w, 'job_id': N+j, 'type': 'PARCEL_DROPOFF'
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

    # 3. VALIDATE LOGIC & REPORTING
    total_cost = 0
    total_revenue_served = 0
    served_set = set()
    
    # Chuẩn bị dữ liệu báo cáo
    report_lines = []
    report_lines.append(f"=== BÁO CÁO CHI TIẾT CHO TOUR: {tour_file} ===")
    report_lines.append(f"Dữ liệu gốc: {input_file}\n")
    report_lines.append("--- CHI TIẾT HÀNH TRÌNH TỪNG XE ---")
    
    is_valid = True
    
    for v_id, route in routes.items():
        curr_time = 0
        curr_load = 0
        curr_loc = v_id
        cap = capacities[v_id]
        
        picked_parcels = set()
        report_lines.append(f"\n[Xe {v_id}] (Tải trọng tối đa: {cap})")
        
        if not route:
            report_lines.append("  Không chở hàng.")
            continue
            
        for step, node_name in enumerate(route):
            if node_name not in requests:
                msg = f"[ERROR] Xe {v_id}: Không tìm thấy node {node_name}"
                print(msg)
                report_lines.append("  " + msg)
                is_valid = False
                break
                
            req = requests[node_name]
            
            # Kiểm tra Pick-Drop logic
            if node_name.startswith("PARCEL_PICKUP"):
                picked_parcels.add(req['job_id'])
            elif node_name.startswith("PARCEL_DROPOFF"):
                if req['job_id'] not in picked_parcels:
                    msg = f"[ERROR] Xe {v_id}: Thả hàng {req['job_id']} khi chưa lấy!"
                    print(msg)
                    report_lines.append("  " + msg)
                    is_valid = False
                picked_parcels.remove(req['job_id'])
                
            if node_name in served_set:
                msg = f"[ERROR] Xe {v_id}: Node {node_name} bị phục vụ 2 lần!"
                print(msg)
                report_lines.append("  " + msg)
                is_valid = False
            served_set.add(node_name)
            
            # Tính toán di chuyển
            t_travel = time_matrix[curr_loc][req['in']]
            c_travel = cost_matrix[curr_loc][req['in']]
            
            arr_time = curr_time + t_travel
            wait = max(0, req['e'] - arr_time)
            start_time = arr_time + wait
            
            # Thời gian kiểm tra Time Window
            if start_time > req['l']:
                msg = f"[ERROR] Xe {v_id}, Node {node_name}: VI PHẠM TIME WINDOW! (start={start_time} > l={req['l']})"
                print(msg)
                report_lines.append("  " + msg)
                is_valid = False
                
            # Kiểm tra tải trọng
            curr_load += req['w']
            if curr_load > cap or curr_load < 0:
                msg = f"[ERROR] Xe {v_id}, Node {node_name}: VI PHẠM TẢI TRỌNG! (load={curr_load} > cap={cap})"
                print(msg)
                report_lines.append("  " + msg)
                is_valid = False
                
            # Ghi báo cáo bước này
            tw_str = f"[{req['e']}-{req['l']}]"
            report_lines.append(f"  {step+1:02d}. {node_name:<16} | Tải trọng: {curr_load:3d}/{cap} | TW: {tw_str:<12} | Tới: {arr_time:6d} | Chờ: {wait:4d} | Bắt đầu: {start_time:6d} | Dịch vụ: {req['dur']:4d} | Xong: {start_time + req['dur']:6d} | Doanh thu: {req['rev']:5d} | Chi phí chặng: {c_travel + req['int_cost']:5d}")
            
            # Cập nhật state
            total_cost += c_travel + req['int_cost']
            total_revenue_served += req['rev']
            curr_time = start_time + req['dur']
            curr_loc = req['out']
            
        if len(picked_parcels) > 0:
            msg = f"[ERROR] Xe {v_id}: Kết thúc chuyến nhưng chưa giao hàng {picked_parcels}!"
            print(msg)
            report_lines.append("  " + msg)
            is_valid = False

    actual_benefit = total_revenue_served - total_cost
    
    if is_valid:
        print("[SUCCESS] Tất cả ràng buộc Tải trọng, Thời gian, Thứ tự hàng hóa đều HỢP LỆ!")
    else:
        print("[FAILED] Tour có vi phạm ràng buộc!")
        
    print(f"  + Tổng doanh thu : {total_revenue_served}")
    print(f"  - Tổng chi phí   : {total_cost}")
    print(f"  = Lợi nhuận thực : {actual_benefit}")
    print(f"  (Lợi nhuận file báo cáo: {claimed_benefit})")
    
    if actual_benefit != claimed_benefit:
        print("[WARNING] Lợi nhuận thực tế KHÁC với lợi nhuận được báo cáo trong file!")
        is_valid = False
        
    # Thống kê Served / Unserved
    all_passengers = [f"PASSENGER {i}" for i in range(N)]
    all_parcels = [f"PARCEL_PICKUP {N+j}" for j in range(M)] # Chỉ đếm pickup là đủ đại diện cho kiện hàng
    
    served_passengers = [p for p in all_passengers if p in served_set]
    unserved_passengers = [p for p in all_passengers if p not in served_set]
    
    served_parcels = [p for p in all_parcels if p in served_set]
    unserved_parcels = [p for p in all_parcels if p not in served_set]
    
    report_lines.append("\n--- THỐNG KÊ PHỤC VỤ ---")
    report_lines.append(f"Tổng hành khách đã phục vụ: {len(served_passengers)} / {N}")
    report_lines.append(f"Tổng hàng hóa đã phục vụ  : {len(served_parcels)} / {M}")
    report_lines.append(f"Tổng hành khách BỎ QUA    : {len(unserved_passengers)}")
    if unserved_passengers:
        report_lines.append(f"  Danh sách: {', '.join(unserved_passengers)}")
    report_lines.append(f"Tổng hàng hóa BỎ QUA      : {len(unserved_parcels)}")
    if unserved_parcels:
        report_lines.append(f"  Danh sách: {', '.join([p.replace('_PICKUP', '') for p in unserved_parcels])}")
        
    report_lines.append("\n--- TỔNG KẾT TÀI CHÍNH ---")
    report_lines.append(f"Tổng doanh thu tối đa có thể: {total_available_revenue}")
    report_lines.append(f"Doanh thu đạt được        : {total_revenue_served}")
    report_lines.append(f"Chi phí vận hành          : {total_cost}")
    report_lines.append(f"Lợi nhuận cuối cùng       : {actual_benefit}")
    
    # Xuất ra file txt
    report_filename = tour_file.replace('.txt', '_detailed_report.txt')
    if report_filename == tour_file:
        report_filename = tour_file + "_detailed_report.txt"
        
    with open(report_filename, 'w', encoding='utf-8') as f:
        f.write('\n'.join(report_lines))
        
    print(f"\nĐã xuất báo cáo chi tiết ra file: {report_filename}")
        
    return is_valid

if __name__ == '__main__':
    input_f = sys.argv[1] if len(sys.argv) > 1 else 'test_1.txt'
    tour_f = sys.argv[2] if len(sys.argv) > 2 else 'ortools_tour.txt'
    validate_tour(input_f, tour_f)
