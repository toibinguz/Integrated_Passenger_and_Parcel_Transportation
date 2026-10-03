import sys
import re

def minify_cpp(input_file, output_file):
    with open(input_file, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

    # Xóa comment 1 dòng (//...)
    content = re.sub(r'//.*', '', content)
    
    # Xóa comment nhiều dòng (/* ... */)
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)

    # Lọc từng dòng: Xóa khoảng trắng lề trái/phải và bỏ đi các dòng trống rỗng
    lines = [line.strip() for line in content.split('\n') if line.strip() != '']
    
    # Nối lại với nhau
    minified = '\n'.join(lines)
    
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(minified)
        
    print(f"Đã nén thành công từ '{input_file}' sang '{output_file}'!")
    print(f"[-] Dung lượng gốc : {len(content):,} ký tự")
    print(f"[-] Dung lượng mới : {len(minified):,} ký tự")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Cú pháp sử dụng: python minify_cpp.py <file_full.cpp> <file_compact.cpp>")
        sys.exit(1)
        
    minify_cpp(sys.argv[1], sys.argv[2])
