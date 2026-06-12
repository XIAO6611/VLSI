import os
import subprocess
import time
import platform
import sys

# 配置项
SOURCE_FILES = "*.cpp"
if platform.system() == "Windows":
    EXECUTABLE = "placer.exe"
else:
    EXECUTABLE = "./placer"

DATA_DIR = "./data"
OUTPUT_DIR = "./output"
TIMEOUT_SEC = 600  # 每个数据集最大运行时间（秒）

def compile_code():
    print("[INFO] 正在编译 C++ 代码...")
    compile_cmd = f"g++ -O3 -std=c++17 {SOURCE_FILES} -o {EXECUTABLE}"
    result = subprocess.run(compile_cmd, shell=True)
    if result.returncode != 0:
        print("[ERROR] 编译失败！请检查代码语法。")
        sys.exit(1)
    print("[INFO] 编译成功！\n")

def run_single_dataset(dataset_path, output_file):
    """运行单个数据集，返回 (hpwl, runtime) 或 (None, None) 表示失败"""
    run_cmd = f"{EXECUTABLE} {dataset_path} {output_file}"
    try:
        start_time = time.time()
        subprocess.run(run_cmd, shell=True, check=True)
        elapsed = time.time() - start_time
    except subprocess.TimeoutExpired:
        print(f"[ERROR] 运行超时（{TIMEOUT_SEC}秒）")
        return None, None
    except subprocess.CalledProcessError as e:
        print(f"[ERROR] 运行失败，返回码 {e.returncode}")
        return None, None
    
    # 从输出文件读取 HPWL 和 Runtime
    hpwl = "N/A"
    runtime = "N/A"
    try:
        with open(output_file, 'r') as f:
            for line in f:
                if line.startswith("# HPWL:"):
                    hpwl = line.split(":")[1].strip()
                elif line.startswith("# Runtime:"):
                    runtime = line.split(":")[1].replace("s", "").strip()
    except FileNotFoundError:
        print(f"[ERROR] 输出文件 {output_file} 未生成")
        return None, None
    
    return hpwl, runtime

def generate_visualization(data_dir, placement_file, output_img):
    """调用 visualize.py 生成图片"""
    cmd = f"python visualize.py --data_dir {data_dir} --placement {placement_file} --output {output_img}"
    try:
        subprocess.run(cmd, shell=True, check=True, timeout=30)
        return True
    except Exception as e:
        print(f"[WARNING] 可视化生成失败: {e}")
        return False

def run_experiments():
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)
    if not os.path.exists(DATA_DIR):
        print(f"[ERROR] 找不到数据目录 {DATA_DIR}，请确保数据集解压在此处。")
        sys.exit(1)

    datasets = [d for d in os.listdir(DATA_DIR) if os.path.isdir(os.path.join(DATA_DIR, d))]
    datasets.sort()
    
    results = []  # 存储 (dataset, hpwl, runtime)
    
    for dataset in datasets:
        print(f"\n[RUN] 正在处理数据集: {dataset}")
        input_path = os.path.join(DATA_DIR, dataset) + "/"
        output_file = os.path.join(OUTPUT_DIR, f"{dataset}_placement.txt")
        output_img = os.path.join(OUTPUT_DIR, f"{dataset}_visual.png")
        
        hpwl, runtime = run_single_dataset(input_path, output_file)
        if hpwl is None:
            print(f"{dataset:<20} | {'[FAILED]':<15} | {'[FAILED]':<15}")
            results.append((dataset, "FAILED", "FAILED"))
            continue
        
        print(f"{dataset:<20} | {hpwl:<15} | {runtime:<15}")
        results.append((dataset, hpwl, runtime))
        
        # 生成可视化图片
        generate_visualization(input_path, output_file, output_img)
    
    # 保存 Markdown 和纯文本总结
    md_path = os.path.join(OUTPUT_DIR, "summary.md")
    txt_path = os.path.join(OUTPUT_DIR, "summary.txt")
    
    with open(md_path, 'w') as f_md, open(txt_path, 'w') as f_txt:
        # Markdown 表格
        f_md.write("# FPGA Placement Results\n\n")
        f_md.write("| 数据集 | 线长 (HPWL) | 运行时间 (s) |\n")
        f_md.write("| :--- | :--- | :--- |\n")
        # 纯文本表格
        f_txt.write("FPGA Placement Results\n")
        f_txt.write("=" * 50 + "\n")
        f_txt.write(f"{'数据集':<20} | {'HPWL':<15} | {'Runtime(s)':<15}\n")
        f_txt.write("-" * 56 + "\n")
        
        for ds, hpwl, rt in results:
            f_md.write(f"| {ds} | {hpwl} | {rt} |\n")
            f_txt.write(f"{ds:<20} | {hpwl:<15} | {rt:<15}\n")
    
    print("\n\n[INFO] 结果已保存至:")
    print(f"  - Markdown: {md_path}")
    print(f"  - 纯文本:   {txt_path}")
    
    # 同时在终端打印 Markdown 表格（方便复制）
    print("\n[INFO] 最终结果表格（可复制到实验报告）：\n")
    print("| 数据集 | 线长 (HPWL) | 运行时间 (s) |")
    print("| :--- | :--- | :--- |")
    for ds, hpwl, rt in results:
        print(f"| {ds} | {hpwl} | {rt} |")

if __name__ == "__main__":
    compile_code()
    run_experiments()