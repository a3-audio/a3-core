"""Which A3 message a REAPER report stands for.

Three address shapes arrive from REAPER -- a plugin parameter, a send, a track
volume -- and three kinds of A3 control are behind them: a channel's, the
master's, and the one filter all four channels share. The last of those is why
scope is a property of an entry rather than of a table: `/fx/frequency` is one
control written to all four input tracks, so it is *reported on a channel's
track* and is still global.

Kept out of a3-core.py on purpose: no test imports that file -- importing it
opens sockets and starts a server -- and "suite green, program broken" has been
the failure mode of this week three times over.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_layout import load_layout                      # noqa: E402
from a3_core_reverse import (CHANNEL, FXPARAM, GAINS,        # noqa: E402
                             GAIN_PARAMS_OF, GLOBAL, REVERSALS,
                             SEND, VOLUME, reverse_for,
                             reversed_address)


class ReverseCase(unittest.TestCase):
    def setUp(self):
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")

    def found(self, address, role_field):
        return reverse_for(self.layout, address, role_field)


class TheChannelStrip(ReverseCase):
    """Channel 0's tracks: input 12, channelbus 9."""

    def test_the_gain(self):
        entry = self.found("/track/12/fx/1/fxparam/1/value", "track_input")
        self.assertEqual(entry.address, "gain")
        self.assertEqual(entry.scope, CHANNEL)

    def test_the_three_bands_are_told_apart(self):
        for param, control in ((1, "eq/high"), (2, "eq/mid"), (3, "eq/low")):
            with self.subTest(param=param):
                entry = self.found(f"/track/12/fx/2/fxparam/{param}/value",
                                   "track_input")
                self.assertEqual(entry.address, control)

    def test_the_volume_answers_on_any_of_its_gain_parameters(self):
        """One plug-in holds the value across several parameters; any of them
        says the same thing."""
        for param in self.layout.gain_params("channelbus"):
            with self.subTest(param=param):
                entry = self.found(f"/track/9/fx/1/fxparam/{param}/value",
                                   "track_channelbus")
                self.assertEqual(entry.address, "volume")

    def test_the_fx_send_is_a_send_and_not_a_parameter(self):
        """It leaves the track rather than sitting on it, so REAPER reports it
        on a shape of its own. Send 3 of the channelbus is the FX bus --
        measured on 2026-09-12 by moving the fader."""
        entry = self.found("/track/9/send/3/volume", "track_channelbus")
        self.assertEqual(entry.address, "fx-send")
        self.assertEqual(entry.scope, CHANNEL)

    def test_another_send_of_the_same_track_is_not_it(self):
        self.assertIsNone(self.found("/track/9/send/1/volume",
                                     "track_channelbus"))


class TheSharedFilter(ReverseCase):
    """One control, written to all four input tracks, reported as a channel's
    and answered globally."""

    def test_the_frequency_comes_back_global(self):
        entry = self.found("/track/12/fx/3/fxparam/7/value", "track_input")
        self.assertEqual(entry.address, "/fx/frequency")
        self.assertEqual(entry.scope, GLOBAL)

    def test_the_resonance_too(self):
        entry = self.found("/track/12/fx/3/fxparam/6/value", "track_input")
        self.assertEqual(entry.address, "/fx/resonance")
        self.assertEqual(entry.scope, GLOBAL)

    def test_the_lopass_is_deliberately_not_read(self):
        """Both filters carry the same A3 value through different curves.
        Reading both would answer one control twice, with two numbers that
        agree only as well as the two curves do."""
        self.assertIsNone(self.found("/track/12/fx/4/fxparam/7/value",
                                     "track_input"))

    def test_every_channel_reports_the_same_control(self):
        for track in (12, 16, 20, 24):
            with self.subTest(track=track):
                entry = self.found(f"/track/{track}/fx/3/fxparam/7/value",
                                   "track_input")
                self.assertEqual(entry.address, "/fx/frequency")


class TheMasterSection(ReverseCase):
    """None of it belongs to a channel, which is why it had no way back at all
    until 2026-09-12 -- and why A3 Motion's master page came up at its own
    defaults for as long as it existed."""

    def test_the_master_volume(self):
        entry = self.found("/track/1/fx/1/fxparam/15/value",
                           "track_masterbus")
        self.assertEqual(entry.address, "/master/volume")
        self.assertEqual(entry.scope, GLOBAL)

    def test_the_booth(self):
        entry = self.found("/track/2/fx/1/fxparam/1/value", "track_booth")
        self.assertEqual(entry.address, "/master/booth")

    def test_the_phones_volume(self):
        entry = self.found("/track/3/fx/2/fxparam/1/value", "track_phones")
        self.assertEqual(entry.address, "/master/phones_volume")

    def test_the_aux_return(self):
        entry = self.found("/track/25/fx/3/fxparam/29/value", "aux_return")
        self.assertEqual(entry.address, "/master/return")

    def test_the_phones_mix_is_a_track_volume(self):
        """The one value that goes out unbent. `identity` says so in the table
        rather than in an `if` somewhere else."""
        entry = self.found("/track/8/volume", "track_ph_mix")
        self.assertEqual(entry.address, "/master/phones_mix")
        self.assertEqual(entry.curve, "identity")


