"""The mirror of StemDeck's bus switches and the desk's rules on it
(spec stemdeck-remote, 2026-10-01). Pair p = deck ceil(p/4), stem
(p-1) % 4 + 1; bus 1-4 the desk channels, 5 AUX, 6 CUE."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

from a3_core_stems import (TOGGLE, ANALOG_MODE, AUX, CUE, INPUTS, RETURN,  # noqa: E402
                           STEM_MODE, Stems)


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


class TheSelector(unittest.TestCase):
    """Each channel encoder is an input selector (2026-10-04): one cursor
    over nine inputs -- D1's stems, D2's stems, the STEM toggle -- and a
    push on a stem makes it the channel's only input."""

    def test_nine_inputs_the_last_is_the_toggle(self):
        self.assertEqual((INPUTS, TOGGLE), (9, 8))

    def test_the_cursor_starts_on_the_toggle(self):
        self.assertEqual(Stems().cursors, [TOGGLE] * 4)

    def test_the_cursor_runs_over_all_nine_and_stops_at_the_ends(self):
        s = Stems()
        s.turn(0, -1)
        self.assertEqual(s.cursors[0], 7)
        s.turn(0, -20)
        self.assertEqual(s.cursors[0], 0)
        s.turn(0, +3)
        self.assertEqual(s.cursors[0], 3)
        s.turn(0, +20)
        self.assertEqual(s.cursors[0], TOGGLE)

    def test_turning_switches_nothing(self):
        s = stems_with(p3=bit(1))
        s.turn(0, -5)
        self.assertEqual(s.channel_mask(0), 1 << 2)

    def test_push_on_a_stem_makes_it_the_only_input(self):
        s = stems_with(p2=bit(1), p6=bit(1))
        s.return_mode = ANALOG_MODE
        s.cursors[0] = 2                                   # D1, stem 3 = pair 3
        commands = s.push(0)
        self.assertEqual(commands[:2], [(3, 1, True), (3, AUX, False)])
        self.assertIn((2, 1, False), commands)
        self.assertIn((6, 1, False), commands)
        self.assertEqual(s.channel_mask(0), 1 << 2)

    def test_deck_2_stems_are_inputs_4_to_7(self):
        s = Stems()
        s.return_mode = ANALOG_MODE
        s.cursors[1] = 4
        s.push(1)
        self.assertEqual(s.place_of(5), 1)

    def test_push_on_what_plays_changes_nothing(self):
        s = stems_with(p3=bit(1))
        s.return_mode = ANALOG_MODE
        s.cursors[0] = 2
        self.assertEqual(s.push(0), [])
        self.assertEqual(s.channel_mask(0), 1 << 2)

    def test_a_stem_on_another_channel_moves_here(self):
        s = stems_with(p3=bit(2))
        s.return_mode = ANALOG_MODE
        s.cursors[0] = 2
        commands = s.push(0)
        self.assertIn((3, 2, False), commands)
        self.assertIn((3, 1, True), commands)
        self.assertEqual((s.place_of(3), s.channel_mask(1)), (0, 0))

    def test_toggle_off_releases_every_stem(self):
        s = stems_with(p3=bit(1))
        s.return_mode = ANALOG_MODE
        s.cursors[0] = TOGGLE
        self.assertEqual(s.push(0), [(3, 1, False)])
        self.assertEqual(s.channel_mask(0), 0)

    def test_toggle_without_memory_while_analog_plays_changes_nothing(self):
        s = Stems()
        s.return_mode = ANALOG_MODE
        self.assertEqual(s.push(0), [])

    def test_a_report_does_not_move_the_cursor(self):
        s = Stems()
        s.cursors[0] = 5
        s.report(2, bit(1))
        self.assertEqual(s.cursors[0], 5)


