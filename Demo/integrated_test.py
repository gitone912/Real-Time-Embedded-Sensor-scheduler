#!/usr/bin/env python3
"""
Integrated Test Runner for Timeline Scheduler (refactored with visualization)
"""

from __future__ import annotations

import argparse
import os
import sys
from datetime import datetime
from pathlib import Path
from typing import Optional

from Tools import ui, suite, log_parser, checks as checks_mod, reporting
from Tools.build import BuildSystem
from Tools.models import TestMeta, TestMetrics, TestResult, CheckResult

# Import visualization if matplotlib is available
try:
    from Tools import visualization
    VISUALIZATION_AVAILABLE = True
except ImportError:
    VISUALIZATION_AVAILABLE = False
    print("[WARNING] matplotlib not available - visualization disabled")


SUITE_DIR_NAME = "suite"
REPORT_DIR_NAME = "test_reports"


def find_project_root() -> str:
    """Walk upwards from CWD until we find Demo/Makefile."""
    current = Path.cwd()
    for parent in [current] + list(current.parents):
        if (parent / "Demo" / "Makefile").exists():
            return str(parent)
    print("[ERROR] Cannot find project root (no Demo/Makefile found).")
    print("        Run this script from within the project directory.")
    raise SystemExit(1)


class IntegratedTestRunner:
    def __init__(self,
                 suite_dir: Optional[str] = None,
                 interactive: bool = True,
                 native_linux: bool = False,
                 save_report: bool = True,
                 generate_charts: bool = True):
        self.interactive = interactive
        self.native_linux = native_linux
        self.save_report = save_report
        self.generate_charts = generate_charts and VISUALIZATION_AVAILABLE

        self.project_root = find_project_root()
        self.demo_dir = os.path.join(self.project_root, "Demo")
        self.log_file = os.path.join(self.project_root, "logs", "uart.log")
        self.report_dir = os.path.join(self.project_root, REPORT_DIR_NAME)
        self.charts_dir = os.path.join(self.report_dir, "charts")

        self.suite_dir = suite_dir or os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            SUITE_DIR_NAME
        )

        self._session_ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.buildsys = BuildSystem(
            project_root=self.project_root,
            demo_dir=self.demo_dir,
            log_file=self.log_file,
            native_linux=self.native_linux,
        )

    # ── suite discovery ─────────────────────────────────────────

    def list_configs(self) -> list[Path]:
        return suite.list_configs(self.suite_dir)

    def find_config_by_id(self, config_id: str) -> Optional[Path]:
        return suite.find_config_by_id(self.suite_dir, config_id)

    def print_available_configs(self) -> None:
        configs = self.list_configs()
        if not configs:
            print(f"No configs found in: {self.suite_dir}")
            return

        ui.banner(f"Available configurations  ({self.suite_dir})")
        for i, p in enumerate(configs, 1):
            try:
                meta = suite.read_meta(p)
                print(f"\n  [{i}] {meta.name}")
                print(f"      id   : {meta.id}")
                print(f"      file : {p.name}")
                print(f"      desc : {meta.description[:80]}")
            except Exception:
                print(f"  {p.stem}  (could not read metadata)")

    # ── high level actions ──────────────────────────────────────

    def clean(self) -> bool:
        return self.buildsys.clean()

    def build(self, config_path: Optional[Path] = None) -> bool:
        task_cfg = str(config_path) if config_path else None
        return self.buildsys.build(task_config_path=task_cfg)

    def run_qemu(self) -> bool:
        return self.buildsys.run_qemu()

    # ── analysis ────────────────────────────────────────────────

    def analyze(self, meta: Optional[TestMeta] = None, config_path: Optional[Path] = None) -> TestResult:
        if meta is None:
            meta = TestMeta(name="(manual)", id="manual", description="")

        # Read timing configuration if provided
        major_frame_ms = 100  # default
        minor_frame_ms = 10   # default
        if config_path is not None:
            major_frame_ms, minor_frame_ms = suite.read_timing_config(config_path)

        ui.header(f"Analyze: {meta.name}")
        metrics = log_parser.parse_log(self.log_file, major_frame_ms, minor_frame_ms)
        reporting.print_metrics_console(metrics)
        check_list = checks_mod.run_checks(meta, metrics, self.log_file)
        overall = self._print_checks(check_list)

        result = TestResult(meta=meta, metrics=metrics, checks=check_list, overall=overall)

        # Generate visualizations
        if self.generate_charts:
            self._generate_visualizations(meta, metrics)

        self._maybe_save_result(result)
        return result

    def _print_checks(self, check_list: list[CheckResult]) -> bool:
        ui.section("Pass / Fail Checks")
        overall = True
        for c in check_list:
            if c.passed:
                ui.ok(f"{c.label}  ({c.detail})")
            else:
                ui.fail(f"{c.label}  ({c.detail})")
                overall = False
        verdict = "PASSED" if overall else "FAILED"
        print(f"\n    {'─' * 44}")
        print(f"    Overall result:  {verdict}")
        return overall

    # ── visualization ───────────────────────────────────────────

    def _generate_visualizations(self, meta: TestMeta, metrics: TestMetrics) -> None:
        """Generate charts for the current test."""
        if not VISUALIZATION_AVAILABLE:
            return

        ui.section("Generating Charts")
        try:
            chart_files = visualization.generate_all_charts(
                log_file=self.log_file,
                task_delays=metrics.task_delays,
                output_dir=self.charts_dir,
                test_name=f"{self._session_ts}_{meta.name}",
                major_frame_ms=metrics.major_frame_ms
            )

            if chart_files:
                ui.info(f"Generated {len(chart_files)} chart(s) → {self.charts_dir}")
            else:
                ui.info("No charts generated (insufficient data)")
        except Exception as e:
            ui.fail(f"Chart generation failed: {e}")

    # ── reporting ───────────────────────────────────────────────

    def _maybe_save_result(self, result: TestResult) -> None:
        if not self.save_report:
            return
        reporting.save_result(self.report_dir, self._session_ts, result, info=ui.info)

    def _maybe_save_suite_summary(self, results: list[TestResult]) -> None:
        if not self.save_report:
            return
        report_path = reporting.save_suite_summary(self.report_dir, self._session_ts, results)
        print(f"\n  Full report saved  →  {report_path}")
        if self.generate_charts:
            print(f"  Charts directory   →  {self.charts_dir}")

    # ── single test ─────────────────────────────────────────────

    def run_single(self, config_path: Path, do_clean: bool = True, next_config: Optional[Path] = None) -> TestResult:
        meta = suite.read_meta(config_path)

        ui.header(f"TEST: {meta.name}")
        print(f"  ID         : {meta.id}")
        print(f"  Config     : {config_path.name}")
        print(f"  Description: {meta.description}")

        ui.pause(self.interactive, "Ready to build and run this configuration?")

        if do_clean:
            self.clean()

        if not self.build(config_path):
            result = TestResult(
                meta=meta, metrics=TestMetrics(),
                checks=[CheckResult("Build", False, "Build failed")],
                overall=False
            )
            self._maybe_save_result(result)
            return result

        if not self.run_qemu():
            result = TestResult(
                meta=meta, metrics=TestMetrics(),
                checks=[CheckResult("QEMU run", False, "QEMU did not produce valid log")],
                overall=False
            )
            self._maybe_save_result(result)
            return result

        ui.pause(
            self.interactive,
            "QEMU finished — read the output above, then press ENTER to run the analysis"
        )

        result = self.analyze(meta, config_path)

        if next_config is not None:
            nmeta = suite.read_meta(next_config)
            ui.pause(self.interactive, f"Analysis saved. Next test → [{nmeta.id}]  {nmeta.name}  ({next_config.name})")
        else:
            ui.pause(self.interactive, "Analysis saved. Last test complete — press ENTER to see the suite summary")

        return result

    # ── full suite ──────────────────────────────────────────────

    def run_all(self, do_clean_between: bool = True) -> int:
        configs = self.list_configs()
        if not configs:
            print(f"[ERROR] No test configs found in: {self.suite_dir}")
            return 1

        mode_bits = []
        if not self.interactive:
            mode_bits.append("CI mode")
        mode_bits.append("native Linux" if self.native_linux else "via Docker")
        if self.generate_charts:
            mode_bits.append("with charts")
        mode = ", ".join(mode_bits)

        ui.banner(f"TIMELINE SCHEDULER TEST SUITE  —  {len(configs)} tests  ({mode})")
        ui.info(f"Suite dir   : {self.suite_dir}")
        ui.info(f"Report dir  : {self.report_dir}")
        ui.info(f"Session     : {self._session_ts}")
        ui.info(f"Execution   : {'native Linux' if self.native_linux else 'Docker container'}")
        if self.generate_charts:
            ui.info(f"Charts      : {self.charts_dir}")
        print()

        for i, cfg in enumerate(configs, 1):
            try:
                meta = suite.read_meta(cfg)
                print(f"  [{i}/{len(configs)}]  {meta.id:<28}  {meta.name}")
            except Exception:
                print(f"  [{i}/{len(configs)}]  {cfg.stem}")

        ui.pause(self.interactive, f"About to run all {len(configs)} tests in sequence. Ready?")

        results: list[TestResult] = []
        for i, cfg in enumerate(configs):
            next_cfg = configs[i + 1] if i + 1 < len(configs) else None
            results.append(self.run_single(cfg, do_clean=do_clean_between, next_config=next_cfg))

        self._print_suite_summary(results)
        self._maybe_save_suite_summary(results)

        failed = sum(1 for r in results if not r.overall)
        return 0 if failed == 0 else 1

    def _print_suite_summary(self, results: list[TestResult]) -> None:
        ui.banner("TEST SUITE SUMMARY")
        width = max(len(r.meta.name) for r in results) + 2 if results else 10
        for r in results:
            verdict = "PASSED" if r.overall else "FAILED"
            sym = "✓" if r.overall else "✗"
            fails = "  →  " + " | ".join(
                f"{c.label} ({c.detail})" for c in r.checks if not c.passed
            ) if not r.overall else ""
            print(f"  {sym}  {r.meta.name:<{width}}  {verdict}{fails}")

        passed = sum(1 for r in results if r.overall)
        total = len(results)
        print(f"\n  Total: {total}  |  Passed: {passed}  |  Failed: {total - passed}")


