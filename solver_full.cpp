#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <cassert>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

// ==============================================================================
// SOICT 2026 - LSSolver in C++ (Single File Integration)
// Phiên bản C++ hợp nhất hoàn chỉnh từ 4 file Python.
// ĐẶC BIỆT CHÚ TRỌNG INDEXING VÀ BOUNDARY CHECK ĐỂ TRÁNH INDEX ERROR.
// ==============================================================================

// === CỜ ĐIỀU KHIỂN & BIẾN TOÀN CỤC ===
bool RELEASE_MODE = true;

double TIME_LIMIT = 100.0;
int TABU_TENURE = 15;
double MUTATION_RATE = 0.05;
double GRAVITY_ALPHA = 100.0;

uint64_t SESSION_PREFIX = 0;
uint64_t ROUTE_COUNTER = 0;
random_device rd;
mt19937 rng(1337);

uint64_t get_next_ver_id()
{
    ROUTE_COUNTER++;
    return (SESSION_PREFIX | ROUTE_COUNTER);
}

void vprint(const string& msg)
{
    if (!RELEASE_MODE)
    {
        cout << msg << endl;
    }
}

// === HỖ TRỢ HASH CHO PAIR (Dùng cho tabu_dict) ===
struct pair_hash
{
    template <class T1, class T2>
    size_t operator()(const pair<T1, T2>& p) const
    {
        auto h1 = hash<T1>{}(p.first);
        auto h2 = hash<T2>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};

// === CẤU TRÚC DỮ LIỆU CỐT LÕI (MODELS) ===
enum NodeType
{
    DEPOT = 0,
    PASSENGER = 1,
    PARCEL_PICKUP = 2,
    PARCEL_DROPOFF = 3
};

struct GenericNode
{
    int node_id;
    NodeType type;
    int job_id;
    long long revenue;
    int e;
    int l;
    int duration;
    int weight;
    int physical_in;
    int physical_out;
    int int_cost;

    GenericNode(int nid, NodeType t, int jid, long long rev, int _e, int _l, int dur, int w)
        : node_id(nid), type(t), job_id(jid), revenue(rev), e(_e), l(_l), duration(dur), weight(w)
    {
        physical_in = -1;
        physical_out = -1;
        int_cost = 0;
    }
};

struct Data
{
    string filename;
    int K, N, M;
    int V_mac;
    vector<int> capacities;

    vector<GenericNode*> nodes;
    vector<GenericNode*> passengers;
    vector<pair<GenericNode*, GenericNode*>> parcels;
    vector<GenericNode*> depots;

    vector<vector<int>> time_matrix;
    vector<vector<int>> cost_matrix;

    // Quản lý vùng nhớ của GenericNode để tránh rò rỉ (memory leak)
    vector<GenericNode*> _allocated_nodes;

    Data(const string& fname)
    {
        filename = fname;
        istream* in = &cin;
        ifstream fin;
        
        if (!RELEASE_MODE && !filename.empty())
        {
            fin.open(filename);
            if (!fin.is_open())
            {
                cerr << "[ERROR] Cannot open file " << filename << endl;
                exit(1);
            }
            in = &fin;
        }

        *in >> K >> N >> M;
        int V = 2 * N + 2 * M + K;

        for (int i = 0; i < K; i++)
        {
            int o_k, q_k;
            *in >> o_k >> q_k;
            capacities.push_back(q_k);
        }

        struct PassRaw
        {
            long long rev;
            int e;
            int l;
            int dur;
            int p_idx;
            int d_idx;
        };
        vector<PassRaw> passengers_raw;
        for (int i = 0; i < N; i++)
        {
            PassRaw p;
            *in >> p.p_idx >> p.d_idx >> p.e >> p.l >> p.dur >> p.rev;
            p.p_idx--; // CHÚ Ý INDEX: Ép về 0-indexed
            p.d_idx--; // CHÚ Ý INDEX: Ép về 0-indexed
            passengers_raw.push_back(p);
        }

        struct ParcRaw
        {
            long long rev;
            int e_p;
            int l_p;
            int e_d;
            int l_d;
            int dur;
            int w;
            int p_idx;
            int d_idx;
        };
        vector<ParcRaw> parcels_raw;
        for (int j = 0; j < M; j++)
        {
            ParcRaw p;
            *in >> p.p_idx >> p.d_idx >> p.w >> p.e_p >> p.l_p >> p.e_d >> p.l_d >> p.dur >> p.rev;
            p.p_idx--; // CHÚ Ý INDEX: Ép về 0-indexed
            p.d_idx--; // CHÚ Ý INDEX: Ép về 0-indexed
            parcels_raw.push_back(p);
        }

        vector<vector<int>> orig_time(V, vector<int>(V, 0));
        for (int i = 0; i < V; i++)
        {
            for (int j = 0; j < V; j++)
            {
                *in >> orig_time[i][j];
            }
        }

        vector<vector<int>> orig_cost(V, vector<int>(V, 0));
        for (int i = 0; i < V; i++)
        {
            for (int j = 0; j < V; j++)
            {
                *in >> orig_cost[i][j];
            }
        }

        // CHUẨN HOÁ SANG GENERIC NODE
        int macro_id = 0;

        // 1. Depot Nodes
        for (int i = 0; i < K; i++)
        {
            GenericNode* depot = new GenericNode(macro_id, DEPOT, N + M + i, 0, 0, 1000000, 0, 0);
            depot->physical_in = i;
            depot->physical_out = i;
            _allocated_nodes.push_back(depot);
            nodes.push_back(depot);
            depots.push_back(depot);
            macro_id++;
        }

        // 2. Hành khách
        for (int i = 0; i < passengers_raw.size(); i++)
        {
            auto& pr = passengers_raw[i];
            // CHÚ Ý INDEX: orig_cost bounds check
            assert(!(pr.p_idx >= V || pr.d_idx >= V || pr.p_idx < 0 || pr.d_idx < 0));

            long long adj_rev = pr.rev - orig_cost[pr.p_idx][pr.d_idx];
            if (adj_rev < 0) continue;  // Garbage collector: Bỏ qua khách hàng sinh lỗ nội tuyến

            int dur = 2 * pr.dur + orig_time[pr.p_idx][pr.d_idx];
            GenericNode* p_node =
                new GenericNode(macro_id, PASSENGER, i, pr.rev, pr.e, pr.l, dur, 0);
            p_node->physical_in = pr.p_idx;
            p_node->physical_out = pr.d_idx;
            p_node->int_cost = orig_cost[pr.p_idx][pr.d_idx];

            _allocated_nodes.push_back(p_node);
            nodes.push_back(p_node);
            passengers.push_back(p_node);
            macro_id++;
        }

        // 3. Hàng hóa
        for (int j = 0; j < parcels_raw.size(); j++)
        {
            auto& pr = parcels_raw[j];

            GenericNode* pick =
                new GenericNode(macro_id, PARCEL_PICKUP, N + j, 0, pr.e_p, pr.l_p, pr.dur, pr.w);
            pick->physical_in = pr.p_idx;
            pick->physical_out = pr.p_idx;
            _allocated_nodes.push_back(pick);
            nodes.push_back(pick);
            macro_id++;

            GenericNode* drop = new GenericNode(macro_id, PARCEL_DROPOFF, N + j, pr.rev, pr.e_d,
                                                pr.l_d, pr.dur, -pr.w);
            drop->physical_in = pr.d_idx;
            drop->physical_out = pr.d_idx;
            _allocated_nodes.push_back(drop);
            nodes.push_back(drop);
            macro_id++;

            parcels.push_back({pick, drop});
        }

        V_mac = nodes.size();
        time_matrix.assign(V_mac, vector<int>(V_mac, 0));
        cost_matrix.assign(V_mac, vector<int>(V_mac, 0));

        // Trích xuất ma trận macro
        for (int u = 0; u < V_mac; u++)
        {
            for (int v = 0; v < V_mac; v++)
            {
                int p_out = nodes[u]->physical_out;
                int p_in = nodes[v]->physical_in;
                if (p_out >= 0 && p_out < V && p_in >= 0 && p_in < V)
                {
                    cost_matrix[u][v] = orig_cost[p_out][p_in];
                    time_matrix[u][v] = orig_time[p_out][p_in];
                }
            }
        }
    }

