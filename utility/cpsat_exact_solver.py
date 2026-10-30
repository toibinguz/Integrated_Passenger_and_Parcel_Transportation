#!/usr/bin/env python3
"""
Exact Set Packing Solver using OR-Tools CP-SAT
Certifies the global optimality of column generation solutions for SOICT 2026.
"""

import sys
import os
import time
import argparse
import subprocess
from pathlib import Path
from ortools.sat.python import cp_model

# Ensure UTF-8 output on Windows
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")


class Column:
    def __init__(self, col_id, k, profit, requests, stops):
        self.id = col_id
        self.k = k
        self.profit = profit
        self.requests = requests
        self.stops = stops


def ensure_column_exporter():
    """Compiles utility/column_exporter.cpp if the executable doesn't exist."""
    base_dir = Path(__file__).resolve().parent
    src = base_dir / "column_exporter.cpp"
    exe = base_dir / ("column_exporter.exe" if os.name == "nt" else "column_exporter")

    if not exe.exists() or exe.stat().st_mtime < src.stat().st_mtime:
        print(f"[CP-SAT] Biên dịch {src.name} -> {exe.name}...")
        cmd = ["g++", "-O3", "-std=c++17", str(src), "-o", str(exe)]
        subprocess.run(cmd, check=True)
    return exe


def export_columns(testcase_path, cols_path=None):
    """Runs column_exporter to generate all feasible columns via DFS."""
    exe = ensure_column_exporter()
    if cols_path is None:
        cols_path = Path("temp_generated_columns.txt")
    
    t0 = time.time()
    cmd = [str(exe), str(testcase_path), str(cols_path)]
    res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    dt = time.time() - t0
    
    if res.returncode != 0:
        print(f"[LỖI] Sinh cột thất bại:\n{res.stderr}", file=sys.stderr)
        sys.exit(1)
        
    print(f"[CP-SAT] Đã sinh toàn bộ cột qua DFS trong {dt:.4f}s. {res.stderr.strip()}")
    return Path(cols_path)


def load_columns(cols_path):
    """Loads columns from file."""
    t0 = time.time()
    cols = []
    with open(cols_path, "r", encoding="utf-8") as f:
        header = f.readline().split()
        if not header:
            raise ValueError("File cột rỗng!")
        K, N, M, R, V, NC = map(int, header)

        col_id = 0
        for line in f:
            parts = list(map(int, line.split()))
            if not parts:
                continue
            k = parts[0]
            profit = parts[1]
            rlen = parts[2]
            reqs = parts[3 : 3 + rlen]
            slen = parts[3 + rlen]
            stops = parts[4 + rlen : 4 + rlen + slen]

            cols.append(Column(col_id, k, profit, reqs, stops))
            col_id += 1

    dt = time.time() - t0
    print(f"[CP-SAT] Nạp {len(cols):,} cột vào bộ nhớ (K={K}, R={R}, V={V}) trong {dt:.3f}s")
    return K, N, M, R, V, cols


def parse_solution_file(sol_path, K):
    """Parses an existing solution file to extract routes per vehicle."""
    routes = {}
    with open(sol_path, "r", encoding="utf-8") as f:
        lines = [l.strip() for l in f if l.strip()]
        if not lines:
            return 0, {}
        reported_obj = int(lines[0])
        for k in range(1, K + 1):
            if k < len(lines):
                parts = list(map(int, lines[k].split()))
                cnt = parts[0]
                stops = parts[1 : 1 + cnt]
                routes[k] = stops
            else:
                routes[k] = []
    return reported_obj, routes


