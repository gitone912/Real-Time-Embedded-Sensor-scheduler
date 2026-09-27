from __future__ import annotations

import statistics

from . import ui
from .models import TestMetrics


def print_metrics(metrics: TestMetrics) -> None:
    ui.section("Metrics")
    col = 26
    sep = "─" * 44
    print(f"\n    {'Metric':<{col}}  Count")
    print(f"    {sep}")
    rows = [
        ("HRT Task Starts",        metrics.hrt_starts),
        ("HRT Task Completions",   metrics.hrt_completions),
        ("Deadline Misses",        metrics.deadline_misses),
        ("SRT Task Starts",        metrics.srt_starts),
        ("SRT Task Completions",   metrics.srt_completions),
        ("SRT Preemptions",        metrics.srt_preempts),
        ("SRT Killed",             metrics.srt_killed),
        ("Major Frame Completions", metrics.major_frames),
    ]
    for label, val in rows:
        warn = "  ← !!!" if (label == "Deadline Misses" and val > 0) else ""
        print(f"    {label:<{col}}  {val}{warn}")
    print(f"    {sep}")

    if metrics.idle_ticks:
        avg = statistics.mean(metrics.idle_ticks)
        std = statistics.pstdev(metrics.idle_ticks)
        mn  = min(metrics.idle_ticks)
        mx  = max(metrics.idle_ticks)
        print(f"\n    Idle Ticks  samples={len(metrics.idle_ticks)}  avg={avg:.1f}  std={std:.1f}  min={mn}  max={mx}")
    else:
        print("\n    Idle Ticks: no samples found in log")

    if metrics.overhead_cycles:
        avg = statistics.mean(metrics.overhead_cycles)
        std = statistics.pstdev(metrics.overhead_cycles)
        mn  = min(metrics.overhead_cycles)
        mx  = max(metrics.overhead_cycles)
        print(f"\n    Kernel Overhead  samples={len(metrics.overhead_cycles)}  avg={avg:.1f}  std={std:.1f}  min={mn}  max={mx}")
    else:
        print("\n    Kernel Overhead: no samples found in log")

    if metrics.overhead_percentages:
        avg_pct = statistics.mean(metrics.overhead_percentages)
        std_pct = statistics.pstdev(metrics.overhead_percentages)
        mn_pct  = min(metrics.overhead_percentages)
        mx_pct  = max(metrics.overhead_percentages)
        print(f"    Kernel Overhead %  samples={len(metrics.overhead_percentages)}  avg={avg_pct:.2f}%  std={std_pct:.2f}%  min={mn_pct:.2f}%  max={mx_pct:.2f}%")

    if metrics.delay_max_cycles:
        avg = statistics.mean(metrics.delay_max_cycles)
        std = statistics.pstdev(metrics.delay_max_cycles)
        mn  = min(metrics.delay_max_cycles)
        mx  = max(metrics.delay_max_cycles)
        print(f"\n    Delay Max (cycles)  samples={len(metrics.delay_max_cycles)}  avg={avg:.1f}  std={std:.1f}  min={mn}  max={mx}")
    else:
        print("\n    Delay Max: no samples found in log")

    if metrics.delay_min_cycles:
        avg = statistics.mean(metrics.delay_min_cycles)
        std = statistics.pstdev(metrics.delay_min_cycles)
        mn  = min(metrics.delay_min_cycles)
        mx  = max(metrics.delay_min_cycles)
        print(f"    Delay Min (cycles)  samples={len(metrics.delay_min_cycles)}  avg={avg:.1f}  std={std:.1f}  min={mn}  max={mx}")
    else:
        print("    Delay Min: no samples found in log")

    if metrics.task_delays:
        _print_jitter_analysis(metrics)


def _print_jitter_analysis(metrics: TestMetrics) -> None:
    print(f"\n    {'─' * 44}")
    print("    JITTER ANALYSIS (delay_max - delay_min per activation)")
    print(f"    {'─' * 44}")

    all_jitters: list[int] = []
    task_jitter_stats: list[dict] = []

    for task_name in sorted(metrics.task_delays.keys()):
        delays = metrics.task_delays[task_name]
        max_delays = delays.get("max", [])
        min_delays = delays.get("min", [])

        jitters = [mx - mn for mx, mn in zip(max_delays, min_delays)]
        all_jitters.extend(jitters)

        if jitters:
            task_jitter_stats.append({
                "task": task_name,
                "avg": statistics.mean(jitters),
                "max": max(jitters),
                "min": min(jitters),
                "samples": len(jitters),
            })

    if task_jitter_stats:
        print(f"\n    {'Task':<8}  {'Samples':<8}  {'Avg Jitter':<12}  {'Max Jitter':<12}  {'Min Jitter':<12}")
        print(f"    {'-' * 64}")
        for stat in task_jitter_stats:
            print(f"    {stat['task']:<8}  {stat['samples']:<8}  {stat['avg']:>10.1f}  {stat['max']:>10}  {stat['min']:>10}")

    if all_jitters:
        print(f"\n    GLOBAL JITTER STATISTICS")
        print(f"    {'-' * 44}")

        print(f"    Maximum jitter    : {max(all_jitters):>10} cycles")
        print(f"    Minimum jitter    : {min(all_jitters):>10} cycles")
        print(f"    Total samples     : {len(all_jitters)}")

        max_task = max(task_jitter_stats, key=lambda x: x["max"]) if task_jitter_stats else None
        if max_task:
            print(f"    Task with max jitter : {max_task['task']} ({max_task['max']} cycles)")
    else:
        print("    No jitter data available")
