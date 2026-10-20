# AGENT.MD: SOICT 2026 PROJECT CONSTITUTION & OPERATIONAL GUIDELINES

Tài liệu này là **Hiến pháp kỹ thuật (Project Constitution)** và **Chỉ dẫn tác vụ (Operational Guidelines)** bắt buộc cho mọi Agent và kỹ sư tham gia nghiên cứu, phát triển và tối ưu hóa hệ thống thuật toán cho bài toán **SOICT 2026: Integrated Passenger and Parcel Transportation**.

Mọi hành động sửa đổi mã nguồn, sinh testcase, đo lường benchmark hay nộp bài đều phải tuân thủ nghiêm ngặt các quy tắc nền tảng trong tài liệu này.

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

---

## 2. KIẾN TRÚC MÃ NGUỒN & PHÂN ĐỊNH TRÁCH NHIỆM

```
250926/
├── AGENT.md                       # Bản quy ước và chỉ dẫn nền tảng này
├── OPTIMIZATION_TECHNIQUES.md     # Tài liệu tổng hợp các kỹ thuật tối ưu hóa dùng chung
├── ALGORITHM_SPECIFICATION.md     # Đặc tả thiết kế thuật toán
├── CODE_SPECIFICATION.md          # Đặc tả quy chuẩn code C++
├── Competition_SOICT_...pdf       # Đề bài chính thức
├── OFFICAL_PROBLEM_SOICT_...txt   # Template format I/O chính thức
├── submissions/                   # THƯ MỤC CHỨA CÁC BẢN CHỐT NỘP BÀI
│   └── submission.cpp             # Mã nguồn nộp bài chính thức (Clean, No debug stdout)
├── experiments/                   # THƯ MỤC CHỨA CÁC NGUYÊN MẪU THỬ NGHIỆM
│   ├── reference_solve.cpp        # Thuật toán tham chiếu (Phase 1 SP + Phase 2 SISR/SA)
│   ├── tabu_prototype.cpp         # Nguyên mẫu Tabu Search đối chứng
│   ├── milp_scip_solver.py        # Solver đối chứng chính xác MILP SCIP
│   └── best_tour_test_500.txt     # Log lộ trình mẫu tốt nhất
├── utility/                       # THƯ MỤC CHỨA SCRIPTS TIỆN ÍCH & CÔNG CỤ
│   ├── validator.py               # Bộ kiểm tra tính hợp lệ tối cao (Ground Truth)
│   ├── run.py                     # Bộ điều phối thực thi & đo lường benchmark tự động
│   ├── testcase_generator.py      # Bộ sinh testcase ngẫu nhiên
│   └── plot_tabu.py               # Công cụ trực quan hóa hội tụ và lộ trình
├── reports/                       # Thư mục lưu các báo cáo chuyên sâu (.tex & .pdf)
└── testcases/                     # Bộ testcases chuẩn
```

### Phân công vai trò:
1. **[`submissions/`](file:///z:/Desktop/250926/submissions)**:
   - Chứa file nộp bài chính thức.
   - Không chứa bất kỳ câu lệnh in debug nào ra stdout (chỉ in đúng định dạng bài toán ra stdout, log phụ trợ xuất ra stderr).
   - Tối ưu hóa tối đa thời gian biên dịch và thực thi (`#pragma GCC optimize("O3,unroll-loops")`).
2. **[`experiments/`](file:///z:/Desktop/250926/experiments)**:
   - Chứa các solver nghiên cứu, nguyên mẫu thuật toán mới, so sánh đối chứng.
   - Cho phép xuất log chi tiết ra stderr để phục vụ đo đạc và phân tích.
3. **[`utility/`](file:///z:/Desktop/250926/utility)**:
   - Chứa toàn bộ scripts hỗ trợ: validator, generator, runner, plotter.
   - [`validator.py`](file:///z:/Desktop/250926/utility/validator.py) là trọng tài độc lập tối cao kiểm định tính đúng đắn.

---

## 3. NGUYÊN TẮC KỸ THUẬT NỀN TẢNG (FOUNDATIONAL PERFORMANCE RULES)

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
   - Khi cài đặt các thuật toán tìm kiếm cục bộ hoặc metaheuristics, các kỹ thuật tối ưu hóa chuyên sâu (như mảng backward slack, bộ đệm phiên bản `icache`, bộ sinh số ngẫu nhiên bitwise, blink heuristic, v.v.) phải tham chiếu và tuân thủ theo tài liệu [`OPTIMIZATION_TECHNIQUES.md`](file:///z:/Desktop/250926/OPTIMIZATION_TECHNIQUES.md).

---

## 4. QUY TRÌNH KIỂM CHỨNG & CHUẨN BENCHMARK (ANTI-LUCK PROTOCOL)

Một sai lầm nguy hiểm trong tối ưu Heuristic/Metaheuristic là **"ngộ nhận do ăn may" (Single-seed bias)**. Để loại bỏ hoàn toàn yếu tố này, mọi thử nghiệm phải tuân thủ:

### 4.1. Quy trình Kiểm thử Hợp lệ Bắt buộc
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

### 4.2. Giao thức Đo lường Robust Đa hạt giống (Multi-Seed Protocol)
Khi đánh giá hiệu quả của một cải tiến thuật toán:
1. **Tối thiểu 5 - 10 hạt giống (Seeds)** cho mỗi testcase.
2. **Báo cáo đầy đủ 4 chỉ số thống kê**:
   - $\text{Min}$ (Đáy xấu nhất): Thể hiện độ an toàn trong kịch bản xấu nhất.
   - $\text{Mean}$ (Trung bình): Thể hiện sức mạnh tổng quát.
   - $\text{Max}$ (Kỷ lục): Đỉnh cao thuật toán chạm tới.
   - $\text{StdDev}$ (Độ lệch chuẩn): Đo lường mức độ ổn định. $\text{StdDev}$ càng nhỏ, thuật toán càng robust.
3. Không chấp nhận một thuật toán có $\text{Max}$ cao nhưng $\text{StdDev}$ lớn và $\text{Min}$ tụt dốc.

---

## 5. QUY TẮC BẢO TOÀN TÀI LIỆU & LỊCH SỬ DỰ ÁN

1. Không xóa các file tài liệu nền tảng (`OFFICAL_PROBLEM_SOICT_2026.txt`, `Competition_SOICT_...pdf`).
2. Mọi báo cáo toán học chuyên sâu phải được viết bằng LaTeX và biên dịch sang PDF trong thư mục [`reports/`](file:///z:/Desktop/250926/reports).
3. Luôn giữ git commit sạch sẽ, có thông điệp mô tả rõ ràng mục tiêu và kết quả định lượng.
4. Khi giao tiếp với người dùng, đưa ra các đường dẫn file nhấp được dạng markdown (`file:///...`) và giữ phong cách súc tích, khách quan, dựa trên dữ liệu thực nghiệm.
