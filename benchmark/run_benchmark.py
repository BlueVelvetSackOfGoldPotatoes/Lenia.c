#!/usr/bin/env python3
"""Benchmark: Python Lenia vs C++ Lenia.

Measures wall time, per-step time, peak memory, final mass, and throughput
for both implementations across multiple board sizes and step counts.
Outputs a JSON results file and prints a comparison table.
"""
import json
import os
import subprocess
import sys
import time
import resource
import tracemalloc
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
LENIA_PY_DIR = ROOT.parent / "external_repos" / "Lenia" / "Python"
ANIMALS_PATH = LENIA_PY_DIR / "animals.json"
CPP_BINARY = ROOT / "build" / "lenia"

SIZES = [64, 128, 256, 512]
STEPS = [100, 500, 1000]
CREATURES = ["O2u"]  # Orbium unicaudatus
RESULTS_PATH = ROOT / "benchmark" / "results.json"


def ch2val(c):
    if c in ".b":
        return 0
    if c == "o":
        return 255
    if len(c) == 1:
        return ord(c) - ord("A") + 1
    return (ord(c[0]) - ord("p")) * 24 + (ord(c[1]) - ord("A") + 25)


def decode_rle_2d(rle):
    rows = []
    current_row = []
    last = ""
    count = ""
    for ch in rle.rstrip("!"):
        if ch.isdigit():
            count += ch
            continue
        if ch in "pqrstuvwxy":
            last = ch
            continue
        if ch == "$":
            rows.append(current_row)
            n = int(count) if count else 1
            for _ in range(n - 1):
                rows.append([])
            current_row = []
            last = ""
            count = ""
            continue
        val = ch2val(last + ch)
        fval = val / 255.0
        n = int(count) if count else 1
        current_row.extend([fval] * n)
        last = ""
        count = ""
    if current_row:
        rows.append(current_row)
    mc = max((len(r) for r in rows), default=0)
    arr = np.zeros((len(rows), mc))
    for i, r in enumerate(rows):
        for j, v in enumerate(r):
            arr[i, j] = v
    return arr


def load_orbium():
    with open(ANIMALS_PATH) as f:
        data = json.load(f)
    for d in data:
        if d["code"] == "O2u":
            return d, decode_rle_2d(d["cells"])
    raise ValueError("O2u not found")


def run_python_benchmark(size, steps, pattern, params):
    """Run pure-Python/NumPy Lenia and measure performance."""
    tracemalloc.start()
    R = params["R"]
    T = params["T"]
    m_val = params["m"]
    s_val = params["s"]
    dt = 1.0 / T

    world = np.zeros((size, size))
    pr = (size - pattern.shape[0]) // 2
    pc = (size - pattern.shape[1]) // 2
    world[pr : pr + pattern.shape[0], pc : pc + pattern.shape[1]] = pattern

    mid = size // 2
    kernel = np.zeros((size, size))
    for r in range(size):
        for c in range(size):
            d = np.sqrt(((r - mid) / R) ** 2 + ((c - mid) / R) ** 2)
            if 0 < d < 1:
                kernel[r, c] = np.exp(4 - 1 / (d * (1 - d)))
    ksum = kernel.sum()
    if ksum > 1e-10:
        kernel /= ksum
    kernel_fft = np.fft.fftn(kernel)

    start = time.perf_counter()
    for _ in range(steps):
        world_fft = np.fft.fftn(world)
        potential = np.fft.fftshift(np.real(np.fft.ifftn(kernel_fft * world_fft)))
        field = np.exp(-((potential - m_val) ** 2) / (2 * s_val**2)) * 2 - 1
        world = np.clip(world + dt * field, 0, 1)
    elapsed = time.perf_counter() - start

    _, peak_mem = tracemalloc.get_traced_memory()
    tracemalloc.stop()

    return {
        "wall_time_s": elapsed,
        "per_step_ms": elapsed / steps * 1000,
        "steps_per_s": steps / elapsed,
        "peak_memory_mb": peak_mem / 1024 / 1024,
        "final_mass": float(world.sum()),
    }


