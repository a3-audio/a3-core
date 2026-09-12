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

from a3_core_tempo import (DEFAULT_DEADBAND, NO_CHANGE,   # noqa: E402
                           TempoFollower)


class TheFirstOneAlwaysGoes(unittest.TestCase):
    def test_nothing_has_been_sent_yet_so_anything_is_news(self):
        self.assertEqual(TempoFollower().wanted(132.5), 132.5)

    def test_and_only_the_first_one(self):
        follower = TempoFollower()
        self.assertEqual(follower.wanted(132.5), 132.5)
        self.assertIs(follower.wanted(132.5), NO_CHANGE)


class TheDeadband(unittest.TestCase):
    def setUp(self):
        self.follower = TempoFollower()
        self.follower.wanted(130.0)

    def test_a_wobble_smaller_than_the_deadband_is_not_worth_a_message(self):
        for bpm in (130.0, 130.05, 129.96):
            self.assertIs(self.follower.wanted(bpm), NO_CHANGE, bpm)

    def test_a_real_change_goes_through(self):
        self.assertEqual(self.follower.wanted(140.0), 140.0)

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
        self.assertEqual(self.follower.wanted(130.0 + DEFAULT_DEADBAND * 2),
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
        like a change, or hides it."""
        self.assertEqual(self.follower.wanted(130.0), 130.0)
        self.assertIs(self.follower.wanted(0.0), NO_CHANGE)
        self.assertIs(self.follower.wanted(130.0), NO_CHANGE)


class TheRangeItAccepts(unittest.TestCase):
    """The analyzer's own .env runs 60 to 140, but a Pioneer deck or a tap
    can be outside that, so the guard here is wider than the analyzer's and
    only catches what cannot be a tempo at all."""

    def test_the_ends_are_in(self):
        from a3_core_tempo import BPM_MAX, BPM_MIN
        self.assertEqual(TempoFollower().wanted(BPM_MIN), BPM_MIN)
        self.assertEqual(TempoFollower().wanted(BPM_MAX), BPM_MAX)

    def test_just_outside_is_out(self):
        from a3_core_tempo import BPM_MAX, BPM_MIN
        self.assertIs(TempoFollower().wanted(BPM_MIN - 0.01), NO_CHANGE)
        self.assertIs(TempoFollower().wanted(BPM_MAX + 0.01), NO_CHANGE)


class NoChangeIsNotAZero(unittest.TestCase):
    def test_it_refuses_to_be_read_as_a_number(self):
        """Same trick as a3_core_buttons.NO_CHANGE: a caller that writes
        `if follower.wanted(bpm):` has asked the wrong question, and gets an
        error rather than a delay line set to nothing."""
        with self.assertRaises(TypeError):
            bool(NO_CHANGE)


if __name__ == "__main__":
    unittest.main()
