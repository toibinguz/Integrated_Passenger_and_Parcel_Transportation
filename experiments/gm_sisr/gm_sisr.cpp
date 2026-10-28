#pragma GCC optimize("O3,unroll-loops")
#include <bits/stdc++.h>
using namespace std;
typedef long long ll;
namespace fs = std::filesystem;

// ============================================================================
// PARAMETERS & HYPERPARAMETERS
// ============================================================================
static double TIME_LIMIT_SEC = 2.0;      // Wall-clock search budget (seconds)

// --- CHẾ ĐỘ NHIỆT RĂNG CƯA BẢO VỆ ĐỘ CHÍN (MATURITY-GUARDED SAWTOOTH) ---
static int          N_WAVES       = 5;      // Số chu kỳ sóng nhiệt răng cưa trong toàn bộ thời gian chạy
static const double T_PEAK        = 38.0;   // Đỉnh nhiệt nung chảy cấu trúc đầu mỗi sóng
static const double T_MIN         = 0.80;   // Sàn nhiệt kết tinh sâu vắt kiệt kỷ lục
static const double RHO_COOL      = 0.70;   // Tỷ lệ dốc làm lạnh (70% thời gian sóng làm lạnh, 30% ngâm đáy kết tinh)
static int          M_QUENCH      = 6000;   // Số bước bế tắc tại đáy lạnh để công nhận "đủ chín"

// --- CÁC THAM SỐ TOÁN TỬ SISR ---
static const int NB_SIZE = 60;           // Normalized spatio-temporal neighbor list size
static const double C_AVG = 10.0;        // SISR: average removed requests
static const int L_MAX = 10;             // SISR: maximum string length
static int CAND_NB = 3;                  // Unserved neighbors offered to recreate
static int CAND_RAND = 10;               // Random unserved requests offered to recreate (Sweet Spot)
static unsigned BLINK_MASK = 31;         // Christiaens blink probability (1/32)
static const ll INFLL = (ll)4e18;

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

// Minimal Telemetry
static long long sort_records[2] = {0};

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
// ATOMIC CHANGE TRACKING, PURE CHRISTIAENS STRING RUIN & BLINK RECREATE
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