class TheStemToggle(unittest.TestCase):
    """Position 8 is a STEM on/off toggle (2026-10-04): off releases every
    stem and remembers the one that played; on brings the remembered stem
    back as if it were pushed on its own position."""

    def toggle(self, s, index=0, connected=True):
        s.cursors[index] = TOGGLE
        return s.push(index, connected=connected)

    def select(self, s, index, pair):
        s.cursors[index] = pair - 1
        return s.push(index)

    def test_off_releases_the_stem_and_remembers_it(self):
        s = stems_with(p3=bit(1))
        s.return_mode = ANALOG_MODE
        self.assertEqual(self.toggle(s), [(3, 1, False)])
        self.assertEqual((s.channel_mask(0), s.last_stems[0]), (0, 3))

    def test_on_restores_the_remembered_stem(self):
        s = stems_with(p3=bit(1))
        s.return_mode = ANALOG_MODE
        self.toggle(s)
        commands = self.toggle(s)
        self.assertEqual(commands[:2], [(3, 1, True), (3, AUX, False)])
        self.assertEqual(s.channel_mask(0), 1 << 2)
        self.assertEqual(s.last_stems[0], 3)

    def test_on_takes_the_stem_off_the_return(self):
        s = stems_with(p3=bit(1))
        s.tidy()
        self.toggle(s)
        self.assertTrue(s.plays_on_return(3))
        self.assertIn((3, AUX, False), self.toggle(s))
        self.assertFalse(s.plays_on_return(3))

    def test_on_moves_the_stem_back_from_another_channel(self):
        s = stems_with(p3=bit(1))
        s.return_mode = ANALOG_MODE
        self.toggle(s)                                   # channel 1 remembers 3
        self.select(s, 1, 3)                             # channel 2 takes it
        self.assertEqual(s.last_stems, [3, 3, None, None])
        commands = self.toggle(s)
        self.assertIn((3, 2, False), commands)
        self.assertEqual((s.place_of(3), s.channel_mask(1)), (0, 0))

    def test_a_stem_taken_away_while_on_keeps_the_memory(self):
        s = Stems()
        s.return_mode = ANALOG_MODE
        self.select(s, 0, 3)
        self.select(s, 1, 3)                             # moves away from channel 1
        self.assertEqual(s.last_stems[0], 3)
        self.toggle(s)                                   # nothing plays: on
        self.assertEqual(s.place_of(3), 0)

    def test_without_memory_on_does_nothing(self):
        s = Stems()
        self.assertEqual(self.toggle(s), [])
        self.assertEqual((s.channel_mask(0), s.last_stems[0]), (0, None))

    def test_selecting_a_stem_sets_the_memory(self):
        s = Stems()
        s.return_mode = ANALOG_MODE
        self.select(s, 2, 6)
        self.assertEqual(s.last_stems, [None, None, 6, None])
        self.select(s, 2, 1)
        self.assertEqual(s.last_stems[2], 1)

    def test_a_push_on_what_plays_also_sets_the_memory(self):
        s = stems_with(p4=bit(1))                        # put there by StemDeck
        s.return_mode = ANALOG_MODE
        self.assertEqual(self.select(s, 0, 4), [])
        self.assertEqual(s.last_stems[0], 4)

    def test_off_remembers_a_stem_stemdeck_put_there(self):
        s = stems_with(p5=bit(2))
        s.return_mode = ANALOG_MODE
        self.toggle(s, 1)
        self.assertEqual(s.last_stems[1], 5)

    def test_stemdeck_removing_the_stem_keeps_the_memory(self):
        s = Stems()
        s.return_mode = ANALOG_MODE
        self.select(s, 0, 3)
        s.report(3, 0)
        s.tidy()
        self.assertEqual(s.last_stems[0], 3)
        self.toggle(s)
        self.assertEqual(s.place_of(3), 0)

    def test_stemdeck_gone_keeps_the_memory(self):
        s = Stems()
        self.select(s, 0, 3)
        s.forget()
        self.assertEqual(s.last_stems[0], 3)

    def test_disconnected_off_switches_nothing_and_remembers_nothing(self):
        s = stems_with(p3=bit(1))
        self.assertEqual(self.toggle(s, connected=False), [])
        self.assertEqual((s.channel_mask(0), s.last_stems[0]), (1 << 2, None))

    def test_disconnected_on_switches_nothing(self):
        s = Stems()
        s.last_stems[0] = 3
        self.assertEqual(self.toggle(s, connected=False), [])
        self.assertEqual(s.place_of(3), None)

    def test_disconnected_select_leaves_the_memory(self):
        s = Stems()
        s.last_stems[0] = 3
        s.cursors[0] = 5
        s.push(0, connected=False)
        self.assertEqual(s.last_stems[0], 3)


