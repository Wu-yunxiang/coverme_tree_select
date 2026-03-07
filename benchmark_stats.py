import subprocess
import re
import numpy as np

def run_once():
    try:
        # 运行命令并获取输出
        result = subprocess.run(
            ["python3", "src/coverage_algorithm.py"],
            capture_output=True,
            text=True,
            check=True,
            cwd="/home/wuyunxiang/coverme_tree_select"
        )
        output = result.stdout
        
        # 使用正则表达式提取 func_count 和 Total process time
        func_count_match = re.search(r"func_count\s*=\s*(\d+)", output)
        time_match = re.search(r"Total process time\s*=\s*([\d.]+)", output)
        
        if func_count_match and time_match:
            return int(func_count_match.group(1)), float(time_match.group(1))
        else:
            print("Could not find patterns in output:")
            print(output)
            return None
    except Exception as e:
        print(f"Error running script: {e}")
        return None

def main():
    func_counts = []
    run_times = []
    iterations = 100
    
    print(f"Starting {iterations} iterations of coverage_algorithm.py...")
    
    for i in range(iterations):
        print(f"Iteration {i+1}/{iterations}...", end=" ", flush=True)
        res = run_once()
        if res:
            fc, rt = res
            func_counts.append(fc)
            run_times.append(rt)
            print(f"Done (func_count={fc}, time={rt:.2f}s)")
        else:
            print("Failed")
            
    if func_counts:
        avg_func_count = np.mean(func_counts)
        avg_run_time = np.mean(run_times)
        
        print("\n" + "="*30)
        print(f"Results over {len(func_counts)} successful runs:")
        print(f"Average func_count: {avg_func_count:.2f}")
        print(f"Average Total process time: {avg_run_time:.2f} seconds")
        print("="*30)
    else:
        print("No successful runs to calculate averages.")

if __name__ == "__main__":
    main()
