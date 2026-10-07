#include <bits/stdc++.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <iomanip>
using namespace std;
namespace fs = std::filesystem;

// ============================================================================
// THAM SỐ CẤU HÌNH THUẬT TOÁN (HYPERPARAMETERS)
// ============================================================================
int MAX_ITER = 10000;            // Tổng số thế hệ chạy Tabu Search
int TABU_TENURE_BASE = 5;       // Thời gian cấm Tabu cơ sở
int TABU_TENURE_RAND = 10;      // Biên độ ngẫu nhiên của Tabu
int STAGNANT_LIMIT = 50;         // Số thế hệ liên tiếp không cải thiện để kích hoạt Ruin
int RUIN_SIZE = 6;              // Số lượng request bị cưỡng chế gỡ bỏ trong 1 lần Ruin

const long long INF = 1e18;
const int MAXV = 1050;
const int MAX_REQ = 1050;

int K, N, M, V;
int O[MAXV], Q[MAXV];
int req_type[MAX_REQ], P[MAX_REQ], D[MAX_REQ], W[MAX_REQ];
int E[MAX_REQ], L_time[MAX_REQ], Ed[MAX_REQ], Ld[MAX_REQ];
int S[MAX_REQ], Rev[MAX_REQ];
int t_mat[MAXV][MAXV], c_mat[MAXV][MAXV];

// Kiểm tra nhanh cạnh vật lý khả thi
bool can_edge[MAXV][MAXV];
long long earliest_dep[MAXV];
long long latest_arr[MAXV];

// Trạng thái nghiệm
vector<int> routes[MAXV];           // Danh sách Task: r (Khách/Lấy hàng), -r (Trả hàng)
int veh_of[MAX_REQ];                // Xe đang phục vụ req r (0 nếu unserved)
long long route_obj[MAXV];          // Điểm số hiện tại của từng xe
long long cur_total_obj = 0;

vector<int> best_routes[MAXV];
long long global_best_obj = -INF;

bool profitable[MAX_REQ];           // Lọc khách sinh lời âm
int tabu_add[MAXV][MAXV];           // Bảng Tabu trên cạnh vật lý
bool has_edge[MAXV][MAXV];          // Cạnh vật lý đang tồn tại trong nghiệm hiện tại

// ============================================================================
// BỘ ĐO THỐNG KÊ HIỆU NĂNG
// ============================================================================
struct Profiler {
    double time_op1 = 0, time_op2 = 0, time_op3 = 0, time_op4 = 0, time_op5 = 0;
    double time_apply = 0, time_ruin = 0;
    long long count_eval = 0; double time_eval = 0;
    long long count_opt_route = 0; double time_opt_route = 0;
    long long count_best_insert = 0; double time_best_insert = 0;

