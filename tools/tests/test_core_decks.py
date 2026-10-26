# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Core's engine-mode deck selector: the desk's input ring (deck A, deck B,
..., analog) on each channel, the deck 3D that follows it, and deck presence.
Pure: the clock is passed in."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

import a3_osc                                    # noqa: E402
from a3_core_decks import ANALOG, DeckSelector   # noqa: E402

TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")


def pick(selector, channel, source):
    """Turn channel's cursor onto `source` and push; the changes."""
    while selector.ring(channel)[selector.cursor(channel)] != source:
        selector.turn(channel, 1)
    return selector.push(channel)


class TheRing(unittest.TestCase):
    def test_ring_is_decks_then_analog(self):
        # The desk reads D1 D2 ... A, analog last (maintainer, 2026-10-26).
        s = DeckSelector()
        self.assertEqual(s.ring(1), [1, 2, ANALOG])
        self.assertEqual(DeckSelector(decks=4).ring(1), [1, 2, 3, 4, ANALOG])

    def test_cursor_starts_on_analog(self):
        s = DeckSelector()
        self.assertEqual(s.ring(1)[s.cursor(1)], ANALOG)

    def test_turn_wraps_both_ways(self):
        s = DeckSelector()
        s.turn(1, 1)
        self.assertEqual(s.ring(1)[s.cursor(1)], 1)
        s.turn(1, -2)
        self.assertEqual(s.ring(1)[s.cursor(1)], 2)
        s.turn(1, 1)
        self.assertEqual(s.ring(1)[s.cursor(1)], ANALOG)

    def test_turn_alone_changes_no_source(self):
        s = DeckSelector()
        s.turn(1, 1)
        self.assertEqual(s.source(1), ANALOG)

    def test_push_selects_the_cursor(self):
        s = DeckSelector()
        s.turn(2, 1)          # from analog, past the end, onto deck 1
        self.assertEqual(s.push(2), [(2, 1)])
        self.assertEqual(s.source(2), 1)
        self.assertEqual(s.channel_of(1), 2)
        self.assertTrue(s.deck_channel(2))
        self.assertFalse(s.deck_channel(1))

    def test_a_deck_is_on_one_channel_at_most(self):
        s = DeckSelector()
        pick(s, 1, 1)
        changes = pick(s, 3, 1)
        self.assertEqual(changes, [(3, 1), (1, ANALOG)])
        self.assertEqual(s.source(1), ANALOG)
        self.assertEqual(s.channel_of(1), 3)
        self.assertEqual(s.ring(1)[s.cursor(1)], ANALOG)

    def test_two_decks_may_sit_on_two_channels(self):
        s = DeckSelector()
        pick(s, 1, 1)
        pick(s, 2, 2)
        self.assertEqual([s.source(c) for c in (1, 2, 3, 4)], [1, 2, 0, 0])

    def test_push_on_the_same_source_changes_nothing(self):
        s = DeckSelector()
        pick(s, 1, 2)
        self.assertEqual(s.push(1), [])
        self.assertEqual(s.push(4), [])      # analog on analog

    def test_channel_of_a_free_deck_is_none(self):
        self.assertIsNone(DeckSelector().channel_of(1))


class TheThreeD(unittest.TestCase):
    def test_three_d_goes_to_the_deck_on_that_channel_only(self):
        s = DeckSelector()
        pick(s, 2, 1)
        self.assertEqual(s.three_d_messages(TRUTH, 2, 0.4), [("/stemdeck/1/3d", 0.4)])
        self.assertEqual(s.three_d_messages(TRUTH, 1, 0.4), [])

    def test_a_leaving_deck_gets_neutral_three_d(self):
        s = DeckSelector()
        pick(s, 1, 2)
        changes = pick(s, 1, ANALOG)
        self.assertEqual(s.leave_messages(TRUTH, changes), [("/stemdeck/2/3d", 1.0)])

    def test_a_deck_that_only_moves_is_not_neutralised(self):
        s = DeckSelector()
        pick(s, 1, 1)
        changes = pick(s, 2, 1)
        self.assertEqual(s.leave_messages(TRUTH, changes), [])

    def test_a_deck_displaced_by_another_pick_on_its_channel_leaves(self):
        s = DeckSelector()
        pick(s, 1, 1)
        changes = pick(s, 1, 2)
        self.assertEqual(s.leave_messages(TRUTH, changes), [("/stemdeck/1/3d", 1.0)])

    def test_a_pick_without_a_leaver_sends_no_leave(self):
        s = DeckSelector()
        changes = pick(s, 1, 1)
        self.assertEqual(s.leave_messages(TRUTH, changes), [])

    def test_hello_resends_the_three_d(self):
        s = DeckSelector()
        pick(s, 3, 2)
        self.assertEqual(s.hello_messages(TRUTH, 2, 0.7), [("/stemdeck/2/3d", 0.7)])

    def test_hello_of_a_deck_on_no_channel_sends_neutral(self):
        s = DeckSelector()
        self.assertEqual(s.hello_messages(TRUTH, 1, 0.7), [("/stemdeck/1/3d", 1.0)])


class Presence(unittest.TestCase):
    def test_presence_times_out_after_five_seconds(self):
        s = DeckSelector()
        self.assertFalse(s.present(1, 0.0))
        s.heard(1, 100.0)
        self.assertTrue(s.present(1, 100.0))
        self.assertTrue(s.present(1, 105.0))
        self.assertFalse(s.present(1, 105.01))
        self.assertFalse(s.present(2, 100.0))

    def test_absent_deck_keeps_its_channel(self):
        s = DeckSelector()
        pick(s, 1, 1)
        s.heard(1, 0.0)
        self.assertFalse(s.present(1, 60.0))
        self.assertEqual(s.source(1), 1)
        self.assertEqual(s.channel_of(1), 1)


class OnDisk(unittest.TestCase):
    def test_round_trip(self):
        s = DeckSelector()
        pick(s, 2, 1)
        pick(s, 4, 2)
        self.assertEqual(s.to_evening(), {"channel.source": [0, 1, 0, 2]})
        t = DeckSelector()
        t.from_evening(s.to_evening())
        self.assertEqual([t.source(c) for c in (1, 2, 3, 4)], [0, 1, 0, 2])
        self.assertEqual(t.ring(2)[t.cursor(2)], 1)

    def test_old_stem_cursors_read_as_analog(self):
        t = DeckSelector()
        t.from_evening({"cursors": [0, 4, 8, 8], "sources": [1, 1, None, 0]})
        self.assertEqual(t.to_evening(), {"channel.source": [0, 0, 0, 0]})

    def test_unknown_decks_and_junk_read_as_analog(self):
        t = DeckSelector()
        t.from_evening({"channel.source": [3, -1, True, "x"]})
        self.assertEqual(t.to_evening(), {"channel.source": [0, 0, 0, 0]})

    def test_a_deck_twice_keeps_the_first(self):
        t = DeckSelector()
        t.from_evening({"channel.source": [0, 1, 1, 2]})
        self.assertEqual(t.to_evening(), {"channel.source": [0, 1, 0, 2]})

    def test_not_a_dict_or_wrong_length_is_ignored(self):
        for junk in (None, [], {"channel.source": [1, 1]}, {"channel.source": "1"}):
            t = DeckSelector()
            t.from_evening(junk)
            self.assertEqual(t.to_evening(), {"channel.source": [0, 0, 0, 0]})


if __name__ == "__main__":
    unittest.main()
