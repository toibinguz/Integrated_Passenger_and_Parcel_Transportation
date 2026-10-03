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
bool RELEASE_MODE = false;

double TIME_LIMIT = 100.0;
int TABU_TENURE = 10;
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

// === TABU SEARCH SOLVER ===
class TabuSolver
{
public:
    Data& data;
    vector<Route> routes;
    vector<Route> best_routes;
    long long best_benefit;
    vector<string> log_history;

    vector<GenericNode*> req_dict;
    vector<pair<GenericNode*, GenericNode*>> parcel_dict;
    int MAX_JOB_ID;

    vector<vector<vector<int>>> tabu_list;
    vector<int> route_iter;

    TabuSolver(Data& data_ref) : data(data_ref)
    {
        MAX_JOB_ID = 0;
        for (auto p : data.passengers) MAX_JOB_ID = max(MAX_JOB_ID, p->job_id);
        for (auto p : data.parcels) MAX_JOB_ID = max(MAX_JOB_ID, p.first->job_id);
        
        req_dict.assign(MAX_JOB_ID + 1, nullptr);
        for (auto p : data.passengers) req_dict[p->job_id] = p;
        for (auto p : data.parcels) req_dict[p.first->job_id] = p.first;

        parcel_dict.assign(MAX_JOB_ID + 1, {nullptr, nullptr});
        for (auto p : data.parcels) parcel_dict[p.first->job_id] = p;

        tabu_list.assign(data.K, vector<vector<int>>(data.V_mac, vector<int>(data.V_mac, -1)));
        route_iter.assign(data.K, 0);
    }

    void log_and_print(const string& msg)
    {
        if (!RELEASE_MODE) {
            cout << msg << "\n";
            log_history.push_back(msg);
        }
    }

    Route insert_request(const Route& route, int job_id)
    {
        GenericNode* req = req_dict[job_id];
        if (!req) return Route(-1, -1, nullptr); // invalid

        if (req->type == PASSENGER) {
            auto [delta, pos] = Evaluator::evaluate_passenger(route, req, data, {});
            if (pos != -1) {
                Route r = route.clone();
                r.nodes.insert(r.nodes.begin() + pos, req);
                r.update_states(&data);
                return r;
            }
        } else {
            GenericNode* pick = parcel_dict[job_id].first;
            GenericNode* drop = parcel_dict[job_id].second;
            auto [delta, p_pos, d_pos] = Evaluator::evaluate_parcel(route, pick, drop, data, {});
            if (p_pos != -1) {
                Route r = route.clone();
                r.nodes.insert(r.nodes.begin() + p_pos, pick);
                int real_d_pos = d_pos + ((d_pos >= p_pos) ? 1 : 0);
                r.nodes.insert(r.nodes.begin() + real_d_pos, drop);
                r.update_states(&data);
                return r;
            }
        }
        Route r(-1, -1, nullptr); // returning a dummy route that is not feasible
        r.is_feasible = false;
        return r;
    }

    void init_regret_2(vector<bool>& is_served)
    {
        while (true) {
            vector<int> U;
            for (int j = 1; j <= MAX_JOB_ID; j++) {
                if (!is_served[j] && req_dict[j] != nullptr) U.push_back(j);
            }

            int best_u = -1;
            long long max_regret = -999999999;
            Route best_route_for_u(-1, -1, nullptr);
            best_route_for_u.is_feasible = false;
            int best_route_idx = -1;

            for (int u : U) {
                long long b1 = -999999999, b2 = -999999999;
                Route best_r(-1, -1, nullptr); 
                best_r.is_feasible = false;
                int best_k = -1;
                
                for (int k = 0; k < routes.size(); k++) {
                    Route r = insert_request(routes[k], u);
                    if (r.veh_id != -1 && r.is_feasible) {
                        long long ben = r.total_benefit - routes[k].total_benefit;
                        if (ben > b1) {
                            b2 = b1;
                            b1 = ben;
                            best_r = r;
                            best_k = k;
                        } else if (ben > b2) {
                            b2 = ben;
                        }
                    }
                }
                if (b1 > 0) {
                    long long regret = b1 - (b2 == -999999999 ? 0 : b2);
                    if (regret > max_regret) {
                        max_regret = regret;
                        best_u = u;
                        best_route_for_u = best_r;
                        best_route_idx = best_k;
                    }
                }
            }

            if (best_u == -1) break;

            routes[best_route_idx] = best_route_for_u;
            is_served[best_u] = true;
        }
    }

    struct BaseRoute {
        Route r;
        vector<int> ejected;
    };

