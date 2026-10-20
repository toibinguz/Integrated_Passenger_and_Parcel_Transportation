
#pragma GCC optimize("O3,unroll-loops")
#include <bits/stdc++.h>
using namespace std;
typedef long long ll;

// ============================================================================
// PARAMETERS
// ============================================================================
static double TIME_LIMIT_SEC = 1.0;      // wall-clock budget, input reading included (argv[1] overrides)
static const int NB_SIZE = 60;           // neighbour list length per request
static const double C_AVG = 10.0;        // SISR: average number of removed requests
static const int L_MAX = 10;             // SISR: max string length
static const int CAND_NB = 3;            // unserved neighbours of a removed request offered to recreate
static const int CAND_RAND = 6;          // random unserved requests offered to recreate
static const unsigned BLINK_MASK = 63;   // insertion position skipped with prob 1/64
static const double T0_RATIO = 1.0;      // SA start temperature relative to mean revenue
static const double TF_RATIO = 0.005;    // SA final temperature relative to mean revenue
static const int RUIN_RANDOM_PCT = 10;   // % of iterations using random removal
static const int RUIN_ROUTE_PCT = 5;     // % of iterations dismantling a whole route
static const int CYCLE_SCALE = 600;      
static const int RESTART_FROM_BEST = 1;  // each new SA cycle restarts from the best solution
static const ll INFLL = (ll)4e18;

// ============================================================================
// FAST INPUT (skips '#' comment lines)
// ============================================================================
static char* ibuf; static size_t ilen, ipos;
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
// RNG
// ============================================================================
static uint64_t rng_s = 0x9E3779B97F4A7C15ull;
static inline uint64_t rng() { rng_s ^= rng_s << 7; rng_s ^= rng_s >> 9; return rng_s; }
static inline int rand_int(int lo, int hi) { return lo + (int)(rng() % (uint64_t)(hi - lo + 1)); } // [lo,hi]
static inline double rand01() { return (rng() >> 11) * (1.0 / 9007199254740992.0); }

// ============================================================================
// PROBLEM DATA
// ============================================================================
static int K, N, M, V, R, SV;
static vector<int> O, Q, Rev;
static vector<int> Tm, Cm;                // flat (V+1)x(V+1)
static inline int T(int u, int v) { return Tm[u * SV + v]; }
static inline int C(int u, int v) { return Cm[u * SV + v]; }

// Route nodes: passenger r -> node r (macro node P->D); parcel r -> pickup node r, drop node R+r
static vector<int> nin, nout, nw, nic;    // in vertex, out vertex, load delta, internal cost
static vector<ll> ne, nl, ndur;           // earliest start, latest arrival, total node duration
static vector<char> servable;
static vector<vector<int>> nbr;

// ============================================================================
// SOLUTION STATE
// ============================================================================
struct Route {
    vector<int> nd;   // nd[1..n] (nd[0] unused)
    vector<ll> dep;   // dep[i]: departure time from position i (dep[0] = depot = 0)
    vector<ll> lat;   // lat[i]: latest arrival at position i keeping suffix feasible (lat[n+1] = INF)
    vector<int> ld;   // ld[i]: load after position i
    ll cost = 0;
    int n() const { return (int)nd.size() - 1; }
};
static vector<Route> rt, backup;
static vector<int> rver;  // route version, bumped on every change (invalidates insertion cache)
static int gver = 0;
struct CacheEntry { int ver = -1, i = 0, j = 0; ll cost = 0; };
static vector<CacheEntry> icache; // [q * (K + 1) + k]
static vector<int> veh;
static ll cur_obj = 0;

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
        if (i < n) { ll u = r.lat[i + 1] - T(nout[x], nin[r.nd[i + 1]]) - ndur[x]; if (u < v) v = u; }
        r.lat[i] = v;
    }
    r.cost = cost;
    return true;
}