    void print_final_report(int iters_done) {
        if (iters_done == 0) return;
        double total_tracked = time_op1 + time_op2 + time_op3 + time_op4 + time_op5 + time_apply + time_ruin;
        cerr << "\n=================== FINAL PROFILING REPORT (" << iters_done << " ITERS) ===================\n";
        cerr << fixed << setprecision(3);
        cerr << "1. OPERATORS BREAKDOWN (Average per Iteration):\n";
        cerr << "   - Op1 (Insert)           : " << (time_op1 / iters_done) / 1000.0 << " ms (" 
             << (total_tracked > 0 ? (time_op1 / total_tracked * 100.0) : 0) << "%)\n";
        cerr << "   - Op2 (Relocate)         : " << (time_op2 / iters_done) / 1000.0 << " ms (" 
             << (total_tracked > 0 ? (time_op2 / total_tracked * 100.0) : 0) << "%)\n";
        cerr << "   - Op3 (Swap same veh)    : " << (time_op3 / iters_done) / 1000.0 << " ms (" 
             << (total_tracked > 0 ? (time_op3 / total_tracked * 100.0) : 0) << "%)\n";
        cerr << "   - Op4 (Drop)             : " << (time_op4 / iters_done) / 1000.0 << " ms (" 
             << (total_tracked > 0 ? (time_op4 / total_tracked * 100.0) : 0) << "%)\n";
        cerr << "   - Op5 (Inter-route Swap) : " << (time_op5 / iters_done) / 1000.0 << " ms (" 
             << (total_tracked > 0 ? (time_op5 / total_tracked * 100.0) : 0) << "%)\n";
        cerr << "   - Apply Move & Tabu Edge : " << (time_apply / iters_done) / 1000.0 << " ms (" 
             << (total_tracked > 0 ? (time_apply / total_tracked * 100.0) : 0) << "%)\n";
        cerr << "   - Ruin Mechanism         : " << (time_ruin / iters_done) / 1000.0 << " ms (" 
             << (total_tracked > 0 ? (time_ruin / total_tracked * 100.0) : 0) << "%)\n";
        cerr << "   -> TOTAL TIME / ITER     : " << (total_tracked / iters_done) / 1000.0 << " ms\n\n";

        cerr << "2. CORE FUNCTIONS BOTTLENECK:\n";
        cerr << "   - eval_single_route()    : Called " << count_eval << " times | Total: " 
             << time_eval / 1000.0 << " ms | Avg: " << (count_eval ? time_eval / count_eval : 0) << " us/call\n";
        cerr << "   - optimize_route()       : Called " << count_opt_route << " times | Total: " 
             << time_opt_route / 1000.0 << " ms | Avg: " << (count_opt_route ? time_opt_route / count_opt_route : 0) << " us/call\n";
        cerr << "   - best_insert()          : Called " << count_best_insert << " times | Total: " 
             << time_best_insert / 1000.0 << " ms | Avg: " << (count_best_insert ? time_best_insert / count_best_insert : 0) << " us/call\n";
        cerr << "===============================================================================\n\n";
    }
} profiler;

#define TIMER_NOW() chrono::high_resolution_clock::now()
#define TIMER_DIFF_US(start, end) (chrono::duration<double, micro>(end - start).count())

int get_v_in(int task) { return (task > 0) ? P[abs(task)] : D[abs(task)]; }
int get_v_out(int task) { return (task > 0 && abs(task) <= N) ? D[abs(task)] : get_v_in(task); }

