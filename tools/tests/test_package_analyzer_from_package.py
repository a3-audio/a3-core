"""The beat-analyzer comes from its own package (2026-10-08).

It used to be built by a3-user-install.service inside the checkout, the same
folder its unit started it from: a rebuild took the service down, and its
config sat in build/. The beat-analyzer package stands alone (people use it
without Core) and has a generic unit; what is A3 about it -- the ordering
against a3-main and JACK, the CPUs, the OSC targets -- is a3-core's, as a
drop-in and a conf.d file. The unit a3-core used to ship in ~/.config would
hide the package's, so the postinst moves it to the backup.
"""

import configparser
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config/debian-x86_64/a3-core"
UNITS = PACKAGE / "home/aaa/.local/share/a3-core/config/systemd/user"
DROP_IN = UNITS / "beat-analyzer.service.d/a3-core.conf"
USER_INSTALL = PACKAGE / "home/aaa/.local/share/a3-core/recipes/user_install.sh"
CONTROL = PACKAGE / "DEBIAN/control"
POSTINST = PACKAGE / "DEBIAN/postinst"

OLD_UNIT = """[Unit]
Description=beat-analyzer (bpm & vu-meters)
PartOf=a3-main.service

[Service]
WorkingDirectory=/home/aaa/a3-system/beat-analyzer/build
ExecStart=/home/aaa/a3-system/beat-analyzer/build/beat-analyzer
"""


def section(path, name):
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.optionxform = str
    parser.read_string(path.read_text())
    return parser[name]


def control_field(name):
    match = re.search(rf"^{name}: (.*)$", CONTROL.read_text(), re.MULTILINE)
    return [part.strip() for part in match.group(1).split(",")] if match else []


def postinst_function(name):
    body = re.search(rf"^{name}\(\) \{{.*?^\}}$", POSTINST.read_text(), re.MULTILINE | re.DOTALL)
    if body is None:
        raise AssertionError(f"postinst defines no {name}()")
    return body.group(0)


class TheUnitIsThePackages(unittest.TestCase):
    def test_a3_core_ships_no_unit_of_its_own(self):
        self.assertFalse((UNITS / "beat-analyzer.service").exists())

    def test_the_drop_in_ties_it_to_the_rig(self):
        unit = section(DROP_IN, "Unit")
        self.assertEqual(unit.get("PartOf"), "a3-main.service")
        self.assertIn("a3-jack.service", unit.get("After", ""))

    def test_the_drop_in_keeps_its_cpus_and_raises_nothing(self):
        service = section(DROP_IN, "Service")
        self.assertEqual(service.get("CPUAffinity"), "1 2 3")
        for key in ("CPUSchedulingPolicy", "CPUSchedulingPriority", "LimitRTPRIO"):
            self.assertNotIn(key, service)

    def test_a3_main_still_pulls_it_in(self):
        wants = section(UNITS / "a3-main.service", "Unit").get("Wants", "")
        self.assertIn("beat-analyzer.service", wants.split())


class NothingBuildsTheCheckout(unittest.TestCase):
    def test_the_user_install_does_not_build_the_analyzer(self):
        text = USER_INSTALL.read_text()
        self.assertNotIn("build.sh", text)
        self.assertNotIn("beat-analyzer/build", text)


class ThePackageIsAsked(unittest.TestCase):
    def test_recommended_not_depended_on(self):
        """a3-core is also on the GitHub apt repo, which has no beat-analyzer:
        a Depends would make a3-core uninstallable there."""
        self.assertIn("beat-analyzer", control_field("Recommends"))
        self.assertNotIn("beat-analyzer", control_field("Depends"))


class TheOldUnitIsRetired(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.config, self.backup = root / "config", root / "backup"
        self.unit = self.config / "systemd/user/beat-analyzer.service"
        self.unit.parent.mkdir(parents=True)
        self.package_unit = root / "usr-lib/beat-analyzer.service"
        self.package_unit.parent.mkdir(parents=True)
        self.package_unit.write_text("[Service]\nExecStart=/usr/bin/beat-analyzer\n")

    def tearDown(self):
        self.tmp.cleanup()

    def retire(self, check=True):
        return subprocess.run(
            ["sh", "-c", f"set -e\n{postinst_function('retire_checkout_analyzer_unit')}\n"
             'retire_checkout_analyzer_unit "$1" "$2" "$3"\necho went-on', "sh",
             str(self.config), str(self.backup), str(self.package_unit)],
            capture_output=True, text=True, check=check)

    def test_the_checkout_unit_goes_to_the_backup(self):
        self.unit.write_text(OLD_UNIT)
        done = self.retire()
        self.assertFalse(self.unit.exists())
        self.assertEqual((self.backup / "systemd/user/beat-analyzer.service").read_text(), OLD_UNIT)
        self.assertIn("beat-analyzer.service", done.stdout)

    def test_a_unit_of_somebody_elses_stays_and_is_named(self):
        self.unit.write_text("[Service]\nExecStart=/opt/mine/beat-analyzer\n")
        done = self.retire()
        self.assertTrue(self.unit.exists())
        self.assertFalse(self.backup.exists())
        self.assertIn("kept", done.stdout)

    def test_without_the_package_the_checkout_unit_stays(self):
        """Without the package's unit, moving the old one away leaves a3-main
        wanting a unit that no longer exists: no /beat, no meters."""
        self.unit.write_text(OLD_UNIT)
        self.package_unit.unlink()
        done = self.retire()
        self.assertEqual(self.unit.read_text(), OLD_UNIT)
        self.assertFalse(self.backup.exists())
        self.assertIn("not installed", done.stdout)

    def test_the_postinst_looks_for_the_package_unit_where_it_is_installed(self):
        self.assertIn("/usr/lib/systemd/user/beat-analyzer.service", POSTINST.read_text())

    def test_a_failed_move_warns_and_the_install_goes_on(self):
        self.unit.write_text(OLD_UNIT)
        self.backup.write_text("a file where the backup folder should be")
        done = self.retire(check=False)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn("went-on", done.stdout)
        self.assertTrue(self.unit.exists())
        self.assertIn("could not", done.stderr)

    def test_no_unit_is_nothing_to_do(self):
        done = self.retire()
        self.assertFalse(self.backup.exists())
        self.assertEqual(done.stdout, "went-on\n")


if __name__ == "__main__":
    unittest.main()
