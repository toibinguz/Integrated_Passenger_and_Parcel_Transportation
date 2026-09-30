import sys
import copy
from ortools.constraint_solver import routing_enums_pb2
from ortools.constraint_solver import pywrapcp
from validator import validate_tour

sys.stdout.reconfigure(encoding='utf-8')

class MacroNode:
    def __init__(self, request_id, type_str, in_node, out_node, revenue, e, l, duration, internal_cost, weight=0):
        self.request_id = request_id  
        self.type = type_str          
        self.in_node = in_node        
        self.out_node = out_node      
        self.revenue = revenue
        self.e = e                    
        self.l = l                    
        self.duration = duration      
        self.internal_cost = internal_cost
        self.weight = weight          

class SoictData:
    def __init__(self):
        self.K = 0; self.N = 0; self.M = 0; self.V = 0
        self.capacities = []
        self.time_matrix = []
        self.cost_matrix = []
        self.requests = [] 
        self.passengers = [] 
        self.parcels = []    

    def load_from_file(self, filename):
        with open(filename, 'r') as f:
            lines = [line.strip() for line in f.readlines() if line.strip()]
        if not lines: return
        tokens = []
        for line in lines: tokens.extend(line.split())
            
        idx = 0
        self.K = int(tokens[idx]); idx += 1
        self.N = int(tokens[idx]); idx += 1
        self.M = int(tokens[idx]); idx += 1
        self.V = 2 * self.N + 2 * self.M + self.K
        
        for _ in range(self.K):
            self.capacities.append(int(tokens[idx]))
            idx += 1
            
        # Thêm K node xuất phát ban đầu (như là các MacroNode rỗng để quản lý index dễ hơn)
        for k in range(self.K):
            node = MacroNode(-1, 'START', k, k, 0, 0, 9999999, 0, 0, 0)
            self.requests.append(node)
            
        for i in range(self.N):
            P_i = self.K + i; D_i = self.K + self.N + i
            T = int(tokens[idx]); idx += 1; E = int(tokens[idx]); idx += 1
            L = int(tokens[idx]); idx += 1; S = int(tokens[idx]); idx += 1
            node = MacroNode(i, 'PASSENGER', P_i, D_i, T, E, L, S + S, 0) 
            self.requests.append(node)
            self.passengers.append(node)
            
        for j in range(self.M):
            P_j = self.K + 2 * self.N + j; D_j = self.K + 2 * self.N + self.M + j
            T = int(tokens[idx]); idx += 1; Ep = int(tokens[idx]); idx += 1
            Lp = int(tokens[idx]); idx += 1; Ed = int(tokens[idx]); idx += 1
            Ld = int(tokens[idx]); idx += 1; S = int(tokens[idx]); idx += 1
            w = int(tokens[idx]); idx += 1
            
            pick_node = MacroNode(j, 'PARCEL_PICKUP', P_j, P_j, T, Ep, Lp, S, 0, w)
            drop_node = MacroNode(j, 'PARCEL_DROPOFF', D_j, D_j, 0, Ed, Ld, S, 0, -w)
            self.requests.extend([pick_node, drop_node])
            self.parcels.append((pick_node, drop_node))
            
        self.time_matrix = [[0] * self.V for _ in range(self.V)]
        for r in range(self.V):
            for c in range(self.V):
                self.time_matrix[r][c] = int(tokens[idx]); idx += 1
                
        self.cost_matrix = [[0] * self.V for _ in range(self.V)]
        for r in range(self.V):
            for c in range(self.V):
                self.cost_matrix[r][c] = int(tokens[idx]); idx += 1
                
        for req in self.passengers:
            req.duration += self.time_matrix[req.in_node][req.out_node]
            req.internal_cost = self.cost_matrix[req.in_node][req.out_node]

