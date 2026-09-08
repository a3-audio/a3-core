"""Core sagt seinen Stand noch einmal.

A device that has just started knows nothing about the room. Rather than a new
message format for that, Core replays: it sends every value exactly as it
would have been sent when it changed, so the receivers that already exist take
it without understanding anything new.

Two halves, and they come from different places:

- **The flags** are Core's own -- the three per channel and the filter mode --
  and are read out of Core's head, which the state file has just filled from
  disk.
- **The continuous values** are REAPER's. Core does not hold them; it relays
  them, and what it relays it notes on the way past. So the replay is what
  REAPER last said rather than a second opinion about it.
"""

import ast
import sys
import unittest
from dataclasses import dataclass
from enum import Enum
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_layout import load_layout   # noqa: E402
from a3_core_recall import (FX_MODE_WORDS, LED_OF,   # noqa: E402
                            Relayed, flag_messages, led_message,
                            recall_messages)


class FXMode(Enum):
    LOW_PASS = 0
    HIGH_PASS = 1


@dataclass
class FakeChannel:
    toggle_fx: bool = False
    toggle_pfl: bool = False
    toggle_3d: bool = False


@dataclass
class FakeMaster:
    fx_mode: FXMode = FXMode.LOW_PASS


def a_rig(channels=4):
    return tuple(FakeChannel() for _ in range(channels)), FakeMaster()


class TheLights(unittest.TestCase):
    def setUp(self):
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")

    def test_a_light_is_addressed_the_way_the_layout_says(self):
        channel = FakeChannel(toggle_fx=True)
        self.assertEqual(led_message(self.layout, "fx", 2, channel),
                         ("/channel/2/led/fx", 1.0))

    def test_pfl_is_sent_the_other_way_round_and_that_is_correct(self):
        """Core sends `not pfl` and the mixer inverts again in
        send_button_leds_data (`0 if led_on else 255`, for led_mode 0). The
        two cancel: pfl on is a lit button. Written out here because it looks
        exactly like a bug and is not one -- see
        issues/a3-doc-led-adressen-falsch-herum.md."""
        lit = FakeChannel(toggle_pfl=True)
        dark = FakeChannel(toggle_pfl=False)
        self.assertEqual(led_message(self.layout, "pfl", 0, lit)[1], 0.0)
        self.assertEqual(led_message(self.layout, "pfl", 0, dark)[1], 1.0)

    def test_the_flags_of_a_rig_are_three_a_channel_and_the_filter(self):
        channels, master = a_rig()
        messages = list(flag_messages(self.layout, channels, master))
        self.assertEqual(len(messages), 4 * 3 + 1)
        self.assertTrue(all(device == "mixer" for device, _, _ in messages))

    def test_the_filter_mode_is_a_word_not_a_number(self):
        channels, master = a_rig()
        master.fx_mode = FXMode.HIGH_PASS
        self.assertIn(("mixer", "/fx/led", "high_pass"),
                      list(flag_messages(self.layout, channels, master)))


class WhatTheSourceSays(unittest.TestCase):
    """Two tables here have a twin in a3-core.py, and twins drift.

    Read out of the syntax tree rather than imported: importing a3-core.py
    opens sockets and starts a server.
    """

    @staticmethod
    def _tree():
        return ast.parse((PACKAGE / "bin/a3-core.py").read_text())

    def test_every_filter_mode_has_a_word(self):
        """A mode added to the enum without a word here would be replayed as
        a KeyError -- or, worse, as the other mode."""
        modes = {target.id
                 for node in ast.walk(self._tree())
                 if isinstance(node, ast.ClassDef) and node.name == "FXMode"
                 for statement in node.body
                 if isinstance(statement, ast.Assign)
                 for target in statement.targets}
        self.assertEqual(modes, set(FX_MODE_WORDS))

    def test_every_light_the_layout_has_is_replayed(self):
        """The layout names four LED addresses. Three are per channel and
        belong to a flag; the fourth is the filter mode, which flag_messages
        sends on its own."""
        layout = load_layout(PACKAGE / "share/a3-core/layout.json")
        named = {name for name in layout._addresses if "led" in name}
        replayed = {name for name, _ in LED_OF.values()} | {"fx_mode_led"}
        self.assertEqual(named, replayed)


class WhatWasPassedOn(unittest.TestCase):
    def test_nothing_relayed_is_nothing_to_replay(self):
        self.assertEqual(list(Relayed().messages()), [])

    def test_the_last_value_wins(self):
        relayed = Relayed()
        relayed.note("mixer", "/channel/0/gain", 0.2)
        relayed.note("mixer", "/channel/0/gain", 0.7)
        self.assertEqual(list(relayed.messages()),
                         [("mixer", "/channel/0/gain", 0.7)])

    def test_a_later_value_does_not_move_its_place(self):
        """Replayed in the order the controls were first touched, so the
        same evening replays the same way twice."""
        relayed = Relayed()
        relayed.note("mixer", "/channel/0/gain", 0.2)
        relayed.note("motion", "/channel/1/3d", 0.5)
        relayed.note("mixer", "/channel/0/gain", 0.7)
        self.assertEqual([address for _, address, _ in relayed.messages()],
                         ["/channel/0/gain", "/channel/1/3d"])

    def test_two_devices_can_hold_the_same_address(self):
        relayed = Relayed()
        relayed.note("mixer", "/channel/0/gain", 0.2)
        relayed.note("motion", "/channel/0/gain", 0.9)
        self.assertEqual(len(list(relayed.messages())), 2)


class TheWholeAnswer(unittest.TestCase):
    def setUp(self):
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")

    def test_a_recall_is_the_flags_and_then_what_reaper_said(self):
        channels, master = a_rig()
        relayed = Relayed()
        relayed.note("mixer", "/channel/0/gain", 0.7)

        messages = list(recall_messages(self.layout, channels, master,
                                        relayed))
        self.assertEqual(messages[-1], ("mixer", "/channel/0/gain", 0.7))
        self.assertEqual(len(messages), 4 * 3 + 1 + 1)

    def test_a_cold_core_still_answers_with_its_own_flags(self):
        """After Core itself restarts, nothing has been relayed yet: REAPER
        reports on change and does not know Core went away. The flags come
        from Core's state file and are answered anyway -- an answer that is
        short is better than silence, which a caller cannot tell from a Core
        that is not there."""
        channels, master = a_rig()
        messages = list(recall_messages(self.layout, channels, master,
                                        Relayed()))
        self.assertEqual(len(messages), 4 * 3 + 1)


if __name__ == "__main__":
    unittest.main()
