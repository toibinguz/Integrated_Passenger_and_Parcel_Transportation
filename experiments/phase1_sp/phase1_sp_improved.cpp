#pragma GCC optimize("O3,unroll-loops")
#include <bits/stdc++.h>
using namespace std;
typedef long long ll;
namespace fs = std::filesystem;

// ============================================================================
// PARAMETERS & HYPERPARAMETERS (PHASE 1 ENHANCED: DFS + DUAL-GUIDED SET PACKING)
// ============================================================================
static double TIME_LIMIT_SEC = 10.0;              // Wall-clock time budget (seconds)
static const long long ENUM_STATE_CAP = 500000000;// State cap for DFS
static const size_t ENUM_COL_CAP = 20000000;      // Unique column memory cap
static const int LAGR_MAX_IT = 2000;              // Lagrangian subgradient max iterations
static const ll LAGR_WORK = 200000000;            // Max subgradient work
static const double ENUM_TIME_FRAC = 0.65;        // Max fraction of time for DFS enumeration
static double SP_STALL_SEC = 3.5;                 // Stagnation threshold for Set Packing SA
static const ll INFLL = (ll)4e18;

// ============================================================================
// FAST INPUT READER
// ============================================================================
static char* ibuf; 
static size_t ilen, ipos;
static void read_all() {
    size_t cap = 1 << 22; ibuf = (char*)malloc(cap); ilen = 0;
    for (;;) {
        if (ilen == cap) { cap <<= 1; ibuf = (char*)realloc(ibuf, cap); }
        size_t r = fread(ibuf + ilen, 1, cap - ilen, stdin);
        if (r == 0) break;
        ilen += r;
    }
}
static inline bool read_int(ll& out) {
    while (ipos < ilen) {
        char ch = ibuf[ipos];
        if (ch == '#') { while (ipos < ilen && ibuf[ipos] != '\n') ++ipos; }
        else if (ch == '-' || (ch >= '0' && ch <= '9')) break;
        else ++ipos;
    }
    if (ipos >= ilen) return false;
    bool neg = false;
    if (ibuf[ipos] == '-') { neg = true; ++ipos; }
    ll v = 0;
    while (ipos < ilen && ibuf[ipos] >= '0' && ibuf[ipos] <= '9') v = v * 10 + (ibuf[ipos++] - '0');
    out = neg ? -v : v;
    return true;
}
static inline int rd() { ll v = 0; read_int(v); return (int)v; }

// ============================================================================
// ULTRA-FAST RNG (Xorshift64)
// ============================================================================
static uint64_t rng_s = 0x9E3779B97F4A7C15ull;
static inline uint64_t rng() { rng_s ^= rng_s << 7; rng_s ^= rng_s >> 9; return rng_s; }
static inline double rand01() { return (rng() >> 11) * (1.0 / 9007199254740992.0); }

// ============================================================================
// PROBLEM DATA STRUCTURES
// ============================================================================
static int K, N, M, V, R, SV;
static vector<int> O, Q, Rev;
static vector<int> Tm, Cm;
static inline int T(int u, int v) { return Tm[u * SV + v]; }
static inline int C(int u, int v) { return Cm[u * SV + v]; }

static vector<int> nin, nout, nw, nic;
static vector<ll> ne, nl, ndur;
static vector<char> servable;

// ============================================================================
// COLUMN ENUMERATION STRUCTURES
// ============================================================================
struct Column { int k; ll profit; int rbeg, rlen, nbeg, nlen; };
static vector<Column> cols;
static vector<int> col_req, col_nodes;
static unordered_map<uint64_t, int> col_index;
static long long enum_states;
static bool enum_abort;
static chrono::steady_clock::time_point enum_deadline;
static vector<int> e_seq, e_open, e_reqs, e_sorted, e_by_l;
static vector<ll> e_l;
static vector<char> e_used;