    ~Data()
    {
        for (auto n : _allocated_nodes)
        {
            delete n;
        }
    }
};

struct Route
{
    int veh_id;
    int capacity;
    vector<GenericNode*> nodes;
    bool is_feasible;
    long long total_benefit;
    long long total_cost;

    vector<int> arr_time;
    vector<int> dep_time;
    vector<int> wait_time;
    vector<int> max_delay;
    vector<int> current_load;

    uint64_t ver_id;

    Route(int v_id, int cap, GenericNode* depot_node) : veh_id(v_id), capacity(cap)
    {
        nodes = {depot_node, depot_node};
        is_feasible = true;
        total_benefit = 0;
        total_cost = 0;
        ver_id = get_next_ver_id();
        update_states(nullptr);
    }

    Route clone() const
    {
        Route new_r(*this);  // Default copy constructor copies std::vector (by
                             // value), which is fast and correct for pointers.
        return new_r;
    }

    void update_states(const Data* data)
    {
        ver_id = get_next_ver_id();
        int n = nodes.size();
        assert(!(n == 0)) ; // CHÚ Ý INDEX: Tránh vector rỗng

        arr_time.assign(n, 0);
        dep_time.assign(n, 0);
        wait_time.assign(n, 0);
        max_delay.assign(n, 0);
        current_load.assign(n, 0);
        is_feasible = true;

        if (!data) return;

        total_cost = 0;
        total_benefit = 0;
        int load = 0;

        // Quét tới (Forward Sweep)
        for (int i = 1; i < n; i++)
        {
            GenericNode* prev = nodes[i - 1];
            GenericNode* curr = nodes[i];

            // CHÚ Ý INDEX: Kiểm tra an toàn truy xuất ma trận macro
            if (prev->node_id >= data->V_mac || curr->node_id >= data->V_mac)
            {
                is_feasible = false;
                continue;
            }

            if (curr->type != DEPOT)
            {
                total_cost += data->cost_matrix[prev->node_id][curr->node_id];
                total_cost += curr->int_cost;
            }
            total_benefit += curr->revenue;

            int arr = dep_time[i - 1] + data->time_matrix[prev->node_id][curr->node_id];
            arr_time[i] = arr;
            wait_time[i] = max(0, curr->e - arr);
            int start = max(arr, curr->e);

            if (start > curr->l)
            {
                is_feasible = false;
            }

            dep_time[i] = start + curr->duration;

            load += curr->weight;
            current_load[i] = load;
            if (load > capacity || load < 0)
            {
                is_feasible = false;
            }
        }

        total_benefit -= total_cost;

        // Quét lùi (Backward Sweep)
        max_delay[n - 1] = nodes[n - 1]->l - max(arr_time[n - 1], nodes[n - 1]->e);
        for (int i = n - 2; i >= 0; i--)
        {
            int start = max(arr_time[i], nodes[i]->e);
            int limit1 = nodes[i]->l - start;
            int limit2 = wait_time[i + 1] + max_delay[i + 1];
            max_delay[i] = min(limit1, limit2);
        }
    }
};

// === CÔNG CỤ TOÁN HỌC O(1) ===
namespace Evaluator
{
pair<long long, int> evaluate_passenger(const Route& route, const GenericNode* p_node,
                                        const Data& data,
                                        const map<pair<int, int>, int>& tabu_edges)
{
    long long best_benefit = -999999;
    int best_pos = -1;
    int n = route.nodes.size();

    for (int i = 1; i < n; i++)
    {
        GenericNode* prev = route.nodes[i - 1];
        GenericNode* next_node = route.nodes[i];


        assert(!(prev->node_id >= data.V_mac || p_node->node_id >= data.V_mac ||
            next_node->node_id >= data.V_mac));

        int arr_p = max(p_node->e,
                        route.dep_time[i - 1] + data.time_matrix[prev->node_id][p_node->node_id]);
        if (arr_p > p_node->l) continue;

        int dep_p = arr_p + p_node->duration;
        int arr_next = dep_p + data.time_matrix[p_node->node_id][next_node->node_id];

        if (!tabu_edges.empty())
        {
            if (tabu_edges.count({prev->node_id, p_node->node_id}) ||
                tabu_edges.count({p_node->node_id, next_node->node_id}))
            {
                continue;
            }
        }

        int delta_time = arr_next - route.arr_time[i];
        if (delta_time > route.wait_time[i] + route.max_delay[i]) continue;
        if (route.capacity < 0) continue;

        int c_prev_p = data.cost_matrix[prev->node_id][p_node->node_id];
        int c_p_next = 0;
        int c_prev_next = 0;
        if (next_node->type != DEPOT)
        {
            c_p_next = data.cost_matrix[p_node->node_id][next_node->node_id];
            c_prev_next = data.cost_matrix[prev->node_id][next_node->node_id];
        }

        int delta_cost = c_prev_p + c_p_next - c_prev_next + p_node->int_cost;
        long long delta_benefit = p_node->revenue - delta_cost;

        if (delta_benefit > best_benefit)
        {
            best_benefit = delta_benefit;
            best_pos = i;
        }
    }
    return {best_benefit, best_pos};
}

tuple<long long, int, int> evaluate_parcel(const Route& route, const GenericNode* pick,
                                           const GenericNode* drop, const Data& data,
                                           const map<pair<int, int>, int>& tabu_edges)
{
    long long best_benefit = -999999;
    int best_p_pos = -1;
    int best_d_pos = -1;
    int n = route.nodes.size();

    for (int i = 1; i < n; i++)
    {
        GenericNode* prev_i = route.nodes[i - 1];
        // CHÚ Ý INDEX
        assert(!(prev_i->node_id >= data.V_mac || pick->node_id >= data.V_mac));

        int arr_p =
            max(pick->e, route.dep_time[i - 1] + data.time_matrix[prev_i->node_id][pick->node_id]);
        if (arr_p > pick->l) continue;

        int curr_dep = arr_p + pick->duration;
        const GenericNode* curr_node = pick;
        int sim_cost = data.cost_matrix[prev_i->node_id][pick->node_id];
        int orig_cost = 0;

        if (route.current_load[i - 1] + pick->weight > route.capacity) continue;

        bool time_valid = true;
        for (int j = i; j < n; j++)
        {
            if (!time_valid) break;
            if (route.current_load[j - 1] + pick->weight > route.capacity) break;

            // CHÚ Ý INDEX
            assert(!(curr_node->node_id >= data.V_mac || drop->node_id >= data.V_mac));

            int arr_d =
                max(drop->e, curr_dep + data.time_matrix[curr_node->node_id][drop->node_id]);
            if (arr_d <= drop->l)
            {
                int sim_cost_drop = sim_cost + data.cost_matrix[curr_node->node_id][drop->node_id];
                int dep_d = arr_d + drop->duration;

                GenericNode* next_j = route.nodes[j];
                // CHÚ Ý INDEX
                assert(!(drop->node_id >= data.V_mac || next_j->node_id >= data.V_mac));

                int arr_next = dep_d + data.time_matrix[drop->node_id][next_j->node_id];
                int delta_time = arr_next - route.arr_time[j];

                if (delta_time <= route.wait_time[j] + route.max_delay[j])
                {
                    int sim_cost_full = sim_cost_drop;
                    if (next_j->type != DEPOT)
                    {
                        sim_cost_full += data.cost_matrix[drop->node_id][next_j->node_id];
                    }

                    int orig_cost_full = orig_cost;
                    if (i == j)
                    {
                        if (next_j->type != DEPOT)
                            orig_cost_full = data.cost_matrix[prev_i->node_id][next_j->node_id];
                        else
                            orig_cost_full = 0;
                    }
                    else
                    {
                        if (next_j->type != DEPOT)
                            orig_cost_full +=
                                data.cost_matrix[route.nodes[j - 1]->node_id][next_j->node_id];
                    }

                    int delta_cost = sim_cost_full - orig_cost_full;
                    long long delta_benefit = drop->revenue - delta_cost;

                    bool is_tabu = false;
                    if (!tabu_edges.empty())
                    {
                        int p_prev_req = prev_i->node_id;
                        int p_next_req = (j == i) ? drop->node_id : route.nodes[i]->node_id;
                        int d_prev_req = (j == i) ? pick->node_id : route.nodes[j - 1]->node_id;
                        int d_next_req = next_j->node_id;

                        if (tabu_edges.count({p_prev_req, pick->node_id}) ||
                            tabu_edges.count({pick->node_id, p_next_req}) ||
                            tabu_edges.count({d_prev_req, drop->node_id}) ||
                            tabu_edges.count({drop->node_id, d_next_req}))
                        {
                            is_tabu = true;
                        }
                    }

                    if (!is_tabu && delta_benefit > best_benefit)
                    {
                        best_benefit = delta_benefit;
                        best_p_pos = i;
                        best_d_pos = j;
                    }
                }
            }

            GenericNode* node_j = route.nodes[j];
            // CHÚ Ý INDEX
            assert(!(node_j->node_id >= data.V_mac || prev_i->node_id >= data.V_mac));

            if (j == i)
                orig_cost += data.cost_matrix[prev_i->node_id][node_j->node_id];
            else
                orig_cost += data.cost_matrix[route.nodes[j - 1]->node_id][node_j->node_id];

            int arr =
                max(node_j->e, curr_dep + data.time_matrix[curr_node->node_id][node_j->node_id]);
            if (arr > node_j->l)
            {
                time_valid = false;
                break;
            }

            sim_cost += data.cost_matrix[curr_node->node_id][node_j->node_id];
            curr_dep = arr + node_j->duration;
            curr_node = node_j;
        }
    }
    return {best_benefit, best_p_pos, best_d_pos};
}
}  // namespace Evaluator

// === TOÁN TỬ GIAO CHÉO SÂU (OPERATOR X) ===
namespace OperatorX
{
struct PoolReq
{
    bool is_parcel;
    GenericNode* p_node;
    GenericNode* pick;
    GenericNode* drop;
};

struct PathResult
{
    long long ben;
    int cost;
    vector<int> path;
};

void get_valid_routes(const vector<PoolReq>& pool_requests, const vector<GenericNode*>& pool_nodes,
                      GenericNode* depot_node, int vehicle_cap, const Data& data,
                      map<int, PathResult>& best_routes)
{
    int R = pool_requests.size();
    vector<int> req_of_node;
    vector<bool> is_dropoff;
    vector<bool> is_pickup;
    map<int, int> pickup_idx_of_dropoff;

    int idx = 0;
    for (int req_id = 0; req_id < R; req_id++)
    {
        if (pool_requests[req_id].is_parcel)
        {
            req_of_node.push_back(req_id);
            req_of_node.push_back(req_id);
            is_pickup.push_back(true);
            is_pickup.push_back(false);
            is_dropoff.push_back(false);
            is_dropoff.push_back(true);
            pickup_idx_of_dropoff[idx + 1] = idx;
            idx += 2;
        }
        else
        {
            req_of_node.push_back(req_id);
            is_pickup.push_back(false);
            is_dropoff.push_back(false);
            idx += 1;
        }
    }

    int V = pool_nodes.size();
    assert(!(V == 0)); // CHÚ Ý INDEX

    vector<int> prereq(V, 0);
    for (int i = 0; i < V; i++)
    {
        if (is_dropoff[i]) prereq[i] = 1 << pickup_idx_of_dropoff[i];
    }

    unordered_map<int, vector<pair<int, int>>> memo;
    best_routes[0] = {0, 0, {}};

    // DP Bitmask DFS
    auto dfs = [&](auto& self, int mask, int u_idx, int current_time, int current_cost,
                   int current_load, long long current_profit, int req_mask, int open_parcels,
                   vector<int>& path) -> void
    {
        int state_key = (mask << 5) | (u_idx + 1);  // Tránh âm
        auto it = memo.find(state_key);
        if (it != memo.end())
        {
            auto& state_history = it->second;
            for (auto& tc : state_history)
            {
                if (tc.first <= current_time && tc.second <= current_cost) return;  // Pruning
            }
            vector<pair<int, int>> new_hist;
            for (auto& tc : state_history)
            {
                if (!(current_time <= tc.first && current_cost <= tc.second))
                {
                    new_hist.push_back(tc);
                }
            }
            new_hist.push_back({current_time, current_cost});
            state_history = move(new_hist);
        }
        else
        {
            memo[state_key] = {{current_time, current_cost}};
        }

        if (open_parcels == 0)
        {
            GenericNode* u_node =
                (u_idx >= 0 && u_idx < V) ? pool_nodes[u_idx] : depot_node;  // CHÚ Ý INDEX
            int ret_cost =
                current_cost + ((u_node->type == DEPOT)
                                    ? 0
                                    : data.cost_matrix[u_node->node_id][depot_node->node_id]);
            long long ret_ben = current_profit - ret_cost;

            if (best_routes.find(req_mask) == best_routes.end() ||
                ret_ben > best_routes[req_mask].ben)
            {
                best_routes[req_mask] = {ret_ben, ret_cost, path};
            }
        }

        GenericNode* u_node = (u_idx >= 0 && u_idx < V) ? pool_nodes[u_idx] : depot_node;
        int unvisited = (~mask) & ((1 << V) - 1);

        while (unvisited > 0)
        {
            int v_bit = unvisited & -unvisited;
            unvisited ^= v_bit;

            // Bit trick tìm index thay thế cho ctz
            int v = 0;
            int temp_v = v_bit;
            while (!(temp_v & 1))
            {
                v++;
                temp_v >>= 1;
            }

            assert(!(v >= V));  // CHÚ Ý INDEX
            if (prereq[v] && (mask & prereq[v]) == 0) continue;

            GenericNode* v_node = pool_nodes[v];
            if (current_load + v_node->weight > vehicle_cap || current_load + v_node->weight < 0)
                continue;

            int arr = current_time + data.time_matrix[u_node->node_id][v_node->node_id];
            if (arr > v_node->l) continue;

            int start_time = max(arr, v_node->e);
            int nxt_time = start_time + v_node->duration;
            int nxt_cost = current_cost + data.cost_matrix[u_node->node_id][v_node->node_id] +
                           v_node->int_cost;
            long long nxt_profit = current_profit + v_node->revenue;

            int nxt_req_mask = req_mask;
            int nxt_open_parcels = open_parcels;
            int req_id = req_of_node[v];

            if (is_pickup[v])
                nxt_open_parcels |= (1 << req_id);
            else if (is_dropoff[v])
            {
                nxt_open_parcels &= ~(1 << req_id);
                nxt_req_mask |= (1 << req_id);
            }
            else
                nxt_req_mask |= (1 << req_id);

            path.push_back(v);
            self(self, mask | v_bit, v, nxt_time, nxt_cost, current_load + v_node->weight,
                 nxt_profit, nxt_req_mask, nxt_open_parcels, path);
            path.pop_back();
        }
    };
    vector<int> initial_path;
    dfs(dfs, 0, -1, depot_node->e, 0, 0, 0, 0, 0, initial_path);
}
}  // namespace OperatorX

// === BỘ GIẢI THUẬT TỐI ƯU CỤC BỘ (LSSOLVER) ===
class LSSolver
{
   public:
    Data& data;
    vector<Route> best_routes;
    long long best_benefit;
    vector<string> log_history;
    unordered_map<int, pair<GenericNode*, GenericNode*>> parcel_dict;

