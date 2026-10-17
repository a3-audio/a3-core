# SPDX-FileCopyrightText: 2026 Patric Schmitz, Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""The pro-DJ app in the one truth (spec prodj-stage-1, 2026-10-17).

The app is a desk that listens on desk.osc/desk.vu; it is followed as a desk
of its own kind, gets the positions nobody else gets, switches the global
pro-DJ LIVE state and clicks on StemDeck's and Motion's screen pictures.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

import a3_osc  # noqa: E402

TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")


class TheProdjIsAProgram(unittest.TestCase):
    def test_is_a_program_that_listens_as_a_desk(self):
        self.assertIn("prodj", TRUTH.programs())
        self.assertEqual([l["name"] for l in TRUTH.listeners() if l["program"] == "prodj"], [])

    def test_talks_to_core(self):
        routes = {(r["from"], r["to"], r.get("carries")) for r in TRUTH.routes()}
        self.assertIn(("prodj", "core.osc", None), routes)

    def test_says_hello_asks_and_hears_core(self):
        addresses = TRUTH.addresses()
        self.assertIn("prodj", addresses["device.hello"]["from"])
        self.assertIn("prodj", addresses["state.recall"]["from"])
        self.assertIn("prodj", addresses["core.here"]["to"])


class TheAppSendsWhatTheDeskSends(unittest.TestCase):
    """The app carries the desk's own panel (a3-desk's DeskComponent): every
    word the desk sends, the app sends too -- the panel's TAP among them."""

    def test_every_desk_word_is_a_prodj_word(self):
        for key, entry in TRUTH.addresses().items():
            if "desk" in entry.get("from", []):
                with self.subTest(key=key):
                    self.assertIn("prodj", entry["from"])

    def test_the_tap_reaches_the_analyzer(self):
        routes = {(r["from"], r["to"], r.get("carries")) for r in TRUTH.routes()}
        self.assertIn(("prodj", "beat-analyzer.clock", None), routes)


class ThePositionsGoToTheProdjOnly(unittest.TestCase):
    def test_only_the_prodj_hears_positions(self):
        for key in ("channel.azimuth", "channel.elevation"):
            self.assertEqual(TRUTH.addresses()[key]["relayed_to"], ["prodj"], key)

    def test_the_meaning_says_a_recall_replays_them_to_everyone(self):
        """Live positions go to the pro-DJ apps only; a recall replays the
        last one to every subscriber, as before (a3_core_recall)."""
        for key in ("channel.azimuth", "channel.elevation"):
            meaning = TRUTH.addresses()[key]["meaning"]
            with self.subTest(key=key):
                self.assertIn("live only to the pro-DJ apps followed", meaning)
                self.assertIn("a recall replays the last one to everyone, as before", meaning)
                self.assertNotIn("no one else", meaning)


class TheLiveState(unittest.TestCase):
    def test_live_is_switched_by_three_and_announced_to_four(self):
        entry = TRUTH.addresses()["prodj.live"]
        self.assertEqual(entry["pattern"], "/prodj/live")
        self.assertEqual(entry["args"], "i")
        self.assertEqual(entry["from"], ["prodj", "desk", "motion"])
        self.assertEqual(entry["to"], ["core"])
        self.assertEqual(entry["announced_to"], ["desk", "prodj", "motion", "stemdeck"])


class ThePointers(unittest.TestCase):
    def test_pointers_come_only_from_the_prodj(self):
        for key, device in (("stemdeck.pointer", "stemdeck"), ("motion.pointer", "motion")):
            entry = TRUTH.addresses()[key]
            self.assertEqual(entry["pattern"], f"/{device}/pointer", key)
            self.assertEqual(entry["from"], ["prodj"], key)
            self.assertEqual(entry["to"], ["core"], key)
            self.assertEqual(entry["args"], "iff", key)
            self.assertEqual(entry["relayed_to"], [device], key)


class CoreSaysWhereDevicesRun(unittest.TestCase):
    def test_core_says_where_devices_run(self):
        entry = TRUTH.addresses()["core.device-at"]
        self.assertEqual(entry["pattern"], "/core/device/at")
        self.assertEqual(entry["args"], "ss")
        self.assertEqual(entry["from"], ["core"])
        self.assertEqual(entry["to"], ["prodj"])


if __name__ == "__main__":
    unittest.main()
