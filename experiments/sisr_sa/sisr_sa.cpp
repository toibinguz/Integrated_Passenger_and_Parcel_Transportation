#pragma GCC optimize("O3,unroll-loops")
#include <bits/stdc++.h>
using namespace std;
typedef long long ll;
namespace fs = std::filesystem;

// ============================================================================
// PARAMETERS & HYPERPARAMETERS (SISR + SA STANDALONE)
// ============================================================================
static double TIME_LIMIT_SEC = 2.0;      // Wall-clock search budget (seconds)
static const int NB_SIZE = 60;           // Spatial + temporal neighbor list size
static const double C_AVG = 10.0;        // SISR: average removed requests
static const int L_MAX = 10;             // SISR: maximum string length
static const int CAND_NB = 3;            // Unserved neighbors offered to recreate
static const int CAND_RAND = 6;          // Random unserved requests offered to recreate
static const unsigned BLINK_MASK = 63;   // Christiaens blink probability (1/64)
static const double T0_RATIO = 1.0;      // SA start temperature multiplier (scale-invariant)
static const double TF_RATIO = 0.005;    // SA final temperature multiplier
static const int RUIN_RANDOM_PCT = 10;   // % of iterations doing random removal
static const int RUIN_ROUTE_PCT = 5;     // % of iterations dismantling a whole route
static const int CYCLE_SCALE = 600;      // Controls number of reheat cycles based on R
static const int RESTART_FROM_BEST = 1;  // Reheat cycle restarts from best solution
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
static inline int rand_int(int lo, int hi) { return lo + (int)(rng() % (uint64_t)(hi - lo + 1)); }
static inline double rand01() { return (rng() >> 11) * (1.0 / 9007199254740992.0); }

// ============================================================================
// PROBLEM DATA STRUCTURES
// ============================================================================
static int K, N, M, V, R, SV;
static vector<int> O, Q, Rev;
static vector<int> Tm, Cm;                // Flat (V+1)x(V+1) matrices
static inline int T(int u, int v) { return Tm[u * SV + v]; }
static inline int C(int u, int v) { return Cm[u * SV + v]; }

// Route node representations:
// Passenger r -> node r (atomic macro node P->D)
// Parcel r -> pickup node r, drop node R+r
static vector<int> nin, nout, nw, nic;
static vector<ll> ne, nl, ndur;
static vector<char> servable;
static vector<vector<int>> nbr;

// ============================================================================
// SOLUTION STATE & ACCELERATION STRUCTURES
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
static vector<int> rver;                 // Route version, bumped on modification
static int gver = 0;
struct CacheEntry { int ver = -1, i = 0, j = 0; ll cost = 0; };
static vector<CacheEntry> icache;        // [q * (K + 1) + k]
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
        if (i < n) { 
            ll u = r.lat[i + 1] - T(nout[x], nin[r.nd[i + 1]]) - ndur[x]; 
            if (u < v) v = u; 
        }
        r.lat[i] = v;
    }
    r.cost = cost;
    return true;
}

// O(1) slack checking best insertion of request q into route k
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

        // Immediate drop after pickup
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

// Change tracking structures for cheap atomic rollbacks
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

// Recreate candidate requests using icache acceleration
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

static vector<int> removed, cand, tmp_reqs, seed_pool;

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

