"""The mirror of StemDeck's bus switches and the desk's rules on it
(spec stemdeck-remote, 2026-10-01). Pair p = deck ceil(p/4), stem
(p-1) % 4 + 1; bus 1-4 the desk channels, 5 AUX, 6 CUE."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

from a3_core_stems import (A, ANALOG_MODE, AUX, BACK, CUE, D1, D2, DECK_1, DECK_2,  # noqa: E402
                           RETURN, STEM_MODE, TOP, Stems)


def bit(bus):
    return 1 << (bus - 1)


def stems_with(**masks):
    s = Stems()
    for name, mask in masks.items():
        s.report(int(name[1:]), mask)
    return s


class TheMirror(unittest.TestCase):
    def test_a_report_sets_the_mask(self):
        s = stems_with(p3=bit(2))
        self.assertEqual(s.channel_mask(1), 1 << 2)

    def test_a_damaged_report_changes_nothing(self):
        s = Stems()
        for pair, mask in ((0, 1), (9, 1), (1, 64), (1, -1), ("1", 1), (1, 1.5), (True, 1)):
            self.assertFalse(s.report(pair, mask), (pair, mask))
        self.assertEqual(s.masks, [0] * 8)

    def test_a_stem_has_one_place(self):
        s = stems_with(p1=bit(1), p2=bit(AUX))
        self.assertEqual((s.place_of(1), s.place_of(2), s.place_of(3)), (0, RETURN, None))


class TheMenu(unittest.TestCase):
    """Each channel encoder is a two-level menu (spec desk-stem-grid-2,
    2026-10-02): D1 / D2 / A, then a deck's stems 1-4 and back."""

    def test_turning_at_the_top_runs_d1_d2_a(self):
        s = Stems()
        self.assertEqual(s.menus[0], (TOP, D1))
        s.turn(0, +1)
        self.assertEqual(s.menus[0], (TOP, D2))
        s.turn(0, +1)
        self.assertEqual(s.menus[0], (TOP, A))
        s.turn(0, +1)
        self.assertEqual(s.menus[0], (TOP, D1))

    def test_push_on_a_deck_enters_and_switches_nothing(self):
        s = stems_with(p2=bit(1))
        s.menus[0] = (TOP, D2)
        self.assertEqual(s.push(0), [])
        self.assertEqual(s.menus[0], (DECK_2, 0))
        self.assertEqual(s.channel_mask(0), 1 << 1)

    def test_turning_in_a_deck_runs_four_stems_and_back(self):
        s = Stems()
        s.menus[0] = (DECK_1, 0)
        s.turn(0, -1)
        self.assertEqual(s.menus[0], (DECK_1, BACK))
        s.turn(0, +2)
        self.assertEqual(s.menus[0], (DECK_1, 1))

    def test_push_on_a_stem_loads_it_as_the_only_one(self):
        s = stems_with(p2=bit(1))
        s.return_mode = ANALOG_MODE
        s.menus[0] = (DECK_1, 2)                      # deck 1, stem 3 = pair 3
        commands = s.push(0)
        self.assertEqual(commands[:2], [(3, 1, True), (3, AUX, False)])
        self.assertIn((2, 1, False), commands)
        self.assertEqual(s.channel_mask(0), 1 << 2)

    def test_deck_2_stems_are_pairs_5_to_8(self):
        s = Stems()
        s.return_mode = ANALOG_MODE
        s.menus[1] = (DECK_2, 0)
        s.push(1)
        self.assertEqual(s.place_of(5), 1)

    def test_a_stem_on_another_channel_does_nothing(self):
        s = stems_with(p3=bit(2))
        s.menus[0] = (DECK_1, 2)
        self.assertEqual(s.push(0), [])
        self.assertEqual(s.place_of(3), 1)

    def test_back_returns_to_the_top_on_that_deck(self):
        s = Stems()
        s.menus[0] = (DECK_2, BACK)
        self.assertEqual(s.push(0), [])
        self.assertEqual(s.menus[0], (TOP, D2))

    def test_push_on_a_releases_every_stem(self):
        s = stems_with(p3=bit(1))
        s.return_mode = ANALOG_MODE
        s.menus[0] = (TOP, A)
        self.assertEqual(s.push(0), [(3, 1, False)])
        self.assertEqual(s.channel_mask(0), 0)

    def test_source_marks_the_deck_or_a(self):
        s = stems_with(p6=bit(2))
        self.assertEqual((s.source(0), s.source(1)), (A, D2))


class OnePerChannel(unittest.TestCase):
    def test_two_stems_on_a_bus_keep_the_lowest(self):
        s = stems_with(p6=bit(1), p3=bit(1))
        s.return_mode = ANALOG_MODE
        self.assertEqual(s.tidy(), [(6, 1, False)])
        self.assertEqual(s.channel_mask(0), 1 << 2)

    def test_tidy_twice_is_nothing(self):
        s = stems_with(p3=bit(1), p6=bit(1), p4=bit(4), p8=bit(4))
        s.tidy()
        self.assertEqual(s.tidy(), [])


