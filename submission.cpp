#pragma GCC optimize("O3,unroll-loops")
#include <bits/stdc++.h>
using namespace std;
typedef long long ll;

// ============================================================================
// 1. GLOBAL CONTROL PARAMETERS & HYPERPARAMETERS (ONLINE JUDGE & BENCHMARK)
// ============================================================================

// --- 1.1. Core Time & Phase 1 Memory Limits ---
static double TIME_LIMIT_SEC = 145.0;                  // Default judge search budget (seconds)
static const double P1_TIME_LIMIT_SEC = 45.0;          // Max total time budget for full Phase 1 (DFS + LagR + SP-SA)
static const size_t P1_MEM_LIMIT_MB = 350;             // Sole Phase 1 column storage capacity limit (MB)
static const size_t P1_MEM_LIMIT_BYTES = P1_MEM_LIMIT_MB * 1024ull * 1024ull;
static const double P1_ENUM_TIME_FRAC = 0.15;          // Max fraction of effective P1 budget spent on DFS enumeration
static const double P1_SP_GATE_GREEDY_RATIO = 0.90;    // Min P1/P2-greedy ratio to run SP-SA when enum_partial=true
static const double P1_WAVE_SEC = 5.5;                 // Target duration per SP-SA wave (seconds)
static const double P1_WAVE_STALL_SEC = 2.5;           // No-improvement stall timeout inside one SP-SA wave (seconds)
static const int P1_MAX_STAGNANT_WAVES = 2;            // Consecutive non-improving waves before handing to P2
static bool VERBOSE_LOG = false;                       // Diagnostics to stderr when enabled (--verbose/-v)

// --- 1.2. Phase 1 Lagrangian Relaxation & Multi-Wave Set Packing SA ---
static const int LAGR_MAX_IT = 2000;              // Max Lagrangian subgradient iterations
static const ll LAGR_WORK = 200000000;            // Work budget for Lagrangian subgradient
static const double LAGR_INIT_LAMBDA = 2.0;       // Initial subgradient step multiplier
static const int LAGR_STALL_ITERS = 10;           // Iterations without bound gain before halving lambda
static const int SP_TCOL_RC_CAP = 2048;           // Elite reduced-cost columns kept per taxi
static const int SP_REQ_COL_CAP = 96;             // Elite columns kept per request
static const double SP_T0_PROFIT_MULT = 0.15;     // Initial SA temperature multiplier of avg column profit
static const double SP_TF_PROFIT_MULT = 0.002;    // Final SA temperature multiplier of avg column profit
static const double SP_COOLING_EXP = 1.35;        // Convex cooling exponent for SP-SA wave temperature
static const int SP_PERTURB_DROP_PCT = 20;        // % of taxis dropped on even-wave perturbation restarts
static const int SP_OP1_PROB_CUTOFF = 45;         // Cumulative % prob for Op1 (Dual Insertion)
static const int SP_OP2_PROB_CUTOFF = 80;         // Cumulative % prob for Op2 (Elite Swap); rest is Op3

// --- 1.3. Phase 2 Hyperparameters (GM-SISR mask=896 + Eject50 + O1 + SEDCA-V4) ---
static const int NB_SIZE = 60;                    // Normalized spatio-temporal neighbor list size
static const double C_AVG = 10.0;                 // SISR: average removed requests
static const int L_MAX = 10;                      // SISR: maximum string length
static const int CAND_NB = 3;                     // Unserved neighbors offered to recreate
static const int CAND_RAND = 10;                  // Random unserved requests offered to recreate
static const unsigned BLINK_MASK = 31;            // Christiaens blink probability (1/32)
static const double REGRET_EMA_DECAY = 0.80;      // EMA persistence factor for request regret memory
static const long long EJECT_ITER_MASK = 1;       // Run Eject50 when (it & EJECT_ITER_MASK) == 0 (50%)
static const int EJECT_PROBES = 6;                // Blocked high-value requests probed per Eject50 step
static const int EJECT_MAX_MOVES = 3;             // Max successful ejection chains per Eject50 pass
static const int EJECT_NBR_SCAN = 14;             // Max neighbors scanned per blocked request in Eject50
static const int EJECT_NBR_TRIES = 7;             // Max occupied neighbors evaluated per blocked request
static const long long O1_ITER_MASK = 31;         // Run O1 polish on accepted move when (it & O1_ITER_MASK) == 0
static const int O1_POOL_CAP = 12;                // Max touched requests scanned per O1 polish
static const int O1_MAX_STEPS = 4;                // Max improving cross-route relocates per polish
static const int P2_WARMUP_DELTAS = 200;          // Negative deltas collected before SEDCA-V4 calibration
static const double P2_TAU_FAST = 0.10;           // Normalized time fraction for initial fast cooling phase
static const double P2_TAU_QUENCH = 0.86;         // Normalized time fraction starting final quench phase
static const double P2_CYCLE_SPAN_FRAC = 0.12;    // Reheat cycle base width relative to (tau_quench - tau_fast)
static const double P2_MIN_CYCLE_TAU = 0.03;      // Minimum normalized tooth duration near quench boundary
static const ll INFLL = (ll)4e18;

// --- Yield-Based Operator Bandit (Ruin Modes 1..3; prior 75 / 15 / 10) ---
static const double BANDIT_PRIOR[4] = {0.0, 7.5, 1.5, 1.0};
static double bandit_w[5] = {0.0, 7.5, 1.5, 1.0, 1.0};
static long long mode_tries[5] = {0}, mode_records[5] = {0};
static const long long BANDIT_UPDATE_ITERS = 20000;
static const int RECALIB_UPDATE_ITERS = 20000;

// ============================================================================
// 2. FAST INPUT READER & ULTRA-FAST RNG (Xorshift64)
// ============================================================================
static char* ibuf = nullptr;
static size_t ilen = 0, ipos = 0;
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
    vector<int> nd;   // nd[1..n] (nd[0] = 0 unused depot anchor)
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

