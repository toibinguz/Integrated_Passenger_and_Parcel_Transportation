#pragma GCC optimize("O3,unroll-loops")
#include <bits/stdc++.h>
using namespace std;
typedef long long ll;
namespace fs = std::filesystem;

// ============================================================================
// PARAMETERS & HYPERPARAMETERS (GUIDED MULTI-BASIN SISR-SA)
// ============================================================================
static double TIME_LIMIT_SEC = 2.0;      // Wall-clock search budget (seconds)
static const int NB_SIZE = 60;           // Normalized spatio-temporal neighbor list size
static const double C_AVG = 10.0;        // SISR: average removed requests
static const int L_MAX = 10;             // SISR: maximum string length
static const int CAND_NB = 3;            // Unserved neighbors offered to recreate
static const int CAND_RAND = 6;          // Random unserved requests offered to recreate
static const unsigned BLINK_MASK = 63;   // Christiaens blink probability (1/64)
static const ll INFLL = (ll)4e18;

// Diverse State Archive configuration
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
static inline int rand_int(int lo, int hi) { return lo + (int)(rng() % (uint64_t)(hi - lo + 1)); }
static inline double rand01() { return (rng() >> 11) * (1.0 / 9007199254740992.0); }

// ============================================================================
// PROBLEM DATA & ACCELERATION STRUCTURES
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

// Piggybacked Memory & Diverse Archive
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
// ROUTE EVALUATION & O(1) SLACK INSERTION
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
// ATOMIC CHANGE TRACKING, RUIN & RECREATE OPERATORS
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
            unserved_streak[q] = 0;
        } else {
            unserved_streak[q]++;
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

// ============================================================================
// DIVERSE STATE ARCHIVE & BASIN JUMP
// ============================================================================
static void save_best() {
    best_obj = cur_obj;
    for (int k = 1; k <= K; ++k) best_nd[k] = rt[k].nd;
}
static void load_best() {
    fill(veh.begin(), veh.end(), 0);
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
        cand.clear(); cand.push_back(priority_head);
        for (int q : removed) cand.push_back(q);
        recreate_with_memory(cand, false);
        ll dcost = 0;
        for (int k : touched) dcost += rt[k].cost - backup[k].cost;
        cur_obj += rev_delta - dcost;
        if (cur_obj > best_obj) { save_best(); ++basin_jumps_records; }
    }
}

// ============================================================================
// UNIFIED THERMAL ENGINE: SEDCA-V3 (Flat-Bottomed U-Valley & Micro-Step Calibration)
// ============================================================================
struct AdaptiveThermostat {
    double t_max = 2.0;

    // Phân bổ cấu trúc chu kỳ tạo "Thung lũng đáy phẳng" (U-Shaped Valley):
    // - 10% đầu tổng thời gian: Hạ nhanh từ T_0 về vùng làm việc
    // - Trong mỗi chu kỳ thở: 
    //   + 12% Tăng nhiệt mượt (Cosine Reheat)
    //   + 33% Hạ nhanh qua vùng nhiệt cao (Fast Recovery)
    //   + 55% Ngâm ở nhiệt độ Trung bình-Thấp & Thấp (Deep Convergence Soaking)
    const double tau_fast = 0.10;
    const double tau_quench = 0.86;
    const double phase_up_end = 0.12;
    const double phase_drop_end = 0.45; // Từ 0.45 -> 1.00 (55% thời gian) là pha ngâm đáy!
    const double PI_CONST = 3.14159265358979323846;

    // Các ngưỡng nhiệt tự động hiệu chỉnh
    double T_0 = 25.0;       // Đỉnh khởi tạo
    double T_work = 4.0;     // Trần của pha ngâm trung bình-thấp (Sweet-spot exploration/exploitation)
    double T_min = 0.6;      // Đáy tinh chỉnh sâu
    double micro_delta = 5.0;// Độ lệch năng lượng của các bước tinh chỉnh cấu trúc nhỏ
    bool calibrated = false;

    // Trạng thái chu kỳ
    bool in_cycle_mode = false;
    double cycle_start_tau = 0.0, cycle_duration_tau = 0.0;
    double cycle_peak_T = 0.0, cycle_work_T = 0.0, cycle_base_T = 0.0;
    int cycle_count = 0;

