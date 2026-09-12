"""The way back, held against the way out.

a3_core_reverse is a second table. The forward path -- the if/elif chain in
a3-core.py -- is the first, and two tables that must agree drift. The drift is
silent here: a knob would report a wrong value, or stop reporting.

So this walks the handlers in the source, finds every place a value is bent by
a curve and sent to a track, and insists the reverse table knows about it or
that it is on the list of things deliberately left alone.

It reads the source rather than importing it: importing a3-core.py opens
sockets and starts a server, which is the same reason every other test here
lifts what it needs out of the syntax tree.

**When this fails**, the question is not "add an entry" but "should this be
reversible at all" -- the crossfade is not, and saying so is the point of the
exemption list.
"""

import ast
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_reverse import (CHANNEL_REVERSALS,   # noqa: E402
                             reverse_for)

#: Curves whose values are deliberately not relayed back, and why.
NOT_REVERSED = {
    # Sent together with the hipass from one A3 control: /fx/frequency drives
    # both filters at once. Reversing either would be reversing half a
    # control, and the hipass already answers for it.
    "slope_fx_freq_hipass": "arrives on /fx/*, which is global rather than "
                            "per channel, and has no way back yet",
    "slope_fx_freq_lopass": "the same control, the other filter",
    "slope_fx_res": "the same control's resonance",
    # The channel fx-send, which currently carries the 3D crossfade -- see
    # issues/a3-core-fx-send-fuehrt-noch-die-3d-funktion.md. One input becomes
    # two gains on two tracks and a single number cannot say which input it
    # came from.
    # Not the fx send, whatever this excuse used to say -- that branch does
    # its own arithmetic and never called this. The only caller left is
    # /master/return, and a master control has no channel to report back on,
    # the same reason the three filter curves above are here.
    "slope_constant_power": "the aux return's, and /master/* is not per "
                            "channel",
}


def curves_sent_in_source():
    """Every curve name that a handler applies before sending."""
    tree = ast.parse((PACKAGE / "bin/a3-core.py").read_text())
    found = set()
    for node in ast.walk(tree):
        if (isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id.startswith("slope_")):
            found.add(node.func.id)
    return found


class EveryCurveIsAccountedFor(unittest.TestCase):
    def test_each_curve_is_reversed_or_listed_as_not(self):
        reversed_by = {entry.curve for entry in CHANNEL_REVERSALS}

        for curve in sorted(curves_sent_in_source()):
            with self.subTest(curve=curve):
                self.assertTrue(
                    curve in reversed_by or curve in NOT_REVERSED,
                    f"{curve} is applied on the way out and nothing here says "
                    f"whether it can come back. Add a reversal, or say in "
                    f"NOT_REVERSED why it cannot.")

    def test_nothing_is_listed_as_unreversed_that_is_not_used(self):
        # An exemption outliving the code it excused is a note that has
        # stopped being true. It caught five on its first run: eleven curves
        # are characterised and six are applied, because the crossfade rework
        # replaced slope_3d and slope_crossfade_gain with arithmetic written
        # out in the handler. See
        # issues/a3-core-fuenf-kurven-werden-nicht-mehr-benutzt.md.
        used = curves_sent_in_source()
        for curve in NOT_REVERSED:
            with self.subTest(curve=curve):
                self.assertIn(curve, used,
                              f"{curve} is excused but no longer sent")

    def test_no_curve_is_both_reversed_and_excused(self):
        reversed_by = {entry.curve for entry in CHANNEL_REVERSALS}
        self.assertEqual(reversed_by & set(NOT_REVERSED), set())


if __name__ == "__main__":
    unittest.main()


def controls_the_forward_handler_accepts():
    """Every `/channel/n/...` the forward handler answers to.

    Read out of `osc_handler_channel`'s if/elif chain: the names it compares
    `parameter` against, and under `eq` the names it compares `eq_parameter`
    against. A control the handler does not name is one the mixer can send
    and Core will drop.
    """
    tree = ast.parse((PACKAGE / "bin/a3-core.py").read_text())
    handler = next(node for node in ast.walk(tree)
                   if isinstance(node, ast.FunctionDef)
                   and node.name == "osc_handler_channel")

    def compared_against(name):
        for node in ast.walk(handler):
            if (isinstance(node, ast.Compare)
                    and isinstance(node.left, ast.Name)
                    and node.left.id == name
                    and isinstance(node.comparators[0], ast.Constant)):
                yield node.comparators[0].value

    controls = set(compared_against("parameter"))
    controls.discard("eq")
    return controls | {f"eq/{band}"
                       for band in compared_against("eq_parameter")}


class TheWayBackIsSpelledLikeTheWayOut(unittest.TestCase):
    """A returned value has to arrive on the address it was set on.

    The two halves are written in different places -- the forward handler
    splits an address into words, the reverse table names a suffix -- and
    nothing but this connects them. Spelled `eq-high` on the way back where
    the way out says `eq/high`, the mixer would simply never hear it, and no
    part of either side would be wrong on its own.
    """

    def setUp(self):
        from a3_core_layout import load_layout
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")
        self.accepted = controls_the_forward_handler_accepts()

    def test_every_reversal_names_a_control_the_forward_path_knows(self):
        for entry in CHANNEL_REVERSALS:
            self.assertIn(
                entry.address, self.accepted,
                f"{entry.address} is reported back but the forward handler "
                f"answers to {sorted(self.accepted)}")

    def test_the_address_is_built_by_the_layout(self):
        self.assertEqual(
            self.layout.address("channel_control", channel=2,
                                control="eq/high"),
            "/channel/2/eq/high")


