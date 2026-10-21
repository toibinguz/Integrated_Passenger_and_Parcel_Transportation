#pragma GCC optimize("O3,unroll-loops")
#include <iostream>
#include <vector>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <numeric>

using namespace std;
namespace fs = std::filesystem;
typedef long long ll;

// ============================================================================
// HYPERPARAMETERS & ALGORITHMIC CONTROL KNOBS
// (Compliant with ALGORITHM_SPECIFICATION.md & OPTIMIZATION_TECHNIQUES.md)
// ============================================================================
static double TIME_LIMIT_SEC = 2.0;       // Wall-clock search budget (seconds, default 2.0s)
static int MAX_ITER = 1000000;            // Maximum search iterations (bounded by time limit)
static int TABU_TENURE_BASE = 5;          // Base Tabu tenure (iterations)
static int TABU_TENURE_RAND = 10;         // Random tenure amplitude [0, TABU_TENURE_RAND)
static int STAGNANT_LIMIT = 50;           // Iterations without improvement to trigger Ruin
static int RUIN_SIZE = 8;                 // Number of requests ejected per Ruin step
static int RUIN_MODES_COUNT = 5;          // 5 Ruin modes (0: Cost, 1: Time, 2: Actual Arr, 3: Dismantle, 4: Random)
static int RUIN_TOP_CANDIDATE_POOL = 4;   // Window size for stochastic selection among sorted requests
static const int ELITE_POOL_SIZE = 6;     // Maximum capacity of the Elite Pool
static double ELITE_QUALIFICATION_RATIO = 0.80; // Minimum objective ratio vs Global Best to enter pool (80%)
static int INTENSIFICATION_PROB = 60;     // % probability to seed Ruin from Best Elite (vs Diverse Elite)
static const int ELITE_PROTECTED_COUNT = 2; // Top-ranking fitness solutions immune from diversity eviction

static const ll INFLL = (ll)4e18;

// ============================================================================
// ULTRA-FAST RNG (Xorshift64)
// ============================================================================
static uint64_t rng_s = 0x9E3779B97F4A7C15ull;
static inline uint64_t rng() {
    rng_s ^= rng_s << 7;
    rng_s ^= rng_s >> 9;
    return rng_s;
}
static inline int rand_int(int lo, int hi) {
    return lo + (int)(rng() % (uint64_t)(hi - lo + 1));
}

// ============================================================================
// PROBLEM DATA STRUCTURES & FLAT CONTIGUOUS MEMORY LAYOUT
// ============================================================================
static int K, N, M, V, R, SV;
static vector<int> O, Q;
static vector<int> req_type, P, D, W, S, Rev;
static vector<ll> E, L_time, Ed, Ld;
static vector<int> Tm, Cm; // Flat (V+1)x(V+1) matrices

static inline int T(int u, int v) { return Tm[u * SV + v]; }
static inline int C(int u, int v) { return Cm[u * SV + v]; }

// Route node representations:
// Passenger r -> node r (atomic macro node P_r -> D_r)
// Parcel r    -> pickup node r, dropoff node R + r
static vector<int> nin, nout, nw, nic;
static vector<ll> ne, nl, ndur;
static vector<vector<bool>> can_edge; // Edge feasibility lookup matrix

// Solution State
struct Route {
    vector<int> nd;   // nd[1..n] tasks (nd[0] unused)
    vector<ll> dep;   // dep[i]: departure time from position i
    vector<ll> lat;   // lat[i]: backward latest arrival slack
    vector<int> ld;   // ld[i]: cumulative parcel load
    ll cost = 0;
    int n() const { return (int)nd.size() - 1; }
};

static vector<Route> rt;
static vector<Route> best_routes;
static vector<int> veh_of;               // veh_of[r]: vehicle serving request r (0 if unserved)
static vector<int> best_veh_of;
static ll cur_total_obj = 0;
static ll global_best_obj = 0;

// Acceleration Structures: Version-tagged Cache
static vector<int> rver;
static int gver = 0;
struct CacheEntry { int ver = -1, i = 0, j = 0; ll cost = INFLL; };
static vector<CacheEntry> icache;        // [r * (K + 1) + k]

// Global Physical Directed Edge Tabu Matrix
static vector<vector<int>> tabu_add;     // [MAXV][MAXV]: Iteration when edge is free
static vector<vector<bool>> has_edge;    // [MAXV][MAXV]: Active physical edge in solution

// Helper: Timestamp & String formatting
static string get_current_timestamp() {
    auto now = chrono::system_clock::now();
    time_t now_c = chrono::system_clock::to_time_t(now);
    tm now_tm = *localtime(&now_c);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &now_tm);
    return string(buf);
}

// ============================================================================
// ROUTE RECOMPUTATION & O(1) BACKWARD SLACK CHECKING
// ============================================================================
static bool recompute_route(int k) {
    Route& r = rt[k];
    rver[k] = ++gver;
    int n = r.n();
    r.dep.resize(n + 1);
    r.lat.resize(n + 2);
    r.ld.resize(n + 1);
    r.dep[0] = 0;
    r.ld[0] = 0;

    int prev = O[k];
    ll cost = 0;
    for (int i = 1; i <= n; ++i) {
        int x = r.nd[i];
        ll arr = r.dep[i - 1] + T(prev, nin[x]);
        if (arr > nl[x]) return false;
        r.dep[i] = max(arr, ne[x]) + ndur[x];
        r.ld[i] = r.ld[i - 1] + nw[x];
        if (r.ld[i] > Q[k]) return false;
        cost += C(prev, nin[x]) + nic[x];
        prev = nout[x];
    }
    r.lat[n + 1] = INFLL;
    for (int i = n; i >= 1; --i) {
        int x = r.nd[i];
        ll v = nl[x];
        if (i < n) {
            ll u = r.lat[i + 1] - T(nout[x], nin[r.nd[i + 1]]) - ndur[x];
            if (u < v) v = u;
        }
        r.lat[i] = v;
    }
    r.cost = cost;
    return true;
}

