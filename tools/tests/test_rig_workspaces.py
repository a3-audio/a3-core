"""The rig's workspaces: which program lives where, and where i3bar shows.

Decided 2026-09-30: 1 A3 Motion, 2 StemDeck, 3 REAPER (the engine window since 2026-10-26, renamed ENGINE), 4 QjackCtl, 5 the
Scarlett mixer; 7 the virtual desk (A3 Desk, since 2026-10-25). The two touch UIs fill their 768x1024 screen and switch
between each other themselves; from workspace 3 on, i3bar at the top gives
the way back -- the a3-core program's i3-bar role shows it there and hides it on 1 and 2.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa"
I3_CONFIG = PACKAGE / ".local/share/a3-core/config/i3/config"


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


class WhereThingsLive(unittest.TestCase):
    def setUp(self):
        self.config = I3_CONFIG.read_text()

    def test_each_program_has_its_workspace(self):
        expected = {
            '[class="A3 Motion UI"]': "1:MOTION",
            '[class="StemDeck"]': "2:STEMDECK",
            '[class="a3-engine"]': "3:ENGINE",
            '[class="REAPER"]': "3:ENGINE",
            '[class="QjackCtl"]': "4:QJACKCTL",
            '[title="Scarlett 18i20 USB"]': "5:SCARLETT",
            '[class="A3 Desk"]': "7:DESK",
            '[class="StemDeck" title="^StemDeck B$"]': "6:STEMDECK B",
        }
        for criteria, name in expected.items():
            self.assertEqual(workspace_of(self.config, criteria), name, criteria)

    def test_the_one_deck_windows_are_main_windows_not_dialogs(self):
        # 2026-10-26: "StemDeck A" / "StemDeck B" (one deck per instance) floated
        # as dialogs, and B covered A on workspace 2.
        border = re.search(r'for_window \[class="StemDeck" title="([^"]+)"\] [^\n]*border none', self.config)
        floating = re.search(r'for_window \[class="StemDeck" title="([^"]+)"\] floating enable', self.config)
        for title in ("StemDeck", "StemDeck A", "StemDeck B"):
            self.assertTrue(re.search(border.group(1), title), title)
            self.assertFalse(re.search(floating.group(1), title), title)
        # JUCE maps the window before it sets the deck's title, so the floating
        # rule catches it first; the main titles must take it back out.
        unfloat = re.search(r'for_window \[class="StemDeck" title="([^"]+)"\] floating disable[^\n]*', self.config)
        for title in ("StemDeck A", "StemDeck B"):
            self.assertTrue(re.search(unfloat.group(1), title), title)
        for dialog in ("Settings", "Audio", "Create stems"):
            self.assertTrue(re.search(floating.group(1), dialog), dialog)

    def test_the_bar_starts_hidden_at_the_top(self):
        bar = re.search(r"^bar \{(.*?)^\}", self.config, re.M | re.S)
        self.assertIsNotNone(bar, "no bar block")
        self.assertRegex(bar.group(1), r"(?m)^\s*position top$")
        self.assertRegex(bar.group(1), r"(?m)^\s*mode invisible$")
        self.assertRegex(bar.group(1), r"(?m)^\s*id a3$")
