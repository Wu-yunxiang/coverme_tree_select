import os
import sys
import time
import ctypes
from ctypes import cdll
import argparse
import numpy as np
import scipy.optimize as op
from hypothesis.strategies import floats

import path_helper

class TargetAndSeed(ctypes.Structure):
    _fields_ = [("targetId", ctypes.c_int), ("seedId", ctypes.c_int)]

lib_dir = path_helper.get_lib_dir()
lib_path = os.path.join(lib_dir, "lib_coverage.so")
lib = cdll.LoadLibrary(lib_path)

lib.__coverme_target_function.restype = None
lib.initialize_runtime.restype = None
lib.get_arg_count.restype = ctypes.c_int
lib.get_br_count.restype = ctypes.c_int
lib.set_target.restype = None
lib.nExplored.restype = ctypes.c_int
lib.begin_self_phase.restype = None
lib.finish_sample.restype = ctypes.c_int
lib.begin_base_phase.restype = None
lib.begin_delta_phase.restype = None
lib.get_r.restype = ctypes.c_double
lib.get_unexplored_at.restype = ctypes.c_int
lib.get_unexplored_count.restype = ctypes.c_int
lib.get_satisfied_count.restype = ctypes.c_int
lib.get_min_node_distance.restype = ctypes.c_double

DELTA = 1.0
COVERAGE_THRESHOLD = 0.9
output_dir = path_helper.get_output_dir()
effective_input_path = os.path.join(output_dir, "effective_input.txt")
trace_log_path = os.path.join(output_dir, "trace_log.txt")

FLAG_NEW_COVERAGE = 1
FLAG_TARGET_COVERED = 2
FLAG_ALL_COVERED = 4

seeds = []
# 用于追踪层级结构的全局计数器和统计
bh_counter = 0
powell_counter = 0
iter_counter = 0
func_counter = 0
func_run_stats = {} # 记录 func_counter 达到某个值的次数
new_coverage_details = [] # 记录新覆盖出现时的层级编号 (BH, PowellRun, PowellIter, Func)
last_node_stats = {} # 缓存上一次节点的 sat 和 dist
last_x = None # 缓存上一次的 x (参数向量)

def log_trace(msg):
    with open(trace_log_path, "a") as f:
        f.write(msg + "\n")

def save_stats():
    stats_path = os.path.join(output_dir, "func_run_stats.txt")
    new_cov_path = os.path.join(output_dir, "new_coverage_func_runs.txt")
    
    # 保存 new coverage 出现时的层级信息
    with open(new_cov_path, "w") as f:
        f.write(f"{'BH_Run':<8} | {'PowellRun':<10} | {'PowellIter':<12} | {'FuncRun':<8}\n")
        f.write("-" * 50 + "\n")
        for bh, pr, pi, fr in new_coverage_details:
            f.write(f"{bh:<8} | {pr:<10} | {pi:<12} | {fr:<8}\n")

    # 总函数运行次数（Workload）：每个 Count 乘以其出现的频率
    total_workload = sum(count * freq for count, freq in func_run_stats.items())
    if total_workload == 0:
        return
        
    sorted_keys = sorted(func_run_stats.keys())
    cumulative_workload = 0
    with open(stats_path, "w") as f:
        f.write("FuncRun_Count | Frequency | Cumulative_Workload_Percentage\n")
        f.write("-" * 65 + "\n")
        for k in sorted_keys:
            frequency = func_run_stats[k]
            current_workload = k * frequency
            cumulative_workload += current_workload
            percentage = (cumulative_workload / total_workload) * 100
            f.write(f"{k:<13} | {frequency:<9} | {percentage:.2f}%\n")
    print(f"Total workload = {total_workload}")

class CoverageComplete(Exception):
    pass

class TargetCovered(Exception):
    pass