// O(1) Slack-Checking Best Insertion of Request q into Route k
static ll best_insert(int k, int q, int& bi, int& bj) {
    const Route& r = rt[k];
    const int n = r.n();
    const int* nd = r.nd.data();
    const ll* dep = r.dep.data();
    const ll* lat = r.lat.data();
    const int* ld = r.ld.data();
    ll best = INFLL;
    bi = -1; bj = -1;

    if (q <= N) { // Passenger Request
        const int P_node = nin[q], D_node = nout[q];
        const ll e = ne[q], l = nl[q], du = ndur[q];
        const int ic = nic[q];
        for (int i = 0; i <= n; ++i) {
            if (dep[i] > l) break;
            int a = i ? nout[nd[i]] : O[k];
            if (!can_edge[a][P_node]) continue;
            ll arr = dep[i] + T(a, P_node);
            if (arr > l) continue;
            ll d = max(arr, e) + du;
            ll dc = C(a, P_node) + ic;
            if (i < n) {
                int b = nin[nd[i + 1]];
                if (!can_edge[D_node][b]) continue;
                if (d + T(D_node, b) > lat[i + 1]) continue;
                dc += C(D_node, b) - C(a, b);
            }
            if (dc < best) {
                best = dc;
                bi = i;
            }
        }
        return best;
    }

    // Parcel Request
    const int p = q, dn = R + q;
    const int Pp = nin[p], Dq = nin[dn], w = nw[p], cap = Q[k];
    const ll ep = ne[p], lp = nl[p], sp = ndur[p], eq = ne[dn], lq = nl[dn], sq = ndur[dn];
    const int cPD = C(Pp, Dq), tPD = T(Pp, Dq);

    for (int i = 0; i <= n; ++i) {
        if (dep[i] > lp) break;
        if (ld[i] + w > cap) continue;
        int a = i ? nout[nd[i]] : O[k];
        if (!can_edge[a][Pp]) continue;
        ll arr = dep[i] + T(a, Pp);
        if (arr > lp) continue;
        ll dp = max(arr, ep) + sp;
        ll cA = C(a, Pp);

        // Option 1: Immediate drop after pickup
        if (can_edge[Pp][Dq]) {
            ll aq = dp + tPD;
            if (aq <= lq) {
                ll dq = max(aq, eq) + sq;
                ll dc = cA + cPD;
                bool ok = true;
                if (i < n) {
                    int b = nin[nd[i + 1]];
                    if (!can_edge[Dq][b]) ok = false;
                    else if (dq + T(Dq, b) > lat[i + 1]) ok = false;
                    else dc += C(Dq, b) - C(a, b);
                }
                if (ok && dc < best) {
                    best = dc; bi = i; bj = i;
                }
            }
        }
        if (i == n) continue;

        int b0 = nin[nd[i + 1]];
        if (!can_edge[Pp][b0]) continue;
        ll base = cA + C(Pp, b0) - C(a, b0);
        ll cur = dp;
        int prev = Pp;
        bool sync = false;

        for (int j = i + 1; j <= n; ++j) {
            int x = nd[j];
            if (!sync) {
                ll ar = cur + T(prev, nin[x]);
                if (ar > nl[x]) break;
                ll dj = max(ar, ne[x]) + ndur[x];
                if (dj == dep[j]) sync = true;
                cur = dj;
            } else {
                cur = dep[j];
            }

            if (ld[j] + w > cap) break;
            if (cur > lq) break;
            int o = nout[x];
            prev = o;
            if (!can_edge[o][Dq]) continue;
            ll aq = cur + T(o, Dq);
            if (aq > lq) continue;
            ll dq = max(aq, eq) + sq;
            ll dc = base + C(o, Dq);
            if (j < n) {
                int b2 = nin[nd[j + 1]];
                if (!can_edge[Dq][b2]) continue;
                if (dq + T(Dq, b2) > lat[j + 1]) continue;
                dc += C(Dq, b2) - C(o, b2);
            }
            if (dc < best) {
                best = dc; bi = i; bj = j;
            }
        }
    }
    return best;
}

// Cached version of best_insert
static inline ll get_cached_best_insert(int k, int q, int& bi, int& bj) {
    CacheEntry& e = icache[q * (K + 1) + k];
    if (e.ver != rver[k]) {
        e.cost = best_insert(k, q, e.i, e.j);
        e.ver = rver[k];
    }
    bi = e.i; bj = e.j;
    return e.cost;
}

static void apply_insert(int k, int q, int bi, int bj) {
    auto& nd = rt[k].nd;
    if (q <= N) {
        nd.insert(nd.begin() + bi + 1, q);
    } else {
        nd.insert(nd.begin() + bi + 1, q);
        nd.insert(nd.begin() + bj + 2, R + q);
    }
    recompute_route(k);
}

static void apply_remove(int k, int q) {
    auto& nd = rt[k].nd;
    int dn = (q <= N) ? -1 : (R + q);
    auto it = nd.begin() + 1;
    while (it != nd.end()) {
        if (*it == q || *it == dn) it = nd.erase(it);
        else ++it;
    }
    recompute_route(k);
}

// Physical route extraction
static vector<int> get_physical_route(const Route& r) {
    vector<int> phys;
    phys.reserve(r.n() * 2);
    for (int i = 1; i <= r.n(); ++i) {
        int x = r.nd[i];
        if (x <= N) {
            phys.push_back(nin[x]);
            phys.push_back(nout[x]);
        } else {
            phys.push_back(nin[x]);
        }
    }
    return phys;
}

