"""The mirror of StemDeck's bus switches and the desk's rules on it
(spec stemdeck-remote, 2026-10-01). Pair p = deck ceil(p/4), stem
(p-1) % 4 + 1; bus 1-4 the desk channels, 5 AUX, 6 CUE."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

from a3_core_stems import AUX, CUE, Stems  # noqa: E402


def bit(bus):
    return 1 << (bus - 1)


def stems_with(**masks):
    s = Stems()
    for name, mask in masks.items():
        s.report(int(name[1:]), mask)
    return s


class TheMirror(unittest.TestCase):
    def test_a_fresh_mirror_has_nothing_on(self):
        s = Stems()
        self.assertEqual(s.masks, [0] * 8)
        self.assertEqual([s.channel_mask(c) for c in range(4)], [0] * 4)

    def test_a_report_sets_the_mask(self):
        s = stems_with(p3=bit(2) | bit(AUX))
        self.assertEqual(s.channel_mask(1), 1 << 2)
        self.assertTrue(s.plays_on_return(3))

    def test_a_channel_shows_every_stem_on_its_bus(self):
        s = stems_with(p1=bit(1), p6=bit(1))
        self.assertEqual(s.channel_mask(0), (1 << 0) | (1 << 5))

    def test_a_damaged_report_changes_nothing(self):
        s = Stems()
        for pair, mask in ((0, 1), (9, 1), (1, 64), (1, -1), ("1", 1), (1, 1.5), (True, 1)):
            self.assertFalse(s.report(pair, mask), (pair, mask))
        self.assertEqual(s.masks, [0] * 8)

    def test_a_good_report_is_accepted(self):
        self.assertTrue(Stems().report(8, 63))

    def test_any_cued_reads_the_cue_bus(self):
        self.assertFalse(stems_with(p2=bit(1)).any_cued())
        self.assertTrue(stems_with(p2=bit(CUE)).any_cued())


class ChannelTurns(unittest.TestCase):
    def test_one_click_from_a_takes_the_first_free_stem(self):
        s = Stems()
        self.assertEqual(s.turn_channel(0, +1), [(1, 1, True), (1, AUX, False)])
        self.assertEqual(s.channel_mask(0), 1)

    def test_a_stem_on_another_channel_is_skipped(self):
        s = stems_with(p1=bit(2))
        s.turn_channel(0, +1)
        self.assertEqual(s.channel_mask(0), 1 << 1)       # pair 2

    def test_turning_back_to_a_releases_and_gives_aux_back(self):
        s = stems_with(p1=bit(1))
        self.assertEqual(s.turn_channel(0, -1), [(1, 1, False), (1, AUX, True)])
        self.assertEqual(s.channel_mask(0), 0)

    def test_a_turn_from_several_leaves_one(self):
        s = stems_with(p2=bit(1), p5=bit(1))
        commands = s.turn_channel(0, +1)                   # from pair 2 to the next free: 3
        self.assertIn((3, 1, True), commands)
        self.assertIn((2, 1, False), commands)
        self.assertIn((5, 1, False), commands)
        self.assertEqual(s.channel_mask(0), 1 << 2)

    def test_a_released_stem_still_on_another_channel_keeps_aux_off(self):
        s = stems_with(p1=bit(1) | bit(2))
        commands = s.turn_channel(0, -1)
        self.assertIn((1, 1, False), commands)
        self.assertNotIn((1, AUX, True), commands)

    def test_a_long_turn_wraps(self):
        s = Stems()
        s.turn_channel(0, 9 + 2)                           # positions A,1..8: 11 % 9 = 2 -> pair 2
        self.assertEqual(s.channel_mask(0), 1 << 1)

    def test_a_report_overwrites_what_core_expected(self):
        s = Stems()
        s.turn_channel(0, +1)                              # Core expects pair 1 on bus 1
        s.report(1, 0)                                     # StemDeck says: nothing
        self.assertEqual(s.channel_mask(0), 0)


class TheReturn(unittest.TestCase):
    def test_the_cursor_skips_stems_on_channels(self):
        s = stems_with(p1=bit(1), p2=bit(2))
        s.return_cursor = 0
        s.turn_return(+1)
        self.assertEqual(s.return_cursor, 3)

    def test_push_toggles_aux_of_the_cursor_stem(self):
        s = stems_with(p3=bit(AUX))
        s.return_cursor = 3
        self.assertEqual(s.push_return(), [(3, AUX, False)])
        self.assertEqual(s.push_return(), [(3, AUX, True)])

    def test_push_with_nothing_free_does_nothing(self):
        s = stems_with(**{f"p{p}": bit(1 + (p - 1) % 4) for p in range(1, 9)})
        s.return_cursor = 0
        self.assertEqual(s.push_return(), [])

    def test_the_cursor_moves_off_a_stem_that_joins_a_channel(self):
        s = Stems()
        s.return_cursor = 1
        s.report(1, bit(1))
        self.assertEqual(s.return_cursor, 2)


class Forgetting(unittest.TestCase):
    def test_forget_clears_every_mask(self):
        s = stems_with(p1=bit(1), p4=bit(AUX))
        s.forget()
        self.assertEqual(s.masks, [0] * 8)


class StateOnDisk(unittest.TestCase):
    def test_only_the_cursor_is_kept(self):
        s = Stems()
        s.return_cursor = 4
        self.assertEqual(s.as_data(), {"return_cursor": 4})
        self.assertEqual(Stems.from_data({"return_cursor": 4}).return_cursor, 4)

    def test_an_old_or_garbled_file_never_raises(self):
        for data in (None, {}, {"return_cursor": "x"}, {"return_cursor": 1e999},
                     {"channel_pair": [1, 0, 0, 0], "return_cursor": 9}):
            Stems.from_data(data)
