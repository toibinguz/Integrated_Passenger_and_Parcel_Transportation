#pragma GCC optimize("O3,unroll-loops")
#include <bits/stdc++.h>
using namespace std;
typedef long long ll;

// ============================================================================
// COLUMN EXPORTER FOR CP-SAT SET PACKING SOLVER
// ============================================================================
static const long long ENUM_STATE_CAP = 500000000;
static const size_t ENUM_COL_CAP = 20000000;

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

static int K, N, M, V, R, SV;
static vector<int> O, Q, Rev;
static vector<int> Tm, Cm;
static inline int T(int u, int v) { return Tm[u * SV + v]; }
static inline int C(int u, int v) { return Cm[u * SV + v]; }

static vector<int> nin, nout, nw, nic;
static vector<ll> ne, nl, ndur;
static vector<char> servable;

struct Column { int k; ll profit; int rbeg, rlen, nbeg, nlen; };
static vector<Column> cols;
static vector<int> col_req, col_nodes;
static unordered_map<uint64_t, int> col_index;
static long long enum_states;
static bool enum_abort;
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

int main(int argc, char** argv) {
    ios_base::sync_with_stdio(false); cin.tie(NULL);
    if (argc < 3) {
        cerr << "Usage: column_exporter <input_testcase.txt> <output_columns.txt>\n";
        return 1;
    }
    if (!freopen(argv[1], "r", stdin)) {
        cerr << "Failed to open input: " << argv[1] << "\n";
        return 1;
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

    e_by_l = servable_list;
    sort(e_by_l.begin(), e_by_l.end(), [](int a, int b) { return nl[a] < nl[b]; });
    e_l.clear(); for (int q : e_by_l) e_l.push_back(nl[q]);
    e_used.assign(R + 1, 0);
    enum_states = 0; enum_abort = false;

    auto t0 = chrono::steady_clock::now();
    for (int k = 1; k <= K && !enum_abort; ++k) {
        enum_dfs(k, O[k], 0, 0, 0);
    }
    double elapsed = chrono::duration<double>(chrono::steady_clock::now() - t0).count();

    const int NC = (int)cols.size();
    cerr << "[COLUMN EXPORTER] states=" << enum_states << " | unique_cols=" << NC << " | time=" << elapsed << "s\n";

    FILE* out = fopen(argv[2], "w");
    if (!out) {
        cerr << "Failed to open output: " << argv[2] << "\n";
        return 1;
    }

    // Header: K N M R V NC
    fprintf(out, "%d %d %d %d %d %d\n", K, N, M, R, V, NC);
    for (int j = 0; j < NC; ++j) {
        const auto& c = cols[j];
        fprintf(out, "%d %lld %d", c.k, c.profit, c.rlen);
        for (int i = 0; i < c.rlen; ++i) fprintf(out, " %d", col_req[c.rbeg + i]);
        
        // Exact route stop vertices
        int stop_cnt = 0;
        for (int i = 0; i < c.nlen; ++i) {
            int x = col_nodes[c.nbeg + i];
            stop_cnt += (x <= N) ? 2 : 1;
        }
        fprintf(out, " %d", stop_cnt);
        for (int i = 0; i < c.nlen; ++i) {
            int x = col_nodes[c.nbeg + i];
            if (x <= N) fprintf(out, " %d %d", nin[x], nout[x]);
            else fprintf(out, " %d", nin[x]);
        }
        fprintf(out, "\n");
    }
    fclose(out);
    return 0;
}