def solve_with_ortools(filename):
    print("=== OR-TOOLS CP SOLVER BASELINE ===")
    data = SoictData()
    data.load_from_file(filename)
    
    # OR-Tools cần 1 node DUMMY END
    num_requests = len(data.requests)
    dummy_end = num_requests
    num_nodes = num_requests + 1
    
    starts = list(range(data.K))
    ends = [dummy_end] * data.K
    
    manager = pywrapcp.RoutingIndexManager(num_nodes, data.K, starts, ends)
    routing = pywrapcp.RoutingModel(manager)
    
    def cost_callback(from_index, to_index):
        from_node = manager.IndexToNode(from_index)
        to_node = manager.IndexToNode(to_index)
        if to_node == dummy_end or from_node == dummy_end: return 0
        u = data.requests[from_node].out_node
        v = data.requests[to_node].in_node
        return data.cost_matrix[u][v] + data.requests[to_node].internal_cost

    transit_callback_index = routing.RegisterTransitCallback(cost_callback)
    routing.SetArcCostEvaluatorOfAllVehicles(transit_callback_index)
    
    # --- CALLBACK THỜI GIAN (TIME) ---
    def time_callback(from_index, to_index):
        from_node = manager.IndexToNode(from_index)
        to_node = manager.IndexToNode(to_index)
        if to_node == dummy_end or from_node == dummy_end: return 0
        u = data.requests[from_node].out_node
        v = data.requests[to_node].in_node
        # FIX CỰC KỲ QUAN TRỌNG: Transit(from, to) = Duration(from) + Travel(from, to)
        # Để đảm bảo CumulVar(to) chính xác là THỜI GIAN BẮT ĐẦU PHỤC VỤ (Start Time) tại to.
        return data.requests[from_node].duration + data.time_matrix[u][v]
        
    time_callback_index = routing.RegisterTransitCallback(time_callback)
    
    routing.AddDimension(
        time_callback_index,
        9999999,  
        9999999,  
        False,    
        'Time'
    )
    time_dimension = routing.GetDimensionOrDie('Time')
    
    for i in range(num_requests):
        index = manager.NodeToIndex(i)
        req = data.requests[i]
        # Thời gian đến in_node phải nằm trong [e, l]
        time_dimension.CumulVar(index).SetRange(req.e, req.l)
        
    # --- CALLBACK TẢI TRỌNG (CAPACITY) ---
    def demand_callback(from_index):
        from_node = manager.IndexToNode(from_index)
        if from_node == dummy_end:
            return 0
        return data.requests[from_node].weight
        
    demand_callback_index = routing.RegisterUnaryTransitCallback(demand_callback)
    routing.AddDimensionWithVehicleCapacity(
        demand_callback_index,
        0,  # Không cho phép lố sức chứa
        data.capacities, 
        True,  # Reset về 0 khi bắt đầu
        'Capacity'
    )
    
    # --- THIẾT LẬP RÀNG BUỘC PRIZE COLLECTING & PRECEDENCE ---
    # Tổng doanh thu
    total_revenue_available = 0
    
    # Hành khách: Disjunction (Chọn hoặc bỏ, bỏ bị phạt bằng đúng doanh thu)
    for p in data.passengers:
        p_idx = data.requests.index(p)
        index = manager.NodeToIndex(p_idx)
        routing.AddDisjunction([index], p.revenue)
        total_revenue_available += p.revenue
        
    # Hàng hóa: Pick & Drop
    for pick, drop in data.parcels:
        pick_idx = data.requests.index(pick)
        drop_idx = data.requests.index(drop)
        
        p_index = manager.NodeToIndex(pick_idx)
        d_index = manager.NodeToIndex(drop_idx)
        
        # Thêm ràng buộc cùng xe và thứ tự (Pick trước Drop)
        routing.AddPickupAndDelivery(p_index, d_index)
        routing.solver().Add(
            routing.VehicleVar(p_index) == routing.VehicleVar(d_index)
        )
        routing.solver().Add(
            time_dimension.CumulVar(p_index) <= time_dimension.CumulVar(d_index)
        )
        
        # Disjunction: Drop không phạt, Pick phạt bằng tổng doanh thu
        routing.AddDisjunction([p_index], pick.revenue)
        routing.AddDisjunction([d_index], 0)
        total_revenue_available += pick.revenue

    # --- CHẠY SOLVER ---
    search_parameters = pywrapcp.DefaultRoutingSearchParameters()
    search_parameters.first_solution_strategy = (
        routing_enums_pb2.FirstSolutionStrategy.PARALLEL_CHEAPEST_INSERTION)
    search_parameters.local_search_metaheuristic = (
        routing_enums_pb2.LocalSearchMetaheuristic.GUIDED_LOCAL_SEARCH)
    search_parameters.time_limit.seconds = 10 # Giới hạn 10 giây
    
    solution = routing.SolveWithParameters(search_parameters)
    
    if solution:
        print("TÌM THẤY NGHIỆM!")
        # Minimize Cost = Travel_Cost + Penalty
        # Penalty = Sum_Revenue_Unperformed
        # Total_Benefit = Total_Revenue_Performed - Travel_Cost
        # => Total_Benefit = Total_Revenue_Available - Cost
        total_benefit = total_revenue_available - solution.ObjectiveValue()
        print(f"Tổng Benefit Tối Ưu Tương Đương: {total_benefit}")
        
        # Trích xuất và ghi ra file
        import os
        base_name = os.path.basename(filename)
        out_filename = f"ortools_tour_{base_name}"
        
        with open(out_filename, "w", encoding='utf-8') as out:
            out.write(f"Objective Benefit: {total_benefit}\n")
            
            for vehicle_id in range(data.K):
                index = routing.Start(vehicle_id)
                route_str = f"Xe {vehicle_id} (Capacity {data.capacities[vehicle_id]}):\n  "
                route_nodes = []
                while not routing.IsEnd(index):
                    node_index = manager.IndexToNode(index)
                    if node_index >= data.K: # Bỏ qua điểm xuất phát ảo
                        req = data.requests[node_index]
                        route_nodes.append(f"[{req.type} {req.request_id}]")
                    index = solution.Value(routing.NextVar(index))
                
                route_str += " -> ".join(route_nodes) if route_nodes else "KHÔNG CHỞ"
                print(route_str)
                out.write(route_str + "\n")
        print(f"\nĐã xuất kết quả kiểm chứng ra {out_filename}")
        
        # BẮT BUỘC KIỂM TRA TÍNH HỢP LỆ (INTERNAL VALIDATION)
        print("-" * 30)
        is_valid = validate_tour(filename, out_filename)
        if not is_valid:
            print("\n[FATAL ERROR] OR-Tools đã sinh ra một lộ trình KHÔNG HỢP LỆ!")
            sys.exit(1)
        print("-" * 30)
        
    else:
        print("KHÔNG TÌM THẤY NGHIỆM.")

if __name__ == '__main__':
    filename = sys.argv[1] if len(sys.argv) > 1 else 'test_1.txt'
    solve_with_ortools(filename)