// Side-effect-free route evaluation used by both rt[k] and probe routes (tmp_r)
static bool recompute_route(int k, Route& r) {
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

static inline bool recompute(int k) {
    if (!recompute_route(k, rt[k])) return false;
    rver[k] = ++gver;
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
// 5. PHASE 1: RESUMABLE REDISTRIBUTED COLUMN ENUMERATION + MULTI-WAVE SP-SA
// ============================================================================
struct Column { int k; ll profit; int rbeg, rlen, nbeg, nlen; };
struct ResumeFrame { int phase; size_t idx; };
static vector<Column> cols;
static vector<int> col_req, col_nodes;
static unordered_map<uint64_t, int> col_index;
static long long enum_states = 0, enum_round_states = 0;
static bool enum_abort = false, enum_partial = false, enum_k_cut = false, enum_resuming = false;
static chrono::steady_clock::time_point enum_deadline, enum_round_deadline;
static size_t enum_round_mem_quota = 0;
static size_t enum_k_col0 = 0, enum_k_req0 = 0, enum_k_nodes0 = 0, enum_k_idx0 = 0;
static vector<vector<ResumeFrame>> saved_paths;
static vector<ResumeFrame> resume_in, resume_out;
static vector<int> e_seq, e_open, e_reqs, e_sorted, e_by_l;
static vector<ll> e_l;
static vector<char> e_used;

static inline size_t enum_bytes() {
    size_t live = cols.size() * sizeof(Column)
                + col_req.size() * sizeof(int)
                + col_nodes.size() * sizeof(int);
    return live + live / 2 + col_index.size() * 64;
}

static inline size_t enum_k_bytes() {
    size_t live = (cols.size()      - enum_k_col0)  * sizeof(Column)
                + (col_req.size()   - enum_k_req0)  * sizeof(int)
                + (col_nodes.size() - enum_k_nodes0)* sizeof(int);
    return live + live / 2 + (col_index.size() - enum_k_idx0) * 64;
}

static inline void release_cols() {
    vector<Column>().swap(cols);
    vector<int>().swap(col_req);
    vector<int>().swap(col_nodes);
    unordered_map<uint64_t, int>().swap(col_index);
    vector<vector<ResumeFrame>>().swap(saved_paths);
    vector<ResumeFrame>().swap(resume_in);
    vector<ResumeFrame>().swap(resume_out);
}

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
    size_t col_add = sizeof(Column) + e_sorted.size() * sizeof(int) + e_seq.size() * sizeof(int);
    col_add += col_add / 2 + 64;
    if (enum_bytes() + col_add > P1_MEM_LIMIT_BYTES) { enum_abort = true; return; }
    if (enum_k_bytes() + col_add > enum_round_mem_quota) { enum_k_cut = true; return; }
    col_index.emplace(h, (int)cols.size());
    cols.push_back({k, profit, (int)col_req.size(), (int)e_sorted.size(), (int)col_nodes.size(), (int)e_seq.size()});
    col_req.insert(col_req.end(), e_sorted.begin(), e_sorted.end());
    col_nodes.insert(col_nodes.end(), e_seq.begin(), e_seq.end());
}

static inline bool open_ok(ll dep, int v, int skip) {
    for (int o : e_open) if (o != skip && dep + T(v, nin[R + o]) > nl[R + o]) return false;
    return true;
}

static void enum_dfs(int k, int last, ll time, int load, ll profit, int depth) {
    if (enum_abort || enum_k_cut) return;

    int start_phase = 0;
    size_t start_idx = 0;
    bool skip_record = false;

    if (enum_resuming) {
        if (depth < (int)resume_in.size()) {
            ResumeFrame rf = resume_in[depth];
            if (rf.phase == -1) {
                enum_resuming = false;
            } else {
                skip_record = true;
                start_phase = rf.phase;
                start_idx = rf.idx;
            }
        } else {
            enum_resuming = false;
        }
    }

    if (!enum_resuming) {
        ++enum_states;
        ++enum_round_states;
        if ((enum_round_states & 4095) == 0) {
            auto now = chrono::steady_clock::now();
            if (now > enum_deadline || enum_bytes() > P1_MEM_LIMIT_BYTES) {
                enum_abort = true;
                resume_out.push_back({-1, 0});
                return;
            }
            if (now > enum_round_deadline || enum_k_bytes() > enum_round_mem_quota) {
                enum_k_cut = true;
                resume_out.push_back({-1, 0});
                return;
            }
        }
        if (!skip_record && e_open.empty() && !e_seq.empty() && profit > 0) {
            enum_record(k, profit);
            if (enum_abort || enum_k_cut) {
                resume_out.push_back({-1, 0});
                return;
            }
        }
    }

    if (start_phase == 0) {
        size_t oi0 = (enum_resuming && start_phase == 0) ? start_idx : 0;
        for (size_t oi = oi0; oi < e_open.size(); ++oi) {
            int q = e_open[oi], x = R + q;
            ll arr = time + T(last, nin[x]);
            if (arr > nl[x]) continue;
            ll dep = max(arr, ne[x]) + ndur[x];
            if (!open_ok(dep, nout[x], q)) continue;
            e_open[oi] = e_open.back(); e_open.pop_back(); e_seq.push_back(x);
            enum_dfs(k, nout[x], dep, load + nw[x], profit - C(last, nin[x]) + Rev[q], depth + 1);
            e_seq.pop_back(); e_open.push_back(q); swap(e_open[oi], e_open.back());
            if (enum_abort || enum_k_cut) {
                resume_out.push_back({0, oi});
                return;
            }
        }
    }

    size_t lb = lower_bound(e_l.begin(), e_l.end(), time) - e_l.begin();
    size_t idx0 = (enum_resuming && start_phase == 1) ? start_idx : lb;
    for (size_t idx = idx0; idx < e_by_l.size(); ++idx) {
        int q = e_by_l[idx];
        if (e_used[q]) continue;
        ll arr = time + T(last, nin[q]);
        if (arr > nl[q]) continue;
        ll dep = max(arr, ne[q]) + ndur[q];
        if (q > N && (load + nw[q] > Q[k] || dep + T(nout[q], nin[R + q]) > nl[R + q])) continue;
        if (!open_ok(dep, nout[q], -1)) continue;
        e_used[q] = 1; e_seq.push_back(q); e_reqs.push_back(q);
        if (q <= N) enum_dfs(k, nout[q], dep, load, profit - C(last, nin[q]) - nic[q] + Rev[q], depth + 1);
        else {
            e_open.push_back(q);
            enum_dfs(k, nout[q], dep, load + nw[q], profit - C(last, nin[q]), depth + 1);
            e_open.pop_back();
        }
        e_used[q] = 0; e_seq.pop_back(); e_reqs.pop_back();
        if (enum_abort || enum_k_cut) {
            resume_out.push_back({1, idx});
            return;
        }
    }
}

static bool column_solve(const vector<int>& s_list, chrono::steady_clock::time_point t_start, ll p2_greedy_obj) {
    e_by_l = s_list;
    sort(e_by_l.begin(), e_by_l.end(), [](int a, int b) { return nl[a] < nl[b]; });
    e_l.clear(); for (int q : e_by_l) e_l.push_back(nl[q]);
    e_used.assign(R + 1, 0);
    enum_states = 0; enum_abort = false; enum_partial = false;

    const double p1_budget = min(P1_TIME_LIMIT_SEC, TIME_LIMIT_SEC * 0.85);
    const auto p1_deadline = t_start + chrono::microseconds((ll)(p1_budget * 1e6));
    const double enum_budget = p1_budget * P1_ENUM_TIME_FRAC;
    auto t_enum_start = chrono::steady_clock::now();
    enum_deadline = min(p1_deadline, t_enum_start + chrono::microseconds((ll)(enum_budget * 1e6)));

    saved_paths.assign(K + 1, {});
    vector<int> active(K), next_active;
    iota(active.begin(), active.end(), 1);
    int enum_rounds = 0;

    while (!active.empty() && !enum_abort) {
        ++enum_rounds;
        next_active.clear();
        for (size_t i = 0; i < active.size(); ++i) {
            int k = active[i];
            auto now = chrono::steady_clock::now();
            size_t cur_mem = enum_bytes();
            if (now >= enum_deadline || cur_mem >= P1_MEM_LIMIT_BYTES) {
                enum_abort = true;
                break;
            }
            long long rem_us = chrono::duration_cast<chrono::microseconds>(enum_deadline - now).count();
            if (rem_us < 2000LL) {
                enum_abort = true;
                break;
            }
            size_t rem_taxis = active.size() - i;
            size_t rem_mem = P1_MEM_LIMIT_BYTES - cur_mem;
            if (rem_mem < 65536ull) {
                enum_abort = true;
                break;
            }
            long long slice_us = max(2000LL, rem_us / (long long)rem_taxis);
            enum_round_mem_quota = min(rem_mem, max<size_t>(65536ull, rem_mem / rem_taxis));
            enum_round_deadline = now + chrono::microseconds(slice_us);

            enum_k_cut = false;
            enum_k_col0 = cols.size(); enum_k_req0 = col_req.size();
            enum_k_nodes0 = col_nodes.size(); enum_k_idx0 = col_index.size();
            enum_round_states = 0;

            resume_in.swap(saved_paths[k]);
            resume_out.clear();
            enum_resuming = !resume_in.empty();
            e_seq.clear(); e_open.clear(); e_reqs.clear();
            fill(e_used.begin(), e_used.end(), 0);

            enum_dfs(k, O[k], 0, 0, 0, 0);

            if (enum_abort) break;
            if (enum_k_cut) {
                reverse(resume_out.begin(), resume_out.end());
                saved_paths[k].swap(resume_out);
                next_active.push_back(k);
            } else {
                saved_paths[k].clear();
            }
        }
        if (enum_abort) break;
        active.swap(next_active);
        next_active.clear();
    }
    enum_partial = !active.empty() || enum_abort;
    col_index = unordered_map<uint64_t, int>();

    double enum_sec = chrono::duration<double>(chrono::steady_clock::now() - t_enum_start).count();
    const int NC = (int)cols.size();
    if (NC == 0) { release_cols(); return false; }

    vector<int> owner(R + 1, -1), chosen(K + 1, -1);
    ll cur = 0;
    auto unassign = [&](int k) {
        int j = chosen[k]; if (j < 0) return;
        for (int i = 0; i < cols[j].rlen; ++i) owner[col_req[cols[j].rbeg + i]] = -1;
        cur -= cols[j].profit; chosen[k] = -1;
    };
    auto assign = [&](int k, int j) {
        if (chosen[k] >= 0) unassign(k);
        chosen[k] = j; if (j < 0) return;
        for (int i = 0; i < cols[j].rlen; ++i) owner[col_req[cols[j].rbeg + i]] = j;
        cur += cols[j].profit;
    };
    auto is_free = [&](int j) {
        for (int i = 0; i < cols[j].rlen; ++i) if (owner[col_req[cols[j].rbeg + i]] >= 0) return false;
        return true;
    };

    // 1. Greedy Initial Selection (always runs to seed a warm start for Phase 2)
    {
        vector<int> ord(NC); iota(ord.begin(), ord.end(), 0);
        sort(ord.begin(), ord.end(), [](int a, int b) { return cols[a].profit > cols[b].profit; });
        for (int j : ord) if (chosen[cols[j].k] < 0 && is_free(j)) assign(cols[j].k, j);
    }
    ll best = cur; vector<int> best_choice = chosen;

    // 2. Smart gate: run Lagrangian Relaxation + Multi-Wave SP-SA if complete OR near Phase 2 greedy
    ll ub = LLONG_MAX;
    bool lagr_exact = false;
    bool should_run_sp = !enum_partial || ((double)best >= (double)p2_greedy_obj * P1_SP_GATE_GREEDY_RATIO);
    if (should_run_sp) {
        vector<vector<int>> tcols(K + 1);
        for (int j = 0; j < NC; ++j) tcols[cols[j].k].push_back(j);
        for (int k = 1; k <= K; ++k) sort(tcols[k].begin(), tcols[k].end(), [](int a, int b) { return cols[a].profit > cols[b].profit; });

        vector<double> u_best(R + 1, 0.0);
        {
            vector<double> u(R + 1, 0.0), g(R + 1);
            double lambda = LAGR_INIT_LAMBDA, best_bound = 1e300; int stall = 0;
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
                if (L < best_bound - 1e-9) { best_bound = L; u_best = u; stall = 0; }
                else if (++stall >= LAGR_STALL_ITERS) { lambda *= 0.5; stall = 0; }

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

        if (VERBOSE_LOG) {
            fprintf(stderr, "[P1: ENUM] cols=%d rounds=%d mem=%.1fMB partial=%d enum_t=%.3fs | UB=%lld greedy=%lld lagr_exact=%d\n",
                    NC, enum_rounds, enum_bytes() / (1024.0 * 1024.0), (int)enum_partial, enum_sec, ub, best, (int)lagr_exact);
        }

        // 3. Fast Dual-Guided Multi-Wave Set Packing SA
        if (best < ub && !lagr_exact && NC > 0) {
            vector<double> col_rc(NC, 0.0);
            for (int j = 0; j < NC; ++j) {
                double rc = (double)cols[j].profit;
                for (int i = 0; i < cols[j].rlen; ++i) rc -= u_best[col_req[cols[j].rbeg + i]];
                col_rc[j] = rc;
            }

            vector<vector<int>> tcols_rc(K + 1);
            for (int k = 1; k <= K; ++k) {
                tcols_rc[k] = tcols[k];
                int keep = min((int)tcols_rc[k].size(), SP_TCOL_RC_CAP);
                partial_sort(tcols_rc[k].begin(), tcols_rc[k].begin() + keep, tcols_rc[k].end(),
                             [&](int a, int b) { return col_rc[a] > col_rc[b]; });
                tcols_rc[k].resize(keep);
            }

            vector<vector<int>> req_cols(R + 1);
            for (int j = 0; j < NC; ++j) {
                for (int i = 0; i < cols[j].rlen; ++i) req_cols[col_req[cols[j].rbeg + i]].push_back(j);
            }
            for (int r : s_list) {
                int keep = min((int)req_cols[r].size(), SP_REQ_COL_CAP);
                partial_sort(req_cols[r].begin(), req_cols[r].begin() + keep, req_cols[r].end(),
                             [&](int a, int b) { return col_rc[a] > col_rc[b]; });
                req_cols[r].resize(keep);
            }

            vector<int> reqs_by_dual = s_list;
            sort(reqs_by_dual.begin(), reqs_by_dual.end(), [&](int a, int b) { return u_best[a] > u_best[b]; });

            vector<int> busy;
            for (int k = 1; k <= K; ++k) if (!tcols_rc[k].empty()) busy.push_back(k);

            if (!busy.empty()) {
                double avgp = 0; for (int j = 0; j < NC; ++j) avgp += cols[j].profit; avgp /= NC;
                const double T0 = max(1e-9, SP_T0_PROFIT_MULT * avgp), TF = max(1e-9, SP_TF_PROFIT_MULT * avgp);

                vector<pair<int, int>> log_;
                vector<int> refill;
                auto set_col = [&](int k, int j) { log_.push_back({k, chosen[k]}); unassign(k); assign(k, j); };
                auto refill_vehicles = [&](const vector<int>& rf) {
                    for (int k2 : rf) for (int c : tcols_rc[k2]) if (is_free(c)) { set_col(k2, c); break; }
                };
                auto apply_col_evict = [&](int k, int j) {
                    set_col(k, -1);
                    if (j >= 0) {
                        for (int i = 0; i < cols[j].rlen; ++i) {
                            int c = owner[col_req[cols[j].rbeg + i]];
                            if (c >= 0) { int k2 = cols[c].k; set_col(k2, -1); refill.push_back(k2); }
                        }
                        set_col(k, j);
                    }
                    refill_vehicles(refill);
                };
                auto sweep_free_vehicles = [&]() {
                    for (int k : busy) if (chosen[k] < 0) {
                        for (int c : tcols_rc[k]) if (is_free(c)) { assign(k, c); break; }
                    }
                };

                auto sp_start = chrono::steady_clock::now();
                auto sp_deadline = p1_deadline;
                double sp_rem = chrono::duration<double>(sp_deadline - sp_start).count();
                double wave_max_sec = min(P1_WAVE_SEC, max(0.0, sp_rem));
                double wave_stall_sec = min(P1_WAVE_STALL_SEC, wave_max_sec);
                int waves_no_impr = 0;

                for (int wave_id = 0; sp_rem > 0.005; ++wave_id) {
                    auto now = chrono::steady_clock::now();
                    if (now >= sp_deadline || best >= ub) break;

                    for (int k = 1; k <= K; ++k) unassign(k);
                    if (wave_id == 0) {
                        for (int k = 1; k <= K; ++k) assign(k, best_choice[k]);
                        sweep_free_vehicles();
                    } else if (wave_id % 2 == 1) {
                        vector<int> v_ord = busy;
                        for (size_t i = v_ord.size() - 1; i > 0; --i) swap(v_ord[i], v_ord[rng() % (i + 1)]);
                        for (int k : v_ord) for (int c : tcols_rc[k]) if (is_free(c)) { assign(k, c); break; }
                        sweep_free_vehicles();
                    } else {
                        for (int k = 1; k <= K; ++k) assign(k, best_choice[k]);
                        vector<int> dropped;
                        for (int k : busy) if ((int)(rng() % 100) < SP_PERTURB_DROP_PCT) { unassign(k); dropped.push_back(k); }
                        for (size_t i = dropped.size(); i > 1; --i) swap(dropped[i - 1], dropped[rng() % i]);
                        for (int k : dropped) for (int c : tcols_rc[k]) if (is_free(c)) { assign(k, c); break; }
                        sweep_free_vehicles();
                    }

                    bool global_impr_this_wave = false;
                    if (cur > best) { best = cur; best_choice = chosen; global_impr_this_wave = true; }
                    if (best >= ub) break;

                    ll wave_best = cur;
                    auto w_start = chrono::steady_clock::now();
                    double rem_sp = chrono::duration<double>(sp_deadline - w_start).count();
                    double wave_budget = min(wave_max_sec, rem_sp);
                    if (wave_budget <= 0.005) break;
                    double w_last_impr = 0.0, temp = T0;

                    for (long long it = 0;; ++it) {
                        if ((it & 255) == 0) {
                            auto tnow = chrono::steady_clock::now();
                            if (tnow >= sp_deadline) break;
                            double w_el = chrono::duration<double>(tnow - w_start).count();
                            if (w_el > wave_budget || w_el - w_last_impr > wave_stall_sec) break;
                            double tau = clamp(w_el / max(1e-6, wave_budget), 0.0, 1.0);
                            temp = T0 * pow(TF / T0, pow(tau, SP_COOLING_EXP));
                        }

                        int roll = (int)(rng() % 100);
                        ll before = cur;
                        log_.clear(); refill.clear();

                        if (roll < SP_OP1_PROB_CUTOFF) {
                            int target_req = -1;
                            size_t dual_lim = max<size_t>(1, min<size_t>(reqs_by_dual.size(), max<size_t>(16, reqs_by_dual.size() / 4)));
                            for (int t = 0; t < 10; ++t) {
                                int cand_r = reqs_by_dual[rng() % dual_lim];
                                if (owner[cand_r] < 0 && !req_cols[cand_r].empty()) { target_req = cand_r; break; }
                            }
                            if (target_req != -1) {
                                const auto& cc = req_cols[target_req];
                                int j = cc[rng() % min<size_t>(cc.size(), 8)];
                                if (j == chosen[cols[j].k]) continue;
                                apply_col_evict(cols[j].k, j);
                            } else {
                                int k = busy[rng() % busy.size()];
                                const auto& tc = tcols_rc[k];
                                int j = tc[rng() % min<size_t>(tc.size(), 16)];
                                if (j == chosen[k]) continue;
                                apply_col_evict(k, j);
                            }
                        } else if (roll < SP_OP2_PROB_CUTOFF) {
                            int k = busy[rng() % busy.size()];
                            const auto& tc = tcols_rc[k];
                            size_t sample_lim = (rng() % 100 < 85) ? min<size_t>(tc.size(), 12) : min<size_t>(tc.size(), 32);
                            int j = tc[rng() % sample_lim];
                            if (rng() % 24 == 0) j = -1;
                            if (j == chosen[k]) continue;
                            apply_col_evict(k, j);
                        } else {
                            int k = busy[rng() % busy.size()];
                            const auto& tc = tcols_rc[k];
                            int j = tc[rng() % min<size_t>(tc.size(), 12)];
                            if (j == chosen[k]) continue;

                            int evict_k2 = -1; bool multi_evict = false;
                            for (int i = 0; i < cols[j].rlen; ++i) {
                                int c = owner[col_req[cols[j].rbeg + i]];
                                if (c >= 0) {
                                    int ko = cols[c].k;
                                    if (ko == k) continue;
                                    if (evict_k2 == -1) evict_k2 = ko;
                                    else if (evict_k2 != ko) { multi_evict = true; break; }
                                }
                            }

                            if (!multi_evict && evict_k2 != -1) {
                                set_col(k, -1);
                                set_col(evict_k2, -1);
                                set_col(k, j);

                                int best_c2 = -1, best_c3 = -1, best_k3 = -1;
                                ll best_chain_p = -INFLL;
                                const auto& tc2 = tcols_rc[evict_k2];
                                for (size_t idx2 = 0; idx2 < min<size_t>(tc2.size(), 10); ++idx2) {
                                    int c2 = tc2[idx2];
                                    if (is_free(c2)) {
                                        ll cp = cols[j].profit + cols[c2].profit;
                                        if (cp > best_chain_p) { best_chain_p = cp; best_c2 = c2; best_k3 = -1; best_c3 = -1; }
                                        break;
                                    }
                                    int evict_k3 = -1; bool c2_multi = false;
                                    for (int i = 0; i < cols[c2].rlen; ++i) {
                                        int c = owner[col_req[cols[c2].rbeg + i]];
                                        if (c >= 0) {
                                            int ko = cols[c].k;
                                            if (ko == k) { c2_multi = true; break; }
                                            if (evict_k3 == -1) evict_k3 = ko;
                                            else if (evict_k3 != ko) { c2_multi = true; break; }
                                        }
                                    }
                                    if (!c2_multi && evict_k3 >= 1 && evict_k3 != evict_k2 && evict_k3 != k) {
                                        int old_c3 = chosen[evict_k3];
                                        unassign(evict_k3);
                                        assign(evict_k2, c2);
                                        int free_c3 = -1;
                                        for (int cc3 : tcols_rc[evict_k3]) if (is_free(cc3)) { free_c3 = cc3; break; }
                                        ll cp = cols[j].profit + cols[c2].profit + (free_c3 >= 0 ? cols[free_c3].profit : 0);
                                        if (cp > best_chain_p) { best_chain_p = cp; best_c2 = c2; best_k3 = evict_k3; best_c3 = free_c3; }
                                        unassign(evict_k2);
                                        assign(evict_k3, old_c3);
                                    }
                                }
                                if (best_c2 >= 0) {
                                    if (best_k3 >= 1) set_col(best_k3, -1);
                                    set_col(evict_k2, best_c2);
                                    if (best_c3 >= 0) set_col(best_k3, best_c3);
                                }
                            } else {
                                apply_col_evict(k, j);
                            }
                        }

                        ll delta = cur - before;
                        if (delta >= 0 || (double)delta > temp * log(rand01() + 1e-300)) {
                            sweep_free_vehicles();
                            if (cur > wave_best) {
                                wave_best = cur;
                                w_last_impr = chrono::duration<double>(chrono::steady_clock::now() - w_start).count();
                            }
                            if (cur > best) {
                                best = cur; best_choice = chosen; global_impr_this_wave = true;
                                if (best >= ub) break;
                            }
                        } else {
                            for (int i = (int)log_.size() - 1; i >= 0; --i) {
                                unassign(log_[i].first);
                                assign(log_[i].first, log_[i].second);
                            }
                        }
                    }

                    if (global_impr_this_wave) waves_no_impr = 0;
                    else if (++waves_no_impr >= P1_MAX_STAGNANT_WAVES) break;
                }
            }
        }
    } else if (VERBOSE_LOG) {
        fprintf(stderr, "[P1: ENUM] cols=%d rounds=%d mem=%.1fMB partial=1 enum_t=%.3fs | greedy=%lld -> handoff to Phase 2\n",
                NC, enum_rounds, enum_bytes() / (1024.0 * 1024.0), enum_sec, best);
    }

    for (int k = 1; k <= K; ++k) {
        best_nd[k].assign(1, 0);
        int j = best_choice[k];
        if (j >= 0) best_nd[k].insert(best_nd[k].end(), col_nodes.begin() + cols[j].nbeg, col_nodes.begin() + cols[j].nbeg + cols[j].nlen);
    }

    bool is_proven_optimal = (best >= ub || lagr_exact) && !enum_partial;
    if (VERBOSE_LOG) {
        double p1_total = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
        fprintf(stderr, "[P1: DONE] best=%lld UB=%lld proven=%d p1_time=%.3fs\n",
                best, ub, (int)is_proven_optimal, p1_total);
    }
    release_cols();
    return is_proven_optimal;
}

// ============================================================================
// 6. PHASE 2: ELITE GM-SISR (mask=896) + EJECT50 + O1 RELOCATE POLISH
// ============================================================================
static ll best_insert_on(int k, const Route& r, int q, int& bi, int& bj, bool blink) {
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
            if (dc < best && !(blink && (rng() & BLINK_MASK) == 0)) { best = dc; bi = i; bj = i; }
        }
        return best;
    }

    if (nw[q] > Q[k]) return INFLL;
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

static inline ll best_insert(int k, int q, int& bi, int& bj, bool blink) {
    return best_insert_on(k, rt[k], q, bi, bj, blink);
}

static vector<int> touch_stamp, ruin_stamp, cand_stamp, touched, removed, cand, tmp_reqs;
static int stamp_id = 0;
static vector<pair<int, int>> veh_log;
static ll rev_delta = 0;
static Route tmp_r;
static long long st_eject_moves = 0, st_eject_chains = 0, st_ls_moves = 0, st_ls_gain = 0;

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
        ema_regret[q] = REGRET_EMA_DECAY * ema_regret[q] + (1.0 - REGRET_EMA_DECAY) * (double)(g1 - g2);

        if (bk != -1 && Rev[q] - best1 > 0) {
            do_insert(bk, q, bi, bj);
        }
    }
}

