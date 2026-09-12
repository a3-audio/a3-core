"""Core sagt seinen Stand noch einmal.

A device that has just started knows nothing about the room. Rather than a new
message format for that, Core replays: it sends every value exactly as it
would have been sent when it changed, so the receivers that already exist take
it without understanding anything new.

Three parts, and they come from different places:

- **The flags** are Core's own -- the three per channel and the filter mode --
  and are read out of Core's head, which the state file has just filled from
  disk.
- **The position** is Core's own as well, because nobody else can be asked:
  it goes to the IEM plugins on their own OSC port, so REAPER never reports it
  back.
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
                            Relayed, flag_messages, lamp_messages,
                            led_message, recall_messages,
                            remembered_messages)


class FXMode(Enum):
    LOW_PASS = 0
    HIGH_PASS = 1


@dataclass
class FakeChannel:
    toggle_fx: bool = False
    toggle_pfl: bool = False
    toggle_3d: bool = False
    azimuth: float = None
    elevation: float = None
    three_d: float = None


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

    def test_the_lamps_of_a_rig_are_three_a_channel_and_the_filter(self):
        channels, master = a_rig()
        self.assertEqual(
            len(list(lamp_messages(self.layout, channels, master))),
            4 * 3 + 1)

    def test_the_flags_of_a_rig_are_three_a_channel_and_the_mode(self):
        channels, master = a_rig()
        self.assertEqual(
            len(list(flag_messages(self.layout, channels, master))),
            4 * 3 + 1)

    def test_the_lamp_says_the_mode_as_a_word_and_the_flag_as_a_number(self):
        channels, master = a_rig()
        master.fx_mode = FXMode.HIGH_PASS
        self.assertIn(("/fx/led", "high_pass"),
                      list(lamp_messages(self.layout, channels, master)))
        self.assertIn(("/fx/mode", 1.0),
                      list(flag_messages(self.layout, channels, master)))

    def test_the_flag_is_not_inverted_the_way_its_lamp_is(self):
        """pfl's lamp is inverted here and inverted again in the desk's
        firmware; the two cancel. The flag is the fact, and a device reading
        it must not have to know any of that -- which is exactly why the two
        travel different wires."""
        channels, master = a_rig()
        channels[0].toggle_pfl = True
        lamps = dict(lamp_messages(self.layout, channels, master))
        flags = dict(flag_messages(self.layout, channels, master))
        self.assertEqual(flags["/channel/0/pfl"], 1.0)
        self.assertEqual(lamps["/channel/0/led/pfl"], 0.0)

    def test_no_lamp_is_ever_broadcast(self):
        """A lamp in front of a department that has not read LED_OF is an
        inverted pfl waiting to be trusted."""
        channels, master = a_rig()
        for address, _ in flag_messages(self.layout, channels, master):
            self.assertNotIn("/led/", address)

    def test_the_flag_is_said_on_4d_because_3d_is_the_crossfade(self):
        channels, master = a_rig()
        addresses = [address
                     for address, _ in flag_messages(self.layout, channels,
                                                     master)]
        self.assertIn("/channel/0/4d", addresses)
        self.assertNotIn("/channel/0/3d", addresses)


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
        relayed.note("/channel/0/gain", 0.2)
        relayed.note("/channel/0/gain", 0.7)
        self.assertEqual(list(relayed.messages()),
                         [("/channel/0/gain", 0.7)])

    def test_a_later_value_does_not_move_its_place(self):
        """Replayed in the order the controls were first touched, so the
        same evening replays the same way twice."""
        relayed = Relayed()
        relayed.note("/channel/0/gain", 0.2)
        relayed.note("/channel/1/3d", 0.5)
        relayed.note("/channel/0/gain", 0.7)
        self.assertEqual([address for address, _ in relayed.messages()],
                         ["/channel/0/gain", "/channel/1/3d"])

    def test_one_address_is_one_value_however_many_hear_it(self):
        """It used to be keyed by (device, address), from when a value went
        to one device. Once it went to two, the same number was held twice
        and replayed twice."""
        relayed = Relayed()
        relayed.note("/channel/0/gain", 0.2)
        relayed.note("/channel/0/gain", 0.9)
        self.assertEqual(len(list(relayed.messages())), 1)

    def test_it_can_say_whether_it_already_holds_a_value(self):
        """Asked before sending: REAPER reports one A3 control on several
        parameters, so without this one knob becomes eight identical
        messages to everybody."""
        relayed = Relayed()
        self.assertFalse(relayed.holds("/channel/0/gain", 0.5))
        relayed.note("/channel/0/gain", 0.5)
        self.assertTrue(relayed.holds("/channel/0/gain", 0.5))
        self.assertFalse(relayed.holds("/channel/0/gain", 0.6))


class WhereTheSoundIs(unittest.TestCase):
    """The values only Core can answer for.

    It reaches the IEM plugins on their own OSC port, never through a REAPER
    track, so REAPER never reports it back -- measured on 2026-09-10: Motion
    sending 158,150 azimuth messages and not one word about them from REAPER.
    The plugins do hold it (their receiver sets the host parameter, so the
    project saves it), but nothing can be asked. Core passed it on, so Core
    is the only one who knows.
    """

    def setUp(self):
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")

    def test_a_position_replays_as_the_message_motion_sent(self):
        channels = (FakeChannel(azimuth=-37.5, elevation=12.0),)
        self.assertEqual(
            list(remembered_messages(self.layout, channels)),
            [("/channel/0/azimuth", -37.5),
             ("/channel/0/elevation", 12.0)])

    def test_a_position_never_seen_is_not_invented(self):
        """None is not 0.0. Zero degrees is the front of the room -- a real
        position -- so answering it for a channel Core has never seen a
        position for would place the sound somewhere on purpose while
        claiming to report. Saying nothing leaves Motion on its own value,
        which is what it does today anyway."""
        self.assertEqual(list(remembered_messages(self.layout,
                                                (FakeChannel(),))), [])

    def test_one_half_known_is_answered_by_that_half(self):
        """Azimuth and elevation arrive as two separate messages and there is
        no moment at which both are known but one is not."""
        channels = (FakeChannel(azimuth=90.0),)
        self.assertEqual(list(remembered_messages(self.layout, channels)),
                         [("/channel/0/azimuth", 90.0)])

    def test_zero_is_a_position_and_is_answered(self):
        """The guard is `is None`, not falsiness. Front-centre and level is
        where a channel most often sits."""
        channels = (FakeChannel(azimuth=0.0, elevation=0.0),)
        self.assertEqual(
            list(remembered_messages(self.layout, channels)),
            [("/channel/0/azimuth", 0.0),
             ("/channel/0/elevation", 0.0)])

    def test_each_channel_is_addressed_as_itself(self):
        channels = (FakeChannel(), FakeChannel(azimuth=5.0),
                    FakeChannel(), FakeChannel(elevation=-90.0))
        self.assertEqual(list(remembered_messages(self.layout, channels)),
                         [("/channel/1/azimuth", 5.0),
                          ("/channel/3/elevation", -90.0)])

    def test_the_crossfade_is_remembered_too_and_for_its_own_reason(self):
        """3d is not a position, and it is here for a different reason.

        It does reach REAPER -- but as two gains on two tracks, and a single
        number cannot say which input produced them. So it is not that
        nobody was told; it is that nobody can be asked. Same answer either
        way: Core holds what it was sent.
        """
        channels = (FakeChannel(three_d=0.62),)
        self.assertEqual(list(remembered_messages(self.layout, channels)),
                         [("/channel/0/3d", 0.62)])

    def test_a_crossfade_never_seen_is_not_invented_either(self):
        self.assertEqual(
            list(remembered_messages(self.layout, (FakeChannel(),))), [])

    def test_all_three_come_in_the_order_they_are_sent_in(self):
        channels = (FakeChannel(azimuth=10.0, elevation=20.0, three_d=0.3),)
        self.assertEqual(
            [address for address, _ in remembered_messages(self.layout,
                                                           channels)],
            ["/channel/0/azimuth", "/channel/0/elevation", "/channel/0/3d"])

    def test_the_position_goes_out_like_everything_else(self):
        """It used to be addressed to Motion alone, on the grounds that the
        desk has no sphere. That was true and it was still the wrong shape:
        deciding per message who may hear it is what left A3 Motion's mixer
        at zero for three days. A device that has no use for a position
        ignores it, which costs nothing; a device that needed one and was
        not on the list costs an evening."""
        channels = (FakeChannel(azimuth=1.0),)
        self.assertEqual(
            list(remembered_messages(self.layout, channels)),
            [("/channel/0/azimuth", 1.0)])


class TheWholeAnswer(unittest.TestCase):
    def setUp(self):
        self.layout = load_layout(PACKAGE / "share/a3-core/layout.json")

    def test_a_recall_is_the_flags_and_then_what_reaper_said(self):
        channels, master = a_rig()
        relayed = Relayed()
        relayed.note("/channel/0/gain", 0.7)

        messages = list(recall_messages(self.layout, channels, master,
                                        relayed))
        self.assertEqual(messages[-1], ("/channel/0/gain", 0.7))
        self.assertEqual(len(messages), 4 * 3 + 1 + 1)

    def test_the_position_is_answered_between_the_flags_and_reaper(self):
        """Both of Core's own certainties first, REAPER's relayed values
        last: a caller reading the replay in order sees what Core knows for
        itself before what it was told."""
        channels, master = a_rig()
        channels[1].azimuth = 45.0
        relayed = Relayed()
        relayed.note("/channel/0/gain", 0.7)

        messages = list(recall_messages(self.layout, channels, master,
                                        relayed))
        self.assertEqual(messages[4 * 3 + 1],
                         ("/channel/1/azimuth", 45.0))
        self.assertEqual(messages[-1], ("/channel/0/gain", 0.7))
        self.assertEqual(len(messages), 4 * 3 + 1 + 1 + 1)

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

    def test_a_cold_core_answers_no_position_either(self):
        """Core's own restart loses the position: it is held in memory and
        nowhere else, deliberately. The plugins still have it and the project
        still saves it -- Core just cannot say which it is, and does not
        guess."""
        channels, master = a_rig()
        positions = [m for m in recall_messages(self.layout, channels, master,
                                                Relayed())
                     if m[0] == "motion"]
        self.assertEqual(positions, [])


if __name__ == "__main__":
    unittest.main()