    struct InsertCacheRecord
    {
        uint64_t ver_id;
        long long ben;
        int p_pos;
        int d_pos;
    };
    unordered_map<int, unordered_map<int, InsertCacheRecord>> insert_cache;
    unordered_set<int> clean_routes;
    unordered_set<int> oropt_clean_routes;
    unordered_map<int, GenericNode*> req_dict;

    std::chrono::_V2::system_clock::time_point t_solve_start = chrono::high_resolution_clock::now();

    LSSolver(Data& _data) : data(_data), best_benefit(-999999)
    {
        for (auto p : data.parcels) parcel_dict[p.first->job_id] = p;
        for (auto p : data.passengers) req_dict[p->job_id] = p;
        for (auto p : data.parcels) req_dict[p.first->job_id] = p.first;
    }

    void log_and_print(const string& msg)
    {
        if (!RELEASE_MODE) cout << msg << endl;
        log_history.push_back(msg);
    }

    int best_insert(vector<Route>& routes, vector<GenericNode*>& unserved_passengers,
                    vector<pair<GenericNode*, GenericNode*>>& unserved_parcels,
                    const map<pair<int, int>, int>& active_tabu_edges, bool use_gravity,
                    bool only_profitable)
    {
        int inserted_count = 0;
        long long threshold = only_profitable ? 0 : -999999;

        while (true)
        {
            struct Cand
            {
                NodeType type;
                int idx;
                int r_idx;
                int p1;
                int p2;
                long long ben;
                GenericNode* req;
                GenericNode* pick;
                GenericNode* drop;
            };
            vector<Cand> top_candidates;

            auto add_candidate = [&](Cand cand)
            {
                if (top_candidates.size() < 5)
                {
                    top_candidates.push_back(cand);
                    sort(top_candidates.begin(), top_candidates.end(),
                         [](const Cand& a, const Cand& b) { return a.ben > b.ben; });
                }
                else if (cand.ben > top_candidates.back().ben)
                {
                    top_candidates.back() = cand;
                    sort(top_candidates.begin(), top_candidates.end(),
                         [](const Cand& a, const Cand& b) { return a.ben > b.ben; });
                }
            };

            // PASSENGERS
            for (int idx = 0; idx < unserved_passengers.size(); idx++)
            {
                GenericNode* p_node = unserved_passengers[idx];
                for (int r_idx = 0; r_idx < routes.size(); r_idx++)
                {
                    Route& route = routes[r_idx];
                    auto& r_cache = insert_cache[p_node->node_id];
                    if (r_cache.find(r_idx) == r_cache.end() ||
                        r_cache[r_idx].ver_id != route.ver_id)
                    {
                        map<pair<int, int>, int> empty_tabu;
                        auto [ben, pos] =
                            Evaluator::evaluate_passenger(route, p_node, data, empty_tabu);
                        r_cache[r_idx] = {route.ver_id, ben, pos, -1};
                    }
                }

                long long p_best_ben = -999999;
                int p_best_pos = -1;
                int p_best_rid = -1;

                for (auto& [r_idx, res] : insert_cache[p_node->node_id])
                {
                    // CHÚ Ý INDEX
                    assert(!(r_idx >= routes.size()));
                    Route& route = routes[r_idx];

                    if (res.ver_id == route.ver_id)
                    {
                        long long ben = res.ben;
                        int pos = res.p_pos;

                        if (ben > -999999 && !active_tabu_edges.empty())
                        {
                            // CHÚ Ý INDEX
                            if (pos >= 1 && pos < route.nodes.size())
                            {
                                int prev_id = route.nodes[pos - 1]->node_id;
                                int next_id = route.nodes[pos]->node_id;
                                if (active_tabu_edges.count({prev_id, p_node->node_id}) ||
                                    active_tabu_edges.count({p_node->node_id, next_id}))
                                {
                                    auto [n_ben, n_pos] = Evaluator::evaluate_passenger(
                                        route, p_node, data, active_tabu_edges);
                                    ben = n_ben;
                                    pos = n_pos;
                                }
                            }
                        }

                        if (ben > -999999 && use_gravity) ben += GRAVITY_ALPHA / route.nodes.size();
                        if (ben > p_best_ben)
                        {
                            p_best_ben = ben;
                            p_best_pos = pos;
                            p_best_rid = r_idx;
                        }
                    }
                }
                if (p_best_ben > threshold)
                    add_candidate({PASSENGER, idx, p_best_rid, p_best_pos, -1, p_best_ben, p_node,
                                   nullptr, nullptr});
            }

            // PARCELS
            for (int idx = 0; idx < unserved_parcels.size(); idx++)
            {
                GenericNode* pick = unserved_parcels[idx].first;
                GenericNode* drop = unserved_parcels[idx].second;

                for (int r_idx = 0; r_idx < routes.size(); r_idx++)
                {
                    Route& route = routes[r_idx];
                    auto& r_cache = insert_cache[pick->node_id];
                    if (r_cache.find(r_idx) == r_cache.end() ||
                        r_cache[r_idx].ver_id != route.ver_id)
                    {
                        map<pair<int, int>, int> empty_tabu;
                        auto [ben, p_pos, d_pos] =
                            Evaluator::evaluate_parcel(route, pick, drop, data, empty_tabu);
                        r_cache[r_idx] = {route.ver_id, ben, p_pos, d_pos};
                    }
                }

                long long p_best_ben = -999999;
                int p_best_p_pos = -1;
                int p_best_d_pos = -1;
                int p_best_rid = -1;

                for (auto& [r_idx, res] : insert_cache[pick->node_id])
                {
                    if (r_idx >= routes.size()) continue;
                    Route& route = routes[r_idx];

                    if (res.ver_id == route.ver_id)
                    {
                        long long ben = res.ben;
                        int p_pos = res.p_pos;
                        int d_pos = res.d_pos;

                        if (ben > -999999 && !active_tabu_edges.empty())
                        {
                            // CHÚ Ý INDEX BOUNDS
                            if (p_pos >= 1 && p_pos < route.nodes.size() && d_pos >= 1 &&
                                d_pos < route.nodes.size())
                            {
                                int prev_pick = route.nodes[p_pos - 1]->node_id;
                                int next_pick = route.nodes[p_pos]->node_id;
                                int prev_drop = (d_pos > p_pos) ? route.nodes[d_pos - 1]->node_id
                                                                : pick->node_id;
                                int next_drop = route.nodes[d_pos]->node_id;

                                if (active_tabu_edges.count({prev_pick, pick->node_id}) ||
                                    active_tabu_edges.count({pick->node_id, next_pick}) ||
                                    active_tabu_edges.count({prev_drop, drop->node_id}) ||
                                    active_tabu_edges.count({drop->node_id, next_drop}))
                                {
                                    auto [n_ben, n_p, n_d] = Evaluator::evaluate_parcel(
                                        route, pick, drop, data, active_tabu_edges);
                                    ben = n_ben;
                                    p_pos = n_p;
                                    d_pos = n_d;
                                }
                            }
                        }
                        if (ben > -999999 && use_gravity) ben += GRAVITY_ALPHA / route.nodes.size();
                        if (ben > p_best_ben)
                        {
                            p_best_ben = ben;
                            p_best_p_pos = p_pos;
                            p_best_d_pos = d_pos;
                            p_best_rid = r_idx;
                        }
                    }
                }
                if (p_best_ben > threshold)
                    add_candidate({PARCEL_PICKUP, idx, p_best_rid, p_best_p_pos, p_best_d_pos,
                                   p_best_ben, nullptr, pick, drop});
            }

            if (top_candidates.empty()) break;

            uniform_int_distribution<int> dist_k(2, 3);
            int TOP_K = min(dist_k(rng), (int)top_candidates.size());
            vector<Cand> k_candidates(top_candidates.begin(), top_candidates.begin() + TOP_K);

            long long min_ben = k_candidates.back().ben;
            vector<double> weights;
            for (auto& c : k_candidates)
            {
                if (min_ben > 0)
                    weights.push_back(c.ben);
                else
                    weights.push_back(c.ben - min_ben + 1);
            }

            discrete_distribution<int> dist_w(weights.begin(), weights.end());
            int chosen_idx = dist_w(rng);
            Cand chosen = k_candidates[chosen_idx];

            // CHÚ Ý INDEX: Đảm bảo xe hợp lệ
            assert(!(chosen.r_idx < 0 || chosen.r_idx >= routes.size()));
            Route& r = routes[chosen.r_idx];

            if (chosen.type == PASSENGER)
            {
                // Xoá khách hàng khỏi danh sách rớt. (CHÚ Ý INDEX: Dùng find để xoá an
                // toàn)
                auto it = find(unserved_passengers.begin(), unserved_passengers.end(), chosen.req);
                if (it != unserved_passengers.end()) unserved_passengers.erase(it);

                int insert_pos = min(chosen.p1, (int)r.nodes.size() - 1);
                r.nodes.insert(r.nodes.begin() + insert_pos, chosen.req);
            }
            else
            {
                auto it = find_if(unserved_parcels.begin(), unserved_parcels.end(),
                                  [&](const pair<GenericNode*, GenericNode*>& p)
                                  { return p.first == chosen.pick && p.second == chosen.drop; });
                if (it != unserved_parcels.end()) unserved_parcels.erase(it);

                int p1 = min(chosen.p1, (int)r.nodes.size() - 1);
                r.nodes.insert(r.nodes.begin() + p1, chosen.pick);
                int p2 = chosen.p2 + ((chosen.p2 >= chosen.p1) ? 1 : 0);
                p2 = min(p2, (int)r.nodes.size() - 1);
                r.nodes.insert(r.nodes.begin() + p2, chosen.drop);
            }

            r.update_states(&data);
            inserted_count++;
            clean_routes.erase(chosen.r_idx);
            oropt_clean_routes.erase(chosen.r_idx);
        }
        return inserted_count;
    }