static void enum_record(int k, ll profit) {
    e_sorted = e_reqs; sort(e_sorted.begin(), e_sorted.end());
    uint64_t h = 1469598103934665603ull ^ (uint64_t)k * 0x9E3779B97F4A7C15ull;
    for (int q : e_sorted) { 
        h ^= (uint64_t)q + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); 
        h *= 1099511628211ull; 
    }
    auto it = col_index.find(h);
    if (it != col_index.end()) {
        Column& c = cols[it->second];
        if (c.k != k || c.rlen != (int)e_sorted.size() || !equal(e_sorted.begin(), e_sorted.end(), col_req.begin() + c.rbeg)) return;
        if (profit > c.profit) { 
            c.profit = profit; 
            copy(e_seq.begin(), e_seq.end(), col_nodes.begin() + c.nbeg); 
        }
        return;
    }
    if (cols.size() >= ENUM_COL_CAP) { enum_abort = true; return; }
    col_index.emplace(h, (int)cols.size());
    cols.push_back({k, profit, (int)col_req.size(), (int)e_sorted.size(), (int)col_nodes.size(), (int)e_seq.size()});
    col_req.insert(col_req.end(), e_sorted.begin(), e_sorted.end());
    col_nodes.insert(col_nodes.end(), e_seq.begin(), e_seq.end());
}

static inline bool open_ok(ll dep, int v, int skip) {
    for (int o : e_open) if (o != skip && dep + T(v, nin[R + o]) > nl[R + o]) return false;
    return true;
}

static void enum_dfs(int k, int last, ll time, int load, ll profit) {
    if (enum_abort) return;
    if (++enum_states > ENUM_STATE_CAP) { enum_abort = true; return; }
    if ((enum_states & 4095) == 0 && chrono::steady_clock::now() > enum_deadline) { enum_abort = true; return; }
    
    if (e_open.empty() && !e_seq.empty() && profit > 0) enum_record(k, profit);
    
    for (size_t oi = 0; oi < e_open.size(); ++oi) {
        int q = e_open[oi], x = R + q;
        ll arr = time + T(last, nin[x]);
        if (arr > nl[x]) continue;
        ll dep = max(arr, ne[x]) + ndur[x];
        if (!open_ok(dep, nout[x], q)) continue;
        e_open[oi] = e_open.back(); e_open.pop_back(); e_seq.push_back(x);
        enum_dfs(k, nout[x], dep, load + nw[x], profit - C(last, nin[x]) + Rev[q]);
        e_seq.pop_back(); e_open.push_back(q); swap(e_open[oi], e_open.back());
    }

    for (size_t idx = lower_bound(e_l.begin(), e_l.end(), time) - e_l.begin(); idx < e_by_l.size(); ++idx) {
        int q = e_by_l[idx];
        if (e_used[q]) continue;
        ll arr = time + T(last, nin[q]);
        if (arr > nl[q]) continue;
        ll dep = max(arr, ne[q]) + ndur[q];
        if (q > N && (load + nw[q] > Q[k] || dep + T(nout[q], nin[R + q]) > nl[R + q])) continue;
        if (!open_ok(dep, nout[q], -1)) continue;
        e_used[q] = 1; e_seq.push_back(q); e_reqs.push_back(q);
        if (q <= N) enum_dfs(k, nout[q], dep, load, profit - C(last, nin[q]) - nic[q] + Rev[q]);
        else { 
            e_open.push_back(q); 
            enum_dfs(k, nout[q], dep, load + nw[q], profit - C(last, nin[q])); 
            e_open.pop_back(); 
        }
        e_used[q] = 0; e_seq.pop_back(); e_reqs.pop_back();
    }
}

