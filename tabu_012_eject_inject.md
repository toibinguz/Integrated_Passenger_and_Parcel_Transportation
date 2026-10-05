# Phân tích hướng cài đặt Tabu Search (0,1,2-Eject-Inject & Edge-based Tabu)

Đây là bản phân tích thuật toán và thiết kế mẫu (prototype) để kiểm chứng nhanh ý tưởng **0,1,2-Eject-Inject** kết hợp **Edge-based Tabulist** cho bài toán SOICT 2026. Code được thiết kế tối giản, procedural, không class/architecture rườm rà.

## 1. Toán tử 0,1,2-Eject-Inject

Mục tiêu của toán tử là sinh ra các lời giải lân cận (neighborhood) bằng cách phá vỡ cấu trúc hiện tại và vá lại. 
*   **Eject (0, 1, hoặc 2)**: Chọn ngẫu nhiên $c_{eject} \in \{0, 1, 2\}$ requests (passenger hoặc parcel) **đang được phục vụ** trong các xe để rút ra (xóa các đỉnh Pickup và Delivery tương ứng khỏi route).
*   **Inject (0, 1, hoặc 2)**: Chọn ngẫu nhiên $c_{inject} \in \{0, 1, 2\}$ requests **chưa được phục vụ** để chèn vào.
    *   Với *Passenger*: Ràng buộc direct-trip (đi thẳng), ta chèn theo cụm liền kề `[P_i, D_i]` tại một vị trí bất kỳ trong route của một xe.
    *   Với *Parcel*: Chèn $P_j$ ở vị trí ngẫu nhiên $pos_1$ và $D_j$ ở $pos_2$ sao cho $pos_1 < pos_2$.

*Vì không gian sinh (đặc biệt là chèn) rất lớn, ta sẽ dùng phương pháp sinh mẫu ngẫu nhiên (stochastic sampling): thử nghiệm $N$ lân cận ngẫu nhiên trong mỗi iteration và chọn lân cận tốt nhất hợp lệ.*

## 2. Tabu List (Attributed Edge-based)

Quản lý tabu trên cấp độ thuộc tính (cạnh - edges) thay vì toàn bộ cấu trúc lời giải.
*   **Lưu vết cạnh đứt**: Khi di chuyển từ lời giải hiện tại $S$ sang lân cận $S'$, ta trích xuất tập các cạnh có trong $S$ nhưng không có trong $S'$ (bị phá vỡ do thao tác eject/inject).
*   **Phạt Tabu**: Cấm không cho các cạnh đứt này được phép **thêm lại** vào lời giải trong $T$ (Tabu Tenure) vòng lặp tiếp theo. (Tương đương việc set `tabu_add[u][v] = current_iter + T`).
*   **Kiểm tra và Khát vọng (Aspiration)**: Nếu lân cận $S'$ chứa bất kỳ cạnh nào đang nằm trong `tabu_add`, lân cận đó bị cấm. Tuy nhiên, nếu $S'$ tạo ra kỉ lục mục tiêu tốt nhất (best known solution), ta bỏ qua lệnh cấm (aspiration criterion).

## 3. Cài đặt C++ (Prototype Mộc mạc)

Trọng tâm là mã ngắn gọn, flatten arrays, và chạy độc lập. Ta dùng struct `Solution` đơn giản và hàm `eval()` tuyến tính mô phỏng trực tiếp route để check các ràng buộc (thời gian, sức chứa).

