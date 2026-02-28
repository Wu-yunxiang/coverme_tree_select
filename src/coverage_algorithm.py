import os
import sys
import time
import ctypes
from ctypes import cdll
import argparse
import numpy as np
import scipy.optimize as op

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
lib.warmup_target.argtypes = [ctypes.c_int]
lib.warmup_target.restype = None
lib.pop_queue_target.restype = TargetAndSeed
lib.nExplored.restype = ctypes.c_int
lib.begin_self_phase.restype = None
lib.finish_sample.restype = ctypes.c_int
lib.begin_base_phase.restype = None
lib.begin_delta_phase.restype = None
lib.update_queue.restype = None
lib.get_r.restype = ctypes.c_double
lib.get_node_seed.argtypes = [ctypes.c_int]
lib.get_node_seed.restype = ctypes.c_int
lib.get_tree_parent.argtypes = [ctypes.c_int]
lib.get_tree_parent.restype = ctypes.c_int
lib.get_tree_children_count.argtypes = [ctypes.c_int]
lib.get_tree_children_count.restype = ctypes.c_int
lib.get_tree_child.argtypes = [ctypes.c_int, ctypes.c_int]
lib.get_tree_child.restype = ctypes.c_int

DELTA = 1.0
WARMUP_COVERAGE = 0.5
WARMUP_MIN_ROUNDS = 4
SEED_LOW = -1024.0
SEED_HIGH = 1024.0
SEEDS_PER_LAYER = 5
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

def func_py(x):
    lib.begin_self_phase()
    lib.__coverme_target_function(*x)
    flags = lib.finish_sample()
    ret = lib.get_r()

    if flags & FLAG_NEW_COVERAGE:
        seeds.append(x)
        
        if flags & FLAG_ALL_COVERED:
            raise CoverageComplete()
        if flags & FLAG_TARGET_COVERED:
            raise TargetCovered()
        
        lib.begin_base_phase()
        lib.__coverme_target_function(*x)
        for i in range(input_dim):
            x_delta = np.array(x)
            x_delta[i] += DELTA
            call_delta(x_delta)
        lib.update_queue()
    
    return ret
    
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Coverage Algorithm based on Tree Select")
    parser.add_argument("-n", "--niter", type=int, default=5, help="Iteration number of BasinHopping")
    parser.add_argument("--stepSize", type=float, default=300.0, help="Step size")
    parser.add_argument("--seedsPerLayer", type=int, default=SEEDS_PER_LAYER, help="Number of seeds per layer in tree-based phase")
    args = parser.parse_args()

    lib.initialize_runtime()

    input_dim = lib.get_arg_count()
    total_exits = lib.get_br_count() * 2
    lib.__coverme_target_function.argtypes = [ctypes.c_double] * input_dim

    def coverage_ratio():
        return float(lib.nExplored()) / float(total_exits)

    start_time = time.process_time()
    
    try:
        iteration_count = 0

        warmup_round = 0
        while warmup_round < WARMUP_MIN_ROUNDS or coverage_ratio() < WARMUP_COVERAGE:
            lib.warmup_target(int(np.random.randint(0, total_exits)))
            try:
                x0 =np.random.uniform(SEED_LOW, SEED_HIGH, size=input_dim).astype(np.float64)
                op.basinhopping(
                    func_py,
                    x0,
                    minimizer_kwargs={"method": "powell"},
                    niter=args.niter,
                    stepsize=args.stepSize,
                )
            except TargetCovered:
                pass

            warmup_round += 1
            iteration_count += 1
            if iteration_count % 100 == 0:
                print(f"({iteration_count}, {coverage_ratio():.2%})")

        while True:
            target_and_seed = lib.pop_queue_target()
            if target_and_seed.targetId == -1:
                break
            try:
                x0 = seeds[target_and_seed.seedId]
                op.basinhopping(
                    func_py,
                    x0,
                    minimizer_kwargs={"method": "powell"},
                    niter=args.niter,
                    stepsize=args.stepSize,
                )
            except TargetCovered:
                pass
            
            iteration_count += 1
            if iteration_count % 100 == 0:
                print(f"({iteration_count}, {coverage_ratio():.2%})")

        # Phase 3: Tree-based weighted random seed generation for remaining uncovered nodes
        uncovered_nodes = [i for i in range(total_exits) if lib.get_node_seed(i) == -1]

        for u in uncovered_nodes:
            if lib.get_node_seed(u) != -1:
                continue

            # Build path from root to u
            path = []
            current = u
            while True:
                path.append(current)
                p = lib.get_tree_parent(current)
                if p == current:
                    break
                current = p
            path.reverse()

            # Try each layer from innermost to outermost
            for level_idx in range(len(path) - 1, -1, -1):
                if lib.get_node_seed(u) != -1:
                    break

                if level_idx == 0:
                    # Outermost layer: completely random
                    for _ in range(args.seedsPerLayer):
                        if lib.get_node_seed(u) != -1:
                            break
                        lib.warmup_target(u)
                        x0 = np.random.uniform(SEED_LOW, SEED_HIGH, size=input_dim).astype(np.float64)
                        try:
                            op.basinhopping(
                                func_py,
                                x0,
                                minimizer_kwargs={"method": "powell"},
                                niter=args.niter,
                                stepsize=args.stepSize,
                            )
                        except TargetCovered:
                            break

                        iteration_count += 1
                        if iteration_count % 100 == 0:
                            print(f"({iteration_count}, {coverage_ratio():.2%})")
                else:
                    level_node = path[level_idx]
                    parent_node = lib.get_tree_parent(level_node)

                    # Collect covered seed IDs from sibling subtrees
                    sibling_seed_ids = []
                    parent_children_count = lib.get_tree_children_count(parent_node)
                    for ci in range(parent_children_count):
                        child = lib.get_tree_child(parent_node, ci)
                        if child != level_node:
                            stack = [child]
                            while stack:
                                n = stack.pop()
                                sid = lib.get_node_seed(n)
                                if sid >= 0 and sid < len(seeds):
                                    sibling_seed_ids.append(sid)
                                cc = lib.get_tree_children_count(n)
                                for j in range(cc):
                                    stack.append(lib.get_tree_child(n, j))

                    if not sibling_seed_ids:
                        continue

                    # Arithmetic mean of covered seeds as initial seed
                    seed_vectors = [seeds[sid] for sid in sibling_seed_ids]
                    avg_seed = np.mean(seed_vectors, axis=0)

                    for _ in range(args.seedsPerLayer):
                        if lib.get_node_seed(u) != -1:
                            break
                        lib.warmup_target(u)
                        x0 = avg_seed.copy()
                        try:
                            op.basinhopping(
                                func_py,
                                x0,
                                minimizer_kwargs={"method": "powell"},
                                niter=args.niter,
                                stepsize=args.stepSize,
                            )
                        except TargetCovered:
                            break

                        iteration_count += 1
                        if iteration_count % 100 == 0:
                            print(f"({iteration_count}, {coverage_ratio():.2%})")

    except CoverageComplete:
        pass

    end_time = time.process_time()

    with open(effective_input_path, "w") as f:
        for seed in seeds:
            f.write(",".join(map(str, seed)) + "\n")
    print(f"Final coverage = {coverage_ratio():.2%}")
    print(f"Total process time = {end_time - start_time:.2f} seconds")