// ============================================================================
// MAIN EXECUTION (ENHANCED PHASE 1: DUAL-GUIDED SET PACKING)
// ============================================================================
int main(int argc, char** argv) {
    ios_base::sync_with_stdio(false); cin.tie(NULL);
    auto t_start = chrono::steady_clock::now();

    string test_filepath = "";
    if (argc > 1) { 
        TIME_LIMIT_SEC = atof(argv[1]); 
        SP_STALL_SEC = min(8.0, max(2.0, 0.08 * TIME_LIMIT_SEC)); 
    }
    if (argc > 2) {
        test_filepath = argv[2];
        if (!freopen(argv[2], "r", stdin)) { cerr << "ERROR: Failed to open input file: " << argv[2] << "\n"; return 1; }
    }

    read_all();
    K = rd(); N = rd(); M = rd();
    R = N + M; V = 2 * N + 2 * M + K; SV = V + 1;

    O.assign(K + 1, 0); Q.assign(K + 1, 0); Rev.assign(R + 1, 0);
    for (int k = 1; k <= K; ++k) { O[k] = rd(); Q[k] = rd(); }

    nin.assign(2 * R + 1, 0); nout = nw = nic = nin;
    ne.assign(2 * R + 1, 0); nl = ndur = ne;
    servable.assign(R + 1, 1);

    vector<int> P(R + 1), D(R + 1), Sv(R + 1);
    for (int i = 1; i <= N; ++i) {
        P[i] = rd(); D[i] = rd(); int e = rd(), l = rd(); Sv[i] = rd(); Rev[i] = rd();
        ne[i] = e; nl[i] = l;
        if (e > l) servable[i] = 0;
    }
    for (int j = N + 1; j <= R; ++j) {
        P[j] = rd(); D[j] = rd(); nw[j] = rd();
        int e = rd(), l = rd(), ed = rd(), lde = rd(); Sv[j] = rd(); Rev[j] = rd();
        ne[j] = e; nl[j] = l; ne[R + j] = ed; nl[R + j] = lde; nw[R + j] = -nw[j];
        if (e > l || ed > lde) servable[j] = 0;
    }

    Tm.assign((size_t)SV * SV, 0); Cm.assign((size_t)SV * SV, 0);
    for (int i = 1; i <= V; ++i) for (int j = 1; j <= V; ++j) Tm[i * SV + j] = rd();
    for (int i = 1; i <= V; ++i) for (int j = 1; j <= V; ++j) Cm[i * SV + j] = rd();

    int maxQ = 0; for (int k = 1; k <= K; ++k) maxQ = max(maxQ, Q[k]);
    for (int r = 1; r <= R; ++r) {
        if (r <= N) {
            nin[r] = P[r]; nout[r] = D[r];
            ndur[r] = 2LL * Sv[r] + T(P[r], D[r]); nic[r] = C(P[r], D[r]);
        } else {
            nin[r] = nout[r] = P[r]; ndur[r] = Sv[r];
            nin[R + r] = nout[R + r] = D[r]; ndur[R + r] = Sv[r];
            if (nw[r] > maxQ) servable[r] = 0;
        }
    }

    vector<int> servable_list;
    for (int r = 1; r <= R; ++r) if (servable[r]) servable_list.push_back(r);

    // 1. ROUTE ENUMERATION VIA DFS
    e_by_l = servable_list;
    sort(e_by_l.begin(), e_by_l.end(), [](int a, int b) { return nl[a] < nl[b]; });
    e_l.clear(); for (int q : e_by_l) e_l.push_back(nl[q]);
    e_used.assign(R + 1, 0);
    enum_states = 0; enum_abort = false;
    enum_deadline = t_start + chrono::microseconds((ll)(TIME_LIMIT_SEC * ENUM_TIME_FRAC * 1e6));

    auto t_enum_start = chrono::steady_clock::now();
    for (int k = 1; k <= K && !enum_abort; ++k) {
        enum_dfs(k, O[k], 0, 0, 0);
    }
    double t_enum_sec = chrono::duration<double>(chrono::steady_clock::now() - t_enum_start).count();
    col_index = unordered_map<uint64_t, int>();

    if (enum_abort) {
        cerr << "[PHASE 1: ABORTED] State/Memory Cap or Timeout! states=" << enum_states 
             << " | cols=" << cols.size() << " | time=" << fixed << setprecision(4) << t_enum_sec << "s\n";
        cout << "0\n";
        for (int k = 1; k <= K; ++k) cout << "0\n";
        return 0;
    }

    const int NC = (int)cols.size();
    cerr << "[PHASE 1: ENUMERATION SUCCESS] states=" << enum_states 
         << " | unique_cols=" << NC 
         << " | time=" << fixed << setprecision(4) << t_enum_sec << "s\n";

    // Build Inverted Index: Request -> Columns that serve it
    vector<vector<int>> req_to_cols(R + 1);
    for (int j = 0; j < NC; ++j) {
        for (int i = 0; i < cols[j].rlen; ++i) {
            req_to_cols[col_req[cols[j].rbeg + i]].push_back(j);
        }
    }

    vector<vector<int>> tcols(K + 1);
    for (int j = 0; j < NC; ++j) tcols[cols[j].k].push_back(j);
    for (int k = 1; k <= K; ++k) sort(tcols[k].begin(), tcols[k].end(), [](int a, int b) { return cols[a].profit > cols[b].profit; });

    vector<int> owner(R + 1, -1), chosen(K + 1, -1);
    ll cur = 0;
    auto unassign = [&](int k) {
        int j = chosen[k]; if (j < 0) return;
        for (int i = 0; i < cols[j].rlen; ++i) owner[col_req[cols[j].rbeg + i]] = -1;
        cur -= cols[j].profit; chosen[k] = -1;
    };
    auto assign = [&](int k, int j) {
        chosen[k] = j; if (j < 0) return;
        for (int i = 0; i < cols[j].rlen; ++i) owner[col_req[cols[j].rbeg + i]] = j;
        cur += cols[j].profit;
    };
    auto is_free = [&](int j) {
        for (int i = 0; i < cols[j].rlen; ++i) if (owner[col_req[cols[j].rbeg + i]] >= 0) return false;
        return true;
    };

    // 2. GREEDY INITIAL SELECTION
    {
        vector<int> ord(NC); iota(ord.begin(), ord.end(), 0);
        sort(ord.begin(), ord.end(), [](int a, int b) { return cols[a].profit > cols[b].profit; });
        for (int j : ord) if (chosen[cols[j].k] < 0 && is_free(j)) assign(cols[j].k, j);
    }
    ll best = cur; vector<int> best_choice = chosen;
    ll greedy_best = best;

    // 3. LAGRANGIAN RELAXATION & DUAL MULTIPLIERS
    auto t_lagr_start = chrono::steady_clock::now();
    ll ub = LLONG_MAX;
    bool lagr_exact = false;
    vector<double> best_u(R + 1, 0.0);
    {
        vector<double> u(R + 1, 0.0), g(R + 1);
        double lambda = 2.0, best_bound = 1e300; int stall = 0;
        const int max_it = (int)min<ll>(LAGR_MAX_IT, LAGR_WORK / max<ll>(1, (ll)col_req.size()));
        vector<int> relax(K + 1, -1);

        for (int it = 0; it < max_it && NC > 0; ++it) {
            double L = 0; for (int q : servable_list) L += u[q];
            fill(g.begin(), g.end(), 1.0);
            for (int k = 1; k <= K; ++k) {
                double bv = 0; int bj = -1;
                for (int j : tcols[k]) {
                    double v = (double)cols[j].profit;
                    for (int i = 0; i < cols[j].rlen; ++i) v -= u[col_req[cols[j].rbeg + i]];
                    if (v > bv) { bv = v; bj = j; }
                }
                L += bv; relax[k] = bj;
                if (bj >= 0) for (int i = 0; i < cols[bj].rlen; ++i) g[col_req[cols[bj].rbeg + i]] -= 1.0;
            }
            if (L < best_bound - 1e-9) { 
                best_bound = L; stall = 0; 
                best_u = u;
            } else if (++stall >= 10) { lambda *= 0.5; stall = 0; }

            if (best_bound < (double)best + 1 - 1e-6) break;
            double norm = 0; for (int q : servable_list) { if (u[q] <= 0 && g[q] > 0) g[q] = 0; norm += g[q] * g[q]; }
            if (norm < 1e-12) {
                for (int k = 1; k <= K; ++k) unassign(k);
                for (int k = 1; k <= K; ++k) assign(k, relax[k]);
                if (cur > best) { best = cur; best_choice = chosen; }
                lagr_exact = true;
                break;
            }
            double step = lambda * (L - (double)best) / norm;
            for (int q : servable_list) u[q] = max(0.0, u[q] - step * g[q]);
            if (lambda < 1e-4) break;
        }
        ub = (ll)floor(best_bound + 1e-6);
    }
    double t_lagr_sec = chrono::duration<double>(chrono::steady_clock::now() - t_lagr_start).count();

    cerr << "[PHASE 1: LAGRANGIAN] greedy_obj=" << greedy_best 
         << " | post_lagr_obj=" << best 
         << " | UB=" << ub 
         << " | comp_slackness=" << (lagr_exact ? "YES" : "NO") 
         << " | time=" << fixed << setprecision(4) << t_lagr_sec << "s\n";

    // 4. DUAL REDUCED-COST SORTING & ELITE COLUMN SELECTION
    // Compute reduced cost: rc_j = profit_j - sum(u_r)
    vector<double> col_rc(NC, 0.0);
    for (int j = 0; j < NC; ++j) {
        double rc = (double)cols[j].profit;
        for (int i = 0; i < cols[j].rlen; ++i) rc -= best_u[col_req[cols[j].rbeg + i]];
        col_rc[j] = rc;
    }
    vector<vector<int>> tcols_rc = tcols;
    for (int k = 1; k <= K; ++k) {
        sort(tcols_rc[k].begin(), tcols_rc[k].end(), [&](int a, int b) { return col_rc[a] > col_rc[b]; });
    }

    // 5. ENHANCED MULTI-OPERATOR SET PACKING OPTIMIZER
    long long sa_iters = 0;
    auto t_sa_start = chrono::steady_clock::now();
    if (best < ub && NC > 0) {
        double avgp = 0; for (int j = 0; j < NC; ++j) avgp += cols[j].profit; avgp /= NC;
        double T0 = max(1e-9, 0.25 * avgp), TF = max(1e-9, 0.001 * avgp);
        vector<int> busy; for (int k = 1; k <= K; ++k) if (!tcols[k].empty()) busy.push_back(k);
        vector<pair<int, int>> log_;
        vector<int> refill;
        auto set_col = [&](int k, int j) { log_.push_back({k, chosen[k]}); unassign(k); assign(k, j); };
        
        double elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
        const double s0 = elapsed, budget = max(1e-9, TIME_LIMIT_SEC - s0);
        double temp = T0, last_impr = s0;
        int reheat_count = 0;

        auto sweep_free_vehicles = [&]() {
            for (int k = 1; k <= K; ++k) {
                if (chosen[k] < 0) {
                    for (int c : tcols[k]) {
                        if (is_free(c)) { assign(k, c); break; }
                    }
                }
            }
        };

        for (long long it = 0;; ++it) {
            sa_iters = it;
            if ((it & 255) == 0) {
                elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
                if (elapsed >= TIME_LIMIT_SEC) break;
                
                // Periodic Thermal Reheating if Stalled
                if (elapsed - last_impr > SP_STALL_SEC) {
                    // Reheat to 55% T0 and reset to best known state
                    temp = 0.55 * T0;
                    for (int k = 1; k <= K; ++k) unassign(k);
                    for (int k = 1; k <= K; ++k) if (best_choice[k] >= 0) assign(k, best_choice[k]);
                    sweep_free_vehicles();
                    last_impr = elapsed;
                    continue;
                }
                double tau = clamp((elapsed - s0) / budget, 0.0, 1.0);
                temp = T0 * pow(TF / T0, tau);
            }

            int roll = rng() % 100;
            ll before = cur;
            log_.clear(); refill.clear();

            if (roll < 30) {
                // OPERATOR 1: TARGETED UNMET DEMAND RUIN-AND-REPAIR
                // Find an unserved request
                int target_req = -1;
                for (int t = 0; t < 8; ++t) {
                    int cand_q = servable_list[rng() % servable_list.size()];
                    if (owner[cand_q] < 0 && !req_to_cols[cand_q].empty()) {
                        if (target_req == -1 || Rev[cand_q] > Rev[target_req]) target_req = cand_q;
                    }
                }
                if (target_req != -1) {
                    const auto& candidate_cols = req_to_cols[target_req];
                    int j = candidate_cols[rng() % min<size_t>(candidate_cols.size(), 8)];
                    int k = cols[j].k;
                    set_col(k, -1);
                    for (int i = 0; i < cols[j].rlen; ++i) {
                        int c = owner[col_req[cols[j].rbeg + i]];
                        if (c >= 0) { int k2 = cols[c].k; set_col(k2, -1); refill.push_back(k2); }
                    }
                    set_col(k, j);
                    for (int k2 : refill)
                        for (int c : tcols[k2]) if (is_free(c)) { set_col(k2, c); break; }
                } else {
                    // Fallback to random vehicle swap
                    int k = busy[rng() % busy.size()];
                    const auto& tc = (rng() & 1) ? tcols[k] : tcols_rc[k];
                    int j = tc[rng() % min<size_t>(tc.size(), 16)];
                    set_col(k, -1);
                    for (int i = 0; i < cols[j].rlen; ++i) {
                        int c = owner[col_req[cols[j].rbeg + i]];
                        if (c >= 0) { int k2 = cols[c].k; set_col(k2, -1); refill.push_back(k2); }
                    }
                    set_col(k, j);
                    for (int k2 : refill)
                        for (int c : tcols[k2]) if (is_free(c)) { set_col(k2, c); break; }
                }
            } else if (roll < 55) {
                // OPERATOR 2: DUAL REDUCED-COST GUIDED SWAP
                int k = busy[rng() % busy.size()];
                const auto& tc = tcols_rc[k];
                int j = (rng() % 4 == 0) ? tc[rng() % tc.size()] : tc[rng() % min<size_t>(tc.size(), 12)];
                if (rng() % 16 == 0) j = -1;
                if (j == chosen[k]) continue;

                set_col(k, -1);
                if (j >= 0) {
                    for (int i = 0; i < cols[j].rlen; ++i) {
                        int c = owner[col_req[cols[j].rbeg + i]];
                        if (c >= 0) { int k2 = cols[c].k; set_col(k2, -1); refill.push_back(k2); }
                    }
                    set_col(k, j);
                }
                for (int k2 : refill)
                    for (int c : tcols[k2]) if (is_free(c)) { set_col(k2, c); break; }
            } else {
                // OPERATOR 3: PROFIT-DENSITY SWAP WITH GREEDY REFILL
                int k = busy[rng() % busy.size()];
                const auto& tc = tcols[k];
                int j = (rng() % 4 == 0) ? tc[rng() % tc.size()] : tc[rng() % min<size_t>(tc.size(), 16)];
                if (rng() % 16 == 0) j = -1;
                if (j == chosen[k]) continue;

                set_col(k, -1);
                if (j >= 0) {
                    for (int i = 0; i < cols[j].rlen; ++i) {
                        int c = owner[col_req[cols[j].rbeg + i]];
                        if (c >= 0) { int k2 = cols[c].k; set_col(k2, -1); refill.push_back(k2); }
                    }
                    set_col(k, j);
                }
                for (int k2 : refill)
                    for (int c : tcols[k2]) if (is_free(c)) { set_col(k2, c); break; }
            }

            ll delta = cur - before;
            if (delta >= 0 || (double)delta > temp * log(rand01() + 1e-300)) {
                // Free column compaction
                sweep_free_vehicles();

                if (cur > best) {
                    best = cur; best_choice = chosen;
                    last_impr = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
                    cerr << "[PHASE 1 ENHANCED: NEW BEST] obj=" << best << " | gap=" << (ub - best) 
                         << " | iter=" << it << " | time=" << fixed << setprecision(3) << last_impr << "s\n";
                    if (best >= ub) break; // Proven optimal
                }
            } else {
                for (int i = (int)log_.size() - 1; i >= 0; --i) { 
                    unassign(log_[i].first); 
                    assign(log_[i].first, log_[i].second); 
                }
            }
        }
    }
    double total_time_sec = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();

    bool is_proven_optimal = (best >= ub || lagr_exact);
    cerr << "[PHASE 1 ENHANCED: RESULT] final_obj=" << best 
         << " | UB=" << ub 
         << " | gap=" << (ub - best) 
         << " | proven_optimal=" << (is_proven_optimal ? "YES" : "NO") 
         << " | total_time=" << fixed << setprecision(4) << total_time_sec << "s\n";

    // Output SOICT Solution
    vector<vector<int>> final_nd(K + 1);
    for (int k = 1; k <= K; ++k) {
        final_nd[k].assign(1, 0);
        int j = best_choice[k];
        if (j >= 0) final_nd[k].insert(final_nd[k].end(), col_nodes.begin() + cols[j].nbeg, col_nodes.begin() + cols[j].nbeg + cols[j].nlen);
    }

    string out;
    out.reserve(1 << 16);
    out += to_string(best) + '\n';
    for (int k = 1; k <= K; ++k) {
        const auto& nd = final_nd[k];
        int cnt = 0;
        for (int i = 1; i < (int)nd.size(); ++i) cnt += (nd[i] <= N) ? 2 : 1;
        out += to_string(cnt);
        for (int i = 1; i < (int)nd.size(); ++i) {
            int x = nd[i];
            if (x <= N) { out += ' '; out += to_string(nin[x]); out += ' '; out += to_string(nout[x]); }
            else { out += ' '; out += to_string(nin[x]); }
        }
        out += '\n';
    }
    fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}

