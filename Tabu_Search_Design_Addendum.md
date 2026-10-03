# Phụ lục: Phân tích Tri thức Miền (Domain Knowledge) và Hệ quả

> Tài liệu này bổ sung cho [Tabu_Search_Design.md](file:///z:/Desktop/250926/Tabu_Search_Design.md)

---

## Tri thức trích xuất từ testcase chính thức

| Tham số | Giới hạn quan sát | Ý nghĩa |
|---------|-------------------|---------|
| $K$ (số xe) | $\leq 10$ | Rất ít xe |
| Request/route | $\leq 6{-}8$ | Mỗi xe phục vụ rất ít request |
| Tổng served | $\leq 60{-}80$ | Ước lượng: $K \times 8 = 80$ |
| Node vật lý/route | $\leq 16{+}2$ | Tệ nhất: 8 parcel × 2 node + 2 depot = 18 |

> [!IMPORTANT]
> Thông tin này thay đổi **hoàn toàn** phân tích độ phức tạp trong bản thiết kế gốc. Mọi con số "rất lớn" ở Section 8 giờ trở thành **nhỏ đến mức đáng ngạc nhiên**.

---

## Vấn đề thực sự: "Eject khó, Insert dễ"

Bạn nói đúng: với route chỉ dài 6–8 request (tối đa ~18 node vật lý), bài toán **tìm vị trí nhét** (insertion position) là tầm thường:

- Đánh giá 1 request passenger vào 1 route: duyệt $L$ vị trí → $L \leq 18$ → **18 phép tính**.
- Đánh giá 1 request parcel vào 1 route: duyệt $L^2$ cặp vị trí → $\leq 324$ phép tính.
- Đánh giá toàn bộ $K$ routes: $\times 10$ → tối đa **3,240 phép tính** cho 1 request.

**Insert (nhét vào đâu) không phải bottleneck.** Nó nhanh đến mức có thể brute-force hoàn toàn.

### Vậy cái khó nằm ở đâu?

Cái khó nằm ở **chiến lược Eject**: chọn request nào để rút ra?

Lý do sâu xa:

1. **Hiệu ứng domino:** Rút 1 request ra khỏi route có thể khiến toàn bộ time window của các request phía sau bị dịch — có thể khiến chúng khả thi (tốt) hoặc không khả thi (xấu). Không thể đánh giá bằng O(1) delta mà phải chạy lại `update_states`.

2. **Tương tác liên xe:** Request A nằm trên xe 1 có thể "chặn" request B (unserved) vì B cần time window gần giống A nhưng ở vị trí vật lý gần xe 2 hơn. Rút A ra khỏi xe 1 rồi nhét B vào xe 2 có thể tốt hơn, nhưng không thể nhận ra điều này nếu chỉ nhìn 1 xe.

3. **Không có hàm "giá trị loại bỏ" đơn giản:** Với passenger thì delta_remove tính được. Nhưng với parcel, rút pickup phải rút cả dropoff, và giá trị thực sự phụ thuộc vào **ai sẽ thay thế**.

---

## Hệ quả thiết kế: Neighborhood "nhỏ nhưng chất"

Với $S \leq 80$ served requests, $K \leq 10$, $L \leq 8$, ta tính lại neighborhood:

### Trường hợp: Toán tử A gốc (Eject 2, Inject 2)

| Bước | Kích thước | Chi phí mỗi candidate |
|------|-----------|----------------------|
| Chọn 2 request eject | $\binom{S}{2} \leq \binom{80}{2} = 3160$ | — |
| Với mỗi eject: rebuild 1–2 route | — | $O(L) = O(18)$ |
| Chọn 2 request inject từ $U' = U + 2$ | $\binom{U'}{2}$ | — |
| Với mỗi inject: tìm best position | — | $O(K \cdot L^2) \approx 3240$ |
| **Tổng** | $3160 \times \binom{U'+2}{2} \times 3240$ | — |

Nếu $U \approx 20$ → $\binom{22}{2} = 231$ → Tổng $\approx 3160 \times 231 \times 3240 \approx 2.4 \times 10^9$.

> [!CAUTION]
> **Vẫn quá lớn** cho 1 iteration dù mỗi phép tính là O(1). Cần giảm.

### Chiến lược giảm: Phân tách Eject và Inject

Thay vì duyệt toàn bộ $\binom{S}{2} \times \binom{U'}{2}$, ta **tách thành 2 pha**:

#### Pha Eject (Chọn 2 request rút ra)

Không cần duyệt mọi cặp. Dùng **heuristic scoring**:

```
Với mỗi request r đang served:
    eject_score[r] = -delta_benefit_if_removed(r)
    // Nghĩa là: request nào mà bỏ ra thì tổn thất ít nhất → nên bỏ trước
```

Sắp xếp theo eject_score giảm dần. Chỉ xét **top-P** request (P = 10–15).

Số cặp eject: $\binom{P}{2} \leq \binom{15}{2} = 105$.

#### Pha Inject (Chọn 2 request nhét vào)

Sau khi rút 2 request, ta có $U' = U + 2$. Chọn inject đơn giản hơn vì:
- Có thể nhét **chính 2 request vừa rút** vào vị trí mới (đây là relocate).
- Hoặc nhét từ unserved pool.

Scoring cho inject:

```
Với mỗi request r ∈ U':
    inject_score[r] = best_insertion_benefit(r, all_routes)
    // Tìm benefit tốt nhất nếu nhét r vào bất kỳ route nào
```

Chỉ xét **top-Q** request (Q = 10–15).

Số cặp inject: $\binom{Q}{2} \leq 105$.

#### Tổng sau khi giảm

$105 \times 105 \times O(rebuild) \approx 105 \times 105 \times 18 \approx 200{,}000$ phép tính/iteration.

Mỗi phép tính ~100–200ns → **~30–40μs/iteration** → **2.5 triệu iterations trong 100s**.

> [!TIP]
> Đây là con số **cực kỳ khả thi**. Thậm chí còn nhanh hơn solver hiện tại.

---

## Thiết kế Eject Scoring: Phân tích sâu

### Mục tiêu

Chọn request nào để rút ra sao cho:
1. Tổn thất benefit khi rút **thấp**.
2. "Giải phóng" time window hoặc capacity cho request khác tốt hơn.

### Metric 1: Delta Benefit khi rút (Cơ bản)

```
eject_score_basic[r] = benefit_without_r - benefit_with_r
```

- Nếu `eject_score_basic[r] > 0` → rút ra còn tốt hơn (nên rút ngay).
- Nếu `eject_score_basic[r]` ít âm → rút ra ít thiệt hại.

Cho passenger: đã có hàm `_delta_cost_remove` hiện tại, tận dụng được.
Cho parcel: cần clone route, xóa pickup+dropoff, `update_states`, so sánh.

### Metric 2: "Giải phóng tiềm năng" (Advanced)

Ngoài delta trực tiếp, đánh giá xem rút request $r$ có **mở ra bao nhiêu vị trí mới** cho các request unserved:

```
liberation_score[r] = Σ (số request unserved mà trước đó infeasible, sau khi rút r trở thành feasible)
```

Đắt hơn (cần thử nhét từng unserved request vào route sau khi rút r) nhưng cho thông tin cực kỳ giá trị.

Với $U \leq 50$, $L \leq 18$: chi phí thêm $\approx 50 \times 18 = 900$ phép tính cho mỗi request r. Chấp nhận được.

### Metric 3: Regret-based (cho Inject)

Tương tự Regret-2 insertion:

```
Với mỗi request r unserved:
    best_1 = best insertion benefit across all routes
    best_2 = second best insertion benefit
    urgency[r] = best_1 - best_2
```

Request có urgency cao = "bây giờ không nhét thì không còn cơ hội" → ưu tiên inject trước.

---

## Ý tưởng mới: Toán tử A dạng "Route Reconstruction"

Với route chỉ dài 6–8 request, một cách tiếp cận **hoàn toàn khác** trở nên khả thi:

### Thay vì eject-inject, hãy **phá và xây lại toàn bộ 1 route**.

```
Toán tử A-Reconstruct:
  1. Chọn 1 route R_k
  2. Lấy toàn bộ request đang trên R_k → pool_served
  3. pool_all = pool_served ∪ (một số request từ unserved)
  4. Tìm tổ hợp tốt nhất của pool_all mà vừa vào R_k
  5. Request nào bị loại → trả về unserved
```

#### Tại sao khả thi?

Bước 4 thực chất là bài toán: "Cho tối đa 8+4=12 request (pool_served + vài unserved), tìm tập con + thứ tự tốt nhất vừa vào 1 xe."

Đây chính là **Operator X hiện tại** (DFS Bitmask DP)! Nhưng chạy trên **1 xe thay vì 2 xe**.

Với 12 request, số node vật lý tối đa ~20. Bitmask $2^{20} = 1{,}048{,}576$. Mỗi state tốn O(1) → ~1 triệu phép tính → ~1ms.

> [!TIP]
> **Operator X đã là toán tử tối ưu cho bài toán 1-route.** Thay vì phát minh toán tử mới, hãy dùng Operator X nhưng với chiến lược chọn route và chọn pool thông minh hơn.

### Toán tử A-Reconstruct vs Toán tử A-EjectInject

| Tiêu chí | A-EjectInject | A-Reconstruct |
|----------|---------------|---------------|
| Granularity | Thay đổi 2 request | Xây lại toàn bộ 1 route |
| Optimality | Heuristic (best position) | Exact (bitmask DP) |
| Neighborhood | Rất lớn, cần sampling | Nhỏ: $K$ routes × pool combinations |
| Liên xe | Inter-route swap tự nhiên | Cần chạy trên 2 route (= Operator X hiện tại) |
| Cài đặt | Mới hoàn toàn | Tận dụng code Operator X |

### Đề xuất: Hybrid

```
Mỗi iteration của Tabu Search:
  Phase A: Chọn 1 route → Reconstruct bằng Bitmask DP (tối ưu đơn xe)
  Phase B: Chọn 2 routes → Cross-Reconstruct bằng Operator X (tối ưu liên xe)
  
  Tabu attribute: cấm chọn lại route k trong T iterations
```

Cái này cực kỳ sạch:
- **Mỗi move = 1 lần chạy Bitmask DP.**
- **Mọi move đều optimal trong không gian con** (tối ưu nhất có thể cho route/cặp route đó).
- **Tabu rõ ràng**: cấm chọn lại route (không cấm request hay cạnh).
- **Debug dễ**: "Iteration 500 cải thiện vì chạy lại route 3 với thêm request 7 từ unserved."

---

## Phân tích lại Độ phức tạp với Tri thức mới

### Toán tử A-Reconstruct (1 route)

| Bước | Chi phí |
|------|---------|
| Chọn route | $K = 10$ |
| Xây pool: served + top-4 unserved | $O(U \cdot K \cdot L)$ cho scoring, 1 lần/iter |
| Bitmask DP trên pool ~12 request (~20 node) | $O(2^{20} \times 20) \approx 20M$ ops |
| **Tổng 1 iteration (duyệt K routes)** | $10 \times 20M = 200M$ ops |
| **Thời gian** | ~200ms/iter |
| **Số iteration trong 100s** | ~500 |

Hơi ít. Nhưng mỗi iteration là **optimal** cho route đó → quality/iteration cực cao.

### Toán tử A-Reconstruct (giảm pool)

Nếu giới hạn pool size = 10 request (~14 node vật lý):

| | Chi phí |
|------|---------|
| Bitmask DP | $2^{14} \times 14 \approx 230K$ ops |
| 10 routes | $10 \times 230K = 2.3M$ ops |
| **Thời gian** | ~2.3ms/iter |
| **Số iteration trong 100s** | ~43,000 |

> [!NOTE]
> Đây là **sweet spot**: đủ nhiều iterations để Tabu Search hoạt động hiệu quả, đủ chính xác (optimal cho pool đã chọn).

### So sánh tổng thể

| Approach | Iterations/100s | Quality/Iteration | Tổng thể |
|----------|----------------|-------------------|----------|
| Solver hiện tại (ILS hỗn hợp) | ~100,000 | Thấp (random + greedy) | Tốt nhưng black-box |
| Toán tử A-EjectInject | ~2,500,000 | Thấp (heuristic) | Có thể tốt, cần tuning |
| A-Reconstruct (pool=20 node) | ~500 | **Cực cao** (exact DP) | Tốt cho small instances |
| A-Reconstruct (pool=14 node) | ~43,000 | Cao (exact nhưng pool giới hạn) | **Cân bằng tốt nhất** |

---

## Kết luận cập nhật

Với tri thức miền ($K \leq 10$, $L \leq 8$), bức tranh thay đổi hoàn toàn:

1. **Insert position KHÔNG phải vấn đề.** Route quá ngắn để brute-force vị trí nhét là tầm thường.

2. **Eject selection MỚI là vấn đề.** Cần heuristic scoring thông minh (delta + liberation) để tránh duyệt toàn bộ $\binom{S}{2}$.

3. **Bitmask DP (Operator X) là vũ khí tối thượng** cho bài toán này. Với pool ≤ 14 node vật lý, nó chạy trong ~0.02ms và cho kết quả **tối ưu chính xác** cho bất kỳ tập con nào.

4. **Chiến lược đề xuất cuối cùng:** Tabu Search + Route Reconstruction bằng Bitmask DP. Mỗi iteration chọn 1 (hoặc 2) route, gom pool (served + top unserved), chạy DP exact, cập nhật. Tabu cấm chọn lại route. Đơn giản, mạnh mẽ, transparent.
