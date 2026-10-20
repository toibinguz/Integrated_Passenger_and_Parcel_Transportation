#include <bits/stdc++.h>
#include <chrono>
using namespace std;

// ============================================================================
// HYPERPARAMETERS & ALGORITHMIC CONTROL KNOBS
// ============================================================================
// 1. Search Iterations & Time Budget
int MAX_ITER = 30000;                     // Total maximum iterations for Tabu Search
double TIME_LIMIT_SEC = 250.0;              // Time limit guard for Online Judge (seconds)

// 2. Tabu Tenure Configuration
int TABU_TENURE_BASE = 8;                 // Base Tabu tenure for (vehicle, request)
int TABU_TENURE_RAND = 8;                 // Random tenure amplitude [0, TABU_TENURE_RAND)

// 3. Epoch & Stagnation Parameters
int STAGNANT_LIMIT = 50;                  // Iterations without improvement in current epoch to trigger Ruin

// 4. Ruin Operators Configuration
int RUIN_SIZE = 6;                        // Number of requests ejected per Ruin step
int RUIN_MODES_COUNT = 4;                 // Number of Ruin strategies (0: Spatial, 1: Time, 2: Window, 3: Dismantle)
int RUIN_TOP_CANDIDATE_POOL = 4;          // Window size for stochastic selection among sorted correlated requests

// 5. Elite Pool Configuration (Quality & Diversity Management)
const int ELITE_POOL_SIZE = 4;            // Maximum capacity of the Elite Pool
double ELITE_QUALIFICATION_RATIO = 0.80;  // Minimum objective ratio vs Global Best to enter pool (80%)
int INTENSIFICATION_PROB = 60;            // Probability (%) to seed Ruin from Best Elite (vs Diverse Elite)

// ============================================================================
// DATA STRUCTURES & PROBLEM CONSTANTS
// ============================================================================
const long long INF = 1e18;
const int MAXV = 1050;
const int MAX_REQ = 1050;

int K, N, M, V;
int O[MAXV], Q[MAXV];
int req_type[MAX_REQ], P[MAX_REQ], D[MAX_REQ], W[MAX_REQ];
int E[MAX_REQ], L_time[MAX_REQ], Ed[MAX_REQ], Ld[MAX_REQ];
int S[MAX_REQ], Rev[MAX_REQ];
int t_mat[MAXV][MAXV], c_mat[MAXV][MAXV];

// Edge Feasibility Lookup
bool can_edge[MAXV][MAXV];

// Solution State
vector<int> routes[MAXV];           // Task list: r (Passenger/Pickup), -r (Dropoff)
int veh_of[MAX_REQ];                // Vehicle serving req r (0 if unserved)
long long route_obj[MAXV];          // Current profit per vehicle
long long cur_total_obj = 0;

vector<int> best_routes[MAXV];
long long global_best_obj = -INF;

bool profitable[MAX_REQ];           // Filter negative-profit requests
int tabu_veh_req[MAXV][MAX_REQ];    // Tabu expiration matrix for (vehicle k, request r)

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================
inline int get_v_in(int task) { return (task > 0) ? P[abs(task)] : D[abs(task)]; }
inline int get_v_out(int task) { return (task > 0 && abs(task) <= N) ? D[abs(task)] : get_v_in(task); }

vector<int> get_physical_route(const vector<int>& rt) {
    vector<int> phys;
    for (int t : rt) {
        if (t > 0 && t <= N) { phys.push_back(P[t]); phys.push_back(D[t]); }
        else if (t > 0) phys.push_back(P[t]);
        else phys.push_back(D[-t]);
    }
    return phys;
}

vector<int> remove_req(const vector<int>& rt, int r) {
    vector<int> res;
    for (int t : rt) if (abs(t) != r) res.push_back(t);
    return res;
}

