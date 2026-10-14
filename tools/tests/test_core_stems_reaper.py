"""What the stem mirror becomes: the analog sends to REAPER, the desk's
announcements and the switch commands to StemDeck (spec stemdeck-remote)."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

import a3_osc                                         # noqa: E402
from a3_core_layout import load_layout                # noqa: E402
from a3_core_stems import (ANALOG_MODE, ANALOG_SOURCE, STEM_MODE,  # noqa: E402
                           STEM_SOURCE, UNDECIDED, Stems)
from a3_core_stems_reaper import (analog_messages, announcements,  # noqa: E402
                                  changed_messages, command_messages,
                                  deck_and_stem, pair_of,
                                  return_source_messages, stems_messages)

TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")
LAYOUT = load_layout(PACKAGE / "home/aaa/.local/share/a3-core/layout.json")


class TheChannelsSource(unittest.TestCase):
    """Channel N's input in REAPER: the analog track's send or the stems
    track's send, by the channel's source -- Core's own, not StemDeck's bus
    masks (maintainer, 2026-10-14). Send volume, as REAPER's OSC has no
    send mute."""

    def _sends(self, sources, **reports):
        s = Stems()
        s.sources = list(sources)
        for name, mask in reports.items():
            s.report(int(name[1:]), mask)
        return dict(analog_messages(s, LAYOUT, 0.7) + stems_messages(s, LAYOUT, 0.7))

    def test_analog_opens_the_analog_send_and_shuts_the_stems_send(self):
        msgs = self._sends([ANALOG_SOURCE] * 4)
        for n in range(1, 5):
            self.assertEqual(msgs[f"/track/27/send/{n}/volume"], 0.7)
            self.assertEqual(msgs[f"/track/28/send/{n}/volume"], 0.0)

    def test_stem_opens_the_stems_send_and_shuts_the_analog_send(self):
        msgs = self._sends([ANALOG_SOURCE, STEM_SOURCE, ANALOG_SOURCE, ANALOG_SOURCE])
        self.assertEqual(msgs["/track/27/send/2/volume"], 0.0)
        self.assertEqual(msgs["/track/28/send/2/volume"], 0.7)
        self.assertEqual(msgs["/track/27/send/1/volume"], 0.7)

    def test_a_stem_on_the_bus_does_not_shut_analog(self):
        # Until 2026-10-14 a stem on bus N shut channel N's analog send.
        msgs = self._sends([ANALOG_SOURCE] * 4, p5=1 << 1)
        self.assertEqual(msgs["/track/27/send/2/volume"], 0.7)
        self.assertEqual(msgs["/track/28/send/2/volume"], 0.0)

    def test_an_empty_bus_does_not_open_analog(self):
        msgs = self._sends([STEM_SOURCE] * 4)
        self.assertEqual(msgs["/track/27/send/1/volume"], 0.0)
        self.assertEqual(msgs["/track/28/send/1/volume"], 0.7)

    def test_an_undecided_channel_leaves_its_sends_as_they_are(self):
        # No guess before StemDeck's first report (maintainer, 2026-10-14):
        # REAPER keeps what it has for that channel.
        msgs = self._sends([UNDECIDED, ANALOG_SOURCE, UNDECIDED, STEM_SOURCE])
        for n in (1, 3):
            self.assertNotIn(f"/track/27/send/{n}/volume", msgs)
            self.assertNotIn(f"/track/28/send/{n}/volume", msgs)
        self.assertEqual(msgs["/track/27/send/2/volume"], 0.7)
        self.assertEqual(msgs["/track/28/send/4/volume"], 0.7)

    def test_the_return_sends_are_not_among_them(self):
        msgs = self._sends([STEM_SOURCE] * 4)
        self.assertNotIn("/track/27/send/5/volume", msgs)
        self.assertNotIn("/track/28/send/5/volume", msgs)


class TheReturnsSource(unittest.TestCase):
    """The return plays analog 11/12 or StemDeck's AUX, never both: its
    mode opens one receive and shuts the other (return-sources, 2026-10-07)."""

    ANALOG = "/track/27/send/5/volume"
    STEMS = "/track/28/send/5/volume"

    def _mode(self, mode):
        s = Stems()
        s.return_mode = mode
        return dict(return_source_messages(s, LAYOUT, 0.7))

    def test_stem_mode_opens_stemdecks_aux_and_shuts_analog(self):
        msgs = self._mode(STEM_MODE)
        self.assertEqual(msgs, {self.STEMS: 0.7, self.ANALOG: 0.0})

    def test_analog_mode_opens_analog_and_shuts_stemdecks_aux(self):
        msgs = self._mode(ANALOG_MODE)
        self.assertEqual(msgs, {self.ANALOG: 0.7, self.STEMS: 0.0})

    def test_the_source_does_not_depend_on_which_stems_play(self):
        s = Stems()
        s.return_mode = ANALOG_MODE
        before = return_source_messages(s, LAYOUT, 1.0)
        s.report(3, 1 << 4)                                # AUX, as reported
        self.assertEqual(return_source_messages(s, LAYOUT, 1.0), before)


class WhatTheDeskHears(unittest.TestCase):
    def test_a_channel_on_stem_is_announced_as_its_bus_mask(self):
        s = Stems()
        s.sources[0] = STEM_SOURCE
        s.report(1, 1)
        s.report(6, 1)
        msgs = dict(announcements(s, TRUTH))
        self.assertEqual(msgs["/channel/1/stem"], 0b100001)

    def test_an_undecided_channel_shows_its_bus(self):
        s = Stems()
        s.report(3, 1)
        self.assertEqual(dict(announcements(s, TRUTH))["/channel/1/stem"], 0b100)

    def test_a_channel_on_analog_shows_a_whatever_is_on_its_bus(self):
        # 0 = the analog input (a3-osc.json); the stem on bus 1 is unheard.
        s = Stems()
        s.sources[0] = ANALOG_SOURCE
        s.report(3, 1)
        msgs = dict(announcements(s, TRUTH))
        self.assertEqual(msgs["/channel/1/stem"], 0)

    def test_the_return_is_cursor_then_aux_flags(self):
        s = Stems()
        s.report(3, 1 << 4)                                # AUX
        msgs = dict(announcements(s, TRUTH))
        self.assertEqual(msgs["/aux-return/stem"][1:], [0, 0, 1, 0, 0, 0, 0, 0])

    def test_each_channel_says_its_cursor(self):
        s = Stems()
        s.cursors[2] = 6
        msgs = dict(announcements(s, TRUTH))
        self.assertEqual(msgs["/channel/3/stem/cursor"], 6)
        self.assertFalse(any(a.endswith("/stem/menu") for a in msgs))
        self.assertFalse(any(a.endswith("/stem/selected") for a in msgs))

    def test_the_return_says_its_cursor_first_and_its_mode(self):
        s = Stems()
        s.return_cursor, s.return_mode = 0, 1
        msgs = dict(announcements(s, TRUTH))
        self.assertEqual(msgs["/aux-return/stem"][0], 0)
        self.assertEqual(msgs["/aux-return/stem/mode"], 1)


class TheTidySettles(unittest.TestCase):
    """StemDeck reports stem by stem; Core tidies once they have settled,
    never on a half-updated mirror (spec desk-stem-grid-2)."""

    def test_due_once_after_the_quiet(self):
        from a3_core_stems_reaper import Settle
        settle = Settle(0.3)
        settle.poke(0.0)
        settle.poke(0.2)
        self.assertFalse(settle.due(0.4))
        self.assertTrue(settle.due(0.51))
        self.assertFalse(settle.due(0.9))

    def test_nothing_poked_is_never_due(self):
        from a3_core_stems_reaper import Settle
        self.assertFalse(Settle(0.3).due(100.0))


class WhatStemDeckHears(unittest.TestCase):
    def test_pair_six_is_deck_two_stem_two(self):
        self.assertEqual(deck_and_stem(6), (2, 2))
        self.assertEqual(pair_of(2, 2), 6)

    def test_a_command_is_a_switch_address(self):
        self.assertEqual(command_messages([(6, 5, True)], TRUTH), [("/stemdeck/2/2/bus/5", 1)])


class OnlyWhatChanged(unittest.TestCase):
    def test_a_second_identical_set_sends_nothing(self):
        memory = {}
        messages = [("/a", 1.0), ("/b", 0.0)]
        self.assertEqual(changed_messages(messages, memory), messages)
        self.assertEqual(changed_messages(messages, memory), [])
