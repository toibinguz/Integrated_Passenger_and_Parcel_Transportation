import matplotlib.pyplot as plt
import re
import os
import sys
import io

# Tự động chuyển console output sang UTF-8 để không bị lỗi font Tiếng Việt trên Windows
if sys.stdout.encoding != 'utf-8':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
if sys.stderr.encoding != 'utf-8':
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

# ==============================================================================
# HÀM ĐỌC TOKENS AN TOÀN (HỖ TRỢ UTF-16, UTF-8 BOM, UTF-8)
# ==============================================================================
def read_file_tokens(filepath):
    if not os.path.exists(filepath):
        return []
    content = ""
    # Nhận diện encoding
    encoding = 'utf-8'
    with open(filepath, "rb") as bf:
        head = bf.read(4)
        if head.startswith(b'\xff\xfe') or head.startswith(b'\xfe\xff'):
            encoding = 'utf-16'
        elif head.startswith(b'\xef\xbb\xbf'):
            encoding = 'utf-8-sig'
            
    with open(filepath, 'r', encoding=encoding, errors='ignore') as f:
        content = f.read()

    tokens = []
    for line in content.splitlines():
        if '#' in line:
            line = line[:line.index('#')]
        tokens.extend(line.split())
    return tokens

# ==============================================================================
# 1. PHÂN TÍCH LOG & VẼ BIỂU ĐỒ (PROGRESSION & MILESTONES)
# ==============================================================================
def parse_and_plot_log(log_file, output_img="tabu_progression.png"):
    if not os.path.exists(log_file):
        print(f"[-] Khong tim thay file log: {log_file}")
        return [], []

    # Nhận diện encoding file log
    encoding = 'utf-8'
    with open(log_file, "rb") as bf:
        head = bf.read(4)
        if head.startswith(b'\xff\xfe') or head.startswith(b'\xfe\xff'):
            encoding = 'utf-16'
        elif head.startswith(b'\xef\xbb\xbf'):
            encoding = 'utf-8-sig'

    with open(log_file, "r", encoding=encoding, errors='ignore') as f:
        lines = f.readlines()

    iterations = []
    best_nb_objs = []
    global_best_objs = []
    valid_neighbors = []
    tabu_hits = []

    cur_iter = None
    cur_nb = None
    cur_valid = 0
    cur_tabu = 0
    last_ruin_iter = None

    milestones = []
    prev_global = 0

    for i, line in enumerate(lines):
        if any(err in line for err in ["CategoryInfo", "FullyQualifiedErrorId", "NativeCommandError"]):
            continue

        # Ghi nhận kích hoạt Ruin
        if "[RUIN ACTIVATED]" in line:
            last_ruin_iter = cur_iter

        # Ghi nhận đầu iteration
        m_iter = re.search(r"\[Iter\s+(\d+)\]\s*Valid Neighbors:\s*(\d+)\s*\|\s*Tabu Hits:\s*(\d+)", line)
        if m_iter:
            cur_iter = int(m_iter.group(1))
            cur_valid = int(m_iter.group(2))
            cur_tabu = int(m_iter.group(3))
            cur_nb = None
            continue

        # Best Neighbor Obj
        m_nb = re.search(r"-> Best Neighbor Obj:\s*(-?\d+)", line)
        if m_nb:
            cur_nb = int(m_nb.group(1))
            continue

        # Global Best Record Break
        if "*** NEW GLOBAL BEST FOUND! ***" in line:
            # Tìm dòng Global Best tiếp theo
            for next_line in lines[i:i+3]:
                m_g = re.search(r"-> Global Best Obj:\s*(-?\d+)", next_line)
                if m_g:
                    val = int(m_g.group(1))
                    if val > prev_global:
                        milestones.append({
                            'iter': cur_iter,
                            'old_val': prev_global,
                            'new_val': val,
                            'delta': val - prev_global,
                            'last_ruin': last_ruin_iter
                        })
                        prev_global = val
                    break

        # Đóng gói kết quả iteration
        m_glob = re.search(r"-> Global Best Obj:\s*(-?\d+)", line)
        if m_glob and cur_iter is not None:
            glob_val = int(m_glob.group(1))
            iterations.append(cur_iter)
            global_best_objs.append(glob_val)
            best_nb_objs.append(cur_nb if cur_nb is not None else float('nan'))
            valid_neighbors.append(cur_valid)
            tabu_hits.append(cur_tabu)
            cur_iter = None

    if not iterations:
        print("[-] Khong co du lieu iteration hop le trong log!")
        return [], []

    # Vẽ đồ thị 2 tầng
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 8), sharex=True, gridspec_kw={'height_ratios': [2.5, 1]})

    ax1.plot(iterations, best_nb_objs, label='Best Neighbor Obj', color='dodgerblue', alpha=0.5, linewidth=1)
    ax1.plot(iterations, global_best_objs, label='Global Best Obj', color='crimson', linewidth=2.5)

    # Đánh dấu các mốc đột phá (Milestones) quan trọng
    if milestones:
        ms_iters = [m['iter'] for m in milestones]
        ms_vals = [m['new_val'] for m in milestones]
        ax1.scatter(ms_iters, ms_vals, color='gold', edgecolor='black', s=50, zorder=5, label='Breakthrough Point')

    ax1.set_ylabel('Objective Value (Profit)', fontsize=12, fontweight='bold')
    ax1.set_title('Tien trinh Hoi tu va Cac Moc But pha cua Tabu Search', fontsize=14, fontweight='bold')
    ax1.legend(loc='lower right', frameon=True, shadow=True)
    ax1.grid(True, linestyle='--', alpha=0.6)

    ax2.plot(iterations, valid_neighbors, label='Valid Neighbors', color='mediumseagreen', linewidth=1.2)
    ax2.plot(iterations, tabu_hits, label='Tabu Hits', color='coral', linewidth=1, linestyle=':')
    ax2.set_xlabel('Iteration', fontsize=12, fontweight='bold')
    ax2.set_ylabel('Count', fontsize=12, fontweight='bold')
    ax2.legend(loc='upper right', frameon=True)
    ax2.grid(True, linestyle='--', alpha=0.6)

    plt.tight_layout()
    plt.savefig(output_img, dpi=200)
    print(f"[+] Da ve bieu do tien trinh va luu tai: {output_img}")

    return iterations, milestones

