# SYSTEM SPECIFICATION: KIẾN TRÚC MATHEURISTIC 2 PHA (MULTI-WAVE KNOWLEDGE-GUIDED COLUMN GENERATION & LAGRANGIAN SET PACKING) CHO BÀI TOÁN SOICT 2026 VRPSP

> **Chỉ thị dành cho AI Cài đặt (Implementation Agent Directive):**
> Tài liệu này chứa toàn bộ khung xương toán học, cấu trúc dữ liệu, độ phức tạp tính toán và mã giả cấp hệ thống. Khi cài đặt bằng C++, bắt buộc tuân thủ các quy tắc:
>
> 1. Sử dụng `#pragma GCC optimize("O3,unroll-loops")` và cấp phát mảng phẳng (flat 1D vectors) cho các ma trận 2D để tối ưu hóa L1/L2 Cache.
>
> 2. Mọi phép kiểm tra tính khả thi trên tuyến phải thực hiện trong $O(1)$ hoặc $O(\vert{}\mathcal{M}_{\text{open}}\vert{})$ thông qua mảng tiền xử lý/trạng thái.
>
> 3. Không sử dụng hằng số nhiệt độ hoặc trọng số cứng phụ thuộc quy mô dữ liệu; toàn bộ phải chuẩn hóa theo thang đo nội tại $S_{\text{scale}}$ của testcase.

## 1. MÔ HÌNH HÓA ĐỒ THỊ SỰ KIỆN & RÀNG BUỘC CƠ SỞ (EVENT-NODE GRAPH)

### 1.1. Tham số đầu vào và Không gian Đỉnh Sự Kiện

* Đội xe gồm $K$ chiếc ($k \in \{1, \dots, K\}$), mỗi xe xuất phát từ Depot $O_k \in \{1, \dots, V\}$ tại thời điểm $t = 0$ với tải trọng tối đa $Q_k$.

* Tập yêu cầu $\mathcal{R} = \{1, \dots, R\}$ với $R = N + M$, bao gồm:

  * **Hành khách (**$q \in \{1, \dots, N\}$**):** Điểm đón $P_q$, điểm trả $D_q$, cửa sổ thời gian tại điểm đón $[e_q, l_q]$, thời gian phục vụ $Sv_q$, doanh thu $Rev_q$. Bắt buộc đi thẳng $P_q \to D_q$.

  * **Bưu kiện (**$q \in \{N+1, \dots, R\}$**):** Điểm nhận $P_q$ (cửa sổ $[e_q, l_q]$), điểm giao $D_q$ (cửa sổ $[ed_q, lde_q]$), khối lượng $nw_q$, thời gian phục vụ tại mỗi đầu $Sv_q$, doanh thu $Rev_q$. Cho phép ghép chuyến xen kẽ.

* Ma trận thời gian di chuyển $T(u, v)$ và chi phí di chuyển $C(u, v)$ kích thước $(V+1) \times (V+1)$.

Để thống nhất cả hành khách (nguyên khối) và bưu kiện (tách rời nhận-giao) vào cùng một máy trạng thái duyệt tuyến, ta định nghĩa **Tập Đỉnh Sự Kiện** $\mathcal{V}_{\text{evt}} = \{1, \dots, N + 2M\}$ (kích thước $E = R + M$) như sau:

| Loại sự kiện | Chỉ số sự kiện $u \in \mathcal{V}_{\text{evt}}$ | Mã đơn $\text{req}(u)$ | Đỉnh vào $\text{nin}[u]$ | Đỉnh ra $\text{nout}[u]$ | Khối lượng $\Delta w[u]$ | Cửa sổ $[e_u, l_u]$ | Thời lượng $\text{ndur}[u]$ | Chi phí nội tại $\text{nic}[u]$ | Doanh thu $\Delta r[u]$ | 
| ----- | ----- | ----- | ----- | ----- | ----- | ----- | ----- | ----- | ----- | 
| **Hành khách** | $1 \le u \le N$ | $u$ | $P_u$ | $D_u$ | $0$ | $[e_u, l_u]$ | $2 Sv_u + T(P_u, D_u)$ | $C(P_u, D_u)$ | $Rev_u$ | 
| **Nhặt kiện** | $N+1 \le u \le R$ | $u$ | $P_u$ | $P_u$ | $+nw_u$ | $[e_u, l_u]$ | $Sv_u$ | $0$ | $Rev_u$ | 
| **Giao kiện** | $R+1 \le u \le R+M$ | $u - M$ | $D_{u-M}$ | $D_{u-M}$ | $-nw_{u-M}$ | $[ed_{u-M}, lde_{u-M}]$ | $Sv_{u-M}$ | $0$ | $0$ | 

