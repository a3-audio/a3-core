"""Was Core beim Start aussprechen muss, damit alle dasselbe meinen.

Gemeldet am 2026-09-18: *„a3-core: fx aktiv obwohl beim runterfahren
inaktiv."* Core merkt sich die Schalter einer Sitzung (a3_core_state), sagt
REAPER aber nur Bescheid, wenn einer sich *ändert* -- `set_filters()` hing
allein am Umschalten. Nach einem Start behält REAPER also den Bypass-Zustand
aus seinem Projekt, und der Filter kann hörbar an sein, während Core „aus"
denkt und die Lampe dunkel ist.

Die Regel steht hier, damit sie geprüft werden kann, ohne a3-core.py zu
importieren -- das öffnet Sockets.
"""

import sys
import unittest
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_startup import (filter_bypass_messages,   # noqa: E402
                             pfl_mute_messages,
                             remembered_reaper_messages)

HIPASS = 3
LOPASS = 4


@dataclass
class Channel:
    track_input: int
    track_pfl: int
    toggle_fx: bool = False
    toggle_pfl: bool = False


def channels(*toggles):
    return [Channel(track_input=10 + i, track_pfl=20 + i, toggle_fx=fx,
                    toggle_pfl=pfl)
            for i, (fx, pfl) in enumerate(toggles)]


class TheFilterPlugins(unittest.TestCase):
    def test_a_channel_without_fx_has_both_plugins_bypassed(self):
        messages = dict(filter_bypass_messages(channels((False, False)),
                                               "low_pass", HIPASS, LOPASS))
        self.assertEqual(messages["/track/10/fx/3/bypass"], 0.0)
        self.assertEqual(messages["/track/10/fx/4/bypass"], 0.0)

    def test_the_mode_says_which_plugin_runs(self):
        low = dict(filter_bypass_messages(channels((True, False)),
                                          "low_pass", HIPASS, LOPASS))
        self.assertEqual(low["/track/10/fx/4/bypass"], 1.0,
                         "low pass runs the lopass plugin")
        self.assertEqual(low["/track/10/fx/3/bypass"], 0.0)

        high = dict(filter_bypass_messages(channels((True, False)),
                                           "high_pass", HIPASS, LOPASS))
        self.assertEqual(high["/track/10/fx/3/bypass"], 1.0)
        self.assertEqual(high["/track/10/fx/4/bypass"], 0.0)

    def test_every_channel_is_spoken_for(self):
        messages = list(filter_bypass_messages(
            channels((True, False), (False, False), (True, False)),
            "low_pass", HIPASS, LOPASS))
        self.assertEqual(len(messages), 6)


class ThePflMutes(unittest.TestCase):
    def test_pfl_off_is_a_muted_track(self):
        messages = dict(pfl_mute_messages(channels((False, False))))
        self.assertEqual(messages["/track/20/mute"], 1.0)

    def test_pfl_on_unmutes_it(self):
        messages = dict(pfl_mute_messages(channels((False, True))))
        self.assertEqual(messages["/track/20/mute"], 0.0)


class WhatAStartSays(unittest.TestCase):
    def test_it_is_the_filters_and_the_mutes_together(self):
        both = list(remembered_reaper_messages(channels((True, True)),
                                               "low_pass", HIPASS, LOPASS))
        addresses = [address for address, _ in both]
        self.assertIn("/track/10/fx/4/bypass", addresses)
        self.assertIn("/track/20/mute", addresses)

    def test_nothing_is_said_twice(self):
        both = list(remembered_reaper_messages(
            channels((True, True), (False, False)), "high_pass", HIPASS,
            LOPASS))
        addresses = [address for address, _ in both]
        self.assertEqual(len(addresses), len(set(addresses)))


class TheEngineActuallySaysIt(unittest.TestCase):
    """A rule with no caller changes nothing -- and this one is the fix."""

    def test_a3_core_speaks_the_remembered_state_at_startup(self):
        source = (PACKAGE / "bin/a3-core.py").read_text()
        # The call, not the import: an import alone says nothing.
        self.assertIn("speak_remembered_state()", source)
        self.assertIn("remembered_reaper_messages(", source)


if __name__ == "__main__":
    unittest.main()