# ==============================================================================
# 2. BÁO CÁO CHI TIẾT TỪNG NODE TRÊN LỘ TRÌNH (TELEMETRY REPORT)
# ==============================================================================
def generate_detailed_tour_report(instance_file, solution_file, milestones, report_file="tour_report.txt"):
    inst_tokens = read_file_tokens(instance_file)
    sol_tokens = read_file_tokens(solution_file)

    if not inst_tokens or not sol_tokens:
        print("[-] Khong the doc file de bai hoac file ket qua de tao report!")
        return

    # Parse instance
    idx = 0
    K = int(inst_tokens[idx]); N = int(inst_tokens[idx+1]); M = int(inst_tokens[idx+2]); idx += 3
    V = 2 * N + 2 * M + K

    O = [0] * (K + 1)
    Q = [0] * (K + 1)
    for k in range(1, K + 1):
        O[k] = int(inst_tokens[idx]); Q[k] = int(inst_tokens[idx+1]); idx += 2

    reqs = {}
    vertex_to_req = {}
    for i in range(1, N + 1):
        P = int(inst_tokens[idx]); D = int(inst_tokens[idx+1]); E = int(inst_tokens[idx+2])
        L = int(inst_tokens[idx+3]); S = int(inst_tokens[idx+4]); Rev = int(inst_tokens[idx+5])
        idx += 6
        reqs[i] = {'type': 1, 'P': P, 'D': D, 'W': 0, 'E': E, 'L': L, 'Ed': E, 'Ld': L, 'S': S, 'Rev': Rev}
        vertex_to_req[P] = ('P', i)
        vertex_to_req[D] = ('D', i)

    for j in range(1, M + 1):
        r = N + j
        P = int(inst_tokens[idx]); D = int(inst_tokens[idx+1]); W = int(inst_tokens[idx+2])
        E = int(inst_tokens[idx+3]); L = int(inst_tokens[idx+4]); Ed = int(inst_tokens[idx+5])
        Ld = int(inst_tokens[idx+6]); S = int(inst_tokens[idx+7]); Rev = int(inst_tokens[idx+8])
        idx += 9
        reqs[r] = {'type': 2, 'P': P, 'D': D, 'W': W, 'E': E, 'L': L, 'Ed': Ed, 'Ld': Ld, 'S': S, 'Rev': Rev}
        vertex_to_req[P] = ('P', r)
        vertex_to_req[D] = ('D', r)

    t_mat = [[0] * (V + 1) for _ in range(V + 1)]
    for i in range(1, V + 1):
        for j in range(1, V + 1):
            t_mat[i][j] = int(inst_tokens[idx]); idx += 1

    c_mat = [[0] * (V + 1) for _ in range(V + 1)]
    for i in range(1, V + 1):
        for j in range(1, V + 1):
            c_mat[i][j] = int(inst_tokens[idx]); idx += 1

    # Parse solution
    declared_obj = int(sol_tokens[0])
    s_idx = 1
    routes = []
    for k in range(1, K + 1):
        length = int(sol_tokens[s_idx]); s_idx += 1
        rt = []
        for _ in range(length):
            rt.append(int(sol_tokens[s_idx])); s_idx += 1
        routes.append(rt)

    # Xây dựng báo cáo
    report_lines = []
    report_lines.append("=" * 115)
    report_lines.append("                         BÁO CÁO PHÂN TÍCH CHI TIẾT LỘ TRÌNH VÀ TIẾN TRÌNH TABU SEARCH")
    report_lines.append("=" * 115)
    report_lines.append(f"  * File Đề bài: {instance_file}")
    report_lines.append(f"  * File Nghiệm: {solution_file}")
    report_lines.append(f"  * Tổng số xe: {K} | Hành khách (N): {N} | Hàng hóa (M): {M} | Tổng điểm: {declared_obj}")
    report_lines.append("=" * 115 + "\n")

    # MỤC 1: CÁC MỐC ĐỘT PHÁ GLOBAL BEST (MILESTONES)
    report_lines.append("---------------------------------------------------------------------------------------------------")
    report_lines.append("1. NHẬT KÝ CÁC THẾ HỆ THAY ĐỔI KỶ LỤC TOÀN CỤC (GLOBAL BEST BREAKTHROUGHS)")
    report_lines.append("---------------------------------------------------------------------------------------------------")
    report_lines.append(f"{'Mốc #':<7} | {'Iteration':<10} | {'Điểm Cũ':<10} -> {'Điểm Mới':<10} | {'Mức Tăng':<10} | {'Liên quan đến Ruin?':<35}")
    report_lines.append("-" * 95)

    for idx_m, m in enumerate(milestones, 1):
        ruin_note = "Khởi tạo / Ban đầu"
        if m['last_ruin'] is not None:
            dist = m['iter'] - m['last_ruin']
            if dist == 0:
                ruin_note = f"Ngay tại vòng Ruin (Iter {m['last_ruin']})"
            elif dist <= 50:
                ruin_note = f"Sau Ruin {dist} vòng (Ruin tại Iter {m['last_ruin']})"
            else:
                ruin_note = f"Khai thác sâu (cách Ruin {dist} vòng)"
        report_lines.append(f"{idx_m:<7} | {m['iter']:<10} | {m['old_val']:<10} -> {m['new_val']:<10} | +{m['delta']:<9} | {ruin_note:<35}")
    report_lines.append("\n")

    # MỤC 2: TELEMETRY CHI TIẾT TỪNG NODE CỦA TỪNG XE
    report_lines.append("---------------------------------------------------------------------------------------------------")
    report_lines.append("2. BẢNG THEO DÕI HÀNH TRÌNH TỪNG ĐỈNH (NODE-LEVEL TELEMETRY) CỦA TOÀN BỘ ĐOÀN XE")
    report_lines.append("---------------------------------------------------------------------------------------------------")

    total_fleet_cost = 0
    total_fleet_rev = 0
    total_fleet_pass = 0
    total_fleet_parc = 0
    active_vehicles = 0

    for k in range(1, K + 1):
        rt = routes[k - 1]
        depot = O[k]
        cap = Q[k]

        report_lines.append(f"\n🚗 [XE #{k}] - Điểm xuất phát (Depot): Node {depot} | Sức chứa tối đa: {cap} kg")
        if not rt:
            report_lines.append("   -> Xe không hoạt động (0 đơn hàng).\n")
            continue

        active_vehicles += 1
        report_lines.append(f"{'Thứ tự':<6} | {'Node':<6} | {'Hành động & Đối tượng':<24} | {'Di chuyển':<12} | {'Đến':<6} | {'TW [E, L]':<14} | {'Chờ':<5} | {'Rời':<6} | {'Tải trọng':<10} | {'Doanh thu':<10}")
        report_lines.append("-" * 115)

        time_now = 0
        load = 0
        curr = depot
        route_cost = 0
        route_rev = 0
        pass_count = set()
        parc_count = set()

        # Dòng xuất phát từ Depot
        report_lines.append(f"{'Start':<6} | {depot:<6} | {'Xuất phát từ Depot':<24} | {'-':<12} | {0:<6} | {'[0, INF]':<14} | {0:<5} | {0:<6} | {0:<10} | {'-':<10}")

        for seq, v in enumerate(rt, 1):
            node_type, r = vertex_to_req[v]
            data = reqs[r]
            travel_t = t_mat[curr][v]
            travel_c = c_mat[curr][v]
            route_cost += travel_c

            arr_time = time_now + travel_t

            if node_type == 'P':
                tw_str = f"[{data['E']}, {data['L']}]"
                wait_time = max(0, data['E'] - arr_time)
                start_service = max(arr_time, data['E'])
                dep_time = start_service + data['S']

                if data['type'] == 1:
                    action_str = f"Đón Khách #{r}"
                    pass_count.add(r)
                else:
                    action_str = f"Nhận Hàng #{r}"
                    load += data['W']
                    parc_count.add(r)
                rev_str = "-"
            else: # 'D'
                if data['type'] == 1:
                    tw_str = "[Khách - Không Ld]"
                    wait_time = 0
                    start_service = arr_time
                    dep_time = start_service + data['S']
                    action_str = f"Trả Khách #{r}"
                else:
                    tw_str = f"[{data['Ed']}, {data['Ld']}]"
                    wait_time = max(0, data['Ed'] - arr_time)
                    start_service = max(arr_time, data['Ed'])
                    dep_time = start_service + data['S']
                    load -= data['W']
                    action_str = f"Giao Hàng #{r}"

                route_rev += data['Rev']
                rev_str = f"+{data['Rev']}"

            load_str = f"{load}/{cap} kg"
            travel_str = f"+{travel_t}p ({travel_c}đ)"

            report_lines.append(f"{seq:<6} | {v:<6} | {action_str:<24} | {travel_str:<12} | {arr_time:<6} | {tw_str:<14} | {wait_time:<5} | {dep_time:<6} | {load_str:<10} | {rev_str:<10}")

            time_now = dep_time
            curr = v

        net_profit = route_rev - route_cost
        total_fleet_cost += route_cost
        total_fleet_rev += route_rev
        total_fleet_pass += len(pass_count)
        total_fleet_parc += len(parc_count)

        report_lines.append("-" * 115)
        report_lines.append(f"   📊 TỔNG KẾT XE #{k}: Phục vụ {len(pass_count)} khách + {len(parc_count)} hàng | Giờ kết thúc: {time_now} phút | Chi phí xăng: {route_cost}đ | Doanh thu: {route_rev}đ | Lợi nhuận ròng: {net_profit}đ\n")

    # MỤC 3: TỔNG KẾT TOÀN ĐOÀN XE
    report_lines.append("=" * 115)
    report_lines.append("                                    TỔNG KẾT HIỆU NĂNG TOÀN ĐỘI XE")
    report_lines.append("=" * 115)
    report_lines.append(f"  * Số xe hoạt động:                 {active_vehicles} / {K} xe")
    report_lines.append(f"  * Tổng số hành khách phục vụ:       {total_fleet_pass} / {N} khách")
    report_lines.append(f"  * Tổng số kiện hàng phục vụ:        {total_fleet_parc} / {M} kiện")
    report_lines.append(f"  * Tổng doanh thu thu về (Revenue):  {total_fleet_rev:,} đ")
    report_lines.append(f"  * Tổng chi phí di chuyển (Cost):    {total_fleet_cost:,} đ")
    report_lines.append(f"  * LỢI NHUẬN RÒNG (OBJECTIVE):       {total_fleet_rev - total_fleet_cost:,} đ")
    report_lines.append("=" * 115)

    with open(report_file, 'w', encoding='utf-8') as f:
        f.write("\n".join(report_lines))

    print(f"[+] Da tao ban bao cao chi tiet tung node tai: {report_file}")

