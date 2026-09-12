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

import json
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


LAYOUT_SOURCE = """{
  "_comment": "not an address",
  "addresses": {
    "channel_control": "/channel/{channel}/{control}",
    "led_pfl": "/channel/{channel}/led/pfl",
    "track_volume": "/track/{track}/volume",
    "dualdelay_bpm": "/DualDelay/delayBPM{side}"
  }
}
"""


class CoresOwnTemplates(unittest.TestCase):
    def setUp(self):
        self.entries = reg.from_layout(LAYOUT_SOURCE, "layout.json")
        self.by_address = {entry["address"]: entry for entry in self.entries}

    def test_a_template_keeps_the_placeholders_the_source_wrote(self):
        # `{channel}` rather than `{ch}`: this one is a format string Core
        # actually fills in, so renaming it would make the register disagree
        # with the file it came from.
        self.assertIn("/channel/{channel}/{control}", self.by_address)

    def test_nothing_outside_the_addresses_block_is_read(self):
        self.assertEqual(len(self.entries), 4)

    def test_each_template_is_named_where_its_key_stands(self):
        self.assertEqual(
            self.by_address["/channel/{channel}/led/pfl"]["source"],
            "layout.json:5")

    def test_a_lamp_is_sent_to_the_mixer(self):
        lamp = self.by_address["/channel/{channel}/led/pfl"]
        self.assertEqual(lamp["device"], "mixer")
        self.assertEqual(lamp["direction"], reg.OUT)

    def test_a_track_is_sent_to_reaper(self):
        self.assertEqual(self.by_address["/track/{track}/volume"]["device"],
                         "reaper")

    def test_the_delay_is_its_own_device(self):
        self.assertEqual(
            self.by_address["/DualDelay/delayBPM{side}"]["device"],
            "dualdelay")

    def test_an_unknown_key_is_refused_rather_than_guessed_at(self):
        with self.assertRaises(ValueError):
            reg.from_layout('{"addresses": {"new_thing": "/new/thing"}}')


CORE_SOURCE = '''
OSC_ADDRESS_BEAT: str = "/beat"


def osc_handler_channel(client_address, address, *osc_arguments):
    if parameter == "gain":
        osc_reaper.send_message(
            f"/track/{track_input}/fx/{FX_INDEX_GAIN}/fxparam/1/value", val)
    elif parameter == "eq":
        if eq_parameter == "high":
            osc_reaper.send_message("/track/1/fx/2/fxparam/1/value", val)
    elif parameter == "4d":
        osc_a3mixer.send_message(*led_message(_layout, "3d", i, c))
    elif parameter == "azimuth":
        addr = f"/MultiEncoder/azimuth{channel_index}"
        for client in udp_clients_iem:
            client.send_message(addr, az)


def osc_handler_master(client_address, address, *osc_arguments):
    if parameter == "booth":
        pass


def osc_handler_fx(client_address, address, *osc_arguments):
    if parameter == "mode":
        pass


def param_handler(address, *osc_arguments):
    if parameter == "ghost":
        pass


if __name__ == "__main__":
    dispatcher.map("/channel/*", osc_handler_channel)
    dispatcher.map(OSC_ADDRESS_BEAT, osc_handler_beat)
    midiout.send_message(note)
'''


