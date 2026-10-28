# AGENT.MD: SOICT 2026 PROJECT CONSTITUTION & OPERATIONAL GUIDELINES

Tài liệu này là **Hiến pháp kỹ thuật (Project Constitution)** và **Chỉ dẫn tác vụ (Operational Guidelines)** bắt buộc cho mọi Agent và kỹ sư tham gia nghiên cứu, phát triển và tối ưu hóa hệ thống thuật toán cho bài toán **SOICT 2026: Integrated Passenger and Parcel Transportation**.

Mọi hành động sửa đổi mã nguồn, tổ chức thử nghiệm, phân tích dữ liệu hay nộp bài đều phải tuân thủ nghiêm ngặt các quy tắc trong tài liệu này.

---

## 1. TỔNG QUAN BÀI TOÁN & RÀNG BUỘC TOÁN HỌC CHUẨN

Tất cả các định nghĩa dưới đây được trích xuất nguyên bản và chuẩn hóa từ đề bài chính thức ([`Competition_SOICT_2026_HCM_v2026_08_23.pdf`](file:///z:/Desktop/250926/Competition_SOICT_2026_HCM_v2026_08_23.pdf)) và bản đặc tả ([`OFFICAL_PROBLEM_SOICT_2026.txt`](file:///z:/Desktop/250926/OFFICAL_PROBLEM_SOICT_2026.txt)).

### 1.1. Thực thể và Đồ thị Mạng lưới
- **Tổng số đỉnh trong đồ thị**: $V = 2N + 2M + K$. Các đỉnh đánh chỉ số $1$-based: $1, 2, \dots, V$.
- **Đội xe (Taxis)**: Gồm $K$ xe, đánh số $k \in \{1, \dots, K\}$.
  - Mỗi xe $k$ xuất phát từ điểm ban đầu $O_k$ tại thời điểm $0$ ($\text{dep}(O_k) = 0$).
  - Sức chứa hàng hóa (parcel capacity) của xe $k$ là $Q_k \ge 0$.
  - Xe **không bắt buộc phải quay về depot ban đầu** và có thể kết thúc hành trình tại bất kỳ đỉnh nào.
- **Ma trận Thời gian & Chi phí**:
  - $t(u, v) \ge 0$: Thời gian di chuyển từ đỉnh $u$ đến đỉnh $v$.
  - $c(u, v) \ge 0$: Chi phí xăng/vận hành khi đi từ $u$ đến $v$.
  - **Lưu ý quan trọng**: Ma trận $t$ và $c$ là đồ thị có hướng (directed), **không nhất thiết đối xứng** ($t(u, v) \ne t(v, u)$).

### 1.2. Ràng buộc Yêu cầu Hành khách (Passenger Requests - $N$)
Mỗi yêu cầu $i \in \{1, \dots, N\}$ được định nghĩa bởi $(P_i^n, D_i^n, E_i^n, L_i^n, S_i^n, T_i^n)$:
1. **Cửa sổ đón**: Xe đến $P_i^n$ lúc $\text{arr}(P_i^n)$ hợp lệ khi và chỉ khi:
   $$\text{arr}(P_i^n) \le L_i^n$$
   Thời điểm bắt đầu đón khách: $\text{srv}(P_i^n) = \max\big(\text{arr}(P_i^n), E_i^n\big)$.
   Thời điểm xe rời điểm đón: $\text{dep}(P_i^n) = \text{srv}(P_i^n) + S_i^n$.
2. **Ràng buộc Chuyến đi Thẳng Tuyệt đối (Direct Passenger Trip)**:
   - Xe chở khách $i$ **bắt buộc phải đi thẳng từ $P_i^n$ đến $D_i^n$**.
   - Trong khoảng thời gian này, xe **không được phép dừng hoặc phục vụ bất kỳ điểm đón/trả của hành khách hay bưu kiện nào khác**.
   - Mỗi thời điểm xe chở tối đa $1$ hành khách.
   $$\text{arr}(D_i^n) = \text{dep}(P_i^n) + t(P_i^n, D_i^n)$$
3. **Thời điểm rời điểm trả khách**: Dịch vụ trả khách bắt đầu ngay khi đến và kết thúc lúc:
   $$\text{dep}(D_i^n) = \text{arr}(D_i^n) + S_i^n$$
4. **Tải trọng**: Hành khách **không** chiếm tải trọng $Q_k$ của xe.

### 1.3. Ràng buộc Yêu cầu Bưu kiện (Parcel Requests - $M$)
Mỗi yêu cầu $j \in \{1, \dots, M\}$ được định nghĩa bởi $(P_j^h, D_j^h, w_j, E_j^{h,p}, L_j^{h,p}, E_j^{h,d}, L_j^{h,d}, S_j^h, T_j^h)$:
1. **Đón hàng**: $\text{arr}(P_j^h) \le L_j^{h,p}$.
   - $\text{srv}(P_j^h) = \max\big(\text{arr}(P_j^h), E_j^{h,p}\big)$.
   - $\text{dep}(P_j^h) = \text{srv}(P_j^h) + S_j^h$.
2. **Trả hàng**: Cùng một xe phải thực hiện cả đón và trả, và điểm đón phải xuất hiện trước điểm trả ($P_j^h \prec D_j^h$).
   - $\text{arr}(D_j^h) \le L_j^{h,d}$.
   - $\text{srv}(D_j^h) = \max\big(\text{arr}(D_j^h), E_j^{h,d}\big)$ (xe chờ nếu đến sớm hơn $E_j^{h,d}$).
   - $\text{dep}(D_j^h) = \text{srv}(D_j^h) + S_j^h$.
3. **Sức chứa xe**: Tại mọi thời điểm trên lộ trình xe $k$, tổng trọng lượng các kiện hàng đang trên xe không được vượt quá $Q_k$:
   $$\text{load}_k(t) \le Q_k, \quad \forall t$$

### 1.4. Hàm Mục tiêu (Objective Function)
Một yêu cầu được phục vụ trọn vẹn (không có phục vụ một phần) bởi tối đa $1$ xe. Không bắt buộc phải phục vụ hết tất cả các yêu cầu.
Mục tiêu là tìm lịch trình lộ trình $\mathcal{R} = \{R_1, \dots, R_K\}$ hợp lệ nhằm cực đại hóa **Lợi nhuận ròng (Benefit)**:

$$\text{Benefit} = \sum_{i \in \mathcal{S}_N} T_i^n + \sum_{j \in \mathcal{S}_M} T_j^h - \sum_{k=1}^K \sum_{r=1}^{m_k} c(v_{r-1}^k, v_r^k)$$

Trong đó $v_0^k = O_k$, và $m_k$ là số điểm phục vụ trên lộ trình xe $k$.

### 1.5. Định dạng Đầu ra (Output Format)
- **Dòng 1**: Giá trị `Benefit` (số nguyên hoặc `long long`).
- **$K$ dòng tiếp theo** (cho từng xe $k = 1 \dots K$ theo đúng thứ tự):
  - `m_k v_1^k v_2^k ... v_{m_k}^k`
  - Nếu xe $k$ không sử dụng, in ra số `0`.
  - **Lưu ý**: Tuyệt đối không in đỉnh xuất phát $O_k$ trong danh sách $v_r^k$.

### 1.6. Giả định Địa hình Nghiệm Chuẩn: Tính Thỏa mãn Bất đẳng thức Tam giác (Triangle Inequality)
1. **Ràng buộc Chuẩn hóa Nghiên cứu Hiện tại**:
   - Nhằm bảo toàn tính đơn điệu toán học của các kỹ thuật cắt tỉa lộ trình trong VRPTW và phản ánh đúng bản chất của mạng lưới giao thông thực tế (nơi đường nối trực tiếp đại diện cho đường đi ngắn nhất), **toàn bộ các testcase benchmark chuẩn trong nghiên cứu hiện tại đều được đảm bảo thỏa mãn 100% Bất đẳng thức Tam giác (Metric Property)**:
     $$t(u, w) \le t(u, v) + t(v, w), \quad \forall u, v, w \in \{1, \dots, V\}$$
     $$c(u, w) \le c(u, v) + c(v, w), \quad \forall u, v, w \in \{1, \dots, V\}$$
   - Mọi ma trận thời gian và chi phí sinh ra đều được chuẩn hóa thông qua **Floyd-Warshall Metric Closure** ([`utility/testcase_generator_v2.py`](file:///z:/Desktop/250926/utility/testcase_generator_v2.py)).
2. **Kế hoạch Mở rộng Tương lai (Future Extension Mechanism)**:
   - Nếu đề bài sau này mở rộng sang các trường hợp phi Metric vi phạm BĐT tam giác, ta có thể nới lỏng giả định này bằng cách bổ sung một tầng tiền xử lý All-Pairs Shortest Path $\mathcal{O}(V^3)$ ngay khi đọc dữ liệu đầu vào trong solver, ánh xạ đồ thị ban đầu về metric closure tương đương. Chi tiết về cơ chế này và bản đồ địa hình nghiệm được quy định tại [`ALGORITHM_LANDSCAPE_MAPPING.md`](file:///z:/Desktop/250926/ALGORITHM_LANDSCAPE_MAPPING.md).

---

## 2. KIẾN TRÚC MÃ NGUỒN & PHÂN ĐỊNH TRÁCH NHIỆM

```
250926/
├── AGENT.md                       # Bản quy ước và chỉ dẫn nền tảng này
├── ALGORITHM_LANDSCAPE_MAPPING.md # Bản đồ địa hình nghiệm & chiến lược phân bổ solver tối ưu
├── OPTIMIZATION_TECHNIQUES.md     # Tài liệu tổng hợp các kỹ thuật tối ưu hóa dùng chung
├── ALGORITHM_SPECIFICATION.md     # Đặc tả thiết kế thuật toán
├── CODE_SPECIFICATION.md          # Đặc tả quy chuẩn code C++
├── Competition_SOICT_...pdf       # Đề bài chính thức
├── OFFICAL_PROBLEM_SOICT_...txt   # Template format I/O chính thức
├── submissions/                   # THƯ MỤC CHỨA CÁC BẢN CHỐT NỘP BÀI
│   └── submission.cpp             # Mã nguồn nộp bài chính thức (Clean, No debug stdout)
├── experiments/                   # THƯ MỤC CHỨA CÁC NGUYÊN MẪU THỬ NGHIỆM
│   ├── <prototype_name>/          # Mỗi nguyên mẫu có 1 thư mục riêng biệt
│   │   ├── <prototype_name>.cpp   # Mã nguồn solver của nguyên mẫu
│   │   ├── plot_<prototype>.py    # Script vẽ đồ thị & minh họa CHUYÊN BIỆT cho nguyên mẫu
│   │   └── output/                # Thư mục chứa các phiên chạy
│   │       └── run_YYYYMMDD_HHMMSS/
│   │           ├── result.txt     # Output chuẩn đề bài (cho validator)
│   │           ├── log.txt        # Toàn văn log chi tiết
│   │           ├── report.txt     # Báo cáo các chỉ số KPIs
│   │           └── plots/         # Các biểu đồ thông số chuyên biệt (.png)
├── utility/                       # THƯ MỤC CHỨA SCRIPTS TIỆN ÍCH & CÔNG CỤ DÙNG CHUNG
│   ├── validator.py               # Bộ kiểm tra tính hợp lệ tối cao (Ground Truth)
│   ├── run.py                     # Bộ điều phối thực thi & đo lường benchmark tự động
│   └── testcase_generator.py      # Bộ sinh testcase
├── reports/                       # Thư mục lưu các báo cáo chuyên sâu (.tex & .pdf)
└── testcases/                     # Bộ testcases chuẩn
```

### Phân công vai trò:
1. **[`submissions/`](file:///z:/Desktop/250926/submissions)**:
   - Chứa các phiên bản chốt dùng để nộp bài trên hệ thống thi chính thức.
   - Không chứa bất kỳ câu lệnh in debug nào ra stdout (chỉ in đúng format nghiệm ra stdout, mọi log phụ trợ nếu có phải xuất ra stderr).
   - Tối ưu hóa tối đa thời gian biên dịch và thực thi (`#pragma GCC optimize("O3,unroll-loops")`).
2. **[`experiments/`](file:///z:/Desktop/250926/experiments)**:
   - Chứa toàn bộ các nguyên mẫu thuật toán nghiên cứu (ví dụ: `experiments/reference_solve/`, `experiments/tabu_prototype/`, ...).
   - **Mỗi nguyên mẫu bắt buộc phải là một thư mục độc lập**, tự quản lý mã nguồn, script trực quan hóa và thư mục `output/` riêng biệt.
3. **[`utility/`](file:///z:/Desktop/250926/utility)**:
   - Chứa các công cụ dùng chung toàn hệ thống: `validator.py`, `testcase_generator.py`, `run.py`.
   - [`validator.py`](file:///z:/Desktop/250926/utility/validator.py) là trọng tài độc lập tối cao kiểm định tính đúng đắn.

---

## 3. LUẬT "LOG, LOG NỮA, LOG MÃI" & TRỰC QUAN HÓA CHUYÊN BIỆT

Tối ưu hóa giải thuật Heuristic và Metaheuristic là khoa học thực nghiệm. Nghiêm cấm mọi hành vi phán đoán bằng cảm giác hoặc chỉ nhìn vào điểm số cuối cùng.

### 3.1. Triết lý "Log, log nữa, log mãi" (Continuous Deep Logging)
1. **Ghi nhận không tiếc dung lượng**:
   - Mọi nguyên mẫu trong `experiments/` khi chạy phải ghi nhận liên tục tiến trình tìm kiếm ra `log.txt` (hoặc `metrics.csv`).
   - Các trường thông tin bắt buộc phải ghi lại qua từng chu kỳ/thế hệ:
     - Số thứ tự vòng lặp (Iteration / Step).
     - Thời gian trôi qua thực tế (Elapsed wall-clock time).
     - Loại toán tử được gọi (Move/Operator: Ruin type, Recreate mode, Relocate, Swap, 2-opt, ...).
     - Biến động mục tiêu ($\Delta \text{Objective}$, Current Objective, Global Best Objective).
     - Tham số điều khiển nội tại (Nhiệt độ hiện tại $T$, Số xe bị ảnh hưởng, Chiều dài chuỗi bị tháo gỡ).
     - Trạng thái chấp nhận: Accepted (Cải thiện), Accepted (Xác suất SA), hoặc Rejected.
     - Tỷ lệ Cache Hit / Cache Miss của bộ nhớ đệm (nếu có).

### 3.2. Cấu trúc Phiên chạy Khép kín (`output/run_YYYYMMDD_HHMMSS/`)
Mỗi lần thực thi một nguyên mẫu thuật toán:
1. **Tự động tạo một thư mục con mới** theo dấu thời gian:
   `experiments/<prototype_name>/output/run_YYYYMMDD_HHMMSS/`
2. **Nghiêm cấm ghi đè**: Mỗi lần chạy là một phiên bản dữ liệu bất biến, không bao giờ ghi đè lên kết quả của lần chạy trước.
3. **Bộ tứ Bắt buộc (The Essential 4 Output Artifacts)**: Sau khi chạy xong, thư mục phiên chạy **bắt buộc phải có đủ 4 thành phần**:
   - `result.txt`: Nghiệm chuẩn theo đúng format đề bài (phục vụ kiểm tra qua `validator.py`).
   - `log.txt` (hoặc `metrics.csv`): Toàn văn log chi tiết từng bước phục vụ mổ xẻ dữ liệu.
   - `report.txt`: Bản báo cáo tổng kết ngắn gọn các chỉ số hiệu năng (Initial Obj, Best Obj, Total Iterations, Số bước cải thiện, Tỷ lệ chấp nhận, Thời gian chạy, Tốc độ iter/giây).
   - `plots/`: Thư mục chứa các biểu đồ trực quan hóa được xuất ra tự động dạng hình ảnh PNG/PDF chất lượng cao ($300$ DPI).

### 3.3. Luật Trực quan hóa Chuyên biệt (Specialized Plotting Mandate)
1. **Nghiêm cấm dùng chung một file plot**:
   - Mỗi thuật toán có cơ chế hoạt động hoàn toàn khác biệt:
     - *Simulated Annealing*: Cần đồ thị Động thái hạ nhiệt ($T(t)$), Tỷ lệ chấp nhận bước lùi theo thời gian, Biểu đồ tần suất thoát cực tiểu địa phương.
     - *Tabu Search*: Cần đồ thị Danh sách cấm (Tabu Tenure), Tần suất viếng thăm đỉnh (Frequency memory), Phân bố cải thiện theo láng giềng.
     - *ALNS / Ruin & Recreate*: Cần đồ thị Tiến hóa trọng số toán tử (Operator weights evolution), Điểm đóng góp của từng phép Ruin/Recreate.
     - *Set Packing Phase 1*: Cần đồ thị Hội tụ cận trên Lagrangian (UB vs LB Gap), Số lượng cột sinh ra qua các độ sâu DFS.
2. **Bắt buộc viết script Python minh họa riêng cho từng nguyên mẫu**:
   - Mỗi thư mục nguyên mẫu `experiments/<prototype_name>/` **bắt buộc phải có file script trực quan hóa riêng**: `plot_<prototype_name>.py`.
   - Script này nhận đường dẫn thư mục phiên chạy `run_.../`, đọc `log.txt` / `metrics.csv` và tự động kết xuất toàn bộ biểu đồ vào thư mục con `plots/`.

### 3.4. Nghiêm cấm Tuyệt đối Lưu Trữ trong Thư mục Hệ thống / Brain (Workspace Direct Visibility Mandate)
1. **Quyền truy cập trực tiếp của người dùng là tối thượng**:
   - Mọi kết quả thử nghiệm, log, báo cáo, và đồ thị hình ảnh **BẮT BUỘC phải được lưu trữ trực tiếp trong Workspace dự án** tại thư mục nguyên mẫu tương ứng:
     `experiments/<prototype_name>/output/run_YYYYMMDD_HHMMSS/`
   - **TUYỆT ĐỐI NGHIÊM CẤM** việc lưu trữ hoặc chỉ trỏ người dùng vào thư mục ẩn của hệ thống/AI (`.gemini/antigravity/brain/...`). Người dùng hoàn toàn không thể xem hoặc đọc dữ liệu trong thư mục này, và hành vi này bị coi là vi phạm nghiêm trọng quy chuẩn vận hành dự án.

### 3.5. Quy Chuẩn Đầy Đủ 100% Cả 7 Testcase Benchmark (No Missing Benchmark Runs Policy)
1. **Không bỏ sót bất kỳ benchmark nào**:
   - Khi tiến hành đánh giá benchmark cho một nguyên mẫu (dù là 2.5s hay 5-10s), **BẮT BUỘC phải chạy đầy đủ 100% cả 7 testcase chuẩn**:
     1. `bench_01_tight_assignment.txt`
     2. `bench_02_loose_sequencing.txt`
     3. `bench_03_clustered_basins.txt`
     4. `bench_04_capacity_bottleneck.txt`
     5. `bench_05_asymmetric_random.txt`
     6. `bench_06_asymmetric_metric.txt`
     7. `bench_07_dense_scale_500.txt`
2. **Tự động sinh đủ Bộ Tứ (The Essential 4) cho từng testcase**:
   - Mỗi testcase chạy xong **phải lập tức có thư mục phiên chạy riêng** chứa đầy đủ:
     - `result.txt` (nghiệm chuẩn)
     - `log.txt` (tiến trình chi tiết)
     - `report.txt` (chỉ số KPIs)
     - `plots/convergence.png` (đồ thị hội tụ)
   - Mọi script benchmark (`run_...benchmark.py`) bắt buộc phải tự động gọi `plot_<prototype>.py` ngay sau khi solver C++ kết thúc để đảm bảo thư mục không bao giờ thiếu plot. Không cho phép có bất kỳ thư mục run nào tồn tại mà thiếu `plots/convergence.png`.
3. **Đồng nhất đường dẫn, cấm phân mảnh thư mục**:
   - Mỗi nguyên mẫu chỉ có DUY NHẤT một thư mục hoạt động chuẩn xác trong `experiments/`. Mã nguồn C++, script chạy và script plot phải đồng bộ 100% đường dẫn ghi ra thư mục `output/` của chính nó, không được tạo song song hai thư mục trùng lặp gây thất lạc dữ liệu kiểm thử.

---

## 4. NGUYÊN TẮC KỸ THUẬT NỀN TẢNG (FOUNDATIONAL PERFORMANCE RULES)

Để đảm bảo hiệu năng và tính ổn định trên mọi solver:

1. **Bộ nhớ liên tục & Mảng phẳng (Contiguous Flat Buffers)**:
   - Sử dụng mảng 1 chiều phẳng cho ma trận thời gian và chi phí: `Tm[u * SV + v]` và `Cm[u * SV + v]`.
   - Tránh cấu trúc con trỏ nhiều cấp hoặc `vector<vector<T>>` ở các tầng dữ liệu lặp sâu.
2. **Cấm cấp phát động trong Vòng lặp Nóng (Zero Dynamic Allocations in Hot Loops)**:
   - Nghiêm cấm `new`, `malloc`, `vector.resize()`, `vector.push_back()` tái cấp phát bộ nhớ bên trong các hàm tìm kiếm lặp hàng triệu lần.
   - Cấp phát sẵn bộ đệm tĩnh cố định theo kích thước tối đa của bài toán ngay từ khi khởi tạo `main`.
3. **Kiểm soát Giới hạn Thời gian Thực (Wall-clock Time Control)**:
   - Sử dụng `std::chrono::steady_clock` để kiểm soát giới hạn thời gian thực (wall-clock budget) thay vì đếm số vòng lặp cố định, đảm bảo chạy an toàn và tối đa hóa thời gian trên mọi môi trường chấm thi.
4. **Áp dụng Kỹ thuật Tối ưu Dùng chung**:
   - Mọi kỹ thuật tối ưu hóa chuyên sâu (mảng backward slack $\mathcal{O}(1)$, bộ nhớ đệm phiên bản `icache` + `rver`, bộ sinh số ngẫu nhiên bitwise, blink heuristic, v.v.) phải tham chiếu và tuân thủ theo tài liệu [`OPTIMIZATION_TECHNIQUES.md`](file:///z:/Desktop/250926/OPTIMIZATION_TECHNIQUES.md).

---

## 5. QUY TRÌNH KIỂM CHỨNG & CHUẨN BENCHMARK (ANTI-LUCK PROTOCOL)

Một sai lầm nguy hiểm trong tối ưu Heuristic/Metaheuristic là **"ngộ nhận do ăn may" (Single-seed bias)**. Để loại bỏ hoàn toàn yếu tố này, mọi thử nghiệm phải tuân thủ:

### 5.1. Quy trình Kiểm thử Hợp lệ Bắt buộc
Mọi phiên bản code C++ mới sau khi sửa đổi **phải vượt qua 2 bước**:
1. **Biên dịch không cảnh báo**:
   ```bash
   g++ -O3 -std=c++20 <source.cpp> -o <source.exe>
   ```
2. **Chạy qua Validator**:
   ```bash
   ./<source.exe> < testcases/test_X.txt > out.txt
   python utility/validator.py testcases/test_X.txt out.txt
   ```
   **Điều kiện Đạt**: Log trả về `🎉 KẾT LUẬN: ĐIỂM SỐ KHỚP HOÀN TOÀN! CHUẨN XÁC!`. Nếu có cảnh báo lệch điểm hoặc vi phạm bất kỳ ràng buộc nào, giải pháp bị từ chối ngay lập tức.

### 5.2. Giao thức Đo lường Robust Đa hạt giống (Multi-Seed Protocol)
Khi đánh giá hiệu quả của một cải tiến thuật toán:
1. **Tối thiểu 5 - 10 hạt giống (Seeds)** cho mỗi testcase.
2. **Báo cáo đầy đủ 4 chỉ số thống kê**:
   - $\text{Min}$ (Đáy xấu nhất): Thể hiện độ an toàn trong kịch bản xấu nhất.
   - $\text{Mean}$ (Trung bình): Thể hiện sức mạnh tổng quát.
   - $\text{Max}$ (Kỷ lục): Đỉnh cao thuật toán chạm tới.
   - $\text{StdDev}$ (Độ lệch chuẩn): Đo lường mức độ ổn định. $\text{StdDev}$ càng nhỏ, thuật toán càng robust.
3. Không chấp nhận một thuật toán có $\text{Max}$ cao nhưng $\text{StdDev}$ lớn và $\text{Min}$ tụt dốc.

---

## 6. QUY TẮC BẢO TOÀN TÀI LIỆU & LỊCH SỬ DỰ ÁN

1. Không xóa các file tài liệu nền tảng (`OFFICAL_PROBLEM_SOICT_2026.txt`, `Competition_SOICT_...pdf`).
2. Mọi báo cáo toán học chuyên sâu phải được viết bằng LaTeX và biên dịch sang PDF trong thư mục [`reports/`](file:///z:/Desktop/250926/reports).
3. Luôn giữ git commit sạch sẽ, có thông điệp mô tả rõ ràng mục tiêu và kết quả định lượng.
4. Khi giao tiếp với người dùng, đưa ra các đường dẫn file nhấp được dạng markdown (`file:///...`) và giữ phong cách súc tích, khách quan, dựa trên dữ liệu thực nghiệm.
