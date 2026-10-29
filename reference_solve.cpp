#pragma GCC optimize("O3,unroll-loops")
#include <bits/stdc++.h>
using namespace std;
typedef long long ll;
namespace fs = std::filesystem;

// ============================================================================
// 1. GLOBAL PARAMETERS & HYPERPARAMETERS
// ============================================================================
static double TIME_LIMIT_SEC = 70.0;              // Total search budget (seconds)
static double TIME_LIMIT_PHASE1 = 10.0;           // Phase 1 exact budget (seconds)
static long long MAX_ITERS = 0;                  // Phase 2 deterministic iteration budget (0 = wall-clock)

// --- PHASE 1 HYPERPARAMETERS (DFS Column Enumeration + Set Packing) ---
static const long long ENUM_STATE_CAP = 500000000;// State cap for DFS
static const size_t ENUM_COL_CAP = 20000000;      // Unique column memory cap
static const int LAGR_MAX_IT = 2000;              // Lagrangian subgradient max iterations
static const ll LAGR_WORK = 200000000;            // Max subgradient work
static const double ENUM_TIME_FRAC = 0.65;        // Max fraction of Phase 1 time for DFS (up to 6.5s in 10s budget)
static double SP_STALL_SEC = 3.5;                 // Stagnation threshold for Set Packing SA

// --- PHASE 2 HYPERPARAMETERS (Champion Guided Multi-Basin SISR-SA) ---
static const int NB_SIZE = 60;                    // Normalized spatio-temporal neighbor list size
static const double C_AVG = 10.0;                 // SISR: average removed requests
static const int L_MAX = 10;                      // SISR: maximum string length
static int CAND_NB = 3;                           // Unserved neighbors offered to recreate
static int CAND_RAND = 10;                        // Random unserved requests offered to recreate
static unsigned BLINK_MASK = 31;                  // Christiaens blink probability (1/32)
static const ll INFLL = (ll)4e18;

// ============================================================================
// 2. FAST INPUT READER & ULTRA-FAST RNG (Xorshift64)
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
    bool neg = (ibuf[ipos] == '-');
    if (neg) ++ipos;
    ll v = 0;
    while (ipos < ilen && ibuf[ipos] >= '0' && ibuf[ipos] <= '9') v = v * 10 + (ibuf[ipos++] - '0');
    out = neg ? -v : v;
    return true;
}
static inline int rd() { ll v = 0; read_int(v); return (int)v; }

static uint64_t rng_s = 0x9E3779B97F4A7C15ull;
static inline uint64_t rng() { rng_s ^= rng_s << 7; rng_s ^= rng_s >> 9; return rng_s; }
static inline int rand_int(int lo, int hi) { return lo + (int)(rng() % (uint64_t)(hi - lo + 1)); }
static inline double rand01() { return (rng() >> 11) * (1.0 / 9007199254740992.0); }

// ============================================================================
// 3. PROBLEM DATA & TOPOLOGY
// ============================================================================
static int K, N, M, V, R, SV;
static vector<int> O, Q, Rev, Tm, Cm;
static inline int T(int u, int v) { return Tm[u * SV + v]; }
static inline int C(int u, int v) { return Cm[u * SV + v]; }

static vector<int> nin, nout, nw, nic, servable_list;
static vector<ll> ne, nl, ndur;
static vector<char> servable;
static vector<vector<int>> nbr;
static double S_scale = 100.0;

// ============================================================================
// 4. SOLUTION STATE & VERIFICATION STRUCTURES
// ============================================================================
struct Route {
    vector<int> nd;   // nd[1..n] (nd[0] unused)
    vector<ll> dep;   // dep[i]: departure time from position i
    vector<ll> lat;   // lat[i]: backward latest arrival slack
    vector<int> ld;   // ld[i]: cumulative parcel load
    ll cost = 0;
    int n() const { return (int)nd.size() - 1; }
};

static vector<Route> rt, backup;
static vector<int> rver, veh;
static int gver = 0;
struct CacheEntry { int ver = -1, i = 0, j = 0; ll cost = 0; };
static vector<CacheEntry> icache;
static ll cur_obj = 0, best_obj = 0;
static vector<vector<int>> best_nd;

// Piggybacked Regret Memory
static vector<double> ema_regret;
static int priority_head = -1;

// Route evaluation & recompute
static bool recompute(int k) {
    Route& r = rt[k];
    rver[k] = ++gver;
    int n = r.n();
    r.dep.resize(n + 1); r.lat.resize(n + 2); r.ld.resize(n + 1);
    r.dep[0] = 0; r.ld[0] = 0;
    int prev = O[k]; ll cost = 0;
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
        if (i < n) v = min(v, r.lat[i + 1] - T(nout[x], nin[r.nd[i + 1]]) - ndur[x]);
        r.lat[i] = v;
    }
    r.cost = cost;
    return true;
}

static inline void save_best() {
    best_obj = cur_obj;
    for (int k = 1; k <= K; ++k) best_nd[k] = rt[k].nd;
}

static inline void load_best() {
    fill(veh.begin(), veh.end(), 0);
    for (int k = 1; k <= K; ++k) {
        rt[k].nd = best_nd[k]; recompute(k);
        for (int i = 1; i <= rt[k].n(); ++i) { int x = rt[k].nd[i]; if (x <= R) veh[x] = k; }
    }
    cur_obj = best_obj;
}