    void auto_calibrate(const vector<double>& worsening_deltas, double scale_S) {
        vector<double> valid;
        valid.reserve(worsening_deltas.size());
        // Chỉ thu thập các bước lệch cấu trúc nhỏ/trung bình (loại bỏ các bước sụp đổ tuyến)
        for (double d : worsening_deltas) {
            if (d > 0.0 && d <= 0.80 * scale_S) valid.push_back(d);
        }
        if (valid.size() < 20) valid = worsening_deltas;

        if (valid.empty()) {
            micro_delta = max(1.0, 0.04 * scale_S);
        } else {
            sort(valid.begin(), valid.end());
            // Lấy phân vị thứ 15% (15th percentile) đại diện cho rào cản tinh chỉnh vi mô (Micro-moves)
            // kết hợp chặn trên bởi 6% S_scale để tuyệt đối không bị vọt nhiệt lên vùng >30
            double p15 = valid[(valid.size() * 15) / 100];
            micro_delta = clamp(p15, 1.0, max(2.0, 0.06 * scale_S));
        }

        // Suy diễn thang nhiệt từ micro_delta:
        // Tại T_min: xác suất nhận bước xấu micro_delta là ~0.1% (-ln(0.001) = 6.9)
        // Tại T_work (vùng trung bình thấp): xác suất nhận bước xấu micro_delta là ~35% (-ln(0.35) = 1.05)
        // Tại T_0 (đỉnh): bằng ~5.5 lần T_work
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

        // 1. Đỉnh sóng (Peak) chỉ tăng vừa phải (từ 0.65*T_0 giảm dần về 1.5*T_work)
        // Không đẩy lên quá cao để tránh phá nát nghiệm vừa hội tụ
        double max_peak = 0.65 * T_0;
        double min_peak = 1.50 * T_work;
        cycle_peak_T = min_peak * pow(max_peak / min_peak, 1.0 - osc_progress);

        // 2. Ngưỡng bắt đầu pha ngâm (Work T) và đáy chu kỳ (Base T)
        cycle_work_T = T_work * pow(0.65, osc_progress);
        cycle_base_T = T_min  * pow(0.75, osc_progress);

        // 3. Thời lượng chu kỳ: Khoảng 4 đến 5 chu kỳ lớn, giãn dài dần để ngâm lâu hơn ở cuối
        double base_span = tau_quench - tau_fast;
        double remaining_osc_tau = tau_quench - current_tau;
        double target_duration = base_span * (0.18 + 0.10 * osc_progress);
        cycle_duration_tau = min(target_duration, max(0.04, remaining_osc_tau));
    }

