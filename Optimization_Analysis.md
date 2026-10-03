# Phân tích Cấu trúc dữ liệu và Hiệu năng trong `solver_full.cpp`

Trong quá trình chuyển đổi và tối ưu hoá, mã nguồn hiện tại đang lạm dụng khá nhiều cấu trúc dữ liệu dựa trên Hash (như `std::unordered_map`, `std::unordered_set`) và Tree (như `std::map`, `std::set`). 

Dù trong lý thuyết, Hash Table có độ phức tạp trung bình là `O(1)`, nhưng trong thực tế (đặc biệt là với C++ trong các bài toán Competitive Programming và Meta-Heuristics), chúng mang lại một lượng **Overhead khổng lồ** do:
1. Chi phí băm (Hashing).
2. Xung đột bộ nhớ và cấp phát động liên tục (Dynamic Memory Allocation & Pointer Chasing).
3. Cache Misses do các node nằm rải rác trên RAM (thay vì nằm liền kề trên Cache CPU).

Đa số các ID (chẳng hạn như `node_id`, `job_id`, `route_idx`) trong bài toán này đều liên tục, bị giới hạn (Bounded) và tương đối nhỏ. Việc thay thế bằng các cấu trúc **Mảng phẳng (Flat Array / std::vector)** sẽ mang lại O(1) thực sự với tốc độ truy xuất cực nhanh (Cache-friendly).

Dưới đây là phân tích chi tiết các điểm nghẽn (Bottlenecks) và hướng tối ưu:

## 1. Tabu List: `map<pair<int, int>, int> tabu_dict`
- **Vị trí**: Nằm trong hàm `LSSolver::solve` và `LSSolver::best_insert`.
- **Cách hoạt động**: Lưu trữ cặp cạnh cấm `(u, v)` (được mã hoá dưới dạng `pair<int, int>`) và vòng lặp hết hạn. 
- **Tại sao nó tệ**: `std::map` được cài đặt bằng Cây Đỏ-Đen (Red-Black Tree). Truy xuất mất `O(log N)` với vô số bước nhảy con trỏ. Hơn nữa, nó được gọi hàng triệu lần trong hàm `evaluate_insertion` để kiểm tra cạnh cấm. Điều này bóp nghẹt tốc độ chạy của thuật toán.
- **Giải pháp**: Vì số lượng đỉnh `node_id` tối đa là `V_mac` (khoảng vài trăm hoặc vài ngàn), ta có thể sử dụng một mảng 2 chiều phẳng `vector<vector<int>> tabu_matrix(V_mac, vector<int>(V_mac, 0))`. Việc kiểm tra hay cập nhật chỉ tốn duy nhất 1 phép tính địa chỉ mảng `O(1)`, nhanh gấp hàng chục lần.

## 2. DP Bitmask Memoization: `unordered_map<int, vector<pair<int, int>>> memo`
- **Vị trí**: Trong hàm `get_valid_routes` (Operator X).
- **Cách hoạt động**: Memoization cho state trong quá trình DFS Bitmask. Key là `(mask << 5) | (u_idx + 1)`.
- **Tại sao nó tệ**: `unordered_map` cấp phát động một Node mới cho mỗi Key sinh ra. Việc tính toán hàm Hash và dò Linked List / Probing rất tốn kém khi gọi đệ quy sâu.
- **Giải pháp**: Số lượng `u_idx` thường `<= 18` và `mask < 2^18`. Dù không gian tối đa khá lớn, nhưng vì DP Bitmask trong bài này chỉ chạy trên những cụm nodes rất nhỏ (Pool Size thường < 12), ta có thể dùng một `vector<vector<pair<int, int>>>` (Flat Array) cấp phát trước để làm bảng DP `O(1)`.