def run_cpp_benchmark(size, steps):
    """Run C++ Lenia binary and measure performance."""
    cmd = [
        str(CPP_BINARY),
        "--animals", str(ANIMALS_PATH),
        "--code", "O2u",
        "--steps", str(steps),
        "--size", str(size),
    ]

    # Try with /usr/bin/time for memory measurement
    mem_cmd = ["/usr/bin/time", "-v"] + cmd
    peak_mem_mb = 0
    start = time.perf_counter()
    try:
        result = subprocess.run(mem_cmd, capture_output=True, text=True, timeout=600)
        for line in result.stderr.splitlines():
            if "Maximum resident" in line:
                peak_mem_mb = float(line.split()[-1]) / 1024
        stdout = result.stdout
    except Exception:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
        stdout = result.stdout
    elapsed_total = time.perf_counter() - start

    # Parse output for mass and internal timing
    internal_time = None
    final_mass = None
    steps_per_s = None
    for line in stdout.splitlines():
        if "steps in" in line:
            import re
            m_time = re.search(r'in (\d+\.\d+)s', line)
            m_sps = re.search(r'\((\d+) steps/s\)', line)
            if m_time:
                internal_time = float(m_time.group(1))
            if m_sps:
                steps_per_s = float(m_sps.group(1))
        if "mass=" in line:
            import re
            m_mass = re.search(r'mass=(\d+\.?\d*)', line)
            if m_mass:
                final_mass = float(m_mass.group(1))

    t = internal_time or elapsed_total
    return {
        "wall_time_s": t,
        "per_step_ms": t / steps * 1000,
        "steps_per_s": steps_per_s or (steps / t),
        "peak_memory_mb": peak_mem_mb,
        "final_mass": final_mass or 0,
    }


def main():
    os.makedirs(ROOT / "benchmark", exist_ok=True)

    orbium_data, orbium_pattern = load_orbium()
    params = {
        "R": orbium_data["params"]["R"],
        "T": orbium_data["params"]["T"],
        "m": orbium_data["params"]["m"],
        "s": orbium_data["params"]["s"],
    }

    all_results = []

    print("=" * 90)
    print(f"{'Size':>6} {'Steps':>6} {'Lang':>6} {'Wall(s)':>10} {'ms/step':>10} "
          f"{'steps/s':>10} {'Mem(MB)':>10} {'Mass':>10} {'Speedup':>10}")
    print("=" * 90)

    for size in SIZES:
        for steps in STEPS:
            # Python
            print(f"{size:>6} {steps:>6} {'py':>6}", end="", flush=True)
            try:
                py = run_python_benchmark(size, steps, orbium_pattern, params)
                print(f" {py['wall_time_s']:>10.3f} {py['per_step_ms']:>10.2f} "
                      f"{py['steps_per_s']:>10.1f} {py['peak_memory_mb']:>10.1f} "
                      f"{py['final_mass']:>10.1f}", end="")
            except Exception as e:
                py = {"wall_time_s": 0, "per_step_ms": 0, "steps_per_s": 0,
                      "peak_memory_mb": 0, "final_mass": 0}
                print(f" {'FAILED':>10}", end="")
            print()

            # C++
            print(f"{size:>6} {steps:>6} {'c++':>6}", end="", flush=True)
            try:
                cpp = run_cpp_benchmark(size, steps)
                speedup = py["wall_time_s"] / cpp["wall_time_s"] if cpp["wall_time_s"] > 0 else 0
                print(f" {cpp['wall_time_s']:>10.3f} {cpp['per_step_ms']:>10.2f} "
                      f"{cpp['steps_per_s']:>10.1f} {cpp['peak_memory_mb']:>10.1f} "
                      f"{cpp['final_mass']:>10.1f} {speedup:>9.1f}x")
            except Exception as e:
                cpp = {"wall_time_s": 0, "per_step_ms": 0, "steps_per_s": 0,
                       "peak_memory_mb": 0, "final_mass": 0}
                speedup = 0
                print(f" {'FAILED':>10}")

            all_results.append({
                "size": size,
                "steps": steps,
                "python": py,
                "cpp": cpp,
                "speedup": speedup,
            })
            print("-" * 90)

    # Save results
    with open(RESULTS_PATH, "w") as f:
        json.dump({"results": all_results, "creature": "O2u", "params": params}, f, indent=2)
    print(f"\nResults saved to {RESULTS_PATH}")

    # Summary
    print("\n" + "=" * 50)
    print("SUMMARY: Average speedup by board size")
    print("=" * 50)
    for size in SIZES:
        speedups = [r["speedup"] for r in all_results if r["size"] == size and r["speedup"] > 0]
        if speedups:
            print(f"  {size}x{size}: {np.mean(speedups):.1f}x average speedup")

    # Mass comparison
    print("\n" + "=" * 50)
    print("CORRECTNESS: Final mass comparison (should match)")
    print("=" * 50)
    for r in all_results:
        if r["steps"] == 100:
            py_mass = r["python"]["final_mass"]
            cpp_mass = r["cpp"]["final_mass"]
            diff = abs(py_mass - cpp_mass)
            match = "✓" if diff < 5 else "✗"
            print(f"  {r['size']}x{r['size']}: Python={py_mass:.1f}  C++={cpp_mass:.1f}  diff={diff:.1f} {match}")


if __name__ == "__main__":
    main()