    int _delta_cost_remove(const Route& route, int i, const Data& data)
    {
        // CHÚ Ý INDEX
        assert(!(i < 1 || i >= route.nodes.size() - 1));

        GenericNode* prev = route.nodes[i - 1];
        GenericNode* curr = route.nodes[i];
        GenericNode* nxt = route.nodes[i + 1];

        int cost_remove = data.cost_matrix[prev->node_id][curr->node_id] + curr->int_cost;
        int cost_save = 0;
        if (nxt->type != DEPOT)
        {
            cost_remove += data.cost_matrix[curr->node_id][nxt->node_id];
            cost_save = data.cost_matrix[prev->node_id][nxt->node_id];
        }

        return (cost_remove - cost_save) - curr->revenue;
    }

    bool or_opt_k1(vector<Route>& routes, const Data& data)
    {
        bool improved_total = false;
        for (int r_idx = 0; r_idx < routes.size(); r_idx++)
        {
            if (oropt_clean_routes.count(r_idx)) continue;

            Route& route = routes[r_idx];
            bool improved = true;
            int safe_counter = 0;

            while (improved && safe_counter < 100)
            {
                safe_counter++;
                improved = false;
                int n = route.nodes.size();
                if (n <= 3) break;

                long long best_delta = 0;
                NodeType best_move_type = DEPOT;
                int best_move_src = -1, best_move_dst = -1;
                vector<GenericNode*> best_move_nodes;

                for (int src = 1; src < n - 1; src++)
                {
                    GenericNode* node = route.nodes[src];
                    if (node->type == PARCEL_DROPOFF) continue;

                    if (node->type == PASSENGER)
                    {
                        GenericNode* prev_s = route.nodes[src - 1];
                        GenericNode* next_s = route.nodes[src + 1];

                        int cost_remove =
                            data.cost_matrix[prev_s->node_id][node->node_id] + node->int_cost;
                        int cost_bridge = 0;
                        if (next_s->type != DEPOT)
                        {
                            cost_remove += data.cost_matrix[node->node_id][next_s->node_id];
                            cost_bridge = data.cost_matrix[prev_s->node_id][next_s->node_id];
                        }
                        int gain_remove = cost_remove - cost_bridge;

                        for (int dst = 1; dst < n - 1; dst++)
                        {
                            if (dst == src || dst == src - 1) continue;

                            GenericNode* real_dst_prev =
                                (dst <= src) ? route.nodes[dst - 1] : route.nodes[dst];
                            GenericNode* real_dst_next =
                                (dst <= src) ? route.nodes[dst] : route.nodes[dst + 1];

                            int cost_insert =
                                data.cost_matrix[real_dst_prev->node_id][node->node_id] +
                                node->int_cost;
                            if (real_dst_next->type != DEPOT)
                            {
                                cost_insert +=
                                    data.cost_matrix[node->node_id][real_dst_next->node_id] -
                                    data.cost_matrix[real_dst_prev->node_id]
                                                    [real_dst_next->node_id];
                            }

                            int delta = gain_remove - cost_insert;

                            if (delta > best_delta)
                            {
                                vector<GenericNode*> test_nodes = route.nodes;
                                test_nodes.erase(test_nodes.begin() + src);
                                int insert_at = (dst < src) ? dst : dst;
                                // CHÚ Ý INDEX
                                if (insert_at < test_nodes.size())
                                {
                                    test_nodes.insert(test_nodes.begin() + insert_at, node);
                                    Route test_route = route.clone();
                                    test_route.nodes = test_nodes;
                                    test_route.update_states(&data);
                                    if (test_route.is_feasible &&
                                        test_route.total_benefit > route.total_benefit)
                                    {
                                        best_delta = test_route.total_benefit - route.total_benefit;
                                        best_move_type = PASSENGER;
                                        best_move_nodes = test_nodes;
                                    }
                                }
                            }
                        }
                    }
                    else if (node->type == PARCEL_PICKUP)
                    {
                        int d_idx = -1;
                        for (int j = src + 1; j < n - 1; j++)
                        {
                            if (route.nodes[j]->type == PARCEL_DROPOFF &&
                                route.nodes[j]->job_id == node->job_id)
                            {
                                d_idx = j;
                                break;
                            }
                        }
                        if (d_idx == -1) continue;

                        GenericNode* drop = route.nodes[d_idx];
                        long long orig_ben = route.total_benefit;
                        vector<GenericNode*> base_nodes;
                        for (int k = 0; k < n; k++)
                        {
                            if (k != src && k != d_idx) base_nodes.push_back(route.nodes[k]);
                        }

                        Route test_route = route.clone();
                        test_route.nodes = base_nodes;
                        test_route.update_states(&data);

                        map<pair<int, int>, int> empty_tabu;
                        auto [ben_insert, p_pos, d_pos] =
                            Evaluator::evaluate_parcel(test_route, node, drop, data, empty_tabu);
                        if (ben_insert <= -999999) continue;

                        long long new_total_ben = test_route.total_benefit + ben_insert;
                        if (new_total_ben > orig_ben)
                        {
                            long long delta = new_total_ben - orig_ben;
                            if (delta > best_delta)
                            {
                                best_delta = delta;
                                vector<GenericNode*> test_nodes = base_nodes;
                                // CHÚ Ý INDEX
                                p_pos = min(p_pos, (int)test_nodes.size() - 1);
                                test_nodes.insert(test_nodes.begin() + p_pos, node);
                                int real_d_pos = d_pos + ((d_pos >= p_pos) ? 1 : 0);
                                real_d_pos = min(real_d_pos, (int)test_nodes.size() - 1);
                                test_nodes.insert(test_nodes.begin() + real_d_pos, drop);

                                best_move_type = PARCEL_PICKUP;
                                best_move_nodes = test_nodes;
                            }
                        }
                    }
                }

                if (best_delta > 0 && best_move_nodes.size() > 0)
                {
                    route.nodes = best_move_nodes;
                    route.update_states(&data);
                    improved = true;
                    improved_total = true;
                }
            }
            oropt_clean_routes.insert(r_idx);
        }
        return improved_total;
    }

