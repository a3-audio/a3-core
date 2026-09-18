"""Zwischendurch sichern, damit ein Stromausfall nicht den Abend kostet.

Gewünscht am 2026-09-18: *„zwischendurch snapshots sichern falls strom
ausfällt."* Cores eigener Stand wird zwei Sekunden nach jeder Änderung
geschrieben (a3_core_state); was ein Stromausfall wirklich kostet, ist das
REAPER-Projekt -- Gains, EQ, Positionen. Core kann REAPER das Speichern
auslösen, und die Frage ist nur, wann.

Zwei Regeln: nur wenn sich seit dem letzten Mal etwas bewegt hat, und
höchstens alle `interval` Sekunden. Ein Speichern kostet REAPER Arbeit im
laufenden Betrieb, und ein stilles Pult soll gar keins auslösen.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_snapshot import Snapshot   # noqa: E402


class WhenASnapshotIsDue(unittest.TestCase):
    def test_an_untouched_rig_is_never_saved(self):
        snapshot = Snapshot(interval=300, now=0.0)
        self.assertFalse(snapshot.due(1000.0))

    def test_a_change_waits_for_the_interval(self):
        snapshot = Snapshot(interval=300, now=0.0)
        snapshot.changed()
        self.assertFalse(snapshot.due(299.0))
        self.assertTrue(snapshot.due(300.0))

    def test_saving_starts_the_clock_again(self):
        snapshot = Snapshot(interval=300, now=0.0)
        snapshot.changed()
        snapshot.saved(300.0)
        self.assertFalse(snapshot.due(1000.0), "nothing has moved since")

        snapshot.changed()
        self.assertFalse(snapshot.due(500.0))
        self.assertTrue(snapshot.due(600.0))

    def test_a_burst_of_changes_is_still_one_save(self):
        # A fader sweep is hundreds of messages and one intention.
        snapshot = Snapshot(interval=300, now=0.0)
        for _ in range(500):
            snapshot.changed()
        self.assertTrue(snapshot.due(300.0))
        snapshot.saved(300.0)
        self.assertFalse(snapshot.due(600.0))


class WhatItAsksFor(unittest.TestCase):
    def test_the_action_is_reapers_save_project(self):
        self.assertEqual(Snapshot.SAVE_ACTION, "/action/40026")


class TheEngineActuallyUsesIt(unittest.TestCase):
    def test_a3_core_saves_the_project_in_between(self):
        source = (PACKAGE / "bin/a3-core.py").read_text()
        self.assertIn("Snapshot", source)
        self.assertIn("SAVE_ACTION", source)

    def test_it_is_off_until_somebody_asks_for_it(self):
        # REAPER is started from a template here and has no project file, so
        # "File: Save project" opens a Save-As dialog over the panel instead
        # of saving. Found on 2026-09-18, before the thread had ever fired.
        source = (PACKAGE / "bin/a3-core.py").read_text()
        self.assertIn('"--save-project", action="store_true"', source)
        self.assertIn("if args.save_project:", source)


if __name__ == "__main__":
    unittest.main()