total_func_counter = 0
total_func_effective = 0
def func_py(x):
    global total_func_counter, total_func_effective
    total_func_counter += 1
    global func_counter, last_node_stats, last_x
    func_counter += 1
    if powell_counter == 0 and iter_counter <= 1 and func_counter <= 20:
        total_func_effective += 1
    
    lib.begin_self_phase()
    lib.__coverme_target_function(*x)
    flags = lib.finish_sample()
    ret = lib.get_r()

    # 处理 x 的各个维度变化量
    x_stats = []
    for i, val in enumerate(x):
        if last_x is not None and i < len(last_x):
            diff_x = val - last_x[i]
            x_stats.append(f"{val:.8e}({diff_x:+.2e})")
        else:
            x_stats.append(f"{val:.8e}(init)")
    last_x = np.array(x)

    # 此处利用 base_phase 评估所有未覆盖点的状态
    lib.begin_base_phase()
    # 模拟一次运行以计算未覆盖点的距离
    lib.__coverme_target_function(*x)

    # 获取所有未覆盖点的状态
    unexplored_stats = []
    u_count = lib.get_unexplored_count()
    current_stats = {}
    for i in range(u_count):
        node_id = lib.get_unexplored_at(i)
        sat = lib.get_satisfied_count(node_id)
        dist = lib.get_min_node_distance(node_id)
        current_stats[node_id] = (sat, dist)
        
        if node_id in last_node_stats:
            l_sat, l_dist = last_node_stats[node_id]
            diff_sat = sat - l_sat
            diff_dist = dist - l_dist
            sat_str = f"{sat}({diff_sat:+=d})"
            dist_str = f"{dist:.4f}({diff_dist:+.4f})"
        else:
            sat_str = f"{sat}(init)"
            dist_str = f"{dist:.4f}(init)"
        
        # 记录缩进格式：Node 缩进 5 层 (比 FuncRun 再多一层)
        unexplored_stats.append(f"            Node({node_id}): sat={sat_str}, dist={dist_str}")

    last_node_stats = current_stats

    # 记录缩进格式：FuncRun 保持 4 层缩进
    indent = "    " * 4
    x_str = ", ".join(x_stats)
    log_trace(f"{indent}FuncRun {func_counter}: x=[{x_str}], r={ret:.6f}")
    if flags & FLAG_NEW_COVERAGE:
        log_trace(f"{indent}    [NEW COVERAGE DETECTED!]")
    #if unexplored_stats:
        #log_trace("\n".join(unexplored_stats))

    if flags & FLAG_NEW_COVERAGE:
        new_coverage_details.append((bh_counter, powell_counter, iter_counter, func_counter))
        seeds.append(x)
        if flags & FLAG_ALL_COVERED:
            raise CoverageComplete()
        if flags & FLAG_TARGET_COVERED:
            raise TargetCovered()
        
    return ret

def powell_callback(x):
    global iter_counter, func_counter
    # 在重置前记录 func_counter 的统计信息
    if func_counter > 0:
        func_run_stats[func_counter] = func_run_stats.get(func_counter, 0) + 1
        
    iter_counter += 1
    func_counter = 0 # 按“内部迭代编号”重置以体现层级
    indent = "    " * 3
    log_trace(f"{indent}PowellIter {iter_counter}: x={x}")

def bh_callback(x, f, accept):
    global powell_counter, iter_counter
    powell_counter += 1
    iter_counter = 0
    indent = "    " * 2
    log_trace(f"{indent}PowellRun {powell_counter}: x={x}, f={f:.6f}, accept={accept}")
    
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Coverage Algorithm based on Tree Select")
    parser.add_argument("-n", "--niter", type=int, default=5, help="Iteration number of BasinHopping")
    parser.add_argument("--stepSize", type=float, default=300.0, help="Step size")
    args = parser.parse_args()

    # 清空 trace log
    if os.path.exists(trace_log_path):
        os.remove(trace_log_path)

    lib.initialize_runtime()

    input_dim = lib.get_arg_count()
    total_exits = lib.get_br_count() * 2
    lib.__coverme_target_function.argtypes = [ctypes.c_double] * input_dim
    get_float = floats().example

    def coverage_ratio():
        return float(lib.nExplored()) / float(total_exits)

    start_time = time.process_time()
    
    try:
        iteration_count = 0
        while coverage_ratio() < COVERAGE_THRESHOLD:
            bh_counter += 1
            powell_counter = 0
            lib.set_target()
            log_trace(f"BasinHopping Run {bh_counter} ")
            try:
                x0 = np.array([get_float() for _ in range(input_dim)], dtype=np.float64)
                op.basinhopping(
                    func_py,
                    x0,
                    minimizer_kwargs={"method": "powell", "callback": powell_callback},
                    niter=args.niter,
                    stepsize=args.stepSize,
                    callback=bh_callback
                )
            except TargetCovered:
                log_trace("    [Target Covered!]")
                pass

            iteration_count += 1
            if iteration_count % 100 == 0:
                print(f"({iteration_count}, {coverage_ratio():.2%})")

    except CoverageComplete:
        log_trace("Coverage Complete!")
        pass

    save_stats()
    end_time = time.process_time()

    with open(effective_input_path, "w") as f:
        for seed in seeds:
            f.write(",".join(map(str, seed)) + "\n")
    print(f"Final coverage = {coverage_ratio():.2%}")
    print(f"Total process time = {end_time - start_time:.2f} seconds")
    print(f"Total function runs = {total_func_counter}")
    print(f"Effective function runs = {total_func_effective}")
    print(f"Effective ratio = {total_func_effective/total_func_counter:.2%}")