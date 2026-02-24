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
lib_path = os.path.join(lib_dir, "lib_coverage.so")
lib = cdll.LoadLibrary(lib_path)

# 待测函数入口（由 insert_pen.cpp 重命名）
lib.__coverme_target_function.restype = None
lib.initialize_runtime.restype = ctypes.c_int
lib.get_arg_count.restype = ctypes.c_int
lib.get_br_count.restype = ctypes.c_int
lib.nExplored.restype = ctypes.c_int
lib.set_target.argtypes = [ctypes.c_int]
lib.set_target.restype = None
lib.pop_queue_target.restype = ctypes.c_int
lib.begin_self_sample.restype = ctypes.c_int
lib.finish_self_sample.argtypes = [ctypes.c_int, ctypes.c_int]
lib.finish_self_sample.restype = ctypes.c_int
lib.begin_base_phase.restype = None
lib.begin_delta_phase.restype = None
lib.begin_delta_sample.restype = None
lib.end_delta_sample.restype = None
lib.finalize_nonself_phase.restype = None

DELTA = 1.0
WARMUP_COVERAGE = 0.25
WARMUP_MIN_ROUNDS = 4
SEED_LOW = -1024.0
SEED_HIGH = 1024.0
_target_argtypes_dim = -1
_raise_on_target_covered = False
NEW_COVERAGE_LOG = os.path.join(path_helper.get_output_dir(), "new_coverage_params.txt")
_total_exits = 0
FLAG_NEW_COVERAGE = 1
FLAG_TARGET_COVERED = 2
FLAG_ALL_COVERED = 4


class CoverageComplete(Exception):
    pass


def call_target(x):
    global _target_argtypes_dim
    values = np.asarray(x, dtype=np.float64)
    if _target_argtypes_dim != values.size:
        lib.__coverme_target_function.argtypes = [ctypes.c_double] * int(values.size)
        _target_argtypes_dim = int(values.size)
    lib.__coverme_target_function(*[float(v) for v in values])


class TargetCovered(Exception):
    pass


def objective(x):
    values = np.asarray(x, dtype=np.float64)

    explored_before = lib.begin_self_sample()
    call_target(values)
    flags = lib.finish_self_sample(explored_before, 1 if _raise_on_target_covered else 0)

    if flags & FLAG_TARGET_COVERED:
        raise TargetCovered()
    if flags & FLAG_ALL_COVERED:
        raise CoverageComplete()

    if flags & FLAG_NEW_COVERAGE:
        with open(NEW_COVERAGE_LOG, "a", encoding="utf-8") as fp:
            fp.write(",".join(f"{float(v):.17g}" for v in values) + "\n")

        lib.begin_base_phase()
        call_target(values)

        lib.begin_delta_phase()
        for i in range(values.size):
            sample = values.copy()
            sample[i] += DELTA
            lib.begin_delta_sample()
            call_target(sample)
            lib.end_delta_sample()

        lib.finalize_nonself_phase()

    return 0.0

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Coverage Algorithm based on Tree Select')
    parser.add_argument('-n', '--niter', help='Iteration number of BasinHopping', type=int, default=5)
    parser.add_argument('--stepSize', help='Step size', type=float, default=300.0)
    args = parser.parse_args()

    lib.initialize_runtime()

    input_dim = lib.get_arg_count()
    _total_exits = lib.get_br_count() * 2

    def coverage_ratio():
        return float(lib.nExplored()) / float(_total_exits)

    start_time = time.process_time()
    iteration_count = 0
    minimizer_kwargs = {'method': 'powell'}

    try:
        # Warm-up: basinhopping + 随机target，且至少4轮并达到阈值
        warmup_round = 0
        while warmup_round < WARMUP_MIN_ROUNDS or coverage_ratio() < WARMUP_COVERAGE:
            if coverage_ratio() >= 1.0:
                break

            warmup_round += 1
            iteration_count += 1
            if iteration_count % 100 == 0:
                print(f"({iteration_count}, {coverage_ratio():.2%})")

            lib.set_target(int(np.random.randint(0, _total_exits)))
            x = np.random.uniform(SEED_LOW, SEED_HIGH, size=input_dim).astype(np.float64)

            try:
                _raise_on_target_covered = True
                op.basinhopping(
                    objective,
                    x,
                    minimizer_kwargs=minimizer_kwargs,
                    niter=args.niter,
                    stepsize=args.stepSize,
                    accept_test=lambda **_: True
                )
            except TargetCovered:
                pass
            finally:
                _raise_on_target_covered = False

        # Queue phase: 取目标 -> basinhopping，命中目标后切下一个目标
        while coverage_ratio() < 1.0:
            iteration_count += 1
            if iteration_count % 100 == 0:
                print(f"({iteration_count}, {coverage_ratio():.2%})")

            node = lib.pop_queue_target()
            if node < 0:
                break

            lib.set_target(node)

            x = np.random.uniform(SEED_LOW, SEED_HIGH, size=input_dim).astype(np.float64)
            try:
                _raise_on_target_covered = True
                op.basinhopping(
                    objective,
                    x,
                    minimizer_kwargs=minimizer_kwargs,
                    niter=args.niter,
                    stepsize=args.stepSize,
                    accept_test=lambda **_: True
                )
            except TargetCovered:
                continue
            finally:
                _raise_on_target_covered = False
    except CoverageComplete:
        pass

    end_time = time.process_time()
    print(f"Final coverage = {coverage_ratio():.2%}")
    print(f"Total process time = {end_time - start_time:.2f} seconds")
