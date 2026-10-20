# OPTIMIZATION TECHNIQUES: HIGH-PERFORMANCE TOOLKIT FOR SOICT 2026

Tài liệu này hệ thống hóa các **kỹ thuật tối ưu hóa dùng chung (General Optimization Toolkit)** có thể áp dụng cho mọi nguyên mẫu thuật toán (Heuristics, Local Search, Tabu Search, ALNS, SISR, Simulated Annealing) giải bài toán **SOICT 2026: Integrated Passenger and Parcel Transportation**.

---

## 1. KIỂM TRA TÍNH KHẢ THI HẬU TỐ TRONG $O(1)$ (`lat[]` BACKWARD SLACK)

### 1.1. Bản chất Vấn đề
Trong bài toán VRP có cửa sổ thời gian (VRPTW/PDPTW), khi thử chèn một khách hàng hoặc kiện hàng vào vị trí $i$ trên lộ trình gồm $n$ điểm dừng của xe $k$:
- Cách tiếp cận ngây thơ (Naïve approach): Duyệt lại toàn bộ các điểm dừng từ $i+1$ đến $n$ để cập nhật thời gian đến và kiểm tra vi phạm time window $\implies$ Tốn $\mathcal{O}(n)$ cho mỗi phép thử chèn.
- Khi một vòng tìm kiếm thử hàng nghìn vị trí, $\mathcal{O}(n)$ trở thành nút thắt cổ chai chiếm tới $80\%$ thời gian CPU.

### 1.2. Mảng Hậu tố Thời điểm Đến Muộn nhất (`lat[1..n+1]`)
Ta tính trước mảng $\text{lat}[i]$ bằng quy nạp lùi từ điểm cuối lộ trình về điểm đầu ($i = n \to 1$):

$$\text{lat}[n+1] = +\infty$$
$$\text{lat}[i] = \min\Big(nl[\text{nd}[i]], \; \text{lat}[i+1] - T\big(\text{nout}[\text{nd}[i]], \text{nin}[\text{nd}[i+1]]\big) - \text{ndur}[\text{nd}[i]]\Big)$$

*Ý nghĩa vật lý*: $\text{lat}[i]$ là **thời điểm muộn nhất** mà xe có thể đặt chân đến điểm vào $\text{nin}[\text{nd}[i]]$ của node thứ $i$ sao cho **toàn bộ các node từ $i$ đến $n$ không có node nào bị quá hạn chót**.

### 1.3. Áp dụng Kiểm tra Chèn Khách hàng trong $\mathcal{O}(1)$
Khi thử chèn khách hàng $q \le N$ vào sau vị trí $i$:
1. Tính thời điểm xe rời điểm trả khách:
   $$d = \max\Big(\text{dep}[i] + T\big(\text{nout}[\text{nd}[i]], P_q\big), \; E_q\Big) + \text{ndur}[q]$$
2. **Kiểm tra đúng 1 phép so sánh duy nhất trong $\mathcal{O}(1)$**:
   ```cpp
   if (d + T(nout[q], nin[nd[i + 1]]) > lat[i + 1]) {
       // BẤT KHẢ THI: Sẽ làm trễ ít nhất một node phía sau!
       continue; 
   }
   ```
3. **Ngắt sớm đơn điệu**: Vì $\text{dep}[i]$ đơn điệu tăng, nếu $\text{dep}[i] > L_q$, ta lập tức `break` vòng lặp duyệt vị trí $i$.

### 1.4. Kỹ thuật Ngắt sớm Đồng bộ (`sync`) khi Chèn Bưu kiện
Khi chèn kiện hàng $q > N$ (gồm điểm đón $P_q$ tại $i$ và điểm trả $D_q$ tại $j > i$):
- Xe có thể đến một điểm $x = \text{nd}[s]$ sớm hơn $ne[x]$ và phải đứng chờ. Khoảng chờ này hấp thụ độ trễ do việc chèn $P_q$ gây ra.
- Nếu thời điểm rời mới $dj == \text{dep}[s]$ cũ, ta bật cờ `sync = true`. Kể từ điểm này trở đi, toàn bộ thời gian di chuyển phía sau không thay đổi, ta có thể lấy thẳng $\text{cur} = \text{dep}[s]$ mà không cần cộng dồn ma trận thời gian nữa!

---

## 2. BỘ NHỚ ĐỆM PHIÊN BẢN LỘ TRÌNH $\mathcal{O}(1)$ (`icache` + `rver`)

