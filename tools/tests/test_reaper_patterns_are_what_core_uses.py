"""REAPER is told about exactly what Core uses, and nothing else.

The pattern file used to be REAPER's whole Default: some 240 actions, and on
every refresh REAPER reported each of them for every track -- 21,470
addresses, about a million messages, rate-limited to some 2,300 a second, so a
start took two passes and 23 seconds (measured on the rig, 2026-09-26). Core
routes a handful of them and counts the rest as unknown; its evening replay
waits for the whole pass to end (#56).

A pattern that is missing here is a control REAPER neither takes nor reports,
without an error anywhere -- so the other half of this file checks that every
address Core writes to REAPER still has its pattern.
"""

import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOCAL = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
PATTERNS = LOCAL / "share/a3-core/config/REAPER/OSC/a3-core.ReaperOSC"
LAYOUT = LOCAL / "share/a3-core/layout.json"
STARTUP = LOCAL / "lib/a3_core_startup.py"

WHAT_CORE_USES = {
    ("FX_PARAM_VALUE", "n/track/@/fx/@/fxparam/@/value"),
    ("FX_BYPASS", "b/track/@/fx/@/bypass"),
    ("TRACK_VOLUME", "n/track/@/volume"),
    ("TRACK_MUTE", "b/track/@/mute"),
    ("TRACK_SEND_VOLUME", "n/track/@/send/@/volume"),
    ("ACTION", "t/action/@"),
}


def actions():
    """(ACTION, pattern) for every pattern line; device settings left out."""
    found = set()
    for line in PATTERNS.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        name, *patterns = line.split()
        for pattern in patterns:
            if "/" in pattern:
                found.add((name, pattern))
    return found


def as_pattern(template):
    """layout.json's "/track/{track}/mute" as REAPER writes it: /track/@/mute."""
    return re.sub(r"\{[a-z_]+\}", "@", template)


class OnlyWhatCoreUses(unittest.TestCase):
    def test_the_file_lists_exactly_those_patterns(self):
        self.assertEqual(WHAT_CORE_USES, actions())

    def test_the_device_settings_are_still_there(self):
        text = PATTERNS.read_text()
        for setting in ("DEVICE_TRACK_COUNT", "DEVICE_FX_PARAM_COUNT",
                        "DEVICE_EQ INSERT", "DEVICE_ROTARY_CENTER"):
            self.assertRegex(text, rf"(?m)^{setting}\b")


class EverythingCoreSendsHasItsPattern(unittest.TestCase):
    def setUp(self):
        self.patterns = {pattern.split("/", 1)[1] for _name, pattern in actions()}

    def assertCovered(self, address):
        self.assertIn(address.lstrip("/"), self.patterns,
                      f"{address} has no pattern: REAPER would drop it")

    def test_every_reaper_address_in_the_layout(self):
        addresses = json.loads(LAYOUT.read_text())["addresses"]
        reaper = {name: template for name, template in addresses.items()
                  if template.startswith("/track/")}
        self.assertTrue(reaper, "layout.json names no REAPER address")
        for template in reaper.values():
            self.assertCovered(as_pattern(template))

    def test_the_filter_bypass_set_at_startup(self):
        self.assertIn('f"/track/{channel.track_input}/fx/{slot}/bypass"',
                      STARTUP.read_text())
        self.assertCovered("/track/@/fx/@/bypass")

    def test_the_actions_core_triggers(self):
        # REFRESH_ACTION (41743) and the project save (40026).
        self.assertCovered("/action/@")


if __name__ == "__main__":
    unittest.main()
