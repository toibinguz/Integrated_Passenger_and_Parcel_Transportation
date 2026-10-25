# BASELINE CHẾ ĐỘ NHIỆT SAIRL ADAPTIVE REHEATING (30,586 TRÊN TEST_850)

## 1. Số liệu Thực nghiệm Baseline Hiện tại (`run_test_850_s42`, 10.0s, Seed 42)

* **Instance**: `testcases/legacy_perturbation/test_850.txt` ($K=50, N=200, M=200, V=850$)
* **Wall-clock Budget**: `10.0s`
* **Random Seed**: `42`
* **Initial Greedy Obj**: `23,252`
* **Final Best Objective**: **`30,586`** (Verified: `30,586`, Net Improvement: **`+7,334` / `+31.5%`**)
* **Total Iterations**: `421,664` (`42,164 iter/s`)
* **Total Accepted Moves**: `117,887` (`27.96%`)
* **Neutral Moves ($\Delta = 0$)**: `115,219` (`27.32%`)
* **True Worsening Accept ($\Delta < 0$)**: `1,239 / 305,016` (`0.41%`)
* **Local Improvements**: `1,191`
* **Global Records**: `238`
* **Learned Scale $\mu_\Delta$ (`mean_delta`)**: `8.72` (`Samples = 10,603`, `Reheats = 6`, `Restores = 0`)
* **Final $B_{\text{sat}}$ & Temp**: `B_sat = 30,586.0` | `Final T = 0.80`
* **Sort Records (0/1)**: `138 / 100`

---

## 2. Cấu hình Tham số Baseline (`SAIRLThermostat` đạt `30,586`)

```cpp
struct SAIRLThermostat {
    double T_MAX      = 100.0;   // Trần nhiệt tối đa
    double T_MIN      = 0.80;    // Sàn nhiệt hội tụ sâu
    int    K_MAX      = 28000;   // Ngưỡng kiên nhẫn (số bước lặp chu kỳ trước khi Reheat)
    double COOL_RATE  = 0.99985; // Hệ số làm lạnh hình học mỗi bước

    // Khởi tạo:
    // mean_delta = clamp(0.06 * scale_S, 12.0, 25.0);
    // T          = max(14.0, 1.05 * mean_delta);

    // 1. Học thang độ lệch bước xấu cấu trúc (không rớt khách):
    // if (delta < 0 && r_delta >= 0 && -delta <= 3.0 * mean_delta)
    //     mean_delta += ((-delta) - mean_delta) / min(++n_worsen, 500LL);

    // 2. Gia hạn kiên nhẫn tỷ lệ khi phá kỷ lục toàn cục:
    // if (cur_obj >= best_obj && delta > 0) {
    //     stag_iters   = (int)(stag_iters * 0.9);
    //     reheat_count = 0;
    // } else {
    //     ++stag_iters;
    // }

    // 3. Làm lạnh hình học mỗi bước:
    // T = max(T_MIN, T * COOL_RATE);

    // 4. Kích hoạt Reheat khi stag_iters >= K_MAX và tau < 0.88:
    // double reheat_peak = mean_delta * (3.20 + 0.80 * log(1.0 + reheat_count));
    // T = clamp(reheat_peak, T_MIN * 4.0, T_MAX * (1.0 - 0.25 * tau));
    // if (best_obj - cur_obj > (ll)(12.0 * mean_delta)) need_load_best = true;
};
```

---

## 3. Hệ Công Thức Suy Dẫn Các Magic Number Từ Tham Số Điều Khiển Vật Lý

Trong bản baseline `30,586`, các hằng số (`T_MAX = 100.0`, `T_MIN = 0.80`, `K_MAX = 28000`, `COOL_RATE = 0.99985`, `3.20`, `0.80`, `0.90`, `0.88`, `12.0`) đang được gán trực tiếp nhưng thực chất **phụ thuộc chặt chẽ vào nhau**. Nếu thay đổi độc lập 1 hằng số (ví dụ đổi `K_MAX` mà quên tính lại `COOL_RATE`, hoặc đổi bài toán có $\mu_\Delta$ và tốc độ vòng lặp $\nu_{\text{iter}}$ khác), hình học của sóng nhiệt sẽ bị phá vỡ.

Dưới đây là hệ phương trình suy dẫn toàn bộ các hằng số trên từ **6 Tham số Điều khiển Vật lý Không thứ nguyên (Dimensionless Control Knobs)**:

### 3.1. Bảng Ánh Xạ Từ Tham Số Điều Khiển Sang Các Hằng Số Thực Thi