    int exhaustive_remove(vector<Route>& routes, vector<GenericNode*>& unserved_pass,
                          vector<pair<GenericNode*, GenericNode*>>& unserved_parc, const Data& data)
    {
        int total_removed = 0;
        for (int r_idx = 0; r_idx < routes.size(); r_idx++)
        {
            if (clean_routes.count(r_idx)) continue;

            Route& route = routes[r_idx];
            bool route_changed = true;
            while (route_changed)
            {
                route_changed = false;
                int i = 1;
                while (i < route.nodes.size() - 1)
                {  // CHÚ Ý INDEX
                    GenericNode* node = route.nodes[i];
                    if (node->type == DEPOT || node->type == PARCEL_DROPOFF)
                    {
                        i++;
                        continue;
                    }

                    if (node->type == PASSENGER)
                    {
                        int delta = _delta_cost_remove(route, i, data);
                        if (delta > 0)
                        {
                            route.nodes.erase(route.nodes.begin() + i);
                            route.update_states(&data);
                            unserved_pass.push_back(node);
                            total_removed++;
                            route_changed = true;
                            continue;
                        }
                    }
                    else if (node->type == PARCEL_PICKUP)
                    {
                        int d_idx = -1;
                        for (int j = i + 1; j < route.nodes.size() - 1; j++)
                        {
                            if (route.nodes[j]->type == PARCEL_DROPOFF &&
                                route.nodes[j]->job_id == node->job_id)
                            {
                                d_idx = j;
                                break;
                            }
                        }
                        if (d_idx != -1)
                        {
                            long long orig_ben = route.total_benefit;
                            GenericNode* drop_node = route.nodes[d_idx];
                            vector<GenericNode*> test_nodes;
                            for (auto n : route.nodes)
                            {
                                if (n != node && n != drop_node) test_nodes.push_back(n);
                            }
                            Route test_route = route.clone();
                            test_route.nodes = test_nodes;
                            test_route.update_states(&data);

                            if (test_route.total_benefit > orig_ben)
                            {
                                route.nodes = test_nodes;
                                route.update_states(&data);
                                unserved_parc.push_back(parcel_dict[node->job_id]);
                                total_removed++;
                                route_changed = true;
                                continue;
                            }
                        }
                    }
                    i++;
                }
            }
            clean_routes.insert(r_idx);
        }
        return total_removed;
    }

