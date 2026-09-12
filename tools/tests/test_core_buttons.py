"""Two senders press the same button, and they do not mean the same thing.

The A³ Mixer's PFL is a momentary switch. Its Pi relays the serial line
verbatim -- `/channel/0/pfl "1"` on the way down, `"0"` on the way up -- so
the press is an *edge* and Core is the one that decides what it toggles.
a3-mixer.py proves the release is real: it guards the fx-mode buttons with
`value == "1"` while sending the channel buttons unguarded, which is exactly
why Core carried `and value == 1`.

A³ Motion's PFL is a key on a screen showing a state. It sends the state it
wants -- 1.0 for on, 0.0 for off -- because that is what the OSC reference
says the address carries ("bool [0 or 1]"), and because an edge over UDP is
lossy: one dropped datagram and the screen and the room disagree forever,
with nothing to put them back.

Core has to serve both, and it can, because the two senders already speak
differently on the wire and always have: the mixer relays *text* (Core's tap
handler compares against the string "1" for the same reason), Motion sends
*numbers*. That is what this module reads. It is a coupling, so it is written
down here and named in the module's own doc rather than left to be rediscovered.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core"
                       / "home/aaa/.local/lib"))

from a3_core_buttons import (NO_CHANGE, wanted_fx_mode,   # noqa: E402
                             wanted_toggle)


class TheMixersMomentaryButton(unittest.TestCase):
    """Text arrives, and it is an edge."""

    def test_a_press_flips_whatever_the_toggle_was(self):
        self.assertIs(wanted_toggle("1", current=False), True)
        self.assertIs(wanted_toggle("1", current=True), False)

    def test_the_release_asks_for_nothing(self):
        self.assertIs(wanted_toggle("0", current=False), NO_CHANGE)
        self.assertIs(wanted_toggle("0", current=True), NO_CHANGE)

    def test_two_presses_come_back_to_where_they_started(self):
        state = False
        for _ in range(2):
            state = wanted_toggle("1", current=state)
        self.assertIs(state, False)

    def test_anything_else_it_could_say_is_not_a_press(self):
        for said in ("", "2", "true", "on"):
            with self.subTest(said=said):
                self.assertIs(wanted_toggle(said, current=True), NO_CHANGE)


class MotionsKeyOnAScreen(unittest.TestCase):
    """A number arrives, and it is the state that is wanted."""

    def test_one_turns_it_on_and_says_so_twice_without_flipping_back(self):
        self.assertIs(wanted_toggle(1.0, current=False), True)
        self.assertIs(wanted_toggle(1.0, current=True), True)

    def test_zero_turns_it_off_and_stays_off(self):
        self.assertIs(wanted_toggle(0.0, current=True), False)
        self.assertIs(wanted_toggle(0.0, current=False), False)

    def test_an_int_is_a_number_too(self):
        self.assertIs(wanted_toggle(1, current=False), True)
        self.assertIs(wanted_toggle(0, current=True), False)

    def test_the_boundary_is_where_motion_puts_it(self):
        # MixerState::channelToggle is `value > 0.5f`, strictly. A state
        # arriving at exactly 0.5 reads as off on the screen it came from,
        # so it must read as off here.
        self.assertIs(wanted_toggle(0.5, current=False), False)
        self.assertIs(wanted_toggle(0.51, current=False), True)

    def test_a_lost_datagram_cannot_leave_the_two_disagreeing(self):
        """The point of a state: repeat it and it converges.

        This is the failure the edge form has and this one does not. Motion
        turns PFL on, the datagram is lost, Motion sends its next state -- and
        Core lands where Motion is, not one flip behind it forever.
        """
        state = False
        for wanted in (1.0, 1.0, 0.0, 0.0, 1.0):
            state = wanted_toggle(wanted, current=state)
        self.assertIs(state, True)


class TheFilterMode(unittest.TestCase):
    """Same two senders, same split, one address: /fx/mode."""

    def test_the_mixer_names_the_mode_it_wants(self):
        self.assertEqual(wanted_fx_mode("high_pass", current="low_pass"),
                         "high_pass")
        self.assertEqual(wanted_fx_mode("low_pass", current="high_pass"),
                         "low_pass")

    def test_naming_the_mode_it_is_already_in_changes_nothing(self):
        self.assertEqual(wanted_fx_mode("high_pass", current="high_pass"),
                         "high_pass")

    def test_motion_sends_the_number_its_own_key_reads(self):
        # MixerState::filterIsHighPass is `> 0.5f`: 1 is HPF, 0 is LPF.
        self.assertEqual(wanted_fx_mode(1.0, current="low_pass"), "high_pass")
        self.assertEqual(wanted_fx_mode(0.0, current="high_pass"), "low_pass")

    def test_a_word_core_does_not_know_leaves_the_filter_where_it_is(self):
        """The bug this replaces did the opposite.

        `high_pass = value == "high_pass"` made *every* unrecognised value --
        including every float Motion has ever sent -- mean LOW_PASS. So a
        message Core could not read still moved the filter, silently, and
        always the same way.
        """
        for said in ("bandpass", "", "HIGH_PASS", None):
            with self.subTest(said=said):
                self.assertEqual(wanted_fx_mode(said, current="high_pass"),
                                 "high_pass")
                self.assertEqual(wanted_fx_mode(said, current="low_pass"),
                                 "low_pass")


class TheTwoSpellingsAreNotMixedUp(unittest.TestCase):
    """The distinction is the type, and only the type."""

    def test_the_string_one_is_never_read_as_the_number_one(self):
        # If it were, the mixer's press would set "on" instead of flipping,
        # and PFL could never be switched off from the mixer again.
        self.assertIs(wanted_toggle("1", current=True), False)
        self.assertIs(wanted_toggle(1.0, current=True), True)

    def test_the_number_zero_is_never_read_as_the_release(self):
        # If it were, Motion could never switch anything off.
        self.assertIs(wanted_toggle("0", current=True), NO_CHANGE)
        self.assertIs(wanted_toggle(0.0, current=True), False)

    def test_no_change_refuses_to_be_mistaken_for_off(self):
        """`if not wanted:` is the mistake, so it raises rather than ships.

        Asking for nothing and asking for off are the two things this module
        keeps apart; a truthiness test merges them again and would drop every
        Motion "switch it off" along with every mixer release.
        """
        with self.assertRaises(TypeError):
            bool(NO_CHANGE)
        with self.assertRaises(TypeError):
            if not wanted_toggle("0", current=True):
                pass

    def test_a_bool_counts_as_a_number(self):
        # python-osc decodes an OSC boolean as True/False, and bool is an int.
        # Nothing sends one today; if something starts, a state is what it is.
        self.assertIs(wanted_toggle(True, current=True), True)
        self.assertIs(wanted_toggle(False, current=True), False)


if __name__ == "__main__":
    unittest.main()
