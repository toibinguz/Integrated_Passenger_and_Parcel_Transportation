# BÁO CÁO PHÂN TÍCH PHẢN BIỆN, ĐẶC TẢ KIẾN TRÚC & MÃ GIẢ THUẬT TOÁN THẾ HỆ MỚI (SOICT 2026)

> [!NOTE]
> **Tài liệu tham chiếu dự án**: SOICT 2026 - Integrated Passenger and Parcel Transportation  
> **Mục tiêu tài liệu**: Đúc kết thực nghiệm, phản biện chặt chẽ các giả thuyết cũ, xác lập bộ tiêu chí thiết kế và cung cấp **khung sườn kiến trúc + mã giả chi tiết (AI-Ready Pseudocode)** cho thuật toán **Guided Multi-Basin SISR-SA (`gm_sisr`)**.  
> **Thời điểm cập nhật**: Tháng 10/2026  

---

## 1. NHÌN LẠI HÀNH TRÌNH THỰC NGHIỆM: SỰ THẬT TỪ DỮ LIỆU

Trong quá trình giải bài toán vận tải tích hợp người và hàng có cửa sổ thời gian và chọn lọc yêu cầu (Selective / Prize-Collecting PDPTW), các nguyên mẫu đã qua kiểm thử cho thấy bức tranh thực tế sau:

```
[Nhánh Duyệt Chính Xác & Phân Rã]
  ├── Exact MILP (SCIP) ─────────────────────> Bùng nổ tổ hợp khi V > 50, bất khả thi trong 5-10s.
  ├── Phase 1 (DFS Column Enum + Set Packing) -> Tối ưu tuyệt đối khi cửa sổ thời gian cực hẹp (d <= 3),
  │                                            nhưng gãy pha hoàn toàn khi chuỗi lộ trình dài (d >= 8).
  │
[Nhánh Vi Toán Tử (Micro-Moves: 1-Relocate, 2-Swap)]
  ├── Naive Tabu / DHTS-Tabu ────────────────> Bão hòa sớm (8,963 trên bench_01) do bán kính bước đi quá nhỏ.
  ├── LA-Tabu (Late Acceptance + Tabu) ──────> Thông lượng 2.18M iter/s nhưng kẹt cứng sau 3.6s (100% REJECT).
  │
[Nhánh Vĩ Toán Tử (Macro Ruin & Recreate)]
  ├── SISR-SA (Multi-cycle SA + icache) ─────> Baseline mạnh nhất hiện tại (~180k iter/s), thoát cực tiểu tốt.
  └── POOL-SISR (Route Pool + SP + Inject) ──> Không tạo được cải thiện robust: SP wins = 0, đường cong hội tụ
                                               trùng khớp với SISR-SA, chênh lệch chỉ là phương sai hạt giống.
```

---

## 2. MỔ XẺ NGUYÊN NHÂN GỐC RỄ: TẠI SAO CÁC CƠ CHẾ "TRÍ NHỚ" TRƯỚC ĐÂY THẤT BẠI?