// ============================================================================
// 5. PHASE 1: EXACT COLUMN ENUMERATION & SET PACKING ENGINE
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

// Solves Phase 1. Returns TRUE if and only if solution is PROVEN OPTIMAL within TIME_LIMIT_PHASE1!
static bool column_solve(const vector<int>& s_list, chrono::steady_clock::time_point t_start) {
    auto t_p1_start = chrono::steady_clock::now();
    e_by_l = s_list;
    sort(e_by_l.begin(), e_by_l.end(), [](int a, int b) { return nl[a] < nl[b]; });
    e_l.clear(); for (int q : e_by_l) e_l.push_back(nl[q]);
    e_used.assign(R + 1, 0);
    enum_states = 0; enum_abort = false;
    
    // Time allocation for DFS: at most ENUM_TIME_FRAC of TIME_LIMIT_PHASE1
    enum_deadline = t_p1_start + chrono::microseconds((ll)(TIME_LIMIT_PHASE1 * ENUM_TIME_FRAC * 1e6));

    auto t_enum_start = chrono::steady_clock::now();
    for (int k = 1; k <= K && !enum_abort; ++k) {
        enum_dfs(k, O[k], 0, 0, 0);
    }
    double t_enum_sec = chrono::duration<double>(chrono::steady_clock::now() - t_enum_start).count();
    col_index = unordered_map<uint64_t, int>();

    if (enum_abort) {
        cerr << "[PHASE 1: ABORTED] State/Cap or 1s Timeout (states=" << enum_states 
             << ", cols=" << cols.size() << ", time=" << fixed << setprecision(4) << t_enum_sec << "s)\n";
        cols.clear(); col_req.clear(); col_nodes.clear();
        return false;
    }

    const int NC = (int)cols.size();
    cerr << "[PHASE 1: ENUM SUCCESS] states=" << enum_states << " | unique_cols=" << NC 
         << " | enum_time=" << fixed << setprecision(4) << t_enum_sec << "s\n";

    if (NC == 0) return false;

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

    // 1. Greedy Initial Selection
    {
        vector<int> ord(NC); iota(ord.begin(), ord.end(), 0);
        sort(ord.begin(), ord.end(), [](int a, int b) { return cols[a].profit > cols[b].profit; });
        for (int j : ord) if (chosen[cols[j].k] < 0 && is_free(j)) assign(cols[j].k, j);
    }
    ll best = cur; vector<int> best_choice = chosen;
    ll greedy_best = best;

    // 2. Lagrangian Relaxation Upper Bound
    ll ub = LLONG_MAX;
    bool lagr_exact = false;
    {
        vector<double> u(R + 1, 0.0), g(R + 1);
        double lambda = 2.0, best_bound = 1e300; int stall = 0;
        const int max_it = (int)min<ll>(LAGR_MAX_IT, LAGR_WORK / max<ll>(1, (ll)col_req.size()));
        vector<int> relax(K + 1, -1);

        for (int it = 0; it < max_it && NC > 0; ++it) {
            double L = 0; for (int q : s_list) L += u[q];
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
            if (L < best_bound - 1e-9) { best_bound = L; stall = 0; } 
            else if (++stall >= 10) { lambda *= 0.5; stall = 0; }

            if (best_bound < (double)best + 1 - 1e-6) break;
            double norm = 0; for (int q : s_list) { if (u[q] <= 0 && g[q] > 0) g[q] = 0; norm += g[q] * g[q]; }
            if (norm < 1e-12) {
                for (int k = 1; k <= K; ++k) unassign(k);
                for (int k = 1; k <= K; ++k) assign(k, relax[k]);
                if (cur > best) { best = cur; best_choice = chosen; }
                lagr_exact = true;
                break;
            }
            double step = lambda * (L - (double)best) / norm;
            for (int q : s_list) u[q] = max(0.0, u[q] - step * g[q]);
            if (lambda < 1e-4) break;
        }
        ub = (ll)floor(best_bound + 1e-6);
    }

    cerr << "[PHASE 1: LAGRANGIAN] greedy=" << greedy_best << " | post_lagr=" << best 
         << " | UB=" << ub << " | comp_slackness=" << (lagr_exact ? "EXACT" : "NO") << "\n";

    // 3. Set Packing SA (runs up to TIME_LIMIT_PHASE1 = 1.0s)
    if (best < ub && !lagr_exact && NC > 0) {
        double avgp = 0; for (int j = 0; j < NC; ++j) avgp += cols[j].profit; avgp /= NC;
        const double T0 = max(1e-9, 0.3 * avgp), TF = max(1e-9, 0.003 * avgp);
        vector<int> busy; for (int k = 1; k <= K; ++k) if (!tcols[k].empty()) busy.push_back(k);
        vector<pair<int, int>> log_;
        vector<int> refill;
        auto set_col = [&](int k, int j) { log_.push_back({k, chosen[k]}); unassign(k); assign(k, j); };
        
        double elapsed_p1 = chrono::duration<double>(chrono::steady_clock::now() - t_p1_start).count();
        const double s0 = elapsed_p1, budget_p1 = max(1e-9, TIME_LIMIT_PHASE1 - s0);
        double temp = T0, last_impr = s0;

        for (long long it = 0;; ++it) {
            if ((it & 255) == 0) {
                elapsed_p1 = chrono::duration<double>(chrono::steady_clock::now() - t_p1_start).count();
                if (elapsed_p1 >= TIME_LIMIT_PHASE1 || elapsed_p1 - last_impr > SP_STALL_SEC) break;
                temp = T0 * pow(TF / T0, (elapsed_p1 - s0) / budget_p1);
            }
            int k = busy[rng() % busy.size()];
            const auto& tc = tcols[k];
            int j = (rng() & 1) ? tc[rng() % tc.size()] : tc[rng() % min<size_t>(tc.size(), 16)];
            if (rng() % 16 == 0) j = -1;
            if (j == chosen[k]) continue;

            ll before = cur;
            log_.clear(); refill.clear();
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

            ll delta = cur - before;
            if (delta >= 0 || (double)delta > temp * log(rand01() + 1e-300)) {
                if (cur > best) {
                    best = cur; best_choice = chosen;
                    last_impr = chrono::duration<double>(chrono::steady_clock::now() - t_p1_start).count();
                    if (best >= ub) break; // Proven optimal!
                }
            } else {
                for (int i = (int)log_.size() - 1; i >= 0; --i) { 
                    unassign(log_[i].first); 
                    assign(log_[i].first, log_[i].second); 
                }
            }
        }
    }

    // Save primal solution into best_nd
    for (int k = 1; k <= K; ++k) {
        best_nd[k].assign(1, 0);
        int j = best_choice[k];
        if (j >= 0) best_nd[k].insert(best_nd[k].end(), col_nodes.begin() + cols[j].nbeg, col_nodes.begin() + cols[j].nbeg + cols[j].nlen);
    }

    bool is_proven_optimal = (best >= ub || lagr_exact);
    double total_p1_sec = chrono::duration<double>(chrono::steady_clock::now() - t_p1_start).count();
    cerr << "[PHASE 1: RESULT] best_LB=" << best << " | UB=" << ub 
         << " | gap=" << (ub - best) 
         << " | PROVEN_OPTIMAL=" << (is_proven_optimal ? "YES" : "NO") 
         << " | time=" << fixed << setprecision(4) << total_p1_sec << "s\n";

    return is_proven_optimal;
}

