# KẾ HOẠCH THIẾT KẾ THỰC NGHIỆM GM-SISR — BẢN v2 (OPERATOR-FIRST)

> **Tài liệu nghiên cứu**: SOICT 2026 - Selective/Prize-Collecting PDPTW
> **Target Solver**: [`experiments/gm_sisr/gm_sisr.cpp`](file:///z:/Desktop/250926/experiments/gm_sisr/gm_sisr.cpp)
> **Cơ sở thực nghiệm**: `output/latest/test_850/report.txt` (60s, 831,392 iters, 13,856 it/s, accept 38.67%, **275 global records**, **5 chu kỳ**, `T0=62.97 / Twork=11.45 / Tmin=1.74`).
> **Optimum tham chiếu**: `testcases/legacy_perturbation/opt_850.txt = 32012`.

---

## 0. NGUYÊN TẮC CHỈ ĐẠO (thay đổi so với v1)

1. **Toán tử là biến độc lập, nhiệt là hệ quả.** Chấp nhận Metropolis chỉ có tác dụng nếu *toán tử sinh ra* bước nhảy lớn. Ruin chuỗi ngắn + recreate tham lam gần như **idempotent** ⇒ Δ nội tại nhỏ ⇒ nâng nhiệt chỉ tạo random-walk. Vì vậy **thi công toán tử trước**, khớp nhiệt sau.
2. **Một biến nhiệt duy nhất điều khiển tất cả.** Thay vì 5 chế độ nhiệt riêng + "handshake protocol", mọi toán tử co giãn theo một đại lượng chuẩn hoá:
   `h = log(T/T_min) / log(T_peak/T_min) ∈ [0,1]` (hàm `AdaptiveThermostat::heat_of`). Nóng ⇒ khám phá; lạnh ⇒ tham lam.
3. **Chế độ nhiệt nguyên sơ = một chu kỳ hàm mũ.** Fallback khi thiết kế nhiệt riêng quá khó: răng cưa mà **mỗi răng là một hàm mũ đơn điệu**, đỉnh **không suy giảm**, **không ngâm đáy**.
4. **Đo trên bộ test đa dạng, so với optimum/best-known**, seed ghép cặp — không tune trên một instance.
5. **Không mặc định tin vào "ghép nhiệt"** *(bổ sung sau ablation vòng 1)*. Kết quả `test_850` 60s×10 seed: ghép nhiệt H2 làm hại **+234**, Basin Kick H3 làm hại **+216** (ngoài CI ±126). Tham số tĩnh đã tune là cực trị mạnh. Mọi thay đổi tham số/toán tử phải **chứng minh dương bằng ablation trước** khi vào default.
6. **Tỉ lệ no-op là chỉ số hạng nhất** *(bổ sung sau phân tích cấu trúc §8)*. Đo được: **26–35% số vòng lặp và 71–95% số move được chấp nhận là `Δ ≡ 0`** — tức ruin bị repair greedy hoàn tác chính xác. Một thay đổi toán tử chỉ được coi là có ý nghĩa về cấu trúc **nếu nó kéo tỉ lệ no-op xuống**. Throughput (it/s) không phải chỉ số; accept-rate không phải chỉ số.
7. **Không chọn champion trên một instance.** *(bổ sung §8.6)* Mask `896` được chọn bằng `test_850` ⇒ bản thân champion là back-fit.
8. **Ghép cặp theo seed là CHƯA ĐỦ — phải đan xen theo thời gian** *(bổ sung §10.3–10.4)*. Chạy từng mask theo *khối thời gian* để trôi máy (nhiệt/nền) lọt vào so sánh: đo được throughput rơi **25% trong 3 phút** với một mask và **41%** với mask khác, và objective bám gần như đơn điệu theo số vòng lặp. Từ nay mọi so sánh nhiều phiên bản phải **chạy đồng thời trên cùng seed** (hoặc round-robin).
9. **Mask là NHÃN TẤT ĐỊNH, không suy từ dấu thời gian.** Hệ quả của #8: dùng `report.txt` (có `STRUCT`, có `Seed`) và ghi nhãn mask ngay khi chạy; cấm suy mask từ thứ tự/timestamp (§10.1 dùng cách suy — chỉ chấp nhận vì có kiểm chứng chéo bằng `H1 Soft-Inserts`).
10. **"CHỈ THÊM move, KHÔNG ĐỔI move"** *(bổ sung §11.5)*. Thêm toán tử vào tập move ⇒ lân cận là **siêu tập** ⇒ trần chất lượng không giảm, rủi ro chỉ còn ở throughput. **Đổi** công thức đang có (thermal, reheat, ordering) có thể làm tệ đi — và toàn bộ chiến dịch v2 chính là chuỗi "đổi", rồi không phân biệt nổi với nhiễu. Mọi thay đổi mới phải là **một nhánh move THÊM**, sau cổng kích hoạt S13.
11. **Chỉ nhận hướng có TUYÊN BỐ LOẠI B** *(bổ sung §11.2)*: siêu tập lân cận, hoặc bảo đảm đơn điệu, hoặc bất biến kiểm tra được — không nhận "nó tăng điểm trên instance Y". Nghiệm thu bằng **property test T1–T6** (§11.6), không bằng benchmark nhiều seed.

### Số liệu neo (đã kiểm chứng)
| Chỉ số | Giá trị |
|---|---|
| Initial greedy → Final | 23,252 → **30,832** (verified) |
| Optimum 850 | **32,012** ⇒ **gap 1,180 (3.7%)** |
| Throughput | 13,856 it/s, accept 38.67% |
| Yield toán tử (records/calls) | Mode1 0.033% · Mode2 0.031% · Mode3 0.040% |
| Chu kỳ / nhiệt | 5 chu kỳ; `T0=62.97, Twork=11.45, Tmin=1.74` |

---

## 1. NĂM HƯỚNG TOÁN TỬ (H1–H5) + GHÉP NHIỆT TƯƠNG ỨNG

| # | Toán tử | Điểm chạm code | Bit (TẮT) | Phán quyết vòng 1 |
|---|---|---|---|---|
| **H1** | **Soft-Deficit Recreate** — nới cổng chèn `Rev[q]-best1 >= -ε` | `recreate_with_memory` | `128` | 🟡 giữ, chờ chốt ≥30 seed |
| **H2** | **Adaptive Ruin Scale + Blink** — `k_s, L_max, BLINK_MASK` theo `h` | `ruin_guided`, `thermal_step` | `256` | ❌ **BÁC BỎ** (tắt đi +234) |
| **H3** | **Basin Kick / Full-Route Ruin** | `basin_kick` + `pending_kick` | `512` | ❌ **BÁC BỎ** (tắt đi +216) |
| **H4** | **Sequential Pairwise Recreate** | `best_insert` (mở rộng) | `1024` | ⬜ chưa cài |
| **H5** | **Bandit chọn toán tử** (yield-based) | `ruin_guided` + `update_bandit` | `2048` | 🔧 đã vá, đang đo lại |

**Ánh xạ ghép nhiệt — BẢN ĐÃ SỬA THEO SỐ LIỆU vòng 1:**

| Toán tử | Công thức theo `h` | Trạng thái |
|---|---|---|
| H1 `ε` | `0.08·S_scale·h` (kèm jitter), `ε→0` khi lạnh | 🟡 giữ, chờ chốt ≥30 seed (hiệu ứng −40, trong nhiễu) |
| ~~H2 `BLINK_MASK`~~ | ~~`3 + round(60(1-h))`~~ | ❌ **BỎ** — tắt đi lợi +234; giữ hằng số 31 |
| ~~H2 `C_AVG_DYN`~~ | ~~`6 + 8h`~~ | ❌ **BỎ** — giữ hằng số 10 |
| ~~H2 `L_MAX_DYN`~~ | ~~`8 + 8h`~~ | ❌ **BỎ** — giữ hằng số 10 |
| H3 kích hoạt | `h ≥ 0.65` **hoặc** `stagnation ≥ 1` | ❌ **BỎ** — tắt đi lợi +216 |
| H5 trọng số | **yield-based**: `w_m = PRIOR_m·clamp(0.5+0.5·y_m/ȳ, 0.5, 2.0)`, `y_m=(records_m+1)/(tries_m+200)`, cập nhật mỗi 20k iter | 🔧 đã thay bản luỹ thừa bão hoà, đang đo |

**Bài học rút ra (ghi vào plan để không lặp lại sai lầm):**
> "Ghép nhiệt cho toán tử" **không tự động tốt**. Tham số tĩnh đã tune (blink 1/32, `C_AVG=10`, `L_MAX=10`) là cực trị cục bộ mạnh; kéo nó co giãn theo `T` phá vỡ cân bằng accept-rate/step-size. Muốn vi phạm quy tắc này phải có bằng chứng ablation dương trước, không đưa thẳng vào default.

---

## 2. CHẾ ĐỘ NHIỆT

### 2.1. Mặc định (mới): "exponential-tooth" (răng cưa hàm mũ, đỉnh hằng)
- Mỗi chu kỳ: `T = cycle_peak_T · (cycle_base_T / cycle_peak_T)^{φ_c}` — giảm mũ đơn điệu, **không có pha ngâm đáy 55%**.
- `cycle_peak_T = T_peak_base` (**không suy giảm** theo thời gian) — khắc phục "nghịch lý suy giảm đỉnh".
- Dwell co rút thích ứng: `duration = base_span · 0.12 / (1 + 0.7·min(3, stag))` ⇒ khi bế tắc, chu kỳ ngắn → tái nung dày hơn.
- Khởi tạo từ `auto_calibrate`: `T_peak_base = 0.65·T_0`, `T_min_base = T_min = micro_delta/6.907`.

### 2.2. Fallback "nguyên sơ 1 chu kỳ hàm mũ"
- Chính là 2.1 với đúng **một** răng phủ toàn bộ ngân sách (tắt tái nung) — dùng khi H3 không triển khai hoặc khi muốn baseline tối giản.

### 2.3. Legacy để đối chứng: `ABLATION_MASK & 4096`
- Ép quay về **SEDCA-V3 U-valley** (đỉnh suy giảm, dwell 55%) → dùng làm baseline nhiệt trong ablation.

### 2.4. Tái hiệu chuẩn trực tuyến (T4, bit `8192` để tắt)
- Vòng đệm 128 delta worsening gần nhất; mỗi 20k iter gọi `online_recalibrate` cập nhật `micro_delta/T_min/T_work/T_0/T_peak_base`. Khắc phục việc `micro_delta` bị đóng băng ở vùng `obj≈23k`.

---

## 3. MA TRẬN ABLATION CẬP NHẬT

| Bit | Ý nghĩa (khi BẬT = TẮT thành phần) | Thành phần |
|:--:|---|---|
| 1 | tắt regret trong `order_candidates` | legacy |
| 2 | tắt Mode 2 (Unserved-Cavity Ruin) | legacy |
| 4 | tắt Mode 3 (Slack-Bottleneck Ruin) | legacy |
| 8 | dùng soft-drift thay hard reset | legacy |
| 16 | seed tournament thay uniform | legacy |
| 32 | thermostat sawtooth tĩnh | legacy |
| 64 | **scale/blink tĩnh** (`1/32`, `C_AVG=10`, `L_MAX=10`) | H2 off |
| **128** | **tắt Soft-Deficit** | H1 off |
| **256** | **tắt ghép nhiệt toán tử** (H2 off) | H2 off |
| **512** | **tắt Basin Kick** | H3 off |
| **1024** | (dự phòng) tắt Pairwise | H4 off |
| **2048** | **tắt bandit** (về 75/15/10 cứng) | H5 off |
| **4096** | **quay về SEDCA-V3 U-valley** | thermal legacy |
| **8192** | **tắt tái hiệu chuẩn trực tuyến** | T4 off |

---

## 4. CHỈ SỐ ĐO (đã chỉnh — bất biến hoán vị)

- **Chính**: objective cuối, **gap tới best-known/optimum** (850 → 32,012), trên **≥8 instance** (`bench_01..07`, `ladder_03`, `test_500`, `test_850`).
- **Records sau 14s** (kiểm chứng "45s còn lại có vô ích không").
- **Phổ Δ nhận được** theo bucket (đo `soft_inserts`, tỉ lệ Δ∈[-40,-5]).
- **Yield toán tử**: `mode_records/mode_tries` mỗi mode (đã có telemetry) + `kick_count`, `kick_records`, `soft_inserts`.
- **Diversity bất biến hoán vị** (thay cho Hamming thô của v1): khoảng cách gán request→xe hoặc Jaccard trên tập cặp kề `(D_a→P_b)`.
- **Throughput** it/s (chặn hồi quy H4).

---

## 5. TIẾN ĐỘ TRIỂN KHAI (cập nhật liên tục)

| Hạng mục | Mô tả | Trạng thái |
|---|---|---|
| Globals toán tử | `C_AVG_DYN`, `L_MAX_DYN`, `SOFT_EPS0`, `pending_kick`, `bandit_w`, `sedca_heat`, telemetry mới | ✅ Xong |
| H1 Soft-Deficit | Cổng chèn `> -ε` trong `recreate_with_memory` | ✅ Xong |
| H3 Basin Kick | `basin_kick()` + `pending_kick` | ✅ Xong → ❌ **bị bác bỏ bằng ablation** |
| H5 Bandit | roulette chọn mode | ✅ Xong → 🔧 **vá sang yield-based** (`update_bandit`) sau khi bản luỹ thừa bão hoà |
| H2 Ghép nhiệt | `thermal_step()` cập nhật blink/scale theo `h` | ✅ Xong → ❌ **bị bác bỏ bằng ablation** |
| Thermal mới | nhánh exponential-tooth | ✅ Xong (trung tính vs legacy) |
| `heat_of` + T4 | `heat_of`, `online_recalibrate` | ✅ Xong |
| Nối main loop | `thermal_step`, `pending_kick`, `sedca.stag`, recalibration, ring delta | ✅ Xong |
| Report v2 | SEDCA-V4, Mode 4, `soft_inserts`, `kick_count/kick_records`, bandit w | ✅ Xong |
| **Build + smoke test** | `g++ -O3 -march=native -std=c++17`; chạy `test_850` 20s ⇒ `obj=29995` | ✅ **Xong** |
| **Fix blocker loader** | g++=UCRT64 nhưng DLL copy từ mingw64 ⇒ `0xC0000139`. Dùng DLL `/c/msys64/ucrt64/bin/` | ✅ **Xong** |
| **Ablation vòng 1** | 6 mask × 10 seed × 60s trên `test_850` | ✅ **Xong** (kết luận ở 6.2) |
| **Ablation vòng 2** | 7 tổ hợp × 10 seed × 60s, bản v2.1 | ✅ **Xong** (kết luận ở 6.3) |
| **Default = champion** | `ABLATION_MASK` mặc định = `256|512` (H2/H3 gỡ) | ✅ **Xong** |
| Chốt 24 seed | `768` / `4864` / `2816` / `2944` | ✅ **Xong** (vòng 3): H5 +421, tooth +116 |
| Chốt 30 seed H1 | `768` vs `896` | ✅ **Xong** (vòng 4) + meta-analysis gộp 5 phép đo ⇒ E(H1)≈0 |
| **Multi-instance Phase A (BKS)** | 10 instance × 3 seed × 180s | ✅ **Xong** (§7.1) |
| Multi-instance Phase B | `768` vs `896` × 10 instance × 6 seed × 60s | ✅ **Xong** — H1 thua 6/10 ⇒ **gỡ H1** (§7.2) |
| **Default = champion cuối** | `ABLATION_MASK` mặc định = `128|256|512` = **896** | ✅ **Xong** |
| **A/B cuối** | champion `896` vs legacy SEDCA-V3 `4992`, 10 instance × 6 seed × 60s | ✅ **Xong** — **HOÀ 5/10, chênh 0.002%** (§7.4) |
| **Kiểm định H5 đa instance** | `896` vs `2944`, 10 instance × 6 seed × 60s | ✅ **Xong** — hoà 5/10 ⇒ H5 cũng không tổng quát (§7.6) |
| **Telemetry cấu trúc** | đếm `Δ=0` no-op / ruin-empty / downhill vào report | ✅ **Xong** (§8.0) |
| **Phân tích điểm nghẽn** | kiểm toán giả định + B1..B7 + hướng mới D1..D7 | ✅ **Xong** (§8) |
| H4 Pairwise | (dự phòng) mở rộng `best_insert` | ⬜ Chờ — chỉ đáng làm nếu chứng minh >3%/instance |
| **Công cụ ghép cặp** | `analyze_paired.py` — hiệu số theo seed + sign-test | ✅ **Xong** |
| **Sweep baseline `test_850`** | 6 mask {768,896,4864,4992,2944,7040} × 18 seed × 60s (ghép cặp) | 🔄 **Đang chạy** (`output/t850_base/`) |
| **Registry hướng nghi ngờ** | §9: nhóm V (kiểm chứng) / S (cấu trúc) / P (protocol) + danh sách ĐÓNG | ✅ **Xong** |
| **Sweep khối `test_850`** | 6 mask × 18 seed × 60s, ghép cặp theo seed | ✅ **Xong** → dùng để *chẩn đoán trôi*, không để xếp hạng (§10) |
| **No-op theo mask** | `analyze_noop.py` gom `STRUCT` từ 108 `report.txt` | ✅ **Xong** — no-op **37.1–37.6% cho cả 6 mask** (§10.3) |
| **Xác thực nền V2/V3** | `validator.py` vs `opt_850.txt` và `result.txt` | ✅ **Xong** — khớp tuyệt đối (32,012 / 9,722 / 29,995) |
| **Sweep ĐAN XEN** | `interleaved_bench.py`: mỗi seed chạy cả 6 mask cùng lúc | ⛔ **ĐÃ HUỶ** (7/18 seed, `output/t850_inter/`) — dừng hướng tune theo `test_850` |
| **Phân tích tổng quát + baseline** | §11: chốt baseline = mask 896; I1–I6; bảo đảm loại B; S1–S13; T1–T6; nợ kỹ thuật N1–N5 | ✅ **Xong** |
| Dọn file tạm | `_probe*`, `_t2*`, `gm_dyn*`, `gm_dbg.exe`, `gm2.exe`, `o*.txt`, `e*.txt` | ✅ Xong |

---

## 6. KẾT QUẢ

### 6.1. Ablation vòng 1 — bản v2 (bandit luỹ thừa cũ), `test_850`, 60s × 10 seeds (seed 1..10)

| Mask | Thành phần tắt | Mean | Std | Median | Min–Max | Δ vs full |
|:--:|---|---|---|---|---|---|
| **0** | *(full v2)* | 30618.7 | 211.3 | 30592 | 30316–30927 | — |
| 128 | H1 Soft-Deficit | 30578.2 | 248.5 | 30563 | 30082–30959 | −40 (nhiễu) |
| **256** | **H2 ghép nhiệt scale/blink** | **30853.0** | 270.8 | 30869 | 30439–31188 | **+234** ⚠️ |
| **512** | **H3 Basin Kick** | **30835.0** | 188.9 | 30799 | 30605–31151 | **+216** ⚠️ |
| 2048 | H5 bandit | 30549.6 | 235.5 | 30534 | 30189–30920 | −69 |
| 4096 | (quay về thermal legacy) | 30613.9 | 237.4 | 30650 | 30190–31007 | −5 (nhiễu) |

CI 95% mỗi cấu hình ≈ ±126…±150 ⇒ **chỉ hiệu ứng H2 và H3 là vượt nhiễu**.

### 6.2. PHÁN QUYẾT vòng 1 (dựa trên số liệu, không dựa trên trực giác)

| Thành phần | Phán quyết | Bằng chứng |
|---|---|---|
| **H2** — ghép nhiệt `BLINK_MASK/C_AVG/L_MAX` theo `h` | ❌ **BÁC BỎ** | Tắt đi lợi **+234** (≈3.4× CI). Bộ tĩnh đã tối ưu (`BLINK=31`, `C_AVG=10`, `L_MAX=10`) tốt hơn co giãn. |
| **H3** — Basin Kick (~20% mạng lưới) | ❌ **BÁC BỎ** | Tắt đi lợi **+216**. Mất cấu trúc quá lớn, không thu hồi được trong ngân sách chu kỳ. |
| **H5** — Bandit | ⚠️ **nghi vấn** | Tắt đi lợi **−69** (tức tắt thì TỐT hơn) — do bản bandit luỹ thừa bão hoà ở Mode 1, triệt tiêu Mode 2/3. Đã vá sang dạng yield-based, đang đo lại (vòng 2). |
| **H1** — Soft-Deficit | ⚠️ **chưa kết luận** | −40, nằm trong nhiễu. Cần ≥30 seed. |
| **Thermal tooth** vs legacy U-valley | ⚠️ **trung tính** | −5 điểm, hoàn toàn trong nhiễu ⇒ không có bằng chứng lịch nhiệt mới tốt hơn. Giữ tooth vì đơn giản hơn (fallback nguyên sơ đúng như yêu cầu). |

**Hệ quả trực kiến trúc**: giả thuyết "nâng nhiệt ⇒ cần toán tử mạnh hơn" chỉ đúng một nửa — nhưng **hướng toán tử mạnh (H2/H3) lại phản tác dụng ở dạng đã cài**. Điều này khớp với phân tích gốc: ruin+recreate gần idempotent, nên phá to/ghép nhiệt chỉ làm tăng random-walk mà không tăng *chất lượng* bước nhảy.

### 6.3. Ablation vòng 2 — bản v2.1 (bandit yield-based + H3 kích đúng ngưỡng `h≥0.65`)

`test_850`, 60s × 10 seeds. Bảng kết quả (đã xếp theo chất lượng):

| Mask | H1 | H5 | H2+H3 | Thermal | Mean | Std | Speed (it/s) |
|:--:|:--:|:--:|:--:|---|---|---|---|
| **4864** | ON | ON | **off** | legacy | **31018.4** | 203.0 | **18,427** |
| **768** | ON | ON | **off** | tooth | **31001.8** | 209.8 | 17,589 |
| 2816 | ON | off | off | tooth | 30919.9 | 128.3 | 17,542 |
| 2944 | off | off | off | tooth | 30855.2 | 98.7 | 18,118 |
| 0 | ON | ON | ON | tooth | 30476.5 | 117.5 | **11,320** |
| 128 | off | ON | ON | tooth | 30409.2 | 84.7 | **9,156** |
| 2048 | ON | off | ON | tooth | 30191.6 | 300.6 | **6,708** |

> ⚠️ Lưu ý về tên: bit = CỜ TẮT. Vậy `2944 = 128|256|512|2048` = tắt **cả H1 lẫn H5** (mọi thành phần mới đều tắt), không phải "chỉ giữ H1".

**Phân tích hiệu ứng (bộ ước lượng cộng, gốc = 2944 = mọi tính năng mới off):**

| Yếu tố | Δ so với gốc | Ý nghĩa |
|---|---|---|
| **+ H1** (2944 → 2816) | **+64.7** | H1 dương, nhất quán với Δ của `128` (−67.3) ⇒ **H1 thực sự có giá trị** |
| **+ H5** (2816 → 768) | **+81.9** | Bandit yield-based dương; nhất quán với `2048` (H5 off ở nhánh H2/H3-on: −284.9) ⇒ **H5 đã hết hại, nay có lợi** |
| **+ H2&H3** (768 → 0) | **−525.3** | **Tác hại khổng lồ** (~4× CI) |
| legacy thermal (768 → 4864) | +16.6 | Nhiễu ⇒ tooth vs legacy **trung tính** |

**Phát hiện thứ hai — H2/H3 còn phá throughput:**

| Nhóm | Speed (it/s) |
|---|---|
| Có H2+H3 (`0`, `128`, `2048`) | 6,708 – 11,320 |
| Không H2+H3 (`768`, `2816`, `2944`, `4864`) | 17,542 – 18,427 |

⇒ H2/H3 **vừa hạ chất lượng vừa mất ~40–60% tốc độ**. Nguyên nhân cơ học: `basin_kick` phải sort toàn bộ tuyến + xoá ~20% mạng lưới mỗi lần, còn `C_AVG_DYN/L_MAX_DYN` nới rộng chuỗi bị phá ⇒ recreate nặng hơn.

**Phán quyết vòng 2:**
1. **H2 và H3: gỡ khỏi default** (đưa bit 256|512 thành **giá trị mặc định của `ABLATION_MASK`**). Vẫn giữ mã để ablation tái lập.
2. **H1: giữ** (+65..+67, nhất quán qua 2 nhánh độc lập).
3. **H5: giữ bản yield-based** (+82 trên nhánh sạch, +285 trên nhánh nhiễu).
4. **Thermal tooth ≈ legacy** (+16.6, nhiễu) ⇒ **giữ tooth** vì đơn giản hơn và là "lịch nguyên sơ 1 chu kỳ hàm mũ" đúng yêu cầu.
5. **Champion tạm (`mask=768`)**: mean 31,001.8 ⇒ **gap tới 32,012 = 1,010 (3.16%)**, cải thiện so với mốc lịch sử 30,832 (gap 3.7%).

### 6.4. Vòng 3 — chốt 24 seed trên 4 ứng viên (`test_850`, 60s)

| Mask | Thành phần | Mean | Std | 95% CI | Median | Min–Max |
|:--:|---|---|---|---|---|---|
| **768** | H1+H5, H2/H3 off, **tooth** | **30877.5** | 170.4 | [30808, 30942] | 30904 | 30482–31178 |
| 4864 | H1+H5, H2/H3 off, legacy | 30761.8 | 272.0 | [30655, 30869] | 30833.5 | 30256–31301 |
| 2816 | H5 off | 30456.7 | 277.3 | [30347, 30561] | 30492.5 | 29622–30964 |
| 2944 | H1+H5 off (sàn) | 30530.9 | 324.2 | [30406, 30654] | 30464.5 | 29938–31146 |

**Phán quyết vòng 3:**
1. ✅ **H5 (bandit yield-based) dương rất mạnh: +420.8** (`768` vs `2816`). Đây là thành phần giá trị nhất trong 5 hướng.
2. ✅ **Thermal tooth THẮNG legacy: +115.7**, hai CI **không giao nhau** ([30808,30942] vs [30655,30869]) ⇒ ở 24 seed, lịch nguyên sơ một chu kỳ hàm mũ **tốt hơn** SEDCA-V3 U-valley. (Ở 10 seed chênh chỉ +16.6 nên bị coi là nhiễu — mẫu lớn đã lật lại kết luận.)
3. ❓ **H1 mất tín hiệu**: ở 24 seed, Δ(H1) = 30456.7 − 30530.9 = **−74.2** (H1 *hại*), ngược dấu với vòng 2 (+64.7) và vòng 1 (−40). ⇒ **H1 không có hiệu ứng thật**; cần vòng 4 quyết định gỡ hay giữ.
4. ➖ **H2/H3 off** đã cố định ở mọi ứng viên (không còn tranh cãi).

> Kết luận tổng hợp: chỉ **H5** và **nhiệt tooth** là hai thành phần *được chứng minh dương*. **H4 chưa cài**, **H1 treo**, **H2/H3 bị bác bỏ**.

### 6.5. Vòng 4 — quyết định H1 (30 seed, `test_850`, 60s)

| Mask | H1 | Mean | Std | 95% CI | Median |
|:--:|:--:|---|---|---|---|
| **768** | ON | **30434.9** | 237.2 | [30350, 30516] | 30464 |
| 896 | OFF | 30586.5 | 282.2 | [30485, 30685] | 30566 |

Δ(H1 ON − OFF) = **+151.6**, t ≈ 2.25 (p ≈ 0.03) — *xu hướng dương nhưng CI còn giao nhau một đoạn rất hẹp*.

**⚠️ Cảnh báo phương pháp luận phát hiện ở vòng này:** cùng `mask=768` cho mean **31001.8 (10 seed) → 30877.5 (24 seed) → 30434.9 (30 seed)**. Seed 25–30 rõ ràng bất lợi ⇒ **mọi hiệu ứng cỡ ≤150 điểm đều nằm trong biến thiên do tập seed**. Không được kết luận từ một vòng.

### 6.5b. Meta-analysis gộp cho H1 (5 phép đo độc lập)

Hiệu ứng E = (mean H1 ON) − (mean H1 OFF), gộp theo trọng số `1/SE²`:

| Vòng | So sánh | E | SE |
|---|---|---|---|
| 1 (10 seed) | `0` vs `128` | +40.5 | 103.2 |
| 2 (10 seed) | `2816` vs `2944` | +64.7 | 51.2 |
| 2 (10 seed) | `0` vs `128` | +67.3 | 45.8 |
| 3 (24 seed) | `2816` vs `2944` | −74.2 | 87.1 |
| 4 (30 seed) | `768` vs `896` | +151.6 | 67.3 |
| **Gộp** | — | **+13.2 ± 27.7** | — |

⇒ **E(H1) ≈ 0, không có ý nghĩa thống kê** (t ≈ 0.5). H1 **không chứng minh được giá trị**; dấu đảo liên tục giữa các vòng.

**Quyết định:** giữ H1 **bật** trong champion vì (a) gộp không âm, (b) chưa vòng nào cho thấy nó **hại có ý nghĩa**, (c) tắt đi làm mất tính tổng quát của bộ toán tử. **Nhưng ghi rõ trong tài liệu là "chưa chứng minh"** — nếu vòng multi-instance cho thấy nó hại, gỡ ngay.

### 6.6. Đối chứng lịch sử
| Biến thể | Instance | Obj | Gap% |
|---|---|---|---|
| Baseline SEDCA-V3 (1 seed 1607) | test_850, 60s | 30,832 | 3.7% |
| Optimum | test_850 | 32,012 | 0% |

---

## 7. PROTOCOL MULTI-INSTANCE (`multi_instance.sh`)

**Bộ instance (10):** `bench_01..07` (7), `ladder_03_V350`, `test_500`, `test_850`.

**Vấn đề:** repo **chỉ có `opt_850.txt`**. 9 instance còn lại **không có optimum/best-known công bố**.

**Cách xử lý (trung thực, không giả danh optimum):**
- **Phase A — BKS nội bộ:** `mask=768`, 180s × seed {1,2,3}, 10 instance chạy song song (1 process/instance). Lấy **max** làm `BKS_solver`.
  → Nhãn chính thức: **"best-known-of-this-solver"**, KHÔNG phải optimum. Mọi `gap%` báo cáo phải ghi rõ mẫu số là BKS nội bộ.
- **Phase B — so sánh:** `768` vs `896`, 60s × 6 seed/instance (seed ghép cặp cùng bộ seed).
- **Chỉ số:** mean/min/median objective mỗi instance; `gap% = (BKS − mean60)/BKS`; và **win-rate** (số instance mà `768` > `896`).

**Điều kiện để một hướng được coi là "thắng":** phải thắng trên **≥6/10 instance** VÀ không thua có ý nghĩa ở instance nào — nếu chỉ thắng trên `test_850` thì đó là **back-fit**, phải bác bỏ.

### 7.1. Bảng BKS nội bộ (mask=768, 180s × seed {1,2,3}, max)

> ⚠️ Đây là **best-known-of-this-solver**, KHÔNG phải optimum. Chỉ `test_850` có optimum công bố (32,012).

| Instance | BKS nội bộ | Ghi chú |
|---|---|---|
| `bench_01_tight_assignment` | 9,722 | |
| `bench_02_loose_sequencing` | 15,800 | |
| `bench_03_clustered_basins` | 13,843 | |
| `bench_04_capacity_bottleneck` | 18,053 | |
| `bench_05_asymmetric_random` | 15,689 | |
| `bench_06_asymmetric_metric` | 15,356 | |
| `bench_07_dense_scale_500` | 31,839 | instance lớn (scale 500) |
| `ladder_03_V350` | 6,204 | |
| `test_500` | 13,199 | |
| `test_850` | 31,066 | **optimum công bố = 32,012 ⇒ còn cách 946 (2.95%)** |

**Nhận xét quan trọng:** ngay cả ở 180s, `test_850` chỉ đạt 31,066 — tức **vẫn cách optimum 2.95%**, trong khi mean 60s của champion là ~30,435 (cách 4.9%). ⇒ Khoảng cách tới optimum **không chỉ là vấn đề ngân sách thời gian**; còn ~3% là **giới hạn chất lượng toán tử**. Đây là căn cứ để cân nhắc H4 (pairwise) ở giai đoạn sau — nhưng phải có bằng chứng >3%/instance mới đáng làm.

### 7.2. Phase B — `768` (H1 ON) vs `896` (H1 OFF), 60s × 6 seed/instance

| Instance | BKS | mean 768 | gap% | mean 896 | gap% | winner |
|---|---|---|---|---|---|---|
| bench_01_tight_assignment | 9,722 | 9,660.5 | 0.63 | 9,678.3 | 0.45 | 896 (+18) |
| bench_02_loose_sequencing | 15,800 | 15,729.3 | 0.45 | 15,746.2 | 0.34 | 896 (+17) |
| bench_03_clustered_basins | 13,843 | 13,859.8 | −0.12 | 13,847.7 | −0.03 | 768 (+12) |
| bench_04_capacity_bottleneck | 18,053 | 18,043.0 | 0.06 | 18,020.2 | 0.18 | 768 (+23) |
| bench_05_asymmetric_random | 15,689 | 15,677.0 | 0.08 | 15,686.7 | 0.01 | 896 (+10) |
| bench_06_asymmetric_metric | 15,356 | 15,314.2 | 0.27 | 15,348.0 | 0.05 | 896 (+34) |
| bench_07_dense_scale_500 | 31,839 | 31,690.8 | 0.47 | 31,657.3 | 0.57 | 768 (+34) |
| ladder_03_V350 | 6,204 | 6,137.0 | 1.08 | 6,141.3 | 1.01 | 896 (+4) |
| test_500 | 13,199 | 13,094.2 | 0.79 | 13,108.3 | 0.69 | 896 (+14) |
| test_850 | 31,066 | 30,452.7 | 1.97 | 30,435.0 | 2.03 | 768 (+18) |

**WIN-RATE: `896` (H1 OFF) thắng 6/10 · `768` (H1 ON) thắng 4/10 · tie 0.**

⇒ **H1 bị bác bỏ theo tiêu chí chống back-fit đã đăng ký trước** (phải thắng ≥6/10 mới được giữ; H1 ON chỉ thắng 4/10, biên độ 4–34 điểm ≈ 0.05–0.1%, tức nhiễu). **Gỡ H1.**

### 7.3. Kiến trúc cuối cùng (được chứng minh bằng ablation)

```
CHAMPION = mask 896
  ✅ H5  — Bandit chọn toán tử theo yield            (bit 2048)
  ✅ Thermal nguyên sơ: 1 chu kỳ hàm mũ, đỉnh hằng   (mặc định, bit 4096 = legacy)
  ❌ H1  — Soft-Deficit        (bit 128) — bác bỏ: E = +13 ± 28, thua 6/10 instance
  ❌ H2  — Ghép nhiệt scale/blink (bit 256) — bác bỏ: −234 điểm, −40..60% throughput
  ❌ H3  — Basin Kick          (bit 512) — bác bỏ: −216 điểm (gộp H2: −525)
  ⬜ H4  — Pairwise            (bit 1024) — chưa cài
```

**`ABLATION_MASK` mặc định đã đặt = `128|256|512` = 896** ⇒ bản build mặc định **chính là champion**.

**Cấu hình đối chứng legacy SEDCA-V3 để A/B:** `896 | 4096 = 4992` (tắt cả tính năng mới + quay về U-valley).

### 7.4. A/B cuối — champion `896` (tooth) vs legacy SEDCA-V3 tái dựng `4992` (U-valley)

`4992 = 128|256|512|4096` ⇒ tắt cả H1/H2/H3 + quay về U-valley; **H5 vẫn BẬT ở cả hai** ⇒ phép so **cô lập đúng một biến: tooth vs legacy thermal**.

| Instance | BKS | mean 896 | gap% | mean 4992 | gap% | winner |
|---|---|---|---|---|---|---|
| bench_01_tight_assignment | 9,722 | 9,667.8 | 0.56 | 9,689.2 | 0.34 | legacy +21 |
| bench_02_loose_sequencing | 15,800 | 15,745.2 | 0.35 | 15,752.3 | 0.30 | legacy +7 |
| bench_03_clustered_basins | 13,843 | 13,853.3 | −0.07 | 13,856.7 | −0.10 | legacy +3 |
| bench_04_capacity_bottleneck | 18,053 | 18,044.5 | 0.05 | 18,017.3 | 0.20 | tooth +27 |
| bench_05_asymmetric_random | 15,689 | 15,716.3 | −0.17 | 15,704.0 | −0.10 | tooth +12 |
| bench_06_asymmetric_metric | 15,356 | 15,333.2 | 0.15 | 15,316.0 | 0.26 | tooth +17 |
| bench_07_dense_scale_500 | 31,839 | 31,648.7 | 0.60 | 31,661.3 | 0.56 | legacy +13 |
| ladder_03_V350 | 6,204 | 6,135.8 | 1.10 | 6,167.8 | 0.58 | legacy +32 |
| test_500 | 13,199 | 13,118.8 | 0.61 | 13,109.2 | 0.68 | tooth +10 |
| test_850 | 31,066 | 30,564.5 | 1.61 | 30,550.3 | 1.66 | tooth +14 |

- **WIN-RATE: 5/10 – 5/10, hoà.**
- **Tổng objective: 169,828.1 (896) vs 169,824.1 (4992)** ⇒ chênh **4 điểm trên ~170k = 0.002%**.
- mean gap%: 0.479% (tooth) vs 0.438% (legacy).

### ⛔ KẾT LUẬN ÂM — phải ghi lại nguyên vẹn
> **Kết quả `test_850` "tooth tốt hơn legacy +115.7 (CI không giao)" KHÔNG tổng quát hoá.** Trên 10 instance, hai lịch nhiệt **tương đương trong sai số**. Đây là ví dụ sách giáo khoa của **back-fit vào một instance**: một hiệu ứng trông "có ý nghĩa" ở mức CI 95% trên 1 instance vẫn có thể biến mất hoàn toàn ở bộ 10 instance.
>
> ⇒ Nếu chỉ dừng ở vòng 3, chúng ta đã báo cáo một cải tiến **không tồn tại**.

### 7.5. Phán quyết tổng cho toàn bộ chiến dịch "operator-first"

| Tuyên bố | Trạng thái |
|---|---|
| H2 (ghép nhiệt scale/blink) có lợi | ❌ **SAI** — hại rõ rệt cả chất lượng (−234) và throughput (−40..60%) |
| H3 (Basin Kick) có lợi | ❌ **SAI** — hại −216, gộp với H2 hại −525 |
| H1 (Soft-Deficit) có lợi | ❌ **SAI** — E_gộp = +13 ± 28; thua 6/10 instance |
| Thermal tooth tốt hơn legacy | ⚠️ **KHÔNG tổng quát hoá** — +115.7 trên `test_850` nhưng 5/10 hoà trên 10 instance |
| H5 (bandit yield-based) có lợi | ❌ **KHÔNG tổng quát hoá** — hoà 5/10; toàn bộ +177 đến từ `test_850` (§7.6) |
| **Lợi ích thực của v2** | **Đơn giản hoá**: gỡ 3 cơ chế (H1/H2/H3) mà **không mất chất lượng**, đồng thời **tăng ~40–60% throughput** so với khi bật H2/H3. **Không có bằng chứng vượt trội về objective so với SEDCA-V3.** |

### 7.6. Kiểm định tổng quát hoá của H5 — `896` (H5 ON) vs `2944` (H5 OFF), 10 instance × 6 seed × 60s

| Instance | BKS | mean 896 (H5 ON) | gap% | mean 2944 (H5 OFF) | gap% | winner |
|---|---|---|---|---|---|---|
| bench_01_tight_assignment | 9,722 | 9,673.7 | 0.50 | 9,675.2 | 0.48 | OFF +2 |
| bench_02_loose_sequencing | 15,800 | 15,738.3 | 0.39 | 15,721.7 | 0.50 | ON +17 |
| bench_03_clustered_basins | 13,843 | 13,852.0 | −0.07 | 13,839.7 | 0.02 | ON +12 |
| bench_04_capacity_bottleneck | 18,053 | 18,033.7 | 0.11 | 18,082.5 | −0.16 | OFF +49 |
| bench_05_asymmetric_random | 15,689 | 15,694.3 | −0.03 | 15,705.8 | −0.11 | OFF +12 |
| bench_06_asymmetric_metric | 15,356 | 15,354.8 | 0.01 | 15,310.0 | 0.30 | ON +45 |
| bench_07_dense_scale_500 | 31,839 | 31,609.2 | 0.72 | 31,604.5 | 0.74 | ON +5 |
| ladder_03_V350 | 6,204 | 6,141.3 | 1.01 | 6,156.3 | 0.77 | OFF +15 |
| test_500 | 13,199 | 13,112.3 | 0.66 | 13,134.3 | 0.49 | OFF +22 |
| test_850 | 31,066 | 30,339.7 | 2.34 | 30,142.0 | 2.97 | ON +198 |
| **Tổng** | — | **169,549.3** | — | **169,372.0** | — | **ON +177.3** |

- **WIN-RATE: 5/10 – 5/10, hoà.** Không đạt tiêu chí đăng ký trước ≥6/10.
- **Toàn bộ +177 điểm tổng cộng đến từ `test_850` (+198).** Bỏ `test_850`: ON +79, OFF +100 ⇒ **OFF tốt hơn 21 điểm**.
- ⇒ **H5 CŨNG KHÔNG TỔNG QUÁT HOÁ.** "Hiệu ứng +421 ở 24 seed trên `test_850`" là **cùng một loại back-fit** như tooth-vs-legacy.

### 7.6b. ⛔ KẾT LUẬN CUỐI CÙNG CỦA CHIẾN DỊCH — không hướng nào tổng quát hoá

| Hướng | Trên `test_850` | Trên 10 instance | Phán quyết |
|---|---|---|---|
| H2 ghép nhiệt scale/blink | −234 (hại) | — | ❌ hại |
| H3 Basin Kick | −216 (hại) | — | ❌ hại |
| H1 Soft-Deficit | +421 → −74 (đảo dấu) | thua 6/10 | ❌ bác bỏ |
| Thermal tooth vs legacy | +115.7 (CI không giao) | 5/10 hoà | ❌ không tổng quát |
| H5 bandit yield-based | +421 (24 seed) | 5/10 hoà | ❌ không tổng quát |

> **Không một hướng nào trong 5 hướng "operator-first" tổng quát hoá ra khỏi `test_850`.** Ngay cả "champion" `896` cũng **được chọn bằng `test_850`** ⇒ bản thân việc chọn champion là một **back-fit**. Kết luận trung thực: **v2 không chứng minh được ưu thế objective nào so với SEDCA-V3; giá trị duy nhất còn lại là đơn giản hoá mã nguồn.**

---

## 8. TÁI XEM XÉT GIẢ ĐỊNH & PHÂN TÍCH ĐIỂM NGHẼN CẤU TRÚC

### 8.0. Phép đo quyết định — telemetry cấu trúc (đã thêm vĩnh viễn vào report)

Đã bổ sung bộ đếm cấu trúc vào `gm_sisr.cpp` và in ra `report.txt`:
`STRUCT no-op (delta==0)`, `accepted null`, `ruin-empty`, `touched-empty`, `downhill`.

Chạy 20s × seed 1 × mask 896 (champion mặc định) trên 4 chế độ quy mô khác nhau:

| Instance | K/N/M | iters | **Δ=0 (no-op)** | Δ<0 (xuống dốc) | Δ>0 (lên dốc) | accepted | records | **accepted mà no-op** |
|---|---|---|---|---|---|---|---|---|
| `test_850` | 50/200/200 | 89,920 | **34.92%** | 64.13% | **0.95%** | 36.69% | 195 (0.22%) | **95.2%** |
| `bench_01` | 15/45/45 | 942,400 | **34.56%** | 61.40% | 4.04% | 42.25% | 87 (0.009%) | **81.8%** |
| `ladder_03` | 15/83/84 | 720,720 | **32.86%** | 65.91% | 1.23% | 35.24% | 45 (0.006%) | **93.2%** |
| `bench_07` | 20/120/120 | 194,464 | **25.92%** | 68.42% | 5.66% | 36.46% | 231 (0.12%) | **71.1%** |

Hai con số `ruin-empty` và `touched-empty` đều **≈ 0.00%** ở mọi instance. Điều đó có nghĩa:
**mọi phép "no-op" KHÔNG phải là "không làm gì" — mà là "đã gỡ request ra rồi chèn lại y hệt, chi phí thay đổi đúng bằng 0".**

> **Kết luận đo được: 26–35% số vòng lặp, và 71–95% số move ĐƯỢC CHẤP NHẬN, là điểm bất động chính xác của chính thủ tục sửa chữa.**
> Chỉ ~1% số vòng lặp (test_850) là move lên dốc. Chỉ số "accept rate 37%" từ trước tới nay là **số liệu trang trí**.

### 8.1. Kiểm toán giả định ban đầu

| # | Giả định ban đầu | Phán quyết | Bằng chứng |
|---|---|---|---|
| A1 | "Nâng nhiệt/độ phá hoại ⇒ cần toán tử mạnh hơn" | ❌ SAI | H2/H3 đều hại (−234, −216) |
| A2 | "Thang/blink của toán tử nên ghép theo nhiệt `h`" | ❌ SAI | bộ tĩnh đã tune tốt hơn hẳn |
| A3 | "Throughput (it/s) là một đòn bẩy" | ❌ SAI | cấu hình nhanh thắng **vì ít phá hoại**, không vì lặp nhiều; 3× thời gian chỉ thu ~2 điểm % |
| A4 | "Ngân sách thời gian là ràng buộc chính" | ❌ SAI | BKS 180s vẫn cách optimum 2.95%; chỉ ~1% vòng lặp là lên dốc |
| A5 | "BKS nội bộ ≈ optimum" | ❌ SAI/không biết | trên instance duy nhất có optimum, BKS **của chính solver** còn cách 2.95% ⇒ tham chiếu nằm trong cùng một basin |
| A6 | "Chênh lệch objective = chất lượng toán tử" | ❌ SAI | toàn bộ chênh lệch "dương" đều là **nhiễu tập seed** trên `test_850` |
| A7 | "Accept rate 37% nghĩa là search đang khám phá" | ❌ SAI | 71–95% move được nhận là **no-op** |

**Giả định sâu nhất bị bác bỏ:** rằng đây là bài toán **"tinh chỉnh metaheuristic"**. Bằng chứng A4+A5 cho thấy đây là bài toán **giới hạn lân cận / khả năng biểu diễn** (reachability), không phải bài toán ngân sách.

### 8.2. Các điểm nghẽn cấu trúc

**B1 — `ruin + recreate` gần như idempotent (ĐIỂM NGHẼN SỐ 1).**
Cơ chế: `best_insert()` là **argmin chi phí chèn xác định**. Gỡ một chuỗi khỏi tuyến `k` rồi chạy lại greedy ⇒ request quay về **đúng vị trí cũ với đúng chi phí gia tăng cũ** ⇒ `Δ ≡ 0`.
Lối thoát duy nhất hiện có: `blink` (xác suất `1/32` **mỗi vị trí**) và việc một candidate "lạ" giành mất chỗ. Với tuyến ngắn, `blink` gần như không bao giờ bắn.
⇒ **Toàn bộ tín hiệu tìm kiếm của solver đến từ nhiễu blink + candidate ngẫu nhiên.** Đây không phải tìm kiếm có hướng, mà là *nhiễu hoá một điểm greedy*.

**B2 — Repair chỉ có CHÈN, không có ĐẨY/ĐỔI.**
`recreate_with_memory()` chỉ gọi `best_insert()` — tức chỉ có thể **thêm** request vào tuyến. **Không có nhánh nào trục xuất một request đang phục vụ để lấy chỗ.** Do đó request `q` chỉ có lãi *sau khi đuổi `r`* là **bất khả đạt**, trừ khi ruin tình cờ gỡ đúng `r`. Ruin được dẫn bởi **láng giềng hình học**, không bởi **quan hệ xung đột** (tranh chấp capacity/time-window) giữa `q` và `r`.
⇒ Lân cận **về mặt cấu trúc không thể biểu diễn** các phép "swap/ejection" kinh điển.

**B3 — Không có toán tử ở mức PHÂN CÔNG (liên tuyến).**
Mọi thay đổi trạng thái đều là chèn/gỡ **từng request**. Một request chỉ đổi xe như **tác dụng phụ** của ruin+recreate — nhưng vì B1 làm repair idempotent, nó **quay lại đúng xe cũ**. Không tồn tại `relocate(u → xe khác)`, `swap(u,v)`, `or-opt liên tuyến` với đánh giá Δ trực tiếp.
⇒ **Phân công K xe trên thực tế bị đóng băng.**

**B4 — Thiết kế destroy TĨNH.**
`nbr[]` tính **một lần** từ metric không-thời-gian, không bao giờ cập nhật. 3 mode ruin chia cứng 75/15/10. Cơ chế thích nghi duy nhất (H5) chỉ điều biến **tỉ lệ mode**, với reward `mode_records` bắn ~0.006–0.2% số lần gọi ⇒ **tỉ số tín hiệu/nhiễu ≈ 0**. Ngoài ra trọng số bị **kẹp trong [0.5, 2]×** nên kể cả tín hiệu hoàn hảo cũng chỉ dịch được 4 lần.
⇒ Destroy **không bao giờ học được "tập request nào đáng gỡ"**.

**B5 — Basin là MỘT ĐIỂM + hard reset.**
`save_best()` ngay sau greedy init; mỗi lần reheat và mỗi chu kỳ bế tắc đều `load_best()`. ~9 chu kỳ / 60s trên `test_850`.
⇒ Search **khởi động lại từ CÙNG MỘT điểm** 9 lần và hiếm khi rời khỏi nó (vì B1). Tên "Guided **Multi-Basin**" **không được code hậu thuẫn**: không có restart từ cách dựng khác, không elite pool, không tái tổ hợp tuyến.

**B6 — Thermostat hiệu chuẩn trên tín hiệu suy biến.**
`auto_calibrate` / `online_recalibrate` chỉ dùng **độ lớn của delta xấu đi** (`d > 0`). Với 35% Δ đúng bằng 0 và 64% Δ âm, hiệu chuẩn **không hề nhìn thấy cái plateau**. Nhiệt đo được: `test_850` T0/Twork/Tmin = **62.97/11.45/1.74**; `bench_01` 51.97/9.45/1.44; `bench_07` 52.42/9.53/1.45 — đều **rất nhỏ so với `S_scale`**.
⇒ **Đây chính là lời giải thích cho việc tooth-vs-legacy ra kết quả NULL**: hai lịch nhiệt bị nén vào một dải hẹp mà phép kiểm định chấp nhận không phân biệt nổi. Nói cách khác, "lịch nhiệt" chưa bao giờ là biến được kiểm định thật.

**B7 — Pool candidate sửa chữa cố tình nhỏ.**
`CAND_NB=3`, `CAND_RAND=10`. Không có cơ chế chèn **một tập** request cùng lúc ⇒ không có tính "khối".

### 8.3. Số học của khoảng cách ~3% không thể đóng bằng kiến trúc hiện tại

- Optimum `test_850` = 32,012. Champion 60s ≈ 30,435 (gap 4.9%). BKS 180s × 3 seed = 31,066 (gap **2.95%**).
- Tỉ lệ move lên dốc ≈ **0.95%** số vòng lặp, và bước lên dốc trung bình rất nhỏ.
- **Chính tham chiếu dài hạn của solver cũng kẹt ở 2.95%** ⇒ tập đạt được của lân cận hiện tại **loại trừ optimum ra ngoài khoảng ~3%**.
- Kiểm chứng chéo: gấp 3× thời gian chỉ thu ~2 điểm %.
⇒ **Đây là giới hạn KHẢ NĂNG BIỂU DIỄN của lân cận, không phải giới hạn nỗ lực tìm kiếm.** Mọi thứ thuộc họ "chỉnh tham số / chỉnh nhiệt / chỉnh mức phá hoại" đều nằm dưới trần này.

### 8.4. Hướng mới (xếp theo trần kỳ vọng / rủi ro triển khai)

| # | Hướng | Tấn công điểm nghẽn | Chi phí |
|---|---|---|---|
| **D1** | **Repair có trục xuất: "insert-then-eject" / regret-k có đuổi** — sau khi chèn greedy, quét request chưa phục vụ có lãi *nếu đuổi* request chặn đường, thực hiện chuỗi đuổi | B1, B2 | Thấp |
| **D2** | **Toán tử liên tuyến hạng nhất (relocate / swap / or-opt / 2-opt\* xuyên xe) với Δ tính chính xác**, đan xen giữa các vòng ruin | B3 | Thấp |
| **D3** | **Sửa chữa CHÍNH XÁC bằng DP trên tuyến nhỏ** — tuyến ≤ ~13 node (type-1 = 1 node, type-2 = cặp có thứ tự tự nhiên) ⇒ Held-Karp + precedence khả thi. Repair trở thành **tối ưu theo tập**, nên Δ mới có nghĩa, và search chuyển sang không gian **set-partitioning** | B1 (repair không tái tạo lại đúng chuỗi cũ) | Trung bình |
| **D4** | **Route pool + tái tổ hợp set-partitioning (column-generation-lite)** — lưu mọi tuyến từng thấy, định kỳ giải bài toán chọn tập tuyến (greedy/LP) để ghép incumbent mới từ các tuyến thuộc **các basin khác nhau** | B5, B1 (đây là cơ chế chuẩn để phá trần ~3% của VRPP) | Trung bình–cao |
| **D5** | **Lớp destroy THÍCH NGHI thật** — thay 3 mode cứng + reward hiếm bằng trọng số theo **Δ chuẩn hoá**, cập nhật mỗi N vòng, **không kẹp 4×** | B4 | Thấp |
| **D6** | **Đa basin thật**: dựng nghiệm bằng GRASP/nhiễu hoá doanh thu + elite pool, cường hoá từ các basin khác nhau | B5 | Trung bình |
| **D7** | **Bỏ SA, dùng LAHC (Late Acceptance Hill Climbing)** — landscape ở đây **thống trị bởi plateau** (35% Δ=0) và SA đã hiệu chuẩn suy biến; LAHC miễn nhiễm với tham số nhiệt | B6 | **Rất thấp** |

### 8.5. Chỉ số mới bắt buộc: **tỉ lệ no-op**

Từ nay `STRUCT no-op` là **chỉ số hạng nhất**, không phải telemetry phụ:
> Một toán tử tốt **phải làm giảm tỉ lệ no-op**. Nếu một thay đổi không kéo tỉ lệ no-op xuống, nó không thay đổi gì về mặt cấu trúc.

### 8.6. Sửa protocol đánh giá (bài học từ chính chiến dịch này)

1. Tiêu chí ≥6/10 **đã hoạt động đúng** — nó giết H1, tooth, và H5. **Giữ nguyên.**
2. **6 seed/instance là quá ít.** Chênh lệch ±50 trên BKS 13k–31k chỉ là 0.2–0.4%, trong khi biến thiên tập seed của `test_850` đo được là 31,001.8 → 30,877.5 → 30,434.9 (10/24/30 seed). Cần **≥10–15 seed** và **thiết kế ghép cặp** (cùng bộ seed, so *hiệu số ghép cặp*, dùng sign-test theo từng instance) thay vì so trung bình giữa hai tập seed khác nhau.
3. **Không được chọn champion bằng `test_850`.** Chính vì chọn champion trên `test_850` mà ta có mask `896` và cả v2 này. Champion phải được chọn trên bộ đa instance bằng tiêu chí đăng ký trước.
4. Chỉ số chính phải là **số instance thắng/thua có ý nghĩa**, không phải tổng objective (một instance to như `test_850` nuốt hết 9 instance còn lại).

## 9. DANH SÁCH HƯỚNG NGHI NGỜ — REGISTRY CHỜ THÍ NGHIỆM

### 9.0. Luật chơi của registry

Mỗi mục dưới đây là một **nghi ngờ có thể bị giết**, ghi đủ 5 trường:
**(a) Nghi ngờ** — niềm tin đang bị treo; **(b) Bằng chứng hiện có**; **(c) Phép đo quyết định**;
**(d) Sống/kill** — điều kiện giữ hay loại; **(e) Trạng thái**.

Ba cổng bắt buộc trước khi một mục được xét bằng objective:

1. **Cổng no-op (§8.5):** nếu thay đổi *không* kéo `STRUCT no-op` xuống, nó bị loại **bất kể objective** (vì khi đó nó không đổi cấu trúc, chỉ đổi con số ngẫu nhiên).
2. **Cổng ghép cặp (§8.6):** cùng bộ seed, so **hiệu số ghép cặp theo từng seed**, sign-test; **cấm** so trung bình hai tập seed khác nhau.
3. **Cổng đăng ký trước:** ngưỡng thắng phải viết **trước** khi chạy.

Phân biệt hai câu hỏi khác nhau — điều này quan trọng vì phân phối kỳ thi gần `test_850`:

- **Q-A — Tinh chỉnh cho kỳ thi giống `test_850`.** Hợp lệ. Đo **chỉ trên `test_850`** (nhiều seed, ghép cặp). Một thắng lợi ở đây **không** cần tổng quát hoá. Đây là cơ sở để chốt baseline.
- **Q-B — Tuyên bố cải tiến tổng quát.** Bắt buộc bộ ≥10 instance + tiêu chí ≥6/10. Câu hỏi này đã bị chứng minh là **thất bại** cho mọi hướng H1–H5 (§7.6b).

⇒ Registry tách hai cột **QA / QB** cho mỗi mục.

---

### 9.1. Nhóm V (kiểm chứng nền — phải làm trước mọi thứ khác)

Đây là các nghi ngờ về **tính đúng đắn của công cụ đo**. Nếu sai, mọi kết luận phía sau vô nghĩa.

| # | Nghi ngờ | Bằng chứng | Phép đo quyết định | Kill nếu |
|---|---|---|---|---|
| **V1** | `best_insert()` tính Δ gia tăng **sai lệch** so với tính lại từ đầu (lỗi time-window/load tích luỹ) | Mọi thắng lợi của search dựa trên Δ này; chưa từng assert | Chế độ `--assert-delta`: sau mỗi move, tính lại objective tuyến từ 0 và so với Δ tích luỹ; đếm số lệch | Δ sai > 0 lần ⇒ **dừng, sửa trước khi làm gì khác** |
| **V2** | Bất biến nghiệm: chi phí báo cáo không bằng chi phí thực của chuỗi phục vụ | Chưa từng tái kiểm độc lập | Script đọc `report.txt`, tự parse tuyến và tính lại objective; so | Lệch > 0 ⇒ sửa kế toán |
| **V3** | Optimum `32012` của `test_850` có đúng không (nếu BKS thật thấp hơn, "gap 3%" là ảo) | `opt_850.txt` là dữ liệu có sẵn, không tự sinh | Kiểm tra chứng cứ/tính khả thi của nghiệm 32012 (feasible + cost khớp) | Nếu 32012 bất khả thi ⇒ **toàn bộ mốc gap phải tính lại** |

### 9.2. Nhóm S — nghi ngờ cấu trúc (tấn công điểm nghẽn B1–B7)

| # | Nghi ngờ | Tấn công | Chi phí | QA | QB (kill nếu) |
|---|---|---|---|---|---|
| **S1** | Repair **chỉ chèn** nên không biểu diễn nổi "swap/ejection" ⇒ thêm **insert-then-eject / regret-k có đuổi** sẽ hạ no-op và mở vùng mới | B1, B2 | Thấp | Đo no-op + obj `test_850` | <6/10 instance |
| **S2** | **Không có toán tử liên tuyến hạng nhất** ⇒ phân công K xe bị đóng băng; thêm `relocate/swap/or-opt/2-opt*` với Δ chính xác, đan xen ruin | B3 | Thấp | như trên | <6/10 |
| **S3** | Repair greedy **tái tạo đúng chuỗi cũ** (Δ≡0) ⇒ nếu repair **tối ưu theo tập** bằng DP (Held-Karp, tuyến ≤ ~13 node) thì Δ mới có nghĩa | B1 | Trung bình | no-op phải giảm mạnh; obj | <6/10 |
| **S4** | Chỉ có **một basin điểm** + hard reset ⇒ cần **route pool + set-partitioning** để ghép tuyến từ các basin khác nhau (cơ chế chuẩn phá trần ~3% của VRPP) | B5, B1 | Trung bình–cao | obj trên `test_850` (nơi trần 3% hiện rõ nhất) | <6/10 |
| **S5** | Destroy **tĩnh**, reward H5 bắn ~0.006–0.2% và **bị kẹp 4×** ⇒ thay bằng trọng số theo **Δ chuẩn hoá, không kẹp** | B4 | Thấp | tỉ lệ bắn reward, no-op, obj | <6/10 |
| **S6** | **Đa basin thật** (GRASP + elite pool) thay vì restart về cùng điểm | B5 | Trung bình | obj, số basin phân biệt | <6/10 |
| **S7** | Landscape **thống trị plateau** (35% Δ=0) trong khi SA đã hiệu chuẩn suy biến ⇒ **thay SA bằng LAHC** (miễn nhiễm tham số nhiệt) | B6 | **Rất thấp** | no-op + obj | <6/10 |
| **S8** | No-op đến từ `blink` **xác suất 1/32 mỗi vị trí** và argmin xác định ⇒ cần **bảo đảm phi-idempotent có cấu trúc**: repair tiêu thụ **hoán vị ngẫu nhiên** các vị trí ứng viên với p>0, sao cho Δ≠0 luôn khả thi | B1 | Thấp | **no-op là chỉ số chính** | no-op không giảm ⇒ loại ngay |
| **S9** | Pool candidate **cố tình nhỏ** (`CAND_NB=3`, `CAND_RAND=10`) ⇒ nới lên (8 / 30) có hạ no-op | B7 | Rất thấp | no-op trước, obj sau | no-op không giảm |
| **S10** | Ruin dẫn bởi **láng giềng hình học**, không bởi **xung đột** (capacity/time-window) ⇒ chọn hạt giống theo **đồ thị xung đột** | B2, B4 | Thấp | no-op + obj | <6/10 |
| **S11** | Chính sách reheat (`load_best()`) chưa từng bị kiểm định thật ⇒ A/B **reset-to-best vs giữ-nguyên vs restart-ngẫu-nhiên** | B5 | Thấp | obj ghép cặp | <6/10 |
| **S12** | Bit **`64`** (blink tĩnh / CAND_RAND) **chưa từng bị tắt riêng** ⇒ chưa biết nó đóng góp hay phá, và tương tác với H5 | B1, B7 | Rất thấp | ghép cặp trên `test_850` | ~~mục này là **chẩn đoán**, không kill~~ → **ĐÃ GIẢI & HUỶ**: bit `64` là **bit chết** (hằng số mặc định đã bằng đúng giá trị nó đặt), xem §11.7/N1 |
| **S13** | **Phân tầng toán tử theo nhiệt/bế tắc** — enabler: chỉ bật toán tử đắt khi `h` thấp / bế tắc; bảo toàn throughput | — (là điều kiện cho S1/S3/S4/S9) | Thấp | T6: it/s ≥ 80% baseline | không đạt 80% ⇒ hạ tần suất kích hoạt |

### 9.3. Nhóm P — nghi ngờ protocol / lựa chọn (đã ghi ở §8.6, liệt kê để không quên)

- **P1 — Chọn lại "champion" trên bộ 10 instance bằng tiêu chí đăng ký trước.** Mask `896` hiện tại là **back-fit trên `test_850`**; phải tái lựa chọn minh bạch.
- **P2 — N ≥ 10–15 seed/instance.** 6 seed để lọt chênh lệch 0.2–0.4%.
- **P3 — Chỉ số chính = số instance thắng/thua có ý nghĩa**, không phải tổng objective (một `test_850` to nuốt 9 instance còn lại — đúng cái bẫy đã lừa ta ở §7.6b).
- **P4 — Không bao giờ chọn champion / tuyên bố tổng quát từ một instance.** (Đã ghi thành nguyên tắc #7 ở §0.)
- **P5 — ĐAN XEN theo thời gian, không chạy từng mask theo khối.** Ghép cặp theo seed là **chưa đủ**: nếu mỗi mask chiếm một *khối thời gian* riêng thì trôi máy (nhiệt/tiến trình nền) vẫn lọt vào so sánh. Bằng chứng định lượng ở §10.3. Thiết kế đúng: mỗi seed chạy **tất cả mask cùng lúc**, hoặc luân phiên round-robin.

### 9.4. Danh sách ĐÓNG — cấm chạy lại (đã có phán quyết)

Các mục sau đã bị dữ liệu giết; **liệt kê để không tiêu tốn thêm quota**:

| Đã đóng | Phán quyết | Nơi ghi |
|---|---|---|
| H1 (soft-deficit) | pooled `+13 ± 28`, mất 6/10 ⇒ **vô hiệu** | §6.5b, §7.2 |
| H2 (thermal-coupled ruin/blink) | `−234`, throughput `−40..60%` ⇒ **loại** | §6.1–6.2 |
| H3 (basin kick) | `−216` (cùng H2 `−525`) ⇒ **loại** | §6.1–6.2 |
| Tooth vs legacy thermal | hoà 5/10, lệch 0.002% ⇒ **không phân biệt được** | §7.4 |
| H5 như "cải tiến tổng quát" | hoà 5/10; toàn bộ `+177` đến từ `test_850` ⇒ **không tổng quát hoá** | §7.6, §7.6b |
| H4 (pairwise) | chưa từng cài đặt — bit `1024` để dành | §3 |
| Tinh chỉnh nhiệt / mức phá hoại / chỉnh tham số | **nằm dưới trần biểu diễn ~3%** ⇒ tốn công vô ích | §8.3 |

> **Ghi chú trung thực:** H5 vẫn **có thể** là "đúng" cho **Q-A** (`test_850`): nó là một trong hai tín hiệu dương duy nhất ở đó. Nó chỉ bị loại với tư cách tuyên bố **Q-B**. Đây là lý do mask `768` (H1+H5 ON) vẫn là ứng viên baseline cho kỳ thi giống `test_850`.

### 9.5. Thứ tự khuyến nghị

> **Cập nhật:** thứ tự dưới đây **đã bị thay thế bởi §11.8** (sau khi chuyển sang phân tích tổng quát,
> xếp theo *bảo đảm cấu trúc* thay vì theo *giá trị kỳ vọng trên một instance*).

1. **Nhóm V** (V1–V3) — bắt buộc, chi phí thấp, chặn rủi ro nền.
2. **S8 + S9 + S12** — rẻ nhất, đo bằng **no-op** trước; nếu no-op không giảm thì dừng ngay, tiết kiệm toàn bộ phần sau.
3. **S2 + S7** — cùng nhau, tấn công B3 (phân công đóng băng) và B6 (SA suy biến); đây là combo rẻ nhất có khả năng phá trần.
4. **S1 / S3 / S4** — chỉ sau khi no-op đã giảm; đây là nhóm có trần cao nhất (§8.3: chỉ nhóm này mới đóng được gap ~3%).
5. **S10 / S11 / S5 / S6** — mở rộng nếu còn quota.
6. **P1–P5** — áp dụng như luật nền cho **mọi** thí nghiệm ở trên.

---

## 10. SWEEP BASELINE `test_850` — 6 PHIÊN BẢN × 18 SEED GHÉP CẶP (60s)

> **Mục đích:** trả lời câu hỏi "phiên bản nào tốt nhất trên `test_850`" để chốt baseline cho kỳ thi
> (phân phối kỳ thi gần `test_850`). **Kết quả: câu hỏi này KHÔNG trả lời được bằng phép đo hiện có** —
> và lý do không trả lời được mới là kết quả quan trọng. Ghi nguyên vẹn ở §10.4.

### 10.1. Nhận dạng phiên bản (giải mã mask)

| mask | H1 | H2 | H3 | H5 | Thermal | Mô tả ngắn |
|---|---|---|---|---|---|---|
| **768** | ON | off | off | **ON** | tooth | H1+H5 |
| **896** | off | off | off | **ON** | tooth | **mặc định hiện tại** (H5, tooth) |
| **4864** | ON | off | off | **ON** | **legacy** | H1+H5 + legacy thermal |
| **4992** | off | off | off | **ON** | **legacy** | H5 + legacy thermal |
| **2944** | off | off | off | off | tooth | tắt hết tính năng mới (tooth) |
| **7040** | off | off | off | off | **legacy** | nguyên bản (tắt hết + thermal cũ) |

### 10.2. Objective — 18 seed ghép cặp, optimum `test_850` = 32,012

| mask | mean | median | min | max | it/s | gap%opt |
|---|---|---|---|---|---|---|
| 768 | 30,829.9 | 30,872 | 30,321 | 31,140 | 14,513 | 3.69 |
| 896 | 30,891.0 | 30,933 | 30,693 | 31,055 | 19,202 | 3.50 |
| **4864** | **30,945.5** | **30,984** | 30,498 | **31,258** | 17,736 | **3.33** |
| 4992 | 30,913.5 | 30,923 | 30,667 | 31,109 | 17,815 | 3.43 |
| 2944 | 30,935.4 | 30,987 | 30,652 | 31,070 | 17,410 | 3.36 |
| 7040 | 30,753.4 | 30,779 | 30,360 | 31,069 | 11,720 | 3.93 |

**So ghép cặp ± sign-test (hai chiều, hai phép quy chiếu khác nhau):**

| vs `4864` | pairedD | SE | thắng/thua | p | | vs `7040` | pairedD | SE | thắng/thua | p |
|---|---|---|---|---|---|---|---|---|---|---|
| 768 | −115.6 | 64.8 | 5/13 | 0.096 | | 768 | +76.6 | 61.4 | 10/8 | 0.81 |
| 896 | −54.5 | 59.9 | 8/10 | 0.81 | | 896 | **+137.6** | 52.0 | **14/4** | **0.031** |
| 4992 | −32.0 | 61.6 | 8/10 | 0.81 | | 4864 | +192.1 | 67.8 | 13/5 | 0.096 |
| 2944 | −10.1 | 56.0 | 9/9 | 1.00 | | 4992 | **+160.1** | 53.5 | **14/4** | **0.031** |
| 7040 | −192.1 | 67.8 | 5/13 | 0.096 | | 2944 | **+182.1** | 60.6 | **15/3** | **0.0075** |

**Nghịch lý lộ ra ngay:** nếu đúng là thermal/H5 có tác dụng thật thì phải có một thứ tự bắc cầu.
Ta lại có `2944 > 7040` (+182, p=0.0075) *và* `4992 > 7040` (+160, p=0.031) *và* `896 > 7040` (+138, p=0.031),
nhưng `896 ≈ 4992 ≈ 2944` (mọi p ≥ 0.81) — trong khi `896` là "cả hai thay đổi cùng lúc" và
`4992`/`2944` là "mỗi thứ một thay đổi". **Hai cái sửa riêng lẻ lại không cộng lại được thành cái sửa gộp.**
Đó là chữ ký của **nhiễu**, không phải của hiệu ứng.

### 10.3. PHÁT HIỆN 1 — no-op rate BẤT BIẾN theo mask

Telemetry `STRUCT` vĩnh viễn (§8.0) gom theo từng mask (18 run/mask, `analyze_noop.py`):

| mask | 768 | 896 | 4864 | 4992 | 2944 | 7040 |
|---|---|---|---|---|---|---|
| **no-op %** | 37.10 | 37.56 | 37.43 | 37.39 | 37.55 | 37.41 |
| accepted % | 38.7 | 39.0 | 38.7 | 38.6 | 39.1 | 38.7 |
| downhill % | 62.10 | 61.71 | 61.90 | 61.98 | 61.67 | 61.91 |
| it/s | 14,513 | 19,202 | 17,736 | 17,815 | 17,410 | 11,720 |

**Toàn bộ 6 phiên bản có tỉ lệ no-op giống nhau trong khoảng ±0.5 điểm %**, trong khi độ lệch chuẩn
từng run ≈ 1.2 điểm % ⇒ **không khác biệt có ý nghĩa**. Theo **cổng §8.5** (*một toán tử tốt phải kéo
no-op xuống*), **cả H1, H5, và lịch nhiệt đều KHÔNG thay đổi cấu trúc tìm kiếm.** Đây là xác nhận
độc lập cho phán quyết §7.6b, lần này bằng chỉ số *cấu trúc* chứ không bằng objective.

### 10.4. PHÁT HIỆN 2 — sweep kiểu khối bị nhiễu bởi TRÔI THỜI GIAN

Sweep chạy 6 mask **tuần tự theo khối** (mỗi mask ~3 phút, tổng ~18 phút, 6 worker đồng thời).
Throughput **trôi ngay bên trong mỗi khối**:

| mask | it/s 3 wave liên tiếp | nhận xét |
|---|---|---|
| 896 | 22.6k, 23.2k, 23.1k, 22.5k, 22.6k, 23.0k → 17.6k, 17.8k, 17.6k, 18.0k, 17.4k, 17.3k → 17.2k, 17.0k, 17.2k, 17.3k, 17.0k, 17.1k | **−25% trong 3 phút** |
| 7040 | 14.3k, 14.4k, 14.1k, 14.1k, 14.3k, 14.1k → 12.6k, 12.4k, 12.5k, 12.5k, 12.4k, 12.4k → **8.6k, 8.6k, 8.3k, 8.3k, 8.5k, 8.7k** | **−41% trong 3 phút** |
| 4864 / 4992 | ~17.4–18.8k ổn định | — |

Và objective bám **gần như đơn điệu** theo số vòng lặp:

| mask | iters (mean) | obj (mean) |
|---|---|---|
| 7040 | 703k | 30,753 |
| 768 | 871k | 30,830 |
| 2944 | 1,045k | 30,935 |
| 4864 | 1,064k | 30,946 |
| 4992 | 1,069k | 30,914 |
| 896 | 1,152k | 30,891 |

⇒ **"Phiên bản tốt hơn" trên `test_850` phần lớn chỉ là "phiên bản chạy được nhiều vòng lặp hơn".**
`7040` chạy cuối cùng, bị bóp throughput nặng nhất, nên bị −192 điểm — **không phải vì nó dở về thuật toán**.
`768` chậm vì H1 thực sự tốn CPU (nó quét thêm candidate), cũng không phải vì H1 dở.

### 10.5. Kết luận §10 (trả lời thẳng câu hỏi "phiên bản nào tốt nhất")

1. **Không có "phiên bản tốt nhất" trên `test_850` theo nghĩa objective.** Ở 18 seed ghép cặp,
   mọi chênh lệch giữa 6 phiên bản (10–192 điểm = **0.03–0.6%** của optimum) đều có SE 52–68
   và/hoặc p ≥ 0.096, **trừ** hiệu ứng "7040 chậm" — thứ bị giải thích bởi trôi thời gian.
2. **Theo chỉ số cấu trúc (no-op), cả 6 là MỘT phiên bản.** Chọn max của một đại lượng
   không phân biệt được ⇒ đang **chọn nhiễu**.
3. **Thiết kế hiện tại chưa thể trả lời câu hỏi này.** Cần sweep **đan xen** (§9.3 P5) để triệt tiêu trôi.
   Đã cài `interleaved_bench.py` (mỗi seed chạy cả 6 mask *cùng lúc*, ghi kèm `report.txt` để mask
   là nhãn **tất định**, không suy từ dấu thời gian).
4. **Điều duy nhất đủ vững để nói:** trên `test_850`, **cấu hình "nguyên bản tắt hết" (7040) là kém nhất**,
   nhưng kém vì *tốc độ*, và ta chưa loại được trôi. **Không có cơ sở để freeze bất kỳ mask nào làm baseline
   từ dữ liệu cũ.** Phải đợi kết quả đan xen.

### 10.6. Trạng thái

- [x] Sweep khối 6 mask × 18 seed (dùng làm *chẩn đoán trôi*, không dùng để xếp hạng).
- [x] Telemetry no-op theo mask (`analyze_noop.py`).
- [x] Xác thực nền: `opt_850.txt` **hợp lệ và đúng 32,012** (`utility/validator.py`); `result.txt` của
      solver khớp **tuyệt đối** với validator độc lập (bench_01 = 9,722; test_850 = 29,995) ⇒ **V2, V3 đóng**;
      V1 (Δ gia tăng) vẫn mở nhưng **rủi ro bị chặn**: objective cuối được `recompute()` lại từ đầu
      (gm_sisr.cpp:1032–1038) nên Δ sai chỉ làm lệch *hướng tìm kiếm*, không làm sai *kế toán điểm*.
- [x] ~~Sweep ĐAN XEN 6 mask × 18 seed~~ → **ĐÃ HUỶ** theo chỉ đạo: dừng hướng "tune theo `test_850`".
      Chạy dở 7/18 seed (`output/t850_inter/`), giữ lại làm bằng chứng phụ: trong 7 seed đan xen đó,
      **thứ hạng đảo hoàn toàn mỗi seed** (seed 4: `4864=31,361` cao nhất; seed 1: `2944=30,997` cao nhất;
      seed 7: `2944=31,025` cao nhất) ⇒ xác nhận thêm rằng **không có thứ hạng ổn định giữa các mask**.
- [x] **Kết luận vận hành:** không tune theo `test_850` nữa. Chuyển sang **phân tích tổng quát** (§11):
      đánh giá các hướng theo **bảo đảm cấu trúc đúng cho mọi instance**, không theo điểm trên một instance.

---

## 11. PHÂN TÍCH TỔNG QUÁT (KHÔNG BÓ THEO `test_850`) + BASELINE CHÍNH THỨC

### 11.1. BASELINE CHÍNH THỨC = mask `896` (mặc định biên dịch)

Chốt từ mã nguồn, không từ trí nhớ: **`gm_sisr.cpp:19`** → `static int ABLATION_MASK = 128 | 256 | 512;` = **896**.

| Bit | Ý nghĩa bit | Ở baseline 896 | Hệ quả *thực tế* trên code |
|---|---|:---:|---|
| 1 | tắt regret khi sắp thứ tự recreate (`order_candidates`) | **clear** | **regret ON** — 50% `ema_regret` + 25% net-profit + 25% random (`gm_sisr.cpp:454–477`) |
| 2 | tắt Mode 2 (unserved cavity) | clear | Mode 2 **ON** |
| 4 | tắt Mode 3 (bottleneck) | clear | Mode 3 **ON** |
| 8 | reheat mềm | clear | **hard reheat**: reset về `best` khi `cur_obj < best_obj − 2·micro_delta` (`:878, :917`) |
| 16 | tournament chọn seed ruin | clear | seed ruin **uniform ngẫu nhiên** |
| 32 | sawtooth tĩnh | clear | dùng **exponential-tooth** |
| 64 | blink 1/32 + `CAND_RAND`=10 | clear | **BIT CHẾT** — xem §11.7 |
| **128** | tắt **H1** soft-deficit | **set** | H1 **OFF**; `SOFT_EPS0` không dùng (`:278` ⇒ `eps=0`) |
| **256** | tắt **H2** ghép nhiệt toán tử | **set** | H2 **OFF** ⇒ `BLINK_MASK=31`, `C_AVG_DYN=10`, `L_MAX_DYN=10` **hằng số** (`:860`) |
| **512** | tắt **H3** basin kick | **set** | H3 **OFF**; `pending_kick` bị nuốt (`:357`) |
| 1024 | H4 pairwise | clear | **không tồn tại code** (bit để dành) |
| 2048 | tắt **H5** bandit | clear | **H5 ON** — roulette `bandit_w` (`:346, :383`), cập nhật mỗi 20k vòng |
| 4096 | dùng thermal **legacy** | clear | **SEDCA-V4 exponential-tooth** |
| 8192 | tắt tái hiệu chuẩn trực tuyến | clear | **recalibration ON** |

**Phát biểu baseline một câu:** *GM-SISR mặc định = khung SEDCA cũ + H5 bandit + thermal exponential-tooth
+ tái hiệu chuẩn trực tuyến; toàn bộ H1/H2/H3 bị ablation, H4 chưa hề được viết.*

⇒ Hệ quả quan trọng: **chiến dịch "operator-first" v2 không để lại toán tử nào trong baseline.**
Thứ duy nhất nó thêm vào sản phẩm cuối là **H5** (mà §7.6 cho thấy không tổng quát hoá) cộng
**lịch nhiệt tooth** (mà §7.4 cho thấy hoà với legacy) cộng hạ tầng telemetry. Đây là sự thật cần ghi
nhận khi nói về "phiên bản hiện tại": nó *không phải* một bước tiến cấu trúc so với legacy, chỉ là
một **cấu hình** khác của cùng một khung.

### 11.2. Đổi thước đo: ba loại tuyên bố

Toàn bộ thất bại của v2 nằm ở chỗ ta đi đo **loại A**. Từ nay phân loại rõ:

| Loại | Nội dung | Kiểm chứng bằng | Có tổng quát? |
|---|---|---|---|
| **A — Hiệu ứng** | "cấu hình này cho điểm cao hơn" | benchmark nhiều seed | ❌ **Không.** Đã bị chứng minh là nhiễu ở §7.6b và §10 (thứ hạng đảo mỗi seed) |
| **B — Bảo đảm cấu trúc** | "lân cận mới là *siêu tập* của lân cận cũ" / "toán tử này *không bao giờ* làm nghiệm xấu đi" | **lập luận + property test**, không cần instance | ✅ **Có.** Đúng cho *mọi* instance theo cấu trúc |
| **C — Chi phí** | "thêm X phép toán mỗi vòng lặp" | đếm phép toán / it/s | ✅ Có (máy-independent nếu đếm phép toán) |

**Luật mới:** một hướng chỉ được đưa vào code nếu nó có **tuyên bố loại B** (hoặc chứng minh được loại C là rẻ).
Không nhận "nó làm tăng điểm trên instance Y".

### 11.3. Sáu bất biến cấu trúc của baseline (I1–I6) và ĐỒ THỊ PHỤ THUỘC

| | Bất biến (đang bị vi phạm) | Phát biểu chính xác | Đo được ở baseline |
|---|---|---|---|
| **I1** | **Repair có điểm bất động** | Với tập gỡ `S`, `best_insert` là argmin **xác định** ⇒ nếu không có blink/candidate lạ, `repair(x∖S) = x` ⇒ `Δ ≡ 0` | **no-op 37.1–37.6%** số vòng lặp, **bất biến với mọi mask** (§10.3) |
| **I2** | **Lân cận chỉ có CHÈN** | Không tồn tại move nào *trục xuất* một request đang phục vụ (`:164–230` chỉ có chèn tại vị trí) | `ruin-empty` = `touched-empty` = **0.00%** ⇒ mọi no-op là "gỡ rồi chèn lại đúng chỗ" |
| **I3** | **Phân công xe đóng băng** | Hệ quả của I2 + ruin **hình học**: `veh[]` chỉ đổi như tác dụng phụ, và do I1 nó đổi về **đúng xe cũ** | không có bộ đếm trực tiếp — hệ quả logic |
| **I4** | **Basin MỘT ĐIỂM + hard reset** | `save_best()` ngay sau greedy; mọi reheat/bế tắc đều `load_best()` (`:490`) ⇒ **1 điểm khởi động duy nhất** | ~9 chu kỳ/60s, tất cả từ cùng một điểm |
| **I5** | **Destroy TĨNH** | `nbr[]` tính **một lần**, không cập nhật; 3 mode chia cứng 75/15/10 | Mode 1/2/3 = 75.0/15.0/10.0% (`:383` + số đo); `bandit_w` hội tụ về đúng tỉ lệ đó |
| **I6** | **Nhiệt hiệu chuẩn trên tín hiệu suy biến** | `auto_calibrate` chỉ dùng delta **xấu đi** (`d>0`) ⇒ không nhìn thấy plateau | `T0/Twork/Tmin = 62.97/11.45/1.74` — dải nén |

**Đồ thị phụ thuộc (điểm quan trọng nhất của §11):**

```
I1 (repair bất động)  ──┬──▶ plateau thống trị (37% Δ=0)
                        ├──▶ I6: hiệu chuẩn nhiệt KHÔNG nhìn thấy plateau ⇒ T bị nén
                        └──▶ năng suất mọi toán tử ≈ 0 (records/calls = 0.03%)
I2 (chỉ chèn)         ────▶ I3 (phân công đóng băng)  [và khiến I5 trở nên chí mạng:
                                                     xác suất gỡ "đúng vật cản" ≈ 0]
I4, I5                ──── độc lập với I1–I3
```

⇒ **Hệ quả phản trực giác:** "lịch nhiệt" **chưa bao giờ là một biến độc lập** trong solver này —
nó chỉ là hàm của plateau, mà plateau là hệ quả của I1. Đây là lời giải thích *cơ học* (không phải
thống kê) cho kết quả NULL của tooth-vs-legacy (§7.4). Cùng lý do: H2 (ghép nhiệt) *không thể* có tác
dụng khi đầu vào của nó (Δ) đã suy biến.

### 11.4. Bảng phân tích tổng quát từng hướng

Cột **Bảo đảm (loại B)** là điều kiện tiên quyết. **"Siêu tập"** = lân cận/kết quả mới ⊇ cũ ⇒
**trần chất lượng không giảm**, rủi ro *chỉ còn ở throughput*.

| # | Hướng | Phá bất biến | Bảo đảm loại B (đúng mọi instance) | Chi phí / vòng lặp | Rủi ro thật | Phụ thuộc |
|---|---|---|---|---|---|---|
| **S2** | Toán tử liên tuyến hạng nhất (relocate / swap / or-opt / 2-opt\*) | I2, I3 | **Siêu tập move**: giữ nguyên ruin+recreate cũ, chỉ *thêm* move mới ⇒ trần không giảm | O(1) cho relocate/swap với cấu trúc Δ tăng dần; O(m) cho or-opt | chỉ tốn thời gian | — |
| **S9** | Nới `CAND_NB` 3→8, `CAND_RAND` 10→30 | I1 (một phần) | **Siêu tập ứng viên**: argmin/regret trên tập lớn hơn ⇒ không thể chọn *tệ hơn* | tuyến tính theo kích thước tập | thêm thời gian | — |
| **S4** | Route pool + chọn tập tuyến (set-partitioning) | I4, I1 | **ĐƠN ĐIỆU**: giữ incumbent như một ứng viên trong bước chọn ⇒ kết quả **không bao giờ xấu hơn** incumbent. Đây là **bảo đảm mạnh nhất** có thể có | định kỳ (không mỗi vòng); pool có dedup | bộ nhớ pool; bước chọn phải luôn so với incumbent | S2/S3 làm pool giàu hơn |
| **S3** | Repair CHÍNH XÁC bằng DP/beam trên tuyến nhỏ | I1 | **Siêu tập** (hoán vị greedy là 1 ứng viên của DP) **+ trội** (cùng tập request ⇒ chi phí DP ≤ greedy) | DP đầy đủ **bất khả thi** ở 40k it/s (2^13·13² ≈ 1.4M); dùng **beam width w** hoặc trần ≤8 node, chỉ gọi khi bế tắc | cần bound chặt nếu không sẽ phá throughput | S13 (phân tầng) |
| **S1** | Ejecting repair (insert-then-eject / regret-k có đuổi) | I2 | **Siêu tập**: thêm khả năng đuổi ⇒ chứa được move "chỉ có lãi sau khi đuổi" (hiện bất khả đạt) | O(\|tuyến\|) mỗi lần eject | chọn sai vật bị đuổi ⇒ đi ngang, không xấu hơn | S10 làm giàu |
| **S10** | Ruin dẫn bởi ĐỒ THỊ XUNG ĐỘT (capacity/time-window) | I5, I2 | **Xác suất > 0**: đưa xác suất gỡ "đúng vật cản" từ **≈0** lên **>0** (hiện `nbr[]` thuần hình học) | O(1) mỗi seed; cần dựng đồ thị xung đột | dựng conflict graph tốn bộ nhớ | S1 |
| **S8** | Bảo đảm phi-idempotent **có cấu trúc** (repair tiêu thụ hoán vị ngẫu nhiên) | I1 | **Không điểm bất động theo cấu trúc**: đầu ra repair là *biến ngẫu nhiên* ⇒ bất biến "repair = identity" bị phá **theo định nghĩa**, không nhờ may mắn của blink | gần như 0 (chỉ đổi thứ tự duyệt) | mất tính tham lam ⇒ có thể *tệ hơn* greedy (không phải siêu tập!) | — |
| **S5** | Destroy thích nghi thật, **không kẹp 4×** | I5 | **Đo được**: phân bố mode phải *khác* 75/15/10 một lượng KL > 0 (hiện `bandit_w` hội tụ về **đúng** 75.0/15.0/10.0% ⇒ cơ chế thích nghi đang là **no-op về mặt phân bố**) | 0 (đã có `update_bandit`) | kẹp bỏ ⇒ dao động mạnh | — |
| **S6** | Đa basin thật (GRASP + elite pool) | I4 | **Đa dạng đo được**: #điểm khởi động phân biệt > 1, khoảng cách Hamming đôi một > 0 | 1 lần greedy/thuật toán khởi động | — | S4 dùng chung pool |
| **S7** | LAHC thay SA | I6 | **Không tham số nhiệt**: quyết định chấp nhận không phụ thuộc `T`; plateau đi qua được theo *quy tắc tất định* | ~0 | có thể thua SA đã tune tốt trên landscape khác — nhưng SA ở đây đã tune *suy biến* (§11.3 I6) nên đối thủ yếu | — |
| **S11** | Chính sách reheat (reset-to-best vs giữ-nguyên vs restart-ngẫu-nhiên) | I4 | **Không có** (đây là *policy*, không phải cấu trúc) ⇒ **hạ cấp**: coi là tham số của S6 | 0 | — | S6 |
| **S12** | Bit `64` | — | **ĐÃ GIẢI**: bit chết (§11.7) ⇒ **huỷ mục này**, không phải thí nghiệm | — | — | — |
| **S13** *(mới)* | **Phân tầng toán tử theo nhiệt/bế tắc** | — (enabler) | **Bảo toàn throughput**: chỉ bật toán tử đắt khi `h` thấp / bế tắc ⇒ ~0 chi phí ở pha khám phá | 0 | ngưỡng kích hoạt cần chọn | **là điều kiện cho S1/S3/S4** |

**Ba kết luận rút ra từ bảng (đều tổng quát, không dựa vào điểm số):**

1. **S4 là hướng duy nhất có bảo đảm ĐƠN ĐIỆU.** Nó là hướng duy nhất *không thể* làm ta tệ đi.
   Đây là lý do nó xứng đáng là ưu tiên số 1 về mặt *rủi ro*, dù không phải rẻ nhất.
2. **S5 vừa được giải bằng số liệu §11.3/I5:** cơ chế H5 "thích nghi" thực chất **hội tụ về đúng tỉ lệ
   tĩnh 75/15/10** ⇒ nó là một cơ chế *trang trí*. Sửa S5 là **bắt buộc** nếu muốn bất kỳ cơ chế thích nghi nào.
3. **S13 là điều kiện tiên quyết**, vì toàn bộ rủi ro của S1/S2/S3/S4/S9 đều là *thời gian*, và §10.4 đã
   đo được objective bám gần như đơn điệu theo số vòng lặp.

### 11.5. Nguyên lý thiết kế rút ra

> **"CHỈ THÊM move, KHÔNG ĐỔI move."**
> Với một tìm kiếm ngẫu nhiên hoá đã có SA, việc **thêm** toán tử vào tập move ⇒ lân cận là *siêu tập*
> ⇒ **trần chất lượng không thể giảm**, rủi ro *chỉ còn* là chi phí thời gian. Ngược lại, **thay** toán tử
> (như toàn bộ chiến dịch v2 đã làm: thay thermal, thay reheat, thay ordering) **có thể làm tệ đi** và
> đã làm tệ đi — rồi ta không phân biệt nổi với nhiễu.

Hệ quả vận hành: mọi thay đổi phải được viết thành **"thêm một nhánh move"** sau một cổng kích hoạt
(S13), chứ không phải "đổi công thức đang có". Đây là điều kiện để mọi so sánh sau này có **tính đơn điệu
về trần** — thứ duy nhất giúp kết luận không phụ thuộc vào việc chọn instance nào.

### 11.6. Tiêu chí nghiệm thu KHÔNG phụ thuộc instance (property tests)

Thay cho việc chạy benchmark nhiều seed (đã bị chứng minh là không phân giải nổi), dùng các phép kiểm tra
**bất biến** — chạy trên *bất kỳ* instance nào, kết quả là **đúng/sai**, không phải trung bình:

| | Phép kiểm | Ngưỡng đạt | Baseline hiện tại |
|---|---|---|---|
| **T1** | Cố định tập ruin `S`, lấy 1,000 mẫu repair; đếm số kết quả **phân biệt** | > 1 (và tỉ lệ `repair = identity` < 5%) | **≈1 kết quả**, identity ≈ 37% (I1) |
| **T2** | Dựng trạng thái có một `swap(u,v)` hoặc `relocate` **cải thiện được**; khẳng định toán tử tìm ra và áp dụng (`Δ<0`) | tìm thấy 100% | **thất bại theo cấu trúc** — không tồn tại toán tử đó (I2/I3) |
| **T3** | Chạy bước "polish" (S4); khẳng định objective **không giảm** | 0 vi phạm (một vi phạm = bug) | chưa có |
| **T4** | Đếm số điểm khởi động/incumbent phân biệt trong một lần chạy | > 1 | **= 1** (I4) |
| **T5** | Phân bố chọn mode sau N vòng, so với 75/15/10 (KL) | > 0 (khác biệt có ý nghĩa) | **= 0** (I5, đo được) |
| **T6** | Chi phí thêm-move: it/s phải ≥ 80% baseline trên *cùng* máy, đo **đan xen** | ≥ 80% | n/a |

T1–T5 **không cần so sánh giữa các phiên bản** ⇒ không cần sweep, không cần chọn instance,
không có "trôi thời gian". T6 là phép đo duy nhất cần máy, và phải làm đan xen (§9.3 P5).

### 11.7. Nợ kỹ thuật phát hiện trong baseline

| # | Vấn đề | Bằng chứng | Xử lý |
|---|---|---|---|
| N1 | **Bit `64` là bit CHẾT** | Comment `:26` nói "mặc định = Blink 1/64, CAND_RAND 6"; nhưng hằng số thật là `BLINK_MASK=31` (1/32), `CAND_RAND=10` (`:32–33`) — **đúng bằng giá trị mà bit 64 đặt** (`:685`). Thêm nữa `:856` đã làm bit 64 vô nghĩa khi H2 tắt (`256` set ⇒ luôn `31`) | Cập nhật comment; đánh dấu bit 64 **deprecated**; mục S12 trong §9 **huỷ** |
| N2 | **`C_AVG_DYN` / `L_MAX_DYN` chết ở baseline** | Chúng chỉ dùng để định cỡ ruin (`:360–361`) và chỉ *thay đổi* khi H2 bật (`:857–859`); ở baseline H2 tắt ⇒ hằng số 10/10 | Ghi nhận; hai biến này thuộc H2, hun khói cùng H2 |
| N3 | **Bit 1 (regret) mâu thuẫn nguồn gốc baseline** | Baseline dùng **regret ON** (bit 1 clear), nhưng tài liệu chiến dịch cũ (`archive/docs/CLAUDE_STEP_BY_STEP_VERIFICATION_PLAN.md`, D1) kết luận "50% net-profit + 50% random **không regret**" mới là cấu hình sống còn (+275) | **Cờ đỏ**: baseline có thể *không phải* cấu hình đã được chứng minh. Ghi lại, **không** chạy test theo `test_850`; xử lý cùng S2 trong khung "thêm nhánh, không đổi" |
| N4 | **Bit 1024 (H4) treo** | Không có code ⇒ ngữ nghĩa mask **không đầy đủ** | Ghi rõ trong doc; hoặc xoá bit, hoặc implement |
| N5 | **H1/H2/H3 còn code nhưng vĩnh viễn tắt** | `:278, :357, :856` | Giữ (để tham chiếu), nhưng mọi thay đổi mới phải **đi qua cổng S13**, không "đổi công thức cũ" |

### 11.8. Thứ tự thi công theo BẢO ĐẢM (không theo điểm)

1. **S13 (phân tầng toán tử) + T6** — enabler; bảo đảm duy nhất cần thiết: throughput.
2. **S5 (bỏ kẹp 4×) + T5** — chứng minh cơ chế thích nghi *thực sự thay đổi phân bố*. Rẻ, và sửa một chỗ
   vừa được chứng minh là trang trí.
3. **S4 (route pool + chọn tập, có ràng buộc đơn điệu) + T3** — bảo đảm mạnh nhất; rủi ro thấp nhất về trần.
4. **S2 (liên tuyến) + T2** — siêu tập move; cần S13 để bảo vệ throughput.
5. **S9 (nới candidate) + T2** — rẻ, siêu tập ứng viên.
6. **S1 + S10** (ejecting + conflict-guided) — cùng nhau; T2.
7. **S3** (beam-exact repair) — sau cùng, vì chi phí khó chặn nhất.
8. **S7 (LAHC)**, **S6 (GRASP)**, **S11** — thay/thêm cơ chế ở tầng meta; chỉ làm sau khi I1/I2 đã được phá,
   nếu không sẽ lặp lại đúng sai lầm của v2 (tinh chỉnh một cơ chế trên tín hiệu đã suy biến).
9. **N3 (bit 1 / ordering)** — xử lý riêng như một nhánh *thêm*, kèm cờ đỏ trong doc.

---

## 12. NỘP BASELINE: MERGE `gm_sisr` (mask 896) VÀO `submission.cpp`

### 12.1. Audit hardcode theo `test_850` — SẠCH

So **tập literal số** giữa `gm_sisr.cpp` và `submission.cpp` (bỏ comment): không tồn tại literal nào
dạng `850 / 200 / 50` hay bất kỳ hằng số nào suy từ cỡ instance. Mọi literal khác nhau đều thuộc
một trong ba nhóm hợp lệ:

| Nhóm | Ví dụ | Bản chất |
|---|---|---|
| Hạ tầng ablation | `128/256/512/2048/4096/8192` | bit mask, không phải tham số instance |
| Ngân sách Phase 1 | `80.0`, `3.5`, `0.003`, `4095` | thời gian/trần bộ nhớ |
| Trần dung lượng | `500000000`, `20000000`, `2000`, `200000000` | kích thước, không phải hình học |

Kích thước instance lấy từ input (`K,N,M,R,V`); tham số nhiệt chuẩn hoá qua `S_scale`, `T_bar`, `E_bar`
**suy từ dữ liệu**. Nguồn duy nhất "gắn với `test_850`" là **giả định thời gian** trong comment/`t_max`.

### 12.2. Ngân sách thời gian (chốt)

| | Giá trị | Ghi chú |
|---|---|---|
| Giới hạn kỳ thi | **300s** | theo xác nhận của chủ dự án |
| Tự giới hạn | **201s** | `TIME_LIMIT_SEC` |
| Phase 1 | **60s** | `TIME_LIMIT_PHASE1` (P2 nhận ~141s) |
| `t_max` trong struct | 60.0 | **dead** — luôn bị `sedca.t_max = budget` ghi đè |

> ⚠️ Bản đã commit trước (`f72eb09`) ghi `69.8/10.0` với comment "70s limit" — **lệch** với thực tế.
> Đã sửa lại thống nhất.

### 12.3. Nội dung merge (Phase 2 = đúng cấu hình mask 896; Phase 1 giữ nguyên)

| Đưa vào | Chi tiết |
|---|---|
| Thermal **exponential-tooth** | thay U-valley legacy: đỉnh **không suy giảm**, dwell thích nghi theo `stag`, **không ngâm đáy** |
| **H5 bandit** | `BANDIT_PRIOR{7.5,1.5,1.0}`; `update_bandit()` mỗi 20k vòng; `ruin_guided` đổi sang **roulette** thay `rng()%100` |
| **Tái hiệu chuẩn trực tuyến (T4)** | vòng `recent_delta[128]` + `online_recalibrate()` mỗi 20k vòng; `auto_calibrate` đặt thêm `T_min_base/T_peak_base` |
| `stag` → thermal | `sedca.stag = cycle_stag` (dwell thích nghi) |
| Guard `ks ≥ 1` | chống `ks_max` âm khi `ls_max` lớn (bug tiềm ẩn ở bản cũ) |
| Đếm yield mode | `mode_tries/mode_records` |

### 12.4. Bằng chứng chạy được

Smoke test ngân sách 8s (bản tạm, đã xoá): `EXIT=0`, stdout `30329`,
`validator.py test_850` → **BẢN BÁO CÁO HỢP LỆ**, validator = C++ = 30,329 ⇒ **khớp tuyệt đối** (V2 đóng lại lần nữa).

### 12.5. Điều KHÔNG được suy ra từ merge này

1. Đây là **bản cài lại đúng *cấu hình***, **không** phải bản dịch bit-identical của `gm_sisr` mask 896:
   `submission.cpp` Phase 2 **thừa hưởng nghiệm Phase 1** khi P1 hợp lệ (chỉ rơi về greedy nếu P1 thất bại),
   còn `gm_sisr` **luôn** greedy. Khác biệt này **chưa** được đo ghép cặp (theo chỉ đạo: dừng tune theo `test_850`).
2. **Không kỳ vọng chất lượng tăng.** Theo §10/§11: tooth hoà legacy (5/10), H5 không tổng quát hoá
   ⇒ baseline này là **cấu hình hiện tại**, không phải một bước tiến.
3. **N3 vẫn là cờ đỏ**: baseline dùng *regret ON*, trong khi tài liệu chiến dịch cũ nói *no-regret* mới là
   cấu hình sống còn. Giữ nguyên theo mask 896, **không tự đổi**.
4. Code H1/H2/H3/H4 **không** được đưa sang `submission.cpp` (đúng: chúng đã bị ablation) ⇒ file nộp
   gọn hơn, và mọi thay đổi mới phải đi qua cổng **S13**, không "đổi công thức cũ".
