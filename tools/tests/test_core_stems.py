"""The mirror of StemDeck's bus switches and the desk's rules on it
(spec stemdeck-remote, 2026-10-01). Pair p = deck ceil(p/4), stem
(p-1) % 4 + 1; bus 1-4 the desk channels, 5 AUX, 6 CUE."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

from a3_core_stems import AUX, CUE, RETURN, Stems  # noqa: E402


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


class Turning(unittest.TestCase):
    def test_a_turn_selects_and_switches_nothing(self):
        s = Stems()
        s.turn(0, +1)
        self.assertEqual(s.selected[0], 1)
        self.assertEqual(s.masks, [0] * 8)

    def test_a_turn_wraps_through_a(self):
        s = Stems()
        s.turn(0, -1)
        self.assertEqual(s.selected[0], 8)
        s.turn(0, +1)
        self.assertEqual(s.selected[0], 0)

    def test_a_channel_turns_over_stems_on_the_return(self):
        s = stems_with(p1=bit(2), p2=bit(AUX))
        s.turn(0, +1)
        self.assertEqual(s.selected[0], 2)

    def test_a_channel_skips_stems_on_other_channels(self):
        s = stems_with(p1=bit(2))
        s.turn(0, +1)
        self.assertEqual(s.selected[0], 2)

    def test_the_return_turns_over_stems_on_no_channel(self):
        s = stems_with(p1=bit(1), p2=bit(AUX))
        s.turn(RETURN, +1)
        self.assertEqual(s.selected[RETURN], 3)
        s.selected[RETURN] = 8
        s.turn(RETURN, +1)
        self.assertEqual(s.selected[RETURN], 2)

    def test_the_return_has_no_empty_field(self):
        s = Stems()
        s.turn(RETURN, +1)
        self.assertEqual(s.selected[RETURN], 2)

    def test_nothing_free_leaves_the_return_at_zero(self):
        s = stems_with(**{f"p{p}": bit(1) for p in range(1, 9)})
        s.turn(RETURN, +1)
        self.assertEqual(s.selected[RETURN], 0)
        self.assertEqual(s.push(RETURN), [])


class Pushing(unittest.TestCase):
    def test_a_stem_on_the_return_moves_to_the_channel(self):
        s = stems_with(p2=bit(AUX))
        s.selected[1] = 2
        self.assertEqual(s.push(1), [(2, 2, True), (2, AUX, False)])
        self.assertEqual(s.place_of(2), 1)

    def test_the_return_push_toggles(self):
        s = stems_with(p2=bit(AUX))
        s.selected[RETURN] = 4
        self.assertEqual(s.push(RETURN), [(4, AUX, True)])
        self.assertEqual(s.push(RETURN), [(4, AUX, False)])

    def test_the_return_holds_several(self):
        s = stems_with(p2=bit(AUX))
        s.selected[RETURN] = 4
        s.push(RETURN)
        self.assertTrue(s.plays_on_return(2) and s.plays_on_return(4))

    def test_a_push_loads_the_selected_stem(self):
        s = Stems()
        s.selected[0] = 3
        self.assertEqual(s.push(0), [(3, 1, True), (3, AUX, False)])
        self.assertEqual(s.channel_mask(0), 1 << 2)

    def test_a_push_of_a_releases_every_stem_on_the_bus(self):
        s = stems_with(p3=bit(1))
        s.selected[0] = 0
        self.assertEqual(s.push(0), [(3, 1, False)])
        self.assertEqual(s.channel_mask(0), 0)

    def test_a_push_clears_every_other_stem_on_the_bus(self):
        s = stems_with(p2=bit(1), p5=bit(1))
        s.selected[0] = 5
        commands = s.push(0)
        self.assertIn((2, 1, False), commands)
        self.assertNotIn((5, 1, False), commands)
        self.assertEqual(s.channel_mask(0), 1 << 4)

    def test_pushing_what_is_loaded_changes_nothing(self):
        s = stems_with(p3=bit(1))
        s.selected[0] = 3
        self.assertEqual(s.push(0), [])

class SelectionsFollow(unittest.TestCase):
    def test_a_selection_that_lost_its_stem_moves_on(self):
        s = Stems()
        s.selected[1] = 3                  # channel 2 looks at pair 3
        s.selected[0] = 3
        s.push(0)                          # channel 1 takes it
        self.assertEqual(s.selected[1], 4)

    def test_a_report_moves_a_selection_too(self):
        s = Stems()
        s.selected[1] = 8
        s.report(8, bit(1))                # StemDeck's screen puts pair 8 on channel 1
        self.assertEqual(s.selected[1], 0)  # nothing after 8: back to A


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
        s.cue_commands([True, False, False, False])
        s.selected[0] = 5
        s.push(0)
        self.assertEqual(sorted(s.cue_commands([True, False, False, False])),
                         [(3, CUE, False), (5, CUE, True)])

    def test_nothing_changed_nothing_sent(self):
        s = stems_with(p3=bit(1) | bit(CUE))
        self.assertEqual(s.cue_commands([True, False, False, False]), [])


class StateOnDisk(unittest.TestCase):
    def test_the_selections_are_kept(self):
        s = Stems()
        s.selected = [1, 2, 0, 0, 5]
        self.assertEqual(Stems.from_data(s.as_data()).selected, [1, 2, 0, 0, 5])

    def test_an_old_or_garbled_file_never_raises(self):
        for data in (None, {}, {"selected": "x"}, {"selected": [1, 2]},
                     {"selected": [1e999, 0, 0, 0, 0]}, {"return_cursor": 3}):
            self.assertEqual(Stems.from_data(data).selected, [0] * 5, data)