* **Thang đo năng lượng chuẩn hóa của bài toán (**$S_{\text{scale}}$**):**
  

  $$
  S_{\text{scale}} = \max\left(1.0, \; \frac{1}{\vert{}\mathcal{R}_{\text{valid}}\vert{}} \sum_{q \in \mathcal{R}_{\text{valid}}} \max(1, Rev_q - \text{nic}[q])\right)
  $$

## 2. TRÍCH RÚT TRI THỨC KHÔNG GIAN TỪ PHASE 0 (DE-BIASED KNOWLEDGE EXTRACTION)

Chạy thuật toán **GM-SISR-SA** trong khoảng thời gian $\tau_0 = 0.25 \cdot T_{\text{limit}}$ (25% tổng quỹ thời gian). Để chống hiện tượng tự tương quan (autocorrelation overfitting) vào một lòng chảo duy nhất, việc thu thập thống kê tuân thủ 2 cơ chế:

### 2.1. Thu thập Tần suất Đa Lòng Chảo & Thu hoạch Wave 1

Mỗi khi GM-SISR tìm thấy một nghiệm cục bộ mới cải thiện ($\Delta > 0$) hoặc tại mỗi chu kỳ nhiệt (mỗi $256$ bước lặp), với mỗi lộ trình xe $k$ có lợi nhuận ròng $\pi_k > 0$:

1. **Băm Zobrist Khử Trùng Lặp (**$O(1)$**):**
   Khởi tạo mảng ngẫu nhiên 64-bit `Z_req[1..R]` và `Z_veh[1..K]`. Một cột được định danh duy nhất bởi cặp `(k, mask_hash)` với:
   

   $$
   H(k, R_j) = Z_{\text{veh}}[k] \oplus \bigoplus_{q \in R_j} Z_{\text{req}}[q]
   $$

   
   Nếu $H(k, R_j)$ đã tồn tại trong bảng băm nhưng lộ trình mới có chi phí đường đi thấp hơn, cập nhật lộ trình mới cho cột đó. (Giới hạn Wave 1 tối đa $15,000$ cột tốt nhất + các cột tiền tố rỗng tải $\text{load}[i] = 0$).

2. **Cập nhật Ma trận Tri thức có Trọng số Chất lượng:**
   Đặt trọng số mẫu $w_{\text{sample}} = 1.0$ cho bước cải thiện thường và $w_{\text{sample}} = 3.0$ cho bước phá kỷ lục toàn cục:

   * **Tần suất Cạnh Sự kiện (**$F_{\text{arc}}$ **kích thước** $(E+1) \times (E+1)$**):** Với lộ trình sự kiện $(u_1, u_2, \dots, u_m)$, cộng $w_{\text{sample}}$ vào $F_{\text{arc}}[0][u_1]$ và $F_{\text{arc}}[u_i][u_{i+1}]$.

   * **Tần suất Thứ tự Đơn hàng Cùng xe (**$F_{\text{order}}$ **kích thước** $(R+1) \times (R+1)$**):** Đặt $\text{first\_pos}(q)$ là vị trí xuất hiện đầu tiên của đơn $q$ trên tuyến. Với mọi cặp đơn phân biệt $(p, q)$ cùng nằm trên xe $k$:
     

     $$
     \text{Nếu } \text{first\_pos}(p) < \text{first\_pos}(q) \implies F_{\text{order}}[p][q] \mathrel{+}= w_{\text{sample}}
     $$

### 2.2. Công thức Làm mượt Laplace bằng Tiên nghiệm Không-Thời gian (Spatio-Temporal Laplace Smoothing)

Để những đơn hàng bị Heuristic bỏ sót vẫn có xác suất dẫn đường hợp lý trong DFS, ta kết hợp tần suất thực nghiệm với tiên nghiệm hình học $P_{\text{geo}}$:

1. **Tiên nghiệm chuyển tiếp hình học giữa hai sự kiện** $u \to v$**:**
   

   $$
   d_{\text{norm}}(u \to v) = \frac{T(\text{nout}[u], \text{nin}[v])}{\bar{T}} + \frac{\max(0, \; e_v - (e_u + \text{ndur}[u] + T(\text{nout}[u], \text{nin}[v])))}{\bar{E}}
   $$

   $$
   P_{\text{geo}}(u \to v) = \exp\left(-1.5 \cdot d_{\text{norm}}(u \to v)\right)
   $$

   
   *(Trong đó* $\bar{T}, \bar{E}$ *là khoảng cách thời gian di chuyển và độ lệch cửa sổ thời gian trung bình của toàn đồ thị).*