# ==============================================================================
# HÀM HỖ TRỢ ĐỊNH VỊ THƯ MỤC VÀ TESTCASE
# ==============================================================================
def find_testcase_file(folder_name):
    """
    Tự động tìm file đề bài trong thư mục ./testcases tương ứng với tên testcase ở đầu thư mục output.
    """
    tc_dir = "testcases"
    if not os.path.exists(tc_dir):
        tc_dir = os.path.join(".", "testcases")

    if os.path.isdir(tc_dir):
        # Sắp xếp theo độ dài stem giảm dần (ví dụ 'test_106' trước 'test_10') để tránh khớp nhầm tiền tố
        tc_files = sorted(
            [f for f in os.listdir(tc_dir) if f.endswith(".txt")],
            key=lambda f: len(os.path.splitext(f)[0]),
            reverse=True
        )
        for f in tc_files:
            stem = os.path.splitext(f)[0]
            # Khớp tên testcase ở đầu tên thư mục (ví dụ 'test_10_tabu_...' khớp 'test_10')
            if folder_name.startswith(stem + "_") or folder_name.startswith(stem):
                return os.path.join(tc_dir, f)

    return None


def extract_timestamp_key(dir_path):
    """
    Trích xuất timestamp từ tên thư mục (định dạng YYYY-MM-DD-HH-MM-SS).
    Nếu không tìm thấy regex thì fallback về mtime của thư mục.
    """
    dirname = os.path.basename(dir_path)
    m = re.search(r"(\d{4}-\d{2}-\d{2}-\d{2}-\d{2}-\d{2})", dirname)
    if m:
        return m.group(1)
    return str(os.path.getmtime(dir_path))


