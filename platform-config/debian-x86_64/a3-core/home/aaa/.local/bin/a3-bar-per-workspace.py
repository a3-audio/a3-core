#!/usr/bin/env python3
"""Shows i3bar on the rig's tool workspaces and hides it on the two UIs.

Workspaces 1 (A3 Motion) and 2 (StemDeck) are touch UIs that fill their
768x1024 screen and switch between each other themselves; from 3 on
(REAPER, QjackCtl, the Scarlett mixer) i3bar at the top is the way back.
i3 has no bar per workspace, so this follows i3's workspace events and sets
the bar's mode (2026-09-30).
"""

import json
import re
import subprocess
import sys

BAR_ID = "a3"
FIRST_TOOL_WORKSPACE = 3


def bar_mode(workspace_name):
    """'dock' where the bar shows, 'invisible' where the screen is a UI's."""
    number = re.match(r"(\d+)", workspace_name or "")
    if number and int(number.group(1)) >= FIRST_TOOL_WORKSPACE:
        return "dock"
    return "invisible"


def set_bar(mode):
    subprocess.run(["i3-msg", "-q", f"bar mode {mode} {BAR_ID}"], check=False)


def focused_workspace():
    workspaces = json.loads(subprocess.run(["i3-msg", "-t", "get_workspaces"],
                                           capture_output=True, text=True).stdout or "[]")
    return next((w["name"] for w in workspaces if w.get("focused")), "")


def main():
    shown = bar_mode(focused_workspace())
    set_bar(shown)
    events = subprocess.Popen(["i3-msg", "-t", "subscribe", "-m", '["workspace"]'],
                              stdout=subprocess.PIPE, text=True)
    for line in events.stdout:
        try:
            event = json.loads(line)
        except ValueError:
            continue
        if event.get("change") != "focus":
            continue
        mode = bar_mode((event.get("current") or {}).get("name", ""))
        if mode != shown:
            set_bar(mode)
            shown = mode
    return events.wait()


if __name__ == "__main__":
    sys.exit(main())