// ============================================================================
// 6. PHASE 2: CHAMPION GUIDED MULTI-BASIN SISR-SA ENGINE
// ============================================================================
static ll best_insert(int k, int q, int& bi, int& bj, bool blink) {
    const Route& r = rt[k];
    const int n = r.n();
    const int* nd = r.nd.data(); const ll* dep = r.dep.data(); const ll* lat = r.lat.data(); const int* ld = r.ld.data();
    ll best = INFLL;

    if (q <= N) {
        const int P = nin[q], D = nout[q], ic = nic[q];
        const ll e = ne[q], l = nl[q], du = ndur[q];
        for (int i = 0; i <= n; ++i) {
            if (dep[i] > l) break;
            int a = i ? nout[nd[i]] : O[k];
            ll arr = dep[i] + T(a, P);
            if (arr > l) continue;
            ll d = max(arr, e) + du, dc = C(a, P) + ic;
            if (i < n) {
                int b = nin[nd[i + 1]];
                if (d + T(D, b) > lat[i + 1]) continue;
                dc += C(D, b) - C(a, b);
            }
            if (dc < best && !(blink && (rng() & BLINK_MASK) == 0)) { best = dc; bi = i; }
        }
        return best;
    }

    const int p = q, dn = R + q, Pp = nin[p], Dq = nin[dn], w = nw[p], cap = Q[k];
    const ll ep = ne[p], lp = nl[p], sp = ndur[p], eq = ne[dn], lq = nl[dn], sq = ndur[dn];
    const int cPD = C(Pp, Dq), tPD = T(Pp, Dq);

    for (int i = 0; i <= n; ++i) {
        if (dep[i] > lp) break;
        if (ld[i] + w > cap) continue;
        int a = i ? nout[nd[i]] : O[k];
        ll arr = dep[i] + T(a, Pp);
        if (arr > lp) continue;
        ll dp = max(arr, ep) + sp, cA = C(a, Pp);

        if (dp + tPD <= lq) {
            ll dq = max(dp + tPD, eq) + sq, dc = cA + cPD;
            bool ok = true;
            if (i < n) {
                int b = nin[nd[i + 1]];
                if (dq + T(Dq, b) > lat[i + 1]) ok = false;
                else dc += C(Dq, b) - C(a, b);
            }
            if (ok && dc < best && !(blink && (rng() & BLINK_MASK) == 0)) { best = dc; bi = i; bj = i; }
        }
        if (i == n) continue;

        int b0 = nin[nd[i + 1]], prev = Pp;
        ll base = cA + C(Pp, b0) - C(a, b0), cur = dp;
        bool sync = false;

        for (int j = i + 1; j <= n; ++j) {
            int x = nd[j];
            if (!sync) {
                ll ar = cur + T(prev, nin[x]);
                if (ar > nl[x]) break;
                ll dj = max(ar, ne[x]) + ndur[x];
                if (dj == dep[j]) sync = true;
                cur = dj;
            } else cur = dep[j];

            if (ld[j] + w > cap || cur > lq) break;
            int o = nout[x]; prev = o;
            ll aq = cur + T(o, Dq);
            if (aq > lq) continue;
            ll dq = max(aq, eq) + sq, dc = base + C(o, Dq);
            if (j < n) {
                int b2 = nin[nd[j + 1]];
                if (dq + T(Dq, b2) > lat[j + 1]) continue;
                dc += C(Dq, b2) - C(o, b2);
            }
            if (dc < best && !(blink && (rng() & BLINK_MASK) == 0)) { best = dc; bi = i; bj = j; }
        }
    }
    return best;
}

