# BẢN ĐỒ ĐỊA HÌNH NGHIỆM & CHIẾN LƯỢC PHÂN BỔ THUẬT TOÁN TỐI ƯU
## (SOICT 2026: Integrated Passenger and Parcel Transportation)

---

## 1. TỔNG QUAN & NGUYÊN LÝ "NO FREE LUNCH" TRONG VRPTW

Bài toán **SOICT 2026 (Integrated Passenger and Parcel Transportation)** là sự kết hợp phức tạp giữa:
- Bài toán định tuyến xe phục vụ hành khách có cửa sổ thời gian (Dial-a-Ride Problem with Time Windows - DARP).
- Bài toán giao nhận hàng hóa đa xe có sức chứa và cửa sổ thời gian kép (Multi-Vehicle Pickup and Delivery Problem with Time Windows and Capacity Constraints - m-PDPTW).
- Ràng buộc chuyến đi thẳng tuyệt đối của hành khách ($P_i^n \to D_i^n$ trực tiếp, không xen kẽ).

Theo định lý **No Free Lunch (Wolpert & Macready, 1997)**, không tồn tại một thuật toán đơn lẻ nào đạt hiệu năng tối ưu trên mọi phân bố dữ liệu đầu vào. Thực nghiệm sâu rộng trên hệ thống mã nguồn cho thấy:
- **Phase 1 (Column Enumeration + Set Packing)** đạt tốc độ cực hạn ($< 0.05\text{s}$) và **tiệm cận tối ưu tuyệt đối** trên một số cấu trúc địa hình cụ thể.
- **Phase 2 (SISR + Simulated Annealing / ALNS)** lại thể hiện sự vượt trội khi không gian tổ hợp bùng nổ, nơi Phase 1 hoàn toàn bị tê liệt.

