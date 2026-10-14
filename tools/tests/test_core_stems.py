"""The mirror of StemDeck's bus switches and the desk's rules on it
(spec stemdeck-remote, 2026-10-01). Pair p = deck ceil(p/4), stem
(p-1) % 4 + 1; bus 1-4 the desk channels, 5 AUX."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

from a3_core_stems import (ANALOG_INPUT, ANALOG_MODE, ANALOG_SOURCE, AUX,  # noqa: E402
                           CHANNELS, CUE_FIELD, INPUTS, PAIRS, RETURN,
                           STEM_MODE, STEM_SOURCE, Stems)

MODES = (ANALOG_MODE, STEM_MODE)


def bit(bus):
    return 1 << (bus - 1)


def stems_with(**masks):
    s = Stems()
    for name, mask in masks.items():
        s.report(int(name[1:]), mask)
    return s


def tidied(mode=STEM_MODE, **masks):
    """A mirror that already obeys the rules, as StemDeck leaves it."""
    s = stems_with(**masks)
    s.return_mode = mode
    s.tidy()
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
    over nine inputs -- D1's stems, D2's stems, A, the analog input -- and a
    push on a stem makes it the channel's only input."""

    def test_nine_inputs_the_last_is_a(self):
        self.assertEqual((INPUTS, ANALOG_INPUT), (9, 8))

    def test_the_cursor_starts_on_a(self):
        self.assertEqual(Stems().cursors, [ANALOG_INPUT] * 4)

    def test_the_cursor_runs_over_all_nine_and_wraps_around(self):
        # Left from stem 1 lands on A, right from A
        # on stem 1 (maintainer, 2026-10-04): A is one turn away.
        s = Stems()
        s.turn(0, -1)
        self.assertEqual(s.cursors[0], 7)
        s.cursors[0] = 0
        s.turn(0, -1)
        self.assertEqual(s.cursors[0], ANALOG_INPUT)
        s.turn(0, +1)
        self.assertEqual(s.cursors[0], 0)
        s.turn(0, +3)
        self.assertEqual(s.cursors[0], 3)
        s.turn(0, -5)
        self.assertEqual(s.cursors[0], 7)

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
        self.assertEqual(s.sources[0], STEM_SOURCE)

    def test_deck_2_stems_are_inputs_4_to_7(self):
        s = Stems()
        s.return_mode = ANALOG_MODE
        s.cursors[1] = 4
        s.push(1)
        self.assertEqual(s.place_of(5), 1)

    def test_push_on_what_plays_changes_nothing(self):
        # Until 2026-10-14 it released the stem to AUX; analog is A's now,
        # and A leaves StemDeck alone.
        s = tidied(STEM_MODE, p3=bit(1))
        s.sources[0] = STEM_SOURCE
        s.cursors[0] = 2
        self.assertEqual(s.push(0), [])
        self.assertEqual((s.channel_mask(0), s.sources[0]), (1 << 2, STEM_SOURCE))

    def test_push_on_the_stem_on_the_bus_while_analog_plays_hears_it(self):
        # The stem stayed on bus 1 while A played: a push only opens the
        # stems send, StemDeck has nothing to do.
        s = tidied(STEM_MODE, p3=bit(1))
        s.cursors[0] = 2
        self.assertEqual(s.push(0), [])
        self.assertEqual(s.sources[0], STEM_SOURCE)

    def test_a_stem_on_another_channel_moves_here(self):
        s = stems_with(p3=bit(2))
        s.return_mode = ANALOG_MODE
        s.cursors[0] = 2
        commands = s.push(0)
        self.assertIn((3, 2, False), commands)
        self.assertIn((3, 1, True), commands)
        self.assertEqual((s.place_of(3), s.channel_mask(1)), (0, 0))

    def test_a_report_does_not_move_the_cursor(self):
        s = Stems()
        s.cursors[0] = 5
        s.report(2, bit(1))
        self.assertEqual(s.cursors[0], 5)