    int random_perturbation(vector<Route>& routes, vector<GenericNode*>& unserved_pass,
                            vector<pair<GenericNode*, GenericNode*>>& unserved_parc,
                            map<pair<int, int>, int>& tabu_dict, int current_iter)
    {
        unordered_set<int> served_reqs;
        for (auto& r : routes)
        {
            for (int i = 1; i < (int)r.nodes.size() - 1; i++)
            {
                served_reqs.insert(r.nodes[i]->job_id);
            }
        }
        if (served_reqs.empty()) return 0;

        vector<int> served_list(served_reqs.begin(), served_reqs.end());
        int num_remove = max(3, (int)(served_list.size() * MUTATION_RATE));
        if (num_remove >= served_list.size()) {
            if (served_list.size() > 1) num_remove = served_list.size() - 1;
            else num_remove = served_list.size();
        }

        shuffle(served_list.begin(), served_list.end(), rng);
        unordered_set<int> to_remove(served_list.begin(), served_list.begin() + num_remove);

        for (auto& r : routes)
        {
            bool route_changed = false;
            vector<GenericNode*> new_nodes;
            for (int i = 0; i < r.nodes.size(); i++)
            {  // CHÚ Ý INDEX
                GenericNode* n = r.nodes[i];
                if (n->type != DEPOT && to_remove.count(n->job_id))
                {
                    route_changed = true;
                    // Cấp quyền cấm Tabu
                    if (i > 0 && i < r.nodes.size() - 1)
                    {
                        tabu_dict[{r.nodes[i - 1]->node_id, r.nodes[i]->node_id}] =
                            current_iter + TABU_TENURE;
                        tabu_dict[{r.nodes[i]->node_id, r.nodes[i + 1]->node_id}] =
                            current_iter + TABU_TENURE;
                    }
                    if (n->type == PASSENGER)
                        unserved_pass.push_back(n);
                    else if (n->type == PARCEL_PICKUP)
                        unserved_parc.push_back(parcel_dict[n->job_id]);
                }
                else
                {
                    new_nodes.push_back(n);
                }
            }
            if (route_changed)
            {
                r.nodes = new_nodes;
                r.update_states(&data);
            }
        }
        return to_remove.size();
    }

