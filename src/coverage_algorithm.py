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
lib.pop_queue_target.restype = TargetAndSeed
lib.nExplored.restype = ctypes.c_int
lib.begin_self_phase.restype = None
lib.finish_sample.restype = ctypes.c_int
lib.begin_base_phase.restype = None
lib.begin_delta_phase.restype = None
lib.update_queue.restype = None
lib.get_r.restype = ctypes.c_double
lib.set_target.restype = ctypes.c_int
lib.set_target.argtypes = [ctypes.c_int]
lib.set_random_target.restype = None
lib.set_random_target.argtypes = [ctypes.c_int]
lib.get_node_status.argtypes = [ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
lib.get_node_status.restype = ctypes.c_int
lib.get_last_covered_node.restype = ctypes.c_int
lib.get_target.restype = ctypes.c_int
lib.set_target_direct.argtypes = [ctypes.c_int]
lib.set_target_direct.restype = None

DELTA = 1.0
COVERAGE_THRESHOLD = 0.98 # 目标覆盖率，到达后停止，可设置
CONDS_DIFF_THRESHOLD = 2
effective_input_path = os.path.join(path_helper.get_output_dir(), "effective_input.txt")

FLAG_NEW_COVERAGE = 1
FLAG_TARGET_COVERED = 2
FLAG_ALL_COVERED = 4

seeds = []

class CoverageComplete(Exception):
    pass

class TargetCovered(Exception):
    pass

def call_delta(x_delta):
    lib.begin_delta_phase()
    lib.__coverme_target_function(*x_delta)
    flags = lib.finish_sample()

    if flags & FLAG_NEW_COVERAGE:
        seeds.append(x_delta)
        if flags & FLAG_ALL_COVERED:
            raise CoverageComplete()
        if flags & FLAG_TARGET_COVERED:
            raise TargetCovered()

func_count = 0
all_seeds = []
all_initial_x = []
current_x0 = None
current_x0_func_count_start = 0
current_seed_id = 0

def record_seed_info(new_seed):
    global current_x0, all_seeds, func_count, current_x0_func_count_start, current_seed_id, all_initial_x
    current_seed_id += 1
    
    # 即排除掉从当前 Initial_X 开始产生的所有输入（最后一个是 new_seed，之前的是中间路径）
    history = all_seeds[:current_x0_func_count_start]
    history_initials = all_initial_x[:current_x0_func_count_start]
    if not history: return
    
    # 距离定义：nan==nan为0, inf==inf为0, 否则计算差的平方
    h_np, s_np = np.array(history), np.array(new_seed)
    mask = (np.isnan(h_np) & np.isnan(s_np)) | (np.isinf(h_np) & np.isinf(s_np) & (np.sign(h_np) == np.sign(s_np)))
    dist_sq = np.sum(np.where(mask, 0, np.square(np.nan_to_num(h_np - s_np, nan=1e38, posinf=1e38, neginf=1e38))), axis=1)
    
    # 获取前5个最近的索引
    num_closest = min(20, len(history))
    closest_indices = np.argsort(dist_sq)[:num_closest]
    
    # 获取当前真正覆盖的节点信息（作为目标）
    # 获取最后一个被覆盖的节点
    target_node = lib.get_last_covered_node() 
    
    with open(os.path.join(path_helper.get_output_dir(), "seed_info.txt"), "a") as f:
        f.write(f"Seed {current_seed_id}: {','.join(map(str, new_seed))}\n")
        f.write(f"  Target: {lib.get_target()}, NewlyCoveredNode: {target_node}\n")
        f.write(f"  Call count since Initial_X: {func_count - current_x0_func_count_start}\n")
        
        for i, idx in enumerate(closest_indices):
            lib.begin_base_phase()
            lib.__coverme_target_function(*history[idx])
            last_d = ctypes.c_double()
            total_c = ctypes.c_int()
            newly_c = ctypes.c_int()
            satisfied = lib.get_node_status(ctypes.byref(last_d), ctypes.byref(total_c), ctypes.byref(newly_c))
            
            num, den = idx + 1, len(history)
            f.write(f"  Closest {i+1}: {','.join(map(str, history[idx]))} (Satisfied: {satisfied}/{total_c.value}, Dist: {last_d.value:.6f}, NewlyCovered: {newly_c.value})\n")
            f.write(f"    Initial_X of Closest: {','.join(map(str, history_initials[idx]))}\n")
            f.write(f"    Ratio: {num/den:.6f} ({num}/{den})\n")
        
        f.write(f"Initial_X: {','.join(map(str, current_x0))}\n{'-'*20}\n")
    
    # 保存后续求解所需信息: seed_id | target_node | 20个 closest input
    with open(os.path.join(path_helper.get_output_dir(), "solve_data.tmp"), "a") as tmp:
        closest_data = [",".join(map(str, history[idx])) for idx in closest_indices]
        tmp.write(f"{current_seed_id}|{target_node}|{'|'.join(closest_data)}\n")

is_solving_phase = False
solve_success = False

def func_py(x):
    global all_seeds, all_initial_x, is_solving_phase, solve_success
    all_seeds.append(x)
    all_initial_x.append(current_x0)
    global func_count
    func_count += 1
    lib.begin_self_phase()
    lib.__coverme_target_function(*x)
    flags = lib.finish_sample()
    ret = lib.get_r()

    if flags & FLAG_NEW_COVERAGE:
        if is_solving_phase:
            if flags & (FLAG_TARGET_COVERED | FLAG_ALL_COVERED):
                solve_success = True
        else:
            seeds.append(x)
            record_seed_info(x)
            if flags & FLAG_ALL_COVERED:
                raise CoverageComplete()
            if flags & FLAG_TARGET_COVERED:
                raise TargetCovered()
    return ret
    
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Coverage Algorithm based on Tree Select")
    parser.add_argument("-n", "--niter", type=int, default=0, help="Iteration number of BasinHopping")
    parser.add_argument("--stepSize", type=float, default=300.0, help="Step size")
    args = parser.parse_args()

    lib.initialize_runtime()

    # 初始化文件：通过 'w' 模式打开直接覆盖旧文件即为清空，无需先 os.remove 再 open
    output_dir = path_helper.get_output_dir()
    seed_info_path = os.path.join(output_dir, "seed_info.txt")
    solve_data_path = os.path.join(output_dir, "solve_data.tmp")
    solve_info_path = os.path.join(output_dir, "solve_info.txt")
    effective_input_path = os.path.join(output_dir, "effective_input.txt")
    
    # 统一使用 'w' 模式清空/创建所有输出文件
    for p in [seed_info_path, solve_data_path, solve_info_path, effective_input_path]:
        with open(p, "w") as f: pass

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
            try:
                x0 = np.array([get_float() for _ in range(input_dim)], dtype=np.float64)
                current_x0 = x0
                current_x0_func_count_start = func_count
                
                lib.begin_base_phase()
                lib.__coverme_target_function(*x0)
                if lib.set_target(CONDS_DIFF_THRESHOLD) < 0:
                    continue
                
                #lib.set_random_target(np.random.randint(0, total_exits - 1))
                op.basinhopping(
                    func_py,
                    x0,
                    minimizer_kwargs={
                        "method": "powell",
                        #"options": {"maxiter": 10, "maxfev": 20}
                    },
                    niter=args.niter,
                    stepsize=args.stepSize,
                )
            except TargetCovered:
                pass

            iteration_count += 1
            if iteration_count % 100 == 0:
                print(f"({iteration_count}, {coverage_ratio():.2%})")

    except CoverageComplete:
        pass

    end_time = time.process_time()
    final_cov = coverage_ratio()
    
    # 运行结束后，重置状态进入求解验证阶段（该阶段用于分析而非覆盖，可被注释）
    is_solving_phase = True
    if os.path.exists(solve_data_path):
        with open(solve_data_path, "r") as info_f:
            for line in info_f:
                parts = line.strip().split('|')
                if not parts: continue
                s_id = parts[0]
                target_node = int(parts[1])
                closest_inputs = [list(map(float, p.split(','))) for p in parts[2:]]
                
                with open(solve_info_path, "a") as out_f:
                    out_f.write(f"Seed {s_id} (Target Node: {target_node}):\n")
                
                for idx, start_x in enumerate(closest_inputs):
                    solve_success = False
                    # 设置当前目标并从已探索中移除
                    lib.set_target_direct(target_node) 
                    
                    # 尝试求解
                    op.basinhopping(
                        func_py,
                        np.array(start_x),
                        minimizer_kwargs={
                            "method": "powell",
                            #"options": {"maxiter": 10, "maxfev": 20}
                        },
                        niter=args.niter,
                        stepsize=args.stepSize,
                    )
                    
                    with open(solve_info_path, "a") as out_f:
                        out_f.write(f"  Closest {idx+1} Solve: {'Success' if solve_success else 'Failed'}\n")
                
                with open(solve_info_path, "a") as out_f:
                    out_f.write(f"{'='*20}\n")
    
    with open(effective_input_path, "w") as f:
        for seed in seeds:
            f.write(",".join(map(str, seed)) + "\n")
    print(f"func_count = {func_count}")
    print(f"Final covrage = {final_cov:.2%}")
    print(f"Total process time = {end_time - start_time:.2f} seconds")