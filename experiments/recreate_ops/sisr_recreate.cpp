#include <bits/stdc++.h>
using namespace std;
using ll = long long;

// ============================================================================
// NON-SEQUENTIAL RECREATE EXPERIMENTS ON CLEAN PHASE-2 SISR-SA ENGINE
// ============================================================================
// CLI: [time_sec] [testcase] [seed] [max_iters] [mode]
// Modes:
//   0: Baseline clean SISR-SA (sequential recreate_with_memory, identical to submission Phase 2)
//   1: Hybrid Wave Bipartite Matching (30% Wave Matching + 70% sequential SISR)
//   2: Ejection-Chain Recreate (Insert-with-Evict depth-2 on blocked candidates)
//   3: Combined (Hybrid Wave Matching + Ejection-Chain Recreate)
//   4: Pure Wave Bipartite Matching (100% Wave Matching recreate)

static double TIME_LIMIT_SEC = 6.0;
static long long MAX_ITERS = 0;
static int MODE = 0;
static double TRACE_STEP_SEC = 0.0;

static const int NB_SIZE = 60;
static const double C_AVG = 10.0;
static const int L_MAX = 10;
static int CAND_NB = 3;
static int CAND_RAND = 10;
static unsigned BLINK_MASK = 31;
static const ll INFLL = (ll)4e18;

static const double BANDIT_PRIOR[4] = {0.0, 7.5, 1.5, 1.0};
static double bandit_w[5] = {0.0, 7.5, 1.5, 1.0, 1.0};
static long long mode_tries[5] = {0}, mode_records[5] = {0};
static const long long BANDIT_UPDATE_ITERS = 20000;
static const int RECALIB_UPDATE_ITERS = 20000;

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
static inline int rand_int(int lo, int hi) { return (lo >= hi) ? lo : lo + (int)(rng() % (uint64_t)(hi - lo + 1)); }
static inline double rand01() { return (rng() >> 11) * (1.0 / 9007199254740992.0); }

static int K, N, M, V, R, SV;
static vector<int> O, Q, Rev, Tm, Cm;
static inline int T(int u, int v) { return Tm[u * SV + v]; }
static inline int C(int u, int v) { return Cm[u * SV + v]; }

static vector<int> nin, nout, nw, nic, servable_list;
static vector<ll> ne, nl, ndur;
static vector<char> servable;
static vector<vector<int>> nbr;
static double S_scale = 100.0;

struct Route {
    vector<int> nd;
    vector<ll> dep;
    vector<ll> lat;
    vector<int> ld;
    ll cost = 0;
    int n() const { return (int)nd.size() - 1; }
};

static vector<Route> rt, backup;
static Route tmp_r;
static vector<int> rver, veh;
static int gver = 0;
struct CacheEntry { int ver = -1, i = 0, j = 0; ll cost = 0; };
static vector<CacheEntry> icache;
static ll cur_obj = 0, best_obj = 0;
static vector<vector<int>> best_nd;

static vector<double> ema_regret;
static int priority_head = -1;

// Telemetry counters
static long long st_wave_calls = 0, st_wave_recs = 0, st_wave_ins = 0;
static long long st_eject_calls = 0, st_eject_moves = 0, st_eject_chains = 0, st_eject_recs = 0;

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
    rver[k] = ++gver;
    return recompute_route(k, rt[k]);
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

// Standard Sequential Recreate (baseline)
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

