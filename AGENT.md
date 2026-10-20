# AGENT.MD: SOICT 2026 PROJECT CONSTITUTION & OPERATIONAL GUIDELINES

Tài liệu này là **Hiến pháp kỹ thuật (Project Constitution)** và **Chỉ dẫn tác vụ (Operational Guidelines)** bắt buộc cho mọi Agent và kỹ sư tham gia nghiên cứu, phát triển và tối ưu hóa hệ thống thuật toán cho bài toán **SOICT 2026: Integrated Passenger and Parcel Transportation**.

Mọi hành động sửa đổi mã nguồn, sinh testcase, đo lường benchmark hay nộp bài đều phải tuân thủ nghiêm ngặt các điều khoản trong tài liệu này.

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
├── AGENT.md                       # Bản quy ước và chỉ dẫn này
├── ALGORITHM_SPECIFICATION.md     # Tài liệu thiết kế thuật toán
├── CODE_SPECIFICATION.md          # Tài liệu quy chuẩn code C++
├── Competition_SOICT_...pdf       # Đề bài chính thức
├── OFFICAL_PROBLEM_SOICT_...txt   # Template format I/O chính thức
├── validator.py                   # Bộ kiểm tra tính hợp lệ tối cao (Ground Truth)
├── run.py                         # Tool runner tự động hóa benchmark
├── testcase_generator.py          # Generator cũ (3 đỉnh thời gian hẹp)
├── testcase_generator_v2.py       # Generator mới (Stress test, đa dạng topology)
├── submission.cpp                 # Mã nguồn nộp bài chính thức (Clean, No debug stdout)
├── reference_solve.cpp            # Mã nguồn nghiên cứu (Phase 1 SP + Phase 2 SISR/SA)
├── tabu_prototype.cpp             # Mã nguồn tham khảo / đối chứng
├── reports/                       # Thư mục lưu các báo cáo chi tiết (.tex & .pdf)
└── testcases/                     # Bộ testcases chuẩn
```

### Phân công vai trò file:
1. **[`submission.cpp`](file:///z:/Desktop/250926/submission.cpp)**:
   - File duy nhất dùng để nộp bài trên hệ thống chấm tự động của cuộc thi.
   - Không chứa bất kỳ câu lệnh `cout`/`printf` gỡ lỗi nào ngoài định dạng chuẩn của đề bài.
   - Tối ưu hóa tối đa thời gian biên dịch và thực thi (`#pragma GCC optimize("O3,unroll-loops")`).