2. **Ma trận Xác suất Cạnh Làm mượt (**$\tilde{P}_{\text{arc}}$**):**
   

   $$
   \tilde{P}_{\text{arc}}(u \to v) = (1 - \eta) \cdot \frac{F_{\text{arc}}[u][v]}{\sum_{v'=1}^E F_{\text{arc}}[u][v'] + 10^{-9}} + \eta \cdot P_{\text{geo}}(u \to v), \quad \text{với } \eta = 0.30
   $$

3. **Ma trận Tương hợp Thứ tự Vĩ mô (**$\tilde{P}_{\text{order}}$**):**
   Với hai đơn hàng $p, q \in \{1, \dots, R\}$:
   

   $$
   \tilde{P}_{\text{order}}(p \prec q) = \frac{F_{\text{order}}[p][q] + 0.25 \cdot P_{\text{geo}}(p \to q)}{N_{\text{total\_routes\_sampled}} + 0.25}
   $$

## 3. ĐỘNG CƠ SINH CỘT ĐA TẦNG BẰNG BEAM-DFS (PHASE 1.1: MULTI-WAVE GENERATOR)

Mục tiêu: Sinh tối đa $\vert{}\mathcal{C}_{\max}\vert{} = 100,000$ cột hợp lệ trong thời gian $\tau_{1.1} = 0.45 \cdot T_{\text{limit}}$, phân bổ thành 3 làn sóng:

* **Wave 1 (**$\le 15,000$ **cột):** Thu hoạch trực tiếp + cắt tiền tố rỗng tải từ Phase 0.

* **Wave 2 (**$\approx 50,000$ **cột):** Sinh bằng Knowledge-Guided Beam-DFS với nhân tử đối ngẫu $\lambda_q = 0$.

* **Wave 3 (**$\approx 35,000$ **cột):** Sau khi chạy nhanh 150 bước Lagrangian trên tập cột của Wave 1 + Wave 2 (mất $\approx 15\text{ms}$) để lấy giá bóng $\lambda_q \ge 0$, chạy tiếp Beam-DFS với doanh thu hiệu chỉnh $\widetilde{Rev}_q = Rev_q - \lambda_q$ để sinh các **"cột hy sinh"** và **"cột cứu đói"**.

### 3.1. Biểu diễn Trạng thái Đệ quy DFS & Cập nhật $O(1)$

Khi xây dựng lộ trình cho xe $k$, trạng thái hiện tại ở độ sâu $d$ gồm:

* `u_last`: Đỉnh sự kiện vừa ghé thăm ($0$ nếu đang ở Depot $O_k$).

* `t_dep`: Thời điểm rời khỏi `u_last`.

* `w_cur`: Tải trọng bưu kiện hiện tại trên xe ($0 \le w_{\text{cur}} \le Q_k$).

* `cur_profit`: Lợi nhuận ròng thực tế lũy kế ($\sum Rev_q - \text{Cost}$).

* `red_profit`: Lợi nhuận rút gọn lũy kế ($\text{cur\_profit} - \sum_{q \in \mathcal{M}_{\text{vis}}} \lambda_q$).

* `vis_req[1..R]`: Mảng đánh dấu các đơn hàng đã được chạm tới.

* `open_parcels`: Vector chứa các mã đơn bưu kiện $q \in \{N+1, \dots, R\}$ đã nhặt nhưng chưa giao (luôn có kích thước rất nhỏ, $\vert{}\text{open\_parcels}\vert{} \le 8$).

* `acc_order[1..R]`: **Mảng cộng dồn điểm thứ tự vĩ mô**. Khi đơn $p$ vừa được đưa vào tuyến (tại bước nhặt hàng hoặc đón khách), ta cập nhật một lần duy nhất:
  

  $$
  \forall q \in \{1, \dots, R\}: \quad \text{acc\_order}[q] \mathrel{+}= \tilde{P}_{\text{order}}(p \prec q)
  $$

  
  Khi lùi đệ quy (backtrack), trừ trả lại $\tilde{P}_{\text{order}}(p \prec q)$. Nhờ đó, việc lấy điểm tương hợp thứ tự của ứng viên $v$ với toàn bộ lịch sử tuyến hiện tại chỉ tốn $O(1)$!

### 3.2. Ba Quy tắc Cắt tỉa Cứng Tất định (Hard Feasibility Pruning)

Một ứng viên sự kiện $v \in \mathcal{V}_{\text{evt}}$ (với đơn $q = \text{req}(v)$) được xét mở rộng từ trạng thái hiện tại chỉ khi vượt qua cả 3 chốt chặn vật lý sau:

1. **Chốt chặn Trạng thái Đơn hàng & Tải trọng:**

   * Nếu $v \le R$ (Đón khách hoặc Nhặt kiện): Phải có `vis_req[q] == 0` VÀ `w_cur + dw[v] <= Q[k]`.

   * Nếu $v > R$ (Giao kiện của đơn $q = v - M$): Đơn $q$ bắt buộc phải đang nằm trong `open_parcels`.