class TheAnalogInput(unittest.TestCase):
    """Position 8 is A, the channel's analog input. Since 2026-10-14 A
    switches in REAPER only (maintainer: "A should switch to the analog
    input in REAPER, not touch StemDeck"): the channel's source becomes
    ANALOG, and whatever StemDeck has on its bus stays there, unheard."""

    def push_a(self, s, index=0, connected=True):
        s.cursors[index] = ANALOG_INPUT
        return s.push(index, connected=connected)

    def select(self, s, index, pair):
        s.cursors[index] = pair - 1
        return s.push(index)

    def test_every_channel_starts_on_analog(self):
        self.assertEqual(Stems().sources, [ANALOG_SOURCE] * CHANNELS)

    def test_a_push_on_a_sends_nothing_to_stemdeck(self):
        for mode in MODES:
            s = tidied(mode, p3=bit(1))
            s.sources[0] = STEM_SOURCE
            masks = list(s.masks)
            self.assertEqual(self.push_a(s), [], mode)
            self.assertEqual(s.masks, masks, mode)
            self.assertEqual(s.sources[0], ANALOG_SOURCE, mode)

    def test_the_stem_stays_on_its_bus_and_off_aux(self):
        s = tidied(STEM_MODE, p3=bit(1))
        self.select(s, 0, 3)
        self.push_a(s)
        self.assertEqual((s.place_of(3), s.plays_on_return(3)), (0, False))

    def test_a_switches_without_stemdeck_too(self):
        # REAPER's sends are Core's; StemDeck is not asked.
        s = stems_with(p3=bit(1))
        s.sources[0] = STEM_SOURCE
        self.assertEqual(self.push_a(s, connected=False), [])
        self.assertEqual(s.sources[0], ANALOG_SOURCE)

    def test_a_on_one_channel_leaves_the_others_alone(self):
        s = tidied(ANALOG_MODE, p3=bit(1), p6=bit(2))
        s.sources = [STEM_SOURCE] * CHANNELS
        self.push_a(s)
        self.assertEqual(s.sources, [ANALOG_SOURCE] + [STEM_SOURCE] * 3)
        self.assertEqual(s.place_of(6), 1)

    def test_a_stem_after_a_is_heard_again(self):
        s = Stems()
        s.return_mode = ANALOG_MODE
        self.select(s, 0, 3)
        self.push_a(s)
        self.assertEqual(self.select(s, 0, 3), [])
        self.assertEqual((s.sources[0], s.place_of(3)), (STEM_SOURCE, 0))

    def test_a_stem_moved_here_leaves_its_old_channel_its_source(self):
        s = tidied(ANALOG_MODE, p3=bit(2))
        s.sources[1] = STEM_SOURCE
        self.select(s, 0, 3)
        self.assertEqual(s.sources[:2], [STEM_SOURCE, STEM_SOURCE])
        self.assertEqual((s.place_of(3), s.channel_mask(1)), (0, 0))

    def test_stemdecks_reports_never_change_the_source(self):
        # Auto-DJ or a click in StemDeck moves stems on and off bus N;
        # channel N keeps what the desk chose.
        s = Stems()
        s.sources = [STEM_SOURCE, ANALOG_SOURCE, STEM_SOURCE, ANALOG_SOURCE]
        s.report(1, bit(2))
        s.report(2, bit(3))
        s.tidy()
        s.report(1, 0)
        s.forget()
        self.assertEqual(s.sources, [STEM_SOURCE, ANALOG_SOURCE, STEM_SOURCE, ANALOG_SOURCE])

    def test_no_stem_is_remembered(self):
        s = Stems()
        self.select(s, 0, 3)
        self.assertFalse(hasattr(s, "last_stems"))
        self.assertNotIn("last_stems", s.as_data())


class WithoutStemDeck(unittest.TestCase):
    """Final review: without StemDeck a push loaded the mirror anyway, and
    Core shut the channel's analog send for a stem nobody plays."""

    def test_the_cursor_moves_the_switches_do_not(self):
        s = Stems()
        s.turn(0, -6)
        self.assertEqual(s.push(0, connected=False), [])
        self.assertEqual((s.cursors[0], s.channel_mask(0)), (2, 0))

    def test_a_stem_nobody_plays_does_not_shut_the_analog_input(self):
        s = Stems()
        s.cursors[0] = 2
        s.push(0, connected=False)
        self.assertEqual(s.sources[0], ANALOG_SOURCE)

    def test_a_does_not_touch_the_mirror_either(self):
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
        commands = s.tidy()
        self.assertEqual([c for c in commands if c[1] != AUX], [(6, 1, False)])
        self.assertIn((6, AUX, True), commands)
        self.assertEqual(s.channel_mask(0), 1 << 2)

    def test_tidy_twice_is_nothing(self):
        s = stems_with(p3=bit(1), p6=bit(1), p4=bit(4), p8=bit(4))
        s.tidy()
        self.assertEqual(s.tidy(), [])