### 2.1. Động lực
Trong các thuật toán tìm kiếm cục bộ (Local Search) hoặc phá hủy/tái tạo (Ruin & Recreate), mỗi bước di chuyển chỉ thay đổi lộ trình của $2 \sim 4$ xe trong tổng số $K$ xe (ví dụ $K = 125$).
- Nếu mỗi vòng lặp đều duyệt lại tất cả các cặp $(\text{request } q, \text{xe } k)$ để tìm vị trí chèn tốt nhất, ta lãng phí $>95\%$ phép tính vào các xe hoàn toàn không bị chỉnh sửa.

### 2.2. Cơ chế Đánh dấu Phiên bản (Version Tagging)
1. Mỗi xe $k \in \{1 \dots K\}$ được gán một số hiệu phiên bản $\text{rver}[k]$.
2. Mỗi khi lộ trình xe $k$ bị thay đổi (thêm/bớt node hoặc hoàn tác), ta tăng biến đếm toàn cục:
   ```cpp
   rver[k] = ++gver;
   ```
3. Bảng nhớ đệm được cấu trúc phẳng 1 chiều:
   ```cpp
   struct CacheEntry { int ver = -1, i = 0, j = 0; ll cost = 0; };
   static vector<CacheEntry> icache; // Kích thước: (R + 1) * (K + 1)
   ```
4. Khi cần tìm chi phí chèn request $q$ vào xe $k$:
   ```cpp
   CacheEntry& e = icache[q * (K + 1) + k];
   if (e.ver != rver[k]) {
       e.cost = best_insert(k, q, e.i, e.j, blink);
       e.ver = rver[k];
   }
   // Nếu e.ver == rver[k]: Lấy ngay (e.cost, e.i, e.j) trong O(1)!
   ```
*Hiệu quả*: Đạt tỷ lệ Cache Hit $>96\%$, giúp hàm tái tạo (Recreate) tăng tốc gấp $20 \sim 30$ lần.

---

## 3. BỐ CỤC BỘ NHỚ TUYẾN TÍNH PHẲNG (FLAT CONTIGUOUS LAYOUT)

### 3.1. Hạn chế của Con trỏ và Vector lồng nhau
Cấu trúc `vector<vector<int>> matrix(V, vector<int>(V))` tạo ra $V$ vùng nhớ phân mảnh rời rạc trên Heap:
- Gây Cache Miss liên tục khi CPU phải nhảy con trỏ.
- Ngăn cản trình biên dịch áp dụng tối ưu hóa SIMD / Vectorization tự động.

### 3.2. Chuẩn hóa Mảng phẳng Tuyến tính
Chuyển đổi toàn bộ ma trận sang vector 1 chiều với bước nhảy cố định $\text{SV} = V + 1$:
```cpp
static vector<int> Tm, Cm; // Kích thước SV * SV
static inline int T(int u, int v) { return Tm[u * SV + v]; }
static inline int C(int u, int v) { return Cm[u * SV + v]; }
```
Tương tự cho các thuộc tính của đỉnh:
```cpp
static vector<int> nin, nout, nw, nic;
static vector<ll> ne, nl, ndur;
```
Toàn bộ dữ liệu nằm trên một dải RAM liên tục, tận dụng tối đa dòng nạp L1/L2 Cache của vi xử lý.

---

## 4. BỘ TẠO SỐ NGẪU NHIÊN BITWISE SIÊU TỐC (XORSHIFT64 RNG)

### 4.1. Vấn đề của RNG Chuẩn
`std::mt19937` (Mersenne Twister) duy trì mảng trạng thái 624 phần tử và tốn hàng chục chu kỳ xung nhịp cho mỗi lần gọi. Trong các vòng lặp sinh hàng chục triệu số ngẫu nhiên/giây, RNG chuẩn chiếm tới $5 \sim 10\%$ CPU.

### 4.2. Bộ sinh Xorshift64
Sử dụng Xorshift64 với chu kỳ $2^{64}-1$, chỉ dùng đúng 1 biến trạng thái 64-bit và 3 phép dịch bit:
```cpp
static uint64_t rng_s = 0x9E3779B97F4A7C15ull;
static inline uint64_t rng() {
    rng_s ^= rng_s << 7;
    rng_s ^= rng_s >> 9;
    return rng_s;
}
static inline int rand_int(int lo, int hi) { 
    return lo + (int)(rng() % (uint64_t)(hi - lo + 1)); 
}
static inline double rand01() { 
    return (rng() >> 11) * (1.0 / 9007199254740992.0); 
}
```
*Tốc độ*: Thực thi chỉ mất $2 \sim 3$ chu kỳ CPU/lần gọi.

---

## 5. KỸ THUẬT NHÁY MẮT (CHRISTIAENS' BLINK HEURISTIC)