    struct Move {
        int route_idx;
        vector<int> ejects;
        vector<int> injects;
        Route new_route;
        long long delta;
        bool is_tabu;
        bool valid;

        int tabu_route_idx;
        vector<int> tabu_ejects;
        vector<int> tabu_injects;
        Route tabu_new_route;
        long long tabu_delta;
        bool has_tabu_move;

        Move() : route_idx(-1), new_route(-1, -1, nullptr), delta(-999999999), is_tabu(false), valid(false),
                 tabu_route_idx(-1), tabu_new_route(-1, -1, nullptr), tabu_delta(-999999999), has_tabu_move(false) {}
    };

    void check_and_update_move(int k, const vector<int>& ejects, const vector<int>& injects, const Route& new_route, 
                               long long delta, Move& best_move, const Route& old_route, long long current_total_ben, 
                               long long best_known_ben) 
    {
        if (ejects.empty() && injects.empty()) return;

        bool is_tabu = false;
        
        for (int i=0; i < (int)new_route.nodes.size() - 1; i++) {
            int u = new_route.nodes[i]->node_id;
            int v = new_route.nodes[i+1]->node_id;
            
            bool has_edge = false;
            for(int j=0; j < (int)old_route.nodes.size() - 1; j++) {
                if (old_route.nodes[j]->node_id == u && old_route.nodes[j+1]->node_id == v) {
                    has_edge = true; break;
                }
            }

            if (!has_edge) {
                if (tabu_list[k][u][v] >= route_iter[k]) {
                    is_tabu = true; break;
                }
            }
        }

        if (is_tabu) {
            if (current_total_ben + delta <= best_known_ben) {
                if (delta > best_move.tabu_delta) {
                    best_move.tabu_route_idx = k;
                    best_move.tabu_ejects = ejects;
                    best_move.tabu_injects = injects;
                    best_move.tabu_new_route = new_route;
                    best_move.tabu_delta = delta;
                    best_move.has_tabu_move = true;
                }
                return; 
            }
        }
        
        if (delta > best_move.delta) {
            best_move.route_idx = k;
            best_move.ejects = ejects;
            best_move.injects = injects;
            best_move.new_route = new_route;
            best_move.delta = delta;
            best_move.is_tabu = is_tabu;
            best_move.valid = true;
        }
    }

