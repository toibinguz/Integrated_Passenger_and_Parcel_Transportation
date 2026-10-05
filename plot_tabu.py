import matplotlib.pyplot as plt
import re

iterations = []
best_nb_objs = []
global_best_objs = []

current_iter = None
current_best_nb = None

# Đọc file log (dùng errors='ignore' để bỏ qua các ký tự rác nếu powershell ghi UTF-16)
with open("log_tabu.txt", "r", encoding="utf-16", errors='ignore') as f:
    # Nếu utf-16 lỗi, thử utf-8
    try:
        f.read(1)
        f.seek(0)
    except:
        f = open("log_tabu.txt", "r", encoding="utf-8", errors='ignore')

    for line in f:
        # Bỏ qua các dòng rác của PowerShell ErrorRecord
        if "CategoryInfo" in line or "FullyQualifiedErrorId" in line or "NativeCommandError" in line:
            continue
            
        m_iter = re.search(r"\[Iter\s+(\d+)\]", line)
        if m_iter:
            current_iter = int(m_iter.group(1))
            current_best_nb = None
            
        m_nb = re.search(r"-> Best Neighbor Obj:\s*(-?\d+)", line)
        if m_nb:
            current_best_nb = int(m_nb.group(1))
            
        m_glob = re.search(r"-> Global Best Obj:\s*(-?\d+)", line)
        if m_glob and current_iter is not None:
            if current_best_nb is not None:
                iterations.append(current_iter)
                best_nb_objs.append(current_best_nb)
                global_best_objs.append(int(m_glob.group(1)))

plt.figure(figsize=(12, 6))
plt.plot(iterations, best_nb_objs, label='Best Neighbor Obj', alpha=0.5, color='blue', linewidth=1)
plt.plot(iterations, global_best_objs, label='Global Best Obj', color='red', linewidth=2)
plt.xlabel('Iterations')
plt.ylabel('Objective Value (Benefit)')
plt.title('Tabu Search Objective Progression')
plt.legend()
plt.grid(True, linestyle='--', alpha=0.6)
plt.tight_layout()

plt.savefig('tabu_progression.png', dpi=150)
print("Đã vẽ biểu đồ và lưu vào file tabu_progression.png")
# plt.show() # Uncomment dòng này nếu muốn popup hiện lên