// ============================================================================
// INITIALIZATION & INPUT
// ============================================================================
void init_edge_feasibility() {
    vector<long long> earliest_dep(V + 1, 0);
    vector<long long> latest_arr(V + 1, INF);
    for (int k = 1; k <= K; ++k) earliest_dep[O[k]] = 0;

    for (int r = 1; r <= N; ++r) {
        latest_arr[P[r]] = L_time[r];
        earliest_dep[P[r]] = E[r] + S[r];
        earliest_dep[D[r]] = E[r] + 2LL * S[r] + t_mat[P[r]][D[r]];
    }
    for (int j = 1; j <= M; ++j) {
        int r = N + j;
        latest_arr[P[r]] = L_time[r];
        earliest_dep[P[r]] = E[r] + S[r];
        latest_arr[D[r]] = Ld[r];
        long long earliest_arr_d = max((long long)Ed[r], (long long)E[r] + S[r] + t_mat[P[r]][D[r]]);
        earliest_dep[D[r]] = earliest_arr_d + S[r];
    }
    for (int u = 1; u <= V; ++u) {
        for (int v = 1; v <= V; ++v) {
            can_edge[u][v] = (earliest_dep[u] + t_mat[u][v] <= latest_arr[v]);
        }
    }
    for (int u = 1; u <= V; ++u) {
        for (int k = 1; k <= K; ++k) can_edge[u][O[k]] = false;
    }
    for (int r = 1; r <= N; ++r) {
        for (int v = 1; v <= V; ++v) if (v != D[r]) can_edge[P[r]][v] = false;
        for (int u = 1; u <= V; ++u) if (u != P[r]) can_edge[u][D[r]] = false;
    }
}

void read_input() {
    if (!(cin >> K >> N >> M)) return;
    V = 2 * N + 2 * M + K;
    for (int k = 1; k <= K; ++k) cin >> O[k] >> Q[k];
    
    for (int i = 1; i <= N; ++i) {
        int r = i; req_type[r] = 1;
        cin >> P[r] >> D[r] >> E[r] >> L_time[r] >> S[r] >> Rev[r];
    }
    for (int j = 1; j <= M; ++j) {
        int r = N + j; req_type[r] = 2;
        cin >> P[r] >> D[r] >> W[r] >> E[r] >> L_time[r] >> Ed[r] >> Ld[r] >> S[r] >> Rev[r];
    }
    for (int i = 1; i <= V; ++i)
        for (int j = 1; j <= V; ++j) cin >> t_mat[i][j];
    for (int i = 1; i <= V; ++i)
        for (int j = 1; j <= V; ++j) cin >> c_mat[i][j];

    for (int i = 1; i <= N + M; ++i) profitable[i] = true;
    for (int i = 1; i <= N; ++i) {
        if (Rev[i] - c_mat[P[i]][D[i]] < 0) profitable[i] = false;
    }

    init_edge_feasibility();
}

// ============================================================================
// ROUTE EVALUATION & LOCAL SEARCH
// ============================================================================
long long eval_single_route(int k, const vector<int>& rt) {
    if (rt.empty()) return 0;
    long long time_now = 0; int load = 0; int curr = O[k];
    vector<bool> picked(N + M + 1, false), dropped(N + M + 1, false);
    long long benefit = 0;

    for (int task : rt) {
        int r = abs(task);
        int v_in = get_v_in(task);
        int v_out = get_v_out(task);

        time_now += t_mat[curr][v_in]; benefit -= c_mat[curr][v_in];
        
        if (task > 0) { // Passenger or Pickup
            if (picked[r]) return -INF;
            time_now = max(time_now, (long long)E[r]);
            if (time_now > L_time[r]) return -INF;
            picked[r] = true;
            
            if (r <= N) { // Passenger macro-node
                time_now += S[r] + t_mat[P[r]][D[r]]; benefit -= c_mat[P[r]][D[r]];
                time_now += S[r]; dropped[r] = true; benefit += Rev[r];
            } else { // Parcel pickup
                load += W[r];
                if (load > Q[k]) return -INF;
                time_now += S[r];
            }
        } else { // Parcel dropoff (-r)
            if (!picked[r] || dropped[r]) return -INF;
            time_now = max(time_now, (long long)Ed[r]);
            if (time_now > Ld[r]) return -INF;
            load -= W[r];
            dropped[r] = true; benefit += Rev[r];
            time_now += S[r];
        }
        curr = v_out;
    }
    return benefit;
}

