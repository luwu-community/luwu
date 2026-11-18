#!/usr/bin/env python3
"""Benchmark Luna; optionally alternate with a baseline compiler binary."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path(__file__).resolve().parents[2] / "build/debug/luwu-dsl-compiler")
    parser.add_argument("--baseline", type=Path, help="optional earlier compiler executable")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--iterations", type=int, default=100000)
    args = parser.parse_args()
    if min(args.runs, args.iterations) < 1:
        parser.error("runs and iterations must be positive")
    n = args.iterations
    workloads = {
        "tail-arithmetic": f"(let loop ([n {n}] [sum 0]) (if (= n 0) sum (loop (- n 1) (+ sum n))))",
        "captured-mutation": f"""(define tick (let ([n 0]) (lambda () (set! n (+ n 1)) n)))
            (let loop ([n {n}]) (if (= n 0) (tick) (begin (tick) (loop (- n 1)))))""",
        "list-map": f"(foldl + 0 (map (lambda (x) (+ x 1)) (range 0 {n})))",
        "macro-expansion": """(define-syntax inc (syntax-rules () [(_ x) (+ x 1)]))
            (define result 0)\n""" + "\n".join("(set! result (inc result))" for _ in range(max(1, n // 100))) + "\nresult",
    }
    report = {"timing": "whole process, including loading, expansion, compilation and startup", "workloads": {}}
    with tempfile.TemporaryDirectory(prefix="luna-vm-benchmark-") as directory:
        for workload, source in workloads.items():
            script = Path(directory) / f"{workload}.luna"
            script.write_text(source)
            compilers = {"current": args.compiler}
            if args.baseline:
                compilers["baseline"] = args.baseline
            samples = {name: [] for name in compilers}
            expected = None
            for run in range(args.runs):
                order = list(compilers) if run % 2 == 0 else list(reversed(compilers))
                for name in order:
                    start = time.perf_counter()
                    result = subprocess.run([str(compilers[name].resolve()), "--eval", str(script)],
                                            check=True, capture_output=True, text=True, timeout=120)
                    samples[name].append(time.perf_counter() - start)
                    if expected is not None and result.stdout != expected:
                        raise RuntimeError(f"{script.stem}: compiler results differ")
                    expected = result.stdout
            medians = {compiler: statistics.median(values) for compiler, values in samples.items()}
            report["workloads"][workload] = {"samples_seconds": samples, "medians_seconds": medians,
                                        "result": expected.strip()}
            if args.baseline:
                report["workloads"][workload]["current_over_baseline"] = medians["current"] / medians["baseline"]
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
