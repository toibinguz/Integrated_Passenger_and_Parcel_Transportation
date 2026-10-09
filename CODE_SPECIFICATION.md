# ĐẶC TẢ CHI TIẾT MÃ NGUỒN `tabu_prototype.cpp`

Tài liệu này đặc tả kỹ thuật toàn bộ cấu trúc dữ liệu, các hàm xử lý, thuật toán tìm kiếm và logic điều khiển được cài đặt trong file [`tabu_prototype.cpp`](file:///z:/Desktop/250926/tabu_prototype.cpp). Nội dung được trích xuất trực tiếp 100% từ mã nguồn hiện tại.

---

## 1. CẤU TRÚC DỮ LIỆU VÀ CÁC THAM SỐ TOÀN CỤC

### 1.1. Các hằng số và Tham số điều khiển (Dòng 14–33)
* `MAX_ITER = 10000`: Số vòng lặp tối đa của thuật toán Tabu Search (có thể ghi đè qua tham số dòng lệnh thứ 2).
* `TABU_TENURE_BASE = 5`, `TABU_TENURE_RAND = 10`: Biên độ thời gian cấm của Tabu:
  $$\text{Tenure} = 5 + (\text{rand}() \pmod{10}) \in [5, 14]$$
* `STAGNANT_LIMIT = 50`: Số vòng lặp liên tiếp không cải thiện điểm đỉnh trong Epoch hiện tại để kích hoạt cơ chế Ruin.
* `RUIN_SIZE = 6`: Số lượng yêu cầu tối đa bị loại bỏ trong một lần thực thi Ruin.
* `RUIN_MODES_COUNT = 4`: Số lượng chiến lược Ruin xoay vòng (0: Spatial Cost, 1: Travel Time, 2: Actual Arrival Time, 3: Vehicle Dismantle).
* `RUIN_TOP_CANDIDATE_POOL = 4`: Cửa sổ lấy mẫu ngẫu nhiên từ danh sách các ứng viên có độ tương quan cao nhất.
* `ELITE_POOL_SIZE = 6`: Dung lượng tối đa của Quần thể tinh hoa.
* `ELITE_QUALIFICATION_RATIO = 0.80`: Tỷ lệ điểm tối thiểu so với kỷ lục toàn cục (`global_best_obj * 0.80`) để một nghiệm được xét duyệt vào Quần thể tinh hoa.
* `INTENSIFICATION_PROB = 60`: Xác suất ($60\%$) chọn nghiệm tốt nhất trong Quần thể tinh hoa làm hạt giống cho pha Ruin; $40\%$ còn lại chọn ngẫu nhiên một nghiệm đa dạng khác.
* `MAXV = 1050`, `MAX_REQ = 1050`: Giới hạn kích thước mảng tĩnh cho số đỉnh và số yêu cầu.

### 1.2. Biến trạng thái toàn cục (Dòng 35–56)
* $K, N, M, V$: Số xe, số khách, số kiện hàng, tổng số đỉnh vật lý ($V = 2N + 2M + K$).
* $O[k], Q[k]$: Đỉnh xuất phát (Depot) và tải trọng tối đa của xe $k$ ($1 \le k \le K$).
* Với yêu cầu $r \in [1, N+M]$:
  * $req\_type[r]$: Loại yêu cầu (1: Khách nếu $r \le N$; 2: Hàng nếu $N < r \le N+M$).
  * $P[r], D[r]$: Đỉnh đón (Pickup) và đỉnh trả (Dropoff).
  * $W[r]$: Khối lượng kiện hàng (với khách, $W[r]$ không sử dụng).
  * $E[r], L\_time[r]$: Cửa sổ thời gian tại điểm đón.
  * $Ed[r], Ld[r]$: Cửa sổ thời gian tại điểm trả (chỉ áp dụng cho hàng).
  * $S[r]$: Thời gian dừng phục vụ tại mỗi điểm dừng.
  * $Rev[r]$: Doanh thu của yêu cầu.
* `t_mat[MAXV][MAXV]`, `c_mat[MAXV][MAXV]`: Ma trận thời gian và chi phí di chuyển giữa các đỉnh.
* `can_edge[MAXV][MAXV]`: Ma trận boolean tiền lọc tính khả thi của cạnh có hướng.
* `routes[MAXV]`: Mảng các `vector<int>` lưu danh sách tác vụ của từng xe $k$ ($1 \le k \le K$).
* `veh_of[MAX_REQ]`: Mảng lưu chỉ số xe phục vụ yêu cầu $r$ ($0$ nếu chưa được phục vụ).
* `route_obj[MAXV]`: Lợi nhuận hiện tại của từng xe.
* `cur_total_obj`, `global_best_obj`: Tổng lợi nhuận hiện tại và kỷ lục toàn cục.
* `best_routes[MAXV]`: Cấu trúc lộ trình tốt nhất toàn cục đã ghi nhận.
* `profitable[MAX_REQ]`: Đánh dấu yêu cầu có tiềm năng sinh lời.
* `has_edge[MAXV][MAXV]`: Đánh dấu cạnh có hướng $(u \to v)$ đang tồn tại trong bất kỳ lộ trình nào của toàn bộ hạm đội xe.
* `tabu_add[MAXV][MAXV]`: Mốc vòng lặp hết hạn cấm tạo lại cạnh $(u \to v)$.

---

## 2. TIỀN XỬ LÝ VÀ ĐỌC DỮ LIỆU

### 2.1. Đọc dữ liệu (`read_input` - Dòng 169–193)
1. Đọc $K, N, M$. Tính $V = 2N + 2M + K$.
2. Đọc $O[k], Q[k]$ cho $k \in [1, K]$.
3. Đọc dữ liệu khách $r \in [1, N]$: gán $req\_type[r] = 1$, đọc $P[r], D[r], E[r], L\_time[r], S[r], Rev[r]$.
4. Đọc dữ liệu hàng $r \in [N+1, N+M]$: gán $req\_type[r] = 2$, đọc $P[r], D[r], W[r], E[r], L\_time[r], Ed[r], Ld[r], S[r], Rev[r]$.
5. Đọc ma trận `t_mat` và `c_mat` kích thước $V \times V$.
6. Gán `profitable[i] = true` cho toàn bộ $i \in [1, N+M]$. Riêng với khách ($i \le N$), nếu $Rev[i] - c\_mat[P[i]][D[i]] < 0$ thì gán `profitable[i] = false`.
7. Gọi `init_edge_feasibility()`.

### 2.2. Khởi tạo tính khả thi cạnh (`init_edge_feasibility` - Dòng 137–167)
1. Tính mốc rời bến sớm nhất (`earliest_dep`) và mốc đến muộn nhất (`latest_arr`) cho từng đỉnh:
   * Với Depot $O[k]$: $earliest\_dep = 0$.
   * Với điểm đón khách $P[r]$: $latest\_arr = L\_time[r]$, $earliest\_dep = E[r] + S[r]$.
   * Với điểm trả khách $D[r]$: $earliest\_dep = E[r] + 2 \times S[r] + t\_mat[P[r]][D[r]]$.
   * Với điểm đón hàng $P[r]$: $latest\_arr = L\_time[r]$, $earliest\_dep = E[r] + S[r]$.
   * Với điểm trả hàng $D[r]$: $latest\_arr = Ld[r]$, $earliest\_dep = \max(Ed[r], E[r] + S[r] + t\_mat[P[r]][D[r]]) + S[r]$.
2. Xác định `can_edge[u][v] = (earliest_dep[u] + t_mat[u][v] <= latest_arr[v])`.
3. Cấm triệt để mọi cạnh đi vào bất kỳ Depot nào: `can_edge[u][O[k]] = false`.
4. Ràng buộc khách đi trực tiếp:
   * `can_edge[P[r]][v] = false` với mọi $v \ne D[r]$.
   * `can_edge[u][D[r]] = false` với mọi $u \ne P[r]$.

---

## 3. BIỂU DIỄN VÀ ĐÁNH GIÁ LỘ TRÌNH

### 3.1. Quy ước mã hóa tác vụ trong `routes[k]`
* Giá trị $t > 0$ và $t \le N$: Khách $t$ (vào $P[t]$, ra $D[t]$).
* Giá trị $t > 0$ và $t > N$: Đón kiện hàng $t$ (vào $P[t]$, ra $P[t]$).
* Giá trị $t < 0$: Trả kiện hàng $-t$ (vào $D[-t]$, ra $D[-t]$).
* Hàm `get_v_in(task)`: Trả về $P[|task|]$ nếu $task > 0$, ngược lại trả về $D[|task|]$.
* Hàm `get_v_out(task)`: Trả về $D[task]$ nếu $task > 0$ và là khách; ngược lại trả về `get_v_in(task)`.
* Hàm `get_physical_route(rt)`: Chuyển đổi dãy tác vụ thành dãy đỉnh vật lý liên tiếp. Khách $t$ được mở rộng thành 2 đỉnh liên tiếp: $P[t] \to D[t]$.

### 3.2. Hàm đánh giá tuyến (`eval_single_route` - Dòng 202–246)
* Nhận vào chỉ số xe $k$ và lộ trình `rt`. Nếu `rt` rỗng, trả về $0$.
* Sử dụng biến đếm thế hệ tĩnh `eval_token` và 2 mảng `picked_tag`, `dropped_tag`:
  * Mỗi lần gọi hàm, `eval_token++`. Nếu chạm ngưỡng $2 \times 10^9$, reset về $1$ và dùng `memset` xóa mảng tag về 0.
  * Trạng thái đã đón/đã trả của yêu cầu $r$ được kiểm tra qua so sánh `tag[r] == eval_token`.
* Khởi tạo $time\_now = 0$, $load = 0$, $curr = O[k]$, $benefit = 0$.
* Duyệt tuần tự từng tác vụ `task` trong `rt`:
  1. $time\_now += t\_mat[curr][v\_in]$, $benefit -= c\_mat[curr][v\_in]$.
  2. Nếu $task > 0$:
     * Nếu đã đón trước đó $\implies$ trả về $-\infty$.
     * $time\_now = \max(time\_now, E[r])$. Nếu $time\_now > L\_time[r] \implies$ trả về $-\infty$.
     * Đánh dấu đã đón (`picked_tag[r] = eval_token`).
     * Nếu là khách ($r \le N$):
       * $time\_now += S[r] + t\_mat[P[r]][D[r]] + S[r]$.
       * $benefit -= c\_mat[P[r]][D[r]]$, $benefit += Rev[r]$.
       * Đánh dấu đã trả (`dropped_tag[r] = eval_token`).
     * Nếu là hàng ($r > N$):
       * $load += W[r]$. Nếu $load > Q[k] \implies$ trả về $-\infty$.
       * $time\_now += S[r]$.
  3. Nếu $task < 0$ (trả hàng $-r$):
     * Nếu chưa đón hoặc đã trả $\implies$ trả về $-\infty$.
     * $time\_now = \max(time\_now, Ed[r])$. Nếu $time\_now > Ld[r] \implies$ trả về $-\infty$.
     * $load -= W[r]$, $benefit += Rev[r]$, $time\_now += S[r]$.
     * Đánh dấu đã trả (`dropped_tag[r] = eval_token`).
  4. $curr = v\_out$.
* Trả về `benefit`.

### 3.3. Tối ưu hóa cục bộ nội tuyến (`optimize_route` - Dòng 248–293)
* Thuật toán Relocate lặp:
  * Trích xuất danh sách các yêu cầu duy nhất có mặt trên tuyến.
  * Với mỗi yêu cầu $r$: tháo $r$ ra khỏi tuyến. Thử chèn lại vào tất cả các vị trí khả thi (có kiểm tra `can_edge`). Với hàng hóa, thử mọi cặp vị trí $(p_1, p_2)$ thỏa $p_1 < p_2$.
  * Nếu tìm được cấu hình có lợi nhuận cao hơn `best_b`, cập nhật tuyến và lặp lại (`while (improved)`).

### 3.4. Chèn tốt nhất (`best_insert` - Dòng 295–328)
* Thử chèn yêu cầu $r$ vào lộ trình `rt` của xe $k$:
  * Khách ($r \le N$): Thử mọi vị trí $pos \in [0, n]$, kiểm tra `can_edge[u][P[r]]` và `can_edge[D[r]][next]`.
  * Hàng ($r > N$): Thử mọi cặp vị trí $(p_1, p_2)$ với $0 \le p_1 < p_2 \le n+1$, kiểm tra `can_edge[u1][P[r]]`.
  * Đánh giá từng phương án bằng `eval_single_route`.
* Nếu có ít nhất một vị trí hợp lệ (`best_b != -INF`), gọi `optimize_route(k, best_rt)` để tối ưu hóa cục bộ tuyến kết quả, sau đó trả về `{best_b, best_rt}`.

---

## 4. QUẢN LÝ QUẦN THỂ TINH HOA (ELITE POOL)

### 4.1. Cấu trúc lưu trữ (`SolutionState`)
* `obj`: Điểm lợi nhuận.
* `veh_assignment`: Mảng kích thước $N+M+1$ lưu vector gán xe của từng yêu cầu.
* `routes`: Vector lưu danh sách tác vụ của $K$ xe.

### 4.2. Khoảng cách gán xe (`calc_assignment_dist` - Dòng 341–347)
* Tính khoảng cách Hamming giữa 2 vector gán xe:
  $$d(a, b) = \sum_{r=1}^{N+M} \mathbb{I}(a[r] \ne b[r])$$

### 4.3. Cập nhật Elite Pool (`update_elite_pool` - Dòng 410–502)
1. Bỏ qua nếu `cand.obj <= 0` hoặc `cand.obj < global_best_obj * 0.80`.
2. Kiểm tra trùng lặp cấu trúc với các cá thể hiện có trong pool:
   * Nếu tồn tại cá thể $i$ có $d = 0$:
     * Nếu `cand.obj > elite_pool[i].obj`: Ghi đè cá thể $i$ bằng `cand`, trả về `true`.
     * Ngược lại: Từ chối, trả về `false`.
3. Nếu kích thước pool $< 6$: Thêm trực tiếp `cand` vào pool, trả về `true`.
4. Nếu pool đã đầy ($6$ cá thể):
   * Tập hợp $T = 7$ cá thể (6 cá thể cũ + 1 ứng viên mới).
   * **Thứ hạng chất lượng ($R_{fit}$):** Sắp xếp 7 cá thể theo `obj` giảm dần, gán hạng từ 1 đến 7.
   * **Đo lường độ phân tán:** Với mỗi cá thể $i$, tính khoảng cách tối thiểu tới 6 cá thể còn lại: $min\_d[i] = \min_{j \ne i} d(i, j)$.
   * **Thứ hạng đa dạng ($R_{div}$):** Sắp xếp 7 cá thể theo $min\_d$ giảm dần, gán hạng từ 1 đến 7.
   * **Điểm phạt:** $Score[i] = R_{fit}[i] + R_{div}[i]$.
   * Tìm cá thể có $Score$ lớn nhất (ưu tiên cá thể có $R_{fit}$ tệ hơn nếu $Score$ bằng nhau).
   * Nếu cá thể bị đào thải chính là ứng viên mới (chỉ số $T-1$): Từ chối, trả về `false`.
   * Ngược lại: Thay thế cá thể bị đào thải trong `elite_pool` bằng `cand`, trả về `true`.

---

## 5. THUẬT TOÁN TÌM KIẾM TABU (CHÍNH)

Hàm `run_tabu_search()` (Dòng 512–830) thực thi toàn bộ tiến trình:

### 5.1. Khởi tạo
* Xóa mảng `has_edge`, `tabu_add`, `veh_of`, `route_obj` về 0.
* Khởi tạo `cur_total_obj = 0`, `global_best_obj = 0`, `elite_pool.clear()`.
* Thiết lập `epoch_idx = 1`, `epoch_iter = 0`, `epoch_stagnant_iters = 0`, `epoch_best_obj = 0`.
* Lambda `apply_edges(k, rt, add, iter)`: Duyệt qua các cạnh vật lý của `rt`, gán `has_edge[curr][v] = add` và `tabu_add[curr][v] = iter + 5 + rand() % 10`.

### 5.2. Đánh giá ứng viên lân cận (`test_candidate`)
* Tính $\Delta = (b_1 - route\_obj[k_1]) + \mathbb{I}(k_2 \ne -1)(b_2 - route\_obj[k_2])$.
* Lambda `check_route_edges`: Duyệt qua các cạnh vật lý của tuyến ứng viên. Nếu cạnh $(curr \to v)$ chưa có trong nghiệm hiện tại (`!has_edge[curr][v]`):
  * Tăng biến đếm `new_edges`.
  * Nếu `tabu_add[curr][v] >= iter` $\implies$ đánh dấu `is_tabu = true`.
* Nếu $type \ne 4$ (không phải Drop) và $new\_edges == 0 \implies$ Bỏ qua (không thay đổi cạnh).
* Kiểm tra Tabu:
  * Nếu `is_tabu == true`:
    * Tiêu chuẩn khát vọng: Nếu `cur_total_obj + delta > global_best_obj` $\implies$ chấp nhận (tăng biến đếm `tabu_overrides`).
    * Ngược lại $\implies$ Từ chối bước đi.
* So sánh cập nhật `best_move`: Nếu $\Delta > best\_move.delta$ (hoặc bằng nhau với xác suất $50\%$), gán $best\_move$ mới.

### 5.3. Không gian 5 toán tử lân cận
1. **Operator 1 (Insert - Dòng 577–584):** Duyệt mọi $u$ chưa phục vụ (`veh_of[u] == 0`) và có lãi. Thử chèn vào từng xe $k \in [1, K]$ qua `best_insert`.
2. **Operator 2 (Relocate - Dòng 586–598):** Duyệt mọi $r$ đang phục vụ trên $k_1$. Gỡ $r$ khỏi $k_1$, thử chèn vào các xe $k_2 \ne k_1$ qua `best_insert`.
3. **Operator 3 (Swap trên cùng xe - Dòng 600–610):** Duyệt mọi $r$ đang phục vụ trên $k$ và $u$ chưa phục vụ có lãi. Gỡ $r$ khỏi $k$, thử chèn $u$ vào tuyến vừa gỡ.
4. **Operator 4 (Drop - Dòng 612–619):** Duyệt mọi $r$ đang phục vụ trên $k$ và thử gỡ $r$ khỏi $k$, đưa về tập chưa phục vụ.
5. **Operator 5 (Inter-route Swap - Dòng 621–640):** Duyệt mọi cặp $r_1$ trên $k_1$ và $r_2$ trên $k_2$ ($k_1 \ne k_2$). Gỡ $r_1$ khỏi $k_1$ và chèn $r_2$; gỡ $r_2$ khỏi $k_2$ và chèn $r_1$.

### 5.4. Cập nhật bước đi tốt nhất
Nếu tìm được $best\_move$ hợp lệ:
1. Gỡ cạnh cũ và cập nhật cạnh mới vào `has_edge`, `tabu_add` cho $k_1$ (và $k_2$ nếu có).
2. Gán lại `routes` và cập nhật `route_obj`.
3. Cập nhật `veh_of` theo từng loại $type \in [1, 5]$.
4. Cập nhật `cur_total_obj += best_move.delta`.
5. Nếu `cur_total_obj > epoch_best_obj`: Cập nhật đỉnh Epoch, reset `epoch_stagnant_iters = 0`, lưu trạng thái `epoch_best_state`. Ngược lại: `epoch_stagnant_iters++`.
6. Nếu `cur_total_obj > global_best_obj`: Cập nhật kỷ lục toàn cục và sao lưu `best_routes`.

---

## 6. CƠ CHẾ RUIN VÀ TÁI THIẾT LẬP (EPOCH TRANSITION)

Kích hoạt khi `epoch_stagnant_iters >= STAGNANT_LIMIT` ($=50$):

### 6.1. Lưu trữ và Backtrack
1. Nếu `epoch_best_state.obj > 0`: Gọi `update_elite_pool`.
2. Chọn hạt giống từ Elite Pool:
   * Nếu pool không rỗng: Tìm cá thể có `obj` cao nhất (`best_elite_idx`).
   * Nếu kích thước pool $> 1$ và `rand() % 100 >= 60`: Chọn ngẫu nhiên một cá thể khác `best_elite_idx` (Đa dạng hóa 40%). Ngược lại: Chọn `best_elite_idx` (Tập trung hóa 60%).
   * Khôi phục toàn bộ `routes`, `veh_of`, cập nhật lại cạnh `apply_edges` và tính lại `route_obj`, `cur_total_obj` từ cá thể hạt giống đã chọn.

### 6.2. Bốn chiến lược Ruin xoay vòng (`strat = (epoch_idx - 1) % 4`)
Lập danh sách `served` gồm tất cả các yêu cầu đang được phục vụ (`veh_of[r] != 0`).

* **Nếu `strat == 3` (VEHICLE_DISMANTLE):**
  * Tìm các xe đang có lộ trình không rỗng.
  * Chọn ngẫu nhiên 1 xe $k_{tgt}$.
  * Gỡ cạnh của $k_{tgt}$, đặt `veh_of` của toàn bộ các yêu cầu trên xe về 0.
  * Xóa trắng `routes[k_tgt].clear()`, gán `route_obj[k_tgt] = 0`.

* **Nếu `strat \ne 3` (Modes 0, 1, 2 - Correlated Ruin):**
  * Chọn ngẫu nhiên một yêu cầu hạt giống $r_{seed} \in \text{served}$.
  * Nếu `strat == 2`: Gọi `get_actual_arrival_times()` để tính mốc thời gian phục vụ thực tế của từng yêu cầu trên lộ trình hiện tại.
  * Sắp xếp danh sách `served` tăng dần theo hàm đo lường:
    * `strat == 0`: $c\_mat[P[r_{seed}]][P[u]]$ (Chi phí không gian).
    * `strat == 1`: $t\_mat[P[r_{seed}]][P[u]]$ (Thời gian di chuyển).
    * `strat == 2`: $|arr\_time[r_{seed}] - arr\_time[u]|$ (Độ lệch thời điểm ghé thăm thực tế).
  * Lặp $num\_to\_ruin = \min(|\text{served}|, 6)$ lần: Lấy ngẫu nhiên một phần tử trong phạm vi $\min(|\text{served}|, 4)$ đầu danh sách, đưa vào `to_eject` và xóa khỏi `served`.
  * **Thực thi tháo dỡ:** Duyệt qua từng yêu cầu $r \in \text{to\_eject}$:
    * Gọi `remove_req` trên xe $k = veh\_of[r]$, cập nhật cạnh cũ/mới vào `has_edge` và `tabu_add`, tính lại `route_obj[k]`, gán `veh_of[r] = 0`.

### 6.3. Khởi tạo Epoch mới
* Tính lại `cur_total_obj = sum(route_obj)`.
* Tăng `epoch_idx++`, đặt `epoch_iter = 0`, `epoch_stagnant_iters = 0`.
* Gán `epoch_best_obj = cur_total_obj`, lưu `epoch_best_state`.

---

## 7. XUẤT KẾT QUẢ VÀ HÀM MAIN

* Hàm `main` đọc đường dẫn file testcase từ `argv[1]` (mặc định `"input.txt"`).
* Tạo thư mục con trong `output/` theo cấu trúc: `<test_name>_<cpp_name>_<timestamp>/`.
* Chuyển hướng toàn bộ luồng `cerr` vào file log `*_log.txt`.
* Gọi `read_input()`, sau đó gọi `run_tabu_search()`.
* Hàm `save_solution` ghi kết quả ra file `*_ket_qua.txt`:
  * Dòng 1: Điểm kỷ lục `global_best_obj`.
  * $K$ dòng tiếp theo: Mỗi dòng bắt đầu bằng số lượng đỉnh vật lý, theo sau là chuỗi các đỉnh vật lý của xe đó (chuyển đổi qua `get_physical_route`).

