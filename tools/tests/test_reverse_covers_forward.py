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

from a3_core_reverse import (CHANNEL, CHANNEL_REVERSALS,   # noqa: E402
                             reverse_for)

#: Curves whose values are deliberately not relayed back, and why.
NOT_REVERSED = {
    # /fx/frequency drives both filters at once, from one A3 value. The
    # hipass is read on the way back and this one is not: reading both would
    # answer one control twice, with two numbers that agree only as well as
    # the two curves do.
    "slope_fx_freq_lopass": "the same control as slope_fx_freq_hipass, which "
                            "is the one that is read",
    # The channel fx-send, which currently carries the 3D crossfade -- see
    # issues/a3-core-fx-send-fuehrt-noch-die-3d-funktion.md. One input becomes
    # two gains on two tracks and a single number cannot say which input it
    # came from.
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
    `parameter` against, as the truth's keys. A control the handler does not name is one the mixer can send
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

    # Since 2026-09-30 the handler compares the part of the truth's name
    # after "channel." -- "eq.high", "filter.q" -- so the full name is that.
    return {f"channel.{parameter}" for parameter in compared_against("parameter")}


def controls_a_handler_accepts(handler_name, variable):
    """Every constant a handler compares `variable` against.

    The generic form of controls_the_forward_handler_accepts above, for the
    two handlers that are not per channel. Same reasoning: an address that no
    branch names is one a device can send and Core will drop.
    """
    tree = ast.parse((PACKAGE / "bin/a3-core.py").read_text())
    handler = next(node for node in ast.walk(tree)
                   if isinstance(node, ast.FunctionDef)
                   and node.name == handler_name)

    return {node.comparators[0].value
            for node in ast.walk(handler)
            if (isinstance(node, ast.Compare)
                and isinstance(node.left, ast.Name)
                and node.left.id == variable
                and isinstance(node.comparators[0], ast.Constant))}


class TheWayBackIsSpelledLikeTheWayOut(unittest.TestCase):
    """A returned value has to arrive on the address it was set on.

    The two halves are written in different places -- the forward handler
    splits an address into words, the reverse table names a suffix -- and
    nothing but this connects them. Spelled `eq-high` on the way back where
    the way out says `eq/high`, the mixer would simply never hear it, and no
    part of either side would be wrong on its own.
    """

    def setUp(self):
        self.accepted = controls_the_forward_handler_accepts()

    def test_every_channel_reversal_names_a_control_the_forward_path_knows(self):
        for entry in CHANNEL_REVERSALS:
            if entry.scope != CHANNEL:
                continue
            self.assertIn(
                entry.key, self.accepted,
                f"{entry.key} is reported back but the forward handler "
                f"answers to {sorted(self.accepted)}")

    def test_every_global_reversal_names_an_address_a_handler_answers_to(self):
        """The master and the filter do not go through osc_handler_channel,
        so their addresses are whole rather than suffixes -- and the check
        has to look at the handler that does answer them. /master/phones_mix
        spelled /master/phones-mix would be a control nobody hears, and
        neither side would be wrong on its own."""
        answered = {f"master.{name}"
                    for name in controls_a_handler_accepts("osc_handler_master",
                                                           "parameter")}
        answered |= {f"filter.{name}"
                     for name in controls_a_handler_accepts("osc_handler_filter",
                                                            "parameter")}

        for entry in CHANNEL_REVERSALS:
            if entry.scope == CHANNEL:
                continue
            self.assertIn(
                entry.key, answered,
                f"{entry.key} is reported back but no handler answers "
                f"to it; the handlers take {sorted(answered)}")



def master_branches():
    """`osc_handler_master`'s branches, as {parameter: [statements]}."""
    tree = ast.parse((PACKAGE / "bin/a3-core.py").read_text())
    handler = next(node for node in ast.walk(tree)
                   if isinstance(node, ast.FunctionDef)
                   and node.name == "osc_handler_master")
    branches = {}
    for node in ast.walk(handler):
        if (isinstance(node, ast.If)
                and isinstance(node.test, ast.Compare)
                and isinstance(node.test.left, ast.Name)
                and node.test.left.id == "parameter"
                and isinstance(node.test.comparators[0], ast.Constant)):
            branches[node.test.comparators[0].value] = node.body
    return branches


def calls_in(statements):
    return [node for statement in statements for node in ast.walk(statement)
            if isinstance(node, ast.Call)]


class TheMasterBendsBothWaysAlike(unittest.TestCase):
    """A master control goes out through one curve and comes back through the
    reverse table's. Two curves that differ are a knob that jumps the moment
    REAPER reports it -- and neither side is wrong on its own."""

    def setUp(self):
        self.branches = master_branches()

    def test_each_master_reversal_names_the_curve_its_branch_sends_with(self):
        for entry in CHANNEL_REVERSALS:
            if not entry.key.startswith("master."):
                continue
            if entry.curve == "identity":
                continue
            parameter = entry.key[len("master."):]
            with self.subTest(key=entry.key):
                sent_with = {call.func.id for call in calls_in(
                                 self.branches[parameter])
                             if isinstance(call.func, ast.Name)
                             and call.func.id.startswith("slope_")}
                self.assertEqual(sent_with, {entry.curve})

    def test_the_return_takes_its_slot_from_the_layout(self):
        # It said fx/3 at the call site, and slot 3 of enc_fx had become the
        # DualDelay: the return pot was writing delay parameters. A slot the
        # layout names moves with the project; a literal does not.
        asked = [call.args[0].value for call in calls_in(self.branches["aux-return"])
                 if isinstance(call.func, ast.Attribute)
                 and call.func.attr == "fx_slot"
                 and isinstance(call.args[0], ast.Constant)]
        self.assertEqual(asked, ["aux_gain"])


#: Controls the forward handler answers to that deliberately have no way back,
#: and why. Checked against the handler itself, so an entry that outlives its
#: reason shows up as a failure rather than as a note nobody reads.
NOT_ANSWERED_FOR = {
    # Core holds these itself and answers a recall from its own memory rather
    # than from REAPER, because REAPER has nothing to report: the position
    # goes straight to the IEM plugins' own OSC port, never through a track.
    "channel.azimuth": "written straight to the IEM plugins; Core remembers it",
    "channel.elevation": "the same",
    # One input, two gains on two tracks. A single number cannot say which
    # input it came from. Since 2026-09-12 Core holds 3d itself and answers a
    # recall from that -- see a3_core_recall.REMEMBERED_CONTROLS.
    "channel.3d": "not invertible from one gain; Core remembers it instead",
    # Core's own state, not REAPER's, and they come back from REAPER as a mute
    # rather than as the flag they set.
    # Relaying these continuously is a feedback loop: REAPER holds the base
    # value with the accent envelope on top, Motion holds the base, and
    # writing the one into the other makes every accent's peak the new base.
    # Built on 2026-09-12, live for a few hours, taken out the same day. See
    # issues/a3-core-dauernder-rueckweg-ist-eine-schleife.md.
    "channel.filter.frequency": "REAPER holds it with the accent on top; "
                                "relaying that ratchets",
    "channel.filter.q": "the same",
    "channel.cue": "a toggle of Core's own; its level goes out as two sends",
    "channel.filter": "the same",
    # An encoder's clicks, not a value: there is nothing in REAPER to read
    # back. The assignment it changes is Core's own (a3_core_stems) and is
    # announced on /channel/n/stem.
    "channel.stem.turn": "a relative turn; the result is announced as channel.stem",
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
        self.reversed_controls = {entry.key
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


#: What an action script may drive, and therefore what REAPER's copy of may
#: carry something the device does not hold.
#:
#: These are the rows of the bar's 4x3 grid in a3-motion-ui
#: (ActionComponent.cc: "3d", "freq", "q"). A value on one of them reaches
#: REAPER as base plus whatever the running action is adding; a value on
#: anything else reaches REAPER unchanged.
#:
#: `freq` and `q` are the channel's filter frequency and Q on the wire (pot_1 and
#: pot_2 until 2026-09-30); `3d` is not reversed at all
#: -- one number becomes two gains and cannot be read back.
MODULATED_BY_ACTIONS = ("channel.filter.frequency", "channel.filter.q",
                        "channel.3d")


class ThePotsStayOut(unittest.TestCase):
    """The two encoder pots had reverse entries for a few hours and must not
    get them back by accident.

    Not an opinion about whether Motion should learn its filter back -- it
    should. It is about *how*: relaying REAPER's value continuously mixes two
    different quantities, because REAPER holds the base with the accent
    envelope on top and Motion holds the base. An answer on request is a
    different mechanism and does not go here.
    See issues/a3-core-dauernder-rueckweg-ist-eine-schleife.md.
    """

    def test_no_reversal_relays_a_pot(self):
        for entry in CHANNEL_REVERSALS:
            self.assertNotIn(entry.key, ("channel.filter.frequency",
                                         "channel.filter.q"),
                             "a continuous reverse path for the pots is a "
                             "feedback loop -- see the issue")

    def test_nothing_an_action_can_drive_is_relayed_continuously(self):
        """The rule the pots are one case of, and the right level to pin it.

        This test used to read `assertEqual(entry.to, "mixer")` -- "nothing
        reports back to Motion continuously" -- which was the pots' rule
        stated one size too large. It outlawed the gain and the volume too,
        and those carry no modulation at all: REAPER's copy of a gain *is*
        the device's gain, so there is nothing for a relay to ratchet.

        Stating it too widely cost something real. The strip in A3 Motion
        came up with GAIN and VOL at zero on a rig that was making sound,
        and this test would have passed the whole time.

        What actually decides it is whether an action script can drive the
        control. That, and only that, is what makes REAPER's copy a different
        quantity from the device's.
        """
        for entry in CHANNEL_REVERSALS:
            with self.subTest(control=entry.key):
                self.assertNotIn(
                    entry.key, MODULATED_BY_ACTIONS,
                    f"{entry.key} can be driven by an action, so REAPER "
                    f"holds base+accent while the device holds base. "
                    f"Relaying that continuously ratchets the value up.")