void optimize_route(int k, vector<int>& rt) {
    if (rt.empty()) return;
    bool improved = true;
    long long best_b = eval_single_route(k, rt);

    while (improved) {
        improved = false;
        vector<int> best_rt = rt;
        vector<int> reqs;
        for (int t : rt) {
            int r = abs(t);
            if (find(reqs.begin(), reqs.end(), r) == reqs.end()) reqs.push_back(r);
        }
        
        for (int r : reqs) {
            vector<int> temp;
            for (int t : rt) if (abs(t) != r) temp.push_back(t);
            int m = temp.size();
            
            if (r <= N) { // Passenger
                for (int pos = 0; pos <= m; ++pos) {
                    int u = (pos == 0) ? O[k] : get_v_out(temp[pos - 1]);
                    if (!can_edge[u][P[r]]) continue;
                    if (pos < m && !can_edge[D[r]][get_v_in(temp[pos])]) continue;

                    vector<int> cand = temp; cand.insert(cand.begin() + pos, r);
                    long long b = eval_single_route(k, cand);
                    if (b > best_b) { best_b = b; best_rt = cand; improved = true; }
                }
            } else { // Parcel
                for (int p1 = 0; p1 <= m; ++p1) {
                    int u1 = (p1 == 0) ? O[k] : get_v_out(temp[p1 - 1]);
                    if (!can_edge[u1][P[r]]) continue;

                    for (int p2 = p1 + 1; p2 <= m + 1; ++p2) {
                        vector<int> cand = temp;
                        cand.insert(cand.begin() + p1, r); cand.insert(cand.begin() + p2, -r);
                        long long b = eval_single_route(k, cand);
                        if (b > best_b) { best_b = b; best_rt = cand; improved = true; }
                    }
                }
            }
        }
        rt = best_rt;
    }
}

pair<long long, vector<int>> best_insert(int k, const vector<int>& rt, int r) {
    long long best_b = -INF;
    vector<int> best_rt;
    int n = rt.size();

    if (r <= N) { // Passenger
        for (int pos = 0; pos <= n; ++pos) {
            int u = (pos == 0) ? O[k] : get_v_out(rt[pos - 1]);
            if (!can_edge[u][P[r]]) continue;
            if (pos < n && !can_edge[D[r]][get_v_in(rt[pos])]) continue;

            vector<int> cand = rt; cand.insert(cand.begin() + pos, r);
            long long b = eval_single_route(k, cand);
            if (b > best_b) { best_b = b; best_rt = cand; }
        }
    } else { // Parcel
        for (int p1 = 0; p1 <= n; ++p1) {
            int u1 = (p1 == 0) ? O[k] : get_v_out(rt[p1 - 1]);
            if (!can_edge[u1][P[r]]) continue;

            for (int p2 = p1 + 1; p2 <= n + 1; ++p2) {
                if (p1 < n && !can_edge[P[r]][get_v_in(rt[p1])]) continue;
                int u2 = (p2 == p1 + 1) ? P[r] : get_v_out(rt[p2 - 2]);
                if (!can_edge[u2][D[r]]) continue;
                if (p2 <= n && !can_edge[D[r]][get_v_in(rt[p2 - 1])]) continue;

                vector<int> cand = rt;
                cand.insert(cand.begin() + p1, r); cand.insert(cand.begin() + p2, -r);
                long long b = eval_single_route(k, cand);
                if (b > best_b) { best_b = b; best_rt = cand; }
            }
        }
    }
    if (best_b != -INF) {
        optimize_route(k, best_rt);
        best_b = eval_single_route(k, best_rt);
    }
    return {best_b, best_rt};
}