class ReturnModes(unittest.TestCase):
    def test_stem_mode_puts_every_free_stem_on_aux(self):
        s = stems_with(p1=bit(1))
        commands = s.tidy()
        self.assertEqual(sorted(p for p, b, on in commands if b == AUX and on),
                         list(range(2, 9)))
        self.assertFalse(s.plays_on_return(1))

    def test_a_stem_on_a_channel_leaves_aux(self):
        s = stems_with(p1=bit(1) | bit(AUX))
        self.assertIn((1, AUX, False), s.tidy())

    def test_analog_mode_takes_only_aux_off(self):
        s = stems_with(p1=bit(1) | bit(AUX), p2=bit(AUX))
        s.return_mode = ANALOG_MODE
        self.assertEqual(sorted(s.tidy()), [(1, AUX, False), (2, AUX, False)])
        self.assertEqual(s.channel_mask(0), 1)

    def test_the_encoder_knows_two_options(self):
        s = Stems()
        self.assertEqual(s.return_cursor, STEM_MODE)
        s.turn(RETURN, +1)
        self.assertEqual(s.return_cursor, ANALOG_MODE)
        s.turn(RETURN, +1)
        self.assertEqual(s.return_cursor, STEM_MODE)

    def test_push_switches_the_mode(self):
        s = stems_with(p2=bit(AUX))
        s.return_cursor = ANALOG_MODE
        self.assertEqual(s.push(RETURN), [(2, AUX, False)])
        self.assertEqual(s.return_mode, ANALOG_MODE)

    def test_a_released_stem_returns_to_aux_in_stem_mode(self):
        s = stems_with(p3=bit(1))
        s.tidy()
        s.menus[0] = (TOP, A)
        self.assertIn((3, AUX, True), s.push(0))

    def test_analog_to_stem_puts_only_the_free_stems_on_aux(self):
        s = stems_with(**{f"p{p}": bit(p) for p in range(1, 5)})   # 1-4 on channels 1-4
        s.return_mode = ANALOG_MODE
        s.tidy()
        s.return_cursor = STEM_MODE
        self.assertEqual(sorted(c for c in s.push(RETURN)),
                         [(p, AUX, True) for p in range(5, 9)])


class TheCue(unittest.TestCase):
    """A channel's cue plays its stem through StemDeck's C (spec
    desk-stem-selector); Core overrides C clicks on StemDeck's screen."""

    def test_a_cued_channel_cues_its_stem(self):
        s = stems_with(p3=bit(1))
        self.assertEqual(s.cue_commands([True, False, False, False]), [(3, CUE, True)])

    def test_a_channel_on_a_cues_nothing_in_stemdeck(self):
        s = stems_with(p3=bit(CUE))                       # clicked on StemDeck's screen
        self.assertEqual(s.cue_commands([True, False, False, False]), [(3, CUE, False)])

    def test_the_cue_moves_with_the_push(self):
        s = stems_with(p3=bit(1))
        s.return_mode = ANALOG_MODE
        s.cue_commands([True, False, False, False])
        s.menus[0] = (DECK_2, 0)
        s.push(0)
        self.assertEqual(sorted(s.cue_commands([True, False, False, False])),
                         [(3, CUE, False), (5, CUE, True)])

    def test_nothing_changed_nothing_sent(self):
        s = stems_with(p3=bit(1) | bit(CUE))
        self.assertEqual(s.cue_commands([True, False, False, False]), [])


class StateOnDisk(unittest.TestCase):
    def test_menus_and_mode_are_kept(self):
        s = Stems()
        s.menus = [(TOP, A), (DECK_1, 3), (DECK_2, BACK), (TOP, D2)]
        s.return_cursor, s.return_mode = ANALOG_MODE, ANALOG_MODE
        back = Stems.from_data(s.as_data())
        self.assertEqual(back.menus, s.menus)
        self.assertEqual((back.return_cursor, back.return_mode), (ANALOG_MODE, ANALOG_MODE))

    def test_an_old_or_garbled_file_never_raises(self):
        for data in (None, {}, {"selected": [1, 2, 0, 0, 5]}, {"menus": "x"},
                     {"menus": [[9, 9]] * 4}, {"menus": [[0, 0]] * 3},
                     {"return_mode": 7}, {"menus": [[1e999, 0]] * 4}):
            s = Stems.from_data(data)
            self.assertEqual(s.menus, [(TOP, D1)] * 4, data)
            self.assertEqual(s.return_mode, STEM_MODE, data)
