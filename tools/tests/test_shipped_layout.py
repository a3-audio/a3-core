"""The shipped layout, checked for the things that can be checked here.

**What used to be here, and why it is gone.** This file held the layout
against the `channel_infos` literal still in a3-core.py, lifted out of the
syntax tree. That test did its job once: it proved the extraction was faithful
before the literal was removed, and it was verified to catch a wrong number by
altering one on purpose (channel 2, track_pfl: "99 != 6"). Then a3-core.py
started reading the layout, the literal went, and there was nothing left to
compare against.

Keeping it would have meant keeping a second copy of the numbers to compare
to, which is the thing the extraction was for.

**What it cannot check.** The layout has to agree with the REAPER project it
ships beside, and nothing here reads .RPP files. A wrong-but-well-formed track
number is caught by a person hearing the wrong channel move -- so the numbers
below are the structure, not the truth.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"

sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_layout import CHANNEL_FIELDS, load_layout   # noqa: E402


class ShippedLayout(unittest.TestCase):
    """A broken layout file stops Core from starting. Better to find that
    here than on a device that has to come up before a set."""

    def setUp(self):
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")

    def test_it_loads_and_has_four_channels(self):
        self.assertEqual(self.layout.channel_count, 4)

    def test_every_channel_names_every_track(self):
        # load_layout refuses a record short of one, so reaching here at all
        # is most of the answer; this says which fields that covers.
        for index in range(self.layout.channel_count):
            channel = self.layout.channel(index)
            for field in CHANNEL_FIELDS:
                self.assertIsInstance(getattr(channel, field), int,
                                      f"channel {index}, {field}")

    def test_no_two_channels_share_a_track(self):
        # Two channels on one track is a wiring mistake that sounds like one
        # channel being deaf, which is hard to trace back to a number.
        for field in ("track_input", "track_channelbus", "track_pfl",
                      "track_multi_enc", "track_stereo_enc"):
            used = [getattr(self.layout.channel(i), field)
                    for i in range(self.layout.channel_count)]
            self.assertEqual(len(set(used)), len(used), f"{field}: {used}")

    def test_a_channel_does_not_use_one_track_for_two_things(self):
        # Within a channel the five tracks are five different tracks. The
        # encoder numbers are a separate numbering and are not compared here.
        for index in range(self.layout.channel_count):
            channel = self.layout.channel(index)
            tracks = [channel.track_input, channel.track_channelbus,
                      channel.track_pfl, channel.track_multi_enc,
                      channel.track_stereo_enc]
            self.assertEqual(len(set(tracks)), len(tracks),
                             f"channel {index}: {tracks}")


if __name__ == "__main__":
    unittest.main()