// ============================================================================
// ELITE POOL (BIASED FITNESS: QUALITY & DIVERSITY MANAGEMENT)
// ============================================================================
struct SolutionState {
    long long obj = 0;
    vector<int> veh_assignment; // size N + M + 1
    vector<vector<int>> routes;  // size K + 1
};

vector<SolutionState> elite_pool;

int calc_assignment_dist(const vector<int>& a, const vector<int>& b) {
    int d = 0;
    for (int r = 1; r <= N + M; ++r) {
        if (a[r] != b[r]) d++;
    }
    return d;
}

bool update_elite_pool(const SolutionState& cand) {
    if (cand.obj <= 0) return false;
    if (global_best_obj > 0 && cand.obj < global_best_obj * ELITE_QUALIFICATION_RATIO) return false;

    if (elite_pool.empty()) {
        elite_pool.push_back(cand);
        return true;
    }

    // Check if candidate is an identical clone (dist == 0)
    for (int i = 0; i < (int)elite_pool.size(); ++i) {
        int d = calc_assignment_dist(cand.veh_assignment, elite_pool[i].veh_assignment);
        if (d == 0) {
            if (cand.obj > elite_pool[i].obj) {
                elite_pool[i] = cand;
                return true;
            }
            return false;
        }
    }

    if ((int)elite_pool.size() < ELITE_POOL_SIZE) {
        elite_pool.push_back(cand);
        return true;
    }

    // Pool is FULL: Apply Bi-criterion Biased Fitness Ranking
    vector<SolutionState> all_sols = elite_pool;
    all_sols.push_back(cand);
    int T = all_sols.size();

    // 1. Fitness Rank R_fit
    vector<int> fit_order(T);
    iota(fit_order.begin(), fit_order.end(), 0);
    sort(fit_order.begin(), fit_order.end(), [&](int a, int b) {
        return all_sols[a].obj > all_sols[b].obj;
    });
    vector<int> r_fit(T);
    for (int rank = 0; rank < T; ++rank) r_fit[fit_order[rank]] = rank + 1;

    // 2. Diversity Rank R_div
    vector<int> min_d(T, 1e9);
    for (int i = 0; i < T; ++i) {
        for (int j = 0; j < T; ++j) {
            if (i == j) continue;
            int d = calc_assignment_dist(all_sols[i].veh_assignment, all_sols[j].veh_assignment);
            min_d[i] = min(min_d[i], d);
        }
    }
    vector<int> div_order(T);
    iota(div_order.begin(), div_order.end(), 0);
    sort(div_order.begin(), div_order.end(), [&](int a, int b) {
        return min_d[a] > min_d[b];
    });
    vector<int> r_div(T);
    for (int rank = 0; rank < T; ++rank) r_div[div_order[rank]] = rank + 1;

    // 3. Biased Rank Eviction: Score = R_fit + R_div
    int evict_idx = -1;
    int worst_score = -1;
    int worst_r_fit = -1;
    for (int i = 0; i < T; ++i) {
        int score = r_fit[i] + r_div[i];
        if (score > worst_score || (score == worst_score && r_fit[i] > worst_r_fit)) {
            worst_score = score;
            worst_r_fit = r_fit[i];
            evict_idx = i;
        }
    }

    if (evict_idx == T - 1) return false;

    elite_pool[evict_idx] = cand;
    return true;
}

// ============================================================================
// TABU SEARCH ENGINE
// ============================================================================
struct Move {
    long long delta = -INF;
    int k1 = -1, k2 = -1;
    vector<int> rt1, rt2;
    int r_in = 0, r_out = 0;
    int type = 0;
};

