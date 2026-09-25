"""Der Abend auf der Platte, damit ein Stromausfall ihn nicht kostet.

Gewünscht am 2026-09-18: *„core soll selbst mitschreiben und beim start
zurückspielen."* REAPER startet hier aus einer Vorlage und hat kein
Projektfile, das man still speichern könnte -- aber Core sieht jeden Wert, der
durchläuft (`Relayed`), und kann ihn beim Start denselben Weg zurückschicken,
den eine Nachricht vom Pult nimmt.

Was **nicht** zurückgespielt wird, ist der eigentliche Inhalt dieser Datei:
Lampen sind Status und keine Einstellung; die Schalter und der Crossfade
stehen schon in a3_core_state; und eine Position ist das Einzige, was sich
ohne Hand ändert -- eine gespeicherte Position beim Start zurückzuspielen
würde den Klang an eine Stelle setzen, an der ihn niemand haben wollte.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_evening import evening_state, replayable   # noqa: E402


class Relayed:
    def __init__(self, values):
        self._values = dict(values)

    def messages(self):
        return iter(self._values.items())


class WhatIsWrittenDown(unittest.TestCase):
    def test_it_is_what_was_passed_on(self):
        state = evening_state(Relayed({"/channel/0/gain": 0.5,
                                       "/master/volume": 0.8}))
        self.assertEqual(state["values"]["/channel/0/gain"], 0.5)
        self.assertEqual(state["values"]["/master/volume"], 0.8)

    def test_an_empty_evening_is_an_empty_file(self):
        self.assertEqual(evening_state(Relayed({}))["values"], {})


class WhatIsPlayedBack(unittest.TestCase):
    def replay(self, values):
        return dict(replayable({"values": values}))

    def test_the_continuous_values_come_back(self):
        played = self.replay({"/channel/0/gain": 0.5,
                              "/channel/2/eq/high": 0.25,
                              "/channel/1/volume": 0.7,
                              "/master/volume": 0.8,
                              "/fx/frequency": 0.33})
        self.assertEqual(len(played), 5)

    def test_lamps_are_status_and_not_settings(self):
        played = self.replay({"/channel/0/led/pfl": 1.0, "/fx/led": "low_pass"})
        self.assertEqual(played, {})

    def test_what_cores_own_state_file_already_keeps_is_left_out(self):
        # The toggles, the crossfade and the filter mode survive a restart in
        # a3_core_state; a second copy here could disagree with it.
        played = self.replay({"/channel/0/pfl": 1.0, "/channel/0/fx": 1.0,
                              "/channel/3/3d": 0.5, "/fx/mode": 1})
        self.assertEqual(played, {})

    def test_a_position_is_never_played_back(self):
        # It moves without a hand: a trajectory writes it continuously. Put
        # back at startup it would place the sound somewhere nobody chose.
        played = self.replay({"/channel/0/azimuth": 90.0,
                              "/channel/0/elevation": 10.0})
        self.assertEqual(played, {})

    def test_an_address_core_does_not_set_is_left_alone(self):
        played = self.replay({"/vu/3": 0.2, "/beat": 1, "/state/recall": 1})
        self.assertEqual(played, {})

    def test_a_file_from_another_version_does_not_raise(self):
        self.assertEqual(dict(replayable({})), {})
        self.assertEqual(dict(replayable({"values": None})), {})


class TheEngineActuallyDoesIt(unittest.TestCase):
    def test_every_value_passed_on_is_written_down(self):
        """relay() -- the desk's and Motion's values, and the replay itself --
        noted what it passed on but never wrote evening.json; only REAPER's
        reports did, as a side effect. With REAPER quiet a knob turned at the
        desk never reached the file, and after a replay the file kept what
        REAPER's template had said (device test, 2026-09-26)."""
        source = (PACKAGE / "bin/a3-core.py").read_text()
        self.assertEqual(1, source.count("_evening_file.remember("))
        for name in ("def broadcast(", "def relay("):
            body = source.split(name, 1)[1].split("\ndef ", 1)[0]
            self.assertIn("note_passed_on(", body, name)
            self.assertNotIn("_relayed.note(", body, name)

    def test_a3_core_writes_and_replays(self):
        source = (PACKAGE / "bin/a3-core.py").read_text()
        self.assertIn("evening_state(", source)
        self.assertIn("replay_evening(\n", source)


if __name__ == "__main__":
    unittest.main()
