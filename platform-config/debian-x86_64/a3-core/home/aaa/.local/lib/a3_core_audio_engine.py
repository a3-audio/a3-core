# SPDX-FileCopyrightText: 2026 Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Engine or REAPER (engine plan 4, decision D22).

REAPER is frozen and there is no boot choice: the a3-engine runs when it is
installed and its configuration checks out, otherwise today's REAPER setup
runs and the reason is logged. engine_runs() is the rule, without processes;
probe() looks at the machine and hands it the two facts.
"""

import os
import subprocess
from pathlib import Path
from typing import Callable

ENGINE = "engine"
REAPER = "reaper"

ENGINE_PATH = Path("/usr/bin/a3-engine")
CHECK_TIMEOUT_SECONDS = 5.0

Check = Callable[[], "tuple[bool, str]"]


def engine_runs(engine_installed: bool, config_check: Check) -> "tuple[bool, str]":
    """(True, "") when the engine runs; else (False, why REAPER runs)."""
    if not engine_installed:
        return False, "a3-engine not installed"
    passed, reason = config_check()
    if not passed:
        return False, f"config invalid: {reason}"
    return True, ""


def probe(engine_path: Path = ENGINE_PATH,
          timeout: float = CHECK_TIMEOUT_SECONDS) -> "tuple[bool, Check]":
    """(installed, check) for the engine binary at engine_path."""
    installed = engine_path.is_file() and os.access(engine_path, os.X_OK)

    def check():
        return _check_config(engine_path, timeout)

    return installed, check


def _check_config(engine_path: Path, timeout: float) -> "tuple[bool, str]":
    try:
        done = subprocess.run([str(engine_path), "--check-config"],
                              capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, f"check timed out after {timeout:g} s"
    except OSError as error:
        return False, f"check could not run: {error}"
    if done.returncode == 0:
        return True, ""
    return False, _last_line(done.stdout + "\n" + done.stderr)


def _last_line(text: str) -> str:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    return lines[-1] if lines else "check failed without output"
