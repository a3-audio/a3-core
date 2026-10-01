"""What a stem assignment becomes: REAPER sends and mutes, and what Core says."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

import a3_osc                                         # noqa: E402
from a3_core_layout import load_layout                # noqa: E402
from a3_core_stems import Stems                       # noqa: E402
from a3_core_stems_reaper import (announcements, changed_messages,  # noqa: E402
                                  reaper_messages)

TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")
SHIPPED = PACKAGE / "home/aaa/.local/share/a3-core/layout.json"

STEMS_BLOCK = {
    "send_unity": 0.716,
    "analog": [41, 42, 43, 44],
    "pairs": [{"track": 30 + p, "sends": [1, 2, 3, 4, 5]} for p in range(1, 9)],
}


def layout_with(block):
    data = json.loads(SHIPPED.read_text())
    if block is not None:
        data["stems"] = block
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
        json.dump(data, f)
    return load_layout(f.name)


class TheLayoutBlock(unittest.TestCase):
    def test_without_it_there_are_no_stems(self):
        self.assertIsNone(layout_with(None).stems)

    def test_with_it_the_tracks_are_read(self):
        stems = layout_with(STEMS_BLOCK).stems
        self.assertEqual(stems.analog, [41, 42, 43, 44])
        self.assertEqual(stems.pairs[0], (31, [1, 2, 3, 4, 5]))
        self.assertAlmostEqual(stems.send_unity, 0.716)


class WhatReaperIsTold(unittest.TestCase):
    def setUp(self):
        self.layout = layout_with(STEMS_BLOCK)
        self.msgs = lambda s: dict(reaper_messages(s, self.layout.stems, self.layout.address))

    def test_full_messages_cover_every_send(self):
        msgs = self.msgs(Stems())
        sends = [a for a in msgs if "/send/" in a]
        self.assertEqual(len(sends), 8 * 5)
        self.assertEqual(len([a for a in msgs if a.endswith("/mute")]), 4)

    def test_a_pair_on_a_channel(self):
        s = Stems()
        s.turn_channel(1, +1)                           # channel 2 holds pair 1
        msgs = self.msgs(s)
        self.assertEqual(msgs["/track/31/send/2/volume"], 0.716)   # into channel 2
        self.assertEqual(msgs["/track/31/send/1/volume"], 0.0)     # not channel 1
        self.assertEqual(msgs["/track/31/send/5/volume"], 0.0)     # off the return
        self.assertEqual(msgs["/track/42/mute"], 1.0)              # channel 2's analog off
        self.assertEqual(msgs["/track/41/mute"], 0.0)

    def test_unassigned_pairs_play_on_the_return_unless_muted(self):
        s = Stems()
        s.push_return()                                 # cursor starts on pair 1: muted
        msgs = self.msgs(s)
        self.assertEqual(msgs["/track/31/send/5/volume"], 0.0)
        self.assertEqual(msgs["/track/32/send/5/volume"], 0.716)


class OnlyWhatChanged(unittest.TestCase):
    def setUp(self):
        self.layout = layout_with(STEMS_BLOCK)
        self.stems = Stems()
        self.sent = {}

    def messages(self):
        return reaper_messages(self.stems, self.layout.stems, self.layout.address)

    def test_the_first_call_sends_everything(self):
        first = changed_messages(self.messages(), self.sent)
        self.assertEqual(len(first), 44)

    def test_a_repeat_sends_nothing(self):
        changed_messages(self.messages(), self.sent)
        self.assertEqual(changed_messages(self.messages(), self.sent), [])

    def test_a_turn_sends_only_what_it_changed(self):
        changed_messages(self.messages(), self.sent)
        self.stems.turn_channel(1, +1)       # channel 2 onto pair 1
        self.assertEqual(len(changed_messages(self.messages(), self.sent)), 3)


class WhatCoreSays(unittest.TestCase):
    def test_every_channel_and_the_return(self):
        s = Stems()
        s.turn_channel(3, +1)
        said = dict((a, v) for a, v in announcements(s, TRUTH))
        self.assertEqual(said["/channel/4/stem"], 1)
        self.assertEqual(said["/channel/1/stem"], 0)
        # cursor, then pairs 1-8: 1 plays on the return, 0 is silent there
        self.assertEqual(said["/fx-return/stem"], [2, 0, 1, 1, 1, 1, 1, 1, 1])

    def test_the_return_says_every_pair(self):
        """The desk's return display draws all eight pairs (2x4 squares,
        filled = plays), so Core says each one, not just the cursor's."""
        s = Stems()
        s.turn_channel(0, +3)                           # channel 1 holds pair 3
        s.turn_return(+1)                               # cursor 1 -> 2
        s.push_return()                                 # pair 2 muted on the return
        said = dict((a, v) for a, v in announcements(s, TRUTH))
        self.assertEqual(said["/fx-return/stem"], [2, 1, 0, 0, 1, 1, 1, 1, 1])


if __name__ == "__main__":
    unittest.main()