static bool build_route_without(int k, int drop_q, Route& dst) {
    const auto& src = rt[k].nd;
    dst.nd.clear();
    dst.nd.reserve(src.size());
    dst.nd.push_back(0);
    for (int i = 1; i < (int)src.size(); ++i) {
        int x = src[i], qx = (x > R) ? (x - R) : x;
        if (qx != drop_q) dst.nd.push_back(x);
    }
    return recompute_route(k, dst);
}

// Bounded Depth <= 2 Ejection-Chain Recreate (Eject50)
static void run_ejection_pass(const vector<int>& blocked, int max_probes) {
    if (blocked.empty()) return;
    static vector<int> pool;
    pool.clear();
    for (int q : blocked) if (veh[q] == 0 && servable[q]) pool.push_back(q);
    if (pool.empty()) return;

    int start = 0;
    if (priority_head != -1) {
        for (size_t i = 0; i < pool.size(); ++i) {
            if (pool[i] == priority_head) { swap(pool[0], pool[i]); start = 1; break; }
        }
    }
    int limit = min((int)pool.size(), max_probes);
    if ((int)pool.size() - start > 1) {
        static vector<pair<double, int>> bscored;
        bscored.resize((int)pool.size() - start);
        for (int i = start; i < (int)pool.size(); ++i) {
            int q = pool[i];
            bscored[i - start] = { -(double)(Rev[q] - nic[q]) * (0.85 + 0.30 * rand01()), q };
        }
        int k_top = min(max(0, limit - start), (int)bscored.size());
        if (k_top > 0) {
            partial_sort(bscored.begin(), bscored.begin() + k_top, bscored.end());
            for (int i = 0; i < k_top; ++i) pool[start + i] = bscored[i].second;
        }
    }

    int ejects_done = 0;
    for (int pi = 0; pi < limit && ejects_done < EJECT_MAX_MOVES; ++pi) {
        int q = pool[pi];
        if (veh[q] != 0) continue;

        ll best_delta = 0;
        int best_k = -1, best_q_old = -1, best_bi = 0, best_bj = 0;
        int best_k2 = -1, best_bi2 = 0, best_bj2 = 0;
        bool best_is_chain = false;

        int tried_nbr = 0;
        const int nlim = min(EJECT_NBR_SCAN, (int)nbr[q].size());
        for (int idx = 0; idx < nlim && tried_nbr < EJECT_NBR_TRIES; ++idx) {
            int q_old = nbr[q][idx];
            int k = veh[q_old];
            if (k == 0) continue;
            if (q > N && nw[q] > Q[k]) continue;
            ++tried_nbr;

            if (!build_route_without(k, q_old, tmp_r)) continue;
            int bi = 0, bj = 0;
            ll dc1 = best_insert_on(k, tmp_r, q, bi, bj, false);
            if (dc1 >= INFLL) continue;

            ll d_cost_k = (tmp_r.cost + dc1) - rt[k].cost;
            ll d_direct = ((ll)Rev[q] - (ll)Rev[q_old]) - d_cost_k;
            if (d_direct > best_delta) {
                best_delta = d_direct;
                best_k = k; best_q_old = q_old; best_bi = bi; best_bj = bj;
                best_k2 = -1; best_is_chain = false;
            }

            ll best_dc2 = INFLL;
            int bk2 = -1, bi2 = 0, bj2 = 0;
            CacheEntry* ce_old = &icache[(size_t)q_old * (K + 1)];
            for (int k2 = 1; k2 <= K; ++k2) {
                if (k2 == k) continue;
                CacheEntry& e = ce_old[k2];
                if (e.ver != rver[k2]) { e.cost = best_insert(k2, q_old, e.i, e.j, false); e.ver = rver[k2]; }
                if (e.cost < best_dc2) { best_dc2 = e.cost; bk2 = k2; bi2 = e.i; bj2 = e.j; }
            }
            if (bk2 != -1 && best_dc2 < INFLL) {
                ll d_chain = (ll)Rev[q] - d_cost_k - best_dc2;
                if (d_chain > best_delta) {
                    best_delta = d_chain;
                    best_k = k; best_q_old = q_old; best_bi = bi; best_bj = bj;
                    best_k2 = bk2; best_bi2 = bi2; best_bj2 = bj2; best_is_chain = true;
                }
            }
        }

        if (best_k != -1 && best_delta > 0 && build_route_without(best_k, best_q_old, tmp_r)) {
            touch(best_k);
            rt[best_k].nd = tmp_r.nd;
            if (q <= N) rt[best_k].nd.insert(rt[best_k].nd.begin() + best_bi + 1, q);
            else {
                rt[best_k].nd.insert(rt[best_k].nd.begin() + best_bj + 1, R + q);
                rt[best_k].nd.insert(rt[best_k].nd.begin() + best_bi + 1, q);
            }
            recompute(best_k);
            set_veh(best_q_old, 0);
            set_veh(q, best_k);
            ++st_eject_moves;
            ++ejects_done;

            if (best_is_chain && best_k2 != -1) {
                do_insert(best_k2, best_q_old, best_bi2, best_bj2);
                ++st_eject_chains;
            }
        }
    }
}

