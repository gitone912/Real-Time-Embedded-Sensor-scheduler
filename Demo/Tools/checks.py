from __future__ import annotations

import os
from typing import Optional

from .models import TestMeta, TestMetrics, CheckResult


def run_checks(meta: TestMeta, metrics: TestMetrics, log_file: str) -> list[CheckResult]:
    checks: list[CheckResult] = []

    # 1. Deadline misses
    if meta.expect_deadline_misses == 0:
        ok = metrics.deadline_misses == 0
        checks.append(CheckResult(
            label="No deadline misses",
            passed=ok,
            detail=f"found {metrics.deadline_misses}"
        ))
    else:
        ok = metrics.deadline_misses >= 1
        checks.append(CheckResult(
            label="Deadline miss detected (expected ≥1)",
            passed=ok,
            detail=f"found {metrics.deadline_misses}"
        ))

    # 2. HRT activations per frame
    if meta.expect_hrt_activations_per_frame is not None and metrics.major_frames > 0:
        per_frame = metrics.hrt_starts / metrics.major_frames
        ok = abs(per_frame - meta.expect_hrt_activations_per_frame) <= 1
        checks.append(CheckResult(
            label=f"HRT activations/frame ≈ {meta.expect_hrt_activations_per_frame}",
            passed=ok,
            detail=f"{per_frame:.1f}/frame  ({metrics.major_frames} frames observed)"
        ))

    # 3. SRT starts exact count
    if meta.expect_srt_starts is not None:
        ok = metrics.srt_starts == meta.expect_srt_starts
        checks.append(CheckResult(
            label=f"SRT starts == {meta.expect_srt_starts}",
            passed=ok,
            detail=f"found {metrics.srt_starts}"
        ))

    # 4. SRT preemptions minimum
    if meta.expect_srt_preemptions_min > 0:
        ok = metrics.srt_preempts >= meta.expect_srt_preemptions_min
        checks.append(CheckResult(
            label=f"SRT preemptions ≥ {meta.expect_srt_preemptions_min}",
            passed=ok,
            detail=f"found {metrics.srt_preempts}"
        ))

    # 5. Simulation started (sanity)
    log_content = ""
    if os.path.exists(log_file):
        with open(log_file, "r", encoding="utf-8", errors="ignore") as f:
            log_content = f.read()
    ok = "START SIMULATION" in log_content
    checks.append(CheckResult(
        label="Simulation started",
        passed=ok,
        detail="START SIMULATION present" if ok else "START SIMULATION missing"
    ))

    return checks
