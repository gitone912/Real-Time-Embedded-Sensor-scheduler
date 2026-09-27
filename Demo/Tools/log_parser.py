from __future__ import annotations

import os
import re
from pathlib import Path

from .models import TestMetrics


_IDLE_RE = re.compile(r"CPU STATS:\s*Idle Ticks\s*=\s*(\d+)")
_OVERHEAD_RE = re.compile(r"KERNEL OVERHEAD:\s*(\d+)\s*Cycles\s*\((\d+\.?\d*)%\)")
_TIME_RE = re.compile(r"^\[\s*(\d+)\s+(\d+)\s*\]")
# Example: "HRT START: T1 ... delay max:123 cycles      delay min: 45 cycles"
_HRT_START_DELAY_RE = re.compile(
    r"HRT START:\s+(\w+).*?delay max:\s*(\d+)\s*cycles\s+delay min:\s*(\d+)\s*cycles"
)


def _remove_last_major_frame(lines: list[str]) -> list[str]:
    if not lines:
        return lines

    last_major_frame_idx = -1
    for i in range(len(lines) - 1, -1, -1):
        if "MAJOR FRAME" in lines[i]:
            last_major_frame_idx = i
            break

    if last_major_frame_idx < 0:
        return lines

    # keep it if the last frame looks complete
    tail = lines[last_major_frame_idx:]
    if any("KERNEL OVERHEAD" in l for l in tail):
        return lines

    return lines[:last_major_frame_idx]

def parse_log(log_file: str, major_frame_ms: int = 100, minor_frame_ms: int = 10) -> TestMetrics:
    m = TestMetrics()
    m.major_frame_ms = major_frame_ms
    m.minor_frame_ms = minor_frame_ms
    if not os.path.exists(log_file):
        return m

    with open(log_file, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    # Remove the last MAJOR FRAME before parsing if it appears to be interrupted (no KERNEL OVERHEAD after it).
    lines = _remove_last_major_frame(lines)
    print(f"  [OK] Parsing {log_file}, removed interrupted last major frame if present")
    explicit_major = 0
    wrap_major = 0
    prev_major_tick = None
    seen_any_tick = False

    for line in lines:
        # --- robust major-frame detection from tick wrap ---
        mt = _TIME_RE.search(line)
        if mt:
            major_tick = int(mt.group(1))  # the 00000..00099 part
            if not seen_any_tick:
                wrap_major = 1
                seen_any_tick = True
            elif prev_major_tick is not None and major_tick < prev_major_tick:
                wrap_major += 1
            prev_major_tick = major_tick

        # --- old explicit marker count (keep it as a sanity reference) ---
        if "MAJOR FRAME" in line:
            explicit_major += 1

        # ... il resto dei conteggi (HRT START, SRT, ecc) invariato ...

    # Use the most reliable estimate
    m.major_frames = max(explicit_major, wrap_major)
    for line in lines:
        if "HRT START"     in line: m.hrt_starts      += 1
        if "HRT COMPLETE"  in line: m.hrt_completions += 1
        if "SRT START"     in line: m.srt_starts      += 1
        if "SRT COMPLETE"  in line: m.srt_completions += 1
        if "SRT PREEMPT"   in line: m.srt_preempts    += 1
        if "SRT KILLED"    in line: m.srt_killed      += 1
        if "DEADLINE MISS" in line: m.deadline_misses += 1

        hit = _IDLE_RE.search(line)
        if hit:
            try:
                m.idle_ticks.append(int(hit.group(1)))
            except ValueError:
                pass

        hit_overhead = _OVERHEAD_RE.search(line)
        if hit_overhead:
            try:
                m.overhead_cycles.append(int(hit_overhead.group(1)))
                m.overhead_percentages.append(float(hit_overhead.group(2)))
            except ValueError:
                pass

        hit_delay = _HRT_START_DELAY_RE.search(line)
        if hit_delay:
            try:
                task_name = hit_delay.group(1)
                delay_max = int(hit_delay.group(2))
                delay_min = int(hit_delay.group(3))

                m.delay_max_cycles.append(delay_max)
                m.delay_min_cycles.append(delay_min)

                if task_name not in m.task_delays:
                    m.task_delays[task_name] = {"max": [], "min": []}
                m.task_delays[task_name]["max"].append(delay_max)
                m.task_delays[task_name]["min"].append(delay_min)
            except (ValueError, IndexError):
                pass

    return m
