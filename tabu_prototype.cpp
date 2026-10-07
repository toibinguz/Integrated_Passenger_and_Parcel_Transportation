#include <bits/stdc++.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <iomanip>
using namespace std;
namespace fs = std::filesystem;

// ============================================================================
// HYPERPARAMETERS & CONSTANTS
// ============================================================================
int MAX_ITER = 10000;            // Total iterations for Tabu Search
int TABU_TENURE_BASE = 5;        // Base Tabu tenure
int TABU_TENURE_RAND = 10;       // Random amplitude of Tabu tenure
int STAGNANT_LIMIT = 50;         // Iterations without improvement to trigger Ruin
int RUIN_SIZE = 6;               // Number of requests ejected per Ruin step

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
int tabu_add[MAXV][MAXV];           // Tabu expiration matrix for physical edges
bool has_edge[MAXV][MAXV];          // Active physical edges in current solution

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================
int get_v_in(int task) { return (task > 0) ? P[abs(task)] : D[abs(task)]; }
int get_v_out(int task) { return (task > 0 && abs(task) <= N) ? D[abs(task)] : get_v_in(task); }

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

string get_current_timestamp() {
    auto now = chrono::system_clock::now();
    time_t now_c = chrono::system_clock::to_time_t(now);
    tm now_tm = *localtime(&now_c);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d-%H-%M-%S", &now_tm);
    return string(buf);
}