static vector<pair<int, int>> get_route_edges(int k, const Route& r) {
    vector<pair<int, int>> edges;
    int prev = O[k];
    for (int i = 1; i <= r.n(); ++i) {
        int x = r.nd[i];
        if (x <= N) {
            edges.push_back({prev, nin[x]});
            edges.push_back({nin[x], nout[x]});
            prev = nout[x];
        } else {
            edges.push_back({prev, nin[x]});
            prev = nin[x];
        }
    }
    return edges;
}

// Fast local re-optimization on modified route
static void optimize_single_route(int k) {
    Route& r = rt[k];
    if (r.n() <= 2) return;
    bool improved = true;
    while (improved) {
        improved = false;
        vector<int> tasks;
        for (int i = 1; i <= r.n(); ++i) {
            int x = r.nd[i];
            if (x <= R) tasks.push_back(x);
        }
        for (int q : tasks) {
            ll old_c = r.cost;
            Route backup_r = r;
            apply_remove(k, q);
            int bi, bj;
            ll add_c = best_insert(k, q, bi, bj);
            if (add_c < INFLL && r.cost + add_c < old_c) {
                apply_insert(k, q, bi, bj);
                improved = true;
                break;
            } else {
                r = backup_r;
                recompute_route(k);
            }
        }
    }
}

// ============================================================================
// ELITE POOL (QUALITY & DIVERSITY MANAGEMENT - SECTION 4)
// ============================================================================
struct SolutionState {
    ll obj = 0;
    vector<int> veh_assignment; // size R + 1
    vector<Route> routes;       // size K + 1
};

static vector<SolutionState> elite_pool;

static int calc_hamming_dist(const SolutionState& a, const SolutionState& b) {
    int diff = 0;
    for (int r = 1; r <= R; ++r) {
        if (a.veh_assignment[r] != b.veh_assignment[r]) diff++;
    }
    return diff;
}