2. **Chốt chặn Cửa sổ Thời gian Trực tiếp:**
   

   $$
   \text{arr}_v = t_{\text{dep}} + T(\text{nout}[u_{\text{last}}], \text{nin}[v]) \le l_v
   $$

   $$
   \text{dep}_v = \max(\text{arr}_v, e_v) + \text{ndur}[v]
   $$

3. **Chốt chặn Sống sót của các Bưu kiện Đang mở (Look-ahead Parcel Survival Pruning):**
   Giả sử bước tới $v$ tại thời điểm $\text{dep}_v$. Mọi bưu kiện $p \in \text{open\_parcels}$ (trừ $q$ nếu $v$ chính là nút giao của $q$) phải còn đủ thời gian để chạy thẳng từ $\text{nout}[v]$ tới điểm giao $D_p$ trước hạn chót $l_{R+p}$:
   

   $$
   \forall p \in \text{open\_parcels} \setminus \{q\}: \quad \text{dep}_v + T(\text{nout}[v], \text{nin}[R+p]) \le l_{R+p}
   $$

   
   *(Nếu vi phạm bất kỳ* $p$ *nào, cắt bỏ nhánh* $v$ *ngay lập tức).*

### 3.3. Hàm Điểm Ưu tiên Nhánh (Dynamic Branching Score)

Với mỗi ứng viên sự kiện $v$ hợp lệ (ứng với đơn $q = \text{req}(v)$), gọi:

* Chi phí biên: $\Delta c = C(\text{nout}[u_{\text{last}}], \text{nin}[v]) + \text{nic}[v]$.

* Thời gian tiêu tốn biên: $\Delta t = \max(1LL, \; \text{dep}_v - t_{\text{dep}})$.

* Doanh thu rút gọn biên:
  

  $$
  \Delta \tilde{r}_v = \begin{cases} Rev_q - \lambda_q & \text{nếu } v \le N \text{ (Hành khách)} \\ 0.5 \cdot (Rev_q - \lambda_q) & \text{nếu } N < v \le R \text{ (Nhặt bưu kiện - ghi nhận 50\% kỳ vọng)} \\ 0.5 \cdot (Rev_q - \lambda_q) & \text{nếu } v > R \text{ (Giao bưu kiện - hoàn tất 50\% còn lại)} \end{cases}
  $$

Điểm ưu tiên mở rộng nhánh $v$ được tính trong $O(1)$:

$$
\text{Score}(v \mid S, k) = \gamma_{\text{fatigue}}(u_{\text{last}}, v) \times \left[ \frac{\Delta \tilde{r}_v - \Delta c}{S_{\text{scale}} \cdot (\Delta t / \bar{T} + 0.5)} + 1.2 \cdot \tilde{P}_{\text{arc}}(u_{\text{last}} \to v) + 0.8 \cdot \frac{\text{acc\_order}[q]}{\vert{}\mathcal{M}_{\text{vis}}\vert{} + 1} \right]
$$


Trong đó:

* $\gamma_{\text{fatigue}}(u, v) = \frac{1.0}{1.0 + 0.03 \cdot \text{usage\_count}_k(u \to v)}$ là hệ số **Phạt bão hòa cung cạnh**, tự động giảm điểm của những cung đã xuất hiện quá nhiều lần trong Pool của xe $k$.

* Tại mỗi nút của cây tìm kiếm, chọn ra Top-$B$ ứng viên có $\text{Score}(v \mid S, k)$ cao nhất (với độ rộng chùm tia giảm dần theo độ sâu: $B = 6$ ở độ sâu $d \le 2$, $B = 3$ ở $3 \le d \le 6$, và $B = 2$ ở $d > 6$) kết hợp nhiễu Gumbel nhẹ để tạo đa dạng.

* **Điều kiện Thu hoạch Cột (Column Emission Condition):** Tại bất kỳ nút nào trên cây mà **`open_parcels.empty() == true`** (tức `w_cur == 0`, không còn bưu kiện treo trên xe) và **`cur_profit > 0`**, trạng thái hiện tại lập tức được đóng gói thành một cột hợp lệ $C_j = \langle k, \text{cur\_profit}, \mathcal{M}_{\text{vis}}, \text{seq\_nodes} \rangle$ và đẩy vào Pool nếu mã Zobrist $H(k, \mathcal{M}_{\text{vis}})$ chưa tồn tại (hoặc có $\text{cur\_profit}$ cao hơn bản ghi cũ).

## 4. GIẢI BÀI TOÁN SET PACKING BẰNG LAGRANGIAN & EJECTION-CHAIN SA (PHASE 1.2)

