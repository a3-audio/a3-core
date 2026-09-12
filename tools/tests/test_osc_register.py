"""The catalogue of addresses the system can speak, read out of the sources.

A traffic log cannot be a register: it shows what flew, not what is possible.
The maintainer went looking for a channel bus's FX-send address and found
three rows, both of which were true. So the addresses are lifted out of the
code that speaks them -- six sources, each with its own shape -- and written
to share/a3-core/osc-register.json.

Every reader here takes **text**, not a path. That is what lets most of this
file run on a machine with none of the neighbouring repos checked out: only
the drift test at the bottom needs the real workspace, and it skips out loud
when a repo is missing rather than passing quietly.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import osc_register as reg   # noqa: E402


MIXER_SOURCE = '''
analog_pots_per_channel_to_osc_param = {
    "0": "fx-send",
    "1": "gain",
}

button_per_channel_to_osc_param = {
    "0": "pfl",
}

master_pots_to_osc_message = {
    "0": "/master/volume",
}

def serial_handler():
    if mode == "TAP":
        osc_core.send_message("/tap", value)

if __name__ == '__main__':
    dispatcher.map("/vu/*", vu_handler)
    dispatcher.map("/fx/led", led_handler_fx)
'''


class TheMixersTables(unittest.TestCase):
    def setUp(self):
        self.entries = reg.from_mixer(MIXER_SOURCE, "a3-mixer.py")
        self.by_address = {entry["address"]: entry for entry in self.entries}

    def test_a_channel_pot_becomes_a_channel_address(self):
        # The source builds this by concatenation -- "/channel/" + track +
        # "/" + param -- so the placeholder is this tool's, spelled the way
        # A3 Motion's header spells it.
        self.assertIn("/channel/{ch}/fx-send", self.by_address)

    def test_the_pot_is_named_where_it_stands_in_the_source(self):
        # Line 3 of MIXER_SOURCE, which starts with a newline: the dict entry
        # itself, not the dict. A reader has to be able to go and look.
        self.assertEqual(self.by_address["/channel/{ch}/fx-send"]["source"],
                         "a3-mixer.py:3")

    def test_a_button_becomes_a_channel_address(self):
        self.assertIn("/channel/{ch}/pfl", self.by_address)

    def test_a_master_pot_keeps_its_written_address(self):
        self.assertIn("/master/volume", self.by_address)

    def test_what_the_mixer_sends_reaches_core(self):
        # Direction is from Core's side: the mixer sends, Core hears.
        self.assertEqual(self.by_address["/channel/{ch}/pfl"]["direction"],
                         reg.IN)
        self.assertEqual(self.by_address["/tap"]["direction"], reg.IN)

    def test_what_the_mixer_subscribes_to_is_sent_to_it(self):
        self.assertEqual(self.by_address["/fx/led"]["direction"], reg.OUT)

    def test_the_vu_meters_never_touch_core(self):
        # The beat-analyzer sends these straight to the mixer. Calling that
        # "in" or "out" would claim Core is a party to it.
        self.assertEqual(self.by_address["/vu/*"]["direction"], reg.ASIDE)

    def test_every_entry_says_it_is_the_mixers(self):
        for entry in self.entries:
            self.assertEqual(entry["device"], "mixer")

    def test_every_entry_carries_the_whole_shape(self):
        for entry in self.entries:
            self.assertEqual(set(entry), set(reg.FIELDS))


MOTION_SOURCE = """
struct OscAddresses
{
  // Outgoing, to A3 Core (SpatBackendA3).
  juce::String channelAzimuth{ "/channel/{ch}/azimuth" };

  /** Something with a slash in prose: see /channel/{ch}/azimuth above. */
  std::array<juce::String, numMixerAddresses> mixerChannel{
    "/channel/{ch}/gain",   "/channel/{ch}/eq/high",
  };

  // Outgoing, to an IEM plugin chain (SpatBackendIEM).
  juce::String iemAzimuth{ "/StereoEncoder/azimuth" };

  // Incoming.
  juce::String vuPrefix{ "/vu/" };
};
"""


class WhatMotionSpeaks(unittest.TestCase):
    def setUp(self):
        self.entries = reg.from_motion(MOTION_SOURCE, "OscAddresses.hh")
        self.by_address = {entry["address"]: entry for entry in self.entries}

    def test_a_named_field_is_read(self):
        self.assertIn("/channel/{ch}/azimuth", self.by_address)

    def test_every_address_of_a_table_is_read(self):
        # The mixer addresses are an array over mixerControlOrder rather than
        # fifteen named fields, so a reader that only understood
        # `juce::String x{...}` would miss the whole channel strip.
        self.assertIn("/channel/{ch}/gain", self.by_address)
        self.assertIn("/channel/{ch}/eq/high", self.by_address)

    def test_the_line_is_the_line_the_address_sits_on(self):
        self.assertEqual(self.by_address["/channel/{ch}/eq/high"]["source"],
                         "OscAddresses.hh:9")

    def test_what_motion_sends_to_core_reaches_core(self):
        self.assertEqual(self.by_address["/channel/{ch}/gain"]["direction"],
                         reg.IN)

    def test_a_plugin_address_does_not_claim_core_hears_it(self):
        # SpatBackendIEM writes straight to the plug-in's own OSC port.
        self.assertEqual(self.by_address["/StereoEncoder/azimuth"]["direction"],
                         reg.ASIDE)

    def test_an_address_the_analyzer_sends_does_not_claim_core_sends_it(self):
        self.assertEqual(self.by_address["/vu/"]["direction"], reg.ASIDE)

    def test_a_slash_in_a_doc_comment_is_not_an_address(self):
        # The header explains itself at length, and prose mentions addresses.
        # Five: the one named field, two out of the table, the plug-in's and
        # the VU prefix -- and nothing out of the comment between them.
        self.assertEqual(len(self.entries), 5)

    def test_an_unknown_field_is_refused_rather_than_guessed_at(self):
        # A new address in the header must not be filed under a direction
        # nobody chose. It has to be added to MOTION_DIRECTIONS by hand.
        with self.assertRaises(ValueError):
            reg.from_motion('  juce::String somethingNew{ "/new/thing" };\n')

    def test_every_entry_says_it_is_motions(self):
        for entry in self.entries:
            self.assertEqual(entry["device"], "motion")


if __name__ == "__main__":
    unittest.main()