    void solve(int max_iterations = 100000)
    {
        auto t_solve_start = chrono::high_resolution_clock::now();

        routes.clear();
        for (int i = 0; i < data.K; i++) {
            routes.push_back(Route(i, data.capacities[i], data.depots[i]));
            routes.back().update_states(&data);
        }

        vector<bool> is_served(MAX_JOB_ID + 1, false);
        init_regret_2(is_served);

        best_benefit = 0;
        for (auto& r : routes) best_benefit += r.total_benefit;
        best_routes = routes;
        
        log_and_print("[Init] Regret-2 Benefit: " + to_string(best_benefit));
        long long current_total_ben = best_benefit;

        for (int iter = 0; iter < max_iterations; iter++)
        {
            auto now = chrono::high_resolution_clock::now();
            chrono::duration<double> elapsed = now - t_solve_start;
            if (elapsed.count() > TIME_LIMIT) break;

            vector<int> U;
            for (int j = 1; j <= MAX_JOB_ID; j++) {
                if (!is_served[j] && req_dict[j] != nullptr) U.push_back(j);
            }

            Move best_move;

            for (int k = 0; k < data.K; k++) {
                Route& route = routes[k];
                vector<int> served_jobs;
                for (auto node : route.nodes) {
                    if (node->type != DEPOT && node->type != PARCEL_DROPOFF) {
                        served_jobs.push_back(node->job_id);
                    }
                }

                vector<BaseRoute> base_routes;
                
                // Eject 0
                base_routes.push_back({route, {}});

                // Eject 1
                for (int i=0; i<served_jobs.size(); i++) {
                    BaseRoute br{Route(-1,-1,nullptr), {}};
                    br.ejected = {served_jobs[i]};
                    br.r = route.clone();
                    
                    vector<GenericNode*> new_nodes;
                    for(auto n : br.r.nodes) {
                        if (n->job_id != served_jobs[i]) new_nodes.push_back(n);
                    }
                    br.r.nodes = new_nodes;
                    br.r.update_states(&data);
                    base_routes.push_back(br);
                }

                // Eject 2
                for (int i=0; i<served_jobs.size(); i++) {
                    for (int j=i+1; j<served_jobs.size(); j++) {
                        BaseRoute br{Route(-1,-1,nullptr), {}};
                        br.ejected = {served_jobs[i], served_jobs[j]};
                        br.r = route.clone();
                        vector<GenericNode*> new_nodes;
                        for(auto n : br.r.nodes) {
                            if (n->job_id != served_jobs[i] && n->job_id != served_jobs[j]) new_nodes.push_back(n);
                        }
                        br.r.nodes = new_nodes;
                        br.r.update_states(&data);
                        base_routes.push_back(br);
                    }
                }

                int num_bases = base_routes.size();
                vector<vector<Route>> memo1(num_bases, vector<Route>(U.size(), Route(-1,-1,nullptr)));
                for (int b = 0; b < num_bases; b++) {
                    for (int ui = 0; ui < U.size(); ui++) {
                        memo1[b][ui] = insert_request(base_routes[b].r, U[ui]);
                    }
                }

                long long route_current_ben = route.total_benefit;

                for (int b = 0; b < num_bases; b++) {
                    const auto& br = base_routes[b];
                    
                    if (!br.ejected.empty()) {
                        long long delta = br.r.total_benefit - route_current_ben;
                        check_and_update_move(k, br.ejected, {}, br.r, delta, best_move, route, current_total_ben, best_benefit);
                    }

                    for (int ui = 0; ui < U.size(); ui++) {
                        const Route& r1 = memo1[b][ui];
                        if (r1.veh_id != -1 && r1.is_feasible) {
                            long long delta = r1.total_benefit - route_current_ben;
                            check_and_update_move(k, br.ejected, {U[ui]}, r1, delta, best_move, route, current_total_ben, best_benefit);
                        }
                    }

                    for (int ui1 = 0; ui1 < U.size(); ui1++) {
                        for (int ui2 = ui1 + 1; ui2 < U.size(); ui2++) {
                            const Route& r1 = memo1[b][ui1];
                            Route final_r_A(-1,-1,nullptr), final_r_B(-1,-1,nullptr);
                            bool valid_A = false, valid_B = false;
                            
                            if (r1.veh_id != -1 && r1.is_feasible) {
                                final_r_A = insert_request(r1, U[ui2]);
                                if (final_r_A.veh_id != -1 && final_r_A.is_feasible) valid_A = true;
                            }
                            
                            const Route& r2 = memo1[b][ui2];
                            if (r2.veh_id != -1 && r2.is_feasible) {
                                final_r_B = insert_request(r2, U[ui1]);
                                if (final_r_B.veh_id != -1 && final_r_B.is_feasible) valid_B = true;
                            }

                            Route best_final(-1,-1,nullptr);
                            bool any_valid = false;
                            if (valid_A && valid_B) {
                                best_final = (final_r_A.total_benefit > final_r_B.total_benefit) ? final_r_A : final_r_B;
                                any_valid = true;
                            } else if (valid_A) {
                                best_final = final_r_A; any_valid = true;
                            } else if (valid_B) {
                                best_final = final_r_B; any_valid = true;
                            }

                            if (any_valid) {
                                long long delta = best_final.total_benefit - route_current_ben;
                                check_and_update_move(k, br.ejected, {U[ui1], U[ui2]}, best_final, delta, best_move, route, current_total_ben, best_benefit);
                            }
                        }
                    }
                }
            }

            if (!best_move.valid && best_move.has_tabu_move) {
                int k_stuck = best_move.tabu_route_idx;
                for (int u = 0; u < data.V_mac; u++) {
                    for (int v = 0; v < data.V_mac; v++) {
                        tabu_list[k_stuck][u][v] = -1;
                    }
                }
                if (!RELEASE_MODE) {
                    log_and_print("[Iter " + to_string(iter) + "] Local optima stuck! Cleared Tabu List for Route " + to_string(k_stuck) + " | Current Benefit: " + to_string(current_total_ben) + " | Best Benefit: " + to_string(best_benefit));
                }
                continue; // Re-evaluate in the next iter with cleared tabu list
            }

            if (best_move.valid) {
                int k = best_move.route_idx;
                route_iter[k]++;
                

                
                for (int i=0; i < (int)routes[k].nodes.size() - 1; i++) {
                    int u = routes[k].nodes[i]->node_id;
                    int v = routes[k].nodes[i+1]->node_id;
                    
                    bool has_edge = false;
                    for(int j=0; j < (int)best_move.new_route.nodes.size() - 1; j++) {
                        if (best_move.new_route.nodes[j]->node_id == u && best_move.new_route.nodes[j+1]->node_id == v) {
                            has_edge = true; break;
                        }
                    }

                    if (!has_edge) {
                        tabu_list[k][u][v] = route_iter[k] + TABU_TENURE;
                    }
                }

                routes[k] = best_move.new_route;
                
                for (int j : best_move.ejects) is_served[j] = false;
                for (int j : best_move.injects) is_served[j] = true;

                current_total_ben += best_move.delta;
                
                bool is_new_record = false;
                if (current_total_ben > best_benefit) {
                    best_benefit = current_total_ben;
                    best_routes = routes;
                    is_new_record = true;
                }
                
                if (!RELEASE_MODE) {
                    string move_str = "[Iter " + to_string(iter) + "] Route " + to_string(k) + " | ";
                    if (best_move.ejects.empty()) move_str += "Eject: [] | ";
                    else {
                        move_str += "Eject: [";
                        for(size_t i=0; i<best_move.ejects.size(); i++) {
                            move_str += to_string(best_move.ejects[i]) + (i == best_move.ejects.size()-1 ? "" : ", ");
                        }
                        move_str += "] | ";
                    }

                    if (best_move.injects.empty()) move_str += "Inject: [] | ";
                    else {
                        move_str += "Inject: [";
                        for(size_t i=0; i<best_move.injects.size(); i++) {
                            move_str += to_string(best_move.injects[i]) + (i == best_move.injects.size()-1 ? "" : ", ");
                        }
                        move_str += "] | ";
                    }
                    move_str += "Delta: " + to_string(best_move.delta);
                    if (best_move.is_tabu) move_str += " (Tabu)";
                    log_and_print(move_str);
                    
                    string ben_str = "    -> Current Benefit: " + to_string(current_total_ben) + " | Best Benefit: " + to_string(best_benefit);
                    if (is_new_record) ben_str += " *** NEW RECORD ***";
                    log_and_print(ben_str);
                    for (int ri = 0; ri < data.K; ri++) {
                        string r_str = "       Route " + to_string(ri) + ": Depot";
                        for (int i = 1; i < (int)routes[ri].nodes.size() - 1; i++) {
                            int j_id = routes[ri].nodes[i]->job_id;
                            int type = routes[ri].nodes[i]->type;
                            r_str += " -> " + to_string(j_id);
                            if (type == PARCEL_PICKUP) r_str += "(P)";
                            else if (type == PARCEL_DROPOFF) r_str += "(D)";
                        }
                        r_str += " -> Depot";
                        log_and_print(r_str);
                    }
                }
            } else {
                log_and_print("[Iter " + to_string(iter) + "] Local optima stuck! No valid moves found.");
                break;
            }
        }

        if (RELEASE_MODE) {
            cout << best_benefit << endl;
            for (auto& r : best_routes) {
                if (r.nodes.size() <= 2) {
                    cout << "0" << endl;
                } else {
                    vector<int> out_path;
                    for (int i = 1; i < (int)r.nodes.size() - 1; i++) {
                        out_path.push_back(r.nodes[i]->physical_in + 1);
                        if (r.nodes[i]->type == PASSENGER)
                            out_path.push_back(r.nodes[i]->physical_out + 1);
                    }
                    cout << out_path.size();
                    for (int x : out_path) cout << " " << x;
                    cout << endl;
                }
            }
        } else {
            ofstream fout("output_tour.txt");
            fout << best_benefit << endl;
            for (auto& r : best_routes) {
                if (r.nodes.size() <= 2) {
                    fout << "0" << endl;
                } else {
                    vector<int> out_path;
                    for (int i = 1; i < (int)r.nodes.size() - 1; i++) {
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
            for (auto& msg : log_history) flog << msg << endl;
            flog.close();
        }
    }
};

int main(int argc, char* argv[])
{
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);

    string filename = (argc > 1) ? argv[1] : "";
    
    Data data(filename);

    if (RELEASE_MODE)
    {
        auto global_start = chrono::high_resolution_clock::now();
        long long overall_best_ben = -999999999;
        string overall_best_output = "";
        
        while (true)
        {
            stringstream ss;
            streambuf* orig_cout = cout.rdbuf(ss.rdbuf());
            
            TabuSolver solver(data);
            solver.solve(30000);
            
            cout.rdbuf(orig_cout);
            
            if (solver.best_benefit > overall_best_ben)
            {
                overall_best_ben = solver.best_benefit;
                overall_best_output = ss.str();
            }
            
            auto now = chrono::high_resolution_clock::now();
            chrono::duration<double> elapsed = now - global_start;
            if (elapsed.count() > TIME_LIMIT - 0.5) 
            {
                break;
            }
        }
        
        cout << overall_best_output;
    }
    else
    {
        TabuSolver solver(data);
        solver.solve(1000);
    }
    
    return 0;
}
