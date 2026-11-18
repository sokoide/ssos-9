#!/usr/bin/env python3
"""Repeat XEiJ suites and compare median/p95 timing reports."""

import argparse
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
SUITE = ROOT / "tools" / "xeij-suite.py"
RESTART_CASES = {"a3", "h5"}
SCHEMA = 1


def percentile(values, fraction):
    """Nearest-rank percentile (p95 is the 95th ordered observation)."""
    ordered = sorted(float(value) for value in values)
    if not ordered:
        raise ValueError("cannot summarize an empty sample")
    rank = max(1, math.ceil(fraction * len(ordered)))
    return ordered[rank - 1]


def summarize(values):
    values = [float(value) for value in values]
    if not values:
        raise ValueError("cannot summarize an empty sample")
    return {
        "n": len(values),
        "median": statistics.median(values),
        "p95": percentile(values, 0.95),
        "min": min(values),
        "max": max(values),
    }


def run_metrics(run):
    case_times = {
        item["case"]: float(item["seconds"])
        for item in run["suite_result"]["cases"]
        if item.get("status") == "pass"
    }
    expected = set(run["cases"])
    if set(case_times) != expected:
        raise ValueError("iteration does not contain a passing result for every case")
    return {
        "suite_seconds": float(run["seconds"]),
        "case_seconds": sum(case_times.values()),
        "session_reuse_seconds": sum(
            seconds for case, seconds in case_times.items()
            if case not in RESTART_CASES
        ),
        "emulator_restart_case_seconds": sum(
            seconds for case, seconds in case_times.items()
            if case in RESTART_CASES
        ),
    }


def environment_identity(suite_result):
    return {
        "host": suite_result.get("host"),
        "artifacts": suite_result.get("artifacts"),
        "emulator_artifacts": suite_result.get("emulator_artifacts"),
        "originals_before": suite_result.get("originals_before"),
    }


def evaluate(baseline, candidate, expected_iterations=None):
    """Compare same-case runs and return detailed acceptance metrics."""
    if baseline.get("schema") != SCHEMA or candidate.get("schema") != SCHEMA:
        raise ValueError("unsupported benchmark report schema")
    if baseline.get("suite") != candidate.get("suite"):
        raise ValueError("baseline and candidate suite groups differ")
    if baseline.get("cases") != candidate.get("cases"):
        raise ValueError("baseline and candidate case lists differ")
    if not baseline.get("all_passed") or not candidate.get("all_passed"):
        raise ValueError("benchmark reports must contain only complete passing runs")
    old_runs, new_runs = baseline.get("runs", []), candidate.get("runs", [])
    expected = expected_iterations
    if expected is None:
        expected = 30 if baseline["suite"] == "short" else 10
    if len(old_runs) != expected or len(new_runs) != expected:
        raise ValueError(f"comparison requires {expected} runs per report")

    for report in (baseline, candidate):
        if any(run.get("status") != "pass" or run.get("exit_code") != 0
               for run in report["runs"]):
            raise ValueError("benchmark contains a failed or incomplete iteration")
        identities = {
            json.dumps(environment_identity(run["suite_result"]), sort_keys=True)
            for run in report["runs"]
        }
        if len(identities) != 1:
            raise ValueError("host, build artifacts, or original media changed between iterations")
    old_identity = environment_identity(old_runs[0]["suite_result"])
    new_identity = environment_identity(new_runs[0]["suite_result"])
    if old_identity != new_identity:
        raise ValueError("baseline and candidate must use the same host, artifacts, and original media")
    old_values = [run_metrics(run) for run in old_runs]
    new_values = [run_metrics(run) for run in new_runs]
    threshold = 0.20 if baseline["suite"] == "short" else 0.30
    summary = {}
    for metric in ("suite_seconds", "session_reuse_seconds"):
        old_stats = summarize([row[metric] for row in old_values])
        new_stats = summarize([row[metric] for row in new_values])
        reduction = 1.0 - new_stats["median"] / old_stats["median"]
        summary[metric] = {
            "baseline": old_stats,
            "candidate": new_stats,
            "median_reduction": reduction,
            "median_target": threshold,
            "median_target_met": reduction >= threshold,
            "p95_not_regressed": new_stats["p95"] <= old_stats["p95"],
        }
    summary["emulator_restart_case_seconds"] = {
        "baseline": summarize([row["emulator_restart_case_seconds"] for row in old_values]),
        "candidate": summarize([row["emulator_restart_case_seconds"] for row in new_values]),
    }
    summary["passed"] = (
        summary["suite_seconds"]["median_target_met"]
        and summary["suite_seconds"]["p95_not_regressed"]
        and summary["session_reuse_seconds"]["p95_not_regressed"]
    )
    return summary


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n")
    temporary.replace(path)


