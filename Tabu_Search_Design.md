# Thiết kế Tabu Search Chuẩn cho SOICT 2026

## Mục lục

1. [Tổng quan vấn đề với code hiện tại](#1-tổng-quan-vấn-đề-với-code-hiện-tại)
2. [Định nghĩa Tabu Search "đúng nghĩa"](#2-định-nghĩa-tabu-search-đúng-nghĩa)
3. [Toán tử A: Thiết kế chi tiết](#3-toán-tử-a-thiết-kế-chi-tiết)
4. [Vấn đề 1: Bảo tồn số lượng nodes](#4-vấn-đề-1-bảo-tồn-số-lượng-nodes)
5. [Vấn đề 2: Hard Constraint vs Soft Constraint](#5-vấn-đề-2-hard-constraint-vs-soft-constraint)
6. [Vấn đề 3: Khởi tạo lời giải ban đầu](#6-vấn-đề-3-khởi-tạo-lời-giải-ban-đầu)
7. [Kiến trúc tổng thể của phiên bản mới](#7-kiến-trúc-tổng-thể-của-phiên-bản-mới)
8. [Phân tích độ phức tạp](#8-phân-tích-độ-phức-tạp)

---

## 1. Tổng quan vấn đề với code hiện tại

### Code hiện tại đang làm gì?

Code hiện tại tự xưng là "ILS + Tabu Search" nhưng thực chất là một **hỗn hợp chắp vá** (hybrid) gồm nhiều chiến lược không rõ ranh giới:

```
solve():
  1. Greedy Init (best_insert)
  2. for each iteration:
     a. Or-Opt K1 (local search intra-route)
     b. Exhaustive Remove (xóa node không có lợi)
     c. Best Insert (nhét lại)
     d. Random Perturbation (phá ngẫu nhiên)
     e. Best Insert (nhét lại)
     f. Operator X (DFS Bitmask DP giao chéo 2 xe)
```

### Tại sao nó là "hộp đen"?

| Khía cạnh | Vấn đề |
|-----------|--------|
| **Neighborhood** | Không có định nghĩa hàng xóm rõ ràng. Mỗi bước thực hiện một chuỗi thao tác phức hợp (remove + insert + or-opt). Không thể nói "1 move = 1 neighbor". |
| **Tabu** | Tabu dict lưu cạnh bị cấm, nhưng cạnh nào bị cấm phụ thuộc vào Random Perturbation — một bước hoàn toàn ngẫu nhiên. Tabu không gắn với move cụ thể nào. |
| **Aspiration** | Không có aspiration criterion rõ ràng. |
| **Đánh giá** | Mỗi iteration không duyệt toàn bộ neighborhood, mà chỉ duyệt "một số" hướng. Không biết liệu best-in-neighborhood đã được tìm hay chưa. |
| **Reproducibility** | Kết quả phụ thuộc RNG seed, thứ tự duyệt, cache invalidation. Khó tái lập. |

### Hệ quả

- Không thể trả lời: "Tại sao iteration 500 tốt hơn iteration 499?"
- Không thể kiểm chứng: "Nếu tôi bỏ Or-Opt, kết quả thay đổi bao nhiêu %?"
- Không thể cải tiến có kiểm soát vì không biết bottleneck nằm ở đâu.

---

## 2. Định nghĩa Tabu Search "đúng nghĩa"

### Khung chuẩn

```
S := InitialSolution()
S* := S                         // Best known
TabuList := {}

for iter = 1 to MAX_ITER:
    N(S) := GenerateAllNeighbors(S)     // ← TOÀN BỘ hàng xóm
    S' := BestNonTabu(N(S), TabuList)   // ← Chọn tốt nhất không bị cấm
    
    // Aspiration: nếu S' tốt hơn S* thì bỏ qua tabu
    if f(S') > f(S*):
        S* := S'
    
    UpdateTabuList(S, S', TabuList)
    S := S'                             // ← LUÔN chuyển, kể cả xấu đi
```

### Khác biệt cốt lõi so với code hiện tại

| Tabu Search chuẩn | Code hiện tại |
|-------------------|---------------|
| Mỗi iter duyệt **toàn bộ** neighborhood | Mỗi iter duyệt **ngẫu nhiên một phần** |
| Chọn **best** trong toàn bộ N(S) | Chọn theo heuristic greedy cục bộ |
| **Luôn chuyển** sang neighbor, kể cả xấu đi | Chỉ chuyển nếu cải thiện (trừ perturbation) |
| Tabu gắn với **move cụ thể** | Tabu gắn với cạnh bị phá bởi random |
| Cấu trúc rõ ràng, debuggable | Black-box phức hợp |

---

## 3. Toán tử A: Thiết kế chi tiết

### Ý tưởng cốt lõi

Toán tử A định nghĩa một **move** là:
> "Rút 2 node ra khỏi lời giải, rồi tìm 2 node khác lấp vào chỗ trống."

Đây là một dạng **Ejection-Injection Operator**. Mỗi move tạo ra đúng 1 neighbor.

### Bước 1: Rút 2 nodes (Ejection)

Gọi lời giải hiện tại là $S = (R_1, R_2, \ldots, R_K, U)$ trong đó $R_k$ là route của xe $k$, $U$ là tập unserved.

**Chọn 2 node để rút:**

Có 2 chiến lược phụ:

#### Chiến lược 1A: Rút cùng route (Intra-route)
- Chọn 1 route $R_k$.
- Chọn 2 node trong $R_k$ để rút.
- Ưu điểm: Giữ nguyên cấu trúc các route khác. Dễ đánh giá delta.
- Nhược điểm: Không tạo ra sự giao lưu giữa các xe.

#### Chiến lược 1B: Rút khác route (Inter-route)
- Chọn 2 route $R_a, R_b$ ($a \neq b$).
- Rút 1 node từ mỗi route.
- Ưu điểm: Tạo cơ hội giao chéo (cross-exchange).
- Nhược điểm: Neighborhood lớn hơn nhiều.

#### Xử lý hàng hoá (Parcel):
- Nếu node được chọn là PARCEL_PICKUP, thì **bắt buộc** rút luôn PARCEL_DROPOFF tương ứng.
- Ngược lại nếu chọn PARCEL_DROPOFF, rút luôn PARCEL_PICKUP.
- Cặp (pickup, dropoff) được coi là **1 đơn vị rút** (chiếm 2 vị trí vật lý nhưng chỉ là 1 request).
- Nếu request thứ nhất đã là parcel (chiếm 2 node), thì "2 node" trong định nghĩa move thực chất có thể là 2, 3, hoặc 4 node vật lý.

**Đề xuất chuẩn hoá:** Mỗi move rút đúng **2 request** (không phải 2 node vật lý). Một request có thể là:
- 1 Passenger (1 node vật lý)
- 1 Parcel (2 node vật lý: pickup + dropoff)

Tổng số node vật lý bị rút: 2, 3, hoặc 4.

### Bước 2: Nhét 2 nodes lại (Injection)

Sau khi rút 2 request ra, ta có:
- Lời giải bị khuyết: $S' = (R'_1, \ldots, R'_K, U')$ với $U' = U \cup \{req_1, req_2\}$
- Tập candidates để nhét lại: toàn bộ $U'$

**Duyệt mọi tổ hợp (candidate_1, candidate_2) ∈ U' × U'**, candidate_1 ≠ candidate_2.

Với mỗi tổ hợp:
1. Tìm vị trí tốt nhất để nhét candidate_1 vào route nào đó.
2. Tìm vị trí tốt nhất để nhét candidate_2 vào route nào đó (có thể cùng hoặc khác route).
3. Tính benefit tổng sau khi nhét cả hai.

**Lưu ý quan trọng:** Thứ tự nhét candidate_1 trước candidate_2 có ảnh hưởng (vì nhét candidate_1 thay đổi route, ảnh hưởng đến vị trí tối ưu của candidate_2). Để chính xác, cần:
- Hoặc duyệt cả 2 thứ tự (tốn gấp đôi).
- Hoặc chấp nhận xấp xỉ: đánh giá độc lập rồi cộng delta (nhanh hơn, sai số nhỏ).

### Tổng kết 1 move

```
Move(req_eject_1, req_eject_2, req_inject_1, req_inject_2,
     inject_1_route, inject_1_pos, inject_2_route, inject_2_pos)
```

Đây là một move hoàn chỉnh, có thể:
- Đánh giá delta benefit.
- Undo được (vì ta biết chính xác 2 request bị rút và 2 request được nhét).
- Đưa vào Tabu List (cấm rút lại req_inject_1 và req_inject_2 trong T iterations tới).

---

## 4. Vấn đề 1: Bảo tồn số lượng nodes

### Phát biểu vấn đề

Toán tử A rút 2 request rồi nhét 2 request. Nếu 2 request nhét vào trùng với 2 request vừa rút, thì move này chỉ thay đổi **vị trí** (relocation). Nếu khác, thì nó là **swap** giữa served và unserved.

Nhưng trong cả 2 trường hợp, **tổng số request được phục vụ không thay đổi**.

### Tại sao đây là vấn đề lớn?

Bài toán SOICT 2026 là bài toán **tối đa hoá** (maximize benefit). Benefit = Σ revenue - Σ cost. Nếu có request chưa phục vụ mà revenue cao, chi phí đi lấy thấp, thì **nhét thêm** mới tối ưu. Nhưng toán tử A không bao giờ nhét thêm.

Ngược lại, nếu có request đang phục vụ mà revenue < cost để phục vụ, thì **bỏ đi** mới tối ưu. Nhưng toán tử A cũng không bao giờ bỏ đi.

### Giải pháp đề xuất

#### Giải pháp 4A: Mở rộng Toán tử A — Cho phép "NULL inject"

Cho phép 1 trong 2 request inject là NULL (nghĩa là rút 2, nhét 1). Khi đó:
- Rút 2 request, nhét 1 → giảm 1 request phục vụ.
- Rút 2 request, nhét 0 → giảm 2.

Tương tự, cho phép "NULL eject":
- Rút 1 request, nhét 2 → tăng 1 request phục vụ.
- Rút 0 request, nhét 2 → tăng 2.

Toán tử A mở rộng trở thành: **Rút 0–2 request, nhét 0–2 request.** Tất cả các tổ hợp hợp lệ đều được xem xét trong cùng 1 neighborhood.

> [!IMPORTANT]
> Đây là bước mở rộng cốt yếu. Nếu không có NULL eject/inject, thuật toán Tabu Search sẽ bị mắc kẹt ở một lời giải cố định kích thước mãi mãi, không bao giờ khám phá được vùng lời giải có kích thước khác.

#### Giải pháp 4B: Toán tử phụ trợ bên ngoài vòng Tabu

Giữ nguyên toán tử A (luôn rút 2, nhét 2), nhưng thêm 2 toán tử đơn giản:
- **Insert**: Lấy 1 request từ unserved, tìm chỗ tốt nhất nhét vào (tăng served).
- **Remove**: Tìm 1 request trong served mà việc bỏ nó ra sẽ cải thiện benefit (giảm served).

Chạy Insert/Remove như **post-processing** sau mỗi N iterations của Tabu.

> [!TIP]
> Giải pháp 4A sạch hơn về mặt lý thuyết (1 neighborhood duy nhất), nhưng 4B dễ cài đặt hơn và cho phép tách biệt debug.

### Phân tích kích thước Neighborhood khi mở rộng

Ký hiệu:
- $S$: số request đang served
- $U$: số request unserved  
- $K$: số xe
- $L_{avg}$: chiều dài trung bình 1 route

| Loại move | Số eject | Số inject | Số tổ hợp eject | Số tổ hợp inject | Tổng |
|-----------|---------|----------|----------------|-----------------|------|
| Pure swap | 2 | 2 | $O(S^2)$ | $O(U^2 \cdot K \cdot L_{avg}^2)$ | Rất lớn |
| Eject only | 2 | 0 | $O(S^2)$ | 1 | $O(S^2)$ |
| Eject 1 Inject 1 | 1 | 1 | $O(S)$ | $O((S+U) \cdot K \cdot L_{avg})$ | Lớn |
| Inject only | 0 | 2 | 1 | $O(U^2 \cdot K \cdot L_{avg}^2)$ | Lớn |
| Relocate | 2 (same) | 2 (same) | $O(S^2)$ | $O(K \cdot L_{avg}^2)$ | Vừa |

**Kết luận:** Duyệt toàn bộ neighborhood theo nghĩa đen là **không khả thi** cho test case lớn (S, U ~ hàng trăm). Cần lấy mẫu thông minh (candidate list) hoặc giới hạn neighborhood.

---

## 5. Vấn đề 2: Hard Constraint vs Soft Constraint

### Hard Constraint (Phiên bản chính)

**Định nghĩa:** Mọi neighbor $S'$ phải thoả mãn tất cả ràng buộc:
1. ✅ Time window: $arr(v) \leq l(v)$ cho mọi node $v$.
2. ✅ Capacity: $load(v) \leq Q_k$ cho mọi vị trí trên route $k$.
3. ✅ Precedence: pickup trước dropoff (parcel).
4. ✅ Direct trip: pickup và dropoff passenger liền kề.

**Ưu điểm:**
- Mọi lời giải trung gian đều hợp lệ → có thể dừng bất cứ lúc nào.
- Đánh giá delta đơn giản hơn (không cần tính penalty).
- Không cần chiến lược ép về feasible.

**Nhược điểm:**
- Neighborhood bị thu hẹp đáng kể. Nhiều move bị loại ngay vì vi phạm time window.
- Khó thoát khỏi local optima vì "con đường" qua infeasible region bị chặn.

### Soft Constraint (Phiên bản phụ trợ)

**Định nghĩa:** Cho phép vi phạm ràng buộc, nhưng trừ penalty vào hàm mục tiêu.

$$f_{soft}(S) = \sum revenue - \sum cost - \alpha \cdot \sum_{v: arr(v) > l(v)} (arr(v) - l(v)) - \beta \cdot \sum_{k} \max(0, load_k - Q_k)$$

Trong đó $\alpha, \beta$ là hệ số penalty.

**Ưu điểm:**
- Neighborhood rộng hơn rất nhiều.
- Cho phép "đi xuyên" qua vùng infeasible để tìm feasible tốt hơn.
- Cho phép nhét unserved request dù gây vi phạm nhẹ, rồi các move sau sẽ sửa.

**Nhược điểm:**
- Lời giải trung gian có thể infeasible → cần chiến lược ép về feasible trước khi kết thúc.
- Cần tinh chỉnh $\alpha, \beta$ (quá nhỏ → lời giải mãi infeasible, quá lớn → giống hard constraint).

### Đề xuất: Chiến lược kết hợp 2 pha

```
Phase 1 — Soft Constraint (Mở rộng): 
    Mục tiêu: Nhét càng nhiều unserved request càng tốt.
    Dùng Toán tử A-soft: cho phép vi phạm, dùng f_soft.
    Kết thúc khi: f_soft không cải thiện sau T1 iterations.

Phase 2 — Hard Constraint (Tối ưu):
    Mục tiêu: Ép mọi vi phạm về 0, đồng thời tối đa benefit.
    Dùng Toán tử A-hard: loại bỏ mọi neighbor infeasible.
    Nếu có vi phạm: tăng penalty α, β và quay lại Phase 1.
    Kết thúc khi: lời giải feasible + f_hard không cải thiện sau T2 iterations.
```

> [!WARNING]
> Chiến lược soft constraint yêu cầu **luôn duy trì một lời giải feasible tốt nhất song song**. Đừng bao giờ ghi đè best solution bằng một lời giải infeasible.

### Adaptive Penalty

Thay vì cố định $\alpha, \beta$, dùng adaptive:

```
Mỗi P iterations:
    Nếu lời giải hiện tại feasible:
        α := α / (1 + δ)    // Giảm penalty → khuyến khích khám phá
    Nếu lời giải hiện tại infeasible:
        α := α * (1 + δ)    // Tăng penalty → ép về feasible
```

Điều này tự động cân bằng giữa exploration (soft) và exploitation (hard).

---

## 6. Vấn đề 3: Khởi tạo lời giải ban đầu

### Phân tích vai trò

Khởi tạo ảnh hưởng đến:
1. **Tốc độ hội tụ**: Lời giải ban đầu tốt → ít iterations hơn.
2. **Diversity**: Lời giải ban đầu đa dạng → multi-start ILS hiệu quả hơn.
3. **Feasibility**: Nếu dùng hard constraint, cần đảm bảo lời giải ban đầu feasible.

### Chiến lược hiện tại: Greedy Insertion

```python
while unserved is not empty:
    for each unserved request:
        find best (route, position) to insert
    insert the request with highest marginal benefit
```

**Ưu điểm:** Đơn giản, nhanh, luôn feasible (vì kiểm tra constraint trước khi nhét).

**Nhược điểm:** Greedy → local optima ngay từ đầu. Thứ tự nhét ảnh hưởng lớn nhưng không kiểm soát được.

### Các chiến lược thay thế

#### 6A: Random Greedy (Recommended cho multi-start)

```
while unserved is not empty:
    candidates = top-k best insertions
    randomly choose one from candidates
    insert it
```

Nhanh, dễ cài, mỗi lần chạy ra lời giải khác → đa dạng cho multi-start.

#### 6B: Regret-2 Insertion

```
while unserved is not empty:
    for each unserved request r:
        best_1 = best insertion position for r
        best_2 = second best insertion position for r
        regret[r] = best_1.cost - best_2.cost
    insert request with highest regret (most urgent)
```

Ưu tiên nhét request mà nếu không nhét ngay thì sẽ không còn chỗ tốt. 
Thường cho kết quả init tốt hơn pure greedy.

#### 6C: Empty Init (cho soft constraint)

Đơn giản nhất: bắt đầu với tất cả route rỗng, mọi request unserved. Dùng soft-constraint Tabu Search để tự nhét vào.

**Khi nào dùng:** Khi bạn tin tưởng rằng Tabu Search đủ mạnh để tự tìm lời giải tốt từ scratch. Giúp tránh bias từ greedy init.

### Đề xuất

Dùng **6A (Random Greedy)** cho hard-constraint Tabu Search. Lý do:
- Multi-start ILS cần mỗi start khác nhau → Random Greedy cung cấp diversity.
- Greedy insert đã chứng minh hiệu quả trong code hiện tại.
- Regret-2 tốt hơn nhưng tốn thời gian gấp đôi (có thể dùng nếu time budget dư).

---

## 7. Kiến trúc tổng thể của phiên bản mới

### Pseudo-code

```cpp
struct Move {
    int eject_req_1;    // -1 nếu NULL
    int eject_req_2;    // -1 nếu NULL  
    int inject_req_1;   // -1 nếu NULL
    int inject_req_2;   // -1 nếu NULL
    int inject_route_1, inject_pos_1;
    int inject_route_2, inject_pos_2;
    long long delta_benefit;
};

void tabu_search(Solution& S, int max_iter) {
    Solution S_star = S;    // Best feasible
    int tabu_tenure = 15;
    vector<vector<int>> tabu_matrix(V_mac, vector<int>(V_mac, 0));
    // tabu_matrix[u][v] = iter until which edge (u,v) is tabu
    
    for (int iter = 1; iter <= max_iter; iter++) {
        Move best_move = {.delta_benefit = -INF};
        
        // === Duyệt Neighborhood ===
        
        // Loại 1: Eject 2, Inject 2 (swap)
        for (eject_pair in get_eject_candidates(S)) {
            Solution S_temp = apply_eject(S, eject_pair);
            for (inject_pair in get_inject_candidates(S_temp)) {
                Move m = evaluate(S, eject_pair, inject_pair);
                if (is_tabu(m, iter, tabu_matrix)) {
                    if (m.delta + f(S) > f(S_star))  // Aspiration
                        ; // cho phép
                    else
                        continue;
                }
                if (m.delta > best_move.delta)
                    best_move = m;
            }
        }
        
        // Loại 2: Eject 1, Inject 0 (pure remove)
        // Loại 3: Eject 0, Inject 1 (pure insert)  
        // ... tương tự
        
        // === Apply best move ===
        apply_move(S, best_move);
        update_tabu(tabu_matrix, best_move, iter, tabu_tenure);
        
        // === Update best ===
        if (is_feasible(S) && f(S) > f(S_star))
            S_star = S;
    }
    
    S = S_star;
}
```

### Cấu trúc file mới

```
solver_tabu.cpp
├── Data structures (GenericNode, Data, Route)     // Giữ nguyên từ solver_full.cpp
├── Solution struct                                 // Mới: đóng gói toàn bộ state
├── Evaluator (O(1) delta)                         // Giữ nguyên, bỏ map → dùng mảng
├── OperatorA_Hard                                  // Mới
├── OperatorA_Soft                                  // Mới  
├── TabuManager                                     // Mới: quản lý tabu list bằng mảng 2D
├── Initializer                                     // Mới: Random Greedy
├── TabuSearchSolver                                // Mới: vòng lặp chính
└── main()
```

### Ước lượng kích thước code

| Module | Dòng ước tính |
|--------|-------------|
| Data structures + IO | ~200 |
| Route + update_states | ~80 |
| Evaluator (delta) | ~120 |
| OperatorA_Hard | ~150 |
| OperatorA_Soft | ~80 (wrapper + penalty) |
| TabuManager | ~30 |
| Initializer | ~60 |
| Solver loop | ~80 |
| Output | ~40 |
| **Tổng** | **~840** |

Ngắn hơn đáng kể so với solver_full.cpp hiện tại (~1750 dòng).

---

## 8. Phân tích độ phức tạp

### Mỗi iteration

Ký hiệu:
- $S$: số request served
- $U$: số request unserved
- $K$: số xe  
- $L$: chiều dài trung bình route

| Bước | Độ phức tạp | Ghi chú |
|------|------------|---------|
| Chọn 2 request rút | $O(S^2)$ | Có thể giới hạn bằng candidate list |
| Đánh giá rút (delta) | $O(1)$ mỗi cặp | Dùng O(1) evaluator hiện có |
| Tìm vị trí nhét cho 2 request | $O((S+U) \cdot K \cdot L)$ | Cho mỗi cặp eject |
| Kiểm tra tabu | $O(1)$ | Mảng 2D |
| **Tổng 1 iteration** | $O(S^2 \cdot (S+U) \cdot K \cdot L)$ | **Rất lớn** |

### Giải pháp giảm phức tạp

1. **Candidate list cho eject**: Chỉ xét top-P request có benefit thấp nhất (P ~ 10–20). Giảm $S^2 → P^2$.

2. **Candidate list cho inject**: Chỉ xét top-Q request unserved có revenue cao nhất (Q ~ 10–20). Giảm $(S+U) → Q$.

3. **First-improvement**: Dừng ngay khi tìm được move cải thiện, không cần best-improvement. Giảm hằng số rất nhiều.

4. **Neighborhood sampling**: Mỗi iteration chỉ duyệt một tập con ngẫu nhiên của neighborhood. Đây là trade-off giữa chất lượng mỗi move vs số iterations.

### Ước lượng thực tế

Với candidate list P=15, Q=15, K=5, L=10:
- Số move xét: $15^2 \times 15 \times 5 \times 10 = 168,750$ moves/iter
- Mỗi move tốn ~100ns (delta evaluation O(1)) → ~17ms/iter
- Time limit 100s → ~5,800 iterations

Đủ cho Tabu Search hội tụ trên hầu hết test case.

---

## Tóm tắt quyết định cần đưa ra

| # | Quyết định | Lựa chọn đề xuất | Lý do |
|---|-----------|-------------------|-------|
| 1 | Eject cùng/khác route | **Cả hai** (luân phiên) | Cùng route cho local refinement, khác route cho diversification |
| 2 | Bảo tồn nodes | **Giải pháp 4A** (NULL eject/inject) | Sạch về lý thuyết, 1 neighborhood duy nhất |
| 3 | Hard vs Soft | **Hard chính, Soft phụ (2 pha)** | Hard đảm bảo feasible, Soft giúp nhét unserved |
| 4 | Khởi tạo | **Random Greedy** | Đa dạng cho multi-start, đã chứng minh tốt |
| 5 | Best vs First improvement | **First improvement** | Tiết kiệm thời gian, nhiều iterations hơn |
| 6 | Tabu attribute | **Request ID** (cấm rút lại request vừa nhét) | Đơn giản, hiệu quả, tránh cycle |