### 2.1. Bài học từ `la_tabu.cpp`: Tốc độ vi mô không cứu được bán kính lân cận hẹp & cơ chế chấp nhận thụ động
1. **Bẫy bán kính vi mô (Micro-Move Horizon)**:
   - Trong PDPTW, để nhét thêm 1 yêu cầu có lợi nhuận cao vào lộ trình đã kín, thường phải dịch chuyển đồng thời 5–15 yêu cầu trên 2–3 xe để dồn độ trễ thời gian (Slack Induction).
   - Các phép biến đổi 1 khách (Relocate) hay 2 khách (Swap) trong [`la_tabu.cpp`](file:///z:/Desktop/250926/experiments/la_tabu/la_tabu.cpp) chỉ lăn dưới đáy của một bồn trũng đơn lẻ, bất lực trước các vách ngăn bất khả thi về Time Window.
2. **Sự sụp đổ của Late Acceptance (Passive Buffer Saturation)**:
   - Ở tốc độ $2.18 \times 10^6$ iter/s, bộ đệm lịch sử `L = 15,000` chỉ tương đương đúng **6.8 mili-giây**.
   - Khi chạm một đỉnh cục bộ, chỉ sau 6.8 ms không có bước đi cải thiện, toàn bộ mảng `la_history` bị lấp đầy bởi giá trị đỉnh đó ($\text{la\_bound} = \text{cur\_obj}$).
   - Thuật toán lập tức thoái hóa thành **leo đồi thuần tham lam (Greedy Hill-Climbing)**: trên `bench_01`, suốt 1.5 triệu vòng lặp cuối cùng, tỷ lệ từ chối là **100% REJECT**.

---

### 2.2. Bài học từ `pool_sisr.cpp`: Ba nguyên nhân thực sự khiến Route Pool & Recombination vô hiệu

Phân tích log thực nghiệm của [`pool_sisr.cpp`](file:///z:/Desktop/250926/experiments/pool_sisr/pool_sisr.cpp) cho thấy cả hai mô-đun **Set Packing (`greedy_assembly`)** lẫn **Single-Route Deduplication (`inject`)** đều không tạo ra bước nhảy thực chất so với [`sisr_sa.cpp`](file:///z:/Desktop/250926/experiments/sisr_sa/sisr_sa.cpp). Nguyên nhân nằm ở **3 lỗi cấu trúc**:

1. **"Sợi dây xích `load_best()`" triệt tiêu sự đa dạng bồn trũng (Zero Basin Diversity)**:
   - Tại dòng 527 của [`sisr_sa.cpp`](file:///z:/Desktop/250926/experiments/sisr_sa/sisr_sa.cpp#L527) và dòng 781 của [`pool_sisr.cpp`](file:///z:/Desktop/250926/experiments/pool_sisr/pool_sisr.cpp#L781), cứ mỗi `0.35s` chuyển chu kỳ, thuật toán gọi `load_best()`, **giật trạng thái tìm kiếm quay về đúng nghiệm `best_nd` duy nhất** kèm nhiệt độ tái nung thấp (`t_mid_h = 22.0`).
   - Toàn bộ các lộ trình tích lũy trong Pool qua 13 chu kỳ thực chất đều là các bản sao nhiễu nhẹ của **cùng một cực tiểu địa phương `best_nd`**. Lai ghép các thành phần từ cùng một bồn trũng không thể sinh ra nghiệm vượt thoát.
2. **Nghịch lý Xung đột Rời rạc trong Set Packing (`SP wins = 0 / 13`)**:
   - Các lộ trình tốt nhất ở các thời điểm khác nhau luôn chia sẻ chung các khách hàng "vàng". Đòi hỏi rời nhau 100% (pairwise disjoint) khiến các xe sau bị bỏ rỗng hoặc phải nhận lộ trình rác.
3. **Bẫy "Lộ trình Lỗ chỗ" (Swiss-Cheese Routes) khi Khấu trừ Khách trùng (`inject`)**:
   - Hàm [`inject(k, j)`](file:///z:/Desktop/250926/experiments/pool_sisr/pool_sisr.cpp#L528-L551) trong `pool_sisr.cpp` đã thực hiện gỡ khách trùng trên các xe khác rồi gọi `recreate`. Tuy nhiên, khi một xe chạy xa để phục vụ cụm 4 khách $\{u_1, u_2, u_3, u_4\}$, việc gỡ bỏ $u_2, u_3$ khiến xe vẫn phải chạy quãng đường xa chỉ để phục vụ $\{u_1, u_4\}$ (chi phí biên tăng vọt), đồng thời cửa sổ thời gian (`dep[]`, `lat[]`) vẫn bị khóa cứng ở cụm đó, tạo ra các lỗ thủng rời rạc tương đương `Random Ruin`.

---

### 2.3. Những khuyết tật tiềm ẩn ngay trong bản thân `sisr_sa.cpp` hiện tại
1. **Ngộ nhận về tính Bất biến Thang đo (False Scale-Invariance)**:
   - Tại dòng 535–536 của [`sisr_sa.cpp`](file:///z:/Desktop/250926/experiments/sisr_sa/sisr_sa.cpp#L535-L536), lịch nhiệt độ 3 pha chứa các hằng số tuyệt đối cứng (`32.0`, `22.0`, `2.5`).
2. **Thứ tự Recreate mù thông tin về độ khó của từng yêu cầu**:
   - `cand` chỉ được sắp xếp theo các quy tắc tĩnh (`random`, `Rev`, `ne`, `Rev - nic`), không biết yêu cầu nào đang bị thắt cổ chai (chỉ 1 xe nhận được).
3. **Ma trận Láng giềng `nbr` chưa chuẩn hóa và bỏ qua tính chất nối chuyến**:
   - Dòng 418 cộng trực tiếp `T(P[a], P[b]) + T(D[a], D[b]) + |ne[a] - ne[b]|` (khác đơn vị đo) và chỉ xét tương đồng song song ($P_a \leftrightarrow P_b, D_a \leftrightarrow D_b$), bỏ qua tương đồng **nối tiếp hành trình** ($D_a \to P_b$ khi xe trả khách $a$ xong đón tiếp khách $b$).

---

## 3. PHẢN BIỆN CÁC ĐỀ XUẤT CŨ: NHỮNG CÁI BẪY LÝ THUYẾT CẦN TRÁNH

| Đề xuất Cũ | Tại sao Bị Bác bỏ (Bẫy Toán học & Kỹ thuật) |
| :--- | :--- |
| **Bẫy 1: Regret-k cổ điển ($\text{Regret} = c_2 - c_1$, với $c_2 = \infty$ khi chỉ 1 xe nhận được)** | **1. Sai bản chất Prize-Collecting**: Phương án thay thế khi không chèn được vào xe thứ 2 có lợi nhuận bằng $0$ (bỏ qua khách), không phải chi phí $\infty$. Gán $c_2 = \infty$ sẽ ưu tiên tuyệt đối một khách rác có $\text{Rev}=100, c_1=99$ (lãi $+1$) lên trước khách vàng có $\text{Rev}=2000, c_1=200, c_2=350$ (lãi $+1800$).<br>**2. Mất tính ngẫu nhiên**: Regret-k thuần túy có tính tất định, gây lặp vòng (cycling).<br>**3. Phá vỡ tốc độ 1-pass của `icache`**: Tính Regret động bắt buộc gọi lại `best_insert` cho toàn bộ ứng viên sau mỗi lượt chèn, làm chậm tốc độ 2–3 lần. |
| **Bẫy 2: Broken-Arc Tabu (Cấm nối lại cung $(u,v)$ vừa cắt trong $T$ bước)** | **1. Phá hỏng bất biến `rver[k]` của `icache`**: Cung $(u,v)$ hết hạn Tabu ở vòng lặp `it + T` khi `rver[k]` không đổi làm `icache` trả kết quả sai.<br>**2. Bóp nghẹt Time Window**: Cấm toàn bộ 25–30 cung vừa cắt ngăn cản việc tái lập 80% khung xương hợp lệ. |
| **Bẫy 3: Worst-Detour Ruin đơn lẻ** | Nhổ khách lẻ có Detour cao rồi đưa ngay vào `cand` trong cùng vòng lặp (khi các khách xung quanh giữ nguyên) sẽ cắm khách đó trở lại đúng vị trí vừa nhổ. |
| **Bẫy 4: Dành 30% thời gian cuối cho Vi toán tử (Micro Local Search)** | Vi toán tử hội tụ chỉ trong $< 10\text{ ms}$. Đóng băng Ruin vĩ mô trong 30% thời gian cuối là tự vứt bỏ 5 chu kỳ nhảy bồn trũng. |

---

## 4. BỘ 5 TIÊU CHÍ BẮT BUỘC CỦA THUẬT TOÁN THẾ HỆ MỚI

1. **Tiêu chí 1 (Zero `icache` Overhead)**: Bảo toàn tuyệt đối cấu trúc duyệt **1-Pass `recreate`** và cơ chế versioning `rver[k]` của `icache` ($\ge 150,000$ iter/s). Không thêm bất kỳ lệnh gọi `best_insert` phụ nào.
2. **Tiêu chí 2 (Prize-Collecting Net Regret)**: Đánh giá độ hối tiếc dựa trên **Lợi nhuận biên ròng không âm** $g_k(q) = \max(0, \text{Rev}[q] - c_k(q))$, định nghĩa $\Delta g(q) = g_{(1)}(q) - g_{(2)}(q) \in [0, \text{Rev}[q]]$.
3. **Tiêu chí 3 (Stochastic Guided Memory)**: Thu thập miễn phí $\Delta g(q)$ ngay trong vòng lặp `recreate` để cập nhật trung bình trượt `ema_regret[q]`, dùng nó để dẫn đường cho bước chọn `seed` và sắp xếp `cand` **có nhân nhiễu ngẫu nhiên** nhằm bảo toàn tính đa dạng hóa.
4. **Tiêu chí 4 (Anti-Tether & True Scale-Invariance)**: Tháo bỏ sợi dây xích `load_best()` cứng nhắc ở mỗi biên chu kỳ và chuẩn hóa 100% mốc nhiệt độ theo thang năng lượng nội tại $S$ của bài toán.
5. **Tiêu chí 5 (Ablation & Multi-Seed Verification)**: Ghi nhận bộ đếm đóng góp kỷ lục của từng chế độ Ruin/Sort trong `report.txt`.

---

## 5. KHUNG SƯỜN KIẾN TRÚC THUẬT TOÁN: GUIDED MULTI-BASIN SISR-SA (`gm_sisr`)

### 5.1. Sơ đồ Luồng Hoạt động Tổng thể

```mermaid
flowchart TD
    INIT["Khởi tạo: Chuẩn hóa Neighbor + Greedy + Tính thang năng lượng S"] --> LOOP["Vòng lặp chính (1-Pass SISR Engine)"]

    subgraph HOT_LOOP ["Chu trình 1 Vòng lặp Nóng (Bảo toàn 100% O(1) icache)"]
        LOOP --> SEED["1. Chọn Seed (70% Uniform / 30% Tournament theo EMA Regret & Khách rớt)"]
        SEED --> RUIN["2. Toán tử Ruin Đa chế độ (75% String / 15% Unserved-Cavity / 10% Slack-Bottleneck)"]
        RUIN --> ORDER["3. Sắp xếp Cand (50% Noisy EMA Regret / 25% Noisy Net Density / 25% Shuffle)"]
        ORDER --> REC["4. Recreate 1-Pass qua icache (Trích xuất miễn phí g1, g2 -> Cập nhật EMA Regret)"]
        REC --> SA["5. Kiểm định SA (Nhiệt độ T nội suy không thứ nguyên theo S)"]
    end

    SA -->|Chấp nhận bước đi| UPD["Cập nhật cur_obj, best_nd"]
    SA -->|Từ chối| ROLL["Rollback O(1) qua backup & veh_log"]
    UPD --> CHECK_CYC{"Chuyển chu kỳ (0.35s)?"}
    ROLL --> CHECK_CYC

    CHECK_CYC -->|Chưa| LOOP
    CHECK_CYC -->|Rồi| ARCH["Cập nhật Diverse State Archive (P=4, Hamming Dist >= D_min)"]
    ARCH --> STAG{"Kiểm tra cycle_stag"}
    STAG -->|cycle_stag == 0: Có kỷ lục mới| S0["Gọi load_best() (Xoáy sâu bồn trũng mới)"]
    STAG -->|cycle_stag == 1: Mới chững 1 chu kỳ| S1["KHÔNG gọi load_best() nếu cur_obj gần best_obj (Đi tiếp + Tăng nhiệt)"]
    STAG -->|cycle_stag >= 2: Bồn trũng cạn kiệt| S2["Nạp nghiệm xa từ Archive + Cú hích Unserved-Cavity"]
    S0 --> LOOP
    S1 --> LOOP
    S2 --> LOOP
```

---

### 5.2. Đặc tả Chi tiết 4 Trụ cột Kỹ thuật

#### Trụ cột 1: Toán tử Neighbor & Bộ 3 Toán tử Phá hủy (Ruin)
1. **Ma trận Láng giềng Không-Thời gian Chuẩn hóa (`nbr[a]`)**:
   - Tính thời gian di chuyển trung bình $\bar{T}$ và độ lệch thời điểm mở cửa sổ trung bình $\bar{E}$ trên tập yêu cầu khả thi $\mathcal{S}$.
   - Với mỗi cặp yêu cầu $a \ne b \in \mathcal{S}$, xét cả **tương đồng song song** (đi chung chặng) và **tương đồng nối tiếp** (xe trả xong $a$ tại $D_a$ rồi đi đón $b$ tại $P_b$):
     $$d_{\text{para}}(a, b) = \frac{T(P_a, P_b) + T(D_a, D_b)}{2\bar{T}} + \frac{|ne_a - ne_b|}{\bar{E}}$$
     $$d_{\text{seq}}(a \to b) = \frac{T(\text{out}_a, P_b)}{\bar{T}} + \frac{|ne_b - (\text{end}_a)|}{\bar{E}}$$
     $$d(a, b) = \min\big(d_{\text{para}}(a, b),\; d_{\text{seq}}(a \to b),\; d_{\text{seq}}(b \to a)\big)$$
   - Trong đó $\text{out}_a = D_a$, và $\text{end}_a = ne_a + ndur_a$ (với khách) hoặc $ne_{R+a} + ndur_{R+a}$ (với hàng). Lưu `NB_SIZE = 60` láng giềng có $d(a, b)$ nhỏ nhất vào `nbr[a]`.
2. **Ba Chế độ Phá hủy (`ruin_mode`)**:
   - **Mode 1 — Spatio-Temporal String Ruin (75%)**: Tháo chuỗi liên tiếp độ dài $1 \dots l_{\max}$ trên `ks` xe chứa `seed` và các láng giềng trong `nbr[seed]`.
   - **Mode 2 — Unserved-Cavity Ruin (15%)**:
     - Chọn một yêu cầu đang bị bỏ rơi (`veh[q*] == 0`) có tiềm năng cao thông qua Tournament 3 phần tử theo điểm $\text{Rev}[q] - \text{nic}[q] + \text{ema\_regret}[q]$.
     - Duyệt qua các láng giềng $c \in \text{nbr}[q^*]$ đang nằm trên xe (`veh[c] != 0`), tháo các chuỗi con xung quanh vị trí của $c$ trên tối đa `ks` xe để tạo ra "hốc trống không-thời gian" đúng vào khung giờ của $q^*$.
     - Đánh dấu `priority_head = q*` để ép đưa $q^*$ lên vị trí đầu tiên (`cand[0]`) khi tái tạo.
   - **Mode 3 — Slack-Bottleneck Substring Ruin (10%)**:
     - Chọn một xe đang hoạt động $k$, tìm vị trí đỉnh $p \in [1, n_k]$ gây ra tổng **thời gian xe phải đứng chờ + chi phí đường vòng** lớn nhất:
       $$W_p = \max(0,\; ne_x - arr_p) + \big(C(prev, nin_x) + C(nout_x, next) - C(prev, next)\big)$$
     - Tháo chuỗi con độ dài `len` bao quanh vị trí $p$ trên xe $k$, kết hợp tháo thêm `ks - 1` chuỗi trên các xe lân cận của đỉnh $x$.

---

#### Trụ cột 2: Cơ chế Trí nhớ 1-Pass (Zero-Cost Piggybacked Memory)
1. **Cấu trúc lưu trữ phẳng $O(1)$**:
   - `ema_regret[1..R]`: Khởi tạo bằng $\max(0, \text{Rev}[q] - \text{nic}[q])$.
   - `unserved_streak[1..R]`: Đếm số lần liên tiếp yêu cầu $q$ được đưa vào `cand` nhưng không chèn được vào bất kỳ xe nào.
2. **Cập nhật miễn phí bên trong `recreate(cand)`**:
   - Khi duyệt $k = 1 \dots K$ để tìm vị trí chèn tốt nhất cho ứng viên $q$, ta tận dụng ngay `icache[q][k].cost` để lấy chi phí thấp nhất `best1` và thấp nhì `best2`.
   - Tính lợi nhuận ròng tương ứng:
     $$g_1 = \max(0,\; \text{Rev}[q] - \text{best1}), \qquad g_2 = \max(0,\; \text{Rev}[q] - \text{best2})$$
   - Cập nhật ngay lập tức:
     $$\text{ema\_regret}[q] \leftarrow 0.80 \cdot \text{ema\_regret}[q] + 0.20 \cdot (g_1 - g_2)$$
3. **Định hướng Sắp xếp `cand` có Nhiễu Ngẫu nhiên (Anti-Cycling)**:
   - Nếu có `priority_head` (từ Ruin Mode 2), giữ cố định `cand[0] = priority_head`, phần còn lại sắp xếp theo:
     - **50% (Mode A — Noisy Learned Regret)**: Sắp giảm dần theo $\text{ema\_regret}[q] \times \text{Uniform}(0.85, 1.15)$.
     - **25% (Mode B — Noisy Net Profit)**: Sắp giảm dần theo $(\text{Rev}[q] - \text{nic}[q]) \times \text{Uniform}(0.90, 1.10)$.
     - **25% (Mode C — Pure Random Shuffle)**: Hoán vị ngẫu nhiên đều.

---

#### Trụ cột 3: Chế độ Nhiệt Bất biến Thang đo & Điều khiển Chu kỳ Tháo xích
1. **Thang năng lượng nội tại $S$**:
   - Tính một lần khi khởi tạo:
     $$S = \max\left(1.0,\; \frac{1}{|\mathcal{S}|} \sum_{q \in \mathcal{S}} \max(0, \text{Rev}[q] - \text{nic}[q])\right)$$
2. **Lịch nhiệt độ 3 pha không thứ nguyên (Loại bỏ hoàn toàn `32.0`, `22.0`, `2.5`)**:
   - Hệ số nhiệt đầu chu kỳ:
     $$\alpha_{\text{start}} = \begin{cases} 0.60 & \text{nếu } \text{cyc} = 0 \\ \min(0.45,\; 0.20 + 0.05 \times \text{cycle\_stag}) & \text{nếu } \text{cyc} \ge 1 \end{cases}$$
   - Các mốc nhiệt độ trong chu kỳ:
     $$T_{\text{start}} = \alpha_{\text{start}} \cdot S, \quad T_{\text{mid\_h}} = \begin{cases} 0.10 \cdot S & (\text{cyc}=0) \\ 0.07 \cdot S & (\text{cyc}\ge 1) \end{cases}, \quad T_{\text{mid\_l}} = 0.008 \cdot S, \quad T_F = 0.0015 \cdot S$$
   - Nội suy hình học theo tiến độ chu kỳ $\text{prog} \in [0, 1]$:
     - $\text{prog} \in [0, 0.15)$: Hạ nhanh từ $T_{\text{start}} \to T_{\text{mid\_h}}$.
     - $\text{prog} \in [0.15, 0.85)$: Đi ngang dốc nhẹ trong vùng vàng $T_{\text{mid\_h}} \to T_{\text{mid\_l}}$.
     - $\text{prog} \in [0.85, 1.0]$: Đóng băng từ $T_{\text{mid\_l}} \to T_F$.
3. **Quy tắc Chuyển giao Chu kỳ 3 Trạng thái (Anti-Tether)**:
   - Cuối mỗi chu kỳ, kiểm tra biến cờ `improved_in_cycle` (chu kỳ vừa qua có phá kỷ lục `best_obj` hay không):
     - **Nếu CÓ (`cycle_stag = 0`)**: Gọi `load_best()` để xoáy sâu vào bồn trũng kỷ lục mới.
     - **Nếu KHÔNG (`cycle_stag++`)**:
       - Khi `cycle_stag == 1`: Nếu `cur_obj >= best_obj - 0.50 * S`, **KHÔNG gọi `load_best()`** (cho phép quỹ đạo hiện tại đi tiếp sang chu kỳ sau với nhiệt độ nung mới để vượt qua đèo năng lượng). Ngược lại mới gọi `load_best()`.
       - Khi `cycle_stag >= 2`: Nạp một nghiệm từ **Diverse State Archive** và kích hoạt **1 bước Cú hích (Basin Kick)** (tháo 2 chuỗi quanh 2 khách chưa phục vụ có doanh thu cao nhất rồi gọi `recreate` với `blink = false`).

---

#### Trụ cột 4: Cơ chế Pooling — Diverse State Archive ($P = 4$)
- **Bãi bỏ hoàn toàn Route-Pool Set Packing**: Không lưu cột lẻ, không giải bài toán phân hoạch tập hợp.
- **Lưu trữ $P = 4$ Nghiệm Hoàn chỉnh Đa dạng Cấu trúc**:
  - Mỗi slot $p \in \{0, 1, 2, 3\}$ lưu: điểm số `obj`, cấu trúc tuyến `nd[1..K]`, mảng gán xe `veh_map[1..R]`, và số lần đã dùng để khởi động lại `used_count`.
  - Slot `0` luôn đồng bộ với `best_nd` (`best_obj`).
  - Cuối mỗi chu kỳ, nghiệm hiện tại $S_{\text{cur}}$ được xét đưa vào các slot `1..3` nếu `cur_obj >= 0.96 * best_obj` VÀ khoảng cách Hamming về phân bổ xe so với **mọi** slot đang có trong Archive thỏa mãn:
    $$\text{dist}(S_{\text{cur}}, S_p) = \big|\{q \in \mathcal{S} : \text{veh}_{\text{cur}}[q] \ne \text{veh}_p[q]\}\big| \ge D_{\min}, \quad D_{\min} = \max\big(4,\; \lfloor 0.08 \times |\mathcal{S}| \rfloor\big)$$
  - Khi `cycle_stag >= 2`, thuật toán chọn slot $p \in \{0..\text{active}-1\}$ có chỉ số `obj - 0.15 * S * used_count` cao nhất để nạp làm điểm xuất phát cho chu kỳ mới, đảm bảo luân phiên khám phá các bồn trũng thực sự khác biệt.

---

## 6. MÃ GIẢ CHI TIẾT ĐỂ CÀI ĐẶT (AI-IMPLEMENTABLE PSEUDOCODE)

Mã giả dưới đây tương thích trực tiếp với cấu trúc dữ liệu nền của [`sisr_sa.cpp`](file:///z:/Desktop/250926/experiments/sisr_sa/sisr_sa.cpp) (`Route`, `recompute`, `best_insert`, `icache`, `rver`, `touch`, `remove_from_route`, `do_insert`).

### 6.1. Cấu trúc Dữ liệu Bổ sung Toàn cục

```cpp
// --- BỘ NHỚ ĐỊNH HƯỚNG 1-PASS (FLAT ARRAYS) ---
static vector<double> ema_regret;       // [R + 1]: Độ hối tiếc chọn lọc trung bình trượt
static vector<int> unserved_streak;     // [R + 1]: Số lần liên tiếp thất bại khi chèn
static double S_scale = 100.0;          // Thang lợi nhuận biên chuẩn hóa của thực thể bài toán
static int priority_head = -1;          // Yêu cầu ưu tiên đặt ở đầu cand (từ Ruin Mode 2)

// --- DIVERSE STATE ARCHIVE (P = 4) ---
static const int ARCHIVE_CAP = 4;
struct ArchiveState {
    ll obj = -1;
    vector<vector<int>> nd;             // [K + 1]
    vector<int> veh_map;                // [R + 1]
    int used_count = 0;
};
static ArchiveState archive_pool[ARCHIVE_CAP];
static int archive_size = 0;
static int D_min_hamming = 4;

// --- BỘ ĐẾM THỐNG KÊ ABLATION (GHI VÀO REPORT.TXT) ---
static long long mode_tries[4]   = {0, 0, 0, 0}; // Index 1..3 cho 3 chế độ Ruin
static long long mode_improve[4] = {0, 0, 0, 0}; // Số lần tạo delta > 0 theo chế độ Ruin
static long long mode_records[4] = {0, 0, 0, 0}; // Số lần phá kỷ lục best_obj theo chế độ Ruin
static long long sort_records[3] = {0, 0, 0};    // Số lần phá kỷ lục theo 3 chế độ Sort cand
```

---

### 6.2. Mã giả Khởi tạo Láng giềng Chuẩn hóa & Thang đo $S$

```cpp
FUNCTION init_neighbors_and_scale():
    // 1. Tính thang lợi nhuận biên S_scale và khởi tạo ema_regret
    double sum_net = 0.0;
    ema_regret.assign(R + 1, 0.0);
    unserved_streak.assign(R + 1, 0);
    FOR q IN servable_list:
        double net = max(1.0, (double)(Rev[q] - nic[q]));
        ema_regret[q] = net;
        sum_net += net;
    END FOR
    S_scale = (!servable_list.empty()) ? max(1.0, sum_net / servable_list.size()) : 100.0;
    D_min_hamming = max(4, (int)(0.08 * servable_list.size()));

    // 2. Tính trung bình thời gian di chuyển T_bar và độ lệch cửa sổ E_bar
    double sum_T = 0.0, sum_E = 0.0;
    long long pairs = 0;
    FOR a IN servable_list:
        FOR b IN servable_list WHERE b != a:
            sum_T += 0.5 * (T(P[a], P[b]) + T(D[a], D[b]));
            sum_E += std::abs(ne[a] - ne[b]);
            pairs++;
        END FOR
    END FOR
    double T_bar = (pairs > 0) ? max(1.0, sum_T / pairs) : 1.0;
    double E_bar = (pairs > 0) ? max(1.0, sum_E / pairs) : 1.0;

    // 3. Xây dựng danh sách láng giềng kết hợp Song song (Parallel) & Nối tiếp (Sequential)
    nbr.assign(R + 1, {});
    FOR a IN servable_list:
        vector<pair<double, int>> tmp;
        ll end_a = (a <= N) ? (ne[a] + ndur[a]) : (ne[R + a] + ndur[R + a]);
        FOR b IN servable_list WHERE b != a:
            ll end_b = (b <= N) ? (ne[b] + ndur[b]) : (ne[R + b] + ndur[R + b]);
            double d_para = (0.5 * (T(P[a], P[b]) + T(D[a], D[b])) / T_bar) + (std::abs(ne[a] - ne[b]) / E_bar);
            double d_seq1 = (T(D[a], P[b]) / T_bar) + (std::abs(ne[b] - end_a) / E_bar);
            double d_seq2 = (T(D[b], P[a]) / T_bar) + (std::abs(ne[a] - end_b) / E_bar);
            double d = min({d_para, d_seq1, d_seq2});
            tmp.push_back({d, b});
        END FOR
        int s = min((int)tmp.size(), NB_SIZE);
        partial_sort(tmp.begin(), tmp.begin() + s, tmp.end());
        FOR i = 0 TO s - 1:
            nbr[a].push_back(tmp[i].second);
        END FOR
    END FOR
END FUNCTION
```

---

### 6.3. Mã giả Chọn Hạt giống & Ba Chế độ Phá hủy (`ruin_guided`)

```cpp
FUNCTION select_seed() -> int:
    IF (rng() % 100) < 70:
        RETURN servable_list[rng() % servable_list.size()];
    ELSE:
        // Binary Tournament ưu tiên yêu cầu có độ hối tiếc cao hoặc đang bị bỏ rơi
        int a = servable_list[rng() % servable_list.size()];
        int b = servable_list[rng() % servable_list.size()];
        double sa = ema_regret[a] * (veh[a] == 0 ? 1.4 : 1.0);
        double sb = ema_regret[b] * (veh[b] == 0 ? 1.4 : 1.0);
        RETURN (sa >= sb) ? a : b;
    END IF
END FUNCTION

FUNCTION ruin_guided(int seed) -> int:
    removed.clear();
    priority_head = -1;

    int tot_nodes = 0, busy = 0;
    FOR k = 1 TO K:
        IF rt[k].n() > 0: tot_nodes += rt[k].n(); busy++;
    END FOR
    IF busy == 0: RETURN 1;

    double avg = (double)tot_nodes / busy;
    double ls_max = min((double)L_MAX, avg);
    double ks_max = 4.0 * C_AVG / (1.0 + ls_max) - 1.0;
    int ks = (int)(rand01() * ks_max) + 1;
    int done = 0;

    // Hàm nội bộ tháo 1 chuỗi con chứa yêu cầu c trên xe veh[c]
    LAMBDA try_route(int c):
        int k = veh[c];
        IF k == 0 OR ruin_stamp[k] == stamp_id: RETURN;
        ruin_stamp[k] = stamp_id;
        Route& r = rt[k];
        int n = r.n(), pos = 1;
        FOR i = 1 TO n:
            int q = (r.nd[i] > R) ? (r.nd[i] - R) : r.nd[i];
            IF q == c: pos = i; BREAK;
        END FOR
        int lmax = max(1, min(n, (int)ls_max));
        int len = rand_int(1, lmax);
        int lo = max(1, pos - len + 1), hi = min(pos, n - len + 1);
        int st = rand_int(lo, hi);
        tmp_reqs.clear();
        FOR i = st TO st + len - 1:
            int x = r.nd[i];
            tmp_reqs.push_back(x > R ? x - R : x);
        END FOR
        IF remove_from_route(k): done++;
    END LAMBDA

    int roll = rng() % 100;

    // --- MODE 2 (15%): UNSERVED-CAVITY RUIN (MỞ HỐC CHO KHÁCH BỊ BỎ RƠI) ---
    IF roll >= 75 AND roll < 90:
        int best_u = -1;
        double best_score = -1.0;
        // Lấy mẫu 6 ứng viên để tìm khách chưa phục vụ có giá trị cao nhất
        FOR t = 0 TO 5:
            int q = servable_list[rng() % servable_list.size()];
            IF veh[q] == 0:
                double sc = (Rev[q] - nic[q]) + ema_regret[q];
                IF sc > best_score: best_score = sc; best_u = q;
            END IF
        END FOR
        IF best_u != -1:
            priority_head = best_u;
            seed = best_u;
            FOR c IN nbr[best_u]:
                IF done >= ks: BREAK;
                IF veh[c] != 0: try_route(c);
            END FOR
            IF done > 0: RETURN 2;
        END IF
    END IF

    // --- MODE 3 (10%): SLACK-BOTTLENECK SUBSTRING RUIN ---
    IF roll >= 90:
        // Chọn xe k có chi phí lớn hơn qua Tournament 2 xe
        int k1 = rand_int(1, K), k2 = rand_int(1, K);
        int k = (rt[k1].cost >= rt[k2].cost) ? k1 : k2;
        IF rt[k].n() > 0:
            // Tìm đỉnh gây lãng phí (Wait Time + Detour Cost) lớn nhất trên xe k
            int worst_pos = 1;
            ll max_waste = -1;
            int n = rt[k].n();
            FOR i = 1 TO n:
                int x = rt[k].nd[i];
                int prev_v = (i == 1) ? O[k] : nout[rt[k].nd[i - 1]];
                ll arr = rt[k].dep[i - 1] + T(prev_v, nin[x]);
                ll wait_t = max(0LL, ne[x] - arr);
                ll detour_c = C(prev_v, nin[x]) + nic[x];
                IF i < n:
                    int next_v = nin[rt[k].nd[i + 1]];
                    detour_c += C(nout[x], next_v) - C(prev_v, next_v);
                END IF
                IF wait_t + detour_c > max_waste:
                    max_waste = wait_t + detour_c;
                    worst_pos = i;
                END IF
            END FOR
            int bottleneck_req = (rt[k].nd[worst_pos] > R) ? (rt[k].nd[worst_pos] - R) : rt[k].nd[worst_pos];
            try_route(bottleneck_req);
            FOR c IN nbr[bottleneck_req]:
                IF done >= ks: BREAK;
                try_route(c);
            END FOR
            RETURN 3;
        END IF
    END IF

    // --- MODE 1 (75% hoặc Fallback): SISR STRING RUIN ---
    try_route(seed);
    FOR c IN nbr[seed]:
        IF done >= ks: BREAK;
        try_route(c);
    END FOR
    RETURN 1;
END FUNCTION
```

---

### 6.4. Mã giả Sắp xếp Ứng viên & Recreate 1-Pass Tích hợp Trí nhớ

```cpp
FUNCTION order_candidates(vector<int>& cand) -> int:
    IF cand.size() <= 1: RETURN 0;

    // Nếu có priority_head từ Mode 2, giữ cố định ở vị trí đầu tiên cand[0]
    int start_idx = (priority_head != -1 && cand[0] == priority_head) ? 1 : 0;
    int sub_n = (int)cand.size() - start_idx;
    IF sub_n <= 1: RETURN 0;

    int sroll = rng() % 100;
    static vector<pair<double, int>> scored;

    IF sroll < 50:
        // Chế độ 0 (50%): Noisy Learned Net-Regret (Ưu tiên khách thắt cổ chai + nhiễu 15%)
        scored.resize(sub_n);
        FOR i = 0 TO sub_n - 1:
            int q = cand[start_idx + i];
            double noise = 0.85 + 0.30 * rand01();
            scored[i] = { -ema_regret[q] * noise, q }; // Dấu trừ để sort tăng dần = giảm dần theo điểm
        END FOR
        sort(scored.begin(), scored.end());
        FOR i = 0 TO sub_n - 1: cand[start_idx + i] = scored[i].second;
        RETURN 0;

    ELSE IF sroll < 75:
        // Chế độ 1 (25%): Noisy Net Profit Density
        scored.resize(sub_n);
        FOR i = 0 TO sub_n - 1:
            int q = cand[start_idx + i];
            double noise = 0.90 + 0.20 * rand01();
            scored[i] = { -(double)(Rev[q] - nic[q]) * noise, q };
        END FOR
        sort(scored.begin(), scored.end());
        FOR i = 0 TO sub_n - 1: cand[start_idx + i] = scored[i].second;
        RETURN 1;

    ELSE:
        // Chế độ 2 (25%): Pure Random Shuffle
        FOR i = sub_n - 1 DOWNTO 1:
            swap(cand[start_idx + i], cand[start_idx + (rng() % (i + 1))]);
        END FOR
        RETURN 2;
    END IF
END FUNCTION

// Recreate 1-Pass: Bảo toàn 100% bất biến icache và trích xuất miễn phí Net Regret
FUNCTION recreate_with_memory(const vector<int>& cand, bool blink):
    FOR q IN cand:
        IF veh[q] != 0: CONTINUE;

        ll best1 = INFLL, best2 = INFLL;
        int bk = -1, bi = 0, bj = 0;
        CacheEntry* ce = &icache[(size_t)q * (K + 1)];

        FOR k = 1 TO K:
            CacheEntry& e = ce[k];
            IF e.ver != rver[k]:
                e.cost = best_insert(k, q, e.i, e.j, blink);
                e.ver = rver[k];
            END IF
            IF e.cost < best1:
                best2 = best1;
                best1 = e.cost;
                bk = k; bi = e.i; bj = e.j;
            ELSE IF e.cost < best2:
                best2 = e.cost;
            END IF
        END FOR

        // --- CẬP NHẬT TRÍ NHỚ MIỄN PHÍ (ZERO EXTRA OVERHEAD) ---
        ll g1 = (best1 < INFLL) ? max(0LL, (ll)Rev[q] - best1) : 0LL;
        ll g2 = (best2 < INFLL) ? max(0LL, (ll)Rev[q] - best2) : 0LL;
        double step_regret = (double)(g1 - g2);
        ema_regret[q] = 0.80 * ema_regret[q] + 0.20 * step_regret;

        // --- QUYẾT ĐỊNH CHÈN ---
        IF bk != -1 AND Rev[q] - best1 > 0:
            do_insert(bk, q, bi, bj);
            unserved_streak[q] = 0;
        ELSE:
            unserved_streak[q]++;
        END IF
    END FOR
END FUNCTION
```

---

### 6.5. Mã giả Quản lý Kho Trạng thái Đa dạng (`Archive`) & Điều khiển Chu kỳ

```cpp
// Tính khoảng cách Hamming về phân bổ xe giữa nghiệm hiện tại và 1 slot trong Archive
FUNCTION hamming_distance(const vector<int>& vmap) -> int:
    int dist = 0;
    FOR q IN servable_list:
        IF veh[q] != vmap[q]: dist++;
    END FOR
    RETURN dist;
END FUNCTION

// Cập nhật Archive ở cuối mỗi chu kỳ (Chỉ lưu nghiệm chất lượng cao & khác biệt cấu trúc)
FUNCTION update_archive_at_cycle_end():
    // Slot 0 luôn giữ best_nd
    archive_pool[0].obj = best_obj;
    archive_pool[0].nd  = best_nd;
    // Cập nhật veh_map của best_nd
    archive_pool[0].veh_map.assign(R + 1, 0);
    FOR k = 1 TO K:
        FOR i = 1 TO (int)best_nd[k].size() - 1:
            int x = best_nd[k][i];
            IF x <= R: archive_pool[0].veh_map[x] = k;
        END FOR
    END FOR
    IF archive_size == 0: archive_size = 1;

    // Xét đưa nghiệm hiện tại (cur_obj) vào các slot 1..3 nếu đủ tốt và đủ khác biệt
    IF cur_obj < best_obj - (ll)(1.5 * S_scale): RETURN;
    FOR p = 0 TO archive_size - 1:
        IF hamming_distance(archive_pool[p].veh_map) < D_min_hamming:
            // Nếu quá giống 1 nghiệm đã có nhưng điểm cao hơn (với p > 0), nâng cấp slot đó
            IF p > 0 AND cur_obj > archive_pool[p].obj:
                archive_pool[p].obj = cur_obj;
                FOR k = 1 TO K: archive_pool[p].nd[k] = rt[k].nd;
                archive_pool[p].veh_map = veh;
            END IF
            RETURN;
        END IF
    END FOR

    // Nếu hoàn toàn khác biệt cấu trúc với mọi slot hiện có:
    int target_slot = -1;
    IF archive_size < ARCHIVE_CAP:
        target_slot = archive_size++;
    ELSE:
        // Thay thế slot có điểm thấp nhất trong 1..ARCHIVE_CAP-1 nếu cur_obj tốt hơn
        int worst_p = 1;
        FOR p = 2 TO ARCHIVE_CAP - 1:
            IF archive_pool[p].obj < archive_pool[worst_p].obj: worst_p = p;
        END FOR
        IF cur_obj > archive_pool[worst_p].obj: target_slot = worst_p;
    END IF

    IF target_slot != -1:
        archive_pool[target_slot].obj = cur_obj;
        archive_pool[target_slot].nd.resize(K + 1);
        FOR k = 1 TO K: archive_pool[target_slot].nd[k] = rt[k].nd;
        archive_pool[target_slot].veh_map = veh;
        archive_pool[target_slot].used_count = 0;
    END IF
END FUNCTION

// Nạp một slot từ Archive và thực hiện Cú hích Bồn trũng khi bế tắc >= 2 chu kỳ
FUNCTION jump_basin_from_archive():
    int best_p = 0;
    double best_val = -1e18;
    FOR p = 0 TO archive_size - 1:
        double val = (double)archive_pool[p].obj - 0.15 * S_scale * archive_pool[p].used_count;
        IF val > best_val: best_val = val; best_p = p;
    END FOR
    archive_pool[best_p].used_count++;

    // Nạp trạng thái từ slot best_p
    fill(veh.begin(), veh.end(), 0);
    FOR k = 1 TO K:
        rt[k].nd = archive_pool[best_p].nd[k];
        recompute(k);
        FOR i = 1 TO rt[k].n():
            int x = rt[k].nd[i];
            IF x <= R: veh[x] = k;
        END FOR
    END FOR
    cur_obj = archive_pool[best_p].obj;

    // Thực hiện 1 bước Directed Perturbation (ép mở chỗ cho khách chưa phục vụ)
    ++stamp_id; touched.clear(); veh_log.clear(); rev_delta = 0;
    int seed = select_seed();
    priority_head = -1;
    // Tìm khách đang rớt có giá trị cao nhất
    ll best_unserved_val = -1;
    FOR q IN servable_list:
        IF veh[q] == 0 AND (Rev[q] - nic[q]) > best_unserved_val:
            best_unserved_val = Rev[q] - nic[q];
            priority_head = q;
        END IF
    END FOR
    IF priority_head != -1:
        int done = 0;
        FOR c IN nbr[priority_head]:
            IF done >= 2: BREAK;
            int k = veh[c];
            IF k != 0 AND ruin_stamp[k] != stamp_id:
                ruin_stamp[k] = stamp_id;
                tmp_reqs.assign(1, c);
                IF remove_from_route(k): done++;
            END IF
        END FOR
        cand.clear();
        cand.push_back(priority_head);
        FOR q IN removed: cand.push_back(q);
        recreate_with_memory(cand, false); // Chèn chính xác (blink = false)
        ll dcost = 0;
        FOR k IN touched: dcost += rt[k].cost - backup[k].cost;
        cur_obj += rev_delta - dcost;
        IF cur_obj > best_obj: save_best();
    END IF
END FUNCTION
```

---

### 6.6. Mã giả Vòng lặp Chính (Chế độ Nhiệt Bất biến Thang đo & Tháo xích Chu kỳ)

```cpp
// Bên trong hàm main(), sau khi chạy Initial Greedy và gọi save_best() + update_archive_at_cycle_end():
int cur_cycle = 0;
int cycle_stag = 0;
bool improved_in_cycle = false;
double temp = 0.60 * S_scale;

FOR it = 0 TO INFINITY:
    IF (it & 15) == 0:
        elapsed = get_elapsed_seconds();
        IF elapsed >= TIME_LIMIT_SEC: BREAK;

        // Xác định chu kỳ cyc và tiến độ prog in [0, 1]
        (cyc, prog) = compute_cycle_and_progress(elapsed - search_start, d0, dsub);

        // --- XỬ LÝ CHUYỂN GIAO CHU KỲ (ANTI-TETHER LOGIC) ---
        IF cyc != cur_cycle:
            update_archive_at_cycle_end();
            IF improved_in_cycle:
                cycle_stag = 0;
                load_best(); // Xoáy sâu vào kỷ lục mới
            ELSE:
                cycle_stag++;
                IF cycle_stag == 1:
                    // Mới chững 1 chu kỳ: Cho đi tiếp nếu cur_obj chưa tụt quá xa
                    IF cur_obj < best_obj - (ll)(0.50 * S_scale):
                        load_best();
                    END IF
                ELSE:
                    // Đã chững >= 2 chu kỳ: Nhảy sang bồn trũng khác trong Archive + Kick
                    jump_basin_from_archive();
                END IF
            END IF
            cur_cycle = cyc;
            improved_in_cycle = false;
        END IF

        // --- CẬP NHẬT NHIỆT ĐỘ 3 PHA BẤT BIẾN THANG ĐO (100% SCALE-INVARIANT) ---
        double alpha_start = (cyc == 0) ? 0.60 : min(0.45, 0.20 + 0.05 * cycle_stag);
        double t_start = alpha_start * S_scale;
        double t_mid_h = ((cyc == 0) ? 0.10 : 0.07) * S_scale;
        double t_mid_l = 0.008 * S_scale;
        double t_final = 0.0015 * S_scale;

        IF prog < 0.15:
            temp = t_start * pow(t_mid_h / t_start, prog / 0.15);
        ELSE IF prog < 0.85:
            temp = t_mid_h * pow(t_mid_l / t_mid_h, (prog - 0.15) / 0.70);
        ELSE:
            temp = t_mid_l * pow(t_final / t_mid_l, (prog - 0.85) / 0.15);
        END IF
    END IF

    // --- BƯỚC ĐI RUIN & RECREATE ---
    ++stamp_id; touched.clear(); veh_log.clear(); rev_delta = 0;

    int seed = select_seed();
    int r_mode = ruin_guided(seed);
    mode_tries[r_mode]++;

    // Xây dựng danh sách ứng viên cand (priority_head nếu có sẽ được thêm đầu tiên)
    cand.clear();
    IF priority_head != -1: add_cand(priority_head);
    FOR q IN removed: add_cand(q);
    size_t nrem = cand.size();
    FOR t = 0 TO nrem - 1:
        FOR i = 0 TO min((int)nbr[cand[t]].size(), CAND_NB) - 1:
            add_cand(nbr[cand[t]][i]);
        END FOR
    END FOR
    add_cand(seed);
    FOR i = 0 TO min((int)nbr[seed].size(), CAND_NB) - 1: add_cand(nbr[seed][i]);
    FOR i = 0 TO CAND_RAND - 1: add_cand(servable_list[rng() % servable_list.size()]);

    int s_mode = order_candidates(cand);
    recreate_with_memory(cand, true);

    // --- ĐÁNH GIÁ & CHẤP NHẬN SA ---
    ll dcost = 0;
    FOR k IN touched: dcost += rt[k].cost - backup[k].cost;
    ll new_obj = cur_obj + rev_delta - dcost;
    ll delta = new_obj - cur_obj;

    IF delta >= 0 OR (double)delta > temp * log(rand01() + 1e-300):
        cur_obj = new_obj;
        IF delta > 0: mode_improve[r_mode]++;
        IF cur_obj > best_obj:
            save_best();
            improved_in_cycle = true;
            mode_records[r_mode]++;
            sort_records[s_mode]++;
        END IF
    ELSE:
        // Rollback O(1)
        FOR k IN touched: swap(rt[k], backup[k]); rver[k] = ++gver;
        FOR i = (int)veh_log.size() - 1 DOWNTO 0: veh[veh_log[i].first] = veh_log[i].second;
    END IF
END FOR
```

---

## 7. BẢNG TỪ ĐIỂN SIÊU THAM SỐ & PHÂN TÍCH ĐỘ NHẠY (HYPERPARAMETER DICTIONARY)

Mọi tham số trong `gm_sisr` đều được chuẩn hóa không thứ nguyên hoặc kế thừa trực tiếp từ các nghiên cứu LNS/SISR kinh điển (Christiaens & Berghe, 2020), loại bỏ hoàn toàn các hằng số fitness tuyệt đối.

| Nhóm Tham số | Tên Biến | Giá trị Chuẩn | Khoảng An toàn | Ý nghĩa Toán học & Cơ chế Bất biến Thang đo |
| :--- | :--- | :---: | :---: | :--- |
| **Không gian & Lân cận** | `NB_SIZE` | `60` | $[40, 80]$ | Số lượng láng giềng không-thời gian gần nhất lưu trong `nbr[a]`. |
| | `C_AVG` | `10.0` | $[6.0, 14.0]$ | Số lượng yêu cầu mục tiêu trung bình bị tháo dỡ trong 1 bước Ruin. |
| | `L_MAX` | `10` | $[6, 16]$ | Chiều dài chuỗi tối đa của một lần cắt trên 1 tuyến xe. |
| | `CAND_NB` | `3` | $[2, 5]$ | Số láng giềng chưa phục vụ của mỗi đỉnh bị tháo được đưa vào `cand`. |
| | `CAND_RAND` | `6` | $[3, 10]$ | Số yêu cầu chưa phục vụ ngẫu nhiên được đưa vào `cand` để tăng đột biến. |
| | `BLINK_MASK` | `63` | $\{31, 63, 127\}$ | Xác suất bỏ qua ngẫu nhiên vị trí tốt nhất trong `best_insert` ($1/64 \approx 1.56\%$). |
| **Trí nhớ 1-Pass** | `\alpha_{\text{regret}}` | `0.20` | $[0.10, 0.30]$ | Hệ số làm mượt trượt (EMA) của độ hối tiếc: $M_{\text{reg}} \leftarrow 0.8 M_{\text{reg}} + 0.2 \Delta g$. |
| | `JITTER_MIN` | `0.85` | $[0.80, 0.90]$ | Cận dưới của hệ số nhiễu nhân vào `ema_regret` khi sắp xếp `cand`. |
| | `JITTER_MAX` | `1.15` | $[1.10, 1.20]$ | Cận trên của hệ số nhiễu nhân vào `ema_regret` (ngăn chặn lặp vòng tất định). |
| **Diverse Archive** | `ARCHIVE_CAP` | `4` | $[3, 6]$ | Số lượng nghiệm hoàn chỉnh tối đa lưu trong kho Diverse State Archive. |
| | `D_MIN_RATIO` | `0.08` | $[0.05, 0.12]$ | Ngưỡng khoảng cách Hamming tối thiểu giữa các nghiệm trong kho ($8\% |\mathcal{S}|$). |
| | `PENALTY_USED` | `0.15` | $[0.10, 0.25]$ | Hệ số phạt tần suất sử dụng lại slot trong kho: $\text{val} = \text{obj} - 0.15 \cdot S \cdot \text{used}$. |

---

### 7.1. CƠ CHẾ NHIỆT ĐỘ KHÔNG THAM SỐ: BỘ ỔN NHIỆT TỰ ĐIỀU HÒA (SELF-REGULATING ACCEPTANCE THERMOSTAT)

Nhằm loại bỏ hoàn toàn các hằng số nhân tạo ("magic numbers" như các mốc nhiệt độ $T = 18.0$, hay các ngưỡng phân pha $12\%, 73\%, 15\%$), `gm_sisr` tích hợp **Chiến lược 2 (Thermostat Feedback)** với các đặc tính:

1. **Tiến độ Thời gian Thực Không Thứ Nguyên**:
   $$u = \frac{t - t_{\text{start}}}{\text{TIME\_LIMIT\_SEC} - t_{\text{start}}} \in [0, 1]$$
   Bảo đảm 100% độc lập với ngân sách phần cứng (từ 1 giây đến 100 giây đều có cùng quy luật).

2. **Quỹ đạo Mục tiêu Tỷ lệ Chấp nhận Tự nhiên**:
   $$\chi^*(u) = (1.0 - u)^2$$
   - Khi $u = 0.0$: $\chi^* = 1.00$ (Khám phá tự do, gần như mọi bước đi đều được chấp nhận).
   - Khi $u = 0.5$: $\chi^* = 0.25$ (Cân bằng giữa khai phá và hội tụ, thềm nhiệt độ trung gian vàng).
   - Khi $u \to 1.0$: $\chi^* \to 0.0$ (Cưỡng bức đóng băng tuyệt đối, hội tụ thuần tham lam tại đáy bồn trũng).

3. **Bóc tách Tỷ lệ Bước đi Leo Dốc Thực thụ ($\chi_{\text{uphill}}$)**:
   - *Phát hiện thực nghiệm quan trọng*: Trong SISR Ruin-and-Recreate, các bước đi trung hòa ($\Delta = 0$, giữ nguyên nghiệm hoặc hoán vị tương đương) chiếm tới $\approx 39\%$ tổng số bước. Do $\Delta \ge 0$ luôn được chấp nhận $100\%$ không phụ thuộc nhiệt độ, nếu tính gộp cả $\Delta = 0$ thì tỷ lệ chấp nhận không bao giờ xuống dưới $39\%$, khiến bộ điều nhiệt hiểu nhầm là hệ thống luôn quá nóng và dìm $T$ xuống sàn.
   - *Giải pháp triệt để*: Đo lường trực tiếp tỷ lệ chấp nhận trên **tập bước đi làm giảm điểm ($\Delta < 0$)**:
     $$\chi_{\text{cur}} = \frac{\text{Số bước } \Delta < 0 \text{ được chấp nhận}}{\text{Tổng số bước } \Delta < 0 \text{ trong cửa sổ } W = 256}$$
   - *Luật cập nhật phản hồi động*:
     $$e = \chi^*(u) - \chi_{\text{cur}}$$
     $$T \leftarrow \max\left(T_{\min}, \; \min\left(T_{\max}, \; T \times \exp(0.20 \cdot e)\right)\right)$$
   - *Động học vượt bồn trũng*: Khi kẹt tại bồn trũng cục bộ, $\chi_{\text{cur}} \to 0 \implies e > 0$. Nhiệt độ tự động được bơm tăng mượt mà cho đến khi năng lượng vượt qua vách ngăn ($\sim 20 - 40$ ms), cho phép nghiệm thoát bồn trũng sang thung lũng mới mà không cần lệnh nạp cưỡng bức hay nhảy trạng thái gián đoạn.

4. **Kết quả Đối đầu Thực nghiệm 63 Runs (7 Testcases $\times$ 3 Seeds, Budget 6.0s)**:
   - `gm_sisr@2` (Thermostat) đạt Net $+68.7$ điểm trung bình so với baseline `sisr_sa`.
   - Trên testcase quy mô lớn nhất `bench_07_dense_scale_500`: `gm_sisr@2` bứt phá ngoạn mục **+109.3 điểm** (Mean 31,655.3 vs 31,546.0; Max 31,712).
   - Trên `bench_02_loose_sequencing`: Xóa bỏ hoàn toàn hiện tượng đi ngang 2 giây của 1-cycle cũ, đạt Mean 15,714.3 và kỷ lục 15,756.

5. **Kết quả Đối đầu Chuyên sâu 30 Giây (7 Testcases, Budget 30.0s)**:
   - `gm_sisr@8` (SEDCA Frontier) **thắng tuyệt đối 7/7 testcases** trước baseline `sisr_sa`, tạo bước nhảy vọt **+192 điểm ròng** (Tổng: 120,236 vs 120,044).
   - `gm_sisr@4` (Frontier Breathing) tạo bước nhảy vọt **+213 điểm ròng** (Tổng: 120,257 vs 120,044), lập đỉnh cao nhất trên `bench_04` (18,109) và `bench_07` (31,701).
| **Nhiệt độ & Chu kỳ** | `\alpha_{\text{start}}^{(0)}` | `0.60` | $[0.40, 0.80]$ | Hệ số nhiệt độ ban đầu cho Chu kỳ 0: $T_0 = 0.60 \cdot S$. |
| | `\alpha_{\text{start}}^{(c)}` | `0.20` | $[0.15, 0.25]$ | Hệ số nhiệt độ ban đầu cơ bản cho các chu kỳ sau ($c \ge 1$): $T_0 = 0.20 \cdot S$. |
| | `\text{STAG\_BOOST}` | `0.05` | $[0.03, 0.08]$ | Độ tăng hệ số nhiệt độ cho mỗi chu kỳ bế tắc: $+0.05 \cdot \text{cycle\_stag} \cdot S$. |
| | `\text{MID\_H\_RATIO}` | `0.07` | $[0.05, 0.10]$ | Mốc nhiệt độ cao của thềm trung gian: $T_{\text{mid\_h}} = 0.07 \cdot S$ (ở $c \ge 1$). |
| | `\text{MID\_L\_RATIO}` | `0.008` | $[0.005, 0.012]$ | Mốc nhiệt độ thấp của thềm trung gian: $T_{\text{mid\_l}} = 0.008 \cdot S$. |
| | `\text{TF\_RATIO}` | `0.0015` | $[0.001, 0.003]$ | Mốc nhiệt độ đóng băng cuối chu kỳ: $T_F = 0.0015 \cdot S$. |
| | `BASE_D0` | `1.05s` | $[0.8s, 1.5s]$ | Thời lượng Chu kỳ 0 cho budget chuẩn 2.0s (tự co giãn $+30\%$ khi budget tăng). |
| | `BASE_DSUB` | `0.35s` | $[0.25s, 0.5s]$ | Thời lượng mỗi chu kỳ con $c \ge 1$. |

---

## 8. XỬ LÝ BIÊN & TÌNH HUỐNG NGOẠI LỆ (EDGE CASES & ROBUSTNESS)

Một thuật toán đạt chuẩn thi đấu bắt buộc phải tự động xử lý mượt mà mọi cấu trúc địa hình suy biến mà không bị crash, lặp vô tận hay vi phạm ràng buộc:

```mermaid
flowchart LR
    E1["Tập khách rớt rỗng (100% được phục vụ)"] --> F1["Mode 2 tự chuyển sang Mode 1 (String Ruin)"]
    E2["Số xe K = 1 hoặc K rất lớn (K >= 40)"] --> F2["Mảng phẳng tĩnh O(1) kiểm soát dung lượng"]
    E3["Archive chỉ có duy nhất 1 nghiệm"] --> F3["Basin Kick trực tiếp từ Slot 0 (best_nd)"]
    E4["Thang doanh thu nhân/chia 1000 lần"] --> F4["Nhiệt độ co giãn 100% theo S_scale nội tại"]
```

1. **Trường hợp Toàn bộ Khách đã được Phục vụ (`unserved_list` rỗng)**:
   - Trong Ruin Mode 2 (`Unserved-Cavity Ruin`), nếu duyệt tìm khách chưa phục vụ mà không thấy bất kỳ ai (`veh[q] == 0` không tồn tại), biến cờ `priority_head = -1`. Hàm lập tức chuyển hướng (fallback) thực hiện Ruin Mode 1 (`SISR String Ruin`), đảm bảo không có vòng lặp rỗng.
2. **Trường hợp Đội xe Cực nhỏ ($K = 1$) hoặc Cực lớn ($K \ge 40$)**:
   - Khi $K = 1$, phép chọn xe ngẫu nhiên hoặc binary tournament xe tự động quy về xe 1 mà không lỗi chia cho 0 (`k = 1`).
   - Khi $K$ lớn ($K \ge 40$), kích thước cache `icache` được cấp phát phẳng liên tục `(R + 1) * (K + 1)` trong 1 lần `malloc`/`assign` duy nhất từ hàm `main()`, hoàn toàn nằm trong bộ nhớ đệm L3 Cache của CPU, không cấp phát động.
3. **Trường hợp Diverse Archive chỉ có 1 Nghiệm duy nhất**:
   - Khi không gian nghiệm bị cô lập quá hẹp khiến thuật toán không tìm được nghiệm thứ 2 nào có khoảng cách Hamming $\ge D_{\min}$, `archive_size` vẫn giữ nguyên bằng 1.
   - Khi `cycle_stag >= 2`, hàm `jump_basin_from_archive()` nạp trực tiếp slot 0 (`best_nd`) và kích hoạt cú sốc `Directed Perturbation` ép tháo dỡ và chèn lại với `blink = false`, giúp tự đập vỡ cấu trúc của chính `best_nd` để tạo ra nghiệm mới.
4. **Trường hợp Cửa sổ Thời gian Không khả thi Ngay từ Đầu ($E_i > L_i$)**:
   - Đã được bộ đọc `servable[r] = 0` loại bỏ ngay khi nạp dữ liệu. Tập `servable_list` chỉ chứa các yêu cầu thực sự khả thi về mặt lý thuyết.

---

## 9. GIAO THỨC ĐO LƯỜNG ABLATION & CHỈ SỐ REPORT.TXT CHUẨN SOICT 2026

Để tuân thủ tuyệt đối quy định của [`AGENT.md`](file:///z:/Desktop/250926/AGENT.md) (*"Nghiêm cấm phán đoán bằng cảm giác; Đo lường định lượng đóng góp của từng thành phần"*), phiên bản hoàn chỉnh của solver bắt buộc phải xuất đầy đủ thông số giải phẫu (Ablation Breakdown) vào `report.txt`:

### 9.1. Cấu trúc Mẫu Báo cáo Hiệu năng (`report.txt`)

```
======================================================================
      GUIDED MULTI-BASIN SISR-SA (GM-SISR): FULL EXPERIMENT REPORT
======================================================================
  * Instance             : testcases/benchmark/bench_01_tight_assignment.txt
  * Problem Dimensions   : K=10, N=50, M=50, V=210, R=100
  * Wall-clock Budget    : 6.00s (Search time: 5.998s)
  * Random Seed          : 20261022
  * Initial Greedy Obj   : 8464
  * Final Best Objective : 9691 (Verified by Ground Truth: 9691)
  * Net Gain vs Greedy   : +1227 points

--- SEARCH THROUGHPUT & ACCEPTANCE PROFILE ---
  * Total Iterations     : 1,124,580 (187,492 iter/s)
  * Accepted Moves       : 146,195 (13.00%)
  * Local Improvements   : 41,208 (3.66%)
  * Global Records       : 312 steps
  * Total SA Cycles      : 15 cycles (Cycle 0: 1.05s, Sub-cycles: 0.35s)
  * Normalized Scale (S) : 88.42 points

--- ABLATION: RUIN OPERATOR CONTRIBUTION ---
  * Mode 1 (String Ruin) : Calls = 843,435 (75.0%) | Improves = 30,906 | Records = 224 (71.8%)
  * Mode 2 (Unserved-Cav): Calls = 168,687 (15.0%) | Improves =  6,593 | Records =  58 (18.6%)
  * Mode 3 (Bottleneck)  : Calls = 112,458 (10.0%) | Improves =  3,709 | Records =  30 ( 9.6%)

--- ABLATION: CANDIDATE SORT CONTRIBUTION ---
  * Sort Mode 0 (Regret) : Calls = 562,290 (50.0%) | Record Moves = 181 (58.0%)
  * Sort Mode 1 (Density): Calls = 281,145 (25.0%) | Record Moves =  78 (25.0%)
  * Sort Mode 2 (Shuffle): Calls = 281,145 (25.0%) | Record Moves =  53 (17.0%)

--- DIVERSE ARCHIVE & BASIN HOPPING DYNAMICS ---
  * Archive Slots Filled : 4 / 4 slots
  * Slot 0 (Best Global) : Obj = 9691 | Hamming Dist to Slot 1 = 12
  * Slot 1 (Basin B)     : Obj = 9668 | Hamming Dist to Slot 2 = 14
  * Slot 2 (Basin C)     : Obj = 9652 | Hamming Dist to Slot 3 = 11
  * Slot 3 (Basin D)     : Obj = 9639 | Hamming Dist to Slot 0 = 16
  * Basin Jumps Executed : 5 jumps (Jumps producing new global record: 2)
======================================================================
```

### 9.2. Tiêu chuẩn Thắng cuộc (Victory Condition)
Thuật toán `gm_sisr` được tuyên bố là **chiến thắng áp đảo và chính thức thay thế `sisr_sa`** khi và chỉ khi:
1. **Kiểm thử Multi-seed (7 testcases $\times$ 5 seeds = 35 cặp đối đầu = 70 runs độc lập)** với cùng budget thời gian:
   - Tổng điểm trung bình ròng $\Delta_{\text{Net Mean}} \ge +50.0$ điểm.
   - Thắng tối thiểu $5/7$ benchmark về chỉ số **Min (cận dưới an toàn)**.
   - Độ lệch chuẩn $\sigma$ trên các testcase lớn (`bench_07`) giảm hoặc tương đương `sisr_sa`.
2. **Ablation Xác nhận**:
   - Cả Mode 2 (`Unserved-Cavity Ruin`) và Sort Mode 0 (`Noisy Regret`) đều có tỷ lệ đóng góp kỷ lục $\ge 15\%$, chứng minh mã nguồn mới thực sự hoạt động và sinh ra giá trị chứ không phải mã chết.

---

## 10. LỘ TRÌNH TRIỂN KHAI THỰC TẾ (NEXT ACTION PLAN)

```
[Kế hoạch Triển khai 4 Bước Chuẩn AGENT.md]
  ├── Bước 1: Khởi tạo Thư mục Nguyên mẫu experiments/gm_sisr/
  │            ├── gm_sisr.cpp (Cài đặt C++ hoàn chỉnh theo mã giả Mục 6)
  │            └── plot_gm_sisr.py (Script Python vẽ 4 đồ thị chuyên biệt)
  │
  ├── Bước 2: Sanity Check & Validator Verification trên bench_01
  │            ├── Chạy thử 2.0s và kiểm tra tính hợp lệ bằng validator.py (Score verified 100%)
  │            └── Kiểm tra đủ Bộ Tứ Artifacts: result.txt, log.txt, report.txt, plots/
  │
  ├── Bước 3: Anti-Luck Multi-Seed Benchmark (70 runs, 5 seeds x 7 benchmarks)
  │            ├── Chạy benchmark tự động bằng run_gm_benchmark.py
  │            └── Kết xuất bảng so sánh trực diện với sisr_sa (Mean, Min, Max, StdDev)
  │
  └── Bước 4: Tổng kết Báo cáo & Đóng gói Mã nguồn Chốt
               └── Đánh giá Ablation và cập nhật kết quả vào BENCHMARK_REPORT.md
```

Tài liệu này xác lập nền tảng lý thuyết và đặc tả kỹ thuật hoàn chỉnh nhất cho thế hệ thuật toán tiếp theo, sẵn sàng cho việc hiện thực hóa mã nguồn.
