import sys

def check_tour(input_txt, tour_txt):
    print(f"--- INDEPENDENT VALIDATION ---")
    print(f"Input: {input_txt}")
    print(f"Tour: {tour_txt}")
    
    with open(input_txt, 'r', encoding='utf-8') as f:
        lines = [line.strip() for line in f.readlines() if line.strip() and not line.startswith('#')]
    tokens = []
    for l in lines:
        tokens.extend(l.split())
        
    idx = 0
    K = int(tokens[idx]); idx += 1
    N = int(tokens[idx]); idx += 1
    M = int(tokens[idx]); idx += 1
    V = 2*N + 2*M + K
    
    capacities = []
    for _ in range(K):
        o_k = int(tokens[idx]); idx += 1
        q_k = int(tokens[idx]); idx += 1
        capacities.append(q_k)
        
    # parse requests
    passengers = {}
    for i in range(N):
        P = int(tokens[idx]); idx += 1
        D = int(tokens[idx]); idx += 1
        e = int(tokens[idx]); idx += 1
        l = int(tokens[idx]); idx += 1
        dur = int(tokens[idx]); idx += 1
        rev = int(tokens[idx]); idx += 1
        passengers[P] = {
            'type': 'PASSENGER_PICKUP', 'pair': D, 'rev': rev, 'e': e, 'l': l, 'dur': dur, 'w': 0, 'id': i
        }
        passengers[D] = {
            'type': 'PASSENGER_DROPOFF', 'pair': P, 'rev': 0, 'e': 0, 'l': 1000000000, 'dur': dur, 'w': 0, 'id': i
        }
        
    parcels = {}
    for j in range(M):
        P = int(tokens[idx]); idx += 1
        D = int(tokens[idx]); idx += 1
        w = int(tokens[idx]); idx += 1
        ep = int(tokens[idx]); idx += 1
        lp = int(tokens[idx]); idx += 1
        ed = int(tokens[idx]); idx += 1
        ld = int(tokens[idx]); idx += 1
        dur = int(tokens[idx]); idx += 1
        rev = int(tokens[idx]); idx += 1
        parcels[P] = {
            'type': 'PARCEL_PICKUP', 'pair': D, 'rev': rev, 'e': ep, 'l': lp, 'dur': dur, 'w': w, 'id': j
        }
        parcels[D] = {
            'type': 'PARCEL_DROPOFF', 'pair': P, 'rev': 0, 'e': ed, 'l': ld, 'dur': dur, 'w': -w, 'id': j
        }
        
    time_matrix = [[0]*V for _ in range(V)]
    for r in range(V):
        for c in range(V):
            time_matrix[r][c] = int(tokens[idx]); idx += 1
            
    cost_matrix = [[0]*V for _ in range(V)]
    for r in range(V):
        for c in range(V):
            cost_matrix[r][c] = int(tokens[idx]); idx += 1
            
    # parse tour
    with open(tour_txt, 'r', encoding='utf-8') as f:
        tour_lines = [l.strip() for l in f.readlines() if l.strip() and not l.startswith('#')]
        
    reported_benefit = int(tour_lines[0].split()[0])
        
    routes = {}
    for v_id in range(K):
        routes[v_id] = []
        line = tour_lines[v_id + 1]
        parts = line.split()
        m_k = int(parts[0])
        if m_k > 0:
            for v_str in parts[1:]:
                routes[v_id].append(int(v_str))
                
    total_rev = 0
    total_cost = 0
    is_valid = True
    
    served_pass = set()
    served_parcel = set()
    
    for v_id, route in routes.items():
        curr_time = 0
        curr_load = 0
        curr_loc = v_id # depot is 0-indexed v_id, but the array is 0-indexed so v_id is correct
        cap = capacities[v_id]
        
        picked = set()
        
        for step_idx, v_node in enumerate(route):
            # v_node is 1-indexed physical node
            v = v_node
            
            # Check what it is
            req = None
            if v in passengers:
                req = passengers[v]
            elif v in parcels:
                req = parcels[v]
            else:
                print(f"ERROR: Vehicle {v_id} visits unknown node {v}!")
                is_valid = False
                continue
                
            t_type = req['type']
            e, l, dur, w, rev = req['e'], req['l'], req['dur'], req['w'], req['rev']
            req_id = req['id']
            
            if t_type == 'PASSENGER_PICKUP':
                if req_id in served_pass:
                    print(f"ERROR: Passenger {req_id} served twice!")
                    is_valid = False
                served_pass.add(req_id)
                # Ensure the next node is the dropoff
                if step_idx + 1 >= len(route) or route[step_idx+1] != req['pair']:
                    print(f"ERROR: Passenger {req_id} pickup not immediately followed by dropoff!")
                    is_valid = False
            elif t_type == 'PASSENGER_DROPOFF':
                pass # checked by pickup
            elif t_type == 'PARCEL_PICKUP':
                if req_id in served_parcel:
                    print(f"ERROR: Parcel {req_id} served twice!")
                    is_valid = False
                served_parcel.add(req_id)
                picked.add(req_id)
            elif t_type == 'PARCEL_DROPOFF':
                if req_id not in picked:
                    print(f"ERROR: Parcel {req_id} dropped off but not picked up!")
                    is_valid = False
                picked.remove(req_id)
                
            # Travel
            arr = curr_time + time_matrix[curr_loc][v-1]
            c_travel = cost_matrix[curr_loc][v-1]
            
            start = max(arr, e)
            if start > l:
                print(f"ERROR: Time window violation for {t_type} (node {v}) on Veh {v_id}. Start {start} > L {l}")
                is_valid = False
                
            curr_load += w
            if curr_load > cap or curr_load < 0:
                print(f"ERROR: Capacity violation on Veh {v_id} at {t_type}. Load {curr_load}, Cap {cap}")
                is_valid = False
                
            curr_time = start + dur
            curr_loc = v-1
            
            total_rev += rev
            total_cost += c_travel
            
        if len(picked) > 0:
            print(f"ERROR: Vehicle {v_id} finished with parcels still on board: {picked}")
            is_valid = False
            
    actual_ben = total_rev - total_cost
    
    print("\n--- VALIDATION RESULTS ---")
    if is_valid:
        print("[SUCCESS] The tour perfectly respects all physical constraints!")
    else:
        print("[FAILED] The tour has constraint violations!")
        
    print(f"Calculated Revenue: {total_rev}")
    print(f"Calculated Cost:    {total_cost}")
    print(f"Calculated Benefit: {actual_ben}")
    print(f"Reported Benefit:   {reported_benefit}")
    
    if actual_ben == reported_benefit:
        print("[MATCH] Calculated benefit matches the reported benefit.")
    else:
        print("[MISMATCH] Calculated benefit does NOT match!")

if __name__ == '__main__':
    check_tour(sys.argv[1], sys.argv[2])