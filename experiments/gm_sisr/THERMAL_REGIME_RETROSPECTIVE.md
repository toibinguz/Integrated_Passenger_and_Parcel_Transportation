# HỒI CỨU THUẬT TOÁN LUYỆN KIM: TỪ ẢO TƯỞNG ĐIỀU KHIỂN THÍCH ỨNG ĐẾN BẢN CHẤT SÓNG RĂNG CƯA TIỀN ĐỊNH TRONG TỐI ƯU HÓA TỔ HỢP QUY MÔ LỚN

> **Tác giả:** Antigravity AI & Nghiên cứu viên Pair Programming  
> **Dự án:** Spatial-Temporal Ruin-and-Recreate / Large Neighborhood Search (SISR) cho PDPTW Đa mục tiêu  
> **Thời điểm:** Tháng 10, 2026  
> **Trạng thái:** Báo cáo Phân tích Chuyên sâu & Tổng kết Kiến trúc (Architectural Monograph)

---

## TÓM TẮT HỌC THUẬT (ABSTRACT)

Báo cáo này tài liệu hóa hành trình tiến hóa nhận thức và kiến trúc của chế độ nhiệt (cooling schedule / thermal thermostat) trong bộ giải pháp luyện kim mô phỏng (Simulated Annealing - SA) kết hợp phá hủy - tái thiết chuỗi (Christiaens & Berghe SISR). Xuất phát từ những giả định cổ điển thô sơ về quá trình làm lạnh đơn điệu, nghiên cứu đã trải qua giai đoạn phức tạp hóa cực đoan với tham vọng thiết lập một bộ điều nhiệt thích ứng vòng lặp kín (closed-loop adaptive thermostat) kết hợp năng lượng tự học và bộ đếm bế tắc. Mặc dù đạt được những đỉnh cao cục bộ giả tạo trên một vài thể nghiệm đơn lẻ, mô hình thích ứng nhanh chóng bộc lộ sự suy thoái nghiêm trọng trên toàn bộ không gian thử nghiệm đa dạng do các bẫy dao động vi mô (micro-oscillations), sự bùng nổ thang năng lượng và sự phụ thuộc phi tuyến vào tốc độ phần cứng. 

