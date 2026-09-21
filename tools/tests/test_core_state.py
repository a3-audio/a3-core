"""Cores eigener Stand, ueber einen Neustart hinweg.

What REAPER holds, REAPER reports -- see test_reverse_covers_forward. What is
left is the handful of things that live only in Core's head: the three toggles
a channel carries, the filter mode, and the 3D crossfade, which REAPER holds
the consequence of but cannot be asked the cause of. Those survive a restart
only if something writes them down.

The module is deliberately ignorant of a3-core.py's dataclasses: it is handed
the objects and reads the fields it knows by name. Importing a3-core.py opens
sockets, which is the same reason every other test here works from the outside.
"""

import json
import sys
import time
import unittest
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_state import (StateFile, apply_state,   # noqa: E402
                           state_of)


class FXMode(Enum):
    LOW_PASS = 0
    HIGH_PASS = 1


@dataclass
class FakeChannel:
    """Only the fields the state module claims. The real ChannelInfo carries
    a dozen track numbers besides, and none of them are the moment -- plus
    `azimuth`, `elevation` and `width`, which are not kept; see
    WhatIsKeptAndWhatIsNot below."""
    toggle_fx: bool = False
    toggle_pfl: bool = False
    three_d: float = None


@dataclass
class FakeMaster:
    fx_mode: FXMode = FXMode.LOW_PASS


def a_rig(channels=4):
    return tuple(FakeChannel() for _ in range(channels)), FakeMaster()


class WhatComesBack(unittest.TestCase):
    def test_a_round_trip_returns_every_field(self):
        channels, master = a_rig()
        channels[0].toggle_fx = True
        channels[2].toggle_pfl = True
        channels[3].toggle_fx = True
        master.fx_mode = FXMode.HIGH_PASS

        fresh_channels, fresh_master = a_rig()
        apply_state(state_of(channels, master), fresh_channels, fresh_master)

        self.assertEqual(fresh_channels, channels)
        self.assertEqual(fresh_master.fx_mode, FXMode.HIGH_PASS)

    def test_the_filter_mode_comes_back_as_the_mode_not_as_a_number(self):
        """A restored `0` would compare unequal to FXMode.LOW_PASS and the
        filter would be told to be both at once."""
        channels, master = a_rig()
        master.fx_mode = FXMode.HIGH_PASS
        written = json.loads(json.dumps(state_of(channels, master)))

        fresh_channels, fresh_master = a_rig()
        apply_state(written, fresh_channels, fresh_master)
        self.assertIsInstance(fresh_master.fx_mode, FXMode)

    def test_it_survives_a_json_round_trip(self):
        channels, master = a_rig()
        channels[1].toggle_fx = True
        json.dumps(state_of(channels, master))   # raises if it is not data


class WhatTheFileMayNotKnow(unittest.TestCase):
    """A file outlives the version that wrote it. Every one of these is a
    thing that must not raise and must not clear a value nobody asked about."""

    def test_a_missing_field_leaves_the_default_alone(self):
        channels, master = a_rig()
        channels[0].toggle_pfl = True
        apply_state({"channels": [{"toggle_fx": True}]}, channels, master)
        self.assertTrue(channels[0].toggle_fx)
        self.assertTrue(channels[0].toggle_pfl)

    def test_a_field_the_module_does_not_know_is_ignored(self):
        channels, master = a_rig()
        apply_state({"channels": [{"toggle_fx": True, "nonsense": 1}]},
                    channels, master)
        self.assertFalse(hasattr(channels[0], "nonsense"))

    def test_a_file_from_a_device_with_more_channels_is_truncated(self):
        channels, master = a_rig(channels=2)
        apply_state({"channels": [{"toggle_fx": True}] * 8}, channels, master)
        self.assertTrue(all(c.toggle_fx for c in channels))

    def test_a_file_from_a_device_with_fewer_channels_leaves_the_rest(self):
        channels, master = a_rig(channels=4)
        apply_state({"channels": [{"toggle_fx": True}]}, channels, master)
        self.assertTrue(channels[0].toggle_fx)
        self.assertFalse(channels[3].toggle_fx)

    def test_an_empty_state_changes_nothing(self):
        channels, master = a_rig()
        before = (tuple(channels), master.fx_mode)
        apply_state({}, channels, master)
        self.assertEqual((tuple(channels), master.fx_mode), before)

    def test_a_filter_mode_that_is_not_one_is_left_alone(self):
        channels, master = a_rig()
        apply_state({"fx_mode": 99}, channels, master)
        self.assertEqual(master.fx_mode, FXMode.LOW_PASS)


