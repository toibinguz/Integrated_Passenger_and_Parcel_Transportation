import os
import sys
import subprocess
import time
import io
import re

# Đảm bảo console UTF-8 trên Windows
if sys.stdout.encoding != 'utf-8':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
if sys.stderr.encoding != 'utf-8':
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

def check_and_compile(cpp_file="tabu_prototype.cpp", exe_file="tabu_prototype.exe"):
    """Tự động biên dịch C++ với -O3 nếu file .exe chưa có hoặc file .cpp mới hơn"""
    if not os.path.exists(cpp_file):
        print(f"[-] Không tìm thấy file mã nguồn: {cpp_file}")
        return False
    
    need_compile = False
    if not os.path.exists(exe_file):
        need_compile = True
    elif os.path.getmtime(cpp_file) > os.path.getmtime(exe_file):
        need_compile = True
        
    if need_compile:
        print(f"[i] Đang biên dịch {cpp_file} -> {exe_file} với cờ tối ưu -O3...")
        cmd = ["g++", "-O3", cpp_file, "-o", exe_file]
        res = subprocess.run(cmd)
        if res.returncode != 0:
            print("[-] Lỗi biên dịch C++!")
            return False
        print("[+] Biên dịch C++ thành công!\n")
    return True

def run_pipeline(testcase_path="testcases/test_65.txt", max_iter=4000, cpp_exe="tabu_prototype.exe"):
    print("=" * 75)
    print("                BỘ ĐIỀU PHỐI TỔNG LỰC: SOLVER + PLOT + REPORT")
    print("=" * 75)
    print(f"  * Testcase         : {testcase_path}")
    print(f"  * Số thế hệ (Iter) : {max_iter}")
    print(f"  * Solver Executable: {cpp_exe}")
    print("=" * 75)

    if not os.path.exists(testcase_path):
        print(f"[-] File testcase không tồn tại: {testcase_path}")
        return

    # 1. Tự động kiểm tra và biên dịch solver nếu cần
    cpp_source = cpp_exe.replace(".exe", ".cpp")
    if os.path.exists(cpp_source):
        if not check_and_compile(cpp_source, cpp_exe):
            return

    # 2. Khởi chạy Solver C++ và stream tiến trình trực tiếp ra terminal
    print("[1/3] Đang chạy Tabu Search Solver...")
    t_start = time.time()
    
    proc = subprocess.Popen(
        [os.path.abspath(cpp_exe), testcase_path, str(max_iter)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding='utf-8',
        errors='replace',
        bufsize=1
    )

    output_dir = None
    solution_file = None
    global_best_obj = None

    for line in proc.stdout:
        print(line, end='', flush=True)
        # Bắt thông tin thư mục output và file kết quả
        m_dir = re.search(r"Output Dir\s*:\s*(.*)", line)
        if m_dir:
            output_dir = m_dir.group(1).strip()
            
        m_res = re.search(r"Result File\s*:\s*(.*)", line)
        if m_res:
            solution_file = m_res.group(1).strip()

        m_obj = re.search(r"Global Best Objective\s*:\s*(-?\d+)", line)
        if m_obj:
            global_best_obj = int(m_obj.group(1))

    proc.wait()
    t_solver = time.time() - t_start

    if proc.returncode != 0:
        print(f"\n[-] Solver kết thúc với mã lỗi: {proc.returncode}")
        return

    print(f"\n[+] Solver hoàn tất trong {t_solver:.2f}s!")

    # 3. Định vị thư mục output nếu chưa bắt được qua stdout
    if not output_dir or not os.path.exists(output_dir):
        if os.path.exists("output"):
            subdirs = [os.path.join("output", d) for d in os.listdir("output") if os.path.isdir(os.path.join("output", d))]
            if subdirs:
                output_dir = max(subdirs, key=os.path.getmtime)

    if not output_dir or not os.path.exists(output_dir):
        print("[-] Không xác định được thư mục output để vẽ biểu đồ!")
        return

    # 4. Tự động gọi plot_tabu.py để vẽ đồ thị và xuất tour report
    print("\n" + "=" * 75)
    print("[2/3] Gọi plot_tabu.py xử lý thư mục output...")
    print("=" * 75)
    
    cmd_plot = [sys.executable, "plot_tabu.py", output_dir, testcase_path]
    subprocess.run(cmd_plot, check=True)

    # 5. Tự động kiểm tra tính hợp lệ bằng validator.py (nếu có)
    if os.path.exists("validator.py") and solution_file and os.path.exists(solution_file):
        print("\n" + "=" * 75)
        print("[3/3] Chạy kiểm định độc lập bằng validator.py...")
        print("=" * 75)
        cmd_val = [sys.executable, "validator.py", testcase_path, solution_file]
        subprocess.run(cmd_val)

    print("\n" + "=" * 75)
    print("                  TỔNG KẾT HOÀN TẤT TOÀN BỘ QUY TRÌNH")
    print("=" * 75)
    print(f"  * Điểm số kỷ lục (Objective) : {global_best_obj}")
    print(f"  * Tổng thời gian tìm kiếm    : {t_solver:.2f}s")
    print(f"  * Toàn bộ dữ liệu nằm tại    : {os.path.abspath(output_dir)}")
    print("=" * 75 + "\n")

if __name__ == '__main__':
    # Hỗ trợ truyền tham số: python run.py [testcase] [max_iter] [solver_exe]
    tc = sys.argv[1] if len(sys.argv) > 1 else "testcases/test_65.txt"
    it = int(sys.argv[2]) if len(sys.argv) > 2 else 4000
    exe = sys.argv[3] if len(sys.argv) > 3 else "tabu_prototype.exe"

    run_pipeline(tc, it, exe)
