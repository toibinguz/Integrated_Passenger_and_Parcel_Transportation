import sys
import os
import io
import time
import argparse
from ortools.linear_solver import pywraplp

if sys.stdout.encoding != 'utf-8':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
if sys.stderr.encoding != 'utf-8':
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

def read_file_tokens(filepath):
    """Đọc file hỗ trợ cả UTF-16 lẫn UTF-8."""
    content = ""
    try:
        with open(filepath, 'r', encoding='utf-16') as f:
            content = f.read()
    except Exception:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
    tokens = []
    for line in content.splitlines():
        if '#' in line:
            line = line[:line.index('#')]
        tokens.extend(line.split())
    return tokens

def load_instance(filepath):
    tokens = read_file_tokens(filepath)
    idx = 0
    K, N, M = int(tokens[idx]), int(tokens[idx+1]), int(tokens[idx+2])
    idx += 3
    V = 2 * N + 2 * M + K

    O = [0] * (K + 1)
    Q = [0] * (K + 1)
    for k in range(1, K + 1):
        O[k] = int(tokens[idx])
        Q[k] = int(tokens[idx+1])
        idx += 2

    reqs = {}
    vertex_to_req = {}
    for i in range(1, N + 1):
        P, D, E, L, S, Rev = map(int, tokens[idx:idx+6])
        idx += 6
        reqs[i] = {
            'type': 1, 'P': P, 'D': D, 'W': 0,
            'E': E, 'L': L, 'Ed': E, 'Ld': 10**9,
            'S': S, 'Rev': Rev
        }
        vertex_to_req[P] = ('P', i)
        vertex_to_req[D] = ('D', i)

    for j in range(1, M + 1):
        r = N + j
        P, D, W, E, L, Ed, Ld, S, Rev = map(int, tokens[idx:idx+9])
        idx += 9
        reqs[r] = {
            'type': 2, 'P': P, 'D': D, 'W': W,
            'E': E, 'L': L, 'Ed': Ed, 'Ld': Ld,
            'S': S, 'Rev': Rev
        }
        vertex_to_req[P] = ('P', r)
        vertex_to_req[D] = ('D', r)

    t_mat = [[0] * (V + 1) for _ in range(V + 1)]
    for i in range(1, V + 1):
        for j in range(1, V + 1):
            t_mat[i][j] = int(tokens[idx])
            idx += 1

    c_mat = [[0] * (V + 1) for _ in range(V + 1)]
    for i in range(1, V + 1):
        for j in range(1, V + 1):
            c_mat[i][j] = int(tokens[idx])
            idx += 1

    return {
        'K': K, 'N': N, 'M': M, 'V': V,
        'O': O, 'Q': Q, 'reqs': reqs,
        'vertex_to_req': vertex_to_req,
        't_mat': t_mat, 'c_mat': c_mat
    }

def load_hint_solution(filepath, inst):
    tokens = read_file_tokens(filepath)
    if not tokens:
        return None
    declared_obj = int(tokens[0])
    idx = 1
    routes = {}
    K = inst['K']
    for k in range(1, K + 1):
        length = int(tokens[idx])
        idx += 1
        rt = [int(tokens[idx + i]) for i in range(length)]
        idx += length
        routes[k] = rt
    return {'obj': declared_obj, 'routes': routes}

def simulate_route(rt, k, inst):
    """Tính toán dòng thời gian, tải trọng, lợi nhuận của lộ trình."""
    t_mat = inst['t_mat']
    c_mat = inst['c_mat']
    reqs = inst['reqs']
    v2r = inst['vertex_to_req']
    O_k = inst['O'][k]

    t_now = 0
    load = 0
    curr = O_k
    times = {}
    loads = {}
    rev = 0
    cost = 0

    times[O_k] = 0
    loads[O_k] = 0

    for v in rt:
        t_now += t_mat[curr][v]
        cost += c_mat[curr][v]
        ntype, r = v2r[v]
        d = reqs[r]
        if ntype == 'P':
            t_now = max(t_now, d['E'])
            load += d['W']
        else:
            if d['type'] == 2:
                t_now = max(t_now, d['Ed'])
                load -= d['W']
            rev += d['Rev']
        times[v] = t_now
        loads[v] = load
        t_now += d['S']
        curr = v

    return {
        'times': times,
        'loads': loads,
        'rev': rev,
        'cost': cost,
        'profit': rev - cost
    }