class WhatCoreItselfBuilds(unittest.TestCase):
    def setUp(self):
        self.entries = reg.from_core(CORE_SOURCE, "a3-core.py")
        self.by_address = {entry["address"]: entry for entry in self.entries}

    def test_a_mapped_pattern_is_what_core_listens_for(self):
        listens = self.by_address["/channel/*"]
        self.assertEqual(listens["device"], "core")
        self.assertEqual(listens["direction"], reg.IN)

    def test_a_mapped_constant_is_resolved_to_its_value(self):
        # dispatcher.map(OSC_ADDRESS_BEAT, ...) -- a register that wrote
        # "{OSC_ADDRESS_BEAT}" here would be useless for the one question it
        # is asked: what is the address.
        self.assertIn("/beat", self.by_address)

    def test_each_handled_parameter_becomes_a_whole_address(self):
        # The if/elif chain is where Core says what it understands, and it
        # says it in pieces: the address is never written out.
        self.assertIn("/channel/{ch}/gain", self.by_address)
        self.assertIn("/channel/{ch}/eq/high", self.by_address)
        self.assertIn("/channel/{ch}/4d", self.by_address)
        self.assertIn("/master/booth", self.by_address)
        self.assertIn("/fx/mode", self.by_address)

    def test_a_branch_that_is_not_an_address_is_left_out(self):
        # `parameter == "eq"` only opens a second comparison on the word after
        # it. Nothing arrives on /channel/n/eq, so listing it would invent a
        # dead wire.
        self.assertNotIn("/channel/{ch}/eq", self.by_address)

    def test_a_dead_handler_is_not_read(self):
        # param_handler is mapped nowhere and calls three functions that do
        # not exist. Listing what it compares against would put addresses in
        # the register that no running code can ever receive.
        self.assertNotIn("/channel/{ch}/ghost", self.by_address)

    def test_a_reaper_parameter_keeps_the_names_that_decide_its_numbers(self):
        reaper = self.by_address[
            "/track/{track_input}/fx/{FX_INDEX_GAIN}/fxparam/1/value"]
        self.assertEqual(reaper["device"], "reaper")
        self.assertEqual(reaper["direction"], reg.OUT)

    def test_the_device_follows_the_client_that_sends(self):
        self.assertEqual(
            self.by_address["/MultiEncoder/azimuth{ch}"]["device"], "iem")
        self.assertEqual(
            self.by_address["/track/1/fx/2/fxparam/1/value"]["device"],
            "reaper")

    def test_something_that_is_not_an_address_is_not_one(self):
        for address in self.by_address:
            self.assertTrue(address.startswith("/"), address)

    def test_an_unknown_sender_is_refused_rather_than_guessed_at(self):
        with self.assertRaises(ValueError):
            reg.from_core('something_new.send_message("/new/thing", 1)\n')


REAPER_SOURCE = """# A comment with a /slash/in/it
DEVICE_TRACK_COUNT 27
REAPER_TRACK_FOLLOWS DEVICE

TRACK_VOLUME n/track/volume n/track/@/volume
TRACK_MUTE b/track/mute t/track/mute/toggle
"""


class EverythingReaperUnderstands(unittest.TestCase):
    def setUp(self):
        self.entries = reg.from_reaper(REAPER_SOURCE, "a3-core.ReaperOSC")
        self.by_address = {entry["address"]: entry for entry in self.entries}

    def test_the_type_letter_is_not_part_of_the_address(self):
        # `n/track/volume` is a pattern, not an address: the leading letter
        # says what kind of value rides on it.
        self.assertIn("/track/volume", self.by_address)
        self.assertNotIn("n/track/volume", self.by_address)

    def test_every_pattern_on_a_line_is_read(self):
        self.assertIn("/track/@/volume", self.by_address)
        self.assertIn("/track/mute/toggle", self.by_address)

    def test_the_wildcard_stays_the_wildcard_reaper_wrote(self):
        self.assertIn("/track/@/volume", self.by_address)

    def test_a_setting_is_not_an_address(self):
        self.assertEqual(len(self.entries), 4)

    def test_a_comment_is_not_an_address(self):
        for address in self.by_address:
            self.assertTrue(address.startswith("/track/"), address)

    def test_the_action_name_is_carried_as_the_note(self):
        # The searchable word. "FX_WETDRY" is how a reader thinks of it; the
        # pattern is what goes on the wire.
        self.assertEqual(self.by_address["/track/volume"]["note"],
                         "TRACK_VOLUME")

    def test_reaper_both_takes_these_and_reports_on_them(self):
        # The file does not distinguish, and inventing the distinction would
        # double the register to say nothing it says.
        self.assertEqual(self.by_address["/track/volume"]["direction"],
                         reg.BOTH)
        self.assertEqual(self.by_address["/track/volume"]["device"], "reaper")


ANALYZER_SOURCE = """
    int len = serializeIntIntFloat(buf, "/beat", msg.beat_number);
    m_vuOscPaths.push_back("/vu/" + std::to_string(i));
    printf("/vu/3 | peak %.3f | rms %.3f\\n", peaks[3], rms[3]);
    else if (std::strcmp(path, "/clockmode") == 0) {
"""