def run_suite(args):
    if args.iterations < 1:
        raise ValueError("iterations must be positive")
    if not args.cases or len(set(args.cases)) != len(args.cases):
        raise ValueError("provide a non-empty list of unique case IDs")
    root = args.results.resolve()
    root.mkdir(parents=True, exist_ok=True)
    report = {
        "schema": SCHEMA,
        "suite": args.suite,
        "host": platform.platform(),
        "cases": args.cases,
        "iterations_requested": args.iterations,
        "all_passed": False,
        "runs": [],
    }
    report_path = root / "benchmark.json"
    atomic_write(report_path, report)
    for iteration in range(1, args.iterations + 1):
        iteration_dir = root / f"run-{iteration:02d}"
        iteration_dir.mkdir(exist_ok=False)
        log_path = iteration_dir / "suite.log"
        command = [sys.executable, str(SUITE), "--results", str(iteration_dir)]
        if args.require_profile:
            command.append("--require-profile")
        command.extend(args.cases)
        print(f"[{args.suite} {iteration}/{args.iterations}] starting", flush=True)
        started = time.monotonic()
        with log_path.open("wb") as log:
            child = subprocess.Popen(
                command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                env=os.environ.copy(),
            )
            assert child.stdout is not None
            for line in child.stdout:
                log.write(line)
                sys.stdout.buffer.write(line)
                sys.stdout.buffer.flush()
            exit_code = child.wait()
        elapsed = time.monotonic() - started
        result_files = sorted(iteration_dir.glob("*/result.json"))
        suite_result = json.loads(result_files[-1].read_text()) if result_files else None
        status = "pass" if exit_code == 0 and suite_result else "fail"
        run = {
            "iteration": iteration,
            "status": status,
            "exit_code": exit_code,
            "seconds": elapsed,
            "cases": args.cases,
            "suite_result_path": str(result_files[-1].relative_to(root)) if result_files else None,
            "suite_result": suite_result,
            "log": str(log_path.relative_to(root)),
        }
        if suite_result:
            current_identity = environment_identity(suite_result)
            prior = report["runs"]
            if prior and environment_identity(prior[0]["suite_result"]) != current_identity:
                run["status"] = "fail"
                run["environment_error"] = "host, build artifacts, or original media changed"
                exit_code = exit_code or 3
        report["runs"].append(run)
        atomic_write(report_path, report)
        if status != "pass":
            print(f"[{args.suite} {iteration}/{args.iterations}] failed; stopping repeats", flush=True)
            return exit_code or 1
        print(f"[{args.suite} {iteration}/{args.iterations}] PASS {elapsed:.2f}s", flush=True)
    report["all_passed"] = True
    report["summary"] = {
        metric: summarize([run_metrics(run)[metric] for run in report["runs"]])
        for metric in (
            "suite_seconds", "case_seconds", "session_reuse_seconds",
            "emulator_restart_case_seconds",
        )
    }
    atomic_write(report_path, report)
    print(f"benchmark report: {report_path}", flush=True)
    print(json.dumps(report["summary"], indent=2), flush=True)
    return 0


def compare(args):
    baseline = json.loads(args.baseline.read_text())
    candidate = json.loads(args.candidate.read_text())
    expected = args.iterations
    result = evaluate(baseline, candidate, expected)
    result["suite"] = baseline["suite"]
    result["cases"] = baseline["cases"]
    result["baseline_report"] = str(args.baseline)
    result["candidate_report"] = str(args.candidate)
    atomic_write(args.output, result)
    print(json.dumps(result, indent=2))
    print(f"comparison report: {args.output}")
    return 0 if result["passed"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("run", help="run a suite repeatedly and record each attempt")
    run.add_argument("--suite", choices=("short", "full"), required=True)
    run.add_argument("--iterations", type=int, required=True)
    run.add_argument("--results", type=Path, required=True)
    run.add_argument("--require-profile", action="store_true")
    run.add_argument("cases", nargs="+")
    run.set_defaults(handler=run_suite)
    compare_parser = commands.add_parser("compare", help="compare repeated-run timing reports")
    compare_parser.add_argument("--baseline", type=Path, required=True)
    compare_parser.add_argument("--candidate", type=Path, required=True)
    compare_parser.add_argument("--iterations", type=int)
    compare_parser.add_argument("--output", type=Path, required=True)
    compare_parser.set_defaults(handler=compare)
    args = parser.parse_args()
    try:
        return args.handler(args)
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        parser.exit(2, f"benchmark error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
