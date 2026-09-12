"""Das Tempo weitergeben, aber nicht jeden Wimpernschlag.

The beat-analyzer sends `/beat beat bar bpm` on every beat -- better than
twice a second at club tempo. The delay only needs to hear about it when it
has actually changed, and a delay line whose length is rewritten constantly
is the one effect where that is audible.

So this is the small thing between the two: what came in, what was last
passed on, and whether the difference is worth a message.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core"
                       / "home/aaa/.local/lib"))

from a3_core_tempo import (DEFAULT_DEADBAND, DEFAULT_STEADY_BEATS,   # noqa: E402
                           NO_CHANGE, TempoFollower)


def settled(follower, bpm, beats=None):
    """Hand the same tempo in until the follower accepts it.

    Every test below that is not about steadiness itself wants a tempo that
    has arrived, and a tempo arrives only after it has held still.
    """
    beats = DEFAULT_STEADY_BEATS if beats is None else beats
    for _ in range(beats):
        answer = follower.wanted(bpm)
        if answer is not NO_CHANGE:
            return answer
    return NO_CHANGE


class TheFirstOneStillHasToHoldStill(unittest.TestCase):
    def test_one_beat_is_not_a_tempo_yet(self):
        self.assertIs(TempoFollower().wanted(132.5), NO_CHANGE)

    def test_a_bar_of_the_same_tempo_is(self):
        self.assertEqual(settled(TempoFollower(), 132.5), 132.5)

    def test_and_then_it_stays_quiet(self):
        follower = TempoFollower()
        settled(follower, 132.5)
        for _ in range(8):
            self.assertIs(follower.wanted(132.5), NO_CHANGE)


class TheDeadband(unittest.TestCase):
    def setUp(self):
        self.follower = TempoFollower()
        settled(self.follower, 130.0)

    def test_a_wobble_smaller_than_the_deadband_is_not_worth_a_message(self):
        for bpm in (130.0, 130.05, 129.96):
            self.assertIs(self.follower.wanted(bpm), NO_CHANGE, bpm)

    def test_a_real_change_goes_through_once_it_holds(self):
        self.assertIs(self.follower.wanted(140.0), NO_CHANGE)
        self.assertEqual(settled(self.follower, 140.0), 140.0)

    def test_the_deadband_is_measured_from_what_was_sent_not_from_the_last_look(self):
        """Otherwise a slow drift creeps past it a hundredth at a time and
        the delay ends up somewhere nobody asked for."""
        for bpm in (130.05, 130.09, 130.05, 130.09):
            self.assertIs(self.follower.wanted(bpm), NO_CHANGE, bpm)
        self.assertIs(self.follower.wanted(130.09), NO_CHANGE)

    def test_clearly_over_the_deadband_goes_and_clearly_under_stays(self):
        """The exact edge is deliberately not pinned.

        `130.0 + 0.1` is 130.09999999999999 in binary floating point, so
        "exactly one deadband away" is smaller than a deadband and any test
        of the boundary tests the arithmetic of doubles rather than this
        class. What matters is that either side of it behaves, with room to
        spare.
        """
        self.assertIs(self.follower.wanted(130.0 + DEFAULT_DEADBAND / 2),
                      NO_CHANGE)
        self.assertEqual(settled(self.follower, 130.0 + DEFAULT_DEADBAND * 2),
                         130.0 + DEFAULT_DEADBAND * 2)


class WhatIsNotATempo(unittest.TestCase):
    """The beat arrives over UDP from another program. None of this should
    reach a plug-in that will happily set a delay line to it."""

    def setUp(self):
        self.follower = TempoFollower()

    def test_zero_is_refused(self):
        self.assertIs(self.follower.wanted(0.0), NO_CHANGE)

    def test_negative_is_refused(self):
        self.assertIs(self.follower.wanted(-120.0), NO_CHANGE)

    def test_absurdly_fast_is_refused(self):
        self.assertIs(self.follower.wanted(100000.0), NO_CHANGE)

    def test_not_a_number_is_refused(self):
        self.assertIs(self.follower.wanted(float("nan")), NO_CHANGE)
        self.assertIs(self.follower.wanted(float("inf")), NO_CHANGE)

    def test_a_refusal_does_not_disturb_what_was_sent(self):
        """A bad value must not become the thing the deadband measures
        against -- otherwise one stray message makes the next good one look
        like a change, or hides it. It must not break a run of steady beats
        either: a single dropout in the middle of a settled tempo is not a
        tempo change."""
        self.assertEqual(settled(self.follower, 130.0), 130.0)
        self.assertIs(self.follower.wanted(0.0), NO_CHANGE)
        self.assertIs(self.follower.wanted(130.0), NO_CHANGE)


class TheRangeItAccepts(unittest.TestCase):
    """The analyzer's own .env runs 60 to 140, but a Pioneer deck or a tap
    can be outside that, so the guard here is wider than the analyzer's and
    only catches what cannot be a tempo at all."""

    def test_the_ends_are_in(self):
        from a3_core_tempo import BPM_MAX, BPM_MIN
        self.assertEqual(settled(TempoFollower(), BPM_MIN), BPM_MIN)
        self.assertEqual(settled(TempoFollower(), BPM_MAX), BPM_MAX)

    def test_just_outside_is_out(self):
        from a3_core_tempo import BPM_MAX, BPM_MIN
        self.assertIs(settled(TempoFollower(), BPM_MIN - 0.01), NO_CHANGE)
        self.assertIs(settled(TempoFollower(), BPM_MAX + 0.01), NO_CHANGE)


class NoChangeIsNotAZero(unittest.TestCase):
    def test_it_refuses_to_be_read_as_a_number(self):
        """Same trick as a3_core_buttons.NO_CHANGE: a caller that writes
        `if follower.wanted(bpm):` has asked the wrong question, and gets an
        error rather than a delay line set to nothing."""
        with self.assertRaises(TypeError):
            bool(NO_CHANGE)


class ATempoOnTheMoveIsNotATempoYet(unittest.TestCase):
    """The reason this class exists at all, measured on the rig 2026-09-12.

    The analyzer's estimate does not only wobble, it *ramps*: 107.7 down to
    102.7 over four seconds, a whole BPM per beat. A deadband alone let every
    single beat through -- 34 rewrites of the delay line in a few seconds,
    which is the pitch wobble this was supposed to prevent.

    So a tempo has to hold still before it counts. While it is moving,
    nothing is sent at all, and the delay keeps the last tempo that meant
    something.
    """

    def test_a_ramp_sends_nothing(self):
        follower = TempoFollower()
        settled(follower, 130.0)
        for bpm in (129.0, 128.0, 127.0, 126.0, 125.0, 124.0, 123.0):
            self.assertIs(follower.wanted(bpm), NO_CHANGE, bpm)

    def test_and_the_tempo_it_comes_to_rest_on_does(self):
        follower = TempoFollower()
        settled(follower, 130.0)
        for bpm in (129.0, 128.0, 127.0, 126.0):
            follower.wanted(bpm)
        self.assertEqual(settled(follower, 126.0), 126.0)

    def test_wandering_back_and_forth_never_settles(self):
        """Two readings that alternate are not a tempo, however long they go
        on -- neither of them ever holds still."""
        follower = TempoFollower()
        settled(follower, 130.0)
        for _ in range(20):
            self.assertIs(follower.wanted(124.0), NO_CHANGE)
            self.assertIs(follower.wanted(126.0), NO_CHANGE)

    def test_a_run_is_counted_in_beats_not_in_seconds(self):
        """One bar at four beats. Nothing here reads a clock: the beats are
        the clock, and that is the point -- at half the tempo the wait is
        twice as long in seconds and exactly as long musically."""
        self.assertEqual(DEFAULT_STEADY_BEATS, 4)


if __name__ == "__main__":
    unittest.main()