static vector<int> touch_stamp, ruin_stamp, cand_stamp, touched, removed, cand, tmp_reqs;
static int stamp_id = 0;
static vector<pair<int, int>> veh_log;
static ll rev_delta = 0;

static inline void touch(int k) {
    if (touch_stamp[k] != stamp_id) { touch_stamp[k] = stamp_id; touched.push_back(k); backup[k] = rt[k]; }
}
static inline void set_veh(int r, int k) {
    veh_log.push_back({r, veh[r]});
    if (veh[r] == 0 && k != 0) rev_delta += Rev[r];
    else if (veh[r] != 0 && k == 0) rev_delta -= Rev[r];
    veh[r] = k;
}
static void do_insert(int k, int q, int i, int j) {
    touch(k);
    auto& nd = rt[k].nd;
    if (q <= N) nd.insert(nd.begin() + i + 1, q);
    else { nd.insert(nd.begin() + j + 1, R + q); nd.insert(nd.begin() + i + 1, q); }
    recompute(k);
    set_veh(q, k);
}

static void recreate_with_memory(const vector<int>& cands, bool blink) {
    for (int q : cands) {
        if (veh[q]) continue;
        ll best1 = INFLL, best2 = INFLL;
        int bk = -1, bi = 0, bj = 0;
        CacheEntry* ce = &icache[(size_t)q * (K + 1)];
        for (int k = 1; k <= K; ++k) {
            CacheEntry& e = ce[k];
            if (e.ver != rver[k]) { e.cost = best_insert(k, q, e.i, e.j, blink); e.ver = rver[k]; }
            if (e.cost < best1) { best2 = best1; best1 = e.cost; bk = k; bi = e.i; bj = e.j; }
            else if (e.cost < best2) { best2 = e.cost; }
        }
        ll g1 = (best1 < INFLL) ? max(0LL, (ll)Rev[q] - best1) : 0LL;
        ll g2 = (best2 < INFLL) ? max(0LL, (ll)Rev[q] - best2) : 0LL;
        ema_regret[q] = 0.80 * ema_regret[q] + 0.20 * (double)(g1 - g2);

        if (bk != -1 && Rev[q] - best1 > 0) {
            do_insert(bk, q, bi, bj);
        }
    }
}

static bool remove_from_route(int k) {
    Route& r = rt[k];
    touch(k);
    static vector<int> keep; keep = r.nd;
    size_t w = 1;
    for (size_t i = 1; i < r.nd.size(); ++i) {
        int x = r.nd[i], q = x > R ? x - R : x;
        if (find(tmp_reqs.begin(), tmp_reqs.end(), q) == tmp_reqs.end()) r.nd[w++] = x;
    }
    if (w == r.nd.size()) return false;
    r.nd.resize(w);
    if (!recompute(k)) { r.nd = keep; recompute(k); return false; }
    for (int q : tmp_reqs) if (veh[q]) { set_veh(q, 0); removed.push_back(q); }
    return true;
}

static inline int select_seed() {
    return servable_list[rng() % servable_list.size()];
}