class WithoutStemDeck(unittest.TestCase):
    """Final review: without StemDeck a push loaded the mirror anyway, and
    Core shut the channel's analog send for a stem nobody plays."""

    def test_the_cursor_moves_the_switches_do_not(self):
        s = Stems()
        s.turn(0, -6)
        self.assertEqual(s.push(0, connected=False), [])
        self.assertEqual((s.cursors[0], s.channel_mask(0)), (2, 0))

    def test_the_toggle_does_not_touch_the_mirror_either(self):
        s = stems_with(p3=bit(1))
        self.assertEqual(s.push(0, connected=False), [])
        self.assertEqual(s.channel_mask(0), 1 << 2)

    def test_the_return_mode_switches_without_commands(self):
        s = stems_with(p2=bit(AUX))
        s.return_cursor = ANALOG_MODE
        self.assertEqual(s.push(RETURN, connected=False), [])
        self.assertEqual(s.return_mode, ANALOG_MODE)
        self.assertTrue(s.plays_on_return(2))


class OnePerChannel(unittest.TestCase):
    def test_a_stem_on_two_channels_keeps_the_one_it_alone_obeys(self):
        """Final review: pair 3 on channels 1 and 2, pair 1 on channel 1 --
        pair 3 leaves channel 1 (pair 1 is lower) and must stay on 2."""
        s = stems_with(p1=bit(1), p3=bit(1) | bit(2))
        s.return_mode = ANALOG_MODE
        commands = s.tidy()
        self.assertEqual(s.channel_mask(0), 1)
        self.assertEqual(s.channel_mask(1), 1 << 2)
        self.assertEqual(len(commands), len(set(commands)))

    def test_two_stems_on_a_bus_keep_the_lowest_whatever_their_deck(self):
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
        s.cursors[0] = TOGGLE
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

    def test_a_channel_on_analog_cues_nothing_in_stemdeck(self):
        s = stems_with(p3=bit(CUE))                       # clicked on StemDeck's screen
        self.assertEqual(s.cue_commands([True, False, False, False]), [(3, CUE, False)])

    def test_the_cue_moves_with_the_push(self):
        s = stems_with(p3=bit(1))
        s.return_mode = ANALOG_MODE
        s.cue_commands([True, False, False, False])
        s.cursors[0] = 3                              # stem 4 replaces 3
        s.push(0)
        self.assertEqual(sorted(s.cue_commands([True, False, False, False])),
                         [(3, CUE, False), (4, CUE, True)])

    def test_a_deck_2_stem_is_cued_in_place_of_deck_1s(self):
        s = stems_with(p3=bit(1))
        s.return_mode = ANALOG_MODE
        s.cue_commands([True, False, False, False])
        s.cursors[0] = 4
        s.push(0)
        self.assertEqual(sorted(s.cue_commands([True, False, False, False])),
                         [(3, CUE, False), (5, CUE, True)])

    def test_nothing_changed_nothing_sent(self):
        s = stems_with(p3=bit(1) | bit(CUE))
        self.assertEqual(s.cue_commands([True, False, False, False]), [])


class StateOnDisk(unittest.TestCase):
    def test_cursors_and_mode_are_kept(self):
        s = Stems()
        s.cursors = [TOGGLE, 3, 0, 7]
        s.return_cursor, s.return_mode = ANALOG_MODE, ANALOG_MODE
        back = Stems.from_data(s.as_data())
        self.assertEqual(back.cursors, s.cursors)
        self.assertEqual((back.return_cursor, back.return_mode), (ANALOG_MODE, ANALOG_MODE))

    def test_the_last_stems_are_kept(self):
        s = Stems()
        s.last_stems = [3, None, 8, 1]
        self.assertEqual(Stems.from_data(s.as_data()).last_stems, [3, None, 8, 1])
        self.assertEqual(s.as_data()["last_stems"], [3, None, 8, 1])

    def test_garbled_last_stems_mean_no_memory(self):
        for value in (None, "x", [3, 3, 3], [0, None, None, None],
                      [9, None, None, None], [True, None, None, None],
                      [1.0, None, None, None], {"0": 3}):
            s = Stems.from_data({"last_stems": value, "cursors": [1, 2, 3, 4]})
            self.assertEqual(s.last_stems, [None] * 4, value)
            self.assertEqual(s.cursors, [1, 2, 3, 4], value)

    def test_an_old_or_garbled_file_never_raises(self):
        for data in (None, {}, {"menus": [[0, 0]] * 4}, {"cursors": "x"},
                     {"cursors": [9, 0, 0, 0]}, {"cursors": [0, 0, 0]},
                     {"cursors": [True, 0, 0, 0]}, {"return_mode": 7},
                     {"cursors": [1e999, 0, 0, 0]}):
            s = Stems.from_data(data)
            self.assertEqual(s.cursors, [TOGGLE] * 4, data)
            self.assertEqual(s.return_mode, STEM_MODE, data)
            self.assertEqual(s.last_stems, [None] * 4, data)