void save_solution(const string& res_path) {
    ofstream fout(res_path);
    if (!fout.is_open()) {
        cerr << "ERROR: Failed to open solution file for writing: " << res_path << "\n";
        return;
    }
    fout << global_best_obj << "\n";
    for (int k = 1; k <= K; ++k) {
        vector<int> phys = get_physical_route(best_routes[k]);
        fout << phys.size();
        for (int v : phys) fout << " " << v;
        fout << "\n";
    }
    fout.close();
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
    if (!(cin >> K >> N >> M)) { cerr << "ERROR: Failed to read input file!\n"; exit(1); }
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
// TABU SEARCH ENGINE
// ============================================================================
struct SolutionState {
    long long obj = 0;
    vector<int> veh_assignment; // size N + M + 1
    vector<vector<int>> routes;  // size K + 1
};

const int ELITE_POOL_SIZE = 4;
vector<SolutionState> elite_pool;

int calc_assignment_dist(const vector<int>& a, const vector<int>& b) {
    int d = 0;
    for (int r = 1; r <= N + M; ++r) {
        if (a[r] != b[r]) d++;
    }
    return d;
}

bool update_elite_pool(const SolutionState& cand, int iter, int epoch_idx, int epoch_iter) {
    if (cand.obj <= 0) return false;
    // Reject any candidate below 80% of global best to prevent premature solutions
    if (global_best_obj > 0 && cand.obj < global_best_obj * 0.80) return false;

    if (elite_pool.empty()) {
        elite_pool.push_back(cand);
        cerr << "  -> [ELITE POOL] Initialized Elite #0: Obj=" << cand.obj 
             << " at Iter " << iter << " (Epoch #" << epoch_idx << " @" << epoch_iter << ")\n";
        return true;
    }

    // Check if candidate is an identical clone (dist == 0) of an existing member
    for (int i = 0; i < (int)elite_pool.size(); ++i) {
        int d = calc_assignment_dist(cand.veh_assignment, elite_pool[i].veh_assignment);
        if (d == 0) {
            if (cand.obj > elite_pool[i].obj) {
                cerr << "  -> [ELITE POOL] Refined clone Elite #" << i 
                     << " (OldObj=" << elite_pool[i].obj << " -> NewObj=" << cand.obj << ")\n";
                elite_pool[i] = cand;
                return true;
            }
            return false; // Identical structure, equal or worse obj
        }
    }

    if ((int)elite_pool.size() < ELITE_POOL_SIZE) {
        elite_pool.push_back(cand);
        cerr << "  -> [ELITE POOL] Added Diverse Elite #" << elite_pool.size() - 1 
             << " (Obj=" << cand.obj << ") at Iter " << iter << " (Epoch #" << epoch_idx << " @" << epoch_iter << ")\n";
        return true;
    }

    // Pool is FULL: Apply Bi-criterion Biased Fitness Ranking
    // Pool has ELITE_POOL_SIZE solutions, plus cand = T solutions
    vector<SolutionState> all_sols = elite_pool;
    all_sols.push_back(cand);
    int T = all_sols.size();

    // 1. Fitness Rank R_fit: Rank by Obj descending (1 is best, T is worst)
    vector<int> fit_order(T);
    iota(fit_order.begin(), fit_order.end(), 0);
    sort(fit_order.begin(), fit_order.end(), [&](int a, int b) {
        return all_sols[a].obj > all_sols[b].obj;
    });
    vector<int> r_fit(T);
    for (int rank = 0; rank < T; ++rank) {
        r_fit[fit_order[rank]] = rank + 1;
    }

    // 2. Diversity Score: min distance to any other solution in all_sols
    vector<int> min_d(T, 1e9);
    for (int i = 0; i < T; ++i) {
        for (int j = 0; j < T; ++j) {
            if (i == j) continue;
            int d = calc_assignment_dist(all_sols[i].veh_assignment, all_sols[j].veh_assignment);
            min_d[i] = min(min_d[i], d);
        }
    }

    // Diversity Rank R_div: Rank by min_d descending (1 is most isolated/diverse, T is most crowded)
    vector<int> div_order(T);
    iota(div_order.begin(), div_order.end(), 0);
    sort(div_order.begin(), div_order.end(), [&](int a, int b) {
        return min_d[a] > min_d[b];
    });
    vector<int> r_div(T);
    for (int rank = 0; rank < T; ++rank) {
        r_div[div_order[rank]] = rank + 1;
    }

    // 3. Biased Rank: Score = R_fit + R_div (smaller is better, largest is worst/evicted)
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

    // If the candidate itself is the worst, reject it
    if (evict_idx == T - 1) {
        cerr << "  -> [ELITE POOL] Candidate rejected by Biased Fitness (Score=" << worst_score 
             << " [Rfit=" << r_fit[T - 1] << ", Rdiv=" << r_div[T - 1] << ", MinDist=" << min_d[T - 1] << "])\n";
        return false;
    }

    // Replace the evicted member in elite_pool
    cerr << "  -> [ELITE POOL] Biased Fitness Eviction: Evicted Elite #" << evict_idx 
         << " (Obj=" << elite_pool[evict_idx].obj << ", Score=" << worst_score 
         << " [Rfit=" << r_fit[evict_idx] << ", Rdiv=" << r_div[evict_idx] << ", MinDist=" << min_d[evict_idx] << "])"
         << " -> Admitted New Elite (Obj=" << cand.obj 
         << " [Rfit=" << r_fit[T - 1] << ", Rdiv=" << r_div[T - 1] << ", MinDist=" << min_d[T - 1] << "])\n";
    elite_pool[evict_idx] = cand;
    return true;
}

struct Move {
    long long delta = -INF;
    int k1 = -1, k2 = -1;
    vector<int> rt1, rt2;
    int r_in = 0, r_out = 0;
    int type = 0;
};

void run_tabu_search() {
    memset(has_edge, false, sizeof(has_edge));
    memset(tabu_add, 0, sizeof(tabu_add));
    memset(veh_of, 0, sizeof(veh_of));
    for (int k = 1; k <= K; ++k) route_obj[k] = 0;
    cur_total_obj = 0; global_best_obj = 0;
    
    elite_pool.clear();
    int epoch_idx = 1;
    int epoch_iter = 0;
    long long epoch_best_obj = 0;
    int epoch_stagnant_iters = 0;
    SolutionState epoch_best_state;

    auto apply_edges = [&](int k, const vector<int>& rt, bool add, int iter) {
        int curr = O[k];
        for (int v : get_physical_route(rt)) {
            has_edge[curr][v] = add;
            tabu_add[curr][v] = iter + TABU_TENURE_BASE + rand() % TABU_TENURE_RAND;
            curr = v;
        }
    };

    for (int iter = 1; iter <= MAX_ITER; ++iter) {
        epoch_iter++;
        Move best_move;
        int valid_neighbors = 0, tabu_hits = 0, tabu_overrides = 0;

        auto test_candidate = [&](int type, int k1, const vector<int>& rt1, long long b1, 
                                  int k2, const vector<int>& rt2, long long b2, 
                                  int r_in, int r_out) {
            if (b1 == -INF || (k2 != -1 && b2 == -INF)) return;
            valid_neighbors++;

            long long delta = (b1 - route_obj[k1]);
            if (k2 != -1) delta += (b2 - route_obj[k2]);

            bool is_tabu = false; int new_edges = 0;
            auto check_route_edges = [&](int k, const vector<int>& rt) {
                int curr = O[k];
                for (int v : get_physical_route(rt)) {
                    if (!has_edge[curr][v]) {
                        new_edges++;
                        if (tabu_add[curr][v] >= iter) is_tabu = true;
                    }
                    curr = v;
                }
            };
            check_route_edges(k1, rt1);
            if (k2 != -1) check_route_edges(k2, rt2);

            if (type != 4 && new_edges == 0) return;

            if (is_tabu) {
                tabu_hits++;
                if (cur_total_obj + delta > global_best_obj) tabu_overrides++;
                else return; // Rejected by Tabu tenure
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
        cerr << "[Iter " << iter << "] Valid Neighbors: " << valid_neighbors 
             << " | Tabu Hits: " << tabu_hits << " (Overrides: " << tabu_overrides << ")\n";

        if (best_move.delta != -INF) {
            apply_edges(best_move.k1, routes[best_move.k1], false, iter);
            apply_edges(best_move.k1, best_move.rt1, true, iter);
            routes[best_move.k1] = best_move.rt1;
            route_obj[best_move.k1] = eval_single_route(best_move.k1, routes[best_move.k1]);

            if (best_move.k2 != -1) {
                apply_edges(best_move.k2, routes[best_move.k2], false, iter);
                apply_edges(best_move.k2, best_move.rt2, true, iter);
                routes[best_move.k2] = best_move.rt2;
                route_obj[best_move.k2] = eval_single_route(best_move.k2, routes[best_move.k2]);
            }

            if (best_move.type == 1) {
                veh_of[best_move.r_in] = best_move.k1;
            } else if (best_move.type == 2) {
                veh_of[best_move.r_in] = best_move.k2;
            } else if (best_move.type == 3) {
                veh_of[best_move.r_out] = 0;
                veh_of[best_move.r_in] = best_move.k1;
            } else if (best_move.type == 4) {
                veh_of[best_move.r_out] = 0;
            } else if (best_move.type == 5) {
                veh_of[best_move.r_in] = best_move.k1;
                veh_of[best_move.r_out] = best_move.k2;
            }

            cur_total_obj += best_move.delta;
            cerr << "  -> Best Neighbor Obj: " << cur_total_obj << " (Delta: " << best_move.delta << ")\n";

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
                cerr << "  -> *** NEW GLOBAL BEST FOUND! ***\n";
                cout << "  -> [Iter " << setw(5) << iter 
                     << " | Epoch " << setw(2) << epoch_idx 
                     << " @" << setw(3) << epoch_iter 
                     << "] NEW GLOBAL BEST: " << global_best_obj << "\n" << flush;
            }
        } else {
            epoch_stagnant_iters++;
            cerr << "  -> No valid neighbor found.\n";
        }
        cerr << "  -> Epoch Status: Epoch #" << epoch_idx << " (Iter " << epoch_iter 
             << ") | Peak In Epoch: " << epoch_best_obj 
             << " | Stagnant: " << epoch_stagnant_iters << "/" << STAGNANT_LIMIT 
             << " | Elite Pool: " << elite_pool.size() << "/" << ELITE_POOL_SIZE << "\n";
        cerr << "  -> Global Best Obj: " << global_best_obj << "\n";

        // RUIN MECHANISM (Triggered on epoch stagnation)
        if (epoch_stagnant_iters >= STAGNANT_LIMIT) {
            long long prev_epoch_peak = epoch_best_obj;
            cerr << "  -> [RUIN ACTIVATED] Epoch #" << epoch_idx << " exhausted after " 
                 << epoch_iter << " iters (Peak: " << prev_epoch_peak << ")\n";

            // Submit this epoch's best state to Elite Pool before ruin
            if (epoch_best_state.obj > 0) {
                update_elite_pool(epoch_best_state, iter, epoch_idx, epoch_iter);
            }

            // 1. Backtrack to an Elite solution from pool
            int elite_sel = -1;
            if (!elite_pool.empty()) {
                int best_elite_idx = 0;
                for (int i = 1; i < (int)elite_pool.size(); ++i) {
                    if (elite_pool[i].obj > elite_pool[best_elite_idx].obj) best_elite_idx = i;
                }

                if (elite_pool.size() > 1 && (rand() % 100 >= 60)) {
                    vector<int> other_indices;
                    for (int i = 0; i < (int)elite_pool.size(); ++i) {
                        if (i != best_elite_idx) other_indices.push_back(i);
                    }
                    elite_sel = other_indices[rand() % other_indices.size()];
                    int d = calc_assignment_dist(elite_pool[elite_sel].veh_assignment, elite_pool[best_elite_idx].veh_assignment);
                    cerr << "  -> [ELITE BACKTRACK] [DIVERSIFY 40%] Picked Diverse Elite #" << elite_sel 
                         << " (Obj: " << elite_pool[elite_sel].obj << ", Dist to Best: " << d << ") as seed for Ruin\n";
                } else {
                    elite_sel = best_elite_idx;
                    cerr << "  -> [ELITE BACKTRACK] [INTENSIFY 60%] Picked Best Elite #" << elite_sel 
                         << " (Obj: " << elite_pool[elite_sel].obj << ") as seed for Ruin\n";
                }

                for (int k = 1; k <= K; ++k) apply_edges(k, routes[k], false, iter);
                for (int r = 1; r <= N + M; ++r) veh_of[r] = elite_pool[elite_sel].veh_assignment[r];
                for (int k = 1; k <= K; ++k) {
                    routes[k] = elite_pool[elite_sel].routes[k];
                    apply_edges(k, routes[k], true, iter);
                    route_obj[k] = eval_single_route(k, routes[k]);
                }
                cur_total_obj = elite_pool[elite_sel].obj;
            }

            vector<int> served;
            for (int r = 1; r <= N + M; ++r) if (veh_of[r] != 0) served.push_back(r);

            if (!served.empty()) {
                // Round-robin Variety Ruin: Each mode gets a dedicated epoch
                int strat = (epoch_idx - 1) % 4;

                if (strat == 3) { // Mode 3: Vehicle Dismantle
                    vector<int> busy;
                    for (int k = 1; k <= K; ++k) if (!routes[k].empty()) busy.push_back(k);
                    if (!busy.empty()) {
                        int k_tgt = busy[rand() % busy.size()];
                        cerr << "  -> [RUIN MODE 3: VEHICLE DISMANTLE] Clearing vehicle #" << k_tgt << ": ";
                        apply_edges(k_tgt, routes[k_tgt], false, iter);
                        for (int t : routes[k_tgt]) {
                            veh_of[abs(t)] = 0;
                            cerr << abs(t) << " ";
                        }
                        routes[k_tgt].clear();
                        route_obj[k_tgt] = 0;
                        cerr << "\n";
                    }
                } else { // Modes 0, 1, 2: Correlated Request Ruin
                    int r_seed = served[rand() % served.size()];
                    sort(served.begin(), served.end(), [&](int a, int b) {
                        if (strat == 0) return c_mat[P[r_seed]][P[a]] < c_mat[P[r_seed]][P[b]];
                        if (strat == 1) return t_mat[P[r_seed]][P[a]] < t_mat[P[r_seed]][P[b]];
                        return abs(E[r_seed] - E[a]) < abs(E[r_seed] - E[b]);
                    });

                    int num_to_ruin = min((int)served.size(), RUIN_SIZE);
                    const char* mode_names[] = {"SPATIAL_COST", "TRAVEL_TIME", "TIME_WINDOW"};
                    cerr << "  -> [RUIN MODE " << strat << ": " << mode_names[strat] 
                         << "] Ejecting near req " << r_seed << ": ";
                    for (int step = 0; step < num_to_ruin; ++step) {
                        int pick_idx = rand() % min((int)served.size(), 4);
                        int r = served[pick_idx];
                        served.erase(served.begin() + pick_idx);

                        int k = veh_of[r];
                        if (k != 0) {
                            apply_edges(k, routes[k], false, iter);
                            routes[k] = remove_req(routes[k], r);
                            apply_edges(k, routes[k], true, iter);
                            route_obj[k] = eval_single_route(k, routes[k]);
                            veh_of[r] = 0;
                            cerr << r << " ";
                        }
                    }
                    cerr << "\n";
                }

                cur_total_obj = 0;
                for (int k = 1; k <= K; ++k) cur_total_obj += route_obj[k];
                cerr << "  -> Total Obj after Ruin: " << cur_total_obj << "\n";
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
            cerr << "  -> [EPOCH " << epoch_idx << " STARTED] Initial Obj: " << cur_total_obj << "\n";
            cout << "  -> [Iter " << setw(5) << iter << "] Epoch " << epoch_idx - 1 
                 << " Peak: " << prev_epoch_peak 
                 << " -> Epoch " << epoch_idx << " Started (Ruin Mode " << ((epoch_idx - 2) % 4) 
                 << ", Seed Elite #" << elite_sel << ")\n" << flush;
        }
    }
}

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================
int main(int argc, char** argv) {
    ios_base::sync_with_stdio(false); cin.tie(NULL);

    string test_filepath = "input.txt";
    if (argc > 1) {
        test_filepath = argv[1];
        if (!freopen(argv[1], "r", stdin)) { cerr << "ERROR: Failed to open input file!\n"; return 1; }
    } else {
        if (!freopen("input.txt", "r", stdin)) { cerr << "ERROR: Failed to open input file!\n"; return 1; }
    }
    if (argc > 2) {
        MAX_ITER = atoi(argv[2]);
    }

    // 1. Resolve testcase name, program name, and timestamp
    string test_name = fs::path(test_filepath).stem().string();
    string cpp_name = fs::path(__FILE__).stem().string();
    string timestamp = get_current_timestamp();

    // 2. Create subfolder inside output/
    string folder_name = test_name + "_" + cpp_name + "_" + timestamp;
    fs::path out_dir = fs::path("output") / folder_name;
    fs::create_directories(out_dir);

    // 3. Define output paths inheriting folder name format
    string res_file_path = (out_dir / (folder_name + "_ket_qua.txt")).string();
    string log_file_path = (out_dir / (folder_name + "_log.txt")).string();

    cout << "======================================================================\n";
    cout << "  STARTING TABU SEARCH SOLVER\n";
    cout << "  * Testcase       : " << test_name << " (" << test_filepath << ")\n";
    cout << "  * Source (.cpp)  : " << cpp_name << ".cpp\n";
    cout << "  * Max Iterations : " << MAX_ITER << "\n";
    cout << "  * Output Dir     : " << out_dir.string() << "\n";
    cout << "  * Result File    : " << res_file_path << "\n";
    cout << "  * Log File       : " << log_file_path << "\n";
    cout << "======================================================================\n" << flush;

    // 4. Redirect cerr to log file
    ofstream log_file(log_file_path);
    streambuf* old_cerr_buf = cerr.rdbuf(log_file.rdbuf());
    cerr << unitbuf;

    read_input();
    run_tabu_search();

    // 5. Save best solution
    save_solution(res_file_path);

    // 6. Restore cerr and finish
    cerr.rdbuf(old_cerr_buf);
    log_file.close();

    cout << "======================================================================\n";
    cout << "  SEARCH COMPLETED SUCCESSFULLY!\n";
    cout << "  * Global Best Objective : " << global_best_obj << "\n";
    cout << "  * Solution saved to     : " << res_file_path << "\n";
    cout << "  * Execution log saved to: " << log_file_path << "\n";
    cout << "======================================================================\n" << flush;

    return 0;
}