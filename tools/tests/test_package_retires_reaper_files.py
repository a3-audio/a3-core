# SPDX-FileCopyrightText: 2026 Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""An upgrade takes REAPER's and qjackctl's copies in ~/.config out of the way.

The package no longer ships them, but an install never removes a file from
~/.config. A drop-in there that still calls ~/.local/bin/a3-engine-runs --
which dpkg deletes with the package -- would fail its ExecCondition and keep
deck A from starting. So the postinst disables a3-reaper, qjackctl and
a3-user-install (their links in the .wants folders), moves the units, their
drop-in folders, every drop-in that names the helper and the patchbay to the
backup -- not deleted -- and names each one; what still runs is stopped once
aaa's manager is up. REAPER's own files (~/.config/REAPER, ~/.local/opt,
~/.local/vst, ~/.local/clap) are the maintainer's to delete by hand, never the
package's.

The fake home holds what a3nuc1 had on 2026-10-27.
"""

import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.paths import DEBIAN
from tools.tests.test_package_retires_jack_and_zita import postinst_function

POSTINST = DEBIAN / "postinst"

RETIRED = ("a3-reaper.service", "qjackctl.service", "a3-user-install.service")
HELPER = "/home/aaa/.local/bin/a3-engine-runs"

#: Relative to ~/.config: everything that is to go to the backup.
MOVED = (
    "systemd/user/a3-reaper.service",
    "systemd/user/qjackctl.service",
    "systemd/user/a3-user-install.service",
    "systemd/user/a3-reaper.service.d",
    "systemd/user/qjackctl.service.d",
    "systemd/user/stemdeck.service.d/audio-engine.conf",
    "systemd/user/stemdeck@.service.d/audio-engine.conf",
    "rncbc.org/a3-patchbay.xml",
    "rncbc.org/QjackCtl.conf",
)

#: Relative to ~/.config: what stays where it is.
KEPT = (
    "systemd/user/a3-main.service",
    "systemd/user/a3-core.service.d/override.conf",
    "systemd/user/stemdeck.service.d/a3-core.conf",
    "REAPER/x",
)


class TheRigsCopiesAreRetired(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.home = Path(self.tmp.name) / "home"
        self.config = self.home / ".config"
        self.units = self.config / "systemd/user"
        self.backup = self.config / "a3-replaced/now"
        self.build_a3nuc1()

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, relative, text):
        path = self.config / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def link(self, wants, unit):
        folder = self.units / wants
        folder.mkdir(parents=True, exist_ok=True)
        (folder / unit).symlink_to(self.units / unit)

    def build_a3nuc1(self):
        self.put("systemd/user/a3-reaper.service", "[Service]\nExecStart=%h/.local/opt/REAPER/reaper\n")
        self.put("systemd/user/qjackctl.service", "[Service]\nExecStart=/usr/bin/qjackctl -a a3-patchbay.xml\n")
        self.put("systemd/user/a3-user-install.service", "[Service]\nExecStart=%h/.local/share/a3-core/recipes/user_install.sh\n")
        self.put("systemd/user/a3-reaper.service.d/audio-engine.conf",
                 f"[Service]\nExecCondition={HELPER} --reaper\n")
        (self.units / "qjackctl.service.d").mkdir()
        self.put("systemd/user/stemdeck.service.d/audio-engine.conf",
                 f"[Service]\nExecCondition={HELPER} --reaper\n")
        self.put("systemd/user/stemdeck@.service.d/audio-engine.conf",
                 f"[Service]\nExecCondition={HELPER}\n")
        self.put("systemd/user/stemdeck.service.d/a3-core.conf", "[Unit]\nBindsTo=pipewire.service\n")
        self.put("systemd/user/a3-core.service.d/override.conf", "[Service]\nNice=-5\n")
        self.put("systemd/user/a3-main.service", "[Unit]\nWants=a3-core.service\n")
        self.link("default.target.wants", "a3-main.service")
        self.link("default.target.wants", "a3-reaper.service")
        self.link("a3-main.service.wants", "qjackctl.service")
        self.link("default.target.wants", "a3-user-install.service")
        self.put("rncbc.org/a3-patchbay.xml", "<patchbay/>\n")
        self.put("rncbc.org/QjackCtl.conf", "[Settings]\n")
        self.put("REAPER/x", "REAPER's own\n")
        for own in (".local/opt/REAPER/reaper", ".local/vst/TAL-Filter-2.vst3/x",
                    ".local/clap/airwindows.clap"):
            (self.home / own).parent.mkdir(parents=True, exist_ok=True)
            (self.home / own).write_text("his\n")

    def retire(self, check=True):
        return subprocess.run(
            ["sh", "-c", f"set -e\n{postinst_function('retire_reaper_files')}\n"
             'RETIRED_UNITS=""\nretire_reaper_files "$1" "$2"\n'
             'echo "retired:$RETIRED_UNITS"\necho went-on', "sh",
             str(self.config), str(self.backup)],
            capture_output=True, text=True, check=check)

    def retired_units(self, stdout):
        return stdout.split("retired:", 1)[1].split("\n", 1)[0].split()

    def test_everything_goes_to_the_backup_keeping_its_path_and_is_named(self):
        before = {rel: (self.config / rel) for rel in MOVED}
        texts = {rel: p.read_text() for rel, p in before.items() if p.is_file()}
        done = self.retire()
        for rel in MOVED:
            with self.subTest(path=rel):
                self.assertFalse((self.config / rel).exists())
                self.assertTrue((self.backup / rel).exists())
                if rel in texts:
                    self.assertEqual(texts[rel], (self.backup / rel).read_text())
                self.assertIn(f"~/.config/{rel}", done.stdout)
        self.assertTrue((self.backup / "systemd/user/a3-reaper.service.d/audio-engine.conf").is_file())

    def test_one_line_per_moved_path(self):
        done = self.retire()
        named = [l for l in done.stdout.splitlines() if l.startswith("a3-core: moved")]
        self.assertEqual(len(MOVED), len(named), done.stdout)

    def test_the_units_are_disabled_and_listed_to_be_stopped(self):
        done = self.retire()
        for wants in ("default.target.wants", "a3-main.service.wants"):
            for unit in RETIRED:
                self.assertFalse((self.units / wants / unit).is_symlink(), f"{wants}/{unit}")
        self.assertEqual(sorted(RETIRED), sorted(self.retired_units(done.stdout)))

    def test_what_is_not_reapers_stays(self):
        texts = {rel: (self.config / rel).read_text() for rel in KEPT}
        self.retire()
        for rel, text in texts.items():
            with self.subTest(path=rel):
                self.assertEqual(text, (self.config / rel).read_text())
        self.assertTrue((self.units / "default.target.wants/a3-main.service").is_symlink())

    def test_reapers_own_files_in_the_home_are_never_touched(self):
        self.retire()
        self.assertEqual("REAPER's own\n", (self.config / "REAPER/x").read_text())
        for own in (".local/opt/REAPER/reaper", ".local/vst/TAL-Filter-2.vst3/x",
                    ".local/clap/airwindows.clap"):
            self.assertEqual("his\n", (self.home / own).read_text(), own)
        self.assertFalse((self.backup / "REAPER").exists())

    def test_a_second_run_moves_nothing(self):
        self.retire()
        self.backup = self.config / "a3-replaced/later"
        done = self.retire()
        self.assertEqual("retired:\nwent-on\n", done.stdout)
        self.assertFalse(self.backup.exists())

    def test_a_failed_move_warns_and_the_install_goes_on(self):
        self.backup.parent.mkdir(parents=True)
        self.backup.write_text("a file where the backup folder should be")
        done = self.retire(check=False)
        self.assertEqual(0, done.returncode, done.stderr)
        self.assertIn("went-on", done.stdout)
        self.assertTrue((self.units / "a3-reaper.service").exists())
        self.assertIn("could not", done.stderr)
        self.assertEqual([], self.retired_units(done.stdout))


class ThePostinstCallsIt(unittest.TestCase):
    def test_right_after_jackd_and_zita_and_before_the_units_are_stopped(self):
        lines = POSTINST.read_text().splitlines()
        jack = lines.index('retire_jack_and_zita_units "${USER_HOME}/.config" "$CONFIG_BACKUP"')
        self.assertEqual('retire_reaper_files "${USER_HOME}/.config" "$CONFIG_BACKUP"',
                         lines[jack + 1])
        self.assertIn("stop_retired_units", postinst_function("run_user_install"))


if __name__ == "__main__":
    unittest.main()
