"""An upgrade takes the old jackd and zita units out of the way (2026-10-14).

The package no longer ships a3-jack, zita-j2n or zita-n2j, but an install
never removes a file from ~/.config: the old units would stay there, enabled,
and an old a3-main or a unit bound to a3-jack would start jackd and take the
sound card away from PipeWire (what happened on the rig on 2026-10-13). So the
postinst disables them -- the links in the .wants folders, what
`systemctl --user disable` removes -- moves the unit files to the backup (not
deleted) and stops what still runs once aaa's manager is up.

Only the Core's own units: StemDeck shipped zita units of the same names
that run its zita-from-truth.py; those are StemDeck's to retire, kept and
named.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.paths import DEBIAN

POSTINST = DEBIAN / "postinst"

CORE_UNITS = {
    "a3-jack.service": "[Service]\nExecStart=/usr/bin/jackd -P 80 -dalsa -dhw:USB\n",
    "zita-j2n.service": "[Service]\nExecStart=/usr/bin/zita-j2n --chan 2 --24bit "
                        "${A3_ZITA_J2N_HOST} ${A3_ZITA_J2N_PORT}\n",
    "zita-n2j.service": "[Service]\nExecStart=/usr/bin/zita-n2j --chan 1-10 --buff 20 "
                        "--info ${A3_ZITA_N2J_HOST} ${A3_ZITA_N2J_PORT}\n",
}
STEMDECKS = ("[Service]\nExecStart=/usr/bin/python3 /usr/lib/stemdeck/zita-from-truth.py "
             "j2n --chan 10 --24bit\n")


def postinst_function(name):
    body = re.search(rf"^{name}\(\) \{{.*?^\}}$", POSTINST.read_text(),
                     re.MULTILINE | re.DOTALL)
    if body is None:
        raise AssertionError(f"postinst defines no {name}()")
    return body.group(0)


class TheOldUnitsAreRetired(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.config, self.backup = root / "config", root / "backup"
        self.units = self.config / "systemd/user"
        (self.units / "default.target.wants").mkdir(parents=True)

    def tearDown(self):
        self.tmp.cleanup()

    def unit(self, name, text):
        (self.units / name).write_text(text)
        (self.units / "default.target.wants" / name).symlink_to(self.units / name)

    def retire(self, check=True):
        return subprocess.run(
            ["sh", "-c", f"set -e\n{postinst_function('retire_jack_and_zita_units')}\n"
             'RETIRED_UNITS=""\nretire_jack_and_zita_units "$1" "$2"\n'
             'echo "retired:$RETIRED_UNITS"\necho went-on', "sh",
             str(self.config), str(self.backup)],
            capture_output=True, text=True, check=check)

    def test_the_cores_units_go_to_the_backup_and_are_disabled(self):
        for name, text in CORE_UNITS.items():
            self.unit(name, text)
        done = self.retire()
        for name, text in CORE_UNITS.items():
            with self.subTest(unit=name):
                self.assertFalse((self.units / name).exists())
                self.assertFalse((self.units / "default.target.wants" / name).is_symlink())
                self.assertEqual((self.backup / "systemd/user" / name).read_text(), text)
                self.assertIn(name, done.stdout)
        retired = done.stdout.split("retired:", 1)[1].split("\n", 1)[0].split()
        self.assertEqual(sorted(retired), sorted(CORE_UNITS))

    def test_a_link_in_another_wants_folder_goes_too(self):
        self.unit("a3-jack.service", CORE_UNITS["a3-jack.service"])
        (self.units / "a3-main.service.wants").mkdir()
        (self.units / "a3-main.service.wants/a3-jack.service").symlink_to(
            self.units / "a3-jack.service")
        self.retire()
        self.assertFalse((self.units / "a3-main.service.wants/a3-jack.service").is_symlink())

    def test_stemdecks_zita_unit_stays_and_is_named(self):
        self.unit("zita-j2n.service", STEMDECKS)
        done = self.retire()
        self.assertEqual((self.units / "zita-j2n.service").read_text(), STEMDECKS)
        self.assertTrue((self.units / "default.target.wants/zita-j2n.service").is_symlink())
        self.assertIn("kept", done.stdout)
        self.assertIn("retired:\n", done.stdout)

    def test_drop_ins_beside_them_are_left_alone(self):
        self.unit("a3-jack.service", CORE_UNITS["a3-jack.service"])
        drop_in = self.units / "a3-jack.service.d/zzz-mine.conf"
        drop_in.parent.mkdir()
        drop_in.write_text("[Unit]\n")
        self.retire()
        self.assertEqual(drop_in.read_text(), "[Unit]\n")

    def test_a_failed_move_warns_and_the_install_goes_on(self):
        self.unit("a3-jack.service", CORE_UNITS["a3-jack.service"])
        self.backup.write_text("a file where the backup folder should be")
        done = self.retire(check=False)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn("went-on", done.stdout)
        self.assertTrue((self.units / "a3-jack.service").exists())
        self.assertIn("could not", done.stderr)

    def test_nothing_there_is_nothing_to_do(self):
        done = self.retire()
        self.assertFalse(self.backup.exists())
        self.assertEqual(done.stdout, "retired:\nwent-on\n")


class WhatStillRunsIsStopped(unittest.TestCase):
    def stop(self, retired):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "log"
            subprocess.run(
                ["sh", "-c", f'set -e\nuser_systemctl() {{ echo "$*" >> {log}; }}\n'
                 f"{postinst_function('stop_retired_units')}\n"
                 'RETIRED_UNITS="$1"\nstop_retired_units', "sh", retired],
                check=True, capture_output=True, text=True)
            return log.read_text() if log.exists() else ""

    def test_the_retired_units_are_stopped(self):
        self.assertEqual(self.stop("a3-jack.service zita-n2j.service"),
                         "stop a3-jack.service zita-n2j.service\n")

    def test_none_retired_stops_nothing(self):
        self.assertEqual(self.stop(""), "")

    def test_the_postinst_retires_before_it_reloads_and_stops_with_the_manager_up(self):
        text = POSTINST.read_text()
        self.assertRegex(text, r'(?m)^retire_jack_and_zita_units "\$\{USER_HOME\}/\.config" "\$CONFIG_BACKUP"$')
        self.assertIn("stop_retired_units", postinst_function("run_user_install"))


if __name__ == "__main__":
    unittest.main()