// Pure Christiaens & Berghe Recreate: Greedy Insertion with Blinks (Returns count of inserted requests)
static int recreate(const vector<int>& cands, bool blink) {
    int n_ins = 0;
    for (int q : cands) {
        if (veh[q]) continue;
        ll best1 = INFLL;
        int bk = -1, bi = 0, bj = 0;
        CacheEntry* ce = &icache[(size_t)q * (K + 1)];
        for (int k = 1; k <= K; ++k) {
            CacheEntry& e = ce[k];
            if (e.ver != rver[k]) { e.cost = best_insert(k, q, e.i, e.j, blink); e.ver = rver[k]; }
            if (e.cost < best1) { best1 = e.cost; bk = k; bi = e.i; bj = e.j; }
        }
        if (bk != -1 && Rev[q] - best1 > 0) {
            do_insert(bk, q, bi, bj);
            ++n_ins;
        }
    }
    return n_ins;
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

// Pure Christiaens & Berghe (2020) Adjacent String Removal
static void ruin_string(int seed) {
    removed.clear();
    int tot_nodes = 0, busy = 0;
    for (int k = 1; k <= K; ++k) if (rt[k].n()) { tot_nodes += rt[k].n(); ++busy; }
    if (!busy) return;

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

    try_route(seed);
    for (int c : nbr[seed]) {
        if (done >= ks) break;
        try_route(c);
    }
}

// Clean Memoryless Candidate Ordering: 50% Static Net Profit, 50% Uniform Random Shuffle
static int order_candidates(vector<int>& cands) {
    int n = (int)cands.size();
    if (n <= 1) return 0;
    if ((rng() & 1) == 0) {
        sort(cands.begin(), cands.end(), [](int a, int b) {
            return (Rev[a] - nic[a]) > (Rev[b] - nic[b]);
        });
        return 0;
    } else {
        for (int i = n - 1; i > 0; --i) {
            swap(cands[i], cands[rng() % (i + 1)]);
        }
        return 1;
    }
}

static void save_best() {
    best_obj = cur_obj;
    for (int k = 1; k <= K; ++k) best_nd[k] = rt[k].nd;
}

static void restore_best() {
    fill(veh.begin(), veh.end(), 0);
    for (int k = 1; k <= K; ++k) {
        rt[k].nd = best_nd[k];
        recompute(k);
        rver[k] = ++gver;
        for (int i = 1; i <= rt[k].n(); ++i) {
            int x = rt[k].nd[i];
            if (x <= R) veh[x] = k;
        }
    }
    cur_obj = best_obj;
}

// ============================================================================
// MATURITY-GUARDED STATIC SAWTOOTH THERMOSTAT (MG-SST)
// ============================================================================
struct MaturitySawtoothThermostat {
    int cur_wave = 0;
    int total_reheats = 0;
    int total_restores = 0;
    double b_sat = 0.0;
    double cur_temp = T_PEAK;

    // Tiến độ sóng thích ứng
    double wave_start_tau = 0.0;
    double nominal_end_tau = 0.0;
    bool in_freezing = false;
    long long stag_cold_steps = 0;

    // Biến thống kê thụ động phục vụ vẽ đồ thị 3 tầng (KHÔNG can thiệp điều khiển)
    double last_r_worse = 0.0, last_r_imp = 0.0, last_r_neutral = 0.0;
    long long win_iters = 0, win_worse_tries = 0, win_worse_acc = 0, win_imp = 0, win_neu = 0;

    void init(ll init_obj) {
        cur_wave = total_reheats = total_restores = 0;
        b_sat = (double)init_obj;
        cur_temp = T_PEAK;
        wave_start_tau = 0.0;
        nominal_end_tau = 1.0 / (double)N_WAVES;
        in_freezing = false;
        stag_cold_steps = 0;
        win_iters = win_worse_tries = win_worse_acc = win_imp = win_neu = 0;
        last_r_worse = last_r_imp = last_r_neutral = 0.0;
    }

    // Cập nhật nhiệt độ định kỳ theo tiến độ thời gian (mỗi 32 bước lặp theo nhịp đo đồng hồ)
    pair<double, bool> update(double tau) {
        if (tau >= 1.0) tau = 1.0;

        double wave_dur = max(0.001, nominal_end_tau - wave_start_tau);
        double p = (tau - wave_start_tau) / wave_dur;

        bool need_restore = false;

        // Nếu đã chạm hoặc vượt thời điểm kết thúc danh định của sóng
        if (tau >= nominal_end_tau && cur_wave < N_WAVES - 1) {
            // Kiểm tra tiêu chuẩn ĐỦ CHÍN:
            // 1. Phải đang ở vùng đóng băng (in_freezing)
            // 2. stag_cold_steps >= M_QUENCH (đã bế tắc tại đáy lạnh, không còn bước cải thiện)
            // 3. Hoặc chạm trần an toàn thời gian gia hạn (tối đa 40% thời lượng sóng)
            double grace = (tau - nominal_end_tau);
            double max_grace = 0.40 * wave_dur;
            bool is_mature = in_freezing && (stag_cold_steps >= M_QUENCH);
            bool is_timeout = (grace >= max_grace);

            if (is_mature || is_timeout) {
                // Đủ chín hoặc hết hạn ân hạn -> Kích hoạt Reheat sang sóng tiếp theo
                ++cur_wave;
                ++total_reheats;
                need_restore = true;
                ++total_restores;
                wave_start_tau = tau;
                nominal_end_tau = wave_start_tau + (1.0 - wave_start_tau) / (double)(N_WAVES - cur_wave);
                cur_temp = T_PEAK;
                in_freezing = false;
                stag_cold_steps = 0;
                return { cur_temp, need_restore };
            }
        }

        // Tính nhiệt độ sóng hiện tại
        if (p < RHO_COOL) {
            cur_temp = T_PEAK * pow(T_MIN / T_PEAK, p / RHO_COOL);
            in_freezing = false;
        } else {
            cur_temp = T_MIN;
            in_freezing = true; // ĐÃ VÀO VÙNG ĐÓNG BĂNG!
        }

        return { cur_temp, need_restore };
    }

    // Ghi nhận thống kê mỗi bước đi (O(1))
    void observe(ll delta, bool accepted, ll cur_o) {
        if ((double)cur_o > b_sat) b_sat = (double)cur_o;
        ++win_iters;

        // CHỈ THEO DÕI BẾ TẮC KHI ĐÃ VÀO VÙNG ĐÓNG BĂNG (in_freezing == true)!
        if (in_freezing) {
            if (delta > 0) {
                // Có bước cải thiện (cục bộ hoặc kỷ lục) -> Reset bế tắc! Nghiệm đang tiếp tục leo đồi!
                stag_cold_steps = 0;
            } else {
                ++stag_cold_steps;
            }
        }

        if (delta > 0) ++win_imp;
        else if (delta == 0) ++win_neu;
        else {
            ++win_worse_tries;
            if (accepted) ++win_worse_acc;
        }
        if (win_iters >= 2000) {
            last_r_worse   = (win_worse_tries > 0) ? ((double)win_worse_acc / (double)win_worse_tries) : 0.0;
            last_r_imp     = (double)win_imp / (double)win_iters;
            last_r_neutral = (double)win_neu / (double)win_iters;
            win_iters = win_worse_tries = win_worse_acc = win_imp = win_neu = 0;
        }
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
    if (argc > 4) CAND_RAND = atoi(argv[4]);
    if (argc > 5) BLINK_MASK = (unsigned)atoi(argv[5]);
    if (argc > 6) CAND_NB = atoi(argv[6]);
    if (argc > 7) N_WAVES = atoi(argv[7]);
    if (argc > 8) M_QUENCH = atoi(argv[8]);

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

    // 1. Initialize Energy Scale
    double sum_net = 0.0;
    for (int q : servable_list) sum_net += max(1.0, (double)(Rev[q] - nic[q]));
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
        string folder_name = "run_" + tname + "_" + string(tbuf) + (run_seed >= 0 ? "_s" + to_string(run_seed) : "") + "_closed_loop";

        fs::path base_out = fs::path("experiments") / "gm_sisr" / "output";
        out_dir = (base_out / "runs" / folder_name).string();
        latest_dir = (base_out / "latest" / tname).string();
        fs::create_directories(out_dir);
        fs::create_directories(latest_dir);

        flog.open(out_dir + "/log.txt");
        freport.open(out_dir + "/report.txt");
        cerr << "[CLOSED_LOOP_SISR] Initialized run folder: " << out_dir << "\n";
        if (flog.is_open()) {
            flog << "# SOICT 2026 - CLEAN SISR + CLOSED-LOOP FEEDBACK THERMOSTAT LOG\n"
                 << "# Instance: " << test_filepath << " | K=" << K << " N=" << N << " M=" << M << " V=" << V << "\n"
                 << "# Header: iter,elapsed_s,cycle,temp,ruin_mode,n_removed,delta,cur_obj,best_obj,status,b_sat,r_worse,r_imp,r_neutral\n";
        }
    }

    // 3. Initial Greedy Solution
    vector<int> init = servable_list;
    sort(init.begin(), init.end(), [&](int a, int b) { return Rev[a] - nic[a] > Rev[b] - nic[b]; });
    ++stamp_id; rev_delta = 0;
    recreate(init, false);
    ll initial_cost = 0; for (int k = 1; k <= K; ++k) initial_cost += rt[k].cost;
    cur_obj = rev_delta - initial_cost;
    save_best();
    ll initial_greedy_obj = cur_obj;

    // 4. Maturity-Guarded Sawtooth Thermostat Initialization
    double elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
    const double search_start = elapsed;
    const double budget = max(0.001, TIME_LIMIT_SEC - search_start);

    MaturitySawtoothThermostat thermo;
    thermo.init(cur_obj);

    double temp = thermo.cur_temp;
    bool need_load_best = false;
    int cur_cycle = 0;
    long long total_iters = 0, accepted_count = 0, improved_count = 0, local_improve_count = 0;
    long long worsening_tries = 0, worsening_accepts = 0, neutral_count = 0;

    // 5. Main Search Loop (Pure Christiaens & Berghe SISR + Minimal Sawtooth SA)
    for (long long it = 0;; ++it) {
        total_iters = it;
        if ((it & 31) == 0) {
            elapsed = chrono::duration<double>(chrono::steady_clock::now() - t_start).count();
            if (elapsed >= TIME_LIMIT_SEC) break;
            double tau = (elapsed - search_start) / budget;
            auto [next_T, load_best] = thermo.update(tau);
            temp = next_T;
            if (load_best) need_load_best = true;
            cur_cycle = thermo.cur_wave;
        }

        if (need_load_best) {
            if (cur_obj < best_obj) restore_best();
            need_load_best = false;
        }

        ++stamp_id; touched.clear(); veh_log.clear(); rev_delta = 0;

        int seed = servable_list[rng() % servable_list.size()];
        ruin_string(seed);
        int n_rem = (int)removed.size();

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

        int s_mode = order_candidates(cand);
        int n_ins = recreate(cand, true);
        (void)n_ins;

        ll dcost = 0;
        for (int k : touched) dcost += rt[k].cost - backup[k].cost;
        ll new_obj = cur_obj + rev_delta - dcost;
        ll delta = new_obj - cur_obj;
        string status = "REJECT";
        if (delta < 0) ++worsening_tries;

        if (delta >= 0 || (double)delta > temp * log(rand01() + 1e-300)) {
            cur_obj = new_obj;
            ++accepted_count;
            if (cur_obj > best_obj) {
                save_best();
                ++improved_count;
                sort_records[s_mode]++;
                status = "IMPROVE_GLOBAL";
            } else if (delta > 0) {
                ++local_improve_count;
                status = "IMPROVE_LOCAL";
            } else if (delta == 0) {
                ++neutral_count;
                status = "NEUTRAL";
            } else {
                ++worsening_accepts;
                status = "SA_ACCEPT";
            }
        } else {
            for (int k : touched) { swap(rt[k], backup[k]); rver[k] = ++gver; }
            for (int i = (int)veh_log.size() - 1; i >= 0; --i) veh[veh_log[i].first] = veh_log[i].second;
        }

        // Real-time passive observation for logging / plotting
        thermo.observe(delta, (status != "REJECT"), cur_obj);

        if (flog.is_open() && ((it & 127) == 0 || status == "IMPROVE_GLOBAL")) {
            flog << it << "," << fixed << setprecision(4) << elapsed << ","
                 << cur_cycle << "," << setprecision(2) << temp << ","
                 << 1 << "," << n_rem << "," << delta << ","
                 << cur_obj << "," << best_obj << "," << status << ","
                 << setprecision(1) << thermo.b_sat << ","
                 << setprecision(4) << thermo.last_r_worse << ","
                 << thermo.last_r_imp << "," << thermo.last_r_neutral << "\n";
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
                    << "   CLEAN SISR-SA + SEGMENT EQUILIBRIUM THERMOSTAT: FULL REPORT        \n"
                    << "======================================================================\n\n"
                    << "  * Instance             : " << test_filepath << "\n"
                    << "  * Architecture         : 1 String Ruin + 1 Blink Recreate + SegmentEquilibriumThermostat\n"
                    << "  * Problem Dimensions   : K=" << K << ", N=" << N << ", M=" << M << ", V=" << V << "\n"
                    << "  * Wall-clock Budget    : " << TIME_LIMIT_SEC << "s (Elapsed: " << fixed << setprecision(4) << total_wall_time << "s)\n"
                    << "  * Random Seed          : " << run_seed << "\n"
                    << "  * Initial Greedy Obj   : " << initial_greedy_obj << "\n"
                    << "  * Final Best Objective : " << best_obj << " (Verified: " << benefit << ")\n"
                    << "  * Net Improvement      : +" << (best_obj - initial_greedy_obj) << "\n\n"
                    << "--- SEARCH THROUGHPUT & MOMENTUM ACCEPTANCE PROFILE ---\n"
                    << "  * Total Iterations     : " << total_iters << " (" << fixed << setprecision(0) << (total_iters / max(0.001, total_wall_time)) << " iter/s)\n"
                    << "  * Total Accepted Moves : " << accepted_count << " (" << fixed << setprecision(2) << (100.0 * accepted_count / max(1LL, total_iters)) << "%)\n"
                    << "  * Neutral Moves (d=0)  : " << neutral_count << " (" << fixed << setprecision(2) << (100.0 * neutral_count / max(1LL, total_iters)) << "%)\n"
                    << "  * True Worsening Accept: " << worsening_accepts << " / " << worsening_tries << " (" << fixed << setprecision(2) << (100.0 * worsening_accepts / max(1LL, worsening_tries)) << "%)\n"
                    << "  * Local Improvements   : " << local_improve_count << "\n"
                    << "  * Global Records       : " << improved_count << "\n"
                    << "  * Thermal Schedule     : N_WAVES=" << N_WAVES << ", T_PEAK=" << T_PEAK << ", T_MIN=" << T_MIN << ", RHO_COOL=" << RHO_COOL << "\n"
                    << "  * Reheats / Restores   : " << thermo.total_reheats << " / " << thermo.total_restores << "\n"
                    << "  * Final B_sat & Temp   : B_sat=" << setprecision(1) << thermo.b_sat << " | Final T=" << setprecision(2) << temp << "\n\n"
                    << "--- OPERATOR TELEMETRY ---\n"
                    << "  * Mode 1 (String Ruin) : Calls=" << total_iters << " | Improves=" << local_improve_count << " | Records=" << improved_count << "\n"
                    << "  * Sort Records (0/1/2) : " << sort_records[0] << " / " << sort_records[1] << " / 0\n"
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