2. **[`reference_solve.cpp`](file:///z:/Desktop/250926/reference_solve.cpp)**:
   - Phòng thí nghiệm thuật toán. Mọi log chi tiết, thống kê số vòng lặp, thời gian chạy từng phase, Lagrangian Upper Bound được xuất ra `cerr`.
   - Nơi thử nghiệm các toán tử Ruin/Recreate mới, cơ chế Fallback và các cải tiến Phase 2 trước khi chuyển giao vào `submission.cpp`.
3. **[`validator.py`](file:///z:/Desktop/250926/validator.py)**:
   - Trọng tài độc lập tối cao. Mọi giải pháp C++ đều vô nghĩa nếu không vượt qua được `validator.py`.

---

## 3. NGUYÊN TẮC HIỆU NĂNG C++ (C++ PERFORMANCE CONSTITUTION)

Để đạt tốc độ hàng triệu phép thử/giây, toàn bộ mã C++ phải tuân thủ nghiêm ngặt:

1. **Bộ nhớ liên tục & Mảng phẳng (Contiguous Flat Buffers)**:
   - Sử dụng mảng 1 chiều phẳng cho ma trận thời gian và chi phí: `Tm[u * SV + v]` và `Cm[u * SV + v]`.
   - Tránh cấu trúc `vector<vector<T>>` ở các tầng dữ liệu lặp sâu.
2. **Cấm cấp phát động trong Vòng lặp Tìm kiếm (Zero Allocations in Hot Loops)**:
   - Nghiêm cấm `new`, `malloc`, `vector.resize()`, `vector.push_back()` tái cấp phát bộ nhớ bên trong các hàm `recreate`, `ruin`, `best_insert`.
   - Cấp phát sẵn bộ đệm tĩnh (`cand`, `removed`, `touched`, `backup`) từ lúc khởi tạo `main`.
3. **Kiểm tra Tính khả thi Hậu tố trong $O(1)$**:
   - Mọi thao tác chèn phải dựa vào mảng `lat[1..n+1]` (Backward Latest Arrival Slack) và cờ đồng bộ `sync` để kiểm tra toàn bộ nhánh đuôi trong $\mathcal{O}(1)$.
4. **Bộ nhớ đệm Phiên bản Tuyệt đối (`icache` + `rver`)**:
   - Mọi ứng viên chèn trên xe $k$ phải kiểm tra qua `icache[q * (K + 1) + k]`. Chỉ tính lại khi `rver[k]` thay đổi.
5. **Đồng hồ thực Wall-clock Deadline**:
   - Sử dụng `std::chrono::steady_clock` để kiểm soát giới hạn thời gian chính xác, kiểm tra chu kỳ theo bitwise (ví dụ `(it & 15) == 0` hoặc `(it & 255) == 0`).

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
   python validator.py testcases/test_X.txt out.txt
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

## 5. CHỈ DẪN NGHIÊN CỨU & PHÁT TRIỂN TIẾP THEO

### 5.1. Bộ sinh Testcase Mới (`testcase_generator_v2.py`)
Cần thiết kế bộ sinh test có khả năng kích hoạt các chế độ thử thách:
- **Dạng 1: Wide-Window Stress Test**: Cửa sổ thời gian rộng, giãn cách đều, ép độ sâu lộ trình lên $8 \sim 15$ requests/xe để đẩy Phase 1 vào trạng thái `enum_abort`.
- **Dạng 2: Heavy Parcel Interleaving**: Tỷ lệ bưu kiện $> 70\%$, kiểm tra khả năng sắp xếp xen kẽ điểm đón/trả và giới hạn tải trọng $Q_k$.
- **Dạng 3: Metric Euclidean Clusters**: Tọa độ địa lý theo cụm (downtown vs suburbs), thời gian và chi phí thỏa mãn bất đẳng thức tam giác.
- **Dạng 4: Extreme Scale ($R \ge 500, K \ge 30$)**: Kiểm tra giới hạn bộ nhớ và tốc độ xử lý của Phase 2.

### 5.2. Hướng Tối ưu Hóa Phase 2
Khi Phase 1 bị hủy do quá tải tổ hợp, Phase 2 phải là "cỗ xe bọc thép" gánh toàn bộ bài toán:
1. **Toán tử Ruin Đa dạng**:
   - *SISR (String Removal)*: Tháo chuỗi láng giềng liên kết.
   - *Time-slice Ruin*: Tháo toàn bộ các request rơi vào một khoảng thời gian $[t_1, t_2]$ đang bị xung đột lịch trình.
   - *Worst-Cost Removal*: Tính chi phí biên đóng góp của từng request; loại bỏ các request khiến xe phải chạy vòng quá xa.
   - *Shaw Removal*: Tính độ tương đồng kết hợp: Khoảng cách không gian + Chênh lệch time window + Tải trọng.
2. **Toán tử Recreate Thông minh**:
   - *Regret-2 / Regret-3*: Lưu trữ chi phí tốt thứ nhất $c_1$ và tốt thứ hai $c_2$ thông qua `icache`. Ưu tiên chèn các request có $c_2 - c_1$ lớn để tránh cận thị.
   - *Blink Heuristic*: Giữ xác suất ngẫu nhiên thông minh $p = 1/64$ để phân nhánh thoát cực tiểu địa phương.
3. **Cơ chế Tận dụng Cột từ Phase 1 (Column Harvesting)**:
   - Khi Phase 1 chạm trần `enum_abort`, không xóa sạch các cột đã tìm được. Lọc ra các cột có lợi nhuận cao nhất để làm **Nghiệm khởi tạo (Warm-Start)** cho Phase 2 thay vì chạy từ khởi tạo tham lam đơn giản.
4. **Intra-route Local Search**:
   - Tối ưu hóa cục bộ sau khi chèn: dùng 2-opt siêu nhanh để gỡ các nút thắt đan chéo trên từng xe riêng biệt.

---

## 6. QUY TẮC BẢO TOÀN TÀI LIỆU & LỊCH SỬ DỰ ÁN

1. Không xóa các file tài liệu nền tảng (`OFFICAL_PROBLEM_SOICT_2026.txt`, `Competition_SOICT_...pdf`).
2. Mọi báo cáo toán học chuyên sâu phải được viết bằng LaTeX và biên dịch sang PDF trong thư mục [`reports/`](file:///z:/Desktop/250926/reports).
3. Luôn giữ git commit sạch sẽ, có thông điệp mô tả rõ ràng mục tiêu và kết quả định lượng.
4. Khi giao tiếp với người dùng, đưa ra các đường dẫn file nhấp được dạng markdown (`file:///...`) và giữ phong cách súc tích, khách quan, dựa trên dữ liệu thực nghiệm.