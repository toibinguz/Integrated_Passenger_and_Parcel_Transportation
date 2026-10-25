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