Sau khi Phase 1.1 kết thúc, ta có tập $\mathcal{C}$ gồm $M_{\text{col}} \le 100,000$ cột. Mỗi cột $j \in \mathcal{C}$ thuộc về xe $k_j \in \{1, \dots, K\}$, có lợi nhuận thực $\pi_j > 0$ và phục vụ tập đơn hàng $R_j \subseteq \{1, \dots, R\}$.

### 4.1. Cấu trúc Dữ liệu Chỉ mục Ngược (Inverted Bit-Index)

Để đạt tốc độ $> 1.5 \times 10^6$ bước lặp/giây trong Phase 1.2, tuyệt đối không duyệt mảng tuần tự để kiểm tra giao tập hợp. Xây dựng 3 bảng chỉ mục tĩnh:

1. `cols_of_veh[k]`: Danh sách các chỉ số cột $j$ thuộc về xe $k$.

2. `req_list[j]`: Mảng nhỏ chứa các đơn hàng $q \in R_j$ của cột $j$.

3. `cols_covering_req[q]`: Danh sách các cột $j$ có chứa đơn hàng $q$ (được sắp xếp giảm dần theo lợi nhuận $\pi_j$).

### 4.2. Thuật toán Hạ Gradient dưới Lagrangian (Lagrangian Subgradient Optimization)

Giải bài toán đối ngẫu để tìm cận trên toàn cục $UB$ và vector giá bóng tối ưu $\lambda^* = (\lambda^*_1, \dots, \lambda^*_R) \ge 0$:

$$
L(\lambda) = \sum_{q=1}^R \lambda_q + \sum_{k=1}^K \max \left( 0.0, \; \max_{j \in \text{cols\_of\_veh}[k]} \left\{ \pi_j - \sum_{q \in R_j} \lambda_q \right\} \right)
$$

**Quy trình lặp (**$I_{\text{lag}} = 400$ **vòng lặp, thời gian chạy** $\approx 0.04\text{s}$**):**

* Khởi tạo: $\lambda^{(0)}_q = 0$, hệ số bước nhảy $\theta = 1.5$, cận dưới tốt nhất hiện tại $LB$ (lấy từ nghiệm tốt nhất của GM-SISR ở Phase 0).

* Tại vòng lặp $t = 1 \dots I_{\text{lag}}$:

  1. Với mỗi cột $j$, tính lợi nhuận rút gọn $\tilde{\pi}_j = \pi_j - \sum_{q \in R_j} \lambda_q$.

  2. Với mỗi xe $k$, tìm cột tốt nhất $j^*_k = \arg\max_{j \in \text{cols\_of\_veh}[k]} \tilde{\pi}_j$ (chỉ chọn nếu $\tilde{\pi}_{j^*_k} > 0$).

  3. Tính giá trị đối ngẫu $L(\lambda^{(t)})$. Nếu $L(\lambda^{(t)}) < UB$, cập nhật $UB \leftarrow L(\lambda^{(t)})$ và lưu $\lambda^* \leftarrow \lambda^{(t)}$.

  4. **Khôi phục Nghiệm Nguyên Tham lam (Primal Heuristic Extraction):** Sắp xếp các xe $k$ theo thứ tự giảm dần của $\tilde{\pi}_{j^*_k}$. Duyệt từng xe, nếu cột $j^*_k$ không xung đột với các đơn đã chọn thì gán trực tiếp cho xe $k$; nếu xung đột, quét nhanh trong `cols_of_veh[k]` tìm cột không xung đột có $\pi_j$ lớn nhất. Nếu tổng lợi nhuận vượt $LB$, cập nhật $LB$ và nghiệm toàn cục!

  5. Cập nhật Subgradient: Đếm số lần mỗi đơn $q$ xuất hiện trong $\{j^*_1, \dots, j^*_K\}$ là $m_q$. Đặt $g_q = 1 - m_q$.
     

     $$
     \text{step} = \theta \cdot \frac{L(\lambda^{(t)}) - LB}{\sum_{q=1}^R g_q^2 + 10^{-9}}, \qquad \lambda^{(t+1)}_q = \max\left(0.0, \; \lambda^{(t)}_q - \text{step} \cdot g_q\right)
     $$

  6. Nếu sau $25$ vòng lặp liên tiếp $UB$ không giảm, giảm nửa $\theta \leftarrow 0.5 \cdot \theta$. Dừng sớm ngay lập tức nếu $\lfloor UB \rfloor \le LB$ (chứng minh được nghiệm tối ưu toàn cục!).

### 4.3. Lọc Rút gọn Cột Bằng Khoảng cách Đối ngẫu (Lagrangian Variable Fixing)

