import copy
from models import Evaluator

VDLS_DEPTH_MAX = 5
VDLS_TOP_K = 10
VDLS_GAIN_FLOOR = -200
VDLS_LOG_DEPTH = 1

def _calc_cost(prev_node, target_node, next_node, data):
    """Tính chi phí đoạn đường prev -> target -> next (Xử lý Open VRP khi next là Depot)"""
    cost = data.cost_matrix[prev_node.id][target_node.id]
    if next_node.type != 'DEPOT':
        cost += data.cost_matrix[target_node.id][next_node.id]
    return cost

def _check_feasibility_O1(route, pos, new_node, data):
    """
    Kiểm tra nhanh O(1) xem việc thay thế route.nodes[pos] bằng new_node có khả thi không.
    - Không thay đổi số lượng khách -> Load không đổi -> Capacity luôn thỏa mãn (load khách = 0)
    - Chỉ cần check ràng buộc thời gian (Time Windows).
    """
    prev_node = route.nodes[pos - 1]
    next_node = route.nodes[pos + 1]

    # 1. Thời gian đến new_node
    # dep_time[pos-1] đã có sẵn (được tính từ update_states)
    arr_new = max(new_node.e, route.dep_time[pos - 1] + data.time_matrix[prev_node.id][new_node.id])
    if arr_new > new_node.l:
        return False

    # 2. Thời gian rời new_node (bao gồm nội thời gian của node khách)
    dep_new = arr_new + new_node.duration

    # 3. Thời gian đến next_node
    arr_next = dep_new + data.time_matrix[new_node.id][next_node.id]

    # 4. Kiểm tra độ trễ (delay push)
    # route.arr_time[pos + 1] là thời gian đến cũ của next_node
    delta_time = arr_next - route.arr_time[pos + 1]
    
    # Nếu delta_time > thời gian chờ + dung sai tối đa của next_node -> Vi phạm Time Window
    if delta_time > route.wait_time[pos + 1] + route.max_delay[pos + 1]:
        return False

    return True

def _vdls_dfs(solver, seed_node, target_route, target_pos, target_orig_node, acc_gain, visited_routes, depth, routes, data, log_lines):
    prev_node = target_route.nodes[target_pos - 1]
    next_node = target_route.nodes[target_pos + 1]
    c_old = _calc_cost(prev_node, target_orig_node, next_node, data)
    
    # --- DỪNG 1: THỬ ĐÓNG VÒNG (CLOSE CYCLE) ---
    if depth >= 1:
        c_close = _calc_cost(prev_node, seed_node, next_node, data)
        close_gain = c_old - c_close
        total_gain = acc_gain + close_gain
        
        if total_gain > 1e-4:
            if _check_feasibility_O1(target_route, target_pos, seed_node, data):
                target_route.nodes[target_pos] = seed_node
                target_route.update_states(data)
                step_desc = f"Đóng vòng: Xe {target_route.vehicle_id} nhận lại Seed [Req {seed_node.request_id}] thay cho [Req {target_orig_node.request_id}] (g_close={close_gain:+.0f}, G_total={total_gain:+.0f})"
                return True, total_gain, depth, [step_desc]

    # --- DỪNG 2: GIỚI HẠN ĐỘ SÂU ---
    if depth >= VDLS_DEPTH_MAX:
        return False, 0, 0, []

    # --- BƯỚC 2: TÌM CANDIDATE B2 ĐỂ ĐIỀN VÀO LỖ HỔNG ---
    raw_candidates = []
    for r_idx, cand_route in enumerate(routes):
        if r_idx in visited_routes:
            continue
        
        for cand_pos in range(1, len(cand_route.nodes) - 1):
            cand_node = cand_route.nodes[cand_pos]
            if cand_node.type != 'PASSENGER':
                continue
            
            c_new = _calc_cost(prev_node, cand_node, next_node, data)
            gain = c_old - c_new
            
            if acc_gain + gain >= VDLS_GAIN_FLOOR:
                if _check_feasibility_O1(target_route, target_pos, cand_node, data):
                    raw_candidates.append((gain, cand_pos, cand_node, r_idx, cand_route))
                    
    # Sắp xếp candidate theo Gain giảm dần
    raw_candidates.sort(key=lambda x: x[0], reverse=True)
    
    do_log = (depth <= VDLS_LOG_DEPTH)
    if do_log:
        log_lines.append(f"  [D={depth}] Hổng=[Req {target_orig_node.request_id}] Xe {target_route.vehicle_id} | acc_gain={acc_gain:+.0f} | {len(raw_candidates)} cands hợp lệ, thử top {VDLS_TOP_K}:")

    # --- BƯỚC 3: ĐỆ QUY XUỐNG CANDIDATE ---
    for gain, cand_pos, cand_node, r_idx, cand_route in raw_candidates[:VDLS_TOP_K]:
        # Tạm thời điền cand_node vào target_route
        target_route.nodes[target_pos] = cand_node
        target_route.update_states(data)
        
        if do_log:
            log_lines.append(f"    Thử điền [Req {cand_node.request_id}] từ Xe {cand_route.vehicle_id} (gain={gain:+.0f}) → G_cum={acc_gain + gain:+.0f}")
            
        new_visited = visited_routes | {r_idx}
        
        # Lỗ hổng mới nằm ở cand_route tại cand_pos (chứa cand_node cũ)
        found, c_gain, c_depth, c_path = _vdls_dfs(
            solver, seed_node, cand_route, cand_pos, cand_node, acc_gain + gain,
            new_visited, depth + 1, routes, data, log_lines
        )
        
        if found:
            step_desc = f"Xe {target_route.vehicle_id} nhận [Req {cand_node.request_id}] thay cho [Req {target_orig_node.request_id}] (gain={gain:+.0f})"
            return True, c_gain, c_depth, [step_desc] + c_path
            
        # Backtrack: Trả lại trạng thái cũ cho target_route
        target_route.nodes[target_pos] = target_orig_node
        target_route.update_states(data)

    return False, 0, 0, []

