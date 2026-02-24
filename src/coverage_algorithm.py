import os
import time
import ctypes
from ctypes import cdll
import argparse
import numpy as np
import scipy.optimize as op

import path_helper

class FlagAndSeed(ctypes.Structure):
    _fields_ = [("flags", ctypes.c_int), ("seedId", ctypes.c_int)]

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
lib.finish_sample.restype = FlagAndSeed
lib.begin_base_phase.restype = None
lib.begin_delta_phase.restype = None
lib.update_queue.restype = None
lib.get_r.restype = ctypes.c_double

DELTA = 1.0
WARMUP_COVERAGE = 0.25
WARMUP_MIN_ROUNDS = 4
SEED_LOW = -1024.0
SEED_HIGH = 1024.0
NUM_TOL = 1e-12
PENALTY_SCALE = 1e6
INPUT_ABS_BOUND = 1.0e4
MAX_OBJECTIVE = 1.0e6
effective_input_path = os.path.join(path_helper.get_output_dir(), "effective_input.txt")

FLAG_NEW_COVERAGE = 1
FLAG_TARGET_COVERED = 2
FLAG_ALL_COVERED = 4

class CoverageComplete(Exception):
    pass

class TargetCovered(Exception):
    pass


_target_argtypes_dim = -1
_raise_on_target_covered = False
seedid_to_input = {}


def sanitize_vector(x):
    values = np.asarray(x, dtype=np.float64)
    values = np.nan_to_num(values, nan=0.0, posinf=INPUT_ABS_BOUND, neginf=-INPUT_ABS_BOUND)
    return np.clip(values, -INPUT_ABS_BOUND, INPUT_ABS_BOUND)


def normalize_objective(r_value):
    if not np.isfinite(r_value):
        return MAX_OBJECTIVE
    safe = max(0.0, float(r_value))
    return float(np.log1p(safe / PENALTY_SCALE))


def call_target(x):
    global _target_argtypes_dim
    values = sanitize_vector(x)
    if values.size != _target_argtypes_dim:
        lib.__coverme_target_function.argtypes = [ctypes.c_double] * int(values.size)
        _target_argtypes_dim = int(values.size)
    lib.__coverme_target_function(*[float(v) for v in values])


def random_seed_vector(input_dim):
    return np.random.uniform(SEED_LOW, SEED_HIGH, size=input_dim).astype(np.float64)


def mybounds(**kwargs):
    x_new = kwargs.get("x_new")
    if x_new is None:
        return True
    values = np.asarray(x_new, dtype=np.float64)
    return bool(np.all(np.isfinite(values)) and np.all(np.abs(values) <= INPUT_ABS_BOUND + NUM_TOL))


def record_seed_if_needed(values, result):
    if (result.flags & FLAG_NEW_COVERAGE) and result.seedId >= 0:
        snapshot = np.asarray(values, dtype=np.float64).copy()
        seedid_to_input[result.seedId] = snapshot
        with open(effective_input_path, "a", encoding="utf-8") as fp:
            fp.write(f"{result.seedId}," + ",".join(f"{float(v):.17g}" for v in snapshot) + "\n")


def raise_if_stop(flags):
    if flags & FLAG_ALL_COVERED:
        raise CoverageComplete()
    if _raise_on_target_covered and (flags & FLAG_TARGET_COVERED):
        raise TargetCovered()


def func_py(x):
    values = sanitize_vector(x)

    lib.begin_self_phase()
    call_target(values)
    self_result = lib.finish_sample()
    self_r = normalize_objective(float(lib.get_r()))
    record_seed_if_needed(values, self_result)
    raise_if_stop(self_result.flags)

    if self_result.flags & FLAG_NEW_COVERAGE:
        lib.begin_base_phase()
        call_target(values)
        base_result = lib.finish_sample()
        record_seed_if_needed(values, base_result)
        raise_if_stop(base_result.flags)

        for idx in range(values.size):
            sample = values.copy()
            sample[idx] += DELTA
            lib.begin_delta_phase()
            call_target(sample)
            delta_result = lib.finish_sample()
            record_seed_if_needed(sample, delta_result)
            raise_if_stop(delta_result.flags)

        lib.update_queue()

    return self_r
    
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Coverage Algorithm based on Tree Select")
    parser.add_argument("-n", "--niter", type=int, default=5, help="Iteration number of BasinHopping")
    parser.add_argument("--stepSize", type=float, default=300.0, help="Step size")
    args = parser.parse_args()

    lib.initialize_runtime()
    open(effective_input_path, "w", encoding="utf-8").close()

    input_dim = lib.get_arg_count()
    total_exits = lib.get_br_count() * 2

    def coverage_ratio():
        return float(lib.nExplored()) / float(total_exits)

    start_time = time.process_time()
    
    try:
        iteration_count = 0

        warmup_round = 0
        while warmup_round < WARMUP_MIN_ROUNDS or coverage_ratio() < WARMUP_COVERAGE:
            lib.warmup_target(int(np.random.randint(0, total_exits)))
            try:
                _raise_on_target_covered = True
                x0 =np.random.uniform(SEED_LOW, SEED_HIGH, size=input_dim).astype(np.float64)
                op.basinhopping(
                    func_py,
                    x0,
                    minimizer_kwargs={"method": "powell", "options": {"xtol": NUM_TOL, "ftol": NUM_TOL}},
                    niter=args.niter,
                    stepsize=args.stepSize,
                    accept_test=mybounds,
                )
            except TargetCovered:
                pass
            finally:
                _raise_on_target_covered = False

            warmup_round += 1
            iteration_count += 1
            if iteration_count % 100 == 0:
                print(f"({iteration_count}, {coverage_ratio():.2%})")

        while True:
            target_and_seed = lib.pop_queue_target()
            if target_and_seed.targetId == -1:
                break
            try:
                _raise_on_target_covered = True
                if target_and_seed.seedId in seedid_to_input:
                    x0 = seedid_to_input[target_and_seed.seedId].copy()
                else:
                    x0 = random_seed_vector(input_dim)
                op.basinhopping(
                    func_py,
                    x0,
                    minimizer_kwargs={"method": "powell", "options": {"xtol": NUM_TOL, "ftol": NUM_TOL}},
                    niter=args.niter,
                    stepsize=args.stepSize,
                    accept_test=mybounds,
                )
            except TargetCovered:
                continue
            finally:
                _raise_on_target_covered = False
            
            iteration_count += 1
            if iteration_count % 100 == 0:
                print(f"({iteration_count}, {coverage_ratio():.2%})")

    except CoverageComplete:
        pass

    end_time = time.process_time()
    print(f"Final coverage = {coverage_ratio():.2%}")
    print(f"Total process time = {end_time - start_time:.2f} seconds")