```cpp
#include <bits/stdc++.h>
using namespace std;

const long long INF = 1e18;
const int MAXV = 1005;
const int MAX_REQ = 1005;

// Data structures bám sát input
int K, N, M, V;
int O[MAXV], Q[MAXV];

int req_type[MAX_REQ]; // 1: passenger, 2: parcel
int P[MAX_REQ], D[MAX_REQ], W[MAX_REQ];
int E[MAX_REQ], L_time[MAX_REQ], Ed[MAX_REQ], Ld[MAX_REQ];
int S[MAX_REQ], Rev[MAX_REQ];

int vertex_to_req[MAXV];
bool is_pickup[MAXV];

int t_mat[MAXV][MAXV];
int c_mat[MAXV][MAXV];

// Edge-based Tabu list
int tabu_add[MAXV][MAXV];
int TABU_TENURE = 10;
int MAX_ITER = 1000;
int NUM_SAMPLES = 200; // Số lượng lân cận lấy mẫu mỗi vòng (sinh ngẫu nhiên)

struct Solution {
    vector<int> routes[MAXV];
    bool served[MAX_REQ] = {false};
    long long obj = -INF;
};

// Đọc input
void read_input() {
    cin >> K >> N >> M;
    V = 2*N + 2*M + K;
    for (int k = 1; k <= K; ++k) cin >> O[k] >> Q[k];
    
    // Passengers
    for (int i = 1; i <= N; ++i) {
        int r = i;
        req_type[r] = 1;
        cin >> P[r] >> D[r] >> E[r] >> L_time[r] >> S[r] >> Rev[r];
        Ed[r] = E[r]; Ld[r] = L_time[r]; 
        vertex_to_req[P[r]] = r; is_pickup[P[r]] = true;
        vertex_to_req[D[r]] = r; is_pickup[D[r]] = false;
    }
    // Parcels
    for (int j = 1; j <= M; ++j) {
        int r = N + j;
        req_type[r] = 2;
        cin >> P[r] >> D[r] >> W[r] >> E[r] >> L_time[r] >> Ed[r] >> Ld[r] >> S[r] >> Rev[r];
        vertex_to_req[P[r]] = r; is_pickup[P[r]] = true;
        vertex_to_req[D[r]] = r; is_pickup[D[r]] = false;
    }
    
    for (int i = 1; i <= V; ++i)
        for (int j = 1; j <= V; ++j) cin >> t_mat[i][j];
        
    for (int i = 1; i <= V; ++i)
        for (int j = 1; j <= V; ++j) cin >> c_mat[i][j];
}

// Đánh giá route, kiểm tra tất cả ràng buộc, trả về objective (-INF nếu vi phạm)
long long eval(const Solution& sol) {
    long long total_benefit = 0;
    
    for (int k = 1; k <= K; ++k) {
        long long time_now = 0;
        int load = 0;
        int curr = O[k];
        vector<bool> picked(N + M + 1, false);
        vector<bool> dropped(N + M + 1, false);

        for (int i = 0; i < sol.routes[k].size(); ++i) {
            int v = sol.routes[k][i];
            int r = vertex_to_req[v];

            time_now += t_mat[curr][v];
            total_benefit -= c_mat[curr][v];

            if (is_pickup[v]) {
                time_now = max(time_now, (long long)E[r]);
                if (time_now > L_time[r]) return -INF; // Khung thời gian đón trễ
                
                picked[r] = true;
                
                if (req_type[r] == 1) { // Passenger
                    // Bắt buộc phải là direct trip
                    if (i + 1 == sol.routes[k].size() || sol.routes[k][i+1] != D[r]) return -INF;
                } else { // Parcel
                    load += W[r];
                    if (load > Q[k]) return -INF; // Quá tải
                }
            } else {
                if (!picked[r] || dropped[r]) return -INF; // Bị rơi / trả lỗi
                time_now = max(time_now, (long long)Ed[r]);
                if (time_now > Ld[r]) return -INF; // Khung thời gian trả trễ
                
                dropped[r] = true;
                if (req_type[r] == 2) load -= W[r];
                
                total_benefit += Rev[r];
            }
            time_now += S[r];
            curr = v;
        }
    }
    return total_benefit;
}

// Lấy danh sách cạnh từ Solution để phục vụ Edge-based Tabu
set<pair<int,int>> get_edges(const Solution& sol) {
    set<pair<int,int>> edges;
    for (int k = 1; k <= K; ++k) {
        if (sol.routes[k].empty()) continue;
        int curr = O[k];
        for (int v : sol.routes[k]) {
            edges.insert({curr, v});
            curr = v;
        }
    }
    return edges;
}

// Toán tử 0,1,2-Eject-Inject sinh ngẫu nhiên
Solution generate_neighbor(Solution sol) {
    int c_eject = rand() % 3;
    int c_inject = rand() % 3;

    // 1. EJECT
    vector<int> served_list;
    for (int r = 1; r <= N + M; ++r) if (sol.served[r]) served_list.push_back(r);
    random_shuffle(served_list.begin(), served_list.end());

    for (int i = 0; i < min(c_eject, (int)served_list.size()); ++i) {
        int r = served_list[i];
        sol.served[r] = false;
        
        for (int k = 1; k <= K; ++k) {
            vector<int> new_route;
            for (int v : sol.routes[k]) {
                if (v != P[r] && v != D[r]) new_route.push_back(v);
            }
            sol.routes[k] = new_route;
        }
    }

    // 2. INJECT
    vector<int> unserved_list;
    for (int r = 1; r <= N + M; ++r) if (!sol.served[r]) unserved_list.push_back(r);
    random_shuffle(unserved_list.begin(), unserved_list.end());

    for (int i = 0; i < min(c_inject, (int)unserved_list.size()); ++i) {
        int r = unserved_list[i];
        int k = (rand() % K) + 1; // Random xe
        sol.served[r] = true;

        if (req_type[r] == 1) { // Passenger: nguyên block [P, D]
            int pos = rand() % (sol.routes[k].size() + 1);
            sol.routes[k].insert(sol.routes[k].begin() + pos, D[r]);
            sol.routes[k].insert(sol.routes[k].begin() + pos, P[r]);
        } else { // Parcel: rời rạc
            int pos1 = rand() % (sol.routes[k].size() + 1);
            sol.routes[k].insert(sol.routes[k].begin() + pos1, P[r]);
            int pos2 = pos1 + 1 + (rand() % (sol.routes[k].size() - pos1));
            sol.routes[k].insert(sol.routes[k].begin() + pos2, D[r]);
        }
    }
    return sol;
}

// Lặp Tabu Search chính
void run_tabu_search() {
    Solution current_sol;
    current_sol.obj = eval(current_sol);
    Solution best_sol = current_sol;
    memset(tabu_add, 0, sizeof(tabu_add));

    for (int iter = 1; iter <= MAX_ITER; ++iter) {
        Solution best_neighbor;
        set<pair<int,int>> best_n_edges;

        // Sinh mẫu random neighbors
        for (int s = 0; s < NUM_SAMPLES; ++s) {
            Solution nb = generate_neighbor(current_sol);
            nb.obj = eval(nb);
            
            if (nb.obj == -INF) continue; // Phế (sai constraint)

            set<pair<int,int>> old_edges = get_edges(current_sol);
            set<pair<int,int>> new_edges = get_edges(nb);

            // Check Tabu
            bool is_tabu = false;
            for (auto edge : new_edges) {
                if (old_edges.find(edge) == old_edges.end() && tabu_add[edge.first][edge.second] >= iter) {
                    is_tabu = true; 
                    break;
                }
            }

            // Aspiration Criterion (Ghi đè Tabu nếu kỉ lục mới)
            if (is_tabu && nb.obj <= best_sol.obj) continue;

            if (nb.obj > best_neighbor.obj) {
                best_neighbor = nb;
                best_n_edges = new_edges;
            }
        }

        // Chuyển bước & Cập nhật Tabulist
        if (best_neighbor.obj != -INF) {
            set<pair<int,int>> old_edges = get_edges(current_sol);
            for (auto edge : old_edges) {
                // Nếu cạnh bị đứt -> Phạt không cho add lại
                if (best_n_edges.find(edge) == best_n_edges.end()) {
                    tabu_add[edge.first][edge.second] = iter + TABU_TENURE;
                }
            }

            current_sol = best_neighbor;
            if (current_sol.obj > best_sol.obj) best_sol = current_sol;
        }
    }

    // In lời giải định dạng SOICT 2026
    cout << best_sol.obj << "\n";
    for (int k = 1; k <= K; ++k) {
        cout << best_sol.routes[k].size();
        for (int v : best_sol.routes[k]) cout << " " << v;
        cout << "\n";
    }
}

int main() {
    ios_base::sync_with_stdio(false); cin.tie(NULL);
    srand(time(NULL));
    read_input();
    run_tabu_search();
    return 0;
}
```