// SISR Ruin operator
static int ruin(int seed) {
    removed.clear();
    int tot_nodes = 0, busy = 0;
    for (int k = 1; k <= K; ++k) { int n = rt[k].n(); if (n) { tot_nodes += n; ++busy; } }
    if (!busy) return 0;
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
    int ruin_type = 1; // 1: SISR string, 2: Random, 3: Whole route
    if (mode < RUIN_RANDOM_PCT) {
        ruin_type = 2;
        int cnt = rand_int(1, max(1, (int)(2 * C_AVG) - 1));
        for (int t = 0; t < cnt; ++t) {
            int q = seed_pool[rng() % seed_pool.size()];
            if (!veh[q]) continue;
            tmp_reqs.assign(1, q);
            remove_from_route(veh[q]);
        }
    } else if (mode < RUIN_RANDOM_PCT + RUIN_ROUTE_PCT) {
        ruin_type = 3;
        int k = rand_int(1, K);
        for (int t = 0; t < K && rt[k].n() == 0; ++t) k = k % K + 1;
        tmp_reqs.clear();
        for (int i = 1; i <= rt[k].n(); ++i) { int x = rt[k].nd[i]; if (x <= R) tmp_reqs.push_back(x); }
        remove_from_route(k);
    } else {
        ruin_type = 1;
        try_route(seed);
        for (int c : nbr[seed]) { if (done >= ks) break; try_route(c); }
    }
    return ruin_type;
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
// MAIN EXECUTION
// ============================================================================
int main(int argc, char** argv) {
    ios_base::sync_with_stdio(false); cin.tie(NULL);
    auto t_start = chrono::steady_clock::now();

    string test_filepath = "";
    if (argc > 1) TIME_LIMIT_SEC = atof(argv[1]);
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

    // Relatedness neighbor list (spatial + temporal)
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

    // --- SETUP OUTPUT DIRECTORY & SESSION LOGGING ("Log, log nữa, log mãi") ---
    string out_dir = "";
    ofstream flog, freport;
    bool write_files = !test_filepath.empty();
    if (write_files) {
        string tname = fs::path(test_filepath).stem().string();
        auto now = chrono::system_clock::now();
        time_t now_c = chrono::system_clock::to_time_t(now);
        tm now_tm = *localtime(&now_c);
        char tbuf[64];
        strftime(tbuf, sizeof(tbuf), "%Y%m%d_%H%M%S", &now_tm);
        string folder_name = "run_" + tname + "_" + string(tbuf);
        out_dir = (fs::path("experiments") / "sisr_sa" / "output" / folder_name).string();
        fs::create_directories(out_dir);
        flog.open(out_dir + "/log.txt");
        freport.open(out_dir + "/report.txt");
        cerr << "[SISR_SA] Initialized run folder: " << out_dir << "\n";
    }

    if (flog.is_open()) {
        flog << "# SOICT 2026 - SISR SA METRIC LOG\n";
        flog << "# Instance: " << test_filepath << " | K=" << K << " N=" << N << " M=" << M << " V=" << V << "\n";
        flog << "# Header: iter,elapsed_s,cycle,temp,ruin_mode,n_removed,delta,cur_obj,best_obj,status\n";
    }

    // 1. Initial Greedy Solution
    vector<int> init = servable_list;
    sort(init.begin(), init.end(), [&](int a, int b) { return Rev[a] - nic[a] > Rev[b] - nic[b]; });
    ++stamp_id; rev_delta = 0;
    recreate(init, false);
    ll initial_cost = 0; for (int k = 1; k <= K; ++k) initial_cost += rt[k].cost;
    cur_obj = rev_delta - initial_cost;
    save_best();
    ll initial_greedy_obj = cur_obj;

    cerr << "[SISR_SA: INITIAL GREEDY] Objective = " << cur_obj << "\n";

    // 2. Scale-Invariant Multi-cycle SA Parameters
    double mean_rev = 0; for (int r : servable_list) mean_rev += Rev[r];
    if (!servable_list.empty()) mean_rev /= servable_list.size();
    const double T0 = max(1e-9, mean_rev * T0_RATIO);
    const double TF = max(1e-9, mean_rev * TF_RATIO);

    double elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
    const double search_start = elapsed;
    const double budget = max(0.001, TIME_LIMIT_SEC - search_start);
    double temp = T0;
    int cur_cycle = 0;
    const int SA_CYCLES = max(1, min(20, CYCLE_SCALE / max(1, (int)servable_list.size())));

    long long total_iters = 0;
    long long accepted_count = 0;
    long long improved_count = 0;

    // 3. Main Search Loop
    for (long long it = 0;; ++it) {
        total_iters = it;
        if ((it & 15) == 0) {
            elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
            if (elapsed >= TIME_LIMIT_SEC) break;
            double prog = budget > 0 ? (elapsed - search_start) / budget : 1.0;
            prog *= SA_CYCLES;
            int cyc = min(SA_CYCLES - 1, (int)prog);
            prog -= cyc;
            if (cyc != cur_cycle) {
                cur_cycle = cyc;
                if (RESTART_FROM_BEST) load_best();
            }
            temp = T0 * pow(TF / T0, prog);
        }

        ++stamp_id; touched.clear(); veh_log.clear(); rev_delta = 0;

        int seed = servable_list[rng() % servable_list.size()];
        int r_mode = ruin(seed);
        int n_rem = (int)removed.size();

        // Candidate pool for recreate
        cand.clear();
        auto add = [&](int q) {
            if (servable[q] && !veh[q] && cand_stamp[q] != stamp_id) {
                cand_stamp[q] = stamp_id; cand.push_back(q);
            }
        };
        for (int q : removed) add(q);
        size_t nrem = cand.size();
        for (size_t t = 0; t < nrem; ++t) {
            const auto& nb = nbr[cand[t]];
            for (int i = 0, lim = min((int)nb.size(), CAND_NB); i < lim; ++i) add(nb[i]);
        }
        add(seed);
        for (int i = 0, lim = min((int)nbr[seed].size(), CAND_NB); i < lim; ++i) add(nbr[seed][i]);
        for (int i = 0; i < CAND_RAND; ++i) add(servable_list[rng() % servable_list.size()]);

        // Stochastic candidate sorting
        int smode = rng() % 10;
        if (smode < 4) { for (int i = (int)cand.size() - 1; i > 0; --i) swap(cand[i], cand[rng() % (i + 1)]); }
        else if (smode < 8) sort(cand.begin(), cand.end(), [&](int a, int b) { return Rev[a] > Rev[b]; });
        else if (smode < 9) sort(cand.begin(), cand.end(), [&](int a, int b) { return ne[a] < ne[b]; });
        else sort(cand.begin(), cand.end(), [&](int a, int b) { return Rev[a] - nic[a] > Rev[b] - nic[b]; });

        recreate(cand, true);

        ll dcost = 0;
        for (int k : touched) dcost += rt[k].cost - backup[k].cost;
        ll new_obj = cur_obj + rev_delta - dcost;
        ll delta = new_obj - cur_obj;
        string status = "REJECT";

        if (delta >= 0 || (double)delta > temp * log(rand01() + 1e-300)) {
            cur_obj = new_obj;
            ++accepted_count;
            if (cur_obj > best_obj) {
                save_best();
                ++improved_count;
                status = "IMPROVE_GLOBAL";
            } else if (delta > 0) {
                status = "IMPROVE_LOCAL";
            } else {
                status = "SA_ACCEPT";
            }
        } else {
            for (int k : touched) { swap(rt[k], backup[k]); rver[k] = ++gver; }
            for (int i = (int)veh_log.size() - 1; i >= 0; --i) veh[veh_log[i].first] = veh_log[i].second;
        }

        // Log continuous rich metrics ("Log log nữa log mãi")
        if (flog.is_open() && ((it & 127) == 0 || status == "IMPROVE_GLOBAL")) {
            flog << it << "," << fixed << setprecision(4) << elapsed << "," 
                 << cur_cycle << "," << setprecision(2) << temp << ","
                 << r_mode << "," << n_rem << "," << delta << ","
                 << cur_obj << "," << best_obj << "," << status << "\n";
        }
    }

    double total_wall_time = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
    cerr << "[SISR_SA: COMPLETED] Total Iters = " << total_iters 
         << " | Best Obj = " << best_obj 
         << " | Total Time = " << total_wall_time << "s\n";

    if (flog.is_open()) flog.close();

    // 4. Independent Final Verification of the Best Solution
    ll benefit = 0;
    for (int k = 1; k <= K; ++k) {
        rt[k].nd = best_nd[k];
        if (!recompute(k)) { rt[k].nd.assign(1, 0); recompute(k); }
        benefit -= rt[k].cost;
        for (int i = 1; i <= rt[k].n(); ++i) { int x = rt[k].nd[i]; if (x <= R) benefit += Rev[x]; }
    }

    // 5. Output SOICT Standard Result to String
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

    // Save result.txt and report.txt into output folder if session folder active
    if (write_files && !out_dir.empty()) {
        ofstream fres(out_dir + "/result.txt");
        fres << out;
        fres.close();

        if (freport.is_open()) {
            freport << "======================================================================\n";
            freport << "         SISR + SA STANDALONE PROTOTYPE: RUN REPORT\n";
            freport << "======================================================================\n\n";
            freport << "  * Instance             : " << test_filepath << "\n";
            freport << "  * Problem Dimensions   : K=" << K << ", N=" << N << ", M=" << M << ", V=" << V << "\n";
            freport << "  * Wall-clock Budget    : " << TIME_LIMIT_SEC << "s\n";
            freport << "  * Total Elapsed Time   : " << fixed << setprecision(4) << total_wall_time << "s\n";
            freport << "  * Initial Greedy Obj   : " << initial_greedy_obj << "\n";
            freport << "  * Final Best Objective : " << best_obj << " (Verified: " << benefit << ")\n";
            freport << "  * Net Improvement      : +" << (best_obj - initial_greedy_obj) << "\n\n";
            freport << "  * Total Iterations     : " << total_iters << " (" << fixed << setprecision(0) << (total_iters / max(0.001, total_wall_time)) << " iter/s)\n";
            freport << "  * Accepted Moves       : " << accepted_count << " (" << fixed << setprecision(2) << (100.0 * accepted_count / max(1LL, total_iters)) << "%)\n";
            freport << "  * Global Record Steps  : " << improved_count << "\n";
            freport << "  * Multi-cycle Count    : " << SA_CYCLES << "\n";
            freport << "======================================================================\n";
            freport.close();
        }
    }

    // Output to stdout for pipeline / validator
    fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
