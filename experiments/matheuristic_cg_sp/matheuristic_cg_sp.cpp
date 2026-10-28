#pragma GCC optimize("O3,unroll-loops")
#include <bits/stdc++.h>
using namespace std;
typedef long long ll;
namespace fs = std::filesystem;

// ============================================================================
// SYSTEM PARAMETERS & MATHEURISTIC SPECIFICATIONS
// ============================================================================
static double TIME_LIMIT_SEC = 2.0;       // Wall-clock search budget (seconds)
static const int NB_SIZE = 60;            // Spatio-temporal neighbor list size
static const double C_AVG = 10.0;         // SISR: average removed requests
static const int L_MAX = 10;              // SISR: maximum string length
static const int CAND_NB = 3;             // Unserved neighbors offered to recreate
static const int CAND_RAND = 6;           // Random unserved requests offered to recreate
static const unsigned BLINK_MASK = 63;    // Christiaens blink probability (1/64)
static const ll INFLL = (ll)4e18;

// Time Budget Fractions (SEDCA-V3 Multi-Phase)
static const double PHASE0_TIME_FRAC    = 0.25;  // Phase 0: GM-SISR Exploration
static const double PHASE1_1A_TIME_FRAC = 0.50;  // Phase 1.1A: Knowledge-guided Beam-DFS (lambda=0)
static const double PHASE1_1B_TIME_FRAC = 0.65;  // Phase 1.1B: Dual Pricing & Beam-DFS (lambda>0)
static const double PHASE1_2_TIME_FRAC  = 0.99;  // Phase 1.2: Set Packing (Lagrangian & SA)

// Column Generation Pool Limits
static const int MAX_POOL_CAP = 100000;
static const int WAVE1_CAP    = 15000;
static const int WAVE2_CAP    = 65000;
static const int WAVE3_CAP    = 100000;

// Diverse State Archive configuration for GM-SISR
static const int ARCHIVE_CAP = 4;
static int D_min_hamming = 4;

// ============================================================================
// FAST INPUT READER & ULTRA-FAST RNG (Xorshift64)
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
static inline int rand_int(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(rng() % (uint64_t)(hi - lo + 1));
}
static inline double rand01() { return (rng() >> 11) * (1.0 / 9007199254740992.0); }
template<typename T>
static inline void my_shuffle(vector<T>& v) {
    for (int i = (int)v.size() - 1; i > 0; --i) {
        int j = rand_int(0, i);
        swap(v[i], v[j]);
    }
}

// ============================================================================
// PROBLEM DATA & ACCELERATION STRUCTURES
// ============================================================================
static int K, N, M, V, R, SV, E;
static vector<int> O, Q, Rev, Tm, Cm;
static inline int T(int u, int v) { return Tm[u * SV + v]; }
static inline int C(int u, int v) { return Cm[u * SV + v]; }

static vector<int> nin, nout, nw, nic, servable_list;
static vector<ll> ne, nl, ndur;
static vector<char> servable;
static vector<vector<int>> nbr;
static double S_scale = 100.0, T_bar = 100.0, E_bar = 100.0;

// GM-SISR Solution Representation
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
static vector<vector<int>> best_nd_gm_sisr;
static ll LB_0 = 0;

// Piggybacked Memory & Diverse Archive for GM-SISR
static vector<double> ema_regret;
static vector<int> unserved_streak;
static int priority_head = -1;

struct ArchiveState {
    ll obj = -1;
    vector<vector<int>> nd;
    vector<int> veh_map;
    int used_count = 0;
};
static ArchiveState archive_pool[ARCHIVE_CAP];
static int archive_size = 0;

// Telemetry
static long long mode_tries[4] = {0}, mode_improve[4] = {0}, mode_records[4] = {0}, sort_records[3] = {0};
static int basin_jumps_total = 0, basin_jumps_records = 0;

// ============================================================================
// ROUTE EVALUATION & SLACK INSERTION
// ============================================================================
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

// ============================================================================
// COLUMN POOL & ZOBRIST HASHING (FLAT DATA STRUCTURES)
// ============================================================================
struct Column {
    int k;
    ll profit;
    double red_profit;
    uint64_t hash_sig;
    int rbeg, rlen;
    int nbeg, nlen;
};

static vector<Column> col_pool;
static vector<int> col_req;
static vector<int> col_nodes;
static unordered_map<uint64_t, int> hash_to_col_idx;
static vector<vector<int>> cols_of_veh;
static vector<uint64_t> Z_veh, Z_req;

static void init_zobrist() {
    Z_veh.assign(K + 1, 0);
    Z_req.assign(R + 1, 0);
    for (int k = 1; k <= K; ++k) Z_veh[k] = rng();
    for (int r = 1; r <= R; ++r) Z_req[r] = rng();
    col_pool.reserve(MAX_POOL_CAP);
    col_req.reserve(MAX_POOL_CAP * 10);
    col_nodes.reserve(MAX_POOL_CAP * 16);
    hash_to_col_idx.reserve(131072);
    cols_of_veh.assign(K + 1, {});
}

static void try_harvest_column(int k, ll profit, const int* nodes, int n_nodes, const int* reqs, int n_reqs) {
    if (profit <= 0 || n_reqs <= 0 || n_nodes <= 0) return;
    for (int i = 0; i < n_reqs; ++i) {
        if (reqs[i] <= 0 || reqs[i] > R) return;
        for (int j = i + 1; j < n_reqs; ++j) {
            if (reqs[i] == reqs[j]) return;
        }
    }
    uint64_t h = Z_veh[k];
    for (int i = 0; i < n_reqs; ++i) h ^= Z_req[reqs[i]];

    auto it = hash_to_col_idx.find(h);
    if (it != hash_to_col_idx.end()) {
        int idx = it->second;
        if (profit > col_pool[idx].profit) {
            col_pool[idx].profit = profit;
            col_pool[idx].red_profit = (double)profit;
            col_pool[idx].nbeg = (int)col_nodes.size();
            col_pool[idx].nlen = n_nodes;
            col_nodes.insert(col_nodes.end(), nodes, nodes + n_nodes);
            col_pool[idx].rbeg = (int)col_req.size();
            col_pool[idx].rlen = n_reqs;
            col_req.insert(col_req.end(), reqs, reqs + n_reqs);
        }
        return;
    }
    if ((int)col_pool.size() >= MAX_POOL_CAP) return;

    int new_idx = (int)col_pool.size();
    hash_to_col_idx[h] = new_idx;

    int rbeg = (int)col_req.size();
    col_req.insert(col_req.end(), reqs, reqs + n_reqs);

    int nbeg = (int)col_nodes.size();
    col_nodes.insert(col_nodes.end(), nodes, nodes + n_nodes);

    col_pool.push_back({k, profit, (double)profit, h, rbeg, n_reqs, nbeg, n_nodes});
    cols_of_veh[k].push_back(new_idx);
}