// Side-effect-free feasibility & cost check for O1 relocate polish
static ll cost_of_seq(int k, const vector<int>& nd) {
    ll dep = 0, ld = 0, cost = 0;
    int prev = O[k];
    for (size_t i = 1; i < nd.size(); ++i) {
        int x = nd[i];
        ll arr = dep + T(prev, nin[x]);
        if (arr > nl[x]) return INFLL;
        dep = max(arr, ne[x]) + ndur[x];
        ld += nw[x];
        if (ld > Q[k]) return INFLL;
        cost += C(prev, nin[x]) + nic[x];
        prev = nout[x];
    }
    return cost;
}

// Exact Cross-Route (and Intra-Route) Relocate Polish (O1, monotone; post-SA accept only)
static bool try_relocate_one(int q, ll& improvement) {
    int a = veh[q];
    if (!a) return false;
    Route& ra = rt[a];
    static vector<int> nd_wo;
    nd_wo.clear(); nd_wo.push_back(0);
    for (size_t i = 1; i < ra.nd.size(); ++i) {
        int x = ra.nd[i], rq = (x > R) ? (x - R) : x;
        if (rq != q) nd_wo.push_back(x);
    }
    if (nd_wo.size() == ra.nd.size()) return false;
    ll c_wo = cost_of_seq(a, nd_wo);
    if (c_wo >= INFLL) return false;
    ll save = ra.cost - c_wo;

    Route old_ra = ra;
    ra.nd = nd_wo;
    if (!recompute(a)) { ra = std::move(old_ra); rver[a] = ++gver; return false; }
    veh[q] = 0;

    ll best = INFLL; int bk = -1, bi = 0, bj = 0;
    for (int k = 1; k <= K; ++k) {
        int ci = 0, cj = 0;
        ll c = best_insert(k, q, ci, cj, false);
        if (c < best) { best = c; bk = k; bi = ci; bj = cj; }
    }

    if (bk != -1 && best < save) {
        auto& nd = rt[bk].nd;
        if (q <= N) nd.insert(nd.begin() + bi + 1, q);
        else { nd.insert(nd.begin() + bj + 1, R + q); nd.insert(nd.begin() + bi + 1, q); }
        recompute(bk);
        veh[q] = bk;
        improvement = save - best;
        ++st_ls_moves;
        return true;
    }
    ra = std::move(old_ra);
    rver[a] = ++gver;
    veh[q] = a;
    return false;
}