    // Operator X gọi tích hợp
    bool run_operator_x(vector<Route>& routes, vector<GenericNode*>& unserved_pass,
                        vector<pair<GenericNode*, GenericNode*>>& unserved_parc)
    {
        auto tx_start = chrono::high_resolution_clock::now();
        vector<pair<int, int>> pairs;
        for (int i = 0; i < routes.size(); i++)
        {
            for (int j = i + 1; j < routes.size(); j++) pairs.push_back({i, j});
        }
        shuffle(pairs.begin(), pairs.end(), rng);

        bool improved_any = false;
        long long global_best_ben_diff = 0;

        struct BestMove
        {
            int r1_idx, r2_idx;
            vector<GenericNode*> pool_nodes;
            int best_m1, best_m2;
            map<int, OperatorX::PathResult> b1, b2;
            long long cur_ben, best_ben;
        };
        BestMove gmove;
        bool found_gmove = false;

        for (auto& p : pairs)
        {
            int r1_idx = p.first;
            int r2_idx = p.second;
            Route& r1 = routes[r1_idx];
            Route& r2 = routes[r2_idx];

            vector<GenericNode*> base_reqs_raw;
            unordered_set<int> job_ids;
            for (int i = 1; i < (int)r1.nodes.size() - 1; i++) job_ids.insert(r1.nodes[i]->job_id);
            for (int i = 1; i < (int)r2.nodes.size() - 1; i++) job_ids.insert(r2.nodes[i]->job_id);
            for (int jid : job_ids)
                if (req_dict.count(jid)) base_reqs_raw.push_back(req_dict[jid]);

            int base_phys = 0;
            for (auto r : base_reqs_raw) base_phys += (r->type == PARCEL_PICKUP ? 2 : 1);
            int MAX_PHYSICAL = 18;
            if (base_phys > MAX_PHYSICAL) continue;

            for (int pool_iter = 0; pool_iter < 10; pool_iter++)
            {
                vector<OperatorX::PoolReq> pool_reqs;
                for (auto r : base_reqs_raw)
                {
                    if (r->type == PASSENGER)
                        pool_reqs.push_back({false, r, nullptr, nullptr});
                    else
                        pool_reqs.push_back({true, nullptr, parcel_dict[r->job_id].first,
                                             parcel_dict[r->job_id].second});
                }

                vector<GenericNode*> p_u_pass = unserved_pass;
                vector<pair<GenericNode*, GenericNode*>> p_u_parc = unserved_parc;
                shuffle(p_u_pass.begin(), p_u_pass.end(), rng);
                shuffle(p_u_parc.begin(), p_u_parc.end(), rng);

                int cur_phys = base_phys;
                while (cur_phys < MAX_PHYSICAL)
                {
                    if (!p_u_pass.empty() && cur_phys + 1 <= MAX_PHYSICAL)
                    {
                        pool_reqs.push_back({false, p_u_pass.back(), nullptr, nullptr});
                        p_u_pass.pop_back();
                        cur_phys += 1;
                    }
                    else if (!p_u_parc.empty() && cur_phys + 2 <= MAX_PHYSICAL)
                    {
                        pool_reqs.push_back(
                            {true, nullptr, p_u_parc.back().first, p_u_parc.back().second});
                        p_u_parc.pop_back();
                        cur_phys += 2;
                    }
                    else
                    {
                        break;
                    }
                }

                vector<GenericNode*> pool_nodes;
                for (auto& pr : pool_reqs)
                {
                    if (pr.is_parcel)
                    {
                        pool_nodes.push_back(pr.pick);
                        pool_nodes.push_back(pr.drop);
                    }
                    else
                        pool_nodes.push_back(pr.p_node);
                }

                map<int, OperatorX::PathResult> b1_routes, b2_routes;
                OperatorX::get_valid_routes(pool_reqs, pool_nodes, r1.nodes[0], r1.capacity, data,
                                            b1_routes);
                OperatorX::get_valid_routes(pool_reqs, pool_nodes, r2.nodes[0], r2.capacity, data,
                                            b2_routes);

                long long best_ben = -999999;
                int best_m1 = -1, best_m2 = -1;

                for (auto& [m1, res1] : b1_routes)
                {
                    for (auto& [m2, res2] : b2_routes)
                    {
                        if ((m1 & m2) == 0 && res1.ben + res2.ben > best_ben)
                        {
                            best_ben = res1.ben + res2.ben;
                            best_m1 = m1;
                            best_m2 = m2;
                        }
                    }
                }
                long long cur_ben = r1.total_benefit + r2.total_benefit;
                long long ben_diff = best_ben - cur_ben;
                if (ben_diff > global_best_ben_diff)
                {
                    global_best_ben_diff = ben_diff;
                    gmove = {r1_idx,    r2_idx,    pool_nodes, best_m1, best_m2,
                             b1_routes, b2_routes, cur_ben,    best_ben};
                    found_gmove = true;
                }
            }
        }

        if (found_gmove)
        {
            Route& r1 = routes[gmove.r1_idx];
            Route& r2 = routes[gmove.r2_idx];
            vector<GenericNode*> n1 = {r1.nodes[0]};
            // CHÚ Ý INDEX
            for (int idx : gmove.b1[gmove.best_m1].path)
            {
                if (idx < gmove.pool_nodes.size()) n1.push_back(gmove.pool_nodes[idx]);
            }
            n1.push_back(r1.nodes.back());
            r1.nodes = n1;
            r1.update_states(&data);

            vector<GenericNode*> n2 = {r2.nodes[0]};
            for (int idx : gmove.b2[gmove.best_m2].path)
            {
                if (idx < gmove.pool_nodes.size()) n2.push_back(gmove.pool_nodes[idx]);
            }
            n2.push_back(r2.nodes.back());
            r2.nodes = n2;
            r2.update_states(&data);

            unordered_set<int> served;
            for (auto& r : routes)
                for (int i = 1; i < (int)r.nodes.size() - 1; i++) served.insert(r.nodes[i]->job_id);
            unserved_pass.clear();
            unserved_parc.clear();
            for (auto p : data.passengers)
                if (!served.count(p->job_id)) unserved_pass.push_back(p);
            for (auto p : data.parcels)
                if (!served.count(p.first->job_id)) unserved_parc.push_back(p);

            log_and_print("    [Opt X DFS] Record broken by X! Ben: " + to_string(gmove.cur_ben) +
                          " -> " + to_string(gmove.best_ben));
            return true;
        }
        return false;
    }

