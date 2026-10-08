"""The shipped template starts silent (a3-system#74).

main, booth and phones -- the tracks the speakers and headphones hang on --
and main_vu, the main meter's track, are muted; Core opens them after its
start-up recall, so the desk's main meter is dark while the room is silent. rec feeds the analyzer's
bpm input and stays open, and no other track is muted. A template saved by
hand with the gate open goes red here.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_layout import load_layout   # noqa: E402

TEMPLATE = PACKAGE / "share/a3-core/config/REAPER/ProjectTemplates/a3-reaper.RPP"


def mute_states(path):
    """{track name: first MUTESOLO field} from a track's own lines."""
    states, name = {}, None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("  <TRACK"):
            name = None
        elif line.startswith("    NAME "):
            name = line[len("    NAME "):].strip().strip('"')
        elif line.startswith("    MUTESOLO ") and name is not None:
            states.setdefault(name, line.split()[1])
    return states


class TheTemplateStartsSilent(unittest.TestCase):
    def setUp(self):
        self.states = mute_states(TEMPLATE)

    def test_main_booth_phones_and_the_main_meter_are_muted(self):
        for name in ("main", "booth", "phones", "main_vu"):
            with self.subTest(name=name):
                self.assertEqual(self.states[name], "1")

    def test_rec_stays_open(self):
        self.assertEqual(self.states["rec"], "0")

    def test_no_other_track_is_muted(self):
        muted = {name for name, state in self.states.items() if state != "0"}
        self.assertEqual(muted, {"main", "booth", "phones", "main_vu"})

    def test_the_muted_tracks_are_the_layouts_gate(self):
        layout = load_layout(PACKAGE / "share/a3-core/layout.json")
        muted = {name for name, state in self.states.items() if state != "0"}
        self.assertEqual(muted, set(layout.gate))


if __name__ == "__main__":
    unittest.main()
