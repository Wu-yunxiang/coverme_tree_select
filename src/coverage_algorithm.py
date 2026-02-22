#!/usr/bin/env python
import os
import sys
import time
import ctypes
from ctypes import cdll
import argparse
import numpy as np
import scipy.optimize as op

root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if root_dir not in sys.path:
    sys.path.append(root_dir)
import path_helper

# 加载插桩后的动态链接库
lib_dir = path_helper.get_lib_dir()
lib_path = os.path.join(lib_dir, "libr.so")
if not os.path.exists(lib_path):
    print(f"Error: {lib_path} not found. Please build the project first.")
    sys.exit(1)

lib = cdll.LoadLibrary(lib_path)

# 统一的待测函数入口（由 insert_pen.cpp 重命名）
lib.__coverme_target_function.restype = ctypes.c_double

_target_argc = None

def call_target_with_unpack(x):
    global _target_argc
    values = np.asarray(x, dtype=np.float64)
    argc = int(values.size)
    if _target_argc != argc:
        lib.__coverme_target_function.argtypes = [ctypes.c_double] * argc
        _target_argc = argc
    return lib.__coverme_target_function(*[float(v) for v in values])

# 绑定 C++ 接口函数
lib.nExplored.restype = ctypes.c_int
lib.initial_sample.restype = None
lib.update_sample.restype = None
lib.update_queue.restype = None

# 假设 C++ 端有这些控制变量的 setter，如果没有，需要在 interface_for_py.cpp 中添加
# lib.set_isSelfMode(ctypes.c_bool)
# lib.set_isGetBase(ctypes.c_bool)
# lib.set_seedId_base(ctypes.c_int)
# lib.get_queue_size.restype = ctypes.c_int
# lib.pop_from_queue.restype = ctypes.c_int # 返回 nodeId 或 seedId
# lib.get_target_node.restype = ctypes.c_int # 获取当前正在优化的目标节点

# 全局变量，用于记录种子数量和提前终止
seed_count = 0
start_time = 0
objective_coverage = 1.0
current_target_node = -1

class StopOptimization(Exception):
    pass

def get_nConditionStatement():
    brInfo_path = os.path.join(path_helper.get_output_dir(), "edges.txt")
    if not os.path.exists(brInfo_path):
        return 0
    with open(brInfo_path) as f:
        return sum(1 for _ in f)

def exploredRatio():
    try:
        return lib.nExplored() * 1.0 / (get_nConditionStatement() * 2.0)
    except ZeroDivisionError:
        return 1.0

def foo_py(x):
    global seed_count, start_time, objective_coverage, current_target_node
    seed_count += 1
    
    # 每 100 个种子输出一次信息
    if seed_count % 100 == 0:
        current_time = time.process_time()
        cov_ratio = exploredRatio()
        print(f"Seeds: {seed_count} | Coverage: {cov_ratio:.2%} | Time: {current_time - start_time:.2f}s")
        
        if cov_ratio >= objective_coverage:
            print("Objective coverage reached. Stopping.")
            raise StopOptimization("Objective reached")

    """
    核心执行逻辑：
    1. 正常执行（isSelfMode = True）
    2. 如果发现新覆盖，则进入梯度计算模式（isSelfMode = False）
       a. 运行 Base 模式 (isGetBase = True)
       b. 对每个维度进行 Delta 扰动 (isGetBase = False)
       c. 更新优先队列
    """
    # 1. 正常执行模式
    # lib.set_isSelfMode(True)
    explored0 = lib.nExplored()
    
    ret = call_target_with_unpack(x)
    
    explored1 = lib.nExplored()
    
    # 检查是否到达了当前优化的目标节点
    # 假设 C++ 端提供了一个接口来判断当前输入是否覆盖了目标节点
    # if lib.is_target_covered(current_target_node):
    #     raise StopOptimization("Target node reached")
    
    # 2. 发现新覆盖，计算梯度并更新队列
    if explored1 > explored0:
        # lib.set_isSelfMode(False)
        # lib.set_seedId_base(seed_count)
        
        # a. 运行 Base 模式
        # lib.set_isGetBase(True)
        lib.initial_sample()
        call_target_with_unpack(x)
        
        # b. 运行 Delta 模式
        # lib.set_isGetBase(False)
        dim = len(x)
        delta = 1e-4 # 扰动步长
        
        for i in range(dim):
            original_val = x[i]
            x[i] = original_val + delta
            call_target_with_unpack(x)
            
            # 恢复原值
            x[i] = original_val
            
            # 更新该维度的样本梯度
            lib.update_sample()
            
        # c. 所有维度扰动完毕，更新优先队列
        lib.update_queue()
        
    return ret

def mybounds(**kwargs):
    return True

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Coverage Algorithm based on Tree Select')
    parser.add_argument('-i', '--inputDim', help='input dimension', type=int, default=1)
    parser.add_argument('-c', '--objective', help='objective Coverage', type=float, default=1.0)
    parser.add_argument('-S', '--seed', help='random seed', type=int, default=None)
    parser.add_argument('-n', '--niter', help='Iteration number of BasinHopping', type=int, default=5)
    parser.add_argument('--stepSize', help='Step size', type=float, default=300.0)
    args = parser.parse_args()

    if args.seed is not None:
        np.random.seed(args.seed)

    objective_coverage = args.objective
    print(f"[Coverage Algorithm] Starting with Dim={args.inputDim}...")
    start_time = time.process_time()
    
    # 初始随机种子
    from hypothesis.strategies import floats
    get_float = lambda: floats(allow_nan=False, allow_infinity=False).example()
    
    try:
        while True:
            # 模拟从优先队列中获取种子，如果队列为空则随机生成
            # 实际应用中需要调用 C++ 接口获取队列顶部的种子信息
            # queue_size = lib.get_queue_size()
            # if queue_size > 0:
            #     current_target_node = lib.pop_from_queue()
            #     lib.set_target_node(current_target_node) # 告诉 C++ 当前优化的目标
            #     x = get_seed_by_node(current_target_node) # 需要 C++ 提供接口
            # else:
            current_target_node = -1
            x = np.array([get_float() for _ in range(args.inputDim)])
            
            # 使用 basinhopping 进行优化
            kw = {'method': 'powell'}
            try:
                res = op.basinhopping(
                    foo_py, 
                    x, 
                    minimizer_kwargs=kw, 
                    niter=args.niter, 
                    stepsize=args.stepSize, 
                    accept_test=mybounds
                )
            except StopOptimization as e:
                if str(e) == "Objective reached":
                    break
                # 如果是 "Target node reached"，则继续下一个队列元素的优化
                print(f"Optimization stopped early: {e}")
                
            if exploredRatio() >= args.objective:
                print("Objective coverage reached. Stopping.")
                break
                
    except KeyboardInterrupt:
        print("\nInterrupted by user.")

    end_time = time.process_time()
    print(f"Total process time = {end_time - start_time:.2f} seconds")
    sys.exit(0)