def build_and_solve_milp(instance_file, hint_file=None, time_limit_sec=300, max_neighbors=25, out_file=None):
    print("=" * 80)
    print("      MILP SOLVER CHO BÀI TOÁN DARP/PDP VỚI SCIP (OR-TOOLS)")
    print("=" * 80)
    print(f"  * Instance file    : {instance_file}")
    print(f"  * Hint file        : {hint_file}")
    print(f"  * Max Neighbors    : {max_neighbors}")
    print(f"  * Time Limit       : {time_limit_sec}s")
    print("=" * 80)

    t0 = time.time()
    inst = load_instance(instance_file)
    K, N, M, V = inst['K'], inst['N'], inst['M'], inst['V']
    reqs = inst['reqs']
    v2r = inst['vertex_to_req']
    t_mat = inst['t_mat']
    c_mat = inst['c_mat']
    O = inst['O']
    Q = inst['Q']

    SINK = V + 1

    # 1. Đọc và phân tích Warm-start Hint
    hint_data = None
    hint_edges_by_k = {k: set() for k in range(1, K + 1)}
    hint_served_by_k = {k: set() for k in range(1, K + 1)}
    hint_times = {}
    hint_loads = {}

    if hint_file and os.path.exists(hint_file):
        hint_data = load_hint_solution(hint_file, inst)
        if hint_data:
            print(f"[i] Đã nạp Hint Solution: Objective = {hint_data['obj']}")
            total_calc_obj = 0
            for k in range(1, K + 1):
                rt = hint_data['routes'][k]
                sim = simulate_route(rt, k, inst)
                total_calc_obj += sim['profit']
                curr = O[k]
                hint_times[curr] = 0
                hint_loads[(curr, k)] = 0
                for v in rt:
                    hint_edges_by_k[k].add((curr, v))
                    hint_times[v] = sim['times'][v]
                    hint_loads[(v, k)] = sim['loads'][v]
                    _, r = v2r[v]
                    hint_served_by_k[k].add(r)
                    curr = v
                hint_edges_by_k[k].add((curr, SINK))
            print(f"[+] Kiểm định Hint Solution nội bộ: {total_calc_obj} điểm.")

    # 2. Xây dựng tập cung khả thi (Feasible Arc Set)
    earliest_dep = [0] * (V + 1)
    latest_arr = [10**9] * (V + 1)
    for r, d in reqs.items():
        earliest_dep[d['P']] = d['E'] + d['S']
        latest_arr[d['P']] = d['L']
        if d['type'] == 1:
            arr_d = max(d['E'] + d['S'] + t_mat[d['P']][d['D']], d['Ed'])
            earliest_dep[d['D']] = arr_d + d['S']
            latest_arr[d['D']] = 10**9
        else:
            earliest_dep[d['D']] = d['Ed'] + d['S']
            latest_arr[d['D']] = d['Ld']

    def is_arc_time_feasible(u, v):
        if u > V or v > V:
            return True
        return earliest_dep[u] + t_mat[u][v] <= latest_arr[v]

    # Phân loại đỉnh
    passenger_pickups = {d['P'] for r, d in reqs.items() if d['type'] == 1}
    passenger_dropoffs = {d['D'] for r, d in reqs.items() if d['type'] == 1}

    # Sinh danh sách cung ứng viên cho từng xe k
    candidate_arcs_by_k = {k: set() for k in range(1, K + 1)}

    for k in range(1, K + 1):
        # 1. Luôn thêm tất cả các cung từ Hint
        for (u, v) in hint_edges_by_k[k]:
            candidate_arcs_by_k[k].add((u, v))

        # 2. Cung từ Depot O[k]
        depot_targets = []
        for v in range(1, V + 1):
            if v in passenger_dropoffs:
                continue
            if v in v2r:
                _, r = v2r[v]
                if reqs[r]['W'] > Q[k]:
                    continue
                if is_arc_time_feasible(O[k], v):
                    depot_targets.append((c_mat[O[k]][v], v))
        depot_targets.sort()
        for _, v in depot_targets[:max_neighbors]:
            candidate_arcs_by_k[k].add((O[k], v))
        candidate_arcs_by_k[k].add((O[k], SINK))

        # 3. Cung giữa các đỉnh request
        for u in range(1, V + 1):
            if u not in v2r:
                continue
            _, r_u = v2r[u]
            if reqs[r_u]['W'] > Q[k]:
                continue

            # Quy tắc Passenger: P[r] CHỈ ĐI ĐẾN D[r]
            if u in passenger_pickups:
                candidate_arcs_by_k[k].add((u, reqs[r_u]['D']))
                continue

            # Từ u (Dropoff khách hoặc Pickup/Dropoff hàng), tìm các đỉnh tiếp theo
            targets = []
            for v in range(1, V + 1):
                if u == v:
                    continue
                if v in passenger_dropoffs:
                    continue
                if v in v2r:
                    _, r_v = v2r[v]
                    if reqs[r_v]['W'] > Q[k]:
                        continue
                    if is_arc_time_feasible(u, v):
                        targets.append((c_mat[u][v], v))
            targets.sort()
            for _, v in targets[:max_neighbors]:
                candidate_arcs_by_k[k].add((u, v))

            # Luôn có thể kết thúc lộ trình về SINK
            candidate_arcs_by_k[k].add((u, SINK))

    total_vars_est = sum(len(candidate_arcs_by_k[k]) for k in range(1, K + 1))
    print(f"[+] Số cung ứng viên tổng cộng sau khi lọc: {total_vars_est}")

    # 3. Khởi tạo SCIP Solver
    solver = pywraplp.Solver.CreateSolver('SCIP')
    if not solver:
        print("[-] Không thể khởi tạo SCIP solver!")
        return

    # Tắt log SCIP thừa thãi hoặc bật tùy chọn
    solver.EnableOutput()
    solver.SetTimeLimit(int(time_limit_sec * 1000))

    # 4. Tạo biến quyết định
    x = {} # x[u, v, k]
    for k in range(1, K + 1):
        for (u, v) in candidate_arcs_by_k[k]:
            x[u, v, k] = solver.BoolVar(f"x_{u}_{v}_{k}")

    y = {} # y[r, k]
    for r in range(1, N + M + 1):
        for k in range(1, K + 1):
            if reqs[r]['W'] <= Q[k]:
                y[r, k] = solver.BoolVar(f"y_{r}_{k}")

    # Biến thời gian T[u]
    T = {}
    for u in range(1, V + 1):
        if u in v2r:
            _, r = v2r[u]
            d = reqs[r]
            if u == d['P']:
                T[u] = solver.NumVar(d['E'], d['L'], f"T_{u}")
            else:
                if d['type'] == 1:
                    T[u] = solver.NumVar(d['E'], 10**6, f"T_{u}")
                else:
                    T[u] = solver.NumVar(d['Ed'], d['Ld'], f"T_{u}")
    for k in range(1, K + 1):
        T[O[k]] = solver.NumVar(0, 0, f"T_depot_{k}")
    T[SINK] = solver.NumVar(0, 10**6, f"T_sink")

    # Biến tải trọng L[u, k]
    L = {}
    for k in range(1, K + 1):
        for u in range(1, V + 1):
            if u in v2r:
                L[u, k] = solver.NumVar(0, Q[k], f"L_{u}_{k}")
        L[O[k], k] = solver.NumVar(0, 0, f"L_depot_{k}")

    print(f"[+] Đã tạo biến: {solver.NumVariables()} biến.")

    # 5. Ràng buộc (Constraints)

    # C1: Mỗi request phục vụ tối đa 1 lần
    for r in range(1, N + M + 1):
        valid_k = [k for k in range(1, K + 1) if (r, k) in y]
        solver.Add(sum(y[r, k] for k in valid_k) <= 1)

    # C2: Depot Flow cho mỗi xe k
    for k in range(1, K + 1):
        out_depot = [x[O[k], v, k] for (u, v) in candidate_arcs_by_k[k] if u == O[k]]
        in_sink = [x[u, SINK, k] for (u, v) in candidate_arcs_by_k[k] if v == SINK]
        if out_depot:
            solver.Add(sum(out_depot) <= 1)
            solver.Add(sum(in_sink) == sum(out_depot))

    # C3: Bảo toàn luồng và ghép nối y[r, k]
    for k in range(1, K + 1):
        # Tập các đỉnh có thể đến
        visited_nodes = set()
        for (u, v) in candidate_arcs_by_k[k]:
            if u <= V and u not in [O[j] for j in range(1, K + 1)]:
                visited_nodes.add(u)
            if v <= V:
                visited_nodes.add(v)

        for u in visited_nodes:
            in_arcs = [x[p, u, k] for (p, q) in candidate_arcs_by_k[k] if q == u]
            out_arcs = [x[u, q, k] for (p, q) in candidate_arcs_by_k[k] if p == u]
            solver.Add(sum(in_arcs) == sum(out_arcs))

        # Ghép nối phục vụ
        for r in range(1, N + M + 1):
            if (r, k) not in y:
                continue
            d = reqs[r]
            p_node = d['P']
            d_node = d['D']

            in_p = [x[u, p_node, k] for (u, v) in candidate_arcs_by_k[k] if v == p_node]
            solver.Add(sum(in_p) == y[r, k])

            if d['type'] == 1:
                # Passenger: p_node -> d_node là cung trực tiếp
                if (p_node, d_node) in candidate_arcs_by_k[k]:
                    solver.Add(x[p_node, d_node, k] == y[r, k])
            else:
                # Parcel: dropoff node cũng có in_flow = y[r, k]
                in_d = [x[u, d_node, k] for (u, v) in candidate_arcs_by_k[k] if v == d_node]
                solver.Add(sum(in_d) == y[r, k])

    # C4: Ràng buộc thời gian (Time Windows & Precedence)
    for k in range(1, K + 1):
        for (u, v) in candidate_arcs_by_k[k]:
            if v == SINK:
                continue
            s_u = reqs[v2r[u][1]]['S'] if u in v2r else 0
            t_uv = t_mat[u][v]
            # Big-M
            l_u = reqs[v2r[u][1]]['L'] if u in v2r else 0
            e_v = reqs[v2r[v][1]]['E'] if v in v2r else 0
            M_uv = max(0, l_u + s_u + t_uv - e_v + 1000)
            solver.Add(T[v] >= T[u] + s_u + t_uv - M_uv * (1 - x[u, v, k]))

    # Precedence: D[r] sau P[r]
    for r in range(1, N + M + 1):
        d = reqs[r]
        s_p = d['S']
        t_pd = t_mat[d['P']][d['D']]
        solver.Add(T[d['D']] >= T[d['P']] + s_p + t_pd)

    # C5: Tải trọng xe (Capacity Propagation cho Parcel)
    for k in range(1, K + 1):
        for (u, v) in candidate_arcs_by_k[k]:
            if u == O[k] or v == SINK:
                continue
            if (u, k) not in L or (v, k) not in L:
                continue
            # Lượng tải thay đổi tại v
            _, r_v = v2r[v]
            d_v = reqs[r_v]
            delta_w = d_v['W'] if v == d_v['P'] else -d_v['W']
            solver.Add(L[v, k] >= L[u, k] + delta_w - Q[k] * (1 - x[u, v, k]))

    # Điểm xuất phát của xe có tải trọng ban đầu
    for k in range(1, K + 1):
        for (u, v) in candidate_arcs_by_k[k]:
            if u == O[k] and v != SINK and (v, k) in L:
                _, r_v = v2r[v]
                d_v = reqs[r_v]
                delta_w = d_v['W'] if v == d_v['P'] else -d_v['W']
                solver.Add(L[v, k] >= delta_w - Q[k] * (1 - x[u, v, k]))

    # 6. Hàm mục tiêu (Objective Function)
    obj_expr = []
    # Doanh thu
    for r in range(1, N + M + 1):
        rev = reqs[r]['Rev']
        valid_k = [k for k in range(1, K + 1) if (r, k) in y]
        for k in valid_k:
            obj_expr.append(rev * y[r, k])

    # Chi phí di chuyển
    for k in range(1, K + 1):
        for (u, v) in candidate_arcs_by_k[k]:
            if v == SINK:
                continue
            cost = c_mat[u][v]
            obj_expr.append(-cost * x[u, v, k])

    solver.Maximize(sum(obj_expr))
    print(f"[+] Đã thêm {solver.NumConstraints()} ràng buộc. Mô hình hoàn tất trong {time.time() - t0:.2f}s!")

    # 7. Nhúng Warm-Start Solution (Hint)
    if hint_data:
        print("[*] Đang nạp Primal Hint vào SCIP...")
        hint_vars = []
        hint_vals = []

        # Hints cho x[u, v, k]
        for k in range(1, K + 1):
            for (u, v) in candidate_arcs_by_k[k]:
                val = 1.0 if (u, v) in hint_edges_by_k[k] else 0.0
                hint_vars.append(x[u, v, k])
                hint_vals.append(val)

        # Hints cho y[r, k]
        for r in range(1, N + M + 1):
            for k in range(1, K + 1):
                if (r, k) in y:
                    val = 1.0 if r in hint_served_by_k[k] else 0.0
                    hint_vars.append(y[r, k])
                    hint_vals.append(val)

        # Hints cho T[u]
        for u in range(1, V + 1):
            if u in hint_times and u in T:
                hint_vars.append(T[u])
                hint_vals.append(float(hint_times[u]))

        # Hints cho L[u, k]
        for (u, k), load_val in hint_loads.items():
            if (u, k) in L:
                hint_vars.append(L[u, k])
                hint_vals.append(float(load_val))

        solver.SetHint(hint_vars, hint_vals)
        print(f"[+] Đã truyền thành công {len(hint_vars)} giá trị Hint vào solver!")

    # 8. Giải bài toán với SCIP
    print("\n" + "=" * 80)
    print("      BẮT ĐẦU GIẢI VỚI SCIP (BẤM CTRL+C NẾU MUỐN DỪNG SỚM)...")
    print("=" * 80)
    solve_start = time.time()
    status = solver.Solve()
    solve_time = time.time() - solve_start

    print("\n" + "=" * 80)
    print("      KẾT QUẢ TÌM KIẾM CỦA SCIP")
    print("=" * 80)
    print(f"  * Thời gian giải  : {solve_time:.2f}s")
    if status == pywraplp.Solver.OPTIMAL:
        print("  * Trạng thái giải : TỐI ƯU TOÀN CỤC (OPTIMAL)!")
    elif status == pywraplp.Solver.FEASIBLE:
        print("  * Trạng thái giải : ĐÃ TÌM THẤY NGHIỆM KHẢ THI (FEASIBLE / TIME LIMIT)!")
    else:
        print(f"  * Trạng thái giải : Không tìm thấy nghiệm (Status = {status})")
        return

    milp_obj = solver.Objective().Value()
    print(f"  * Objective SCIP  : {milp_obj:.2f}")

    # 9. Tái tạo lộ trình nghiệm
    sol_routes = {}
    for k in range(1, K + 1):
        # Tìm đường đi từ O[k] đến SINK
        route_nodes = []
        curr = O[k]
        visited = set()
        while curr != SINK and curr not in visited:
            visited.add(curr)
            next_node = None
            for (p, q) in candidate_arcs_by_k[k]:
                if p == curr and x[p, q, k].solution_value() > 0.5:
                    next_node = q
                    break
            if next_node is None or next_node == SINK:
                break
            route_nodes.append(next_node)
            curr = next_node
        sol_routes[k] = route_nodes

    # 10. Xuất file kết quả
    if not out_file:
        out_file = "scip_solution.txt"
    with open(out_file, "w") as f:
        f.write(f"{int(round(milp_obj))}\n")
        for k in range(1, K + 1):
            rt = sol_routes[k]
            f.write(f"{len(rt)}")
            for v in rt:
                f.write(f" {v}")
            f.write("\n")
    print(f"[+] Đã lưu nghiệm vào: {out_file}")

    # 11. Kiểm tra lại bằng validator nội bộ
    val_obj = 0
    for k in range(1, K + 1):
        rt = sol_routes[k]
        if rt:
            sim = simulate_route(rt, k, inst)
            val_obj += sim['profit']
    print(f"  * Objective xác thực lại: {val_obj}")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="MILP SCIP Solver for DARP/PDP")
    parser.add_argument("--instance", type=str, default="testcases/test_500.txt", help="Đường dẫn file đề bài")
    parser.add_argument("--hint", type=str, default="best_tour_test_500.txt", help="Đường dẫn file hint best tour")
    parser.add_argument("--time_limit", type=int, default=1800, help="Thời gian tối đa chạy solver (giây, mặc định 1800s = 30 phút)")
    parser.add_argument("--neighbors", type=int, default=20, help="Số láng giềng ứng viên tối đa cho mỗi đỉnh")
    parser.add_argument("--output", type=str, default="scip_best_500_result.txt", help="File lưu nghiệm xuất ra")
    args = parser.parse_args()

    build_and_solve_milp(args.instance, args.hint, args.time_limit, args.neighbors, args.output)
