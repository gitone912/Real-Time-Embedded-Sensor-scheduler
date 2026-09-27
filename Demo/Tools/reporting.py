from __future__ import annotations

import os
import statistics
from datetime import datetime
from typing import Callable, Optional

from .models import TestMetrics, TestResult


# ─────────────────────────────────────────────────────────────────────────────
# Table rendering helpers
# ─────────────────────────────────────────────────────────────────────────────

def _u_line(n: int) -> str:
    return "─" * max(0, n)


def _pad(text: object, width: int, align: str = "left") -> str:
    s = "" if text is None else str(text)
    if len(s) > width:
        s = s[: max(0, width - 1)] + "…"
    if align == "right":
        return f"{s:>{width}}"
    if align == "center":
        return f"{s:^{width}}"
    return f"{s:<{width}}"


def render_table(
        headers: list[str],
        rows: list[list[object]],
        aligns: Optional[list[str]] = None,
        indent: int = 4,
        min_col_width: int = 6,
) -> list[str]:
    """Render a simple unicode table (same style as your metrics section)."""
    aligns = aligns or ["left"] * len(headers)
    if len(aligns) != len(headers):
        raise ValueError("aligns must match headers length")

    cols = len(headers)
    widths = [max(min_col_width, len(h)) for h in headers]
    for r in rows:
        for i in range(cols):
            cell = r[i] if i < len(r) else ""
            widths[i] = max(widths[i], len(str(cell)))

    sep_len = sum(widths) + 2 * (cols - 1)
    ind = " " * indent

    out: list[str] = []
    out.append(ind + "  ".join(_pad(headers[i], widths[i], aligns[i]) for i in range(cols)))
    out.append(ind + _u_line(sep_len))
    for r in rows:
        out.append(ind + "  ".join(_pad(r[i] if i < len(r) else "", widths[i], aligns[i]) for i in range(cols)))
    out.append(ind + _u_line(sep_len))
    return out


def _stat_summary(values: list[float]) -> tuple[int, float, float, float, float]:
    """samples, avg, std(population), min, max"""
    n = len(values)
    if n == 0:
        return 0, 0.0, 0.0, 0.0, 0.0
    avg = statistics.mean(values)
    std = statistics.pstdev(values) if n > 1 else 0.0
    return n, avg, std, min(values), max(values)


# ─────────────────────────────────────────────────────────────────────────────
# Metrics formatting (console + report)
# ─────────────────────────────────────────────────────────────────────────────

def _build_count_rows(m: TestMetrics) -> list[list[object]]:
    return [
        ["HRT Task Starts", m.hrt_starts],
        ["HRT Task Completions", m.hrt_completions],
        ["Deadline Misses", m.deadline_misses],
        ["SRT Task Starts", m.srt_starts],
        ["SRT Task Completions", m.srt_completions],
        ["SRT Preemptions", m.srt_preempts],
        ["SRT Killed", m.srt_killed],
        ["Major Frame Completions", m.major_frames],
    ]


def _build_stats_rows(m: TestMetrics) -> list[list[object]]:
    rows: list[list[object]] = []

    def add(name: str, values: list[float], avg_fmt: str, std_fmt: str, minmax_fmt: str) -> None:
        n, avg, std, mn, mx = _stat_summary(values)
        rows.append([name, n, avg_fmt.format(avg), std_fmt.format(std), minmax_fmt.format(mn), minmax_fmt.format(mx)])

    if m.idle_ticks:
        add("Idle Ticks", [float(x) for x in m.idle_ticks], "{:.1f}", "{:.1f}", "{:.0f}")
    else:
        rows.append(["Idle Ticks", 0, "-", "-", "-", "-"])

    if m.overhead_cycles:
        add("Kernel Overhead (cycles)", [float(x) for x in m.overhead_cycles], "{:.1f}", "{:.1f}", "{:.0f}")
    else:
        rows.append(["Kernel Overhead (cycles)", 0, "-", "-", "-", "-"])

    if m.overhead_percentages:
        add("Kernel Overhead (%)", [float(x) for x in m.overhead_percentages], "{:.2f}%", "{:.2f}%", "{:.2f}%")
    else:
        rows.append(["Kernel Overhead (%)", 0, "-", "-", "-", "-"])

    if m.delay_max_cycles:
        add("Delay Max (cycles)", [float(x) for x in m.delay_max_cycles], "{:.1f}", "{:.1f}", "{:.0f}")
    else:
        rows.append(["Delay Max (cycles)", 0, "-", "-", "-", "-"])

    if m.delay_min_cycles:
        add("Delay Min (cycles)", [float(x) for x in m.delay_min_cycles], "{:.1f}", "{:.1f}", "{:.0f}")
    else:
        rows.append(["Delay Min (cycles)", 0, "-", "-", "-", "-"])

    return rows


