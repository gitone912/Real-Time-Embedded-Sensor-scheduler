from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional


@dataclass
class TestMeta:
    name: str
    id: str
    description: str
    expect_deadline_misses: int = 0
    expect_hrt_activations_per_frame: Optional[int] = None
    expect_srt_preemptions_min: int = 0
    expect_srt_starts: Optional[int] = None  # None = don't check


@dataclass
class TestMetrics:

    major_frame_ms = 0
    minor_frame_ms = 0
    hrt_starts: int = 0
    hrt_completions: int = 0
    srt_starts: int = 0
    srt_completions: int = 0
    srt_preempts: int = 0
    srt_killed: int = 0
    deadline_misses: int = 0
    major_frames: int = 0
    idle_ticks: list[int] = field(default_factory=list)
    overhead_cycles: list[int] = field(default_factory=list)
    overhead_percentages: list[float] = field(default_factory=list)
    delay_max_cycles: list[int] = field(default_factory=list)
    delay_min_cycles: list[int] = field(default_factory=list)
    # Per-task delay tracking for jitter analysis:
    # {task_name: {"max": [..], "min": [..]}}
    task_delays: dict[str, dict[str, list[int]]] = field(default_factory=dict)


@dataclass
class CheckResult:
    label: str
    passed: bool
    detail: str


@dataclass
class TestResult:
    meta: TestMeta
    metrics: TestMetrics
    checks: list[CheckResult]
    overall: bool
