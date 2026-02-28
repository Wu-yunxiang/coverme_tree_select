#!/usr/bin/env python3
"""
Coverage test runner for coverme_tree_select.

Runs the coverage algorithm against multiple benchmark targets and reports
the achieved coverage for each. Also tests the simple_func.c test target.
"""

import os
import sys
import shutil
import subprocess
import time
import json

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(ROOT_DIR, "build")
TARGET_INPUT = os.path.join(ROOT_DIR, "target_input.txt")
OUTPUT_DIR = os.path.join(ROOT_DIR, "output")
EFFECTIVE_INPUT = os.path.join(OUTPUT_DIR, "effective_input.txt")
COVERAGE_TIMEOUT = 120

# Test targets: (source_path_relative_to_root, function_name, min_expected_coverage)
TEST_TARGETS = [
    ("tests/simple_func.c", "simple_func", 1.0),
    ("benchs/foo/foo.c", "foo_raw", 1.0),
    ("benchs/if_sequential/foo.c", "foo", 1.0),
    ("benchs/if_nested/foo.c", "foo", 0.5),
    ("benchs/if_complex/foo.c", "foo", 0.5),
    ("benchs/loop_incr/foo.c", "foo", 0.5),
    ("benchs/foo_select/foo.c", "foo_raw", 0.5),
]


def write_target_input(source_path, function_name):
    """Update target_input.txt with the given source and function."""
    abs_path = os.path.join(ROOT_DIR, source_path)
    with open(TARGET_INPUT, "w") as f:
        f.write(f"{abs_path}\n{function_name}\n")


def rebuild():
    """Rebuild the project."""
    subprocess.run(
        ["cmake", "--build", BUILD_DIR, "--clean-first"],
        cwd=ROOT_DIR,
        capture_output=True,
        text=True,
    )


def configure_and_build():
    """Configure and build the project."""
    env = os.environ.copy()
    result = subprocess.run(
        ["cmake", "-S", ".", "-B", "build"],
        cwd=ROOT_DIR,
        capture_output=True,
        text=True,
        env=env,
    )
    if result.returncode != 0:
        return False, result.stderr
    result = subprocess.run(
        ["cmake", "--build", "build"],
        cwd=ROOT_DIR,
        capture_output=True,
        text=True,
        env=env,
    )
    if result.returncode != 0:
        return False, result.stderr
    return True, ""


def run_coverage(niter=5, step_size=300):
    """Run coverage algorithm and return (coverage_ratio, elapsed_time, output_text)."""
    result = subprocess.run(
        [sys.executable, "src/coverage_algorithm.py", "-n", str(niter), "--stepSize", str(step_size)],
        cwd=ROOT_DIR,
        capture_output=True,
        text=True,
        timeout=COVERAGE_TIMEOUT,
    )
    output = result.stdout + result.stderr
    coverage = None
    elapsed = None
    for line in output.splitlines():
        if "Final coverage" in line:
            # Parse "Final coverage = XX.XX%"
            pct = line.split("=")[1].strip().rstrip("%")
            coverage = float(pct) / 100.0
        if "Total process time" in line:
            elapsed = float(line.split("=")[1].strip().split()[0])
    return coverage, elapsed, output


def read_effective_inputs():
    """Read generated effective inputs."""
    if not os.path.exists(EFFECTIVE_INPUT):
        return []
    with open(EFFECTIVE_INPUT) as f:
        lines = [l.strip() for l in f if l.strip()]
    return lines


def run_test(source_path, function_name, min_coverage):
    """Run a single coverage test. Returns (passed, coverage, elapsed, details)."""
    # Update target
    write_target_input(source_path, function_name)

    # Clean and rebuild
    if os.path.isdir(BUILD_DIR):
        shutil.rmtree(BUILD_DIR)
    ok, err = configure_and_build()
    if not ok:
        return False, None, None, f"Build failed: {err}"

    # Run coverage
    try:
        coverage, elapsed, output = run_coverage()
    except subprocess.TimeoutExpired:
        return False, None, None, f"Timeout ({COVERAGE_TIMEOUT}s)"
    except Exception as e:
        return False, None, None, str(e)

    if coverage is None:
        return False, None, None, f"Could not parse coverage from output:\n{output}"

    inputs = read_effective_inputs()
    passed = coverage >= min_coverage

    details = (
        f"Coverage: {coverage:.2%}, Time: {elapsed:.2f}s, "
        f"Effective inputs: {len(inputs)}"
    )
    return passed, coverage, elapsed, details


def main():
    print("=" * 70)
    print("CoverMe Tree Select - Coverage Test Suite")
    print("=" * 70)
    print()

    results = []
    total_passed = 0
    total_tests = len(TEST_TARGETS)

    for i, (source, func, min_cov) in enumerate(TEST_TARGETS):
        print(f"[{i+1}/{total_tests}] Testing: {source} :: {func} (min coverage: {min_cov:.0%})")
        passed, coverage, elapsed, details = run_test(source, func, min_cov)
        status = "PASS" if passed else "FAIL"
        if passed:
            total_passed += 1
        print(f"  {status}: {details}")
        results.append({
            "source": source,
            "function": func,
            "min_coverage": min_cov,
            "passed": passed,
            "coverage": coverage,
            "elapsed": elapsed,
            "details": details,
        })
        print()

    # Print summary
    print("=" * 70)
    print(f"Results: {total_passed}/{total_tests} tests passed")
    print("=" * 70)

    # Print table
    print(f"\n{'Target':<45} {'Function':<15} {'Coverage':<12} {'Time':<10} {'Status':<6}")
    print("-" * 88)
    for r in results:
        cov_str = f"{r['coverage']:.2%}" if r['coverage'] is not None else "N/A"
        time_str = f"{r['elapsed']:.2f}s" if r['elapsed'] is not None else "N/A"
        status = "PASS" if r['passed'] else "FAIL"
        print(f"{r['source']:<45} {r['function']:<15} {cov_str:<12} {time_str:<10} {status:<6}")

    # Restore original target_input.txt for the simple test
    write_target_input("tests/simple_func.c", "simple_func")

    return 0 if total_passed == total_tests else 1


if __name__ == "__main__":
    sys.exit(main())
