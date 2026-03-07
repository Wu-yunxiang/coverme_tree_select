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

DELTA = 1.0
COVERAGE_THRESHOLD = 0.92
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
def func_py(x):
    global func_count
    func_count += 1
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
        '''
        lib.begin_base_phase()
        lib.__coverme_target_function(*x)
        for i in range(input_dim):
            x_delta = np.array(x)
            x_delta[i] += DELTA
            call_delta(x_delta)
        lib.update_queue()
        '''
    return ret
    
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Coverage Algorithm based on Tree Select")
    parser.add_argument("-n", "--niter", type=int, default=0, help="Iteration number of BasinHopping")
    parser.add_argument("--stepSize", type=float, default=300.0, help="Step size")
    args = parser.parse_args()

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
            
            if lib.set_target(CONDS_DIFF_THRESHOLD) < 0:
                continue
            
            try:
                #lib.set_random_target(int(np.random.randint(0, total_exits)))
                x0 = np.array([get_float() for _ in range(input_dim)], dtype=np.float64)
                op.basinhopping(
                    func_py,
                    x0,
                    minimizer_kwargs={
                        "method": "powell",
                        "options": {"maxiter": 1, "maxfev": 20}
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

    with open(effective_input_path, "w") as f:
        for seed in seeds:
            f.write(",".join(map(str, seed)) + "\n")
    print(f"func_count = {func_count}")
    print(f"Final covrage = {coverage_ratio():.2%}")
    print(f"Total process time = {end_time - start_time:.2f} seconds")