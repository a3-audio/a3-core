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
TRUTH = ROOT / "platform-config/debian-x86_64/a3-core/usr/share/a3/a3-osc.json"

from a3_core_layout import load_layout                      # noqa: E402
import a3_osc                                               # noqa: E402
from a3_core_reverse import (CHANNEL, FXPARAM, GAINS,        # noqa: E402
                             GAIN_PARAMS_OF, GLOBAL, REVERSALS,
                             SEND, VOLUME, reverse_for,
                             reversed_address)


class ReverseCase(unittest.TestCase):
    def setUp(self):
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")
        self.truth = a3_osc.load(TRUTH)

    def found(self, address, role_field):
        return reverse_for(self.layout, address, role_field)


class TheChannelStrip(ReverseCase):
    """Channel 0's tracks: input 12, channelbus 9."""

    def test_the_gain(self):
        entry = self.found("/track/12/fx/1/fxparam/1/value", "track_input")
        self.assertEqual(entry.key, "channel.gain")
        self.assertEqual(entry.scope, CHANNEL)

    def test_the_three_bands_are_told_apart(self):
        for param, control in ((1, "channel.eq.high"), (2, "channel.eq.mid"),
                               (3, "channel.eq.low")):
            with self.subTest(param=param):
                entry = self.found(f"/track/12/fx/2/fxparam/{param}/value",
                                   "track_input")
                self.assertEqual(entry.key, control)

    def test_the_volume_answers_on_any_of_its_gain_parameters(self):
        """One plug-in holds the value across several parameters; any of them
        says the same thing."""
        for param in self.layout.gain_params("channelbus"):
            with self.subTest(param=param):
                entry = self.found(f"/track/9/fx/1/fxparam/{param}/value",
                                   "track_channelbus")
                self.assertEqual(entry.key, "channel.volume")

    def test_the_fx_send_is_a_send_and_not_a_parameter(self):
        """It leaves the track rather than sitting on it, so REAPER reports it
        on a shape of its own. Send 1 of the channelbus is the FX bus --
        measured on 2026-10-01: sends 1-5 of track 1 set to five levels, the
        maintainer read enc_fx on send 1 (2 enc_main, 3 dec_phones pre,
        4 dec_phones post, 5 VU)."""
        entry = self.found("/track/1/send/1/volume", "track_channelbus")
        self.assertEqual(entry.key, "channel.fx-send")
        self.assertEqual(entry.scope, CHANNEL)

    def test_another_send_of_the_same_track_is_not_it(self):
        self.assertIsNone(self.found("/track/1/send/3/volume",
                                     "track_channelbus"))


class TheSharedFilter(ReverseCase):
    """One control, written to all four input tracks, reported as a channel's
    and answered globally."""

    def test_the_frequency_comes_back_global(self):
        entry = self.found("/track/12/fx/3/fxparam/7/value", "track_input")
        self.assertEqual(entry.key, "filter.frequency")
        self.assertEqual(entry.scope, GLOBAL)

    def test_the_resonance_too(self):
        entry = self.found("/track/12/fx/3/fxparam/6/value", "track_input")
        self.assertEqual(entry.key, "filter.resonance")
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
                self.assertEqual(entry.key, "filter.frequency")


class TheMasterSection(ReverseCase):
    """None of it belongs to a channel, which is why it had no way back at all
    until 2026-09-12 -- and why A3 Motion's master page came up at its own
    defaults for as long as it existed."""

    def test_the_master_volume(self):
        entry = self.found("/track/1/fx/1/fxparam/15/value",
                           "track_masterbus")
        self.assertEqual(entry.key, "master.volume")
        self.assertEqual(entry.scope, GLOBAL)

    def test_the_booth(self):
        entry = self.found("/track/2/fx/1/fxparam/1/value", "track_booth")
        self.assertEqual(entry.key, "master.booth")

    def test_the_phones_volume(self):
        entry = self.found("/track/3/fx/2/fxparam/1/value", "track_phones")
        self.assertEqual(entry.key, "master.phones-volume")

    def test_the_aux_return(self):
        """Since 2026-09-29 the FX return is its own track, 28 "Return", with
        one Airwindows PurestGain as its first plug-in."""
        entry = self.found("/track/28/fx/1/fxparam/1/value", "aux_return")
        self.assertEqual(entry.key, "master.fx-return")
        self.assertEqual(entry.curve, "slope_volume")

    def test_the_old_aux_return_address_is_not_answered(self):
        # /track/25/fx/3 is enc_fx's DualDelay. It was the return's address
        # until 2026-09-29, and what arrives there now is a delay setting.
        self.assertIsNone(self.found("/track/25/fx/3/fxparam/29/value",
                                     "aux_return"))

    def test_the_phones_mix_is_not_read_back_from_a_track(self):
        """Since the template of 2026-10-01 there is no ph-mix track: the
        crossfade lives in the channel buses' sends to dec_phones, and Core's
        cue logic for it follows. Until then nothing reports it."""
        self.assertFalse(any(e.key == "master.phones-mix" for e in REVERSALS))


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
        keys = [entry.key for entry in REVERSALS]
        self.assertNotIn("channel.filter.frequency", keys)
        self.assertNotIn("channel.filter.q", keys)
        self.assertNotIn("channel.3d", keys)


class TheAddressItComesBackOn(ReverseCase):
    def entry_named(self, key):
        return next(e for e in REVERSALS if e.key == key)

    def test_a_channel_control_is_hung_under_its_channel(self):
        """Channel index 2 is the third channel, /channel/3 on the wire."""
        self.assertEqual(
            reversed_address(self.truth, self.entry_named("channel.gain"), 2),
            "/channel/3/gain")

    def test_a_global_control_carries_its_whole_address(self):
        self.assertEqual(
            reversed_address(self.truth, self.entry_named("master.volume"), 0),
            "/master/volume")

    def test_a_global_control_needs_no_channel_at_all(self):
        """A master track has no channel, and demanding one would mean
        inventing a number only to throw it away."""
        self.assertEqual(
            reversed_address(self.truth,
                             self.entry_named("filter.frequency"), None),
            "/filter/frequency")


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
            with self.subTest(key=entry.key):
                self.assertIn(entry.scope, (CHANNEL, GLOBAL))

    def test_every_entry_is_an_address_of_the_truth_core_accepts(self):
        """Since 2026-09-30 an entry names its address in the truth; a key the
        truth does not have, or one Core does not receive, would be a value
        reported back onto nothing."""
        truth = a3_osc.load(TRUTH)
        for entry in REVERSALS:
            with self.subTest(key=entry.key):
                self.assertIn(entry.key, truth.addresses())
                self.assertIn("core", truth.addresses()[entry.key]["to"])

    def test_a_channel_entry_is_a_channel_address(self):
        """Mixing the two scopes up is silent: a channel entry answered without
        a channel has no ch to fill, a global one hung under a channel does not
        exist."""
        truth = a3_osc.load(TRUTH)
        for entry in REVERSALS:
            with self.subTest(key=entry.key):
                self.assertEqual(entry.scope == CHANNEL,
                                 "ch" in truth.addresses()[entry.key])

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
                with self.subTest(key=entry.key):
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
            with self.subTest(key=entry.key):
                self.assertIn(entry.field, known)


if __name__ == "__main__":
    unittest.main()