def _build_jitter_tables(m: TestMetrics) -> tuple[list[list[object]], Optional[list[object]]]:
    if not m.task_delays:
        return [], None

    rows: list[list[object]] = []
    all_jitters: list[int] = []

    for task_name in sorted(m.task_delays.keys()):
        delays = m.task_delays[task_name]
        jitters = [mx - mn for mx, mn in zip(delays.get("max", []), delays.get("min", []))]
        all_jitters.extend(jitters)

        if not jitters:
            rows.append([task_name, 0, "-", "-", "-"])
            continue

        rows.append([
            task_name,
            len(jitters),
            f"{statistics.mean(jitters):.1f}",
            max(jitters),
            min(jitters),
        ])

    if not all_jitters:
        return rows, None

    global_row = [
        "Global Jitter",
        len(all_jitters),
        f"{statistics.mean(all_jitters):.1f}",
        max(all_jitters),
        min(all_jitters),
    ]
    return rows, global_row


def format_metrics_block(m: TestMetrics, indent: int = 2) -> list[str]:
    """Counts + stats + jitter, all in a consistent tabular style."""
    lines: list[str] = []

    lines.append(" " * indent + "--- Metrics ---")
    lines.append("")
    lines.extend(
        render_table(
            headers=["Metric", "Count"],
            rows=_build_count_rows(m),
            aligns=["left", "right"],
            indent=indent + 2,
        )
    )
    lines.append("")

    lines.extend(
        render_table(
            headers=["Measure", "Samples", "Avg", "Std", "Min", "Max"],
            rows=_build_stats_rows(m),
            aligns=["left", "right", "right", "right", "right", "right"],
            indent=indent + 2,
        )
    )
    lines.append("")

    if m.task_delays:
        lines.append(" " * indent + "--- Jitter Analysis ---")
        lines.append("")
        task_rows, global_row = _build_jitter_tables(m)
        if task_rows:
            lines.extend(
                render_table(
                    headers=["Task", "Samples", "Avg Jitter", "Max Jitter", "Min Jitter"],
                    rows=task_rows,
                    aligns=["left", "right", "right", "right", "right"],
                    indent=indent + 2,
                )
            )
        if global_row:
            lines.extend(
                render_table(
                    headers=["", "Samples", "Avg", "Max", "Min"],
                    rows=[[global_row[0], global_row[1], global_row[2], global_row[3], global_row[4]]],
                    aligns=["left", "right", "right", "right", "right"],
                    indent=indent + 2,
                )
            )
        lines.append("")

    return lines


def print_metrics_console(m: TestMetrics) -> None:
    print("\n".join(format_metrics_block(m, indent=2)))


# ─────────────────────────────────────────────────────────────────────────────
# Report writers
# ─────────────────────────────────────────────────────────────────────────────

def save_result(
        report_dir: str,
        session_ts: str,
        result: TestResult,
        info: Optional[Callable[[str], None]] = None,
) -> str:
    """Append this test's result to the session report file. Returns report_path."""
    os.makedirs(report_dir, exist_ok=True)
    report_path = os.path.join(report_dir, f"{session_ts}_suite_report.txt")

    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    lines: list[str] = []
    lines.append("=" * 66)
    lines.append(f"  TEST : {result.meta.name}")
    lines.append(f"  ID   : {result.meta.id}")
    lines.append(f"  TIME : {ts}")
    lines.append(f"  DESC : {result.meta.description}")
    lines.append("=" * 66)
    lines.append("")

    lines.extend(format_metrics_block(result.metrics, indent=2))

    lines.append("  " + _u_line(44))
    lines.append("  Checks:")
    for c in result.checks:
        sym = "PASS" if c.passed else "FAIL"
        lines.append(f"    [{sym}]  {c.label}  ({c.detail})")

    lines.append("")
    lines.append(f"  Overall:  {'PASSED' if result.overall else 'FAILED'}")
    lines.append("")

    with open(report_path, "a", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    if info:
        info(f"Result saved  ->  {report_path}")
    return report_path


def save_suite_summary(report_dir: str, session_ts: str, results: list[TestResult]) -> str:
    os.makedirs(report_dir, exist_ok=True)
    report_path = os.path.join(report_dir, f"{session_ts}_suite_report.txt")

    passed = sum(1 for r in results if r.overall)
    total = len(results)

    lines: list[str] = []
    lines.append("=" * 66)
    lines.append("  SUITE SUMMARY")
    lines.append("=" * 66)

    if not results:
        lines.append("")
        lines.append("  (no results)")
        lines.append("")
        with open(report_path, "a", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        return report_path

    rows: list[list[object]] = []
    for r in results:
        sym = "✓" if r.overall else "✗"
        verdict = "PASSED" if r.overall else "FAILED"
        notes = ""
        if not r.overall:
            notes = " | ".join(f"{c.label} ({c.detail})" for c in r.checks if not c.passed)
        rows.append([sym, r.meta.name, verdict, notes])

    lines.append("")
    lines.extend(
        render_table(
            headers=["", "Test", "Result", "Notes"],
            rows=rows,
            aligns=["left", "left", "left", "left"],
            indent=2,
        )
    )
    lines.append("")
    lines.append(f"  Total: {total}  |  Passed: {passed}  |  Failed: {total - passed}")
    lines.append("")

    with open(report_path, "a", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return report_path