#: Controls the forward handler answers to that deliberately have no way back,
#: and why. Checked against the handler itself, so an entry that outlives its
#: reason shows up as a failure rather than as a note nobody reads.
NOT_ANSWERED_FOR = {
    # Core holds these itself and answers a recall from its own memory rather
    # than from REAPER, because REAPER has nothing to report: the position
    # goes straight to the IEM plugins' own OSC port, never through a track.
    "azimuth": "written straight to the IEM plugins; Core remembers it",
    "elevation": "the same",
    # One input, two gains on two tracks. A single number cannot say which
    # input it came from. Since 2026-09-12 Core holds 3d itself and answers a
    # recall from that -- see a3_core_recall.REMEMBERED_CONTROLS. fx-send has
    # no holder, so it is still simply absent.
    "3d": "not invertible from one gain; Core remembers it instead",
    # The same crossfade, arriving by the A3 Mixer's road. It writes the same
    # memory as 3d does, so a recall does answer for it -- on /channel/n/3d,
    # because that is the one control that still exists on both devices. The
    # mixer's pot is analog and has nothing to be told.
    "fx-send": "the same crossfade as 3d; answered on 3d's address",
    # Core's own state, not REAPER's, and they come back from REAPER as a mute
    # rather than as the flag they set.
    "pfl": "a toggle of Core's own; comes back as a mute",
    "fx": "the same",
    "4d": "the same",
}


class EveryControlIsAnsweredFor(unittest.TestCase):
    """Coverage by control name, which is the level the gap was at.

    The curve-based check above walks calls named slope_*. The two encoder
    pots went out through a plain np.interp and so were invisible to it --
    they had no way back for as long as this table existed and nothing said
    so. A control either reports back or is written down here with a reason.
    """

    def setUp(self):
        self.accepted = controls_the_forward_handler_accepts()
        self.reversed_controls = {entry.address
                                  for entry in CHANNEL_REVERSALS}

    def test_each_control_reports_back_or_says_why_not(self):
        for control in sorted(self.accepted):
            self.assertTrue(
                control in self.reversed_controls
                or control in NOT_ANSWERED_FOR,
                f"the forward handler answers to {control!r} but nothing "
                f"reports it back and NOT_ANSWERED_FOR does not say why")

    def test_nothing_is_excused_that_the_handler_does_not_have(self):
        """An excuse outliving its control is a note that has become
        fiction."""
        for control in NOT_ANSWERED_FOR:
            self.assertIn(
                control, self.accepted,
                f"{control!r} is excused but the forward handler no longer "
                f"answers to it")

    def test_no_control_is_both_answered_and_excused(self):
        self.assertEqual(self.reversed_controls & set(NOT_ANSWERED_FOR),
                         set())


class ThePotsFindTheirWayHome(unittest.TestCase):
    """The two entries added on 2026-09-12, end to end through reverse_for.

    Worth spelling out rather than trusting the table: the pots are the first
    entries whose `to` is not the mixer, and the first whose inverse is
    arithmetic rather than a recorded curve.
    """

    def setUp(self):
        from a3_core_layout import load_layout
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")

    def _address(self, channel, slot, param):
        track = getattr(self.layout.channel(channel), "track_stereo_enc")
        return f"/track/{track}/fx/{slot}/fxparam/{param}/value"

    def test_the_first_pot_is_recognised(self):
        slot = self.layout.fx_slot("enc_pots")
        entry = reverse_for(self.layout,
                            self._address(0, slot,
                                          self.layout.fx_param("enc_pot_1")),
                            "track_stereo_enc")
        self.assertIsNotNone(entry)
        self.assertEqual(entry.address, "pot_1")
        self.assertEqual(entry.curve, "linear_enc_pot")

    def test_the_second_pot_is_a_different_entry(self):
        slot = self.layout.fx_slot("enc_pots")
        entry = reverse_for(self.layout,
                            self._address(3, slot,
                                          self.layout.fx_param("enc_pot_2")),
                            "track_stereo_enc")
        self.assertIsNotNone(entry)
        self.assertEqual(entry.address, "pot_2")

    def test_a_pot_goes_back_to_motion_and_not_to_the_mixer(self):
        """The mixer has no encoder. Sending it there would be a message it
        has no handler for."""
        for entry in CHANNEL_REVERSALS:
            if entry.address in ("pot_1", "pot_2"):
                self.assertEqual(entry.to, "motion", entry.address)

    def test_another_plugin_on_the_same_track_is_not_a_pot(self):
        """The stereo encoder itself sits on the same track in a different
        slot. Matching on the track alone would answer for it too."""
        other = self.layout.fx_slot("stereo_enc")
        self.assertNotEqual(other, self.layout.fx_slot("enc_pots"))
        self.assertIsNone(
            reverse_for(self.layout, self._address(0, other, 8),
                        "track_stereo_enc"))