class WhatIsNotAnswered(ReverseCase):
    def test_a_shape_reaper_has_and_a3_does_not(self):
        for address in ("/track/12/pan", "/track/12/name",
                        "/track/12/fx/1/name", "/master/volume",
                        "/beat", "/track", "/track/12"):
            with self.subTest(address=address):
                self.assertIsNone(self.found(address, "track_input"))

    def test_a_slot_or_parameter_that_is_not_a_number(self):
        for address in ("/track/12/fx/x/fxparam/1/value",
                        "/track/12/fx/1/fxparam/x/value",
                        "/track/9/send/x/volume"):
            with self.subTest(address=address):
                self.assertIsNone(self.found(address, "track_input"))

    def test_the_track_number_is_not_this_module_s_business(self):
        """It is read once, by the caller, in order to work out which role to
        pass in -- so by the time an address reaches here the track has
        already been resolved and parts[1] is never looked at again. Said out
        loud because /track/x/... does find an entry here, and that looks like
        a hole until you know who asks."""
        self.assertIsNotNone(self.found("/track/x/fx/1/fxparam/1/value",
                                        "track_input"))

    def test_the_right_address_on_the_wrong_track_is_nothing(self):
        """The gain's shape on a channelbus is not the channelbus's gain: the
        parameter numbers differ, and answering anyway would put an input
        gain on a channel fader."""
        self.assertIsNone(self.found("/track/9/fx/2/fxparam/2/value",
                                     "track_channelbus"))

    def test_the_encoder_pots_have_no_entry_and_must_not_get_one(self):
        """An action script drives them, so REAPER holds base plus accent
        while the device holds base. Relaying that ratchets -- built
        2026-09-12, live for a few hours, taken out the same day."""
        self.assertNotIn("pot_1", [entry.address for entry in REVERSALS])
        self.assertNotIn("pot_2", [entry.address for entry in REVERSALS])
        self.assertNotIn("3d", [entry.address for entry in REVERSALS])


class TheAddressItComesBackOn(ReverseCase):
    def entry_named(self, address):
        return next(e for e in REVERSALS if e.address == address)

    def test_a_channel_control_is_hung_under_its_channel(self):
        self.assertEqual(
            reversed_address(self.layout, self.entry_named("gain"), 2),
            "/channel/2/gain")

    def test_a_global_control_carries_its_whole_address(self):
        self.assertEqual(
            reversed_address(self.layout,
                             self.entry_named("/master/volume"), 0),
            "/master/volume")

    def test_a_global_control_needs_no_channel_at_all(self):
        """A master track has no channel, and demanding one would mean
        inventing a number only to throw it away."""
        self.assertEqual(
            reversed_address(self.layout,
                             self.entry_named("/fx/frequency"), None),
            "/fx/frequency")


class TheTableItself(unittest.TestCase):
    def setUp(self):
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")

    def test_no_two_entries_answer_the_same_report(self):
        """A duplicate would make which of them wins depend on the order of
        the tuple, which is written for reading."""
        keys = [(e.kind, e.field, e.slot, e.param) for e in REVERSALS]
        self.assertEqual(len(keys), len(set(keys)))

    def test_every_scope_is_one_of_the_two(self):
        for entry in REVERSALS:
            with self.subTest(address=entry.address):
                self.assertIn(entry.scope, (CHANNEL, GLOBAL))

    def test_a_global_entry_carries_a_whole_address_and_a_channel_one_a_suffix(self):
        """Mixing the two up is silent: a suffix used whole becomes an address
        with no leading slash, which JUCE and pythonosc both refuse, and a
        whole address hung under a channel becomes /channel/2//master/volume."""
        for entry in REVERSALS:
            with self.subTest(address=entry.address):
                self.assertEqual(entry.scope == GLOBAL,
                                 entry.address.startswith("/"))

    def test_every_slot_name_is_one_the_layout_knows(self):
        for entry in REVERSALS:
            if entry.kind == FXPARAM:
                self.layout.fx_slot(entry.slot)      # raises if unknown
            elif entry.kind == SEND:
                self.layout.send(entry.slot)
            else:
                self.assertIsNone(entry.slot)

    def test_every_named_parameter_is_one_the_layout_knows(self):
        for entry in REVERSALS:
            if isinstance(entry.param, str) and entry.param != GAINS:
                self.layout.fx_param(entry.param)    # raises if unknown

    def test_every_gains_entry_has_a_list_to_look_in(self):
        for entry in REVERSALS:
            if entry.param == GAINS:
                with self.subTest(address=entry.address):
                    self.assertIn(entry.field, GAIN_PARAMS_OF)
                    self.layout.gain_params(GAIN_PARAMS_OF[entry.field])

    def test_every_field_is_a_track_the_layout_can_resolve(self):
        """A typo here would be an entry that never matches -- a control that
        silently stops reporting, which is exactly the failure this whole
        file exists to prevent."""
        known = set()
        for index in range(self.layout.channel_count):
            channel = self.layout.channel(index)
            known.update(f for f in dir(channel) if f.startswith("track_"))
        known.update(f for f in dir(self.layout.master)
                     if f.startswith("track_"))
        known.add("aux_return")

        for entry in REVERSALS:
            with self.subTest(address=entry.address):
                self.assertIn(entry.field, known)


if __name__ == "__main__":
    unittest.main()