### 5.1. Vấn đề của Chèn Tham lam Tất định
Khi tháo các request ra khỏi lộ trình rồi chèn lại bằng hàm tham lam (Best Insertion) thuần túy, các request có xu hướng rơi ngược trở lại đúng chiếc xe và vị trí ban đầu $\implies$ Bước nhảy trở nên vô nghĩa ($\Delta \text{Benefit} = 0$), thuật toán bị kẹt cứng ở cực tiểu địa phương.

### 5.2. Giải pháp Blink Heuristic ($p = 1/64$)
Trong hàm tìm vị trí chèn tốt nhất, ta bỏ qua vị trí tối ưu với xác suất $1/64$:
```cpp
static const unsigned BLINK_MASK = 63; // 63 = 2^6 - 1

if (dc < best && !(blink && (rng() & BLINK_MASK) == 0)) {
    best = dc;
    bi = i;
    bj = j;
}
```
*Ưu điểm*:
- Phép kiểm tra `(rng() & 63) == 0` chỉ tốn đúng 1 chu kỳ CPU (phép bitwise `AND`).
- Khi chèn một chuỗi gồm $\approx 8$ requests, xác suất để ít nhất một request bị "chệch hướng" sang vị trí tốt thứ nhì là:
  $$P = 1 - \left(1 - \frac{1}{64}\right)^8 \approx 11.84\%$$
- Thuật toán tự động tạo hiệu ứng tương đương **Stochastic Regret-2** trong $\approx 12\%$ trường hợp, ép cấu hình phải tái sắp xếp mà không cần tốn bộ nhớ lưu trữ danh sách Top-2!

---

## 6. CHẾ ĐỘ NHIỆT ĐỘ SA TỰ CHUẨN HÓA BẤT BIẾN THANG ĐO (SCALE-INVARIANT SA)

### 6.1. Nguyên lý Chuẩn hóa theo Doanh thu Trung bình
Không bao giờ dùng nhiệt độ cố định ($T_0 = 100, T_F = 0.1$) vì các bộ testcase có thể có doanh thu chênh lệch hàng nghìn lần.
Lấy doanh thu trung bình $\overline{\text{Rev}}$ của một yêu cầu làm đơn vị đo năng lượng chuẩn:
```cpp
double mean_rev = 0; 
for (int r : servable_list) mean_rev += Rev[r];
mean_rev /= servable_list.size();

const double T0 = max(1e-9, mean_rev * 1.0);     // Cho phép bước lùi ~1 request với xác suất e^-1 ≈ 36.8%
const double TF = max(1e-9, mean_rev * 0.005);   // Cuối chu kỳ biến thành Hill-Climbing thuần túy
```

### 6.2. Làm nguội theo Thời gian Thực kết hợp Đa chu kỳ (Multi-cycle SA)
Nhiệt độ được làm nguội theo đường cong hàm mũ dựa trên tỷ lệ thời gian thực tế đã tiêu thụ:
```cpp
double elapsed = chrono::duration<double>(now - t_start).count();
double prog = (elapsed - search_start) / budget; // [0.0, 1.0]
prog *= SA_CYCLES;
int cyc = min(SA_CYCLES - 1, (int)prog);
prog -= cyc;

// Mỗi khi sang chu kỳ mới: Nung lại từ nghiệm tốt nhất
if (cyc != cur_cycle) {
    cur_cycle = cyc;
    load_best();
}
temp = T0 * pow(TF / T0, prog);
```

---

## 7. QUẢN LÝ BỘ ĐỆM TĨNH BẰNG ĐÁNH DẤU THỜI GIAN (TIMESTAMP MARKING)

### 7.1. Tránh `memset` hoặc `vector.clear()` lặp đi lặp lại
Khi cần theo dõi tập hợp các phần tử đã được xét (ví dụ danh sách ứng viên `cand` hoặc các xe đã chạm `touched`), việc gọi `fill` hoặc tạo mảng `bool visited[]` rồi xóa đi xóa lại tốn $\mathcal{O}(N)$ mỗi vòng lặp.

### 7.2. Kỹ thuật Dấu thời gian Toàn cục (`stamp_id`)
```cpp
static int stamp_id = 0;
static vector<int> cand_stamp; // Kích thước R + 1, khởi tạo bằng 0

// Trong vòng lặp tìm kiếm:
++stamp_id;

auto add_candidate = [&](int q) {
    if (servable[q] && !veh[q] && cand_stamp[q] != stamp_id) {
        cand_stamp[q] = stamp_id; // Đánh dấu đã xét trong O(1)
        cand.push_back(q);
    }
};
```
*Ưu điểm*: Không cần tốn bất kỳ chu kỳ nào để reset mảng giữa các vòng lặp!