    void solve(int max_iterations = 100000)
    {
        t_solve_start = chrono::high_resolution_clock::now();

        vector<Route> routes;
        for (int i = 0; i < data.K; i++) {
            routes.push_back(Route(i, data.capacities[i], data.depots[i]));
            routes.back().update_states(&data);
        }

        vector<GenericNode*> unserved_pass = data.passengers;
        vector<pair<GenericNode*, GenericNode*>> unserved_parc = data.parcels;

        log_and_print("Init Greedy Tour...");
        map<pair<int, int>, int> empty_tabu;
        best_insert(routes, unserved_pass, unserved_parc, empty_tabu, true, false);

        best_benefit = 0;
        for (auto& r : routes) best_benefit += r.total_benefit;
        best_routes = routes;
        log_and_print("[Init] Initial Benefit: " + to_string(best_benefit));

        map<pair<int, int>, int> tabu_dict;

        for (int it = 0; it < max_iterations; it++)
        {
            chrono::duration<double> elapsed = chrono::high_resolution_clock::now() - t_solve_start;
            if (elapsed.count() > TIME_LIMIT)
            {
                log_and_print("\n[TIME LIMIT] Stopped due to exceeding " + to_string(TIME_LIMIT) +
                              "s at Iter " + to_string(it));
                break;
            }

            bool local_improved = true;
            // Xóa rác tabu (Garbage collection tabu list)
            for (auto it_map = tabu_dict.begin(); it_map != tabu_dict.end();)
            {
                if (it_map->second < it)
                    it_map = tabu_dict.erase(it_map);
                else
                    it_map++;
            }

            long long start_ben = 0;
            for (auto& r : routes) start_ben += r.total_benefit;
            int or_opt_count = 0, remove_count = 0, insert_count = 0;

            while (local_improved)
            {
                local_improved = false;
                if (or_opt_k1(routes, data))
                {
                    local_improved = true;
                    or_opt_count++;
                }

                int removed = exhaustive_remove(routes, unserved_pass, unserved_parc, data);
                int inserted =
                    best_insert(routes, unserved_pass, unserved_parc, tabu_dict, false, true);

                remove_count += removed;
                insert_count += inserted;
                if (removed > 0 || inserted > 0) local_improved = true;
            }

            long long current_benefit = 0;
            for (auto& r : routes) current_benefit += r.total_benefit;
            if (current_benefit > best_benefit)
            {
                best_benefit = current_benefit;
                best_routes = routes;
                log_and_print(
                    "[Iter " + to_string(it) + "] NEW RECORD: " + to_string(best_benefit) +
                    " | Unserved: " + to_string(unserved_pass.size() + unserved_parc.size()));

                bool improved_by_x = true;
                while (improved_by_x)
                {
                    improved_by_x = run_operator_x(best_routes, unserved_pass, unserved_parc);
                    if (improved_by_x)
                    {
                        routes = best_routes;  // Đồng bộ ngược
                        current_benefit = 0;
                        for (auto& r : routes) current_benefit += r.total_benefit;
                        best_benefit = current_benefit;
                        log_and_print("    [Opt X] Record continuously broken by X! New Ben: " +
                                      to_string(current_benefit));
                    }
                }
            }
            else
            {
                if (it % 100 == 0)
                    log_and_print("[Iter " + to_string(it) +
                                  "] Local Opt : " + to_string(current_benefit) +
                                  " (Best: " + to_string(best_benefit) + ")");
            }

            routes = best_routes;  // Phá luôn đi từ đỉnh
            served_reqs_rebuild(routes, unserved_pass, unserved_parc);
            random_perturbation(routes, unserved_pass, unserved_parc, tabu_dict, it);
            best_insert(routes, unserved_pass, unserved_parc, tabu_dict, false, false);
        }

        chrono::duration<double> final_elapsed =
            chrono::high_resolution_clock::now() - t_solve_start;
        log_and_print("--- FINAL RESULTS ---");
        log_and_print("Total Max Benefit: " + to_string(best_benefit));
        log_and_print("Total Run Time: " + to_string(final_elapsed.count()) + "s");

        if (RELEASE_MODE)
        {
            cout << best_benefit << endl;
            for (auto& r : best_routes)
            {
                if (r.nodes.size() <= 2)
                {
                    cout << "0" << endl;
                }
                else
                {
                    vector<int> out_path;
                    for (int i = 1; i < r.nodes.size() - 1; i++)
                    {  // CHÚ Ý INDEX
                        out_path.push_back(r.nodes[i]->physical_in + 1);
                        if (r.nodes[i]->type == PASSENGER)
                            out_path.push_back(r.nodes[i]->physical_out + 1);
                    }
                    cout << out_path.size();
                    for (int x : out_path) cout << " " << x;
                    cout << endl;
                }
            }
        }
        else
        {
            ofstream fout("output_tour.txt");
            fout << best_benefit << endl;
            for (auto& r : best_routes)
            {
                if (r.nodes.size() <= 2)
                {
                    fout << "0" << endl;
                }
                else
                {
                    vector<int> out_path;
                    for (int i = 1; i < r.nodes.size() - 1; i++)
                    {
                        out_path.push_back(r.nodes[i]->physical_in + 1);
                        if (r.nodes[i]->type == PASSENGER)
                            out_path.push_back(r.nodes[i]->physical_out + 1);
                    }
                    fout << out_path.size();
                    for (int x : out_path) fout << " " << x;
                    fout << endl;
                }
            }
            fout.close();

            ofstream flog("output_log.txt");
            for (auto& s : log_history) flog << s << "\n";
            flog.close();
            log_and_print("Output written to output_tour.txt and output_log.txt");
        }
    }

   private:
    void served_reqs_rebuild(vector<Route>& routes, vector<GenericNode*>& unserved_pass,
                             vector<pair<GenericNode*, GenericNode*>>& unserved_parc)
    {
        unordered_set<int> served;
        for (auto& r : routes)
            for (int i = 1; i < (int)r.nodes.size() - 1; i++) served.insert(r.nodes[i]->job_id);
        unserved_pass.clear();
        unserved_parc.clear();
        for (auto p : data.passengers)
            if (!served.count(p->job_id)) unserved_pass.push_back(p);
        for (auto p : data.parcels)
            if (!served.count(p.first->job_id)) unserved_parc.push_back(p);
    }
};

int main(int argc, char* argv[])
{
    // Để chương trình chạy nhanh hơn
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);

    string filename = (argc > 1) ? argv[1] : "";
    
    Data data(filename);

    if (RELEASE_MODE)
    {
        auto global_start = chrono::high_resolution_clock::now();
        long long overall_best_ben = -999999;
        string overall_best_output = "";
        
        while (true)
        {
            stringstream ss;
            streambuf* orig_cout = cout.rdbuf(ss.rdbuf());
            
            LSSolver solver(data);
            solver.solve(30000);
            
            cout.rdbuf(orig_cout);
            
            if (solver.best_benefit > overall_best_ben)
            {
                overall_best_ben = solver.best_benefit;
                overall_best_output = ss.str();
            }
            
            auto now = chrono::high_resolution_clock::now();
            chrono::duration<double> elapsed = now - global_start;
            // Dừng vòng lặp trước khi hết giờ (trừ hao 0.5s)
            if (elapsed.count() > TIME_LIMIT - 0.5) 
            {
                break;
            }
        }
        
        cout << overall_best_output;
    }
    else
    {
        LSSolver solver(data);
        solver.solve(30000);
    }
    
    return 0;
}
