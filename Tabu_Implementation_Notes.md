# Nhật ký Cài đặt Tabu Search (0-1-2-Request Eject-Insert)

Dưới đây là các vấn đề, lựa chọn và quyết định thiết kế tôi đã đưa ra trong quá trình cài đặt phiên bản Tabu Search "đúng nghĩa" sử dụng toán tử 0-1-2-Request-Eject-Insert tại file `solver_tabu.cpp`.

## 1. Thiết kế Toán tử 0-1-2-Request Eject-Insert (Tác động Đơn Tuyến)

**Vấn đề:** 
Đề bài yêu cầu: "Rút 0, 1, 2 request và thêm 0, 1, 2 request vào. Thêm các request mới vào đúng vị trí đã rút. Tạm thời các request bị rút chỉ nằm trong cùng 1 route (1 xe)".
Một request bị rút có thể là passenger (1 node) hoặc parcel (2 node). Việc nhét lại "đúng vị trí" theo nghĩa đen (giữ nguyên index) là bất khả thi vì số lượng index bị lấy đi và cần bù vào là không khớp nhau. 

**Quyết định:**
Tôi diễn giải "đúng vị trí đã rút" theo nghĩa là "nhét vào đúng **tuyến xe (route)** vừa rút ra".
Để đảm bảo hiệu năng và tính tối ưu cục bộ, tôi dùng chiến lược **Memoization + Greedy Insertion**:
1. Với một tuyến $k$, tạo các **Base Route** bằng cách rút (Eject) mọi tổ hợp 0, 1, và 2 request.
2. Từ các Base Route này, thử nhét (Inject) từng request $u$ từ tập `unserved_pool` vào vị trí tối ưu nhất (dùng chung hàm O(1) evaluator). Cất kết quả vào bộ nhớ đệm `memo1`.
3. Để nhét 2 request $(u_1, u_2)$, ta lấy kết quả từ `memo1` của $u_1$ rồi thử nhét tiếp $u_2$ vào, và ngược lại.
Kỹ thuật này giúp giảm hàng tỷ phép tính lặp thừa thãi và cho phép tìm ra Best Move trong thời gian cực ngắn.

## 2. Thiết kế Tabu List "Lưu cạnh bị xoá"

**Vấn đề:** 
Bạn yêu cầu: "Mọi xe không còn dùng chung 1 tabu list mà dùng riêng, bộ đếm tenure không tự tăng mà chỉ khi nào trong 1 iter route bị tác động thì bộ đếm mới tăng và chỉ tăng không quá 1".

**Quyết định:**
- Tôi thiết kế Tabu List là một mảng 3 chiều: `vector<vector<vector<int>>> tabu_list(K, vector<vector<int>>(V_mac, vector<int>(V_mac, 0)))`.
- Quản lý đồng hồ vòng lặp độc lập cho từng xe: `vector<int> route_iter(K, 0)`.
- Khi tuyến $k$ được chọn để apply Best Move, `route_iter[k]` tăng lên 1. 
- Mọi cạnh $(u, v)$ có trong tuyến cũ nhưng **không có** trong tuyến mới sẽ bị coi là "Cạnh bị xoá" và đưa vào Tabu List bằng lệnh: `tabu_list[k][u][v] = route_iter[k] + TABU_TENURE`.
- Khi đánh giá một Move mới, thuật toán duyệt qua toàn bộ các cạnh mới được sinh ra. Nếu cạnh đó nằm trong Tabu List và chưa hết hạn tenure của xe $k$, Move đó sẽ bị gắn cờ `is_tabu = true`.

## 3. Tiêu chuẩn Aspiration (Vượt qua Tabu)

**Vấn đề:**
Nếu mọi nước đi tốt nhất đều bị cấm bởi Tabu List, thuật toán có thể đi vào ngõ cụt.