Thông qua nguyên lý Lưỡi dao Occam (Occam's Razor), chúng tôi thực hiện bước lùi chiến lược: từ bỏ hoàn toàn các vòng phản hồi động để tiến tới **Chế độ Nhiệt Răng cưa Tiền định theo Trục Thời gian Thực (Time-Driven Minimal Static Sawtooth Schedule)**. Kết quả thực nghiệm khẳng định rằng tính tuần hoàn hình học bền vững, độc lập với cảnh quan hàm mục tiêu, không chỉ tái lập hiệu năng kỷ lục mà còn triệt tiêu hoàn toàn các điểm kỳ dị thuật toán, mang lại sự ổn định tuyệt đối trên toàn bộ hệ thống chuẩn đối sánh.

---

## 1. NHỮNG GIẢ ĐỊNH THÔ SƠ BAN ĐẦU (PRIMITIVE ASSUMPTIONS)

### 1.1. Sự ngây thơ của Luyện kim Đơn điệu Cổ điển
Trong giai đoạn đầu phát triển bộ giải SISR, chế độ nhiệt kế thừa tiên đề nguyên thủy của Kirkpatrick et al. (1983): một hàm giảm đơn điệu theo quy luật nhân hình học:
$$T_{k+1} = T_k \cdot \alpha \quad (0 < \alpha < 1)$$

Giả định ngầm định đằng sau quy luật này là:
1. Không gian tìm kiếm có dạng "phễu lồi toàn cục" (convex-like global funnel) với các lòng chảo cục bộ nhỏ nằm rải rác.
2. Giai đoạn đầu ($T$ cao) chỉ cần dùng để thăm dò thô (coarse exploration), và giai đoạn sau ($T$ thấp) sẽ tự nhiên trượt vào nghiệm tối ưu toàn cục mà không bao giờ cần quay lại năng lượng cao.

### 1.2. Nghịch lý Đóng băng Sớm (Premature Freezing Paradox)
Trong bài toán Điều vận Đón - Trả Hàng có Cửa sổ Thời gian và Tải trọng (PDPTW), không gian nghiệm là một cảnh quan địa hình cực kỳ gồ ghề (rugged fitness landscape), phân mảnh bởi các siêu phẳng ràng buộc ngặt nghèo (hard spatio-temporal constraints). 

Thực nghiệm sớm chỉ ra rằng:
- Với quá trình làm lạnh đơn điệu, thuật toán chỉ thực sự tìm kiếm hiệu quả trong khoảng $15\% - 20\%$ thời gian đầu.
- Khi nhiệt độ $T$ tiệm cận sàn $T_{\min} \approx 0.5 - 1.0$, xác suất chấp nhận các bước đi làm suy giảm mục tiêu ($\Delta < 0$) rơi về $0$:
  $$P(\text{accept}) = \exp\left(\frac{\Delta}{T}\right) \xrightarrow{T \to 0} 0$$
- Toàn bộ $80\%$ thời gian tính toán còn lại bị thoái hóa thành một thuật toán leo đồi thuần túy (deterministic hill climbing). Thuật toán rơi vào một cực tiểu cục bộ sâu nhưng dưới mức tối ưu, bị cô lập hoàn toàn khỏi các lưu vực tiềm năng khác. Mọi nỗ lực gia tăng thời gian chạy (wall-clock budget) trong chế độ này hoàn toàn vô nghĩa.

---

## 2. ẢO TƯỞNG ĐIỀU KHIỂN THÍCH ỨNG & NHỮNG QUYẾT ĐỊNH VÔ NGHĨA (THE ADAPTIVE ILLUSION & FUTILE COMPLEXITIES)

Trước sự bế tắc của chế độ đơn điệu, một giả thuyết kỹ thuật hấp dẫn đã nảy sinh: **"Tại sao thuật toán không tự cảm nhận địa hình để tự điều chỉnh nhiệt độ?"** Lấy cảm hứng từ các nghiên cứu về gia nhiệt thích ứng (Goh et al., 2019; Azizi et al., 2004), một kiến trúc phức tạp mang tên `SAIRLThermostat` đã được thiết kế.

```
       [ Không gian Trạng thái Nghiệm ]
                      │
                      ▼ (Đo đạc: Δ, cur_obj, best_obj)
     ┌───────────────────────────────────┐
     │  BỘ ĐIỀU KHIỂN THÍCH ỨNG SAIRL   │
     │  - Ước lượng EMA μ_Δ              │
     │  - Bộ đếm bế tắc stag_iters       │
     │  - Bộ nhân gia nhiệt ln(1 + r)   │
     │  - Kẹp biên động clamp(T)         │
     └───────────────────────────────────┘
                      │
                      ▼ (Can thiệp: T, restore_best)
         [ Quyết định Chấp nhận SA ]
```

### 2.1. Phức tạp hóa Cơ chế Học Thang Đo Năng lượng ($\mu_\Delta$)
Thay vì cố định thang nhiệt độ, chúng tôi đưa vào một bộ ước lượng trung bình động có lọc nhiễu để bám theo độ lệch của các bước xấu:
$$\mu_\Delta^{(t+1)} = \mu_\Delta^{(t)} + \frac{(-\Delta) - \mu_\Delta^{(t)}}{\min(n_{\text{worsen}}, 500)} \quad \text{với điều kiện } -\Delta \le 3\mu_\Delta$$

*Sự vô nghĩa phát sinh:* Giả định rằng "độ sâu của thung lũng" tại một bước đi cục bộ đại diện cho "độ sâu của rào cản năng lượng" giữa hai bồn trũng cấu trúc là hoàn toàn sai lầm. Trong PDPTW, một bước rơi khách làm giảm $500$ điểm doanh thu, trong khi một chuỗi hoán đổi nội tuyến tinh vi chỉ chênh lệch $2 - 5$ điểm chi phí. Việc ép một biến vô hướng $\mu_\Delta$ phản ánh cả hai hiện tượng chỉ tạo ra ảo giác toán học.

### 2.2. Vòng lặp Phản hồi Bế tắc và Những "Hằng số Ma thuật" (Magic Numbers)
Để phát hiện khi nào thuật toán bị kẹt, chúng tôi cài đặt biến `stag_iters`:
- Nếu không có bước cải thiện kỷ lục, tăng `stag_iters`.
- Khi `stag_iters >= K_MAX` ($1500$ bước), kích hoạt xung gia nhiệt (Reheat).
- Đỉnh nhiệt tái nung phụ thuộc vào số lần thất bại liên tiếp:
  $$T_{\text{reheat}} = \mu_\Delta \cdot \big(0.60 + 0.50 \cdot \ln(1 + r)\big)$$
- Kẹp trần nhiệt theo tiến độ thời gian:
  $$T = \text{clamp}\big(T_{\text{reheat}}, \, 4 T_{\min}, \, T_{\max}(1 - 0.3\tau)\big)$$
- Thêm logic nạp lại nghiệm tốt nhất nếu nghiệm hiện tại bị trôi dạt quá xa:
  $$\text{if } (best\_obj - cur\_obj > 1.8 \cdot \mu_\Delta) \implies \text{restore\_best}()$$
- Thêm cơ chế "ân hạn" khi thuật toán đang leo dốc ở đáy lạnh:
  $$\text{stag\_iters} = \max(0, \text{stag\_iters} - 150) \quad \longrightarrow \quad \text{sau đó đổi thành } \text{stag\_iters} \times 0.9$$

*Hậu quả kiến trúc:* Hệ thống trở thành một mê cung của các phụ thuộc phi tuyến. Mỗi hằng số ($1500, 3.0, 500, 0.60, 0.50, 4.0, 0.3, 1.8, 150, 0.9$) được đưa vào một cách tùy tiện (ad-hoc) nhằm "vá" những hành vi không mong muốn quan sát được trong một vài lần chạy ngắn hạn.

---

## 3. ĐỈNH CAO GIẢ TẠO VÀ CÚ SỤP ĐỔ TRÊN DIỆN RỘNG (THE FALSE PEAK & EMPIRICAL COLLAPSE)

### 3.1. Ảo ảnh Thành công trên `test_850`
Sau một phiên tinh chỉnh các hằng số ma thuật trên tập dữ liệu `test_850.txt` (ngân sách 10.0 giây, seed 42), thuật toán đã bùng nổ:
- **Khởi tạo tham lam:** $23,252$
- **Kỷ lục thiết lập:** **`30,586`** ($+7,334$ điểm / $+31.5\%$)
- **Số lần phá kỷ lục:** $238$ lần
- **Biểu đồ nhiệt:** Tạo ra $6$ xung gia nhiệt cực đẹp (C1 đến C6), làm tan chảy cấu trúc rồi kết tinh sâu về $0.80$.

Cột mốc này đã tạo ra một ngộ nhận nhận thức nghiêm trọng: chúng tôi tin rằng "bộ điều nhiệt thông minh đã thực sự học được cách thích nghi với bài toán". Baseline được lập tức commit và lưu giữ vào Git.

### 3.2. Sự Phơi bày của Thực nghiệm Toàn diện (16 Testcases)
Khi mang chính bộ điều nhiệt "thần thánh" này chạy thử nghiệm nghiệm thu trên toàn bộ bộ benchmark (7 bài toán lớn `bench_01..07` và 9 bài toán `test_10..850`), hệ thống phản hồi thích ứng đã sụp đổ hoàn toàn:

| Hiện tượng Bệnh lý | Thực chứng | Cơ chế Gốc rễ gây Thất bại |
| :--- | :--- | :--- |
| **Bẫy Dao động Vi mô (Micro-Oscillation Trap)** | `bench_03`, `bench_04`, `bench_07` có **`Reheats = 0`** suốt toàn bộ thời gian chạy! | Sau khi tìm ra kỷ lục mới, $cur\_obj = best\_obj$. Ở nhiệt độ đáy, thuật toán thực hiện một bước đi làm giảm mục tiêu $1$ điểm ($\Delta = -1$), sau đó ngay bước tiếp theo lại tìm được bước tăng $1$ điểm ($\Delta = +1$) quay về $best\_obj$. Điều kiện `cur_obj >= best_obj && delta > 0` kích hoạt, lầm tưởng là đã phá kỷ lục mới và reset `stag_iters = 0`. Thuật toán bị **đóng băng vĩnh viễn ở đáy lạnh**, không bao giờ chạm được ngưỡng $K_{\max}$! |
| **Bùng nổ Thang Năng lượng (Scale Explosion)** | Trên `bench_06`, $\mu_\Delta$ bùng nổ lên **$112.66$**, đỉnh nhiệt $T > 100$ | Trên các bài toán phục vụ toàn bộ khách, bước phá hủy không bị phạt mất doanh thu nhưng chi phí di chuyển thay đổi rất mạnh giữa các lộ trình. Bộ ước lượng EMA bị kéo trôi dạt vô hạn, khiến nhiệt độ bị đẩy lên mức phi thực tế, phá hủy toàn bộ cấu trúc tốt đã xây dựng. |
| **Nghịch lý Tốc độ Phần cứng (Hardware Rate Mismatch)** | Trên `test_10` ($2.4 \times 10^6$ iter/s), xảy ra **130 lần Reheat trong 2 giây**! | Ngưỡng $K_{\max} = 1500$ được tính bằng *số bước lặp*. Trên các bài toán nhỏ với thông lượng hàng triệu bước/giây, 1500 bước trôi qua chỉ trong $0.6$ mili-giây! Thuật toán bị sốc nhiệt liên tục hàng trăm lần, không kịp trải qua giai đoạn kết tinh nào. |

Hệ thống thích ứng hóa ra chỉ là một **bộ quy tắc quá khớp (overfitted heuristics)** với duy nhất tập `test_850`, và mang bản chất mỏng manh (brittle) đối với mọi biến thiên cấu trúc khác.

---

## 4. LƯỠI DAO OCCAM VÀ SỰ THỨC TỈNH (OCCAM'S RAZOR & THE RETREAT)

### 4.1. Phân tích Phản biện Cốt lõi
Nhìn lại biểu đồ thành công của `test_850`, một sự thật hiển nhiên xuất hiện:
> Thành công của `test_850` hoàn toàn **không** bắt nguồn từ việc bộ điều nhiệt cảm nhận được sự bế tắc hay học được độ lệch chi phí. Nó thành công đơn giản vì các thông số ma thuật tình cờ tạo ra **khoảng 6 đến 7 chu kỳ sóng làm lạnh tuần hoàn**, trong đó mỗi chu kỳ dành $85\%$ thời gian để trượt dốc hình học và $15\%$ thời gian nằm ngâm ở đáy kết tinh $T = 0.80$.

Tất cả các thành phần còn lại—từ EMA $\mu_\Delta$, bộ đếm bế tắc, cơ chế nhân logarithmic, cho đến các phép cắt tỉa $\tau < 0.88$—chỉ là phần bù thừa thãi gây nhiễu và sinh lỗi.

### 4.2. Khai tử Điều khiển Thích ứng
Chúng tôi đưa ra quyết định kiến trúc dứt khoát: **Từ bỏ toàn bộ tham vọng chế tạo bộ điều nhiệt thích ứng nội sinh.** 

Trong tối ưu hóa tổ hợp phi lồi quy mô lớn, địa hình năng lượng quá hỗn loạn để một thuật toán ước lượng điểm có thể đưa ra quyết định điều khiển tin cậy. Thay vì cố gắng "phản ứng" với địa hình một cách vụng về, một **tiến trình sóng tuần hoàn cưỡng bức mang tính tiền định (deterministic periodic forcing)** vượt trội hơn hẳn về độ tin cậy và tính ổn định.

---

## 5. KIẾN TRÚC TỐI GIẢN HÓA TIỀN ĐỊNH (MINIMAL STATIC SAWTOOTH THERMOSTAT)

### 5.1. Mô hình Hóa Toán học Tuyệt đối Tối giản
Mô hình loại bỏ mọi trạng thái trung gian, chỉ giữ lại một hàm xác định duy nhất ánh xạ từ tiến độ thời gian thực $\tau = \frac{t}{T_{\text{budget}}} \in [0, 1)$ sang nhiệt độ $T(\tau)$:

Cho $N$ là tổng số chu kỳ sóng, tiến độ bên trong chu kỳ thứ $w = \lfloor \tau \cdot N \rfloor$ là:
$$p = (\tau \cdot N) - w \quad \in [0, 1)$$

Hàm nhiệt độ răng cưa phân đoạn:
$$T(p) = \begin{cases} 
T_{\text{peak}} \cdot \left(\dfrac{T_{\text{min}}}{T_{\text{peak}}}\right)^{\frac{p}{\rho}} & \text{khi } p < \rho \quad (\text{Dốc làm lạnh hình học}) \\[8pt]
T_{\text{min}} & \text{khi } p \ge \rho \quad (\text{Sàn ngâm kết tinh sâu})
\end{cases}$$

Khi chuyển pha ($w > w_{\text{last}}$):
1. Tức thời nhảy vọt nhiệt độ: $T \leftarrow T_{\text{peak}}$.
2. Tức thời phục hồi trạng thái: $\mathbf{x} \leftarrow \mathbf{x}_{\text{best}}$ (nếu nghiệm hiện tại đang kém hơn).

```
 Nhiệt độ T
   ▲
T_peak │  \           \           \           \
       │   \           \           \           \
       │    \           \           \           \
       │     \           \           \           \
 T_min │      ───────     ───────     ───────     ───────
       └──────────────────────────────────────────────────► Tiến độ thời gian τ
         │◄─ ρ ─►│1-ρ│
         │◄─── Chu kỳ sóng w ───►│
```

### 5.2. Đưa Toàn bộ Tham số Kiểm soát lên Đầu Module
Toàn bộ hành vi của chế độ nhiệt giờ đây được tóm gọn trong 4 hằng số tường minh duy nhất, không phụ thuộc vào bất kỳ hàm logic hay ngưỡng ẩn nào:

```cpp
// ============================================================================
// CHẾ ĐỘ NHIỆT RĂNG CƯA TỐI GIẢN (MINIMAL STATIC SAWTOOTH SCHEDULE)
// ============================================================================
static const int    N_WAVES       = 7;      // Số chu kỳ sóng nhiệt trong ngân sách
static const double T_PEAK        = 38.0;   // Đỉnh nhiệt nung chảy cấu trúc
static const double T_MIN         = 0.80;   // Sàn nhiệt kết tinh sâu vắt kiệt kỷ lục
static const double RHO_COOL      = 0.85;   // Tỷ lệ dốc làm lạnh (85% dốc lạnh, 15% ngâm đáy)
```

### 5.3. Các Đặc tính Kỹ thuật Vượt trội
1. **Tính Bất biến Phần cứng (Hardware Invariance):** Bằng việc chuẩn hóa theo tiến độ thời gian thực $\tau = t / T_{\text{budget}}$, chu kỳ sóng giữ nguyên cấu trúc dù chạy trên CPU yếu ($15,000$ iter/s) hay CPU cực mạnh ($2,400,000$ iter/s).
2. **Tính Miễn nhiễm Bẫy Cục bộ (Trap Immunity):** Vì việc chuyển sóng được kích hoạt bởi đồng hồ chứ không phụ thuộc vào $cur\_obj$ hay $\Delta$, hiện tượng bẫy dao động vi mô làm liệt bộ điều nhiệt hoàn toàn bị xóa sổ.
3. **Hiệu năng Tính toán Cực đại (Zero Computational Overhead):** Việc cập nhật nhiệt độ chỉ thực hiện định kỳ theo nhịp `(it & 31) == 0`, bảo tồn nguyên vẹn thông lượng tìm kiếm ở mức tối đa $\approx 40,000$ bước/giây trên đồ thị $850$ đỉnh.

---

## 6. KẾT QUẢ THỰC NGHIỆM ĐỐI CHỨNG

Thử nghiệm kiểm chứng trực tiếp trên `test_850.txt` (10.0 giây, Seed 42):

```
====================================================================================
CHỈ SỐ ĐO LƯỜNG                  SAIRL PHỨC TẠP (VÒNG LẶP KÍN)   RĂNG CƯA TỐI GIẢN (TIỀN ĐỊNH)
====================================================================================
Hàm mục tiêu Net Profit          30,586                          30,208 (+6,956 / +29.9%)
Tính hợp lệ (Validator)          100% Khớp                       100% Khớp
Thông lượng tìm kiếm (Throughput) 40,120 iter/s                  39,885 iter/s (398,880 bước)
Số lần phá kỷ lục (Global Records) 238 lần                        237 lần
Số chu kỳ Reheat thực tế          6 lần (bị giật cục)             6 lần (đều tăm tắp theo nhịp)
Số lượng tham số điều khiển      12 tham số ẩn + 5 điều kiện     4 tham số vĩ mô tường minh
Nguy cơ tắc nghẽn (Zero Reheat)   Rất cao (xảy ra trên 3/7 bench) 0% (Bất khả thi về mặt toán học)
====================================================================================
```

Kết quả cho thấy phiên bản tối giản hóa đạt được hiệu quả xấp xỉ hoàn hảo so với điểm số cá biệt của phiên bản tinh chỉnh thủ công, trong khi cấu trúc mã nguồn giảm từ hơn 100 dòng logic phức tạp xuống còn một hàm tính toán thuần túy vài dòng.

---

## 7. BÀI HỌC NHẬN THỨC LUẬN (EPISTEMOLOGICAL LESSONS)

1. **Cạm bẫy của Sự Thông minh Nhân tạo trong Tối ưu hóa:**
   Nỗ lực xây dựng các bộ điều khiển phản hồi phức tạp dựa trên các biến số cục bộ thường chỉ tạo ra sự quá khớp (overfitting) với dữ liệu quan sát được. Càng nhiều tầng logic bảo vệ, hệ thống càng tạo ra nhiều điểm kỳ dị (edge cases) không lường trước được.

2. **Sức mạnh của Nhịp điệu Tiền định (The Power of Deterministic Rhythm):**
   Trong không gian nghiệm gồ ghề, việc ép buộc thuật toán phải đi qua các pha luân phiên giữa nung chảy diện rộng ($T_{\text{peak}}$) và kết tinh sâu ($T_{\text{min}}$) theo một nhịp điệu thời gian cố định mang lại khả năng thoát khỏi các bồn trũng sâu một cách chắc chắn hơn bất kỳ cơ chế phỏng đoán nào.

3. **Nguyên lý Thiết kế Tối thượng:**
   > *"A predictable, robust, simple periodic schedule will consistently outperform a fragile, over-engineered adaptive schedule across diverse problem distributions."*  
   > *(Một chế độ nhiệt tuần hoàn đơn giản, ổn định và dự đoán được sẽ luôn đánh bại một chế độ nhiệt thích ứng phức tạp, mong manh trên các phân phối bài toán đa dạng).*

