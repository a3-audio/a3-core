"""A REAPER that closes and comes back under a running Core keeps the evening.

Measured on 2026-10-14 against the shipped template, with REAPER's OSC surface
sending to a listener: `reaper -closeall:nosave` -- a3-reaper.service's
ExecStop, so every stop, restart and reboot -- makes REAPER report the project
it just closed: every track volume as -2.75, every FX parameter as 0.0, every
mute as 0. Core took those for the evening and wrote them to evening.json, and
the next start replayed every fader and EQ at 0.

A normalised fader is never below 0, so a negative volume on a track the
layout names is the project going away, not a value. The rules are here,
without sockets; test_core_reaper_restart_wiring holds that a3-core.py asks
them.
"""

import sys
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_gate import Gate, StartupRecall   # noqa: E402
from a3_core_reaper import (CLOSED, REOPENED, ReaperProject,   # noqa: E402
                            replay_after_every_start)

EVENING = [("/channel/1/volume", 0.8), ("/channel/1/eq/high", 0.45)]
#: What REAPER said for a closed project's tracks (2026-10-14).
CLOSED_VOLUME = -2.75


def never_asked():
    raise AssertionError("the live evening was read while it was not the evening")


def live_project(evening=EVENING):
    project = ReaperProject(evening)
    project.report("volume", 1.0, never_asked)
    project.replayed(project.round)
    return project


class TheProjectCloses(unittest.TestCase):
    def test_a_negative_volume_closes_it_once(self):
        project = live_project()
        self.assertEqual(CLOSED, project.report("volume", CLOSED_VOLUME, list))
        self.assertIsNone(project.report("volume", CLOSED_VOLUME, list))
        self.assertFalse(project.is_open)
        self.assertFalse(project.live)

    def test_a_fader_at_zero_is_a_value_not_a_close(self):
        project = live_project()
        self.assertIsNone(project.report("volume", 0.0, never_asked))
        self.assertTrue(project.is_open)
        self.assertTrue(project.live)

    def test_a_close_under_a_live_evening_holds_that_evening(self):
        project = live_project()
        tonight = [("/channel/1/volume", 0.3)]
        project.report("volume", CLOSED_VOLUME, lambda: iter(tonight))
        self.assertEqual(tonight, project.evening())

    def test_a_close_before_the_replay_keeps_the_evening_it_holds(self):
        # Core's own start, or a second close before the first replay: what
        # passed through meanwhile is the template's or the closing's.
        project = ReaperProject(EVENING)
        project.report("volume", CLOSED_VOLUME, never_asked)
        self.assertEqual(EVENING, project.evening())

    def test_the_closing_reports_do_not_reopen_it(self):
        project = live_project()
        project.report("volume", CLOSED_VOLUME, list)
        for word in ("fxparam", "mute", "bypass", ""):
            self.assertIsNone(project.report(word, 0.0, never_asked), word)
        self.assertFalse(project.is_open)

    def test_every_close_is_a_new_round(self):
        project = live_project()
        before = project.round
        project.report("volume", CLOSED_VOLUME, list)
        self.assertEqual(before + 1, project.round)


class TheProjectComesBack(unittest.TestCase):
    def setUp(self):
        self.project = live_project()
        self.project.report("volume", CLOSED_VOLUME, list)

    def test_a_volume_a_live_track_can_have_reopens_it(self):
        # A fresh REAPER's pass starts with /track/1/volume and carries every
        # track's volume (measured).
        self.assertEqual(REOPENED, self.project.report("volume", 1.0, never_asked))
        self.assertTrue(self.project.is_open)
        self.assertIsNone(self.project.report("volume", 0.5, never_asked))

    def test_reopened_is_not_yet_the_evening(self):
        self.project.report("volume", 1.0, never_asked)
        self.assertFalse(self.project.live)

    def test_the_replay_of_this_round_makes_it_the_evening_again(self):
        self.project.report("volume", 1.0, never_asked)
        self.assertTrue(self.project.replayed(self.project.round))
        self.assertTrue(self.project.live)