static int ruin_guided(int seed) {
    removed.clear();
    priority_head = -1;
    int tot_nodes = 0, busy = 0;
    for (int k = 1; k <= K; ++k) if (rt[k].n()) { tot_nodes += rt[k].n(); ++busy; }
    if (!busy) return 1;

    double ls_max = min((double)L_MAX, (double)tot_nodes / busy);
    double ks_max = 4.0 * C_AVG / (1.0 + ls_max) - 1.0;
    int ks = (int)(rand01() * ks_max) + 1, done = 0;

    auto try_route = [&](int c) {
        int k = veh[c];
        if (!k || ruin_stamp[k] == stamp_id) return;
        ruin_stamp[k] = stamp_id;
        Route& r = rt[k];
        int n = r.n(), pos = 1;
        for (int i = 1; i <= n; ++i) {
            int q = (r.nd[i] > R) ? (r.nd[i] - R) : r.nd[i];
            if (q == c) { pos = i; break; }
        }
        int len = rand_int(1, max(1, min(n, (int)ls_max)));
        int st = rand_int(max(1, pos - len + 1), min(pos, n - len + 1));
        tmp_reqs.clear();
        for (int i = st; i < st + len; ++i) { int x = r.nd[i]; tmp_reqs.push_back(x > R ? x - R : x); }
        if (remove_from_route(k)) ++done;
    };

    int roll = rng() % 100;
    // Mode 2 (15%): Unserved-Cavity Ruin
    if (roll >= 75 && roll < 90) {
        int best_u = -1; double best_score = -1.0;
        for (int t = 0; t < 6; ++t) {
            int q = servable_list[rng() % servable_list.size()];
            if (veh[q] == 0) {
                double sc = (Rev[q] - nic[q]) + ema_regret[q];
                if (sc > best_score) { best_score = sc; best_u = q; }
            }
        }
        if (best_u != -1) {
            priority_head = seed = best_u;
            for (int c : nbr[best_u]) { if (done >= ks) break; if (veh[c]) try_route(c); }
            if (done > 0) return 2;
        }
    }
    // Mode 3 (10%): Slack-Bottleneck Substring Ruin
    if (roll >= 90) {
        int k1 = rand_int(1, K), k2 = rand_int(1, K);
        int k = (rt[k1].cost >= rt[k2].cost) ? k1 : k2;
        if (rt[k].n() > 0) {
            int worst_pos = 1, n = rt[k].n(); ll max_waste = -1;
            for (int i = 1; i <= n; ++i) {
                int x = rt[k].nd[i], prev_v = (i == 1) ? O[k] : nout[rt[k].nd[i - 1]];
                ll arr = rt[k].dep[i - 1] + T(prev_v, nin[x]);
                ll wait_t = max(0LL, ne[x] - arr), detour_c = C(prev_v, nin[x]) + nic[x];
                if (i < n) { int next_v = nin[rt[k].nd[i + 1]]; detour_c += C(nout[x], next_v) - C(prev_v, next_v); }
                if (wait_t + detour_c > max_waste) { max_waste = wait_t + detour_c; worst_pos = i; }
            }
            int b_req = (rt[k].nd[worst_pos] > R) ? (rt[k].nd[worst_pos] - R) : rt[k].nd[worst_pos];
            try_route(b_req);
            for (int c : nbr[b_req]) { if (done >= ks) break; try_route(c); }
            if (done > 0) return 3;
        }
    }

    // Mode 1 (75%): Spatio-Temporal String Ruin
    try_route(seed);
    for (int c : nbr[seed]) { if (done >= ks) break; try_route(c); }
    return 1;
}

static int order_candidates(vector<int>& cands) {
    if (cands.size() <= 1) return 0;
    int start_idx = (priority_head != -1 && cands[0] == priority_head) ? 1 : 0;
    int sub_n = (int)cands.size() - start_idx;
    if (sub_n <= 1) return 0;

    int sroll = rng() % 100;
    static vector<pair<double, int>> scored;
    if (sroll < 50) {
        scored.resize(sub_n);
        for (int i = 0; i < sub_n; ++i) {
            int q = cands[start_idx + i];
            scored[i] = { -ema_regret[q] * (0.85 + 0.30 * rand01()), q };
        }
        sort(scored.begin(), scored.end());
        for (int i = 0; i < sub_n; ++i) cands[start_idx + i] = scored[i].second;
        return 0;
    } else if (sroll < 75) {
        scored.resize(sub_n);
        for (int i = 0; i < sub_n; ++i) {
            int q = cands[start_idx + i];
            scored[i] = { -(double)(Rev[q] - nic[q]) * (0.90 + 0.20 * rand01()), q };
        }
        sort(scored.begin(), scored.end());
        for (int i = 0; i < sub_n; ++i) cands[start_idx + i] = scored[i].second;
        return 1;
    } else {
        for (int i = sub_n - 1; i > 0; --i) swap(cands[start_idx + i], cands[start_idx + (rng() % (i + 1))]);
        return 2;
    }
}

// SEDCA-V3 U-Valley Thermostat
struct AdaptiveThermostat {
    double t_max = 60.0;
    const double tau_fast = 0.10;
    const double tau_quench = 0.86;
    const double phase_up_end = 0.12;
    const double phase_drop_end = 0.45;
    const double PI_CONST = 3.14159265358979323846;

    double T_0 = 25.0, T_work = 4.0, T_min = 0.6, micro_delta = 5.0;
    bool calibrated = false;

    bool in_cycle_mode = false;
    double cycle_start_tau = 0.0, cycle_duration_tau = 0.0;
    double cycle_peak_T = 0.0, cycle_work_T = 0.0, cycle_base_T = 0.0;
    int cycle_count = 0;

    void auto_calibrate(const vector<double>& worsening_deltas, double scale_S) {
        vector<double> valid;
        valid.reserve(worsening_deltas.size());
        for (double d : worsening_deltas) if (d > 0.0 && d <= 0.80 * scale_S) valid.push_back(d);
        if (valid.size() < 20) valid = worsening_deltas;

        if (valid.empty()) micro_delta = max(1.0, 0.04 * scale_S);
        else {
            sort(valid.begin(), valid.end());
            double p15 = valid[(valid.size() * 15) / 100];
            micro_delta = clamp(p15, 1.0, max(2.0, 0.06 * scale_S));
        }

        T_min  = max(0.2, micro_delta / 6.907);
        T_work = max(T_min * 3.0, micro_delta / 1.05);
        T_0    = T_work * 5.5;
        calibrated = true;
    }