class WhatTheAnalyzerSpeaks(unittest.TestCase):
    def setUp(self):
        self.entries = reg.from_beat_analyzer(ANALYZER_SOURCE, "osc_sender.cpp")
        self.by_address = {entry["address"]: entry for entry in self.entries}

    def test_the_beat_reaches_core(self):
        beat = self.by_address["/beat"]
        self.assertEqual(beat["device"], "beat-analyzer")
        self.assertEqual(beat["direction"], reg.IN)

    def test_the_vu_meters_go_straight_to_the_two_controllers(self):
        self.assertEqual(self.by_address["/vu/"]["direction"], reg.ASIDE)

    def test_what_the_analyzer_listens_for_does_not_come_from_core(self):
        self.assertEqual(self.by_address["/clockmode"]["direction"], reg.ASIDE)

    def test_a_format_string_is_not_an_address(self):
        # printf("/vu/3 | peak %.3f ...") starts with a slash and is not an
        # address. A space is what gives it away.
        self.assertNotIn("/vu/3 | peak %.3f | rms %.3f\\n", self.by_address)
        self.assertEqual(len(self.entries), 3)

    def test_an_unknown_address_is_refused_rather_than_guessed_at(self):
        with self.assertRaises(ValueError):
            reg.from_beat_analyzer('sendTo("/new/thing");\n')


class PuttingItTogether(unittest.TestCase):
    def test_the_same_thing_said_twice_is_one_row(self):
        said = [reg.entry("/beat", "beat-analyzer", reg.IN, "b.cpp:9"),
                reg.entry("/beat", "beat-analyzer", reg.IN, "a.cpp:1")]
        merged = reg.merge(said)
        self.assertEqual(len(merged), 1)
        # The first in sort order, so the file does not churn between runs.
        self.assertEqual(merged[0]["source"], "a.cpp:1")

    def test_two_devices_on_one_address_stay_two_rows(self):
        said = [reg.entry("/channel/{ch}/gain", "mixer", reg.IN, "m.py:62"),
                reg.entry("/channel/{ch}/gain", "motion", reg.IN, "o.hh:83"),
                reg.entry("/channel/{ch}/gain", "core", reg.IN, "c.py:526")]
        self.assertEqual(len(reg.merge(said)), 3)

    def test_one_address_heard_and_said_stays_two_rows(self):
        said = [reg.entry("/channel/{ch}/azimuth", "motion", reg.IN, "o.hh:59"),
                reg.entry("/channel/{ch}/azimuth", "motion", reg.OUT, "c.py:1")]
        self.assertEqual(len(reg.merge(said)), 2)

    def test_what_is_written_is_what_json_reads_back(self):
        # as_text lays one entry per line by hand, so that a diff of this file
        # shows a moved address as one line rather than five. Hand-rolled
        # enough to be worth proving it is still JSON.
        register = {"_comment": "c", "devices": ["mixer"],
                    "directions": ["in"],
                    "entries": [reg.entry("/a", "mixer", reg.IN, "x:1"),
                                reg.entry("/b", "mixer", reg.OUT, "x:2")]}
        self.assertEqual(json.loads(reg.as_text(register)), register)

    def test_a_missing_repo_is_named_rather_than_skipped_over(self):
        paths = {"mixer": [Path("/nowhere/a3-mixer.py")],
                 "core": [Path(__file__)],
                 "beat-analyzer": []}
        self.assertEqual(reg.missing(paths), ["beat-analyzer", "mixer"])


class TheCheckedInRegister(unittest.TestCase):
    """The price of generating and checking in: the file can go stale.

    This is what stops that. It rebuilds from the sources in the workspace and
    compares -- and where a neighbouring repo is not checked out it skips and
    says which, rather than passing quietly against a register it could not
    have rebuilt.
    """

    def test_it_is_what_the_sources_say_today(self):
        paths = reg.source_paths(ROOT)
        absent = reg.missing(paths)
        if absent:
            self.skipTest(
                f"the register cannot be rebuilt here: {', '.join(absent)} "
                f"is not beside this checkout, so drift in it would go "
                f"unnoticed. Run this in the a3-system workspace.")

        shipped = json.loads((ROOT / reg.REGISTER).read_text())
        self.assertEqual(
            reg.build(paths), shipped,
            "share/a3-core/osc-register.json no longer matches the sources -- "
            "run tools/osc_register.py and commit the result")


if __name__ == "__main__":
    unittest.main()
