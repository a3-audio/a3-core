"""The rig's workspaces: which program lives where, and where i3bar shows.

Decided 2026-09-30: 1 A3 Motion, 2 StemDeck, 3 REAPER, 4 QjackCtl, 5 the
Scarlett mixer. The two touch UIs fill their 768x1024 screen and switch
between each other themselves; from workspace 3 on, i3bar at the top gives
the way back -- a3-bar-per-workspace shows it there and hides it on 1 and 2.
"""

import importlib.util
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa"
I3_CONFIG = PACKAGE / ".local/share/a3-core/config/i3/config"
BAR_SCRIPT = PACKAGE / ".local/bin/a3-bar-per-workspace.py"


def load_bar_script():
    spec = importlib.util.spec_from_file_location("a3_bar_per_workspace", BAR_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def workspace_variables(config):
    return dict(re.findall(r'^set \$(ws\d+) "([^"]+)"', config, re.M))


def workspace_of(config, criteria):
    """The workspace name a window matching `criteria` is sent to."""
    names = workspace_variables(config)
    for line in config.splitlines():
        if criteria in line and ("move container to workspace" in line or line.startswith("assign")):
            ref = re.search(r"number \$(ws\d+)", line)
            if ref:
                return names[ref.group(1)]
    return None


class BarPerWorkspace(unittest.TestCase):
    def setUp(self):
        self.bar = load_bar_script()

    def test_the_two_uis_have_the_whole_screen(self):
        for name in ("1:MOTION", "2:STEMDECK", "1", "2"):
            self.assertEqual(self.bar.bar_mode(name), "invisible", name)

    def test_from_workspace_three_on_the_bar_shows(self):
        for name in ("3:REAPER", "4:QJACKCTL", "5:SCARLETT", "7"):
            self.assertEqual(self.bar.bar_mode(name), "dock", name)

    def test_a_workspace_without_a_number_keeps_the_bar_away(self):
        self.assertEqual(self.bar.bar_mode("scratch"), "invisible")


class WhereThingsLive(unittest.TestCase):
    def setUp(self):
        self.config = I3_CONFIG.read_text()

    def test_each_program_has_its_workspace(self):
        expected = {
            '[class="A3 Motion UI"]': "1:MOTION",
            '[class="StemDeck"]': "2:STEMDECK",
            '[class="REAPER"]': "3:REAPER",
            '[class="QjackCtl"]': "4:QJACKCTL",
            '[title="Scarlett 18i20 USB"]': "5:SCARLETT",
        }
        for criteria, name in expected.items():
            self.assertEqual(workspace_of(self.config, criteria), name, criteria)

    def test_the_bar_starts_hidden_at_the_top(self):
        bar = re.search(r"^bar \{(.*?)^\}", self.config, re.M | re.S)
        self.assertIsNotNone(bar, "no bar block")
        self.assertRegex(bar.group(1), r"(?m)^\s*position top$")
        self.assertRegex(bar.group(1), r"(?m)^\s*mode invisible$")
        self.assertRegex(bar.group(1), r"(?m)^\s*id a3$")