| Tham số điều khiển cấp cao (Control Knob) | Ký hiệu | Giá trị chuẩn (khớp Baseline `30,586`) | Ý nghĩa vật lý trực tiếp |
| :--- | :---: | :---: | :--- |
| **1. Số chu kỳ sóng mục tiêu** (hoặc thời lượng 1 sóng $\Delta t_{\text{wave}}$) | $N_{\text{waves}}$ (hoặc $\Delta t_{\text{wave}}$) | $\Delta t_{\text{wave}} \approx 0.66\text{s}$ ($N_{\text{waves}} \approx 15$ sóng cơ sở) | Quyết định độ dài cơ sở của 1 chu kỳ sóng $K_{\text{MAX}}$ theo tốc độ thực thi $\nu_{\text{iter}}$ (iter/s). |
| **2. Tỷ phần Pha Dốc Làm Lạnh trong 1 sóng** | $\rho_{\text{cool}}$ | **`0.884`** ($88.4\%$ dốc làm lạnh, $11.6\%$ ngâm đáy) | Tỷ lệ phần trăm bước lặp trong $K_{\text{MAX}}$ dùng để hạ nhiệt từ $T_{\text{peak}}$ chạm sàn $T_{\min}$. |
| **3. Tỷ số Nhiệt độ Sàn Kết tinh** (hoặc xác suất bước xấu vi mô ở đáy) | $\eta_{\min}$ | **`0.0917`** ($T_{\min} / \mu_\Delta$) | Xác định độ sâu kết tinh ở đáy sóng theo thang năng lượng $\mu_\Delta$. |
| **4. Xác suất Metropolis mở cửa tại Đỉnh Bơm Nhiệt cơ sở** | $p_{\text{base}}, p_{\text{step}}, p_{\max}$ | $p_{\text{base}} = 73.1\%$, $p_{\max} = 91.7\%$ | Xác suất chấp nhận một bước xấu chuẩn $\Delta = -\mu_\Delta$ ngay tại đỉnh bơm nhiệt. |
| **5. Ngưỡng mật độ kỷ lục giữ sóng** (Harvest Retention Ratio) | $\theta_{\text{harvest}}$ | **`0.111`** ($1/9 \approx 11.1\% K_{\text{MAX}}$) | Nếu khoảng cách giữa 2 lần phá kỷ lục toàn cục liên tiếp $< \theta_{\text{harvest}} K_{\text{MAX}}$, sóng được giữ lại để vắt kiệt chuỗi kỷ lục. |
| **6. Bán kính Dung sai Trôi dạt Lưu vực** (Basin Drift Tolerance) | $\kappa_{\text{drift}}$ | **`12.0`** ($12 \mu_\Delta$) | Độ tụt tối đa dưới `best_obj` được phép giữ lại khi bước sang sóng mới mà không bị kéo về `best_obj`. |

---

### 3.2. Các Công Thức Suy Dẫn Tường Minh (Exact Derivation Formulas)

Gọi $\mu_\Delta$ (`mean_delta`) là chi phí bước xấu cấu trúc trung bình tự học ($\Delta < 0, r_\Delta \ge 0$), và $\nu_{\text{iter}}$ là tốc độ vòng lặp/giây thực tế:

#### (1) Công thức suy ra Ngưỡng kiên nhẫn $K_{\text{MAX}}$ từ Thời lượng sóng cơ sở $\Delta t_{\text{wave}}$:
$$K_{\text{MAX}} = \text{round}\big(\nu_{\text{iter}} \cdot \Delta t_{\text{wave}}\big)$$
*(Kiểm chứng Baseline: Với $\nu_{\text{iter}} \approx 42,164 \text{ iter/s}$ và $\Delta t_{\text{wave}} = 0.664\text{s} \implies K_{\text{MAX}} = 28,000$ bước).*

#### (2) Công thức suy ra Sàn nhiệt $T_{\min}$, Trần nhiệt $T_{\max}$ và Đỉnh Bơm Nhiệt $T_{\text{peak}}(r)$ từ Xác suất Metropolis:
Theo phân phối Boltzmann $\exp(-\mu_\Delta / T) = p \iff T = \frac{\mu_\Delta}{-\ln(p)}$:
* **Sàn kết tinh $T_{\min}$**:
  $$T_{\min} = \eta_{\min} \cdot \mu_\Delta \quad \Big(\text{với } \eta_{\min} \approx 0.0917 \implies \text{khi } \mu_\Delta = 8.72, \, T_{\min} = 0.80\Big)$$
* **Hệ số đỉnh bơm nhiệt cơ sở $\gamma_0$ và hệ số leo thang Azizi $\beta_{\text{azizi}}$**:
  $$\gamma_0 = \frac{1}{-\ln(p_{\text{base}})}, \qquad \beta_{\text{azizi}} = \frac{1}{\ln 2}\left(\frac{1}{-\ln(p_1)} - \gamma_0\right)$$
  $$\Longrightarrow \quad T_{\text{peak}}(r) = \mu_\Delta \cdot \Big(\gamma_0 + \beta_{\text{azizi}} \ln(1 + r)\Big)$$
  *(Kiểm chứng Baseline: Với $p_{\text{base}} = 0.7316 \implies \gamma_0 = 3.20$; với $p_1 = 0.7662 \implies \gamma_1 = 3.7545 \implies \beta_{\text{azizi}} = 0.80$. Tại $\mu_\Delta = 8.72 \implies T_{\text{peak}}(1) = 32.74$).*
