# SPDX-FileCopyrightText: 2026 Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""What starts at boot, now that REAPER is gone (2026-10-27).

There is no choice left between REAPER and the engine: a3-main wants the
engine and the B-format deck A, and they start unconditioned. The two-deck
StemDeck (stemdeck.service, enabled globally by its own package) must still
stay off on a Core; a drop-in with a new name says so, so that the postinst
installs it unasked on a rig that already has the folder. No Conflicts=: a
conflict between units in one transaction drops a start job before any
condition runs (2026-10-27)."""

import unittest

from tools.tests.paths import LOCAL

UNITS = LOCAL / "share/a3-core/config/systemd/user"
MAIN_UNIT = UNITS / "a3-main.service"
TWO_DECK_OFF = UNITS / "stemdeck.service.d" / "two-deck-off.conf"


def drop_in_lines(path):
    return [l for l in path.read_text().splitlines()
            if l.strip() and not l.startswith("#")]


def main_wants():
    return sum([l.split("=", 1)[1].split() for l in MAIN_UNIT.read_text().splitlines()
                if l.startswith("Wants=")], [])


class BootChoice(unittest.TestCase):
    def test_the_two_deck_stemdeck_never_starts_on_a_core(self):
        self.assertEqual(["[Service]", "ExecCondition=/bin/false"],
                         drop_in_lines(TWO_DECK_OFF))

    def test_the_drop_in_says_why_and_carries_no_conflict(self):
        text = TWO_DECK_OFF.read_text()
        self.assertIn("stemdeck@", text)
        self.assertNotIn("Conflicts=", "\n".join(drop_in_lines(TWO_DECK_OFF)))

    def test_the_b_format_decks_run_unconditioned(self):
        self.assertFalse((UNITS / "stemdeck@.service.d").exists())

    def test_a3_main_wants_the_engine(self):
        self.assertIn("a3-engine.service", main_wants())

    def test_a3_main_wants_the_window(self):
        # The unit itself ships from packaging/, beside a3-engine.service.
        self.assertIn("a3-core-window.service", main_wants())

    def test_a3_main_wants_deck_a_only(self):
        wants = main_wants()
        self.assertIn("stemdeck@A.service", wants)
        self.assertNotIn("stemdeck@B.service", wants)   # B runs on a3nuc2
        self.assertNotIn("stemdeck.service", wants)


if __name__ == "__main__":
    unittest.main()