def run_vdls(solver, routes, data):
    """
    Điểm truy cập của pha VDLS.
    """
    for r_seed_idx, route_seed in enumerate(routes):
        if len(route_seed.nodes) <= 2:
            continue

        for seed_pos in range(1, len(route_seed.nodes) - 1):
            p_seed = route_seed.nodes[seed_pos]
            if p_seed.type != 'PASSENGER':
                continue

            visited = {r_seed_idx}
            log_lines = []
            
            # Lưu lại trạng thái gốc để khôi phục nếu sai lệch
            benefit_before_vdls = sum(r.total_benefit for r in routes)
            saved_routes_state = copy.deepcopy(routes)

            found, cycle_gain, cycle_depth, cycle_path = _vdls_dfs(
                solver,
                p_seed,          # Node để dành đóng vòng
                route_seed,      # Route bị rút
                seed_pos,        # Vị trí bị hổng
                p_seed,          # Orig node (bị mất)
                0,               # acc_gain
                visited,
                0,               # depth
                routes,
                data,
                log_lines
            )

            if found:
                benefit_after = sum(r.total_benefit for r in routes)
                actual_gain = benefit_after - benefit_before_vdls
                
                if actual_gain > 0:
                    if hasattr(solver, 'oropt_clean_routes'):
                        solver.oropt_clean_routes.clear()

                    seed_desc = f"Khởi tạo: Rút [Req {p_seed.request_id}] khỏi Xe {route_seed.vehicle_id} tạo lỗ hổng"
                    full_path = " ==> ".join([seed_desc] + cycle_path)

                    if log_lines:
                        solver.log_and_print("")
                        solver.log_and_print(f"  [VDLS-LOG] Seed=[Req {p_seed.request_id}] Xe {route_seed.vehicle_id}:")
                        for line in log_lines:
                            solver.log_and_print(line)

                    return True, actual_gain, cycle_depth, full_path
                else:
                    # Lỗi tính toán hoặc sai lệch nhỏ, Backtrack lại toàn bộ routes
                    for i in range(len(routes)):
                        routes[i] = saved_routes_state[i]

    return False, 0, 0, []