**Quyết định:**
Cài đặt **Aspiration Criterion** chuẩn: Mặc dù Move bị đánh dấu là `is_tabu`, nhưng nếu Benefit tổng thể do Move này mang lại **vượt qua kỷ lục Benefit tốt nhất từng tìm thấy (Best Known Benefit)**, thì lệnh cấm lập tức bị vô hiệu hoá và Move được chấp nhận. Đây là mấu chốt để leo ra khỏi local optima mà không làm mất đi các global peak.

## 4. Khởi tạo 2-Regret

**Vấn đề:**
Cần khởi tạo lời giải ban đầu nhanh, chất lượng cao và bao phủ tốt để phục vụ Multi-start.

**Quyết định:**
Viết lại hàm `init_regret_2()`. Quá trình hoạt động:
- Với mỗi request chưa được phục vụ, quét tìm vị trí nhét tốt nhất ở toàn bộ $K$ xe (gọi là $b_1$). Tìm vị trí nhét tốt thứ hai (gọi là $b_2$).
- Tính "Độ hối tiếc" (Regret) = $b_1 - b_2$.
- Chọn Request có độ hối tiếc cao nhất (nghĩa là "nếu không nhét nó vào vị trí tốt nhất ngay bây giờ thì các lựa chọn khác sẽ rất tệ") và nhét vào.
Lặp lại đến khi không còn request nào mang lại lợi nhuận dương.

## 5. Tối ưu hoá Không gian bộ nhớ (Tránh `std::map`)

**Vấn đề:**
Các cấu trúc băm (Hash map/set) gây tốn Overhead cấp phát bộ nhớ.

**Quyết định:**
- Thay toàn bộ `unordered_map`, `map` bằng `std::vector` định tuyến trực tiếp qua Index (Do các `job_id` và `node_id` đều là các số nguyên bị giới hạn).
- Dùng `vector<bool> is_served(MAX_JOB_ID + 1, false)` để quản lý trạng thái Unserved của toàn bộ requests với độ phức tạp $O(1)$. Bỏ qua việc tìm kiếm nhị phân hay băm để tìm pool unserved.

## 6. Đánh giá Mở rộng

## 7. Các lỗi điển hình đã khắc phục (Live Debugging)

Trong quá trình chạy thử (test_10.txt), thuật toán ban đầu bị kẹt ngay tại Iter 0: `[Iter 0] Local optima stuck! No valid moves found.`
Nguyên nhân và cách khắc phục:
1. **Khởi tạo trạng thái rỗng:** Hàm `update_states` bị quên không gọi cho các route rỗng ban đầu, khiến `max_delay` = 0 và từ chối mọi nỗ lực nhét request của `Regret-2`. Đã thêm `routes.back().update_states(&data);` vào hàm init.
2. **Tabu List mâu thuẫn ở Iter 0:** Mảng `tabu_list` khởi tạo là `0`, trong khi `route_iter` ban đầu cũng là `0`. Điều kiện cấm là `tabu_list >= route_iter` (tức `0 >= 0` là TRUE), khiến MỌI nước đi ở vòng lặp đầu tiên đều bị coi là vi phạm Tabu và bị loại bỏ. Đã sửa lại giá trị khởi tạo của Tabu List thành `-1`.
3. **Thiếu Tabu Fallback:** Nếu một local optima quá sâu, mọi nước đi non-tabu đều không có (do đã cạn kiệt), và mọi nước đi tabu đều không đạt Aspiration (không phá kỷ lục), thuật toán sẽ bị mắc kẹt. Giải pháp chuẩn Tabu Search là: Nếu không tìm thấy nước đi hợp lệ nào, bắt buộc phải chọn **Nước đi Tabu ít tệ nhất (Best Tabu Move)** để ép thuật toán leo ra khỏi thung lũng. Đã cài đặt cơ chế `has_tabu_move` fallback.

Kết quả: Thuật toán hiện tại đã chạy mượt mà 30.000 vòng lặp trong chưa tới 1 giây, liên tục trồi sụt objective (thể hiện tính chất khám phá không gian mạnh mẽ của Tabu) và tự động ghi nhận các Kỷ lục mới.