Tài liệu này xác lập:
1. **Bản đồ phân loại địa hình nghiệm (Fitness Landscapes Taxonomy)**.
2. **Bằng chứng thực nghiệm và phạm vi áp dụng tối ưu của từng thuật toán**.
3. **Phân tích vai trò cốt lõi của Bất đẳng thức Tam giác (Triangle Inequality)**.
4. **Kiến trúc Solver Tích hợp Đa pha (Adaptive Portfolio Solver Architecture)** cho bản chốt chính thức [`submissions/submission.cpp`](file:///z:/Desktop/250926/submissions/submission.cpp).

---

## 2. BẢN ĐỒ ĐỊA HÌNH NGHIỆM (PROBLEM LANDSCAPES TAXONOMY)

Dựa trên phân tích hình thái dữ liệu, không gian bài toán SOICT 2026 được chia thành **5 hình thái địa hình cơ bản**:

```
                              BẢN ĐỒ ĐỊA HÌNH NGHIỆM SOICT 2026
                                               │
             ┌─────────────────────────────────┴─────────────────────────────────┐
             ▼                                                                   ▼
    CỬA SỔ HẸP & HORIZON NGẮN                                        CỬA SỔ RỘNG & HORIZON DÀI
    (R/K <= 4, Slack <= 35)                                          (R/K >= 6, Slack >= 100)
             │                                                                   │
    ┌────────┴────────┐                                                 ┌────────┴────────┐
    ▼                 ▼                                                 ▼                 ▼
[Landscape 1]     [Landscape 5]                                     [Landscape 2]     [Landscape 3 & 4]
Pinpoint Windows  Asymmetric Flow                                   Loose Sequencing  Clustered / Capacity
(DFS Set Packing  (Directed Local Search)                           (SISR + SA)       (Spatial Ruin / Knapsack)
 --> OPTIMAL)
```

---

### 2.1. Landscape 1: Pinpoint Windows & Metric Graph (Cửa sổ Thời gian Cực hẹp)

#### Đặc trưng Định lượng:
- **Tỉ số Chặng trên Xe ($R/K$)**: Nhỏ, thường $R/K \le 3 \sim 4$.
- **Độ hẹp Cửa sổ Thời gian ($\overline{\text{slack}}$)**: Rất hẹp: $L_i - E_i \in [5, 25]$ (trung bình $\le 20$).
- **Độ chặt Horizon ($H / \overline{t}$)**: Nhỏ: Toàn bộ đường chân trời thời gian $H$ chỉ gấp $3 \sim 5$ lần thời gian di chuyển trung bình $\overline{t}$.
- **Tính chất Không gian**: Đồ thị đối xứng thỏa mãn Bất đẳng thức Tam giác ($t(u, v) = t(v, u)$ và $t(u, w) \le t(u, v) + t(v, w)$).
- **Ví dụ điển hình**: Testcase chính thức [`testcases/legacy_perturbation/test_500.txt`](file:///z:/Desktop/250926/testcases/legacy_perturbation/test_500.txt) ($V=500, H=244, \overline{t}=50, \overline{\text{slack}}=13.8$), toàn bộ bộ kiểm thử [`testcases/phase1_ladder/`](file:///z:/Desktop/250926/testcases/phase1_ladder/).

#### Thuật toán Thống trị:
**Phase 1: Liệt kê Cột DFS + Cận trên Đối ngẫu Lagrangian + Set Packing SA**.

#### Bằng chứng Thực nghiệm & Giới hạn Trần:
Trên bộ testcase bậc thang `phase1_ladder` (đo lường trần hiệu năng tại [`utility/benchmark_phase1.py`](file:///z:/Desktop/250926/utility/benchmark_phase1.py)):

| Quy mô | Đỉnh ($V$) | Yêu cầu ($R$) | Xe ($K$) | Số cột sinh ra | Thời gian DFS | Gap so với Cận trên | Trạng thái Nghiệm |
|---|---|---|---|---|---|---|---|
| **Ladder 01** | 100 | 48 | 4 | 7,619 | 0.003s | **0 (0.00%)** | **Tối ưu Toàn cục** |
| **Ladder 02** | 200 | 96 | 8 | 15,238 | 0.005s | **0 (0.00%)** | **Tối ưu Toàn cục** |
| **Ladder 03** | 350 | 170 | 10 | 19,047 | 0.007s | **0 (0.00%)** | **Tối ưu Toàn cục** |
| **Ladder 04** | 500 | 240 | 20 | 38,095 | 0.013s | **0 (0.00%)** | **Tối ưu Toàn cục** |
| **Ladder 05** | 650 | 315 | 20 | 38,095 | 0.013s | **0 (0.00%)** | **Tối ưu Toàn cục** |
| **Ladder 06** | 800 | 385 | 30 | 57,142 | 0.020s | **0 (0.00%)** | **Tối ưu Toàn cục** |
| **Ladder 07** | 1000 | 480 | 40 | 76,190 | 0.026s | **22 (0.09%)** | **Tiệm cận Tối ưu** |
| **Ladder 08** | 1200 | 580 | 40 | 76,190 | 0.026s | **7 (0.02%)** | **Tiệm cận Tối ưu** |

#### Tại sao Phase 1 đạt Tiệm cận Tối ưu trên Địa hình này?
1. **Chiều sâu Lộ trình bị Khóa cứng ($d \le 3$)**: Vì $H \approx 250$ và mỗi chuyến đi tốn $t \approx 50$, một xe về mặt vật lý không thể phục vụ quá 2 đến 3 đơn trước khi hết thời gian. Cây tìm kiếm DFS bị cắt cụt tự nhiên ở độ sâu $d \le 3$.
2. **Không gian Cột Siêu nhỏ**: Tổng số cột khả thi của toàn bộ $K$ xe chỉ dao động từ vài nghìn đến dưới $80,000$ cột. Thuật toán DFS duyệt sạch $100\%$ không gian cột trong chưa đầy **$0.03$ giây**.
3. **Set Packing giải quyết triệt để xung đột**: Khi các cột đơn lẻ đã là tối ưu cục bộ, bài toán quy về chọn ra tập cột không giao nhau có tổng doanh thu lớn nhất. Thuật toán Simulated Annealing trên mô hình Set Packing tìm ra lời giải tối ưu toàn cục chỉ sau $0.5\text{s} - 3\text{s}$.

#### Điểm Gãy Pha (Phase Transition Breakdown):
Khi nới lỏng cửa sổ thời gian từ $L - E = 20$ lên $L - E \ge 40$, số lượng đỉnh tương thích bùng nổ hàm mũ. Số cột sinh ra vượt quá $2,600,000$ cột ở quy mô $V = 200$, làm tràn mảng tĩnh `ENUM_STATE_CAP = 3,000,000`, dẫn đến DFS bị cắt cụt do hết bộ nhớ đệm và bỏ sót các cột tối ưu. **Phase 1 bị gãy pha hoàn toàn khi cửa sổ thời gian bị nới lỏng.**

---

### 2.2. Landscape 2: Loose Sequencing & Long Horizons (Cửa sổ Rộng, Chuỗi Sâu)

#### Đặc trưng Định lượng:
- **Tỉ số Chặng trên Xe ($R/K$)**: Lớn, $R/K \ge 8 \sim 15$.
- **Độ hẹp Cửa sổ Thời gian ($\overline{\text{slack}}$)**: Rất rộng: $L_i - E_i \ge 500 \sim 2500$.
- **Độ sâu Lộ trình**: Mỗi xe phục vụ từ $8$ đến $20$ yêu cầu (chuỗi hành trình dài $20 \sim 40$ đỉnh).
- **Không gian Cột Khả thi**: $> 10^8$ cột (hoàn toàn bất khả thi cho việc duyệt toàn bộ bằng DFS).
- **Ví dụ điển hình**: [`testcases/benchmark/bench_02_loose_sequencing.txt`](file:///z:/Desktop/250926/testcases/benchmark/bench_02_loose_sequencing.txt) ($V=248, K=8, H=8000$).

#### Thuật toán Thống trị:
**Phase 2: SISR (Slack Induction by String Removal) + Multi-mode Recreate + Simulated Annealing (Metaheuristics)**.

#### Tại sao Phase 2 Thống trị trên Địa hình này?
1. **Phá hủy Chuỗi Phụ thuộc (String Removal)**: Với lộ trình dài, việc di chuyển từng đỉnh đơn lẻ (như Relocate hay Swap) rất dễ vi phạm ràng buộc thứ tự $P \prec D$ hoặc làm vỡ cửa sổ thời gian. SISR tháo gỡ cả một chuỗi liên tục các điểm dừng liên quan, tạo ra khoảng trống thời gian lớn (slack induction) để tái cấu trúc toàn diện.
2. **Kiểm tra Tính Khả thi Hậu tố trong $\mathcal{O}(1)$ via `lat[]`**: Nhờ mảng thời điểm muộn nhất cho phép `lat[]`, việc thử chèn cặp $(P_j, D_j)$ vào hai vị trí bất kỳ trong lộ trình $m$ đỉnh chỉ tốn $\mathcal{O}(1)$ thay vì phải duyệt lại toàn bộ lộ trình $\mathcal{O}(m)$.
3. **Tốc độ Khám phá Cực lớn**: Đạt trên **$500,000$ đến $1,000,000$ phép thử/giây** trên CPU, liên tục vượt qua các rào cản năng lượng của cực tiểu địa phương.

---

### 2.3. Landscape 3: Clustered Basins (Hố Sâu Cực tiểu Không gian)

#### Đặc trưng Định lượng:
- Các yêu cầu phân bố thành các cụm địa lý tách biệt (4 - 6 cụm đô thị lớn). Khoảng cách nội cụm ngắn, khoảng cách liên cụm rất dài.
- **Hiện tượng**: Thuật toán dễ bị "kẹt hố" (Local Minima Basin) khi một xe gom hàng rải rác ở nhiều cụm khác nhau, gây lãng phí chi phí di chuyển liên cụm mà các phép di chuyển nhỏ lẻ không thể tự thoát ra.
- **Ví dụ điển hình**: [`testcases/benchmark/bench_03_clustered_basins.txt`](file:///z:/Desktop/250926/testcases/benchmark/bench_03_clustered_basins.txt) ($V=250, K=10, 4 \text{ Cụm}$).

#### Thuật toán Thống trị:
**SISR với Toán tử Phá hủy Cụm Không gian (Spatial Clustering Ruin) + Variable Neighborhood Search (VNS)**.
- Khi chọn đỉnh hạt giống $u$, toán tử phá hủy sẽ đồng thời tháo gỡ toàn bộ các đỉnh nằm trong bán kính không gian lân cận của $u$ (hoặc cùng thuộc một xe đi sai cụm), buộc thuật toán phải tái phân bổ các đơn hàng về đúng cụm địa lý tối ưu.

---

### 2.4. Landscape 4: Capacity Bottlenecks (Nghẽn Tải trọng Bưu kiện)

#### Đặc trưng Định lượng:
- Số lượng đơn bưu kiện $M$ áp đảo đơn khách $N$ ($M / N \ge 2$).
- Khối lượng bưu kiện $w_j$ lớn so với sức chứa $Q_k$ của xe ($\sum w_j \gg \sum Q_k$).
- Ràng buộc thời gian lỏng, nhưng tính khả thi bị chi phối 90% bởi **bài toán ba lô (Knapsack Feasibility)**.
- **Ví dụ điển hình**: [`testcases/benchmark/bench_04_capacity_bottleneck.txt`](file:///z:/Desktop/250926/testcases/benchmark/bench_04_capacity_bottleneck.txt) ($V=250, K=10, N=40, M=80, Q_k \le 45, w \in [12, 28]$).

#### Thuật toán Thống trị:
**Knapsack-guided Recreate Heuristic + Direct Passenger Interleaving**.
- Ưu tiên chọn bưu kiện có tỷ số Doanh thu / Trọng lượng ($T_j / w_j$) cao nhất để đóng gói vào các xe trước khi xem xét hành khách.
- Khách hàng không chiếm tải trọng $Q_k$, do đó các chuyến đi thẳng của khách đóng vai trò làm cầu nối "chuyển vùng" giữa các cụm giao hàng.

---

### 2.5. Landscape 5: Directed Asymmetric Metric Graphs (Đồ thị Có hướng Bất đối xứng)

#### Đặc trưng Định lượng:
- Ma trận thời gian và chi phí có hướng: $t(u, v) \ne t(v, u)$ và $c(u, v) \ne c(v, u)$.
- Vẫn thỏa mãn Bất đẳng thức Tam giác ($t(u, w) \le t(u, v) + t(v, w)$).
- **Ví dụ điển hình**: [`testcases/benchmark/bench_05_asymmetric_random.txt`](file:///z:/Desktop/250926/testcases/benchmark/bench_05_asymmetric_random.txt), [`testcases/benchmark/bench_06_asymmetric_metric.txt`](file:///z:/Desktop/250926/testcases/benchmark/bench_06_asymmetric_metric.txt).

#### Thuật toán Thống trị:
**Toán tử Lân cận Có hướng (Directed-Only Operators: Relocate, Directed 2-Opt / Or-Opt, Exchange)**.
- **Cấm kỵ tuyệt đối**: Nghiêm cấm dùng toán tử 2-Opt đảo ngược chuỗi đoạn con truyền thống (`reverse(subroute)`), vì trên đồ thị có hướng, chi phí đi ngược $c(v_{i+1}, v_i)$ hoàn toàn khác $c(v_i, v_{i+1})$ và có thể phá vỡ toàn bộ lịch trình thời gian hợp lệ.
- Bắt buộc dùng các toán tử bảo toàn hướng di chuyển của các đoạn con (Or-Opt dịch chuyển đoạn hoặc Relocate có hướng).

---

## 3. PHÂN TÍCH CHUYÊN SÂU: VAI TRÒ CỐT LÕI CỦA BẤT ĐẲNG THỨC TAM GIÁC

### 3.1. Tại sao Đồ thị Phi Metric (Vi phạm BĐT Tam giác) làm Bài toán Trở nên Cực khó?

Trong lý thuyết đồ thị và tối ưu hóa vận tải (VRPTW):
1. **Mất tính Đơn điệu khi Cắt tỉa (Pruning Monotonicity Failure)**:
   - Trong không gian Metric thỏa mãn BĐT tam giác ($t(u, w) \le t(u, v) + t(v, w)$), việc bỏ qua một đỉnh trung gian $v$ **chắc chắn không làm tăng thời gian hoặc chi phí**:
     $$t(u, w) \le t(u, v) + t(v, w) \implies \text{Đi tắt luôn ngắn hơn hoặc bằng đi vòng}$$
   - Nếu vi phạm BĐT tam giác ($t(u, w) > t(u, v) + t(v, w)$), việc đi thẳng từ $u$ sang $w$ lại chậm hơn hoặc tốn xăng hơn đi vòng qua đỉnh ngẫu nhiên $v$.
   - Khi đó, các thuật toán cận dưới (Bounding), cắt tỉa DFS trong Phase 1, và hàm đánh giá backward slack $\mathcal{O}(1)$ trong Phase 2 **bị mất tính đơn điệu**, khiến không gian trạng thái bị vỡ vụn. Một lời giải có thể "tự nhiên tốt hơn" nếu xe cố tình ghé qua những đỉnh vô nghĩa chỉ để tận dụng các cạnh rẻ bất thường.
2. **Nghịch lý Thuật toán Chèn Rẻ nhất (Cheapest Insertion Anomaly)**:
   - Các thuật toán Heuristic chèn điểm $v$ vào giữa $(u, w)$ tính chi phí tăng thêm:
     $$\Delta c = c(u, v) + c(v, w) - c(u, w)$$
   - Trong đồ thị vi phạm BĐT tam giác, $\Delta c$ có thể mang giá trị **âm rất lớn**, dẫn đến việc thuật toán chèn liên tục các đỉnh xa xôi vào lộ trình thay vì gom cụm không gian hợp lý.

### 3.2. Quy chuẩn Chuẩn hóa Hiện tại
- **100% các testcase chuẩn trong [`testcases/benchmark/`](file:///z:/Desktop/250926/testcases/benchmark/)** đã được chuẩn hóa thông qua **Floyd-Warshall Metric Closure** trong [`utility/testcase_generator_v2.py`](file:///z:/Desktop/250926/utility/testcase_generator_v2.py).
- Kết quả kiểm định tự động khẳng định:
  - `t_viol = 0, c_viol = 0` trên toàn bộ 7 testcase benchmark.
  - Phù hợp hoàn toàn với dữ liệu thực tế: Trong mạng lưới giao thông đường bộ, ma trận $t(u, v)$ giữa các địa điểm chính là độ dài đường đi ngắn nhất (All-Pairs Shortest Path), vốn luôn thỏa mãn BĐT tam giác.

### 3.3. Cơ chế Nới lỏng Mở rộng trong Tương lai (Future Extension Mechanism)
Nếu trong các vòng đấu chính thức, ban tổ chức cung cấp các bộ testcase phi Metric cố tình vi phạm BĐT tam giác:
1. Solver sẽ kích hoạt một tầng tiền xử lý **Bao đóng Đường đi Ngắn nhất (All-Pairs Shortest Path Closure)** với độ phức tạp $\mathcal{O}(V^3)$ (tốn $< 0.05\text{s}$ cho $V = 500$ bằng bitset/mảng tối ưu) ngay khi nạp ma trận vào:
   $$t_{\text{eff}}(u, w) = \min_{v} \big(t(u, w), t(u, v) + t(v, w)\big)$$
   $$c_{\text{eff}}(u, w) = \min_{v} \big(c(u, w), c(u, v) + c(v, w)\big)$$
2. Thuật toán tìm kiếm sẽ chạy an toàn trên ma trận hiệu dụng $t_{\text{eff}}$ và $c_{\text{eff}}$ (đã được đảm bảo Metric $100\%$).
3. Khi xuất nghiệm cuối cùng, nếu cạnh $(u, w)$ thực chất là đi qua đường vòng $v$, solver chỉ cần giải nén đường đi (path reconstruction) để đưa ra đúng danh sách các đỉnh cần ghé.

---

## 4. KIẾN TRÚC TỔNG HỢP CHO BẢN CHỐT (`submissions/submission.cpp`)

Để đạt điểm số tối đa trong mọi kịch bản của kỳ thi SOICT 2026, phiên bản chốt sẽ không chạy một thuật toán mù quáng, mà vận hành theo kiến trúc **Bộ Giải Thích ứng theo Địa hình (Adaptive Portfolio / Ensemble Solver)**:

```
                            KIẾN TRÚC SUBMISSION ENSEMBLE SOLVER
                                             │
                                     [ĐỌC DỮ LIỆU INPUT]
                                             │
                                             ▼
                          [BỘ ĐO LƯỜNG ĐỊA HÌNH NGHIỆM (O(V))]
                          - Tính trung bình Slack: mean(L - E)
                          - Tính tỉ số Request / Xe: (N + M) / K
                          - Tính độ chặt Horizon: H / mean(t)
                                             │
             ┌───────────────────────────────┴───────────────────────────────┐
             │                                                               │
             ▼                                                               ▼
   ĐỊA HÌNH 1: CỬA SỔ HẸP                                          ĐỊA HÌNH 2: CỬA SỔ RỘNG
   (Slack <= 35 VÀ R/K <= 4.5)                                     (Slack > 35 HOẶC R/K > 4.5)
             │                                                               │
             ▼                                                               ▼
   [PHASE 1 DOMINANT SOLVER]                                       [HYBRID WARM-START + PHASE 2]
   1. Sinh cột DFS (Budget: 0.5s)                                  1. Chạy Phase 1 nhanh (0.3s)
   2. Tính Cận trên Lagrangian                                        hoặc Greedy Construction
   3. Set Packing SA (Budget: 1.5s)                                2. Bàn giao nghiệm hạt giống tốt
   4. CHỐT NGHIỆM TỐI ƯU TOÀN CỤC                                  3. Kích hoạt SISR + SA (Budget: 7.0s)
      (Gap ~ 0%, Tốc độ siêu tốc)                                     (Tháo gỡ chuỗi & tối ưu sâu)
                                                                   4. CHỐT NGHIỆM METAHEURISTIC
```

### 4.1. Tầng 1: Bộ Đo lường Địa hình Cấp tốc ($\mathcal{O}(V + K)$)
Ngay sau khi đọc dữ liệu vào các mảng phẳng liên tục:
```cpp
double avg_slack = 0;
for (int i = 0; i < N; i++) avg_slack += (passengers[i].L - passengers[i].E);
for (int j = 0; j < M; j++) avg_slack += (parcels[j].Lp - parcels[j].Ep);
avg_slack /= (N + M);

double req_per_taxi = (double)(N + M) / K;
bool is_tight_landscape = (avg_slack <= 35.0 && req_per_taxi <= 4.5);
```

### 4.2. Tầng 2: Bộ Điều phối Thuật toán Động (Dynamic Dispatcher)
1. **Nếu `is_tight_landscape == true` (Địa hình Pinpoint Windows)**:
   - Toàn bộ ngân sách thời gian được ưu tiên cho **Phase 1**:
     - DFS liệt kê toàn bộ cột khả thi trong giới hạn $0.5$ giây.
     - Lagrangian Relaxation tính Upper Bound đối ngẫu.
     - Simulated Annealing trên mô hình Set Packing tìm lời giải tối ưu.
   - Cam kết đạt nghiệm tối ưu hoặc sai số $< 0.1\%$ với tốc độ kỷ lục.
2. **Nếu `is_tight_landscape == false` (Địa hình Loose Sequencing)**:
   - Kích hoạt cơ chế **Lai tạo (Hybrid Warm-Start)**:
     - Dùng Phase 1 cắt tỉa gắt (hoặc Solomon Insertion Heuristic) trong $0.3$ giây để tạo ra một nghiệm khởi tạo vững chắc (thay vì khởi tạo rỗng hoặc ngẫu nhiên).
     - Bàn giao cấu trúc nghiệm cho **Phase 2 (SISR + SA)**: Dành trọn vẹn $85\% \sim 90\%$ ngân sách thời gian còn lại (ví dụ $7.5$ giây) để liên tục tháo gỡ chuỗi (String Removal), khai thác backward slack $\mathcal{O}(1)$, và tối ưu hóa doanh thu ròng.

---

## 5. BẢNG TỔNG KẾT & QUY TẮC VÀNG CHO CÁC NGUYÊN MẪU

| Địa hình Nghiệm | Thuật toán Tối ưu | Cơ chế Cốt lõi | Chỉ số KPI Quan trọng nhất | Lưu ý Kỹ thuật |
|---|---|---|---|---|
| **Pinpoint Metric** ($R/K \le 4$) | **Phase 1 (Set Packing)** | DFS Column Gen + Dual Bound + SA | Gap so với Cận trên ($\approx 0$) | Ngắt DFS nếu số cột vượt quá $100,000$ |
| **Loose Sequencing** ($R/K \ge 8$) | **Phase 2 (SISR + SA)** | String Removal + $O(1)$ Slack Check | Số iter/giây ($> 500k$) & Tốc độ hội tụ | Dùng Blink Heuristic ($1/64$) khi chèn |
| **Clustered Basins** | **Spatial SISR** | Phá hủy theo bán kính địa lý cụm | Đa dạng hóa láng giềng | Tránh để xe di chuyển chéo cụm vô ích |
| **Capacity Bottleneck** | **Knapsack SISR** | Sắp xếp $T_j / w_j$ + Ghép cặp bưu kiện | Tỷ lệ lấp đầy tải trọng ($Load / Q_k$) | Tận dụng khách làm cầu nối không tốn tải |
| **Asymmetric Flow** | **Directed Local Search** | Relocate, Directed 2-Opt / Or-Opt | Tính hợp lệ lịch trình $100\%$ | CẤM dùng toán tử đảo ngược chuỗi đoạn con |

> [!IMPORTANT]
> **Kết luận Triết lý**: Không có thuật toán nào là vô dụng; mỗi thuật toán là một vũ khí tối thượng trên đúng địa hình sở trường của nó. Sự kết hợp thông minh giữa khả năng tiệm cận tối ưu của Phase 1 trên địa hình hẹp và khả năng tìm kiếm toàn cục của Phase 2 trên địa hình rộng là chìa khóa chiến thắng tuyệt đối cho giải đấu SOICT 2026.

