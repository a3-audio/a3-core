"""The silent start, decided (a3-system#74, decided 2026-10-07).

REAPER's template starts main, booth and phones muted. Core opens them after
its start-up recall has been applied -- fader down, unmute, then 1 s back up
to the fader REAPER reported -- and only those REAPER reported muted: tracks
reported open are a running set, and Core leaves them alone. The rules are
here, without sockets; test_core_gate_wiring holds that a3-core.py asks them.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_gate import (FADE_SECONDS, FADE_STEPS, SETTLE_SECONDS,   # noqa: E402
                          Gate, Opening, StartupRecall, fade_frames,
                          open_after_recall, run_fade)

TRACKS = {"main": 30, "booth": 31, "phones": 32}


def address(name, track):
    return {"track_mute": f"/track/{track}/mute",
            "track_volume": f"/track/{track}/volume"}[name]


def reported(muted, faders):
    """A gate that has heard REAPER's refresh: `muted` per track, `faders`
    per track (None = no fader heard)."""
    gate = Gate(TRACKS)
    for track, mute in muted.items():
        gate.heard(track, "mute", mute)
    for track, fader in faders.items():
        if fader is not None:
            gate.heard(track, "volume", fader)
    return gate


def cold():
    return reported({30: 1.0, 31: 1.0, 32: 1.0}, {30: 0.55, 31: 0.55, 32: 0.72})


def running():
    return reported({30: 0.0, 31: 0.0, 32: 0.0}, {30: 0.55, 31: 0.55, 32: 0.72})


class WhatTheGateHears(unittest.TestCase):
    def test_only_mute_and_volume_of_a_gate_track_are_taken(self):
        gate = Gate(TRACKS)
        self.assertTrue(gate.heard(30, "mute", 1.0))
        self.assertTrue(gate.heard(31, "volume", 0.5))
        self.assertFalse(gate.heard(33, "mute", 1.0))   # rec: never gated
        self.assertFalse(gate.heard(30, "pan", 0.5))
        self.assertFalse(gate.heard(23, "volume", 0.5))

    def test_a_cold_reaper_opens_all_three_to_their_faders(self):
        self.assertEqual(cold().to_open(),
                         ([Opening("main", 30, 0.55), Opening("booth", 31, 0.55),
                           Opening("phones", 32, 0.72)], []))

    def test_a_running_set_is_left_alone(self):
        self.assertEqual(running().to_open(), ([], []))

    def test_a_track_never_reported_is_not_opened(self):
        # No REAPER answer: the gate stays shut, nothing is sent.
        self.assertEqual(Gate(TRACKS).to_open(), ([], []))

    def test_a_muted_track_without_a_fader_stays_shut_and_is_named(self):
        gate = reported({30: 1.0, 31: 1.0}, {30: 0.5})
        self.assertEqual(gate.to_open(), ([Opening("main", 30, 0.5)], ["booth"]))

    def test_the_last_report_wins(self):
        gate = cold()
        gate.heard(30, "mute", 0.0)
        self.assertEqual([o.name for o in gate.to_open()[0]], ["booth", "phones"])

    def test_one_hand_muted_track_in_a_running_set_is_opened(self):
        # The spec's rule is per track: Core asks, it does not remember, and a
        # hand-muted phones looks like a cold one. Pinned so that changing the
        # rule (e.g. "only when all three are muted") is a decision.
        gate = reported({30: 0.0, 31: 0.0, 32: 1.0}, {30: 0.5, 31: 0.5, 32: 0.7})
        self.assertEqual(gate.to_open(), ([Opening("phones", 32, 0.7)], []))


class TheFade(unittest.TestCase):
    def setUp(self):
        self.opening = cold().to_open()[0]
        self.frames = fade_frames(self.opening, address)

    def volumes(self, track):
        return [value for frame in self.frames[1:] for sent, value in frame
                if sent == f"/track/{track}/volume"]

    def test_one_second_in_fifty_steps_after_a_short_settle(self):
        self.assertEqual((FADE_SECONDS, FADE_STEPS, SETTLE_SECONDS), (1.0, 50, 0.2))
        self.assertEqual(len(self.frames), FADE_STEPS + 1)

    def test_the_fader_is_down_before_the_mute_lifts(self):
        first = self.frames[0]
        for o in self.opening:
            self.assertLess(first.index((f"/track/{o.track}/volume", 0.0)),
                            first.index((f"/track/{o.track}/mute", 0.0)))

    def test_it_rises_and_never_passes_the_target(self):
        for o in self.opening:
            values = self.volumes(o.track)
            self.assertEqual(values, sorted(values))
            self.assertLessEqual(max(values), o.target)

    def test_it_ends_on_exactly_the_reported_fader(self):
        last = dict(self.frames[-1])
        for o in self.opening:
            self.assertEqual(last[f"/track/{o.track}/volume"], o.target)

    def test_the_mute_is_touched_once_at_the_start(self):
        for frame in self.frames[1:]:
            self.assertFalse(any(sent.endswith("/mute") for sent, _ in frame))

    def test_nothing_to_open_is_no_frames(self):
        self.assertEqual(fade_frames([], address), [])

    def test_frames_are_a_snapshot(self):
        # REAPER echoes every step; a report arriving mid-fade must not move
        # the target of a fade already planned.
        gate = cold()
        frames = fade_frames(gate.to_open()[0], address)
        gate.heard(30, "volume", 0.02)
        self.assertEqual(dict(frames[-1])["/track/30/volume"], 0.55)

    def test_every_frame_is_sent_twenty_ms_apart(self):
        sent, slept = [], []
        run_fade(self.frames, lambda a, v: sent.append((a, v)), sleep=slept.append)
        self.assertEqual(sent, [message for frame in self.frames for message in frame])
        self.assertEqual(len(slept), FADE_STEPS)
        self.assertAlmostEqual(sum(slept), FADE_SECONDS)


class OpeningAfterTheRecall(unittest.TestCase):
    def open(self, gate):
        sent, said, slept = [], [], []
        opened = open_after_recall(gate, address, lambda a, v: sent.append((a, v)),
                                   said.append, sleep=slept.append)
        return opened, sent, said, slept

    def test_it_settles_before_anything_is_sent(self):
        _, _, _, slept = self.open(cold())
        self.assertEqual(slept[0], SETTLE_SECONDS)

    def test_a_cold_start_says_what_it_opened(self):
        opened, sent, said, _ = self.open(cold())
        self.assertEqual([o.name for o in opened], ["main", "booth", "phones"])
        self.assertIn("gate: opened main, booth, phones after the recall", said)
        self.assertEqual(sent[-1], ("/track/32/volume", 0.72))

    def test_a_running_set_gets_nothing_sent_and_no_opened_line(self):
        opened, sent, said, _ = self.open(running())
        self.assertEqual((opened, sent), ([], []))
        self.assertFalse(any("gate: opened" in line for line in said))
        self.assertIn("gate: left alone, REAPER reports its outputs open", said)

    def test_no_mute_report_is_named_and_is_not_called_open(self):
        opened, sent, said, _ = self.open(reported({32: 0.0}, {32: 0.72}))
        self.assertEqual((opened, sent), ([], []))
        self.assertIn("gate: no mute report from main, booth \u2014 left shut", said)
        self.assertFalse(any("left alone" in line for line in said))

    def test_a_partial_report_names_only_the_unheard(self):
        opened, _, said, _ = self.open(reported({30: 1.0, 32: 0.0},
                                                {30: 0.5, 32: 0.7}))
        self.assertEqual([o.name for o in opened], ["main"])
        self.assertIn("gate: no mute report from booth \u2014 left shut", said)

    def test_a_shut_track_is_named_and_not_touched(self):
        opened, sent, said, _ = self.open(reported({30: 1.0, 31: 1.0}, {30: 0.5}))
        self.assertEqual([o.name for o in opened], ["main"])
        self.assertIn("gate: booth stays shut, REAPER reported no fader for it", said)
        self.assertFalse(any("/track/31/" in sent_address for sent_address, _ in sent))


class TheStartupRecall(unittest.TestCase):
    def test_a_devices_recall_is_not_it(self):
        recall = StartupRecall()
        for arguments in ((1,), (0,), ()):
            self.assertFalse(recall.is_it(arguments))

    def test_the_token_is_never_what_a_device_sends_and_fits_an_osc_int(self):
        for _ in range(1000):
            self.assertTrue(1 < StartupRecall().token < 2 ** 31)

    def test_cores_own_recall_is_it_once(self):
        recall = StartupRecall(token=12345)
        self.assertTrue(recall.is_it((12345,)))
        self.assertFalse(recall.is_it((12345,)))

    def test_a_word_is_not_it(self):
        self.assertFalse(StartupRecall(token=5).is_it(("five",)))


if __name__ == "__main__":
    unittest.main()
