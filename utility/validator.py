import sys
import io

# Đảm bảo in được emoji trên Windows
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

def read_file_tokens(filepath):
    """Đọc file và hỗ trợ cả UTF-16 (PowerShell) lẫn UTF-8 (Chuẩn)"""
    content = ""
    try:
        # PowerShell mặc định xuất file text bằng UTF-16 (chứa byte 0xFF ở đầu)
        with open(filepath, 'r', encoding='utf-16') as f:
            content = f.read()
    except:
        # Nếu không phải UTF-16 thì dùng UTF-8
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
            
    tokens = []
    for line in content.splitlines():
        if '#' in line:
            line = line[:line.index('#')]
        tokens.extend(line.split())
    return tokens

def validate(instance_file, solution_file):
    print(f"🔍 Đang kiểm tra file nghiệm: {solution_file} dựa trên đề bài: {instance_file}\n")
    
    # 1. Đọc dữ liệu đầu vào
    try:
        tokens = read_file_tokens(instance_file)
    except Exception as e:
        print(f"❌ Lỗi đọc file đề bài: {e}")
        return

    if not tokens:
        print("❌ File đề bài trống!")
        return

    idx = 0
    K = int(tokens[idx]); N = int(tokens[idx+1]); M = int(tokens[idx+2]); idx += 3
    V = 2*N + 2*M + K
    
    O = [0] * (K + 1)
    Q = [0] * (K + 1)
    for k in range(1, K + 1):
        O[k] = int(tokens[idx])
        Q[k] = int(tokens[idx+1])
        idx += 2
        
    reqs = {}
    vertex_to_req = {}
    for i in range(1, N + 1):
        P = int(tokens[idx]); D = int(tokens[idx+1]); E = int(tokens[idx+2])
        L = int(tokens[idx+3]); S = int(tokens[idx+4]); Rev = int(tokens[idx+5])
        idx += 6
        reqs[i] = {'type': 1, 'P': P, 'D': D, 'W': 0, 'E': E, 'L': L, 'Ed': E, 'Ld': L, 'S': S, 'Rev': Rev}
        vertex_to_req[P] = ('P', i)
        vertex_to_req[D] = ('D', i)
        
    for j in range(1, M + 1):
        r = N + j
        P = int(tokens[idx]); D = int(tokens[idx+1]); W = int(tokens[idx+2])
        E = int(tokens[idx+3]); L = int(tokens[idx+4]); Ed = int(tokens[idx+5])
        Ld = int(tokens[idx+6]); S = int(tokens[idx+7]); Rev = int(tokens[idx+8])
        idx += 9
        reqs[r] = {'type': 2, 'P': P, 'D': D, 'W': W, 'E': E, 'L': L, 'Ed': Ed, 'Ld': Ld, 'S': S, 'Rev': Rev}
        vertex_to_req[P] = ('P', r)
        vertex_to_req[D] = ('D', r)
        
    t_mat = [[0]*(V+1) for _ in range(V+1)]
    for i in range(1, V+1):
        for j in range(1, V+1):
            t_mat[i][j] = int(tokens[idx]); idx += 1
            
    c_mat = [[0]*(V+1) for _ in range(V+1)]
    for i in range(1, V+1):
        for j in range(1, V+1):
            c_mat[i][j] = int(tokens[idx]); idx += 1

    # 2. Đọc kết quả Solution
    try:
        sol_tokens = read_file_tokens(solution_file)
    except Exception as e:
        print(f"❌ Lỗi đọc file nghiệm: {e}")
        return

    if not sol_tokens:
        print("❌ File nghiệm trống!")
        return

    declared_obj = int(sol_tokens[0])
    s_idx = 1
    
    routes = []
    for k in range(1, K + 1):
        length = int(sol_tokens[s_idx]); s_idx += 1
        route = []
        for _ in range(length):
            route.append(int(sol_tokens[s_idx])); s_idx += 1
        routes.append(route)
        
    # 3. Validation Logic
    total_benefit = 0
    global_served = set()
    
    for k in range(1, K + 1):
        rt = routes[k-1]
        if not rt: continue
        
        time_now = 0
        load = 0
        curr = O[k]
        picked = set()
        dropped = set()
        
        for i, v in enumerate(rt):
            if v not in vertex_to_req:
                print(f"❌ [LỖI] Xe {k}: Node {v} không thuộc bất kỳ request nào!")
                return
                
            node_type, r = vertex_to_req[v]
            data = reqs[r]
            
            # Tính di chuyển
            time_now += t_mat[curr][v]
            total_benefit -= c_mat[curr][v]
            
            if node_type == 'P':
                # Chờ tới giờ mở cửa E
                time_now = max(time_now, data['E'])
                if time_now > data['L']:
                    print(f"❌ [LỖI Time-Window] Xe {k}: Đón Req {r} lúc {time_now} trễ hơn giờ đóng cửa L = {data['L']}")
                    return
                if r in picked:
                    print(f"❌ [LỖI Logic] Xe {k}: Pick-up Req {r} nhiều lần!")
                    return
                picked.add(r)
                
                if r in global_served:
                    print(f"❌ [LỖI Trùng lặp] Request {r} được phục vụ bởi nhiều xe khác nhau!")
                    return
                global_served.add(r)
                
                # Check hành khách (Phải đi thẳng)
                if data['type'] == 1:
                    if i + 1 == len(rt) or rt[i+1] != data['D']:
                        print(f"❌ [LỖI Hành Khách] Xe {k}: Đón khách {r} nhưng không Drop-off ngay lập tức!")
                        return
                else: # Hàng hóa
                    load += data['W']
                    if load > Q[k]:
                        print(f"❌ [LỖI Tải Trọng] Xe {k}: Quá tải tại node {v} (Hiện tại: {load} > Sức chứa: {Q[k]})")
                        return
            else: # node_type == 'D'
                if r not in picked:
                    print(f"❌ [LỖI Logic] Xe {k}: Drop-off Req {r} khi chưa Pick-up!")
                    return
                if r in dropped:
                    print(f"❌ [LỖI Logic] Xe {k}: Drop-off Req {r} nhiều lần!")
                    return
                
                if data['type'] == 2: # CHỈ CHECK TIME WINDOW TRẢ HÀNG CHO PARCEL
                    time_now = max(time_now, data['Ed'])
                    if time_now > data['Ld']:
                        print(f"❌ [LỖI Time-Window] Xe {k}: Trả Req {r} lúc {time_now} trễ hơn giờ đóng cửa Ld = {data['Ld']}")
                        return
                    load -= data['W']
                
                dropped.add(r)
                
                # Trả thành công -> cộng tiền
                total_benefit += data['Rev']
                
            # Service Time
            time_now += data['S']
            curr = v
            
        if len(picked) != len(dropped):
            print(f"❌ [LỖI Trọn vẹn] Xe {k}: Pick-up {len(picked)} đơn nhưng chỉ Drop-off {len(dropped)} đơn!")
            return
            
    print("-" * 50)
    print(f"✅ BẢN BÁO CÁO HỢP LỆ")
    print(f"📊 Tổng Objective theo thuật toán Validator: {total_benefit}")
    print(f"📌 Objective do file C++ tự nhận:           {declared_obj}")
    
    if total_benefit == declared_obj:
        print("🎉 KẾT LUẬN: ĐIỂM SỐ KHỚP HOÀN TOÀN! CHUẨN XÁC!")
    else:
        print("⚠️ KẾT LUẬN: LỆCH ĐIỂM (C++ có thể đã tính sai cost/revenue hoặc validator đọc nhầm định dạng).")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("📌 Hướng dẫn sử dụng:")
        print("python validator.py <đường_dẫn_file_đề_bài> <đường_dẫn_file_kết_quả>")
        print("Ví dụ: python validator.py input.txt ketqua.txt")
    else:
        validate(sys.argv[1], sys.argv[2])
