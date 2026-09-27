from __future__ import annotations

import json
from pathlib import Path
from typing import Optional

from .models import TestMeta


def list_configs(suite_dir: str) -> list[Path]:
    suite = Path(suite_dir)
    if not suite.exists():
        return []
    return sorted(suite.glob("*.json"))


def find_config_by_id(suite_dir: str, config_id: str) -> Optional[Path]:
    # 1) Match _test_meta.id exactly
    for path in list_configs(suite_dir):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            if data.get("_test_meta", {}).get("id") == config_id:
                return path
        except Exception:
            continue
    # 2) Fallback: config_id contained in filename stem
    for path in list_configs(suite_dir):
        if config_id in path.stem:
            return path
    return None


def read_meta(config_path: Path) -> TestMeta:
    data = json.loads(config_path.read_text(encoding="utf-8"))
    raw = data.get("_test_meta", {})
    return TestMeta(
        name=raw.get("name", config_path.stem),
        id=raw.get("id", config_path.stem),
        description=raw.get("description", ""),
        expect_deadline_misses=raw.get("expect_deadline_misses", 0),
        expect_hrt_activations_per_frame=raw.get("expect_hrt_activations_per_frame"),
        expect_srt_preemptions_min=raw.get("expect_srt_preemptions_min", 0),
        expect_srt_starts=raw.get("expect_srt_starts"),
    )


def read_timing_config(config_path: Path) -> tuple[int, int]:
    """Read major_frame and subframe (minor_frame) from config.

    Returns: (major_frame_ms, minor_frame_ms)
    Defaults to (100, 10) if not found.
    """
    try:
        data = json.loads(config_path.read_text(encoding="utf-8"))
        major_frame = data.get("major_frame", 100)
        subframe = data.get("subframe", 10)
        return (major_frame, subframe)
    except Exception:
        return (100, 10)


def strip_meta_for_generator(config_path: Path) -> str:
    """Return JSON text without the _test_meta key (useful if needed)."""
    data = json.loads(config_path.read_text(encoding="utf-8"))
    data.pop("_test_meta", None)
    return json.dumps(data, indent=2)
