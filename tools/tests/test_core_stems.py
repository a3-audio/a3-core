"""The rules of stems on the desk (spec stem-routing-on-the-desk).

Eight stereo pairs, four desk channels. A channel holds one pair or none and
turning skips pairs held elsewhere; the FX return lists the pairs on no
channel and mutes them one by one; releasing a pair unmutes it there.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

from a3_core_stems import Stems  # noqa: E402


class ChannelTurns(unittest.TestCase):
    def test_a_fresh_desk_holds_nothing(self):
        s = Stems()
        self.assertEqual(s.channel_pair, [0, 0, 0, 0])
        self.assertEqual(s.return_muted, [False] * 8)

    def test_one_click_takes_the_first_pair(self):
        s = Stems()
        s.turn_channel(0, +1)
        self.assertEqual(s.channel_pair[0], 1)

    def test_a_pair_held_elsewhere_is_skipped(self):
        s = Stems()
        s.turn_channel(1, +1)          # channel 2 takes pair 1
        s.turn_channel(0, +1)          # channel 1 skips it
        self.assertEqual(s.channel_pair[:2], [2, 1])

    def test_turning_back_past_one_is_none(self):
        s = Stems()
        s.turn_channel(0, +1)
        s.turn_channel(0, -1)
        self.assertEqual(s.channel_pair[0], 0)

    def test_down_from_none_wraps_to_the_last_free_pair(self):
        s = Stems()
        s.turn_channel(1, -1)          # channel 2: none -> 8
        s.turn_channel(0, -1)          # channel 1: none -> 7 (8 is held)
        self.assertEqual(s.channel_pair[:2], [7, 8])

    def test_a_long_turn_wraps_and_skips(self):
        s = Stems()
        s.channel_pair = [0, 3, 0, 0]
        s.turn_channel(0, +13)         # positions none,1,2,4,5,6,7,8: 13 % 8 = 5 -> pair 6
        self.assertEqual(s.channel_pair[0], 6)


class TheReturn(unittest.TestCase):
    def test_it_lists_only_unassigned_pairs(self):
        s = Stems()
        s.channel_pair = [1, 2, 0, 0]
        s.turn_return(+1)
        self.assertEqual(s.return_cursor, 3)

    def test_push_mutes_and_unmutes_the_shown_pair(self):
        s = Stems()
        s.turn_return(+1)
        s.push_return()
        self.assertTrue(s.return_muted[s.return_cursor - 1])
        s.push_return()
        self.assertFalse(s.return_muted[s.return_cursor - 1])

    def test_releasing_a_pair_unmutes_it_on_the_return(self):
        s = Stems()
        s.return_cursor = 1
        s.push_return()                # pair 1 muted on the return
        s.turn_channel(0, +1)          # channel 1 takes pair 1
        s.turn_channel(0, -1)          # and lets it go
        self.assertFalse(s.return_muted[0])

    def test_the_cursor_moves_off_a_pair_that_gets_assigned(self):
        s = Stems()
        s.return_cursor = 1
        s.turn_channel(0, +1)          # pair 1 joins channel 1
        self.assertEqual(s.return_cursor, 2)


class NoFreePair(unittest.TestCase):
    def test_no_free_pair_turn_and_push_do_nothing(self):
        s = Stems(pairs=4)             # a rig with as many pairs as channels
        s.channel_pair = [1, 2, 3, 4]
        s.turn_return(+1)
        s.push_return()
        self.assertEqual(s.return_cursor, 0)
        self.assertEqual(s.return_muted, [False] * 4)


class StateOnDisk(unittest.TestCase):
    def test_it_comes_back_as_it_went(self):
        s = Stems()
        s.turn_channel(2, +2)
        s.turn_return(+1)
        s.push_return()
        self.assertEqual(Stems.from_data(s.as_data()).as_data(), s.as_data())

    def test_a_garbled_entry_is_all_none(self):
        for data in (None, {}, {"channel_pair": "x"}, {"channel_pair": [9, 9, 9, 9]},
                     {"channel_pair": [1, 1, 0, 0]}):
            s = Stems.from_data(data)
            self.assertEqual(s.channel_pair, [0, 0, 0, 0], data)


if __name__ == "__main__":
    unittest.main()