class ReturnModes(unittest.TestCase):
    def test_a_stem_on_no_bus_stays_off(self):
        # StemDeck's Auto-DJ leaves the incoming deck's stems on no bus at all
        # until their switch; putting them on AUX would play them early.
        s = stems_with(p1=bit(1))
        self.assertEqual(s.tidy(), [])
        self.assertEqual(s.masks[1:], [0] * 7)
        self.assertFalse(s.plays_on_return(1))

    def test_a_stem_on_a_channel_leaves_aux(self):
        s = stems_with(p1=bit(1) | bit(AUX))
        self.assertIn((1, AUX, False), s.tidy())

    def test_analog_mode_takes_a_stem_on_a_channel_off_aux_too(self):
        # One stem per bus whatever the mode: Core switches the return's
        # source in REAPER, so AUX is silent on the return in ANALOG mode.
        s = stems_with(p1=bit(1) | bit(AUX), p2=bit(AUX))
        s.return_mode = ANALOG_MODE
        self.assertEqual(sorted(s.tidy()), [(1, AUX, False)])
        self.assertEqual(s.channel_mask(0), 1)
        self.assertTrue(s.plays_on_return(2))

    def test_in_both_modes_the_rules_hold_and_an_off_stem_stays_off(self):
        import random
        rng = random.Random(20261007)
        for mode in MODES:
            for _ in range(300):
                s = stems_with(**{f"p{p}": rng.randrange(1 << AUX)
                                  for p in range(1, PAIRS + 1)})
                s.return_mode = mode
                off = [p for p in range(1, PAIRS + 1) if s.masks[p - 1] == 0]
                s.tidy()
                for index in range(CHANNELS):
                    self.assertLessEqual(bin(s.channel_mask(index)).count("1"), 1)
                for pair in range(1, PAIRS + 1):
                    channels = [c for c in range(CHANNELS) if s.channel_mask(c) >> (pair - 1) & 1]
                    self.assertLessEqual(len(channels), 1)
                    if channels:
                        self.assertFalse(s.plays_on_return(pair), (mode, s.masks))
                for pair in off:
                    self.assertEqual(s.masks[pair - 1], 0, (mode, pair))
                self.assertEqual(s.tidy(), [])

    def test_the_encoder_runs_a_ring_of_three(self):
        # The display's order, left to right: STEM, ANALOG, CUE, and round
        # again (spec return-cue, 2026-10-04).
        s = Stems()
        self.assertEqual(s.return_cursor, STEM_MODE)
        s.turn(RETURN, +1)
        self.assertEqual(s.return_cursor, ANALOG_MODE)
        s.turn(RETURN, +1)
        self.assertEqual(s.return_cursor, CUE_FIELD)
        s.turn(RETURN, +1)
        self.assertEqual(s.return_cursor, STEM_MODE)

    def test_left_from_stem_is_cue(self):
        s = Stems()
        s.turn(RETURN, -1)
        self.assertEqual(s.return_cursor, CUE_FIELD)

    def test_a_fast_turn_counts_every_click(self):
        s = Stems()
        s.turn(RETURN, +2)
        self.assertEqual(s.return_cursor, CUE_FIELD)
        s.turn(RETURN, -4)
        self.assertEqual(s.return_cursor, ANALOG_MODE)

    def test_push_on_cue_switches_nothing_and_keeps_the_mode(self):
        s = stems_with(p1=bit(1), p2=bit(AUX))
        s.return_mode = ANALOG_MODE
        s.return_cursor = CUE_FIELD
        masks = list(s.masks)
        self.assertEqual(s.push(RETURN), [])
        self.assertEqual(s.return_mode, ANALOG_MODE)
        self.assertEqual(s.masks, masks)

    def test_push_switches_the_mode_and_no_stem(self):
        # The mode chooses the return's source in REAPER; StemDeck's
        # switches stay as they are.
        for start, target in ((STEM_MODE, ANALOG_MODE), (ANALOG_MODE, STEM_MODE)):
            s = tidied(start, p1=bit(1), p6=bit(3))
            masks = list(s.masks)
            s.return_cursor = target
            self.assertEqual(s.push(RETURN), [])
            self.assertEqual((s.return_mode, s.masks), (target, masks))

    def test_a_puts_nothing_on_aux(self):
        # Until 2026-10-14 A released the stem onto AUX.
        s = tidied(STEM_MODE, p3=bit(1))
        s.cursors[0] = ANALOG_INPUT
        self.assertEqual(s.push(0), [])
        self.assertFalse(s.plays_on_return(3))

    def test_analog_mode_leaves_the_stems_on_no_bus_off(self):
        s = stems_with(**{f"p{p}": bit(p) for p in range(1, 5)})   # 1-4 on channels 1-4
        s.return_mode = ANALOG_MODE
        self.assertEqual(s.tidy(), [])
        s.return_cursor = STEM_MODE
        self.assertEqual(s.push(RETURN), [])

    def test_a_stem_a_push_takes_off_its_channel_goes_to_aux(self):
        s = stems_with(p3=bit(1), p5=bit(AUX))
        s.cursors[0] = 4                    # pair 5 onto channel 1
        commands = s.push(0)
        self.assertIn((3, 1, False), commands)
        self.assertIn((3, AUX, True), commands)
        self.assertTrue(s.plays_on_return(3))