void run_tabu_search() {
    memset(tabu_veh_req, 0, sizeof(tabu_veh_req));
    memset(veh_of, 0, sizeof(veh_of));
    for (int k = 1; k <= K; ++k) route_obj[k] = 0;
    cur_total_obj = 0; global_best_obj = 0;
    
    elite_pool.clear();
    int epoch_idx = 1;
    int epoch_iter = 0;
    long long epoch_best_obj = 0;
    int epoch_stagnant_iters = 0;
    SolutionState epoch_best_state;

    auto start_time = chrono::steady_clock::now();

    for (int iter = 1; iter <= MAX_ITER; ++iter) {
        // Online Judge Time Guard (checked every 32 iterations)
        if ((iter & 31) == 0) {
            auto now = chrono::steady_clock::now();
            if (chrono::duration<double>(now - start_time).count() >= TIME_LIMIT_SEC) {
                break;
            }
        }

        epoch_iter++;
        Move best_move;

        auto test_candidate = [&](int type, int k1, const vector<int>& rt1, long long b1, 
                                  int k2, const vector<int>& rt2, long long b2, 
                                  int r_in, int r_out) {
            if (b1 == -INF || (k2 != -1 && b2 == -INF)) return;

            long long delta = (b1 - route_obj[k1]);
            if (k2 != -1) delta += (b2 - route_obj[k2]);

            bool is_tabu = false;
            if (type == 1) { // Insert: r_in into k1
                if (tabu_veh_req[k1][r_in] >= iter) is_tabu = true;
            } else if (type == 2) { // Relocate: r_in into k2
                if (tabu_veh_req[k2][r_in] >= iter) is_tabu = true;
            } else if (type == 3) { // Swap on same vehicle: r_in into k1
                if (tabu_veh_req[k1][r_in] >= iter) is_tabu = true;
            } else if (type == 4) { // Drop: r_out removed from k1
                // Dropping is always permitted (not tabu)
            } else if (type == 5) { // Inter-route swap: r_in into k1, r_out into k2
                if (tabu_veh_req[k1][r_in] >= iter || tabu_veh_req[k2][r_out] >= iter) is_tabu = true;
            }

            if (is_tabu) {
                if (cur_total_obj + delta <= global_best_obj) return;
            }

            if (delta > best_move.delta || (delta == best_move.delta && rand() % 2 == 0)) {
                best_move = {delta, k1, k2, rt1, rt2, r_in, r_out, type};
            }
        };

        // OPERATOR 1: INSERT (Unserved request into any vehicle)
        for (int u = 1; u <= N + M; ++u) {
            if (veh_of[u] != 0 || !profitable[u]) continue;
            for (int k = 1; k <= K; ++k) {
                auto [b, rt] = best_insert(k, routes[k], u);
                test_candidate(1, k, rt, b, -1, {}, 0, u, 0);
            }
        }

        // OPERATOR 2: RELOCATE (Move request from k1 to k2)
        for (int r = 1; r <= N + M; ++r) {
            if (veh_of[r] == 0) continue;
            int k1 = veh_of[r];
            vector<int> rt1 = remove_req(routes[k1], r);
            long long b1 = eval_single_route(k1, rt1);

            for (int k2 = 1; k2 <= K; ++k2) {
                if (k2 == k1) continue;
                auto [b2, rt2] = best_insert(k2, routes[k2], r);
                test_candidate(2, k1, rt1, b1, k2, rt2, b2, r, r);
            }
        }

        // OPERATOR 3: SWAP ON SAME VEHICLE (Replace served r with unserved u)
        for (int r = 1; r <= N + M; ++r) {
            if (veh_of[r] == 0) continue;
            int k = veh_of[r];
            vector<int> rt_without_r = remove_req(routes[k], r);

            for (int u = 1; u <= N + M; ++u) {
                if (veh_of[u] != 0 || !profitable[u]) continue;
                auto [b, rt] = best_insert(k, rt_without_r, u);
                test_candidate(3, k, rt, b, -1, {}, 0, u, r);
            }
        }

        // OPERATOR 4: DROP (Remove served request to unserved pool)
        for (int r = 1; r <= N + M; ++r) {
            if (veh_of[r] == 0) continue;
            int k = veh_of[r];
            vector<int> rt = remove_req(routes[k], r);
            long long b = eval_single_route(k, rt);
            test_candidate(4, k, rt, b, -1, {}, 0, 0, r);
        }

        // OPERATOR 5: INTER-ROUTE SWAP (Cross swap r1 on k1 with r2 on k2)
        for (int r1 = 1; r1 <= N + M; ++r1) {
            if (veh_of[r1] == 0) continue;
            int k1 = veh_of[r1];
            for (int r2 = r1 + 1; r2 <= N + M; ++r2) {
                if (veh_of[r2] == 0) continue;
                int k2 = veh_of[r2];
                if (k1 == k2) continue;

                auto rt1_no_r1 = remove_req(routes[k1], r1);
                auto [b1, new_rt1] = best_insert(k1, rt1_no_r1, r2);
                if (b1 == -INF) continue;

                auto rt2_no_r2 = remove_req(routes[k2], r2);
                auto [b2, new_rt2] = best_insert(k2, rt2_no_r2, r1);
                if (b2 == -INF) continue;

                test_candidate(5, k1, new_rt1, b1, k2, new_rt2, b2, r2, r1);
            }
        }

        // APPLY BEST MOVE
        if (best_move.delta != -INF) {
            routes[best_move.k1] = best_move.rt1;
            route_obj[best_move.k1] = eval_single_route(best_move.k1, routes[best_move.k1]);

            if (best_move.k2 != -1) {
                routes[best_move.k2] = best_move.rt2;
                route_obj[best_move.k2] = eval_single_route(best_move.k2, routes[best_move.k2]);
            }

            int tenure = iter + TABU_TENURE_BASE + rand() % TABU_TENURE_RAND;

            if (best_move.type == 1) { // Insert: r_in into k1
                veh_of[best_move.r_in] = best_move.k1;
            } else if (best_move.type == 2) { // Relocate: r_in from k1 to k2
                tabu_veh_req[best_move.k1][best_move.r_in] = tenure;
                veh_of[best_move.r_in] = best_move.k2;
            } else if (best_move.type == 3) { // Swap on same: r_out removed from k1, r_in added
                tabu_veh_req[best_move.k1][best_move.r_out] = tenure;
                veh_of[best_move.r_out] = 0;
                veh_of[best_move.r_in] = best_move.k1;
            } else if (best_move.type == 4) { // Drop: r_out removed from k1
                tabu_veh_req[best_move.k1][best_move.r_out] = tenure;
                veh_of[best_move.r_out] = 0;
            } else if (best_move.type == 5) { // Inter-route swap: r_out from k1 to k2, r_in from k2 to k1
                tabu_veh_req[best_move.k1][best_move.r_out] = tenure;
                tabu_veh_req[best_move.k2][best_move.r_in] = tenure;
                veh_of[best_move.r_in] = best_move.k1;
                veh_of[best_move.r_out] = best_move.k2;
            }

            cur_total_obj += best_move.delta;

            if (cur_total_obj > epoch_best_obj) {
                epoch_best_obj = cur_total_obj;
                epoch_stagnant_iters = 0;
                epoch_best_state.obj = cur_total_obj;
                epoch_best_state.veh_assignment.assign(veh_of, veh_of + N + M + 1);
                epoch_best_state.routes.resize(K + 1);
                for (int k = 1; k <= K; ++k) epoch_best_state.routes[k] = routes[k];
            } else {
                epoch_stagnant_iters++;
            }

            if (cur_total_obj > global_best_obj) {
                global_best_obj = cur_total_obj;
                for (int k = 1; k <= K; ++k) best_routes[k] = routes[k];
            }
        } else {
            epoch_stagnant_iters++;
        }

        // RUIN MECHANISM (Triggered on epoch stagnation)
        if (epoch_stagnant_iters >= STAGNANT_LIMIT) {
            // Submit this epoch's mature peak state to Elite Pool before ruin
            if (epoch_best_state.obj > 0) {
                update_elite_pool(epoch_best_state);
            }

            // 1. Backtrack to an Elite solution from pool
            if (!elite_pool.empty()) {
                int best_elite_idx = 0;
                for (int i = 1; i < (int)elite_pool.size(); ++i) {
                    if (elite_pool[i].obj > elite_pool[best_elite_idx].obj) best_elite_idx = i;
                }

                int elite_sel = best_elite_idx;
                if (elite_pool.size() > 1 && (rand() % 100 >= INTENSIFICATION_PROB)) {
                    vector<int> other_indices;
                    for (int i = 0; i < (int)elite_pool.size(); ++i) {
                        if (i != best_elite_idx) other_indices.push_back(i);
                    }
                    elite_sel = other_indices[rand() % other_indices.size()];
                }

                for (int r = 1; r <= N + M; ++r) veh_of[r] = elite_pool[elite_sel].veh_assignment[r];
                for (int k = 1; k <= K; ++k) {
                    routes[k] = elite_pool[elite_sel].routes[k];
                    route_obj[k] = eval_single_route(k, routes[k]);
                }
                cur_total_obj = elite_pool[elite_sel].obj;
            }

            vector<int> served;
            for (int r = 1; r <= N + M; ++r) if (veh_of[r] != 0) served.push_back(r);

            if (!served.empty()) {
                // Round-robin Variety Ruin: Each mode gets a dedicated epoch
                int strat = (epoch_idx - 1) % RUIN_MODES_COUNT;

                if (strat == 3) { // Mode 3: Vehicle Dismantle
                    vector<int> busy;
                    for (int k = 1; k <= K; ++k) if (!routes[k].empty()) busy.push_back(k);
                    if (!busy.empty()) {
                        int k_tgt = busy[rand() % busy.size()];
                        for (int t : routes[k_tgt]) veh_of[abs(t)] = 0;
                        routes[k_tgt].clear();
                        route_obj[k_tgt] = 0;
                    }
                } else { // Modes 0, 1, 2: Correlated Request Ruin
                    int r_seed = served[rand() % served.size()];
                    sort(served.begin(), served.end(), [&](int a, int b) {
                        if (strat == 0) return c_mat[P[r_seed]][P[a]] < c_mat[P[r_seed]][P[b]];
                        if (strat == 1) return t_mat[P[r_seed]][P[a]] < t_mat[P[r_seed]][P[b]];
                        return abs(E[r_seed] - E[a]) < abs(E[r_seed] - E[b]);
                    });

                    int num_to_ruin = min((int)served.size(), RUIN_SIZE);
                    for (int step = 0; step < num_to_ruin; ++step) {
                        int pick_idx = rand() % min((int)served.size(), RUIN_TOP_CANDIDATE_POOL);
                        int r = served[pick_idx];
                        served.erase(served.begin() + pick_idx);

                        int k = veh_of[r];
                        if (k != 0) {
                            routes[k] = remove_req(routes[k], r);
                            route_obj[k] = eval_single_route(k, routes[k]);
                            veh_of[r] = 0;
                        }
                    }
                }

                cur_total_obj = 0;
                for (int k = 1; k <= K; ++k) cur_total_obj += route_obj[k];
            }

            // Start New Epoch
            epoch_idx++;
            epoch_iter = 0;
            epoch_best_obj = cur_total_obj;
            epoch_best_state.obj = cur_total_obj;
            epoch_best_state.veh_assignment.assign(veh_of, veh_of + N + M + 1);
            epoch_best_state.routes.resize(K + 1);
            for (int k = 1; k <= K; ++k) epoch_best_state.routes[k] = routes[k];
            epoch_stagnant_iters = 0;
            memset(tabu_veh_req, 0, sizeof(tabu_veh_req)); // Reset Tabu list for clean exploration in new epoch
        }
    }
}

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================
int main() {
    // Fast I/O for Online Judge
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);

    read_input();
    run_tabu_search();

    // Output strictly in the official contest format:
    // Line 1: Benefit (Objective)
    // Next K lines: m_k v_1 v_2 ... v_{m_k}
    cout << global_best_obj << "\n";
    for (int k = 1; k <= K; ++k) {
        vector<int> phys = get_physical_route(best_routes[k]);
        cout << phys.size();
        for (int v : phys) cout << " " << v;
        cout << "\n";
    }

    return 0;
}