    void start_new_cycle(double current_tau) {
        in_cycle_mode = true;
        cycle_start_tau = current_tau;
        ++cycle_count;

        double osc_progress = clamp((current_tau - tau_fast) / (tau_quench - tau_fast), 0.0, 1.0);
        double max_peak = 0.65 * T_0, min_peak = 1.50 * T_work;
        cycle_peak_T = min_peak * pow(max_peak / min_peak, 1.0 - osc_progress);
        cycle_work_T = T_work * pow(0.65, osc_progress);
        cycle_base_T = T_min  * pow(0.75, osc_progress);

        double base_span = tau_quench - tau_fast;
        double remaining_osc_tau = tau_quench - current_tau;
        double target_duration = base_span * (0.18 + 0.10 * osc_progress);
        cycle_duration_tau = min(target_duration, max(0.04, remaining_osc_tau));
    }

    pair<double, bool> get_temperature_tau(double tau) {
        tau = clamp(tau, 0.0, 1.0);
        bool triggered_reheat = false;

        if (tau < tau_fast) {
            double p = tau / tau_fast;
            double T = (p < 0.4) 
                ? (T_0 * pow(T_work / T_0, p / 0.4))
                : (T_work * pow(T_min / T_work, (p - 0.4) / 0.6));
            return { max(T, T_min), false };
        }

        if (tau >= tau_quench) {
            double quench_prog = clamp((tau - tau_quench) / max(0.001, 1.0 - tau_quench), 0.0, 1.0);
            double T = (0.7 * T_work) * pow((0.5 * T_min) / (0.7 * T_work), quench_prog);
            return { max(T, 0.2), false };
        }

        double phi_c = in_cycle_mode ? ((tau - cycle_start_tau) / max(0.0001, cycle_duration_tau)) : 1.0;
        if (!in_cycle_mode || phi_c >= 1.0) {
            start_new_cycle(tau);
            triggered_reheat = true;
            phi_c = 0.0;
        }
        phi_c = clamp(phi_c, 0.0, 1.0);

        double T = cycle_base_T;
        if (phi_c < phase_up_end) {
            double u = phi_c / phase_up_end;
            double cos_factor = 0.5 * (1.0 - cos(PI_CONST * u));
            T = cycle_base_T + (cycle_peak_T - cycle_base_T) * cos_factor;
        } else if (phi_c < phase_drop_end) {
            double u = (phi_c - phase_up_end) / (phase_drop_end - phase_up_end);
            T = cycle_peak_T * pow(cycle_work_T / cycle_peak_T, u);
        } else {
            double u = (phi_c - phase_drop_end) / (1.0 - phase_drop_end);
            T = cycle_work_T * pow(cycle_base_T / cycle_work_T, u);
        }

        return { max(T, 0.2), triggered_reheat };
    }

    pair<double, bool> get_temperature(double elapsed_time) {
        return get_temperature_tau(elapsed_time / max(0.001, t_max));
    }
};