// ============================================================================
// TECHNIQUE 1: WAVE BIPARTITE MATCHING RECREATE (Hungarian per wave)
// ============================================================================
static void recreate_wave_matching(const vector<int>& cands, bool blink) {
    ++st_wave_calls;
    static vector<int> rem;
    rem.clear();
    for (int q : cands) if (veh[q] == 0 && servable[q]) rem.push_back(q);
    if (rem.empty()) return;

    // Cap Hungarian batch size at 36 for bounded O(m^2 (K+m)) speed; overflow falls through to standard recreate
    static vector<int> overflow;
    overflow.clear();
    if ((int)rem.size() > 36) {
        overflow.assign(rem.begin() + 36, rem.end());
        rem.resize(36);
    }

    static const ll INF_MATCH = 100000000000000LL; // 1e14
    static const ll INF_HUNG  = 10000000000000000LL; // 1e16
    static vector<ll> mat, u, v, minv;
    static vector<int> p, way;
    static vector<char> used;

    for (int wave = 0; !rem.empty(); ++wave) {
        int m = (int)rem.size();
        int cols = K + m;
        int stride = cols + 1;
        mat.assign((size_t)(m + 1) * stride, INF_MATCH);

        bool any_pos = false;
        for (int i = 1; i <= m; ++i) {
            int q = rem[i - 1];
            ll best1 = INFLL, best2 = INFLL;
            CacheEntry* ce = &icache[(size_t)q * (K + 1)];
            for (int k = 1; k <= K; ++k) {
                CacheEntry& e = ce[k];
                if (e.ver != rver[k]) {
                    // Use non-blinked exact cost inside matching + cache!
                    e.cost = best_insert(k, q, e.i, e.j, false);
                    e.ver = rver[k];
                }
                if (e.cost < best1) { best2 = best1; best1 = e.cost; }
                else if (e.cost < best2) { best2 = e.cost; }

                if (e.cost < INFLL) {
                    ll gain = (ll)Rev[q] - e.cost;
                    if (gain > 0) {
                        if (blink && (rng() & BLINK_MASK) == 0) continue; // Christiaens-style stochastic edge blink
                        mat[(size_t)i * stride + k] = -gain;
                        any_pos = true;
                    }
                }
            }
            if (wave == 0) {
                ll g1 = (best1 < INFLL) ? max(0LL, (ll)Rev[q] - best1) : 0LL;
                ll g2 = (best2 < INFLL) ? max(0LL, (ll)Rev[q] - best2) : 0LL;
                ema_regret[q] = 0.80 * ema_regret[q] + 0.20 * (double)(g1 - g2);
            }
            mat[(size_t)i * stride + (K + i)] = 0; // private dummy unserved option
        }

        if (!any_pos) break;

        u.assign(m + 1, 0);
        v.assign(cols + 1, 0);
        p.assign(cols + 1, 0);
        way.assign(cols + 1, 0);

        for (int i = 1; i <= m; ++i) {
            p[0] = i;
            int j0 = 0;
            minv.assign(cols + 1, INF_HUNG);
            used.assign(cols + 1, 0);
            do {
                used[j0] = 1;
                int i0 = p[j0], j1 = 0;
                ll delta = INF_HUNG;
                const ll* row = &mat[(size_t)i0 * stride];
                for (int j = 1; j <= cols; ++j) {
                    if (!used[j]) {
                        ll cur = row[j] - u[i0] - v[j];
                        if (cur < minv[j]) { minv[j] = cur; way[j] = j0; }
                        if (minv[j] < delta) { delta = minv[j]; j1 = j; }
                    }
                }
                for (int j = 0; j <= cols; ++j) {
                    if (used[j]) { u[p[j]] += delta; v[j] -= delta; }
                    else minv[j] -= delta;
                }
                j0 = j1;
            } while (p[j0] != 0);
            do {
                int j1 = way[j0];
                p[j0] = p[j1];
                j0 = j1;
            } while (j0);
        }

        int matched = 0;
        for (int k = 1; k <= K; ++k) {
            int i = p[k];
            if (i >= 1 && i <= m && mat[(size_t)i * stride + k] < 0) {
                int q = rem[i - 1];
                const CacheEntry& e = icache[(size_t)q * (K + 1) + k];
                do_insert(k, q, e.i, e.j);
                ++matched;
                ++st_wave_ins;
            }
        }
        if (matched == 0) break;

        // Compact remaining unmatched requests for next wave
        size_t w = 0;
        for (size_t idx = 0; idx < rem.size(); ++idx) {
            if (veh[rem[idx]] == 0) rem[w++] = rem[idx];
        }
        rem.resize(w);
    }

    if (!overflow.empty()) recreate_with_memory(overflow, blink);
}

// ============================================================================
// TECHNIQUE 2: EJECTION-CHAIN RECREATE (Insert-with-Evict, depth <= 2)
// ============================================================================
static void build_route_without(int k, int drop_q, Route& dst) {
    const auto& src = rt[k].nd;
    dst.nd.clear();
    dst.nd.reserve(src.size());
    dst.nd.push_back(0);
    for (int i = 1; i < (int)src.size(); ++i) {
        int x = src[i];
        int qx = (x > R) ? (x - R) : x;
        if (qx != drop_q) dst.nd.push_back(x);
    }
    recompute_route(k, dst);
}

