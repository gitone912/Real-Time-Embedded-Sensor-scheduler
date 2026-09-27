"""
Visualization module for Timeline Scheduler analysis.
Generates Gantt-style charts showing task execution over time.
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from typing import Optional

import colorsys
from matplotlib.colors import to_rgb

import matplotlib.pyplot as plt


from matplotlib.ticker import MultipleLocator
import numpy as np


@dataclass
class TaskEvent:
    """Represents a task execution event."""
    task_name: str
    task_type: str  # "HRT" or "SRT"
    start_tick: int
    end_tick: Optional[int] = None  # None if still running at end of log
    subframe: int = 0
    deadline: Optional[int] = None  # Deadline for HRT tasks
    was_preempted: bool = False  # True if SRT was preempted
    deadline_missed: bool = False  # True if HRT missed its deadline


def parse_timeline_events(log_file: str, max_frames: int = 1) -> list[TaskEvent]:
    """
    Parse log file and extract task execution events.
    Returns list of TaskEvent objects for the first `max_frames` major frames.
    """
    if not os.path.exists(log_file):
        return []

    events: list[TaskEvent] = []
    active_tasks: dict[str, TaskEvent] = {}
    frame_count = 0
    frame_start_tick = 0

    # Regex patterns
    hrt_start_re = re.compile(
        r"\[\s*(\d+)\s+\d+\s*\]\s*HRT START:\s+(\w+)\s+\(\s*SubFrame:\s*(\d+)\s*\)\s+Deadline @(\d+)")
    hrt_complete_re = re.compile(r"\[\s*(\d+)\s+\d+\s*\]\s*HRT COMPLETE:\s+(\w+)")
    srt_start_re = re.compile(r"\[\s*(\d+)\s+\d+\s*\]\s*SRT START:\s+(\w+)")
    srt_complete_re = re.compile(r"\[\s*(\d+)\s+\d+\s*\]\s*SRT COMPLETE:\s+(\w+)")
    srt_preempt_re = re.compile(r"\[\s*(\d+)\s+\d+\s*\]\s*SRT PREEMPT:\s+(\w+)")
    srt_resume_re = re.compile(r"\[\s*(\d+)\s+\d+\s*\]\s*SRT RESUME:\s+(\w+)")
    srt_killed_re = re.compile(r"\[\s*(\d+)\s+\d+\s*\]\s*SRT KILLED:\s+(\w+)")
    major_frame_re = re.compile(r"\[\s*(\d+)\s+\d+\s*\]\s*MAJOR FRAME")
    deadline_miss_re = re.compile(r"\[\s*(\d+)\s+\d+\s*\]\s*DEADLINE MISS!\s+(\w+)")

    with open(log_file, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            # Check for major frame boundary
            mf_match = major_frame_re.search(line)
            if mf_match:
                frame_count += 1
                if frame_count > max_frames:
                    # Close only active SRT tasks at major frame boundary.
                    # IMPORTANT: Do NOT force-close HRT tasks here; if an HRT doesn't
                    # produce an 'HRT COMPLETE' line, we treat it as incomplete and
                    # skip plotting it (end_tick=None).
                    for task_name in list(active_tasks.keys()):
                        event = active_tasks[task_name]
                        if event.task_type == "SRT":
                            event.end_tick = frame_start_tick + 100  # Major frame end
                            events.append(event)
                            active_tasks.pop(task_name)
                    break
                frame_start_tick = int(mf_match.group(1))
                continue

            # HRT START
            match = hrt_start_re.search(line)
            if match:
                tick = int(match.group(1))
                task_name = match.group(2)
                subframe = int(match.group(3))
                deadline = int(match.group(4))
                event = TaskEvent(
                    task_name=task_name,
                    task_type="HRT",
                    start_tick=tick,
                    subframe=subframe,
                    deadline=deadline
                )
                active_tasks[task_name] = event
                continue

            # HRT COMPLETE
            match = hrt_complete_re.search(line)
            if match:
                tick = int(match.group(1))
                task_name = match.group(2)
                if task_name in active_tasks:
                    active_tasks[task_name].end_tick = tick
                    events.append(active_tasks.pop(task_name))
                continue

            # DEADLINE MISS (HRT)
            match = deadline_miss_re.search(line)
            if match:
                tick = int(match.group(1))
                task_name = match.group(2)
                if task_name in active_tasks:
                    ev = active_tasks[task_name]
                    ev.deadline_missed = True
                    if ev.end_tick is None:
                        ev.end_tick = tick
                    events.append(active_tasks.pop(task_name))
                else:
                    events.append(TaskEvent(task_name=task_name, task_type="HRT",
                                            start_tick=tick, end_tick=tick,
                                            deadline=tick, deadline_missed=True))
                continue

            # SRT START
            match = srt_start_re.search(line)
            if match:
                tick = int(match.group(1))
                task_name = match.group(2)
                event = TaskEvent(task_name=task_name, task_type="SRT", start_tick=tick)
                active_tasks[task_name] = event
                continue

            # SRT COMPLETE
            match = srt_complete_re.search(line)
            if match:
                tick = int(match.group(1))
                task_name = match.group(2)
                if task_name in active_tasks:
                    active_tasks[task_name].end_tick = tick
                    events.append(active_tasks.pop(task_name))
                continue

            # SRT PREEMPT
            match = srt_preempt_re.search(line)
            if match:
                tick = int(match.group(1))
                task_name = match.group(2)
                if task_name in active_tasks:
                    active_tasks[task_name].end_tick = tick
                    active_tasks[task_name].was_preempted = True
                    events.append(active_tasks.pop(task_name))
                continue

            # SRT RESUME
            match = srt_resume_re.search(line)
            if match:
                tick = int(match.group(1))
                task_name = match.group(2)
                event = TaskEvent(task_name=task_name, task_type="SRT", start_tick=tick)
                active_tasks[task_name] = event
                continue

            # SRT KILLED
            match = srt_killed_re.search(line)
            if match:
                tick = int(match.group(1))
                task_name = match.group(2)
                if task_name in active_tasks:
                    active_tasks[task_name].end_tick = tick
                    events.append(active_tasks.pop(task_name))
                continue

    return events


def generate_timeline_chart(
        log_file: str,
        output_path: str,
        major_frame_ms: int = 100,
        title: str = "Timeline Scheduler Analysis -- Gantt Chart",
        max_frames: int = 1,
        dpi: int = 150
) -> bool:
    """
    Generate a Gantt-style timeline chart from log file.
    """
    events = parse_timeline_events(log_file, max_frames=max_frames)

    if not events:
        print(f"  [WARNING] No task events found in log for visualization")
        return False

    hrt_events = [e for e in events if e.task_type == "HRT"]
    srt_events = [e for e in events if e.task_type == "SRT"]
    '''
    #uncomment if you want different color for each task
    all_task_names = sorted(set(e.task_name for e in events))
    color_map = _generate_color_map(all_task_names)
    '''

    #gradient color map by type (SRT vs HRT)
    color_map = _generate_color_map_by_type(events)

    fig, axes = plt.subplots(3, 1, figsize=(14, 6), sharex=True)
    fig.suptitle(title, fontsize=14, fontweight='bold')

    _plot_task_row(axes[0], srt_events, color_map, "SRT Tasks", major_frame_ms)
    _plot_task_row(axes[1], hrt_events, color_map, "HRT Tasks", major_frame_ms)
    _plot_idle_row(axes[2], events, major_frame_ms, "Idle / Kernel")

    for ax in axes:
        ax.set_xlim(0, major_frame_ms * max_frames)
        ax.grid(True, axis='x', alpha=0.3, linestyle='--')
        ax.xaxis.set_major_locator(MultipleLocator(10))
        for frame_idx in range(1, max_frames + 1):
            ax.axvline(x=frame_idx * major_frame_ms, color='red', linestyle='-',
                       linewidth=2.5, alpha=0.9, zorder=100)

    axes[-1].set_xlabel("Time (ms)", fontsize=11)

    # Legend
    '''
    legend_handles = [
        mpatches.Patch(color=color_map[name], label=name)
        for name in all_task_names
    ]
    legend_handles.append(Line2D([0], [0], color='darkred', lw=1.5, ls='--', label='HRT Deadline'))
    legend_handles.append(Line2D([0], [0], color='red', lw=2.5, ls='-', label='Major Frame End'))
    legend_handles.append(Line2D([0], [0], marker='x', color='black', lw=0, markersize=8,
                                 markeredgewidth=1.5, label='SRT Preemption'))
    legend_handles.append(Line2D([0], [0], marker='x', color='darkred', lw=0, markersize=10,
                                 markeredgewidth=2.5, label='HRT Deadline Miss'))

    fig.legend(
        handles=legend_handles,
        loc='upper center',
        bbox_to_anchor=(0.5, -0.04),
        ncol=min(6, len(legend_handles)),
        frameon=True,
        fancybox=True,
        shadow=True,
        fontsize=9
    )
    '''

    plt.tight_layout()
    plt.savefig(output_path, dpi=dpi, bbox_inches='tight')
    plt.close()

    return True


def _sanitize_filename(filename: str) -> str:
    """Remove or replace invalid filename characters."""
    invalid_chars = ['/', '\\', ':', '*', '?', '"', '<', '>', '|', ' ']
    sanitized = filename
    for char in invalid_chars:
        sanitized = sanitized.replace(char, '_')
    return sanitized


def parse_overhead_metrics(log_file: str) -> dict[str, float]:
    """
    Extract kernel overhead percentage from log file.
    Returns dict with 'kernel_overhead_percent', 'task_work_percent'.
    """
    if not os.path.exists(log_file):
        return {"kernel_overhead_percent": 0.0, "task_work_percent": 100.0}

    overhead_re = re.compile(r"KERNEL OVERHEAD:\s*\d+\s*Cycles\s*\((\d+\.?\d*)%\)")
    overheads = []

    with open(log_file, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            match = overhead_re.search(line)
            if match:
                try:
                    overhead_pct = float(match.group(1))
                    overheads.append(overhead_pct)
                except ValueError:
                    pass

    if not overheads:
        return {"kernel_overhead_percent": 0.0, "task_work_percent": 100.0}

    avg_overhead = sum(overheads) / len(overheads)
    return {
        "kernel_overhead_percent": avg_overhead,
        "task_work_percent": 100.0 - avg_overhead
    }

'''
It generates a color map for tasks, assigning distinct colors to each task name.'''
def _generate_color_map(task_names: list[str]) -> dict[str, str]:
    """Generate distinct colors for each task."""
    base_colors = [
        '#e74c3c', '#3498db', '#2ecc71', '#f39c12', '#9b59b6',
        '#1abc9c', '#e67e22', '#34495e', '#16a085', '#c0392b',
        '#8e44ad', '#2980b9', '#27ae60', '#f1c40f', '#d35400'
    ]
    return {name: base_colors[i % len(base_colors)] for i, name in enumerate(task_names)}

def _shade_hex(base_hex: str, t: float) -> str:
    """
    Return a shade of base_hex by varying lightness in HLS.
    t in [0,1] -> darker .. lighter
    """
    r, g, b = to_rgb(base_hex)
    h, l, s = colorsys.rgb_to_hls(r, g, b)

    l_min, l_max = 0.30, 0.78
    l2 = l_min + (l_max - l_min) * t

    r2, g2, b2 = colorsys.hls_to_rgb(h, l2, s)
    return "#{:02x}{:02x}{:02x}".format(int(r2 * 255), int(g2 * 255), int(b2 * 255))

def _generate_color_map_by_type(events: list[TaskEvent]) -> dict[str, str]:
    """
    Assign colors by type:
      - SRT: shades of blue
      - HRT: shades of orange
    Each task gets a distinct shade within its group.
    """
    # Pick your two bases (feel free to tweak)
    base_srt = "#1f77b4"  # blue
    base_hrt = "#ff7f0e"  # orange

    # Determine each task type (assumes a task name is consistently SRT or HRT)
    task_type: dict[str, str] = {}
    for e in events:
        task_type.setdefault(e.task_name, e.task_type)

    srt_names = sorted([n for n, t in task_type.items() if t == "SRT"])
    hrt_names = sorted([n for n, t in task_type.items() if t == "HRT"])

    color_map: dict[str, str] = {}

    def assign_shades(names: list[str], base_hex: str) -> None:
        n = len(names)
        if n == 0:
            return
        if n == 1:
            color_map[names[0]] = _shade_hex(base_hex, 0.55)
            return

        # Spread shades across a range (avoid extremes)
        ts = np.linspace(0.20, 0.95, n)
        for name, t in zip(names, ts):
            color_map[name] = _shade_hex(base_hex, float(t))

    assign_shades(srt_names, base_srt)
    assign_shades(hrt_names, base_hrt)

    return color_map
def _plot_task_row(ax, events: list[TaskEvent], color_map: dict[str, str],
                   row_label: str, major_frame_ms: int) -> None:
    """Plot a single row of task executions."""
    ax.set_ylabel(row_label, fontsize=10, fontweight='bold')
    ax.set_ylim(-0.5, 0.5)
    ax.set_yticks([])

    for event in events:
        if event.end_tick is None:
            continue

        start_ms = event.start_tick
        end_ms = event.end_tick
        duration_ms = end_ms - start_ms

        # HRT deadline vertical dashed line
        if event.task_type == "HRT" and event.deadline is not None:
            ax.axvline(x=event.deadline, ymin=0.1, ymax=0.9,
                       color='darkred', linestyle='--', linewidth=1.5, alpha=0.8, zorder=3)

        # Main execution bar
        ax.barh(y=0, width=duration_ms, left=start_ms, height=0.8,
                color=color_map[event.task_name], edgecolor='black',
                linewidth=0.5, alpha=0.85, zorder=2)

        # Deadline miss
        miss_flag = (
                event.task_type == "HRT"
                and event.deadline is not None
                and (getattr(event, "deadline_missed", False) or end_ms > event.deadline)
        )
        if miss_flag and event.deadline is not None:
            ax.plot(event.deadline, 0.4, marker='x', markersize=10,
                    color='darkred', markeredgewidth=2.5, zorder=6)
            if end_ms > event.deadline:
                ax.barh(y=0, width=max(0, end_ms - event.deadline), left=event.deadline,
                        height=0.8, color='red', edgecolor='black',
                        linewidth=0.5, alpha=0.35, zorder=4)

        # SRT preemption marker
        if event.task_type == "SRT" and event.was_preempted:
            ax.plot(end_ms, 0.4, marker='x', markersize=8,
                    color='black', markeredgewidth=1.5, zorder=6)


def _plot_idle_row(ax, all_events: list[TaskEvent], major_frame_ms: int,
                   row_label: str) -> None:
    """Plot idle time."""
    ax.set_ylabel(row_label, fontsize=10, fontweight='bold')
    ax.set_ylim(-0.5, 0.5)
    ax.set_yticks([])

    max_tick_events = max((e.end_tick or e.start_tick or 0) for e in all_events) if all_events else 0
    max_tick = max(major_frame_ms - 1, max_tick_events)
    timeline = np.zeros(max_tick + 1, dtype=bool)
    for event in all_events:
        if event.end_tick is None:
            continue
        timeline[event.start_tick:event.end_tick + 1] = True

    idle_regions = []
    in_idle = False
    idle_start = 0

    for tick in range(len(timeline)):
        if not timeline[tick] and not in_idle:
            idle_start = tick
            in_idle = True
        elif timeline[tick] and in_idle:
            idle_regions.append((idle_start, tick))
            in_idle = False
    if in_idle:
        idle_regions.append((idle_start, len(timeline) - 1))

    for start, end in idle_regions:
        duration = end - start
        if duration > 0:
            ax.barh(y=0, width=duration, left=start, height=0.8,
                    color='#95a5a6', edgecolor='black', linewidth=0.5, alpha=0.6)


def generate_task_distribution_bar(
        log_file: str,
        output_path: str,
        title: str = "Task Distribution and Overhead",
        max_frames: int = 1,
        dpi: int = 150
) -> bool:
    """
    Generate a combined chart with:
      - Left: horizontal bar chart of task execution time (replaces pie chart).
               Each bar shows absolute time; percentage annotated inside/beside.
               Legend placed externally like the Gantt chart (no cluttered labels).
      - Right: bar chart of kernel overhead vs task work.

    Handles many tasks gracefully by dynamically sizing the figure height.
    """
    events = parse_timeline_events(log_file, max_frames=max_frames)

    if not events:
        print(f"  [WARNING] No task events found in log for visualization")
        return False

    # Accumulate execution time per task
    task_times: dict[str, int] = {}
    for event in events:
        if event.end_tick is None:
            continue
        duration = event.end_tick - event.start_tick
        task_times[event.task_name] = task_times.get(event.task_name, 0) + duration

    if not task_times:
        return False

    sorted_tasks = sorted(task_times.items(), key=lambda x: x[1], reverse=True)
    task_names = [name for name, _ in sorted_tasks]
    task_durations = [dur for _, dur in sorted_tasks]
    total_time = sum(task_durations)
    task_pcts = [d / total_time * 100 for d in task_durations]


    color_map = _generate_color_map_by_type(events)  # Override with type-based colors
    colors = [color_map[name] for name in task_names]

    overhead_data = parse_overhead_metrics(log_file)
    kernel_overhead = overhead_data["kernel_overhead_percent"]
    task_work = overhead_data["task_work_percent"]

    # Dynamic figure height: at least 5, grows with number of tasks
    n_tasks = len(task_names)
    bar_height_per_task = 0.5          # inches per task row
    left_height = max(5.0, n_tasks * bar_height_per_task + 2.0)
    fig_height = max(left_height, 5.0)

    fig = plt.figure(figsize=(16, fig_height))
    fig.suptitle(title, fontsize=14, fontweight='bold', y=1.01)

    # Two columns: barplot (wider) | overhead chart
    gs = fig.add_gridspec(1, 2, width_ratios=[2, 1], wspace=0.35)
    ax1 = fig.add_subplot(gs[0])
    ax2 = fig.add_subplot(gs[1])

    # ── Left: horizontal bar chart ──────────────────────────────────────────
    y_positions = np.arange(n_tasks)
    bars = ax1.barh(
        y_positions,
        task_durations,
        color=colors,
        edgecolor='black',
        linewidth=0.8,
        height=0.65,
        alpha=0.88
    )

    # Annotate each bar with percentage (right-aligned inside or just outside)
    x_max = max(task_durations) if task_durations else 1
    for i, (bar, pct, dur) in enumerate(zip(bars, task_pcts, task_durations)):
        label_x = bar.get_width() + x_max * 0.01
        ax1.text(
            label_x,
            bar.get_y() + bar.get_height() / 2,
            f"{pct:.1f}%  ({dur} ms)",
            va='center',
            ha='left',
            fontsize=8.5,
            color='#222222'
        )

    # Y-axis: task names replaced by colored legend patches (no cluttered ytick labels)
    ax1.set_yticks(y_positions)
    ax1.set_yticklabels(task_names, fontsize=9)
    ax1.invert_yaxis()                          # largest bar on top
    ax1.set_xlabel("Execution Time (ms)", fontsize=10, fontweight='bold')
    ax1.set_title("Task Execution Time Distribution", fontsize=11, fontweight='bold', pad=10)
    ax1.set_xlim(0, x_max * 1.30)              # extra room for annotations
    ax1.grid(True, axis='x', alpha=0.3, linestyle='--')
    ax1.spines['top'].set_visible(False)
    ax1.spines['right'].set_visible(False)

    # ── Right: overhead bar chart ────────────────────────────────────────────
    categories = ["Kernel\nOverhead", "Task\nWork"]
    values = [kernel_overhead, task_work]
    colors_bar = ['#e74c3c', '#2ecc71']

    bars2 = ax2.bar(categories, values, color=colors_bar,
                    edgecolor='black', linewidth=1.5, alpha=0.85, width=0.5)

    for bar, value in zip(bars2, values):
        ax2.text(
            bar.get_x() + bar.get_width() / 2. ,
            bar.get_height() / 2+6,
            f'{value:.2f}%',
            ha='center', va='center',
            fontsize=16, fontweight='bold', color='black'
        )

    ax2.set_ylabel('Percentage (%)', fontsize=10, fontweight='bold')
    ax2.set_title("CPU Time Distribution", fontsize=11, fontweight='bold', pad=10)
    ax2.set_ylim(0, 100)
    ax2.grid(True, axis='y', alpha=0.3, linestyle='--')
    ax2.spines['top'].set_visible(False)
    ax2.spines['right'].set_visible(False)

    # ── External legend  uncomment if necessary ───────────────────────────────
    '''
    legend_handles = [
        mpatches.Patch(color=color_map[name], label=name)
        for name in task_names
    ]
    fig.legend(
        handles=legend_handles,
        loc='upper center',
        bbox_to_anchor=(0.5, -0.03),
        ncol=min(6, n_tasks),
        frameon=True,
        fancybox=True,
        shadow=True,
        fontsize=9
    )
    '''
    plt.tight_layout()
    plt.savefig(output_path, dpi=dpi, bbox_inches='tight')
    plt.close()

    return True



def generate_task_distribution_and_overhead_chart(
        log_file: str,
        output_path: str,
        title: str = "Task Distribution and Overhead",
        max_frames: int = 1,
        dpi: int = 150
) -> bool:
    """Alias kept for backward compatibility — delegates to generate_task_distribution_bar."""
    return generate_task_distribution_bar(
        log_file=log_file,
        output_path=output_path,
        title=title,
        max_frames=max_frames,
        dpi=dpi
    )


def generate_all_charts(
        log_file: str,
        task_delays: dict[str, dict[str, list[int]]],
        output_dir: str,
        test_name: str = "test",
        major_frame_ms: int = 100
) -> list[str]:
    """
    Generate all visualization charts for a test.

    Args:
        log_file: Path to UART log
        task_delays: From TestMetrics.task_delays
        output_dir: Directory to save charts
        test_name: Base name for output files
        major_frame_ms: Major frame duration in ms

    Returns:
        List of paths to generated chart files
    """
    os.makedirs(output_dir, exist_ok=True)
    safe_test_name = _sanitize_filename(test_name)
    generated_files = []

    # 1. Gantt timeline chart
    timeline_path = os.path.join(output_dir, f"{safe_test_name}_timeline.png")
    if generate_timeline_chart(
            log_file=log_file,
            output_path=timeline_path,
            major_frame_ms=major_frame_ms,
            title=f"Timeline Analysis - {test_name}",
            max_frames=1
    ):
        generated_files.append(timeline_path)
        print(f"  [OK] Timeline chart saved → {timeline_path}")

    # 2. Task distribution barplot + overhead chart
    distribution_path = os.path.join(output_dir, f"{safe_test_name}_distribution_overhead.png")
    if generate_task_distribution_bar(
            log_file=log_file,
            output_path=distribution_path,
            title=f"Task Distribution & Overhead - {test_name}",
            max_frames=1
    ):
        generated_files.append(distribution_path)
        print(f"  [OK] Distribution & Overhead chart saved → {distribution_path}")


    return generated_files