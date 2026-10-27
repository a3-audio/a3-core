# SPDX-FileCopyrightText: 2026 Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""REAPER is obsolete (maintainer, 2026-10-27), and qjackctl with it.

The a3-core program is the Core and its engine renders the sound field, so
REAPER's template, OSC map, presets and unit, the IEM settings of the VST3s it
hosted, the recipe that downloaded it, the start-condition helper that kept it
off, and the qjackctl patchbay whose every cable had a REAPER end all leave
the package. What was measured from REAPER stays as test data under tests/
(curves, port maps, the parity corpus); none of it ships.
"""

import re
import subprocess
import unittest

from tools.tests.paths import DEBIAN, LOCAL, PACKAGE, ROOT

UNITS = LOCAL / "share/a3-core/config/systemd/user"
SYSTEM_UNITS = PACKAGE / "etc/systemd/system"
#: The program's own units, laid into the package by packaging/stage.
PROGRAM_UNITS = (ROOT / "packaging/a3-engine.service",
                 ROOT / "packaging/a3-core-window.service")
I3_CONFIG = LOCAL / "share/a3-core/config/i3/config"

GONE_FROM_PATHS = ("rncbc.org", "config/IEM", "a3-engine-runs", "user_install.sh",
                   "a3-user-install.service", "layout.json", "curves-golden.json")
GONE_FROM_UNITS = ("a3-engine-runs", "a3-reaper", "qjackctl")
GONE_FROM_DEPENDS = ("qjackctl", "iem-plugin-suite-vst3", "unzip")

A3_MAIN_WANTS = ["a3-engine.service", "a3-core.service", "a3-core-window.service",
                 "beat-analyzer.service", "a3-motion.service", "stemdeck@A.service"]


def shipped_paths():
    """The payload as git has it: what dpkg unpacks."""
    names = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "-z", "--",
         str(PACKAGE.relative_to(ROOT))],
        capture_output=True, check=True).stdout.decode()
    return [n for n in names.split("\0") if n]


def depends():
    text = (DEBIAN / "control").read_text()
    field = re.search(r"^Depends:(.*)$", text, re.MULTILINE).group(1)
    return [e.strip().split()[0] for e in field.split(",")]


def unit_and_drop_in_files():
    """The links in the .wants folders point into the machine's own ~/.config,
    not into the package: left out."""
    found = [*UNITS.rglob("*.service"), *UNITS.rglob("*.conf"),
             *SYSTEM_UNITS.rglob("*.service"), *SYSTEM_UNITS.rglob("*.conf")]
    return [p for p in found if not p.is_symlink()] + list(PROGRAM_UNITS)


class NothingOfReaperShips(unittest.TestCase):
    def test_no_shipped_path_names_reaper_or_its_helpers(self):
        paths = shipped_paths()
        self.assertTrue(paths, "git lists no payload")
        for path in paths:
            with self.subTest(path=path):
                self.assertNotRegex(path, re.compile("reaper", re.IGNORECASE))
                for gone in GONE_FROM_PATHS:
                    self.assertNotIn(gone, path)

    def test_no_unit_or_drop_in_names_reaper_qjackctl_or_the_helper(self):
        for unit in unit_and_drop_in_files():
            with self.subTest(unit=unit.relative_to(ROOT)):
                text = unit.read_text()
                for gone in GONE_FROM_UNITS:
                    self.assertNotIn(gone, text)

    def test_the_track_names_of_reaper_are_gone(self):
        self.assertFalse((ROOT / "tools/reaper-track-names.json").exists())


class NothingOfReaperIsDependedOn(unittest.TestCase):
    def test_depends_lacks_what_only_reaper_and_qjackctl_needed(self):
        listed = depends()
        for gone in GONE_FROM_DEPENDS:
            self.assertNotIn(gone, listed)


class TheRigStartsWithoutThem(unittest.TestCase):
    def test_a3_main_wants_the_program_the_analyzer_motion_and_deck_a(self):
        wants = [l.split("=", 1)[1].split()
                 for l in (UNITS / "a3-main.service").read_text().splitlines()
                 if l.startswith("Wants=")]
        self.assertEqual([A3_MAIN_WANTS], wants)

    def test_i3_places_no_reaper_or_qjackctl_window(self):
        text = I3_CONFIG.read_text()
        self.assertNotIn('class="REAPER"', text)
        self.assertNotIn('class="QjackCtl"', text)


if __name__ == "__main__":
    unittest.main()