def resolve_output_dir(arg_input=None):
    """
    Xác định thư mục output mục tiêu:
    - Nếu arg_input là thư mục tồn tại: dùng trực tiếp.
    - Nếu arg_input là tên thư mục bên trong output/: dùng output/arg_input.
    - Nếu không truyền: MẶC ĐỊNH lấy thư mục có TIMESTAMP MỚI NHẤT trong output/.
    """
    if arg_input:
        if os.path.isdir(arg_input):
            return os.path.abspath(arg_input)
        cand = os.path.join("output", arg_input)
        if os.path.isdir(cand):
            return os.path.abspath(cand)
        if os.path.isfile(arg_input):
            return None
        print(f"[-] Khong tim thay thu muc: {arg_input}")
        return None

    # Mặc định quét thư mục output/ và lấy thư mục có timestamp mới nhất
    if os.path.exists("output"):
        subdirs = [os.path.join("output", d) for d in os.listdir("output") if os.path.isdir(os.path.join("output", d))]
        if subdirs:
            latest_dir = max(subdirs, key=extract_timestamp_key)
            print(f"[i] Mặc định nhận thư mục có timestamp mới nhất: {latest_dir}")
            return os.path.abspath(latest_dir)
        else:
            print("[-] Thư mục output/ hiện đang trống, không tìm thấy lần chạy nào!")
    else:
        print("[-] Chưa tìm thấy thư mục output/!")
            
    return None