// ============================================================================
// 7. MAIN ENGINE (TWO-PHASE HYBRID SOLVER)
// ============================================================================
int main(int argc, char** argv) {
    ios_base::sync_with_stdio(false); cin.tie(NULL);
    auto t_start = chrono::steady_clock::now();

    string test_filepath = "";
    if (argc > 1) TIME_LIMIT_SEC = atof(argv[1]);
    if (argc > 2) {
        test_filepath = argv[2];
        if (!freopen(argv[2], "r", stdin)) { cerr << "ERROR: Failed to open " << argv[2] << "\n"; return 1; }
    }
    long long run_seed = -1;
    if (argc > 3) {
        run_seed = atoll(argv[3]);
        rng_s = 0x9E3779B97F4A7C15ull ^ ((uint64_t)run_seed * 0xBF58476D1CE4E5B9ull + 0x94D049BB133111EBull);
        if (rng_s == 0) rng_s = 0x9E3779B97F4A7C15ull;
        for (int w = 0; w < 16; ++w) rng();
    }
    if (argc > 4) MAX_ITERS = atoll(argv[4]);
    if (argc > 5) {
        TIME_LIMIT_PHASE1 = atof(argv[5]);
        SP_STALL_SEC = max(0.5, 0.40 * TIME_LIMIT_PHASE1);
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

    int maxQ = *max_element(Q.begin(), Q.end());
    for (int r = 1; r <= R; ++r) {
        if (r <= N) {
            nin[r] = P[r]; nout[r] = D[r];
            ndur[r] = 2LL * Sv[r] + T(P[r], D[r]); nic[r] = C(P[r], D[r]);
        } else {
            nin[r] = nout[r] = P[r]; ndur[r] = Sv[r];
            nin[R + r] = nout[R + r] = D[r]; ndur[R + r] = Sv[r];
            if (nw[r] > maxQ) servable[r] = 0;
        }
        if (servable[r]) servable_list.push_back(r);
    }

    if (servable_list.empty() || K == 0) {
        printf("0\n");
        for (int k = 1; k <= K; ++k) printf("0\n");
        return 0;
    }

    best_nd.assign(K + 1, vector<int>(1, 0));
    rt.assign(K + 1, Route()); backup.assign(K + 1, Route());
    rver.assign(K + 1, 0); veh.assign(R + 1, 0);
    icache.assign((size_t)(R + 1) * (K + 1), CacheEntry());

    // ------------------------------------------------------------------------
    // ATTEMPT PHASE 1: EXACT COLUMN ENUMERATION & SET PACKING (Limit: 1.0s)
    // ------------------------------------------------------------------------
    bool p1_proven_optimal = column_solve(servable_list, t_start);
    long long total_p2_iters = 0;

    if (p1_proven_optimal) {
        cerr << "[SYSTEM] Solution is PROVEN GLOBALLY OPTIMAL in Phase 1! Skipping Phase 2.\n";
    } else {
        // ------------------------------------------------------------------------
        // ATTEMPT PHASE 2: GUIDED MULTI-BASIN SISR-SA (GM-SISR CHAMPION)
        // ------------------------------------------------------------------------
        cerr << "[SYSTEM] Phase 1 not proven optimal -> Executing Phase 2 (GM-SISR Champion)...\n";

        // 1. Initialize Energy Scale & Learned Regret Memory
        double sum_net = 0.0;
        ema_regret.assign(R + 1, 0.0);
        for (int q : servable_list) {
            double net = max(1.0, (double)(Rev[q] - nic[q]));
            ema_regret[q] = net;
            sum_net += net;
        }
        S_scale = max(1.0, sum_net / servable_list.size());

        // 2. Build Normalized Spatio-Temporal Neighbor List
        double sum_T = 0.0, sum_E = 0.0;
        long long pairs = 0;
        for (size_t i = 0; i < servable_list.size(); ++i) {
            int a = servable_list[i];
            for (size_t j = i + 1; j < servable_list.size(); ++j) {
                int b = servable_list[j];
                sum_T += 0.5 * (T(P[a], P[b]) + T(D[a], D[b]));
                sum_E += llabs(ne[a] - ne[b]);
                pairs++;
            }
        }
        double T_bar = (pairs > 0) ? max(1.0, sum_T / pairs) : 1.0;
        double E_bar = (pairs > 0) ? max(1.0, sum_E / pairs) : 1.0;

        nbr.assign(R + 1, {});
        vector<pair<double, int>> tmp_nb;
        for (int a : servable_list) {
            tmp_nb.clear();
            ll end_a = (a <= N) ? (ne[a] + ndur[a]) : (ne[R + a] + ndur[R + a]);
            for (int b : servable_list) if (b != a) {
                ll end_b = (b <= N) ? (ne[b] + ndur[b]) : (ne[R + b] + ndur[R + b]);
                double d_para = (0.5 * (T(P[a], P[b]) + T(D[a], D[b])) / T_bar) + (llabs(ne[a] - ne[b]) / E_bar);
                double d_seq1 = (T(D[a], P[b]) / T_bar) + (llabs(ne[b] - end_a) / E_bar);
                double d_seq2 = (T(D[b], P[a]) / T_bar) + (llabs(ne[a] - end_b) / E_bar);
                tmp_nb.push_back({min({d_para, d_seq1, d_seq2}), b});
            }
            int s = min((int)tmp_nb.size(), NB_SIZE);
            partial_sort(tmp_nb.begin(), tmp_nb.begin() + s, tmp_nb.end());
            for (int i = 0; i < s; ++i) nbr[a].push_back(tmp_nb[i].second);
        }

        touch_stamp.assign(K + 1, 0); ruin_stamp.assign(K + 1, 0); cand_stamp.assign(R + 1, 0);

        // 3. Solution Handoff / Initial Solution
        // Check if Phase 1 found a valid high-quality primal solution
        ll p1_obj = 0;
        bool p1_valid = true;
        for (int k = 1; k <= K; ++k) {
            rt[k].nd = best_nd[k];
            if (!recompute(k)) { p1_valid = false; break; }
            p1_obj -= rt[k].cost;
            for (int i = 1; i <= rt[k].n(); ++i) {
                int x = rt[k].nd[i];
                if (x <= R) p1_obj += Rev[x];
            }
        }

        if (p1_valid && p1_obj > 0) {
            cur_obj = p1_obj;
            fill(veh.begin(), veh.end(), 0);
            for (int k = 1; k <= K; ++k)
                for (int i = 1; i <= rt[k].n(); ++i) { int x = rt[k].nd[i]; if (x <= R) veh[x] = k; }
            save_best();
            cerr << "[SYSTEM] Phase 2 inherited Phase 1 solution: obj=" << p1_obj << "\n";
        } else {
            for (int k = 1; k <= K; ++k) { rt[k].nd.assign(1, 0); recompute(k); }
            fill(veh.begin(), veh.end(), 0);
            vector<int> init = servable_list;
            sort(init.begin(), init.end(), [&](int a, int b) { return Rev[a] - nic[a] > Rev[b] - nic[b]; });
            ++stamp_id; rev_delta = 0;
            recreate_with_memory(init, false);
            ll initial_cost = 0; for (int k = 1; k <= K; ++k) initial_cost += rt[k].cost;
            cur_obj = rev_delta - initial_cost;
            save_best();
            cerr << "[SYSTEM] Phase 2 initialized with Greedy: obj=" << cur_obj << "\n";
        }

        // 4. Thermal Schedule Initialization
        double elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
        const double search_start = elapsed;
        const double budget = max(0.001, TIME_LIMIT_SEC - search_start);

        AdaptiveThermostat sedca;
        sedca.t_max = budget;
        vector<double> warmup_worsening;
        warmup_worsening.reserve(256);
        const int WARMUP_COUNT = 200;

        double temp = 0.15 * S_scale;
        int cur_cycle = 0, cycle_stag = 0;
        bool improved_in_cycle = false;
        long long accepted_count = 0, improved_count = 0;

        // 5. Main Search Loop
        for (long long it = 0;; ++it) {
            total_p2_iters = it;

            if (MAX_ITERS > 0) {
                if (it >= MAX_ITERS) break;
                if ((it & 15) == 0) {
                    double tau = (double)it / (double)MAX_ITERS;
                    if (sedca.calibrated) {
                        auto [new_temp, triggered_reheat] = sedca.get_temperature_tau(tau);
                        temp = new_temp;
                        if (triggered_reheat) {
                            if (improved_in_cycle) {
                                cycle_stag = 0;
                                load_best();
                            } else {
                                cycle_stag++;
                                load_best();
                            }
                            improved_in_cycle = false;
                        }
                        cur_cycle = sedca.cycle_count;
                    } else {
                        temp = 0.08 * S_scale;
                        cur_cycle = 0;
                    }
                }
            } else {
                if ((it & 15) == 0) {
                    elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
                    if (elapsed >= TIME_LIMIT_SEC) break;
                    double t = max(0.0, elapsed - search_start);

                    if (sedca.calibrated) {
                        auto [new_temp, triggered_reheat] = sedca.get_temperature(t);
                        temp = new_temp;
                        if (triggered_reheat) {
                            if (improved_in_cycle) {
                                cycle_stag = 0;
                                load_best();
                            } else {
                                cycle_stag++;
                                load_best();
                            }
                            improved_in_cycle = false;
                        }
                        cur_cycle = sedca.cycle_count;
                    } else {
                        temp = 0.08 * S_scale;
                        cur_cycle = 0;
                    }
                }
            }

            ++stamp_id; touched.clear(); veh_log.clear(); rev_delta = 0;

            int seed = select_seed();
            int r_mode = ruin_guided(seed);

            cand.clear();
            auto add = [&](int q) {
                if (servable[q] && !veh[q] && cand_stamp[q] != stamp_id) {
                    cand_stamp[q] = stamp_id; cand.push_back(q);
                }
            };
            if (priority_head != -1) add(priority_head);
            for (int q : removed) add(q);
            size_t nrem = cand.size();
            for (size_t t = 0; t < nrem; ++t) {
                const auto& nb = nbr[cand[t]];
                for (int i = 0, lim = min((int)nb.size(), CAND_NB); i < lim; ++i) add(nb[i]);
            }
            add(seed);
            for (int i = 0, lim = min((int)nbr[seed].size(), CAND_NB); i < lim; ++i) add(nbr[seed][i]);
            for (int i = 0; i < CAND_RAND; ++i) add(servable_list[rng() % servable_list.size()]);

            order_candidates(cand);
            recreate_with_memory(cand, true);

            ll dcost = 0;
            for (int k : touched) dcost += rt[k].cost - backup[k].cost;
            ll new_obj = cur_obj + rev_delta - dcost;
            ll delta = new_obj - cur_obj;

            if (delta >= 0 || (double)delta > temp * log(rand01() + 1e-300)) {
                cur_obj = new_obj;
                ++accepted_count;
                if (cur_obj > best_obj) {
                    save_best();
                    ++improved_count;
                    improved_in_cycle = true;
                }
            } else {
                for (int k : touched) { swap(rt[k], backup[k]); rver[k] = ++gver; }
                for (int i = (int)veh_log.size() - 1; i >= 0; --i) veh[veh_log[i].first] = veh_log[i].second;
            }

            if (!sedca.calibrated && delta < 0) {
                warmup_worsening.push_back((double)llabs(delta));
                if ((int)warmup_worsening.size() >= WARMUP_COUNT) {
                    sedca.auto_calibrate(warmup_worsening, S_scale);
                }
            }
        }
    }

    double total_wall_time = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();

    // ------------------------------------------------------------------------
    // FINAL INDEPENDENT VERIFICATION & STANDARD OUTPUT
    // ------------------------------------------------------------------------
    rt.assign(K + 1, Route());
    rver.assign(K + 1, 0);
    ll benefit = 0;
    for (int k = 1; k <= K; ++k) {
        rt[k].nd = best_nd[k];
        if (!recompute(k)) { rt[k].nd.assign(1, 0); recompute(k); }
        benefit -= rt[k].cost;
        for (int i = 1; i <= rt[k].n(); ++i) { int x = rt[k].nd[i]; if (x <= R) benefit += Rev[x]; }
    }

    fprintf(stderr, "RESULT: obj=%lld phase=%s iters=%lld speed=%.0f time=%.3f\n",
            benefit, (p1_proven_optimal ? "PHASE1_EXACT" : "PHASE2_GM_SISR"), 
            total_p2_iters, total_p2_iters / max(0.001, total_wall_time), total_wall_time);

    string out;
    out.reserve(1 << 16);
    out += to_string(benefit) + '\n';
    for (int k = 1; k <= K; ++k) {
        const auto& nd = rt[k].nd;
        int cnt = 0;
        for (int i = 1; i < (int)nd.size(); ++i) cnt += (nd[i] <= N) ? 2 : 1;
        out += to_string(cnt);
        for (int i = 1; i < (int)nd.size(); ++i) {
            int x = nd[i];
            if (x <= N) { out += ' ' + to_string(nin[x]) + ' ' + to_string(nout[x]); }
            else { out += ' ' + to_string(nin[x]); }
        }
        out += '\n';
    }
    fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