def solve_set_packing(K, R, cols, time_limit=60.0, threads=8, hint_routes=None):
    """Formulates and solves the binary Set Packing problem with OR-Tools CP-SAT."""
    NC = len(cols)
    print("\n" + "=" * 70)
    print(f" KHỞI TẠO MÔ HÌNH TOÁN HỌC EXACT SET PACKING (OR-TOOLS CP-SAT)")
    print(f" Biến nhị phân: {NC:,} | Xe: {K} | Yêu cầu khách/hàng: {R}")
    print(f" Thời gian tối đa: {time_limit}s | Số luồng CPU: {threads}")
    print("=" * 70)

    model = cp_model.CpModel()

    # 1. Decision variables: x[j] in {0, 1}
    x = [model.new_bool_var(f"x_{j}") for j in range(NC)]

    # 2. Inverted index: Vehicle -> Columns
    veh_cols = [[] for _ in range(K + 1)]
    # Inverted index: Request -> Columns
    req_cols = [[] for _ in range(R + 1)]

    for c in cols:
        veh_cols[c.k].append(c.id)
        for r in c.requests:
            req_cols[r].append(c.id)

    # 3. Constraint: At most one column per vehicle (Vehicle Mutex)
    for k in range(1, K + 1):
        if veh_cols[k]:
            model.add_at_most_one([x[j] for j in veh_cols[k]])

    # 4. Constraint: At most one vehicle serves each request (Request Mutex)
    active_req_constraints = 0
    for r in range(1, R + 1):
        if req_cols[r]:
            model.add_at_most_one([x[j] for j in req_cols[r]])
            active_req_constraints += 1

    print(f"[CP-SAT] Đã lập {K} ràng buộc xe và {active_req_constraints} ràng buộc không trùng khách/hàng (add_at_most_one)")

    # 5. Objective: Maximize total profit
    model.maximize(cp_model.LinearExpr.weighted_sum(x, [c.profit for c in cols]))

    # 6. Warm Start (Solution Hint)
    matched_hints = 0
    if hint_routes:
        print("[CP-SAT] Đang ánh xạ nghiệm tham chiếu (primal hint) vào mô hình...")
        for k in range(1, K + 1):
            h_stops = hint_routes.get(k, [])
            if not h_stops:
                continue
            # Find column matching this route
            for j in veh_cols[k]:
                if cols[j].stops == h_stops:
                    model.add_hint(x[j], 1)
                    matched_hints += 1
                    break
        print(f"[CP-SAT] Đã nhúng warm start hint thành công: {matched_hints}/{K} xe khớp chính xác!")

    # 7. Solve
    solver = cp_model.CpSolver()
    solver.parameters.max_time_in_seconds = float(time_limit)
    solver.parameters.num_workers = int(threads)
    solver.parameters.log_search_progress = True

    print("\n[CP-SAT] BẮT ĐẦU GIẢI EXACT SOLVE VỚI ĐỘNG CƠ CP-SAT...")
    t_solve_start = time.time()
    status = solver.solve(model)
    solve_time = time.time() - t_solve_start

    status_name = solver.status_name(status)
    best_obj = int(solver.objective_value) if status in (cp_model.OPTIMAL, cp_model.FEASIBLE) else 0
    best_bound = int(solver.best_objective_bound) if status in (cp_model.OPTIMAL, cp_model.FEASIBLE) else 0
    gap = best_bound - best_obj if status in (cp_model.OPTIMAL, cp_model.FEASIBLE) else -1

    # Extract chosen solution
    chosen_cols = []
    if status in (cp_model.OPTIMAL, cp_model.FEASIBLE):
        for j in range(NC):
            if solver.value(x[j]) == 1:
                chosen_cols.append(cols[j])

    return status_name, best_obj, best_bound, gap, solve_time, chosen_cols


def write_solution(output_path, K, best_obj, chosen_cols):
    """Writes solution in the standard format."""
    veh_route = {k: [] for k in range(1, K + 1)}
    for c in chosen_cols:
        veh_route[c.k] = c.stops

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(f"{best_obj}\n")
        for k in range(1, K + 1):
            stops = veh_route[k]
            f.write(f"{len(stops)}")
            for v in stops:
                f.write(f" {v}")
            f.write("\n")
    print(f"[CP-SAT] Đã ghi file nghiệm chuẩn tại: {output_path}")


def validate_solution(testcase_path, solution_path):
    """Validates the generated solution using utility/validator.py."""
    validator_path = Path(__file__).resolve().parent / "validator.py"
    if not validator_path.exists():
        return
    print("\n" + "-" * 70)
    print(" CHẠY BỘ KIỂM TRA TÍNH ĐÚNG ĐẮN (VALIDATOR)")
    print("-" * 70)
    cmd = [sys.executable, str(validator_path), str(testcase_path), str(solution_path)]
    subprocess.run(cmd)