class AReplayIntoAClosingReaper(unittest.TestCase):
    def test_a_replay_from_an_earlier_round_does_not_count(self):
        project = live_project()
        started = project.round
        project.report("volume", CLOSED_VOLUME, list)
        project.report("volume", 1.0, never_asked)
        self.assertFalse(project.replayed(started))
        self.assertFalse(project.live)

    def test_a_replay_while_closed_does_not_count(self):
        project = live_project()
        project.report("volume", CLOSED_VOLUME, list)
        self.assertFalse(project.replayed(project.round))
        self.assertFalse(project.live)


class CoresOwnStart(unittest.TestCase):
    def test_nothing_is_the_evening_until_the_first_replay(self):
        project = ReaperProject(EVENING)
        self.assertFalse(project.live)
        self.assertIsNone(project.report("fxparam", 0.5, never_asked))
        self.assertTrue(project.is_open)
        self.assertFalse(project.live)
        self.assertEqual(EVENING, project.evening())
        self.assertTrue(project.replayed(project.round))
        self.assertTrue(project.live)

    def test_the_evening_can_come_from_a_generator(self):
        project = ReaperProject(iter(EVENING))
        self.assertEqual(EVENING, project.evening())
        self.assertEqual(EVENING, project.evening())


class TheReplayLoop(unittest.TestCase):
    """Core's start-up wait, once per REAPER that comes up."""

    def setUp(self):
        self.heard = threading.Event()
        self.gone = threading.Event()
        self.log = []

    def run_rounds(self, rounds, replay=None):
        worker = threading.Thread(
            target=replay_after_every_start, daemon=True,
            args=(self.heard, self.gone, lambda: self.log.append("ask"),
                  replay or (lambda: self.log.append("replay"))),
            kwargs={"every": 0.01, "say": lambda _line: None,
                    "rounds": rounds})
        worker.start()
        return worker

    def test_the_first_round_is_cores_own_start(self):
        self.heard.set()
        self.run_rounds(1).join(1.0)
        self.assertEqual(["ask", "replay"], self.log)

    def test_after_a_replay_it_waits_for_reaper_to_go(self):
        self.heard.set()
        worker = self.run_rounds(2)
        worker.join(0.1)
        self.assertTrue(worker.is_alive())
        self.assertEqual(1, self.log.count("replay"))
        self.gone.set()
        worker.join(1.0)
        self.assertFalse(worker.is_alive())
        self.assertEqual(2, self.log.count("replay"))

    def test_a_reaper_that_went_and_is_not_back_is_asked_not_replayed(self):
        self.heard.set()
        worker = self.run_rounds(2)
        worker.join(0.1)
        self.heard.clear()
        self.gone.set()
        worker.join(0.1)
        self.assertEqual(1, self.log.count("replay"))
        self.assertGreater(self.log.count("ask"), 2)
        self.heard.set()
        worker.join(1.0)
        self.assertEqual(2, self.log.count("replay"))

    def test_a_close_during_the_replay_is_not_missed(self):
        def replay():
            self.log.append("replay")
            self.gone.set()

        self.heard.set()
        self.run_rounds(2, replay).join(1.0)
        self.assertEqual(2, self.log.count("replay"))


class TheGateForgets(unittest.TestCase):
    def test_after_forgetting_nothing_is_opened_and_all_are_unheard(self):
        gate = Gate({"main": 30, "booth": 31})
        gate.heard(30, "mute", 1.0)
        gate.heard(30, "volume", 0.2)
        gate.heard(31, "mute", 0.0)
        gate.forget()
        self.assertEqual(([], []), gate.to_open())
        self.assertEqual(["main", "booth"], gate.unheard())


class TheRecallIsRearmed(unittest.TestCase):
    def test_a_rearmed_recall_takes_its_new_token_once(self):
        recall = StartupRecall(token=7)
        self.assertTrue(recall.is_it([7]))
        token = recall.rearm()
        self.assertEqual(token, recall.token)
        self.assertGreaterEqual(token, 2)
        self.assertTrue(recall.is_it([token]))
        self.assertFalse(recall.is_it([token]))

    def test_the_old_token_is_not_it_after_a_rearm(self):
        recall = StartupRecall(token=7)
        recall.rearm(token=9)
        self.assertFalse(recall.is_it([7]))
        self.assertTrue(recall.is_it([9]))


if __name__ == "__main__":
    unittest.main()