# ==============================================================================
# HÀM MAIN THỰC THI TOÀN BỘ
# ==============================================================================
if __name__ == '__main__':
    print("=" * 70)
    print("      BẮT ĐẦU QUY TRÌNH PHÂN TÍCH TABU SEARCH & XUẤT BÁO CÁO")
    print("=" * 70)

    first_arg = sys.argv[1] if len(sys.argv) > 1 else None
    target_dir = resolve_output_dir(first_arg)

    if target_dir:
        folder_base = os.path.basename(target_dir)
        print(f"[*] Thu muc phan tich : {target_dir}")

        # 1. Tìm file log
        log_candidates = [f for f in os.listdir(target_dir) if f.endswith("_log.txt") or f == "log.txt"]
        if not log_candidates:
            print(f"[-] Khong tim thay file _log.txt trong {target_dir}!")
            sys.exit(1)
        log_file = os.path.join(target_dir, log_candidates[0])

        # 2. Tìm file kết quả
        sol_candidates = [f for f in os.listdir(target_dir) if f.endswith("_ket_qua.txt") or f == "ketqua.txt" or f.endswith("_res.txt")]
        if not sol_candidates:
            print(f"[-] Khong tim thay file _ket_qua.txt trong {target_dir}!")
            sys.exit(1)
        sol_file = os.path.join(target_dir, sol_candidates[0])

        # 3. Tìm file đề bài
        if len(sys.argv) > 2 and os.path.isfile(sys.argv[2]):
            inst_file = sys.argv[2]
        else:
            inst_file = find_testcase_file(folder_base)

        if not inst_file or not os.path.isfile(inst_file):
            print(f"[-] Khong tim thay file de bai phu hop trong ./testcases cho thu muc: {folder_base}!")
            sys.exit(1)

        # 4. Định nghĩa các file đầu ra nằm trong chính thư mục đó
        output_img = os.path.join(target_dir, f"{folder_base}_progression.png")
        report_file = os.path.join(target_dir, f"{folder_base}_tour_report.txt")

        print(f"  * File Log          : {log_file}")
        print(f"  * File Ket qua      : {sol_file}")
        print(f"  * File De bai       : {inst_file}")
        print(f"  * Anh bieu do dau ra: {output_img}")
        print(f"  * Bao cao tour dau ra: {report_file}")
        print("-" * 70)

    else:
        # Fallback chế độ file đơn lẻ truyền thống (backward compatibility)
        log_file = sys.argv[1] if len(sys.argv) > 1 else "log_tabu.txt"
        inst_file = sys.argv[2] if len(sys.argv) > 2 else "testcases/test_500.txt"
        sol_file = sys.argv[3] if len(sys.argv) > 3 else "ketqua.txt"
        report_file = sys.argv[4] if len(sys.argv) > 4 else "tour_report.txt"
        output_img = "tabu_progression.png"

    # 1. Phân tích Log và vẽ biểu đồ
    iters, milestones = parse_and_plot_log(log_file, output_img)

    # In nhanh tóm tắt các mốc đột phá lên console
    if milestones:
        print(f"\n[★] PHÁT HIỆN {len(milestones)} MỐC ĐỘT PHÁ GLOBAL BEST:")
        for m in milestones[-8:]:
            ruin_txt = f"(Ruin trước đó tại iter {m['last_ruin']})" if m['last_ruin'] else "(Ban đầu)"
            print(f"  -> Iter {m['iter']:4d}: {m['old_val']:4d} -> {m['new_val']:4d} (+{m['delta']:3d}) | {ruin_txt}")

    # 2. Tạo báo cáo chi tiết đến từng Node
    generate_detailed_tour_report(inst_file, sol_file, milestones, report_file)

    print("\n[✔] HOÀN TẤT TOÀN BỘ QUY TRÌNH!")
