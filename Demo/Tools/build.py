from __future__ import annotations

import os
import subprocess
import time
from pathlib import Path
from typing import Optional

from . import ui


def _host_to_container_path(project_root: str, host_path: str, container_root: str = "/app") -> str:
    """
    Convert an absolute host path inside the repo to the container-mounted path.
    Assumes the repo is mounted at container_root (commonly /app).
    If host_path is not under project_root, returns host_path unchanged.
    """
    try:
        pr = Path(project_root).resolve()
        hp = Path(host_path).resolve()
        rel = hp.relative_to(pr)
        return str(Path(container_root) / rel)
    except Exception:
        return host_path


class BuildSystem:
    def __init__(self, project_root: str, demo_dir: str, log_file: str, native_linux: bool):
        self.project_root = project_root
        self.demo_dir = demo_dir
        self.log_file = log_file
        self.native_linux = native_linux

    def run_make(self, target: str, timeout: int = 120, make_vars: Optional[dict[str, str]] = None) -> bool:
        target_map = {
            "build": "docker_build" if not self.native_linux else "all",
            "qemu":  "docker_qemu"  if not self.native_linux else "qemu_start",
            "clean": "docker_clean" if not self.native_linux else "clean",
        }
        actual_target = target_map.get(target, target)

        cmd = ["make", "-C", self.demo_dir]
        if make_vars:
            for k, v in make_vars.items():
                cmd.append(f"{k}={v}")
        cmd.append(actual_target)

        try:
            result = subprocess.run(cmd, timeout=timeout)
            return result.returncode == 0
        except subprocess.TimeoutExpired:
            print(f"  [ERROR] make {actual_target} timed out")
            return False
        except Exception as e:
            print(f"  [ERROR] make {actual_target}: {e}")
            return False

    def build(self, task_config_path: Optional[str] = None) -> bool:
        ui.section("Build")
        make_vars = {}
        if task_config_path:
            # If we're in Docker mode, pass container path for TASK_CONFIG.
            if not self.native_linux:
                make_vars["TASK_CONFIG"] = _host_to_container_path(self.project_root, task_config_path)
            else:
                make_vars["TASK_CONFIG"] = task_config_path

        if not self.run_make("build", make_vars=make_vars):
            ui.fail("Build failed")
            return False
        ui.ok("Build successful")
        return True

    def clean(self) -> bool:
        ui.section("Clean")
        ok = self.run_make("clean")
        ui.ok("Clean done") if ok else ui.fail("Clean failed")
        return ok

    def run_qemu(self) -> bool:
        ui.section("Running QEMU")

        if os.path.exists(self.log_file):
            os.remove(self.log_file)

        cmd = ["make", "-C", self.demo_dir]
        if self.native_linux:
            cmd += [f"LOG_DIR={os.path.join(self.project_root, 'logs')}", "qemu_start"]
        else:
            cmd += ["docker_qemu"]

        try:
            subprocess.run(cmd, timeout=90)
            # QEMU often exits non-zero (SIGINT / signal 2) — ignore returncode,
            # validate success via log content instead.
        except subprocess.TimeoutExpired:
            ui.info("Python-side timeout reached — checking log anyway")
        except KeyboardInterrupt:
            ui.info("Interrupted by user (Ctrl+C) — proceeding to log check / analysis")
            time.sleep(0.2)
        except Exception as e:
            ui.fail(f"Unexpected error launching QEMU: {e}")
            return False

        if not os.path.exists(self.log_file):
            ui.fail(f"UART log not produced at {self.log_file}")
            return False

        with open(self.log_file, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()

        if "START SIMULATION" not in content:
            ui.fail("'START SIMULATION' not found in log — scheduler did not start")
            return False

        lines = content.splitlines()
        ui.ok(f"Log captured  ({len(lines)} lines)  →  {self.log_file}")
        return True
