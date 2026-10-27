# SPDX-FileCopyrightText: 2026 Patric Schmitz, Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""The virtual desk in the one truth (spec virtual-desk, 2026-10-16).

Several desks, each the program "desk" on a machine of its own, hear Core on
desk.osc and the meters on desk.vu. Wherever the hardware desk ("mixer")
sends, hears or is told something, a desk does too: it sends exactly what the
hardware sends and shows what Core reports (D5).
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(ROOT / "tools/lib"))

import a3_osc  # noqa: E402

TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")
LISTS = ("from", "to", "relayed_to", "announced_to")


def _listener(name):
    return next((l for l in TRUTH.listeners() if l["name"] == name), {})


class TheDeskListens(unittest.TestCase):
    def test_on_desk_osc_for_core(self):
        found = _listener("desk.osc")
        self.assertEqual((found.get("program"), found.get("role"), found.get("host"), found.get("port")),
                         ("desk", "osc", "any", 7773))

    def test_on_desk_vu_for_the_meters(self):
        """A port of its own: 50 bundles a second must never queue in front
        of a lamp (spec Q4)."""
        found = _listener("desk.vu")
        self.assertEqual((found.get("program"), found.get("role"), found.get("host"), found.get("port")),
                         ("desk", "vu", "any", 7774))

    def test_is_a_program(self):
        self.assertIn("desk", TRUTH.programs())


class TheDeskTalks(unittest.TestCase):
    def setUp(self):
        self.routes = {(r["from"], r["to"], r.get("carries")) for r in TRUTH.routes()}

    def test_to_core_and_to_the_beat_clock(self):
        self.assertIn(("desk", "core.osc", None), self.routes)
        self.assertIn(("desk", "beat-analyzer.clock", None), self.routes)

    def test_core_answers_it_and_passes_the_meters_on(self):
        self.assertIn(("core", "desk.osc", None), self.routes)
        self.assertIn(("core", "desk.vu", "vu"), self.routes)

    def test_stemdeck_hands_its_meters_to_the_relay(self):
        """Without it a desk's stem meters and its return STEM stay dark."""
        self.assertIn(("stemdeck", "core.vu-relay", "vu"), self.routes)


class TheDeskMirrorsTheHardware(unittest.TestCase):
    def test_wherever_the_hardware_desk_is_named_a_desk_is_too(self):
        for key, entry in TRUTH.addresses().items():
            for field in LISTS:
                names = entry.get(field, [])
                if "mixer" in names:
                    self.assertIn("desk", names, f"{key}.{field}")

    def test_it_says_hello_and_asks_for_the_state(self):
        addresses = TRUTH.addresses()
        self.assertIn("desk", addresses["device.hello"]["from"])
        self.assertIn("desk", addresses["state.recall"]["from"])

    def test_it_hears_core_announce_itself(self):
        self.assertIn("desk", TRUTH.addresses()["core.here"]["to"])


if __name__ == "__main__":
    unittest.main()