// Register exact whole route as column and return its column index
static int register_exact_route_column(int k, const vector<int>& nd) {
    int n = (int)nd.size() - 1;
    if (n <= 0) return -1;

    int prev = O[k];
    ll cost = 0, rev = 0;
    ll dep = 0;
    int cur_ld = 0;
    vector<int> reqs;
    int open_cnt = 0;

    for (int i = 1; i <= n; ++i) {
        int x = nd[i];
        ll arr = dep + T(prev, nin[x]);
        if (arr > nl[x]) return -1;
        dep = max(arr, ne[x]) + ndur[x];
        cur_ld += nw[x];
        if (cur_ld > Q[k]) return -1;
        cost += C(prev, nin[x]) + nic[x];
        prev = nout[x];

        if (x <= N) {
            rev += Rev[x];
            reqs.push_back(x);
        } else if (x <= R) {
            open_cnt++;
        } else {
            open_cnt--;
            int q = x - R;
            rev += Rev[q];
            reqs.push_back(q);
        }
    }
    if (open_cnt != 0) return -1;
    ll profit = rev - cost;
    if (profit <= 0 || reqs.empty()) return -1;

    for (size_t i = 0; i < reqs.size(); ++i) {
        if (reqs[i] <= 0 || reqs[i] > R) return -1;
        for (size_t j = i + 1; j < reqs.size(); ++j) {
            if (reqs[i] == reqs[j]) return -1;
        }
    }

    uint64_t h = Z_veh[k];
    for (int q : reqs) h ^= Z_req[q];

    auto it = hash_to_col_idx.find(h);
    if (it != hash_to_col_idx.end()) {
        int idx = it->second;
        if (profit > col_pool[idx].profit) {
            col_pool[idx].profit = profit;
            col_pool[idx].red_profit = (double)profit;
            col_pool[idx].nbeg = (int)col_nodes.size();
            col_pool[idx].nlen = n;
            col_nodes.insert(col_nodes.end(), nd.begin() + 1, nd.end());
            col_pool[idx].rbeg = (int)col_req.size();
            col_pool[idx].rlen = (int)reqs.size();
            col_req.insert(col_req.end(), reqs.begin(), reqs.end());
        }
        return idx;
    }
    if ((int)col_pool.size() >= MAX_POOL_CAP) return -1;

    int new_idx = (int)col_pool.size();
    hash_to_col_idx[h] = new_idx;

    int rbeg = (int)col_req.size();
    col_req.insert(col_req.end(), reqs.begin(), reqs.end());

    int nbeg = (int)col_nodes.size();
    col_nodes.insert(col_nodes.end(), nd.begin() + 1, nd.end());

    col_pool.push_back({k, profit, (double)profit, h, rbeg, (int)reqs.size(), nbeg, n});
    cols_of_veh[k].push_back(new_idx);
    return new_idx;
}

// Prefix harvesting on any valid route
static void harvest_from_route(int k, const vector<int>& nd) {
    int n = (int)nd.size() - 1;
    if (n <= 0) return;

    int open_cnt = 0;
    ll cur_cost = 0, cur_rev = 0;
    int prev = O[k];
    vector<int> req_prefix;
    req_prefix.reserve(n);

    for (int i = 1; i <= n; ++i) {
        int x = nd[i];
        cur_cost += C(prev, nin[x]) + nic[x];
        prev = nout[x];

        if (x <= N) {
            cur_rev += Rev[x];
            req_prefix.push_back(x);
        } else if (x <= R) {
            open_cnt++;
        } else {
            open_cnt--;
            int q = x - R;
            cur_rev += Rev[q];
            req_prefix.push_back(q);
        }

        if (open_cnt == 0) {
            ll profit = cur_rev - cur_cost;
            if (profit > 0) {
                try_harvest_column(k, profit, nd.data() + 1, i, req_prefix.data(), (int)req_prefix.size());
            }
        }
    }
}

// ============================================================================
// KNOWLEDGE ACCUMULATION & LAPLACE SMOOTHING
// ============================================================================
static vector<double> F_arc_data;
static vector<double> P_arc_data;
static vector<double> F_order_data;
static vector<double> P_order_data;
static double total_routes_sampled = 0.0;

static inline double& F_arc(int u, int v) { return F_arc_data[u * (2 * R + 1) + v]; }
static inline double P_arc_smooth(int u, int v) { return P_arc_data[u * (2 * R + 1) + v]; }
static inline double& F_order(int p, int q) { return F_order_data[p * (R + 1) + q]; }
static inline double P_order_smooth(int p, int q) { return P_order_data[p * (R + 1) + q]; }

static void init_knowledge_matrices() {
    E = 2 * R;
    F_arc_data.assign((size_t)(E + 1) * (E + 1), 0.0);
    P_arc_data.assign((size_t)(E + 1) * (E + 1), 0.0);
    F_order_data.assign((size_t)(R + 1) * (R + 1), 0.0);
    P_order_data.assign((size_t)(R + 1) * (R + 1), 0.0);
    total_routes_sampled = 0.0;
}

static void record_route_knowledge(int k, const vector<int>& nd, double w_sample) {
    int n = (int)nd.size() - 1;
    if (n <= 0) return;

    // Arc transitions
    F_arc(0, nd[1]) += w_sample;
    for (int i = 1; i < n; ++i) {
        F_arc(nd[i], nd[i + 1]) += w_sample;
    }

    // Macro order of requests
    vector<int> req_order;
    vector<int> first_pos(R + 1, -1);
    for (int i = 1; i <= n; ++i) {
        int x = nd[i];
        int q = (x <= R) ? x : (x - R);
        if (first_pos[q] == -1) {
            first_pos[q] = i;
            req_order.push_back(q);
        }
    }
    int m = (int)req_order.size();
    for (int i = 0; i < m; ++i) {
        int p = req_order[i];
        for (int j = i + 1; j < m; ++j) {
            int q = req_order[j];
            F_order(p, q) += w_sample;
        }
    }
    total_routes_sampled += w_sample;
}

static void compute_smoothed_knowledge() {
    const double eta = 0.30;
    // 1. Spatio-temporal transition probabilities
    for (int u = 0; u <= E; ++u) {
        int u_out = (u == 0) ? O[1] : nout[u];
        ll u_end = (u == 0) ? 0 : (ne[u] + ndur[u]);

        double sum_f = 0.0;
        for (int v = 1; v <= E; ++v) sum_f += F_arc(u, v);

        for (int v = 1; v <= E; ++v) {
            double d_norm = (double)T(u_out, nin[v]) / T_bar + (double)max(0LL, ne[v] - (u_end + T(u_out, nin[v]))) / E_bar;
            double p_geo = exp(-1.5 * d_norm);
            double p_empirical = (sum_f > 1e-9) ? (F_arc(u, v) / sum_f) : 0.0;
            P_arc_data[u * (E + 1) + v] = (1.0 - eta) * p_empirical + eta * p_geo;
        }
    }

    // 2. Macro order probabilities
    for (int p : servable_list) {
        int p_out = (p <= N) ? nout[p] : nout[R + p];
        ll p_end = (p <= N) ? (ne[p] + ndur[p]) : (ne[R + p] + ndur[R + p]);

        for (int q : servable_list) {
            if (p == q) continue;
            double d_norm = (double)T(p_out, nin[q]) / T_bar + (double)max(0LL, ne[q] - (p_end + T(p_out, nin[q]))) / E_bar;
            double p_geo = exp(-1.5 * d_norm);
            P_order_data[p * (R + 1) + q] = (F_order(p, q) + 0.25 * p_geo) / (total_routes_sampled + 0.25);
        }
    }
}

