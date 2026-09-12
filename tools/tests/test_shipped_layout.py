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


class TheRestOfTheMap(unittest.TestCase):
    """The FX slots and the gain parameter lists.

    **These held against the source too, and could not keep doing it.** Both
    the FX_INDEX_* constants and the four `for … in [1, 15, …]` loops were
    compared against the layout while they were still literals; both were
    verified to catch a wrong value ("9 != 2 : eq" and "[1, 15] != [1, 15, 29,
    43, 57, 71, 85, 99]"). Then a3-core.py started reading them from the
    layout and there was nothing left to compare to -- the same door the
    track-number comparison went through one commit earlier.

    One of the four gain lists was first written from memory and only checked
    afterwards. It happened to be right. That is why they were pinned before
    the literals went, rather than after.
    """

    def setUp(self):
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")

    def test_every_slot_a3_core_asks_for_is_there(self):
        # a3-core.py reads exactly these eight at import. One missing is a
        # Core that does not start -- which is the right failure, but it
        # should be found here.
        for slot in ("gain", "eq", "eq_enc", "hipass", "lopass",
                     "channel_volume", "stereo_enc", "enc", "enc_pots"):
            self.assertGreater(self.layout.fx_slot(slot), 0, slot)

    def test_the_fx_send_is_named_and_is_the_one_that_was_measured(self):
        """Send 3 of a channelbus reaches enc_fx, where the delay sits.

        Derived from the receiver order in the project (1-pfl, ph-mix,
        enc_fx, enc_main) and then confirmed on 2026-09-12 by moving the
        fader and watching /track/9/send/3/volume arrive at Core.
        """
        self.assertEqual(self.layout.send("fx"), 3)

    def test_every_gain_list_a3_core_asks_for_is_there(self):
        for name in ("channelbus", "masterbus", "boothbus", "aux_return"):
            params = self.layout.gain_params(name)
            self.assertTrue(params, name)
            self.assertEqual(len(set(params)), len(params),
                             f"{name} repeats a parameter: {params}")

    def test_the_master_tracks_are_distinct(self):
        from a3_core_layout import MASTER_FIELDS
        used = [getattr(self.layout.master, f) for f in MASTER_FIELDS]
        self.assertEqual(len(set(used)), len(used), used)


class AddressesMatchTheSource(unittest.TestCase):
    """Every address a3-core.py sends, rebuilt from the layout.

    The call sites still hold the literals, so this can be checked -- and it
    is checked *before* they are rewritten, which is the lesson from the two
    comparisons already spent: a pin that arrives after the literal is gone
    pins nothing.

    It walks every send_message in the source, turns the f-string back into a
    template, and insists the layout can produce it. What it does not check is
    which values are filled in -- that is the rewrite's job, and the numbers
    it fills from are pinned by fx_params below.
    """

    def setUp(self):
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")

    @staticmethod
    def _templates_in_source():
        import ast
        src = (PACKAGE / "bin/a3-core.py").read_text()
        found = set()
        for node in ast.walk(ast.parse(src)):
            if not (isinstance(node, ast.Call)
                    and getattr(node.func, "attr", "") == "send_message"
                    and node.args):
                continue
            first = node.args[0]
            if isinstance(first, ast.Constant) and isinstance(first.value, str):
                if first.value.startswith("/"):
                    found.add(first.value)
            elif isinstance(first, ast.JoinedStr):
                shape = ""
                for part in first.values:
                    shape += (str(part.value) if isinstance(part, ast.Constant)
                              else "{}")
                if shape.startswith("/"):
                    found.add(shape)
        return found

    @staticmethod
    def _normalised(address):
        """Every number and every placeholder blanked to {}.

        Some call sites fill the slot and the parameter in from a variable and
        some write them out -- /fx/1/fxparam/15/value is the same shape as
        /fx/{slot}/fxparam/{param}/value, and after the rewrite it will be
        spelled that way. Normalising both sides is what lets this be checked
        *before* the rewrite rather than after, which is when a pin is worth
        having: a comparison that arrives after the literal is gone pins
        nothing.

        Every number in these addresses is a track, a slot or a parameter --
        there is no fixed digit to protect.
        """
        import re
        return re.sub(r"\{[^}]*\}", "{}", re.sub(r"/\d+", "/{}", address))

    def test_every_address_sent_has_a_shape_in_the_layout(self):
        shapes = {self._normalised(template)
                  for template in self.layout._addresses.values()}

        for sent in self._templates_in_source():
            self.assertIn(
                self._normalised(sent), shapes,
                f"{sent} is sent but no layout address has that shape")

    def test_the_named_parameters_are_the_numbers_the_source_uses(self):
        # The ones that can be read off a literal address unambiguously.
        expected = {"elevation": 8, "eq_high": 1, "eq_mid": 2, "eq_low": 3,
                    "filter_frequency": 7, "filter_resonance": 6,
                    "enc_pot_1": 1, "enc_pot_2": 2}
        for name, number in expected.items():
            self.assertEqual(self.layout.fx_param(name), number, name)