    pair<double, bool> get_temperature(double elapsed_time) {
        double tau = clamp(elapsed_time / max(0.001, t_max), 0.0, 1.0);
        bool triggered_reheat = false;

        // GIAI ĐOẠN 1 (0% -> 10%): Hạ nhanh từ T_0 xuống thẳng vùng T_work rồi lướt về T_min
        if (tau < tau_fast) {
            double p = tau / tau_fast;
            // Nửa đầu rơi nhanh về T_work, nửa sau lướt êm từ T_work về T_min
            double T = (p < 0.4) 
                ? (T_0 * pow(T_work / T_0, p / 0.4))
                : (T_work * pow(T_min / T_work, (p - 0.4) / 0.6));
            return { max(T, T_min), false };
        }

        // GIAI ĐOẠN 3 (86% -> 100%): Pha vắt kiệt cuối kỳ (Quenching từ T_work về 0.5 * T_min)
        if (tau >= tau_quench) {
            double quench_prog = clamp((tau - tau_quench) / max(0.001, 1.0 - tau_quench), 0.0, 1.0);
            double T = (0.7 * T_work) * pow((0.5 * T_min) / (0.7 * T_work), quench_prog);
            return { max(T, 0.2), false };
        }

        // GIAI ĐOẠN 2 (10% -> 86%): Chu kỳ 3 pha có Đáy Phẳng Kéo Dài (55% Dwell Time)
        double phi_c = in_cycle_mode ? ((tau - cycle_start_tau) / max(0.0001, cycle_duration_tau)) : 1.0;
        if (!in_cycle_mode || phi_c >= 1.0) {
            start_new_cycle(tau);
            triggered_reheat = true;
            phi_c = 0.0;
        }
        phi_c = clamp(phi_c, 0.0, 1.0);

        double T = cycle_base_T;
        if (phi_c < phase_up_end) {
            // Pha 2A (12% thời lượng): Tăng nhiệt mượt Cosine từ đáy lên đỉnh
            double u = phi_c / phase_up_end;
            double cos_factor = 0.5 * (1.0 - cos(PI_CONST * u));
            T = cycle_base_T + (cycle_peak_T - cycle_base_T) * cos_factor;
        } else if (phi_c < phase_drop_end) {
            // Pha 2B (33% thời lượng): Hạ nhiệt nhanh từ Peak về ngưỡng trung bình-thấp (cycle_work_T)
            double u = (phi_c - phase_up_end) / (phase_drop_end - phase_up_end);
            T = cycle_peak_T * pow(cycle_work_T / cycle_peak_T, u);
        } else {
            // Pha 2C (55% thời lượng): NGÂM HỘI TỤ (Soaking Phase) lướt cực thoải từ cycle_work_T về cycle_base_T
            double u = (phi_c - phase_drop_end) / (1.0 - phase_drop_end);
            T = cycle_work_T * pow(cycle_base_T / cycle_work_T, u);
        }

        return { max(T, 0.2), triggered_reheat };
    }

};

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

    // 1. Initialize Energy Scale & Learned Regret Memory
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

    rver.assign(K + 1, 0); icache.assign((size_t)(R + 1) * (K + 1), CacheEntry());
    rt.assign(K + 1, Route()); backup.assign(K + 1, Route()); best_nd.assign(K + 1, vector<int>(1, 0));
    for (int k = 1; k <= K; ++k) { rt[k].nd.assign(1, 0); recompute(k); }
    veh.assign(R + 1, 0);
    touch_stamp.assign(K + 1, 0); ruin_stamp.assign(K + 1, 0); cand_stamp.assign(R + 1, 0);

    // Setup Output Session
    string out_dir = "", latest_dir = "";
    ofstream flog, freport;
    bool write_files = !test_filepath.empty();
    if (write_files) {
        string tname = fs::path(test_filepath).stem().string();
        time_t now_c = chrono::system_clock::to_time_t(chrono::system_clock::now());
        char tbuf[64];
        strftime(tbuf, sizeof(tbuf), "%Y%m%d_%H%M%S", localtime(&now_c));
        string folder_name = "run_" + tname + "_" + string(tbuf) + (run_seed >= 0 ? "_s" + to_string(run_seed) : "") + "_sedca";

        fs::path base_out = fs::path("experiments") / "gm_sisr" / "output";
        out_dir = (base_out / "runs" / folder_name).string();
        latest_dir = (base_out / "latest" / tname).string();
        fs::create_directories(out_dir);
        fs::create_directories(latest_dir);

        flog.open(out_dir + "/log.txt");
        freport.open(out_dir + "/report.txt");
        cerr << "[GM_SISR] Initialized run folder: " << out_dir << "\n";
        if (flog.is_open()) {
            flog << "# SOICT 2026 - GM-SISR METRIC LOG\n"
                 << "# Instance: " << test_filepath << " | K=" << K << " N=" << N << " M=" << M << " V=" << V << "\n"
                 << "# Header: iter,elapsed_s,cycle,temp,ruin_mode,n_removed,delta,cur_obj,best_obj,status\n";
        }
    }

    // 3. Initial Greedy Solution
    vector<int> init = servable_list;
    sort(init.begin(), init.end(), [&](int a, int b) { return Rev[a] - nic[a] > Rev[b] - nic[b]; });
    ++stamp_id; rev_delta = 0;
    recreate_with_memory(init, false);
    ll initial_cost = 0; for (int k = 1; k <= K; ++k) initial_cost += rt[k].cost;
    cur_obj = rev_delta - initial_cost;
    save_best();
    ll initial_greedy_obj = cur_obj;
    update_archive_at_cycle_end();

    // 4. Thermal Schedule Initialization (Unified SEDCA-V2)
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
    double time_last_improve = search_start;
    long long total_iters = 0, accepted_count = 0, improved_count = 0;

    // 5. Main Search Loop
    for (long long it = 0;; ++it) {
        total_iters = it;
        if ((it & 15) == 0) {
            elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
            if (elapsed >= TIME_LIMIT_SEC) break;
            double t = max(0.0, elapsed - search_start);

            if (sedca.calibrated) {
                auto [new_temp, triggered_reheat] = sedca.get_temperature(t);
                temp = new_temp;

                if (triggered_reheat) {
                    update_archive_at_cycle_end();
                    if (improved_in_cycle) {
                        cycle_stag = 0;
                        // Neo về kỷ lục tốt nhất nếu cuối chu kỳ bị lệch nhẹ
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
                temp = 0.08 * S_scale; // Khởi động ấm vừa phải trong 200 bước đầu (~1.5ms)
                cur_cycle = 0;
            }
            if ((it & 8191) == 0) update_archive_at_cycle_end();
        }

        ++stamp_id; touched.clear(); veh_log.clear(); rev_delta = 0;

        int seed = select_seed();
        int r_mode = ruin_guided(seed);
        mode_tries[r_mode]++;
        int n_rem = (int)removed.size();

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

        int s_mode = order_candidates(cand);
        recreate_with_memory(cand, true);

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
                improved_in_cycle = true;
                mode_records[r_mode]++;
                sort_records[s_mode]++;
                status = "IMPROVE_GLOBAL";
                time_last_improve = elapsed;
            } else if (delta > 0) {
                mode_improve[r_mode]++;
                status = "IMPROVE_LOCAL";
                time_last_improve = elapsed;
            } else {
                status = "SA_ACCEPT";
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

        if (flog.is_open() && ((it & 127) == 0 || status == "IMPROVE_GLOBAL")) {
            flog << it << "," << fixed << setprecision(4) << elapsed << ","
                 << cur_cycle << "," << setprecision(2) << temp << ","
                 << r_mode << "," << n_rem << "," << delta << ","
                 << cur_obj << "," << best_obj << "," << status << "\n";
        }
    }

    double total_wall_time = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
    if (flog.is_open()) flog.close();

    // 6. Independent Final Verification & Output Formatting
    ll benefit = 0;
    for (int k = 1; k <= K; ++k) {
        rt[k].nd = best_nd[k];
        if (!recompute(k)) { rt[k].nd.assign(1, 0); recompute(k); }
        benefit -= rt[k].cost;
        for (int i = 1; i <= rt[k].n(); ++i) { int x = rt[k].nd[i]; if (x <= R) benefit += Rev[x]; }
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
            if (x <= N) out += ' ' + to_string(nin[x]) + ' ' + to_string(nout[x]);
            else out += ' ' + to_string(nin[x]);
        }
        out += '\n';
    }

    if (write_files && !out_dir.empty()) {
        ofstream fres(out_dir + "/result.txt");
        fres << out;
        fres.close();

        if (freport.is_open()) {
            freport << "======================================================================\n"
                    << "      GUIDED MULTI-BASIN SISR-SA (GM-SISR): FULL EXPERIMENT REPORT\n"
                    << "======================================================================\n\n"
                    << "  * Instance             : " << test_filepath << "\n"
                    << "  * Thermal Strategy     : SEDCA-V3 (Flat-Bottomed U-Valley & Micro-Step Calibration)\n"
                    << "  * Problem Dimensions   : K=" << K << ", N=" << N << ", M=" << M << ", V=" << V << "\n"
                    << "  * Wall-clock Budget    : " << TIME_LIMIT_SEC << "s (Elapsed: " << fixed << setprecision(4) << total_wall_time << "s)\n"
                    << "  * Random Seed          : " << run_seed << "\n"
                    << "  * Initial Greedy Obj   : " << initial_greedy_obj << "\n"
                    << "  * Final Best Objective : " << best_obj << " (Verified: " << benefit << ")\n"
                    << "  * Net Improvement      : +" << (best_obj - initial_greedy_obj) << "\n\n"
                    << "--- SEARCH THROUGHPUT & THERMAL PROFILE ---\n"
                    << "  * Total Iterations     : " << total_iters << " (" << fixed << setprecision(0) << (total_iters / max(0.001, total_wall_time)) << " iter/s)\n"
                    << "  * Accepted Moves       : " << accepted_count << " (" << fixed << setprecision(2) << (100.0 * accepted_count / max(1LL, total_iters)) << "%)\n"
                    << "  * Global Records       : " << improved_count << "\n"
                    << "  * Total SA Cycles      : " << (cur_cycle + 1) << "\n"
                    << "  * Calibrated Temperatures: T0=" << sedca.T_0 << ", Twork=" << sedca.T_work << ", Tmin=" << sedca.T_min << "\n\n"
                    << "--- ABLATION & ARCHIVE TELEMETRY ---\n"
                    << "  * Mode 1 (String Ruin) : Calls=" << mode_tries[1] << " | Improves=" << mode_improve[1] << " | Records=" << mode_records[1] << "\n"
                    << "  * Mode 2 (Unserved-Cav): Calls=" << mode_tries[2] << " | Improves=" << mode_improve[2] << " | Records=" << mode_records[2] << "\n"
                    << "  * Mode 3 (Bottleneck)  : Calls=" << mode_tries[3] << " | Improves=" << mode_improve[3] << " | Records=" << mode_records[3] << "\n"
                    << "  * Sort Records (0/1/2) : " << sort_records[0] << " / " << sort_records[1] << " / " << sort_records[2] << "\n"
                    << "  * Archive Basin Jumps  : " << basin_jumps_total << " (Records=" << basin_jumps_records << ", Slots=" << archive_size << "/" << ARCHIVE_CAP << ")\n"
                    << "======================================================================\n";
            freport.close();
        }
        if (!latest_dir.empty()) {
            try {
                fs::copy_file(out_dir + "/result.txt", latest_dir + "/result.txt", fs::copy_options::overwrite_existing);
                if (fs::exists(out_dir + "/report.txt")) fs::copy_file(out_dir + "/report.txt", latest_dir + "/report.txt", fs::copy_options::overwrite_existing);
                if (fs::exists(out_dir + "/log.txt")) fs::copy_file(out_dir + "/log.txt", latest_dir + "/log.txt", fs::copy_options::overwrite_existing);
            } catch (...) {}
        }
    }

    fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}