static void run_ejection_pass(const vector<int>& blocked, int max_probes) {
    if (blocked.empty()) return;
    ++st_eject_calls;

    // Order blocked candidates: priority_head first, then mix of highest (Rev - nic)
    static vector<int> pool;
    pool.clear();
    for (int q : blocked) if (veh[q] == 0 && servable[q]) pool.push_back(q);
    if (pool.empty()) return;

    int start = 0;
    if (priority_head != -1) {
        for (size_t i = 0; i < pool.size(); ++i) {
            if (pool[i] == priority_head) {
                swap(pool[0], pool[i]);
                start = 1;
                break;
            }
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
        int k_top = min(limit - start, (int)bscored.size());
        partial_sort(bscored.begin(), bscored.begin() + k_top, bscored.end());
        for (int i = 0; i < k_top; ++i) pool[start + i] = bscored[i].second;
    }

    int ejects_done = 0;
    for (int pi = 0; pi < limit && ejects_done < 3; ++pi) {
        int q = pool[pi];
        if (veh[q] != 0) continue;

        ll best_delta = 0;
        int best_k = -1, best_q_old = -1, best_bi = 0, best_bj = 0;
        int best_k2 = -1, best_bi2 = 0, best_bj2 = 0;
        bool best_is_chain = false;

        int tried_nbr = 0;
        const int nlim = min(14, (int)nbr[q].size());
        for (int idx = 0; idx < nlim && tried_nbr < 7; ++idx) {
            int q_old = nbr[q][idx];
            int k = veh[q_old];
            if (k == 0) continue;
            if (q > N && nw[q] > Q[k]) continue;
            ++tried_nbr;

            build_route_without(k, q_old, tmp_r);
            int bi = 0, bj = 0;
            ll dc1 = best_insert_on(k, tmp_r, q, bi, bj, false);
            if (dc1 >= INFLL) continue;

            // Direct 1-for-1 replacement delta on route k
            ll d_cost_k = (tmp_r.cost + dc1) - rt[k].cost;
            ll d_direct = ((ll)Rev[q] - (ll)Rev[q_old]) - d_cost_k;
            if (d_direct > best_delta) {
                best_delta = d_direct;
                best_k = k; best_q_old = q_old; best_bi = bi; best_bj = bj;
                best_k2 = -1; best_is_chain = false;
            }

            // Depth-2 Ejection Chain: re-insert evicted q_old into another route k2 != k (all K routes via icache)
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

        if (best_k != -1 && best_delta > 0) {
            // Apply eviction + insertion cleanly using touch() + set_veh() for exact SA rollback
            build_route_without(best_k, best_q_old, tmp_r);
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
    for (int m = 1; m <= 3; ++m) y[m] = (double)(mode_records[m] + 1) / (double)(mode_tries[m] + 200);
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

struct AdaptiveThermostat {
    double t_max = 60.0;
    const double tau_fast = 0.10, tau_quench = 0.86;
    double T_0 = 25.0, T_work = 4.0, T_min = 0.6, micro_delta = 5.0;
    double T_peak_base = 25.0, T_min_base = 0.6;
    int stag = 0;
    bool calibrated = false;
    bool in_cycle_mode = false;
    double cycle_start_tau = 0.0, cycle_duration_tau = 0.0;
    double cycle_peak_T = 0.0, cycle_work_T = 0.0, cycle_base_T = 0.0;
    int cycle_count = 0;

    void auto_calibrate(const vector<double>& worsening_deltas, double scale_S) {
        vector<double> valid; valid.reserve(worsening_deltas.size());
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
        T_min_base = T_min;
        T_peak_base = max(2.0 * T_min, 0.65 * T_0);
        calibrated = true;
    }
    void online_recalibrate(const vector<double>& deltas, double scale_S) {
        vector<double> valid;
        for (double d : deltas) if (d > 0.0 && d <= 0.80 * scale_S) valid.push_back(d);
        if (valid.size() < 20) return;
        sort(valid.begin(), valid.end());
        double p15 = valid[(valid.size() * 15) / 100];
        micro_delta = clamp(p15, 1.0, max(2.0, 0.06 * scale_S));
        T_min  = max(0.2, micro_delta / 6.907);
        T_work = max(T_min * 3.0, micro_delta / 1.05);
        T_0    = T_work * 5.5;
        T_min_base = T_min;
        T_peak_base = max(2.0 * T_min, 0.65 * T_0);
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
        cycle_work_T = sqrt(cycle_peak_T * cycle_base_T);
        double target_duration = base_span * 0.12 * stag_factor;
        cycle_duration_tau = min(target_duration, max(0.03, remaining_osc_tau));
    }
    pair<double, bool> get_temperature_tau(double tau) {
        tau = clamp(tau, 0.0, 1.0);
        if (tau < tau_fast) {
            double p = tau / tau_fast;
            double T = (p < 0.4) ? (T_0 * pow(T_work / T_0, p / 0.4))
                                 : (T_work * pow(T_min / T_work, (p - 0.4) / 0.6));
            return { max(T, T_min), false };
        }
        if (tau >= tau_quench) {
            double quench_prog = clamp((tau - tau_quench) / max(0.001, 1.0 - tau_quench), 0.0, 1.0);
            double T = (0.7 * T_work) * pow((0.5 * T_min) / (0.7 * T_work), quench_prog);
            return { max(T, 0.2), false };
        }
        double phi_c = in_cycle_mode ? ((tau - cycle_start_tau) / max(0.0001, cycle_duration_tau)) : 1.0;
        bool triggered_reheat = false;
        if (!in_cycle_mode || phi_c >= 1.0) {
            start_new_cycle(tau);
            triggered_reheat = true;
            phi_c = 0.0;
        }
        phi_c = clamp(phi_c, 0.0, 1.0);
        double T = cycle_peak_T * pow(cycle_base_T / cycle_peak_T, phi_c);
        return { max(T, 0.2), triggered_reheat };
    }
    pair<double, bool> get_temperature(double elapsed_time) {
        return get_temperature_tau(elapsed_time / max(0.001, t_max));
    }
};

int main(int argc, char** argv) {
    ios_base::sync_with_stdio(false); cin.tie(NULL);
    auto t_start = chrono::steady_clock::now();

    if (argc > 1) TIME_LIMIT_SEC = atof(argv[1]);
    if (argc > 2) {
        if (!freopen(argv[2], "r", stdin)) { cerr << "ERROR: Failed to open " << argv[2] << "\n"; return 1; }
    }
    long long run_seed = 1;
    if (argc > 3) {
        run_seed = atoll(argv[3]);
        rng_s = 0x9E3779B97F4A7C15ull ^ ((uint64_t)run_seed * 0xBF58476D1CE4E5B9ull + 1ULL);
        if (rng_s == 0) rng_s = 1;
        for (int w = 0; w < 16; ++w) rng();
    }
    if (argc > 4) MAX_ITERS = atoll(argv[4]);
    if (argc > 5) MODE = atoi(argv[5]);
    if (argc > 6) TRACE_STEP_SEC = atof(argv[6]);

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

    int maxQ = K > 0 ? *max_element(Q.begin(), Q.end()) : 0;
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
        fprintf(stderr, "RESULT: obj=0 iters=0 speed=0 time=0.000\n");
        return 0;
    }

    best_nd.assign(K + 1, vector<int>(1, 0));
    rt.assign(K + 1, Route()); backup.assign(K + 1, Route());
    rver.assign(K + 1, 0); veh.assign(R + 1, 0);
    icache.assign((size_t)(R + 1) * (K + 1), CacheEntry());

    double sum_net = 0.0;
    ema_regret.assign(R + 1, 0.0);
    for (int q : servable_list) {
        double net = max(1.0, (double)(Rev[q] - nic[q]));
        ema_regret[q] = net;
        sum_net += net;
    }
    S_scale = max(1.0, sum_net / servable_list.size());

    double sum_T = 0.0, sum_E = 0.0; long long pairs = 0;
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

    // Initial Greedy construction
    for (int k = 1; k <= K; ++k) { rt[k].nd.assign(1, 0); recompute(k); }
    fill(veh.begin(), veh.end(), 0);
    vector<int> init = servable_list;
    sort(init.begin(), init.end(), [&](int a, int b) { return Rev[a] - nic[a] > Rev[b] - nic[b]; });
    ++stamp_id; rev_delta = 0;
    recreate_with_memory(init, false);
    ll initial_cost = 0; for (int k = 1; k <= K; ++k) initial_cost += rt[k].cost;
    cur_obj = rev_delta - initial_cost;
    save_best();
    ll init_obj = best_obj;

    double elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
    const double search_start = elapsed;
    const double budget = max(0.001, TIME_LIMIT_SEC - search_start);

    AdaptiveThermostat sedca;
    sedca.t_max = budget;
    vector<double> warmup_worsening;
    warmup_worsening.reserve(256);
    const int WARMUP_COUNT = 200;

    double temp = 0.15 * S_scale;
    int cycle_stag = 0;
    bool improved_in_cycle = false;
    double recent_delta[128] = {0}; int recent_pos = 0, recent_filled = 0;

    long long iters = 0;
    struct TracePt { double t; ll best; ll cur; double temp; };
    vector<TracePt> trace_pts;
    double next_trace_t = TRACE_STEP_SEC;
    if (TRACE_STEP_SEC > 0.0) {
        trace_pts.push_back({elapsed, best_obj, cur_obj, temp});
    }
    for (long long it = 0;; ++it) {
        if (MAX_ITERS > 0) { if (it >= MAX_ITERS) break; }
        else if ((it & 15) == 0) {
            elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
            if (TRACE_STEP_SEC > 0.0 && elapsed >= next_trace_t) {
                trace_pts.push_back({elapsed, best_obj, cur_obj, temp});
                while (next_trace_t <= elapsed) next_trace_t += TRACE_STEP_SEC;
            }
            if (elapsed >= TIME_LIMIT_SEC) break;
            double t = max(0.0, elapsed - search_start);
            if (sedca.calibrated) {
                auto [new_temp, triggered_reheat] = sedca.get_temperature(t);
                temp = new_temp;
                if (triggered_reheat) {
                    if (improved_in_cycle) { cycle_stag = 0; load_best(); }
                    else { cycle_stag++; load_best(); }
                    improved_in_cycle = false;
                }
                sedca.stag = cycle_stag;
            } else {
                temp = 0.08 * S_scale;
            }
        }
        ++iters;

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

        bool use_wave = false, use_eject = false;
        if (MODE == 1) use_wave = (rand01() < 0.30);
        else if (MODE == 4) use_wave = true;
        else if (MODE == 2) use_eject = true;
        else if (MODE == 3) { use_wave = (rand01() < 0.30); use_eject = true; }

        if (use_wave) {
            recreate_wave_matching(cand, true);
        } else {
            order_candidates(cand);
            recreate_with_memory(cand, true);
        }

        ll ej_before = st_eject_moves;
        if (use_eject && !cand.empty()) {
            // Collect blocked candidates and run fast bounded ejection chains
            static vector<int> blocked;
            blocked.clear();
            for (int q : cand) if (veh[q] == 0 && servable[q]) blocked.push_back(q);
            if (!blocked.empty() && rand01() < 0.50) {
                run_ejection_pass(blocked, 5);
            }
        }
        bool did_eject = (st_eject_moves > ej_before);

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
                if (use_wave) ++st_wave_recs;
                if (did_eject) ++st_eject_recs;
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
            if ((int)warmup_worsening.size() >= WARMUP_COUNT) sedca.auto_calibrate(warmup_worsening, S_scale);
        }
        if (sedca.calibrated && recent_filled >= 64 && it > 0 && (it % RECALIB_UPDATE_ITERS == 0)) {
            vector<double> rec(recent_delta, recent_delta + recent_filled);
            sedca.online_recalibrate(rec, S_scale);
        }
        if (it > 0 && (it % BANDIT_UPDATE_ITERS == 0)) update_bandit();
    }

    double total_wall = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();

    rt.assign(K + 1, Route());
    rver.assign(K + 1, 0);
    ll benefit = 0;
    int served_cnt = 0;
    for (int k = 1; k <= K; ++k) {
        rt[k].nd = best_nd[k];
        if (!recompute(k)) { rt[k].nd.assign(1, 0); recompute(k); }
        benefit -= rt[k].cost;
        for (int i = 1; i <= rt[k].n(); ++i) {
            int x = rt[k].nd[i];
            if (x <= R) { benefit += Rev[x]; ++served_cnt; }
        }
    }

    fprintf(stderr, "RESULT: obj=%lld iters=%lld speed=%.0f time=%.3f init=%lld served=%d/%d | WAVE: calls=%lld ins=%lld rec=%lld | EJECT: calls=%lld mv=%lld ch=%lld rec=%lld\n",
            benefit, iters, iters / max(0.001, total_wall), total_wall, init_obj, served_cnt, R,
            st_wave_calls, st_wave_ins, st_wave_recs,
            st_eject_calls, st_eject_moves, st_eject_chains, st_eject_recs);
    if (!trace_pts.empty()) {
        trace_pts.push_back({total_wall, benefit, cur_obj, temp});
        fprintf(stderr, "TRACE:");
        for (size_t i = 0; i < trace_pts.size(); ++i) {
            fprintf(stderr, "%s%.2f:%lld:%lld:%.3g", i == 0 ? " " : ",", trace_pts[i].t, trace_pts[i].best, trace_pts[i].cur, trace_pts[i].temp);
        }
        fprintf(stderr, "\n");
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
