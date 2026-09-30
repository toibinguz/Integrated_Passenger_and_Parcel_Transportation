import sys
from models import Data
from solver_ls import LSSolver

# --- CẤU HÌNH CHẠY (RUN CONFIGURATION) ---
MAX_ITERATIONS = 500       # Số vòng lặp tối đa của Local Search
INIT_FRACTION = 0.85       # Tỷ lệ node nhét vào ban đầu (giữ lại 15% để LS tự nhét sau)

def main():
    if len(sys.argv) < 2:
        print("Usage: python test_ls.py <testcase>")
        sys.exit(1)
        
    data = Data(sys.argv[1])
    data.filename = sys.argv[1]
    
    solver = LSSolver(data)
    print("Bắt đầu Local Search...")
    best_ben = solver.solve(max_iterations=MAX_ITERATIONS, init_fraction=INIT_FRACTION)
    
    print(f"Benefit cuối cùng: {best_ben}")
    
if __name__ == '__main__':
    main()