// Best insertion of request q into route k. Returns cost increase (INFLL if impossible) and positions.
// Passenger: inserted after position bi. Parcel: pickup after bi, drop after original position bj (bj >= bi).
static ll best_insert(int k, int q, int& bi, int& bj, bool blink) {
    const Route& r = rt[k];
    const int n = r.n();
    const int* nd = r.nd.data(); const ll* dep = r.dep.data(); const ll* lat = r.lat.data(); const int* ld = r.ld.data();
    ll best = INFLL;
    if (q <= N) {
        const int P = nin[q], D = nout[q]; const ll e = ne[q], l = nl[q], du = ndur[q]; const int ic = nic[q];
        for (int i = 0; i <= n; ++i) {
            if (dep[i] > l) break;
            int a = i ? nout[nd[i]] : O[k];
            ll arr = dep[i] + T(a, P);
            if (arr > l) continue;
            ll d = max(arr, e) + du;
            ll dc = C(a, P) + ic;
            if (i < n) {
                int b = nin[nd[i + 1]];
                if (d + T(D, b) > lat[i + 1]) continue;
                dc += C(D, b) - C(a, b);
            }
            if (dc < best && !(blink && (rng() & BLINK_MASK) == 0)) { best = dc; bi = i; }
        }
        return best;
    }
    const int p = q, dn = R + q;
    const int Pp = nin[p], Dq = nin[dn], w = nw[p], cap = Q[k];
    const ll ep = ne[p], lp = nl[p], sp = ndur[p], eq = ne[dn], lq = nl[dn], sq = ndur[dn];
    const int cPD = C(Pp, Dq), tPD = T(Pp, Dq);
    for (int i = 0; i <= n; ++i) {
        if (dep[i] > lp) break;
        if (ld[i] + w > cap) continue;
        int a = i ? nout[nd[i]] : O[k];
        ll arr = dep[i] + T(a, Pp);
        if (arr > lp) continue;
        ll dp = max(arr, ep) + sp;
        ll cA = C(a, Pp);
        // drop immediately after pickup
        {
            ll aq = dp + tPD;
            if (aq <= lq) {
                ll dq = max(aq, eq) + sq;
                ll dc = cA + cPD; bool ok = true;
                if (i < n) {
                    int b = nin[nd[i + 1]];
                    if (dq + T(Dq, b) > lat[i + 1]) ok = false;
                    else dc += C(Dq, b) - C(a, b);
                }
                if (ok && dc < best && !(blink && (rng() & BLINK_MASK) == 0)) { best = dc; bi = i; bj = i; }
            }
        }
        if (i == n) continue;
        int b0 = nin[nd[i + 1]];
        ll base = cA + C(Pp, b0) - C(a, b0);
        ll cur = dp; int prev = Pp; bool sync = false;
        for (int j = i + 1; j <= n; ++j) {
            int x = nd[j];
            if (!sync) {
                ll ar = cur + T(prev, nin[x]);
                if (ar > nl[x]) break;
                ll dj = max(ar, ne[x]) + ndur[x];
                if (dj == dep[j]) sync = true;
                cur = dj;
            } else cur = dep[j];
            if (ld[j] + w > cap) break;
            if (cur > lq) break;
            int o = nout[x];
            prev = o;
            ll aq = cur + T(o, Dq);
            if (aq > lq) continue;
            ll dq = max(aq, eq) + sq;
            ll dc = base + C(o, Dq);
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

// --- change tracking for cheap rollback ---
static vector<int> touch_stamp, ruin_stamp, cand_stamp;
static int stamp_id = 0;
static vector<int> touched;
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

// Greedy insertion of candidate list (in given order) at the cheapest profitable position over all taxis.
static void recreate(const vector<int>& cand, bool blink) {
    for (int q : cand) {
        if (veh[q]) continue;
        ll best = INFLL; int bk = -1, bi = 0, bj = 0;
        CacheEntry* ce = &icache[(size_t)q * (K + 1)];
        for (int k = 1; k <= K; ++k) {
            CacheEntry& e = ce[k];
            if (e.ver != rver[k]) { e.cost = best_insert(k, q, e.i, e.j, blink); e.ver = rver[k]; }
            if (e.cost < best) { best = e.cost; bk = k; bi = e.i; bj = e.j; }
        }
        if (bk != -1 && Rev[q] - best > 0) do_insert(bk, q, bi, bj);
    }
}

static vector<int> removed, cand, tmp_reqs;

static vector<int> seed_pool;
// Remove every request listed in tmp_reqs from route k. Rolled back if the shortened route becomes
// infeasible (only possible when travel times violate the triangle inequality).
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

// SISR-style ruin: remove strings of consecutive nodes from routes near a random seed request.
static void ruin(int seed) {
    removed.clear();
    int tot_nodes = 0, busy = 0;
    for (int k = 1; k <= K; ++k) { int n = rt[k].n(); if (n) { tot_nodes += n; ++busy; } }
    if (!busy) return;
    double avg = (double)tot_nodes / busy;
    double ls_max = min((double)L_MAX, avg);
    double ks_max = 4.0 * C_AVG / (1.0 + ls_max) - 1.0;
    int ks = (int)(rand01() * ks_max) + 1;
    int done = 0;
    auto try_route = [&](int c) {
        int k = veh[c];
        if (!k || ruin_stamp[k] == stamp_id) return;
        ruin_stamp[k] = stamp_id;
        Route& r = rt[k];
        int n = r.n(), pos = 1;
        for (int i = 1; i <= n; ++i) if (r.nd[i] == c) { pos = i; break; }
        int lmax = max(1, min(n, (int)ls_max));
        int len = rand_int(1, lmax);
        int lo = max(1, pos - len + 1), hi = min(pos, n - len + 1);
        int st = rand_int(lo, hi);
        tmp_reqs.clear();
        for (int i = st; i < st + len; ++i) { int x = r.nd[i]; tmp_reqs.push_back(x > R ? x - R : x); }
        if (remove_from_route(k)) ++done;
    };
    int mode = rng() % 100;
    if (mode < RUIN_RANDOM_PCT) {                          // random removal across all routes
        int cnt = rand_int(1, max(1, (int)(2 * C_AVG) - 1));
        for (int t = 0; t < cnt; ++t) {
            int q = seed_pool[rng() % seed_pool.size()];
            if (!veh[q]) continue;
            tmp_reqs.assign(1, q);
            remove_from_route(veh[q]);
        }
    } else if (mode < RUIN_RANDOM_PCT + RUIN_ROUTE_PCT) {  // dismantle one whole route
        int k = rand_int(1, K);
        for (int t = 0; t < K && rt[k].n() == 0; ++t) k = k % K + 1;
        tmp_reqs.clear();
        for (int i = 1; i <= rt[k].n(); ++i) { int x = rt[k].nd[i]; if (x <= R) tmp_reqs.push_back(x); }
        remove_from_route(k);
    } else {                                               // SISR string removal around the seed
        try_route(seed);
        for (int c : nbr[seed]) { if (done >= ks) break; try_route(c); }
    }
}

static vector<vector<int>> best_nd;
static ll best_obj = 0;
static void save_best() {
    best_obj = cur_obj;
    for (int k = 1; k <= K; ++k) best_nd[k] = rt[k].nd;
}

static void load_best() {
    for (int r = 1; r <= R; ++r) veh[r] = 0;
    for (int k = 1; k <= K; ++k) {
        rt[k].nd = best_nd[k]; recompute(k);
        for (int i = 1; i <= rt[k].n(); ++i) { int x = rt[k].nd[i]; if (x <= R) veh[x] = k; }
    }
    cur_obj = best_obj;
}

// ============================================================================
// EXACT ROUTE ENUMERATION + SET PACKING
// When time windows are tight every taxi has only a few thousand feasible routes. We enumerate all of
// them (best ordering per (taxi, request set)) and pick one route per taxi with simulated annealing;
// a Lagrangian upper bound stops the search as soon as optimality is proven.
// ============================================================================
static const long long ENUM_STATE_CAP = 500000000; // give up enumeration (fall back to R&R) beyond this
static const size_t ENUM_COL_CAP = 20000000;
static const int LAGR_MAX_IT = 2000;             // Lagrangian subgradient iterations (upper bound)
static const ll LAGR_WORK = 200000000;           // ... capped so that iterations * column entries <= this
static const double ENUM_TIME_FRAC = 0.50;       // enumeration may use at most this share of the time limit
static double SP_STALL_SEC = 2.0;                // stop set-packing SA after this long without improvement

struct Column { int k; ll profit; int rbeg, rlen, nbeg, nlen; };
static vector<Column> cols;
static vector<int> col_req, col_nodes;          // flattened sorted request sets / node sequences
static unordered_map<uint64_t, int> col_index;
static long long enum_states;
static bool enum_abort;
static chrono::steady_clock::time_point enum_deadline;
static vector<int> e_seq, e_open, e_reqs, e_sorted, e_by_l;
static vector<ll> e_l;                            // nl of e_by_l, ascending
static vector<char> e_used;

static void enum_record(int k, ll profit) {
    e_sorted = e_reqs; sort(e_sorted.begin(), e_sorted.end());
    uint64_t h = 1469598103934665603ull ^ (uint64_t)k * 0x9E3779B97F4A7C15ull;
    for (int q : e_sorted) { h ^= (uint64_t)q + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); h *= 1099511628211ull; }
    auto it = col_index.find(h);
    if (it != col_index.end()) {
        Column& c = cols[it->second];
        if (c.k != k || c.rlen != (int)e_sorted.size() || !equal(e_sorted.begin(), e_sorted.end(), col_req.begin() + c.rbeg)) return; // hash clash: drop
        if (profit > c.profit) { c.profit = profit; copy(e_seq.begin(), e_seq.end(), col_nodes.begin() + c.nbeg); }
        return;
    }
    if (cols.size() >= ENUM_COL_CAP) { enum_abort = true; return; }
    col_index.emplace(h, (int)cols.size());
    cols.push_back({k, profit, (int)col_req.size(), (int)e_sorted.size(), (int)col_nodes.size(), (int)e_seq.size()});
    col_req.insert(col_req.end(), e_sorted.begin(), e_sorted.end());
    col_nodes.insert(col_nodes.end(), e_seq.begin(), e_seq.end());
}
// every still-open parcel (except `skip`) must remain deliverable directly from (v, dep)
static inline bool open_ok(ll dep, int v, int skip) {
    for (int o : e_open) if (o != skip && dep + T(v, nin[R + o]) > nl[R + o]) return false;
    return true;
}
static void enum_dfs(int k, int last, ll time, int load, ll profit) {
    if (enum_abort) return;
    if (++enum_states > ENUM_STATE_CAP) { enum_abort = true; return; }
    if ((enum_states & 4095) == 0 && chrono::steady_clock::now() > enum_deadline) { enum_abort = true; return; }
    if (e_open.empty() && !e_seq.empty() && profit > 0) enum_record(k, profit);
    for (size_t oi = 0; oi < e_open.size(); ++oi) {           // deliver an open parcel
        int q = e_open[oi], x = R + q;
        ll arr = time + T(last, nin[x]);
        if (arr > nl[x]) continue;
        ll dep = max(arr, ne[x]) + ndur[x];
        if (!open_ok(dep, nout[x], q)) continue;
        e_open[oi] = e_open.back(); e_open.pop_back(); e_seq.push_back(x);
        enum_dfs(k, nout[x], dep, load + nw[x], profit - C(last, nin[x]) + Rev[q]);
        e_seq.pop_back(); e_open.push_back(q); swap(e_open[oi], e_open.back());
    }
    // start a new request (only those whose latest pickup time is not already passed)
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
        else { e_open.push_back(q); enum_dfs(k, nout[q], dep, load + nw[q], profit - C(last, nin[q])); e_open.pop_back(); }
        e_used[q] = 0; e_seq.pop_back(); e_reqs.pop_back();
    }
}

// Returns false (and frees everything) if the route space is too large to enumerate.
static bool column_solve(const vector<int>& servable_list, chrono::steady_clock::time_point t_start) {
    e_by_l = servable_list;
    sort(e_by_l.begin(), e_by_l.end(), [](int a, int b) { return nl[a] < nl[b]; });
    e_l.clear(); for (int q : e_by_l) e_l.push_back(nl[q]);
    e_used.assign(R + 1, 0);
    enum_states = 0; enum_abort = false;
    enum_deadline = t_start + chrono::microseconds((ll)(TIME_LIMIT_SEC * ENUM_TIME_FRAC * 1e6));
    auto t_enum_start = chrono::steady_clock::now();
    for (int k = 1; k <= K && !enum_abort; ++k) enum_dfs(k, O[k], 0, 0, 0);
    double t_enum_sec = chrono::duration<double>(chrono::steady_clock::now() - t_enum_start).count();
    col_index = unordered_map<uint64_t, int>();
    if (enum_abort) {
        cerr << "[PHASE 1: ABORTED] enum_states=" << enum_states 
             << " | cols=" << cols.size() 
             << " | enum_time=" << fixed << setprecision(4) << t_enum_sec << "s -> Falling back to Phase 2!\n";
        cols = vector<Column>(); col_req = vector<int>(); col_nodes = vector<int>();
        return false;
    }
    const int NC = cols.size();
    cerr << "[PHASE 1: ENUMERATION SUCCESS] enum_states=" << enum_states 
         << " | unique_cols=" << NC 
         << " | enum_time=" << fixed << setprecision(4) << t_enum_sec << "s\n";
    vector<vector<int>> tcols(K + 1);                 // per taxi, by profit descending
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
    // greedy start
    {
        vector<int> ord(NC); iota(ord.begin(), ord.end(), 0);
        sort(ord.begin(), ord.end(), [](int a, int b) { return cols[a].profit > cols[b].profit; });
        for (int j : ord) if (chosen[cols[j].k] < 0 && is_free(j)) assign(cols[j].k, j);
    }
    ll best = cur; vector<int> best_choice = chosen;
    ll greedy_best = best;

    // Lagrangian upper bound (relax "each request at most once")
    ll ub = LLONG_MAX;
    bool lagr_exact = false;
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
            if (L < best_bound - 1e-9) { best_bound = L; stall = 0; } else if (++stall >= 10) { lambda *= 0.5; stall = 0; }
            if (best_bound < (double)best + 1 - 1e-6) break;
            double norm = 0; for (int q : servable_list) { if (u[q] <= 0 && g[q] > 0) g[q] = 0; norm += g[q] * g[q]; }
            if (norm < 1e-12) {                          // relaxed solution satisfies complementary slackness -> optimal
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
    cerr << "[PHASE 1: LAGRANGIAN] greedy_obj=" << greedy_best 
         << " | post_lagr_obj=" << best 
         << " | lagrangian_UB=" << ub 
         << " | comp_slackness_optimal=" << (lagr_exact ? "YES" : "NO") << "\n";

    // simulated annealing over route selection
    long long sa_iters = 0;
    if (best < ub && NC > 0) {
        double avgp = 0; for (int j = 0; j < NC; ++j) avgp += cols[j].profit; avgp /= NC;
        const double T0 = max(1e-9, 0.3 * avgp), TF = max(1e-9, 0.003 * avgp);
        vector<int> busy; for (int k = 1; k <= K; ++k) if (!tcols[k].empty()) busy.push_back(k);
        vector<pair<int, int>> log_;                 // (taxi, previous column)
        vector<int> refill;
        auto set_col = [&](int k, int j) { log_.push_back({k, chosen[k]}); unassign(k); assign(k, j); };
        double elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
        const double s0 = elapsed, budget = max(1e-9, TIME_LIMIT_SEC - s0);
        double temp = T0, last_impr = s0;
        for (long long it = 0;; ++it) {
            sa_iters = it;
            if ((it & 255) == 0) {
                elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
                if (elapsed >= TIME_LIMIT_SEC || elapsed - last_impr > SP_STALL_SEC) break;
                temp = T0 * pow(TF / T0, (elapsed - s0) / budget);
            }
            int k = busy[rng() % busy.size()];
            const auto& tc = tcols[k];
            int j = (rng() & 1) ? tc[rng() % tc.size()] : tc[rng() % min<size_t>(tc.size(), 16)];
            if (rng() % 16 == 0) j = -1;                // occasionally just empty the taxi
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
            for (int k2 : refill)                      // best compatible route for each ejected taxi
                for (int c : tcols[k2]) if (is_free(c)) { set_col(k2, c); break; }
            ll delta = cur - before;
            if (delta >= 0 || (double)delta > temp * log(rand01() + 1e-300)) {
                if (cur > best) {
                    best = cur; best_choice = chosen;
                    if (best >= ub) break;                 // proven optimal
                    last_impr = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
                }
            } else {
                for (int i = (int)log_.size() - 1; i >= 0; --i) { unassign(log_[i].first); assign(log_[i].first, log_[i].second); }
            }
        }
    }
    double total_p1_sec = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
    cerr << "[PHASE 1: SET PACKING SA] sa_iters=" << sa_iters 
         << " | final_obj=" << best 
         << " | UB=" << ub 
         << " | gap=" << (ub - best) 
         << " | proven_optimal=" << (best >= ub ? "YES" : "NO")
         << " | total_time=" << fixed << setprecision(4) << total_p1_sec << "s\n";
    for (int k = 1; k <= K; ++k) {
        best_nd[k].assign(1, 0);
        int j = best_choice[k];
        if (j >= 0) best_nd[k].insert(best_nd[k].end(), col_nodes.begin() + cols[j].nbeg, col_nodes.begin() + cols[j].nbeg + cols[j].nlen);
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc > 1) { TIME_LIMIT_SEC = atof(argv[1]); SP_STALL_SEC = max(SP_STALL_SEC, 0.4 * TIME_LIMIT_SEC); }
    auto t_start = chrono::steady_clock::now();
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
    vector<int> pe(R + 1);
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

    // neighbour lists (spatial + temporal relatedness)
    nbr.assign(R + 1, {});
    {
        vector<pair<ll, int>> tmp;
        for (int a = 1; a <= R; ++a) {
            tmp.clear();
            for (int b = 1; b <= R; ++b) if (b != a && servable[b])
                tmp.push_back({(ll)T(P[a], P[b]) + T(D[a], D[b]) + llabs(ne[a] - ne[b]), b});
            int s = min((int)tmp.size(), NB_SIZE);
            partial_sort(tmp.begin(), tmp.begin() + s, tmp.end());
            for (int i = 0; i < s; ++i) nbr[a].push_back(tmp[i].second);
        }
    }

    rver.assign(K + 1, 0); icache.assign((size_t)(R + 1) * (K + 1), CacheEntry());
    rt.assign(K + 1, Route()); backup.assign(K + 1, Route()); best_nd.assign(K + 1, vector<int>(1, 0));
    for (int k = 1; k <= K; ++k) { rt[k].nd.assign(1, 0); recompute(k); }
    veh.assign(R + 1, 0);
    touch_stamp.assign(K + 1, 0); ruin_stamp.assign(K + 1, 0); cand_stamp.assign(R + 1, 0);

    vector<int> servable_list;
    for (int r = 1; r <= R; ++r) if (servable[r]) servable_list.push_back(r);
    seed_pool = servable_list;

    bool solved = K > 0 && !servable_list.empty() && column_solve(servable_list, t_start);
    if (solved) {
        cerr << "[SUMMARY] STOPPED AT PHASE 1 (Column Enumeration + Set Packing)! Phase 2 (SISR) was NOT executed.\n";
    } else if (K > 0 && !servable_list.empty()) {
        cerr << "[PHASE 2: SISR RUIN & RECREATE] Starting fallback search...\n";
        // initial greedy: most valuable first
        vector<int> init = servable_list;
        sort(init.begin(), init.end(), [&](int a, int b) { return Rev[a] - nic[a] > Rev[b] - nic[b]; });
        ++stamp_id; rev_delta = 0;
        recreate(init, false);
        ll c = 0; for (int k = 1; k <= K; ++k) c += rt[k].cost;
        cur_obj = rev_delta - c;
        save_best();

        double mean_rev = 0; for (int r : servable_list) mean_rev += Rev[r];
        mean_rev /= servable_list.size();
        const double T0 = max(1e-9, mean_rev * T0_RATIO), TF = max(1e-9, mean_rev * TF_RATIO);

        double elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
        const double search_start = elapsed;
        const double budget = max(0.0, TIME_LIMIT_SEC - search_start);
        double temp = T0;
        int cur_cycle = 0;
        const int SA_CYCLES = max(1, min(20, CYCLE_SCALE / max(1, (int)servable_list.size())));
        long long p2_iters = 0;
        for (long long it = 0;; ++it) {
            p2_iters = it;
            if ((it & 15) == 0) {
                elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
                if (elapsed >= TIME_LIMIT_SEC) break;
                double prog = budget > 0 ? (elapsed - search_start) / budget : 1.0;
                prog *= SA_CYCLES;
                int cyc = min(SA_CYCLES - 1, (int)prog);
                prog -= cyc;
                if (cyc != cur_cycle) { cur_cycle = cyc; if (RESTART_FROM_BEST) load_best(); }
                temp = T0 * pow(TF / T0, prog);
            }
            ++stamp_id; touched.clear(); veh_log.clear(); rev_delta = 0;

            int seed = servable_list[rng() % servable_list.size()];
            ruin(seed);

            // candidate pool for recreate
            cand.clear();
            auto add = [&](int q) { if (servable[q] && !veh[q] && cand_stamp[q] != stamp_id) { cand_stamp[q] = stamp_id; cand.push_back(q); } };
            for (int q : removed) add(q);
            size_t nrem = cand.size();
            for (size_t t = 0; t < nrem; ++t) {
                const auto& nb = nbr[cand[t]];
                for (int i = 0, lim = min((int)nb.size(), CAND_NB); i < lim; ++i) add(nb[i]);
            }
            add(seed);
            for (int i = 0, lim = min((int)nbr[seed].size(), CAND_NB); i < lim; ++i) add(nbr[seed][i]);
            for (int i = 0; i < CAND_RAND; ++i) add(servable_list[rng() % servable_list.size()]);

            int mode = rng() % 10;
            if (mode < 4) { for (int i = (int)cand.size() - 1; i > 0; --i) swap(cand[i], cand[rng() % (i + 1)]); }
            else if (mode < 8) sort(cand.begin(), cand.end(), [&](int a, int b) { return Rev[a] > Rev[b]; });
            else if (mode < 9) sort(cand.begin(), cand.end(), [&](int a, int b) { return ne[a] < ne[b]; });
            else sort(cand.begin(), cand.end(), [&](int a, int b) { return Rev[a] - nic[a] > Rev[b] - nic[b]; });

            recreate(cand, true);

            ll dcost = 0;
            for (int k : touched) dcost += rt[k].cost - backup[k].cost;
            ll new_obj = cur_obj + rev_delta - dcost;
            if (new_obj >= cur_obj || (double)(new_obj - cur_obj) > temp * log(rand01() + 1e-300)) {
                cur_obj = new_obj;
                if (cur_obj > best_obj) save_best();
            } else {
                for (int k : touched) { swap(rt[k], backup[k]); rver[k] = ++gver; }
                for (int i = (int)veh_log.size() - 1; i >= 0; --i) veh[veh_log[i].first] = veh_log[i].second;
            }
        }
        cerr << "[SUMMARY] COMPLETED PHASE 2 (SISR) | p2_iters=" << p2_iters << " | final_obj=" << best_obj << "\n";
    }

    // final independent verification of the best solution
    ll benefit = 0;
    for (int k = 1; k <= K; ++k) {
        rt[k].nd = best_nd[k];
        if (!recompute(k)) { rt[k].nd.assign(1, 0); recompute(k); } // never expected
        benefit -= rt[k].cost;
        for (int i = 1; i <= rt[k].n(); ++i) { int x = rt[k].nd[i]; if (x <= R) benefit += Rev[x]; }
    }

    string out;
    out.reserve(1 << 16);
    out += to_string(benefit); out += '\n';
    for (int k = 1; k <= K; ++k) {
        const auto& nd = rt[k].nd;
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