def cli_main() -> None:
    parser = argparse.ArgumentParser(
        description="Integrated test runner for timeline scheduler",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 integrated_test.py                         run all tests (interactive, via Docker)
  python3 integrated_test.py --linux                 run all tests (native Linux, no Docker)
  python3 integrated_test.py --config baseline       run one test by id
  python3 integrated_test.py --list                  list available tests
  python3 integrated_test.py --action analyze        only analyze existing log
  python3 integrated_test.py --no-charts             skip chart generation
  python3 integrated_test.py --no-pause              CI mode, no interactive pauses
"""
    )
    parser.add_argument(
        "--action",
        choices=["build", "test", "analyze", "clean", "full"],
        default="full",
        help="Action to perform (default: full = run all tests)"
    )
    parser.add_argument(
        "--config", metavar="ID",
        help="Run only the test config matching this id (e.g. baseline)"
    )
    parser.add_argument(
        "--suite-dir", metavar="DIR",
        help="Directory containing test JSON configs (default: ./suite/)"
    )
    parser.add_argument(
        "--list", action="store_true",
        help="List available test configurations and exit"
    )
    parser.add_argument(
        "--no-clean", action="store_true",
        help="Skip clean between tests"
    )
    parser.add_argument(
        "--no-pause", action="store_true",
        help="Disable interactive pauses (CI/CD mode)"
    )
    parser.add_argument(
        "--linux", action="store_true",
        help="Run natively on Linux (no Docker, direct make commands)"
    )
    parser.add_argument(
        "--no-save-report", action="store_true",
        help="Disable report file generation (default: reports are saved)"
    )
    parser.add_argument(
        "--no-charts", action="store_true",
        help="Disable chart generation (default: charts are generated if matplotlib available)"
    )

    args = parser.parse_args()
    runner = IntegratedTestRunner(
        suite_dir=args.suite_dir,
        interactive=not args.no_pause,
        native_linux=args.linux,
        save_report=not args.no_save_report,
        generate_charts=not args.no_charts
    )

    if args.list:
        runner.print_available_configs()
        raise SystemExit(0)

    if args.config:
        cfg_path = runner.find_config_by_id(args.config)
        if cfg_path is None:
            print(f"[ERROR] Config '{args.config}' not found.")
            print("        Use --list to see available configs.")
            raise SystemExit(1)
        result = runner.run_single(cfg_path, do_clean=not args.no_clean)
        raise SystemExit(0 if result.overall else 1)

    if args.action == "build":
        raise SystemExit(0 if runner.build() else 1)
    if args.action == "test":
        raise SystemExit(0 if runner.run_qemu() else 1)
    if args.action == "analyze":
        runner.analyze()
        raise SystemExit(0)
    if args.action == "clean":
        runner.clean()
        raise SystemExit(0)

    raise SystemExit(runner.run_all(do_clean_between=not args.no_clean))


if __name__ == "__main__":
    cli_main()