void init_edge_feasibility() {
    for (int i = 1; i <= V; ++i) {
        earliest_dep[i] = 0;
        latest_arr[i] = INF;
    }
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

vector<int> get_physical_route(const vector<int>& rt) {
    vector<int> phys;
    for (int t : rt) {
        if (t > 0 && t <= N) { phys.push_back(P[t]); phys.push_back(D[t]); }
        else if (t > 0) phys.push_back(P[t]);
        else phys.push_back(D[-t]);
    }
    return phys;
}

long long eval_single_route(int k, const vector<int>& rt) {
    auto t_start = TIMER_NOW();
    profiler.count_eval++;

    if (rt.empty()) {
        auto t_end = TIMER_NOW();
        profiler.time_eval += TIMER_DIFF_US(t_start, t_end);
        return 0;
    }
    long long time_now = 0; int load = 0; int curr = O[k];
    vector<bool> picked(N + M + 1, false), dropped(N + M + 1, false);
    long long benefit = 0;

    for (int task : rt) {
        int r = abs(task);
        int v_in = get_v_in(task);
        int v_out = get_v_out(task);

        time_now += t_mat[curr][v_in]; benefit -= c_mat[curr][v_in];
        
        if (task > 0) { // Đón khách hoặc Lấy hàng
            if (picked[r]) {
                auto t_end = TIMER_NOW();
                profiler.time_eval += TIMER_DIFF_US(t_start, t_end);
                return -INF;
            }
            time_now = max(time_now, (long long)E[r]);
            if (time_now > L_time[r]) {
                auto t_end = TIMER_NOW();
                profiler.time_eval += TIMER_DIFF_US(t_start, t_end);
                return -INF;
            }
            picked[r] = true;
            
            if (r <= N) { // Hành khách
                time_now += S[r] + t_mat[P[r]][D[r]]; benefit -= c_mat[P[r]][D[r]];
                time_now += S[r]; dropped[r] = true; benefit += Rev[r];
            } else { // Kiện hàng
                load += W[r];
                if (load > Q[k]) {
                    auto t_end = TIMER_NOW();
                    profiler.time_eval += TIMER_DIFF_US(t_start, t_end);
                    return -INF;
                }
                time_now += S[r];
            }
        } else { // Trả hàng (-r)
            if (!picked[r] || dropped[r]) {
                auto t_end = TIMER_NOW();
                profiler.time_eval += TIMER_DIFF_US(t_start, t_end);
                return -INF;
            }
            time_now = max(time_now, (long long)Ed[r]);
            if (time_now > Ld[r]) {
                auto t_end = TIMER_NOW();
                profiler.time_eval += TIMER_DIFF_US(t_start, t_end);
                return -INF;
            }
            load -= W[r];
            dropped[r] = true; benefit += Rev[r];
            time_now += S[r];
        }
        curr = v_out;
    }

    auto t_end = TIMER_NOW();
    profiler.time_eval += TIMER_DIFF_US(t_start, t_end);
    return benefit;
}

void optimize_route(int k, vector<int>& rt) {
    auto t_start = TIMER_NOW();
    profiler.count_opt_route++;

    if (rt.empty()) {
        auto t_end = TIMER_NOW();
        profiler.time_opt_route += TIMER_DIFF_US(t_start, t_end);
        return;
    }
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

    auto t_end = TIMER_NOW();
    profiler.time_opt_route += TIMER_DIFF_US(t_start, t_end);
}

pair<long long, vector<int>> best_insert(int k, const vector<int>& rt, int r) {
    auto t_start = TIMER_NOW();
    profiler.count_best_insert++;

    long long best_b = -INF;
    vector<int> best_rt;
    int n = rt.size();

    if (r <= N) { // Khách
        for (int pos = 0; pos <= n; ++pos) {
            int u = (pos == 0) ? O[k] : get_v_out(rt[pos - 1]);
            if (!can_edge[u][P[r]]) continue;
            if (pos < n && !can_edge[D[r]][get_v_in(rt[pos])]) continue;

            vector<int> cand = rt; cand.insert(cand.begin() + pos, r);
            long long b = eval_single_route(k, cand);
            if (b > best_b) { best_b = b; best_rt = cand; }
        }
    } else { // Hàng
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

    auto t_end = TIMER_NOW();
    profiler.time_best_insert += TIMER_DIFF_US(t_start, t_end);
    return {best_b, best_rt};
}

vector<int> remove_req(const vector<int>& rt, int r) {
    vector<int> res;
    for (int t : rt) if (abs(t) != r) res.push_back(t);
    return res;
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
    int stagnant_iters = 0;

    for (int iter = 1; iter <= MAX_ITER; ++iter) {
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
                // Aspiration Criterion: vượt kỷ lục toàn cục
                if (cur_total_obj + delta > global_best_obj) tabu_overrides++;
                else return;
            }

            if (delta > best_move.delta || (delta == best_move.delta && rand() % 2 == 0)) {
                best_move = {delta, k1, k2, rt1, rt2, r_in, r_out, type};
            }
        };

        // TOÁN TỬ 1: INSERT
        auto t1_start = TIMER_NOW();
        for (int u = 1; u <= N + M; ++u) {
            if (veh_of[u] != 0 || !profitable[u]) continue;
            for (int k = 1; k <= K; ++k) {
                auto [b, rt] = best_insert(k, routes[k], u);
                test_candidate(1, k, rt, b, -1, {}, 0, u, 0);
            }
        }
        auto t1_end = TIMER_NOW();
        profiler.time_op1 += TIMER_DIFF_US(t1_start, t1_end);

        // TOÁN TỬ 2: RELOCATE
        auto t2_start = TIMER_NOW();
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
        auto t2_end = TIMER_NOW();
        profiler.time_op2 += TIMER_DIFF_US(t2_start, t2_end);

        // TOÁN TỬ 3: SWAP ON SAME VEHICLE
        auto t3_start = TIMER_NOW();
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
        auto t3_end = TIMER_NOW();
        profiler.time_op3 += TIMER_DIFF_US(t3_start, t3_end);

        // TOÁN TỬ 4: DROP
        auto t4_start = TIMER_NOW();
        for (int r = 1; r <= N + M; ++r) {
            if (veh_of[r] == 0) continue;
            int k = veh_of[r];
            vector<int> rt = remove_req(routes[k], r);
            long long b = eval_single_route(k, rt);
            test_candidate(4, k, rt, b, -1, {}, 0, 0, r);
        }
        auto t4_end = TIMER_NOW();
        profiler.time_op4 += TIMER_DIFF_US(t4_start, t4_end);

        // TOÁN TỬ 5: INTER-ROUTE SWAP
        auto t5_start = TIMER_NOW();
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
        auto t5_end = TIMER_NOW();
        profiler.time_op5 += TIMER_DIFF_US(t5_start, t5_end);

        // ÁP DỤNG NƯỚC ĐI TỐT NHẤT
        auto t_apply_start = TIMER_NOW();
        cerr << "[Iter " << iter << "] Valid Neighbors: " << valid_neighbors 
             << " | Tabu Hits: " << tabu_hits << " (Overrides: " << tabu_overrides << ")\n";

        if (best_move.delta != -INF) {
            // Cập nhật Tabu vi sai: cấm tái tạo các cạnh vừa bị gỡ bỏ, làm mới tem các cạnh mới
            auto apply_diff_edges = [&](int k, const vector<int>& old_rt, const vector<int>& new_rt) {
                vector<pair<int, int>> old_edges, new_edges;
                int curr = O[k];
                for (int v : get_physical_route(old_rt)) { old_edges.push_back({curr, v}); curr = v; }
                curr = O[k];
                for (int v : get_physical_route(new_rt)) { new_edges.push_back({curr, v}); curr = v; }

                // Xóa các cạnh không còn tồn tại và gán Tabu (ngăn nước đi sau lập tức lắp lại)
                for (const auto& edge : old_edges) {
                    bool still_exists = false;
                    for (const auto& ne : new_edges) {
                        if (ne == edge) { still_exists = true; break; }
                    }
                    if (!still_exists) {
                        has_edge[edge.first][edge.second] = false;
                        tabu_add[edge.first][edge.second] = iter + TABU_TENURE_BASE + rand() % TABU_TENURE_RAND;
                    }
                }
                // Thêm các cạnh mới và làm mới tem Tabu
                for (const auto& edge : new_edges) {
                    has_edge[edge.first][edge.second] = true;
                    tabu_add[edge.first][edge.second] = iter + TABU_TENURE_BASE + rand() % TABU_TENURE_RAND;
                }
            };

            apply_diff_edges(best_move.k1, routes[best_move.k1], best_move.rt1);
            if (best_move.k2 != -1) {
                apply_diff_edges(best_move.k2, routes[best_move.k2], best_move.rt2);
            }

            // Cập nhật trạng thái
            routes[best_move.k1] = best_move.rt1;
            route_obj[best_move.k1] = eval_single_route(best_move.k1, routes[best_move.k1]);

            if (best_move.k2 != -1) {
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

            if (cur_total_obj > global_best_obj) {
                global_best_obj = cur_total_obj;
                for (int k = 1; k <= K; ++k) best_routes[k] = routes[k];
                cerr << "  -> *** NEW GLOBAL BEST FOUND! ***\n";
                cout << "  -> [Iter " << setw(5) << iter << "] NEW GLOBAL BEST: " << global_best_obj << "\n" << flush;
                stagnant_iters = 0;
            } else {
                stagnant_iters++;
            }
        } else {
            stagnant_iters++;
            cerr << "  -> No valid neighbor found.\n";
        }
        cerr << "  -> Global Best Obj: " << global_best_obj << "\n";
        auto t_apply_end = TIMER_NOW();
        profiler.time_apply += TIMER_DIFF_US(t_apply_start, t_apply_end);

        // CƠ CHẾ RUIN
        auto t_ruin_start = TIMER_NOW();
        if (stagnant_iters >= STAGNANT_LIMIT) {
            stagnant_iters = 0;

            vector<int> served_reqs;
            for (int r = 1; r <= N + M; ++r) if (veh_of[r] != 0) served_reqs.push_back(r);

            if (!served_reqs.empty()) {
                int strategy = rand() % 4;

                if (strategy == 3) {
                    vector<int> busy_vehs;
                    for (int k = 1; k <= K; ++k) if (!routes[k].empty()) busy_vehs.push_back(k);

                    if (!busy_vehs.empty()) {
                        int k_target = busy_vehs[rand() % busy_vehs.size()];
                        cerr << "  -> [RUIN MODE 3: VEHICLE DISMANTLE] Clearing vehicle #" << k_target << ": ";

                        int curr = O[k_target];
                        for (int v : get_physical_route(routes[k_target])) { 
                            has_edge[curr][v] = false; 
                            tabu_add[curr][v] = iter + TABU_TENURE_BASE + rand() % TABU_TENURE_RAND;
                            curr = v; 
                        }

                        vector<int> reqs_in_k;
                        for (int t : routes[k_target]) {
                            int r = abs(t);
                            if (find(reqs_in_k.begin(), reqs_in_k.end(), r) == reqs_in_k.end()) reqs_in_k.push_back(r);
                        }
                        for (int r : reqs_in_k) { veh_of[r] = 0; cerr << r << " "; }
                        routes[k_target].clear();
                        route_obj[k_target] = 0;
                        cerr << "\n";
                    }
                } else {
                    int r_seed = served_reqs[rand() % served_reqs.size()];

                    if (strategy == 0) {
                        sort(served_reqs.begin(), served_reqs.end(), [&](int a, int b) {
                            return c_mat[P[r_seed]][P[a]] < c_mat[P[r_seed]][P[b]];
                        });
                        cerr << "  -> [RUIN MODE 0: COST RUIN] Ejecting near req " << r_seed << ": ";
                    } else if (strategy == 1) {
                        sort(served_reqs.begin(), served_reqs.end(), [&](int a, int b) {
                            return t_mat[P[r_seed]][P[a]] < t_mat[P[r_seed]][P[b]];
                        });
                        cerr << "  -> [RUIN MODE 1: TIME RUIN] Ejecting near req " << r_seed << ": ";
                    } else {
                        sort(served_reqs.begin(), served_reqs.end(), [&](int a, int b) {
                            return abs(E[r_seed] - E[a]) < abs(E[r_seed] - E[b]);
                        });
                        cerr << "  -> [RUIN MODE 2: TIME-WINDOW RUIN] Ejecting near req " << r_seed << ": ";
                    }

                    int num_to_ruin = min((int)served_reqs.size(), RUIN_SIZE);
                    for (int step = 0; step < num_to_ruin; ++step) {
                        int pick_idx = rand() % min((int)served_reqs.size(), 4);
                        int r = served_reqs[pick_idx];
                        served_reqs.erase(served_reqs.begin() + pick_idx);

                        int k = veh_of[r];
                        if (k != 0) {
                            int curr = O[k];
                            for (int v : get_physical_route(routes[k])) { 
                                has_edge[curr][v] = false; 
                                tabu_add[curr][v] = iter + TABU_TENURE_BASE + rand() % TABU_TENURE_RAND;
                                curr = v; 
                            }

                            routes[k] = remove_req(routes[k], r);
                            route_obj[k] = eval_single_route(k, routes[k]);
                            veh_of[r] = 0;

                            curr = O[k];
                            for (int v : get_physical_route(routes[k])) { has_edge[curr][v] = true; curr = v; }

                            cerr << r << " ";
                        }
                    }
                    cerr << "\n";
                }

                cur_total_obj = 0;
                for (int k = 1; k <= K; ++k) cur_total_obj += route_obj[k];
                cerr << "  -> Total Obj after Ruin: " << cur_total_obj << "\n";
            }
        }
        auto t_ruin_end = TIMER_NOW();
        profiler.time_ruin += TIMER_DIFF_US(t_ruin_start, t_ruin_end);
    }

    profiler.print_final_report(MAX_ITER);

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

    // 1. Identify testcase name, program name, and execution timestamp
    string test_name = fs::path(test_filepath).stem().string();
    string cpp_name = fs::path(__FILE__).stem().string();
    string timestamp = get_current_timestamp();

    // 2. Create new subfolder inside output directory
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

    // 4. Redirect cerr stream to log file
    ofstream log_file(log_file_path);
    streambuf* old_cerr_buf = cerr.rdbuf(log_file.rdbuf());
    cerr << unitbuf;

    read_input();
    run_tabu_search();

    // 5. Save final best solution
    save_solution(res_file_path);

    // 6. Restore cerr stream and finish
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