static ll ls_relocate_polish(const vector<int>& pool, int max_steps) {
    ll total = 0; int steps = 0;
    for (int q : pool) {
        if (steps >= max_steps) break;
        if (!veh[q]) continue;
        ll imp = 0;
        if (try_relocate_one(q, imp)) { total += imp; ++steps; }
    }
    return total;
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

static inline int select_seed() { return servable_list[rng() % servable_list.size()]; }

static void update_bandit() {
    double y[4] = {0.0, 0.0, 0.0, 0.0};
    for (int m = 1; m <= 3; ++m)
        y[m] = (double)(mode_records[m] + 1) / (double)(mode_tries[m] + 200);
    double ymean = (y[1] + y[2] + y[3]) / 3.0;
    for (int m = 1; m <= 3; ++m) {
        double rel = (ymean > 0.0) ? (y[m] / ymean) : 1.0;
        bandit_w[m] = BANDIT_PRIOR[m] * clamp(0.5 + 0.5 * rel, 0.5, 2.0);
    }
}

static int ruin_guided(int seed) {
    removed.clear();
    priority_head = -1;
    int tot_nodes = 0, busy = 0;
    for (int k = 1; k <= K; ++k) if (rt[k].n()) { tot_nodes += rt[k].n(); ++busy; }
    if (!busy) return 1;

    double ls_max = min((double)L_MAX, (double)tot_nodes / busy);
    double ks_max = 4.0 * C_AVG / (1.0 + ls_max) - 1.0;
    int ks = max(1, (int)(rand01() * max(1.0, ks_max)) + 1), done = 0;

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

    int roll;
    {
        double bw1 = bandit_w[1], bw2 = bandit_w[2], bw3 = bandit_w[3];
        double r = rand01() * (bw1 + bw2 + bw3);
        if ((r -= bw1) < 0) roll = 0; else if ((r -= bw2) < 0) roll = 75; else roll = 90;
    }
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

    try_route(seed);
    for (int c : nbr[seed]) { if (done >= ks) break; try_route(c); }
    return 1;
}

static int order_candidates(vector<int>& cands) {
    if (cands.size() <= 1) return 0;
    int start_idx = (priority_head != -1 && cands[0] == priority_head) ? 1 : 0;
    int sub_n = (int)cands.size() - start_idx;
    if (sub_n <= 1) return 0;

    int sroll = (int)(rng() % 100);
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

// SEDCA-V4 Thermostat (Exponential-Tooth Reheat + Online Micro-Recalibration)
struct AdaptiveThermostat {
    double t_max = 60.0;
    const double tau_fast = P2_TAU_FAST;
    const double tau_quench = P2_TAU_QUENCH;

    double T_0 = 25.0, T_work = 4.0, T_min = 0.6, micro_delta = 5.0;
    double T_peak_base = 25.0, T_min_base = 0.6;
    int stag = 0;
    bool calibrated = false;

    bool in_cycle_mode = false;
    double cycle_start_tau = 0.0, cycle_duration_tau = 0.0;
    double cycle_peak_T = 0.0, cycle_base_T = 0.0;
    int cycle_count = 0;

    void apply_deltas(const vector<double>& deltas, double scale_S, bool init_mode) {
        vector<double> valid;
        valid.reserve(deltas.size());
        for (double d : deltas) if (d > 0.0 && d <= 0.80 * scale_S) valid.push_back(d);
        if (init_mode && valid.size() < 20) valid = deltas;
        if (!init_mode && valid.size() < 20) return;

        if (valid.empty()) micro_delta = max(1.0, 0.04 * scale_S);
        else {
            sort(valid.begin(), valid.end());
            double p15 = valid[(valid.size() * 15) / 100];
            micro_delta = clamp(p15, 1.0, max(2.0, 0.06 * scale_S));
        }

        T_min  = max(0.2, micro_delta / 6.907);
        T_work = max(T_min * 3.0, micro_delta / 1.05);
        T_0    = T_work * 5.5;
        T_min_base = T_min;
        T_peak_base = max(2.0 * T_min, 0.65 * T_0);
        if (init_mode) calibrated = true;
    }

    void start_new_cycle(double current_tau) {
        in_cycle_mode = true;
        cycle_start_tau = current_tau;
        ++cycle_count;

        double base_span = tau_quench - tau_fast;
        double remaining_osc_tau = tau_quench - current_tau;
        double stag_factor = 1.0 / (1.0 + 0.7 * min(3, stag));
        cycle_peak_T = max(T_peak_base, 2.0 * T_min_base);
        cycle_base_T = T_min_base;
        double target_duration = base_span * P2_CYCLE_SPAN_FRAC * stag_factor;
        cycle_duration_tau = min(target_duration, max(P2_MIN_CYCLE_TAU, remaining_osc_tau));
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
        double T = cycle_peak_T * pow(cycle_base_T / cycle_peak_T, phi_c);
        return { max(T, 0.2), triggered_reheat };
    }
};

// ============================================================================
// 7. MAIN ENGINE (JUDGE & BENCHMARK ENTRYPOINT)
// ============================================================================
int main(int argc, char** argv) {
    ios_base::sync_with_stdio(false); cin.tie(NULL);
    auto t_start = chrono::steady_clock::now();

    vector<string> pos_args;
    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        if (a == "--verbose" || a == "-v") VERBOSE_LOG = true;
        else pos_args.push_back(a);
    }
    if (pos_args.size() >= 1) TIME_LIMIT_SEC = max(0.1, atof(pos_args[0].c_str()));
    if (pos_args.size() >= 2 && pos_args[1] != "-") {
        if (!freopen(pos_args[1].c_str(), "r", stdin)) {
            if (VERBOSE_LOG) fprintf(stderr, "ERROR: Failed to open %s\n", pos_args[1].c_str());
            return 1;
        }
    }
    if (pos_args.size() >= 3) {
        long long seed_val = atoll(pos_args[2].c_str());
        rng_s = 0x9E3779B97F4A7C15ull ^ ((uint64_t)seed_val * 0xBF58476D1CE4E5B9ull + 0x94D049BB133111EBull);
        if (rng_s == 0) rng_s = 0x9E3779B97F4A7C15ull;
        for (int w = 0; w < 16; ++w) rng();
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
    touch_stamp.assign(K + 1, 0); ruin_stamp.assign(K + 1, 0); cand_stamp.assign(R + 1, 0);

    // Precompute Phase 2 Greedy (~1-2ms) so Phase 1 can skip SP-SA when partial columns lag behind
    double sum_net = 0.0;
    ema_regret.assign(R + 1, 0.0);
    for (int q : servable_list) {
        double net = max(1.0, (double)(Rev[q] - nic[q]));
        ema_regret[q] = net;
        sum_net += net;
    }
    S_scale = max(1.0, sum_net / servable_list.size());

    for (int k = 1; k <= K; ++k) { rt[k].nd.assign(1, 0); recompute(k); }
    vector<int> greedy_init_ord = servable_list;
    sort(greedy_init_ord.begin(), greedy_init_ord.end(), [&](int a, int b) { return Rev[a] - nic[a] > Rev[b] - nic[b]; });
    ++stamp_id; rev_delta = 0;
    recreate_with_memory(greedy_init_ord, false);
    ll greedy_cost = 0; for (int k = 1; k <= K; ++k) greedy_cost += rt[k].cost;
    ll greedy_obj = rev_delta - greedy_cost;
    vector<vector<int>> greedy_nd(K + 1);
    for (int k = 1; k <= K; ++k) greedy_nd[k] = rt[k].nd;

    // ------------------------------------------------------------------------
    // PHASE 1: BALANCED COLUMN ENUMERATION + DUAL-GUIDED MULTI-WAVE SP-SA
    // ------------------------------------------------------------------------
    bool p1_proven_optimal = column_solve(servable_list, t_start, greedy_obj);

    long long total_iters = 0;
    if (!p1_proven_optimal) {
        // --------------------------------------------------------------------
        // PHASE 2: GM-SISR (mask=896) + EJECT50 + O1 CROSS-ROUTE POLISH
        // --------------------------------------------------------------------
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

        // Compare Phase 1 solution cleanly against precomputed Phase 2 Greedy
        vector<vector<int>> p1_nd = best_nd;
        ll p1_obj = 0;
        bool p1_valid = true;
        for (int k = 1; k <= K; ++k) {
            rt[k].nd = p1_nd[k];
            if (!recompute(k)) { p1_valid = false; break; }
            p1_obj -= rt[k].cost;
            for (int i = 1; i <= rt[k].n(); ++i) { int x = rt[k].nd[i]; if (x <= R) p1_obj += Rev[x]; }
        }

        const vector<vector<int>>& start_nd = (p1_valid && p1_obj >= greedy_obj) ? p1_nd : greedy_nd;
        fill(veh.begin(), veh.end(), 0);
        for (int k = 1; k <= K; ++k) {
            rt[k].nd = start_nd[k]; recompute(k);
            for (int i = 1; i <= rt[k].n(); ++i) { int x = rt[k].nd[i]; if (x <= R) veh[x] = k; }
        }
        cur_obj = (p1_valid && p1_obj >= greedy_obj) ? p1_obj : greedy_obj;
        save_best();
        if (VERBOSE_LOG) {
            fprintf(stderr, "[P2: INIT] p1_obj=%lld (valid=%d) greedy_obj=%lld -> start=%lld\n",
                    p1_obj, (int)p1_valid, greedy_obj, best_obj);
        }

        double elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
        const double search_start = elapsed;
        const double budget = max(0.001, TIME_LIMIT_SEC - search_start);

        AdaptiveThermostat sedca;
        sedca.t_max = budget;
        vector<double> warmup_worsening;
        warmup_worsening.reserve(max(256, P2_WARMUP_DELTAS));
        const int WARMUP_COUNT = P2_WARMUP_DELTAS;

        double temp = 0.15 * S_scale;
        int cycle_stag = 0;
        bool improved_in_cycle = false;
        double recent_delta[128] = {0}; int recent_pos = 0, recent_filled = 0;
        static vector<int> ls_pool;

        for (long long it = 0;; ++it) {
            if ((it & 15) == 0) {
                elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
                if (elapsed >= TIME_LIMIT_SEC) break;
                double t = max(0.0, elapsed - search_start);

                if (sedca.calibrated) {
                    auto [new_temp, triggered_reheat] = sedca.get_temperature(t);
                    temp = new_temp;
                    if (triggered_reheat) {
                        if (improved_in_cycle) cycle_stag = 0;
                        else ++cycle_stag;
                        load_best();
                        improved_in_cycle = false;
                    }
                    sedca.stag = cycle_stag;
                } else {
                    temp = 0.08 * S_scale;
                }
            }
            ++total_iters;

            ++stamp_id; touched.clear(); veh_log.clear(); rev_delta = 0;

            int seed = select_seed();
            int r_mode = ruin_guided(seed);
            mode_tries[r_mode]++;

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

            // Eject50: Depth <= 2 Ejection-Chain Recreate on 50% of iterations
            if ((it & EJECT_ITER_MASK) == 0) run_ejection_pass(cand, EJECT_PROBES);

            ll dcost = 0;
            for (int k : touched) dcost += rt[k].cost - backup[k].cost;
            ll new_obj = cur_obj + rev_delta - dcost;
            ll delta = new_obj - cur_obj;

            bool accepted = (delta >= 0 || (double)delta > temp * log(rand01() + 1e-300));
            if (accepted) {
                cur_obj = new_obj;
                if (cur_obj > best_obj) {
                    save_best();
                    improved_in_cycle = true;
                    mode_records[r_mode]++;
                }
                // O1: Bounded exact cross-route relocate polish after accepted moves
                if (!touched.empty() && (it & O1_ITER_MASK) == 0) {
                    ls_pool.clear();
                    for (int k : touched) {
                        for (int i = 1; i <= rt[k].n() && (int)ls_pool.size() < O1_POOL_CAP; ++i) {
                            int x = rt[k].nd[i];
                            ls_pool.push_back((x > R) ? (x - R) : x);
                        }
                        if ((int)ls_pool.size() >= O1_POOL_CAP) break;
                    }
                    if (!ls_pool.empty()) {
                        sort(ls_pool.begin(), ls_pool.end());
                        ls_pool.erase(unique(ls_pool.begin(), ls_pool.end()), ls_pool.end());
                        ll gain = ls_relocate_polish(ls_pool, O1_MAX_STEPS);
                        if (gain > 0) {
                            cur_obj += gain;
                            st_ls_gain += gain;
                            if (cur_obj > best_obj) { save_best(); improved_in_cycle = true; }
                        }
                    }
                }
            } else {
                for (int k : touched) { swap(rt[k], backup[k]); rver[k] = ++gver; }
                for (int i = (int)veh_log.size() - 1; i >= 0; --i) veh[veh_log[i].first] = veh_log[i].second;
            }

            if (delta < 0) {
                recent_delta[recent_pos] = (double)llabs(delta);
                recent_pos = (recent_pos + 1) & 127;
                if (recent_filled < 128) ++recent_filled;
            }
            if (!sedca.calibrated && delta < 0) {
                warmup_worsening.push_back((double)llabs(delta));
                if ((int)warmup_worsening.size() >= WARMUP_COUNT) {
                    sedca.apply_deltas(warmup_worsening, S_scale, true);
                }
            }
            if (sedca.calibrated && recent_filled >= 64 && it > 0 && (it % RECALIB_UPDATE_ITERS == 0)) {
                vector<double> rec(recent_delta, recent_delta + recent_filled);
                sedca.apply_deltas(rec, S_scale, false);
            }
            if (it > 0 && (it % BANDIT_UPDATE_ITERS == 0)) update_bandit();
        }
    }

    // ------------------------------------------------------------------------
    // FINAL INDEPENDENT VERIFICATION & STANDARD JUDGE OUTPUT
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

    if (VERBOSE_LOG) {
        double tot_t = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
        fprintf(stderr, "RESULT: obj=%lld p2_iters=%lld eject_mv=%lld eject_ch=%lld ls_mv=%lld ls_gain=%lld time=%.3fs\n",
                benefit, total_iters, st_eject_moves, st_eject_chains, st_ls_moves, st_ls_gain, tot_t);
    }

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