Sau khi có $UB$, $LB$ và $\lambda^*$, tính độ lệch đối ngẫu $\Delta_{\text{gap}} = UB - LB$.
Mọi cột $j \in \mathcal{C}$ của xe $k$ có lợi nhuận rút gọn kém hơn cột tốt nhất của xe đó một lượng vượt quá $\Delta_{\text{gap}}$ chắc chắn không thể nằm trong nghiệm tối ưu:

$$
\text{Nếu } \tilde{\pi}_{j^*_k}(\lambda^*) - \tilde{\pi}_j(\lambda^*) > \Delta_{\text{gap}} \implies \text{Loại bỏ cột } j \text{ khỏi không gian tìm kiếm SA.}
$$


*(Bước này thường cắt bỏ được* $60\% - 85\%$ *số cột rác, cô đọng Pool xuống chỉ còn* $15,000 - 30,000$ *cột tinh nhuệ nhất).*

### 4.4. Tối ưu Hóa Tổ hợp bằng Ejection-Chain Simulated Annealing (Phase 1.2 Core)

Duy trì trạng thái hiện tại của bài toán Set Packing gồm:

* `active_col[1..K]`: Chỉ số cột đang được chọn cho xe $k$ ($-1$ nếu xe nghỉ).

* `owner_veh[1..R]`: Mã xe đang phục vụ đơn hàng $q$ ($0$ nếu đơn $q$ chưa được xe nào phục vụ).

* `cur_sp_obj`: Tổng lợi nhuận của $K$ cột đang chọn.

**Phép biến đổi Chuỗi Đẩy (1-Step & 2-Step Ejection Chain Move) trong mỗi vòng lặp SA:**

1. Chọn ngẫu nhiên một xe $k_1 \in \{1, \dots, K\}$ và bốc một cột ứng viên $j_{\text{new}} \in \text{cols\_of\_veh}[k_1]$ (ưu tiên theo trọng số $\tilde{\pi}_j(\lambda^*)$).

2. Giải phóng tạm thời cột cũ `j_old = active_col[k_1]`.

3. Kiểm tra các đơn hàng $q \in R_{j_{\text{new}}}$ xem đang bị những xe nào khác chiếm giữ (`owner_veh[q] != 0`):

   * Tập các xe bị xâm phạm: $\mathcal{K}_{\text{conflict}} = \{ \text{owner\_veh}[q] : q \in R_{j_{\text{new}}} \text{ và } \text{owner\_veh}[q] \neq 0 \}$.

   * Nếu $\vert{}\mathcal{K}_{\text{conflict}}\vert{} > 2$: Từ chối bước đi ngay lập tức ($O(\vert{}R_{j_{\text{new}}}\vert{})$) vì phá vỡ quá nhiều xe cùng lúc.

4. **Cơ chế Sửa chữa Tức thời (Instant Repair / Re-routing Displaced Vehicles):**
   Với mỗi xe bị đá văng $k_2 \in \mathcal{K}_{\text{conflict}}$ (tối đa 2 xe):

   * Tháo cột hiện tại của $k_2$ ra (các đơn trên cột cũ của $k_2$ nay trở thành tự do!).

   * Duyệt danh sách cột đã sắp xếp của $k_2$ để tìm cột thay thế $j_{\text{rep}} \in \text{cols\_of\_veh}[k_2]$ **hoàn toàn không xung đột** với các đơn đang có chủ và mang lại lợi nhuận $\pi_{j_{\text{rep}}}$ lớn nhất (dùng thêm cơ chế *Blink* bỏ qua với xác suất $1/32$ để tránh kẹt cục bộ).

5. **Tính Độ lệch Toàn cục (**$\Delta_{\text{SP}}$**) và Xét duyệt Metropolis:**
   

   $$
   \Delta_{\text{SP}} = \pi_{j_{\text{new}}} - \pi_{j_{\text{old}}} + \sum_{k_2 \in \mathcal{K}_{\text{conflict}}} \left( \pi_{j_{\text{rep}}(k_2)} - \pi_{\text{active\_col}[k_2]} \right)
   $$

   * Áp dụng bộ điều nhiệt **SEDCA-V3 (Thung lũng đáy phẳng chữ U)** để quyết định chấp nhận hay từ chối phép thế chuỗi đẩy này.

   * Sau mỗi $500$ vòng lặp ở đáy lạnh, quét nhanh các đơn đang tự do (`owner_veh[q] == 0`): nếu có xe $k$ nào đang dùng cột $j$ mà tồn tại cột $j'$ chứa toàn bộ đơn của $j$ cộng thêm các đơn tự do ($\pi_{j'} > \pi_j$), lập tức nâng cấp $j \to j'$!

## 5. MÃ GIẢ TỔNG THỂ CẤP ĐỘ KIẾN TRÚC (END-TO-END C++ SKELETON)