static bool update_elite_pool(const SolutionState& cand, ostream& log_out) {
    if (cand.obj <= 0) return false;
    if (global_best_obj > 0 && cand.obj < global_best_obj * ELITE_QUALIFICATION_RATIO) return false;

    if (elite_pool.empty()) {
        elite_pool.push_back(cand);
        return true;
    }

    // Check for clone (Hamming distance == 0)
    for (size_t i = 0; i < elite_pool.size(); ++i) {
        if (calc_hamming_dist(cand, elite_pool[i]) == 0) {
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

    // Pool is FULL: Apply Bi-criterion Biased Fitness Ranking with Elitism Protection
    vector<SolutionState> all_sols = elite_pool;
    all_sols.push_back(cand);
    int T_pool = (int)all_sols.size();

    // 1. Fitness Rank
    vector<int> fit_order(T_pool);
    iota(fit_order.begin(), fit_order.end(), 0);
    sort(fit_order.begin(), fit_order.end(), [&](int a, int b) {
        return all_sols[a].obj > all_sols[b].obj;
    });
    vector<int> r_fit(T_pool);
    for (int rank = 0; rank < T_pool; ++rank) r_fit[fit_order[rank]] = rank + 1;

    // 2. Diversity Rank (min Hamming distance)
    vector<int> min_d(T_pool, 1e9);
    for (int i = 0; i < T_pool; ++i) {
        for (int j = 0; j < T_pool; ++j) {
            if (i == j) continue;
            min_d[i] = min(min_d[i], calc_hamming_dist(all_sols[i], all_sols[j]));
        }
    }
    vector<int> div_order(T_pool);
    iota(div_order.begin(), div_order.end(), 0);
    sort(div_order.begin(), div_order.end(), [&](int a, int b) {
        return min_d[a] > min_d[b];
    });
    vector<int> r_div(T_pool);
    for (int rank = 0; rank < T_pool; ++rank) r_div[div_order[rank]] = rank + 1;

    // 3. Score = R_fit + (1.0 - PROTECTED / T_pool) * R_div
    double lambda = 1.0 - (double)ELITE_PROTECTED_COUNT / (double)T_pool;
    int evict_idx = -1;
    double worst_score = -1e9;
    for (int i = 0; i < T_pool; ++i) {
        if (r_fit[i] <= ELITE_PROTECTED_COUNT) continue; // Protected
        double score = (double)r_fit[i] + lambda * (double)r_div[i];
        if (score > worst_score) {
            worst_score = score;
            evict_idx = i;
        }
    }
    if (evict_idx == -1) evict_idx = fit_order.back();

    // If candidate itself is evicted, reject
    if (evict_idx == T_pool - 1) return false;

    elite_pool[evict_idx] = cand;
    return true;
}

// ============================================================================
// TABU SEARCH MOVE DEFINITION & APPLICATION
// ============================================================================
struct TabuMove {
    ll delta = -INFLL;
    int type = 0; // 1: Insert, 2: Relocate, 3: Replace, 4: Drop, 5: Swap
    int k1 = -1, k2 = -1;
    int r1 = 0, r2 = 0;
    int bi1 = -1, bj1 = -1;
    int bi2 = -1, bj2 = -1;
    vector<pair<int, int>> old_edges;
    vector<pair<int, int>> new_edges;
};

static void apply_edges_tabu(const vector<pair<int, int>>& old_edges,
                             const vector<pair<int, int>>& new_edges,
                             int iter) {
    for (const auto& e : old_edges) {
        has_edge[e.first][e.second] = false;
        int tenure = TABU_TENURE_BASE + rand_int(0, TABU_TENURE_RAND - 1);
        tabu_add[e.first][e.second] = iter + tenure;
    }
    for (const auto& e : new_edges) {
        has_edge[e.first][e.second] = true;
    }
}

// Check if candidate new edges violate tabu
static inline bool check_is_tabu(const vector<pair<int, int>>& new_edges, int iter) {
    for (const auto& e : new_edges) {
        if (!has_edge[e.first][e.second] && tabu_add[e.first][e.second] >= iter) {
            return true;
        }
    }
    return false;
}

// Calculate actual arrival time at pickup of request r
static inline ll get_actual_pickup_arr(int r) {
    int k = veh_of[r];
    if (k == 0) return -1;
    const Route& r_obj = rt[k];
    for (int i = 1; i <= r_obj.n(); ++i) {
        if (r_obj.nd[i] == r) {
            int prev = (i == 1) ? O[k] : nout[r_obj.nd[i - 1]];
            return r_obj.dep[i - 1] + T(prev, nin[r]);
        }
    }
    return -1;
}

// ============================================================================
// MAIN TABU SEARCH ENGINE
// ============================================================================
static void run_tabu_search(ostream& log_out, double time_budget) {
    auto start_time = chrono::steady_clock::now();

    // 1. Initialize State
    for (int u = 0; u <= V; ++u) {
        fill(tabu_add[u].begin(), tabu_add[u].end(), 0);
        fill(has_edge[u].begin(), has_edge[u].end(), false);
    }
    fill(veh_of.begin(), veh_of.end(), 0);
    fill(rver.begin(), rver.end(), 0);
    gver = 0;
    cur_total_obj = 0;
    global_best_obj = 0;
    elite_pool.clear();

    for (int k = 1; k <= K; ++k) {
        rt[k].nd.assign(1, 0);
        recompute_route(k);
    }

    int epoch_idx = 1;
    int epoch_iter = 0;
    ll epoch_best_obj = 0;
    int epoch_stagnant_iters = 0;
    SolutionState epoch_best_state;

    // Fast initial greedy insertion to seed Tabu Search
    for (int q = 1; q <= R; ++q) {
        ll best_gain = -INFLL; int best_k = -1, best_i = -1, best_j = -1;
        for (int k = 1; k <= K; ++k) {
            int bi, bj;
            ll dc = best_insert(k, q, bi, bj);
            if (dc < INFLL) {
                ll gain = Rev[q] - dc;
                if (gain > best_gain) {
                    best_gain = gain; best_k = k; best_i = bi; best_j = bj;
                }
            }
        }
        if (best_gain > 0) {
            apply_insert(best_k, q, best_i, best_j);
            veh_of[q] = best_k;
            cur_total_obj += best_gain;
        }
    }

    // Set initial edges
    for (int k = 1; k <= K; ++k) {
        auto ed = get_route_edges(k, rt[k]);
        for (const auto& e : ed) has_edge[e.first][e.second] = true;
    }

    global_best_obj = cur_total_obj;
    best_routes = rt;
    best_veh_of = veh_of;
    epoch_best_obj = cur_total_obj;
    epoch_best_state.obj = cur_total_obj;
    epoch_best_state.veh_assignment = veh_of;
    epoch_best_state.routes = rt;

    log_out << "[Iter 0] Initial Solution Obj = " << cur_total_obj << "\n";
    cerr << "[TABU_INITIAL] Obj = " << cur_total_obj << "\n";

    // Tabu Loop
    int iter = 0;
    while (iter < MAX_ITER) {
        iter++;
        epoch_iter++;

        // Check Wall-clock Time Budget
        double elapsed = chrono::duration<double>(chrono::steady_clock::now() - start_time).count();
        if (elapsed >= time_budget) break;

        TabuMove best_move;
        int valid_neighbors = 0, tabu_hits = 0, tabu_overrides = 0;

        // --------------------------------------------------------------------
        // OPERATOR 1: INSERT (Unserved request into vehicle)
        // --------------------------------------------------------------------
        for (int u = 1; u <= R; ++u) {
            if (veh_of[u] != 0) continue;
            for (int k = 1; k <= K; ++k) {
                int bi, bj;
                ll dc = get_cached_best_insert(k, u, bi, bj);
                if (dc >= INFLL) continue;
                valid_neighbors++;
                ll delta = Rev[u] - dc;

                // Candidate edges:
                // Old: (O[k], nin[nd[1]]) or (nout[nd[bi]], nin[nd[bi+1]])
                Route temp_rt = rt[k];
                auto old_ed = get_route_edges(k, temp_rt);
                auto& nd = temp_rt.nd;
                if (u <= N) nd.insert(nd.begin() + bi + 1, u);
                else { nd.insert(nd.begin() + bi + 1, u); nd.insert(nd.begin() + bj + 2, R + u); }
                auto new_ed = get_route_edges(k, temp_rt);

                bool is_tabu = check_is_tabu(new_ed, iter);
                if (is_tabu) {
                    tabu_hits++;
                    if (cur_total_obj + delta > global_best_obj) {
                        tabu_overrides++;
                        is_tabu = false;
                    }
                }
                if (!is_tabu) {
                    if (delta > best_move.delta || (delta == best_move.delta && (rng() & 1))) {
                        best_move = {delta, 1, k, -1, u, 0, bi, bj, -1, -1, old_ed, new_ed};
                    }
                }
            }
        }

        // --------------------------------------------------------------------
        // OPERATOR 2: RELOCATE (Move served request from k1 to k2)
        // --------------------------------------------------------------------
        for (int r = 1; r <= R; ++r) {
            int k1 = veh_of[r];
            if (k1 == 0) continue;
            ll cost_k1_orig = rt[k1].cost;
            Route temp_rt1 = rt[k1];
            auto old_ed1 = get_route_edges(k1, temp_rt1);
            
            // Remove r from k1
            int dn = (r <= N) ? -1 : (R + r);
            auto it = temp_rt1.nd.begin() + 1;
            while (it != temp_rt1.nd.end()) {
                if (*it == r || *it == dn) it = temp_rt1.nd.erase(it);
                else ++it;
            }
            // Temporary recompute
            ll cost_k1_new = 0;
            int prev = O[k1];
            for (int i = 1; i < (int)temp_rt1.nd.size(); ++i) {
                int x = temp_rt1.nd[i];
                cost_k1_new += C(prev, nin[x]) + nic[x];
                prev = nout[x];
            }
            ll drop_saving = cost_k1_orig - cost_k1_new;
            auto new_ed1 = get_route_edges(k1, temp_rt1);

            for (int k2 = 1; k2 <= K; ++k2) {
                if (k2 == k1) continue;
                int bi, bj;
                ll dc = get_cached_best_insert(k2, r, bi, bj);
                if (dc >= INFLL) continue;
                valid_neighbors++;

                ll delta = drop_saving - dc;

                Route temp_rt2 = rt[k2];
                auto old_ed2 = get_route_edges(k2, temp_rt2);
                auto& nd2 = temp_rt2.nd;
                if (r <= N) nd2.insert(nd2.begin() + bi + 1, r);
                else { nd2.insert(nd2.begin() + bi + 1, r); nd2.insert(nd2.begin() + bj + 2, R + r); }
                auto new_ed2 = get_route_edges(k2, temp_rt2);

                vector<pair<int, int>> combined_new = new_ed1;
                combined_new.insert(combined_new.end(), new_ed2.begin(), new_ed2.end());
                bool is_tabu = check_is_tabu(combined_new, iter);
                if (is_tabu) {
                    tabu_hits++;
                    if (cur_total_obj + delta > global_best_obj) {
                        tabu_overrides++;
                        is_tabu = false;
                    }
                }
                if (!is_tabu) {
                    if (delta > best_move.delta || (delta == best_move.delta && (rng() & 1))) {
                        vector<pair<int, int>> combined_old = old_ed1;
                        combined_old.insert(combined_old.end(), old_ed2.begin(), old_ed2.end());
                        best_move = {delta, 2, k1, k2, r, 0, bi, bj, -1, -1, combined_old, combined_new};
                    }
                }
            }
        }

        // --------------------------------------------------------------------
        // OPERATOR 3: REPLACE ON SAME VEHICLE (Swap served r with unserved u)
        // --------------------------------------------------------------------
        for (int r = 1; r <= R; ++r) {
            int k = veh_of[r];
            if (k == 0) continue;
            ll cost_k_orig = rt[k].cost;
            Route temp_rt = rt[k];
            auto old_ed = get_route_edges(k, temp_rt);

            // Temporarily remove r
            Route backup_k = rt[k];
            apply_remove(k, r);
            ll cost_after_remove = rt[k].cost;

            for (int u = 1; u <= R; ++u) {
                if (veh_of[u] != 0) continue;
                int bi, bj;
                ll dc = best_insert(k, u, bi, bj);
                if (dc >= INFLL) continue;
                valid_neighbors++;

                ll delta = Rev[u] - Rev[r] + (cost_k_orig - cost_after_remove) - dc;

                Route temp_rt_insert = rt[k];
                if (u <= N) temp_rt_insert.nd.insert(temp_rt_insert.nd.begin() + bi + 1, u);
                else {
                    temp_rt_insert.nd.insert(temp_rt_insert.nd.begin() + bi + 1, u);
                    temp_rt_insert.nd.insert(temp_rt_insert.nd.begin() + bj + 2, R + u);
                }
                auto new_ed = get_route_edges(k, temp_rt_insert);

                bool is_tabu = check_is_tabu(new_ed, iter);
                if (is_tabu) {
                    tabu_hits++;
                    if (cur_total_obj + delta > global_best_obj) {
                        tabu_overrides++;
                        is_tabu = false;
                    }
                }
                if (!is_tabu) {
                    if (delta > best_move.delta || (delta == best_move.delta && (rng() & 1))) {
                        best_move = {delta, 3, k, -1, r, u, bi, bj, -1, -1, old_ed, new_ed};
                    }
                }
            }
            rt[k] = backup_k;
            recompute_route(k);
        }

        // --------------------------------------------------------------------
        // OPERATOR 4: DROP (Eject served request to unserved pool)
        // --------------------------------------------------------------------
        for (int r = 1; r <= R; ++r) {
            int k = veh_of[r];
            if (k == 0) continue;
            ll cost_k_orig = rt[k].cost;
            Route temp_rt = rt[k];
            auto old_ed = get_route_edges(k, temp_rt);

            int dn = (r <= N) ? -1 : (R + r);
            auto it = temp_rt.nd.begin() + 1;
            while (it != temp_rt.nd.end()) {
                if (*it == r || *it == dn) it = temp_rt.nd.erase(it);
                else ++it;
            }
            ll cost_k_new = 0;
            int prev = O[k];
            for (int i = 1; i < (int)temp_rt.nd.size(); ++i) {
                int x = temp_rt.nd[i];
                cost_k_new += C(prev, nin[x]) + nic[x];
                prev = nout[x];
            }
            ll delta = (cost_k_orig - cost_k_new) - Rev[r];
            auto new_ed = get_route_edges(k, temp_rt);

            valid_neighbors++;
            bool is_tabu = check_is_tabu(new_ed, iter);
            if (is_tabu) {
                tabu_hits++;
                if (cur_total_obj + delta > global_best_obj) {
                    tabu_overrides++;
                    is_tabu = false;
                }
            }
            if (!is_tabu) {
                if (delta > best_move.delta || (delta == best_move.delta && (rng() & 1))) {
                    best_move = {delta, 4, k, -1, r, 0, -1, -1, -1, -1, old_ed, new_ed};
                }
            }
        }

        // --------------------------------------------------------------------
        // APPLY BEST TABU MOVE
        // --------------------------------------------------------------------
        log_out << "[Iter " << iter << "] Valid Neighbors: " << valid_neighbors
                << " | Tabu Hits: " << tabu_hits << " (Overrides: " << tabu_overrides << ")\n";

        if (best_move.delta != -INFLL) {
            if (best_move.type == 1) { // Insert
                apply_insert(best_move.k1, best_move.r1, best_move.bi1, best_move.bj1);
                veh_of[best_move.r1] = best_move.k1;
                optimize_single_route(best_move.k1);
            } else if (best_move.type == 2) { // Relocate
                apply_remove(best_move.k1, best_move.r1);
                apply_insert(best_move.k2, best_move.r1, best_move.bi1, best_move.bj1);
                veh_of[best_move.r1] = best_move.k2;
                optimize_single_route(best_move.k1);
                optimize_single_route(best_move.k2);
            } else if (best_move.type == 3) { // Replace
                apply_remove(best_move.k1, best_move.r1);
                apply_insert(best_move.k1, best_move.r2, best_move.bi1, best_move.bj1);
                veh_of[best_move.r1] = 0;
                veh_of[best_move.r2] = best_move.k1;
                optimize_single_route(best_move.k1);
            } else if (best_move.type == 4) { // Drop
                apply_remove(best_move.k1, best_move.r1);
                veh_of[best_move.r1] = 0;
                optimize_single_route(best_move.k1);
            }

            // Recalculate exact total objective
            ll sum_rev = 0, sum_cost = 0;
            for (int r = 1; r <= R; ++r) if (veh_of[r] != 0) sum_rev += Rev[r];
            for (int k = 1; k <= K; ++k) sum_cost += rt[k].cost;
            cur_total_obj = sum_rev - sum_cost;

            apply_edges_tabu(best_move.old_edges, best_move.new_edges, iter);

            log_out << "  -> Best Neighbor Obj: " << cur_total_obj << " (Delta: " << best_move.delta << ")\n";

            if (cur_total_obj > epoch_best_obj) {
                epoch_best_obj = cur_total_obj;
                epoch_stagnant_iters = 0;
                epoch_best_state.obj = cur_total_obj;
                epoch_best_state.veh_assignment = veh_of;
                epoch_best_state.routes = rt;
            } else {
                epoch_stagnant_iters++;
            }

            if (cur_total_obj > global_best_obj) {
                global_best_obj = cur_total_obj;
                best_routes = rt;
                best_veh_of = veh_of;
                log_out << "  -> *** NEW GLOBAL BEST FOUND! ***\n";
                log_out << "  -> Global Best Obj: " << global_best_obj << "\n";
                cerr << "  -> [Iter " << setw(5) << iter << " | Epoch " << epoch_idx 
                     << "] NEW GLOBAL BEST: " << global_best_obj << "\n" << flush;
            }
        } else {
            epoch_stagnant_iters++;
            log_out << "  -> No valid neighbor found.\n";
        }

        // --------------------------------------------------------------------
        // RUIN MECHANISM (TRIGGERED ON EPOCH STAGNATION - SECTION 5)
        // --------------------------------------------------------------------
        if (epoch_stagnant_iters >= STAGNANT_LIMIT) {
            ll prev_epoch_peak = epoch_best_obj;
            int strat = (epoch_idx - 1) % RUIN_MODES_COUNT;
            const char* mode_names[] = {
                "SPATIAL_COST", "TRAVEL_TIME", "ACTUAL_ARRIVAL_TIME", "VEHICLE_DISMANTLE", "RANDOM"
            };

            log_out << "  -> [RUIN ACTIVATED] Epoch #" << epoch_idx << " exhausted after "
                    << epoch_iter << " iters (Peak: " << prev_epoch_peak << " | Mode " << strat << ": " << mode_names[strat] << ")\n";

            // Submit epoch best state to Elite Pool
            if (epoch_best_state.obj > 0) {
                update_elite_pool(epoch_best_state, log_out);
            }

            // Backtrack from Elite Pool
            int elite_sel = -1;
            if (!elite_pool.empty()) {
                int best_elite_idx = 0;
                for (size_t i = 1; i < elite_pool.size(); ++i) {
                    if (elite_pool[i].obj > elite_pool[best_elite_idx].obj) best_elite_idx = i;
                }
                if (elite_pool.size() > 1 && (rand_int(0, 99) >= INTENSIFICATION_PROB)) {
                    vector<int> other_indices;
                    for (size_t i = 0; i < elite_pool.size(); ++i) {
                        if ((int)i != best_elite_idx) other_indices.push_back(i);
                    }
                    elite_sel = other_indices[rand_int(0, other_indices.size() - 1)];
                    log_out << "  -> [ELITE BACKTRACK] [DIVERSIFY " << (100 - INTENSIFICATION_PROB)
                            << "%] Picked Diverse Elite #" << elite_sel << " (Obj: " << elite_pool[elite_sel].obj << ") as seed\n";
                } else {
                    elite_sel = best_elite_idx;
                    log_out << "  -> [ELITE BACKTRACK] [INTENSIFY " << INTENSIFICATION_PROB
                            << "%] Picked Best Elite #" << elite_sel << " (Obj: " << elite_pool[elite_sel].obj << ") as seed\n";
                }

                // Restore seed
                veh_of = elite_pool[elite_sel].veh_assignment;
                rt = elite_pool[elite_sel].routes;
                cur_total_obj = elite_pool[elite_sel].obj;
                for (int k = 1; k <= K; ++k) recompute_route(k);
            }

            // Execute Ruin
            vector<int> served;
            for (int r = 1; r <= R; ++r) if (veh_of[r] != 0) served.push_back(r);

            if (!served.empty()) {
                if (strat == 3) { // Mode 3: Vehicle Dismantle
                    vector<int> busy;
                    for (int k = 1; k <= K; ++k) if (rt[k].n() > 0) busy.push_back(k);
                    if (!busy.empty()) {
                        int k_tgt = busy[rand_int(0, busy.size() - 1)];
                        for (int t = 1; t <= rt[k_tgt].n(); ++t) {
                            int task = rt[k_tgt].nd[t];
                            if (task <= R) veh_of[task] = 0;
                        }
                        rt[k_tgt].nd.assign(1, 0);
                        recompute_route(k_tgt);
                    }
                } else if (strat == 4) { // Mode 4: Random Ejection
                    // Shuffle served requests
                    for (int i = (int)served.size() - 1; i > 0; --i) {
                        int j = rand_int(0, i);
                        swap(served[i], served[j]);
                    }
                    int q_ruin = min((int)served.size(), RUIN_SIZE);
                    for (int s = 0; s < q_ruin; ++s) {
                        int req = served[s];
                        int k = veh_of[req];
                        if (k != 0) {
                            apply_remove(k, req);
                            veh_of[req] = 0;
                        }
                    }
                } else { // Modes 0, 1, 2: Seed-correlated Ejection
                    int r_seed = served[rand_int(0, served.size() - 1)];
                    ll seed_arr = (strat == 2) ? get_actual_pickup_arr(r_seed) : 0;

                    auto metric = [&](int u) -> ll {
                        if (strat == 0) return (ll)C(nin[r_seed], nin[u]);
                        if (strat == 1) return (ll)T(nin[r_seed], nin[u]);
                        // Strat 2: actual arrival time deviation
                        ll u_arr = get_actual_pickup_arr(u);
                        return abs(seed_arr - u_arr);
                    };

                    sort(served.begin(), served.end(), [&](int a, int b) {
                        return metric(a) < metric(b);
                    });

                    int q_ruin = min((int)served.size(), RUIN_SIZE);
                    for (int s = 0; s < q_ruin; ++s) {
                        int pick_idx = rand_int(0, min((int)served.size() - 1, RUIN_TOP_CANDIDATE_POOL - 1));
                        int req = served[pick_idx];
                        served.erase(served.begin() + pick_idx);
                        int k = veh_of[req];
                        if (k != 0) {
                            apply_remove(k, req);
                            veh_of[req] = 0;
                        }
                    }
                }

                // Re-evaluate total objective after Ruin
                ll sum_rev = 0, sum_cost = 0;
                for (int r = 1; r <= R; ++r) if (veh_of[r] != 0) sum_rev += Rev[r];
                for (int k = 1; k <= K; ++k) sum_cost += rt[k].cost;
                cur_total_obj = sum_rev - sum_cost;
            }

            // Start new epoch
            epoch_idx++;
            epoch_iter = 0;
            epoch_best_obj = cur_total_obj;
            epoch_best_state.obj = cur_total_obj;
            epoch_best_state.veh_assignment = veh_of;
            epoch_best_state.routes = rt;
            epoch_stagnant_iters = 0;
        }
    }

    double total_time = chrono::duration<double>(chrono::steady_clock::now() - start_time).count();
    cerr << "[TABU_COMPLETED] Iters = " << iter << " | Best Obj = " << global_best_obj 
         << " | Total Time = " << fixed << setprecision(2) << total_time << "s\n";
    log_out << "[TABU_COMPLETED] Total Iters = " << iter << " | Global Best Obj = " << global_best_obj 
            << " | Time = " << total_time << "s\n";
}

// ============================================================================
// INPUT READING & EDGE FEASIBILITY
// ============================================================================
static void read_input() {
    if (!(cin >> K >> N >> M)) {
        cerr << "ERROR: Failed to read input!\n";
        exit(1);
    }
    V = 2 * N + 2 * M + K;
    R = N + M;
    SV = V + 1;

    O.assign(K + 1, 0);
    Q.assign(K + 1, 0);
    for (int k = 1; k <= K; ++k) cin >> O[k] >> Q[k];

    req_type.assign(R + 1, 0);
    P.assign(R + 1, 0);
    D.assign(R + 1, 0);
    W.assign(R + 1, 0);
    S.assign(R + 1, 0);
    Rev.assign(R + 1, 0);
    E.assign(R + 1, 0);
    L_time.assign(R + 1, 0);
    Ed.assign(R + 1, 0);
    Ld.assign(R + 1, 0);

    for (int i = 1; i <= N; ++i) {
        req_type[i] = 1;
        cin >> P[i] >> D[i] >> E[i] >> L_time[i] >> S[i] >> Rev[i];
    }
    for (int j = 1; j <= M; ++j) {
        int r = N + j;
        req_type[r] = 2;
        cin >> P[r] >> D[r] >> W[r] >> E[r] >> L_time[r] >> Ed[r] >> Ld[r] >> S[r] >> Rev[r];
    }

    Tm.assign(SV * SV, 0);
    Cm.assign(SV * SV, 0);
    for (int i = 1; i <= V; ++i) {
        for (int j = 1; j <= V; ++j) {
            cin >> Tm[i * SV + j];
        }
    }
    for (int i = 1; i <= V; ++i) {
        for (int j = 1; j <= V; ++j) {
            cin >> Cm[i * SV + j];
        }
    }

    // Node attribute initialization
    int max_node = 2 * R + 1;
    nin.assign(max_node, 0);
    nout.assign(max_node, 0);
    nw.assign(max_node, 0);
    nic.assign(max_node, 0);
    ne.assign(max_node, 0);
    nl.assign(max_node, 0);
    ndur.assign(max_node, 0);

    for (int r = 1; r <= N; ++r) {
        nin[r] = P[r]; nout[r] = D[r];
        ne[r] = E[r]; nl[r] = L_time[r];
        ndur[r] = 2LL * S[r] + T(P[r], D[r]);
        nic[r] = C(P[r], D[r]);
        nw[r] = 0;
    }
    for (int j = 1; j <= M; ++j) {
        int p = N + j, d = R + p;
        nin[p] = P[p]; nout[p] = P[p];
        ne[p] = E[p]; nl[p] = L_time[p];
        ndur[p] = S[p]; nw[p] = W[p]; nic[p] = 0;

        nin[d] = D[p]; nout[d] = D[p];
        ne[d] = Ed[p]; nl[d] = Ld[p];
        ndur[d] = S[p]; nw[d] = -W[p]; nic[d] = 0;
    }

    // Edge Feasibility Lookup initialization
    can_edge.assign(V + 1, vector<bool>(V + 1, false));
    vector<ll> earliest_dep(V + 1, 0);
    vector<ll> latest_arr(V + 1, INFLL);

    for (int k = 1; k <= K; ++k) earliest_dep[O[k]] = 0;
    for (int r = 1; r <= N; ++r) {
        latest_arr[P[r]] = L_time[r];
        earliest_dep[P[r]] = E[r] + S[r];
        earliest_dep[D[r]] = E[r] + 2LL * S[r] + T(P[r], D[r]);
    }
    for (int j = 1; j <= M; ++j) {
        int r = N + j;
        latest_arr[P[r]] = L_time[r];
        earliest_dep[P[r]] = E[r] + S[r];
        latest_arr[D[r]] = Ld[r];
        ll earliest_arr_d = max(Ed[r], E[r] + S[r] + T(P[r], D[r]));
        earliest_dep[D[r]] = earliest_arr_d + S[r];
    }
    for (int u = 1; u <= V; ++u) {
        for (int v = 1; v <= V; ++v) {
            can_edge[u][v] = (earliest_dep[u] + T(u, v) <= latest_arr[v]);
        }
    }
    for (int u = 1; u <= V; ++u) {
        for (int k = 1; k <= K; ++k) can_edge[u][O[k]] = false;
    }
    for (int r = 1; r <= N; ++r) {
        for (int v = 1; v <= V; ++v) if (v != D[r]) can_edge[P[r]][v] = false;
        for (int u = 1; u <= V; ++u) if (u != P[r]) can_edge[u][D[r]] = false;
    }

    // Allocate solution structures
    rt.resize(K + 1);
    best_routes.resize(K + 1);
    veh_of.assign(R + 1, 0);
    best_veh_of.assign(R + 1, 0);
    rver.assign(K + 1, 0);
    icache.assign((R + 1) * (K + 1), CacheEntry());

    tabu_add.assign(V + 1, vector<int>(V + 1, 0));
    has_edge.assign(V + 1, vector<bool>(V + 1, false));
}

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================
int main(int argc, char** argv) {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);

    string test_filepath = "";
    for (int a = 1; a < argc; ++a) {
        string arg = argv[a];
        if (arg == "--time" && a + 1 < argc) {
            TIME_LIMIT_SEC = atof(argv[++a]);
        } else if (arg == "--seed" && a + 1 < argc) {
            rng_s = strtoull(argv[++a], NULL, 10);
        } else if (fs::exists(arg)) {
            test_filepath = arg;
        } else {
            double val = atof(arg.c_str());
            if (val > 0) TIME_LIMIT_SEC = val;
        }
    }

    if (!test_filepath.empty()) {
        if (!freopen(test_filepath.c_str(), "r", stdin)) {
            cerr << "ERROR: Failed to open input file: " << test_filepath << "\n";
            return 1;
        }
    }

    read_input();

    // Setup output directories
    string timestamp = get_current_timestamp();
    string test_name = fs::path(test_filepath).stem().string();
    fs::path run_dir = fs::path("experiments") / "tabu_prototype" / "output" / ("run_" + timestamp);
    fs::create_directories(run_dir);

    string res_path = (run_dir / "result.txt").string();
    string log_path = (run_dir / "log.txt").string();
    string rpt_path = (run_dir / "report.txt").string();

    ofstream log_file(log_path);

    // Run Tabu Search
    run_tabu_search(log_file, TIME_LIMIT_SEC);

    log_file.close();

    // 1. Output Standard Format to result.txt
    ofstream res_file(res_path);
    res_file << global_best_obj << "\n";
    for (int k = 1; k <= K; ++k) {
        vector<int> phys = get_physical_route(best_routes[k]);
        res_file << phys.size();
        for (int v : phys) res_file << " " << v;
        res_file << "\n";
    }
    res_file.close();

    // 2. Output Standard Format to stdout (for direct validator piping)
    cout << global_best_obj << "\n";
    for (int k = 1; k <= K; ++k) {
        vector<int> phys = get_physical_route(best_routes[k]);
        cout << phys.size();
        for (int v : phys) cout << " " << v;
        cout << "\n";
    }
    cout << flush;

    // 3. Write summary report.txt
    ofstream rpt_file(rpt_path);
    rpt_file << "======================================================================\n";
    rpt_file << "  TABU SEARCH WITH LNS & BACKWARD SLACK: RUN REPORT\n";
    rpt_file << "======================================================================\n";
    rpt_file << "Testcase              : " << test_name << "\n";
    rpt_file << "Time Budget (seconds) : " << TIME_LIMIT_SEC << "\n";
    rpt_file << "Global Best Objective : " << global_best_obj << "\n";
    rpt_file << "Vehicles (K)          : " << K << "\n";
    rpt_file << "Passengers (N)        : " << N << "\n";
    rpt_file << "Parcels (M)           : " << M << "\n";
    int served_cnt = 0;
    for (int r = 1; r <= R; ++r) if (best_veh_of[r] != 0) served_cnt++;
    rpt_file << "Served Requests       : " << served_cnt << "/" << R << " (" 
             << fixed << setprecision(1) << (100.0 * served_cnt / R) << "%)\n";
    rpt_file << "Result File           : " << res_path << "\n";
    rpt_file << "Log File              : " << log_path << "\n";
    rpt_file << "======================================================================\n";
    rpt_file.close();

    return 0;
}