class TheCueLeavesStemDeckAlone(unittest.TestCase):
    """The channel cue goes through the channel bus (2026-10-06); StemDeck's
    C switches are StemDeck's own pre-listening again, Core sets none."""

    def test_the_mirror_has_no_cue_commands(self):
        self.assertFalse(hasattr(Stems(), "cue_commands"))

    def test_there_is_no_c_switch_to_clear(self):
        # StemDeck's C switches are gone (2026-10-07).
        self.assertFalse(hasattr(Stems(), "all_cue_off"))

    def test_an_older_stemdecks_c_bit_is_not_a_bus(self):
        # A StemDeck from before 2026-10-07 may still report its C (bit 5):
        # the stem is taken at its word on buses 1-5 and the C is dropped,
        # not the whole report.
        s = Stems()
        self.assertTrue(s.report(1, bit(1) | (1 << 5)))
        self.assertEqual(s.masks[0], bit(1))


class StateOnDisk(unittest.TestCase):
    def test_the_sources_are_kept(self):
        s = Stems()
        s.sources = [STEM_SOURCE, ANALOG_SOURCE, STEM_SOURCE, STEM_SOURCE]
        self.assertEqual(Stems.from_data(s.as_data()).sources, s.sources)

    def test_a_file_without_sources_starts_on_analog(self):
        self.assertEqual(Stems.from_data({"cursors": [1, 2, 3, 4]}).sources,
                         [ANALOG_SOURCE] * CHANNELS)

    def test_garbled_sources_start_on_analog(self):
        for sources in ("x", [1, 1, 1], [1, 1, 1, 2], [True, 1, 1, 1], None):
            self.assertEqual(Stems.from_data({"sources": sources}).sources,
                             [ANALOG_SOURCE] * CHANNELS, sources)

    def test_cursors_and_mode_are_kept(self):
        s = Stems()
        s.cursors = [ANALOG_INPUT, 3, 0, 7]
        s.return_cursor, s.return_mode = ANALOG_MODE, ANALOG_MODE
        back = Stems.from_data(s.as_data())
        self.assertEqual(back.cursors, s.cursors)
        self.assertEqual((back.return_cursor, back.return_mode), (ANALOG_MODE, ANALOG_MODE))

    def test_the_cursor_on_cue_is_kept(self):
        s = Stems()
        s.return_cursor = CUE_FIELD
        self.assertEqual(Stems.from_data(s.as_data()).return_cursor, CUE_FIELD)

    def test_cue_is_never_a_mode(self):
        self.assertEqual(Stems.from_data({"return_mode": CUE_FIELD}).return_mode, STEM_MODE)
        self.assertEqual(Stems.from_data({"return_cursor": 3}).return_cursor, STEM_MODE)

    def test_an_old_file_with_last_stems_still_loads(self):
        s = Stems.from_data({"last_stems": [3, None, 8, 1], "cursors": [1, 2, 3, 4]})
        self.assertEqual(s.cursors, [1, 2, 3, 4])
        self.assertFalse(hasattr(s, "last_stems"))

    def test_an_old_or_garbled_file_never_raises(self):
        for data in (None, {}, {"menus": [[0, 0]] * 4}, {"cursors": "x"},
                     {"cursors": [9, 0, 0, 0]}, {"cursors": [0, 0, 0]},
                     {"cursors": [True, 0, 0, 0]}, {"return_mode": 7},
                     {"cursors": [1e999, 0, 0, 0]}):
            s = Stems.from_data(data)
            self.assertEqual(s.cursors, [ANALOG_INPUT] * 4, data)
            self.assertEqual(s.return_mode, STEM_MODE, data)
