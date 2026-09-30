import sys
import time
import copy
from models import Data, Route
from inter_route_milp import optimize_2_routes_milp
from intra_milp import optimize_route_milp

def load_tour(data, tour_filename):
    routes = [Route(i, data.capacities[i], data.depots[i]) for i in range(data.K)]
    
    with open(tour_filename, 'r', encoding='utf-8') as f:
        lines = f.readlines()
        
    current_vehicle = -1
    for line in lines:
        line = line.strip()
        if line.startswith("Xe "):
            # Vd: Xe 0 (Capacity 59):
            parts = line.split()
            current_vehicle = int(parts[1])
        elif line.startswith("[") and current_vehicle != -1:
            # Parse nodes: [PARCEL_PICKUP 169] -> [PARCEL_PICKUP 39]
            # Tách bằng " -> "
            tokens = line.split(" -> ")
            for token in tokens:
                token = token.strip("[]")
                type_str, req_id_str = token.split()
                req_id = int(req_id_str)
                
                # Tìm node tương ứng trong data.nodes
                matched_node = None
                for n in data.nodes:
                    if n.type == type_str and n.request_id == req_id:
                        matched_node = n
                        break
                
                if matched_node:
                    # Chèn vào trước điểm END_DEPOT
                    routes[current_vehicle].nodes.insert(-1, matched_node)
                    
    # Update states cho toàn bộ xe sau khi parse
    for r in routes:
        r.update_states(data)
        
    return routes

def write_tour(routes, filename):
    total_benefit = sum(r.total_benefit for r in routes)
    with open(filename, 'w', encoding='utf-8') as f:
        f.write(f"Total Benefit: {total_benefit}\n")
        for i, r in enumerate(routes):
            f.write(f"Xe {i} (Capacity {r.capacity}):\n")
            if len(r.nodes) <= 2:
                f.write("  KHONG CHAY\n")
            else:
                path_str = " -> ".join([f"[{'PASSENGER' if n.type == 'PASSENGER' else n.type} {n.request_id}]" for n in r.nodes[1:-1]])
                f.write(f"  {path_str}\n")
    print(f"--- Đã xuất file tour mới: {filename} (Benefit: {total_benefit}) ---")

def exhaustive_sweep(data, routes, time_limit_ms=30000):
    print(f"\n[BẮT ĐẦU CHUỐT NGHIỆM HẬU KỲ] Time Limit/Pair: {time_limit_ms/1000}s")
    
    current_benefit = sum(r.total_benefit for r in routes)
    print(f"Benefit khởi điểm: {current_benefit}")
    print("--- Trạng thái các tuyến xe ban đầu ---")
    for r in routes:
        if len(r.nodes) > 2:
            max_load = max(r.current_load) if r.current_load else 0
            print(f"  Xe {r.vehicle_id:02d} | Nodes: {len(r.nodes):02d} | Benefit: {r.total_benefit} | Max Load: {max_load}/{r.capacity}")
    print("---------------------------------------")
    
    improved = True
    sweep_count = 1
    
    # Cache lưu cặp phiên bản xe đã quét mà không mang lại kết quả
    evaluated_pairs = set()
    
    while improved:
        improved = False
        print(f"\n--- Bắt đầu Vòng quét toàn hạt lần thứ {sweep_count} ---")
        
        for i in range(len(routes)):
            for j in range(i + 1, len(routes)):
                rA, rB = routes[i], routes[j]
                
                if len(rA.nodes) <= 2 and len(rB.nodes) <= 2:
                    continue # Cả 2 xe trống
                    
                # Chỉ chạy nếu tổng số node của 2 xe >= 5 (tức là có ít nhất 1 node khách)
                if len(rA.nodes) + len(rB.nodes) <= 5:
                    continue
                    
                # Kiểm tra Cache
                pair_key = tuple(sorted((rA.version, rB.version)))
                if pair_key in evaluated_pairs:
                    continue
                
                # Gọi MILP vắt kiệt
                print(f"  [Đang chạy] Xe {i:02d} (Nodes: {len(rA.nodes):02d}, Ben: {rA.total_benefit}) vs Xe {j:02d} (Nodes: {len(rB.nodes):02d}, Ben: {rB.total_benefit}) | Ver: {rA.version}, {rB.version}", flush=True)
                
                # Nhớ lưu lại old_ben TRƯỚC KHI gọi hàm vì hàm này update in-place rA, rB
                old_ben = rA.total_benefit + rB.total_benefit
                
                success, rA_new, rB_new = optimize_2_routes_milp(
                    rA, rB, [], data, time_limit_ms=time_limit_ms
                )
                
                # Ghi nhận cặp này đã được đánh giá xong (dù thành công hay thất bại)
                evaluated_pairs.add(pair_key)
                
                if success:
                    new_ben = rA_new.total_benefit + rB_new.total_benefit
                    # Hàm optimize_2_routes_milp ĐÃ kiểm tra new_ben > old_ben và update in-place
                    print(f"  [MILP SWEEP] Vắt kiệt thành công giữa Xe {i} và Xe {j}! | Benefit: {old_ben} -> {new_ben}")
                    # Gán lại cho chắc dù nó in-place
                    routes[i] = rA_new
                    routes[j] = rB_new
                    
                    total_ben = sum(r.total_benefit for r in routes)
                    print(f"  [+] Global Benefit tăng lên: {total_ben}")
                    
                    # Backup ngay lập tức
                    tour_filename = f"sweep_tour_{total_ben}.txt"
                    write_tour(routes, tour_filename)
                    
                    # Validate độc lập 100%
                    from validator import validate_tour
                    is_valid = validate_tour(data.filename, tour_filename)
                    if not is_valid:
                        print("[NGHIÊM TRỌNG] Tour mới bị sai logic ràng buộc! Sẽ dừng chương trình.")
                        sys.exit(1)
                        
                    improved = True
                    break # Reset vòng lặp for
            if improved:
                break
                
        sweep_count += 1
        
    print(f"\n[HOÀN TẤT] Quá trình vắt kiệt đã dừng lại ở Benefit: {sum(r.total_benefit for r in routes)}")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Sử dụng: python sweep_post_opt.py <data_file> <tour_file>")
        sys.exit(1)
        
    data_file = sys.argv[1]
    tour_file = sys.argv[2]
    
    data = Data(data_file)
    routes = load_tour(data, tour_file)
    
    # 30 giây cho mỗi cặp. Có thể đổi qua tham số dòng lệnh nếu thích.
    exhaustive_sweep(data, routes, time_limit_ms=30000)
