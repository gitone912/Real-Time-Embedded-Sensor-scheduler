from __future__ import annotations

import sys


def banner(title: str) -> None:
    width = 66
    print(f"\n{'═' * width}")
    print(f"  {title}")
    print(f"{'═' * width}")


def header(title: str) -> None:
    width = 64
    bar = "─" * width
    print(f"\n┌{bar}┐")
    print(f"│  {title:<{width - 2}}│")
    print(f"└{bar}┘")


def section(title: str) -> None:
    print(f"\n--- {title} ---")


def ok(msg: str) -> None:
    print(f"\t[OK]{msg}")


def fail(msg: str) -> None:
    # Keep the user's original tag style
    print(f"\t[KO]{msg}")


def info(msg: str) -> None:
    print(f"\t[INFO]{msg}")


def pause(interactive: bool, prompt: str) -> None:
    """Pause and wait for Enter. Skipped if interactive=False."""
    if not interactive:
        return
    try:
        print(f"\n  ┌─ {prompt}")
        print("  └─ Press ENTER to continue  (Ctrl+C to abort) ...")
        input()
    except KeyboardInterrupt:
        print("\n\n  [ABORTED by user]")
        raise SystemExit(1)
