# SPDX-FileCopyrightText: 2026 Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""The start-time rule is wired into the units without touching REAPER's."""

import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UNITS = (ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
         / "share/a3-core/config/systemd/user")
REAPER_UNIT = UNITS / "a3-reaper.service"
DROP_IN = UNITS / "a3-reaper.service.d" / "audio-engine.conf"
MAIN_UNIT = UNITS / "a3-main.service"
TWO_DECK_DROP_IN = UNITS / "stemdeck.service.d" / "audio-engine.conf"
DECK_DROP_IN = UNITS / "stemdeck@.service.d" / "audio-engine.conf"


def unit_on_main():
    relative = REAPER_UNIT.relative_to(ROOT).as_posix()
    return subprocess.run(["git", "-C", str(ROOT), "show", f"main:{relative}"],
                          capture_output=True, check=True).stdout


def drop_in_lines(path):
    return [l for l in path.read_text().splitlines()
            if l.strip() and not l.startswith("#")]


class BootChoice(unittest.TestCase):
    def test_reaper_unit_is_unchanged_and_conditioned_by_a_drop_in(self):
        self.assertEqual(unit_on_main(), REAPER_UNIT.read_bytes())
        lines = [l for l in DROP_IN.read_text().splitlines()
                 if l.strip() and not l.startswith("#")]
        self.assertEqual(
            ["[Service]",
             "ExecCondition=/home/aaa/.local/bin/a3-engine-runs --reaper"],
            lines)

    def test_a3_main_wants_the_engine(self):
        wants = [l.split("=", 1)[1].split() for l in MAIN_UNIT.read_text().splitlines()
                 if l.startswith("Wants=")]
        self.assertIn("a3-engine.service", sum(wants, []))

    def test_the_two_deck_stemdeck_runs_only_in_reaper_mode(self):
        self.assertEqual(
            ["[Service]",
             "ExecCondition=/home/aaa/.local/bin/a3-engine-runs --reaper"],
            drop_in_lines(TWO_DECK_DROP_IN))

    def test_the_b_format_decks_run_only_when_the_engine_does(self):
        self.assertEqual(
            ["[Service]",
             "ExecCondition=/home/aaa/.local/bin/a3-engine-runs"],
            drop_in_lines(DECK_DROP_IN))

    def test_a3_main_wants_deck_a_only(self):
        wants = sum([l.split("=", 1)[1].split() for l in MAIN_UNIT.read_text().splitlines()
                     if l.startswith("Wants=")], [])
        self.assertIn("stemdeck@A.service", wants)
        self.assertNotIn("stemdeck@B.service", wants)   # B runs on a3nuc2


if __name__ == "__main__":
    unittest.main()