def main():
    parser = argparse.ArgumentParser(
        description="Exact Set Packing Solver using OR-Tools CP-SAT for SOICT 2026."
    )
    parser.add_argument("testcase", type=str, help="Đường dẫn đến file testcase đề bài (.txt)")
    parser.add_argument("--cols", type=str, default=None, help="File cột đã sinh sẵn (nếu có)")
    parser.add_argument("--time-limit", type=float, default=60.0, help="Giới hạn thời gian chạy CP-SAT (giây)")
    parser.add_argument("--threads", type=int, default=8, help="Số luồng CPU cho CP-SAT (mặc định: 8)")
    parser.add_argument("--hint-solution", type=str, default=None, help="File nghiệm tham chiếu (primal hint) để warm start")
    parser.add_argument("--output", type=str, default=None, help="Đường dẫn file nghiệm xuất ra")
    parser.add_argument("--no-validate", action="store_true", help="Không tự động chạy validator")

    args = parser.parse_args()

    testcase_path = Path(args.testcase)
    if not testcase_path.exists():
        print(f"[LỖI] Không tìm thấy file testcase: {testcase_path}", file=sys.stderr)
        sys.exit(1)

    # 1. Export / Load Columns
    temp_cols_created = False
    if args.cols:
        cols_path = Path(args.cols)
    else:
        cols_path = Path(f"cols_{testcase_path.stem}.txt")
        cols_path = export_columns(testcase_path, cols_path)
        temp_cols_created = True

    K, N, M, R, V, cols = load_columns(cols_path)

    # 2. Parse Hint Solution if provided
    hint_obj = None
    hint_routes = None
    if args.hint_solution:
        hint_path = Path(args.hint_solution)
        if hint_path.exists():
            hint_obj, hint_routes = parse_solution_file(hint_path, K)
            print(f"[CP-SAT] Đã đọc file nghiệm tham chiếu: Obj = {hint_obj:,}")
        else:
            print(f"[CẢNH BÁO] Không tìm thấy file hint: {hint_path}", file=sys.stderr)

    # 3. Solve with CP-SAT
    status, best_obj, best_bound, gap, solve_time, chosen_cols = solve_set_packing(
        K, R, cols, time_limit=args.time_limit, threads=args.threads, hint_routes=hint_routes
    )

    # 4. Summary Report
    print("\n" + "=" * 70)
    print(" BÁO CÁO KẾT QUẢ EXACT SOLVE BẰNG CP-SAT")
    print("=" * 70)
    print(f"• Trạng thái nghiệm (Status):     {status}")
    print(f"• Điểm mục tiêu (Objective):      {best_obj:,}")
    print(f"• Cận trên lý thuyết (Dual Bound): {best_bound:,}")
    print(f"• Khoảng cách tối ưu (Gap):       {gap} điểm ({gap / max(1, best_bound) * 100:.3f}%)")
    print(f"• Thời gian giải CP-SAT:         {solve_time:.3f} giây")
    print(f"• Số xe sử dụng:                 {len(chosen_cols)} / {K}")

    if hint_obj is not None:
        diff = best_obj - hint_obj
        if diff == 0:
            if status == "OPTIMAL":
                print(f"🎉 CHỨNG NHẬN: Nghiệm tham chiếu ({hint_obj:,}) ĐẠT TỐI ƯU TOÀN CỤC CHÍNH XÁC 100%!")
            else:
                print(f"✅ Nghiệm tham chiếu ({hint_obj:,}) khớp với nghiệm tốt nhất hiện tại của CP-SAT.")
        elif diff > 0:
            print(f"🔥 KỶ LỤC MỚI: CP-SAT đã tìm ra nghiệm tốt hơn nghiệm tham chiếu: +{diff} điểm ({hint_obj} -> {best_obj})!")
        else:
            print(f"⚠️ Nghiệm tham chiếu ({hint_obj}) cao hơn CP-SAT ({best_obj}) - cần thêm thời gian chạy.")

    # 5. Output solution file
    output_path = Path(args.output) if args.output else Path(f"sol_cpsat_{testcase_path.stem}.txt")
    write_solution(output_path, K, best_obj, chosen_cols)

    # 6. Validate
    if not args.no_validate:
        validate_solution(testcase_path, output_path)

    # Clean up temp cols file if auto-generated
    if temp_cols_created and cols_path.exists():
        cols_path.unlink()
        print(f"[CP-SAT] Đã dọn dẹp file cột tạm thời: {cols_path.name}")


if __name__ == "__main__":
    main()
