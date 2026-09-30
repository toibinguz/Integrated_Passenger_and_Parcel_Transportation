import re

with open('solver_ls.py', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Turn off TABU_TENURE
content = re.sub(r'TABU_TENURE\s*=\s*\d+', 'TABU_TENURE = 0', content)

# 2. Fix first block (line ~505)
old_block_1 = "            active_tabu_edges = set([edge for edge, exp in tabu_dict.items() if exp >= it])"
new_block_1 = """            # Xóa các cạnh đã hết hạn khỏi dictionary để tránh rò rỉ bộ nhớ
            expired_edges = [edge for edge, exp in tabu_dict.items() if exp < it]
            for edge in expired_edges:
                del tabu_dict[edge]
            active_tabu_edges = set(tabu_dict.keys())"""
content = content.replace(old_block_1, new_block_1)

# 3. Fix second block (line ~593)
old_block_2 = "            active_tabu_edges_next = set([edge for edge, exp in tabu_dict.items() if exp >= it])"
new_block_2 = """            # Ở pha này tabu_dict chỉ vừa thêm đồ mới vào (chưa bị quá hạn thêm), nên lấy toàn bộ
            active_tabu_edges_next = set(tabu_dict.keys())"""
content = content.replace(old_block_2, new_block_2)

with open('solver_ls.py', 'w', encoding='utf-8') as f:
    f.write(content)
