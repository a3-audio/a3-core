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

from a3_core_reverse import CHANNEL_REVERSALS   # noqa: E402

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
    "slope_constant_power": "the fx send, which currently carries 3d, and is "
                            "not invertible from one number",
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