Dưới đây là cấu trúc khung chuẩn hóa để AI cài đặt trực tiếp vào một file `.cpp` duy nhất:

```
// ============================================================================
// CORE DATA STRUCTURES FOR PHASE 1.1 & PHASE 1.2
// ============================================================================
struct Column {
    int veh_id;               // Xe k (1..K)
    ll profit;                // Lợi nhuận ròng thực tế > 0
    double reduced_profit;    // Lợi nhuận rút gọn theo giá bóng Lagrangian
    uint64_t hash_sig;        // Chữ ký Zobrist của tập đơn hàng R_j
    vector<int> reqs;         // Danh sách các đơn hàng q thuộc R_j (đã sort)
    vector<int> seq_nodes;    // Chuỗi đỉnh sự kiện thực tế để in kết quả cuối cùng
};

static vector<Column> col_pool;
static unordered_map<uint64_t, int> hash_to_col_idx; // Khử trùng lặp O(1)
static vector<vector<int>> cols_of_veh;              // [K + 1]

// Hàm thu hoạch cột O(|R_j|) dùng chung cho cả GM-SISR (Wave 1) và Beam-DFS (Wave 2 & 3)
static void try_harvest_column(int k, ll profit, const vector<int>& seq_nodes, const vector<int>& reqs_served) {
    if (profit <= 0 || reqs_served.empty()) return;
    uint64_t h = Z_veh[k];
    for (int q : reqs_served) h ^= Z_req[q];

    auto it = hash_to_col_idx.find(h);
    if (it != hash_to_col_idx.end()) {
        int idx = it->second;
        // Nếu cùng tập đơn hàng trên cùng xe k nhưng thứ tự đi mới tiết kiệm chi phí hơn -> Cập nhật!
        if (profit > col_pool[idx].profit) {
            col_pool[idx].profit = profit;
            col_pool[idx].seq_nodes = seq_nodes;
        }
        return;
    }
    if ((int)col_pool.size() >= MAX_POOL_CAP) return;

    int new_idx = (int)col_pool.size();
    hash_to_col_idx[h] = new_idx;
    col_pool.push_back({k, profit, (double)profit, h, reqs_served, seq_nodes});
    cols_of_veh[k].push_back(new_idx);
}

// ============================================================================
// KNOWLEDGE-GUIDED BEAM-DFS (WAVE 2 & WAVE 3 PRICING ENGINE)
// ============================================================================
struct DFSContext {
    int k;
    vector<char> vis_req;
    vector<int> open_parcels;
    vector<int> seq_nodes;
    vector<int> reqs_served;
    vector<double> acc_order;
    ll cur_rev = 0, cur_cost = 0;

    void dfs_expand(int u_last, ll t_dep, int w_cur, int depth, const vector<double>& dual_lambda) {
        // 1. Nếu không còn bưu kiện treo trên xe và có lãi -> Thu hoạch cột ngay!
        if (open_parcels.empty() && !reqs_served.empty()) {
            ll net_pi = cur_rev - cur_cost;
            if (net_pi > 0) try_harvest_column(k, net_pi, seq_nodes, reqs_served);
        }
        if (depth >= MAX_DFS_DEPTH || (int)col_pool.size() >= MAX_POOL_CAP) return;

        // 2. Sinh danh sách ứng viên sự kiện v hợp lệ
        struct CandBranch { int v; ll next_dep; int dc; double score; };
        vector<CandBranch> branches;

        // Xét giao các bưu kiện đang mở trên xe trước, cộng với các đơn lân cận chưa phục vụ
        for (int v : get_candidate_events(u_last, open_parcels)) {
            int q = (v <= R) ? v : (v - M);
            if (v <= R && (vis_req[q] || w_cur + nw[v] > Q[k])) continue;

            int u_out = (u_last == 0) ? O[k] : nout[u_last];
            ll arr = t_dep + T(u_out, nin[v]);
            if (arr > nl[v]) continue;
            ll dep = max(arr, ne[v]) + ndur[v];

            // Chốt chặn sống sót cho mọi bưu kiện đang mở trên xe
            bool parcel_dead = false;
            for (int p : open_parcels) {
                if (v == R + p) continue;
                if (dep + T(nout[v], nin[R + p]) > nl[R + p]) { parcel_dead = true; break; }
            }
            if (parcel_dead) continue;

            // Tính điểm O(1) kết hợp Reduced Profit + P_arc + acc_order
            int dc = C(u_out, nin[v]) + nic[v];
            double dr = (v <= N) ? (Rev[q] - dual_lambda[q]) : (0.5 * (Rev[q] - dual_lambda[q]));
            double dt_norm = (double)max(1LL, dep - t_dep) / T_bar + 0.5;
            double red_rate = (dr - dc) / (S_scale * dt_norm);
            double p_arc = P_arc_smooth[u_last][v];
            double p_ord = acc_order[q] / (double)(reqs_served.size() + 1);
            double fatigue = 1.0 / (1.0 + 0.03 * arc_usage[k][u_last][v]);

            double sc = fatigue * (red_rate + 1.2 * p_arc + 0.8 * p_ord) + 0.05 * rand01();
            branches.push_back({v, dep, dc, sc});
        }

        // 3. Cắt tỉa Beam theo độ sâu
        int beam_w = (depth <= 2) ? 5 : ((depth <= 6) ? 3 : 2);
        if ((int)branches.size() > beam_w) {
            partial_sort(branches.begin(), branches.begin() + beam_w, branches.end(),
                         [](const CandBranch& a, const CandBranch& b) { return a.score > b.score; });
            branches.resize(beam_w);
        }

        // 4. Đệ quy mở rộng và cập nhật cộng dồn O(R)
        for (const auto& br : branches) {
            int v = br.v, q = (v <= R) ? v : (v - M);
            seq_nodes.push_back(v);
            cur_cost += br.dc;
            arc_usage[k][u_last][v]++;

            if (v <= R) {
                vis_req[q] = 1;
                reqs_served.push_back(q);
                cur_rev += Rev[q];
                for (int r : servable_list) acc_order[r] += P_order_smooth[q][r];
                if (v > N) open_parcels.push_back(q);
            } else {
                open_parcels.erase(find(open_parcels.begin(), open_parcels.end(), q));
            }

            dfs_expand(v, br.next_dep, w_cur + nw[v], depth + 1, dual_lambda);

            // Backtrack hoàn trả trạng thái
            if (v <= R) {
                if (v > N) open_parcels.pop_back();
                for (int r : servable_list) acc_order[r] -= P_order_smooth[q][r];
                cur_rev -= Rev[q];
                reqs_served.pop_back();
                vis_req[q] = 0;
            } else {
                open_parcels.push_back(q);
            }
            cur_cost -= br.dc;
            seq_nodes.pop_back();
        }
    }
};

```

