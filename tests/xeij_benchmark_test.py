#!/usr/bin/env python3
"""Host tests for repeat-run XEiJ benchmark acceptance math."""

import importlib.util
from pathlib import Path
import unittest


TOOL = Path(__file__).resolve().parents[1] / "tools" / "xeij-benchmark.py"
SPEC = importlib.util.spec_from_file_location("xeij_benchmark", TOOL)
BENCHMARK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BENCHMARK)


def report(group, suite_seconds, warm_seconds, restart_seconds=5.0):
    cases = ["a2", "a3", "b1"]
    runs = []
    for iteration, total in enumerate(suite_seconds):
        cases_result = [
            {"case": "a2", "seconds": warm_seconds[iteration], "status": "pass"},
            {"case": "a3", "seconds": restart_seconds, "status": "pass"},
            {"case": "b1", "seconds": 2.0, "status": "pass"},
        ]
        runs.append({
            "status": "pass",
            "exit_code": 0,
            "seconds": total,
            "cases": cases,
            "suite_result": {"cases": cases_result},
        })
    return {
        "schema": BENCHMARK.SCHEMA,
        "suite": group,
        "cases": cases,
        "all_passed": True,
        "runs": runs,
    }


class BenchmarkMathTest(unittest.TestCase):
    def test_nearest_rank_p95(self):
        self.assertEqual(BENCHMARK.percentile(list(range(1, 11)), 0.95), 10)

    def test_short_gate_uses_20_percent_median_and_p95(self):
        baseline = report("short", [100.0] * 30, [40.0] * 30)
        candidate = report("short", [79.0] * 30, [38.0] * 30)
        result = BENCHMARK.evaluate(baseline, candidate)
        self.assertTrue(result["suite_seconds"]["median_target_met"])
        self.assertTrue(result["suite_seconds"]["p95_not_regressed"])
        self.assertTrue(result["passed"])

    def test_full_gate_requires_30_percent_median_reduction(self):
        baseline = report("full", [100.0] * 10, [40.0] * 10)
        candidate = report("full", [71.0] * 10, [39.0] * 10)
        result = BENCHMARK.evaluate(baseline, candidate)
        self.assertFalse(result["suite_seconds"]["median_target_met"])
        self.assertFalse(result["passed"])

    def test_session_reuse_p95_cannot_regress(self):
        baseline = report("short", [100.0] * 30, [40.0] * 30)
        candidate = report("short", [70.0] * 30, [41.0] * 30)
        result = BENCHMARK.evaluate(baseline, candidate)
        self.assertTrue(result["suite_seconds"]["median_target_met"])
        self.assertFalse(result["session_reuse_seconds"]["p95_not_regressed"])
        self.assertFalse(result["passed"])

    def test_rejects_incomplete_repeat_count(self):
        baseline = report("short", [100.0] * 29, [40.0] * 29)
        candidate = report("short", [70.0] * 30, [30.0] * 30)
        with self.assertRaisesRegex(ValueError, "requires 30 runs"):
            BENCHMARK.evaluate(baseline, candidate)

    def test_rejects_different_case_sets(self):
        baseline = report("short", [100.0] * 30, [40.0] * 30)
        candidate = report("short", [70.0] * 30, [30.0] * 30)
        candidate["cases"].append("c4")
        with self.assertRaisesRegex(ValueError, "case lists differ"):
            BENCHMARK.evaluate(baseline, candidate)

    def test_reports_session_reuse_and_emulator_restart_separately(self):
        sample = report("short", [100.0], [40.0])
        result = BENCHMARK.run_metrics(sample["runs"][0])
        self.assertEqual(result["session_reuse_seconds"], 42.0)
        self.assertEqual(result["emulator_restart_case_seconds"], 5.0)


if __name__ == "__main__":
    unittest.main()