// ============================================================================
// ATOMIC TRACKING & GM-SISR HEURISTIC OPERATORS
// ============================================================================
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
        if (q <= 0 || q > R || veh[q]) continue;
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
            unserved_streak[q] = 0;
        } else {
            unserved_streak[q]++;
            if (priority_head == -1 && unserved_streak[q] >= 3) priority_head = q;
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

static int select_seed() {
    if ((rng() % 100) < 70) return servable_list[rng() % servable_list.size()];
    int a = servable_list[rng() % servable_list.size()];
    int b = servable_list[rng() % servable_list.size()];
    double sa = ema_regret[a] * (veh[a] == 0 ? 1.4 : 1.0);
    double sb = ema_regret[b] * (veh[b] == 0 ? 1.4 : 1.0);
    return (sa >= sb) ? a : b;
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
        int len = min(n, max(1, rand_int(1, max(1, min(n, (int)ls_max)))));
        int lo_st = max(1, pos - len + 1);
        int hi_st = min(pos, max(1, n - len + 1));
        int st = rand_int(lo_st, hi_st);
        tmp_reqs.clear();
        for (int i = st; i < min(n + 1, st + len); ++i) { int x = r.nd[i]; tmp_reqs.push_back(x > R ? x - R : x); }
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
    // Mode 1 (75% or Fallback): Spatio-Temporal String Ruin
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

static void save_best() {
    best_obj = cur_obj;
    for (int k = 1; k <= K; ++k) best_nd[k] = rt[k].nd;
}

static void load_best() {
    for (int k = 1; k <= K; ++k) {
        rt[k].nd = best_nd[k]; recompute(k);
        for (int i = 1; i <= rt[k].n(); ++i) { int x = rt[k].nd[i]; if (x <= R) veh[x] = k; }
    }
    cur_obj = best_obj;
}

static int hamming_distance(const vector<int>& vmap) {
    int dist = 0;
    for (int q : servable_list) if (veh[q] != vmap[q]) ++dist;
    return dist;
}

static void update_archive_at_cycle_end() {
    archive_pool[0].obj = best_obj;
    archive_pool[0].nd = best_nd;
    archive_pool[0].veh_map.assign(R + 1, 0);
    for (int k = 1; k <= K; ++k)
        for (int i = 1; i < (int)best_nd[k].size(); ++i)
            if (best_nd[k][i] <= R) archive_pool[0].veh_map[best_nd[k][i]] = k;
    if (archive_size == 0) archive_size = 1;

    if (cur_obj < best_obj - (ll)(1.5 * S_scale)) return;
    for (int p = 0; p < archive_size; ++p) {
        if (hamming_distance(archive_pool[p].veh_map) < D_min_hamming) {
            if (p > 0 && cur_obj > archive_pool[p].obj) {
                archive_pool[p].obj = cur_obj;
                for (int k = 1; k <= K; ++k) archive_pool[p].nd[k] = rt[k].nd;
                archive_pool[p].veh_map = veh;
            }
            return;
        }
    }

    int target_slot = -1;
    if (archive_size < ARCHIVE_CAP) target_slot = archive_size++;
    else {
        int worst_p = 1;
        for (int p = 2; p < ARCHIVE_CAP; ++p)
            if (archive_pool[p].obj < archive_pool[worst_p].obj) worst_p = p;
        if (cur_obj > archive_pool[worst_p].obj) target_slot = worst_p;
    }

    if (target_slot != -1) {
        archive_pool[target_slot].obj = cur_obj;
        archive_pool[target_slot].nd.resize(K + 1);
        for (int k = 1; k <= K; ++k) archive_pool[target_slot].nd[k] = rt[k].nd;
        archive_pool[target_slot].veh_map = veh;
        archive_pool[target_slot].used_count = 0;
    }
}

static void jump_basin_from_archive() {
    ++basin_jumps_total;
    int best_p = 0; double best_val = -1e18;
    for (int p = 0; p < archive_size; ++p) {
        double val = (double)archive_pool[p].obj - 0.15 * S_scale * archive_pool[p].used_count;
        if (val > best_val) { best_val = val; best_p = p; }
    }
    archive_pool[best_p].used_count++;

    fill(veh.begin(), veh.end(), 0);
    for (int k = 1; k <= K; ++k) {
        rt[k].nd = archive_pool[best_p].nd[k]; recompute(k);
        for (int i = 1; i <= rt[k].n(); ++i) { int x = rt[k].nd[i]; if (x <= R) veh[x] = k; }
    }
    cur_obj = archive_pool[best_p].obj;

    // Directed Perturbation Kick: evict bottleneck and force-insert best unserved
    ++stamp_id; touched.clear(); veh_log.clear(); rev_delta = 0;
    priority_head = -1; ll best_unserved_val = -1;
    for (int q : servable_list) {
        if (veh[q] == 0 && (Rev[q] - nic[q]) > best_unserved_val) {
            best_unserved_val = Rev[q] - nic[q];
            priority_head = q;
        }
    }
    if (priority_head != -1) {
        int done = 0;
        for (int c : nbr[priority_head]) {
            if (done >= 2) break;
            int k = veh[c];
            if (k != 0 && ruin_stamp[k] != stamp_id) {
                ruin_stamp[k] = stamp_id; tmp_reqs.assign(1, c);
                if (remove_from_route(k)) ++done;
            }
        }
        cand.clear();
        if (priority_head > 0) cand.push_back(priority_head);
        for (int q : removed) if (q > 0 && q <= R) cand.push_back(q);
        recreate_with_memory(cand, false);
        ll dcost = 0;
        for (int k : touched) dcost += rt[k].cost - backup[k].cost;
        cur_obj += rev_delta - dcost;
        if (cur_obj > best_obj) { save_best(); ++basin_jumps_records; }
    }
}

// ============================================================================
// SEDCA-V3 ADAPTIVE THERMOSTAT
// ============================================================================
struct AdaptiveThermostat {
    double t_max = 2.0;
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
        for (double d : worsening_deltas) {
            if (d > 0.0 && d <= 0.80 * scale_S) valid.push_back(d);
        }
        if (valid.size() < 20) valid = worsening_deltas;

        if (valid.empty()) {
            micro_delta = max(1.0, 0.04 * scale_S);
        } else {
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
        double max_peak = 0.65 * T_0;
        double min_peak = 1.50 * T_work;
        cycle_peak_T = min_peak * pow(max_peak / min_peak, 1.0 - osc_progress);
        cycle_work_T = T_work * pow(0.65, osc_progress);
        cycle_base_T = T_min  * pow(0.75, osc_progress);

        double base_span = tau_quench - tau_fast;
        double remaining_osc_tau = tau_quench - current_tau;
        double target_duration = base_span * (0.18 + 0.10 * osc_progress);
        cycle_duration_tau = min(target_duration, max(0.04, remaining_osc_tau));
    }

    pair<double, bool> get_temperature(double elapsed_time) {
        double tau = clamp(elapsed_time / max(0.001, t_max), 0.0, 1.0);
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
};

// ============================================================================
// KNOWLEDGE-GUIDED BEAM-DFS (PHASE 1.1 GENERATOR)
// ============================================================================
static vector<uint16_t> arc_usage_data;
static inline uint16_t& arc_usage(int k, int u, int v) {
    return arc_usage_data[((size_t)k * (E + 1) + u) * (E + 1) + v];
}

struct DFSContext {
    int k = 1;
    vector<char> vis_req;
    vector<int> open_parcels;
    vector<int> seq_nodes;
    vector<int> reqs_served;
    vector<double> acc_order;
    ll cur_rev = 0, cur_cost = 0;
    int dfs_states = 0;
    int max_states_budget = 800;
    int target_cap = WAVE2_CAP;

    struct CandBranch { int v; ll dep; int dc; double score; };

    void init(int num_reqs) {
        vis_req.assign(num_reqs + 1, 0);
        open_parcels.reserve(16);
        seq_nodes.reserve(32);
        reqs_served.reserve(32);
        acc_order.assign(num_reqs + 1, 0.0);
    }

    void dfs_expand(int u_last, ll t_dep, int w_cur, int depth, const vector<double>& dual_lambda) {
        // Emit column if route has zero parcel load and positive profit
        if (open_parcels.empty() && !reqs_served.empty()) {
            ll profit = cur_rev - cur_cost;
            if (profit > 0) {
                try_harvest_column(k, profit, seq_nodes.data(), (int)seq_nodes.size(), reqs_served.data(), (int)reqs_served.size());
            }
        }
        if (depth >= 16 || (int)col_pool.size() >= target_cap || ++dfs_states >= max_states_budget) return;

        // Collect candidate events
        vector<CandBranch> branches;
        branches.reserve(16);

        // 1. Drops of currently open parcels
        for (int p : open_parcels) {
            int v = R + p;
            int u_out = (u_last == 0) ? O[k] : nout[u_last];
            ll arr = t_dep + T(u_out, nin[v]);
            if (arr > nl[v]) continue;
            ll dep = max(arr, ne[v]) + ndur[v];

            // Lookahead check for other open parcels
            bool dead = false;
            for (int other : open_parcels) {
                if (other == p) continue;
                if (dep + T(nout[v], nin[R + other]) > nl[R + other]) { dead = true; break; }
            }
            if (dead) continue;

            int dc = C(u_out, nin[v]);
            double dr = 0.5 * (Rev[p] - dual_lambda[p]);
            double dt_norm = (double)max(1LL, dep - t_dep) / T_bar + 0.5;
            double red_rate = (dr - dc) / (S_scale * dt_norm);
            double p_arc = P_arc_smooth(u_last, v);
            double fatigue = 1.0 / (1.0 + 0.03 * arc_usage(k, u_last, v));
            double sc = fatigue * (red_rate + 1.2 * p_arc) + 0.05 * rand01();
            branches.push_back({v, dep, dc, sc});
        }

        // 2. New requests to pickup / onboard
        static int dfs_stamp_id = 0;
        static vector<int> dfs_cand_stamp;
        if (dfs_cand_stamp.size() <= (size_t)R) dfs_cand_stamp.assign(R + 1, 0);
        ++dfs_stamp_id;
        vector<int> cand_reqs;
        cand_reqs.reserve(48);
        auto add_c = [&](int q) {
            if (q > 0 && q <= R && !vis_req[q] && dfs_cand_stamp[q] != dfs_stamp_id) {
                dfs_cand_stamp[q] = dfs_stamp_id;
                cand_reqs.push_back(q);
            }
        };
        if (u_last == 0) {
            for (int q : servable_list) {
                add_c(q);
                if (cand_reqs.size() >= 40) break;
            }
        } else {
            int q_last = (u_last <= R) ? u_last : (u_last - R);
            for (int q : nbr[q_last]) add_c(q);
            for (int it = 0; it < 6; ++it) add_c(servable_list[rng() % servable_list.size()]);
        }

        for (int q : cand_reqs) {
            if (vis_req[q] || w_cur + nw[q] > Q[k]) continue;
            int v = q;
            int u_out = (u_last == 0) ? O[k] : nout[u_last];
            ll arr = t_dep + T(u_out, nin[v]);
            if (arr > nl[v]) continue;
            ll dep = max(arr, ne[v]) + ndur[v];

            // Lookahead check for open parcels
            bool dead = false;
            for (int p : open_parcels) {
                if (dep + T(nout[v], nin[R + p]) > nl[R + p]) { dead = true; break; }
            }
            if (dead) continue;

            int dc = C(u_out, nin[v]) + nic[v];
            double dr = (v <= N) ? (Rev[q] - dual_lambda[q]) : (0.5 * (Rev[q] - dual_lambda[q]));
            double dt_norm = (double)max(1LL, dep - t_dep) / T_bar + 0.5;
            double red_rate = (dr - dc) / (S_scale * dt_norm);
            double p_arc = P_arc_smooth(u_last, v);
            double p_ord = reqs_served.empty() ? 0.0 : (acc_order[q] / (double)reqs_served.size());
            double fatigue = 1.0 / (1.0 + 0.03 * arc_usage(k, u_last, v));
            double sc = fatigue * (red_rate + 1.2 * p_arc + 0.8 * p_ord) + 0.05 * rand01();
            branches.push_back({v, dep, dc, sc});
        }

        // Beam width pruning
        int beam_w = (depth <= 2) ? 6 : ((depth <= 6) ? 3 : 2);
        if ((int)branches.size() > beam_w) {
            partial_sort(branches.begin(), branches.begin() + beam_w, branches.end(),
                         [](const CandBranch& a, const CandBranch& b) { return a.score > b.score; });
            branches.resize(beam_w);
        }

        // Recursive expansion & backtracking
        for (const auto& br : branches) {
            int v = br.v;
            int q = (v <= R) ? v : (v - R);
            seq_nodes.push_back(v);
            cur_cost += br.dc;
            arc_usage(k, u_last, v)++;

            int erased_parcel_pos = -1;
            if (v <= R) {
                vis_req[q] = 1;
                reqs_served.push_back(q);
                cur_rev += Rev[q];
                for (int r : servable_list) acc_order[r] += P_order_smooth(q, r);
                if (v > N) open_parcels.push_back(q);
            } else {
                auto it = find(open_parcels.begin(), open_parcels.end(), q);
                erased_parcel_pos = (int)(it - open_parcels.begin());
                open_parcels.erase(it);
            }

            dfs_expand(v, br.dep, w_cur + nw[v], depth + 1, dual_lambda);

            // Backtrack
            if (v <= R) {
                if (v > N) open_parcels.pop_back();
                for (int r : servable_list) acc_order[r] -= P_order_smooth(q, r);
                cur_rev -= Rev[q];
                reqs_served.pop_back();
                vis_req[q] = 0;
            } else {
                open_parcels.insert(open_parcels.begin() + erased_parcel_pos, q);
            }
            cur_cost -= br.dc;
            seq_nodes.pop_back();
        }
    }

    void run(int veh_k, const vector<double>& dual_lambda, int budget_states, int target_cap_in) {
        k = veh_k;
        dfs_states = 0;
        max_states_budget = budget_states;
        target_cap = target_cap_in;
        open_parcels.clear();
        seq_nodes.clear();
        reqs_served.clear();
        cur_rev = cur_cost = 0;
        fill(vis_req.begin(), vis_req.end(), 0);
        fill(acc_order.begin(), acc_order.end(), 0.0);
        dfs_expand(0, 0, 0, 0, dual_lambda);
    }
};

// ============================================================================
// LAGRANGIAN SUBGRADIENT OPTIMIZATION & VARIABLE FIXING (PHASE 1.2)
// ============================================================================
struct LagrangianResult {
    double best_UB = 1e30;
    ll best_primal_obj = 0;
    vector<int> best_choice;
    vector<double> best_lambda;
};

static LagrangianResult solve_lagrangian(int max_iters, double init_theta, ll current_lb, const vector<int>& initial_choice, bool extract_primal) {
    LagrangianResult res;
    res.best_primal_obj = current_lb;
    res.best_choice = initial_choice;
    res.best_lambda.assign(R + 1, 0.0);

    vector<double> u_lambda(R + 1, 0.0);
    vector<double> subg(R + 1, 0.0);
    double theta = init_theta;
    int stall = 0;

    for (int it = 0; it < max_iters && !col_pool.empty(); ++it) {
        double L = 0.0;
        for (int q : servable_list) L += u_lambda[q];

        vector<int> relax_col(K + 1, -1);
        vector<double> relax_val(K + 1, 0.0);

        for (int k = 1; k <= K; ++k) {
            double max_red = 0.0;
            int best_j = -1;
            for (int j : cols_of_veh[k]) {
                double red = (double)col_pool[j].profit;
                const int* reqs = &col_req[col_pool[j].rbeg];
                int rlen = col_pool[j].rlen;
                for (int i = 0; i < rlen; ++i) red -= u_lambda[reqs[i]];
                if (red > max_red) {
                    max_red = red;
                    best_j = j;
                }
            }
            L += max_red;
            relax_col[k] = best_j;
            relax_val[k] = max_red;
        }

        if (it == 0) {
            cerr << "  [LAGRANGIAN it=0] Initial unconstrained bound L(0) = " << L << " | current_lb=" << current_lb << "\n";
        }
        if (L < res.best_UB - 1e-6) {
            res.best_UB = L;
            res.best_lambda = u_lambda;
            stall = 0;
        } else if (++stall >= 15) {
            theta *= 0.70;
            stall = 0;
        }

        // Primal Heuristic Extraction
        if (extract_primal) {
            vector<int> ord(K);
            iota(ord.begin(), ord.end(), 1);
            sort(ord.begin(), ord.end(), [&](int a, int b) { return relax_val[a] > relax_val[b]; });

            vector<int> primal_chosen(K + 1, -1);
            vector<char> req_used(R + 1, 0);
            ll primal_obj = 0;

            for (int k : ord) {
                int j = relax_col[k];
                bool ok = false;
                if (j >= 0) {
                    ok = true;
                    const int* reqs = &col_req[col_pool[j].rbeg];
                    int rlen = col_pool[j].rlen;
                    for (int i = 0; i < rlen; ++i) {
                        if (req_used[reqs[i]]) { ok = false; break; }
                    }
                }
                if (ok) {
                    primal_chosen[k] = j;
                    const int* reqs = &col_req[col_pool[j].rbeg];
                    int rlen = col_pool[j].rlen;
                    for (int i = 0; i < rlen; ++i) req_used[reqs[i]] = 1;
                    primal_obj += col_pool[j].profit;
                } else {
                    int best_c = -1;
                    ll best_prof = 0;
                    for (int c : cols_of_veh[k]) {
                        if (col_pool[c].profit <= best_prof) continue;
                        const int* reqs = &col_req[col_pool[c].rbeg];
                        int rlen = col_pool[c].rlen;
                        bool c_ok = true;
                        for (int i = 0; i < rlen; ++i) {
                            if (req_used[reqs[i]]) { c_ok = false; break; }
                        }
                        if (c_ok) {
                            best_prof = col_pool[c].profit;
                            best_c = c;
                        }
                    }
                    if (best_c >= 0) {
                        primal_chosen[k] = best_c;
                        const int* reqs = &col_req[col_pool[best_c].rbeg];
                        int rlen = col_pool[best_c].rlen;
                        for (int i = 0; i < rlen; ++i) req_used[reqs[i]] = 1;
                        primal_obj += col_pool[best_c].profit;
                    }
                }
            }
            if (primal_obj > res.best_primal_obj) {
                res.best_primal_obj = primal_obj;
                res.best_choice = primal_chosen;
            }
        }

        if (floor(res.best_UB + 1e-6) <= (double)res.best_primal_obj) break; // Proven optimal

        // Subgradient update
        fill(subg.begin(), subg.end(), 1.0);
        for (int k = 1; k <= K; ++k) {
            int j = relax_col[k];
            if (j >= 0) {
                const int* reqs = &col_req[col_pool[j].rbeg];
                int rlen = col_pool[j].rlen;
                for (int i = 0; i < rlen; ++i) subg[reqs[i]] -= 1.0;
            }
        }
        double norm_sq = 0.0;
        for (int q : servable_list) {
            if (u_lambda[q] <= 0.0 && subg[q] > 0.0) subg[q] = 0.0;
            norm_sq += subg[q] * subg[q];
        }
        if (norm_sq < 1e-9) break;

        double step = theta * max(0.0, L - (double)res.best_primal_obj) / norm_sq;
        for (int q : servable_list) {
            u_lambda[q] = max(0.0, u_lambda[q] - step * subg[q]);
        }
        if (theta < 1e-4) break;
    }
    return res;
}

// Lagrangian Variable Fixing
static void apply_variable_fixing(double gap, const vector<double>& best_lambda, vector<vector<int>>& filtered_cols, const vector<int>& baseline_choice) {
    filtered_cols.assign(K + 1, {});
    for (int k = 1; k <= K; ++k) {
        double max_red = -1e30;
        for (int j : cols_of_veh[k]) {
            double red = (double)col_pool[j].profit;
            const int* reqs = &col_req[col_pool[j].rbeg];
            int rlen = col_pool[j].rlen;
            for (int i = 0; i < rlen; ++i) red -= best_lambda[reqs[i]];
            col_pool[j].red_profit = red;
            if (red > max_red) max_red = red;
        }

        for (int j : cols_of_veh[k]) {
            if (max_red - col_pool[j].red_profit <= gap + 1e-6) {
                filtered_cols[k].push_back(j);
            }
        }
        if (baseline_choice[k] >= 0) {
            if (find(filtered_cols[k].begin(), filtered_cols[k].end(), baseline_choice[k]) == filtered_cols[k].end()) {
                filtered_cols[k].push_back(baseline_choice[k]);
            }
        }
        if (filtered_cols[k].empty() && !cols_of_veh[k].empty()) {
            filtered_cols[k] = cols_of_veh[k];
        }
        sort(filtered_cols[k].begin(), filtered_cols[k].end(), [](int a, int b) {
            return col_pool[a].profit > col_pool[b].profit;
        });
    }
}

// ============================================================================
// EJECTION-CHAIN SIMULATED ANNEALING (PHASE 1.2 CORE)
// ============================================================================
static void run_set_packing_sa(chrono::steady_clock::time_point t_start, ll ub,
                              const vector<vector<int>>& filtered_cols,
                              vector<int>& best_choice, ll& best_sp_obj,
                              long long& sa_iters) {
    vector<int> owner(R + 1, -1);
    vector<int> chosen(K + 1, -1);
    ll cur_sp_obj = 0;

    auto assign = [&](int k, int j) {
        chosen[k] = j;
        if (j < 0) return;
        const int* reqs = &col_req[col_pool[j].rbeg];
        int len = col_pool[j].rlen;
        for (int i = 0; i < len; ++i) owner[reqs[i]] = k;
        cur_sp_obj += col_pool[j].profit;
    };

    auto unassign = [&](int k) {
        int j = chosen[k];
        if (j < 0) return;
        const int* reqs = &col_req[col_pool[j].rbeg];
        int len = col_pool[j].rlen;
        for (int i = 0; i < len; ++i) owner[reqs[i]] = -1;
        cur_sp_obj -= col_pool[j].profit;
        chosen[k] = -1;
    };

    auto is_free = [&](int j) -> bool {
        const int* reqs = &col_req[col_pool[j].rbeg];
        int len = col_pool[j].rlen;
        for (int i = 0; i < len; ++i) {
            if (owner[reqs[i]] >= 0) return false;
        }
        return true;
    };

    auto verify_integrity = [&]() -> bool {
        vector<int> chk_owner(R + 1, -1);
        ll chk_obj = 0;
        for (int k = 1; k <= K; ++k) {
            int j = chosen[k];
            if (j >= 0) {
                chk_obj += col_pool[j].profit;
                const int* reqs = &col_req[col_pool[j].rbeg];
                int len = col_pool[j].rlen;
                for (int i = 0; i < len; ++i) {
                    int q = reqs[i];
                    if (q <= 0 || q > R || chk_owner[q] >= 0) return false;
                    chk_owner[q] = k;
                }
            }
        }
        for (int q = 1; q <= R; ++q) {
            if (owner[q] != chk_owner[q]) return false;
        }
        if (chk_obj != cur_sp_obj) return false;
        return true;
    };

    // Load initial primal solution
    for (int k = 1; k <= K; ++k) {
        if (best_choice[k] >= 0) assign(k, best_choice[k]);
    }
    if (!verify_integrity()) {
        cerr << "WARNING: Initial SP choice had conflicts! Resetting chosen.\n";
        for (int k = 1; k <= K; ++k) unassign(k);
    }
    best_sp_obj = cur_sp_obj;

    // Active vehicles with available columns
    vector<int> busy;
    for (int k = 1; k <= K; ++k) if (!filtered_cols[k].empty()) busy.push_back(k);
    if (busy.empty()) return;

    double elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
    double s0 = elapsed, budget = max(0.001, TIME_LIMIT_SEC * PHASE1_2_TIME_FRAC - s0);

    AdaptiveThermostat sa_sedca;
    sa_sedca.t_max = budget;
    sa_sedca.T_0 = 0.35 * S_scale;
    sa_sedca.T_work = 0.06 * S_scale;
    sa_sedca.T_min = 0.005 * S_scale;
    sa_sedca.calibrated = true;

    pair<int, int> log_undo[8];
    int conflict_k[4];
    double temp = sa_sedca.T_0;

    for (long long it = 0;; ++it) {
        sa_iters = it;
        if ((it & 255) == 0) {
            elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
            if (elapsed >= TIME_LIMIT_SEC * PHASE1_2_TIME_FRAC) break;
            temp = sa_sedca.get_temperature(elapsed - s0).first;

            // Cold-bottom greedy fill pass
            if (temp <= sa_sedca.T_work) {
                for (int k = 1; k <= K; ++k) {
                    if (chosen[k] < 0) {
                        for (int j : filtered_cols[k]) {
                            if (is_free(j)) { assign(k, j); break; }
                        }
                    }
                }
                if (cur_sp_obj > best_sp_obj) {
                    if (verify_integrity()) {
                        best_sp_obj = cur_sp_obj;
                        best_choice = chosen;
                        if (best_sp_obj >= ub) break;
                    }
                }
            }
        }

        int k1 = busy[rng() % busy.size()];
        const auto& tc = filtered_cols[k1];
        int j_new = -1;
        uint32_t rpick = (uint32_t)(rng() % 100);
        if (rpick < 50) {
            j_new = tc[rng() % tc.size()];
        } else if (rpick < 95) {
            j_new = tc[rng() % min<size_t>(tc.size(), 16)];
        } else {
            j_new = -1; // Empty taxi
        }

        if (j_new == chosen[k1]) continue;

        // Detect conflicting vehicles
        int num_conflicts = 0;
        bool rejected = false;
        if (j_new >= 0) {
            const int* reqs = &col_req[col_pool[j_new].rbeg];
            int len = col_pool[j_new].rlen;
            for (int i = 0; i < len; ++i) {
                int q = reqs[i];
                if (q <= 0 || q > R) continue;
                int vo = owner[q];
                if (vo >= 0 && vo != k1) {
                    bool seen = false;
                    for (int c = 0; c < num_conflicts; ++c) {
                        if (conflict_k[c] == vo) { seen = true; break; }
                    }
                    if (!seen) {
                        if (num_conflicts >= 2) { rejected = true; break; }
                        conflict_k[num_conflicts++] = vo;
                    }
                }
            }
        }
        if (rejected) continue;

        // Undo logging
        ll before_obj = cur_sp_obj;
        int log_sz = 0;
        log_undo[log_sz++] = {k1, chosen[k1]};
        for (int c = 0; c < num_conflicts; ++c) {
            log_undo[log_sz++] = {conflict_k[c], chosen[conflict_k[c]]};
        }

        // Apply unassignments
        unassign(k1);
        for (int c = 0; c < num_conflicts; ++c) unassign(conflict_k[c]);

        // Assign new column to k1
        if (j_new >= 0) assign(k1, j_new);

        // Instant repair for displaced vehicles
        for (int c = 0; c < num_conflicts; ++c) {
            int k2 = conflict_k[c];
            int best_rep = -1;
            const auto& tcols = filtered_cols[k2];
            for (int cand_col : tcols) {
                if (is_free(cand_col)) {
                    if (rng() & 31) {
                        best_rep = cand_col;
                        break;
                    } else if (best_rep == -1) {
                        best_rep = cand_col;
                    } else {
                        best_rep = cand_col;
                        break;
                    }
                }
            }
            if (best_rep >= 0) assign(k2, best_rep);
        }

        ll delta = cur_sp_obj - before_obj;
        if (delta >= 0 || (double)delta > temp * log(rand01() + 1e-300)) {
            if (cur_sp_obj > best_sp_obj) {
                if (verify_integrity()) {
                    best_sp_obj = cur_sp_obj;
                    best_choice = chosen;
                    if (best_sp_obj >= ub) break;
                }
            }
        } else {
            // Revert changes cleanly
            // 1. Unassign all vehicles that received new/repaired columns in this step
            if (chosen[k1] >= 0) unassign(k1);
            for (int c = 0; c < num_conflicts; ++c) {
                int k2 = conflict_k[c];
                if (chosen[k2] >= 0) unassign(k2);
            }
            // 2. Re-assign all original columns
            for (int i = 0; i < log_sz; ++i) {
                int vk = log_undo[i].first;
                int old_col = log_undo[i].second;
                if (old_col >= 0) assign(vk, old_col);
            }
            if (cur_sp_obj != before_obj) {
                cerr << "FATAL: Objective mismatch after SA undo! cur=" << cur_sp_obj << " before=" << before_obj << "\n";
                exit(1);
            }
        }
    }
}

// ============================================================================
// MAIN EXECUTION ENGINE
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

    read_all();
    K = rd(); N = rd(); M = rd();
    R = N + M; V = 2 * N + 2 * M + K; SV = V + 1; E = 2 * R;

    O.assign(K + 1, 0); Q.assign(K + 1, 0); Rev.assign(R + 1, 0);
    for (int k = 1; k <= K; ++k) { O[k] = rd(); Q[k] = rd(); }

    nin.assign(2 * R + 1, 0); nout = nw = nic = nin;
    ne.assign(2 * R + 1, 0); nl = ndur = ne;
    servable.assign(R + 1, 1);
    servable[0] = 0;

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

    // 1. Initialize Energy Scale & Neighbor Lists
    double sum_net = 0.0;
    ema_regret.assign(R + 1, 0.0);
    unserved_streak.assign(R + 1, 0);
    for (int q : servable_list) {
        double net = max(1.0, (double)(Rev[q] - nic[q]));
        ema_regret[q] = net;
        sum_net += net;
    }
    S_scale = max(1.0, sum_net / servable_list.size());
    D_min_hamming = max(4, (int)(0.08 * servable_list.size()));

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
    T_bar = (pairs > 0) ? max(1.0, sum_T / pairs) : 1.0;
    E_bar = (pairs > 0) ? max(1.0, sum_E / pairs) : 1.0;

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

    rver.assign(K + 1, 0); icache.assign((size_t)(R + 1) * (K + 1), CacheEntry());
    rt.assign(K + 1, Route()); backup.assign(K + 1, Route());
    best_nd.assign(K + 1, vector<int>(1, 0)); best_nd_gm_sisr.assign(K + 1, vector<int>(1, 0));
    for (int k = 1; k <= K; ++k) { rt[k].nd.assign(1, 0); recompute(k); }
    veh.assign(R + 1, 0);
    touch_stamp.assign(K + 1, 0); ruin_stamp.assign(max(K, R) + 1, 0); cand_stamp.assign(R + 1, 0);

    init_zobrist();
    init_knowledge_matrices();
    arc_usage_data.assign(((size_t)(K + 1) * (E + 1) * (E + 1)), 0);

    // Setup Output Session
    string out_dir = "", latest_dir = "";
    ofstream flog, freport;
    bool write_files = !test_filepath.empty();
    if (write_files) {
        string tname = fs::path(test_filepath).stem().string();
        time_t now_c = chrono::system_clock::to_time_t(chrono::system_clock::now());
        char tbuf[64];
        strftime(tbuf, sizeof(tbuf), "%Y%m%d_%H%M%S", localtime(&now_c));
        string folder_name = "run_" + tname + "_" + string(tbuf) + (run_seed >= 0 ? "_s" + to_string(run_seed) : "") + "_cg_sp";

        fs::path base_out = fs::path("experiments") / "matheuristic_cg_sp" / "output";
        out_dir = (base_out / "runs" / folder_name).string();
        latest_dir = (base_out / "latest" / tname).string();
        fs::create_directories(out_dir);
        fs::create_directories(latest_dir);

        flog.open(out_dir + "/log.txt");
        freport.open(out_dir + "/report.txt");
        cerr << "[MATHEURISTIC] Initialized run folder: " << out_dir << "\n";
    }

    // ========================================================================
    // PHASE 0: GM-SISR EXPLORATION (0.00 -> 0.25 Time Budget)
    // ========================================================================
    cerr << "[PHASE 0: START] GM-SISR Thermal Exploration...\n";
    vector<int> init = servable_list;
    sort(init.begin(), init.end(), [&](int a, int b) { return Rev[a] - nic[a] > Rev[b] - nic[b]; });
    ++stamp_id; rev_delta = 0;
    recreate_with_memory(init, false);
    ll initial_cost = 0; for (int k = 1; k <= K; ++k) initial_cost += rt[k].cost;
    cur_obj = rev_delta - initial_cost;
    save_best();
    ll initial_greedy_obj = cur_obj;
    update_archive_at_cycle_end();

    for (int k = 1; k <= K; ++k) harvest_from_route(k, rt[k].nd);

    double elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
    const double search_start = elapsed;
    const double p0_budget = max(0.001, TIME_LIMIT_SEC * PHASE0_TIME_FRAC - search_start);

    AdaptiveThermostat sedca;
    sedca.t_max = p0_budget;
    vector<double> warmup_worsening;
    warmup_worsening.reserve(256);
    const int WARMUP_COUNT = 200;

    double temp = 0.15 * S_scale;
    int cur_cycle = 0, cycle_stag = 0;
    bool improved_in_cycle = false;
    long long p0_iters = 0;

    for (long long it = 0;; ++it) {
        p0_iters = it;
        if ((it & 15) == 0) {
            elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
            if (elapsed >= TIME_LIMIT_SEC * PHASE0_TIME_FRAC) break;
            double t = max(0.0, elapsed - search_start);

            if (sedca.calibrated) {
                auto [new_temp, triggered_reheat] = sedca.get_temperature(t);
                temp = new_temp;

                if (triggered_reheat) {
                    update_archive_at_cycle_end();
                    for (int k = 1; k <= K; ++k) {
                        harvest_from_route(k, rt[k].nd);
                        record_route_knowledge(k, rt[k].nd, 1.0);
                    }
                    if (improved_in_cycle) {
                        cycle_stag = 0;
                        if (cur_obj < best_obj - (ll)(2.0 * sedca.micro_delta)) load_best();
                    } else {
                        cycle_stag++;
                        if (cycle_stag >= 2 && archive_size > 1) {
                            jump_basin_from_archive();
                            cycle_stag = 0;
                        } else {
                            load_best();
                        }
                    }
                    improved_in_cycle = false;
                }
                cur_cycle = sedca.cycle_count;
            } else {
                temp = 0.08 * S_scale;
                cur_cycle = 0;
            }
        }

        ++stamp_id; touched.clear(); veh_log.clear(); rev_delta = 0;
        int seed = select_seed();
        int r_mode = ruin_guided(seed);
        mode_tries[r_mode]++;

        cand.clear();
        auto add = [&](int q) {
            if (q > 0 && q <= R && servable[q] && !veh[q] && cand_stamp[q] != stamp_id) {
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

        int sort_mode = order_candidates(cand);

        ll dcost = 0;
        for (int k : touched) dcost += rt[k].cost - backup[k].cost;
        ll new_obj = cur_obj + rev_delta - dcost;
        ll delta = new_obj - cur_obj;

        if (delta >= 0 || (double)delta > temp * log(rand01() + 1e-300)) {
            cur_obj = new_obj;
            if (cur_obj > best_obj) {
                save_best();
                improved_in_cycle = true;
                mode_records[r_mode]++;
                sort_records[sort_mode]++;
                for (int k : touched) {
                    harvest_from_route(k, rt[k].nd);
                    record_route_knowledge(k, rt[k].nd, 3.0);
                }
            } else if (delta > 0) {
                mode_improve[r_mode]++;
                for (int k : touched) {
                    harvest_from_route(k, rt[k].nd);
                    record_route_knowledge(k, rt[k].nd, 1.0);
                }
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

    // Preserve Phase 0 record & solution
    load_best();
    LB_0 = best_obj;
    best_nd_gm_sisr = best_nd;
    vector<int> gm_sisr_col_choice(K + 1, -1);
    ll registered_gm_profit = 0;
    for (int k = 1; k <= K; ++k) {
        harvest_from_route(k, best_nd_gm_sisr[k]);
        int col_id = register_exact_route_column(k, best_nd_gm_sisr[k]);
        gm_sisr_col_choice[k] = col_id;
        cerr << "  k=" << k << " col_id=" << col_id;
        if (col_id >= 0) {
            registered_gm_profit += col_pool[col_id].profit;
            cerr << " profit=" << col_pool[col_id].profit;
        }
        cerr << "\n";
    }
    if (registered_gm_profit > 0) LB_0 = registered_gm_profit;

    compute_smoothed_knowledge();
    for (int k = 1; k <= K; ++k) {
        cerr << "  k=" << k << " best_nd_gm_sisr: ";
        for (int x : best_nd_gm_sisr[k]) cerr << x << " ";
        cerr << "\n";
    }
    int wave1_cols = (int)col_pool.size();
    double p0_time = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
    cerr << "[PHASE 0: COMPLETE] GM-SISR Best LB_0=" << LB_0 
         << " | Wave 1 cols=" << wave1_cols 
         << " | iters=" << p0_iters 
         << " | time=" << fixed << setprecision(3) << p0_time << "s\n";

    // ========================================================================
    // PHASE 1.1A: BEAM-DFS GENERATOR (lambda=0, 0.25 -> 0.50 Time Budget)
    // ========================================================================
    cerr << "[PHASE 1.1A: START] Knowledge-guided Beam-DFS (Wave 2, Target " << WAVE2_CAP << ")...\n";
    DFSContext dfs_ctx;
    dfs_ctx.init(R);
    vector<double> dual_lambda(R + 1, 0.0);

    int veh_idx = 1;
    long long dfs_trees_1a = 0;
    while ((int)col_pool.size() < WAVE2_CAP) {
        elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
        if (elapsed >= TIME_LIMIT_SEC * PHASE1_1A_TIME_FRAC) break;
        dfs_ctx.run(veh_idx, dual_lambda, 800, WAVE2_CAP);
        dfs_trees_1a++;
        veh_idx = (veh_idx % K) + 1;
    }

    int wave2_cols = (int)col_pool.size();
    double p1_1a_time = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
    cerr << "[PHASE 1.1A: COMPLETE] Wave 2 cols=" << wave2_cols 
         << " (+" << (wave2_cols - wave1_cols) << ") | trees=" << dfs_trees_1a 
         << " | time=" << fixed << setprecision(3) << p1_1a_time << "s\n";

    // ========================================================================
    // PHASE 1.1B: DUAL PRICING & BEAM-DFS (lambda>0, 0.50 -> 0.65 Time Budget)
    // ========================================================================
    cerr << "[PHASE 1.1B: START] 150-Step Lagrangian Dual Pricing...\n";
    LagrangianResult lag_pre = solve_lagrangian(150, 1.5, LB_0, gm_sisr_col_choice, false);
    dual_lambda = lag_pre.best_lambda;
    cerr << "[PHASE 1.1B] Dual shadow prices extracted. Running Beam-DFS (Wave 3, Target " << WAVE3_CAP << ")...\n";

    long long dfs_trees_1b = 0;
    while ((int)col_pool.size() < WAVE3_CAP) {
        elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
        if (elapsed >= TIME_LIMIT_SEC * PHASE1_1B_TIME_FRAC) break;
        dfs_ctx.run(veh_idx, dual_lambda, 800, WAVE3_CAP);
        dfs_trees_1b++;
        veh_idx = (veh_idx % K) + 1;
    }

    int wave3_cols = (int)col_pool.size();
    double p1_1b_time = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
    cerr << "[PHASE 1.1B: COMPLETE] Wave 3 Total Unique Cols=" << wave3_cols 
         << " (+" << (wave3_cols - wave2_cols) << ") | trees=" << dfs_trees_1b 
         << " | time=" << fixed << setprecision(3) << p1_1b_time << "s\n";

    // ========================================================================
    // PHASE 1.2: SET PACKING (LAGRANGIAN & EJECTION-CHAIN SA, 0.65 -> 0.99)
    // ========================================================================
    cerr << "[PHASE 1.2: START] 400-Step Full Lagrangian Subgradient with Primal Recovery...\n";
    LagrangianResult lag_full = solve_lagrangian(400, 1.8, LB_0, gm_sisr_col_choice, true);
    ll final_UB = (ll)floor(lag_full.best_UB + 1e-6);
    ll primal_LB = max(LB_0, lag_full.best_primal_obj);
    vector<int> sp_best_choice = lag_full.best_choice;
    double gap = max(0.0, (double)final_UB - (double)primal_LB);

    cerr << "[PHASE 1.2: LAGRANGIAN] UB=" << final_UB 
         << " | LB=" << primal_LB 
         << " | Gap=" << fixed << setprecision(2) << gap << "\n";

    // Lagrangian Variable Fixing
    vector<vector<int>> filtered_cols;
    apply_variable_fixing(gap, lag_full.best_lambda, filtered_cols, gm_sisr_col_choice);

    int total_filtered = 0;
    for (int k = 1; k <= K; ++k) total_filtered += (int)filtered_cols[k].size();
    double prune_pct = (wave3_cols > 0) ? (100.0 * (wave3_cols - total_filtered) / wave3_cols) : 0.0;
    cerr << "[PHASE 1.2: VARIABLE FIXING] Retained " << total_filtered 
         << " / " << wave3_cols << " elite cols (Pruned " 
         << fixed << setprecision(1) << prune_pct << "%)\n";

    // Ejection-Chain Simulated Annealing
    ll final_sp_obj = primal_LB;
    long long sp_sa_iters = 0;
    if (final_sp_obj < final_UB && total_filtered > 0) {
        cerr << "[PHASE 1.2: SA] Running Ejection-Chain SA...\n";
        run_set_packing_sa(t_start, final_UB, filtered_cols, sp_best_choice, final_sp_obj, sp_sa_iters);
    }

    double p1_2_time = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
    cerr << "[PHASE 1.2: COMPLETE] Final SP Obj=" << final_sp_obj 
         << " | UB=" << final_UB 
         << " | SA Iters=" << sp_sa_iters 
         << " | Proven Optimal=" << (final_sp_obj >= final_UB ? "YES" : "NO") 
         << " | time=" << fixed << setprecision(3) << p1_2_time << "s\n";

    // ========================================================================
    // FINAL SOLUTION RECOVERY & NON-REGRESSION GUARANTEE
    // ========================================================================
    bool used_sp = false;
    if (final_sp_obj >= LB_0) {
        used_sp = true;
        for (int k = 1; k <= K; ++k) {
            best_nd[k].assign(1, 0);
            int j = sp_best_choice[k];
            if (j >= 0) {
                best_nd[k].insert(best_nd[k].end(),
                                  col_nodes.begin() + col_pool[j].nbeg,
                                  col_nodes.begin() + col_pool[j].nbeg + col_pool[j].nlen);
            }
        }
    } else {
        cerr << "[MATHEURISTIC: SAFE FALLBACK] Falling back to Phase 0 GM-SISR record LB_0=" << LB_0 << "\n";
        best_nd = best_nd_gm_sisr;
    }

    // Feasibility verification on final solution
    ll benefit = 0;
    for (int k = 1; k <= K; ++k) {
        rt[k].nd = best_nd[k];
        if (!recompute(k)) {
            cerr << "WARNING: Vehicle " << k << " failed recompute! Resetting.\n";
            rt[k].nd.assign(1, 0); recompute(k);
        }
        benefit -= rt[k].cost;
        for (int i = 1; i <= rt[k].n(); ++i) {
            int x = rt[k].nd[i];
            if (x <= R) benefit += Rev[x];
        }
    }

    if (benefit < LB_0) {
        cerr << "[MATHEURISTIC: SAFE FALLBACK] Benefit " << benefit << " < LB_0 " << LB_0 << ". Falling back to Phase 0 GM-SISR!\n";
        best_nd = best_nd_gm_sisr;
        benefit = 0;
        for (int k = 1; k <= K; ++k) {
            rt[k].nd = best_nd[k];
            recompute(k);
            benefit -= rt[k].cost;
            for (int i = 1; i <= rt[k].n(); ++i) {
                int x = rt[k].nd[i];
                if (x <= R) benefit += Rev[x];
            }
        }
        used_sp = false;
    }

    // Strict Set Packing verification on reconstructed routes
    vector<int> req_claimed(R + 1, 0);
    bool has_duplicate_request = false;
    for (int k = 1; k <= K; ++k) {
        for (int i = 1; i <= rt[k].n(); ++i) {
            int x = rt[k].nd[i];
            if (x <= R) {
                int q = x; // passenger or parcel pickup
                if (req_claimed[q] != 0 && req_claimed[q] != k) {
                    cerr << "CRITICAL ERROR: Request " << q << " is served by vehicle " 
                         << req_claimed[q] << " and vehicle " << k << "!\n";
                    has_duplicate_request = true;
                    break;
                }
                req_claimed[q] = k;
            }
        }
        if (has_duplicate_request) break;
    }

    if (has_duplicate_request) {
        cerr << "[MATHEURISTIC: SAFE FALLBACK] Duplicate requests detected! Falling back to Phase 0 GM-SISR LB_0=" << LB_0 << "\n";
        best_nd = best_nd_gm_sisr;
        benefit = 0;
        for (int k = 1; k <= K; ++k) {
            rt[k].nd = best_nd[k];
            recompute(k);
            benefit -= rt[k].cost;
            for (int i = 1; i <= rt[k].n(); ++i) {
                int x = rt[k].nd[i];
                if (x <= R) benefit += Rev[x];
            }
        }
        used_sp = false;
    }

    cerr << "DEBUG: Final best_nd[1] contents before print: ";
    for (int x : best_nd[1]) cerr << x << " ";
    cerr << "\n";

    // Format output string
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
            if (x <= N) out += ' ' + to_string(nin[x]) + ' ' + to_string(nout[x]);
            else out += ' ' + to_string(nin[x]);
        }
        out += '\n';
    }

    double total_wall_time = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();

    if (write_files && !out_dir.empty()) {
        ofstream fres(out_dir + "/result.txt");
        fres << out;
        fres.close();

        if (freport.is_open()) {
            freport << "======================================================================\n"
                    << "   TWO-PHASE MATHEURISTIC: MULTI-WAVE CG & LAGRANGIAN SET PACKING\n"
                    << "======================================================================\n\n"
                    << "  * Instance             : " << test_filepath << "\n"
                    << "  * Problem Dimensions   : K=" << K << ", N=" << N << ", M=" << M << ", V=" << V << "\n"
                    << "  * Wall-clock Budget    : " << TIME_LIMIT_SEC << "s (Elapsed: " << fixed << setprecision(4) << total_wall_time << "s)\n"
                    << "  * Random Seed          : " << run_seed << "\n"
                    << "  * Phase 0 Greedy Obj   : " << initial_greedy_obj << "\n"
                    << "  * Phase 0 GM-SISR Best : " << LB_0 << "\n"
                    << "  * Final Best Objective : " << benefit << " (Source: " << (used_sp ? "Set Packing" : "GM-SISR Fallback") << ")\n"
                    << "  * Lagrangian Upper Bnd : " << final_UB << "\n"
                    << "  * Optimality Proven    : " << (benefit >= final_UB ? "YES" : "NO") << "\n"
                    << "  * Net Improvement      : +" << (benefit - initial_greedy_obj) << " (vs GM-SISR: +" << (benefit - LB_0) << ")\n\n"
                    << "--- COLUMN GENERATION TELEMETRY ---\n"
                    << "  * Wave 1 (Phase 0)     : " << wave1_cols << " cols\n"
                    << "  * Wave 2 (Beam-DFS)    : " << (wave2_cols - wave1_cols) << " cols\n"
                    << "  * Wave 3 (Dual Pricing): " << (wave3_cols - wave2_cols) << " cols\n"
                    << "  * Total Unique Columns : " << wave3_cols << "\n"
                    << "  * Lagrangian Pruned    : " << (wave3_cols - total_filtered) << " (" << fixed << setprecision(1) << prune_pct << "%)\n"
                    << "  * Elite SP Columns     : " << total_filtered << "\n\n"
                    << "--- SET PACKING THROUGHPUT ---\n"
                    << "  * SA Moves Tested      : " << sp_sa_iters << " (" << fixed << setprecision(0) << (sp_sa_iters / max(0.001, p1_2_time - p1_1b_time)) << " moves/s)\n"
                    << "  * Final Optimality Gap : " << (final_UB - benefit) << "\n"
                    << "======================================================================\n";
            freport.close();
        }
        if (!latest_dir.empty()) {
            try {
                fs::copy_file(out_dir + "/result.txt", latest_dir + "/result.txt", fs::copy_options::overwrite_existing);
                if (fs::exists(out_dir + "/report.txt")) fs::copy_file(out_dir + "/report.txt", latest_dir + "/report.txt", fs::copy_options::overwrite_existing);
            } catch (...) {}
        }
    }

    fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