## 6. PHÂN BỔ NGÂN SÁCH THỜI GIAN TỔNG THỂ (TIME BUDGET ALLOCATION)

Để toàn bộ hệ thống vận hành trơn tru trong mọi giới hạn thời gian $T_{\text{limit}}$ (ví dụ $2.0\text{s}$ hoặc $6.0\text{s}$) mà không bao giờ xảy ra lỗi Timeout hay thiếu cột:

| Pha thực thi | Tỷ lệ Thời gian | Nhiệm vụ cụ thể | Đầu ra bàn giao | 
| ----- | ----- | ----- | ----- | 
| **Phase 0: GM-SISR Exploration** | $25\%$  *(0.00* $\to$ *0.25)* | Chạy GM-SISR với bộ điều nhiệt SEDCA-V3. Thu thập tần suất $F_{\text{arc}}, F_{\text{order}}$ và thu hoạch trực tiếp **Wave 1** ($\le 15,000$ cột tinh hoa + tiền tố). | Cận dưới cơ sở $LB_0$, ma trận $\tilde{P}_{\text{arc}}, \tilde{P}_{\text{order}}$, và $15,000$ cột đầu tiên. | 
| **Phase 1.1A: Wave 2 Beam-DFS** | $25\%$  *(0.25* $\to$ *0.50)* | Chạy Beam-DFS dẫn đường bởi $\tilde{P}_{\text{arc}}, \tilde{P}_{\text{order}}$ với $\lambda_q = 0$ để tái tổ hợp các mảnh tri thức thành các cột lai ghép mới. | Pool đạt $\approx 65,000$ cột đa dạng. | 
| **Phase 1.1B: Dual Pricing & Wave 3** | $15\%$  *(0.50* $\to$ *0.65)* | Chạy $150$ vòng lặp Lagrangian Subgradient lấy giá bóng $\lambda_q$. Chạy tiếp Beam-DFS với $\widetilde{Rev}_q = Rev_q - \lambda_q$ để sinh các cột hy sinh và cứu các đơn có $\lambda_q = 0$. | Hoàn tất Pool $100,000$ cột bao phủ toàn diện. | 
| **Phase 1.2: Lagrangian Filter & Set Packing SA** | $35\%$  *(0.65* $\to$ *1.00)* | Chạy $400$ vòng Subgradient siết cận trên $UB$, lọc bỏ cột rác ($\tilde{\pi} < -\Delta_{\text{gap}}$), và chạy **Ejection-Chain SA** ghép các cột thành nghiệm toàn cục tốt nhất. | Nghiệm xuất chuẩn định dạng SOICT 2026 kèm cận chứng minh $UB$. | 