* **Trần nhiệt tối đa $T_{\max}$**:
  $$T_{\max} = \gamma_{\max} \cdot \mu_\Delta = \frac{\mu_\Delta}{-\ln(p_{\max})} \quad \Big(\text{với } p_{\max} = 0.9166 \implies \gamma_{\max} = 11.47 \implies \text{khi } \mu_\Delta = 8.72, \, T_{\max} \approx 100.0\Big)$$

#### (3) Công thức khóa pha Hệ số Làm lạnh `COOL_RATE` theo $(K_{\text{MAX}}, \rho_{\text{cool}}, \gamma_1, \eta_{\min})$:
Để đảm bảo sườn dốc làm lạnh từ $T_{\text{peak}}(1) = \gamma_1 \mu_\Delta$ xuống chạm sàn $T_{\min} = \eta_{\min} \mu_\Delta$ **luôn chiếm đúng tỷ phần $\rho_{\text{cool}}$ của chu kỳ $K_{\text{MAX}}$** (và phần ngâm phẳng ở đáy $T_{\min}$ luôn chiếm đúng $1 - \rho_{\text{cool}}$):
$$T_{\text{peak}}(1) \cdot (\text{COOL\_RATE})^{\rho_{\text{cool}} \cdot K_{\text{MAX}}} = T_{\min}$$
$$\Longrightarrow \quad \boxed{\text{COOL\_RATE} = \left(\frac{\eta_{\min}}{\gamma_1}\right)^{\frac{1}{\rho_{\text{cool}} \cdot K_{\text{MAX}}}}} \quad \text{hoặc tổng quát ở mỗi sóng } r: \quad \text{COOL\_RATE}(r) = \left(\frac{T_{\min}}{T_{\text{peak}}(r)}\right)^{\frac{1}{\rho_{\text{cool}} \cdot K_{\text{MAX}}}}$$
*(Kiểm chứng Baseline: Với $\eta_{\min} = 0.0917$, $\gamma_1 = 3.7545$, $\rho_{\text{cool}} = 0.884$, $K_{\text{MAX}} = 28,000$:*
$$\text{COOL\_RATE} = \left(\frac{0.0917}{3.7545}\right)^{\frac{1}{0.884 \times 28000}} = (0.02443)^{\frac{1}{24752}} = \mathbf{0.9998500}!$$
*Nhờ công thức này, khi thay đổi `K_MAX` hoặc `T_peak`, `COOL_RATE` tự động co giãn theo hàm mũ để không bao giờ xảy ra hiện tượng "chưa kịp chạm đáy đã Reheat" hay "nằm chết ở đáy quá lâu").*

#### (4) Công thức suy ra Hệ số Gia hạn Kỷ lục $\alpha_{\text{extend}}$ (`0.90`) từ Mật độ Thu hoạch $\theta_{\text{harvest}}$:
Khi đang ở cuối sóng ($k \approx K_{\text{MAX}}$), nếu xuất hiện chuỗi phá kỷ lục toàn cục cách nhau trung bình $\Delta k$ bước lặp, điểm bất động của phép lùi $k \leftarrow \alpha_{\text{extend}} (k + \Delta k)$ là $k^* = \frac{\alpha_{\text{extend}}}{1 - \alpha_{\text{extend}}} \Delta k$. Để giữ sóng không bị ngắt khi $\Delta k \le \theta_{\text{harvest}} K_{\text{MAX}}$ ($k^* \le K_{\text{MAX}}$):
$$\boxed{\alpha_{\text{extend}} = \frac{1}{1 + \theta_{\text{harvest}}}}$$
*(Kiểm chứng Baseline: Muốn giữ sóng khi khoảng cách giữa 2 kỷ lục liên tiếp $\le \theta_{\text{harvest}} = \frac{1}{9} \approx 11.1\%$ độ dài sóng $\implies \alpha_{\text{extend}} = \frac{1}{1 + 1/9} = \mathbf{0.90}$).*

#### (5) Công thức suy ra Điểm ngắt Bơm nhiệt cuối giờ $\tau_{\text{cutoff}}$ (`0.88`):
Để sóng cuối cùng luôn có đủ thời gian hoàn tất toàn bộ pha làm lạnh + pha kết tinh ($\approx c_{\text{final}}$ lần thời lượng một sóng thực tế $\Delta t_{\text{wave\_actual}}$, với $c_{\text{final}} \approx 1.0$):
$$\boxed{\tau_{\text{cutoff}} = 1.0 - \frac{c_{\text{final}} \cdot \Delta t_{\text{wave\_actual}}}{T_{\text{budget}}}}$$
*(Kiểm chứng Baseline: Trên `test_850` với $T_{\text{budget}} = 10.0\text{s}$ và 1 sóng thu hoạch trung bình dài $\Delta t_{\text{wave\_actual}} \approx 1.2\text{s} \implies \tau_{\text{cutoff}} = 1.0 - \frac{1.2}{10.0} = \mathbf{0.88}$).*