class WhatIsKeptAndWhatIsNot(unittest.TestCase):
    """Which of ChannelInfo's continuous values survive a restart, and why
    they are not all the same answer.

    `azimuth` and `elevation` are written now -- the position, which only Core
    can answer a recall with. Keeping them out is a decision: a trajectory
    changes the position continuously, so remembering it would write this file
    every DEFAULT_DELAY seconds for a whole set. The plugins hold the position
    and the project saves it; a cold Core loses only its ability to say so.

    `three_d` is kept, and the difference is the rate: a knob changes when a
    hand turns it. Nothing else holds it either -- it reaches REAPER as two
    gains and cannot be read back -- so a cold Core that forgot it would have
    nothing to answer with at all.

    `width` is out for the original reason -- nothing assigns it, and
    send_elevation(), the one reader of either, is never called.

    Pinned rather than merely left out: the way this goes wrong is somebody
    adding the position here for the obvious-looking reason and not noticing
    what it costs. See issues/a3-core-elevation-cache-ist-tot.md and
    issues/a3-core-position-hat-keinen-rueckweg-und-keinen-halter.md."""

    def test_the_position_is_not_remembered_across_a_restart(self):
        from a3_core_state import CHANNEL_FIELDS
        self.assertNotIn("azimuth", CHANNEL_FIELDS)
        self.assertNotIn("elevation", CHANNEL_FIELDS)

    def test_the_crossfade_is_remembered_and_that_is_not_a_contradiction(self):
        """3d is kept although the position is not, and the difference is
        how often it changes.

        A trajectory moves the position continuously -- keeping it would
        mean writing this file every DEFAULT_DELAY seconds for a whole set.
        3d is a knob: it changes when a hand turns it, which is the same
        rate as the three toggles that have always been kept here.
        """
        from a3_core_state import CHANNEL_FIELDS
        self.assertIn("three_d", CHANNEL_FIELDS)

    def test_the_dead_cache_is_not_remembered(self):
        from a3_core_state import CHANNEL_FIELDS
        self.assertNotIn("width", CHANNEL_FIELDS)


class TheFileItself(unittest.TestCase):
    def setUp(self):
        self._dir = TemporaryDirectory()
        self.path = Path(self._dir.name) / "state.json"
        self.addCleanup(self._dir.cleanup)

    def test_a_file_that_is_not_there_is_an_empty_state(self):
        self.assertEqual(StateFile(self.path).load(), {})

    def test_a_file_that_is_not_json_is_an_empty_state(self):
        """A half-written file from a crash reads as nothing rather than
        stopping Core from starting. Nothing here is worth a device that will
        not come up."""
        self.path.write_text("{ this is not")
        self.assertEqual(StateFile(self.path).load(), {})

    def test_what_was_written_is_what_is_read(self):
        state = StateFile(self.path, delay=0.01)
        state.remember({"fx_mode": 1})
        state.flush()
        self.assertEqual(StateFile(self.path).load(), {"fx_mode": 1})

    def test_the_directory_is_made_if_it_is_not_there(self):
        path = Path(self._dir.name) / "deeper" / "state.json"
        state = StateFile(path, delay=0.01)
        state.remember({"fx_mode": 1})
        state.flush()
        self.assertTrue(path.exists())

    def test_nothing_is_left_beside_the_file(self):
        """The write goes to a temporary name and is moved into place, so a
        crash mid-write leaves the old file whole rather than half of a new
        one. What must not happen is a stray temporary left behind."""
        state = StateFile(self.path, delay=0.01)
        state.remember({"fx_mode": 1})
        state.flush()
        self.assertEqual([p.name for p in self.path.parent.iterdir()],
                         [self.path.name])

    def test_a_burst_of_changes_is_one_write(self):
        """A hand sweeping the filter knob is one intention. Writing per
        change would put a file write in the path of every OSC message.

        Die beiden Behauptungen unten sagen das vollständig und ohne Uhr:
        **ein** Schreibvorgang für zwanzig Änderungen, und er trägt den
        **letzten** Wert -- ein eiliger erster Schreibvorgang wäre damit
        ebenso ausgeschlossen wie zwanzig.

        Dazwischen stand bis zum 2026-09-21 ein dritter Satz:
        `assertFalse(self.path.exists(), "wrote before the delay was up")`.
        Der behauptete, dass in *diesem Augenblick* die fünfzig Millisekunden
        noch nicht um seien -- eine Wette auf die Wanduhr, die auf einer Kiste
        verlorengeht, die nebenher baut. Genau so ist er am 2026-09-21 in
        einem Lauf von 411 Tests umgefallen und in fünf Läufen danach nicht.
        Er hat nichts geprüft, was die anderen beiden nicht prüfen, und die
        Zeitspanne war das Einzige, was er behauptet hat."""
        state = StateFile(self.path, delay=0.05)
        for value in range(20):
            state.remember({"fx_mode": value})

        deadline = time.monotonic() + 2.0
        while not self.path.exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertEqual(state.writes, 1)
        self.assertEqual(StateFile(self.path).load(), {"fx_mode": 19})

    def test_the_same_state_again_does_not_restart_the_clock(self):
        """Every /channel/* message offers a state, and almost none of them
        change one -- a fader sweep is hundreds. Restarting the clock on each
        would push the write past the end of the sweep every time, so a state
        equal to the last one offered is not a change."""
        state = StateFile(self.path, delay=0.05)
        state.remember({"fx_mode": 1})
        for _ in range(50):                      # half a second of sweeping
            time.sleep(0.01)
            state.remember({"fx_mode": 1})

        # Written long before the sweep ended, because the sweep changed
        # nothing. Restarting the clock would put the write 0.05s after this
        # loop instead of 0.05s after the one change.
        self.assertTrue(self.path.exists(),
                        "the clock was restarted by an unchanged state")
        self.assertEqual(state.writes, 1)

    def test_flush_writes_nothing_when_nothing_changed(self):
        state = StateFile(self.path, delay=0.01)
        state.flush()
        self.assertEqual(state.writes, 0)
        self.assertFalse(self.path.exists())


if __name__ == "__main__":
    unittest.main()