## 3. Quản lý Route Caches: `unordered_map<int, unordered_map<int, InsertCacheRecord>> insert_cache`
- **Vị trí**: Bộ nhớ đệm (Cache) của thuật toán Greedy Insertion `LSSolver::best_insert`.
- **Cách hoạt động**: Bản đồ lồng nhau. Ánh xạ `node_id -> route_idx -> InsertCacheRecord`.
- **Tại sao nó tệ**: Cực kỳ thảm hoạ về bộ nhớ. Để tra cứu một cache, nó phải đi qua 2 lần tính Hash và 2 lần đối chiếu (Pointer Chasing). Quá trình dọn rác (GC) hoặc xoá cache cũng rất chậm chạp.
- **Giải pháp**: `node_id` luôn `< V_mac` và `route_idx` luôn `< K`. Cấu trúc này phải được làm phẳng thành 1 mảng 2 chiều tĩnh: `vector<vector<InsertCacheRecord>> insert_cache(V_mac, vector<InsertCacheRecord>(K))`. Khi cần đánh dấu Invalid, chỉ cần thêm 1 trường `bool is_valid` thay vì xoá khỏi Map.

## 4. Kiểm tra Routes bị thay đổi: `unordered_set<int> clean_routes`
- **Vị trí**: Quản lý Tabu/Or-Opt K1.
- **Cách hoạt động**: Lưu ID của các xe (`route_idx`) chưa bị thay đổi.
- **Tại sao nó tệ**: Cấp phát động từng số nguyên vào cấu trúc Hash.
- **Giải pháp**: Rất đơn giản, dùng `vector<bool> is_clean(K, true)`. Truy xuất và cập nhật siêu tốc `O(1)`.

## 5. Danh mục Tra cứu (Dictionaries): `parcel_dict` & `req_dict`
- **Vị trí**: Khởi tạo của `LSSolver`.
- **Cách hoạt động**: `unordered_map<int, pair<GenericNode*, GenericNode*>> parcel_dict` dùng để tra cứu nhanh thông tin từ `job_id`.
- **Tại sao nó tệ**: `job_id` là một chuỗi số nguyên tăng dần liên tục và bị giới hạn (chẳng hạn từ 1 đến N+M).
- **Giải pháp**: Biến nó thành mảng tra cứu trực tiếp `vector<pair<GenericNode*, GenericNode*>> parcel_array(MAX_JOB_ID + 1)`.

## 6. Lưu trữ Đường đi tốt nhất trong DP: `map<int, PathResult> best_routes`
- **Vị trí**: Hàm DP của Operator X.
- **Cách hoạt động**: Lưu kết quả tốt nhất của một `req_mask`.
- **Tại sao nó tệ**: Giống như Tabu List, dùng Cây để lưu cấu hình. 
- **Giải pháp**: `req_mask` là bitmask đại diện cho số lượng request (thường <= 9). Kích thước tối đa là 512. Chỉ cần khởi tạo mảng `vector<PathResult> best_routes(1024)` với giá trị ban đầu (Benefit cực âm).

## 7. Các danh sách đánh dấu: `unordered_set<int> served` / `to_remove`
- **Vị trí**: Khắp nơi (Random Perturbation, Served Rebuild, Operator X).
- **Cách hoạt động**: Chứa `job_id` của các hành khách/hàng hoá.
- **Tại sao nó tệ**: Tốn chi phí cấp phát và huỷ bộ nhớ liên tục trong mỗi vòng lặp ILS.
- **Giải pháp**: Tái sử dụng (Reuse) một `vector<bool> is_served(MAX_JOB_ID + 1, false)` toàn cục kết hợp với `vector<int> served_list`. Khi cần Reset, chỉ cần loop qua `served_list` để trả `is_served` về `false` (Tránh `std::fill` toàn mảng). Kỹ thuật này được gọi là **Sparse Set** hoặc **O(1) Array Clearing**.

---

### Tổng kết
C++ có sức mạnh tính toán kinh khủng, nhưng tốc độ hiện tại của mã nguồn (ví dụ: mất 0.25 giây cho 100,000 vòng lặp ở Test 10) hoàn toàn có thể **giảm xuống còn 0.05 giây hoặc ít hơn** nếu gỡ bỏ toàn bộ các nút thắt cổ chai (Bottlenecks) liên quan đến cấp phát bộ nhớ động của Map và Set. Việc quy hoạch lại ID để sử dụng Index trực tiếp trên mảng là chìa khoá cho tối ưu hoá cấp thấp (Low-level Optimization) trong Competitive Programming.
