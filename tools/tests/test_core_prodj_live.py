# SPDX-FileCopyrightText: 2026 Patric Schmitz, Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Pro-DJ LIVE is a lease Core holds: off at start, repeated every five
seconds while on, and the only thing that lets a prodj move the room."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

from a3_core_prodj_live import ProdjLive, may_switch   # noqa: E402


def live_on():
    live = ProdjLive()
    live.set(True, 0.0)
    return live


class TheState(unittest.TestCase):
    def test_starts_off(self):
        self.assertFalse(ProdjLive().on)

    def test_set_reports_a_change_once(self):
        live = ProdjLive()
        self.assertTrue(live.set(True, 0.0))
        self.assertFalse(live.set(True, 1.0))
        self.assertTrue(live.on)
        self.assertTrue(live.set(False, 2.0))
        self.assertFalse(live.set(False, 3.0))

    def test_the_last_prodj_leaving_switches_it_off(self):
        live = live_on()
        self.assertTrue(live.nobody_left())
        self.assertFalse(live.on)
        self.assertFalse(live.nobody_left())


class TheLease(unittest.TestCase):
    def test_repeats_every_five_seconds_while_on(self):
        live = live_on()
        self.assertFalse(live.due(4.9))
        self.assertTrue(live.due(5.0))
        self.assertFalse(live.due(6.0))
        self.assertTrue(live.due(10.0))

    def test_never_repeats_while_off(self):
        live = ProdjLive()
        for now in (0.0, 5.0, 100.0):
            self.assertFalse(live.due(now))

    def test_switching_on_again_restarts_the_count(self):
        live = live_on()
        live.set(False, 1.0)
        live.set(True, 20.0)
        self.assertFalse(live.due(24.9))
        self.assertTrue(live.due(25.0))


class Admission(unittest.TestCase):
    def test_a_prodj_moves_nothing_while_off(self):
        live = ProdjLive()
        self.assertFalse(live.admits("prodj", "channel.volume"))
        for key in ("device.hello", "state.recall", "prodj.live"):
            with self.subTest(key=key):
                self.assertTrue(live.admits("prodj", key))

    def test_a_prodj_moves_things_while_on(self):
        live = live_on()
        self.assertTrue(live.admits("prodj", "channel.volume"))
        self.assertTrue(live.admits("prodj", "stemdeck.pointer"))
        self.assertTrue(live.admits("prodj", "motion.pointer"))

    def test_others_are_always_admitted(self):
        for live in (ProdjLive(), live_on()):
            for kind in ("desk", "motion"):
                with self.subTest(kind=kind, on=live.on):
                    self.assertTrue(live.admits(kind, "channel.volume"))

    def test_no_kind_at_all_is_admitted_too(self):
        # Motion's origin is the bare name "motion": kind_of() is None.
        for live in (ProdjLive(), live_on()):
            self.assertTrue(live.admits(None, "channel.volume"))

    def test_pointers_pass_only_from_a_prodj_while_on(self):
        for live in (ProdjLive(), live_on()):
            for kind in ("desk", "motion", None):
                for key in ("stemdeck.pointer", "motion.pointer"):
                    with self.subTest(kind=kind, key=key, on=live.on):
                        self.assertFalse(live.admits(kind, key))


class WhoSwitches(unittest.TestCase):
    def test_motion_may_only_switch_off(self):
        self.assertTrue(may_switch("motion", False))
        self.assertFalse(may_switch("motion", True))
        self.assertTrue(may_switch("prodj", True))
        self.assertTrue(may_switch("desk", True))
        self.assertFalse(may_switch("mixer", True))
        self.assertFalse(may_switch("mixer", False))


if __name__ == "__main__":
    unittest.main()
